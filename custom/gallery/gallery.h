/* gallery - the wiki's public build gallery, read-only, for the Mods window's Gallery tab (owner 2026-10-15:
 * "integrate the build gallery inside of the mod loader ... they can choose in the gallery build units or teams").
 * Source: dungeonsettlers.wiki/api/gallery (site/api/gallery.js):
 *   GET /api/gallery?kind=team|char&sort=popular|recent[&race=K,..][&tree=T,..][&has=K1,K2][&complete=0][&by=<aid>][&page=N]
 *       -> { items:[{ id, k, c, e?, gb?, o, cp, lk, bm, f, u, au, tags }], more }   24 per page, pages 0..20
 *   GET /api/gallery?id=<id>&full=1 -> one item + `blob` (the stored build: a team code, or "b:" + character JSON)
 * Names follow the wiki's rule (site/assets/teamname.js render / memberName / authorName) with the words of
 * /builds/words.json; nobody's typed team name is ever shown, as on the wiki.
 * Read-only: no likes, bookmarks, reports or uploads. Use = decode the blob (sg_decode_blob, the builder's own
 * decoder) -> Save to My Units / Test Fight. An item made on an older game build is used with the current data, as
 * the wiki's builder opens an old link (lead 2026-10-15); only a failed decode shows an error. */
#pragma once
#include <stddef.h>

#define GAL_PAGE_SIZE 24
#define GAL_MAX_PAGE 20
#define GAL_MAX_UNITS 8
#define GAL_KEY 48

typedef struct gal_query {
    int chars;                       /* 0 = teams (kind=team), 1 = single characters (kind=char) */
    int recent;                      /* 0 = popular, 1 = recent */
    const char *races[12]; int nraces;   /* race keys (AFFECTER_Elf), OR inside the group */
    const char *trees[30]; int ntrees;   /* weapon trees (Greatsword), OR inside the group */
    const char *has[4];    int nhas;     /* game keys the build is tagged with or contains, AND */
    int incomplete;                  /* 1 = also incomplete builds (complete=0); the wiki's default hides them */
    const char *by;                  /* one author's public id (16 hex), or NULL */
    int page;                        /* 0..GAL_MAX_PAGE */
} gal_query;

typedef struct gal_unit {
    char race[GAL_KEY], bg[GAL_KEY], portrait[64], npc[128];   /* f.races[i] (else u.r), b, p, npc icon */
    int  lvl, hp, pa, ma;
    char trees[8][32]; int ntrees;                    /* t */
    char skills[48][GAL_KEY]; int nskills;            /* s: learned skills */
    char insc[16][64]; int ninsc;                     /* i */
    char gear[16][GAL_KEY]; int ngear;                /* g: equipped items */
    int  majors[6];                                   /* m: Strength, Constitution, WillPower, Intelligence, Agility, Perception */
    char talents[6][16];                              /* mt: talent per major ("Genius"), "" = none */
} gal_unit;

typedef struct gal_item {
    char id[32];
    int  team;                                        /* k == "team" */
    char created[32], edited[32], game_build[32];     /* c, e, gb ("B.0.4.29"); "" when unknown */
    int  opens, copies, likes, bookmarks;             /* o, cp, lk, bm */
    char name_race[GAL_KEY], name_noun[24], name_trees[4][32]; int name_ntrees;   /* f.name: the name spec */
    char focus[GAL_KEY];                              /* f.focus (key stat) or "" */
    int  complete, min_level;                         /* f.complete, f.minl */
    gal_unit units[GAL_MAX_UNITS]; int nunits;        /* u */
    char au_name[96];                                 /* au.n: the name the author chose, or "" */
    char au_race[GAL_KEY], au_bg[GAL_KEY]; int au_n, au_pseudo;   /* au.p: pseudonym [race, background, n] */
    char au_id[20], au_flag[16];                      /* au.aid, au.fl */
    char tags[8][GAL_KEY]; int ntags;
} gal_item;

typedef struct gal_page { gal_item *items; int n; int more; } gal_page;

/* the wiki words for names (builds/words.json W): races, trees, bg, nouns + the templates */
typedef struct gal_words gal_words;

int  gal_list_url(const gal_query *q, char *url, size_t n);           /* 0 ok */
int  gal_parse_page(const char *json, size_t n, gal_page *out, char *err, size_t errn);
int  gal_parse_item(const char *json, size_t n, gal_item *out, char **blob, char *err, size_t errn);  /* blob malloc'd or NULL */
void gal_page_free(gal_page *p);

/* network (cm_http; dungeonsettlers.wiki only). 0 ok, -1 err ("no internet" ...). */
int  gal_fetch_page(const gal_query *q, gal_page *out, char *err, size_t errn);
int  gal_fetch_item(const char *id, gal_item *out, char **blob, char *err, size_t errn);

gal_words *gal_words_parse(const char *json, size_t n, char *err, size_t errn);
gal_words *gal_words_fetch(char *err, size_t errn);
void       gal_words_free(gal_words *w);
const char *gal_word(const gal_words *w, const char *group, const char *key);   /* W.<group>[key] (first of a pair) */
int  gal_word_keys(const gal_words *w, const char *group, const char **keys, int max);   /* keys of W.<group>, file order */

/* the names, by teamname.js: a team "Elf Shield & Greatsword Fellowship", a character its race,
 * a team member "Elf 2", the author "<chosen name>" or "Elf Mage 52" */
void gal_item_title(const gal_item *it, const gal_words *w, char *out, size_t n);
void gal_member_name(const gal_item *it, int index, const gal_words *w, char *out, size_t n);
void gal_author_name(const gal_item *it, const gal_words *w, char *out, size_t n);

/* 1 = gb unknown or equal to game_version (a "DS_" prefix is ignored). NOT used to refuse items (lead 2026-10-15). */
int  gal_usable(const gal_item *it, const char *game_version);
