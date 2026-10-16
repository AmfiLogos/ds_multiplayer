/* mu_card - the Compact Plus card data (see mu_card.h). Pure C. */
#include "mu_card.h"
#include "pvp_wiki_tables.h"
#include "mu_card_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(MUC_FP == WK_FP, "mu_card_tables.h and pvp_wiki_tables.h come from different wiki data: run tools/gen_card_tables.js");

static int has(const int *a, int n, int v) { for (int i = 0; i < n; i++) if (a[i] == v) return 1; return 0; }
static int has_s(const char *const *a, int n, const char *v) { for (int i = 0; i < n; i++) if (!strcmp(a[i], v)) return 1; return 0; }

/* builder.js activeTrees: the background's weapon trees, its auxiliary trees (no duplicates), then inscription trees */
static int active_trees(const wk_build *b, int *out) {
    int n = 0;
    if (b->bg >= 0 && MUC_BG_OK[b->bg]) {
        for (int i = MUC_BG_MAIN_AT[b->bg]; i < MUC_BG_MAIN_AT[b->bg + 1]; i++) if (!has(out, n, MUC_BG_MAIN[i])) out[n++] = MUC_BG_MAIN[i];
        for (int i = MUC_BG_SUB_AT[b->bg]; i < MUC_BG_SUB_AT[b->bg + 1]; i++) if (!has(out, n, MUC_BG_SUB[i])) out[n++] = MUC_BG_SUB[i];
    }
    for (int i = 0; i < b->ntrees; i++) { int t = b->trees[i]; if (MUC_TREE_KIND[t] >= 0 && !has(out, n, t)) out[n++] = t; }
    return n;
}
static int tree_has(int t, int sk) { for (int i = MUC_TREE_SK_AT[t]; i < MUC_TREE_SK_AT[t + 1]; i++) if (MUC_TREE_SK[i] == sk) return 1; return 0; }
/* treeOfSkillFor: the first of the unit's trees holding the skill, -1 none */
static int tree_of(const int *at, int nat, int sk) { for (int i = 0; i < nat; i++) if (tree_has(at[i], sk)) return at[i]; return -1; }

/* stackCountFor for a decoded link (no stackLv picks, no remembered default level outside a browser) */
static double stack_count(const wk_build *b, int sk, int learned) {
    int n = MUC_PG_THR_AT[sk + 1] - MUC_PG_THR_AT[sk];
    if (!n) return 0;
    if (sk < WK_SKILL_N) for (int i = 0; i < b->npstk; i++) if (b->pstk[i].skill == sk) return b->pstk[i].st;
    if (b->has_mb) return 0;
    if (!learned) return 0;
    return MUC_PG_THR[MUC_PG_THR_AT[sk + 1] - 1];
}
static int stack_lv(const wk_build *b, int sk, int learned) {
    double c = stack_count(b, sk, learned); int lv = 0;
    for (int i = MUC_PG_THR_AT[sk]; i < MUC_PG_THR_AT[sk + 1]; i++) if (c >= MUC_PG_THR[i]) lv++;
    return lv;
}

int mu_card_of(const wk_build *b, mu_card *o) {
    memset(o, 0, sizeof *o);
    snprintf(o->name, sizeof o->name, "%s", b->name);
    o->lvl = b->lvl ? b->lvl : 1;
    o->race = WK_RACE[b->race];
    o->bg = b->bg >= 0 && MUC_BG_OK[b->bg] ? WK_BG[b->bg] : NULL;
    int prim = b->eq[0];                                                  /* WK_SLOT[0] = Primary */
    o->weapon = prim >= 0 && MUC_ITEM_OK[prim] ? MUC_ITEM_WLABEL[prim] : NULL;
    o->portrait = b->portrait >= 0 && WK_PORTRAITS[b->race] ? WK_PORTRAITS[b->race][b->portrait] : NULL;
    o->avatar = o->portrait ? o->portrait : MUC_FIRST_PORTRAIT[b->race];

    int at[WK_TREE_N + WK_MAX_LIST], nat = active_trees(b, at);
    int inv[WK_MAX_LIST], ninv = 0;                                      /* investedTrees: learned order */
    for (int i = 0; i < b->nlearned; i++) { int t = tree_of(at, nat, b->learned[i]); if (t >= 0 && !has(inv, ninv, t)) inv[ninv++] = t; }
    int seen[WK_TREE_N] = { 0 };
#define PUSH(t) do { int t_ = (t); if (MUC_TREE_KIND[t_] >= 0 && !seen[t_]) { seen[t_] = 1; \
        if (MUC_TREE_KIND[t_] == 1) { o->active[o->nactive].tree = WK_TREE[t_]; o->active[o->nactive++].invested = has(inv, ninv, t_); } \
        else { o->passive[o->npassive].tree = WK_TREE[t_]; o->passive[o->npassive++].invested = has(inv, ninv, t_); } } } while (0)
    if (b->bg >= 0 && MUC_BG_OK[b->bg]) {
        for (int i = MUC_BG_MAIN_AT[b->bg]; i < MUC_BG_MAIN_AT[b->bg + 1]; i++) PUSH(MUC_BG_MAIN[i]);
        for (int i = MUC_BG_SUB_AT[b->bg]; i < MUC_BG_SUB_AT[b->bg + 1]; i++) PUSH(MUC_BG_SUB[i]);
    }
    for (int i = 0; i < ninv; i++) if (!has(b->trees, b->ntrees, inv[i])) PUSH(inv[i]);
