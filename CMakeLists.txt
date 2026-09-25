cmake_minimum_required(VERSION 3.20)

# The one place the version is written down. The binary reports it,
# the image reads it from the binary (image/identity.sh), and
# publish.sh reads it from the image. Change it here, commit, build.
project(NexusCore
    VERSION 0.1.10
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
#
# Regenerated on every build; see cmake/version.cmake for why.
set(NEXUS_VERSION_DIR "${CMAKE_BINARY_DIR}/generated")
set(NEXUS_VERSION_HEADER "${NEXUS_VERSION_DIR}/nexus_version.h")

add_custom_target(nexus_version_header ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSRC=${CMAKE_SOURCE_DIR}"
        "-DOUT=${NEXUS_VERSION_HEADER}"
        "-DNEXUS_VERSION=${PROJECT_VERSION}"
        "-DOVERRIDE=${NEXUS_COMMIT_OVERRIDE}"
        -P "${CMAKE_SOURCE_DIR}/cmake/version.cmake"
    COMMENT "Reading build version from git"
    VERBATIM
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(core)
add_subdirectory(system)
add_subdirectory(hardware)
add_subdirectory(cli)

add_dependencies(nexus nexus_version_header)
target_include_directories(nexus PRIVATE "${NEXUS_VERSION_DIR}")

# Tests need GoogleTest, which a deployment image has no reason to
# carry. Building the tool and testing the tool are different jobs.
option(NEXUS_BUILD_TESTS "Build the test suite" ON)

if(NEXUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
