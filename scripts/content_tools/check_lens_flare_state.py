"""Offline model of the lens flare state pass (lensFlareStateF.glsl).

Model A -- the spatial coverage kernel: taps on a unit disk with centre-heavy
weights, driven across edges and poles of various widths.
Model B -- the temporal filter: fade-in/out time constants, a slew cap
relative to a running reference luminance, and adaptive damping keyed off
direction reversals of the raw target.  Replayed at 30/60/144 fps.

Constants chosen here are transcribed into the shader statement for
statement; keep the two in step.  Pure Python on purpose (no numpy).

    python check_lens_flare_state.py          # verify the chosen constants
    python check_lens_flare_state.py --grid   # compare tap patterns and weights
"""
import math
import sys

GOLDEN_ANGLE = 2.39996322972865332

# The ad-hoc Poisson disk the original shader used (for comparison only).
POISSON = [
    (0.0, 0.0), (-0.326, -0.406), (-0.840, -0.074), (-0.196, 0.457),
    (0.498, 0.336), (0.106, -0.747), (0.736, -0.290), (0.423, 0.767),
    (-0.621, 0.572), (0.890, 0.156), (-0.453, -0.780), (0.215, -0.945),
    (-0.928, 0.326), (0.673, -0.685), (-0.158, 0.892), (0.952, 0.548),
    (-0.756, -0.518), (0.347, -0.412), (-0.089, -0.290), (0.612, 0.710),
    (-0.544, 0.815), (0.818, -0.543), (-0.987, -0.321), (0.145, 0.623),
    (-0.412, 0.178), (0.567, -0.098), (-0.278, -0.654), (0.934, 0.389),
    (-0.703, 0.112), (0.056, -0.512), (0.289, 0.934), (-0.867, 0.745),
]


def vogel(n):
    """Fermat spiral: uniform area density, no angular clustering."""
    return [(math.sqrt((i + 0.5) / n) * math.cos(i * GOLDEN_ANGLE),
             math.sqrt((i + 0.5) / n) * math.sin(i * GOLDEN_ANGLE)) for i in range(n)]


def rings(spec):
    """spec = [(radius, count), ...]; a count of 1 is the centre tap."""
    out = []
    for ring, (r, n) in enumerate(spec):
        if n == 1:
            out.append((0.0, 0.0))
            continue
        for i in range(n):
            a = 2.0 * math.pi * i / n + ring * 0.5   # stagger successive rings
            out.append((r * math.cos(a), r * math.sin(a)))
    return out


PATTERNS = {
    "poisson32": POISSON,
    "vogel32": vogel(32),
    "vogel48": vogel(48),
    "rings1-6-10-15": rings([(0.0, 1), (0.3, 6), (0.6, 10), (0.9, 15)]),
    "rings1-7-13-19": rings([(0.0, 1), (0.28, 7), (0.58, 13), (0.88, 19)]),
}

FAIL = []


def check(name, ok, detail):
    print(("  ok   " if ok else "  FAIL ") + name + ": " + detail)
    if not ok:
        FAIL.append(name)


def rotate(x, y, a):
    c, s = math.cos(a), math.sin(a)
    return (c * x - s * y, s * x + c * y)


# --------------------------------------------------------------------------
# Model A: spatial kernel
# --------------------------------------------------------------------------
PATTERN = "vogel48"
K_COVER = 1.0   # coverage weight  w = exp(-K_COVER * r^2)
K_COLOR = 8.0   # colour weight    w = exp(-K_COLOR * r^2)


def kernel(taps, occluded, angle=0.0, lum=None, k_cover=K_COVER, k_color=K_COLOR):
    """Returns (coverage, mean_lum_of_unoccluded_taps)."""
    ws = vs = 0.0
    cs = cw = 0.0
    for (tx, ty) in taps:
        x, y = rotate(tx, ty, angle)
        r2 = x * x + y * y
        w = math.exp(-k_cover * r2)
        sky = 0.0 if occluded(x, y) else 1.0
        ws += w
        vs += w * sky
        if lum is not None:
            wc = math.exp(-k_color * r2) * sky
            cs += wc * lum(x, y)
            cw += wc
    return vs / ws, (cs / cw if cw > 0 else 0.0)


