/* mp_worldsave.h - is a save file one of this mod's own worlds (Foreign Expedition hub, PvP world), by its file name?
 * The Bring Units save lists (card window mu_save.c, fe_game.c fe_list_saves, pvp_ui.c text list) show real worlds only
 * (owner 2026-10-20). Our worlds: the temp copies MPFE_ / MPHub_ / MPPvP_ / mp_sync / mp_fullcheck, and the game's own
 * saves of those worlds ("<clan>__AutoSave_<campaign guid, 32 hex>_<n>.json"), whose campaign guid starts with the mod's
 * world prefix: hub fe5ea00 (running) / fe5eb (template), PvP v1 b45e0 (fe_game.c FE_HUB_PREFIX, fe_hub.c). */
#pragma once
#include <string.h>
#include <ctype.h>

static inline int mp_world_save_name(const char *f) {
    static const char *const tmp[] = { "mp_sync", "mp_fullcheck", "MPFE_", "MPHub_", "MPPvP_" };
    for (size_t i = 0; i < sizeof tmp / sizeof *tmp; i++) if (!strncmp(f, tmp[i], strlen(tmp[i]))) return 1;
    static const char *const pre[] = { "fe5ea00", "fe5eb", "b45e0" };
    for (const char *p = strchr(f, '_'); p; p = strchr(p + 1, '_')) {    /* every "_<32 hex>" token */
        size_t k = 0; while (k < 32 && isxdigit((unsigned char)p[1 + k])) k++;
        if (k != 32 || isxdigit((unsigned char)p[33])) continue;
        for (size_t i = 0; i < sizeof pre / sizeof *pre; i++) if (!strncasecmp(p + 1, pre[i], strlen(pre[i]))) return 1;
    }
    return 0;
}
