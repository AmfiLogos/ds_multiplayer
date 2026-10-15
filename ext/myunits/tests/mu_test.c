/* My Units + link reader tests (no game): make -C ext/myunits/tests test  (NET=1 adds the live short-link checks)
 *  1. every unit of the launcher-written library (fixtures/launcher_library.json, Rust store.rs) read by the C gives the
 *     SAME decoded unit as the team link the launcher made for it (fixtures/launcher_links.json, codec::team_link)
 *  2. a team added from a link, saved, loaded back: same units (wk_hash), library.json.bak kept, launcher format
 *  3. mu_entries: copies with manifest.source "wiki" + library_id; the library file bytes unchanged
 *  4. link forms: ?team= / #team= / %-encoded / #b=~ / #b=<JSON> / bare code / short links / other hosts refused
 *  5. short-link answers: 302 Location, 200 preview page meta refresh (&amp;), other host, no /builder
 *  6. NET=1: a real short link resolves (302) and equals its long link; an unknown id = WKL_UNKNOWN */
#include "../../../src/myunits.h"
#include "../../../src/wk_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int pass, failc;
#define CHECK(c, ...) do { if (c) pass++; else { failc++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)L + 1); size_t k = fread(b, 1, (size_t)L, f); fclose(f); b[k] = 0; if (n) *n = k; return b;
}
static wk_build A[WK_MAX_UNITS], B[WK_MAX_UNITS];

static void t_launcher_fixture(const char *dir) {
    char p[600], e[400]; snprintf(p, sizeof p, "%s/launcher_library.json", dir);
    setenv("DSMP_UNITS_LIB", p, 1);
    jv *lib = mu_load(e, sizeof e); CHECK(lib, "load launcher library: %s", e); if (!lib) return;
    snprintf(p, sizeof p, "%s/launcher_links.json", dir);
    jv *links = jload(p, e, sizeof e); CHECK(links, "links: %s", e); if (!links) return;
    int same = 0;
    for (size_t i = 0; i < links->cnt; i++) {
        char id[17]; snprintf(id, sizeof id, "%s", jget(links->v[i], "id")->s);
        char *lk = jstrdup_raw(jget(links->v[i], "link")); int code;
        int n = wkl_decode_offline(lk, A, WK_MAX_UNITS, &code, e, sizeof e);
        CHECK(n == 1, "unit %s link: %d %s", id, n, e);
        int r = mu_build_of(lib, id, &B[0], e, sizeof e);
        CHECK(!r, "unit %s from the library: %s", id, e);
        if (n == 1 && !r) {
            char *ja = wk_json(A, 1), *jb = wk_json(B, 1);
            if (!strcmp(ja, jb)) same++; else printf("DIFF %s\n link: %.900s\n lib:  %.900s\n", id, ja, jb);
            free(ja); free(jb);
        }
        free(lk);
    }
    CHECK(same == (int)links->cnt, "launcher library == its links: %d of %zu", same, links->cnt);
    printf("launcher library: %d of %zu units equal to their team links; %zu teams\n", same, links->cnt, mu_team_count(lib));
    char nm[80], ids[MU_TEAM_MAX][17];
    for (size_t t = 0; t < mu_team_count(lib); t++) { int k = mu_team_at(lib, t, nm, sizeof nm, ids); CHECK(k >= 1 && k <= 8, "team %zu: %d units", t, k); }
    jfree(links); jfree(lib);
}

