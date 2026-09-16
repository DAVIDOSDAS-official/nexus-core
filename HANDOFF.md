# Nexus-CORE — where things stand

Paste this at the start of a new session. It is written for someone
picking the project up cold.

**Repo:** `~/Documents/nexus-core` on the Pop!_OS laptop (Acer).
**Test machine:** an Asus laptop, 3.6 GB RAM, QCA9377 wifi, Secure Boot
enforcing, TPM present (`tpm0`). Nexus-CORE is installed on it.

Last session: 15 September 2026. Head was `e9019c2`.

---

## What it is

A capability-based decision layer for Linux package management.
You ask for a *web browser*, not for `firefox`; Nexus shows every way
to get one with what each costs, verifies the plan against the real
package manager, then hands the work over. **Nexus decides and
explains; apt, dnf and rpm-ostree do the work.**

There is also a distribution built from it: a Fedora bootc image whose
package list is generated from a profile.

**493 tests.** Six sources (apt, dnf, Flatpak, Snap, Nix, Portage),
five metadata formats. `core/` has barely changed in weeks — the model
has held.

---

## The three things blocking a 0.1 release

They are one piece of work. All three live in Anaconda and first boot.

### 1. Every install ships the same login

`image/config-installer.toml` creates `nexus`/`nexus`. Removing it was
supposed to make Anaconda prompt for a user — **it does not.** The
Users module is enabled and the screen never appears, so the machine
installs with no account at all and cannot be logged into. Verified by
installing it.

So the account is back, and a published ISO would carry a default
credential documented in a public repo. The Asus shell prompt reads
`[nexus@fedora nexus]$` — the blocker is visible in every screenshot.

Needs: working out why Anaconda skips its user screen under
bootc-image-builder (kickstart handling, probably). **This has to be
understood rather than routed around, because encryption is configured
in the same place.**

A first-boot alternative exists and is not built: `nexus-first-boot`
already runs as root before anyone logs in and already asks what the
machine should be. It could ask *who you are* at the same time. One
screen: what is this machine for, and whose is it.

### 2. Nothing is encrypted

`lsblk -f` on the Asus shows `ext4` on every partition. No
`crypto_LUKS` anywhere. Root, `/var` (which holds `/home` on bootc),
and `/boot` are all plaintext. A stolen laptop gives up everything.

`nexus doctor` reports this correctly and in plain words: *the root
filesystem is not encrypted; anyone with the disk can read it.* It is
classified `[warn]`, the same severity as an unused package, which is
probably too quiet for what it means.

**Decision 15** (`docs/specifications/00-decisions.md`) settles the
policy: encryption on by default, never forced, TPM-bound unlock where
the hardware allows so there is only one password. The Asus has a TPM,
so this is testable on real hardware.

Retrofitting encryption onto installed machines is not possible. This
has to land before anyone installs 0.1.

### 3. The hostname is still `fedora`

`NAME`, `PRETTY_NAME` and `VARIANT` say Nexus-CORE, but the machine
introduces itself as `fedora`. Small, and another place the system
cannot say what it is.

---

## The rpm memory problem — measured, and smaller than it looked

**This is a bug, not a blocker.** The previous version of this document
said the rpm reader was the problem and that the fix was streaming.
That was inferred from an OOM kill and is wrong. Everything below was
measured.

| | components | peak RSS | per component |
|---|---|---|---|
| `scan` (installed only, Asus) | 1,088 | not measured | — |
| `scan --with-available` apt (Acer) | 128,080 | 388 MB | ~3.0 KB |
| `scan --with-available` rpm (container) | 82,166 | 725–738 MB | ~8.8 KB |
| `scan --with-available` rpm (**Asus**) | 82,210 | **2,970 MB** | — |
| `install nmap` rpm (container) | 82,166 | 843 MB | — |

So rpm costs **~2.9× dpkg per component, not 8×**. The Asus has 2.26 GB
available, needs 2.97 GB, and was killed. With a 4 GB swapfile it
completes.

