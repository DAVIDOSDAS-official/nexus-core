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

A checksum proves the download is intact, not who built it. From the
next preview on, ISOs are built on GitHub by `.github/workflows/installer-iso.yml`,
and GitHub keeps a signed record that the file came from this
repository's workflow at a given commit. Check any copy, wherever you
got it, with the GitHub CLI:

```
gh attestation verify nexus-core-kde-<version>.iso --repo DAVIDOSDAS-official/nexus-core
```

Preview 3 was built on the maintainer's laptop and has no such record.

Needs a 64-bit PC with UEFI. Secure Boot can stay on.

**About the disk:** an empty disk is used whole. Next to another
**Linux** with 30 GB or more free in one piece (UEFI, GPT disk), Nexus
installs into the free space and leaves the other system alone. If
anything else is on the disk, Windows included, the installer asks
before touching anything. Dual boot with Windows is not supported.

## What you get

- **It asks what the machine is for.** At first start you pick add-ons
  (media, gaming, development, school, cybersecurity, VPN). Nexus shows what
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
  `./image/build-installer.sh`, run on GitHub by
  `.github/workflows/installer-iso.yml` with a build attestation (see
  Download). Every build writes down what went into it (image digest,
  Nexus version, builder digest, ISO checksum).
- **Containers by digest.** The Kali toolbox, the Minecraft and website
  servers and the dev boxes are pulled at the digest the weekly build
  recorded in the signed image (`image/pins/`), not whatever their tag
  points at that day.
- **The kernel is Fedora's**, with Fedora's signature, which is why it
  boots with Secure Boot on.

The `nexus` tool itself is C++20 and Qt 6 (the Shop). It decides and
explains; Fedora's own tools (rpm-ostree, flatpak, bootc) do the work.


## What Nexus contacts

Nothing about you is sent anywhere by Nexus: no telemetry, no
accounts, no crash reports. These are the places a Nexus machine
reaches, and when:

| Where | When | What it learns |
|---|---|---|
| ghcr.io (GitHub) | the daily update check, and every update | your IP address and that a Nexus machine asked |
| Fedora mirrors | system updates and `nexus` package lookups | your IP address, which packages |
| dl.flathub.org | apps from the Shop or `nexus app` | your IP address, which apps |
| mirrors.rpmfusion.org | only if you add media codecs or Steam | your IP address, which packages |
| geoip.fedoraproject.org | only if you turn on automatic time zone | your IP address (it answers with a time zone) |
| Steam store, ProtonDB, GitHub (AreWeAntiCheatYet) | only when you run `nexus gamecheck <game>` | the game name you typed |
| Docker Hub, registry.fedoraproject.org | only when you create a toolbox or server (`nexus cyber`, `nexus dev`, `nexus server`) | your IP address, which image |
| Fedora's anonymous weekly count | if Fedora's `rpm-ostree-countme.timer` is on: `systemctl is-enabled rpm-ostree-countme.timer` | one anonymous "a Fedora-based machine exists this week" |

## How this is made

One person makes Nexus, with a great deal of help from AI (Claude,
mostly): much of the code and text was written with it, and the
working notes in HANDOFF.md are kept for that. What keeps that honest
is checking, not trust:

- about 660 automated tests run on every build and every weekly image;
- outside reviews of the whole source -- each finding and its fix is
  in HANDOFF.md, named as such; one ran the tests under AddressSanitizer
  and UndefinedBehaviorSanitizer and fuzzed every parser that reads
  data from the internet (8.5 million runs, no memory errors);
- nothing changes on the machine without `--apply`, and every change
  is in `nexus history`.

Read the code, or hand it to any reviewer you trust: it is all here.

## Leaving Nexus

Nexus is Fedora underneath, and your files are in `/var/home`, which
switching systems does not touch. To move a KDE machine to Fedora's own
KDE edition (Fedora Kinoite) and keep getting Fedora's updates:

```
sudo rpm-ostree rebase ostree-unverified-registry:quay.io/fedora/fedora-kinoite:44
systemctl reboot
```

The Nexus system stays in the boot menu until the next update, so you
can go back. If this project stops, the machine keeps working as it
is; it just gets no new Nexus images -- that is the time to switch.

## Building

```
cmake -S . -B build && cmake --build build
ctest --test-dir build
./build/cli/nexus doctor
```

Needs a C++20 compiler and CMake 3.20, Qt 6 for the Shop, and
GoogleTest for the tests (`-DNEXUS_BUILD_TESTS=OFF` to skip).
About 650 automated tests (`ctest --test-dir build`).

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
