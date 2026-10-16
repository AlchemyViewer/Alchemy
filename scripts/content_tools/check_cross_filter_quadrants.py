#!/usr/bin/env python3
"""Check that the cross-screen filter reads its streak buffers identically
whether they are dedicated targets or quadrants of one borrowed target.

The three streak buffers used to be three render targets of their own, each
sampled through a CLAMP_TO_EDGE bilinear sampler. They are now quadrants of
mWaterDis (see LLPipeline::generateBloomHDR), and crossFilterF.glsl maps a
region-relative coordinate into the texture through uCrossRegion after
clamping it to the quadrant's texel-centre range through uCrossClamp -- which
is what the sampler's edge clamp did for a dedicated target. colorCorrectF.glsl
reads the accumulator the same way.

This models both layouts statement for statement against the shader's tap
loop and compares every texel of the accumulator, for every arm, on an input
with bright texels on the edges so that taps run off the region. It also
runs the naive quadrant read without the clamp, to show that the clamp is
load-bearing rather than cosmetic: without it a tap near a quadrant's edge
reads the neighbouring quadrant.

The tap loop here mirrors crossFilterF.glsl. If the shader's tiling, stride
or weighting changes, change it here in the same commit; the whole point of
the mirror is that the two cannot drift apart unnoticed.

Exit status 0 when the layouts agree to floating-point noise, 1 otherwise.
"""

import math
import random
import sys

TAPS = 4                                   # CROSS_FILTER_TAPS
STREAK_W, STREAK_H = 24, 14                # the streak buffers: half of mip 0
MIP_W, MIP_H = 48, 28                      # mip 0
PACK_W, PACK_H = 64, 40                    # the borrowed target: larger than 2x the streak size
QUADRANT = [(0, 0), (STREAK_W, 0), (0, STREAK_H)]   # scratch A, scratch B, accumulator (texels)

LENGTH = 1.0                               # uCrossLength
FALLOFF = 1.07                             # uCrossFalloff
CHROMATIC = 0.6                            # uCrossChromatic
ARMS = 5
ANGLE = 0.3
CHAIN_REACH = float(TAPS * TAPS * TAPS - 1)


def bilinear(img, w, h, u, v, clamp_edge=True):
    """A GL bilinear fetch at normalised (u, v). clamp_edge is CLAMP_TO_EDGE."""
    if clamp_edge:
        u = min(max(u, 0.5 / w), 1.0 - 0.5 / w)
        v = min(max(v, 0.5 / h), 1.0 - 0.5 / h)
    x = u * w - 0.5
    y = v * h - 0.5
    x0 = int(math.floor(x))
    y0 = int(math.floor(y))
    fx = x - x0
    fy = y - y0

    def texel(i, j):
        i = min(max(i, 0), w - 1)
        j = min(max(j, 0), h - 1)
        return img[j][i]

    c00, c10, c01, c11 = texel(x0, y0), texel(x0 + 1, y0), texel(x0, y0 + 1), texel(x0 + 1, y0 + 1)
    return tuple((c00[k] * (1 - fx) + c10[k] * fx) * (1 - fy) + (c01[k] * (1 - fx) + c11[k] * fx) * fy
                 for k in range(3))


def spectrum(t):
    """crossFilterF.glsl: spectrum()"""
    s = min(max(t, 0.0), 1.0) * 3.0
    return tuple(min(max(c, 0.0), 1.0) for c in (1.5 - abs(s - 0.5), 1.5 - abs(s - 1.5), 1.5 - abs(s - 2.5)))


