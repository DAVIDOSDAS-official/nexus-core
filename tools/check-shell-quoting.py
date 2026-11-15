#!/usr/bin/env python3
"""Fail if a value is joined into a shell command without shellQuote.

Run by ctest (tests/CMakeLists.txt) on every build. Every runCommand(),
std::system() and popen() call is read up to its ';'; shellQuote(...)
and std::to_string(...) are taken out; any other `+ name` left is a
value going into /bin/sh as it is. Names known to be fixed text are in
ALLOWED -- adding one is a decision to read the code for, not a way to
make the check pass. (0.1.84: outside reviews found unquoted repository
metadata in rpm-ostree layering and a hand-quoted locale.)
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
    ('cli/src/dualboot.cpp', 'options'): 1,
    ('cli/src/dualboot.cpp', 'std::string(kGrubEnv)'): 1,
    ('cli/src/security.cpp', 'nexus::system::kHardeningConf'): 1,
    ('cli/src/security.cpp', 'z'): 2,
    ('cli/src/server.cpp', 'nexus::system::serverUnit(name)'): 1,
    ('cli/src/server.cpp', 'nexus::system::serverUnit(options.name)'): 1,
    ('cli/src/server.cpp', 'nexus::system::serverUnit(words[1])'): 1,
    ('cli/src/server.cpp', 'port'): 1,
    ('cli/src/server.cpp', 'unit'): 1,
    ('cli/src/update.cpp', 'quoted'): 1,
    ('cli/src/update.cpp', 'quoted("Update available")'): 1,
    ('cli/src/update.cpp', 'std::string(shop ? "-A open=\'Open Nexus Shop\' " : "")'): 1,
    ('system/src/compression.cpp', 'errors'): 1,
    ('system/src/plan_apply.cpp', 'arguments'): 2,
    ('system/src/services.cpp', 'paths'): 2,
}

CALL = re.compile(r"(runCommand|std::system|::system|popen)\s*\(")


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
    for f in files:
        src = open(f).read()
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
    for b in bad:
        print(b)
    print("%d unquoted value(s)" % len(bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
