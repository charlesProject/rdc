#include "common/semaphore.h"
#if defined(_WIN32)
Semaphore::Semaphore(int initialCount) {
    assert(initialCount >= 0);
    const long maxLong = 0x7fffffff;
    m_hSema = CreateSemaphoreW(nullptr, initialCount, maxLong, nullptr);
}

Semaphore::~Semaphore() {
    CloseHandle(m_hSema);
}

void Semaphore::Wait() {
    const unsigned long infinite = 0xffffffff;
    WaitForSingleObject(m_hSema, infinite);
}

bool Semaphore::TryWait() {
    const unsigned long RC_WAIT_TIMEOUT = 0x00000102;
    return WaitForSingleObject(m_hSema, 0) != RC_WAIT_TIMEOUT;
}

bool Semaphore::TimedWait(std::uint64_t usecs) {
    const unsigned long RC_WAIT_TIMEOUT = 0x00000102;
    return WaitForSingleObject(m_hSema, (unsigned long)(usecs / 1000)) !=
           RC_WAIT_TIMEOUT;
}

void Semaphore::Signal(int count) {
    ReleaseSemaphore(m_hSema, count, nullptr);
}
#elif defined(__MACH__)
//---------------------------------------------------------
// Semaphore (Apple iOS and OSX)
// Can't use POSIX semaphores due to
// http://lists.apple.com/archives/darwin-kernel/2009/Apr/msg00010.html
//---------------------------------------------------------
Semaphore::Semaphore(int initialCount) {
    assert(initialCount >= 0);
    semaphore_create(mach_task_self(), &m_sema, SYNC_POLICY_FIFO, initialCount);
}

Semaphore::~Semaphore() {
    semaphore_destroy(mach_task_self(), m_sema);
}

void Semaphore::Wait() {
    semaphore_wait(m_sema);
}

bool Semaphore::TryWait() {
    return TimedWait(0);
}

bool Semaphore::TimedWait(std::uint64_t timeout_usecs) {
    mach_timespec_t ts;
    ts.tv_sec = static_cast<unsigned int>(timeout_usecs / 1000000);
    ts.tv_nsec = (timeout_usecs % 1000000) * 1000;

    kern_return_t rc = semaphore_timedwait(m_sema, ts);

    return rc != KERN_OPERATION_TIMED_OUT && rc != KERN_ABORTED;
}

void Semaphore::Signal() {
    semaphore_signal(m_sema);
}

void Semaphore::Signal(int count) {
    while (count-- > 0) {
        semaphore_signal(m_sema);
    }
}
}
;
#elif defined(__unix__)
//---------------------------------------------------------
// Semaphore (POSIX, Linux)
//---------------------------------------------------------
Semaphore::Semaphore(int initialCount) {
    assert(initialCount >= 0);
    sem_init(&m_sema, 0, initialCount);
}

Semaphore::~Semaphore() {
    sem_destroy(&m_sema);
}

void Semaphore::Wait() {
    int rc;
    do {
        rc = sem_wait(&m_sema);
    } while (rc == -1 && errno == EINTR);
}

bool Semaphore::TryWait() {
    int rc;
    do {
        rc = sem_trywait(&m_sema);
    } while (rc == -1 && errno == EINTR);
    return !(rc == -1 && errno == EAGAIN);
}

bool Semaphore::TimedWait(std::uint64_t usecs) {
    struct timespec ts;
    const int usecs_in_1_sec = 1000000;
    const int nsecs_in_1_sec = 1000000000;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += usecs / usecs_in_1_sec;
    ts.tv_nsec += (usecs % usecs_in_1_sec) * 1000;
    // sem_timedwait bombs if you have more than 1e9 in tv_nsec
    // so we have to clean things up before passing it in
    if (ts.tv_nsec >= nsecs_in_1_sec) {
        ts.tv_nsec -= nsecs_in_1_sec;
        ++ts.tv_sec;
    }

    int rc;
    do {
        rc = sem_timedwait(&m_sema, &ts);
    } while (rc == -1 && errno == EINTR);
    return !(rc == -1 && errno == ETIMEDOUT);
}

void Semaphore::Signal() {
    sem_post(&m_sema);
}

void Semaphore::Signal(int count) {
    while (count-- > 0) {
        sem_post(&m_sema);
    }
}
#else
#error Unsupported platform! (No semaphore wrapper available)
#endif

