#!/usr/bin/env bash
# Every command nexus accepts is named in nexus help (0.2.4).
#
# Until 0.2.4 the guide named 15 of the 40 commands, and nothing
# noticed. The list of commands is read from main.cpp, so a command
# added there and not to the guide fails here. The guide's source is
# read rather than its output because some lines (update, rollback)
# are printed only on an image-based machine, which a build box is not.
set -euo pipefail

main="$1"
guide="$2"

commands=$(awk '/kCommands = \{/,/\};/' "${main}" | grep -o '"[a-z-]*"' | tr -d '"')
[ -n "${commands}" ] || { echo "FAIL: no command list found in ${main}"; exit 1; }

missing=0
count=0
while IFS= read -r command; do
    count=$((count + 1))
    if ! grep -qE "nexus ${command}([^a-z-]|$)" "${guide}"; then
        echo "FAIL: nexus help does not name: nexus ${command}"
        missing=1
    fi
done <<< "${commands}"
[ "${missing}" -eq 0 ] || exit 1
echo "PASS: nexus help names all ${count} commands"
