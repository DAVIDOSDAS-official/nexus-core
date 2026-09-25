#!/usr/bin/env python3
"""Require the Nexus key for Nexus images; leave every other rule alone.

Run once in image/Containerfile. Merges into Fedora's
/etc/containers/policy.json rather than replacing it, so registries
this project knows nothing about keep the rules Fedora gave them.
"""

import json

PATH = "/etc/containers/policy.json"
SCOPE = "ghcr.io/davidosdas-official"
KEY = "/etc/pki/containers/nexus-core.pub"

with open(PATH) as f:
    policy = json.load(f)

print("Fedora's policy default:", policy.get("default"))

policy.setdefault("transports", {}).setdefault("docker", {})[SCOPE] = [{
    "type": "sigstoreSigned",
    "keyPath": KEY,
    "signedIdentity": {"type": "matchRepository"},
}]

with open(PATH, "w") as f:
    json.dump(policy, f, indent=4)
    f.write("\n")

print(f"Signing: {SCOPE} requires the Nexus key")
