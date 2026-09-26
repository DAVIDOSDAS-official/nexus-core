# Nexus-CORE — where things stand

Paste this at the start of a new session. Written for someone picking
the project up cold. Supersedes every earlier HANDOFF, `NEXUS-STATE.md`
and `CAPABILITIES.md`.

**Machines**
- **Acer** (Pop!_OS 22.04, podman 3.4) — builds. Repo at
  `~/Documents/nexus-core`.
- **Asus** — the test machine. 3.6 GB RAM, Intel UHD 600, QCA9377
  wifi, Secure Boot enforcing. Nexus **0.1.6** from GHCR,
  encrypted (LUKS), with aircrack-ng, gamemode, gobuster, john, nmap
  and radare2 layered, plus mesa-vulkan-drivers and steam (25 Sept);
  `rhgb quiet` added by hand.
- **No NVIDIA hardware anywhere.** Matters for the NVIDIA plan.
- **VM** — qemu on the Acer, for anything that does not need hardware.

**Registry:** `ghcr.io/davidosdas-official/nexus-core-testing:minimalism`
— **public**. The real name, `nexus-core`, has never been pushed; its
first push is the actual release.

**Last session:** 21–25 September 2026. **0.1.10** (patches 1–25)
published, signed, and running on the Asus with signed updates enforced. **509 tests** (506 pass, 3 skipped).

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
- **RPM Fusion** (free + nonfree) enabled in the `os` stage, for Steam,
  codecs, and later NVIDIA. **Correction (25 Sept):** I said nothing
  from it would be installed by default. False — 0.1.3–0.1.7 shipped
  `fdk-aac` (rpmfusion-nonfree) and `openh264`/`mozilla-openh264`
  (Cisco's repo) inside the image. Patch 22 fixes it.
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
- **Steam, part 1 — found:** i686-only in RPM Fusion, unreachable under
  Debian's architecture rule. Fixed in patch 18 (0.1.5): Nexus now plans
  `[install] steam`.
- **Steam, part 2 — patch 19 (0.1.6):** the apply then refused because
  dnf's list differed from Nexus's. Now a plan that *differs* proceeds
  (only one name is handed over; the package manager resolves it) as
  long as the manager's list contains the requested package; *refused*
  and *could not be asked* still stop. When asked to confirm, the
  person sees the manager's list. Tested with a fake apt: differs →
  applies; requested missing → refuses; answer n → nothing changed.
- **Steam, part 3 — works.** First launch took 10–15 minutes (Steam
  downloads and unpacks itself); that was not a failure. Its log showed
  three gaps in the image, fixed in patch 20 and verified on the Asus
  (`locale`, `lspci`, `pactl info` all clean): `glibc-langpack-en` (the
  base had no en_US.UTF-8 at all), `pciutils`, `pulseaudio-utils`.
  Other languages still missing — needs the installer's language
  choice (later: install the chosen langpack, or all-langpacks).
- **Time zone, language, keyboard — patch 21 (0.1.7), not yet built.**
  The installer kickstart forced `lang en_US`, a US keyboard and
  `timezone UTC`: every clock was hours off (Asus 13:15 at 15:15 in
  Skopje). Now the installer asks all three (Localization and Timezone
  Anaconda modules enabled). `glibc-all-langpacks` replaces the English
  one, since the installer can't add packages. **`nexus-timezone`**:
  set by hand, look up once, or `sudo nexus-timezone auto on|off` —
  **off by default** (David: nothing forced on). Auto uses a
  NetworkManager dispatcher hook and Fedora's GeoIP service; tested with
  fakes (bad names, hostile answer, offline, on/off, hook off/on).
  **Needs a VM install from a new ISO** to confirm Anaconda shows the
  three screens. Unknown: whether Anaconda's own IP-based preselect runs
  (can't be switched off from our config — bootc-image-builder has no
  ISO kernel-argument option). If the VM preselects your real zone, it
  ran. Asus fixed by hand: `sudo timedatectl set-timezone Europe/Skopje`.
- First boot should warn that Steam's first start takes minutes.

- **Splash: works** on the Asus (password box confirmed, 25 Sept), but
  only after adding kargs by hand: **`rpm-ostree upgrade` does not
  apply new kargs.d files** — only fresh installs get them. Upgraded
  machines need `sudo rpm-ostree kargs --append=rhgb --append=quiet`.
  Worth a first-boot or update-time check later (also affects
  `rd.luks.options=tries=0` on machines installed before it existed).
- **Wallpaper: works** — minimalism by default, gaming after choosing
  it (Asus, 25 Sept). Remove the stray `/etc/nexus` on the Acer.

- **Patch 22 (0.1.8) — redistribution.** Image builds now disable
  `rpmfusion-*` and `fedora-cisco-openh264`, and the build fails if any
  RPM Fusion package (other than the two repo-release packages) or
  openh264 is in the image. Why: Cisco's patent licence covers openh264
  only when each user downloads it from Cisco (Fedora ships the
  `noopenh264` stub for that reason); RPM Fusion nonfree builds are
  meant to be fetched by the user, not redistributed inside an image.
  Machines keep the repos enabled, so users still get Steam/codecs by
  downloading them. Package layer rebuilds: **big push**.
- **Acer disk:** ISO build stopped at 11 GB free (needs ~20).

- **Patch 23 (0.1.9) — signed updates.** cosign 2.x key pair; private
  key `~/.config/nexus-signing/cosign.key` (password, offline backup,
  never in git — `*.key` ignored), public key committed as
  `image/signing/cosign.pub`. Image carries the key at
  `/etc/pki/containers/nexus-core.pub`, `registries.d/nexus-core.yaml`
  (sigstore attachments) and a policy.json *merged* to require the key
  for `ghcr.io/davidosdas-official` only. `publish.sh` refuses without
  cosign 2.x and the key, pushes by digest, signs
  (`--tlog-upload=false`, using podman's login), then verifies with the
  public key. New installs: kickstart `%post` runs `bootc switch
  --mutate-in-place --enforce-container-sigpolicy`, image name filled in
  by `build-installer.sh` (left out for localhost images). Tested with
  fakes: signs+verifies; refuses cosign 3; sign failure says "PUSHED BUT
  NOT SIGNED". **0.1.9 signed and verified for real, 25 Sept**
  (`sha256:0e9a1c9b…`).
- **Patch 24 (0.1.10):** policy default becomes `reject`, with an
  explicit accept-anything rule per transport — same effect for every
  other registry, but ostree's container code refuses signed pulls while
  the *top-level* default is `insecureAcceptAnything`. Existing machines
  therefore switch in two steps: `rpm-ostree upgrade` + reboot (gets the
  new policy into /etc), *then* `rpm-ostree rebase ostree-image-signed:…`
  + reboot.
- **Signed updates verified end to end (Asus, 25 Sept):** 0.1.10 signed
  (`sha256:4ce8e0c1…`), Asus switched in two steps and shows
  `ostree-image-signed:docker://…`. An unsigned image with a different
  digest (`:unsigned-test`) was **refused**: "A signature was required,
  but no signature exists." Delete the `unsigned-test` version on GitHub.
- **Patch 25:** `publish.sh` retries signing 3×; `SIGN_ONLY=sha256:…
  NAME=… ./image/publish.sh` signs without pushing (used once, when a
  slow connection timed out). Cosmetic: sign-only still prints
  "Pushed…" and the after-push text. Existing machines switch
  once: `sudo rpm-ostree rebase
  ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core-testing:minimalism`.
  Negative test: push an image with a different digest and no
  signature as `:unsigned-test`; rebasing to it must fail.
- **nexus-timezone works** (Asus, 25 Sept). Paris showed the same clock
  as Skopje because both are UTC+2 in summer; `now` then replaced the
  manual Paris with the looked-up zone, as designed.
- **Decided 25 Sept:** release as a public preview without other
  testers (feedback on the website instead), NVIDIA on the open driver,
  paid version not before ~6 months — all free until then.

- **VM install from the 0.1.10 ISO (26 Sept) — passed:** installer
  asked language, keyboard and time zone (preselected New York: no IP
  lookup happened); installed machine came up `ostree-image-signed`
  straight from the installer; `VC Keymap: us` (the empty keymap bug
  is gone); first boot layered showcase+security+vpn and **waited for
  Enter before the login screen on a real first boot**; wallpaper was
  showcase's (first add-on chosen). Not tested: a non-English choice.
- **Installer still showed Fedora's logo** (sidebar). Title already
  said "NEXUS-CORE 44" (from os-release). **Patch 26 (0.1.11):** the
  artwork stage draws sidebar/topbar pixmaps
  (`image/installer-branding.py`, node-graph N in orange + name) and
  packs them as a gzip cpio `product.img`, carried in the image at
  `/usr/share/nexus/installer/product.img`; `build-installer.sh` adds it
  to the ISO as `images/product.img` (merging any existing one), with
  `xorriso -boot_image any replay`, same volume label, `implantisomd5`,
  and writes `install.iso.sha256`. Tested on a small bootable test ISO:
  merge, no-merge, El Torito kept, label kept, media check PASS. Needs
  host tools: `sudo apt install xorriso cpio isomd5sum`. Also: "Handing
  the plan to rpm-ostree" on image-based systems (said dnf).
- Minor: after a fresh install the deployment digest is the ISO's
  embedded copy, so the first `rpm-ostree upgrade` may re-download the
  layers even with no new version.

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
