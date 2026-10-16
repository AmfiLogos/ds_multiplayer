/* tf_worlds.c - see tf_worlds.h */
#include "tf_worlds.h"
#include "cm_win.h"
#include "../fe/fe_json.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_(p) _mkdir(p)
#else
#include <dirent.h>
#define mkdir_(p) mkdir(p, 0755)
#endif

#define NW 64
static char W[NW][33]; static int nw, loaded;             /* test world GUIDs: 32 lowercase hex, no dashes; newest last */

static void norm(const char *g, char out[33]) {
    int k = 0;
    for (; g && *g && k < 32; g++) if (isxdigit((unsigned char)*g)) out[k++] = (char)tolower((unsigned char)*g);
    out[k] = 0;
}
static int reg_path(char *p, size_t n) {
    char d[1024]; if (cmw_game_data(d, sizeof d)) return -1;
    snprintf(p, n, "%s/Mods", d); mkdir_(p);
    snprintf(p, n, "%s/Mods/custom-modes", d); mkdir_(p);
    snprintf(p, n, "%s/Mods/custom-modes/test_worlds.json", d);
    return 0;
}
static void reg_load(void) {                             /* once the game's data folder is known (first frame) */
    if (loaded) return;
    char p[1100]; nw = 0; if (reg_path(p, sizeof p)) return;
    loaded = 1;
    char e[200]; jv *r = jload(p, e, sizeof e); if (!r) return;
    jv *a = jget(r, "worlds");
    for (size_t i = 0; a && a->t == JN_ARR && i < a->cnt && nw < NW; i++) {
        char *s = jstrdup_raw(a->v[i]); norm(s, W[nw]); free(s);
        if (strlen(W[nw]) == 32) nw++;
    }
    jfree(r);
}
static void reg_save(void) {
    char p[1100], t[1120]; if (reg_path(p, sizeof p)) return;
    snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "wb"); if (!f) return;
    fprintf(f, "{\"format\":\"custom-modes-test-worlds\",\"version\":1,\"worlds\":[");
    for (int i = 0; i < nw; i++) fprintf(f, "%s\"%s\"", i ? "," : "", W[i]);
    fprintf(f, "]}\n"); fclose(f);
#ifdef _WIN32
    MoveFileExA(t, p, MOVEFILE_REPLACE_EXISTING);
#else
    rename(t, p);
#endif
}
int tfw_is_test(const char *guid, const char *save_name) {
    reg_load();
    char g[33]; norm(guid, g);
    for (int i = 0; i < nw; i++) {
        if (g[0] && !strcmp(g, W[i])) return 1;
        if (save_name && strstr(save_name, W[i])) return 1;   /* "<team>_<fight>__AutoSave_<guid hex>_<n>" */
    }
    return 0;
}

/* the save files of an older test world: every .json in Saves whose name carries its GUID */
static void remove_world_files(const char *saves, const char *g) {
#ifdef _WIN32
    char pat[1200]; snprintf(pat, sizeof pat, "%s\\*%s*.json", saves, g);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do { char f[1400]; snprintf(f, sizeof f, "%s\\%s", saves, fd.cFileName); if (DeleteFileA(f)) cmw_log("tf: old test world file removed: %s", fd.cFileName); } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(saves); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 6 || strcmp(e->d_name + l - 5, ".json") || !strstr(e->d_name, g)) continue;
        char f[1400]; snprintf(f, sizeof f, "%s/%s", saves, e->d_name);
        if (!remove(f)) cmw_log("tf: old test world file removed: %s", e->d_name);
    }
    closedir(d);
#endif
}

static int world_has_files(const char *saves, const char *g) {
#ifdef _WIN32
    char pat[1200]; snprintf(pat, sizeof pat, "%s\\*%s*.json", saves, g);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h); return 1;
#else
    DIR *d = opendir(saves); if (!d) return 0;
    struct dirent *e; int found = 0;
    while (!found && (e = readdir(d))) found = strstr(e->d_name, g) != NULL;
    closedir(d); return found;
#endif
}

int tfw_write(const char *save, size_t n, char *name_out, size_t name_n, char *err, size_t errn) {
    reg_load();
    jv *s = jparse(save, n, err, errn); if (!s) return -1;
    jv *h = jget(s, "CampaignSaveHeader");
    char *name = h ? jstrdup_raw(jget(h, "SaveName")) : NULL, *guid = h ? jstrdup_raw(jget(h, "CampaignGuid")) : NULL;
    jfree(s);
    char g[33]; norm(guid, g); free(guid);
    if (!name || !name[0] || strlen(g) != 32 || strpbrk(name, "/\\:") || strstr(name, "..")) {
        snprintf(err, errn, "the generated save has no usable name"); free(name); return -1;
    }
    char saves[1100], p[1400], t[1420];
    if (cmw_saves_dir(saves, sizeof saves)) { snprintf(err, errn, "the game's save folder is not known"); free(name); return -1; }
    mkdir_(saves);
    /* older test worlds go first (only the newest one is kept) */
    for (int i = 0; i < nw; i++) remove_world_files(saves, W[i]);
    nw = 0;
    snprintf(W[nw++], 33, "%s", g); reg_save();                        /* listed BEFORE the file exists: never visible */
    snprintf(p, sizeof p, "%s/%s.json", saves, name); snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "wb");
    if (!f || fwrite(save, 1, n, f) != n) { if (f) fclose(f); snprintf(err, errn, "could not write the test world into the Saves folder"); free(name); return -1; }
    fclose(f);
#ifdef _WIN32
    if (!MoveFileExA(t, p, MOVEFILE_REPLACE_EXISTING)) { snprintf(err, errn, "could not write the test world into the Saves folder"); free(name); return -1; }
#else
    if (rename(t, p)) { snprintf(err, errn, "could not write the test world into the Saves folder"); free(name); return -1; }
#endif
    snprintf(name_out, name_n, "%s", name); free(name);
    cmw_log("tf: test world %s written (%zu bytes)", name_out, n);
    return 0;
}

/* Leaving a test world: back on the title, its saves are removed (the game's own Remove is switched off in this build:
 * CampaignSaveDeletionService.IsAvailable returns false), then the game refreshes its save list and the title's Continue,
 * which otherwise points at the test world. Not while our own load is starting (the title still shows for a few
 * frames after Start). Checked every 30 frames. */
static int loading, frames;
void tfw_loading(void) { loading = 1; }
static void tick(void *ud) {
    (void)ud;
    if (++frames % 30) return;
    int title = cmw_title_shown();
    if (loading) { if (!title) loading = 0; return; }
    if (!title) return;
    reg_load();
    if (!nw) return;
    char saves[1100]; if (cmw_saves_dir(saves, sizeof saves)) return;
    int changed = 0;
    for (int i = 0; i < nw; i++) if (world_has_files(saves, W[i])) { remove_world_files(saves, W[i]); changed = 1; }
    nw = 0; reg_save();
    if (changed) cmw_saves_changed();
}
static int f_show(void *ud, const char *guid, const char *save_name, int toggle_on) {
    (void)ud; (void)toggle_on;
    return !tfw_is_test(guid, save_name);
}
int tfw_init(const DSLHost *host, DSLPlugin *self) {
    static const DSLLoadFilter F = { f_show, NULL, NULL, NULL };
    int r = dsl_loadlist_filter(host, self, &F);
    cmw_tick_add(tick, NULL);
    cmw_log("tf: load-list filter for test worlds %s", r ? "NOT registered" : "on");
    return r;
}
