#!/usr/bin/env python3
"""Draw the installer's branding and pack it as an Anaconda product.img.

    make-installer-branding.py OUTPUT_DIR

Writes OUTPUT_DIR/product.img: a gzip-compressed cpio archive that the
installer lays over its own files at boot (images/product.img on the
ISO). It carries Nexus replacements for the three pictures Anaconda
takes from fedora-logos:

    usr/share/anaconda/pixmaps/sidebar-bg.png    the left column
    usr/share/anaconda/pixmaps/sidebar-logo.png  the logo at its top
    usr/share/anaconda/pixmaps/topbar-bg.png     the bar across each screen

Why: the installer showed Fedora's logo on every screen of an ISO that
is not Fedora, and Fedora's trademark guidelines do not allow a
modified system to carry its marks. The mark drawn here is the
node-graph N, from the same coordinates as the website's SVG.
"""

import glob
import gzip
import io
import os
import pathlib
import stat
import sys

from PIL import Image, ImageDraw, ImageFont

BACKGROUND = (12, 12, 12)
RAISED = (20, 20, 20)
ACCENT = (249, 115, 22)
TEXT = (232, 232, 232)

PIXMAPS = "usr/share/anaconda/pixmaps"


def font(size):
    for pattern in ("/usr/share/fonts/**/NotoSans-Bold.ttf",
                    "/usr/share/fonts/**/NotoSans*Bold*.ttf",
                    "/usr/share/fonts/**/DejaVuSans-Bold.ttf"):
        found = sorted(glob.glob(pattern, recursive=True))
        if found:
            return ImageFont.truetype(found[0], size)
    raise SystemExit("No bold sans font found; the artwork stage installs one.")


def mark(size, colour, background):
    """The node-graph N, drawn from the website SVG's 100x100 geometry.

    Strokes a little heavier than the SVG's: at sidebar size the
    website's weight thins to a hairline.
    """
    scale = 4
    big = size * scale
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    unit = big / 100.0
    width = max(1, round(6 * unit))

    def p(x, y):
        return (x * unit, y * unit)

    for start, end in (((24, 24), (24, 76)),
                       ((24, 24), (76, 76)),
                       ((76, 24), (76, 76))):
        draw.line([p(*start), p(*end)], fill=colour, width=width)

    radius = 10 * unit
    for cx, cy in ((24, 24), (76, 24), (50, 50), (24, 76), (76, 76)):
        x, y = p(cx, cy)
        draw.ellipse([x - radius, y - radius, x + radius, y + radius],
                     fill=background, outline=colour, width=width)

    return image.resize((size, size), Image.LANCZOS)


def sidebar_logo():
    """Mark above name: the sidebar is narrow, about 150 pixels."""
    mark_size = 60
    face = font(13)
    text = "NEXUS-CORE"
    spacing = 2
    boxes = [face.getbbox(c) for c in text]
    widths = [box[2] - box[0] for box in boxes]
    text_width = sum(widths) + spacing * (len(text) - 1)
    text_height = face.getbbox("N")[3] - face.getbbox("N")[1]

    width = max(mark_size, text_width) + 8
    height = mark_size + 10 + text_height + 4
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))

    logo = mark(mark_size, ACCENT + (255,), BACKGROUND + (255,))
    image.paste(logo, ((width - mark_size) // 2, 0), logo)

    draw = ImageDraw.Draw(image)
    x = (width - text_width) // 2
    y = mark_size + 10 - face.getbbox("N")[1]
    for character, w, box in zip(text, widths, boxes):
        draw.text((x - box[0], y), character, font=face, fill=TEXT + (255,))
        x += w + spacing
    return image


def sidebar_background():
    # Plain. Anaconda does not scale it, and the sidebar's width depends
    # on the screen, so anything placed at an edge may be cut off.
    return Image.new("RGB", (400, 2000), BACKGROUND)


def topbar_background():
    return Image.new("RGB", (64, 64), RAISED)


def cpio_newc(entries):
    """A gzip-compressed 'newc' cpio archive, as Anaconda expects."""
    out = io.BytesIO()
    inode = 1

    def header(name, mode, size, nlink):
        nonlocal inode
        fields = [inode, mode, 0, 0, nlink, 0, size, 0, 0, 0, 0,
                  len(name) + 1, 0]
        inode += 1
        return b"070701" + b"".join(b"%08X" % f for f in fields)

    def pad(n):
        return b"\0" * ((4 - n % 4) % 4)

    for name, data in entries:
        encoded = name.encode()
        if data is None:
            record = header(encoded, stat.S_IFDIR | 0o755, 0, 2) + encoded + b"\0"
            out.write(record + pad(len(record)))
        else:
            record = header(encoded, stat.S_IFREG | 0o644, len(data), 1) \
                + encoded + b"\0"
            out.write(record + pad(len(record)) + data + pad(len(data)))

    trailer = header(b"TRAILER!!!", 0, 0, 1) + b"TRAILER!!!\0"
    out.write(trailer + pad(len(trailer)))
    return gzip.compress(out.getvalue(), 9)


def png(image):
    buffer = io.BytesIO()
    image.save(buffer, "PNG", optimize=True)
    return buffer.getvalue()


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2

    output = pathlib.Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)

    files = {
        "sidebar-logo.png": sidebar_logo(),
        "sidebar-bg.png": sidebar_background(),
        "topbar-bg.png": topbar_background(),
    }

    entries = []
    parts = PIXMAPS.split("/")
    for depth in range(1, len(parts) + 1):
        entries.append(("/".join(parts[:depth]), None))

    for name, image in files.items():
        data = png(image)
        entries.append((f"{PIXMAPS}/{name}", data))
        (output / name).write_bytes(data)
        print(f"  {name:18} {image.size[0]}x{image.size[1]}")

    (output / "product.img").write_bytes(cpio_newc(entries))
    print(f"  product.img        {(output / 'product.img').stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
