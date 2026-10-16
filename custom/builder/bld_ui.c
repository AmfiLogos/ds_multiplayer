// bld_ui.c - the Unit Builder window (owner AFTER_MVP 4: "Unit Builder drawn in the game's menus"; 2026-10-15: "we want
// to allow people to create custom units inside of the multiplayer"; "when you create a unit, make sure that it's saved
// inside of the game, so that if I want to use the same unit multiple times inside of a hub, I can").
// Opened from the title's Custom Modes page ("Unit Builder"), from My Units ("New unit", "Edit") and from Bring Units
// ("New unit"). A game-style window (the My Units look, mu_ui.h): name, level, then Race / Background / Traits / Skills /
// Gear / Inscriptions on the left, the live sheet on the right. Every choice comes from the wiki builder's own data and
// rules (bld_js.h model on the bld_work thread): the window shows what the model lists and sends each click to it.
// Save = the build into My Units (myunits.h mu_add_build; Edit replaces that unit's build in place, its teams keep it).
// A unit made here lives only in the My Units library file: it never goes into a regular save (Bring Units hands out
// copies; the FE guard refuses library units in a world's results).
#include "bld.h"
#include "bld_js.h"
#include "bld_work.h"
#include "mu_ui.h"
#include "myunits.h"
#include "cm_home.h"
#include "cm_il.h"
#include "sg_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define SCRIPTS_MAX_AGE (6 * 3600)          /* same as Test Fights: the wiki's scripts are asked again after 6 h */
enum { SEC_RACE, SEC_BG, SEC_TRAITS, SEC_SKILLS, SEC_GEAR, SEC_INSC, SEC_N };
static const char *const SEC_NAME[SEC_N] = { "Race", "Background", "Traits", "Skills", "Gear", "Inscriptions" };
enum { ENG_NONE, ENG_OPENING, ENG_READY, ENG_FAILED };

static const DSLUi *U;

/* ------------------------------------------------------------------ the model on the worker thread */
static bld_engine *E;                      /* worker thread only */
static int eng;                            /* main thread's view of the engine */
static char eng_msg[700];

typedef struct { bld_job j; int kind; char *in, *out, *build, *link; char err[700]; char root[1100]; char game_build[32]; } Job;
enum { J_OPEN = 1, J_LOAD, J_ACT, J_SAVE };

static int eng_ensure(Job *b) {
    if (E) return 0;
    sgc c; sgc_init(&c, b->root);
    sg_sources src; char e[500] = "", st[200] = "";
    if (sgc_scripts(&c, SCRIPTS_MAX_AGE, &src, st, sizeof st, e, sizeof e)) {
        snprintf(b->err, sizeof b->err, "The Unit Builder needs the wiki's builder files once (dungeonsettlers.wiki), and they could not be downloaded: %s", e);
        return -1;
    }
    E = bld_open(src.data_js, src.data_n, src.builder_js, src.builder_n, NULL, e, sizeof e);
    sgc_sources_free(&src);
    if (!E) { snprintf(b->err, sizeof b->err, "The wiki's Unit Builder did not start: %s", e); return -1; }
    char *tag = bld_call(E, "tag", NULL, e, sizeof e), t[64] = "";
    if (tag) { sscanf(tag, "\"%63[^\"]", t); free(tag); }
    if (b->game_build[0] && t[0] && strcmp(t, b->game_build)) {
        snprintf(b->err, sizeof b->err, "The wiki's Unit Builder files are for game build %s, and this game is build %s. The Unit Builder opens again when both match.", t, b->game_build);
        bld_close(E); E = NULL;
        return -1;
    }
    return 0;
}
static void job_run(bld_job *j) {
    Job *b = (Job *)j; char e[700] = "";
    if (eng_ensure(b)) return;
    if (b->kind == J_LOAD) b->out = bld_call(E, "load", b->in ? b->in : "null", e, sizeof e);
    else if (b->kind == J_ACT) b->out = bld_call(E, "act", b->in, e, sizeof e);
    else if (b->kind == J_SAVE) {
        if (b->in) { char *s = bld_call(E, "act", b->in, e, sizeof e); free(s); }
        b->build = bld_call(E, "build", NULL, e, sizeof e);
        b->link = b->build ? bld_call(E, "link", NULL, e, sizeof e) : NULL;
        b->out = b->link ? bld_call(E, "state", NULL, e, sizeof e) : NULL;
    } else return;
    if (!b->out) snprintf(b->err, sizeof b->err, "%s", e[0] ? e : "no answer");
}
static void job_done(bld_job *j);
static void post(int kind, const char *in) {
    Job *b = calloc(1, sizeof *b); if (!b) return;
    b->j.run = job_run; b->j.done = job_done; b->kind = kind; b->in = in ? strdup(in) : NULL;
    char d[1024] = "";
    { cm_m m = cm_il_method("UnityEngine", "Application", "get_persistentDataPath", 0, NULL);
      if (m.fn) cm_il_utf8(CM_CALL(m, Obj, const void *)(m.mi), d, sizeof d); }
    snprintf(b->root, sizeof b->root, "%s/Mods/custom-modes/wiki", d);
    { const DSLHost *h = cm_host(); snprintf(b->game_build, sizeof b->game_build, "%s", h && h->game_build ? h->game_build : ""); }
    if (eng != ENG_READY) eng = ENG_OPENING;
    bld_work_post(&b->j);
}

/* ------------------------------------------------------------------ the window */
#define REGMAX 900
static struct {
    int open, section, open_slot;          /* open_slot: gear slot whose item list shows (-1 none) */
    char unit_id[17], lib_name[96];        /* editing this My Units unit ("" = a new unit) */
    void (*saved)(const char *, void *); void *ud;
    DSLObj win, f_name, v_lvl, status, b_save, b_cancel, nav[SEC_N], mid, mid_holder, sheet;
    float w, h, lw, lh, scale, mx, my, mw, mh, sx;
    int lvl_n;                             /* values in v_lvl */
    jv *st;                                /* the model's last state */
    char msg[800]; uint32_t msg_col;
    int saving;
    struct { char name[120]; DSLObj o; } reg[REGMAX]; int nreg;
} W = { .open_slot = -1 };

