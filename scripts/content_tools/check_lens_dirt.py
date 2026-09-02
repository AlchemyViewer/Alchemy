#!/usr/bin/env python3
"""\
@file   check_lens_dirt.py
@brief  Offline harness for the procedural lens dirt generator.

        The plate is generated on the GPU by lensDirtGenF.glsl, which cannot be
        inspected from here. This mirrors that shader in Python and answers the
        one question the GLSL cannot answer on its own: does the plate still
        read as dirt?

        That question is not rhetorical. The four plates this effect used to
        ship with were, on their first bake, almost invisible at maxed sliders
        -- every one of them had bright specks and nothing in between, and the
        histogram is what caught it. Coverage and peak are therefore scored
        against per-preset targets carried over from the plates that replaced
        them. The median is reported but not scored: a subtle plate is supposed
        to have a near-black median, and scoring it would push every preset to
        the same level.

        A Python mirror of a shader is only worth having while it stays a
        mirror, so --verify-port compares every numeric constant in this file's
        build() against the shader's main() and fails if they have drifted.
        Run it before trusting any number this script prints.

        Usage:
            python scripts/content_tools/check_lens_dirt.py
            python scripts/content_tools/check_lens_dirt.py --seeds 8
            python scripts/content_tools/check_lens_dirt.py --sheet dirt.png

        Unlike the bake script it replaces, this uses numpy. That script
        avoided it because it produced shipped assets and the viewer's build
        environment has no numpy; this one produces nothing the viewer loads,
        so the constraint no longer applies -- and a pure-Python port of a
        few-hundred-ALU shader would take minutes per plate.

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
"""

import argparse
import os
import re
import sys

try:
    import numpy as np
except ImportError:
    sys.exit("numpy is required: pip install numpy")


SHADER = os.path.join("indra", "newview", "app_settings", "shaders",
                      "class1", "effects", "lensDirtGenF.glsl")

# Layer densities and tone curve per preset, plus the histogram it is aiming
# for. These are the four looks the bundled plates used to cover, kept as
# starting points rather than as shipped files -- the settings' Comments carry
# the same numbers so a user can dial one in.
#
# `want` is (p50, p99, coverage above 0.25) with a generous tolerance. It
# exists to catch a preset drifting back into invisibility, not to pin exact
# numbers.
PRESETS = {
    "Subtle":  dict(grime=0.45, mote_scale=1.0, smudge=0.5, scratches=0,
                    toe=1.8, gain=1.00, want=(0.01, 0.30, 0.03),
                    blurb="a clean lens that has simply been outdoors"),
    "Dirty":   dict(grime=1.00, mote_scale=1.0, smudge=1.0, scratches=0,
                    toe=1.6, gain=1.00, want=(0.06, 0.50, 0.12),
                    blurb="a working lens nobody has wiped in a while"),
    "Extreme": dict(grime=1.90, mote_scale=1.0, smudge=1.5, scratches=0,
                    toe=1.2, gain=1.10, want=(0.16, 0.75, 0.37),
                    blurb="filthy -- fingerprints, sea spray, a bad day"),
    "Damaged": dict(grime=0.95, mote_scale=1.0, smudge=0.9, scratches=16,
                    toe=1.2, gain=0.90, want=(0.07, 0.50, 0.19),
                    blurb="grime plus physical damage: scratches and chips"),
}


# ---------------------------------------------------------------- hashes ----
def fract(x):
    return x - np.floor(x)


def hash21(px, py, seed):
    return fract(np.sin(px * 127.1 + py * 311.7 + seed * 74.7) * 43758.5453)


def hash22(px, py, seed):
    return (fract(np.sin(px * 127.1 + py * 311.7 + seed * 74.7) * 43758.5453),
            fract(np.sin(px * 269.5 + py * 183.3 + seed * 51.3) * 43758.5453))


def screen(a, b):
    return 1.0 - (1.0 - a) * (1.0 - b)


