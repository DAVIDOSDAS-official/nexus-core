# Nexus

A package tool that explains itself.

Nexus reasons about a Linux system in terms of **capabilities** — what
you want the machine to be able to do, rather than which packages
provide it — and it says why it chose what it chose. It works on
Debian-family and Fedora-family systems from the same profiles,
without being told which one it is on.

It is not a package manager. It decides and explains; `apt` and `dnf`
carry out the work.

---

## What it does

**Explains a failure instead of reporting one.**

```
$ apt-get install steam-installer
E: Unable to correct problems, you have held broken packages.

$ nexus install steam-installer
Blocked on:
    libgl1:i386 is 1.4.0-1 but libgl1:amd64 is 1.7.0-2101~22.04.
    Multi-Arch: same builds share files and must be the same version.
```

Same fact. One of them tells you that a third-party graphics
repository pinned to 64-bit is why Steam will not install.

**Shows what a choice costs, before you make it.**

```
$ nexus options video-editor
  kdenlive  21.12.3   [installed]       654 components, already here
  org.kde.kdenlive  26.08.0  (flatpak)  107 MB to fetch
  kdenlive  26.08.0-1  (container)      414 components, 1.9 GB on disk
```

The same program three ways. The container costs 1.9 GB because a
container is a whole distribution.

**Checks a machine.**

```
$ nexus doctor
  [ ok ]  Package database  3046 components read from dpkg status.
  [ ok ]  Hardware          6 capabilities detected.
  [ ok ]  Encryption        Root and swap encrypted (luks). This
                            protects the machine when it is off, not
                            while it is running.
  [ ok ]  Dependencies      Every requirement is satisfied.
  [ ok ]  Conflicts         No installed component collides.
  [warn]  Unused            4 component(s) nothing needs.
```

**Describes a machine as intent, not as a package list.**

```
Profile: minimalism
Requires: desktop-session, display-manager, terminal-emulator,
 file-manager, text-editor, web-browser, audio-server, ...
Prefers: desktop-session=plasma-desktop, terminal-emulator=konsole
```

That resolves to `plasma-workspace`, `konsole`, `dolphin` on Fedora
and to the equivalents on Debian. `nexus image <profile>` turns it
into a package list a container build consumes.

---

## Commands

| | |
|---|---|
| `nexus guide` | how to work this system, on this system |
| `nexus doctor` | dependencies, conflicts, unused, encryption, interrupted changes |
| `nexus setup [profile...]` | what this machine could be, and what each costs here |
| `nexus options <capability>` | every way to satisfy it, with costs |
| `nexus install <thing>` | a verified plan; `--apply` to carry it out |
| `nexus remove <thing>` | what removing it would take with it |
| `nexus source <package>` | what building it from source would cost |
| `nexus container <package>` | another distribution's package, in a container |
| `nexus services` | what starts at boot, and which package installed each |
| `nexus largest [n]` | what is taking up the room; `--unused` for what is not needed |
| `nexus hardware` / `nexus secureboot` | what this machine is |
| `nexus vpn config ...` | generate a WireGuard tunnel |
| `nexus why` / `inspect` / `conflicts` / `plan` / `history` | the rest |

Every command reads by default. Only `--apply` writes. `--commands`
prints what would be run instead of running it.

---

## Where software can come from

Six sources, each labelled with its trade-off rather than ranked:

| Source | What it means |
|---|---|
| **Base** (apt / dnf) | integrated, smallest, moves with the distribution |
| **Flatpak** | current and sandboxed, larger, weaker desktop integration |
| **Snap** | confined and self-updating, slower to start, one store |
| **Container** (Arch, BlackArch…) | another distribution's package, behind a boundary |
| **Nix** | any version, side by side, its own model to learn |
| **Portage** (Gentoo) | built from source, with your own flags |

Five metadata formats, one model: RFC822 stanzas, compressed XML,
tar'd `%FIELD%` blocks, whitespace tables, and `KEY=value` with a
nested dependency grammar.

---

## How it is verified

**493 tests**, and — more usefully — **differential checks against the
tools that already know the answer**:

| Question | Checked against |
|---|---|
| Debian version ordering | `dpkg --compare-versions`, ~1,800 pairs |
| RPM version ordering | `rpm.vercmp`, ~800 pairs |
| Which packages are unused | `apt autoremove` |
| Whether a plan will work | `apt-get install --dry-run` |

That last one is an interlock, not a diagnostic: `--apply` refuses a
plan the package manager will not agree to.

Plus a suite of deliberately hostile input — truncated databases,
binary garbage, directories where files should be, 50,000-deep XML —
which found two real bugs the day it was written, and a suite for
interrupted changes.

Nearly every serious bug here was found by computing a number, finding
it implausible, and comparing it against a native tool. Almost none
were found by a failing test. The suite catches regressions; it rarely
catches a wrong answer nobody thought to assert.

---

## What it does not do

- **It does not update.** `apt` and `dnf` do that correctly, and a
  second implementation would only be a way to get it wrong.
- **It does not unpack, configure or remove files.** Same reason.
- **It does not mix distributions in one filesystem.** Nothing can:
  different file layouts, different libc builds, package databases
  that do not know about each other. What does move between them is
  isolated runtimes, which Nexus offers as sources.
- **It cannot make a binary system source-based.** That is decided by
  the base, not by a tool above it.
- **It does not sign anything yet**, so its images ship an unsigned
  kernel. `nexus secureboot` explains the real options rather than
  telling you to turn Secure Boot off.
- **No conditional dependencies.** RPM's `(a if b)` is recorded as an
  unrepresented gap; Gentoo's USE conditions are evaluated against
  flag defaults and the count of assumptions is reported.

---

## The state of it

A working tool, and one bootable image built from a profile.

The image is a Fedora bootc build whose package list is generated from
the `minimalism` profile. It boots through UEFI, reaches KDE, and asks
on first boot what the machine should be — then reports that it
already is what it was built to be, because the same profile produced
both. `minimal` and `minimalism` build from the same Containerfile
with one argument changed, at 2.88 GB and 3.91 GB.

**Not yet a distribution anyone can install**: no partitioning, no
signed kernel, nothing published.

---

## Building

```
cmake -S . -B build && cmake --build build
ctest --test-dir build
./build/cli/nexus doctor
```

Needs a C++20 compiler and CMake 3.20; GoogleTest for the tests
(`-DNEXUS_BUILD_TESTS=OFF` to skip). `dpkg`, `rpm`, `flatpak`, `snap`,
`nix`, `distrobox`, `podman`, `zstd` and `wg` are used when present and
reported when absent.

## Layout

```
core/        the model: capabilities, resolution, conflicts, ordering
system/      reading real systems: dpkg, apt, rpm, pacman, portage,
             flatpak, snap, nix, containers, services, transactions
hardware/    what the machine is: devices, encryption, secure boot
components/  profiles and per-ecosystem aliases — data, not code
image/       a bootc image definition built from a profile
docs/        specifications, decisions, and where this is going
```

`docs/specifications/00-decisions.md` is the one worth reading: the
reasoning that is not recoverable from the code. `docs/overview.md`
describes the project in prose; `docs/vision.md` is where it is going.