static void refresh(void);
static int build_ui(void);
static void reg(const char *name, DSLObj o) { if (o && W.nreg < REGMAX) { snprintf(W.reg[W.nreg].name, sizeof W.reg[W.nreg].name, "%s", name); W.reg[W.nreg++].o = o; } }
static void reg_forget_dynamic(void) {     /* keep the fixed widgets (registered first), drop the section's */
    int k = 0; for (int i = 0; i < W.nreg; i++) if (W.reg[i].name[0] != '+') W.reg[k++] = W.reg[i];
    W.nreg = k;
}
static void set_msg(const char *m, uint32_t col) {
    snprintf(W.msg, sizeof W.msg, "%s", m ? m : ""); W.msg_col = col;
    if (W.status) U->set_text(W.status, W.msg);
}
static void sraw(const jv *v, char *out, size_t n) {          /* a JSON string's text (escapes resolved for \" \\ \n) */
    if (!n) return; out[0] = 0;
    if (!v || v->t != JN_STR) return;
    size_t k = 0;
    for (size_t i = 0; i < v->n && k + 1 < n; i++) {
        char c = v->s[i];
        if (c == '\\' && i + 1 < v->n) {
            char d = v->s[++i];
            if (d == 'n') c = '\n'; else if (d == 't') c = ' ';
            else if (d == 'u' && i + 4 < v->n) {                 /* \uXXXX -> UTF-8 (BMP) */
                unsigned u = 0; sscanf(v->s + i + 1, "%4x", &u); i += 4;
                if (u < 0x80) c = (char)u;
                else if (u < 0x800) { if (k + 2 >= n) break; out[k++] = (char)(0xC0 | (u >> 6)); c = (char)(0x80 | (u & 0x3F)); }
                else { if (k + 3 >= n) break; out[k++] = (char)(0xE0 | (u >> 12)); out[k++] = (char)(0x80 | ((u >> 6) & 0x3F)); c = (char)(0x80 | (u & 0x3F)); }
            } else c = d;
        }
        out[k++] = c;
    }
    out[k] = 0;
}
static void jesc(char *o, size_t cap, const char *s) {
    size_t n = 0;
    for (; *s && n + 3 < cap; s++) { if (*s == '"' || *s == '\\') o[n++] = '\\'; if ((unsigned char)*s < 0x20) continue; o[n++] = *s; }
    o[n] = 0;
}
static int num(const jv *o, const char *k) { return (int)jnumv(jget(o, k)); }
static int truthy(const jv *o, const char *k) { return jtruthy(jget(o, k)); }

static void close_win(void) {
    if (!W.open) return;
    W.open = 0;
    if (W.win) U->destroy(W.win);
    W.win = W.mid = W.mid_holder = W.sheet = NULL;
    W.nreg = 0;
    if (W.st) { jfree(W.st); W.st = NULL; }
    W.saved = NULL; W.ud = NULL;
}
static void on_cancel(void *ud, DSLObj w) { (void)ud; (void)w; mu_log("builder: closed without saving"); close_win(); }

/* a click: the action JSON goes to the model, the answer redraws */
static void act(const char *json) {
    if (!W.open || eng != ENG_READY || W.saving) return;
    post(J_ACT, json);
}
static void act_k(const char *t, const char *key) {
    char a[400], k[300]; jesc(k, sizeof k, key ? key : "");
    snprintf(a, sizeof a, "{\"t\":\"%s\",\"k\":\"%s\"}", t, k); act(a);
}

/* ------------------------------------------------------------------ Save */
static void finish_save(Job *b) {
    char e[500] = "", m[900];
    jv *lib = mu_load(e, sizeof e);
    jv *bv = b->build ? jparse(b->build, strlen(b->build), e, sizeof e) : NULL;
    char name[96] = ""; if (W.f_name) U->field_get(W.f_name, name, sizeof name);
    char *p = name; while (*p == ' ') p++;
    size_t L = strlen(p); while (L && p[L - 1] == ' ') p[--L] = 0;
    char id[17] = ""; int rc = -1;
    if (!lib) snprintf(m, sizeof m, "Could not read My Units: %s", e);
    else if (!bv) snprintf(m, sizeof m, "The build could not be read: %s", e);
    else if (W.unit_id[0] && mu_find(lib, W.unit_id)) {
        /* Edit: that unit keeps its id (its teams keep it); its build and name are replaced */
        wk_build wb;
        if (wk_from_json(bv, &wb, e, sizeof e)) snprintf(m, sizeof m, "This unit cannot be saved: %s", e);
        else {
            jv *u = (jv *)mu_find(lib, W.unit_id);
            jset(u, "build", jcopy(bv));
            if (*p) mu_rename(lib, W.unit_id, p);
            jset(u, "updated", jnum_i((long long)time(NULL) * 1000));
            memcpy(id, W.unit_id, 17); rc = 0;
        }
    } else if (mu_add_build(lib, bv, *p ? p : NULL, id, e, sizeof e)) snprintf(m, sizeof m, "This unit cannot be saved: %s", e);
    else rc = 0;
    if (!rc && mu_save(lib, e, sizeof e)) { snprintf(m, sizeof m, "Could not save My Units: %s", e); rc = -1; }
    if (lib) jfree(lib);
    if (bv) jfree(bv);
    W.saving = 0;
    if (rc) { set_msg(m, MU_C_BAD); mu_log("builder: save failed: %s", m); refresh(); return; }
    mu_log("builder: saved unit %s (%s) %s", id, W.unit_id[0] ? "edited" : "new", b->link ? b->link : "");
    void (*cb)(const char *, void *) = W.saved; void *ud = W.ud;
    close_win();
    if (cb) cb(id, ud);
}
static void on_save(void *ud, DSLObj w) {
    (void)ud; (void)w;
    if (!W.open || eng != ENG_READY || W.saving) return;
    char name[96] = "", k[200], a[300]; if (W.f_name) U->field_get(W.f_name, name, sizeof name);
    jesc(k, sizeof k, name);
    snprintf(a, sizeof a, "{\"t\":\"name\",\"v\":\"%s\"}", k);
    W.saving = 1; set_msg("Saving...", MU_C_SUB);
    post(J_SAVE, name[0] ? a : NULL);
}
static void on_name(void *ud, DSLObj w) {        /* Enter in the name field: the build's name follows */
    (void)ud; (void)w;
    char name[96] = "", k[200], a[300]; U->field_get(W.f_name, name, sizeof name);
    if (!name[0]) return;
    jesc(k, sizeof k, name); snprintf(a, sizeof a, "{\"t\":\"name\",\"v\":\"%s\"}", k); act(a);
}

