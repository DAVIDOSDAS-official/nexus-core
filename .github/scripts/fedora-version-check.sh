#!/usr/bin/env bash
#
# Open an issue when a newer Fedora bootc image exists. Run monthly by
# .github/workflows/fedora-version-check.yml; see there for why.

set -euo pipefail

current="$(sed -n 's/^ARG FEDORA_VERSION=//p' image/Containerfile)"
echo "Building on Fedora ${current}."

exists() {
    skopeo inspect --raw "docker://quay.io/fedora/fedora-bootc:$1" \
        > /dev/null 2>&1
}

# One issue per title: an open one is not opened again.
open_issue() {
    local title="$1" body="$2"

    if gh issue list --state open --limit 100 --json title --jq '.[].title' \
            | grep -qxF "${title}"; then
        echo "Already open: ${title}"
        return 0
    fi

    gh issue create --title "${title}" --body "${body}"
}

next=$((current + 1))
after=$((current + 2))

if exists "${after}"; then
    open_issue "URGENT: Fedora ${current} is near end of life" "$(cat <<BODY
Fedora ${after} bootc images exist, so Fedora ${current} stops getting
security updates about four weeks after Fedora ${after} is released.
Every Nexus machine is on Fedora ${current}.

Move now: image/README.md, "Fedora bump order". Change ARG
FEDORA_VERSION in image/Containerfile, build --target base, run
./image/generate-lists.sh, read the diff, build, test on the VM and the
Asus, publish.
BODY
)"
elif exists "${next}"; then
    open_issue "Fedora ${next} is available" "$(cat <<BODY
Fedora ${next} bootc images now exist. Nothing is broken: the weekly
image keeps Fedora ${current} updated. Plan the move once Fedora ${next}
is officially released (fedoraproject.org), using image/README.md,
"Fedora bump order".
BODY
)"
else
    echo "Nothing newer than Fedora ${current}."
fi
