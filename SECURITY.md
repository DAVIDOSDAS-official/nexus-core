# Security

Nexus-CORE is a preview made by one person. Security problems are
taken seriously and fixed first.

## Reporting a problem

**Please do not open a public issue for a security problem.** Report
it privately instead:

1. Go to the repository's **Security** tab.
2. Click **Report a vulnerability**.
3. Describe what you found, how to reproduce it, and which version
   (`nexus --version`) and edition (KDE or Minimal) it affects.

You will get an answer, normally within a few days. Once a fix is
out, you are credited in the release notes unless you would rather
not be.

## What counts

Anything that lets someone do more than they should, for example:

- an update or image that is accepted without a valid Nexus-CORE
  signature;
- `nexus` changing the system without `--apply`, or doing more than
  its "What Nexus will do" list says (the services below are the only
  things that change it on their own);
- a way for a normal user to gain root through Nexus, the Shop, or
  the services Nexus adds;
- the installer or `nexus dualboot` changing another system's
  partitions or boot files;
- secrets (keys, passwords) written where other users can read them.

Problems in Fedora's own packages belong to Fedora; Nexus-CORE picks
up their fixes in the weekly image.

## What changes without `--apply`, by design

A few services in the image act on their own, at boot, without
anyone typing a command:

- **first boot** (`nexus-first-boot`): after the questions on the
  console, installs the add-ons chosen there and writes the keyboard
  layout for each person (as that person, not as root);
- **boot check** (`nexus-boot-check`): if a new version does not reach
  the login screen, the machine goes back to the previous one by
  itself;
- **boot entry** (`nexus-boot-entry`): on the first start, removes
  stale "Fedora" entries from the firmware's boot list -- only ones
  whose partition no longer holds that loader;
- **boot menu** (`nexus-boot-menu`): sets how long the boot menu
  waits;
- **update check** (daily, as the user): only looks; installing an
  update always needs `nexus update --apply` or the Shop's button.

## Supported versions

Only the latest image is supported. Run `nexus update` to get it:
a new signed image is built every week.

## How images are protected

Every image is signed with cosign when the weekly workflow builds it.
Installed machines are set to refuse images from
`ghcr.io/davidosdas-official` that do not carry that signature
(`image/signing/`). The kernel and boot loader are Fedora's, with
Fedora's signatures, so Secure Boot stays on.

## If the signing key is stolen

Installed machines accept a Nexus update only if it is signed with the
key in `/etc/pki/containers/nexus-core.pub`. Whoever has the private key
can sign an update every machine would accept, so a stolen key is the
worst case. Signatures are not written to a public log yet
(`--tlog-upload=false` in `image/publish.sh`), so a misused key would
not show up anywhere by itself.

What happens, in order:

1. **Stop the weekly workflow** (Actions → Weekly image → Disable) and
   delete the `COSIGN_PRIVATE_KEY` and `COSIGN_PASSWORD` secrets.
2. **Say so here and on the website**, with the time window in which
   the key may have been misused, and tell people **not to update**
   until a fixed image is out (`nexus update` without `--apply` only
   looks).
3. **Make a new key pair** (`cosign generate-key-pair`), put the new
   public key in `image/signing/cosign.pub`, and build one image that
   carries the new public key **and still verifies with the old one**:
   machines can only take an update their current key accepts, so the
   last old-key image is the one that hands over the new key.
4. Every image after that is signed with the new key only.
5. A machine that may have installed an update signed during the bad
   window should be reinstalled; `rpm-ostree status` shows the digest
   of every deployment, to compare with the list published in step 2.

Planned, to make step 2 checkable by anyone: signatures in the public
Rekor log, and machines that require them there.
