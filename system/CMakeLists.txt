add_library(nexus_system
    src/control_file.cpp
    src/dependency_expression.cpp
    src/dpkg_source.cpp
    src/version.cpp
    src/profile_file.cpp
    src/apt_source.cpp
    src/identity.cpp
    src/auto_installed.cpp
    src/rpm_version.cpp
    src/xml.cpp
    src/rpm_source.cpp
)

target_include_directories(nexus_system
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nexus_system
    PUBLIC
        nexus_core
)

target_compile_features(nexus_system PUBLIC cxx_std_20)
