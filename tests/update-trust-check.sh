#!/usr/bin/env bash
#
# `nexus update`, end to end, against a registry that answers wrongly:
# an older signed build offered as "the update", and a tag that points
# somewhere else by the time the download starts (0.1.95, round-4
# audit M5 and L9).
#
#     sudo NEXUS_TEST_ALLOW_ROOT=1 tests/update-trust-check.sh build/cli/nexus
#
# Needs root, because `nexus update --apply` does, and because nexus
# takes the image-based path only where /run/ostree-booted exists: the
# test makes that file when it is missing and removes it again. So it
# runs only in a throwaway machine or container, and only when told to
# (NEXUS_TEST_ALLOW_ROOT=1): the Tests workflow on GitHub does. Anywhere
# else it says SKIPPED. It restores the history log and removes what it
# made, also when it fails. On a real image-based machine it refuses to
# run at all.
#
# rpm-ostree, skopeo, flatpak and notify-send are stand-ins that come
# first in PATH. The stand-in rpm-ostree keeps "the machine" in a file
# (what `rpm-ostree status` prints) and replaces it when asked to
# upgrade or clean up; the stand-in skopeo answers with whatever the
# case says the registry answers. Nothing is downloaded or staged.
#
# What must hold:
#   - a build older than the running one is never called new, never
#     announced by the daily notification, and rpm-ostree is never asked
#     to upgrade to it -- by version, by build time, or when it does not
#     say when it was built
#   - a newer build is shown with its digest, and after --apply the
#     deployment that starts next is that digest; the history says which
#   - if rpm-ostree staged anything else (the tag moved), or the staged
#     build is older than the running one by rpm-ostree's own account,
#     it is removed again (rpm-ostree cleanup --pending), the command
#     fails, and the history says what happened -- never "staged"
#   - if it could not be removed, the person is told so, and how
#
# What this cannot verify: what the real rpm-ostree, skopeo and
# registry do. That needs the Asus (HANDOFF, patch 123).

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
# A machine that really is image-based is somebody's machine.
if [ -e /run/ostree-booted ] || [ -d /ostree ] || [ -d /sysroot/ostree ]; then
    echo "SKIPPED: this is a real image-based system; not run here."
    exit 77
fi

VERSION="$("${NEXUS}" --version 2>/dev/null | awk 'NR == 1 {print $2}')"
case "${VERSION}" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "Could not read the version of ${NEXUS} (got '${VERSION}')." >&2; exit 2 ;;
esac

LOG="/var/lib/nexus/transactions.log"
WORK="$(mktemp -d)"
BIN="${WORK}/bin"
STATE="${WORK}/state"
CALLS="${WORK}/calls.log"
FAKEHOME="${WORK}/home"
mkdir -p "${BIN}" "${STATE}" "${FAKEHOME}"
HAD_LOG=0
[ -f "${LOG}" ] && cp -p "${LOG}" "${WORK}/log.saved" && HAD_LOG=1
HAD_NEXUS_DIR=0
[ -d /var/lib/nexus ] && HAD_NEXUS_DIR=1

cleanup() {
    rm -f /run/ostree-booted
    if [ "${HAD_LOG}" -eq 1 ]; then
        cp -p "${WORK}/log.saved" "${LOG}"
    else
        rm -f "${LOG}"
    fi
    rm -f "${LOG}.lock" "${LOG}.new"
    [ "${HAD_NEXUS_DIR}" -eq 1 ] || rmdir /var/lib/nexus 2>/dev/null
    rm -rf "${WORK}"
}
trap cleanup EXIT
: > /run/ostree-booted

