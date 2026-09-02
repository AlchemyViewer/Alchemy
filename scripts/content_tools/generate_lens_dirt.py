#!/usr/bin/env python3
"""\
@file   generate_lens_dirt.py
@brief  Generates the bundled lens dirt plates used by the lens dirt post
        effect (RenderLensDirtTexture).

        A plate is a greyscale mask of the muck on a front element:
        defocused dust motes, wipe smudges, stray fibres, fine specks, and for
        the damaged variant a few scratches and chips. The renderer multiplies
        bloom and lens flare by it, so only relative brightness matters.

        Four are baked, and the difference between them is coverage rather
        than peak. Dirt reads as a mottled veil over bright areas, so what
        matters is how much of the plate sits in the visible midtones -- a
        plate can have bright specks and still be invisible if everything
        between them is black. Each preset therefore declares the histogram it
        is aiming for and the bake checks itself against it.

        Committed alongside the PNGs it produces so the assets are reproducible
        and tunable rather than an opaque binary, and so its provenance is
        unambiguous: everything here is generated from a seeded RNG, so the
        output is original work with no third-party source.

        Deliberately Pillow-only. numpy would be the natural tool for this and
        would be much faster, but it is not in the viewer's build environment
        and a one-off 1024x1024 bake does not justify adding a dependency.

        Usage:
            python scripts/content_tools/generate_lens_dirt.py --all
            python scripts/content_tools/generate_lens_dirt.py --preset Dirty
            python scripts/content_tools/generate_lens_dirt.py --all --size 2048

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


OUT_DIR = os.path.join("indra", "newview", "app_settings", "lensdirt")


# Each preset carries its layer densities, the tone curve applied afterwards,
# and the histogram it is aiming for. `toe` is the low-end crush: it is doing
# the tonal work, because it is far easier to author generous layers and shape
# them afterwards than to hand-tune a dozen peaks into the right distribution.
# Lower toe keeps more midtone, which is what makes a plate visible at all.
#
# `want` is (p50, p99, coverage above 0.25) with a generous tolerance -- it
# exists to catch a preset drifting into invisibility again, not to pin exact
# numbers.
PRESETS = {
    "Subtle": dict(
        motes_big=50, motes_small=140, smudges=3, fibres=8, specks=600,
        scratches=0, chips=0, toe=1.25, gain=0.90,
        want=(0.01, 0.30, 0.03),
        blurb="a clean lens that has simply been outdoors"),
    "Dirty": dict(
        motes_big=80, motes_small=220, smudges=6, fibres=16, specks=1000,
        scratches=0, chips=0, toe=1.80, gain=2.10,
        want=(0.06, 0.50, 0.12),
        blurb="a working lens nobody has wiped in a while"),
    "Extreme": dict(
        motes_big=120, motes_small=320, smudges=10, fibres=26, specks=1500,
        scratches=0, chips=0, toe=1.50, gain=2.25,
        want=(0.16, 0.75, 0.37),
        blurb="filthy -- fingerprints, sea spray, a bad day"),
    "Damaged": dict(
        motes_big=70, motes_small=200, smudges=5, fibres=14, specks=900,
        scratches=16, chips=34, toe=1.35, gain=1.35,
        want=(0.07, 0.50, 0.19),
        blurb="grime plus physical damage: scratches and coating chips"),
}


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


def add_scratches(img, rng, size, count, chips):
    """Physical damage rather than dirt. Scratches are long, near-straight and
    bright with a hard core -- a wipe with grit in the cloth, not a fibre lying
    on the glass -- and chips are the small very bright points where the
    coating has actually gone. Both are sharper than anything in the muck
    layers, which is what makes the variant read as damage and not just more
    dust."""
    layer = _blank(size)
    draw = ImageDraw.Draw(layer)

    for _ in range(count):
        # Start somewhere off-centre and run a long way; real scratches cross
        # the element rather than sitting in one corner.
        x = rng.uniform(-0.1, 1.1) * size
        y = rng.uniform(-0.1, 1.1) * size
        ang = rng.uniform(0, math.tau)
        length = rng.uniform(0.25, 0.85) * size
        segs = 40
        step = length / segs
        w = max(1, int(rng.uniform(0.0016, 0.0042) * size))
        v = int(255 * rng.uniform(0.70, 1.00))
        for _ in range(segs):
            ang += rng.uniform(-0.04, 0.04)      # far straighter than a fibre
            nx = x + math.cos(ang) * step
            ny = y + math.sin(ang) * step
            draw.line([x, y, nx, ny], fill=v, width=w)
            x, y = nx, ny

    for _ in range(chips):
        x, y = rng.uniform(0, size), rng.uniform(0, size)
        r = rng.uniform(0.0015, 0.006) * size
        v = int(255 * rng.uniform(0.75, 1.0))
        # A few overlapping blobs so the chip has a ragged edge.
        for _ in range(rng.randint(2, 4)):
            ox, oy = rng.uniform(-r, r), rng.uniform(-r, r)
            rr = r * rng.uniform(0.5, 1.0)
            draw.ellipse([x + ox - rr, y + oy - rr, x + ox + rr, y + oy + rr], fill=v)

    layer = layer.filter(ImageFilter.GaussianBlur(0.0007 * size))
    return _screen(img, layer)


def build(size, seed, preset):
    rng = random.Random(seed)
    img = _blank(size)
    p = PRESETS[preset]

    # Counts scale with area so a larger bake looks like the same lens rather
    # than a cleaner one.
    scale = (size / 1024.0) ** 2

    img = add_motes(img, rng, size, int(p["motes_big"] * scale), 0.020, 0.070, 0.010, 0.55)
    img = add_motes(img, rng, size, int(p["motes_small"] * scale), 0.006, 0.022, 0.004, 0.65)
    img = add_smudges(img, rng, size, max(1, int(p["smudges"] * scale)), 0.50)
    img = add_fibres(img, rng, size, max(1, int(p["fibres"] * scale)), 0.85)
    img = add_specks(img, rng, size, int(p["specks"] * scale), 1.00)
    if p["scratches"] or p["chips"]:
        img = add_scratches(img, rng, size, max(1, int(p["scratches"] * scale)),
                            int(p["chips"] * scale))

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

    # Shape the low end.
    #
    # The layers above are deliberately generous with area -- broad soft motes
    # are what make the plate read as grime rather than confetti -- but summed
    # and blurred they leave most of the frame faintly lit. A power curve pulls
    # that haze back down while leaving specks and fibre highlights intact.
    #
    # Getting this wrong in the crushing direction is what made the first
    # bundled plate invisible: at an exponent of 2.6 only 0.14% of it survived
    # above 0.25, so the effect was a few dozen bright dots rather than a veil,
    # and it stayed imperceptible even with the slider at maximum. The exponents
    # here are per-preset and much gentler.
    inv = 1.0 / 255.0
    toe, gain = p["toe"], p["gain"]
    lut = [min(255, int(255.0 * gain * ((i * inv) ** toe) + 0.5)) for i in range(256)]
    img = img.point(lut)

    return img


def describe(img):
    """(p50, p99, fraction above 0.25) -- the numbers that decide whether a
    plate reads as dirt or as confetti."""
    hist = img.histogram()
    total = sum(hist)
    acc, p50, p99 = 0, None, None
    for i, n in enumerate(hist):
        acc += n
        if p50 is None and acc >= total * 0.50:
            p50 = i / 255.0
        if p99 is None and acc >= total * 0.99:
            p99 = i / 255.0
    cover = sum(hist[int(0.25 * 255):]) / float(total)
    mean = sum(i * n for i, n in enumerate(hist)) / float(total) / 255.0
    return p50, p99, cover, mean


def main():
    ap = argparse.ArgumentParser(description="Generate the bundled lens dirt plates.")
    ap.add_argument("--size", type=int, default=1024, help="square edge in pixels (default 1024)")
    ap.add_argument("--seed", type=int, default=20260831, help="RNG seed; changing it reshuffles the muck")
    ap.add_argument("--preset", choices=sorted(PRESETS), help="bake one variant")
    ap.add_argument("--all", action="store_true", help="bake every variant")
    ap.add_argument("--out-dir", default=OUT_DIR, help="output directory (default %s)" % OUT_DIR)
    args = ap.parse_args()

    if not args.all and not args.preset:
        ap.error("pass --preset NAME or --all")

    names = sorted(PRESETS) if args.all else [args.preset]
    os.makedirs(args.out_dir, exist_ok=True)

    print("%-9s %6s %6s %8s %7s   %s" % ("preset", "p50", "p99", ">0.25", "size", "check"))
    worst = 0
    order = sorted(PRESETS)
    for name in names:
        # Offset the seed per preset so the variants are different lenses
        # rather than the same lens at four exposures. Keyed to the preset's
        # fixed position, not to its index in this run -- otherwise baking one
        # variant on its own would not reproduce what --all produced.
        img = build(args.size, args.seed + order.index(name) * 977, name)
        out = os.path.join(args.out_dir, name + ".png")

        # Greyscale, no alpha: the renderer wants a scalar mask, and an 8-bit L
        # PNG is both the smallest on disk and the cheapest as GL_R8.
        img.save(out, "PNG", optimize=True)

        p50, p99, cover, mean = describe(img)
        w50, w99, wcov = PRESETS[name]["want"]
        # Coverage and peak decide whether a plate reads; the median is
        # reported but not scored, because a subtle plate is supposed to have a
        # near-black median and scoring it would push every variant to the same
        # level.
        off = max(abs(p99 - w99) / max(w99, 1e-3),
                  abs(cover - wcov) / max(wcov, 1e-3))
        worst = max(worst, off)
        print("%-9s %6.3f %6.3f %7.1f%% %6.0fKB   %s (want %.2f/%.2f/%.0f%%, off %.0f%%)"
              % (name, p50, p99, 100 * cover, os.path.getsize(out) / 1024.0,
                 "ok" if off < 0.35 else "DRIFTED", w50, w99, 100 * wcov, 100 * off))

    if worst >= 0.35:
        print("\nAt least one preset is off its target histogram by more than a third.")
        print("Adjust its toe/gain in PRESETS -- lower toe keeps more midtone.")
        return 1

    return 0


if __name__ == "__main__":
    # The drift check is only worth having if something can act on it, so
    # the verdict reaches the exit status and a regeneration or packaging
    # step can refuse a plate that missed its histogram.
    sys.exit(main())
