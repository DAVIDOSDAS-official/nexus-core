#!/usr/bin/env bash
#
# The rule everything else rests on: nothing changes the system without
# --apply. This runs every command that can change something WITHOUT
# --apply, and the commands that only ever read, and checks what each
# one did.
#
#     tests/no-apply-changes-nothing.sh build/cli/nexus
#     tests/no-apply-changes-nothing.sh --self-test
#
# WHAT IT VERIFIES depends on whether strace is installed, and it says
# at the end which of the two it was (0.1.88, review of patch 113: the
# first version of this test claimed more than it checked).
#
# With strace (the full check). Every command runs under
# `strace -f`, so the test sees, for nexus and everything it starts:
#   - every program that was run, by its full path. Each must be a
#     stand-in (below), nexus itself, or on the list of programs that
#     only read (tests/no-apply-trace.py). A system tool run by its own
#     path, past its stand-in, is a failure. So is a program nobody has
#     classified: the list is the claim.
#   - every file opened for writing, created, renamed, removed or
#     re-permissioned, anywhere -- /etc, /sys, /proc, /var, anywhere.
#     Allowed: /dev/null and the terminal, ~/.cache, and nexus's own
#     scratch folder in /tmp, which must be gone afterwards. An attempt
#     that failed counts too: as an ordinary user a write to /etc is
#     refused, and the attempt is the bug.
#   - system calls that are a change in themselves: mount, reboot,
#     loading a kernel module, setting the clock or the hostname, swap.
#
# Without strace (the reduced check):
#   - stand-ins: no tool that changes a system was asked to change
#     anything. The stand-ins come first in PATH, record how they are
#     called and do nothing.
#   - files: nothing changed in the fake home folder (apart from
#     ~/.cache) or in the folders listed in WATCHED below, and no
#     nexus-* scratch was left in /tmp.
#   NOT checked in this mode, and the last line says so: programs run
#   by their own path instead of through PATH; writes anywhere outside
#   WATCHED, /sys and /proc included; tools that have no stand-in.
#
# In both modes: the nexus program is searched for the full paths of
# tools that have a stand-in (a path written into the program is how a
# stand-in would be bypassed), and the stand-ins' records are checked.
#
# What it cannot verify in either mode: what the commands do WITH
# --apply; paths through the code that these 70-odd command lines do
# not reach; the network (curl, wget and skopeo are stand-ins, so
# nothing is fetched, and nothing else is watched for connections);
# requests made over D-Bus rather than by running a program.
#
# Safe on any machine: the stand-ins replace every tool that could
# change something, and nexus is never given --apply. Still, it refuses
# to run as root unless told to (NEXUS_TEST_ALLOW_ROOT=1, for a
# throwaway container), because as root a bug it is looking for would
# be a real change.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
TRACE_PY="${HERE}/no-apply-trace.py"

SELF_TEST=0
if [ "${1:-}" = "--self-test" ]; then
    SELF_TEST=1
    shift
fi

NEXUS="${1:-}"
if [ "${SELF_TEST}" -eq 0 ]; then
    if [ -z "${NEXUS}" ] || [ ! -x "${NEXUS}" ]; then
        echo "Use: $0 <path to the nexus program>   or   $0 --self-test" >&2
        exit 2
    fi
    NEXUS="$(cd "$(dirname "${NEXUS}")" && pwd)/$(basename "${NEXUS}")"
fi

if [ "$(id -u)" -eq 0 ] && [ "${NEXUS_TEST_ALLOW_ROOT:-0}" != 1 ]; then
    echo "SKIPPED: not run as root (set NEXUS_TEST_ALLOW_ROOT=1 in a throwaway container)."
    exit 77
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
STUBS="${WORK}/stubs"
CALLS="${WORK}/calls.log"
FAKEHOME="${WORK}/home"
SITE="${WORK}/site"
CWD="${WORK}/cwd"
# ~/.cache exists from the start, so that something appearing in it
# does not show as a change to the home folder itself.
mkdir -p "${STUBS}" "${FAKEHOME}/.cache" "${SITE}" "${CWD}"
echo "<h1>hello</h1>" > "${SITE}/index.html"
: > "${CALLS}"

