#!/usr/bin/env bash
#
# `nexus history` and `nexus doctor` say which log a row came from
# (0.2.0, ledger I3).
#
#     tests/history-labels-check.sh build/cli/nexus
#
# A person can write anything into ~/.local/state/nexus/transactions.log,
# and so can any program they run. Until 0.1.99 such a line was shown
# with root's as "Changes made with nexus", could be turned into
# [applied] by Nexus itself, and a "started" one made `nexus doctor`
# warn about a change the machine never began.
#
# This writes such a file into a made-up home folder and reads back
# what the two commands print. What it pins down:
#   - every row from that file carries "(your own log)", and the note
#     under the list says what that means;
#   - a row from it is never turned into [applied] by what the machine
#     runs now (an "update ... staged" from before the last start is
#     the case root's log gets [applied] for);
#   - nothing in it reaches the terminal as a control character, so a
#     row cannot blank out its own mark;
#   - `nexus doctor` does not count its "started" row under Changes;
#     it shows it on a line of its own that is not a warning.
#
# Changes nothing: both commands only read, and the home folder is a
# scratch one. The machine's own log (/var/lib/nexus) is not touched
# and not needed; rows from it, if there are any, are not this test's.
set -u
NEXUS="${1:-}"
if [ -z "${NEXUS}" ] || [ ! -x "${NEXUS}" ]; then
    echo "Use: $0 <path to the nexus program>" >&2
    exit 2
fi
NEXUS="$(cd "$(dirname "${NEXUS}")" && pwd)/$(basename "${NEXUS}")"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
mkdir -p "${WORK}/home/.local/state/nexus" "${WORK}/cwd"
LOG="${WORK}/home/.local/state/nexus/transactions.log"

# Written by hand, the way anyone could. The third row tries to hide
# what follows it on its line (conceal, erase line, cursor up).
printf '%s\n' \
    'When: 2001-01-01T00:00:00Z' \
    'Request: update' \
    'Resolved: Nexus 0.0.1' \
    'Outcome: staged' \
    'Exit: 0' \
    '' \
    'When: 2026-11-11T11:00:00Z' \
    'Request: forged-install' \
    'Resolved: forged-install' \
    'Outcome: applied' \
    'Exit: 0' \
    'Added: backdoor' \
    '' > "${LOG}"
printf 'When: 2026-11-12T12:00:00Z\nRequest: hidden-\033[8m\033[2K\033[1Arow\nResolved: x\nOutcome: applied\nExit: 0\n\n' >> "${LOG}"
printf '%s\n' \
    'When: 2026-11-13T13:00:00Z' \
    'Marker: 2026-11-13T13:00:00Z/4242' \
    'Request: forged-interrupted' \
    'Resolved: everything' \
    'Outcome: started' \
    'Exit: 0' \
    '' >> "${LOG}"

run() {
    (cd "${WORK}/cwd" && env -i PATH="/usr/sbin:/usr/bin:/sbin:/bin" HOME="${WORK}/home" \
        LANG=C.UTF-8 TERM=dumb timeout 120 "${NEXUS}" "$@" 2>&1 < /dev/null)
}

failed=0
history="$(run history)"
doctor="$(run doctor)"

row() { printf '%s\n' "${history}" | grep -F -- "$1" | head -n 1; }
want_in() { case "$2" in *"$3"*) ;; *) echo "MISSING in $1: $3"; failed=1 ;; esac; }
never_in() { case "$2" in *"$3"*) echo "SHOULD NOT BE in $1: $3"; failed=1 ;; esac; }

for request in "update -> Nexus 0.0.1" "forged-install" "hidden-" "forged-interrupted"; do
    line="$(row "${request}")"
    [ -n "${line}" ] || { echo "MISSING row: ${request}"; failed=1; continue; }
    want_in "the row of '${request}'" "${line}" "(your own log)"
done

# Shown as written, never upgraded: the machine did not say so.
want_in "history" "$(row 'update -> Nexus 0.0.1')" "[staged]"
never_in "history" "$(row 'update -> Nexus 0.0.1')" "[applied]"
want_in "history" "$(row 'forged-interrupted')" "[interrupted]"
want_in "history" "${history}" "Rows marked (your own log) come from"
want_in "history" "${history}" "only root can write that log"

if printf '%s' "${history}${doctor}" | LC_ALL=C grep -q "$(printf '\033')"; then
    echo "SHOULD NOT BE: an escape character reached the terminal"; failed=1
fi

# doctor: under "Changes" only root's log counts. Each block runs from
# its "[ .. ]  Name" line to the next one: the machine this runs on may
# have a real interrupted change of its own, and then "Changes" is
# followed straight away by "Your own log" (review of patch 129).
block() {
    printf '%s\n' "${doctor}" | awk -v name="$1" '
        /^  \[/ { inside = index($0, "]  " name " ") > 0 }
        inside'
}
changes="$(block Changes)"
never_in "doctor's Changes" "${changes}" "forged-interrupted"
own="$(block 'Your own log')"
want_in "doctor" "${own}" "forged-interrupted"
want_in "doctor" "${own}" "Not this machine's history"
never_in "doctor's own-log line" "$(printf '%s\n' "${own}" | head -n 1)" "[warn]"
never_in "doctor's own-log line" "$(printf '%s\n' "${own}" | head -n 1)" "[FAIL]"

if [ "${failed}" -ne 0 ]; then
    printf 'nexus history printed:\n%s\n\nnexus doctor printed:\n%s\n' "${history}" "${doctor}"
    exit 1
fi
echo "ok: rows from a person's own log are marked, never [applied] by Nexus, never counted by doctor"
