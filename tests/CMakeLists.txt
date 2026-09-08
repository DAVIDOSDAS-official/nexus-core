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
    unit/protection_test.cpp
    unit/transaction_log_test.cpp
    unit/rpm_version_test.cpp
    unit/xml_test.cpp
    unit/rpm_source_test.cpp
    unit/rpm_repo_test.cpp
    unit/rpm_database_test.cpp
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
