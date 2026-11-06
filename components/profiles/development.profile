Profile: development
Description: Compilers, build tools, a debugger, and a container for everything else
# C and C++ and Python here; every other language goes into a
# Distrobox container -- `nexus dev add js,java,rust,go,ruby` (0.1.69)
# puts them in "nexus-dev" and their commands in ~/.local/bin, so they
# work in any terminal. On an image-based system that matters most:
# install anything in there, the system itself stays untouched.
Requires: compiler-toolchain,
 git,
 make,
 cmake,
 ninja-build,
 pkg-config,
 gdb | lldb,
 python3-pip,
 distrobox
