#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <chrono>
#include <cstring>

#include "transport/channel.h"
#include "transport/tcp/tcp_adapter.h"
#include "transport/tcp/tcp_channel.h"
#include "core/status.h"

namespace rdc {
static inline uint32_t channel_type_to_epoll_event(
        const ChannelType& channel_type) {
    switch(channel_type) {
        case kRead:
            return EPOLLIN;
        case kWrite:
            return EPOLLOUT;
        case kReadWrite:
            return EPOLLIN | EPOLLOUT;
        case kNone:
            return 0;
        default:
            return 0;
    }
}

static inline std::string channel_type_to_string(ChannelType channel_type) {
    switch(channel_type) {
        case kRead:
            return "read";
        case kWrite:
            return "write";
        case kReadWrite:
            return "readwrite";
        case kNone:
            return "none";
        default:
            return "none";
    }
}
TcpChannel::TcpChannel() {
    this->poller_ = nullptr;
    this->fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    this->type_ = kReadWrite;
    this->spin_.store(false, std::memory_order_release);
}
TcpChannel::TcpChannel(int32_t fd) {
    this->poller_ = nullptr;
    this->fd_ = fd;
    this->type_ = kReadWrite;
    this->spin_.store(false, std::memory_order_release);
}
TcpChannel::TcpChannel(ChannelType type) {
    this->poller_ = nullptr;
    this->fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    this->type_ = type;
    this->spin_.store(false, std::memory_order_release);
}

TcpChannel::TcpChannel(int32_t fd, ChannelType type) {
    this->poller_ = nullptr;
    this->fd_ = fd;
    this->type_ = type;
    this->spin_.store(false, std::memory_order_release);
}
TcpChannel::TcpChannel(TcpAdapter* poller, ChannelType type) {
    this->poller_ = poller;
    this->fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    this->type_ = type;
    uint32_t flags = channel_type_to_epoll_event(type);
    this->spin_.store(false, std::memory_order_release);
    this->poller_->AddChannel(this);
    epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd_;
    ev.events |= flags;
    epoll_ctl(this->poller_->epoll_fd(), EPOLL_CTL_ADD, this->fd_, &ev);
}


TcpChannel::TcpChannel(TcpAdapter* poller, int32_t fd, ChannelType type) {
    this->poller_ = poller;
    this->fd_ = fd;
    this->type_ = type;
    uint32_t flags = 0;
    if (this->type_ == kRead) {
        flags |= EPOLLIN;
    } else if (this->type_ == kWrite) {
        flags |= EPOLLOUT;
    } else {
        flags |= EPOLLIN | EPOLLOUT;
    }

    this->spin_.store(false, std::memory_order_release);
    this->poller_->AddChannel(this);
    epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd_;
    ev.events |= flags;
    epoll_ctl(this->poller_->epoll_fd(), EPOLL_CTL_ADD, 
            this->fd_, &ev);
}

TcpChannel::~TcpChannel() {
    if (this->poller_) {
        epoll_ctl(this->poller_->epoll_fd(), EPOLL_CTL_DEL,
                  this->fd_, nullptr);
    }
    this->Close();
}
void TcpChannel::Modify(const ChannelType& type) {
    type_ = type;
    epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd_;
    uint32_t flags = channel_type_to_epoll_event(type);
    ev.events |= flags;
    epoll_ctl(this->poller_->epoll_fd(), EPOLL_CTL_MOD,
            this->fd_, &ev);
}
Status TcpChannel::Connect(const std::string& hostname,
        const uint32_t& port) {
    sockaddr_in peer_addr;
    std::memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_addr.s_addr = inet_addr(hostname.c_str());
    peer_addr.sin_port = htons(port);
    LOG_F(INFO, "%s %d", hostname.c_str(), port);
    if (connect(this->fd_, (struct sockaddr*)&peer_addr,
                sizeof(peer_addr)) != 0) {
        int error = GetLastSocketError(fd_);
        LOG_F(INFO, "Fail to connect to host %s port %d : %s",
                hostname.c_str(), port, strerror(error));
        return static_cast<Status>(errno);
    }
    fcntl(this->fd_, F_SETFL, O_NONBLOCK);
    if (this->poller_ == nullptr) {
        this->poller_ = TcpAdapter::Get();
        this->poller_->AddChannel(this);
        epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.data.fd = fd_;
        ev.events |= EPOLLIN | EPOLLOUT;
        epoll_ctl(this->poller_->epoll_fd(), EPOLL_CTL_ADD, this->fd_, &ev);
    }
    return Status::kSuccess;
}

WorkCompletion TcpChannel::ISend(const void* data, size_t size) {
    uint64_t send_req_id = WorkRequestManager::Get()->
                           NewWorkRequest(kSend, data, size);
    WorkCompletion wc(send_req_id);
    if (spin_) {
        send_reqs_.NoLockPush(send_req_id);
    } else {
        send_reqs_.Push(send_req_id);
    }
    return wc;
}
WorkCompletion TcpChannel::IRecv(void* data, size_t size) {
    uint64_t recv_req_id = WorkRequestManager::Get()->
                           NewWorkRequest(kRecv, data, size);
    WorkCompletion wc(recv_req_id);
    if (spin_) {
        recv_reqs_.NoLockPush(recv_req_id);
    } else {
        recv_reqs_.Push(recv_req_id);
    }
    return wc;
}
void TcpChannel::ReadCallback() {
    uint64_t recv_req_id = -1;
    if (spin_) {
        if (!recv_reqs_.TryPeek(recv_req_id)) {
            return;
        }
    } else {
        if (!recv_reqs_.WaitAndPeek(recv_req_id,
                    std::chrono::milliseconds(kCommTimeoutMs))) {
            return;
        }
    }
    WorkRequest& recv_req = WorkRequestManager::Get()->
                            GetWorkRequest(recv_req_id);
    auto read_nbytes = read(fd_, recv_req.ptr_at<uint8_t>(
                            recv_req.completed_bytes()),
                            recv_req.remain_nbytes());
    if (read_nbytes == 0) {
        int error = GetLastSocketError(fd_);
        LOG_F(ERROR, "Error during recieving : %s", strerror(errno));
        WorkRequestManager::Get()->set_status(
            recv_req.id(), static_cast<Status>(error));
    }
    if (recv_req.AddBytes(read_nbytes)) {
        if (spin_) {
            recv_reqs_.NoLockPop();
        } else {
            recv_req.Notify();
            recv_reqs_.Pop();
        }
    }
    return;
}
void TcpChannel::WriteCallback() {
    uint64_t send_req_id;
    if (spin_) {
        if (!send_reqs_.TryPeek(send_req_id)) {
            return;
        }
    } else {
        if (!send_reqs_.WaitAndPeek(send_req_id,
                    std::chrono::milliseconds(kCommTimeoutMs))) {
            return;
        }
    }
    WorkRequest& send_req = WorkRequestManager::Get()->
                            GetWorkRequest(send_req_id);
    auto write_nbytes = write(fd_, send_req.ptr_at<uint8_t>(
                            send_req.completed_bytes()),
                            send_req.remain_nbytes());
    if (write_nbytes == 0) {
        int error = GetLastSocketError(fd_);
        LOG_F(ERROR, "Error during sending : %s", strerror(error));
        WorkRequestManager::Get()->set_status(
            send_req.id(), static_cast<Status>(error));
    }
    if (send_req.AddBytes(write_nbytes)) {
        if (spin_) {
            send_reqs_.NoLockPop();
        } else {
            send_req.Notify();
            send_reqs_.Pop();
        }
    }
    return;
}


void TcpChannel::Delete(const ChannelType& type) {
    mu_.lock();
    if (type == ChannelType::kRead) {
        if (type_ == ChannelType::kReadWrite) {
            type_ = ChannelType::kWrite;
        } else if (type_ == ChannelType::kRead) {
            type_ = ChannelType::kNone;
        } else {
            LOG_F(ERROR, "cannot delete");
        }
    } else if (type == ChannelType::kWrite) {
        if (type_ == ChannelType::kReadWrite) {
            type_ = ChannelType::kRead;
        } else if (type_ == ChannelType::kWrite) {
            type_ = ChannelType::kNone;
        } else {
            LOG_F(ERROR, "cannot delete");
        }
    } else if (type == ChannelType::kReadWrite) {
        if (type_ == ChannelType::kReadWrite) {
            type_ = ChannelType::kNone;
        } else {
            LOG_F(ERROR, "cannot delete");
        }
    }
    Modify(type_);
    mu_.unlock();
}
void TcpChannel::Add(const ChannelType& type) {
    mu_.lock();
    if (type == ChannelType::kRead) {
        if (type_ == ChannelType::kNone) {
            type_ = kRead;
        } else if (type_ == ChannelType::kWrite) {
            type_ = kReadWrite;
        } else {
            LOG_F(ERROR, "cannot add");
        }
    } else if (type == ChannelType::kWrite) {
        if (type_ == ChannelType::kNone) {
            type_ = kWrite;
        } else if (type_ == ChannelType::kRead) {
            type_ = kReadWrite;
        } else {
            LOG_F(ERROR, "cannot add");
        }
    } else if (type == ChannelType::kReadWrite) {
        if (type_ == ChannelType::kNone) {
            type_ = kReadWrite;
        } else {
            LOG_F(ERROR, "cannot add");
        }
    }
    Modify(type_);
    mu_.unlock();
}
}
