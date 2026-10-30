cmake_minimum_required(VERSION 3.20)
project(latency_probe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(latency_probe INTERFACE)
target_include_directories(latency_probe INTERFACE include)
target_compile_options(latency_probe INTERFACE -Wall -Wextra -Werror)

add_executable(probe_tests tests/probe_tests.cpp)
target_link_libraries(probe_tests PRIVATE latency_probe)
add_executable(probe_bench bench/bench.cpp)
target_link_libraries(probe_bench PRIVATE latency_probe)

enable_testing()
add_test(NAME probe_tests COMMAND probe_tests)
