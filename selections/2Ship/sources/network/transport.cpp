// SDL_net 2.x compatible socket surface implemented over Winsock/XNet.
// This implementation contains no SDL_net source; transport.h defines the
// subset and byte-order contract exposed to consumers.
#ifdef _XBOX
#include <xtl.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif
#include "network/transport.h"
#include "async/native.h"
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>

struct ShipyardTcpSocket {
    SOCKET channel; // Owned until SDLNet_TCP_Close.
    IPaddress peer;
    bool listener;
    int ready;
};
struct ShipyardSocketSet {
    int capacity;
    int count;
    TCPsocket* sockets; // Borrowed sockets; only this pointer array is owned.
};
namespace {
__declspec(thread) char lastError[256];
Shipyard::Async::mutex lifecycleMutex;
unsigned int startupReferences = 0;
void set_error(const char* operation, int code) {
    _snprintf(lastError, sizeof(lastError) - 1, "%s (%d)", operation, code);
    lastError[sizeof(lastError) - 1] = 0;
}
void set_error(const char* operation) {
    set_error(operation, WSAGetLastError());
}
// Takes ownership of channel even when allocation fails.
TCPsocket adopt_channel(SOCKET channel, const sockaddr_in& address, bool listener) {
    TCPsocket socket = new (std::nothrow) ShipyardTcpSocket;
    if (!socket) {
        closesocket(channel);
        set_error("socket allocation", WSAENOBUFS);
        return NULL;
    }
    socket->channel = channel;
    socket->listener = listener;
    socket->ready = 0;
    socket->peer.host = address.sin_addr.s_addr;
    socket->peer.port = address.sin_port;
    BOOL noDelay = TRUE;
    setsockopt(channel, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay),
               sizeof(noDelay));
    return socket;
}
} // namespace
const char* SDLNet_GetError() {
    return lastError;
}
int SDLNet_Init() {
    Shipyard::Async::lock_guard<Shipyard::Async::mutex> lock(lifecycleMutex);
    if (startupReferences) {
        ++startupReferences;
        return 0;
    }
#ifdef _XBOX
    XNetStartupParams parameters = {0};
    parameters.cfgSizeOfStruct = sizeof(parameters);
    // PC/external remote-control peers require the XDK's documented devkit mode.
    parameters.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
    int status = XNetStartup(&parameters);
    if (status) {
        set_error("XNetStartup", status);
        return -1;
    }
#endif
    WSADATA data;
    int statusWsa = WSAStartup(MAKEWORD(2, 2), &data);
    if (statusWsa) {
#ifdef _XBOX
        XNetCleanup();
#endif
        set_error("WSAStartup", statusWsa);
        return -1;
    }
    startupReferences = 1;
    return 0;
}
void SDLNet_Quit() {
    Shipyard::Async::lock_guard<Shipyard::Async::mutex> lock(lifecycleMutex);
    if (!startupReferences)
        return;
    --startupReferences;
    if (startupReferences)
        return;
    if (WSACleanup())
        set_error("WSACleanup");
#ifdef _XBOX
    int status = XNetCleanup();
    if (status)
        set_error("XNetCleanup", status);
#endif
}
int SDLNet_ResolveHost(IPaddress* address, const char* host, uint16_t port) {
    if (!address) {
        set_error("null IPaddress", WSAEINVAL);
        return -1;
    }
    address->port = htons(port);
    address->host = INADDR_NONE;
    if (!host) {
        address->host = INADDR_ANY;
        return 0;
    }
    unsigned long numeric = inet_addr(host);
    if (numeric != INADDR_NONE) {
        address->host = numeric;
        return 0;
    }
#ifdef _XBOX
    HANDLE dnsEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!dnsEvent) {
        set_error("DNS event", GetLastError());
        return -1;
    }
    XNDNS* dns = NULL;
    int status = XNetDnsLookup(host, dnsEvent, &dns);
    if (!status && dns && dns->iStatus == WSAEINPROGRESS) {
        if (WaitForSingleObject(dnsEvent, INFINITE) != WAIT_OBJECT_0)
            status = WSAENETDOWN;
    }
    if (!status && dns)
        status = dns->iStatus;
    if (!status && dns && dns->cina)
        address->host = dns->aina[0].s_addr;
    else if (!status)
        status = WSAHOST_NOT_FOUND;
    if (dns)
        XNetDnsRelease(dns);
    CloseHandle(dnsEvent);
#else
    addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* resolvedAddress = NULL;
    int status = getaddrinfo(host, NULL, &hints, &resolvedAddress);
    if (!status && resolvedAddress)
        address->host = reinterpret_cast<sockaddr_in*>(resolvedAddress->ai_addr)->sin_addr.s_addr;
    else if (!status)
        status = WSAHOST_NOT_FOUND;
    if (resolvedAddress)
        freeaddrinfo(resolvedAddress);
