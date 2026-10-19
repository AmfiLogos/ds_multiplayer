// mp_modchat.c - the pure part of mod chat lines (mp_api v2 chat_line; mpmenu.c shows and sends them; docs:
// src/MP_API.md): text clean-up, the broadcast wire form, rate limits. No game calls (unit test: mp_api_test).
// Broadcast wire: an ordinary chat message (P_CHAT, outside the sim) whose text is 0x1F <mod> 0x1F <text>. Mail is not
// used: Custom Modes reads every mail kind itself (fe_game.c / pvp_units.c dsnet_mail_poll).
#include "mp_api.h"
#include <stdio.h>
#include <string.h>

/* copy at most cap-1 bytes, never cutting a UTF-8 character; control bytes (newlines, 0x1F) become spaces */
size_t mpc_clean(const char *in, char *out, size_t cap) {
    size_t n = 0;
    for (; in && *in && n + 1 < cap; in++) out[n++] = (unsigned char)*in < 0x20 || *in == 0x7F ? ' ' : *in;
    if (in && ((unsigned char)*in & 0xC0) == 0x80) {                           /* cut inside a character: drop its start */
        while (n && ((unsigned char)out[n - 1] & 0xC0) == 0x80) n--;
        if (n) n--;
    }
    out[n] = 0;
    return n;
}
int mpc_encode(const char *mod, const char *text, char *out, size_t cap) {
    char m[MP_CHAT_MOD_MAX + 1], t[MP_CHAT_TEXT_BROADCAST + 1];
    if (!mpc_clean(mod, m, sizeof m) || !mpc_clean(text, t, sizeof t)) return -1;
    int n = snprintf(out, cap, "\x1F%s\x1F%s", m, t);
    return n > 0 && (size_t)n < cap ? n : -1;
}
int mpc_decode(const char *wire, char *mod, size_t mn, const char **text) {
    if (!wire || wire[0] != 0x1F) return 0;
    const char *e = strchr(wire + 1, 0x1F);
    if (!e || e == wire + 1 || (size_t)(e - wire - 1) > MP_CHAT_MOD_MAX) return 0;
    snprintf(mod, mn, "%.*s", (int)(e - wire - 1), wire + 1);
    *text = e + 1;
    return 1;
}

/* token buckets: burst lines at once, then one per every_s seconds */
int mpc_take(mpc_bucket *b, double now, double burst, double every_s) {
    if (!b->t) { b->t = now; b->tokens = burst; }
    b->tokens += (now - b->t) / every_s; b->t = now;
    if (b->tokens > burst) b->tokens = burst;
    if (b->tokens < 1) return 0;
    b->tokens -= 1; return 1;
}
