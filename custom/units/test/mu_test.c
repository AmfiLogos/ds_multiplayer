/* My Units + link reader + card tests (no game): make -C custom/units/test test  (NET=1 adds the live short-link checks)
 *  1. every unit of the launcher-written library (data/launcher_library.json, Rust store.rs) read by the C gives the
 *     SAME decoded unit as the team link the launcher made for it (data/launcher_links.json, codec::team_link)
 *  2. a team added from a link, saved, loaded back: same units (wk_hash), library.json.bak kept, launcher format
 *  3. mu_entries: copies with manifest.source "wiki" + library_id; the library file bytes unchanged
 *  4. link forms: ?team= / #team= / %-encoded / #b=~ / #b=<JSON> / bare code / short links / other hosts refused
 *  5. short-link answers: 302 Location, 200 preview page meta refresh (&amp;), other host, no /builder
 *  6. NET=1: a real short link resolves (302) and equals its long link; an unknown id = WKL_UNKNOWN; preview-card
 *     pages (200 + meta refresh) resolve; a dead address gives up within the 5 s limit
 *  7. the My Units tab's store operations: add one build, rename, delete (teams follow), Save as team, delete a team
 *  8. one click (mu_link): a link on the clipboard (a private test pasteboard, never the player's) is added as a team
 *     without a box; no link = the box; the box's text; NET=1: a short link on the clipboard
 *  9. folding: the unit writer (pvp_build.c pb_folded_skill) folds exactly the builder's conditional passives
 * 10. the card of a library unit (mu_build_of -> mu_card_of) */
#include "myunits.h"
#include "wk_link.h"
#include "mu_link.h"
#include "mu_card.h"
#include "pvp_build.h"
#include "mu_card_tables.h"
#include <time.h>
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
    struct stat st0;
    if (stat(p, &st0)) { printf("SKIP launcher fixture (%s missing: cargo run -p ds-unit-builder --example myunits_fixture in ~/ds_launcher)\n", p); return; }
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
    /* preview-card pages (200 + meta refresh), /build/<id>, a language prefix */
    const char *ids[] = { "jEkTAd", "NNUGfJ", "rey8Rw", "txg3sz" };
    for (int i = 0; i < 4; i++) {
        char l[200]; snprintf(l, sizeof l, i % 2 ? "https://dungeonsettlers.wiki/fr/build/%s" : "dungeonsettlers.wiki/t/%s", ids[i]);
        st = 0; wkl_short_url(l, u, sizeof u); wk_http_get(u, 5000, &st, loc, sizeof loc, NULL, 0, e, sizeof e);
        int m = wkl_resolve(l, A, WK_MAX_UNITS, 5000, &code, e, sizeof e);
        printf("net: %s -> HTTP %d, %d units %s\n", l, st, m, m < 1 ? e : "");
        CHECK(m >= 1 || code == WKL_OTHER_DATA, "short link %s: %s", l, e);
    }
    CHECK(wkl_resolve("https://dungeonsettlers.wiki/build/zzzzzz9", A, WK_MAX_UNITS, 5000, &code, e, sizeof e) < 0 && code == WKL_UNKNOWN, "unknown /build/ link");
    wkl_short_url("https://dungeonsettlers.wiki/t/RzTjHe", u, sizeof u);
    char pr[2000]; wk_http_probe(u, pr, sizeof pr); printf("%s", pr);
}

