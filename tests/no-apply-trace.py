#!/usr/bin/env python3
"""Reads one strace log of one `nexus` command and says what it changed.

Used by tests/no-apply-changes-nothing.sh (0.1.88, review of patch 113).
The log is from

    strace -f -y -qq -s 512 -e trace=<the calls below> nexus <command>

so it holds every program the command started, with its full path and
arguments, and every attempt to create, open for writing, rename,
remove or re-permission a file -- by nexus or by anything it started --
including attempts that failed. A failed attempt counts: a command run
as an ordinary user that tries to write to /etc and is refused would
have written as root.

Prints one line per finding and exits 1 if there is any; prints
nothing and exits 0 otherwise. With --summary, also prints what was
seen (programs, places written to) on the last line, for the caller.

    no-apply-trace.py <log> --nexus <path> --stubs <dir> --allow <prefix>...
"""
import argparse
import os
import re
import sys

# Programs that only read. A program that is not here, not a stand-in
# and not nexus itself is a finding ("not classified"): the list is the
# test's claim about what a preview may run, so it grows only by
# someone deciding that a program belongs on it.
READ_ONLY = {
    # shells and the words a shell command is made of
    "sh", "bash", "dash", "env", "timeout", "nice", "true", "false", "test", "[",
    "echo", "printf", "cat", "head", "tail", "cut", "sort", "uniq", "tr", "wc",
    "grep", "egrep", "fgrep", "awk", "gawk", "mawk", "sed", "xargs",
    "basename", "dirname", "readlink", "realpath", "expr", "seq", "sleep",
    "command", "which", "nproc", "date", "id", "whoami", "uname", "hostname",
    "getent", "locale", "tput", "stty", "tty", "file", "mktemp",
    # looking at files and disks
    "ls", "stat", "find", "df", "du", "free", "lsblk", "blkid", "findmnt",
    "lscpu", "lspci", "lsusb", "lsmod", "modinfo", "dmidecode", "lsmem",
    # looking at packages and what apt runs to read its own lists
    "rpm", "dpkg", "dpkg-query", "apt-cache", "apt-config", "lz4", "zstd",
    "gzip", "gunzip", "zcat", "xz", "bzip2", "pacman-conf",
    # looking at the network and the system
    "ss", "ip", "getenforce", "sestatus", "ps", "pgrep", "uptime", "nexus",
    "od", "mountpoint", "lsb_release", "getconf", "systemd-detect-virt", "udevadm",
}

# ...unless called like this. Program name -> pattern over its arguments.
CHANGING_ARGUMENTS = {
    "sed": r"(^| )(-i|--in-place)",
    "find": r"(^| )(-delete|-exec|-execdir|-ok|-fprint)",
    "xargs": r"(^| )(rm|mv|cp|chmod|chown|ln|dd|tee|truncate)( |$)",
    "ip": r"(^| )(add|del|delete|set|flush|replace|change|append)( |$)",
    "rpm": r"(^| )(-e|-i|-U|-F|--erase|--install|--upgrade|--freshen|--import|--rebuilddb|--initdb|--setperms|--setugids|--restore)( |$)|^-[a-zA-Z]*[eiUF]",
    "dpkg": r"(^| )(-i|-r|-P|--install|--remove|--purge|--configure|--unpack|--set-selections|--clear-avail|--add-architecture|--remove-architecture)( |$)",
    "hostname": r"(^| )[^- ][^ ]*$",    # a bare word sets the name
    "date": r"(^| )(-s|--set)",
    "udevadm": r"^(trigger|control|settle|hwdb --update|test-builtin)",
}

# System calls that are a change whatever their arguments.
ALWAYS_A_CHANGE = {
    "mount", "umount2", "reboot", "init_module", "finit_module", "delete_module",
    "sethostname", "setdomainname", "settimeofday", "clock_settime", "clock_adjtime",
    "adjtimex", "swapon", "swapoff", "kexec_load", "kexec_file_load", "pivot_root",
    "chroot", "mknod", "mknodat", "acct", "quotactl",
}
# adjtimex and clock_adjtime are also how a program READS the clock's
# state; only a call that sets something counts (modes != 0).
READ_FORMS = {
    "adjtimex": r"modes=0[,}]",
    "clock_adjtime": r"modes=0[,}]",
}

