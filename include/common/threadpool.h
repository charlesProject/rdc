#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "common/env.h"

class ThreadPool {
private:
    // number of thread
    uint32_t num_workers_;
    std::vector<std::thread> workers_;
    // where tasks are storage
    std::queue<std::function<void(void)>> queue_;

    std::atomic_bool stop_;
    std::condition_variable wait_var_;
    std::mutex queue_mutex_;
    std::mutex worker_mutex_;
    // Body of every running thread.
    // The thread will run until deconstructor or `JoinAll` are called
    // and it fetch the top of the queue_ to find a task to exec.
    void Run();

public:
    ThreadPool() : ThreadPool(Env::Get()->GetIntEnv("RDC_NUM_WORKERS") + 4) {}
    // Constructor
    ThreadPool(uint32_t num_workers);

    // Deconstructor
    ~ThreadPool();
    static ThreadPool* Get();
    /**
     * @brief: Add new workers to worker queue
     *
     * @param num_new_workers number of new workers
     */
    void AddWorkers(uint32_t num_new_workers);
    /**
     * @brief: Add a task to queue
     * The function will add, at the end of the queue, a 'void'
     * function only if no one is waiting for stop
     * @param job job function
     */
    void AddTask(std::function<void(void)> job);

    /**
     * @brief: Wait until all tasks ended.
     * If the queue_ is not empty wait the end of all tasks inserted
     * and terminate the workers_.
     */
    void JoinAll();
};
