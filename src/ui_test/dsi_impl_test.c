/* Test builds only (ui_test, teams_test): the string table code of ds_i18n.h. The dylib gets its one
   DSI_IMPLEMENTATION from the menu lane's file, so this file is never in src/build.sh or the Windows build. */
#define DSI_IMPLEMENTATION
#include "ds_i18n.h"
