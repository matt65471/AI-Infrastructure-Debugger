cmake_minimum_required(VERSION 3.16)

project(telemetry_agent LANGUAGES CXX)

add_executable(telemetry_agent
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
