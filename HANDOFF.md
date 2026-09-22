# Nexus-CORE — where things stand

Paste this at the start of a new session. Written for someone picking
the project up cold. Supersedes `docs/HANDOFF.md` (stale, 190 lines),
`NEXUS-STATE.md` (14 Sept) and `CAPABILITIES.md` (22 Sept).

**Repo:** `~/Documents/nexus-core` on the Pop!_OS laptop (Acer).
**Test machine:** Asus laptop, 3.6 GB RAM, QCA9377 wifi, Secure Boot
enforcing, TPM present, not encrypted.
**Test VM:** qemu on the Acer, 4 GB. Disk passphrase `testtest123`.
**Registry:** `ghcr.io/davidosdas-official/nexus-core` — account
`DAVIDOSDAS-official`, logged in with podman. **Nothing pushed yet.**

Last session: 21–22 September 2026. Four patches applied on top of
`86932f0`. **496 tests.**

---

## In one paragraph

A capability-based decision layer for Linux package management: you
ask for a *web browser*, not `firefox`; Nexus shows every way to get
one with what each costs, verifies the plan against the real package
manager, and hands the work over. **Nexus decides and explains; apt,
dnf and rpm-ostree do the work.** Six sources, five metadata formats,
twelve profiles. There is also a distribution built from it: a Fedora
bootc image whose package list is generated from a profile.

The tool is essentially done for 0.1. **What is left is mostly
verification on the VM, not code** — plus a licence, artwork, and the
website.

---

## The release bar

"Flawless" cannot be a ship criterion; nobody can prove it. This list
can. When every line passes, 0.1.0 ships.

| # | Test | Status |
|---|---|---|
| 0 | Base is a supported Fedora (44), builds end to end | **in progress** — base stage fails at os-release; patch 4 fixes |
| 1 | VM: clean install → encrypted boot → first boot → profiles → reboot | not run on 44 |
| 2 | VM: wrong passphrase ×3, then right one → boots normally | not run — `tries=0` is inference until a real boot |
| 3 | VM: first boot **offline** → honest message, no false "missing" | not run |
| 4 | Publish 0.1.0 → ISO from the ghcr name → install → `bootc status` shows ghcr → push 0.1.1 → `bootc upgrade` → new version appears | not run |
| 5 | `nexus --version` on the installed machine shows a real commit, not `unknown` or `-dirty` | not run |
| 6 | Asus: Secure Boot enforcing and wifi still work on 44 | not run |

Tests 1–5 on the VM; 6 on the Asus. Every one needs a machine, which
is why none of them can be done by the test suite.

---

## Done on 21–22 September

Four patches, all in the tree.

**Patch 1**
- The rpm branch says when it reads no package metadata, as the apt
  branch always did.
- First boot fetches package lists before asking, and checks for
  `repodata/repomd.xml` the same way `rpm_repo.cpp` does. If dnf
  reports success but leaves nothing readable, it says so.
- `rd.luks.options=tries=0` in `/usr/lib/bootc/kargs.d/`, so a
  mistyped passphrase is asked again instead of giving up after three.
- The `final` stage had **three** SELinux reinstalls, two of them after
  `bootc container lint` — so the lint never saw the shipped layer.
  Duplicated by a patch script that checked "done?" by looking for a
  line already present. Now: no reinstall (nothing installs there),
  lint last.
- Installed hostname is `nexus`, not `fedora`.
- `build-installer.sh` moves the previous `output/` aside and reclaims
  root's copy of the image afterwards (`CLEAN=no` keeps it). That was
  8–11 GB per build.
- `showcase.profile`'s "unresolvable requirement" was
  `gpu-vendor-amd | …`, copied from gaming where it is the condition
  for a `Prefers-When`. Nothing in showcase read it, and a GPU is not
  installable. Removed.
- **Parser bug.** `control_file.cpp` had no comment handling. Comments
  survived by accident — a line with no colon is dropped — but a
  comment *with* a colon is parsed as a field and ends the field above
  it, so a note inside a `Requires:` list silently drops everything
  after it. Nine comments already contain a colon; none currently sits
  above a continuation line. Fixed, with two tests that fail without
  the fix (checked by disabling it) and one guarding against the fix
  over-reaching.

**Patch 2**
- `publish.sh` defaults to `ghcr.io/davidosdas-official` and
  lowercases the registry, because image references must be lowercase
  and the account is not.
- It used to tell you to rebuild before building the ISO. Unnecessary:
  the tag already exists locally.
- It now says the package is **private until made public**, and prints
  the check that decides whether any of it worked:
  `sudo bootc status | grep -i image` on the installed machine.

**Patch 3**
- **Fedora 42 → 44.** 42 reached end of life on 27 May 2026. Declared
  once as `ARG FEDORA_VERSION=44` before the first `FROM`.
