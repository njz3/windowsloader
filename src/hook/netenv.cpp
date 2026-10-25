// windowsloader - runs Windows arcade games with emulated cabinet hardware
// Copyright (C) 2026 Benjamin Maurin - njz3
// License: GNU General Public License version 3 (GPL v3)
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
// file for the full text. The author reserves the right to change the license of this code.
//
// Code taken from MAME, linuxloader, sm2-emu, Flycast or segatools stays under the license of
// its own project; such code is marked with its origin.

// The PC's network cards as the game sees them (iphlpapi). By default the PC's own, the
// GetAdaptersInfo calls logged (some games check a MAC address). A game's module can put a
// cabinet's card in their place (wl_netenv_fake): one Ethernet card in the cabinet's subnet, its
// gateway its DNS server, answering pings, the way segatools' platform/netenv.c (Unlicense) gives
// it to an ALLS's amdaemon, which checks the cabinet's network so.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hooks.h"
#include "log.h"

static struct
{
    int on;
    ULONG ip, gateway; // network order
    BYTE mac[6];
} g_fake;

void wl_netenv_fake(unsigned ip, unsigned gateway, const unsigned char mac[6])
{
    g_fake.ip = _byteswap_ulong(ip);
    g_fake.gateway = _byteswap_ulong(gateway);
    memcpy(g_fake.mac, mac, 6);
    g_fake.on = 1;
    wl_log("network: the cabinet's card, %u.%u.%u.%u/24 gateway %u.%u.%u.%u, in place of the PC's", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255,
           gateway >> 24, gateway >> 16 & 255, gateway >> 8 & 255, gateway & 255);
}

