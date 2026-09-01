#!/usr/bin/env python3
"""\
@file   generate_lens_dirt.py
@brief  Generates the bundled lens dirt plate used by the lens dirt post
        effect (RenderLensDirtTexture).

        The plate is a greyscale mask of the muck that accumulates on a front
        element: defocused dust motes, a few wipe smudges, stray fibres and
        fine specks. The renderer multiplies bloom and lens flare by it, so
        only relative brightness matters and the image is heavily biased to
        black -- a plate whose mean is near zero keeps strength 1.0 looking
        photographic instead of like a dirty window.

        Committed alongside the PNG it produces so the asset is reproducible
        and tunable rather than an opaque binary, and so its provenance is
        unambiguous: everything here is generated from a seeded RNG, so the
        output is original work with no third-party source.

        Deliberately Pillow-only. numpy would be the natural tool for this and
        would be much faster, but it is not in the viewer's build environment
        and a one-off 1024x1024 bake does not justify adding a dependency.

        Usage:
            python scripts/content_tools/generate_lens_dirt.py
            python scripts/content_tools/generate_lens_dirt.py --size 2048 --seed 7

$LicenseInfo:firstyear=2026&license=viewerlgpl$
Alchemy Viewer Source Code
Copyright (C) 2026, Alchemy Viewer Project

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation;
version 2.1 of the License only.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
$/LicenseInfo$

The generated plate itself is released as CC0 / public domain.
"""

import argparse
import math
import os
import random
import sys

try:
    from PIL import Image, ImageChops, ImageDraw, ImageFilter
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")


DEFAULT_OUT = os.path.join("indra", "newview", "app_settings", "lensdirt", "DefaultDirt.png")


def _blank(size):
    return Image.new("L", (size, size), 0)


def _screen(base, layer):
    """Lighten-only compositing. Dirt occludes light additively rather than
    stacking multiplicatively, so overlapping motes brighten toward the cap
    instead of blowing straight past it."""
    return ImageChops.lighter(base, layer)


def add_motes(img, rng, size, count, r_min, r_max, blur, peak):
    """Defocused dust: large, very soft, very dim. This is the layer that
    reads as 'grubby lens' rather than 'texture overlay', so it carries most
    of the plate's area and almost none of its brightness."""
    layer = _blank(size)
    draw = ImageDraw.Draw(layer)
    for _ in range(count):
        r = rng.uniform(r_min, r_max) * size
        x = rng.uniform(-r, size + r)
        y = rng.uniform(-r, size + r)
        v = int(255 * peak * rng.uniform(0.35, 1.0))
        # Slight eccentricity: a mote sitting off the optical axis projects
        # as an ellipse, and perfectly round blobs read as CG.
        rx = r * rng.uniform(0.8, 1.25)
        ry = r * rng.uniform(0.8, 1.25)
        draw.ellipse([x - rx, y - ry, x + rx, y + ry], fill=v)
    layer = layer.filter(ImageFilter.GaussianBlur(blur * size))
    return _screen(img, layer)


def add_smudges(img, rng, size, count, peak):
    """Wipe marks. Long, low-frequency arcs left by a cloth: a chain of
    overlapping stamps along a gentle curve, then blurred hard."""
    layer = _blank(size)
    draw = ImageDraw.Draw(layer)
    for _ in range(count):
        cx, cy = rng.uniform(0, size), rng.uniform(0, size)
        ang = rng.uniform(0, math.tau)
        length = rng.uniform(0.25, 0.75) * size
        width = rng.uniform(0.02, 0.06) * size
        curve = rng.uniform(-0.4, 0.4)
        steps = 64
        for i in range(steps):
            t = i / (steps - 1.0)
            a = ang + curve * (t - 0.5)
            x = cx + math.cos(a) * (t - 0.5) * length
            y = cy + math.sin(a) * (t - 0.5) * length
            # Fade toward both ends so a smudge has no hard stop.
            fade = math.sin(t * math.pi)
            w = width * (0.6 + 0.4 * fade)
            v = int(255 * peak * fade * rng.uniform(0.7, 1.0))
            draw.ellipse([x - w, y - w, x + w, y + w], fill=v)
    layer = layer.filter(ImageFilter.GaussianBlur(0.012 * size))
    return _screen(img, layer)


