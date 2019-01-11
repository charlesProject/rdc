#include "common/threadpool.h"

void ThreadPool::Run() {
    while (true) {
        std::function<void(void)> run;
        std::unique_lock<std::mutex> lock(queue_mutex_);
        wait_var_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (stop_) {
            break;
        }
        run = queue_.front();
        queue_.pop();
        lock.unlock();
        // unlock befor `run` to ensure parallelism
        run();
    }
    while (true) {
        queue_mutex_.lock();
        auto run = queue_.front();
        queue_.pop();
        queue_mutex_.unlock();
        run();

        if (queue_.empty()) {
            stop_var_.notify_one();
            return;
        }
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

ThreadPool::~ThreadPool() {
    JoinAll();
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
    std::unique_lock<std::mutex> lock(queue_mutex_);
    stop_ = true;
    wait_var_.notify_all();
    queue_mutex_.unlock();

    std::unique_lock<std::mutex> stop_lock(stop_mutex_);
    stop_var_.wait(stop_lock, [this] { return queue_.empty(); });
    worker_mutex_.lock();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    worker_mutex_.unlock();
}

int ThreadPool::num_jobs() {
    std::lock_guard<std::mutex> guard(queue_mutex_);
    return queue_.size();
}
