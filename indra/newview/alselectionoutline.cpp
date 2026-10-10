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
#include "llface.h"
#include "llfetchedgltfmaterial.h"
#include "llframetimer.h"
#include "llglstates.h"
#include "lltextureentry.h"
#include "llui.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerobject.h"
#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llviewertexture.h"
#include "llvovolume.h"
#include "pipeline.h"

#include <iterator>

// llviewerdisplay.cpp: the matrices the HUD's attachments are drawn with.
bool get_hud_matrices(LLMatrix4a& proj, LLMatrix4a& model);

namespace
{
    // Whether a texture's alpha can leave anything out, as LLPipeline::getPoolTypeFromTE reads it: media is drawn
    // opaque whatever its format.
    bool texture_has_alpha(const LLViewerTexture* texture)
    {
        const S8 components = texture->getComponents();
        return (components == 4 && texture->getType() != LLViewerTexture::MEDIA_TEXTURE) || components == 2;
    }
}

ALSelectionOutline::View ALSelectionOutline::worldView()
{
    static LLCachedControl<F32> contour_width(gSavedSettings, "AlchemySelectionOutlineWidth", DEFAULT_CONTOUR_WIDTH);
    return makeView(LLViewerCamera::getCurrent().getModelview(), gGL.getProjectionMatrix(), contour_width,
                    LLUI::getScaleFactor().mV[VX], false, LLPipeline::RenderHighlightThickness,
                    LLPipeline::RenderHighlightBrightness);
}

