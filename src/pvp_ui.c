/* pvp_ui.c - PvP windows and HUD (plan research/plan_pvp.md 4.7; Custom Modes, custom-modes-mp.dylib). Included by mpmenu.c
 * until the M4 split; now it uses the menu's exported building blocks (src/mpmenu_kit.h) and runs through the menu hooks
 * (pvp_menu_hooks, registered by custom/cm_mp.c): stock Box frame, Cinzel titles, the body font without outline, SubUI_SystemMenuButton
 * clones (the click hook knows them), the game's tooltip window (tip_try). Rules and state: pvp_game.c (pvp_ui_* API).
 *  - Esc menu, in a multiplayer game: "PvP Match" (clone of Report bug, above Quit) opens the PvP window.
 *  - PvP window (on the Esc menu's root, its column hidden, like the lobby): Blue | Red player columns (host: click a
 *    player to move him to the other team), rules rows with the menu's arrows, Close / Start. Guests see it read-only.
 *  - HUD while a match runs: a copy of the boss health bar (BossHUDView): "Blue 1 - 0 Red  Round 2  1:23", the two bars =
 *    each team's HP left. Planning pause: a Ready button under it.
 *  - Result window when the match is over and the world is back: winner, score, the rounds. */
#include "mpmenu_kit.h"
#include "mp_modes_text.h"
#include "mp.h"
#include "mp_game.h"
#include "net/dsnet.h"
#include "pvp_core.h"
#include "fe_save.h"
#include "mp_worldsave.h"
#include "mp_esc.h"
#include "dsl_sdk.h"                   /* the loader kit: red X, right-drag move (dsl_movable) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
const pvp_state *pvp_ui_state(void) __attribute__((weak_import));
const DSLUi *mu_kit(void);                                               /* mu_ui.c: the loader's widget kit, NULL = none */
char *pvp_units_exec(const char *cmd, const char *line) __attribute__((weak_import));
int pvp_units_received(void) __attribute__((weak_import));
int pvp_units_roster(int i, char uid[33], char name[64], char port[64]) __attribute__((weak_import));
int rig_det_mp_ready(void);
int  pvp_ui_is_host(void) __attribute__((weak_import));
void pvp_ui_team_hp(uint32_t pm[3]) __attribute__((weak_import));
int  pvp_ui_start_match(int mode, const uint8_t teams[PVP_MAX_PLAYERS], const pvp_rules *r, int region, int floor, int seed) __attribute__((weak_import));
int  pvp_ui_ready(void) __attribute__((weak_import));
/* Vanilla check (pvp_mods.c) */
int  pvp_mods_default_vanilla(void) __attribute__((weak_import));
int  pvp_mods_told_vanilla(void) __attribute__((weak_import));
void pvp_mods_ask(int vanilla) __attribute__((weak_import));
const char *pvp_mods_last(void) __attribute__((weak_import));
void pvp_mods_summary(int vanilla, int host, char *out, size_t n) __attribute__((weak_import));

/* PvP is played from the Foreign Expedition hub (owner 2026-10-21: "keep only the Hub: Co-op or PvP"; the separate PvP
   host choice, its PvP world and its rules page are gone: the page's rows and presets are in this window now) */
int fe_world_is_hub(void);                                               /* fe/fe_game.c: a Foreign Expedition hub */
static int pv_here(void) { return fe_world_is_hub(); }                   /* the match is played on this settlement */
static void pre_step(int d); static int pre_typed(const char *name); static int pre_delete(void); static void pre_naming_set(int on);
static int pre_save(const char *given);
static void pre_status(Buf *b);
Obj mpmenu_field_new(Obj parent, const char *placeholder, const char *text);   /* mpmenu.c */
void mpmenu_field_get(Obj f, char *out, size_t cap);
void mpmenu_field_put(Obj f, const char *s);
/* host's choices (local until Start) */
static const int pv_bo[3] = { 1, 3, 5 };
static const int pv_secs[5] = { 60, 120, 180, 300, 480 };
static struct { int mode, bo, secs, pause, vanilla, team[PVP_MAX_PLAYERS], team_set[PVP_MAX_PLAYERS]; } pv = { PVP_MODE_FIGHT, 1, 2, 0, -1, { 0 }, { 0 } };
/* lane pvprules (owner 2026-10-20): every rule lives in pvp_rules.c by id (saved in the mod's data folder at each change,
   presets, the host's rules sent to every player at Start); pv mirrors the five old rules. */
#include "pvp_rules.h"
int pvpr_ui_rows(void); const char *pvpr_ui_id(int r); const char *pvpr_ui_label(int r); const char *pvpr_ui_tip(int r);
void pvpr_ui_value(int r, const pvpr_vals *v, int here, char *out, size_t cap); int pvpr_ui_steppable(int r, const pvpr_vals *v);
void pvpr_ui_step(int r, pvpr_vals *v, int d);
static int pv_loaded;
static void pv_from_store(void) {
    const pvpr_vals *v = pvp_rules_local();
    pv.mode = pvpr_vals_get(v, "mode") ? PVP_MODE_CTF : PVP_MODE_FIGHT; pv.bo = pvpr_vals_get(v, "win"); pv.secs = pvpr_vals_get(v, "round_time");
    pv.pause = pvpr_vals_get(v, "pause"); pv.vanilla = pvpr_vals_get(v, "vanilla");
}
static void pv_to_store(void) {
    pvpr_vals *v = pvp_rules_local();
    pvpr_vals_set(v, "mode", pv.mode == PVP_MODE_CTF); pvpr_vals_set(v, "win", pv.bo); pvpr_vals_set(v, "round_time", pv.secs);
    pvpr_vals_set(v, "pause", pv.pause != 0); pvpr_vals_set(v, "vanilla", pv.vanilla < 0 ? -1 : pv.vanilla != 0);
    pvp_rules_changed();
}
static void pv_sync_once(void) { if (!pv_loaded && pvp_rules_path()[0]) { pv_from_store(); pv_loaded = 1; } }   /* the file's last used rules */
/* the map for Start: the host's choice + a fresh seed (one draw on the host; every player gets the result in the orders) */
#define PV_REGION 1
#define PV_FLOOR 2
#define PV_SEED 424242
static void pv_pick_map(int here_world, pvp_map_pick *m) {
    const pvpr_vals *v = pvp_rules_local();
    uint32_t seed = (uint32_t)time(NULL) * 2654435761u ^ (uint32_t)clock() * 40503u ^ (uint32_t)(uintptr_t)m;
    if (pvp_map_resolve(pvpr_vals_get(v, "map"), pvpr_vals_get(v, "floor"), seed, m)) pvp_map_resolve(PVP_MAP_HERE, 0, 0, m);
    if (m->here && !here_world) { m->here = 0; m->region = PV_REGION; m->floor = PV_FLOOR; m->seed = PV_SEED; }   /* not a settlement world: the tested arena */
}
static char pv_err[700], pv_note_txt[700];                            /* Start Match refused: why (until the next change) */
static int pv_note_err;                                                  /* the note shown is a refusal (red) */
static const Col C_WARN = { 0.945f, 0.376f, 0.443f, 1 };                 /* #F16071, the lobby's refusal colour (mpmenu.c C_WARN_HEX) */
/* tested arena (rig run arena2: same map on every peer, 14 rooms, Blue arrival vs exit portal 28 cells apart): PV_REGION etc. above */
/* rule rows: left column Mode .. Vanilla check, right column the six visibility rules (pvp_rules.c), then the Preset row */
enum { PVR_MODE, PVR_WIN, PVR_TIME, PVR_PAUSE, PVR_ARENA, PVR_FLOOR, PVR_VANILLA, PVR_VIS0, PVR_N = PVR_VIS0 + 6 };
static int pvpr_row(int r) { return r == PVR_ARENA ? 0 : r == PVR_FLOOR ? 1 : r >= PVR_VIS0 && r < PVR_N ? 2 + r - PVR_VIS0 : -1; }   /* pvpr_ui_* row */
enum { PVB_ESC, PVB_CLOSE, PVB_START, PVB_READY, PVB_FIGHT, PVB_PROP_YES, PVB_PROP_NO, PVB_HUD_BRING, PVB_RESULT_OK, PVB_BRING, PVB_BR_BACK, PVB_BR_SEND, PVB_BR_WIKI, PVB_BR_SAVE0, PVB_BR_UNIT0 = PVB_BR_SAVE0 + 8,
       PVB_P0 = PVB_BR_UNIT0 + 12, PVB_ARROW0 = PVB_P0 + 8, PVB_PRE_L = PVB_ARROW0 + 2 * PVR_N, PVB_PRE_R, PVB_PRE_SAVEAS, PVB_PRE_DEL, PVB_PRE_SAVE, PVB_PRE_CANCEL, PVB_N };
static uintptr_t h_pvb[PVB_N];
static uintptr_t h_pv_por[8][4], h_pv_pname[8][4];
static char pv_col_names[16][64]; static int pv_ncol;                 /* names shown in the team columns (mpm_pvp state) */                      /* per player slot: unit portraits + names */
#define PV_TEAM_TOP 150
#define PV_BLOCK 100                                                     /* one player: name button 32 + portraits 46 + names 16 */
#define PV_TEAM_ROWS 2                                                   /* 4 units in total: at most 2 players per team bring units */
#define PV_RULES_TOP (PV_TEAM_TOP + PV_TEAM_ROWS * PV_BLOCK + 40)
#define PV_W 1180                                                        /* two columns: the teams, then the rules */
#define BR_W 760.0f                                                      /* the Bring Units text list */
#define PR_W 560.0f                                                      /* the result window */
#define PV_COLX(c) (24 + 576 * (c))                                      /* a column's left edge; 556 wide, 20 apart */
#define PV_COLW 556
#define PV_RULE_ROWS 7                                                   /* rows per rules column (right: 6 + Preset) */
#define PV_ROWY(i) (PV_RULES_TOP + 56 + 38 * (i))
#define PV_PBTN_Y PV_ROWY(PV_RULE_ROWS)                                  /* Save as preset... / Delete preset */
#define PV_H (PV_RULES_TOP + 516)                                        /* rows, preset buttons, Vanilla check line, note, bar */
static uintptr_t h_pv_prow, h_pv_plab, h_pv_pval, h_pv_field;          /* presets (host): row, label, name; the name field */
static int pre_naming; static char pre_msg[200];                      /* the name field is open; last preset message */
static float pv_k = 1;                                                   /* the window's fit scale (pv_fit) */
static uintptr_t h_pv_vsub, h_pv_root, h_pv_view, h_pv_area, h_pv_title, h_pv_hint, h_pv_teamh[3], h_pv_units[3], h_pv_rlab[PVR_N], h_pv_rval[PVR_N], h_pv_rrow[PVR_N], h_pv_note;
static uintptr_t h_pr_root, h_pr_title, h_pr_score, h_pr_rounds;          /* result window */
static uintptr_t h_hud, h_hud_name, h_hud_bar[2], h_hud_lab[2], h_hud_rlab, h_hud_line;            /* boss bar copy (+ its bottom line) */
static uintptr_t h_lab_yes, h_lab_no, h_lab_bring, h_hud_flab, h_prop_root, h_prop_txt, h_rs_root, h_rs_title, h_rs_score, h_rs_sub;    /* E4 fight button + proposal box, E9 round screen */
static double rs_t0; static uint32_t rs_key; static char hud_last[400];
#define HUD_INSET 84.0f                                                  /* the bars start this far right of the copy's own bar start (its end-cap stays there) */
#define PROP_W 520.0f                                                    /* the fight vote box */
static float hud_k = 1, prop_x, prop_y, bl_x, bl_w, bl_y; static Obj bl_par;                                                        /* E9: when this round screen showed (local clock) */
/* P2 (UX review 2026-10-25): the match messages (planning pause, fight, flags) under the score panel. The game's centred
   notice (HelpNoticeView) sat over the Teams button, the Players panel icons and the panel's top edge (de 960x540). */
static uintptr_t h_msg, h_msg_txt; static float msg_x, msg_y0, msg_w; static double msg_until; static int msg_hold; static char msg_now[400];
int  mu_bring_is_open(void);                                         /* mu_bring.c */
int  pvp_ui_vote(int op) __attribute__((weak_import));
int  pvp_ui_my_slot(void) __attribute__((weak_import));
int  pvp_ui_player_name(int slot, char *out, int cap) __attribute__((weak_import));
/* E4 vote state for this player: 1 = may propose, 2 = proposal waits for my answer, 3 = I agreed, waiting, 4 = agreed countdown (may cancel) */
static int vote_view(const pvp_state *s) {
    int me = pvp_ui_my_slot ? pvp_ui_my_slot() : -1;
    if (!s || !pvp_active(s) || !s->placing || s->round > 1 || s->resetting || s->ending || me < 0 || me >= PVP_MAX_PLAYERS || !s->team_of[me]) return 0;
    if (s->vote_open) return (s->agree_mask >> me & 1u) ? 3 : 2;
    return s->countdown_at ? 4 : 1;
}
/* review_ja #2: CJK (ko, zh-Hans, zh-Hant, ja) is never designed below 14 (smaller kanji turn to blobs at 960x540) */
static float pv_fs(float s) { int l = dsi_lang(); return (l == 1 || (l >= 5 && l <= 7)) && s < 14 ? 14 : s; }
static int pv_open, pr_open, pv_click = -1;
static void pv_esc_close(void *ud) { pv_click = (int)(intptr_t)ud; }   /* the kit's Escape and the red X: the window's own button, as a click */
static mpesc_win esc_pv = { .name = "the PvP Match window", .close = pv_esc_close, .ud = (void *)(intptr_t)PVB_CLOSE },
                 esc_br = { .name = "the PvP Bring Units window", .close = pv_esc_close, .ud = (void *)(intptr_t)PVB_BR_BACK },
                 esc_pr = { .name = "the PvP result window", .close = pv_esc_close, .ud = (void *)(intptr_t)PVB_RESULT_OK };
/* CLOSE RULE (owner 2026-10-25): the game's red X (the loader kit's, mp_esc.h mpesc_close_x) at the top-right of a window's
   title band (the band is 60 high from the top), a child of the window: it scales and moves with it. A click = the window's
   Escape close above (PvP Match: Close; Bring list: Back; result: Back to the World). Made when the window opens (a rebuilt
   window gets a new one; mpesc_close_x keeps one that is there). NULL = no kit red X (the rig's own dylib, an older loader). */
#define PV_XS 36.0f                                                      /* its size; x, y = its top-left in the window (kit coordinates) */
#define PV_XX(wd) ((wd) - PV_XS - 14)
#define PV_XY (32 - PV_XS / 2)
static int pv_kit_x(void) { const DSLUi *U = mu_kit(); return DSL_UI_HAS(U, close_x); }   /* else no call (no "none" line per open) */
static int br_open;
int pvp_ui_window_open(void) { return pv_open || pr_open || br_open; }   /* fe_menu.c: its window steps aside meanwhile */
static uint32_t pr_shown_match;
static int pr_hist[16], pr_nhist; static uint32_t pr_hist_match, pr_hist_round;
static const Col C_BLUE = { 0.498f, 0.659f, 1.0f, 1 }, C_RED = { 1.0f, 0.498f, 0.498f, 1 };
static inline Obj u_go_tr(Obj go) { return mm_is(go, K_GO) ? F(0x50F300C, Obj (*)(Obj, void *))(go, NULL) : NULL; }   /* GameObject.transform */
static Obj pvb(int k) { Obj o = u_gc_get(h_pvb[k]); return u_alive(o) ? o : NULL; }
static Obj pvt(uintptr_t h) { Obj o = u_gc_get(h); return u_alive(o) ? o : NULL; }
static int pvp_ui_present(int *ids, char (*names)[32], int max) {
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL; if (!c) return 0;
    uint32_t pm = dsnet_present_mask(c); int n = 0;
    for (int p = 0; p < 32 && n < max; p++) if (pm >> p & 1) {
        const char *nm = dsnet_player_name(c, p);
        ids[n] = p; snprintf(names[n], 32, "%s", nm && nm[0] ? nm : p == 0 ? TR("Host") : TR("Player")); dsi_utf8_fix_end(names[n]); n++;
    }
    return n;
}
static void pv_default_teams(void) {
    int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8), cnt[3] = { 0, 0, 0 };
    /* UX pass 2026-10-21: a newcomer goes to the smaller team. Before, the count restarted at Blue for each new player:
       after a refused Start with the host alone (host Blue), the friend who joined was put on Blue too, and every Start
       was refused with "Red has 0" */
    for (int i = 0; i < n; i++) if (pv.team_set[ids[i]] && pv.team[ids[i]] >= PVP_TEAM_BLUE && pv.team[ids[i]] <= PVP_TEAM_RED) cnt[pv.team[ids[i]]]++;
    for (int i = 0; i < n; i++) if (!pv.team_set[ids[i]]) {
        int t = cnt[PVP_TEAM_RED] < cnt[PVP_TEAM_BLUE] ? PVP_TEAM_RED : PVP_TEAM_BLUE;
        pv.team[ids[i]] = t; pv.team_set[ids[i]] = 1; cnt[t]++;
    }
}
/* P1 (UX review 2026-10-25): a guest's window shows the host's window (pvp_rules.c mails it: rules + teams), not its own
   default choices. NULL = this is the host, or nothing came yet (then the defaults). */
