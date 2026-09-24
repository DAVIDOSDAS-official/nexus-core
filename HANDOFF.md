# Nexus-CORE — where things stand

Paste this at the start of a new session. Written for someone picking
the project up cold. Supersedes every earlier HANDOFF, `NEXUS-STATE.md`
and `CAPABILITIES.md`.

**Machines**
- **Acer** (Pop!_OS 22.04, podman 3.4) — builds. Repo at
  `~/Documents/nexus-core`.
- **Asus** — the test machine. 3.6 GB RAM, QCA9377 wifi, Secure Boot
  enforcing. Upgraded to Nexus **0.1.3** from GHCR (confirm),
  encrypted (LUKS), with aircrack-ng, gamemode, gobuster, john, nmap
  and radare2 layered (plus whatever `nexus setup gaming` added on 24
  Sept — see Steam below).
- **No NVIDIA hardware anywhere.** Matters for the NVIDIA plan.
- **VM** — qemu on the Acer, for anything that does not need hardware.

**Registry:** `ghcr.io/davidosdas-official/nexus-core-testing:minimalism`
— **public**. The real name, `nexus-core`, has never been pushed; its
first push is the actual release.

**Last session:** 21–24 September 2026. Head `5581cea` (0.1.3,
patches 1–16). Patch 17 (0.1.4, splash) built, not published; patch 18
(0.1.5, Steam) delivered. **509 tests** (506 pass, 3 skipped).

---

## In one paragraph

A capability-based decision layer for Linux package management: you
ask for a *web browser*, not `firefox`; Nexus shows every way to get
one with what each costs, verifies the plan against the real package
manager, and hands the work over. **Nexus decides and explains; apt,
dnf and rpm-ostree do the work.** There is also a distribution built
from it: a Fedora 44 bootc image whose package list is generated from a
profile, installed from an ISO, updated from GHCR.

---

## The release bar — all passed

| # | Test | Result |
|---|---|---|
| 0 | Supported base, builds end to end | Fedora 44. Built 22 Sept |
| 1 | Install → encrypted boot → first boot → profiles → reboot | VM and Asus |
| 2 | Wrong passphrase ×3, then right → boots | VM: asked a 4th time |
| 3 | First boot offline → honest message | VM and Asus (Asus found a bug; fixed) |
| 4 | Publish → install from GHCR ISO → upgrade | Asus: 0.1.0 → 0.1.1 → 0.1.2, layered packages kept |
| 5 | Real version on the installed machine | Asus: `nexus 0.1.2 (98603ab)` |
| 6 | Secure Boot and wifi on hardware | Asus: `SecureBoot enabled`, picker ran over wifi |

Also verified on the Asus, 24 Sept: first boot waits for Enter before
the login screen (patch 8), run by hand with the network off.

That bar was "does it work end to end". The bar for the first public
release is below, under **Open**.

---

## What changed 21–24 September

**Base and image**
- Fedora 42 (end of life 27 May 2026) → **44**, named once as
  `ARG FEDORA_VERSION`.
- `fedora-logos` → `generic-logos`, checked with `rpm -q` after the base,
  after the desktop, and at the end. Fedora 44 bootc ships no
  fedora-logos at all.
- `PLATFORM_ID` copied only if present (Fedora 43 removed it).
- `rd.luks.options=tries=0` in kargs.d — **verified**: a 4th prompt.
- **Layer order: packages below Nexus.** A Nexus change used to rebuild
  and re-upload the whole desktop. Now it rebuilds a few small layers —
  verified in the 0.1.2 build: every `desktop-packages` step "Using
  cache". The cost: packages refresh only on purpose:
  ```bash
  podman pull quay.io/fedora/fedora-bootc:44
  podman build ... --build-arg REFRESH="$(date +%Y%m%d)"
  ```
  Do that every couple of weeks. It is a big push; it is also the only
  way Fedora's security fixes reach anyone.
- **One version.** `project(VERSION ...)` in `CMakeLists.txt`. The image
  asks the binary (`image/identity.sh`); `publish.sh` asks the image.
  `--build-arg NEXUS_VERSION` / `VERSION=` are only checks and stop the
  build if they disagree.

