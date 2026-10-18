// mp_api.h - the Multiplayer API (research/MASTER_PLAN.md 2.2 B; M4 split 2026-10-15).
// Custom Modes (ds_multiplayer/custom, its own dylib) never gets called by name from multiplayer: it registers callbacks
// here. It finds the table with module_loaded("multiplayer") and dlsym(<multiplayer image>, "mp_api_get"). Multiplayer
// works alone: with nothing registered every hook point below behaves as "no custom mode".
// Rules: every callback runs on the game's main thread (sim callbacks inside the lockstep step, same step on every peer).
// Registration is allowed any time after mp_api_get; nothing is ever unregistered (a module stays for the process).
// Table layout: functions are only appended (check size); version = MP_API_VERSION of the multiplayer that answered.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define MP_API_VERSION 1

enum { MP_ROLE_NONE = 0, MP_ROLE_HOST = 1, MP_ROLE_GUEST = 2 };
/* ui_set_flag: game UI rules a mod sets (ui_game.c). Default off. */
enum { MPUI_HIDE_GUILD_CONTACT = 1,          /* the HUD's Guild Contact button is hidden (hub with Recruiting Off) */
       MPUI_NO_RECRUIT_POPUP = 2 };          /* the recruitment window (popup 8) is not offered */
/* register_frame: where in the frame */
enum { MP_FRAME_ALWAYS = 0,                  /* every frame, title included (end of the rig command pump) */
       MP_FRAME_SESSION = 1 };               /* every frame while a multiplayer session runs (mp_game poll) */

typedef struct mp_player { int id; char name[32]; uint8_t uid[16]; } mp_player;
typedef struct mp_menu_hooks mp_menu_hooks;  /* mpmenu_kit.h (the menu's building blocks; windows inside the menu) */

/* join-point block: written between the det state and the save JSON as tag(4) + u32 len + bytes, in registration order */
typedef struct mp_joinpoint {
    char tag[4];                                                   /* e.g. "PVPB" */
    size_t (*size)(void);                                          /* 0 = no block in a normal join-point save */
    size_t (*write)(uint8_t *out, size_t cap);                     /* returns bytes written (must = size()) */
    void (*read)(const uint8_t *p, size_t n);                      /* p NULL / n 0: the save had no such block */
    /* optional: the host's join point sends a kept snapshot instead of the world now (PvP round reset / match end).
       1 = use det / json (owned by the module); the block is then always written. */
    int (*override_save)(const uint8_t **det, size_t *dl, const uint8_t **json, size_t *jn);
} mp_joinpoint;

/* register_host_choice2 (appended 2026-10-16, MPF API for lane fepvp). Texts are copied on register. */
typedef struct mp_host_choice {
    const char *id, *label, *tip;                  /* copied on register (id <= 31, label <= 47, tip <= 299 bytes) */
    void (*draw_page)(void *panel);                /* NULL = the menu's own Expedition Rules panel (rp_fe, M.set.fe_*), shown OPEN, no Done button. Else: called each frame while this choice's host page shows, panel = empty RectTransform in its own panel next to the column (where the Rules panel sits); draw_page(NULL) once when the page closes */
    int (*page_start)(char *err, size_t errn);     /* host page Start: 0 = the mod queued the world load; menu then waits like "Your save" (HOST_LOADING -> LOBBY + MPA_HOST_START). != 0: err shown on the page */
    int (*lobby_start)(char *err, size_t errn);    /* lobby Start of a session hosted from this choice: 0 = started (PLAYING, LOBBY_HIDE, START_GAME, like MPA_FE_START); != 0: err shown */
} mp_host_choice;

