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