static void job_done(bld_job *j) {
    Job *b = (Job *)j;
    if (!b->out && !E) {
        eng = ENG_FAILED; snprintf(eng_msg, sizeof eng_msg, "%s", b->err[0] ? b->err : "The wiki's Unit Builder gave no answer.");
        mu_log("builder: engine failed: %s", eng_msg);
        W.saving = 0;
        if (W.open) refresh();
    } else {
        eng = ENG_READY;
        if (!b->out) { char m[800]; snprintf(m, sizeof m, "%s", b->err); set_msg(m, MU_C_BAD); mu_log("builder: %s", b->err); W.saving = 0; }
        else if (W.open) {
            char e[200]; jv *s = jparse(b->out, strlen(b->out), e, sizeof e);
            if (s) { if (W.st) jfree(W.st); W.st = s; }
            if (b->kind == J_SAVE) finish_save(b);
            else if (!bld_work_busy()) {                 /* more clicks queued: draw the last answer only */
                if (b->kind == J_LOAD && W.f_name) { char nm[96]; sraw(jget(W.st, "name"), nm, sizeof nm); if (!W.lib_name[0]) U->field_set(W.f_name, nm); }
                refresh();
            }
        }
    }
    free(b->in); free(b->out); free(b->build); free(b->link); free(b);
}

/* ------------------------------------------------------------------ drawing helpers */
/* a button with a game icon on its left and a one-line label (never spills: ellipsis); state colours the label */
enum { ST_OFF, ST_ON, ST_LOCKED };
static DSLObj icon_button(DSLObj parent, float x, float y, float w, float h, int kind, const char *key, const char *label,
                          const char *sub, int state, dsl_ui_cb cb, void *ud) {
    DSLObj b = mu_button(parent, x, y, w, h, "", cb, ud);
    if (!b) return NULL;
    if (state == ST_ON) {                                   /* chosen: a gold edge and a faint gold wash (flat, no sprite) */
        DSLObj wash = U->panel(b, 3, 3, w - 6, h - 6, 0); if (wash) { cm_set_sprite(wash, NULL, 0xD9BE8F26u); cm_raycast(wash, false); }
        DSLObj bar = U->panel(b, 3, 3, 4, h - 6, 0); if (bar) { cm_set_sprite(bar, NULL, MU_C_GOLD); cm_raycast(bar, false); }
    }
    float s = h - 12 > 40 ? 40 : h - 12, tx = 10;
    if (kind) { mu_icon(b, 10, (h - s) / 2, s, kind, key, state == ST_LOCKED, NULL); tx = 10 + s + 8; }
    uint32_t col = state == ST_ON ? MU_C_GOLD : state == ST_LOCKED ? MU_C_SUB : MU_C_MAIN;
    if (sub && *sub) {
        mu_text(b, tx, 4, w - tx - 6, h / 2 - 2, label, 15, DSL_TXT_LEFT, col);
        mu_text(b, tx, h / 2, w - tx - 6, h / 2 - 4, sub, 12, DSL_TXT_LEFT, MU_C_SUB);
    } else mu_text(b, tx, 0, w - tx - 6, h, label, 15, DSL_TXT_LEFT, col);
    if (state == ST_LOCKED) U->set_enabled(b, 0);
    return b;
}
static float heading(DSLObj p, float y, const char *t) { mu_text(p, 4, y, W.mw - 30, 30, t, 17, DSL_TXT_LEFT, MU_C_COPPER); return y + 32; }
static float note(DSLObj p, float y, const char *t, float h) { mu_text(p, 4, y, W.mw - 30, h, t, 14, DSL_TXT_WRAP, MU_C_SUB); return y + h + 4; }
/* key carried by a widget callback: index into the state's array for that section */
#define UD(i) ((void *)(intptr_t)(i))
#define IDX(ud) ((int)(intptr_t)(ud))
static const jv *arr(const char *k) { jv *a = W.st ? jget(W.st, k) : NULL; return a && a->t == JN_ARR ? a : NULL; }

