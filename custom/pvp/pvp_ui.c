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
#include "mp.h"
#include "mp_game.h"
#include "net/dsnet.h"
#include "pvp_core.h"
#include "fe/fe_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
const pvp_state *pvp_ui_state(void) __attribute__((weak_import));
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

/* host's choices (local until Start) */
static const int pv_bo[3] = { 1, 3, 5 };
static const int pv_secs[5] = { 60, 120, 180, 300, 480 };
static struct { int mode, bo, secs, pause, vanilla, team[PVP_MAX_PLAYERS], team_set[PVP_MAX_PLAYERS]; } pv = { PVP_MODE_FIGHT, 1, 2, 0, -1, { 0 }, { 0 } };
static char pv_err[700], pv_note_txt[700];                            /* Start Match refused: why (until the next change) */
/* tested arena (rig run arena2: same map on every peer, 14 rooms, Blue arrival vs exit portal 28 cells apart) */
#define PV_REGION 1
#define PV_FLOOR 2
#define PV_SEED 424242
enum { PVR_MODE, PVR_WIN, PVR_TIME, PVR_PAUSE, PVR_ARENA, PVR_VANILLA, PVR_N };
enum { PVB_ESC, PVB_CLOSE, PVB_START, PVB_READY, PVB_RESULT_OK, PVB_BRING, PVB_BR_BACK, PVB_BR_SEND, PVB_BR_WIKI, PVB_BR_SAVE0, PVB_BR_UNIT0 = PVB_BR_SAVE0 + 8,
       PVB_P0 = PVB_BR_UNIT0 + 12, PVB_ARROW0 = PVB_P0 + 8, PVB_N = PVB_ARROW0 + 2 * PVR_N };
static uintptr_t h_pvb[PVB_N];
static uintptr_t h_pv_por[8][4], h_pv_pname[8][4];
static char pv_col_names[16][64]; static int pv_ncol;                 /* names shown in the team columns (mpm_pvp state) */                      /* per player slot: unit portraits + names */
#define PV_TEAM_TOP 150
#define PV_BLOCK 100                                                     /* one player: name button 32 + portraits 46 + names 16 */
#define PV_TEAM_ROWS 2                                                   /* 4 units in total: at most 2 players per team bring units */
#define PV_RULES_TOP (PV_TEAM_TOP + PV_TEAM_ROWS * PV_BLOCK + 40)
#define PV_H (PV_RULES_TOP + 40 + 38 * PVR_N + 26 + 130)                  /* window height; 26 = the Vanilla check line */
static uintptr_t h_pv_vsub, h_pv_root, h_pv_view, h_pv_area, h_pv_title, h_pv_hint, h_pv_teamh[3], h_pv_units[3], h_pv_rlab[PVR_N], h_pv_rval[PVR_N], h_pv_rrow[PVR_N], h_pv_note;
static uintptr_t h_pr_root, h_pr_title, h_pr_score, h_pr_rounds;          /* result window */
static uintptr_t h_hud, h_hud_name, h_hud_bar[2], h_hud_lab[2], h_hud_rlab;                        /* boss bar copy */
static int pv_open, pr_open, pv_click = -1;
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
        ids[n] = p; snprintf(names[n], 32, "%s", nm && nm[0] ? nm : p == 0 ? "Host" : "Player"); n++;
    }
    return n;
}
static void pv_default_teams(void) {
    int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8), k = 0;
    for (int i = 0; i < n; i++) if (!pv.team_set[ids[i]]) { pv.team[ids[i]] = (k++ % 2) ? PVP_TEAM_RED : PVP_TEAM_BLUE; pv.team_set[ids[i]] = 1; }
}
/* the rule shown: in a match the one sent with SETUP; else the host's choice (a guest: as the host last told it) */
static int pv_vanilla_shown(const pvp_state *s, int host) {
    if (s && s->phase != PVP_OFF) return s->rules.vanilla;
    if (host) return pv.vanilla;
    int t = pvp_mods_told_vanilla ? pvp_mods_told_vanilla() : -1;
    return t >= 0 ? t : 1;
}
static void pv_value(int r, char *out, size_t cap) {
    switch (r) {
    case PVR_VANILLA: { const pvp_state *s = pvp_ui_state ? pvp_ui_state() : NULL;
                        snprintf(out, cap, "%s", pv_vanilla_shown(s, pvp_ui_is_host && pvp_ui_is_host()) ? "On: vanilla only" : "Off: mods allowed"); break; }
    case PVR_MODE: snprintf(out, cap, "%s", pv.mode == PVP_MODE_CTF ? "Capture the Flag" : "Fight"); break;
    case PVR_WIN: if (pv.mode == PVP_MODE_CTF) snprintf(out, cap, "%d capture%s to win", pv_bo[pv.bo], pv_bo[pv.bo] > 1 ? "s" : "");
                  else snprintf(out, cap, "Best of %d", pv_bo[pv.bo]); break;
    case PVR_TIME: snprintf(out, cap, "%d:%02d per round", pv_secs[pv.secs] / 60, pv_secs[pv.secs] % 60); break;
    case PVR_PAUSE: snprintf(out, cap, "%s", pv.pause ? "Planning pause every 7 s" : "No pause"); break;
    case PVR_ARENA: snprintf(out, cap, "Dungeon floor %d (tested)", PV_FLOOR + 1); break;
    }
}
static const char *pv_label[PVR_N] = { "Mode", "To win", "Round time", "Pause", "Arena", "Vanilla check" };
static const char *pv_tip[PVR_N] = {
    "Fight: a team wins the round when the other team has no unit left standing. Capture the Flag: take the other team's banner flag and bring it to your own banner while your flag is home.",
    "Fight: how many rounds are played at most; the first team to win most of them wins the match. Capture the Flag: how many captures win the match.",
    "Time for one round of game time at normal speed. When it runs out: Fight gives the round to the team with more health left; Capture the Flag counts it as a draw.",
    "No pause: nobody can pause during the match. Planning pause: the game stops every 7 seconds of fighting so everyone can give orders; it goes on after 5 seconds or when everyone pressed Ready.",
    "The dungeon floor used as the arena. The same map for everyone, with no monsters, no items and no portal. Teams start at opposite ends.",
    "On: vanilla only. No gameplay mods for anyone. Client mods (screen and controls) are fine.\n\nOff: mods allowed. The host's gameplay mods are required, and the launcher shares them with every player." };

