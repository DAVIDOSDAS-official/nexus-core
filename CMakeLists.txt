cmake_minimum_required(VERSION 3.20)

project(NexusCore
    VERSION 0.1.0
    DESCRIPTION "A modular Linux operating system platform"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(core)
add_subdirectory(system)
add_subdirectory(hardware)
add_subdirectory(cli)

# Tests need GoogleTest, which a deployment image has no reason to
# carry. Building the tool and testing the tool are different jobs.
option(NEXUS_BUILD_TESTS "Build the test suite" ON)

if(NEXUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
