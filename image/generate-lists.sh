#!/usr/bin/env bash
#
# Generate the package list for each profile that can produce an
# image, from inside the image itself.
#
# Inside, because that is where the Fedora repositories are. The
# profiles and aliases are mounted from the working tree rather than
# read from the image: without the mounts the baked-in copies are
# used, so editing a profile and regenerating produces the previous
# answer -- confidently, with nothing to say it is stale.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${IMAGE:-localhost/nexus-os:base}"

# Not every profile describes a whole machine. server and security add
# things to one; generating an image from them alone would produce a
# system with no way to log in.
PROFILES="${PROFILES:-minimal minimalism showcase}"

# Which image, and is it the one the Containerfile describes?
#
# The lists are only as current as the image they are read from, and
# nothing here used to check. On 22 September the base build failed
# half way through the move to Fedora 44 -- and this script, run
# straight after it, read the previous image instead, loaded the same
# 82,210 Fedora 42 packages as before, and produced a clean diff. A
# regeneration that could not have noticed a new release reported
# that nothing had changed in it.
#
# So: the image has to exist, and its Fedora has to be the one the
# Containerfile names. Anything else is a list for a different system.
if ! podman image exists "${IMAGE}"; then
    echo "No image ${IMAGE}. Build the base stage first:" >&2
    echo "    podman build --target base -t ${IMAGE} -f image/Containerfile ." >&2
    exit 1
fi

WANT="$(sed -n 's/^ARG FEDORA_VERSION=\([0-9][0-9]*\).*/\1/p' \
    "${ROOT}/image/Containerfile" | head -1)"
HAVE="$(podman run --rm "${IMAGE}" \
    sh -c '. /etc/os-release && echo "${VERSION_ID}"' 2>/dev/null)"
BUILT="$(podman image inspect --format '{{.Created}}' "${IMAGE}" \
    2>/dev/null | cut -d. -f1)"

echo "Image:   ${IMAGE}"
echo "Built:   ${BUILT:-unknown}"
echo "Fedora:  ${HAVE:-unknown} (Containerfile wants ${WANT:-unknown})"
echo

if [ -z "${WANT}" ] || [ -z "${HAVE}" ]; then
    echo "Could not tell which Fedora this is; refusing to guess." >&2
    exit 1
fi

if [ "${WANT}" != "${HAVE}" ]; then
    echo "The image is Fedora ${HAVE}; the Containerfile says ${WANT}." >&2
    echo "Lists generated from it would describe the wrong release." >&2
    echo "Rebuild the base stage, and check that the build finished:" >&2
    echo "    podman build --target base -t ${IMAGE} -f image/Containerfile ." >&2
    exit 1
fi

mkdir -p "${ROOT}/image/generated"

for profile in ${PROFILES}; do
    echo "=== ${profile} ==="

    if ! podman run --rm \
        -v "${ROOT}/components/profiles:/usr/share/nexus/profiles:ro" \
        -v "${ROOT}/components/aliases:/usr/share/nexus/aliases:ro" \
        "${IMAGE}" bash -c \
        'dnf makecache -q > /dev/null 2>&1
         nexus image '"${profile}"' --with-available' \
        > "${ROOT}/image/generated/${profile}.rpm.list.new"; then

        # An incomplete list still gets written, and building from one
        # produces an image missing whatever could not be resolved.
        # Saying so here is cheaper than finding out after the build.
        INCOMPLETE="${INCOMPLETE:-} ${profile}"
    fi

    # An empty list is not a smaller image, it is no image. Saying a
    # result is incomplete and then writing it anyway keeps the
    # destructive half of a safeguard and discards the useful half.
    new="${ROOT}/image/generated/${profile}.rpm.list.new"
    final="${ROOT}/image/generated/${profile}.rpm.list"

    if [ ! -s "${new}" ] || [ "$(grep -vc '^#' "${new}" || true)" -eq 0 ]; then
        echo "  nothing resolved; refusing to overwrite the previous list" >&2
        rm -f "${new}"
        INCOMPLETE="${INCOMPLETE} ${profile}"
        continue
    fi

    mv "${new}" "${final}"

    lines="$(grep -vc '^#' "${final}" || true)"

    echo "${lines} package(s)"
done

echo
echo "Generated:"
ls -la "${ROOT}/image/generated/"

if [ -n "${INCOMPLETE:-}" ]; then
    echo
    echo "Incomplete, some requirements could not be resolved:"
    echo "   ${INCOMPLETE}"
    echo "Building from these gives an image missing those pieces."
fi
