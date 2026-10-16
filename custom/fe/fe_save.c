/* Foreign Expedition save surgery: see fe_save.h. Rules and comments: ../../fe_save.py + ../../NOTES.md. */
#include "fe_save.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------- ids */
static uint64_t seed_state;
void fe_guid_seed(uint64_t seed) { seed_state = seed; }
static uint64_t rnd64(void) {
    if (seed_state) { uint64_t z = (seed_state += 0x9E3779B97F4A7C15ULL); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); }
    uint64_t r; arc4random_buf(&r, sizeof r); return r;
}
void fe_new_guid(char out[37]) {
    uint8_t b[16]; uint64_t a = rnd64(), c = rnd64(); memcpy(b, &a, 8); memcpy(b + 8, &c, 8);
    b[6] = (b[6] & 0x0F) | 0x40; b[8] = (b[8] & 0x3F) | 0x80;
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}
void fe_new_id(char out[33]) { snprintf(out, 33, "%016llx%016llx", (unsigned long long)rnd64(), (unsigned long long)rnd64()); }
static int is_guid(const char *s, size_t n) {
    if (n != 36) return 0;
    for (size_t i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (s[i] != '-') return 0; }
        else if (!isxdigit((unsigned char)s[i])) return 0;
    }
    return 1;
}
static int zero(const jv *v) { return !v || v->t != JN_STR || !v->n || jstreq(v, FE_ZERO); }

/* ---------------------------------------------------------------- tiny helpers */
static jv *obj_at(jv *o, const char *key) {           /* member, created as {} when missing */
    jv *v = jget(o, key);
    if (!v) { jset(o, key, jobj()); v = jget(o, key); }
    return v;
}
jv *fe_comp(const jv *comps, int type) {
    if (!jis(comps, JN_ARR)) return NULL;
    for (size_t i = 0; i < comps->cnt; i++)
        if ((int)jnumv(jget(comps->v[i], "Type")) == type && jget(comps->v[i], "Type")) return jget(comps->v[i], "Data");
    return NULL;
}
static jv *ec_of(const jv *s) { return jpath(s, "EntityComponentSaveData.EntityComponents"); }
static jv *ucs(const jv *s) { return jpath(s, "EntityContainerSaveData.UnitContainerSaveData"); }
const char *fe_side_of(const jv *save, const char *g) {
    jv *u = ucs(save);
    for (size_t i = 0; u && i < u->cnt; i++) {
        jv *pl = jget(u->v[i], "PlayerGuidList");
        for (size_t j = 0; pl && j < pl->cnt; j++) if (jstreq(pl->v[j], g)) return u->k[i];
    }
    return NULL;
}
int fe_clan_count(const jv *save) {
    jv *u = ucs(save); int n = 0;
    for (size_t i = 0; u && i < u->cnt; i++) { jv *pl = jget(u->v[i], "PlayerGuidList"); n += pl ? (int)pl->cnt : 0; }
    return n;
}
static jv *clan_list(const jv *save) {                /* [[guid, side], ...] */
    jv *out = jarr(), *u = ucs(save);
    for (size_t i = 0; u && i < u->cnt; i++) {
        jv *pl = jget(u->v[i], "PlayerGuidList");
        for (size_t j = 0; pl && j < pl->cnt; j++) { jv *r = jarr(); jpush(r, jcopy(pl->v[j])); jpush(r, jstrv(u->k[i])); jpush(out, r); }
    }
    return out;
}
static void item_guids_of(const jv *comps, jv *out) {   /* appends string values */
    for (size_t i = 0; comps && comps->t == JN_ARR && i < comps->cnt; i++) {
        jv *d = jget(comps->v[i], "Data");
        jv *sl = jget(d, "ItemSlots");
        for (size_t j = 0; sl && sl->t == JN_ARR && j < sl->cnt; j++) { jv *g = jget(sl->v[j], "Guid"); if (!zero(g)) jpush(out, jcopy(g)); }
        jv *t = jget(d, "TempSlot");
        if (jis(t, JN_OBJ)) { jv *g = jget(t, "Guid"); if (!zero(g)) jpush(out, jcopy(g)); }
    }
}
static int arr_has(const jv *a, const char *s) { for (size_t i = 0; a && i < a->cnt; i++) if (jstreq(a->v[i], s)) return 1; return 0; }
static void arr_remove_str(jv *a, const char *s) { for (size_t i = 0; a && a->t == JN_ARR && i < a->cnt; ) if (jstreq(a->v[i], s)) jarr_del(a, i); else i++; }
static void hex16(uint64_t h, char out[17]) { snprintf(out, 17, "%016llx", (unsigned long long)h); }

/* deep copy with every string key / value in map (object old -> new) replaced */
static jv *remap(const jv *v, const jv *map) {
    if (!v) return NULL;
    if (v->t == JN_STR) { jv *m = map ? jget(map, v->s) : NULL; return jcopy(m ? m : v); }
    if (v->t == JN_ARR) { jv *a = jarr(); for (size_t i = 0; i < v->cnt; i++) jpush(a, remap(v->v[i], map)); return a; }
    if (v->t == JN_OBJ) {
        jv *o = jobj();
        for (size_t i = 0; i < v->cnt; i++) { jv *m = map ? jget(map, v->k[i]) : NULL; jset(o, m ? m->s : v->k[i], remap(v->v[i], map)); }
        return o;
    }
    return jcopy(v);
}
static void reset_task(jv *d) {
    for (size_t i = 0; d && d->t == JN_OBJ && i < d->cnt; i++) {
        if (!strcmp(d->k[i], "$type")) continue;
        jv *v = d->v[i], *n = NULL;
        if (v->t == JN_STR && is_guid(v->s, v->n)) n = jstrv(FE_ZERO);
        else if (v->t == JN_TRUE || v->t == JN_FALSE) n = jbool(0);
        else if (v->t == JN_NUM) n = jnum_i(0);
        else if (v->t == JN_STR) n = jnull();
        else if (v->t == JN_ARR) n = jarr();
        if (n) { jfree(d->v[i]); d->v[i] = n; }
    }
}