static void pv_build(Obj v) {
    if (!scan_done) assets_scan_now();
    Obj src = AT(v, 0x70, Obj); if (!u_alive(src)) return;
    Obj tpl_img = first_image(u_tr(src), 0), tpl_txt = AT(src, 0x28, Obj);
    const float W = 760, H = PV_H;
    #define PX(x) ((x) - W / 2)
    #define PY(y) (H / 2 - (y))
    #define KEEP(hh, o) { Obj o_ = (o); u_gc_free(hh); hh = o_ ? u_gc_new(o_) : 0; }
    Obj root = mk_img(tpl_img, u_tr(v), -1, (Col){ 0, 0, 0, 0 }, 0, 0, W, H);
    if (!root) return;
    Obj R = u_tr(root);
    KEEP(h_pv_root, root); KEEP(h_pv_view, v); KEEP(h_pv_area, u_tr_parent(u_tr_parent(u_tr(src))));
    mk_img(tpl_img, R, S_BOX, C_WHITE, 0, 0, W, H);
    mk_img(tpl_img, R, -1, C_BAND, 0, PY(32), W - 4, 60);
    mk_img(tpl_img, R, -1, C_COPPER, 0, PY(62), W - 4, 1);
    KEEP(h_pv_title, mk_txt(tpl_txt, R, 1, 30, 514, 0, 0, PY(32), 600, 50, C_MAIN));
    mk_img(tpl_img, R, -1, C_ROW1, 0, PY(84), W - 4, 42);
    KEEP(h_pv_hint, mk_txt(tpl_txt, R, 0, 16, 514, 0, 0, PY(84), 700, 30, C_SUB));
    /* teams: two columns */
    for (int t = 1; t <= 2; t++) {
        float x0 = t == 1 ? 24 : 392, cx = x0 + 172;
        KEEP(h_pv_teamh[t], mk_txt(tpl_txt, R, 1, 22, 514, 0, PX(cx), PY(128), 340, 32, t == 1 ? C_BLUE : C_RED));
        mk_img(tpl_img, R, -1, C_LINE, PX(cx), PY(148), 344, 1);
        KEEP(h_pv_units[t], mk_txt(tpl_txt, R, 0, 13, 514, 0, PX(cx), PY(PV_TEAM_TOP + PV_TEAM_ROWS * PV_BLOCK + 14), 340, 20, C_SUB));
    }
    for (int i = 0; i < 8; i++) {                                     /* player buttons + their units; placed in their column at refresh */
        Obj b = mk_button(src, R, 1, S_NBTN, C_WHITE, 0, 0, 330, 32, 16, "", C_MAIN);
        KEEP(h_pvb[PVB_P0 + i], b); u_active(b, false);
        for (int k = 0; k < 4; k++) {
            Obj fr = mk_img(tpl_img, R, -1, C_LINE, 0, 0, 44, 44); u_active(fr, false);      /* copper edge */
            Obj im = fr ? mk_img(tpl_img, u_tr(fr), -1, C_ROW2, 0, 0, 42, 42) : NULL;
            KEEP(h_pv_por[i][k], fr);
            if (fr && im) { u_gc_free(h_pv_pname[i][k]); h_pv_pname[i][k] = 0; }
            Obj nt = mk_txt(tpl_txt, R, 0, 11, 514, 0, 0, 0, 80, 16, C_SUB); u_active(nt, false);
            KEEP(h_pv_pname[i][k], nt);
        }
    }
    /* rules */
    mk_img(tpl_img, R, -1, C_LINE, 0, PY(PV_RULES_TOP), W - 48, 1);
    { Obj t = mk_txt(tpl_txt, R, 1, 20, 514, 0, 0, PY(PV_RULES_TOP + 18), 300, 28, C_GOLD); tmp_set(t, "Rules"); }
    for (int r = 0; r < PVR_N; r++) {
        float yc = PY(PV_RULES_TOP + 56 + 38 * r);
        KEEP(h_pv_rrow[r], mk_img(tpl_img, R, -1, r % 2 ? C_ROW2 : C_ROW1, 0, yc, W - 48, 36));
        KEEP(h_pv_rlab[r], mk_txt(tpl_txt, R, 0, 15, 513, 0, PX(44 + 90), yc, 180, 30, C_SUB)); tmp_set(pvt(h_pv_rlab[r]), pv_label[r]);
        KEEP(h_pv_rval[r], mk_txt(tpl_txt, R, 0, 16, 514, 0, PX(470), yc, 380, 30, C_MAIN));
        for (int a = 0; a < 2; a++) {
            Obj b = mk_button(src, R, 1, S_NBTN, C_WHITE, PX(a ? 690 : 250), yc, 28, 28, 16, a ? "\xE2\x80\xBA" : "\xE2\x80\xB9", C_MAIN);
            KEEP(h_pvb[PVB_ARROW0 + 2 * r + a], b);
        }
    }
    KEEP(h_pv_vsub, mk_txt(tpl_txt, R, 0, 13, 514, 0, 0, PY(PV_RULES_TOP + 56 + 38 * PVR_N - 6), W - 60, 22, C_SUB));
    KEEP(h_pv_note, mk_txt(tpl_txt, R, 0, 13, 514, 1, 0, PY(H - 90), W - 60, 34, C_ORANGE));
    mk_img(tpl_img, R, -1, C_BAND, 0, PY(H - 32), W - 4, 60);
    mk_img(tpl_img, R, -1, C_COPPER, 0, PY(H - 62), W - 4, 1);
    KEEP(h_pvb[PVB_CLOSE], mk_button(src, R, 1, S_NBTN, C_WHITE, PX(24 + 90), PY(H - 32), 180, 34, 16, "Close", C_MAIN));
    KEEP(h_pvb[PVB_START], mk_button(src, R, 1, S_RECRUIT, C_WHITE, PX(W - 24 - 110), PY(H - 32), 220, 42, 18, "Start Match", C_START));
    KEEP(h_pvb[PVB_BRING], mk_button(src, R, 1, S_NBTN, C_WHITE, 0, PY(H - 32), 220, 34, 16, "Bring Units...", C_MAIN));
    u_active(root, false);
    #undef PX
    #undef PY
    #undef KEEP
    rig_log("pvp ui: PvP window built");
}
static void pv_refresh(void) {
    const pvp_state *s = pvp_ui_state ? pvp_ui_state() : NULL;
    int host = pvp_ui_is_host && pvp_ui_is_host();
    pv_default_teams();
    tmp_set(pvt(h_pv_title), "PvP Match");
    int busy_match = s && s->phase != PVP_OFF;
    tmp_set(pvt(h_pv_hint), busy_match ? "A match is set up or running." : host ? "Choose the teams and the rules, then press Start Match." : "The host chooses the teams and the rules.");
    int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8), row[3] = { 0, 0, 0 };
    mp_session *ss = mp_game_session(); dsnet_client *c = ss ? mp_client(ss) : NULL;
    int units[3] = { 0, 0, 0 }; pv_ncol = 0;
    static void *kparts; if (!kparts) kparts = class_by_name("Refactor.UI", "SubUI_UnitShortCutParts");
    Obj parr = kparts ? F(0x50FCCC0, Obj (*)(Obj, void *))(F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(kparts)), NULL) : NULL;
    int nparts = parr ? (int)AT(parr, 0x18, uintptr_t) : 0;
    float H2 = PV_H / 2.0f;
    for (int i = 0; i < 8; i++) {
        Obj b = pvb(PVB_P0 + i);
        int shown = i < n && row[pv.team[ids[i]] ? pv.team[ids[i]] : PVP_TEAM_BLUE] < PV_TEAM_ROWS;
        if (!shown) { u_active(b, false); for (int k = 0; k < 4; k++) { u_active(pvt(h_pv_por[i][k]), false); u_active(pvt(h_pv_pname[i][k]), false); } continue; }
        int t = pv.team[ids[i]] ? pv.team[ids[i]] : PVP_TEAM_BLUE;
        float x0 = t == 1 ? 24 : 392, top = PV_TEAM_TOP + PV_BLOCK * row[t]; row[t]++;
        place(b, x0 + 172 - 380, H2 - (top + 16), 330, 32);
        char l[64]; snprintf(l, sizeof l, "%s%s", nm[i], ids[i] == (c ? dsnet_player_id(c) : -1) ? " (you)" : "");
        tmp_set(btn_label(b, 1), l); txt_color(btn_label(b, 1), t == 1 ? C_BLUE : C_RED);
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
                float cx = x0 + 172 - 380 - 1.5f * 82 + 82 * shown_u++;
                place(fr, cx, H2 - (top + 32 + 4 + 22), 44, 44);
                Obj sp = rp[0] ? portrait_sprite(rp) : NULL;
                Obj inner = mm_is(u_tr(fr), K_TR) && F(0x510EAE4, int (*)(Obj, void *))(u_tr(fr), NULL) > 0 ? F(0x510FC60, Obj (*)(Obj, int, void *))(u_tr(fr), 0, NULL) : NULL;
                Obj inim = inner ? image_of(inner) : NULL;
                if (inim) img_set(inim, sp, sp ? C_WHITE : C_ROW2);
                u_active(fr, true);
                char un[64] = ""; Obj txt = !strncmp(rn, "TEXTKEY", 7) ? F(0x80EBAC, Obj (*)(Obj, void *))(u_str(rn), NULL) : NULL;   /* TextKeyExtensions.GetText */
                if (txt) str_utf8(txt, un, sizeof un);
                if (!un[0] || !strncmp(un, "TEXTKEY", 7)) snprintf(un, sizeof un, "%s", rn);   /* a typed name is the key itself */
                place(nt, cx, H2 - (top + 32 + 4 + 44 + 10), 80, 16); tmp_set(nt, un); u_active(nt, true);
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
            float cx = x0 + 172 - 380 - 1.5f * 82 + 82 * shown_u++;
            place(fr, cx, H2 - (top + 32 + 4 + 22), 44, 44); u_active(fr, true);
            Obj sp = NULL;
            for (int q = 0; q < nparts; q++) { Obj pt = AT((char *)parr + 0x20, q * 8, Obj); if (u_alive(pt) && AT(pt, 0x228, Obj) == u) {
                Obj im = AT(pt, 0x140, Obj); sp = mm_is(im, K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(im, NULL) : NULL; break; } }
            Obj inner = mm_is(u_tr(fr), K_TR) && F(0x510EAE4, int (*)(Obj, void *))(u_tr(fr), NULL) > 0 ? F(0x510FC60, Obj (*)(Obj, int, void *))(u_tr(fr), 0, NULL) : NULL;
            Obj inim = inner ? image_of(inner) : NULL;
            if (inim) img_set(inim, u_alive(sp) ? sp : NULL, u_alive(sp) ? C_WHITE : C_ROW2);
            char un[48] = ""; str_utf8(F(0xFA467C, Obj (*)(Obj, void *))(u, NULL), un, sizeof un);   /* GetDisplayName */
            place(nt, cx, H2 - (top + 32 + 4 + 44 + 10), 80, 16); tmp_set(nt, un); u_active(nt, true);
            if (pv_ncol < 16) snprintf(pv_col_names[pv_ncol++], 64, "%s", un);
        }
    }
    tmp_set(pvt(h_pv_teamh[1]), "Blue"); tmp_set(pvt(h_pv_teamh[2]), "Red");
    for (int t = 1; t <= 2; t++) { char u[48]; snprintf(u, sizeof u, "%d unit%s", units[t], units[t] == 1 ? "" : "s"); tmp_set(pvt(h_pv_units[t]), u); }
    for (int r = 0; r < PVR_N; r++) {
        char v[64]; pv_value(r, v, sizeof v); tmp_set(pvt(h_pv_rval[r]), v);
        for (int a = 0; a < 2; a++) u_active(pvb(PVB_ARROW0 + 2 * r + a), host && !busy_match && r != PVR_ARENA);
    }
    const char *note = "";
    const char *refused = pvp_mods_last ? pvp_mods_last() : "";
    if (!busy_match && pv_err[0]) note = pv_err;                                        /* host: Start Match was refused */
    else if (!host && !busy_match && refused && refused[0]) note = refused;            /* guest: the host's refusal */
    else if (host && !busy_match && (!units[1] || !units[2])) note = "Each team needs at least one unit (claim units with right-click on their portraits).";
    else if (host && !busy_match) note = "Nothing that happens in the match stays: the world comes back as it was before.";
    { char vs[400] = ""; if (pvp_mods_summary) pvp_mods_summary(pv_vanilla_shown(s, host), host, vs, sizeof vs); tmp_set(pvt(h_pv_vsub), vs); }
    u_active(pvb(PVB_START), host && !busy_match);
    u_active(pvb(PVB_BRING), !busy_match && pvp_units_exec != NULL);
    char nb[160];
    if (!busy_match && note != pv_err && note != refused && pvp_units_roster && pvp_units_roster(-1, NULL, NULL, NULL) > 0) {
        snprintf(nb, sizeof nb, "The teams fight with the %d brought unit(s) shown above; the world's units step aside until the end.", pvp_units_roster(-1, NULL, NULL, NULL));
        note = nb;
    }
    tmp_set(pvt(h_pv_note), note); snprintf(pv_note_txt, sizeof pv_note_txt, "%s", note);
}
static void pv_show(int on) {
    Obj root = pvt(h_pv_root), area = u_gc_get(h_pv_area);
    if (!root) return;
    u_active(root, on); if (u_alive(area)) u_active(area, !on);
    pv_open = on;
    if (on) {
        int host = pvp_ui_is_host && pvp_ui_is_host();
        if (pv.vanilla < 0) pv.vanilla = pvp_mods_default_vanilla ? pvp_mods_default_vanilla() : 1;
        pv_err[0] = 0;
        if (host && pvp_mods_ask) pvp_mods_ask(pv.vanilla);                 /* every player sends its mods (and sees the rule) */
        pv_refresh();
    }
}
static void pv_start(void) {
    uint8_t teams[PVP_MAX_PLAYERS] = { 0 };
    int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8);
    for (int i = 0; i < n; i++) teams[ids[i]] = (uint8_t)(pv.team[ids[i]] ? pv.team[ids[i]] : PVP_TEAM_BLUE);
    pvp_rules r = { (uint8_t)pv_bo[pv.bo], (uint16_t)pv_secs[pv.secs], (uint8_t)(pv.pause ? PVP_PAUSE_PLANNING : PVP_PAUSE_NONE), 7, 5, (uint8_t)(pv.vanilla != 0) };
    int rc = pvp_ui_start_match ? pvp_ui_start_match(pv.mode, teams, &r, PV_REGION, PV_FLOOR, PV_SEED) : -9;
    rig_log("pvp ui: Start Match: rc %d", rc);
    if (rc == -4) {                                                       /* Vanilla check refused: stay, say why */
        snprintf(pv_err, sizeof pv_err, "%s", pvp_mods_last ? pvp_mods_last() : "Start Match was refused.");
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
    if (k == PVB_RESULT_OK) { u_active(pvt(h_pr_root), false); pr_open = 0; Obj area = u_gc_get(h_pv_area); if (u_alive(area)) u_active(area, true);
                              Obj ev = esc_view(); if (ev && u_on(ev)) (mm_is(ev, K_SMV) ? F(0xF2ED1C, void (*)(Obj, void *))(ev, NULL) : (void)0); return; }
    if (!host) return;
    if (k >= PVB_P0 && k < PVB_P0 + 8) {
        int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8), i = k - PVB_P0;
        if (i < n) { pv.team[ids[i]] = pv.team[ids[i]] == PVP_TEAM_RED ? PVP_TEAM_BLUE : PVP_TEAM_RED; pv.team_set[ids[i]] = 1; }
    } else if (k >= PVB_ARROW0 && k < PVB_N) {
        int r = (k - PVB_ARROW0) / 2, d = (k - PVB_ARROW0) % 2 ? 1 : -1;
        if (r == PVR_MODE) pv.mode = pv.mode == PVP_MODE_FIGHT ? PVP_MODE_CTF : PVP_MODE_FIGHT;
        if (r == PVR_WIN) pv.bo = (pv.bo + d + 3) % 3;
        if (r == PVR_TIME) pv.secs = (pv.secs + d + 5) % 5;
        if (r == PVR_PAUSE) pv.pause = !pv.pause;
        if (r == PVR_VANILLA) { pv.vanilla = !pv.vanilla; if (pvp_mods_ask) pvp_mods_ask(pv.vanilla); }
        pv_err[0] = 0;
    }
    pv_refresh();
}

