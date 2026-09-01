cmake_minimum_required(VERSION 3.16)

project(telemetry_agent LANGUAGES CXX)

include(CTest)

add_executable(telemetry_agent
    src/linux/cgroup_collector.cpp
    src/linux/cpu_collector.cpp
    src/linux/disk_collector.cpp
    src/linux/mem_collector.cpp
    src/linux/network_collector.cpp
    src/linux/process_collector.cpp
    src/linux/tcp_collector.cpp
    src/telemetry_collector.cpp
    src/main.cpp
)

target_include_directories(telemetry_agent PRIVATE include)

target_compile_features(telemetry_agent PRIVATE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(telemetry_agent PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(cgroup_collector_test
        tests/cgroup_collector_test.cpp
        src/linux/cgroup_collector.cpp
    )
    target_include_directories(cgroup_collector_test PRIVATE include)
    target_compile_features(cgroup_collector_test PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(cgroup_collector_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME cgroup_collector_test COMMAND cgroup_collector_test)
endif()
