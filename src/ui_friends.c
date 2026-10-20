/* ui_friends.c - in-game Steam friends picker for the host's "Steam invite" (lobby window and Esc menu).
 *
 * Why: the modded game is not started by Steam, so the Steam overlay is not in the process and
 * ISteamFriends::ActivateGameOverlayInviteDialog shows nothing (PLAN_steam_invite.md, option A).
 * When ISteamUtils::IsOverlayEnabled says the overlay is there, the menu keeps the overlay dialog; otherwise
 * "Steam invite" opens this window: the host's Steam friends (online first, players of this game on top, then the
 * others), 8 rows a page (mouse wheel or the arrows), click a friend = ISteamMatchmaking::InviteUserToLobby
 * (mp_steam_invite_friend): the friend gets a lobby invite in Steam chat. The line under the lobby's Steam invite
 * button says the friend must open Dungeon Settlers Modded first (else Steam opens the stock game).
 *
 * Built with the menu kit (mpmenu_kit.h, the same widgets as the lobby), on the Esc menu's root like the lobby
 * window; it hides the lobby (or the Esc column) while open. Hooks: mpmenu_register_hooks (press / escape / click /
 * frame_late / exec), registered on the first open. Windows: the same code (steam_api64.dll since lane steamwin);
 * the overlay check decides there too.
 * Test: mpm_friends {"action": "state" | "open" | "row", "i": n | "next" | "prev" | "wheel", "dir": +-1 | "close"};
 * DSRIG_TEST_STEAMFRIENDS=1 in a test copy gives a fake friends list and a fake invite (mp_steam.h). */
#include "mpmenu_kit.h"
#include "mp_game.h"
#include "mp_steam.h"
#include "ds_i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int ui_friends_invite_click(void);
float ui_friends_lobby_note(float x, float y, float w, int steam);


#define UF_ROWS 8
#define UF_MAX 400
#define UF_W 560.0f
#define UF_H 640.0f
#define UF_ESC_CLICK 0x7F31                    /* private click id: Escape while the picker is open (escape -> click hook) */
enum { UFB_ROW0, UFB_PREV = UFB_ROW0 + UF_ROWS, UFB_NEXT, UFB_CLOSE, UFB_N };

static uintptr_t h_root, h_view, h_area, h_b[UFB_N], h_rname[UF_ROWS], h_rstate[UF_ROWS], h_title, h_hint, h_page, h_status, h_empty;
static uintptr_t h_note, h_note_root;          /* the line under the lobby's Steam invite button */
static int uf_open, uf_click = -1, uf_page, uf_n, uf_registered, uf_hid_area;
static dsnet_steam_friend uf_list[UF_MAX];
static uint8_t uf_sent[UF_MAX];                /* 1 = invite sent this time the window is open, 2 = failed */
static char uf_status[200], uf_status_kind;    /* 'o' sent, 'e' error */

static Obj uo(uintptr_t h) { Obj o = u_gc_get(h); return u_alive(o) ? o : NULL; }
#define KEEP(hh, o) { Obj o_ = (o); u_gc_free(hh); hh = o_ ? u_gc_new(o_) : 0; }

static int rank(const dsnet_steam_friend *f) {          /* 0 plays this game, 1 online, 2 busy / away / snooze, 3 offline */
    if (f->state == 0) return 3;
    if (f->in_game) return 0;
    return f->state == 2 || f->state == 3 || f->state == 4 ? 2 : 1;
}
static int cmp_friend(const void *a, const void *b) {
    const dsnet_steam_friend *x = a, *y = b;
    int d = rank(x) - rank(y);
    return d ? d : strcasecmp(x->name, y->name);
}
static void uf_load(void) {
    uf_n = mp_steam_friends(uf_list, UF_MAX);
    if (uf_n < 0) uf_n = 0;
    qsort(uf_list, uf_n, sizeof *uf_list, cmp_friend);
    memset(uf_sent, 0, sizeof uf_sent);
    int on = 0; for (int i = 0; i < uf_n; i++) on += uf_list[i].state != 0;
    rig_log("ui_friends: %d Steam friend(s), %d online", uf_n, on);
}
static int npages(void) { return uf_n ? (uf_n + UF_ROWS - 1) / UF_ROWS : 1; }

