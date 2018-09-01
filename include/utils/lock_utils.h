#pragma once
#include <atomic>
#include <mutex>
namespace rdc {
namespace utils {
class SpinLock {
public:
    SpinLock() : lock_(ATOMIC_FLAG_INIT) {}
    ~SpinLock() = default;
    /*!
     * \brief Acquire lock.
     */
    void lock() noexcept(true);
    /*!
     * \brief Release lock.
     */
    void unlock() noexcept(true);
private:
    std::atomic_flag lock_;
    SpinLock(const SpinLock&) = delete;
};
class MixedLock {
public:
    MixedLock();
    MixedLock(const uint64_t& spin_timeout);
    ~MixedLock() = default;
    inline void lock() noexcept(true);
    inline void unlock() noexcept(true);
private:
    std::atomic_flag spin_;
    std::mutex mu_;
    uint64_t spin_timeout_;
    uint64_t spin_timer_;
    MixedLock(const MixedLock&) = delete;
};
template <class Lock>
class LockGuard {
public:
    LockGuard(Lock& lock) : lock_(lock) {
        lock_.lock();
    }
    ~LockGuard() {
        lock_.unlock();
    }
    Lock& lock_;
};
} // namespace utils
} // namespace rdc