/* ---------------------------------------------------------------- export */
jv *fe_export_unit(const jv *save, const char *g, char *err, size_t errn) {
    jv *ec = ec_of(save);
    const char *sd = fe_side_of(save, g);
    if (!sd || !jget(ec, g)) { snprintf(err, errn, "unit %s is not a clan unit of this save", g); return NULL; }
    jv *ecd = jget(save, "EntityContainerSaveData");
    jv *comps = jcopy(jget(ec, g));
    reset_task(fe_comp(comps, 7));                                  /* a job in the home world means nothing elsewhere */
    jv *cy = fe_comp(comps, 9);
    if (cy) { jset(cy, "CarriedGuid", jstrv(FE_ZERO)); jset(cy, "CarrierGuid", jstrv(FE_ZERO)); }
    jv *u = jobj();
    jset(u, "guid", jstrv(g)); jset(u, "side", jstrv(sd)); jset(u, "components", comps);
    jv *reg = jget(jget(jget(jget(ecd, "UnitContainerSaveData"), sd), "UnitSaveDataDictionary"), g);
    jset(u, "registry", reg ? jcopy(reg) : jnull());
    jv *pos = jget(jget(jget(ecd, "EntityPositions"), sd), g);
    jset(u, "position", pos ? jcopy(pos) : jnull());
    jv *anim = jobj(), *st = jpath(save, "AnimationStateSaveData.States");
    for (size_t i = 0; st && st->t == JN_OBJ && i < st->cnt; i++) { jv *a = jget(st->v[i], g); if (a) jset(anim, st->k[i], jcopy(a)); }
    jset(u, "anim", anim);
    jv *pusd = jobj(), *pu = jget(save, "PlayerUnitsSaveData");
    for (size_t i = 0; pu && i < pu->cnt; i++) { jv *r = jis(pu->v[i], JN_OBJ) ? jget(pu->v[i], g) : NULL; if (r) jset(pusd, pu->k[i], jcopy(r)); }
    jset(u, "pusd", pusd);
    jv *pr = jget(jpath(save, "CampaignRandomSaveData.LevelUpPrayerAttemptIndexByUnitGuid"), g);
    jset(u, "prayer", pr ? jcopy(pr) : jnull());
    jv *es = jget(jpath(save, "ReportSaveData.LevelUpTimeData.UnitExpMaxStamp"), g);
    jset(u, "exp_stamp", es ? jcopy(es) : jnull());
    jv *items = jobj(), *todo = jarr();
    item_guids_of(comps, todo);
    jv *ic = jget(jget(ecd, "ItemContainerSaveData"), sd);
    while (todo->cnt) {
        jv *x = todo->v[todo->cnt - 1]; char ig[64]; snprintf(ig, sizeof ig, "%s", x->s); jarr_del(todo, todo->cnt - 1);
        if (jget(items, ig)) continue;
        jv *ie = jget(ec, ig);
        if (!ie) { snprintf(err, errn, "item %s of unit %s has no entity in this save", ig, g); jfree(todo); jfree(items); jfree(u); return NULL; }
        jv *it = jobj(), *cc = jcopy(ie);
        item_guids_of(cc, todo);
        jset(it, "components", cc);
        jv *ir = jget(jget(ic, "ItemSaveData"), ig); jset(it, "registry", ir ? jcopy(ir) : jnull());
        jv *af = jget(jget(ic, "ItemAutoFixable"), ig); jset(it, "autofix", af ? jcopy(af) : jnull());
        jset(items, ig, it);
    }
    jfree(todo);
    jv *cl = jobj();
    jset(cl, "fe_closure", jnum_i(1)); jset(cl, "unit", u); jset(cl, "items", items);
    return cl;
}

void fe_progress_sig(const jv *closure, char out[17]) {
    jv *c = jpath(closure, "unit.components");
    jv *st = fe_comp(c, 0), *sk = fe_comp(c, 8), *eq = fe_comp(c, 6), *sig = jobj(), *skills = jarr(), *keys = jarr();
    jv *x;
    jset(sig, "prog", (x = jget(st, "ProgressionStats")) ? jcopy(x) : jnull());
    jset(sig, "gen", (x = jget(st, "GeneratedStats")) ? jcopy(x) : jnull());
    jset(sig, "perm", (x = jget(st, "PermanentStats")) ? jcopy(x) : jnull());
    const char *sn[3] = { "BasicSkills", "ActiveSkills", "PassiveSkills" };
    for (int i = 0; i < 3; i++) jpush(skills, (x = jget(sk, sn[i])) ? jcopy(x) : jnull());
    jset(sig, "skills", skills);
    jset(sig, "trees", (x = jget(jpath(closure, "unit.pusd"), "SkillTrees")) ? jcopy(x) : jnull());
    jv *sl = jget(eq, "ItemSlots");
    for (size_t i = 0; sl && i < sl->cnt; i++) jpush(keys, (x = jget(sl->v[i], "Key")) ? jcopy(x) : jnull());
    jset(sig, "equip", keys);
    hex16(jhash(sig), out);
    jfree(sig);
}

jv *fe_export_entry(const jv *save, const char *g, const char *player_uid, char *err, size_t errn) {
    jv *cl = fe_export_unit(save, g, err, errn);
    if (!cl) return NULL;
    jv *hdr = jget(save, "CampaignSaveHeader"), *m = jobj(), *x;
    char id[33]; fe_new_id(id);
    char h[17], ps[17]; hex16(jhash(cl), h); fe_progress_sig(cl, ps);
    jset(m, "export_id", jstrv(id)); jset(m, "player_uid", jstrv(player_uid ? player_uid : ""));
    jset(m, "home_campaign_guid", jcopy(jget(hdr, "CampaignGuid")));
    jset(m, "home_save_name", jcopy(jget(hdr, "SaveName")));
    jset(m, "home_save_version", jcopy(jget(hdr, "Version")));
    jset(m, "unit_guid", jstrv(g)); jset(m, "closure_hash", jstrv(h)); jset(m, "progress_sig", jstrv(ps));
    jv *pr = fe_comp(jpath(cl, "unit.components"), 10), *st = fe_comp(jpath(cl, "unit.components"), 0);
    jset(m, "name_key", (x = jget(pr, "UnitNameTextKey")) ? jcopy(x) : jnull());
    jset(m, "level", (x = jpath(st, "ProgressionStats.Level")) ? jcopy(x) : jnull());
    jset(m, "exp", (x = jpath(st, "ProgressionStats.Exp")) ? jcopy(x) : jnull());
    jv *e = jobj(); jset(e, "closure", cl); jset(e, "manifest", m);
    return e;
}

