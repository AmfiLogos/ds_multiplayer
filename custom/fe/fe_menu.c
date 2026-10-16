/* Foreign Expedition windows (Custom Modes, custom-modes-mp.dylib). Before the M4 split this file was included by mpmenu.c;
 * it now uses the menu's exported helpers and click registries (src/mpmenu_kit.h) and is called through the menu hooks
 * (fe_menu_hooks, registered by custom/cm_mp.c). Boards:
 *   FEBringA          Bring Units: your saves on the left, unit cards on the right (lobby, every player)
 *   FEResultsA / B    Back from the Expedition: one row per unit, dungeon loot, what will be applied, Bring home to
 *   FEResultsApplied  Expedition Results list (Multiplayer page) and the "Already Brought Home" refusal
 * One window at a time, 960 x 620, on the lobby's parent (Esc menu root) in a session or on the title view. Buttons are
 * registered by id in h_lwr (Esc menu clones) or h_tb (title clones); the click hooks queue the id (pending_click). */
#include "mpmenu_kit.h"
#include "mp.h"
#include "mp_steam.h"
#include "fe_ui_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { FEW_NONE, FEW_BRING, FEW_RESULTS, FEW_LIST };
enum { FT_TITLE, FT_SUB, FT_RULES, FT_LHEAD, FT_RHEAD, FT_COUNT, FT_NOTE, FT_TABLE, FT_LOOT, FT_APPLY, FT_TARGET, FT_CONFLICT, FT_MSG, FT_N };
#define FEW_W 960.0f
#define FEW_H 620.0f
#define WX(x) ((x) - FEW_W / 2)
#define WY(y) (FEW_H / 2 - (y))
static struct {
    int mode, esc, page, upage, sel_save, nsave, nunit, picked[64], npicked, ntarget, target, choice, applied, conflict, nres;
    uintptr_t root, view, t[FT_N], rowt[8][3], cardt[8][4];
    fe_save_info saves[64]; fe_unit_info units[64]; fe_save_info targets[16]; fe_result_info res[16];
    char run[40], msg[400], err[300];
} W;
static Obj few_t(int k) { Obj o = u_gc_get(W.t[k]); return u_alive(o) ? o : NULL; }
static uintptr_t *few_reg(void) { return W.esc ? h_lwr : h_tb; }
static Obj few_btn(int id) { Obj o = u_gc_get(few_reg()[id]); return u_alive(o) ? o : NULL; }
static void few_txt(const char *key, char *out, size_t cap) {             /* TextKeyExtensions.GetText (the game's language) */
    out[0] = 0;
    if (!key || !key[0]) return;
    Obj s = F(0x80EBAC, Obj (*)(Obj, void *))(u_str(key), NULL);
    if (s) utf16_to_utf8(s, out, cap);
    if (!out[0] || !strcmp(out, key)) snprintf(out, cap, "%s", key);
}
static void few_item_name(const char *key, char *out, size_t cap) { char k[128]; snprintf(k, sizeof k, "TEXTKEY_%s_NAME", key); few_txt(k, out, cap); }
static void few_rules_line(const fe_mode_info *m, char *out, size_t cap) {
    snprintf(out, cap, "%s%s hub rules:  <color=#D9BE8F>%s  \xC2\xB7  %s  \xC2\xB7  %s  \xC2\xB7  %d units each</color>", m->host[0] ? m->host : "The host", m->host[0] ? "'s" : "",
             m->death ? "Death is permanent" : "Death is not permanent", m->xp ? "XP counts" : "XP does not count",
             m->loot ? "Dungeon loot comes back" : "Loot stays in the hub", m->units > 0 ? m->units : 3);
}
static void few_build(Obj parent, Obj src, int esc, Obj tpl_img, Obj tpl_txt) {
    Obj root = mk_img(tpl_img, parent, S_BOX, C_WHITE, 0, 0, FEW_W, FEW_H);
    if (!root) return;
    Obj R = u_tr(root);
    u_gc_free(W.root); W.root = u_gc_new(root);
    W.esc = esc;
    uintptr_t *reg = few_reg();
    #define T(k, cz, sz, al, wr, x, y, w, h, c) { Obj t_ = mk_txt(tpl_txt, R, cz, sz, al, wr, x, y, w, h, c); u_gc_free(W.t[k]); W.t[k] = t_ ? u_gc_new(t_) : 0; }
    #define BTN(id, spr_, x, y, w, h, fs, lab, col) { Obj b_ = mk_button(src, R, esc, spr_, C_WHITE, x, y, w, h, fs, lab, col); u_gc_free(reg[id]); reg[id] = b_ ? u_gc_new(b_) : 0; }
    mk_img(tpl_img, R, -1, C_BAND, 0, WY(32), FEW_W - 4, 60);
    mk_img(tpl_img, R, -1, C_COPPER, 0, WY(62), FEW_W - 4, 1);
    T(FT_TITLE, 1, 30, 514, 0, 0, WY(32), 900, 50, C_MAIN);
    T(FT_SUB, 0, 15, 514, 1, 0, WY(82), 900, 26, C_SUB);
    T(FT_RULES, 0, 15, 514, 0, 0, WY(108), 900, 24, C_MAIN);
    T(FT_LHEAD, 1, 18, 513, 0, WX(24 + 150), WY(138), 300, 26, C_GOLD);
    T(FT_RHEAD, 1, 18, 513, 0, WX(344 + 290), WY(138), 580, 26, C_GOLD);
    for (int i = 0; i < 6; i++) {                                           /* left column: saves (Bring) / results rows (List) */
        float y = WY(156 + 56 * i + 26);
        BTN(MPB_FE_SAVE0 + i, S_NBTN, WX(24 + 150), y, 300, 52, 14, "", C_MAIN);
        Obj b = u_gc_get(reg[MPB_FE_SAVE0 + i]);
        for (int k = 0; b && k < 2; k++) { Obj t = mk_txt(tpl_txt, u_tr(b), 0, k ? 12 : 16, 513, 0, 4, k ? -11 : 9, 280, 22, k ? C_SUB : C_MAIN); u_gc_free(W.rowt[i][k]); W.rowt[i][k] = t ? u_gc_new(t) : 0; }
    }
    BTN(MPB_FE_SAVEPG, S_NBTN, WX(24 + 150), WY(156 + 56 * 6 + 14), 300, 26, 13, "More saves \xE2\x80\xBA", C_SUB);
    for (int i = 0; i < 8; i++) {                                           /* right column: unit cards (2 x 4) */
        float x = WX(344 + 290 * (i % 2) + 142), y = WY(156 + 66 * (i / 2) + 31);
        BTN(MPB_FE_UNIT0 + i, S_NBTN, x, y, 284, 62, 14, "", C_MAIN);
        Obj b = u_gc_get(reg[MPB_FE_UNIT0 + i]);
        static const float ys[4] = { 15, -2, -18, 15 };
        for (int k = 0; b && k < 4; k++) {
            Obj t = mk_txt(tpl_txt, u_tr(b), 0, k == 0 ? 15 : k == 3 ? 18 : 12, k == 3 ? 514 : 513, 0, k == 3 ? -126 : 18, ys[k], k == 3 ? 24 : 240, 20, k == 3 ? C_GOLD : k == 0 ? C_MAIN : C_SUB);
            u_gc_free(W.cardt[i][k]); W.cardt[i][k] = t ? u_gc_new(t) : 0;
        }
    }
    BTN(MPB_FE_UNITPG, S_NBTN, WX(344 + 100), WY(156 + 66 * 4 + 14), 200, 26, 13, "More units \xE2\x80\xBA", C_SUB);
    T(FT_COUNT, 0, 15, 516, 0, WX(936 - 150), WY(156 + 66 * 4 + 14), 300, 24, C_GOLD);
    T(FT_NOTE, 0, 13, 513, 1, 0, WY(486), 900, 66, C_SUB);
    /* results: table, loot, what will be applied, target, conflict */
    T(FT_TABLE, 0, 15, 513, 1, 0, WY(140 + 105), 900, 210, C_MAIN);
    T(FT_LOOT, 0, 13, 513, 1, 0, WY(372), 900, 40, C_SUB);
    T(FT_APPLY, 0, 13, 513, 1, 0, WY(430), 900, 64, C_MAIN);
    T(FT_TARGET, 0, 15, 514, 0, 0, WY(486), 520, 26, C_MAIN);
    BTN(MPB_FE_TGT_PREV, S_NBTN, WX(480 - 280), WY(486), 28, 28, 16, "\xE2\x80\xB9", C_MAIN);
    BTN(MPB_FE_TGT_NEXT, S_NBTN, WX(480 + 280), WY(486), 28, 28, 16, "\xE2\x80\xBA", C_MAIN);
    T(FT_CONFLICT, 0, 13, 513, 1, WX(24 + 300), WY(522), 600, 34, C_ORANGE);
    BTN(MPB_FE_VER_EXP, S_NBTN, WX(700), WY(522), 170, 30, 13, "Expedition version", C_MAIN);
    BTN(MPB_FE_VER_HOME, S_NBTN, WX(880), WY(522), 150, 30, 13, "Home version", C_MAIN);
    for (int i = 0; i < 6; i++) BTN(MPB_FE_RES0 + i, S_NBTN, 0, WY(160 + 54 * i + 25), 900, 50, 15, "", C_MAIN);
    T(FT_MSG, 0, 15, 514, 1, 0, WY(300), 820, 160, C_MAIN);
    /* bottom bar */
    mk_img(tpl_img, R, -1, C_BAND, 0, WY(FEW_H - 34), FEW_W - 4, 64);
    mk_img(tpl_img, R, -1, C_COPPER, 0, WY(FEW_H - 66), FEW_W - 4, 1);
    BTN(MPB_FE_CANCEL, S_NBTN, WX(24 + 100), WY(FEW_H - 34), 200, 36, 16, "Cancel", C_MAIN);
    BTN(MPB_FE_OK, S_RECRUIT, WX(936 - 130), WY(FEW_H - 34), 260, 44, 17, "", C_START);
    #undef T
    #undef BTN
    u_active(root, false);
    rig_log("fe: window built (%s)", esc ? "Esc menu root" : "title");
}
static void few_label(int id, const char *s) { Obj b = few_btn(id); if (b) tmp_set(btn_label(b, W.esc), s); }
static void few_show(int id, int on) { Obj b = few_btn(id); if (b) u_active(b, on); }
static void few_set(int k, const char *s) { Obj t = few_t(k); if (t) { u_active(t, s && s[0]); tmp_set(t, s ? s : ""); } }
static void few_hide_all(void) {
    for (int id = MPB_FE_SAVE0; id <= MPB_FE_RES5; id++) few_show(id, 0);
    for (int k = FT_LHEAD; k < FT_N; k++) few_set(k, "");
}
/* ---- data */
static void few_load_units(void) {
    W.nunit = W.sel_save >= 0 && W.sel_save < W.nsave ? fe_list_units(W.saves[W.sel_save].path, W.units, 64) : 0;
    W.npicked = 0; W.upage = 0;
}
static void few_open(int mode, const char *run) {
    W.mode = mode; W.err[0] = W.msg[0] = 0; W.page = W.upage = 0; W.applied = 0; W.conflict = 0; W.choice = 0;
    if (mode == FEW_BRING) {
        W.nsave = fe_list_saves(W.saves, 64); W.sel_save = -1;
        for (int i = 0; i < W.nsave && W.sel_save < 0; i++) if (W.saves[i].ok) W.sel_save = i;
        few_load_units();
    }
    if (mode == FEW_RESULTS) { snprintf(W.run, sizeof W.run, "%s", run ? run : ""); W.ntarget = fe_result_targets(W.run, W.targets, 16); W.target = 0; }
    if (mode == FEW_LIST) W.nres = fe_list_results(W.res, 16);
    rig_log("fe: window %d opened%s%s", mode, run ? " run " : "", run ? run : "");
}
static int few_version_ok(const fe_save_info *s, const fe_mode_info *m) { return !m->version[0] || !strcmp(s->version, m->version); }
/* ---- per frame */
static void few_sync(void) {
    Obj root = u_gc_get(W.root);
    if (!u_alive(root)) return;
    u_active(root, W.mode != FEW_NONE);
    if (W.mode == FEW_NONE) return;
    few_hide_all();
    fe_mode_info m; fe_mode_get(&m);
    char b[1600], r[400];
    few_show(MPB_FE_CANCEL, 1); few_show(MPB_FE_OK, 1);
    if (W.mode == FEW_BRING) {
        few_set(FT_TITLE, "Bring Units");
        few_set(FT_SUB, "Pick one of your saves, then the units to bring. Copies travel: your units stay at home and keep working.");
        few_rules_line(&m, r, sizeof r); few_set(FT_RULES, r);
        few_set(FT_LHEAD, "Your Saves");
        for (int i = 0; i < 6; i++) {
            int k = W.page * 6 + i; few_show(MPB_FE_SAVE0 + i, k < W.nsave);
            if (k >= W.nsave) continue;
            fe_save_info *s = &W.saves[k]; int ok = s->ok && few_version_ok(s, &m);
            snprintf(b, sizeof b, "%s%s  <size=80%%><color=#A69085>Day %d</color></size>", k == W.sel_save ? "<color=#D9BE8F>" : "", s->clan, s->day);
            tmp_set(u_gc_get(W.rowt[i][0]), b);
            if (!s->ok) snprintf(b, sizeof b, "<color=#A69085>In a dungeon: load it and return to the settlement first.</color>");
            else if (!ok) snprintf(b, sizeof b, "<color=#F16071>Made with %s. Load and save it once in this build first.</color>", s->version);
            else snprintf(b, sizeof b, "%s \xC2\xB7 %d units", s->folder, s->units);
            tmp_set(u_gc_get(W.rowt[i][1]), b);
        }
        few_show(MPB_FE_SAVEPG, W.nsave > 6);
        if (W.sel_save >= 0) {
            snprintf(b, sizeof b, "Units in %s", W.saves[W.sel_save].clan); few_set(FT_RHEAD, b);
            for (int i = 0; i < 8; i++) {
                int k = W.upage * 8 + i; few_show(MPB_FE_UNIT0 + i, k < W.nunit);
                if (k >= W.nunit) continue;
                fe_unit_info *u = &W.units[k]; char nm[64], gear[200] = ""; size_t o = 0;
                few_txt(u->name_key, nm, sizeof nm);
                int on = 0; for (int p = 0; p < W.npicked; p++) if (W.picked[p] == k) on = 1;
                snprintf(b, sizeof b, "%s  <size=80%%><color=#A69085>Level %d</color></size>", nm, u->level); tmp_set(u_gc_get(W.cardt[i][0]), b);
                tmp_set(u_gc_get(W.cardt[i][1]), u->trees[0] ? u->trees : "No skill tree yet");
                for (int g = 0; g < u->ngear && o < sizeof gear - 40; g++) { char in[64]; few_item_name(u->gear_keys[g], in, sizeof in); o += (size_t)snprintf(gear + o, sizeof gear - o, "%s%s", g ? ", " : "", in); }
                tmp_set(u_gc_get(W.cardt[i][2]), gear[0] ? gear : "No gear");
                tmp_set(u_gc_get(W.cardt[i][3]), on ? "\xE2\x9C\x93" : "");
            }
            few_show(MPB_FE_UNITPG, W.nunit > 8);
        } else few_set(FT_RHEAD, "No save at the settlement");
        int cap = m.units > 0 ? m.units : 3;
        snprintf(b, sizeof b, "%d / %d picked", W.npicked, cap); few_set(FT_COUNT, b);
        snprintf(b, sizeof b, "<color=#CCBEB8>At home:</color> your units stay and keep working. Nothing changes until you bring the results home.   "
                 "<color=#CCBEB8>In the hub:</color> their copies play. %s   <color=#CCBEB8>If a copy dies:</color> %s%s%s",
                 m.xp ? "XP, levels and dungeon loot can come back." : "Experience does not count here.",
                 m.death ? "death is permanent here: that unit and its gear are removed at home." : "it comes home as it was at the last hub save.",
                 W.err[0] ? "\n<color=#F16071>" : "", W.err[0] ? W.err : "");
        if (W.err[0]) { size_t l = strlen(b); snprintf(b + l, sizeof b - l, "</color>"); }
        few_set(FT_NOTE, b);
        few_label(MPB_FE_CANCEL, "Cancel");
        few_label(MPB_FE_OK, W.npicked ? "Bring these units" : "<color=#A69085>Bring these units</color>");
    }
    if (W.mode == FEW_LIST) {
        few_set(FT_TITLE, "Expedition Results");
        few_set(FT_SUB, "Each result can be brought home once.");
        if (W.msg[0]) { few_set(FT_MSG, W.msg); few_label(MPB_FE_CANCEL, "Back"); few_show(MPB_FE_OK, 0); return; }
        for (int i = 0; i < 6; i++) {
            few_show(MPB_FE_RES0 + i, i < W.nres);
            if (i >= W.nres) continue;
            fe_result_info *x = &W.res[i];
            if (x->applied) snprintf(b, sizeof b, "%s's hub  <size=80%%><color=#A69085>%s \xC2\xB7 %d units</color></size>  \xC2\xB7  <color=#A69085>Brought home %s</color>", x->host, x->date, x->units, x->applied_date);
            else snprintf(b, sizeof b, "%s's hub  <size=80%%><color=#A69085>%s \xC2\xB7 %d units</color></size>  \xC2\xB7  <color=#8FD18F>Ready to bring home</color>", x->host, x->date, x->units);
            few_label(MPB_FE_RES0 + i, b);
        }
        few_label(MPB_FE_CANCEL, "Back"); few_show(MPB_FE_OK, 0);
    }
    if (W.mode == FEW_RESULTS) {
        fe_res_row rows[8]; fe_res_item items[40]; int ni = 0; fe_mode_info rr;
        int n = fe_result_rows(W.run, rows, 8, items, 40, &ni, &rr);
        few_set(FT_TITLE, "Back from the Expedition");
        snprintf(b, sizeof b, "%s ended the expedition. Here is what your units bring home.", rr.host[0] ? rr.host : "The host"); few_set(FT_SUB, b);
        snprintf(r, sizeof r, "Rules:  <color=#D9BE8F>%s  \xC2\xB7  %s  \xC2\xB7  %s</color>", rr.death ? "death is permanent" : "death is not permanent",
                 rr.xp ? "XP counts" : "XP does not count", rr.loot ? "dungeon loot comes back" : "loot stays in the hub");
        few_set(FT_RULES, r);
        if (W.applied) {
            snprintf(b, sizeof b, "<size=130%%>Brought home</size>\n\nSaved as \"%s\". Load it from Load campaign.\nYour old save is not changed, and a backup copy was made first.", W.msg);
            few_set(FT_MSG, b); few_show(MPB_FE_CANCEL, 0); few_label(MPB_FE_OK, "OK"); return;
        }
        size_t o = (size_t)snprintf(b, sizeof b, "<color=#A69085>Unit<pos=30%%>Level<pos=52%%>XP<pos=70%%>Status</color>");
        char ap[900] = ""; size_t ao = 0;
        for (int i = 0; i < n && o < sizeof b - 200; i++) {
            char nm[64]; few_txt(rows[i].name_key, nm, sizeof nm);
            char lv[40], xp[40], stt[120];
            if (rows[i].lv1 != rows[i].lv0) snprintf(lv, sizeof lv, "%.0f \xE2\x86\x92 <color=#D9BE8F>%.0f</color>", rows[i].lv0, rows[i].lv1); else snprintf(lv, sizeof lv, "%.0f", rows[i].lv0);
            if (!rr.xp) snprintf(xp, sizeof xp, "<color=#A69085>does not count</color>");
            else if (rows[i].lv1 == rows[i].lv0 && rows[i].exp1 > rows[i].exp0) snprintf(xp, sizeof xp, "+%.0f XP", rows[i].exp1 - rows[i].exp0);
            else if (rows[i].lv1 > rows[i].lv0) snprintf(xp, sizeof xp, "+%.0f levels", rows[i].lv1 - rows[i].lv0);
            else snprintf(xp, sizeof xp, "<color=#A69085>none</color>");
            if (!rows[i].dead) snprintf(stt, sizeof stt, "<color=#8FD18F>Alive</color>");
            else if (rows[i].removed) snprintf(stt, sizeof stt, "<color=#F16071>Died \xC2\xB7 removed at home, with its gear</color>");
            else snprintf(stt, sizeof stt, "<color=#EA9F3D>Died \xC2\xB7 home as at the last hub save</color>");
            o += (size_t)snprintf(b + o, sizeof b - o, "\n%s<pos=30%%>%s<pos=52%%>%s<pos=70%%>%s", nm, lv, xp, stt);
            if (ao < sizeof ap - 160) {
                if (rows[i].removed) ao += (size_t)snprintf(ap + ao, sizeof ap - ao, "%s: removed from your save, with its gear.  ", nm);
                else if (rows[i].dead && !rows[i].lv1) ao += (size_t)snprintf(ap + ao, sizeof ap - ao, "%s: comes home as before.  ", nm);
                else if (rr.xp && rows[i].lv1 != rows[i].lv0) ao += (size_t)snprintf(ap + ao, sizeof ap - ao, "%s: level %.0f \xE2\x86\x92 %.0f.  ", nm, rows[i].lv0, rows[i].lv1);
                else if (rr.xp && rows[i].exp1 > rows[i].exp0) ao += (size_t)snprintf(ap + ao, sizeof ap - ao, "%s: +%.0f XP.  ", nm, rows[i].exp1 - rows[i].exp0);
                else ao += (size_t)snprintf(ap + ao, sizeof ap - ao, "%s: unchanged.  ", nm);
            }
        }
        few_set(FT_TABLE, b);
        o = (size_t)snprintf(b, sizeof b, "<color=#D9BE8F>Dungeon loot \xC2\xB7 %d item%s</color>  ", ni, ni == 1 ? "" : "s");
        for (int i = 0; i < ni && o < sizeof b - 80; i++) { char in[64]; few_item_name(items[i].key, in, sizeof in); o += (size_t)snprintf(b + o, sizeof b - o, "%s%s%s", i ? ", " : "", in, items[i].amount > 1 ? "" : ""); if (items[i].amount > 1) o += (size_t)snprintf(b + o, sizeof b - o, " \xC3\x97%d", items[i].amount); }
        if (!ni) o += (size_t)snprintf(b + o, sizeof b - o, "none");
        snprintf(b + o, sizeof b - o, "\n<color=#A69085>Items made from hub stock stay in the hub.</color>");
        few_set(FT_LOOT, b);
        if (ni && ao < sizeof ap - 80) ao += (size_t)snprintf(ap + ao, sizeof ap - ao, "%d dungeon item%s: storage first, then backpacks.", ni, ni == 1 ? "" : "s");
        snprintf(b, sizeof b, "<color=#D9BE8F>What will be applied</color>\n%s", ap); few_set(FT_APPLY, b);
        if (W.ntarget) {
            fe_save_info *t = &W.targets[W.target];
            snprintf(b, sizeof b, "<color=#A69085>Bring home to</color>   %s  <size=80%%><color=#A69085>Day %d \xC2\xB7 %s</color></size>", t->clan, t->day, t->folder); few_set(FT_TARGET, b);
            few_show(MPB_FE_TGT_PREV, W.ntarget > 1); few_show(MPB_FE_TGT_NEXT, W.ntarget > 1);
        } else few_set(FT_TARGET, "<color=#F16071>The home save is not on this computer.</color>");
        if (W.conflict) { few_set(FT_CONFLICT, "Units changed at home since they left. Bring home:"); few_show(MPB_FE_VER_EXP, 1); few_show(MPB_FE_VER_HOME, 1); }
        else if (W.err[0]) { snprintf(b, sizeof b, "%s", W.err); few_set(FT_CONFLICT, b); }
        few_set(FT_NOTE, "");
        few_label(MPB_FE_CANCEL, "Later");
        few_label(MPB_FE_OK, "Apply to a new save");
    }
}
static int few_open_now;                                                    /* frames since opened (tests) */
/* clicks (pending_click); returns 1 if consumed */
/* ---- the Foreign Expedition host page (owner 2026-10-15: no "host from a save / new campaign" question; the hub is a
   neutral map). Until the menu has page Start buttons (mp_api register_host_choice2, lane mpfix), the page's
   "Host from a save" button is this page's Start: it builds the neutral hub (fe_hub.c) and loads it; the menu then
   waits for the campaign as after a pick in the load list and opens the lobby. The Expedition Rules panel stays open. */
