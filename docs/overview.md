# Nexus-CORE — what it is, for the website

Written to be a source for site copy. Everything here is true of the
code as it stands; nothing describes a plan. Where something is not
built, it says so.

---

## The one-line version

**A package tool that explains itself.**

Nexus reasons about a Linux system in terms of *capabilities* — what
you want the machine to be able to do — rather than package names, and
it says why it chose what it chose.

## The two-paragraph version

Every Linux distribution asks you to learn its package manager. `apt`
on Debian, `dnf` on Fedora, `pacman` on Arch, `emerge` on Gentoo —
different commands, different names for the same software, different
answers to the same question. And none of them tell you *why*: why
this dependency, what it costs, what breaks if you remove it.

Nexus sits above all of them. You ask for a web browser, not for
`firefox`; it shows you every way to get one — from your distribution,
from Flatpak, from a container running another distribution entirely —
with what each costs in packages, megabytes and trade-offs. Then it
verifies the plan against the real package manager before doing
anything, and hands the work to the tool that already does it
correctly.

---

## What makes it different

**It explains failures instead of reporting them.**

    $ apt-get install steam-installer
    E: Unable to correct problems, you have held broken packages.

    $ nexus install steam-installer
    Blocked on:
        libgl1:i386 is 1.4.0-1 but libgl1:amd64 is 1.7.0-2101~22.04.
        Multi-Arch: same builds share files and must be the same version.

Same fact. One of them tells you that a third-party graphics
repository pinned to 64-bit is why Steam will not install.

**It shows what things cost, before you agree to them.**

    $ nexus options video-editor
    kdenlive  21.12.3   [installed]      654 components, already here
    org.kde.kdenlive  26.08.0  (flatpak) 107 MB to fetch
    kdenlive  26.08.0-1  (container)     414 components, 1.9 GB on disk

The same program, three ways, with the price of each. The container
costs 1.9 GB because a container is a whole distribution. Nobody else
puts that number in front of you.

**It never acts on a plan the package manager will not agree to.**
Every install is checked against `apt` or `dnf` first, and refused if
they disagree. That interlock has already stopped plans that Nexus
itself considered perfect.

**It is honest about what it does not know.** Sizes it cannot read are
reported as unknown rather than zero. Dependency conditions it has to
guess are counted and stated. It does not invent a compatibility
percentage or a privacy score.

---

## What it actually does

### Understanding a machine

| | |
|---|---|
| `nexus doctor` | is anything wrong? dependencies, conflicts, unused, encryption |
| `nexus services` | what starts at boot, and which package installed each |
| `nexus hardware` | GPU, firmware, Secure Boot, chassis |
| `nexus largest 20` | what is taking up the room |
| `nexus largest --unused` | what is big and needed by nothing |
| `nexus why <thing>` | what pulled this in |
| `nexus history` | what Nexus has changed |

### Deciding what to install

| | |
|---|---|
| `nexus options <capability>` | every way to get it, with costs |
| `nexus install <thing>` | what it would do, and nothing more |
| `nexus install <thing> --commands` | the exact commands, to run yourself |
| `nexus remove <thing>` | what removing it would take with it |
| `nexus source <package>` | what building it from source would cost |
| `nexus container <package>` | run another distribution's package |
| `nexus vpn config ...` | generate a WireGuard tunnel |

### Describing a whole machine

| | |
|---|---|
| `nexus setup` | what this machine could be, with what each costs here |
| `nexus profile check school,gaming` | how far it is from being both |
| `nexus image minimal` | the package list that profile resolves to |

---

## Profiles

A profile says what a machine is *for*, in capabilities rather than
packages — so the same file works on Debian, Fedora, or in a
container.

    Profile: minimalism
    Description: A full desktop kept simple: one tool per job
    Requires: desktop-session, display-manager, terminal-emulator,
     file-manager, text-editor, web-browser, audio-server, ...
    Prefers: desktop-session=plasma-desktop, terminal-emulator=konsole

