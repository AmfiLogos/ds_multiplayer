/* mp_owners_pick.h - which saved owner table belongs to the world a host opens (header only; mp_game.c + its test).
   Owner tables: <data>/mp_owners/<campaign guid>_<world tick>.bin, one per host save in a session (mp_game.c owners_store).
   Rule (owner item 37, 2026-11-02, "keep a removed or turned-off mod's save data"; tModLoader keeps a missing mod's world
   data and gives it back when the mod returns): the table saved at the HIGHEST tick <= now, however old. So the claims
   come back after the world was played without a session (single player, multiplayer off or uninstalled).
   Never a table from a LATER tick: an older save of the world loaded must not get the claims of a later save
   (bug 2026-10-13, pair CD). Until 2026-11-02 only tables within 1200 ticks counted: any single-player play between two
   sessions reset every claim, lend and reserved unit. Test: src/mp_test/owners_pick_test.c (make -C src/mp_test owners). */
#ifndef MP_OWNERS_PICK_H
#define MP_OWNERS_PICK_H
#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* dir = the mp_owners folder, guid = the campaign guid as in the file names. -1 = no table for this world at or before now. */
static int64_t mp_owners_pick(const char *dir, const char *guid, int64_t now) {
    size_t gl = strlen(guid);
    int64_t best = -1;
    DIR *d = opendir(dir);
    for (struct dirent *e; d && (e = readdir(d)); ) {
        if (strncmp(e->d_name, guid, gl) || e->d_name[gl] != '_') continue;
        const char *s = e->d_name + gl + 1; char *end;
        long long t = strtoll(s, &end, 10);
        if (end == s || strcmp(end, ".bin") || t < 0) continue;
        if (t <= now && t > best) best = t;
    }
    if (d) closedir(d);
    return best;
}
#endif
