/* Offline unit test of src/mp_bridge.c: stubs for mp / dsnet / rig; mp_order loops the order straight back into the
 * registered handler (what every peer does at the stamped step). Build + run: ./run_unit.sh */
#include "mp_bridge.h"
#include "mp.h"
#include "rig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* ---- stubs ---- */
static int pid = -1, host = 0, spect = 0; static uint32_t step = 100;
static mp_order_fn h50; static uint16_t last_type; static uint8_t last_guid[16]; static int loopback = 1;
static struct { char pad[0x50]; void *clan; } cdc_obj; static struct { char pad[0x2C]; int32_t gold; } clan_obj;
void rig_log(const char *fmt, ...) { va_list a; va_start(a, fmt); printf("  log: "); vprintf(fmt, a); printf("\n"); va_end(a); }
Obj cdc(void) { return (Obj)&cdc_obj; }
dsnet_client *mp_client(mp_session *s) { return (dsnet_client *)s; }
int dsnet_player_id(dsnet_client *c) { (void)c; return pid; }
int dsnet_is_host(dsnet_client *c) { (void)c; return host; }
int dsnet_is_spectator(dsnet_client *c) { (void)c; return spect; }
uint32_t mp_cur_step(mp_session *s) { (void)s; return step; }
void mp_set_order_handler(mp_session *s, uint16_t type, mp_order_fn fn) { (void)s; if (type == MP_BRIDGE_TYPE) h50 = fn; }
int mp_order(mp_session *s, uint16_t type, const uint8_t guid[16], const void *args, uint16_t len) {
    last_type = type; memcpy(last_guid, guid, 16);
    if (type == MP_BRIDGE_TYPE && loopback && h50) h50(s, pid, guid, args, len);
    return 0;
}
int mp_session_mods(int *vanilla, char modset[65], char *mods, size_t n) {
    *vanilla = 0; strcpy(modset, "abcd"); snprintf(mods, n, "cheaper-recruits@1.0:ff\nhell@2.0"); return 0;
}
void bput(Buf *b, const char *fmt, ...) { char t[4096]; va_list a; va_start(a, fmt); int w = vsnprintf(t, sizeof t, fmt, a); va_end(a);
    b->p = realloc(b->p, b->n + w + 1); memcpy(b->p + b->n, t, w + 1); b->n += w; }
void bstr(Buf *b, const char *s) { bput(b, "\"%s\"", s); }
int jstr(const char *j, const char *k, char *o, size_t n) { (void)j; (void)k; (void)o; (void)n; return 0; }
int jnum(const char *j, const char *k, double *o) { (void)j; (void)k; (void)o; return 0; }

static int calls; static char got_ch[80]; static uint8_t got_d[64]; static int got_n, got_player; static uint32_t got_step;
static int disp(int player, const char *ch, const uint8_t *d, int n, uint32_t s) {
    calls++; snprintf(got_ch, sizeof got_ch, "%s", ch); memcpy(got_d, d, n); got_n = n; got_player = player; got_step = s;
    CHECK(dsmp_bridge_applying() == 1);
    return strcmp(ch, "demo.fail") ? 0 : -5;
}

