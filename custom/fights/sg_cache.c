/* sg_cache - see sg_cache.h */
#include "sg_cache.h"
#include "cm_http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define mkdir_one(p) _mkdir(p)
#else
#include <unistd.h>
#define mkdir_one(p) mkdir(p, 0755)
#endif

static const char *SCRIPTS[3] = { "data.js", "builder.js", "savegen.js" };
#define SCRIPT_MAX ((size_t)16 << 20)
#define TEMPLATE_MAX ((size_t)64 << 20)

void sgc_init(sgc *c, const char *root) {
    memset(c, 0, sizeof *c);
    snprintf(c->root, sizeof c->root, "%s", root);
    c->timeout_ms = 30000;
}

void sgc_default_root(const char *pdp, char *out, size_t n) {
#ifdef _WIN32
    snprintf(out, n, "%s\\Mods\\custom-modes\\wiki", pdp);
#else
    snprintf(out, n, "%s/Mods/custom-modes/wiki", pdp);
#endif
}

static void mkdirs(const char *path) {
    char p[1100]; snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++) if (*s == '/' || *s == '\\') { char k = *s; *s = 0; mkdir_one(p); *s = k; }
    mkdir_one(p);
}

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = L >= 0 ? malloc((size_t)L + 1) : NULL;
    if (b && fread(b, 1, (size_t)L, f) != (size_t)L) { free(b); b = NULL; }
    if (b) { b[L] = 0; if (n) *n = (size_t)L; }
    fclose(f);
    return b;
}

/* write to <path>.part then rename over <path> */
static int put_file(const char *path, const char *data, size_t n) {
    char tmp[1100]; snprintf(tmp, sizeof tmp, "%s.part", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, n, f) == n;
    if (fclose(f)) ok = 0;
    if (!ok) { remove(tmp); return -1; }
#ifdef _WIN32
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) { remove(tmp); return -1; }
#else
    if (rename(tmp, path)) { remove(tmp); return -1; }
#endif
    return 0;
}

static int find_tag(const char *s, size_t n, const char *key, char quote, char *out, size_t on) {
    size_t kl = strlen(key);
    for (size_t i = 0; i + kl < n; i++) {
        if (memcmp(s + i, key, kl)) continue;
        size_t j = i + kl, k = 0;
        while (j < n && s[j] != quote && isdigit((unsigned char)s[j]) && k + 1 < on) out[k++] = s[j++];
        out[k] = 0;
        if (k && j < n && s[j] == quote) return 0;
    }
    return -1;
}
int sgc_tag_of_data(const char *d, size_t n, char *out, size_t on) { return find_tag(d, n, "\"savegen\":{\"tag\":\"", '"', out, on); }
int sgc_tag_of_savegen(const char *s, size_t n, char *out, size_t on) { return find_tag(s, n, "TAG: '", '\'', out, on); }

void sgc_sources_free(sg_sources *s) {
    if (!s) return;
    free((char *)s->data_js); free((char *)s->builder_js); free((char *)s->savegen_js);
    memset(s, 0, sizeof *s);
}

static int load_cached(sgc *c, sg_sources *o) {
    char p[1100];
    memset(o, 0, sizeof *o);
    snprintf(p, sizeof p, "%s/builder/%s", c->root, SCRIPTS[0]); o->data_js = slurp(p, &o->data_n);
    snprintf(p, sizeof p, "%s/builder/%s", c->root, SCRIPTS[1]); o->builder_js = slurp(p, &o->builder_n);
    snprintf(p, sizeof p, "%s/builder/%s", c->root, SCRIPTS[2]); o->savegen_js = slurp(p, &o->savegen_n);
    if (!o->data_js || !o->builder_js || !o->savegen_js) { sgc_sources_free(o); return -1; }
    return 0;
}

