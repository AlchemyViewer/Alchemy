#!/usr/bin/env python3
"""
check_sh_projection.py -- mirror of the reflection probe SH irradiance projection.

The probe manager projects each probe's irradiance cube onto nine spherical-harmonic
coefficients (indra/newview/app_settings/shaders/class1/interface/shProjectF.glsl) and the
deferred shaders reconstruct irradiance from them (evalSHIrradiance in
class3/deferred/reflectionProbeF.glsl). Two things about that pass are easy to get subtly
wrong and hard to see on screen, so they are checked here, statement for statement:

  (a) The two-pass, row-parallel form of the projection (SH_ROW_PARTIAL in shProjectF.glsl
      followed by shProjectReduceF.glsl) must sum to exactly what the single-pass triple loop
      sums, and the partials must survive being stored as 16-bit floats.
  (b) The texel solid-angle weights must sum to 4 pi over the whole cube at every face
      resolution the viewer can integrate (ALProbeSHProjectionRes is clamped to 4 and up).
  (c) Integrating a coarser mip must not move the reconstructed irradiance by more than a
      fraction of a percent: nine coefficients are band-limited to degree two, so an 8x8
      face (the default) is already enough. This is the number that justified lowering the
      integrated resolution from 32 to 8.
  (d) The packed row the projection's last draw writes (shPackTexel in shPackUtilF.glsl,
      from the coefficients each texel holds a part of) must unpack, the way
      evalSHIrradiance unpacks it, to every coefficient at half precision, with the padding
      zero.
  (e) The halves both files convert by hand below GLSL 4.20 (shHalfBits, shHalfValue) must be
      IEEE binary16's: every half reads back as itself, and every single packs as round to
      nearest even does, ties, denormals, overflow and NaN included.

The weights, directions, basis functions and loop order here mirror shProjectF.glsl, and the
packing mirrors shPackUtilF.glsl and evalSHIrradiance. If the shader's tiling, order, layout
or constants change, change them here in the same commit; the whole point of the mirror is
that the two cannot drift apart unnoticed.

Standard library only. Exit status 0 on RESULT: OK, 1 on RESULT: MISMATCH.
"""

import math
import random
import struct
import sys

SH_COEFF_COUNT = 9

# ---------------------------------------------------------------------------------------
# shProjectF.glsl, statement for statement


def area_element(x, y):
    return math.atan2(x * y, math.sqrt(x * x + y * y + 1.0))


def texel_solid_angle(u, v, inv_res):
    x0 = u - inv_res
    y0 = v - inv_res
    x1 = u + inv_res
    y1 = v + inv_res
    return area_element(x1, y1) - area_element(x0, y1) - area_element(x1, y0) + area_element(x0, y0)


def cube_direction(face, u, v):
    if face == 0:
        return (1.0, -v, -u)
    if face == 1:
        return (-1.0, -v, u)
    if face == 2:
        return (u, 1.0, v)
    if face == 3:
        return (u, -1.0, -v)
    if face == 4:
        return (u, -v, 1.0)
    return (-u, -v, -1.0)


def sh_basis(i, d):
    x, y, z = d
    if i == 0:
        return 0.282095
    if i == 1:
        return 0.488603 * y
    if i == 2:
        return 0.488603 * z
    if i == 3:
        return 0.488603 * x
    if i == 4:
        return 1.092548 * x * y
    if i == 5:
        return 1.092548 * y * z
    if i == 6:
        return 0.315392 * (3.0 * z * z - 1.0)
    if i == 7:
        return 1.092548 * x * z
    return 0.546274 * (x * x - y * y)


def normalize(d):
    l = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
    return (d[0] / l, d[1] / l, d[2] / l)


def texel_uv(i, inv_res):
    return (2.0 * (i + 0.5) * inv_res) - 1.0


# ---------------------------------------------------------------------------------------
# reflectionProbeF.glsl evalSHIrradiance, statement for statement


