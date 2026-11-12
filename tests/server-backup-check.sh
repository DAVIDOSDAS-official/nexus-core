#!/usr/bin/env bash
#
# `nexus server backup` and `nexus server remove`, end to end, through
# every way they can go wrong: the server does not stop, the copy is
# killed, Ctrl-C, a closed terminal, the server does not start again.
#
#     sudo NEXUS_TEST_ALLOW_ROOT=1 tests/server-backup-check.sh build/cli/nexus
#
# Needs root, because the command does; and it writes a pretend server
# into /etc/containers/systemd and /var/lib/nexus. So it runs only in a
# throwaway machine or container, and only when told to
# (NEXUS_TEST_ALLOW_ROOT=1): the Tests workflow on GitHub does. Anywhere
# else it says SKIPPED. It restores the history log and removes what it
# made, also when it fails.
#
# systemctl, tar and firewall-cmd are stand-ins that come first in PATH:
# the stand-in systemctl keeps the "server"'s state in a file and can be
# told to misbehave. Nothing is really started or stopped.
#
# What must hold after every case (0.1.87 audit, 0.1.88 review):
#   - a server that was running is running; one that was stopped on
#     purpose is stopped
#   - no copy is taken, and nothing removed, unless the server is seen
#     to be stopped
#   - a failed or interrupted backup leaves no file in the backups
#     folder, neither a finished-looking one nor a .partial
#   - the history says what happened, never "started"
#   - after Ctrl-C, nexus is seen to have ended by that signal

set -u

NEXUS="${1:-}"
if [ -z "${NEXUS}" ] || [ ! -x "${NEXUS}" ]; then
    echo "Use: $0 <path to the nexus program>" >&2
    exit 2
fi
NEXUS="$(cd "$(dirname "${NEXUS}")" && pwd)/$(basename "${NEXUS}")"

if [ "$(id -u)" -ne 0 ] || [ "${NEXUS_TEST_ALLOW_ROOT:-0}" != 1 ]; then
    echo "SKIPPED: needs root in a throwaway machine (sudo NEXUS_TEST_ALLOW_ROOT=1 $0 ...)."
    exit 77
fi
if ! command -v python3 > /dev/null 2>&1; then
    echo "SKIPPED: python3 is needed to send signals the way a terminal does."
    exit 77
fi

NAME="nexus-selftest"
UNITFILE="/etc/containers/systemd/nexus-${NAME}.container"
DATA="/var/lib/nexus/servers/${NAME}"
BACKUPS="/var/lib/nexus/backups"
LOG="/var/lib/nexus/transactions.log"

if [ -e "${UNITFILE}" ] || [ -e "${DATA}" ]; then
    echo "A server called ${NAME} already exists here; not touching it." >&2
    exit 2
fi

WORK="$(mktemp -d)"
BIN="${WORK}/bin"
STATE="${WORK}/state"
CALLS="${WORK}/calls.log"
mkdir -p "${BIN}" "${STATE}"
HAD_LOG=0
[ -f "${LOG}" ] && cp -p "${LOG}" "${WORK}/log.saved" && HAD_LOG=1
HAD_BACKUPS=0
[ -d "${BACKUPS}" ] && HAD_BACKUPS=1
HAD_SERVERS=0
[ -d /var/lib/nexus/servers ] && HAD_SERVERS=1
# Records already at "started" before this test (this machine's own
# history) are not this test's doing.
STARTED_BEFORE="$(grep -c '^Outcome: started' "${LOG}" 2>/dev/null || true)"

cleanup() {
    rm -f "${UNITFILE}"
    rm -rf "${DATA}"
    rm -f "${BACKUPS}/${NAME}-"* 2>/dev/null
    [ "${HAD_BACKUPS}" -eq 1 ] || rmdir "${BACKUPS}" 2>/dev/null
    [ "${HAD_SERVERS}" -eq 1 ] || rmdir /var/lib/nexus/servers 2>/dev/null
    if [ "${HAD_LOG}" -eq 1 ]; then
        cp -p "${WORK}/log.saved" "${LOG}"
    else
        rm -f "${LOG}"
    fi
    rm -f "${LOG}.lock" "${LOG}.new"
    rm -rf "${WORK}"
}
trap cleanup EXIT

