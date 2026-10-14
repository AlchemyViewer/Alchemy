/**
 * @file alwatersurface.h
 * @brief The water's field for the frame: its layout around the camera and the map of cell water heights.
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

#include "alwaterfield.h"

#include "indra_constants.h"
#include "llpointer.h"
#include "lluuid.h"
#include "v2math.h"
#include "v3math.h"
#include "v4math.h"

#include <memory>
#include <vector>

class LLCamera;
class LLGLSLShader;
class LLImageGL;
class LLImageRaw;
class LLVector3;
class LLViewerTexture;

/// The water's field for the frame: ALWaterField's layout around the main camera, and the cell map the water's shaders
/// read the heights from, ALWaterField::MAP_SIZE texels a side of RG32F (height, flags) as fillCells lays them out.
/// The water is drawn from these alone (draw), with no vertex buffer, object or spatial partition.
/// The map is refilled when what it holds changes -- the loaded regions, their water heights, the agent region's
/// height that empty cells take, or the square under the camera -- which a key of those, compared each frame, finds
/// without a trigger at each place they change.
///
/// Owned by LLPipeline. Its texture goes with the pipeline's other GL objects and is made again on the next update.
class ALWaterSurface
{
public:
    /// Lays the field out around the main camera and refills the map if its key moved, and follows the water pool's
    /// wave normal maps (trackMaps). Once a main frame, at its start, before anything draws or fits to the water, with
    /// the context current; without an agent region nothing changes, and with RenderWater off there is no field.
    void update();

    /// Whether there is a field to draw.
    bool exists() const { return mCellMap.notNull(); }

    /// Releases the GL textures: the cell map and the maps' heights. What the maps' pixels gave is kept, and their
    /// heights are uploaded again when next wanted.
    void release();

    /// Binds the cell map and sets the uniforms environment/waterSurface.glsl reads on `shader`, bound: the square,
    /// the rings, the agent region's height, and whether the void is drawn (RENDER_TYPE_VOIDWATER). Before the first
    /// update with an agent region, every point takes the agent region's height.
    void bind(LLGLSLShader& shader) const;

    /// Draws the field with `shader`, bound and bound to the field (bind): ALWaterField::fieldDraws, instances whose
    /// vertices the vertex stage places (water_vertex). With `patched`, where the waves' patches are drawn too
    /// (bindWaves with them drawn), the cells beside them are drawn as the fringe. Nothing before the first update.
    void draw(LLGLSLShader& shader, bool patched) const;

    /// Whether the water is drawn with waves this frame: AlchemyRenderWaterWaves where tessellation is, a cell within
    /// the reach, and a map whose heights have landed. Captures always draw the field flat.
    bool wavesDrawn() const { return mWavesDrawn; }

    /// A, the most the waves move the surface this frame, in metres; 0 without waves.
    F32 maxDisplacement() const { return mWavesDrawn ? mMaxDisplacement : 0.f; }

    /// The waves' patches drawn this frame: PATCHES_PER_CELL squared for each cell within the reach; 0 without waves.
    S32 patchCount() const;

    /// Sets what the water's own draws need for the waves on `shader`, bound and bound to the field: where this view
    /// draws them (`drawn`, never without wavesDrawn), the cells the patches cover, which a flat draw then leaves out,
    /// and for the waves' program (`patches`), their tessellation, cull, order and heights, the maps bound as the
    /// current (`a`) and next (`b`) map giving those. Where it does not, no cell is left out.
    void bindWaves(LLGLSLShader& shader, bool drawn, bool patches, const LLViewerTexture* a, const LLViewerTexture* b) const;

    /// Draws the waves' patches with the waves' program, bound and bound to the field and the waves (bindWaves).
    void drawPatches(LLGLSLShader& shader) const;

    /// Whether `camera`'s frustum meets the cells the patches cover: their rectangle, from the lowest water to the
    /// highest and A past each. False without waves.
    bool patchesInView(LLCamera& camera) const;

    /// The field's outer rectangle, horizontally, relative to `eye` in agent space: the rim each way from the square's
    /// centre. False before the first update.
    bool rim(const LLVector3& eye, LLVector2& lo, LLVector2& hi) const;

    /// Whether `camera`'s frustum meets the field at all: its slab, from rim to rim and from its lowest water to its
    /// highest, and A past each for the waves. False only where the view misses the whole sea, as looking up.
    bool inView(LLCamera& camera) const;

    /// Grows `min` and `max` (agent space) to hold the field: its rim, and its lowest and highest water, A past each.
    void addExtents(LLVector3& min, LLVector3& max) const;

    /// waterSurface.glsl's waterMapMean for the maps bound as the current (`a`) and next (`b`) map: each one's mean
    /// normal xy, zero for a map whose pixels have not arrived or that is null.
    LLVector4 mapMeans(const LLViewerTexture* a, const LLViewerTexture* b) const;

    /// A wave map's baked heights: the big layer's field in r and the mid layers' in g, in repeats of the map, RG16F
    /// with every mip; the largest each displaces the surface by, in metres, at a wave height of 1; and how far its
    /// waves have risen since the bake landed, 0 to 1 over RAMP_SECONDS, so a late bake does not pop. A null texture,
    /// maxima of 0 and no rise until then.
    struct Heights
    {
        LLImageGL* mTexture = nullptr;
        F32        mMaxBig = 0.f;
        F32        mMaxMid = 0.f;
        F32        mRise = 0.f;
    };
    Heights mapHeights(const LLViewerTexture* map) const;

    static constexpr F64 RAMP_SECONDS = 1.0;

    /// The waves' settings, held to their ranges: the wave height dial and the tessellation density.
    struct WaveSettings
    {
        F32 mHeight = 0.5f;
        F32 mDensity = 32.f;
    };
    static WaveSettings waveSettings();

    /// Whether the waves are wanted this session: AlchemyRenderWaterWaves, where tessellation is (GL 4.0). The waves'
    /// programs, and the heights they read, exist only then.
    static bool wavesWanted();

    const ALWaterField::Layout& getLayout() const { return mLayout; }

    /// The cell map, null until the first update with an agent region.
    LLImageGL* getCellMap() const { return mCellMap; }

private:
    struct Key
    {
        S32                               mCentreX = 0;
        S32                               mCentreY = 0;
        S32                               mCells = 0;
        F32                               mDefaultHeight = 0.f;
        std::vector<ALWaterField::Region> mRegions;

        bool operator==(const Key&) const = default;
    };

    /// What a wave normal map's pixels gave, by asset id. The heights' texels outlive a release of the GL textures, which
    /// are uploaded from them again.
    struct MapStats
    {
        LLUUID               mID;
        LLVector2            mMean;
        bool                 mKnown = false;        ///< the mean has landed
        bool                 mHeightsBaked = false; ///< the heights have landed, or the map had none to give
        bool                 mBaking = false;       ///< its pixels are on the General queue
        U32                  mUsed = 0;             ///< the frame it was last tracked, for the cache's eviction
        std::shared_ptr<const std::vector<U16>> mTexels; ///< the heights as RG16F texels; null without heights
        S32                  mTexelsWidth = 0;
        S32                  mTexelsHeight = 0;
        LLPointer<LLImageGL> mHeights;              ///< the texels uploaded; null until uploaded, and after a release
        F32                  mMaxBig = 0.f;
        F32                  mMaxMid = 0.f;
        F64                  mLanded = 0.0;         ///< LLFrameTimer::getTotalSeconds() when the heights landed
    };
    static constexpr size_t MAP_CACHE = 4;

    /// What a bake hands back to the main thread.
    struct Baked;

    /// Follows the water pool's two wave normal maps, current and next, for what the water's shaders take from their
    /// pixels: with `means` each map's mean slope, which only the tiled reads take, and with `heights` the heights its
    /// waves are displaced by (ALWaterHeightBake), both worked on the General queue, the heights uploaded here when
    /// wanted. A map's full-resolution pixels are asked for only when something wanted is still to come from them, read
    /// in place, and let go once a bake holds them. What they gave is kept for the last few maps seen.
    void            trackMaps(LLViewerTexture* current, LLViewerTexture* next, bool means, bool heights);
    MapStats&       mapStats(const LLUUID& id);
    MapStats*       findMapStats(const LLUUID& id);
    const MapStats* findMapStats(const LLUUID& id) const;
    void            bakeMap(MapStats& stats, LLPointer<LLImageRaw> raw, bool heights);
    /// What a bake gave, or with null, that it failed: the map then gives no heights and the neutral mean.
    void            land(const LLUUID& id, const Baked* baked);
    void            uploadHeights(MapStats& stats);

    ALWaterField::Layout mLayout;
    LLVector4            mSquare;
    LLVector2            mCentre;
    F32                  mDefaultHeight = DEFAULT_WATER_HEIGHT;
    // The camera the layout is around, in agent space: the waves' fade follows it in every view.
    LLVector3            mEye;
    F32                  mLowest = DEFAULT_WATER_HEIGHT;
    F32                  mHighest = DEFAULT_WATER_HEIGHT;
    // RENDER_TYPE_VOIDWATER as the frame began: the passes that draw the water run under render-type masks that
    // leave it out.
    bool                 mVoidDrawn = true;
    bool                 mWavesDrawn = false;
    F32                  mMaxDisplacement = 0.f;
    // The maps tracked this frame, current and next, whose heights the waves' bound is taken from.
    LLUUID               mTracked[2];
    Key                  mKey;
    Key                  mScratch;
    std::vector<F32>     mTexels;
    LLPointer<LLImageGL> mCellMap;
    std::vector<MapStats> mMaps;
};
