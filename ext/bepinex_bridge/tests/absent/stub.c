/* Stand-in for the multiplayer mod's bridge exports (src/mp_bridge.h) for the C# "absent / old / new" test.
   STUB_API 1 = an old multiplayer mod (no random, no chat); 2 = the api 2 calls, returning values the test can predict
   (they check that strings, stream and bounds reach the C side intact). */
#include <stdint.h>
#include <string.h>
#define EX __attribute__((visibility("default")))
static uint32_t h(const char *s, uint32_t st) { uint32_t x = 2166136261u; for (; *s; s++) x = (x ^ (uint8_t)*s) * 16777619u; return x ^ (st * 2654435761u); }
EX int dsmp_bridge_api_version(void) { return STUB_API; }
EX int dsmp_bridge_active(void) { return 0; }
EX int dsmp_bridge_role(void) { return 0; }
EX int dsmp_bridge_player(void) { return -1; }
EX uint32_t dsmp_bridge_step(void) { return 7; }
EX int dsmp_bridge_applying(void) { return 0; }
EX int dsmp_bridge_modset(char *out, int n) { const char *t = "none\n\n"; if (n > 0) { strncpy(out, t, (size_t)n - 1); out[n - 1] = 0; } return (int)strlen(t); }
EX int dsmp_bridge_send(const char *c, const uint8_t *d, int n) { (void)c; (void)d; (void)n; return 1; }
EX int dsmp_bridge_order(int t, const uint8_t o[16], const uint8_t *a, int n) { (void)t; (void)o; (void)a; (void)n; return 1; }
EX void dsmp_bridge_set_dispatch(void *fn) { (void)fn; }
#if STUB_API >= 2
EX uint32_t dsmp_bridge_rand_u32(const char *m, uint32_t st) { return h(m, st); }
EX int dsmp_bridge_rand_range(const char *m, uint32_t st, int lo, int hi) { return hi <= lo ? lo : lo + (int)(h(m, st) % (uint32_t)(hi - lo)); }
EX float dsmp_bridge_rand_value(const char *m, uint32_t st) { return (float)(h(m, st) >> 8) / 16777216.0f; }
EX int dsmp_bridge_rand_shared(void) { return 1; }
/* chat: 0 when the name is "My Mod" and the text is the test's UTF-8 line, else -1 (strings must arrive intact) */
EX int dsmp_bridge_chat(const char *name, const char *text, int b) { return !strcmp(name, "My Mod") && !strcmp(text, "h\xC3\xA9llo <b>") ? (b ? 0 : 1) : -1; }
#endif