/* ---------------------------------------------------------------- removal */
static void drop_item_everywhere(jv *save, const char *ig) {
    jdel(ec_of(save), ig);
    jv *icd = jpath(save, "EntityContainerSaveData.ItemContainerSaveData");
    for (size_t i = 0; icd && i < icd->cnt; i++) { jdel(jget(icd->v[i], "ItemSaveData"), ig); jdel(jget(icd->v[i], "ItemAutoFixable"), ig); }
}
void fe_remove_unit(jv *save, const char *gin, int with_gear, jv *gone) {
    char g[64]; snprintf(g, sizeof g, "%s", gin);
    jv *ec = ec_of(save), *ecd = jget(save, "EntityContainerSaveData");
    if (gone) jpush(gone, jstrv(g));
    if (with_gear) {
        jv *todo = jarr(); item_guids_of(jget(ec, g), todo);
        while (todo->cnt) {
            char ig[64]; snprintf(ig, sizeof ig, "%s", todo->v[todo->cnt - 1]->s); jarr_del(todo, todo->cnt - 1);
            item_guids_of(jget(ec, ig), todo);
            drop_item_everywhere(save, ig);
            if (gone) jpush(gone, jstrv(ig));
        }
        jfree(todo);
    }
    jdel(ec, g);
    jv *u = jget(ecd, "UnitContainerSaveData");
    for (size_t i = 0; u && i < u->cnt; i++) {
        jv *uc = u->v[i];
        jdel(jget(uc, "UnitSaveDataDictionary"), g);
        arr_remove_str(jget(uc, "PlayerGuidList"), g);
        arr_remove_str(jget(uc, "AllyGuidList"), g);
        jv *sm = jget(uc, "UnitSummonerDictionary");             /* summons of the unit go with it */
        for (size_t k = 0; sm && sm->t == JN_OBJ && k < sm->cnt; ) {
            jv *val = sm->v[k];
            int hit = !strcmp(sm->k[k], g) || jstreq(val, g) || (jis(val, JN_ARR) && arr_has(val, g));
            if (!hit) { k++; continue; }
            jv *summ = jarr();
            if (strcmp(sm->k[k], g)) jpush(summ, jstrv(sm->k[k]));
            if (jis(val, JN_ARR)) for (size_t j = 0; j < val->cnt; j++) { if (!jstreq(val->v[j], g)) jpush(summ, jcopy(val->v[j])); }
            else if (val->t == JN_STR && !jstreq(val, g)) jpush(summ, jcopy(val));
            char key[64]; snprintf(key, sizeof key, "%s", sm->k[k]); jdel(sm, key);
            for (size_t j = 0; j < summ->cnt; j++) {
                const char *x = summ->v[j]->s;
                if (jstreq(jget(jget(jget(uc, "UnitSaveDataDictionary"), x), "Key"), "UNIT_PlayerUnit")) continue;
                jdel(ec, x); jdel(jget(uc, "UnitSaveDataDictionary"), x);
                jv *pm = jget(ecd, "EntityPositions");
                for (size_t q = 0; pm && q < pm->cnt; q++) jdel(pm->v[q], x);
            }
            jfree(summ);
        }
    }
    jv *pm = jget(ecd, "EntityPositions");
    for (size_t i = 0; pm && i < pm->cnt; i++) jdel(pm->v[i], g);
    jv *st = jpath(save, "AnimationStateSaveData.States");
    for (size_t i = 0; st && st->t == JN_OBJ && i < st->cnt; i++) if (jis(st->v[i], JN_OBJ)) jdel(st->v[i], g);
    jv *pu = jget(save, "PlayerUnitsSaveData");
    for (size_t i = 0; pu && i < pu->cnt; i++) if (jis(pu->v[i], JN_OBJ)) jdel(pu->v[i], g);
    jdel(jpath(save, "CampaignRandomSaveData.LevelUpPrayerAttemptIndexByUnitGuid"), g);
    jdel(jpath(save, "ReportSaveData.LevelUpTimeData.UnitExpMaxStamp"), g);
    jv *bo = jpath(save, "BedOwnerSaveData.BedOwnerMap");
    for (size_t i = 0; bo && i < bo->cnt; ) { if (jstreq(bo->v[i], g)) { char k[64]; snprintf(k, sizeof k, "%s", bo->k[i]); jdel(bo, k); } else i++; }
    jv *rec = jpath(save, "RecreationSaveData.Recreations");
    for (size_t i = 0; jis(rec, JN_ARR) && i < rec->cnt; ) { if (jstreq(jget(rec->v[i], "PerformerGuid"), g)) jarr_del(rec, i); else i++; }
    jv *evs = jpath(save, "EventSaveData.EventSourceData.EventInstanceSaveData");
    for (size_t i = 0; evs && evs->t == JN_OBJ && i < evs->cnt; i++) arr_remove_str(jget(evs->v[i], "TargetUnits"), g);
}

