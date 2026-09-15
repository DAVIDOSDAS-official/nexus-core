cmake_minimum_required(VERSION 3.20)

project(NexusCore
    VERSION 0.1.0
    DESCRIPTION "A modular Linux operating system platform"
    LANGUAGES CXX
)

# Version identity.
#
# A binary that cannot say what it is leaves anyone debugging an
# installed machine guessing whether it matches the source in front of
# them. The release number alone does not answer that -- the commit
# does, and a build from a modified tree has to admit it rather than
# claim to be a clean one.
find_package(Git QUIET)

set(NEXUS_GIT_COMMIT "unknown")
set(NEXUS_GIT_DIRTY "")

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE NEXUS_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(NEXUS_GIT_COMMIT STREQUAL "")
        set(NEXUS_GIT_COMMIT "unknown")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE NEXUS_GIT_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(NOT NEXUS_GIT_STATUS STREQUAL "")
        set(NEXUS_GIT_DIRTY "-dirty")
    endif()

    # Re-run configure when the commit or the staged set changes, so
    # the string cannot go stale behind a rebuild.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/.git/HEAD"
        "${CMAKE_SOURCE_DIR}/.git/index"
    )
endif()

message(STATUS
    "Nexus version: ${PROJECT_VERSION} (${NEXUS_GIT_COMMIT}${NEXUS_GIT_DIRTY})")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(core)
add_subdirectory(system)
add_subdirectory(hardware)
add_subdirectory(cli)

target_compile_definitions(nexus PRIVATE
    NEXUS_VERSION="${PROJECT_VERSION}"
    NEXUS_COMMIT="${NEXUS_GIT_COMMIT}${NEXUS_GIT_DIRTY}"
)

# Tests need GoogleTest, which a deployment image has no reason to
# carry. Building the tool and testing the tool are different jobs.
option(NEXUS_BUILD_TESTS "Build the test suite" ON)

if(NEXUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
