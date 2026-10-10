# Nexus-CORE — status

Updated 10 October 2026. Paste this at the start of a new session; it is
written for someone picking the project up cold.

**Version 0.1.42** (patches 1–66). **559 automated tests**, all passing.
**Preview 2 released 8 Oct 2026** (both editions, 0.1.34).

## What it is

Two things built together:

- **`nexus`**, a C++ tool that explains the system instead of hiding it.
  You ask for a capability ("a web browser"), it shows every way to get
  one and what each costs, checks the plan with the real package
  manager, then hands the work over. **Nexus decides and explains;
  dnf, rpm-ostree and flatpak do the work.** Nothing changes without
  `--apply`.
- **Nexus-CORE**, a Fedora 44 bootc image built around it: signed
  images, updates that can be undone from the boot menu, add-ons chosen
  at first boot.

## Released

| Edition | Desktop | Release |
|---|---|---|
| KDE | KDE Plasma | Preview 1 (0.1.16), 27 Sept — archive.org/details/nexus-core-preview1 |
| Minimal | LXQt on labwc (login screen on Miriway) | Preview 1 (0.1.22), 2 Oct — archive.org/details/nexus-core-minimal-preview1 |
| Server, tiling | — | parked |

- Website: https://nexus-core-davidosdas.netlify.app. Version 3 is
  deployed (5 Oct): edition chooser, feedback form, interactive demos
  (simulations, labelled as such), local fonts, security headers.
  Scripts must live in `.js` files: the security headers block scripts
  written inside the HTML.
- Images: `ghcr.io/davidosdas-official/nexus-core:kde` and `:minimal`,
  signed with cosign, built by GitHub Actions weekly and on "Run
  workflow" (both editions each run). Each image carries a
  `nexus.version` label (set by the build command from CMakeLists.txt).

## Preview 2 — released 8 October

| Step | Minimal | KDE |
|---|---|---|
| Image 0.1.34 built | yes | yes |
| ISO built ("Boot menus made quiet: 3 of 3") | yes | yes |
| VM: disk-password keyboard plain `us` for English + Serbian | **yes** (log: "console keymap: us-acentos ... changed to us") | **yes** (password `test'test1` unlocked) |
| VM: installer boots without the long `[ OK ]` list | **yes** (5 Oct) | "3 of 3" at build; not filmed |
| sha256 | 194f80f80fbc1636fc6f9c3e2b6e8bd1417086b6251c6f7acece347f05c8be15 | b852af8b5c7f868c9943f5bea5f00f8cf1f844a065b6b4171c0381946afeb1b3 |
| Upload: archive.org/details/nexus-core-minimal-preview2, …/nexus-core-preview2 | done (2.8 G) | done (3.1 G) |
| Website v4 (release.js, release notes) | deployed | deployed |

ISOs are kept in `~/nexus-isos/` (the build moves and later deletes
`output/`). Names: `nexus-core-minimal-preview2.iso`,
`nexus-core-preview2.iso`.

## Every command

Nothing changes without `--apply` (and `sudo`); everything else only reads.

**Everyday**
- `nexus` — this machine in a few lines
- `nexus guide` (or `help`), `nexus --help`, `nexus --version`
- `nexus doctor` — health: clock, memory, storage, services, packages
- `nexus history` — what changed and when, with system versions
- `nexus update` / `sudo nexus update --apply` — what is new; take it

**Language**
- `nexus language` — language, keyboard and packs side by side
- `nexus language list` — the 101 languages Fedora offers
- `nexus language add <name>` — fonts, spell checker; `--apply`
- `nexus language set <name or locale>` — menus and formats; `--apply`

**Add-ons and installing**
- `nexus setup [add-ons]` — media, school, development, vpn, security, gaming (KDE)
- `nexus install <thing>` — plain-words preview; `--apply`
- `nexus options <thing>` — every way to get it, with costs
- `nexus plan <thing>`, `nexus remove <package>`, `nexus source <package>`,
  `nexus container <package> [--from-distro d]`

**Understanding the system**
- `nexus why <package>` — image, added by you, or needed by something
- `nexus inspect <package>`, `nexus what-provides <capability>`
- `nexus largest [count] [--unused]`, `nexus services [--all]`
- `nexus hardware`, `nexus secureboot`
- `nexus scan`, `nexus gaps [kind]`, `nexus conflicts`

**Games**: `nexus gamecheck <name or Steam id>`, `nexus gamecheck --identifiers` — reports only.

**Nexus Shop** (window, desktops only): `nexus-shop`, or "Nexus Shop" in the menu.