/* ------------------------------------------------------------------ sections */
static void click_list(const char *list, const char *t, void *ud) {
    const jv *a = arr(list); int i = IDX(ud);
    if (!a || i < 0 || (size_t)i >= a->cnt) return;
    char k[200]; sraw(jget(a->v[i], "k"), k, sizeof k); act_k(t, k);
}
static void on_race(void *ud, DSLObj w) { (void)w; click_list("races", "race", ud); }
static void on_bg(void *ud, DSLObj w) { (void)w; click_list("bgs", "bg", ud); }
static void on_ind(void *ud, DSLObj w) { (void)w; click_list("inds", "ind", ud); }
static void on_tree(void *ud, DSLObj w) { (void)w; click_list("treeCands", "tree", ud); }
static void on_insc(void *ud, DSLObj w) { (void)w; click_list("inscs", "insc", ud); }
static void on_skill(void *ud, DSLObj w) {
    (void)w; int v = IDX(ud), t = v >> 16, r = (v >> 4) & 0xFFF, c = v & 15;
    const jv *trees = arr("trees"); if (!trees || (size_t)t >= trees->cnt) return;
    jv *rows = jget(trees->v[t], "rows"); if (!rows || (size_t)r >= rows->cnt) return;
    jv *row = rows->v[r]; if (!row || row->t != JN_ARR || (size_t)c >= row->cnt) return;
    char k[200]; sraw(jget(row->v[c], "k"), k, sizeof k); act_k("skill", k);
}
static void on_slot(void *ud, DSLObj w) { (void)w; W.open_slot = W.open_slot == IDX(ud) ? -1 : IDX(ud); refresh(); }
static void on_item(void *ud, DSLObj w) {
    (void)w; int v = IDX(ud), s = v >> 12, i = (v & 0xFFF) - 1;
    const jv *eq = arr("eq"); if (!eq || (size_t)s >= eq->cnt) return;
    char slot[40], k[200] = "", kk[300], a[400]; sraw(jget(eq->v[s], "slot"), slot, sizeof slot);
    jv *items = jget(eq->v[s], "items");
    if (i >= 0) { if (!items || (size_t)i >= items->cnt) return; sraw(jget(items->v[i], "k"), k, sizeof k); }
    jesc(kk, sizeof kk, k);
    if (i >= 0) snprintf(a, sizeof a, "{\"t\":\"eq\",\"slot\":\"%s\",\"k\":\"%s\"}", slot, kk);
    else snprintf(a, sizeof a, "{\"t\":\"eq\",\"slot\":\"%s\",\"k\":null}", slot);
    W.open_slot = -1;
    act(a);
}
static void on_roll(void *ud, DSLObj w) {
    int v = IDX(ud), s = v >> 4, r = v & 15;
    const jv *eq = arr("eq"); if (!eq || (size_t)s >= eq->cnt) return;
    char slot[40], a[200]; sraw(jget(eq->v[s], "slot"), slot, sizeof slot);
    snprintf(a, sizeof a, "{\"t\":\"roll\",\"slot\":\"%s\",\"r\":%d,\"i\":%d}", slot, r, U->value_get(w) - 1);
    act(a);
}

/* a grid of icon buttons from one of the state's lists; returns the y below it */
typedef struct { const char *list; int kind; int cols; float h; dsl_ui_cb cb; const char *rprefix; int locked_when_full; } Grid;
static float grid(DSLObj p, float y, const Grid *g, int full) {
    const jv *a = arr(g->list); if (!a) return y;
    float cw = (W.mw - 24 - (g->cols - 1) * 8) / g->cols;
    for (size_t i = 0; i < a->cnt; i++) {
        jv *o = a->v[i]; char k[200], n[200], d[1200], rn[220];
        sraw(jget(o, "k"), k, sizeof k); sraw(jget(o, "n"), n, sizeof n); sraw(jget(o, "d"), d, sizeof d);
        int on = truthy(o, "on"), ok = jget(o, "ok") ? truthy(o, "ok") : 1;
        if (g->locked_when_full && !on && !full) ok = 0;
        float x = 4 + (i % g->cols) * (cw + 8), yy = y + (i / g->cols) * (g->h + 6);
        DSLObj b = icon_button(p, x, yy, cw, g->h, g->kind, k, n, NULL, on ? ST_ON : ok ? ST_OFF : ST_LOCKED, g->cb, UD(i));
        if (b) {
            char tr[300] = ""; sraw(jget(o, "trees"), tr, sizeof tr);
            char body[1600]; snprintf(body, sizeof body, "%s%s%s%s", d, tr[0] && d[0] ? "\n" : "", tr[0] ? "Skill trees: " : "", tr);
            if (body[0]) U->tooltip(b, n, body);
            snprintf(rn, sizeof rn, "+%s:%s", g->rprefix, k); reg(rn, b);
        }
    }
    return y + ((a->cnt + g->cols - 1) / g->cols) * (g->h + 6);
}