static void ip_text(ULONG ip, char *out, size_t size)
{
    const unsigned char *b = (const unsigned char *)&ip;
    snprintf(out, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

static void first_call(const char *what)
{
    static const char *seen[16];
    static int count;
    for (int i = 0; i < count; i++)
        if (seen[i] == what)
            return;
    if (count < 16)
        seen[count++] = what;
    wl_log("network: %s answered with the cabinet's card", what);
}

static ULONG(WINAPI *oGetAdaptersInfo)(PIP_ADAPTER_INFO, PULONG);
static ULONG WINAPI hGetAdaptersInfo(PIP_ADAPTER_INFO info, PULONG size)
{
    if (g_fake.on && !wl_busy())
    {
        first_call("GetAdaptersInfo");
        if (!size)
            return ERROR_INVALID_PARAMETER;
        if (!info || *size < sizeof(IP_ADAPTER_INFO))
        {
            *size = sizeof(IP_ADAPTER_INFO);
            return ERROR_BUFFER_OVERFLOW;
        }
        memset(info, 0, sizeof *info);
        strcpy_s(info->AdapterName, sizeof info->AdapterName, "{00000000-0000-0000-0000-000000000000}");
        strcpy_s(info->Description, sizeof info->Description, "Cabinet Ethernet");
        info->AddressLength = 6;
        memcpy(info->Address, g_fake.mac, 6);
        info->Index = 1;
        info->Type = MIB_IF_TYPE_ETHERNET;
        info->DhcpEnabled = 1;
        ip_text(g_fake.ip, info->IpAddressList.IpAddress.String, sizeof info->IpAddressList.IpAddress.String);
        strcpy_s(info->IpAddressList.IpMask.String, sizeof info->IpAddressList.IpMask.String, "255.255.255.0");
        ip_text(g_fake.gateway, info->GatewayList.IpAddress.String, sizeof info->GatewayList.IpAddress.String);
        strcpy_s(info->GatewayList.IpMask.String, sizeof info->GatewayList.IpMask.String, "255.255.255.0");
        ip_text(g_fake.gateway, info->DhcpServer.IpAddress.String, sizeof info->DhcpServer.IpAddress.String);
        time_t now = time(NULL);
        info->LeaseObtained = now - 3600;
        info->LeaseExpires = now + 86400;
        return ERROR_SUCCESS;
    }
    int nested = wl_busy();
    wl_busy_enter();
    ULONG r = oGetAdaptersInfo(info, size);
    wl_busy_leave();
    if (!nested && !wl_is_system_caller(WL_CALLER))
        wl_log("GetAdaptersInfo(%s) -> %lu, size %lu", info ? "buffer" : "size query", r, size ? *size : 0);
    return r;
}

// One card, its address, gateway and DNS server, its names, all in the caller's buffer
struct FakeAddresses
{
    IP_ADAPTER_ADDRESSES a;
    IP_ADAPTER_UNICAST_ADDRESS unicast;
    IP_ADAPTER_GATEWAY_ADDRESS gateway;
    IP_ADAPTER_DNS_SERVER_ADDRESS dns;
    SOCKADDR_IN ip, gw, dnsIp;
    char name[40];
    wchar_t friendly[24], description[24], suffix[8];
};

static ULONG(WINAPI *oGetAdaptersAddresses)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);
static ULONG WINAPI hGetAdaptersAddresses(ULONG family, ULONG flags, PVOID reserved, PIP_ADAPTER_ADDRESSES out, PULONG size)
{
    if (!g_fake.on || wl_busy())
        return oGetAdaptersAddresses(family, flags, reserved, out, size);
    first_call("GetAdaptersAddresses");
    if (!size)
        return ERROR_INVALID_PARAMETER;
    if (family == AF_INET6)
        return ERROR_NO_DATA;
    if (!out || *size < sizeof(FakeAddresses))
    {
        *size = sizeof(FakeAddresses);
        return ERROR_BUFFER_OVERFLOW;
    }
    FakeAddresses *f = (FakeAddresses *)out;
    memset(f, 0, sizeof *f);
    strcpy_s(f->name, sizeof f->name, "{00000000-0000-0000-0000-000000000000}");
    wcscpy_s(f->friendly, 24, L"Cabinet Ethernet");
    wcscpy_s(f->description, 24, L"Cabinet Ethernet");
    wcscpy_s(f->suffix, 8, L"local");
    f->ip.sin_family = f->gw.sin_family = f->dnsIp.sin_family = AF_INET;
    f->ip.sin_addr.s_addr = g_fake.ip;
    f->gw.sin_addr.s_addr = f->dnsIp.sin_addr.s_addr = g_fake.gateway;
    f->unicast.Length = sizeof f->unicast;
    f->unicast.Address.lpSockaddr = (SOCKADDR *)&f->ip;
    f->unicast.Address.iSockaddrLength = sizeof f->ip;
    f->unicast.PrefixOrigin = IpPrefixOriginDhcp;
    f->unicast.SuffixOrigin = IpSuffixOriginDhcp;
    f->unicast.DadState = IpDadStatePreferred;
    f->unicast.ValidLifetime = f->unicast.PreferredLifetime = 0xFFFFFFFF;
    f->unicast.LeaseLifetime = 86400;
    f->unicast.OnLinkPrefixLength = 24;
    f->gateway.Length = sizeof f->gateway;
    f->gateway.Address.lpSockaddr = (SOCKADDR *)&f->gw;
    f->gateway.Address.iSockaddrLength = sizeof f->gw;
    f->dns.Length = sizeof f->dns;
    f->dns.Address.lpSockaddr = (SOCKADDR *)&f->dnsIp;
    f->dns.Address.iSockaddrLength = sizeof f->dnsIp;
    IP_ADAPTER_ADDRESSES *a = &f->a;
    a->Length = sizeof *a;
    a->IfIndex = 1;
    a->AdapterName = f->name;
    a->FirstUnicastAddress = &f->unicast;
    a->FirstDnsServerAddress = &f->dns;
    a->FirstGatewayAddress = (flags & GAA_FLAG_INCLUDE_GATEWAYS) ? &f->gateway : NULL;
    a->DnsSuffix = f->suffix;
    a->Description = f->description;
    a->FriendlyName = f->friendly;
    memcpy(a->PhysicalAddress, g_fake.mac, 6);
    a->PhysicalAddressLength = 6;
    a->Flags = IP_ADAPTER_DHCP_ENABLED | IP_ADAPTER_IPV4_ENABLED;
    a->Mtu = 1500;
    a->IfType = IF_TYPE_ETHERNET_CSMACD;
    a->OperStatus = IfOperStatusUp;
    a->TransmitLinkSpeed = a->ReceiveLinkSpeed = 1000000000;
    return ERROR_SUCCESS;
}

static void fake_row(MIB_IFROW *row)
{
    memset(row, 0, sizeof *row);
    wcscpy_s(row->wszName, MAX_INTERFACE_NAME_LEN, L"Cabinet Ethernet");
    row->dwIndex = 1;
    row->dwType = IF_TYPE_ETHERNET_CSMACD;
    row->dwMtu = 1500;
    row->dwSpeed = 1000000000;
    row->dwPhysAddrLen = 6;
    memcpy(row->bPhysAddr, g_fake.mac, 6);
    row->dwAdminStatus = 1;
    row->dwOperStatus = IF_OPER_STATUS_OPERATIONAL;
}

static DWORD(WINAPI *oGetIfTable)(PMIB_IFTABLE, PULONG, BOOL);
static DWORD WINAPI hGetIfTable(PMIB_IFTABLE table, PULONG size, BOOL order)
{
    if (!g_fake.on || wl_busy())
        return oGetIfTable(table, size, order);
    first_call("GetIfTable");
    if (!size)
        return ERROR_INVALID_PARAMETER;
    if (!table || *size < sizeof(MIB_IFTABLE))
    {
        *size = sizeof(MIB_IFTABLE);
        return ERROR_INSUFFICIENT_BUFFER;
    }
    table->dwNumEntries = 1;
    fake_row(&table->table[0]);
    return NO_ERROR;
}

static DWORD(WINAPI *oGetIfEntry)(PMIB_IFROW);
static DWORD WINAPI hGetIfEntry(PMIB_IFROW row)
{
    if (!g_fake.on || wl_busy())
        return oGetIfEntry(row);
    first_call("GetIfEntry");
    if (!row || row->dwIndex != 1)
        return ERROR_INVALID_DATA;
    fake_row(row);
    return NO_ERROR;
}

static DWORD(WINAPI *oGetBestRoute)(DWORD, DWORD, PMIB_IPFORWARDROW);
static DWORD WINAPI hGetBestRoute(DWORD dest, DWORD source, PMIB_IPFORWARDROW route)
{
    if (!g_fake.on || wl_busy())
        return oGetBestRoute(dest, source, route);
    first_call("GetBestRoute");
    if (!route)
        return ERROR_INVALID_PARAMETER;
    memset(route, 0, sizeof *route);
    route->dwForwardDest = 0;
    route->dwForwardMask = 0;
    route->dwForwardNextHop = g_fake.gateway;
    route->dwForwardIfIndex = 1;
    route->dwForwardType = MIB_IPROUTE_TYPE_INDIRECT;
    route->dwForwardProto = MIB_IPPROTO_NETMGMT;
    return NO_ERROR;
}

// The card's address, its interface and the default route (Wangan Midnight 5 looks for its
// address in the cabinet's subnet)
static DWORD(WINAPI *oGetIpAddrTable)(PMIB_IPADDRTABLE, PULONG, BOOL);
static DWORD WINAPI hGetIpAddrTable(PMIB_IPADDRTABLE table, PULONG size, BOOL order)
{
    if (!g_fake.on || wl_busy())
        return oGetIpAddrTable(table, size, order);
    first_call("GetIpAddrTable");
    if (!size)
        return ERROR_INVALID_PARAMETER;
    if (!table || *size < sizeof(MIB_IPADDRTABLE))
    {
        *size = sizeof(MIB_IPADDRTABLE);
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memset(table, 0, sizeof(MIB_IPADDRTABLE));
    table->dwNumEntries = 1;
    MIB_IPADDRROW *row = &table->table[0];
    row->dwAddr = g_fake.ip;
    row->dwIndex = 1;
    row->dwMask = _byteswap_ulong(0xFFFFFF00);
    row->dwBCastAddr = 1;
    row->dwReasmSize = 65535;
    row->wType = MIB_IPADDR_PRIMARY;
    return NO_ERROR;
}

static DWORD(WINAPI *oGetInterfaceInfo)(PIP_INTERFACE_INFO, PULONG);
static DWORD WINAPI hGetInterfaceInfo(PIP_INTERFACE_INFO info, PULONG size)
{
    if (!g_fake.on || wl_busy())
        return oGetInterfaceInfo(info, size);
    first_call("GetInterfaceInfo");
    if (!size)
        return ERROR_INVALID_PARAMETER;
    if (!info || *size < sizeof(IP_INTERFACE_INFO))
    {
        *size = sizeof(IP_INTERFACE_INFO);
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memset(info, 0, sizeof(IP_INTERFACE_INFO));
    info->NumAdapters = 1;
    info->Adapter[0].Index = 1;
    wcscpy_s(info->Adapter[0].Name, MAX_ADAPTER_NAME, L"\\DEVICE\\TCPIP_{57494E44-4F57-534C-4F41-444552000001}");
    return NO_ERROR;
}

static DWORD(WINAPI *oGetIpForwardTable)(PMIB_IPFORWARDTABLE, PULONG, BOOL);
static DWORD WINAPI hGetIpForwardTable(PMIB_IPFORWARDTABLE table, PULONG size, BOOL order)
{
    if (!g_fake.on || wl_busy())
        return oGetIpForwardTable(table, size, order);
    first_call("GetIpForwardTable");
    if (!size)
        return ERROR_INVALID_PARAMETER;
    if (!table || *size < sizeof(MIB_IPFORWARDTABLE))
    {
        *size = sizeof(MIB_IPFORWARDTABLE);
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memset(table, 0, sizeof(MIB_IPFORWARDTABLE));
    table->dwNumEntries = 1;
    MIB_IPFORWARDROW *route = &table->table[0];
    route->dwForwardNextHop = g_fake.gateway;
    route->dwForwardIfIndex = 1;
    route->dwForwardType = MIB_IPROUTE_TYPE_INDIRECT;
    route->dwForwardProto = MIB_IPPROTO_NETMGMT;
    return NO_ERROR;
}

int wl_netenv_fake_ip(unsigned *ip)
{
    if (g_fake.on && ip)
        *ip = g_fake.ip;
    return g_fake.on;
}

// A ping: answered at once, as by every host of the cabinet's network; nothing is sent
static int echo_reply(IPAddr dest, LPVOID reply, DWORD replySize)
{
    if (!reply || replySize < sizeof(ICMP_ECHO_REPLY))
    {
        SetLastError(IP_BUF_TOO_SMALL);
        return 0;
    }
    ICMP_ECHO_REPLY *r = (ICMP_ECHO_REPLY *)reply;
    memset(r, 0, sizeof *r);
    r->Address = dest;
    r->Status = IP_SUCCESS;
    r->RoundTripTime = 1;
    r->Reserved = 1; // the replies' count, for IcmpParseReplies
    return 1;
}

static DWORD(WINAPI *oIcmpSendEcho2)(HANDLE, HANDLE, PVOID, PVOID, IPAddr, LPVOID, WORD, PIP_OPTION_INFORMATION, LPVOID, DWORD, DWORD);
static DWORD WINAPI hIcmpSendEcho2(HANDLE icmp, HANDLE event, PVOID apc, PVOID context, IPAddr dest, LPVOID data, WORD dataSize,
                                   PIP_OPTION_INFORMATION options, LPVOID reply, DWORD replySize, DWORD timeout)
{
    if (!g_fake.on || wl_busy())
        return oIcmpSendEcho2(icmp, event, apc, context, dest, data, dataSize, options, reply, replySize, timeout);
    first_call("IcmpSendEcho2");
    if (apc)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (!echo_reply(dest, reply, replySize))
        return 0;
    if (event)
    {
        SetEvent(event);
        SetLastError(ERROR_IO_PENDING);
        return 0;
    }
    return 1;
}

// The same, waited for (Unreal Engine's FIcmp: the game's check of the router)
static DWORD(WINAPI *oIcmpSendEcho)(HANDLE, IPAddr, LPVOID, WORD, PIP_OPTION_INFORMATION, LPVOID, DWORD, DWORD);
static DWORD WINAPI hIcmpSendEcho(HANDLE icmp, IPAddr dest, LPVOID data, WORD dataSize, PIP_OPTION_INFORMATION options, LPVOID reply,
                                  DWORD replySize, DWORD timeout)
{
    if (!g_fake.on || wl_busy())
        return oIcmpSendEcho(icmp, dest, data, dataSize, options, reply, replySize, timeout);
    first_call("IcmpSendEcho");
    return echo_reply(dest, reply, replySize);
}

static WlHook g_hooks[] = {
    WL_HOOK(L"iphlpapi.dll", GetAdaptersInfo),
    WL_HOOK(L"iphlpapi.dll", GetAdaptersAddresses),
    WL_HOOK(L"iphlpapi.dll", GetIfTable),
    WL_HOOK(L"iphlpapi.dll", GetIfEntry),
    WL_HOOK(L"iphlpapi.dll", GetBestRoute),
    WL_HOOK(L"iphlpapi.dll", GetIpAddrTable),
    WL_HOOK(L"iphlpapi.dll", GetInterfaceInfo),
    WL_HOOK(L"iphlpapi.dll", GetIpForwardTable),
    WL_HOOK(L"iphlpapi.dll", IcmpSendEcho2),
    WL_HOOK(L"iphlpapi.dll", IcmpSendEcho),
};

void wl_netenv_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}