**Installer and first boot**
- Hostname `nexus`. Keymap line made explicit — **did not fix** the empty
  `vconsole.keymap=` (see open items).
- First boot fetches package lists before asking; says so honestly when
  it cannot; **waits for Enter** before the login screen takes the
  console.

**Nexus itself**
- rpm branch says when it reads no metadata.
- Control-file parser skips `#` comments. A comment containing a colon
  used to be parsed as a field and could swallow the rest of a
  `Requires:` list.
- `showcase.profile` no longer requires a GPU vendor (it was copied
  from gaming, where it is a condition, not a requirement).
- `doctor` Encryption: judges a composefs `/` by `/sysroot`. It said
  "not encrypted" on a LUKS machine. Swap files judged by the
  filesystem they live on.
- `doctor` Updates: reads status without root (rpm-ostree), and names
  **`rpm-ostree upgrade`** on machines with layered packages —
  `bootc upgrade` refuses those.
- `nexus guide`: rpm-ostree on image-based systems, dnf elsewhere.
- rpm reads check themselves: exit status, rpm's own errors, and a
  second count. On a mismatch, doctor warns and marks Dependencies
  **not judged** instead of reporting false "missing".

**Tooling**
- `build-installer.sh`: moves old `output/` aside, reclaims disk,
  writes `output/nexus-build.txt` (image and builder digests).
- `build.sh` passes the commit, `-dirty` when modified.
- `generate-lists.sh` refuses an image whose Fedora differs from the
  Containerfile's.
- `publish.sh`: defaults to the account, lowercases it, reads the
  version from the image, **checks the login before pushing**.

---

**24 September, evening (patches 14–17)**
- **RPM Fusion** (free + nonfree) enabled in the `os` stage — the
  release packages only, nothing from it installed by default. For
  Steam, codecs, and later NVIDIA.
- **Licence:** GPL-3.0 in `LICENSE` (copied from Pop!_OS's
  `/usr/share/common-licenses/GPL-3` — gnu.org reset the connection).
  **`TRADEMARKS.md`:** the name and the node-graph N are not GPL; a
  changed version must be renamed; unmodified copies may be shared.
- **Wallpapers:** `image/artwork/wallpapers/<profile>.png`, 12 files,
  3840×2160 (`base.png` = `minimal.png` on purpose). An `artwork` stage
  converts them to JPEG (42 MB → 9 MB) as Plasma packages
  `/usr/share/wallpapers/nexus-<profile>/`. At login,
  `/usr/libexec/nexus/apply-wallpaper` (autostart) applies: the first
  add-on chosen at first boot (`/etc/nexus/wallpaper`) → else the
  image's profile (`VARIANT_ID`) → else base. Once per choice; a
  wallpaper the user picks stays.
- **First boot offers 8, not 12:** basic, development, gaming,
  minimalism, school, security, showcase, vpn. minimal, tiling and
  server are left out of the desktop image's profile directory (they
  are different machines, not add-ons); `base` is hidden from
  `nexus setup` (it is in every image anyway).
- **Patch 17 (0.1.4, not built yet):** `sddm-breeze` + login theme
  (the build said "No SDDM theme" — the login screen was SDDM's bare
  fallback), Plymouth `bgrt` splash with a graphical passphrase box,
  initramfs rebuilt with `dracut --no-hostonly --add "ostree plymouth"`
  and checked for plymouthd, systemd-cryptsetup and
  ostree-prepare-root before it is kept; `rhgb quiet` via kargs.d.
  **This is the riskiest change so far** — it replaces the initramfs.
  If a machine fails to boot, pick the previous entry in the boot menu.

## Things learned the hard way

- **podman logins vanish at shutdown** (kept in `/run`). Log in again
  after every reboot, **as yourself, no sudo**:
  `podman login ghcr.io -u DAVIDOSDAS-official`. Every 403 so far was
  this — misdiagnosed twice, once as the token, once as the registry.
- **Don't delete `~/.local/share/containers/cache/blob-info-cache-v1.*`.**
  It is how podman knows what GHCR already has. Deleting it turned a
  small push into a full re-upload.
- **Push with the VPN off** and heavy apps closed. A dropped tunnel
  killed a 2 GB upload mid-layer; running everything at once froze the
  Acer (REISUB).
