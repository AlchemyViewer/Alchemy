"""Offline mirror of the lens flare state pass, lensFlareStateF.glsl.

The shader is the source of truth; this script mirrors its kernel and its
temporal filter statement for statement, is the tool that chose the
constants, and is the only regression test they have. Change a constant in
the shader first, then make the copy below agree and re-run this.

Model A -- the spatial kernel: a fixed Fermat spiral of taps, each gated on
its own (an occluded tap contributes nothing, an unoccluded one the texel's
overbright colour), weighted exp(-K_TAP r^2); driven across edges, poles and
a random needle mask with a still and a drifting camera.
Model B -- the temporal filter: fade-in/out time constants derived from
FadeTime, a slew cap relative to a running reference luminance, and adaptive
damping keyed off direction reversals of the raw target, detected as
displacement from an anchor so the verdict does not depend on frame rate.
Replayed at 30/60/144 fps.  Pure Python on purpose (no numpy).

    python check_lens_flare_state.py          # verify the chosen constants
    python check_lens_flare_state.py --grid   # compare tap counts and patterns
"""
import math
import random
import sys

GOLDEN_ANGLE = 2.39996322972865332
GATE = 2.0           # shader GATE: luminance below which a texel is not sun
K_TAP = 8.0          # shader K_TAP
TAP_COUNT = 256      # shader TAP_COUNT
PX = 46.0            # probe radius in pixels at the default Size, 60 deg FOV, 1080p

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


