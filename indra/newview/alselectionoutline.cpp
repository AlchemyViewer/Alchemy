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
#include "llglstates.h"
#include "llselectmgr.h"
#include "llviewercamera.h"
#include "llviewerobject.h"
#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llvovolume.h"
#include "pipeline.h"

// llviewerdisplay.cpp: the matrices the HUD's attachments are drawn with.
bool get_hud_matrices(LLMatrix4a& proj, LLMatrix4a& model);

ALSelectionOutline& ALSelectionOutline::instance()
{
    static ALSelectionOutline* outline = new ALSelectionOutline();
    return *outline;
}

ALSelectionOutline::View ALSelectionOutline::worldView()
{
    const LLViewerCamera* camera = LLViewerCamera::getInstance();
    View view;
    view.mModelview = LLViewerCamera::getCurrent().getModelview();
    view.mProjection = gGL.getProjectionMatrix();
    view.mRadius = radiusPixels(LLSelectMgr::sHighlightThickness, (F32)gGLViewport[3], camera->getView(), camera->getDefaultFOV());
    view.mHUD = false;
    return view;
}

bool ALSelectionOutline::hudView(View& view)
{
    if (!get_hud_matrices(view.mProjection, view.mModelview))
    {
        return false;
    }
    view.mRadius = hudRadiusPixels(LLSelectMgr::sHighlightThickness, (F32)gGLViewport[3]);
    view.mHUD = true;
    return true;
}

void ALSelectionOutline::add(LLViewerObject* object, U32 te_mask, const LLColor4& colour, bool show_hidden)
{
    if (!object)
    {
        return;
    }

    const auto found = mEntryIndex.find(object);
    if (found != mEntryIndex.end())
    {
        mEntries[found->second] = { object, te_mask, colour, show_hidden };
        return;
    }

    if (mEntries.size() >= MAX_IDS)
    {
        return;
    }
    mEntryIndex.emplace(object, (U32)mEntries.size());
    mEntries.push_back({ object, te_mask, colour, show_hidden });
}

void ALSelectionOutline::clearEntries()
{
    mEntries.clear();
    mEntryIndex.clear();
}

void ALSelectionOutline::release()
{
    // Called every frame nothing is outlined, so it touches nothing that is not there.
    if (mIdMap.isComplete())
    {
        mIdMap.release();
    }
    if (mPalette)
    {
        LLImageGL::deleteTextures(1, &mPalette);
        mPalette = 0;
    }
    mPaletteRows = 0;
}

