// cm_home.h - the Custom Modes home (PLAN_V13 R4): the title button "Custom Modes" (below "Mods", only while the Custom
// Modes mod is on) opens a page in the title column, the same pattern as Multiplayer > Host a game: one button per
// registered entry (in `order`), then Back. Each entry opens its own game-style window.
// Fixed interface (PLAN_V13 section 4). Orders: My Units 10, Unit Builder 20, Gallery 30, Test Fights 40.
// Built into custom-modes.dylib (the solo module); works without multiplayer.
#pragma once
#include "dsl_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register an entry of the home page. id unique ("my-units"), label = button text, tip = hover text or NULL (no card),
   open = called on the main thread between frames when the button is clicked (the page stays open under the window).
   Call it from your cm_*_init. 0 = ok, -1 = bad arguments / id taken / table full. */
int cm_home_add(const char *id, const char *label, const char *tip, int order, void (*open)(void));

/* the host and module handle Custom Modes runs with (valid after cm_home_init; NULL before) */
const DSLHost *cm_host(void);
DSLPlugin *cm_self(void);

/* cm_module.c only: registers the title button (dsl_plugin_init time: the widget kit exists then) */
int cm_home_init(const DSLHost *host, DSLPlugin *self);
/* page state for tests: 1 = the home page shows; out = "label|shown;..." of its buttons in order */
int cm_home_test_state(char *out, size_t cap);
/* the RectTransform of a page button by label ("Back" included), NULL = none; for test drivers */
DSLObj cm_home_test_button(const char *label);
/* test drivers (ext/units13/driver): widgets by name "<prefix>:<name>" and a JSON state, from every part that registered
   a resolver ("home" = this page: home:<label>). state() appends `"<prefix>":{...}` members (no braces around all). */
typedef DSLObj (*cm_test_find_fn)(const char *name);
typedef size_t (*cm_test_state_fn)(char *out, size_t cap);
int cm_test_register(const char *prefix, cm_test_find_fn find, cm_test_state_fn state);
DSLObj cm_test_target(const char *name);
size_t cm_test_state(char *out, size_t cap);
/* open / close the page without a click (test drivers) */
void cm_home_open(void);
void cm_home_close(void);

#ifdef __cplusplus
}
#endif
