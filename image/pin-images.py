#!/usr/bin/env python3
"""Resolve every image in image/pins/images.list to the digest its tag
points at today, and write image/pins/pinned-images for the image build.

Run by .github/workflows/weekly-image.yml before `podman build`; the
Containerfile copies the result into the signed image, where `nexus`
reads it (system/src/pins.cpp). A local build without it pulls by tag,
as before.

HEAD requests only: Docker Hub does not count a HEAD of a manifest
against its pull limit, and GitHub's runners share addresses that hit
it. The digest is the registry's Docker-Content-Digest -- the manifest
list for multi-arch images, which is what `podman pull ref@digest`
asks for.

An image that cannot be resolved is left out and named in the output;
`nexus` then uses its tag. The step does not fail the build over it:
a Docker Hub hiccup must not hold back a week of Fedora fixes.

    python3 image/pin-images.py <fedora-version> [list] [output]
"""

import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

ACCEPT = ", ".join([
    "application/vnd.oci.image.index.v1+json",
    "application/vnd.docker.distribution.manifest.list.v2+json",
    "application/vnd.oci.image.manifest.v1+json",
    "application/vnd.docker.distribution.manifest.v2+json",
])
DIGEST = re.compile(r"^sha256:[0-9a-f]{64}$")


def split_ref(ref):
    """docker.io/library/caddy:2 -> (registry host, repository, tag)."""
    host, _, rest = ref.partition("/")
    repo, _, tag = rest.rpartition(":")
    if not repo or not tag or "/" in tag:
        raise ValueError("not registry/repository:tag: " + ref)
    if host == "docker.io":
        host = "registry-1.docker.io"
    return host, repo, tag


def parse_challenge(header):
    """Bearer realm="...",service="...",scope="..." -> dict."""
    if not header.lower().startswith("bearer "):
        return {}
    return dict(re.findall(r'(\w+)="([^"]*)"', header[7:]))


def head(url, token=None):
    request = urllib.request.Request(url, method="HEAD")
    request.add_header("Accept", ACCEPT)
    if token:
        request.add_header("Authorization", "Bearer " + token)
    return urllib.request.urlopen(request, timeout=30)


def resolve(ref):
    host, repo, tag = split_ref(ref)
    url = "https://%s/v2/%s/manifests/%s" % (host, repo, tag)
    try:
        answer = head(url)
    except urllib.error.HTTPError as error:
        if error.code != 401:
            raise
        challenge = parse_challenge(error.headers.get("WWW-Authenticate", ""))
        if "realm" not in challenge:
            raise
        query = {k: v for k, v in challenge.items() if k in ("service", "scope")}
        query.setdefault("scope", "repository:%s:pull" % repo)
        with urllib.request.urlopen(
                challenge["realm"] + "?" + urllib.parse.urlencode(query),
                timeout=30) as reply:
            body = json.load(reply)
        answer = head(url, body.get("token") or body.get("access_token"))
    digest = answer.headers.get("Docker-Content-Digest", "")
    if not DIGEST.match(digest):
        raise ValueError("no usable digest in the answer: %r" % digest)
    return digest


def read_list(path, fedora):
    refs = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                refs.append(line.replace("@FEDORA@", fedora))
    return refs


def main(argv):
    if len(argv) < 2 or not argv[1].isdigit():
        print(__doc__.strip().splitlines()[-1].strip(), file=sys.stderr)
        return 2
    fedora = argv[1]
    source = argv[2] if len(argv) > 2 else "image/pins/images.list"
    target = argv[3] if len(argv) > 3 else "image/pins/pinned-images"
    lines, missed = [], []
    for ref in read_list(source, fedora):
        for attempt in range(3):
            try:
                digest = resolve(ref)
                lines.append(ref + " " + digest)
                print("pinned   ", ref, digest)
                break
            except Exception as problem:  # noqa: BLE001 -- reported below
                last = problem
        else:
            missed.append(ref)
            print("NOT PINNED", ref, "--", last)
    with open(target, "w") as f:
        f.write("# tag digest -- written by image/pin-images.py\n")
        for line in lines:
            f.write(line + "\n")
    print("%d pinned, %d by tag" % (len(lines), len(missed)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
