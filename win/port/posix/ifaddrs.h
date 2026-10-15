/* shim: minimal getifaddrs over GetAdaptersAddresses (IPv4 unicast, up, non-loopback); names are "en0"-style
   only for the first adapter so mpmenu's "prefer en0" pass picks the main LAN address. */
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
    struct ifaddrs **tail = out; int k = 0;
    for (IP_ADAPTER_ADDRESSES *a = aa; a; a = a->Next)
        for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            struct ifaddrs *i = calloc(1, sizeof *i + sizeof(struct sockaddr_in) + 16);
            i->ifa_addr = (struct sockaddr *)(i + 1); memcpy(i->ifa_addr, u->Address.lpSockaddr, sizeof(struct sockaddr_in));
            i->ifa_name = (char *)i->ifa_addr + sizeof(struct sockaddr_in); snprintf(i->ifa_name, 16, "en%d", k++);
            i->ifa_flags = (a->OperStatus == IfOperStatusUp ? IFF_UP : 0) | (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK ? IFF_LOOPBACK : 0);
            *tail = i; tail = &i->ifa_next;
        }
    free(aa); return 0;
}
static inline void freeifaddrs(struct ifaddrs *i) { while (i) { struct ifaddrs *n = i->ifa_next; free(i); i = n; } }
