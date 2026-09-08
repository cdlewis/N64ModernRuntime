# Thread lifecycle regression

This standalone test links the real scheduler, message queues and thread lifecycle
implementation, without launching a game or renderer. It uses an in-memory RDRAM
buffer and a high-priority current thread to check queue transitions deterministically.

From the N64ModernRuntime root:

```sh
cmake -S ultramodern/tests -B build-thread-tests
cmake --build build-thread-tests
ctest --test-dir build-thread-tests --output-on-failure
```

The checks cover head/middle/tail/missing removal, 1,000 audio-style stop/start
cycles with an idle thread behind the audio thread, repeated start/stop calls,
resuming stopped message waiters by priority, and explicitly waking a waiter.
The timeout catches list traversal hangs. These tests do not replace gameplay
validation or exercise real host-thread interleavings.