# ------------------------------------------------------------ stand-ins
# ALWAYS: tools a preview has no business running at all. Any call is a
# failure.
ALWAYS="shutdown reboot poweroff halt kexec
    useradd usermod userdel groupadd groupdel groupmod gpasswd passwd chpasswd chsh chfn visudo
    sudo pkexec runuser su doas
    parted sfdisk sgdisk fdisk gdisk wipefs mkswap swapon swapoff dd losetup
    mkfs mkfs.ext4 mkfs.btrfs mkfs.vfat mkfs.fat mkfs.xfs mkfs.f2fs mkfs.ntfs mkfs.exfat
    cryptsetup resize2fs ntfsresize xfs_growfs e2fsck fsck tune2fs
    pvcreate vgcreate lvcreate lvresize lvremove vgremove pvremove
    grub2-reboot grub2-set-default grub2-install grub-install grub-reboot grub-set-default
    update-grub dracut mkinitcpio kernel-install depmod ldconfig
    setenforce setsebool semanage restorecon chcon setcap chattr authselect
    iptables ip6tables iptables-restore ip6tables-restore
    wg-quick modprobe insmod rmmod
    update-alternatives alternatives systemd-sysusers
    crontab at ssh-keygen
    distrobox-export toolbox
    pip pip3 npm cargo rustup gem go
    docker docker-compose podman-compose buildah
    pacman zypper emerge apk"

# PATTERNED: tools nexus also uses to look. Only a call that matches
# the tool's pattern in CHANGES is a failure.
PATTERNED="systemctl rpm-ostree bootc ostree flatpak flatpak-builder firewall-cmd ufw nft
    podman distrobox dnf dnf5 apt apt-get snap nix nix-env sysctl
    grub2-editenv grub2-mkconfig grub-mkconfig efibootmgr mokutil sbctl bootctl bootupctl
    mount umount tar timedatectl hostnamectl localectl hwclock chronyc
    wg nmcli resolvectl mullvad rfkill powerprofilesctl brightnessctl
    loginctl journalctl systemd-run systemd-tmpfiles
    gsettings dconf kwriteconfig5 kwriteconfig6 xdg-settings
    btrfs lvm zramctl fwupdmgr gpg git
    skopeo curl wget cosign notify-send"

for tool in ${ALWAYS} ${PATTERNED}; do
    cat > "${STUBS}/${tool}" <<EOF
#!/bin/sh
printf '%s' '${tool}' >> '${CALLS}'
for a in "\$@"; do printf ' %s' "\$a" >> '${CALLS}'; done
printf '\n' >> '${CALLS}'
exit 0
EOF
    chmod +x "${STUBS}/${tool}"
done