void ALSelectionOutline::render(const View& view)
{
    if (mEntries.empty())
    {
        release();
        return;
    }

    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    // The window's viewport: the final blit filled it from the scene, the HUD is drawn over it, and the outlines
    // are laid over both.
    const S32 vp_x = gGLViewport[0];
    const S32 vp_y = gGLViewport[1];
    const S32 width = gGLViewport[2];
    const S32 height = gGLViewport[3];
    if (width <= 0 || height <= 0 || !gSelectionIdProgram.isComplete() || !gSelectionIdProgram.mRiggedVariant ||
        !gSelectionOutlineProgram.isComplete() || !gPipeline.mRT || !gPipeline.mScreenTriangleVB)
    {
        clearEntries();
        return;
    }

    if (!mIdMap.isComplete() || mIdMap.getWidth() != (U32)width || mIdMap.getHeight() != (U32)height)
    {
        if (!mIdMap.allocate(width, height, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE,
                             LLPipeline::mainDepthFormat()))
        {
            LL_WARNS_ONCE("Pipeline") << "Could not allocate the selection outline's id target" << LL_ENDL;
            clearEntries();
            return;
        }
    }

    LL_PROFILE_GPU_ZONE("selection outline");

    LLMatrix4a mvp;
    mvp.setMul(view.mModelview, view.mProjection);

    const U32 count = (U32)mEntries.size();
    const U32 rows = paletteRows(count);
    mPaletteTexels.assign((size_t)PALETTE_WIDTH * rows * 4, 0);
    for (U32 i = 0; i < count; ++i)
    {
        writePalette(mPaletteTexels, i + 1, mEntries[i].mColour, mEntries[i].mShowHidden);
    }

    LLGLSLShader* previous = LLGLSLShader::sCurBoundShaderPtr;
    gGL.flush();

    mScissorValid = false;
    mLastAvatar = nullptr;
    mLastMeshId = 0;
    mSkipLastSkin = false;

    mIdMap.bindTarget();
    {
        LLGLSColorMask mask(true, true);
        // Depth writes on before the clear, which they mask.
        LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_LEQUAL);
        // Faces seen from behind are outlined too.
        LLGLDisable cull(GL_CULL_FACE);
        LLGLDisable blend(GL_BLEND);
        LLGLDisable scissor(GL_SCISSOR_TEST);

        gGL.setClearColor(LLColor4(0.f, 0.f, 0.f, 0.f));
        mIdMap.clear();

        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.pushMatrix();
        gGL.loadMatrix(view.mProjection);
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
                drawObject(program, mEntries[i], i + 1, rigged, view, mvp);
            }
            program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
        }

        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.popMatrix();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.popMatrix();
    }
    mIdMap.flush();

    if (mScissorValid)
    {
        uploadPalette(mPalette, mPaletteRows, mPaletteTexels, rows);

        LLGLSColorMask mask(true, false);
        LLGLDepthTest depth(GL_FALSE);
        LLGLEnable blend(GL_BLEND);
        gGL.setSceneBlendType(LLRender::BT_ALPHA);
        LLGLSScissor scissor(vp_x + mScissor.mLeft, vp_y + mScissor.mBottom, mScissor.getWidth(), mScissor.getHeight());

        gSelectionOutlineProgram.bind();
        drawEdges(gSelectionOutlineProgram, mIdMap, mPalette, view.mRadius, *gPipeline.mScreenTriangleVB);
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

void ALSelectionOutline::drawObject(LLGLSLShader& program, const Entry& entry, U32 id, bool rigged, const View& view,
                                    const LLMatrix4a& mvp)
{
    LLViewerObject* object = entry.mObject;
    LLDrawable* drawable = object->mDrawable;
    if (!drawable || drawable->isDead() || !drawable->getVOVolume())
    {
        return;
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
            return;
        }
    }

    LLVector4a lo(FLT_MAX, FLT_MAX, FLT_MAX);
    LLVector4a hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    bool bounded = true;
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

        LLVector4a box[2];
        if (rigged)
        {
            if (!face->mAvatar || !face->mSkinInfo ||
                !LLRenderPass::uploadMatrixPalette(face->mAvatar, face->mSkinInfo, mLastAvatar, mLastMeshId, mSkipLastSkin))
            {
                continue;
            }
            // Joint boxes, refreshed while the face is in view (LLFace::calcPixelArea); empty until they are.
            box[0] = face->mRiggedExtents[0];
            box[1] = face->mRiggedExtents[1];
            LLVector4a size;
            size.setSub(box[1], box[0]);
            if (!(size[0] > 0.f && size[1] > 0.f && size[2] > 0.f))
            {
                bounded = false;
            }
        }
        else if (agent_extents)
        {
            box[0] = face->mExtents[0];
            box[1] = face->mExtents[1];
        }
        else
        {
            matMulBoundBox(*model, face->mExtents, box);
        }
        lo.setMin(lo, box[0]);
        hi.setMax(hi, box[1]);

        if (!drawn)
        {
            gGL.loadMatrix(view.mModelview);
            if (model)
            {
                gGL.multMatrix(model->getF32ptr());
            }
            setId(program, id);
            drawn = true;
        }

        buffer->setBuffer();
        buffer->drawRange(LLRender::TRIANGLES, face->getGeomIndex(), face->getGeomIndex() + face->getGeomCount() - 1,
                          face->getIndicesCount(), face->getIndicesStart());
    }

    if (!drawn)
    {
        return;
    }

    const S32 width = (S32)mIdMap.getWidth();
    const S32 height = (S32)mIdMap.getHeight();
    LLRect rect;
    if (!bounded)
    {
        rect.set(0, height, width, 0);
    }
    else
    {
        const LLVector4a extents[2] = { lo, hi };
        if (!scissorRect(mvp, extents, width, height, view.mRadius, rect))
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
