# Nexus-CORE — where things stand (1 October 2026)

Nexus-CORE is a Fedora 44 bootc image plus the Nexus C++ tool (`nexus`).
Current version: **0.1.22** (patches 1–41). 513 tests pass.

## Released

**Preview 1 — KDE edition (27 September 2026)**
- ISO: `nexus-core-preview1.iso` on archive.org
  (https://archive.org/details/nexus-core-preview1), sha256
  `93f0055b1912028d8161a0beff42bca581dc3628a8e4bd9940b2bff1486f123c`
- Website: https://nexus-core-davidosdas.netlify.app
- Updates: signed images, `ghcr.io/davidosdas-official/nexus-core:kde`,
  built weekly by GitHub Actions (plus "Run workflow" by hand after a push).
  A bad update can be undone from the boot menu.

## Editions

| Edition | Desktop | State |
|---|---|---|
| `kde` | KDE Plasma | Released (Preview 1) |
| `minimal` | LXQt: login screen on Miriway, desktop on labwc | **Ready** — tested 1 Oct, awaiting upload |
| `server`, `tiling` | — | Parked, later |

## Add-ons (chosen at first boot, or `nexus setup <names>`)

media, school, development, vpn, security, gaming (KDE only).
`nexus setup media, school` now works with spaces as well as commas.

## Minimal edition — test results

**On the Asus (real hardware, 0.1.21):** login screen, mouse, panel,
battery, wallpaper and keyboard switching (Alt+Shift, us/rs Latin) all work.

**Fresh install in a VM (2 GB RAM, ISO sha256 `f7d997da…`, 0.1.21):**
- Works: installer, first boot, add-ons (`media, school`: 9 of 9),
  login screen, desktop, wallpaper, keyboard file written correctly.
- RAM after login: 621 MB used (target under 600 — close).
- Bug found: a new user's desktop ran on Miriway instead of labwc, so
  keyboard layouts didn't switch. Cause: Fedora's
  `/etc/lxqt/session.conf` says Miriway and LXQt reads it before
  `/etc/xdg/lxqt/session.conf`. Fixed in 0.1.22 (patch 40).

**Fresh install in a VM, 0.1.22 (1 October):** labwc starts by itself,
Alt+Shift switches us/rs Latin (č). Release candidate ISO:
`output/bootiso/install.iso`, sha256
`6fff85984b3c1d92fd2fe8c5bf23502c8e40efe1675feb08decc72c05a41e7dd`.
The Asus, updated to 0.1.22, boots cleanly (no text before the password).

## What was learned this week

- Miriway can't switch keyboard layouts, and a keyboard line in
  `/etc/environment` stopped the login screen from starting.
- labwc takes layouts from `~/.config/lxqt/labwc/environment`
  (`XKB_DEFAULT_LAYOUT/VARIANT/OPTIONS`).
- LXQt picks the compositor from the first `session.conf` with a
  `compositor=` line: your own `~/.config/lxqt`, then `/etc/lxqt`, then
  `/etc/xdg/lxqt`, then `/usr/share/lxqt`.
- The Acer's podman (3.4.4) can't retry broken downloads; the skopeo
  container does (`--retry-times 10`), then `podman pull dir:…`.

## Next steps

1. Upload the minimal ISO to archive.org as
   `nexus-core-minimal-preview1.iso`; add the second download button on
   the website ("Minimal — for older computers") with its sha256.
2. Patch 41 (done): the ISO build falls back to skopeo by itself when
   podman's download breaks off.

## Later

- Installer: don't pre-select any disk when there are several.
- Give the ISO a Nexus name instead of "Fedora-S-dvd-x86_64-44".
- Installer start shows a long `[ OK ]` list (Fedora's installer):
  hide it with `quiet` in the ISO's boot entries (two grub.cfg copies,
  one inside the EFI image).
- Check why the installer set the disk-password keyboard to
  `us-acentos` (dead keys) for English (US) + Serbian (Latin).
- Trim minimal's RAM under 600 MB.
- Security review, server and tiling editions.
- `gamecheck` (AreWeAntiCheatYet + ProtonDB, never bypass anti-cheat).
- First big update: graphical Nexus app (Qt Quick), the minimalism look,
  the showcase tour.
- NVIDIA, greenboot, dual boot.
