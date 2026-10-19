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
  its "What Nexus will do" list says;
- a way for a normal user to gain root through Nexus, the Shop, or
  the services Nexus adds;
- the installer or `nexus dualboot` changing another system's
  partitions or boot files;
- secrets (keys, passwords) written where other users can read them.

Problems in Fedora's own packages belong to Fedora; Nexus-CORE picks
up their fixes in the weekly image.

## Supported versions

Only the latest image is supported. Run `nexus update` to get it:
a new signed image is built every week.

## How images are protected

Every image is signed with cosign when the weekly workflow builds it.
Installed machines are set to refuse images from
`ghcr.io/davidosdas-official` that do not carry that signature
(`image/signing/`). The kernel and boot loader are Fedora's, with
Fedora's signatures, so Secure Boot stays on.
