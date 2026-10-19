// The network: the Xbox 360's sockets on the host's, and the secure network
// layer (XNet) as a plain local network.
//
// Only the multiplayer executable imports these (CoD3MP.exe); the single
// player one starts Winsock and nothing more (kernel_objects.cpp). They are
// built into both all the same: nothing calls them in single player.
//
// The console's sockets are BSD sockets with two differences that matter
// here. Every call takes the caller kind as an extra first argument
// (NetDll_socket(caller, af, type, protocol)), and a socket's address is
// guest memory: big endian where the host is little, so sin_family is
// swapped on the way in and out, while the port and the address are in
// network order on both and copy as they are. Option values and ioctl
// arguments are guest words, big endian too.
//
// Games talk over VDP, the console's protocol 254: UDP with the game's
// data and its voice in one datagram. Both ends here are this runtime, so a
// VDP socket is a UDP socket and the datagram goes as the title wrote it.
//
// XNet is what makes an Xbox address out of an IP one: on the console a
// secure association, keyed by the session. On a local network it is the
// identity: an XNADDR carries the IP address of the machine that made it,
// and turning it back into an IN_ADDR gives that address. That is enough for
// System Link, which is what the title uses between consoles on one network.
// COD3_LANIP=a.b.c.d picks the address this machine gives out (a Hamachi or
// other VPN adapter's, say); otherwise the first IPv4 address that is not
// the loopback, preferring a private network's.

// Winsock before anything that brings in Windows.h, which would otherwise
// bring in the old winsock.h and clash with it.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "kernel.h"
#include "settings.h"
#include "net.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include <Windows.h>

namespace
{
    // The console's numbers where they differ from Winsock's names.
    constexpr uint32_t XIPPROTO_VDP = 254;

    // For the settings menu: what went out and came in.
    std::atomic<unsigned long long> g_sent{ 0 }, g_received{ 0 };
    std::mutex g_lastFromMutex;
    std::string g_lastFrom;
    std::atomic<uint32_t> g_lastFromAddress{ 0 };   // in_addr's s_addr, for the side channel
    std::string g_addressWhy;

    std::once_flag g_started;
    void StartWinsock()
    {
        std::call_once(g_started, [] {
            WSADATA data{};
            const int result = WSAStartup(MAKEWORD(2, 2), &data);
            printf("net: Winsock %s\n", result == 0 ? "started" : "failed to start");
            fflush(stdout);
        });
    }

    // Guest socket handles: small numbers of this runtime's own, each naming
    // a host socket. The guest compares a socket with INVALID_SOCKET (~0) and
    // nothing else, so any other value will do.
    std::mutex g_socketMutex;
    std::map<uint32_t, SOCKET> g_sockets;
    uint32_t g_nextSocket = 0x100;
    thread_local int t_lastError = 0;

    SOCKET HostSocket(uint32_t guest)
    {
        std::lock_guard<std::mutex> lock(g_socketMutex);
        const auto found = g_sockets.find(guest);
        return found == g_sockets.end() ? INVALID_SOCKET : found->second;
    }

    // The result of a socket call, for the guest: the error, if any, is kept
    // for NetDll_WSAGetLastError.
    uint32_t Result(int value)
    {
        if (value == SOCKET_ERROR)
        {
            t_lastError = WSAGetLastError();
            return uint32_t(SOCKET_ERROR);
        }
        t_lastError = 0;
        return uint32_t(value);
    }

    // A guest sockaddr_in to the host's: the family swapped, the rest copied.
    bool ToHost(uint8_t* base, uint32_t guest, uint32_t length, sockaddr_in& out)
    {
        memset(&out, 0, sizeof(out));
        if (guest == 0 || length < 8) return false;
        out.sin_family = uint16_t(Guest::Read16(base, guest));
        memcpy(&out.sin_port, base + guest + 2, 2);
        memcpy(&out.sin_addr, base + guest + 4, 4);
        return true;
    }

    void ToGuest(uint8_t* base, const sockaddr_in& in, uint32_t guest)
    {
        Guest::Write16(base, guest, uint16_t(in.sin_family));
        memcpy(base + guest + 2, &in.sin_port, 2);
        memcpy(base + guest + 4, &in.sin_addr, 4);
        memset(base + guest + 8, 0, 8);
    }

    std::string Dotted(const in_addr& address)
    {
        char text[32] = {};
        inet_ntop(AF_INET, &address, text, sizeof(text));
        return text;
    }

    // This machine's IPv4 networks, from the adapters that are up.
    //
    // System Link finds games by broadcast, and Windows sends a broadcast to
    // 255.255.255.255 out of one adapter only, the one with the default
    // route. A friend on Hamachi, ZeroTier or Radmin is on another adapter,
    // so a search would never reach them. Each broadcast the title sends
    // goes out of every adapter here instead, to its own network's
    // broadcast address (25.255.255.255 on Hamachi's, say).
    struct Adapter
    {
        in_addr address{};
        in_addr broadcast{};
        uint32_t mask = 0;            // host order
        std::string name;
        bool vpn = false;
        // A network with no room for others (a /32: Tailscale gives each
        // machine one) carries no broadcasts: only the peers list reaches
        // anyone on it.
        bool broadcasts() const { return mask != 0xFFFFFFFFu; }
    };