static int pv_is_host(void) { return pvp_ui_is_host && pvp_ui_is_host(); }
static const pvpr_vals *pv_guest_view(uint8_t teams[PVPR_VIEW_TEAMS]) {
    static pvpr_vals dflt;
    if (pv_is_host()) return NULL;
    const pvpr_vals *v = pvp_rules_view(teams);
    if (v) return v;
    pvpr_defaults(&dflt); if (teams) memset(teams, 0, PVPR_VIEW_TEAMS);
    return &dflt;
}
static int pv_team_of(int id) {                                          /* Blue / Red as the host's window shows it */
    uint8_t t[PVPR_VIEW_TEAMS];
    if (id >= 0 && id < PVPR_VIEW_TEAMS && pv_guest_view(t) && (t[id] == PVP_TEAM_BLUE || t[id] == PVP_TEAM_RED)) return t[id];
    return id >= 0 && id < PVP_MAX_PLAYERS && pv.team[id] ? pv.team[id] : PVP_TEAM_BLUE;
}
static void pv_host_view(pvpr_vals *v) {                                 /* the host's rules as its window shows them */
    *v = *pvp_rules_local();
    pvpr_vals_set(v, "mode", pv.mode == PVP_MODE_CTF); pvpr_vals_set(v, "win", pv.bo); pvpr_vals_set(v, "round_time", pv.secs); pvpr_vals_set(v, "pause", pv.pause != 0);
}
static void pv_sync_once(void);
/* host, every 10 frames outside a match: what its window shows (rules, teams, who is here) changed, or a guest asked = mail
   it to every other player (at most 4 times a second). The guests' open windows redraw within 15 frames. */
static void pv_host_sync(const pvp_state *s) {
    static uint64_t last_sig; static double last_t; static void *last_sess;
    mp_session *ss = mp_game_session(); dsnet_client *c = ss ? mp_client(ss) : NULL;
    if (!c || !pv_is_host() || (s && s->phase != PVP_OFF)) { if (!c) last_sess = NULL; return; }
    if ((void *)ss != last_sess) { last_sess = ss; last_sig = 0; }
    if (frames % 10) return;
    pv_sync_once(); pv_default_teams();
    pvpr_vals v; pv_host_view(&v);
    uint8_t teams[PVPR_VIEW_TEAMS] = { 0 }; uint32_t pm = dsnet_present_mask(c);
    for (int p = 0; p < PVPR_VIEW_TEAMS && p < PVP_MAX_PLAYERS; p++) if (pm >> p & 1) teams[p] = (uint8_t)pv_team_of(p);   /* the host's own: pv.team */
    uint64_t sig = pvpr_hash(&v) ^ ((uint64_t)pm << 1) * 1099511628211ULL;
    for (int p = 0; p < PVPR_VIEW_TEAMS; p++) sig = (sig ^ teams[p]) * 1099511628211ULL;
    static int asked; asked |= pvp_rules_view_asked();
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); double now = ts.tv_sec + ts.tv_nsec / 1e9;
    if ((sig == last_sig && !asked) || now - last_t < 0.25) return;
    if (pvp_rules_view_send(&v, teams) != -2) { last_sig = sig; last_t = now; asked = 0; }   /* a failed mail: the next change or ask sends again */
}
/* the rule shown: in a match the one sent with SETUP; else the host's choice (a guest: as the host last told it) */
static int pv_vanilla_shown(const pvp_state *s, int host) {
    if (s && s->phase != PVP_OFF) return s->rules.vanilla;
    if (host) return pv.vanilla;
    int t = pvp_mods_told_vanilla ? pvp_mods_told_vanilla() : -1;
    return t >= 0 ? t : 1;
}
static void pv_value(int r, char *out, size_t cap) {
    const pvpr_vals *gv = pv_guest_view(NULL);                         /* P1: a guest shows the host's window */
    if (pvpr_row(r) >= 0) { pvpr_ui_value(pvpr_row(r), gv ? gv : pvp_rules_local(), pv_here(), out, cap); return; }   /* map, floor, visibility (pvp_rules.c) */
    int mode = gv ? (pvpr_vals_get(gv, "mode") ? PVP_MODE_CTF : PVP_MODE_FIGHT) : pv.mode, bo = gv ? pvpr_vals_get(gv, "win") : pv.bo,
        secs = gv ? pvpr_vals_get(gv, "round_time") : pv.secs, pause = gv ? pvpr_vals_get(gv, "pause") : pv.pause;
    if (bo < 0 || bo > 2) bo = 1; if (secs < 0 || secs > 4) secs = 2;
    switch (r) {
    case PVR_VANILLA: { const pvp_state *s = pvp_ui_state ? pvp_ui_state() : NULL;
                        snprintf(out, cap, "%s", pv_vanilla_shown(s, pvp_ui_is_host && pvp_ui_is_host()) ? TR("On: vanilla only") : TR("Off: mods allowed")); break; }
    case PVR_MODE: snprintf(out, cap, "%s", mode == PVP_MODE_CTF ? TR("Capture the Flag") : TR("Fight")); break;
    case PVR_WIN: if (mode == PVP_MODE_CTF) snprintf(out, cap, TRN("%d capture to win", "%d captures to win", pv_bo[bo]), pv_bo[bo]);
                  else snprintf(out, cap, TRN("Best of %d", "Best of %d", pv_bo[bo]), pv_bo[bo]); break;   /* TRN: "1 Runde" / "3 Runden" */
    case PVR_TIME: snprintf(out, cap, TR("%d:%02d per round"), pv_secs[secs] / 60, pv_secs[secs] % 60); break;
    case PVR_PAUSE: snprintf(out, cap, "%s", pause ? TR("Planning pause every 7 s") : TR("No pause")); break;
    }
    dsi_utf8_fix_end(out);
}
static const char *pv_label[PVR_N] = { TR_NOOP("Mode"), TR_NOOP("To Win"), TR_NOOP("Round Time"), TR_NOOP("Pause"), TR_NOOP("Arena"), NULL, TR_NOOP("Vanilla Check") };   /* TR() when drawn; NULL: pvp_rules.c */
static const char *pv_tip[PVR_N] = {
    TR_NOOP("Fight: a team wins the round when the other team has no unit left standing. Capture the Flag: take the other team's banner flag and bring it to your own banner while your flag is home."),
    TR_NOOP("Fight: how many rounds are played at most; the first team to win most of them wins the match. Capture the Flag: how many captures win the match."),
    TR_NOOP("Time for one round of game time at normal speed. When it runs out: Fight gives the round to the team with more health left; Capture the Flag counts it as a draw."),
    TR_NOOP("No pause: nobody can pause during the match. Planning pause: the game stops every 7 seconds of fighting so everyone can give orders; it goes on after 5 seconds or when everyone pressed Ready."),
    NULL,                                                                /* Arena: pvpr_ui_tip(0) */
    NULL,                                                                /* Floor */
    TR_NOOP("On: vanilla only. No gameplay mods for anyone. Client mods (screen and controls) are fine.\n\nOff: mods allowed. The host's gameplay mods are required, and the launcher shares them with every player.") };
static const char *pv_lab(int r) { return pvpr_row(r) > 0 ? pvpr_ui_label(pvpr_row(r)) : TR(pv_label[r]); }   /* Arena keeps its own word */
static const char *pv_tipf(int r) { return pvpr_row(r) >= 0 ? pvpr_ui_tip(pvpr_row(r)) : TR(pv_tip[r]); }

static void pv_build(Obj v) {
    if (!scan_done) assets_scan_now();
    Obj src = AT(v, 0x70, Obj); if (!u_alive(src)) return;
    Obj tpl_img = first_image(u_tr(src), 0), tpl_txt = AT(src, 0x28, Obj);
    const float W = PV_W, H = PV_H;
    #define PX(x) ((x) - W / 2)
    #define PY(y) (H / 2 - (y))
    #define KEEP(hh, o) { Obj o_ = (o); u_gc_free(hh); hh = o_ ? u_gc_new(o_) : 0; }
    Obj root = mk_img(tpl_img, u_tr(v), -1, (Col){ 0, 0, 0, 0 }, 0, 0, W, H);
    if (!root) return;
    Obj R = u_tr(root);
    KEEP(h_pv_root, root); KEEP(h_pv_view, v); pv_k = 1; KEEP(h_pv_area, u_tr_parent(u_tr_parent(u_tr(src))));
    mk_img(tpl_img, R, S_BOX, C_WHITE, 0, 0, W, H);
    mk_img(tpl_img, R, -1, C_BAND, 0, PY(32), W - 4, 60);
    mk_img(tpl_img, R, -1, C_COPPER, 0, PY(62), W - 4, 1);
    KEEP(h_pv_title, mk_txt(tpl_txt, R, 1, 30, 514, 0, 0, PY(32), 600, 50, C_MAIN));
    mk_img(tpl_img, R, -1, C_ROW1, 0, PY(84), W - 4, 42);
    KEEP(h_pv_hint, mk_txt(tpl_txt, R, 0, 16, 514, 0, 0, PY(84), W - 60, 30, C_SUB));
    /* teams: two columns */
    for (int t = 1; t <= 2; t++) {
        float x0 = PV_COLX(t - 1), cx = x0 + PV_COLW / 2;
        KEEP(h_pv_teamh[t], mk_txt(tpl_txt, R, 1, 22, 514, 0, PX(cx), PY(128), PV_COLW - 20, 32, t == 1 ? C_BLUE : C_RED));
        mk_img(tpl_img, R, -1, C_LINE, PX(cx), PY(148), PV_COLW - 12, 1);
        KEEP(h_pv_units[t], mk_txt(tpl_txt, R, 0, pv_fs(13), 514, 0, PX(cx), PY(PV_TEAM_TOP + PV_TEAM_ROWS * PV_BLOCK + 14), PV_COLW - 20, 20, C_SUB));
    }
    for (int i = 0; i < 8; i++) {                                     /* player buttons + their units; placed in their column at refresh */
        Obj b = mk_button(src, R, 1, S_NBTN, C_WHITE, 0, 0, 330, 32, 16, "", C_MAIN);
        KEEP(h_pvb[PVB_P0 + i], b); u_active(b, false);
        for (int k = 0; k < 4; k++) {
            Obj fr = mk_img(tpl_img, R, -1, C_LINE, 0, 0, 44, 44); u_active(fr, false);      /* copper edge */
            Obj im = fr ? mk_img(tpl_img, u_tr(fr), -1, C_ROW2, 0, 0, 42, 42) : NULL;
            KEEP(h_pv_por[i][k], fr);
            if (fr && im) { u_gc_free(h_pv_pname[i][k]); h_pv_pname[i][k] = 0; }
            Obj nt = mk_txt(tpl_txt, R, 0, pv_fs(11), 514, 0, 0, 0, 80, 16, C_SUB); u_active(nt, false);
            KEEP(h_pv_pname[i][k], nt);
        }
    }
    /* rules */
    mk_img(tpl_img, R, -1, C_LINE, 0, PY(PV_RULES_TOP), W - 48, 1);
    { Obj t = mk_txt(tpl_txt, R, 1, 20, 514, 0, 0, PY(PV_RULES_TOP + 18), 300, 28, C_GOLD); cmt_set(t, TR("Rules")); }
    /* each row: label | < | value | >, in its column (x0 = the column's left edge); r = PVR_N is the Preset row */
    for (int r = 0; r <= PVR_N; r++) {
        int c = r < PVR_VIS0 ? 0 : 1, i = r < PVR_VIS0 ? r : r - PVR_VIS0; float x0 = PV_COLX(c), yc = PY(PV_ROWY(i));
        Obj bg = mk_img(tpl_img, R, -1, i % 2 ? C_ROW2 : C_ROW1, PX(x0 + PV_COLW / 2), yc, PV_COLW, 36);
        Obj lab = mk_txt(tpl_txt, R, 0, 15, 513, 0, PX(x0 + 12 + 98), yc, 196, 34, r == PVR_N ? C_GOLD : C_SUB);
        Obj val = mk_txt(tpl_txt, R, 0, 15, 514, 1, PX(x0 + 382), yc, 272, 36, C_MAIN);
        if (r < PVR_N) { KEEP(h_pv_rrow[r], bg); KEEP(h_pv_rlab[r], lab); KEEP(h_pv_rval[r], val); cmt_set(lab, pv_lab(r)); }
        else { KEEP(h_pv_prow, bg); KEEP(h_pv_plab, lab); KEEP(h_pv_pval, val); cmt_set(lab, TR("Preset")); }
        for (int a = 0; a < 2; a++) {
            Obj b = mk_button(src, R, 1, S_NBTN, C_WHITE, PX(a ? x0 + PV_COLW - 18 : x0 + 226), yc, 28, 28, 16, a ? "\xE2\x80\xBA" : "\xE2\x80\xB9", C_MAIN);
            KEEP(h_pvb[r < PVR_N ? PVB_ARROW0 + 2 * r + a : a ? PVB_PRE_R : PVB_PRE_L], b);
        }
    }
    /* presets (host, this computer only): Save as preset... / Delete preset; naming: the name field, Save, Cancel */
    { float x0 = PV_COLX(1), yc = PY(PV_PBTN_Y);
      const float bx[4] = { 139, PV_COLW - 139, 370, 496 }, bw[4] = { 270, 270, 120, 120 };
      static const char *bl[4] = { TR_NOOP("Save as Preset..."), TR_NOOP("Delete Preset"), TR_NOOP("Save"), TR_NOOP("Cancel") };
      for (int k = 0; k < 4; k++) {
          Obj b = mk_button(src, R, 1, S_NBTN, C_WHITE, PX(x0 + bx[k]), yc, bw[k], 34, 15, k == 3 ? cmt_game("TEXTKEY_Common_Cancel", "Cancel") : TR(bl[k]), C_MAIN);
          KEEP(h_pvb[PVB_PRE_SAVEAS + k], b); if (k >= 2) u_active(b, false);
      }
      u_gc_free(h_pv_field); h_pv_field = 0; pre_naming = 0; }               /* the name field: made when Save as preset... opens */
    /* the Vanilla check line (a guest's list of mods to turn off can be long: two lines) and the note (a refusal: three) */
    KEEP(h_pv_vsub, mk_txt(tpl_txt, R, 0, pv_fs(13), 514, 1, 0, PY(PV_PBTN_Y + 17 + 24), W - 60, 40, C_SUB));
    KEEP(h_pv_note, mk_txt(tpl_txt, R, 0, pv_fs(13), 514, 1, 0, PY(H - 100), W - 60, 56, C_ORANGE));
    mk_img(tpl_img, R, -1, C_BAND, 0, PY(H - 32), W - 4, 60);
    mk_img(tpl_img, R, -1, C_COPPER, 0, PY(H - 62), W - 4, 1);
    KEEP(h_pvb[PVB_CLOSE], mk_button(src, R, 1, S_NBTN, C_WHITE, PX(24 + 90), PY(H - 32), 180, 34, 16, cmt_game("TEXTKEY_Common_Close", "Close"), C_MAIN));
    KEEP(h_pvb[PVB_START], mk_button(src, R, 1, S_RECRUIT, C_WHITE, PX(W - 24 - 110), PY(H - 32), 220, 42, 18, TR("Start Match"), C_START));
    KEEP(h_pvb[PVB_BRING], mk_button(src, R, 1, S_NBTN, C_WHITE, 0, PY(H - 32), 220, 34, 16, TR("Bring Units..."), C_MAIN));
    u_active(root, false);
    /* MOVE RULE (owner 2026-10-25): a right-drag moves the window, its place is kept (the loader's ui_places.tsv) and put
       back when it is built again. Never placed after this (pv_fit only scales it). */
    dsl_movable(mu_kit(), root, "multiplayer.pvp_match");
    #undef PX
    #undef PY
    #undef KEEP
    rig_log("pvp ui: PvP window built");
}
/* M5 (UX review 2026-10-25, owner: one rule for every multiplayer window): a window follows the game's UI scale (Settings >
   UI scale, 0.75..1.4) like Bring Units: its root's scale = that scale, then smaller only as much as the screen needs (16 px
   free on each side). Measured in pixels about its centre (the Esc menu under it does not carry the UI scale: the window was
   the same size at 1.0 and 1.4). Its buttons are Unity UI buttons: clicks follow the scale. Run at each refresh, so a new
   scale or screen size applies at once. */