int fe_world_start(int kind, char *err, size_t errn);
int fe_choice_v2(void);                                                     /* cm_mp.c: the menu has page Start buttons */
static int fe_page(void) { return !fe_choice_v2() && M.state == MPM_TITLE_HOST && M.set.fe; }
static int fe_page_click(int c) {
    if (!fe_page() || c != MPB_LOAD_SAVE) return 0;
    char err[300] = "";
    if (fe_world_start(0, err, sizeof err)) { snprintf(M.msg, sizeof M.msg, "%s", err); return 1; }
    M.msg[0] = 0; M.picker_seen = 0; M.closed_frames = 0; M.state = MPM_PICK_SAVE;   /* = a pick in the load list: loading -> lobby */
    rig_log("fe: host page Start: the neutral hub loads");
    return 1;
}
static void fe_page_frame(void) {
    if (!fe_page()) return;
    M.settings_open = 1;                                                    /* owner v12 5: the rules open, no extra click */
    Obj b = u_gc_get(h_tb[MPB_LOAD_SAVE]); if (u_alive(b)) tmp_set(btn_label(b, 0), "Start");
    Obj d = u_gc_get(h_tb[MPB_FE_DONE]); if (u_alive(d)) u_active(d, false);
}
static int few_click(int c) {
    if (fe_page_click(c)) return 1;
    if (c < MPB_FE_SAVE0 || c > MPB_FE_RES5 || W.mode == FEW_NONE) return 0;
    fe_mode_info m; fe_mode_get(&m); int cap = m.units > 0 ? m.units : 3;
    if (c == MPB_FE_CANCEL) {
        if (W.mode == FEW_LIST && W.msg[0]) { W.msg[0] = 0; return 1; }
        rig_log("fe: window %d closed (Cancel / Later)", W.mode); W.mode = FEW_NONE; return 1;
    }
    if (W.mode == FEW_BRING) {
        if (c >= MPB_FE_SAVE0 && c <= MPB_FE_SAVE5) { int k = W.page * 6 + c - MPB_FE_SAVE0; if (k < W.nsave && W.saves[k].ok && few_version_ok(&W.saves[k], &m)) { W.sel_save = k; few_load_units(); W.err[0] = 0; } return 1; }
        if (c == MPB_FE_SAVEPG) { W.page = (W.page + 1) * 6 < W.nsave ? W.page + 1 : 0; return 1; }
        if (c == MPB_FE_UNITPG) { W.upage = (W.upage + 1) * 8 < W.nunit ? W.upage + 1 : 0; return 1; }
        if (c >= MPB_FE_UNIT0 && c <= MPB_FE_UNIT7) {
            int k = W.upage * 8 + c - MPB_FE_UNIT0, at = -1;
            for (int p = 0; p < W.npicked; p++) if (W.picked[p] == k) at = p;
            if (at >= 0) { W.picked[at] = W.picked[--W.npicked]; W.err[0] = 0; }
            else if (W.npicked < cap) { W.picked[W.npicked++] = k; W.err[0] = 0; }
            else snprintf(W.err, sizeof W.err, "Up to %d units each in this hub.", cap);
            return 1;
        }
        if (c == MPB_FE_OK && W.npicked && W.sel_save >= 0) {
            char g[64][40]; for (int p = 0; p < W.npicked; p++) snprintf(g[p], 40, "%s", W.units[W.picked[p]].guid);
            int rc = fe_bring_units(W.saves[W.sel_save].path, (const char (*)[40])g, W.npicked, W.err, sizeof W.err);
            rig_log("fe: Bring these units (%d from %s): rc %d", W.npicked, W.saves[W.sel_save].clan, rc);
            if (!rc) W.mode = FEW_NONE;
            return 1;
        }
        return 1;
    }
    if (W.mode == FEW_LIST && c >= MPB_FE_RES0 && c <= MPB_FE_RES5) {
        fe_result_info *x = &W.res[c - MPB_FE_RES0];
        if (c - MPB_FE_RES0 >= W.nres) return 1;
        if (x->applied) {
            const char *f = strrchr(x->applied_file, '/'); char stem[300]; snprintf(stem, sizeof stem, "%s", f ? f + 1 : x->applied_file);
            char *dot = strstr(stem, ".json"); if (dot) *dot = 0;
            snprintf(W.msg, sizeof W.msg, "<size=130%%>Already Brought Home</size>\n\nThese results were brought home on %s.\nSaved as \"%s\". Load it from Load campaign.\n\n<color=#A69085>Results apply once, so units cannot come home twice.</color>", x->applied_date, stem);
        } else few_open(FEW_RESULTS, x->run);
        return 1;
    }
    if (W.mode == FEW_RESULTS) {
        if (c == MPB_FE_TGT_PREV && W.ntarget) { W.target = (W.target + W.ntarget - 1) % W.ntarget; return 1; }
        if (c == MPB_FE_TGT_NEXT && W.ntarget) { W.target = (W.target + 1) % W.ntarget; return 1; }
        if (c == MPB_FE_OK && W.applied) { W.mode = FEW_NONE; return 1; }
        if ((c == MPB_FE_OK && !W.conflict) || c == MPB_FE_VER_EXP || c == MPB_FE_VER_HOME) {
            int choice = c == MPB_FE_VER_EXP ? 1 : c == MPB_FE_VER_HOME ? 2 : 0; char out[600];
            int rc = fe_apply_run(W.run, W.ntarget ? W.targets[W.target].path : "", choice, out, sizeof out, W.err, sizeof W.err);
            rig_log("fe: Apply to a new save (choice %d): rc %d %s", choice, rc, rc ? W.err : out);
            if (rc == 0) { const char *f = strrchr(out, '/'); snprintf(W.msg, sizeof W.msg, "%s", f ? f + 1 : out); char *d = strstr(W.msg, ".json"); if (d) *d = 0; W.applied = 1; W.conflict = 0; }
            else if (rc == 1) W.conflict = 1;
            return 1;
        }
        return 1;
    }
    return 1;
}
/* every frame: where the window lives (session: Esc menu root; title: title view), auto-open on new results */
static char few_seen_run[40];
static void few_frame(const mpm_obs *o) {
    Obj ev = o->scene == 2 ? esc_view() : NULL, tv = o->scene == 1 ? title_view() : NULL;
    Obj host = ev && u_on(ev) ? ev : tv && u_on(tv) ? tv : NULL;
    if (W.mode != FEW_NONE && host && u_gc_get(W.view) != host) {           /* (re)build on the view that shows now */
        int esc = host == ev;
        Obj src = esc ? AT(host, 0x70, Obj) : AT(host, 0x50, Obj);
        if (u_alive(src)) {
            Obj tpl_img = first_image(u_tr(src), 0), tpl_txt = esc ? AT(src, 0x28, Obj) : AT(src, 0x130, Obj);
            Obj root0 = u_gc_get(W.root); if (u_alive(root0)) u_active(root0, false);
            few_build(u_tr(host), src, esc, tpl_img, tpl_txt);
            u_gc_free(W.view); W.view = u_gc_new(host);
        }
    }
    /* new results in a session (the host ended the run): open them over the Esc menu */
    if (o->role && frames % 60 == 0) {
        fe_result_info r[16]; int n = fe_list_results(r, 16);
        for (int i = 0; i < n; i++) if (!r[i].applied && strcmp(r[i].run, few_seen_run)) {
            snprintf(few_seen_run, sizeof few_seen_run, "%s", r[i].run);
            if (W.mode == FEW_NONE) { few_open(FEW_RESULTS, r[i].run); if (!o->esc_open) popup_activate(1); }
            break;
        }
    }
    if (o->scene == 1 && frames % 120 == 0) { fe_result_info r[16]; M.fe_results = fe_list_results(r, 16); }
    if (M.state == MPM_LOBBY) {                                              /* host: the lobby's mode goes to the guests */
        mp_steam_info in; mp_steam_state(&in);
        fe_host_mode(M.set.fe, M.set.fe_death, M.set.fe_xp, M.set.fe_loot, M.set.units, M.set.fe_recruit, my_name);
    }
    fe_page_frame();
    few_sync();
    Obj lw = u_gc_get(h_lw_root);                                           /* the lobby steps aside while ours is open */
    if (W.mode != FEW_NONE && W.esc && u_alive(lw)) u_active(lw, false);
    few_open_now = W.mode == FEW_NONE ? 0 : few_open_now + 1;
}
static void few_tips(void) {
    #define TIPW(id, t, b) tip_try(few_btn(id), (t), (b))
    if (W.mode == FEW_NONE) return;
    if (W.mode == FEW_BRING) {
        for (int i = 0; i < 6; i++) TIPW(MPB_FE_SAVE0 + i, "Your save", "Units come from this save. It is only read: nothing in it changes until you bring the results home.");
        for (int i = 0; i < 8; i++) TIPW(MPB_FE_UNIT0 + i, "Bring this unit", "Click to add or remove it. A copy travels with its gear and backpack; the unit itself stays at home and keeps working.");
        TIPW(MPB_FE_OK, "Bring these units", "Sends copies of the picked units to the host. You can bring other units instead until the host presses Start.");
        TIPW(MPB_FE_CANCEL, "Cancel", "Closes this window. Nothing is sent.");
    }
    if (W.mode == FEW_RESULTS) {
        TIPW(MPB_FE_OK, "Apply to a new save", "Writes the results into a NEW save next to your home save. Your old save is not changed, and the whole save folder is copied to a backup first. Results apply once.");
        TIPW(MPB_FE_CANCEL, "Later", "Keeps the results. Bring them home later from Multiplayer > Expedition results.");
        TIPW(MPB_FE_TGT_PREV, "Bring home to", "The save that gets the results: the units' home campaign, newest first.");
        TIPW(MPB_FE_TGT_NEXT, "Bring home to", "The save that gets the results: the units' home campaign, newest first.");
        TIPW(MPB_FE_VER_EXP, "Expedition version", "Units that changed at home since they left come back as they ended the expedition.");
        TIPW(MPB_FE_VER_HOME, "Home version", "Units that changed at home since they left keep their home progress; the expedition's is dropped.");
    }
    if (W.mode == FEW_LIST) for (int i = 0; i < 6; i++) TIPW(MPB_FE_RES0 + i, "Expedition results", "Open to see what your units bring home. Each result can be brought home once.");
    #undef TIPW
}
#undef WX
#undef WY
/* test read-back: mpm_fewin {} */
static char *few_status(void) {
    Buf b = {0}; fe_mode_info m; int on = fe_mode_get(&m);
    bput(&b, "{\"ok\":true,\"mode\":%d,\"esc\":%d,\"fe_mode\":%d,\"version\":", W.mode, W.esc, on); bstr(&b, m.version);
    bput(&b, ",\"sel_save\":%d,\"picked\":%d,\"applied\":%d,\"conflict\":%d,\"err\":", W.sel_save, W.npicked, W.applied, W.conflict); bstr(&b, W.err);
    bput(&b, ",\"msg\":"); bstr(&b, W.msg); bput(&b, ",\"run\":"); bstr(&b, W.run);
    bput(&b, ",\"saves\":[");
    for (int i = 0; i < W.nsave; i++) { bput(&b, "%s{\"clan\":", i ? "," : ""); bstr(&b, W.saves[i].clan); bput(&b, ",\"path\":"); bstr(&b, W.saves[i].path); bput(&b, ",\"ok\":%d,\"version\":", W.saves[i].ok && few_version_ok(&W.saves[i], &m)); bstr(&b, W.saves[i].version); bput(&b, "}"); }
    bput(&b, "],\"units\":[");
    for (int i = 0; i < W.nunit; i++) { bput(&b, "%s{\"guid\":\"%s\",\"level\":%d}", i ? "," : "", W.units[i].guid, W.units[i].level); }
    bput(&b, "],\"results\":%d,\"ntarget\":%d,\"target\":", W.nres, W.ntarget); bstr(&b, W.ntarget ? W.targets[W.target].path : ""); bput(&b, "}");
    return b.p;
}

