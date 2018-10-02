#pragma once
#include <unordered_map>
#include <functional>
#include <vector>
#include <thread>
#include <algorithm>
#include "utils/lock_utils.h"
#include "transport/channel.h"
#include "transport/adapter.h"
#include "transport/tcp/tcp_channel.h"


namespace rdc {
/**
 * @class TcpAdapter
 * @brief tcpadapther which will govern all tcp connections
 */
class TcpAdapter: public IAdapter {
public:
    TcpAdapter();
    static TcpAdapter* Get() {
        static TcpAdapter poller;
        return &poller;
    }
    std::unordered_map<int, TcpChannel*> channels_;
    ~TcpAdapter();
    void AddChannel(int fd, TcpChannel* channel);
    void AddChannel(TcpChannel* channel);
    void RemoveChannel(TcpChannel* channel);
    void ModifyChannel(TcpChannel* channel, const ChannelKind& target_kind);
    void Shutdown();
    void PollForever();
    bool Poll();
    void Listen(const int& port);
    TcpChannel* Accept();

    int32_t epoll_fd() const {
        return epoll_fd_;
    }

    inline bool shutdown() const {
        return shutdown_.load(std::memory_order_acquire);
    }
    inline void set_shutdown(const bool& shutdown) {
        shutdown_.store(shutdown, std::memory_order_release);
    }
    inline bool shutdown_called() const {
        return shutdown_called_.load(std::memory_order_acquire);
    }
    inline void set_shutdown_called(const bool& shutdown_called) {
        shutdown_called_.store(shutdown_called, std::memory_order_release);
    }

private:
    /** timeout duration */
    int32_t timeout_;
    /** epoll file descriptor*/
    int32_t epoll_fd_;
    int32_t shutdown_fd_;
    TcpSocket listen_sock_;

    std::atomic<bool> shutdown_;
    std::atomic<bool> shutdown_called_;
    //utils::SpinLock lock_;
    std::mutex lock_;
    std::mutex shutdown_lock_;
    std::unique_ptr<std::thread> loop_thrd;
    std::unique_ptr<std::thread> listen_thrd;
};

}
