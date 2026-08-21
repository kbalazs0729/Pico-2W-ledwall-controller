#!/usr/bin/env python3
"""Generate smooth (Perlin) colored noise and emit it as the LED-matrix blob.

Output is the same format as BASE64_FORMAT.md:
    column-major RGB, one byte per channel, standard base64 with padding,
    no line breaks.
"""

import argparse
import base64
import math
import random
import sys

GRAD = (
    (1, 1, 0), (-1, 1, 0), (1, -1, 0), (-1, -1, 0),
    (1, 0, 1), (-1, 0, 1), (1, 0, -1), (-1, 0, -1),
    (0, 1, 1), (0, -1, 1), (0, 1, -1), (0, -1, -1),
    (1, 1, 0), (-1, 1, 0), (0, -1, 1), (0, -1, -1),
)


def _fade(t):
    return t * t * t * (t * (t * 6 - 15) + 10)


def _lerp(a, b, t):
    return a + t * (b - a)


def _dot3(g, x, y, z):
    return g[0] * x + g[1] * y + g[2] * z


def _smooth_noise(seed, x, y):
    """Classic 2D Perlin noise, ~[-1, 1]. Deterministic per seed."""
    px, py = math.floor(x), math.floor(y)
    fx, fy = x - px, y - py
    ux, uy = _fade(fx), _fade(fy)

    rnd = random.Random(seed)
    grid = [[rnd.choice(GRAD) for _ in range(px + 2)] for _ in range(py + 2)]

    def g(cx, cy):
        return grid[cy][cx]

    n00 = _dot3(g(px, py), fx, fy, 0.0)
    n10 = _dot3(g(px + 1, py), fx - 1, fy, 0.0)
    n01 = _dot3(g(px, py + 1), fx, fy - 1, 0.0)
    n11 = _dot3(g(px + 1, py + 1), fx - 1, fy - 1, 0.0)
    n0 = _lerp(n00, n10, ux)
    n1 = _lerp(n01, n11, ux)
    return _lerp(n0, n1, uy)


def _fbm(seed, x, y, octaves, lacunarity, gain):
    """Fractal (multi-octave) noise in ~[-1, 1]."""
    total, amp, freq, norm = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        total += amp * _smooth_noise(seed + o * 1013, x * freq, y * freq)
        norm += amp
        amp *= gain
        freq *= lacunarity
    return total / norm


def build_noise(w, h, scale, octaves, lacunarity, gain, seed):
    """Return column-major bytes: for each col, for each row, R,G,B."""
    rng = random.Random(seed)
    seeds = (rng.randrange(2**31), rng.randrange(2**31), rng.randrange(2**31))
    out = bytearray(w * h * 3)
    i = 0
    for col in range(w):
        for row in range(h):
            nx = col * scale
            ny = row * scale
            r = _fbm(seeds[0], nx, ny, octaves, lacunarity, gain)
            g = _fbm(seeds[1], nx, ny, octaves, lacunarity, gain)
            b = _fbm(seeds[2], nx, ny, octaves, lacunarity, gain)
            out[i] = int((r + 1) * 0.5 * 255 + 0.5)
            out[i + 1] = int((g + 1) * 0.5 * 255 + 0.5)
            out[i + 2] = int((b + 1) * 0.5 * 255 + 0.5)
            i += 3
    return bytes(out)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("width", type=int, help="image width (number of columns)")
    p.add_argument("height", type=int, help="image height (number of rows)")
    p.add_argument("--scale", type=float, default=0.08, metavar="F",
                   help="noise frequency (default 0.08; lower = smoother/coarser)")
    p.add_argument("--octaves", type=int, default=4,
                   help="fBm octaves (default 4)")
    p.add_argument("--lacunarity", type=float, default=2.0,
                   help="fBm lacunarity (default 2.0)")
    p.add_argument("--gain", type=float, default=0.5,
                   help="fBm persistence/gain (default 0.5)")
    p.add_argument("--seed", type=int, default=None,
                   help="random seed (default: derive from time)")
    p.add_argument("--stdin", action="store_true", help=argparse.SUPPRESS)
    return p.parse_args()


def main():
    args = parse_args()
    seed = args.seed if args.seed is not None else random.randrange(2**31)
    pixels = build_noise(
        args.width, args.height, args.scale, args.octaves,
        args.lacunarity, args.gain, seed,
    )
    sys.stdout.write(base64.b64encode(pixels).decode())


if __name__ == "__main__":
    main()
