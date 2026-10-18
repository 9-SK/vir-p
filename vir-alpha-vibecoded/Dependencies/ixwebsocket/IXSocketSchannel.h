/*
 * IXSocketSchannel.h — Windows SSPI/Schannel TLS backend for IXWebSocket
 */
#pragma once

#ifdef IXWEBSOCKET_USE_SCHANNEL

#include "IXSocket.h"
#include "IXSocketTLSOptions.h"
#include "IXCancellationRequest.h"

#include <windows.h>
#include <wincrypt.h>
#include <security.h>
#include <sspi.h>

#include <string>
#include <vector>
#include <mutex>

namespace ix
{
    class SocketSchannel final : public Socket
    {
    public:
        SocketSchannel(const SocketTLSOptions& tlsOptions, int fd = -1);
        ~SocketSchannel() override;

        bool accept(std::string& errMsg) override;

        bool connect(const std::string& host,
                     int port,
                     std::string& errMsg,
                     const CancellationRequest& isCancellationRequested) override;

        void close() override;

        ssize_t send(char* buffer, size_t length) override;
        ssize_t recv(void* buffer, size_t length) override;

    private:
        bool handshake(const std::string& host, std::string& errMsg);
        bool encryptAndSend(const char* data, size_t len, std::string& errMsg);
        bool decryptInto(void* out, size_t len, size_t& outLen, std::string& errMsg);
        void freeSecurityContext();
        bool ensureContextBuffer(size_t needed);

        SocketTLSOptions _tlsOptions;
        std::string _host;

        CredHandle _credHandle{};
        CtxtHandle _ctxHandle{};
        bool _credValid = false;
        bool _ctxValid = false;
        bool _handshakeDone = false;

        SecPkgContext_StreamSizes _streamSizes{};
        bool _streamSizesValid = false;

        std::vector<BYTE> _decrypted;
        size_t _decryptedPos = 0;

        std::vector<BYTE> _ioBuffer;
        size_t _ioLen = 0;

        std::mutex _tlsMutex;
    };
}

#endif // IXWEBSOCKET_USE_SCHANNEL
