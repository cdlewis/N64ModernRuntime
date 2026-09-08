// Real host workers with a low-priority driver; no game binary is launched.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include "ultramodern/ultramodern.hpp"

extern thread_local PTR(OSThread) thread_self;
std::atomic_bool exited{false};

constexpr PTR(OSThread) driver = (s32)0x80000100;
constexpr PTR(OSThread) worker = (s32)0x80000200;
constexpr PTR(OSThread) other = (s32)0x80000300;
constexpr PTR(OSMesgQueue) queue = (s32)0x80000600;
constexpr PTR(OSMesg) messages = (s32)0x80000700;
constexpr PTR(OSMesgQueue) pump = (s32)0x80000800;
constexpr PTR(OSMesg) pump_messages = (s32)0x80000900;
constexpr PTR(OSMesg) received = (s32)0x80000A00;
std::atomic_int progress{0};

static void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        std::exit(1);
    }
}

void run_thread_function(uint8_t* rdram, uint64_t entry, uint64_t, uint64_t arg) {
    check(TO_PTR(OSThread, thread_self)->state == OSThreadState::RUNNING, "entry marked running");
    switch (entry) {
        case 1: // Repeated receives, each waking at a higher priority than driver.
            for (uint64_t n = 0; n < arg; ++n) {
                check(osRecvMesg(rdram, queue, received, OS_MESG_BLOCK) == 0, "worker receive");
                check(*TO_PTR(OSMesg, received) == (OSMesg)(n + 1), "message retained while stopped");
                ++progress;
            }
            break;
        case 2:
            ++progress;
            osStopThread(rdram, NULLPTR);
            check(TO_PTR(OSThread, thread_self)->state == OSThreadState::RUNNING, "self-stop resume");
            ++progress;
            osStopThread(rdram, thread_self);
            ++progress;
            break;
        case 3:
            check(osSendMesg(rdram, queue, 99, OS_MESG_BLOCK) == 0, "blocked send resumes");
            ++progress;
            break;
        case 4:
            osRecvMesg(rdram, queue, received, OS_MESG_BLOCK);
            check(false, "destroyed receiver must not continue");
            break;
        default:
            check(false, "unknown worker entry");
    }
}

static UltraThreadContext* create(uint8_t* rdram, PTR(OSThread) address, uint64_t entry, uint64_t arg = 0) {
    osCreateThread(rdram, address, 3, (PTR(thread_func_t))entry, (PTR(void))arg, (PTR(void))0x80000F00, 20);
    return TO_PTR(OSThread, address)->context;
}

static void join_worker(UltraThreadContext* context) {
    // Join directly; the asynchronous cleanup queue is not consumed in this test.
    context->host_thread.join();
    delete context;
}

template <class F> static void expect_rejection(F operation, const char* description) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    check(rejected, description);
}

