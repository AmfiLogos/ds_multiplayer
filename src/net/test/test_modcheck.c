/* ds_modcheck.h tests that catch real breaks of "Get the host's mods" (v0.1.14):
 *  - a gameplay-settings suffix (;set=) read as a version or as files (a guest would be told "other version" /
 *    "reinstall" for a settings difference, and the loader's plan would get the wrong lines);
 *  - the DSMP_REJOIN line not surviving format -> parse (the new process would join nobody, or the wrong host);
 *  - the loader's plan JSON misread (wrong rows, or the button shown for a mod that cannot be downloaded). */
#include "../ds_modcheck.h"
#include <stdio.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void settings_suffix(void) {
    dsmc_mod m[4];
    int n = dsmc_parse("hell@0.1.4:aa:pp;set=hp=2&x=a%20b\ncdr@1.0;set=k=1\n", m, 4);
    CHECK(n == 2, "2 lines, got %d", n);
    CHECK(!strcmp(m[0].ver, "0.1.4") && !strcmp(m[0].rest, "aa:pp") && !strcmp(m[0].set, "hp=2&x=a%20b"), "line 1: ver '%s' rest '%s' set '%s'", m[0].ver, m[0].rest, m[0].set);
    CHECK(!strcmp(m[1].ver, "1.0") && !m[1].rest[0] && !strcmp(m[1].set, "k=1"), "line 2: ver '%s' rest '%s' set '%s'", m[1].ver, m[1].rest, m[1].set);
    char out[1000]; dsmc_counts c;
    int d = dsmc_guest_text("hell@0.1.4:aa;set=hp=2", "hell@0.1.4:aa;set=hp=3", 0, NULL, out, sizeof out, &c);
    CHECK(d == 1 && c.settings == 1 && !c.version && !c.files && !c.missing && !c.extra, "settings only: d %d v %d f %d s %d", d, c.version, c.files, c.settings);
    d = dsmc_guest_text("hell@0.1.4:aa;set=hp=2", "hell@0.1.4:aa;set=hp=2", 0, NULL, out, sizeof out, &c);
    CHECK(d == 0, "same settings: no difference, got %d", d);
    char p[100]; dsmc_set_pretty("hp=2&x=a%20b", p, sizeof p);
    CHECK(!strcmp(p, "hp = 2, x = a b"), "pretty '%s'", p);
}

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
    CHECK(dsmc_plan_parse("{\"error\":\"x\"}", &pl) < 0 && dsmc_plan_parse("{\"rows\":[{\"id\":\"a\"", &pl) < 0, "bad JSON refused");
}

int main(void) {
    settings_suffix(); rejoin_line(); plan_json();
    if (fails) { printf("SUMMARY 0 PASS, %d FAIL\n", fails); return 1; }
    printf("PASS test_modcheck\n");
    return 0;
}
