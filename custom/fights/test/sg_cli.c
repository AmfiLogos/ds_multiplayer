/* sg_cli - standalone harness for sg_js (no game): the same inputs and outputs as ref_node.js, so the two can be
 * compared byte for byte (run_eq.sh).
 * usage: sg_cli <builder dir> <saves dir> <out dir> <cases.json> <builds.json> [blobs.json]
 * env DET_SEED, DET_NOW, DET_JS (path of det_prelude.js; unset = no determinism, the in-game behaviour)
 * Case selection, builds and outputs: see ref_node.js. Also writes <out>/timing.txt. */
#include "../sg_js.h"
#include "../../fe/fe_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)L + 1);
    if (b && fread(b, 1, (size_t)L, f) != (size_t)L) { free(b); b = NULL; }
    if (b) { b[L] = 0; if (n) *n = (size_t)L; }
    fclose(f);
    return b;
}
static int spit(const char *dir, const char *name, const char *s, size_t n) {
    char p[2048]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    fwrite(s, 1, n, f); fclose(f);
    return 0;
}
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static const char *g_saves;
static int tpl_from_dir(void *u, const char *file, char **text, size_t *len, char *err, size_t errn) {
    (void)u;
    char p[2048]; snprintf(p, sizeof p, "%s/%s", g_saves, file);
    *text = slurp(p, len);
    if (!*text) { snprintf(err, errn, "no file %s", p); return -1; }
    return 0;
}

/* builds for a case: "<set>" or "<set>:i,j" from builds.json -> JSON array text */
static char *pick_builds(const jv *sets, const char *spec) {
    char name[128]; const char *colon = strchr(spec, ':');
    size_t k = colon ? (size_t)(colon - spec) : strlen(spec);
    if (k >= sizeof name) return NULL;
    memcpy(name, spec, k); name[k] = 0;
    const jv *all = jget(sets, name);
    if (!all || all->t != JN_ARR) return NULL;
    jv *a = jarr();
    if (!colon) { for (size_t i = 0; i < all->cnt; i++) jpush(a, jcopy(all->v[i])); }
    else for (const char *p = colon + 1; *p;) {
        long i = strtol(p, (char **)&p, 10);
        if (i >= 0 && (size_t)i < all->cnt) jpush(a, jcopy(all->v[i]));
        if (*p == ',') p++; else break;
    }
    char *o = jdump(a, 0, NULL);
    jfree(a);
    return o;
}

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: sg_cli <builder dir> <saves dir> <out dir> <cases.json> <builds.json> [blobs.json]\n"); return 2; }
    const char *dir = argv[1], *out = argv[3];
    g_saves = argv[2];
    char p[2048], err[2048] = "";
    sg_sources s = {0};
    snprintf(p, sizeof p, "%s/data.js", dir);    s.data_js = slurp(p, &s.data_n);
    snprintf(p, sizeof p, "%s/builder.js", dir); s.builder_js = slurp(p, &s.builder_n);
    snprintf(p, sizeof p, "%s/savegen.js", dir); s.savegen_js = slurp(p, &s.savegen_n);
    sg_det det = {0}, *dp = NULL;
    if (getenv("DET_JS")) {
        det.js = slurp(getenv("DET_JS"), NULL);
        det.seed = (unsigned)strtoul(getenv("DET_SEED") ? getenv("DET_SEED") : "1", NULL, 10);
        det.now_ms = strtod(getenv("DET_NOW") ? getenv("DET_NOW") : "1760000000000", NULL);
        dp = &det;
    }
    double t0 = now_s();
    sg_engine *e = sg_open(&s, tpl_from_dir, NULL, dp, err, sizeof err);
    double t_open = now_s() - t0;
    if (!e) { fprintf(stderr, "open: %s\n", err); return 1; }
    char timing[8192]; size_t tn = (size_t)snprintf(timing, sizeof timing, "open %.3f s\n", t_open);

    char *panel = sg_panel(e, err, sizeof err);
    if (!panel) { fprintf(stderr, "panel: %s\n", err); return 1; }
    spit(out, "panel.json", panel, strlen(panel)); free(panel);

    if (argc > 6) {
        jv *blobs = jload(argv[6], err, sizeof err);
        if (!blobs) { fprintf(stderr, "blobs: %s\n", err); return 1; }
        for (size_t i = 0; i < blobs->cnt; i++) {
            char *b = jstrdup_raw(blobs->v[i]);          /* base64url: no escapes */
            char *d = sg_decode_blob(e, b, err, sizeof err);
            char nm[256]; snprintf(nm, sizeof nm, "decode_%.*s.json", (int)blobs->kn[i], blobs->k[i]);
            if (d) { spit(out, nm, d, strlen(d)); free(d); } else fprintf(stderr, "decode %s: %s\n", nm, err);
            free(b);
        }
        jfree(blobs);
    }

    jv *cases = jload(argv[4], err, sizeof err), *sets = jload(argv[5], err, sizeof err);
    if (!cases || !sets) { fprintf(stderr, "cases/builds: %s\n", err); return 1; }
    int fails = 0;
    for (size_t i = 0; i < cases->cnt; i++) {
        const jv *c = cases->v[i];
        char *id = jstrdup_raw(jget(c, "id")), *spec = jstrdup_raw(jget(c, "builds"));
        char *bj = pick_builds(sets, spec);
        jv *opts = jcopy(jget(c, "opts"));
        if (!opts || !bj) { fprintf(stderr, "%s: bad case\n", id); fails++; goto next; }
        jv *bv = jparse(bj, strlen(bj), err, sizeof err);
        jset(opts, "builds", bv);
        char *oj = jdump(opts, 0, NULL);
        sg_result r;
        double t1 = now_s();
        char nm[512];
        if (sg_generate(e, oj, &r, err, sizeof err)) {
            snprintf(nm, sizeof nm, "%s.error.txt", id); spit(out, nm, err, strlen(err));
        } else {
            snprintf(nm, sizeof nm, "%s.save.json", id); spit(out, nm, r.file, r.file_n);
            /* meta as ref_node.js writes it: {filename, problems, ...} in the result's own key order */
            snprintf(nm, sizeof nm, "%s.meta.json", id); spit(out, nm, r.meta, strlen(r.meta));
            sg_result_free(&r);
        }
        tn += (size_t)snprintf(timing + tn, tn < sizeof timing ? sizeof timing - tn : 0, "%s %.3f s\n", id, now_s() - t1);
        free(oj); jfree(opts);
    next:
        free(id); free(spec); free(bj);
    }
    spit(out, "timing.txt", timing, strlen(timing));
    jfree(cases); jfree(sets);
    sg_close(e);
    return fails ? 1 : 0;
}
