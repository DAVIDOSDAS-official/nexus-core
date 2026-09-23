# The image

A bootable container. `bootc` treats the whole OS as an image: updates
replace it wholesale, and the previous image stays on disk so the
bootloader can return to it.

That is where rollback comes from, and it is why the write path is
easier here than on a mutable system. Nexus never has to mutate a live
machine; it emits a build artifact you can read and diff before
anything boots.

## Build

```
./image/build.sh
```

Four stages, built separately so a failure names the layer:

| Stage | Contains |
|---|---|
| `base` | Fedora bootc plus the `nexus` binary and profiles |
| `desktop` | KDE on Wayland, from `image/generated/minimalism.rpm.list` |
| `gaming` | 32-bit stack, Steam, Gamescope, GameMode, MangoHud, controllers |
| `final` | Labels, and `bootc container lint` |

## Publishing, and why it matters

```
./image/publish.sh
```

**This is the line between a snapshot and a distribution.**

A bootc system updates by pulling a newer image from the place it was
installed from. An image built locally and installed from a USB stick
has nowhere to pull from: no fix can ever reach that machine, however
urgent. Everything works, nothing reports a problem, and the machine
is frozen at the day it was installed.

So the ISO has to be built from a published name rather than from
`localhost/`:

```
./image/publish.sh
IMAGE=ghcr.io/davidosdas-official/nexus-core:minimalism \
    ./image/build-installer.sh
```

The reference is lowercase even though the account is not. GitHub
does not care; the image format does, and `publish.sh` lowercases it
rather than letting podman refuse after the build.

**A pushed package is private until it is made public.** A machine
running `bootc upgrade` has no GitHub account, so an update against a
private package fails on authentication and says nothing about why.
Change it under the package's settings, then prove it from a shell
that has logged out:

```
podman logout ghcr.io
podman pull ghcr.io/davidosdas-official/nexus-core:minimalism
```

**Check what the installed machine thinks it came from**, rather than
assuming the ISO carried the right name:

```
sudo bootc status | grep -i image
```

If that says `localhost`, the machine can never update, and nothing
else on it will ever mention that.

Then, on an installed machine:

```
sudo bootc upgrade
sudo reboot
sudo bootc rollback     # if the new one is worse
```

`nexus doctor` reports whether a machine can update at all, because a
machine that cannot is a fact about it worth knowing.

## Something you can install

```
./image/build-installer.sh                  # minimalism, an ISO
PROFILE=minimal ./image/build-installer.sh  # a lighter one
TYPE=qcow2 ./image/build-installer.sh       # a disk to boot in a VM
```

The installer is bootc-image-builder's, not one written here.
Partitioning a disk is the single most destructive thing a program
can do, it is already solved, and a second implementation would be a
second set of ways to erase somebody's data.

What this script adds is the things that go wrong around it: checking
there is disk space before a fifteen-minute build rather than after,
handing a rootless image to root's storage, and saying plainly that a
failed build leaves output that must not be written to a disk.

**The kernel is unsigned**, so booting the result needs Secure Boot
turned off. `nexus secureboot` explains why and what the alternative
is.

## One pipeline, several machines

Which profile an image is built from is a build argument:

```
podman build --target desktop --build-arg NEXUS_PROFILE=minimal \
    -t localhost/nexus-os:minimal -f image/Containerfile .
```

`minimal` produces a light system for hardware that cannot afford a
full desktop; `minimalism` produces the KDE one; `showcase` adds the
effects. Same Containerfile, same pipeline, different machines --
which is the claim the design rests on, and hardcoding one profile
would have left it untested.

Not every profile can produce an image. `server` and `security` add
things to a machine rather than describing one, so generating an image
from either alone gives a system with no way to log in.

## Two kinds of update, and why they cost different amounts

The Containerfile puts the packages below Nexus, so the two things an
image contains change independently:

| What changed | Build | What gets uploaded, and downloaded by every machine |
|---|---|---|
| Nexus: code, profiles, version | the usual `podman build ...` | a few small layers |
| Fedora: security and bug fixes | `podman pull quay.io/fedora/fedora-bootc:44` first, then the usual build plus `--build-arg REFRESH="$(date +%Y%m%d)"` | the whole package layer, gigabytes |

Measured on 23 September with a stub Fedora: changing only the version
number used to rebuild the 500-package desktop install; now it
rebuilds two layers and the package install stays cached.

**The cache is why the second row has to be deliberate.** A cached
package layer is exactly as old as the last time it was built. Without
`REFRESH`, every image keeps the packages from the last refresh, and
Fedora's fixes never reach anybody -- while every build reports
success. So refresh on a schedule, not when you remember: at least
every couple of weeks, and at once for anything serious.

(Not `--pull=newer`: that exists only from podman 4, and Pop!_OS 22.04
ships 3.4.)

The first push after this change is a full one, because the package
layer now sits on a different parent. Every one after it is not.

**Later:** a refresh still re-sends the whole package layer even when
three packages changed. `rpm-ostree compose build-chunked-oci`
("rechunking") splits an image into many layers by package, so a
machine downloads only what changed. Worth doing before there are
users on slow connections; not needed for 0.1.

## Moving to a new Fedora

