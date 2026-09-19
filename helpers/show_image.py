#!/usr/bin/env python3
"""Decode a column-major RGB base64 blob and display it.

The base64 string decodes to bytes laid out as:
    Columns { Rows { uint8_t * 3 } }
i.e. for each column x, then for each row y, three bytes R,G,B.

The image is stored column-major but PPM/kitty expect row-major, so the
pixel data is transposed here: WIDTH is the number of columns, HEIGHT the
number of rows.
"""

import argparse
import base64
import os
import sys


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("width", type=int, help="image width in pixels (number of columns)")
    p.add_argument("height", type=int, help="image height in pixels (number of rows)")
    p.add_argument("b64", nargs="?", help="base64 string; if omitted, read from stdin")
    p.add_argument("--file", metavar="PATH", help="read base64 from a file")
    p.add_argument("--ppm", metavar="PATH", help="also write a P6 PPM file")
    p.add_argument("--no-kitty", action="store_true", help="disable kitty display")
    disp = p.add_argument_group("kitty scaling (display size in terminal cells)")
    disp.add_argument("--scale", type=float, default=1.0, metavar="N",
                      help="enlarge displayed image by N (width-based; height follows aspect)")
    disp.add_argument("--cols", type=int, metavar="N",
                      help="display width in terminal columns (overrides --scale)")
    disp.add_argument("--rows", type=int, metavar="N",
                      help="display height in terminal rows (overrides --scale)")
    return p.parse_args()


def decode_pixels(w, h, b64):
    raw = base64.b64decode(b64)
    expected = w * h * 3
    if len(raw) != expected:
        raise SystemExit(
            f"decoded {len(raw)} bytes, expected {expected} "
            f"({w}x{h}x3); input is not column-major RGB of that size"
        )
    # raw is column-major: pixel at (col, row) is raw[(col*h + row)*3 : +3].
    # PPM/kitty want row-major, so transpose into out[row][col].
    out = bytearray()
    for row in range(h):
        for col in range(w):
            i = (col * h + row) * 3
            out += raw[i:i + 3]
    return bytes(out)


def write_ppm(path, w, h, pixels):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(pixels)
    print(f"wrote {path}")


def kitty_display(w, h, pixels, cols=None, rows=None):
    b64 = base64.b64encode(pixels).decode()
    total = len(b64)
    chunk = 4096
    first = True
    for i in range(0, total, chunk):
        part = b64[i:i + chunk]
        more = 1 if i + chunk < total else 0
        if first:
            params = f"f=24,s={w},v={h},a=T"
            if cols:
                params += f",c={cols}"
            if rows:
                params += f",r={rows}"
            params += f",m={more}"
        else:
            params = f"a=T,m={more}"
        first = False
        sys.stdout.write(f"\x1b_G{params};{part}\x1b\\")
    sys.stdout.flush()


def main():
    args = parse_args()

    if args.file:
        with open(args.file) as f:
            b64 = f.read().strip()
    elif args.b64:
        b64 = args.b64
    elif not sys.stdin.isatty():
        b64 = sys.stdin.read().strip()
    else:
        raise SystemExit("no base64 input given (argument, --file, or stdin)")

    pixels = decode_pixels(args.width, args.height, b64)

    if (
        not args.no_kitty
        and sys.stdout.isatty()
        and os.environ.get("TERM", "").startswith("xterm-kitty")
    ):
        cols = args.cols or round(args.width * args.scale)
        rows = args.rows
        kitty_display(args.width, args.height, pixels, cols=cols, rows=rows)
        print()  # newline after graphics escape
    else:
        print(
            "not showing in terminal (kitty not detected); "
            "use --ppm PATH to save the image"
        )

    if args.ppm:
        write_ppm(args.ppm, args.width, args.height, pixels)


if __name__ == "__main__":
    main()
