#include "DataLinker.h"

//
// Standard C++ Library
//
#include <list>
#include <thread>
#include <mutex>

#pragma warning(disable:4996)

#define DATA_LINKER_DEFAULT_BUFFER_SIZE 256                     // Default buffer size.
#define DATA_LINKER_DEFAULT_PORT 8016                           // Default port offset.
#define DATA_LINKER_DEFAULT_ADDRESS "127.0.0.5"                 // Default loopback ip address.

//
// IfLessThenZeroSetZero(value)
// if value is less then zero. return NULL.
// else return value itself.
//
#define IfLessThenZeroSetZero(_value) \
    (uint32_t)(_value < NULL) ? NULL : _value

//
// AddAddress(_value, _offset)
// return (&_value + _offset)
//
#define AddAddress(_value, _offset) \
    (void*)((size_t)&##_value + (size_t)_offset)

#pragma data_seg("DATA_LINKER_SHARED")                         // DLL SHARED MEMORY START
//
// long long = 8byte = 64bit
// 4 long long = 32 = 256bit
// 256 channel maximum.
//
struct
{
    unsigned long long byte_1;
    unsigned long long byte_2;
    unsigned long long byte_3;
    unsigned long long byte_4;
} g_bitIsInitialized;

#pragma data_seg()                                             // DLL SHARED MEMORY END
#pragma comment(linker, "/SECTION:DATA_LINKER_SHARED,RWS")    // set DATA_LINKER_SHARED section as R/W/S(Read, Write, Share)

class LinkedChannelImpl : public LinkedChannel
{
protected:
    SOCKET              mSocket;
    SOCKADDR_IN         mAddress;

public:
    LinkedChannelImpl(uint8_t channel)
    {
        //PreInitialize.
        ZeroMemory(&mAddress, sizeof(SOCKADDR_IN));
        ZeroMemory(&mSocket,  sizeof(SOCKET));

        mSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);   // Create socket object.

        mAddress.sin_addr.s_addr = inet_addr(DATA_LINKER_DEFAULT_ADDRESS);
        mAddress.sin_port        = htons(DATA_LINKER_DEFAULT_PORT + channel);
        mAddress.sin_family      = AF_INET;
    }

    ~LinkedChannelImpl()
    {
        closesocket(mSocket);                                  // Close socket object.
    }
};

class LinkedChannelClientImpl : public LinkedChannelImpl
{
public:
    LinkedChannelClientImpl(uint8_t channel) : LinkedChannelImpl(channel)
    {
        connect(
            mSocket, 
            (SOCKADDR*)&mAddress, 
            sizeof(SOCKADDR_IN));                             // Connect to server.
    }
    ~LinkedChannelClientImpl()
    {
        shutdown(mSocket, SD_SEND);                             // Shutdown socket.
    }
    uint32_t Send(const void* message, uint32_t length) { return send(mSocket, (const char*)message, length, NULL); }
    uint32_t Recv(void* message, uint32_t length) { return recv(mSocket, (char*)message, length, NULL); }
};

class LinkedChannelServerImpl : public LinkedChannelImpl
{
private:
    uint32_t                    mBufferSize;                    // Maximum buffer size of recv, send.
    bool                        mIsStopped;                     // is Threads are stopped?

    std::thread*                mTickAcceptThread;              // TickServerThread object.

    std::list<std::thread*>     mListTickSendThreads;           // TickServerSend object.
    std::list<SOCKET>           mListClients;                   // List of connected clients.
    std::mutex                  mTickMutex;                     // Mutex of mListClients. 

    void TickServerSend(SOCKET socket)
    {
        char* Buffer = new char[mBufferSize];                   // Allocate Buffer.

        //
        // ServerTick Loop
        //  - Stop if mIsStopped is true.
        //  - Send all message to all clients.
        //
        while (!mIsStopped)
        {
            int32_t szBuffer = recv(socket, Buffer, mBufferSize, NULL);

            if ( szBuffer < 0 )                                 // If failed to recv, then continue.
                continue;

            // Send message to all clients.
            mTickMutex.lock();
            for (auto it = mListClients.begin(); it != mListClients.end(); ++it)
            {
                if ( (*it) == socket )                          // If iterator is client that sent. continue.
                    continue;
                
                send((*it), Buffer, szBuffer, NULL);            // Else send message to client.
            }
            mTickMutex.unlock();
        }

        delete[] Buffer;                                        // Release Buffer.

        closesocket(socket);                                    // Close socket object.
    }

    void PushClient(SOCKET socket)                              // Push socket handle on list.
    {
        mTickMutex.lock();
        mListClients.push_back(socket);
        mListTickSendThreads.push_back(new std::thread(&LinkedChannelServerImpl::TickServerSend, this, socket));
        mTickMutex.unlock();
    }

