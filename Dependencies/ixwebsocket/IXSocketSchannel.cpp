/*
 * IXSocketSchannel.cpp — Windows SSPI/Schannel TLS backend for IXWebSocket
 */
#ifdef IXWEBSOCKET_USE_SCHANNEL

#include "IXSocketSchannel.h"
#include "IXSocketConnect.h"
#include "IXNetSystem.h"

#include <string.h>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

namespace ix
{
    static void SetSecError(std::string& errMsg, const char* prefix, SECURITY_STATUS st)
    {
        char buf[128];
        sprintf_s(buf, "%s (0x%08lX)", prefix, static_cast<unsigned long>(st));
        errMsg = buf;
    }

    SocketSchannel::SocketSchannel(const SocketTLSOptions& tlsOptions, int fd)
        : Socket(fd)
        , _tlsOptions(tlsOptions)
    {
    }

    SocketSchannel::~SocketSchannel()
    {
        close();
    }

    void SocketSchannel::freeSecurityContext()
    {
        if (_ctxValid)
        {
            DeleteSecurityContext(&_ctxHandle);
            _ctxValid = false;
        }
        if (_credValid)
        {
            FreeCredentialsHandle(&_credHandle);
            _credValid = false;
        }
        _handshakeDone = false;
        _streamSizesValid = false;
        _decrypted.clear();
        _decryptedPos = 0;
        _ioLen = 0;
    }

    void SocketSchannel::close()
    {
        std::lock_guard<std::mutex> lock(_tlsMutex);
        freeSecurityContext();
        Socket::close();
    }

    bool SocketSchannel::accept(std::string& errMsg)
    {
        errMsg = "Server-side Schannel accept not implemented";
        return false;
    }

    bool SocketSchannel::connect(const std::string& host,
                                 int port,
                                 std::string& errMsg,
                                 const CancellationRequest& isCancellationRequested)
    {
        int fd = SocketConnect::connect(host, port, errMsg, isCancellationRequested);
        if (fd < 0)
            return false;

        _sockfd = fd;
        _host = host;

        return handshake(host, errMsg);
    }

