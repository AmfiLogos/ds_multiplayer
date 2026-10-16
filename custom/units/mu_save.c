/* mu_save - the units of a save as wiki builds (see mu_save.h). Pure C: fe_json + the wiki tables, no game code.
 * The save -> build mapping is the wiki builder's own save import, line for line where it can be
 * (~/dungeonsettlersdb/site/builder/builder.js: dsbExtractUnit, dsbSaveUnitToBuild, dsbItemRolls, dsbResolvePortrait).
 * It writes the builder's build JSON and hands it to wk_from_json (pvp_wiki.c), so the tag steps, the trimmed name,
 * retired races / traits and the talent tiers come out exactly as for a My Units library build.
 * Two additions to the builder's import, both read straight from the save:
 *  - portrait: the profile's UnitPortraitID when it is a sprite of the race's pool (the builder only guesses it from
 *    ProfileKey; the save holds the exact one); otherwise the builder's guess.
 *  - acquired traits (WK_ACQ) the save holds that are not individual traits (the builder's import drops them; the card
 *    has a row for them and pvp_build writes them back as holders). */
#ifdef _WIN32
#define MU_MTIME_NS(st) ((long long)(st).st_mtime * 1000000000LL)
#else
#define MU_MTIME_NS(st) ((long long)(st).st_mtimespec.tv_sec * 1000000000LL + (st).st_mtimespec.tv_nsec)
#endif
#include "mu_save.h"
#include "pvp_wiki_tables.h"
#include "pvp_unit_tables.h"
#include "mu_card_tables.h"
#include "fe/fe_json.h"
#include "fe/fe_save.h"
#include <dirent.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
static SRWLOCK lock_ = SRWLOCK_INIT;                    /* Windows: a slim lock (no pthreads in the game's DLL set) */
#define LOCK() AcquireSRWLockExclusive(&lock_)
#define UNLOCK() ReleaseSRWLockExclusive(&lock_)
#else
#include <pthread.h>
static pthread_mutex_t lock_ = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&lock_)
#define UNLOCK() pthread_mutex_unlock(&lock_)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------- the parsed-save cache */
#define CACHE_N 3                       /* a save is 2-5 MB of JSON; a few parsed ones are plenty for one window */
typedef struct { char path[1024]; long long size, mtime; jv *save; unsigned long long used; } centry;
static centry cache[CACHE_N];
static unsigned long long tick;

static void cache_clear_locked(void) {
    for (int i = 0; i < CACHE_N; i++) { jfree(cache[i].save); memset(&cache[i], 0, sizeof cache[i]); }
}
void mu_save_cache_clear(void) { LOCK(); cache_clear_locked(); UNLOCK(); }

/* the parsed save (owned by the cache), or NULL + err. Call with the lock held. The file is read like fe_game.c
   read_json -> fe_json jload: the game writes plain JSON (checked on the saves of 2026-10: no compression, no wrapper). */
static const jv *save_of(const char *path, char *err, size_t errn) {
    struct stat st;
    if (!path || !*path || stat(path, &st)) { snprintf(err, errn, "The save file cannot be found."); return NULL; }
    long long mt = (long long)MU_MTIME_NS(st);
    for (int i = 0; i < CACHE_N; i++)
        if (cache[i].save && !strcmp(cache[i].path, path) && cache[i].size == (long long)st.st_size && cache[i].mtime == mt) {
            cache[i].used = ++tick; return cache[i].save;
        }
    char e[300] = "";
    jv *s = jload(path, e, sizeof e);
    if (!s) { snprintf(err, errn, "The save could not be read (%s).", e); return NULL; }
    if (s->t != JN_OBJ || !jget(s, "EntityContainerSaveData") || !jget(s, "EntityComponentSaveData")) {
        jfree(s); snprintf(err, errn, "This file is not a Dungeon Settlers save."); return NULL;
    }
    int k = 0;
    for (int i = 0; i < CACHE_N; i++) {
        if (cache[i].save && !strcmp(cache[i].path, path)) { k = i; break; }      /* an older copy of the same file */
        if (!cache[i].save) { k = i; break; }
        if (cache[i].used < cache[k].used) k = i;
    }
    jfree(cache[k].save);
    snprintf(cache[k].path, sizeof cache[k].path, "%s", path);
    cache[k].size = (long long)st.st_size; cache[k].mtime = mt; cache[k].save = s; cache[k].used = ++tick;
    return s;
}

