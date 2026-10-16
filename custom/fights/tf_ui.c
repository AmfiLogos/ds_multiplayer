/* tf_ui.c - the Test Fights window (tf.h). Texts are the wiki panel's own (builder.js renderSaveGen), so the game and
 * the wiki say the same thing. The fight choices, defaults and limits come from sg_panel (the wiki's data), never typed
 * here. */
#include "tf.h"
#include "cm_win.h"
#include "cm_work.h"
#include "tf_worlds.h"
#include "sg_js.h"
#include "sg_cache.h"
#include "myunits.h"
#include "cm_home.h"
#include "../fe/fe_json.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define WIN_W 1040.0f
#define WIN_H 720.0f
#define BODY_H (WIN_H - CMW_BAND - 1)
#define LX 24.0f
#define LW 360.0f
#define RX 408.0f
#define RW 608.0f
#define GOLD 0xD9BE8FFFu
#define SUB 0xA69085FFu
#define WARN 0xEA9F3DFFu
#define MAIN 0xE8DCC8FFu
#define SCRIPTS_MAX_AGE (6 * 3600)          /* the wiki's scripts are asked again after 6 h (offline = the cached copy) */

/* ------------------------------------------------------------------ the panel (sg_panel JSON) */
#define MAXL 128
typedef struct { char id[24], n[96]; int abyss; } Boss;
typedef struct { char id[24], n[96]; int slots; double w; int on; } Kit;
typedef struct { char k[80], n[96]; } Mon;
static struct {
    int ok; char tag[32], site[32];
    int max_squad;
    char modes[6][16]; int nmodes;
    Boss bosses[8]; int nbosses;
    char combat[8][24], mgmt[8][24], mgmt_for[8][24], label[8][32]; int ncombat, nmgmt;
    Kit kits[8]; int nkits; char insc_n[96]; char miss[8][160]; int nmiss;
    Mon *mons; int nmons;
    int msat_keys[12], nmsat, msat_def;
    char dummy_n[96]; double dummy_dur;
    char def_mode[16], def_boss[24], def_combat[24];
} P;

static void unesc(const char *s, char *o, size_t n) {           /* raw JSON string text -> UTF-8 */
    size_t k = 0;
    for (; s && *s && k + 4 < n; s++) {
        if (*s != '\\') { o[k++] = *s; continue; }
        s++;
        if (*s == 'u' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2]) && isxdigit((unsigned char)s[3]) && isxdigit((unsigned char)s[4])) {
            char hx[5] = { s[1], s[2], s[3], s[4], 0 }; unsigned c = (unsigned)strtoul(hx, NULL, 16); s += 4;
            if (c < 0x80) o[k++] = (char)c;
            else if (c < 0x800) { o[k++] = (char)(0xC0 | c >> 6); o[k++] = (char)(0x80 | (c & 63)); }
            else { o[k++] = (char)(0xE0 | c >> 12); o[k++] = (char)(0x80 | (c >> 6 & 63)); o[k++] = (char)(0x80 | (c & 63)); }
        } else if (*s == 'n') o[k++] = '\n';
        else if (*s == 't') o[k++] = ' ';
        else if (*s) o[k++] = *s;
        else break;
    }
    o[k] = 0;
}
static void jtxt(const jv *v, char *o, size_t n) { o[0] = 0; if (v && v->t == JN_STR) { char *r = jstrdup_raw(v); unesc(r, o, n); free(r); } }
static void jesc(const char *s, char *o, size_t n) {           /* UTF-8 -> JSON string body */
    size_t k = 0;
    for (; s && *s && k + 7 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { o[k++] = '\\'; o[k++] = (char)c; }
        else if (c < 0x20) k += (size_t)snprintf(o + k, n - k, "\\u%04x", c);
        else o[k++] = (char)c;
    }
    o[k] = 0;
}
static int panel_parse(const char *json) {
    char e[200]; jv *r = jparse(json, strlen(json), e, sizeof e);
    if (!r) { cmw_log("tf: panel JSON: %s", e); return -1; }
    free(P.mons); memset(&P, 0, sizeof P);
    jtxt(jget(r, "tag"), P.tag, sizeof P.tag); jtxt(jget(r, "siteVer"), P.site, sizeof P.site);
    P.max_squad = (int)jnumv(jget(r, "maxSquad")); if (P.max_squad <= 0) P.max_squad = 4;
    jv *a = jget(r, "modes");
    for (size_t i = 0; a && i < a->cnt && P.nmodes < 6; i++) jtxt(a->v[i], P.modes[P.nmodes++], 16);
    a = jget(r, "bosses");
    for (size_t i = 0; a && i < a->cnt && P.nbosses < 8; i++) {
        Boss *b = &P.bosses[P.nbosses++];
        jtxt(jget(a->v[i], "id"), b->id, sizeof b->id); jtxt(jget(a->v[i], "n"), b->n, sizeof b->n); b->abyss = jtruthy(jget(a->v[i], "abyss"));
    }
    jv *labels = jget(r, "diffLabels"), *mf = jget(r, "mgmtFor");
    a = jget(r, "combatTiers");
    for (size_t i = 0; a && i < a->cnt && P.ncombat < 8; i++) {
        jtxt(a->v[i], P.combat[P.ncombat], 24);
        jtxt(jget(labels, P.combat[P.ncombat]), P.label[P.ncombat], 32);
        jtxt(jget(mf, P.combat[P.ncombat]), P.mgmt_for[P.ncombat], 24);
        if (!P.label[P.ncombat][0]) snprintf(P.label[P.ncombat], 32, "%s", P.combat[P.ncombat]);
        P.ncombat++;
    }
    a = jget(r, "mgmtTiers");
    for (size_t i = 0; a && i < a->cnt && P.nmgmt < 8; i++) jtxt(a->v[i], P.mgmt[P.nmgmt++], 24);
    jv *dk = jget(jget(r, "defaults"), "kits");
    a = jget(r, "kits");
    for (size_t i = 0; a && i < a->cnt && P.nkits < 8; i++) {
        Kit *k = &P.kits[P.nkits++];
        jtxt(jget(a->v[i], "id"), k->id, sizeof k->id); jtxt(jget(a->v[i], "n"), k->n, sizeof k->n);
        k->slots = (int)jnumv(jget(a->v[i], "slots")); k->w = jnumv(jget(a->v[i], "w"));
        k->on = jtruthy(jget(dk, k->id));
    }
    jtxt(jget(jget(r, "kitInsc"), "n"), P.insc_n, sizeof P.insc_n);
    a = jget(r, "kitMiss");
    for (size_t i = 0; a && i < a->cnt && P.nmiss < 8; i++) {
        char k[80], why[80]; jtxt(jget(a->v[i], "k"), k, sizeof k); jtxt(jget(a->v[i], "why"), why, sizeof why);
        snprintf(P.miss[P.nmiss++], 160, "Left out of the kits: %s (%s).", k, why);
    }
    a = jget(r, "enemies");
    if (a && a->cnt) {
        P.mons = calloc(a->cnt, sizeof *P.mons);
        for (size_t i = 0; P.mons && i < a->cnt; i++) { Mon *m = &P.mons[P.nmons++]; jtxt(jget(a->v[i], "k"), m->k, sizeof m->k); jtxt(jget(a->v[i], "n"), m->n, sizeof m->n); }
    }
    jv *ms = jget(r, "msat"); a = jget(ms, "keys");
    for (size_t i = 0; a && i < a->cnt && P.nmsat < 12; i++) P.msat_keys[P.nmsat++] = (int)jnumv(a->v[i]);
    P.msat_def = (int)jnumv(jget(ms, "def"));
    jv *d = jget(r, "dummy");
    if (d && d->t == JN_OBJ) { jtxt(jget(d, "n"), P.dummy_n, sizeof P.dummy_n); P.dummy_dur = jnumv(jget(d, "dur")); }
    jv *df = jget(r, "defaults");
    jtxt(jget(df, "mode"), P.def_mode, sizeof P.def_mode); jtxt(jget(df, "boss"), P.def_boss, sizeof P.def_boss);
    jtxt(jget(df, "combat"), P.def_combat, sizeof P.def_combat);
    jfree(r);
    P.ok = P.nmodes > 0 && P.ncombat > 0;
    return P.ok ? 0 : -1;
}