static float pv_ui_scale(void);
static void win_fit(Obj root, float *kc, const char *what) {
    float r[4];
    float sw = (float)F(0x5092C94, int (*)(void *))(NULL), sh = (float)F(0x5092CD4, int (*)(void *))(NULL);   /* Screen.width / height */
    if (!root || *kc <= 0 || sw <= 0 || sh <= 0 || !rect_of(root, r) || r[2] <= r[0] || r[3] <= r[1]) return;
    float fw = (r[2] - r[0]) / *kc, fh = (r[3] - r[1]) / *kc, ui = pv_ui_scale(), k = ui;
    if ((sw - 32) / fw < k) k = (sw - 32) / fw;
    if ((sh - 32) / fh < k) k = (sh - 32) / fh;
    if (k < 0.3f) k = 0.3f;
    if (k - *kc > 0.002f || *kc - k > 0.002f) {
        *kc = k; F(0x510AFF8, void (*)(Obj, V3, void *))(u_tr(root), (V3){ k, k, 1 }, NULL);   /* localScale */
        rig_log("pvp ui: %s %.0fx%.0f px at scale 1 on a %.0fx%.0f screen, UI scale %.2f: scale %.3f", what, fw, fh, sw, sh, ui, k);
    }
}
static float br_k = 1, pr_k = 1;                                         /* the Bring (text list) and result windows' scales */
static void pv_fit(void) { win_fit(pvt(h_pv_root), &pv_k, "PvP window"); }
static void pv_refresh(void) {
    const pvp_state *s = pvp_ui_state ? pvp_ui_state() : NULL;
    int host = pvp_ui_is_host && pvp_ui_is_host();
    pv_default_teams();
    cmt_set(pvt(h_pv_title), TR("PvP Match"));
    int busy_match = s && s->phase != PVP_OFF;
    cmt_set(pvt(h_pv_hint), busy_match ? TR("A match is set up or running.") : host ? TR("Choose the teams and the rules, then press Start Match.") : TR("The host chooses the teams and the rules."));
    int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8), row[3] = { 0, 0, 0 };
    mp_session *ss = mp_game_session(); dsnet_client *c = ss ? mp_client(ss) : NULL;
    int units[3] = { 0, 0, 0 }; pv_ncol = 0;
    static void *kparts; if (!kparts) kparts = class_by_name("Refactor.UI", "SubUI_UnitShortCutParts");
    Obj parr = kparts ? F(0x50FCCC0, Obj (*)(Obj, void *))(F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(kparts)), NULL) : NULL;
    int nparts = parr ? (int)AT(parr, 0x18, uintptr_t) : 0;
    float H2 = PV_H / 2.0f;
    for (int i = 0; i < 8; i++) {
        Obj b = pvb(PVB_P0 + i);
        int shown = i < n && row[pv_team_of(ids[i])] < PV_TEAM_ROWS;
        if (!shown) { u_active(b, false); for (int k = 0; k < 4; k++) { u_active(pvt(h_pv_por[i][k]), false); u_active(pvt(h_pv_pname[i][k]), false); } continue; }
        int t = pv_team_of(ids[i]);                                      /* P1: a guest shows the host's teams */
        float x0 = PV_COLX(t - 1), top = PV_TEAM_TOP + PV_BLOCK * row[t]; row[t]++;
        place(b, x0 + PV_COLW / 2 - PV_W / 2.0f, H2 - (top + 16), 330, 32);
        char l[96]; if (ids[i] == (c ? dsnet_player_id(c) : -1)) snprintf(l, sizeof l, TR("%s (you)"), nm[i]); else snprintf(l, sizeof l, "%s", nm[i]);
        cmt_set(btn_label(b, 1), l); txt_color(btn_label(b, 1), t == 1 ? C_BLUE : C_RED);
        u_active(b, true);
        /* before the match, when units were brought: show the brought units (they fight; the world's units step aside),
           from the host's roster (pvp_units.c); else the units this player owns in the world */
        int nr = !busy_match && pvp_units_roster ? pvp_units_roster(-1, NULL, NULL, NULL) : 0;
        if (nr > 0) {
            uint8_t pu[16]; char ph[33] = ""; if (c && !dsnet_player_uid(c, ids[i], pu)) for (int q = 0; q < 16; q++) snprintf(ph + 2 * q, 3, "%02x", pu[q]);
            int shown_u = 0;
            for (int r = 0; r < nr && shown_u < 4; r++) {
                char ru[33], rn[64], rp[64]; pvp_units_roster(r, ru, rn, rp);
                if (strcmp(ru, ph)) continue;
                Obj fr = pvt(h_pv_por[i][shown_u]), nt = pvt(h_pv_pname[i][shown_u]);
                float cx = x0 + PV_COLW / 2 - PV_W / 2.0f - 1.5f * 82 + 82 * shown_u++;
                place(fr, cx, H2 - (top + 32 + 4 + 22), 44, 44);
                Obj sp = rp[0] ? portrait_sprite(rp) : NULL;
                Obj inner = mm_is(u_tr(fr), K_TR) && F(0x510EAE4, int (*)(Obj, void *))(u_tr(fr), NULL) > 0 ? F(0x510FC60, Obj (*)(Obj, int, void *))(u_tr(fr), 0, NULL) : NULL;
                Obj inim = inner ? image_of(inner) : NULL;
                if (inim) img_set(inim, sp, sp ? C_WHITE : C_ROW2);
                u_active(fr, true);
                char un[64] = ""; Obj txt = !strncmp(rn, "TEXTKEY", 7) ? F(0x80EBAC, Obj (*)(Obj, void *))(u_str(rn), NULL) : NULL;   /* TextKeyExtensions.GetText */
                if (txt) utf16_to_utf8(txt, un, sizeof un);
                if (!un[0] || !strncmp(un, "TEXTKEY", 7)) snprintf(un, sizeof un, "%s", rn);   /* a typed name is the key itself */
                place(nt, cx, H2 - (top + 32 + 4 + 44 + 10), 80, 16); cmt_set(nt, un); u_active(nt, true);
                if (pv_ncol < 16) snprintf(pv_col_names[pv_ncol++], 64, "%s", un);
            }
            units[t] += shown_u;
            for (int k = shown_u; k < 4; k++) { u_active(pvt(h_pv_por[i][k]), false); u_active(pvt(h_pv_pname[i][k]), false); }
            continue;
        }
        uint8_t g[8][16]; int nu = c ? dsnet_owned_list(c, ids[i], g, 8) : 0; if (nu > 8) nu = 8;
        units[t] += nu;
        int shown_u = 0;
        for (int k = 0; k < 4; k++) {
            Obj fr = pvt(h_pv_por[i][k]), nt = pvt(h_pv_pname[i][k]);
            Guid gg; Obj u = NULL;
            if (k < nu) { memcpy(&gg, g[k], 16); u = find_unit(cur_map(), gg); if (!u && cur_map() != 0) u = find_unit(0, gg); }
            if (!u) { u_active(fr, false); u_active(nt, false); continue; }
            float cx = x0 + PV_COLW / 2 - PV_W / 2.0f - 1.5f * 82 + 82 * shown_u++;
            place(fr, cx, H2 - (top + 32 + 4 + 22), 44, 44); u_active(fr, true);
            Obj sp = NULL;
            for (int q = 0; q < nparts; q++) { Obj pt = AT((char *)parr + 0x20, q * 8, Obj); if (u_alive(pt) && AT(pt, 0x228, Obj) == u) {
                Obj im = AT(pt, 0x140, Obj); sp = mm_is(im, K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(im, NULL) : NULL; break; } }
            Obj inner = mm_is(u_tr(fr), K_TR) && F(0x510EAE4, int (*)(Obj, void *))(u_tr(fr), NULL) > 0 ? F(0x510FC60, Obj (*)(Obj, int, void *))(u_tr(fr), 0, NULL) : NULL;
            Obj inim = inner ? image_of(inner) : NULL;
            if (inim) img_set(inim, u_alive(sp) ? sp : NULL, u_alive(sp) ? C_WHITE : C_ROW2);
            char un[48] = ""; utf16_to_utf8(F(0xFA467C, Obj (*)(Obj, void *))(u, NULL), un, sizeof un);   /* GetDisplayName */
            place(nt, cx, H2 - (top + 32 + 4 + 44 + 10), 80, 16); cmt_set(nt, un); u_active(nt, true);
            if (pv_ncol < 16) snprintf(pv_col_names[pv_ncol++], 64, "%s", un);
        }
    }
    cmt_set(pvt(h_pv_teamh[1]), TRC("team", "Blue")); cmt_set(pvt(h_pv_teamh[2]), TRC("team", "Red"));
    for (int t = 1; t <= 2; t++) { char u[96]; snprintf(u, sizeof u, TRN("%d unit", "%d units", units[t]), units[t]); cmt_set(pvt(h_pv_units[t]), u); }
    const pvpr_vals *lv = pvp_rules_local(), *gv = pv_guest_view(NULL);
    for (int r = 0; r < PVR_N; r++) {
        char v[96]; pv_value(r, v, sizeof v); cmt_set(pvt(h_pv_rval[r]), v); cmt_set(pvt(h_pv_rlab[r]), pv_lab(r));
        int on = pvpr_row(r) < 0 || pvpr_ui_steppable(pvpr_row(r), gv ? gv : lv);   /* Floor: only for Dungeon 1 / 2 */
        for (int a = 0; a < 2; a++) u_active(pvb(PVB_ARROW0 + 2 * r + a), host && !busy_match && on);
        txt_color(pvt(h_pv_rval[r]), on ? C_MAIN : C_SUB);
    }
    /* presets: the host's own (this computer); the arrows apply one at once */
    { pvpr_store *ps = pvp_rules_store(); int cur = pvpr_preset_match(ps, lv), pon = host && !busy_match; char pvv[96];
      if (ps->npresets <= 0) snprintf(pvv, sizeof pvv, "%s", TR("No presets yet"));
      else if (cur < 0) snprintf(pvv, sizeof pvv, "%s", TR("Custom"));
      else snprintf(pvv, sizeof pvv, "%s", ps->presets[cur].name);
      dsi_utf8_fix_end(pvv); cmt_set(pvt(h_pv_pval), pvv); txt_color(pvt(h_pv_pval), cur >= 0 ? C_GOLD : C_SUB); cmt_set(pvt(h_pv_plab), TR("Preset"));
      u_active(pvt(h_pv_prow), pon); u_active(pvt(h_pv_plab), pon); u_active(pvt(h_pv_pval), pon);
      for (int a = 0; a < 2; a++) u_active(pvb(a ? PVB_PRE_R : PVB_PRE_L), pon && ps->npresets > 0);
      if (!pon && pre_naming) pre_naming_set(0);
      u_active(pvb(PVB_PRE_SAVEAS), pon && !pre_naming); u_active(pvb(PVB_PRE_DEL), pon && !pre_naming);
      u_active(pvb(PVB_PRE_SAVE), pon && pre_naming); u_active(pvb(PVB_PRE_CANCEL), pon && pre_naming);
      Obj dl = pvb(PVB_PRE_DEL); if (dl) txt_color(btn_label(dl, 1), cur >= 0 ? C_MAIN : C_SUB); }   /* dim: nothing to delete */
    const char *note = "";
    const char *refused = pvp_mods_last ? pvp_mods_last() : "";
    if (!busy_match && pv_err[0]) note = pv_err;                                        /* host: Start Match was refused */
    else if (host && !busy_match && pre_msg[0]) note = pre_msg;                        /* host: a preset was saved / applied / deleted */
    else if (!host && !busy_match && refused && refused[0]) note = refused;            /* guest: the host's refusal */
    else if (host && !busy_match && (!units[1] || !units[2])) note = TR("Each team needs at least one unit. Right-click a unit's portrait to take it.");
    else if (host && !busy_match) note = TR("Nothing that happens in the match stays: the world comes back as it was before.");
    { char vs[400] = ""; if (pvp_mods_summary) pvp_mods_summary(pv_vanilla_shown(s, host), host, vs, sizeof vs); cmt_set(pvt(h_pv_vsub), vs); }
    u_active(pvb(PVB_START), host && !busy_match);
    u_active(pvb(PVB_BRING), !busy_match && pvp_units_exec != NULL);
    char nb[400];
    if (!busy_match && note != pv_err && note != refused && pvp_units_roster && pvp_units_roster(-1, NULL, NULL, NULL) > 0) {
        int nr = pvp_units_roster(-1, NULL, NULL, NULL);
        snprintf(nb, sizeof nb, TRN("The teams fight with the %d brought unit shown above; the world's units step aside until the end.",
                                    "The teams fight with the %d brought units shown above; the world's units step aside until the end.", nr), nr);
        note = nb;
    }
    cmt_set(pvt(h_pv_note), note); snprintf(pv_note_txt, sizeof pv_note_txt, "%s", note);
    pv_note_err = note[0] && (note == pv_err || note == refused);                     /* M2: a refusal in red (the lobby's #F16071) */
    txt_color(pvt(h_pv_note), pv_note_err ? C_WARN : C_ORANGE);
    pv_fit();
}
static void pv_show(int on) {
    Obj root = pvt(h_pv_root), area = u_gc_get(h_pv_area);
    if (!root) return;
    u_active(root, on); if (u_alive(area)) u_active(area, !on);
    pv_open = on;
    if (on) {
        int host = pvp_ui_is_host && pvp_ui_is_host();
        pv_sync_once();
        if (pv.vanilla < 0) pv.vanilla = pvp_mods_default_vanilla ? pvp_mods_default_vanilla() : 1;
        pv_err[0] = 0;
        if (host && pvp_mods_ask) pvp_mods_ask(pv.vanilla);                 /* every player sends its mods (and sees the rule) */
        if (!host) pvp_rules_view_ask();                                   /* P1: the host mails its window (pv_host_sync) */
        pv_refresh();
        /* CLOSE RULE: the red X; the bottom Close (same action: closes the window, nothing is sent) goes when it is there */
        Obj x = pv_kit_x() ? mpesc_close_x(&esc_pv, root, PV_XX(PV_W), PV_XY, PV_XS, root) : NULL;
        u_active(pvb(PVB_CLOSE), x == NULL);
    }
}
/* M2 (UX review 2026-10-25): the players and units each team would start with, counted as the match start counts them
   (pvp_core.c start: the units each player of the team owns; brought units: the host's roster) */
