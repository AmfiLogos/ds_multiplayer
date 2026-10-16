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
int fe_hub_template_world(void) { return world_prefix(FE_HUB_TPL_PREFIX); }
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
    if (css) { jset(css, "IronModeEnabled", jbool(0)); jset(css, "CampaignName", jstrv(disp)); }
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
   prefix (host, join point, right after that build). 0 ok. */
int pvp_v1_keep_prefix(const char *path) {
    char err[300]; jv *w = jload(path, err, sizeof err);
    if (!w) { rig_log("pvp v1: %s: %s", path, err); return -1; }
    jv *h = jget(w, "CampaignSaveHeader"), *cl = jget(w, "ClanSaveData"), *g = h ? jget(h, "CampaignGuid") : NULL;
    if (!g || g->t != JN_STR || g->n != 36 || !cl) { jfree(w); return -1; }
    char cg[37]; memcpy(cg, g->s, 36); cg[36] = 0; memcpy(cg, PVP_V1_PREFIX, 5);
    jset(h, "CampaignGuid", jstrv(cg)); jset(cl, "CampaignGuid", jstrv(cg));
    char tmp[1400]; snprintf(tmp, sizeof tmp, "%s.v1", path);
    unlink(tmp);
    int rc = jsave_new(w, tmp) || rename(tmp, path) ? -1 : 0;
    jfree(w);
    rig_log("pvp v1: world with the brought units keeps the PvP v1 campaign prefix (%s): rc %d", cg, rc);
    return rc;
}

/* the hub's storage stock (brief "storage stocked"; owner earlier: no food in the hub). Inside the DSNET_FE_START sim
   command (fe_game.c fe_on_start), every peer at the same step: the stock SpawnItemRequested event per stack (the game
   puts the items on the ground; the settlers carry them to storage), on walkable cells next to the clan's units.
   Keys and stack sizes checked in the game's ItemTable (build 25846389, DS_B.0.4.29: MaxStack 20 each). */
static const struct { const char *key; int amount; } HUB_STOCK[] = {
    { "ITEM_Hardtack", 100 }, { "ITEM_WildHerb", 40 }, { "ITEM_Ointment", 20 }, { "ITEM_AidKit", 20 },
    { "ITEM_WoodPlank", 60 }, { "ITEM_StoneBrick", 60 }, { "ITEM_Textile", 40 }, { "ITEM_Leather", 40 } };
#define HUB_STACK 20
Obj pvp_runtime_map(int map);                                               /* pvp/pvp_game.c */
typedef struct { int32_t x, y; } HV2I;
static int hub_cell(Obj pd, const uint8_t g[16], int32_t c[2]) {           /* Dictionary<Guid, Vector2>: entry 0x20 */
    Obj ent = AT(pd, 0x18, Obj); int n = AT(pd, 0x20, int32_t);
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x20;
        if (*(int32_t *)e >= 0 && !memcmp(e + 8, g, 16)) { c[0] = (int32_t)floorf(*(float *)(e + 0x18)); c[1] = (int32_t)floorf(*(float *)(e + 0x1C)); return 1; }
    }
    return 0;
}
int fe_hub_stock(void) {
    Obj ud = units_dict(0), pd = pos_dict(0), rd = pvp_runtime_map(0), eq = mf ? AT(mf, 0x48, Obj) : NULL;
    if (!ud || !pd || !rd || !eq) { rig_log("fe hub: stock skipped (map data %p %p %p)", ud, pd, rd); return -1; }
    int64_t sx = 0, sy = 0; int nu = 0;
    Obj ent = AT(ud, 0x18, Obj); int n = AT(ud, 0x20, int32_t);
    for (int i = 0; ent && i < n; i++) {                                   /* the clan's units on the settlement */
        char *e = (char *)ent + 0x20 + i * 0x20; if (*(int32_t *)e < 0) continue;
        Obj u = *(Obj *)(e + 0x18); Obj r = u ? AT(u, 0x40, Obj) : NULL; Obj ci = r ? AT(r, 0x10, Obj) : NULL;
        int32_t c[2]; if (!ci || AT(ci, 0x18, int32_t) != 1 || !hub_cell(pd, (const uint8_t *)(e + 8), c)) continue;
        sx += c[0]; sy += c[1]; nu++;
    }
    if (!nu) { rig_log("fe hub: stock skipped (no clan unit on the settlement)"); return -1; }
    int cx = (int)(sx / nu), cy = (int)(sy / nu), mx = AT(rd, 0x28, int32_t), my = AT(rd, 0x2C, int32_t);
    int ring = 2, stacks = 0, items = 0, side = 0, step = 0;
    for (size_t s = 0; s < sizeof HUB_STOCK / sizeof HUB_STOCK[0]; s++)
        for (int left = HUB_STOCK[s].amount; left > 0; left -= HUB_STACK) {
            HV2I v = { 0, 0 }; int found = 0;
            while (!found && ring <= 12) {                                 /* square rings around the centre, fixed order */
                int len = 2 * ring;
                int x = side == 0 ? cx - ring + step : side == 1 ? cx + ring : side == 2 ? cx + ring - step : cx - ring;
                int y = side == 0 ? cy - ring : side == 1 ? cy - ring + step : side == 2 ? cy + ring : cy + ring - step;
                if (++step >= len) { step = 0; if (++side == 4) { side = 0; ring += 2; } }
                if (x < 0 || y < 0 || x >= mx || y >= my) continue;
                v.x = x; v.y = y;
                found = F(0xBEF2F0, bool (*)(Obj, HV2I, void *))(rd, v, NULL);   /* RuntimeMapData.IsMovable */
            }
            if (!found) { rig_log("fe hub: stock: no free cell left after %d stacks", stacks); goto done; }
            int cnt = left < HUB_STACK ? left : HUB_STACK;
            Obj ev = obj_new(0x66549B0);                                       /* SpawnItemRequested */
            uint64_t pos = (uint32_t)v.x | ((uint64_t)(uint32_t)v.y << 32);
            F(0xD62DDC, void (*)(Obj, int32_t, Obj, uint64_t, int32_t, void *))(ev, 0, F(0x6F9738, Obj (*)(const char *))(HUB_STOCK[s].key), pos, cnt, NULL);
            F(0xCCFAD4, void (*)(Obj, Obj, void *))(eq, ev, NULL);            /* EventQueue.AddEvent */
            stacks++; items += cnt;
        }
done:
    rig_log("fe hub: storage stock: %d stack(s), %d item(s) put down around (%d,%d)", stacks, items, cx, cy);
    return stacks;
}