# What counts as asking for a change. Reading (status, list, info,
# --state, is-active, a read-only mount) is not in here.
CHANGES='^systemctl .*(\b(start|stop|restart|reload|try-restart|reload-or-restart|enable|disable|reenable|mask|unmask|link|daemon-reload|daemon-reexec|reboot|poweroff|halt|suspend|hibernate|kexec|soft-reboot|set-default|set-property|set-environment|unset-environment|import-environment|kill|isolate|edit|revert|preset|preset-all|reset-failed|clean|freeze|thaw|switch-root|emergency|rescue|default)\b)'
CHANGES="${CHANGES}"'|^rpm-ostree (install|uninstall|upgrade|update|rebase|rollback|kargs|override|cleanup|reset|initramfs|initramfs-etc|deploy|cancel|apply-live|refresh-md|compose|finalize-deployment|reload|usroverlay)\b'
CHANGES="${CHANGES}"'|^bootc (upgrade|update|switch|rollback|install|edit|usr-overlay)\b'
CHANGES="${CHANGES}"'|^ostree admin (deploy|undeploy|set-default|pin|unlock|upgrade|switch|cleanup|set-origin|config-diff|kargs)\b|^ostree (commit|pull|pull-local|prune|reset|remote (add|delete))\b'
CHANGES="${CHANGES}"'|^flatpak (install|uninstall|remove|remote-add|remote-delete|remote-modify|override|repair|mask|pin|make-current|config|run|kill|permission-(remove|set|reset)|document-(export|unexport))\b|^flatpak-builder '
CHANGES="${CHANGES}"'|^firewall-cmd .*--(add-|remove-|reload|complete-reload|set-|change-|panic-o|runtime-to-permanent|new-|delete-|load-|lockdown-o|reset-to-defaults)'
CHANGES="${CHANGES}"'|^ufw (enable|disable|allow|deny|reject|limit|delete|insert|reset|default|reload|logging|route)\b'
CHANGES="${CHANGES}"'|^nft (add|delete|flush|insert|replace|create|rename|reset|-f|--file)\b'
CHANGES="${CHANGES}"'|^podman (pull|run|create|rm|rmi|stop|start|restart|kill|pause|unpause|load|import|build|login|logout|push|tag|untag|commit|exec|cp|rename|update|generate|play|kube|system (prune|reset|migrate|renumber)|volume (create|rm|prune|import)|network (create|rm|prune|connect|disconnect)|image (rm|prune|pull|push|load|import|tag|untag|build)|container (rm|prune|create|run|start|stop|kill|restart|exec|cp|rename)|pod (create|rm|start|stop|kill|restart|prune)|secret (create|rm))\b'
CHANGES="${CHANGES}"'|^distrobox (create|rm|enter|upgrade|stop|assemble|generate-entry|ephemeral)\b'
CHANGES="${CHANGES}"'|^(dnf|dnf5) .*\b(install|remove|erase|upgrade|update|swap|reinstall|downgrade|autoremove|distro-sync|copr|config-manager|group|module|mark|versionlock|system-upgrade|offline|history (undo|redo|rollback|replay)|clean)\b'
CHANGES="${CHANGES}"'|^(apt|apt-get) .*\b(install|remove|purge|upgrade|full-upgrade|dist-upgrade|autoremove|autopurge|update|clean|autoclean|source|build-dep|reinstall|satisfy)\b'
CHANGES="${CHANGES}"'|^snap (install|remove|refresh|revert|enable|disable|set|unset|connect|disconnect|alias|unalias|switch|start|stop|restart|run|try|ack)\b'
CHANGES="${CHANGES}"'|^nix-env .*(-i|-e|-u|--install|--uninstall|--upgrade|--set|--rollback|--switch|--delete-generations)|^nix (profile (install|remove|upgrade|rollback|wipe-history)|build|run|develop|shell|store (delete|gc|optimise)|upgrade-nix|registry (add|remove|pin))\b'
CHANGES="${CHANGES}"'|^sysctl .*(-w|--write|--system|-p|--load|=)'
CHANGES="${CHANGES}"'|^grub2-editenv .*\b(set|unset|create)\b|^(grub2|grub)-mkconfig .*(-o|--output)'
CHANGES="${CHANGES}"'|^efibootmgr .*( -[cCBboOnNaAtTD]\b| --(create|create-only|delete|delete-bootnum|bootorder|delete-bootorder|bootnext|delete-bootnext|active|inactive|timeout|delete-timeout|remove-dups))'
CHANGES="${CHANGES}"'|^mokutil .*--(import|delete|reset|password|clear-password|enable-validation|disable-validation|revoke-import|revoke-delete|set-|trust-mok|untrust-mok|timeout)|^sbctl (create-keys|enroll-keys|sign|sign-all|remove-file|rotate-keys|reset|import-keys)\b|^bootctl (install|update|remove|set-default|set-oneshot|set-timeout|random-seed|cleanup|unlink)\b|^bootupctl (update|adopt-and-update|install|backend)\b'
CHANGES="${CHANGES}"'|^umount '
CHANGES="${CHANGES}"'|^tar .*(-[a-zA-Z]*[cxruA]|--create|--extract|--get|--append|--update|--delete|--catenate|--concatenate)'
CHANGES="${CHANGES}"'|^timedatectl .*set-|^hostnamectl .*(set-|hostname .|chassis .|deployment .|location .|icon-name .)|^localectl .*set-|^hwclock .*(-w|-s|--systohc|--hctosys|--set|--adjust|--update-drift)|^chronyc .*(makestep|burst|settime|reload|reset|add |delete |offline|online|dump|trimrtc|writertc|manual|local|smoothtime|maxdelay|minpoll|maxpoll|rekey|shutdown)'
CHANGES="${CHANGES}"'|^wg (set|setconf|addconf|syncconf)\b'
CHANGES="${CHANGES}"'|^nmcli .*\b(add|modify|mod|delete|del|up|down|connect|disconnect|reapply|import|load|reload|edit|clone|set|on|off|rescan|hotspot|migrate)\b'
CHANGES="${CHANGES}"'|^resolvectl (dns|domain|default-route|llmnr|mdns|dnsovertls|dnssec|nta|revert|flush-caches|reset-server-features|reset-statistics|log-level) .|^resolvectl (revert|flush-caches|reset-server-features|reset-statistics)\b'
CHANGES="${CHANGES}"'|^mullvad (connect|disconnect|reconnect|lockdown-mode set|auto-connect set|lan set|dns set|relay set|tunnel set|account (login|logout|create|redeem)|factory-reset|split-tunnel (add|delete|clear|set)|obfuscation set|api-access|bridge set|custom-list)\b'
CHANGES="${CHANGES}"'|^rfkill (block|unblock|toggle)\b|^powerprofilesctl (set|launch)\b|^brightnessctl .*\b(set|s)\b'
CHANGES="${CHANGES}"'|^loginctl .*\b(terminate-|kill-|lock-|unlock-|activate|enable-linger|disable-linger|attach|flush-devices)'
CHANGES="${CHANGES}"'|^journalctl .*--(vacuum-|rotate|flush|sync|relinquish-var|smart-relinquish-var|setup-keys|update-catalog)'
CHANGES="${CHANGES}"'|^systemd-run |^systemd-tmpfiles .*--(create|remove|clean|purge)'
CHANGES="${CHANGES}"'|^gsettings (set|reset|reset-recursively)\b|^dconf (write|reset|load|update|compile)\b|^kwriteconfig[56] |^xdg-settings set\b'
CHANGES="${CHANGES}"'|^btrfs (subvolume (create|delete|snapshot|set-default)|filesystem (resize|defragment|label .* .|mkswapfile)|balance|scrub (start|cancel|resume)|device (add|remove|delete)|property set|quota|qgroup (create|destroy|assign|limit)|send|receive|replace|rescue|check --repair|restore)\b|^lvm (pvcreate|vgcreate|lvcreate|lvresize|lvextend|lvreduce|lvremove|vgremove|pvremove|vgextend|vgreduce|lvrename|vgrename|lvchange|vgchange|pvchange|pvmove)\b|^zramctl .*(-f|--find|-r|--reset|-s|--size)'
CHANGES="${CHANGES}"'|^fwupdmgr (update|install|refresh|downgrade|reinstall|activate|unlock|clear-results|modify-|enable-|disable-|set-|security --force)\b'
CHANGES="${CHANGES}"'|^gpg .*--(import|gen-key|generate-key|full-generate-key|delete-|edit-key|sign|encrypt|decrypt|recv-keys|refresh-keys|send-keys|import-ownertrust)|^git (clone|pull|fetch|push|commit|add|rm|mv|reset|checkout|switch|merge|rebase|apply|am|init|config .* .|clean|stash|submodule (add|update|init))\b'
CHANGES="${CHANGES}"'|^skopeo (copy|delete|sync|login|logout)\b|^cosign (sign|attest|login|generate-key-pair|import-key-pair)\b'
CHANGES="${CHANGES}"'|^curl .*( -X ?(POST|PUT|DELETE|PATCH)| --request ?(POST|PUT|DELETE|PATCH)| -d | --data| -T | --upload-file| -F | --form)|^wget .*--(post-|method=(POST|PUT|DELETE|PATCH)|body-)'

