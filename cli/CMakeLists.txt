# One file per command area (0.1.96): main.cpp reads the command line,
# cli.hpp declares what the files share.
add_executable(nexus
    src/main.cpp
    src/common.cpp
    src/inspect.cpp
    src/profile.cpp
    src/install.cpp
    src/remove.cpp
    src/source.cpp
    src/container.cpp
    src/setup.cpp
    src/app.cpp
    src/language.cpp
    src/summary.cpp
    src/doctor.cpp
    src/history.cpp
    src/update.cpp
    src/rollback.cpp
    src/security.cpp
    src/server.cpp
    src/dualboot.cpp
    src/gaming.cpp
    src/gamecheck.cpp
    src/cyber.cpp
    src/dev.cpp
    src/vpn.cpp
    src/specs.cpp
    src/guide.cpp
)

target_link_libraries(nexus
    PRIVATE
        nexus_core
        nexus_system
        nexus_hardware
)