static void pv_team_count(int players[3], int units[3]) {
    for (int t = 0; t < 3; t++) players[t] = units[t] = 0;
    int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8);
    mp_session *ss = mp_game_session(); dsnet_client *c = ss ? mp_client(ss) : NULL;
    int nr = pvp_units_roster ? pvp_units_roster(-1, NULL, NULL, NULL) : 0;
    for (int i = 0; i < n && c; i++) {
        int t = pv_team_of(ids[i]); players[t]++;
        if (nr > 0) {
            uint8_t pu[16]; char ph[33] = ""; if (!dsnet_player_uid(c, ids[i], pu)) for (int q = 0; q < 16; q++) snprintf(ph + 2 * q, 3, "%02x", pu[q]);
            for (int r = 0; r < nr; r++) { char ru[33], rn[64], rp[64]; pvp_units_roster(r, ru, rn, rp); if (!strcmp(ru, ph)) units[t]++; }
        } else { static uint8_t g[64][16]; int k = dsnet_owned_list(c, ids[i], g, 64); if (k > 0) units[t] += k; }
    }
}
static void pv_start(void) {
    uint8_t teams[PVP_MAX_PLAYERS] = { 0 };
    int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8);
    for (int i = 0; i < n; i++) teams[ids[i]] = (uint8_t)pv_team_of(ids[i]);
    /* M2: a team with no unit: the match start would refuse it after the window closed (nothing happened, no message).
       Nothing is sent; the window stays, the reason in red */
    { int pl[3], un[3]; pv_team_count(pl, un);
      if (!un[PVP_TEAM_BLUE] || !un[PVP_TEAM_RED]) {
          snprintf(pv_err, sizeof pv_err, "%s", !pl[PVP_TEAM_BLUE] || !pl[PVP_TEAM_RED] ? TR("Each team needs at least one player. Click a player's name to move them to the other team.")
                                                                                        : TR("Each team needs at least one unit. Right-click a unit's portrait to take it."));
          rig_log("pvp ui: Start Match refused before sending: Blue %d player(s) %d unit(s), Red %d player(s) %d unit(s)", pl[1], un[1], pl[2], un[2]);
          pv_refresh(); return;
      } }
    pvp_rules r = { (uint8_t)pv_bo[pv.bo], (uint16_t)pv_secs[pv.secs], (uint8_t)(pv.pause ? PVP_PAUSE_PLANNING : PVP_PAUSE_NONE), 7, 5, (uint8_t)(pv.vanilla != 0) };
    pv_sync_once(); pv_to_store();
    pvp_map_pick mp_; pv_pick_map(pv_here(), &mp_);
    int src = pvp_rules_send(&mp_);                                      /* every player gets the host's rules (+ the map) first */
    int rc = pvp_ui_start_match ? pvp_ui_start_match(pv.mode, teams, &r, mp_.here ? -1 : mp_.region, mp_.floor, mp_.seed) : -9;   /* region -1: here */
    rig_log("pvp ui: Start Match: rules sent rc %d, map %s region %d floor %d seed %d", src, mp_.here ? "here" : "dungeon", mp_.region, mp_.floor, mp_.seed);
    rig_log("pvp ui: Start Match: rc %d", rc);
    if (rc == -4) {                                                       /* Vanilla check refused: stay, say why */
        snprintf(pv_err, sizeof pv_err, "%s", pvp_mods_last ? pvp_mods_last() : TR("Start Match was refused."));
        pv_refresh(); return;
    }
    pv_show(0);
    Obj ev = esc_view(); if (ev && u_on(ev) && !rc) (mm_is(ev, K_SMV) ? F(0xF2ED1C, void (*)(Obj, void *))(ev, NULL) : (void)0);   /* back to the game */
}
static int br_click(int k);
static void pv_click_do(int k) {
    int host = pvp_ui_is_host && pvp_ui_is_host();
    if (br_click(k)) return;
    if (k == PVB_ESC) { pv_show(1); return; }
    if (k == PVB_CLOSE) { pv_show(0); return; }
    if (k == PVB_START && host) { pv_start(); return; }
    if (k == PVB_READY) { if (pvp_ui_ready) rig_log("pvp ui: Ready (rc %d)", pvp_ui_ready()); return; }
    if (k == PVB_FIGHT || k == PVB_PROP_YES || k == PVB_PROP_NO) {      /* E4: the fight vote */
        const pvp_state *vs = pvp_ui_state ? pvp_ui_state() : NULL; int v = vote_view(vs);
        int op = k == PVB_PROP_YES ? PVP_OP_AGREE : k == PVB_PROP_NO ? PVP_OP_NOTYET : v == 4 ? PVP_OP_CANCEL : PVP_OP_PROPOSE;
        if (pvp_ui_vote) rig_log("pvp ui: fight vote button %d (view %d): op %d rc %d", k, v, op, pvp_ui_vote(op));
        return;
    }
    if (k == PVB_RESULT_OK) { u_active(pvt(h_pr_root), false); pr_open = 0; Obj area = u_gc_get(h_pv_area); if (u_alive(area)) u_active(area, true);
                              Obj ev = esc_view(); if (ev && u_on(ev)) (mm_is(ev, K_SMV) ? F(0xF2ED1C, void (*)(Obj, void *))(ev, NULL) : (void)0); return; }
    if (!host) return;
    if (k >= PVB_P0 && k < PVB_P0 + 8) {
        int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8), i = k - PVB_P0;
        if (i < n) { pv.team[ids[i]] = pv.team[ids[i]] == PVP_TEAM_RED ? PVP_TEAM_BLUE : PVP_TEAM_RED; pv.team_set[ids[i]] = 1; pv_err[0] = 0; }
    } else if (k >= PVB_ARROW0 && k < PVB_ARROW0 + 2 * PVR_N) {
        int r = (k - PVB_ARROW0) / 2, d = (k - PVB_ARROW0) % 2 ? 1 : -1;
        if (r == PVR_MODE) pv.mode = pv.mode == PVP_MODE_FIGHT ? PVP_MODE_CTF : PVP_MODE_FIGHT;
        if (r == PVR_WIN) pv.bo = (pv.bo + d + 3) % 3;
        if (r == PVR_TIME) pv.secs = (pv.secs + d + 5) % 5;
        if (r == PVR_PAUSE) pv.pause = !pv.pause;
        if (r == PVR_VANILLA) { pv.vanilla = !pv.vanilla; if (pvp_mods_ask) pvp_mods_ask(pv.vanilla); }
        if (pvpr_row(r) >= 0 && pvpr_ui_steppable(pvpr_row(r), pvp_rules_local())) pvpr_ui_step(pvpr_row(r), pvp_rules_local(), d);   /* map, floor, visibility */
        pv_to_store();
        pv_err[0] = 0; pre_msg[0] = 0;
    } else if (k == PVB_PRE_L || k == PVB_PRE_R) pre_step(k == PVB_PRE_R ? 1 : -1);   /* presets */
    else if (k == PVB_PRE_SAVEAS) { pre_naming_set(1); pre_msg[0] = 0; }
    else if (k == PVB_PRE_DEL) pre_delete();
    else if (k == PVB_PRE_SAVE) pre_save(NULL);
    else if (k == PVB_PRE_CANCEL) pre_naming_set(0);
    pv_refresh();
}

/* ---- Bring Units window (M3c): pick one of YOUR saves, pick units from it (at most your share of the 4-unit dungeon
   team), Bring = they are copied from that save and sent to the host (pvp_units.c). Same frame as the PvP window. */
#include <dirent.h>
#include <sys/stat.h>
int mu_save_files(const char *dir, char (*out)[1024], int max);   /* mu_save.c */
static uintptr_t h_br_root, h_br_title, h_br_hint, h_br_status, h_br_por[12], h_br_lv[12];
static uintptr_t h_br_tile[12];                                          /* CARD RULE: a unit's whole tile (portrait, name, level), invisible */
static int br_nsaves, br_save = -1, br_nunits, br_pick[12];
static char br_files[8][300], br_stems[8][96], br_unit_guid[12][40], br_unit_name[12][48], br_unit_port[12][64], br_sent[600];
static int br_unit_lvl[12];
static int br_share(void) {                                              /* 4 units in total, split between the players */
    int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8); if (n < 1) n = 1;
    int sh = 4 / n; return sh < 1 ? 1 : sh;
}
/* E1 (owner 2026-10-20, v0.1.20 PvP session: "Bring World does not list units from the regular saves"): the player's saves
   are in TWO folders in the modded game (loader dsl_saves.c): the modded worlds in <persistentDataPath>/Saves (= DSML_DATA/Saves)
   and the normal game's own worlds in DSML_GAME_SAVES (set by both starters; the Load list shows both). The PvP lists read
   only the first one, so a player's regular worlds were never offered. Both folders now, newest first; a modded file with
   the name of a game save (an old first-start copy) is left out: the game's one is listed, as in the Load list. */