# A rehearsal is not a change: the package manager is asked what it
# WOULD do (apt-get --dry-run, dnf --assumeno) so the preview can be
# checked against it.
REHEARSAL='^(apt|apt-get) .*(--dry-run|--simulate|--just-print|--no-act|--recon| -s )|^(dnf|dnf5) .*--assumeno|^rpm-ostree install .*--dry-run'

# A mount is a change unless it is read-only (nexus dualboot looks
# inside other systems' partitions read-only, with sudo, and says so).
mount_changes() {
    grep -E '^mount ' "${CALLS}" | grep -Ev '(^| )-r( |$)|--read-only|-o [^ ]*\bro\b' ; grep -E '^mount .*remount,rw' "${CALLS}"
}

stub_findings() {
    local tool
    for tool in ${ALWAYS}; do
        grep -E "^${tool//./\\.}( |\$)" "${CALLS}" | sed 's/^/asked a tool no preview should run: /'
    done
    grep -E "${CHANGES}" "${CALLS}" | grep -Ev "${REHEARSAL}" | sed 's/^/asked a tool to change something: /'
    mount_changes | sed 's/^/asked for a mount that is not read-only: /'
}

# ------------------------------------------------- the reduced file check
# Folders a preview could plausibly write to. Kept to places that do not
# change by themselves while the test runs: /var/log, /var/lib (other
# than nexus's own) and /run are written by the rest of the system all
# the time, and a snapshot cannot tell whose write it was. strace can;
# this cannot. Compared by time of last change, so a quiet folder costs
# little however large it is.
WATCHED="/etc /boot /efi /var/lib/nexus /usr/local /opt /srv
    /usr/lib/sysctl.d /usr/lib/systemd/system /usr/lib/tmpfiles.d
    /usr/lib/NetworkManager /usr/share/nexus"
