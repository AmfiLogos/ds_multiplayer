// dswin.h - POSIX shims for building the Mac mod sources (rig, dsnet, mp) for Windows x64 with llvm-mingw.
// Force-included (-include dswin.h) before every ported source. System headers come first, so later
// #includes of the same headers are no-ops and the macros below only touch the mod's own code.
// Sockets: dsnet keeps socket handles in int; Winsock handles fit (they are small kernel handle values).
#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h>

/* ---- sockets: errno-setting wrappers (dswin.c) */
int dsw_close(int fd);
int dsw_read(int fd, void *buf, unsigned int n);
int dsw_write(int fd, const void *buf, unsigned int n);
int dsw_send(int fd, const void *buf, size_t n, int flags);
int dsw_recv(int fd, void *buf, size_t n, int flags);
int dsw_fcntl(int fd, int cmd, ...);
int dsw_connect(int fd, const struct sockaddr *a, int len);
int dsw_getsockopt(int fd, int level, int opt, void *v, int *len);
int dsw_socketpair(int sv[2]);          /* connected loopback TCP pair (pipe / socketpair stand-in) */
int dsw_net_init(void);                 /* WSAStartup once */

/* close/read/write on sockets: gen_port.py rewrites those calls to dsw_* (a macro would also hit struct
   members such as tr.close) */
#define fcntl                dsw_fcntl
#define pipe(fds)            dsw_socketpair(fds)
#define socketpair(d, t, p, sv) dsw_socketpair(sv)
#define poll(p, n, t)        WSAPoll(p, n, t)
#define connect(s, a, n)     dsw_connect(s, a, (int)(n))
#define getsockopt(s, l, o, v, n) dsw_getsockopt(s, l, o, v, (int *)(n))
#define setsockopt(s, l, o, v, n) setsockopt(s, l, o, (const char *)(v), n)
#ifndef F_GETFL
#define F_GETFL 3
#define F_SETFL 4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x4000
#endif
#ifndef SO_NOSIGPIPE
#define SO_NOSIGPIPE 0x7FFF               /* not a Windows option: setsockopt fails, the result is ignored */
#endif
#ifndef AF_UNIX
#define AF_UNIX 1
#endif

/* ---- files */
#define mkdir(p, m) _mkdir(p)

/* ---- threads */
extern DWORD rig_main_tid;              /* set by the rig on the first MainFlow.Update (0 = unknown) */
static inline int pthread_main_np(void) { return rig_main_tid && GetCurrentThreadId() == rig_main_tid; }
/* pthread_cond_timedwait_relative_np: provided by winpthreads */

/* Vector2 by value (Windows x64: packed into one integer register; gen_port.py rewrites float-pair calls to this) */
typedef struct { float x, y; } dsw_v2;

/* arc4random_buf (BSD): system RNG */
#include <bcrypt.h>
static inline void arc4random_buf(void *buf, size_t n) { BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG); }
static inline uint32_t arc4random(void) { uint32_t r; arc4random_buf(&r, 4); return r; }

/* strcasestr (BSD/glibc, not in mingw): case-insensitive strstr (ASCII) */
#include <ctype.h>
static inline char *strcasestr(const char *h, const char *n) {
    size_t k = strlen(n);
    for (; *h; h++) if (!_strnicmp(h, n, k)) return (char *)h;
    return k ? NULL : (char *)h;
}