# ------------------------------------------------------------ mote layer ----
def motes(u, v, cells, r_min, r_max, softness, seed, aspect, density, bias):
    """One jittered disc per grid cell over the 3x3 neighbourhood.

    Cells are dropped when their hash exceeds `density`, because one feature
    per cell is perfectly even and dirt is not. Radii are raised to `bias` so
    most motes come out small with a few large. The profile is a flat interior
    with a quick rim rather than a Gaussian -- a Gaussian has no edge anywhere
    and reads as fog the moment neighbours overlap."""
    acc = np.zeros_like(u)
    cx, cy = np.floor(u * cells), np.floor(v * cells)
    for ox in (-1, 0, 1):
        for oy in (-1, 0, 1):
            gx, gy = cx + ox, cy + oy
            keep = hash21(gx, gy, seed + 57.0) < density
            jx, jy = hash22(gx, gy, seed)
            fx, fy = (gx + jx) / cells, (gy + jy) / cells
            rad = r_min + (r_max - r_min) * np.power(hash21(gx, gy, seed + 19.0), bias)
            d = np.sqrt(((u - fx) * aspect) ** 2 + (v - fy) ** 2)
            x = np.clip((rad - d) / (rad * softness + 1e-9), 0.0, 1.0)
            acc = screen(acc, x * x * (3.0 - 2.0 * x) * keep)
    return acc


# ----------------------------------------------------------- value noise ----
def vnoise(u, v, freq, seed):
    x, y = u * freq, v * freq
    ix, iy = np.floor(x), np.floor(y)
    fx, fy = x - ix, y - iy
    fx = fx * fx * (3.0 - 2.0 * fx)
    fy = fy * fy * (3.0 - 2.0 * fy)
    a = hash21(ix, iy, seed)
    b = hash21(ix + 1, iy, seed)
    c = hash21(ix, iy + 1, seed)
    d = hash21(ix + 1, iy + 1, seed)
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def fbm3(u, v, freq, seed):
    total, amp, norm = np.zeros_like(u), 1.0, 0.0
    for i in range(3):
        total += amp * vnoise(u, v, freq * (2 ** i), seed + i * 13.0)
        norm += amp
        amp *= 0.5
    return total / norm


# ------------------------------------------------------------- segments -----
def seg_dist(u, v, ax, ay, bx, by, aspect):
    pax, pay = (u - ax) * aspect, v - ay
    bax, bay = (bx - ax) * aspect, by - ay
    h = np.clip((pax * bax + pay * bay) / max(bax * bax + bay * bay, 1e-9), 0.0, 1.0)
    return np.sqrt((pax - bax * h) ** 2 + (pay - bay * h) ** 2)


def lines(u, v, count, length, width, seed, aspect, wander):
    """Fibres and scratches: a segment distance field, domain-warped by
    low-frequency noise so a fibre wanders while a scratch stays straight."""
    uu, vv = u, v
    if wander > 0.0:
        uu = u + (fbm3(u, v, 3.0, seed + 7.0) - 0.5) * wander
        vv = v + (fbm3(u, v, 3.0, seed + 31.0) - 0.5) * wander
    acc = np.zeros_like(u)
    for i in range(count):
        fi = float(i)
        ax = hash21(fi, 1.0, seed)
        ay = hash21(fi, 2.0, seed)
        ang = hash21(fi, 3.0, seed) * 6.2831853
        ln = length * (0.4 + 0.6 * hash21(fi, 4.0, seed))
        bx, by = ax + np.cos(ang) * ln, ay + np.sin(ang) * ln
        w = width * (0.5 + 0.5 * hash21(fi, 5.0, seed))
        d = seg_dist(uu, vv, ax, ay, bx, by, aspect)
        t = np.clip((w - d) / max(w, 1e-9), 0.0, 1.0)
        acc = np.maximum(acc, t * t * (3.0 - 2.0 * t) * (0.55 + 0.45 * hash21(fi, 6.0, seed)))
    return acc


