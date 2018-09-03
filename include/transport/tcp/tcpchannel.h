#pragma once
#include <atomic>
#include <unistd.h>
#include "core/threadsafe_queue.h"
#include "core/work_request.h"
#include "transport/channel.h"
namespace rdc {
/**
 * @struct TcpChannel
 * @brief a channel which send and recv data on tcp protocal and ethernet
 */
struct TcpPoller;
const int INVALID_SOCKET = -1;
class TcpChannel final: public IChannel {
public:
    TcpChannel();
    TcpChannel(int32_t fd);
    TcpChannel(ChannelType type);
    TcpChannel(int32_t fd, ChannelType type);
    TcpChannel(TcpPoller* poller, ChannelType type);
    TcpChannel(TcpPoller* poller, int32_t fd, ChannelType type);
    ~TcpChannel();
    /**
      * Function to add this channel to the poller obeject
      * @note if add is performed on an fd already in Reactor, the flags are updated in the existing object
      * @param Reactor poll event object which fd has to be added
      * @param flags events flags from epoll
    */
    Status Connect(const std::string& hostname, const int32_t& port);
    WorkCompletion ISend(const void* data, size_t size) override;
    WorkCompletion IRecv(void* data, size_t size) override;

    void Close() override {
        if (fd_ != INVALID_SOCKET) {
#ifdef _WIN32
            closesocket(fd_);
#else
            close(fd_);
#endif
        }
    }

    void ReadCallback();
    void WriteCallback();

    int32_t fd() const {
        return fd_;
    }

    bool IsClosed() const {
        return fd_ == INVALID_SOCKET;
    }

private:
    int32_t fd_;
    // send recv request queue
    ThreadsafeQueue<uint64_t> send_reqs_;
    ThreadsafeQueue<uint64_t> recv_reqs_;
    /** only used to enable accept and listen callbacks */
    TcpPoller* poller_;
    ChannelType type_;
};
}
