/* mpsolo_test - the single-player copy (mp_solo.c) on a real save: the wiki's public base save (argv[1]).
 * Real breaks it catches: the copy is not the world as it was (a value lost or changed), the copy still carries the old
 * campaign guid somewhere (two worlds with one guid: the game's iron-mode cleanup deletes the other world's saves), the
 * original file changes, the wrong save of the world is copied (an older one, or another world's).
 * make test FIXTURE=<save.json> (default: ~/dungeonsettlersdb/site/assets/saves/base_25846389.json) */
#include "../mp_solo.h"
#include "../fe_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static int fails, passes;
static void check(const char *name, int ok, const char *info) {
    if (ok) passes++; else fails++;
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, info && *info ? "  " : "", info ? info : "");
}
static char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)l + 1); *n = fread(b, 1, (size_t)l, f); b[*n] = 0; fclose(f); return b;
}
static void put(const char *p, const char *s, size_t n, time_t mtime) {
    FILE *f = fopen(p, "wb"); fwrite(s, 1, n, f); fclose(f);
    struct timeval tv[2] = { { mtime, 0 }, { mtime, 0 } }; utimes(p, tv);
}
/* the fixture as a save of world `guid` named name */
static void make_save(const char *dir, const char *name, const char *guid, time_t mtime) {
    char e[300]; jv *w = jload(getenv("FIXTURE"), e, sizeof e);
    if (!w) { printf("FAIL fixture: %s\n", e); exit(2); }
    jset(jget(w, "CampaignSaveHeader"), "CampaignGuid", jstrv(guid)); jset(jget(w, "ClanSaveData"), "CampaignGuid", jstrv(guid));
    jset(jget(w, "CampaignSaveHeader"), "SaveName", jstrv(name));
    jset(jget(w, "CampaignSaveHeader"), "ClanName", jstrv("Moonlight \\\"Test\\\" Brigade")); jset(jget(w, "ClanSaveData"), "ClanName", jstrv("Moonlight \\\"Test\\\" Brigade"));
    size_t n; char *s = jdump(w, 1, &n); jfree(w);
    char p[1400]; snprintf(p, sizeof p, "%s/%s.json", dir, name); put(p, s, n, mtime); free(s);
}
static int count(const char *hay, const char *needle) { int n = 0; for (const char *p = hay; (p = strstr(p, needle)); p++) n++; return n; }

