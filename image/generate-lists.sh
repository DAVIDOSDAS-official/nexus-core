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
