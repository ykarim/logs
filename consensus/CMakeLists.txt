cmake_minimum_required(VERSION 3.16)
project(consensus_lab LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(lab_core
    src/config.cpp
    src/client.cpp
    src/application.cpp
    src/runtime.cpp
    src/protocol_factory.cpp
    src/protocols/quepaxa_driver.cpp
    src/protocols/async_replication.cpp
    src/reads.cpp
    src/network.cpp
    src/inspection.cpp
    src/checks.cpp
)
target_include_directories(lab_core PUBLIC src)
target_compile_options(lab_core PRIVATE -Wall -Wextra -Wpedantic)
add_executable(consensus-lab src/main.cpp)
target_link_libraries(consensus-lab PRIVATE lab_core)
add_executable(lab-tests tests/tests.cpp tests/protocol_boundary.cpp)
target_link_libraries(lab-tests PRIVATE lab_core)
enable_testing()
add_test(NAME lab-tests COMMAND lab-tests)
