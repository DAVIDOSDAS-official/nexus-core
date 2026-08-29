#!/usr/bin/env bash
#
# Build the Nexus image, one stage at a time.
#
# Stages are built separately on purpose: a failure then names the
# layer that broke instead of just the build.

set -euo pipefail

TAG="${TAG:-localhost/nexus-os}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

for stage in base desktop gaming final; do
    echo
    echo "=== building stage: ${stage} ==="

    podman build \
        --target "${stage}" \
        --tag "${TAG}:${stage}" \
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
