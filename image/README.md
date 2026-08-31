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

## The package list is generated

The desktop stage installs what the `minimalism` profile resolves to,
not a list somebody typed. Regenerate it inside the image, where the
Fedora repositories are:

```
podman run --rm \
    -v ./components/profiles:/usr/share/nexus/profiles:ro \
    -v ./components/aliases:/usr/share/nexus/aliases:ro \
    localhost/nexus-os:base bash -c \
    'dnf makecache -q > /dev/null 2>&1
     nexus image minimalism --with-available' \
    > image/generated/minimalism.rpm.list
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