/* ---------------------------------------------------------------- small helpers */
static int sx(const char *const *tab, int n, const jv *v) {             /* index of a JSON string in a key table */
    if (!v || v->t != JN_STR) return -1;
    for (int i = 0; i < n; i++) if (tab[i] && strlen(tab[i]) == v->n && !memcmp(tab[i], v->s, v->n)) return i;
    return -1;
}
static int sxs(const char *const *tab, int n, const char *s) { for (int i = 0; i < n; i++) if (tab[i] && !strcmp(tab[i], s)) return i; return -1; }
static int has_i(const int *a, int n, int v) { for (int i = 0; i < n; i++) if (a[i] == v) return 1; return 0; }
static void push_i(int *a, int *n, int cap, int v) { if (!has_i(a, *n, v) && *n < cap) a[(*n)++] = v; }
static int zero_guid(const jv *g) { return !g || g->t != JN_STR || !g->n || jstreq(g, FE_ZERO); }

typedef struct { int n; char *keys; size_t kn; } unk;
static void unknown_s(unk *U, const char *what, const char *k, size_t kn) {
    U->n++;
    if (!U->keys) return;
    size_t at = strlen(U->keys);
    if (at + kn + strlen(what) + 4 < U->kn) snprintf(U->keys + at, U->kn - at, "%s%s %.*s", at ? ", " : "", what, (int)kn, k);
}
static void unknown(unk *U, const char *what, const jv *v) { if (v && v->t == JN_STR) unknown_s(U, what, v->s, v->n); else unknown_s(U, what, "?", 1); }

/* the race / background / trait / inscription / soul tables of the builder (D.races, D.bg, D.ind, D.insc, D.souls).
   WK_RACE and WK_IND also hold retired keys (for old links); the builder's sets do not. */
static int race_of(const jv *k) { int r = sx(WK_RACE, WK_RACE_N, k); return r >= 0 && !WK_RACE_RETIRED[r] ? r : -1; }
static int ind_of(const jv *k) { int r = sx(WK_IND, WK_IND_N, k); return r >= 0 && !WK_IND_RETIRED[r] ? r : -1; }
static int tree_insc(int insc) { for (int t = 0; t < WK_TREE_N; t++) if (WU_TREE_INSC[t] && !strcmp(WU_TREE_INSC[t], WK_INSC[insc])) return 1; return 0; }

/* which units: fe_game.c fe_list_units (all sides' PlayerGuidList, container order) + fe_save.c fe_export_unit (the
   guid must have an entity). out: the guid jv's (borrowed from the save). */
static int clan_guids(const jv *s, const jv **out, int max) {
    const jv *u = jpath(s, "EntityContainerSaveData.UnitContainerSaveData"), *ec = jpath(s, "EntityComponentSaveData.EntityComponents");
    int n = 0;
    for (size_t i = 0; u && u->t == JN_OBJ && i < u->cnt; i++) {
        const jv *pl = jget(u->v[i], "PlayerGuidList");
        for (size_t j = 0; pl && pl->t == JN_ARR && j < pl->cnt && n < max; j++) {
            const jv *g = pl->v[j];
            if (!g || g->t != JN_STR || !g->n || !jget(ec, g->s)) continue;
            int dup = 0; for (int k = 0; k < n; k++) if (out[k]->n == g->n && !memcmp(out[k]->s, g->s, g->n)) dup = 1;
            if (!dup) out[n++] = g;
        }
    }
    return n;
}

