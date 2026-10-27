/**
 * @file alskylayers.h
 * @brief What the sky's composite takes of the layers it works out along each view ray from a few values a frame: the
 *        layers a view draws, the sun's and moon's quads, the sun bloom's square, and the meteors' streaks on the
 *        screen.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#pragma once

#include "stdtypes.h"
#include "v3math.h"
#include "v4coloru.h"
#include "v4math.h"

#include <array>
#include <vector>

class LLMatrix4a;
class LLQuaternion;

/// The sky's layers as the composite takes them (deferred/skyCompositeF.glsl and the layers' files): no GL and no
/// settings, so ALSky, the pass that binds the composite and the GL tests share one definition.
namespace ALSkyLayers
{

/// The layers the composite draws, one bit each in its skyLayers. These are deferred/skyCompositeF.glsl's
/// SKY_LAYER_* and must stay equal to them.
enum Layer : S32
{
    STARS   = 1 << 0, ///< the stars' attachment of the G-buffer, added
    BLOOM   = 1 << 1, ///< the sun bloom, laid under the sun disc (its program's SUN_BLOOM only)
    SUN     = 1 << 2, ///< the sun disc
    MOON    = 1 << 3, ///< the moon
    METEORS = 1 << 4, ///< the meteors' streaks, added
    AURORA  = 1 << 5, ///< the aurora, added
    CLOUDS  = 1 << 6, ///< the clouds
    HDRI    = 1 << 7, ///< an HDRI sky in place of every other layer, beside WindLight's dome by the split screen
};

/// The fragment files the composite is built from: the composite itself and the layers it works out, with the value
/// noise the aurora takes. The sky's pass links them under its own stages (deferred/skyCompositeV.glsl,
/// deferred/skyCompositeMainF.glsl), as the GL tests that draw the composite link them under theirs.
inline constexpr const char* COMPOSITE_FILES[] = {
    "deferred/skyCompositeF.glsl", "deferred/skyBodiesF.glsl",  "deferred/skyCloudsF.glsl",
    "deferred/skyAuroraF.glsl",    "deferred/skyMeteorsF.glsl", "windlight/windlightDomeF.glsl",
    "deferred/valueNoiseUtil.glsl",
};

/// The star brightness over 500 (Frame::mStarAlpha) from which a view draws the night's layers: the stars, the
/// meteors and the aurora.
constexpr F32 NIGHT_STAR_ALPHA = 0.001f;

/// What decides the layers a view of the sky draws.
struct Frame
{
    bool mProbe     = false; ///< a reflection probe's face or a mirror's (gCubeSnapshot)
    bool mMirror    = false; ///< a mirror's (LLHeroProbeManager::isMirrorPass)
    bool mRadiance  = true;  ///< not an irradiance pass of the probes (LLReflectionMapManager::isRadiancePass)
    bool mHdri      = false; ///< an HDRI sky in place of WindLight's (ALSky::useHDRISky)
    bool mSun       = false; ///< the sun is up and has a texture
    bool mBloom     = false; ///< the sun bloom is drawn (AlchemyRenderSunBloom, latched) and has a texture
    bool mMoon      = false; ///< the moon is up and has a texture
    F32  mStarAlpha = 0.f;   ///< the star brightness over 500
    bool mMeteors   = false; ///< meteors are on (RenderMeteorsEnabled), so a view of the night ticks them
    F32  mAurora    = 0.f;   ///< the aurora's intensity after the moon's wash
    bool mClouds    = false; ///< clouds are shown (RENDER_TYPE_CLOUDS) and have a noise texture
};

/// The layers @a frame draws in its kind of view: an HDRI alone; in a probe's face neither the stars, the meteors nor
/// the aurora, and no sun, so that the punctual sun is not counted twice, but in a mirror, which is a picture rather
/// than lighting; no clouds in an irradiance pass; and the night's layers only while the stars are bright enough to
/// draw.
S32 layersFor(const Frame& frame);

/// The attachment of the G-buffer the stars are drawn into and the composite reads them from: the emissive with the
/// emissive buffer (frag_data[3]), the albedo without it (frag_data[0]). A view whose layers leave the stars out
/// neither clears nor reads it.
constexpr U32 starAttachment(bool emissive) { return emissive ? 3 : 0; }

/// A sun's or moon's quad about the camera, in the agent's frame: corners 0 to 3 at (-right, +up), (-right, -up),
/// (+right, +up) and (+right, -up), texture coordinates (0, 1), (0, 0), (1, 1) and (1, 0). A parallelogram, its right
/// and up axes square to each other.
using Corners = std::array<LLVector3, 4>;

/// A body's half-extent per unit of its disk radius, its scale and its distance.
constexpr F32 BODY_FACTOR = 0.1f;

/// The quad of a body whose direction is @a rotation's turn of +x, centred @a distance from the camera along it, its
/// half-extents @a scale times @a disk_radius times a tenth of the distance: wider by up to 30% and taller by up to 20%
/// as the body nears the horizon.
Corners bodyCorners(const LLQuaternion& rotation, F32 distance, F32 scale, F32 disk_radius);

/// The sun bloom's square about the sun's quad @a sun: centred on it, a half-extent every way of four times the disc's
/// half-width, along the disc's axes.
Corners bloomCorners(const Corners& sun);

/// The moon's opacity as it nears the horizon, from @a moon_z, the agent's z of its direction: four times its square,
/// to 1, the pre-EEP fade; 1 below the horizon, where the moon is not drawn.
F32 moonFade(F32 moon_z);

/// How far the sun disc lies below its quad, in metres down the agent's z: rayQuad's offset for the sun. The bloom is
/// lowered with it, so the two stay centred on each other.
constexpr F32 SUN_DROP = 50.f;

/// What the composite's ray test takes of a quad (deferred/skyBodiesF.glsl's skyQuadCoord), the quad moved by
/// @a offset in the agent's frame: its centre, then its right and up half-extents each over its squared length, in the
/// dome's frame (north, up, east). A ray hits the quad where both of its offset's dot products with those axes lie in
/// [-1, 1].
std::array<LLVector3, 3> rayQuad(const Corners& corners, const LLVector3& offset = LLVector3::zero);

/// The most meteors ALSky keeps, and the composite's array of them.
constexpr U32 MAX_METEORS = 64;

/// The vec4s a meteor takes in the composite's skyMeteorData. deferred/skyMeteorsF.glsl's SKY_METEOR_VECTORS must equal
/// it, and its SKY_MAX_METEORS MAX_METEORS.
constexpr U32 METEOR_VECTORS = 3;

/// The width the meteors draw at, in pixels each side of the streak for a meteor of width 1.
constexpr F32 METEOR_WIDTH_PIXELS = 1.5f;

/// A meteor as ALSky ticks it for a frame, about the camera in the agent's frame: the streak's midpoint and half
/// of it toward the head, its colour times its envelope (sRGB) and its width class as eight bits each, the width a
/// quarter in alpha, and its width class as it is.
struct Meteor
{
    LLVector3 mMidpoint;
    LLVector3 mHalf;
    LLColor4U mColour;
    F32       mWidth = 1.f;
};

/// The streaks of @a meteors on the screen, into @a out as skyMeteorData takes them, METEOR_VECTORS a streak:
/// - the midpoint's normalized device coordinates, and the first row of the inverse of the streak's frame (the half
///   streak toward the head and the half width across it, each in normalized device coordinates);
/// - that inverse's second row, the width class the quad reaches across, and the one its profile is divided by, read
///   back from the eight bits the meteor holds it in;
/// - the colour, linear (sRGB to the power 2.2).
///
/// Each streak is a quad on the screen of @a width_px pixels a side for each unit of width, through @a mvp (the
/// agent's frame about the camera to clip space) onto a @a width by @a height viewport. A streak with an end at or
/// behind the camera, one seen end on (shorter on the screen than a hundredth of a pixel each way), one wholly off the
/// screen, and one with no area are left out. Returns how many are in @a out.
U32 packMeteors(const std::vector<Meteor>& meteors, const LLMatrix4a& mvp, F32 width, F32 height, F32 width_px,
                std::vector<LLVector4>& out);

} // namespace ALSkyLayers