/* ------------------------------------------------------------------ choices (the wiki panel's `st`) */
#define MAXMON 16
static struct {
    int mode, boss, combat, mgmt, mgmt_own, msat, kit[8], kit_insc;
    struct { char k[80]; int n; } mon[MAXMON]; int nmon; char monq[80]; int mon_count;
    int src;                                    /* 0 My Units, 1 Gallery */
    char ids[8][17]; int nids; char team[96];   /* My Units picks; team = the picked team's name ("" = single units) */
    char *gal_builds; char gal_title[160]; char gal_names[8][96]; int gal_n;
    int seeded;
} st;
static int idx_of(char (*list)[24], int n, const char *v) { for (int i = 0; i < n; i++) if (!strcmp(list[i], v)) return i; return 0; }
static void seed_choices(void) {
    if (st.seeded) return;
    st.seeded = 1;
    st.mode = 0; for (int i = 0; i < P.nmodes; i++) if (!strcmp(P.modes[i], P.def_mode)) st.mode = i;
    st.boss = 0; for (int i = 0; i < P.nbosses; i++) if (!strcmp(P.bosses[i].id, P.def_boss)) st.boss = i;
    st.combat = idx_of(P.combat, P.ncombat, P.def_combat[0] ? P.def_combat : "Normal");
    st.mgmt_own = 0;
    st.msat = 0; for (int i = 0; i < P.nmsat; i++) if (P.msat_keys[i] == P.msat_def) st.msat = i;
    for (int i = 0; i < P.nkits; i++) st.kit[i] = P.kits[i].on;
    st.kit_insc = 1; st.mon_count = 3;
}
static int mgmt_index(void) {
    if (st.mgmt_own) return st.mgmt;
    const char *m = P.mgmt_for[st.combat]; int i = idx_of(P.mgmt, P.nmgmt, m);
    return i;
}
static const char *mode_id(void) { return st.mode < P.nmodes ? P.modes[st.mode] : "dps"; }
static int is_fight(void) { return !strcmp(mode_id(), "boss") || !strcmp(mode_id(), "arena"); }

/* ------------------------------------------------------------------ the engine on the worker thread */
static sg_engine *E; static sgc C; static int engine_state;       /* 0 none, 1 opening, 2 ready, 3 failed */
static char eng_msg[600], status[1200]; static int status_warn, busy;
typedef struct { cm_job j; char *in, *out, *file; size_t file_n; char err[600]; int problems; char status[200]; } Job;

