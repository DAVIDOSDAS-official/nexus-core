#!/usr/bin/env bash
#
# Turn a Nexus image into something you can install from.
#
# bootc-image-builder makes the installer, the same way it makes the
# qcow2. Writing one here would mean reimplementing partitioning,
# which is the single most destructive thing a program can get wrong
# and is already solved.
#
#     ./image/build-installer.sh                 kde, an ISO
#     PROFILE=minimal ./image/build-installer.sh a lighter one
#     TYPE=qcow2 ./image/build-installer.sh      a disk to boot in a VM

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

PROFILE="${PROFILE:-kde}"
TYPE="${TYPE:-anaconda-iso}"
# A locally-built image cannot be updated from: bootc records where
# it was pulled, and localhost is nowhere a machine can reach. Pass
# IMAGE=ghcr.io/you/nexus-core:profile to build an ISO whose installs
# can receive updates.
IMAGE="${IMAGE:-localhost/nexus-os:${PROFILE}}"
OUTPUT="${OUTPUT:-${ROOT}/output}"

BUILDER="quay.io/centos-bootc/bootc-image-builder:latest"

# An ISO anybody can download must not carry a known account; a disk
# image booted in a VM has nobody to ask.
if [ "${TYPE}" = "anaconda-iso" ]; then
    CONFIG="config-installer.toml"
else
    CONFIG="config.toml"
fi

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

# Tools for putting Nexus branding into the ISO, checked before a build
# that takes a quarter of an hour rather than after it.
if [ "${TYPE}" = "anaconda-iso" ]; then
    missing=""
    for tool in xorriso cpio implantisomd5 mcopy mtype; do
        command -v "${tool}" > /dev/null 2>&1 || missing="${missing} ${tool}"
    done
    if [ -n "${missing}" ]; then
        echo "Missing:${missing}. Install them once:" >&2
        echo "    sudo apt install xorriso cpio isomd5sum mtools" >&2
        exit 1
    fi
fi

# An image from a registry is fetched fresh every time. Using whatever
# copy was already here built a 0.1.18 ISO on 29 September, a day after
# 0.1.21 was published.
#
# podman first. Old podman (3.4, on the Acer) cannot resume a download
# that breaks off, and on a connection that drops long downloads it
# never finishes (30 September: three tries, three failures). Then
# skopeo, which retries each piece on its own: the system's skopeo if
# it has --retry-times, otherwise skopeo as a container -- itself a
# small download, tried until it arrives. skopeo keeps what it has
# fetched, so a broken-off download carries on next time instead of
# starting over. PULL=no uses the copy already here.
fetch_with_skopeo() {
    # A fixed folder, kept when a try breaks off: skopeo's "oci:" folder
    # format skips every piece already downloaded, so each new try only
    # fetches what is still missing. (The "dir:" format used before
    # empties its folder first -- every try started from zero, and a
    # 45-minute download that broke at the end was all lost, 3 October.)
    # Pieces are saved under a temporary name and renamed only when
    # complete, so a broken-off piece is never mistaken for a whole one.
    local dir
    dir="/var/tmp/nexus-download/$(echo "${IMAGE}" | tr '/:' '__')"
    mkdir -p "${dir}"

    local skopeo=(skopeo) mount=""
    if command -v skopeo > /dev/null 2>&1 \
            && skopeo copy --help 2>&1 | grep -q -- '--retry-times'; then
        echo "Fetching with skopeo into ${dir}."
    else
        echo "Fetching with skopeo in a container into ${dir}."
        local got=no
        for _ in 1 2 3 4 5 6 7 8; do
            if podman pull quay.io/skopeo/stable:latest; then
                got=yes
                break
            fi
            sudo rm -rf /var/tmp/storage* 2> /dev/null || true
            sleep 5
        done
        [ "${got}" = yes ] || return 1
        skopeo=(podman run --rm -v "${dir}:/out" quay.io/skopeo/stable:latest)
        mount=/out
    fi
    local here="${mount:-${dir}}"

    local try done=no
    for try in $(seq 1 20); do
        rm -f "${dir}"/oci/oci-put-blob* 2> /dev/null || true
        if "${skopeo[@]}" copy --retry-times 10 \
                "docker://${IMAGE}" "oci:${here}/oci:img"; then
            done=yes
            break
        fi
        echo "Download broke off (try ${try} of 20). The finished pieces" >&2
        echo "are kept; trying again for the rest in 15 seconds." >&2
        sleep 15
    done
    if [ "${done}" != yes ]; then
        echo "Still incomplete. Running this again carries on from" >&2
        echo "${dir} instead of starting over." >&2
        return 1
    fi

    # podman of this age cannot read the "oci:" folder, so it is turned
    # into a "dir:" folder here, on this disk (no download), and loaded.
    rm -rf "${dir}/img"
    "${skopeo[@]}" copy "oci:${here}/oci:img" "dir:${here}/img" || return 1

    # Only the folder's own name is removed afterwards. (podman untag
    # with no name removes every name, and an image with no name is
    # deleted by the next prune -- which is how 0.1.22 was lost once.)
    local id status=0
    id="$(podman pull -q "dir:${dir}/img")" \
        && podman tag "${id}" "${IMAGE}" || status=1
    podman untag "${id}" "localhost${dir}/img" > /dev/null 2>&1 || true
    [ "${status}" = 0 ] && sudo rm -rf "${dir}"
    return "${status}"
}