static void uf_build(Obj v) {
    if (!scan_done) assets_scan_now();
    Obj src = AT(v, 0x70, Obj);                                             /* Report bug: button template (as the lobby) */
    if (!u_alive(src)) return;
    Obj tpl_img = first_image(u_tr(src), 0), tpl_txt = AT(src, 0x28, Obj);
    #define PX(x) ((x) - UF_W / 2)
    #define PY(y) (UF_H / 2 - (y))
    Obj root = mk_img(tpl_img, u_tr(v), -1, (Col){ 0, 0, 0, 0 }, 0, 0, UF_W, UF_H);
    if (!root) { rig_log("ui_friends: no frame"); return; }
    Obj R = u_tr(root);
    KEEP(h_root, root); KEEP(h_view, v); KEEP(h_area, u_tr_parent(u_tr_parent(u_tr(src))));
    mk_img(tpl_img, R, S_BOX, C_WHITE, 0, 0, UF_W, UF_H);
    mk_img(tpl_img, R, -1, (Col){ 0.133f, 0.125f, 0.145f, 1 }, 0, PY((64 + UF_H - 64) / 2), UF_W - 4, UF_H - 128);   /* body #222025 */
    mk_img(tpl_img, R, -1, C_BAND, 0, PY(32), UF_W - 4, 62);
    mk_img(tpl_img, R, -1, C_COPPER, 0, PY(63), UF_W - 4, 1);
    KEEP(h_title, mk_txt(tpl_txt, R, 1, 26, 514, 0, 0, PY(32), UF_W - 40, 48, C_MAIN));
    mk_img(tpl_img, R, -1, C_ROW1, 0, PY(86), UF_W - 4, 44);
    KEEP(h_hint, mk_txt(tpl_txt, R, 0, 14, 514, 1, 0, PY(86), UF_W - 40, 40, C_SUB));
    for (int i = 0; i < UF_ROWS; i++) {
        float yc = PY(130 + 46 * i);
        Obj b = mk_button(src, R, 1, S_NBTN, C_WHITE, 0, yc, UF_W - 48, 42, 16, "", C_MAIN);
        KEEP(h_b[UFB_ROW0 + i], b);
        if (!b) continue;
        KEEP(h_rname[i], mk_txt(tpl_txt, u_tr(b), 0, 16, 513, 0, -(UF_W - 48) / 2 + 16 + 160, 0, 320, 30, C_MAIN));
        KEEP(h_rstate[i], mk_txt(tpl_txt, u_tr(b), 0, 13, 516, 0, (UF_W - 48) / 2 - 16 - 85, 0, 170, 26, C_SUB));
    }
    KEEP(h_empty, mk_txt(tpl_txt, R, 0, 16, 514, 1, 0, PY(220), UF_W - 80, 80, C_SUB));
    float yp = PY(130 + 46 * UF_ROWS - 4);
    KEEP(h_b[UFB_PREV], mk_button(src, R, 1, S_NBTN, C_WHITE, PX(24 + 20), yp, 40, 34, 18, "\xE2\x80\xB9", C_MAIN));
    KEEP(h_b[UFB_NEXT], mk_button(src, R, 1, S_NBTN, C_WHITE, PX(UF_W - 24 - 20), yp, 40, 34, 18, "\xE2\x80\xBA", C_MAIN));
    KEEP(h_page, mk_txt(tpl_txt, R, 0, 14, 514, 0, 0, yp, 300, 28, C_SUB));
    KEEP(h_status, mk_txt(tpl_txt, R, 0, 15, 514, 1, 0, PY(UF_H - 88), UF_W - 48, 40, C_GOLD));
    mk_img(tpl_img, R, -1, C_BAND, 0, PY(UF_H - 32), UF_W - 4, 60);
    mk_img(tpl_img, R, -1, C_COPPER, 0, PY(UF_H - 62), UF_W - 4, 1);
    KEEP(h_b[UFB_CLOSE], mk_button(src, R, 1, S_NBTN, C_WHITE, 0, PY(UF_H - 32), 180, 34, 16, TR("Close"), C_MAIN));
    u_active(root, false);
    #undef PX
    #undef PY
    rig_log("ui_friends: window built");
}

