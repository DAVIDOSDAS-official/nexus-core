<p align="center">
  <img src="docs/logo.svg" width="96" alt="">
</p>

<h1 align="center">Nexus-CORE</h1>

<p align="center">
  <b>A Linux desktop that explains itself, and asks before it changes anything.</b><br>
  Fedora 44 · KDE Plasma or a light LXQt edition · free and open source (GPL-3.0)
</p>

<p align="center">
  <a href="https://nexus-core-davidosdas.netlify.app/">Website</a> ·
  <a href="https://github.com/DAVIDOSDAS-official/nexus-core/releases/latest">Latest release</a> ·
  <a href="https://nexus-core-davidosdas.netlify.app/#limits">Known limits</a> ·
  <a href="https://github.com/DAVIDOSDAS-official/nexus-core/issues">Report a problem</a>
</p>

---

Nexus-CORE is a Fedora-based desktop for people who want Linux to tell
them what is going on. Before anything changes, Nexus says what it
will do and what it will not do, and nothing happens until you add
`--apply` or press the button. Every change is written down in
`nexus history`.

It is a **preview made by one person**, tested on a few laptops and in
virtual machines. Read the [known limits](https://nexus-core-davidosdas.netlify.app/#limits)
before installing, and keep a backup of anything that matters.

<!--
Screenshots: put PNGs in docs/screenshots/ and remove this comment.
<p align="center">
  <img src="docs/screenshots/desktop.png" width="49%" alt="KDE desktop">
  <img src="docs/screenshots/shop.png" width="49%" alt="Nexus Shop">
</p>
-->

## Download

| Edition | Desktop | For | Download |
|---|---|---|---|
| **KDE Plasma** | KDE Plasma | the full desktop, gaming included; 4 GB RAM or more | [Preview 3 ISO](https://archive.org/details/nexus-core-preview3) |
| **Minimal** | LXQt | older computers, from 2 GB RAM | [Preview 3 ISO](https://archive.org/details/nexus-core-minimal-preview3) |

Checksums are on the [release page](https://github.com/DAVIDOSDAS-official/nexus-core/releases/latest)
and the website. Check the file before writing it to a USB stick:

```
sha256sum nexus-core-preview3.iso
```

Needs a 64-bit PC with UEFI. Secure Boot can stay on.

**About the disk:** an empty disk is used whole. Next to another
**Linux** with 30 GB or more free in one piece (UEFI, GPT disk), Nexus
installs into the free space and leaves the other system alone. If
anything else is on the disk, Windows included, the installer asks
before touching anything. Dual boot with Windows is not supported.

## What you get

- **It asks what the machine is for.** At first start you pick add-ons
  (media, gaming, development, school, security, VPN). Nexus shows what
  each would add on *your* hardware and installs nothing until you agree.
- **Nexus Shop.** Flathub, Fedora and RPM Fusion side by side, with
  what Nexus suggests and why: who packaged the app, whether it needs a
  restart, whether it is open source. Before every install, a "What
  Nexus will do / It will not" screen.
- **Updates on your terms.** A new signed image is built every week
  with Fedora's latest fixes. Your machine checks daily and tells you.
  Nothing is installed, and the computer never restarts by itself,
  until you say so. The previous version stays in the boot menu.
- **Next to your Linux.** `sudo nexus dualboot` lists the other systems
  on the disk; `sudo nexus dualboot 1 --apply` restarts straight into
  one, once. Each one starts through its own signed loader, so Secure
  Boot stays on.
- **Encrypted by default**, with signed images: an installed machine
  refuses an update that Nexus-CORE did not sign.
- **`nexus doctor`** checks clock, memory, storage, services, updates
  and encryption, and says what to do about anything wrong.
- **`nexus gamecheck <game>`**: will it run, what its anti-cheat
  allows, and what it can read about your computer. Reports only.
- **No accounts, no tracking.**

## The `nexus` command

```
nexus                      this machine in a few lines
nexus guide                how to work this system
nexus doctor               is anything wrong, and what to do
nexus update [--apply]     what is new; take it
nexus app install|remove <flathub id> [--apply]
nexus setup [add-ons] [--apply]
nexus dualboot [add | remove | <number>] [--apply]
nexus gamecheck <game>     will it run here, and what it can see
nexus language [list | add | set] [--apply]
nexus history              every change Nexus has made
nexus install | remove | options | why | largest | services | hardware | secureboot
```

**Every command only reads and explains. Only `--apply` changes
anything.** `nexus --help` lists the rest.

An example, before anything is changed:

```
$ sudo nexus dualboot 1
What Nexus will do
  Restart the computer now, into Linux Mint 22.3
  Only this once: the next restart starts Nexus again

It will not
  Change the order the computer starts in
  Close your apps for you first: save your work

Nothing has been changed. To restart:
    sudo nexus dualboot 1 --apply
```

## How it is built

- **Fedora bootc.** The system is a container image
  (`image/Containerfile`), built from a profile: a list of what the
  machine should be able to do, resolved to Fedora packages. The KDE
  and minimal editions build from the same file with one argument
  changed.
- **Updated as one piece.** An update downloads a whole new image and
  switches to it at the next restart; the old one stays in the boot
  menu. Nothing is half-updated.
- **Signed.** Images are signed with cosign by the weekly workflow
  (`.github/workflows/weekly-image.yml`), and installed machines are
  set to refuse unsigned ones.
- **The installer ISO** is made with Fedora's `bootc-image-builder`:
  `./image/build-installer.sh`. Every build writes down what went into
  it (image digest, Nexus version, builder digest, ISO checksum).
- **The kernel is Fedora's**, with Fedora's signature, which is why it
  boots with Secure Boot on.

The `nexus` tool itself is C++20 and Qt 6 (the Shop). It decides and
explains; Fedora's own tools (rpm-ostree, flatpak, bootc) do the work.

## Building

```
cmake -S . -B build && cmake --build build
ctest --test-dir build
./build/cli/nexus doctor
```

Needs a C++20 compiler and CMake 3.20, Qt 6 for the Shop, and
GoogleTest for the tests (`-DNEXUS_BUILD_TESTS=OFF` to skip).
576 automated tests.

To build an installer ISO from a published image:

```
PROFILE=kde IMAGE=ghcr.io/davidosdas-official/nexus-core:kde ./image/build-installer.sh
```

It needs about 20 GB of free disk space.

## Layout

```
cli/         the nexus command
gui/         Nexus Shop (Qt Quick)
core/        the model: capabilities, resolution, conflicts
system/      reading real systems: rpm, dpkg, flatpak, bootc, disks, dual boot
hardware/    what the machine is: devices, encryption, secure boot
components/  profiles and add-ons, as data
image/       the bootc image, installer, first start, updates, boot menu
tests/       unit and differential tests
docs/        decisions and specifications
```

`docs/specifications/00-decisions.md` has the reasoning that is not
recoverable from the code.

## Reporting problems

Open an [issue](https://github.com/DAVIDOSDAS-official/nexus-core/issues)
with what you did, what happened, and the output of `nexus doctor`.
For anything security-related, see [SECURITY.md](SECURITY.md).

## Licence

The code is under the GNU General Public License, version 3. See
`LICENSE`.

The name Nexus-CORE and the node-graph N logo are trademarks and are
not covered by the GPL: a changed version has to be renamed. See
`TRADEMARKS.md`.

Made by [DAVIDOSDAS](https://davidosdas.netlify.app).