if [ "${PULL:-yes}" != no ] && [ "${IMAGE#localhost/}" = "${IMAGE}" ]; then
    echo "Fetching the latest ${IMAGE}."
    # A download skopeo left half done goes straight back to skopeo,
    # which carries on from it; podman would start over.
    if [ -d "/var/tmp/nexus-download/$(echo "${IMAGE}" | tr '/:' '__')/oci" ]; then
        echo "Carrying on with the download that broke off last time."
        pulled=no
    elif podman pull "${IMAGE}"; then
        pulled=yes
    else
        echo "podman's download broke off; switching to skopeo." >&2
        sudo rm -rf /var/tmp/storage* 2> /dev/null || true
        pulled=no
    fi
    if [ "${pulled}" = no ]; then
        if ! fetch_with_skopeo; then
            echo >&2
            echo "Could not fetch ${IMAGE}, even with retries. Nothing was" >&2
            echo "built: the copy here, if any, may be old. Try again later," >&2
            echo "or run this with PULL=no to use the copy here anyway." >&2
            exit 1
        fi
    fi
    echo "Image: $(podman run --rm "${IMAGE}" nexus --version 2> /dev/null || echo 'nexus version unknown')"
fi

if ! podman image exists "${IMAGE}"; then
    echo "No image ${IMAGE}. Build it first:" >&2
    echo "    podman build --target desktop \\" >&2
    echo "        --build-arg NEXUS_PROFILE=${PROFILE} \\" >&2
    echo "        -t ${IMAGE} -f image/Containerfile ." >&2
    exit 1
fi

# A previous ISO in the way.
#
# The builder writes into output/ without clearing it, so a failed
# build leaves the last good ISO sitting next to a broken one with no
# way to tell them apart by looking. Moved rather than deleted: it is
# several gigabytes somebody may have spent fifteen minutes on.
if [ -e "${OUTPUT}/bootiso" ] || [ -e "${OUTPUT}/qcow2" ]; then
    previous="${OUTPUT}.previous"
    echo "Moving the last build to ${previous}."
    sudo rm -rf "${previous}"
    sudo mv "${OUTPUT}" "${previous}"
fi

mkdir -p "${OUTPUT}"

# The builder runs as root and reads the image from root's storage,
# so an image built rootless has to be handed over first.
echo "Handing the image to root's storage."
podman save "${IMAGE}" | sudo podman load

echo
echo "Building. This takes a while."
echo

# The installer config names the image it installs, for the %post that
# makes installed machines require signed updates. A localhost image is
# never signed and never updated, so for it the %post is left out
# rather than written to fail.
RENDERED="$(mktemp --suffix=.toml)"

