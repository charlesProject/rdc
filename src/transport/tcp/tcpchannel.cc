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
TcpChannel::TcpChannel() {
    this->adapter_ = nullptr;
    this->fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    this->set_type(kReadWrite);
    this->set_spin(false);
}
TcpChannel::TcpChannel(int32_t fd) {
    this->adapter_ = nullptr;
    this->fd_ = fd;
    this->set_type(kReadWrite);
    this->set_spin(false);
}
TcpChannel::TcpChannel(ChannelType type) {
    this->adapter_ = nullptr;
    this->fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    this->set_type(type);
    this->set_spin(false);
}

TcpChannel::TcpChannel(int32_t fd, ChannelType type) {
    this->adapter_ = nullptr;
    this->fd_ = fd;
    this->set_type(type);
    this->set_spin(false);
}
TcpChannel::TcpChannel(TcpAdapter* adapter, ChannelType type) {
    this->adapter_ = adapter;
    this->fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    this->set_type(type);
    this->set_spin(false);
    this->adapter_->AddChannel(this);
}


TcpChannel::TcpChannel(TcpAdapter* adapter, int32_t fd, ChannelType type) {
    this->adapter_ = adapter;
    this->fd_ = fd;
    this->set_type(type);
    this->spin_.store(false, std::memory_order_release);
    this->adapter_->AddChannel(this);
}

TcpChannel::~TcpChannel() {
    if (this->adapter_) {
        adapter_->RemoveChannel(this);
    }
    this->Close();
}
void TcpChannel::ModifyType(const ChannelType& type) {
    this->set_type(type);
    if (this->adapter_) {
        adapter_->ModifyChannel(this, type);
    }
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
    if (this->adapter_ == nullptr) {
        this->adapter_ = TcpAdapter::Get();
        this->adapter_->AddChannel(this);
    }
    return Status::kSuccess;
}

WorkCompletion TcpChannel::ISend(const Buffer sendbuf) {
    uint64_t send_req_id = WorkRequestManager::Get()->
        NewWorkRequest(kSend, sendbuf.addr(), sendbuf.size_in_bytes());
    WorkCompletion wc(send_req_id);
    if (spin_) {
        send_reqs_.NoLockPush(send_req_id);
    } else {
        send_reqs_.Push(send_req_id);
    }
    return wc;
}
WorkCompletion TcpChannel::IRecv(Buffer recvbuf) {
    uint64_t recv_req_id = WorkRequestManager::Get()->
        NewWorkRequest(kRecv, recvbuf.addr(), recvbuf.size_in_bytes());
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
    if (this->spin()) {
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
                recv_req.completed_bytes()), recv_req.remain_nbytes());
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
    if (this->spin()) {
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
                send_req.completed_bytes()), send_req.remain_nbytes());
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


void TcpChannel::DeleteCarefulEvent(const ChannelType& type) {
    mu_.lock();
    if (type == ChannelType::kRead) {
        if (this->type() == ChannelType::kReadWrite) {
            this->set_type(ChannelType::kWrite);
        } else if (this->type() == ChannelType::kRead) {
            this->set_type(ChannelType::kNone);
        } else {
            LOG_F(ERROR, "cannot delete");
        }
    } else if (type == ChannelType::kWrite) {
        if (this->type() == ChannelType::kReadWrite) {
            this->set_type(ChannelType::kRead);
        } else if (this->type() == ChannelType::kWrite) {
            this->set_type(ChannelType::kNone);
        } else {
            LOG_F(ERROR, "cannot delete");
        }
    } else if (type == ChannelType::kReadWrite) {
        if (this->type() == ChannelType::kReadWrite) {
            this->set_type(ChannelType::kNone);
        } else {
            LOG_F(ERROR, "cannot delete");
        }
    }
    ModifyType(this->type());
    mu_.unlock();
}
void TcpChannel::AddCarefulEvent(const ChannelType& type) {
    mu_.lock();
    if (type == ChannelType::kRead) {
        if (this->type() == ChannelType::kNone) {
            this->set_type(kRead);
        } else if (this->type() == ChannelType::kWrite) {
            this->set_type(kReadWrite);
        } else {
            LOG_F(ERROR, "cannot add");
        }
    } else if (type == ChannelType::kWrite) {
        if (this->type() == ChannelType::kNone) {
            this->set_type(kWrite);
        } else if (this->type() == ChannelType::kRead) {
            this->set_type(kReadWrite);
        } else {
            LOG_F(ERROR, "cannot add");
        }
    } else if (type == ChannelType::kReadWrite) {
        if (this->type() == ChannelType::kNone) {
            this->set_type(kReadWrite);
        } else {
            LOG_F(ERROR, "cannot add");
        }
    }
    ModifyType(this->type());
    mu_.unlock();
}
}