- **`git apply`, never `patch`.** `patch` asks questions and reads the
  answers from whatever you pasted next.
- **One command at a time when the first can fail.** A failed build
  followed by pasted commands produced "Fedora 44" lists from Fedora 42.
- **The package is separate from the repo.** Package public, repo
  private is fine: the image carries the binary, not the source.
- **A VM is always online.** The offline first-boot bug only showed on
  hardware. Use the VM to iterate, hardware before release.
- **`bootc upgrade` refuses machines with layered packages.** Use
  `sudo rpm-ostree upgrade`.
- **`/etc/nexus` does not exist until first boot writes to it.** Writing
  a file there by hand needs `sudo mkdir -p /etc/nexus` first (my
  instruction on 24 Sept forgot it).

The recurring bug shape, still: **something reported success about
its own narrow view while the wider claim was false.** This week:
doctor's encryption check, its update advice, `generate-lists.sh`
reading a stale image, the logos check that looked at one stage, three
places holding one version number, a first-boot message nobody could
see, and tests that could not fail. Always disable a fix and watch its
test fail.

---

## Open, in order

**Waiting on the Asus (24 Sept)**
- **Steam did not appear** after `sudo nexus setup gaming --apply`
  ("1 of 1 installed" — that was mesa-vulkan-drivers). Cause found:
  Steam in RPM Fusion is i686-only, and Nexus applied Debian's rule
  (an unqualified name means the requester's own architecture), so a
  64-bit profile could not reach it. RPM names carry no architecture.
  **Patch 18** marks every rpm package Multi-Arch foreign (any build
  satisfies a name; native still preferred; sonames still pick the
  right libraries). 4 tests, each seen failing without the fix.
- **Wallpaper:** confirmed working — `wallpaper-applied` said
  `minimalism`. `/etc/nexus/wallpaper` now says `gaming` on the Asus;
  confirm the switch after logging out and in. (Also created by
  accident on the Acer: `sudo rm -r /etc/nexus` there.)
- **Patch 17:** build, publish, upgrade, reboot; expect the splash and
  a passphrase box. Check `cat /proc/cmdline` for `rhgb quiet` — if
  `rpm-ostree upgrade` does not apply new kargs.d, add them once with
  `sudo rpm-ostree kargs --append=rhgb --append=quiet`.

**The bar for the first public release** (one stable, impressive
release first; paid comes later)
1. **Every offered profile focused and tested.** Each strictly about its
   job while still able to do normal things. Written out for review
   before code. `basic` currently adds nothing on KDE — make it the
   media add-on (codecs from RPM Fusion, hardware video decoding, VLC).
2. **Artwork:** wallpapers ✓ (patch 16); login theme and boot splash
   (patch 17); the Nexus mark in the splash and on the login screen.
3. **Steam working** from the gaming add-on.
4. **NVIDIA** — no hardware to test on. First release: ship Fedora's
   open driver (nouveau/NVK) and have `nexus doctor` say so honestly.
   Alongside: find one tester with an NVIDIA card. The real fix, later:
   build and sign the driver at image build (ublue-os/akmods approach),
   a Nexus MOK key enrolled once, an NVIDIA image variant that first
   boot offers to switch to.
5. **Image signing** (cosign) — before the public release, because
   adding it later means switching every installed machine by hand.
6. **`nexus update`** preview (what an update changes) and automatic
   rollback when an update fails to boot (greenboot).
7. Keymap (`vconsole.keymap=` empty), zram.
8. **Publish under the real name** `nexus-core`, make it public, ISO
   from `ghcr.io/davidosdas-official/nexus-core:minimalism`.
9. **Website** rewritten to match what ships.

**Decided, 24 Sept**
- **One ISO (KDE) for now.** minimal, tiling and server return later as
  their own images; server = no desktop, managed from Cockpit, services
  as Podman containers (clean removal), firewall/SSH/fail2ban, updates
  with rollback, disk health, restic backups, no hacking tools.
- **RPM Fusion over Flatpak** for Steam and codecs.
- **Licence:** GPL-3.0 code + trademark policy. Under GPL, sharing
  unmodified copies cannot be forbidden; the paid part will have its
  own closed licence. Search the name before registering it ("Nexus"
  is crowded: WIPO Global Brand Database). AI-written code has weak
  copyright; the trademark is the stronger protection.

**Paid version (after the free release)**
- Model: self-hosted, local-first, $1 once per account. Lemon Squeezy
  (pays out to North Macedonia) issues and checks licence keys, so no
  server of our own. At $1 a key check is an honesty box.
- Keep everything that protects the user free: rollback, snapshots,
  recovery, basic backup. Fedora gives rollback free; so do Bazzite,
  Aurora, Silverblue.
- Best Pro candidate: the **System Contract** — declare what the
  machine should be, Nexus reports drift. Builds on profiles. One Pro
  feature for the first paid release, not thirteen.
- Source documents: "Nexus-upgrade" (feature list, S/A/B/C) and
  "Nexus-paid-inside" (Free vs Pro). Several of their S items already
  exist: atomic updates + rollback (rpm-ostree), signed boot chain
  (Fedora shim/kernel), installer disk plan (Anaconda).

**Later**
- Installer artwork is Fedora's (from bootc-image-builder, archived 18
  June 2026: pin its digest, plan `image-builder --bootc-ref`).
