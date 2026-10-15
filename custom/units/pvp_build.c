/* pvp_build - wiki build -> unit closure (see pvp_build.h). Game rules used (build 25846389):
 *  - StatusComponent.Deserialize 0xDDEAAC / ApplyStructuredUnitStatus 0xDDEDAC: a SchemaVersion 2 unit is rebuilt on load
 *    from the defaults + GeneratedStats + ProgressionStats + PermanentStats, then the AdjustStats of every affecter it
 *    holds, then its gear. So the closure stores only the growth; race / background / trait / inscription / gear
 *    stats come back by themselves from the holders and the items.
 *  - generation (recruit): major = whole part of base x (1 + (race roll + background roll) / 100), rolls are the
 *    traits' <Major>PerModifierMin..Max (code truth builder_levelup_growth, RecruitHelper single-candidate builder).
 *  - level-up (LevelUpHelper.GetOfferedMajorStat 0xCA1808): per level and major, a whole roll in the talent range of
 *    GAMESYSTEM_LevelUp_Talent<Major><Tier> x StatIncreaseFactorByOffering, rounded half to even, into ProgressionStats.
 *  - skill points (UnitSpawner.SetUnitStatus 0xCAE714): main = DefaultMain + (level - 1) x MainIncrease, sub alike; a
 *    tree inscription adds 5 to its pool (SkillTreeInscriptionDefinition.get_Points 0xABF170). Saved: the unspent
 *    points (ProgressionStats Main/SubSkillPoint) and the spent ones (PlayerUnitsSaveData UsedSkillPoints).
 *  - skill cost (SkillPointCostCalculator.GetRequiredSkillPointCost 0xCC76E8): table SP - round half to even (the
 *    tree's aptitude stat); no floor.
 *  - Perfection inscription (Passive_PerfectionInscription 0x963E64): each talent up one tier, Genius stays.
 *  - elixirs (StatusComponent.AddPermanentValue 0xDDDAE0): FloatValues[0] of SKILL_<Major>Elixir per use, PermanentStats.
 *  - default skills of a player unit (UnitTable UNIT_PlayerUnit DefaultSkills): BasicPunchAttack + EmergencyEscape
 *    (a temp skill: PlayerUnitDataContainer.OnAddTempSkill -> ExtraSkills). Item default skills come with the item.
 * Where the builder shows something a save cannot hold, it goes to `notes`. */