bool ALSelectionOutline::hudView(View& view)
{
    static LLCachedControl<F32> contour_width(gSavedSettings, "AlchemySelectionOutlineWidth", DEFAULT_CONTOUR_WIDTH);
    LLMatrix4a projection;
    LLMatrix4a modelview;
    if (!get_hud_matrices(projection, modelview))
    {
        return false;
    }
    view = makeView(modelview, projection, contour_width, LLUI::getScaleFactor().mV[VX], true,
                    LLPipeline::RenderHighlightThickness, LLPipeline::RenderHighlightBrightness);
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
    if (mMarkedMap.isComplete())
    {
        mMarkedMap.release();
    }
    if (mTileMap.isComplete())
    {
        mTileMap.release();
    }
    if (mPalette)
    {
        LLImageGL::deleteTextures(1, &mPalette);
        mPalette = 0;
    }
    mPaletteRows = 0;
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
        if (mMarkedMap.isComplete())
        {
            mMarkedMap.release();
        }
        if (mTileMap.isComplete())
        {
            mTileMap.release();
        }
    }
    if (mPalette && stale(mPaletteUse.mFrame, mPaletteUse.mTime, frame, now))
    {
        LLImageGL::deleteTextures(1, &mPalette);
        mPalette = 0;
        mPaletteRows = 0;
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

    // The targets the passes that run read are made at the window's size; the others are given up once they are
    // stale.
    bool allocated = true;
    if (passes.mIds)
    {
        if (!mIdMap.isComplete() || mIdMap.getWidth() != (U32)width || mIdMap.getHeight() != (U32)height)
        {
            allocated = mIdMap.allocate(width, height, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE,
                                        LLRenderTarget::MIPS_NONE, LLPipeline::mainDepthFormat());
        }
        mIdUse = use;
    }
    if (passes.mEdges)
    {
        if (!mMarkedMap.isComplete() || !mTileMap.isComplete() || mMarkedMap.getWidth() != (U32)width ||
            mMarkedMap.getHeight() != (U32)height)
        {
            allocated = allocated && mMarkedMap.allocate(width, height, GL_RGBA8) &&
                        mTileMap.allocate(tileCount((U32)width), tileCount((U32)height), GL_RGBA16);
        }
        mEdgeUse = use;
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

    const U32 rows = paletteRows(count);
    mPaletteTexels.assign((size_t)PALETTE_WIDTH * rows * 4, 0);
    for (U32 i = 0; i < count; ++i)
    {
        const Entry& entry = mEntries[mOrder[i]];
        writePalette(mPaletteTexels, i + 1, entry.mColour, entry.mShowHidden);
    }

    LLGLSLShader* previous = LLGLSLShader::sCurBoundShaderPtr;
    gGL.flush();

    mScissorValid = false;
    mLastAvatar = nullptr;
    mLastMeshId = 0;
    mSkipLastSkin = false;

    if (passes.mIds)
    {
        drawIds(view, width, height, mvp);
    }

    // What the id pass drew says whether the outline and the glow have anything to touch, and the HUD's wireframe,
    // whose faces it drew too; the world's wireframe, drawn without it, draws whatever its faces put on screen.
    const bool edges = passes.mEdges && mScissorValid;
    const bool wireframe = passes.mWireframe && (!view.mHUD || mScissorValid);
    if (edges || wireframe)
    {
        uploadPalette(mPalette, mPaletteRows, mPaletteTexels, rows);

        // The wireframe first, the outline over it.
        if (wireframe)
        {
            drawWireframes(view, width, height);
        }

        if (edges)
        {
            // The scissor holds every texel drawn and the reach of its line or glow around them; the edge pass reads
            // no further, and what it reads past the jump and tile passes' work they clear to nothing, as it is.
            gSelectionJumpProgram.bind();
            drawJumps(gSelectionJumpProgram, mIdMap, mMarkedMap, mScissor, view, *gPipeline.mScreenTriangleVB);
            gSelectionTileProgram.bind();
            drawTiles(gSelectionTileProgram, mMarkedMap, mTileMap, mScissor, *gPipeline.mScreenTriangleVB);

            LLGLSColorMask mask(true, false);
            LLGLDepthTest depth(GL_FALSE);
            LLGLEnable blend(GL_BLEND);
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            LLGLSScissor scissor(vp_x + mScissor.mLeft, vp_y + mScissor.mBottom, mScissor.getWidth(),
                                 mScissor.getHeight());

            gSelectionOutlineProgram.bind();
            drawEdges(gSelectionOutlineProgram, mMarkedMap, mIdMap, mTileMap, mPalette, view, wants.mGlow,
                      *gPipeline.mScreenTriangleVB);
        }
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

void ALSelectionOutline::drawIds(const View& view, S32 width, S32 height, const LLMatrix4a& mvp)
{
    const U32 count = (U32)mEntries.size();
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
        // order, decides which surface each pixel keeps.
        for (bool rigged : { false, true })
        {
            if (rigged && view.mHUD)
            {
                break;
            }
            LLGLSLShader& program = rigged ? *gSelectionIdProgram.mRiggedVariant : gSelectionIdProgram;
            gSelectionIdProgram.bind(rigged);
            bindIdPass(program, view.mHUD ? nullptr : &gPipeline.mRT->deferredScreen, width, height);
            for (U32 i = 0; i < count; ++i)
            {
                const Entry& entry = mEntries[mOrder[i]];
                if (inIdPass(entry.mParts, entry.mPriority, view.mHUD))
                {
                    drawObject(program, entry, i + 1, rigged, view, mvp);
                }
            }
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

template <typename Begin>
bool ALSelectionOutline::drawFaces(LLGLSLShader& program, const Entry& entry, bool rigged, const View& view, FaceBox& box,
                                   Begin&& begin)
{
    LLViewerObject* object = entry.mObject;
    LLDrawable* drawable = object->mDrawable;
    if (!drawable || drawable->isDead() || !drawable->getVOVolume())
    {
        return false;
    }

    // The frame each face's vertex buffer is in, as LLVolumeGeometryManager::registerFace places it. Rigged faces
    // are skinned into agent space and take none.
    const LLMatrix4a* model = nullptr;
    bool agent_extents = false;
    if (!rigged)
    {
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
        else
        {
            return false;
        }
    }

    bool drawn = false;

    // The TE mask holds 32 faces, more than a volume has.
    const S32 faces = llmin(llmin((S32)object->getNumTEs(), drawable->getNumFaces()), 32);
    for (S32 te = 0; te < faces; ++te)
    {
        if (!(entry.mTEMask & (1u << te)))
        {
            continue;
        }

        LLFace* face = drawable->getFace(te);
        if (!face || face->isState(LLFace::RIGGED) != rigged)
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
            if (!face->mAvatar || !face->mSkinInfo ||
                !LLRenderPass::uploadMatrixPalette(face->mAvatar, face->mSkinInfo, mLastAvatar, mLastMeshId, mSkipLastSkin))
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

        if (!drawn)
        {
            gGL.loadMatrix(view.mModelview);
            if (model)
            {
                gGL.multMatrix(model->getF32ptr());
            }
            begin();
            drawn = true;
        }

        const FaceBinding binding = bindFace(program, face);
        drawFace(*buffer, face->getGeomIndex(), face->getGeomIndex() + face->getGeomCount() - 1, face->getIndicesCount(),
                 face->getIndicesStart(), binding.mDoubleSided);

        if (binding.mAnimated)
        {
            gGL.matrixMode(LLRender::MM_TEXTURE0);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
        }
    }
    return drawn;
}

void ALSelectionOutline::drawObject(LLGLSLShader& program, const Entry& entry, U32 id, bool rigged, const View& view,
                                    const LLMatrix4a& mvp)
{
    // An object drawn for the HUD wireframe's depth alone is not outlined: it hides what lies behind it and draws no
    // line of its own.
    const bool drawn = outlined(entry.mParts, entry.mPriority);
    FaceBox box;
    if (!drawFaces(program, entry, rigged, view, box,
                   [&] { setId(program, id, entry.mPriority, entry.mShowHidden, drawn); }))
    {
        return;
    }

    const S32 width = (S32)mIdMap.getWidth();
    const S32 height = (S32)mIdMap.getHeight();
    LLRect rect;
    if (!box.mBounded)
    {
        rect.set(0, height, width, 0);
    }
    else
    {
        const LLVector4a extents[2] = { box.mLo, box.mHi };
        const S32 reach = lineReach(entry.mPriority == PRIORITY_HOVER ? view.mGlowRadius : view.mWidth);
        if (!scissorRect(mvp, extents, width, height, reach, rect))
        {
            return;
        }
    }

    if (mScissorValid)
    {
        mScissor.unionWith(rect);
    }
    else
    {
        mScissor = rect;
        mScissorValid = true;
    }
}

void ALSelectionOutline::drawWireframes(const View& view, S32 width, S32 height)
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
            if (rigged && view.mHUD)
            {
                break;
            }
            LLGLSLShader& program = rigged ? *gSelectionWireframeProgram.mRiggedVariant : gSelectionWireframeProgram;
            gSelectionWireframeProgram.bind(rigged);
            bindWirePass(program, pass, view.mWireWidth, width, height, mPalette, &mIdMap);
            // Joint palettes are uploaded to the bound program, which this one is not yet.
            mLastAvatar = nullptr;
            mLastMeshId = 0;
            mSkipLastSkin = false;
            for (U32 i = 0; i < (U32)mEntries.size(); ++i)
            {
                // What glows, or is outlined alone, has no wireframe.
                const Entry& entry = mEntries[mOrder[i]];
                if (!inWirePass(entry.mParts, entry.mShowHidden, pass))
                {
                    continue;
                }
                FaceBox box;
                drawFaces(program, entry, rigged, view, box, [&] { setId(program, i + 1, entry.mPriority, entry.mShowHidden); });
            }
            program.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
            program.unbindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP);
            program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
        }
    }

    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.popMatrix();
    gGL.matrixMode(LLRender::MM_TEXTURE0);
    gGL.popMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.popMatrix();
}

ALSelectionOutline::FaceBinding ALSelectionOutline::bindFace(LLGLSLShader& program, LLFace* face)
{
    // Only the texture's alpha is read, never the face's colour: a prim made invisible by its transparency is
    // still outlined where its texture is there.
    const LLTextureEntry* te = face->getTextureEntry();
    LLViewerTexture* texture = face->getTexture();
    F32 cutoff = NO_ALPHA_TEST;
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
            cutoff = faceAlphaCutoff(texture->getID(), gltfAlphaCutoff(gltf->mAlphaMode, gltf->mAlphaCutoff));
        }
        gltf->mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR].getPacked(transform);
    }
    else if (te && texture)
    {
        // The diffuse map as the face names it, whatever stands in for it while it loads.
        const LLMaterial* material = te->getMaterialParams().get();
        cutoff = faceAlphaCutoff(te->getID(),
                                 legacyAlphaCutoff(material != nullptr, material ? material->getDiffuseAlphaMode() : 0,
                                                   material ? material->getAlphaMaskCutoff() : 0,
                                                   texture_has_alpha(texture)));
    }

    setAlphaTest(program, cutoff, transform);
    if (cutoff <= 0.f)
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
