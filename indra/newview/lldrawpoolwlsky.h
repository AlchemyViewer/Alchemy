/**
 * @file lldrawpoolwlsky.h
 * @brief LLDrawPoolWLSky class definition
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

#ifndef LL_DRAWPOOLWLSKY_H
#define LL_DRAWPOOLWLSKY_H

#include "lldrawpool.h"

class LLGLSLShader;

class LLDrawPoolWLSky : public LLDrawPool {
public:

    // A star's centre, and its colour in the position's fourth word, where the texture index goes (ALStarField).
    static const U32 STAR_VERTEX_DATA_MASK =    LLVertexBuffer::MAP_VERTEX |
        LLVertexBuffer::MAP_TEXTURE_INDEX;
    LLDrawPoolWLSky(void);
    /*virtual*/ ~LLDrawPoolWLSky();

    /*virtual*/ bool isDead() { return false; }

    /*virtual*/ S32 getNumDeferredPasses() { return 1; }
    /*virtual*/ void renderDeferred(S32 pass);

    /*virtual*/ LLViewerTexture *getDebugTexture();
    /*virtual*/ bool verify() const { return true; }        // Verify that all data in the draw pool is correct!
    /*virtual*/ S32 getShaderLevel() const { return mShaderLevel; }

    //static LLDrawPool* createPool(const U32 type, LLViewerTexture *tex0 = NULL);

    /*virtual*/ LLViewerTexture* getTexture();
    /*virtual*/ bool isFacePool() { return false; }
    /*virtual*/ void resetDrawOrders();

    static void cleanupGL();
    static void restoreGL();

    /// What windlight/windlightDomeF.glsl reads besides the Environment block, on the bound @a shader: the rainbow
    /// and halo maps and their levels, the glow, density and light, and the dome's geometry.
    static void bindDome(LLGLSLShader& shader);

    /// Whether the sky this view draws is the HDRI rather than WindLight's: always in a capture that has one (unless
    /// only the irradiance takes it), and in the main view while its split screen shows any of it.
    static bool useHDRISky();

    /// The sky's composite's inputs on the bound @a shader, the soften pass's sky variant (deferred/skyCompositeF.glsl):
    /// the dome's (bindDome), the layers this view draws (ALSkyLayers::layersFor) and what each takes, about the view's
    /// camera. Ticks the meteors and the aurora's clock in a view that draws them, as their draws did.
    static void bindComposite(LLGLSLShader& shader);

    /// The textures bindComposite bound on @a shader, let go, and those alone: a unit whose map this view's layers left
    /// unbound holds what an earlier pass bound there.
    static void unbindComposite(LLGLSLShader& shader);
private:
    // The stars into their attachment of the bound G-buffer, the one layer of the sky drawn into it.
    void renderStarsDeferred(const LLVector3& camPosLocal) const;
};

#endif // LL_DRAWPOOLWLSKY_H
