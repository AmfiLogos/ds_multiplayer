/* fe_hub.c - the world a Foreign Expedition hub or a PvP v1 match starts in (owner 2026-10-15: "a neutral hub ... some
 * kind of a basic map. We have everything unlocked"; v12 #13: "PvP v1, very simple: a map (the starting map for now)").
 * Both are the wiki's base template (the day-0 starting base, dungeonsettlers.wiki /assets/saves/base_<build>.json),
 * downloaded once and kept in the game's data folder next to Test Fights' cache (never shipped, never in the repo):
 *   <data>/Mods/custom-modes/wiki/saves/base_<build>.json
 * The hub is then unlocked: every research done, every region open. The PvP world is the template as it is.
 * The host writes it as a new save (data/Saves/<name>.json) and loads it; the multiplayer menu then opens the lobby as
 * for "Your save". The template's own clan units stay until the start: the expedition / PvP start replaces the clan with
 * the brought units (fe_build_hub). */
#include "rig.h"
#include "fe_json.h"
#include "fe_save.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

enum { FE_WORLD_HUB = 0, FE_WORLD_PVP = 1 };
/* a PvP v1 world's campaign guid starts with this (every peer can tell it plays in one: lobby Bring Units, the match on
   the settlement, the result window's way back to the title). Kept through the brought-units build (pvp_units.c). */
#define PVP_V1_PREFIX "b45e0"
/* a neutral hub world before its expedition starts: campaign guid prefix (every peer); the start (fe_join_point_save)
   gives the hub its run prefix fe5ea00x, so the stock below comes once */
#define FE_HUB_TPL_PREFIX "fe5eb"
static int world_prefix(const char *pre) {
    Obj c = cdc(); Obj clan = c ? AT(c, 0x50, Obj) : NULL;
    if (!clan) return 0;
    char gs[40]; guid_str(AT(clan, 0x10, Guid), gs);
    return !strncmp(gs, pre, 5);
}
int pvp_v1_world(void) {
    Obj c = cdc(); Obj clan = c ? AT(c, 0x50, Obj) : NULL;
    if (!clan) return 0;
    char gs[40]; guid_str(AT(clan, 0x10, Guid), gs);
    return !strncmp(gs, PVP_V1_PREFIX, 5);
}

/* Test Fights' wiki cache (custom/fights/sg_cache.h, lane fights): downloads the template when it is not cached yet.
   Found at run time (dlsym): without it only an already cached template is used. Same layout as sgc in sg_cache.h. */
typedef struct fe_sgc { char root[1024]; int timeout_ms; } fe_sgc;

static void wiki_root(char *o, size_t n) { snprintf(o, n, "%s/data/Mods/custom-modes/wiki", rig_dir); }
static void template_name(char *o, size_t n) { snprintf(o, n, "base_%s.json", RIG_BUILD); }

static int read_all(const char *p, char **text, size_t *len) {
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *d = n > 0 ? malloc((size_t)n + 1) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); fclose(f); return -1; }
    fclose(f); d[n] = 0; *text = d; *len = (size_t)n;
    return 0;
}

/* the template text: Test Fights' cache (download on first use), else the cached file itself */
static int fe_world_template(char **text, size_t *len, char *err, size_t errn) {
    char root[1100], file[64], p[1300];
    wiki_root(root, sizeof root); template_name(file, sizeof file);
    void (*sgc_init)(fe_sgc *, const char *) = (void (*)(fe_sgc *, const char *))dlsym(RTLD_DEFAULT, "sgc_init");
    int (*sgc_template)(void *, const char *, char **, size_t *, char *, size_t) =
        (int (*)(void *, const char *, char **, size_t *, char *, size_t))dlsym(RTLD_DEFAULT, "sgc_template");
    if (sgc_init && sgc_template) {
        fe_sgc c; memset(&c, 0, sizeof c); sgc_init(&c, root);
        if (!sgc_template(&c, file, text, len, err, errn)) return 0;
        rig_log("fe hub: wiki cache: %s", err);
    }
    snprintf(p, sizeof p, "%s/saves/%s", root, file);
    if (!read_all(p, text, len)) return 0;
    snprintf(err, errn, "The hub map is not downloaded yet: the game needs dungeonsettlers.wiki once (%s).", file);
    return -1;
}