**Ruled out by measurement, do not re-investigate without new evidence:**

- *Streaming / whole-document parsing.* The heaptrack Sizes histogram
  has nothing above 1 KB. Nothing is slurped. 37.7M allocations,
  largest bucket 17–32 bytes.
- *The rpm reader being pathological.* It reads 82,166 packages in
  5 seconds; apt takes 20 seconds for 128,080.
- *The solver.* Adds 118 MB (725 → 843).
- *Installed-set size.* 211 → 634 → 242 packages installed changed
  `--with-available` peak by under 12 MB.
- *Capability count.* 242 packages with 76,731 capabilities cost the
  same as 634 packages with 64,383.

**What is left:** the Asus differs from every container run by holding
206,774 capabilities (160,104 of them file paths, 77%) where the
container held 50–77k. 2,970 MB against 737 MB is ~17 KB per extra
capability, which is absurd for what is fundamentally a string like
`/usr/lib64/libssl.so.3`. The next step is heaptrack in a container
with a large installed set, pointed at the rpm path, to name that
17 KB. Interning the file-path strings — they share prefixes heavily —
is the obvious first cut and would help apt too.

Worth deciding separately: whether every file needs to be a capability
up front. dnf keeps filelists in a separate index because most
resolutions never need them.

---

## What is done and should not be re-litigated

- **Secure Boot works.** The kernel is Fedora's and carries Fedora's
  signature; images boot with Secure Boot enforcing. Verified on
  hardware.
- **Wifi works from the image.** It needs *three* things, not one:
  `network-manager`, `network-manager-wifi`, `wifi-supplicant`. On
  Fedora the plugin is a virtual provide of `NetworkManager`; only rpm
  splits it out.
- **`base` is composed into every image.** It was not, for weeks. This
  was the root cause of the entire wifi saga.
- **`base` is the floor, not a peer.** Exclusivity (`minimal` cannot
  combine) ignores `base`.
- **rpm-ostree layering works.** `--apply` reports **`staged`**, not
  `applied`, because nothing is in effect until a reboot.
- **An empty SELinux module (`extra_varrun`) broke all of that.** It is
  removed at first boot, not during the build: overlayfs refuses to
  delete it in a container and a real filesystem does not.
- **Branding:** `NAME`, `PRETTY_NAME`, `VARIANT` say Nexus-CORE. `ID`,
  `VERSION_ID`, `PLATFORM_ID` stay Fedora's — tooling reads them, and
  bootc-image-builder composes a distro name from `ID`+`VERSION_ID`.
- **Transaction records are written before the work.**
- **Kernel protection works.** `nexus remove` on the running kernel
  refuses and says why. Verified.
- **The binary reports its own version.** `nexus 0.1.0 (e9019c2)`, and
  `-dirty` when built from a modified tree. Read at **build** time, not
  configure time — a configure-time lookup went stale the moment a
  source file changed, which is the same failure this string exists to
  catch. `.containerignore` keeps `.git` out of the build context, so
  the Containerfile takes the commit as `--build-arg NEXUS_COMMIT`.
  Unset, it reports `unknown` and doctor warns.

---

## Fixed on 15 September

- **`Loaded 0 available packages` was a use-after-move.**
  `cli/src/main.cpp` counted `available.components.size()` *after*
  moving the vector into `availableOnly`. Both the apt and rpm branches
  did it. The merge was always correct; only the number was wrong. A
  128,080-package load had been reporting zero.
- **Protection patterns were recompiled per package.** Ten patterns ×
  3,057 components = 30,552 `std::regex` constructions. Now cached.
  Worth ~2 seconds of 22.
- **`tests/unit/alias_test.cpp` was missing `<algorithm>`.** It built on
  Pop by luck and never on Fedora. `std::count` needs it.
- **Stanzas were copied into the parse result**, not moved.

---

## The recurring failure, worth knowing

