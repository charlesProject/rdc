#pragma once
#include <unordered_map>
#include <functional>
#include <vector>
#include <thread>
#include <algorithm>
#include "utils/lock_utils.h"
#include "transport/tcp/tcpchannel.h"


namespace rabit {
/**
 * @struct TcpPoller "tcppoller.h"
 * @brief reactor object 
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
    ~TcpPoller() {
        this->Shutdown();
        loop_thrd->join();
        listen_thrd->join();
    }
    void AddChannel(int32_t fd, TcpChannel* channel) {
        lock_.lock();
        channels_[fd] = channel;
        LOG_S(INFO) << "Added new channel with fd :" << fd;
        lock_.unlock();
    }
    void AddChannel(TcpChannel* channel) {
        lock_.lock();
        channels_[channel->fd()] = channel;
        LOG_S(INFO) << "Added new channel with fd :" << channel->fd();
        lock_.unlock();
    }
    void Shutdown();
    int32_t epoll_fd() const {
        return epoll_fd_;
    }
    std::unique_ptr<std::thread> loop_thrd;
    std::unique_ptr<std::thread> listen_thrd;
    void PollForever();
    int Poll();
    int Listen(const int32_t& port, const size_t& backlog = 1024); 
    std::unique_ptr<TcpChannel> Accept();
    int32_t shutdown_fd_;
    int32_t listen_fd_;
    utils::SpinLock lock_;
    //std::mutex lock_;
};
}
