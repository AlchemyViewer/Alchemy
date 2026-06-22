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
// The vertex shader uses the reserved attribute names (position, texcoord0,
// diffuse_color, glyph_loc) so the program binds through the standard
// LLVertexBuffer pipeline via mapAttributes(). The per-vertex normal is derived
// from gl_VertexID (the draw must start at vertex 0), and the em->screen
// Jacobian is a per-run uniform (constant for a single point size), so neither
// needs a vertex attribute.
//
// Lives in llrender rather than LLViewerShaderMgr because the source is
// assembled at runtime, not loaded from .glsl files on disk.
class LLFontGpuShader
{
public:
    // Standalone assembled GLSL (#version + HarfBuzz source + app main). Retained
    // for the headless shader smoke test (validates the hb-gpu GLSL compiles and
    // links); the production path injects the lib into the UI shader instead (see
    // fragmentLibSource). fragmentSource = draw renderer; colorFragmentSource =
    // paint renderer; vertexSource serves both.
    static std::string vertexSource();
    static std::string fragmentSource();
    static std::string colorFragmentSource();

    // Library-only source — the HarfBuzz functions for a stage, with NO `#version`
    // line and NO app main(). PRODUCTION path: fragmentLibSource is spliced into
    // the UI shader (uiF.glsl) so glyphs render under it as ordinary gGL geometry,
    // keyed per-vertex by glyph_loc — the host supplies main()/IO and calls
    // hb_gpu_draw (coverage) / hb_gpu_paint (color). vertexLibSource (hb_gpu_dilate)
    // is NOT injected anymore (the emit pre-dilates on the CPU); it is kept only
    // for the standalone vertexSource above.
    static std::string vertexLibSource();
    static std::string fragmentLibSource();

    // Assemble, compile, link, and map uniforms into `program`. Returns true on
    // success (program.isComplete()). Requires a current GL context and an
    // initialized LLShaderMgr (for the reserved uniform name table). On failure
    // the program is left unloaded and the GL compile/link log is LL_WARNS'd.
    // buildProgram      — monochrome draw program (outline coverage).
    // buildColorProgram — COLR color program (hb-gpu paint, premultiplied out).
    static bool buildProgram(LLGLSLShader& program);
    static bool buildColorProgram(LLGLSLShader& program);
};

#endif // LL_HAS_HB_GPU
#endif // LL_LLFONTGPUSHADER_H
