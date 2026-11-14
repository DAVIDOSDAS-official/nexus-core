# Nexus-CORE — status

Updated 1 November 2026. Paste this at the start of a new session; it is
written for someone picking the project up cold.

**Version 0.1.85** (patches 1–111). **679 automated tests**, all passing.
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

**Asus 24 Oct (0.1.55):** hosting add-on installed ("already
satisfied" after); `nexus server` status ok; Cockpit `--apply` done
(browser login not yet confirmed); Minecraft and website only
previewed. Found: **no firewall at all** (firewalld not installed).
**Patch 82 (0.1.56):** firewalld installed and on in every image,
default zone public + dhcpv6-client, mdns, samba-client, kdeconnect (not
FedoraWorkstation, which opens every port >1024); `nexus server` opens
and closes its own ports. Unknown `nexus server` options suggest the
right one ("--accept-elua" -> "Did you mean --accept-eula?").

**Weekly build #53 failed** on patch 82: firewall-offline-cmd exits 16
(ZONE_ALREADY_SET) when the zone is already public. **Patch 83
(0.1.57)** sets zone/services only when not already so, and adds
**making room** to the installer %pre: one internal disk, UEFI, GPT,
under 30 GiB free -> an unencrypted, unmounted ext4 partition that can
give enough (keeps used+20 GiB and at least half) is offered on tty6:
"<PRETTY_NAME from its os-release> uses the whole disk ... Type YES".
Only on mains power (or no battery), only after e2fsck -f -p is clean;
resize2fs below target, sfdisk -N <n> to target, resize2fs to fill;
any failure or no YES in 120 s -> the installer asks as before. Tested
here on a loop disk shaped like Pop's (EFI, full ext4, swap at the end):
300 MB file intact (sha256), e2fsck clean, free space appeared between
the ext4 and swap. Not yet on hardware: Asus left with Pop on the whole
disk (229.5 GiB /, 11 GiB used) as the test case.

**Asus 28 Oct, 0.1.57 KDE ISO next to Pop (229.5 GiB ext4, 13.6 GiB
used):** the plan was right ("could keep 115 GiB and free 114.5 GiB")
but "Making room stopped: FileNotFoundError" -- the installer has no
fgconsole/chvt. **Patch 84 (0.1.58):** switches consoles with the
kernel's VT_ACTIVATE/VT_WAITACTIVE ioctls; checks e2fsck, resize2fs,
sfdisk exist before asking. The question screen itself is still
untested (next: a VM with a full ext4 disk).

**Asus 28 Oct: Nexus 0.1.57 (KDE) next to Pop!_OS 24.04 PASSED.**
Pop shrunk by hand in the installer console (the same steps as the
make-room code: resize2fs 114G, sfdisk -N 3 to 117760MiB, resize2fs;
e2fsck -f -n clean), then the installer planned Nexus by itself into
the 114.5 GiB gap (p5-p7, / encrypted 111.9 GiB). `sudo nexus dualboot`
found "Pop!_OS 24.04 LTS ... started by /EFI/systemd/systemd-bootx64.efi
on /dev/nvme0n1p1" -- systemd-boot chainload works; `dualboot 1
--apply` restarted straight into Pop (Secure Boot off: Pop's
systemd-boot is unsigned).
Restarting Pop returns to Nexus; `nexus server` says "Firewall: on";
the disk password is asked at Nexus's start (its / is encrypted).

**Asus 29 Oct: `nexus server add website <folder> --apply` WORKS** on
the Asus itself (Caddy on 8080, port opened). The phone did not load
it -- most likely the phone was on mobile data or its VPN (to confirm).
Hosting from home for everyone is not done: it needs a domain, port
forwarding and a public IP (CGNAT often), and it shows the home IP.
Planned: `nexus server publish` through a tunnel (see docs/ROADMAP.md).

**Patch 85 (0.1.59):** baresip (SIP calls, scriptable from Python;
Pop offers it too) + Opus + PipeWire or PulseAudio module in the
desktop image, so people can build calling on it with nothing to add.

**Patch 86 (0.1.60): `nexus specs`.** The machine in one screen: the
Nexus node-graph logo in colour + a 16-colour and true-colour test
bar, then host, model, Nexus version/edition, base, kernel, uptime,
desktop, resolution, shell, CPU, GPU(s), RAM, disks, battery, and the
Nexus-only lines (Secure Boot, encryption, firewall, NTSync, add-ons,
image signature, build date). All read from /proc, /sys, os-release
and rpm-ostree -- nothing installed, nothing guessed; a failed read
goes to "Could not read:", never shown as a value. `--plain` drops
colour (bug reports), `--json` for scripts. Colour only when stdout is
a terminal. New system/specs.{hpp was already there, cpp new} + 9
specs_test cases. 591 tests pass. Tested here (container: fields with
no data correctly drop out); the full picture shows on the Asus.

**Verified Asus (1 Nov):** Minecraft server passed -- `Done (13.0s)`,
rcon `list` answered, `nc` from the Acer reached 25565. Website reached
from the Acer (`curl -I` -> 200 from Caddy); the phone failure was the
phone (Firefox HTTPS-Only / guest Wi-Fi), not Nexus. `nexus server
remove minecraft --apply` removes the unit, keeps the world.

**Two bugs found 1 Nov -- fixed in patch 87 (0.1.61), not yet on the Asus:**
1. CLOCK: chrony's `makestep 1.0 3` only steps the clock in its first
   3 checks, so a laptop that wakes with a wrong clock never catches
   up (the Asus sat 3 days behind; Firefox then rejected every HTTPS
   cert). Fix: the image's /etc/chrony.conf says `makestep 1.0 -1`
   (set straight whenever >1 s off; the build fails if the line is
   not there exactly once). `nexus doctor` now reads `chronyc
   tracking`: more than a minute behind network time is FAIL, ahead
   is warn, both with `sudo chronyc makestep`.
2. TIME ZONE: with nothing chosen in the installer, the machine ends
   up America/New_York. Fix: `nexus-timezone check | pick | keep`.
   `pick`: type a city, choose from the matching zones, confirm
   (sudo). Setting or keeping a zone writes /etc/nexus/timezone-chosen.
   Until then: a note at first boot, one notification per person 2 min
   after login (button opens `pick` in a terminal), and a "Time zone"
   warn line in `nexus doctor`. No online lookup (auto stays opt-in).
   Test on the Asus: `timedatectl` -> `System clock synchronized: yes`;
   `grep makestep /etc/chrony.conf`; `nexus doctor`; `nexus-timezone
   pick`. The notification only shows on a machine still on New York.
**Asus 1 Nov, 0.1.61 KDE: BLACK SCREEN after update.** Fedora's
weekly updates brought KDE Plasma 6.7.5 -> 6.8.2 (no kernel change).
TTY3 login worked; sddm running; kwin_wayland 6.8.2 dumped core:
"Couldn't open libGLESv2.so.2" (KWin opens it at start, no package
requires it, weak deps are off). Rolled back to 0.1.59 from the boot
menu (0.1.60 was never installed on the Asus: two deployments kept).
**Patch 88 (0.1.62):** libglvnd-gles in every desktop image; the
build fails if libGLESv2.so.2 is missing. Lesson: a weekly rebuild
can break the desktop with no Nexus change -- test the KDE image in
the VM before updating the Asus.

**Asus 1-2 Nov: 0.1.61 + 0.1.62 PASSED.** `grep makestep
/etc/chrony.conf` -> `makestep 1.0 -1`; timedatectl "System clock
synchronized: yes"; doctor all ok incl. the new Clock and Time zone
lines (zone Europe/Skopje, so no New York nudge -- that path untested
on hardware); `nexus-timezone pick` runs; KDE back on 0.1.62 (no
layered libglvnd-gles needed). The 0.1.62 update came through `nexus
update` (history: "update -> Nexus 0.1.62 [applied]"), not by itself.

**Patch 89 (0.1.63), not yet on the Asus:**
- `nexus install <typo>`: "No package named X. Did you mean Y?" (new
  system/suggest.{hpp,cpp}, 6 tests; java-lastest-openjdk ->
  java-latest-openjdk). With no package lists read it says so instead
  of guessing.
- install/setup/options/source as root fetch the package lists
  themselves (`dnf makecache -q`) when none are there; without root
  they say "run it with sudo and it fetches them first".
- /etc/profile.d/nexus-dnf-hint.sh: interactive `dnf`/`sudo dnf`
  install/remove/upgrade that FAILS is followed by the Nexus command
  for it. dnf itself runs unchanged; `command sudo dnf` bypasses.
- doctor: Updates suggests `nexus update` (was `sudo bootc upgrade`,
  which bypasses history); the "Unused ?" line is hidden when it has
  nothing to judge. `nexus guide` UPDATING rewritten around `nexus
  update`.
Test on the Asus: `nexus install java-lastest-openjdk`; `sudo dnf
install vlc` (dnf's error, then the hint); `nexus doctor` (Updates line,
no Unused line).

**Asus 3 Nov: 0.1.63 PASSED** -- typo hint (java-lastest -> java-latest),
install staged with dnf agreeing, `sudo dnf install vlc` showed Fedora's
read-only error then the Nexus hint, doctor all ok with the new Updates
line. Auto `dnf makecache` not exercised (lists were present).

**Patch 90 (0.1.64): `nexus vpn check`** (also plain `nexus vpn`; was
`keys`). Read-only, sends nothing. Lines: VPN (which kind, on which
interface: Mullvad, Proton VPN, Tailscale, NordVPN, WireGuard, OpenVPN),
Traffic (`ip route get 1.1.1.1`), IPv6 (`ip -6 route get`: LEAK when it
leaves outside the tunnel), DNS (resolvectl: servers actually used --
"~." links win, else +DefaultRoute links + global -- each checked with
`ip route get`; LEAK when outside), Kill switch (Proton pvpnksintrf0,
Mullvad lockdown-mode, else `nft list ruleset` as root: `oifname !=
"<tun>" ... reject|drop`). Exit 1 on a leak. Not checked: the public
address (needs a server). New system/vpn_check.{hpp,cpp} + 13 tests.
Tested here with stand-in ip/resolvectl only. Test on the Asus: with no
VPN, then with Proton/Mullvad (free tiers) or the Acer's WireGuard,
plain and with sudo.

**Asus 3 Nov: 0.1.64 vpn check PASSED** with no VPN and with Proton
(proton0: IPv4, IPv6, DNS 10.2.0.1 + 2a07:b944::2:1 all inside). Bug:
Proton's ADVANCED kill switch is `pvpnksintrf1`, only `...0` was known
-> "Off". (nft tables there: netavark, firewalld, nm-wg-proton0.)
**Patch 91 (0.1.65):** any pvpnksintrf* = Proton's kill switch (no root
needed); disconnected with the kill switch blocking (route into
pvpnksintrf*) is reported as the kill switch working, not "no VPN".
+2 tests. Test: `nexus vpn check` (no sudo) with Proton connected, then
disconnect Proton (kill switch on) and run it again.

**Asus 4 Nov: 0.1.65 PASSED** -- Proton advanced kill switch "On";
disconnected: "kill switch is blocking". Bug: IPv6 routed into
pvpnksintrf1 (Proton blocking IPv6) showed as LEAK. **Patch 92
(0.1.66):** IPv6 into pvpnksintrf*/ipv6leakintrf* = "Blocked by Proton
VPN", ok. +1 test.

**Asus 4 Nov: 0.1.66 PASSED** (vpn check complete).

**Patch 93 (0.1.67): cybersecurity add-on + `nexus cyber`.**
- Add-on renamed security -> **cybersecurity** (profile file renamed;
  `nexus setup security` still works and becomes cybersecurity; the
  wallpaper maps to the old security picture). Same six native tools
  plus distrobox.
- `nexus cyber`: Kali's tools in a rootless Distrobox container
  "nexus-cyber" (docker.io/kalilinux/kali-rolling), sizes small =
  kali-tools-top10, medium = kali-linux-default, full =
  kali-linux-everything. `create <size> [--root] --apply` (apt inside
  shows the exact download and asks), `shell`, `run <tool> [args]`,
  `export <tool> [--app] --apply` (distrobox-export to ~/.local/bin or
  the menu), `remove --apply`. Refuses sudo (the box is the user's);
  `--root` box for Wi-Fi monitor mode / raw sockets. Legal line shown.
  create/export/remove in the user's history
  (~/.local/state/nexus/transactions.log). New system/cyber.{hpp,cpp}
  + 3 tests. Tested here with a stand-in distrobox only.
- NOT done: BlackArch (`--from blackarch`, planned), development
  add-on toolchains and `nexus dev help` (next).
Test on the Asus: `sudo nexus setup cybersecurity --apply`, restart,
`nexus cyber`, `nexus cyber create small --apply`, `nexus cyber run nmap
--version`, `nexus cyber export nmap --apply` then `nmap --version`,
`nexus cyber remove --apply`.

**Asus 6 Nov: 0.1.67 PASSED** -- setup cybersecurity (first try failed
only because Proton's advanced kill switch had cut the internet:
"0 of 7 installed", nothing changed), `nexus cyber create small` (apt in
Kali installed kali-tools-top10 incl. burpsuite), export nmap -> `nmap
--version` = Nmap 7.991 from the box, `remove --apply` also removed the
exported nmap (distrobox does it).
**Patch 94 (0.1.68):** a mistyped command is named before any flag is
read, with the closest real one (`nexus cuber run nmap --version` had
printed Nexus's version); `nexus cyber run <tool> ...` passes every
later word to the tool (`--version`, `-h`, even `--root`); remove's
text says exported commands go with the box.

**0.1.68 PASSED on the Acer** (typo hints: cuber -> cyber, instal -> install).

**Patch 95 (0.1.69): `nexus dev`** -- languages on request: cpp, python,
js, java, rust, go, ruby (names like c++, py, node, golang, rb work).
`nexus dev add rust,go --apply` creates "nexus-dev" from
registry.fedoraproject.org/fedora-toolbox:<VERSION_ID> if needed, runs
`sudo dnf install` inside (dnf asks), exports the commands to
~/.local/bin with distrobox-export and prints each version. A language
whose main command is already on the system is skipped (C/C++ and
Python come with the development add-on). `remove <langs> --apply`
unexports and dnf-removes; `remove all --apply` deletes the box;
`shell`; `nexus dev help <topic>`: git python js java rust go ruby cpp
containers ports ssh. Refuses sudo. History records add/remove.
New system/dev.{hpp,cpp} + 4 tests. Tested with a stand-in distrobox
only; package names (java-devel, nodejs-npm, clippy, rubygem-bundler...)
are unverified until the Asus runs it.
Test on the Asus: `sudo nexus setup development --apply` (if not
installed), restart, `nexus dev`, `nexus dev add js,java,rust,go,ruby
--apply`, new terminal, `node --version; cargo --version; go version;
java -version; ruby --version`, `nexus dev help rust`, `nexus dev remove
go --apply`.

**Asus 8 Nov: 0.1.69 PASSED** -- node v22.23.1, cargo 1.99.1, go 1.26.10,
ruby 4.0.9 from nexus-dev; `nexus dev help rust`; `remove go` (dnf's
"remove failed: /usr/bin/go" lines are golang's alternatives links,
harmless). `java -version` showed 27-ea: that is the host's layered
java-latest-openjdk from the 3 Nov install test, so dev add skipped Java.
SSHole check: sshd is glibc (libc.so.6) with usepam yes -> not affected.
BUT sshd was active with passwordauthentication yes and the public zone
allowed ssh.
**Patch 96 (0.1.70):** desktop images disable sshd.service and remove
ssh from the public zone; doctor "Remote login" line (warn when sshd is
active and the firewall lets ssh in; suggests `sudo systemctl disable
--now sshd`). Existing machines: the zone change arrives only if
/etc/firewalld/zones/public.xml was never changed locally (nexus server
changes it), so doctor is how they find out.

**Patch 96's image build FAILED (8 Nov, both editions):**
`firewall-offline-cmd --zone=public --remove-service=ssh` -> "Can't use
lokkit options with other options" (--remove-service is also lokkit's).
**Patch 97 (0.1.71):** `--remove-service-from-zone=ssh`. Also: the
research report on other distributions is in
docs/research/distro-features-2026-11.md, summarised in ROADMAP.md as
"To consider". (Patch 96 was committed with the 0.1.69 message by
mistake; harmless.)

**Asus 8 Nov: 0.1.71 PASSED** (image builds again; doctor "Remote login:
Off"; services 42 -> 41).

**Patch 98 (0.1.72): automatic rollback when the desktop does not start.**
Own unit, not greenboot (greenboot-rs is in Fedora 44, but its checks
may run before the display manager -- a check waiting for the login
screen could then fail every update; unverified, so not risked).
nexus-boot-check.service (After=display-manager, WantedBy graphical)
runs /usr/libexec/nexus/boot-check: only for a NEW deployment (ostree
commit differs from /var/lib/nexus/boot-check/last-good); waits up to
150 s for sddm-greeter | plasmashell | lxqt-panel; pass -> remember as
good; fail with a known-good one -> log to failures.log, `bootc
rollback` (rpm-ostree rollback as fallback), reboot. No known-good ->
log only. Skipped without a display manager or with
/etc/nexus/no-boot-check. Test switch (once):
`sudo touch /var/lib/nexus/boot-check/test-fail`, update, restart ->
it should roll back by itself; `nexus doctor` then warns "Boot check".
Tested here with stand-ins only.

**Asus 8 Nov: 0.1.72 boot check PASSED with the test switch** -- went
back by itself to the 17:30 build (● on it, 18:41 below), failures.log
written, doctor warned. The boot menu entry "(ostree:0)" is always the
default, so after a rollback it IS the older version.

**Patch 99 (0.1.73):**
- `nexus rollback [--apply]`: shows now / go back to (version, build
  time, digest), apps added on top that the older one lacks, and files
  in /etc changed while this version ran (mtime newer than the
  deployment's .origin and different from the other deployment's /etc,
  under /ostree/deploy/<stateroot>/deploy/<id>/etc). --apply (root)
  saves those to /var/lib/nexus/etc-saved/<time>/ first, then
  `rpm-ostree rollback`; history records it. Refuses while an update is
  staged (points to `rpm-ostree cleanup --pending`).
- `nexus rebase`: status (follows which ref, pinned or not);
  `kde|minimal` switch edition; `sha256:<64hex>` pin one build;
  `latest` follow the edition's tag again. `rpm-ostree rebase`, history.
- boot check: the test switch is now logged as "TEST: ..." and doctor
  says "Test rollback ... worked. Nothing was wrong." (no warning).
New system/rollback.{hpp,cpp} + 4 tests. Tested with stand-ins only.
Test on the Asus: `nexus rollback` (read the /etc list), `sudo nexus
rollback --apply`, restart, `rpm-ostree status`; `nexus rebase`.

**Asus 9 Nov: 0.1.73 PASSED** -- `sudo nexus rollback --apply` went back
(8 Nov), plan view read both builds, a planted /etc/nexus-rollback-test
was listed "(new)"; `nexus rebase` status right; typo hint tollback.

**Patch 100 (0.1.74): gaming baseline.** `nexus gaming` (also `doctor`,
old `tune`) reports NTSync, split lock, GameMode (honest: responsiveness
under load, not FPS), MangoHud, Gamescope, power profile, memory maps,
and how to measure (mangohud / gamescope -r 60 -F fsr launch options).
New: Intel split-lock penalty -- where /proc/sys/kernel/
split_lock_mitigate exists and is 1, --apply writes
/etc/sysctl.d/90-nexus-split-lock.conf (=0) and sets it now. `nexus
gaming ntsync|split-lock --apply` for one; `nexus gaming off --apply`
removes Nexus's two files (split lock back to 1). History records all.
New system/gaming.{hpp,cpp} + 5 tests. Opt-in, not default.
Test: Acer (Tiger Lake: likely has the knob) and Asus (Gemini Lake:
may not -- then the line says nothing to fix).

**Acer 9 Nov: 0.1.74 gaming PASSED** -- split-lock --apply set 0, off
put it back to 1. Bug seen: a split-lock-only run printed "[ ok ] NTSync
not on yet" under "Now:"; fixed in 101 (the Now: lines show only what
the run changed; NTSync not on is "note", not "ok").

**Patch 101 (0.1.75): `nexus security`.** `nexus security` (= `nexus
security audit`) reads only: firewall, network listeners (ss -tulnH,
loopback and DHCP client ports left out), remote login, disk
encryption, Secure Boot, SELinux, signed image ref, days since the
booted build (warn >14, FAIL >30), Nexus's kernel hardening tier (n of
14), accounts with no password (root only, /etc/shadow), and what
starts by itself for the person (~/.config/autostart, user units; the
sudo user's home under sudo). Exit 1 on any FAIL. Honest note: no
virus scan. `sudo nexus security harden --apply` writes only the weak
settings to /etc/sysctl.d/90-nexus-hardening.conf and runs sysctl -p;
`harden off --apply` removes it. History records both. Tier: kptr/dmesg
restrict, unprivileged BPF off, no redirects/source routes, syncookies,
rp_filter (written loose 2 -- VPN-safe; strict 1 also counts as fine),
fs.protected_*. Not ptrace/userns/module lockdown. New
system/security_audit.{hpp,cpp} + 7 tests.
Test: Asus and Acer: `nexus security`, `sudo nexus security`,
`sudo nexus security harden --apply`, Proton VPN still connects,
`sudo nexus security harden off --apply`.

**Patch 102 (0.1.76): fixes from an outside review (9 Nov).** A source
review of the 0.1.75 snapshot (David ran it with another AI; findings
checked against the code here, all confirmed) found:
- **Shell quoting.** About 20 commands hand-wrapped values in '...'; a '
  in a value ended the quoting. `shellQuote` and `isPackageName` now in
  system/process.hpp, used at every such site (dnf, rpm, apt, flatpak,
  pacman, tar, podman, distrobox, locale, services' rpm -qf). `nexus
  container` and `nexus source` refuse a name that is not a package
  name (and one starting with -). Container exports: a package's file
  is exported only if `isPlainPath` (letters, digits, / . _ + - @ :);
  `podman exec ... sh -c` quotes its command.
- **WireGuard.** `TunnelSettings::invalid()` checks each field's shape
  (IP[/prefix] lists, host:port or [v6]:port, 44-char key, wg-quick's
  interface-name rule) and any control character; `nexus vpn config`
  refuses; renderTunnel writes nothing if a value has a line break
  (PostUp would run as root).
- **Installer.** A disk with any partition, even one with no filesystem
  lsblk knows, is "in use": never auto-erased (Installation
  Destination screen or install beside it in free space).
- **Rollback.** Each /etc file is saved and size-checked first; any
  failure stops before `rpm-ostree rollback`, listing the files.
- **Website hosting (as root).** A symlink leading outside the folder,
  or a device/pipe, refuses the copy (a link to /etc/shadow would have
  been published); the copy goes to site.new and replaces the site only
  when complete.
- **The audit itself.** Firewall: every active zone's target (ACCEPT =
  FAIL) and ports (a 10000+ port range = warn); Disk: separate /home and
  disk swap; System image: policy.json must require the Nexus key
  (policyRequiresKey), not only the signed: prefix.
- **Docs.** image/README.md and docs/overview.md no longer say the
  kernel is unsigned or Secure Boot must be off; README test count.
+9 tests. **Not done yet (next):** argv execution instead of /bin/sh
for runCommand (quoting closes the holes; argv removes the shell);
`nexus cyber` with its own home folder (distrobox is not a sandbox --
say so in `nexus cyber`); pin GitHub Actions to SHAs and the
bootc-image-builder / base / Caddy / Minecraft images to digests.

**Acer 9 Nov: 0.1.75 security PASSED** (Pop!_OS: disk encrypted, all 14
kernel settings on, Secure Boot off warned). Bug: without firewalld it
said "not checked", then "the firewall is off" on the Listening line.

**Second outside review (9 Nov) of 0.1.76:** all six earlier findings
fixed or improved; four new points, all fixed in **patch 103 (0.1.77)**:
- **Installer fails closed.** in_use() raises if lsblk fails or does
  not list the disk itself; the kernel's /sys/block/<disk>/<part> list
  is read too. A disk that cannot be read goes to the installer's own
  screen, never to clearpart.
- **Firewall:** each active zone read with --info-zone, runtime AND
  --permanent; ACCEPT in either = FAIL, wide port range = warn, runtime
  differing from saved = warn (fix: firewall-cmd --reload). Without
  firewalld: ufw status under sudo (Pop/Ubuntu); Listening says
  "firewall not checked" instead of "off" when it could not be read.
- **Website swap:** site -> site.old, site.new -> site, put back if
  that fails; site.old removed only after.
- **Rollback backups** compared byte for byte (sameContents), not size.
- `nexus cyber` says it is not a sandbox (sees home, runs as you; VM for
  untrusted tools). Own home folder still open: distrobox-export inside
  a box with its own $HOME would export to the wrong ~/.local/bin --
  needs the host path passed in; do it with a real test.
+1 test (parseZoneInfo). Still open: argv execution (posix_spawn),
cyber home, pinned digests/SHAs.

**9 Nov: 0.1.77 PASSED on the Acer (ufw on) and the Asus** (harden added
4 settings, all 14 on, harden off removed the file, history right).
The Asus showed: zone public lets in ssh (sshd off) -- a locally changed
public.xml keeps it past 0.1.71's image change (ostree keeps edited
/etc files) -- and 8080/tcp (not from Nexus); zone trusted FAIL without
saying for what (not from Nexus either).

**Patch 104 (0.1.78): third outside review + the Asus.**
- **Guessable /tmp names** (/tmp/nexus-plan-check[-dnf]-<pid>,
  -decompress-, -pacman-) for child stderr, written as root under sudo:
  a pre-planted symlink would be followed (Fedora's protected_symlinks=1
  blocks it; no longer relied on). New `ScratchDirectory` (mkdtemp 0700,
  removed in its destructor; "/dev/null" if none -- the per-file
  remove() calls were dropped so /dev/null is never removed as root).
- **Missed in 102:** values quoted '...' across two source lines --
  `requested` in both plan checks (apt, dnf: under sudo install),
  flatpak remote/app ids, skopeo origin, curl url. All shellQuote now.
- **Firewall audit:** a target-ACCEPT zone judged by what is in it
  (trustedReach): containers/VMs ok, VPN tunnel warn, real interface or
  sources FAIL; names the interfaces. ssh allowed in a zone while sshd
  is not enabled: warn with the remove-service command.
- Peek mounts (installer, boot-entry) ro,nodev,nosuid,noexec; build.log
  removed and ignored.
- Not changed: publish.sh's verify --insecure-ignore-tlog matches
  signing with --tlog-upload=false (keys are what machines check);
  dropping it alone would fail every publish. qcow2 is not published by
  CI (no artifact upload) -- keep it that way.
+3 tests.

**Asus 9 Nov, 0.1.78:** active zones = public (wlo1, pvpnksintrf1) and
trusted (source 10.88.0.0/16 -- podman's network, put there by
netavark). The audit FAILed it as a real network, with a fix for
interfaces. ssh warning worked; David removed ssh from public.
**Fourth outside review of 0.1.78: pass** (scratch dirs, quoting, mount
options, security module checked; no new injection).

**Patch 105 (0.1.79):**
- trustedReach judges sources too: podman's/docker's default networks,
  or the network of a container bridge here (from `ip -o -f inet addr
  show`, parseIpAddresses) = Containers (ok: "they can reach every
  port here"); any other range = FAIL, fix --remove-source.
- policyRequiresKey parses policy.json (json.hpp) instead of a text
  scan: every docker scope at or under Nexus's (more exact ones win for
  those images) needs a sigstoreSigned rule with the key.
- "system built today / 1 day ago".
+2 tests. Accepted risk: image/config.toml's nexus/nexus wheel account
is for a local test VM only; CI uploads no qcow2. Never publish one.

**Fifth outside review (0.1.79): no new vulnerabilities.** Asked whether
a scope's requirement array is AND or OR: containers-policy.json(5) says
all must be satisfied, so sigstoreSigned + insecureAcceptAnything is
still enforced -- the suggested "insecureAcceptAnything = not enforced"
guard would be a false red; not added. The same page: a policy.json in
the puller's home is read instead of /etc's.
Correction: 8080/tcp on the Asus IS Nexus's -- the website server's
default port (server.hpp), not "not from Nexus" as said for 104.

**Patch 106 (0.1.80):**
- Installer %post writes /etc/hostname = nexus-<6 random hex> (was
  "nexus" on every machine: collisions, one name for all). The audit
  shows a " -- " Name line with a rename command on older installs.
- Server add (minecraft, website): says the firewall now lets the port
  in, who can reach it, and how to close it.
- Audit: /root/.config/containers/policy.json present (as root) = warn,
  it may be read instead of /etc's.
- trustedReach: comment that IPv6 sources are not network-matched yet
  (judged real network: a false warning at worst).
- ROADMAP: the nexus security direction (rules + next four checks).

**10 Nov:** weekly run #82 failed on both editions at `podman pull
quay.io/fedora/fedora-bootc:44` (502 / "can't talk to a V1 registry"):
quay.io, not Nexus; re-run. A "trust" audit for a video (Claude, fresh
chat, zip of 0.1.80 = GitHub 6bfd40c, 656/656 tests): nothing hidden or
malicious; for an ordinary person it named: preview by one person, back
up before installing (installer already says so), ISO built on the
laptop so it cannot be proven to match the source, containers by tag.

**Patch 107 (0.1.81):**
- **ISO on GitHub:** .github/workflows/installer-iso.yml (by hand,
  edition choice): builds with image/build-installer.sh from the signed
  ghcr image, names it nexus-core-<edition>-<version>.iso, sha256 +
  nexus-build.txt in the run summary, actions/attest-build-provenance,
  ISO kept as the run's download 90 days (~3 GB, over the 2 GB release
  asset limit). Upload THAT file to archive.org; anyone checks it with
  `gh attestation verify <iso> --repo DAVIDOSDAS-official/nexus-core`.
  build-installer.sh: podman run -it only with a terminal (CI has none).
  NOT RUN YET -- first run is the test.
- **Containers by digest:** image/pins/images.list (Kali, Minecraft,
  Caddy, fedora-toolbox:<FEDORA> and :latest, archlinux, debian,
  ubuntu); weekly-image.yml "Pin container images" runs
  image/pin-images.py (HEAD requests: not counted by Docker Hub's pull
  limit; an image it cannot resolve stays by tag and is named, never
  fails the run) -> image/pins/pinned-images (gitignored) ->
  Containerfile copies image/pins/ to /usr/share/nexus/pins/ and prints
  "Pins: N ... by digest" or "Pins: none (local build)". pins.{hpp,cpp}:
  pinnedImage(ref) = ref@digest when listed; used in server quadlets,
  server podman pull (now quoted), Kali and dev box create,
  Containers::create. +2 tests. Network part of pin-images.py untested
  here (no registry access): run it on the Acer first.
- Build step: base image pull retried 3x, 60 s apart.
- README: verifying an ISO with gh attestation; containers by digest.

**10 Nov: 0.1.81 pushed.** Second "trust" audit (fresh chat, zip of
0.1.81): 658/658 under ASan/UBSan/LSan, parsers fuzzed 8.5 M runs, no
memory bugs; ShellCheck clean. Findings H1-H3, M1-M5, L1-L7, docs.

**Patch 108 (0.1.82), from it:**
- **M1** Shop starts /usr/bin/nexus, /usr/bin/pkexec, /usr/bin/flatpak
  (pkexec searched the caller's PATH: a nexus in ~/.local/bin would have
  run as root). Source build: the nexus next to the Shop. Compiled.
- **H1** installer-iso.yml: skopeo reads the tag's digest, cosign
  verifies that digest with image/signing/cosign.pub, pulls exactly it,
  tags it, builds with PULL=no; digest in the run summary.
- **H2** the signing key is written (image/signing/write-key.sh) only
  in the check step (and deleted there) and in the publish step; not on
  disk through tests/build. tools/pin-actions.py (David runs it: needs
  github.com) pins every `uses:` to a commit SHA; .github/dependabot.yml.
- **M2** website copy: system/site_copy.{hpp,cpp} -- one pass, openat +
  O_NOFOLLOW, links copied as links (only if they stay inside), hidden
  files left out (.well-known kept), and under sudo only what SUDO_UID
  could read (getgrouplist). +5 tests incl. the attacks.
- **H3** .github/workflows/security-watch.yml twice a day: Firefox in
  the published KDE image (label nexus.firefox, added by the weekly
  Build step) vs Fedora's newest; newer -> starts weekly-image.yml.
- **L1** ProtonDB total clamped (+1 test). **L2** audit line
  "Containers: N pinned". **L3** profiles/aliases fallback found from
  /proc/self/exe, never the current folder. **L5** cyber --root says it
  runs as root. **L6** kdeconnect only if kdeconnectd is installed (and
  the audit names a leftover).
- **M4** SECURITY.md: what happens if the signing key is stolen.
- Docs: README "What Nexus contacts", "How this is made" (AI said
  plainly), "Leaving Nexus" (rebase to Kinoite -- TEST IN A VM).
- Open: M3 (rootful podman ports vs firewalld -- test on the Asus),
  M5 (opt-in auto-staging -- David decides), L4 (builder image by
  digest), L7 (dnf hint wraps sudo), tlog/Rekor, identity in public
  files (David decides), main.cpp split.

**10 Nov, Asus 0.1.82:** audit 11 ok: zones public (wlo1, pvpnksintrf1)
and trusted (10.88.0.0/16 = podman, now judged right); 8 containers
pinned; kdeconnect leftover named (kdeconnectd not installed);
8080/tcp = conmon (a Nexus website server). rpm-ostree-countme.timer:
enabled (Fedora default). **David's decisions:** M5 auto-staging NO;
his name stays in public files (DAVIDOSDAS says it anyway) -- do not
scrub HANDOFF.md or rewrite history.

**Third trust audit (0.1.82):** all 4 pinned Action SHAs genuine;
664/664 under sanitizers; old fuzz crashes fixed. **New, my bug:**
copySiteTree checked link targets as text, so `up -> .` plus
`tls.key -> up/up/../../data/caddy/...` left /srv inside the Caddy
container and could serve Caddy's TLS key.

**Patch 109 (0.1.83):**
- The reviewer's fix, applied as given (checked: its 2 new link tests
  fail on 0.1.82, pass with it): links are created only after the whole
  copy, each checked in the finished root-owned copy step by step --
  refused if any step is a link, leaves the folder, or is not there
  (e.g. a hidden .env). Plus its CI change: installer-iso.yml stops
  (not warns) if nexus-build.txt names another digest.
- Server files left out of websites (isServerSideFile: .php .py .sql
  .db .bak .key .pem ... and ~ backups -- wp-config.php would have
  served the DB password as text), listed when copying. +1 test.
- Reader rights also from PKEXEC_UID (the Shop), not only SUDO_UID.
- README: countme stated as on by default with how to turn it off;
  Leaving Nexus uses $(rpm -E %fedora) and covers Minimal (no Fedora
  LXQt atomic: Kinoite, Silverblue or Sway Atomic).
- Still open: M3 (David tests: remove 8080 from the firewall, connect
  from the phone), the Caddy link curl test (moot after the fix, but
  confirms the finding), M4 tlog, L4 builder by digest, main.cpp split.

**11 Nov: weekly run, minimal failed** at dnf ("Unable to validate
zchunk checksums" = a Fedora mirror mid-sync); KDE fine; re-run.

**Adversarial audit of 0.1.82 (24-page PDF, 11 Nov):** no critical, no
default path to root; 668 tests under ASan/UBSan, ~976k fuzz runs.
H1 key in GitHub secrets (David decides: sign locally vs protected
environment; rotate). M1 WireGuard key in sh's argv (PoC 20/20). M2
ESP folder names into GRUB custom.cfg unescaped. M3 first-boot chown
through user symlinks (when the marker was not written). M4 repo
metadata names into `rpm-ostree install` unquoted (PoC ran id as root).
M5 `nexus rebase kde` could not move unverified origins to signed. M6
builder :latest privileged. L1-L8, I1-I7.

**Patch 110 (0.1.84):**
- M4: layerWithRpmOstree(vector) checks isPackageName and quotes each
  (Refused otherwise); language add drops non-package names and quotes
  the dnf line; locale, dev box package lists and flatpak app quoted.
  tools/check-shell-quoting.py runs in ctest (ShellQuoting): fails on a
  value joined into runCommand/system/popen without shellQuote (it
  cannot see commands built in a variable first -- hence the checks at
  the source too). +1 test (PlanApplyTest).
- M1: runProgram(argv, stdin) in process.{hpp,cpp} (posix_spawnp,
  pipes, no shell, SIGPIPE ignored while writing); publicKeyOf passes
  the key on wg's stdin. Test: a /proc/*/cmdline watcher over 200 runs
  with a run-time secret sees it 0 times (the old sh way: 8 of 8 in a
  side check). +2 tests.
- M2: isSafeLoaderPath / isSafeFsUuid; readLoaders skips and grubEntry
  refuses anything else; chainloader path in quotes. +1 test with the
  reviewer's exact name. (USB disks are still scanned -- open.)
- M3: first-boot writes each person's labwc keyboard file with
  `runuser -u <name>`, never root; no chown. Replayed the attack with a
  real user: Permission denied, target stays root:root.
- M5: parseImageRef reads `ostree-unverified-registry:` too;
  transportFor() makes every ghcr.io/davidosdas-official image
  ostree-image-signed:docker://; `nexus rebase signed` (and every
  rebase) switches to it after checking policy.json; the audit's advice
  points there. +1 test.
- L2: SecureBoot/SetupMode/MokListRT read by exact GUID; MokSBStateRT
  (shim validation off) read and reported (audit warn, fix mokutil
  --enable-validation); "A key of your own is enrolled" replaced (shim
  15.3+ copies Fedora's key into MokListRT). +2 tests.
- L3: dualboot mounts ro,nosuid,nodev,noexec(+noload/norecovery);
  readWholeFile caps at 4 MiB; grub.cfg only if a regular file.
- L4: textFormat: Text.PlainText on every Shop Text (76) and Chip;
  screenshots https only. qmllint: no new warnings; Shop starts
  offscreen without QML errors.
- L5: compareFragment with a NUL byte compares bytes (no hang). +1 test.
- L7: installer refuses to shrink if a swsuspend (hibernated) swap is
  on the disk or the ext4 has needs_recovery; peek mount with noload.
- I1 boot-check matches process names (-x), not any command line.
  I2 registry login through env:. I4 ResolutionPlan status defaults to
  Failed. I5 gaming says what the split-lock guard protects.
- Docs: image/signing/README.md no longer claims a key "only the
  maintainer holds"; SECURITY.md lists what changes without --apply.
- Open: H1 + key rotation + Rekor (decisions), M6/L6 digests for
  bootc-image-builder, skopeo, fedora-bootc (David runs skopeo), I2
  persist-credentials (sed on David's pinned workflows), L1 freshness
  warning, L8 rootless servers, I3 history source labels, USB disks in
  dualboot, systemd hardening of root units, I7 in SECURITY.md (done
  as part of the list).

**11 Nov, Asus 0.1.84:** kdeconnect removed from public; 8080 removed at
runtime for the M3 phone test (result not reported yet -- the audit then
rightly said runtime differs from saved). Digests read on the Asus:
bootc-image-builder dab86ee6..., skopeo/stable ed369b58..., fedora-
bootc:44 48acc575.... Doctor still warned about the 8 Nov rollback.

**Adversarial audit round 2 (0.1.84):** 11 of 20 fixed, 3 partly, 6
open (H1 the one that matters, now worse: the signing step shares the
runner with Tests). New: N1 registry label into notifications (M), N2
world-readable server backups with Caddy keys (M), N3 control chars to
terminals, N4 btrfs log replay, N5 FIFO hangs first boot, N6 swap-file
hibernation, N7 boot check spoofable by name, N8 global allow-list.

**Patch 111 (0.1.85):**
- N1: parseImageInspect keeps nexus.version only if it is
  ^[0-9][0-9A-Za-z.+~-]{0,31}$ and Created only if timestamp chars;
  everything downstream (notification, terminal, --lines) then says "a
  new version". +1 test (the reviewer's PoC label).
- N2: backups dir 0700, tar under umask 077, archives (old ones too)
  0600; tmpfiles.d nexus-backups.conf sets the dir 0700 at every boot.
  NOT a global umask(077) as suggested: `nexus history` reads
  /var/lib/nexus without sudo, and children (dnf, podman, distrobox,
  flatpak) would inherit it.
- N3: printable() in process.hpp (C0 incl. \n\t, DEL, UTF-8 C1 -> ?),
  applied where outside text is parsed: os-release of other disks,
  ProtonDB/Steam/AreWeAntiCheatYet fields, dnf Summary, Flathub
  name/summary/developer, language summaries; the installer prints the
  other system's name through isprintable(). +1 test.
- N4: dualboot mounts btrfs rescue=nologreplay, f2fs/bcachefs
  norecovery.
- N5: labwc_keyboard_file leaves anything but a plain file alone; each
  runuser under timeout 10. (No unit timeout: first boot waits for
  answers on the console.) PoC FIFO: returns at once.
- N6: installer refuses to shrink when the ext4 holds a hibernated
  /swapfile, /swap.img or /swap/swapfile (SUSPEND/LINHIB signature) or
  cannot be checked, or the disk has a crypto_LUKS partition.
- N7: boot check = display-manager.service active AND (sddm-greeter run
  by the sddm user OR a logind session of Type wayland/x11). David to
  verify on the Asus before relying on it (a false "no desktop" means a
  rollback).
- N8: check-shell-quoting.py allow-list keyed by (file, name) with
  today's use count; one more use fails (tested by injecting one).
- M6: build-installer.sh BUILDER and SKOPEO by digest. L6: base NOT
  pinned (the build upgrades nothing, a pinned base would freeze
  Fedora's fixes); its digest is a label (nexus.base-digest) and a
  line in the build log.
- Doctor: the boot-check warning turns ok once last-good is newer than
  failures.log.
- Open: H1 (done in 0.1.86), L1, L8, I3, USB disks in
  dualboot, systemd hardening of root units, runProgram caveats (>64 KiB
  input, process-wide SIGPIPE swap) noted.

**11 Nov, Asus 0.1.85:** N7 boot check verified: display manager
running, logind session 2 Type=wayland, so desktop_up is true.
David's workflow files (SHA pins, persist-credentials: false) synced
into this tree. GitHub account has two-factor login (David).

**Privacy and security audit round 3 (0.1.85):** H1 the one serious
item left; privacy P1 Firefox telemetry/studies/ad measurement (Fedora
build reports), P2 hostname in every DHCP request (links networks the
per-SSID MAC separates), P3 Count Me on, P4 no IPv6 temporary
addresses, P5 daily update check with no off switch, P6 gamecheck curl
follows redirects to http; R1 flatpak update version unfiltered, R2
bidi/zero-width pass printable(), R3 root units unsandboxed.

**Patch 112 (0.1.86):**
- H1: weekly-image.yml is three jobs. tests (contents: read only),
  build (packages: write, no key; pushes <profile>-unsigned with
  `STAGE=1 image/publish.sh`, digest as job output digest_<profile>),
  sign (environment: release, waits for David's approval; key is an
  environment secret; image/promote.sh signs exactly that digest,
  verifies with cosign.pub, then skopeo copy --preserve-digests moves
  <profile> and <profile>-<version>, digest read back). Machines never
  see an unsigned newest image any more (before, the tag moved before
  signing). Residual, said in image/signing/README.md: a stolen login
  with 2FA can still approve; a tampered build gets signed if approved.
  Real fix later: local signing with a hardware key. David's one-time
  setup in "Signing in its own job" below. Security-watch builds wait
  for approval too.
- Build retried once (podman build, layers reused; 11 Nov mirror
  error); stage push retried 3x.
- P1: image/firefox/policies.json -> /etc/firefox/policies/ (telemetry,
  studies, attribution locked off; sponsored/promos off, unlocked).
- P2/P4: image/network/30-nexus-privacy.conf ->
  /usr/lib/NetworkManager/conf.d/ (dhcp-send-hostname=false v4/v6, a
  global default only since NM 1.52; ip6-privacy=2).
- P3: Count Me off before the first dnf: sed on /etc/yum.repos.d,
  [*] countme=0 in /usr/share/dnf5/repos.override.d/80-nexus-no-
  countme.repo (an /etc file of the same name overrules it), timer
  masked; build fails if countme=1 is left.
- P5: `nexus update notify off|on|status [--apply]` writes/removes
  ~/.config/nexus/update-notify-off; the user unit has
  ConditionPathExists=!%h/...; refused under sudo (would land in
  /root); the first notification on an account says how to turn it off.
- P6: every curl has --proto =https (and --proto-redir =https with -L).
- R1: flatpak update ids must be [A-Za-z0-9._-]; versions printable(),
  64 chars max. R2: printable() also replaces U+061C, U+200B-200F,
  U+202A-202E, U+2060-2064, U+2066-2069, U+FEFF.
- Tests: tools/check-network-hygiene.py in ctest (curl https, policies
  JSON, NM keys); check-add-ons.sh checks the built image (policies,
  NetworkManager --print-config, countme masked/0). +5 unit tests.
- Open: R3 systemd sandboxing of root units, L1, L8, I3, USB disks in
  dualboot, Rekor, a network-destination allow-list test, key rotation
  after H1 (suggested by the audit; the key never left GitHub as far
  as is known, so rotation is a decision, not an emergency).

**Signing in its own job -- David's one-time setup (0.1.86):**
1. Settings -> Environments -> New environment `release`. Required
   reviewers: DAVIDOSDAS-official. Deployment branches and tags:
   Selected -> `main`. (Leave "prevent self-review" off: you are the
   only reviewer.)
2. In that environment: Add secret COSIGN_PRIVATE_KEY and
   COSIGN_PASSWORD, the same values as now.
3. Settings -> Secrets and variables -> Actions: delete the repository
   secrets COSIGN_PRIVATE_KEY and COSIGN_PASSWORD. Then nothing but the
   sign job can read them.
4. Each run: GitHub emails "waiting for review"; open the run, check
   the build jobs passed and the digests in their summary, Review
   deployments -> release -> Approve.

**Patch 113 (0.1.87): full engineering audit of 0.1.86** (David's zip =
patch 112 + his pinned fedora-version-check.yml). Baseline before any
change: 685/685 tests; ASan+UBSan build 0 reports; -Wall -Wextra
-Wshadow -Wconversion 17 warnings; cppcheck 0 errors; shellcheck 3
unused-variable notes; a new mutation fuzzer, 1.67 million inputs over
34 parsers, 0 crashes. Found and fixed (each with a test that fails on
0.1.86):
- A1 runCommand/plan_check read only the exit-code bits of a wait
  status: a command killed by a signal (OOM killer, SIGKILL, a crash)
  has 0 there and was reported ok. Bash (Fedora's /bin/sh) execs the
  last command of a line, so this was reachable. exitCodeFromStatus():
  128+signal, never 0.
- A2 transaction log: finishing a record truncated and rewrote the
  whole log in place, unlocked. Two nexus at once (Shop + terminal)
  lost records (25 of 150 survived the new test); a kill or full disk
  between truncate and write lost all history; a line break in a
  field forged records (5 read back from 2 written). Now: owner-only
  lock file <log>.lock (bounded wait), write-beside + fsync + rename,
  every field on one line, appends fsynced.
- A3 `nexus server backup`: Ctrl-C left the server stopped, a
  cut-short archive under a finished backup's name, and the record at
  "started"; "The server is running again." was printed whatever
  systemctl start answered; a server stopped on purpose was started.
  Now HoldSignals over stop..start, <name>.partial until whole,
  started only if it ran before, start result reported (exit 1).
- A4 runProgram wrote all input before reading any output: over 64 KiB
  both ways it waited for ever (3 MiB through cat: 10 s timeout on
  0.1.86). poll loop; SIGPIPE blocked per thread and drained, the
  process-wide handler no longer swapped.
- A5 the shipped nexus and nexus-shop had no hardening flags of their
  own (the image builds with plain g++, not rpmbuild). CMake option
  NEXUS_HARDENING (on): PIE, full RELRO, BIND_NOW, stack protector,
  stack clash, CET, _FORTIFY_SOURCE=3, _GLIBCXX_ASSERTIONS.
  tools/check-hardening.sh in ctest and in the Containerfile's builder
  stage (the image build stops if a program lacks them). NOT yet seen
  on Fedora's g++: the first image build is the test.
- A6 smaller: --domain could spell an option ("--browse");
  comparePortageVersions threw on 20+ digits; update-notified fell
  back to /tmp/.cache/nexus without HOME; seven struct members with
  no initial value; fetchUrl capped at 32 MiB; "0 MiB" for small
  sizes; dead readVariable() removed.
- New tests: tests/no-apply-changes-nothing.sh (46 commands without
  --apply against stand-in tools: no tool asked to change anything,
  no file changed; passes as user and as root in a container -- the
  rule holds), tests/fuzz/parser_fuzz.cpp (ctest quick pass;
  `parser_fuzz 60` for longer), Hardening. New workflow tests.yml:
  every push and pull request, release build + sanitizer build +
  fuzzer, read-only, no secrets. NOT yet run on GitHub.
- Checked and left alone (already right): all workflows pinned, no
  expressions in run blocks, no pull_request_target; no polkit,
  sudoers or setuid files in the image; JSON depth limit 200; XML pull
  parser not recursive; solver iterative; Shop runs programs by full
  path with argument lists.
- Open, with reasons, in the audit report (patch 113's reply): R3
  systemd sandboxing of the root units (cannot be tested without
  systemd; a wrong line in nexus-boot-check means a false rollback);
  firewall-cmd --reload after opening a port drops a person's
  unsaved runtime rules, and ports go to the default zone only;
  core/resolver.cpp and dependency_graph.cpp are used by tests only;
  main.cpp is 11,000 lines; L1, L8, I3, USB disks in dualboot, Rekor.

**Patch 114 (0.1.88): independent review of patch 113, seven findings,
all confirmed on 0.1.87 and fixed.** Not pushed by David at the time
(113 applied locally only); 114 applies on top of 113.
- R1 runProgram: waitpid failing with anything but EINTR (ECHILD when
  SIGCHLD is ignored) left `status = 0` and the program was "ok".
  Now exit -1, not ok. Shown on 0.1.87: runProgram({"false"}) ok=1.
- R2 LogLock waited 5 s and then rewrote the log without the lock.
  Now held() is asked: without the lock the log is only added to
  ("Finishes: <marker>" on the outcome; readers drop the started half
  it names). Appends survive a concurrent rewrite both ways (the
  appender re-adds if its file was renamed away; the rewriter copies
  across what reached the old file late, and re-reads if the log grew).
  Folder fsync after create and rename; a torn last record is closed
  off before the next is added. Tests: lock held by another process;
  8 writers, half never waiting for the lock; SIGKILL at random
  moments, 25 rounds over a 300-record log.
- R3 server backup/remove went on whatever `systemctl stop` did: a
  copy from a running server, a unit file removed from under one.
  Now the state after stop must be inactive or failed, else nothing is
  copied or removed and a server that ran is seen running again.
  After Ctrl-C the cleanup finishes and nexus then ends by the signal
  (a shell loop stops). SIGQUIT held too. tests/server-backup-check.sh:
  18 cases with stand-in systemctl/tar, root only, run by GitHub;
  0.1.87 fails 9 of them.
- R4 check-hardening.sh passed a file with no GNU_STACK segment and
  printed protections it had not looked for. Now: GNU_STACK must exist
  and be RW; FORTIFY, libstdc++ assertions, the CET mark and
  stack-clash probes are read from the program (--expect=...), and the
  header says what each proves ("at least one" for the per-function
  ones; CET is a mark, not enforcement). Self-test builds a program
  without each protection and requires a failure.
- R5 no-apply test: with strace it now sees every program started (by
  full path; unclassified programs fail) and every file written
  anywhere, failed attempts included; 75 commands incl. the read-only
  ones; ~110 stand-ins; the binary is searched for built-in tool
  paths. Without strace it runs the reduced check and its last line
  says what was not checked. Self-test: 12 ways of breaking the rule.
- R6 Portage numbers compared as digit strings of any length (0.1.87's
  cap made 10^30 == 10^30+1).
- R7 extra: fd-leak and zombie test over the failure paths; kill test
  for the log; five signal cases for backup.
- Not verified here, for David: tests.yml on GitHub (now with strace
  and a root step); the image build's hardening check on Fedora's g++
  with all eight protections expected; `ctest` on the Acer with and
  without strace; a real `sudo nexus server backup <name> --apply`
  with Ctrl-C on the Asus.
- Still open: power-loss durability is by design (fsync of file and
  folder), not tested; a nexus killed outright (SIGKILL) mid-backup
  leaves the server stopped and the record at "started"; plain
  records added twice in the rarest double race are not de-duplicated;
  everything listed as open under patch 113.

**Patch 115 (0.1.89): first run of the patch 114 tests on Pop!_OS.**
717 of 718 passed on the Acer; NoApplyChangesNothing (with strace)
failed on five commands. Reproduced here by installing rpm and kmod.
- `nexus doctor`, `language`, `source firefox` ran `rpm -q...` because
  the rpm program exists. Debian's rpm keeps its database in ~/.rpmdb
  and a plain query creates it: a preview wrote to the user's home.
  Real, on any Debian/Ubuntu with rpm installed. Now rpm is asked only
  where an rpm database file is present (rpmDatabasePresent():
  rpmdb.sqlite / Packages / Packages.db under /usr/lib/sysimage/rpm or
  /var/lib/rpm). Fedora is unaffected. Test:
  RpmDatabaseTest.ADatabaseIsOnlyThereWhenItsFileIs.
- `nexus gaming` (and `gaming split-lock`) run `modinfo ntsync`, which
  only reads; the tracer resolved /usr/sbin/modinfo to /usr/bin/kmod
  and did not know it. Test bug: multi-call programs (kmod, busybox,
  toybox) are now judged by the name they were started as, so modinfo
  reads and modprobe by its own path is still caught.
- One command (on the Acer: probably `source firefox`; not reproduced
  here) opened an unnamed O_TMPFILE file in /tmp. Such a file has no
  name and is gone on close; keeping it needs linkat, which is checked.
  In /tmp, /var/tmp, /dev/shm it is now noted in the test's summary
  (with the program that did it), not a failure; elsewhere still one.
- Findings now say which program did them ("[by modinfo]").
- New test NoApplyTraceRules pins these rules on a hand-written log
  (fails on the 0.1.88 tracer). 719 tests.

**Patch 116 (0.1.90): the image build stopped at the stack-clash check.**
On GitHub (Fedora's g++) both nexus and nexus-shop got "PROBLEM: ...
missing: stack clash probes (none found)"; every other protection was
read back fine. The 0.1.88 checker looked for one instruction spelling
(`orq $0x0,(%rsp)`); a program whose compiler probes another way, or
that has no frame over a page, failed although nothing was wrong.
- The checker now reads the property itself from every function: no
  constant step of the stack pointer downwards larger than one page
  (sub $N,%rsp or add $-N,%rsp). A frame taken in one jump fails and
  is named with its size; frames taken a page at a time pass; a
  program with no frame over a page passes and says so. Run-time
  sized frames are counted, not judged.
- Self-test: a program with no large frame, and a hand-written
  function that steps a page at a time with a plain store, must pass;
  the same with a two-page jump must fail. Both new passes fail on the
  0.1.89 checker. Works with mawk and gawk.
- Not known yet: what Fedora's compiler actually emits. The next
  image build prints it in the "verified in the file" line.

**Patch 117 (0.1.91): what is confirmed is what runs (round-4 audit).**
The Asus, 13 November: `nexus install vlc` said 11 packages (dnf: 12);
`rpm-ostree install --dry-run vlc` listed 193, because it recomputes
the whole layer (gcc, nmap, wireshark... layered earlier, already in
the booted deployment) and takes weak dependencies dnf's check leaves
out. 56 rich dependencies installed. transactions.log was 0644.
- M2: dnf's dry run is read whole (parseDnfTransaction): installs,
  upgrades, reinstalls, downgrades, removals (dependents and unused
  dependencies), and "replacing" rows as replacements, not as a
  package called "replacing". A plan that also removes, replaces or
  downgrades no longer "agrees"; those lines are always shown, need a
  typed "yes", and are refused under -y. 0.1.89's parser called such a
  plan Agrees (shown with the old library).
- M3: on the image, rpm-ostree is asked first, with the options the
  real layering uses (--dry-run --idempotent --allow-inactive). Its
  list, less what the running system already has (rpm -q), is the list
  shown and confirmed; packages it adds beyond dnf's list are listed
  by name; recomputed earlier layers are counted. If rpm-ostree cannot
  be asked or refuses, nothing is layered. The confirmation now lists
  every name (it stopped at 20).
- M1: `nexus remove` asks dnf (`remove --assumeno`) or apt
  (`remove --simulate`) what it would remove, in the preview too. The
  count typed against is the package manager's; extra removals are
  listed by name and refused under -y; a removal the tool will not
  confirm, or one that would also install, is not done. dnf declining
  to answer without root is "could not be asked", not a refusal.
- M4: `nexus setup` asks once for the set, then each item shows its
  full list and asks again (-y given to setup goes to each item).
- L: transactions.log is 0640, group wheel (sudo/admin on Debian)
  when root writes; an older 0644 log is fixed on its next write;
  permissions are only ever narrowed (a 0600 log stays 0600).
- Rules in plan_check (decideInstall, decideRemoval) with 13 new
  tests on dnf 4/5 tables, the Asus rpm-ostree output, apt, and both
  decisions; 1 log test. 731 tests.
- Not verified here: dnf 5's exact wording for removals, downgrades
  and replacements on Fedora 44 (shape known, wording from dnf's
  documentation and memory); `dnf remove --assumeno` as an ordinary
  user on Fedora; the image install flow end to end (needs the Asus).

**Patch 118:** test code only. FakeTools took a list of pairs; with one
brace-enclosed pair the Acer's compiler found it ambiguous with the
copy constructor. Stand-ins are now added with tools.add(name, script).

**Patch 119 (0.1.92): the Asus test of 0.1.91.**
`nexus install vlc` on the Asus printed no "Layered" line: the
rpm-ostree check (M3) never ran. isImageBased() asked
`findmnt -no OPTIONS /usr`, which prints nothing on a bootc/composefs
image (/usr is not a mount point of its own), and read that as
"writable". The install path's older block read the same empty answer
as "not writable" and layered anyway, so 0.1.91 would layer without
rpm-ostree's list having been shown.
- isImageBased(): /run/ostree-booted, else statvfs("/usr") read-only,
  with rpm-ostree present. One answer for the whole install command.
  Test: PlanApplyTest.AnOstreeSystemIsImageBasedWithoutAUsrMountPoint.
- `dnf remove --assumeno nmap` on the Asus confirmed dnf 5's table:
  "Removing:" heading, one-space rows, "Transaction Summary:"; the
  removal check read it right ("dnf would remove the same 1").
- `nexus remove` on Fedora no longer warns about
  /var/lib/apt/extended_states.
- `nexus remove --apply` on the image stops before the confirmation and
  names `rpm-ostree uninstall` (dnf cannot remove from the image; it
  used to fail after the person had typed "remove"). Real removal of
  layered packages through rpm-ostree is still to do.

**Patch 120 (test only):** NoApplyChangesNothing failed on the Acer for
`nexus setup media,development,vpn`: /etc/cups/subscriptions.conf
changed during the command. cupsd rewrites that file on its own; strace
showed no process of nexus's touching it. With strace, a watched file
that changed but that the trace reader never names (as a finding or as
an allowed write, paths resolved) was changed by another program, and
is listed at the end instead of failing. Without strace every change
still fails. Checked as root with a loop touching /etc during the whole
run (passes, every command lists the file), and the self-test still
catches all 12 rule-breakers.

Done in 89 (was queued): `nexus install` should fetch the package list itself
(not make the user run `dnf makecache`) and give a "did you mean"
hint for a mistyped package (java-lastest -> java-latest-openjdk);
plain `sudo dnf install` should point to `sudo nexus install`.

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
