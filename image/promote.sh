#!/usr/bin/env bash
#
# Sign an image that is already in the registry, then point the tags
# machines follow at it. The second half of a weekly release; the
# first half is `STAGE=1 ./image/publish.sh`.
#
#     DIGEST=sha256:... PROFILE=kde NAME=nexus-core-testing \
#         COSIGN_KEY=/path/to/cosign.key ./image/promote.sh
#
# Why two halves (0.1.86, outside review H1, open since round 1): the
# signing key used to sit in the same job as the tests and the build,
# which run apt, the project's own code and the Containerfile. Anything
# that got to run code there could read the key, and with it sign an
# update every Nexus machine installs as root. Now the build job has no
# key at all: it pushes the image to a staging tag that no machine
# follows (<profile>-unsigned). This script runs in a separate job that
# GitHub starts only after the maintainer approves it (the "release"
# environment), and only that job can read the key.
#
# It signs exactly the digest the build job reported -- not whatever a
# tag says now -- and moves <profile> and <profile>-<version> only once
# the signature verifies with the public key the machines carry. Until
# then machines see no new image at all, rather than an unsigned one
# they would refuse.
#
# Needs: cosign 2.x, skopeo, and a registry login in REGISTRY_AUTH_FILE
# (or podman's default) with write access.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

PROFILE="${PROFILE:-kde}"
REGISTRY="${REGISTRY:-ghcr.io/davidosdas-official}"
REGISTRY="$(printf '%s' "${REGISTRY}" | tr '[:upper:]' '[:lower:]')"
NAME="${NAME:-nexus-core}"
DIGEST="${DIGEST:-}"
COSIGN_KEY="${COSIGN_KEY:-${HOME}/.config/nexus-signing/cosign.key}"
PUBLIC_KEY="${ROOT}/image/signing/cosign.pub"

case "${PROFILE}" in
    *[!a-z0-9-]* | "") echo "PROFILE '${PROFILE}' is not a profile name." >&2; exit 2 ;;
esac
case "${NAME}" in
    *[!a-z0-9._-]* | "") echo "NAME '${NAME}' is not an image name." >&2; exit 2 ;;
esac
# A digest, exactly: sha256: and 64 hex characters. Anything else is
# refused rather than looked up.
if ! printf '%s' "${DIGEST}" | grep -Eqx 'sha256:[0-9a-f]{64}'; then
    echo "DIGEST '${DIGEST}' is not a sha256 digest." >&2
    exit 2
fi

REPO="${REGISTRY}/${NAME}"
SIGNED_REF="${REPO}@${DIGEST}"

for tool in cosign skopeo; do
    if ! command -v "${tool}" > /dev/null 2>&1; then
        echo "${tool} is not installed." >&2
        exit 1
    fi
done
# cosign 3 writes signatures the machines' container tools cannot read
# yet (image/publish.sh says more).
if ! cosign version 2>/dev/null | grep -q 'GitVersion: *v2\.'; then
    echo "cosign 2.x is needed; this is:" >&2
    cosign version 2>/dev/null | grep GitVersion >&2
    exit 1
fi
if [ ! -f "${COSIGN_KEY}" ] || [ ! -f "${PUBLIC_KEY}" ]; then
    echo "Signing key missing. Expected:" >&2
    echo "    private: ${COSIGN_KEY}" >&2
    echo "    public:  ${PUBLIC_KEY}" >&2
    exit 1
fi

AUTH="${REGISTRY_AUTH_FILE:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/containers/auth.json}"
if [ ! -f "${AUTH}" ]; then
    echo "No registry login at ${AUTH}." >&2
    exit 1
fi
export REGISTRY_AUTH_FILE="${AUTH}"

# What is being signed, said before it is signed: the approval was for
# this run, and this is what the run built.
INFO="$(skopeo inspect --no-tags "docker://${SIGNED_REF}" 2>&1)" || {
    echo "The registry has no ${SIGNED_REF}:" >&2
    printf '%s\n' "${INFO}" >&2
    exit 1
}
VERSION="$(printf '%s' "${INFO}" | python3 -c \
    'import json, sys; print(json.load(sys.stdin).get("Labels", {}).get("nexus.version", ""))' \
    2>/dev/null)"
case "${VERSION}" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "No nexus.version label on ${SIGNED_REF} (got '${VERSION}')." >&2; exit 1 ;;
esac
case "${VERSION}" in
    *[!0-9.]*) echo "nexus.version '${VERSION}' is not a plain version." >&2; exit 1 ;;
esac

echo "Image:    ${SIGNED_REF}"
echo "Version:  ${VERSION} (${PROFILE})"
echo

AUTHDIR="$(mktemp -d)"
trap 'rm -rf "${AUTHDIR}"' EXIT
ln -s "${AUTH}" "${AUTHDIR}/config.json"

# Retried, as in publish.sh: one timeout (25 September) is not a
# reason to stop.
signed=0
for attempt in 1 2 3; do
    if DOCKER_CONFIG="${AUTHDIR}" cosign sign --yes --tlog-upload=false \
            --key "${COSIGN_KEY}" "${SIGNED_REF}"; then
        signed=1
        break
    fi
    echo "Signing attempt ${attempt} failed; trying again in 10 seconds." >&2
    sleep 10
done
if [ "${signed}" -ne 1 ]; then
    echo "Not signed; no tag was moved, so machines see nothing new." >&2
    exit 1
fi

if ! DOCKER_CONFIG="${AUTHDIR}" cosign verify --insecure-ignore-tlog=true \
        --key "${PUBLIC_KEY}" "${SIGNED_REF}" > /dev/null; then
    echo "Signed, but the signature does not verify with ${PUBLIC_KEY}." >&2
    echo "No tag was moved." >&2
    exit 1
fi
echo "Signed and verified."
echo

# Now the tags machines follow. A copy inside one repository uploads
# no layers: the registry already has them, so this is the manifest
# only. --preserve-digests refuses rather than writing a different
# manifest, and the digest is read back anyway: a signature belongs to
# a digest, and a tag pointing anywhere else would be refused by every
# machine.
for tag in "${PROFILE}" "${PROFILE}-${VERSION}"; do
    moved=0
    for attempt in 1 2 3; do
        if skopeo copy --quiet --preserve-digests \
                "docker://${SIGNED_REF}" "docker://${REPO}:${tag}"; then
            moved=1
            break
        fi
        echo "Moving ${tag} failed (try ${attempt}); again in 10 seconds." >&2
        sleep 10
    done
    now="$(skopeo inspect --no-tags --format '{{.Digest}}' "docker://${REPO}:${tag}" 2>/dev/null)"
    if [ "${moved}" -ne 1 ] || [ "${now}" != "${DIGEST}" ]; then
        echo "${REPO}:${tag} points at '${now}', not ${DIGEST}." >&2
        echo "The image is signed; run this again to move the tag." >&2
        exit 1
    fi
    echo "${REPO}:${tag} -> ${DIGEST}"
done

echo
echo "Released: machines following ${REPO}:${PROFILE} see ${VERSION} now."
