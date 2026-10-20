/* upnpport.h - the host's router port for friends outside the home (owner 2026-10-20: most players cannot set up port
 * forwarding; the game finds a port that works and shows it, instead of assuming 47777).
 * A computer cannot see from inside which ports its router forwards, except through the router's UPnP service
 * (Internet Gateway Device, WANIPConnection / WANPPPConnection). All router talk runs on one worker thread, never on a
 * game frame:
 *   1. Hosting on port P (the Host port setting, default 47777): the router's mapping list is read; a listed mapping P -> this computer = UPNPP_ALREADY (shown, never touched). Else AddPortMapping
 *      external P (taken: P+1..P+4, then 3 more) -> this computer:P, description "Dungeon Settlers host", lease 86400 s
 *      (the router's usual cap; error 725 = permanent only: lease 0), checked with GetSpecificPortMappingEntry =
 *      UPNPP_OPENED. Renewed at half the lease while hosting; deleted when the session ends, on Application.Quit and (Mac) at exit. Only a mapping
 *      this process added is ever deleted.
 *   2. No router answer / refused = UPNPP_FAILED; switched off = UPNPP_OFF.
 * No outside port-check service. Results are logged in rig.log (ports and states, no addresses).
 * Switches (env): DSRIG_UPNP=0 off, =1 on also in test copies (DSRIG_CTL=1 / DSRIG_PORT turn it off by default, so rig
 * runs never touch the router); DSRIG_UPNP_LEASE=<s> (test: short lease to see renewals); DSRIG_UPNP_SKIP_EXISTING=1
 * (test: ignore listed mappings, always add). */
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { UPNPP_IDLE, UPNPP_PENDING, UPNPP_OPENED, UPNPP_ALREADY, UPNPP_FAILED, UPNPP_OFF };

void     upnpport_host(uint16_t port);            /* hosting on port (0 = not hosting: our mapping is deleted) */
int      upnpport_get(uint16_t *ext);             /* state; *ext = the external port friends type (OPENED / ALREADY) */
void     upnpport_quit(void);                     /* the app quits: delete our mapping now (blocks up to 2 s) */

/* pure helpers (host test src/extaddr_test/upnpport_test.c) */
typedef struct { char proto[8]; uint16_t ext, in; char client[48]; int enabled; char desc[96]; unsigned lease; } upnpp_map;
int upnpport_parse_entry(const char *soap, upnpp_map *m);          /* Get*PortMappingEntry answer; 1 = an entry */
int upnpport_soap_error(const char *body);                          /* UPnP <errorCode>, 0 = none */
/* a listed mapping that already forwards to lan:want (TCP, enabled, internal port >= 1024); -1 = none */
int upnpport_choose(const upnpp_map *m, int n, const char *lan, uint16_t want);
