/* gallery - see gallery.h */
#include "gallery.h"
#include "../fights/cm_http.h"
#include "../../src/fe/fe_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define GAL_LIST_MAX ((size_t)4 << 20)
#define GAL_ITEM_MAX ((size_t)1 << 20)
#define GAL_WORDS_MAX ((size_t)4 << 20)
#define GAL_TIMEOUT_MS 15000

static const char *MAJORS[6] = { "Strength", "Constitution", "WillPower", "Intelligence", "Agility", "Perception" };

/* ---- JSON string (raw, escapes kept by fe_json) -> UTF-8 */
static void put_utf8(char *o, size_t n, size_t *k, unsigned cp) {
    char b[4]; int L;
    if (cp < 0x80) { b[0] = (char)cp; L = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 63)); L = 2; }
    else if (cp < 0x10000) { b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | (cp >> 6 & 63)); b[2] = (char)(0x80 | (cp & 63)); L = 3; }
    else { b[0] = (char)(0xF0 | cp >> 18); b[1] = (char)(0x80 | (cp >> 12 & 63)); b[2] = (char)(0x80 | (cp >> 6 & 63)); b[3] = (char)(0x80 | (cp & 63)); L = 4; }
    if (*k + (size_t)L < n) { memcpy(o + *k, b, (size_t)L); *k += (size_t)L; }
}
static unsigned hex4(const char *s) { unsigned v = 0; for (int i = 0; i < 4; i++) { int c = s[i]; v = v * 16 + (unsigned)(isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10)); } return v; }
static void unesc(const char *s, size_t len, char *o, size_t n) {
    size_t k = 0;
    if (!n) return;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c != '\\' || i + 1 >= len) { if (k + 1 < n) o[k++] = c; continue; }
        c = s[++i];
        switch (c) {
        case 'n': c = '\n'; break; case 't': c = '\t'; break; case 'r': c = '\r'; break;
        case 'b': c = '\b'; break; case 'f': c = '\f'; break;
        case 'u':
            if (i + 4 < len) {
                unsigned cp = hex4(s + i + 1); i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < len && s[i + 1] == '\\' && s[i + 2] == 'u') {
                    unsigned lo = hex4(s + i + 3);
                    if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
                }
                put_utf8(o, n, &k, cp);
            }
            continue;
        default: break;                         /* \" \\ \/ */
        }
        if (k + 1 < n) o[k++] = c;
    }
    o[k] = 0;
}
static void str_of(const jv *v, char *o, size_t n) {
    if (!n) return;
    o[0] = 0;
    if (v && v->t == JN_STR) unesc(v->s, v->n, o, n);
}
static int int_of(const jv *v) { return v && v->t == JN_NUM ? (int)(jnumv(v) + (jnumv(v) < 0 ? -0.5 : 0.5)) : 0; }
static int arr_strs(const jv *a, char *base, size_t stride, int max) {
    int k = 0;
    if (!a || a->t != JN_ARR) return 0;
    for (size_t i = 0; i < a->cnt && k < max; i++) if (a->v[i]->t == JN_STR) str_of(a->v[i], base + stride * (size_t)k++, stride);
    return k;
}

/* ---- URL */
static int url_add(char *url, size_t n, size_t *k, const char *fmt, const char *val) {
    char enc[512]; size_t e = 0;
    for (const char *s = val; *s && e + 4 < sizeof enc; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '_' || c == '-' || c == '.') enc[e++] = (char)c;
        else e += (size_t)snprintf(enc + e, sizeof enc - e, "%%%02X", c);
    }
    enc[e] = 0;
    int w = snprintf(url + *k, n - *k, fmt, enc);
    if (w < 0 || *k + (size_t)w >= n) return -1;
    *k += (size_t)w;
    return 0;
}
static int url_list(char *url, size_t n, size_t *k, const char *name, const char *const *v, int cnt) {
    if (cnt <= 0) return 0;
    char fmt[64]; snprintf(fmt, sizeof fmt, "&%s=%%s", name);
    if (url_add(url, n, k, fmt, v[0])) return -1;
    for (int i = 1; i < cnt; i++) if (url_add(url, n, k, ",%s", v[i])) return -1;   /* the server splits on ',' */
    return 0;
}
int gal_list_url(const gal_query *q, char *url, size_t n) {
    size_t k = 0;
    int w = snprintf(url, n, CM_WIKI "/api/gallery?kind=%s&sort=%s", q->chars ? "char" : "team", q->recent ? "recent" : "popular");
    if (w < 0 || (size_t)w >= n) return -1;
    k = (size_t)w;
    if (url_list(url, n, &k, "race", q->races, q->nraces) || url_list(url, n, &k, "tree", q->trees, q->ntrees) ||
        url_list(url, n, &k, "has", q->has, q->nhas)) return -1;
    if (q->incomplete && url_add(url, n, &k, "&complete=%s", "0")) return -1;
    if (q->by && url_add(url, n, &k, "&by=%s", q->by)) return -1;
    if (q->page > 0) { char p[16]; snprintf(p, sizeof p, "%d", q->page > GAL_MAX_PAGE ? GAL_MAX_PAGE : q->page); if (url_add(url, n, &k, "&page=%s", p)) return -1; }
    return 0;
}