/* dsbResolvePortrait (+ UnitPortraitID first, see the top). Returns a sprite key of the race's pool or NULL. */
static const char *portrait_of(int race, const jv *prof) {
    const char *const *pool = WK_PORTRAITS[race]; int pn = WK_PORTRAITS_N[race];
    if (!pool || pn <= 0) return NULL;
    int i = sx(pool, pn, jget(prof, "UnitPortraitID")); if (i >= 0) return pool[i];
    const jv *pk = jget(prof, "ProfileKey"); char vis[128] = "";
    if (pk && pk->t == JN_STR && pk->n < sizeof vis) {
        memcpy(vis, pk->s, pk->n); vis[pk->n] = 0;
        char *p = !strncmp(vis, "UNITVISUAL_", 11) ? vis + 11 : vis;
        memmove(vis, p, strlen(p) + 1);
        /* /^(.*)_(\d+)$/ -> "<body>_" + ('00' + n).slice(-3) */
        char *us = strrchr(vis, '_');
        if (us && us[1]) {
            int dig = 1; for (char *d = us + 1; *d; d++) if (*d < '0' || *d > '9') dig = 0;
            if (dig) {
                char num[16]; snprintf(num, sizeof num, "00%s", us + 1); size_t L = strlen(num);
                char key[160]; snprintf(key, sizeof key, "%.*s_%s", (int)(us - vis), vis, num + L - 3);
                if ((i = sxs(pool, pn, key)) >= 0) return pool[i];
            }
        }
    }
    const jv *gt = jget(prof, "GenderType"); const char *g = NULL;
    if (gt && gt->t == JN_NUM) { double d = jnumv(gt); g = d == 0 ? "Male" : d == 1 ? "Female" : NULL; }
    if (!g) {
        const jv *dk = jget(prof, "DefaultUnitNameTextKey"); char t[160] = "";
        if (dk && dk->t == JN_STR && dk->n < sizeof t) { memcpy(t, dk->s, dk->n); t[dk->n] = 0; }
        if (strstr(t, "_Male_")) g = "Male"; else if (strstr(t, "_Female_")) g = "Female";
    }
    if (g) {
        char body[128] = "";
        if (vis[0]) {                                   /* /^([A-Za-z]+?)(?:Male|Female|Unisex)/: the shortest letters prefix */
            for (size_t L = 1; vis[L - 1] && ((vis[L - 1] | 32) >= 'a' && (vis[L - 1] | 32) <= 'z'); L++) {
                const char *r = vis + L;
                if (!strncmp(r, "Male", 4) || !strncmp(r, "Female", 6) || !strncmp(r, "Unisex", 6)) { snprintf(body, sizeof body, "%.*s", (int)L, vis); break; }
            }
        }
        if (body[0]) { char bg[160]; snprintf(bg, sizeof bg, "%s%s", body, g); for (i = 0; i < pn; i++) if (!strncmp(pool[i], bg, strlen(bg))) return pool[i]; }
        for (i = 0; i < pn; i++) if (strstr(pool[i], g)) return pool[i];
    }
    return pool[0];
}