# The stand-in rpm-ostree. "The machine" is $STATE/status.
cat > "${BIN}/rpm-ostree" <<'EOF'
#!/bin/sh
echo "rpm-ostree $*" >> "$CALLS"
case "$1" in
    status)
        [ -e "$STATE/status-unreadable" ] && [ -e "$STATE/upgraded" ] && exit 1
        cat "$STATE/status" ;;
    upgrade)
        code="$(cat "$STATE/upgrade-exit" 2>/dev/null || echo 0)"
        if [ "$code" != 0 ]; then
            echo "error: Creating importer: failed to invoke method OpenImage"
            exit "$code"
        fi
        echo "Pulling manifest: ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:kde"
        [ -e "$STATE/status-after" ] && cp "$STATE/status-after" "$STATE/status"
        : > "$STATE/upgraded"
        echo "Staging deployment... done" ;;
    cleanup)
        if [ -e "$STATE/cleanup-fails" ]; then
            echo "error: Transaction in progress"
            exit 1
        fi
        cp "$STATE/status-clean" "$STATE/status"
        echo "Deployments unchanged." ;;
    *) exit 0 ;;
esac
EOF
# The registry: one answer per case.
cat > "${BIN}/skopeo" <<'EOF'
#!/bin/sh
echo "skopeo $*" >> "$CALLS"
cat "$STATE/inspect"
EOF
printf '#!/bin/sh\necho "flatpak $*" >> "$CALLS"\nexit 0\n' > "${BIN}/flatpak"
printf '#!/bin/sh\necho "notify-send $*" >> "$CALLS"\nexit 0\n' > "${BIN}/notify-send"
chmod +x "${BIN}/rpm-ostree" "${BIN}/skopeo" "${BIN}/flatpak" "${BIN}/notify-send"
export CALLS STATE

ORIGIN="ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:kde"
RUNNING="sha256:$(printf '1%.0s' $(seq 64))"
NEWER="sha256:$(printf '2%.0s' $(seq 64))"
OTHER="sha256:$(printf '3%.0s' $(seq 64))"
OLDER="sha256:$(printf '0%.0s' $(seq 64))"
RUNNING_BUILT="2026-11-10T03:41:07Z"

# status_text [<pending digest> <pending built>]
status_text() {
    echo "State: idle"
    echo "Deployments:"
    if [ -n "${1:-}" ]; then
        echo "  ${ORIGIN}"
        echo "                   Digest: $1"
        echo "                  Version: 44.20261116.0 ($2)"
        echo "                     Diff: 55 upgraded, 2 removed, 1 added"
        echo
    fi
    printf '\xe2\x97\x8f %s\n' "${ORIGIN}"
    echo "                   Digest: ${RUNNING}"
    echo "                  Version: 44.20261109.0 (${RUNNING_BUILT})"
    echo "          LayeredPackages: gcc nmap"
    echo
    echo "  ${ORIGIN}"
    echo "                   Digest: ${OLDER}"
    echo "                  Version: 44.20261102.0 (2026-11-03T03:40:12Z)"
}

# registry <digest> <created> <nexus.version>
registry() {
    printf '{"Name":"ghcr.io/davidosdas-official/nexus-core","Digest":"%s","Created":"%s","Labels":{"nexus.version":"%s","containers.bootc":"1"}}\n' \
        "$1" "$2" "$3" > "${STATE}/inspect"
}

# setup [<staged digest> <staged built>] -- what `rpm-ostree upgrade`
# would leave behind; nothing given: it stages nothing.
setup() {
    rm -rf "${STATE}" "${FAKEHOME}"
    mkdir -p "${STATE}" "${FAKEHOME}"
    status_text > "${STATE}/status"
    status_text > "${STATE}/status-clean"
    [ -n "${1:-}" ] && status_text "$1" "$2" > "${STATE}/status-after"
    : > "${CALLS}"
}

# run <nexus update arguments...> -> CODE, OUT
run() {
    OUT="${WORK}/out.txt"
    PATH="${BIN}:/usr/bin:/bin" HOME="${FAKEHOME}" "${NEXUS}" update "$@" \
        > "${OUT}" 2>&1 < /dev/null
    CODE=$?
}

called() { grep -c "^$1" "${CALLS}" || true; }
last_field() { grep "^$1:" "${LOG}" 2>/dev/null | tail -n 1 | sed "s/^$1: //"; }
pending_now() { awk '/^  ostree/ && !seen {p=1} /^\xe2\x97\x8f/ {seen=1; p=0} p && /Digest:/ {print $2; exit}' "${STATE}/status"; }