    const std::vector<Adapter>& Adapters()
    {
        static std::vector<Adapter> adapters;
        static std::once_flag once;
        std::call_once(once, [] {
            ULONG size = 32 * 1024;
            std::vector<uint8_t> buffer(size);
            auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
            const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
            ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size);
            if (result == ERROR_BUFFER_OVERFLOW)
            {
                buffer.resize(size);
                list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
                result = GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size);
            }
            if (result != NO_ERROR) return;
            for (auto* at = list; at != nullptr; at = at->Next)
            {
                if (at->OperStatus != IfOperStatusUp || at->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                char name[256] = {};
                WideCharToMultiByte(CP_UTF8, 0, at->FriendlyName, -1, name, sizeof(name) - 1, nullptr, nullptr);
                char description[256] = {};
                WideCharToMultiByte(CP_UTF8, 0, at->Description, -1, description, sizeof(description) - 1, nullptr, nullptr);
                std::string both = std::string(name) + " " + description;
                for (char& c : both) c = char(tolower(static_cast<unsigned char>(c)));
                for (auto* unicast = at->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
                {
                    if (unicast->Address.lpSockaddr->sa_family != AF_INET) continue;
                    Adapter adapter;
                    adapter.address = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr)->sin_addr;
                    const uint32_t ip = ntohl(adapter.address.s_addr);
                    const ULONG prefix = unicast->OnLinkPrefixLength;
                    const uint32_t mask = prefix == 0 ? 0 : prefix >= 32 ? 0xFFFFFFFFu : ~((1u << (32 - prefix)) - 1);
                    adapter.broadcast.s_addr = htonl(ip | ~mask);
                    adapter.mask = mask;
                    adapter.name = name;
                    adapter.vpn = both.find("hamachi") != std::string::npos || both.find("zerotier") != std::string::npos ||
                                  both.find("radmin") != std::string::npos || both.find("tailscale") != std::string::npos ||
                                  both.find("wireguard") != std::string::npos || (ip >> 24) == 25 || (ip >> 24) == 26;
                    adapters.push_back(adapter);
                        printf("net: adapter \"%s\": %s, %s%s\n", name, Dotted(adapter.address).c_str(),
                        adapter.broadcasts() ? ("broadcast " + Dotted(adapter.broadcast)).c_str() : "no broadcasts (use the peers list)",
                        adapter.vpn ? ", a virtual LAN" : "");
                }
            }
            fflush(stdout);
        });
        return adapters;
    }

    bool IsOwnAddress(in_addr address)
    {
        for (const Adapter& adapter : Adapters())
            if (adapter.address.s_addr == address.s_addr) return true;
        return (ntohl(address.s_addr) >> 24) == 127;
    }

    // The IPv4 address this machine gives out as its own: the one other
    // players are told to send to. COD3_LANIP=a.b.c.d or the settings
    // menu's System Link address chooses; otherwise a virtual LAN's
    // (Hamachi and the like), then a home network's, then any.
    in_addr LocalAddress()
    {
        static in_addr chosen{};
        static std::once_flag once;
        std::call_once(once, [] {
            StartWinsock();
            const char* why = nullptr;
            std::string wanted = Settings::Get().lanAddress;
            if (const char* overridden = getenv("COD3_LANIP")) wanted = overridden;
            if (!wanted.empty() && inet_pton(AF_INET, wanted.c_str(), &chosen) == 1) why = "chosen in the settings (or COD3_LANIP)";
            if (why == nullptr)
            {
                const Adapter* best = nullptr;
                int bestRank = 0;
                for (const Adapter& adapter : Adapters())
                {
                    const uint8_t* b = reinterpret_cast<const uint8_t*>(&adapter.address);
                    const bool isPrivate = b[0] == 10 || (b[0] == 192 && b[1] == 168) || (b[0] == 172 && (b[1] & 0xF0) == 16);
                    const int rank = adapter.vpn && adapter.broadcasts() ? 4 : isPrivate ? 3 : adapter.vpn ? 2 : (b[0] == 169 && b[1] == 254) ? 0 : 1;
                    if (rank > bestRank) { best = &adapter; bestRank = rank; }
                }
                if (best != nullptr) { chosen = best->address; why = best->vpn ? "a virtual LAN" : "this network's"; }
                else { inet_pton(AF_INET, "127.0.0.1", &chosen); why = "no network is up"; }
            }
            g_addressWhy = why;
            printf("net: this machine is %s to System Link (%s)\n", Dotted(chosen).c_str(), why);
            fflush(stdout);
        });
        return chosen;
    }

    // XNADDR: ina, inaOnline, wPortOnline, abEnet[6], abOnline[20].
    constexpr uint32_t XnAddrSize = 36;

    // An XNADDR carries one address, ina. This machine may be on several
    // networks (home, Hamachi, Tailscale...) and the one it gives out may
    // not be one the other machine shares. The online fields are unused on
    // a LAN, so the rest of this machine's addresses go there: inaOnline
    // and the twenty bytes of abOnline, five more. XNetXnAddrToInAddr on
    // the other machine picks the first one on a network it is on too.
    constexpr uint32_t MaxXnAddresses = 7;

    void WriteXnAddr(uint8_t* base, uint32_t guest, in_addr address, bool withOthers = false)
    {
        memset(base + guest, 0, XnAddrSize);
        memcpy(base + guest + 0, &address, 4);
        if (withOthers)
        {
            uint32_t slot = 1;
            for (const Adapter& adapter : Adapters())
            {
                if (adapter.address.s_addr == address.s_addr || slot >= MaxXnAddresses) continue;
                const uint32_t at = slot == 1 ? 4 : 16 + (slot - 2) * 4;   // inaOnline, then abOnline
                memcpy(base + guest + at, &adapter.address, 4);
                slot++;
            }
        }
        // A made up but stable Ethernet address, from the IP one, so two
        // machines never share one.
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&address);
        const uint8_t mac[6] = { 0x00, 0x22, b[0], b[1], b[2], b[3] };
        memcpy(base + guest + 10, mac, 6);
    }

    // Whether an address is on one of this machine's networks. Tailscale's
    // /32s count as its whole range (100.64.0.0/10).
    bool Reachable(in_addr address)
    {
        const uint32_t ip = ntohl(address.s_addr);
        for (const Adapter& adapter : Adapters())
        {
            uint32_t mask = adapter.mask;
            const uint32_t own = ntohl(adapter.address.s_addr);
            if (mask == 0xFFFFFFFFu && (own & 0xFFC00000u) == 0x64400000u) mask = 0xFFC00000u;
            if (mask != 0 && (ip & mask) == (own & mask)) return true;
        }
        return false;
    }

    // The peers list: addresses a broadcast is sent to as well, one by one,
    // for a network that carries no broadcasts (Tailscale) or a friend
    // somewhere a broadcast does not reach. COD3_PEERS or the settings
    // menu's, separated by commas or spaces.
    const std::vector<in_addr>& Peers()
    {
        static std::vector<in_addr> peers;
        static std::once_flag once;
        std::call_once(once, [] {
            std::string text = Settings::Get().systemLinkPeers;
            if (const char* overridden = getenv("COD3_PEERS")) text = overridden;
            for (char& c : text) if (c == ',' || c == ';') c = ' ';
            size_t at = 0;
            while (at < text.size())
            {
                const size_t end = text.find(' ', at);
                const std::string item = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
                at = end == std::string::npos ? text.size() : end + 1;
                in_addr address{};
                if (!item.empty() && inet_pton(AF_INET, item.c_str(), &address) == 1)
                {
                    peers.push_back(address);
                    printf("net: peer %s: System Link searches are sent there too\n", item.c_str());
                }
            }
            fflush(stdout);
        });
        return peers;
    }

    std::mt19937& Random()
    {
        static std::mt19937 generator{ std::random_device{}() };
        return generator;
    }

    void Announce(const char* what)
    {
        static std::mutex mutex;
        static std::map<std::string, int> seen;
        std::lock_guard<std::mutex> lock(mutex);
        if (seen[what]++ < 4)
        {
            printf("net: %s\n", what);
            fflush(stdout);
        }
    }
}