    bool SocketSchannel::handshake(const std::string& host, std::string& errMsg)
    {
        std::lock_guard<std::mutex> lock(_tlsMutex);

        SCHANNEL_CRED credData{};
        credData.dwVersion = SCHANNEL_CRED_VERSION;
        credData.grbitEnabledProtocols = static_cast<DWORD>(PROTO_SSL3 | PROTO_TLS1 | PROTO_TLS1_1 | PROTO_TLS1_2);
        credData.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;

        if (_tlsOptions.isPeerVerifyDisabled())
            credData.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION;
        else
            credData.dwFlags |= SCH_CRED_AUTO_CRED_VALIDATION;

        SECURITY_STATUS st = AcquireCredentialsHandleW(
            nullptr,
            const_cast<LPWSTR>(UNISP_NAME_W),
            SECPKG_CRED_OUTBOUND,
            nullptr,
            &credData,
            nullptr,
            nullptr,
            &_credHandle,
            nullptr);

        if (st != SEC_E_OK)
        {
            SetSecError(errMsg, "AcquireCredentialsHandle failed", st);
            return false;
        }
        _credValid = true;

        _ioBuffer.clear();
        _ioBuffer.resize(0x10000);
        _ioLen = 0;

        SecBufferDesc outBufDesc{};
        SecBuffer outSecBuf{};
        SecBufferDesc inBufDesc{};
        SecBuffer inSecBuf{};

        bool done = false;
        int iterations = 0;

        while (!done && iterations++ < 16)
        {
            if (isCancellationRequested && isCancellationRequested())
            {
                errMsg = "Cancelled";
                freeSecurityContext();
                return false;
            }

            outSecBuf.BufferType = SECBUFFER_TOKEN;
            outSecBuf.pvBuffer = nullptr;
            outSecBuf.cbBuffer = 0;
            outBufDesc.ulVersion = SECBUFFER_VERSION;
            outBufDesc.cBuffers = 1;
            outBufDesc.pBuffers = &outSecBuf;

            bool haveInput = _ioLen > 0;
            if (haveInput)
            {
                inSecBuf.BufferType = SECBUFFER_TOKEN;
                inSecBuf.pvBuffer = _ioBuffer.data();
                inSecBuf.cbBuffer = static_cast<ULONG>(_ioLen);
                inBufDesc.ulVersion = SECBUFFER_VERSION;
                inBufDesc.cBuffers = 1;
                inBufDesc.pBuffers = &inBufDesc.pBuffers ? &inSecBuf : &inSecBuf;
                inBufDesc.pBuffers = &inSecBuf;
            }

            ULONG ctxAttrs = 0;
            st = InitializeSecurityContextW(
                &_credHandle,
                _ctxValid ? &_ctxHandle : nullptr,
                const_cast<SEC_WCHAR*>(host.empty() ? nullptr : std::wstring(host.begin(), host.end()).c_str()),
                ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                    ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM,
                0,
                SECURITY_NATIVE_DREP,
                haveInput ? &inBufDesc : nullptr,
                0,
                _ctxValid ? &_ctxHandle : nullptr,
                &outBufDesc,
                &ctxAttrs,
                nullptr);

            if (!_ctxValid && (st == SEC_E_OK || st == SEC_I_CONTINUE_NEEDED))
            {
                // first call may not fill _ctxHandle on some paths; re-query
            }

            if (st == SEC_E_INCOMPLETE_MESSAGE)
            {
                // need more data
            }
            else if (st == SEC_I_CONTINUE_NEEDED || st == SEC_E_OK)
            {
                if (outSecBuf.pvBuffer && outSecBuf.cbBuffer > 0)
                {
                    int sent = 0;
                    char* p = static_cast<char*>(outSecBuf.pvBuffer);
                    int remaining = static_cast<int>(outSecBuf.cbBuffer);
                    while (remaining > 0)
                    {
                        int n = ::send(_sockfd, p + sent, remaining, 0);
                        if (n <= 0)
                        {
                            if (outSecBuf.pvBuffer) FreeContextBuffer(outSecBuf.pvBuffer);
                            errMsg = "TLS handshake send failed";
                            freeSecurityContext();
                            return false;
                        }
                        sent += n;
                        remaining -= n;
                    }
                }
                if (outSecBuf.pvBuffer)
                    FreeContextBuffer(outSecBuf.pvBuffer);

                if (st == SEC_E_OK)
                {
                    done = true;
                    break;
                }
            }
            else
            {
                if (outSecBuf.pvBuffer)
                    FreeContextBuffer(outSecBuf.pvBuffer);
                SetSecError(errMsg, "InitializeSecurityContext failed", st);
                freeSecurityContext();
                return false;
            }

            // read more server data
            {
                fd_set rfds;
                FD_ZERO(&rfds);
                int s = _sockfd;
                FD_SET(s, &rfds);
                timeval tv{};
                tv.tv_sec = 0;
                tv.tv_usec = 100000;

                int sel = select(0, &rfds, nullptr, nullptr, &tv);
                if (sel > 0)
                {
                    if (_ioLen >= _ioBuffer.size())
                        _ioBuffer.resize(_ioBuffer.size() * 2);

                    int n = recv(s,
                                 reinterpret_cast<char*>(_ioBuffer.data()) + _ioLen,
                                 static_cast<int>(_ioBuffer.size() - _ioLen),
                                 0);
                    if (n > 0)
                        _ioLen += n;
                    else if (n == 0)
                    {
                        errMsg = "TLS handshake: connection closed by peer";
                        freeSecurityContext();
                        return false;
                    }
                    else
                    {
                        int err = WSAGetLastError();
                        if (err != WSAEWOULDBLOCK && err != WSAETIMEDOUT)
                        {
                            errMsg = "TLS handshake recv failed";
                            freeSecurityContext();
                            return false;
                        }
                    }
                }
                else if (sel == 0)
                {
                    // timeout — keep looping; cancellation checked at top
                }
            }
        }

        if (!done)
        {
            errMsg = "TLS handshake did not complete";
            freeSecurityContext();
            return false;
        }

        _ctxValid = true;

        st = QueryContextAttributes(&_ctxHandle, SECPKG_ATTR_STREAM_sizes, &_streamSizes);
        if (st != SEC_E_OK)
        {
            SetSecError(errMsg, "QueryContextAttributes(STREAM_sizes) failed", st);
            freeSecurityContext();
            return false;
        }
        _streamSizesValid = true;
        _handshakeDone = true;
        _decrypted.clear();
        _decryptedPos = 0;

        return true;
    }