/* ---------------------------------------------------------------- inject */
static int is_diff_key(const jv *k) { return k && k->t == JN_STR && (!strncmp(k->s, "AFFECTER_PlayerDifficulty", 25) || !strncmp(k->s, "AFFECTER_PlayerManagementDifficulty", 35)); }
static int scmp(const void *a, const void *b) { return strcmp((*(jv *const *)a)->s, (*(jv *const *)b)->s); }
jv *fe_difficulty_keys(const jv *save) {
    jv *cl = clan_list(save), *ec = ec_of(save), *out = NULL;
    for (size_t i = 0; i < cl->cnt && !out; i++) {
        jv *hs = jget(fe_comp(jget(ec, cl->v[i]->v[0]->s), 3), "AffecterHolders"), *ks = jarr();
        for (size_t j = 0; hs && j < hs->cnt; j++) { jv *k = jget(hs->v[j], "Key"); if (is_diff_key(k)) jpush(ks, jcopy(k)); }
        if (ks->cnt) { qsort(ks->v, ks->cnt, sizeof *ks->v, scmp); out = ks; } else jfree(ks);
    }
    jfree(cl);
    return out;
}
int fe_retarget_difficulty(jv *comps, const jv *want) {
    jv *a = fe_comp(comps, 3), *hs = jget(a, "AffecterHolders");
    if (!hs || !want) return 0;
    jv *old = jarr(); long at = -1;
    for (size_t j = 0; j < hs->cnt; j++) if (is_diff_key(jget(hs->v[j], "Key"))) { if (at < 0) at = (long)j; jpush(old, jcopy(jget(hs->v[j], "Key"))); }
    qsort(old->v, old->cnt, sizeof *old->v, scmp);
    int same = jequal(old, want);
    jfree(old);
    if (same) return 0;
    jv *tmpl = NULL;
    for (size_t j = 0; j < hs->cnt && !tmpl; j++) if (is_diff_key(jget(hs->v[j], "Key"))) tmpl = jcopy(hs->v[j]);
    if (!tmpl) { tmpl = jobj(); jset(tmpl, "Key", jstrv("")); jset(tmpl, "Stack", jnum_i(1)); jset(tmpl, "Duration", jnum_d(0)); jset(tmpl, "InitialDuration", jnum_d(0)); }
    jv *nh = jarr();                                   /* rest[:at] + wanted keys + rest[at:] (as fe_save.py) */
    for (size_t j = 0; j < hs->cnt; j++) if (!is_diff_key(jget(hs->v[j], "Key"))) jpush(nh, jcopy(hs->v[j]));
    size_t rest = nh->cnt, ins = at < 0 ? rest : (size_t)at; if (ins > rest) ins = rest;
    jv *fin = jarr();
    for (size_t j = 0; j < ins; j++) jpush(fin, jcopy(nh->v[j]));
    for (size_t w = 0; w < want->cnt; w++) { jv *h = jcopy(tmpl); jset(h, "Key", jcopy(want->v[w])); jpush(fin, h); }
    for (size_t j = ins; j < rest; j++) jpush(fin, jcopy(nh->v[j]));
    jfree(nh); nh = fin;
    jfree(tmpl);
    jset(a, "AffecterHolders", nh);
    return 1;
}
static void next_free_pos(jv *posmap, const jv *prefer, double *ox, double *oy) {
    for (size_t i = 0; prefer && i < prefer->cnt; i++) {
        double x = jnumv(jget(prefer->v[i], "x")), y = jnumv(jget(prefer->v[i], "y")); int taken = 0;
        for (size_t j = 0; j < posmap->cnt && !taken; j++) taken = jnumv(jget(posmap->v[j], "x")) == x && jnumv(jget(posmap->v[j], "y")) == y;
        if (!taken) { *ox = x; *oy = y; return; }
    }
    double x = 100.5, y = 100.5;
    /* every preferred cell taken (an item lying there counts: the hub / PvP template has ground items): the nearest free
       cell around the first preferred one, never the map's first entity (pair test v1_g: a unit landed at (124,147),
       outside the settlement, and walked off) */
    if (prefer && prefer->cnt) {
        double px = jnumv(jget(prefer->v[0], "x")), py = jnumv(jget(prefer->v[0], "y"));
        for (int r = 1; r <= 6; r++)
            for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
                if (dx != -r && dx != r && dy != -r && dy != r) continue;
                double cx = px + dx, cy = py + dy; int taken = 0;
                for (size_t j = 0; j < posmap->cnt && !taken; j++) taken = jnumv(jget(posmap->v[j], "x")) == cx && jnumv(jget(posmap->v[j], "y")) == cy;
                if (!taken) { *ox = cx; *oy = cy; return; }
            }
    }
    if (posmap->cnt) { x = jnumv(jget(posmap->v[0], "x")); y = jnumv(jget(posmap->v[0], "y")); }
    for (int again = 1; again; ) {
        again = 0;
        for (size_t j = 0; j < posmap->cnt; j++) if (jnumv(jget(posmap->v[j], "x")) == x && jnumv(jget(posmap->v[j], "y")) == y) { x += 1.0; again = 1; break; }
    }
    *ox = x; *oy = y;
}
static jv *make_map(const jv *closure) {
    jv *m = jobj(); char g[37];
    fe_new_guid(g); jset(m, jpath(closure, "unit.guid")->s, jstrv(g));
    jv *it = jget(closure, "items");
    for (size_t i = 0; it && i < it->cnt; i++) { fe_new_guid(g); jset(m, it->k[i], jstrv(g)); }
    return m;
}
jv *fe_hub_maps(const jv *entries) {
    jv *a = jarr();
    for (size_t i = 0; entries && i < entries->cnt; i++) jpush(a, make_map(jget(entries->v[i], "closure")));
    return a;
}
/* returns new unit guid (static buffer); item map (old -> new) appended into item_map */
static const char *inject(jv *save, const jv *closure, const char *side, const jv *prefer, const jv *diff, const jv *map, jv *item_map) {
    static char out[64];
    jv *ec = ec_of(save), *ecd = jget(save, "EntityContainerSaveData");
    jv *cl = remap(closure, map);
    jv *u = jget(cl, "unit");
    snprintf(out, sizeof out, "%s", jget(u, "guid")->s);
    const char *g = out;
    jv *comps = jtake(u, "components");
    jv *ei = fe_comp(comps, 1);
    if (ei) { jset(ei, "Faction", jnum_i(1)); jset(ei, "IsDead", jbool(0)); }
    if (diff) fe_retarget_difficulty(comps, diff);
    jset(ec, g, comps);
    jv *uc = jget(jget(ecd, "UnitContainerSaveData"), side);
    jv *reg = jcopy(jget(u, "registry")); if (!jis(reg, JN_OBJ)) { jfree(reg); reg = jobj(); }
    jset(reg, "Guid", jstrv(g));
    jset(obj_at(uc, "UnitSaveDataDictionary"), g, reg);
    jv *pl = jget(uc, "PlayerGuidList"); if (!pl) { jset(uc, "PlayerGuidList", jarr()); pl = jget(uc, "PlayerGuidList"); }
    jpush(pl, jstrv(g));
    jv *pm = obj_at(obj_at(ecd, "EntityPositions"), side);
    double x, y; next_free_pos(pm, prefer, &x, &y);
    jv *p = jobj(); jset(p, "x", jnum_d(x)); jset(p, "y", jnum_d(y)); jset(pm, g, p);
    jv *st = jpath(save, "AnimationStateSaveData.States"), *an = jget(u, "anim");
    for (size_t i = 0; st && an && i < an->cnt; i++) { jv *sd = jget(st, an->k[i]); if (jis(sd, JN_OBJ)) jset(sd, g, jcopy(an->v[i])); }
    jv *pu = jget(save, "PlayerUnitsSaveData"), *pusd = jget(u, "pusd");
    for (size_t i = 0; pu && i < pu->cnt; i++) {
        if (!jis(pu->v[i], JN_OBJ)) continue;
        jv *r = jget(pusd, pu->k[i]);
        if (r) jset(pu->v[i], g, jcopy(r));
        else { jv *any = pu->v[i]->cnt ? pu->v[i]->v[0] : NULL; jset(pu->v[i], g, jis(any, JN_ARR) ? jarr() : jis(any, JN_NUM) ? jnum_i(0) : jobj()); }
    }
    jv *pr = jget(u, "prayer"); if (pr && pr->t != JN_NULL) jset(obj_at(jget(save, "CampaignRandomSaveData"), "LevelUpPrayerAttemptIndexByUnitGuid"), g, jcopy(pr));
    jv *es = jget(u, "exp_stamp"); if (es && es->t != JN_NULL) jset(obj_at(obj_at(jget(save, "ReportSaveData"), "LevelUpTimeData"), "UnitExpMaxStamp"), g, jcopy(es));
    jv *ic = obj_at(obj_at(ecd, "ItemContainerSaveData"), side), *items = jget(cl, "items");
    for (size_t i = 0; items && i < items->cnt; i++) {
        const char *ig = items->k[i]; jv *it = items->v[i];
        jset(ec, ig, jcopy(jget(it, "components")));
        jv *r = jcopy(jget(it, "registry")); if (!jis(r, JN_OBJ)) { jfree(r); r = jobj(); }
        jset(r, "Guid", jstrv(ig));
        jset(obj_at(ic, "ItemSaveData"), ig, r);
        jset(obj_at(ic, "ItemAutoFixable"), ig, jbool(jtruthy(jget(it, "autofix"))));
    }
    jv *ci = jget(closure, "items");
    for (size_t i = 0; ci && item_map && i < ci->cnt; i++) jset(item_map, ci->k[i], jcopy(jget(map, ci->k[i])));
    jfree(cl);
    return out;
}

static void now_str(char *b, size_t n, const char *fmt) { time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); strftime(b, n, fmt, &tm); }