static const char *pv_game_saves(void) { const char *g = getenv("DSML_GAME_SAVES"); return g && *g ? g : NULL; }
int pvp_bring_save_paths(char (*out)[1024], int max) {
    static char f[2][64][1024]; int nf[2] = { 0, 0 };
    Obj ps = F(0x507B74C, Obj (*)(void *))(NULL);                         /* Application.persistentDataPath (the copy's own data) */
    char base[600] = ""; str_utf8(ps, base, sizeof base);
    char dir[700]; snprintf(dir, sizeof dir, "%s/Saves", base);
    const char *gd = pv_game_saves();
    if (gd) nf[0] = mu_save_files(gd, f[0], 64);
    if (base[0]) nf[1] = mu_save_files(dir, f[1], 64);
    struct { int d, i; time_t t; } all[128]; int n = 0;
    for (int d = 0; d < 2; d++) for (int i = 0; i < nf[d]; i++) {
        const char *nm = strrchr(f[d][i], '/'), *b2 = strrchr(f[d][i], '\\'); if (b2 > nm) nm = b2; nm = nm ? nm + 1 : f[d][i];
        int dup = 0;
        for (int k = 0; d == 1 && k < nf[0] && !dup; k++) {
            const char *on = strrchr(f[0][k], '/'), *o2 = strrchr(f[0][k], '\\'); if (o2 > on) on = o2; on = on ? on + 1 : f[0][k];
            dup = !strcmp(on, nm);
        }
        if (dup) continue;
        struct stat sb; all[n].d = d; all[n].i = i; all[n].t = stat(f[d][i], &sb) == 0 ? sb.st_mtime : 0; n++;
    }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && all[j].t > all[j - 1].t; j--) { __typeof__(all[0]) t = all[j]; all[j] = all[j - 1]; all[j - 1] = t; }
    int k = 0;
    for (int i = 0; i < n && k < max; i++) snprintf(out[k++], 1024, "%s", f[all[i].d][all[i].i]);
    static int logged = -1;
    if (logged != n) { logged = n; rig_log("pvp ui: Bring saves: %d game (%s), %d modded, %d listed", nf[0], gd ? "DSML_GAME_SAVES" : "no game folder", nf[1], k); }
    return k;
}
static void br_list_saves(void) {
    static char p[8][1024];
    int n = pvp_bring_save_paths(p, 8);
    br_nsaves = n;
    for (int i = 0; i < n; i++) {
        snprintf(br_files[i], sizeof br_files[i], "%s", p[i]);
        const char *nm = strrchr(p[i], '/'), *b2 = strrchr(p[i], '\\'); if (b2 > nm) nm = b2; nm = nm ? nm + 1 : p[i];
        size_t l = strlen(nm); snprintf(br_stems[i], sizeof br_stems[i], "%.*s", (int)(l > 5 ? l - 5 : l), nm);
    }
}
static void br_read_units(int k) {
    br_nunits = 0; memset(br_pick, 0, sizeof br_pick);
    char err[200]; jv *sv = jload(br_files[k], err, sizeof err);
    if (!sv) { rig_log("pvp ui: save %s: %s", br_files[k], err); return; }
    jv *comps = jpath(sv, "EntityComponentSaveData.EntityComponents");
    const char *sides[2] = { "Base", "Dungeon" };
    for (int sd = 0; sd < 2; sd++) {
        jv *pl = jget(jget(jpath(sv, "EntityContainerSaveData.UnitContainerSaveData"), sides[sd]), "PlayerGuidList");
        for (size_t i = 0; pl && i < pl->cnt && br_nunits < 12; i++) {
            jv *g = pl->v[i]; if (!jis(g, JN_STR)) continue;
            jv *cl = jget(comps, g->s); if (!cl) continue;
            jv *st = fe_comp(cl, 0), *pr = fe_comp(cl, 10);
            int u = br_nunits++;
            snprintf(br_unit_guid[u], sizeof br_unit_guid[u], "%s", g->s);
            jv *nk = pr ? jget(pr, "UnitNameTextKey") : NULL, *pid = pr ? jget(pr, "UnitPortraitID") : NULL;
            char key[96] = ""; if (nk && nk->t == JN_STR) snprintf(key, sizeof key, "%.*s", (int)nk->n, nk->s);
            Obj txt = key[0] ? F(0x80EBAC, Obj (*)(Obj, void *))(u_str(key), NULL) : NULL;     /* TextKeyExtensions.GetText */
            br_unit_name[u][0] = 0; if (txt) utf16_to_utf8(txt, br_unit_name[u], sizeof br_unit_name[u]);
            if (!br_unit_name[u][0] || !strncmp(br_unit_name[u], "TEXTKEY", 7)) snprintf(br_unit_name[u], sizeof br_unit_name[u], "%s", key);   /* a renamed unit keeps its name in the key */
            snprintf(br_unit_port[u], sizeof br_unit_port[u], "%.*s", pid && pid->t == JN_STR ? (int)pid->n : 0, pid && pid->t == JN_STR ? pid->s : "");
            br_unit_lvl[u] = (int)jnumv(jpath(st, "ProgressionStats.Level"));
        }
    }
    jfree(sv);
}
static void br_build(Obj v) {
    Obj src = AT(v, 0x70, Obj); if (!u_alive(src)) return;
    Obj tpl_img = first_image(u_tr(src), 0), tpl_txt = AT(src, 0x28, Obj);
    const float W = BR_W, H = 700;
    #define BX(x) ((x) - W / 2)
    #define BY(y) (H / 2 - (y))
    #define KEEP(hh, o) { Obj o_ = (o); u_gc_free(hh); hh = o_ ? u_gc_new(o_) : 0; }
    Obj root = mk_img(tpl_img, u_tr(v), -1, (Col){ 0, 0, 0, 0 }, 0, 0, W, H); if (!root) return;
    Obj R = u_tr(root); KEEP(h_br_root, root); br_k = 1;
    mk_img(tpl_img, R, S_BOX, C_WHITE, 0, 0, W, H);
    mk_img(tpl_img, R, -1, C_BAND, 0, BY(32), W - 4, 60); mk_img(tpl_img, R, -1, C_COPPER, 0, BY(62), W - 4, 1);
    KEEP(h_br_title, mk_txt(tpl_txt, R, 1, 30, 514, 0, 0, BY(32), 600, 50, C_MAIN));
    mk_img(tpl_img, R, -1, C_ROW1, 0, BY(84), W - 4, 42);
    KEEP(h_br_hint, mk_txt(tpl_txt, R, 0, 15, 514, 0, 0, BY(84), 720, 30, C_SUB));
    { Obj t = mk_txt(tpl_txt, R, 1, 20, 514, 0, BX(24 + 120), BY(124), 240, 28, C_GOLD); cmt_set(t, TR("Your Saves")); }
    { Obj t = mk_txt(tpl_txt, R, 1, 20, 514, 0, BX(290 + 223), BY(124), 446, 28, C_GOLD); cmt_set(t, TR("Units")); }
    for (int i = 0; i < 8; i++) KEEP(h_pvb[PVB_BR_SAVE0 + i], mk_button(src, R, 1, S_NBTN, C_WHITE, BX(24 + 120), BY(160 + 44 * i), 240, 38, 13, "", C_MAIN));
    for (int i = 0; i < 12; i++) {
        float x = BX(290 + 74 + 150 * (i % 3)), top = 150 + 112 * (i / 3);       /* portrait, name button, level under it */
        Obj tile = mk_img(tpl_img, R, -1, (Col){ 0, 0, 0, 0 }, x, BY(top + 52), 146, 108);   /* the tile's area (br_tile_click) */
        if (tile && mm_is(tile, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(tile, false, NULL);   /* raycastTarget off: it only measures */
        KEEP(h_br_tile[i], tile);
        Obj fr = mk_img(tpl_img, R, -1, C_LINE, x, BY(top + 24), 44, 44); if (fr) mk_img(tpl_img, u_tr(fr), -1, C_ROW2, 0, 0, 42, 42);
        KEEP(h_br_por[i], fr);
        Obj b = mk_button(src, R, 1, S_NBTN, C_WHITE, x, BY(top + 66), 140, 30, 13, "", C_MAIN); KEEP(h_pvb[PVB_BR_UNIT0 + i], b);
        KEEP(h_br_lv[i], mk_txt(tpl_txt, R, 0, 11, 514, 0, x, BY(top + 92), 140, 16, C_SUB));
    }
    KEEP(h_br_status, mk_txt(tpl_txt, R, 0, 13, 514, 1, 0, BY(H - 90), W - 60, 34, C_ORANGE));
    mk_img(tpl_img, R, -1, C_BAND, 0, BY(H - 32), W - 4, 60); mk_img(tpl_img, R, -1, C_COPPER, 0, BY(H - 62), W - 4, 1);
    KEEP(h_pvb[PVB_BR_BACK], mk_button(src, R, 1, S_NBTN, C_WHITE, BX(24 + 90), BY(H - 32), 180, 34, 16, cmt_game("TEXTKEY_Shared_Back", "Back"), C_MAIN));
    KEEP(h_pvb[PVB_BR_SEND], mk_button(src, R, 1, S_RECRUIT, C_WHITE, BX(W - 24 - 110), BY(H - 32), 220, 42, 18, TR("Bring"), C_START));
    u_active(root, false);
    dsl_movable(mu_kit(), root, "multiplayer.pvp_bring");                  /* MOVE RULE: as the PvP window */
    #undef BX
    #undef BY
    #undef KEEP
}
static void br_refresh(void) {
    int share = br_share(), npick = 0; for (int i = 0; i < br_nunits; i++) npick += br_pick[i];
    cmt_set(pvt(h_br_title), TR("Bring Units"));
    char h[600]; snprintf(h, sizeof h, TRN("Pick up to %d unit from a save on your computer. Copies only: nothing is changed.",
                                         "Pick up to %d units from a save on your computer. Copies only: nothing is changed.", share), share);
    cmt_set(pvt(h_br_hint), h);
    for (int i = 0; i < 8; i++) {
        Obj b = pvb(PVB_BR_SAVE0 + i); u_active(b, i < br_nsaves);
        if (i < br_nsaves) { char st[40]; size_t L = strlen(br_stems[i]);
                             if (L > 26) { char c24[25]; snprintf(c24, sizeof c24, "%s", br_stems[i]); dsi_utf8_fix_end(c24);   /* long autosave names: cut at a character */
                                           snprintf(st, sizeof st, "%s\xE2\x80\xA6", c24); } else snprintf(st, sizeof st, "%s", br_stems[i]);
                             cmt_set(btn_label(b, 1), st); txt_color(btn_label(b, 1), i == br_save ? C_GOLD : C_MAIN); }
    }
    for (int i = 0; i < 12; i++) {
        Obj b = pvb(PVB_BR_UNIT0 + i), fr = pvt(h_br_por[i]), lv = pvt(h_br_lv[i]);
        int on = i < br_nunits; u_active(b, on); u_active(fr, on); u_active(lv, on); u_active(pvt(h_br_tile[i]), on);
        if (!on) continue;
        char l[80]; snprintf(l, sizeof l, "%s%s", br_pick[i] ? "\xE2\x80\xA2 " : "", br_unit_name[i]);   /* picked: gold with a dot (the font has no tick) */
        cmt_set(btn_label(b, 1), l); txt_color(btn_label(b, 1), br_pick[i] ? C_GOLD : C_MAIN);
        char lt[64]; cmt_level(lt, sizeof lt, br_unit_lvl[i]); cmt_set(lv, lt);
        Obj inner = mm_is(u_tr(fr), K_TR) && F(0x510EAE4, int (*)(Obj, void *))(u_tr(fr), NULL) > 0 ? F(0x510FC60, Obj (*)(Obj, int, void *))(u_tr(fr), 0, NULL) : NULL;
        Obj im = inner ? image_of(inner) : NULL, sp = br_unit_port[i][0] ? portrait_sprite(br_unit_port[i]) : NULL;
        if (im) img_set(im, sp, sp ? C_WHITE : C_ROW2);
        u_active(fr, sp != NULL);                                        /* no loaded portrait: no empty frame */
    }
    char st[600];
    if (br_sent[0]) snprintf(st, sizeof st, "%s", br_sent);
    else snprintf(st, sizeof st, TR("%d of %d picked."), npick, share);
    cmt_set(pvt(h_br_status), st);
    u_active(pvb(PVB_BR_SEND), npick > 0);
    win_fit(pvt(h_br_root), &br_k, "Bring Units window");                 /* M5 */
}
static int br_lobby;                                                    /* opened from the HUD (preparation): no PvP window behind it */
static void br_show(int on) {
    Obj root = pvt(h_br_root), pw = pvt(h_pv_root); if (!root) return;
    u_active(root, on); if (pw && !br_lobby) u_active(pw, !on);
    br_open = on;
    if (on) { br_list_saves(); br_refresh(); if (pv_kit_x()) mpesc_close_x(&esc_br, root, PV_XX(BR_W), PV_XY, PV_XS, root); }   /* CLOSE RULE: the red X = Back */
    else if (br_lobby) br_lobby = 0; else pv_refresh();
}
static void br_send(void) {
    if (br_save < 0 || !pvp_units_exec) return;
    char gl[600] = ""; size_t o = 0; int n = 0;
    for (int i = 0; i < br_nunits; i++) if (br_pick[i]) { o += (size_t)snprintf(gl + o, sizeof gl - o, "%s%s", n ? "," : "", br_unit_guid[i]); n++; }
    Buf b = { 0 }; bput(&b, "{\"cmd\":\"pvp_units_bring\",\"file\":"); bstr(&b, br_files[br_save]); bput(&b, ",\"guids\":"); bstr(&b, gl); bput(&b, "}");
    char *r = pvp_units_exec("pvp_units_bring", b.p); free(b.p);
    int ok = r && strstr(r, "\"ok\":true");
    char names[160] = ""; size_t q = 0; for (int i = 0, k = 0; i < br_nunits; i++) if (br_pick[i]) q += (size_t)snprintf(names + q, sizeof names - q, "%s%s", k++ ? ", " : "", br_unit_name[i]);
    char er[300] = ""; if (!ok && r) jstr(r, "error", er, sizeof er);
    if (ok) snprintf(br_sent, sizeof br_sent, TR("Brought: %s. The host starts the match with them."), names);
    else if (er[0]) { char em[400]; snprintf(br_sent, sizeof br_sent, TR("Could not bring the units: %s"), cmt_msg(er, em, sizeof em)); }
    else snprintf(br_sent, sizeof br_sent, "%s", TR("Could not bring the units. Try again."));
    rig_log("pvp ui: Bring %d unit(s) from %s: %s", n, br_files[br_save], r ? r : "(no answer)");
    free(r);
    br_refresh();
}
static int pv_bring_open(void);
static int br_click(int k) {                                            /* 1 = a Bring window button */
    if (k == PVB_BRING) { if (!pv_bring_open()) return 1; br_sent[0] = 0; br_show(1); return 1; }   /* units from this player's own saves */
    if (k == PVB_HUD_BRING) { if (pv_bring_open()) { br_lobby = 1; br_sent[0] = 0; br_show(1); } rig_log("pvp ui: Bring Units from the HUD (preparation)"); return 1; }   /* E2 */
    if (k == PVB_BR_BACK) { br_show(0); return 1; }
    if (k == PVB_BR_SEND) { br_send(); return 1; }
    if (k >= PVB_BR_SAVE0 && k < PVB_BR_SAVE0 + 8) { br_save = k - PVB_BR_SAVE0; br_sent[0] = 0; br_read_units(br_save); br_refresh(); return 1; }
    if (k >= PVB_BR_UNIT0 && k < PVB_BR_UNIT0 + 12) {
        int i = k - PVB_BR_UNIT0, npick = 0; for (int j = 0; j < br_nunits; j++) npick += br_pick[j];
        if (i < br_nunits && (br_pick[i] || npick < br_share())) br_pick[i] = !br_pick[i];
        br_sent[0] = 0; br_refresh(); return 1;
    }
    return 0;
}
/* CARD RULE (owner 2026-10-25: a click anywhere on a card or picker row toggles it): a left click anywhere on a unit's tile
   in the text Bring list (its portrait, its level, the space between) picks / unpicks it, as its name button does. Read
   like the HUD's Ready (Input.GetMouseButtonDown(0) on the tile's screen rect); a click on the name button is that
   button's own click (not counted twice); nothing while one of our boxes is over the window. */
static void br_tile_click(void) {
    if (!br_open || !u_on(pvt(h_br_root)) || mpesc_box_over() || !F(0x51ACDD0, bool (*)(int, void *))(0, NULL)) return;   /* Input.GetMouseButtonDown(0) */
    V3 m = F(0x51AD4DC, V3 (*)(void *))(NULL); float r[4];                                                                   /* Input.mousePosition */
    for (int i = 0; i < br_nunits && i < 12; i++) {
        Obj tile = pvt(h_br_tile[i]), b = pvb(PVB_BR_UNIT0 + i);
        if (!u_on(tile) || !rect_of(tile, r) || m.x < r[0] || m.x > r[2] || m.y < r[1] || m.y > r[3]) continue;
        if (u_on(b) && rect_of(b, r) && m.x >= r[0] && m.x <= r[2] && m.y >= r[1] && m.y <= r[3]) return;
        pv_click = PVB_BR_UNIT0 + i; rig_log("pvp ui: Bring list: click on unit %d's tile", i); return;
    }
}
static int pvp_ui_press(Obj btn) {                                      /* from the Esc-menu click hook */
    for (int k = 0; k < PVB_N; k++) if (btn && h_pvb[k] && btn == u_gc_get(h_pvb[k])) { pv_click = k; return 1; }
    return 0;
}

/* ---- HUD: the boss bar (BossHUDView: _bossNameText +0x30 DASText, _headHP +0x38 / _bodyHP +0x40 Images) */
static void hud_build(void) {
    static uintptr_t hb; static void *kb;
    Obj bv = find_view_x(&hb, &kb, "Refactor", "BossHUDView", true);
    if (!bv) bv = find_view_x(&hb, &kb, "", "BossHUDView", true);
    if (!bv) return;
    Obj go = u_go(bv), parent = u_tr_parent(u_tr(bv));
    if (!go || !parent) return;
    Obj cl = u_instantiate(go, parent);
    if (!u_alive(cl)) return;
    Obj tr = u_go_tr(cl);
    static void *kc; if (!kc) kc = class_by_name("Refactor", "BossHUDView");
    Obj ty = kc ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(kc)) : NULL;
    Obj comp = ty && tr ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(tr, ty, NULL) : NULL;
    if (!u_alive(comp)) { rig_log("pvp ui: boss bar copy has no BossHUDView"); return; }
    Obj name = AT(comp, 0x30, Obj), head = AT(comp, 0x38, Obj), body = AT(comp, 0x40, Obj);
    if (u_alive(name)) das_key_set(name, false);                         /* DASText._useTextKey off: our text stays */
    if (mm_is(comp, K_BEH)) F(0x50ECEA0, void (*)(Obj, bool, void *))(comp, false, NULL);   /* the copy never runs the boss code */
    Obj aff = AT(comp, 0x48, Obj); if (u_alive(aff)) u_active(aff, false);
    u_gc_free(h_hud); h_hud = u_gc_new(cl); u_gc_free(h_hud_name); h_hud_name = u_alive(name) ? u_gc_new(name) : 0;
    u_gc_free(h_hud_bar[0]); h_hud_bar[0] = u_alive(head) ? u_gc_new(head) : 0;
    u_gc_free(h_hud_bar[1]); h_hud_bar[1] = u_alive(body) ? u_gc_new(body) : 0;
    if (u_alive(name)) txt_style(name, 1, 22, 514, 0);                    /* one line, Cinzel */
    /* owner 2026-10-14: each bar in its team colour (the boss sprites are red / orange, a tint cannot make them blue): a
       plain rect in the team colour, pivot moved to its left edge (same place), filled by scaling x from that edge */
    bl_par = NULL;
    for (int k = 0; k < 2; k++) {
        Obj bar = k ? body : head, rt = u_alive(bar) ? u_tr(bar) : NULL; if (!mm_is(rt, K_RT)) continue;
        typedef struct { float x, y, w, h; } RectF;
        RectF r = F(0x5106978, RectF (*)(Obj, void *))(rt, NULL);
        V2 pv = F(0x5107200, V2 (*)(Obj, void *))(rt, NULL), ap = F(0x5106E38, V2 (*)(Obj, void *))(rt, NULL);
        F(0x51072F4, void (*)(Obj, V2, void *))(rt, (V2){ 0, pv.y }, NULL);
        /* the bar starts HUD_INSET further right (its fill scale is HUD_K = the rest of its width): the Blue / Red labels
           left of it stay inside the panel (they stuck out past its left edge) */
        ap.x -= r.w * pv.x; ap.x += HUD_INSET; F(0x5106F2C, void (*)(Obj, V2, void *))(rt, ap, NULL);
        if (r.w > HUD_INSET * 2) hud_k = (r.w - HUD_INSET) / r.w;
        if (k) { bl_x = ap.x - HUD_INSET; bl_w = r.w; bl_y = ap.y; bl_par = u_tr_parent(rt); }   /* the bottom line below */
        img_set(bar, NULL, k ? (Col){ 0.86f, 0.30f, 0.30f, 1 } : (Col){ 0.33f, 0.52f, 0.95f, 1 });
    }
    /* under the unit portraits; 140 to the right: the Players panel (ui_game.c, top left, right edge 222 left of the centre)
       covered the bar's left end and its Blue / Red labels */
    { Obj rt = tr; if (mm_is(rt, K_RT)) { V2 p = F(0x5106E38, V2 (*)(Obj, void *))(rt, NULL); p.y -= 215; p.x += 140; F(0x5106F2C, void (*)(Obj, V2, void *))(rt, p, NULL); } }
    /* the panel's sprite fades out at the bottom: a copper line closes it (the copy's root is a 100 x 100 holder, so it is
       placed from the bars: the dark panel reaches ~27 past each bar end and ~38 under the lower bar, measured on shots) */
    { Obj ln = bl_par && bl_w > 100 && u_alive(head) ? mk_img(head, bl_par, -1, C_COPPER, bl_x + bl_w / 2, bl_y - 38, bl_w + 54, 2) : NULL;
      u_gc_free(h_hud_line); h_hud_line = ln ? u_gc_new(ln) : 0; }
    Obj tpl_t = u_alive(name) ? name : NULL, labs[2] = { NULL, NULL }; float lab_pw = 0;
    for (int k = 0; k < 2 && tpl_t; k++) {                               /* "Blue" left of the top bar, "Red" left of the bottom one */
        Obj bar = k ? body : head; if (!u_alive(bar)) continue;
        Obj l = u_instantiate(tpl_t, u_tr_parent(u_tr(bar)));
        if (!u_alive(l)) continue;
        das_key_clear(l); txt_style(l, 1, 16, 516, 0); txt_color(l, k ? C_RED : C_BLUE); cmt_set(l, k ? TRC("team", "Red") : TRC("team", "Blue"));
        u_gc_free(h_hud_lab[k]); h_hud_lab[k] = u_gc_new(l); labs[k] = l;
        if (mm_is(l, K_TMP)) { float pw = F(0x4F0D81C, float (*)(Obj, void *))(l, NULL); if (pw > lab_pw) lab_pw = pw; }   /* TMP_Text.preferredWidth at 16 */
        Obj brt = u_tr(bar); V2 bp = mm_is(brt, K_RT) ? F(0x5106E38, V2 (*)(Obj, void *))(brt, NULL) : (V2){ 0, 0 };
        /* between the copy's end-cap and the bar (bp.x = the bar's left edge): 10 clear of the end-cap, 6 before the bar */
        place(l, bp.x - (HUD_INSET + 4) / 2, bp.y, HUD_INSET - 16, 24);
    }
    /* one size for both labels: the size at which the longer one fits ("Красные" was auto-sized to 13.5 next to "Синие"
       at 16); 68 wide holds "Красные" at 15, "Vermelho" at about 13 */
    if (lab_pw > HUD_INSET - 16) { float sz = 16 * (HUD_INSET - 16) / lab_pw; sz = (float)(int)(sz * 2) / 2; if (sz < 12) sz = 12;
                                   for (int k = 0; k < 2; k++) if (labs[k]) txt_style(labs[k], 1, sz, 516, 0); }
    /* Ready button (planning pause): built from the bar's own parts (the Esc menu may never have been opened here): the
       Recruit button sprite + a Cinzel label; the click is read with Input.GetMouseButtonDown in hud_frame */
    if (u_alive(head) && u_alive(name)) {
        Obj hrt = u_tr(body ? body : head); V2 hp = mm_is(hrt, K_RT) ? F(0x5106E38, V2 (*)(Obj, void *))(hrt, NULL) : (V2){ 0, 0 };
        if (mm_is(hrt, K_RT)) { typedef struct { float x, y, w, h; } RectB; RectB rb = F(0x5106978, RectB (*)(Obj, void *))(hrt, NULL); hp.x += (rb.w - HUD_INSET) / 2; }   /* the bar's pivot is its left edge (above): centre under it */
        Obj b = mk_img(head, u_tr_parent(hrt), S_RECRUIT, C_WHITE, hp.x, hp.y - 44, 170, 38);   /* under the bars, centred */
        if (b) { Obj l = mk_txt(name, u_tr(b), 1, 18, 514, 0, 0, 0, 170, 38, C_START);
                 if (l) { das_key_clear(l); cmt_set(l, TR("Ready")); u_gc_free(h_hud_rlab); h_hud_rlab = u_gc_new(l); } }
        u_gc_free(h_pvb[PVB_READY]); h_pvb[PVB_READY] = b ? u_gc_new(b) : 0; u_active(b, false);
        /* E4: Start fight (host) / Propose fight (guest) in the preparation: same place as Ready (never both shown) */
        Obj fb = mk_img(head, u_tr_parent(hrt), S_RECRUIT, C_WHITE, hp.x, hp.y - 44, 220, 38);
        if (fb) { Obj l = mk_txt(name, u_tr(fb), 1, 18, 514, 0, 0, 0, 220, 38, C_START);
                  if (l) { das_key_clear(l); u_gc_free(h_hud_flab); h_hud_flab = u_gc_new(l); } }
        u_gc_free(h_pvb[PVB_FIGHT]); h_pvb[PVB_FIGHT] = fb ? u_gc_new(fb) : 0; u_active(fb, false);
        /* E2: Bring Units in the preparation, under it */
        Obj hb = mk_img(head, u_tr_parent(hrt), S_NBTN, C_WHITE, hp.x, hp.y - 88, 220, 34);
        if (hb) { Obj l = mk_txt(name, u_tr(hb), 1, 16, 514, 0, 0, 0, 220, 34, C_MAIN); if (l) { das_key_clear(l); cmt_set(l, TR("Bring Units...")); u_gc_free(h_lab_bring); h_lab_bring = u_gc_new(l); } }
        u_gc_free(h_pvb[PVB_HUD_BRING]); h_pvb[PVB_HUD_BRING] = hb ? u_gc_new(hb) : 0; u_active(hb, false);
        /* E4: the host's box when a guest proposes the fight: "<name> proposes a fight" + Start / Not yet */
        if (!scan_done) assets_scan_now();
        prop_x = hp.x - HUD_INSET / 2; prop_y = hp.y - 120;              /* centred on the panel (hp.x is the centre of the bars, right of the labels) */
        Obj pb = mk_img(head, u_tr_parent(hrt), S_BOX, C_WHITE, prop_x, prop_y, PROP_W, 104);
        if (pb) {
            Obj t = mk_txt(name, u_tr(pb), 0, 18, 514, 1, 0, 22, PROP_W - 30, 40, C_MAIN);
            if (t) { das_key_clear(t); u_gc_free(h_prop_txt); h_prop_txt = u_gc_new(t); }
            for (int k = 0; k < 2; k++) {
                Obj bb = mk_img(head, u_tr(pb), k ? S_NBTN : S_RECRUIT, C_WHITE, k ? 100 : -100, -24, 170, 36);
                Obj bl = bb ? mk_txt(name, u_tr(bb), 1, 17, 514, 0, 0, 0, 170, 36, k ? C_MAIN : C_START) : NULL;
                if (bl) { das_key_clear(bl); cmt_set(bl, k ? TR("Not Yet") : TR("Agree")); uintptr_t *hl = k ? &h_lab_no : &h_lab_yes; u_gc_free(*hl); *hl = u_gc_new(bl); }
                u_gc_free(h_pvb[k ? PVB_PROP_NO : PVB_PROP_YES]); h_pvb[k ? PVB_PROP_NO : PVB_PROP_YES] = bb ? u_gc_new(bb) : 0;
            }
        }
        u_gc_free(h_prop_root); h_prop_root = pb ? u_gc_new(pb) : 0; u_active(pb, false);
        /* E9: the round result screen, under the bars: "Blue wins round 1", the score, "Next round in 4" */
        Obj rs = mk_img(head, u_tr_parent(hrt), S_BOX, C_WHITE, hp.x, hp.y - 170, 600, 190);
        if (rs) {
            Obj t1 = mk_txt(name, u_tr(rs), 1, 34, 514, 0, 0, 52, 560, 52, C_MAIN), t2 = mk_txt(name, u_tr(rs), 1, 30, 514, 0, 0, -4, 560, 44, C_GOLD),
                t3 = mk_txt(name, u_tr(rs), 0, 18, 514, 0, 0, -56, 560, 30, C_SUB);
            Obj tt[3] = { t1, t2, t3 }; uintptr_t *hh[3] = { &h_rs_title, &h_rs_score, &h_rs_sub };
            for (int k = 0; k < 3; k++) if (tt[k]) { das_key_clear(tt[k]); u_gc_free(*hh[k]); *hh[k] = u_gc_new(tt[k]); }
        }
        u_gc_free(h_rs_root); h_rs_root = rs ? u_gc_new(rs) : 0; u_active(rs, false);
        /* P2: the message line: a dark plate as wide as the panel, the body font; placed under whatever shows (hud_msg_place) */
        msg_x = prop_x; msg_y0 = hp.y; msg_w = bl_w > 100 ? bl_w + 54 : 520;
        Obj mb = mk_img(head, u_tr_parent(hrt), -1, (Col){ 0.102f, 0.094f, 0.114f, 0.9f }, msg_x, hp.y - 90, msg_w, 40);
        if (mb) {
            F(0x5260348, void (*)(Obj, bool, void *))(mb, false, NULL);  /* raycastTarget off: clicks go to the world */
            Obj t = mk_txt(name, u_tr(mb), 0, 17, 514, 1, 0, 0, msg_w - 24, 36, C_MAIN);
            if (t) { das_key_clear(t); u_gc_free(h_msg_txt); h_msg_txt = u_gc_new(t); }
        }
        u_gc_free(h_msg); h_msg = mb ? u_gc_new(mb) : 0; u_active(mb, false); msg_until = 0; msg_now[0] = 0;
    }
    u_go_active(cl, false);
    rig_log("pvp ui: boss bar copy built (name %p, bars %p %p)", name, head, body);
}
static double mono_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
/* P2: the line shows for 4 s (the planning one while the pause lasts), 6 units under the lowest thing shown under the bars
   (the panel's bottom line, Ready / Propose fight, Bring Units, the fight vote box, the round result screen) */
static void hud_msg_place(const pvp_state *s) {
    Obj mb = u_gc_get(h_msg), mt = u_gc_get(h_msg_txt);
    if (!u_alive(mb) || !u_alive(mt)) return;
    int on = msg_now[0] && (mono_now() < msg_until || (msg_hold && s && s->planning));
    if (!on) { if (u_on(mb)) u_active(mb, false); return; }
    float low = msg_y0 - 38;                                              /* the panel's bottom line (hud_build) */
    if (u_on(pvb(PVB_READY)) || u_on(pvb(PVB_FIGHT))) low = msg_y0 - 63;
    if (u_on(pvb(PVB_HUD_BRING))) low = msg_y0 - 105;
    Obj pr = u_gc_get(h_prop_root); if (u_alive(pr) && u_on(pr)) { int vv = vote_view(s); float b = vv == 2 ? prop_y - 52 : prop_y; if (b < low) low = b; }
    Obj rs = u_gc_get(h_rs_root); if (u_alive(rs) && u_on(rs) && msg_y0 - 265 < low) low = msg_y0 - 265;
    cmt_set(mt, msg_now);
    float pw = mm_is(mt, K_TMP) ? F(0x4F0D81C, float (*)(Obj, void *))(mt, NULL) : 0;   /* TMP_Text.preferredWidth: lines needed */
    int lines = pw > msg_w - 24 ? (int)(pw / (msg_w - 24)) + 1 : 1; if (lines > 3) lines = 3;
    float h = 14 + 22 * lines;
    place(mb, msg_x, low - 6 - h / 2, msg_w, h); place(mt, 0, 0, msg_w - 24, h - 8);
    if (!u_on(mb)) u_active(mb, true);
}
/* pvp_game.c center(): 1 = shown under the score panel (the match HUD is up), 0 = the caller uses the game's notice.
   hold: stays while the planning pause lasts. */
int pvp_ui_hud_say(const char *text, int hold) {
    Obj cl = u_gc_get(h_hud);
    if (!text || !u_alive(cl) || !u_go_active_self(cl) || !u_alive(u_gc_get(h_msg))) return 0;
    snprintf(msg_now, sizeof msg_now, "%s", text); dsi_utf8_fix_end(msg_now);
    msg_until = mono_now() + 4; msg_hold = hold;
    hud_msg_place(pvp_ui_state ? pvp_ui_state() : NULL);
    return 1;
}
static void hud_frame(const pvp_state *s) {
    int on = s && pvp_active(s) && scene() == 2;
    Obj cl = u_gc_get(h_hud);
    if (on && !u_alive(cl)) { if (frames % 60 == 0) hud_build(); cl = u_gc_get(h_hud); }
    if (!u_alive(cl)) return;
    if (u_go_active_self(cl) != on) u_go_active(cl, on);
    if (!on || frames % 6) return;
    uint32_t pm[3] = { 0, 1000, 1000 }; if (pvp_ui_team_hp) pvp_ui_team_hp(pm);
    char t[400], clock[64] = "";
    if (s->placing && s->countdown_at && s->cd_ticks == PVP_CD_TAKING_PLACES) snprintf(clock, sizeof clock, "%s", TR("Taking places..."));   /* F6 */
    else if (s->placing && s->countdown_at) {                            /* E8: the countdown to the fight, in seconds */
        int len = s->cd_ticks ? s->cd_ticks : PVP_COUNTDOWN_TICKS, left = (len - (int)(s->ticks - (s->countdown_at - 1)) + 59) / 60; if (left < 1) left = 1;
        snprintf(clock, sizeof clock, TR("Fight in %d"), left);
    } else if (s->placing && s->vote_open) snprintf(clock, sizeof clock, "%s", TR("Fight proposed"));
    else if (s->placing && s->round <= 1) snprintf(clock, sizeof clock, "%s", TR("Preparation"));
    else if (s->placing) snprintf(clock, sizeof clock, "%s", TR("Taking places..."));   /* later rounds: F6, at most 20 s */
    else if (s->fight_start) { int left = (int)s->rules.round_secs - (int)((s->ticks - (s->fight_start - 1)) / 60); if (left < 0) left = 0; snprintf(clock, sizeof clock, "%d:%02d", left / 60, left % 60); }
    if (s->planning) snprintf(clock, sizeof clock, "%s", TR("Planning"));
    dsi_utf8_fix_end(clock);
    char rd[64]; snprintf(rd, sizeof rd, TR("Round %d"), s->round);
    snprintf(t, sizeof t, "<color=#7FA8FF>%s</color> %d - %d <color=#FF7F7F>%s</color>   %s   %s", TRC("team", "Blue"), s->score[1], s->score[2], TRC("team", "Red"), rd, clock);
    Obj nm = u_gc_get(h_hud_name); if (u_alive(nm)) cmt_set(nm, t);
    { size_t k = 0; for (const char *q = t; *q && k < sizeof hud_last - 1; q++) hud_last[k++] = *q == '"' || *q == '\\' ? '\'' : *q; hud_last[k] = 0; }   /* tests */
    for (int k = 0; k < 2; k++) { Obj l = u_gc_get(h_hud_lab[k]); if (u_alive(l)) cmt_set(l, k ? TRC("team", "Red") : TRC("team", "Blue")); }
    { Obj l = u_gc_get(h_hud_rlab); if (u_alive(l)) cmt_set(l, TR("Ready")); }   /* (DASText re-reads its key on enable) */
    for (int k = 0; k < 2; k++) { Obj im = u_gc_get(h_hud_bar[k]); Obj rt = u_alive(im) ? u_tr(im) : NULL;
        if (mm_is(rt, K_TR)) F(0x510AFF8, void (*)(Obj, V3, void *))(rt, (V3){ pm[k + 1] / 1000.0f * hud_k, 1, 1 }, NULL); }   /* Transform.localScale */
    Obj rb = pvb(PVB_READY); if (rb) u_active(rb, s->planning != 0);
    /* E4: Propose fight / Cancel (agreed countdown); the vote box: "<name> proposes a fight" Agree / Not yet, or waiting */
    int vv = vote_view(s);
    Obj fb = pvb(PVB_FIGHT); if (fb) u_active(fb, (vv == 1 || vv == 4) && !s->planning);
    Obj hbb = pvb(PVB_HUD_BRING); if (hbb) u_active(hbb, vv == 1 && !br_open && !mu_bring_is_open());   /* E2: in the preparation of a match */
    { Obj l = u_gc_get(h_hud_flab); if (u_alive(l)) cmt_set(l, vv == 4 ? TR("Cancel") : TR("Propose Fight")); }
    { uintptr_t lh[3] = { h_lab_yes, h_lab_no, h_lab_bring }; const char *lt[3] = { TR("Agree"), TR("Not Yet"), TR("Bring Units...") };
      for (int k = 0; k < 3; k++) { Obj l = u_gc_get(lh[k]); if (u_alive(l)) cmt_set(l, lt[k]); } }   /* (DASText re-reads its key on enable: set every refresh, like Ready) */
    Obj pr = u_gc_get(h_prop_root); if (u_alive(pr)) u_active(pr, vv == 2 || vv == 3);
    for (int k = 0; k < 2; k++) { Obj bb = pvb(k ? PVB_PROP_NO : PVB_PROP_YES); if (bb) u_active(bb, vv == 2); }
    if (vv == 2 || vv == 3) {
        /* waiting (no buttons): the box shrinks to its text line, no empty band where the buttons were */
        if (u_alive(pr)) place(pr, prop_x, vv == 2 ? prop_y : prop_y + 26, PROP_W, vv == 2 ? 104 : 52);
        { Obj l = u_gc_get(h_prop_txt); if (u_alive(l)) place(l, 0, vv == 2 ? 22 : 0, PROP_W - 30, 40); }
        char pn[64] = "", t2[200]; if (pvp_ui_player_name) pvp_ui_player_name(s->vote_by, pn, sizeof pn);
        if (strlen(pn) > 24) { pn[24] = 0; dsi_utf8_fix_end(pn); strcat(pn, "\xE2\x80\xA6"); }   /* a long name: cut, the box keeps one line */
        if (vv == 2) snprintf(t2, sizeof t2, TR("%s proposes a fight"), pn); else snprintf(t2, sizeof t2, "%s", TR("Waiting for the other players to agree"));
        dsi_utf8_fix_end(t2); Obj l = u_gc_get(h_prop_txt); if (u_alive(l)) cmt_set(l, t2);
    }
    /* E9: the round result screen while the ended round waits (the world stands still); the host's NEXT ends it */
    Obj rs = u_gc_get(h_rs_root); int ending = s->ending && !s->resetting;
    if (u_alive(rs)) u_active(rs, ending);
    if (ending) {
        uint32_t key = s->matches * 64 + s->round;
        if (key != rs_key) { rs_key = key; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); rs_t0 = ts.tv_sec + ts.tv_nsec / 1e9; rig_log("pvp ui: round result screen (round %d, winner %d, score %d-%d, %s)", s->round, s->last_winner, s->score[1], s->score[2], s->ending == PVP_RESET_MATCH ? "match over" : "next round"); }
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); int left = 5 - (int)(ts.tv_sec + ts.tv_nsec / 1e9 - rs_t0); if (left < 1) left = 1;
        char a[160], b2[160], c2[160];
        if (s->ending == PVP_RESET_MATCH) snprintf(a, sizeof a, "%s", s->match_winner == PVP_TEAM_BLUE ? TR("Blue wins the match") : s->match_winner == PVP_TEAM_RED ? TR("Red wins the match") : TR("The match is a draw"));
        else if (s->last_winner == PVP_TEAM_BLUE) snprintf(a, sizeof a, TR("Blue wins round %d"), s->round);
        else if (s->last_winner == PVP_TEAM_RED) snprintf(a, sizeof a, TR("Red wins round %d"), s->round);
        else snprintf(a, sizeof a, TR("Round %d is a draw"), s->round);
        snprintf(b2, sizeof b2, "<color=#7FA8FF>%s</color>  %d - %d  <color=#FF7F7F>%s</color>", TRC("team", "Blue"), s->score[1], s->score[2], TRC("team", "Red"));
        if (s->ending == PVP_RESET_MATCH) snprintf(c2, sizeof c2, TR("Results in %d"), left); else snprintf(c2, sizeof c2, TR("Next round in %d"), left);
        dsi_utf8_fix_end(a); dsi_utf8_fix_end(c2);
        Obj t; if (u_alive(t = u_gc_get(h_rs_title))) cmt_set(t, a); if (u_alive(t = u_gc_get(h_rs_score))) cmt_set(t, b2); if (u_alive(t = u_gc_get(h_rs_sub))) cmt_set(t, c2);
    }
    hud_msg_place(s);                                                    /* P2: under what shows now */
}
static void hud_click(const pvp_state *s) {                             /* every frame: a left click on Ready */
    if (s && F(0x51ACDD0, bool (*)(int, void *))(0, NULL)) {              /* E4: Start / Propose fight, the proposal box's buttons */
        V3 m = F(0x51AD4DC, V3 (*)(void *))(NULL); float r[4];
        static const int ks[4] = { PVB_FIGHT, PVB_PROP_YES, PVB_PROP_NO, PVB_HUD_BRING };
        for (int i = 0; i < 4; i++) { Obj b = pvb(ks[i]); if (u_on(b) && rect_of(b, r) && m.x >= r[0] && m.x <= r[2] && m.y >= r[1] && m.y <= r[3]) { pv_click = ks[i]; return; } }
    }
    Obj rb = pvb(PVB_READY);
    if (!s || !s->planning || !u_on(rb) || !F(0x51ACDD0, bool (*)(int, void *))(0, NULL)) return;   /* Input.GetMouseButtonDown(0) */
    float r[4]; V3 m = F(0x51AD4DC, V3 (*)(void *))(NULL);                                         /* Input.mousePosition */
    if (rect_of(rb, r) && m.x >= r[0] && m.x <= r[2] && m.y >= r[1] && m.y <= r[3]) pv_click = PVB_READY;
}

