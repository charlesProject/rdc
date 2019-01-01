#include "comm/tracker.h"
#include <thread>
#include "common/env.h"
#include "comm/communicator_manager.h"
#include "sys/network.h"
#include "transport/adapter.h"
#include "utils/string_utils.h"

namespace rdc {
namespace comm {

std::atomic<Tracker*> Tracker::instance(nullptr);
std::mutex Tracker::create_mutex;
std::atomic<bool> Tracker::created(false);

Tracker::Tracker() {
    rank_ = -1;
    world_size_ = -1;
    tracker_uri_ = "NULL";
    tracker_port_ = 9000;
    tracker_connected_.store(false);
    tracker_closed_ = false;
    tracker_lock_ = std::make_shared<std::mutex>();
    worker_port_ = 9910;
    host_uri_ = "";
    connect_retry_ = 5;
}

Tracker::Tracker(const std::string& tracker_uri, const int& tracker_port)
    : Tracker() {
    tracker_uri_ = tracker_uri;
    tracker_port_ = tracker_port;
    this->Connect("start");
}

Tracker::~Tracker() {
    if (!this->tracker_closed_.load(std::memory_order_acquire)) {
        Lock();
        this->tracker_closed_.store(true, std::memory_order_release);
        this->tracker_sock_->Close();
        UnLock();
    }
}

Tracker* Tracker::Get() {
    bool created_ = created.load(std::memory_order_acquire);
    Tracker* instance_ = instance.load(std::memory_order_relaxed);
    if (created_ == false) {
        std::lock_guard<std::mutex> lock(create_mutex);
        created_ = created.load(std::memory_order_relaxed);
        //instance_ = instance.load(std::memory_order_acquire);
        if (created_ == false) {
            auto&& tracker_uri = CommunicatorManager::Get()->tracker_uri();
            auto&& tracker_port = CommunicatorManager::Get()->tracker_port();
            instance_ = new Tracker(tracker_uri, tracker_port);
            instance.store(instance_, std::memory_order_relaxed);
            created.store(true, std::memory_order_release);
        }
    }
    return instance_;
}

void Tracker::Release() {
    bool created_ = false;
    while ((created_ = created.load(std::memory_order_acquire)) == false) {
        continue;
    }
    Tracker* instance_ = instance.load(std::memory_order_relaxed);
    delete instance_;
    instance.store(nullptr, std::memory_order_relaxed);
}

void Tracker::Lock() const {
    tracker_lock_->lock();
}

void Tracker::UnLock() const {
    tracker_lock_->unlock();
}

void Tracker::SendInt(const int32_t& value) {
    tracker_sock_->SendInt(value);
}

void Tracker::SendStr(const std::string& str) {
    tracker_sock_->SendStr(str);
}

void Tracker::SendBytes(void* buf, int32_t size) {
    tracker_sock_->SendBytes(buf, size);
}

void Tracker::RecvInt(int32_t& value) {
    tracker_sock_->RecvInt(value);
}

void Tracker::RecvStr(std::string& str) {
    tracker_sock_->RecvStr(str);
}

void Tracker::RecvBytes(void* buf, int32_t& size) {
    tracker_sock_->RecvBytes(buf, size);
}

std::tuple<int, int> Tracker::Connect(const char* cmd) {
    std::string interface, ip;
    network::GetAvailableInterfaceAndIP(&interface, &ip);
    worker_port_ = network::GetAvailablePort();
    this->host_uri_ = ip;
    // get information from tracker
    tracker_lock_->lock();
    if (!tracker_connected()) {
        tracker_sock_ = std::make_shared<TcpSocket>();
        int retry = 0;
        do {
            if (!tracker_sock_->Connect(tracker_uri_.c_str(), tracker_port_)) {
                if (++retry >= connect_retry_) {
                    LOG_F(ERROR, "connect to (failed): [%s:%d]\n",
                          tracker_uri_.c_str(), tracker_port_);
                    LOG_F(ERROR, "Connect");
                } else {
                    LOG_F(ERROR, "retry connect to ip(retry time %d): [%s:%d]\n",
                          retry, tracker_uri_.c_str(), tracker_port_);
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }
            break;
        } while (true);
        this->set_tracker_connected(true);
        // start listener at very begining
        GetAdapter()->Listen(worker_port_);
        tracker_sock_->SendStr(std::string(cmd));
        rank_ = Env::Get()->GetEnv("RDC_RANK", -1);
        // first send my rank to tracker for global rank scheduling
        tracker_sock_->SendInt(rank_);
        // send my addr to tracker for decision making
        auto backend_str = GetAdapter()->backend_str();
        auto host_addr = str_utils::SPrintf("%s:%s:%d", backend_str.c_str(),
                                            host_uri_.c_str(), worker_port_);
        tracker_sock_->SendStr(host_addr);

        tracker_sock_->RecvInt(world_size_);
        VLOG_F(2, "workd size %d", world_size_);
        // recieve my new rank from tracker
        tracker_sock_->RecvInt(rank_);
        VLOG_F(2, "new rank %d", rank_);
        // get number of to connect and number of to accept nodes from tracker
        tracker_sock_->RecvInt(num_conn_);

        VLOG_F(2, "number peers need to connect %d", num_conn_);
        tracker_sock_->RecvInt(num_accept_);

        VLOG_F(2, "number peers need to accept %d", num_accept_);
        for (int i = 0; i < num_conn_; ++i) {
            std::string haddr;
            int hrank = -1;
            tracker_sock_->RecvStr(haddr);
            tracker_sock_->RecvInt(hrank);
            peer_addrs_[hrank] = haddr;
        }
    }
    tracker_lock_->unlock();
    tracker_sema_.Signal();
    return std::tie(num_conn_, num_accept_);
}

void Tracker::TrackerPrint(const std::string& msg) {
    tracker_lock_->lock();
    SendStr(std::string("print"));
    SendStr(msg);
    tracker_lock_->unlock();
}

}  // namespace comm
}  // namespace rdc
