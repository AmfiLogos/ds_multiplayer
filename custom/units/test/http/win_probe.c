/* win_probe - the Windows side of the HTTP proof and a compile check of the units code for Windows (llvm-mingw):
 * WinHTTP GET of one short link, the short link resolved and decoded, the clipboard read, and the one-click job
 * end to end (DSMP_UNITS_LIB = a scratch library). Build + run: win_run.sh. */
#include "wk_link.h"
#include "mu_link.h"
#include "mu_card.h"
#include "myunits.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

int main(int argc, char **argv) {
    const char *url = argc > 1 ? argv[1] : "https://dungeonsettlers.wiki/t/jEkTAd";
    char out[2000]; wk_http_probe(url, out, sizeof out); printf("%s", out);
    wk_build b[WK_MAX_UNITS]; int code; char e[400];
    DWORD t0 = GetTickCount();
    int n = wkl_resolve(url, b, WK_MAX_UNITS, MUL_TIMEOUT_MS, &code, e, sizeof e);
    printf("wkl_resolve: %d units via %s in %lu ms %s\n", n, wk_http_backend(), GetTickCount() - t0, n < 1 ? e : "");
    if (n >= 1) { mu_card c; mu_card_of(&b[0], &c); char *j = mu_card_json(&c); printf("card 0: %.300s\n", j); free(j); }
    char clip[300]; printf("clipboard: %s\n", mu_clip_text(clip, sizeof clip) ? "(no text)" : "text read");
    /* the one-click job on the box path (the clipboard is the player's: only read above) */
    _putenv("DSMP_UNITS_LIB=win_probe_library.json");
    int s = mul_submit(url); mul_result r;
    for (int i = 0; i < 800 && s == MUL_BUSY; i++) { Sleep(10); s = mul_poll(&r); }
    mul_poll(&r);
    printf("one click (box path): state %d, %s\n", s, r.msg);
    t0 = GetTickCount();
    int st = 0; int rc = wk_http_get("https://192.0.2.1/t/abc", 5000, &st, NULL, 0, NULL, 0, e, sizeof e);
    printf("dead address: rc %d after %lu ms (%s)\n", rc, GetTickCount() - t0, e);
    return n >= 1 && s == MUL_DONE ? 0 : 1;
}
