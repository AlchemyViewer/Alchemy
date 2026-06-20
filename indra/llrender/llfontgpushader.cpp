/**
 * @file llfontgpushader.cpp
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

#include "linden_common.h"

#include "llfontgpushader.h"

#if LL_HAS_HB_GPU

#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"

namespace
{
    // App vertex main. Reserved attributes (position, texcoord0, diffuse_color,
    // glyph_loc) bind through LLVertexBuffer. The corner normal is derived from
    // gl_VertexID (6 verts/glyph in TL,BL,BR,TL,BR,TR order; the draw starts at
    // vertex 0). jac is a per-run uniform (one point size => one scale).
    // hb_gpu_dilate (shared vertex source) expands the quad by ~half a pixel on
    // screen for analytic-edge coverage, adjusting the em-space sample coord to
    // match. modelview_projection_matrix is fed by LLRender::syncMatrices.
    const char* APP_VERTEX_MAIN = R"GLSL(
uniform mat4 modelview_projection_matrix;
uniform vec2 viewport;
uniform vec4 jac;

in vec3 position;
in vec2 texcoord0;
in vec4 diffuse_color;
in uint glyph_loc;

out vec2 vary_renderCoord;
flat out uint vary_glyphLoc;
out vec4 vary_color;

const vec2 HB_CORNER_NORMAL[6] = vec2[6](
    vec2(-1.0,  1.0),   // TL
    vec2(-1.0, -1.0),   // BL
    vec2( 1.0, -1.0),   // BR
    vec2(-1.0,  1.0),   // TL
    vec2( 1.0, -1.0),   // BR
    vec2( 1.0,  1.0)    // TR
);

void main()
{
    vec2 pos = position.xy;
    vec2 tc  = texcoord0;
    vec2 normal = HB_CORNER_NORMAL[gl_VertexID % 6];
    hb_gpu_dilate(pos, tc, normal, jac, modelview_projection_matrix, viewport);
    gl_Position      = modelview_projection_matrix * vec4(pos, 0.0, 1.0);
    vary_renderCoord = tc;
    vary_glyphLoc    = glyph_loc;
    vary_color       = diffuse_color;
}
)GLSL";

    // App fragment main. hb_gpu_draw (from the draw-renderer fragment source)
    // returns analytic coverage in [0,1]; composite it into the text color's
    // alpha, matching the existing atlas path's color * coverage semantics.
    const char* APP_FRAGMENT_MAIN = R"GLSL(
in vec2 vary_renderCoord;
flat in uint vary_glyphLoc;
in vec4 vary_color;

out vec4 frag_color;

void main()
{
    float coverage = hb_gpu_draw(vary_renderCoord, vary_glyphLoc);
    // Stem darkening: adapt thin-stroke coverage at small ppem so text stays
    // legible (a no-op at large sizes via the smoothstep inside the helper).
    // NOTE: hb_gpu_stem_darken is a coverage gamma keyed on foreground
    // brightness; the UI composites in sRGB (not linear-light), so this is an
    // approximation -- revisit the brightness/curve if it over- or
    // under-weights at small sizes.
    float ppem = hb_gpu_ppem(vary_renderCoord, vary_glyphLoc);
    float brightness = dot(vary_color.rgb, vec3(0.299, 0.587, 0.114));
    coverage = hb_gpu_stem_darken(coverage, brightness, ppem);
    frag_color = vec4(vary_color.rgb, vary_color.a * coverage);
}
)GLSL";

    GLuint compileStage(GLenum type, const std::string& src)
    {
        GLuint sh = glCreateShader(type);
        const char* s = src.c_str();
        glShaderSource(sh, 1, &s, nullptr);
        glCompileShader(sh);

        GLint ok = GL_FALSE;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLchar log[4096] = { 0 };
            glGetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
            LL_WARNS("FontGpu") << "hb-gpu "
                << (type == GL_VERTEX_SHADER ? "vertex" : "fragment")
                << " shader compile failed:\n" << log << LL_ENDL;
            glDeleteShader(sh);
            return 0;
        }
        return sh;
    }
}

std::string LLFontGpuShader::vertexSource()
{
    std::string src = "#version 330\n";
    if (const char* shared = hb_gpu_shader_source(HB_GPU_SHADER_STAGE_VERTEX, HB_GPU_SHADER_LANG_GLSL))
    {
        src += shared;
    }
    src += '\n';
    src += APP_VERTEX_MAIN;
    return src;
}

std::string LLFontGpuShader::fragmentSource()
{
    std::string src = "#version 330\n";
    // Shared fragment source FIRST (provides _hb_gpu_slug + the hb_gpu_atlas
    // sampler), then the draw-renderer wrapper that calls it — this order is
    // required (see hb-gpu-draw-fragment.glsl).
    if (const char* shared = hb_gpu_shader_source(HB_GPU_SHADER_STAGE_FRAGMENT, HB_GPU_SHADER_LANG_GLSL))
    {
        src += shared;
    }
    src += '\n';
    if (const char* draw = hb_gpu_draw_shader_source(HB_GPU_SHADER_STAGE_FRAGMENT, HB_GPU_SHADER_LANG_GLSL))
    {
        src += draw;
    }
    src += '\n';
    src += APP_FRAGMENT_MAIN;
    return src;
}

bool LLFontGpuShader::buildProgram(LLGLSLShader& program)
{
    program.unload();

    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSource());
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSource());
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    program.mProgramObject = glCreateProgram();
    program.mName = "font gpu (hb-gpu)";
    program.attachObject(vs);
    program.attachObject(fs);

    // mapAttributes() binds the reserved attribute names (position, texcoord0,
    // diffuse_color, glyph_loc) to their LLVertexBuffer slots and links, so the
    // standard setBuffer() path feeds this program.
    if (!program.mapAttributes())
    {
        glDeleteShader(vs);
        glDeleteShader(fs);
        program.unload();
        program.mProgramObject = 0;
        return false;
    }

    // The linked program retains the stage objects; the standalone handles can go.
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!program.mapUniforms())
    {
        program.unload();
        program.mProgramObject = 0;
        return false;
    }

    return program.isComplete();
}

#endif // LL_HAS_HB_GPU
