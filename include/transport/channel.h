#pragma once
#include <string>
#include "rdc.h"
#include "core/status.h"
#include "transport/adapter.h"

namespace rdc {
const uint32_t kCommTimeoutMs = 600;

enum ChannelType : uint32_t {
    kRead,
    kWrite,
    kReadWrite,
    kNone,
};

class IChannel {
public:
    IChannel() = default;
    IChannel(const ChannelType& type) : type_(type) {}
    virtual ~IChannel() = 0;
    virtual WorkCompletion ISend(const Buffer& sendbuf) = 0;
    virtual WorkCompletion IRecv(Buffer& recvbuf) = 0;
    WorkCompletion ISend(const void* sendaddr, size_t sendbytes) {
        Buffer sendbuf(sendaddr, sendbytes);
        return this->ISend(sendbuf);
    }
    WorkCompletion IRecv(void* recvaddr, size_t recvbytes) {
        Buffer recvbuf(recvaddr, recvbytes);
        return this->IRecv(recvbuf);
    }
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
    inline bool CheckError() const {
        return error_detected_.load(std::memory_order_acquire);
    }
    inline void set_error_detected(const bool& error_detected) {
        error_detected_.store(error_detected, std::memory_order_release);
    }
    inline bool error_detected() const {
        return error_detected_.load(std::memory_order_acquire);
    }
    inline ChannelType type() const {
        return type_;
    }
    inline void set_type(const ChannelType& type) {
        type_ = type;
    }
private:
    std::atomic<bool> error_detected_;
    ChannelType type_;
};
}
