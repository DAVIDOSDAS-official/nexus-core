#!/usr/bin/env python3
"""Require the Nexus key for Nexus images; leave every other rule alone.

Run once in image/Containerfile. Merges into Fedora's
/etc/containers/policy.json rather than replacing it, so registries
this project knows nothing about keep the rules Fedora gave them.

The top-level default becomes "reject", with an explicit accept-anything
entry for every transport that did not already have a rule. The result
for everything except Nexus images is the same as Fedora's -- but the
machine's update tool (ostree's container support) refuses to verify
signatures at all while the top-level default is insecureAcceptAnything,
on the grounds that such a policy verifies nothing. So the permissive
rule moves from the top level to each transport, where it still applies
and no longer trips that check.
"""

import json

PATH = "/etc/containers/policy.json"
SCOPE = "ghcr.io/davidosdas-official"
KEY = "/etc/pki/containers/nexus-core.pub"

# Every transport containers/image knows. Any left out would go from
# "accept" to "reject" with the default change.
TRANSPORTS = [
    "docker", "atomic", "containers-storage", "dir", "docker-archive",
    "docker-daemon", "oci", "oci-archive", "sif", "tarball",
]

ACCEPT = [{"type": "insecureAcceptAnything"}]

with open(PATH) as f:
    policy = json.load(f)

old_default = policy.get("default")
print("Fedora's policy default:", old_default)

transports = policy.setdefault("transports", {})

if old_default == ACCEPT:
    for name in TRANSPORTS:
        transports.setdefault(name, {}).setdefault("", ACCEPT)
    policy["default"] = [{"type": "reject"}]
elif old_default != [{"type": "reject"}]:
    raise SystemExit(f"Unexpected policy default {old_default!r}; "
                     "look at /etc/containers/policy.json before merging.")

transports.setdefault("docker", {})[SCOPE] = [{
    "type": "sigstoreSigned",
    "keyPath": KEY,
    "signedIdentity": {"type": "matchRepository"},
}]

with open(PATH, "w") as f:
    json.dump(policy, f, indent=4)
    f.write("\n")

print(f"Signing: {SCOPE} requires the Nexus key")
