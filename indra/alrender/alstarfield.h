/**
 * @file alstarfield.h
 * @brief The night sky's stars: where they lie and the colour each one is.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#pragma once

#include "llmath.h"
#include "llstrider.h"
#include "llvector4a.h"
#include "stdtypes.h"
#include "v3color.h"
#include "v3math.h"
#include "v4coloru.h"

#include <cmath>
#include <vector>

class LLMatrix4a;

/// The star field, worked from a star count and the dome's radius alone: no GL and no settings, so ALSky's buffer and
/// the GL tests that draw it share one definition.
///
/// The stars' frame is the agent's turned slowly about z (ALSky::renderStars): x and y on the
/// horizon, z up. Every star lies on the upper hemisphere.
namespace ALStarField
{

/// A star: its centre on its tier's sphere about the camera, and its colour as the vertex stage takes it, the
/// black-body colour (sRGB) in rgb and the star's intrinsic intensity in alpha.
struct Star
{
    LLVector3 mPosition;
    LLColor4U mColour;
};

/// The stars a count of DEFAULT_COUNT lays out in each of the three tiers: bright primaries, mid stars and the faint
/// dust, at 1, 1.6 and 3 dome radii. Any other count scales every tier alike, so the mix stays the same.
constexpr U32 TIER_COUNT[3] = { 5000, 30000, 120000 };
constexpr U32 DEFAULT_COUNT = TIER_COUNT[0] + TIER_COUNT[1] + TIER_COUNT[2];

/// The most stars a count may ask for.
constexpr U32 MAX_COUNT = 700000;

/// Tanner Helland's black-body colour for a temperature in kelvin, sRGB 0..1: within a few percent over the visual
/// range 1000 K..40000 K, and the familiar red to white to blue of the stars. The meteors take theirs from it too.
LLColor3 blackBodyColor(F32 temperature_K);

/// A direction uniform over the upper hemisphere (z >= 0), from two of @a draw's numbers in [0, 1): z, which is
/// uniform by area, then the azimuth. The meteors start from it too.
template <typename Rand> LLVector3 uniformUpperHemisphere(Rand& draw)
{
    const F32 z   = draw();
    const F32 phi = F_TWO_PI * draw();
    const F32 r   = sqrtf(llmax(0.0f, 1.0f - z * z));
    return LLVector3(r * cosf(phi), r * sinf(phi), z);
}

/// The Milky Way band's density at @a direction, a unit vector of the stars' frame: 1 on the galactic plane, falling as
/// a Gaussian to under 0.0001 at its pole. A star of the band keeps a direction drawn uniformly over the hemisphere
/// where a draw in [0, 1) falls under the density there.
F32 milkyWayDensity(const LLVector3& direction);

/// The field of @a count stars, held to MAX_COUNT, about a dome of @a dome_radius, the band's stars drawn by
/// milkyWayDensity (detail::generate, alstarfieldgenerate.h). The stars draw from a generator of their own with a fixed
/// seed, so a count lays out the same field in every session and on every platform, and images of the sky compare.
/// Each star takes one draw of it, the seed of a generator of the star's own that every draw of the star comes from; so
/// where a platform's maths rounds one of a star's comparisons the other way, as expf may for the band's, that star may
/// differ, and no other does.
std::vector<Star> generate(U32 count, F32 dome_radius);

/// The bounds of starsV.glsl's and starsF.glsl's per-star terms, each the shader's constant of the same name with
/// STAR_ before it, which brightest bounds what a star can add by.
constexpr F32 FAINT_PX      = 1.8f;  ///< the ideal width of a star of intensity 0, in pixels
constexpr F32 BRIGHT_PX     = 7.0f;  ///< and of intensity 1, before its spikes
constexpr F32 SPIKE_START   = 0.90f; ///< the intensity past which a star grows diffraction spikes
constexpr F32 SPIKE_PX      = 12.0f; ///< the width the spikes add at intensity 1
constexpr F32 MIN_PX        = 2.6f;  ///< the least width a star is drawn at, its brightness spread over the area
constexpr F32 TWINKLE_MAX   = 0.65f; ///< the most the scintillation moves a star's brightness, a share of its intensity
constexpr F32 CHROMA        = 0.18f; ///< the colour scintillation, a share of the brightness's
constexpr F32 MOON_WASH     = 0.55f; ///< how far a moon at full power washes out a star of intensity 0
constexpr F32 MOON_FADE_MAX = 0.95f; ///< the most the moon's wash and glare dim a star together
constexpr F32 BLOOM_BOOST   = 1.2f;  ///< the emissive's gain past 1, times the intensity squared
constexpr F32 HALO          = 0.06f; ///< the shape's halo at the star's centre, where its core is 1 (starsF.glsl)
constexpr F32 SPIKE         = 0.30f; ///< each spike's there at full strength (starsF.glsl)

/// The faintest intensity, as the colour's alpha holds it, that has diffraction spikes: starsV.glsl's
/// smoothstep(STAR_SPIKE_START, 1.0, intensity) is above 0 past SPIKE_START, and 230 / 255 is the first step past it.
/// Such a star reaches up to 19 pixels across, and GL drops a point whose centre leaves the screen, so its arms would
/// pop at the edge: it is drawn as a quad instead. Every other star is a point, at most 7 pixels across.
constexpr U8 SPIKE_INTENSITY = 230;

inline bool spiked(const Star& star)
{
    return star.mColour.mV[VALPHA] >= SPIKE_INTENSITY;
}

/// The vertices of a spiked star's quad: two triangles, whose corners starsV.glsl takes from gl_VertexID, the quads
/// lying first in the buffer, from vertex 0.
constexpr U32 QUAD_VERTICES = 6;

/// The sky's cells, which the points are binned into so a view draws only those it can see: BANDS bands of equal height
/// in z, so of equal area, over the upper hemisphere, each cut into SEGMENTS equal arcs of azimuth. Each is about 6
/// degrees across, but those of the top band, which are wedges 20 degrees long.
constexpr U32 BANDS    = 16;
constexpr U32 SEGMENTS = 32;
constexpr U32 CELLS    = BANDS * SEGMENTS;

/// The cell a direction from the camera lies in; one below the horizon counts in the lowest band.
U32 cellOf(const LLVector3& direction);

/// A cell's points in the buffer, and a cone about the camera holding every one of their directions.
struct Cell
{
    U32       mFirst = 0;        ///< its first vertex
    U32       mCount = 0;        ///< its stars
    LLVector3 mAxis;             ///< the cone's axis, unit
    F32       mSinRadius = 0.f;  ///< the sine of its half-angle, which is under 90 degrees
};

/// Where the stars lie in the buffer: the spiked stars' quads first, QUAD_VERTICES vertices each, then every other
/// star, one vertex each, drawn as points, cell by cell. The quads, and each cell's points, run brightest first, so the
/// stars a frame can see are a prefix of each run (faintestVisible).
struct Layout
{
    U32 mQuads = 0;  ///< spiked stars
    U32 mPoints = 0; ///< the rest
    std::vector<Cell> mCells; ///< CELLS of them, in order, each starting where the one before ends
    std::vector<U8> mIntensities; ///< each star's intensity byte, in the order the stars are laid out

    U32 firstPoint() const { return mQuads * QUAD_VERTICES; }
    U32 vertices() const { return firstPoint() + mPoints; }
};

/// Orders @a stars as the buffer lays them out, and says where each kind and each cell lies.
Layout layOut(std::vector<Star>& stars);

/// A step of 8 bits, and the most a star left out may add to any pixel: half that step. The cull is an 8-bit
/// attachment's, the albedo or the emissive without HDR (RGB8), which the stars blend into a step at a time, so what a
/// star adds under half a step rounds away and leaves the picture as it was. The float one, the emissive under HDR
/// (R11G11B10F), rounds nothing away, and the exposure, the tonemapper and the grade after it may show any of it, so
/// every star in view is drawn there.
constexpr F32 QUANTUM = 1.f / 255.f;
constexpr F32 LEFT_OUT = 0.5f * QUANTUM;

/// The most a star of intensity @a intensity can add to any pixel, by starsV.glsl and starsF.glsl at their bounds, for a
/// frame's star brightness (custom_alpha), the moon's power over the sky (moon_brightness times the sine of its
/// elevation, 0 below the horizon) and the attachment's gain (the emissive's or the albedo's). Every term is at its most
/// but the star's own: its colour and extinction at 1, its scintillation and colour jitter at their peaks (up to
/// TWINKLE_MAX and CHROMA of that, +65% and +12%), the moon's glare at 0 and its wash at its least, the shape at its
/// centre. It rises with the intensity.
F32 brightest(F32 intensity, F32 star_alpha, F32 moon_power, bool emissive);

/// The faintest intensity byte that can add LEFT_OUT or more to a pixel of an 8-bit attachment this frame (brightest),
/// so every fainter star may be left out; 256 where none can.
U32 faintestVisible(F32 star_alpha, F32 moon_power, bool emissive);

/// The spiked stars a frame draws, those at @a faintest or brighter: a prefix of the quads, QUAD_VERTICES vertices
/// each.
U32 quadsDrawn(const Layout& layout, U32 faintest);

/// The most a point reaches past its centre, in pixels: half the widest star drawn as a point (6.5 pixels across, just
/// under SPIKE_INTENSITY) and the half pixel starsV.glsl widens each side by. GL drops a point whose centre leaves the
/// clip volume, but a view's cull keeps every point this near the edge, so nothing a driver would draw is left out.
constexpr F32 POINT_REACH = 4.f;

/// The side planes of a view, in the stars' frame and through the camera at its origin: the inward normals, unit, of
/// x = -w, x = w, y = -w and y = w through @a mvp (the stars' frame to clip space), each plane moved out by @a margin_x
/// or @a margin_y of normalized device coordinates. A star is thousands of times farther from the camera than the
/// camera is from the stars' origin, so a direction is in view where each normal makes a non-negative product with it.
struct Frustum
{
    LLVector3 mNormals[4];
};
Frustum frustum(const LLMatrix4a& mvp, F32 margin_x, F32 margin_y);

/// The frustum of a view @a width x @a height pixels through @a mvp, each side moved out by POINT_REACH pixels.
Frustum pointFrustum(const LLMatrix4a& mvp, U32 width, U32 height);

/// Whether any direction of @a cell's cone may lie inside @a frustum: false only where one plane has every one outside.
bool inView(const Frustum& frustum, const Cell& cell);

/// The runs of points a view draws, for glMultiDrawArrays: each cell in view, and runs that meet merged.
struct Draws
{
    std::vector<S32> mFirst;
    std::vector<S32> mCount;

    void clear()
    {
        mFirst.clear();
        mCount.clear();
    }
};

/// The points of @a layout that a view through @a frustum draws, at @a faintest or brighter, into @a draws.
void pointDraws(const Layout& layout, const Frustum& frustum, U32 faintest, Draws& draws);

/// The buffer's vertices for @a stars laid out as @a layout, layout.vertices() of them: a star's centre in xyz and its
/// colour's four bytes in w, the word where LLVertexBuffer keeps the texture index, so a star is 16 bytes. A quad's
/// star is written QUAD_VERTICES times.
void fillVertices(const std::vector<Star>& stars, const Layout& layout, LLStrider<LLVector4a> vertices);

} // namespace ALStarField
