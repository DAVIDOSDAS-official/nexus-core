#!/usr/bin/env bash
# Checks the rules of tests/no-apply-trace.py on a hand-written strace
# log (0.1.89, after the first run on Pop!_OS). No strace or root
# needed. What it pins down:
#   - a multi-call program (kmod) is judged by the name it was started
#     as: modinfo reads, modprobe by its own path is past the stand-in,
#     kmod as itself is not classified;
#   - O_TMPFILE in /tmp is not a change (noted), but in /etc it is, and
#     linkat that would keep such a file is;
#   - a finding names the program that did it.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
command -v python3 > /dev/null || { echo "SKIPPED: python3 is not installed"; exit 77; }
WORK="$(mktemp -d)"; trap 'rm -rf "${WORK}"' EXIT
mkdir "${WORK}/stubs"; touch "${WORK}/stubs/modprobe"
cat > "${WORK}/log" <<'EOF'
100 execve("/usr/sbin/modinfo", ["modinfo", "ntsync"], 0x7ff /* 3 vars */) = 0
101 execve("/usr/sbin/modprobe", ["modprobe", "ntsync"], 0x7ff /* 3 vars */) = 0
102 execve("/usr/bin/kmod", ["kmod", "load"], 0x7ff /* 3 vars */) = 0
100 openat(AT_FDCWD</>, "/tmp", O_RDWR|O_EXCL|O_TMPFILE, 0600) = 3</tmp/#123 (deleted)>
100 openat(AT_FDCWD</>, "/etc", O_RDWR|O_TMPFILE, 0600) = -1 EACCES (Permission denied)
100 linkat(3</tmp/#123>, "", AT_FDCWD</>, "/tmp/kept", AT_EMPTY_PATH) = 0
EOF
out="$(python3 -I "${HERE}/no-apply-trace.py" "${WORK}/log" --nexus /nonexistent \
    --stubs "${WORK}/stubs" --summary 2>&1)"
code=$?
failed=0
want() { printf '%s\n' "${out}" | grep -qF -- "$1" || { echo "MISSING: $1"; failed=1; }; }
never() { ! printf '%s\n' "${out}" | grep -qF -- "$1" || { echo "SHOULD NOT BE THERE: $1"; failed=1; }; }
want "past the stand-in for modprobe: ntsync"
want "not classified: /usr/bin/kmod load"
want "opened for writing (O_RDWR|O_TMPFILE): /etc [by modinfo]"
want "linkat: /tmp/kept [by modinfo]"
want "unnamed=/tmp [by modinfo]"
never "classified: /usr/bin/kmod ntsync"
never "(O_RDWR|O_TMPFILE): /tmp "
[ "${code}" -eq 1 ] || { echo "exit ${code}, should be 1"; failed=1; }
if [ "${failed}" -ne 0 ]; then printf 'output was:\n%s\n' "${out}"; exit 1; fi
echo "ok: multi-call names, unnamed temp files and who-did-it follow the rules"
