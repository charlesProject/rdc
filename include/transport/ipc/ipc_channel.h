#pragma once
#include <unistd.h>
#include <atomic>
#include "common/threadsafe_queue.h"
#include "core/work_request.h"
#include "transport/channel.h"
namespace rdc {
class IpcAdapter;
/**
 * @brief: a channel which send and recv data on tcp protocal and ethernet
 */
class IpcChannel final : public IChannel {
public:
    IpcChannel(const std::string& path, int size);
    IpcChannel(IpcAdapter* adapter);
    virtual ~IpcChannel() override;
    bool Connect(const std::string& hostname, const uint32_t& port) override;
    WorkCompletion* ISend(Buffer sendbuf) override;
    WorkCompletion* IRecv(Buffer recvbuf) override;

    void Close() override;

    void set_adapter(IpcAdapter* adapter) {
        adapter_ = adapter;
    }

    int fd() const {
        return sock_.sockfd;
    }
private:
    // send recv request queue
    ThreadsafeQueue<uint64_t> send_reqs_;
    ThreadsafeQueue<uint64_t> recv_reqs_;
    /** only used to enable accept and listen callbacks */
    IpcAdapter* adapter_;
    Shm shm_;
};
}  // namespace rdc
