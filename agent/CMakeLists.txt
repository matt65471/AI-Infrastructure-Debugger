cmake_minimum_required(VERSION 3.16)

project(telemetry_agent LANGUAGES CXX)

include(CTest)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_executable(telemetry_agent
    src/container_metric_calculator.cpp
    src/deployment_metric_aggregator.cpp
    src/json_formatter.cpp
    src/infrastructure_exporter.cpp
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
target_link_libraries(telemetry_agent PRIVATE CURL::libcurl Threads::Threads)

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

    add_executable(json_formatter_test
        tests/json_formatter_test.cpp
        src/json_formatter.cpp
    )
    target_include_directories(json_formatter_test PRIVATE include)
    target_compile_features(json_formatter_test PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(json_formatter_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME json_formatter_test COMMAND json_formatter_test)

    add_executable(infrastructure_exporter_test
        tests/infrastructure_exporter_test.cpp
        src/infrastructure_exporter.cpp
    )
    target_include_directories(infrastructure_exporter_test PRIVATE include)
    target_compile_features(infrastructure_exporter_test PRIVATE cxx_std_17)
    target_link_libraries(infrastructure_exporter_test PRIVATE CURL::libcurl Threads::Threads)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(infrastructure_exporter_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME infrastructure_exporter_test COMMAND infrastructure_exporter_test)
    add_test(
        NAME telemetry_agent_rejects_partial_http_config
        COMMAND telemetry_agent --ingest-url http://127.0.0.1:30081/v1/infra-snapshots
    )
    set_tests_properties(
        telemetry_agent_rejects_partial_http_config
        PROPERTIES WILL_FAIL TRUE
    )

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