- `fedora-logos` → `generic-logos` (not built for 42 — another reason
  to move). Ends with `rpm -q`, failing the build if Fedora's logos
  survived. **Verified on 22 Sept:** the F44 bootc image does not ship
  fedora-logos at all; generic-logos installed cleanly.
- `build.sh` now passes `NEXUS_COMMIT`, appending `-dirty` for a
  modified tree. It never passed it, so every image it built reported
  `unknown`.
- `build-installer.sh` writes `output/nexus-build.txt`: image and
  builder refs **and digests**, nexus version, date. The first half of
  pinning bootc-image-builder.

**Patch 4**
- Fedora 43 removed `PLATFORM_ID` from os-release. Under `set -u` the
  os-release step died on 44. Now copied only if the base has one.
- The logos check ran only in the base stage — which proves nothing
  about the desktop's hundreds of packages. Checked again at the end
  of `desktop` and `final`, naming whatever required it.
- `generate-lists.sh` refuses an image whose Fedora differs from the
  Containerfile's. See the next section for why.

---

## The recurring failure, worth knowing

Nearly every bug has the same shape: **something reported success
about its own narrow view while the wider claim was false.**

This session added three:

- **`generate-lists.sh` reported "nothing changed" about a release it
  never read.** On 22 Sept the F44 base build failed; the script, run
  straight after, used the previous F42 image, loaded the same 82,210
  packages, and produced a clean diff. Fixed in patch 4.
- **The logos check looked at one stage and would have read as passed
  for the image.** Fixed in patch 4.
- **Four of five tests first written for the parser bug passed without
  the fix.** They were testing nothing. A test that cannot fail reads
  like coverage. Always disable the fix and watch the test fail.

From before: `history` said "no changes recorded"; `--apply` reported
success into a `usr-overlay`; `scan` reported `Loaded 0`; the version
string went stale; first boot reported `SUCCESS` after asking nobody.

Almost none were found by a failing test. They were found by computing
a number, finding it implausible, and comparing against a native tool
— or by putting it on hardware.

---

## What is left, in order

**Engineering**
1. Commit patches 1–4 **before** building anything that matters. An
   image built from an uncommitted tree with
   `NEXUS_COMMIT=$(git rev-parse --short HEAD)` claims to be a clean
   `86932f0` while containing none of it.
2. F44 base builds → `generate-lists.sh` → **read the diff** → desktop
   image → ISO.
3. Release-bar tests 1–6.
4. Pin bootc-image-builder by digest (the manifest now records it).
   The project was archived 18 June 2026 and merged into `image-builder
   --bootc-ref`; plan the migration.

**Before 0.1 ships**
- **`LICENSE` is 0 bytes.** Nobody can legally redistribute anything.
  The choice also decides what "the last release is paid" can mean:
  binaries can be sold, but GPL parts stay redistributable by anyone.
  What you control is your own code and the Nexus-CORE name and mark.
- Repo public, or at least the website's "read the source" link real.
- Website rewrite — after the release bar passes, so it describes what
  ships. Current page is wrong in several places (says the kernel is
  unsigned, no installer, ~480 tests, "the terminal above"; has a
  stray `</section>`, two footers, `<main>` outside `.container`).

**Polish**
- Artwork into the image (next section).
- The installer still shows Fedora's logo — it comes from anaconda
  branding inside bootc-image-builder, not from this image.
- Warn when someone creates a user called `nexus`.
- Verify no plaintext swap on a fresh install: `swapon --show`.
- Delete the 17 empty junk files at the root, the five empty
  directories (`gui/ installer/ recovery/ package/ tools/`), and the
  stale `docs/HANDOFF.md`.

**Known and not blocking 0.1**
- `runCommand` still holds a whole decompressed repository (1.3 GB of
  the 2.8 GB peak heap). Needs `XmlReader` to consume a stream.
- No conditional dependencies (rpm `(a if b)` is counted as a gap).
- Changes are recorded, not atomic across sources.

---

## Artwork

Eleven images supplied on 22 Sept. Decisions so far:

- **The mark is the white node-graph N** — four corner nodes, one
  centre node. It is already the SVG on the website, it *is* the idea
  (capabilities as nodes, what connects them as edges), and it survives
  16 px and one colour. The others (Tux-and-atom star, orbit N,
  wireframe N, low-poly N) are not the mark.
- **The brand colour is already orange.** `ANSI_COLOR` in os-release
  is `249;115;22` = `#f97316`, the website's accent. Blue-led images
  are off-brand against both.
- Proposed wallpapers per profile: gaming → orange carbon; security →
  green circuit; school → light "friendly core"; minimal/minimalism →
  flat grey with the small mark; showcase → silver carbon.

