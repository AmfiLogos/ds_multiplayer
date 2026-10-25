/* ds_modcheck.h tests that catch real breaks of "Get the host's mods" (v0.1.14):
 *  - the DSMP_REJOIN line not surviving format -> parse (the new process would join nobody, or the wrong host);
 *  - the loader's plan JSON misread (wrong rows, or the button shown for a mod that cannot be downloaded);
 *  - the box printing raw setting lines ("settings-demo/gold_mult@7") instead of "<label>: host 7, you 1". */
#include "../ds_modcheck.h"
#include <stdio.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void rejoin_line(void) {
    dsmc_rejoin a = {0}, b; char l[300];
    snprintf(a.how, sizeof a.how, "ip"); snprintf(a.addr, sizeof a.addr, "192.168.1.20"); a.port = 47777; a.watch = 1;
    snprintf(a.world, sizeof a.world, "5f0c-11aa");
    CHECK(!dsmc_rejoin_format(&a, l, sizeof l) && !strcmp(l, "ip|192.168.1.20|47777|watch|world=5f0c-11aa"), "ip line '%s'", l);
    CHECK(!dsmc_rejoin_parse(l, &b) && !strcmp(b.how, "ip") && !strcmp(b.addr, a.addr) && b.port == 47777 && b.watch && !strcmp(b.world, a.world), "ip round trip");
    memset(&a, 0, sizeof a); snprintf(a.how, sizeof a.how, "steam"); a.steamid = 76561198000000001ULL;
    CHECK(!dsmc_rejoin_format(&a, l, sizeof l) && !strcmp(l, "steam|76561198000000001"), "steam line '%s'", l);
    CHECK(!dsmc_rejoin_parse("steam|76561198000000001\n", &b) && b.steamid == 76561198000000001ULL && !b.watch, "steam round trip");
    CHECK(!dsmc_rejoin_parse("ip|::1|47777", &b) && !strcmp(b.addr, "::1"), "IPv6 address");
    CHECK(dsmc_rejoin_parse("ip|10.0.0.1|0", &b) && dsmc_rejoin_parse("ip||47777", &b) && dsmc_rejoin_parse("steam|0", &b)
          && dsmc_rejoin_parse("", &b) && dsmc_rejoin_parse("tcp|1.2.3.4|5", &b), "bad lines refused");
    memset(&a, 0, sizeof a); snprintf(a.how, sizeof a.how, "steam");
    CHECK(dsmc_rejoin_format(&a, l, sizeof l), "steam without a host id: no line");
}

