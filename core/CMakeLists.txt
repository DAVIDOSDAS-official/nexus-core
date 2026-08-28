add_library(nexus_core
    src/core.cpp
    src/component.cpp
    src/capability.cpp
    src/resolver.cpp
)

target_include_directories(nexus_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(nexus_core PUBLIC cxx_std_20)
