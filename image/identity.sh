#!/bin/sh
#
# Make an image say what it is. Run once, at the end of every target
# stage in image/Containerfile, after the Nexus files are copied in.
#
# One script rather than the same RUN written into two stages: a
# Containerfile cannot include a block in more than one place, and
# two copies of this logic are two things that will drift apart.
#
# Kept in the image at /usr/libexec/nexus/identity. It is a few
# hundred bytes, and an image that carries the script that named it
# can be asked how it came to say what it says.

set -eu

# The version comes from the binary, not from a build argument.
#
# There is one place the version is written down: project(VERSION ...)
# in CMakeLists.txt, which the binary reports. Asking the binary means
# the system and the tool cannot disagree about what they are -- they
# did, on 23 September, when a build argument set os-release to 0.1.1
# and `nexus --version` in the same image still said 0.1.0.
BINARY_VERSION="$(/usr/bin/nexus --version 2>/dev/null | awk 'NR == 1 {print $2}')"

# Refuse rather than guess: an image that cannot say its own version
# is worse than one that does not build. Not merely non-empty -- a
# version. While this was being tested, a binary that printed its
# version wrongly had its next word, "(stub)", accepted as one, and the
# image called itself "Nexus-CORE (stub)".
case "${BINARY_VERSION}" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *)
        echo "The nexus binary reported '${BINARY_VERSION}', which is not a version." >&2
        echo "Expected the first line of 'nexus --version' to be: nexus X.Y.Z (commit)" >&2
        exit 1
        ;;
esac

# A version passed in is a claim to check, not a second source.
if [ -n "${NEXUS_VERSION:-}" ] && [ "${NEXUS_VERSION}" != "${BINARY_VERSION}" ]; then
    echo "Asked to build ${NEXUS_VERSION}, but the source says ${BINARY_VERSION}." >&2
    echo "Change project(VERSION ...) in CMakeLists.txt and commit;" >&2
    echo "the build argument is only a check against it." >&2
    exit 1
fi

NEXUS_VERSION="${BINARY_VERSION}"

# A global ARG declared after the first FROM in the Containerfile is
# not global, and arrives here empty. The result would be an image
# calling itself "Nexus-CORE 0.1.2 ()" -- built, installed and booted
# with nothing noticing. That exact mistake was made, and caught, while
# this file was first being written.
: "${NEXUS_PROFILE:?NEXUS_PROFILE is empty; check the ARG defaults in image/Containerfile}"

# The first thing a new machine does. See image/first-boot/README.md.
chmod 0755 /usr/bin/nexus-first-boot
systemctl enable nexus-first-boot.service

# What this system says it is.
#
# Without this an installed machine introduces itself as Fedora -- in
# /etc/os-release and to every tool that reads it. That is not a
# cosmetic gap: somebody debugging their own machine needs to know
# what they are running.
#
# ID and VERSION_ID stay Fedora's deliberately. Both are read by
# tooling that has to know what the base is: package managers branch
# on ID, and the image builder composes a distro name from ID plus
# VERSION_ID. Setting VERSION_ID to the Nexus version sent it looking
# for "fedora-0.1", which does not exist, and the ISO build failed at
# manifest generation.
#
# Branding goes in the fields meant for it. NAME, PRETTY_NAME and
# VARIANT are what a person reads; ID and VERSION_ID are what programs
# read, and a system that lies to programs about its base breaks
# things that were right to trust it.
#
# PLATFORM_ID is copied only when the base has one. Fedora 43 removed
# it, and under `set -u` referring to it killed the build on 44.
#
# The redirect writes through /usr/lib/os-release, which on Fedora is
# a symlink into os.release.d; replacing the link instead would leave
# the package that owns it disagreeing with the file.
. /etc/os-release

{
    echo "NAME=\"Nexus-CORE\""
    echo "PRETTY_NAME=\"Nexus-CORE ${NEXUS_VERSION} (${NEXUS_PROFILE})\""
    echo "ID=fedora"
    echo "ID_LIKE=fedora"
    echo "VERSION=\"${VERSION_ID} (Nexus-CORE ${NEXUS_VERSION})\""
    echo "VERSION_ID=${VERSION_ID}"
    echo "NEXUS_VERSION=\"${NEXUS_VERSION}\""
    echo "VARIANT=\"${NEXUS_PROFILE}\""
    echo "VARIANT_ID=${NEXUS_PROFILE}"
    echo "BUILD_ID=\"$(date -u +%Y%m%d)\""
    if [ -n "${PLATFORM_ID:-}" ]; then
        echo "PLATFORM_ID=\"${PLATFORM_ID}\""
    fi
    echo "ANSI_COLOR=\"0;38;2;249;115;22\""
    echo "NEXUS_BASE=\"${PRETTY_NAME}\""
} > /usr/lib/os-release

ln -sf ../usr/lib/os-release /etc/os-release

echo "Identity: Nexus-CORE ${NEXUS_VERSION} (${NEXUS_PROFILE}) on ${PRETTY_NAME}"