    bool SocketSchannel::ensureContextBuffer(size_t needed)
    {
        if (_ioBuffer.size() < needed)
            _ioBuffer.resize(needed + 0x1000);
        return true;
    }

    bool SocketSchannel::encryptAndSend(const char* data, size_t len, std::string& errMsg)
    {
        if (!_handshakeDone || !_streamSizesValid)
        {
            errMsg = "TLS not ready";
            return false;
        }

        size_t totalHeader = _streamSizes.cbHeader;
        size_t totalTrailer = _streamSizes.cbTrailer;
        size_t maxMsg = _streamSizes.cbMaximumMessage;

        size_t offset = 0;
        while (offset < len)
        {
            size_t chunk = len - offset;
            if (chunk > maxMsg)
                chunk = maxMsg;

            size_t bufLen = totalHeader + chunk + totalTrailer;
            std::vector<BYTE> buf(bufLen);

            SecBufferDesc bufDesc{};
            SecBuffer buffers[3];

            buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
            buffers[0].pvBuffer = buf.data();
            buffers[0].cbBuffer = static_cast<ULONG>(totalHeader);

            buffers[1].BufferType = SECBUFFER_DATA;
            buffers[1].pvBuffer = buf.data() + totalHeader;
            buffers[1].cbBuffer = static_cast<ULONG>(chunk);
            memcpy(buffers[1].pvBuffer, data + offset, chunk);

            buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
            buffers[2].pvBuffer = buf.data() + totalHeader + chunk;
            buffers[2].cbBuffer = static_cast<ULONG>(totalTrailer);

            bufDesc.ulVersion = SECBUFFER_VERSION;
            bufDesc.cBuffers = 3;
            bufDesc.pBuffers = buffers;

            SECURITY_STATUS st = EncryptMessage(&_ctxHandle, 0, &bufDesc, 0);
            if (st != SEC_E_OK)
            {
                SetSecError(errMsg, "EncryptMessage failed", st);
                return false;
            }

            size_t outLen = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
            int sent = 0;
            int remaining = static_cast<int>(outLen);
            char* p = reinterpret_cast<char*>(buf.data());
            while (remaining > 0)
            {
                int n = ::send(_sockfd, p + sent, remaining, 0);
                if (n <= 0)
                {
                    errMsg = "TLS send failed";
                    return false;
                }
                sent += n;
                remaining -= n;
            }

            offset += chunk;
        }

        return true;
    }

    ssize_t SocketSchannel::send(char* buffer, size_t length)
    {
        std::lock_guard<std::mutex> lock(_tlsMutex);
        std::string errMsg;
        if (!encryptAndSend(buffer, length, errMsg))
            return -1;
        return static_cast<ssize_t>(length);
    }