**VPN**: `nexus vpn keys`, `nexus vpn config --address A --peer-key K --endpoint H:P`

**Profiles (image building)**: `nexus profile list | show <name> | check <names>`,
`nexus image <profile>`, `nexus solve <capability>`

**Options**: `--apply`, `--yes`, `--explain`, `--commands`,
`--with-available`, `--with-flatpak`, `--from <source>`, `--arch <arch>`
(plus developer options for reading other systems' package data).

## How updates work (decided 4 Oct)

Checked once a day, **installed only when asked**, never a restart by
itself.

- A timer in each person's session (`nexus-update-notify`, 10–15 min
  after login, then daily) compares the image the machine runs with the
  registry's (fingerprints via skopeo; no password) and sends a
  notification once per new version. It stays until closed.
- Weekly rebuilds with the same Nexus version show as
  "Nexus 0.1.34, rebuilt with Fedora's latest updates".
- `sudo nexus update --apply` stages the new system version (in effect
  at the next restart; the old one stays in the boot menu) and updates
  Flatpak apps.
- Fedora's `bootc-fetch-apply-updates.timer` is masked (it restarts by
  itself, and fails silently with added packages); rpm-ostree's
  automatic check is off (it missed new Nexus images).
- The boot menu waits 5 seconds (Fedora: 1), written once to
  `/boot/grub2/custom.cfg`.
- Missed notifications: the bell by the clock keeps the last 10 on
  minimal (LXQt's own feature); KDE has its own bell.

## Tested on real hardware

**Asus laptop** (3.6 GB RAM, Secure Boot on), minimal edition: login,
desktop, battery, wallpaper, Alt+Shift us / Serbian Latin, add-ons,
`nexus language set sr_RS.UTF-8@latin` (menus switched, keyboard
untouched), `nexus update` and the daily notification, the 5-second
boot menu, gamecheck (Elden Ring, CS2, Fortnite).

**VM** (2 GB): installer, first boot, add-ons, labwc session, keyboard,
disk-password keymap fix (0.1.34 ISO, 5 Oct).

## Next

1. A short Preview 2 video / post (optional).
2. Done (0.1.35–0.1.36, verified on the Asus 8 Oct): `nexus gamecheck
   --identifiers` and a section in every gamecheck: network card, disks,
   machine ID, screen readable; motherboard serials and TPM root-only
   (matches `ls -l`). Reports only. Non-exact Steam matches are labelled.
3. **Nexus Shop** (design: the "Nexus Shop mockups" canvas, 8 Oct).
   - Step 1, 0.1.37 (patch 61), **works on the Asus** (9 Oct; 185 MB
     while open there, used memory 709 → 864 MB: trim in step 2):
     Home (Nexus picks, categories), search across Flathub and Fedora,
     app pages with every source side by side and "Nexus suggests"
     with reasons. Reads only; shows the command to install.
     Qt Quick (QML); motion on KDE only. Measured here: ~110 MB while
     open (43 MB of it shared Qt libraries), lists read in ~1 s.
   - Data: Fedora's list from `appstream-data` (in the image, 14 MB;
     written once per Fedora release), Flathub's downloaded by the
     Shop once a day with `flatpak update --appstream` (no password,
     tested on the Asus; 107 MB on disk with icons). The versions in
     the lists are the developer's latest notes, never shown as what
     would be installed.
   - Real data (8 Oct): Flathub 3,310 apps (2,188 verified), Fedora
     1,098, 458 in both. Steam, GeoGebra and Minecraft Launcher on
     Flathub are packaged by volunteers, not the developer.
   - Step 2, 0.1.39 (patch 63), **works on the Asus** (10 Oct: Kalk
     installed and removed from the Shop, both in `nexus history`;
     "Update everything" asks for the password; brightness 0.1.38
     works). Memory there: 173 MB RSS = 67 MB the Shop's own + 106 MB
     shared files (Qt, Mesa). 0.1.40 (patch 64): a line under greyed
     buttons saying another change is running (Remove looked broken
     while Kalk was installing). Step 2 was: Updates page (system
     + apps, one button: `pkexec nexus update --apply --yes`, password
     once), Installed (Open / Remove), History (same records as
     `nexus history`), Install from Flathub with a "what Nexus will do /
     it will not" dialog first. Installs go through the new
     `nexus app install|remove <id> --apply` (Flatpak, system-wide, no
     password: tested 9 Oct with GNOME Calculator; written in history).
     Fedora packages stay a copyable command (password + restart).
     `nexus update --lines` is what the Shop reads. One malloc arena:
     ~71 MB while open here (was ~110-130). `nexus-shop --updates`
     opens Updates; the notification now says "open Nexus Shop".
   - Step 3, 0.1.41 (patch 65), **works on the Asus** (10 Oct: the
     notification button opened the Shop at Updates, "rebuilt with
     Fedora's latest updates"; Steam shows "Nexus suggests RPM Fusion
     (non-free)", so RPM Fusion's list is in the image). The update
     notification has an "Open Nexus Shop" button (LXQt shows buttons,
     and the user manager knows WAYLAND_DISPLAY: both checked on the
     Asus 10 Oct); the Shop opens through systemd-run so it outlives
     the notify service. Sources page (what each source is, how many
     apps, list date, Refresh lists). RPM Fusion's app lists
     (rpmfusion-*-appstream-data) added to the image, not fatal if
     missing; RPM Fusion offers show beside Fedora and Flathub, with
     "not open source" for non-free. Fedora Flatpaks: no list on the
     Asus (`flatpak update --appstream fedora` made none); not shown.
   - Next for the Shop: the KDE look (motion), picks per add-on.
4. **Brightness keys** (0.1.38, patch 62). Pieces tested on the Asus
   (9 Oct; whole thing works on the Asus 10 Oct): logind SetBrightness with no password, the one-line bar,
   one notification replaced in place, transient ones kept out of the
   bell. **Image not built yet** (no internet for GitHub that day):
   push, Run workflow (or wait for the weekly build), update, log out
   and in, `grep nexus-brightness ~/.config/lxqt/labwc/rc.xml` shows
   two lines, ~11 presses dark to bright. LXQt's
   moved 2% a press (385 of 19200 on the Asus) with nothing on screen.
   `nexus-brightness up|down`: 12 steps (5…100%, closer at the dark
   end, never black) through logind's SetBrightness (no password, no
   package), and a one-line bar as a notification that each press
   replaces (LXQt notifications show text, not progress bars).
   Existing users' labwc keys changed once by a user unit.
   - Decided 9 Oct: GameVox not preinstalled (not on Flathub, beta,
     licence not stated). The Shop shows it if it reaches Flathub.
