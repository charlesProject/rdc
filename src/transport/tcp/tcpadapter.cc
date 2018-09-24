#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <unordered_map>
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <atomic>
#include "transport/tcp/tcp_adapter.h"
#include "transport/tcp/tcp_channel.h"
#include "core/threadpool.h"
#include "core/logging.h"
#include "core/status.h"

static const uint32_t kNumMaxEvents = 32;

namespace rdc {

static inline bool IsRead(uint32_t events) {
    return (events & EPOLLIN || events & EPOLLPRI);
}
static inline bool IsWrite(uint32_t events) {
    return events & EPOLLOUT;
}

static inline bool IsReadOnly(uint32_t events) {
    return IsRead(events) && !IsWrite(events);
}

static inline bool IsWriteOnly(uint32_t events) {
    return IsWrite(events) && !IsRead(events);
}
static inline bool IsReadWrite(uint32_t events) {
    return IsRead(events) && IsWrite(events);
}

static inline bool IsError(uint32_t events) {
    return (events & EPOLLERR || events & EPOLLHUP
            || events & EPOLLRDHUP);
}
TcpAdapter::TcpAdapter() {
    this->set_backend(kTcp);
    this->listen_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    this->shutdown_called_ = false;
    this->timeout_ = -1;
    this->epoll_fd_ = epoll_create(kNumMaxEvents);
    PollForever();
}

void TcpAdapter::PollForever() {
    loop_thrd = std::unique_ptr<std::thread>(
        new std::thread([this]() {
          logging::set_thread_name("tcppoller");
          LOG_F(INFO, "Tcp poller Started");
          while (true) {
              bool ret = Poll();
              if (ret) break;
          }
    }));
}
TcpAdapter::~TcpAdapter() {
    this->Shutdown();
    this->loop_thrd->join();
    CloseSocket(this->epoll_fd_);
    CloseSocket(this->listen_fd_);
}
void TcpAdapter::AddChannel(int32_t fd, TcpChannel* channel) {
    lock_.lock();
    channels_[fd] = channel;
    LOG_S(INFO) << "Added new channel with fd :" << fd;
    lock_.unlock();
}
void TcpAdapter::AddChannel(TcpChannel* channel) {
    lock_.lock();
    channels_[channel->fd()] = channel;
    LOG_S(INFO) << "Added new channel with fd :" << channel->fd();
    lock_.unlock();
}

void TcpAdapter::RemoveChannel(TcpChannel* channel) {
    lock_.lock();
    channels_.erase(channel->fd());
    lock_.unlock();
}

void TcpAdapter::Shutdown() {
    shutdown_lock_.lock();
    if (!this->shutdown_called_) {
        this->shutdown_called_ = true;
        int pipe_fd[2];
        pipe(pipe_fd);
        int flags = EPOLLIN;
        this->shutdown_fd_ = pipe_fd[0];
        epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.data.fd = shutdown_fd_;
        ev.events |= flags;
        epoll_ctl(this->epoll_fd_, EPOLL_CTL_ADD,
                this->shutdown_fd_, &ev);
        write(pipe_fd[1], "shutdown", 9);
    }
    shutdown_lock_.unlock();
}
/**
  * Function which processes the events from epoll_wait and calls the appropriate callbacks
  * @note only process events once if you need to use an event loop use TcpAdapter_loop
  * @return shutdown
*/
bool TcpAdapter::Poll() {
    epoll_event events[kNumMaxEvents];
    int fds = epoll_wait(this->epoll_fd_, events,
                            kNumMaxEvents, this->timeout_);
    for (int i = 0; i < fds; i++) {
        TcpChannel* channel = nullptr;
        lock_.lock();
        channel = this->channels_[events[i].data.fd];
        lock_.unlock();
        if (channel) {
            // when data avaliable for read or urgent flag is set
            if (IsRead(events[i].events)) {
                if (events[i].events & EPOLLIN) {
                    if (this->shutdown_fd_) {
                        if (events[i].data.fd == this->shutdown_fd_) {
                            this->shutdown_ = true;
                        }
                    }
                }
                channel->Delete(ChannelType::kRead);
                ThreadPool::Get()->AddTask([channel, this] {
                    channel->ReadCallback();
                    this->shutdown_lock_.lock();
                    if (!this->shutdown_called_) {
                        channel->Add(ChannelType::kRead);
                    }
                    this->shutdown_lock_.unlock();
                });
            }

            // when write possible
            if (IsWrite(events[i].events)) {
                channel->Delete(ChannelType::kWrite);
                ThreadPool::Get()->AddTask([channel, this] {
                    channel->WriteCallback();
                    this->shutdown_lock_.lock();
                    if (!this->shutdown_called_) {
                        channel->Add(ChannelType::kWrite);
                    }
                    this->shutdown_lock_.unlock();
                });
            }
            // shutdown or error
            if (IsError(events[i].events)) {
                int32_t error = GetLastSocketError(events[i].data.fd);
                LOG_F(ERROR, "%s", strerror(error));
                return true;
            }
        } // if
    } // for
    if (shutdown_) {
        return true;
    }
    return false;
}
int TcpAdapter::Listen(const uint32_t& port) {
    struct sockaddr_in own_addr;
    std::memset(&own_addr, 0 , sizeof(own_addr));
    own_addr.sin_family = AF_INET;
    own_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    own_addr.sin_port = htons(port);
    if(bind(this->listen_fd_, (struct sockaddr *) &own_addr,
            sizeof(own_addr)) != 0) {
        LOG_S(ERROR) << "Fail to bind on port " << port
                     << " :" << strerror(errno);
    };
    if (listen(this->listen_fd_, kNumBacklogs) != 0) {
        LOG_S(ERROR) << "Fail to listen on port " << port
                     << " :" << strerror(errno);
    }
    int32_t opt = 0;
    setsockopt(this->listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               &opt,sizeof(opt));
    return 0;
}

TcpChannel* TcpAdapter::Accept() {
    // accept the connection
    sockaddr_in incoming_addr;
    socklen_t incoming_addr_len = sizeof(incoming_addr);
    int32_t accepted_fd = accept(this->listen_fd_,
                                 (struct sockaddr*)&incoming_addr,
                                 &incoming_addr_len);
    CHECK_F(accepted_fd > 0);
    fcntl(accepted_fd, F_SETFL, O_NONBLOCK);
    // set flags to check
    return new TcpChannel(this, accepted_fd, kReadWrite);
}
}