// --- Sockets -----------------------------------------------------------------

// SOCKET NetDll_socket(caller, int af, int type, int protocol)
PPC_FUNC(__imp__NetDll_socket)
{
    Kernel::CountImport("NetDll_socket");
    StartWinsock();
    const int af = int(ctx.r4.u32), type = int(ctx.r5.u32);
    int protocol = int(ctx.r6.u32);
    const bool vdp = uint32_t(protocol) == XIPPROTO_VDP;
    if (vdp) protocol = IPPROTO_UDP;
    const SOCKET host = socket(af, type, protocol);
    if (host == INVALID_SOCKET)
    {
        t_lastError = WSAGetLastError();
        printf("net: socket(%d, %d, %u) failed, error %d\n", af, type, ctx.r6.u32, t_lastError);
        fflush(stdout);
        ctx.r3.u32 = 0xFFFFFFFFu;
        return;
    }
    uint32_t guest;
    {
        std::lock_guard<std::mutex> lock(g_socketMutex);
        guest = g_nextSocket++;
        g_sockets[guest] = host;
    }
    printf("net: socket %u: %s\n", guest, vdp ? "VDP, as UDP" : type == SOCK_DGRAM ? "UDP" : type == SOCK_STREAM ? "TCP" : "other");
    fflush(stdout);
    t_lastError = 0;
    ctx.r3.u32 = guest;
}

// int NetDll_closesocket(caller, SOCKET s)
PPC_FUNC(__imp__NetDll_closesocket)
{
    Kernel::CountImport("NetDll_closesocket");
    SOCKET host = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(g_socketMutex);
        const auto found = g_sockets.find(ctx.r4.u32);
        if (found != g_sockets.end()) { host = found->second; g_sockets.erase(found); }
    }
    ctx.r3.u32 = host == INVALID_SOCKET ? Result(SOCKET_ERROR) : Result(closesocket(host));
}

