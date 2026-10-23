# Nexus-CORE — status

Updated 23 October 2026. Paste this at the start of a new session; it is
written for someone picking the project up cold.

**Version 0.1.55** (patches 1–81). **582 automated tests**, all passing.
**Preview 3 released 18 Oct 2026** (KDE 0.1.50, minimal 0.1.49).
Preview 2: 8 Oct (0.1.34). Since Preview 3, see "Since Preview 3" just
below.

## Since Preview 3 (17–23 October)

**Released 18–19 Oct:** archive.org/details/nexus-core-preview3 (KDE,
0.1.50, 3.2G, sha256 0a353971bd5206786f0b9539bddc6c83a71bc282331fd8e9af285d462e36bfc1)
and archive.org/details/nexus-core-minimal-preview3 (0.1.49, 2.9G, sha256
3a551a20bb6ec81bba2442ab7ade32685e4862f19fc20a4ecd62f4d9d3dccdc8), both
in Community Software. KDE ISO tested in the VM first (install, boot,
`systemctl --failed` 0, `nexus doctor` all ok). Nexus website v5
(release.js Preview 3, dual-boot callout and limits, Shop / Next to
your Linux / gamecheck panels). GitHub release `preview3` (Latest, on
38f9a3f). Portfolio updated (compact Nexus card, Preview 3 metadata,
footer mail icon removed). Bing Webmaster Tools verified for both sites.

**Repo (patch 75):** new README (download table, disk warning, commands,
how it is built), SECURITY.md (private vulnerability reports enabled),
CONTRIBUTING.md, docs/logo.svg. About box + topics set. Profile README
repo DAVIDOSDAS-official. Leftover HANDOFF.md.orig/.rej removed.

**Patch 76:** the monthly Fedora check asks Bodhi (state=current) which
releases are out; issue #1 "URGENT: Fedora 44 near EOL" was a false
alarm (quay.io carries development tags 45/46). Run 19 Oct: "Released
Fedora versions: 43 44".

**Patch 77 (0.1.51): first boot asks once.** One minute, then the login
screen; a skip, no answer or no network all count as asked
(/var/lib/nexus/setup-complete) and leave /var/lib/nexus/setup-not-chosen;
desktops get one notification per person a minute after login
(nexus-setup-reminder user timer, "Choose add-ons" button opens
/usr/libexec/nexus/choose-add-ons in Konsole/QTerminal). Only a missing
or broken console is asked again. Tested in the VM (minimal) and on
the Asus.

**Patch 78 (0.1.52):** `nexus setup --apply` that installs anything
removes setup-not-chosen. **Patch 79 (0.1.53):** installed Flatpaks
count as met in the `nexus setup` list and `nexus profile check`
(gaming showed 4/11 missing = Steam, Heroic, Lutris, ProtonUp-Qt;
media 1/2 = VLC); any `nexus setup --apply`, also "already is what you
asked for", writes /etc/nexus/wallpaper and clears the reminder; new
`nexus gaming [--apply]`: reports NTSync, GameMode, MangoHud, Gamescope,
power profile, vm.max_map_count; --apply writes
/etc/modules-load.d/nexus-ntsync.conf and loads ntsync (history).
Asus 23 Oct: all four checks PASSED (kernel 7.2.11-200.fc44, ntsync
module present, /dev/ntsync crw-rw-rw- root, GameMode 1.8.2).
**Patch 80 (0.1.54):** after --apply, `nexus gaming` prints NTSync's
new state (the report above it is from before the change).

**Patch 81 (0.1.55): `nexus server`** -- servers as Podman Quadlet
containers (/etc/containers/systemd/nexus-<name>.container ->
nexus-<name>.service, started at boot, Restart=always; data in
/var/lib/nexus/servers/<name>, kept on remove). `nexus server` lists
them (kind, state, ports), the LAN address, firewall and Cockpit state.
`add minecraft` (itzg/minecraft-server, Java; needs --accept-eula,
nothing ticks Mojang's EULA for you; memory 40% of RAM in 256 MB steps,
1024-4096 MB -> 1280M on the Asus; --port, --memory), `add website
<folder>` (Caddy 2, copies the folder to .../site, plain http on 8080
or --domain for HTTPS on 80/443; adding again updates the files),
`logs`, `backup` (stop, tar.gz to /var/lib/nexus/backups, start),
`remove` (data stays, ports closed), `cockpit` (enable cockpit.socket +
firewall service). Ports opened only when firewalld runs. Everything
behind --apply and in history. New add-on **hosting** (podman, cockpit,
cockpit-podman, firewall) offered on both editions. Not yet tested on
hardware. Lanewright is NOT tested on Nexus (David, 23 Oct: keep it
separate).

