// win_kit.c - "fingerprint recorder" mode for a test on a real Windows PC (no rig, no Mac tools there).
// Active when <game folder>\dsmp_kit\record.txt exists (and DSRIG_DIR is not set). The game then starts normally
// (Steam stays on), and by itself: loads dsmp_kit\save.json (lockstep det mode, seed from record.txt), schedules
// the moves in dsmp_kit\orders.txt, runs `ticks` locked ticks, writes the per-tick fingerprints to dsmp_kit\fp.log
// (rig_det does that), writes dsmp_kit\RESULT.txt and quits. Saves and settings go to dsmp_kit\data (the player's own
// saves are not touched). Mac side: win/kit/kit_record.py compare fp.log.
#include "dswin.h"
#include "rig.h"

int kit_mode;
static int seed = 12345, ticks = 6000, per_frame = 6, math_on = 1, state;
static int64_t target, t_state;

static void kit_cmd(const char *line) {
    char *r = rig_exec(line);
    rig_log("kit: %s -> %.300s", line, r ? r : "(null)");
    free(r);
}

/* DllMain: <exe dir>\dsmp_kit\record.txt -> kit mode, rig_dir = that folder */
int win_kit_detect(char *dir, size_t n) {
    char exe[MAX_PATH]; DWORD k = GetModuleFileNameA(NULL, exe, sizeof exe);
    if (!k) return 0;
    char *sl = strrchr(exe, '\\'); if (!sl) return 0; *sl = 0;
    char rec[MAX_PATH + 64]; snprintf(rec, sizeof rec, "%s\\dsmp_kit\\record.txt", exe);
    FILE *f = fopen(rec, "r"); if (!f) return 0;
    char key[32]; int v;
    while (fscanf(f, "%31s %d", key, &v) == 2) {
        if (!strcmp(key, "seed")) seed = v; else if (!strcmp(key, "ticks")) ticks = v;
        else if (!strcmp(key, "per_frame")) per_frame = v; else if (!strcmp(key, "math")) math_on = v;
    }
    fclose(f);
    snprintf(dir, n, "%s/dsmp_kit", exe);
    for (char *p = dir; *p; p++) if (*p == '\\') *p = '/';
    if (math_on) _putenv("DSMP_MATH=1");
    kit_mode = 1;
    return 1;
}

static void finish(const char *what) {
    char p[1200]; snprintf(p, sizeof p, "%s/RESULT.txt", rig_dir);
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%s\nlast tick %lld\nsend the file fp.log (same folder)\n", what, (long long)tick()); fclose(f); }
    rig_log("kit: %s", what);
    ((void (*)(void *))(ds_base + rv(0x38E91E0)))(NULL);              /* UnityEngine.Application$$Quit */
}

/* once per MainFlow.Update, after rig_frame */
void win_kit_frame(void) {
    if (!kit_mode || state < 0) return;
    if (state == 0) {                                              /* title, idle -> load the bundled save */
        if (scene() != 1 || busy()) return;
        char line[1400];
        snprintf(line, sizeof line, "{\"cmd\":\"det_seed\",\"seed\":%d}", seed); kit_cmd(line);
        snprintf(line, sizeof line, "{\"cmd\":\"load_save\",\"file\":\"%s/save.json\",\"det\":1}", rig_dir); kit_cmd(line);
        state = 1; t_state = rig_frames;
    } else if (state == 1) {                                       /* auto-lock after the load */
        if (!rig_det_locked()) { if (rig_frames - t_state > 30 * 600) { finish("FAILED: the save did not load in 10 min"); state = -1; } return; }
        int64_t lock = tick();
        char p[1200]; snprintf(p, sizeof p, "%s/orders.txt", rig_dir);
        FILE *f = fopen(p, "r"); int n = 0;
        if (f) {
            long long t; char g[64]; int x, y;
            while (fscanf(f, "%lld %63s %d %d", &t, g, &x, &y) == 4) {
                char line[300];
                snprintf(line, sizeof line, "{\"cmd\":\"schedule\",\"kind\":\"move\",\"tick\":%lld,\"guid\":\"%s\",\"x\":%d,\"y\":%d}", t, g, x, y);
                kit_cmd(line); n++;
            }
            fclose(f);
        }
        target = lock + ticks;
        char line[200]; snprintf(line, sizeof line, "{\"cmd\":\"run\",\"to\":%lld,\"per_frame\":%d,\"jitter\":0}", (long long)target, per_frame);
        kit_cmd(line);
        rig_log("kit: locked at %lld, %d orders, running to %lld", (long long)lock, n, (long long)target);
        state = 2;
    } else if (state == 2) {
        if (tick() >= target) { finish("DONE: recording complete"); state = -1; }
    }
}
