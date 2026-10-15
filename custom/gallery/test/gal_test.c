/* gal_test - gallery URL, parser and names.
 * usage: gal_test <data dir>                            unit checks on test/data/ (committed, made-up items)
 *        gal_test --names <words.json> <page.json>...   prints every title / member / author name (gal_names.sh
 *                                                       compares them with the wiki's teamname.js on node)
 *        gal_test --live                                the real gallery (network) */
#include "../gallery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)L + 1); if (fread(b, 1, (size_t)L, f) != (size_t)L) { free(b); fclose(f); return NULL; }
    b[L] = 0; if (n) *n = (size_t)L; fclose(f); return b;
}

static void names(const char *words, int argc, char **argv) {
    size_t n; char *t = slurp(words, &n), err[300];
    gal_words *w = gal_words_parse(t, n, err, sizeof err); free(t);
    if (!w) { printf("words: %s\n", err); exit(1); }
    for (int a = 0; a < argc; a++) {
        t = slurp(argv[a], &n);
        gal_page p;
        if (!t || gal_parse_page(t, n, &p, err, sizeof err)) { printf("%s: %s\n", argv[a], err); exit(1); }
        for (int i = 0; i < p.n; i++) {
            char s[512];
            gal_item_title(&p.items[i], w, s, sizeof s); printf("%s\ttitle\t%s\n", p.items[i].id, s);
            for (int u = 0; u < p.items[i].nunits; u++) { gal_member_name(&p.items[i], u, w, s, sizeof s); printf("%s\tmember%d\t%s\n", p.items[i].id, u, s); }
            gal_author_name(&p.items[i], w, s, sizeof s); printf("%s\tauthor\t%s\n", p.items[i].id, s);
        }
        gal_page_free(&p); free(t);
    }
    gal_words_free(w);
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "--names")) { names(argv[2], argc - 3, argv + 3); return 0; }
    char url[2048], err[300];
    if (argc >= 2 && !strcmp(argv[1], "--live")) {
        gal_query q = { 0 }; gal_page p;
        CHECK(gal_fetch_page(&q, &p, err, sizeof err) == 0, "live list: %s", err);
        CHECK(p.n > 0 && p.n <= GAL_PAGE_SIZE, "live list count %d", p.n);
        gal_words *w = gal_words_fetch(err, sizeof err);
        CHECK(w != NULL, "live words: %s", err);
        if (p.n) {
            char t[512], a[512]; gal_item_title(&p.items[0], w, t, sizeof t); gal_author_name(&p.items[0], w, a, sizeof a);
            printf("live first: %s \"%s\" by \"%s\", %d units, gb %s, more %d\n", p.items[0].id, t, a, p.items[0].nunits, p.items[0].game_build, p.more);
            gal_item it; char *blob = NULL;
            CHECK(gal_fetch_item(p.items[0].id, &it, &blob, err, sizeof err) == 0 && blob && strlen(blob) > 10, "live item: %s", err);
            CHECK(!strcmp(it.id, p.items[0].id), "same id");
            free(blob);
        }
        gal_query q2 = { 0 }; q2.chars = 1; q2.recent = 1; q2.page = 1;
        gal_page p2;
        CHECK(gal_fetch_page(&q2, &p2, err, sizeof err) == 0 && p2.n > 0, "live chars page 1: %s", err);
        gal_item it; char *blob = NULL;
        CHECK(gal_fetch_item("zzzzzzzzzzzz", &it, &blob, err, sizeof err) == -1, "unknown id refused: %s", err);
        printf("live: unknown id -> \"%s\"\n", err);
        gal_page_free(&p); gal_page_free(&p2); gal_words_free(w);
        printf("%d checks, %d failed\n", checks, fails);
        return fails ? 1 : 0;
    }
    if (argc < 2) { fprintf(stderr, "usage: gal_test <data dir> | --names <words.json> <page.json>... | --live\n"); return 2; }
    const char *fx = argv[1];
    char p[1024];

    /* URLs, the way site/api/gallery.js parseList reads them */
    gal_query q = { 0 };
    CHECK(gal_list_url(&q, url, sizeof url) == 0 && !strcmp(url, "https://dungeonsettlers.wiki/api/gallery?kind=team&sort=popular"), "%s", url);
    q.chars = 1; q.recent = 1; q.page = 3;
    q.races[0] = "AFFECTER_Elf"; q.races[1] = "AFFECTER_Human"; q.nraces = 2;
    q.trees[0] = "Greatsword"; q.ntrees = 1; q.has[0] = "SKILL_ShieldUp"; q.nhas = 1; q.incomplete = 1;
    CHECK(gal_list_url(&q, url, sizeof url) == 0 && !strcmp(url, "https://dungeonsettlers.wiki/api/gallery?kind=char&sort=recent"
          "&race=AFFECTER_Elf,AFFECTER_Human&tree=Greatsword&has=SKILL_ShieldUp&complete=0&page=3"), "%s", url);
    q.page = 99; q.by = "6aa0e116bf825d40";
    CHECK(gal_list_url(&q, url, sizeof url) == 0 && strstr(url, "&by=6aa0e116bf825d40&page=20"), "%s", url);

    /* the list fixture */
    size_t n; snprintf(p, sizeof p, "%s/list_team.json", fx); char *t = slurp(p, &n);
    gal_page pg;
    CHECK(t && gal_parse_page(t, n, &pg, err, sizeof err) == 0, "parse: %s", err);
    CHECK(pg.n == 2 && pg.more == 1, "count %d more %d", pg.n, pg.more);
    gal_item *a = &pg.items[0], *b = &pg.items[1];
    CHECK(!strcmp(a->id, "TestAa") && a->team && a->opens == 12 && a->copies == 3 && a->likes == 1 && a->bookmarks == 2, "item a counts");
    CHECK(!strcmp(a->game_build, "B.0.4.29") && !strcmp(a->created, "2026-09-29T22:57:43.631Z"), "item a dates");
    CHECK(a->nunits == 2 && !strcmp(a->units[0].race, "AFFECTER_LizardMan") && !strcmp(a->units[1].race, "AFFECTER_Elf"), "team races from f.races");
    CHECK(a->units[0].lvl == 5 && a->units[0].hp == 1967 && a->units[0].ntrees == 3 && a->units[0].nskills == 2 && a->units[0].ngear == 2, "unit fields");
    CHECK(a->units[0].majors[1] == 91 && !strcmp(a->units[0].talents[1], "Genius") && !a->units[0].talents[0][0], "majors");
    CHECK(a->name_ntrees == 2 && !strcmp(a->name_noun, "fellowship") && a->complete && a->min_level == 5, "name spec");
    CHECK(!strcmp(a->au_name, "Zo\xC3\xA9 \"the tester\"") && !a->au_pseudo, "author name unescaped: %s", a->au_name);
    CHECK(b->au_pseudo && !strcmp(b->au_race, "AFFECTER_Lycan") && !strcmp(b->au_bg, "AFFECTER_Mage") && b->au_n == 52, "pseudonym");
    CHECK(!b->game_build[0] && gal_usable(b, "DS_B.0.4.29"), "no gb = usable");
    CHECK(gal_usable(a, "DS_B.0.4.29") && gal_usable(a, "B.0.4.29") && !gal_usable(a, "DS_B.0.4.30"), "gb check");

    /* names with the fixture words */
    snprintf(p, sizeof p, "%s/words.json", fx); char *wt = slurp(p, &n);
    gal_words *w = gal_words_parse(wt, n, err, sizeof err);
    CHECK(w != NULL, "words: %s", err);
    char s[512];
    gal_item_title(a, w, s, sizeof s); CHECK(!strcmp(s, "Shield & Greatsword Fellowship"), "title a: %s", s);
    gal_member_name(a, 1, w, s, sizeof s); CHECK(!strcmp(s, "Elf 2"), "member: %s", s);
    gal_author_name(a, w, s, sizeof s); CHECK(!strcmp(s, "Zo\xC3\xA9 \"the tester\""), "author a: %s", s);
    gal_item_title(b, w, s, sizeof s); CHECK(!strcmp(s, "Elf"), "title b (solo): %s", s);
    gal_member_name(b, 0, w, s, sizeof s); CHECK(!strcmp(s, "Elf"), "solo member: %s", s);
    gal_author_name(b, w, s, sizeof s); CHECK(!strcmp(s, "Lycan Mage 52"), "author b: %s", s);
    CHECK(!strcmp(gal_word(w, "bg", "AFFECTER_Mage"), "Mage"), "word of a pair");

    /* one item with its blob */
    free(t); snprintf(p, sizeof p, "%s/item_char.json", fx); t = slurp(p, &n);
    gal_item it; char *blob = NULL;
    CHECK(t && gal_parse_item(t, n, &it, &blob, err, sizeof err) == 0 && blob && !strncmp(blob, "b:eyJ", 5), "item blob: %s", err);
    CHECK(!it.team && !strcmp(it.units[0].race, "AFFECTER_Elf"), "char race from u.r");
    free(blob);
    CHECK(gal_parse_item("{}", 2, &it, &blob, err, sizeof err) == -1, "empty item refused");
    CHECK(gal_fetch_item("../x", &it, &blob, err, sizeof err) == -1, "bad id refused");

    /* the same through the network layer, from a folder (DSCM_WIKI_DIR) */
    setenv("DSCM_WIKI_DIR", fx, 1);
    gal_query q0 = { 0 }; gal_page pg2;
    CHECK(gal_fetch_page(&q0, &pg2, err, sizeof err) == 0 && pg2.n == 2, "fetch from dir: %s", err);
    gal_page_free(&pg2);
    gal_page_free(&pg); gal_words_free(w); free(wt); free(t);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
