#!/usr/bin/env bash
#
# Can every add-on first boot offers actually be installed?
#
#     ./image/check-add-ons.sh                         localhost/nexus-os:kde
#     IMAGE=ghcr.io/.../nexus-core:kde ./image/check-add-ons.sh
#
# Runs Nexus inside the finished image, against the same repositories
# an installed machine has switched on (Fedora, RPM Fusion, Cisco's
# OpenH264), and asks it what each add-on would install. Only metadata
# is downloaded; nothing is installed and nothing leaves the image.
#
# Why this exists: a package name in a profile that no repository
# carries is invisible until somebody picks that add-on on first boot
# and gets "nothing available provides this". Names get renamed and
# retired between Fedora releases, and RPM Fusion drops things. The
# weekly build now finds out before anybody installs it, and does not
# publish.
#
# What is not checked here:
#   - Flatpak applications (Flathub is not asked; the line says
#     "(Flatpak, from Flathub)" and is taken as it stands);
#   - hardware requirements (gpu-vendor-*): a container has no GPU.

set -uo pipefail

IMAGE="${IMAGE:-${1:-localhost/nexus-os:kde}}"

if ! podman image exists "${IMAGE}"; then
    echo "No image ${IMAGE}." >&2
    exit 2
fi

echo "Checking the add-ons in ${IMAGE}"

# A script file rather than a heredoc on stdin: podman 3.4 (the Acer)
# drops stdin without -i and says nothing.
INNER="$(mktemp)"
trap 'rm -f "${INNER}"' EXIT

cat > "${INNER}" <<'INSIDE'
set -uo pipefail
export LC_ALL=C.UTF-8

if ! dnf -q makecache > /tmp/makecache.log 2>&1; then
    echo "PROBLEM: could not read the repositories:"
    tail -n 20 /tmp/makecache.log
    exit 3
fi

echo "Repositories:"
dnf -q repolist 2>/dev/null | sed 's/^/    /'
echo

problems=0
checked=0

# The privacy defaults (0.1.86, outside review round 3). They live in
# plain files, so a mistake in one shows only in the built image: this
# is where it is caught, before anything is published.
echo "Privacy defaults:"
policy=/etc/firefox/policies/policies.json
if [ ! -f "${policy}" ]; then
    problems=$((problems + 1))
    echo "PROBLEM: ${policy} is missing"
elif command -v python3 > /dev/null 2>&1 &&
     ! python3 -c 'import json, sys; p = json.load(open(sys.argv[1]))["policies"]; sys.exit(0 if p["DisableTelemetry"] is True and p["DisableFirefoxStudies"] is True else 1)' "${policy}"; then
    problems=$((problems + 1))
    echo "PROBLEM: ${policy} is not valid JSON or does not turn telemetry off"
elif ! grep -q '"DisableTelemetry": true' "${policy}"; then
    problems=$((problems + 1))
    echo "PROBLEM: ${policy} does not turn telemetry off"
else
    echo "    ok: Firefox telemetry, studies and ad measurement off"
fi

if command -v NetworkManager > /dev/null 2>&1; then
    nmconf="$(NetworkManager --print-config 2>&1)"
    if printf '%s\n' "${nmconf}" | grep -q 'unknown key.*nexus-privacy'; then
        problems=$((problems + 1))
        echo "PROBLEM: this NetworkManager does not accept 30-nexus-privacy.conf:"
        printf '%s\n' "${nmconf}" | grep 'unknown key' | sed 's/^/    /'
    elif ! printf '%s\n' "${nmconf}" | grep -q '^ipv4.dhcp-send-hostname=false' ||
         ! printf '%s\n' "${nmconf}" | grep -q '^ipv6.ip6-privacy=2'; then
        problems=$((problems + 1))
        echo "PROBLEM: NetworkManager's settings do not include 30-nexus-privacy.conf"
    else
        echo "    ok: no hostname sent over DHCP, IPv6 temporary addresses"
    fi
