cmake_minimum_required(VERSION 3.16)

project(telemetry_agent LANGUAGES CXX)

include(CTest)

add_executable(telemetry_agent
    src/container_metric_calculator.cpp
    src/deployment_metric_aggregator.cpp
    src/kubernetes_metadata_collector.cpp
    src/pod_metric_aggregator.cpp
    src/linux/cgroup_collector.cpp
    src/linux/cpu_collector.cpp
    src/linux/disk_collector.cpp
    src/linux/mem_collector.cpp
    src/linux/network_collector.cpp
    src/linux/process_collector.cpp
    src/linux/pressure_collector.cpp
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

    add_executable(pressure_collector_test
        tests/pressure_collector_test.cpp
        src/linux/pressure_collector.cpp
    )
    target_include_directories(pressure_collector_test PRIVATE include)
    target_compile_features(pressure_collector_test PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(pressure_collector_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME pressure_collector_test COMMAND pressure_collector_test)

    add_executable(container_metric_calculator_test
        tests/container_metric_calculator_test.cpp
        src/container_metric_calculator.cpp
    )
    target_include_directories(container_metric_calculator_test PRIVATE include)
    target_compile_features(container_metric_calculator_test PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(container_metric_calculator_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(
        NAME container_metric_calculator_test
        COMMAND container_metric_calculator_test
    )

    add_executable(kubernetes_metadata_collector_test
        tests/kubernetes_metadata_collector_test.cpp
        src/kubernetes_metadata_collector.cpp
    )
    target_include_directories(kubernetes_metadata_collector_test PRIVATE include)
    target_compile_features(kubernetes_metadata_collector_test PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(kubernetes_metadata_collector_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(
        NAME kubernetes_metadata_collector_test
        COMMAND kubernetes_metadata_collector_test
    )

    add_executable(pod_metric_aggregator_test
        tests/pod_metric_aggregator_test.cpp
        src/pod_metric_aggregator.cpp
    )
    target_include_directories(pod_metric_aggregator_test PRIVATE include)
    target_compile_features(pod_metric_aggregator_test PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(pod_metric_aggregator_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(
        NAME pod_metric_aggregator_test
        COMMAND pod_metric_aggregator_test
    )

    add_executable(deployment_metric_aggregator_test
        tests/deployment_metric_aggregator_test.cpp
        src/deployment_metric_aggregator.cpp
    )
    target_include_directories(deployment_metric_aggregator_test PRIVATE include)
    target_compile_features(deployment_metric_aggregator_test PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(deployment_metric_aggregator_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(
        NAME deployment_metric_aggregator_test
        COMMAND deployment_metric_aggregator_test
    )
endif()
