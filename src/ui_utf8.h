// ui_utf8.h - C string -> il2cpp string without the blank-label trap (i18n, used by ui_game.c, ui_teams.c, ui_tip.c).
// il2cpp_string_new returns an EMPTY string when its UTF-8 input is not valid, so one byte-cut CJK character blanks
// the whole label (research/i18n_plan.md section 4). ui_str_new passes valid text as is; a broken text is copied and
// repaired first: invalid bytes in the middle are dropped and a cut character at the end too (dsi_utf8_fix_end).
// Pure C, no game calls (the caller passes il2cpp_string_new).
#pragma once
#include <stdlib.h>
#include <string.h>
#include "ds_i18n.h"

/* length of the valid UTF-8 sequence at s (1..4), 0 = invalid (same rules as il2cpp's validator: no overlong forms,
   no surrogates, nothing above U+10FFFF) */
static inline int ui_utf8_seq(const unsigned char *s) {
    unsigned c = s[0];
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) return (s[1] & 0xC0) == 0x80 ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        unsigned lo = c == 0xE0 ? 0xA0 : 0x80, hi = c == 0xED ? 0x9F : 0xBF;
        return s[1] >= lo && s[1] <= hi && (s[2] & 0xC0) == 0x80 ? 3 : 0;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        unsigned lo = c == 0xF0 ? 0x90 : 0x80, hi = c == 0xF4 ? 0x8F : 0xBF;
        return s[1] >= lo && s[1] <= hi && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80 ? 4 : 0;
    }
    return 0;
}
static inline int ui_utf8_valid(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) { int n = ui_utf8_seq(p); if (!n) return 0; p += n; }
    return 1;
}
/* in place: drop every invalid byte (a cut character at the end included) */
static inline void ui_utf8_repair(char *s) {
    unsigned char *r = (unsigned char *)s, *w = r;
    while (*r) {
        int n = ui_utf8_seq(r);
        if (!n) { r++; continue; }
        while (n--) *w++ = *r++;
    }
    *w = 0;
    dsi_utf8_fix_end(s);
}
static inline void *ui_str_new(void *(*string_new)(const char *), const char *s) {
    if (!s || ui_utf8_valid(s)) return string_new(s);
    size_t n = strlen(s);
    char *b = (char *)malloc(n + 1);
    if (!b) return string_new("");
    memcpy(b, s, n + 1);
    ui_utf8_repair(b);
    void *o = string_new(b);
    free(b);
    return o;
}
