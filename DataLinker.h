#ifndef DATA_LINKER_H
#define DATA_LINKER_H

//
// Interprocess Synchronized Data Sharing Library using WinSock2. DataLinker.
//

#include <WinSock2.h>
#include <stdint.h>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

#ifdef _DLL
#define DATA_LINKER_API __declspec(dllexport)
#else
#define DATA_LINKER_API __declspec(dllimport)
#endif

//
//  Interprocess shared channel object.
//
class LinkedChannel
{
    // Send message to channel.
    virtual uint32_t Send(const void* message, uint32_t length) = 0;

    // Recv message from channel.
    virtual uint32_t Recv(void* message, uint32_t length) = 0;
};

//
// Create Interprocess shared channel.
// NOTE: You have to keep this channel before all channel being disconnected.
//   Arguments : const uint8_t channel - channel to connect. maximum 256.
//               LinkedChannel**       - object out return.
//
DATA_LINKER_API BOOL CreateChannel(const uint8_t channel, const uint32_t szBuffer, LinkedChannel** object);

//
// Connect to interprocess shared channel.
//   Arguments : const uint8_t channel - channel to connect. maximum 256.
//               LinkedChannel**       - object out return.
//
DATA_LINKER_API BOOL ConnectChannel(const uint8_t channel, LinkedChannel** object);

//
// Distroy interprocess linked channel.
// NOTE: if you distroy client channel(Object that made from ConnectChanne) it will be disconnected.
//       if you distroy server channel(Object that made from CreateChannel) all client that connected
//       on that channel will be disconnected and channel will be closed.
//   Arguments : LinkedChannel*        - channel object to distroy.
//
DATA_LINKER_API BOOL DistroyChannel(LinkedChannel* object);
#endif
