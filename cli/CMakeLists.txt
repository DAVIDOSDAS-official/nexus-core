add_executable(nexus
    src/main.cpp
)

target_link_libraries(nexus
    PRIVATE
        nexus_core
        nexus_system
)