The base is named once, as `FEDORA_VERSION` at the top of the
Containerfile. Changing it is a small edit with a wide blast radius,
so it has an order:

```
# 1. the base stage, which is what generate-lists.sh reads from
podman build --target base \
    --build-arg NEXUS_COMMIT="$(git rev-parse --short HEAD)" \
    -t localhost/nexus-os:base -f image/Containerfile .

# 2. regenerate the lists against the new release's repositories
./image/generate-lists.sh

# 3. read what it says before going on
git diff image/generated/
```

Step 3 is the point. Package names move between Fedora releases, and
the generated lists are the only place that shows it. A capability
whose package was renamed comes back as `# UNRESOLVED`, and
`generate-lists.sh` says which profiles are incomplete. Building from
an incomplete list produces an image missing whatever could not be
resolved -- and the image boots, so nothing announces it.

Only then the desktop stage and the ISO.

**A release ships on a Fedora that is still supported the day it
ships.** Fedora supports a release until four weeks after the release
two ahead of it, so there is always somewhere to move to and about a
year to do it. Fedora 42 was the base until September 2026 and went
end of life on 27 May 2026, which meant no security updates could
reach an installed machine however well the update path worked.

## Fedora's artwork is not shipped

The base stage swaps `fedora-logos` for `generic-logos`. Fedora's
trademark guidelines allow building on Fedora and saying so; they do
not allow shipping Fedora's marks on something called Nexus-CORE.
Both packages provide `system-logos`, so this is one package for
another rather than a gap.

The swap ends with `rpm -q`, which fails the build if Fedora's logos
survived it. `dnf` can report success on a transaction that resolved
differently than intended, and an image that quietly kept them would
look exactly like one that did not.

**Still unresolved:** the installer's own artwork comes from
`anaconda-*` branding inside bootc-image-builder, not from this
image, so the ISO still shows a Fedora logo while installing. And
`ID=fedora` stays in os-release because tooling reads it. Whether
those are acceptable for a paid release is a question for whoever
answers legal questions, not one the Containerfile can settle.

## The package list is generated

The desktop stage installs what the `minimalism` profile resolves to,
not a list somebody typed. Regenerate it inside the image, where the
Fedora repositories are:

```
./image/generate-lists.sh
```

The mounts matter. Without them the profiles baked into the image are
used instead of the ones in the checkout, so editing a profile and
regenerating produces the previous answer -- confidently, with no
indication anything is stale. The container supplies the repositories
and the binary; the working tree supplies what is being generated
from.

`makecache` output is discarded deliberately: `-q` still prints
"Metadata cache created." to stdout, and that line lands in the list
as a package named `Metadata`.

Committed rather than generated during the build, deliberately. Every
line names the capability it answers, so the file can be read and the
diff reviewed. A build nobody can read is a build nobody can audit.

## Before trusting any of it

**The package names in the Containerfile are unverified.** They were
written from memory, not checked against Fedora's repositories, and
that is exactly the failure mode this project keeps running into.
Check them before believing the build:

```
podman run --rm quay.io/fedora/fedora-bootc:42 \
    dnf --setopt=install_weak_deps=False list steam gamescope mangohud
```

Expect at least one wrong name. `steam` in particular is in RPM Fusion
rather than Fedora proper, so it will only resolve after that repository
is enabled.

## Known build failures

**`rootfiles`.** It ships `/root/.bash_logout`, and in a bootc image
`/root` is managed by ostree rather than being a normal directory. RPM
cannot unpack into it and aborts the whole transaction, so a single
unwanted file rolls back a thousand successful installs. Every `dnf
install` here passes `--exclude=rootfiles`.

**Weak dependencies.** `@kde-desktop-environment` pulled 1,519 packages
with its Recommends enabled. `--setopt=install_weak_deps=False` keeps
the image to what it actually needs.

**Decompressors.** Repository metadata is compressed, and Fedora uses
zstd for small repositories and zchunk for large ones. Without `zstd`
and `unzck` in the image, `nexus --with-available` reads whichever
repositories happen to be small and silently reports the rest as
unavailable -- which looks like a resolver problem and is not one.

## Things that will bite

**Secure Boot.** A custom image ships an unsigned kernel. Machines with
Secure Boot enabled will refuse to boot it, and beginners do not know
how to turn it off. This kills more custom distributions than any
technical failing. `nexus hardware` reports the state, which is a start,
not a solution.

**NVIDIA.** The driver must be baked into the image, because DKMS
rebuilding kernel modules against a read-only `/usr` is precisely what
immutability prevents. Bazzite ships separate NVIDIA images for this
reason, and so will this eventually.

**Anti-cheat.** EAC and BattlEye either work or they do not, depending
on Proton and kernel versions, and it is not something the image can
fix. Say so in the profile description rather than letting people
discover it.

**Size.** Gaming images with drivers and a 32-bit stack are large, and
an image-based system keeps the previous one too. Budget double.

## Not done

- No first-boot profile picker. The plan is a small greeter that runs
  `nexus profile check` and shows what the machine can do.
- No signing, so no Secure Boot.
- No `nexus` involvement in the build. The image is assembled by `dnf`;
  Nexus only observes it. Closing that gap needs an RPM source, which is
  Track A's next big piece.