Nearly every bug has the same shape: **something reported success about
its own narrow view while the wider claim was false.**

- `history` said "no changes recorded" — true of the file it read,
  false of the machine
- `--apply` reported success into a `usr-overlay` that vanishes at
  reboot
- `generate-lists.sh` said a list was incomplete and wrote it anyway
- the Containerfile checked for a *missing* list and never a *stale* one
- patch scripts reported "already done" by checking for a line that
  existed in the unchanged text
- **`scan --with-available` reported `Loaded 0` and exited 0** after
  successfully reading 48 index files
- **the version string reported a stale commit** because it was read at
  configure time and the binary was rebuilt without reconfiguring

Almost none were found by a failing test. They were found by computing
a number, finding it implausible, and comparing against a native
tool — or by putting it on hardware.

**Added to the list on 15 September:** a measurement that fails is not
evidence about the code. `scan --with-available` was killed on the Asus
and that was read as proof the rpm reader was heavy. It was proof the
machine was small. Get the number on a machine that completes before
concluding anything.

---

## Useful commands

```bash
cd ~/Documents/nexus-core
cmake --build build && ctest --test-dir build
./build/cli/nexus --version

./image/generate-lists.sh          # needs localhost/nexus-os:base rebuilt first
podman build --target desktop --build-arg NEXUS_PROFILE=minimalism \
    -t localhost/nexus-os:minimalism -f image/Containerfile .
PROFILE=minimalism ./image/build-installer.sh   # wants ~20 GB free
```

**Measuring the rpm path** (the Asus cannot complete without swap; a
container can):

```bash
podman run --rm -v ~/Documents/nexus-core:/src:ro -v /tmp/m.sh:/run.sh:ro \
    --memory=12g fedora:42 bash /run.sh
```

where `/run.sh` installs `gcc-c++ cmake make rpm-devel time zchunk`,
builds `--target nexus`, runs `dnf makecache`, then
`/usr/bin/time -v ./build/cli/nexus scan --with-available`.
**`zchunk` is required** — Fedora ships `.zck` repodata and without
`unzck` the reader skips both real repos and silently reads almost
nothing (it does warn).

**Writing the ISO** (plug the stick in *first*, check `lsblk`, and
`/dev/sdX` is a placeholder — writing to it literally creates a file):

```bash
sudo umount /dev/sda1
sudo dd if=output/bootiso/install.iso of=/dev/sda bs=4M \
    status=progress oflag=sync
sync
```

Four minutes at ~12 MB/s. Anything faster means it went somewhere
wrong. **Pull the stick before rebooting the build machine.**

---

## Also true

- Nothing is published, so any installed machine can never update.
  `nexus doctor` reports this. `image/publish.sh` has never been run.
- The repo is not public.
- The website exists as a single `index.html` on the Pop laptop. Not
  hosted. Needs the logo (a white node-graph N), a real screenshot,
  and a repo link.
- `showcase.profile` has one requirement that cannot be resolved.
- The Asus was **reinstalled on 15 September**. It now has a 4 GB
  swapfile at `/var/swapfile`, in fstab, surviving reboot — and that
  swapfile is **plaintext on an unencrypted disk**, which is a hole in
  exactly the property Decision 15 is about.
- `nexus-first-boot.service` has `Before=display-manager.service` but
  nothing about getty, and both it and `getty@tty1.service` are pulled
  in by `multi-user.target`. They race for `/dev/tty1`. Needs
  `Conflicts=getty@tty1.service` before account creation goes anywhere
  near first boot.
- `image/Containerfile` line 100 has `ARG NEXUS_VERSION=0.1` while
  CMake says `0.1.0`. Two places, two formats, drifting.

## Key documents

`README.md`, `docs/overview.md`, `docs/vision.md`,
`docs/specifications/00-decisions.md` (15 numbered decisions — the
reasoning not recoverable from the code), `image/README.md`,
`image/first-boot/README.md`.
