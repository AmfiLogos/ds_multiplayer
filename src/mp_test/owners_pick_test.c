/* owners_pick_test: the saved owner table that goes with a hosted world (../mp_owners_pick.h). Real breaks it catches:
   1. claims lost: a world hosted again after single-player play (mod off, uninstalled, or on without a session) gets
      no table, so every claim, lend and reserved unit is reset (the rule before 2026-11-02 took only tables within
      1200 ticks);
   2. the 2026-10-13 bug: an older save of the world gets the claims of a LATER save.
   `make owners` (nice 19). Prints "owners_pick: all passed" or the failed cases; exit 1 on a failure. */
#include "../mp_owners_pick.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>                                   /* mkdtemp */

static int fails;
static void touch(const char *dir, const char *name) {
    char p[600]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb"); if (f) { fputs("x", f); fclose(f); }
}
static void check(const char *what, int64_t got, int64_t want) {
    if (got != want) { fails++; printf("FAIL %s: picked %lld, want %lld\n", what, (long long)got, (long long)want); }
    else printf("PASS %s\n", what);
}

int main(void) {
    char dir[] = "/tmp/owners_pick_XXXXXX";
    if (!mkdtemp(dir)) { printf("FAIL setup: mkdtemp\n"); return 1; }
    const char *W = "f816ebc6-195c-432c-9fff-754b75ee3738", *O = "0a0a0a0a-0000-0000-0000-000000000001";
    touch(dir, "f816ebc6-195c-432c-9fff-754b75ee3738_5000.bin");      /* session save at tick 5000 */
    touch(dir, "f816ebc6-195c-432c-9fff-754b75ee3738_9000.bin");      /* a later session save */
    touch(dir, "0a0a0a0a-0000-0000-0000-000000000001_8000.bin");      /* another world */
    touch(dir, "f816ebc6-195c-432c-9fff-754b75ee3738_7000.bin.tmp");  /* a write that never finished */
    check("hosted again right after the session save (tick 9060)", mp_owners_pick(dir, W, 9060), 9000);
    check("hosted again after 200000 ticks of single-player play: the claims come back", mp_owners_pick(dir, W, 209000), 9000);
    check("an older save (tick 5060) never gets the later save's claims (2026-10-13 bug)", mp_owners_pick(dir, W, 5060), 5000);
    check("an older save from before any session (tick 3000): none", mp_owners_pick(dir, W, 3000), -1);
    check("another world's table is never taken", mp_owners_pick(dir, O, 7000), -1);
    check("another world: its own table", mp_owners_pick(dir, O, 8100), 8000);
    check("no folder yet: none", mp_owners_pick("/tmp/owners_pick_no_such_folder", W, 9000), -1);
    /* the temp folder stays (no delete: owner rule); /tmp is emptied at restart */
    if (fails) { printf("owners_pick: %d failed\n", fails); return 1; }
    printf("owners_pick: all passed\n");
    return 0;
}
