#!/usr/bin/env python3
"""Is the `release` environment still guarded? (0.2.2, ledger item 2, R4-H1)

    tools/check-release-environment.py <answer.json>

<answer.json> is what GitHub answers for
GET /repos/<owner>/<repo>/environments/release. The signing job of
weekly-image.yml asks for it as its first step and gives it to this
script, before the job touches the signing key.

Why: the key and its password are secrets of the `release` environment,
and what keeps a workflow run from using them without the maintainer
saying yes is a GitHub setting -- a required reviewer on that
environment. Settings are not in the repository and change without a
commit. If the rule were ever lost (removed by mistake, or by someone
who got into the account settings but not the code), every run would
sign without asking. This makes that fail closed: no reviewer rule, no
signing.

What it cannot do: someone who can change the workflow can also remove
this step. That is guarded by tools/check-workflow-secrets.py in the
tests, and in the end by who may push to the repository.

Passes only when the environment is called "release" and has a
required_reviewers rule with at least one reviewer. Anything else --
no rule, an empty list, an answer that is not the expected JSON, an
error from GitHub -- fails. A deployment branch policy is checked too,
but only warned about: without one, a run from any branch may ask.
Exit codes: 0 guarded, 1 not guarded, 2 used wrongly.
"""
import json
import sys


def judge(answer):
    """(ok, lines): whether the environment is guarded, and what to say."""
    if not isinstance(answer, dict):
        return False, ["GitHub's answer is not an environment."]
    if "message" in answer and "protection_rules" not in answer:
        return False, ["GitHub answered: %s" % str(answer.get("message"))[:200]]
    if answer.get("name") != "release":
        return False, ["The environment is %r, not 'release'." % (answer.get("name"),)]

    rules = answer.get("protection_rules")
    if not isinstance(rules, list):
        return False, ["The environment has no protection rules."]

    reviewers = []
    for rule in rules:
        if not isinstance(rule, dict) or rule.get("type") != "required_reviewers":
            continue
        for entry in rule.get("reviewers") or []:
            if not isinstance(entry, dict):
                continue
            who = entry.get("reviewer") or {}
            name = who.get("login") or who.get("slug") or who.get("name")
            if name:
                reviewers.append("%s (%s)" % (name, entry.get("type", "?")))

    if not reviewers:
        return False, [
            "The 'release' environment has no required reviewer.",
            "Every run could use the signing key without anyone saying yes.",
            "Settings -> Environments -> release -> Required reviewers: add yourself.",
        ]

    lines = ["Signing waits for a yes from: " + ", ".join(reviewers) + "."]
    if not answer.get("deployment_branch_policy"):
        lines.append("warning: no deployment branch policy; a run from any branch may "
                     "ask to sign. Settings -> Environments -> release -> Deployment "
                     "branches: protected branches only.")
    return True, lines


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    try:
        with open(argv[1], encoding="utf-8") as answer_file:
            answer = json.load(answer_file)
    except (OSError, ValueError) as error:
        print("Could not read GitHub's answer: %s" % error)
        print("Not signing: the environment's protection could not be seen.")
        return 1
    ok, lines = judge(answer)
    for line in lines:
        print(line)
    if not ok:
        print("Not signing.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