/* all three or nothing; the tags of data.js and savegen.js must agree (one deploy) */
static int download_set(sgc *c, char *err, size_t errn) {
    cmh_resp r[3]; memset(r, 0, sizeof r);
    int rc = -1;
    for (int i = 0; i < 3; i++) {
        char url[512]; snprintf(url, sizeof url, CM_WIKI "/builder/%s", SCRIPTS[i]);
        if (cmh_get(url, c->timeout_ms, SCRIPT_MAX, &r[i], err, errn)) goto out;
        if (r[i].status != 200 || r[i].n < 1000) { snprintf(err, errn, "the wiki answered %d for %s", r[i].status, SCRIPTS[i]); goto out; }
    }
    char t1[32], t2[32];
    if (sgc_tag_of_data(r[0].body, r[0].n, t1, sizeof t1) || sgc_tag_of_savegen(r[2].body, r[2].n, t2, sizeof t2)) {
        snprintf(err, errn, "the wiki's builder files do not carry a build tag"); goto out;
    }
    if (strcmp(t1, t2)) { snprintf(err, errn, "the wiki is between two updates (data %s, generator %s); try again in a minute", t1, t2); goto out; }
    char dir[1100]; snprintf(dir, sizeof dir, "%s/builder", c->root); mkdirs(dir);
    for (int i = 0; i < 3; i++) {
        char p[1100]; snprintf(p, sizeof p, "%s/%s", dir, SCRIPTS[i]);
        if (put_file(p, r[i].body, r[i].n)) { snprintf(err, errn, "could not write %s (%s)", p, strerror(errno)); goto out; }
    }
    rc = 0;
out:
    for (int i = 0; i < 3; i++) cmh_free(&r[i]);
    return rc;
}

int sgc_scripts(sgc *c, long max_age_s, sg_sources *out, char *status, size_t sn, char *err, size_t errn) {
    char p[1100], e2[300] = "";
    snprintf(p, sizeof p, "%s/builder/%s", c->root, SCRIPTS[0]);
    struct stat st;
    int have = stat(p, &st) == 0;
    long age = have ? (long)(time(NULL) - st.st_mtime) : -1;
    int want = !have || (max_age_s >= 0 && age > max_age_s);
    const char *what = "cached";
    if (want) {
        if (download_set(c, e2, sizeof e2) == 0) what = "downloaded";
        else if (!have) { snprintf(err, errn, "Test Fights needs the wiki's builder files once: %s", e2); return -1; }
        else what = "offline";
    }
    if (load_cached(c, out)) {
        /* a damaged set: one fresh try */
        if (download_set(c, e2, sizeof e2) || load_cached(c, out)) { snprintf(err, errn, "the cached wiki files are unreadable: %s", e2); return -1; }
        what = "downloaded";
    }
    if (status && sn) {
        if (!strcmp(what, "offline")) {
            char d[32]; struct tm tmv; time_t mt = st.st_mtime;
#ifdef _WIN32
            localtime_s(&tmv, &mt);
#else
            localtime_r(&mt, &tmv);
#endif
            strftime(d, sizeof d, "%Y-%m-%d", &tmv);
            snprintf(status, sn, "offline: using the wiki files from %s (%s)", d, e2);
        } else snprintf(status, sn, "%s", what);
    }
    return 0;
}

/* "<lowercase/_ name>_<digits>.json" only: the name goes into a path and a URL */
static int good_template_name(const char *f) {
    const char *u = strrchr(f, '_'), *dot = strrchr(f, '.');
    if (!u || !dot || dot < u || strcmp(dot, ".json") || u == f) return 0;
    for (const char *s = f; s < u; s++) if (!(islower((unsigned char)*s) || *s == '_')) return 0;
    for (const char *s = u + 1; s < dot; s++) if (!isdigit((unsigned char)*s)) return 0;
    return dot > u + 1;
}
/* a complete JSON object (a cut-off download ends mid-file) */
static int looks_complete(const char *s, size_t n) {
    size_t a = 0; while (a < n && isspace((unsigned char)s[a])) a++;
    while (n > a && isspace((unsigned char)s[n - 1])) n--;
    return n > a + 1 && s[a] == '{' && s[n - 1] == '}';
}

int sgc_template(void *user, const char *file, char **text, size_t *len, char *err, size_t errn) {
    sgc *c = user;
    *text = NULL; *len = 0;
    if (!good_template_name(file)) { snprintf(err, errn, "bad template name %.80s", file); return -1; }
    char dir[1100], p[1200];
    snprintf(dir, sizeof dir, "%s/saves", c->root);
    snprintf(p, sizeof p, "%s/%s", dir, file);
    size_t n = 0; char *t = slurp(p, &n);
    if (t && looks_complete(t, n)) { *text = t; *len = n; return 0; }
    free(t);
    char url[512]; snprintf(url, sizeof url, CM_WIKI "/assets/saves/%s", file);
    cmh_resp r;
    if (cmh_get(url, c->timeout_ms * 4, TEMPLATE_MAX, &r, err, errn)) return -1;
    if (r.status != 200 || !looks_complete(r.body, r.n)) {
        snprintf(err, errn, r.status == 200 ? "the save template %s arrived incomplete" : "the wiki has no save template %s (%d)", file, r.status);
        cmh_free(&r); return -1;
    }
    mkdirs(dir);
    put_file(p, r.body, r.n);          /* a cache that cannot be written still serves this run */
    *text = r.body; *len = r.n;
    return 0;
}