/* ---- Bring Units window (M3c): pick one of YOUR saves, pick units from it (at most your share of the 4-unit dungeon
   team), Bring = they are copied from that save and sent to the host (pvp_units.c). Same frame as the PvP window. */
#include <dirent.h>
#include <sys/stat.h>
static uintptr_t h_br_root, h_br_title, h_br_hint, h_br_status, h_br_por[12], h_br_lv[12];
static int br_open, br_nsaves, br_save = -1, br_nunits, br_pick[12];
static char br_files[8][300], br_stems[8][96], br_unit_guid[12][40], br_unit_name[12][48], br_unit_port[12][64], br_sent[200];
static int br_unit_lvl[12];
static int br_share(void) {                                              /* 4 units in total, split between the players */
    int ids[8]; char nm[8][32]; int n = pvp_ui_present(ids, nm, 8); if (n < 1) n = 1;
    int sh = 4 / n; return sh < 1 ? 1 : sh;
}
static void br_list_saves(void) {
    Obj ps = F(0x507B74C, Obj (*)(void *))(NULL);                         /* Application.persistentDataPath (the copy's own data) */
    char base[600] = ""; str_utf8(ps, base, sizeof base);
    char dir[700]; snprintf(dir, sizeof dir, "%s/Saves", base);
    struct { char f[300], st[96]; time_t t; } all[64]; int n = 0;
    DIR *d = opendir(dir);
    for (struct dirent *e; d && (e = readdir(d)) && n < 64; ) {
        size_t l = strlen(e->d_name); if (l < 6 || strcmp(e->d_name + l - 5, ".json")) continue;
        if (!strncmp(e->d_name, "mp_sync", 7) || !strncmp(e->d_name, "MPFE_", 5)) continue;
        snprintf(all[n].f, sizeof all[n].f, "%s/%s", dir, e->d_name);
        snprintf(all[n].st, sizeof all[n].st, "%.*s", (int)(l - 5), e->d_name);
        struct stat sb; all[n].t = stat(all[n].f, &sb) == 0 ? sb.st_mtime : 0; n++;
    }
    if (d) closedir(d);
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && all[j].t > all[j - 1].t; j--) { __typeof__(all[0]) t = all[j]; all[j] = all[j - 1]; all[j - 1] = t; }
    br_nsaves = n < 8 ? n : 8;
    for (int i = 0; i < br_nsaves; i++) { snprintf(br_files[i], sizeof br_files[i], "%s", all[i].f); snprintf(br_stems[i], sizeof br_stems[i], "%s", all[i].st); }
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
            br_unit_name[u][0] = 0; if (txt) str_utf8(txt, br_unit_name[u], sizeof br_unit_name[u]);
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
    const float W = 760, H = 700;
    #define BX(x) ((x) - W / 2)
    #define BY(y) (H / 2 - (y))
    #define KEEP(hh, o) { Obj o_ = (o); u_gc_free(hh); hh = o_ ? u_gc_new(o_) : 0; }
    Obj root = mk_img(tpl_img, u_tr(v), -1, (Col){ 0, 0, 0, 0 }, 0, 0, W, H); if (!root) return;
    Obj R = u_tr(root); KEEP(h_br_root, root);
    mk_img(tpl_img, R, S_BOX, C_WHITE, 0, 0, W, H);
    mk_img(tpl_img, R, -1, C_BAND, 0, BY(32), W - 4, 60); mk_img(tpl_img, R, -1, C_COPPER, 0, BY(62), W - 4, 1);
    KEEP(h_br_title, mk_txt(tpl_txt, R, 1, 30, 514, 0, 0, BY(32), 600, 50, C_MAIN));
    mk_img(tpl_img, R, -1, C_ROW1, 0, BY(84), W - 4, 42);
    KEEP(h_br_hint, mk_txt(tpl_txt, R, 0, 15, 514, 0, 0, BY(84), 720, 30, C_SUB));
    { Obj t = mk_txt(tpl_txt, R, 1, 20, 514, 0, BX(24 + 120), BY(124), 240, 28, C_GOLD); tmp_set(t, "Your Saves"); }
    { Obj t = mk_txt(tpl_txt, R, 1, 20, 514, 0, BX(290 + 223), BY(124), 446, 28, C_GOLD); tmp_set(t, "Units"); }
    for (int i = 0; i < 8; i++) KEEP(h_pvb[PVB_BR_SAVE0 + i], mk_button(src, R, 1, S_NBTN, C_WHITE, BX(24 + 120), BY(160 + 44 * i), 240, 38, 13, "", C_MAIN));
    for (int i = 0; i < 12; i++) {
        float x = BX(290 + 74 + 150 * (i % 3)), top = 150 + 112 * (i / 3);       /* portrait, name button, level under it */
        Obj fr = mk_img(tpl_img, R, -1, C_LINE, x, BY(top + 24), 44, 44); if (fr) mk_img(tpl_img, u_tr(fr), -1, C_ROW2, 0, 0, 42, 42);
        KEEP(h_br_por[i], fr);
        Obj b = mk_button(src, R, 1, S_NBTN, C_WHITE, x, BY(top + 66), 140, 30, 13, "", C_MAIN); KEEP(h_pvb[PVB_BR_UNIT0 + i], b);
        KEEP(h_br_lv[i], mk_txt(tpl_txt, R, 0, 11, 514, 0, x, BY(top + 92), 140, 16, C_SUB));
    }
    KEEP(h_br_status, mk_txt(tpl_txt, R, 0, 13, 514, 1, 0, BY(H - 90), W - 60, 34, C_ORANGE));
    mk_img(tpl_img, R, -1, C_BAND, 0, BY(H - 32), W - 4, 60); mk_img(tpl_img, R, -1, C_COPPER, 0, BY(H - 62), W - 4, 1);
    KEEP(h_pvb[PVB_BR_BACK], mk_button(src, R, 1, S_NBTN, C_WHITE, BX(24 + 90), BY(H - 32), 180, 34, 16, "Back", C_MAIN));
    KEEP(h_pvb[PVB_BR_SEND], mk_button(src, R, 1, S_RECRUIT, C_WHITE, BX(W - 24 - 110), BY(H - 32), 220, 42, 18, "Bring", C_START));
    KEEP(h_pvb[PVB_BR_WIKI], mk_button(src, R, 1, S_NBTN, C_WHITE, 0, BY(H - 32), 220, 34, 15, "Paste Wiki Link", C_MAIN));
    u_active(root, false);
    #undef BX
    #undef BY
    #undef KEEP
}
static void br_refresh(void) {
    int share = br_share(), npick = 0; for (int i = 0; i < br_nunits; i++) npick += br_pick[i];
    tmp_set(pvt(h_br_title), "Bring Units");
    char h[200]; snprintf(h, sizeof h, "Pick up to %d units from a save on your computer, or paste a wiki builder team link. Copies only: nothing is changed.", share);
    tmp_set(pvt(h_br_hint), h);
    for (int i = 0; i < 8; i++) {
        Obj b = pvb(PVB_BR_SAVE0 + i); u_active(b, i < br_nsaves);
        if (i < br_nsaves) { char st[40]; size_t L = strlen(br_stems[i]);
                             if (L > 26) snprintf(st, sizeof st, "%.24s\xE2\x80\xA6", br_stems[i]); else snprintf(st, sizeof st, "%s", br_stems[i]);   /* long autosave names: cut */
                             tmp_set(btn_label(b, 1), st); txt_color(btn_label(b, 1), i == br_save ? C_GOLD : C_MAIN); }
    }
    for (int i = 0; i < 12; i++) {
        Obj b = pvb(PVB_BR_UNIT0 + i), fr = pvt(h_br_por[i]), lv = pvt(h_br_lv[i]);
        int on = i < br_nunits; u_active(b, on); u_active(fr, on); u_active(lv, on);
        if (!on) continue;
        char l[80]; snprintf(l, sizeof l, "%s%s", br_pick[i] ? "\xE2\x80\xA2 " : "", br_unit_name[i]);   /* picked: gold with a dot (the font has no tick) */
        tmp_set(btn_label(b, 1), l); txt_color(btn_label(b, 1), br_pick[i] ? C_GOLD : C_MAIN);
        char lt[40]; snprintf(lt, sizeof lt, "Level %d", br_unit_lvl[i]); tmp_set(lv, lt);
        Obj inner = mm_is(u_tr(fr), K_TR) && F(0x510EAE4, int (*)(Obj, void *))(u_tr(fr), NULL) > 0 ? F(0x510FC60, Obj (*)(Obj, int, void *))(u_tr(fr), 0, NULL) : NULL;
        Obj im = inner ? image_of(inner) : NULL, sp = br_unit_port[i][0] ? portrait_sprite(br_unit_port[i]) : NULL;
        if (im) img_set(im, sp, sp ? C_WHITE : C_ROW2);
        u_active(fr, sp != NULL);                                        /* no loaded portrait: no empty frame */
    }
    char st[220];
    if (br_sent[0]) snprintf(st, sizeof st, "%s", br_sent);
    else snprintf(st, sizeof st, "%d of %d picked.", npick, share);
    tmp_set(pvt(h_br_status), st);
    u_active(pvb(PVB_BR_SEND), npick > 0);
}
static void br_show(int on) {
    Obj root = pvt(h_br_root), pw = pvt(h_pv_root); if (!root) return;
    u_active(root, on); if (pw) u_active(pw, !on);
    br_open = on; if (on) { br_list_saves(); br_refresh(); } else pv_refresh();
}
static void br_send(void) {
    if (br_save < 0 || !pvp_units_exec) return;
    char gl[600] = ""; size_t o = 0; int n = 0;
    for (int i = 0; i < br_nunits; i++) if (br_pick[i]) { o += (size_t)snprintf(gl + o, sizeof gl - o, "%s%s", n ? "," : "", br_unit_guid[i]); n++; }
    Buf b = { 0 }; bput(&b, "{\"cmd\":\"pvp_units_bring\",\"file\":"); bstr(&b, br_files[br_save]); bput(&b, ",\"guids\":"); bstr(&b, gl); bput(&b, "}");
    char *r = pvp_units_exec("pvp_units_bring", b.p); free(b.p);
    int ok = r && strstr(r, "\"ok\":true");
    char names[160] = ""; size_t q = 0; for (int i = 0, k = 0; i < br_nunits; i++) if (br_pick[i]) q += (size_t)snprintf(names + q, sizeof names - q, "%s%s", k++ ? ", " : "", br_unit_name[i]);
    snprintf(br_sent, sizeof br_sent, ok ? "Brought: %s. The host starts the match with them." : "Could not bring the units: %s", ok ? names : (r ? r : "?"));
    rig_log("pvp ui: Bring %d unit(s) from %s: %s", n, br_files[br_save], r ? r : "(no answer)");
    free(r);
    br_refresh();
}
/* "Paste Wiki Link": the units of a DS wiki builder team link on the clipboard (Team Builder > Share), rebuilt in the
   mod from the game's own tables (pvp_build.c), at most this player's share */
