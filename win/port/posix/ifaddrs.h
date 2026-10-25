/* shim: minimal getifaddrs over GetAdaptersAddresses (IPv4 unicast, up, non-loopback). Names follow the Mac's kinds so
   ui_addr_kind (the host's address lines) and mpmenu's "prefer en0" pass work: real adapters "en0", "en1" ... in the
   system's order, VPN / tunnel adapters "utun<n>", virtual ones (VMware, VirtualBox, Hyper-V) "vmnet<n>". */
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define IFF_UP 0x1
#define IFF_LOOPBACK 0x8
struct ifaddrs { struct ifaddrs *ifa_next; char *ifa_name; unsigned ifa_flags; struct sockaddr *ifa_addr; };
static inline int getifaddrs(struct ifaddrs **out) {
    ULONG sz = 16384; IP_ADAPTER_ADDRESSES *aa = malloc(sz); *out = NULL;
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, aa, &sz) != NO_ERROR) { free(aa); return -1; }
    struct ifaddrs **tail = out; int k = 0, kv = 0, kt = 0;
    static const wchar_t *const tun[] = { L"TAP-", L"Tunnel", L"WireGuard", L"Tailscale", L"ZeroTier", L"Hamachi", L"OpenVPN", L"VPN" };
    static const wchar_t *const virt[] = { L"Virtual", L"VMware", L"VirtualBox", L"Hyper-V", L"Docker", L"WSL", L"Loopback" };
    for (IP_ADAPTER_ADDRESSES *a = aa; a; a = a->Next) {
        int kind = a->IfType == IF_TYPE_PPP || a->IfType == IF_TYPE_TUNNEL ? 2 : 0;
        for (size_t t = 0; !kind && a->Description && t < sizeof tun / sizeof tun[0]; t++) if (wcsstr(a->Description, tun[t])) kind = 2;
        for (size_t t = 0; !kind && a->Description && t < sizeof virt / sizeof virt[0]; t++) if (wcsstr(a->Description, virt[t])) kind = 1;
        for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            struct ifaddrs *i = calloc(1, sizeof *i + sizeof(struct sockaddr_in) + 16);
            i->ifa_addr = (struct sockaddr *)(i + 1); memcpy(i->ifa_addr, u->Address.lpSockaddr, sizeof(struct sockaddr_in));
            i->ifa_name = (char *)i->ifa_addr + sizeof(struct sockaddr_in);
            if (kind == 2) snprintf(i->ifa_name, 16, "utun%d", kt++); else if (kind == 1) snprintf(i->ifa_name, 16, "vmnet%d", kv++); else snprintf(i->ifa_name, 16, "en%d", k++);
            i->ifa_flags = (a->OperStatus == IfOperStatusUp ? IFF_UP : 0) | (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK ? IFF_LOOPBACK : 0);
            *tail = i; tail = &i->ifa_next;
        }
    }
    free(aa); return 0;
}
static inline void freeifaddrs(struct ifaddrs *i) { while (i) { struct ifaddrs *n = i->ifa_next; free(i); i = n; } }
