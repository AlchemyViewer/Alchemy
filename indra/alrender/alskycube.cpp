/**
 * @file alskycube.cpp
 * @brief WindLight's dome baked into a small cube, the environment map probe-less reflections read.
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

#include "alskycube.h"

#include "alfarplane.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llimagegl.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llvertexbuffer.h"

#include <array>
#include <vector>

namespace ALSkyCube
{

namespace
{
    // GL's cube lookup (the GL specification's table of cube map face selection): for the major axis' face, s runs
    // along sc and t along tc. Each face's right and up are those axes in the agent's frame.
    const std::array<Face, 6> FACES = { {
        { LLVector3(1.f, 0.f, 0.f), LLVector3(0.f, 0.f, -1.f), LLVector3(0.f, -1.f, 0.f) },  // +x: sc = -z, tc = -y
        { LLVector3(-1.f, 0.f, 0.f), LLVector3(0.f, 0.f, 1.f), LLVector3(0.f, -1.f, 0.f) },  // -x: sc = +z, tc = -y
        { LLVector3(0.f, 1.f, 0.f), LLVector3(1.f, 0.f, 0.f), LLVector3(0.f, 0.f, 1.f) },    // +y: sc = +x, tc = +z
        { LLVector3(0.f, -1.f, 0.f), LLVector3(1.f, 0.f, 0.f), LLVector3(0.f, 0.f, -1.f) },  // -y: sc = +x, tc = -z
        { LLVector3(0.f, 0.f, 1.f), LLVector3(1.f, 0.f, 0.f), LLVector3(0.f, -1.f, 0.f) },   // +z: sc = +x, tc = -y
        { LLVector3(0.f, 0.f, -1.f), LLVector3(-1.f, 0.f, 0.f), LLVector3(0.f, -1.f, 0.f) }, // -z: sc = -x, tc = -y
    } };
}

Face face(U32 face)
{
    return FACES[face];
}

LLVector3 texelDirection(U32 face, U32 x, U32 y, U32 size)
{
    const Face& basis = FACES[face];
    const F32   s     = (F32)(2 * x + 1) / (F32)size - 1.f;
    const F32   t     = (F32)(2 * y + 1) / (F32)size - 1.f;
    LLVector3   dir   = basis.mForward + basis.mRight * s + basis.mUp * t;
    dir.normalize();
    return dir;
}

LLVector3 toDome(const LLVector3& agent)
{
    return ALFarPlane::skyFrame(agent);
}

bool termsAlike(std::span<const F32> a, std::span<const F32> b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        const F32 apart = fabsf(a[i] - b[i]);
        if (!(apart < F_APPROXIMATELY_ZERO || apart < llmax(fabsf(a[i]), fabsf(b[i])) * TERM_TOLERANCE))
        {
            return false;
        }
    }
    return true;
}

U32 allocate()
{
    U32 name = 0;
    LLImageGL::generateTextures(1, &name);
    if (!name)
    {
        return 0;
    }

    // Allocated whole, every face and level at once: glTexStorage2D takes the cube's own target.
    gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_CUBE_MAP, name);
    glTexStorage2D(GL_TEXTURE_CUBE_MAP, LEVELS, GL_RGBA8, SIZE, SIZE);
    // Recorded once for the whole cube: the count of 6 covers its faces, each with every level.
    LLImageGLMemory::alloc_tex_image(SIZE, SIZE, GL_RGBA8, 6, LEVELS, LLImageGLMemory::OTHER_TEXTURE);

    // Every face and level neutral, each written whole, so the cube holds nothing undefined before its first bake.
    std::vector<U8> texels(size_t(SIZE) * SIZE * 4);
    for (size_t i = 0; i < texels.size(); i += 4)
    {
        texels[i + 0] = NEUTRAL;
        texels[i + 1] = NEUTRAL;
        texels[i + 2] = NEUTRAL;
        texels[i + 3] = 255;
    }
    for (U32 level = 0; level < LEVELS; ++level)
    {
        const S32 size = (S32)(SIZE >> level);
        for (U32 f = 0; f < 6; ++f)
        {
            LLImageGL::setManualSubImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, (S32)level, size, size, GL_RGBA,
                                         GL_UNSIGNED_BYTE, texels.data());
        }
    }
    gGL.getTextureSlot(0)->unbind();
    stop_glerror();
    return name;
}

void bake(LLGLSLShader& program, U32 cube, LLRenderTarget& scratch, LLVertexBuffer& triangle)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    LL_PROFILE_GPU_ZONE("sky cube");

    // Every face's directions, in the dome's frame the program works in, three to a face.
    std::array<LLVector3, 18> directions;
    for (U32 f = 0; f < 6; ++f)
    {
        directions[f * 3 + 0] = toDome(FACES[f].mForward);
        directions[f * 3 + 1] = toDome(FACES[f].mRight);
        directions[f * 3 + 2] = toDome(FACES[f].mUp);
    }

    LLGLDisable   blend(GL_BLEND);
    LLGLDisable   cull(GL_CULL_FACE);
    LLGLDisable   scissor(GL_SCISSOR_TEST);
    LLGLDisable   srgb(GL_FRAMEBUFFER_SRGB);
    LLGLDepthTest depth(GL_FALSE, GL_FALSE);

    // The six faces side by side in one draw (the program picks the face from where the pixel lies across the strip),
    // so nothing is bound between them over the units the program samples through.
    scratch.bindTarget();
    program.uniform3fv(LLShaderMgr::SKY_CUBE_FACE, (U32)directions.size(), directions[0].mV);
    triangle.setBuffer();
    triangle.drawArrays(LLRender::TRIANGLES, 0, 3);

    // Each face's square of the strip into the face, read from the target still bound.
    gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_CUBE_MAP, cube);
    for (U32 f = 0; f < 6; ++f)
    {
        glCopyTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, 0, 0, f * SIZE, 0, SIZE, SIZE);
    }
    LLImageGL::generateMipmaps(GL_TEXTURE_CUBE_MAP);
    gGL.getTextureSlot(0)->unbind();
    scratch.flush();
    stop_glerror();
}

} // namespace ALSkyCube
