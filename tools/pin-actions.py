#!/usr/bin/env python3
"""Pin every GitHub Action in .github/workflows/ to a full commit SHA.

    python3 tools/pin-actions.py            show what would change
    python3 tools/pin-actions.py --apply    write it

`uses: actions/checkout@v5` becomes
`uses: actions/checkout@<40-hex sha> # v5`. A tag can be moved to other
code by whoever controls the action's repository (tj-actions/
changed-files, March 2025); a commit cannot. The weekly job holds the
signing key, so it runs nothing that can change under it (0.1.82,
outside review). Dependabot (.github/dependabot.yml) proposes updates
and keeps the comment in step.

Resolved with `git ls-remote`: no GitHub login needed.
"""

import pathlib
import re
import subprocess
import sys

USES = re.compile(r"^(?P<lead>\s*-?\s*uses:\s*)(?P<action>[\w.-]+/[\w.-]+(?:/[\w./-]+)?)@(?P<ref>[\w.-]+)(?P<tail>.*)$")
SHA = re.compile(r"^[0-9a-f]{40}$")


def ls_remote(owner_repo, ref):
    """The commit a tag (annotated or not) or branch points at."""
    url = "https://github.com/%s.git" % owner_repo
    out = subprocess.run(
        ["git", "ls-remote", url, "refs/tags/%s" % ref, "refs/tags/%s^{}" % ref, "refs/heads/%s" % ref],
        capture_output=True, text=True, timeout=60, check=True).stdout
    found = {}
    for line in out.splitlines():
        sha, name = line.split("\t")
        found[name] = sha
    for key in ("refs/tags/%s^{}" % ref, "refs/tags/%s" % ref, "refs/heads/%s" % ref):
        if key in found:
            return found[key]
    raise LookupError("%s has no tag or branch %s" % (owner_repo, ref))


def pin_text(text, resolve):
    changes = []
    lines = text.splitlines(keepends=True)
    for i, line in enumerate(lines):
        m = USES.match(line.rstrip("\n"))
        if not m or SHA.match(m.group("ref")) or m.group("action").startswith("./"):
            continue
        owner_repo = "/".join(m.group("action").split("/")[:2])
        sha = resolve(owner_repo, m.group("ref"))
        tail = m.group("tail")
        new = "%s%s@%s # %s" % (m.group("lead"), m.group("action"), sha, m.group("ref"))
        if tail.strip() and not tail.strip().startswith("#"):
            new += tail
        lines[i] = new + ("\n" if line.endswith("\n") else "")
        changes.append((m.group("action"), m.group("ref"), sha))
    return "".join(lines), changes


def main(argv):
    apply = "--apply" in argv
    root = pathlib.Path(__file__).resolve().parent.parent / ".github" / "workflows"
    cache = {}

    def resolve(owner_repo, ref):
        if (owner_repo, ref) not in cache:
            cache[(owner_repo, ref)] = ls_remote(owner_repo, ref)
        return cache[(owner_repo, ref)]

    total = 0
    for path in sorted(root.glob("*.yml")):
        text = path.read_text()
        new, changes = pin_text(text, resolve)
        for action, ref, sha in changes:
            print("%-28s %s@%s -> %s" % (path.name, action, ref, sha))
        total += len(changes)
        if apply and changes:
            path.write_text(new)
    print("%d to pin%s" % (total, "" if apply else " (dry run; --apply writes)"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