PATTERNS = {
    "poisson32": POISSON,
    "vogel48": vogel(48),
    "vogel128": vogel(128),
    "vogel256": vogel(256),
    "vogel512": vogel(512),
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
def sun_lum(x, y, core=20.0, sky=1.0):
    """Soft disc: bright core to r=0.5, fading to sky at the quad edge r=1."""
    r = math.sqrt(x * x + y * y)
    t = min(max((r - 0.5) / 0.5, 0.0), 1.0)
    t = t * t * (3.0 - 2.0 * t)
    return core + (sky - core) * t


def energy(taps, occluded, angle=0.0, k=K_TAP, offset=(0.0, 0.0)):
    """The shader's estimate: weighted mean over taps of sky_i x overbright_i.
    `angle` rotates the pattern (equivalently, the edge); `offset` shifts the
    occluder under the sun (a drifting camera). Returns (drive, peak)."""
    es = ws = 0.0
    peak = 0.0
    for tx, ty in taps:
        x, y = rotate(tx, ty, angle)
        w = math.exp(-k * (x * x + y * y))
        ws += w
        if not occluded(x + offset[0], y + offset[1]):
            lum = sun_lum(x, y)
            over = lum * max(lum - GATE, 0.0) / max(lum, 1e-4)
            es += w * over
            peak = max(peak, over)
    return es / ws, peak


def spatial_stats(taps, k=K_TAP):
    full = energy(taps, lambda x, y: False, k=k)[0]
    angles = [i * GOLDEN_ANGLE for i in range(360)]
    vals = [energy(taps, lambda x, y: x < 0.0, a, k)[0] / full for a in angles]
    mean = sum(vals) / len(vals)
    std = math.sqrt(sum((v - mean) ** 2 for v in vals) / len(vals))
    wsum = sum(math.exp(-k * (x * x + y * y)) for x, y in taps)
    share = max(math.exp(-k * (x * x + y * y)) for x, y in taps) / wsum
    sweep = [energy(taps, lambda x, y, x0=x0: x < x0, k=k)[0] / full
             for x0 in [i * 0.01 - 1.2 for i in range(241)]]
    edge_step = max(abs(a - b) for a, b in zip(sweep, sweep[1:]))
    poles = {}
    for W in (0.25, 0.5, 1.0, 2.0):
        worst = 1.0
        for i in range(201):
            x0 = i * 0.02 - 2.0
            worst = min(worst, energy(taps, lambda x, y, x0=x0, W=W: abs(x - x0) < W * 0.5, k=k)[0] / full)
        poles[W] = worst
    return dict(full=full, mean=mean, std=std, worst=max(abs(v - 0.5) for v in vals),
                share=share, edge_step=edge_step, poles=poles)


class NeedleMask:
    """Random thin segments on a grid: alpha-masked foliage over the sun."""
    def __init__(self, n_needles, width_px, res=0.02, extent=3.0, seed=7):
        rng = random.Random(seed)
        self.res = res
        self.extent = extent
        n = int(2 * extent / res)
        self.n = n
        grid = bytearray(n * n)
        w = width_px / PX
        rr = int(w / 2 / res) + 1
        for _ in range(n_needles):
            x0 = rng.uniform(-extent, extent)
            y0 = rng.uniform(-extent, extent)
            a = rng.uniform(0, math.pi)
            L = rng.uniform(0.4, 1.2)
            dx, dy = math.cos(a), math.sin(a)
            for s in range(int(L / res) + 1):
                cx = x0 + dx * s * res
                cy = y0 + dy * s * res
                ci = int((cx + extent) / res)
                cj = int((cy + extent) / res)
                for i in range(max(ci - rr, 0), min(ci + rr + 1, n)):
                    for j in range(max(cj - rr, 0), min(cj + rr + 1, n)):
                        if ((i - ci) * res) ** 2 + ((j - cj) * res) ** 2 <= (w / 2) ** 2:
                            grid[i * n + j] = 1
        self.grid = grid

    def occluded(self, x, y):
        i = int((x + self.extent) / self.res)
        j = int((y + self.extent) / self.res)
        if i < 0 or i >= self.n or j < 0 or j >= self.n:
            return False
        return self.grid[i * self.n + j] == 1


def grid():
    print("pattern          full   mean   std   worst  share  edge  pole.25 pole.5 pole1  pole2")
    for name, taps in PATTERNS.items():
        s = spatial_stats(taps)
        print("%-15s %5.2f  %.3f  %.3f  %.3f  %.3f  %.3f  %.3f   %.3f  %.3f  %.3f" % (
            name, s["full"], s["mean"], s["std"], s["worst"], s["share"], s["edge_step"],
            s["poles"][0.25], s["poles"][0.5], s["poles"][1.0], s["poles"][2.0]))


def delta_std(v):
    d = [b - a for a, b in zip(v, v[1:])]
    mu = sum(d) / len(d)
    return math.sqrt(sum((x - mu) ** 2 for x in d) / len(d))


def model_a():
    taps = vogel(TAP_COUNT)
    print("Model A: spatial kernel (%d fixed taps, K_TAP=%g, GATE=%g)" % (TAP_COUNT, K_TAP, GATE))
    s = spatial_stats(taps)
    check("brightness parity", s["full"] > 0.95 * (20.0 - GATE),
          "unoccluded drive %.2f vs centre texel overbright %.2f" % (s["full"], 20.0 - GATE))
    check("half-plane mean", abs(s["mean"] - 0.5) < 0.03, "mean %.3f (ideal 0.5)" % s["mean"])
    check("half-plane bias", s["std"] < 0.03 and s["worst"] < 0.05,
          "std %.3f, worst %.3f from half over edge angles (the shader header quotes both)" % (
              s["std"], s["worst"]))
    check("max tap share", s["share"] < 0.05, "%.3f of weight" % s["share"])
    check("edge step", s["edge_step"] < 0.1, "%.3f per 0.01 r of edge travel" % s["edge_step"])
    for W, lo, hi in [(0.25, 0.5, 1.0), (0.5, 0.25, 1.0), (1.0, 0.0, 0.05), (2.0, 0.0, 0.01)]:
        check("pole width %.2f" % W, lo <= s["poles"][W] <= hi,
              "min drive %.3f (want %.2f..%.2f)" % (s["poles"][W], lo, hi))
    # Alpha-masked foliage: a still camera must give a constant estimate, and
    # a drifting one a smooth signal the filter passes without shimmer.
    mask = NeedleMask(n_needles=150, width_px=2.5)
    full = s["full"]
    still = [energy(taps, mask.occluded)[0] / full for _ in range(30)]
    check("needles, still camera", max(still) - min(still) == 0.0,
          "coverage %.2f of full, frame-to-frame change %.4f" % (still[0], max(still) - min(still)))
    for fps, drift_px, limit in ((60, 0.5, 0.01), (30, 1.0, 0.015)):
        raw = []
        for i in range(int(2.0 * fps)):
            off = (drift_px * i / PX, 0.37 * drift_px * i / PX)
            d, peak = energy(taps, mask.occluded, offset=off)
            raw.append((d / full, peak / full))
        st = lit_state()
        out = []
        for Lt, L_full in raw:
            st = step(st, Lt, L_full, 1.0 / fps)
            out.append(st["drive"])
        rd = delta_std([r[0] for r in raw])
        fd = delta_std(out[fps // 2:])
        check("needles, drifting camera at %d fps" % fps, rd < 0.06 and fd < limit,
              "raw change %.4f, filtered change %.4f per frame (limit %.3f)" % (rd, fd, limit))


# --------------------------------------------------------------------------
# Model B: temporal filter (mirror of the shader's constants and statements)
# --------------------------------------------------------------------------
class P:
    fade_time = 0.35     # RenderLensFlareFadeTime (seconds); uLensFlareFadeTime
    tau_in_mul = 1.0 / 3.0   # TAU_IN_MUL
    tau_out_mul = 0.2        # TAU_OUT_MUL
    slew_in = 1.0        # SLEW_IN_MUL: / fade_time, per second, relative to L_ref
    slew_out = 1.0 / 0.6  # SLEW_OUT_MUL
    dt_max = 0.25        # DT_MAX
    tau_ref = 2.0        # TAU_REF: running-max reference decay (s)
    step_thresh = 0.1    # STEP_THRESH: displacement from the anchor, rel. the unoccluded sun
    gain = 0.35          # GAIN: instability added per reversal
    tau_I = 1.0          # TAU_I: instability decay (s)
    I0 = 0.30            # I_DEADZONE
    k = 20.0             # K_DAMP
    snap_floor = 1e-4    # SNAP_FLOOR


def sign(v):
    return 1.0 if v > 0 else (-1.0 if v < 0 else 0.0)


def zero_state():
    return dict(drive=0.0, packed=0.0, raw=0.0, L_ref=0.0, anchor=0.0)


def lit_state():
    return dict(drive=1.0, packed=0.0, raw=1.0, L_ref=1.0, anchor=1.0)


def step(st, Lt, L_full, dt, p=P):
    """One frame of the state pass, luminance only (RGB follows Lp->Lt).
    Lt is the raw target (visible overbright sun x edge fade); L_full is the
    brightest unoccluded tap, the step detector's yardstick."""
    dt = min(max(dt, 0.0), p.dt_max)
    fade = max(p.fade_time, 0.05)
    Lp = st["drive"]
    L_ref = max(st["L_ref"] * math.exp(-dt / p.tau_ref), Lt, Lp)
    packed = st["packed"]
    I = abs(packed)
    s_prev = sign(packed)
    anchor = st["anchor"]
    delta = (Lt - anchor) / max(max(L_full, L_ref), 1e-6)
    s_now = sign(delta) if abs(delta) > p.step_thresh else 0.0
    if s_now != 0.0:
        anchor = Lt
    reversal = (s_now != 0.0) and (s_prev != 0.0) and (s_now != s_prev)
    I = min(I * math.exp(-dt / p.tau_I) + (p.gain if reversal else 0.0), 1.0)
    s_store = s_now if s_now != 0.0 else s_prev
    packed = s_store * max(I, 1e-3) if s_store != 0.0 else 0.0
    rising = Lt > Lp
    tau = fade * (p.tau_in_mul if rising else p.tau_out_mul)
    tau *= 1.0 + p.k * max(I - p.I0, 0.0)
    alpha = 1.0 - math.exp(-dt / tau)
    slew = (p.slew_in if rising else p.slew_out) / fade
    cap = slew * dt * L_ref
    diff = abs(Lt - Lp)
    if diff > 1e-9:
        alpha = min(alpha, cap / diff)
    drive = max(Lp + (Lt - Lp) * alpha, 0.0)
    if Lt <= 0.0 and drive < p.snap_floor:
        drive = 0.0
    return dict(drive=drive, packed=packed, raw=Lt, L_ref=L_ref, anchor=anchor)


def run(target_fn, fps, seconds, p=P, start=None, full_fn=None):
    """Replay a target sequence. Unless full_fn is given, the sun is taken as
    fully bright whenever the target is nonzero (partial occlusion of a bright
    sun) and dark when it is zero (hidden, or behind the camera)."""
    st = start or zero_state()
    dt = 1.0 / fps
    out = []
    for i in range(int(seconds * fps)):
        t = i * dt
        Lt = target_fn(t)
        L_full = full_fn(t) if full_fn else (1.0 if Lt > 0.0 else 0.0)
        st = step(st, Lt, L_full, dt, p)
        out.append((t + dt, st["drive"], abs(st["packed"])))
    return out


def crossing(series, level, rising, after=0.0):
    for t, v, _ in series:
        if t < after:
            continue
        if (v >= level) if rising else (v <= level):
            return t
    return None


def span(series, hi, lo, rising, after=0.0):
    """10-90% style interval between two level crossings, or None."""
    a = crossing(series, lo if rising else hi, rising, after)
    b = crossing(series, hi if rising else lo, rising, after)
    return None if a is None or b is None else b - a


def window(series, t0, t1):
    return [v for t, v, _ in series if t0 <= t < t1]


def square(f):
    return lambda t: 1.0 if int(t * 2 * f) % 2 == 0 else 0.0


def ramped_square(f, ramp):
    """A square wave whose edges are linear ramps of `ramp` seconds: a fence
    post crossing the probe over several frames rather than one."""
    period = 1.0 / f
    half = period / 2.0

    def target(t):
        u = t % period
        if u < ramp:
            return 1.0 - u / ramp
        if u < half:
            return 0.0
        if u < half + ramp:
            return (u - half) / ramp
        return 1.0
    return target


def pole_series(W, fps, seconds):
    """Per-frame drive fraction with a pole of width W disc radii over the
    sun, jittered by a sub-pixel camera wobble the way a held camera is."""
    taps = vogel(TAP_COUNT)
    full = energy(taps, lambda x, y: False)[0]
    out = []
    for i in range(int(seconds * fps)):
        wob = 0.3 / PX * math.sin(i * 0.7)
        out.append(energy(taps, lambda x, y, W=W: abs(x) < W * 0.5, offset=(wob, 0.0))[0] / full)
    return out


def fmt(v):
    return "none" if v is None else "%.3fs" % v


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
        rise = span(s, 0.9, 0.1, True)
        fall = span(s, 0.9, 0.1, False, 3.0)
        over = max(v for t, v, _ in s if t < 3.0) - 1.0
        dmax = max(abs(b[1] - a[1]) for a, b in zip(s, s[1:]))
        print("    %3d fps: rise10-90 %s fall90-10 %s overshoot %.4f max frame delta %.4f" %
              (fps, fmt(rise), fmt(fall), over, dmax))
        if fps == 60:
            check("rise time", rise is not None and abs(rise - ft) <= 0.2 * ft,
                  "%s vs fade_time %.2fs" % (fmt(rise), ft))
            check("fall time", fall is not None and abs(fall - 0.6 * ft) <= 0.2 * 0.6 * ft + 0.02,
                  "%s vs 0.6*fade_time %.3fs" % (fmt(fall), 0.6 * ft))
            check("no overshoot", over <= 1e-6, "%.5f" % over)
            check("frame delta cap", dmax <= p.slew_out / ft / 60.0 + 1e-6,
                  "%.4f per frame at 60 fps" % dmax)
            check("decays to exact zero", s[-1][1] == 0.0,
                  "drive %.3e two seconds after the fall (half-float storage pins a "
                  "geometric decay, hence the snap)" % s[-1][1])
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
    # Fence posts crossing the probe over several frames: the damping must
    # engage whatever the frame rate, and the residual must not depend on it.
    for ramp in (0.05, 0.1, 0.2):
        pps = {}
        for fps in (30, 60, 144):
            s = run(ramped_square(2.0, ramp), fps, 4.0, p, start=lit_state())
            last = window(s, 3.0, 4.0)
            pps[fps] = max(last) - min(last)
            imax = max(i for t, v, i in s)
            check("2 Hz, %.2fs edges at %d fps" % (ramp, fps), imax >= p.I0 and pps[fps] <= 0.2,
                  "instability peak %.2f, residual p-p %.3f" % (imax, pps[fps]))
        check("2 Hz, %.2fs edges: rate independence" % ramp, max(pps.values()) - min(pps.values()) < 0.05,
              "p-p spread %.3f across 30/60/144 fps" % (max(pps.values()) - min(pps.values())))
    # Worst-case flash count: full-scale cycles per second the filter can pass.
    cyc = 1.0 / (ft * (1.0 / p.slew_in + 1.0 / p.slew_out))
    check("cycle bound", cyc < 3.0, "at most %.2f full off-on-off cycles per second" % cyc)
    blip = lambda t: 0.0 if 1.0 <= t < 1.1 else 1.0
    s = run(blip, 60, 3.0, p, start=lit_state())
    dip = 1.0 - min(v for t, v, _ in s)
    check("blip depth", dip < 0.5, "dip %.3f for a 100 ms occlusion" % dip)
    hidden = lambda t: 0.0 if t < 5.0 else 1.0
    s = run(hidden, 60, 8.0, p, start=lit_state())
    rise = span(s, 0.9, 0.1, True, 5.0)
    check("isolated reveal", rise is not None and abs(rise - ft) <= 0.25 * ft,
          "rise %s vs %.2fs" % (fmt(rise), ft))
    # A thin post held over the sun with a held camera's sub-pixel wobble,
    # then removed: the wobble must not count as steps, so the reveal is not
    # slowed.
    for fps in (30, 60, 144):
        cov = pole_series(0.6, fps, 5.0)
        n_hold = len(cov)
        s = run(lambda t, cov=cov, n_hold=n_hold, fps=fps: cov[int(t * fps)] if int(t * fps) < n_hold else 1.0,
                fps, 8.0, p, start=lit_state())
        lo = s[n_hold - 1][1]
        rise = span(s, lo + 0.9 * (1.0 - lo), lo + 0.1 * (1.0 - lo), True, 5.0)
        I_at = s[n_hold - 1][2]
        check("partial occlusion reveal at %d fps" % fps,
              rise is not None and abs(rise - ft) <= 0.25 * ft and I_at < p.I0,
              "drive %.2f..%.2f held 5 s, instability %.2f at the reveal, rise %s vs %.2fs" % (
                  min(cov), max(cov), I_at, fmt(rise), ft))
    mixed = lambda t: square(3.0)(t) if t < 2.0 else 1.0
    s = run(mixed, 60, 6.0, p, start=lit_state())
    t90 = crossing(s, 0.9, True, 2.0)
    check("recovery after strobe", t90 is not None and t90 - 2.0 < 2.5,
          "reaches 0.9 %.2fs after the strobe stops" % ((t90 or 99) - 2.0))
    dim = lambda t: max(1.0 - 0.3 * t, 0.0)
    s = run(dim, 60, 3.0, p, start=lit_state(), full_fn=dim)
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
