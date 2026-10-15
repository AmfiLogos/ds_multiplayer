// dswin.c - Winsock wrappers behind the dswin.h macros (compiled WITHOUT those macros).
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <errno.h>
#include <stdarg.h>
#include <io.h>

DWORD rig_main_tid;

static int wsa_to_errno(int e) {
    return (e == WSAEWOULDBLOCK) ? EAGAIN : (e == WSAEINTR) ? EINTR : (e == WSAEINPROGRESS) ? EINPROGRESS :
           (e == WSAECONNREFUSED) ? ECONNREFUSED : (e == WSAETIMEDOUT) ? ETIMEDOUT : EIO;
}
static void set_errno(void) { errno = wsa_to_errno(WSAGetLastError()); }

int dsw_net_init(void) {
    static volatile LONG done;
    if (InterlockedCompareExchange(&done, 1, 0) == 0) { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w); }
    return 0;
}
__attribute__((constructor)) static void dsw_ctor(void) { dsw_net_init(); }

int dsw_close(int fd) { return closesocket((SOCKET)fd) == 0 ? 0 : -1; }
int dsw_send(int fd, const void *buf, size_t n, int flags) {
    int r = send((SOCKET)fd, (const char *)buf, (int)n, flags);
    if (r == SOCKET_ERROR) { set_errno(); return -1; }
    return r;
}
int dsw_recv(int fd, void *buf, size_t n, int flags) {
    int r = recv((SOCKET)fd, (char *)buf, (int)n, flags);
    if (r == SOCKET_ERROR) { set_errno(); return -1; }
    return r;
}
int dsw_read(int fd, void *buf, unsigned int n) { return dsw_recv(fd, buf, n, 0); }
int dsw_write(int fd, const void *buf, unsigned int n) { return dsw_send(fd, buf, n, 0); }

/* non-blocking connect: Winsock says WSAEWOULDBLOCK where POSIX says EINPROGRESS */
int dsw_connect(int fd, const struct sockaddr *a, int len) {
    if (connect((SOCKET)fd, a, len) == 0) return 0;
    int e = WSAGetLastError();
    errno = (e == WSAEWOULDBLOCK) ? EINPROGRESS : wsa_to_errno(e);
    return -1;
}
/* SO_ERROR comes back as a WSA code: translate it to errno values */
int dsw_getsockopt(int fd, int level, int opt, void *v, int *len) {
    if (getsockopt((SOCKET)fd, level, opt, (char *)v, len)) { set_errno(); return -1; }
    if (level == SOL_SOCKET && opt == SO_ERROR && *(int *)v) *(int *)v = wsa_to_errno(*(int *)v);
    return 0;
}

int dsw_fcntl(int fd, int cmd, ...) {
    if (cmd == 3) return 0;                                  /* F_GETFL */
    if (cmd == 4) {                                          /* F_SETFL */
        va_list ap; va_start(ap, cmd); int fl = va_arg(ap, int); va_end(ap);
        u_long nb = (fl & 0x4000) ? 1 : 0;
        return ioctlsocket((SOCKET)fd, FIONBIO, &nb) == 0 ? 0 : -1;
    }
    return -1;
}

/* loopback TCP pair: listener on 127.0.0.1:0, connect, accept */
int dsw_socketpair(int sv[2]) {
    dsw_net_init();
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), a = INVALID_SOCKET, c = INVALID_SOCKET;
    struct sockaddr_in ad = {0}; int len = sizeof ad;
    ad.sin_family = AF_INET; ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK); ad.sin_port = 0;
    if (l == INVALID_SOCKET) return -1;
    if (bind(l, (struct sockaddr *)&ad, sizeof ad) || listen(l, 1) || getsockname(l, (struct sockaddr *)&ad, &len)) goto fail;
    c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c == INVALID_SOCKET || connect(c, (struct sockaddr *)&ad, sizeof ad)) goto fail;
    a = accept(l, NULL, NULL);
    if (a == INVALID_SOCKET) goto fail;
    closesocket(l);
    int one = 1;
    setsockopt(a, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    sv[0] = (int)a; sv[1] = (int)c;
    return 0;
fail:
    if (c != INVALID_SOCKET) closesocket(c);
    closesocket(l);
    return -1;
}

int dsw_rename(const char *from, const char *to) {
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return 0;
    errno = EACCES;
    return -1;
}