/* worker thread: the engine, opened once (scripts from the cache, refreshed after SCRIPTS_MAX_AGE) */
static int eng_ensure(char *err, size_t errn, char *st_out, size_t sn) {
    if (E) { if (st_out) snprintf(st_out, sn, "running"); return 0; }
    char root[1100], d[1024];
    if (cmw_game_data(d, sizeof d)) { snprintf(err, errn, "The game's data folder is not known."); return -1; }
    snprintf(root, sizeof root, "%s/Mods/custom-modes/wiki", d);
    sgc_init(&C, root);
    sg_sources src; char e[400] = "", s2[200] = "";
    if (sgc_scripts(&C, SCRIPTS_MAX_AGE, &src, s2, sizeof s2, e, sizeof e)) {
        snprintf(err, errn, "Test Fights needs the wiki's files once (dungeonsettlers.wiki), and they could not be downloaded: %s", e);
        return -1;
    }
    E = sg_open(&src, sgc_template, &C, NULL, e, sizeof e);
    sgc_sources_free(&src);
    if (!E) { snprintf(err, errn, "The wiki's save generator did not start: %s", e); return -1; }
    if (st_out) snprintf(st_out, sn, "%s", s2);
    return 0;
}
sg_engine *tf_engine_for_gallery(char *err, size_t errn) { return eng_ensure(err, errn, NULL, 0) ? NULL : E; }
static void job_open_run(cm_job *j) {
    Job *b = (Job *)j; char e[400] = "";
    if (eng_ensure(b->err, sizeof b->err, b->status, sizeof b->status)) return;
    b->out = sg_panel(E, e, sizeof e);
    if (!b->out) snprintf(b->err, sizeof b->err, "The wiki's save generator did not start: %s", e);
}
static void paint_all(void);
static void job_open_done(cm_job *j) {
    Job *b = (Job *)j;
    if (b->out && !panel_parse(b->out)) {
        engine_state = 2; eng_msg[0] = 0;
        const char *gb = cmw_host()->game_build;
        if (gb && P.tag[0] && strcmp(gb, P.tag)) {
            snprintf(eng_msg, sizeof eng_msg, "The wiki's Test Fights files are for game build %s, and this game is build %s. Test Fights opens again when both match.", P.tag, gb);
            engine_state = 3;
        }
        seed_choices();
        cmw_log("tf: engine ready (%s), wiki data %s, %d modes", b->status, P.tag, P.nmodes);
    } else {
        engine_state = 3;
        snprintf(eng_msg, sizeof eng_msg, "%s", b->err[0] ? b->err : "The wiki's save generator gave no answer.");
        cmw_log("tf: engine failed: %s", eng_msg);
    }
    free(b->in); free(b->out); free(b);
    paint_all();
}
static void engine_open(void) {
    if (engine_state == 1 || engine_state == 2) return;
    engine_state = 1;
    Job *b = calloc(1, sizeof *b); b->j.run = job_open_run; b->j.done = job_open_done;
    cm_work_post(&b->j);
}