static void t_store(const char *tmpdir, const char *link) {
    char p[600], e[400]; snprintf(p, sizeof p, "%s/lib_store/library.json", tmpdir);
    setenv("DSMP_UNITS_LIB", p, 1);
    jv *lib = mu_load(e, sizeof e); CHECK(lib && !mu_count(lib), "store: empty library"); if (!lib) return;
    int code, n = wkl_decode_offline(link, A, WK_MAX_UNITS, &code, e, sizeof e);
    CHECK(n >= 2, "store: a team link of 2+ units (%d) %s", n, e); if (n < 2) { jfree(lib); return; }
    char ids[MU_TEAM_MAX][17];
    CHECK(mu_add_team(lib, A, n, "Linked", ids, e, sizeof e) == n, "store: add team");
    /* one build (the Unit Builder's / Gallery's Save to My Units): the builder JSON of unit 0 */
    char *js = wk_json(A, 1); char pe[200]; jv *arr = jparse(js, strlen(js), pe, sizeof pe); free(js);
    char one[17];
    CHECK(arr && !mu_add_build(lib, arr->v[0], "Built", one, e, sizeof e), "store: add build: %s", e);
    CHECK(mu_count(lib) == (size_t)n + 1, "store: count after add build");
    CHECK(!mu_build_of(lib, one, &B[0], e, sizeof e) && wk_hash(&B[0], 1) == wk_hash(&A[0], 1) || !strcmp(B[0].name, "Built"), "store: added build = the unit");
    const char *bj = "{\"race\":\"AFFECTER_Nope\",\"lvl\":3}"; jv *bad = jparse(bj, strlen(bj), pe, sizeof pe); e[0] = 0;
    CHECK(bad && mu_add_build(lib, bad, NULL, NULL, e, sizeof e) < 0 && strstr(e, "not in this game version"), "store: unknown race refused: %s", e);
    jfree(bad); jfree(arr);
    CHECK(!mu_rename(lib, one, "Renamed \"x\""), "store: rename");
    mu_info inf; int found = 0;
    for (size_t i = 0; i < mu_count(lib); i++) if (!mu_info_at(lib, i, &inf) && !strcmp(inf.id, one)) found = strstr(inf.name, "Renamed") && strstr(inf.name, "x") && !strchr(inf.name, '"');
    CHECK(found, "store: renamed name shown (quotes as spaces)");
    char two[2][17]; memcpy(two[0], ids[0], 17); memcpy(two[1], one, 17); char tid[17];
    CHECK(!mu_team_new(lib, NULL, two, 2, tid, e, sizeof e) && mu_team_count(lib) == 2, "store: save as team: %s", e);
    char nm[200], tids[MU_TEAM_MAX][17];
    CHECK(mu_team_at(lib, 1, nm, sizeof nm, tids) == 2 && strstr(nm, "Renamed"), "store: team auto name: %s", nm);
    char got[17]; CHECK(!mu_team_id_at(lib, 1, got) && !strcmp(got, tid), "store: team id");
    char bogus[1][17] = { "ffffffffffffffff" };
    CHECK(mu_team_new(lib, "x", bogus, 1, NULL, e, sizeof e) < 0, "store: team of a missing unit refused");
    /* delete: the unit leaves every team; a team left empty goes */
    CHECK(!mu_delete(lib, one) && mu_count(lib) == (size_t)n, "store: delete");
    CHECK(mu_team_count(lib) == 2 && mu_team_at(lib, 1, nm, sizeof nm, tids) == 1, "store: the team lost the unit");
    /* deleting ids[0] empties the second team (it held ids[0] and the deleted one): that team goes */
    CHECK(!mu_delete(lib, ids[0]) && mu_team_count(lib) == 1, "store: delete a second; the emptied team goes (%zu teams)", mu_team_count(lib));
    CHECK(mu_team_at(lib, 0, nm, sizeof nm, tids) == n - 1, "store: first team now %d", n - 1);
    CHECK(mu_delete(lib, "ffffffffffffffff") == -1 && mu_team_delete(lib, tid) == -1, "store: unknown unit / team = -1");
    char t0[17]; mu_team_id_at(lib, 0, t0);
    CHECK(!mu_team_delete(lib, t0) && mu_team_count(lib) == 0, "store: team delete");
    CHECK(mu_count(lib) == (size_t)n - 1, "store: team delete keeps the units");
    CHECK(!mu_save(lib, e, sizeof e), "store: save: %s", e);
    jfree(lib);
}

static int wait_done(mul_result *r, int ms) {
    for (int t = 0; t < ms / 10; t++) { int s = mul_poll(r); if (s != MUL_BUSY) return s; struct timespec ts = { 0, 10000000 }; nanosleep(&ts, NULL); }
    return mul_poll(r);
}
static void t_click(const char *tmpdir, const char *link) {
    char p[600], pb[80]; snprintf(p, sizeof p, "%s/lib_click/library.json", tmpdir);
    setenv("DSMP_UNITS_LIB", p, 1);
    snprintf(pb, sizeof pb, "dsmp.mu_test.%d", (int)getpid()); setenv("DSMP_CLIP_PB", pb, 1);
    mul_result r; char e[400], clip[200];
    if (mu_clip_test_put("hello there")) { printf("SKIP click (no pasteboard: AppKit not loaded)\n"); return; }
    CHECK(!mu_clip_text(clip, sizeof clip) && !strcmp(clip, "hello there"), "clip: read back the test pasteboard: %s", clip);
    CHECK(mul_click() == MUL_NEED_BOX && mul_poll(&r) == MUL_NEED_BOX && r.msg[0], "click without a link -> the box");
    CHECK(mul_submit("still not a link") == MUL_NEED_BOX, "box with no link -> the box again");
    mul_reset();
    mu_clip_test_put(link);
    CHECK(mul_click() == MUL_BUSY, "click with a link -> busy");
    int s = wait_done(&r, 3000);
    CHECK(s == MUL_DONE && r.n >= 1 && r.team_id[0] && strstr(r.msg, "Added"), "click -> added: %d %s", s, r.msg);
    jv *lib = mu_load(e, sizeof e);
    CHECK(lib && (int)mu_count(lib) == r.n && mu_team_count(lib) == 1, "click: saved in My Units");
    jfree(lib);
    CHECK(mul_click() == MUL_BUSY || mul_poll(&r) == MUL_DONE, "click again");
    s = wait_done(&r, 3000); CHECK(s == MUL_DONE, "second click adds a second team");
    mul_reset();
    CHECK(mul_submit(link) == MUL_BUSY && wait_done(&r, 3000) == MUL_DONE, "box text -> added");
    mul_reset();
    mu_clip_test_put("https://dungeonsettlers.wiki/builder?team=AAAAAAAAAAAAAAAAAAAA");
    CHECK(mul_click() == MUL_BUSY && wait_done(&r, 3000) == MUL_FAILED && r.code == WKL_BAD, "damaged link -> failed with the reason: %s", r.msg);
    mul_reset();
    if (getenv("NET")) {
        mu_clip_test_put("Look at this team https://dungeonsettlers.wiki/t/RzTjHe");
        struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
        CHECK(mul_click() == MUL_BUSY, "short link click -> busy (the frame does not wait)");
        clock_gettime(CLOCK_MONOTONIC, &b);
        double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
        CHECK(ms < 50, "the click returned in %.1f ms", ms);
        s = wait_done(&r, 7000);
        CHECK(s == MUL_DONE || (s == MUL_FAILED && r.code == WKL_OTHER_DATA), "short link on the clipboard -> %d %s", s, r.msg);
        mul_reset();
    }
    mu_clip_test_release();
    unsetenv("DSMP_CLIP_PB");
}