static void br_wiki(void) {
    if (!pvp_units_exec) return;
    static char clip[8192]; clip[0] = 0; str_utf8(F(0x518AEA0, Obj (*)(void *))(NULL), clip, sizeof clip);   /* GUIUtility.systemCopyBuffer */
    if (!strstr(clip, "team=") && strlen(clip) < 8) { snprintf(br_sent, sizeof br_sent, "Copy a team link on the wiki builder first (Share), then press Paste Wiki Link."); br_refresh(); return; }
    Buf b = { 0 }; bput(&b, "{\"cmd\":\"pvp_units_wiki\",\"max\":%d,\"link\":", br_share()); bstr(&b, clip); bput(&b, "}");
    char *r = pvp_units_exec("pvp_units_wiki", b.p); free(b.p);
    int ok = r && strstr(r, "\"ok\":true");
    char er[300] = "", nn[16] = ""; if (r) { jstr(r, "error", er, sizeof er); }
    double n = 0; if (r) jnum(r, "n", &n); snprintf(nn, sizeof nn, "%d", (int)n);
    if (ok) snprintf(br_sent, sizeof br_sent, "Brought %s unit(s) from the wiki link. The host starts the match with them.", nn);
    else snprintf(br_sent, sizeof br_sent, "Could not use the wiki link: %s", er[0] ? er : "?");
    for (int i = 0; i < br_nunits; i++) br_pick[i] = 0;
    rig_log("pvp ui: wiki link (%zu chars): %s", strlen(clip), r ? r : "(no answer)");
    free(r);
    br_refresh();
}
static int br_click(int k) {                                            /* 1 = a Bring window button */
    if (k == PVB_BR_WIKI) { br_wiki(); return 1; }
    if (k == PVB_BRING) { br_sent[0] = 0; br_show(1); return 1; }
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
    if (u_alive(name)) AT(name, 0x7C0, bool) = false;                    /* DASText._useTextKey off: our text stays */
    if (mm_is(comp, K_BEH)) F(0x50ECEA0, void (*)(Obj, bool, void *))(comp, false, NULL);   /* the copy never runs the boss code */
    Obj aff = AT(comp, 0x48, Obj); if (u_alive(aff)) u_active(aff, false);
    u_gc_free(h_hud); h_hud = u_gc_new(cl); u_gc_free(h_hud_name); h_hud_name = u_alive(name) ? u_gc_new(name) : 0;
    u_gc_free(h_hud_bar[0]); h_hud_bar[0] = u_alive(head) ? u_gc_new(head) : 0;
    u_gc_free(h_hud_bar[1]); h_hud_bar[1] = u_alive(body) ? u_gc_new(body) : 0;
    if (u_alive(name)) txt_style(name, 1, 22, 514, 0);                    /* one line, Cinzel */
    /* owner 2026-10-14: each bar in its team colour (the boss sprites are red / orange, a tint cannot make them blue): a
       plain rect in the team colour, pivot moved to its left edge (same place), filled by scaling x from that edge */
    for (int k = 0; k < 2; k++) {
        Obj bar = k ? body : head, rt = u_alive(bar) ? u_tr(bar) : NULL; if (!mm_is(rt, K_RT)) continue;
        typedef struct { float x, y, w, h; } RectF;
        RectF r = F(0x5106978, RectF (*)(Obj, void *))(rt, NULL);
        V2 pv = F(0x5107200, V2 (*)(Obj, void *))(rt, NULL), ap = F(0x5106E38, V2 (*)(Obj, void *))(rt, NULL);
        F(0x51072F4, void (*)(Obj, V2, void *))(rt, (V2){ 0, pv.y }, NULL);
        ap.x -= r.w * pv.x; F(0x5106F2C, void (*)(Obj, V2, void *))(rt, ap, NULL);
        img_set(bar, NULL, k ? (Col){ 0.86f, 0.30f, 0.30f, 1 } : (Col){ 0.33f, 0.52f, 0.95f, 1 });
    }
    { Obj rt = tr; if (mm_is(rt, K_RT)) { V2 p = F(0x5106E38, V2 (*)(Obj, void *))(rt, NULL); p.y -= 215; F(0x5106F2C, void (*)(Obj, V2, void *))(rt, p, NULL); } }   /* under the unit portraits */
    Obj tpl_t = u_alive(name) ? name : NULL;
    for (int k = 0; k < 2 && tpl_t; k++) {                               /* "Blue" left of the top bar, "Red" left of the bottom one */
        Obj bar = k ? body : head; if (!u_alive(bar)) continue;
        Obj l = u_instantiate(tpl_t, u_tr_parent(u_tr(bar)));
        if (!u_alive(l)) continue;
        AT(l, 0x7C0, bool) = false; AT(l, 0x7C8, Obj) = NULL; txt_style(l, 1, 16, 516, 0); txt_color(l, k ? C_RED : C_BLUE); tmp_set(l, k ? "Red" : "Blue");
        u_gc_free(h_hud_lab[k]); h_hud_lab[k] = u_gc_new(l);
        Obj brt = u_tr(bar); V2 bp = mm_is(brt, K_RT) ? F(0x5106E38, V2 (*)(Obj, void *))(brt, NULL) : (V2){ 0, 0 };
        place(l, bp.x - 52, bp.y, 90, 24);                                /* bp.x = the bar's left edge (pivot moved above) */
    }
    /* Ready button (planning pause): built from the bar's own parts (the Esc menu may never have been opened here): the
       Recruit button sprite + a Cinzel label; the click is read with Input.GetMouseButtonDown in hud_frame */
    if (u_alive(head) && u_alive(name)) {
        Obj hrt = u_tr(body ? body : head); V2 hp = mm_is(hrt, K_RT) ? F(0x5106E38, V2 (*)(Obj, void *))(hrt, NULL) : (V2){ 0, 0 };
        Obj b = mk_img(head, u_tr_parent(hrt), S_RECRUIT, C_WHITE, hp.x, hp.y - 44, 170, 38);   /* under the bars, centred */
        if (b) { Obj l = mk_txt(name, u_tr(b), 1, 18, 514, 0, 0, 0, 170, 38, C_START);
                 if (l) { AT(l, 0x7C0, bool) = false; AT(l, 0x7C8, Obj) = NULL; tmp_set(l, "Ready"); u_gc_free(h_hud_rlab); h_hud_rlab = u_gc_new(l); } }
        u_gc_free(h_pvb[PVB_READY]); h_pvb[PVB_READY] = b ? u_gc_new(b) : 0; u_active(b, false);
    }
    u_go_active(cl, false);
    rig_log("pvp ui: boss bar copy built (name %p, bars %p %p)", name, head, body);
}
static void hud_frame(const pvp_state *s) {
    int on = s && pvp_active(s) && scene() == 2;
    Obj cl = u_gc_get(h_hud);
    if (on && !u_alive(cl)) { if (frames % 60 == 0) hud_build(); cl = u_gc_get(h_hud); }
    if (!u_alive(cl)) return;
    if (u_go_active_self(cl) != on) u_go_active(cl, on);
    if (!on || frames % 6) return;
    uint32_t pm[3] = { 0, 1000, 1000 }; if (pvp_ui_team_hp) pvp_ui_team_hp(pm);
    char t[200], clock[32] = "";
    if (s->placing) snprintf(clock, sizeof clock, s->countdown_at ? "Get ready" : "Places");
    else if (s->fight_start) { int left = (int)s->rules.round_secs - (int)((s->ticks - (s->fight_start - 1)) / 60); if (left < 0) left = 0; snprintf(clock, sizeof clock, "%d:%02d", left / 60, left % 60); }
    if (s->planning) snprintf(clock, sizeof clock, "Planning");
    snprintf(t, sizeof t, "<color=#7FA8FF>Blue</color> %d - %d <color=#FF7F7F>Red</color>   Round %d   %s", s->score[1], s->score[2], s->round, clock);
    Obj nm = u_gc_get(h_hud_name); if (u_alive(nm)) tmp_set(nm, t);
    for (int k = 0; k < 2; k++) { Obj l = u_gc_get(h_hud_lab[k]); if (u_alive(l)) tmp_set(l, k ? "Red" : "Blue"); }
    { Obj l = u_gc_get(h_hud_rlab); if (u_alive(l)) tmp_set(l, "Ready"); }   /* (DASText re-reads its key on enable) */
    for (int k = 0; k < 2; k++) { Obj im = u_gc_get(h_hud_bar[k]); Obj rt = u_alive(im) ? u_tr(im) : NULL;
        if (mm_is(rt, K_TR)) F(0x510AFF8, void (*)(Obj, V3, void *))(rt, (V3){ pm[k + 1] / 1000.0f, 1, 1 }, NULL); }   /* Transform.localScale */
    Obj rb = pvb(PVB_READY); if (rb) u_active(rb, s->planning != 0);
}
static void hud_click(const pvp_state *s) {                             /* every frame: a left click on Ready */
    Obj rb = pvb(PVB_READY);
    if (!s || !s->planning || !u_on(rb) || !F(0x51ACDD0, bool (*)(int, void *))(0, NULL)) return;   /* Input.GetMouseButtonDown(0) */
    float r[4]; V3 m = F(0x51AD4DC, V3 (*)(void *))(NULL);                                         /* Input.mousePosition */
    if (rect_of(rb, r) && m.x >= r[0] && m.x <= r[2] && m.y >= r[1] && m.y <= r[3]) pv_click = PVB_READY;
}

