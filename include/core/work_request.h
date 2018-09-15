#pragma once
#include <unistd.h>
#include <cstring>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <condition_variable>
#include "rdc.h"
#include "utils/utils.h"
#include "utils/lock_utils.h"
#include "core/status.h"
#include "core/logging.h"
namespace rdc {
enum WorkType : uint32_t {
    kSend,
    kRecv,
};
struct WorkRequest {
    WorkRequest(): done_(false), completed_bytes_(0) {};
    WorkRequest(const uint64_t& req_id, const WorkType& work_type,
        void* ptr, const size_t& size) : req_id_(req_id),
            work_type_(work_type), done_(false),
            ptr_(ptr), size_in_bytes_(size), completed_bytes_(0) {
            }
    WorkRequest(const uint64_t& req_id, const WorkType& work_type,
        const void* ptr, const size_t& size) : req_id_(req_id),
            work_type_(work_type), done_(false),
            ptr_(const_cast<void*>(ptr)), size_in_bytes_(size), completed_bytes_(0) {
            }
    ~WorkRequest() = default;

    WorkRequest(const WorkRequest& other) {
        this->req_id_ = other.req_id_;
        this->ptr_ = other.ptr_;
        this->size_in_bytes_ = other.size_in_bytes_;
        this->work_type_ = other.work_type_;
        this->completed_bytes_ = other.completed_bytes_;
        this->done_ = other.done_;
    }
    WorkRequest operator=(const WorkRequest& other) {
        this->req_id_ = other.req_id_;
        this->ptr_ = other.ptr_;
        this->size_in_bytes_ = other.size_in_bytes_;
        this->work_type_ = other.work_type_;
        this->completed_bytes_ = other.completed_bytes_;
        this->done_ = other.done_;
        return *this;
    }

    bool operator()() {
        return done_;
    }
    bool done() {
        return done_;
    }
    void set_done(const bool& done) {
        done_ = done;
    }
    Status status() {
        return status_;
    }
    void set_status(const Status& status) {
        status_ = status;
        return;
    }
    bool AddBytes(const size_t nbytes);
    size_t nbytes() const {
        return size_in_bytes_;
    }
    size_t completed_bytes() const {
        return completed_bytes_;
    }
    size_t remain_nbytes() const {
        return size_in_bytes_ - completed_bytes_;
    }
    uint64_t id() const {
        return req_id_;
    }
    void* ptr() {
      return ptr_;
    }
    template <typename T>
    T* ptr_at(const size_t& pos) {
        return reinterpret_cast<T*>(ptr_) + pos;
    }
    void Wait() {
        std::unique_lock<std::mutex> lock(done_lock_);
        done_cond_.wait(lock, [this] { return done_;});
    }
    void Notify() {
        done_lock_.lock();
        done_ = true;
        done_lock_.unlock();
        done_cond_.notify_one();
    }
private:
    uint64_t req_id_;
    WorkType work_type_;
    bool done_;
    void* ptr_;
    size_t size_in_bytes_;
    size_t completed_bytes_;
    Status status_;
    void* extra_data_;
    std::mutex done_lock_;
    std::condition_variable done_cond_;
    std::function<void()> done_callback_;
};
struct WorkRequestManager {
    std::unordered_map<uint64_t, WorkRequest> all_work_reqs;
    WorkRequestManager() {
       store_lock = utils::make_unique<utils::SpinLock>();
       id_lock = utils::make_unique<utils::SpinLock>();
       cond_lock_ = utils::make_unique<std::mutex>();
       cond_ = utils::make_unique<std::condition_variable>();
       cur_req_id = 0;
    }
    static WorkRequestManager* Get() {
        static WorkRequestManager mgr;
        return &mgr;
    }
    void AddWorkRequest(const WorkRequest& req) {
        store_lock->lock();
        all_work_reqs[req.id()] = req;
        store_lock->unlock();
    }
    uint64_t NewWorkRequest(const WorkType& work_type, void* ptr,
            const size_t& size) {
        id_lock->lock();
        cur_req_id++;
        WorkRequest work_req(cur_req_id, work_type, ptr, size);
        id_lock->unlock();
        AddWorkRequest(work_req);
        return work_req.id();
    }
    uint64_t NewWorkRequest(const WorkType& work_type, const void* ptr,
            const size_t& size) {
        id_lock->lock();
        cur_req_id++;
        WorkRequest work_req(cur_req_id, work_type, ptr, size);
        id_lock->unlock();
        AddWorkRequest(work_req);
        return work_req.id();
    }

