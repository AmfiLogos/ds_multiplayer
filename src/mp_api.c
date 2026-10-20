// mp_api.c - the Multiplayer API table and its hook registries (mp_api.h; MASTER_PLAN 2.2 B, M4 split).
// Every hook point keeps the order the code had when FE / PvP were compiled in: callbacks run in registration order
// (Custom Modes registers FE first, then PvP).
#include "mp_api.h"
#include "mpmenu_kit.h"
#include "mp.h"
#include "mp_game.h"
#include "net/dsnet.h"
#include "rig.h"
#include "ds_i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NREG 8

static dsnet_client *cli(void) { mp_session *s = mp_game_session(); return s ? mp_client(s) : NULL; }

/* ---------------------------------------------------------------- session facts */
static int a_is_host(void) { dsnet_client *c = cli(); return c && dsnet_is_host(c); }
static int a_role(void) { dsnet_client *c = cli(); return !c ? MP_ROLE_NONE : dsnet_is_host(c) ? MP_ROLE_HOST : MP_ROLE_GUEST; }
static int a_my_uid(uint8_t out[16]) { dsnet_client *c = cli(); return c ? dsnet_my_uid(c, out) : -1; }
static uint32_t a_present(void) { dsnet_client *c = cli(); return c ? dsnet_present_mask(c) : 0; }
static int a_players(mp_player *out, int max) {
    dsnet_client *c = cli(); if (!c) return 0;
    uint32_t pm = dsnet_present_mask(c); int n = 0;
    for (int p = 0; p < 32 && n < max; p++) if (pm >> p & 1) {
        const char *nm = dsnet_player_name(c, p);
        out[n].id = p; snprintf(out[n].name, sizeof out[n].name, "%s", nm ? nm : "");
        if (dsnet_player_uid(c, p, out[n].uid)) memset(out[n].uid, 0, 16);
        n++;
    }
    return n;
}
__attribute__((visibility("default"))) int (*mp_setting_int_fn)(const char *id, int dflt);   /* mpmenu.h mpm_host_port; set by the loader glue */
const char *mp_session_busy_reason(void) {
    if (mp_game_session()) return TR("a multiplayer game is running");      /* shown by the loader (same process, same language) */
    switch (M.state) {
    case MPM_HOST_LOADING: case MPM_LOBBY: case MPM_PLAYING: case MPM_GUEST_JOINING: case MPM_GUEST_LOADING: case MPM_GUEST_LOBBY:
        return TR("a multiplayer game is starting");
    default: return NULL;
    }
}

/* ---------------------------------------------------------------- mail (one reader, only once something registered) */
static struct { uint8_t lo, hi; void (*cb)(const uint8_t *, uint8_t, const uint8_t *, size_t); } mail[NREG]; static int nmail;
static int a_mail_send(uint8_t kind, const uint8_t to[16], const void *b, size_t n) { dsnet_client *c = cli(); return c ? dsnet_mail_send(c, to, kind, b, n) : -1; }
static int a_register_mail(uint8_t lo, uint8_t hi, void (*cb)(const uint8_t *, uint8_t, const uint8_t *, size_t)) {
    if (!cb || nmail >= NREG) return -1;
    mail[nmail].lo = lo; mail[nmail].hi = hi; mail[nmail].cb = cb; nmail++; return 0;
}
static void mail_poll(void) {
    dsnet_client *c = cli(); if (!c || !nmail) return;
    uint8_t from[16], kind; uint8_t *data; size_t len;
    while (dsnet_mail_poll(c, from, &kind, &data, &len)) {
        int took = 0;
        for (int i = 0; i < nmail && !took; i++) if (kind >= mail[i].lo && kind <= mail[i].hi) { mail[i].cb(from, kind, data, len); took = 1; }
        if (!took) rig_log("mp_api: mail kind %d with no reader: dropped", kind);
        free(data);
    }
}

