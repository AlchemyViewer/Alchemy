/**
 * @file alcolorwheelmodel.h
 * @brief The maths behind a colour wheel: RGB triplet <-> master, hue, saturation
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

#ifndef AL_COLORWHEELMODEL_H
#define AL_COLORWHEELMODEL_H

#include "v3math.h"

/**
 * The shape half of @ref ALColorWheelCtrl. No GL, no LLUI, no viewer globals,
 * so @c alcolorwheelmodel_test can exercise it directly.
 *
 * @par The decomposition
 * A grading wheel offers three things to drag -- a master level, a hue and a
 * saturation -- and the settings behind it are plain RGB triplets. Three
 * degrees of freedom either way, and the bridge is the achromatic/chroma
 * basis:
 *
 * @code
 *   master m = (r+g+b)/3           deviation d = (r,g,b) - m   (sums to zero)
 *   u = normalize(1, -1/2, -1/2)   v = normalize(0, +sqrt3/2, -sqrt3/2)
 *   hue = atan2(d.v, d.u)          sat = |d|
 * @endcode
 *
 * {(1,1,1)/sqrt3, u, v} is orthonormal, so this is a rotation of the RGB cube
 * and nothing else -- which is why it **round-trips exactly**. Typing a value
 * into a numeric field moves the puck to the right place, and dragging the
 * puck writes back the same number the user would have typed. Nothing in the
 * UI has to remember a "true" hue alongside the setting.
 *
 * The chroma-plane angle also lands on the usual colour circle: 0 is red, 120
 * degrees green, 240 blue, because pushing the deviation toward +R and away
 * from G and B is exactly what "a red cast" means. So a conventional hue ring
 * is honest here -- but @ref ringColor derives it from this same basis anyway,
 * so the ring and the puck cannot drift apart.
 *
 * @par The reachable region is a hexagon, not a disc
 * Each channel clamps independently, so the set of legal deviations is a
 * regular hexagon in the chroma plane, not a circle. Its inradius is
 * @c sqrt(1.5) * halfRange -- reached along the primary directions, where one
 * channel carries 0.8165 of the unit deviation -- and its circumradius is
 * @c sqrt(2) * halfRange at the corners. @ref getMaxSat returns the inradius,
 * so a puck anywhere on the rim is always reachable; the corners sit slightly
 * outside it and a typed value may legitimately put the puck a little past the
 * ring.
 *
 * @par One model, four configurations
 * Lift is centred on 0 over [-0.5, 0.5]; gamma and gain on 1 over [0.5, 1.5];
 * a split-tone tint on 0.5 over [0, 1] with the master locked. The tint case
 * needs the lock because the renderer divides each tint by
 * @c dot(tint, LUMA) (pipeline.cpp, @c tint_to_ratio), so tint *magnitude*
 * cancels out entirely -- a master there would be a control that does nothing.
 */
class ALColorWheelModel
{
public:
    /// Unit deviation direction for a hue angle, in radians. The one function
    /// both the puck and the ring go through.
    static LLVector3 chromaDirection(F32 hue);

    /// Arithmetic mean of the three channels.
    static F32 masterOf(const LLVector3& rgb);

    /// Cartesian coordinates of a triplet in the chroma plane, which is what
    /// a wheel's puck sits on. @ref toPolar is this in polar form.
    ///
    /// Exposed so a vectorscope can plot pixels on the very plane the wheels
    /// edit, without a second copy of the basis: push a wheel and the trace
    /// must move the same way, or the two are lying about the same colour.
    /// The magnitude never exceeds sqrt(2/3), the widest deviation any legal
    /// triplet has -- see @ref MAX_CHROMA.
    static void toChroma(const LLVector3& rgb, F32& u_out, F32& v_out);

    /// Largest chroma magnitude a triplet within a unit range can reach:
    /// sqrt(2/3), at the RGB cube's corners. The bound a plot of this plane
    /// scales to.
    static const F32 MAX_CHROMA;

    /// Hue in [0, 2pi) and saturation (the deviation's magnitude) of a triplet.
    static void toPolar(const LLVector3& rgb, F32& hue_out, F32& sat_out);

    /// Rebuild a triplet. Unclamped -- callers go through @ref setPolar to get
    /// the channel clamp.
    static LLVector3 toRGB(F32 master, F32 hue, F32 sat);

    ALColorWheelModel() = default;

    /// @param centre  the neutral master value (0 for lift, 1 for gain, ...)
    /// @param lo, hi  the per-channel clamp the renderer applies
    /// @param master_locked  true for split-tone tints, whose magnitude the
    ///        shader normalises away
    void configure(F32 centre, F32 lo, F32 hi, bool master_locked);

    F32  getCentre() const { return mCentre; }
    F32  getMin() const    { return mMin; }
    F32  getMax() const    { return mMax; }
    bool isMasterLocked() const { return mMasterLocked; }

    /// Saturation at the ring's rim: the hexagon's inradius, so every hue is
    /// reachable at radius 1. Independent of the current master on purpose --
    /// a ring that shrank as the master approached its limit would be unusable
    /// exactly where a colourist needs it. Going past the rim is handled by
    /// the channel clamp instead.
    F32 getMaxSat() const;

    /// Store a triplet, clamped per channel. Everything else reads back from
    /// the stored value, so the puck can never show a colour the setting is
    /// not actually holding.
    void setRGB(const LLVector3& rgb);
    const LLVector3& getRGB() const { return mRGB; }

    F32 getMaster() const;
    F32 getHue() const;
    F32 getSat() const;

    /// Move the master, keeping hue and saturation. Ignored when locked.
    void setMaster(F32 master);

    /// Move the puck, keeping the master. @a sat is in value units, not ring
    /// fractions; pass @c getMaxSat() * radius.
    void setPolar(F32 hue, F32 sat);

    /// Set one channel, as a numeric field does.
    void setChannel(S32 index, F32 value);

    /// Back to neutral: master at the centre, no chroma.
    void reset();

    /// What the ring should show at this angle -- the same chroma direction
    /// the puck would produce, rendered at a fixed reference so the ring stays
    /// stable while the master moves.
    static LLVector3 ringColor(F32 hue);

private:
    LLVector3 mRGB{ 0.f, 0.f, 0.f };
    F32  mCentre = 0.f;
    F32  mMin = -0.5f;
    F32  mMax = 0.5f;
    bool mMasterLocked = false;
};

#endif // AL_COLORWHEELMODEL_H
