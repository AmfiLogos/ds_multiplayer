/* dsnet's save header (simstate) on bad input: dsnet_client.c is compiled in here so sim_deserialize can be fed
 * exact bytes. A failed parse must leave the state as it was; owners must index known players. Code review #2, #5. */
#include "../dsnet_client.c"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; ok = 0; } } while (0)

int main(void) {
    int ok = 1;
    static simstate src, dst, keep;
    src.present = 3; src.nknown = 2;
    memset(src.known[0].uid, 0x11, 16); memset(src.known[1].uid, 0x22, 16);
    src.nown = 1; memset(src.own[0].guid, 0xEE, 16); src.own[0].owner = 1;
    src.nitem = 1; memset(src.item[0].guid, 0xDD, 16); src.item[0].owner = 0;
    static uint8_t buf[SIM_BYTES];
    size_t n = sim_serialize(&src, buf);
    size_t own_at = 0;
    for (size_t i = 0; i + 17 <= n; i++) if (!memcmp(buf + i, src.own[0].guid, 16)) { own_at = i; break; }

    dst.present = 0x80; dst.nknown = 0; dst.lend_seq = 42; keep = dst;
    CHECK(own_at && sim_deserialize(&dst, buf, own_at + 17) < 0 && !memcmp(&dst, &keep, sizeof dst),
          "cut after the units: state changed (nknown %d, nown %d)", dst.nknown, dst.nown);
    dst = keep;
    buf[own_at + 16] = 2;                       /* owner 2 of 2 known players */
    CHECK(sim_deserialize(&dst, buf, n) < 0 && !memcmp(&dst, &keep, sizeof dst), "owner past known[] accepted");
    buf[own_at + 16] = 1;
    long u = sim_deserialize(&dst, buf, n);
    CHECK(u == (long)n && dst.nknown == 2 && dst.nown == 1 && dst.own[0].owner == 1 && dst.lend_seq == 42,
          "good header: used %ld, nknown %d, nown %d, lend_seq %u", u, dst.nknown, dst.nown, dst.lend_seq);
    printf("%s sim_header: cut header and owner past known[] refused with the state unchanged; good one loaded (local fields kept)\n",
           ok ? "PASS" : "FAIL");
    printf("%d failure(s)\n", failures);
    return failures != 0;
}