**Promotion:** 25 s vertical dual-boot video (TikTok/IG/X/LinkedIn).
r/Fedora: standalone project posts are not allowed (rule 4: weekly
Project Showcase thread; screenshots Saturdays only) -- posted there.
**Reddit removes every netlify.app link site-wide**: link GitHub only
on Reddit; an own domain would fix it. Next: r/linux, Universal Blue
forum, r/unixporn on a Saturday, DistroWatch later.

**Preview 4 plan:** 1) first boot once DONE, 2) gaming DONE, 3) server:
Cockpit, `nexus server add minecraft|website|app`, status/logs/backup,
firewall + SSH, honest about port forwarding, 4) VPN: `nexus vpn check`
(route + DNS leak), kill switch, Mullvad/Proton/Tailscale setup. Then
Development (toolchains, `nexus dev help <topic>`, opt-in Kali container
small/medium/full) and **Showcase**: "insane UI for everything, desktop
to terminal, like from another universe" incl. a graphical add-on
picker. Later: NVIDIA (separate image + MOK signing key, needs NVIDIA
hardware to test), Nexus share (on LocalSend's protocol so phones
work), multi-user, Nexus-pet, greenboot, minimal < 600 MB.

**Next test on the Asus:** Pop!_OS next to Nexus. Secure Boot off
(Pop's systemd-boot is unsigned), Pop first via Custom (Advanced) with
30+ GB left empty, no encryption, then Nexus; `nexus dualboot` knows
systemd-bootx64.efi but it is untested.

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

**Dual boot** (0.1.44): `nexus dualboot` — the systems on this disk
(names need `sudo`); `sudo nexus dualboot add --apply` — put them in the
boot menu; `sudo nexus dualboot remove --apply`; `sudo nexus dualboot 1
--apply` — restart into system 1, once.

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

## Dual boot — how it works (tested on the Asus, 12 Oct)

Nexus next to another **Linux** (never Windows). Tested with Linux Mint
22.3 on the Asus, Secure Boot on. **Not released yet: Preview 3.**

**Installing next to another Linux** (0.1.43+ ISO):
1. The other system is already installed, with at least **30 GiB of
   free space in one piece** next to it (UEFI, GPT disk). Mint's
   installer: "Something else", leave the space free.
2. Start the Nexus USB. The installer finds the other system on its
   own and plans Nexus into the free space with **its own** EFI
   partition (600 MB), `/boot` (2 GB) and an encrypted system. The
   other system's partitions are not touched. (`/tmp/nexus-disk.log`
   in the installer says what it decided.)
3. It asks for the disk password straight away. Then keyboard, user,
   **Begin Installation. Do not open Installation Destination** —
   pressing Done there throws Nexus's plan away (0.1.45: the installer
   then stops early with a plain message instead of failing at the end).
4. After installing, the laptop may still start the other system: the
   new Nexus entry lands below it in the BIOS boot list. Fix once in
   the **BIOS Utility (F2 on the Asus) → Boot Priority: drag Fedora
   (the Nexus entry) to the top → F10 save**. (0.1.46: the installer
   fixes the entry and puts Nexus first itself -- to be tested.) From then on **every restart starts
   Nexus**.

**Using it:**
- `sudo nexus dualboot` — lists the systems: "1  Linux Mint 22.3,
  not encrypted, 95.4 GiB, started by /EFI/ubuntu/shimx64.efi".
- `sudo nexus dualboot add --apply` — Mint appears at the bottom of
  Nexus's 5-second boot menu ("Linux Mint 22.3"). Pick it with the
  arrow keys.
- `sudo nexus dualboot 1 --apply` — **one line from Nexus straight
  into Mint**: restarts with Mint already chosen, once; the next
  restart is Nexus again. No arrow keys, no BIOS.
- The BIOS boot list (F2 → Boot menu → ubuntu) still starts Mint too.

**How it works inside:** each entry in `/boot/grub2/custom.cfg`
(between `### BEGIN/END nexus dualboot ###`) hands over to the other
system's own signed loader on its own EFI partition (chainload), so
Secure Boot stays on and nothing of the other system changes. The
Asus ignores the firmware's "start this next" (BootNext), so
`nexus dualboot 1` uses GRUB's next_entry, handled in Nexus's block.