- Backup, firmware status (fwupd), network doctor, export/apply,
  security and privacy reports, app centre.
- `runCommand` holds a whole decompressed repository in memory; needs
  a streaming XML reader. Rechunking for smaller Fedora refreshes.
- Clean up: 17 empty junk files at the root, five empty directories,
  stale `docs/HANDOFF.md`.

---

## Commands

```bash
cd ~/Documents/nexus-core
cmake --build build && ctest --test-dir build        # 509 (3 skipped)
./build/cli/nexus --version
```

**Release a Nexus change** (small push):
```bash
# bump project(VERSION ...) in CMakeLists.txt, commit, then:
podman build --target desktop --build-arg NEXUS_PROFILE=minimalism \
    --build-arg NEXUS_COMMIT="$(git rev-parse --short HEAD)" \
    -t localhost/nexus-os:minimalism -f image/Containerfile . 2>&1 | tee build.log
podman login ghcr.io -u DAVIDOSDAS-official          # after any reboot
NAME=nexus-core-testing ./image/publish.sh
```

**Refresh Fedora** (big push, every couple of weeks): same, after
`podman pull quay.io/fedora/fedora-bootc:44`, with
`--build-arg REFRESH="$(date +%Y%m%d)"`.

**After changing the Fedora version:** build `--target base`, run
`./image/generate-lists.sh`, read `git diff image/generated/`.

**ISO:**
```bash
IMAGE=ghcr.io/davidosdas-official/nexus-core-testing:minimalism \
    PROFILE=minimalism ./image/build-installer.sh
cat output/nexus-build.txt
```

**On an installed machine:**
```bash
sudo rpm-ostree upgrade && systemctl reboot
grep PRETTY /etc/os-release; nexus --version; nexus doctor
sudo bootc status | grep -i image      # must say ghcr, not localhost
cat /proc/cmdline                      # rd.luks.options=tries=0
```

**VM:**
```bash
rm -f /tmp/nexus-test.qcow2 && qemu-img create -f qcow2 /tmp/nexus-test.qcow2 30G
qemu-system-x86_64 -m 4096 -smp 4 -enable-kvm -bios /usr/share/ovmf/OVMF.fd \
    -drive file=/tmp/nexus-test.qcow2,format=qcow2 \
    -cdrom output/bootiso/install.iso -boot d
```

**Build check lines** (after a build, before publishing):
```bash
grep -E "KB ->|Login|Splash|No SDDM|No Plasma|Identity" build.log
```

**Applying a patch:**
```bash
git apply --check ~/Downloads/X.patch && git apply ~/Downloads/X.patch
```
Same name on both sides. Silence means it worked; "does not apply"
on a second run means it was already in.

## Key documents

`README.md`, `docs/overview.md`, `docs/vision.md`,
`docs/specifications/00-decisions.md` (15 decisions),
`image/README.md` (layer order, refresh rule, Fedora bump order, logos,
update commands), `image/first-boot/README.md`.