/* ---------------------------------------------------------------- sim */
static struct { int kind; void (*apply)(const uint8_t *, size_t); } simc[NREG]; static int nsimc;
static int a_register_sim_cmd(int kind, void (*apply)(const uint8_t *, size_t)) {
    if (!apply || nsimc >= NREG || kind != DSNET_FE_START) return -1;      /* the one mod command kind dsnet carries today */
    simc[nsimc].kind = kind; simc[nsimc].apply = apply; nsimc++; return 0;
}
static int a_send_sim_cmd(int kind, const void *p, size_t n) {
    dsnet_client *c = cli(); if (!c || kind != DSNET_FE_START || n > 0xFFFF) return -1;
    return dsnet_submit(c, (uint8_t)kind, 0, p, (uint16_t)n);
}
int mpx_sim_cmd(int kind, const uint8_t *p, size_t n) {
    int k = 0; for (int i = 0; i < nsimc; i++) if (simc[i].kind == kind) { simc[i].apply(p, n); k++; }
    return k;
}
static void (*ticks[NREG])(void); static int (*freezes[NREG])(void); static int nticks;
static int a_register_tick(void (*tick)(void), int (*freeze)(void)) {
    if ((!tick && !freeze) || nticks >= NREG) return -1;
    ticks[nticks] = tick; freezes[nticks] = freeze; nticks++; return 0;
}
int mpx_tick_frozen(void) { for (int i = 0; i < nticks; i++) if (freezes[i] && freezes[i]()) return 1; return 0; }
void mpx_tick(void) { for (int i = 0; i < nticks; i++) if (ticks[i]) ticks[i](); }
static uint64_t (*fpmix[NREG])(void); static int nfp;
static int a_register_fp(uint64_t (*mix)(void)) { if (!mix || nfp >= NREG) return -1; fpmix[nfp++] = mix; return 0; }
int mpx_fp_mix(uint64_t *m) {
    if (!nfp) return 0;
    uint64_t x = 0; for (int i = 0; i < nfp; i++) x ^= fpmix[i]();       /* one module: its value as is */
    *m = x; return 1;
}
static mp_joinpoint jps[NREG]; static int njp;
static int a_register_jp(const mp_joinpoint *jp) {
    if (!jp || !jp->size || !jp->write || !jp->read || njp >= NREG) return -1;
    jps[njp++] = *jp; return 0;
}
int mpx_jp_count(void) { return njp; }
const mp_joinpoint *mpx_jp_at(int i) { return i >= 0 && i < njp ? &jps[i] : NULL; }
int mpx_jp_override(const uint8_t **det, size_t *dl, const uint8_t **json, size_t *jn) {
    for (int i = 0; i < njp; i++) if (jps[i].override_save && jps[i].override_save(det, dl, json, jn)) return 1;
    return 0;
}
static struct { char what[64]; int (*build)(const char *, const char *); } wb[NREG]; static int nwb;
static int a_register_world_build(const char *what, int (*build)(const char *, const char *)) {
    if (!build || nwb >= NREG) return -1;
    snprintf(wb[nwb].what, sizeof wb[nwb].what, "%s", what ? what : "a mod's world build"); wb[nwb].build = build; nwb++; return 0;
}
void mpx_world_build(const char *path, const char *save_name) {
    for (int i = 0; i < nwb; i++) if (wb[i].build(path, save_name) < 0) rig_log("mp: %s failed: the world goes on unchanged", wb[i].what);
}
static int (*keeps[NREG])(const char *); static int nkeep;
static int a_register_save_keep(int (*keep)(const char *)) { if (!keep || nkeep >= NREG) return -1; keeps[nkeep++] = keep; return 0; }
int mpx_save_keep(const char *stem) { for (int i = 0; i < nkeep; i++) if (keeps[i](stem)) return 1; return 0; }

/* ---------------------------------------------------------------- frame */
static struct { int when; void (*cb)(void); } frm[NREG]; static int nfrm;
static int a_register_frame(int when, void (*cb)(void)) {
    if (!cb || nfrm >= NREG || (when != MP_FRAME_ALWAYS && when != MP_FRAME_SESSION)) return -1;
    frm[nfrm].when = when; frm[nfrm].cb = cb; nfrm++; return 0;
}
void mpx_frame(int when) {
    if (when == MP_FRAME_SESSION) mail_poll();
    for (int i = 0; i < nfrm; i++) if (frm[i].when == when) frm[i].cb();
}

/* ---------------------------------------------------------------- orders */
static int (*recruit_ok[NREG])(void); static void (*pickups[NREG])(const uint8_t *, int); static int nord;
static int a_register_order_filter(int (*ok)(void), void (*pick)(const uint8_t *, int)) {
    if ((!ok && !pick) || nord >= NREG) return -1;
    recruit_ok[nord] = ok; pickups[nord] = pick; nord++; return 0;
}
int mpx_recruit_allowed(void) { for (int i = 0; i < nord; i++) if (recruit_ok[i] && !recruit_ok[i]()) return 0; return 1; }
void mpx_pickup(const uint8_t item[16], int player) { for (int i = 0; i < nord; i++) if (pickups[i]) pickups[i](item, player); }

