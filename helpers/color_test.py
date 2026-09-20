#!/usr/bin/env python3
"""Send a red/green/blue band test frame to the LED wall.

The columns are split into three equal vertical bands, left to right:
red, green, blue. Use it to verify the whole color chain
(encoder -> firmware -> strips): all three should match the names.

If red and green come out swapped, set `wireOrderRGB` in include/config.hpp to
the other value. Blue should always be blue.

Usage:
  color_test.py [--device URL] [--width N] [--height N] [--print]

Defaults to the current matrix (21x75) and http://ledfal.local.
"""

import argparse
import base64
import sys
import urllib.request

BANDS = ((255, 0, 0), (0, 255, 0), (0, 0, 255))  # R, G, B


def build(w, h):
    """Column-major RGB blob: three vertical color bands."""
    out = bytearray(w * h * 3)
    i = 0
    for col in range(w):
        band = 0 if col * 3 < w else (1 if col * 3 < 2 * w else 2)
        r, g, b = BANDS[band]
        for _ in range(h):
            out[i] = r
            out[i + 1] = g
            out[i + 2] = b
            i += 3
    return bytes(out)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--device", default="http://ledfal.local",
                   help="device base URL (default: http://ledfal.local)")
    p.add_argument("--width", type=int, default=21, help="matrix columns")
    p.add_argument("--height", type=int, default=75, help="matrix rows")
    p.add_argument("--print", dest="print_only", action="store_true",
                   help="print the base64 blob instead of sending it")
    args = p.parse_args()

    b64 = base64.b64encode(build(args.width, args.height)).decode()

    if args.print_only:
        sys.stdout.write(b64)
        return

    req = urllib.request.Request(args.device + "/matrix",
                                 data=b64.encode(), method="POST")
    with urllib.request.urlopen(req, timeout=10) as resp:
        body = resp.read().decode(errors="replace")
    print(f"{resp.status} {body}")


if __name__ == "__main__":
    main()