def streak_pass(sample, direction, pass_scale, texel):
    """crossFilterF.glsl: main(), once per destination texel of the streak buffer.
    `sample(u, v)` is the texture read, with whatever region mapping the layout needs."""
    out = [[None] * STREAK_W for _ in range(STREAK_H)]
    for j in range(STREAK_H):
        for i in range(STREAK_W):
            u0 = (i + 0.5) / STREAK_W       # vary_texcoord0 at this fragment
            v0 = (j + 0.5) / STREAK_H
            accum = [0.0, 0.0, 0.0]
            total_w = 0.0
            for tap in range(TAPS):
                step_index = float(tap) * pass_scale
                offset = (direction[0] * texel[0] * LENGTH * step_index,
                          direction[1] * texel[1] * LENGTH * step_index)
                weight = FALLOFF ** (-step_index)
                tint = (1.0, 1.0, 1.0)
                if CHROMATIC > 0.0:
                    t = min(max(step_index / CHAIN_REACH, 0.0), 1.0)
                    sp = spectrum(t)
                    norm = 3.0 / max(sp[0] + sp[1] + sp[2], 1e-4)
                    sp = tuple(c * norm for c in sp)
                    tint = tuple(1.0 + (sp[k] - 1.0) * CHROMATIC * t for k in range(3))
                s = sample(u0 + offset[0], v0 + offset[1])
                for k in range(3):
                    accum[k] += s[k] * weight * tint[k]
                total_w += weight
            out[j][i] = tuple(a / max(total_w, 1e-4) for a in accum)
    return out


def add_into(dst, src):
    for j in range(STREAK_H):
        for i in range(STREAK_W):
            dst[j][i] = tuple(dst[j][i][k] + src[j][i][k] for k in range(3))


def make_mip0():
    random.seed(7)
    mip0 = [[(0.0, 0.0, 0.0)] * MIP_W for _ in range(MIP_H)]
    for _ in range(6):
        x, y = random.randrange(MIP_W), random.randrange(MIP_H)
        mip0[y][x] = (random.uniform(1, 6), random.uniform(1, 6), random.uniform(1, 6))
    for j in range(MIP_H):
        mip0[j][MIP_W - 1] = (3.0, 2.0, 1.0)     # a bright column on the right edge
    mip0[0][0] = (5.0, 5.0, 5.0)                 # and a bright corner
    return mip0


def arm_direction(arm):
    theta = ANGLE + 2.0 * math.pi * arm / ARMS
    return (math.cos(theta), math.sin(theta))


TEXEL = (1.0 / STREAK_W, 1.0 / STREAK_H)          # uCrossTexel: always the streak buffer's texel
SCALES = [1.0, float(TAPS), float(TAPS * TAPS)]   # uCrossPassScale


def run_dedicated(mip0):
    """Layout A: three targets of their own; the sampler clamps at each texture's edge."""
    accum = None
    for arm in range(ARMS):
        d = arm_direction(arm)
        p0 = streak_pass(lambda u, v: bilinear(mip0, MIP_W, MIP_H, u, v), d, SCALES[0], TEXEL)
        p1 = streak_pass(lambda u, v: bilinear(p0, STREAK_W, STREAK_H, u, v), d, SCALES[1], TEXEL)
        p2 = streak_pass(lambda u, v: bilinear(p1, STREAK_W, STREAK_H, u, v), d, SCALES[2], TEXEL)
        if accum is None:
            accum = [row[:] for row in p2]
        else:
            add_into(accum, p2)
    return accum


class Packed:
    """Layout B: the three buffers as quadrants of one larger texture."""

    def __init__(self):
        # Poisoned outside the quadrants, so any read that strays shows up.
        self.tex = [[(9.0, -9.0, 9.0)] * PACK_W for _ in range(PACK_H)]

    def write(self, quad, img):
        ox, oy = QUADRANT[quad]
        for j in range(STREAK_H):
            for i in range(STREAK_W):
                self.tex[oy + j][ox + i] = img[j][i]

    def region(self, quad):
        """uCrossRegion (origin, scale) and uCrossClamp for a source in this quadrant."""
        ox, oy = QUADRANT[quad]
        origin = (ox / PACK_W, oy / PACK_H)
        scale = (STREAK_W / PACK_W, STREAK_H / PACK_H)
        clamp = (0.5 / STREAK_W, 0.5 / STREAK_H)
        return origin, scale, clamp

    def sampler(self, quad, clamped=True):
        origin, scale, clamp = self.region(quad)

        def sample(u, v):
            if clamped:
                u = min(max(u, clamp[0]), 1.0 - clamp[0])
                v = min(max(v, clamp[1]), 1.0 - clamp[1])
            # The region is interior to the texture, so the sampler's own edge
            # clamp never engages; the raw texture coordinate is what is read.
            return bilinear(self.tex, PACK_W, PACK_H, origin[0] + u * scale[0], origin[1] + v * scale[1],
                            clamp_edge=False)
        return sample


