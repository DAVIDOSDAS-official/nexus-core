# Image signing

Every published image is signed with the Nexus key. Installed
machines refuse an update that is not signed by it.

What that protects against: anybody who can push to the registry but
cannot run this repository's workflow -- a leaked package token, a
compromised registry, a mirror -- cannot get an image installed.

What it does not protect against yet: the private key is a GitHub
Actions secret, because the weekly workflow signs. So whoever takes
over the maintainer's GitHub account can also run a workflow that signs
(an outside review, 0.1.84, rated this the most serious open problem).
What is in place: actions pinned to commits, and the
key on disk only in the step that signs. What happens if the key is
ever stolen is in SECURITY.md.

## The pieces

| File | Where it goes | What it does |
|---|---|---|
| `cosign.pub` | `/etc/pki/containers/nexus-core.pub` | The public key. Safe to publish; it can only check, not sign. |
| `nexus-core.yaml` | `/etc/containers/registries.d/` | Tells the machine the signatures sit next to the image in the registry. |
| (merged by the Containerfile) | `/etc/containers/policy.json` | Requires that key for everything under `ghcr.io/davidosdas-official`. Every other registry keeps Fedora's defaults. |

The machine's origin also has to say "signed": `ostree-image-signed:`
rather than `ostree-unverified-registry:`. New installs get that from the
installer (a `%post` in `config-installer.toml`). Machines installed
before signing existed switch once, by hand -- see HANDOFF.

## The private key

`~/.config/nexus-signing/cosign.key`, protected by a password. **Never
in the repository, never on a test machine, never uploaded.**

Losing it means no machine will accept another update until each one is
switched to a new key by hand. Keep a copy offline (a USB stick in a
drawer) and the password somewhere other than the stick.

## Signing

`image/publish.sh` signs after every push and verifies the signature
before saying it is done. It uses cosign 2.x: cosign 3 stores
signatures in a newer format that the tools on the machines do not read
yet.