/* amount of each item key in every inventory slot of a save (stackable loot is counted by key, see fe_extract_results) */
static jv *key_totals(const jv *save) {
    jv *ec = ec_of(save), *kt = jobj();
    for (size_t i = 0; ec && i < ec->cnt; i++) {
        jv *comps = ec->v[i];
        for (size_t j = 0; comps && comps->t == JN_ARR && j < comps->cnt; j++) {
            jv *sl = jget(jget(comps->v[j], "Data"), "ItemSlots");
            for (size_t k = 0; jis(sl, JN_ARR) && k < sl->cnt; k++) {
                jv *g = jget(sl->v[k], "Guid"), *ky = jget(sl->v[k], "Key"); if (zero(g) || !ky || ky->t != JN_STR) continue;
                jv *t = jget(kt, ky->s); jset(kt, ky->s, jnum_i((t ? (long long)jnumv(t) : 0) + (long long)jnumv(jget(sl->v[k], "Amount"))));
            }
        }
    }
    return kt;
}
jv *fe_build_hub(const jv *host, const jv *entries, const char *run_id, const jv *maps, jv **run_out, char *err, size_t errn) {
    jv *hdr = jget(host, "CampaignSaveHeader");
    if (jnumv(jget(hdr, "CurrentMap")) != 0) { snprintf(err, errn, "the hub save must be at the settlement (CurrentMap 0)"); return NULL; }
    jv *hv = jget(hdr, "Version");
    for (size_t i = 0; i < entries->cnt; i++) {
        jv *m = jget(entries->v[i], "manifest");
        if (!jequal(jget(m, "home_save_version"), hv)) { snprintf(err, errn, "a unit comes from %s, the hub is %s", jget(m, "home_save_version") ? jget(m, "home_save_version")->s : "?", hv ? hv->s : "?"); return NULL; }
    }
    jv *hub = jcopy(host);
    char rid[33]; if (run_id && *run_id) snprintf(rid, sizeof rid, "%s", run_id); else fe_new_id(rid);
    jv *diff = fe_difficulty_keys(hub);
    jv *old_pos = jarr(), *cl = clan_list(hub), *removed = jarr();
    for (size_t i = 0; i < cl->cnt; i++) {
        if (strcmp(cl->v[i]->v[1]->s, "Base")) continue;
        jv *p = jget(jget(jpath(hub, "EntityContainerSaveData.EntityPositions"), "Base"), cl->v[i]->v[0]->s);
        if (p) jpush(old_pos, jcopy(p));
    }
    for (size_t i = 0; i < cl->cnt; i++) fe_remove_unit(hub, cl->v[i]->v[0]->s, 1, removed);
    jfree(cl);
    char cg[37], stem[64], disp[96], ts[32];
    fe_new_guid(cg);
    snprintf(stem, sizeof stem, "MPFE_%.8s", rid);
    now_str(ts, sizeof ts, "%Y-%m-%d"); snprintf(disp, sizeof disp, "Foreign Expedition %s", ts);
    jv *h = jget(hub, "CampaignSaveHeader");
    jset(h, "CampaignGuid", jstrv(cg)); jset(h, "SaveName", jstrv(stem)); jset(h, "IronModeEnabled", jbool(0));
    jset(h, "IsAutoSave", jbool(0)); jset(h, "DisplayName", jstrv(disp));
    now_str(ts, sizeof ts, "%Y-%m-%d %H:%M:%S"); jset(h, "SaveTime", jstrv(ts));
    jset(jget(hub, "ClanSaveData"), "CampaignGuid", jstrv(cg));
    jv *css = jpath(hub, "CampaignSettingSaveData.CampaignStartSettingSaveData");
    if (css) jset(css, "IronModeEnabled", jbool(0));
    jv *rows = jarr();
    for (size_t i = 0; i < entries->cnt; i++) {
        jv *e = entries->v[i], *m = jget(e, "manifest");
        jv *map = maps && i < maps->cnt ? jcopy(maps->v[i]) : make_map(jget(e, "closure"));
        jv *im = jobj();
        const char *g = inject(hub, jget(e, "closure"), "Base", old_pos, diff, map, im);
        jv *r = jobj();
        jset(r, "hub_guid", jstrv(g)); jset(r, "export_id", jcopy(jget(m, "export_id")));
        jset(r, "player_uid", jcopy(jget(m, "player_uid"))); jset(r, "home_guid", jcopy(jget(m, "unit_guid")));
        jset(r, "item_map", im);
        jpush(rows, r);
        jfree(map);
    }
    jset(jget(hub, "ClanSaveData"), "CurrentPopulation", jnum_i(fe_clan_count(hub)));
    jv *run = jobj();
    jset(run, "run_id", jstrv(rid)); jset(run, "hub_save_name", jstrv(stem)); jset(run, "hub_campaign_guid", jstrv(cg));
    jset(run, "hub_version", hv ? jcopy(hv) : jnull()); jset(run, "hub_difficulty", diff ? diff : jnull());
    jset(run, "host_campaign_guid", jcopy(jget(hdr, "CampaignGuid"))); jset(run, "removed_host_guids", removed);
    jset(run, "units", rows);
    jset(run, "key_totals_start", key_totals(hub));
    jfree(old_pos);
    if (run_out) *run_out = run; else jfree(run);
    return hub;
}

/* ---------------------------------------------------------------- results */
static void result_hash(const jv *res, char out[17]) {
    jv *c = jcopy(res); jdel(c, "result_hash"); hex16(jhash(c), out); jfree(c);
}
int fe_result_hash_ok(const jv *result) {
    char h[17]; result_hash(result, h);
    return jstreq(jget(result, "result_hash"), h);
}
static void level_exp(const jv *closure, jv *row) {
    jv *st = fe_comp(jpath(closure, "unit.components"), 0), *x;
    jset(row, "level_after", (x = jpath(st, "ProgressionStats.Level")) ? jcopy(x) : jnull());
    jset(row, "exp_after", (x = jpath(st, "ProgressionStats.Exp")) ? jcopy(x) : jnull());
}
jv *fe_extract_results(const jv *hub_end, const jv *run, fe_rules r, const char *uid, const jv *loot,
                       const jv *last_alive, char *err, size_t errn) {
    if (!jequal(jpath(hub_end, "CampaignSaveHeader.CampaignGuid"), jget(run, "hub_campaign_guid"))) {
        snprintf(err, errn, "this save is not the hub of that run"); return NULL;
    }
    jv *units = jarr(), *rows = jget(run, "units"), *sent = jobj();
    for (size_t i = 0; rows && i < rows->cnt; i++) {
        jv *im = jget(rows->v[i], "item_map");
        for (size_t j = 0; im && j < im->cnt; j++) jset(sent, im->v[j]->s, jnull());     /* the copies that travelled */
        if (!jstreq(jget(rows->v[i], "player_uid"), uid)) continue;
        const char *hg = jget(rows->v[i], "hub_guid")->s;
        int alive = fe_side_of(hub_end, hg) != NULL;
        jv *row = jobj();
        jset(row, "export_id", jcopy(jget(rows->v[i], "export_id"))); jset(row, "home_guid", jcopy(jget(rows->v[i], "home_guid")));
        jset(row, "hub_guid", jstrv(hg)); jset(row, "status", jstrv(alive ? "alive" : "dead"));
        jv *cl = NULL;
        if (alive) cl = fe_export_unit(hub_end, hg, err, errn);
        else if (!r.death_permanent && jget(last_alive, hg)) { cl = jcopy(jget(last_alive, hg)); jset(row, "from_snapshot", jbool(1)); }
        if (alive && !cl) { jfree(row); jfree(units); jfree(sent); return NULL; }
        if (cl) { level_exp(cl, row); jset(row, "closure_end", cl); }
        jpush(units, row);
    }
    /* amounts as the slots hold them */
    jv *ec = ec_of(hub_end), *amount = jobj();
    for (size_t i = 0; ec && i < ec->cnt; i++) {
        jv *comps = ec->v[i];
        for (size_t j = 0; comps && comps->t == JN_ARR && j < comps->cnt; j++) {
            jv *d = jget(comps->v[j], "Data"), *sl = jget(d, "ItemSlots");
            for (size_t k = 0; jis(sl, JN_ARR) && k < sl->cnt; k++) { jv *g = jget(sl->v[k], "Guid"); if (!zero(g)) jset(amount, g->s, jcopy(jget(sl->v[k], "Amount"))); }
        }
    }
    jv *items = jobj();
    jv *icd = jpath(hub_end, "EntityContainerSaveData.ItemContainerSaveData");
    for (size_t i = 0; r.items_back && loot && i < loot->cnt; i++) {
        const char *ig = loot->k[i];
        if (jget(sent, ig) || !jget(ec, ig) || !jtruthy(jget(loot->v[i], "found_in_dungeon"))) continue;
        jv *reg = NULL; int af = 0;
        for (size_t s = 0; icd && s < icd->cnt; s++) {
            if (!reg) reg = jget(jget(icd->v[s], "ItemSaveData"), ig);
            af |= jtruthy(jget(jget(icd->v[s], "ItemAutoFixable"), ig));
        }
        jv *it = jobj(), *a = jget(amount, ig);
        jset(it, "components", jcopy(jget(ec, ig))); jset(it, "registry", reg ? jcopy(reg) : jnull());
        jset(it, "autofix", jbool(af)); jset(it, "amount", a ? jcopy(a) : jnum_i(1));
        jset(items, ig, it);
    }
    /* stackable loot merged into a stack (its own guid is gone): back by key and amount, capped by what the hub still
       holds of that key (used up in the hub = gone); the item data comes from any item of that key in the hub */
    /* what the hub gained of each key since the start (end - start): loot lost with dead units or used up in the hub
       does not come back */
    jv *keytot = key_totals(hub_end), *k0 = jget(run, "key_totals_start");
    for (size_t i = 0; k0 && i < k0->cnt; i++) { jv *t = jget(keytot, k0->k[i]); jset(keytot, k0->k[i], jnum_i((t ? (long long)jnumv(t) : 0) - (long long)jnumv(k0->v[i]))); }
    for (size_t i = 0; r.items_back && loot && i < loot->cnt; i++) {
        const char *ig = loot->k[i]; jv *ky = jget(loot->v[i], "key");
        if (jget(items, ig) || jget(sent, ig) || jget(ec, ig) || !ky || ky->t != JN_STR || !jtruthy(jget(loot->v[i], "found_in_dungeon"))) continue;
        jv *t = jget(keytot, ky->s); long long have = t ? (long long)jnumv(t) : 0, want = (long long)jnumv(jget(loot->v[i], "amount"));
        if (want < 1) want = 1;
        if (want > have) want = have;
        if (want < 1) continue;
        const char *tg = NULL; jv *treg = NULL;
        for (size_t s2 = 0; !tg && icd && s2 < icd->cnt; s2++) {
            jv *isd = jget(icd->v[s2], "ItemSaveData");
            for (size_t k = 0; isd && k < isd->cnt; k++) if (jstreq(jget(isd->v[k], "Key"), ky->s) && jget(ec, isd->k[k])) { tg = isd->k[k]; treg = isd->v[k]; break; }
        }
        if (!tg) continue;
        jv *it = jobj();
        jset(it, "components", jcopy(jget(ec, tg))); jset(it, "registry", jcopy(treg));
        jset(it, "autofix", jbool(0)); jset(it, "amount", jnum_i(want)); jset(it, "stacked", jbool(1));
        jset(items, ig, it);
        jset(keytot, ky->s, jnum_i(have - want));
    }
    jfree(keytot);
    jfree(amount); jfree(sent);
    jv *res = jobj(), *ru = jobj();
    jset(ru, "death_permanent", jbool(r.death_permanent)); jset(ru, "xp_counts", jbool(r.xp_counts)); jset(ru, "items_back", jbool(r.items_back));
    jset(res, "fe_result", jnum_i(1)); jset(res, "run_id", jcopy(jget(run, "run_id"))); jset(res, "player_uid", jstrv(uid));
    jset(res, "rules", ru); jset(res, "hub_version", jcopy(jget(run, "hub_version"))); jset(res, "hub_difficulty", jcopy(jget(run, "hub_difficulty")));
    jset(res, "units", units); jset(res, "items", items);
    char h[17]; result_hash(res, h); jset(res, "result_hash", jstrv(h));
    return res;
}