# (Not /root: where this runs as root, in a container, other things
# write there too. nexus is given a home folder of its own, above.)

# (nexus's scratch folders are /tmp/nexus- and six characters, from
# mkdtemp; other things named nexus-... in /tmp are not its doing.)
# What changed since the marker was set: anything whose content,
# permissions or owner changed, was created, or (through its folder)
# was removed. Names, so a failure says what it was.
mark() {
    touch "${WORK}/marker"
    sleep 0.02          # file times are coarse; do not race the marker
    find /tmp /var/tmp -maxdepth 1 -name 'nexus-??????' 2>/dev/null | sort > "${WORK}/scratch.before"
}
changed_since_mark() {
    find "${FAKEHOME}" -path "${FAKEHOME}/.cache" -prune -o -newercm "${WORK}/marker" -print 2>/dev/null | head -n 5
    local dir
    for dir in ${WATCHED}; do
        [ -d "${dir}" ] && [ -r "${dir}" ] || continue
        find "${dir}" -xdev -newercm "${WORK}/marker" -print 2>/dev/null | head -n 5
    done
    # Scratch that a preview made and did not remove.
    find /tmp /var/tmp -maxdepth 1 -name 'nexus-??????' 2>/dev/null | sort \
        | comm -13 "${WORK}/scratch.before" - | sed 's/$/ (left behind)/'
}

# ---------------------------------------------------------------- strace
TRACE=0
TRACE_WHY="strace is not installed"
if command -v strace > /dev/null 2>&1 && command -v python3 > /dev/null 2>&1; then
    if strace -f -qq -o /dev/null true > /dev/null 2>&1; then
        TRACE=1
    else
        TRACE_WHY="strace is installed but may not trace here (ptrace is not allowed)"
    fi
elif command -v strace > /dev/null 2>&1; then
    TRACE_WHY="python3 is not installed (it reads strace's record)"
fi
[ "${NEXUS_TEST_NO_TRACE:-0}" = 1 ] && { TRACE=0; TRACE_WHY="tracing was turned off (NEXUS_TEST_NO_TRACE=1)"; }

CALLS_TRACED='execve,execveat,open,openat,?openat2,creat,unlink,unlinkat,rmdir,mkdir,mkdirat,rename,renameat,renameat2,link,linkat,symlink,symlinkat,chmod,fchmod,fchmodat,?fchmodat2,chown,lchown,fchown,fchownat,truncate,setxattr,lsetxattr,removexattr,lremovexattr,mknod,mknodat,mount,umount2,reboot,init_module,finit_module,delete_module,sethostname,setdomainname,settimeofday,clock_settime,?clock_adjtime,adjtimex,swapon,swapoff,kexec_load,?kexec_file_load,pivot_root,chroot,acct,quotactl'

# How long one command may take, in seconds. A build with the
# sanitizers on is many times slower (NEXUS_TEST_COMMAND_TIMEOUT=300
# in the Tests workflow).
LIMIT="${NEXUS_TEST_COMMAND_TIMEOUT:-60}"
case "${LIMIT}" in *[!0-9]* | "") LIMIT=60 ;; esac