def run_packed(mip0, clamped):
    packed = Packed()
    accum = None
    for arm in range(ARMS):
        d = arm_direction(arm)
        # Pass 0 reads mip 0, a whole texture: identity region, the sampler clamps.
        p0 = streak_pass(lambda u, v: bilinear(mip0, MIP_W, MIP_H, u, v), d, SCALES[0], TEXEL)
        packed.write(0, p0)
        p1 = streak_pass(packed.sampler(0, clamped), d, SCALES[1], TEXEL)
        packed.write(1, p1)
        p2 = streak_pass(packed.sampler(1, clamped), d, SCALES[2], TEXEL)
        if accum is None:
            accum = [row[:] for row in p2]
        else:
            add_into(accum, p2)
        packed.write(2, accum)
    return accum, packed


def max_difference(a, b):
    worst, where = 0.0, None
    for j in range(STREAK_H):
        for i in range(STREAK_W):
            d = max(abs(a[j][i][k] - b[j][i][k]) for k in range(3))
            if d > worst:
                worst, where = d, (i, j)
    return worst, where


def main():
    mip0 = make_mip0()
    dedicated = run_dedicated(mip0)
    packed_accum, packed = run_packed(mip0, clamped=True)
    naive_accum, _ = run_packed(mip0, clamped=False)

    chain_diff, chain_at = max_difference(dedicated, packed_accum)
    naive_diff, naive_at = max_difference(dedicated, naive_accum)

    # colorCorrectF.glsl reads the accumulator at the screen coordinate through
    # the same region mapping and clamp. Compare against a direct read of the
    # dedicated buffer on a grid that includes the frame edges.
    composite_diff = 0.0
    read = packed.sampler(2, clamped=True)
    for v in (0.0, 0.5 / STREAK_H, 0.13, 0.5, 0.87, 1.0 - 0.5 / STREAK_H, 1.0):
        for u in (0.0, 0.5 / STREAK_W, 0.21, 0.5, 0.77, 1.0 - 0.5 / STREAK_W, 1.0):
            a = bilinear(dedicated, STREAK_W, STREAK_H, u, v)
            b = read(u, v)
            composite_diff = max(composite_diff, max(abs(a[k] - b[k]) for k in range(3)))

    peak = max(max(max(c) for c in row) for row in dedicated)
    print('arms=%d taps=%d streak=%dx%d mip0=%dx%d borrowed=%dx%d chromatic=%.1f'
          % (ARMS, TAPS, STREAK_W, STREAK_H, MIP_W, MIP_H, PACK_W, PACK_H, CHROMATIC))
    print('peak accumulator value          : %.4f' % peak)
    print('quadrants with the clamp        : max |dedicated - packed| = %.3e at %s' % (chain_diff, chain_at))
    print('quadrants without the clamp     : max |dedicated - packed| = %.3e at %s' % (naive_diff, naive_at))
    print('composite read of the accumulator: max difference %.3e over a 7x7 grid with edges' % composite_diff)

    ok = chain_diff < 1e-9 and composite_diff < 1e-9
    warned = naive_diff > 1e-3
    if not warned:
        print('note: the unclamped read agreed too, so this input does not exercise the quadrant edge')
    print('RESULT:', 'EXACT' if ok else 'MISMATCH')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