static void t_roundtrip(const char *tmpdir, const char *link) {
    char p[600], e[400]; snprintf(p, sizeof p, "%s/lib/library.json", tmpdir);
    setenv("DSMP_UNITS_LIB", p, 1);
    jv *lib = mu_load(e, sizeof e); CHECK(lib && mu_count(lib) == 0, "new empty library");
    int code, n = wkl_decode_offline(link, A, WK_MAX_UNITS, &code, e, sizeof e);
    CHECK(n >= 1, "link decodes: %s", e);
    char ids[MU_TEAM_MAX][17];
    CHECK(mu_add_team(lib, A, n, NULL, ids, e, sizeof e) == n, "add team: %s", e);
    CHECK(!mu_save(lib, e, sizeof e), "save: %s", e);
    jfree(lib);
    lib = mu_load(e, sizeof e); CHECK(lib && (int)mu_count(lib) == n && mu_team_count(lib) == 1, "reload: %zu units", lib ? mu_count(lib) : 0);
    if (!lib) return;
    CHECK(jstreq(jget(lib, "format"), "ds-launcher-units") && jnumv(jget(lib, "version")) == 1, "format / version");
    char nm[100], tids[MU_TEAM_MAX][17]; int k = mu_team_at(lib, 0, nm, sizeof nm, tids);
    CHECK(k == n && !strcmp(tids[0], ids[0]), "team ids");
    for (int i = 0; i < n; i++) CHECK(!mu_build_of(lib, ids[i], &B[i], e, sizeof e), "build_of: %s", e);
    CHECK(wk_hash(A, n) == wk_hash(B, n), "saved team == link team (wk_hash)");
    /* a second save keeps the first as .bak */
    CHECK(mu_add_team(lib, A, 1, "Solo", NULL, e, sizeof e) == 1 && !mu_save(lib, e, sizeof e), "second save");
    char bak[700]; snprintf(bak, sizeof bak, "%s.bak", p); struct stat st; CHECK(stat(bak, &st) == 0, ".bak kept");
    /* mu_entries: copies; the file is not touched */
    size_t n0, n1; char *before = slurp(p, &n0);
    char notes[2000]; jv *en = mu_entries(lib, ids, n, "0123456789abcdef0123456789abcdef", notes, sizeof notes, e, sizeof e);
    CHECK(en && (int)en->cnt == n, "entries: %s", e);
    for (size_t i = 0; en && i < en->cnt; i++) {
        jv *m = jget(en->v[i], "manifest");
        CHECK(jstreq(jget(m, "source"), "wiki") && jstreq(jget(m, "library_id"), ids[i]), "manifest source/library_id");
        CHECK(jstreq(jget(m, "home_campaign_guid"), "00000000000000000000000000000000") || (jget(m, "home_campaign_guid") && strspn(jget(m, "home_campaign_guid")->s, "0-") == jget(m, "home_campaign_guid")->n), "zero home campaign");
    }
    char *after = slurp(p, &n1);
    CHECK(before && after && n0 == n1 && !memcmp(before, after, n0), "library file unchanged by mu_entries");
    free(before); free(after); jfree(en); jfree(lib);
    /* damaged file: an error, never an empty overwrite */
    FILE *f = fopen(p, "wb"); fputs("{\"format\":\"ds-launcher-units\",\"version\":1,\"units\":[{", f); fclose(f);
    lib = mu_load(e, sizeof e); CHECK(!lib && strstr(e, "damaged"), "damaged file refused: %s", e); jfree(lib);
    f = fopen(p, "wb"); fputs("{\"format\":\"ds-launcher-units\",\"version\":2,\"units\":[]}", f); fclose(f);
    lib = mu_load(e, sizeof e); CHECK(!lib && strstr(e, "newer"), "newer version refused: %s", e); jfree(lib);
}

