Profile: gaming
Description: Steam, graphics drivers and the 32-bit libraries games need
Architecture: amd64
Enables-Architectures: i386
Requires: steam-installer | steam,
 mesa-vulkan-drivers | nvidia-driver-libs,
 libgl1,
 pipewire | pulseaudio,
 gamemode
Prefers: audio=pipewire
