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
#     ./image/publish.sh
#     PROFILE=minimal ./image/publish.sh
#     REGISTRY=ghcr.io/somewhere-else ./image/publish.sh
#
# The tag is the profile, so one repository holds every variant and a
# machine pulls the one it was installed with.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

PROFILE="${PROFILE:-minimalism}"

# Defaulted rather than demanded.
#
# Forgetting it is the failure this script exists to prevent, and an
# argument somebody has to remember is one they eventually do not.
REGISTRY="${REGISTRY:-ghcr.io/davidosdas-official}"
NAME="${NAME:-nexus-core}"
# Not defaulted: read from the image below. See the version check.
ASKED_VERSION="${VERSION:-}"

if [ -z "${REGISTRY}" ]; then
    cat >&2 <<'USAGE'
No registry given. Where should this be published?

    REGISTRY=ghcr.io/yourname ./image/publish.sh

GitHub Container Registry is free for public images. Log in once:

    echo "$GITHUB_TOKEN" | podman login ghcr.io -u DAVIDOSDAS-official \
        --password-stdin

The token needs write:packages.
USAGE
    exit 2
fi

# Image references are lowercase; GitHub account names are not.
#
# The account is DAVIDOSDAS-official, and typing it that way produces
# "repository name must be lowercase" from podman -- after the build,
# which is the expensive place to learn it. GitHub itself does not
# care about the case of the account, so lowercasing here is safe and
# means the name can be written the way it is written everywhere else.
LOWERED="$(printf '%s' "${REGISTRY}" | tr '[:upper:]' '[:lower:]')"

if [ "${LOWERED}" != "${REGISTRY}" ]; then
    echo "Registry lowercased for the image reference:"
    echo "    ${REGISTRY}  ->  ${LOWERED}"
    echo
    REGISTRY="${LOWERED}"
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

# The version is the image's own, read from the binary inside it.
#
# It used to default to a number written here, so a tag could name a
# version the image did not contain -- the same disagreement that left
# os-release saying 0.1.1 while `nexus --version` said 0.1.0. The only
# place the version is written down is CMakeLists.txt; everything else
# asks.
VERSION="$(podman run --rm "${LOCAL}" nexus --version 2>/dev/null \
    | awk 'NR == 1 {print $2}')"

case "${VERSION}" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *)
        echo "Could not read a version from ${LOCAL} (got '${VERSION}');" >&2
        echo "refusing to tag it." >&2
        exit 1
        ;;
esac

if [ -n "${ASKED_VERSION}" ] && [ "${ASKED_VERSION}" != "${VERSION}" ]; then
    echo "Asked to publish ${ASKED_VERSION}, but ${LOCAL} is ${VERSION}." >&2
    echo "Build it again after changing CMakeLists.txt, or drop VERSION=." >&2
    exit 1
fi

echo "Version: ${VERSION}"
echo

# Logged in, as the user who is about to push?
#
# Asked rather than inferred from a failed push. podman keeps logins
# in $XDG_RUNTIME_DIR, which is emptied at every shutdown, so a login
# that worked yesterday is gone today -- and GHCR's answer to a push
# nobody is logged in for is "403 (Forbidden)" while "requesting bear
# token", which reads like a permissions problem rather than a missing
# login. It was misdiagnosed twice (22 and 24 September): once as the
# token's scope, once as a registry refusal. Both times the fix was to
# log in again.
#
# Not `sudo podman login`: root's podman and yours keep separate
# logins, and this script pushes as you.
LOGIN_HOST="${REGISTRY%%/*}"

if ! WHO="$(podman login --get-login "${LOGIN_HOST}" 2>/dev/null)"; then
    echo "Not logged in to ${LOGIN_HOST} as $(id -un)." >&2
    echo "Logins are forgotten at every shutdown. Log in, no sudo:" >&2
    echo "    podman login ${LOGIN_HOST} -u DAVIDOSDAS-official" >&2
    exit 1
fi

echo "Logged in to ${LOGIN_HOST} as ${WHO}."
echo

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
    echo "Push failed; see podman's message above." >&2
    echo "  401 or 403 while requesting a token: the login -- check with" >&2
    echo "      podman login --get-login ${REGISTRY%%/*}" >&2
    exit 1
fi

podman push "${REGISTRY}/${NAME}:${PROFILE}-${VERSION}" || true

cat <<NEXT

Pushed ${REMOTE}

The package is private until you say otherwise. A machine running
'bootc upgrade' has no GitHub account, so until it is public every
update fails with an authentication error that says nothing about
the cause:

    https://github.com/users/DAVIDOSDAS-official/packages/container/${NAME}/settings
    -> Danger Zone -> Change visibility -> Public

Check it from a machine that has never logged in:

    podman logout ghcr.io
    podman pull ${REMOTE}

Now build the ISO from this name rather than from localhost. No
rebuild is needed -- the tag above already points at the image that
was just pushed, and bootc records the name it was installed from:

    IMAGE=${REMOTE} PROFILE=${PROFILE} ./image/build-installer.sh

After installing from that ISO, the machine should agree:

    sudo bootc status | grep -i image

If that says localhost, the ISO was built from the wrong name and
the machine can never update. That is the whole point of this
script, and it is worth checking rather than assuming.

Then, on an installed machine:

    sudo rpm-ostree upgrade
    sudo reboot

Not bootc upgrade on a machine that has been set up: bootc refuses a
deployment with packages layered on top, and first-boot setup layers
them. rpm-ostree upgrade pulls the same image and keeps them.

The version tag ${PROFILE}-${VERSION} is pushed as well, so a machine
can be pinned to a known build rather than always taking the newest.
NEXT
