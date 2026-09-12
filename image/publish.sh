#!/usr/bin/env bash
#
# Push an image somewhere installed machines can reach it.
#
# This is the difference between a snapshot and a distribution. A
# bootc system updates by pulling a newer image from the place it was
# installed from; an image that only exists on the machine that built
# it can never update anything. Not a fix, not a security patch,
# nothing.
#
#     REGISTRY=ghcr.io/yourname ./image/publish.sh
#     REGISTRY=ghcr.io/yourname PROFILE=minimal ./image/publish.sh
#
# The tag is the profile, so one repository holds every variant and a
# machine pulls the one it was installed with.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

PROFILE="${PROFILE:-minimalism}"
REGISTRY="${REGISTRY:-}"
NAME="${NAME:-nexus-core}"
VERSION="${VERSION:-0.1}"

if [ -z "${REGISTRY}" ]; then
    cat >&2 <<'USAGE'
No registry given. Where should this be published?

    REGISTRY=ghcr.io/yourname ./image/publish.sh

GitHub Container Registry is free for public images. Log in once:

    echo "$GITHUB_TOKEN" | podman login ghcr.io -u yourname \
        --password-stdin

The token needs write:packages.
USAGE
    exit 2
fi

REMOTE="${REGISTRY}/${NAME}:${PROFILE}"
LOCAL="localhost/nexus-os:${PROFILE}"

echo "Local:   ${LOCAL}"
echo "Remote:  ${REMOTE}"
echo

if ! podman image exists "${LOCAL}"; then
    echo "No image ${LOCAL}. Build it first:" >&2
    echo "    podman build --target desktop \\" >&2
    echo "        --build-arg NEXUS_PROFILE=${PROFILE} \\" >&2
    echo "        -t ${LOCAL} -f image/Containerfile ." >&2
    exit 1
fi

# An image built as localhost/ cannot be updated from: bootc records
# where it was pulled from, and localhost is not somewhere a machine
# can reach. So it is retagged before pushing, and the ISO has to be
# built from the remote name for updates to work at all.
podman tag "${LOCAL}" "${REMOTE}"
podman tag "${LOCAL}" "${REGISTRY}/${NAME}:${PROFILE}-${VERSION}"

echo "Pushing. This takes a while: it is a few gigabytes."
echo

if ! podman push "${REMOTE}"; then
    echo >&2
    echo "Push failed. Usually that is a login:" >&2
    echo "    podman login ${REGISTRY%%/*}" >&2
    exit 1
fi

podman push "${REGISTRY}/${NAME}:${PROFILE}-${VERSION}" || true

cat <<NEXT

Pushed ${REMOTE}

For an installed machine to update from it, the ISO has to be built
from this name rather than from localhost:

    podman build --target desktop \\
        --build-arg NEXUS_PROFILE=${PROFILE} \\
        -t ${REMOTE} -f image/Containerfile .

    IMAGE=${REMOTE} PROFILE=${PROFILE} ./image/build-installer.sh

Then, on an installed machine:

    sudo bootc upgrade
    sudo reboot

The version tag ${PROFILE}-${VERSION} is pushed as well, so a machine
can be pinned to a known build rather than always taking the newest.
NEXT
