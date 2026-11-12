cmake_minimum_required(VERSION 3.20)

# The one place the version is written down. The binary reports it,
# the image reads it from the binary (image/identity.sh), and
# publish.sh reads it from the image. Change it here, commit, build.
project(NexusCore
    VERSION 0.1.89
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

# Hardening, said here rather than left to whatever the compiler was
# built with (0.1.87, audit). The image is built with Fedora's plain
# g++ through this file -- not through rpmbuild, which is where Fedora's
# hardening flags normally come from -- so `nexus`, which runs as root
# and reads what other machines and other systems' disks hand it, was
# getting only the compiler's defaults. tools/check-hardening.sh reads
# every one of these back from the built program, in ctest and again
# in the image build; it says what each of its checks can and cannot
# prove (asking for a flag here is not the same as having it).
#
#   position independent + full RELRO + BIND_NOW: the program's own
#     addresses are random, and its tables of function addresses are
#     read-only before main() runs
#   stack protector, stack clash protection, non-executable stack
#   _FORTIFY_SOURCE: the C library checks the sizes it can know
#     (needs optimisation, so not in a Debug build)
#   _GLIBCXX_ASSERTIONS: v[i] past the end of a vector or string stops
#     the program instead of reading or writing next to it
#   -fcf-protection: the program is marked for Intel CET, where the
#     compiler knows it. A mark, not a guarantee: the kernel and the
#     processor decide whether it is enforced
option(NEXUS_HARDENING "Build with hardening flags" ON)
if(NEXUS_HARDENING)
    include(CheckCXXCompilerFlag)
    include(CheckPIESupported)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    check_pie_supported(LANGUAGES CXX)
    add_compile_options(-fstack-protector-strong)
    add_compile_definitions(_GLIBCXX_ASSERTIONS)
    add_compile_options(
        "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-U_FORTIFY_SOURCE>"
        "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-D_FORTIFY_SOURCE=3>")
    check_cxx_compiler_flag(-fstack-clash-protection NEXUS_HAS_STACK_CLASH)
    if(NEXUS_HAS_STACK_CLASH)
        add_compile_options(-fstack-clash-protection)
    endif()
    check_cxx_compiler_flag(-fcf-protection NEXUS_HAS_CF_PROTECTION)
    if(NEXUS_HAS_CF_PROTECTION)
        add_compile_options(-fcf-protection)
    endif()
    add_link_options(-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
endif()

add_subdirectory(core)
add_subdirectory(system)
add_subdirectory(hardware)
add_subdirectory(cli)

add_dependencies(nexus nexus_version_header)
target_include_directories(nexus PRIVATE "${NEXUS_VERSION_DIR}")

# Nexus Shop needs Qt 6; the tool does not. The image builds it, a
# plain checkout does not unless asked.
option(NEXUS_BUILD_SHOP "Build Nexus Shop (needs Qt 6)" OFF)

if(NEXUS_BUILD_SHOP)
    add_subdirectory(gui/shop)
endif()

# Tests need GoogleTest, which a deployment image has no reason to
# carry. Building the tool and testing the tool are different jobs.
option(NEXUS_BUILD_TESTS "Build the test suite" ON)

if(NEXUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
