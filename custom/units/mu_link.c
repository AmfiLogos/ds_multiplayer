/* mu_link - one-click "Add from wiki link" (see mu_link.h). */
#include "mu_link.h"
#include "myunits.h"
#include "wk_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static int state = MUL_IDLE;           /* written by the caller's thread only */
static mul_result res;
static char text[16384];
static wk_build units[WK_MAX_UNITS];
static volatile int w_done, w_n, w_code;   /* worker -> caller, read after the join */
static char w_err[400];
static int w_thread;                    /* a worker thread to join */
#ifdef _WIN32
static HANDLE th;
static DWORD WINAPI work(LPVOID u) {
#else
static pthread_t th;
static void *work(void *u) {
#endif
    (void)u;
    int code = 0;
    int n = wkl_resolve(text, units, WK_MAX_UNITS, MUL_TIMEOUT_MS, &code, w_err, sizeof w_err);
    w_n = n; w_code = code;
    __atomic_store_n(&w_done, 1, __ATOMIC_RELEASE);
    return 0;
}

static int start(const char *t) {
    if (state == MUL_BUSY) return state;
    memset(&res, 0, sizeof res);
    while (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n') t++;
    if (!*t || !wkl_looks_like_link(t)) {
        state = MUL_NEED_BOX; res.state = state;
        snprintf(res.msg, sizeof res.msg, "%s", *t ? "That is not a wiki builder link. Copy a team or unit link on the wiki builder, or paste it here."
                                                   : "Copy a team or unit link on the wiki builder, or paste it here.");
        return state;
    }
    snprintf(text, sizeof text, "%s", t);
    w_done = 0; w_n = 0; w_code = 0; w_err[0] = 0;
#ifdef _WIN32
    th = CreateThread(NULL, 0, work, NULL, 0, NULL);
    int ok = th != NULL;
#else
    int ok = pthread_create(&th, NULL, work, NULL) == 0;
#endif
    w_thread = ok;
    if (!ok) work(NULL);                                                 /* no thread: read here (a short link may block) */
    state = MUL_BUSY; res.state = state;
    return state;
}

int mul_click(void) {
    if (state == MUL_BUSY) return state;
    static char clip[16384];
    if (mu_clip_text(clip, sizeof clip)) clip[0] = 0;
    return start(clip);
}
int mul_submit(const char *t) { return start(t ? t : ""); }

int mul_poll(mul_result *r) {
    if (state == MUL_BUSY && __atomic_load_n(&w_done, __ATOMIC_ACQUIRE)) {
        if (w_thread) {
#ifdef _WIN32
            WaitForSingleObject(th, INFINITE); CloseHandle(th);
#else
            pthread_join(th, NULL);
#endif
        }
        memset(&res, 0, sizeof res);
        if (w_n < 1) { state = MUL_FAILED; res.code = w_code; snprintf(res.msg, sizeof res.msg, "%s", w_err); }
        else {
            char e[400];
            jv *lib = mu_load(e, sizeof e);
            if (!lib) { state = MUL_FAILED; snprintf(res.msg, sizeof res.msg, "%s", e); }
            else if (mu_add_team(lib, units, w_n, NULL, res.ids, e, sizeof e) < 0 || mu_save(lib, e, sizeof e)) {
                state = MUL_FAILED; snprintf(res.msg, sizeof res.msg, "Could not add to My Units: %s", e);
            } else {
                state = MUL_DONE; res.n = w_n;
                size_t nt = mu_team_count(lib); char nm[200] = "", ids[MU_TEAM_MAX][17];
                if (nt) { mu_team_id_at(lib, nt - 1, res.team_id); mu_team_at(lib, nt - 1, nm, sizeof nm, ids); }
                if (w_n == 1) snprintf(res.msg, sizeof res.msg, "Added 1 unit to My Units: %s", nm);
                else snprintf(res.msg, sizeof res.msg, "Added %d units to My Units as the team %s", w_n, nm);
            }
            jfree(lib);
        }
        res.state = state;
    }
    if (r) { *r = res; r->state = state; }
    return state;
}
int mul_reset(void) {
    if (state == MUL_BUSY) return state;
    state = MUL_IDLE; memset(&res, 0, sizeof res);
    return state;
}