/* ---- one item */
static void read_item(const jv *o, gal_item *it) {
    memset(it, 0, sizeof *it);
    str_of(jget(o, "id"), it->id, sizeof it->id);
    char k[16]; str_of(jget(o, "k"), k, sizeof k); it->team = !strcmp(k, "team");
    str_of(jget(o, "c"), it->created, sizeof it->created);
    str_of(jget(o, "e"), it->edited, sizeof it->edited);
    str_of(jget(o, "gb"), it->game_build, sizeof it->game_build);
    it->opens = int_of(jget(o, "o")); it->copies = int_of(jget(o, "cp"));
    it->likes = int_of(jget(o, "lk")); it->bookmarks = int_of(jget(o, "bm"));
    const jv *f = jget(o, "f"), *nm = jget(f, "name");
    str_of(jget(nm, "race"), it->name_race, sizeof it->name_race);
    str_of(jget(nm, "noun"), it->name_noun, sizeof it->name_noun);
    it->name_ntrees = arr_strs(jget(nm, "trees"), it->name_trees[0], sizeof it->name_trees[0], 4);
    str_of(jget(f, "focus"), it->focus, sizeof it->focus);
    it->complete = jtruthy(jget(f, "complete"));
    it->min_level = int_of(jget(f, "minl"));
    const jv *races = jget(f, "races"), *u = jget(o, "u");
    for (size_t i = 0; u && u->t == JN_ARR && i < u->cnt && it->nunits < GAL_MAX_UNITS; i++) {
        const jv *x = u->v[i];
        gal_unit *g = &it->units[it->nunits++];
        /* the gallery page names a unit by f.races[i] (builds/index.html); u.r only when that is missing */
        if (races && races->t == JN_ARR && i < races->cnt) str_of(races->v[i], g->race, sizeof g->race);
        if (!g->race[0]) str_of(jget(x, "r"), g->race, sizeof g->race);
        str_of(jget(x, "b"), g->bg, sizeof g->bg);
        str_of(jget(x, "p"), g->portrait, sizeof g->portrait);
        str_of(jget(x, "npc"), g->npc, sizeof g->npc);
        g->lvl = int_of(jget(x, "l")); g->hp = int_of(jget(x, "hp")); g->pa = int_of(jget(x, "pa")); g->ma = int_of(jget(x, "ma"));
        g->ntrees = arr_strs(jget(x, "t"), g->trees[0], sizeof g->trees[0], 8);
        g->nskills = arr_strs(jget(x, "s"), g->skills[0], sizeof g->skills[0], 48);
        g->ninsc = arr_strs(jget(x, "i"), g->insc[0], sizeof g->insc[0], 16);
        g->ngear = arr_strs(jget(x, "g"), g->gear[0], sizeof g->gear[0], 16);
        const jv *m = jget(x, "m"), *mt = jget(x, "mt");
        for (int j = 0; j < 6; j++) { g->majors[j] = int_of(jget(m, MAJORS[j])); str_of(jget(mt, MAJORS[j]), g->talents[j], sizeof g->talents[j]); }
    }
    const jv *au = jget(o, "au");
    str_of(jget(au, "n"), it->au_name, sizeof it->au_name);
    const jv *p = jget(au, "p");
    if (p && p->t == JN_ARR && p->cnt == 3) {
        str_of(p->v[0], it->au_race, sizeof it->au_race); str_of(p->v[1], it->au_bg, sizeof it->au_bg); it->au_n = int_of(p->v[2]);
        it->au_pseudo = 1;
    }
    str_of(jget(au, "aid"), it->au_id, sizeof it->au_id);
    str_of(jget(au, "fl"), it->au_flag, sizeof it->au_flag);
    it->ntags = arr_strs(jget(o, "tags"), it->tags[0], sizeof it->tags[0], 8);
}

int gal_parse_page(const char *json, size_t n, gal_page *out, char *err, size_t errn) {
    memset(out, 0, sizeof *out);
    jv *r = jparse(json, n, err, errn);
    if (!r) return -1;
    const jv *items = jget(r, "items");
    if (!items || items->t != JN_ARR) { jfree(r); snprintf(err, errn, "the gallery answer has no items"); return -1; }
    out->items = calloc(items->cnt ? items->cnt : 1, sizeof *out->items);
    if (!out->items) { jfree(r); snprintf(err, errn, "out of memory"); return -1; }
    for (size_t i = 0; i < items->cnt; i++) if (items->v[i]->t == JN_OBJ) read_item(items->v[i], &out->items[out->n++]);
    out->more = jtruthy(jget(r, "more"));
    jfree(r);
    return 0;
}

