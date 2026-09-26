Profile: gaming
Description: Steam, graphics drivers and the 32-bit libraries games need
Architecture: amd64
Enables-Architectures: i386
Requires: steam,
 vulkan-driver,
 opengl,
 audio-server,
 gamemode,
 gpu-vendor-amd | gpu-vendor-intel | gpu-vendor-nvidia
Prefers: audio-server=pipewire
# Steam from Flathub, not from RPM Fusion. As an rpm layered on an
# image-based system it needs 32-bit libraries at exactly the image's
# versions, and a Fedora update to any of them made it uninstallable
# until the next image build (26 September). The Flatpak carries its
# own. A Steam already installed as a package still counts.
Flatpak: steam=com.valvesoftware.Steam
Prefers-When: gpu-vendor-amd -> mesa-vulkan-drivers,
 gpu-vendor-intel -> mesa-vulkan-drivers,
 gpu-vendor-nvidia -> nvidia-driver-libs
