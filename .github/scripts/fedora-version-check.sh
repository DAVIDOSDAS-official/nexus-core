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

# The registry is not enough: quay.io carries the development versions
# too (rawhide and the branched release are tagged with their future
# numbers), so "Fedora N+2 exists there" fired weeks before N+2 was
# released (issue #1, October 2026). Fedora's update system, Bodhi,
# knows which releases are actually out: state "current".
released_versions() {
    curl -fsS --retry 3 \
        "https://bodhi.fedoraproject.org/releases/?state=current&rows_per_page=100" \
        | jq -r '.releases[] | select(.id_prefix == "FEDORA")
                 | select(.name | test("^F[0-9]+$")) | .version'
}

released() {
    grep -qxF "$1" <<< "${released}"
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

if ! released="$(released_versions)" || [ -z "${released}" ]; then
    echo "Could not ask Bodhi which Fedora releases are out; trying again next month."
    exit 0
fi
echo "Released Fedora versions: $(echo ${released})"

next=$((current + 1))
after=$((current + 2))

if released "${after}"; then
    open_issue "URGENT: Fedora ${current} is near end of life" "$(cat <<BODY
Fedora ${after} is released, so Fedora ${current} stops getting
security updates about four weeks after that. Every Nexus machine is
on Fedora ${current}.

Move now: image/README.md, "Fedora bump order". Change ARG
FEDORA_VERSION in image/Containerfile, build --target base, run
./image/generate-lists.sh, read the diff, build, test on the VM and the
Asus, publish.
BODY
)"
elif released "${next}" && exists "${next}"; then
    open_issue "Fedora ${next} is released" "$(cat <<BODY
Fedora ${next} is released and its bootc image exists. Nothing is
broken: the weekly image keeps Fedora ${current} updated, and Fedora
${current} is supported until about four weeks after Fedora ${after}.
Plan the move with image/README.md, "Fedora bump order".
BODY
)"
else
    echo "Nothing released after Fedora ${current}."
fi