/* ---------------------------------------------------------------- one unit -> builder build JSON -> wk_build */
static int unit_build(const jv *s, const jv *gv, wk_build *out, mu_save_extra *x, char *err, size_t errn) {
    char g[64]; snprintf(g, sizeof g, "%.*s", (int)(gv->n < 63 ? gv->n : 63), gv->s);
    const jv *ec = jpath(s, "EntityComponentSaveData.EntityComponents"), *comps = jget(ec, g);
    if (!comps) { snprintf(err, errn, "This unit is not in the save."); return -1; }
    const jv *status = fe_comp(comps, 0), *aff = fe_comp(comps, 3), *eqc = fe_comp(comps, 6), *prof = fe_comp(comps, 10);
    const jv *pu = jget(s, "PlayerUnitsSaveData");
    const jv *st = jget(jget(pu, "SkillTrees"), g);
    unk U = { 0, x ? x->unknown_keys : NULL, x ? sizeof x->unknown_keys : 0 };
    if (x) memset(x, 0, sizeof *x);

    /* affecter holders (dsbExtractUnit): first race, first background, traits, souls, inscriptions; last Stack per key */
    int race = -1, bg = -1, inds[WK_MAX_LIST], ni = 0, souls[WK_MAX_LIST], ns = 0, inscs[WK_MAX_LIST], nc = 0, acq[WK_MAX_LIST], na = 0;
    const jv *hold = jget(aff, "AffecterHolders");
    for (size_t i = 0; hold && hold->t == JN_ARR && i < hold->cnt; i++) {
        const jv *k = jget(hold->v[i], "Key"); int ix;
        if (!k || k->t != JN_STR || !k->n) continue;
        if ((ix = race_of(k)) >= 0) { if (race < 0) race = ix; }
        else if ((ix = sx(WK_BG, WK_BG_N, k)) >= 0) { if (bg < 0) bg = ix; }
        else if ((ix = ind_of(k)) >= 0) push_i(inds, &ni, WK_MAX_LIST, ix);
        else if ((ix = sx(WK_SOUL, WK_SOUL_N, k)) >= 0) push_i(souls, &ns, WK_MAX_LIST, ix);
        else if ((ix = sx(WK_INSC, WK_INSC_N, k)) >= 0) push_i(inscs, &nc, WK_MAX_LIST, ix);
        else if ((ix = sx(WK_ACQ, WK_ACQ_N, k)) >= 0) push_i(acq, &na, WK_MAX_LIST, ix);
        /* an inscription or a soul stone this wiki data does not know (other holders are states: night, hunger, ...) */
        else if ((k->n > 11 && !memcmp(k->s + k->n - 11, "Inscription", 11)) || (k->n > 4 && !memcmp(k->s + k->n - 4, "Soul", 4))) unknown(&U, "affecter", k);
    }
    const jv *nameKey = jget(prof, "UnitNameTextKey"), *defKey = jget(prof, "DefaultUnitNameTextKey");
    if (race < 0) {                                         /* dsbRaceFromNameKey(defKey) || (nameKey) */
        for (int pass = 0; pass < 2 && race < 0; pass++) {
            const jv *nk = pass ? nameKey : defKey; const char *p = "TEXTKEY_PLAYERNAME_";
            if (!nk || nk->t != JN_STR || nk->n <= strlen(p) || memcmp(nk->s, p, strlen(p))) continue;
            char r[96]; size_t a = strlen(p), b = a; while (b < nk->n && nk->s[b] != '_') b++;
            snprintf(r, sizeof r, "AFFECTER_%.*s", (int)(b - a), nk->s + a);
            int ix = sxs(WK_RACE, WK_RACE_N, r); if (ix >= 0 && !WK_RACE_RETIRED[ix]) race = ix;
        }
        if (race < 0) unknown_s(&U, "race", "(none known, Human used)", 24);
    }
    if (race < 0) race = sxs(WK_RACE, WK_RACE_N, "AFFECTER_Human");        /* b.race fallback */

    jv *b = jobj();
    if (nameKey && nameKey->t == JN_STR) jset(b, "name", jcopy(nameKey)); else jset(b, "name", jstrv(""));
    const jv *prog = jget(status, "ProgressionStats"), *gen = jget(status, "GeneratedStats"), *perm = jget(status, "PermanentStats");
    double lv = jnumv(jget(prog, "Level")); long L = lv ? (long)floor(lv + 0.5) : 1;
    if (L < 1) L = 1; if (L > WU_MAX_LEVEL) L = WU_MAX_LEVEL;              /* Math.max(1, Math.min(maxLevel, round)) */
    jset(b, "lvl", jnum_i(L));
    jset(b, "offer", jstrv("Least")); jset(b, "roll", jstrv("avg"));
    jset(b, "race", jstrv(WK_RACE[race]));
    if (bg >= 0) jset(b, "bg", jstrv(WK_BG[bg]));
    /* talents: ProgressionStats.Talent<Major> -1..3 = WK_TIER 0..4 (pvp_build writes tier - 1); none = unset */
    jv *tal = jobj();
    for (int m = 0; m < 6; m++) {
        char k[48]; snprintf(k, sizeof k, "Talent%s", WK_MAJ[m]); const jv *v = jget(prog, k);
        if (!v || v->t != JN_NUM) continue;
        long t = (long)floor(jnumv(v) + 0.5); if (t < -1) t = -1; if (t > 3) t = 3;
        jset(tal, WK_MAJ[m], jstrv(WK_TIER[t + 1]));
    }
    jset(b, "tal", tal);
    /* gear: each worn item in ITS slot (D.items[key].slot), with its rolled bonuses (dsbItemRolls) */
    jv *eq = jobj(), *tags = jobj();
    const jv *sl = jget(eqc, "ItemSlots");
    for (size_t i = 0; sl && sl->t == JN_ARR && i < sl->cnt; i++) {
        const jv *k = jget(sl->v[i], "Key"); if (!k || k->t != JN_STR || !k->n) continue;
        int it = sx(WK_ITEM, WK_ITEM_N, k);
        if (it < 0 || WU_ITEM[it].slot < 0 || WU_ITEM[it].slot >= WK_SLOT_N) { unknown(&U, "item", k); continue; }
        const char *slot = WK_SLOT[(int)WU_ITEM[it].slot];
        jset(eq, slot, jcopy(k));
        jv *picks = jarr(); int np = 0;
        const jv *ig = jget(sl->v[i], "Guid"), *stats = zero_guid(ig) ? NULL : jget(fe_comp(jget(ec, ig->s), 0), "Stats");
        for (int t = 0; stats && t < WU_ITEM[it].ntag; t++) {
            const char *ts = WU_TAG_STAT[WU_ITEM[it].tag_at + t]; char base[64]; size_t tl = strlen(ts);
            snprintf(base, sizeof base, "%s", ts);
            if (tl > 11 && !strcmp(ts + tl - 11, "PerModifier")) base[tl - 11] = 0;
            const jv *v = jget(stats, base); if (!v || v->t == JN_NULL) v = jget(stats, ts);
            if (!v || v->t != JN_NUM) continue;
            double d = jnumv(v); if (d == 0) continue;
            double lo = fmin(WU_TAG_LOHI[WU_ITEM[it].tag_at + t][0], WU_TAG_LOHI[WU_ITEM[it].tag_at + t][1]);
            double hi = fmax(WU_TAG_LOHI[WU_ITEM[it].tag_at + t][0], WU_TAG_LOHI[WU_ITEM[it].tag_at + t][1]);
            if (d >= lo - 0.01 && d <= hi + 0.01) { jv *p = jobj(); jset(p, "i", jnum_i(t)); jset(p, "v", jnum_d(d)); jpush(picks, p); np++; }
        }
        if (np) jset(tags, slot, picks); else jfree(picks);
    }
    jset(b, "eq", eq); jset(b, "tags", tags);
    /* traits: the race's pool, capped at indMax (4, data.js consts.indMax); savedInds = every known one */
    jv *ji = jarr(), *jsi = jarr(); int nin = 0;
    for (int i = 0; i < ni; i++) {
        if (MUC_RACE_IND[race][inds[i]] && nin < 4) { jpush(ji, jstrv(WK_IND[inds[i]])); nin++; }
        jpush(jsi, jstrv(WK_IND[inds[i]]));
    }
    jset(b, "inds", ji); jset(b, "savedInds", jsi);
    jv *jso = jarr(); for (int i = 0; i < ns; i++) jpush(jso, jstrv(WK_SOUL[souls[i]])); jset(b, "souls", jso);
    jv *jac = jarr(); for (int i = 0; i < na; i++) jpush(jac, jstrv(WK_ACQ[acq[i]])); jset(b, "acqInds", jac);
    /* trees beyond the background's become inscription trees, the ones with a learned skill first */
    const jv *trees = jget(st, "SkillTrees"), *lbt = jget(st, "LearnedSkills");
    int extra[WK_MAX_LIST], ne = 0;
    static const char *const PART[3] = { "Main", "Sub", "Other" };
    for (int p = 0; p < 3; p++) {
        const jv *a = jget(trees, PART[p]);
        for (size_t i = 0; a && a->t == JN_ARR && i < a->cnt; i++) {
            int t = sx(WK_TREE, WK_TREE_N, a->v[i]);
            if (t < 0) { unknown(&U, "tree", a->v[i]); continue; }
            int nat = 0; if (bg >= 0) for (int q = 0; q < 4; q++) if (WU_BG_MAINS[bg][q] == t || WU_BG_SUBS[bg][q] == t) nat = 1;
            if (!nat) push_i(extra, &ne, WK_MAX_LIST, t);
        }
    }
    jv *jt = jarr();
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < ne; i++) {
        const jv *ls = jget(lbt, WK_TREE[extra[i]]); int has = ls && ls->t == JN_ARR && ls->cnt > 0;
        if (has == !pass) jpush(jt, jstrv(WK_TREE[extra[i]]));
    }
    jset(b, "insctrees", jt);
    /* learned: every tree's learned skills (save order), then ExtraSkills, known skills only, no duplicates */
    int lr[WK_MAX_LIST], nl = 0;
    for (size_t i = 0; lbt && lbt->t == JN_OBJ && i < lbt->cnt; i++) {
        const jv *a = lbt->v[i];
        for (size_t j = 0; a && a->t == JN_ARR && j < a->cnt; j++) {
            int k = sx(WK_SKILL, WK_SKILL_N, a->v[j]);
            if (k < 0) { if (a->v[j] && a->v[j]->t == JN_STR) unknown(&U, "skill", a->v[j]); continue; }
            push_i(lr, &nl, WK_MAX_LIST, k);
        }
    }
    const jv *ex = jget(st, "ExtraSkills");                /* item / temp skills: not wiki tree skills, silently left out */
    for (size_t j = 0; ex && ex->t == JN_ARR && j < ex->cnt; j++) { int k = sx(WK_SKILL, WK_SKILL_N, ex->v[j]); if (k >= 0) push_i(lr, &nl, WK_MAX_LIST, k); }
    jv *jl = jarr(); for (int i = 0; i < nl; i++) jpush(jl, jstrv(WK_SKILL[lr[i]])); jset(b, "learned", jl);
    /* milestone counters (passiveStacks): every milestone passive whose holder the unit has, learned or not */
    jv *ps = jobj();
    for (int sk = 0; sk < WK_SKILL_N; sk++) {
        if (!WU_PG[sk].aff || !WU_PG[sk].nthr) continue;
        const jv *last = NULL;
        for (size_t i = 0; hold && hold->t == JN_ARR && i < hold->cnt; i++)
            if (jstreq(jget(hold->v[i], "Key"), WU_PG[sk].aff) && jget(hold->v[i], "Stack") && jget(hold->v[i], "Stack")->t == JN_NUM) last = jget(hold->v[i], "Stack");
        if (last) jset(ps, WK_SKILL[sk], jnum_d(jnumv(last)));
    }
    jset(b, "passiveStacks", ps);
    /* inscriptions: held = the non-tree ones (tree ones are the insctrees), savedInsc = every known one */
    jv *jin = jarr(), *jsn = jarr();
    for (int i = 0; i < nc; i++) { if (!tree_insc(inscs[i])) jpush(jin, jstrv(WK_INSC[inscs[i]])); jpush(jsn, jstrv(WK_INSC[inscs[i]])); }
    jset(b, "insc", jin); jset(b, "savedInsc", jsn);
    /* growth: ProgressionStats + GeneratedStats + PermanentStats per major (when the save has any), elixir uses out */
    double mb[6]; int any = 0;
    for (int m = 0; m < 6; m++) {
        const jv *a = jget(prog, WK_MAJ[m]), *c = jget(gen, WK_MAJ[m]), *d = jget(perm, WK_MAJ[m]);
        mb[m] = jnumv(a) + jnumv(c) + jnumv(d);
        if ((a && a->t != JN_NULL) || (c && c->t != JN_NULL) || (d && d->t != JN_NULL)) any = 1;
    }
    jv *el = jobj();
    for (int m = 0; m < 6; m++) {
        double pv = jnumv(jget(perm, WK_MAJ[m])), per = WU_ELIXIR[m];
        if (!(per > 0) || pv <= 0) continue;
        double n = floor(pv / per + 1e-6); if (n > 99) n = 99;                 /* D.elixMax */
        if (n < 1 || fabs(pv - n * per) > 1e-3) continue;
        jset(el, WK_MAJ[m], jnum_i((long long)n)); mb[m] -= n * per;
    }
    jset(b, "elix", el);
    if (any) { jv *jm = jobj(); for (int m = 0; m < 6; m++) jset(jm, WK_MAJ[m], jnum_d(mb[m])); jset(b, "majorBase", jm); }
    const char *por = portrait_of(race, prof);
    if (por) jset(b, "portrait", jstrv(por));
    jset(b, "hideHead", jbool(jtruthy(jget(prof, "HideHeadEquipment"))));

    char e[300] = "";
    int r = wk_from_json(b, out, e, sizeof e);
    jfree(b);
    if (r) { snprintf(err, errn, "This unit could not be read: %s", e); return -1; }
    /* the skill bar the unit has (wk_build has no place for it) */
    const jv *qs = jpath(jget(jget(pu, "QuickSlots"), g), "QuickSlots");
    for (size_t i = 0; x && qs && qs->t == JN_ARR && i < qs->cnt; i++) {
        const jv *k = qs->v[i]; if (!k || k->t != JN_STR || !k->n) continue;
        /* the wiki's skills + the two every player unit has (UnitTable UNIT_PlayerUnit DefaultSkills, see pvp_build.c);
           a player can put Emergency Escape on the bar */
        if (sx(MUC_SK, MUC_SK_N, k) < 0 && !jstreq(k, "SKILL_EmergencyEscape") && !jstreq(k, "SKILL_BasicPunchAttack")) { unknown(&U, "bar skill", k); continue; }
        if (x->nbar < 8) snprintf(x->bar[x->nbar++], sizeof x->bar[0], "%.*s", (int)k->n, k->s);
    }
    if (x) x->unknown = U.n;
    return 0;
}

