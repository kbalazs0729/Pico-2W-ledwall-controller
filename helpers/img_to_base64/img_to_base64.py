#!/usr/bin/env python3
"""Encode an image as the column-major base64 blob from BASE64_FORMAT.md.

The image is resized to width x height and its pixels are emitted as:
    Columns { Rows { uint8_t * 3 } }
i.e. for each column col, then for each row row, three bytes. The LEDs want
G,R,B (swapped relative to the doc's R,G,B). The buffer is then
standard-base64 encoded (A-Z a-z 0-9 + /), padded, no breaks.

This is the inverse of helpers/show_image.py.
"""

import argparse
import base64
import os
import sys

IMAGE_EXTS = (".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp", ".ppm", ".tga")


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("width", type=int, help="image width in pixels (number of columns)")
    p.add_argument("height", type=int, help="image height in pixels (number of rows)")
    p.add_argument("image", nargs="?", help="input image; if omitted, the only image in the cwd")
    p.add_argument("--out", metavar="PATH", help="write base64 to a file instead of stdout")
    return p.parse_args()


def find_image():
    matches = [
        f for f in sorted(os.listdir("."))
        if f.lower().endswith(IMAGE_EXTS) and os.path.isfile(f)
    ]
    if not matches:
        raise SystemExit("no image found in current directory; pass one explicitly")
    if len(matches) > 1:
        raise SystemExit(
            "multiple images in current directory, pass one explicitly:\n  "
            + "\n  ".join(matches)
        )
    return matches[0]


def encode(w, h, path):
    from PIL import Image

    img = Image.open(path).convert("RGB").resize((w, h), Image.LANCZOS)
    # img is row-major; the blob is column-major, so walk columns outer.
    # The LEDs want G,R,B (swapped relative to BASE64_FORMAT.md's R,G,B).
    out = bytearray(w * h * 3)
    i = 0
    for col in range(w):
        for row in range(h):
            r, g, b = img.getpixel((col, row))
            out[i] = g
            out[i + 1] = r
            out[i + 2] = b
            i += 3
    return bytes(out)


def main():
    args = parse_args()
    path = args.image or find_image()
    if not os.path.isfile(path):
        raise SystemExit(f"image not found: {path}")

    pixels = encode(args.width, args.height, path)
    b64 = base64.b64encode(pixels).decode()

    if args.out:
        with open(args.out, "w") as f:
            f.write(b64)
        print(f"wrote {args.out} ({len(b64)} chars, {len(pixels)} bytes)")
    else:
        sys.stdout.write(b64)


if __name__ == "__main__":
    main()
