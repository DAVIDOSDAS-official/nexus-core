#!/usr/bin/env python3
"""Fail if a value is joined into a shell command without shellQuote.

Run by ctest (tests/CMakeLists.txt) on every build. Every runCommand(),
std::system() and popen() call is read up to its ';'; shellQuote(...)
and std::to_string(...) are taken out; any other `+ name` left is a
value going into /bin/sh as it is. Names known to be fixed text are in
ALLOWED -- adding one is a decision to read the code for, not a way to
make the check pass. (0.1.84: outside reviews found unquoted repository
metadata in rpm-ostree layering and a hand-quoted locale.)

Since 0.2.1 (ledger item 4) the files in MOVED start every program
directly, with runProgram and an argument vector. In those files any
runCommand(), std::system() or popen() fails this check, quoted or not,
and so does runProgram({"sh", ...}): a shell by another door. A call
that needs a shell on purpose goes into SHELL_ON_PURPOSE with the
reason, after reading the code.
"""
import glob
import re
import sys

# Fixed text or values checked before they get here: (file, name) and
# how many uses of it there are today (0.1.85, outside review N8: a
# global list of names let any new use of `options` or `unit` through).
# A new use, or the same name in another file, fails -- read the code,
# then raise the count here on purpose.
ALLOWED = {
    ('cli/src/cyber.cpp', 'box'): 5,
    ('cli/src/cyber.cpp', 'rootFlag'): 6,
    ('cli/src/cyber.cpp', 'tier->metapackage'): 1,
    ('cli/src/dev.cpp', 'box'): 8,
    ('cli/src/dev.cpp', 'pk'): 2,
    ('cli/src/security.cpp', 'nexus::system::kHardeningConf'): 1,
    ('cli/src/security.cpp', 'z'): 2,
    ('system/src/compression.cpp', 'errors'): 1,
    ('system/src/services.cpp', 'paths'): 2,
}

CALL = re.compile(r"(runCommand|std::system|::system|popen)\s*\(")

# Files moved off /bin/sh altogether (0.2.1, ledger item 4): the ones
# whose commands run as root, first. No shell call may come back into
# them. To move another file: change its calls, then add it here.
MOVED = (
    'system/src/plan_apply.cpp',
    'cli/src/server.cpp',
    'cli/src/dualboot.cpp',
    'cli/src/update.cpp',
    'cli/src/language.cpp',
)

# A call in a moved file that goes through a shell on purpose:
# (file, the call's first line without the spaces around it) and why
# it has to. Empty today. An entry that matches nothing fails, like an
# allowance above that is larger than its use.
SHELL_ON_PURPOSE = {
}

# runProgram({"sh", ...}), {"/bin/bash", ...}, {"env", ...} and the like.
SHELL_BY_ARGV = re.compile(
    r'runProgram\s*\(\s*\{\s*"(?:/usr/bin/|/bin/)?(?:sh|bash|dash|zsh|ksh|env|xargs)"')


def in_comment(src, at):
    """Whether position `at` is inside a // comment."""
    start = src.rfind("\n", 0, at) + 1
    return "//" in src[start:at]


def first_line(src, at):
    start = src.rfind("\n", 0, at) + 1
    end = src.find("\n", at)
    return src[start:end if end >= 0 else len(src)].strip()


def call_text(src, start):
    """From the call's '(' to its matching ')', skipping string literals."""
    depth, i, n = 0, start, len(src)
    while i < n:
        c = src[i]
        if c == '"':
            i += 1
            while i < n and src[i] != '"':
                i += 2 if src[i] == "\\" else 1
        elif c == "'" :
            i += 1
            while i < n and src[i] != "'":
                i += 2 if src[i] == "\\" else 1
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
        i += 1
    return src[start:]


def main(root):
    # Every file of the tool (0.1.96: main.cpp is one file per command
    # area now; naming it alone would have read almost nothing).
    files = sorted(glob.glob(root + "/cli/src/*.cpp"))
    for d in ("system", "hardware", "core"):
        files += glob.glob(root + "/%s/src/*.cpp" % d)
    bad = []
    seen = {}
    purposeful = set()
    for f in files:
        src = open(f).read()
        name = f[len(root) + 1:]
        if name in MOVED:
            for m in list(CALL.finditer(src)) + list(SHELL_BY_ARGV.finditer(src)):
                if in_comment(src, m.start()):
                    continue
                key = (name, first_line(src, m.start()))
                if key in SHELL_ON_PURPOSE:
                    purposeful.add(key)
                    continue
                line = src.count("\n", 0, m.start()) + 1
                bad.append("%s:%d: a shell call in a file that was moved to runProgram "
                           "with an argument vector: %s" % (name, line, key[1]))
        for m in CALL.finditer(src):
            seg = call_text(src, m.end() - 1)
            seg = re.sub(r"shellQuote\([^()]*(\([^()]*\))?[^()]*\)", "Q", seg)
            seg = re.sub(r"std::to_string\([^()]*\)", "N", seg)
            for v in re.findall(r"\+\s*([A-Za-z_][A-Za-z_0-9.:>\-\[\]]*(?:\([^()]*\))?)", seg):
                if v in ("Q", "N"):
                    continue
                key = (f[len(root) + 1:], v)
                seen[key] = seen.get(key, 0) + 1
                if seen[key] > ALLOWED.get(key, 0):
                    line = src.count("\n", 0, m.start()) + 1
                    bad.append("%s:%d: %s goes into a shell unquoted" % (f[len(root) + 1:], line, v))
    # An allowance larger than its use lets the next unquoted value of
    # that name through unseen (0.1.98: `unit` was allowed three times,
    # used once). Each must match its use exactly.
    for key, count in sorted(ALLOWED.items()):
        if seen.get(key, 0) != count:
            bad.append("%s: %s is allowed %d time(s) but used %d; set the allowance to the use"
                       % (key[0], key[1], count, seen.get(key, 0)))
    for key in sorted(SHELL_ON_PURPOSE):
        if key not in purposeful:
            bad.append("%s: SHELL_ON_PURPOSE names a call that is not there: %s" % key)
    for name in MOVED:
        if not any(f[len(root) + 1:] == name for f in files):
            bad.append("%s: listed in MOVED but not found" % name)
    for b in bad:
        print(b)
    print("%d unquoted value(s) or shell call(s) in a moved file" % len(bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
