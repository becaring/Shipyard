#pragma once
#include <stdint.h>
// The reached SDL_net 2.x transport contract. Addresses and ports are network
// byte order on both PC and Xenon; never swap them twice on big-endian hosts.
struct IPaddress { uint32_t host; uint16_t port; };
struct ShipyardTcpSocket;
typedef ShipyardTcpSocket* TCPsocket;
struct ShipyardSocketSet;
typedef ShipyardSocketSet* SDLNet_SocketSet;
extern "C" {
int SDLNet_Init();
void SDLNet_Quit();
const char* SDLNet_GetError();
int SDLNet_ResolveHost(IPaddress* address,const char* host,uint16_t port);
TCPsocket SDLNet_TCP_Open(IPaddress* address);
TCPsocket SDLNet_TCP_Accept(TCPsocket listener);
IPaddress* SDLNet_TCP_GetPeerAddress(TCPsocket socket);
int SDLNet_TCP_Send(TCPsocket socket,const void* bytes,int length);
int SDLNet_TCP_Recv(TCPsocket socket,void* bytes,int length);
void SDLNet_TCP_Close(TCPsocket socket);
SDLNet_SocketSet SDLNet_AllocSocketSet(int capacity);
int SDLNet_TCP_AddSocket(SDLNet_SocketSet set,TCPsocket socket);
int SDLNet_TCP_DelSocket(SDLNet_SocketSet set,TCPsocket socket);
int SDLNet_CheckSockets(SDLNet_SocketSet set,uint32_t timeout);
void SDLNet_FreeSocketSet(SDLNet_SocketSet set);
}
