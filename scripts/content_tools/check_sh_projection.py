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

The weights, directions, basis functions and loop order here mirror shProjectF.glsl. If the
shader's tiling, order or constants change, change them here in the same commit; the whole
point of the mirror is that the two cannot drift apart unnoticed.

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

    print("RESULT: %s" % ("OK" if ok else "MISMATCH"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