def sun_lum(x, y, core=20.0, sky=1.0):
    """Soft disc: bright core to r=0.5, fading to sky at the quad edge r=1."""
    r = math.sqrt(x * x + y * y)
    t = min(max((r - 0.5) / 0.5, 0.0), 1.0)
    t = t * t * (3.0 - 2.0 * t)
    return core + (sky - core) * t


def spatial_stats(taps, k_cover):
    """Numbers that decide between patterns; see model_a for the criteria."""
    angles = [i * GOLDEN_ANGLE for i in range(360)]
    vals = [kernel(taps, lambda x, y: x < 0.0, a, k_cover=k_cover)[0] for a in angles]
    mean = sum(vals) / len(vals)
    std = math.sqrt(sum((v - mean) ** 2 for v in vals) / len(vals))
    # consecutive-frame change of a static half occlusion under golden rotation
    jump = max(abs(a - b) for a, b in zip(vals, vals[1:]))
    wsum = sum(math.exp(-k_cover * (x * x + y * y)) for x, y in taps)
    share = max(math.exp(-k_cover * (x * x + y * y)) for x, y in taps) / wsum
    # largest per-frame step for an edge advancing 0.01 r per frame (fixed pattern)
    sweep = [kernel(taps, lambda x, y, x0=x0: x < x0, k_cover=k_cover)[0]
             for x0 in [i * 0.01 - 1.2 for i in range(241)]]
    edge_step = max(abs(a - b) for a, b in zip(sweep, sweep[1:]))
    poles = {}
    for W in (0.25, 0.5, 1.0, 2.0):
        worst = 1.0
        for i in range(201):
            x0 = i * 0.02 - 2.0
            c = sum(kernel(taps, lambda x, y, x0=x0, W=W: abs(x - x0) < W * 0.5,
                           n * GOLDEN_ANGLE, k_cover=k_cover)[0] for n in range(8)) / 8.0
            worst = min(worst, c)
        poles[W] = worst
    return dict(mean=mean, std=std, jump=jump, share=share, edge_step=edge_step, poles=poles)


def grid():
    print("pattern          k   mean   std   jump  share  edge  pole.25 pole.5 pole1  pole2")
    for name, taps in PATTERNS.items():
        for k in (1.0, 1.5, 2.0):
            s = spatial_stats(taps, k)
            print("%-15s %3.1f  %.3f  %.3f  %.3f  %.3f  %.3f  %.3f   %.3f  %.3f  %.3f" % (
                name, k, s["mean"], s["std"], s["jump"], s["share"], s["edge_step"],
                s["poles"][0.25], s["poles"][0.5], s["poles"][1.0], s["poles"][2.0]))


def model_a():
    taps = PATTERNS[PATTERN]
    print("Model A: spatial kernel (%s, K_COVER=%g, K_COLOR=%g)" % (PATTERN, K_COVER, K_COLOR))
    s = spatial_stats(taps, K_COVER)
    check("half-plane mean", abs(s["mean"] - 0.5) < 0.03, "mean %.3f (ideal 0.5)" % s["mean"])
    check("half-plane bias", s["std"] < 0.03, "std %.3f over edge angles" % s["std"])
    check("rotation noise", s["jump"] < 0.1,
          "max frame-to-frame change %.3f under golden rotation (step threshold 0.1)" % s["jump"])
    check("max tap share", s["share"] < 0.1, "%.3f of weight" % s["share"])
    check("edge step", s["edge_step"] < 0.1, "%.3f per 0.01 r of edge travel" % s["edge_step"])
    for W, lo, hi in [(0.25, 0.75, 1.0), (0.5, 0.55, 1.0), (1.0, 0.2, 0.75), (2.0, 0.0, 0.05)]:
        check("pole width %.2f" % W, lo <= s["poles"][W] <= hi,
              "min coverage %.3f (want %.2f..%.2f)" % (s["poles"][W], lo, hi))
    full = kernel(taps, lambda x, y: False, 0.0, sun_lum)[1]
    half = kernel(taps, lambda x, y: x < 0.0, 0.0, sun_lum)[1]
    check("brightness parity", full > 0.95 * 20.0 and half > 0.9 * 20.0,
          "weighted mean %.2f, half-occluded %.2f, centre texel 20.00" % (full, half))


