/* mp_bridge.c - C entry points for managed mods (BepInEx plugins): session info + actions through the lockstep order
 * path. API and rules: mp_bridge.h. Lane notes: ext/bepinex_bridge/NOTES.md.
 *
 * Wire (args of mp order MP_BRIDGE_TYPE, zero owner Guid): u8 version (1), u8 channel length, channel bytes, data. */
#include "mp_bridge.h"
#include "mp.h"
#include "rig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BR_WIRE_VER 1

static mp_session *cur;
static mp_bridge_dispatch_fn dispatch;
static mp_bridge_stats st;
static int applying;
static char ms_text[DSNET_MODLINES_MAX + 128]; static int ms_len = -1;   /* modset text, read once per session */

static dsnet_client *cli(void) { return cur ? mp_client(cur) : NULL; }

int dsmp_bridge_api_version(void) { return MP_BRIDGE_API; }
int dsmp_bridge_active(void) { dsnet_client *c = cli(); return c && dsnet_player_id(c) >= 0; }
int dsmp_bridge_role(void) {
    dsnet_client *c = cli();
    if (!c || dsnet_player_id(c) < 0) return MP_BRIDGE_ROLE_NONE;
    return dsnet_is_spectator(c) ? MP_BRIDGE_ROLE_SPECTATOR : dsnet_is_host(c) ? MP_BRIDGE_ROLE_HOST : MP_BRIDGE_ROLE_GUEST;
}
int dsmp_bridge_player(void) { dsnet_client *c = cli(); return c ? dsnet_player_id(c) : -1; }
uint32_t dsmp_bridge_step(void) { return cur ? mp_cur_step(cur) : 0; }
int dsmp_bridge_applying(void) { return applying; }

int dsmp_bridge_modset(char *out, int n) {
    if (ms_len < 0) {
        int vanilla = 0; char hash[65]; static char lines[DSNET_MODLINES_MAX];
        int rc = mp_session_mods(&vanilla, hash, lines, sizeof lines);
        ms_len = snprintf(ms_text, sizeof ms_text, "%s\n%s%s%s", rc ? "none" : vanilla ? "vanilla" : "mods", hash, lines[0] ? "\n" : "", lines);
        if (ms_len >= (int)sizeof ms_text) ms_len = (int)sizeof ms_text - 1;
    }
    if (out && n > 0) { int k = ms_len < n - 1 ? ms_len : n - 1; memcpy(out, ms_text, (size_t)k); out[k] = 0; }
    return ms_len;
}

static int can_send(void) {
    dsnet_client *c = cli();
    if (!c || dsnet_player_id(c) < 0) return MP_BRIDGE_NO_SESSION;
    if (dsnet_is_spectator(c)) return MP_BRIDGE_SPECTATOR;
    return MP_BRIDGE_SENT;
}

int dsmp_bridge_send(const char *channel, const uint8_t *data, int len) {
    size_t cl = channel ? strlen(channel) : 0;
    if (!cl || cl > MP_BRIDGE_CH_MAX || len < 0 || (len && !data) || 2 + cl + (size_t)len > DSNET_MAX_PAYLOAD - MP_ORDER_HDR) return MP_BRIDGE_BAD_ARGS;
    int ok = can_send(); if (ok != MP_BRIDGE_SENT) return ok;
    uint8_t *p = malloc(2 + cl + (size_t)len);
    p[0] = BR_WIRE_VER; p[1] = (uint8_t)cl; memcpy(p + 2, channel, cl); if (len) memcpy(p + 2 + cl, data, (size_t)len);
    static const uint8_t zero[16];
    int rc = mp_order(cur, MP_BRIDGE_TYPE, zero, p, (uint16_t)(2 + cl + (size_t)len));
    free(p);
    if (rc) { rig_log("bridge: send '%s' (%d bytes) refused by mp_order (%d)", channel, len, rc); return MP_BRIDGE_REFUSED; }
    st.sent++;
    rig_log("bridge: sent '%s' (%d bytes) at step %u", channel, len, mp_cur_step(cur));
    return MP_BRIDGE_SENT;
}

/* native orders a plugin may send as is (unit-owner check by mp_core): MOVE (guid, i32 x, i32 y), ORD (ord wire bytes) */
int dsmp_bridge_order(int type, const uint8_t owner[16], const uint8_t *args, int len) {
    if (type != MP_ORDER_MOVE && type != 2 /* ORD_MP_TYPE */) return MP_BRIDGE_NOT_ALLOWED;
    if (!owner || len < 0 || (len && !args) || len > DSNET_MAX_PAYLOAD - MP_ORDER_HDR) return MP_BRIDGE_BAD_ARGS;
    int ok = can_send(); if (ok != MP_BRIDGE_SENT) return ok;
    if (mp_order(cur, (uint16_t)type, owner, args, (uint16_t)len)) return MP_BRIDGE_REFUSED;
    st.sent++;
    return MP_BRIDGE_SENT;
}