static const char *state_text(const dsnet_steam_friend *f, Col *c) {
    static const Col on = { 0.553f, 0.796f, 0.475f, 1 }, off = { 0.47f, 0.44f, 0.43f, 1 };
    if (f->state == 0) { *c = off; return TR("Offline"); }
    if (f->in_game) { *c = C_GOLD; return TR("In Dungeon Settlers"); }
    if (f->state == 2) { *c = C_SUB; return TR("Busy"); }
    if (f->state == 3 || f->state == 4) { *c = C_SUB; return TR("Away"); }
    *c = on; return TR("Online");
}
static void uf_refresh(void) {
    tmp_set(uo(h_title), TR("Invite a Steam friend"));
    tmp_set(uo(h_hint), TR("Your friend must open Dungeon Settlers Modded first."));
    if (uf_page >= npages()) uf_page = npages() - 1;
    if (uf_page < 0) uf_page = 0;
    for (int i = 0; i < UF_ROWS; i++) {
        int k = uf_page * UF_ROWS + i;
        Obj b = uo(h_b[UFB_ROW0 + i]);
        u_active(b, k < uf_n);
        if (k >= uf_n || !b) continue;
        char nm[40]; snprintf(nm, sizeof nm, "%s", uf_list[k].name[0] ? uf_list[k].name : "?"); dsi_utf8_fix_end(nm);
        tmp_set(uo(h_rname[i]), nm);
        Col c; const char *st = state_text(&uf_list[k], &c);
        if (uf_sent[k] == 1) { st = TR("Invite sent"); c = C_GOLD; }
        else if (uf_sent[k] == 2) { st = TR("Not sent"); c = C_ORANGE; }
        tmp_set(uo(h_rstate[i]), st); txt_color(uo(h_rstate[i]), c);
        txt_color(uo(h_rname[i]), uf_list[k].state == 0 ? C_SUB : C_MAIN);
    }
    mp_steam_info in; mp_steam_state(&in);
    Obj em = uo(h_empty);
    u_active(em, uf_n == 0);
    if (!uf_n) tmp_set(em, in.steam ? TR("No Steam friends found.") : TR("Steam is not running in this game, so no friends can be shown."));
    char pg[64] = "";
    if (uf_n) snprintf(pg, sizeof pg, TR("%d-%d of %d"), uf_page * UF_ROWS + 1, uf_page * UF_ROWS + UF_ROWS < uf_n ? uf_page * UF_ROWS + UF_ROWS : uf_n, uf_n);
    tmp_set(uo(h_page), pg);
    u_active(uo(h_b[UFB_PREV]), uf_page > 0);
    u_active(uo(h_b[UFB_NEXT]), uf_page + 1 < npages());
    tmp_set(uo(h_status), uf_status); txt_color(uo(h_status), uf_status_kind == 'e' ? C_ORANGE : C_GOLD);
}

static void uf_show(int on) {
    Obj root = uo(h_root);
    if (!root) return;
    u_active(root, on);
    if (!on) {
        Obj lw = u_gc_get(h_lw_root); if (u_alive(lw) && (M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY)) u_active(lw, true);
        if (uf_hid_area) { Obj a = uo(h_area); if (a) u_active(a, true); uf_hid_area = 0; }
    }
    if (on != uf_open) rig_log("ui_friends: picker %s", on ? "open" : "closed");
    uf_open = on;
    if (on) { uf_page = 0; uf_status[0] = 0; uf_status_kind = 0; uf_load(); uf_refresh(); }
}