# --------------------------------------------------------------------------
# Model B: temporal filter
# --------------------------------------------------------------------------
class P:
    fade_time = 0.35     # RenderLensFlareFadeTime (seconds)
    tau_in_mul = 1.0 / 3.0   # tau_in  = fade_time * tau_in_mul
    tau_out_mul = 0.2        # tau_out = fade_time * tau_out_mul
    slew_in = 1.0        # x / fade_time: max rise per second, relative to L_ref
    slew_out = 1.0 / 0.6  # x / fade_time: max fall per second
    tau_ref = 2.0        # running-max reference decay (s)
    step_thresh = 0.1    # a raw-target move counts as a step above this (rel. L_ref)
    gain = 0.35          # instability added per reversal
    tau_I = 1.0          # instability decay (s)
    I0 = 0.30            # dead zone: below this, no damping
    k = 20.0             # tau multiplier slope above the dead zone


def sign(v):
    return 1.0 if v > 0 else (-1.0 if v < 0 else 0.0)


def zero_state():
    return dict(drive=0.0, packed=0.0, prev_raw=0.0, L_ref=0.0)


def lit_state():
    return dict(drive=1.0, packed=0.0, prev_raw=1.0, L_ref=1.0)


def step(st, Lt, dt, p=P):
    """One frame of the state pass, luminance only (RGB follows Lp->Lt)."""
    dt = min(max(dt, 0.0), 0.25)
    Lp = st["drive"]
    L_ref = max(st["L_ref"] * math.exp(-dt / p.tau_ref), Lt, Lp)
    packed = st["packed"]
    I = abs(packed)
    s_prev = sign(packed)
    delta = (Lt - st["prev_raw"]) / max(L_ref, 1e-6)
    s_now = sign(delta) if abs(delta) > p.step_thresh else 0.0
    reversal = (s_now != 0.0) and (s_prev != 0.0) and (s_now != s_prev)
    I = I * math.exp(-dt / p.tau_I) + (p.gain if reversal else 0.0)
    I = min(I, 1.0)
    s_store = s_now if s_now != 0.0 else s_prev
    packed = s_store * max(I, 1e-3) if s_store != 0.0 else 0.0
    rising = Lt > Lp
    tau = p.fade_time * (p.tau_in_mul if rising else p.tau_out_mul)
    tau *= 1.0 + p.k * max(I - p.I0, 0.0)
    alpha = 1.0 - math.exp(-dt / tau)
    slew = (p.slew_in if rising else p.slew_out) / p.fade_time
    cap = slew * dt * L_ref
    diff = abs(Lt - Lp)
    if diff > 1e-9:
        alpha = min(alpha, cap / diff)
    drive = Lp + (Lt - Lp) * alpha
    return dict(drive=drive, packed=packed, prev_raw=Lt, L_ref=L_ref)


def run(target_fn, fps, seconds, p=P, start=None):
    st = start or zero_state()
    dt = 1.0 / fps
    out = []
    for i in range(int(seconds * fps)):
        t = i * dt
        st = step(st, target_fn(t), dt, p)
        out.append((t + dt, st["drive"], abs(st["packed"])))
    return out


def crossing(series, level, rising, after=0.0):
    for t, v, _ in series:
        if t < after:
            continue
        if (v >= level) if rising else (v <= level):
            return t
    return None


def window(series, t0, t1):
    return [v for t, v, _ in series if t0 <= t < t1]


def square(f):
    return lambda t: 1.0 if int(t * 2 * f) % 2 == 0 else 0.0