# The stand-in systemctl. The state of the one unit is the word in
# $STATE/state; files beside it make it misbehave.
cat > "${BIN}/systemctl" <<'EOF'
#!/bin/sh
echo "systemctl $*" >> "$CALLS"
state="$(cat "$STATE/state" 2>/dev/null)"
case "$1" in
    is-active)
        [ -e "$STATE/no-answer" ] && exit 1
        echo "$state"
        [ "$state" = active ] ;;
    stop)
        if [ -e "$STATE/stop-fails" ]; then
            echo "Failed to stop nexus-selftest.service: Connection timed out"
            exit 1
        fi
        [ -e "$STATE/stop-lies" ] && exit 0          # says yes, stops nothing
        if [ -e "$STATE/stop-not-loaded" ]; then
            echo "Failed to stop nexus-selftest.service: Unit nexus-selftest.service not loaded."
            exit 5
        fi
        echo inactive > "$STATE/state" ;;
    start)
        if [ -e "$STATE/start-fails" ]; then
            echo "Job for nexus-selftest.service failed because the control process exited with error code."
            exit 1
        fi
        [ -e "$STATE/start-lies" ] && exit 0
        echo active > "$STATE/state" ;;
    *) exit 0 ;;
esac
EOF
# firewalld "not running": no ports are touched.
printf '#!/bin/sh\necho "firewall-cmd $*" >> "$CALLS"\nexit 1\n' > "${BIN}/firewall-cmd"
chmod +x "${BIN}/systemctl" "${BIN}/firewall-cmd"
REAL_TAR="$(command -v tar)"
export CALLS STATE REAL_TAR

# The stand-in tar: records the call, then does what $STATE/tar says.
cat > "${BIN}/tar" <<'EOF'
#!/bin/sh
echo "tar $*" >> "$CALLS"
case "$(cat "$STATE/tar" 2>/dev/null)" in
    killed)  head -c 5000 /dev/urandom > "$2"; kill -KILL $$ ;;
    slow)    head -c 5000 /dev/urandom > "$2"; exec sleep 30 ;;
    slow-ok) sleep 3; exec "$REAL_TAR" "$@" ;;
    *)       exec "$REAL_TAR" "$@" ;;
esac
EOF
chmod +x "${BIN}/tar"

failures=0
cases=0

# setup <state before> [flag files...]
setup() {
    rm -rf "${STATE}" "${DATA}"
    rm -f "${BACKUPS}/${NAME}-"* 2>/dev/null
    mkdir -p "${STATE}" /etc/containers/systemd "${DATA}/data"
    printf '[Container]\nLabel=nexus.kind=website\nPublishPort=8080:80/tcp\n' > "${UNITFILE}"
    head -c 200000 /dev/urandom > "${DATA}/data/world"
    echo "$1" > "${STATE}/state"
    shift
    for flag in "$@"; do
        case "${flag}" in
            tar=*) echo "${flag#tar=}" > "${STATE}/tar" ;;
            *) : > "${STATE}/${flag}" ;;
        esac
    done
    : > "${CALLS}"
}

# run <action> [signal] [whole-group|nexus-only] -> sets CODE (exit code,
# or -N if ended by signal N) and OUT (what it printed)
run() {
    OUT="${WORK}/out.txt"
    CODE="$(PATH="${BIN}:${PATH}" python3 -I - "${NEXUS}" "${OUT}" "${NAME}" "$@" <<'PY'
import os, signal, subprocess, sys, time
nexus, out, name, action = sys.argv[1:5]
sig = getattr(signal, sys.argv[5]) if len(sys.argv) > 5 else None
whole = len(sys.argv) > 6 and sys.argv[6] == "whole-group"
def like_a_terminal():
    # Signals at their defaults, as in a shell at a terminal.
    for s in (signal.SIGINT, signal.SIGHUP, signal.SIGTERM, signal.SIGQUIT):
        signal.signal(s, signal.SIG_DFL)
with open(out, "w") as f:
    p = subprocess.Popen([nexus, "server", action, name, "--apply"], stdout=f, stderr=f,
                         stdin=subprocess.DEVNULL, start_new_session=True,
                         preexec_fn=like_a_terminal)
    if sig is not None:
        time.sleep(1.5)
        if whole:
            os.killpg(p.pid, sig)      # what Ctrl-C and a closed terminal do
        else:
            os.kill(p.pid, sig)        # what `kill <pid>` does
    try:
        rc = p.wait(timeout=60)
    except subprocess.TimeoutExpired:
        p.kill()
        rc = 999
print(rc)
PY
)"
}

