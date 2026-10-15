// steam_stub_win.c - Windows stand-in for src/net/steam/dsnet_steam.c (Mac: dyld/dlopen of libsteam_api.dylib).
// Not ported yet: every call reports "no Steam", so mp falls back to direct TCP (host/port in mp.json).
// Port later: GetModuleHandleA("steam_api64.dll") + GetProcAddress for the same flat-API symbols.
#include "dsnet.h"
#include "dsnet_steam.h"
#include <stddef.h>

static const char *E = "Steam bridge not ported to Windows yet";
int dsnet_steam_load(const char *path) { (void)path; return -1; }
const char *dsnet_steam_error(void) { return E; }
int dsnet_steam_is_running(void) { return 0; }
int dsnet_steam_init_standalone(uint32_t appid) { (void)appid; return -1; }
void dsnet_steam_shutdown_standalone(void) {}
int dsnet_steam_logged_on(void) { return 0; }
uint64_t dsnet_steam_my_id(void) { return 0; }
int dsnet_steam_relay_status(void) { return -1; }
dsnet_transport dsnet_steam_transport(uint32_t hconn) { (void)hconn; dsnet_transport t = {0}; return t; }
dsnet_steam_host *dsnet_steam_host_start(uint16_t p, const char *b, uint32_t m, int n) { (void)p; (void)b; (void)m; (void)n; return NULL; }
uint64_t dsnet_steam_host_lobby(dsnet_steam_host *h) { (void)h; return ~0ull; }
uint32_t dsnet_steam_host_listen_socket(dsnet_steam_host *h) { (void)h; return 0; }
int dsnet_steam_host_peers(dsnet_steam_host *h) { (void)h; return 0; }
int dsnet_steam_host_invite(dsnet_steam_host *h) { (void)h; return -1; }
void dsnet_steam_host_stop(dsnet_steam_host *h) { (void)h; }
dsnet_steam_join *dsnet_steam_join_lobby(uint64_t l, const char *b, uint32_t m) { (void)l; (void)b; (void)m; return NULL; }
dsnet_steam_join *dsnet_steam_join_host(uint64_t id) { (void)id; return NULL; }
int dsnet_steam_join_poll(dsnet_steam_join *j, uint16_t *port) { (void)j; (void)port; return -1; }
const char *dsnet_steam_join_error(dsnet_steam_join *j) { (void)j; return E; }
void dsnet_steam_join_close(dsnet_steam_join *j) { (void)j; }
uint64_t dsnet_steam_take_join_request(void) { return 0; }
int dsnet_steam_friends_in_game(dsnet_steam_friend *out, int max) { (void)out; (void)max; return 0; }
const char *dsnet_steam_lobby_data(uint64_t lobby, const char *key) { (void)lobby; (void)key; return ""; }
uint64_t dsnet_steam_lobby_owner(uint64_t lobby) { (void)lobby; return 0; }
int dsnet_steam_host_adopt(dsnet_steam_host *h, uint32_t hconn) { (void)h; (void)hconn; return -1; }
dsnet_steam_join *dsnet_steam_join_adopt(uint32_t hconn) { (void)hconn; return NULL; }
int dsnet_steam_socket_pair(uint32_t *a, uint32_t *b) { (void)a; (void)b; return -1; }
const char *dsnet_steam_my_name(void) { return ""; }
int dsnet_steam_initialized(void) { return 0; }
int dsnet_steam_host_invite_friend(dsnet_steam_host *h, uint64_t friend_id) { (void)h; (void)friend_id; return -1; }
int dsnet_steam_friends(dsnet_steam_friend *out, int max) { (void)out; (void)max; return 0; }
void dsnet_steam_feed_callback(int id, const void *param, int cub) { (void)id; (void)param; (void)cub; }