/* ---- result window (after the match, back in the world): on the Esc menu's root, like the PvP window */
static void pr_build(Obj v) {
    Obj src = AT(v, 0x70, Obj); if (!u_alive(src)) return;
    Obj tpl_img = first_image(u_tr(src), 0), tpl_txt = AT(src, 0x28, Obj);
    const float W = PR_W, H = 360;
    Obj root = mk_img(tpl_img, u_tr(v), -1, (Col){ 0, 0, 0, 0 }, 0, 0, W, H); if (!root) return;
    Obj R = u_tr(root);
    u_gc_free(h_pr_root); h_pr_root = u_gc_new(root); pr_k = 1;
    mk_img(tpl_img, R, S_BOX, C_WHITE, 0, 0, W, H);
    mk_img(tpl_img, R, -1, C_BAND, 0, H / 2 - 32, W - 4, 60);
    mk_img(tpl_img, R, -1, C_COPPER, 0, H / 2 - 62, W - 4, 1);
    Obj t = mk_txt(tpl_txt, R, 1, 30, 514, 0, 0, H / 2 - 32, 440, 50, C_MAIN); u_gc_free(h_pr_title); h_pr_title = t ? u_gc_new(t) : 0;   /* 440: clear of the red X */
    t = mk_txt(tpl_txt, R, 1, 40, 514, 0, 0, H / 2 - 110, 520, 60, C_GOLD); u_gc_free(h_pr_score); h_pr_score = t ? u_gc_new(t) : 0;
    t = mk_txt(tpl_txt, R, 0, 16, 514, 1, 0, H / 2 - 190, 500, 80, C_SUB); u_gc_free(h_pr_rounds); h_pr_rounds = t ? u_gc_new(t) : 0;
    Obj b = mk_button(src, R, 1, S_RECRUIT, C_WHITE, 0, -H / 2 + 40, 260, 42, 18, TR("Back to the World"), C_START);
    u_gc_free(h_pvb[PVB_RESULT_OK]); h_pvb[PVB_RESULT_OK] = b ? u_gc_new(b) : 0;
    u_active(root, false);
    dsl_movable(mu_kit(), root, "multiplayer.pvp_result");                 /* MOVE RULE: as the PvP window */
}
static int pr_show(const pvp_state *s);
static void pr_frame(const pvp_state *s) {
    if (!s) return;
    /* round history (view only): one entry per round end */
    if (s->matches != pr_hist_match) { pr_hist_match = s->matches; pr_nhist = 0; pr_hist_round = 0; }
    if (pvp_active(s) && s->resetting && s->round != pr_hist_round && pr_nhist < 16) { pr_hist_round = s->round; pr_hist[pr_nhist++] = s->last_winner; }
    static int steady;
    int ready_now = s->phase == PVP_OFF && s->matches && s->matches != pr_shown_match && scene() == 2 && !busy() && rig_det_mp_ready();
    steady = ready_now ? steady + 1 : 0;
    if (steady < 120) return;                                           /* the world is back and running for 4 s */
    if (pr_show(s)) pr_shown_match = s->matches;
}
/* the result window over the Esc menu (opened first); 1 = shown. Tests: mpm_pvp {action:"result_show"} (no match needed) */
static int pr_show(const pvp_state *s) {
    Obj v = esc_view();
    if (!v) { popup_activate(1); return 0; }
    if (!u_on(v)) { popup_activate(1); return 0; }
    if (!u_alive(u_gc_get(h_pr_root)) || u_gc_get(h_pv_view) != v) { pv_build(v); pr_build(v); }
    Obj root = u_gc_get(h_pr_root); if (!u_alive(root)) return 0;
    char b[300];
    snprintf(b, sizeof b, "%s", s->match_winner == PVP_TEAM_BLUE ? TR("Blue wins the match") : s->match_winner == PVP_TEAM_RED ? TR("Red wins the match") : TR("The match is a draw"));
    cmt_set(u_gc_get(h_pr_title), b);
    snprintf(b, sizeof b, "<color=#7FA8FF>%d</color>  -  <color=#FF7F7F>%d</color>", s->score[1], s->score[2]); cmt_set(u_gc_get(h_pr_score), b);
    char r[900] = ""; size_t o = 0;
    o += (size_t)snprintf(r + o, sizeof r - o, "%s ", TR("Rounds:"));
    for (int i = 0; i < pr_nhist && o < sizeof r - 80; i++) o += (size_t)snprintf(r + o, sizeof r - o, "%s%d %s", i ? ",  " : "", i + 1, pr_hist[i] == 1 ? TRC("team", "Blue") : pr_hist[i] == 2 ? TRC("team", "Red") : TR("draw"));
    if (s->draws && o < sizeof r) { o += (size_t)snprintf(r + o, sizeof r - o, "\n"); if (o < sizeof r) o += (size_t)snprintf(r + o, sizeof r - o, TR("Draws: %d"), s->draws); }
    if (o < sizeof r) snprintf(r + o, sizeof r - o, "\n%s", pv_here() ? TR("The hub is back as it was before the match.") : TR("The world is back as it was before the match."));
    { Obj ok = pvb(PVB_RESULT_OK); if (ok) cmt_set(btn_label(ok, 1), pv_here() ? TR("Back to the Hub") : TR("Back to the World")); }
    cmt_set(u_gc_get(h_pr_rounds), r);
    Obj area = u_gc_get(h_pv_area); if (u_alive(area)) u_active(area, false);
    u_active(root, true); pr_open = 1; win_fit(root, &pr_k, "result window");   /* M5 */
    if (pv_kit_x()) mpesc_close_x(&esc_pr, root, PV_XX(PR_W), PV_XY, PV_XS, root);   /* CLOSE RULE: the red X = Back to the World */
    rig_log("pvp ui: result window: %s", b);
    return 1;
}

