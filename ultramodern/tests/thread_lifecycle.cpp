// Standalone regression for stop/start queue ownership. Links the real runtime
// thread, message-queue and scheduling sources; no game or renderer is launched.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include "ultramodern/ultramodern.hpp"

extern thread_local PTR(OSThread) thread_self;
std::atomic_bool exited{false};
void run_thread_function(uint8_t*, uint64_t, uint64_t, uint64_t) { std::abort(); }

static void check(bool condition) {
    if (!condition) {
        std::fputs("FAIL: thread lifecycle invariant\n", stderr);
        std::exit(1);
    }
}

int main() {
    alignas(16) uint8_t memory[4096]{};
    uint8_t* rdram = memory;
    constexpr PTR(OSThread) main_thread = (s32)0x80000100;
    constexpr PTR(OSThread) audio = (s32)0x80000200;
    constexpr PTR(OSThread) idle = (s32)0x80000300;
    constexpr PTR(OSThread) other = (s32)0x80000400;
    constexpr PTR(PTR(OSThread)) waiters = (s32)0x80000500;
    auto* main = TO_PTR(OSThread, main_thread);
    auto* a = TO_PTR(OSThread, audio);
    auto* i = TO_PTR(OSThread, idle);
    auto* o = TO_PTR(OSThread, other);
    thread_self = main_thread;
    main->priority = 100;
    main->state = OSThreadState::RUNNING;
    a->priority = 10;
    i->priority = 0;
    o->priority = 20;

    // Head, middle, tail, missing and empty removals must terminate and preserve
    // the other links, including the runtime's native runnable-queue sentinel.
    for (auto queue : {waiters, ultramodern::running_queue}) {
        ultramodern::thread_queue_insert(rdram, queue, idle);
        ultramodern::thread_queue_insert(rdram, queue, audio);
        ultramodern::thread_queue_insert(rdram, queue, other);
        check(ultramodern::thread_queue_remove(rdram, queue, audio));
        check(o->next == idle);
        check(!ultramodern::thread_queue_remove(rdram, queue, main_thread));
        check(ultramodern::thread_queue_remove(rdram, queue, idle));
        check(o->next == 0);
        check(ultramodern::thread_queue_remove(rdram, queue, other));
        check(!ultramodern::thread_queue_remove(rdram, queue, audio));
    }

    // Reproduce the game's repeated audio stop/start, with an idle thread behind
    // it. The old implementation left audio on both queues and formed a cycle.
    ultramodern::schedule_running_thread(rdram, idle);
    for (int n = 0; n < 1000; ++n) {
        ultramodern::thread_queue_insert(rdram, waiters, audio);
        a->state = OSThreadState::BLOCKED;
        osStopThread(rdram, audio);
        check(a->state == OSThreadState::STOPPED && a->queue == waiters);
        check(ultramodern::thread_queue_empty(rdram, waiters));
        osStopThread(rdram, audio); // Already stopped: preserve restart queue.
        osStartThread(rdram, audio);
        osStartThread(rdram, audio); // Already runnable: never insert twice.
        check(ultramodern::thread_queue_empty(rdram, waiters));
        check(ultramodern::thread_queue_pop(rdram, ultramodern::running_queue) == audio);
        check(ultramodern::thread_queue_peek(rdram, ultramodern::running_queue) == idle);
        check(i->next == 0);
    }

    // Restarting a stopped waiter wakes the highest-priority original waiter,
    // not necessarily the thread passed to osStartThread (libultra semantics).
    ultramodern::thread_queue_insert(rdram, waiters, other);
    o->state = OSThreadState::BLOCKED;
    ultramodern::thread_queue_insert(rdram, waiters, audio);
    a->state = OSThreadState::BLOCKED;
    osStopThread(rdram, audio);
    osStartThread(rdram, audio);
    check(ultramodern::thread_queue_pop(rdram, ultramodern::running_queue) == other);
    check(ultramodern::thread_queue_peek(rdram, waiters) == audio);
    check(a->state == OSThreadState::BLOCKED && a->next == 0);

    // Explicit wake of an unstopped waiter also transfers ownership once.
    osStartThread(rdram, audio);
    check(ultramodern::thread_queue_empty(rdram, waiters));
    osStopThread(rdram, audio);
    check(ultramodern::thread_queue_peek(rdram, ultramodern::running_queue) == idle);
    osStartThread(rdram, audio);
    check(ultramodern::thread_queue_pop(rdram, ultramodern::running_queue) == audio);
    check(ultramodern::thread_queue_pop(rdram, ultramodern::running_queue) == idle);
    check(ultramodern::thread_queue_empty(rdram, ultramodern::running_queue));
    std::puts("PASS: queue removal, 1000 audio stop/start cycles, repeated start/stop, waiter priority and explicit wake");
}