static float draw_race(DSLObj p, float y) {
    y = heading(p, y, "Race");
    Grid g = { "races", CM_ICON_AFFECTER, 2, 56, on_race, "race", 0 };
    return grid(p, y, &g, 1);
}
static float draw_bg(DSLObj p, float y) {
    y = heading(p, y, "Background");
    y = note(p, y, "The background decides the unit's skill trees.", 22);
    Grid g = { "bgs", CM_ICON_AFFECTER, 2, 48, on_bg, "bg", 0 };
    return grid(p, y, &g, 1);
}
static float draw_traits(DSLObj p, float y) {
    const jv *a = arr("inds"); int on = 0;
    for (size_t i = 0; a && i < a->cnt; i++) on += truthy(a->v[i], "on");
    char t[120]; snprintf(t, sizeof t, "Traits (%d of %d)", on, num(W.st, "indCap"));
    y = heading(p, y, t);
    Grid g = { "inds", CM_ICON_AFFECTER, 2, 48, on_ind, "ind", 0 };
    return grid(p, y, &g, 1);
}
static float draw_skills(DSLObj p, float y) {
    jv *sp = jget(W.st, "sp"); char t[200];
    snprintf(t, sizeof t, "Main SP left %d of %d    Sub SP left %d of %d", num(sp, "main"), num(sp, "mainMax"), num(sp, "sub"), num(sp, "subMax"));
    y = heading(p, y, "Skills");
    y = note(p, y, t, 22);
    const jv *trees = arr("trees");
    if (!trees || !trees->cnt) return note(p, y, "Pick a background first - it decides which trees this unit has.", 24);
    float cw = (W.mw - 24 - 16) / 3, ch = 52;
    for (size_t ti = 0; ti < trees->cnt; ti++) {
        jv *tr = trees->v[ti]; char k[100], n[160], head[300];
        sraw(jget(tr, "k"), k, sizeof k); sraw(jget(tr, "n"), n, sizeof n);
        snprintf(head, sizeof head, "%s  -  %s%s", n, truthy(tr, "w") ? "weapon tree, Main SP" : "auxiliary tree, Sub SP", truthy(tr, "ins") ? ", from an inscription" : "");
        mu_icon(p, 4, y + 2, 28, CM_ICON_TREE, k, 0, NULL);
        mu_text(p, 38, y, W.mw - 60, 32, head, 15, DSL_TXT_LEFT, MU_C_MAIN);
        y += 36;
        jv *rows = jget(tr, "rows");
        for (size_t r = 0; rows && r < rows->cnt; r++) {
            jv *row = rows->v[r];
            for (size_t c = 0; row && row->t == JN_ARR && c < row->cnt && c < 3; c++) {
                jv *nd = row->v[c]; if (!nd || nd->t != JN_OBJ) continue;
                char sk[160], sn[160], why[300], d[1200], sub[120], tip[1700];
                sraw(jget(nd, "k"), sk, sizeof sk); sraw(jget(nd, "n"), sn, sizeof sn); sraw(jget(nd, "why"), why, sizeof why); sraw(jget(nd, "d"), d, sizeof d);
                int on = truthy(nd, "on"), lv = num(nd, "lv");
                char lvs[24] = ""; if (lv > 1) snprintf(lvs, sizeof lvs, "  \xC2\xB7  Lv %d", lv);
                snprintf(sub, sizeof sub, "%s%d %s SP%s%s", on ? "learned  \xC2\xB7  " : "", num(nd, "cost"), truthy(tr, "w") ? "main" : "sub", lvs, truthy(nd, "p") ? "  \xC2\xB7  passive" : "");
                DSLObj b = icon_button(p, 4 + c * (cw + 8), y, cw, ch, CM_ICON_SKILL, sk, sn, sub, on ? ST_ON : why[0] ? ST_LOCKED : ST_OFF,
                                       on_skill, UD(((int)ti << 16) | ((int)r << 4) | (int)c));
                if (b) {
                    snprintf(tip, sizeof tip, "%s%s%s%s", why[0] ? "Locked: " : "", why, why[0] && d[0] ? "\n" : "", d);
                    U->tooltip(b, sn, tip[0] ? tip : " ");
                    char rn[200]; snprintf(rn, sizeof rn, "+skill:%s", sk); reg(rn, b);
                }
            }
            y += ch + 6;
        }
        y += 8;
    }
    return y;
}
static float draw_gear(DSLObj p, float y) {
    y = heading(p, y, "Gear");
    const jv *eq = arr("eq"); if (!eq) return y;
    for (size_t s = 0; s < eq->cnt; s++) {
        jv *e = eq->v[s]; char slot[40], label[60], k[160], n[160], blocked[300], rn[200];
        sraw(jget(e, "slot"), slot, sizeof slot); sraw(jget(e, "label"), label, sizeof label); sraw(jget(e, "k"), k, sizeof k);
        sraw(jget(e, "n"), n, sizeof n); sraw(jget(e, "blocked"), blocked, sizeof blocked);
        mu_text(p, 4, y, 110, 48, label, 15, DSL_TXT_LEFT, MU_C_SUB);
        if (blocked[0] && !k[0]) { mu_text(p, 120, y, W.mw - 150, 48, blocked, 13, DSL_TXT_WRAP, MU_C_SUB); y += 54; continue; }
        DSLObj b = icon_button(p, 120, y, W.mw - 150, 48, k[0] ? CM_ICON_ITEM : 0, k, k[0] ? n : "(empty)",
                               W.open_slot == (int)s ? "choose below" : "click to change", k[0] ? ST_ON : ST_OFF, on_slot, UD(s));
        snprintf(rn, sizeof rn, "+slot:%s", slot); reg(rn, b);
        y += 54;
        /* the item's random rolls (which stats rolled; the value is the middle of the range, like the wiki's picker) */
        jv *rolls = jget(e, "rolls"), *tags = jget(e, "tags");
        if (rolls && rolls->t == JN_ARR && tags && tags->t == JN_ARR && tags->cnt) {
            const char *vals[64]; char store[64][120]; int nv = 0;
            vals[nv++] = "(none)";
            for (size_t i = 0; i < tags->cnt && nv < 64; i++) { sraw(tags->v[i], store[nv], sizeof store[nv]); vals[nv] = store[nv]; nv++; }
            for (size_t r = 0; r < rolls->cnt && r < 8; r++) {
                char lab[40]; snprintf(lab, sizeof lab, "Roll %zu", r + 1);
                DSLObj v = U->value_row(p, 120, y, W.mw - 150, lab, vals, nv, num(rolls->v[r], "i") + 1, on_roll, UD(((int)s << 4) | (int)r));
                snprintf(rn, sizeof rn, "+roll:%s:%zu", slot, r); reg(rn, v);
                y += 46;
            }
        }
        if (W.open_slot == (int)s) {
            jv *items = jget(e, "items");
            float cw = (W.mw - 24 - 8) / 2;
            DSLObj be = icon_button(p, 4, y, cw, 44, 0, "", "(empty)", NULL, ST_OFF, on_item, UD(((int)s << 12) | 0));
            reg("+item:", be);
            for (size_t i = 0; items && i < items->cnt; i++) {
                jv *it = items->v[i]; char ik[160], in[160], sub[120];
                sraw(jget(it, "k"), ik, sizeof ik); sraw(jget(it, "n"), in, sizeof in);
                char sb[60], rr[40]; sraw(jget(it, "sub"), sb, sizeof sb); sraw(jget(it, "r"), rr, sizeof rr);
                snprintf(sub, sizeof sub, "%s%s%s%s%s", sb, rr[0] ? "  \xC2\xB7  " : "", rr, truthy(it, "th") ? "  \xC2\xB7  two-handed" : "", truthy(it, "ob") ? "" : "  \xC2\xB7  not obtainable");
                size_t cell = i + 1;
                DSLObj ib = icon_button(p, 4 + (cell % 2) * (cw + 8), y + (cell / 2) * 50, cw, 44, CM_ICON_ITEM, ik, in, sub,
                                        !strcmp(ik, k) ? ST_ON : ST_OFF, on_item, UD(((int)s << 12) | (int)(i + 1)));
                snprintf(rn, sizeof rn, "+item:%s", ik); reg(rn, ib);
            }
            size_t cells = (items ? items->cnt : 0) + 1;
            y += ((cells + 1) / 2) * 50 + 8;
        }
    }
    return y;
}
static float draw_insc(DSLObj p, float y) {
    jv *in = jget(W.st, "insc"); char t[400];
    int slots = num(in, "slots"), trees = num(in, "trees"), used = num(in, "used"), freen = num(in, "free");
    y = heading(p, y, "Inscriptions");
    snprintf(t, sizeof t, "One inscription per level: %d by level %d. %d spent on extra skill trees, %d on inscriptions, so %d free.",
             slots, num(W.st, "lvl"), trees, used, freen);
    y = note(p, y, t, 40);
    y = heading(p, y, "Extra skill trees");
    Grid gt = { "treeCands", CM_ICON_TREE, 3, 44, on_tree, "tree", 1 };
    y = grid(p, y, &gt, freen > 0) + 8;
    y = heading(p, y, "Inscriptions");
    Grid gi = { "inscs", CM_ICON_AFFECTER, 2, 48, on_insc, "insc", 1 };
    return grid(p, y, &gi, freen > 0);
}