#undef PUSH
    /* bankedStacks: milestone passives of a tree the unit has, with a level above 0 */
    for (int g = 0; g < MUC_PG_ORDER_N && o->nstacks < (int)(sizeof o->stacks / sizeof o->stacks[0]); g++) {
        int sk = MUC_PG_ORDER[g], nthr = MUC_PG_THR_AT[sk + 1] - MUC_PG_THR_AT[sk];
        if (!MUC_PG_AFF[sk] || !nthr || tree_of(at, nat, sk) < 0) continue;
        int learned = sk < WK_SKILL_N && has(b->learned, b->nlearned, sk);
        if (MUC_BANKED_NEEDS_SKILL && !learned) continue;
        int lv = stack_lv(b, sk, learned); if (lv <= 0) continue;
        o->stacks[o->nstacks].skill = MUC_SK[sk]; o->stacks[o->nstacks].lv = lv; o->stacks[o->nstacks].max = nthr;
        o->stacks[o->nstacks++].learned = learned;
    }
    /* displayInscriptions: tree inscriptions, the held ones, the save's exact list; known ones, no duplicates */
    int ins[MUC_MAX], nins = 0;
    for (int i = 0; i < b->ntrees; i++) { int k = MUC_TREE_INSC[b->trees[i]]; if (k >= 0 && MUC_INSC_OK[k] && !has(ins, nins, k) && nins < MUC_MAX) ins[nins++] = k; }
    for (int i = 0; i < b->ninsc; i++) { int k = b->insc[i]; if (MUC_INSC_OK[k] && !has(ins, nins, k) && nins < MUC_MAX) ins[nins++] = k; }
    for (int i = 0; i < b->nsaved_insc; i++) { int k = b->saved_insc[i]; if (MUC_INSC_OK[k] && !has(ins, nins, k) && nins < MUC_MAX) ins[nins++] = k; }
    for (int i = 0; i < nins; i++) o->insc[o->ninsc++] = WK_INSC[ins[i]];
    for (int i = 0; i < b->nsouls; i++) if (MUC_SOUL_OK[b->souls[i]]) o->souls[o->nsouls++] = WK_SOUL[b->souls[i]];
    /* acquiredTraits: the acquired list, then every saved trait the race cannot roll */
    for (int i = 0; i < b->nacq; i++) { const char *k = MUC_ACQ_KEY[b->acq[i]]; if (k && !has_s(o->acq, o->nacq, k) && o->nacq < MUC_MAX) o->acq[o->nacq++] = k; }
    const int *si = b->has_saved_inds ? b->saved_inds : b->inds; int nsi = b->has_saved_inds ? b->nsaved_inds : b->ninds;
    for (int i = 0; i < nsi; i++) {
        int k = si[i];
        if (MUC_IND_OK[k] && !MUC_RACE_IND[b->race][k] && !has_s(o->acq, o->nacq, WK_IND[k]) && o->nacq < MUC_MAX) o->acq[o->nacq++] = WK_IND[k];
    }
    for (int s = 0; s < MUC_SLOT_ORDER_N; s++) {
        int sl = MUC_SLOT_ORDER[s], it = sl >= 0 && sl < WK_SLOTS ? b->eq[sl] : -1;
        if (it >= 0 && MUC_ITEM_OK[it]) { o->eq[o->neq].slot = WK_SLOT[sl]; o->eq[o->neq++].item = WK_ITEM[it]; }
    }
    /* unitSkillSets: learned skills (actives / passives), then each worn item's own skills that are not its basic attack */
    int act[MUC_MAX], nact = 0, pas[MUC_MAX], npas = 0, bas[WK_SLOTS], nbas = 0;
    for (int i = 0; i < b->nlearned; i++) {
        int k = b->learned[i]; if (!MUC_SK_OK[k]) continue;
        if (MUC_SK_PASSIVE[k]) { if (!has(pas, npas, k)) pas[npas++] = k; }
        else if (!has(act, nact, k)) act[nact++] = k;
    }
    for (int s = 0; s < MUC_SLOT_ORDER_N; s++) {
        int sl = MUC_SLOT_ORDER[s], it = sl >= 0 && sl < WK_SLOTS ? b->eq[sl] : -1;
        if (it < 0 || !MUC_ITEM_OK[it]) continue;
        int bsk = MUC_ITEM_BSK[it];
        if (bsk >= 0 && !has(bas, nbas, bsk)) bas[nbas++] = bsk;
        for (int d = MUC_ITEM_DEF_AT[it]; d < MUC_ITEM_DEF_AT[it + 1]; d++) {
            int k = MUC_ITEM_DEF[d];
            if (k == bsk || has(bas, nbas, k) || has(act, nact, k) || has(pas, npas, k)) continue;
            if (MUC_SK_OK[k] && MUC_SK_PASSIVE[k]) { if (npas < MUC_MAX) pas[npas++] = k; } else if (nact < MUC_MAX) act[nact++] = k;
        }
    }
    /* quickFor: the actives that are not bar-only, capped at the shortcut slots (a link carries no own bar pick) */
    for (int i = 0; i < nact && o->nbar < MUC_QUICK_SLOTS && o->nbar < 16; i++) if (!MUC_SK_BARONLY[act[i]]) o->bar[o->nbar++] = MUC_SK[act[i]];
    for (int i = 0; i < npas; i++) {
        int k = pas[i], nthr = MUC_PG_THR_AT[k + 1] - MUC_PG_THR_AT[k];
        o->pass[o->npass].skill = MUC_SK[k];
        o->pass[o->npass].folded = MUC_PG[k] && MUC_PG_COND[k] && !nthr;
        o->pass[o->npass++].stack_lv = nthr ? stack_lv(b, k, k < WK_SKILL_N && has(b->learned, b->nlearned, k)) : -1;
    }
    return 0;
}

