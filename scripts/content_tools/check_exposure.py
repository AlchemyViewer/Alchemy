#!/usr/bin/env python3
"""
check_exposure.py -- mirror of the auto-exposure meter.

The meter (doc/AUTO_EXPOSURE.md) runs in three passes: a grid of patch luminances
(class1/deferred/luminanceF.glsl), a 64-bin histogram of their log2 luminance built from points
(exposureHistogramV.glsl), and the exposure itself (exposureF.glsl): the mean log2 luminance
between two percentiles of the histogram's weight, put at middle grey, clamped, and adapted in
log space. The histogram and exposure passes are mirrored here statement for statement; the grid
is taken as given, one luminance and one sky share per texel.

What is checked, because each is easy to get subtly wrong and hard to see on screen:

  (a) Calibration: a uniform scene at the key (0.18) meters EV 0, and doubling every luminance
      adds exactly one stop; compensation and the EV clamps do what the settings say.
  (b) Robustness: a sun-sized hot spot, a thousandth of the frame at 10^4, moves the EV by less
      than 0.1, where a linear mean (the meter this replaced) moves it by stops.
  (c) The trim: the histogram's trimmed mean matches the exact trimmed mean of the sorted points
      to within 0.05 EV -- the bins decide only where the cut falls, not the values averaged.
  (d) Adaptation: stepping toward a new EV lands in the same place at the same time at 30, 60
      and 144 fps, and a brighter scene is followed faster than a darker one.
  (e) Nothing lit: a black frame opens up to the minimum EV rather than dividing by nothing.

If the shaders' binning, trim, target or adaptation change, change them here in the same commit.

Standard library only. Exit status 0 on RESULT: OK, 1 on RESULT: MISMATCH.
"""

import math
import random
import sys

# ---------------------------------------------------------------------------------------
# pipeline.cpp's constants and the settings' defaults

BINS = 64
HISTOGRAM_MIN_EV = -16.0
HISTOGRAM_MAX_EV = 12.0
KEY = 0.18
METER_WIDTH = 128
METER_HEIGHT = 72

DEFAULTS = {
    "compensation": 0.0,
    "min_ev": -3.0,
    "max_ev": 4.0,
    "low_percent": 10.0,
    "high_percent": 95.0,
    "speed_up": 3.0,
    "speed_down": 1.0,
    "metering_mode": 1,
    "sky_weight": 1.0,
}


# ---------------------------------------------------------------------------------------
# exposureHistogramV.glsl, statement for statement

def meter_weight(u, v, mode):
    qx, qy = u * 2.0 - 1.0, v * 2.0 - 1.0
    d2 = qx * qx + qy * qy
    if mode < 0.5:
        return 1.0
    if mode < 1.5:
        return math.exp(-2.0 * d2)
    d = math.sqrt(d2)
    t = min(max((d - 0.15) / (0.3 - 0.15), 0.0), 1.0)
    return 1.0 - t * t * (3.0 - 2.0 * t)


def histogram(grid, settings):
    """grid: {(x, y): (ground luminance, sky luminance, sky share)}; returns [(weight, weight * ev)]."""
    bins_per_ev = BINS / (HISTOGRAM_MAX_EV - HISTOGRAM_MIN_EV)
    bins = [[0.0, 0.0] for _ in range(BINS)]
    for (x, y), (ground, sky, sky_share) in grid.items():
        for is_sky in (False, True):
            L = sky if is_sky else ground
            share = sky_share if is_sky else 1.0 - sky_share
            u = (x + 0.5) / METER_WIDTH
            v = (y + 0.5) / METER_HEIGHT
            w = share * meter_weight(u, v, settings["metering_mode"]) * (settings["sky_weight"] if is_sky else 1.0)
            ev = min(max(math.log2(max(L, 1e-10)), HISTOGRAM_MIN_EV), HISTOGRAM_MIN_EV + BINS / bins_per_ev)
            b = min(math.floor((ev - HISTOGRAM_MIN_EV) * bins_per_ev), BINS - 1)
            if w > 0.0 and L > 0.0:
                bins[b][0] += w
                bins[b][1] += w * ev
    return bins