/* ---- result window (after the match, back in the world): on the Esc menu's root, like the PvP window */
static void pr_build(Obj v) {
    Obj src = AT(v, 0x70, Obj); if (!u_alive(src)) return;
    Obj tpl_img = first_image(u_tr(src), 0), tpl_txt = AT(src, 0x28, Obj);
    const float W = 560, H = 360;
    Obj root = mk_img(tpl_img, u_tr(v), -1, (Col){ 0, 0, 0, 0 }, 0, 0, W, H); if (!root) return;
    Obj R = u_tr(root);
    u_gc_free(h_pr_root); h_pr_root = u_gc_new(root);
    mk_img(tpl_img, R, S_BOX, C_WHITE, 0, 0, W, H);
    mk_img(tpl_img, R, -1, C_BAND, 0, H / 2 - 32, W - 4, 60);
    mk_img(tpl_img, R, -1, C_COPPER, 0, H / 2 - 62, W - 4, 1);
    Obj t = mk_txt(tpl_txt, R, 1, 30, 514, 0, 0, H / 2 - 32, 520, 50, C_MAIN); u_gc_free(h_pr_title); h_pr_title = t ? u_gc_new(t) : 0;
    t = mk_txt(tpl_txt, R, 1, 40, 514, 0, 0, H / 2 - 110, 520, 60, C_GOLD); u_gc_free(h_pr_score); h_pr_score = t ? u_gc_new(t) : 0;
    t = mk_txt(tpl_txt, R, 0, 16, 514, 1, 0, H / 2 - 190, 500, 80, C_SUB); u_gc_free(h_pr_rounds); h_pr_rounds = t ? u_gc_new(t) : 0;
    Obj b = mk_button(src, R, 1, S_RECRUIT, C_WHITE, 0, -H / 2 + 40, 260, 42, 18, "Back to the World", C_START);
    u_gc_free(h_pvb[PVB_RESULT_OK]); h_pvb[PVB_RESULT_OK] = b ? u_gc_new(b) : 0;
    u_active(root, false);
}
static void pr_frame(const pvp_state *s) {
    if (!s) return;
    /* round history (view only): one entry per round end */
    if (s->matches != pr_hist_match) { pr_hist_match = s->matches; pr_nhist = 0; pr_hist_round = 0; }
    if (pvp_active(s) && s->resetting && s->round != pr_hist_round && pr_nhist < 16) { pr_hist_round = s->round; pr_hist[pr_nhist++] = s->last_winner; }
    static int steady;
    int ready_now = s->phase == PVP_OFF && s->matches && s->matches != pr_shown_match && scene() == 2 && !busy() && rig_det_mp_ready();
    steady = ready_now ? steady + 1 : 0;
    if (steady < 120) return;                                           /* the world is back and running for 4 s */
    Obj v = esc_view();
    if (!v) { popup_activate(1); return; }
    if (!u_on(v)) { popup_activate(1); return; }
    if (!u_alive(u_gc_get(h_pr_root)) || u_gc_get(h_pv_view) != v) { pv_build(v); pr_build(v); }
    Obj root = u_gc_get(h_pr_root); if (!u_alive(root)) return;
    pr_shown_match = s->matches;
    char b[200];
    snprintf(b, sizeof b, "%s", s->match_winner == PVP_TEAM_BLUE ? "Blue wins the match" : s->match_winner == PVP_TEAM_RED ? "Red wins the match" : "The match is a draw");
    tmp_set(u_gc_get(h_pr_title), b);
    snprintf(b, sizeof b, "<color=#7FA8FF>%d</color>  -  <color=#FF7F7F>%d</color>", s->score[1], s->score[2]); tmp_set(u_gc_get(h_pr_score), b);
    char r[300] = ""; size_t o = 0;
    o += (size_t)snprintf(r + o, sizeof r - o, "%s: ", s->rules.best_of && pr_nhist ? "Rounds" : "Rounds");
    for (int i = 0; i < pr_nhist && o < sizeof r - 40; i++) o += (size_t)snprintf(r + o, sizeof r - o, "%s%d %s", i ? ",  " : "", i + 1, pr_hist[i] == 1 ? "Blue" : pr_hist[i] == 2 ? "Red" : "draw");
    if (s->draws) o += (size_t)snprintf(r + o, sizeof r - o, "\nDraws: %d", s->draws);
    snprintf(r + o, sizeof r - o, "\nThe world is back as it was before the match.");
    tmp_set(u_gc_get(h_pr_rounds), r);
    Obj area = u_gc_get(h_pv_area); if (u_alive(area)) u_active(area, false);
    u_active(root, true); pr_open = 1;
    rig_log("pvp ui: result window: %s", b);
}