int gal_parse_item(const char *json, size_t n, gal_item *out, char **blob, char *err, size_t errn) {
    if (blob) *blob = NULL;
    jv *r = jparse(json, n, err, errn);
    if (!r) return -1;
    if (r->t != JN_OBJ || !jget(r, "id")) { jfree(r); snprintf(err, errn, "this build is no longer in the gallery"); return -1; }
    read_item(r, out);
    const jv *b = jget(r, "blob");
    if (blob && b && b->t == JN_STR) {
        *blob = malloc(b->n + 1);
        if (*blob) unesc(b->s, b->n, *blob, b->n + 1);
    }
    jfree(r);
    return 0;
}

void gal_page_free(gal_page *p) { if (p) { free(p->items); memset(p, 0, sizeof *p); } }

static int fetch_json(const char *url, size_t max, cmh_resp *r, char *err, size_t errn) {
    if (cmh_get(url, GAL_TIMEOUT_MS, max, r, err, errn)) return -1;
    if (r->status != 200) {
        snprintf(err, errn, r->status == 404 ? "this build is no longer in the gallery" : "the gallery answered %d", r->status);
        cmh_free(r); return -1;
    }
    return 0;
}
int gal_fetch_page(const gal_query *q, gal_page *out, char *err, size_t errn) {
    char url[2048]; cmh_resp r;
    if (gal_list_url(q, url, sizeof url)) { snprintf(err, errn, "too many filters"); return -1; }
    if (fetch_json(url, GAL_LIST_MAX, &r, err, errn)) return -1;
    int rc = gal_parse_page(r.body, r.n, out, err, errn);
    cmh_free(&r);
    return rc;
}
int gal_fetch_item(const char *id, gal_item *out, char **blob, char *err, size_t errn) {
    for (const char *s = id; *s; s++) if (!isalnum((unsigned char)*s) && *s != '-') { snprintf(err, errn, "bad gallery id"); return -1; }
    char url[256]; cmh_resp r;
    snprintf(url, sizeof url, CM_WIKI "/api/gallery?id=%s&full=1", id);
    if (fetch_json(url, GAL_ITEM_MAX, &r, err, errn)) return -1;
    int rc = gal_parse_item(r.body, r.n, out, blob, err, errn);
    cmh_free(&r);
    return rc;
}

/* ---- words + names (site/assets/teamname.js) */
struct gal_words { jv *root; const jv *W; };

gal_words *gal_words_parse(const char *json, size_t n, char *err, size_t errn) {
    jv *r = jparse(json, n, err, errn);
    if (!r) return NULL;
    const jv *W = jget(r, "W");
    if (!W || W->t != JN_OBJ) { jfree(r); snprintf(err, errn, "the gallery words have no W"); return NULL; }
    gal_words *w = calloc(1, sizeof *w);
    if (!w) { jfree(r); return NULL; }
    w->root = r; w->W = W;
    return w;
}
gal_words *gal_words_fetch(char *err, size_t errn) {
    cmh_resp r;
    if (fetch_json(CM_WIKI "/builds/words.json", GAL_WORDS_MAX, &r, err, errn)) return NULL;
    gal_words *w = gal_words_parse(r.body, r.n, err, errn);
    cmh_free(&r);
    return w;
}
void gal_words_free(gal_words *w) { if (w) { jfree(w->root); free(w); } }

/* W.<group>[key], or W.<group> itself when key is NULL; a [name, icon, ...] entry gives its name */
static const jv *word_v(const gal_words *w, const char *group, const char *key) {
    if (!w) return NULL;
    const jv *g = jget(w->W, group);
    if (!key) return g;
    const jv *v = (g && g->t == JN_OBJ) ? jget(g, key) : NULL;
    if (v && v->t == JN_ARR) v = v->cnt ? v->v[0] : NULL;
    return v;
}
static char word_buf[8][256]; static int word_slot;
const char *gal_word(const gal_words *w, const char *group, const char *key) {
    const jv *v = word_v(w, group, key);
    if (!v || v->t != JN_STR) return "";
    char *o = word_buf[word_slot++ & 7];
    str_of(v, o, sizeof word_buf[0]);
    return o;
}