def add_fibres(img, rng, size, count, peak):
    """Stray threads. Thin, wandering, slightly brighter than the motes but
    covering almost no area."""
    layer = _blank(size)
    draw = ImageDraw.Draw(layer)
    for _ in range(count):
        x, y = rng.uniform(0, size), rng.uniform(0, size)
        ang = rng.uniform(0, math.tau)
        segs = rng.randint(12, 40)
        step = rng.uniform(0.004, 0.012) * size
        w = max(1, int(rng.uniform(0.0008, 0.002) * size))
        v = int(255 * peak * rng.uniform(0.5, 1.0))
        for _ in range(segs):
            ang += rng.uniform(-0.35, 0.35)
            nx = x + math.cos(ang) * step
            ny = y + math.sin(ang) * step
            draw.line([x, y, nx, ny], fill=v, width=w)
            x, y = nx, ny
    layer = layer.filter(ImageFilter.GaussianBlur(0.0015 * size))
    return _screen(img, layer)


def add_specks(img, rng, size, count, peak):
    """Fine grit. Near-pixel scale, the only layer allowed to be sharp."""
    layer = _blank(size)
    draw = ImageDraw.Draw(layer)
    for _ in range(count):
        x, y = rng.uniform(0, size), rng.uniform(0, size)
        r = rng.uniform(0.0006, 0.003) * size
        v = int(255 * peak * rng.uniform(0.4, 1.0))
        draw.ellipse([x - r, y - r, x + r, y + r], fill=v)
    layer = layer.filter(ImageFilter.GaussianBlur(0.0008 * size))
    return _screen(img, layer)


def build(size, seed, toe):
    rng = random.Random(seed)
    img = _blank(size)

    # Counts scale with area so a larger bake looks like the same lens rather
    # than a cleaner one.
    scale = (size / 1024.0) ** 2

    img = add_motes(img, rng, size, int(70 * scale), 0.020, 0.070, 0.010, 0.55)
    img = add_motes(img, rng, size, int(180 * scale), 0.006, 0.022, 0.004, 0.65)
    img = add_smudges(img, rng, size, max(1, int(5 * scale)), 0.50)
    img = add_fibres(img, rng, size, max(1, int(14 * scale)), 0.85)
    img = add_specks(img, rng, size, int(900 * scale), 1.00)

    # Gentle radial bias. Real dirt is not centre-weighted, but the plate is
    # multiplied against bloom and flare, and keeping the very centre slightly
    # clearer stops the effect from veiling the middle of frame where the
    # subject usually is.
    vign = _blank(size)
    vd = ImageDraw.Draw(vign)
    steps = 96
    for i in range(steps):
        t = i / (steps - 1.0)
        r = (0.15 + 0.85 * t) * size * 0.85
        v = int(255 * (0.55 + 0.45 * t))
        vd.ellipse([size / 2 - r, size / 2 - r, size / 2 + r, size / 2 + r], outline=v, width=max(1, int(size / steps) + 2))
    vign = vign.filter(ImageFilter.GaussianBlur(0.05 * size))
    img = ImageChops.multiply(img, vign)

    # Crush the low end.
    #
    # The layers above are deliberately generous with area -- broad soft motes
    # are what make the plate read as grime rather than confetti -- but summed
    # and blurred they leave most of the frame faintly lit, which multiplied
    # into bloom would veil the whole image. A power curve pushes that haze
    # back toward black while leaving specks and fibre highlights intact.
    #
    # This is doing the tonal work, not the layer peaks: it is much easier to
    # author generous layers and crush them than to hand-tune a dozen peaks
    # into the right histogram.
    inv = 1.0 / 255.0
    lut = [int(255.0 * ((i * inv) ** toe) + 0.5) for i in range(256)]
    img = img.point(lut)

    return img


def main():
    ap = argparse.ArgumentParser(description="Generate the bundled lens dirt plate.")
    ap.add_argument("--size", type=int, default=1024, help="square edge in pixels (default 1024)")
    ap.add_argument("--seed", type=int, default=20260831, help="RNG seed; changing it reshuffles the muck")
    ap.add_argument("--out", default=DEFAULT_OUT, help="output path (default %s)" % DEFAULT_OUT)
    ap.add_argument("--toe", type=float, default=2.6,
                    help="low-end crush exponent; higher is darker and more contrasty (default 2.6)")
    args = ap.parse_args()

    img = build(args.size, args.seed, args.toe)

    out_dir = os.path.dirname(args.out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    # Greyscale, no alpha: the renderer wants a scalar mask, and an 8-bit L
    # PNG is both the smallest on disk and the cheapest as GL_R8.
    img.save(args.out, "PNG", optimize=True)

    hist = img.histogram()
    total = float(sum(hist))
    mean = sum(i * n for i, n in enumerate(hist)) / total / 255.0
    print("wrote %s (%dx%d, mean %.4f, %.1f KB)"
          % (args.out, args.size, args.size, mean, os.path.getsize(args.out) / 1024.0))
    if mean > 0.10:
        print("warning: mean is high for a dirt plate; strength 1.0 may veil the frame")


if __name__ == "__main__":
    main()
