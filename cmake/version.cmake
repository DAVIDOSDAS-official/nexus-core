# Run at build time, not configure time.
#
# Configure runs once and then only when CMake decides it must. A
# commit read there is frozen: editing a file rebuilds the binary
# without re-reading git, so the version string keeps reporting the
# commit that was current when configure last ran. That is a tool
# reporting confidently about its own stale view, which is the exact
# failure this string exists to catch.
#
# configure_file leaves the header untouched when the contents have
# not changed, so this costs a rebuild only when the answer differs.
find_package(Git QUIET)

set(NEXUS_COMMIT "unknown")
set(NEXUS_DIRTY "")

# An override wins over the lookup. A container build has no .git to
# read -- the commit is known on the host and handed in, rather than
# guessed at or left blank.
if(NOT "${OVERRIDE}" STREQUAL "" AND NOT "${OVERRIDE}" STREQUAL "unknown")
    set(NEXUS_COMMIT "${OVERRIDE}")
elseif(GIT_FOUND AND EXISTS "${SRC}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${SRC}"
        OUTPUT_VARIABLE FOUND_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(NOT FOUND_COMMIT STREQUAL "")
        set(NEXUS_COMMIT "${FOUND_COMMIT}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain
        WORKING_DIRECTORY "${SRC}"
        OUTPUT_VARIABLE FOUND_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(NOT FOUND_STATUS STREQUAL "")
        set(NEXUS_DIRTY "-dirty")
    endif()
endif()

set(NEXUS_COMMIT "${NEXUS_COMMIT}${NEXUS_DIRTY}")

configure_file("${SRC}/cmake/nexus_version.h.in" "${OUT}" @ONLY)