/* ---- every frame (from mpmenu_frame) */
static void pvp_ui_tips(void) {
    if (!pv_open && !pr_open) {
        tip_try(pvb(PVB_ESC), TR("PvP Match"), TR("Opens the PvP match setup, where two teams of players fight each other. After the match, the world is back as it was."));
        tip_try(pvb(PVB_READY), TR("Ready"), TR("You have given your orders. The planning pause ends when every player pressed Ready, or after 5 seconds."));
        return;
    }
    if (pv_open) {
        for (int r = 0; r < PVR_N; r++) tip_try(pvt(h_pv_rrow[r]), pv_lab(r), pv_tipf(r));
        tip_try(pvt(h_pv_prow), TR("Preset"), TR("Your saved sets of rules. The arrows pick one and apply its rules. Custom: the rules shown are not a saved preset."));
        tip_try(pvb(PVB_PRE_SAVEAS), TR("Save as Preset..."), TR("Saves all the rules shown under a name you type, on this computer only. The same name replaces that preset."));
        tip_try(pvb(PVB_PRE_DEL), TR("Delete Preset"), TR("Deletes the preset shown. The rules stay as they are."));
        if (pv_here()) tip_try(pvb(PVB_START), TR("Start Match"), TR("Starts the match here with the teams and rules shown: each team walks to its end of the settlement, then the fight begins."));
        else tip_try(pvb(PVB_START), TR("Start Match"), TR("Sends everyone to the arena floor with the teams and rules shown. The match begins by itself once everyone has arrived."));
        tip_try(pvb(PVB_CLOSE), cmt_game("TEXTKEY_Common_Close", "Close"), TR("Closes this window. Nothing is sent."));
        tip_try(pvb(PVB_BRING), TR("Bring Units"), TR("Fight with units from one of your own saves instead of this world's units. They are copies: your save stays as it is."));
        for (int i = 0; i < 8; i++) tip_try(pvb(PVB_P0 + i), TR("Team"), TR("The host clicks a player to move them to the other team. Each player brings the units they own."));
        tip_try(pvt(h_pv_teamh[1]), TR("Blue Team"), pv_here() ? TR("Starts at one end of the settlement.") : TR("Starts at the arrival point of the arena."));
        tip_try(pvt(h_pv_teamh[2]), TR("Red Team"), pv_here() ? TR("Starts at the other end of the settlement.") : TR("Starts at the far end of the arena, by the exit portal (which does not work in a match)."));
    }
    if (br_open) {
        for (int i = 0; i < br_nsaves; i++) { char tb[600]; snprintf(tb, sizeof tb, TR("%s\n\nA save on this computer, newest first. Click it to see its units."), br_stems[i]); dsi_utf8_fix_end(tb); tip_try(pvb(PVB_BR_SAVE0 + i), TR("Save"), tb); }
        for (int i = 0; i < 12; i++) tip_try(pvb(PVB_BR_UNIT0 + i), TR("Unit"), TR("Click to pick or unpick this unit. The dungeon team is 4 units in total, shared between the players."));
        tip_try(pvb(PVB_BR_SEND), TR("Bring"), TR("Copies the picked units with their gear and sends them to the host. A new Bring replaces your last one."));
        tip_try(pvb(PVB_BR_BACK), cmt_game("TEXTKEY_Shared_Back", "Back"), TR("Back to the PvP window."));
    }
    if (pr_open && pv_here()) tip_try(pvb(PVB_RESULT_OK), TR("Back to the Hub"), TR("Closes the result. The hub is the same as before the match: go on an expedition or start another match."));
    else if (pr_open) tip_try(pvb(PVB_RESULT_OK), TR("Back to the World"), TR("Closes the result. The world is the same as before the match."));
}
static void pvp_ui_frame(const mpm_obs *o) {
    const pvp_state *s = pvp_ui_state ? pvp_ui_state() : NULL;
    if (!s) return;
    hud_frame(s);
    hud_click(s);
    pr_frame(s);
    pv_host_sync(s);                                                     /* P1: the guests see the host's window */
    Obj v = o->scene == 2 ? esc_view() : NULL;
    int ingame = v && u_on(v) && mp_game_session() && M.state == MPM_PLAYING;
    if (v && u_on(v)) {
        if (u_gc_get(h_pv_view) != v || !u_alive(u_gc_get(h_pv_root))) { pv_build(v); pr_build(v); br_build(v); }
        /* the Esc entry: a Report bug clone above Quit, in a multiplayer game with no match set up */
        Obj eb0 = pvb(PVB_ESC);
        if (!eb0 && eb(MPB_LEAVE)) {
            Obj src = AT(v, 0x70, Obj);
            Obj b = u_instantiate(src, u_tr_parent(u_tr(src)));
            if (u_alive(b)) { if (mm_is(b, K_SMB)) F(0xF2DB60, void (*)(Obj, Obj, void *))(b, NULL, NULL); AT(b, 0x30, Obj) = NULL;
                              u_set_sibling(u_tr(b), u_sibling(u_tr(eb(MPB_LEAVE)))); h_pvb[PVB_ESC] = u_gc_new(b); eb0 = b; }
        }
        if (eb0) { int show = ingame && s->phase == PVP_OFF && !pv_open && !pr_open; u_active(eb0, show); if (show) cmt_set(AT(eb0, 0x28, Obj), TR("PvP Match")); }
        if (pv_open && frames % 15 == 0) pv_refresh();
        if (frames % 15 == 0) { if (br_open) win_fit(pvt(h_br_root), &br_k, "Bring Units window"); if (pr_open) win_fit(pvt(h_pr_root), &pr_k, "result window"); }   /* M5 */
    } else { pv_open = 0; pr_open = 0; br_open = 0; br_lobby = 0; }
    br_tile_click();                                                     /* CARD RULE: the text Bring list's unit tiles */
    if (pv_click >= 0) { int k = pv_click; pv_click = -1; pv_click_do(k); }
    /* Escape (owner 2026-10-25): these windows are on the loader kit's Escape stack while they show (mp_esc.h): one press
       = the top window's own Close / Back / "Back to the World" (as a click), and the game does not see it (the Esc menu
       under them stays). Without that kit: the old check (the game also closes the whole Esc menu on that press). */
    Obj pvr = pvt(h_pv_root), brr = pvt(h_br_root), prr = pvt(h_pr_root);
    int kpv = mpesc_sync(&esc_pv, pvr, pv_open && u_on(pvr), 0), kbr = mpesc_sync(&esc_br, brr, br_open && u_on(brr), 0);
    mpesc_sync(&esc_pr, prr, pr_open && u_on(prr), 0);
    /* old path: Escape over the Bring Units card window closes only that window (its own key check, between frames: this frame or the last) */
    static int mb_prev; int mb = mu_bring_is_open(), esc = o->escape && !mb && !mb_prev; mb_prev = mb;
    if (esc && br_open && !kbr) br_show(0); else if (esc && pv_open && !kpv) pv_show(0);
    Obj lw = u_gc_get(h_lw_root);                                       /* the lobby steps aside while Bring Units is open over it */
    if (br_open && br_lobby && u_alive(lw)) u_active(lw, false);
}
/* ---- layout probe (tests, UX review 2026-10-25): screen rects in pixels (x0, y0, x1, y1; y up) of our windows and HUD
   parts, the game's UI scale, the Teams button, and how much of a given rect lies on the Players panel. */
