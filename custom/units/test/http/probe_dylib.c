/* probe_dylib - loaded into http_host the way the mod is loaded into the game (DYLD_INSERT_LIBRARIES into a
 * hardened-runtime process with the game's entitlements). Its constructor starts a thread that asks the wiki for one
 * short link with each Mac backend of src/wk_http.c, from that worker thread (the mu_link path) and then from the
 * main thread (a hook on the game's main thread), and prints one line per try. */
#include "wk_link.h"
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *URL = "https://dungeonsettlers.wiki/t/jEkTAd";
static void probe(const char *where) {
    char out[2000]; wk_http_probe(URL, out, sizeof out);
    for (char *l = strtok(out, "\n"); l; l = strtok(NULL, "\n")) printf("[%s] %s\n", where, l);
    /* the full path the player uses: the short link resolved and decoded */
    wk_build b[WK_MAX_UNITS]; int code; char e[400];
    int n = wkl_resolve(URL, b, WK_MAX_UNITS, 5000, &code, e, sizeof e);
    printf("[%s] wkl_resolve: %d units via %s %s\n", where, n, wk_http_backend(), n < 1 ? e : "");
    fflush(stdout);
}
static void on_main(void *u) { (void)u; probe("main thread"); exit(0); }
static void *worker(void *u) {
    (void)u;
    probe("worker thread");
    CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, ^{ on_main(NULL); });
    CFRunLoopWakeUp(CFRunLoopGetMain());
    return NULL;
}
__attribute__((constructor)) static void start(void) {
    pthread_t t; pthread_create(&t, NULL, worker, NULL); pthread_detach(t);
}
