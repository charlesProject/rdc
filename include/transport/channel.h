#pragma once
#include <string>
#include "common/status.h"
#include "rdc.h"
#include "transport/adapter.h"
#include "transport/buffer.h"

namespace rdc {
const uint32_t kCommTimeoutMs = 600;

enum class ChannelKind : uint32_t {
    kRead,
    kWrite,
    kReadWrite,
    kNone,
};

std::string ChannelKindToString(const ChannelKind& channel_kind);

class IChannel {
public:
    IChannel() = default;
    IChannel(const ChannelKind& kind);

    virtual ~IChannel() = default;

    virtual WorkCompletion* ISend(Buffer sendbuf) = 0;

    virtual WorkCompletion* IRecv(Buffer recvbuf) = 0;

    virtual void Close() = 0;

    virtual bool Connect(const std::string& host, const uint32_t& port) = 0;

    bool Connect(const std::string& addr_str);

    WorkCompletion* ISend(const void* sendaddr, const uint64_t& sendbytes);

    WorkCompletion* IRecv(void* recvaddr, const uint64_t& recvbytes);

    WorkStatus SendInt(int32_t val);

    WorkStatus SendStr(std::string str);

    WorkStatus RecvInt(int32_t& val);

    WorkStatus RecvStr(std::string& str);

    bool CheckError() const;
    
    bool CanRead() const;
    
    bool CanWrite() const;
    //---------------------------properties--------------------------------
    void set_error_detected(const bool& error_detected);

    bool error_detected() const;

    ChannelKind kind() const;
    
    void set_kind(const ChannelKind& kind);

private:
    ChannelKind kind_;
    std::atomic<bool> error_detected_;
};
}  // namespace rdc
