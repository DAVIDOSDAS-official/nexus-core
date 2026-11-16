#!/usr/bin/env bash
# The plants below are Python with GitHub's ${{ }} in them, meant literally.
# shellcheck disable=SC2016
#
# The release environment's guard (0.2.2, ledger item 2, round-4 audit H1).
#
#     tests/release-guard-check.sh <repository root>
#
# 1. tools/check-release-environment.py: passes GitHub's answer only when
#    "release" has a required reviewer; every other answer fails.
# 2. tools/check-workflow-secrets.py: the real workflows pass; copies with
#    the key moved to another job, used before the guard, used outside a
#    job, or handed over with every secret, fail.
#
# Reads and writes only a scratch folder; asks GitHub nothing.
set -u
ROOT="${1:-}"
if [ -z "${ROOT}" ] || [ ! -d "${ROOT}/.github/workflows" ]; then
    echo "Use: $0 <repository root>" >&2
    exit 2
fi
ROOT="$(cd "${ROOT}" && pwd)"
JUDGE="${ROOT}/tools/check-release-environment.py"
LINT="${ROOT}/tools/check-workflow-secrets.py"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
failed=0

judge() {   # judge <expected exit> <label> <json>
    printf '%s' "$3" > "${WORK}/answer.json"
    python3 "${JUDGE}" "${WORK}/answer.json" > "${WORK}/said" 2>&1
    got=$?
    if [ "${got}" -ne "$1" ]; then
        echo "judge, $2: exit ${got}, expected $1"; sed 's/^/    /' "${WORK}/said"; failed=1
    fi
}

# GitHub's answer when a reviewer is set (shape from its REST docs).
REVIEWER='{"type":"required_reviewers","reviewers":[{"type":"User","reviewer":{"login":"DAVIDOSDAS-official"}}]}'
judge 0 "a reviewer" "{\"name\":\"release\",\"protection_rules\":[${REVIEWER}],\"deployment_branch_policy\":{\"protected_branches\":true,\"custom_branch_policies\":false}}"
judge 0 "a reviewer and a wait timer" "{\"name\":\"release\",\"protection_rules\":[{\"type\":\"wait_timer\",\"wait_timer\":5},${REVIEWER}],\"deployment_branch_policy\":null}"
judge 0 "a team as reviewer" '{"name":"release","protection_rules":[{"type":"required_reviewers","reviewers":[{"type":"Team","reviewer":{"slug":"maintainers"}}]}]}'
judge 1 "no rules" '{"name":"release","protection_rules":[],"deployment_branch_policy":null}'
judge 1 "only a wait timer" '{"name":"release","protection_rules":[{"type":"wait_timer","wait_timer":30}]}'
judge 1 "a reviewer rule with nobody in it" '{"name":"release","protection_rules":[{"type":"required_reviewers","reviewers":[]}]}'
judge 1 "rules missing" '{"name":"release"}'
judge 1 "another environment" "{\"name\":\"staging\",\"protection_rules\":[${REVIEWER}]}"
judge 1 "GitHub refused" '{"message":"Not Found","documentation_url":"https://docs.github.com/rest"}'
judge 1 "not JSON" 'Bad gateway'
judge 1 "empty" ''
judge 1 "a list" '[]'

# The policy warning is said, and does not fail.
printf '%s' "{\"name\":\"release\",\"protection_rules\":[${REVIEWER}],\"deployment_branch_policy\":null}" > "${WORK}/answer.json"
if ! python3 "${JUDGE}" "${WORK}/answer.json" | grep -q "warning: no deployment branch policy"; then
    echo "judge: no warning without a deployment branch policy"; failed=1
fi

# The workflows as they are.
if ! python3 "${LINT}" "${ROOT}" > "${WORK}/lint" 2>&1; then
    echo "lint: the repository's own workflows fail:"; sed 's/^/    /' "${WORK}/lint"; failed=1
fi

# Planted copies: each must fail.
plant() {   # plant <label> <python that edits weekly-image.yml text in s>
    rm -rf "${WORK}/tree"; mkdir -p "${WORK}/tree/.github" "${WORK}/tree/tools"
    cp -r "${ROOT}/.github/workflows" "${WORK}/tree/.github/"
    cp "${JUDGE}" "${WORK}/tree/tools/"
    python3 - "${WORK}/tree/.github/workflows/weekly-image.yml" "$2" <<'EOF'
import sys
path, code = sys.argv[1], sys.argv[2]
s = open(path).read()
before = s
exec(code)
assert s != before, "the plant changed nothing"
open(path, "w").write(s)
EOF
    if python3 "${LINT}" "${WORK}/tree" > "${WORK}/lint" 2>&1; then
        echo "lint did not catch: $1"; failed=1
    fi
}
plant "the guard taken out" \
    's = s.replace("python3 tools/check-release-environment.py", "true")'
plant "the guard after the key's first use" \
    'i = s.index("      - name: Check the release environment is guarded"); j = s.index("      - name: Anything to sign?"); block = s[i:j]; s = s[:i] + s[j:]; k = s.index("      - name: Log in to GitHub Container Registry", s.index("\n  sign:\n")); s = s[:k] + block + s[k:]'
plant "the environment taken off the job" \
    's = s.replace("    environment: release\n", "", 1)'
plant "the key in another job" \
    's = s.replace("  build:\n", "  build:\n    env:\n      LEAK: ${{ secrets.COSIGN_PRIVATE_KEY }}\n", 1)'
plant "the key workflow-wide" \
    's = s.replace("permissions: {}\n", "permissions: {}\nenv:\n  KEY: ${{ secrets.COSIGN_PASSWORD }}\n", 1)'
plant "every secret at once" \
    's = s.replace("  build:\n", "  build:\n    env:\n      ALL: ${{ toJSON(secrets) }}\n", 1)'
plant "the key by index" \
    's = s.replace("  build:\n", "  build:\n    env:\n      K: ${{ secrets['"'"'COSIGN_PASSWORD'"'"'] }}\n", 1)'

if [ "${failed}" -ne 0 ]; then
    exit 1
fi
echo "ok: signing stops without a reviewer on 'release'; the key is used only after that check, only in that environment"