#endif
    if (status) {
        set_error("resolve host", status);
        return -1;
    }
    return 0;
}
TCPsocket SDLNet_TCP_Open(IPaddress* address) {
    if (!address) {
        set_error("null IPaddress", WSAEINVAL);
        return NULL;
    }
    SOCKET channel = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (channel == INVALID_SOCKET) {
        set_error("socket");
        return NULL;
    }
    sockaddr_in peer = {0};
    peer.sin_family = AF_INET;
    peer.sin_port = address->port;
    peer.sin_addr.s_addr = address->host;
    bool listener = address->host == INADDR_ANY || address->host == INADDR_NONE;
    if (listener) {
        peer.sin_addr.s_addr = INADDR_ANY;
        if (bind(channel, reinterpret_cast<sockaddr*>(&peer), sizeof(peer))) {
            set_error("bind/listen");
            closesocket(channel);
            return NULL;
        }
        if (listen(channel, 5)) {
            set_error("bind/listen");
            closesocket(channel);
            return NULL;
        }
        u_long mode = 1;
        if (ioctlsocket(channel, FIONBIO, &mode)) {
            set_error("listener nonblocking");
            closesocket(channel);
            return NULL;
        }
    } else if (connect(channel, reinterpret_cast<sockaddr*>(&peer), sizeof(peer))) {
        set_error("connect");
        closesocket(channel);
        return NULL;
    }
    return adopt_channel(channel, peer, listener);
}
TCPsocket SDLNet_TCP_Accept(TCPsocket listener) {
    if (!listener || !listener->listener) {
        set_error("not a listening socket", WSAEINVAL);
        return NULL;
    }
    listener->ready = 0;
    sockaddr_in peer = {0};
    int length = sizeof(peer);
    SOCKET channel = accept(listener->channel, reinterpret_cast<sockaddr*>(&peer), &length);
    if (channel == INVALID_SOCKET) {
        set_error("accept");
        return NULL;
    }
    u_long mode = 0;
    if (ioctlsocket(channel, FIONBIO, &mode)) {
        set_error("accepted blocking mode");
        closesocket(channel);
        return NULL;
    }
    return adopt_channel(channel, peer, false);
}
IPaddress* SDLNet_TCP_GetPeerAddress(TCPsocket socket) {
    return socket && !socket->listener ? &socket->peer : NULL;
}
int SDLNet_TCP_Send(TCPsocket socket, const void* bytes, int length) {
    if (!socket || socket->listener || length < 0 || (!bytes && length)) {
        set_error("invalid send", WSAEINVAL);
        return -1;
    }
    int sent = 0;
    do {
        int count = send(socket->channel, static_cast<const char*>(bytes) + sent, length - sent, 0);
        if (count > 0) {
            sent += count;
        } else if (count == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) {
            continue;
        } else {
            if (count == SOCKET_ERROR)
                set_error("send");
            break;
        }
    } while (sent < length);
    return sent;
}
int SDLNet_TCP_Recv(TCPsocket socket, void* bytes, int length) {
    if (!socket || socket->listener || length < 0 || (!bytes && length)) {
        set_error("invalid receive", WSAEINVAL);
        return -1;
    }
    int count;
    do {
        count = recv(socket->channel, static_cast<char*>(bytes), length, 0);
    } while (count == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
    socket->ready = 0;
    if (count == SOCKET_ERROR)
        set_error("receive");
    return count;
}
void SDLNet_TCP_Close(TCPsocket socket) {
    if (socket) {
        closesocket(socket->channel);
        delete socket;
    }
}
SDLNet_SocketSet SDLNet_AllocSocketSet(int capacity) {
    if (capacity < 0 || capacity > FD_SETSIZE) {
        set_error("socket-set capacity", WSAEINVAL);
        return NULL;
    }
    SDLNet_SocketSet set = new (std::nothrow) ShipyardSocketSet;
    if (!set) {
        set_error("socket-set allocation", WSAENOBUFS);
        return NULL;
    }
    set->count = 0;
    set->capacity = capacity;
    set->sockets = new (std::nothrow) TCPsocket[capacity];
    if (!set->sockets) {
        delete set;
        set_error("socket-set allocation", WSAENOBUFS);
        return NULL;
    }
    return set;
}
int SDLNet_TCP_AddSocket(SDLNet_SocketSet set, TCPsocket socket) {
    if (!set) {
        set_error("null socket set", WSAEINVAL);
        return -1;
    }
    if (socket) {
        if (set->count == set->capacity) {
            set_error("socket set full", WSAENOBUFS);
            return -1;
        }
        set->sockets[set->count++] = socket;
    }
    return set->count;
}
int SDLNet_TCP_DelSocket(SDLNet_SocketSet set, TCPsocket socket) {
    if (!set) {
        set_error("null socket set", WSAEINVAL);
        return -1;
    }
    if (socket) {
        int i = 0;
        while (i < set->count && set->sockets[i] != socket)
            ++i;
        if (i == set->count) {
            set_error("socket not in set", WSAEINVAL);
            return -1;
        }
        --set->count;
        for (; i < set->count; ++i)
            set->sockets[i] = set->sockets[i + 1];
    }
    return set->count;
}
int SDLNet_CheckSockets(SDLNet_SocketSet set, uint32_t timeout) {
    if (!set) {
        set_error("null socket set", WSAEINVAL);
        return -1;
    }
    fd_set mask;
    int result;
    do {
        FD_ZERO(&mask);
        for (int i = 0; i < set->count; ++i)
            FD_SET(set->sockets[i]->channel, &mask);
        timeval wait = {long(timeout / 1000), long((timeout % 1000) * 1000)};
        result = select(0, &mask, NULL, NULL, &wait);
    } while (result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
    if (result == SOCKET_ERROR)
        set_error("select");
    if (result > 0) {
        for (int i = 0; i < set->count; ++i) {
            if (FD_ISSET(set->sockets[i]->channel, &mask)) {
                set->sockets[i]->ready = 1;
            }
        }
    }
    return result;
}
void SDLNet_FreeSocketSet(SDLNet_SocketSet set) {
    if (set) {
        delete[] set->sockets;
        delete set;
    }
}