static void t_forms(const char *link) {
    char e[400], u[300]; int code;
    const char *code_s = strstr(link, "team=") + 5;
    int n0 = wkl_decode_offline(link, A, WK_MAX_UNITS, &code, e, sizeof e); uint64_t h0 = wk_hash(A, n0);
    char t[16384];
    snprintf(t, sizeof t, "https://dungeonsettlers.wiki/fr/builder#team=%s", code_s);
    CHECK(wkl_decode_offline(t, B, WK_MAX_UNITS, &code, e, sizeof e) == n0 && wk_hash(B, n0) == h0, "#team= form");
    snprintf(t, sizeof t, "  %s  \n", code_s);
    CHECK(wkl_decode_offline(t, B, WK_MAX_UNITS, &code, e, sizeof e) == n0 && wk_hash(B, n0) == h0, "bare code");
    snprintf(t, sizeof t, "look: https://dungeonsettlers.wiki/builder?sl=abc&team=%s&g=1 nice", code_s);
    CHECK(wkl_decode_offline(t, B, WK_MAX_UNITS, &code, e, sizeof e) == n0 && wk_hash(B, n0) == h0, "&team= in a sentence");
    /* #b=<JSON>: the full single-build link (builder.js b64enc) */
    char *js = wk_json(A, 1); size_t L = strlen(js); js[L - 1] = 0;                          /* "[{...}]" -> "{...}" */
    static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const unsigned char *s = (const unsigned char *)js + 1; size_t sl = strlen((const char *)s), k = 0; char *b64 = malloc(sl * 2 + 8);
    for (size_t i = 0; i < sl; i += 3) {
        uint32_t v = s[i] << 16 | (i + 1 < sl ? s[i + 1] << 8 : 0) | (i + 2 < sl ? s[i + 2] : 0);
        b64[k++] = T[v >> 18 & 63]; b64[k++] = T[v >> 12 & 63]; if (i + 1 < sl) b64[k++] = T[v >> 6 & 63]; if (i + 2 < sl) b64[k++] = T[v & 63];
    }
    b64[k] = 0;
    snprintf(t, sizeof t, "https://dungeonsettlers.wiki/builder#b=%s", b64);
    wk_build one[1]; wkl_decode_offline(link, A, WK_MAX_UNITS, &code, e, sizeof e);
    int n = wkl_decode_offline(t, one, 1, &code, e, sizeof e);
    CHECK(n == 1 && wk_hash(one, 1) == wk_hash(A, 1), "#b=<JSON> = the same unit (%s)", e);
    free(js); free(b64);
    /* short link URLs */
    CHECK(wkl_short_url("https://dungeonsettlers.wiki/t/RzTjHe", u, sizeof u) && !strcmp(u, "https://dungeonsettlers.wiki/t/RzTjHe"), "short /t/");
    CHECK(wkl_short_url("dungeonsettlers.wiki/t/RzTjHe?l=fr", u, sizeof u) && !strcmp(u, "https://dungeonsettlers.wiki/t/RzTjHe"), "short no scheme + ?l=");
    CHECK(wkl_short_url("https://www.dungeonsettlers.wiki/fr/t/RzTjHe", u, sizeof u) && !strcmp(u, "https://dungeonsettlers.wiki/t/RzTjHe"), "short www + lang");
    CHECK(wkl_short_url("http://dungeonsettlers.wiki/build/my-cool_build", u, sizeof u) && !strcmp(u, "https://dungeonsettlers.wiki/build/my-cool_build"), "short /build/");
    CHECK(!wkl_short_url("https://evil-dungeonsettlers.wiki/t/RzTjHe", u, sizeof u), "other host refused");
    CHECK(!wkl_short_url("https://dungeonsettlers.wiki.evil.com/t/RzTjHe", u, sizeof u), "host suffix refused");
    CHECK(!wkl_short_url("https://dungeonsettlers.wiki:8080/t/RzTjHe", u, sizeof u), "port refused");
    CHECK(!wkl_short_url("https://dungeonsettlers.wiki/items/RzTjHe", u, sizeof u), "not a short path");
    CHECK(wkl_decode_offline("https://dungeonsettlers.wiki/t/RzTjHe", A, 8, &code, e, sizeof e) < 0 && code == WKL_OFFLINE && strstr(e, "needs the internet"), "offline short link: %s", e);
    CHECK(wkl_decode_offline("hello world", A, 8, &code, e, sizeof e) < 0 && code == WKL_NOT_LINK, "not a link");
    CHECK(wkl_decode_offline("", A, 8, &code, e, sizeof e) < 0 && code == WKL_EMPTY, "empty");
    CHECK(wkl_decode_offline("https://dungeonsettlers.wiki/builder?team=AAAAAAAAAAAAAAAAAAAA", A, 8, &code, e, sizeof e) < 0 && code == WKL_BAD, "damaged: %s", e);
    CHECK(wkl_looks_like_link("https://dungeonsettlers.wiki/t/abc123") && wkl_looks_like_link(link) && !wkl_looks_like_link("Hello there") && !wkl_looks_like_link(""), "looks_like_link");
}