def model_b(p=P):
    ft = p.fade_time
    print("Model B: temporal filter (fade_time=%.2f tau_in=%.3f tau_out=%.3f slew_in=%.2f/s "
          "slew_out=%.2f/s gain=%.2f tau_I=%.1f I0=%.2f k=%.0f)" % (
              ft, ft * p.tau_in_mul, ft * p.tau_out_mul, p.slew_in / ft, p.slew_out / ft,
              p.gain, p.tau_I, p.I0, p.k))
    stepfn = lambda t: 1.0 if 0.5 <= t < 3.0 else 0.0
    ref = None
    for fps in (30, 60, 144):
        s = run(stepfn, fps, 5.0, p)
        rise = crossing(s, 0.9, True) - crossing(s, 0.1, True)
        fall = crossing(s, 0.1, False, 3.0) - crossing(s, 0.9, False, 3.0)
        over = max(v for t, v, _ in s if t < 3.0) - 1.0
        dmax = max(abs(b[1] - a[1]) for a, b in zip(s, s[1:]))
        print("    %3d fps: rise10-90 %.3fs fall90-10 %.3fs overshoot %.4f max frame delta %.4f" %
              (fps, rise, fall, over, dmax))
        if fps == 60:
            check("rise time", abs(rise - ft) <= 0.2 * ft, "%.3fs vs fade_time %.2fs" % (rise, ft))
            check("fall time", abs(fall - 0.6 * ft) <= 0.2 * 0.6 * ft + 0.02,
                  "%.3fs vs 0.6*fade_time %.3fs" % (fall, 0.6 * ft))
            check("no overshoot", over <= 1e-6, "%.5f" % over)
            check("frame delta cap", dmax <= p.slew_out / ft / 60.0 + 1e-6,
                  "%.4f per frame at 60 fps" % dmax)
        if ref is None:
            ref = s
        else:
            coarse, fine = (ref, s) if len(ref) < len(s) else (s, ref)
            worst = 0.0
            for t, v, _ in coarse:
                j = min(range(len(fine)), key=lambda k: abs(fine[k][0] - t))
                worst = max(worst, abs(fine[j][1] - v))
            check("frame-rate independence %d fps" % fps, worst < 0.03,
                  "max deviation %.3f vs 30 fps" % worst)
    # Square waves from a lit state: residual swing over the last second of four.
    for f, limit in ((1.0, 0.5), (2.0, 0.2), (3.0, 0.15), (5.0, 0.15), (8.0, 0.15)):
        s = run(square(f), 60, 4.0, p, start=lit_state())
        last = window(s, 3.0, 4.0)
        first = window(s, 0.0, 1.0)
        pp = max(last) - min(last)
        check("%.0f Hz residual" % f, pp <= limit,
              "p-p %.3f in the last second (limit %.2f); first second p-p %.3f, mean %.2f" % (
                  pp, limit, max(first) - min(first), sum(last) / len(last)))
    # Worst-case flash count: full-scale cycles per second the filter can pass.
    cyc = 1.0 / (ft * (1.0 / p.slew_in + 1.0 / p.slew_out))
    check("cycle bound", cyc < 3.0, "at most %.2f full off-on-off cycles per second" % cyc)
    blip = lambda t: 0.0 if 1.0 <= t < 1.1 else 1.0
    s = run(blip, 60, 3.0, p, start=lit_state())
    dip = 1.0 - min(v for t, v, _ in s)
    check("blip depth", dip < 0.5, "dip %.3f for a 100 ms occlusion" % dip)
    hidden = lambda t: 0.0 if t < 5.0 else 1.0
    s = run(hidden, 60, 8.0, p, start=lit_state())
    rise = crossing(s, 0.9, True, 5.0) - crossing(s, 0.1, True, 5.0)
    check("isolated reveal", abs(rise - ft) <= 0.25 * ft, "rise %.3fs vs %.2fs" % (rise, ft))
    mixed = lambda t: square(3.0)(t) if t < 2.0 else 1.0
    s = run(mixed, 60, 6.0, p, start=lit_state())
    t90 = crossing(s, 0.9, True, 2.0)
    check("recovery after strobe", t90 is not None and t90 - 2.0 < 2.5,
          "reaches 0.9 %.2fs after the strobe stops" % ((t90 or 99) - 2.0))
    dim = lambda t: max(1.0 - 0.3 * t, 0.0)
    s = run(dim, 60, 3.0, p, start=lit_state())
    lag = max(abs(v - dim(t)) for t, v, _ in s)
    check("slow dimming tracks", lag < 0.1, "max lag %.3f" % lag)
    # Noise robustness: a static half occlusion jittered +-0.03 must not
    # trigger the reversal detector or wobble the drive.
    noisy = lambda t: 0.5 + 0.03 * math.sin(t * 97.0) * math.cos(t * 31.0)
    s = run(noisy, 60, 4.0, p, start=lit_state())
    last = window(s, 3.0, 4.0)
    imax = max(i for t, v, i in s)
    check("noise immunity", max(last) - min(last) < 0.02 and imax < p.I0,
          "drive p-p %.4f, instability peak %.3f" % (max(last) - min(last), imax))


if __name__ == "__main__":
    if "--grid" in sys.argv:
        grid()
        sys.exit(0)
    model_a()
    model_b()
    print("%d failure(s)" % len(FAIL))
    sys.exit(1 if FAIL else 0)
