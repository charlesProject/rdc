#include "common/threadpool.h"

void ThreadPool::Run() {
    while (true) {
        std::function<void(void)> run;
        std::unique_lock<std::mutex> lock(queue_mutex_);
        wait_var_.wait(lock, [this] { return stop_ || !queue_.empty(); });
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

ThreadPool::ThreadPool(uint32_t num_workers)
    : num_workers_(num_workers), workers_(num_workers), stop_(false) {
    // create the workers_
    worker_mutex_.lock();
    for (std::thread& worker : workers_) {
        worker = std::thread([this] { this->Run(); });
    }
    worker_mutex_.unlock();
}

ThreadPool::~ThreadPool() { JoinAll(); }

ThreadPool* ThreadPool::Get() {
    static ThreadPool pool;
    return &pool;
}
void ThreadPool::AddWorkers(uint32_t num_new_workers) {
    worker_mutex_.lock();
    num_workers_ += num_new_workers;
    for (auto i = 0U; i < num_new_workers; i++) {
        workers_.emplace_back(std::thread([this] { this->Run(); }));
    }
    worker_mutex_.unlock();
}

void ThreadPool::AddTask(std::function<void(void)> job) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!stop_) {
        queue_.emplace(job);
        wait_var_.notify_one();
    }
}

void ThreadPool::JoinAll() {
    //        std::unique_lock<std::mutex> lock(queue_mutex_);
    //        wait_var_.wait(lock, [this] { return queue_.empty(); });
    queue_mutex_.lock();
    stop_ = true;
    wait_var_.notify_all();
    queue_mutex_.unlock();
    //        lock.unlock();
    worker_mutex_.lock();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    worker_mutex_.unlock();
}