Problems to fix before any of it ships:
- **All wallpapers are 1376×768.** Blurry on 1080p, unusable on 4K.
  Need 3840×2160 sources.
- **The wallpapers draw a different N** from the chosen mark. Composite
  the real SVG onto plain backgrounds instead of an AI-drawn mark.
- **The security wallpaper has garbled AI text** ("SDC_KEPNEL.LDR",
  "INTEGRITIT", hex noise) — worst on the one profile whose pitch is
  honesty.

How it gets in: the profile is chosen at first boot, after the image
is built. So every profile's wallpaper ships in the image (they are
small), and first boot writes the default for the chosen one into
`/etc` (writable on bootc). The mark replaces generic-logos' files for
Plymouth, SDDM and "about this system".

---

## What is done and should not be re-litigated

- **Secure Boot works.** Fedora's kernel, Fedora's signature.
- **Wifi needs three things:** `network-manager`,
  `network-manager-wifi`, `wifi-supplicant`.
- **`base` is composed into every image**, and is the floor.
- **rpm-ostree layering works** once the empty `extra_varrun` SELinux
  module is removed at first boot. `--apply` reports `staged`.
- **Anaconda asks for a user**; no account in the ISO. Encryption on by
  default, never forced (Decision 15).
- **`ID` and `VERSION_ID` stay Fedora's** — tooling reads them.
- **Memory:** `scan --with-available` fits 2.3 GB.
- **systemd ordering was never the problem** for first boot.
- **The first-boot picker** discards keys pressed at the passphrase
  prompt and asks twice before accepting a skip.

---

## Useful commands

```bash
cd ~/Documents/nexus-core
cmake --build build && ctest --test-dir build       # 496
./build/cli/nexus --version
```

**Applying a patch.** Use `git apply`, never `patch`: `patch` asks
questions and reads its answers from whatever you pasted next.

```bash
git apply --check ~/Downloads/X.patch && git apply ~/Downloads/X.patch
```

Silence means it worked. Already applied → `--check` fails, nothing
happens.

**Paste one command at a time** when the first can fail. A failed
build followed by a pasted `generate-lists.sh` is how the F42 lists
passed for F44 lists.

**After changing the Fedora version:**

```bash
podman build --target base \
    --build-arg NEXUS_COMMIT="$(git rev-parse --short HEAD)" \
    -t localhost/nexus-os:base -f image/Containerfile .
# check it ended with a COMMIT line, not "Error:"
./image/generate-lists.sh      # refuses a stale image now
git diff image/generated/
```

**Build the desktop image and ISO:**

```bash
podman build --target desktop \
    --build-arg NEXUS_PROFILE=minimalism \
    --build-arg NEXUS_COMMIT="$(git rev-parse --short HEAD)" \
    -t localhost/nexus-os:minimalism -f image/Containerfile .

PROFILE=minimalism ./image/build-installer.sh   # wants 20 GB free
cat output/nexus-build.txt
```

**Publish, then build the ISO that can update:**

```bash
./image/publish.sh
# then make the package public on GitHub, and check logged out:
podman logout ghcr.io && podman pull ghcr.io/davidosdas-official/nexus-core:minimalism
IMAGE=ghcr.io/davidosdas-official/nexus-core:minimalism PROFILE=minimalism \
    ./image/build-installer.sh
```

**Test in a VM:**

```bash
rm -f /tmp/nexus-test.qcow2 && qemu-img create -f qcow2 /tmp/nexus-test.qcow2 30G
qemu-system-x86_64 -m 4096 -smp 4 -enable-kvm -bios /usr/share/ovmf/OVMF.fd \
    -drive file=/tmp/nexus-test.qcow2,format=qcow2 \
    -cdrom output/bootiso/install.iso -boot d
```

Drop `-cdrom` and `-boot d` to boot the installed disk. On the
installed machine — **not on the Acer**, which has no bootc:

```bash
sudo bootc status | grep -i image     # must say ghcr, not localhost
cat /proc/cmdline                     # must contain rd.luks.options=tries=0
nexus --version                       # a real commit
```

**Profile memory** and **check a small machine**: unchanged, see git
history of this file.

**Writing an ISO to a USB stick** — plug it in first, check `lsblk`;
`/dev/sdX` is a placeholder:

```bash
sudo umount /dev/sda1
sudo dd if=output/bootiso/install.iso of=/dev/sda bs=4M status=progress oflag=sync
sync
```

## Key documents

`README.md`, `docs/overview.md`, `docs/vision.md`,
`docs/specifications/00-decisions.md` (15 decisions),
`image/README.md` (now documents the Fedora-bump order and the logos
swap), `image/first-boot/README.md`.
