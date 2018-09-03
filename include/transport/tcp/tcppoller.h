#pragma once
#include <unordered_map>
#include <functional>
#include <vector>
#include <thread>
#include <algorithm>
#include "utils/lock_utils.h"
#include "transport/tcp/tcpchannel.h"


namespace rdc {
/**
 * @struct TcpPoller "tcppoller.h"
 * @brief tcppoller object
 */
class TcpPoller {
public:
    TcpPoller();
    static TcpPoller* Get() {
        static TcpPoller poller;
        return &poller;
    }
    /** timeout duration */
    size_t timeout_;
    /** epoll file descriptor*/
    int32_t epoll_fd_;
    std::unordered_map<int32_t, TcpChannel*> channels_;
    ~TcpPoller();
    void AddChannel(int32_t fd, TcpChannel* channel);
    void AddChannel(TcpChannel* channel);
    void RemoveChannel(TcpChannel* channel);
    void Shutdown();
    int32_t epoll_fd() const {
        return epoll_fd_;
    }
    std::unique_ptr<std::thread> loop_thrd;
    std::unique_ptr<std::thread> listen_thrd;
    void PollForever();
    int Poll();
    int Listen(const int32_t& port, const size_t& backlog = 1024);
    TcpChannel* Accept();
    int32_t shutdown_fd_;
    int32_t listen_fd_;
    bool shutdown_;
    //utils::SpinLock lock_;
    std::mutex lock_;
};
}
