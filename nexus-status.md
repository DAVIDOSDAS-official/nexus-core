# Nexus-CORE — where things stand (30 September 2026)

Nexus-CORE is a Fedora 44 bootc image plus the Nexus C++ tool (`nexus`).
Current version: **0.1.22** (patches 1–40). 513 tests pass.

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
| `minimal` | LXQt: login screen on Miriway, desktop on labwc | Almost ready — last fix awaiting a test |
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
- **Bug (fixed in 0.1.22, to be re-tested):** a new user's desktop ran
  on Miriway instead of labwc, so keyboard layouts didn't switch.
  Cause, confirmed: Fedora's `/etc/lxqt/session.conf` says Miriway and
  LXQt reads it before our `/etc/xdg/lxqt/session.conf`. Patch 40 sets
  labwc in both.

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

1. Apply patch 40 (0.1.22), run the workflow, build a fresh minimal ISO
   (build-installer now downloads the image itself).
2. Fresh VM install again: `ps` must show labwc, and Alt+Shift must
   switch layouts.
3. Minimal ISO on archive.org, second download button on the website.

## Later

- Installer: don't pre-select any disk when there are several.
- Give the ISO a Nexus name instead of "Fedora-S-dvd-x86_64-44".
- Trim minimal's RAM under 600 MB.
- Security review, server and tiling editions.
- `gamecheck` (AreWeAntiCheatYet + ProtonDB, never bypass anti-cheat).
- First big update: graphical Nexus app (Qt Quick), the minimalism look,
  the showcase tour.
- NVIDIA, greenboot, dual boot.
