/* mp_bridge - C entry points for managed mods (BepInEx / MelonLoader plugins) that want to know about the multiplayer
 * session and send gameplay actions through the lockstep order path (lane ext/bepinex_bridge, NOTES.md there).
 *
 * Exported by our mod library (Windows: baselib.dll, Mac: dsrig.dylib) under these exact names. A plugin looks them up
 * at run time; missing = no multiplayer mod installed (the plugin then applies its actions locally, single player).
 *
 * Actions: dsmp_bridge_send(channel, data) becomes one mp order (type MP_BRIDGE_TYPE, zero owner Guid = open to every
 * player) and runs on EVERY peer at the step the server stamps, the sender included, inside the sim scope:
 *   - channel "dsmp.*": built-in native actions, the same C code on Mac and Windows (a peer needs no plugin for them);
 *   - any other channel: the managed dispatcher set with dsmp_bridge_set_dispatch (the plugin's handlers).
 *     Every peer must run the same plugin set: a peer without a handler counts it as unhandled (that peer desyncs).
 * dsmp_bridge_order(type, owner, args) passes a native order type the bridge allows (MOVE, ORD) to mp_order,
 * with the normal unit-owner check.
 *
 * All functions: Unity main thread. The dispatcher is called from inside the command step (sim scope). */
#pragma once
#include <stdint.h>

#define MP_BRIDGE_TYPE    50   /* mp order type (used elsewhere: 1-7, 20-22, 30-37, 48-49) */
#define MP_BRIDGE_API     1    /* dsmp_bridge_api_version() */
#define MP_BRIDGE_CH_MAX  63   /* channel name bytes */

enum { MP_BRIDGE_ROLE_NONE = 0, MP_BRIDGE_ROLE_HOST = 1, MP_BRIDGE_ROLE_GUEST = 2, MP_BRIDGE_ROLE_SPECTATOR = 3 };
/* dsmp_bridge_send / dsmp_bridge_order results */
enum { MP_BRIDGE_SENT = 0, MP_BRIDGE_NO_SESSION = 1,   /* not in a session: the caller applies the action itself */
       MP_BRIDGE_BAD_ARGS = -1, MP_BRIDGE_SPECTATOR = -2, MP_BRIDGE_REFUSED = -3, MP_BRIDGE_NOT_ALLOWED = -4 };

/* managed dispatcher: one call per replayed bridge order on every peer. Return 0 = applied, else failed (counted). */
typedef int (*mp_bridge_dispatch_fn)(int player, const char *channel, const uint8_t *data, int len, uint32_t step);

typedef struct mp_bridge_stats {
    uint32_t sent, received, applied_native, applied_managed, unhandled, failed;
} mp_bridge_stats;

#ifdef _WIN32
#define MP_BRIDGE_EXPORT __declspec(dllexport)
#else
#define MP_BRIDGE_EXPORT __attribute__((visibility("default")))
#endif

MP_BRIDGE_EXPORT int      dsmp_bridge_api_version(void);
MP_BRIDGE_EXPORT int      dsmp_bridge_active(void);       /* 1 = in a multiplayer session (welcome received) */
MP_BRIDGE_EXPORT int      dsmp_bridge_role(void);         /* MP_BRIDGE_ROLE_* */
MP_BRIDGE_EXPORT int      dsmp_bridge_player(void);       /* this peer's player id, -1 = none */
MP_BRIDGE_EXPORT uint32_t dsmp_bridge_step(void);         /* lockstep step being applied / next */
MP_BRIDGE_EXPORT int      dsmp_bridge_applying(void);     /* 1 while a bridge order is being replayed */
/* "rule\nmodset_hash\nline\nline..." (rule: vanilla | mods | none = no launcher session). Copies at most n-1 bytes and a
 * 0; returns the full length (call with n = 0 to size the buffer). */
MP_BRIDGE_EXPORT int      dsmp_bridge_modset(char *out, int n);
MP_BRIDGE_EXPORT int      dsmp_bridge_send(const char *channel, const uint8_t *data, int len);
MP_BRIDGE_EXPORT int      dsmp_bridge_order(int type, const uint8_t owner[16], const uint8_t *args, int len);
MP_BRIDGE_EXPORT void     dsmp_bridge_set_dispatch(mp_bridge_dispatch_fn fn);   /* NULL = none */
MP_BRIDGE_EXPORT void     dsmp_bridge_get_stats(mp_bridge_stats *out);

/* mod side */
struct mp_session;
void  mp_bridge_attach(struct mp_session *s);             /* mp_game.c, next to ord_mp_attach; NULL detaches */
char *mp_bridge_exec(const char *cmd, const char *line);  /* rig commands bridge_*; NULL = not ours */