failures=0
cases=0
expect() {
    if [ "$2" != "$3" ]; then
        echo "    WRONG ${1}: is '${2}', should be '${3}'"
        case_failed=1
    fi
}
says() {
    grep -qF -- "$1" "${OUT}" || { echo "    WRONG output: no '$1'"; case_failed=1; }
}
says_not() {
    if grep -qF -- "$1" "${OUT}"; then echo "    WRONG output: says '$1'"; case_failed=1; fi
}
begin() { cases=$((cases + 1)); case_failed=0; title="$1"; }
finish() {
    if [ "${case_failed}" -ne 0 ]; then
        failures=$((failures + 1))
        echo "FAILED: ${title}"
        sed 's/^/    | /' "${OUT}" | tail -n 14
    fi
}

# ------------------------------------------------ M5: never backwards
begin "preview: last week's build of the same version is not called new"
setup; registry "${OLDER}" "2026-11-03T03:40:12.5Z" "${VERSION}"
run
expect "exit code" "${CODE}" 0
says "Not taken"
says "2026-11-03 03:40 UTC"
says_not "New   "
says_not "sudo nexus update --apply"
expect "upgrade calls" "$(called 'rpm-ostree upgrade')" 0
finish

begin "preview: a lower Nexus version is not called new, whatever date it claims"
setup; registry "${OLDER}" "2027-01-01T00:00:00Z" "0.0.1"
run
expect "exit code" "${CODE}" 0
says "Not taken"
says "Nexus 0.0.1"
says_not "New   "
finish

begin "preview: a build that does not say when it was built is not called new"
setup; registry "${OTHER}" "" "${VERSION}"
run
expect "exit code" "${CODE}" 0
says "Not taken"
says_not "New   "
finish

begin "--apply: rpm-ostree is never asked to upgrade to an older build"
setup "${OLDER}" "2026-11-03T03:40:12Z"; registry "${OLDER}" "2026-11-03T03:40:12Z" "${VERSION}"
run --apply --yes
says "Not taken"
says_not "handing over to rpm-ostree"
expect "upgrade calls" "$(called 'rpm-ostree upgrade')" 0
expect "deployment that starts next" "$(pending_now)" ""
finish

begin "the daily notification does not announce an older build"
setup; registry "${OLDER}" "2026-11-03T03:40:12Z" "${VERSION}"
run notify
expect "exit code" "${CODE}" 0
expect "notifications" "$(called 'notify-send')" 0
finish

begin "the Shop's lines: an older build is 'refused', not 'new'"
setup; registry "${OLDER}" "2026-11-03T03:40:12Z" "${VERSION}"
run --lines
expect "exit code" "${CODE}" 0
expect "system line" "$(awk -F'\t' '$1 == "system" {print $2}' "${OUT}")" "refused"
finish

# ------------------------------------------------ a real update still is one
begin "preview: a newer build is new, and says which build"
setup; registry "${NEWER}" "2026-11-17T03:40:55Z" "${VERSION}"
run
expect "exit code" "${CODE}" 0
says "New          Nexus ${VERSION}"
says "build 222222222222: --apply installs exactly this one"
says_not "Not taken"
expect "upgrade calls" "$(called 'rpm-ostree upgrade')" 0
finish

begin "the daily notification still announces a newer build"
setup; registry "${NEWER}" "2026-11-17T03:40:55Z" "${VERSION}"
run notify
expect "notifications" "$(called 'notify-send')" 1
finish

begin "the Shop's lines: a newer build is 'new'"
setup; registry "${NEWER}" "2026-11-17T03:40:55Z" "${VERSION}"
run --lines
expect "system line" "$(awk -F'\t' '$1 == "system" {print $2}' "${OUT}")" "new"
finish

# ------------------------------------------------ L9: shown is installed
begin "--apply: the build that was shown is the one staged, and the history says which"
setup "${NEWER}" "2026-11-17T03:40:55Z"; registry "${NEWER}" "2026-11-17T03:40:55Z" "${VERSION}"
run --apply --yes
expect "exit code" "${CODE}" 0
expect "upgrade calls" "$(called 'rpm-ostree upgrade')" 1
expect "cleanup calls" "$(called 'rpm-ostree cleanup')" 0
expect "deployment that starts next" "$(pending_now)" "${NEWER}"
expect "history outcome" "$(last_field Outcome)" "staged"
says "Staged: build 222222222222, the one shown above"
case "$(last_field Resolved)" in
    *"system build 222222222222"*) ;;
    *) echo "    WRONG history: no build in '$(last_field Resolved)'"; case_failed=1 ;;