/* "DS_B." + Application.version, the Version the game writes into its saves (a different one makes the game ask) */
static void game_version(char *o, size_t n) {
    Obj s = F(0x507B9CC, Obj (*)(void *))(NULL);                         /* Application.get_version */
    char v[64] = ""; int k = 0, len = s ? AT(s, 0x10, int32_t) : 0;
    for (int i = 0; s && i < len && k < 63; i++) { uint16_t c = AT(s, 0x14 + 2 * i, uint16_t); if (c < 128) v[k++] = (char)c; }
    v[k] = 0;
    snprintf(o, n, "%s%s", strncmp(v, "DS_B.", 5) ? "DS_B." : "", v);   /* the build answers "DS_B.0.4.29" */
}

/* everything unlocked: research done (Workload = RequiredWorkload, unlocked, empty queue), every region open */
static void unlock_all(jv *save, int *nres, int *nreg) {
    jv *rd = jpath(save, "ResearchSaveData.ResearchDatas");
    for (size_t i = 0; rd && i < rd->cnt; i++) {
        jv *r = rd->v[i]; if (!jis(r, JN_OBJ)) continue;
        jv *req = jget(r, "RequiredWorkload");
        jset(r, "Workload", req ? jcopy(req) : jnum_d(0)); jset(r, "IsLocked", jbool(0));
        (*nres)++;
    }
    jv *rs = jget(save, "ResearchSaveData"); if (rs) jset(rs, "ResearchKeyQueue", jarr());
    jv *em = jpath(save, "ClanSaveData.ExpeditionMapData");
    for (size_t i = 0; em && i < em->cnt; i++) {
        jv *r = em->v[i];
        if (!jis(r, JN_OBJ) || jnumv(jget(r, "Region")) < 0) continue;      /* Region -1: not a selectable region */
        jset(r, "IsLocked", jbool(0)); (*nreg)++;
    }
}

/* the world as a new save; out_name = its SaveName. kind FE_WORLD_*. NULL on error (err). */
static jv *fe_world_build(int kind, char *out_name, size_t on, char *err, size_t errn) {
    char *text = NULL; size_t len = 0;
    if (fe_world_template(&text, &len, err, errn)) return NULL;
    jv *w = jparse(text, len, err, errn); free(text);
    if (!w) return NULL;
    jv *h = jget(w, "CampaignSaveHeader");
    if (!h || !jget(w, "ClanSaveData") || jnumv(jget(h, "CurrentMap")) != 0) { jfree(w); snprintf(err, errn, "The hub map file is not a base save."); return NULL; }
    int nres = 0, nreg = 0;
    if (kind == FE_WORLD_HUB) unlock_all(w, &nres, &nreg);
    char cg[37], ver[80], stem[64], ts[32];
    fe_new_guid(cg);
    memcpy(cg, kind == FE_WORLD_PVP ? PVP_V1_PREFIX : FE_HUB_TPL_PREFIX, 5);
    game_version(ver, sizeof ver);
    snprintf(stem, sizeof stem, "%s_%.8s", kind == FE_WORLD_HUB ? "MPHub" : "MPPvP", cg);
    const char *disp = kind == FE_WORLD_HUB ? "Foreign Expedition Hub" : "PvP Base";
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    jset(h, "CampaignGuid", jstrv(cg)); jset(h, "SaveName", jstrv(stem)); jset(h, "DisplayName", jstrv(disp));
    jset(h, "ClanName", jstrv(disp)); jset(h, "SaveTime", jstrv(ts)); jset(h, "Version", jstrv(ver));
    jset(h, "IronModeEnabled", jbool(0)); jset(h, "IsAutoSave", jbool(0));
    jv *cs = jget(w, "CampaignSettingSaveData");
    if (cs) jset(cs, "GameSpeed", jnum_i(1));                              /* the template was saved paused: the world runs */
    jv *css = jpath(w, "CampaignSettingSaveData.CampaignStartSettingSaveData");
    if (css) {
        jset(css, "IronModeEnabled", jbool(0)); jset(css, "CampaignName", jstrv(disp));
        /* the template predates "Lock Consecration Results": without these two fields the game asks at load (pause
           reason ConsecrationNotice, a Yes / No box) and a multiplayer world waits there. Off = the stock default. */
        if (!jget(css, "LockConsecrationEnabled")) jset(css, "LockConsecrationEnabled", jbool(0));
        jset(css, "LockConsecrationNoticeAcknowledged", jbool(1));
    }
    jv *cl = jget(w, "ClanSaveData"); jset(cl, "CampaignGuid", jstrv(cg)); jset(cl, "ClanName", jstrv(disp));
    snprintf(out_name, on, "%s", stem);
    rig_log("fe hub: %s world %s built from the base template (%s): %d research done, %d regions open", kind == FE_WORLD_HUB ? "hub" : "PvP", stem, ver, nres, nreg);
    return w;
}

