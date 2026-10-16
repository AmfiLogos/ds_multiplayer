/* cm_win - the game-style window the Custom Modes solo tools (Test Fights, Gallery) open from the title's Custom Modes
 * page, drawn with the loader's widget kit (DSLUi, dsl_sdk.h): dark backdrop over the screen, centred panel, title in
 * the game's title font, X and Esc close. Also: a per-frame tick on the main thread (worker results come back through
 * it) and the game's event queue (LoadCampaignRequested).
 *
 * Test only: env CMF_TEST=<dir> runs one command at a time from <dir>/cmd.json and answers in <dir>/result.json
 * (open / close / click / type / state / shot); controls are found by the names the windows give them (cmw_named). */
#pragma once
#include "dsl_sdk.h"
#include <stddef.h>

int  cmw_init(const DSLHost *host, DSLPlugin *self);      /* once, after il2cpp_init (host->ui set). 0 ok */
const DSLHost *cmw_host(void);
const DSLUi *cmw_ui(void);
void cmw_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* every frame on the main thread (from the game's InputDispatcher.Update) */
int  cmw_tick_add(void (*tick)(void *ud), void *ud);

/* a window: returns its body (w x (h - band), origin top-left), or NULL. One window per id; opening an open id closes
 * it first. closed(ud) runs after the window is gone (X, Esc, cmw_close). */
#define CMW_BAND 64.0f
DSLObj cmw_open(const char *id, const char *title, float w, float h, void (*closed)(void *ud), void *ud);
void   cmw_close(const char *id);
int    cmw_is_open(const char *id);
DSLObj cmw_flat(DSLObj panel, uint32_t rgba);              /* a kit panel as a flat colour (no frame sprite) */

/* test registry: kind 0 button, 1 toggle, 2 field, 3 text, 4 value row. A name given again replaces the old entry. */
enum { CMW_BUTTON, CMW_TOGGLE, CMW_FIELD, CMW_TEXT, CMW_VALUE };
DSLObj cmw_named(DSLObj w, int kind, const char *name, dsl_ui_cb cb, void *ud, const char *text);
void   cmw_set_text(DSLObj w, const char *s);             /* ui->set_text + the registry's copy */
void   cmw_forget(const char *prefix);                    /* drop names starting with prefix (before a section is rebuilt) */
/* test: a window opener ("fights", "gallery") and extra state (JSON object members, no braces) */
void   cmw_test_opener(const char *win, void (*open)(void));
void   cmw_test_state(void (*state)(char *out, size_t n));

/* the game: queue LoadCampaignRequested(<save name>) (the save is in <game data>/Saves/<name>.json). 0 ok */
int  cmw_load_campaign(const char *save_name);
/* the game: after files in the Saves folder changed, refresh its save list and the title's Continue (main thread). 0 ok */
int  cmw_saves_changed(void);
int  cmw_title_shown(void);                               /* the title menu is up (TitleMenuView active) */
/* the game's own folders (read on the main thread at init): Application.persistentDataPath, and the save folder
 * (SaveLoadHelper.GetSaveDirectory). Safe from any thread once read. 0 ok */
int  cmw_game_data(char *out, size_t n);
int  cmw_saves_dir(char *out, size_t n);
