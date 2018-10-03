#pragma once
#include <string>
#include "rdc.h"
#include "core/status.h"
#include "transport/buffer.h"
#include "transport/adapter.h"

namespace rdc {
const uint32_t kCommTimeoutMs = 600;

enum ChannelKind : uint32_t {
    kRead,
    kWrite,
    kReadWrite,
    kNone,
};

class IChannel {
public:
    IChannel() = default;
    IChannel(const ChannelKind& kind) : kind_(kind), error_detected_(false) {}
    virtual ~IChannel() = default;
    virtual WorkCompletion ISend(Buffer sendbuf) = 0;
    virtual WorkCompletion IRecv(Buffer recvbuf) = 0;
    virtual void Close() = 0;
    virtual bool Connect(const std::string& host, const uint32_t& port) = 0;
    bool Connect(const std::string& addr_str) {
        Backend backend;
        std::string host;
        uint32_t port;
        std::tie(backend, host, port) = ParseAddr(addr_str);
        return Connect(host, port);
    }
    WorkCompletion ISend(const void* sendaddr, const uint64_t& sendbytes) {
        Buffer sendbuf(sendaddr, sendbytes);
        return this->ISend(sendbuf);
    }
    WorkCompletion IRecv(void* recvaddr, const uint64_t& recvbytes) {
        Buffer recvbuf(recvaddr, recvbytes);
        return this->IRecv(recvbuf);
    }

    inline bool SendInt(int32_t val) {
        auto wc = this->ISend(&val, sizeof(int32_t));
        wc.Wait();
        return wc.status();
    }
    inline bool SendStr(std::string str) {
        int32_t size = static_cast<int32_t>(str.size());
        ChainWorkCompletion wc;
        wc << this->ISend(&size, sizeof(size));
        wc << this->ISend(utils::BeginPtr(str), str.size());
        wc.Wait();
        return wc.status();
    }
    inline bool RecvInt(int32_t& val) {
        auto wc = this->IRecv(&val, sizeof(int32_t));
        wc.Wait();
        return wc.status();
    }
    inline bool RecvStr(std::string& str) {
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
    inline ChannelKind kind() const {
        return kind_;
    }
    inline void set_kind(const ChannelKind& kind) {
        kind_ = kind;
    }
private:
    std::atomic<bool> error_detected_;
    ChannelKind kind_;
};
}
