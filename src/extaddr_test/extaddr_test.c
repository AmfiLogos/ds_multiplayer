/* extaddr_test - the lobby's internet address parsing. Breaks it catches: a router answer we misread (no internet line for
 * the host), a provider-NAT address shown as joinable (friends type an address that never works). Documentation addresses only. */
#include "extaddr.h"
#include <stdio.h>
#include <string.h>
static int fails, n;
#define CHECK(c) do { n++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
static unsigned ip(int a, int b, int c, int d) { return (unsigned)a << 24 | b << 16 | c << 8 | d; }
int main(void) {
    /* private / provider NAT: never shown as the internet address */
    CHECK(extaddr_is_private(ip(10, 1, 2, 3)));
    CHECK(extaddr_is_private(ip(172, 16, 0, 1)) && extaddr_is_private(ip(172, 31, 255, 255)) && !extaddr_is_private(ip(172, 32, 0, 1)));
    CHECK(extaddr_is_private(ip(192, 168, 1, 1)) && !extaddr_is_private(ip(192, 169, 0, 1)));
    CHECK(extaddr_is_private(ip(100, 64, 0, 1)) && extaddr_is_private(ip(100, 127, 255, 255)) && !extaddr_is_private(ip(100, 128, 0, 1)));
    CHECK(!extaddr_is_private(ip(203, 0, 113, 5)) && !extaddr_is_private(ip(198, 51, 100, 7)));
    /* SSDP answer: header names in any case */
    char u[256];
    CHECK(extaddr_ssdp_location("HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nlocation: http://192.168.1.1:49153/rootDesc.xml\r\nST: x\r\n\r\n", u, sizeof u)
          && !strcmp(u, "http://192.168.1.1:49153/rootDesc.xml"));
    CHECK(!extaddr_ssdp_location("HTTP/1.1 200 OK\r\nST: x\r\n\r\n", u, sizeof u));
    /* device XML: the WAN connection service's controlURL, not the one of the service before it */
    const char *xml = "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType>"
                      "<controlURL>/ctl/CmnIfCfg</controlURL></service></serviceList><deviceList><device><serviceList><service>"
                      "<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><serviceId>x</serviceId>"
                      "<controlURL>/ctl/IPConn</controlURL></service></serviceList></device></deviceList></device></root>";
    char cu[256], svc[160];
    CHECK(extaddr_control_url(xml, "http://192.168.1.1:49153/rootDesc.xml", cu, sizeof cu, svc, sizeof svc)
          && !strcmp(cu, "http://192.168.1.1:49153/ctl/IPConn") && !strcmp(svc, "urn:schemas-upnp-org:service:WANIPConnection:1"));
    const char *ppp = "<root><URLBase>http://192.168.0.254:5000</URLBase><service><serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>"
                      "<controlURL>upnp/control/WANPPPConn1</controlURL></service></root>";
    CHECK(extaddr_control_url(ppp, "http://192.168.0.254:80/desc.xml", cu, sizeof cu, svc, sizeof svc) && !strcmp(cu, "http://192.168.0.254:5000/upnp/control/WANPPPConn1"));
    CHECK(!extaddr_control_url("<root><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType><controlURL>/a</controlURL></root>",
                               "http://192.168.1.1/", cu, sizeof cu, svc, sizeof svc));
    /* SOAP answer */
    char a[64];
    CHECK(extaddr_soap_ip("<s:Envelope><s:Body><u:GetExternalIPAddressResponse xmlns:u=\"x\"><NewExternalIPAddress>203.0.113.5</NewExternalIPAddress>"
                          "</u:GetExternalIPAddressResponse></s:Body></s:Envelope>", a, sizeof a) && !strcmp(a, "203.0.113.5"));
    CHECK(!extaddr_soap_ip("<NewExternalIPAddress></NewExternalIPAddress>", a, sizeof a));
    printf("== %d passed, %d failed\n", n - fails, fails);
    return fails != 0;
}