if [ "${IMAGE#localhost/}" != "${IMAGE}" ]; then
    echo "Local image: installs will not require signed updates."
    sed '/^%post/,/^%end/d' "${ROOT}/image/${CONFIG}" > "${RENDERED}"
else
    sed "s|@NEXUS_IMAGE@|${IMAGE}|" "${ROOT}/image/${CONFIG}" > "${RENDERED}"
fi

if grep -q '@NEXUS_IMAGE@' "${RENDERED}"; then
    echo "The installer config still has a placeholder; refusing." >&2
    exit 1
fi

chmod 0644 "${RENDERED}"

sudo podman run --rm -it --privileged \
    --security-opt label=type:unconfined_t \
    -v "${OUTPUT}":/output \
    -v "${RENDERED}":/config.toml:ro \
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

# Nexus branding in the installer.
#
# bootc-image-builder assembles the installer from Fedora's packages,
# so every screen of it showed Fedora's logo -- on an ISO that is not
# Fedora, which Fedora's trademark guidelines do not allow. Anaconda
# has a mechanism made for this: images/product.img on the ISO is laid
# over the installer's files when it starts. The pictures come from the
# image being installed (image/installer-branding.py draws them), so
# the ISO and the system cannot disagree.
#
# If the ISO already has a product.img it is merged, not replaced.
# The boot setup is replayed as it was, the volume label is kept (the
# installer finds itself by that label), and the media checksum is
# written again so the installer's own media check still passes.
if [ "${TYPE}" = "anaconda-iso" ]; then
    ISO="${OUTPUT}/bootiso/install.iso"
    WORK="$(mktemp -d)"
    mkdir -p "${WORK}/root"

    echo
    echo "Branding the installer."

    if ! podman run --rm --entrypoint cat "${IMAGE}" \
            /usr/share/nexus/installer/product.img > "${WORK}/ours.img" \
       || ! gzip -t "${WORK}/ours.img" 2> /dev/null; then
        echo "The image carries no installer branding" >&2
        echo "(/usr/share/nexus/installer/product.img). The ISO is built" >&2
        echo "but still shows Fedora's logo: do not publish it." >&2
        exit 1
    fi

    if xorriso -osirrox on -indev "${ISO}" \
            -extract /images/product.img "${WORK}/theirs.img" \
            > /dev/null 2>&1 && [ -s "${WORK}/theirs.img" ]; then
        if ! gzip -t "${WORK}/theirs.img" 2> /dev/null; then
            echo "The ISO has a product.img that is not a gzip cpio" >&2
            echo "archive; not merging blindly. The ISO still shows" >&2
            echo "Fedora's logo: do not publish it." >&2
            exit 1
        fi
        echo "Merging with the product.img the builder made."
        (cd "${WORK}/root" && gzip -dc ../theirs.img | cpio -idm --quiet)
    fi

    (cd "${WORK}/root" && gzip -dc ../ours.img | cpio -idmu --quiet)
    (cd "${WORK}/root" && find . | cpio -o -H newc --quiet | gzip -9) \
        > "${WORK}/product.img"

    # The boot text. Every menu entry already has `quiet`, and the
    # installer still scrolled two screens of systemd's "[ OK ]" lines
    # before its first window (filmed in the VM, 1 October): that list
    # is systemd's, not the kernel's. show_status=error hides it and
    # still prints a line when something fails. (Not "auto": auto turns
    # the whole list back on once boot is slow, and the installer always
    # is -- it loads its image from the disc first. The 0.1.28 test ISO
    # with "auto" showed the full list in the VM, 2 October.)
    # Not on the troubleshooting entries
    # (basic graphics, rescue): there the text is the point.
    #
    # Three copies of the menu: /EFI/BOOT/grub.cfg and
    # /boot/grub2/grub.cfg on the disc, and the one UEFI machines read,
    # inside images/efiboot.img (a small FAT image). All three, or the
    # change does nothing on most PCs.
    #
    # efiboot.img is not replaced like the other two: replacing it makes
    # xorriso drop the UEFI boot record ("Cannot enable El Torito boot
    # image ... not a data file"), which is an ISO that no longer boots
    # on UEFI -- found testing this on 2 October. Its menu is edited in
    # place in the finished ISO instead (mtools at the file's offset),
    # so not one byte of the boot setup moves. See below.
    QUIET="systemd.show_status=error rd.systemd.show_status=error"
    quiet_menu() {
        sed -i -E "/^[[:space:]]*linux .*inst\.stage2=/{/nomodeset|inst\.rescue|show_status/!s/\$/ ${QUIET}/}" "$1"
    }
    MAPS=()
    mkdir -p "${WORK}/boot"
    for menu in /EFI/BOOT/grub.cfg /boot/grub2/grub.cfg; do
        local_copy="${WORK}/boot/$(echo "${menu}" | tr '/' '_')"
        if xorriso -osirrox on -indev "${ISO}" -extract "${menu}" \
                "${local_copy}" > /dev/null 2>&1 && [ -s "${local_copy}" ]; then
            chmod u+w "${local_copy}"
            quiet_menu "${local_copy}"
            MAPS+=(-map "${local_copy}" "${menu}")
        fi
    done

    if ! xorriso -indev "${ISO}" -outdev "${ISO}.branded" \
            -map "${WORK}/product.img" /images/product.img \
            "${MAPS[@]}" \
            -boot_image any replay > "${WORK}/xorriso.log" 2>&1; then
        cat "${WORK}/xorriso.log" >&2
        rm -f "${ISO}.branded"
        echo "Could not write the branded ISO. The unbranded one is" >&2
        echo "still there, and still shows Fedora's logo." >&2
        exit 1
    fi

    # The UEFI menu, in place. UEFI machines do not read the
    # images/efiboot.img file in the disc's file list: Fedora 44's ISO
    # carries a second copy of it as a partition after the files, and
    # both the UEFI boot record and the disk's partition table point
    # there (osbuild's xorrisofs stage, -append_partition 2). xorriso
    # copies that partition across as it is. So the menu is edited
    # where the UEFI boot record points, in the finished ISO -- which
    # is also the right place on discs that do use the file.
    # (Patch 49 edited the file copy: counted 3 of 3, changed nothing
    # UEFI reads, and its check looked for the file name in the boot
    # record, which this ISO does not have, so it refused a good ISO.
    # 3 October.)
    uefi_images() {
        xorriso -indev "$1" -report_el_torito plain 2> /dev/null \
            | awk '/^El Torito boot img/ && $7 == "UEFI" {n++} END {print n+0}'
    }
    quieted=$(( ${#MAPS[@]} / 3 ))
    lba="$(xorriso -indev "${ISO}.branded" -report_el_torito plain 2> /dev/null \
        | awk '/^El Torito boot img/ && $7 == "UEFI" {print $NF; exit}')"
    if [ -n "${lba}" ]; then
        efi="${ISO}.branded@@$(( lba * 2048 ))"
        if mtype -i "${efi}" ::/EFI/BOOT/grub.cfg > "${WORK}/efi-inner.cfg" \
                2> /dev/null && grep -q 'inst.stage2=' "${WORK}/efi-inner.cfg"; then
            quiet_menu "${WORK}/efi-inner.cfg"
            if mcopy -o -i "${efi}" "${WORK}/efi-inner.cfg" ::/EFI/BOOT/grub.cfg \
                    && mtype -i "${efi}" ::/EFI/BOOT/grub.cfg \
                       | grep -q 'show_status=error'; then
                quieted=$(( quieted + 1 ))
            fi
        fi
    fi
    echo "Boot menus made quiet: ${quieted} of 3."

    # The boot record must be what it was: as many UEFI boot entries as
    # the ISO osbuild made. If anything above lost one, the ISO would
    # not start on UEFI machines: refuse it.
    before="$(uefi_images "${ISO}")"
    after="$(uefi_images "${ISO}.branded")"
    if [ "${before}" = 0 ] || [ "${after}" != "${before}" ]; then
        rm -f "${ISO}.branded"
        echo "The branded ISO lost its UEFI boot record (UEFI entries:" >&2
        echo "${before} before, ${after} after); not keeping it." >&2
        echo "The unbranded one is still there." >&2
        exit 1
    fi

    implantisomd5 --force "${ISO}.branded" > /dev/null
    mv "${ISO}.branded" "${ISO}"
    rm -rf "${WORK}"
    echo "Installer branded: images/product.img"
fi

# A checksum to publish next to the download.
if [ -f "${OUTPUT}/bootiso/install.iso" ]; then
    (cd "${OUTPUT}/bootiso" && sha256sum install.iso > install.iso.sha256)
fi

echo
echo "Done:"
find "${OUTPUT}" -type f \( -name '*.iso' -o -name '*.qcow2' \) \
    -exec ls -lh {} \;

# What this ISO actually is.
#
# An ISO is an opaque few gigabytes. Two of them from the same week
# look identical and can differ in the base Fedora, the Nexus commit,
# and the version of an archived tool that assembled them -- and once
# one is downloaded by somebody else, none of that is recoverable by
# looking at it.
#
# Digests rather than tags, because a tag is a name and names move.
# 'bootc-image-builder:latest' in particular is an unpinned tag on a
# project archived in June 2026: whatever it means today, it is not a
# promise. Recording what it meant for this build is what makes the
# ISO reproducible later, and is the first half of pinning it.
MANIFEST="${OUTPUT}/nexus-build.txt"

{
    echo "built:          $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "profile:        ${PROFILE}"
    echo "type:           ${TYPE}"
    echo "image:          ${IMAGE}"
    echo "image digest:   $(podman image inspect --format '{{.Digest}}' \
        "${IMAGE}" 2>/dev/null || echo unknown)"
    echo "nexus version:  $(podman run --rm "${IMAGE}" \
        nexus --version 2>/dev/null || echo unknown)"
    echo "builder:        ${BUILDER}"
    echo "builder digest: $(sudo podman image inspect \
        --format '{{.Digest}}' "${BUILDER}" 2>/dev/null || echo unknown)"
    if [ -f "${OUTPUT}/bootiso/install.iso.sha256" ]; then
        echo "iso sha256:     $(cut -d' ' -f1 "${OUTPUT}/bootiso/install.iso.sha256")"
    fi
} > "${MANIFEST}"

echo
echo "What went into it (${MANIFEST}):"
sed 's/^/    /' "${MANIFEST}"

# Reclaim what this build cost.
#
# Every iteration leaves four things behind: the new image in rootless
# storage, a second copy in root's storage from the save/load above,
# the previous image dangling in both, and the previous output/. That
# is 8-11 GB per build, and nothing removed any of it -- which is how
# a laptop runs out of disk half way through the build after the one
# that worked.
#
# The copy in root's storage is the one to take: it exists only so the
# builder could read it, the build is over, and the rootless copy is
# the one the next build reads. Dangling images go too; tagged ones
# never do.
#
# CLEAN=no to keep everything, when comparing two builds.
if [ "${CLEAN:-yes}" = "yes" ]; then
    echo
    echo "Clearing the build's leftovers. CLEAN=no keeps them."

    sudo podman rmi "${IMAGE}" > /dev/null 2>&1 || true
    sudo podman image prune -f > /dev/null 2>&1 || true
    podman image prune -f > /dev/null 2>&1 || true
    sudo rm -rf "${OUTPUT}.previous"

    echo "Free now: $(df -h --output=avail "${OUTPUT}" \
        | tail -1 | tr -d ' ')"
fi

if [ "${IMAGE#localhost/}" != "${IMAGE}" ]; then
    cat <<'LOCAL'

Note: this was built from a local image, so machines installed from
it have nowhere to update from. bootc pulls from where it was
installed; localhost is not reachable.

To make updates possible, publish the image and build from that name:

    ./image/publish.sh
    IMAGE=ghcr.io/davidosdas-official/nexus-core:PROFILE \
        ./image/build-installer.sh
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
