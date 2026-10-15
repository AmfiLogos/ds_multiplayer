/* fe_cli: the C library from the command line, for tests (tests/test_c.py). Exit 0 ok, 2 refused / error. */
#include "../fe_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jv *L(const char *p) {
    if (!strcmp(p, "-")) return NULL;
    char e[300]; jv *v = jload(p, e, sizeof e);
    if (!v) { fprintf(stderr, "load %s: %s\n", p, e); exit(3); }
    return v;
}
static void W(const jv *v, const char *p) {
    size_t n; char *s = jdump(v, 1, &n); FILE *f = fopen(p, "wb"); fwrite(s, 1, n, f); fclose(f); free(s);
}
int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *c = argv[1]; char err[600] = "";
    if (getenv("FE_SEED")) fe_guid_seed(strtoull(getenv("FE_SEED"), NULL, 10));
    if (!strcmp(c, "roundtrip") && argc == 4) { jv *s = L(argv[2]); int r = jsave_new(s, argv[3]); jfree(s); return r ? 2 : 0; }
    if (!strcmp(c, "export") && argc >= 5) {         /* export save uid out guid... */
        jv *s = L(argv[2]), *a = jarr();
        for (int i = 5; i < argc; i++) { jv *e = fe_export_entry(s, argv[i], argv[3], err, sizeof err); if (!e) { fprintf(stderr, "%s\n", err); return 2; } jpush(a, e); }
        W(a, argv[4]); jfree(a); jfree(s); return 0;
    }
    if (!strcmp(c, "hub") && argc == 7) {            /* hub host entries run_id out_hub out_run */
        jv *h = L(argv[2]), *e = L(argv[3]), *run = NULL;
        jv *maps = fe_hub_maps(e);
        jv *hub = fe_build_hub(h, e, argv[4], maps, &run, err, sizeof err);
        if (!hub) { fprintf(stderr, "%s\n", err); return 2; }
        if (jsave_new(hub, argv[5])) { fprintf(stderr, "exists\n"); return 2; }
        W(run, argv[6]); jfree(hub); jfree(run); jfree(h); jfree(e); jfree(maps); return 0;
    }
    if (!strcmp(c, "extract") && argc == 9) {        /* extract hub_end run uid dp,xp,ib loot last_alive out */
        jv *h = L(argv[2]), *run = L(argv[3]), *loot = L(argv[6]), *la = L(argv[7]);
        fe_rules r = { argv[5][0] == '1', argv[5][2] == '1', argv[5][4] == '1' };
        jv *res = fe_extract_results(h, run, r, argv[4], loot, la, err, sizeof err);
        if (!res) { fprintf(stderr, "%s\n", err); return 2; }
        W(res, argv[8]); return 0;
    }
    if (!strcmp(c, "apply") && argc >= 7) {          /* apply home result manifests ledger outdir [choices] */
        jv *h = L(argv[2]), *res = L(argv[3]), *m = L(argv[4]), *ch = argc > 7 ? L(argv[7]) : NULL, *rep = jarr();
        char *p = fe_apply_results(h, res, m, argv[5], argv[6], ch, rep, err, sizeof err);
        if (!p) { printf("REFUSED %s\n", err); return 2; }
        printf("%s\n", p);
        for (size_t i = 0; i < rep->cnt; i++) printf("  %s\n", rep->v[i]->s);
        return 0;
    }
    if (!strcmp(c, "custom") && argc == 3) {         /* custom manifests.json: "<export_id> 0|1" per manifest (fe_manifest_custom) */
        jv *m = L(argv[2]);
        for (size_t i = 0; i < m->cnt; i++) printf("%s %d\n", m->k[i], fe_manifest_custom(m->v[i]));
        jfree(m); return 0;
    }
    if (!strcmp(c, "check") && argc >= 3) {
        jv *s = L(argv[2]), *g = argc > 3 ? L(argv[3]) : NULL; char o[2000];
        int n = fe_check_refs(s, g, o, sizeof o); printf("%d %s\n", n, o); return n ? 2 : 0;
    }
    return 1;
}