def eval_sh_irradiance(n, coeffs):
    a0, a1, a2 = 1.0, 0.6666667, 0.25
    x, y, z = n
    out = []
    for c in range(3):
        l = [coeffs[i][c] for i in range(SH_COEFF_COUNT)]
        e = (a0 * 0.282095 * l[0]
             + a1 * 0.488603 * (y * l[1] + z * l[2] + x * l[3])
             + a2 * (1.092548 * (x * y * l[4] + y * z * l[5] + x * z * l[7])
                     + 0.315392 * (3.0 * z * z - 1.0) * l[6]
                     + 0.546274 * (x * x - y * y) * l[8]))
        out.append(max(e, 0.0))
    return out


# ---------------------------------------------------------------------------------------
# a synthetic environment: sky gradient, warm ground, a small bright sun, a coloured side lobe

SUN_DIR = normalize((0.5, 0.3, 0.81))
SUN_COS = math.cos(math.radians(2.0))
WINDOW_DIR = normalize((-0.7, 0.6, 0.1))
WINDOW_COS = math.cos(math.radians(12.0))


def environment(d):
    n = normalize(d)
    up = max(-1.0, min(1.0, n[2]))
    if up >= 0.0:
        col = [0.35 + 0.25 * up, 0.45 + 0.3 * up, 0.7 + 0.3 * up]
    else:
        g = 1.0 + 0.3 * up
        col = [0.35 * g, 0.28 * g, 0.2 * g]
    if n[0] * SUN_DIR[0] + n[1] * SUN_DIR[1] + n[2] * SUN_DIR[2] > SUN_COS:
        col = [c + 60.0 for c in col]
    if n[0] * WINDOW_DIR[0] + n[1] * WINDOW_DIR[1] + n[2] * WINDOW_DIR[2] > WINDOW_COS:
        col = [col[0] + 6.0, col[1] + 4.8, col[2] + 3.0]
    return col


def render_cube(res):
    """radiance sampled at texel centres: faces[face][y][x] = (r, g, b)"""
    inv_res = 1.0 / res
    faces = []
    for face in range(6):
        rows = []
        for y in range(res):
            v = texel_uv(y, inv_res)
            row = []
            for x in range(res):
                u = texel_uv(x, inv_res)
                row.append(environment(cube_direction(face, u, v)))
            rows.append(row)
        faces.append(rows)
    return faces


def box_downsample(faces):
    """2x2 box average, what the probe mip chain does between levels"""
    res = len(faces[0]) // 2
    out = []
    for face in faces:
        rows = []
        for y in range(res):
            row = []
            for x in range(res):
                a = face[2 * y][2 * x]
                b = face[2 * y][2 * x + 1]
                c = face[2 * y + 1][2 * x]
                d = face[2 * y + 1][2 * x + 1]
                row.append([(a[k] + b[k] + c[k] + d[k]) * 0.25 for k in range(3)])
            rows.append(row)
        out.append(rows)
    return out


# ---------------------------------------------------------------------------------------
# the projection, single pass and two pass


def texel_term(coef, faces, face, x, y, inv_res):
    u = texel_uv(x, inv_res)
    v = texel_uv(y, inv_res)
    d = cube_direction(face, u, v)
    sa = texel_solid_angle(u, v, inv_res)
    w = sh_basis(coef, normalize(d)) * sa
    rad = faces[face][y][x]
    return (rad[0] * w, rad[1] * w, rad[2] * w)


def project_single_pass(faces):
    """shProjectF.glsl without SH_ROW_PARTIAL: one fragment per coefficient, every texel"""
    res = len(faces[0])
    inv_res = 1.0 / res
    coeffs = []
    for coef in range(SH_COEFF_COUNT):
        s = [0.0, 0.0, 0.0]
        for face in range(6):
            for y in range(res):
                for x in range(res):
                    t = texel_term(coef, faces, face, x, y, inv_res)
                    s[0] += t[0]
                    s[1] += t[1]
                    s[2] += t[2]
        coeffs.append(s)
    return coeffs


def fp16(v):
    return struct.unpack("e", struct.pack("e", v))[0]


