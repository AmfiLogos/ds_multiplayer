/* one-decoder check: lane units' wk_link (wkl_decode_offline + wk_json) vs the wiki's own decoder (sg_decode_blob output
 * from run_eq, build_c4/eq_ref/decode_<id>.json). Prints <id>\t<n units>\t<json> per gallery blob. */
#include "wk_link.h"
#include "fe/fe_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    char e[300]; jv *b = jload(argv[1], e, sizeof e); if (!b) { fprintf(stderr, "%s\n", e); return 1; }
    for (size_t i = 0; i < b->cnt; i++) {
        char *blob = jstrdup_raw(b->v[i]), t[8192];
        if (!strncmp(blob, "b:", 2)) snprintf(t, sizeof t, "#b=%s", blob + 2); else snprintf(t, sizeof t, "%s", blob);
        static wk_build bs[WK_MAX_UNITS]; int code = 0;
        int n = wkl_decode_offline(t, bs, WK_MAX_UNITS, &code, e, sizeof e);
        char *j = n > 0 ? wk_json(bs, n) : NULL;
        printf("%.*s\t%d\t%s\n", (int)strlen(b->k[i]), b->k[i], n, j ? j : e);
        free(j); free(blob);
    }
    return 0;
}