// int NetDll_bind(caller, SOCKET s, const sockaddr* name, int length)
PPC_FUNC(__imp__NetDll_bind)
{
    Kernel::CountImport("NetDll_bind");
    sockaddr_in address{};
    const SOCKET host = HostSocket(ctx.r4.u32);
    if (host == INVALID_SOCKET || !ToHost(base, ctx.r5.u32, ctx.r6.u32, address))
    {
        t_lastError = WSAENOTSOCK;
        ctx.r3.u32 = uint32_t(SOCKET_ERROR);
        return;
    }
    const int result = bind(host, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    printf("net: socket %u bound to %s:%u%s\n", ctx.r4.u32, Dotted(address.sin_addr).c_str(), ntohs(address.sin_port),
        result == 0 ? "" : " (failed)");
    fflush(stdout);
    ctx.r3.u32 = Result(result);
}

// int NetDll_setsockopt(caller, SOCKET s, int level, int name, const char* value, int length)
PPC_FUNC(__imp__NetDll_setsockopt)
{
    Kernel::CountImport("NetDll_setsockopt");
    const SOCKET host = HostSocket(ctx.r4.u32);
    if (host == INVALID_SOCKET) { t_lastError = WSAENOTSOCK; ctx.r3.u32 = uint32_t(SOCKET_ERROR); return; }
    const int level = int(ctx.r5.u32), name = int(ctx.r6.u32);
    const uint32_t value = ctx.r7.u32, length = ctx.r8.u32;
    int result = 0;
    if (value != 0 && length == 4)
    {
        const int option = int(Guest::Read32(base, value));
        result = setsockopt(host, level, name, reinterpret_cast<const char*>(&option), sizeof(option));
    }
    else if (value != 0 && length == 1)
    {
        const int option = base[value];
        result = setsockopt(host, level, name, reinterpret_cast<const char*>(&option), sizeof(option));
    }
    // An option the host does not know (the console has a few of its own)
    // is accepted and ignored: refusing it would stop the title for nothing.
    if (result == SOCKET_ERROR && WSAGetLastError() == WSAENOPROTOOPT)
    {
        char what[96];
        snprintf(what, sizeof(what), "setsockopt(level 0x%X, option 0x%X) not on the host, ignored", level, name);
        Announce(what);
        result = 0;
    }
    ctx.r3.u32 = Result(result);
}

// int NetDll_ioctlsocket(caller, SOCKET s, long command, u_long* argument)
PPC_FUNC(__imp__NetDll_ioctlsocket)
{
    Kernel::CountImport("NetDll_ioctlsocket");
    const SOCKET host = HostSocket(ctx.r4.u32);
    if (host == INVALID_SOCKET) { t_lastError = WSAENOTSOCK; ctx.r3.u32 = uint32_t(SOCKET_ERROR); return; }
    u_long argument = ctx.r6.u32 != 0 ? u_long(Guest::Read32(base, ctx.r6.u32)) : 0;
    const int result = ioctlsocket(host, long(ctx.r5.u32), &argument);
    if (result == 0 && ctx.r6.u32 != 0) Guest::Write32(base, ctx.r6.u32, uint32_t(argument));
    ctx.r3.u32 = Result(result);
}

// int NetDll_sendto(caller, SOCKET s, const char* buffer, int length, int flags,
//                   const sockaddr* to, int toLength)
PPC_FUNC(__imp__NetDll_sendto)
{
    Kernel::CountImport("NetDll_sendto");
    const SOCKET host = HostSocket(ctx.r4.u32);
    if (host == INVALID_SOCKET) { t_lastError = WSAENOTSOCK; ctx.r3.u32 = uint32_t(SOCKET_ERROR); return; }
    sockaddr_in to{};
    // The seven arguments are r3 to r9: the caller first, so the address
    // is r8 and its length r9. Read one register on (r9 and r10), every
    // sendto had no address and went nowhere, and no System Link search
    // ever left the machine.
    const bool addressed = ToHost(base, ctx.r8.u32, ctx.r9.u32, to);
    // A broadcast goes out of every adapter, to each network's own
    // broadcast address, as well as the way Windows sends it (above).
    if (addressed && to.sin_addr.s_addr == INADDR_BROADCAST)
    {
        static std::atomic<int> shown{ 0 };
        std::vector<in_addr> targets;
        for (const Adapter& adapter : Adapters())
            if (adapter.broadcasts()) targets.push_back(adapter.broadcast);
        for (const in_addr& peer : Peers()) targets.push_back(peer);
        for (const in_addr& target : targets)
        {
            sockaddr_in each = to;
            each.sin_addr = target;
            const int sent = sendto(host, reinterpret_cast<const char*>(base + ctx.r5.u32), int(ctx.r6.u32), int(ctx.r7.u32),
                                    reinterpret_cast<sockaddr*>(&each), sizeof(each));
            if ((shown.load(std::memory_order_relaxed) < 8 && shown.fetch_add(1) < 8))
            {
                printf("net: socket %u broadcast %u bytes to %s:%u%s\n", ctx.r4.u32, ctx.r6.u32,
                    Dotted(each.sin_addr).c_str(), ntohs(each.sin_port), sent == SOCKET_ERROR ? " (failed)" : "");
                fflush(stdout);
            }
        }
    }
    const int result = addressed
        ? sendto(host, reinterpret_cast<const char*>(base + ctx.r5.u32), int(ctx.r6.u32), int(ctx.r7.u32),
                 reinterpret_cast<sockaddr*>(&to), sizeof(to))
        : send(host, reinterpret_cast<const char*>(base + ctx.r5.u32), int(ctx.r6.u32), int(ctx.r7.u32));
    if (result != SOCKET_ERROR) g_sent.fetch_add(1);
    static std::atomic<int> shown{ 0 };
    if ((shown.load(std::memory_order_relaxed) < 12 && shown.fetch_add(1) < 12))
    {
        printf("net: socket %u sent %u bytes to %s:%u%s\n", ctx.r4.u32, ctx.r6.u32,
            addressed ? Dotted(to.sin_addr).c_str() : "its peer", addressed ? ntohs(to.sin_port) : 0,
            result == SOCKET_ERROR ? " (failed)" : "");
        fflush(stdout);
    }
    ctx.r3.u32 = Result(result);
}

// int NetDll_recvfrom(caller, SOCKET s, char* buffer, int length, int flags,
//                     sockaddr* from, int* fromLength)
PPC_FUNC(__imp__NetDll_recvfrom)
{
    Kernel::CountImport("NetDll_recvfrom");
    const SOCKET host = HostSocket(ctx.r4.u32);
    if (host == INVALID_SOCKET) { t_lastError = WSAENOTSOCK; ctx.r3.u32 = uint32_t(SOCKET_ERROR); return; }
    sockaddr_in from{};
    int fromLength = sizeof(from);
    // This socket's own port, to know its own broadcasts when they come
    // back: a console does not hear itself, and a title that did would find
    // its own game in the list, or answer its own search.
    sockaddr_in local{};
    int localLength = sizeof(local);
    getsockname(host, reinterpret_cast<sockaddr*>(&local), &localLength);
    int result;
    for (;;)
    {
        fromLength = sizeof(from);
        result = recvfrom(host, reinterpret_cast<char*>(base + ctx.r5.u32), int(ctx.r6.u32), int(ctx.r7.u32),
                          reinterpret_cast<sockaddr*>(&from), &fromLength);
        if (result == SOCKET_ERROR || from.sin_port != local.sin_port || !IsOwnAddress(from.sin_addr)) break;
    }
    if (result != SOCKET_ERROR)
    {
        g_received.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(g_lastFromMutex);
            g_lastFrom = Dotted(from.sin_addr) + ":" + std::to_string(ntohs(from.sin_port));
        }
        g_lastFromAddress.store(from.sin_addr.s_addr);
        // The sender's address at r8 and its length's word at r9 (the
        // caller is r3), as for sendto.
        if (ctx.r8.u32 != 0) ToGuest(base, from, ctx.r8.u32);
        if (ctx.r9.u32 != 0) Guest::Write32(base, ctx.r9.u32, 16);
        static std::atomic<int> shown{ 0 };
        if ((shown.load(std::memory_order_relaxed) < 12 && shown.fetch_add(1) < 12))
        {
            printf("net: socket %u received %d bytes from %s:%u\n", ctx.r4.u32, result,
                Dotted(from.sin_addr).c_str(), ntohs(from.sin_port));
            fflush(stdout);
        }
    }
    ctx.r3.u32 = Result(result);
}

