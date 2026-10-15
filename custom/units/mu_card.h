/* mu_card - the data of one unit card as the wiki builder's Compact Plus view shows it (owner 2026-10-15: "the avatar,
 * what the NPC looks like, a quick view of the items being worn and the skills in the skill bar, the branches being
 * used, the passive, the inscriptions ... a bit like the Compact Plus view"). Pure C, no game code: the Bring Units
 * window and the My Units tab draw it (icons and names come from the game by these keys).
 * Port of builder.js unitCompactCard(plus) + branchIconRow + compactInscHtml + compactEqHtml, plus quickFor (the skill
 * bar the unit arrives with) and unitSkillSets().passives. Tables: mu_card_tables.h (tools/gen_card_tables.js).
 * Checked against the builder itself on random builds: test/builder_vs_c.js.
 * Every key is a static string (tables), valid for the life of the program. */
#pragma once
#include "pvp_wiki.h"

#define MUC_MAX 128

typedef struct mu_card {
    char name[64];                     /* the build's own name, "" = none (the card says "Unit") */
    int lvl;
    const char *race, *bg;             /* AFFECTER_ keys; bg NULL = none */
    const char *weapon;                /* the Primary item's weapon label ("Greatsword", "Fire Staff"), NULL = unarmed */
    const char *portrait;              /* face sprite key (e.g. "HumanFatFemale_003"), NULL = none */
    const char *avatar;                /* the look the NPC is drawn with: portrait, else the race's first one */
    /* branches (skill trees): ACTIVE = weapon / magic trees, PASSIVE = the others; background trees first, then other
       trees with a learned skill. invested = at least one learned skill in it (else drawn dimmed). Inscription trees
       are not here: they show as their inscription in insc[]. */
    int nactive, npassive;
    struct { const char *tree; int invested; } active[WK_MAX_LIST], passive[WK_MAX_LIST];
    /* banked milestone passives (Sharp Shooter, Deep Water): closes the PASSIVE group; learned 0 = "!" */
    int nstacks;
    struct { const char *skill; int lv, max, learned; } stacks[8];
    int ninsc;   const char *insc[MUC_MAX];   /* inscriptions (tree inscriptions first), then ... */
    int nsouls;  const char *souls[WK_MAX_LIST]; /* ... soul stones, then ... */
    int nacq;    const char *acq[MUC_MAX];    /* ... traits acquired in play: one row, three groups */
    int neq;     struct { const char *slot, *item; } eq[WK_SLOTS];    /* worn items, slot order */
    int nbar;    const char *bar[16];         /* the skill bar (shortcut slots) the unit arrives with */
    /* passive skills (learned + the gear's own). folded = a conditional passive the builder counts at its maximum
       (owner rule, CORE_TRUTHS "Builder passives at rest"); stack_lv = banked milestone level, -1 = not a counter */
    int npass;   struct { const char *skill; int folded, stack_lv; } pass[MUC_MAX];
} mu_card;

/* 0 ok (the tables are checked against pvp_wiki_tables.h at compile time) */
int   mu_card_of(const wk_build *b, mu_card *out);
/* the card as JSON (same shape as test/node/card_ref.js), malloc'd */
char *mu_card_json(const mu_card *c);
