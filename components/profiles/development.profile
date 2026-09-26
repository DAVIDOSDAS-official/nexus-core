Profile: development
Description: Compilers, build tools, a debugger, and a container for everything else
# C and C++ and Python here; every other language goes into a
# Distrobox container, where it can be installed with that
# distribution's own tools. On an image-based system that matters
# most: install anything in there, the system itself stays untouched.
Requires: compiler-toolchain,
 git,
 make,
 cmake,
 ninja-build,
 pkg-config,
 gdb | lldb,
 python3-pip,
 distrobox
