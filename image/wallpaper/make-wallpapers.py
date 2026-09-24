#!/usr/bin/env python3
"""Turn image/artwork/wallpapers/*.png into Plasma wallpaper packages.

    make-wallpapers.py SOURCE_DIR OUTPUT_DIR

Each <profile>.png becomes OUTPUT_DIR/nexus-<profile>/ -- the layout
Plasma's wallpaper picker reads from /usr/share/wallpapers:

    nexus-gaming/
        metadata.json
        contents/images/3840x2160.jpg

JPEG rather than PNG: the twelve PNGs are 42 MB and every machine
downloads them; as JPEG at quality 92 with full colour resolution they
are about 9 MB and look the same. The originals stay in the repository.

Run inside the artwork stage of image/Containerfile. It refuses rather
than guesses: a picture that cannot be read, is too small to fill a
screen, or a missing base.png (the fallback for every profile without
its own) stops the build.
"""

import json
import pathlib
import sys

from PIL import Image

MINIMUM_WIDTH = 1920
QUALITY = 92


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2

    source = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2])

    pictures = sorted(source.glob("*.png"))

    if not pictures:
        print(f"No .png files in {source}.", file=sys.stderr)
        return 1

    names = [picture.stem for picture in pictures]

    # Profile names are lowercase, and a file called VPN.png is a
    # wallpaper for a profile that does not exist. It happened once.
    for name in names:
        if name != name.lower():
            print(f"{name}.png: profile names are lowercase; "
                  f"rename it to {name.lower()}.png.", file=sys.stderr)
            return 1

    if "base" not in names:
        print("No base.png. It is the wallpaper for every profile "
              "without one of its own, so it is required.",
              file=sys.stderr)
        return 1

    for picture in pictures:
        name = picture.stem

        try:
            image = Image.open(picture)
            image.load()
        except Exception as error:  # noqa: BLE001 -- any failure stops it
            print(f"{picture.name}: cannot be read ({error}).",
                  file=sys.stderr)
            return 1

        width, height = image.size

        if width < MINIMUM_WIDTH:
            print(f"{picture.name} is {width}x{height}; it needs to be "
                  f"at least {MINIMUM_WIDTH} wide to fill a screen.",
                  file=sys.stderr)
            return 1

        package = output / f"nexus-{name}"
        images = package / "contents" / "images"
        images.mkdir(parents=True, exist_ok=True)

        target = images / f"{width}x{height}.jpg"
        image.convert("RGB").save(
            target, "JPEG", quality=QUALITY, subsampling=0, optimize=True)

        title = "Nexus-CORE" if name == "base" else \
            f"Nexus-CORE {name.capitalize()}"

        metadata = {
            "KPlugin": {
                "Id": f"nexus-{name}",
                "Name": title,
                "Authors": [{"Name": "DAVIDOSDAS"}],
                "License": "Nexus-CORE trademark policy (TRADEMARKS.md)",
            }
        }

        (package / "metadata.json").write_text(
            json.dumps(metadata, indent=4) + "\n")

        before = picture.stat().st_size // 1024
        after = target.stat().st_size // 1024
        print(f"  {name:12} {width}x{height}  {before:6} KB -> {after:5} KB")

    return 0


if __name__ == "__main__":
    sys.exit(main())
