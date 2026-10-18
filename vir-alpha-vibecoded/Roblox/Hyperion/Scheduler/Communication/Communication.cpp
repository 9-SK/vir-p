#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include "Communication.hpp"
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>
#include <Roblox/Hyperion/Communication/BootLog.hpp>
#include <Roblox/Hyperion/Communication/StatusRing.hpp>
#include <thread>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace
{
bool ReadExactSocket(SOCKET Socket, void* Buffer, size_t Size)
{
    size_t Total = 0;
    char* Out = static_cast<char*>(Buffer);
    while (Total < Size)
    {
        int Received = recv(Socket, Out + Total, static_cast<int>(Size - Total), 0);
        if (Received <= 0)
            return false;

        Total += static_cast<size_t>(Received);
    }

    return true;
}

bool WriteExactSocket(SOCKET Socket, const void* Buffer, size_t Size)
{
    size_t Total = 0;
    const char* Out = static_cast<const char*>(Buffer);
    while (Total < Size)
    {
        int Sent = send(Socket, Out + Total, static_cast<int>(Size - Total), 0);
        if (Sent <= 0)
            return false;

        Total += static_cast<size_t>(Sent);
    }

    return true;
}

// Wire format, big-endian length prefix:
//   request : [u32 be payloadLen][u8 cmd][payload...]   (payloadLen counts cmd)
//   response: [u32 be payloadLen][payload...]
// Byte-identical to what the desktop application sends/parses (its MODULE IPC
// client): PING -> "PONG", STATUS -> 15360-byte snapshot, EXEC with payload
// [u32 le reqId][Lua source] -> empty payload; the reqId is echoed back later
// through the snapshot's last_req_id once the script has run.
constexpr uint8_t kCmdPing = 1;
constexpr uint8_t kCmdStatus = 2;
constexpr uint8_t kCmdExec = 3;

// Same frame cap the application uses (its IPC_MAX_PAYLOAD): a longer length
// prefix is not this server, so it is never allocated for.
constexpr uint32_t kMaxFrame = 8u * 1024u * 1024u;

bool SendResponse(SOCKET Sock, const void* Data, size_t Size)
{
    const uint32_t Net = htonl(static_cast<uint32_t>(Size));
    if (!WriteExactSocket(Sock, &Net, sizeof(Net)))
        return false;
    if (Size == 0)
        return true;
    return WriteExactSocket(Sock, Data, Size);
}

void HandleClient(SOCKET Sock)
{
    uint32_t NetLen = 0;
    if (!ReadExactSocket(Sock, &NetLen, sizeof(NetLen)))
        return;
    const uint32_t Len = ntohl(NetLen);
    if (Len < 1 || Len > kMaxFrame)
    {
        SendResponse(Sock, nullptr, 0);
        return;
    }

    std::vector<uint8_t> Payload(Len);
    if (!ReadExactSocket(Sock, Payload.data(), Len))
        return;

    switch (Payload[0])
    {
    case kCmdPing:
    {
        static const char kPong[] = "PONG";
        SendResponse(Sock, kPong, sizeof(kPong) - 1);
        break;
    }
    case kCmdStatus:
    {
        std::vector<uint8_t> Snap(StatusRing::kSnapshotSize);
        if (StatusRing::SnapshotTo(Snap.data(), static_cast<uint32_t>(Snap.size())))
            SendResponse(Sock, Snap.data(), Snap.size());
        else
            SendResponse(Sock, nullptr, 0);
        break;
    }
    case kCmdExec:
    {
        // Payload after the cmd byte is [u32 le request_id][Lua source].
        if (Len < 5)
        {
            SendResponse(Sock, nullptr, 0);
            break;
        }
        uint32_t ReqId = 0;
        memcpy(&ReqId, Payload.data() + 1, 4);
        std::string Script(reinterpret_cast<const char*>(Payload.data() + 5), Len - 5);
        if (!Script.empty())
        {
            char msg[96]{};
            snprintf(msg, sizeof(msg), "exec id=%u size=%u",
                ReqId, static_cast<unsigned>(Script.size()));
            BootLog::WriteLine("comm", msg);
            TaskScheduler::RequestExecution(std::move(Script), ReqId);
        }
        SendResponse(Sock, nullptr, 0);
        break;
    }
    default:
        SendResponse(Sock, nullptr, 0);
        break;
    }
}

void TcpServer()
{
    WSADATA Wsa;
    // WSAStartup/getaddrinfo report failure through their RETURN VALUE, not
    // the thread's last-error, so each is captured before anything clobbers it.
    const int WsaRc = WSAStartup(MAKEWORD(2, 2), &Wsa);
    if (WsaRc != 0)
    {
        char msg[64]{};
        snprintf(msg, sizeof(msg), "WSAStartup failed rc=%d", WsaRc);
        BootLog::WriteLine("comm", msg);
        return;
    }

    SOCKET ListenSocket = INVALID_SOCKET;

    addrinfo Hints{};
    addrinfo* Result = nullptr;
    Hints.ai_family = AF_INET;
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_protocol = IPPROTO_TCP;
    Hints.ai_flags = AI_PASSIVE;

    const int GaiRc = getaddrinfo("127.0.0.1", "6969", &Hints, &Result);
    if (GaiRc != 0)
    {
        char msg[64]{};
        snprintf(msg, sizeof(msg), "getaddrinfo failed rc=%d", GaiRc);
        BootLog::WriteLine("comm", msg);
        WSACleanup();
        return;
    }

    ListenSocket = socket(Result->ai_family, Result->ai_socktype, Result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET)
    {
        BootLog::WriteLine("comm", "socket failed");
        freeaddrinfo(Result);
        WSACleanup();
        return;
    }

    BOOL Opt = TRUE;
    setsockopt(ListenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&Opt), sizeof(Opt));
    if (bind(ListenSocket, Result->ai_addr, static_cast<int>(Result->ai_addrlen)) == SOCKET_ERROR)
    {
        char msg[64]{};
        snprintf(msg, sizeof(msg), "bind failed err=%d", WSAGetLastError());
        BootLog::WriteLine("comm", msg);
        closesocket(ListenSocket);
        freeaddrinfo(Result);
        WSACleanup();
        return;
    }

    freeaddrinfo(Result);
    if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        BootLog::WriteLine("comm", "listen failed");
        closesocket(ListenSocket);
        WSACleanup();
        return;
    }

    BootLog::WriteLine("comm", "listening on 127.0.0.1:6969");

    while (true)
    {
        SOCKET ClientSocket = accept(ListenSocket, nullptr, nullptr);
        if (ClientSocket == INVALID_SOCKET)
        {
            // Log the first occurrence only: the retry loop is 10/s and every
            // line also pokes the ring tail refresh.
            static LONG s_acceptLogged = 0;
            if (InterlockedCompareExchange(&s_acceptLogged, 1, 0) == 0)
                BootLog::WriteLine("comm", "accept failed (further errors suppressed)");
            Sleep(100);
            continue;
        }

        // Requests are handled serially on this one accept loop, so a client
        // that connects and then stalls would wedge every later request
        // forever. Bound the read and write so a bad or abandoned connection
        // costs one timeout instead of the whole channel.
        DWORD TimeoutMs = 2000;
        setsockopt(ClientSocket, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&TimeoutMs), sizeof(TimeoutMs));
        setsockopt(ClientSocket, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char*>(&TimeoutMs), sizeof(TimeoutMs));
        BOOL NoDelay = TRUE;
        setsockopt(ClientSocket, IPPROTO_TCP, TCP_NODELAY,
            reinterpret_cast<const char*>(&NoDelay), sizeof(NoDelay));

        HandleClient(ClientSocket);
        closesocket(ClientSocket);
    }

    closesocket(ListenSocket);
    WSACleanup();
}
} // namespace

void Communication::Initialize()
{
    std::thread(TcpServer).detach();
}
