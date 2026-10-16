// cm_mp.c - Custom Modes, the part that needs multiplayer (custom-modes-mp.dylib): Foreign Expedition (fe/), PvP (pvp/)
// and the units code they share (units/). Loaded by custom-modes.dylib (cm_module.c) only when multiplayer is active; it
// links two-level to @rpath/multiplayer.dylib (the image already loaded: the loader's module or the rig's dsrig.dylib)
// and registers everything through the Multiplayer API (src/mp_api.h). Multiplayer never calls this code by name.
// Registration order = the order the code ran in when it was compiled into multiplayer: FE first, then PvP.
#include "dsl_sdk.h"
#include "mp_api.h"
#include "mpmenu_kit.h"
#include "rig.h"
#include "net/dsnet.h"
#include <stdio.h>
#include <string.h>

static const DSLHost *H __attribute__((unused));
static DSLPlugin *ME __attribute__((unused));

#ifndef CM_SAME_IMAGE
/* hooks of this module go through the loader's engine as Custom Modes' own (the rig host maps them to rig_hook).
   CM_SAME_IMAGE (Windows: one DLL with multiplayer): multiplayer's own rig_hook is used. */
int rig_hook(uintptr_t rva, void *repl, void **orig, const char *who) {
    int r = H && H->hook_rva ? H->hook_rva(ME, rva, repl, orig, who) : -1;
    if (r) rig_log("%s: hook %#lx refused (custom-modes)", who, (unsigned long)rva);
    return r;
}
#endif

/* Foreign Expedition (fe/fe_game.c, fe/fe_menu.c) */
void fe_on_start(const uint8_t *payload, size_t len);
int fe_join_point_save(const char *path, const char *save_name);
int fe_is_protected(const char *stem);
void fe_poll(void);
int fe_recruit_allowed(void);
void fe_note_loot(const uint8_t item[16], int player);
char *fe_exec(const char *cmd, const char *line);
int fe_menu_start(int death, int xp, int loot, int recruit, char *err, size_t errn);
const mp_menu_hooks *fe_menu_hooks(void);
/* PvP (pvp/pvp_game.c, pvp/pvp_ui.c, units/pvp_units.c) */
void pvp_on_units_start(const uint8_t *payload, size_t len);
int pvp_units_join_point(const char *path);
void pvp_sim_tick(void);
int pvp_freeze_now(void);
uint64_t pvp_fp_mix(void);
size_t pvp_block_size(void);
size_t pvp_block_get(uint8_t *out, size_t cap);
void pvp_block_set(const uint8_t *p, size_t n);
int pvp_save_override(const uint8_t **det, size_t *dl, const uint8_t **json, size_t *jn);
int pvp_unit_tag(const uint8_t g[16], char *out, int cap);
uint64_t pvp_tag_sig(void);
int pvp_portal_blocked(void);
void pvp_frame(void);
char *pvp_exec(const char *cmd, const char *line);
void pvp_install(void);
const mp_menu_hooks *pvp_menu_hooks(void);

static const mp_api_table *A;
/* the menu has per-choice page Start buttons (mp_api register_host_choice2, requested from lane mpfix 2026-10-16).
   0 until it exists: fe_menu.c then takes the FE page's "Host from a save" button as Start. */
int fe_choice_v2(void) { return 0; }
static void fe_ui_flags(void) {                          /* hub with Recruiting Off: Guild Contact hidden, popup 8 not offered */
    int off = !fe_recruit_allowed();
    A->ui_set_flag(MPUI_HIDE_GUILD_CONTACT, off);
    A->ui_set_flag(MPUI_NO_RECRUIT_POPUP, off);
}
static int pvp_world_build(const char *path, const char *save_name) { (void)save_name; return pvp_units_join_point(path); }

__attribute__((visibility("default"))) int cm_mp_start(const DSLHost *host, DSLPlugin *self, const mp_api_table *api) {
    H = host; ME = self; A = api;
    if (!api || api->size < sizeof(mp_api_table)) { rig_log("custom-modes: multiplayer API too old (size %u): Foreign Expedition and PvP off", api ? api->size : 0); return 1; }
    int bad = 0;
    /* Foreign Expedition */
    bad |= api->register_sim_cmd(DSNET_FE_START, fe_on_start);
    bad |= api->register_world_build("Foreign Expedition hub build", fe_join_point_save);
    bad |= api->register_save_keep(fe_is_protected);
    bad |= api->register_frame(MP_FRAME_SESSION, fe_poll);
    bad |= api->register_frame(MP_FRAME_ALWAYS, fe_ui_flags);
    bad |= api->register_order_filter(fe_recruit_allowed, fe_note_loot);
    bad |= api->register_rig_cmd("fe_", fe_exec);
    bad |= api->register_host_choice("foreign-expedition", "Foreign Expedition",
                                     "Everyone brings units into a neutral hub with everything unlocked. Nobody's save changes until they bring the results home.",
                                     NULL, fe_menu_start);
    bad |= api->register_menu(fe_menu_hooks());
    /* PvP */
    bad |= api->register_sim_cmd(DSNET_FE_START, pvp_on_units_start);         /* own units use the same command; each acts on its own run id */
    bad |= api->register_tick(pvp_sim_tick, pvp_freeze_now);
    bad |= api->register_fingerprint(pvp_fp_mix);
    { mp_joinpoint jp = { .tag = { 'P', 'V', 'P', 'B' }, .size = pvp_block_size, .write = pvp_block_get, .read = pvp_block_set, .override_save = pvp_save_override };
      bad |= api->register_joinpoint(&jp); }
    bad |= api->register_world_build("PvP own-units world build", pvp_world_build);
    bad |= api->register_frame(MP_FRAME_ALWAYS, pvp_frame);
    bad |= api->register_rig_cmd("pvp_", pvp_exec);
    bad |= api->register_unit_tag(pvp_unit_tag, pvp_tag_sig);
    bad |= api->register_teams_portal(pvp_portal_blocked);
    bad |= api->register_menu(pvp_menu_hooks());
    pvp_install();                                       /* hostility hooks */
    rig_log("custom-modes: Foreign Expedition + PvP registered with multiplayer (API %u)%s", api->version, bad ? " (a registration was refused)" : "");
    return bad ? 2 : 0;
}
