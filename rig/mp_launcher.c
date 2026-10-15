// mp_launcher.c - CFBundleExecutable of "Dungeon Settlers MP.app" / "Dungeon Settlers MP (LAN).app".
// Same job as the old zsh launcher script, as a signed arm64 binary (a script main executable never
// started on a quarantined copy): resolve the real app path (works under App Translocation), make the
// MP folder, copy the starting saves on the first run, set the mod environment, exec the game binary.
// Config: <app>/Contents/Resources/mp_launcher.conf, lines "folder=<name under CanOpener>" and "steam=0|1".
// Errors: ~/Library/Logs/DungeonSettlersMP_launcher.log
#include <mach-o/dyld.h>
#include <copyfile.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char home[PATH_MAX];

static void logf_(const char *fmt, ...) {
    char p[PATH_MAX]; snprintf(p, sizeof p, "%s/Library/Logs/DungeonSettlersMP_launcher.log", home);
    FILE *f = fopen(p, "a"); if (!f) return;
    time_t t = time(NULL); char ts[32]; strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    fprintf(f, "%s [%d] ", ts, getpid());
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

static void mkdirs(const char *path) {
    char b[PATH_MAX]; snprintf(b, sizeof b, "%s", path);
    for (char *s = b + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(b, 0755); *s = '/'; }
    mkdir(b, 0755);
}

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static void dirname_n(char *p, int n) { while (n-- > 0) { char *s = strrchr(p, '/'); if (s && s != p) *s = 0; } }

int main(int argc, char **argv) {
    const char *h = getenv("HOME");
    if (!h || !*h) { struct passwd *pw = getpwuid(getuid()); h = pw ? pw->pw_dir : "/tmp"; }
    snprintf(home, sizeof home, "%s", h);

    /* real app path: <app>/Contents/MacOS/<launcher> */
    char exe[PATH_MAX], real[PATH_MAX]; uint32_t sz = sizeof exe;
    if (_NSGetExecutablePath(exe, &sz) != 0 || !realpath(exe, real)) { logf_("cannot resolve own path"); return 1; }
    char app[PATH_MAX]; snprintf(app, sizeof app, "%s", real); dirname_n(app, 3);

    /* config */
    char folder[256] = "Dungeon Settlers MP"; int steam = 1;
    char conf[PATH_MAX]; snprintf(conf, sizeof conf, "%s/Contents/Resources/mp_launcher.conf", app);
    FILE *cf = fopen(conf, "r");
    if (cf) {
        char line[512];
        while (fgets(line, sizeof line, cf)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "folder=", 7) && line[7]) snprintf(folder, sizeof folder, "%s", line + 7);
            else if (!strncmp(line, "steam=", 6)) steam = atoi(line + 6);
        }
        fclose(cf);
    } else logf_("no %s: using defaults", conf);

    char mp[PATH_MAX], data[PATH_MAX], uhome[PATH_MAX], flag[PATH_MAX];
    snprintf(mp, sizeof mp, "%s/Library/Application Support/CanOpener/%s", home, folder);
    snprintf(data, sizeof data, "%s/data", mp);
    snprintf(uhome, sizeof uhome, "%s/home", mp);
    mkdirs(data); mkdirs(uhome);
    if (!exists(data)) { logf_("cannot create %s (%s)", data, strerror(errno)); return 1; }

    /* first run: COPY the starting saves (portable: bundled test saves; else the normal game's folder, read only) */
    snprintf(flag, sizeof flag, "%s/.copied_from_normal_game", data);
    if (!exists(flag)) {
        char src[PATH_MAX]; snprintf(src, sizeof src, "%s/Contents/Resources/mp_seed", app);
        if (!exists(src)) snprintf(src, sizeof src, "%s/Library/Application Support/CanOpener/Dungeon Settlers", home);
        const char *items[] = { "Saves", "OutGame.json", "UserSetting.json", "UserCampaignLog.json" };
        for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
            char s[PATH_MAX], d[PATH_MAX];
            snprintf(s, sizeof s, "%s/%s", src, items[i]); snprintf(d, sizeof d, "%s/%s", data, items[i]);
            if (!exists(s)) continue;
            if (copyfile(s, d, NULL, COPYFILE_ALL | COPYFILE_RECURSIVE) != 0) logf_("copy %s failed (%s)", s, strerror(errno));
        }
        FILE *f = fopen(flag, "w"); if (f) { time_t t = time(NULL); fputs(ctime(&t), f); fclose(f); }
        logf_("first run: starting saves copied from %s", src);
    }

    if (chdir(mp) != 0) logf_("chdir %s failed (%s)", mp, strerror(errno));
    if (steam) {
        FILE *f = fopen("steam_appid.txt", "w"); if (f) { fputs("2798330\n", f); fclose(f); }   /* run here, no restart through Steam */
        setenv("SteamAppId", "2798330", 1); setenv("SteamGameId", "2798330", 1);
    }
    char dylib[PATH_MAX], game[PATH_MAX], plog[PATH_MAX];
    snprintf(dylib, sizeof dylib, "%s/Contents/Frameworks/dsrig.dylib", app);
    snprintf(game, sizeof game, "%s/Contents/MacOS/Dungeon Settlers", app);
    snprintf(plog, sizeof plog, "%s/player.log", mp);
    setenv("CFFIXED_USER_HOME", uhome, 1);              /* Unity's own files stay out of the normal game's folder */
    setenv("DSRIG_DIR", mp, 1);
    if (!getenv("DSRIG_FPS")) setenv("DSRIG_FPS", "60", 1);
    if (!getenv("DSRIG_TEST_MUTE")) setenv("DSRIG_LIVE", "1", 1);   /* test launches: rig behaviour (muted) */
    setenv("DYLD_INSERT_LIBRARIES", dylib, 1);

    char **av = calloc((size_t)argc + 4, sizeof *av); int k = 0;
    av[k++] = game; av[k++] = "-logFile"; av[k++] = plog;
    for (int i = 1; i < argc; i++) if (strncmp(argv[i], "-psn_", 5)) av[k++] = argv[i];   /* old LaunchServices arg */
    av[k] = NULL;
    logf_("start %s (app %s, folder %s, steam %d)", game, app, folder, steam);
    execv(game, av);
    logf_("exec %s failed (%s)", game, strerror(errno));
    return 1;
}
