// hell-probe (TEST ONLY, never shipped): proves the Hell staff strip. When <DSRIG_DIR>/hell_probe_go.txt appears (its text =
// the CombatDifficulty to test), a thread attached to il2cpp calls the game's RecruitHelper.DetermineInitialEquipments
// ("AFFECTER_Mage", whose default kit has ITEM_NatureWoodStaff_1) on a helper whose campaign setting has that difficulty,
// and logs the kit it returns. The hell-difficulty module's hooks run inside that call exactly as for a real recruit
// (DetermineInitialEquipments only reads `this` for its ParseItems call; the strip reads _campaignSetting).
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(n) Sleep(n)
#else
#include <pthread.h>
#include <unistd.h>
#define SLEEP_MS(n) usleep((n) * 1000)
#endif

DSL_PLUGIN();
static const DSLHost *H; static DSLPlugin *ME;
static void *(*det)(void *self, void *key, const void *mi);
static void *(*str_new)(const char *); static void *(*dom_get)(void); static void *(*th_attach)(void *);

static void run_once(int diff) {
    static char helper[0x40], cs[0x28], st[0x20];
    memset(helper, 0, sizeof helper); memset(cs, 0, sizeof cs); memset(st, 0, sizeof st);
    *(void **)(helper + 0x30) = cs; *(void **)(cs + 0x20) = st; *(int32_t *)(st + 0x1C) = diff;
    void *list = det(helper, str_new("AFFECTER_Mage"), NULL);
    char out[600] = ""; size_t k = 0; int32_t n = list ? *(int32_t *)((char *)list + 0x18) : -1;
    void *arr = list ? *(void **)((char *)list + 0x10) : NULL;
    for (int32_t i = 0; arr && i < n; i++) {
        void *s = *(void **)((char *)arr + 0x20 + 16 * i);
        int32_t L = s ? *(int32_t *)((char *)s + 0x10) : 0; const uint16_t *c = s ? (const uint16_t *)((char *)s + 0x14) : NULL;
        k += (size_t)snprintf(out + k, sizeof out - k, "%s", i ? "," : "");
        for (int32_t j = 0; j < L && k + 2 < sizeof out; j++) out[k++] = (char)c[j];
        out[k] = 0;
    }
    H->log(ME, "hell-probe: CombatDifficulty %d: AFFECTER_Mage kit = [%s] (%d items)", diff, out, n);
}
#ifdef _WIN32
static DWORD WINAPI th(LPVOID a) {
#else
static void *th(void *a) {
#endif
    (void)a; char p[1200]; const char *d = getenv("DSRIG_DIR"); snprintf(p, sizeof p, "%s/hell_probe_go.txt", d ? d : ".");
    for (;;) {
        SLEEP_MS(500);
        FILE *f = fopen(p, "r"); if (!f) continue;
        int diff = -1; if (fscanf(f, "%d", &diff) != 1) diff = -1; fclose(f);
        char q[1300]; snprintf(q, sizeof q, "%s.done", p); remove(q); rename(p, q);
        if (diff < 0) continue;
        static int attached; if (!attached) { th_attach(dom_get()); attached = 1; }
        run_once(diff);
    }
    return 0;
}
DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    det = host->find_method_address("Refactor.Main.Event", "RecruitHelper", "DetermineInitialEquipments", 1);
    str_new = host->il2cpp_fn("il2cpp_string_new"); dom_get = host->il2cpp_fn("il2cpp_domain_get"); th_attach = host->il2cpp_fn("il2cpp_thread_attach");
    if (!det || !str_new || !dom_get || !th_attach) { host->log(self, "hell-probe: missing %p %p %p %p", (void *)det, (void *)str_new, (void *)dom_get, (void *)th_attach); return 1; }
#ifdef _WIN32
    CloseHandle(CreateThread(NULL, 0, th, NULL, 0, NULL));
#else
    pthread_t t; pthread_create(&t, NULL, th, NULL); pthread_detach(t);
#endif
    host->log(self, "hell-probe: waiting for hell_probe_go.txt");
    return 0;
}