void dsmp_bridge_set_dispatch(mp_bridge_dispatch_fn fn) {
    dispatch = fn;
    rig_log("bridge: managed dispatcher %s", fn ? "set" : "cleared");
}
void dsmp_bridge_get_stats(mp_bridge_stats *out) { if (out) *out = st; }

/* ---- built-in native actions ("dsmp.*"): the same code on every platform, no plugin needed on any peer ---- */
static int clan_gold(int32_t **p) {
    Obj c0 = cdc(), clan = c0 ? AT(c0, 0x50, Obj) : NULL;   /* ClanDataContainer: gold +0x2C (ui_game.c debug_gold_order) */
    if (!clan) return -1;
    *p = &AT(clan, 0x2C, int32_t); return 0;
}
static int native_action(int player, const char *ch, const uint8_t *d, int n, int *rc) {
    if (!strcmp(ch, "dsmp.gold.add")) {                       /* i32 delta; the result is clamped to 0..int32 max */
        int32_t *g, delta;
        if (n != 4 || clan_gold(&g)) { *rc = -1; return 1; }
        memcpy(&delta, d, 4);
        int64_t v = (int64_t)*g + delta; if (v < 0) v = 0; if (v > INT32_MAX) v = INT32_MAX;
        rig_log("bridge: dsmp.gold.add %d by player %d: %d -> %d", delta, player, *g, (int)v);
        *g = (int32_t)v; *rc = 0; return 1;
    }
    return 0;
}

static int bridge_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)guid;
    st.received++;
    if (n < 2 || a[0] != BR_WIRE_VER || !a[1] || 2u + a[1] > n) { st.failed++; return -1; }
    char ch[MP_BRIDGE_CH_MAX + 1]; memcpy(ch, a + 2, a[1]); ch[a[1]] = 0;
    const uint8_t *d = a + 2 + a[1]; int dn = n - 2 - a[1], rc = 0;
    applying = 1;
    if (native_action(player, ch, d, dn, &rc)) { if (!rc) st.applied_native++; }
    else if (dispatch) { rc = dispatch(player, ch, d, dn, mp_cur_step(s)); if (!rc) st.applied_managed++; }
    else { st.unhandled++; rc = 0;
           rig_log("bridge: '%s' from player %d has no handler on this peer (no plugin): ignored, peers can differ now", ch, player); }
    applying = 0;
    if (rc) { st.failed++; rig_log("bridge: '%s' from player %d failed (%d)", ch, player, rc); }
    return rc;
}

void mp_bridge_attach(struct mp_session *s) {
    cur = s; ms_len = -1;
    if (s) mp_set_order_handler(s, MP_BRIDGE_TYPE, bridge_order);
}

/* rig commands: bridge_status; bridge_send {"channel", "hex"} (test: what a plugin's Send does) */
char *mp_bridge_exec(const char *cmd, const char *line) {
    Buf b = {0};
    if (!strcmp(cmd, "bridge_status")) {
        int32_t *g = NULL; clan_gold(&g);
        char ms[512]; dsmp_bridge_modset(ms, sizeof ms);
        bput(&b, "{\"ok\":true,\"api\":%d,\"active\":%d,\"role\":%d,\"player\":%d,\"step\":%u,\"dispatch\":%d,\"gold\":%d,"
                 "\"sent\":%u,\"received\":%u,\"applied_native\":%u,\"applied_managed\":%u,\"unhandled\":%u,\"failed\":%u,\"modset\":",
             MP_BRIDGE_API, dsmp_bridge_active(), dsmp_bridge_role(), dsmp_bridge_player(), dsmp_bridge_step(), dispatch != NULL,
             g ? *g : -1, st.sent, st.received, st.applied_native, st.applied_managed, st.unhandled, st.failed);
        bstr(&b, ms); bput(&b, "}"); return b.p;
    }
    if (!strcmp(cmd, "bridge_send")) {
        char ch[80] = "", hex[1024] = ""; jstr(line, "channel", ch, sizeof ch); jstr(line, "hex", hex, sizeof hex);
        uint8_t d[512]; int n = 0;
        for (const char *h = hex; h[0] && h[1] && n < (int)sizeof d; h += 2) { unsigned v; if (sscanf(h, "%2x", &v) != 1) break; d[n++] = (uint8_t)v; }
        bput(&b, "{\"ok\":true,\"rc\":%d}", dsmp_bridge_send(ch, d, n)); return b.p;
    }
    return NULL;
}