int main(void) {
    static char dflt[1200]; if (!getenv("FIXTURE")) { snprintf(dflt, sizeof dflt, "%s/dungeonsettlersdb/site/assets/saves/base_25846389.json", getenv("HOME")); setenv("FIXTURE", dflt, 1); }
    char dir[1100]; snprintf(dir, sizeof dir, "%s/mpsolo_test_XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(dir)) { printf("FAIL mkdtemp\n"); return 2; }
    char gdir[1200]; snprintf(gdir, sizeof gdir, "%s/game", dir); mkdir(gdir, 0755);   /* a second folder: the game's own Saves */
    const char *G = "41e9c1bb-8790-492c-bdc1-0f4dcad22179", *O = "0d094c40-81f9-41e3-95a4-1989d839dd6f";
    time_t t = time(NULL) - 1000;
    make_save(dir, "MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0", G, t);            /* older, modded folder */
    make_save(gdir, "MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_1", G, t + 100);     /* the newest of the world */
    make_save(gdir, "Elsewhere__AutoSave_0d094c4081f941e395a41989d839dd6f_0", O, t + 200);            /* another world, newer */
    make_save(gdir, "my manual save", G, t + 50);                                                       /* manual name: guid from its header */
    make_save(gdir, "mp_sync", G, t + 300);                                                             /* a multiplayer temporary save */

    const char *dirs[2] = { dir, gdir }; mps_save s;
    int f = mps_newest(dirs, 2, G, &s);
    check("newest save of the world: the newest file of THAT world (not another world's, not mp_*)", f && strstr(s.path, "_AutoSave_41e9c1bb8790492cbdc10f4dcad22179_1.json") != NULL, f ? s.path : "none");
    make_save(gdir, "manual newest", G, t + 250);
    f = mps_newest(dirs, 2, G, &s);
    check("newest save of the world: a manual save (guid read from its header) counts", f && strstr(s.path, "/manual newest.json") != NULL, f ? s.path : "none");

    char src[1400]; snprintf(src, sizeof src, "%s/MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_1.json", gdir);
    size_t n0; char *before = slurp(src, &n0);
    const char *NG = "5a7c0e2b-3f4d-4c8e-9b1a-2d6e8f0a1b3c";
    char out[1400], err[300] = "";
    int rc = mps_copy(src, NG, "", " (solo) \"x\"", out, sizeof out, err, sizeof err);
    check("copy written", rc == 0, err);
    if (rc) { printf("RESULT FAIL %d PASS, %d FAIL\n", passes, fails); return 1; }
    char want[1400]; snprintf(want, sizeof want, "%s/MoonlightBrigade__AutoSave_5a7c0e2b3f4d4c8e9b1a2d6e8f0a1b3c_1.json", gdir);
    check("copy: in the same folder as the original, named for its new guid", !strcmp(out, want), out);
    size_t n1; char *after = slurp(src, &n1);
    check("original file unchanged (bytes)", n0 == n1 && !memcmp(before, after, n0), "");
    size_t nc; char *ct = slurp(out, &nc);
    char og32[33] = "41e9c1bb8790492cbdc10f4dcad22179";
    check("copy: the old campaign guid is nowhere in it", ct && !count(ct, G) && !count(ct, og32), "");
    jv *c = jload(out, err, sizeof err), *o = jload(src, err, sizeof err);
    check("copy: campaign guid = the new one in the header and the clan record", c && jstreq(jpath(c, "CampaignSaveHeader.CampaignGuid"), NG) && jstreq(jpath(c, "ClanSaveData.CampaignGuid"), NG), "");
    check("copy: SaveName = its file name (the game loads <SaveName>.json)", c && jstreq(jpath(c, "CampaignSaveHeader.SaveName"), "MoonlightBrigade__AutoSave_5a7c0e2b3f4d4c8e9b1a2d6e8f0a1b3c_1"), "");
    const char *nm = "Moonlight \\\"Test\\\" Brigade (solo) \\\"x\\\"";
    check("copy: the world's name + the suffix (JSON-escaped)", c && jstreq(jpath(c, "CampaignSaveHeader.ClanName"), nm) && jstreq(jpath(c, "ClanSaveData.ClanName"), nm), c && jpath(c, "CampaignSaveHeader.ClanName") ? jpath(c, "CampaignSaveHeader.ClanName")->s : "");
    /* everything else = the original: put the original's identity back into the copy and compare the whole world */
    if (c && o) {
        jv *h = jget(c, "CampaignSaveHeader"), *cl = jget(c, "ClanSaveData"), *oh = jget(o, "CampaignSaveHeader");
        jset(h, "CampaignGuid", jcopy(jget(oh, "CampaignGuid"))); jset(cl, "CampaignGuid", jcopy(jget(oh, "CampaignGuid")));
        jset(h, "ClanName", jcopy(jget(oh, "ClanName"))); jset(cl, "ClanName", jcopy(jpath(o, "ClanSaveData.ClanName")));
        jset(h, "SaveName", jcopy(jget(oh, "SaveName")));
        jv *css = jpath(c, "CampaignSettingSaveData.CampaignStartSettingSaveData"), *ocss = jpath(o, "CampaignSettingSaveData.CampaignStartSettingSaveData");
        if (css && ocss) jset(css, "CampaignName", jcopy(jget(ocss, "CampaignName")));
        check("copy: every other value equals the original (the world as it was)", jequal(c, o), "");
    }
    jfree(c); jfree(o);
    printf("work folder %s (left for a look; under TMPDIR)\n", dir);
    printf("RESULT %s %d PASS, %d FAIL\n", fails ? "FAIL" : "PASS", passes, fails);
    free(before); free(after); free(ct);
    return fails ? 1 : 0;
}