// int NetDll_WSAGetLastError(void)
PPC_FUNC(__imp__NetDll_WSAGetLastError)
{
    Kernel::CountImport("NetDll_WSAGetLastError");
    ctx.r3.u32 = uint32_t(t_lastError);
}

// --- XNet: the secure network layer, as a plain local network -------------------

// int NetDll_XNetStartup(caller, const XNetStartupParams* params)
PPC_FUNC(__imp__NetDll_XNetStartup)
{
    Kernel::CountImport("NetDll_XNetStartup");
    StartWinsock();
    LocalAddress();
    ctx.r3.u32 = 0;
}

// int NetDll_XNetCleanup(caller)
PPC_FUNC(__imp__NetDll_XNetCleanup)
{
    Kernel::CountImport("NetDll_XNetCleanup");
    ctx.r3.u32 = 0;
}

// int NetDll_XNetRandom(caller, BYTE* buffer, UINT length)
PPC_FUNC(__imp__NetDll_XNetRandom)
{
    Kernel::CountImport("NetDll_XNetRandom");
    for (uint32_t i = 0; i < ctx.r5.u32; i++) base[ctx.r4.u32 + i] = uint8_t(Random()());
    ctx.r3.u32 = 0;
}

// DWORD NetDll_XNetGetTitleXnAddr(caller, XNADDR* address)
PPC_FUNC(__imp__NetDll_XNetGetTitleXnAddr)
{
    Kernel::CountImport("NetDll_XNetGetTitleXnAddr");
    if (ctx.r4.u32 != 0) WriteXnAddr(base, ctx.r4.u32, LocalAddress(), true);
    // XNET_GET_XNADDR_ETHERNET | STATIC: an address on a local network, and
    // nothing online.
    ctx.r3.u32 = 0x02 | 0x04;
}

