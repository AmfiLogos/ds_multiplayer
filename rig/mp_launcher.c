// mp_launcher.c - CFBundleExecutable of "Dungeon Settlers MP.app" / "Dungeon Settlers MP (LAN).app".
// Same job as the old zsh launcher script, as a signed arm64 binary (a script main executable never
// started on a quarantined copy): resolve the real app path (works under App Translocation), make the
// MP folder, copy the starting saves on the first run, set the mod environment, exec the game binary.
// Config: <app>/Contents/Resources/mp_launcher.conf, lines "folder=<name under CanOpener>" and "steam=0|1".
// steam=1: if Steam is not running, start it in the background and wait until it accepts the game (a probe
// SteamAPI_InitFlat in a child process); if it never does, show a notice instead of a game that quits at once.
// Errors: ~/Library/Logs/DungeonSettlersMP_launcher.log
#include <mach-o/dyld.h>
#include <CoreFoundation/CoreFoundation.h>
#include <copyfile.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

extern char **environ;

/* Asks the game's own libsteam_api in a child process (the launcher itself later becomes the game: it must not
   hold a Steam connection). init=0: SteamAPI_IsSteamRunning. init=1: SteamAPI_InitFlat + Shutdown, the same
   check the game makes (needs Steam running AND a signed-in user). 1 = yes, 0 = no, -1 = cannot ask. */
static int steam_probe(const char *lib, int init) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        void *h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
        if (!h) { logf_("dlopen %s: %s", lib, dlerror()); _exit(3); }
        if (!init) { int (*run)(void) = (int (*)(void))dlsym(h, "SteamAPI_IsSteamRunning"); _exit(!run ? 3 : run() ? 0 : 1); }
        int (*ini)(char *) = (int (*)(char *))dlsym(h, "SteamAPI_InitFlat");
        void (*shut)(void) = (void (*)(void))dlsym(h, "SteamAPI_Shutdown");
        if (!ini || !shut) _exit(3);
        char err[1024] = "";
        int r = ini(err);                       /* ESteamAPIInitResult: 0 = OK */
        if (r == 0) { shut(); _exit(0); }
        logf_("steam probe: init result %d (%s)", r, err);
        _exit(1);
    }
    for (int i = 0; i < 300; i++) {             /* 15 s at most */
        int st; pid_t w = waitpid(pid, &st, WNOHANG);
        if (w == pid) return WIFEXITED(st) ? (WEXITSTATUS(st) == 0 ? 1 : WEXITSTATUS(st) == 1 ? 0 : -1) : -1;
        if (w < 0) return -1;
        usleep(50000);
    }
    kill(pid, SIGKILL); waitpid(pid, NULL, 0);
    logf_("steam probe %d timed out", init);
    return 0;
}

static void notice(const char *msg) {
    if (getenv("DSMP_NO_DIALOG")) { logf_("notice (not shown): %s", msg); return; }
    CFStringRef m = CFStringCreateWithCString(NULL, msg, kCFStringEncodingUTF8);
    CFOptionFlags resp;
    CFUserNotificationDisplayAlert(0, kCFUserNotificationStopAlertLevel, NULL, NULL, NULL,
                                   CFSTR("Dungeon Settlers MP"), m, CFSTR("OK"), NULL, NULL, &resp);
    if (m) CFRelease(m);
}

/* steam=1: the game needs Steam with a signed-in user. Steam not running: start it in the background and wait.
   0 = go on, 1 = Steam never got ready (notice shown). Steam already running: no wait, same as before. */
static int ensure_steam(const char *app) {
    char lib[PATH_MAX]; snprintf(lib, sizeof lib, "%s/Contents/PlugIns/libsteam_api.dylib", app);
    if (!exists(lib)) { logf_("no %s: Steam not checked", lib); return 0; }
    int run = steam_probe(lib, 0);
    if (run != 0) { if (run < 0) logf_("cannot ask libsteam_api: Steam not checked"); return 0; }
    const char *sapp = getenv("DSMP_STEAM_APP"); if (!sapp || !*sapp) sapp = "Steam";   /* tests: a fake Steam */
    const char *w = getenv("DSMP_STEAM_WAIT"); int wait_s = w ? atoi(w) : 90;
    logf_("Steam is not running: starting %s in the background (wait up to %d s)", sapp, wait_s);
    char *oav[] = { "/usr/bin/open", "-g", "-a", (char *)sapp, NULL };
    pid_t op; int st = 1;
    if (posix_spawn(&op, oav[0], NULL, NULL, oav, environ) == 0) waitpid(op, &st, 0);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) {
        logf_("open -a %s failed (status %d)", sapp, st);
        notice("Steam could not be started. Install Steam, start it and sign in, then open Dungeon Settlers MP again.");
        return 1;
    }
    time_t t0 = time(NULL);
    while (time(NULL) - t0 < wait_s) {
        sleep(2);
        if (steam_probe(lib, 0) == 1 && steam_probe(lib, 1) == 1) {
            logf_("Steam ready after %ld s", (long)(time(NULL) - t0));
            sleep(1);                           /* let Steam see the probe's game session end */
            return 0;
        }
    }
    logf_("Steam not ready after %d s", wait_s);
    notice("Steam did not get ready. Start Steam and sign in, then open Dungeon Settlers MP again.");
    return 1;
}

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
        if (ensure_steam(app)) return 1;
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
