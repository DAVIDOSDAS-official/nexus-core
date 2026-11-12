find_package(GTest REQUIRED)

add_executable(component_test
    unit/component_test.cpp
    unit/capability_test.cpp
    unit/resolver_test.cpp
    unit/dependency_graph_test.cpp
    unit/control_file_test.cpp
    unit/dependency_expression_test.cpp
    unit/dpkg_source_test.cpp
    unit/version_test.cpp
    unit/conflict_detector_test.cpp
    unit/solver_test.cpp
    unit/architecture_test.cpp
    unit/profile_test.cpp
    unit/apt_source_test.cpp
    unit/hardware_test.cpp
    unit/encryption_test.cpp
    unit/secure_boot_test.cpp
    unit/removal_test.cpp
    unit/transaction_test.cpp
    unit/options_test.cpp
    unit/alias_test.cpp
    unit/diagnosis_test.cpp
    unit/composition_test.cpp
    unit/source_test.cpp
    unit/flatpak_test.cpp
    unit/size_test.cpp
    unit/pacman_test.cpp
    unit/hostile_test.cpp
    unit/container_test.cpp
    unit/process_test.cpp
    unit/services_test.cpp
    unit/snap_test.cpp
    unit/nix_test.cpp
    unit/vpn_test.cpp
    unit/portage_test.cpp
    unit/interrupted_test.cpp
    unit/protection_test.cpp
    unit/transaction_log_test.cpp
    unit/rpm_version_test.cpp
    unit/xml_test.cpp
    unit/rpm_source_test.cpp
    unit/rpm_repo_test.cpp
    unit/rpm_database_test.cpp
    unit/plan_check_test.cpp
    unit/gamecheck_test.cpp
    unit/language_test.cpp
    unit/update_test.cpp
    unit/catalogue_test.cpp
    unit/dualboot_test.cpp
    unit/server_test.cpp
    unit/specs_test.cpp
    unit/clock_test.cpp
    unit/suggest_test.cpp
    unit/vpn_check_test.cpp
    unit/cyber_test.cpp
    unit/dev_test.cpp
    unit/rollback_test.cpp
    unit/gaming_test.cpp
    unit/security_audit_test.cpp
    unit/pins_test.cpp
    unit/site_copy_test.cpp
    unit/plan_apply_test.cpp
)

target_link_libraries(component_test
    PRIVATE
        nexus_core
        nexus_system
        nexus_hardware
        GTest::gtest_main
)

include(GoogleTest)

gtest_discover_tests(component_test)

# Odd input for every text parser (0.1.87). A quick pass here; longer,
# and with the sanitizers on, in the Tests workflow.
add_executable(parser_fuzz fuzz/parser_fuzz.cpp)
target_link_libraries(parser_fuzz PRIVATE nexus_core nexus_system nexus_hardware)
add_test(NAME ParserFuzz COMMAND parser_fuzz)
set_tests_properties(ParserFuzz PROPERTIES TIMEOUT 600)

# Nothing changes without --apply: every command that can change the
# system, and every command that only reads, run without it (0.1.87).
# With strace installed it sees every program started and every file
# written, anywhere; without, a reduced check, and it says which it was
# (0.1.88). The self-test breaks the rule in twelve ways and requires
# the test to notice.
add_test(NAME NoApplyChangesNothing
         COMMAND bash ${PROJECT_SOURCE_DIR}/tests/no-apply-changes-nothing.sh $<TARGET_FILE:nexus>)
# RUN_SERIAL: it watches folders for changes, so nothing else may be
# running (ServerBackup, as root, writes to the very folders it watches).
set_tests_properties(NoApplyChangesNothing PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800 RUN_SERIAL TRUE)
add_test(NAME NoApplySelfTest
         COMMAND bash ${PROJECT_SOURCE_DIR}/tests/no-apply-changes-nothing.sh --self-test)
set_tests_properties(NoApplySelfTest PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 RUN_SERIAL TRUE)
add_test(NAME NoApplyTraceRules
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/no-apply-trace-rules.sh)
set_tests_properties(NoApplyTraceRules PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

# Server backup and removal through every way they can go wrong
# (0.1.88). Needs root and a throwaway machine, so it is skipped unless
# run as root with NEXUS_TEST_ALLOW_ROOT=1; the Tests workflow does.
add_test(NAME ServerBackup
         COMMAND bash ${PROJECT_SOURCE_DIR}/tests/server-backup-check.sh $<TARGET_FILE:nexus>)
set_tests_properties(ServerBackup PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 RUN_SERIAL TRUE)

# The nexus that was just built is hardened the way CMakeLists.txt asks:
# read from the program, not from the flags (0.1.87; 0.1.88 for the
# protections beyond the first five). What is expected is what this
# build asked for AND this compiler can show: FORTIFY needs an
# optimised build, CET and the stack-clash probes are x86-64, the
# assertion handler is visible from GCC 13. Skipped, and said so,
# where readelf is not installed.
if(NEXUS_HARDENING)
    set(NEXUS_HARDENING_EXPECT "")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
            list(APPEND NEXUS_HARDENING_EXPECT "--expect=assertions")
        endif()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
            if(NEXUS_HAS_CF_PROTECTION)
                list(APPEND NEXUS_HARDENING_EXPECT "--expect=cet")
            endif()
            if(NEXUS_HAS_STACK_CLASH)
                list(APPEND NEXUS_HARDENING_EXPECT "--expect=stack-clash")
            endif()
        endif()
    endif()
    add_test(NAME Hardening
             COMMAND bash ${PROJECT_SOURCE_DIR}/tools/check-hardening.sh
                     ${NEXUS_HARDENING_EXPECT}
                     "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:--expect=fortify>"
                     $<TARGET_FILE:nexus>)
    set_tests_properties(Hardening PROPERTIES SKIP_RETURN_CODE 77)
endif()

# The checker itself: it must fail a program with any one protection
# taken away (0.1.88). Needs a compiler and python3; x86-64 only.
add_test(NAME HardeningCheckSelfTest
         COMMAND bash ${PROJECT_SOURCE_DIR}/tests/check-hardening-selftest.sh ${CMAKE_CXX_COMPILER})
set_tests_properties(HardeningCheckSelfTest PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)

# No value joined into a shell command without shellQuote (0.1.84).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME ShellQuoting
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check-shell-quoting.py ${PROJECT_SOURCE_DIR})
    # curl stays on https; Firefox and NetworkManager privacy files
    # are valid (0.1.86).
    add_test(NAME NetworkHygiene
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check-network-hygiene.py ${PROJECT_SOURCE_DIR})
endif()