static void plan_json(void) {
    static dsmc_plan pl;
    const char *j = "{\"rows\":[{\"id\":\"hell-difficulty\",\"name\":\"Hell \\\"difficulty\\\" \\u00e9\",\"host_ver\":\"0.1.4\",\"my_ver\":\"0.1.3\","
                    "\"my_on\":1,\"official\":1,\"action\":\"update\",\"host_set\":\"\",\"my_set\":\"\"},"
                    "{\"id\":\"cdr\",\"name\":\"\xe5\x86\xb7\xe5\x8d\xb4\",\"host_ver\":\"1.0\",\"my_ver\":\"1.0\",\"my_on\":1,\"official\":1,\"action\":\"same\",\"host_set\":\"a=1\",\"my_set\":\"a=1\"},"
                    "{\"id\":\"old\",\"name\":\"Old\",\"host_ver\":\"0.9\",\"my_ver\":\"1.0\",\"my_on\":1,\"official\":1,\"action\":\"host_older\",\"host_set\":\"\",\"my_set\":\"\"}],"
                    "\"can_fix\":1,\"downloads\":2}";
    int n = dsmc_plan_parse(j, &pl);
    CHECK(n == 3 && pl.can_fix == 1 && pl.downloads == 2, "rows %d can_fix %d downloads %d", n, pl.can_fix, pl.downloads);
    CHECK(!strcmp(pl.row[0].name, "Hell \"difficulty\" \xc3\xa9") && !strcmp(pl.row[0].action, "update") && !strcmp(pl.row[0].my_ver, "0.1.3"), "row 0 name '%s'", pl.row[0].name);
    CHECK(!strcmp(pl.row[1].name, "\xe5\x86\xb7\xe5\x8d\xb4") && !strcmp(pl.row[1].host_set, "a=1"), "row 1 '%s'", pl.row[1].name);
    char t[2048]; int d = dsmc_hostmods_text(&pl, 0, t, sizeof t, 6);
    CHECK(d == 2 && strstr(t, "0.1.3") && strstr(t, "the host should update") && strstr(t, "Same as the host:") && strstr(t, "Get the host's mods"), "box text: %s", t);
    const char *k = "{\"rows\":[{\"id\":\"x\",\"name\":\"X\",\"host_ver\":\"1\",\"my_ver\":\"\",\"my_on\":0,\"official\":0,\"action\":\"not_official\"}],\"can_fix\":1,\"downloads\":0}";
    CHECK(dsmc_plan_parse(k, &pl) == 1 && pl.can_fix == 0, "not_official: no button (can_fix %d)", pl.can_fix);
    dsmc_hostmods_text(&pl, 0, t, sizeof t, 6);
    CHECK(strstr(t, "it cannot be downloaded") && !strstr(t, "\"Get the host's mods\" downloads"), "not_official text: %s", t);
    /* the host's version, on, but it does not run here (crash guard): its own line with the loader's reason, never the
       button (a soft restart leaves it off: refused again, restart again), never "you have it, turned off" */
    const char *nr = "{\"rows\":[{\"id\":\"gcd-cdr\",\"name\":\"Global cooldown fix\",\"host_ver\":\"0.1.7\",\"my_ver\":\"0.1.7\",\"my_on\":0,"
                     "\"official\":1,\"action\":\"not_running\",\"host_set\":\"\",\"my_set\":\"\",\"why\":\"it stopped the game while starting last time, so it stays off until it is updated\"}],"
                     "\"can_fix\":1,\"downloads\":0}";
    CHECK(dsmc_plan_parse(nr, &pl) == 1 && pl.can_fix == 0, "not_running: no button (can_fix %d)", pl.can_fix);
    dsmc_hostmods_text(&pl, 0, t, sizeof t, 6);
    CHECK(strstr(t, "Global cooldown fix</color> is on but did not start on this computer: it stopped the game while starting last time")
          && strstr(t, "cannot fix a mod that does not start on this computer") && !strstr(t, "turned off") && !strstr(t, "downloads what is missing")
          && !strstr(t, "not one of our official mods"), "not_running text: %s", t);
    CHECK(dsmc_plan_parse("{\"error\":\"x\"}", &pl) < 0 && dsmc_plan_parse("{\"rows\":[{\"id\":\"a\"", &pl) < 0, "bad JSON refused");
}

static int fake_setting(const char *mod, const char *setting, const char *value, char *label, size_t lcap, char *vtext, size_t vcap) {
    if (strcmp(mod, "settings-demo") || strcmp(setting, "gold_mult")) return -1;
    snprintf(label, lcap, "Gold multiplier"); snprintf(vtext, vcap, "x%s", value); return 0;
}
static void settings_row(void) {
    static dsmc_plan pl; char t[2048];
    const char *j = "{\"rows\":[{\"id\":\"settings-demo\",\"name\":\"Settings demo\",\"host_ver\":\"1.0.0\",\"my_ver\":\"1.0.0\",\"my_on\":1,"
                    "\"official\":1,\"action\":\"settings\",\"host_set\":\"settings-demo/difficulty@normal\\nsettings-demo/gold_mult@7\","
                    "\"my_set\":\"settings-demo/difficulty@normal\\nsettings-demo/gold_mult@1\"}],\"can_fix\":1,\"downloads\":0}";
    CHECK(dsmc_plan_parse(j, &pl) == 1, "settings plan");
    dsmc_set_setting_text(fake_setting);
    dsmc_hostmods_text(&pl, 0, t, sizeof t, 6);
    CHECK(strstr(t, "Gold multiplier: host x7, you x1") && !strstr(t, "gold_mult@") && !strstr(t, "difficulty"), "settings row: %s", t);
    dsmc_set_setting_text(NULL);
    dsmc_hostmods_text(&pl, 0, t, sizeof t, 6);
    CHECK(strstr(t, "Gold mult: host 7, you 1") && !strstr(t, "gold_mult") && !strstr(t, "@"), "settings row without the loader's words: %s", t);
}

int main(void) {
    rejoin_line(); plan_json(); settings_row();
    if (fails) { printf("SUMMARY 0 PASS, %d FAIL\n", fails); return 1; }
    printf("PASS test_modcheck\n");
    return 0;
}