// DWORD NetDll_XNetGetEthernetLinkStatus(caller)
PPC_FUNC(__imp__NetDll_XNetGetEthernetLinkStatus)
{
    Kernel::CountImport("NetDll_XNetGetEthernetLinkStatus");
    // XNET_ETHERNET_LINK_ACTIVE | 100MBPS | FULL_DUPLEX: a cable is in.
    ctx.r3.u32 = 0x01 | 0x02 | 0x08;
}

// int NetDll_XNetXnAddrToInAddr(caller, const XNADDR* address, const XNKID* key, IN_ADDR* out)
PPC_FUNC(__imp__NetDll_XNetXnAddrToInAddr)
{
    Kernel::CountImport("NetDll_XNetXnAddrToInAddr");
    if (ctx.r4.u32 == 0 || ctx.r6.u32 == 0) { ctx.r3.u32 = uint32_t(WSAEINVAL); return; }
    // The first of the machine's addresses on a network this one is on;
    // its main one if none is.
    const uint32_t xn = ctx.r4.u32;
    in_addr chosen{};
    memcpy(&chosen, base + xn, 4);
    for (uint32_t slot = 0; slot < MaxXnAddresses; slot++)
    {
        const uint32_t at = slot == 0 ? 0 : slot == 1 ? 4 : 16 + (slot - 2) * 4;
        in_addr candidate{};
        memcpy(&candidate, base + xn + at, 4);
        if (candidate.s_addr == 0) continue;
        if (Reachable(candidate)) { chosen = candidate; break; }
    }
    memcpy(base + ctx.r6.u32, &chosen, 4);
    static std::atomic<int> shown{ 0 };
    if ((shown.load(std::memory_order_relaxed) < 8 && shown.fetch_add(1) < 8))
    {
        printf("net: a player's address resolved to %s\n", Dotted(chosen).c_str());
        fflush(stdout);
    }
    ctx.r3.u32 = 0;
}

// int NetDll_XNetInAddrToXnAddr(caller, IN_ADDR address, XNADDR* out, XNKID* outKey)
PPC_FUNC(__imp__NetDll_XNetInAddrToXnAddr)
{
    Kernel::CountImport("NetDll_XNetInAddrToXnAddr");
    // The address arrives in a register, as the guest loaded it: its bytes
    // in network order are the register's from the top.
    const uint32_t value = ctx.r4.u32;
    in_addr address{};
    uint8_t* b = reinterpret_cast<uint8_t*>(&address);
    b[0] = uint8_t(value >> 24); b[1] = uint8_t(value >> 16); b[2] = uint8_t(value >> 8); b[3] = uint8_t(value);
    if (ctx.r5.u32 != 0) WriteXnAddr(base, ctx.r5.u32, address);
    if (ctx.r6.u32 != 0) memset(base + ctx.r6.u32, 0, 8);
    ctx.r3.u32 = 0;
}

// int NetDll_XNetTsAddrToInAddr(caller, const TSADDR* address, DWORD service, const XNKID* key, IN_ADDR* out)
PPC_FUNC(__imp__NetDll_XNetTsAddrToInAddr)
{
    Kernel::CountImport("NetDll_XNetTsAddrToInAddr");
    Announce("XNetTsAddrToInAddr: title servers are Xbox Live's, not here");
    ctx.r3.u32 = uint32_t(WSAEINVAL);
}

