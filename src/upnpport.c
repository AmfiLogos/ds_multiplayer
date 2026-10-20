/* upnpport.c - the host's router port (UPnP port mapping). See upnpport.h. One worker thread; the game thread only sets
 * wishes (the host port) and reads the cached result. Router plumbing (SSDP, SOAP) is in extaddr.c. */
#include "upnpport.h"
#include "extaddr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>
#ifdef _WIN32
#define up_close(s) closesocket((SOCKET)(s))
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define up_close(s) close(s)
#endif

void rig_log(const char *fmt, ...);

#define UP_DESC "Dungeon Settlers host"
#define UP_MAX_MAPS 64

/* ---------------------------------------------------------------- pure helpers */
/* text of the first <...name ...>text< in s (the opening tag; namespace prefix allowed) */
static int field(const char *s, const char *name, char *out, size_t cap) {
    size_t nl = strlen(name);
    for (const char *p = s; (p = strstr(p, name)); p += nl) {
        if (p == s || (p[-1] != '<' && p[-1] != ':')) continue;
        if (p[-1] == ':') { const char *q = p - 1; while (q > s && q[-1] != '<' && q[-1] != '/' && q[-1] != '>' && q[-1] != ' ') q--; if (q == s || q[-1] != '<') continue; }
        const char *a = p + nl; if (*a != '>' && *a != ' ') continue;
        a = strchr(a, '>'); if (!a) return 0; a++;
        const char *e = strchr(a, '<'); if (!e) return 0;
        while (a < e && (*a == ' ' || *a == '\r' || *a == '\n' || *a == '\t')) a++;
        size_t n = (size_t)(e - a); while (n && (a[n - 1] == ' ' || a[n - 1] == '\r' || a[n - 1] == '\n' || a[n - 1] == '\t')) n--;
        if (n >= cap) n = cap - 1;
        memcpy(out, a, n); out[n] = 0; return 1;
    }
    return 0;
}
int upnpport_soap_error(const char *body) {
    char t[16]; return body && field(body, "errorCode", t, sizeof t) ? atoi(t) : 0;
}
int upnpport_parse_entry(const char *soap, upnpp_map *m) {
    char t[128];
    if (!soap || upnpport_soap_error(soap)) return 0;
    if (field(soap, "NewProtocol", t, sizeof t)) snprintf(m->proto, sizeof m->proto, "%s", t);
    if (field(soap, "NewExternalPort", t, sizeof t)) m->ext = (uint16_t)atoi(t);
    m->enabled = field(soap, "NewEnabled", t, sizeof t) ? atoi(t) != 0 : 1;
    m->desc[0] = 0; if (field(soap, "NewPortMappingDescription", t, sizeof t)) snprintf(m->desc, sizeof m->desc, "%s", t);
    m->lease = field(soap, "NewLeaseDuration", t, sizeof t) ? (unsigned)strtoul(t, NULL, 10) : 0;
    if (!field(soap, "NewInternalClient", m->client, sizeof m->client) || !field(soap, "NewInternalPort", t, sizeof t)) return 0;
    int in = atoi(t); if (in <= 0 || in > 65535) return 0;
    m->in = (uint16_t)in;
    return 1;
}
static int usable(const upnpp_map *m, const char *lan) {
    return !strcasecmp(m->proto, "TCP") && m->enabled && m->ext && m->in >= 1024 && !strcmp(m->client, lan);
}
int upnpport_choose(const upnpp_map *m, int n, const char *lan, uint16_t want) {
    for (int i = 0; i < n; i++) if (usable(&m[i], lan) && m[i].in == want) return i;
    return -1;
}

