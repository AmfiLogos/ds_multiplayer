/* Host test of the test control channel guard (src/rig_sock.c "channel guard"), no game: the real rig_sock.c listener
   on a socket in a temp folder. Breaks it catches: an HTTP-looking connection or a wrong / missing token runs a command
   (the guard is open), or the right token is refused (every test tool stops working).
   Run: zsh src/sock_test/run.sh */
#include "../rig.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

uintptr_t ds_base; char rig_dir[1024]; volatile int64_t rig_frames = 7;
void rig_log(const char *fmt, ...) { va_list a; va_start(a, fmt); fprintf(stderr, "  log: "); vfprintf(stderr, fmt, a); fputc('\n', stderr); va_end(a); }

static char sock[1100]; static int fails;
static void check(const char *name, int ok, const char *got) { printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, ok ? "" : " | got: ", ok ? "" : got); fails += !ok; }

/* one connection: send, then read until the game closes it or 2 s pass; *closed = the game closed it */
static void talk(const char *out, char *in, size_t n, int *closed) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = {0}; a.sun_family = AF_UNIX; snprintf(a.sun_path, sizeof a.sun_path, "%s", sock);
    if (connect(s, (struct sockaddr *)&a, sizeof a)) { snprintf(in, n, "connect failed"); *closed = 0; close(s); return; }
    struct timeval tv = {2, 0}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    write(s, out, strlen(out));
    size_t got = 0; *closed = 0;
    for (;;) {
        ssize_t k = read(s, in + got, n - 1 - got);
        if (k == 0) { *closed = 1; break; }
        if (k < 0) break;                       /* timeout: still open */
        got += (size_t)k;
    }
    in[got] = 0; close(s);
}

int main(void) {
    char tmpl[] = "/tmp/ctlguard.XXXXXX"; if (!mkdtemp(tmpl)) return 2;
    snprintf(rig_dir, sizeof rig_dir, "%s", tmpl); snprintf(sock, sizeof sock, "%s/ctl.sock", tmpl);
    setenv("DSRIG_CTL", "1", 1);
    rig_sock_start(sock);
    char tp[1100]; snprintf(tp, sizeof tp, "%s/ctl.token", rig_dir);
    for (int i = 0; i < 100 && access(sock, F_OK); i++) usleep(20000);
    char tok[64] = ""; FILE *f = fopen(tp, "r"); if (f) { if (!fgets(tok, sizeof tok, f)) tok[0] = 0; fclose(f); }
    tok[strcspn(tok, "\n")] = 0;
    struct stat st = {0}; stat(tp, &st);
    int hex = strlen(tok) == 32; for (int i = 0; tok[i]; i++) hex &= !!strchr("0123456789abcdef", tok[i]);
    check("ctl.token: 32 lowercase hex characters, mode 0600", hex && (st.st_mode & 0777) == 0600, tok);

    char in[4096], out[512]; int closed;
    talk("POST / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n{\"cmd\":\"ping\"}\n", in, sizeof in, &closed);
    check("HTTP request line first: closed, nothing answered", closed && !in[0], in);
    talk("GET / HTTP/1.1\r\n", in, sizeof in, &closed);
    check("GET line: closed, nothing answered", closed && !in[0], in);
    talk("{\"cmd\":\"ping\"}\n", in, sizeof in, &closed);
    check("no token: one error line, closed, ping not answered", closed && strstr(in, "bad token") && !strstr(in, "pong"), in);
    talk("0123456789abcdef0123456789abcdef\n{\"cmd\":\"ping\"}\n", in, sizeof in, &closed);
    check("wrong token: one error line, closed, ping not answered", closed && strstr(in, "bad token") && !strstr(in, "pong"), in);
    snprintf(out, sizeof out, "{\"token\":\"%s\"}\n{\"cmd\":\"ping\"}\n", tok); talk(out, in, sizeof in, &closed);
    check("token inside JSON (not the documented form): refused", closed && strstr(in, "bad token") && !strstr(in, "pong"), in);
    memset(out, 'a', 300); out[300] = 0; talk(out, in, sizeof in, &closed);
    check("300 bytes, no newline: refused and closed", closed && strstr(in, "bad token"), in);
    snprintf(out, sizeof out, "%s\r\n{\"cmd\":\"ping\"}\n{\"cmd\":\"ping\"}\n", tok); talk(out, in, sizeof in, &closed);
    check("right token (CRLF): no answer to it, both pings answered, stays open",
          !closed && !strncmp(in, "{\"ok\":true,\"pong\":true", 22) && strstr(strchr(in, '\n') + 1, "\"pong\":true") && !strstr(in, "token"), in);
    snprintf(out, sizeof out, "%s\n{\"cmd\":\"ping\"}\nHost: x\n{\"cmd\":\"ping\"}\n", tok); talk(out, in, sizeof in, &closed);
    check("right token, then an HTTP header line: first ping answered, then closed", closed && strstr(in, "pong") && !strstr(strchr(in, '\n') + 1, "pong"), in);
    printf("RESULT %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