// int NetDll_XNetInAddrToString(caller, IN_ADDR address, char* text, int length)
PPC_FUNC(__imp__NetDll_XNetInAddrToString)
{
    Kernel::CountImport("NetDll_XNetInAddrToString");
    const uint32_t v = ctx.r4.u32;
    char text[32];
    snprintf(text, sizeof(text), "%u.%u.%u.%u", v >> 24, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    if (ctx.r5.u32 != 0 && ctx.r6.u32 > 0)
    {
        const size_t n = std::min<size_t>(strlen(text), ctx.r6.u32 - 1);
        memcpy(base + ctx.r5.u32, text, n);
        base[ctx.r5.u32 + n] = 0;
    }
    ctx.r3.u32 = 0;
}

// int NetDll_XNetUnregisterInAddr(caller, IN_ADDR address)
PPC_FUNC(__imp__NetDll_XNetUnregisterInAddr) { Kernel::CountImport("NetDll_XNetUnregisterInAddr"); ctx.r3.u32 = 0; }

// int NetDll_XNetConnect(caller, IN_ADDR address)
PPC_FUNC(__imp__NetDll_XNetConnect) { Kernel::CountImport("NetDll_XNetConnect"); ctx.r3.u32 = 0; }

// DWORD NetDll_XNetGetConnectStatus(caller, IN_ADDR address)
PPC_FUNC(__imp__NetDll_XNetGetConnectStatus)
{
    Kernel::CountImport("NetDll_XNetGetConnectStatus");
    ctx.r3.u32 = 2;   // XNET_CONNECT_STATUS_CONNECTED: there is nothing to negotiate on a LAN
}

// int NetDll_XNetCreateKey(caller, XNKID* id, XNKEY* key)
PPC_FUNC(__imp__NetDll_XNetCreateKey)
{
    Kernel::CountImport("NetDll_XNetCreateKey");
    for (uint32_t i = 0; i < 8 && ctx.r4.u32 != 0; i++) base[ctx.r4.u32 + i] = uint8_t(Random()());
    for (uint32_t i = 0; i < 16 && ctx.r5.u32 != 0; i++) base[ctx.r5.u32 + i] = uint8_t(Random()());
    // The key's kind is in the top bits of its first byte: a System Link
    // session's are zero.
    if (ctx.r4.u32 != 0) base[ctx.r4.u32] &= 0x0F;
    ctx.r3.u32 = 0;
}

// int NetDll_XNetRegisterKey(caller, const XNKID* id, const XNKEY* key)
PPC_FUNC(__imp__NetDll_XNetRegisterKey) { Kernel::CountImport("NetDll_XNetRegisterKey"); ctx.r3.u32 = 0; }

// int NetDll_XNetUnregisterKey(caller, const XNKID* id)
PPC_FUNC(__imp__NetDll_XNetUnregisterKey) { Kernel::CountImport("NetDll_XNetUnregisterKey"); ctx.r3.u32 = 0; }

// int NetDll_XNetQosListen(caller, const XNKID* id, const BYTE* data, UINT length, DWORD bitsPerSecond, DWORD flags)
PPC_FUNC(__imp__NetDll_XNetQosListen) { Kernel::CountImport("NetDll_XNetQosListen"); ctx.r3.u32 = 0; }

// int NetDll_XNetQosLookup(caller, ... XNQOS** out): the quality of service
// probes are Xbox Live's matchmaking. Refused: the title then lists a
// session without them.
PPC_FUNC(__imp__NetDll_XNetQosLookup)
{
    Kernel::CountImport("NetDll_XNetQosLookup");
    Announce("XNetQosLookup refused: no Xbox Live quality probes here");
    ctx.r3.u32 = uint32_t(WSAEOPNOTSUPP);
}

// --- XAM: sessions, friends, privileges, voice ------------------------------------

// DWORD XamUserCheckPrivilege(DWORD user, DWORD privilege, BOOL* result)
PPC_FUNC(__imp__XamUserCheckPrivilege)
{
    Kernel::CountImport("XamUserCheckPrivilege");
    // Everything allowed: the player owns the machine.
    if (ctx.r5.u32 != 0) Guest::Write32(base, ctx.r5.u32, 1);
    ctx.r3.u32 = 0;
}

// DWORD XamUserAreUsersFriends(DWORD user, const XUID* xuids, DWORD count, BOOL* result, XOVERLAPPED* overlapped)
PPC_FUNC(__imp__XamUserAreUsersFriends)
{
    Kernel::CountImport("XamUserAreUsersFriends");
    if (ctx.r6.u32 != 0)
        for (uint32_t i = 0; i < ctx.r5.u32; i++) Guest::Write32(base, ctx.r6.u32 + i * 4, 0);
    ctx.r3.u32 = 0;
}

// DWORD XamUserCreateStatsEnumerator(...): leaderboards are Xbox Live's.
PPC_FUNC(__imp__XamUserCreateStatsEnumerator)
{
    Kernel::CountImport("XamUserCreateStatsEnumerator");
    Announce("XamUserCreateStatsEnumerator refused: no Xbox Live leaderboards");
    ctx.r3.u32 = 0x80070005;   // access denied: not signed in to Live
}

// DWORD XamShowGamerCardUIForXUID(DWORD user, XUID xuid)
PPC_FUNC(__imp__XamShowGamerCardUIForXUID)
{
    Kernel::CountImport("XamShowGamerCardUIForXUID");
    ctx.r3.u32 = 0x000004C7;   // ERROR_CANCELLED: as if the guide was closed again
}

// DWORD XamSessionCreateHandle(HANDLE* handle)
PPC_FUNC(__imp__XamSessionCreateHandle)
{
    Kernel::CountImport("XamSessionCreateHandle");
    // A session handle is something to hand back to XamSessionRefObjByHandle
    // and to close; an event of the runtime's own serves.
    static std::atomic<uint32_t> next{ 0xF5000000u };
    if (ctx.r3.u32 != 0) Guest::Write32(base, ctx.r3.u32, next.fetch_add(4));
    ctx.r3.u32 = 0;
}

// DWORD XamSessionRefObjByHandle(HANDLE handle, void** object)
PPC_FUNC(__imp__XamSessionRefObjByHandle)
{
    Kernel::CountImport("XamSessionRefObjByHandle");
    // A block of guest memory stands for the session object: the title only
    // passes it on to the session calls, which are its own code over XNet.
    static std::mutex mutex;
    static std::map<uint32_t, uint32_t> objects;
    uint32_t object;
    {
        std::lock_guard<std::mutex> lock(mutex);
        uint32_t& slot = objects[ctx.r3.u32];
        if (slot == 0) slot = Guest::AllocateKernelObject(256);
        object = slot;
    }
    if (ctx.r4.u32 != 0) Guest::Write32(base, ctx.r4.u32, object);
    ctx.r3.u32 = object != 0 ? 0 : 0x8007000E;
}

// DWORD XamVoiceCreate(DWORD user, DWORD flags, void** voice): no headset here.
PPC_FUNC(__imp__XamVoiceCreate)
{
    Kernel::CountImport("XamVoiceCreate");
    if (ctx.r5.u32 != 0) Guest::Write32(base, ctx.r5.u32, 0);
    ctx.r3.u32 = 0x8007048F;   // ERROR_DEVICE_NOT_CONNECTED
}

// BOOL XamVoiceHeadsetPresent(void* voice)
PPC_FUNC(__imp__XamVoiceHeadsetPresent) { Kernel::CountImport("XamVoiceHeadsetPresent"); ctx.r3.u32 = 0; }

// DWORD XamVoiceClose(void* voice)
PPC_FUNC(__imp__XamVoiceClose) { Kernel::CountImport("XamVoiceClose"); ctx.r3.u32 = 0; }

// DWORD XamVoiceSubmitPacket(void* voice, DWORD channel, const void* data)
PPC_FUNC(__imp__XamVoiceSubmitPacket) { Kernel::CountImport("XamVoiceSubmitPacket"); ctx.r3.u32 = 0; }

// DWORD XMsgCancelIORequest(XOVERLAPPED* overlapped, BOOL wait)
PPC_FUNC(__imp__XMsgCancelIORequest) { Kernel::CountImport("XMsgCancelIORequest"); ctx.r3.u32 = 0; }

// --- The settings menu's view of it ------------------------------------------------

Net::Summary Net::Status()
{
    Summary summary;
    const in_addr address = LocalAddress();
    summary.address = Dotted(address) + " (" + g_addressWhy + ")";
    for (const Adapter& adapter : Adapters())
        summary.adapters.push_back(adapter.name + ": " + Dotted(adapter.address) +
            (adapter.broadcasts() ? "" : ", no broadcasts: friends need adding as peers") +
            (adapter.vpn ? " (virtual LAN)" : ""));
    for (const in_addr& peer : Peers()) summary.peers.push_back(Dotted(peer));
    {
        std::lock_guard<std::mutex> lock(g_socketMutex);
        summary.sockets = unsigned(g_sockets.size());
    }
    summary.sent = g_sent.load();
    summary.received = g_received.load();
    {
        std::lock_guard<std::mutex> lock(g_lastFromMutex);
        summary.lastFrom = g_lastFrom;
    }
    return summary;
}


// The side channel: a small UDP socket of this runtime's own (port 28964),
// for what the title itself never sends to a joiner (the custom rules). A
// message goes where a System Link search goes (each network's broadcast
// address and the peers list) and to the machine the game last heard from.
namespace Net
{
    namespace
    {
        constexpr u_short SidePort = 28964;
        SOCKET g_side = INVALID_SOCKET;
        std::once_flag g_sideOpened;

        SOCKET Side()
        {
            std::call_once(g_sideOpened, [] {
                StartWinsock();
                SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (s == INVALID_SOCKET) return;
                BOOL yes = TRUE;
                setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
                setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
                u_long nonBlocking = 1;
                ioctlsocket(s, FIONBIO, &nonBlocking);
                sockaddr_in local{};
                local.sin_family = AF_INET;
                local.sin_port = htons(SidePort);
                local.sin_addr.s_addr = htonl(INADDR_ANY);
                const bool bound = bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0;
                printf("net: side channel on port %u%s\n", SidePort, bound ? "" : " (could not bind: sending only)");
                fflush(stdout);
                g_side = s;
            });
            return g_side;
        }

        void SendTo(SOCKET s, const void* data, int size, in_addr address)
        {
            sockaddr_in to{};
            to.sin_family = AF_INET;
            to.sin_port = htons(SidePort);
            to.sin_addr = address;
            sendto(s, reinterpret_cast<const char*>(data), size, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        }
    }

    void SideSend(const void* data, int size, uint32_t only)
    {
        const SOCKET s = Side();
        if (s == INVALID_SOCKET) return;
        if (only != 0)
        {
            in_addr address{}; address.s_addr = only;
            SendTo(s, data, size, address);
            return;
        }
        std::vector<in_addr> targets;
        in_addr everyone{}; everyone.s_addr = INADDR_BROADCAST;
        targets.push_back(everyone);
        for (const Adapter& adapter : Adapters())
            if (adapter.broadcasts()) targets.push_back(adapter.broadcast);
        for (const in_addr& peer : Peers()) targets.push_back(peer);
        if (const uint32_t last = g_lastFromAddress.load()) { in_addr a{}; a.s_addr = last; targets.push_back(a); }
        for (const in_addr& target : targets) SendTo(s, data, size, target);
    }

    int SideReceive(void* data, int size, uint32_t* from)
    {
        const SOCKET s = Side();
        if (s == INVALID_SOCKET) return -1;
        for (;;)
        {
            sockaddr_in sender{};
            int length = sizeof(sender);
            const int got = recvfrom(s, reinterpret_cast<char*>(data), size, 0, reinterpret_cast<sockaddr*>(&sender), &length);
            if (got == SOCKET_ERROR) return -1;
            if (IsOwnAddress(sender.sin_addr)) continue;   // our own broadcast
            if (from) *from = sender.sin_addr.s_addr;
            return got;
        }
    }
}