# Calls that name one or two files they change.
ONE_PATH = {"unlink", "rmdir", "mkdir", "chmod", "chown", "lchown", "truncate",
            "creat", "setxattr", "lsetxattr", "removexattr", "lremovexattr"}
ONE_PATH_AT = {"unlinkat", "mkdirat", "fchmodat", "fchmodat2", "fchownat"}
# Calls on an open file, which strace -y shows with its path.
BY_DESCRIPTOR = {"fchmod", "fchown"}
TWO_PATHS = {"rename", "link", "symlink"}
TWO_PATHS_AT = {"renameat", "renameat2", "linkat", "symlinkat"}
OPENS = {"open", "openat", "openat2"}
WRITE_FLAGS = re.compile(r"O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND|O_TMPFILE")

# One program that is many: which one runs is decided by the name it
# was started by (argv[0]), not by the file. /usr/sbin/modinfo is a link
# to /usr/bin/kmod and runs as modinfo; /usr/sbin/modprobe is the same
# file and runs as modprobe. Judged by the started name, so modinfo is
# read-only and modprobe meets the stand-in rule. kmod run as itself is
# not classified.
MULTI_CALL = {"kmod", "busybox", "toybox"}

# An open with O_TMPFILE makes a file with no name: it is gone when it is
# closed, and the only way to keep it is linkat, which is checked as a
# change of its own. So it changes nothing, and in these folders it is
# noted, not a finding.
UNNAMED_TEMP_FOLDERS = ("/tmp", "/var/tmp", "/dev/shm")

STRING = r'"((?:[^"\\]|\\.)*)"(\.\.\.)?'
DIRFD = r"(AT_FDCWD|\d+)(?:<([^>]*)>)?"