static void draw_sheet(void) {
    if (W.sheet) U->destroy(W.sheet);
    W.sheet = U->panel(W.win, W.sx, W.my, W.w - W.sx - 20, W.mh, MU_C_BAND);
    if (!W.sheet) return;
    cm_raycast(W.sheet, false);
    float sw = W.w - W.sx - 20, y = 10;
    if (!W.st) return;
    jv *sh = jget(W.st, "sheet");
    const char *parts[2] = { "stats", "majors" };
    for (int pi = 0; pi < 2; pi++) {
        jv *a = sh ? jget(sh, parts[pi]) : NULL;
        for (size_t i = 0; a && a->t == JN_ARR && i < a->cnt; i++) {
            char n[80], t[60]; sraw(jget(a->v[i], "n"), n, sizeof n); sraw(jget(a->v[i], "t"), t, sizeof t);
            mu_text(W.sheet, 14, y, sw - 120, 26, n, 15, DSL_TXT_LEFT, MU_C_SUB);
            DSLObj v = mu_text(W.sheet, sw - 110, y, 96, 26, t, 15, DSL_TXT_RIGHT, MU_C_MAIN);
            char rn[120]; snprintf(rn, sizeof rn, "+stat:%s", n); reg(rn, v);
            y += 27;
        }
        y += 10;
    }
    jv *sp = jget(W.st, "sp"), *in = jget(W.st, "insc"); char t[80];
    snprintf(t, sizeof t, "%d / %d", num(sp, "main"), num(sp, "mainMax"));
    mu_text(W.sheet, 14, y, sw - 120, 26, "Main SP left", 15, DSL_TXT_LEFT, MU_C_SUB); mu_text(W.sheet, sw - 110, y, 96, 26, t, 15, DSL_TXT_RIGHT, MU_C_MAIN); y += 27;
    snprintf(t, sizeof t, "%d / %d", num(sp, "sub"), num(sp, "subMax"));
    mu_text(W.sheet, 14, y, sw - 120, 26, "Sub SP left", 15, DSL_TXT_LEFT, MU_C_SUB); mu_text(W.sheet, sw - 110, y, 96, 26, t, 15, DSL_TXT_RIGHT, MU_C_MAIN); y += 27;
    snprintf(t, sizeof t, "%d / %d", num(in, "free"), num(in, "slots"));
    mu_text(W.sheet, 14, y, sw - 120, 26, "Inscriptions free", 15, DSL_TXT_LEFT, MU_C_SUB); mu_text(W.sheet, sw - 110, y, 96, 26, t, 15, DSL_TXT_RIGHT, MU_C_MAIN);
}

static void draw_mid(void) {
    if (!W.mid) {
        W.mid = U->scroll(W.win, W.mx, W.my, W.mw, W.mh);
        W.mid_holder = NULL;
    }
    if (!W.mid) return;
    if (W.mid_holder) U->destroy(W.mid_holder);
    W.mid_holder = U->panel(W.mid, 0, 0, W.mw - 16, 10, 0x00000000u);
    if (!W.mid_holder) return;
    cm_raycast(W.mid_holder, false);
    DSLObj p = W.mid_holder; float y = 8;
    if (eng == ENG_FAILED) y = note(p, y, eng_msg, 140);
    else if (eng != ENG_READY || !W.st) y = note(p, y, "Loading the wiki's Unit Builder...", 30);
    else switch (W.section) {
        case SEC_RACE: y = draw_race(p, y); break;
        case SEC_BG: y = draw_bg(p, y); break;
        case SEC_TRAITS: y = draw_traits(p, y); break;
        case SEC_SKILLS: y = draw_skills(p, y); break;
        case SEC_GEAR: y = draw_gear(p, y); break;
        default: y = draw_insc(p, y); break;
    }
    U->place(p, 0, 0, W.mw - 16, y + 12);
    U->scroll_height(W.mid, y + 12);
}