# ---------------------------------------------------------------- build -----
def build(w, h, seed, grime, mote_scale, smudge, scratches, toe, gain):
    """Mirror of lensDirtGenF.glsl's main().

    Written statement for statement against the shader, and deliberately so:
    the port check below compares the two constant streams in order, which only
    works while the two read in parallel. That is why `img` is spelled with an
    explicit 0.0 and the vignette uses hypot on both sides -- idiomatic numpy
    would drop constants the shader has, or add ones it does not, and the check
    would start reporting drift that is not there."""
    aspect = w / float(h)
    v, u = np.meshgrid(np.linspace(0, 1, h, endpoint=False),
                       np.linspace(0, 1, w, endpoint=False), indexing='ij')
    mscale = max(mote_scale, 0.05)

    clump = fbm3(u, v, 5.0, seed + 101.0)
    clump = 0.62 + 0.76 * np.clip((clump - 0.34) / 0.36, 0.0, 1.0)

    img = np.full_like(u, 0.0)
    img = screen(img, motes(u, v, 4.5 / mscale, 0.050, 0.140, 0.45,
                            seed, aspect, 0.80 * grime, 1.7) * 0.46 * clump)
    img = screen(img, motes(u, v, 9.0 / mscale, 0.022, 0.064, 0.42,
                            seed + 3.0, aspect, 0.75 * grime, 1.9) * 0.50 * clump)
    img = screen(img, motes(u, v, 20.0 / mscale, 0.009, 0.028, 0.40,
                            seed + 5.0, aspect, 0.60 * grime, 2.1) * 0.44 * clump)
    img = screen(img, motes(u, v, 90.0, 0.0011, 0.0030, 0.55,
                            seed + 9.0, aspect, 0.34 * grime, 1.5) * 0.75)

    sm = fbm3(u, v, 1.6, seed + 21.0)
    img = screen(img, np.power(np.clip((sm - 0.50) / 0.34, 0.0, 1.0), 1.4) * 0.34 * smudge)

    img = screen(img, lines(u, v, 12, 0.20, 0.0014, seed + 41.0, aspect, 0.06) * 0.62)

    if scratches > 0:
        img = screen(img, lines(u, v, scratches, 0.55, 0.0022, seed + 57.0, aspect, 0.0))

    r = np.clip(np.hypot((u - 0.5) * aspect, v - 0.5) / (0.5 * np.hypot(aspect, 1.0)), 0.0, 1.0)
    img *= 0.22 + 0.78 * np.power(r, 0.75)

    return np.clip(gain * np.power(np.clip(img, 0.0, 1.0), toe), 0.0, 1.0)


# ---------------------------------------------------------- port checking ---
def _floats(text):
    """Numeric literals, normalised so 9 and 9.0 compare equal."""
    return [float(x) for x in re.findall(r'(?<![\w.])\d+\.?\d*(?:e-?\d+)?', text)]


def _span(text, start_pat, end_pat):
    """From the first match of start_pat to the end of the first match of
    end_pat after it. None if either is missing."""
    a = re.search(start_pat, text)
    if not a:
        return None
    b = re.search(end_pat, text[a.start():])
    return text[a.start():a.start() + b.end()] if b else None


def verify_port(shader_path):
    """The Python above is only meaningful while it matches the shader. Compare
    the constants of the two build routines and report any that differ.

    Both are read from the first shared statement to the last. Outside that
    range the two differ structurally -- the shader is handed its uv and its
    parameters as uniforms, this builds a grid and takes arguments -- and those
    constants describe the harness rather than the plate."""
    try:
        glsl = open(shader_path, encoding='utf-8').read()
    except IOError:
        print("cannot read %s -- run from the repository root" % shader_path)
        return False

    glsl_body = _span(glsl, r'float mscale = max\(', r'float v = clamp\([^\n]*\n')
    if glsl_body is None:
        print("could not find the generator body in %s" % shader_path)
        return False
    # Comments carry prose numbers ("19% to 76%"); only code is comparable.
    glsl_body = re.sub(r'//[^\n]*', '', glsl_body)

    py = open(__file__, encoding='utf-8').read()
    py_body = _span(py, r'    mscale = max\(', r'    return np\.clip\([^\n]*\n')
    py_body = re.sub(r'#[^\n]*', '', py_body)

    a, b = _floats(glsl_body), _floats(py_body)
    if a == b:
        print("port check: %d constants, shader and mirror agree" % len(a))
        return True

    print("port check FAILED: %d constants in the shader, %d here" % (len(a), len(b)))
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else None
        y = b[i] if i < len(b) else None
        if x != y:
            print("  first difference at %d: shader %s, mirror %s" % (i, x, y))
            break
    print("  lensDirtGenF.glsl and build() have drifted; every number below is suspect.")
    return False


