# Nexus-CORE — where things stand

Paste this at the start of a new session. It is written for someone
picking the project up cold.

**Repo:** `~/Documents/nexus-core` on the Pop!_OS laptop (Acer).
**Test machine:** an Asus laptop, 3.6 GB RAM, no swap, QCA9377 wifi,
Secure Boot enforcing. Nexus-CORE is installed on it.

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

## The two things blocking a 0.1 release

### 1. Every install ships the same login

`image/config-installer.toml` creates `nexus`/`nexus`. Removing it was
supposed to make Anaconda prompt for a user — **it does not.** The
Users module is enabled and the screen never appears, so the machine
installs with no account at all and cannot be logged into. Verified
by installing it.

So the account is back, and a published ISO would carry a default
credential documented in a public repo.

Needs: working out why Anaconda skips its user screen under
bootc-image-builder (kickstart handling, probably).

**A better idea came up and is not built yet:** `nexus-first-boot`
already runs as root before anyone logs in, and already asks what the
machine should be. It could ask *who you are* at the same time —
create the account, delete `nexus`. One screen: what is this machine
for, and whose is it.

### 2. The rpm path runs out of memory

On the Asus, `nexus install nmap` and `nexus scan --with-available`
are both **killed** (exit 137). 82,000 packages, 2.4 GB available, no
swap. It allocates fast enough that a one-second poll of `/proc`
never gets a reading.

Measured on Debian for comparison:

| | peak RSS |
|---|---|
| `scan` (installed only, ~3,000 components) | 18 MB |
| `scan --with-available` (128,000 components) | 388 MB |
| `install tree` | 541 MB |

So dpkg costs ~2.9 KB per component and works; **rpm costs enough
that 82,000 exceeds 2.4 GB.** Roughly 8× per package. The rpm reader
is the problem, not the solver, not `Component`, not `mergeAvailable`.

Already tried, with no measurable effect: moving instead of copying
the available set, `reserve()` on the merge, `unordered_map` instead
of `map`. Those are committed and worth keeping, but they were not
it.

**Likely cause:** repodata is compressed XML and the reader probably
decompresses and parses the whole document in memory. The fix is
streaming.

**Why it matters:** old machines with little RAM are the best story
this project has, and it does not run on them.

---

## What is done and should not be re-litigated

- **Secure Boot works.** The kernel is Fedora's and carries Fedora's
  signature; images boot with Secure Boot enforcing. Verified on
  hardware. (An earlier claim that it was unsigned was wrong and is
  corrected everywhere.)
- **Wifi works from the image.** It needs *three* things, not one:
  `network-manager`, `network-manager-wifi`, `wifi-supplicant`. All
  three are in `base.profile`. On Fedora the plugin is a virtual
  provide of `NetworkManager`; only rpm splits it out.
- **`base` is composed into every image.** It was not, for weeks —
  `nexus image minimalism` resolved only `minimalism`. Nothing noticed
  because Fedora's base image already provided init, libc and
  coreutils, right up until a profile asked for something it did not.
  **This was the root cause of the entire wifi saga.**
- **`base` is the floor, not a peer.** Exclusivity (`minimal` cannot
  combine) ignores `base`, or `base,minimal` is refused and the image
  comes out with no init.
- **rpm-ostree layering works.** `--apply` on an image-based system
  layers into a new deployment and reports **`staged`**, not
  `applied`, because nothing is in effect until a reboot. Verified:
  `nmap` survived.
- **An empty SELinux module (`extra_varrun`) broke all of that.**
  `semodule` could not read past it — not to rebuild policy, not even
  to list modules — so every new deployment failed to finalize. It is
  removed at first boot, not during the build: **overlayfs refuses to
  delete it in a container** ("Input/output error") and a real
  filesystem does not.
- **Branding:** `NAME`, `PRETTY_NAME`, `VARIANT` say Nexus-CORE.
  `ID`, `VERSION_ID`, `PLATFORM_ID` stay Fedora's — tooling reads
  them, and bootc-image-builder composes a distro name from
  `ID`+`VERSION_ID`. Setting `VERSION_ID=0.1` made it look for
  "fedora-0.1" and the ISO build failed.
- **Transaction records are written before the work**, so an
  interrupted change is visible rather than absent. `history` merges
  the root and user logs — changes are made under sudo and read back
  without it.

---

## The recurring failure, worth knowing

Nearly every bug in the last week has the same shape: **something
reported success about its own narrow view while the wider claim was
false.**

- `history` said "no changes recorded" — true of the file it read,
  false of the machine
- `--apply` reported success into a `usr-overlay` that vanishes at
  reboot
- `generate-lists.sh` said a list was incomplete and wrote it anyway
- the Containerfile checked for a *missing* list and never for a
  *stale* one — it used five-day-old lists silently
- patch scripts reported "already done" by checking for a line that
  existed in the unchanged text

Almost none of these were found by a failing test. They were found by
computing a number, finding it implausible, and comparing against a
native tool — or by putting it on hardware.

---

## Useful commands

```bash
cd ~/Documents/nexus-core
cmake --build build && ctest --test-dir build

./image/generate-lists.sh          # needs localhost/nexus-os:base rebuilt first
podman build --target desktop --build-arg NEXUS_PROFILE=minimalism \
    -t localhost/nexus-os:minimalism -f image/Containerfile .
PROFILE=minimalism ./image/build-installer.sh   # wants ~20 GB free
```

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
  `nexus doctor` reports this rather than letting it look healthy.
  `image/publish.sh` exists and has never been run.
- The website exists as a single `index.html` on the Pop laptop.
  Not hosted. Needs the logo (a white node-graph N), a real
  screenshot, and a repo link.
- The repo is not public.
- `showcase.profile` has one requirement that cannot be resolved.
- The Asus currently has a hand-made 4 GB swapfile at `/var/swapfile`
  and `nmap` layered.

## Key documents

`README.md`, `docs/overview.md`, `docs/vision.md`,
`docs/specifications/00-decisions.md` (14 numbered decisions — the
reasoning not recoverable from the code), `image/README.md`.
