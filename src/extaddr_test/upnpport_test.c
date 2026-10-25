/* upnpport_test - the router's port-mapping answers (canned, as the owner's router sends them). Breaks it catches: a
 * mapping list we misread or a wrong pick (the lobby shows a port that reaches another computer or nothing, or a duplicate
 * mapping added over one that already works), a refusal code we miss (no retry on a taken port). Documentation addresses only. */
#include "upnpport.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
void rig_log(const char *fmt, ...) { (void)fmt; }
static int fails, n;
#define CHECK(c) do { n++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
static char *entry(char *b, size_t cap, const char *proto, int ext, const char *client, int in, int en, const char *desc) {
    snprintf(b, cap, "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>\n"
             "<u:GetGenericPortMappingEntryResponse xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">\n"
             "<NewRemoteHost></NewRemoteHost>\n<NewExternalPort>%d</NewExternalPort>\n<NewProtocol>%s</NewProtocol>\n"
             "<NewInternalPort>%d</NewInternalPort>\n<NewInternalClient>%s</NewInternalClient>\n<NewEnabled>%d</NewEnabled>\n"
             "<NewPortMappingDescription>%s</NewPortMappingDescription>\n<NewLeaseDuration>68400</NewLeaseDuration>\n"
             "</u:GetGenericPortMappingEntryResponse>\n</s:Body> </s:Envelope>", ext, proto, in, client, en, desc);
    return b;
}
int main(void) {
    const char *me = "192.168.1.20", *other = "192.168.1.31";
    char b[2048]; upnpp_map m[6]; memset(m, 0, sizeof m);
    /* the list as the router sends it: VPN UDP mapping, another computer's 47777, ours 12345 -> 47777 */
    CHECK(upnpport_parse_entry(entry(b, sizeof b, "UDP", 53267, me, 41641, 1, "vpn-portmap"), &m[0]));
    CHECK(upnpport_parse_entry(entry(b, sizeof b, "TCP", 47777, other, 47777, 1, "Dungeon Settlers host"), &m[1]));
    CHECK(upnpport_parse_entry(entry(b, sizeof b, "TCP", 12345, me, 47777, 1, "Dungeon Settlers host"), &m[2]));
    CHECK(m[2].ext == 12345 && m[2].in == 47777 && !strcmp(m[2].client, me) && !strcmp(m[2].proto, "TCP") && m[2].enabled
          && m[2].lease == 68400 && !strcmp(m[2].desc, "Dungeon Settlers host"));
    /* hosting on 47777: our mapping, never the other computer's one, never the UDP one; another port: none (add one) */
    CHECK(upnpport_choose(m, 3, me, 47777) == 2);
    CHECK(upnpport_choose(m, 3, me, 50000) == -1);
    /* switched off on the router: never used */
    CHECK(upnpport_parse_entry(entry(b, sizeof b, "TCP", 12346, me, 47777, 0, "x"), &m[4]) && !m[4].enabled);
    CHECK(upnpport_choose(&m[4], 1, me, 47777) == -1);
    /* GetSpecificPortMappingEntry answer (no external port / protocol in it): the internal side only */
    upnpp_map s; memset(&s, 0, sizeof s);
    CHECK(upnpport_parse_entry("<s:Envelope><s:Body><u:GetSpecificPortMappingEntryResponse xmlns:u=\"x\"><NewInternalPort>47777</NewInternalPort>"
                               "<NewInternalClient>192.168.1.20</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>Dungeon Settlers host"
                               "</NewPortMappingDescription><NewLeaseDuration>86400</NewLeaseDuration></u:GetSpecificPortMappingEntryResponse></s:Body></s:Envelope>", &s)
          && s.in == 47777 && !strcmp(s.client, me) && s.lease == 86400);
    /* namespaced tags (some routers) */
    CHECK(upnpport_parse_entry("<m:R><m:NewProtocol>TCP</m:NewProtocol><m:NewExternalPort>40001</m:NewExternalPort><m:NewInternalPort>40001</m:NewInternalPort>"
                               "<m:NewInternalClient>192.168.1.20</m:NewInternalClient><m:NewEnabled>1</m:NewEnabled></m:R>", &s) && s.ext == 40001 && s.in == 40001);
    /* SOAP faults: end of the list, port taken; never read as an entry */
    const char *fault = "<s:Envelope><s:Body><s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>"
                        "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>718</errorCode><errorDescription>ConflictInMappingEntry"
                        "</errorDescription></UPnPError></detail></s:Fault></s:Body></s:Envelope>";
    CHECK(upnpport_soap_error(fault) == 718 && !upnpport_parse_entry(fault, &s));
    CHECK(upnpport_soap_error("<UPnPError><errorCode>713</errorCode></UPnPError>") == 713);
    CHECK(upnpport_soap_error(entry(b, sizeof b, "TCP", 1, me, 2000, 1, "x")) == 0);
    printf("== %d passed, %d failed\n", n - fails, fails);
    return fails != 0;
}
