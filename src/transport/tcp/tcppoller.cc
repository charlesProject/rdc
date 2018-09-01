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
#include "transport/tcp/tcppoller.h"
#include "transport/tcp/tcpchannel.h"
#include "core/logging.h"
#include "core/status.h"

static const uint32_t kNumMaxEvents=128;

namespace rdc {
TcpPoller::TcpPoller() {
    epoll_fd_ = epoll_create(kNumMaxEvents);
    PollForever();
}

void TcpPoller::PollForever() {
    loop_thrd = std::unique_ptr<std::thread>(new std::thread([=](){
        logging::set_thread_name("tcppoller");
        LOG_S(INFO) << "Tcp poller started";
        for(;;) {
            int ret = Poll();
            if (ret) break;
        }
    }));
}
void TcpPoller::Shutdown() {
    int pipe_fd[2];
    pipe(pipe_fd);
    int flags = EPOLLIN;
    this->shutdown_fd_ = pipe_fd[0];
    epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.data.fd = shutdown_fd_;
    ev.events |= flags;
    epoll_ctl(this->epoll_fd_, EPOLL_CTL_ADD, this->shutdown_fd_, &ev);
    write(pipe_fd[1], "shutdown", 9);
}
/**
  * Function which processes the events from epoll_wait and calls the appropriate callbacks
  * @note only process events once if you need to use an event loop use TcpPoller_loop
*/
int TcpPoller::Poll() {
    epoll_event events[kNumMaxEvents];
    size_t fds = epoll_wait(this->epoll_fd_, events, 
                            kNumMaxEvents, this->timeout_);
    for (size_t i = 0; i < fds; i++) {
        TcpChannel * channel = nullptr;
        lock_.lock();
        channel = this->channels_[events[i].data.fd];
        lock_.unlock();
        if (channel) {
            // when data avaliable for read or urgent flag is set
            if (events[i].events & EPOLLIN || events[i].events & EPOLLPRI) {
                if (events[i].events & EPOLLIN) {
                    if (this->shutdown_fd_) {
                        if (events[i].data.fd == this->shutdown_fd_) {
                            return 1;
                        }
                    }
                }
                //if (GetRank() == 1) LOG(INFO);
                channel->ReadCallback();
            }
            // when write possible
            if (events[i].events & EPOLLOUT) {
                channel->WriteCallback();
            }
            // shutdown or error
            if (events[i].events & EPOLLRDHUP || events[i].events & EPOLLERR || 
                    events[i].events & EPOLLHUP) {
                return 0;
            }
        } // if
        else {
            return 0;
        }
    } // for
    return 0;
}
int TcpPoller::Listen(const int32_t& port, const size_t& backlog) {
    this->listen_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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
    if (listen(this->listen_fd_, backlog) != 0) {
        LOG_S(ERROR) << "Fail to listen on port " << port
                     << " :" << strerror(errno);
    }
    int32_t opt = 0;
    setsockopt(this->listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               &opt,sizeof(opt));
    return 0;
}

std::unique_ptr<TcpChannel> TcpPoller::Accept() {
    // accept the connection 
    sockaddr_in incoming_addr;
    socklen_t incoming_addr_len = sizeof(incoming_addr);
    int32_t accepted_fd = accept(this->listen_fd_,
                                 (struct sockaddr*)&incoming_addr,
                                 &incoming_addr_len);
    CHECK_F(accepted_fd > 0);
    fcntl(accepted_fd, F_SETFL, O_NONBLOCK);
    // set flags to check
    return utils::make_unique<TcpChannel>(this, accepted_fd, kReadWrite);
}
}