/* LoadCampaignRequested(name) into MainFlow's EventQueue (as the title's Load list does) */
static void queue_load(const char *name) {
    Obj ev = obj_new(0x6651118);
    F(0xDCFD78, void (*)(Obj, Obj, void *))(ev, F(0x6F9738, Obj (*)(const char *))(name), NULL);
    F(0xCCFAD4, void (*)(Obj, Obj, void *))(AT(mf, 0x48, Obj), ev, NULL);
}

static char pvp_file[1300];                                                /* host: the PvP v1 world file it made */
/* build the world and write it into data/Saves; name = its SaveName, path (optional) = the file. 0 ok. */
int fe_world_write(int kind, char *name, size_t nn, char *path, size_t pn, char *err, size_t errn) {
    jv *w = fe_world_build(kind, name, nn, err, errn);
    if (!w) { rig_log("fe hub: %s", err); return -1; }
    char dir[1100], p[1300];
    snprintf(dir, sizeof dir, "%s/data/Saves", rig_dir); mkdir(dir, 0755);
    snprintf(p, sizeof p, "%s/%s.json", dir, name);
    int rc = jsave_new(w, p); jfree(w);
    if (rc) { snprintf(err, errn, "The hub map could not be written into the Saves folder."); rig_log("fe hub: write %s failed (%s)", p, strerror(errno)); return -1; }
    if (path) snprintf(path, pn, "%s", p);
    if (kind == FE_WORLD_PVP) snprintf(pvp_file, sizeof pvp_file, "%s", p);
    rig_log("fe hub: %s written", p);
    return 0;
}
/* host, leaving a PvP v1 world for the title: its save file goes (the world was made only for the match) */
void pvp_v1_forget_world(void) {
    if (!pvp_file[0]) return;
    int rc = unlink(pvp_file);
    rig_log("pvp v1: the PvP world file %s %s", pvp_file, rc ? "was already gone" : "is deleted (it was only for the match)");
    pvp_file[0] = 0;
}
/* host, at the title: build the world, write it into data/Saves and load it. 0 = the load is queued. */
int fe_world_start(int kind, char *err, size_t errn) {
    if (scene() != 1 || !mf) { snprintf(err, errn, "Start this from the title screen."); return -1; }
    char name[64];
    if (fe_world_write(kind, name, sizeof name, NULL, 0, err, errn)) return -1;
    queue_load(name);
    rig_log("fe hub: load of %s queued", name);
    return 0;
}

/* the brought-units build (pvp_units.c, fe_build_hub) gives the world a new campaign guid: a PvP v1 world keeps its
   prefix (host, join point, right after that build); its fighters are fed and rested. 0 ok. */