/* ------------------------------------------------------------------ Start */
static void job_gen_run(cm_job *j) {
    Job *b = (Job *)j; char e[600] = "";
    if (!E) { snprintf(b->err, sizeof b->err, "The save generator is not running."); return; }
    sg_result r; memset(&r, 0, sizeof r);
    if (sg_generate(E, b->in, &r, e, sizeof e)) { snprintf(b->err, sizeof b->err, "%s", e); return; }
    b->problems = r.problems;
    if (r.problems) {                                                 /* the wiki would not hand the file out: neither do we */
        char e2[200]; jv *m = jparse(r.meta, strlen(r.meta), e2, sizeof e2);
        jv *pr = m ? jget(m, "problems") : NULL;
        size_t k = (size_t)snprintf(b->err, sizeof b->err, "%d problem(s) found - the test world was NOT made.", r.problems);
        for (size_t i = 0; pr && i < pr->cnt && i < 4 && k < sizeof b->err; i++) {
            char t[300]; jtxt(pr->v[i], t, sizeof t); k += (size_t)snprintf(b->err + k, sizeof b->err - k, " %s", t);
        }
        jfree(m);
    } else { b->file = r.file; b->file_n = r.file_n; r.file = NULL; }
    sg_result_free(&r);
}
static void job_gen_done(cm_job *j) {
    Job *b = (Job *)j; char name[300] = "", e[300] = "";
    busy = 0;
    if (!b->file) { snprintf(status, sizeof status, "%s", b->err[0] ? b->err : "The save generator gave no answer."); status_warn = 1; }
    else if (tfw_write(b->file, b->file_n, name, sizeof name, e, sizeof e)) { snprintf(status, sizeof status, "%s", e); status_warn = 1; }
    else if (tfw_loading(), cmw_load_campaign(name)) { snprintf(status, sizeof status, "The game did not take the load. Try again from the title screen."); status_warn = 1; }
    else {
        snprintf(status, sizeof status, "Loading..."); status_warn = 0;
        free(b->in); free(b->file); free(b);
        cmw_close("gallery"); cmw_close("fights");                    /* the game's loading screen takes over */
        return;
    }
    cmw_log("tf: Start failed: %s", status);
    free(b->in); free(b->file); free(b);
    paint_all();
}
static jv *lib;                                                      /* My Units library (mu_load) while the window is open */
static char *builds_json(char *err, size_t errn) {
    if (st.src == 1) {
        if (!st.gal_builds) { snprintf(err, errn, "Pick a build in the Gallery first."); return NULL; }
        return strdup(st.gal_builds);
    }
    if (!st.nids) { snprintf(err, errn, "Pick at least one unit."); return NULL; }
    size_t cap = 1024, k = 0; char *o = malloc(cap); o[k++] = '[';
    for (int i = 0; i < st.nids && i < P.max_squad; i++) {
        const jv *u = lib ? mu_find(lib, st.ids[i]) : NULL; jv *b = u ? jget(u, "build") : NULL;
        size_t n = 0; char *t = b ? jdump(b, 0, &n) : NULL;
        if (!t) { free(o); snprintf(err, errn, "A picked unit is no longer in My Units."); return NULL; }
        if (k + n + 4 > cap) { cap = (k + n + 4) * 2; o = realloc(o, cap); }
        if (i) o[k++] = ',';
        memcpy(o + k, t, n); k += n; free(t);
    }
    o[k++] = ']'; o[k] = 0;
    return o;
}
static void start_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    if (busy || engine_state != 2) return;
    char e[300] = "";
    char *builds = builds_json(e, sizeof e);
    if (!builds) { snprintf(status, sizeof status, "%s", e); status_warn = 1; paint_all(); return; }
    if (!strcmp(mode_id(), "arena") && !st.nmon) { free(builds); snprintf(status, sizeof status, "Add at least one monster to the room."); status_warn = 1; paint_all(); return; }
    size_t cap = strlen(builds) + 4096; char *o = malloc(cap); size_t k = 0;
    char team[300]; jesc(st.src == 1 ? st.gal_title : st.team, team, sizeof team);
    k += (size_t)snprintf(o + k, cap - k, "{\"mode\":\"%s\",\"boss\":\"%s\",\"combat\":\"%s\",\"mgmt\":\"%s\",\"kitInsc\":%s,\"team\":\"%s\",\"kits\":[",
                          mode_id(), P.nbosses ? P.bosses[st.boss].id : "", P.combat[st.combat], P.mgmt[mgmt_index()], st.kit_insc ? "true" : "false", team);
    int first = 1;
    for (int i = 0; i < P.nkits; i++) if (st.kit[i]) { k += (size_t)snprintf(o + k, cap - k, "%s\"%s\"", first ? "" : ",", P.kits[i].id); first = 0; }
    k += (size_t)snprintf(o + k, cap - k, "],\"monsters\":[");
    for (int i = 0; i < st.nmon; i++) k += (size_t)snprintf(o + k, cap - k, "%s{\"k\":\"%s\",\"n\":%d}", i ? "," : "", st.mon[i].k, st.mon[i].n);
    k += (size_t)snprintf(o + k, cap - k, "]");
    if (is_fight() && P.nbosses && P.bosses[st.boss].abyss && P.nmsat) k += (size_t)snprintf(o + k, cap - k, ",\"msat\":%d", P.msat_keys[st.msat]);
    k += (size_t)snprintf(o + k, cap - k, ",\"builds\":%s}", builds);
    free(builds);
    Job *b = calloc(1, sizeof *b); b->j.run = job_gen_run; b->j.done = job_gen_done; b->in = o;
    busy = 1; snprintf(status, sizeof status, "Building your save file..."); status_warn = 0;
    cmw_log("tf: Start %s", mode_id());
    cm_work_post(&b->j);
    paint_all();
}

/* ------------------------------------------------------------------ drawing */
static const DSLUi *U;
static DSLObj body, left, right_sc, right, w_status, w_start;
static float lines_h(const char *s, float w, float size) {           /* height of a wrapped text (rough: 0.5 em per char) */
    float per = w / (size * 0.5f); int n = 1; float c = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) { if ((*p & 0xC0) == 0x80) continue; if (*p == '\n' || ++c > per) { n++; c = 0; } }
    return n * size * 1.3f + 4;
}
static float txt(DSLObj parent, const char *name, float x, float y, float w, const char *s, float size, uint32_t col) {
    float h = lines_h(s, w, size);
    DSLObj t = U->text(parent, x, y, w, h, s, size, DSL_TXT_LEFT | DSL_TXT_WRAP, col);
    if (name) cmw_named(t, CMW_TEXT, name, NULL, NULL, s);
    return h;
}
static float head(DSLObj parent, float x, float y, float w, const char *s) {
    U->text(parent, x, y, w, 28, s, 19, DSL_TXT_LEFT | DSL_TXT_TITLE, GOLD);
    return 32;
}

