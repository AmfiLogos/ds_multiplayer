/* myunits - the My Units library file (see myunits.h). Pure C. */
#include "myunits.h"
#include "pvp_build.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

static char data_dir[1024];
void mu_set_data_dir(const char *p) { snprintf(data_dir, sizeof data_dir, "%s", p ? p : ""); }

void mu_lib_path(char *out, size_t n) {
    const char *e = getenv("DSMP_UNITS_LIB");
    if (e && *e) { snprintf(out, n, "%s", e); return; }
#ifdef _WIN32
    if (data_dir[0]) { snprintf(out, n, "%s\\Mods\\units\\library.json", data_dir); return; }
    const char *up = getenv("USERPROFILE");
    snprintf(out, n, "%s\\AppData\\LocalLow\\CanOpener\\Dungeon Settlers\\Mods\\units\\library.json", up ? up : ".");
#else
    /* the REAL home: a play copy runs with CFFIXED_USER_HOME (its own save folder), and every copy and the launcher must
       see the one library. getpwuid does not read that variable. */
    struct passwd *pw = getpwuid(getuid());
    const char *home = pw && pw->pw_dir ? pw->pw_dir : getenv("HOME");
    if (home && *home) snprintf(out, n, "%s/Library/Application Support/CanOpener/Dungeon Settlers/Mods/units/library.json", home);
    else snprintf(out, n, "%s/Mods/units/library.json", data_dir);
#endif
}

static void mkdirs_for(const char *file) {
    char d[1200]; snprintf(d, sizeof d, "%s", file);
    for (char *p = d + 1; *p; p++) if (*p == '/' || *p == '\\') {
        char c = *p; *p = 0;
#ifdef _WIN32
        if (!(p == d + 2 && d[1] == ':')) _mkdir(d);
#else
        mkdir(d, 0755);
#endif
        *p = c;
    }
}

