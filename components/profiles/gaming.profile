Profile: gaming
Description: Steam, Heroic and Lutris, Proton versions, and the drivers games need
Architecture: amd64
Enables-Architectures: i386
Requires: steam,
 vulkan-driver,
 opengl,
 audio-server,
 gamemode,
 mangohud,
 gamescope,
 heroic,
 lutris,
 protonup-qt,
 gpu-vendor-amd | gpu-vendor-intel | gpu-vendor-nvidia
Prefers: audio-server=pipewire
# Steam from Flathub, not from RPM Fusion. As an rpm layered on an
# image-based system it needs 32-bit libraries at exactly the image's
# versions, and a Fedora update to any of them made it uninstallable
# until the next image build (26 September). The Flatpak carries its
# own. A Steam already installed as a package still counts.
#
# More than Steam (1 October): Heroic runs the Epic, GOG and Amazon
# libraries, Lutris everything else (other launchers, older games,
# emulators), and ProtonUp-Qt installs community Proton builds
# (Proton-GE) for games Valve's Proton does not run yet. All three from
# Flathub: Heroic and ProtonUp-Qt are not in Fedora, and Lutris as a
# layered rpm would pull Wine's 32-bit libraries at the image's exact
# versions -- Steam's old problem.
Flatpak: steam=com.valvesoftware.Steam,
 heroic=com.heroicgameslauncher.hgl,
 lutris=net.lutris.Lutris,
 protonup-qt=net.davidotek.pupgui2
Prefers-When: gpu-vendor-amd -> mesa-vulkan-drivers,
 gpu-vendor-intel -> mesa-vulkan-drivers,
 gpu-vendor-nvidia -> nvidia-driver-libs