# run_one <program> <words...>: runs it, sets PROBLEM (empty if clean)
SEEN_PROGRAMS="${WORK}/programs.txt"
SEEN_UNNAMED="${WORK}/unnamed.txt"
: > "${SEEN_UNNAMED}"
: > "${SEEN_PROGRAMS}"
run_one() {
    local program="$1"; shift
    : > "${CALLS}"
    rm -f "${WORK}/trace.txt"
    local code asked traced changed
    mark
    if [ "${TRACE}" -eq 1 ]; then
        # LeakSanitizer cannot work under a tracer; in a sanitizer build
        # the leak check is left to the run without strace.
        (cd "${CWD}" && env -i PATH="${STUBS}:/usr/sbin:/usr/bin:/sbin:/bin" HOME="${FAKEHOME}" \
            LANG=C.UTF-8 TERM=dumb ASAN_OPTIONS=detect_leaks=0 \
            strace -f -y -qq -s 512 -o "${WORK}/trace.txt" -e "trace=${CALLS_TRACED}" \
            timeout "${LIMIT}" "${program}" "$@" > "${WORK}/out.txt" 2>&1 < /dev/null)
        code=$?
    else
        (cd "${CWD}" && env -i PATH="${STUBS}:/usr/sbin:/usr/bin:/sbin:/bin" HOME="${FAKEHOME}" \
            LANG=C.UTF-8 TERM=dumb \
            timeout "${LIMIT}" "${program}" "$@" > "${WORK}/out.txt" 2>&1 < /dev/null)
        code=$?
    fi
    changed="$(changed_since_mark)"

    PROBLEM=""
    add_problem() { PROBLEM="${PROBLEM}${PROBLEM:+
}$1"; }

    asked="$(stub_findings)"
    [ -z "${asked}" ] || add_problem "${asked}"

    if [ "${TRACE}" -eq 1 ]; then
        traced="$(python3 -I "${TRACE_PY}" "${WORK}/trace.txt" --summary \
            --nexus "${program}" --stubs "${STUBS}" \
            --allow "${FAKEHOME}/.cache/" --allow "${WORK}/" --allow "/tmp/nexus-" \
            --allow "/var/cache/apt/" --allow "/var/lib/rpm/.rpm.lock" \
            --allow "/usr/lib/sysimage/rpm/.rpm.lock" 2>&1)"
        printf '%s\n' "${traced}" | grep '^SUMMARY ' | sed 's/^SUMMARY programs=//; s/ written=.*//' \
            | tr ',' '\n' >> "${SEEN_PROGRAMS}"
        # O_TMPFILE in /tmp: a file with no name, gone when closed. Not a
        # change, but shown at the end so it is never invisible.
        printf '%s\n' "${traced}" | grep '^SUMMARY ' | sed -n 's/.* unnamed=//p' | tr ';' '\n' \
            | grep -v '^$' | sed "s|^|nexus ${*:-}: |" >> "${SEEN_UNNAMED}" || true
        traced="$(printf '%s\n' "${traced}" | grep -v '^SUMMARY ')"
        [ -z "${traced}" ] || add_problem "${traced}"
        if [ ! -s "${WORK}/trace.txt" ]; then
            add_problem "strace recorded nothing for this command (the check did not happen)"
        fi
    fi

    if [ -n "${changed}" ]; then
        add_problem "changed on disk: $(printf '%s' "${changed}" | tr '\n' ' ')"
    fi
    case "${code}" in
        124|137) add_problem "did not finish in ${LIMIT} seconds (waiting for an answer?)" ;;
        134|139) add_problem "crashed (exit ${code})" ;;
    esac
    # In a build with the sanitizers on, a report is a failure even if
    # the command went on to finish.
    if grep -Eq 'ERROR: (Address|Leak)Sanitizer|runtime error:' "${WORK}/out.txt"; then
        add_problem "a sanitizer reported a problem: $(grep -E -m 1 'Sanitizer|runtime error:' "${WORK}/out.txt")"
    fi
}

