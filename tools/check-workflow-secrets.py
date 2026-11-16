#!/usr/bin/env python3
"""Where may the signing key be used? (0.2.2, ledger item 2, R4-H1)

    tools/check-workflow-secrets.py [repository root]

The key and its password are secrets of the `release` environment
(COSIGN_PRIVATE_KEY, COSIGN_PASSWORD). This fails when a workflow:

  - names a COSIGN_ secret in a job that does not run in the `release`
    environment, or outside any job (workflow-wide env);
  - uses one in a job before that job has run
    tools/check-release-environment.py (the step that stops signing when
    the environment has lost its required reviewer);
  - hands every secret over at once (`secrets: inherit`,
    `toJSON(secrets)`), which would include them.

Read as text, by indentation, as GitHub writes these files (two spaces
per level): no YAML library is needed on the machine running the tests.
Run by ctest (ReleaseGuard) and so by the Tests workflow on every push.
"""
import glob
import os
import re
import sys

JOB = re.compile(r"^  ([A-Za-z0-9_-]+):\s*(#.*)?$")
COSIGN = re.compile(r"secrets\s*(\.\s*|\[\s*['\"])COSIGN_")
ALL_SECRETS = re.compile(r"secrets:\s*inherit|toJSON\(\s*secrets\s*\)", re.IGNORECASE)
ENVIRONMENT = re.compile(r"^    environment:\s*(['\"]?)release\1\s*(#.*)?$")
ENVIRONMENT_NAME = re.compile(r"^      name:\s*(['\"]?)release\1\s*(#.*)?$")
GUARD = "tools/check-release-environment.py"


def problems_in(path, text):
    found = []
    lines = text.splitlines()
    name = os.path.basename(path)

    for number, line in enumerate(lines, 1):
        if ALL_SECRETS.search(line) and not line.lstrip().startswith("#"):
            found.append("%s:%d: every secret handed over at once: %s"
                         % (name, number, line.strip()))

    # Split into jobs.
    jobs = []          # (job name, first line number, lines)
    in_jobs = False
    for number, line in enumerate(lines, 1):
        if re.match(r"^jobs:\s*(#.*)?$", line):
            in_jobs = True
            continue
        if in_jobs and line and not line.startswith(" ") and not line.startswith("#"):
            in_jobs = False
        match = JOB.match(line) if in_jobs else None
        if match:
            jobs.append((match.group(1), number, []))
            continue
        if in_jobs and jobs:
            jobs[-1][2].append((number, line))
        elif COSIGN.search(line) and not line.lstrip().startswith("#"):
            found.append("%s:%d: a COSIGN_ secret outside any job: %s"
                         % (name, number, line.strip()))

    for job, start, body in jobs:
        uses = [(n, l) for n, l in body
                if COSIGN.search(l) and not l.lstrip().startswith("#")]
        if not uses:
            continue
        guarded_env = any(ENVIRONMENT.match(l) for _, l in body)
        if not guarded_env:
            # environment:\n      name: release
            for i, (_, l) in enumerate(body):
                if re.match(r"^    environment:\s*(#.*)?$", l) and i + 1 < len(body) \
                        and ENVIRONMENT_NAME.match(body[i + 1][1]):
                    guarded_env = True
        if not guarded_env:
            found.append("%s:%d: job '%s' uses a COSIGN_ secret but does not run in "
                         "the 'release' environment" % (name, uses[0][0], job))
        guard_at = next((n for n, l in body
                         if GUARD in l and not l.lstrip().startswith("#")), None)
        if guard_at is None:
            found.append("%s:%d: job '%s' uses a COSIGN_ secret and never runs %s"
                         % (name, uses[0][0], job, GUARD))
        elif guard_at > uses[0][0]:
            found.append("%s:%d: job '%s' uses a COSIGN_ secret before %s (line %d)"
                         % (name, uses[0][0], job, GUARD, guard_at))
    return found


def main(argv):
    root = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(__file__), "..")
    files = sorted(glob.glob(os.path.join(root, ".github", "workflows", "*.yml")) +
                   glob.glob(os.path.join(root, ".github", "workflows", "*.yaml")))
    if not files:
        print("no workflows found under %s" % root)
        return 1
    if not os.path.isfile(os.path.join(root, GUARD)):
        print("%s is missing" % GUARD)
        return 1
    found = []
    for path in files:
        with open(path, encoding="utf-8") as handle:
            found += problems_in(path, handle.read())
    for problem in found:
        print(problem)
    print("%d problem(s) with where the signing key may be used" % len(found))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
