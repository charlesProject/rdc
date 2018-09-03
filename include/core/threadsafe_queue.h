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
    bool TryPeek(T& value) {
        mu_.lock();
        if (queue_.empty()) {
            mu_.unlock();
            return false;
        }
        else {
            value = queue_.front();
            mu_.unlock();
            return true;
        }
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

private:
    mutable std::mutex mu_;
    std::queue<T> queue_;
    std::condition_variable cond_;
};