5. **Dual boot** (chosen 10 Oct: Pop!_OS + Nexus on the Acer one day;
   Windows + Linux is what beginners want).
   - 0.1.42 (patch 66): the installer no longer erases the only
     internal disk when anything is on it (it did, Windows included);
     the Installation Destination screen asks, and is where "install
     next to it" starts. Only an empty disk is taken without asking.
   - Next: VM test of installing next to another Linux; then
     `nexus dualboot` (what other systems are on the disk, and boot
     menu entries for them in /boot/grub2/custom.cfg).
6. Showcase ("insane UI", Linux showing off) and the minimalism look.
7. Later: trim minimal under 600 MB RAM, security review, server and
   tiling editions, greenboot, NVIDIA (needs NVIDIA hardware).

## Acer housekeeping (10 Oct)

Freed 38 → 75 GB: old Nexus podman images (`podman image prune -a`,
with and without sudo), the build's leftover KDE ISO in output/
(checked against the Preview 2 sha256 first), apt cache, old
installers and ISOs in Downloads. The Preview 2 ISOs stay in
~/nexus-isos as backups.

## Lessons worth keeping

- Test on the real thing: stand-in tests passed for every bug the Asus
  and the VM found.
- In VM instructions, never use key combinations the host uses itself
  (Ctrl+Alt+F3 switched the host, not the VM).
- Paste `sudo` commands one at a time: the password prompt swallows the
  lines pasted after it.
- A clock set in the past makes rpm skip packages signed "after" it;
  doctor checks the clock.
- LXQt picks its compositor from the first `session.conf` with a
  `compositor=` line; labwc takes keyboard layouts from
  `~/.config/lxqt/labwc/environment`.
- `podman untag` without a name removes every name; the next prune
  deletes the image.
- Fedora 44 ISOs keep the UEFI boot menu in an appended partition, not
  in `images/efiboot.img`.
- rpm-ostree's `--check` does not see new container images; compare
  digests with the registry instead.
- The Containerfile has two stages that start the same way (server and
  desktop); check which one an edit lands in.

## Rules

- The cosign private key never leaves `~/.config/nexus-signing/`; it is
  only ever uploaded as a GitHub secret.
- No personal email or location on the website or in recordings.
- Nothing is changed without `--apply`; every change is in history.
