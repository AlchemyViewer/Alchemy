/**
 * @file altextureblit.cpp
 * @brief A copy from one texture into another through framebuffers it keeps, for textures no render target holds.
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

#include "linden_common.h"

#include "altextureblit.h"

#include "llgl.h"
#include "llrender.h"
#include "llrendertarget.h"

ALTextureBlit::~ALTextureBlit()
{
    release();
}

void ALTextureBlit::blit(U32 src_target, U32 src, U32 dst, S32 width, S32 height, bool flip_y)
{
    llassert(src_target == GL_TEXTURE_2D || src_target == GL_TEXTURE_RECTANGLE);
    gGL.flush();

    if (!mReadFBO)
    {
        glGenFramebuffers(1, &mReadFBO);
    }
    if (!mDrawFBO)
    {
        glGenFramebuffers(1, &mDrawFBO);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, mReadFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src_target, src, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mDrawFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);

    {
        // glBlitFramebuffer writes only where the scissor test passes.
        LLGLDisable scissor(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, width, height,
                          0, flip_y ? height : 0, width, flip_y ? 0 : height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    // Detached, so neither framebuffer keeps a texture its owner may delete or hand to another context.
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src_target, 0, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, LLRenderTarget::sCurFBO);
}

void ALTextureBlit::release()
{
    // The names die with the context; deleting them after it would fault in the driver.
    if (gGLManager.mInited)
    {
        if (mReadFBO)
        {
            glDeleteFramebuffers(1, &mReadFBO);
        }
        if (mDrawFBO)
        {
            glDeleteFramebuffers(1, &mDrawFBO);
        }
    }
    mReadFBO = 0;
    mDrawFBO = 0;
}
