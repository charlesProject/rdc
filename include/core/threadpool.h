#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>

#include "core/env.h"

class ThreadPool {
private:
    // number of thread
    uint32_t num_workers_;
    std::vector<std::thread> workers_;
    // where tasks are storage
    std::queue<std::function<void(void)> > queue_;

    std::atomic_bool stop_;
    std::condition_variable wait_var_;
    std::mutex queue_mutex_;
    std::mutex worker_mutex_;
    // Body of every running thread.
    // The thread will run until deconstructor or `JoinAll` are called
    // and it fetch the top of the queue_ to find a task to exec.
    void Run() {
        while (true) {
            std::function<void(void)> run;
            std::unique_lock<std::mutex> lock(queue_mutex_);
            wait_var_.wait(lock, [this] {return stop_ || !queue_.empty();});
            if (stop_) {
                return;
            }
            run = queue_.front();
            queue_.pop();
            lock.unlock();
            // unlock befor `run` to ensure parallelism
            run();
        }
    }

public:
    ThreadPool() : ThreadPool(2 * Env::Get()->GetIntEnv("RDC_NUM_WORKERS")) {}
    // Constructor
    ThreadPool(uint32_t num_workers)
        : num_workers_(num_workers)
        , workers_(num_workers)
        , stop_(false) {
        // create the workers_
        worker_mutex_.lock();
        for (std::thread& worker : workers_) {
            worker = std::thread([this] { this->Run(); });
        }
        worker_mutex_.unlock();
    }

    // Deconstructor
    ~ThreadPool() {
        JoinAll();
    }
    static ThreadPool* Get() {
        static ThreadPool pool;
        return &pool;
    }
    void AddWorkers(uint32_t num_new_workers) {
        worker_mutex_.lock();
        num_workers_ += num_new_workers;
        for (auto i = 0U; i < num_new_workers; i++) {
            workers_.emplace_back(std::thread([this] { this->Run(); }));
        }
        worker_mutex_.unlock();
    }
    // Add a task to queue
    // The function will add, at the end of the queue, a `void`
    // function only if no one is waiting for stop.
    void AddTask(std::function<void(void)> job) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!stop_) {
            queue_.emplace(job);
            wait_var_.notify_one();
        }
    }

    // Wait until all tasks ended.
    // If the queue_ is not empty wait the end of all tasks inserted
    // and terminate the workers_.
    void JoinAll() {
//        std::unique_lock<std::mutex> lock(queue_mutex_);
//        wait_var_.wait(lock, [this] { return queue_.empty(); });
        queue_mutex_.lock();
        stop_ = true;
        wait_var_.notify_all();
        queue_mutex_.unlock();
//        lock.unlock();
        worker_mutex_.lock();
        for (std::thread& worker : workers_) {
            if (worker.joinable())
                worker.join();
        }
        worker_mutex_.unlock();
    }
};

