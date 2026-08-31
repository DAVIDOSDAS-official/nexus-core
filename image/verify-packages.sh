#!/usr/bin/env bash
#
# Check every package name the Containerfile uses against the real
# Fedora and RPM Fusion repositories.
#
# Prints only what is wrong. Silence means every name is good.

set -uo pipefail

IMAGE="${IMAGE:-quay.io/fedora/fedora-bootc:42}"

# -i matters: without it podman does not attach stdin and the
# heredoc below is silently discarded, which looks exactly like a
# clean run.
podman run --rm -i "${IMAGE}" bash -s <<'INNER'
set -uo pipefail

echo "fedora release: $(rpm -E %fedora)"

echo "enabling rpmfusion..."
dnf install -y \
    "https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm" \
    "https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm" \
    > /dev/null 2>&1 || echo "PROBLEM: rpmfusion release packages did not install"

PACKAGES="
cmake gcc-c++ git make
plasma-workspace plasma-workspace-wayland plasma-desktop
plasma-nm plasma-pa sddm sddm-breeze xdg-desktop-portal-kde
konsole dolphin kate ark spectacle gwenview
mesa-dri-drivers.i686 mesa-vulkan-drivers.i686 mesa-libGL.i686
mesa-libEGL.i686 glibc.i686 libgcc.i686 alsa-lib.i686
pulseaudio-libs.i686 libX11.i686 libXext.i686 libXinerama.i686
libXrandr.i686 libXScrnSaver.i686 nss.i686
steam gamemode gamescope mangohud vulkan-tools
kernel-modules-extra libratbag-ratbagd
pipewire pipewire-pulseaudio wireplumber
"

missing=0

for package in ${PACKAGES}; do
    if ! dnf -q info "${package}" > /dev/null 2>&1; then
        echo "MISSING: ${package}"
        missing=$((missing + 1))
    fi
done

# The desktop is named package by package now, so there is no group
# left to check.

echo
echo "done: ${missing} missing package name(s)"
INNER