    void TickServerAccept()
    {
        SOCKET sockClient;

        if( listen(mSocket, SOMAXCONN) == SOCKET_ERROR )        // Socket listen start.
            return;

        //
        // ServerTick Loop
        //  - Stop if mIsStopped is true.
        //  - Accept clients that trying to connect on server.
        //
        while (!mIsStopped)
        {
            sockClient = accept(mSocket, NULL, NULL);           // Wait for client.

            if ( sockClient == INVALID_SOCKET )                 // If client socket handle is INVALID, continue.
                continue;

            PushClient(sockClient);                             // Push socket client handle to client list.
        }
    }

public:

    LinkedChannelServerImpl(const uint8_t channel, const uint32_t szBuffer) : LinkedChannelImpl(channel),
        mIsStopped(false)
    {
        mBufferSize = szBuffer ? szBuffer : DATA_LINKER_DEFAULT_BUFFER_SIZE;
        // If szBuffer is NULL set mBufferSize as default.

        bind(mSocket, (SOCKADDR*)&mAddress, sizeof(SOCKADDR_IN));

        mTickAcceptThread = new std::thread(&LinkedChannelServerImpl::TickServerAccept, this);
    }

    ~LinkedChannelServerImpl()
    {
        // set mIsStopped true to stop threads. (Loop exit)
        mIsStopped = true;        

        // Wait for TickAcceptThread.
        mTickAcceptThread->join();
        delete mTickAcceptThread;
        
        // Wait for all TickServerSendThreads.
        for(auto it = mListTickSendThreads.begin(); it != mListTickSendThreads.end(); ++it)
        {
            (*it)->join();
            delete (*it);
        }
    }

    uint32_t Send(const void* message, uint32_t length)
    {
        for(auto it = mListClients.begin(); it != mListClients.end(); ++it)
        {
            send((*it), (const char*)message, length, NULL);
        }

        return length;
    }

    // TODO : Queue based recv impl with TickServerSend.
    uint32_t Recv(void* message, uint32_t length)
    {
        return 0;
    }
};

// TODO : Is bit functions should be inlined??

//
// inline bool BitInitializedCompare(const uint8_t)
// Logical AND(&) compare with g_bitIsInitialized
// if bit exist. return true, else false.
//
inline bool BitInitializedCompare(const uint8_t bit)
{
    //
    //                ===========================================================....
    //                |                     struct g_bitIsInitialized....
    //                ===================================================...
    //                |          (unsigned long long byte_1)........
    //                ==============================================....
    //  byte_position |    1b    |    2b    |    3b    |    4b    |   5b....
    //  ============= | 12345678 | 12345678 | 12345678 | 12345678 | 123456....
    //                |   Bits   |   Bits   |   Bits   |   Bits   |
    //                ============================================...
    //

    int byte_position = (bit / 8);
    int bit_position  = (1 << IfLessThenZeroSetZero((bit % 8) - 1));
    // you cannot shift -1 so you have to set -1 to 0.

    // (1 << [Bit Position] - 1) ==> Set Bit Position.
    // Example : (1 << 5 - 1)
    //      => ================================
    //         |0|0|0|0|0|0|0|1| << 4         |
    //      => ================================
    //         |0|0|0|1|0|0|0|0| Bit Position |
    //         |8|7|6|5|4|3|2|1| Bit Number   |
    //         ================================


    if( *(int*)AddAddress(g_bitIsInitialized, byte_position) & bit_position ) 
        return true;

    return false;
}

//
// inline void BitInitializedAdd(const uint8_t)
// Add bit on g_bitIsInitialized.
//
inline void BitInitializedAdd(const uint8_t bit)
{
    int byte_position = (bit / 8);
    int bit_position  = (1 << IfLessThenZeroSetZero((bit % 8) - 1));
    // you cannot shift -1 so you have to set -1 to 0.

    int* addr_current_position = (int*)AddAddress(g_bitIsInitialized, byte_position);
    (*addr_current_position)  = (*addr_current_position) | bit_position;
    
    return;
}

//
// BOOL CreateChannel(const uint8_t, LinkedChannel**)
// Create Interprocess shared channel.
//
BOOL CreateChannel(const uint8_t channel, const uint32_t szBuffer, LinkedChannel** object)
{
    if( BitInitializedCompare(channel) ) return FALSE;
    BitInitializedAdd(channel);

    (*object) = (LinkedChannel*)(new LinkedChannelServerImpl(channel, szBuffer));

    return TRUE;
}

//
// BOOL ConnectChannel(const uint8_t, LinkedChannel**)
// Connect to interprocess shared channel.
//
BOOL ConnectChannel(const uint8_t channel, LinkedChannel** object)
{
    if( !BitInitializedCompare(channel) ) return FALSE;

    (*object) = (LinkedChannel*)(new LinkedChannelClientImpl(channel));

    return TRUE;
}

//
// BOOL DistroyChannel(LinkedChannel*)
// Distroy interprocess channel.
//
BOOL DistroyChannel(LinkedChannel** object)
{
    if(*object == nullptr) return FALSE;

    delete (*object);
    (*object) = nullptr;

    return TRUE;
}