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
    this->sock_ = TcpSocket();
    this->set_kind(kReadWrite);
    this->set_spin(false);
}
TcpChannel::TcpChannel(const ChannelKind& kind) {
    this->adapter_ = nullptr;
    this->sock_ = TcpSocket();
    this->set_kind(kind);
    this->set_spin(false);
}

TcpChannel::TcpChannel(TcpAdapter* adapter, const ChannelKind& kind) {
    this->adapter_ = adapter;
    this->sock_ = TcpSocket();
    this->set_kind(kind);
    this->set_spin(false);
    this->adapter_->AddChannel(this);
}

TcpChannel::TcpChannel(TcpAdapter* adapter, const int& sockfd,
        const ChannelKind& kind) {
    this->adapter_ = adapter;
    this->sock_ = TcpSocket(sockfd);
    this->set_kind(kind);
    this->set_spin(false);
    this->adapter_->AddChannel(this);
}
TcpChannel::TcpChannel(TcpAdapter* adapter, const TcpSocket& sock,
        const ChannelKind& kind) {
    this->adapter_ = adapter;
    this->sock_ = sock;
    this->set_kind(kind);
    this->set_spin(false);
    this->adapter_->AddChannel(this);
}

TcpChannel::~TcpChannel() {
    if (this->adapter_) {
        adapter_->RemoveChannel(this);
    }
    this->Close();
}
void TcpChannel::ModifyKind(const ChannelKind& kind) {
    this->set_kind(kind);
    if (this->adapter_) {
        adapter_->ModifyChannel(this, kind);
    }
}
bool TcpChannel::Connect(const std::string& hostname,
        const uint32_t& port) {
    LOG_F(INFO, "%s %d", hostname.c_str(), port);
    if (!sock_.Connect(hostname, port)) {
        return false;
    }
    sock_.SetNonBlock(true);
    if (this->adapter_ == nullptr) {
        this->set_adapter(TcpAdapter::Get());
        this->adapter_->AddChannel(this);
    }
    return true;
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
    auto read_nbytes = sock_.Recv(recv_req.ptr_at<uint8_t>(
                recv_req.completed_bytes()), recv_req.remain_nbytes());
    if (read_nbytes == 0) {
        WorkRequestManager::Get()->set_status(recv_req.id(), false);
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
    auto write_nbytes = sock_.Send(send_req.ptr_at<uint8_t>(
                send_req.completed_bytes()), send_req.remain_nbytes());
    if (write_nbytes == 0) {
        WorkRequestManager::Get()->set_status(send_req.id(), false);
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


void TcpChannel::DeleteCarefulEvent(const ChannelKind& kind) {
    mu_.lock();
    if (kind == ChannelKind::kRead) {
        if (this->kind() == ChannelKind::kReadWrite) {
            this->set_kind(ChannelKind::kWrite);
        } else if (this->kind() == ChannelKind::kRead) {
            this->set_kind(ChannelKind::kNone);
        } else {
            LOG_F(ERROR, "cannot delete");
        }
    } else if (kind == ChannelKind::kWrite) {
        if (this->kind() == ChannelKind::kReadWrite) {
            this->set_kind(ChannelKind::kRead);
        } else if (this->kind() == ChannelKind::kWrite) {
            this->set_kind(ChannelKind::kNone);
        } else {
            LOG_F(ERROR, "cannot delete");
        }
    } else if (kind == ChannelKind::kReadWrite) {
        if (this->kind() == ChannelKind::kReadWrite) {
            this->set_kind(ChannelKind::kNone);
        } else {
            LOG_F(ERROR, "cannot delete");
        }
    }
    ModifyKind(this->kind());
    mu_.unlock();
}
void TcpChannel::AddCarefulEvent(const ChannelKind& kind) {
    mu_.lock();
    if (kind == ChannelKind::kRead) {
        if (this->kind() == ChannelKind::kNone) {
            this->set_kind(kRead);
        } else if (this->kind() == ChannelKind::kWrite) {
            this->set_kind(kReadWrite);
        } else {
            LOG_F(ERROR, "cannot add");
        }
    } else if (kind == ChannelKind::kWrite) {
        if (this->kind() == ChannelKind::kNone) {
            this->set_kind(kWrite);
        } else if (this->kind() == ChannelKind::kRead) {
            this->set_kind(kReadWrite);
        } else {
            LOG_F(ERROR, "cannot add");
        }
    } else if (kind == ChannelKind::kReadWrite) {
        if (this->kind() == ChannelKind::kNone) {
            this->set_kind(kReadWrite);
        } else {
            LOG_F(ERROR, "cannot add");
        }
    }
    ModifyKind(this->kind());
    mu_.unlock();
}
}
