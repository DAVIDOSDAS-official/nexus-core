find_package(GTest REQUIRED)

add_executable(component_test
    unit/component_test.cpp
    unit/capability_test.cpp
    unit/resolver_test.cpp
)

target_link_libraries(component_test
    PRIVATE
        nexus_core
        GTest::gtest_main
)

include(GoogleTest)

gtest_discover_tests(component_test)
