/* extaddr.c - the host's internet address for the lobby. See extaddr.h. Runs on its own thread; the lobby reads the
 * cached result with extaddr_get each refresh. Plain sockets for UPnP (LAN, http only); HTTPS through the system
 * (Mac /usr/bin/curl like the loader's updater, Windows WinHTTP). */
#include "extaddr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#ifdef _WIN32
#include <winhttp.h>
#define sk_close(s) closesocket((SOCKET)(s))
#else
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define sk_close(s) close(s)
extern char **environ;
#endif

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int st = EXTADDR_IDLE, gen;
static char res_ip[64]; static const char *res_via = "";

/* ---------------------------------------------------------------- pure helpers */
int extaddr_is_private(uint32_t a) {
    return (a >> 24) == 10 || (a >> 20) == (172u << 4 | 1) || (a >> 16) == (192u << 8 | 168) || (a >> 22) == (100u << 2 | 1)
        || (a >> 24) == 127 || (a >> 16) == (169u << 8 | 254) || a == 0;
}
static int ipv4(const char *s, uint32_t *out) {          /* dotted IPv4 only */
    struct in_addr a; if (inet_pton(AF_INET, s, &a) != 1) return 0;
    *out = ntohl(a.s_addr); return 1;
}
static void trim_copy(char *dst, size_t cap, const char *s, size_t n) {
    while (n && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
    if (n >= cap) n = cap - 1;
    memcpy(dst, s, n); dst[n] = 0;
}
int extaddr_ssdp_location(const char *resp, char *url, size_t cap) {
    for (const char *l = resp; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL)
        if (!strncasecmp(l, "LOCATION:", 9)) {
            const char *v = l + 9; size_t n = strcspn(v, "\r\n");
            trim_copy(url, cap, v, n);
            return !strncmp(url, "http://", 7);
        }
    return 0;
}
/* text of the first <tag>...</tag> at or after p, before lim (NULL = no limit) */
static const char *tag_text(const char *p, const char *lim, const char *tag, char *out, size_t cap) {
    char o[48], c[48]; snprintf(o, sizeof o, "<%s>", tag); snprintf(c, sizeof c, "</%s>", tag);
    const char *a = strstr(p, o); if (!a || (lim && a > lim)) return NULL;
    a += strlen(o); const char *b = strstr(a, c); if (!b || (lim && b > lim)) return NULL;
    trim_copy(out, cap, a, (size_t)(b - a)); return b;
}
/* "http://h:p/x" + relative "/ctl" -> "http://h:p/ctl" */
static void join_url(const char *base, const char *rel, char *out, size_t cap) {
    if (!strncmp(rel, "http://", 7)) { snprintf(out, cap, "%s", rel); return; }
    const char *h = strstr(base, "://"); h = h ? h + 3 : base;
    size_t root = (size_t)(h - base) + strcspn(h, "/");
    snprintf(out, cap, "%.*s%s%s", (int)root, base, rel[0] == '/' ? "" : "/", rel);
}
int extaddr_control_url(const char *xml, const char *base, char *url, size_t cap, char *svc, size_t svc_cap) {
    char ub[256] = "";
    if (tag_text(xml, NULL, "URLBase", ub, sizeof ub) && ub[0]) base = ub;
    for (const char *p = xml; (p = strstr(p, "<serviceType>")); p++) {
        char t[160]; const char *e = tag_text(p, NULL, "serviceType", t, sizeof t); if (!e) return 0;
        if (!strstr(t, ":WANIPConnection:") && !strstr(t, ":WANPPPConnection:")) continue;
        const char *end = strstr(e, "</service>");
        char cu[256]; if (!tag_text(e, end, "controlURL", cu, sizeof cu) || !cu[0]) continue;
        join_url(base, cu, url, cap); snprintf(svc, svc_cap, "%s", t);
        return 1;
    }
    return 0;
}
int extaddr_soap_ip(const char *resp, char *ip, size_t cap) {
    const char *p = strstr(resp, "NewExternalIPAddress"); if (!p) return 0;
    p = strchr(p, '>'); if (!p) return 0; p++;
    const char *e = strchr(p, '<'); if (!e) return 0;
    trim_copy(ip, cap, p, (size_t)(e - p));
    uint32_t a; return ipv4(ip, &a);
}

/* ---------------------------------------------------------------- plain sockets (LAN) */
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int wait_fd(int s, short ev, double deadline) {
    double left = deadline - now_ms(); if (left <= 0) return 0;
    struct pollfd p = { .fd = s, .events = ev }; return poll(&p, 1, (int)left) > 0;
}
static int split_url(const char *url, char *host, size_t hcap, int *port, const char **path) {
    if (strncmp(url, "http://", 7)) return 0;
    const char *h = url + 7; size_t n = strcspn(h, ":/"); if (!n || n >= hcap) return 0;
    memcpy(host, h, n); host[n] = 0; *port = 80;
    if (h[n] == ':') *port = atoi(h + n + 1);
    const char *pa = strchr(h, '/'); *path = pa ? pa : "/";
    return *port > 0 && *port < 65536;
}
/* one HTTP/1.0 exchange; the body (after the headers) into out, also for a non-2xx answer (a SOAP fault comes as 500).
   Returns the HTTP status code, 0 = no answer. */
int extaddr_http(const char *url, const char *extra_hdr, const char *body, char *out, size_t cap, int ms) {
    char host[64]; int port; const char *path; uint32_t ip;
    if (cap) out[0] = 0;
    if (!split_url(url, host, sizeof host, &port, &path) || !ipv4(host, &ip)) return 0;   /* routers give IPs */
    int s = (int)socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return 0;
    double dl = now_ms() + ms; int code = 0, got = 0;
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port), .sin_addr.s_addr = htonl(ip) };
    if (connect(s, (struct sockaddr *)&a, sizeof a) && errno != EINPROGRESS) goto done;
    if (!wait_fd(s, POLLOUT, dl)) goto done;
    { int e = 0; socklen_t el = sizeof e; if (getsockopt(s, SOL_SOCKET, SO_ERROR, &e, &el) || e) goto done; }
    char req[2048]; size_t bl = body ? strlen(body) : 0;
    int n = snprintf(req, sizeof req, "%s %s HTTP/1.0\r\nHost: %s:%d\r\n%s%s\r\n", body ? "POST" : "GET", path, host, port,
                     extra_hdr ? extra_hdr : "", "Connection: close\r\n");
    if (n <= 0 || (size_t)n >= sizeof req) goto done;
    char *msg = malloc((size_t)n + bl + 1); if (!msg) goto done;
    memcpy(msg, req, (size_t)n); if (bl) memcpy(msg + n, body, bl);
    size_t tot = (size_t)n + bl, sent = 0;
    while (sent < tot) {
        if (!wait_fd(s, POLLOUT, dl)) break;
        int k = (int)send(s, msg + sent, (int)(tot - sent), 0); if (k <= 0) break; sent += (size_t)k;
    }
    free(msg); if (sent < tot) goto done;
    while ((size_t)got < cap - 1) {
        if (!wait_fd(s, POLLIN, dl)) break;
        int k = (int)recv(s, out + got, (int)(cap - 1 - got), 0); if (k <= 0) break; got += k;
    }
    out[got] = 0;
    { const char *sp = strchr(out, ' '); char *b = strstr(out, "\r\n\r\n");
      if (b && sp) { code = atoi(sp + 1); memmove(out, b + 4, strlen(b + 4) + 1); } }
