/* sg_cache_test - the download + cache rules of sg_cache, then one fight through the cache.
 * usage: sg_cache_test <scratch dir> <wiki site dir>            offline: the site folder plays the wiki (DSCM_WIKI_DIR)
 *        sg_cache_test <scratch dir> --live                       the real dungeonsettlers.wiki (network)
 * The scratch dir must be new or empty (the test makes cache roots inside it). */
#include "../sg_cache.h"
#include "../cm_http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <time.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)L + 1); if (fread(b, 1, (size_t)L, f) != (size_t)L) { free(b); fclose(f); return NULL; }
    b[L] = 0; if (n) *n = (size_t)L; fclose(f); return b;
}
static void spit(const char *path, const char *s, size_t n) { FILE *f = fopen(path, "wb"); fwrite(s, 1, n, f); fclose(f); }
static int same_file(const char *a, const char *b) {
    size_t na, nb; char *x = slurp(a, &na), *y = slurp(b, &nb);
    int r = x && y && na == nb && !memcmp(x, y, na); free(x); free(y); return r;
}
static void age_file(const char *p, long secs) { struct utimbuf u; u.actime = u.modtime = time(NULL) - secs; utime(p, &u); }

static void fight(sgc *c, const char *label) {
    sg_sources s; char st[300] = "", err[2000] = "";
    CHECK(sgc_scripts(c, -1, &s, st, sizeof st, err, sizeof err) == 0, "%s scripts: %s", label, err);
    sg_engine *e = sg_open(&s, sgc_template, c, NULL, err, sizeof err);
    CHECK(e != NULL, "%s open: %s", label, err);
    if (e) {
        char tf[128] = "";
        CHECK(sg_template_file(e, "dps", NULL, tf, sizeof tf) == 0 && strstr(tf, "boss_golem_"), "%s template name %s", label, tf);
        /* a one-unit DPS check with a default build: the wiki's own generator decides everything */
        const char *opts = "{\"mode\":\"dps\",\"builds\":[{\"name\":\"Ayla\",\"race\":\"AFFECTER_Human\",\"lvl\":5,\"offer\":\"Least\","
                           "\"roll\":\"avg\",\"bg\":\"AFFECTER_Butcher\",\"inds\":[],\"tal\":{},\"insctrees\":[],\"learned\":[],"
                           "\"insc\":[],\"souls\":[],\"acqInds\":[],\"eq\":{},\"tags\":{}}],\"team\":\"Cache test\"}";
        sg_result r;
        int rc = sg_generate(e, opts, &r, err, sizeof err);
        CHECK(rc == 0, "%s generate: %s", label, err);
        if (!rc) {
            CHECK(r.problems == 0, "%s problems %d: %s", label, r.problems, r.meta);
            CHECK(r.file_n > 1000000 && strstr(r.filename, "Cache-test_DPS Check_"), "%s file %zu %s", label, r.file_n, r.filename);
            printf("%s: %s, %zu bytes, %s\n", label, st, r.file_n, r.filename);
            sg_result_free(&r);
        }
        sg_close(e);
    }
    sgc_sources_free(&s);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: sg_cache_test <scratch dir> <wiki site dir>|--live\n"); return 2; }
    const char *scr = argv[1];
    int live = !strcmp(argv[2], "--live");
    char root[1024], p[1200], q[1200], st[300], err[2000];
    sg_sources s;
    mkdir(scr, 0755);

    if (live) {
        unsetenv("DSCM_WIKI_DIR");
        snprintf(root, sizeof root, "%s/live", scr);
        sgc c; sgc_init(&c, root);
        CHECK(sgc_scripts(&c, 86400, &s, st, sizeof st, err, sizeof err) == 0 && !strcmp(st, "downloaded"), "live download: %s %s", st, err);
        sgc_sources_free(&s);
        fight(&c, "live");
        /* every template of this build into the cache (run_eq.sh can then run on the live files) */
        CHECK(sgc_scripts(&c, -1, &s, st, sizeof st, err, sizeof err) == 0, "live scripts: %s", err);
        char tag[32] = "";
        sgc_tag_of_data(s.data_js, s.data_n, tag, sizeof tag);
        sgc_sources_free(&s);
        const char *tn[4] = { "base", "boss_golem", "boss_sentinel", "boss_abyss" };
        for (int i = 0; i < 4; i++) {
            char f[128], *text; size_t len;
            snprintf(f, sizeof f, "%s_%s.json", tn[i], tag);
            CHECK(sgc_template(&c, f, &text, &len, err, sizeof err) == 0, "live template %s: %s", f, err);
            if (text) { printf("live template %s: %zu bytes\n", f, len); free(text); }
        }
        cmh_resp r;
        CHECK(cmh_get("https://example.com/", 5000, 1000, &r, err, sizeof err) == -1 && strstr(err, "only"), "other host refused: %s", err);
        printf("%d checks, %d failed\n", checks, fails);
        return fails ? 1 : 0;
    }

    const char *site = argv[2];
    setenv("DSCM_WIKI_DIR", site, 1);
    snprintf(root, sizeof root, "%s/a", scr);
    sgc c; sgc_init(&c, root);

    /* first use downloads all three, the same bytes as the wiki serves */
    CHECK(sgc_scripts(&c, 86400, &s, st, sizeof st, err, sizeof err) == 0, "first: %s", err);
    CHECK(!strcmp(st, "downloaded"), "first status %s", st);
    sgc_sources_free(&s);
    const char *names[3] = { "data.js", "builder.js", "savegen.js" };
    for (int i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/builder/%s", root, names[i]); snprintf(q, sizeof q, "%s/builder/%s", site, names[i]);
        CHECK(same_file(p, q), "cached %s equals the wiki's", names[i]);
    }
    /* second use: from the cache */
    CHECK(sgc_scripts(&c, 86400, &s, st, sizeof st, err, sizeof err) == 0 && !strcmp(st, "cached"), "second %s", st);
    char t1[32], t2[32];
    CHECK(sgc_tag_of_data(s.data_js, s.data_n, t1, sizeof t1) == 0 && sgc_tag_of_savegen(s.savegen_js, s.savegen_n, t2, sizeof t2) == 0
          && !strcmp(t1, t2), "tags %s %s", t1, t2);
    sgc_sources_free(&s);
    /* old cache: fetched again */
    snprintf(p, sizeof p, "%s/builder/data.js", root); age_file(p, 2 * 86400);
    CHECK(sgc_scripts(&c, 86400, &s, st, sizeof st, err, sizeof err) == 0 && !strcmp(st, "downloaded"), "stale -> %s", st);
    sgc_sources_free(&s);
    /* old cache + wiki unreachable: the cached set is used, and says so */
    age_file(p, 2 * 86400);
    snprintf(q, sizeof q, "%s/empty", scr); mkdir(q, 0755);
    setenv("DSCM_WIKI_DIR", q, 1);
    CHECK(sgc_scripts(&c, 86400, &s, st, sizeof st, err, sizeof err) == 0 && !strncmp(st, "offline", 7), "offline -> %s", st);
    sgc_sources_free(&s);
    /* nothing cached + unreachable: a clear error */
    sgc c2; snprintf(p, sizeof p, "%s/b", scr); sgc_init(&c2, p);
    CHECK(sgc_scripts(&c2, 86400, &s, st, sizeof st, err, sizeof err) == -1 && strstr(err, "needs the wiki"), "none -> %s", err);
    /* a wiki between two deploys (tags differ): refused, the cache keeps the old set */
    snprintf(q, sizeof q, "%s/split", scr); mkdir(q, 0755);
    snprintf(p, sizeof p, "%s/builder", q); mkdir(p, 0755);
    for (int i = 0; i < 3; i++) {
        size_t n; snprintf(p, sizeof p, "%s/builder/%s", site, names[i]); char *b = slurp(p, &n);
        if (i == 2) { char *t = strstr(b, "TAG: '"); if (t) t[6] = t[6] == '9' ? '1' : '9'; }
        snprintf(p, sizeof p, "%s/builder/%s", q, names[i]); spit(p, b, n); free(b);
    }
    setenv("DSCM_WIKI_DIR", q, 1);
    snprintf(p, sizeof p, "%s/builder/data.js", root); age_file(p, 2 * 86400);
    CHECK(sgc_scripts(&c, 86400, &s, st, sizeof st, err, sizeof err) == 0 && !strncmp(st, "offline", 7) && strstr(st, "between two updates"), "split -> %s", st);
    snprintf(p, sizeof p, "%s/builder/savegen.js", root); snprintf(q, sizeof q, "%s/builder/savegen.js", site);
    CHECK(same_file(p, q), "split deploy did not replace the cached generator");
    sgc_sources_free(&s);
    setenv("DSCM_WIKI_DIR", site, 1);

    /* templates: names checked, downloaded once, then from the cache; a cut-off file is fetched again */
    char *text; size_t len;
    CHECK(sgc_template(&c, "../etc/passwd", &text, &len, err, sizeof err) == -1, "path refused");
    CHECK(sgc_template(&c, "boss_golem_x.json", &text, &len, err, sizeof err) == -1, "bad tag refused");
    char tf[128]; snprintf(tf, sizeof tf, "boss_golem_%s.json", t1);
    CHECK(sgc_template(&c, tf, &text, &len, err, sizeof err) == 0, "template: %s", err);
    snprintf(p, sizeof p, "%s/saves/%s", root, tf); snprintf(q, sizeof q, "%s/assets/saves/%s", site, tf);
    CHECK(same_file(p, q), "cached template equals the wiki's");
    free(text);
    spit(p, "{\"cut\":", 7);
    CHECK(sgc_template(&c, tf, &text, &len, err, sizeof err) == 0 && same_file(p, q), "cut-off template fetched again");
    free(text);
    CHECK(sgc_template(&c, "boss_golem_1.json", &text, &len, err, sizeof err) == -1 && strstr(err, "no save template"), "missing: %s", err);

    fight(&c, "offline-wiki");
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
