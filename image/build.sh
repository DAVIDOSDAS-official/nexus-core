#!/usr/bin/env bash
#
# Build the Nexus image, one stage at a time.
#
# Stages are built separately on purpose: a failure then names the
# layer that broke instead of just the build.

set -euo pipefail

TAG="${TAG:-localhost/nexus-os}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# The commit, or the binary cannot identify itself.
#
# .containerignore keeps .git out of the build context, so the build
# cannot look this up for itself -- it has to be handed in. Left out,
# every image built by this script reports 'unknown' and doctor warns
# about it, which is how an installed machine ends up unable to say
# which source it was built from.
#
# Read here rather than left to the caller: a step somebody has to
# remember is a step that gets forgotten, and this one fails silently.
if [ -n "${NEXUS_COMMIT:-}" ]; then
    :
elif git -C "${ROOT}" rev-parse --short HEAD > /dev/null 2>&1; then
    NEXUS_COMMIT="$(git -C "${ROOT}" rev-parse --short HEAD)"

    # A modified tree that claims to be a clean commit is the exact
    # failure the version string exists to catch.
    #
    # Only asked where there is a repository to ask. Outside one,
    # 'git diff' fails for want of a repository rather than because
    # anything was modified, and reading that as "modified" produces
    # 'unknown-dirty' -- a string that states two things and knows
    # neither.
    if ! git -C "${ROOT}" diff --quiet; then
        NEXUS_COMMIT="${NEXUS_COMMIT}-dirty"
        echo "Uncommitted changes; building as ${NEXUS_COMMIT}." >&2
    fi
else
    NEXUS_COMMIT="unknown"
    echo "Not a git repository; the image will report 'unknown'." >&2
fi

for stage in builder base desktop gaming final; do
    echo
    echo "=== building stage: ${stage} ==="

    podman build \
        --target "${stage}" \
        --tag "${TAG}:${stage}" \
        --build-arg NEXUS_COMMIT="${NEXUS_COMMIT}" \
        --file "${ROOT}/image/Containerfile" \
        "${ROOT}"
done

podman tag "${TAG}:final" "${TAG}:latest"

echo
echo "Built ${TAG}:latest"
echo
echo "Check what went in:"
echo "    podman run --rm ${TAG}:latest nexus scan"
echo "    podman run --rm ${TAG}:latest nexus hardware"
echo
echo "Turn it into a bootable disk image:"
echo "    sudo podman run --rm -it --privileged \\"
echo "        --security-opt label=type:unconfined_t \\"
echo "        -v ./output:/output \\"
echo "        -v /var/lib/containers/storage:/var/lib/containers/storage \\"
echo "        quay.io/centos-bootc/bootc-image-builder:latest \\"
echo "        --type qcow2 --local ${TAG}:latest"