# ---------------------------------------------------------- self-test
# Is the test itself any good? Small programs that each break the rule
# in one way; each must be caught. In the reduced mode some cannot be,
# and that is printed, not hidden.
if [ "${SELF_TEST}" -eq 1 ]; then
    MUT="${WORK}/mutants"
    mkdir -p "${MUT}/elsewhere"
    printf '#!/bin/sh\nexit 0\n' > "${MUT}/elsewhere/systemctl"; chmod +x "${MUT}/elsewhere/systemctl"
    make_mutant() { printf '#!/bin/sh\n%s\n' "$2" > "${MUT}/$1"; chmod +x "${MUT}/$1"; }
    make_mutant clean            'echo "Nothing has been changed."; cat /etc/hostname > /dev/null 2>&1; exit 0'
    make_mutant asks-systemctl   'systemctl daemon-reload'
    make_mutant asks-rpm-ostree  'rpm-ostree install vim'
    make_mutant runs-sudo        'sudo true'
    make_mutant mounts-rw        'mount -o rw /dev/sda1 /mnt'
    make_mutant writes-home      'mkdir -p "$HOME/.config/nexus" && echo x > "$HOME/.config/nexus/changed"'
    make_mutant leaves-scratch   'mkdir /tmp/nexus-sLfTsT'
    make_mutant by-its-own-path  "${MUT}/elsewhere/systemctl daemon-reload"
    make_mutant tries-etc        'echo x 2>/dev/null > /etc/nexus-selftest-should-not-exist || true; rm -f /etc/nexus-selftest-should-not-exist'
    make_mutant writes-sys       'echo 1 2>/dev/null > /proc/sys/kernel/sysrq-selftest-not-a-file || true'
    make_mutant writes-tmp       'echo x > /tmp/selftest-plain-file; rm -f /tmp/selftest-plain-file'
    make_mutant sed-in-place     'echo a > "$HOME/.cache/f"; sed -i s/a/b/ "$HOME/.cache/f"'
    make_mutant unknown-program  'cp /etc/hostname /dev/null'
    mkdir -p "${FAKEHOME}/.cache"

    # name, caught with strace, caught without
    wrong=0
    total=0
    got=0
    while read -r name full reduced; do
        run_one "${MUT}/${name}"
        rm -rf /tmp/nexus-sLfTsT
        caught=no; [ -n "${PROBLEM}" ] && caught=yes
        want="${reduced}"; [ "${TRACE}" -eq 1 ] && want="${full}"
        # "root": without strace, seen only when the write goes through,
        # which it does only as root.
        if [ "${want}" = root ]; then
            want=no; [ "$(id -u)" -eq 0 ] && want=yes
        fi
        total=$((total + 1)); [ "${caught}" = yes ] && got=$((got + 1))
        if [ "${caught}" != "${want}" ]; then
            wrong=$((wrong + 1))
            echo "FAILED: mutant '${name}': caught=${caught}, should be ${want}"
            [ -z "${PROBLEM}" ] || printf '%s\n' "${PROBLEM}" | sed 's/^/    /'
        elif [ "${TRACE}" -eq 0 ] && [ "${caught}" = no ] && [ "${full}" = yes ]; then
            echo "note: '${name}' is not caught without strace (as documented)"
        fi
    done <<EOF
clean            no  no
asks-systemctl   yes yes
asks-rpm-ostree  yes yes
runs-sudo        yes yes
mounts-rw        yes yes
writes-home      yes yes
leaves-scratch   yes yes
by-its-own-path  yes no
tries-etc        yes root
writes-sys       yes no
writes-tmp       yes no
sed-in-place     yes no
unknown-program  yes no
EOF
    if [ "${wrong}" -ne 0 ]; then
        echo "${wrong} mutant(s): the test did not behave as it says."
        exit 1
    fi
    ways=$((total - 1))   # all but the clean one
    if [ "${TRACE}" -eq 1 ]; then
        echo "ok: with strace, all ${ways} ways of breaking the rule were caught and the clean program passed"
    else
        echo "ok: reduced check (${TRACE_WHY}): ${got} of ${ways} ways of breaking the rule were caught, the others need strace, and the clean program passed"
    fi
    exit 0
fi

# ------------------------------------------- full paths inside the program
# A tool run as /usr/bin/systemctl never meets its stand-in. Does the
# program hold such a path for a tool that has a stand-in?
BUILT_IN_PATHS=""
if command -v strings > /dev/null 2>&1; then
    for tool in ${ALWAYS} ${PATTERNED}; do
        case "${tool}" in curl|wget|notify-send|git|gpg) continue ;; esac
        if strings -n 6 "${NEXUS}" | grep -Eq "(^|[\"' =:])/(usr/)?(s?bin|libexec)/${tool//./\\.}([\"' ]|\$)"; then
            BUILT_IN_PATHS="${BUILT_IN_PATHS} ${tool}"
        fi
    done
    STRINGS_CHECKED=1
