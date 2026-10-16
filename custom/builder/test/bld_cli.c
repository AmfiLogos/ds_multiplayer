/* bld_cli - the Unit Builder's model in QuickJS, driven the way the game window drives it: N random builds, each made by
 * clicks the window offers (only choices the state lists), then written out for the node reference (ref_node.js) and
 * checked against the C side of My Units:
 *   <out>/b<i>.json = {"build":..., "computed":..., "link":"...", "sheet":...}
 *   mu_add_build accepts every build (the Save path), and the link decodes (wk_link, the units lane's one decoder) to the
 *   same unit as the stored build (wk_hash).
 * usage: bld_cli <wiki builder dir> <out dir> <count> <seed> */
#include "bld_js.h"
#include "fe_json.h"
#include "myunits.h"
#include "wk_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long z = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)z + 1); if (!b) { fclose(f); return NULL; }
    *n = fread(b, 1, (size_t)z, f); b[*n] = 0; fclose(f); return b;
}
static unsigned rs;
static unsigned rnd(unsigned n) { rs = rs * 1103515245u + 12345u; return n ? ((rs >> 8) % n) : 0; }
static void raw_str(const jv *v, char *out, size_t n) { if (v && v->t == JN_STR) snprintf(out, n, "%.*s", (int)v->n, v->s); else if (n) out[0] = 0; }