Twelve of them: **base, basic, minimal, minimalism, showcase, tiling,
gaming, school, development, server, security, vpn**.

They compose. `school,gaming` is one machine that does both, and Nexus
reports which requirements the two share so the total makes sense.
Profiles that describe a *whole* machine refuse to combine and say
what to use instead — `minimal` plus anything is not minimal.

---

## Where software can come from

Six sources, each labelled with its trade-off rather than ranked:

| Source | What it means |
|---|---|
| **Base** (apt / dnf) | integrated, smallest, moves with the distribution |
| **Flatpak** | current and sandboxed, larger, weaker desktop integration |
| **Snap** | confined and self-updating, slower to start, one store |
| **Container** (Arch, BlackArch, Fedora…) | another distribution's package, behind a boundary |
| **Nix** | any version, side by side, its own model to learn |
| **Portage** (Gentoo) | built from source, with your own flags |

**On mixing distributions**, which is the question everyone asks:
binary packages from different distributions cannot share one root
filesystem. Different file layouts, different libc builds, package
databases that do not know about each other. Nothing changes that.

What *does* work is isolation. Nexus can install Metasploit — which
Ubuntu does not ship at all — from Arch, in a container, and put
`msfconsole` on your PATH. It tells you it will cost 745 MB first.

---

## Both beginners and experienced users

The same engine, two surfaces.

**A beginner** boots the machine and is asked what they want it to be,
with each option showing what it would cost on that hardware. They
choose; it explains, verifies and installs.

**An experienced user** gets `--commands`, which resolves everything
and then hands over the exact commands to run by hand. Nothing hidden,
nothing run. The appeal of assembling a system yourself is knowing
what happened, and that does not require doing the resolving by hand.

And `nexus guide` answers "how do I update this thing" *on the system
you are standing on* — `apt` on Debian, `dnf` and `bootc` on Fedora.
The answer lives in the tool rather than in a forum post that is right
somewhere else.

---

## How it is verified

**~480 tests.** More usefully, **differential checks against the tools
that already know the answer**:

| Question | Checked against |
|---|---|
| Debian version ordering | `dpkg --compare-versions`, ~1,800 pairs |
| RPM version ordering | `rpm.vercmp`, ~800 pairs |
| Which packages are unused | `apt autoremove` |
| Whether a plan will work | `apt-get install --dry-run` |

Plus a suite of deliberately hostile input — truncated databases,
binary garbage, directories where files should be, 50,000-deep XML —
which found two real bugs the day it was written.

Nearly every serious bug in this project was found by computing a
number, finding it implausible, and comparing it against a native
tool. Almost none were found by a failing test.

---

## What it does not do

- **It does not update.** `apt` and `dnf` do that, correctly. A second
  implementation would only be a way to get it wrong.
- **It does not unpack, configure or remove files.** Same reason.
- **It does not mix distributions in one filesystem.** Nothing can.
- **It cannot make a binary system source-based.** That is decided by
  the base, not by a tool on top of it.
- **There is no installer yet**, no signed kernel, and no update
  channel. It boots as an image and runs; it is not yet something a
  stranger installs.

---

## The state of it

A working tool, and one bootable image built from a profile.

The image is a Fedora bootc build whose package list is *generated
from the `minimalism` profile*. It boots through UEFI, reaches a
desktop, and on first boot asks what the machine should be — then
reports that it already is what it was built to be, because the same
profile produced both.

`minimal` and `minimalism` build from the same Containerfile with one
argument changed, at 2.88 GB and 3.91 GB.

**Not yet a distribution anyone can install.** No partitioning, no
Secure Boot signing, nothing published. That is the next body of work.

---

## Things worth quoting

> Nexus decides and explains; apt does the work.

> Every command reads by default. Only `--apply` changes anything.

> An empty result from broken input is more dangerous than a crash.

> This protects the machine when it is off, not while it is running.
> *(what `doctor` says about full-disk encryption)*

> Be wrong in the visible direction.

> The container is a whole distribution. The first package from it
> costs several hundred megabytes; later ones cost only themselves.