esac
finish

begin "--apply: the tag moved before the download -- what was staged is removed"
setup "${OTHER}" "2026-11-17T09:00:00Z"; registry "${NEWER}" "2026-11-17T03:40:55Z" "${VERSION}"
run --apply --yes
expect "exit code" "${CODE}" 1
expect "upgrade calls" "$(called 'rpm-ostree upgrade')" 1
expect "cleanup calls" "$(called 'rpm-ostree cleanup --pending')" 1
expect "deployment that starts next" "$(pending_now)" ""
says "another build than the one shown"
says_not "Done."
case "$(last_field Outcome)" in
    failed*"it was removed"*) ;;
    *) echo "    WRONG history: '$(last_field Outcome)'"; case_failed=1 ;;
esac
finish

begin "--apply: the tag moved and the staged build could not be removed -- said plainly"
setup "${OTHER}" "2026-11-17T09:00:00Z"; registry "${NEWER}" "2026-11-17T03:40:55Z" "${VERSION}"
: > "${STATE}/cleanup-fails"
run --apply --yes
expect "exit code" "${CODE}" 1
says "could NOT be removed"
says "sudo rpm-ostree cleanup --pending"
says_not "Done."
case "$(last_field Outcome)" in
    failed*"still staged"*) ;;
    *) echo "    WRONG history: '$(last_field Outcome)'"; case_failed=1 ;;
esac
finish

begin "--apply: the staged build is older than the running one by rpm-ostree's own account"
# The registry described the build as newer; what was signed is not.
setup "${NEWER}" "2026-11-03T03:40:12Z"; registry "${NEWER}" "2026-11-17T03:40:55Z" "${VERSION}"
run --apply --yes
expect "exit code" "${CODE}" 1
expect "cleanup calls" "$(called 'rpm-ostree cleanup --pending')" 1
expect "deployment that starts next" "$(pending_now)" ""
says "never goes backwards"
says_not "Done."
case "$(last_field Outcome)" in
    failed*"older build"*"removed"*) ;;
    *) echo "    WRONG history: '$(last_field Outcome)'"; case_failed=1 ;;
esac
finish

begin "--apply: rpm-ostree says yes and stages nothing"
setup; registry "${NEWER}" "2026-11-17T03:40:55Z" "${VERSION}"
run --apply --yes
expect "exit code" "${CODE}" 1
expect "cleanup calls" "$(called 'rpm-ostree cleanup')" 0
says "staged nothing new"
says_not "Done."
case "$(last_field Outcome)" in
    failed*"nothing was staged"*) ;;
    *) echo "    WRONG history: '$(last_field Outcome)'"; case_failed=1 ;;
esac
finish

begin "--apply: rpm-ostree's status cannot be read back -- not called done"
setup "${NEWER}" "2026-11-17T03:40:55Z"; registry "${NEWER}" "2026-11-17T03:40:55Z" "${VERSION}"
: > "${STATE}/status-unreadable"
run --apply --yes
expect "exit code" "${CODE}" 1
says "could not be read"
says_not "Done."
finish

begin "--apply: rpm-ostree itself fails -- as before, and nothing is cleaned up"
setup "${NEWER}" "2026-11-17T03:40:55Z"; registry "${NEWER}" "2026-11-17T03:40:55Z" "${VERSION}"
echo 1 > "${STATE}/upgrade-exit"
run --apply --yes
expect "exit code" "${CODE}" 1
expect "cleanup calls" "$(called 'rpm-ostree cleanup')" 0
expect "history outcome" "$(last_field Outcome)" "failed"
finish

echo
if [ "${failures}" -ne 0 ]; then
    echo "${failures} of ${cases} update cases FAILED."
    exit 1
fi
echo "All ${cases} update cases passed (nexus ${VERSION}, stand-in rpm-ostree and skopeo)."