    WorkRequest& GetWorkRequest(uint64_t req_id) {
        std::lock_guard<utils::SpinLock> lg(*store_lock);
        return all_work_reqs[req_id];
    }
    bool AddBytes(uint64_t req_id, size_t nbytes) {
        return all_work_reqs[req_id].AddBytes(nbytes);
    }
    bool Contain(uint64_t req_id) {
        return all_work_reqs.count(req_id);
    }
    void Wait(uint64_t req_id) {
        store_lock->lock();
        auto& work_req = all_work_reqs[req_id];
        store_lock->unlock();
        work_req.Wait();
    }
    void Notify() {
        cond_->notify_all();
    }
    bool done(uint64_t req_id) {
        return all_work_reqs[req_id].done();
    }
    void set_done(uint64_t req_id, bool done) {
        all_work_reqs[req_id].set_done(done);
    }

    void set_finished(uint64_t req_id) {
        cond_lock_->lock();
        all_work_reqs[req_id].set_done(true);
        cond_lock_->unlock();
        cond_->notify_all();
    }
    size_t completed_bytes(uint64_t req_id) {
        return all_work_reqs[req_id].completed_bytes();
    }
    Status status(uint64_t req_id) {
        return all_work_reqs[req_id].status();
    }
    void set_status(uint64_t req_id, const Status& status) {
        all_work_reqs[req_id].set_status(status);
    }
    uint64_t cur_req_id;
    std::unique_ptr<utils::SpinLock> store_lock;
    std::unique_ptr<utils::SpinLock> id_lock;
    // used when we do not want workrequest shutdown by themselves, deprecated
    std::unique_ptr<std::mutex> cond_lock_;
    std::unique_ptr<std::condition_variable> cond_;
};


struct WorkCompletion {
    WorkCompletion(const uint64_t& id) : id_(id), done_(false),
        completed_bytes_(0) {}
    WorkCompletion(const WorkCompletion& other) = default;

    uint64_t id_;
    bool done_;
    size_t completed_bytes_;
    Status status_;
    bool is_status_setted_;
    uint64_t id() const {
        return id_;
    }
    bool done()  {
        if (!done_) {
          done_ = WorkRequestManager::Get()->done(id_);
        }
        return done_;
    }
    bool operator()() {
        if (!done_) {
          done_ = WorkRequestManager::Get()->done(id_);
        }
        return done_;
    }
    void Wait(bool spin = false) {
        if (spin) {
            while (!done_) {
                done_ = WorkRequestManager::Get()->done(id_);
            };
        } else {
            WorkRequestManager::Get()->Wait(id_);
        }
    }
    Status status() {
        if (WorkRequestManager::Get()->Contain(id_)) {
            status_ = WorkRequestManager::Get()->status(id_);
        }
        return status_;
    }
};

class ChainWorkCompletion {
public:
    ChainWorkCompletion() = default;
    void Push(const WorkCompletion& work_comp) {
        work_comps_.emplace_back(work_comp);
    }
    void operator<<(const WorkCompletion& work_comp) {
        work_comps_.emplace_back(work_comp);
    }
    bool done() {
        bool done = false;
        for (auto& work_comp : work_comps_) {
            done |= work_comp.done();
        }
        return done;
    }
    void Wait() {
        for (auto& work_comp : work_comps_) {
            work_comp.Wait();
        }
    }
    Status status() {
        for (auto& work_comp : work_comps_) {
            if (work_comp.status() != Status::kSuccess) {
                return work_comp.status();
            }
        }
        return Status::kSuccess;
    }
private:
    std::vector<WorkCompletion> work_comps_;
};
}