def unescape(text):
    try:
        return bytes(text, "latin-1").decode("unicode_escape").encode("latin-1").decode("utf-8", "replace")
    except (UnicodeDecodeError, UnicodeEncodeError):
        return text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("--nexus", required=True)
    parser.add_argument("--stubs", required=True)
    parser.add_argument("--allow", action="append", default=[])
    parser.add_argument("--summary", action="store_true")
    args = parser.parse_args()

    stubs = os.path.realpath(args.stubs) + "/"
    stub_names = set(os.listdir(args.stubs))
    nexus = os.path.realpath(args.nexus)
    allowed_exact = {"/dev/null", "/dev/tty", "/dev/stdout", "/dev/stderr", "/dev/zero", "/dev/full"}
    allowed_prefixes = ["/dev/pts/", "/proc/self/fd/"] + [p for p in args.allow if p]

    findings = []
    programs = set()
    written = set()
    unnamed = set()
    # pid -> the program it is running, from the last execve that worked,
    # so a finding can say who did it.
    running = {}

    def by(pid):
        return f" [by {running[pid]}]" if pid in running else f" [by pid {pid}]"

    def allowed(path):
        return path in allowed_exact or any(path.startswith(prefix) for prefix in allowed_prefixes)

    def place(dirfd, dirpath, path):
        """An absolute path, or None if it cannot be worked out."""
        if path.startswith("/"):
            return os.path.normpath(path)
        if dirpath:
            return os.path.normpath(os.path.join(dirpath, path))
        return None

    def touch(call, path, raw, pid=None):
        who = by(pid) if pid else ""
        if path is None:
            findings.append(f"{call} of a file whose place cannot be worked out: {raw.strip()[:160]}{who}")
        elif not allowed(path):
            findings.append(f"{call}: {path}{who}")
        else:
            written.add(path)

    with open(args.log, errors="replace") as log:
        for line in log:
            match = re.match(r"^\s*(\d+)\s+(\w+)\((.*)$", line)
            if not match:
                continue
            pid, call, rest = match.group(1), match.group(2), match.group(3)
            failed_to_start = re.search(r"\)\s*=\s*-1\s", line) is not None

            if call in ("execve", "execveat"):
                # Looking for a program in PATH fails many times before
                # it succeeds; only a program that started counts.
                if failed_to_start:
                    continue
                found = re.search(STRING, rest)
                if not found:
                    continue
                started_as = unescape(found.group(1))
                path = os.path.realpath(started_as)
                name = os.path.basename(path)
                argv = re.search(r"\[(.*?)\](?:, | <unfinished)", rest)
                words = [unescape(w[0]) for w in re.findall(STRING, argv.group(1))] if argv else []
                arguments = " ".join(words[1:])
                if name in MULTI_CALL:
                    name = os.path.basename(words[0] if words else started_as)
                    path = os.path.join(os.path.dirname(path), name) if name != os.path.basename(path) else path
                programs.add(path)
                running[pid] = name
                if path == nexus or path.startswith(stubs):
                    continue
                # A script's interpreter: /bin/sh /path/to/stubs/systemctl
                if name in ("sh", "bash", "dash") and words[1:] and os.path.realpath(words[1]).startswith(stubs):
                    continue
                if name in stub_names:
                    findings.append(f"ran {path} itself, past the stand-in for {name}: {arguments[:120]}")
                elif name not in READ_ONLY:
                    findings.append(f"ran a program this test has not classified: {path} {arguments[:120]}")
                elif name in CHANGING_ARGUMENTS and re.search(CHANGING_ARGUMENTS[name], arguments):
                    findings.append(f"ran {name} in a way that changes something: {name} {arguments[:120]}")
                continue

            if call in ALWAYS_A_CHANGE:
                if call in READ_FORMS and re.search(READ_FORMS[call], rest):
                    continue
                findings.append(f"system call {call}({rest.strip()[:120]}{by(pid)}")
                continue

            if call in OPENS:
                if not WRITE_FLAGS.search(rest):
                    continue
                if call == "open":
                    found = re.match(STRING, rest)
                    path = place(None, None, unescape(found.group(1))) if found else None
                else:
                    found = re.match(DIRFD + r",\s*" + STRING, rest)
                    path = place(found.group(1), found.group(2), unescape(found.group(3))) if found else None
                flags = WRITE_FLAGS.findall(rest)
                if "O_TMPFILE" in flags and path in UNNAMED_TEMP_FOLDERS:
                    unnamed.add(f"{path}{by(pid)}")
                    continue
                touch("opened for writing (" + "|".join(sorted(set(flags))) + ")", path, line, pid)
                continue

            if call in BY_DESCRIPTOR:
                found = re.match(r"\d+<([^>]*)>", rest)
                touch(call, os.path.normpath(found.group(1)) if found and found.group(1).startswith("/") else None, line, pid)
                continue

            if call in ONE_PATH:
                found = re.match(STRING, rest)
                touch(call, place(None, None, unescape(found.group(1))) if found else None, line, pid)
            elif call in ONE_PATH_AT:
                found = re.match(DIRFD + r",\s*" + STRING, rest)
                touch(call, place(found.group(1), found.group(2), unescape(found.group(3))) if found else None, line, pid)
            elif call in TWO_PATHS:
                found = re.match(STRING + r",\s*" + STRING, rest)
                if not found:
                    touch(call, None, line, pid)
                else:
                    # symlink's first argument is what the link says, not a file.
                    if call != "symlink":
                        touch(call, place(None, None, unescape(found.group(1))), line, pid)
                    touch(call, place(None, None, unescape(found.group(3))), line, pid)
            elif call in TWO_PATHS_AT:
                if call == "symlinkat":
                    found = re.match(STRING + r",\s*" + DIRFD + r",\s*" + STRING, rest)
                    touch(call, place(found.group(3), found.group(4), unescape(found.group(5))) if found else None, line, pid)
                else:
                    found = re.match(DIRFD + r",\s*" + STRING + r",\s*" + DIRFD + r",\s*" + STRING, rest)
                    if not found:
                        touch(call, None, line, pid)
                    else:
                        touch(call, place(found.group(1), found.group(2), unescape(found.group(3))), line, pid)
                        touch(call, place(found.group(5), found.group(6), unescape(found.group(7))), line, pid)

    seen = set()
    for finding in findings:
        if finding not in seen:
            seen.add(finding)
            print(finding)
    if args.summary:
        names = sorted({os.path.basename(p) for p in programs})
        print("SUMMARY programs=" + ",".join(names) + " written=" + ",".join(sorted(written))
              + " unnamed=" + ";".join(sorted(unnamed)))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
