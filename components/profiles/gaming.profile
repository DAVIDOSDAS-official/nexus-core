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
Prefers-When: gpu-vendor-amd -> mesa-vulkan-drivers,
 gpu-vendor-intel -> mesa-vulkan-drivers,
 gpu-vendor-nvidia -> nvidia-driver-libs