    bool SocketSchannel::decryptInto(void* out, size_t len, size_t& outLen, std::string& errMsg)
    {
        outLen = 0;

        // serve leftover decrypted bytes first
        if (_decryptedPos < _decrypted.size())
        {
            size_t avail = _decrypted.size() - _decryptedPos;
            size_t take = avail < len ? avail : len;
            memcpy(out, _decrypted.data() + _decryptedPos, take);
            _decryptedPos += take;
            if (_decryptedPos >= _decrypted.size())
            {
                _decrypted.clear();
                _decryptedPos = 0;
            }
            outLen = take;
            return true;
        }

        if (!_handshakeDone || !_streamSizesValid)
        {
            errMsg = "TLS not ready";
            return false;
        }

        // accumulate ciphertext until DecryptMessage succeeds
        for (;;)
        {
            if (_ioLen > 0)
            {
                std::vector<BYTE> work(_ioLen);
                memcpy(work.data(), _ioBuffer.data(), _ioLen);

                SecBufferDesc bufDesc{};
                SecBuffer buffers[4];

                buffers[0].BufferType = SECBUFFER_DATA;
                buffers[0].pvBuffer = work.data();
                buffers[0].cbBuffer = static_cast<ULONG>(_ioLen);

                buffers[1].BufferType = SECBUFFER_EMPTY;
                buffers[1].pvBuffer = nullptr;
                buffers[1].cbBuffer = 0;

                buffers[2].BufferType = SECBUFFER_EMPTY;
                buffers[2].pvBuffer = nullptr;
                buffers[2].cbBuffer = 0;

                buffers[3].BufferType = SECBUFFER_EMPTY;
                buffers[3].pvBuffer = nullptr;
                buffers[3].cbBuffer = 0;

                bufDesc.ulVersion = SECBUFFER_VERSION;
                bufDesc.cBuffers = 4;
                bufDesc.pBuffers = buffers;

                SECURITY_STATUS st = DecryptMessage(&_ctxHandle, &bufDesc, 0, nullptr);

                if (st == SEC_E_INCOMPLETE_MESSAGE)
                {
                    // need more data — fall through to recv
                }
                else if (st == SEC_I_RENEGOTIATE)
                {
                    errMsg = "TLS renegotiation not supported";
                    return false;
                }
                else if (st == SEC_E_OK)
                {
                    // find data buffer
                    size_t dataOffset = 0;
                    size_t dataLen = 0;
                    size_t consumed = 0;
                    for (ULONG i = 0; i < bufDesc.cBuffers; i++)
                    {
                        if (buffers[i].BufferType == SECBUFFER_DATA)
                        {
                            dataOffset = reinterpret_cast<BYTE*>(buffers[i].pvBuffer) - work.data();
                            dataLen = buffers[i].cbBuffer;
                        }
                        if (buffers[i].BufferType != SECBUFFER_EMPTY)
                            consumed += buffers[i].cbBuffer;
                    }

                    // leftover unconsumed ciphertext
                    size_t leftover = _ioLen > consumed ? _ioLen - consumed : 0;
                    // actually Schannel marks extra with SECBUFFER_EXTRA
                    size_t extra = 0;
                    for (ULONG i = 0; i < bufDesc.cBuffers; i++)
                    {
                        if (buffers[i].BufferType == SECBUFFER_EXTRA)
                            extra = buffers[i].cbBuffer;
                    }

                    if (dataLen > 0)
                    {
                        size_t toCopy = dataLen < len ? dataLen : len;
                        memcpy(out, work.data() + dataOffset, toCopy);
                        outLen = toCopy;

                        if (dataLen > toCopy)
                        {
                            _decrypted.assign(work.data() + dataOffset + toCopy,
                                              work.data() + dataOffset + dataLen);
                            _decryptedPos = 0;
                        }
                    }

                    // shift io buffer past consumed
                    size_t remove = _ioLen - extra;
                    if (remove > 0 && remove <= _ioLen)
                    {
                        memmove(_ioBuffer.data(), _ioBuffer.data() + remove, _ioLen - remove);
                        _ioLen -= remove;
                    }

                    errMsg.clear();
                    return true;
                }
                else
                {
                    SetSecError(errMsg, "DecryptMessage failed", st);
                    return false;
                }
            }

            // need more network data
            {
                fd_set rfds;
                FD_ZERO(&rfds);
                int s = _sockfd;
                FD_SET(s, &rfds);
                timeval tv{};
                tv.tv_sec = 0;
                tv.tv_usec = 200000;

                int sel = select(0, &rfds, nullptr, nullptr, &tv);
                if (sel > 0)
                {
                    ensureContextBuffer(_ioLen + 0x4000);
                    int n = recv(s,
                                 reinterpret_cast<char*>(_ioBuffer.data()) + _ioLen,
                                 static_cast<int>(_ioBuffer.size() - _ioLen),
                                 0);
                    if (n > 0)
                    {
                        _ioLen += n;
                        continue;
                    }
                    else if (n == 0)
                    {
                        errMsg = "TLS connection closed";
                        return false;
                    }
                    else
                    {
                        int err = WSAGetLastError();
                        if (err == WSAEWOULDBLOCK)
                        {
                            errMsg = "TLS would block";
                            return false;
                        }
                        errMsg = "TLS recv failed";
                        return false;
                    }
                }
                else if (sel == 0)
                {
                    errMsg = "TLS recv timeout";
                    return false;
                }
                else
                {
                    errMsg = "TLS select failed";
                    return false;
                }
            }
        }
    }

    ssize_t SocketSchannel::recv(void* buffer, size_t length)
    {
        std::lock_guard<std::mutex> lock(_tlsMutex);
        std::string errMsg;
        size_t got = 0;
        if (!decryptInto(buffer, length, got, errMsg))
            return -1;
        return static_cast<ssize_t>(got);
    }
}

#endif // IXWEBSOCKET_USE_SCHANNEL