static void uf_invite(int k) {
    if (k < 0 || k >= uf_n) return;
    const dsnet_steam_friend *f = &uf_list[k];
    mp_steam_info in; mp_steam_state(&in);
    char nm[72]; snprintf(nm, sizeof nm, "%s", f->name); dsi_utf8_fix_end(nm);
    if (!in.lobby && !mp_steam_test_friends()) {
        snprintf(uf_status, sizeof uf_status, "%s", TR("The Steam lobby is not ready yet. Wait a moment, then try again."));
        uf_status_kind = 'e'; rig_log("ui_friends: invite: no Steam lobby yet");
    } else {
        int rc = mp_steam_invite_friend(f->steamid);
        rig_log("ui_friends: invite to row %d: rc %d", k, rc);
        if (rc == 0) { snprintf(uf_status, sizeof uf_status, TR("Invite sent to %s."), nm); uf_status_kind = 'o'; uf_sent[k] = 1; }
        else { snprintf(uf_status, sizeof uf_status, TR("Steam did not send the invite to %s. Try again, or use the direct address."), nm); uf_status_kind = 'e'; uf_sent[k] = 2; }
    }
    dsi_utf8_fix_end(uf_status);
    uf_refresh();
}

static void uf_do(int k) {
    if (k >= UFB_ROW0 && k < UFB_ROW0 + UF_ROWS) uf_invite(uf_page * UF_ROWS + k - UFB_ROW0);
    else if (k == UFB_PREV && uf_page > 0) { uf_page--; uf_refresh(); }
    else if (k == UFB_NEXT && uf_page + 1 < npages()) { uf_page++; uf_refresh(); }
    else if (k == UFB_CLOSE) uf_show(0);
}

static int uf_press(Obj btn) {
    if (!uf_open || !btn) return 0;
    for (int k = 0; k < UFB_N; k++) if (h_b[k] && btn == u_gc_get(h_b[k])) { uf_click = k; return 1; }
    return 0;
}
static int uf_escape(void) { return uf_open ? UF_ESC_CLICK : 0; }
static int uf_click_hook(int c) { if (c != UF_ESC_CLICK) return 0; uf_show(0); return 1; }

static void uf_wheel(void) {
    static void (*wheel)(V2 *); static int looked;
    if (!looked) { looked = 1; void *(*ri)(const char *) = F(0x6F8B90, void *(*)(const char *));
                   wheel = (void (*)(V2 *))ri("UnityEngine.Input::get_mouseScrollDelta_Injected(UnityEngine.Vector2&)"); }
    if (!wheel) return;
    V2 d = { 0, 0 }; wheel(&d);
    if (d.y < 0 && uf_page + 1 < npages()) { uf_page++; uf_refresh(); }
    else if (d.y > 0 && uf_page > 0) { uf_page--; uf_refresh(); }
}

static void uf_frame(const mpm_obs *o) {
    if (!uf_open) return;
    Obj v = esc_view();
    int host = o->role == 1 && mp_game_session() != NULL;
    if (!v || !u_on(v) || u_gc_get(h_view) != v || !uo(h_root) || !host) { uf_show(0); return; }
    if (uf_click >= 0) { int k = uf_click; uf_click = -1; uf_do(k); }
    if (!uf_open) return;
    uf_wheel();
    Obj lw = u_gc_get(h_lw_root); if (u_alive(lw) && u_on(lw)) u_active(lw, false);       /* the lobby steps aside (as Bring Units) */
    Obj a = uo(h_area); if (a && u_on(a)) { u_active(a, false); uf_hid_area = 1; }          /* Esc menu: its column too */
    if (frames % 600 == 0) { uf_load(); uf_refresh(); }                                     /* states change: every ~10 s */
}

