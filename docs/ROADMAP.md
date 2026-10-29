# Nexus-CORE: what gets built

Last updated: 28 Oct 2026. Current version: **0.1.58** (patches 1–84). 582 tests pass.

Keep a copy outside the repo (e.g. `~/Documents/nexus-notes/ROADMAP.md`) and one in the repo at `docs/ROADMAP.md`.

---

## 1. Rules that never change

- Nothing changes the system without `--apply`. Every change is recorded in `nexus history`.
- Nexus informs and warns, then lets the person decide. It never forces anything on (location, time zone and similar stay opt-in).
- Nexus-CORE stays automated and easy. Manual low-level work belongs to Bleedroot.
- Every profile does only its own job. No bloat: heavy tool sets (Kali and similar) go in opt-in containers.
- Linux next to Linux only. No Windows dual-boot claims and no Windows testing.
- Claude stays credited on commits (`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`).
- The cosign private key (`~/.config/nexus-signing/cosign.key`) is never pasted, shown or uploaded.
- Never show a time zone, location, IP or personal email in videos, posts or on the website.
- Build an ISO, copy it to `~/nexus-isos/`, and only then build the next one (each build clears `output/`).

---

## 2. Preview 4: done

| What | Version | Tested on the Asus |
|---|---|---|
| Fedora version check uses Bodhi (no false alarm) | 0.1.50 | yes (workflow) |
| First boot asks once; 1-minute reminder once per user | 0.1.51 | yes |
| Hand-typed `nexus setup --apply` disarms the reminder | 0.1.52 | yes |
| Add-on checks count Flatpaks; wallpaper set on any choice | 0.1.53 | yes |
| `nexus gaming [--apply]`: NTSync loaded at every start | 0.1.53–54 | yes |
| `nexus server`: Minecraft, website, logs, backup, remove, cockpit | 0.1.55 | partly (Cockpit apply) |
| Hosting add-on (Podman, Cockpit, firewall) | 0.1.55 | yes |
| firewalld on by default (public zone + KDE Connect, mDNS, Samba client) | 0.1.56 | yes ("Firewall: on") |
| Typo hints for `nexus server` options | 0.1.56 | yes |
| Firewall build step idempotent (weekly build #53 fix) | 0.1.57 | yes (build passed) |
| Installer "make room" next to another Linux (shrinks ext4, asks YES first) | 0.1.57–58 | **no** (0.1.58 fix untested) |
| Nexus next to Pop!_OS 24.04: systemd-boot chainload, `dualboot 1 --apply` | 0.1.57 | yes (Secure Boot off for Pop) |

## 3. Preview 4: still to do

**Tests (Asus):**
- [ ] Cockpit login in the browser (`https://localhost:9090`)
- [ ] `sudo nexus server add minecraft --accept-eula --apply`, then join from Minecraft on another device
- [ ] `sudo nexus server add website <folder> --apply`, then open it from the phone
- [ ] KDE Connect and printing with the firewall on

**Tests (VM):**
- [ ] Make-room question screen (0.1.58) with a nearly full ext4 disk

**Build:**
- [ ] `nexus specs` (see section 4)
- [ ] `nexus vpn check`: is a VPN active, DNS leak, kill switch on/off; Mullvad, Proton, Tailscale, own WireGuard
- [ ] Development add-on, small/medium/full, with an opt-in Kali toolbox container
- [ ] Showcase edition: "insane UI, like from another universe", desktop to terminal, graphical add-on picker
- [ ] HANDOFF.md: deliver the Pop pass note

**Website and docs:**
- [ ] Note that Pop!_OS needs Secure Boot off for the dual-boot restart
- [ ] Preview 4 release text, archive.org upload, GitHub release

**Promotion:**
- [ ] "Not vibe-coded" video: open about AI help; show tests, signing, real-hardware runs, the firewall found and fixed, the EULA not auto-accepted
- [ ] r/linux, Universal Blue forum, r/unixporn (Saturdays), DistroWatch later
- [ ] Reddit: no netlify.app links (banned site-wide); r/Fedora only in the weekly showcase thread

---

## 4. New ideas (28 Oct 2026), with the honest scope

### `nexus specs`: system info built in (next to build)
Replaces installing fastfetch/neofetch.
- The Nexus logo drawn in the terminal **in colour**, with a colour bar under it (all 16 terminal colours plus a true-colour gradient), so you can see the terminal shows them right.
- Host, model, Nexus version and edition, Fedora base, kernel, uptime, desktop, display and resolution, shell, CPU (model, cores, speed), GPU(s) and driver, RAM used/total, disks used/total, battery, Secure Boot, encryption, firewall, NTSync, installed add-ons, last update and image signature.
- Options: `--plain` (no colours, for scripts and bug reports), `--json`.
- Read straight from `/proc`, `/sys` and `os-release` in C++, so nothing extra is installed. Can be tested here.

### Nexus Guard: security, scoped honestly
"100× better than any Windows antivirus" can't be promised or proven, and making that claim would hurt the project's trust. What Nexus *can* do is something Windows antiviruses can't: check that the whole system is the signed image it should be.
- `nexus guard` (read-only): image signature verified, updates current, firewall on, Secure Boot, disk encrypted, open ports and what is listening, SSH settings, Flatpak apps with risky permissions, files in `/etc` changed from the image.
- `nexus guard scan <folder>`: ClamAV scan (opt-in add-on), e.g. Downloads.
- Optional: scan new files in Downloads automatically; warnings as desktop notifications.
- Graphical app later, in the same "organic" style as the other apps.

### Nexus Music / Media
- Yes: a library and player close to the Shop's look. Plays local music and video (MP4, MKV, FLAC, MP3…), imports folders and phones (with Nexus share), podcasts via RSS, Internet Archive and other free/Creative Commons sources, internet radio, and installs official apps (Spotify, etc.) as Flatpaks.
- No: downloading from Spotify or other DRM platforms. That breaks copy protection, is illegal in the EU and US, and gets the repo taken down.
- Maybe, opt-in: a downloader for non-DRM sites using yt-dlp (already in Fedora's own repos), with a clear note that the site's terms are the user's responsibility. Not built into the default image.

### Nexus AI: no "no limits" mode
A switch that removes the AI's safety limits won't be built. A disclaimer doesn't move responsibility off the project that ships it.
- Instead, **Expert mode**: no beginner explanations, shorter answers, and the AI can carry out system actions (install, configure servers, firewall, repairs) after showing the exact command and getting a yes, recorded in history like any `--apply`.
- Nexus doesn't lock anyone out of their own files. It's GPL and their PC, so what they change themselves is on them. Nexus just doesn't ship, document or automate a way to remove the limits.

### `nexus server publish`: a website everyone can see (29 Oct 2026)
Tested 29 Oct: `nexus server add website` works on the Asus itself. The phone was probably on mobile data or its VPN (not yet confirmed).
- Home hosting with `--domain` needs a domain, router port forwarding (80/443) and a real public IP (often missing: CGNAT). It also shows the home IP to every visitor, which is not acceptable.
- So: publish through a tunnel. The site goes out via a small server or service, the router stays untouched, and the home IP stays hidden. HTTPS is automatic.
- Choose the tunnel (own small VPS with WireGuard + Caddy, or a service), then build `nexus server publish <name> [--apply]` and `nexus server unpublish`.

### Calls on Nexus (baresip) (29 Oct 2026)
- baresip is in Fedora's own repos (`baresip`, `baresip-opus`, `baresip-pulse`, …), small, and scriptable from Python (the way Jarvis uses it).
- Possible: a "calls" add-on (baresip + Opus + audio), `nexus call` to make and answer internet (SIP) calls, and a base for Jarvis and the AI's voice.
- Needs a SIP account or a SIP app on the other side (e.g. Linphone), or another baresip. It doesn't call normal phone numbers without a paid provider.

### Already noted earlier
- Nexus share (built on LocalSend's protocol; app and terminal command)
- NVIDIA edition (separate image, MOK signing key, needs NVIDIA hardware to test)
- Multi-user support, Nexus-pet
- Bleedroot / Nexus-Nightmare (hard-mode edition)
- NEXUS-PACK ("Privacy, protection and control")
- Paid features later (about half a year out); everything free until then

---

## 5. Suggested order

1. Server tests on the Asus, then the make-room VM test
2. `nexus specs`
3. `nexus vpn check`
4. Video: "not vibe-coded"
5. Development + Kali container
6. Nexus Guard (CLI first)
7. Showcase edition
8. Preview 4 release
9. Then: Nexus share, Media, AI Expert mode, NVIDIA, multi-user
