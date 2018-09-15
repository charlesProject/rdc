#include "utils/lock_utils.h"
#include "utils/timer.h"
namespace rdc {
namespace utils {
static const uint64_t kTimeout = 1000; 
void SpinLock::lock() noexcept(true) {
    while (lock_.test_and_set(std::memory_order_acquire)) {
    }
}

void SpinLock::unlock() noexcept(true) {
    lock_.clear(std::memory_order_release);
}


MixedLock::MixedLock() : spin_(ATOMIC_FLAG_INIT) {
    spin_timeout_ = kTimeout;
}


MixedLock::MixedLock(const uint64_t& spin_timeout) : spin_(ATOMIC_FLAG_INIT) {
    spin_timeout_ = spin_timeout;
}

void MixedLock::lock() noexcept(true) {
    uint64_t start = GetTimeInUs();
    while (spin_.test_and_set(std::memory_order_acquire)) {
        uint64_t end = GetTimeInUs();
        if (end - start > spin_timeout_) { 
            spin_.clear(std::memory_order_release);
        }
    }
}

void MixedLock::unlock() noexcept(true) {
    if (!spin_.test_and_set(std::memory_order_acquire)) {
    }
}
} // namespace utils
} // namespace rdc
