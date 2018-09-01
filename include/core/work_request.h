#pragma once
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstring>
#include "utils/utils.h"
#include "utils/lock_utils.h"
#include "core/status.h"
#include "core/logging.h"
namespace rabit {
enum WorkType : uint32_t {
    kSend,
    kRecv,
};
struct WorkRequest {
    WorkRequest(): done_(false), completed_bytes_(0) {};
    WorkRequest(const uint64_t& req_id, const WorkType& work_type,
        void* ptr, const size_t& size) : req_id_(req_id), 
            work_type_(work_type), done_(false),
            ptr_(ptr), size_(size), completed_bytes_(0) {}
    WorkRequest(const uint64_t& req_id, const WorkType& work_type, 
        const void* ptr, const size_t& size) : req_id_(req_id), 
            work_type_(work_type), done_(false),
            ptr_(const_cast<void*>(ptr)), size_(size), completed_bytes_(0) {}
    WorkRequest(const WorkRequest& other) = default;
//    WorkRequest(const WorkRequest& other) {
//        this->req_id_ = other.req_id_;
//        this->ptr_ = other.ptr_;
//        this->size_ = other.size_;
//        this->work_type_ = other.work_type_;
//        this->completed_bytes_ = other.completed_bytes_;
//        //this->completed_bytes_.store(other.completed_bytes_.load());
//    }
//    WorkRequest operator=(const WorkRequest& other) {
//        this->req_id_ = other.req_id_;
//        this->ptr_ = other.ptr_;
//        this->size_ = other.size_;
//        this->work_type_ = other.work_type_;
//        this->completed_bytes_ = other.completed_bytes_;
//        //this->completed_bytes_.store(other.completed_bytes_.load());
//        return *this;
//    }
    bool operator()() {
        return done_;
//        return done_.load(std::memory_order_acquire);
    }
    bool done() {
        return done_;
//        return done_.load(std::memory_order_acquire);
    }
    void set_done(const bool& done) {
        done_ = done;
//        done_.store(done, std::memory_order_release);
    }
    Status status() {
        return status_;
        //return status_.load(std::memory_order_acquire);
    }
    void set_status(const Status& status) {
        //status_.store(status, std::memory_order_release);
        status_ = status;
        return;
    }
    bool AddBytes(const size_t nbytes);
    size_t nbytes() const {
        return size_;
    }
    size_t completed_bytes() const {
        return completed_bytes_;
        //return completed_bytes_.load(std::memory_order_acquire);
    }
    size_t remain_nbytes() const {
        return size_ - completed_bytes_;
        //return size_ - completed_bytes_.load(std::memory_order_acquire);
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
private:
    uint64_t req_id_;
    WorkType work_type_;
    bool done_;
//    std::atomic<bool> done_;
    void* ptr_;
    size_t size_;
    //std::atomic<size_t> completed_bytes_;
    size_t completed_bytes_;
    Status status_;
//    std::atomic<Status> status_;
    void* extra_data_;
};
struct WorkRequestManager {
    std::unordered_map<uint64_t, WorkRequest> all_work_reqs;
    WorkRequestManager() {
       store_lock = utils::make_unique<utils::SpinLock>();
       id_lock = utils::make_unique<utils::SpinLock>();
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
//        LOG_S(INFO) << cur_req_id;
        id_lock->unlock();
        AddWorkRequest(work_req);
        return work_req.id();
    }
    uint64_t NewWorkRequest(const WorkType& work_type, const void* ptr,
            const size_t& size) {
        id_lock->lock();
        cur_req_id++;
//        LOG_S(INFO) << cur_req_id;
        WorkRequest work_req(cur_req_id, work_type, ptr, size);
        id_lock->unlock();
        AddWorkRequest(work_req);
        return work_req.id();
    }

    WorkRequest& GetWorkRequest(uint64_t req_id) {
        utils::LockGuard<utils::SpinLock> lg(*store_lock);
        return all_work_reqs[req_id];
    }
    bool AddBytes(uint64_t req_id, size_t nbytes) {
//        utils::LockGuard<utils::SpinLock> lg(*store_lock);
//        LOG_S(INFO) << req_id;
        return all_work_reqs[req_id].AddBytes(nbytes);
    }
    bool Contain(uint64_t req_id) {
//        utils::LockGuard<utils::SpinLock> lg(*store_lock);
        return all_work_reqs.count(req_id);
    }
    bool done(uint64_t req_id) {
//        utils::LockGuard<utils::SpinLock> lg(*store_lock);
        return all_work_reqs[req_id].done();
    }
    void set_done(uint64_t req_id, bool done) {
//        utils::LockGuard<utils::SpinLock> lg(*store_lock);
        all_work_reqs[req_id].set_done(done);
    }
    size_t completed_bytes(uint64_t req_id) {
//        utils::LockGuard<utils::SpinLock> lg(*store_lock);
        return all_work_reqs[req_id].completed_bytes();
    }
    Status status(uint64_t req_id) {
//        utils::LockGuard<utils::SpinLock> lg(*store_lock);
        return all_work_reqs[req_id].status();
    }
    void set_status(uint64_t req_id, const Status& status) {
//        utils::LockGuard<utils::SpinLock> lg(*store_lock);
        all_work_reqs[req_id].set_status(status);
    }
    uint64_t cur_req_id;
    std::unique_ptr<utils::SpinLock> store_lock;
    std::unique_ptr<utils::SpinLock> id_lock;
};


struct WorkCompletion {
    WorkCompletion(const uint64_t& id) : id_(id), done_(false),
        completed_bytes_(0) {}
    WorkCompletion(const WorkCompletion& other) = default;

    uint64_t id_;
    bool done_;
//    std::atomic<bool> done_;
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
    size_t completed_bytes() {
        if (WorkRequestManager::Get()->Contain(id_)) {
    //    if (!done_) {
            completed_bytes_ = WorkRequestManager::Get()
                                    ->completed_bytes(id_);
        }
        return completed_bytes_;
    }
    bool operator()() {
        if (!done_) {
          done_ = WorkRequestManager::Get()->done(id_);
        }
        return done_;
    }
    void Wait() {
        while (!done_) {
            done_ = WorkRequestManager::Get()->done(id_);
        };
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
//        work_comps_.back().Wait();
//        if(this->done()) {
//            LOG_F(ERROR, "Not all requests are done, please ensure"\
//                "all work completions are associated with the same"\
//                " communication channel and were added by their actual"\
//                " execution order");
//        }
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