/* ---- JSON (tests, the rig read-back) */
typedef struct { char *p; size_t n, cap; } sb;
static void put(sb *s, const char *t) {
    size_t L = strlen(t);
    if (s->n + L + 1 > s->cap) { s->cap = (s->n + L + 1) * 2; s->p = realloc(s->p, s->cap); }
    memcpy(s->p + s->n, t, L + 1); s->n += L;
}
static void putq(sb *s, const char *t) {
    if (!t) { put(s, "null"); return; }
    put(s, "\"");
    for (const unsigned char *p = (const unsigned char *)t; *p; p++) {
        char e[8];
        if (*p == '"' || *p == '\\') { e[0] = '\\'; e[1] = (char)*p; e[2] = 0; }
        else if (*p < 32) snprintf(e, sizeof e, "\\u%04x", *p);
        else { e[0] = (char)*p; e[1] = 0; }
        put(s, e);
    }
    put(s, "\"");
}
static void puti(sb *s, int v) { char t[16]; snprintf(t, sizeof t, "%d", v); put(s, t); }
static void putl(sb *s, const char *k, const char *const *a, int n) {
    put(s, ",\""); put(s, k); put(s, "\":[");
    for (int i = 0; i < n; i++) { if (i) put(s, ","); putq(s, a[i]); }
    put(s, "]");
}
char *mu_card_json(const mu_card *c) {
    sb s = { 0 };
    put(&s, "{\"name\":"); putq(&s, c->name); put(&s, ",\"lvl\":"); puti(&s, c->lvl);
    put(&s, ",\"race\":"); putq(&s, c->race); put(&s, ",\"bg\":"); putq(&s, c->bg); put(&s, ",\"weapon\":"); putq(&s, c->weapon);
    put(&s, ",\"portrait\":"); putq(&s, c->portrait); put(&s, ",\"avatar\":"); putq(&s, c->avatar);
    put(&s, ",\"active\":[");
    for (int i = 0; i < c->nactive; i++) { put(&s, i ? ",[" : "["); putq(&s, c->active[i].tree); put(&s, ","); puti(&s, c->active[i].invested); put(&s, "]"); }
    put(&s, "],\"passive\":[");
    for (int i = 0; i < c->npassive; i++) { put(&s, i ? ",[" : "["); putq(&s, c->passive[i].tree); put(&s, ","); puti(&s, c->passive[i].invested); put(&s, "]"); }
    put(&s, "],\"stacks\":[");
    for (int i = 0; i < c->nstacks; i++) {
        put(&s, i ? ",[" : "["); putq(&s, c->stacks[i].skill); put(&s, ","); puti(&s, c->stacks[i].lv); put(&s, ",");
        puti(&s, c->stacks[i].max); put(&s, ","); puti(&s, c->stacks[i].learned); put(&s, "]");
    }
    put(&s, "]");
    putl(&s, "insc", c->insc, c->ninsc); putl(&s, "souls", c->souls, c->nsouls); putl(&s, "acq", c->acq, c->nacq);
    put(&s, ",\"eq\":[");
    for (int i = 0; i < c->neq; i++) { put(&s, i ? ",[" : "["); putq(&s, c->eq[i].slot); put(&s, ","); putq(&s, c->eq[i].item); put(&s, "]"); }
    put(&s, "]");
    putl(&s, "bar", c->bar, c->nbar);
    put(&s, ",\"passives\":[");
    for (int i = 0; i < c->npass; i++) {
        put(&s, i ? ",[" : "["); putq(&s, c->pass[i].skill); put(&s, ","); puti(&s, c->pass[i].folded); put(&s, ","); puti(&s, c->pass[i].stack_lv); put(&s, "]");
    }
    put(&s, "]}");
    return s.p;
}
