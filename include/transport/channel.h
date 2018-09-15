#pragma once
#include <string>
#include "rdc.h"
#include "core/status.h"
#include "transport/adapter.h"
namespace rdc {
enum ChannelType : uint32_t {
    kRead,
    kWrite,
    kReadWrite,
    kNone,
};

class IChannel {
public:
    virtual WorkCompletion ISend(const void* sendbuf, size_t sendbytes) = 0;
    virtual WorkCompletion IRecv(void* recvbuf, size_t recvbytes) = 0;
    virtual void Close() = 0;
    virtual Status Connect(const std::string& host, const uint32_t& port) = 0;
    Status Connect(const std::string& addr_str) {
        Backend backend;
        std::string host;
        uint32_t port;
        std::tie(backend, host, port) = ParseAddr(addr_str);
        return Connect(host, port);
    }
    inline Status SendInt(int32_t val) {
        auto wc = this->ISend(&val, sizeof(int32_t));
        wc.Wait();
        return wc.status();
    }
    inline Status SendStr(std::string str) {
        int32_t size = static_cast<int32_t>(str.size());
        ChainWorkCompletion wc;
        wc << this->ISend(&size, sizeof(size));
        wc << this->ISend(utils::BeginPtr(str), str.size());
        wc.Wait();
        return wc.status();
    }
    inline Status RecvInt(int32_t& val) {
        auto wc = this->IRecv(&val, sizeof(int32_t));
        wc.Wait();
        return wc.status();
    }
    inline Status RecvStr(std::string& str) {
        int32_t size = 0;
        auto wc = this->IRecv(&size, sizeof(int32_t));
        wc.Wait();
        str.resize(size);
        wc = this->IRecv(utils::BeginPtr(str), str.size());
        wc.Wait();
        return wc.status();
    }
};
}
