/* extaddr.h - the host's internet address for the lobby (owner report 2026-10-20: the lobby showed only the local one).
 * One background lookup per host session, never on the game's main thread:
 *   1. UPnP: SSDP search for the router (InternetGatewayDevice), its device XML, WANIPConnection / WANPPPConnection
 *      GetExternalIPAddress. Read only here: the port mapping is upnpport.c.
 *   2. No UPnP answer: one HTTPS GET of https://api.ipify.org (plain text). Mac /usr/bin/curl, Windows WinHTTP, 3 s.
 * A router address in 10/8, 172.16/12, 192.168/16 or 100.64/10 (carrier NAT) is not reachable from outside: state
 * EXTADDR_PRIVATE, the lobby says so and does not show it.
 * Test switches (env): DSRIG_EXTADDR_UPNP=0 (skip UPnP), DSRIG_EXTADDR_WEB=0 (skip the web), DSRIG_EXTADDR_WEB_URL=<url>. */
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { EXTADDR_IDLE, EXTADDR_PENDING, EXTADDR_OK, EXTADDR_PRIVATE, EXTADDR_UNKNOWN };

void extaddr_start(void);                    /* starts the lookup once (no-op while one runs or has a result) */
void extaddr_reset(void);                    /* host session ended: the next start looks again */
int  extaddr_get(char *ip, size_t cap, const char **via);   /* state; ip = the address (OK / PRIVATE); via "upnp" / "web" */

/* router plumbing, shared with upnpport.c (blocking: call from a worker thread only) */
int  extaddr_igd_find(char *ctl, size_t ctl_cap, char *svc, size_t svc_cap);   /* SSDP + device XML; 1 = router found */
int  extaddr_http(const char *url, const char *extra_hdr, const char *body, char *out, size_t cap, int ms);   /* HTTP code, 0 = none */
int  extaddr_soap(const char *ctl, const char *svc, const char *action, const char *args, char *out, size_t cap, int ms);   /* HTTP code */

/* pure helpers (host test src/extaddr_test) */
int  extaddr_is_private(uint32_t ip);        /* host byte order */
int  extaddr_ssdp_location(const char *resp, char *url, size_t cap);
int  extaddr_control_url(const char *xml, const char *base, char *url, size_t cap, char *svc, size_t svc_cap);
int  extaddr_soap_ip(const char *resp, char *ip, size_t cap);