else
    echo "    (no NetworkManager in this image)"
fi

if [ "$(systemctl is-enabled rpm-ostree-countme.timer 2>/dev/null)" != masked ]; then
    problems=$((problems + 1))
    echo "PROBLEM: rpm-ostree-countme.timer is not masked"
elif grep -ls '^countme=1' /etc/yum.repos.d/*.repo; then
    problems=$((problems + 1))
    echo "PROBLEM: Count Me is still on in the files above"
else
    # What dnf5 itself will use, overrides included. Only a clear
    # "countme = 1" fails the check: output this script cannot read is
    # said so, not taken for a problem.
    effective="$(dnf --dump-repo-config fedora 2>/dev/null \
        | sed -n 's/^countme *= *//p' | head -n 1)"
    case "${effective}" in
        0) echo "    ok: Count Me off (dnf: countme = 0)" ;;
        1) problems=$((problems + 1))
           echo "PROBLEM: dnf would still send Count Me for the fedora repository" ;;
        *) echo "    ok: Count Me off in the files (dnf's own answer not readable here)" ;;
    esac
fi
echo

for file in /usr/share/nexus/profiles/*.profile; do
    name="$(basename "${file}" .profile)"
    [ "${name}" = base ] && continue

    checked=$((checked + 1))
    out="$(nexus setup "${name}" --with-available 2>&1)"

    # "[missing] <requirement>" -- one line per requirement that no
    # repository can fill. Hardware is not something to install.
    missing="$(printf '%s\n' "${out}" \
        | sed -n 's/^ *\[missing\] //p' | grep -v 'gpu-vendor')"

    # Names that exist can still be impossible to add: one that
    # conflicts with a package the image already has (mozilla-openh264
    # against the image's noopenh264, 26 September) resolves here and
    # fails on the machine. So the packages Nexus picked are handed to
    # dnf as one transaction and answered "no": resolved or refused,
    # nothing is downloaded or installed.
    packages="$(printf '%s\n' "${out}" \
        | awk '/^ *\[install\]/ {getline; if ($0 !~ /Flatpak/) print $1}' \
        | tr '\n' ' ')"
    refused=""

    if [ -n "${packages// /}" ]; then
        # shellcheck disable=SC2086
        answer="$(dnf install --assumeno ${packages} 2>&1)"
        if printf '%s\n' "${answer}" \
            | grep -qiE '^ *Problem|conflict|nothing provides|cannot install|No match for argument'; then
            refused="$(printf '%s\n' "${answer}" \
                | grep -iE 'Problem|conflict|nothing provides|cannot install|No match' | head -n 6)"
        fi
    fi

    if [ -n "${missing}" ]; then
        problems=$((problems + 1))
        echo "PROBLEM: ${name} cannot be installed. Nothing provides:"
        printf '%s\n' "${missing}" | sed 's/^/    /'
    elif [ -n "${refused}" ]; then
        problems=$((problems + 1))
        echo "PROBLEM: ${name} cannot be added to this image. dnf says:"
        printf '%s\n' "${refused}" | sed 's/^/    /'
    else
        echo "ok: ${name}"
        printf '%s\n' "${out}" | sed -n 's/^ \{12\}\([^ ].*\)$/        \1/p' \
            | grep -v 'nothing available provides this'
    fi
done

if [ "${checked}" -eq 0 ]; then
    echo "PROBLEM: no add-ons found in /usr/share/nexus/profiles"
    exit 1
fi

echo
if [ "${problems}" -gt 0 ]; then
    echo "${problems} add-on(s) would fail on first boot."
    exit 1
fi

echo "Add-ons: all ${checked} can be installed."
INSIDE

podman run --rm -v "${INNER}:/tmp/check.sh:ro,Z" --entrypoint bash \
    "${IMAGE}" /tmp/check.sh
