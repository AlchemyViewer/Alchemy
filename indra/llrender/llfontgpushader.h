/**
 * @file llfontgpushader.h
 * @brief Builds the analytic (hb-gpu) UI text shader program.
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

#ifndef LL_LLFONTGPUSHADER_H
#define LL_LLFONTGPUSHADER_H

#include "stdtypes.h"
#include "llhbgpu.h"   // LL_HAS_HB_GPU + hb-gpu shader-source API

#if LL_HAS_HB_GPU

#include <string>

class LLGLSLShader;

// Assembles and builds the analytic (hb-gpu) UI text program. HarfBuzz emits
// the heavy GLSL at runtime — the shared helpers (hb_gpu_dilate in the vertex
// stage; the _hb_gpu_slug rasterizer + isamplerBuffer atlas in the fragment
// stage) plus the draw-renderer wrapper (hb_gpu_draw, fragment-only). This
// class splices those with a thin app main()/IO layer into a linkable program.
//
// Lives in llrender rather than LLViewerShaderMgr because the source is
// assembled at runtime, not loaded from .glsl files on disk.
class LLFontGpuShader
{
public:
    // Vertex attribute locations, pinned via explicit layout(location=) in the
    // vertex shader so the Phase-4 vertex buffer binds to the same slots
    // without relying on the reserved-attribute table.
    enum EAttrib : U32
    {
        ATTRIB_POSITION = 0,  // vec2  object-space quad corner (font units * scale)
        ATTRIB_TEXCOORD = 1,  // vec2  em-space sample coordinate (renderCoord)
        ATTRIB_NORMAL   = 2,  // vec2  outward normal, for half-pixel dilation
        ATTRIB_JAC      = 3,  // vec4  inverse em->object Jacobian (row-major)
        ATTRIB_GLYPHLOC = 4,  // uint  texel offset into the glyph buffer (glyphLoc)
        ATTRIB_COLOR    = 5,  // vec4  text color
    };

    // The assembled GLSL for each stage (#version + HarfBuzz source + app main).
    static std::string vertexSource();
    static std::string fragmentSource();

    // Assemble, compile, link, and map uniforms into `program`. Returns true on
    // success (program.isComplete()). Requires a current GL context and an
    // initialized LLShaderMgr (for the reserved uniform name table). On failure
    // the program is left unloaded and the GL compile/link log is LL_WARNS'd.
    static bool buildProgram(LLGLSLShader& program);
};

#endif // LL_HAS_HB_GPU
#endif // LL_LLFONTGPUSHADER_H