# ---------------------------------------------------------------------------------------
# pipeline.cpp generateExposure's uniforms, then exposureF.glsl, statement for statement

def exposure_params(settings, meter=True):
    low = min(max(settings["low_percent"] * 0.01, 0.0), 0.99)
    high = min(max(settings["high_percent"] * 0.01, low + 0.01), 1.0)
    params = (math.log2(KEY) + settings["compensation"], min(settings["min_ev"], settings["max_ev"]),
              max(settings["min_ev"], settings["max_ev"]), 1.0 if meter else 0.0)
    params2 = (low, high, max(settings["speed_up"], 0.0), max(settings["speed_down"], 0.0))
    return params, params2


def exposure(bins, params, params2, prev=None, dt=0.0):
    """Returns (scale, ev, target); prev is last frame's EV, or None without history."""
    total = sum(b[0] for b in bins)
    low = total * params2[0]
    high = total * params2[1]
    below = 0.0
    kept = 0.0
    kept_log = 0.0
    for weight, weighted_log in bins:
        lo = max(low, below)
        hi = min(high, below + weight)
        if hi > lo:
            share = (hi - lo) / weight
            kept += weight * share
            kept_log += weighted_log * share
        below += weight

    target = kept_log / kept - params[0] if kept > 0.0 else params[1]
    target = min(max(target, params[1]), params[2])

    ev = target
    if prev is not None:
        rate = params2[2] if target > prev else params2[3]
        ev = prev + (target - prev) * (1.0 - math.exp(-rate * dt))

    if params[3] < 0.5:
        ev = 0.0
        target = 0.0

    return 2.0 ** -ev, ev, target


# ---------------------------------------------------------------------------------------
# scenes

def uniform_grid(luminance, sky_share=0.0, sky=None):
    sky = luminance if sky is None else sky
    return {(x, y): (luminance, sky, sky_share) for y in range(METER_HEIGHT) for x in range(METER_WIDTH)}


def random_grid(seed, median, spread_ev):
    rng = random.Random(seed)
    return {(x, y): (median * 2.0 ** rng.gauss(0.0, spread_ev), 0.0, 0.0)
            for y in range(METER_HEIGHT) for x in range(METER_WIDTH)}


def meter(grid, settings=None, prev=None, dt=0.0):
    settings = settings or DEFAULTS
    params, params2 = exposure_params(settings)
    return exposure(histogram(grid, settings), params, params2, prev, dt)


class Checker:
    def __init__(self):
        self.failures = 0

    def expect(self, what, ok, detail=""):
        print("  %-4s %s%s" % ("ok" if ok else "FAIL", what, (" -- " + detail) if detail else ""))
        if not ok:
            self.failures += 1


def check_calibration(ck):
    print("(a) calibration")
    _, ev, _ = meter(uniform_grid(KEY))
    ck.expect("a uniform scene at the key meters EV 0", abs(ev) < 1e-9, "EV %.6f" % ev)
    _, ev1, _ = meter(uniform_grid(KEY * 2.0))
    ck.expect("doubling every luminance adds one stop", abs(ev1 - 1.0) < 1e-9, "EV %.6f" % ev1)
    _, ev_c, _ = meter(uniform_grid(KEY), dict(DEFAULTS, compensation=1.0))
    ck.expect("+1 compensation opens up one stop", abs(ev_c + 1.0) < 1e-9, "EV %.6f" % ev_c)
    _, ev_hi, _ = meter(uniform_grid(KEY * 2.0 ** 10))
    _, ev_lo, _ = meter(uniform_grid(KEY * 2.0 ** -10))
    ck.expect("a very bright scene stops at the maximum EV", ev_hi == DEFAULTS["max_ev"], "EV %.3f" % ev_hi)
    ck.expect("a very dark scene stops at the minimum EV", ev_lo == DEFAULTS["min_ev"], "EV %.3f" % ev_lo)
    _, ev_sky0, _ = meter(uniform_grid(KEY, sky_share=0.5, sky=KEY * 64.0), dict(DEFAULTS, sky_weight=0.0))
    ck.expect("sky weight 0 meters the ground alone", abs(ev_sky0) < 1e-9, "EV %.6f" % ev_sky0)
    params, params2 = exposure_params(DEFAULTS, meter=False)
    scale, ev_off, _ = exposure(histogram(uniform_grid(KEY * 16.0), DEFAULTS), params, params2)
    ck.expect("with the meter off the scale is 1", scale == 1.0 and ev_off == 0.0, "scale %.3f" % scale)


