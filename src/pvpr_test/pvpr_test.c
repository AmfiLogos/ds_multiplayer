/* pvpr_test - the PvP rules file (last used + presets) and the map pick (lane pvprules). make test.
 * Only breaks a player would see: rules or presets lost / changed across a restart, a file from another version wiping the
 * rules, the boss room / floor numbers pointing at the wrong game floor, a random map that never picks a dungeon or a floor. */
#include "pvp_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void t_persist(void) {
    char path[] = "/tmp/pvpr_test_XXXXXX"; int fd = mkstemp(path); close(fd);
    pvpr_store a; pvpr_store_init(&a);
    pvpr_vals_set(&a.last, "map", PVP_MAP_DUNGEON2); pvpr_vals_set(&a.last, "floor", PVP_FLOOR_BOSS); pvpr_vals_set(&a.last, "enemy_hpbars", 1); pvpr_vals_set(&a.last, "vanilla", 0);
    pvpr_vals p1; pvpr_defaults(&p1); pvpr_vals_set(&p1, "mode", 1); pvpr_vals_set(&p1, "enemy_vision", 1); pvpr_vals_set(&p1, "round_time", 4);
    pvpr_vals p2; pvpr_defaults(&p2); pvpr_vals_set(&p2, "map", PVP_MAP_RANDOM); pvpr_vals_set(&p2, "enemy_viewrange", 1);
    CHECK(pvpr_preset_save(&a, "  Tournoi [équipe] = 2v2  ", &p1) == 0, "save preset 1");
    CHECK(pvpr_preset_save(&a, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E rules", &p2) == 1, "save preset 2");
    CHECK(pvpr_store_save(&a, path) == 0, "store save");
    pvpr_store b; CHECK(pvpr_store_load(&b, path) == 0, "store load");
    CHECK(pvpr_vals_equal(&a.last, &b.last), "last used rules back after a restart");
    CHECK(b.npresets == 2, "2 presets back, got %d", b.npresets);
    CHECK(!strcmp(b.presets[0].name, "Tournoi [équipe] = 2v2"), "name 1 back: '%s'", b.presets[0].name);
    CHECK(!strcmp(b.presets[1].name, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E rules"), "name 2 back: '%s'", b.presets[1].name);
    CHECK(pvpr_vals_equal(&b.presets[0].v, &p1) && pvpr_vals_equal(&b.presets[1].v, &p2), "preset values back");
    /* same name replaces, delete removes, and both stay so after another restart */
    pvpr_vals_set(&p1, "pause", 1);
    CHECK(pvpr_preset_save(&b, "Tournoi [équipe] = 2v2", &p1) == 0 && b.npresets == 2, "same name = replaced");
    CHECK(pvpr_preset_delete(&b, 1) == 0 && b.npresets == 1, "delete");
    CHECK(pvpr_store_save(&b, path) == 0, "save 2");
    pvpr_store c; pvpr_store_load(&c, path);
    CHECK(c.npresets == 1 && pvpr_vals_get(&c.presets[0].v, "pause") == 1, "replace + delete kept after a restart");
    CHECK(pvpr_preset_match(&c, &p1) == 0, "the preset is recognised from the values");
    /* a long name is cut at a whole character and stays the same after a round trip */
    char lng[200]; strcpy(lng, ""); for (int i = 0; i < 30; i++) strcat(lng, "\xC3\xA9");    /* 60 bytes of 'é' */
    int k = pvpr_preset_save(&c, lng, &p2); CHECK(k == 1, "long name saved");
    CHECK(strlen(c.presets[1].name) == 40, "cut at 40 bytes = 20 characters, got %zu", strlen(c.presets[1].name));
    pvpr_store_save(&c, path); pvpr_store d; pvpr_store_load(&d, path);
    CHECK(d.npresets == 2 && !strcmp(d.presets[1].name, c.presets[1].name), "long name round trip");
    CHECK(pvpr_preset_save(&d, "   ", &p2) == -1, "empty name refused");
    unlink(path);
    char tmp[300]; snprintf(tmp, sizeof tmp, "%s.tmp", path); unlink(tmp);
    pvpr_store e; CHECK(pvpr_store_load(&e, path) == 1 && e.npresets == 0 && pvpr_vals_get(&e.last, "map") == 0, "no file = defaults");
}
static void t_other_version(void) {
    const char *f = "# x\n[last]\nmode=1\nnew_rule_of_v2=7\nenemy_status=9\nfloor=3\n[preset]\nmap=2\n[preset]\nname=A\nmap=3\nbogus line\n[preset]\nname=A\nmap=1\n";
    pvpr_store s; pvpr_store_read(&s, f, strlen(f));
    CHECK(pvpr_vals_get(&s.last, "mode") == 1 && pvpr_vals_get(&s.last, "floor") == 3, "known rules kept");
    CHECK(pvpr_vals_get(&s.last, "enemy_status") == 0, "out-of-range value = default");
    CHECK(s.npresets == 1 && pvpr_vals_get(&s.presets[0].v, "map") == 3, "nameless and duplicate presets dropped (%d)", s.npresets);
}
static void t_map(void) {
    pvp_map_pick a;
    CHECK(!pvp_map_resolve(PVP_MAP_DUNGEON1, PVP_FLOOR_BOSS, 7, &a) && a.region == 0 && a.floor == 4 && a.boss && !a.here, "dungeon 1 boss room = VeilSilence floor index 4");
    CHECK(!pvp_map_resolve(PVP_MAP_DUNGEON2, 1, 7, &a) && a.region == 1 && a.floor == 0 && !a.boss, "dungeon 2 floor 1 = FesteringBurrow floor index 0");
    CHECK(!pvp_map_resolve(PVP_MAP_DUNGEON2, 4, 7, &a) && a.floor == 3 && !a.boss, "floor 4 = index 3");
    CHECK(!pvp_map_resolve(PVP_MAP_HERE, 0, 7, &a) && a.here, "here");
    CHECK(pvp_map_resolve(9, 0, 7, &a) == -1 && pvp_map_resolve(2, 9, 7, &a) == -1, "bad input refused");
    int regions[2] = { 0, 0 }, floors[5] = { 0 };
    for (uint32_t s = 0; s < 4000; s++) {
        pvp_map_resolve(PVP_MAP_RANDOM, 0, s * 2654435761u, &a);
        if (a.region < 0 || a.region > 1 || a.floor < 0 || a.floor > 3 || a.boss || a.seed < 0 || !a.random_starts) { CHECK(0, "random map out of range: r %d f %d", a.region, a.floor); break; }
        regions[a.region]++; floors[a.floor]++;
        pvp_map_resolve(PVP_MAP_DUNGEON1, PVP_FLOOR_RANDOM, s, &a);
        if (a.floor < 0 || a.floor > 3 || a.region != 0) { CHECK(0, "random floor out of range"); break; }
    }
    CHECK(regions[0] > 1500 && regions[1] > 1500 && floors[0] > 700 && floors[3] > 700, "both dungeons and every floor come up");
}
int main(void) {
    t_persist(); t_other_version(); t_map();
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