def project_two_pass(faces, round_partials=False):
    """shProjectF.glsl with SH_ROW_PARTIAL (one fragment per coefficient and face row) then
    shProjectReduceF.glsl (one fragment per coefficient summing rows 0 .. 6*res-1)"""
    res = len(faces[0])
    inv_res = 1.0 / res
    coeffs = []
    for coef in range(SH_COEFF_COUNT):
        partials = []
        for row in range(6 * res):
            face = row // res
            y = row - face * res
            s = [0.0, 0.0, 0.0]
            for x in range(res):
                t = texel_term(coef, faces, face, x, y, inv_res)
                s[0] += t[0]
                s[1] += t[1]
                s[2] += t[2]
            if round_partials:
                s = [fp16(c) for c in s]
            partials.append(s)
        total = [0.0, 0.0, 0.0]
        for p in partials:
            total[0] += p[0]
            total[1] += p[1]
            total[2] += p[2]
        coeffs.append(total)
    return coeffs


# ---------------------------------------------------------------------------------------
# shPackUtilF.glsl and evalSHIrradiance's unpack, statement for statement

SH_PACKED_TEXELS = 4


def pack_half2x16(a, b):
    lo = struct.unpack("<H", struct.pack("<e", a))[0]
    hi = struct.unpack("<H", struct.pack("<e", b))[0]
    return lo | (hi << 16)


def unpack_half2x16(u):
    lo = struct.unpack("<e", struct.pack("<H", u & 0xFFFF))[0]
    hi = struct.unpack("<e", struct.pack("<H", u >> 16))[0]
    return (lo, hi)


def sh_packed_first(t):
    return (8 * t) // 3


def sh_packed_last(t):
    return min((8 * t + 7) // 3, 8)


def sh_pack_texel(t, sums):
    first = sh_packed_first(t)
    h = []
    for j in range(8):
        k = 8 * t + j
        coef = k // 3
        h.append(sums[coef - first][k - 3 * coef] if coef < 9 else 0.0)
    return (pack_half2x16(h[0], h[1]), pack_half2x16(h[2], h[3]),
            pack_half2x16(h[4], h[5]), pack_half2x16(h[6], h[7]))


def pack_row(coeffs):
    """the last draw: one fragment per texel, its sums the coefficients it holds a part of and
    zero past them, as both shProjectF (single pass) and shProjectReduceF fill them"""
    row = []
    for t in range(SH_PACKED_TEXELS):
        first = sh_packed_first(t)
        count = sh_packed_last(t) - first + 1
        sums = [[0.0, 0.0, 0.0] for _ in range(4)]
        for k in range(count):
            sums[k] = list(coeffs[first + k])
        row.append(sh_pack_texel(t, sums))
    return row


def unpack_row(row):
    t0, t1, t2, t3 = row
    h0 = unpack_half2x16(t0[0])
    h1 = unpack_half2x16(t0[1])
    h2 = unpack_half2x16(t0[2])
    h3 = unpack_half2x16(t0[3])
    h4 = unpack_half2x16(t1[0])
    h5 = unpack_half2x16(t1[1])
    h6 = unpack_half2x16(t1[2])
    h7 = unpack_half2x16(t1[3])
    h8 = unpack_half2x16(t2[0])
    h9 = unpack_half2x16(t2[1])
    h10 = unpack_half2x16(t2[2])
    h11 = unpack_half2x16(t2[3])
    h12 = unpack_half2x16(t3[0])
    h13 = unpack_half2x16(t3[1])
    return [
        (h0[0], h0[1], h1[0]),
        (h1[1], h2[0], h2[1]),
        (h3[0], h3[1], h4[0]),
        (h4[1], h5[0], h5[1]),
        (h6[0], h6[1], h7[0]),
        (h7[1], h8[0], h8[1]),
        (h9[0], h9[1], h10[0]),
        (h10[1], h11[0], h11[1]),
        (h12[0], h12[1], h13[0]),
    ]


def f32_bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def bits_f32(u):
    return struct.unpack("<f", struct.pack("<I", u & 0xFFFFFFFF))[0]


def sh_half_bits(f):
    """shHalfBits, for a single's bits"""
    x = f32_bits(f)
    sign = (x >> 16) & 0x8000
    e = (x >> 23) & 0xFF
    m = x & 0x7FFFFF

    if e == 0xFF:
        return sign | 0x7C00 | (0x200 if m != 0 else 0)

    he = e - 112
    if he >= 31:
        return sign | 0x7C00

    if he <= 0:
        if he < -10:
            return sign
        m |= 0x800000
        shift = 14 - he
        q = m >> shift
        rest = m & ((1 << shift) - 1)
        tie = 1 << (shift - 1)
        q += 1 if (rest > tie or (rest == tie and (q & 1) != 0)) else 0
        return sign | q

    h = (he << 10) | (m >> 13)
    rest = m & 0x1FFF
    h += 1 if (rest > 0x1000 or (rest == 0x1000 and (h & 1) != 0)) else 0
    return sign | h


def sh_half_value(h):
    """shHalfValue, returned as a single"""
    sign = (h & 0x8000) << 16
    e = (h >> 10) & 0x1F
    m = h & 0x3FF
    if e == 0:
        v = bits_f32(f32_bits(float(m) * 5.9604644775390625e-8))
        return -v if sign != 0 else v
    if e == 31:
        return bits_f32(sign | 0x7F800000 | (m << 13))
    return bits_f32(sign | ((e + 112) << 23) | (m << 13))


def ieee_half_bits(f):
    """binary16 of a single, round to nearest even, past the largest half to infinity"""
    if math.isnan(f):
        return None
    try:
        return struct.unpack("<H", struct.pack("<e", f))[0]
    except OverflowError:
        return 0xFC00 if f < 0 else 0x7C00


def half_is_nan(h):
    return (h & 0x7C00) == 0x7C00 and (h & 0x3FF) != 0


# ---------------------------------------------------------------------------------------


def sample_normals():
    normals = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                normals.append(normalize((sx, sy, sz)))
    rng = random.Random(1)
    for _ in range(100):
        normals.append(normalize((rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 1))))
    return [tuple(float(c) for c in n) for n in normals]