static void on_lvl(void *ud, DSLObj w) { (void)ud; char a[64]; snprintf(a, sizeof a, "{\"t\":\"lvl\",\"v\":%d}", U->value_get(w) + 1); act(a); }
static void on_nav(void *ud, DSLObj w) {
    (void)w; int s = IDX(ud); if (s < 0 || s >= SEC_N) return;
    if (W.section != s) { W.section = s; W.open_slot = -1; if (W.mid) { U->destroy(W.mid); W.mid = W.mid_holder = NULL; } }
    refresh();
}

static void refresh(void) {
    if (!W.open || !W.win) return;
    reg_forget_dynamic();
    for (int i = 0; i < SEC_N; i++) if (W.nav[i]) {
        char t[80]; snprintf(t, sizeof t, "%s%s%s", i == W.section ? "<color=#D9BE8F>" : "", SEC_NAME[i], i == W.section ? "</color>" : "");
        U->set_text(W.nav[i], t);
    }
    int ready = eng == ENG_READY && W.st;
    if (W.v_lvl && ready) {
        int mx = num(W.st, "maxLvl"), lv = num(W.st, "lvl");
        if (mx != W.lvl_n) {                                  /* the level values come from the data (consts.maxLevel) */
            char store[100][8]; const char *vals[100]; int n = mx > 0 && mx <= 100 ? mx : 12;
            for (int i = 0; i < n; i++) { snprintf(store[i], sizeof store[i], "%d", i + 1); vals[i] = store[i]; }
            U->destroy(W.v_lvl);
            W.v_lvl = U->value_row(W.win, 360, 56, 240, "Level", vals, n, lv - 1, on_lvl, NULL);
            W.lvl_n = n;
            reg("lvl", W.v_lvl);
        } else U->value_set(W.v_lvl, lv - 1);
    }
    if (W.b_save) U->set_enabled(W.b_save, ready && !W.saving);
    if (W.v_lvl) U->set_enabled(W.v_lvl, ready);
    draw_mid();
    draw_sheet();
    if (W.status) U->set_text(W.status, W.msg);
}

static int build_ui(void) {
    float w, h;
    W.win = mu_window(W.unit_id[0] ? "Unit Builder - Edit" : "Unit Builder", 1240, 780, &w, &h);
    if (!W.win) { mu_log("builder: kit not ready"); return -1; }
    W.w = w; W.h = h; W.mid = W.mid_holder = W.sheet = NULL; W.nreg = 0; W.lvl_n = 0;
    W.lw = W.lh = 0; cm_rect_size(cm_parent(W.win), &W.lw, &W.lh); W.scale = mu_ui_scale();
    float y = 56;
    mu_text(W.win, 20, y, 64, 40, "Name", 15, DSL_TXT_LEFT, MU_C_SUB);
    W.f_name = U->field(W.win, 84, y, 260, 40, "Unit name", W.lib_name, on_name, NULL);
    { const char *one[] = { "-" }; W.v_lvl = U->value_row(W.win, 360, y, 240, "Level", one, 1, 0, on_lvl, NULL); }
    W.b_save = mu_button(W.win, w - 280, y, 125, 40, "Save", on_save, NULL);
    W.b_cancel = mu_button(W.win, w - 145, y, 125, 40, "Cancel", on_cancel, NULL);
    W.status = mu_text(W.win, 612, y, w - 280 - 612 - 10, 40, W.msg, 14, DSL_TXT_LEFT, MU_C_SUB);
    U->tooltip(W.b_save, "Save", "Saves this unit in My Units. Use it in Bring Units (Foreign Expedition, PvP) and Test Fights.");
    reg("name", W.f_name); reg("lvl", W.v_lvl); reg("save", W.b_save); reg("cancel", W.b_cancel); reg("status", W.status); reg("win", W.win);
    float ny = 108;
    for (int i = 0; i < SEC_N; i++) {
        W.nav[i] = mu_button(W.win, 20, ny + i * 48, 180, 42, SEC_NAME[i], on_nav, UD(i));
        char rn[40]; snprintf(rn, sizeof rn, "nav:%s", SEC_NAME[i]); reg(rn, W.nav[i]);
    }
    W.my = 108; W.mh = h - W.my - 20; W.mx = 212;
    float sheet_w = w >= 1100 ? 300 : 250;
    W.sx = w - 20 - sheet_w; W.mw = W.sx - 12 - W.mx;
    return 0;
}

static void win_poll(void) {             /* the kit's screen layer or the UI scale changed: build the window again */
    if (!W.open || !W.win) return;
    float lw = 0, lh = 0;
    if (!cm_rect_size(cm_parent(W.win), &lw, &lh) && lw >= 100 && lh >= 100
        && (fabsf(lw - W.lw) > 1 || fabsf(lh - W.lh) > 1 || fabsf(mu_ui_scale() - W.scale) > 0.001f)) {
        char name[96] = ""; if (W.f_name) U->field_get(W.f_name, name, sizeof name);
        mu_log("builder: screen layer %.0fx%.0f -> %.0fx%.0f, window built again", W.lw, W.lh, lw, lh);
        U->destroy(W.win); W.win = NULL;
        if (build_ui()) { close_win(); return; }
        if (name[0]) U->field_set(W.f_name, name);
        refresh();
    }
}