state_now() { cat "${STATE}/state"; }
in_backups() { find "${BACKUPS}" -maxdepth 1 -name "${NAME}-*" 2>/dev/null | wc -l; }
last_outcome() { grep '^Outcome:' "${LOG}" 2>/dev/null | tail -n 1 | sed 's/^Outcome: //'; }
called() { grep -c "^$1" "${CALLS}" || true; }

# expect <what> <actual> <wanted>
expect() {
    if [ "$2" != "$3" ]; then
        echo "    WRONG ${1}: is '${2}', should be '${3}'"
        case_failed=1
    fi
}
begin() { cases=$((cases + 1)); case_failed=0; title="$1"; }
finish() {
    if [ "${case_failed}" -ne 0 ]; then
        failures=$((failures + 1))
        echo "FAILED: ${title}"
        sed 's/^/    | /' "${OUT}" | tail -n 6
    fi
}

# ---------------------------------------------------------------- backup
begin "an ordinary backup of a running server"
setup active; run backup
expect "exit code" "${CODE}" 0
expect "server" "$(state_now)" active
expect "files in backups" "$(in_backups)" 1
expect "history" "$(last_outcome)" "backed up"
archive="$(ls "${BACKUPS}/${NAME}-"*.tar.gz 2>/dev/null | head -n 1)"
if [ -z "${archive}" ] || ! "${REAL_TAR}" -tzf "${archive}" > /dev/null 2>&1; then
    echo "    WRONG archive: does not list"; case_failed=1
else
    expect "archive mode" "$(stat -c %a "${archive}")" 600
fi
grep -q "running again" "${OUT}" || { echo "    WRONG message: no 'running again'"; case_failed=1; }
finish

begin "the server does not stop (systemctl stop fails)"
setup active stop-fails; run backup
expect "exit code" "${CODE}" 1
expect "server" "$(state_now)" active
expect "tar calls" "$(called tar)" 0
expect "files in backups" "$(in_backups)" 0
expect "history" "$(last_outcome)" failed
grep -q "did not stop" "${OUT}" || { echo "    WRONG message: no 'did not stop'"; case_failed=1; }
finish

begin "systemctl stop says yes but the server is still running"
setup active stop-lies; run backup
expect "exit code" "${CODE}" 1
expect "server" "$(state_now)" active
expect "tar calls" "$(called tar)" 0
expect "files in backups" "$(in_backups)" 0
expect "history" "$(last_outcome)" failed
finish

begin "systemd gives no answer about the server after the stop"
setup active stop-lies no-answer; run backup
expect "exit code" "${CODE}" 1
expect "tar calls" "$(called tar)" 0
expect "files in backups" "$(in_backups)" 0
expect "history" "$(last_outcome)" failed
finish

begin "stop 'fails' because the unit was never loaded, and it is stopped"
setup inactive stop-not-loaded; run backup
expect "exit code" "${CODE}" 0
expect "server" "$(state_now)" inactive
expect "files in backups" "$(in_backups)" 1
expect "history" "$(last_outcome)" "backed up"
finish

begin "tar is killed part-way (as the kernel does when memory runs out)"
setup active tar=killed; run backup
expect "exit code" "${CODE}" 1
expect "server" "$(state_now)" active
expect "files in backups" "$(in_backups)" 0
expect "history" "$(last_outcome)" failed
finish