int main() {
    alignas(16) uint8_t memory[4096]{};
    uint8_t* rdram = memory;
    UltraThreadContext driver_context;
    ultramodern::set_main_thread();
    thread_self = driver;
    auto* d = TO_PTR(OSThread, driver);
    auto* w = TO_PTR(OSThread, worker);
    d->priority = 10;
    d->state = OSThreadState::RUNNING;
    d->context = &driver_context;
    osCreateMesgQueue(rdram, queue, messages, 1);
    osCreateMesgQueue(rdram, pump, pump_messages, 1);

    // Invalid operations must leave queues unchanged, including in release builds.
    thread_self = NULLPTR;
    expect_rejection([&] { osStopThread(rdram, worker); }, "external stop rejected");
    thread_self = driver;
    w->state = OSThreadState::RUNNING;
    expect_rejection([&] { osStopThread(rdram, worker); }, "running non-self target rejected");
    check(w->state == OSThreadState::RUNNING, "rejection preserves state");
    w->state = OSThreadState::BLOCKED;
    w->queue = GET_MEMBER(OSMesgQueue, queue, blocked_on_recv);
    expect_rejection([&] { osStopThread(rdram, worker); }, "missing waiter stop rejected");
    expect_rejection([&] { osStartThread(rdram, worker); }, "missing waiter start rejected");
    check(w->state == OSThreadState::BLOCKED, "missing waiter not marked stopped/runnable");

    progress = 0;
    auto* context = create(rdram, worker, 1, 1000);
    osStartThread(rdram, worker);
    for (int n = 0; n < 1000; ++n) {
        check(w->state == OSThreadState::BLOCKED, "receiver blocked");
        osStartThread(rdram, worker); // No message: must wake then block again, not duplicate itself.
        check(w->state == OSThreadState::BLOCKED && w->next == NULLPTR, "explicit wake reblocks once");
        osStopThread(rdram, worker);
        osStopThread(rdram, worker);
        check(w->state == OSThreadState::STOPPED, "receiver stopped");
        // Use the same ingress as device completion events.
        std::thread producer([n] { ultramodern::enqueue_external_message(queue, n + 1, false, false); });
        producer.join();
        check(osRecvMesg(rdram, pump, NULLPTR, OS_MESG_NOBLOCK) == -1, "pump external message");
        check(progress == n && w->state == OSThreadState::STOPPED, "message cannot run stopped receiver");
        check(TO_PTR(OSMesgQueue, queue)->validCount == 1, "queued completion retained");
        osStartThread(rdram, worker);
        check(progress == n + 1, "restart immediately hands off to receiver");
        check(d->state == OSThreadState::RUNNING, "driver resumes after worker yields");
    }
    join_worker(context);
    check(w->context == nullptr, "returned worker context cleared");

    progress = 0;
    context = create(rdram, worker, 2);
    osStartThread(rdram, worker);
    check(progress == 1 && w->state == OSThreadState::STOPPED && w->queue == NULLPTR, "null self-stop");
    osSetThreadPri(rdram, worker, 30);
    osStartThread(rdram, worker);
    check(progress == 2 && w->state == OSThreadState::STOPPED, "explicit self-stop");
    osStartThread(rdram, worker);
    check(progress == 3, "second self restart");
    join_worker(context);

    progress = 0;
    osSendMesg(rdram, queue, 7, OS_MESG_NOBLOCK);
    context = create(rdram, worker, 3);
    osStartThread(rdram, worker);
    check(w->state == OSThreadState::BLOCKED, "sender blocks on full queue");
    osStartThread(rdram, worker);
    check(w->state == OSThreadState::BLOCKED && w->next == NULLPTR, "explicit sender wake reblocks once");
    osStopThread(rdram, worker);
    osRecvMesg(rdram, queue, received, OS_MESG_NOBLOCK);
    check(progress == 0 && w->state == OSThreadState::STOPPED, "space does not wake stopped sender");
    osStartThread(rdram, worker);
    check(progress == 1, "sender restarted");
    join_worker(context);
    osRecvMesg(rdram, queue, received, OS_MESG_NOBLOCK);
    check(*TO_PTR(OSMesg, received) == 99, "sender message retained");

    // Destroy blocked, stopped and never-started workers; join before reusing storage.
    for (int mode = 0; mode < 3; ++mode) {
        context = create(rdram, worker, 4);
        if (mode != 2) osStartThread(rdram, worker);
        if (mode == 1) osStopThread(rdram, worker);
        osDestroyThread(rdram, worker);
        join_worker(context);
        check(w->context == nullptr, "destroyed context cleared");
        check(TO_PTR(OSMesgQueue, queue)->blocked_on_recv == NULLPTR, "destroy removes waiter");
    }

    // Stop a runnable target, change its priority, then restart it.
    progress = 0;
    context = create(rdram, other, 2);
    osSetThreadPri(rdram, other, 5);
    osStartThread(rdram, other);
    check(TO_PTR(OSThread, other)->state == OSThreadState::QUEUED, "low-priority worker runnable");
    osStopThread(rdram, other);
    osSetThreadPri(rdram, other, 30);
    osStartThread(rdram, other);
    check(progress == 1, "stopped runnable target resumes");
    osStartThread(rdram, other);
    osStartThread(rdram, other);
    join_worker(context);
    check(ultramodern::thread_queue_empty(rdram, ultramodern::running_queue), "all workers drained");
    std::puts("PASS: 1000 real handoffs with stopped-receiver messages, self-stop, blocked send, explicit wake, "
              "priority change, destruction/recreation and release guards");
}
