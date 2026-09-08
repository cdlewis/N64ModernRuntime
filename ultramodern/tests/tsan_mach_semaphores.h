#pragma once

#if !defined(__APPLE__) || !__has_feature(thread_sanitizer)
#error "This test-only header requires Apple ThreadSanitizer"
#endif

#include <cstdint>
#include <mach/mach.h>
#include <sanitizer/tsan_interface.h>

// Model Mach semaphore ordering without suppressing instrumentation or changing operations.
inline void* test_semaphore_identity(semaphore_t semaphore) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(semaphore));
}

inline kern_return_t test_semaphore_signal(semaphore_t semaphore) {
    __tsan_release(test_semaphore_identity(semaphore));
    return semaphore_signal(semaphore);
}

inline kern_return_t test_semaphore_wait(semaphore_t semaphore) {
    kern_return_t result = semaphore_wait(semaphore);
    if (result == KERN_SUCCESS) {
        __tsan_acquire(test_semaphore_identity(semaphore));
    }
    return result;
}

inline kern_return_t test_semaphore_timedwait(semaphore_t semaphore, mach_timespec_t timeout) {
    kern_return_t result = semaphore_timedwait(semaphore, timeout);
    if (result == KERN_SUCCESS) {
        __tsan_acquire(test_semaphore_identity(semaphore));
    }
    return result;
}

#define semaphore_signal test_semaphore_signal
#define semaphore_wait test_semaphore_wait
#define semaphore_timedwait test_semaphore_timedwait