/* one click from what the state offers; the action JSON in out */
static void pick_action(const jv *st, char *out, size_t n) {
    char k[160];
    for (int tries = 0; tries < 20; tries++) {
        switch (rnd(18)) {
        case 0: { jv *a = jget(st, "races"); if (!a || !a->cnt) break; raw_str(jget(a->v[rnd(a->cnt)], "k"), k, sizeof k); snprintf(out, n, "{\"t\":\"race\",\"k\":\"%s\"}", k); return; }
        case 1: { jv *a = jget(st, "bgs"); if (!a || !a->cnt) break; raw_str(jget(a->v[rnd(a->cnt)], "k"), k, sizeof k); snprintf(out, n, "{\"t\":\"bg\",\"k\":\"%s\"}", k); return; }
        case 2: case 3: { jv *a = jget(st, "inds"); if (!a || !a->cnt) break; raw_str(jget(a->v[rnd(a->cnt)], "k"), k, sizeof k); snprintf(out, n, "{\"t\":\"ind\",\"k\":\"%s\"}", k); return; }
        case 4: { if (rnd(3)) break;      /* a level change now and then (it reshapes everything) */
            int mx = (int)jnumv(jget(st, "maxLvl")); snprintf(out, n, "{\"t\":\"lvl\",\"v\":%d}", 1 + (int)rnd((unsigned)(mx > 0 ? mx : 12))); return; }
        case 5: { jv *a = jget(st, "treeCands"); if (!a || !a->cnt) break; raw_str(jget(a->v[rnd(a->cnt)], "k"), k, sizeof k); snprintf(out, n, "{\"t\":\"tree\",\"k\":\"%s\"}", k); return; }
        case 6: case 7: case 8: case 12: case 13: case 14: case 15: {   /* a skill node (learnable ones most of the time, like a player) */
            jv *t = jget(st, "trees"); if (!t || !t->cnt) break;
            jv *rows = jget(t->v[rnd(t->cnt)], "rows"); if (!rows || !rows->cnt) break;
            jv *row = rows->v[rnd(rows->cnt)]; if (!row || row->t != JN_ARR || !row->cnt) break;
            jv *node = row->v[rnd(row->cnt)]; if (!node || node->t != JN_OBJ) break;
            char why[200]; raw_str(jget(node, "why"), why, sizeof why);
            if (why[0] && rnd(4)) break;
            raw_str(jget(node, "k"), k, sizeof k); snprintf(out, n, "{\"t\":\"skill\",\"k\":\"%s\"}", k); return;
        }
        case 9: { jv *a = jget(st, "inscs"); if (!a || !a->cnt) break; raw_str(jget(a->v[rnd(a->cnt)], "k"), k, sizeof k); snprintf(out, n, "{\"t\":\"insc\",\"k\":\"%s\"}", k); return; }
        case 10: {
            jv *eq = jget(st, "eq"); if (!eq || !eq->cnt) break;
            jv *s = eq->v[rnd(eq->cnt)]; char slot[40]; raw_str(jget(s, "slot"), slot, sizeof slot);
            jv *it = jget(s, "items");
            if (!it || !it->cnt || !rnd(8)) { snprintf(out, n, "{\"t\":\"eq\",\"slot\":\"%s\",\"k\":null}", slot); return; }
            raw_str(jget(it->v[rnd(it->cnt)], "k"), k, sizeof k); snprintf(out, n, "{\"t\":\"eq\",\"slot\":\"%s\",\"k\":\"%s\"}", slot, k); return;
        }
        case 11: case 16: case 17: {
            jv *eq = jget(st, "eq"); if (!eq || !eq->cnt) break;
            jv *s = eq->v[rnd(eq->cnt)], *r = jget(s, "rolls"), *tg = jget(s, "tags");
            if (!r || !r->cnt || !tg || !tg->cnt) break;
            char slot[40]; raw_str(jget(s, "slot"), slot, sizeof slot);
            snprintf(out, n, "{\"t\":\"roll\",\"slot\":\"%s\",\"r\":%d,\"i\":%d}", slot, (int)rnd(r->cnt), rnd(6) ? (int)rnd(tg->cnt) : -1); return;
        }
        }
    }
    snprintf(out, n, "{\"t\":\"lvl\",\"v\":%d}", 1 + (int)rnd(12));
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: bld_cli <builder dir> <out dir> <count> <seed>\n"); return 2; }
    char p[2048]; size_t dn = 0, bn = 0, err_n = 0;
    snprintf(p, sizeof p, "%s/data.js", argv[1]); char *data = slurp(p, &dn);
    snprintf(p, sizeof p, "%s/builder.js", argv[1]); char *bjs = slurp(p, &bn);
    if (!data || !bjs) { fprintf(stderr, "cannot read %s/data.js, builder.js\n", argv[1]); return 1; }
    int count = atoi(argv[3]); rs = (unsigned)strtoul(argv[4], NULL, 10);
    char err[2000] = "";
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    bld_engine *e = bld_open(data, dn, bjs, bn, NULL, err, sizeof err);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (!e) { fprintf(stderr, "open: %s\n", err); return 1; }
    double t_open = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9, t_act = 0; long nact = 0;
    char lib_path[2100]; snprintf(lib_path, sizeof lib_path, "%s/library.json", argv[2]);
    setenv("DSMP_UNITS_LIB", lib_path, 1);
    jv *lib = mu_load(err, sizeof err);
    if (!lib) { fprintf(stderr, "mu_load: %s\n", err); return 1; }
    int bad = 0;
    for (int i = 0; i < count; i++) {
        char *st = bld_call(e, "load", "null", err, sizeof err);
        if (!st) { fprintf(stderr, "load: %s\n", err); return 1; }
        int nclick = 5 + (int)rnd(120);
        for (int c = 0; c < nclick; c++) {
            jv *s = jparse(st, strlen(st), err, sizeof err); free(st);
            if (!s) { fprintf(stderr, "state json: %s\n", err); return 1; }
            char a[400]; pick_action(s, a, sizeof a); jfree(s);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            st = bld_call(e, "act", a, err, sizeof err);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            t_act += (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9; nact++;
            if (!st) { fprintf(stderr, "act %s: %s\n", a, err); return 1; }
        }
        char *bj = bld_call(e, "build", NULL, err, sizeof err), *cj = bld_call(e, "computed", NULL, err, sizeof err);
        char *lk = bld_call(e, "link", NULL, err, sizeof err);
        if (!bj || !cj || !lk) { fprintf(stderr, "build %d: %s\n", i, err); return 1; }
        jv *s = jparse(st, strlen(st), err, sizeof err); jv *sh = s ? jget(s, "sheet") : NULL;
        char *shj = sh ? jdump(sh, 0, NULL) : NULL;
        snprintf(p, sizeof p, "%s/b%03d.json", argv[2], i);
        FILE *f = fopen(p, "wb");
        if (!f) { fprintf(stderr, "cannot write %s\n", p); return 1; }
        fprintf(f, "{\"build\":%s,\"computed\":%s,\"link\":%s,\"sheet\":%s}\n", bj, cj, lk, shj ? shj : "null");
        fclose(f);
        /* Save path: My Units takes it; the link is the same unit as the stored build */
        jv *bv = jparse(bj, strlen(bj), err, sizeof err); char id[17];
        if (!bv || mu_add_build(lib, bv, NULL, id, err, sizeof err)) { printf("SAVE REFUSED b%03d: %s\n", i, err); bad++; }
        else {
            wk_build a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
            char lks[4000]; jv *lv = jparse(lk, strlen(lk), err, sizeof err); raw_str(lv, lks, sizeof lks); jfree(lv);
            int code = 0;
            if (wk_from_json(bv, &a, err, sizeof err)) { printf("FROM_JSON b%03d: %s\n", i, err); bad++; }
            else if (wkl_decode_offline(lks, &b, 1, &code, err, sizeof err) != 1) { printf("LINK DECODE b%03d: %s\n", i, err); bad++; }
            else if (wk_hash(&a, 1) != wk_hash(&b, 1)) { printf("LINK != BUILD b%03d\n", i); bad++; }
        }
        jfree(bv); jfree(s); free(shj); free(bj); free(cj); free(lk); free(st);
    }
    if (mu_save(lib, err, sizeof err)) { printf("mu_save: %s\n", err); bad++; }
    jfree(lib);
    snprintf(p, sizeof p, "%s/timing.txt", argv[2]);
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "open %.3f s, %ld clicks, %.2f ms per click\n", t_open, nact, nact ? t_act * 1000 / nact : 0); fclose(f); }
    printf("quickjs: %d builds, %ld clicks, open %.3f s, %.2f ms per click; save/link checks failed: %d\n", count, nact, t_open, nact ? t_act * 1000 / nact : 0, bad);
    bld_close(e); free(data); free(bjs);
    (void)err_n;
    return bad ? 3 : 0;
}