done:
    sk_close(s); if (!code && cap) out[0] = 0;
    return code;
}
static int http_ex(const char *url, const char *extra_hdr, const char *body, char *out, size_t cap, int ms) {   /* 1 = 2xx */
    int code = extaddr_http(url, extra_hdr, body, out, cap, ms);
    if (code < 200 || code >= 300) { if (cap) out[0] = 0; return 0; }
    return 1;
}
int extaddr_soap(const char *ctl, const char *svc, const char *action, const char *args, char *out, size_t cap, int ms) {
    char hdr[400], body[1600];
    int bl = snprintf(body, sizeof body, "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s"
                      "</u:%s></s:Body></s:Envelope>\r\n", action, svc, args ? args : "", action);
    if (bl <= 0 || (size_t)bl >= sizeof body) return 0;
    snprintf(hdr, sizeof hdr, "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"%s#%s\"\r\nContent-Length: %d\r\n", svc, action, bl);
    return extaddr_http(ctl, hdr, body, out, cap, ms);
}
/* SSDP search for the router, then its device XML: the WAN connection's control URL + service type. 1 = found */
int extaddr_igd_find(char *ctl, size_t ctl_cap, char *svc, size_t svc_cap) {
    int s = (int)socket(AF_INET, SOCK_DGRAM, 0); if (s < 0) return 0;
    unsigned char ttl = 2; setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    struct sockaddr_in g = { .sin_family = AF_INET, .sin_port = htons(1900) }; inet_pton(AF_INET, "239.255.255.250", &g.sin_addr);
    static const char *sts[] = { "urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:service:WANIPConnection:1",
                                 "urn:schemas-upnp-org:service:WANPPPConnection:1" };
    for (int k = 0; k < 3; k++) {
        char m[256]; int n = snprintf(m, sizeof m, "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n", sts[k]);
        sendto(s, m, n, 0, (struct sockaddr *)&g, sizeof g);
    }
    char locs[4][256]; int nl = 0; double dl = now_ms() + 2000;
    while (nl < 4 && wait_fd(s, POLLIN, dl)) {
        char r[1500]; int k = (int)recv(s, r, sizeof r - 1, 0); if (k <= 0) break; r[k] = 0;
        char u[256]; if (!extaddr_ssdp_location(r, u, sizeof u)) continue;
        int dup = 0; for (int i = 0; i < nl; i++) dup |= !strcmp(locs[i], u);
        if (!dup) snprintf(locs[nl++], sizeof locs[0], "%s", u);
        if (nl && now_ms() + 1500 < dl) dl = now_ms() + 300;               /* first answer: wait only a little for more */
    }
    sk_close(s);
    char *buf = malloc(65536); if (!buf) return 0;
    int found = 0;
    for (int i = 0; i < nl && !found; i++)
        found = http_ex(locs[i], NULL, NULL, buf, 65536, 2000) && extaddr_control_url(buf, locs[i], ctl, ctl_cap, svc, svc_cap);
    free(buf);
    return found;
}
static int upnp_lookup(char *ip, size_t cap) {
    char cu[512], svc[160], buf[4096];
    if (!extaddr_igd_find(cu, sizeof cu, svc, sizeof svc)) return 0;
    int code = extaddr_soap(cu, svc, "GetExternalIPAddress", NULL, buf, sizeof buf, 2000);
    return code >= 200 && code < 300 && extaddr_soap_ip(buf, ip, cap);
}

/* ---------------------------------------------------------------- HTTPS fallback (3 s) */
#ifdef _WIN32
static int web_get(const char *url, char *out, size_t cap) {
    wchar_t w[512], host[256], path[512]; int ok = 0; DWORD got = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, w, 512)) return 0;
    URL_COMPONENTS uc = { sizeof uc }; uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 512;
    if (!WinHttpCrackUrl(w, 0, 0, &uc)) return 0;
    HINTERNET ses = WinHttpOpen(L"DungeonSettlersModded", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), con = NULL, req = NULL;
    if (!ses) return 0;
    WinHttpSetTimeouts(ses, 3000, 3000, 3000, 3000);
    if (!(con = WinHttpConnect(ses, host, uc.nPort, 0))) goto done;
    if (!(req = WinHttpOpenRequest(con, L"GET", uc.dwUrlPathLength ? path : L"/", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0))) goto done;
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req, NULL)) goto done;
    { DWORD code = 0, sz = sizeof code;
      if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX) || code / 100 != 2) goto done; }
    for (DWORD k = 0; got < cap - 1 && WinHttpReadData(req, out + got, (DWORD)(cap - 1 - got), &k) && k; ) got += k;
    ok = 1;