bool LightweightSemaphore::WaitWithPartialSpinning(
    std::int64_t timeout_usecs) {
    ssize_t oldCount;
    // Is there a better way to set the initial spin count?
    // If we lower it to 1000, testBenaphore becomes 15x slower on my Core
    // i7-5930K Windows PC, as threads start hitting the kernel semaphore.
    int spin = 10000;
    while (--spin >= 0) {
        oldCount = m_count.load(std::memory_order_relaxed);
        if ((oldCount > 0) &&
            m_count.compare_exchange_strong(oldCount, oldCount - 1,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed))
            return true;
        std::atomic_signal_fence(
            std::memory_order_acquire);  // Prevent the compiler from
                                         // collapsing the loop.
    }
    oldCount = m_count.fetch_sub(1, std::memory_order_acquire);
    if (oldCount > 0)
        return true;
    if (timeout_usecs < 0) {
        m_sema.Wait();
        return true;
    }
    if (m_sema.TimedWait((std::uint64_t)timeout_usecs))
        return true;
    // At this point, we've timed out waiting for the semaphore, but the
    // count is still decremented indicating we may still be waiting on
    // it. So we have to re-adjust the count, but only if the semaphore
    // wasn't signaled enough times for us too since then. If it was, we
    // need to release the semaphore too.
    while (true) {
        oldCount = m_count.load(std::memory_order_acquire);
        if (oldCount >= 0 && m_sema.TryWait())
            return true;
        if (oldCount < 0 &&
            m_count.compare_exchange_strong(oldCount, oldCount + 1,
                                            std::memory_order_relaxed,
                                            std::memory_order_relaxed))
            return false;
    }
}

ssize_t LightweightSemaphore::WaitManyWithPartialSpinning(
    ssize_t max, std::int64_t timeout_usecs) {
    assert(max > 0);
    ssize_t oldCount;
    int spin = 10000;
    while (--spin >= 0) {
        oldCount = m_count.load(std::memory_order_relaxed);
        if (oldCount > 0) {
            ssize_t newCount = oldCount > max ? oldCount - max : 0;
            if (m_count.compare_exchange_strong(oldCount, newCount,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed))
                return oldCount - newCount;
        }
        std::atomic_signal_fence(std::memory_order_acquire);
    }
    oldCount = m_count.fetch_sub(1, std::memory_order_acquire);
    if (oldCount <= 0) {
        if (timeout_usecs < 0)
            m_sema.Wait();
        else if (!m_sema.TimedWait((std::uint64_t)timeout_usecs)) {
            while (true) {
                oldCount = m_count.load(std::memory_order_acquire);
                if (oldCount >= 0 && m_sema.TryWait())
                    break;
                if (oldCount < 0 &&
                    m_count.compare_exchange_strong(oldCount, oldCount + 1,
                                                    std::memory_order_relaxed,
                                                    std::memory_order_relaxed))
                    return 0;
            }
        }
    }
    if (max > 1)
        return 1 + TryWaitMany(max - 1);
    return 1;
}

LightweightSemaphore::LightweightSemaphore(ssize_t initialCount)
    : m_count(initialCount) {
    assert(initialCount >= 0);
}

bool LightweightSemaphore::TryWait() {
    ssize_t oldCount = m_count.load(std::memory_order_relaxed);
    while (oldCount > 0) {
        if (m_count.compare_exchange_weak(oldCount, oldCount - 1,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed))
            return true;
    }
    return false;
}

void LightweightSemaphore::Wait() {
    if (!TryWait())
        WaitWithPartialSpinning();
}

bool LightweightSemaphore::Wait(std::int64_t timeout_usecs) {
    return TryWait() || WaitWithPartialSpinning(timeout_usecs);
}

// Acquires between 0 and (greedily) max, inclusive
ssize_t LightweightSemaphore::TryWaitMany(ssize_t max) {
    assert(max >= 0);
    ssize_t oldCount = m_count.load(std::memory_order_relaxed);
    while (oldCount > 0) {
        ssize_t newCount = oldCount > max ? oldCount - max : 0;
        if (m_count.compare_exchange_weak(oldCount, newCount,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed))
            return oldCount - newCount;
    }
    return 0;
}

// Acquires at least one, and (greedily) at most max
ssize_t LightweightSemaphore::WaitMany(ssize_t max,
                                       std::int64_t timeout_usecs) {
    assert(max >= 0);
    ssize_t result = TryWaitMany(max);
    if (result == 0 && max > 0)
        result = WaitManyWithPartialSpinning(max, timeout_usecs);
    return result;
}

ssize_t LightweightSemaphore::WaitMany(ssize_t max) {
    ssize_t result = WaitMany(max, -1);
    assert(result > 0);
    return result;
}

void LightweightSemaphore::Signal(ssize_t count) {
    assert(count >= 0);
    ssize_t oldCount = m_count.fetch_add(count, std::memory_order_release);
    ssize_t toRelease = -oldCount < count ? -oldCount : count;
    if (toRelease > 0) {
        m_sema.Signal((int)toRelease);
    }
}

ssize_t LightweightSemaphore::AvailableApprox() const {
    ssize_t count = m_count.load(std::memory_order_relaxed);
    return count > 0 ? count : 0;
}
