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
add_subdirectory(cli)

enable_testing()
add_subdirectory(tests)
