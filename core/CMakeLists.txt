add_library(nexus_core
    src/core.cpp
    src/component.cpp
    src/architecture.cpp
    src/constraint.cpp
    src/requirement.cpp
    src/solver.cpp
    src/profile_check.cpp
    src/conflict_detector.cpp
    src/capability.cpp
    src/resolver.cpp
    src/dependency_graph.cpp
)

target_include_directories(nexus_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(nexus_core PUBLIC cxx_std_20)
