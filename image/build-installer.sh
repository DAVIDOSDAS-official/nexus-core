#!/usr/bin/env bash
#
# Turn a Nexus image into something you can install from.
#
# bootc-image-builder makes the installer, the same way it makes the
# qcow2. Writing one here would mean reimplementing partitioning,
# which is the single most destructive thing a program can get wrong
# and is already solved.
#
#     ./image/build-installer.sh                 minimalism, an ISO
#     PROFILE=minimal ./image/build-installer.sh a lighter one
#     TYPE=qcow2 ./image/build-installer.sh      a disk to boot in a VM

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

PROFILE="${PROFILE:-minimalism}"
TYPE="${TYPE:-anaconda-iso}"
# A locally-built image cannot be updated from: bootc records where
# it was pulled, and localhost is nowhere a machine can reach. Pass
# IMAGE=ghcr.io/you/nexus-core:profile to build an ISO whose installs
# can receive updates.
IMAGE="${IMAGE:-localhost/nexus-os:${PROFILE}}"
OUTPUT="${OUTPUT:-${ROOT}/output}"

BUILDER="quay.io/centos-bootc/bootc-image-builder:latest"

echo "Profile: ${PROFILE}"
echo "Image:   ${IMAGE}"
echo "Type:    ${TYPE}"
echo

# An ISO is roughly the size of the image plus the installer. Running
# out of disk half way through a fifteen-minute build is a bad way to
# find that out.
NEEDED_GB=20

available="$(df -BG --output=avail "${OUTPUT%/*}" 2>/dev/null \
    | tail -1 | tr -dc '0-9')"

if [ -n "${available}" ] && [ "${available}" -lt "${NEEDED_GB}" ]; then
    echo "Only ${available} GB free; this wants about ${NEEDED_GB}." >&2
    echo "Stopping now rather than half way through." >&2
    exit 1
fi

if ! podman image exists "${IMAGE}"; then
    echo "No image ${IMAGE}. Build it first:" >&2
    echo "    podman build --target desktop \\" >&2
    echo "        --build-arg NEXUS_PROFILE=${PROFILE} \\" >&2
    echo "        -t ${IMAGE} -f image/Containerfile ." >&2
    exit 1
fi

mkdir -p "${OUTPUT}"

# The builder runs as root and reads the image from root's storage,
# so an image built rootless has to be handed over first.
echo "Handing the image to root's storage."
podman save "${IMAGE}" | sudo podman load

echo
echo "Building. This takes a while."
echo

sudo podman run --rm -it --privileged \
    --security-opt label=type:unconfined_t \
    -v "${OUTPUT}":/output \
    -v "${ROOT}/image/config.toml":/config.toml:ro \
    -v /var/lib/containers/storage:/var/lib/containers/storage \
    "${BUILDER}" \
    --type "${TYPE}" \
    --rootfs ext4 \
    "${IMAGE}"

STATUS=$?

sudo chown -R "$USER":"$USER" "${OUTPUT}" 2>/dev/null

if [ "${STATUS}" -ne 0 ]; then
    echo >&2
    echo "The build did not finish. Anything under ${OUTPUT} is" >&2
    echo "incomplete and should not be written to a disk." >&2
    exit "${STATUS}"
fi

echo
echo "Done:"
find "${OUTPUT}" -type f \( -name '*.iso' -o -name '*.qcow2' \) \
    -exec ls -lh {} \;

if [ "${IMAGE#localhost/}" != "${IMAGE}" ]; then
    cat <<'LOCAL'

Note: this was built from a local image, so machines installed from
it have nowhere to update from. bootc pulls from where it was
installed; localhost is not reachable.

To make updates possible, publish the image and build from that name:

    REGISTRY=ghcr.io/yourname ./image/publish.sh
    IMAGE=ghcr.io/yourname/nexus-core:PROFILE ./image/build-installer.sh
LOCAL
fi

cat <<'NEXT'

To write an ISO to a USB stick, check the device name first --
this overwrites whatever is on it:

    lsblk
    sudo dd if=output/bootiso/install.iso of=/dev/sdX bs=4M \
        status=progress oflag=sync

The kernel is Fedora's and carries Fedora's signature, so this
boots with Secure Boot on. 'nexus secureboot' explains what would
change if a kernel were ever built rather than inherited.
NEXT
