add_library(nexus_hardware
    src/hardware.cpp
    src/encryption.cpp
    src/secure_boot.cpp
)

target_include_directories(nexus_hardware
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nexus_hardware
    PUBLIC
        nexus_core
)

target_compile_features(nexus_hardware PUBLIC cxx_std_20)
