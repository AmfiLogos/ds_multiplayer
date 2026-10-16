/* card_tool - for test/builder_vs_c.js: one link per stdin line (any form wkl_decode_offline takes) ->
 * one JSON line {"n":units,"dec":[wk_json units],"cards":[mu_card_json ...]} or {"err":"...","code":WKL_*}. */
#include "mu_card.h"
#include "wk_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    static char line[1 << 20];
    static wk_build b[WK_MAX_UNITS];
    while (fgets(line, sizeof line, stdin)) {
        size_t L = strlen(line); while (L && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = 0;
        char err[400]; int code = 0;
        int n = wkl_decode_offline(line, b, WK_MAX_UNITS, &code, err, sizeof err);
        if (n < 1) {
            printf("{\"err\":\"");
            for (char *p = err; *p; p++) { if (*p == '"' || *p == '\\') putchar('\\'); putchar(*p); }
            printf("\",\"code\":%d}\n", code);
            continue;
        }
        char *dj = wk_json(b, n);
        printf("{\"n\":%d,\"dec\":%s,\"cards\":[", n, dj); free(dj);
        for (int i = 0; i < n; i++) { mu_card c; mu_card_of(&b[i], &c); char *cj = mu_card_json(&c); printf("%s%s", i ? "," : "", cj); free(cj); }
        printf("]}\n");
    }
    return 0;
}