/* left: the squad */
static void src_click(void *ud, DSLObj w) { (void)w; st.src = (int)(intptr_t)ud; paint_all(); }
static void team_click(void *ud, DSLObj w) {
    (void)w; size_t i = (size_t)(intptr_t)ud; char ids[MU_TEAM_MAX][17]; char name[96];
    int n = lib ? mu_team_at(lib, i, name, sizeof name, ids) : 0;
    st.nids = 0;
    for (int k = 0; k < n && st.nids < P.max_squad; k++) snprintf(st.ids[st.nids++], 17, "%s", ids[k]);
    snprintf(st.team, sizeof st.team, "%s", name);
    status[0] = 0; paint_all();
}
static void unit_toggle(void *ud, DSLObj w) {
    size_t i = (size_t)(intptr_t)ud; mu_info in; if (!lib || mu_info_at(lib, i, &in)) return;
    int at = -1; for (int k = 0; k < st.nids; k++) if (!strcmp(st.ids[k], in.id)) at = k;
    if (at >= 0) { memmove(st.ids[at], st.ids[at + 1], (size_t)(st.nids - at - 1) * 17); st.nids--; }
    else if (st.nids < P.max_squad) snprintf(st.ids[st.nids++], 17, "%s", in.id);
    else { U->toggle_set(w, 0); snprintf(status, sizeof status, "An expedition holds %d units: untick one first.", P.max_squad); status_warn = 1; }
    st.team[0] = 0;
    paint_all();
}
static void gallery_open_click(void *ud, DSLObj w);
static void paint_left(void) {
    cmw_forget("fights:l.");
    if (left) U->destroy(left);
    left = cmw_flat(U->panel(body, LX, 16, LW, BODY_H - 110, 0), 0x00000000);
    float y = head(left, 0, 0, LW, "Squad");
    const char *src_l[2] = { "My Units", "Gallery" };
    for (int s = 0; s < 2; s++) {
        char nm[40]; snprintf(nm, sizeof nm, "fights:l.src%d", s);
        cmw_named(U->button(left, s * (LW / 2 + 4), y, LW / 2 - 4, 38, src_l[s], src_click, (void *)(intptr_t)s), CMW_BUTTON, nm, src_click, (void *)(intptr_t)s, src_l[s]);
    }
    cmw_flat(U->panel(left, st.src * (LW / 2 + 4), y + 40, LW / 2 - 4, 3, 0), GOLD);
    y += 52;
    float lh = BODY_H - 110 - y;
    if (st.src == 0) {
        if (!lib) { txt(left, "fights:l.note", 0, y, LW, "My Units could not be read.", 15, WARN); return; }
        DSLObj sc = U->scroll(left, 0, y, LW, lh); float sy = 0;
        size_t nt = mu_team_count(lib), nu = mu_count(lib);
        if (!nt && !nu) { txt(sc, "fights:l.note", 0, 0, LW - 12, "My Units is empty. Add units there (wiki link, Unit Builder) or pick a build in the Gallery.", 15, SUB); return; }
        for (size_t i = 0; i < nt; i++) {
            char name[96], ids[MU_TEAM_MAX][17], lab[160], nm[40];
            int n = mu_team_at(lib, i, name, sizeof name, ids);
            snprintf(lab, sizeof lab, "%s (%d)", name, n); snprintf(nm, sizeof nm, "fights:l.team%zu", i);
            cmw_named(U->button(sc, 0, sy, LW - 12, 38, lab, team_click, (void *)(intptr_t)i), CMW_BUTTON, nm, team_click, (void *)(intptr_t)i, lab);
            sy += 44;
        }
        for (size_t i = 0; i < nu; i++) {
            mu_info in; if (mu_info_at(lib, i, &in)) continue;
            int on = 0; for (int k = 0; k < st.nids; k++) if (!strcmp(st.ids[k], in.id)) on = 1;
            char lab[160], nm[40]; snprintf(lab, sizeof lab, "%s  Lv %d", in.name, in.lvl); snprintf(nm, sizeof nm, "fights:l.unit%zu", i);
            cmw_named(U->toggle(sc, 0, sy, LW - 12, lab, on, unit_toggle, (void *)(intptr_t)i), CMW_TOGGLE, nm, unit_toggle, (void *)(intptr_t)i, lab);
            sy += 40;
        }
        U->scroll_height(sc, sy);
    } else {
        if (st.gal_builds) {
            y += txt(left, "fights:l.galtitle", 0, y, LW, st.gal_title, 17, MAIN);
            for (int i = 0; i < st.gal_n; i++) y += txt(left, NULL, 12, y, LW - 12, st.gal_names[i], 15, SUB);
            y += 8;
        } else y += txt(left, "fights:l.note", 0, y, LW, "Pick a team or a unit in the Gallery, then press Test Fight there.", 15, SUB) + 8;
        cmw_named(U->button(left, 0, y, LW, 40, "Open the Gallery", gallery_open_click, NULL), CMW_BUTTON, "fights:l.gallery", gallery_open_click, NULL, "Open the Gallery");
    }
}