int ui_teams_button_rect(float *x, float *y, float *w, float *h) __attribute__((weak_import));   /* ui_teams.c */
int ui_panel_hit(float x, float y) __attribute__((weak_import));                                  /* ui_game.c */
#include "cm_il.h"
const DSLUi *mu_kit(void);
float mu_ui_scale(void);
static float pv_ui_scale(void) { return mu_kit() ? mu_ui_scale() : 1.0f; }   /* the game's UI scale (1 without the loader's kit) */
static void probe_rect(Buf *b, const char *k, Obj o) {
    float r[4]; bput(b, ",\"%s\":", k);
    if (u_alive(o) && u_on(o) && rect_of(o, r)) bput(b, "[%.1f,%.1f,%.1f,%.1f]", r[0], r[1], r[2], r[3]); else bput(b, "null");
}
static void rect_union(float u[4], Obj o, int *any) {
    float r[4]; if (!u_alive(o) || !u_on(o) || !rect_of(o, r)) return;
    if (!*any) { memcpy(u, r, sizeof r); *any = 1; return; }
    if (r[0] < u[0]) u[0] = r[0]; if (r[1] < u[1]) u[1] = r[1]; if (r[2] > u[2]) u[2] = r[2]; if (r[3] > u[3]) u[3] = r[3];
}
static char *pv_probe(const char *line) {
    Buf b = { 0 };
    bput(&b, "{\"ok\":true,\"screen\":[%d,%d],\"ui_scale\":%.3f,\"pv_k\":%.3f", F(0x5092C94, int (*)(void *))(NULL), F(0x5092CD4, int (*)(void *))(NULL), pv_ui_scale(), pv_k);
    probe_rect(&b, "window", pvt(h_pv_root)); probe_rect(&b, "close", pvb(PVB_CLOSE)); probe_rect(&b, "start", pvb(PVB_START));
    probe_rect(&b, "win_right", pvb(PVB_ARROW0 + 2 * PVR_WIN + 1));
    probe_rect(&b, "bring", pvt(h_br_root)); probe_rect(&b, "bring_back", pvb(PVB_BR_BACK));
    probe_rect(&b, "br_tile0", pvt(h_br_tile[0])); probe_rect(&b, "br_por0", pvt(h_br_por[0])); probe_rect(&b, "br_btn0", pvb(PVB_BR_UNIT0));   /* CARD RULE tests */
    probe_rect(&b, "result", pvt(h_pr_root)); probe_rect(&b, "result_ok", pvb(PVB_RESULT_OK));
    probe_rect(&b, "ready", pvb(PVB_READY));
    probe_rect(&b, "msg", u_gc_get(h_msg)); bput(&b, ",\"msg_text\":"); bstr(&b, u_on(u_gc_get(h_msg)) ? msg_now : "");   /* P2 */
    { float u[4] = { 0 }; int any = 0; Obj cl = u_gc_get(h_hud);           /* the score panel: its title line, the bars, its bottom line */
      if (u_alive(cl) && u_go_active_self(cl)) { rect_union(u, u_gc_get(h_hud_name), &any); rect_union(u, u_gc_get(h_hud_bar[0]), &any); rect_union(u, u_gc_get(h_hud_bar[1]), &any); rect_union(u, u_gc_get(h_hud_line), &any); }
      if (any) bput(&b, ",\"score_panel\":[%.1f,%.1f,%.1f,%.1f]", u[0], u[1], u[2], u[3]); else bput(&b, ",\"score_panel\":null"); }
    { float x, y, w, h; if (ui_teams_button_rect && ui_teams_button_rect(&x, &y, &w, &h)) bput(&b, ",\"teams_btn\":[%.1f,%.1f,%.1f,%.1f]", x, y, x + w, y + h); else bput(&b, ",\"teams_btn\":null"); }
    { static uintptr_t h_hn; static void *k_hn; Obj hv = find_view_x(&h_hn, &k_hn, "Refactor", "HelpNoticeView", true);   /* the game's centred notice */
      static void *kt; if (!kt) kt = class_by_name("TMPro", "TMP_Text");
      Obj ty = kt ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(kt)) : NULL;
      Obj arr = hv && ty && u_on(hv) ? F(0x50EDFD0, Obj (*)(Obj, Obj, void *))(hv, ty, NULL) : NULL;   /* GetComponentsInChildren(Type): active ones */
      int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0; bput(&b, ",\"stock_notice\":[");
      for (int i = 0, k = 0; i < n && k < 4; i++) {
          Obj t = AT((char *)arr + 0x20, i * 8, Obj); float r[4]; if (!u_alive(t) || !mm_is(t, K_TMP) || !rect_of(t, r)) continue;
          void *vk = *(void **)t; Obj so = (*(Obj (**)(Obj, void *))((char *)vk + 0x548))(t, *(void **)((char *)vk + 0x550));   /* TMP_Text.text (slot 65) */
          char tx[400] = ""; str_utf8(so, tx, sizeof tx);
          bput(&b, "%s{\"text\":", k++ ? "," : ""); bstr(&b, tx); bput(&b, ",\"rect\":[%.1f,%.1f,%.1f,%.1f]}", r[0], r[1], r[2], r[3]);
      }
      bput(&b, "]"); }
    double q[4] = { -1, -1, -1, -1 }; jnum(line, "x0", &q[0]); jnum(line, "y0", &q[1]); jnum(line, "x1", &q[2]); jnum(line, "y1", &q[3]);
    if (q[2] > q[0] && q[3] > q[1]) {                                   /* points of that rect on the Players panel (16 x 6 grid) */
        int hits = 0, n = 0;
        for (int i = 0; i < 16; i++) for (int j = 0; j < 6; j++, n++) if (ui_panel_hit && ui_panel_hit((float)(q[0] + (q[2] - q[0]) * (i + 0.5) / 16), (float)(q[1] + (q[3] - q[1]) * (j + 0.5) / 6))) hits++;
        bput(&b, ",\"panel_hits\":%d,\"panel_points\":%d", hits, n);
    }
    bput(&b, "}");
    return b.p;
}
static int pv_set_ui_scale(float v) {                                   /* tests: Settings > UI scale, as the game sets it */
    if (!mu_kit()) return -1;
    static cm_m inst, set;
    if (!set.fn) { inst = cm_il_method("Refactor", "UIScaleManager", "get_Instance", 0, NULL); set = cm_il_method("Refactor", "UIScaleManager", "SetScale", 2, NULL); }
    Obj m = inst.fn ? CM_CALL(inst, Obj, const void *)(inst.mi) : NULL;
    if (!cm_alive(m) || !set.fn) return -2;
    CM_CALL(set, void, Obj, float, bool, const void *)(m, v, true, set.mi);
    rig_log("pvp ui: test: UI scale set to %.2f (now %.3f)", v, pv_ui_scale());
    return 0;
}
static char *pvp_ui_exec(const char *cmd, const char *line) {            /* tests: mpm_pvp {"action":"esc|close|start|ready|ok|player0..|arrow r d"} */
    if (strcmp(cmd, "mpm_pvp")) return NULL;
    char a[32] = ""; jstr(line, "action", a, sizeof a); double v = 0, d = 1;
    if (!strcmp(a, "probe")) return pv_probe(line);
    if (!strcmp(a, "uiscale")) { jnum(line, "v", &v); int rc = v > 0 ? pv_set_ui_scale((float)v) : -9; char o[64]; snprintf(o, sizeof o, "{\"ok\":%s,\"rc\":%d}", rc ? "false" : "true", rc); return strdup(o); }
    if (!strcmp(a, "esc_menu")) { popup_activate(1); goto out; }          /* test: open the Esc menu (as the Esc key) */
    if (!strcmp(a, "bring_text")) { if (pv_open) { br_sent[0] = 0; br_show(1); } goto out; }   /* test: the text Bring list (the card window's fallback without the kit) */
    if (!strcmp(a, "esc")) pv_click = PVB_ESC;
    else if (!strcmp(a, "close")) pv_click = PVB_CLOSE;
    else if (!strcmp(a, "start")) pv_click = PVB_START;
    else if (!strcmp(a, "ready")) pv_click = PVB_READY;
    else if (!strcmp(a, "press_fight") || !strcmp(a, "press_prop_yes") || !strcmp(a, "press_prop_no") || !strcmp(a, "press_hud_bring")) {   /* E4 / E2: only when shown (as a click) */
        int k = a[6] == 'f' ? PVB_FIGHT : a[6] == 'h' ? PVB_HUD_BRING : a[11] == 'y' ? PVB_PROP_YES : PVB_PROP_NO;
        if (!u_on(pvb(k))) return strdup("{\"ok\":false,\"error\":\"button not shown\"}");
        pv_click = k; goto out;
    }
    else if (!strcmp(a, "ok")) pv_click = PVB_RESULT_OK;
    else if (!strcmp(a, "result_show")) { const pvp_state *s = pvp_ui_state ? pvp_ui_state() : NULL; if (!s || !pr_show(s)) return strdup("{\"ok\":false,\"error\":\"no Esc menu\"}"); }   /* test: the result window now (Escape tests) */
    else if (!strncmp(a, "press_", 6)) {                                  /* the real path: OnPressButton of the clone */
        if (!strcmp(a + 6, "ready")) { if (!u_on(pvb(PVB_READY))) return strdup("{\"ok\":false,\"error\":\"Ready not shown\"}"); pv_click = PVB_READY; goto out; }   /* image button: same handler as its click */
        int k = !strcmp(a + 6, "esc") ? PVB_ESC : !strcmp(a + 6, "start") ? PVB_START :
                !strcmp(a + 6, "ok") ? PVB_RESULT_OK : !strcmp(a + 6, "close") ? PVB_CLOSE : -1;
        Obj bt = k >= 0 ? pvb(k) : NULL;
        if (bt && u_on(bt)) (mm_is(bt, K_SMB) ? F(0xF2DC30, void (*)(Obj, void *))(bt, NULL) : (void)0);
        else return strdup("{\"ok\":false,\"error\":\"button not shown\"}");
    }
    else if (!strcmp(a, "player")) { jnum(line, "i", &v); pv_click = PVB_P0 + (int)v; }
    else if (!strcmp(a, "bring")) pv_click = PVB_BRING;
    else if (!strcmp(a, "br_save")) { jnum(line, "i", &v); pv_click = PVB_BR_SAVE0 + (int)v; }
    else if (!strcmp(a, "br_unit")) { jnum(line, "i", &v); pv_click = PVB_BR_UNIT0 + (int)v; }
    else if (!strcmp(a, "br_send")) pv_click = PVB_BR_SEND;
    else if (!strcmp(a, "br_back")) pv_click = PVB_BR_BACK;
    else if (!strcmp(a, "arrow")) { jnum(line, "row", &v); jnum(line, "dir", &d); pv_click = PVB_ARROW0 + 2 * (int)v + (d > 0); }
    else if (!strcmp(a, "preset_arrow")) { jnum(line, "dir", &d); pv_click = d > 0 ? PVB_PRE_R : PVB_PRE_L; }   /* lane pvprules: the presets (as clicks) */
    else if (!strcmp(a, "preset_save_open")) pv_click = PVB_PRE_SAVEAS;
    else if (!strcmp(a, "preset_save")) { char nm[300] = ""; jstr(line, "name", nm, sizeof nm); pre_typed(nm); goto out; }
    else if (!strcmp(a, "preset_delete")) pv_click = PVB_PRE_DEL;
    out:;
    Buf b = {0};
    bput(&b, "{\"ok\":true,\"bring\":{\"open\":%d,\"lobby\":%d,\"saves\":[", br_open, br_lobby);
    for (int i = 0; i < br_nsaves; i++) { bput(&b, "%s", i ? "," : ""); bstr(&b, br_stems[i]); }
    bput(&b, "],\"save\":%d,\"units\":[", br_save);
    for (int i = 0; i < br_nunits; i++) { bput(&b, "%s[", i ? "," : ""); bstr(&b, br_unit_name[i]); bput(&b, ",\"%s\",%d,%d]", br_unit_guid[i], br_unit_lvl[i], br_pick[i]); }
    bput(&b, "],\"status\":"); bstr(&b, br_sent); bput(&b, "},");
    bput(&b, "\"roots\":{\"window\":%d,\"bring\":%d,\"result\":%d},", u_on(pvt(h_pv_root)), u_on(pvt(h_br_root)), u_on(pvt(h_pr_root)));   /* shown objects (Escape tests) */
    { Obj ev = esc_view(); bput(&b, "\"esc_menu\":%d,", ev && u_on(ev) ? 1 : 0); }                    /* the game's Esc menu shows */
    bput(&b, "\"window\":%d,\"result\":%d,\"esc_entry\":%d,\"hud\":%d,\"ready\":%d,\"fight_btn\":%d,\"proposal_box\":%d,\"round_screen\":%d,\"hud_text\":\"%s\",\"mode\":%d,\"bo\":%d,\"secs\":%d,\"pause\":%d,\"vanilla\":%d,\"vanilla_shown\":%d,\"note\":",
         pv_open, pr_open, u_on(pvb(PVB_ESC)), u_go_active_self(u_gc_get(h_hud)), u_on(pvb(PVB_READY)), u_on(pvb(PVB_FIGHT)), u_alive(u_gc_get(h_prop_root)) && u_on(u_gc_get(h_prop_root)), u_alive(u_gc_get(h_rs_root)) && u_on(u_gc_get(h_rs_root)), hud_last, pv.mode, pv_bo[pv.bo], pv_secs[pv.secs], pv.pause,
         pv.vanilla, pv_vanilla_shown(pvp_ui_state ? pvp_ui_state() : NULL, pvp_ui_is_host && pvp_ui_is_host()));
    bstr(&b, pv_note_txt); bput(&b, ",\"note_err\":%d,\"tip\":", pv_open && pv_note_err);
    bstr(&b, tip_now);
    bput(&b, ",\"columns\":[");                          /* unit names shown in the team columns */
    for (int i = 0; i < pv_ncol; i++) { bput(&b, "%s", i ? "," : ""); bstr(&b, pv_col_names[i]); }
    bput(&b, "],\"shown_teams\":[");                    /* the team each present player is shown in (id, 1 Blue / 2 Red) */
    { int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8);
      for (int i = 0; i < n; i++) bput(&b, "%s[%d,%d]", i ? "," : "", ids[i], pv_team_of(ids[i])); }
    bput(&b, "],");
    pre_status(&b);
    bput(&b, "}");
    return b.p;
}

/* Bring Units = the card window (mu_bring.h: this player's own saves, Compact Plus cards with the game's icons), up to this
   player's share. Without the loader's widget kit (the rig's own dylib) the text list above opens. */
#include "mu_bring.h"
int pvp_bring_picks(const mu_pick *picks, int n, char *err, size_t errn);   /* fe_game.c */
void ui_notice(const char *text);
static mu_pick pv_last_picks[8]; static int pv_nlast;
static void pv_bring_done(const mu_pick *picks, int n, void *ud) {
    (void)ud; if (n < 0) return;
    char err[300] = "";
    if (pvp_bring_picks(picks, n, err, sizeof err)) { rig_log("pvp ui: Bring Units: %s", err); snprintf(br_sent, sizeof br_sent, TR("Could not bring the units: %s"), err); dsi_utf8_fix_end(br_sent); ui_notice(err); return; }
    pv_nlast = n < 8 ? n : 8; memcpy(pv_last_picks, picks, sizeof picks[0] * (size_t)pv_nlast);
    snprintf(br_sent, sizeof br_sent, TRN("Brought %d unit. The host starts the match with them.", "Brought %d units. The host starts the match with them.", n), n);
    rig_log("pvp ui: Bring Units: %d unit(s) sent", n);
    if (pv_open) pv_refresh();
}
static int pv_bring_saves(mu_save_ref *out, int max, void *ud) {   /* E1: game + modded saves (pvp_bring_save_paths) */
    (void)ud; static char p[64][1024]; int n = pvp_bring_save_paths(p, max < 64 ? max : 64);
    for (int i = 0; i < n; i++) {
        snprintf(out[i].path, sizeof out[i].path, "%s", p[i]); out[i].name[0] = 0;   /* empty name: the window's own pretty name */
        struct stat st; out[i].when[0] = 0;
        if (!stat(p[i], &st)) { time_t t = st.st_mtime; struct tm *tm = localtime(&t); if (tm) strftime(out[i].when, sizeof out[i].when, "%Y-%m-%d %H:%M", tm); }
    }
    return n;
}
static int pv_bring_open(void) {
    char sub[400]; snprintf(sub, sizeof sub, TRN("Up to %d unit for the match. Copies only: your saves do not change.",
                                                 "Up to %d units for the match. Copies only: your saves do not change.", br_share()), br_share());
    dsi_utf8_fix_end(sub);
    mu_bring_req r = { .title = TR("Bring Units"), .subtitle = sub, .max_units = br_share(), .ticked = pv_last_picks, .nticked = pv_nlast, .list_saves = pv_bring_saves, .done = pv_bring_done };
    return mu_bring_open(&r);
}

/* ---- presets (lane pvprules, owner 2026-10-20; on the old PvP page until 2026-10-21): every change is saved at once
   (pvp_rules.txt in the mod's data folder), so the last used rules come back at the next start */
static void pre_step(int d) {                                            /* the arrows go through the saved presets (applied at once) */
    pv_sync_once();
    pvpr_store *s = pvp_rules_store(); if (s->npresets <= 0) return;
    int cur = pvpr_preset_match(s, &s->last), n = s->npresets;
    int nx = cur < 0 ? (d > 0 ? 0 : n - 1) : ((cur + d) % n + n) % n;
    s->last = s->presets[nx].v; pv_from_store(); pvp_rules_changed();
    snprintf(pre_msg, sizeof pre_msg, TR("Preset \"%s\" applied."), s->presets[nx].name); dsi_utf8_fix_end(pre_msg);
    rig_log("pvp ui: preset %d \"%s\" applied", nx, s->presets[nx].name);
}
/* the preset name field: a clone of a stock TMP_InputField, made on first use (mpmenu.c; NULL when the scene has none) */
static Obj pre_field(void) {
    Obj f = u_gc_get(h_pv_field); if (u_alive(f)) return f;
    Obj root = pvt(h_pv_root); if (!root) return NULL;
    f = mpmenu_field_new(u_tr(root), TR("Preset name"), "");
    if (!f) return NULL;
    place(f, PV_COLX(1) + 150 - PV_W / 2.0f, PV_H / 2.0f - PV_PBTN_Y, 300, 34);
    u_gc_free(h_pv_field); h_pv_field = u_gc_new(f);
    return f;
}
static void pre_naming_set(int on) {
    pre_naming = on;
    u_active(pvb(PVB_PRE_SAVEAS), !on); u_active(pvb(PVB_PRE_DEL), !on);
    u_active(pvb(PVB_PRE_SAVE), on); u_active(pvb(PVB_PRE_CANCEL), on);
    Obj f = on ? pre_field() : u_gc_get(h_pv_field); if (u_alive(f)) {
        if (on) { pvpr_store *s = pvp_rules_store(); int cur = pvpr_preset_match(s, &s->last); mpmenu_field_put(f, cur >= 0 ? s->presets[cur].name : ""); }
        u_active(f, on);
    }
}
/* Save as preset: the name in the field (or the given one: tests); same name = that preset is replaced */
static int pre_save(const char *given) {
    char nm[256] = "";
    if (given) snprintf(nm, sizeof nm, "%s", given); else mpmenu_field_get(u_gc_get(h_pv_field), nm, sizeof nm);
    pv_sync_once(); pv_to_store();
    pvpr_store *s = pvp_rules_store();
    int had = pvpr_preset_find(s, nm), i = pvpr_preset_save(s, nm, &s->last);
    if (i == -1) { snprintf(pre_msg, sizeof pre_msg, "%s", TR("Type a name for the preset first.")); return -1; }
    if (i == -2) { snprintf(pre_msg, sizeof pre_msg, TR("You have %d presets: delete one first."), PVPR_PRESETS_MAX); return -2; }
    pvp_rules_changed();
    snprintf(pre_msg, sizeof pre_msg, had >= 0 ? TR("Preset \"%s\" replaced.") : TR("Preset \"%s\" saved."), s->presets[i].name); dsi_utf8_fix_end(pre_msg);
    rig_log("pvp ui: preset \"%s\" %s (%d preset(s))", s->presets[i].name, had >= 0 ? "replaced" : "saved", s->npresets);
    pre_naming_set(0);
    return i;
}
static int pre_typed(const char *name) {                                 /* tests: the name typed into the field (when it exists), then Save */
    Obj f = pv_open ? pre_field() : NULL;
    int rc;
    if (u_alive(f)) { pre_naming_set(1); mpmenu_field_put(f, name); rc = pre_save(NULL); } else rc = pre_save(name);
    if (pv_open) pv_refresh();
    return rc;
}
static int pre_delete(void) {
    pvpr_store *s = pvp_rules_store(); int cur = pvpr_preset_match(s, &s->last);
    if (cur < 0) return -1;
    char nm[PVPR_NAME_MAX + 1]; snprintf(nm, sizeof nm, "%s", s->presets[cur].name);
    pvpr_preset_delete(s, cur); pvp_rules_changed();
    snprintf(pre_msg, sizeof pre_msg, TR("Preset \"%s\" deleted. The rules stay as they are."), nm); dsi_utf8_fix_end(pre_msg);
    rig_log("pvp ui: preset \"%s\" deleted (%d left)", nm, s->npresets);
    return 0;
}
static void pre_status(Buf *b) {                                         /* tests: the rows as shown, the presets */
    pvpr_store *s = pvp_rules_store(); int cur = pvpr_preset_match(s, &s->last);
    bput(b, "\"naming\":%d,\"field\":%d,\"preset_now\":", pre_naming, u_alive(u_gc_get(h_pv_field)));
    bstr(b, cur >= 0 ? s->presets[cur].name : ""); bput(b, ",\"presets\":%d,\"preset_msg\":", s->npresets); bstr(b, pre_msg);
    bput(b, ",\"rows\":[");
    for (int i = 0; i < PVR_N; i++) { char v[96]; pv_value(i, v, sizeof v); bput(b, "%s[", i ? "," : ""); bstr(b, pv_lab(i)); bput(b, ","); bstr(b, v); bput(b, "]"); }
    bput(b, "]");
}
static void pvp_frame_all(const mpm_obs *o) { pvp_ui_frame(o); }

/* ---- menu hooks (mpmenu.c calls them where this file was called before the split) */
static const mp_menu_hooks pvp_hooks = { .tips = pvp_ui_tips, .frame_late = pvp_frame_all, .press = pvp_ui_press, .exec = pvp_ui_exec };
const mp_menu_hooks *pvp_menu_hooks(void) { return &pvp_hooks; }