/* ---------------------------------------------------------------- API */
int mu_save_units(const char *save_path, mu_save_unit *out, int max, char *err, size_t errn) {
    if (err && errn) err[0] = 0;
    LOCK();
    const jv *s = save_of(save_path, err, errn);
    if (!s) { UNLOCK(); return -1; }
    const jv *gs[512]; int ng = clan_guids(s, gs, 512), n = 0;
    wk_build *b = malloc(sizeof *b);
    for (int i = 0; i < ng && n < max; i++) {
        char e[300];
        if (unit_build(s, gs[i], b, NULL, e, sizeof e)) continue;          /* cannot happen with known keys only; never list a unit we cannot build */
        mu_save_unit *u = &out[n++]; memset(u, 0, sizeof *u);
        snprintf(u->guid, sizeof u->guid, "%.*s", (int)gs[i]->n, gs[i]->s);
        snprintf(u->name, sizeof u->name, "%s", b->name);
        const jv *lv = jpath(fe_comp(jget(jpath(s, "EntityComponentSaveData.EntityComponents"), u->guid), 0), "ProgressionStats.Level");
        u->level = lv ? (int)jnumv(lv) : b->lvl;
        snprintf(u->race, sizeof u->race, "%s", WK_RACE[b->race]);
        if (b->portrait >= 0 && WK_PORTRAITS[b->race]) snprintf(u->portrait, sizeof u->portrait, "%s", WK_PORTRAITS[b->race][b->portrait]);
    }
    free(b);
    UNLOCK();
    if (ng == 0 && err && errn) snprintf(err, errn, "No units of your clan are stored in this save.");
    return n;
}
int mu_save_build_ex(const char *save_path, const char *guid, wk_build *out, mu_save_extra *x, char *err, size_t errn) {
    if (err && errn) err[0] = 0;
    LOCK();
    const jv *s = save_of(save_path, err, errn);
    int r = -1;
    if (s) {
        const jv *gs[512]; int ng = clan_guids(s, gs, 512);
        for (int i = 0; i < ng; i++) if (guid && strlen(guid) == gs[i]->n && !memcmp(guid, gs[i]->s, gs[i]->n)) { r = unit_build(s, gs[i], out, x, err, errn); goto done; }
        snprintf(err, errn, "This unit is not one of your clan's units in this save.");
    }
done:
    UNLOCK();
    return r;
}
int mu_save_build(const char *save_path, const char *guid, wk_build *out, char *err, size_t errn) {
    return mu_save_build_ex(save_path, guid, out, NULL, err, errn);
}