/* teamname.js fill(): {name} -> value or '', whitespace runs to one space, trimmed */
typedef struct { const char *k, *v; } kv;
static void fill(const char *tpl, const kv *map, int nm, char *out, size_t n) {
    char tmp[1024]; size_t k = 0;
    for (const char *s = tpl; *s && k + 1 < sizeof tmp;) {
        if (*s == '{') {
            const char *e = s + 1;
            while (isalnum((unsigned char)*e) || *e == '_') e++;
            if (*e == '}' && e > s + 1) {
                const char *val = "";
                for (int i = 0; i < nm; i++) if (strlen(map[i].k) == (size_t)(e - s - 1) && !strncmp(map[i].k, s + 1, (size_t)(e - s - 1))) val = map[i].v;
                for (; *val && k + 1 < sizeof tmp; val++) tmp[k++] = *val;
                s = e + 1;
                continue;
            }
        }
        tmp[k++] = *s++;
    }
    tmp[k] = 0;
    /* JS \s: space, tab, newline ... (ASCII here; the templates are plain) */
    size_t o = 0; int sp = 0;
    for (size_t i = 0; tmp[i]; i++) {
        if (isspace((unsigned char)tmp[i])) { sp = 1; continue; }
        if (sp && o && o + 1 < n) out[o++] = ' ';
        sp = 0;
        if (o + 1 < n) out[o++] = tmp[i];
    }
    if (n) out[o < n ? o : n - 1] = 0;
}
static void tpl_or(const gal_words *w, const char *key, const char *def, char *o, size_t n) {
    const jv *v = word_v(w, key, NULL);
    if (v && v->t == JN_STR && v->n) str_of(v, o, n); else snprintf(o, n, "%s", def);
}

void gal_item_title(const gal_item *it, const gal_words *w, char *out, size_t n) {
    char rn[128] = "", tpl[128], pair[128], tn[256] = "", noun[128] = "", unit[128];
    if (it->name_race[0]) snprintf(rn, sizeof rn, "%s", gal_word(w, "races", it->name_race));
    if (!it->name_noun[0]) {                                   /* a solo character: its race */
        tpl_or(w, "unit", "", unit, sizeof unit);
        snprintf(out, n, "%s", rn[0] ? rn : unit);
        return;
    }
    char names[4][128]; int nn = 0;
    for (int i = 0; i < it->name_ntrees && i < 4; i++) {
        const char *t = gal_word(w, "trees", it->name_trees[i]);
        if (*t) snprintf(names[nn++], sizeof names[0], "%s", t);
    }
    if (nn == 2) {
        tpl_or(w, "pair", "{a} & {b}", pair, sizeof pair);
        kv m[2] = { { "a", names[0] }, { "b", names[1] } };
        fill(pair, m, 2, tn, sizeof tn);
    } else if (nn) snprintf(tn, sizeof tn, "%s", names[0]);
    const char *nw = gal_word(w, "nouns", it->name_noun);
    snprintf(noun, sizeof noun, "%s", *nw ? nw : it->name_noun);
    tpl_or(w, "tpl", "{race} {trees} {noun}", tpl, sizeof tpl);
    kv m[3] = { { "race", rn }, { "trees", tn }, { "noun", noun } };
    fill(tpl, m, 3, out, n);
}

void gal_member_name(const gal_item *it, int index, const gal_words *w, char *out, size_t n) {
    char rn[128] = "", tpl[128], num[16];
    const char *race = index >= 0 && index < it->nunits ? it->units[index].race : "";
    if (race[0]) snprintf(rn, sizeof rn, "%s", gal_word(w, "races", race));
    if (!rn[0]) tpl_or(w, "unit", "", rn, sizeof rn);
    if (!it->team) { snprintf(out, n, "%s", rn); return; }
    tpl_or(w, "member", "{race} {n}", tpl, sizeof tpl);
    snprintf(num, sizeof num, "%d", index + 1);
    kv m[2] = { { "race", rn }, { "n", num } };
    fill(tpl, m, 2, out, n);
}

void gal_author_name(const gal_item *it, const gal_words *w, char *out, size_t n) {
    if (it->au_name[0]) { snprintf(out, n, "%s", it->au_name); return; }
    if (!it->au_pseudo) { if (n) out[0] = 0; return; }
    char tpl[128], rn[128], bg[128], num[16];
    tpl_or(w, "pseudo", "{race} {background} {n}", tpl, sizeof tpl);
    snprintf(rn, sizeof rn, "%s", gal_word(w, "races", it->au_race));
    snprintf(bg, sizeof bg, "%s", gal_word(w, "bg", it->au_bg));
    snprintf(num, sizeof num, "%d", it->au_n);
    kv m[3] = { { "race", rn }, { "background", bg }, { "n", num } };
    fill(tpl, m, 3, out, n);
}

int gal_usable(const gal_item *it, const char *game_version) {
    if (!it->game_build[0] || !game_version || !game_version[0]) return 1;
    const char *a = it->game_build, *b = game_version;
    if (!strncmp(a, "DS_", 3)) a += 3;
    if (!strncmp(b, "DS_", 3)) b += 3;
    return !strcmp(a, b);
}
