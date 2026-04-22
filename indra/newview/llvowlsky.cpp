/**
 * @file llvowlsky.cpp
 * @brief LLVOWLSky class implementation
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
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

#include "llviewerprecompiledheaders.h"

#include "pipeline.h"

#include "llvowlsky.h"
#include "llsky.h"
#include "lldrawpoolwlsky.h"
#include "llface.h"
#include "llviewercontrol.h"
#include "llenvironment.h"
#include "llsettingssky.h"

constexpr U32 SKY_DETAIL = 18; // Any lower and there will be artifacts

// Three tiers of stars, layered at different simulated distances. The counts
// stack to ~155k stars; the faint "dust" tier dominates density, giving the
// eye a rich background without per-star cost dominating memory.
namespace
{
    struct StarTier
    {
        U32 count;               // number of stars in this tier
        F32 distance_factor;     // multiplies dome radius
        F32 intensity_floor;     // minimum post-distribution intensity
        F32 intensity_ceil;      // maximum post-distribution intensity
        F32 milky_way_fraction;  // 0..1, fraction of this tier biased to galactic band
    };

    constexpr StarTier kStarTiers[] = {
        //  count    dist    i_floor  i_ceil  mw_frac
        {    5000,   1.00f,  0.55f,   1.00f,  0.25f },   // bright primary
        {   30000,   1.60f,  0.18f,   0.60f,  0.35f },   // mid
        {  120000,   3.00f,  0.05f,   0.22f,  0.55f },   // faint "star dust"
    };

    constexpr U32 kTotalStars =
        kStarTiers[0].count + kStarTiers[1].count + kStarTiers[2].count;

    // Meteor tuning. kMeteorHardMax is a fixed upper bound used for dynamic VB
    // sizing — the user-facing RenderMeteorMaxCount setting is clamped to this
    // so the VB never has to be reallocated at runtime.
    constexpr U32 kMeteorHardMax        = 64;
    constexpr U32 kMeteorVertsPerQuad   = 6;

    // Arbitrary but fixed galactic pole - rotating this would rotate the Milky Way band.
    // Chosen so the band runs across the sky at a slight incline.
    inline LLVector3 getGalacticPole()
    {
        LLVector3 pole(0.30f, 0.72f, 0.62f);
        pole.normVec();
        return pole;
    }

    // Tanner Helland-style black-body color approximation from temperature in Kelvin.
    // Returns sRGB 0..1. Accurate enough (within a few percent) for the visual range
    // 1000K..40000K and matches the familiar red->white->blue stellar appearance.
    LLColor3 blackBodyColor(F32 temperature_K)
    {
        const F32 t = temperature_K * 0.01f; // temperature in hundreds of K

        F32 r, g, b;

        if (t <= 66.0f)
        {
            r = 1.0f;
        }
        else
        {
            const F32 x = t - 60.0f;
            r = 329.698727446f * powf(x, -0.1332047592f) / 255.0f;
        }

        if (t <= 66.0f)
        {
            g = (99.4708025861f * logf(llmax(1.0f, t)) - 161.1195681661f) / 255.0f;
        }
        else
        {
            const F32 x = t - 60.0f;
            g = 288.1221695283f * powf(x, -0.0755148492f) / 255.0f;
        }

        if (t >= 66.0f)
        {
            b = 1.0f;
        }
        else if (t <= 19.0f)
        {
            b = 0.0f;
        }
        else
        {
            b = (138.5177312231f * logf(t - 10.0f) - 305.0447927307f) / 255.0f;
        }

        return LLColor3(llclamp(r, 0.0f, 1.0f),
                        llclamp(g, 0.0f, 1.0f),
                        llclamp(b, 0.0f, 1.0f));
    }

    // Sample a stellar temperature. The distribution roughly tracks stellar
    // population statistics - dominated by cool M/K dwarfs with a long tail
    // toward the blue.
    F32 sampleStellarTemperature()
    {
        const F32 u = ll_frand();
        if (u < 0.0025f)      return 18000.0f + ll_frand() * 22000.0f; // O/B class (rare, very blue)
        if (u < 0.02f)        return 10000.0f + ll_frand() *  8000.0f; // late-B / A class (blue-white)
        if (u < 0.10f)        return  7000.0f + ll_frand() *  3000.0f; // F class (yellow-white)
        if (u < 0.30f)        return  5500.0f + ll_frand() *  1500.0f; // G class (sun-like)
        if (u < 0.60f)        return  4000.0f + ll_frand() *  1500.0f; // K class (orange)
        return                       2600.0f + ll_frand() *  1400.0f; // M class (cool red, majority)
    }

    // Power-law luminosity, biased heavily toward the faint end. Returned
    // value is normalized to [0..1]. Callers remap to the tier's intensity
    // range so the per-tier distribution forms a plausible magnitude scale.
    F32 samplePowerLawIntensity()
    {
        const F32 u = ll_frand();
        // Exponent 4 is a rough visual match to the Salpeter-like tail: most
        // stars are near the floor, a few outliers reach the ceiling.
        return powf(u, 4.0f);
    }

    // Uniformly sample a direction on the upper hemisphere (z >= 0).
    LLVector3 uniformUpperHemisphere()
    {
        const F32 z = ll_frand();                         // area-weighted
        const F32 phi = F_TWO_PI * ll_frand();
        const F32 r = sqrtf(llmax(0.0f, 1.0f - z * z));
        return LLVector3(r * cosf(phi), r * sinf(phi), z);
    }

    // Pick a random unit vector perpendicular to `n`. Uses a deterministic
    // orthogonal basis built from `n` and a fixed "up" swap for robustness
    // at the poles, then rotates around `n` by a uniform angle.
    LLVector3 randomTangentTo(const LLVector3& n)
    {
        const LLVector3 fallback = (fabsf(n.mV[2]) < 0.9f)
                                       ? LLVector3(0.f, 0.f, 1.f)
                                       : LLVector3(1.f, 0.f, 0.f);
        LLVector3 a = (n % fallback);   // cross
        a.normVec();
        LLVector3 b = (n % a);
        b.normVec();
        const F32 rot = F_TWO_PI * ll_frand();
        return a * cosf(rot) + b * sinf(rot);
    }

    // Rejection-sampled direction with optional bias toward the galactic plane.
    // A Gaussian density around cos(angle_from_plane)=0 yields a band a few
    // tens of degrees wide, which reads as a believable Milky Way without
    // relying on a texture.
    LLVector3 generateStarDirection(const LLVector3& galactic_pole, bool milky_way_biased)
    {
        constexpr U32 MAX_ATTEMPTS = 24;

        for (U32 attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
        {
            LLVector3 dir = uniformUpperHemisphere();

            if (!milky_way_biased)
                return dir;

            // cos(angle-to-pole) == 0 at the plane, == 1 at the pole.
            const F32 cos_to_pole = fabsf(dir * galactic_pole);
            // Gaussian density peaking at the plane. sigma ~ 18 degrees.
            const F32 density = expf(-cos_to_pole * cos_to_pole * 10.0f);
            if (ll_frand() < density)
                return dir;
        }

        // Unlikely to ever hit this fallback, but keeps the function total.
        return uniformUpperHemisphere();
    }
}

inline U32 LLVOWLSky::getNumStacks(void)
{
    return SKY_DETAIL;
}

inline U32 LLVOWLSky::getNumSlices(void)
{
    return 2 * SKY_DETAIL;
}

inline U32 LLVOWLSky::getStripsNumVerts(void)
{
    return (getNumStacks() - 1) * getNumSlices();
}

inline U32 LLVOWLSky::getStripsNumIndices(void)
{
    return 2 * ((getNumStacks() - 2) * (getNumSlices() + 1)) + 1 ;
}

U32 LLVOWLSky::getStarsNumVerts(void)
{
    return kTotalStars;
}

LLVOWLSky::LLVOWLSky(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp)
    : LLStaticViewerObject(id, pcode, regionp, true)
{
    initStars();
}

void LLVOWLSky::idleUpdate(LLAgent &agent, const F64 &time)
{

}

bool LLVOWLSky::isActive(void) const
{
    return false;
}

LLDrawable * LLVOWLSky::createDrawable(LLPipeline * pipeline)
{
    pipeline->allocDrawable(this);

    //LLDrawPoolWLSky *poolp = static_cast<LLDrawPoolWLSky *>(
        gPipeline.getPool(LLDrawPool::POOL_WL_SKY);

    mDrawable->setRenderType(LLPipeline::RENDER_TYPE_WL_SKY);

    return mDrawable;
}

// a tiny helper function for controlling the sky dome tesselation.
inline F32 calcPhi(const U32 &i, const F32 &reciprocal_num_stacks)
{
    // Calc: PI/8 * 1-((1-t^4)*(1-t^4))  { 0<t<1 }
    // Demos: \pi/8*\left(1-((1-x^{4})*(1-x^{4}))\right)\ \left\{0<x\le1\right\}

    // i should range from [0..SKY_STACKS] so t will range from [0.f .. 1.f]
    F32 t = float(i) * reciprocal_num_stacks; //SL-16127: remove: / float(getNumStacks());

    // ^4 the parameter of the tesselation to bias things toward 0 (the dome's apex)
    t *= t;
    t *= t;

    // invert and square the parameter of the tesselation to bias things toward 1 (the horizon)
    t = 1.f - t;
    t = t*t;
    t = 1.f - t;

    return (F_PI / 8.f) * t;
}

void LLVOWLSky::resetVertexBuffers()
{
    mStripsVerts.clear();
    mStarsVerts = nullptr;
    mFsSkyVerts = nullptr;
    mMeteorVerts = nullptr;

    gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);
}

void LLVOWLSky::cleanupGL()
{
    mStripsVerts.clear();
    mStarsVerts = nullptr;
    mFsSkyVerts = nullptr;
    mMeteorVerts = nullptr;

    LLDrawPoolWLSky::cleanupGL();
}

void LLVOWLSky::restoreGL()
{
    LLDrawPoolWLSky::restoreGL();
    gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);
}

bool LLVOWLSky::updateGeometry(LLDrawable * drawable)
{
    LL_PROFILE_ZONE_SCOPED;
    LLStrider<LLVector3>    vertices;
    LLStrider<LLVector2>    texCoords;
    LLStrider<U16>          indices;

    if (mFsSkyVerts.isNull())
    {
        mFsSkyVerts = new LLVertexBuffer(LLDrawPoolWLSky::ADV_ATMO_SKY_VERTEX_DATA_MASK);

        if (!mFsSkyVerts->allocateBuffer(4, 6))
        {
            LL_WARNS() << "Failed to allocate Vertex Buffer on full screen sky update" << LL_ENDL;
        }

        bool success = mFsSkyVerts->getVertexStrider(vertices)
                    && mFsSkyVerts->getTexCoord0Strider(texCoords)
                    && mFsSkyVerts->getIndexStrider(indices);

        if(!success)
        {
            LL_ERRS() << "Failed updating WindLight fullscreen sky geometry." << LL_ENDL;
        }

        *vertices++ = LLVector3(-1.0f, -1.0f, 0.0f);
        *vertices++ = LLVector3( 1.0f, -1.0f, 0.0f);
        *vertices++ = LLVector3(-1.0f,  1.0f, 0.0f);
        *vertices++ = LLVector3( 1.0f,  1.0f, 0.0f);

        *texCoords++ = LLVector2(0.0f, 0.0f);
        *texCoords++ = LLVector2(1.0f, 0.0f);
        *texCoords++ = LLVector2(0.0f, 1.0f);
        *texCoords++ = LLVector2(1.0f, 1.0f);

        *indices++ = 0;
        *indices++ = 1;
        *indices++ = 2;
        *indices++ = 1;
        *indices++ = 3;
        *indices++ = 2;

        mFsSkyVerts->unmapBuffer();
    }

    {
        const F32 dome_radius = LLEnvironment::instance().getCurrentSky()->getDomeRadius();

        const U32 max_buffer_bytes = gSavedSettings.getS32("RenderMaxVBOSize")*1024;
        const U32 data_mask = LLDrawPoolWLSky::SKY_VERTEX_DATA_MASK;
        const U32 max_verts = max_buffer_bytes / LLVertexBuffer::calcVertexSize(data_mask);

        const U32 total_stacks = getNumStacks();

        const U32 verts_per_stack = getNumSlices();

        // each seg has to have one more row of verts than it has stacks
        // then round down
        const U32 stacks_per_seg = (max_verts - verts_per_stack) / verts_per_stack;

        // round up to a whole number of segments
        const U32 strips_segments = (total_stacks+stacks_per_seg-1) / stacks_per_seg;

        mStripsVerts.resize(strips_segments, NULL);

#if RELEASE_SHOW_DEBUG
        LL_INFOS() << "WL Skydome strips in " << strips_segments << " batches." << LL_ENDL;

        LLTimer timer;
        timer.start();
#endif

        for (U32 i = 0; i < strips_segments ;++i)
        {
            LLVertexBuffer * segment = new LLVertexBuffer(LLDrawPoolWLSky::SKY_VERTEX_DATA_MASK);
            mStripsVerts[i] = segment;

            U32 num_stacks_this_seg = stacks_per_seg;
            if ((i == strips_segments - 1) && (total_stacks % stacks_per_seg) != 0)
            {
                // for the last buffer only allocate what we'll use
                num_stacks_this_seg = total_stacks % stacks_per_seg;
            }

            // figure out what range of the sky we're filling
            const U32 begin_stack = i * stacks_per_seg;
            const U32 end_stack = begin_stack + num_stacks_this_seg;
            llassert(end_stack <= total_stacks);

            const U32 num_verts_this_seg = verts_per_stack * (num_stacks_this_seg+1);
            llassert(num_verts_this_seg <= max_verts);

            const U32 num_indices_this_seg = 1+num_stacks_this_seg*(2+2*verts_per_stack);
            llassert(num_indices_this_seg * sizeof(U16) <= max_buffer_bytes);

            bool allocated = segment->allocateBuffer(num_verts_this_seg, num_indices_this_seg);
#if RELEASE_SHOW_WARNS
            if( !allocated )
            {
                LL_WARNS() << "Failed to allocate Vertex Buffer on update to "
                    << num_verts_this_seg << " vertices and "
                    << num_indices_this_seg << " indices" << LL_ENDL;
            }
#else
            (void) allocated;
#endif

            // lock the buffer
            bool success = segment->getVertexStrider(vertices)
                && segment->getTexCoord0Strider(texCoords)
                && segment->getIndexStrider(indices);

#if RELEASE_SHOW_DEBUG
            if(!success)
            {
                LL_ERRS() << "Failed updating WindLight sky geometry." << LL_ENDL;
            }
#else
            (void) success;
#endif

            // fill it
            buildStripsBuffer(begin_stack, end_stack, vertices, texCoords, indices, dome_radius, verts_per_stack, total_stacks);

            // and unlock the buffer
            segment->unmapBuffer();
        }

#if RELEASE_SHOW_DEBUG
        LL_INFOS() << "completed in " << llformat("%.2f", timer.getElapsedTimeF32().value()) << "seconds" << LL_ENDL;
#endif
    }

    updateStarGeometry(drawable);

    LLPipeline::sCompiles++;

    return true;
}

void LLVOWLSky::drawStars(void)
{
    //  render the stars as a sphere centered at viewer camera
    if (mStarsVerts.notNull())
    {
        mStarsVerts->setBuffer();
        mStarsVerts->drawArrays(LLRender::TRIANGLES, 0, (S32)(getStarsNumVerts() * 6));
    }
}

void LLVOWLSky::drawFsSky(void)
{
    if (mFsSkyVerts.isNull())
    {
        updateGeometry(mDrawable);
    }

    LLGLDisable disable_blend(GL_BLEND);

    mFsSkyVerts->setBuffer();
    mFsSkyVerts->drawRange(LLRender::TRIANGLES, 0, mFsSkyVerts->getNumVerts() - 1, mFsSkyVerts->getNumIndices(), 0);
    gPipeline.addTrianglesDrawn(mFsSkyVerts->getNumIndices());
    LLVertexBuffer::unbind();
}

void LLVOWLSky::drawDome(void)
{
    if (mStripsVerts.empty())
    {
        updateGeometry(mDrawable);
    }

    LLGLDepthTest gls_depth(GL_TRUE, GL_FALSE);

    std::vector< LLPointer<LLVertexBuffer> >::const_iterator strips_vbo_iter, end_strips;
    end_strips = mStripsVerts.end();
    for(strips_vbo_iter = mStripsVerts.begin(); strips_vbo_iter != end_strips; ++strips_vbo_iter)
    {
        LLVertexBuffer * strips_segment = strips_vbo_iter->get();

        strips_segment->setBuffer();

        strips_segment->drawRange(
            LLRender::TRIANGLE_STRIP,
            0, strips_segment->getNumVerts()-1, strips_segment->getNumIndices(),
            0);
        gPipeline.addTrianglesDrawn(strips_segment->getNumIndices());
    }

    LLVertexBuffer::unbind();
}

void LLVOWLSky::initStars()
{
    const F32 DISTANCE_TO_STARS = LLEnvironment::instance().getCurrentSky()->getDomeRadius();
    const LLVector3 galactic_pole = getGalacticPole();

    mStarPositions.clear();
    mStarColors.clear();
    mStarPositions.reserve(kTotalStars);
    mStarColors.reserve(kTotalStars);

    for (const StarTier& tier : kStarTiers)
    {
        const F32 tier_radius = DISTANCE_TO_STARS * tier.distance_factor;

        for (U32 i = 0; i < tier.count; ++i)
        {
            const bool in_milky_way = ll_frand() < tier.milky_way_fraction;
            LLVector3 dir = generateStarDirection(galactic_pole, in_milky_way);

            mStarPositions.push_back(dir * tier_radius);

            // Distance-tier dimming is baked into intensity_floor/ceil so the
            // far "dust" tier naturally renders faint. We only remap the
            // power-law sample into that range here.
            const F32 raw = samplePowerLawIntensity();
            const F32 intensity = llclamp(
                tier.intensity_floor + raw * (tier.intensity_ceil - tier.intensity_floor),
                0.0f, 1.0f);

            const F32 temperature_K = sampleStellarTemperature();
            const LLColor3 bb = blackBodyColor(temperature_K);

            LLColor4 c;
            c.mV[VRED]   = bb.mV[VRED];
            c.mV[VGREEN] = bb.mV[VGREEN];
            c.mV[VBLUE]  = bb.mV[VBLUE];
            c.mV[VALPHA] = intensity;
            mStarColors.push_back(c);
        }
    }
}

void LLVOWLSky::buildStripsBuffer(U32 begin_stack,
                                  U32 end_stack,
                                  LLStrider<LLVector3> & vertices,
                                  LLStrider<LLVector2> & texCoords,
                                  LLStrider<U16> & indices,
                                  const F32 dome_radius,
                                  const U32& num_slices,
                                  const U32& num_stacks)
{
    U32 i, j;
    F32 phi0, theta, x0, y0, z0;
    const F32 reciprocal_num_stacks = 1.f / num_stacks;

    llassert(end_stack <= num_stacks);

    // stacks are iterated one-indexed since phi(0) was handled by the fan above
#if NEW_TESS
    for(i = begin_stack; i <= end_stack; ++i)
#else
    for(i = begin_stack + 1; i <= end_stack+1; ++i)
#endif
    {
        phi0 = calcPhi(i, reciprocal_num_stacks);

        for(j = 0; j < num_slices; ++j)
        {
            theta = F_TWO_PI * (float(j) / float(num_slices));

            // standard transformation from  spherical to
            // rectangular coordinates
            x0 = sin(phi0) * cos(theta);
            y0 = cos(phi0);
            z0 = sin(phi0) * sin(theta);

#if NEW_TESS
            *vertices++ = LLVector3(x0 * dome_radius, y0 * dome_radius, z0 * dome_radius);
#else
            if (i == num_stacks-2)
            {
                *vertices++ = LLVector3(x0*dome_radius, y0*dome_radius-1024.f*2.f, z0*dome_radius);
            }
            else if (i == num_stacks-1)
            {
                *vertices++ = LLVector3(0, y0*dome_radius-1024.f*2.f, 0);
            }
            else
            {
                *vertices++     = LLVector3(x0 * dome_radius, y0 * dome_radius, z0 * dome_radius);
            }
#endif

            // generate planar uv coordinates
            // note: x and z are transposed in order for things to animate
            // correctly in the global coordinate system where +x is east and
            // +y is north
            *texCoords++    = LLVector2((-z0 + 1.f) / 2.f, (-x0 + 1.f) / 2.f);
        }
    }

    //build triangle strip...
    *indices++ = 0 ;

    S32 k = 0 ;
    for(i = 1; i <= end_stack - begin_stack; ++i)
    {
        *indices++ = i * num_slices + k ;

        k = (k+1) % num_slices ;
        for(j = 0; j < num_slices ; ++j)
        {
            *indices++ = (i-1) * num_slices + k ;
            *indices++ = i * num_slices + k ;

            k = (k+1) % num_slices ;
        }

        if((--k) < 0)
        {
            k = num_slices - 1 ;
        }

        *indices++ = i * num_slices + k ;
    }
}

bool LLVOWLSky::updateStarGeometry(LLDrawable *drawable)
{
    LL_PROFILE_ZONE_SCOPED;

    // Star data is static after initStars(); skip re-upload if the buffer
    // already exists. It's released on cleanupGL/resetVertexBuffers, so we'll
    // rebuild it after a context loss.
    if (mStarsVerts.notNull())
    {
        return true;
    }

    LLStrider<LLVector3> verticesp;
    LLStrider<LLColor4U> colorsp;
    LLStrider<LLVector2> texcoordsp;

    const U32 num_stars = getStarsNumVerts();
    const U32 num_verts = num_stars * 6;

    mStarsVerts = new LLVertexBuffer(LLDrawPoolWLSky::STAR_VERTEX_DATA_MASK);
    if (!mStarsVerts->allocateBuffer(num_verts, 0))
    {
        LL_WARNS() << "Failed to allocate Vertex Buffer for Sky to " << num_verts << " vertices" << LL_ENDL;
        return false;
    }

    bool success = mStarsVerts->getVertexStrider(verticesp)
        && mStarsVerts->getColorStrider(colorsp)
        && mStarsVerts->getTexCoord0Strider(texcoordsp);

    if(!success)
    {
        LL_ERRS() << "Failed updating star geometry." << LL_ENDL;
    }

    if (mStarPositions.size() < num_stars || mStarColors.size() < num_stars)
    {
        LL_ERRS() << "Star reference geometry insufficient." << LL_ENDL;
    }

    // Two-triangle quad. Corner offsets are in [-1, 1] — the vertex shader
    // turns them into pixel-sized screen-space offsets, so the actual CPU
    // "position" of every vertex of a star is identical (the star center).
    // This trades a small amount of memory for full resolution/FOV control on
    // the GPU side.
    static const LLVector2 kCornerOffsets[6] = {
        LLVector2(-1.f, -1.f), // 0: BL
        LLVector2( 1.f, -1.f), // 1: BR
        LLVector2( 1.f,  1.f), // 2: TR
        LLVector2(-1.f, -1.f), // 3: BL
        LLVector2( 1.f,  1.f), // 4: TR
        LLVector2(-1.f,  1.f), // 5: TL
    };

    for (U32 s = 0; s < num_stars; ++s)
    {
        const LLVector3& center = mStarPositions[s];
        const LLColor4U  color_u(mStarColors[s]);

        for (U32 v = 0; v < 6; ++v)
        {
            *(verticesp++)  = center;
            *(colorsp++)    = color_u;
            *(texcoordsp++) = kCornerOffsets[v];
        }
    }

    mStarsVerts->unmapBuffer();
    return true;
}

void LLVOWLSky::tickMeteors(F32 dt_seconds)
{
    LL_PROFILE_ZONE_SCOPED;

    if (dt_seconds <= 0.0f) return;

    // Age existing meteors and drop any that have run their course.
    for (MeteorState& m : mMeteors)
    {
        m.age += dt_seconds;
    }
    mMeteors.erase(
        std::remove_if(mMeteors.begin(), mMeteors.end(),
                       [](const MeteorState& m) { return m.age >= m.lifetime; }),
        mMeteors.end());

    static LLCachedControl<S32> max_count_setting(gSavedSettings, "RenderMeteorMaxCount", 16);
    static LLCachedControl<F32> spawn_secs_setting(gSavedSettings, "RenderMeteorSpawnInterval", 4.0f);
    const U32 max_count  = (U32)llclamp((S32)max_count_setting, 0, (S32)kMeteorHardMax);
    const F32 spawn_secs = llmax(0.05f, (F32)spawn_secs_setting);

    // Poisson-ish spawn: per-frame probability = dt / mean_interval. At 60 FPS
    // with a 4s mean, this produces a new meteor roughly every 240 frames.
    if (mMeteors.size() < max_count
        && ll_frand() < dt_seconds / spawn_secs)
    {
        const F32 dome_radius = LLEnvironment::instance().getCurrentSky()->getDomeRadius();

        // Start point anywhere on the upper hemisphere, but prefer a bit above
        // the horizon so meteors don't immediately pop out of view.
        LLVector3 start_dir = uniformUpperHemisphere();
        // Lift horizon-hugging starts slightly so short streaks stay visible.
        if (start_dir.mV[2] < 0.1f)
        {
            start_dir.mV[2] = 0.1f;
            start_dir.normVec();
        }

        // Travel direction is a random tangent in the sky tangent plane at
        // start_dir. This gives great-circle-like paths.
        LLVector3 travel_dir = randomTangentTo(start_dir);

        // Classify each new meteor into a visual archetype. The weighted mix
        // makes showers feel alive at high spawn rates — most meteors are
        // faint and quick, but the occasional fireball with exotic chemistry
        // colors draws the eye. The cumulative thresholds below are the
        // probability cutoffs for each class.
        const F32 roll = ll_frand();
        F32 width_scale;
        F32 path_mult  = 1.0f;       // multiplier on both path and trail length
        F32 life_mult  = 1.0f;       // multiplier on lifetime (shorter = faster)
        F32 peak;
        LLColor3 color;

        if (roll < 0.55f)
        {
            // Common: faint, thin, cool red-orange. These dominate by count
            // (matching real meteor distributions) and give the eye a quiet
            // background of activity.
            width_scale = 0.55f + 0.25f * ll_frand();
            peak        = 0.35f + 0.25f * ll_frand();
            life_mult   = 0.75f + 0.25f * ll_frand();
            const F32 tk = 2500.0f + 1500.0f * ll_frand();
            color = blackBodyColor(tk);
        }
        else if (roll < 0.85f)
        {
            // Standard: yellow-white, the "average" visible meteor.
            width_scale = 0.85f + 0.35f * ll_frand();
            peak        = 0.6f + 0.3f * ll_frand();
            const F32 tk = 4500.0f + 2500.0f * ll_frand();
            color = blackBodyColor(tk);
        }
        else if (roll < 0.96f)
        {
            // Fast: hot blue-white, travels farther in the same time so
            // apparent angular velocity is higher.
            width_scale = 0.7f + 0.3f * ll_frand();
            peak        = 0.8f + 0.2f * ll_frand();
            path_mult   = 1.25f + 0.35f * ll_frand();
            life_mult   = 0.6f + 0.25f * ll_frand();
            const F32 tk = 8000.0f + 7000.0f * ll_frand();
            color = blackBodyColor(tk);
        }
        else
        {
            // Fireball: thick, bright, persistent. Some roll to an exotic
            // chemistry color from ablation (Mg, Na, N, Cu) rather than the
            // usual blackbody curve.
            width_scale = 1.8f + 1.4f * ll_frand();
            peak        = 0.9f + 0.1f * ll_frand();
            path_mult   = 1.3f + 0.4f * ll_frand();
            life_mult   = 1.1f + 0.4f * ll_frand();

            const F32 chem = ll_frand();
            if (chem < 0.22f)       color = LLColor3(0.35f, 1.00f, 0.45f);  // magnesium green
            else if (chem < 0.42f)  color = LLColor3(1.00f, 0.55f, 0.20f);  // sodium orange
            else if (chem < 0.58f)  color = LLColor3(1.00f, 0.35f, 0.30f);  // nitrogen red
            else if (chem < 0.72f)  color = LLColor3(0.40f, 0.55f, 1.00f);  // copper/Mg blue
            else
            {
                // Hot white bolide, no exotic chemistry.
                const F32 tk = 9000.0f + 6000.0f * ll_frand();
                color = blackBodyColor(tk);
            }
        }

        MeteorState m;
        m.origin_world      = start_dir * dome_radius;
        m.direction_world   = travel_dir;
        // Base lifetime 1.1..2.5s. Fast class scales this down, fireballs up,
        // so the full range across classes is roughly 0.7..3.7s.
        m.lifetime          = (1.1f + 1.4f * ll_frand()) * life_mult;
        const F32 path_angle  = (0.21f + 0.31f * ll_frand()) * path_mult;   // radians
        const F32 trail_angle = path_angle * (0.35f + 0.25f * ll_frand());
        m.path_length_world  = dome_radius * path_angle;
        m.trail_length_world = dome_radius * trail_angle;
        m.color              = color;
        m.peak_intensity     = peak;
        m.width_scale        = width_scale;
        m.age                = 0.0f;

        mMeteors.push_back(m);
    }
}

void LLVOWLSky::updateMeteorGeometry()
{
    LL_PROFILE_ZONE_SCOPED;

    constexpr U32 total_verts = kMeteorHardMax * kMeteorVertsPerQuad;

    if (mMeteorVerts.isNull())
    {
        mMeteorVerts = new LLVertexBuffer(LLDrawPoolWLSky::METEOR_VERTEX_DATA_MASK);
        if (!mMeteorVerts->allocateBuffer(total_verts, 0))
        {
            LL_WARNS() << "Failed to allocate meteor VB" << LL_ENDL;
            mMeteorVerts = nullptr;
            return;
        }
    }

    LLStrider<LLVector3> positions;
    LLStrider<LLVector3> normals;
    LLStrider<LLColor4U> colors;
    LLStrider<LLVector2> texcoords;
    if (!mMeteorVerts->getVertexStrider(positions)
        || !mMeteorVerts->getNormalStrider(normals)
        || !mMeteorVerts->getColorStrider(colors)
        || !mMeteorVerts->getTexCoord0Strider(texcoords))
    {
        LL_WARNS() << "Failed striding meteor VB" << LL_ENDL;
        return;
    }

    // Six-vertex stretched quad: u in {-1,+1} (tail..head), v in {-1,+1} (side).
    static const LLVector2 kUV[kMeteorVertsPerQuad] = {
        LLVector2(-1.f, -1.f), // tail L
        LLVector2( 1.f, -1.f), // head L
        LLVector2( 1.f,  1.f), // head R
        LLVector2(-1.f, -1.f), // tail L
        LLVector2( 1.f,  1.f), // head R
        LLVector2(-1.f,  1.f), // tail R
    };

    U32 written = 0;
    for (const MeteorState& m : mMeteors)
    {
        if (written >= kMeteorHardMax) break;

        // Envelope: quick fade in, steady, slower fade out. These ratios read
        // as a meteor "blooming" and then trailing off.
        const F32 frac = m.age / llmax(m.lifetime, 1e-4f);
        F32 alpha = 1.0f;
        if (frac < 0.10f)      alpha = frac / 0.10f;
        else if (frac > 0.80f) alpha = (1.0f - frac) / 0.20f;
        alpha = llclamp(alpha, 0.0f, 1.0f) * m.peak_intensity;
        if (alpha <= 0.0f) continue;

        // Head progresses linearly along the travel direction. Tail is a fixed
        // length behind the head regardless of age — the trail doesn't grow,
        // it just moves with the head.
        const LLVector3 head = m.origin_world + m.direction_world * (m.path_length_world * frac);
        const LLVector3 tail = head - m.direction_world * m.trail_length_world;
        const LLVector3 midpoint = (head + tail) * 0.5f;
        const LLVector3 half_vec = (head - tail) * 0.5f;

        // Pack per-frame envelope alpha into the RGB channels (in sRGB space — a
        // mild nonlinearity vs. linear-space premultiply, but the envelope is
        // smooth enough that the difference isn't visible). This frees the
        // color's alpha slot to carry the per-meteor width_scale, which the
        // fragment shader needs to normalize its cross-streak Gaussian so
        // thicker meteors are actually thicker (not just bigger empty quads).
        LLColor4 color4;
        color4.mV[VRED]   = m.color.mV[VRED]   * alpha;
        color4.mV[VGREEN] = m.color.mV[VGREEN] * alpha;
        color4.mV[VBLUE]  = m.color.mV[VBLUE]  * alpha;
        // Width_scale range is [0, 4]; pack as /4 so it fits in 8-bit unsigned.
        color4.mV[VALPHA] = llclamp(m.width_scale * 0.25f, 0.0f, 1.0f);
        const LLColor4U color_u(color4);

        for (U32 v = 0; v < kMeteorVertsPerQuad; ++v)
        {
            *(positions++) = midpoint;
            *(normals++)   = half_vec;
            *(colors++)    = color_u;
            // Scale the cross-streak coordinate by width_scale. The vertex
            // shader multiplies this by meteor_width_pixels to get the
            // screen-space half-width; the fragment shader divides by
            // width_scale to keep the Gaussian shape constant across classes.
            *(texcoords++) = LLVector2(kUV[v].mV[VX], kUV[v].mV[VY] * m.width_scale);
        }
        ++written;
    }

    // Zero-fill the unused meteor slots so they produce degenerate triangles
    // (all verts at origin) that rasterize to nothing.
    const LLColor4U black_u(0, 0, 0, 0);
    for (U32 i = written; i < kMeteorHardMax; ++i)
    {
        for (U32 v = 0; v < kMeteorVertsPerQuad; ++v)
        {
            *(positions++) = LLVector3::zero;
            *(normals++)   = LLVector3::zero;
            *(colors++)    = black_u;
            *(texcoords++) = LLVector2(0.f, 0.f);
        }
    }

    mMeteorVerts->unmapBuffer();
}

void LLVOWLSky::drawMeteors()
{
    if (mMeteorVerts.isNull()) return;
    if (mMeteors.empty()) return;   // skip the GL call entirely when nothing's active

    mMeteorVerts->setBuffer();
    mMeteorVerts->drawArrays(LLRender::TRIANGLES, 0, (S32)(kMeteorHardMax * kMeteorVertsPerQuad));
}