static char *uf_exec(const char *cmd, const char *line) {
    if (strcmp(cmd, "mpm_friends")) return NULL;
    char a[16] = ""; jstr(line, "action", a, sizeof a); double v = 0;
    if (!strcmp(a, "open")) ui_friends_invite_click();
    else if (!strcmp(a, "row") && jnum(line, "i", &v)) {                  /* the real path: OnPressButton of that row's clone */
        Obj b = (int)v >= 0 && (int)v < UF_ROWS ? uo(h_b[UFB_ROW0 + (int)v]) : NULL;
        if (!b || !u_on(b)) return strdup("{\"ok\":false,\"error\":\"row not shown\"}");
        if (mm_is(b, K_SMB)) F(0xF2DC30, void (*)(Obj, void *))(b, NULL);
    }
    else if (!strcmp(a, "next") || !strcmp(a, "prev") || !strcmp(a, "close")) {
        Obj b = uo(h_b[a[0] == 'n' ? UFB_NEXT : a[0] == 'p' ? UFB_PREV : UFB_CLOSE]);
        if (!b || !u_on(b)) return strdup("{\"ok\":false,\"error\":\"button not shown\"}");
        if (mm_is(b, K_SMB)) F(0xF2DC30, void (*)(Obj, void *))(b, NULL);
    }
    Buf b = {0};
    bput(&b, "{\"ok\":true,\"open\":%d,\"overlay\":%d,\"page\":%d,\"pages\":%d,\"n\":%d,\"rows\":[", uf_open, mp_steam_overlay_enabled(), uf_page, npages(), uf_n);
    for (int i = 0; uf_open && i < UF_ROWS && uf_page * UF_ROWS + i < uf_n; i++) {
        int k = uf_page * UF_ROWS + i; Col c;
        bput(&b, "%s{\"name\":", i ? "," : ""); bstr(&b, uf_list[k].name); bput(&b, ",\"state\":");
        bstr(&b, uf_sent[k] == 1 ? TR("Invite sent") : uf_sent[k] == 2 ? TR("Not sent") : state_text(&uf_list[k], &c)); bput(&b, ",\"shown\":%d}", u_on(uo(h_b[UFB_ROW0 + i])));
    }
    char nt[200] = ""; Obj note = uo(h_note); if (note && u_on(note)) snprintf(nt, sizeof nt, "%s", TR("Your friend must open Dungeon Settlers Modded first."));
    bput(&b, "],\"status\":"); bstr(&b, uf_status); bput(&b, ",\"lobby_note\":"); bstr(&b, nt);
    bput(&b, ",\"lobby_hidden\":%d}", uf_open && !u_on(u_gc_get(h_lw_root)));
    return b.p;
}

static const mp_menu_hooks uf_hooks = { .press = uf_press, .escape = uf_escape, .click = uf_click_hook, .frame_late = uf_frame, .exec = uf_exec };
static void uf_register(void) { if (!uf_registered) { uf_registered = 1; mpmenu_register_hooks(&uf_hooks); } }

/* mpmenu.c MPA_INVITE: 1 = the picker took the click (overlay off), 0 = the menu opens the overlay dialog as before. */
int ui_friends_invite_click(void) {
    uf_register();
    int ov = mp_steam_overlay_enabled();
    rig_log("ui_friends: Steam invite clicked, overlay %d", ov);
    if (ov == 1) return 0;
    Obj v = esc_view();
    if (!v || !u_on(v)) { rig_log("ui_friends: no Esc view"); return 0; }
    if (u_gc_get(h_view) != v || !uo(h_root)) uf_build(v);
    if (!uo(h_root)) return 0;
    uf_show(1);
    return 1;
}

/* mpmenu.c lobby layout (host): the line under the Steam invite button; returns the height it takes (0 = none). */
float ui_friends_lobby_note(float x, float y, float w, int steam) {
    uf_register();                                   /* the lobby shows first: the test command works from here on */
    Obj root = u_gc_get(h_lw_root);
    if (!u_alive(root)) return 0;
    if (!steam) { Obj n = uo(h_note); if (n) u_active(n, false); return 0; }
    if (u_gc_get(h_note_root) != root || !uo(h_note)) {
        Obj v = esc_view(), src = v ? AT(v, 0x70, Obj) : NULL;
        if (!u_alive(src)) return 0;
        KEEP(h_note, mk_txt(AT(src, 0x28, Obj), u_tr(root), 0, 13, 514, 0, x, y, w, 20, C_SUB));
        KEEP(h_note_root, root);
    }
    Obj n = uo(h_note);
    if (!n) return 0;
    place(n, x, y, w, 20); u_active(n, true);
    tmp_set(n, TR("Your friend must open Dungeon Settlers Modded first."));
    return 22;
}