typedef struct mp_api_table {
    uint32_t version, size;                                        /* size = sizeof(mp_api_table) of the multiplayer that answered */
    /* ---- session facts */
    int (*role)(void);                                             /* MP_ROLE_* */
    int (*is_host)(void);
    int (*my_uid)(uint8_t out[16]);                                /* 0 ok, -1 no session */
    int (*players)(mp_player *out, int max);                       /* present players, slot order; count */
    uint32_t (*present_mask)(void);
    const char *(*session_busy)(void);                             /* NULL = free, else why (a session runs / is starting) */
    /* ---- menus */
    /* third choice under "Host a game": shown only once registered. on_start(death, xp, loot, recruit, err, errn) runs
       when the host presses Start on that page; 0 = started. draw_page: the page (not called yet, MASTER_PLAN C2). */
    int (*register_host_choice)(const char *id, const char *label, const char *tip, void (*draw_page)(void *root),
                                int (*on_start)(int death, int xp, int loot, int recruit, char *err, size_t errn));
    /* Esc menu button in a multiplayer game (a Report bug clone above Leave multiplayer): visible() each frame, click() */
    int (*register_esc_button)(const char *label, const char *tip, int (*visible)(void), void (*click)(void));
    /* ---- mail outside the sim (dsnet BLOB_MAIL). to_uid NULL = the host. One reader in multiplayer: a kind in a
       registered range goes to its callback (data freed after the call). Nothing registered = multiplayer never reads. */
    int (*mail_send)(uint8_t kind, const uint8_t to_uid[16], const void *blob, size_t len);
    int (*register_mail)(uint8_t kind_lo, uint8_t kind_hi, void (*cb)(const uint8_t from_uid[16], uint8_t kind, const uint8_t *data, size_t len));
    /* ---- sim commands through the lockstep (stamped, applied at the same step on every peer). kind: DSNET_FE_START (21) */
    int (*register_sim_cmd)(int kind, void (*apply)(const uint8_t *payload, size_t len));
    int (*send_sim_cmd)(int kind, const void *payload, size_t len);
    /* every sim step: freeze() 1 = this step is not run (tests), tick() after the world's step */
    int (*register_tick)(void (*tick)(void), int (*freeze)(void));
    int (*register_fingerprint)(uint64_t (*mix)(void));            /* mixed into the world fingerprint (a peer that differs = desync) */
    int (*register_joinpoint)(const mp_joinpoint *jp);
    /* host, after its join-point save is written and before it is sent: build(path, save_name) may rewrite that save
       (< 0 = failed: logged "<what> failed: the world goes on unchanged") */
    int (*register_world_build)(const char *what, int (*build)(const char *path, const char *save_name));
    /* orders: recruit_allowed() 0 = a recruit order is refused (silent safety net); on_pickup: the host saw a pick-up */
    int (*register_order_filter)(int (*recruit_allowed)(void), void (*on_pickup)(const uint8_t item[16], int player));
    /* ---- UI */
    void (*ui_set_flag)(int flag, int on);
    int (*register_unit_tag)(int (*tag)(const uint8_t guid[16], char *out, int cap), uint64_t (*sig)(void));   /* text after a unit's world name */
    int (*register_teams_portal)(int (*blocked)(void));           /* 1 = the expedition portal does nothing now */
    size_t (*modset_lines)(char *out, size_t cap);                 /* this player's gameplay mod lines ('\n'-joined); length */
    int (*register_rig_cmd)(const char *prefix, char *(*exec)(const char *cmd, const char *line));   /* test commands */
    /* ---- frame + save + menu (the split needed them: FE / PvP code that ran inside these places before) */
    int (*register_frame)(int when, void (*cb)(void));             /* MP_FRAME_* */
    int (*register_save_keep)(int (*keep)(const char *stem));      /* 1 = the game may not delete this save (an original kept by a mod) */
    int (*register_menu)(const mp_menu_hooks *h);                  /* windows inside the Multiplayer menu (mpmenu_kit.h) */
    /* ---- appended 2026-10-16 (check size before use) */
    int (*register_host_choice2)(const mp_host_choice *c);   /* 0 ok, -1 refused (5th choice / bad args); same id again = replace. Up to 4, under "Host a game" after "New save", registration order, stock gap + hover card right of the column */
    const char *(*host_choice_now)(void);                    /* id of the choice whose host page shows or whose session runs; "" none */
} mp_api_table;

/* exported by multiplayer: NULL if version > MP_API_VERSION */
__attribute__((visibility("default"))) const mp_api_table *mp_api_get(uint32_t version);
typedef const mp_api_table *(*mp_api_get_fn)(uint32_t version);

/* ---- multiplayer-internal hook points (mp_api.c), called by mp_core / mp_game / ord_mp / ui_game / ui_teams / rig_cmd */
int  mpx_sim_cmd(int kind, const uint8_t *payload, size_t len);   /* callbacks run; count */
int  mpx_tick_frozen(void);
void mpx_tick(void);
int  mpx_fp_mix(uint64_t *mix);                                    /* 1 = mix set */
int  mpx_jp_count(void);
const mp_joinpoint *mpx_jp_at(int i);
int  mpx_jp_override(const uint8_t **det, size_t *dl, const uint8_t **json, size_t *jn);
void mpx_world_build(const char *path, const char *save_name);
int  mpx_save_keep(const char *stem);
void mpx_frame(int when);
int  mpx_recruit_allowed(void);
void mpx_pickup(const uint8_t item[16], int player);
int  mpx_ui_flag(int flag);
int  mpx_unit_tag(const uint8_t guid[16], char *out, int cap);    /* > 0 = text written */
int  mpx_tag_sig(uint64_t *sig);                                   /* 1 = sig set */
int  mpx_portal_blocked(void);
char *mpx_rig_cmd(const char *cmd, const char *line);
const char *mp_session_busy_reason(void);                         /* = api session_busy (also for the loader glue) */
/* mp_moddata.c: mod data sync files in the join point (block "MDAT"; the loader's dsl_moddata_*) */
void mp_moddata_init(void);                                       /* registers the block (mp_game_install) */
int  mp_moddata_refused(char *why, size_t n);                     /* guest: the last block was refused (MD_* code), why = English */
void mp_moddata_clear_refusal(void);
void mp_moddata_session_over(void);                               /* title, no session: the guest's own files back */
