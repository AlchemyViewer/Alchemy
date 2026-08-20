/**
 * @file aldaycyclelandmarks.h
 * @brief Finding sunrise, noon, sunset and midnight in an arbitrary day cycle
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

#ifndef AL_DAYCYCLELANDMARKS_H
#define AL_DAYCYCLELANDMARKS_H

#include "stdtypes.h"

#include <functional>

/// Where the interesting moments are in a day cycle.
///
/// A cycle position is a fraction of the whole cycle and means nothing on its
/// own: nothing in the viewer maps position to a clock. The day cycle editor
/// labels its timeline as a percentage for exactly that reason, and a region
/// can put its keyframes anywhere it likes, so "noon is 0.5" is true of some
/// day cycles and false of others. The only honest way to find noon is to ask
/// where the sun is highest, which is what this does.
///
/// It takes a sampler rather than a day cycle so that the arithmetic can be
/// tested without a viewer around it -- the same shape `curve_editor` uses,
/// and for the same reason. The caller supplies "sun altitude at this
/// position"; whether that comes from blending a real `LLSettingsDay` or from
/// a formula in a test is not this code's business.
namespace ALDayCycleLandmarks
{

/// Positions in [0, 1). A landmark a cycle does not have is reported absent
/// rather than guessed at: a sun that never sets has no sunrise, and a day
/// built from one repeated frame has nothing at all.
struct Landmarks
{
    bool has_sunrise  = false;
    bool has_noon     = false;
    bool has_sunset   = false;
    bool has_midnight = false;

    F32  sunrise      = 0.f;
    F32  noon         = 0.f;
    F32  sunset       = 0.f;
    F32  midnight     = 0.f;

    bool any() const { return has_sunrise || has_noon || has_sunset || has_midnight; }
};

/// Sun altitude at a cycle position: the vertical component of the sun's
/// direction, so +1 is overhead, 0 is exactly on the horizon and negative is
/// below it.
using altitude_sampler_t = std::function<F32(F32 position)>;

/// Default sample count. 96 samples is under four minutes of resolution on a
/// cycle mapped to a 24 hour day, which is finer than the eye reads off a
/// slider, and the horizon crossings are interpolated between samples rather
/// than snapped to one, so the sunrise and sunset it finds are better than
/// the grid.
constexpr S32 DEFAULT_SAMPLES = 96;

/// Sample the cycle and pick out its landmarks.
///
/// Noon and midnight are the highest and lowest the sun gets. Sunrise and
/// sunset are where it crosses the horizon going up and going down, taken
/// from the first crossing of each kind so that a cycle with several is
/// answered the same way every time.
///
/// A cycle whose altitude never varies has no landmarks -- there is no moment
/// in it to single out -- and one whose sun never sets has a noon but no
/// sunrise, sunset or midnight, because those three are defined by the
/// horizon and it never reaches it.
Landmarks find(const altitude_sampler_t& sampler, S32 samples = DEFAULT_SAMPLES);

} // namespace ALDayCycleLandmarks

#endif // AL_DAYCYCLELANDMARKS_H