/* right: the fight */
static void mode_change(void *ud, DSLObj w) { (void)ud; st.mode = U->value_get(w); status[0] = 0; paint_all(); }
static void boss_change(void *ud, DSLObj w) { (void)ud; st.boss = U->value_get(w); paint_all(); }
static void msat_change(void *ud, DSLObj w) { (void)ud; st.msat = U->value_get(w); }
static void combat_change(void *ud, DSLObj w) { (void)ud; st.combat = U->value_get(w); paint_all(); }
static void mgmt_change(void *ud, DSLObj w) { (void)ud; st.mgmt = U->value_get(w); }
static void mgmt_own_change(void *ud, DSLObj w) { (void)ud; int cur = mgmt_index(); st.mgmt_own = U->toggle_get(w); st.mgmt = cur; paint_all(); }
static void kit_change(void *ud, DSLObj w) { st.kit[(int)(intptr_t)ud] = U->toggle_get(w); paint_all(); }
static void insc_change(void *ud, DSLObj w) { (void)ud; st.kit_insc = U->toggle_get(w); paint_all(); }
static void monq_submit(void *ud, DSLObj w) { (void)ud; U->field_get(w, st.monq, sizeof st.monq); paint_all(); }
static void moncount_change(void *ud, DSLObj w) { (void)ud; st.mon_count = U->value_get(w) + 1; }
static void mon_add(void *ud, DSLObj w) {
    (void)w; Mon *m = &P.mons[(int)(intptr_t)ud]; int n = st.mon_count < 1 ? 1 : st.mon_count > 30 ? 30 : st.mon_count;
    for (int i = 0; i < st.nmon; i++) if (!strcmp(st.mon[i].k, m->k)) { st.mon[i].n = st.mon[i].n + n > 30 ? 30 : st.mon[i].n + n; paint_all(); return; }
    if (st.nmon < MAXMON) { snprintf(st.mon[st.nmon].k, sizeof st.mon[0].k, "%s", m->k); st.mon[st.nmon++].n = n; }
    paint_all();
}
static void mon_del(void *ud, DSLObj w) {
    (void)w; int i = (int)(intptr_t)ud; if (i < 0 || i >= st.nmon) return;
    memmove(&st.mon[i], &st.mon[i + 1], (size_t)(st.nmon - i - 1) * sizeof st.mon[0]); st.nmon--; paint_all();
}
static const char *mon_name(const char *k) { for (int i = 0; i < P.nmons; i++) if (!strcmp(P.mons[i].k, k)) return P.mons[i].n; return k; }
static int fold_has(const char *hay, const char *q) {
    if (!*q) return 1;
    size_t n = strlen(q);
    for (const char *h = hay; *h; h++) { size_t i = 0; while (i < n && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)q[i])) i++; if (i == n) return 1; }
    return 0;
}
static void fmt_n(double v, char *o, size_t n) {                     /* 1000000 -> "1,000,000" (fmtN) */
    char d[40]; snprintf(d, sizeof d, "%.0f", v); size_t l = strlen(d), k = 0;
    for (size_t i = 0; i < l && k + 2 < n; i++) { if (i && (l - i) % 3 == 0) o[k++] = ','; o[k++] = d[i]; }
    o[k] = 0;
}
static void paint_right(void) {
    cmw_forget("fights:r.");
    if (right_sc) U->destroy(right_sc);
    right_sc = NULL; right = NULL;
    float y = 0, w = RW - 16;
    if (engine_state != 2) {
        right_sc = cmw_flat(U->panel(body, RX, 16, RW, BODY_H - 110, 0), 0x00000000); right = right_sc;
        const char *m = engine_state == 3 ? eng_msg : "Getting the wiki's Test Fights files...";
        txt(right, "fights:r.engine", 0, 0, w, m, 16, engine_state == 3 ? WARN : SUB);
        return;
    }
    right_sc = cmw_flat(U->panel(body, RX, 16, RW, BODY_H - 110, 0), 0x00000000);   /* holder: one destroy removes it all */
    DSLObj vp = U->scroll(right_sc, 0, 0, RW, BODY_H - 110);
    right = vp;
    /* What do you want */
    y += head(right, 0, y, w, "What do you want");
    const char *ml[6]; char md[600] = "";
    for (int i = 0; i < P.nmodes; i++)
        ml[i] = !strcmp(P.modes[i], "dps") ? "DPS Check" : !strcmp(P.modes[i], "boss") ? "Boss fight" : !strcmp(P.modes[i], "arena") ? "Custom fight" : "Finished base";
    cmw_named(U->value_row(right, 0, y, w, "Test", ml, P.nmodes, st.mode, mode_change, NULL), CMW_VALUE, "fights:r.mode", mode_change, NULL, ml[st.mode]);
    y += 50;
    if (!strcmp(mode_id(), "dps")) { char dur[40]; fmt_n(P.dummy_dur, dur, sizeof dur); snprintf(md, sizeof md, "A practice %s with %s durability that taunts you - hit it and read your squad's damage.", P.dummy_n, dur); }
    else if (!strcmp(mode_id(), "boss")) snprintf(md, sizeof md, "Your squad standing at a boss door, boss alive and never beaten.");
    else if (!strcmp(mode_id(), "arena")) snprintf(md, sizeof md, "The same boss room with the boss taken out and the monsters you choose put in instead.");
    else snprintf(md, sizeof md, "A fully built base with all research done. Your squad walks in ready to play.");
    y += txt(right, "fights:r.modedesc", 12, y, w - 12, md, 15, SUB) + 10;
    /* Which fight / Which room */
    if (is_fight() && P.nbosses) {
        y += head(right, 0, y, w, !strcmp(mode_id(), "boss") ? "Which fight" : "Which room");
        const char *bl[8]; for (int i = 0; i < P.nbosses; i++) bl[i] = P.bosses[i].n;
        cmw_named(U->value_row(right, 0, y, w, "Boss", bl, P.nbosses, st.boss, boss_change, NULL), CMW_VALUE, "fights:r.boss", boss_change, NULL, bl[st.boss]);
        y += 50;
        if (P.bosses[st.boss].abyss && P.nmsat) {
            char lab[12][24]; const char *ll[12];
            for (int i = 0; i < P.nmsat; i++) { snprintf(lab[i], sizeof lab[i], P.msat_keys[i] ? "Day %d" : "Off", P.msat_keys[i]); ll[i] = lab[i]; }
            cmw_named(U->value_row(right, 0, y, w, "Magic Saturation", ll, P.nmsat, st.msat, msat_change, NULL), CMW_VALUE, "fights:r.msat", msat_change, NULL, ll[st.msat]);
            y += 50;
            y += txt(right, NULL, 12, y, w - 12, "The save turns Magic Saturation on and writes the day you pick. Only Abyss monsters get it.", 14, SUB) + 6;
        }
        if (!strcmp(mode_id(), "arena")) {
            y += txt(right, NULL, 12, y, w - 12, "The boss is removed from that room and your monsters take its place.", 14, SUB) + 6;
            cmw_named(U->field(right, 0, y, w - 220, 40, "Search monsters by name...", st.monq, monq_submit, NULL), CMW_FIELD, "fights:r.monq", monq_submit, NULL, st.monq);
            static const char *cnt[30]; static char cb[30][4];
            for (int i = 0; i < 30; i++) { snprintf(cb[i], 4, "%d", i + 1); cnt[i] = cb[i]; }
            cmw_named(U->value_row(right, w - 212, y - 2, 212, "How many", cnt, 30, st.mon_count - 1, moncount_change, NULL), CMW_VALUE, "fights:r.moncount", moncount_change, NULL, cnt[st.mon_count - 1]);
            y += 48;
            int shown = 0;
            for (int i = 0; i < P.nmons && shown < 12; i++) {
                if (!fold_has(P.mons[i].n, st.monq)) continue;
                char nm[40]; snprintf(nm, sizeof nm, "fights:r.hit%d", shown);
                float bx = (shown % 2) * (w / 2 + 2);
                cmw_named(U->button(right, bx, y, w / 2 - 2, 34, P.mons[i].n, mon_add, (void *)(intptr_t)i), CMW_BUTTON, nm, mon_add, (void *)(intptr_t)i, P.mons[i].n);
                if (shown % 2) y += 38;
                shown++;
            }
            if (shown % 2) y += 38;
            if (!shown) y += txt(right, NULL, 12, y, w - 12, "No monster matches.", 14, SUB);
            y += 6;
            for (int i = 0; i < st.nmon; i++) {
                char lab[160], nm[40]; snprintf(lab, sizeof lab, "%s x%d", mon_name(st.mon[i].k), st.mon[i].n);
                txt(right, NULL, 12, y + 6, w - 140, lab, 16, MAIN);
                snprintf(nm, sizeof nm, "fights:r.mondel%d", i);
                cmw_named(U->button(right, w - 120, y, 120, 34, "Remove", mon_del, (void *)(intptr_t)i), CMW_BUTTON, nm, mon_del, (void *)(intptr_t)i, "Remove");
                y += 38;
            }
            y += 8;
        }
    }
    /* Difficulty */
    y += head(right, 0, y, w, "Difficulty");
    const char *cl[8], *gl[8];
    for (int i = 0; i < P.ncombat; i++) cl[i] = P.label[i];
    for (int i = 0; i < P.nmgmt; i++) { gl[i] = P.mgmt[i]; for (int c = 0; c < P.ncombat; c++) if (!strcmp(P.combat[c], P.mgmt[i])) gl[i] = P.label[c]; }
    cmw_named(U->value_row(right, 0, y, w, "Combat", cl, P.ncombat, st.combat, combat_change, NULL), CMW_VALUE, "fights:r.combat", combat_change, NULL, cl[st.combat]);
    y += 50;
    int mi = mgmt_index();
    DSLObj mg = cmw_named(U->value_row(right, 0, y, w, "Management", gl, P.nmgmt, mi, mgmt_change, NULL), CMW_VALUE, "fights:r.mgmt", mgmt_change, NULL, gl[mi]);
    if (!st.mgmt_own) U->set_enabled(mg, 0);
    y += 50;
    cmw_named(U->toggle(right, 0, y, w, "Choose management difficulty separately", st.mgmt_own, mgmt_own_change, NULL), CMW_TOGGLE, "fights:r.mgmtown", mgmt_own_change, NULL, "Choose management difficulty separately");
    y += 42;
    if (!st.mgmt_own) { char t[200]; snprintf(t, sizeof t, "Left off, the economy matches the combat difficulty (%s).", gl[mi]); y += txt(right, NULL, 12, y, w - 12, t, 14, SUB); }
    y += 10;
    /* Battle kits */
    if (P.nkits) {
        y += head(right, 0, y, w, "Battle kits");
        y += txt(right, NULL, 12, y, w - 12, "Each ticked kit is packed into the personal backpack of every character going in, so the squad walks in carrying it.", 14, SUB) + 4;
        int sl = 0, any = 0; double wt = 0;
        for (int i = 0; i < P.nkits; i++) {
            char lab[200], nm[40]; snprintf(lab, sizeof lab, "%s  (%d slot(s), %g weight)", P.kits[i].n, P.kits[i].slots, P.kits[i].w);
            snprintf(nm, sizeof nm, "fights:r.kit%d", i);
            cmw_named(U->toggle(right, 0, y, w, lab, st.kit[i], kit_change, (void *)(intptr_t)i), CMW_TOGGLE, nm, kit_change, (void *)(intptr_t)i, lab);
            y += 40;
            if (st.kit[i]) { sl += P.kits[i].slots; wt += P.kits[i].w; any = 1; }
        }
        char t[300];
        if (!any) snprintf(t, sizeof t, "No kit ticked - the squad walks in with nothing but its gear.");
        else snprintf(t, sizeof t, "%d slot(s) and %g weight per character.", sl, (double)((long long)(wt * 100 + 0.5)) / 100);
        y += txt(right, NULL, 12, y, w - 12, t, 14, SUB) + 4;
        if (any && P.insc_n[0]) {
            char lab[200]; snprintf(lab, sizeof lab, "Grant %s on custom saves", P.insc_n);
            cmw_named(U->toggle(right, 0, y, w, lab, st.kit_insc, insc_change, NULL), CMW_TOGGLE, "fights:r.insc", insc_change, NULL, lab);
            y += 42;
            y += txt(right, NULL, 12, y, w - 12, "Added by default so every unit has enough carry weight for the battle kits.", 14, SUB);
            if (st.kit_insc) snprintf(t, sizeof t, "Every character gains %s so the load is carryable.", P.insc_n);
            else snprintf(t, sizeof t, "Inscription off. Units may be overweight if you keep the kits packed.");
            y += txt(right, NULL, 12, y, w - 12, t, 14, SUB);
        }
        for (int i = 0; i < P.nmiss; i++) y += txt(right, NULL, 12, y, w - 12, P.miss[i], 14, WARN);
    }
    U->scroll_height(vp, y + 16);
}
static void paint_status(void) {
    char sq[400]; int n = st.src == 1 ? (st.gal_builds ? st.gal_n : 0) : st.nids;
    if (n > P.max_squad && P.max_squad) n = P.max_squad;
    snprintf(sq, sizeof sq, "Squad: %d / %d", n, P.max_squad ? P.max_squad : 4);
    char all[1700]; snprintf(all, sizeof all, "%s%s%s", sq, status[0] ? "\n" : "", status);
    if (w_status) U->destroy(w_status);                               /* the kit has no colour setter: a problem shows orange */
    w_status = cmw_named(U->text(body, 24, BODY_H - 76, WIN_W - 300, 70, all, 15, DSL_TXT_LEFT | DSL_TXT_WRAP, status_warn ? WARN : MAIN), CMW_TEXT, "fights:status", NULL, NULL, all);
    U->set_enabled(w_start, engine_state == 2 && !busy);
    cmw_set_text(w_start, busy ? "Working..." : "Start");
}
static void paint_all(void) {
    if (!cmw_is_open("fights") || !body) return;
    paint_left(); paint_right(); paint_status();
}
static void closed(void *ud) {
    (void)ud; body = left = right = right_sc = w_status = w_start = NULL;
    if (lib) { jfree(lib); lib = NULL; }
    cmw_forget("fights:");
}
static void retry_click(void *ud, DSLObj w) { (void)ud; (void)w; if (engine_state == 3) { engine_state = 0; engine_open(); paint_all(); } }
static void build_window(void) {
    U = cmw_ui(); if (!U) return;
    body = cmw_open("fights", "Test Fights", WIN_W, WIN_H, closed, NULL);
    if (!body) return;
    left = right = right_sc = NULL;
    char e[300]; if (lib) jfree(lib);
    lib = mu_load(e, sizeof e);
    if (!lib) cmw_log("tf: My Units: %s", e);
    cmw_flat(U->panel(body, 24, BODY_H - 84, WIN_W - 48, 1, 0), 0x633D33FF);
    w_status = NULL;
    w_start = cmw_named(U->button(body, WIN_W - 24 - 220, BODY_H - 72, 220, 52, "Start", start_click, NULL), CMW_BUTTON, "fights:start", start_click, NULL, "Start");
    if (engine_state == 3) cmw_named(U->button(body, WIN_W - 24 - 220 - 180, BODY_H - 66, 170, 40, "Try again", retry_click, NULL), CMW_BUTTON, "fights:retry", retry_click, NULL, "Try again");
    engine_open();
    paint_all();
}
void tf_open(void) { build_window(); }
void tf_open_with_builds(const char *builds_json, const char *title, const char *const *names, int n) {
    free(st.gal_builds); st.gal_builds = builds_json ? strdup(builds_json) : NULL;
    snprintf(st.gal_title, sizeof st.gal_title, "%s", title ? title : "");
    st.gal_n = 0;
    for (int i = 0; i < n && i < 8; i++) snprintf(st.gal_names[st.gal_n++], 96, "%s", names[i] ? names[i] : "");
    st.src = 1; status[0] = 0;
    build_window();
}
extern void gal_open(void);
static void gallery_open_click(void *ud, DSLObj w) { (void)ud; (void)w; gal_open(); }

static void test_state(char *out, size_t n) {
    snprintf(out, n, "\"tf\":{\"engine\":%d,\"busy\":%d,\"mode\":\"%s\",\"squad\":%d,\"src\":%d,\"status\":\"%.300s\",\"tag\":\"%s\"}",
             engine_state, busy, P.ok ? mode_id() : "", st.src == 1 ? st.gal_n : st.nids, st.src, status, P.tag);
    for (char *p = out; *p; p++) if (*p == '\n') *p = ' ';
}

/* ------------------------------------------------------------------ entry point (called by cm_module.c) */
__attribute__((visibility("default"))) void cm_fights_init(const DSLHost *host, DSLPlugin *self) {
    if (cmw_init(host, self)) return;
    if (cm_work_start()) { cmw_log("tf: worker thread did not start: Test Fights is off"); return; }
    tfw_init(host, self);
    cmw_test_opener("fights", tf_open);
    cmw_test_state(test_state);
    cm_home_add("test-fights", "Test Fights", NULL, 40, tf_open);
}
