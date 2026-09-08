cmake_minimum_required(VERSION 3.20)
project(ultramodern_thread_tests LANGUAGES CXX)

find_package(Threads REQUIRED)
add_executable(thread_lifecycle
    thread_lifecycle.cpp
    ../src/threads.cpp
    ../src/threadqueue.cpp
    ../src/scheduling.cpp
    ../src/mesgqueue.cpp
)
target_compile_features(thread_lifecycle PRIVATE cxx_std_20)
target_include_directories(thread_lifecycle PRIVATE
    ../include
    ../../thirdparty
    ../../thirdparty/concurrentqueue
)
target_link_libraries(thread_lifecycle PRIVATE Threads::Threads)

enable_testing()
add_test(NAME thread_lifecycle COMMAND thread_lifecycle)
set_tests_properties(thread_lifecycle PROPERTIES TIMEOUT 10)

add_executable(thread_handoffs
    thread_handoffs.cpp
    ../src/threads.cpp
    ../src/threadqueue.cpp
    ../src/scheduling.cpp
    ../src/mesgqueue.cpp
)
target_compile_features(thread_handoffs PRIVATE cxx_std_20)
target_include_directories(thread_handoffs PRIVATE
    ../include
    ../../thirdparty
    ../../thirdparty/concurrentqueue
)
target_link_libraries(thread_handoffs PRIVATE Threads::Threads)
add_test(NAME thread_handoffs COMMAND thread_handoffs)
set_tests_properties(thread_handoffs PROPERTIES TIMEOUT 30)

# Test-only model for Mach semaphore ordering, which Apple TSan does not observe.
option(THREAD_TEST_ANNOTATE_MACH_SEMAPHORES "Model Mach semaphore ordering for Apple ThreadSanitizer" OFF)
if(THREAD_TEST_ANNOTATE_MACH_SEMAPHORES)
    if(NOT APPLE)
        message(FATAL_ERROR "Mach semaphore annotations are only for Apple ThreadSanitizer tests")
    endif()
    foreach(target thread_lifecycle thread_handoffs)
        target_compile_options(${target} PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/tsan_mach_semaphores.h)
    endforeach()
endif()
