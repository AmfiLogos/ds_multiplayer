/* bld_init - the Unit Builder's entry point (cm_module.c calls cm_builder_init at dsl_plugin_init): the home page entry
 * "Unit Builder" (owner v12 14: on the title's Custom Modes page, not in the Mods window) and the test hooks. */
#include "bld.h"
#include "cm_home.h"
#include "mu_ui.h"

static void open_home(void) { cm_builder_open(NULL, NULL, NULL); }

void cm_builder_init(const DSLHost *host, DSLPlugin *self) {
    mu_ui_init(host, self);           /* the My Units window kit (same look); idempotent */
    if (cm_home_add("unit-builder", "Unit Builder", NULL, 20, open_home)) host->log(self, "Unit Builder: home entry refused");
    cm_test_register("bld", bld_test_find, bld_test_state);
    host->log(self, "Unit Builder ready (home entry, order 20)");
}