/* fe_game.c fe_list_saves: the name filter and the newest-first order (no header parse here) */
int mu_save_files(const char *dir, char (*out)[1024], int max) {
    DIR *dp = dir ? opendir(dir) : NULL; if (!dp) return 0;
    int n = 0; double *mt = malloc(sizeof(double) * (size_t)(max > 0 ? max : 1));
    for (struct dirent *e; (e = readdir(dp)) && n < max; ) {
        size_t l = strlen(e->d_name);
        if (l < 6 || strcmp(e->d_name + l - 5, ".json") || !strncmp(e->d_name, "mp_sync", 7) || !strncmp(e->d_name, "MPFE_", 5)
            || !strncmp(e->d_name, "MPPvP_", 6) || !strncmp(e->d_name, "MPHub_", 6) || strstr(e->d_name, "SaveHeaderCache")) continue;   /* custom-mode worlds (PvP match, hub) are no regular saves: as pvp_ui.c's list */
        char p[1024]; if ((size_t)snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= sizeof p) continue;
        struct stat st; if (stat(p, &st) || !S_ISREG(st.st_mode)) continue;
        memcpy(out[n], p, sizeof p); mt[n] = MU_MTIME_NS(st) / 1e9; n++;
    }
    closedir(dp);
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (mt[j] > mt[i]) {
        char t[1024]; memcpy(t, out[i], sizeof t); memcpy(out[i], out[j], sizeof t); memcpy(out[j], t, sizeof t);
        double d = mt[i]; mt[i] = mt[j]; mt[j] = d;
    }
    free(mt);
    return n;
}