done:
    out[ok ? got : 0] = 0;
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}
#else
static int web_get(const char *url, char *out, size_t cap) {
    int p[2]; if (pipe(p)) return 0;
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, p[1], 1); posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&fa, p[0]); posix_spawn_file_actions_addclose(&fa, p[1]);
    char *av[] = { "/usr/bin/curl", "-fsS", "--max-time", "3", "--connect-timeout", "3", "-A", "DungeonSettlersModded", (char *)url, NULL };
    pid_t pid; int r = posix_spawn(&pid, av[0], &fa, NULL, av, environ);
    posix_spawn_file_actions_destroy(&fa); close(p[1]);
    size_t got = 0; ssize_t k;
    if (!r) while (got < cap - 1 && (k = read(p[0], out + got, cap - 1 - got)) > 0) got += (size_t)k;
    out[got] = 0; close(p[0]);
    int status = 0; if (!r) while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return !r && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

/* ---------------------------------------------------------------- worker */
static int env_off(const char *n) { const char *v = getenv(n); return v && !strcmp(v, "0"); }
static void *worker(void *arg) {
    int my = (int)(intptr_t)arg, state = EXTADDR_UNKNOWN; const char *via = ""; char ip[64] = "";
#ifdef _WIN32
    { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
#endif
    uint32_t a;
    if (!env_off("DSRIG_EXTADDR_UPNP") && upnp_lookup(ip, sizeof ip) && ipv4(ip, &a) && a) {
        via = "upnp"; state = extaddr_is_private(a) ? EXTADDR_PRIVATE : EXTADDR_OK;
    } else if (!env_off("DSRIG_EXTADDR_WEB")) {
        const char *u = getenv("DSRIG_EXTADDR_WEB_URL"); char b[128];
        if (web_get(u && u[0] ? u : "https://api.ipify.org", b, sizeof b)) {
            trim_copy(ip, sizeof ip, b, strlen(b));
            if (ipv4(ip, &a) && !extaddr_is_private(a)) { via = "web"; state = EXTADDR_OK; }
        }
    }
    if (state == EXTADDR_UNKNOWN) ip[0] = 0;
    pthread_mutex_lock(&mu);
    if (my == gen) { st = state; res_via = via; snprintf(res_ip, sizeof res_ip, "%s", ip); }
    pthread_mutex_unlock(&mu);
    return NULL;
}
void extaddr_start(void) {
    pthread_mutex_lock(&mu);
    if (st == EXTADDR_IDLE) {
        pthread_t t; st = EXTADDR_PENDING;
        if (pthread_create(&t, NULL, worker, (void *)(intptr_t)gen) == 0) pthread_detach(t);
        else st = EXTADDR_UNKNOWN;
    }
    pthread_mutex_unlock(&mu);
}
void extaddr_reset(void) {
    pthread_mutex_lock(&mu);
    if (st != EXTADDR_IDLE) { gen++; st = EXTADDR_IDLE; res_ip[0] = 0; res_via = ""; }
    pthread_mutex_unlock(&mu);
}
int extaddr_get(char *ip, size_t cap, const char **via) {
    pthread_mutex_lock(&mu);
    int s = st; if (ip && cap) snprintf(ip, cap, "%s", res_ip); if (via) *via = res_via;
    pthread_mutex_unlock(&mu);
    return s;
}