**Known:** Mint on the Asus sometimes starts to a "melting" logo and a
white screen (Intel Gemini Lake graphics, "FIFO underrun" in Mint's
log). Mint's own problem on this laptop: it happened on David's first
Mint install before Nexus existed; not caused by the handover.

**For the website and a video (Preview 3):**
- "Install Nexus next to your Linux. It finds the other system, takes
  only the free space, and never touches the other system."
- "One command from Nexus into Mint: `sudo nexus dualboot 1 --apply`.
  The next restart is Nexus again."
- Show: `nexus dualboot` listing both systems; the boot menu with
  Linux Mint at the bottom; the one-line switch.
- Say honestly: Linux next to Linux only (no Windows); tested with
  Linux Mint on one laptop; the BIOS step until the installer does it.

## Tested on real hardware

**Asus laptop** (3.6 GB RAM, Secure Boot on), minimal edition: login,
desktop, battery, wallpaper, Alt+Shift us / Serbian Latin, add-ons,
`nexus language set sr_RS.UTF-8@latin` (menus switched, keyboard
untouched), `nexus update` and the daily notification, the 5-second
boot menu, gamecheck (Elden Ring, CS2, Fortnite).

**Asus, dual boot** (12 Oct): 0.1.43 installed next to Linux Mint 22.3;
0.1.44 `nexus dualboot` list/add/1; Mint from the menu, by
`dualboot 1`, and from the BIOS; Secure Boot on.

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
   - Decided 10 Oct: Linux only, never Windows. Tested on the Asus
     (reinstall), never the Acer until finished. First partner: Linux
     Mint (cleaner than Pop!_OS for a first test).
   - VM test dropped (mouse unusable); real hardware instead.
   - Test 1, Asus, 11 Oct: Mint 22.3 ("Something else": 512 MB EFI +
     100 GB ext4, ~150 GB free). Nexus 0.1.42 saw 142.63 GiB free and
     erased nothing, but failed at "Bootloader write config:
     grub2-mkconfig": it reused Mint's EFI partition, so ostree saw
     Mint's GRUB and tried to rebuild the menu with grub2-mkconfig,
     which fails inside the installer ("failed to get canonical path of
     LiveOS_rootfs"). os-prober itself worked (found Mint). Known Fedora
     limitation: fedora-silverblue/issue-tracker#530 (static GRUB +
     bootupd: no dual boot yet; use separate empty boot partitions).
     Leftovers (p3 /boot, p4 LUKS) deleted from Mint; Mint's ESP has
     only BOOT + ubuntu; BIOS list only Ubuntu + USB. Asus left on Mint
     with the free space, ready for test 2.
   - 0.1.43 (patch 67): one disk in use + UEFI + GPT + 30 GiB or more
     free in one piece -> Nexus installs into the free space with its
     OWN EFI partition (600 MB), /boot (2 GB ext4) and encrypted / (xfs,
     LUKS2). Otherwise the installer asks, as before.
   - Test 2, Asus, 12 Oct: PASSED with the 0.1.43 ISO (image f728ccc,
     nexus 0.1.43 aafd96a). Disk log: 142.6 GiB free -> own EFI p3
     600M, /boot p4 2G, / p5 LUKS2 xfs 140G; Mint p1/p2 untouched.
     Nexus boots (disk password, login), Mint boots from the BIOS list,
     restarting goes straight to Nexus (BootOrder 0003 first).
     Found on the way:
     - The disk password is asked as soon as the installer starts,
       before the keyboard screen: a non-US keyboard could set a
       password that does not match at boot. To fix.
     - Opening Installation Destination and pressing Done with
       Automatic selected throws the Nexus plan away: the installer's
       own layout reused Mint's ESP, unencrypted btrfs, and failed with
       the grub2-mkconfig error again. To fix (trap for real users).
     - The new Fedora entry (0003) was added third in the boot order,
       behind a dead Boot0000 Fedora entry left by the failed attempts
       (pointing at Mint's ESP) and Ubuntu: the first restart started
       Mint. Fixed by hand in the BIOS; 0000 to remove with
       efibootmgr -b 0000 -B. The installer/nexus should put Nexus first.
     - The Asus ignores BootNext (efibootmgr --bootnext 0003 started
       Mint; the same entry works from the BIOS list). So
       `nexus dualboot 1` uses Nexus's own GRUB (next entry), not
       BootNext.
     - Time zone skipped in the installer: clock not on Skopje time.
   - Asus facts (12 Oct, Nexus): grub.cfg sources $prefix/custom.cfg
     (41_custom); grub.cfg has NO next_entry handling; custom.cfg holds
     nexus-boot-menu's "set timeout=5"; grub2-editenv, grub2-reboot,
     os-prober, mokutil present; Secure Boot enabled.
   - 0.1.44 (patch 68): `nexus dualboot` written (not yet run on the
     Asus). Its block in /boot/grub2/custom.cfg (between "### BEGIN/END
     nexus dualboot ###") holds chainload entries (search ESP uuid,
     chainloader /EFI/<dir>/shimx64.efi) plus its own next_entry
     handling. To test: chainloading Mint's shim with Secure Boot on.
   - Test on the Asus, 12 Oct, 0.1.44: PASSED. `nexus dualboot` (no
     root: Mint seen from outside; sudo: "Linux Mint 22.3", started by
     /EFI/ubuntu/shimx64.efi on p1), `add --apply` wrote the block,
     Mint appears last in the GRUB menu, chainloading works with Secure
     Boot on, `nexus dualboot 1 --apply` restarted with Mint
     preselected (next_entry handling works), BIOS -> ubuntu also OK.
     One Mint start went white ("melting" logo, then white; i915 "CPU
     pipe A FIFO underrun" in Mint's log): Mint/Gemini Lake, not the
     handover -- David saw the same on his first Mint install, before
     Nexus existed; 3 later starts (menu, dualboot 1, BIOS) all fine.
   - Found 12 Oct, to fix next:
     - First-boot add-on question: typing is not shown (keys DO arrive:
       log showed "Did you mean: development?"). Likely the disk
       password prompt (plymouth) leaves echo off. Fix: stty sane/echo
       before asking; give the question a time limit (it waits forever).
     - Clock a day behind and on New York time (time zone skipped in
       the installer; "Oct 11 11:38 EDT" on 12 Oct).
     - `nexus dualboot` text: "a ext4" -> "an ext4"; the no-root hint
       wraps badly.
   - 0.1.45 (patch 69, not yet tested):
     - first boot: `stty sane` before asking (typing shows); the
       question waits 15 minutes at most (NEXUS_SETUP_WAIT), then the
       login screen, asking again next start.
     - installer %pre: when it plans the disk (both paths, encrypted),
       adds `keyboard --vckeymap=us --xlayouts=us` so the early disk
       password is typed on US; %post keeps the console on US
       (/etc/nexus/install-passphrase-us marker, from a --nochroot
       %post). Layouts added on the Keyboard screen still apply to the
       desktop.
     - installer %pre-install guard (--erroronfail): stops with a plain
       message if /boot/efi holds another system's EFI/<dir> (the Done
       trap) -- before anything is written.
     - installer %post --nochroot: puts the Nexus EFI entries (matched
       by the EFI partition's PARTUUID) first in BootOrder.
     - dualboot wording: "a partition (ext4)"; shorter no-root hint.
     - Clock: not yet; needs `timedatectl` from the Asus.
   - 0.1.45 tested on the Asus, 12-13 Oct (ISO sha256 41dafe3c…,
     nexus 0.1.45 5053196):
     - Done-trap guard: PASSED -- stopped before installing, nothing
       written to Mint's ESP or the boot list; message in
       /tmp/nexus-esp-check.log (on screen only the generic "unknown
       error" -- to improve).
     - Password window shows "us"; nexus-disk.log "Disk password asked
       at the start, on a US layout"; vconsole KEYMAP=us; disk password
       and login work: PASSED.
     - First-boot typing visible: PASSED.
     - Nexus first: FAILED. packaging.log: bootupctl ran `efibootmgr
       --create --disk /dev/nvme0n1 --part 1 ... --label Fedora` --
       partition 1 is MINT's ESP (Nexus's is 3). bootupd takes the
       first ESP on the disk. That dead entry (Boot0000, file missing)
       went first and the firmware fell through to Mint; the working
       entry (0003, SHIM.EFI + data "BO") is created by shim's fallback
       on the first start from the disk. Same cause as the dead
       Boot0000 of 11 Oct. Fixed by hand in the BIOS.
   - 0.1.46 (patch 70, not yet tested):
     - installer %post --nochroot: removes Fedora entries that point
       at another ESP lacking the file (mounted read-only to check; a
       real Fedora elsewhere is kept), creates "Nexus-CORE" on Nexus's
       own ESP partition if none, puts Nexus's entries first. Log kept
       in /var/log/nexus-install-boot.log. Tested with a fake efibootmgr
       on the Asus's exact list: 0000 removed, Nexus-CORE created first.
     - Shop History: "applied" (green) for packages that are in the
       running system version, like `nexus history` -- it said "staged"
       for all of them (Asus, 13 Oct). Reads `rpm-ostree status --booted
       --json` requested-packages/packages. (OBS was in history; it was
       the oldest entry and had scrolled off the terminal.)
     - nexus-clock.service (all editions, once per machine): network
       time on, hardware clock in UTC (Asus: NTP inactive, RTC local).
   - 0.1.46 on the Asus, 13 Oct: clock PASSED (NTP active, RTC UTC,
     right date); Shop History "applied" PASSED. Time zone left on New
     York on purpose (David). Clock was stuck around 11 Oct 11:30 before
     (CMOS battery? BIOS time not yet checked), so older history
     entries carry 11 Oct.
   - 0.1.47 (patch 71): a staged `update` record shows "applied" once
     the machine restarted after it and runs that Nexus version or
     newer (updateTookEffect, bootTimestamp from /proc/stat btime);
     CLI and Shop. Flatpak entries keep "installed" (no restart).
     Not done: the Done-trap stop still shows Anaconda's generic
     "unknown error" on screen; the plain message is only in
     /tmp/nexus-esp-check.log (a kickstart script cannot put text on
     the GUI).
   - 0.1.48 (patch 72): `nexus update --apply` no longer captures
     rpm-ostree's and flatpak's output (std::system): progress shows in
     the terminal and in the Shop's job panel. Before: 2+ hours of
     silent "Updating..." on the Asus (14 Oct) for 0.1.46 -> 0.1.47 with
     ~15 layered packages, and a second `sudo nexus update` queued
     silently behind it. Now refuses with a plain message when
     rpm-ostree is already busy.
   - 0.1.48 ISO reinstall on the Asus, 16 Oct: Nexus first FAILED
     again. /var/log/nexus-install-boot.log: "EFI partition:
     /dev/nvme0n1p3" printed on two lines -- findmnt returned the ESP
     twice (mounted twice in the installer), the two-line name broke
     lsblk ("partuuid: ?"), and the whole fix was skipped. Mint's ESP
     has no EFI/fedora, so Boot0000 was dead as before.
   - 0.1.49 (patch 73): installer %post takes the first findmnt line;
     plus nexus-boot-entry.service (once per machine, marker
     /var/lib/nexus/boot-entry-set) doing the same on the running
     system -- fixes the Asus by an ordinary update, and any machine
     installed before. Both tested with fake tools on the Asus's exact
     list (two-line findmnt): 0000 removed, order 0003,0001,0002.
   - Asus, 16 Oct: nexus-boot-entry ran (0.1.49): removed the dead
     Boot0000 on Mint's ESP -- PASSED on real hardware. "Nexus first"
     not exercised (David had already moved it in the BIOS); to see in
     the Preview 3 install test. Shop update now shows rpm-ostree's
     "[5/55] Fetching..." line in the job panel, and the full output
     when opened -- patch 72 PASSED.
   - Next: Preview 3 (both editions, 0.1.49), with one install next to
     Mint as the release test. In progress 16 Oct: minimal ISO building
     on the Acer (PROFILE=minimal IMAGE=ghcr.io/davidosdas-official/
     nexus-core:minimal ./image/build-installer.sh), then KDE (PROFILE=
     kde IMAGE=...:kde); save as ~/nexus-isos/nexus-core-minimal-
     preview3.iso and nexus-core-preview3.iso. Acer disk: ~31 GB free
     after removing 0.1.42/0.1.43/0.1.45 ISOs, a nameless podman image
     and a flatpak cache (a build with 13 GB free froze the Acer).
     Minimal Preview 3 ISO built 16 Oct 21:12Z: nexus 0.1.49 (aa80e1c),
     image sha256:aeae1d1f90cb9599eac3024d85b66335f60f38f977bf398269792165d014c733,
     iso sha256 3a551a20bb6ec81bba2442ab7ade32685e4862f19fc20a4ecd62f4d9d3dccdc8.
     RELEASE TEST PASSED 16 Oct 23:55 (Asus, minimal Preview 3 ISO,
     next to Mint): first restart went straight to Nexus. Install log:
     dead Boot0000 removed, "Nexus-CORE" entry created on p3, first.
     `sudo nexus dualboot 1 --apply` worked after `add`.
   - 0.1.50 (patch 74): `nexus dualboot <n> --apply` adds the other
     systems to the boot menu itself when they are missing (fresh
     install), then restarts into the chosen one. Not in the Preview 3
     ISOs (0.1.49); arrives as an update.
     Old note follows.
   - Next: one ISO (0.1.47) and one reinstall next to Mint to test the
     boot-entry fix (Nexus first by itself); then Preview 3.
   - `nexus dualboot` design (decided 11 Oct with David):
     - no arguments: lists this system and the others, numbered, each
       with the name it gives itself (/etc/os-release via os-prober),
       encrypted or not, size; Nexus is not numbered. An encrypted
       other system: "encrypted, can't tell which until unlocked".
       No list of distro names in Nexus.
     - `nexus dualboot 1`: restart into other system 1, once (firmware
       BootNext), asks first, --apply to restart. Test whether the Asus
       honours BootNext.
     - plus boot menu entries for the others in /boot/grub2/custom.cfg.
6. Showcase ("insane UI", Linux showing off) and the minimalism look.
7. Later: trim minimal under 600 MB RAM, security review, server and
   tiling editions, greenboot, NVIDIA (needs NVIDIA hardware).

## Ideas for later (David, 10 Oct)

- **More people on one computer** (family). Accounts are added by
  someone who knows the admin password (Settings or `nexus user add`),
  never by anyone at the login screen. Kinds: admin / standard / child
  (child = app limits through Fedora's parental controls). The login
  screen already lists every account. With disk encryption, everyone
  types the disk password at start, or someone starts the computer
  for them: say so when adding a person.
- **Nexus-pet**: an optional assistant that can see the screen and
  talk. Asked once after the first login, on/off in settings any time.
  Modes: text only / text + voice / text + voice + listening
  (microphone). Rules: off unless chosen; a visible sign whenever it
  looks or listens; it suggests, never runs anything by itself; says
  plainly whether it runs on this computer or sends to a company.
  On this computer it needs about 8-16 GB RAM (small language model,
  speech: Whisper or Vosk to listen, Piper to speak), so the minimal
  edition on 3.6 GB could only offer it through an online service
  the person chooses. **That is said in bold**, with what is sent.
  - Look (David, 10 Oct): Grok's concept, a rounded charcoal hexagon
    with two antennae, big eyes, small tail, an "N" on the front, icy
    blue accents; expressions happy, curious, thinking, processing
    (spinner instead of a face), concerned, proud, sleepy. The cube
    version was turned down (eyes split across two faces look dull).
  - Services (David, 10 Oct): Claude, ChatGPT, Grok. Wanted: sign in
    with the plan they already pay for, its limits apply; otherwise
    their own API key (billed by that company). Checked 10 Oct for
    Claude only: Anthropic's help pages say third-party apps can draw
    on a subscription's limits through the Agent SDK, but the rules
    changed and were paused this year and developer requirements are
    not spelled out. ChatGPT (checked 10 Oct): official "Sign in with
    ChatGPT" (developers.openai.com/siwc) lets open-source apps use an
    eligible Plus/Pro user's plan without an API key (OAuth, the user
    authorises; "Continue with ChatGPT" branding); commercial apps need
    approval. Grok (checked 10 Oct): one third-party tool signs in with
    a SuperGrok/X plan, but no xAI page saying third-party apps may;
    treat as API key only until xAI documents it. Check all three again
    before building; API keys are the path that certainly works.

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

- Anaconda: opening Installation Destination and pressing Done with
  Automatic selected re-plans the disk itself, discarding the
  kickstart's partitions (12 Oct).
- An ostree system must not share another system's EFI partition: its
  GRUB files make ostree rebuild the menu with grub2-mkconfig, which
  fails in the installer (fedora-silverblue issue 530).
- The Asus ignores BootNext; use GRUB's next_entry instead.
- The disk password prompt can leave the console with typing hidden;
  anything asking on tty1 afterwards must turn echo back on.

## Rules

- The cosign private key never leaves `~/.config/nexus-signing/`; it is
  only ever uploaded as a GitHub secret.
- No personal email or location on the website or in recordings.
- Nothing is changed without `--apply`; every change is in history.
