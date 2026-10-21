/**
 * @file alselectionoutline.cpp
 * @brief Outlines of selected world objects, drawn on the GPU from an id target in the UI stage.
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

#include "llviewerprecompiledheaders.h"

#include "alselectionoutline.h"

#include "lldrawable.h"
#include "lldrawpool.h"
#include "lldrawpoolalpha.h"
#include "llface.h"
#include "llfetchedgltfmaterial.h"
#include "llframetimer.h"
#include "llglstates.h"
#include "lltextureentry.h"
#include "llui.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerdisplay.h"
#include "llviewerobject.h"
#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llviewertexture.h"
#include "llvovolume.h"
#include "pipeline.h"
#include "rlvactions.h"

#include <iterator>

namespace
{
    // Whether a texture's alpha can leave anything out, as LLPipeline::getPoolTypeFromTE reads it: media is drawn
    // opaque whatever its format.
    bool texture_has_alpha(const LLViewerTexture* texture)
    {
        const S8 components = texture->getComponents();
        return (components == 4 && texture->getType() != LLViewerTexture::MEDIA_TEXTURE) || components == 2;
    }

    // The highest alpha of what a face samples, 255 where it is not known (LLImageGL::getMaxAlpha).
    U8 texture_max_alpha(const LLViewerTexture* texture)
    {
        const LLImageGL* image = texture->getGLTexture();
        return image ? image->getMaxAlpha() : 255;
    }

    // Which faces a view draws whole (View::mWholeFaces, View::mWholeGlow): Highlight Transparent, which RLVa already
    // gates, draws them so in the scene, and AlchemySelectionOutlineGeometry asks for them where RLVa would allow it.
    void set_whole_faces(ALSelectionOutline::View& view)
    {
        static LLCachedControl<bool> outline_geometry(gSavedSettings, "AlchemySelectionOutlineGeometry", true);
        const bool debug_alpha = LLDrawPoolAlpha::sShowDebugAlpha;
        view.mWholeFaces = debug_alpha || (outline_geometry && RlvActions::canHighlightTransparent());
        view.mWholeGlow = debug_alpha;
    }

    // The contour width setting in force (ALSelectionOutline::contourSetting).
    F32 contour_setting()
    {
        static LLCachedControl<bool> wireframe(gSavedSettings, "AlchemySelectionWireframe", false);
        static LLCachedControl<F32> width(gSavedSettings, "AlchemySelectionOutlineWidth",
                                          ALSelectionOutline::DEFAULT_CONTOUR_WIDTH);
        static LLCachedControl<F32> wireframe_width(gSavedSettings, "AlchemySelectionOutlineWireframeWidth",
                                                    ALSelectionOutline::DEFAULT_WIREFRAME_CONTOUR_WIDTH);
        return ALSelectionOutline::contourSetting(wireframe, width, wireframe_width);
    }

    // The wireframe's width setting (AlchemySelectionWireframeWidth).
    F32 wire_setting()
    {
        static LLCachedControl<F32> wire_width(gSavedSettings, "AlchemySelectionWireframeWidth",
                                               ALSelectionOutline::DEFAULT_WIRE_WIDTH);
        return wire_width;
    }

    // What every view takes from the settings beyond its matrices and widths.
    void set_view_settings(ALSelectionOutline::View& view)
    {
        static LLCachedControl<S32> contrast(gSavedSettings, "AlchemySelectionOutlineContrast",
                                             ALSelectionOutline::CONTRAST_RIM);
        set_whole_faces(view);
        view.mContrast = ALSelectionOutline::contrastMode(contrast);
    }
}

ALSelectionOutline::ALSelectionOutline()
{
    mIdMap.setLabel("outline.ids");
    mJumpMap.setLabel("outline.jumps");
    mTileMap.setLabel("outline.tiles");
    mGlowReachMap.setLabel("outline.glow.reach");
    mGlowRowMap.setLabel("outline.glow.rows");
    mBackdropMap.setLabel("outline.backdrop");
}

ALSelectionOutline::View ALSelectionOutline::worldView()
{
    View view = makeView(LLViewerCamera::getCurrent().getModelview(), gGL.getProjectionMatrix(), contour_setting(),
                         LLUI::getScaleFactor().mV[VX], false, LLPipeline::RenderHighlightThickness,
                         LLPipeline::RenderHighlightBrightness, wire_setting());
    set_view_settings(view);
    return view;
}

bool ALSelectionOutline::hudView(View& view)
{
    LLMatrix4a projection;
    LLMatrix4a modelview;
    if (!get_hud_matrices(projection, modelview))
    {
        return false;
    }
    view = makeView(modelview, projection, contour_setting(), LLUI::getScaleFactor().mV[VX], true,
                    LLPipeline::RenderHighlightThickness, LLPipeline::RenderHighlightBrightness, wire_setting());
    set_view_settings(view);
    return true;
}

void ALSelectionOutline::add(LLViewerObject* object, U32 te_mask, const LLColor4& colour, bool show_hidden,
                             EPriority priority, U32 parts)
{
    if (!object)
    {
        return;
    }

    // What glows is drawn with nothing else, and only where the scene shows it.
    const Entry entry = { object, te_mask, colour, show_hidden && priority != PRIORITY_HOVER, priority,
                          partsFor(priority, parts) };

    const auto found = mEntryIndex.find(object);
    if (found != mEntryIndex.end())
    {
        Entry& added = mEntries[found->second];
        if (replaces(added.mPriority, priority))
        {
            added = entry;
        }
        return;
    }

    if (mEntries.size() >= MAX_IDS)
    {
        return;
    }
    mEntryIndex.emplace(object, (U32)mEntries.size());
    mEntries.push_back(entry);
}

void ALSelectionOutline::clearEntries()
{
    mEntries.clear();
    mEntryIndex.clear();
}

void ALSelectionOutline::release()
{
    if (mIdMap.isComplete())
    {
        mIdMap.release();
    }
    if (mJumpMap.isComplete())
    {
        mJumpMap.release();
    }
    if (mTileMap.isComplete())
    {
        mTileMap.release();
    }
    if (mGlowReachMap.isComplete())
    {
        mGlowReachMap.release();
    }
    if (mGlowRowMap.isComplete())
    {
        mGlowRowMap.release();
    }
    if (mBackdropMap.isComplete())
    {
        mBackdropMap.release();
    }
    if (mPalette)
    {
        LLImageGL::deleteTextures(1, &mPalette);
        mPalette = 0;
    }
    mPaletteRows = 0;
    mUploadedTexels.clear();
}

void ALSelectionOutline::releaseStale(U32 frame, F64 now)
{
    // Called every frame, so it touches nothing that is not there.
    if (mIdMap.isComplete() && stale(mIdUse.mFrame, mIdUse.mTime, frame, now))
    {
        mIdMap.release();
    }
    if (stale(mEdgeUse.mFrame, mEdgeUse.mTime, frame, now))
    {
        if (mJumpMap.isComplete())
        {
            mJumpMap.release();
        }
        if (mTileMap.isComplete())
        {
            mTileMap.release();
        }
    }
    if (stale(mGlowUse.mFrame, mGlowUse.mTime, frame, now))
    {
        if (mGlowReachMap.isComplete())
        {
            mGlowReachMap.release();
        }
        if (mGlowRowMap.isComplete())
        {
            mGlowRowMap.release();
        }
    }
    if (mBackdropMap.isComplete() && stale(mBackdropUse.mFrame, mBackdropUse.mTime, frame, now))
    {
        mBackdropMap.release();
    }
    if (mPalette && stale(mPaletteUse.mFrame, mPaletteUse.mTime, frame, now))
    {
        LLImageGL::deleteTextures(1, &mPalette);
        mPalette = 0;
        mPaletteRows = 0;
        mUploadedTexels.clear();
    }
}

void ALSelectionOutline::render(const View& view)
{
    const Use use = { LLFrameTimer::getFrameCount(), LLFrameTimer::getTotalSeconds() };
    if (mEntries.empty())
    {
        releaseStale(use.mFrame, use.mTime);
        return;
    }

    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    // The window's viewport: the final blit filled it from the scene, the HUD is drawn over it, and the outlines
    // are laid over both.
    const S32 vp_x = gGLViewport[0];
    const S32 vp_y = gGLViewport[1];
    const S32 width = gGLViewport[2];
    const S32 height = gGLViewport[3];

    Wants wants;
    for (const Entry& entry : mEntries)
    {
        wants.mContour = wants.mContour || (entry.mParts & PART_CONTOUR);
        wants.mWireframe = wants.mWireframe || (entry.mParts & PART_WIREFRAME);
        wants.mGlow = wants.mGlow || entry.mPriority == PRIORITY_HOVER;
    }
    const Passes passes = passesFor(wants, view.mHUD, gSelectionWireframeProgram.isComplete() &&
                                                         gSelectionWireframeProgram.mRiggedVariant != nullptr);
    if (!passes.any() || width <= 0 || height <= 0 || !gPipeline.mRT || !gPipeline.mScreenTriangleVB ||
        (passes.mIds && (!gSelectionIdProgram.isComplete() || !gSelectionIdProgram.mRiggedVariant)) ||
        (passes.mEdges && (!gSelectionJumpProgram.isComplete() || !gSelectionTileProgram.isComplete() ||
                           !gSelectionOutlineProgram.isComplete())))
    {
        releaseStale(use.mFrame, use.mTime);
        clearEntries();
        return;
    }

    LLMatrix4a mvp;
    mvp.setMul(view.mModelview, view.mProjection);

    // Ids in priority order: id i + 1 is mEntries[mOrder[i]].
    const U32 count = (U32)mEntries.size();
    mPriorities.resize(count);
    for (U32 i = 0; i < count; ++i)
    {
        mPriorities[i] = mEntries[i].mPriority;
    }
    priorityOrder(mPriorities, mOrder);

    // What this frame draws on screen, and where. The HUD's wireframe tests the id pass's depth, so it needs its
    // faces there; the world's needs nothing of it.
    collectFaces(view, width, height, mvp);
    const bool ids = passes.mIds && !mRects.empty();
    const bool edges = passes.mEdges && ids;
    const bool contour = edges && wants.mContour;
    const bool wireframe = passes.mWireframe && !mLineRects.empty() && (!view.mHUD || ids);
    const bool backdrop = readsBackdrop(view, contour, wireframe) && gSelectionBackdropMaskProgram.isComplete();
    if (!ids && !wireframe)
    {
        releaseStale(use.mFrame, use.mTime);
        clearEntries();
        return;
    }

    // The targets the passes that run read are made at the window's size; the others are given up once they are
    // stale.
    bool allocated = true;
    if (ids)
    {
        if (!mIdMap.isComplete() || mIdMap.getWidth() != (U32)width || mIdMap.getHeight() != (U32)height)
        {
            allocated = mIdMap.allocate(width, height, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE,
                                        LLRenderTarget::MIPS_NONE, LLPipeline::mainDepthFormat());
        }
        mIdUse = use;
    }
    if (edges)
    {
        if (!mJumpMap.isComplete() || !mTileMap.isComplete() || mJumpMap.getWidth() != (U32)width ||
            mJumpMap.getHeight() != (U32)height)
        {
            allocated = allocated && mJumpMap.allocate(width, height, GL_R8) &&
                        mTileMap.allocate(tileCount((U32)width), tileCount((U32)height), GL_RGBA16) &&
                        mTileMap.addColorAttachment(GL_R32F);
        }
        mEdgeUse = use;
    }
    // The glow is drawn where its passes' programs loaded; without them what glows is drawn with nothing.
    const bool glow = edges && wants.mGlow && gSelectionGlowReachProgram.isComplete() &&
                      gSelectionGlowRowProgram.isComplete();
    if (glow)
    {
        if (!mGlowReachMap.isComplete() || !mGlowRowMap.isComplete() || mGlowRowMap.getWidth() != (U32)width ||
            mGlowRowMap.getHeight() != (U32)height)
        {
            allocated = allocated && mGlowReachMap.allocate(tileCount((U32)width), tileCount((U32)height), GL_RGBA16) &&
                        mGlowRowMap.allocate(width, height, GL_R8);
        }
        mGlowUse = use;
    }
    if (backdrop)
    {
        // Whole squares of BACKDROP_SCALE pixels: an odd window's last column and row take the texels beside them.
        const U32 backdrop_width = (U32)llmax(width / BACKDROP_SCALE, 1);
        const U32 backdrop_height = (U32)llmax(height / BACKDROP_SCALE, 1);
        if (!mBackdropMap.isComplete() || mBackdropMap.getWidth() != backdrop_width ||
            mBackdropMap.getHeight() != backdrop_height)
        {
            allocated = allocated && mBackdropMap.allocate(backdrop_width, backdrop_height, GL_RGBA8);
        }
        mBackdropUse = use;
    }
    mPaletteUse = use;
    releaseStale(use.mFrame, use.mTime);
    if (!allocated)
    {
        LL_WARNS_ONCE("Pipeline") << "Could not allocate the selection outline's targets" << LL_ENDL;
        release();
        clearEntries();
        return;
    }

    LL_PROFILE_GPU_ZONE("selection outline");

    const U32 rows = paletteRows(count);
    mPaletteTexels.assign((size_t)PALETTE_WIDTH * rows * 4, 0);
    for (U32 i = 0; i < count; ++i)
    {
        const Entry& entry = mEntries[mOrder[i]];
        writePalette(mPaletteTexels, i + 1, entry.mColour, entry.mShowHidden);
    }

    LLGLSLShader* previous = LLGLSLShader::sCurBoundShaderPtr;
    gGL.flush();

    if (ids)
    {
        drawIds(view, width, height);
    }

    // The same colours as the frame before upload nothing.
    if (!mPalette || mUploadedTexels != mPaletteTexels)
    {
        uploadPalette(mPalette, mPaletteRows, mPaletteTexels, rows);
        mUploadedTexels = mPaletteTexels;
    }

    // What the lines are drawn over, copied before either draws, around where they are drawn; under the contour's
    // the texels showing a selected surface are marked, which it does not read.
    LLRenderTarget* backdrop_map = nullptr;
    if (backdrop)
    {
        std::vector<LLRect> copied;
        copied.reserve(mLineRects.size());
        for (const LLRect& rect : mLineRects)
        {
            const LLRect from = backdropRect(rect, width, height);
            if (from.notEmpty())
            {
                copied.push_back(from);
            }
        }
        mergeRects(copied);
        for (const LLRect& from : copied)
        {
            mBackdropMap.copyFromBound(vp_x + from.mLeft, vp_y + from.mBottom, vp_x + from.mRight, vp_y + from.mTop,
                                       from.mLeft / BACKDROP_SCALE, from.mBottom / BACKDROP_SCALE,
                                       from.mRight / BACKDROP_SCALE, from.mTop / BACKDROP_SCALE, GL_LINEAR);
        }
        if (!copied.empty())
        {
            if (contour)
            {
                gSelectionBackdropMaskProgram.bind();
                drawBackdropMask(gSelectionBackdropMaskProgram, mIdMap, mBackdropMap, copied,
                                 *gPipeline.mScreenTriangleVB);
            }
            backdrop_map = &mBackdropMap;
        }
    }

    // The wireframe first, the outline over it.
    if (wireframe)
    {
        drawWireframes(view, width, height, backdrop_map);
    }

    if (edges)
    {
        // The rects hold every texel drawn and the reach of its line or glow around them; the edge pass reads no
        // further, and what it reads past the jump and tile passes' work they clear to nothing, as it is. A frame
        // with no contour reads no jump, but the tile pass reads them all.
        gSelectionJumpProgram.bind();
        if (contour)
        {
            drawJumps(gSelectionJumpProgram, mIdMap, mJumpMap, mRects, view, *gPipeline.mScreenTriangleVB);
        }
        else
        {
            clearJumps(mJumpMap);
        }
        gSelectionTileProgram.bind();
        drawTiles(gSelectionTileProgram, mIdMap, mJumpMap, mTileMap, mRects, view, *gPipeline.mScreenTriangleVB);
        if (glow)
        {
            gSelectionGlowReachProgram.bind();
            drawGlowReach(gSelectionGlowReachProgram, mTileMap, mGlowReachMap, mRects, view,
                          *gPipeline.mScreenTriangleVB);
            gSelectionGlowRowProgram.bind();
            drawGlowRows(gSelectionGlowRowProgram, mIdMap, mGlowReachMap, mGlowRowMap, mRects, view,
                         *gPipeline.mScreenTriangleVB);
        }

        LLGLSColorMask mask(true, false);
        LLGLDepthTest depth(GL_FALSE);
        LLGLEnable blend(GL_BLEND);
        gGL.setSceneBlendType(LLRender::BT_ALPHA);

        gSelectionOutlineProgram.bind();
        drawEdges(gSelectionOutlineProgram, mIdMap, mJumpMap, mTileMap, glow ? &mGlowReachMap : nullptr,
                  glow ? &mGlowRowMap : nullptr, contour ? backdrop_map : nullptr, mPalette, view,
                  *gPipeline.mScreenTriangleVB, &mRects, vp_x, vp_y);
    }

    if (previous)
    {
        previous->bind();
    }
    else
    {
        LLGLSLShader::unbind();
    }

    clearEntries();
}

void ALSelectionOutline::collectFaces(const View& view, S32 width, S32 height, const LLMatrix4a& mvp)
{
    mFaces.clear();
    mRects.clear();
    mLineRects.clear();

    const S32 contour_reach = contourReach(view);
    const S32 glow_reach = lineReach(view.mGlowRadius);
    const S32 wire_reach = lineReach(view.mWireWidth);
    const LLRect target(0, height, width, 0);

    for (U32 i = 0; i < (U32)mEntries.size(); ++i)
    {
        const Entry& entry = mEntries[mOrder[i]];
        const bool in_ids = inIdPass(entry.mParts, entry.mPriority, view.mHUD);
        const bool wire = (entry.mParts & PART_WIREFRAME) != 0;
        if (!in_ids && !wire)
        {
            continue;
        }

        LLViewerObject* object = entry.mObject;
        LLDrawable* drawable = object->mDrawable;
        if (!drawable || drawable->isDead() || !drawable->getVOVolume())
        {
            continue;
        }

        // The frame each face's vertex buffer is in, as LLVolumeGeometryManager::registerFace places it. Rigged
        // faces are skinned into agent space and take none.
        const LLMatrix4a* model = nullptr;
        bool agent_extents = false;
        if (drawable->isState(LLDrawable::ANIMATED_CHILD))
        {
            model = &drawable->getWorldMatrix();
        }
        else if (drawable->isActive())
        {
            model = &drawable->getRenderMatrix();
        }
        else if (drawable->getRegion())
        {
            model = &drawable->getRegion()->mRenderMatrix;
            // A static face's box already has the region's origin in it (LLFace::genVolumeBBoxes).
            agent_extents = true;
        }

        FaceBox box;
        const size_t first = mFaces.size();
        // The TE mask holds 32 faces, more than a volume has.
        const S32 faces = llmin(llmin((S32)object->getNumTEs(), drawable->getNumFaces()), 32);
        for (S32 te = 0; te < faces; ++te)
        {
            if (!(entry.mTEMask & (1u << te)))
            {
                continue;
            }

            LLFace* face = drawable->getFace(te);
            if (!face)
            {
                continue;
            }
            // The HUD's matrices have no place for a rigged face, posed about its avatar in the world.
            const bool rigged = face->isState(LLFace::RIGGED);
            if ((rigged && view.mHUD) || (!rigged && !model))
            {
                continue;
            }
            LLVertexBuffer* buffer = face->getVertexBuffer();
            if (!buffer || face->getGeomCount() == 0 || face->getIndicesCount() == 0)
            {
                continue;
            }

            LLVector4a extents[2];
            if (rigged)
            {
                if (!face->mAvatar || !face->mSkinInfo)
                {
                    continue;
                }
                // Joint boxes, refreshed while the face is in view (LLFace::calcPixelArea); empty until they are.
                extents[0] = face->mRiggedExtents[0];
                extents[1] = face->mRiggedExtents[1];
                LLVector4a size;
                size.setSub(extents[1], extents[0]);
                if (!(size[0] > 0.f && size[1] > 0.f && size[2] > 0.f))
                {
                    box.mBounded = false;
                }
            }
            else if (agent_extents)
            {
                extents[0] = face->mExtents[0];
                extents[1] = face->mExtents[1];
            }
            else
            {
                matMulBoundBox(*model, face->mExtents, extents);
            }
            box.mLo.setMin(box.mLo, extents[0]);
            box.mHi.setMax(box.mHi, extents[1]);

            mFaces.push_back({ face, rigged ? nullptr : model, i, entry.mPriority, rigged });
        }
        if (mFaces.size() == first)
        {
            continue;
        }

        // What reaches past the faces: the glow, the contour, or the wireframe's lines alone.
        const S32 reach = entry.mPriority == PRIORITY_HOVER ? glow_reach
                          : (entry.mParts & PART_CONTOUR)   ? llmax(contour_reach, wire_reach)
                                                            : wire_reach;
        LLRect rect = target;
        if (box.mBounded)
        {
            const LLVector4a extents[2] = { box.mLo, box.mHi };
            if (!scissorRect(mvp, extents, width, height, reach, rect))
            {
                // Nothing of it can reach the window.
                mFaces.resize(first);
                continue;
            }
        }
        if (in_ids)
        {
            mRects.push_back(rect);
        }
        if (entry.mPriority != PRIORITY_HOVER)
        {
            mLineRects.push_back(rect);
        }
    }

    mergeRects(mRects);
    mergeRects(mLineRects);

    // The rigged apart, as they take another program; the lowest priority first, so that where surfaces the scene
    // does not show lie at the same depth, the highest priority's is drawn last and keeps the texel, and its lines
    // are laid over the others'; then by what the draws share.
    std::sort(mFaces.begin(), mFaces.end(), [](const FaceDraw& a, const FaceDraw& b)
    {
        if (a.mRigged != b.mRigged)
        {
            return !a.mRigged;
        }
        if (a.mPriority != b.mPriority)
        {
            return a.mPriority > b.mPriority;
        }
        if (a.mRigged)
        {
            if (a.mFace->mAvatar != b.mFace->mAvatar)
            {
                return std::less<const LLVOAvatar*>()(a.mFace->mAvatar, b.mFace->mAvatar);
            }
            if (a.mFace->mSkinInfo != b.mFace->mSkinInfo)
            {
                return std::less<const LLMeshSkinInfo*>()(a.mFace->mSkinInfo, b.mFace->mSkinInfo);
            }
        }
        else if (a.mModel != b.mModel)
        {
            return std::less<const LLMatrix4a*>()(a.mModel, b.mModel);
        }
        const LLVertexBuffer* a_buffer = a.mFace->getVertexBuffer();
        const LLVertexBuffer* b_buffer = b.mFace->getVertexBuffer();
        if (a_buffer != b_buffer)
        {
            return std::less<const LLVertexBuffer*>()(a_buffer, b_buffer);
        }
        if (a.mIndex != b.mIndex)
        {
            return a.mIndex < b.mIndex;
        }
        return a.mFace->getGeomIndex() < b.mFace->getGeomIndex();
    });
}

template <typename Drawn, typename Begin>
void ALSelectionOutline::drawFaceList(LLGLSLShader& program, bool rigged, const View& view, Drawn&& drawn, Begin&& begin)
{
    // Joint palettes and the base colour transform are given to the bound program, which this one is not yet.
    mLastAvatar = nullptr;
    mLastMeshId = 0;
    mSkipLastSkin = false;
    TransformCache transform;

    const LLMatrix4a* loaded = nullptr;
    bool any_loaded = false;
    U32 index = U32_MAX;
    for (const FaceDraw& draw : mFaces)
    {
        if (draw.mRigged != rigged || !drawn(draw))
        {
            continue;
        }

        LLFace* face = draw.mFace;
        LLVertexBuffer* buffer = face->getVertexBuffer();
        if (rigged &&
            !LLRenderPass::uploadMatrixPalette(face->mAvatar, face->mSkinInfo, mLastAvatar, mLastMeshId, mSkipLastSkin))
        {
            continue;
        }
        if (!any_loaded || draw.mModel != loaded)
        {
            gGL.loadMatrix(view.mModelview);
            if (draw.mModel)
            {
                gGL.multMatrix(draw.mModel->getF32ptr());
            }
            loaded = draw.mModel;
            any_loaded = true;
        }
        if (draw.mIndex != index)
        {
            begin(draw);
            index = draw.mIndex;
        }

        const FaceBinding binding = bindFace(program, face, wholeFaces(view, draw.mPriority), transform);
        drawFace(*buffer, face->getGeomIndex(), face->getGeomIndex() + face->getGeomCount() - 1, face->getIndicesCount(),
                 face->getIndicesStart(), binding.mDoubleSided);

        if (binding.mAnimated)
        {
            gGL.matrixMode(LLRender::MM_TEXTURE0);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
        }
    }
}

void ALSelectionOutline::drawIds(const View& view, S32 width, S32 height)
{
    const bool any_rigged =
        std::any_of(mFaces.begin(), mFaces.end(), [](const FaceDraw& draw) { return draw.mRigged; });
    mIdMap.bindTarget();
    {
        // Depth writes on before the clear, which they mask.
        IdPassState state;

        gGL.setClearColor(LLColor4(0.f, 0.f, 0.f, 0.f));
        mIdMap.clear();

        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.pushMatrix();
        gGL.loadMatrix(view.mProjection);
        // A face's texture animation is loaded for its draw alone (bindFace); the others' is the identity.
        gGL.matrixMode(LLRender::MM_TEXTURE0);
        gGL.pushMatrix();
        gGL.loadIdentity();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.pushMatrix();

        // Static faces through the program, rigged ones through its rigged variant: the depth test, not the
        // order, decides which surface each pixel keeps, but where two surfaces the scene does not show lie at the
        // cleared depth, the later.
        for (bool rigged : { false, true })
        {
            if (rigged && !any_rigged)
            {
                break;
            }
            LLGLSLShader& program = rigged ? *gSelectionIdProgram.mRiggedVariant : gSelectionIdProgram;
            gSelectionIdProgram.bind(rigged);
            bindIdPass(program, view.mHUD ? nullptr : &gPipeline.mRT->deferredScreen, width, height);
            drawFaceList(program, rigged, view,
                         [this, &view](const FaceDraw& draw)
                         {
                             const Entry& entry = mEntries[mOrder[draw.mIndex]];
                             return inIdPass(entry.mParts, entry.mPriority, view.mHUD);
                         },
                         [this, &program](const FaceDraw& draw)
                         {
                             // An object drawn for the HUD wireframe's depth alone is not outlined: it hides what
                             // lies behind it and draws no line of its own.
                             const Entry& entry = mEntries[mOrder[draw.mIndex]];
                             setId(program, draw.mIndex + 1, entry.mPriority, entry.mShowHidden,
                                   outlined(entry.mParts, entry.mPriority));
                         });
            program.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
            program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
        }

        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.popMatrix();
        gGL.matrixMode(LLRender::MM_TEXTURE0);
        gGL.popMatrix();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.popMatrix();
    }
    mIdMap.flush();
}

void ALSelectionOutline::drawWireframes(const View& view, S32 width, S32 height, LLRenderTarget* backdrop)
{
    LL_PROFILE_GPU_ZONE("selection wireframe");

    // The window's depth is the final blit's point-sampled copy of the scene's, taken a render texel at a time.
    F32 resample = 1.f;
    if (!view.mHUD && gPipeline.mRT->deferredScreen.getWidth() > 0)
    {
        resample = (F32)width / (F32)gPipeline.mRT->deferredScreen.getWidth();
    }
    const F32 offset_factor = wireOffsetFactor(resample);

    bool any_hidden = false;
    for (const Entry& entry : mEntries)
    {
        any_hidden = any_hidden || inWirePass(entry.mParts, entry.mShowHidden, WIRE_HIDDEN);
    }
    const bool any_rigged =
        std::any_of(mFaces.begin(), mFaces.end(), [](const FaceDraw& draw) { return draw.mRigged; });

    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.pushMatrix();
    gGL.loadMatrix(view.mProjection);
    gGL.matrixMode(LLRender::MM_TEXTURE0);
    gGL.pushMatrix();
    gGL.loadIdentity();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.pushMatrix();

    const EWirePass world_passes[] = { WIRE_VISIBLE, WIRE_HIDDEN };
    const EWirePass hud_passes[] = { WIRE_ID_DEPTH };
    const EWirePass* passes = view.mHUD ? hud_passes : world_passes;
    const U32 pass_count = view.mHUD ? 1 : 2;
    for (U32 p = 0; p < pass_count; ++p)
    {
        const EWirePass pass = passes[p];
        if (pass == WIRE_HIDDEN && !any_hidden)
        {
            continue;
        }

        WireState state(pass, offset_factor);
        for (bool rigged : { false, true })
        {
            if (rigged && !any_rigged)
            {
                break;
            }
            LLGLSLShader& program = rigged ? *gSelectionWireframeProgram.mRiggedVariant : gSelectionWireframeProgram;
            gSelectionWireframeProgram.bind(rigged);
            bindWirePass(program, pass, view.mWireWidth, width, height, mPalette, &mIdMap, view.mContrast, backdrop);
            // What glows, or is outlined alone, has no wireframe.
            drawFaceList(program, rigged, view,
                         [this, pass](const FaceDraw& draw)
                         {
                             const Entry& entry = mEntries[mOrder[draw.mIndex]];
                             return inWirePass(entry.mParts, entry.mShowHidden, pass);
                         },
                         [this, &program](const FaceDraw& draw)
                         {
                             const Entry& entry = mEntries[mOrder[draw.mIndex]];
                             setId(program, draw.mIndex + 1, entry.mPriority, entry.mShowHidden);
                         });
            program.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
            program.unbindTexture(LLShaderMgr::SELECTION_PALETTE);
            program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
            program.unbindTexture(LLShaderMgr::SELECTION_BACKDROP_MAP);
        }
    }

    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.popMatrix();
    gGL.matrixMode(LLRender::MM_TEXTURE0);
    gGL.popMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.popMatrix();
}

ALSelectionOutline::FaceBinding ALSelectionOutline::bindFace(LLGLSLShader& program, LLFace* face, bool whole,
                                                             TransformCache& bound)
{
    const LLTextureEntry* te = face->getTextureEntry();
    LLViewerTexture* texture = face->getTexture();
    F32 cutoff = NO_ALPHA_TEST;
    LLUUID texture_id;
    U8 max_alpha = 255;
    LLGLTFMaterial::TextureTransform::Pack transform;
    std::copy(std::begin(IDENTITY_TRANSFORM), std::end(IDENTITY_TRANSFORM), std::begin(transform));
    FaceBinding binding;

    auto* gltf = te ? static_cast<LLFetchedGLTFMaterial*>(te->getGLTFRenderMaterial()) : nullptr;
    if (gltf)
    {
        // As LLRenderPass::pushGLTFBatch culls it, media or not.
        binding.mDoubleSided = gltf->mDoubleSided;

        // The base colour, unless the face plays media, which LLVolumeGeometryManager::registerFace hands
        // LLFetchedGLTFMaterial::bind in its place.
        if (!face->hasMedia() || !texture || texture->getType() != LLViewerTexture::MEDIA_TEXTURE)
        {
            texture = gltf->mBaseColorTexture.get();
        }
        if (texture && texture_has_alpha(texture))
        {
            cutoff = gltfAlphaCutoff(gltf->mAlphaMode, gltf->mAlphaCutoff);
            texture_id = texture->getID();
            max_alpha = texture_max_alpha(texture);
        }
        gltf->mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR].getPacked(transform);
    }
    else if (te && texture)
    {
        // The diffuse map as the face names it, whatever stands in for it while it loads.
        const LLMaterial* material = te->getMaterialParams().get();
        cutoff = legacyAlphaCutoff(material != nullptr, material ? material->getDiffuseAlphaMode() : 0,
                                   material ? material->getAlphaMaskCutoff() : 0, texture_has_alpha(texture));
        texture_id = te->getID();
        max_alpha = texture_max_alpha(texture);
    }

    // Cut by its texture's alpha alone, never its colour's; but a face the scene draws nothing of, as a prim made
    // invisible by its colour's alpha, is kept whole as surface the scene does not show.
    const AlphaTest test = faceAlphaTest(texture_id, max_alpha, cutoff, whole, LLVOVolume::isInvisibleFace(te));
    setAlphaTest(program, test, transform, &bound);
    if (test.mCutoff <= 0.f || test.mCutoff > 1.f || !texture)
    {
        return binding;
    }

    // Sampled as LLRenderPass::pushBatch samples the diffuse map; alpha is the same with or without sRGB decode.
    program.bindTexture(LLShaderMgr::DIFFUSE_MAP, texture, ALSamplers::AnisoWrap);

    // A texture animation the face is drawn with rather than baked into its texture coordinates, as
    // LLVolumeGeometryManager::registerFace hands it to the draw.
    if (face->mTextureMatrix && face->isState(LLFace::TEXTURE_ANIM) && face->getVirtualSize() > MIN_TEX_ANIM_SIZE)
    {
        gGL.matrixMode(LLRender::MM_TEXTURE0);
        gGL.loadMatrix((const GLfloat*)face->mTextureMatrix->mMatrix);
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        binding.mAnimated = true;
    }
    return binding;
}
