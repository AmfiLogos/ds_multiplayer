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
#include <time.h>

enum { FE_WORLD_HUB = 0, FE_WORLD_PVP = 1 };

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
    game_version(ver, sizeof ver);
    snprintf(stem, sizeof stem, "%s_%.8s", kind == FE_WORLD_HUB ? "MPHub" : "MPPvP", cg);
    const char *disp = kind == FE_WORLD_HUB ? "Foreign Expedition Hub" : "PvP Base";
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    jset(h, "CampaignGuid", jstrv(cg)); jset(h, "SaveName", jstrv(stem)); jset(h, "DisplayName", jstrv(disp));
    jset(h, "ClanName", jstrv(disp)); jset(h, "SaveTime", jstrv(ts)); jset(h, "Version", jstrv(ver));
    jset(h, "IronModeEnabled", jbool(0)); jset(h, "IsAutoSave", jbool(0));
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

/* host, at the title: build the world, write it into data/Saves and load it. 0 = the load is queued. */
int fe_world_start(int kind, char *err, size_t errn) {
    if (scene() != 1 || !mf) { snprintf(err, errn, "Start this from the title screen."); return -1; }
    char name[64];
    jv *w = fe_world_build(kind, name, sizeof name, err, errn);
    if (!w) { rig_log("fe hub: %s", err); return -1; }
    char dir[1100], p[1300];
    snprintf(dir, sizeof dir, "%s/data/Saves", rig_dir); mkdir(dir, 0755);
    snprintf(p, sizeof p, "%s/%s.json", dir, name);
    int rc = jsave_new(w, p); jfree(w);
    if (rc) { snprintf(err, errn, "The hub map could not be written into the Saves folder."); rig_log("fe hub: write %s failed (%s)", p, strerror(errno)); return -1; }
    queue_load(name);
    rig_log("fe hub: %s written, load queued", p);
    return 0;
}