#include "pvp_build.h"
#include "pvp_unit_tables.h"
#include "fe/fe_save.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *p; size_t n, cap; } sb;
static void sp(sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sp(sb *b, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); va_list a2; va_copy(a2, ap);
    int k = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    if (b->n + (size_t)k + 1 > b->cap) { b->cap = (b->n + (size_t)k + 1) * 2; b->p = realloc(b->p, b->cap); }
    vsnprintf(b->p + b->n, (size_t)k + 1, fmt, a2); va_end(a2); b->n += (size_t)k;
}
static void sq(sb *b, const char *s) {                 /* a JSON string */
    sp(b, "\"");
    for (; s && *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') sp(b, "\\%c", ch); else if (ch < 0x20) sp(b, "\\u%04x", ch); else sp(b, "%c", ch);
    }
    sp(b, "\"");
}
static void note(char *notes, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void note(char *notes, size_t n, const char *fmt, ...) {
    if (!notes || !n) return;
    size_t at = strlen(notes); if (at + 2 >= n) return;
    va_list ap; va_start(ap, fmt); vsnprintf(notes + at, n - at, fmt, ap); va_end(ap);
    at = strlen(notes); if (at + 1 < n) { notes[at] = '\n'; notes[at + 1] = 0; }
}
static double rhe(double x) { double r = nearbyint(x); return r; }     /* default FE_TONEAREST = half to even (Math.Round) */
static int aff_find(const char *k) {
    int lo = 0, hi = WU_AFF_N - 1;
    while (lo <= hi) { int m = (lo + hi) / 2, c = strcmp(WU_AFF_KEY[m], k); if (!c) return m; if (c < 0) lo = m + 1; else hi = m - 1; }
    return -1;
}

/* ---- what the unit holds (affecter keys, no duplicates) */
typedef struct { const char *k[160]; int n; } keys;
static void kadd(keys *K, const char *k) { if (!k || !*k) return; for (int i = 0; i < K->n; i++) if (!strcmp(K->k[i], k)) return; if (K->n < 160) K->k[K->n++] = k; }
static int khas(const keys *K, const char *k) { for (int i = 0; i < K->n; i++) if (!strcmp(K->k[i], k)) return 1; return 0; }
static int has_tree(const wk_build *b, const int *tr, int nt, int t) { (void)b; for (int i = 0; i < nt; i++) if (tr[i] == t) return 1; return 0; }
/* the unit's trees: background weapon trees, background sub trees, then the inscription trees */
static int unit_trees(const wk_build *b, int *tr) {
    int n = 0;
    if (b->bg >= 0) {
        for (int i = 0; i < 4; i++) if (WU_BG_MAINS[b->bg][i] >= 0) tr[n++] = WU_BG_MAINS[b->bg][i];
        for (int i = 0; i < 4; i++) if (WU_BG_SUBS[b->bg][i] >= 0) tr[n++] = WU_BG_SUBS[b->bg][i];
    }
    for (int i = 0; i < b->ntrees && n < 64; i++) if (!has_tree(b, tr, n, b->trees[i])) tr[n++] = b->trees[i];
    return n;
}
static void unit_affecters(const wk_build *b, keys *K) {
    kadd(K, WK_RACE[b->race]);
    if (b->bg >= 0) kadd(K, WK_BG[b->bg]);
    if (b->nsaved_inds) for (int i = 0; i < b->nsaved_inds; i++) kadd(K, WK_IND[b->saved_inds[i]]);
    else for (int i = 0; i < b->ninds; i++) kadd(K, WK_IND[b->inds[i]]);
    for (int i = 0; i < b->ntrees; i++) kadd(K, WU_TREE_INSC[b->trees[i]]);
    for (int i = 0; i < b->ninsc; i++) kadd(K, WK_INSC[b->insc[i]]);
    for (int i = 0; i < b->nsaved_insc; i++) kadd(K, WK_INSC[b->saved_insc[i]]);
    for (int i = 0; i < b->nsouls; i++) kadd(K, WK_SOUL[b->souls[i]]);
    for (int i = 0; i < b->nacq; i++) kadd(K, WK_ACQ[b->acq[i]]);
}

/* ---- stat sums from the holders and the gear: flat and % per stat name (only majors, major %, aptitudes are in the tables) */
typedef struct { char st[48]; double flat, pct; } statv;
typedef struct { statv s[64]; int n; } stats;
static statv *sget(stats *S, const char *st) {
    for (int i = 0; i < S->n; i++) if (!strcmp(S->s[i].st, st)) return &S->s[i];
    if (S->n >= 64) return NULL;
    statv *v = &S->s[S->n++]; snprintf(v->st, sizeof v->st, "%s", st); v->flat = v->pct = 0; return v;
}
static void sadd(stats *S, const char *st, double v) {
    size_t n = strlen(st); char base[48];
    if (n > 11 && !strcmp(st + n - 11, "PerModifier")) { snprintf(base, sizeof base, "%.*s", (int)(n - 11), st); statv *x = sget(S, base); if (x) x->pct += v; }
    else { statv *x = sget(S, st); if (x) x->flat += v; }
}
static void sadd_list(stats *S, const char *adj) {          /* "Stat=v;Stat=v" */
    char buf[400]; snprintf(buf, sizeof buf, "%s", adj ? adj : "");
    for (char *save = NULL, *t = strtok_r(buf, ";", &save); t; t = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(t, '='); if (!eq) continue; *eq = 0; sadd(S, t, atof(eq + 1));
    }
}
static void unit_stats(const wk_build *b, const keys *K, stats *S) {
    memset(S, 0, sizeof *S);
    for (int i = 0; i < K->n; i++) { int a = aff_find(K->k[i]); if (a >= 0) sadd_list(S, WU_AFF_ADJ[a]); }
    for (int s = 0; s < WK_SLOT_N && s < WK_SLOTS; s++) {
        int it = b->eq[s]; if (it < 0 || it >= WK_ITEM_N) continue;
        sadd_list(S, WU_ITEM_ADJ[it]);
        char oe[300]; snprintf(oe, sizeof oe, "%s", WU_ITEM_ONEQUIP[it]);
        for (char *save = NULL, *t = strtok_r(oe, ";", &save); t; t = strtok_r(NULL, ";", &save)) { int a = aff_find(t); if (a >= 0) sadd_list(S, WU_AFF_ADJ[a]); }
        for (int k = 0; k < b->ntag[s]; k++) {
            int ti = WU_ITEM[it].tag_at + b->tag[s][k].idx;
            if (b->tag[s][k].idx < WU_ITEM[it].ntag) sadd(S, WU_TAG_STAT[ti], b->tag[s][k].v);
        }
    }
}
static double sflat(stats *S, const char *st) { for (int i = 0; i < S->n; i++) if (!strcmp(S->s[i].st, st)) return S->s[i].flat; return 0; }
static double spct(stats *S, const char *st) { for (int i = 0; i < S->n; i++) if (!strcmp(S->s[i].st, st)) return S->s[i].pct; return 0; }

/* ---- majors: generated, progression, permanent per major */
typedef struct { double gen[6], prog[6], perm[6]; int talent[6]; } majors;
static void calc_majors(const wk_build *b, const keys *K, stats *S, majors *M) {
    memset(M, 0, sizeof *M);
    const double q = b->roll == 0 ? 0.0 : b->roll == 2 ? 1.0 : 0.5;
    const int bump = khas(K, "AFFECTER_PerfectionInscription");
    const float f = WU_OFFER[b->offer < 0 || b->offer > 2 ? 0 : b->offer];
    for (int m = 0; m < 6; m++) {
        M->talent[m] = b->tal[m] - 1;                                /* WK_TIER Poor..Genius -> save -1..3 */
        if (b->has_elix && b->elix[m] > 0) M->perm[m] = b->elix[m] * (double)WU_ELIXIR[m];
        if (b->has_mb) { M->gen[m] = b->mb[m]; continue; }          /* the save's own growth, carried by the link */
        const float B0 = WU_MAJOR_BASE[m];
        float rr = WU_RACE_ROLL[b->race][m][0] + (float)((WU_RACE_ROLL[b->race][m][1] - WU_RACE_ROLL[b->race][m][0]) * q);
        float br = b->bg >= 0 ? WU_BG_ROLL[b->bg][m][0] + (float)((WU_BG_ROLL[b->bg][m][1] - WU_BG_ROLL[b->bg][m][0]) * q) : 0.0f;
        M->gen[m] = truncf(((rr + br) / 100.0f + 1.0f) * B0) - B0;     /* RecruitHelper.ApplyModifiers 0xD60C08: (int)((sum / 100 + 1) x base) */
        int tier = b->tal[m] + bump; if (tier > 4) tier = 4;
        int lo = (int)WU_TALENT[tier][m][0], hi = (int)WU_TALENT[tier][m][1];
        double per;
        if (b->roll == 0) per = rhe((float)lo * f);
        else if (b->roll == 2) per = rhe((float)hi * f);
        else { per = 0; for (int k = lo; k <= hi; k++) per += rhe((float)k * f); per /= (hi - lo + 1); }
        M->prog[m] = per * (b->lvl - 1);
    }
    /* a manual major ("input my stats"): the stored growth that makes the sheet read that number,
       sheet = (base + growth + flat majors) x (1 + major % / 100) */
    for (int i = 0; i < b->nman; i++) for (int m = 0; m < 6; m++) if (!strcmp(WK_STAT[b->man[i].stat], WK_MAJ[m])) {
        double raw = b->man[i].v / (1.0 + spct(S, WK_MAJ[m]) / 100.0);
        M->gen[m] = raw - WU_MAJOR_BASE[m] - sflat(S, WK_MAJ[m]) - M->perm[m]; M->prog[m] = 0;
    }
}
/* ---- the builder's passive folding (owner rule 2026-10-14, CORE_TRUTHS "Builder passives at rest": the builder is a
   testing tool; a wiki import gives ITS numbers). builder.js computeBuild / stackCountFor:
   - unconditional passives: the game applies them itself;
   - conditional, non-milestone (First Assault, Bastion...): a hand-built unit gets the full value, an imported one
     (majorBase) only what its banked stack gives -> PermanentStats grants (P);
   - milestone passives (Sharp Shooter, Deep Water): a counter on the skill's binding holder; the game derives the stat
     from it on load. Count = the link's banked stack, else 0 for an imported unit, else the top threshold when learned. */
static int learned(const wk_build *b, int sk) { for (int i = 0; i < b->nlearned; i++) if (b->learned[i] == sk) return 1; return 0; }
static int pstk_of(const wk_build *b, int sk) { for (int i = 0; i < b->npstk; i++) if (b->pstk[i].skill == sk) return b->pstk[i].st; return -1; }
static int milestone_count(const wk_build *b, int sk, const int *tr, int nt) {
    const wu_pgrant *g = &WU_PG[sk];
    if (!g->known || !g->aff || !g->nthr || WU_SKILL[sk].tree < 0 || !has_tree(b, tr, nt, WU_SKILL[sk].tree)) return 0;
    int st = pstk_of(b, sk); if (st >= 0) return st;
    if (b->has_mb) return 0;
    return learned(b, sk) ? g->thr[g->nthr - 1] : 0;
}
static void passive_grants(const wk_build *b, stats *P) {
    memset(P, 0, sizeof *P);
    for (int i = 0; i < b->nlearned; i++) {
        int sk = b->learned[i]; const wu_pgrant *g = &WU_PG[sk];
        if (!g->known || !g->cond || g->nthr) continue;
        double mul = 1;
        if (b->has_mb) { int st = pstk_of(b, sk); if (st < 0) continue; mul = 0; for (int t = 0; t < g->nthr; t++) if (st >= g->thr[t]) mul++; }
        char buf[400]; snprintf(buf, sizeof buf, "%s", g->adj ? g->adj : "");
        for (char *sv = NULL, *t = strtok_r(buf, ";", &sv); t; t = strtok_r(NULL, ";", &sv)) {
            char *eq = strchr(t, '='); if (!eq) continue; *eq = 0;
            statv *v = sget(P, t); if (v) v->flat += (g->per >= 0 && b->has_mb ? g->per : atof(eq + 1)) * mul;
        }
    }
}
static void add_grants(stats *S, const stats *P) { for (int i = 0; i < P->n; i++) sadd(S, P->s[i].st, P->s[i].flat); }
void pb_stored_majors(const wk_build *b, double out[6]) {
    keys K = { 0 }; stats S, P; majors M;
    unit_affecters(b, &K); unit_stats(b, &K, &S); passive_grants(b, &P); add_grants(&S, &P); calc_majors(b, &K, &S, &M);
    for (int m = 0; m < 6; m++) { const statv *v = NULL; for (int i = 0; i < P.n; i++) if (!strcmp(P.s[i].st, WK_MAJ[m])) v = &P.s[i];
        out[m] = WU_MAJOR_BASE[m] + M.gen[m] + M.prog[m] + M.perm[m] + (v ? v->flat : 0); }
}

static const char *const FLAT_ITEM_STATS[11] = { "CriticalChance", "CriticalDamageBonus", "CooldownReduction", "HitChance", "DodgeChance",
    "GuardChance", "GuardDamageReduction", "PhysicalResistance", "MagicResistance", "MovementSpeed", "HealthPercentageRegeneration" };
static const char *const ZERO = "00000000-0000-0000-0000-000000000000";
static void empty_slot(sb *o) { sp(o, "{\"Guid\":\"%s\",\"Key\":\"\",\"Amount\":0,\"AutoFixed\":false}", ZERO); }

jv *pb_entry(const wk_build *b, const char *player_uid, char *notes, size_t notesn, char *err, size_t errn) {
    if (notes && notesn) notes[0] = 0;
    if (b->race < 0 || b->race >= WK_RACE_N || WU_PROF_N[b->race] <= 0) { snprintf(err, errn, "race %s has no look (portraits) in this game build", wk_race_key(b->race)); return NULL; }
    if (b->lvl < 1 || b->lvl > WU_MAX_LEVEL) { snprintf(err, errn, "level %d is outside 1..%d", b->lvl, (int)WU_MAX_LEVEL); return NULL; }
    for (int m = 0; m < 6; m++) if (b->tal[m] < 0 || b->tal[m] > 4) { snprintf(err, errn, "talent tier out of range"); return NULL; }
    keys K = { 0 }; stats S, PG; majors M;
    unit_affecters(b, &K); unit_stats(b, &K, &S); passive_grants(b, &PG); add_grants(&S, &PG); calc_majors(b, &K, &S, &M);
    int tr[72], nt = unit_trees(b, tr);
    /* gear: each item in its own slot */
    for (int s = 0; s < WK_SLOT_N && s < WK_SLOTS; s++) {
        int it = b->eq[s]; if (it < 0) continue;
        if (it >= WK_ITEM_N || WU_ITEM[it].slot != s) { snprintf(err, errn, "%s cannot go in the %s slot", wk_item_key(it), wk_slot_name(s)); return NULL; }
        for (int k = 0; k < b->ntag[s]; k++) {
            int ix = b->tag[s][k].idx; if (ix >= WU_ITEM[it].ntag) { snprintf(err, errn, "%s: bonus %d is not in its pool", WK_ITEM[it], ix); return NULL; }
            const float *lh = WU_TAG_LOHI[WU_ITEM[it].tag_at + ix];
            if (b->tag[s][k].v < lh[0] - 1e-3 || b->tag[s][k].v > lh[1] + 1e-3) { snprintf(err, errn, "%s: %s %.2f is outside %.2f..%.2f", WK_ITEM[it], WU_TAG_STAT[WU_ITEM[it].tag_at + ix], b->tag[s][k].v, lh[0], lh[1]); return NULL; }
        }
        if (b->ntag[s] > WU_ITEM[it].tagamount) note(notes, notesn, "%s carries %d bonus(es); the game rolls %d", WK_ITEM[it], b->ntag[s], WU_ITEM[it].tagamount);
    }
    /* skills: each learned skill in one of the unit's trees; cost = table SP - round(aptitude) */
    double used[2] = { 0, 0 };
    for (int i = 0; i < b->nlearned; i++) {
        int sk = b->learned[i], t = WU_SKILL[sk].tree;
        if (t < 0 || !has_tree(b, tr, nt, t)) { snprintf(err, errn, "%s is not in one of this unit's skill trees", WK_SKILL[sk]); return NULL; }
        int kind = WU_TREE_KIND[t]; if (kind < 0) { snprintf(err, errn, "tree %s has no pool", WK_TREE[t]); return NULL; }
        double apt = WU_TREE_APT[t] ? rhe(sflat(&S, WU_TREE_APT[t])) : 0;
        used[kind] += kind == 0 ? WU_SKILL[sk].sp - apt : WU_SKILL[sk].sp;
    }
    double budget[2] = { WU_SP[0] + WU_SP[2] * (b->lvl - 1), WU_SP[1] + WU_SP[3] * (b->lvl - 1) };
    for (int i = 0; i < b->ntrees; i++) if (WU_TREE_INSC[b->trees[i]]) budget[WU_TREE_KIND[b->trees[i]] == 0 ? 0 : 1] += 5;
    for (int k = 0; k < 2; k++) if (used[k] > budget[k] + 1e-6) { snprintf(err, errn, "%s skills cost %.0f points, the unit has %.0f", k ? "sub" : "main", used[k], budget[k]); return NULL; }
    for (int i = 0; i < b->nman; i++) {
        int major = 0; for (int m = 0; m < 6; m++) if (!strcmp(WK_STAT[b->man[i].stat], WK_MAJ[m])) major = 1;
        if (!major) note(notes, notesn, "typed %s %.2f: a save stores no such number (the game computes it)", WK_STAT[b->man[i].stat], b->man[i].v);
    }
    for (int g = 0; g < b->ncond && g < 8; g++) if (b->cond[g]) note(notes, notesn, "hunger / exhaustion setting %d: a passing state, not carried", g);
    /* profile */
    int pi = b->portrait >= 0 && b->portrait < WU_PROF_N[b->race] ? b->portrait : 0;
    const wu_profile *P = &WU_PROF[b->race][pi];
    char defname[96]; snprintf(defname, sizeof defname, "TEXTKEY_PLAYERNAME_%s_%s_0", WU_RACE_NAME[b->race], P->gender ? "Female" : "Male");

    char ug[37]; fe_new_guid(ug);
    char ig[WK_SLOTS][37];
    sb o = { 0 };
    sp(&o, "{\"fe_closure\":1,\"unit\":{\"guid\":\"%s\",\"side\":\"Base\",\"components\":[", ug);
    /* 0 status */
    sp(&o, "{\"Type\":0,\"Data\":{\"$type\":\"Refactor.StatusComponentSaveData, Assembly-CSharp\",\"SchemaVersion\":2,"
           "\"StateStats\":{\"HealthHead\":1.0,\"HealthBody\":1.0,\"Energy\":1.0,\"Hunger\":1.0,\"Stress\":0.0},\"ProgressionStats\":{\"Level\":%d.0", b->lvl);
    static const char *const TAL[6] = { "TalentStrength", "TalentConstitution", "TalentWillPower", "TalentIntelligence", "TalentAgility", "TalentPerception" };
    for (int m = 0; m < 6; m++) sp(&o, ",\"%s\":%d.0", TAL[m], M.talent[m]);
    sp(&o, ",\"MainSkillPoint\":%.6g,\"SubSkillPoint\":%.6g,\"Exp\":0.0", budget[0] - used[0], budget[1] - used[1]);
    for (int m = 0; m < 6; m++) if (M.prog[m] != 0) sp(&o, ",\"%s\":%.6g", WK_MAJ[m], M.prog[m]);
    sp(&o, "},\"GeneratedStats\":{");
    for (int m = 0; m < 6; m++) sp(&o, "%s\"%s\":%.6g", m ? "," : "", WK_MAJ[m], M.gen[m]);
    sp(&o, "},\"PermanentStats\":{");
    {   /* elixirs + the builder's folded conditional passives (StatusComponent.AddPermanentValue keeps any StatType here) */
        stats T; memset(&T, 0, sizeof T);
        for (int m = 0; m < 6; m++) if (M.perm[m] != 0) { statv *v = sget(&T, WK_MAJ[m]); if (v) v->flat += M.perm[m]; }
        for (int i = 0; i < PG.n; i++) { statv *v = sget(&T, PG.s[i].st); if (v) v->flat += PG.s[i].flat; }
        for (int i = 0, k = 0; i < T.n; i++) if (T.s[i].flat != 0) sp(&o, "%s\"%s\":%.6g", k++ ? "," : "", T.s[i].st, T.s[i].flat);
    }
    sp(&o, "}}},");
    sp(&o, "{\"Type\":1,\"Data\":{\"$type\":\"Refactor.EntityInfoComponentSaveData, Assembly-CSharp\",\"Faction\":1,\"IsDead\":false,\"Option\":0}},");
    /* 3 affecters: the unit's holders; a stacking passive keeps its counter as its binding holder's Stack */
    sp(&o, "{\"Type\":3,\"Data\":{\"$type\":\"Refactor.AffecterComponentSaveData, Assembly-CSharp\",\"AffecterHolders\":[");
    int nh = 0;
    for (int i = 0; i < K.n; i++) sp(&o, "%s{\"Key\":\"%s\",\"Stack\":1,\"Duration\":0.0,\"InitialDuration\":0.0}", nh++ ? "," : "", K.k[i]);
    for (int sk = 0; sk < WK_SKILL_N; sk++) {                         /* milestone counters (see milestone_count) */
        int n = milestone_count(b, sk, tr, nt); if (n <= 0 || khas(&K, WU_PG[sk].aff)) continue;
        sp(&o, "%s{\"Key\":\"%s\",\"Stack\":%d,\"Duration\":0.0,\"InitialDuration\":0.0}", nh++ ? "," : "", WU_PG[sk].aff, n);
    }
    for (int i = 0; i < b->npstk; i++) if (!WU_PG[b->pstk[i].skill].known || !WU_PG[b->pstk[i].skill].nthr)
        note(notes, notesn, "%s: stack %d is not a milestone counter in the builder, dropped", WK_SKILL[b->pstk[i].skill], b->pstk[i].st);
    sp(&o, "],\"InactiveMoodCauseHolders\":[]}},");
    sp(&o, "{\"Type\":4,\"Data\":{\"$type\":\"Refactor.CombatComponentSaveData, Assembly-CSharp\",\"State\":0}},");
    sp(&o, "{\"Type\":5,\"Data\":{\"$type\":\"Refactor.InventoryComponentSaveData, Assembly-CSharp\",\"ItemSlots\":[");
    for (int i = 0; i < 36; i++) { if (i) sp(&o, ","); empty_slot(&o); }
    sp(&o, "],\"TempSlot\":"); empty_slot(&o); sp(&o, "}},");
    sp(&o, "{\"Type\":6,\"Data\":{\"$type\":\"Refactor.InventoryComponentSaveData, Assembly-CSharp\",\"ItemSlots\":[");
    for (int s = 0; s < WK_SLOT_N && s < WK_SLOTS; s++) {
        if (s) sp(&o, ",");
        if (b->eq[s] < 0) { empty_slot(&o); ig[s][0] = 0; continue; }
        fe_new_guid(ig[s]);
        sp(&o, "{\"Guid\":\"%s\",\"Key\":\"%s\",\"Amount\":1,\"AutoFixed\":true}", ig[s], WK_ITEM[b->eq[s]]);
    }
    sp(&o, "],\"TempSlot\":"); empty_slot(&o); sp(&o, "}},");
    sp(&o, "{\"Type\":7,\"Data\":{\"$type\":\"Refactor.TaskComponentSaveData, Assembly-CSharp\",\"TaskType\":0,\"TaskGroupGuid\":\"%s\",\"TaskDataGuid\":\"%s\",\"TargetString\":null}},", ZERO, ZERO);
    /* 8 skills: basic attacks, actives, passives (defaults + item skills + learned) */
    const char *basic[24], *act[96], *pas[96], *extra[24]; int nb = 0, na = 0, np = 0, ne = 0;
#define ADDU(arr, n, k) do { int _d = 0; for (int _i = 0; _i < n; _i++) if (!strcmp(arr[_i], k)) _d = 1; if (!_d && n < (int)(sizeof arr / sizeof arr[0])) arr[n++] = k; } while (0)
    ADDU(basic, nb, "SKILL_BasicPunchAttack"); ADDU(act, na, "SKILL_EmergencyEscape"); ADDU(extra, ne, "SKILL_EmergencyEscape");
    for (int s = 0; s < WK_SLOT_N && s < WK_SLOTS; s++) {
        int it = b->eq[s]; if (it < 0) continue;
        for (int k = 0; k < WU_ITEM[it].nskill; k++) {
            const char *sk = WU_ITEM_SKILLS[WU_ITEM[it].skill_at + k]; int ty = WU_ITEM_SKILL_TYPE[WU_ITEM[it].skill_at + k];
            if (ty == 2) ADDU(basic, nb, sk); else if (ty == 1) { ADDU(pas, np, sk); ADDU(extra, ne, sk); } else { ADDU(act, na, sk); ADDU(extra, ne, sk); }
        }
    }
    for (int i = 0; i < b->nlearned; i++) { const char *sk = WK_SKILL[b->learned[i]]; if (WU_SKILL[b->learned[i]].passive) ADDU(pas, np, sk); else ADDU(act, na, sk); }
    sp(&o, "{\"Type\":8,\"Data\":{\"$type\":\"Refactor.SkillComponentSaveData, Assembly-CSharp\",\"BasicSkills\":[");
    for (int i = 0; i < nb; i++) sp(&o, "%s\"%s\"", i ? "," : "", basic[i]);
    sp(&o, "],\"ActiveSkills\":["); for (int i = 0; i < na; i++) sp(&o, "%s\"%s\"", i ? "," : "", act[i]);
    sp(&o, "],\"PassiveSkills\":["); for (int i = 0; i < np; i++) sp(&o, "%s\"%s\"", i ? "," : "", pas[i]);
    sp(&o, "],\"EquipBypassList\":[],\"Cooldowns\":{},\"SkillDurations\":{},\"AutoUse\":{");
    for (int i = 0; i < na; i++) sp(&o, "%s\"%s\":true", i ? "," : "", act[i]);
    sp(&o, "}}},");
    sp(&o, "{\"Type\":9,\"Data\":{\"$type\":\"Refactor.CarryComponentSaveData, Assembly-CSharp\",\"CarriedGuid\":\"%s\",\"CarrierGuid\":\"%s\",\"IsDead\":false}},", ZERO, ZERO);
    sp(&o, "{\"Type\":10,\"Data\":{\"$type\":\"Refactor.UnitProfileComponentSaveData, Assembly-CSharp\",\"ProfileKey\":\"%s\",\"UnitNameTextKey\":", P->key);
    if (b->name[0]) sq(&o, b->name); else sq(&o, defname);
    sp(&o, ",\"DefaultUnitNameTextKey\":\"%s\",\"UnitBioTextKey\":\"\",\"UnitPortraitID\":\"%s\",\"UnitVisualType\":%d,\"HeadType\":%d,\"GenderType\":%d,"
           "\"BodyType\":%d,\"SkinColorType\":%d,\"HairType\":%d,\"FacialDecoType\":%d,\"EyesType\":%d,\"HideHeadEquipment\":%s}},",
       defname, P->portrait, P->vis, P->head, P->gender, P->body, P->skin, P->hair, P->facial, P->eyes, b->hide_head ? "true" : "false");
    sp(&o, "{\"Type\":15,\"Data\":{\"$type\":\"Refactor.TradeDataComponentSaveData, Assembly-CSharp\",\"GoldAmount\":0,\"HasRefreshTimer\":false,\"TradeRefreshType\":0,"
           "\"TradePoolRefreshKey\":\"TRADEPOOL_GuildMerchant\",\"TradeTickPassed\":0.0,\"TradeRefreshInterval\":0.0,\"DiscountRates\":[");
    for (int i = 0; i < 36; i++) sp(&o, "%s0.0", i ? "," : "");
    sp(&o, "]}}],\"registry\":{\"Guid\":\"%s\",\"Key\":\"UNIT_PlayerUnit\"},\"position\":null,\"anim\":{},", ug);
    /* PlayerUnitsSaveData records */
    sp(&o, "\"pusd\":{\"SkillTrees\":{\"SkillTrees\":{\"Main\":[");
    for (int i = 0, k = 0; i < nt; i++) if (WU_TREE_KIND[tr[i]] == 0) sp(&o, "%s\"%s\"", k++ ? "," : "", WK_TREE[tr[i]]);
    sp(&o, "],\"Sub\":[");
    for (int i = 0, k = 0; i < nt; i++) if (WU_TREE_KIND[tr[i]] == 1) sp(&o, "%s\"%s\"", k++ ? "," : "", WK_TREE[tr[i]]);
    sp(&o, "],\"Other\":[]},\"ExtraSkills\":[");
    for (int i = 0; i < ne; i++) sp(&o, "%s\"%s\"", i ? "," : "", extra[i]);
    sp(&o, "],\"UsedSkillPoints\":{\"Main\":%.6g,\"Sub\":%.6g,\"Other\":0},\"LearnedSkills\":{", used[0], used[1]);
    for (int i = 0; i < nt; i++) {
        sp(&o, "%s\"%s\":[", i ? "," : "", WK_TREE[tr[i]]);
        for (int j = 0, k = 0; j < b->nlearned; j++) if (WU_SKILL[b->learned[j]].tree == tr[i]) sp(&o, "%s\"%s\"", k++ ? "," : "", WK_SKILL[b->learned[j]]);
        sp(&o, "]");
    }
    sp(&o, "}},\"SkillLearnNotices\":{\"HasSnapshot\":true,\"ShowNotice\":false,\"MainSkillPoint\":%.6g,\"SubSkillPoint\":%.6g},", budget[0] - used[0], budget[1] - used[1]);
    sp(&o, "\"QuickSlots\":{\"QuickSlots\":[");
    for (int i = 0, k = 0; i < na && k < 5; i++) if (strcmp(act[i], "SKILL_EmergencyEscape")) sp(&o, "%s\"%s\"", k++ ? "," : "", act[i]);
    sp(&o, "],\"ItemQuickSlotKey\":null},\"ConsumePolicies\":{\"Policy\":{}},\"TaskPriorities\":{\"Priority\":{\"PickUp\":2,\"Feeding\":2,\"Research\":2,"
           "\"Construct\":2,\"Demolition\":2,\"Store\":2,\"Crafting\":2,\"Cooking\":2,\"Farming\":2,\"Harvesting\":2,\"Gathering\":2,\"Logging\":2,\"Mining\":2}},"
           "\"WayPointQueues\":[],\"RecruitCosts\":0},\"prayer\":null,\"exp_stamp\":null},\"items\":{");
    /* items: own entity (status with the rolled bonuses and durability, info, empty affecters) */
    for (int s = 0, k = 0; s < WK_SLOT_N && s < WK_SLOTS; s++) {
        int it = b->eq[s]; if (it < 0) continue;
        sp(&o, "%s\"%s\":{\"components\":[{\"Type\":0,\"Data\":{\"$type\":\"Refactor.StatusComponentSaveData, Assembly-CSharp\",\"SchemaVersion\":0,\"Stats\":{", k++ ? "," : "", ig[s]);
        stats T; memset(&T, 0, sizeof T);                   /* the flat keys a stock item carries, then the rolled bonuses */
        for (int j = 0; j < 11; j++) sget(&T, FLAT_ITEM_STATS[j]);
        for (int j = 0; j < b->ntag[s]; j++) { statv *v = sget(&T, WU_TAG_STAT[WU_ITEM[it].tag_at + b->tag[s][j].idx]); if (v) v->flat += b->tag[s][j].v; }
        for (int j = 0; j < T.n; j++) sp(&o, "\"%s\":%.6g,", T.s[j].st, T.s[j].flat);
        sp(&o, "\"Durability\":%d.0,\"MaxDurability\":%d.0},\"StateStats\":null,\"ProgressionStats\":null,\"GeneratedStats\":null,\"PermanentStats\":null}},", WU_ITEM[it].dur, WU_ITEM[it].dur);
        sp(&o, "{\"Type\":1,\"Data\":{\"$type\":\"Refactor.EntityInfoComponentSaveData, Assembly-CSharp\",\"Faction\":0,\"IsDead\":false,\"Option\":0}},"
               "{\"Type\":3,\"Data\":{\"$type\":\"Refactor.AffecterComponentSaveData, Assembly-CSharp\",\"AffecterHolders\":[],\"InactiveMoodCauseHolders\":[]}}],"
               "\"registry\":{\"Guid\":\"%s\",\"Key\":\"%s\"},\"autofix\":true}", ig[s], WK_ITEM[it]);
    }
    sp(&o, "}}");
    char perr[200] = "";
    jv *cl = jparse(o.p, o.n, perr, sizeof perr);
    free(o.p);
    if (!cl) { snprintf(err, errn, "internal: closure JSON: %s", perr); return NULL; }
    /* manifest (fe_export_entry's fields; home = the wiki link) */
    jv *m = jobj(); char id[33], h[17], ps[17], hv[24];
    fe_new_id(id); snprintf(h, sizeof h, "%016llx", (unsigned long long)jhash(cl)); fe_progress_sig(cl, ps);
    snprintf(hv, sizeof hv, "%016llx", (unsigned long long)wk_hash(b, 1));
    jset(m, "export_id", jstrv(id)); jset(m, "player_uid", jstrv(player_uid ? player_uid : ""));
    jset(m, "home_campaign_guid", jstrv(ZERO)); jset(m, "home_save_name", jstrv("wiki link")); jset(m, "home_save_version", jstrv(WU_GAME_VERSION));   /* fe_build_hub refuses a unit of another version */
    jset(m, "unit_guid", jstrv(ug)); jset(m, "closure_hash", jstrv(h)); jset(m, "progress_sig", jstrv(ps));
    jv *pr = fe_comp(jpath(cl, "unit.components"), 10);
    jset(m, "name_key", jcopy(jget(pr, "UnitNameTextKey"))); jset(m, "level", jnum_i(b->lvl)); jset(m, "exp", jnum_i(0));
    jset(m, "source", jstrv("wiki")); jset(m, "wiki_hash", jstrv(hv));
    jv *e = jobj(); jset(e, "closure", cl); jset(e, "manifest", m);
    return e;
}

/* the builder folds this skill's conditional bonus into PermanentStats (passive_grants): then its live grant must not run
   too (PassiveActionContainer.Register hook, pvp_game.c). *stat = the first stat it grants (the marker on the unit). */
int pb_folded_skill(const char *key, char *stat, size_t n) {
    for (int sk = 0; sk < WK_SKILL_N; sk++) if (!strcmp(WK_SKILL[sk], key)) {
        const wu_pgrant *g = &WU_PG[sk];
        if (!g->known || !g->cond || g->nthr || !g->adj) return 0;
        const char *eq = strchr(g->adj, '='); if (!eq) return 0;
        snprintf(stat, n, "%.*s", (int)(eq - g->adj), g->adj); return 1;
    }
    return 0;
}