def check_hot_spot(ck):
    print("(b) a sun-sized hot spot")
    base = random_grid(1, KEY, 1.5)
    _, ev_base, _ = meter(base)
    spot = dict(base)
    rng = random.Random(2)
    keys = list(spot)
    for key in rng.sample(keys, max(1, len(keys) // 1000)):
        spot[key] = (1e4, 0.0, 0.0)
    _, ev_spot, _ = meter(spot)
    shift = ev_spot - ev_base
    linear_base = sum(v[0] for v in base.values()) / len(base)
    linear_spot = sum(v[0] for v in spot.values()) / len(spot)
    linear_shift = math.log2(linear_spot / linear_base)
    ck.expect("moves the metered EV by under 0.1", abs(shift) < 0.1,
              "%.4f EV (a linear mean moves %.2f EV)" % (shift, linear_shift))


def check_trim(ck):
    print("(c) the trim against an exact sort")
    settings = dict(DEFAULTS, metering_mode=0)
    params, params2 = exposure_params(settings)
    for seed, median, spread in ((3, 0.05, 2.0), (4, 1.0, 3.0), (5, 0.3, 0.5)):
        grid = random_grid(seed, median, spread)
        _, _, target = exposure(histogram(grid, settings), params, params2)
        evs = sorted(min(max(math.log2(v[0]), HISTOGRAM_MIN_EV), HISTOGRAM_MAX_EV) for v in grid.values())
        n = len(evs)
        lo, hi = params2[0] * n, params2[1] * n
        kept = [ev for i, ev in enumerate(evs) if lo <= i < hi]
        exact = min(max(sum(kept) / len(kept) - params[0], params[1]), params[2])
        ck.expect("median %.2f, spread %.1f EV" % (median, spread), abs(target - exact) < 0.05,
                  "histogram %.4f, exact %.4f" % (target, exact))


def check_adaptation(ck):
    print("(d) adaptation")
    settings = DEFAULTS
    params, params2 = exposure_params(settings)
    for label, luminance in (("toward a brighter scene", KEY * 8.0), ("toward a darker scene", KEY / 4.0)):
        bins = histogram(uniform_grid(luminance), settings)
        results = []
        for fps in (30, 60, 144):
            ev = 0.0
            for _ in range(fps):  # one second
                _, ev, target = exposure(bins, params, params2, prev=ev, dt=1.0 / fps)
            results.append(ev)
        spread = max(results) - min(results)
        ck.expect("%s: the same EV after one second at 30, 60 and 144 fps" % label, spread < 1e-9,
                  "EV %.6f, spread %.1e" % (results[0], spread))
    # after one second, the share of a step covered in each direction
    up = histogram(uniform_grid(KEY * 2.0), settings)
    down = histogram(uniform_grid(KEY / 2.0), settings)
    ev_up = 0.0
    ev_down = 0.0
    for _ in range(60):
        _, ev_up, _ = exposure(up, params, params2, prev=ev_up, dt=1.0 / 60)
        _, ev_down, _ = exposure(down, params, params2, prev=ev_down, dt=1.0 / 60)
    ck.expect("a brighter scene is followed faster than a darker one", ev_up > -ev_down,
              "one second covers %.0f%% up, %.0f%% down" % (ev_up * 100.0, -ev_down * 100.0))


def check_black(ck):
    print("(e) nothing lit")
    _, ev, _ = meter(uniform_grid(0.0))
    ck.expect("a black frame opens up to the minimum EV", ev == DEFAULTS["min_ev"], "EV %.3f" % ev)


def main():
    ck = Checker()
    check_calibration(ck)
    check_hot_spot(ck)
    check_trim(ck)
    check_adaptation(ck)
    check_black(ck)
    print("RESULT: %s" % ("OK" if ck.failures == 0 else "MISMATCH"))
    return 0 if ck.failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