/* ---------------------------------------------------------------- state (mu) */
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int started, state = UPNPP_IDLE;
static uint16_t want_port, cur_port, ext_port;
/* the mapping this process added (only that one is ever deleted) */
static uint16_t own_ext, own_in; static unsigned own_lease; static double renew_at;
static char own_ctl[512], own_svc[160], own_lan[48];

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static int env_is(const char *n, const char *v) { const char *e = getenv(n); return e && !strcmp(e, v); }
static int upnp_enabled(void) {
    if (env_is("DSRIG_UPNP", "0")) return 0;
    if (env_is("DSRIG_UPNP", "1")) return 1;
    return !env_is("DSRIG_CTL", "1") && !getenv("DSRIG_PORT");        /* test copies: off unless asked (never touch the router) */
}

/* ---------------------------------------------------------------- router calls (worker thread, unlocked) */
typedef struct { char ctl[512], svc[160], lan[48]; int ok; } igd_t;
static int local_addr_to(const char *ctl, char *out, size_t cap) {    /* the address this computer uses toward the router */
    const char *h = strstr(ctl, "://"); if (!h) return 0; h += 3;
    char host[64]; size_t n = strcspn(h, ":/"); if (!n || n >= sizeof host) return 0;
    memcpy(host, h, n); host[n] = 0;
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(1900) };
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) return 0;
    int s = (int)socket(AF_INET, SOCK_DGRAM, 0); if (s < 0) return 0;
    int ok = 0; struct sockaddr_in me; socklen_t ml = sizeof me;
    if (!connect(s, (struct sockaddr *)&a, sizeof a) && !getsockname(s, (struct sockaddr *)&me, &ml))
        ok = inet_ntop(AF_INET, &me.sin_addr, out, (socklen_t)cap) != NULL;
    up_close(s);
    return ok;
}
static int igd_get(igd_t *g) {
    if (g->ok) return 1;
    g->ok = extaddr_igd_find(g->ctl, sizeof g->ctl, g->svc, sizeof g->svc) && local_addr_to(g->ctl, g->lan, sizeof g->lan);
    return g->ok;
}
static int list_maps(const igd_t *g, upnpp_map *m, int cap) {
    char buf[4096], args[96]; int n = 0;
    for (int i = 0; i < 256 && n < cap; i++) {
        snprintf(args, sizeof args, "<NewPortMappingIndex>%d</NewPortMappingIndex>", i);
        int code = extaddr_soap(g->ctl, g->svc, "GetGenericPortMappingEntry", args, buf, sizeof buf, 2000);
        if (code < 200 || code >= 300) break;                           /* 713 SpecifiedArrayIndexInvalid = end of the list */
        upnpp_map e; memset(&e, 0, sizeof e);
        if (upnpport_parse_entry(buf, &e)) m[n++] = e;
    }
    return n;
}
static int get_specific(const igd_t *g, uint16_t ext, upnpp_map *m) {
    char buf[4096], args[200];
    snprintf(args, sizeof args, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>TCP</NewProtocol>", ext);
    int code = extaddr_soap(g->ctl, g->svc, "GetSpecificPortMappingEntry", args, buf, sizeof buf, 2000);
    memset(m, 0, sizeof *m); snprintf(m->proto, sizeof m->proto, "TCP"); m->ext = ext;
    return code >= 200 && code < 300 && upnpport_parse_entry(buf, m);
}
/* AddPortMapping; returns 0 = done, else the UPnP error code (-1 = no answer) */
static int add_map(const char *ctl, const char *svc, const char *lan, uint16_t ext, uint16_t in, unsigned lease) {
    char buf[4096], args[600];
    snprintf(args, sizeof args, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>TCP</NewProtocol>"
             "<NewInternalPort>%u</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
             "<NewPortMappingDescription>" UP_DESC "</NewPortMappingDescription><NewLeaseDuration>%u</NewLeaseDuration>", ext, in, lan, lease);
    int code = extaddr_soap(ctl, svc, "AddPortMapping", args, buf, sizeof buf, 3000);
    if (code >= 200 && code < 300) return 0;
    int e = upnpport_soap_error(buf); return e ? e : -1;
}
static int del_map(const char *ctl, const char *svc, uint16_t ext) {
    char buf[4096], args[200];
    snprintf(args, sizeof args, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>TCP</NewProtocol>", ext);
    int code = extaddr_soap(ctl, svc, "DeletePortMapping", args, buf, sizeof buf, 2000);
    if (code >= 200 && code < 300) return 0;
    int e = upnpport_soap_error(buf); return e ? e : -1;
}
static unsigned lease_wanted(void) {
    const char *e = getenv("DSRIG_UPNP_LEASE"); long v = e ? strtol(e, NULL, 10) : 0;
    return v >= 10 && v <= 86400 ? (unsigned)v : 86400;
}
static void delete_own(const char *why) {                              /* caller: unlocked; takes the record under mu */
    pthread_mutex_lock(&mu);
    uint16_t ext = own_ext; char ctl[512], svc[160]; snprintf(ctl, sizeof ctl, "%s", own_ctl); snprintf(svc, sizeof svc, "%s", own_svc);
    own_ext = 0; own_lease = 0;
    pthread_mutex_unlock(&mu);
    if (!ext) return;
    int e = del_map(ctl, svc, ext);
    rig_log("upnpport: deleted the router mapping %u (%s): %s", ext, why, e == 0 ? "done" : "failed");
    if (e) rig_log("upnpport: delete error %d", e);
}

/* hosting on p: find or add the mapping; sets state / ext_port / own_* */
static void open_port(igd_t *g, uint16_t p) {
    int st = UPNPP_FAILED; uint16_t ext = 0;
    if (!igd_get(g)) { rig_log("upnpport: port %u: no router answered (UPnP off or not supported)", p); goto out; }
    upnpp_map *m = calloc(UP_MAX_MAPS, sizeof *m); if (!m) goto out;
    int n = list_maps(g, m, UP_MAX_MAPS), mine = 0;
    for (int i = 0; i < n; i++) mine += usable(&m[i], g->lan);
    rig_log("upnpport: port %u: router lists %d mappings, %d TCP to this computer", p, n, mine);
    int k = env_is("DSRIG_UPNP_SKIP_EXISTING", "1") ? -1 : upnpport_choose(m, n, g->lan, p);
    if (k >= 0) {
        st = UPNPP_ALREADY; ext = m[k].ext;
        rig_log("upnpport: port %u: the router already forwards external %u to it", p, ext);
        free(m); goto out;
    }
    uint16_t cand[8]; int nc = 0;
    for (int i = 0; i < 5 && p + i <= 65535; i++) cand[nc++] = (uint16_t)(p + i);
    for (int i = 0; nc < 8; i++) cand[nc++] = (uint16_t)(20000 + (p * 7919u + (unsigned)i * 104729u) % 40000);
    unsigned lease = lease_wanted();
    for (int i = 0; i < nc; i++) {
        int taken = 0;
        for (int j = 0; j < n; j++) taken |= !strcasecmp(m[j].proto, "TCP") && m[j].ext == cand[i];
        if (taken) { rig_log("upnpport: external %u is taken on the router", cand[i]); continue; }
        int e = add_map(g->ctl, g->svc, g->lan, cand[i], p, lease);
        if (e == 725) { lease = 0; e = add_map(g->ctl, g->svc, g->lan, cand[i], p, 0); }   /* OnlyPermanentLeasesSupported */
        if (e == 718 || e == 724) { rig_log("upnpport: external %u refused (%d), next", cand[i], e); continue; }   /* conflict / ports must match */
        if (e) { rig_log("upnpport: AddPortMapping refused (%d)", e); break; }
        upnpp_map chk;
        if (!get_specific(g, cand[i], &chk) || chk.in != p || strcmp(chk.client, g->lan)) {
            rig_log("upnpport: external %u: the router said yes but does not list it", cand[i]); break;
        }
        pthread_mutex_lock(&mu);
        own_ext = cand[i]; own_in = p; own_lease = lease; renew_at = lease ? now_s() + lease / 2.0 : 0;
        snprintf(own_ctl, sizeof own_ctl, "%s", g->ctl); snprintf(own_svc, sizeof own_svc, "%s", g->svc); snprintf(own_lan, sizeof own_lan, "%s", g->lan);
        pthread_mutex_unlock(&mu);
        st = UPNPP_OPENED; ext = cand[i];
        rig_log("upnpport: port %u: opened on the router as external %u (lease %u s, listed: yes)", p, ext, lease);
        break;
    }
    free(m);
out:
    pthread_mutex_lock(&mu);
    if (want_port == p) { state = st; ext_port = ext; }
    pthread_mutex_unlock(&mu);
}
static void renew_own(void) {
    pthread_mutex_lock(&mu);
    uint16_t ext = own_ext, in = own_in; unsigned lease = own_lease; char ctl[512], svc[160], lan[48];
    snprintf(ctl, sizeof ctl, "%s", own_ctl); snprintf(svc, sizeof svc, "%s", own_svc); snprintf(lan, sizeof lan, "%s", own_lan);
    pthread_mutex_unlock(&mu);
    if (!ext || !lease) return;
    int e = add_map(ctl, svc, lan, ext, in, lease);
    pthread_mutex_lock(&mu);
    if (own_ext == ext) renew_at = now_s() + (e ? 60 : lease / 2.0);  /* failed: try again in a minute */
    pthread_mutex_unlock(&mu);
    rig_log("upnpport: renewed the router mapping %u (lease %u s): %s", ext, lease, e ? "failed" : "done");
    if (e) rig_log("upnpport: renew error %d", e);
}
static void at_exit(void) { delete_own("game exit"); }
void upnpport_quit(void) { if (started > 0) delete_own("game quit"); }   /* Application.Quit (mp_game.c), game thread: up to 2 s */
static void *worker(void *arg) {
    (void)arg;
#ifdef _WIN32
    { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
#endif
    igd_t g; memset(&g, 0, sizeof g);
    pthread_mutex_lock(&mu);
    for (;;) {
        if (want_port != cur_port) {
            uint16_t p = want_port; cur_port = p;
            pthread_mutex_unlock(&mu);
            delete_own(p ? "port changed" : "session ended");
            if (p) open_port(&g, p);
            pthread_mutex_lock(&mu);
            continue;
        }
        if (own_ext && own_lease && now_s() >= renew_at) { pthread_mutex_unlock(&mu); renew_own(); pthread_mutex_lock(&mu); continue; }
        double wait = own_ext && own_lease ? renew_at - now_s() : 3600; if (wait < 0.05) wait = 0.05; if (wait > 3600) wait = 3600;
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        double t = ts.tv_sec + ts.tv_nsec / 1e9 + wait; ts.tv_sec = (time_t)t; ts.tv_nsec = (long)((t - (double)ts.tv_sec) * 1e9);
        pthread_cond_timedwait(&cv, &mu, &ts);
    }
    return NULL;
}
static void ensure_thread(void) {                                     /* caller holds mu */
    if (started) return;
    started = 1;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) == 0) {
        pthread_detach(t);
#ifndef _WIN32
        atexit(at_exit);                                               /* Windows: atexit of a DLL runs under the loader lock (no sockets there) */
#else
        (void)at_exit;
#endif
    }
    else { started = -1; rig_log("upnpport: no worker thread"); }
}

/* ---------------------------------------------------------------- game thread API (never blocks) */
void upnpport_host(uint16_t port) {
    pthread_mutex_lock(&mu);
    if (port != want_port) {
        want_port = port; ext_port = 0;
        if (!port) state = UPNPP_IDLE;
        else if (!upnp_enabled()) { state = UPNPP_OFF; rig_log("upnpport: port %u: UPnP switched off (DSRIG_UPNP / test copy)", port); }
        else state = UPNPP_PENDING;
        if (state != UPNPP_OFF && (port || started > 0)) { ensure_thread(); pthread_cond_signal(&cv); }
    }
    pthread_mutex_unlock(&mu);
}
int upnpport_get(uint16_t *ext) {
    pthread_mutex_lock(&mu);
    int s = state; if (ext) *ext = ext_port;
    pthread_mutex_unlock(&mu);
    return s;
}