static uint64_t now_ms(void) {
    struct timespec ts; timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
static void new_id(char out[17]) {                                       /* 16 hex, as store.rs new_id */
    static uint64_t ctr;
    uint64_t seed = 0;
    FILE *f = fopen("/dev/urandom", "rb"); if (f) { if (fread(&seed, 1, sizeof seed, f) != sizeof seed) seed = 0; fclose(f); }
    uint64_t z = seed ^ (now_ms() << 16) ^ (++ctr * 0x9E3779B97F4A7C15ULL) ^ (uint64_t)(uintptr_t)&ctr;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    snprintf(out, 17, "%016llx", (unsigned long long)(z ^ (z >> 31)));
}

static jv *empty_lib(void) {
    jv *l = jobj();
    jset(l, "format", jstrv(MU_FORMAT)); jset(l, "version", jnum_i(MU_VERSION)); jset(l, "game_build", jstrv("25846389"));
    jset(l, "units", jarr()); jset(l, "teams", jarr());
    return l;
}
jv *mu_load(char *err, size_t errn) {
    char p[1200]; mu_lib_path(p, sizeof p);
    struct stat st;
    if (stat(p, &st) != 0) return empty_lib();
    char e[300]; jv *l = jload(p, e, sizeof e);
    if (!l) { snprintf(err, errn, "My Units file is damaged (%s): %s", p, e); return NULL; }
    if (!jstreq(jget(l, "format"), MU_FORMAT)) { snprintf(err, errn, "My Units file has another format: %s", p); jfree(l); return NULL; }
    if (jnumv(jget(l, "version")) > MU_VERSION) { snprintf(err, errn, "My Units file was written by a newer launcher (version %d)", (int)jnumv(jget(l, "version"))); jfree(l); return NULL; }
    if (!jis(jget(l, "units"), JN_ARR)) jset(l, "units", jarr());
    if (!jis(jget(l, "teams"), JN_ARR)) jset(l, "teams", jarr());
    return l;
}
int mu_save(const jv *lib, char *err, size_t errn) {
    char p[1200], tmp[1300], bak[1300]; mu_lib_path(p, sizeof p);
    snprintf(tmp, sizeof tmp, "%s.tmp", p); snprintf(bak, sizeof bak, "%s.bak", p);
    mkdirs_for(p);
    size_t n; char *s = jdump(lib, 2, &n);
    FILE *f = fopen(tmp, "wb");
    if (!f) { snprintf(err, errn, "could not write %s: %s", tmp, strerror(errno)); free(s); return -1; }
    int bad = fwrite(s, 1, n, f) != n; bad |= fflush(f) != 0; bad |= fclose(f) != 0; free(s);
    if (bad) { snprintf(err, errn, "could not write %s", tmp); return -1; }
    struct stat st;
    if (stat(p, &st) == 0) {                                              /* the previous file stays as .bak */
#ifdef _WIN32
        remove(bak);
#endif
        if (rename(p, bak) != 0) { snprintf(err, errn, "could not keep the backup %s: %s", bak, strerror(errno)); return -1; }
    }
    if (rename(tmp, p) != 0) { snprintf(err, errn, "could not write %s: %s", p, strerror(errno)); return -1; }
    return 0;
}

static void raw_cpy(char *o, size_t n, const jv *v) { if (v && v->t == JN_STR) snprintf(o, n, "%.*s", (int)v->n, v->s); else if (n) o[0] = 0; }
size_t mu_count(const jv *lib) { jv *u = jget(lib, "units"); return u && u->t == JN_ARR ? u->cnt : 0; }
int mu_info_at(const jv *lib, size_t i, mu_info *o) {
    memset(o, 0, sizeof *o);
    jv *u = jget(lib, "units"); if (!u || i >= u->cnt) return -1;
    jv *x = u->v[i], *b = jget(x, "build");
    raw_cpy(o->id, sizeof o->id, jget(x, "id"));
    jv *nm = jget(x, "name"); if (!nm || nm->t != JN_STR || !nm->n) nm = jget(b, "name");
    raw_cpy(o->name, sizeof o->name, nm);
    for (char *p = o->name; *p; p++) if (*p == '\\' || *p == '"') *p = ' ';   /* raw text: no escapes on screen */
    raw_cpy(o->race, sizeof o->race, jget(b, "race"));
    raw_cpy(o->portrait, sizeof o->portrait, jget(b, "portrait"));
    o->lvl = (int)jnumv(jget(b, "lvl"));
    if (!o->name[0]) snprintf(o->name, sizeof o->name, "%s", !strncmp(o->race, "AFFECTER_", 9) ? o->race + 9 : o->race);
    return 0;
}
const jv *mu_find(const jv *lib, const char *id) {
    jv *u = jget(lib, "units");
    for (size_t i = 0; u && i < u->cnt; i++) if (jstreq(jget(u->v[i], "id"), id)) return u->v[i];
    return NULL;
}
size_t mu_team_count(const jv *lib) { jv *t = jget(lib, "teams"); return t && t->t == JN_ARR ? t->cnt : 0; }
int mu_team_at(const jv *lib, size_t i, char *name, size_t namen, char ids[][17]) {
    jv *t = jget(lib, "teams"); if (!t || i >= t->cnt) { if (namen) name[0] = 0; return 0; }
    raw_cpy(name, namen, jget(t->v[i], "name"));
    for (char *p = name; *p; p++) if (*p == '\\' || *p == '"') *p = ' ';
    jv *ui = jget(t->v[i], "unit_ids"); int n = 0;
    for (size_t k = 0; ui && ui->t == JN_ARR && k < ui->cnt && n < MU_TEAM_MAX; k++) {
        jv *id = ui->v[k]; if (!id || id->t != JN_STR || id->n != 16) continue;
        char s[17]; memcpy(s, id->s, 16); s[16] = 0;
        if (mu_find(lib, s)) memcpy(ids[n++], s, 17);
    }
    return n;
}

/* a UTF-8 text as a raw JSON string body (escapes) */
static jv *jstr_text(const char *t) {
    size_t n = strlen(t); char *o = malloc(n * 6 + 1), *q = o;
    for (const unsigned char *p = (const unsigned char *)t; *p; p++) {
        if (*p == '"' || *p == '\\') { *q++ = '\\'; *q++ = (char)*p; }
        else if (*p < 32) q += snprintf(q, 8, "\\u%04x", *p);
        else *q++ = (char)*p;
    }
    *q = 0; jv *v = jstrv(o); free(o); return v;
}
int mu_add_team(jv *lib, const wk_build *b, int n, const char *team_name, char ids_out[][17], char *err, size_t errn) {
    if (n < 1 || n > MU_TEAM_MAX) { snprintf(err, errn, "a team holds 1 to %d units", MU_TEAM_MAX); return -1; }
    jv *units = jget(lib, "units"), *teams = jget(lib, "teams");
    if (!units || !teams) { snprintf(err, errn, "not a My Units library"); return -1; }
    char *js = wk_json(b, n); char pe[200];
    jv *arr = jparse(js, strlen(js), pe, sizeof pe); free(js);
    if (!arr || arr->t != JN_ARR || (int)arr->cnt != n) { snprintf(err, errn, "internal: unit JSON: %s", pe); jfree(arr); return -1; }
    uint64_t t = now_ms(); jv *tids = jarr();
    char tname[200] = "";
    for (int i = 0; i < n; i++) {
        char id[17]; new_id(id);
        jv *u = jobj(); jset(u, "id", jstrv(id));
        const char *nm = b[i].name[0] ? b[i].name : (!strncmp(wk_race_key(b[i].race), "AFFECTER_", 9) ? wk_race_key(b[i].race) + 9 : wk_race_key(b[i].race));
        char nm2[96]; if (b[i].name[0]) snprintf(nm2, sizeof nm2, "%s", nm); else snprintf(nm2, sizeof nm2, "%s %d", nm, b[i].lvl);
        jset(u, "name", jstr_text(nm2)); jset(u, "created", jnum_i((long long)t)); jset(u, "updated", jnum_i((long long)t));
        jset(u, "build", jcopy(arr->v[i]));
        jpush(units, u); jpush(tids, jstrv(id));
        if (ids_out) memcpy(ids_out[i], id, 17);
        if (i < 3) { size_t L = strlen(tname); snprintf(tname + L, sizeof tname - L, "%s%s", i ? ", " : "", nm2); }
        else if (i == 3) { size_t L = strlen(tname); snprintf(tname + L, sizeof tname - L, " +%d", n - 3); }
    }
    jfree(arr);
    char tid[17]; new_id(tid);
    jv *tm = jobj(); jset(tm, "id", jstrv(tid)); jset(tm, "name", jstr_text(team_name && *team_name ? team_name : tname)); jset(tm, "unit_ids", tids);
    jpush(teams, tm);
    return n;
}
int mu_add_build(jv *lib, const jv *build, const char *name, char id_out[17], char *err, size_t errn) {
    jv *units = jget(lib, "units");
    if (!units) { snprintf(err, errn, "not a My Units library"); return -1; }
    wk_build b; char e[300];
    if (wk_from_json(build, &b, e, sizeof e)) { snprintf(err, errn, "%s", e); return -1; }
    char nm[96];
    if (name && *name) snprintf(nm, sizeof nm, "%s", name);
    else if (b.name[0]) snprintf(nm, sizeof nm, "%s", b.name);
    else { const char *r = wk_race_key(b.race); snprintf(nm, sizeof nm, "%s %d", !strncmp(r, "AFFECTER_", 9) ? r + 9 : r, b.lvl); }
    uint64_t t = now_ms(); char id[17]; new_id(id);
    jv *u = jobj(); jset(u, "id", jstrv(id)); jset(u, "name", jstr_text(nm));
    jset(u, "created", jnum_i((long long)t)); jset(u, "updated", jnum_i((long long)t)); jset(u, "build", jcopy(build));
    jpush(units, u);
    if (id_out) memcpy(id_out, id, 17);
    return 0;
}
int mu_rename(jv *lib, const char *id, const char *name) {
    jv *u = (jv *)mu_find(lib, id); if (!u || !name || !*name) return -1;
    jset(u, "name", jstr_text(name)); jset(u, "updated", jnum_i((long long)now_ms()));
    return 0;
}
int mu_delete(jv *lib, const char *id) {
    jv *units = jget(lib, "units"), *teams = jget(lib, "teams"); size_t i;
    for (i = 0; units && i < units->cnt; i++) if (jstreq(jget(units->v[i], "id"), id)) break;
    if (!units || i >= units->cnt) return -1;
    jarr_del(units, i);
    for (size_t t = teams ? teams->cnt : 0; t-- > 0; ) {
        jv *ui = jget(teams->v[t], "unit_ids");
        for (size_t k = ui && ui->t == JN_ARR ? ui->cnt : 0; k-- > 0; ) if (jstreq(ui->v[k], id)) jarr_del(ui, k);
        if (!ui || ui->t != JN_ARR || !ui->cnt) jarr_del(teams, t);
    }
    return 0;
}
int mu_team_new(jv *lib, const char *name, char ids[][17], int n, char tid_out[17], char *err, size_t errn) {
    jv *teams = jget(lib, "teams");
    if (!teams) { snprintf(err, errn, "not a My Units library"); return -1; }
    if (n < 1 || n > MU_TEAM_MAX) { snprintf(err, errn, "a team holds 1 to %d units", MU_TEAM_MAX); return -1; }
    for (int i = 0; i < n; i++) if (!mu_find(lib, ids[i])) { snprintf(err, errn, "unit %s is not in My Units any more", ids[i]); return -1; }
    char tname[200] = "";
    if (name && *name) snprintf(tname, sizeof tname, "%s", name);
    else for (int i = 0; i < n && i < 4; i++) {
        mu_info inf; size_t L = strlen(tname);
        for (size_t k = 0; k < mu_count(lib); k++) if (!mu_info_at(lib, k, &inf) && !strcmp(inf.id, ids[i])) break;
        if (i < 3) snprintf(tname + L, sizeof tname - L, "%s%s", i ? ", " : "", inf.name);
        else snprintf(tname + L, sizeof tname - L, " +%d", n - 3);
    }
    jv *tids = jarr(); for (int i = 0; i < n; i++) jpush(tids, jstrv(ids[i]));
    char tid[17]; new_id(tid);
    jv *tm = jobj(); jset(tm, "id", jstrv(tid)); jset(tm, "name", jstr_text(tname)); jset(tm, "unit_ids", tids);
    jpush(teams, tm);
    if (tid_out) memcpy(tid_out, tid, 17);
    return 0;
}
int mu_team_delete(jv *lib, const char *tid) {
    jv *teams = jget(lib, "teams");
    for (size_t t = 0; teams && t < teams->cnt; t++) if (jstreq(jget(teams->v[t], "id"), tid)) { jarr_del(teams, t); return 0; }
    return -1;
}
int mu_team_id_at(const jv *lib, size_t i, char out[17]) {
    jv *t = jget(lib, "teams"); out[0] = 0;
    if (!t || i >= t->cnt) return -1;
    raw_cpy(out, 17, jget(t->v[i], "id"));
    return out[0] ? 0 : -1;
}
int mu_build_of(const jv *lib, const char *id, wk_build *out, char *err, size_t errn) {
    const jv *u = mu_find(lib, id);
    if (!u) { snprintf(err, errn, "unit %s is not in My Units any more", id); return -1; }
    char e[300];
    if (wk_from_json(jget(u, "build"), out, e, sizeof e)) { snprintf(err, errn, "%s", e); return -1; }
    /* the library name wins over the build's own name field (the launcher renames the entry, not the build) */
    jv *nm = jget(u, "name");
    if (nm && nm->t == JN_STR && nm->n) {
        char t[64]; snprintf(t, sizeof t, "%.*s", (int)(nm->n < 48 ? nm->n : 48), nm->s);
        int bad = strchr(t, '\\') != NULL;                                /* escaped text: keep the build's own name */
        if (!bad) { size_t L = strlen(t); while (L > 0 && ((unsigned char)t[L] & 0xC0) == 0x80) L--; t[L] = 0; snprintf(out->name, sizeof out->name, "%s", t); }
    }
    return 0;
}
jv *mu_entries(const jv *lib, char ids[][17], int n, const char *player_uid, char *notes, size_t notesn, char *err, size_t errn) {
    if (notes && notesn) notes[0] = 0;
    if (n < 1) { snprintf(err, errn, "no units picked"); return NULL; }
    jv *entries = jarr();
    for (int i = 0; i < n; i++) {
        wk_build b; char e[400], nt[600] = "";
        mu_info inf; memset(&inf, 0, sizeof inf);
        const jv *u = mu_find(lib, ids[i]); if (u) raw_cpy(inf.name, sizeof inf.name, jget(u, "name"));
        if (mu_build_of(lib, ids[i], &b, e, sizeof e)) { snprintf(err, errn, "%s: %s", inf.name[0] ? inf.name : ids[i], e); jfree(entries); return NULL; }
        jv *en = pb_entry(&b, player_uid, nt, sizeof nt, e, sizeof e);
        if (!en) { snprintf(err, errn, "%s: %s", inf.name[0] ? inf.name : ids[i], e); jfree(entries); return NULL; }
        jset(jget(en, "manifest"), "library_id", jstrv(ids[i]));
        jset(jget(en, "manifest"), "home_save_name", jstrv("My Units"));
        if (nt[0] && notes && strlen(notes) + strlen(nt) + 80 < notesn) { size_t L = strlen(notes); snprintf(notes + L, notesn - L, "%s%s: %s", L ? "; " : "", inf.name, nt); }
        jpush(entries, en);
    }
    return entries;
}
