#!/usr/bin/env bash
#
# Old program against new, command by command (0.1.96).
#
#     tools/compare-outputs.sh <old nexus> <new nexus> [<report file>]
#
# For a change that must not change behaviour -- cli/src/main.cpp
# becoming one file per command area was the first. Runs every command
# tests/no-apply-changes-nothing.sh runs -- the list and the stand-in
# tools are read out of that script, not copied -- once with each
# program, in the same folder, with the same home folder contents, and
# compares: what was printed (both streams, in order), the exit code,
# and which stand-in tools were called with which words. Nothing has
# --apply. Exit 0 only if every command is the same.
#
# Not compared, because it is not behaviour: each program's own path
# (a preview prints the command to run again), its own version and
# commit, and the numbers on the Memory, Uptime, Disk and Storage
# lines of `nexus doctor` and `nexus specs` (in --json: ram_used_mib,
# used_bytes, uptime_seconds), which differ between two runs of one
# program. Keep the two programs in the same folder
# (build/cli/nexus and build/cli/nexus-before): nexus looks for its
# profiles relative to where it is.
#
# This is a tool for a person, not a ctest test: it needs a second
# program to compare with. As root it refuses unless
# NEXUS_TEST_ALLOW_ROOT=1, like the script it reads.
set -u
OLD="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
NEW="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
REPORT="${3:-/dev/stdout}"
SCRIPT="${NEXUS_NO_APPLY_SCRIPT:-$(cd "$(dirname "$0")/.." && pwd)/tests/no-apply-changes-nothing.sh}"
[ -r "${SCRIPT}" ] || { echo "Cannot read ${SCRIPT}" >&2; exit 2; }
[ -x "${OLD}" ] && [ -x "${NEW}" ] || { echo "Use: $0 <old nexus> <new nexus> [<report file>]" >&2; exit 2; }
if [ "$(id -u)" -eq 0 ] && [ "${NEXUS_TEST_ALLOW_ROOT:-0}" != 1 ]; then
    echo "Not run as root (set NEXUS_TEST_ALLOW_ROOT=1 in a throwaway container)." >&2
    exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
STUBS="${WORK}/stubs"; CALLS="${WORK}/calls.log"; FAKEHOME="${WORK}/home"
SITE="${WORK}/site"; CWD="${WORK}/cwd"
mkdir -p "${STUBS}" "${SITE}" "${CWD}" "${WORK}/home-old/.cache" "${WORK}/home-new/.cache"
echo "<h1>hello</h1>" > "${SITE}/index.html"

# The two lists of stand-ins, and the commands, as the test has them.
block() { awk -v name="$1" '$0 ~ "^"name"=\"" {on=1; sub("^"name"=\"", "")} on {line=$0; if (sub(/"$/, "", line)) {print line; exit} print line}' "${SCRIPT}"; }
ALWAYS="$(block ALWAYS)"
PATTERNED="$(block PATTERNED)"
COMMANDS="$(awk '/^COMMANDS="\$\(cat <<EOF$/ {on=1; next} on && /^EOF$/ {exit} on' "${SCRIPT}" | sed "s|\${SITE}|${SITE}|g")"
[ -n "${ALWAYS}" ] && [ -n "${PATTERNED}" ] && [ -n "${COMMANDS}" ] || { echo "could not read the lists out of ${SCRIPT}" >&2; exit 2; }

for tool in ${ALWAYS} ${PATTERNED}; do
    cat > "${STUBS}/${tool}" <<STUB
#!/bin/sh
printf '%s' '${tool}' >> '${CALLS}'
for a in "\$@"; do printf ' %s' "\$a" >> '${CALLS}'; done
printf '\n' >> '${CALLS}'
exit 0
STUB
    chmod +x "${STUBS}/${tool}"
done

# A program's own "0.1.96" and "(1a2b3c4-dirty)", as text for sed. The
# commit only where it stands in brackets, as `nexus --version` and
# `nexus doctor` print it: a build without one says "unknown", which is
# also an ordinary word in other lines.
own() { "$1" --version 2>/dev/null | awk -v want="$2" 'NR == 1 {v = $2; c = $3; gsub(/[()]/, "", c); gsub(/[.]/, "\\.", v); print (want == "version" ? v : c)}'; }
plain() {   # plain <which> <file>: the same text for the same behaviour
    local program="${OLD}"; [ "$1" = new ] && program="${NEW}"
    sed -E -e "s/$(printf '%s' "${program}" | sed 's/[][\\.*^$/+?(){}|]/\\&/g')/<nexus>/g" \
        -e "s/$(own "${program}" version)/<version>/g" \
        -e "s/\\($(own "${program}" commit)\\)/(<commit>)/g" \
        -e '/^ *(\[[^]]*\] +)?(Memory|Uptime|Disk|Storage) /s/[0-9]+/#/g' \
        -e 's/"(ram_used_mib|used_bytes|uptime_seconds)": [0-9]+/"\1": #/g' "$2"
}

LIMIT="${NEXUS_TEST_COMMAND_TIMEOUT:-60}"
run() {   # run <which> <program> <words...>
    local which="$1" program="$2"; shift 2
    mv "${WORK}/home-${which}" "${FAKEHOME}"
    : > "${CALLS}"
    (cd "${CWD}" && env -i PATH="${STUBS}:/usr/sbin:/usr/bin:/sbin:/bin" HOME="${FAKEHOME}" \
        LANG=C.UTF-8 TERM=dumb timeout "${LIMIT}" "${program}" "$@" > "${WORK}/raw-${which}.txt" 2>&1 < /dev/null)
    echo "exit code: $?" >> "${WORK}/raw-${which}.txt"
    plain "${which}" "${WORK}/raw-${which}.txt" > "${WORK}/out-${which}.txt"
    cp "${CALLS}" "${WORK}/calls-${which}.txt"
    mv "${FAKEHOME}" "${WORK}/home-${which}"
}

same=0; different=0; count=0; bytes=0
{
while IFS= read -r line; do
    [ -n "${line}" ] || continue
    count=$((count + 1))
    # shellcheck disable=SC2086
    run old "${OLD}" ${line}
    # shellcheck disable=SC2086
    run new "${NEW}" ${line}
    bytes=$((bytes + $(wc -c < "${WORK}/out-new.txt")))
    if cmp -s "${WORK}/out-old.txt" "${WORK}/out-new.txt" && cmp -s "${WORK}/calls-old.txt" "${WORK}/calls-new.txt"; then
        same=$((same + 1))
    else
        different=$((different + 1))
        echo "DIFFERENT: nexus ${line}"
        diff "${WORK}/out-old.txt" "${WORK}/out-new.txt" | head -n 20 | sed 's/^/    /'
        diff "${WORK}/calls-old.txt" "${WORK}/calls-new.txt" | head -n 10 | sed 's/^/    tools: /'
    fi
done <<LIST
${COMMANDS}
LIST
# What is left in the two home folders (~/.cache is where a preview may write).
if diff -r "${WORK}/home-old" "${WORK}/home-new" > "${WORK}/homes.diff" 2>&1; then
    echo "home folders after all commands: identical ($(find "${WORK}/home-new" -type f | wc -l) files)"
else
    different=$((different + 1))
    echo "DIFFERENT: the home folders after all commands"; head -n 20 "${WORK}/homes.diff" | sed 's/^/    /'
fi
echo "${count} commands as $(id -un): ${same} identical (output, exit code and tools called), ${different} different; ${bytes} bytes of output compared."
} > "${REPORT}"
[ "${different}" -eq 0 ]