/* ---------------------------------------------------------------- apply */
static jv *place_slot(jv *save, const char *key, const jv *units) {
    jv *ec = ec_of(save), *pol = jpath(save, "StoragePolicySaveData.StoragePolicies");
    jv *bsd = jget(jget(jpath(save, "EntityContainerSaveData.BuildingContainerSaveData"), "Base"), "BuildingSaveData");
    for (int pass = 0; pass < 2; pass++) {
        size_t n = pass == 0 ? (bsd ? bsd->cnt : 0) : (units ? units->cnt : 0);
        for (size_t i = 0; i < n; i++) {
            const char *h = pass == 0 ? bsd->k[i] : units->v[i]->s;
            if (pass == 0 && !jtruthy(jget(jget(jget(pol, h), "AvailableItems"), key))) continue;
            jv *sl = jget(fe_comp(jget(ec, h), 5), "ItemSlots");
            for (size_t k = 0; jis(sl, JN_ARR) && k < sl->cnt; k++) if (jstreq(jget(sl->v[k], "Guid"), FE_ZERO)) return sl->v[k];
        }
    }
    return NULL;
}
int fe_manifest_custom(const jv *m) {
    jv *src = jget(m, "source");
    if (jstreq(src, "wiki") || jstreq(src, "library") || jget(m, "library_id")) return 1;
    jv *cg = jget(m, "home_campaign_guid");
    if (!cg || cg->t != JN_STR || !cg->n) return 1;
    for (size_t i = 0; i < cg->n; i++) if (cg->s[i] != '0' && cg->s[i] != '-') return 0;
    return 1;                                            /* all zeros (with or without dashes) */
}
static void rep(jv *report, const char *g, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void rep(jv *report, const char *g, const char *fmt, ...) {
    if (!report) return;
    char m[300], line[400]; va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
    snprintf(line, sizeof line, "%.8s: %s", g, m);
    /* raw JSON string: our text has no quotes or backslashes */
    jpush(report, jstrv(line));
}
char *fe_apply_results(const jv *home, const jv *result, const jv *manifests, const char *ledger_path, const char *out_dir,
                       const jv *choices, jv *report, char *err, size_t errn) {
    const char *rid = jget(result, "run_id") ? jget(result, "run_id")->s : "";
    if (!fe_result_hash_ok(result)) { snprintf(err, errn, "these results are damaged (hash mismatch)"); return NULL; }
    char e2[200];
    jv *led = jload(ledger_path, e2, sizeof e2); if (!led) { led = jobj(); jset(led, "claims", jobj()); }
    jv *claim = jget(jget(led, "claims"), rid);
    if (claim) { snprintf(err, errn, "Already brought home on %s.", jget(claim, "date") ? jget(claim, "date")->s : "?"); jfree(led); return NULL; }
    jv *units = jget(result, "units");
    for (size_t i = 0; units && i < units->cnt; i++)
        if (!jget(manifests, jget(units->v[i], "export_id")->s)) { snprintf(err, errn, "These results are not for your units."); jfree(led); return NULL; }
    size_t ncustom = 0;                                  /* owner rule: custom units never enter a regular save */
    for (size_t i = 0; units && i < units->cnt; i++) ncustom += fe_manifest_custom(jget(manifests, jget(units->v[i], "export_id")->s)) != 0;
    if (units && units->cnt && ncustom == units->cnt) { snprintf(err, errn, FE_CUSTOM_REFUSE); jfree(led); return NULL; }
    jv *hdr = jget(home, "CampaignSaveHeader");
    for (size_t i = 0; units && i < units->cnt; i++) {
        jv *m = jget(manifests, jget(units->v[i], "export_id")->s);
        if (fe_manifest_custom(m)) continue;
        if (!jequal(jget(m, "home_campaign_guid"), jget(hdr, "CampaignGuid"))) { snprintf(err, errn, "this save is not the campaign the units came from"); jfree(led); return NULL; }
    }
    if (!jequal(jget(hdr, "Version"), jget(result, "hub_version"))) {
        snprintf(err, errn, "Made with %s; the expedition ran on %s.", jget(hdr, "Version") ? jget(hdr, "Version")->s : "?", jget(result, "hub_version") ? jget(result, "hub_version")->s : "?");
        jfree(led); return NULL;
    }
    char mk[80]; snprintf(mk, sizeof mk, FE_MARK "%s", rid);
    if (jget(jpath(home, "ReportSaveData.InGameRecordData.Counts"), mk)) { snprintf(err, errn, "Already brought home (marker in this save)."); jfree(led); return NULL; }
    jv *out = jcopy(home), *ec = ec_of(out), *rules = jget(result, "rules");
    jv *home_diff = fe_difficulty_keys(out);
    char need[600] = "";
    for (size_t i = 0; units && i < units->cnt; i++) {
        jv *u = units->v[i], *m = jget(manifests, jget(u, "export_id")->s);
        char H[64]; snprintf(H, sizeof H, "%s", jget(u, "home_guid")->s);
        const char *eid = jget(u, "export_id")->s;
        if (fe_manifest_custom(m)) { rep(report, H, FE_CUSTOM_LINE); continue; }
        if (!fe_side_of(out, H)) { rep(report, H, "not in this save anymore"); continue; }
        char e3[200]; jv *cur_cl = fe_export_unit(out, H, e3, sizeof e3); char ps[17] = ""; if (cur_cl) fe_progress_sig(cur_cl, ps); jfree(cur_cl);
        int changed = !jstreq(jget(m, "progress_sig"), ps);
        jv *ch = jget(choices, eid);
        if (changed && !ch) { size_t l = strlen(need); snprintf(need + l, sizeof need - l, "%s%s", l ? ", " : "", eid); continue; }
        if (changed && jstreq(ch, "home")) { rep(report, H, "changed at home: home version kept"); continue; }
        int dead = jstreq(jget(u, "status"), "dead");
        if (dead && jtruthy(jget(rules, "death_permanent"))) { fe_remove_unit(out, H, 1, NULL); rep(report, H, "died: removed"); continue; }
        if (dead && !jget(u, "closure_end")) { rep(report, H, "died: no multiplayer save with it alive, comes home as before"); continue; }
        const char *tag = dead ? "died: back to its last multiplayer save" : "alive";
        if (!jtruthy(jget(rules, "xp_counts"))) { rep(report, H, "%s: experience does not count", tag); continue; }
        jv *map = jobj(); jset(map, jget(u, "hub_guid")->s, jstrv(H));
        jv *xc = remap(jget(u, "closure_end"), map); jfree(map);
        jv *x = jget(xc, "unit"), *cur = jget(ec, H);
        jv *old_active = jcopy(jget(fe_comp(cur, 8), "ActiveSkills"));
        int ts[3] = { 0, 3, 8 };
        for (int t = 0; t < 3; t++) {
            jv *a = fe_comp(jget(x, "components"), ts[t]), *b = fe_comp(cur, ts[t]);
            if (!a || !b) continue;
            if (ts[t] == 0) {
                const char *ks[5] = { "SchemaVersion", "ProgressionStats", "GeneratedStats", "PermanentStats", "Stats" };
                for (int k = 0; k < 5; k++) if (jget(a, ks[k])) jset(b, ks[k], jcopy(jget(a, ks[k])));
            } else {
                for (size_t c = 0; c < cur->cnt; c++)
                    if ((int)jnumv(jget(cur->v[c], "Type")) == ts[t]) { jset(cur->v[c], "Data", jcopy(a)); break; }
            }
        }
        fe_retarget_difficulty(cur, home_diff);
        jv *pu = jget(out, "PlayerUnitsSaveData"), *xp = jget(x, "pusd");
        const char *take[2] = { "SkillTrees", "SkillLearnNotices" };
        for (int k = 0; k < 2; k++) if (jget(xp, take[k]) && jis(jget(pu, take[k]), JN_OBJ)) jset(jget(pu, take[k]), H, jcopy(jget(xp, take[k])));
        jv *qs = jpath(jget(jget(pu, "QuickSlots"), H), "QuickSlots");    /* home bar kept; new skills fill empty slots */
        jv *act = jget(fe_comp(cur, 8), "ActiveSkills");
        for (size_t k = 0; jis(qs, JN_ARR) && act && k < act->cnt; k++) {
            jv *sk = act->v[k];
            if (sk->t != JN_STR || arr_has(old_active, sk->s) || arr_has(qs, sk->s)) continue;
            for (size_t q = 0; q < qs->cnt; q++) if (qs->v[q]->t == JN_NULL) { jfree(qs->v[q]); qs->v[q] = jcopy(sk); break; }
        }
        jfree(old_active);
        jv *pr = jget(x, "prayer"); if (pr && pr->t != JN_NULL) jset(obj_at(jget(out, "CampaignRandomSaveData"), "LevelUpPrayerAttemptIndexByUnitGuid"), H, jcopy(pr));
        jv *es = jget(x, "exp_stamp"); if (es && es->t != JN_NULL) jset(obj_at(obj_at(jget(out, "ReportSaveData"), "LevelUpTimeData"), "UnitExpMaxStamp"), H, jcopy(es));
        jv *lv0 = jget(m, "level"), *lv1 = jpath(fe_comp(cur, 0), "ProgressionStats.Level");
        rep(report, H, "%s: level %s -> %s", tag, lv0 && lv0->s ? lv0->s : "?", lv1 && lv1->s ? lv1->s : "?");
        jfree(xc);
    }
    jfree(home_diff);
    if (need[0]) { snprintf(err, errn, "changed at home since they left, choose per unit: %s", need); jfree(out); jfree(led); return NULL; }
    /* items (dungeon loot only, filtered at extraction) */
    jv *alive = jarr(), *cl = clan_list(out), *left = jarr();
    for (size_t i = 0; i < cl->cnt; i++) if (!strcmp(cl->v[i]->v[1]->s, "Base")) jpush(alive, jcopy(cl->v[i]->v[0]));
    jfree(cl);
    jv *items = jget(result, "items");
    for (size_t i = 0; items && i < items->cnt; i++) {
        jv *it = items->v[i], *reg = jget(it, "registry");
        const char *key = jget(reg, "Key") ? jget(reg, "Key")->s : "";
        jv *slot = place_slot(out, key, alive);
        if (!slot) { jpush(left, jstrv(items->k[i])); continue; }
        char ng[37]; fe_new_guid(ng);
        jset(ec, ng, jcopy(jget(it, "components")));
        jv *ic = obj_at(obj_at(jget(out, "EntityContainerSaveData"), "ItemContainerSaveData"), "Base");
        jv *r = jcopy(reg); if (!jis(r, JN_OBJ)) { jfree(r); r = jobj(); }
        jset(r, "Guid", jstrv(ng)); jset(obj_at(ic, "ItemSaveData"), ng, r);
        jset(obj_at(ic, "ItemAutoFixable"), ng, jbool(jtruthy(jget(it, "autofix"))));
        jset(slot, "Guid", jstrv(ng)); jset(slot, "Key", jstrv(key));
        jset(slot, "Amount", jget(it, "amount") ? jcopy(jget(it, "amount")) : jnum_i(1)); jset(slot, "AutoFixed", jbool(jtruthy(jget(it, "autofix"))));
    }
    jfree(alive);
    if (left->cnt) rep(report, "items", "%zu item(s) did not fit; kept in the result", left->cnt);
    jset(obj_at(obj_at(obj_at(out, "ReportSaveData"), "InGameRecordData"), "Counts"), mk, jnum_i(1));
    jv *oh = jget(out, "CampaignSaveHeader");
    char base[64] = "", stem[200], disp[200], ts[32], cgn[40] = "";
    const char *cn = jget(oh, "ClanName") ? jget(oh, "ClanName")->s : "Clan";
    for (size_t i = 0, k = 0; cn[i] && k < sizeof base - 1; i++) if (isalnum((unsigned char)cn[i]) || cn[i] == '+' || cn[i] == '_' || cn[i] == '-') base[k++] = cn[i];
    if (!base[0]) snprintf(base, sizeof base, "Clan");
    const char *cg = jget(oh, "CampaignGuid") ? jget(oh, "CampaignGuid")->s : "";
    for (size_t i = 0, k = 0; cg[i] && k < sizeof cgn - 1; i++) if (cg[i] != '-') cgn[k++] = cg[i];
    snprintf(stem, sizeof stem, "%s_BackFromExpedition_%.8s__AutoSave_%s_0", base, rid, cgn);
    snprintf(disp, sizeof disp, "%s - back from expedition", cn);
    now_str(ts, sizeof ts, "%Y-%m-%d %H:%M:%S");
    jset(oh, "SaveName", jstrv(stem)); jset(oh, "DisplayName", jstrv(disp));
    jset(oh, "IsAutoSave", jbool(jtruthy(jget(oh, "IronModeEnabled")))); jset(oh, "SaveTime", jstrv(ts));
    jset(jget(out, "ClanSaveData"), "CurrentPopulation", jnum_i(fe_clan_count(out)));
    char probs[600];
    if (fe_check_refs(out, NULL, probs, sizeof probs)) { snprintf(err, errn, "the merged save failed the reference check: %s", probs); jfree(out); jfree(led); jfree(left); return NULL; }
    char path[1500]; snprintf(path, sizeof path, "%s/%s.json", out_dir, stem);
    mkdir(out_dir, 0755);
    if (jsave_new(out, path)) { snprintf(err, errn, "could not write %s (exists already?)", path); jfree(out); jfree(led); jfree(left); return NULL; }
    jfree(out);
    jv *c = jobj(); now_str(ts, sizeof ts, "%Y-%m-%d %H:%M");
    jset(c, "date", jstrv(ts)); jset(c, "file", jstrv(path)); jset(c, "left_items", left);
    jset(obj_at(led, "claims"), rid, c);
    size_t n; char *s = jdump(led, 1, &n);
    char tmp[1600]; snprintf(tmp, sizeof tmp, "%s.tmp", ledger_path);
    FILE *f = fopen(tmp, "wb"); if (f) { fwrite(s, 1, n, f); fclose(f); rename(tmp, ledger_path); }
    free(s); jfree(led);
    return strdup(path);
}

/* ---------------------------------------------------------------- integrity */
typedef struct { char *o; size_t n, used; int count; } Pr;
static void prob(Pr *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void prob(Pr *p, const char *fmt, ...) {
    p->count++;
    if (!p->o || p->used + 4 >= p->n) return;
    va_list ap; va_start(ap, fmt);
    int k = vsnprintf(p->o + p->used, p->n - p->used, fmt, ap); va_end(ap);
    if (k > 0) { p->used += (size_t)k; if (p->used >= p->n) p->used = p->n - 1; }
    if (p->used + 2 < p->n) { p->o[p->used++] = ';'; p->o[p->used++] = ' '; p->o[p->used] = 0; }
}
static void scan_gone(const jv *v, const jv *gs, Pr *p, int *hits) {
    if (!v || *hits > 20) return;
    if (v->t == JN_STR) { if (jget(gs, v->s)) { (*hits)++; prob(p, "dangling reference to %.8s", v->s); } return; }
    for (size_t i = 0; i < v->cnt; i++) {
        if (v->t == JN_OBJ && jget(gs, v->k[i])) { (*hits)++; prob(p, "dangling key %.8s", v->k[i]); }
        scan_gone(v->v[i], gs, p, hits);
    }
}
int fe_check_refs(const jv *save, const jv *gone, char *out, size_t outn) {
    Pr p = { out, outn, 0, 0 }; if (out && outn) out[0] = 0;
    jv *ec = ec_of(save), *ecd = jget(save, "EntityContainerSaveData"), *pu = jget(save, "PlayerUnitsSaveData"), *u = jget(ecd, "UnitContainerSaveData");
    jv *seen = jobj();
    for (size_t i = 0; u && i < u->cnt; i++) {
        const char *sd = u->k[i]; jv *uc = u->v[i], *reg = jget(uc, "UnitSaveDataDictionary"), *pl = jget(uc, "PlayerGuidList");
        jv *ic = jget(jget(ecd, "ItemContainerSaveData"), sd);
        for (size_t j = 0; pl && j < pl->cnt; j++) {
            const char *g = pl->v[j]->s;
            if (jget(seen, g)) prob(&p, "%s: unit %.8s listed twice", sd, g);
            jset(seen, g, jnull());
            if (!jget(ec, g)) prob(&p, "%s: unit %.8s has no components", sd, g);
            if (!jget(reg, g)) prob(&p, "%s: unit %.8s not in the unit registry", sd, g);
            if (!jget(jget(jget(ecd, "EntityPositions"), sd), g)) prob(&p, "%s: unit %.8s has no position", sd, g);
            for (size_t k = 0; pu && k < pu->cnt; k++) if (jis(pu->v[k], JN_OBJ) && !jget(pu->v[k], g)) prob(&p, "%s: unit %.8s misses its %s row", sd, g, pu->k[k]);
            jv *its = jarr(); item_guids_of(jget(ec, g), its);
            for (size_t k = 0; k < its->cnt; k++) {
                const char *ig = its->v[k]->s;
                if (!jget(ec, ig)) prob(&p, "%s: item %.8s of unit %.8s has no entity", sd, ig, g);
                if (!jget(jget(ic, "ItemSaveData"), ig)) prob(&p, "%s: item %.8s not registered", sd, ig);
                if (!jget(jget(ic, "ItemAutoFixable"), ig)) prob(&p, "%s: item %.8s has no AutoFixable row", sd, ig);
            }
            jfree(its);
        }
    }
    jfree(seen);
    jv *holders = jobj();
    for (size_t i = 0; ec && i < ec->cnt; i++) {
        jv *its = jarr(); item_guids_of(ec->v[i], its);
        for (size_t k = 0; k < its->cnt; k++) {
            jv *h = jget(holders, its->v[k]->s);
            if (h) prob(&p, "item %.8s held twice (%.8s, %.8s)", its->v[k]->s, h->s, ec->k[i]);
            else jset(holders, its->v[k]->s, jstrv(ec->k[i]));
        }
        jfree(its);
    }
    jfree(holders);
    if (!jequal(jpath(save, "ClanSaveData.CampaignGuid"), jpath(save, "CampaignSaveHeader.CampaignGuid"))) prob(&p, "clan record and header carry different campaign ids");
    if ((int)jnumv(jpath(save, "ClanSaveData.CurrentPopulation")) != fe_clan_count(save)) prob(&p, "population %d != roster %d", (int)jnumv(jpath(save, "ClanSaveData.CurrentPopulation")), fe_clan_count(save));
    if (gone && gone->cnt) {
        jv *gs = jobj(); for (size_t i = 0; i < gone->cnt; i++) if (gone->v[i]->t == JN_STR) jset(gs, gone->v[i]->s, jnull());
        int hits = 0; scan_gone(save, gs, &p, &hits); jfree(gs);
    }
    return p.count;
}
