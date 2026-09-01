# Nexus

A package tool that explains itself.

Nexus reasons about a Linux system in terms of **capabilities** rather
than package names — what you want the machine to be able to do,
rather than which packages happen to provide it — and it says why it
chose what it chose. It works on Debian-family and Fedora-family
systems, from the same profiles, without being told which one it is
on.

It is not a package manager. It decides and explains; `apt` and `dnf`
carry out the work.

---

## What it does

**Shows the options, with what each costs.**

```
$ nexus options awk
Capability:  awk
Options:     3

  mawk  1.3.4.20240123-1build1   [installed]
      4 components, already here
  original-awk  2023-11-27-1
      4 components, 1 of them new
  gawk  1:5.2.1-2build3
      10 components, 2 of them new
```

**Explains a failure instead of reporting one.**

```
$ apt-get install steam-installer
E: Unable to correct problems, you have held broken packages.

$ nexus install steam-installer
Blocked on:
    libgl1:i386 is 1.4.0-1 but libgl1:amd64 is 1.7.0-2101~22.04.
    Multi-Arch: same builds share files and must be the same version.
```

Same fact. One of them tells you that a third-party repository pinned
to `amd64` is why Steam will not install.

**Checks a machine.**

```
$ nexus doctor
  [ ok ]  Package database  3046 components read from dpkg status.
  [ ok ]  Hardware          6 capabilities detected.
  [ ok ]  Dependencies      Every requirement is satisfied.
  [ ok ]  Conflicts         No installed component collides with another.
  [warn]  Unused            4 component(s) nothing needs.
```

**Describes a system as intent, not as a package list.**

```
Profile: minimalism
Requires: desktop-session, display-manager, terminal-emulator,
 file-manager, text-editor, web-browser, document-viewer,
 image-viewer, archive-manager, system-settings, screenshot-tool,
 audio-server, network-applet, volume-applet, desktop-portal
Prefers: desktop-session=plasma-desktop, terminal-emulator=konsole
```

That profile resolves to `plasma-workspace`, `konsole`, `dolphin` and
`kate` on Fedora, and to whatever the equivalents are on Debian. The
same file works on both, and `nexus image <profile>` turns it into an
annotated package list a container build can consume.

**Installs and removes, once the plan has been verified.**

```
$ sudo nexus install gamemode --apply
Verified:  apt would do the same thing.
Would install:
    gamemode
Proceed? [y/N]
```

---

## Commands

| | |
|---|---|
| `nexus scan` | what is installed, and how much of the metadata the model holds |
| `nexus doctor` | check a machine: dependencies, conflicts, unused, hardware |
| `nexus options <capability>` | every way to satisfy something, and what each costs |
| `nexus solve <capability>` | resolve it, with a reason for every component chosen |
| `nexus why <component>` | what pulled this in |
| `nexus inspect <component>` | version, architecture, what it provides and needs |
| `nexus conflicts` | which declared conflicts are live |
| `nexus plan <capability>` | the order things must be applied in |
| `nexus remove <component>` | what removing it would take with it |
| `nexus install <capability>` | a verified plan, and `--apply` to carry it out |
| `nexus hardware` | GPU, firmware, Secure Boot, chassis, as capabilities |
| `nexus profile list / show / check` | what a machine can do, against a named intent |
| `nexus image <profile>` | the package list a profile resolves to |
| `nexus history` | what Nexus has changed |

Every command reads by default. Only `--apply` writes.

---

## How it is verified

309 tests, and — more usefully — **differential checks against the
tools that already know the answer**:

| Question | Checked against |
|---|---|
| Debian version ordering | `dpkg --compare-versions`, ~1,800 pairs |
| RPM version ordering | `rpm.vercmp`, ~800 pairs |
| Which packages are unused | `apt autoremove` |
| Whether a plan will work | `apt-get install --dry-run` |

The last one is an interlock rather than a diagnostic: `--apply`
refuses a plan the package manager will not agree to. It has already
refused two plans Nexus itself considered perfect.

Nearly every serious bug in this project was found by computing a
number, finding it implausible, and comparing it against a native
tool. Almost none were found by a failing test. The suite catches
regressions; it rarely catches a wrong answer nobody thought to
assert.

---

## What it does not do

- **It does not mix distributions.** Binary packages from different
  distributions cannot share a root filesystem: different file
  layouts, different libc builds, package databases that do not know
  about each other. What does move between them is recipes and
  isolated runtimes — Flatpak, containers, Nix — and Nexus does not
  model those yet.
- **No services.** Nexus has no concept of a running service, so
  "audit what actually runs" is outside it.
- **No conditional dependencies.** RPM's `(a if b)` is recorded as an
  unrepresented gap rather than evaluated. On a Fedora base that is
  0.4% of dependency clauses, and `nexus scan` reports the count.
- **No rollback of its own.** On an image-based system the previous
  deployment provides it; on a traditional install it does not exist.
- **Fedora writes are not implemented.** Plans are verified against
  `apt` only, so `--apply` refuses on an rpm system rather than
  proceeding unverified.

---

## Building

```
cmake -S . -B build && cmake --build build
ctest --test-dir build
./build/cli/nexus doctor
```

Needs a C++20 compiler and CMake 3.20. GoogleTest for the tests;
`-DNEXUS_BUILD_TESTS=OFF` builds without it. `dpkg`, `rpm`, `zstd` and
`unzck` are used when present and reported when absent.

## Layout

```
core/        the model: capabilities, resolution, conflicts, ordering
system/      reading real systems: dpkg, apt, rpm, repodata, protection rules
hardware/    what the machine is, read from sysfs
components/  profiles and per-ecosystem aliases — data, not code
image/       a bootc image definition built from a profile
docs/        specifications, and the decisions behind them
```

`docs/specifications/00-decisions.md` is the one worth reading: the
reasoning that is not recoverable from the code.
