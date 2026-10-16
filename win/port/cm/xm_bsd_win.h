/* force-included into Custom Modes' solo sources on Windows (build_win.sh): BSD string calls msvcrt lacks */
#pragma once
#include <string.h>
#include <stddef.h>
static inline size_t cm_strlcat(char *d, const char *s, size_t n) {
    size_t a = strnlen(d, n), b = strlen(s);
    if (a < n) { size_t c = b < n - a - 1 ? b : n - a - 1; memcpy(d + a, s, c); d[a + c] = 0; }
    return a + b;
}
static inline size_t cm_strlcpy(char *d, const char *s, size_t n) {
    size_t b = strlen(s);
    if (n) { size_t c = b < n - 1 ? b : n - 1; memcpy(d, s, c); d[c] = 0; }
    return b;
}
#define strlcat cm_strlcat
#define strlcpy cm_strlcpy