begin "Ctrl-C while the copy runs"
setup active tar=slow; run backup SIGINT whole-group
expect "ended by" "${CODE}" -2
expect "server" "$(state_now)" active
expect "files in backups" "$(in_backups)" 0
expect "history" "$(last_outcome)" failed
finish

begin "the terminal is closed while the copy runs (SIGHUP)"
setup active tar=slow; run backup SIGHUP whole-group
expect "ended by" "${CODE}" -1
expect "server" "$(state_now)" active
expect "files in backups" "$(in_backups)" 0
expect "history" "$(last_outcome)" failed
finish

begin "Ctrl-\\ while the copy runs (SIGQUIT)"
setup active tar=slow; run backup SIGQUIT whole-group
expect "ended by" "${CODE}" -3
expect "server" "$(state_now)" active
expect "files in backups" "$(in_backups)" 0
expect "history" "$(last_outcome)" failed
finish

begin "only nexus is asked to end (kill <pid>), the copy goes on and finishes"
setup active tar=slow-ok; run backup SIGTERM nexus-only
expect "exit code" "${CODE}" 0
expect "server" "$(state_now)" active
expect "files in backups" "$(in_backups)" 1
expect "history" "$(last_outcome)" "backed up"
finish

begin "the copy is whole but the server does not start again"
setup active start-fails; run backup
expect "exit code" "${CODE}" 1
expect "server" "$(state_now)" inactive
expect "files in backups" "$(in_backups)" 1
grep -q "did not start again" "${OUT}" || { echo "    WRONG message: no 'did not start again'"; case_failed=1; }
if grep -q "running again" "${OUT}"; then echo "    WRONG message: says 'running again'"; case_failed=1; fi
finish

begin "systemctl start says yes but the server is not running"
setup active start-lies; run backup
expect "exit code" "${CODE}" 1
expect "files in backups" "$(in_backups)" 1
grep -q "did not start again" "${OUT}" || { echo "    WRONG message: no 'did not start again'"; case_failed=1; }
finish

begin "a server that had been stopped on purpose"
setup inactive; run backup
expect "exit code" "${CODE}" 0
expect "server" "$(state_now)" inactive
expect "start calls" "$(called 'systemctl start')" 0
expect "files in backups" "$(in_backups)" 1
grep -q "left stopped" "${OUT}" || { echo "    WRONG message: no 'left stopped'"; case_failed=1; }
finish

begin "a server that had failed is backed up and started"
setup failed; run backup
expect "exit code" "${CODE}" 0
expect "server" "$(state_now)" active
expect "files in backups" "$(in_backups)" 1
finish

# ---------------------------------------------------------------- remove
begin "an ordinary removal"
setup active; run remove
expect "exit code" "${CODE}" 0
expect "server" "$(state_now)" inactive
expect "unit file" "$([ -e "${UNITFILE}" ] && echo there || echo gone)" gone
expect "data" "$([ -e "${DATA}/data/world" ] && echo kept || echo GONE)" kept
expect "history" "$(last_outcome)" removed
finish

begin "removal when the server does not stop"
setup active stop-fails; run remove
expect "exit code" "${CODE}" 1
expect "server" "$(state_now)" active
expect "unit file" "$([ -e "${UNITFILE}" ] && echo there || echo gone)" there
expect "history" "$(last_outcome)" failed
finish

begin "removal of a server that was never started"
setup inactive stop-not-loaded; run remove
expect "exit code" "${CODE}" 0
expect "unit file" "$([ -e "${UNITFILE}" ] && echo there || echo gone)" gone
finish

# ------------------------------------------------- nothing left "started"
begin "no record is left at 'started'"
OUT="${WORK}/none.txt"; : > "${OUT}"
expect "records at started" "$(grep -c '^Outcome: started' "${LOG}" || true)" "${STARTED_BEFORE:-0}"
finish

if [ "${failures}" -ne 0 ]; then
    echo
    echo "${failures} of ${cases} cases went wrong."
    exit 1
fi
echo "ok: ${cases} cases of server backup and removal: stopped before anything is touched, back as it was after, nothing half-made left behind"
