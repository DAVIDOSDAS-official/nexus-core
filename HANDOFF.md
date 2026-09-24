# Nexus-CORE — where things stand

Paste this at the start of a new session. Written for someone picking
the project up cold. Supersedes every earlier HANDOFF, `NEXUS-STATE.md`
and `CAPABILITIES.md`.

**Machines**
- **Acer** (Pop!_OS 22.04, podman 3.4) — builds. Repo at
  `~/Documents/nexus-core`.
- **Asus** — the test machine. 3.6 GB RAM, QCA9377 wifi, Secure Boot
  enforcing. Running Nexus **0.1.2** from GHCR, encrypted (LUKS), with
  aircrack-ng, gamemode, gobuster, john, nmap and radare2 layered.
- **VM** — qemu on the Acer, for anything that does not need hardware.

**Registry:** `ghcr.io/davidosdas-official/nexus-core-testing:minimalism`
— **public**. The real name, `nexus-core`, has never been pushed; its
first push is the actual release.

**Last session:** 21–24 September 2026. Head `98603ab` plus patches 12
and 13 (check `git status`; commit them if they are not). **505 tests.**

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

**Not yet re-verified on hardware:** first boot now waits for Enter
before the login screen (patch 8). It only runs once per install, so
test it without reinstalling, network off:

```bash
nmcli networking off
sudo NEXUS_SETUP_MARKER=/tmp/t /usr/bin/nexus-first-boot
nmcli networking on
```

It should stop at "Press Enter to continue to the login screen."

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

The recurring bug shape, still: **something reported success about
its own narrow view while the wider claim was false.** This week:
doctor's encryption check, its update advice, `generate-lists.sh`
reading a stale image, the logos check that looked at one stage, three
places holding one version number, a first-boot message nobody could
see, and tests that could not fail. Always disable a fix and watch its
test fail.

---

## Open, in order

**Before the first public release (0.1.0 under `nexus-core`)**
1. **`LICENSE` is 0 bytes.** Nobody can legally redistribute anything.
   The choice also decides what "the last release is paid" can mean:
   GPL parts of the system stay freely redistributable; what you
   control is your own code and the Nexus-CORE name and mark.
2. **Steam.** The gaming profile installs only `gamemode` on the
   shipped image: `steam` is in RPM Fusion, which only the unshipped
   gaming stage enables. Decide: RPM Fusion by default (a third-party
   repo on every machine) or Steam as a Flatpak (Nexus already reads
   Flatpak).
3. **Website** — rewrite after the above, so it describes what ships.
   Current page is wrong in places (unsigned kernel, no installer, test
   count, "the terminal above", a stray `</section>`, two footers).
4. **Publish under the real name:** `./image/publish.sh` (no `NAME=`),
   make `nexus-core` public, build the ISO from
   `ghcr.io/davidosdas-official/nexus-core:minimalism`.

**Before a paid release**
- **Image signing.** Machines show `ostree-unverified-registry:` — they
  check where an update came from, not who built it. cosign/sigstore
  plus a policy on the machine.
- **Installer artwork** is Fedora's (comes from bootc-image-builder,
  not this image). `ID=fedora` stays for tooling — a legal question,
  not a technical one.
- **bootc-image-builder** is archived (18 June 2026): pin its digest
  (recorded in `nexus-build.txt`), plan `image-builder --bootc-ref`.

**Artwork**
- **Logo: the white node-graph N** (four corner nodes, one centre) —
  already the SVG on the website; survives 16 px.
- Brand colour is orange (`ANSI_COLOR` 249;115;22 = `#f97316`).
- Wallpapers per profile are 3840×2160 now. Two fixes left: they draw a
  different (zigzag) N — composite the real mark instead; the security
  one still has garbled text ("VERIFIEB").
- Choosing a profile changes nothing you can see. Per-profile
  wallpapers, set by first boot in `/etc`, would fix that. The mark
  also replaces generic-logos' files (boot splash, login).

**Smaller**
- Empty `vconsole.keymap=` on installed machines; the kickstart change
  did not fix it. Harmless for US layouts.
- No zram on the image — add `zram-generator-defaults` for small
  machines.
- Once, on the Asus, doctor read 1054 of 1103 packages. Never
  reproduced; doctor now says so itself if it happens.
- `runCommand` holds a whole decompressed repository in memory (1.3 GB
  of the 2.8 GB peak). Needs a streaming XML reader.
- Rechunking (`rpm-ostree compose build-chunked-oci`) so a Fedora
  refresh downloads only changed packages.
- Clean up: 17 empty junk files at the root, five empty directories,
  stale `docs/HANDOFF.md`.

---

## Commands

```bash
cd ~/Documents/nexus-core
cmake --build build && ctest --test-dir build        # 505
./build/cli/nexus --version
```

**Release a Nexus change** (small push):
```bash
# bump project(VERSION ...) in CMakeLists.txt, commit, then:
podman build --target desktop --build-arg NEXUS_PROFILE=minimalism \
    --build-arg NEXUS_COMMIT="$(git rev-parse --short HEAD)" \
    -t localhost/nexus-os:minimalism -f image/Containerfile .
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