/* ---------------------------------------------------------------- UI */
static int ui_flags;
static void a_ui_set_flag(int flag, int on) {
    int was = ui_flags;
    ui_flags = on ? ui_flags | flag : ui_flags & ~flag;
    if (was != ui_flags) rig_log("mp_api: ui flags %#x -> %#x", was, ui_flags);
}
int mpx_ui_flag(int flag) { return (ui_flags & flag) != 0; }
static int (*utag[NREG])(const uint8_t *, char *, int); static uint64_t (*usig[NREG])(void); static int ntag;
static int a_register_unit_tag(int (*tag)(const uint8_t *, char *, int), uint64_t (*sig)(void)) {
    if (!tag || ntag >= NREG) return -1;
    utag[ntag] = tag; usig[ntag] = sig; ntag++; return 0;
}
int mpx_unit_tag(const uint8_t guid[16], char *out, int cap) {
    for (int i = 0; i < ntag; i++) { int r = utag[i](guid, out, cap); if (r > 0) return r; }
    return 0;
}
int mpx_tag_sig(uint64_t *sig) {
    int any = 0; uint64_t x = 0;
    for (int i = 0; i < ntag; i++) if (usig[i]) { x ^= usig[i](); any = 1; }
    if (any) *sig = x;
    return any;
}
static int (*portal_blk[NREG])(void); static int nportal;
static int a_register_portal(int (*b)(void)) { if (!b || nportal >= NREG) return -1; portal_blk[nportal++] = b; return 0; }
int mpx_portal_blocked(void) { for (int i = 0; i < nportal; i++) if (portal_blk[i]()) return 1; return 0; }
static size_t a_modset_lines(char *out, size_t cap) {
    int van = 0; char ms[65]; static char buf[DSNET_MODLINES_MAX];
    if (mp_session_mods(&van, ms, buf, sizeof buf)) buf[0] = 0;
    size_t n = strlen(buf);
    if (out && cap) snprintf(out, cap, "%s", buf);
    return n;
}
static struct { char pre[16]; char *(*exec)(const char *, const char *); } rcmd[NREG]; static int nrcmd;
static int a_register_rig_cmd(const char *pre, char *(*exec)(const char *, const char *)) {
    if (!pre || !pre[0] || !exec || nrcmd >= NREG) return -1;
    snprintf(rcmd[nrcmd].pre, sizeof rcmd[nrcmd].pre, "%s", pre); rcmd[nrcmd].exec = exec; nrcmd++; return 0;
}
char *mpx_rig_cmd(const char *cmd, const char *line) {
    for (int i = 0; i < nrcmd; i++) if (!strncmp(cmd, rcmd[i].pre, strlen(rcmd[i].pre))) { char *r = rcmd[i].exec(cmd, line); if (r) return r; }
    return NULL;
}

/* ---------------------------------------------------------------- menus (mpmenu.c) */
int mpmenu_register_esc_button(const char *label, const char *tip, int (*visible)(void), void (*click)(void));
static int a_register_host_choice(const char *id, const char *label, const char *tip, void (*draw_page)(void *),
                                  int (*on_start)(int, int, int, int, char *, size_t)) {
    return mpmenu_register_host_choice(id, label, tip, draw_page, on_start) ? 0 : -1;
}

int mpmenu_register_host_choice2(const char *id, const char *label, const char *tip, void (*draw_page)(void *),
                                 int (*page_start)(char *, size_t), int (*lobby_start)(char *, size_t));
const char *mpmenu_host_choice_now(void);
static int a_register_host_choice2(const mp_host_choice *c) {
    return c ? mpmenu_register_host_choice2(c->id, c->label, c->tip, c->draw_page, c->page_start, c->lobby_start) : -1;
}

/* ---------------------------------------------------------------- the table */
static const mp_api_table api = {
    .version = MP_API_VERSION, .size = sizeof(mp_api_table),
    .role = a_role, .is_host = a_is_host, .my_uid = a_my_uid, .players = a_players, .present_mask = a_present,
    .session_busy = mp_session_busy_reason,
    .register_host_choice = a_register_host_choice, .register_esc_button = mpmenu_register_esc_button,
    .mail_send = a_mail_send, .register_mail = a_register_mail,
    .register_sim_cmd = a_register_sim_cmd, .send_sim_cmd = a_send_sim_cmd,
    .register_tick = a_register_tick, .register_fingerprint = a_register_fp, .register_joinpoint = a_register_jp,
    .register_world_build = a_register_world_build, .register_order_filter = a_register_order_filter,
    .ui_set_flag = a_ui_set_flag, .register_unit_tag = a_register_unit_tag, .register_teams_portal = a_register_portal,
    .modset_lines = a_modset_lines, .register_rig_cmd = a_register_rig_cmd,
    .register_frame = a_register_frame, .register_save_keep = a_register_save_keep, .register_menu = mpmenu_register_hooks,
    .register_host_choice2 = a_register_host_choice2, .host_choice_now = mpmenu_host_choice_now,
    .rand_u32 = mp_rand_u32, .rand_range = mp_rand_range, .rand_value = mp_rand_value, .rand_shared = mp_rand_shared,
    .chat_line = mpmenu_mod_chat,
};
const mp_api_table *mp_api_get(uint32_t version) {
    if (version > MP_API_VERSION) { rig_log("mp_api: version %u asked, this multiplayer has %d", version, MP_API_VERSION); return NULL; }
    return &api;
}