int main(void) {
    cdc_obj.clan = &clan_obj; clan_obj.gold = 1000;
    mp_bridge_stats st;
    /* no session */
    CHECK(dsmp_bridge_api_version() == MP_BRIDGE_API);
    CHECK(dsmp_bridge_active() == 0 && dsmp_bridge_role() == MP_BRIDGE_ROLE_NONE && dsmp_bridge_player() == -1);
    CHECK(dsmp_bridge_send("demo.note", (const uint8_t *)"x", 1) == MP_BRIDGE_NO_SESSION);
    /* session, guest player 1 */
    mp_bridge_attach((mp_session *)0x1); pid = 1;
    CHECK(h50 != NULL);
    CHECK(dsmp_bridge_active() == 1 && dsmp_bridge_role() == MP_BRIDGE_ROLE_GUEST && dsmp_bridge_player() == 1 && dsmp_bridge_step() == 100);
    host = 1; CHECK(dsmp_bridge_role() == MP_BRIDGE_ROLE_HOST); host = 0;
    char ms[256]; int n = dsmp_bridge_modset(ms, sizeof ms);
    CHECK(n == (int)strlen(ms) && !strcmp(ms, "mods\nabcd\ncheaper-recruits@1.0:ff\nhell@2.0"));
    CHECK(dsmp_bridge_modset(NULL, 0) == n);
    char small[5]; dsmp_bridge_modset(small, sizeof small); CHECK(!strcmp(small, "mods"));
    /* native gold */
    int32_t d = 250;
    CHECK(dsmp_bridge_send("dsmp.gold.add", (uint8_t *)&d, 4) == MP_BRIDGE_SENT);
    CHECK(last_type == MP_BRIDGE_TYPE && clan_obj.gold == 1250);
    d = -5000; dsmp_bridge_send("dsmp.gold.add", (uint8_t *)&d, 4); CHECK(clan_obj.gold == 0);   /* clamped */
    dsmp_bridge_send("dsmp.gold.add", (uint8_t *)&d, 3);                                          /* bad length: failed */
    /* managed without a dispatcher: unhandled */
    dsmp_bridge_send("demo.note", (const uint8_t *)"hi", 2);
    dsmp_bridge_get_stats(&st);
    CHECK(st.sent == 4 && st.received == 4 && st.applied_native == 2 && st.unhandled == 1 && st.failed == 1);
    /* managed with a dispatcher */
    dsmp_bridge_set_dispatch(disp);
    CHECK(dsmp_bridge_send("demo.note", (const uint8_t *)"hello", 5) == MP_BRIDGE_SENT);
    CHECK(calls == 1 && !strcmp(got_ch, "demo.note") && got_n == 5 && !memcmp(got_d, "hello", 5) && got_player == 1 && got_step == 100);
    CHECK(dsmp_bridge_applying() == 0);
    dsmp_bridge_send("demo.empty", NULL, 0); CHECK(calls == 2 && got_n == 0);
    dsmp_bridge_send("demo.fail", NULL, 0);
    dsmp_bridge_get_stats(&st); CHECK(st.applied_managed == 2 && st.failed == 2);
    /* bad args */
    char longch[80]; memset(longch, 'a', 70); longch[70] = 0;
    CHECK(dsmp_bridge_send(longch, NULL, 0) == MP_BRIDGE_BAD_ARGS);
    CHECK(dsmp_bridge_send("", NULL, 0) == MP_BRIDGE_BAD_ARGS);
    CHECK(dsmp_bridge_send("x", NULL, 3) == MP_BRIDGE_BAD_ARGS);
    CHECK(dsmp_bridge_send("x", (uint8_t *)"a", DSNET_MAX_PAYLOAD) == MP_BRIDGE_BAD_ARGS);
    /* bad wire */
    uint8_t w1[] = { 2, 1, 'x' }, w2[] = { 1, 9, 'x' }, w3[] = { 1 };
    static const uint8_t z[16];
    CHECK(h50(NULL, 0, z, w1, 3) != 0 && h50(NULL, 0, z, w2, 3) != 0 && h50(NULL, 0, z, w3, 1) != 0);
    /* native orders */
    uint8_t g[16] = { 7 }, a8[8] = { 0 };
    CHECK(dsmp_bridge_order(1, g, a8, 8) == MP_BRIDGE_SENT && last_type == 1 && last_guid[0] == 7);
    CHECK(dsmp_bridge_order(2, g, a8, 8) == MP_BRIDGE_SENT && last_type == 2);
    CHECK(dsmp_bridge_order(35, g, a8, 4) == MP_BRIDGE_NOT_ALLOWED);
    CHECK(dsmp_bridge_order(MP_BRIDGE_TYPE, g, a8, 4) == MP_BRIDGE_NOT_ALLOWED);
    /* spectator */
    spect = 1;
    CHECK(dsmp_bridge_role() == MP_BRIDGE_ROLE_SPECTATOR);
    CHECK(dsmp_bridge_send("demo.note", NULL, 0) == MP_BRIDGE_SPECTATOR && dsmp_bridge_order(1, g, a8, 8) == MP_BRIDGE_SPECTATOR);
    spect = 0;
    /* rig command */
    char *r = mp_bridge_exec("bridge_status", "{}"); CHECK(r && strstr(r, "\"gold\":0") && strstr(r, "\"role\":2")); printf("  %s\n", r); free(r);
    CHECK(mp_bridge_exec("other", "{}") == NULL);
    /* detach */
    mp_bridge_attach(NULL); CHECK(dsmp_bridge_active() == 0 && dsmp_bridge_send("demo.note", NULL, 0) == MP_BRIDGE_NO_SESSION);
    printf(fails ? "unit: %d FAIL\n" : "unit: all PASS\n", fails);
    return fails != 0;
}