def max_irradiance_error(coeffs, reference, normals):
    """largest |E - Eref| over the normals, relative to the brightest reference irradiance"""
    worst = 0.0
    scale = 0.0
    pairs = []
    for n in normals:
        e = eval_sh_irradiance(n, coeffs)
        r = eval_sh_irradiance(n, reference)
        pairs.append((e, r))
        scale = max(scale, max(r))
    for e, r in pairs:
        for k in range(3):
            worst = max(worst, abs(e[k] - r[k]))
    return worst / scale


def main():
    ok = True

    # (b) solid angles cover the sphere exactly
    print("solid-angle coverage (sum over 6*R*R texels vs 4pi):")
    for res in (4, 8, 16, 32):
        inv_res = 1.0 / res
        total = 0.0
        for y in range(res):
            v = texel_uv(y, inv_res)
            for x in range(res):
                total += texel_solid_angle(texel_uv(x, inv_res), v, inv_res)
        total *= 6.0
        rel = abs(total - 4.0 * math.pi) / (4.0 * math.pi)
        flag = "ok" if rel < 1e-6 else "MISMATCH"
        ok = ok and rel < 1e-6
        print("  R=%3d  sum=%.9f  rel err=%.2e  %s" % (res, total, rel, flag))

    # mips of the synthetic environment, 128 down to 4
    faces = render_cube(128)
    mips = {128: faces}
    res = 128
    while res > 4:
        faces = box_downsample(faces)
        res //= 2
        mips[res] = faces

    # (a) the two-pass path equals the single pass, and survives fp16 partials
    print("two-pass row partials vs single pass:")
    for res in (4, 8, 16):
        single = project_single_pass(mips[res])
        rows = project_two_pass(mips[res])
        rows16 = project_two_pass(mips[res], round_partials=True)
        exact = max(abs(rows[i][c] - single[i][c]) for i in range(SH_COEFF_COUNT) for c in range(3))
        scale = max(abs(single[0][c]) for c in range(3))
        half = max(abs(rows16[i][c] - single[i][c]) for i in range(SH_COEFF_COUNT) for c in range(3)) / scale
        flag = "ok" if (exact < 1e-9 and half < 1e-3) else "MISMATCH"
        ok = ok and exact < 1e-9 and half < 1e-3
        print("  R=%3d  max abs diff=%.2e  with fp16 partials rel=%.2e  %s" % (res, exact, half, flag))

    # (c) integrating coarser mips barely moves the reconstructed irradiance
    print("irradiance error of coarser integration mips vs the 128 face:")
    normals = sample_normals()
    reference = project_single_pass(mips[128])
    gates = {8: 0.01, 16: 0.005}
    for res in (64, 32, 16, 8, 4):
        err = max_irradiance_error(project_single_pass(mips[res]), reference, normals)
        gate = gates.get(res)
        if gate is None:
            flag = ""
        elif err < gate:
            flag = "ok (gate %.1f%%)" % (gate * 100.0)
        else:
            flag = "MISMATCH (gate %.1f%%)" % (gate * 100.0)
            ok = False
        print("  R=%3d  texels/coef=%6d  max rel err=%.3f%%  %s" % (res, 6 * res * res, err * 100.0, flag))

    # (d) the packed row unpacks to every coefficient at half precision, padding zero
    print("packed coefficient row (4 RGBA32UI texels) round trip:")
    rng = random.Random(2)
    sets = [project_single_pass(mips[8])]
    for _ in range(200):
        sets.append([[rng.uniform(-100.0, 100.0) for _ in range(3)] for _ in range(SH_COEFF_COUNT)])
    worst = 0
    for coeffs in sets:
        row = pack_row(coeffs)
        back = unpack_row(row)
        for i in range(SH_COEFF_COUNT):
            for c in range(3):
                if back[i][c] != fp16(coeffs[i][c]):
                    worst += 1
        if unpack_half2x16(row[3][1])[1] != 0.0 or row[3][2] != 0 or row[3][3] != 0:
            worst += 1
    flag = "ok" if worst == 0 else "MISMATCH"
    ok = ok and worst == 0
    print("  %d coefficient sets  mismatched values=%d  %s" % (len(sets), worst, flag))

    # (e) the hand-written halves below GLSL 4.20 are binary16's
    print("hand-packed halves (below GLSL 4.20) vs IEEE binary16:")
    bad = 0
    for h in range(0x10000):
        v = sh_half_value(h)
        if half_is_nan(h):
            bad += 0 if (math.isnan(v) and half_is_nan(sh_half_bits(v))) else 1
            continue
        ref = struct.unpack("<e", struct.pack("<H", h))[0]
        if v != ref or f32_bits(v) >> 31 != h >> 15 or sh_half_bits(v) != h:
            bad += 1
    singles = []
    for h in range(0x7C00 - 1):
        a = struct.unpack("<e", struct.pack("<H", h))[0]
        b = struct.unpack("<e", struct.pack("<H", h + 1))[0]
        tie = (a + b) * 0.5
        singles += [tie, -tie, bits_f32(f32_bits(tie) + 1), bits_f32(f32_bits(tie) - 1)]
    rng = random.Random(3)
    for _ in range(100000):
        singles.append(bits_f32(rng.getrandbits(32)))
    singles += [65504.0, 65519.99, 65520.0, 1e30, -1e30, 2.0 ** -25, 2.0 ** -26, 1.5 * 2.0 ** -25,
                float("inf"), float("-inf"), 0.0, -0.0]
    for f in singles:
        f = bits_f32(f32_bits(f))
        ref = ieee_half_bits(f)
        got = sh_half_bits(f)
        if ref is None:
            bad += 0 if half_is_nan(got) else 1
        elif got != ref:
            bad += 1
    flag = "ok" if bad == 0 else "MISMATCH"
    ok = ok and bad == 0
    print("  65536 halves, %d singles  mismatched=%d  %s" % (len(singles), bad, flag))

    print("RESULT: %s" % ("OK" if ok else "MISMATCH"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
