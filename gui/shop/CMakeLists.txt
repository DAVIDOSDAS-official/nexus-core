# Nexus Shop. Built only when asked (-DNEXUS_BUILD_SHOP=ON): the tool
# and its tests need no Qt, and a server image has no window to show.
find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Qml Quick)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(nexus-shop
    main.cpp
    shop.cpp
    shop.hpp
    shop.qrc
)

target_link_libraries(nexus-shop
    PRIVATE
        nexus_system
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

# The running Nexus version, to tell a staged update that has since
# taken effect (History).
add_dependencies(nexus-shop nexus_version_header)
target_include_directories(nexus-shop PRIVATE "${NEXUS_VERSION_DIR}")