else
    STRINGS_CHECKED=0
fi

# ------------------------------------------------------------- commands
# One per line. No --apply anywhere, on purpose -- including the ways
# someone might think --apply is implied.
COMMANDS="$(cat <<EOF
install firefox
install firefox --yes
install firefox -y
install firefox apply
install firefox --Apply
install firefox --apply-later
install vlc --with-flatpak
install vlc --commands
remove firefox
remove firefox --yes
setup gaming
setup media,development,vpn
setup cybersecurity --yes
setup hosting
update
update --yes
update notify off
update notify on
language add sr
language set sr_RS.UTF-8
server add minecraft --accept-eula
server add minecraft --accept-eula --port 25570 --memory 2G
server add website ${SITE}
server add website ${SITE} --port 8081
server add website ${SITE} --domain example.org
server remove minecraft
server backup minecraft
server cockpit
gaming
gaming split-lock
security harden
security harden off
dualboot add
dualboot remove
dualboot 1
rollback
rebase signed
rebase ghcr.io/davidosdas-official/nexus-core:kde
vpn keys
vpn config --address 10.0.0.2/32 --peer-key AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA= --endpoint example.org:51820
dev add python
dev add cpp rust
dev remove python
cyber create small
cyber create full --root
cyber remove
app install org.gnome.Calculator
app remove org.gnome.Calculator
container firefox
container firefox --from-distro arch
source firefox
plan web-browser
doctor
specs
security
history
scan
gaps
conflicts
services
hardware
secureboot
largest
dualboot
vpn
vpn check
server
language
language list
update --lines
update notify status
gamecheck 730
why bash
inspect bash
options web-browser
EOF
)"

failures=0
count=0
while IFS= read -r line; do
    [ -n "${line}" ] || continue
    count=$((count + 1))
    # shellcheck disable=SC2086  # the words of the command, split on purpose
    run_one "${NEXUS}" ${line}
    if [ -n "${PROBLEM}" ]; then
        failures=$((failures + 1))
        echo "FAILED: nexus ${line}"
        printf '%s\n' "${PROBLEM}" | sed 's/^/    /'
        echo "    what it printed (first lines):"
        head -n 4 "${WORK}/out.txt" | sed 's/^/        /'
    fi
done <<EOF
${COMMANDS}
EOF

if [ -n "${BUILT_IN_PATHS}" ]; then
    failures=$((failures + 1))
    echo "FAILED: the nexus program holds the full path of:${BUILT_IN_PATHS}"
    echo "    A tool run by its full path never meets its stand-in, so this test"
    echo "    would not see what it is asked to do."
fi

if [ "${failures}" -ne 0 ]; then
    echo
    echo "${failures} problem(s) in ${count} commands run without --apply."
    exit 1
fi

if [ "${TRACE}" -eq 1 ]; then
    programs="$(sort -u "${SEEN_PROGRAMS}" | grep -v '^$' | tr '\n' ' ')"
    echo "ok (full check, with strace): ${count} commands without --apply."
    echo "    Every program started was a stand-in, nexus, or a program known only to read: ${programs}"
    echo "    No file was opened for writing, created, renamed, removed or re-permissioned outside"
    echo "    /dev/null, ~/.cache and nexus's own scratch in /tmp -- attempts that failed included."
    echo "    No mount, reboot, kernel-module, clock, hostname or swap system call."
    echo "    No stand-in was asked to change anything."
    if [ -s "${SEEN_UNNAMED}" ]; then
        echo "    Unnamed temporary files (O_TMPFILE: no name, gone when closed; keeping one"
        echo "    would need linkat, which is checked) were opened by:"
        sort -u "${SEEN_UNNAMED}" | sed 's/^/        /'
    fi
else
    echo "ok (REDUCED check: ${TRACE_WHY}): ${count} commands without --apply."
    echo "    No stand-in was asked to change anything; nothing changed in the fake home folder"
    echo "    or in: $(echo ${WATCHED})"
    echo "    NOT checked: programs run by their own path, writes anywhere else (/sys and /proc"
    echo "    included), tools without a stand-in. Install strace for the full check."
fi
[ "${STRINGS_CHECKED}" -eq 1 ] ||
    echo "    (The search of the program for full tool paths was skipped: 'strings' is not installed.)"
exit 0
