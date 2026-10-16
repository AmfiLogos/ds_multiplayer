/* bld - the Unit Builder (lane builder, PLAN_V13 WP BLD). Home entry "Unit Builder" (order 20) on the title's Custom
 * Modes page; the door other windows call (fixed interface, PLAN_V13 section 4):
 *   void cm_builder_open(const char *unit_id_or_NULL, void (*saved)(const char *unit_id, void *ud), void *ud);
 * NULL = a new unit; a My Units id = Edit that unit. saved() runs on the main thread after Save, with the unit's My Units
 * id (a new id, or the edited unit's own); never on Cancel. */
#pragma once
#include "dsl_sdk.h"
#include <stddef.h>

void cm_builder_init(const DSLHost *host, DSLPlugin *self);
void cm_builder_open(const char *unit_id_or_null, void (*saved)(const char *unit_id, void *ud), void *ud);
int  bld_is_open(void);
void bld_close_window(void);
/* test driver (ext/units13/driver "bld:<name>"): widgets by name, JSON state */
DSLObj bld_test_find(const char *name);
size_t bld_test_state(char *out, size_t cap);