/* ---- menu hooks (mpmenu.c calls them where these windows were called before the split) */
static int few_escape(void) {
    if (W.mode == FEW_NONE) return 0;
    rig_log("mpmenu: Escape closed the Foreign Expedition window");
    return MPB_FE_CANCEL;
}
static void few_action(int a) {
    if (a & MPA_FE_BRING) few_open(FEW_BRING, NULL);
    if (a & MPA_FE_RESULTS) few_open(FEW_LIST, NULL);
}
static int few_lobby_bring(void) { fe_mode_info fm; return fe_mode_get(&fm) && W.mode == FEW_NONE; }
static int few_lobby_name(int id, const char *nmn, char *t2, size_t cap) {   /* "2 units brought" / "Choosing units..." */
    fe_mode_info fm; if (!fe_mode_get(&fm)) return 0;
    int nb = fe_brought_of(id);
    if (nb > 0) snprintf(t2, cap, "%s  <size=78%%><color=#8FD18F>%d unit%s brought</color></size>", nmn, nb, nb == 1 ? "" : "s");
    else snprintf(t2, cap, "%s  <size=78%%><color=#A69085>Choosing units...</color></size>", nmn);
    return 1;
}
static char *few_exec(const char *cmd, const char *line) { (void)line; return !strcmp(cmd, "mpm_fewin") ? few_status() : NULL; }   /* Foreign Expedition window read-back */
static const mp_menu_hooks fe_hooks = { .click = few_click, .escape = few_escape, .action = few_action, .frame = few_frame,
    .lobby_bring = few_lobby_bring, .lobby_name = few_lobby_name, .tips = few_tips, .exec = few_exec };
const mp_menu_hooks *fe_menu_hooks(void) { return &fe_hooks; }