# ---------------------------------------------------------------- report ----
def describe(a):
    """(p50, p99, fraction above 0.25) -- the numbers that decide whether a
    plate reads as dirt or as confetti."""
    f = np.sort(a.ravel())
    return (float(f[int(0.50 * (len(f) - 1))]),
            float(f[int(0.99 * (len(f) - 1))]),
            float((a >= 0.25).mean()))


def main():
    ap = argparse.ArgumentParser(description="Check the procedural lens dirt generator.")
    ap.add_argument("--width", type=int, default=768, help="plate width (default 768)")
    ap.add_argument("--height", type=int, default=768, help="plate height (default 768)")
    ap.add_argument("--seeds", type=int, default=4,
                    help="seeds per preset; dirt pools, so density varies with the seed")
    ap.add_argument("--preset", choices=sorted(PRESETS), help="check one variant")
    ap.add_argument("--sheet", help="also write a contact sheet to this PNG (needs Pillow)")
    ap.add_argument("--shader", default=SHADER, help="shader to check the port against")
    ap.add_argument("--skip-port-check", action="store_true",
                    help="measure without first checking the mirror against the shader")
    args = ap.parse_args()

    if not args.skip_port_check and not verify_port(args.shader):
        return 2
    print()

    names = [args.preset] if args.preset else sorted(PRESETS)
    order = sorted(PRESETS)
    tiles, worst = [], 0.0

    print("%-9s %6s %6s %8s   %s" % ("preset", "p50", "p99", ">0.25", "check"))
    for name in names:
        cfg = PRESETS[name]
        w50, w99, wcov = cfg["want"]
        # Offset per preset so the variants are different lenses rather than the
        # same lens at four exposures, keyed to the preset's fixed position so
        # checking one alone reproduces what checking all of them produced.
        base = 7.0 + order.index(name) * 11.0
        stats = []
        for k in range(args.seeds):
            a = build(args.width, args.height, base + k * 97.0,
                      cfg["grime"], cfg["mote_scale"], cfg["smudge"],
                      cfg["scratches"], cfg["toe"], cfg["gain"])
            stats.append(describe(a))
            if k == 0:
                tiles.append((name, a))
        p50 = sum(s[0] for s in stats) / len(stats)
        p99 = sum(s[1] for s in stats) / len(stats)
        cov = sum(s[2] for s in stats) / len(stats)
        off = max(abs(p99 - w99) / max(w99, 1e-3),
                  abs(cov - wcov) / max(wcov, 1e-3))
        worst = max(worst, off)
        print("%-9s %6.3f %6.3f %7.1f%%   %s (want %.2f/%.2f/%.0f%%, off %.0f%%)"
              % (name, p50, p99, 100 * cov,
                 "ok" if off < 0.35 else "DRIFTED", w50, w99, 100 * wcov, 100 * off))

    if args.sheet:
        try:
            from PIL import Image, ImageDraw
        except ImportError:
            print("\n--sheet needs Pillow: pip install Pillow")
        else:
            cols = 2 if len(tiles) > 1 else 1
            rows = (len(tiles) + cols - 1) // cols
            W, H = args.width, args.height
            sheet = Image.new("RGB", (cols * (W + 10) + 10, rows * (H + 10) + 10), (16, 16, 18))
            d = ImageDraw.Draw(sheet)
            for i, (name, a) in enumerate(tiles):
                im = Image.fromarray((a * 255).astype(np.uint8), "L").convert("RGB")
                x, y = 10 + (i % cols) * (W + 10), 10 + (i // cols) * (H + 10)
                sheet.paste(im, (x, y))
                d.text((x + 8, y + 7), name, fill=(255, 236, 170))
            sheet.save(args.sheet)
            print("\nwrote %s" % args.sheet)

    if worst >= 0.35:
        print("\nAt least one preset is off its target histogram by more than a third.")
        print("Adjust its toe/gain in PRESETS -- a lower toe keeps more midtone --")
        print("then carry the change into lensDirtGenF.glsl's defaults and the settings.")
        return 1

    return 0


if __name__ == "__main__":
    # The drift check is only worth having if something can act on it, so the
    # verdict reaches the exit status.
    sys.exit(main())