static void t_answers(void) {
    char o[2000];
    CHECK(!wkl_target_of("https://dungeonsettlers.wiki/builder?team=ABC&sl=x", NULL, o, sizeof o) && !strcmp(o, "https://dungeonsettlers.wiki/builder?team=ABC&sl=x"), "302 absolute");
    CHECK(!wkl_target_of("/fr/builder#b=~ABC", NULL, o, sizeof o) && !strcmp(o, "https://dungeonsettlers.wiki/fr/builder#b=~ABC"), "302 relative");
    CHECK(wkl_target_of("https://evil.com/builder?team=ABC", NULL, o, sizeof o) == -1, "302 other host refused");
    CHECK(wkl_target_of("//evil.com/builder?team=ABC", NULL, o, sizeof o) == -1, "302 protocol-relative refused");
    CHECK(wkl_target_of("https://dungeonsettlers.wiki/items?x=/builder", NULL, o, sizeof o) == -1, "302 not the builder");
    const char *page = "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<title>Team &amp; co</title>\n"
                       "<meta http-equiv=\"refresh\" content=\"0; url=/builder?team=Y7q_gYVJ&amp;sl=RzTjHe\">\n<link rel=\"canonical\" href=\"x\">";
    CHECK(!wkl_target_of(NULL, page, o, sizeof o) && !strcmp(o, "https://dungeonsettlers.wiki/builder?team=Y7q_gYVJ&sl=RzTjHe"), "preview page meta refresh: %s", o);
    CHECK(wkl_target_of(NULL, "<meta http-equiv=\"refresh\" content=\"0; url=https://evil.com/builder?team=A\">", o, sizeof o) == -1, "meta other host refused");
    CHECK(wkl_target_of(NULL, "<html>nothing</html>", o, sizeof o) == -1, "no target");
}

static void t_net(void) {
    char e[400], loc[2048], u[300]; int code, st = 0;
    int n = wkl_resolve("https://dungeonsettlers.wiki/t/RzTjHe", A, WK_MAX_UNITS, 5000, &code, e, sizeof e);
    printf("net: backend %s, RzTjHe -> %d units, code %d %s\n", wk_http_backend(), n, code, e);
    CHECK(n >= 1 || code == WKL_OTHER_DATA, "real short link resolves: %s", e);
    wkl_short_url("https://dungeonsettlers.wiki/t/RzTjHe", u, sizeof u);
    CHECK(!wk_http_get(u, 5000, &st, loc, sizeof loc, NULL, 0, e, sizeof e) && st == 302, "302: %d %s", st, e);
    if (n >= 1) {
        int m = wkl_decode_offline(loc, B, WK_MAX_UNITS, &code, e, sizeof e);
        CHECK(m == n && wk_hash(A, n) == wk_hash(B, m), "short link == its long link");
    }
    n = wkl_resolve("https://dungeonsettlers.wiki/t/zzzzzz9", A, WK_MAX_UNITS, 5000, &code, e, sizeof e);
    CHECK(n < 0 && code == WKL_UNKNOWN && strstr(e, "does not exist"), "unknown short link: %d %s", code, e);
    char pr[2000]; wk_http_probe(u, pr, sizeof pr); printf("%s", pr);
}

int main(int argc, char **argv) {
    const char *fx = argc > 1 ? argv[1] : "../fixtures", *tmp = argc > 2 ? argv[2] : "./tmp";
    char link[16384] = "";
    { char p[600]; snprintf(p, sizeof p, "%s/launcher_links.json", fx); char e[200]; jv *l = jload(p, e, sizeof e);
      /* a 3-unit team link: three fixture links' codes would need re-encoding; the first link is a 1-unit team */
      if (l && l->cnt) { char *s = jstrdup_raw(jget(l->v[0], "link")); snprintf(link, sizeof link, "%s", s); free(s); } jfree(l); }
    const char *team3 = getenv("MU_TEAM_LINK");                      /* a multi-unit team link (run.sh makes one) */
    t_launcher_fixture(fx);
    t_roundtrip(tmp, team3 && *team3 ? team3 : link);
    t_forms(team3 && *team3 ? team3 : link);
    t_answers();
    if (getenv("NET")) t_net();
    printf("%d PASS, %d FAIL\n", pass, failc);
    return failc ? 1 : 0;
}
