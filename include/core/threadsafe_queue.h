#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include "logging.h"
/**
 * \brief thread-safe queue allowing push and waited pop
 */
template<typename T> class ThreadsafeQueue {
public:
    ThreadsafeQueue() { }
    ~ThreadsafeQueue() { }

    /**
     * \brief push an value into the end. threadsafe.
     * \param new_value the value
     */
    void Push(T new_value) {
        mu_.lock();
        queue_.push(std::move(new_value));
        mu_.unlock();
        cond_.notify_all();
    }
    /**
     * \brief wait until there is at least one element, threadsafe
     * use it carefully
     */
    T Peek() { 
        std::unique_lock<std::mutex> lk(mu_);
        cond_.wait(lk, [this]{return !queue_.empty();});
        return queue_.front();
    }
    bool TryPeek(T& value) {
        std::lock_guard<std::mutex> lk(mu_);
        if (queue_.empty()) {
            return false;
        }
        value = queue_.front();
        return true;
    }

    void Pop() {
        mu_.lock();
        queue_.pop();
        mu_.unlock(); 
    }
    /**
     * \brief wait until pop an element from the beginning, threadsafe
     * \param value the poped value
     */
    void WaitAndPop(T* value) {
        std::unique_lock<std::mutex> lk(mu_);
        cond_.wait(lk, [this]{return !queue_.empty();});
        *value = std::move(queue_.front());
        queue_.pop();
    }
    int32_t size() {
      std::lock_guard<std::mutex> lk(mu_);
      return queue_.size();
    }

private:
    mutable std::mutex mu_;
    std::queue<T> queue_;
    std::condition_variable cond_;
};


// bool TryPop(T& value) {
//   std::lock_guard<std::mutex> lk(mut);
//   if(data_queue.empty())
//     return false;
//   value=std::move(data_queue.front());
//   data_queue.pop();
//   return true;
// }