/* ---- every frame (from mpmenu_frame) */
static void pvp_ui_tips(void) {
    if (!pv_open && !pr_open) {
        tip_try(pvb(PVB_ESC), "PvP Match", "Opens the PvP match setup: two teams of players fight each other on a dungeon floor with no monsters. The world comes back as it was after the match.");
        tip_try(pvb(PVB_READY), "Ready", "You have given your orders. The planning pause ends when every player pressed Ready, or after 5 seconds.");
        return;
    }
    if (pv_open) {
        for (int r = 0; r < PVR_N; r++) tip_try(pvt(h_pv_rrow[r]), pv_label[r], pv_tip[r]);
        tip_try(pvb(PVB_START), "Start Match", "Sends everyone to the arena floor with the teams and rules shown. The match begins by itself once everyone has arrived.");
        tip_try(pvb(PVB_CLOSE), "Close", "Closes this window. Nothing is sent.");
        tip_try(pvb(PVB_BRING), "Bring Units", "Fight with units from one of your own saves instead of this world's units. They are copies: your save stays as it is.");
        for (int i = 0; i < 8; i++) tip_try(pvb(PVB_P0 + i), "Team", "The host clicks a player to move him to the other team. Each player brings the units he owns.");
        tip_try(pvt(h_pv_teamh[1]), "Blue Team", "Starts at the arrival point of the arena.");
        tip_try(pvt(h_pv_teamh[2]), "Red Team", "Starts at the far end of the arena, by the exit portal (which does not work in a match).");
    }
    if (br_open) {
        for (int i = 0; i < br_nsaves; i++) { char tb[240]; snprintf(tb, sizeof tb, "%s\n\nA save on this computer, newest first. Click it to see its units.", br_stems[i]); tip_try(pvb(PVB_BR_SAVE0 + i), "Save", tb); }
        for (int i = 0; i < 12; i++) tip_try(pvb(PVB_BR_UNIT0 + i), "Unit", "Click to pick or unpick this unit. The dungeon team is 4 units in total, shared between the players.");
        tip_try(pvb(PVB_BR_SEND), "Bring", "Copies the picked units with their gear and sends them to the host. A new Bring replaces your last one.");
        tip_try(pvb(PVB_BR_WIKI), "Paste Wiki Link", "Uses a team link from the Dungeon Settlers wiki builder (Share > copy link): its units are built here from the game's own data and sent to the host. A new one replaces your last Bring.");
        tip_try(pvb(PVB_BR_BACK), "Back", "Back to the PvP window.");
    }
    if (pr_open) tip_try(pvb(PVB_RESULT_OK), "Back to the World", "Closes the result. The world is the same as before the match.");
}
static void pvp_ui_frame(const mpm_obs *o) {
    const pvp_state *s = pvp_ui_state ? pvp_ui_state() : NULL;
    if (!s) return;
    hud_frame(s);
    hud_click(s);
    pr_frame(s);
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
        if (eb0) { int show = ingame && s->phase == PVP_OFF && !pv_open && !pr_open; u_active(eb0, show); if (show) tmp_set(AT(eb0, 0x28, Obj), "PvP Match"); }
        if (pv_open && frames % 15 == 0) pv_refresh();
    } else { pv_open = 0; pr_open = 0; br_open = 0; }
    if (pv_click >= 0) { int k = pv_click; pv_click = -1; pv_click_do(k); }
    if (o->escape && br_open) br_show(0); else if (o->escape && pv_open) pv_show(0);
}
static char *pvp_ui_exec(const char *cmd, const char *line) {            /* tests: mpm_pvp {"action":"esc|close|start|ready|ok|player0..|arrow r d"} */
    if (strcmp(cmd, "mpm_pvp")) return NULL;
    char a[32] = ""; jstr(line, "action", a, sizeof a); double v = 0, d = 1;
    if (!strcmp(a, "esc")) pv_click = PVB_ESC;
    else if (!strcmp(a, "close")) pv_click = PVB_CLOSE;
    else if (!strcmp(a, "start")) pv_click = PVB_START;
    else if (!strcmp(a, "ready")) pv_click = PVB_READY;
    else if (!strcmp(a, "ok")) pv_click = PVB_RESULT_OK;
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
    else if (!strcmp(a, "br_wiki")) pv_click = PVB_BR_WIKI;
    else if (!strcmp(a, "br_back")) pv_click = PVB_BR_BACK;
    else if (!strcmp(a, "arrow")) { jnum(line, "row", &v); jnum(line, "dir", &d); pv_click = PVB_ARROW0 + 2 * (int)v + (d > 0); }
    out:;
    Buf b = {0};
    bput(&b, "{\"ok\":true,\"bring\":{\"open\":%d,\"saves\":[", br_open);
    for (int i = 0; i < br_nsaves; i++) { bput(&b, "%s", i ? "," : ""); bstr(&b, br_stems[i]); }
    bput(&b, "],\"save\":%d,\"units\":[", br_save);
    for (int i = 0; i < br_nunits; i++) { bput(&b, "%s[", i ? "," : ""); bstr(&b, br_unit_name[i]); bput(&b, ",\"%s\",%d,%d]", br_unit_guid[i], br_unit_lvl[i], br_pick[i]); }
    bput(&b, "],\"status\":"); bstr(&b, br_sent); bput(&b, "},");
    bput(&b, "\"window\":%d,\"result\":%d,\"esc_entry\":%d,\"hud\":%d,\"ready\":%d,\"mode\":%d,\"bo\":%d,\"secs\":%d,\"pause\":%d,\"vanilla\":%d,\"vanilla_shown\":%d,\"note\":",
         pv_open, pr_open, u_on(pvb(PVB_ESC)), u_go_active_self(u_gc_get(h_hud)), u_on(pvb(PVB_READY)), pv.mode, pv_bo[pv.bo], pv_secs[pv.secs], pv.pause,
         pv.vanilla, pv_vanilla_shown(pvp_ui_state ? pvp_ui_state() : NULL, pvp_ui_is_host && pvp_ui_is_host()));
    bstr(&b, pv_note_txt); bput(&b, ",\"tip\":");
    bstr(&b, tip_now);
    bput(&b, ",\"columns\":[");                          /* unit names shown in the team columns */
    for (int i = 0; i < pv_ncol; i++) { bput(&b, "%s", i ? "," : ""); bstr(&b, pv_col_names[i]); }
    bput(&b, "]}");
    return b.p;
}

/* ---- menu hooks (mpmenu.c calls them where this file was called before the split) */
static const mp_menu_hooks pvp_hooks = { .tips = pvp_ui_tips, .frame_late = pvp_ui_frame, .press = pvp_ui_press, .exec = pvp_ui_exec };
const mp_menu_hooks *pvp_menu_hooks(void) { return &pvp_hooks; }