int pvp_v1_keep_prefix(const char *path) {
    char err[300]; jv *w = jload(path, err, sizeof err);
    if (!w) { rig_log("pvp v1: %s: %s", path, err); return -1; }
    jv *h = jget(w, "CampaignSaveHeader"), *cl = jget(w, "ClanSaveData"), *g = h ? jget(h, "CampaignGuid") : NULL;
    if (!g || g->t != JN_STR || g->n != 36 || !cl) { jfree(w); return -1; }
    char cg[37]; memcpy(cg, g->s, 36); cg[36] = 0; memcpy(cg, PVP_V1_PREFIX, 5);
    jset(h, "CampaignGuid", jstrv(cg)); jset(cl, "CampaignGuid", jstrv(cg));
    /* the fighters arrive fed and rested: a hungry settler left its walk to the team's end for the food (pair test v1_d);
       they are copies in a world made only for the match, nothing goes home */
    jv *pl = jpath(w, "EntityContainerSaveData.UnitContainerSaveData.Base.PlayerGuidList"), *ec = jpath(w, "EntityComponentSaveData.EntityComponents");
    int fed = 0;
    for (size_t i = 0; pl && ec && i < pl->cnt; i++) {
        jv *u = pl->v[i]; if (!jis(u, JN_STR)) continue;
        char gk[40]; snprintf(gk, sizeof gk, "%.*s", (int)u->n, u->s);
        jv *st = fe_comp(jget(ec, gk), 0), *ss = st ? jget(st, "StateStats") : NULL;
        if (!jis(ss, JN_OBJ)) continue;
        jset(ss, "Hunger", jnum_d(1.0)); jset(ss, "Energy", jnum_d(1.0)); jset(ss, "Stress", jnum_d(0.0)); fed++;
        /* no settler jobs: they hauled the template's ground items instead of walking / fighting (v1_e, task 15) */
        jv *tp = jget(jget(jpath(w, "PlayerUnitsSaveData.TaskPriorities"), gk), "Priority");
        for (size_t k = 0; jis(tp, JN_OBJ) && k < tp->cnt; k++) { jfree(tp->v[k]); tp->v[k] = jnum_i(0); }
    }
    rig_log("pvp v1: %d fighter(s) fed and rested, settler jobs off", fed);
    char tmp[1400]; snprintf(tmp, sizeof tmp, "%s.v1", path);
    unlink(tmp);
    int rc = jsave_new(w, tmp) || rename(tmp, path) ? -1 : 0;
    jfree(w);
    rig_log("pvp v1: world with the brought units keeps the PvP v1 campaign prefix (%s): rc %d", cg, rc);
    return rc;
}

/* the hub's stock (brief "storage stocked"; owner earlier: no food in the hub), put into the hub save by the host while it
   builds the hub (fe_join_point_save; every peer loads that file). Each stack is a copy of one of the template's own ground
   items of the same key (its component record shape), fresh durability, new guid, on a free explored cell (the template's
   discovered field area, no entity there) nearest to the clan's units; settlers carry them to storage. Keys = those the
   template has on the ground (MaxStack 20 each in the ItemTable of 0.4.29). An in-sim SpawnItemRequested at the start did
   not work: the join point was written before the events ran (hub dive 2026-10-16: none of the 380 items in the hub). */
static const struct { const char *key; int amount; } HUB_STOCK[] = {
    { "ITEM_Hardtack", 100 }, { "ITEM_BlueBerry", 40 }, { "ITEM_Ointment", 40 }, { "ITEM_AidKit", 20 }, { "ITEM_LumaLog", 100 } };
