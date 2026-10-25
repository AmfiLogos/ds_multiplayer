/* Test copies only (rig.py start inserts it after dsrig.dylib; never shipped): the port a copy WITHOUT the loader
   listens on when it hosts from the Multiplayer menu.
   src/mpmenu.h mpm_host_port() asks mp_setting_int_fn("host_port"), which only the loader's multiplayer glue sets, so a
   plain rig copy hosted on 47777: the owner's game port, where every lane's menu host met (2026-10-25). With
   DSRIG_HOST_PORT=<the run's port> (rigports.py) this sets the pointer when nothing else did; other ids keep dflt.
   Build: cc -dynamiclib -O2 -o rig/tools/hostport_shim.dylib rig/hostport_shim.c (rig.py does it when missing). */
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

static int run_port(const char *id, int dflt) {
    const char *p = getenv("DSRIG_HOST_PORT");
    return id && !strcmp(id, "host_port") && p && *p ? atoi(p) : dflt;
}

__attribute__((constructor)) static void hostport_shim(void) {
    const char *p = getenv("DSRIG_HOST_PORT");
    if (!p || !*p) return;
    int (**fn)(const char *, int) = (int (**)(const char *, int))dlsym(RTLD_DEFAULT, "mp_setting_int_fn");
    if (fn && !*fn) *fn = run_port;
}