void cm_builder_open(const char *unit_id_or_null, void (*saved)(const char *unit_id, void *ud), void *ud) {
    U = mu_kit(); if (!U) return;
    if (W.open) close_win();
    if (bld_work_start()) { mu_log("builder: worker thread did not start"); return; }
    { static int polling; if (!polling) polling = !mu_tick_add(win_poll); }
    W.section = SEC_RACE; W.open_slot = -1; W.msg[0] = 0; W.saving = 0;
    W.unit_id[0] = 0; W.lib_name[0] = 0; W.saved = saved; W.ud = ud;
    { char p[1200] = ""; cm_m m = cm_il_method("UnityEngine", "Application", "get_persistentDataPath", 0, NULL);
      if (m.fn) { cm_il_utf8(CM_CALL(m, Obj, const void *)(m.mi), p, sizeof p); if (p[0]) mu_set_data_dir(p); } }
    char *bj = NULL;
    if (unit_id_or_null && *unit_id_or_null) {
        char e[400]; jv *lib = mu_load(e, sizeof e);
        const jv *u = lib ? mu_find(lib, unit_id_or_null) : NULL;
        jv *b = u ? jget(u, "build") : NULL;
        if (b) {
            snprintf(W.unit_id, sizeof W.unit_id, "%s", unit_id_or_null);
            sraw(jget(u, "name"), W.lib_name, sizeof W.lib_name);
            bj = jdump(b, 0, NULL);
        } else set_msg(lib ? "That unit is not in My Units any more: a new unit starts here." : e, MU_C_BAD);
        if (lib) jfree(lib);
    }
    W.saved = saved; W.ud = ud;
    if (build_ui()) { free(bj); return; }
    W.open = 1;
    if (eng == ENG_FAILED) eng = ENG_NONE;              /* try again (the wiki may answer now) */
    post(J_LOAD, bj ? bj : "null");
    free(bj);
    refresh();
    mu_log("builder: window open (%s)", W.unit_id[0] ? W.unit_id : "new unit");
}

int bld_is_open(void) { return W.open; }
void bld_close_window(void) { close_win(); }

/* ------------------------------------------------------------------ tests (ext/units13/driver "bld:<name>") */
DSLObj bld_test_find(const char *n) {
    if (!W.open) return NULL;
    for (int i = 0; i < W.nreg; i++) {
        const char *rn = W.reg[i].name; if (*rn == '+') rn++;
        if (!strcmp(rn, n)) return W.reg[i].o;
    }
    return NULL;
}
size_t bld_test_state(char *out, size_t cap) {
    char m[900], nm[200] = "", race[100] = "", bg[100] = "", em[800];
    jesc(m, sizeof m, W.msg); jesc(em, sizeof em, eng_msg);
    if (W.st) { char t[200]; sraw(jget(W.st, "name"), t, sizeof t); jesc(nm, sizeof nm, t); sraw(jget(W.st, "race"), race, sizeof race); sraw(jget(W.st, "bg"), bg, sizeof bg); }
    int learned = 0; const jv *trees = arr("trees");
    for (size_t t = 0; trees && t < trees->cnt; t++) { jv *rows = jget(trees->v[t], "rows");
        for (size_t r = 0; rows && r < rows->cnt; r++) for (size_t c = 0; rows->v[r]->t == JN_ARR && c < rows->v[r]->cnt; c++) learned += jtruthy(jget(rows->v[r]->v[c], "on")); }
    return (size_t)snprintf(out, cap, "{\"open\":%d,\"engine\":%d,\"busy\":%d,\"saving\":%d,\"section\":%d,\"edit\":\"%s\",\"msg\":\"%s\",\"eng_msg\":\"%s\","
                            "\"name\":\"%s\",\"race\":\"%s\",\"bg\":\"%s\",\"lvl\":%d,\"learned\":%d,\"widgets\":%d,\"w\":%.0f,\"h\":%.0f}",
                            W.open, eng, bld_work_busy(), W.saving, W.section, W.unit_id, m, em, nm, race, bg, W.st ? num(W.st, "lvl") : 0, learned,
                            W.nreg, W.w, W.h);
}
/* test: keys the window lists now, "key,on,locked;..." for kind races|bgs|inds|treeCands|inscs|skills|items:<Slot> */
int bld_test_keys(const char *kind, char *out, size_t n) {
    size_t o = 0; out[0] = 0;
    if (!W.st) return -1;
    if (!strcmp(kind, "skills")) {
        const jv *trees = arr("trees");
        for (size_t t = 0; trees && t < trees->cnt; t++) { jv *rows = jget(trees->v[t], "rows");
            for (size_t r = 0; rows && r < rows->cnt; r++) for (size_t c = 0; rows->v[r]->t == JN_ARR && c < rows->v[r]->cnt; c++) {
                jv *nd = rows->v[r]->v[c]; if (!nd || nd->t != JN_OBJ) continue;
                char k[160], why[200]; sraw(jget(nd, "k"), k, sizeof k); sraw(jget(nd, "why"), why, sizeof why);
                if (o + 200 < n) o += (size_t)snprintf(out + o, n - o, "%s,%d,%d;", k, jtruthy(jget(nd, "on")), why[0] != 0);
            } }
        return 0;
    }
    const jv *a = NULL;
    if (!strncmp(kind, "items:", 6)) {
        const jv *eq = arr("eq");
        for (size_t s = 0; eq && s < eq->cnt; s++) { char sl[40]; sraw(jget(eq->v[s], "slot"), sl, sizeof sl); if (!strcmp(sl, kind + 6)) a = jget(eq->v[s], "items"); }
    } else a = arr(kind);
    for (size_t i = 0; a && a->t == JN_ARR && i < a->cnt; i++) {
        char k[160]; sraw(jget(a->v[i], "k"), k, sizeof k);
        int ok = jget(a->v[i], "ok") ? jtruthy(jget(a->v[i], "ok")) : 1;
        if (o + 200 < n) o += (size_t)snprintf(out + o, n - o, "%s,%d,%d;", k, jtruthy(jget(a->v[i], "on")), !ok);
    }
    return 0;
}
/* test: one click by JSON (the level row's arrows are not clickable by the driver) */
int bld_test_act_json(const char *json) { if (!W.open || eng != ENG_READY) return -1; act(json); return 0; }