#define HUB_STACK 20
int fe_hub_template_save(const jv *save) {                                 /* this save is a neutral hub not started yet */
    jv *g = jpath(save, "ClanSaveData.CampaignGuid");
    return g && g->t == JN_STR && g->n >= 5 && !memcmp(g->s, FE_HUB_TPL_PREFIX, 5);
}
static int cell_taken(const jv *pm, int x, int y) {
    for (size_t j = 0; pm && j < pm->cnt; j++) if ((int)floor(jnumv(jget(pm->v[j], "x"))) == x && (int)floor(jnumv(jget(pm->v[j], "y"))) == y) return 1;
    return 0;
}
int fe_hub_stock_json(jv *hub) {
    jv *ecd = jget(hub, "EntityContainerSaveData"), *ic = jpath(hub, "EntityContainerSaveData.ItemContainerSaveData.Base");
    jv *om = ic ? jget(ic, "OnMapItems") : NULL, *reg = ic ? jget(ic, "ItemSaveData") : NULL, *af = ic ? jget(ic, "ItemAutoFixable") : NULL;
    jv *pm = jpath(hub, "EntityContainerSaveData.EntityPositions.Base"), *ec = jpath(hub, "EntityComponentSaveData.EntityComponents");
    jv *fa = jpath(hub, "RawBaseSaveData.BaseFieldAreas"), *pl = jpath(hub, "EntityContainerSaveData.UnitContainerSaveData.Base.PlayerGuidList");
    if (!ecd || !om || !reg || !af || !pm || !ec || !jis(fa, JN_ARR)) { rig_log("fe hub: stock skipped (save layout)"); return -1; }
    double sx = 0, sy = 0; int nu = 0;                                     /* centre: the clan's units, else the ground items */
    for (size_t i = 0; pl && i < pl->cnt; i++) { jv *u = pl->v[i]; char k[40]; if (!jis(u, JN_STR)) continue; snprintf(k, sizeof k, "%.*s", (int)u->n, u->s);
        jv *p = jget(pm, k); if (p) { sx += jnumv(jget(p, "x")); sy += jnumv(jget(p, "y")); nu++; } }
    for (size_t i = 0; !nu && i < om->cnt; i++) { jv *p = jget(pm, om->k[i]); if (p) { sx += jnumv(jget(p, "x")); sy += jnumv(jget(p, "y")); } if (i + 1 == om->cnt) nu = (int)om->cnt; }
    if (!nu) { rig_log("fe hub: stock skipped (no centre)"); return -1; }
    int cx = (int)floor(sx / nu), cy = (int)floor(sy / nu);
    /* free cells: discovered field area, no entity, nearest first (ties: y then x) */
    int cand[2048][2], nc = 0;
    for (size_t a = 0; a < fa->cnt && nc < 2048; a++) {
        jv *ar = fa->v[a]; if (!jtruthy(jget(ar, "IsDiscovered"))) continue;
        jv *ps = jget(ar, "Positions");
        for (size_t i = 0; jis(ps, JN_ARR) && i + 1 < ps->cnt && nc < 2048; i += 2) {
            int x = (int)jnumv(ps->v[i]), y = (int)jnumv(ps->v[i + 1]);
            if (abs(x - cx) > 12 || abs(y - cy) > 12 || cell_taken(pm, x, y)) continue;
            cand[nc][0] = x; cand[nc][1] = y; nc++;
        }
    }
    for (int i = 1; i < nc; i++) for (int j = i; j > 0; j--) {             /* insertion sort by distance */
        int *p = cand[j - 1], *q = cand[j];
        long dp = (long)(p[0] - cx) * (p[0] - cx) + (long)(p[1] - cy) * (p[1] - cy), dq = (long)(q[0] - cx) * (q[0] - cx) + (long)(q[1] - cy) * (q[1] - cy);
        if (dp < dq || (dp == dq && (p[1] < q[1] || (p[1] == q[1] && p[0] <= q[0])))) break;
        int t0 = p[0], t1 = p[1]; p[0] = q[0]; p[1] = q[1]; q[0] = t0; q[1] = t1;
    }
    int used = 0, stacks = 0, items = 0, missing = 0;
    for (size_t s = 0; s < sizeof HUB_STOCK / sizeof HUB_STOCK[0]; s++) {
        const char *donor = NULL;
        for (size_t i = 0; i < om->cnt && !donor; i++) if (jstreq(jget(om->v[i], "Key"), HUB_STOCK[s].key) && jget(ec, om->k[i])) donor = om->k[i];
        if (!donor) { missing++; rig_log("fe hub: stock: no %s in the template to copy", HUB_STOCK[s].key); continue; }
        for (int left = HUB_STOCK[s].amount; left > 0 && used < nc; left -= HUB_STACK, used++) {
            int cnt = left < HUB_STACK ? left : HUB_STACK;
            char g[37]; fe_new_guid(g);
            jv *comps = jcopy(jget(ec, donor)), *st = fe_comp(comps, 0), *stats = st ? jget(st, "Stats") : NULL;
            if (jis(stats, JN_OBJ) && jget(stats, "MaxDurability")) jset(stats, "Durability", jcopy(jget(stats, "MaxDurability")));
            jset(ec, g, comps);
            jv *r = jobj(); jset(r, "Guid", jstrv(g)); jset(r, "Key", jstrv(HUB_STOCK[s].key)); jset(reg, g, r);
            jset(af, g, jbool(0));
            jv *o = jobj(); jset(o, "Guid", jstrv(g)); jset(o, "Key", jstrv(HUB_STOCK[s].key)); jset(o, "Amount", jnum_i(cnt)); jset(o, "AutoFixed", jbool(0)); jset(om, g, o);
            jv *p = jobj(); jset(p, "x", jnum_d(cand[used][0] + 0.5)); jset(p, "y", jnum_d(cand[used][1] + 0.5)); jset(pm, g, p);
            stacks++; items += cnt;
        }
    }
    rig_log("fe hub: storage stock: %d stack(s), %d item(s) on free cells around (%d,%d)%s", stacks, items, cx, cy, missing ? " (some keys had no template copy)" : "");
    return stacks;
}