static void t_fold(void) {
    int n = 0, same = 0;
    for (int k = 0; k < MUC_SK_N; k++) {
        if (!MUC_PG[k]) continue;
        int nthr = MUC_PG_THR_AT[k + 1] - MUC_PG_THR_AT[k], builder = MUC_PG_COND[k] && !nthr;
        char st[80]; int writer = pb_folded_skill(MUC_SK[k], st, sizeof st);
        n++; if (builder == writer) same++; else printf("FOLD %s: builder %d, unit writer %d\n", MUC_SK[k], builder, writer);
    }
    CHECK(n > 0 && same == n, "folding: unit writer == builder on %d of %d passive grants", same, n);
}

static void t_card_lib(const char *tmpdir) {
    char p[600], e[400]; snprintf(p, sizeof p, "%s/lib_click/library.json", tmpdir); setenv("DSMP_UNITS_LIB", p, 1);
    jv *lib = mu_load(e, sizeof e); if (!lib || !mu_count(lib)) { jfree(lib); return; }
    mu_info inf; mu_info_at(lib, 0, &inf); mu_card c;
    CHECK(!mu_build_of(lib, inf.id, &B[0], e, sizeof e) && !mu_card_of(&B[0], &c) && c.race && c.avatar, "card of a library unit: %s", e);
    char *j = mu_card_json(&c); char pe[200]; jv *v = jparse(j, strlen(j), pe, sizeof pe);
    CHECK(v && jget(v, "eq") && jget(v, "bar") && jget(v, "insc") && jget(v, "passives"), "card JSON parses: %s", pe);
    jfree(v); free(j); jfree(lib);
}

static void t_timeout(void) {
    /* a dead address (TEST-NET-1, never routed): the GET gives up within the 5 s limit */
    char e[300]; int st = 0; struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    int rc = wk_http_get("https://192.0.2.1/t/abc", 5000, &st, NULL, 0, NULL, 0, e, sizeof e);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double s = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
    printf("dead address: rc %d after %.2f s (%s, %s)\n", rc, s, wk_http_backend(), e);
    CHECK(rc < 0 && s < 6.0, "dead address gives up in %.2f s", s);
}

int main(int argc, char **argv) {
    const char *fx = argc > 1 ? argv[1] : "../data", *tmp = argc > 2 ? argv[2] : "./tmp";
    char link[16384] = "";
    { char p[600]; snprintf(p, sizeof p, "%s/launcher_links.json", fx); char e[200]; jv *l = jload(p, e, sizeof e);
      /* a 3-unit team link: three fixture links' codes would need re-encoding; the first link is a 1-unit team */
      if (l && l->cnt) { char *s = jstrdup_raw(jget(l->v[0], "link")); snprintf(link, sizeof link, "%s", s); free(s); } jfree(l); }
    const char *team3 = getenv("MU_TEAM_LINK");                      /* a multi-unit team link (run.sh makes one) */
    t_launcher_fixture(fx);
    t_roundtrip(tmp, team3 && *team3 ? team3 : link);
    t_forms(team3 && *team3 ? team3 : link);
    t_answers();
    t_store(tmp, team3 && *team3 ? team3 : link);
    t_click(tmp, team3 && *team3 ? team3 : link);
    t_fold();
    t_card_lib(tmp);
    if (getenv("NET")) { t_net(); t_timeout(); }
    printf("%d PASS, %d FAIL\n", pass, failc);
    return failc ? 1 : 0;
}
