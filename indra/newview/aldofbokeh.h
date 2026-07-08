/**
 * @file aldofbokeh.h
 * @brief Pure CPU-side baking of depth-of-field bokeh (aperture / anamorphic) kernel parameters.
 *
 * Copyright (c) 2026, Alchemy Viewer Project
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#ifndef AL_DOFBOKEH_H
#define AL_DOFBOKEH_H

#include "stdtypes.h"

// Depth-of-field bokeh shaping.
//
// The DoF gather (postDeferredF.glsl) places its blur taps on a circle. To give
// the bokeh real lens character we reshape WHERE each tap lands -- into an
// aperture-blade polygon and/or an anamorphic oval -- without changing HOW MANY
// taps are taken, so the O(CoC^2) gather cost is unaffected.
//
// The trig the shader needs is constant across the whole frame, so we bake it
// once here on the CPU (mirroring the chromatic-aberration path in
// LLPipeline::colorCorrect) and hand the shader a ready-to-use form.
namespace ALDoFBokeh
{
    // Shader-ready bokeh kernel. The geometry packs into two vec4 uniforms
    // consumed by postDeferredF.glsl, plus a scalar intensity uniform:
    //   bokeh_shape     = (seg, halfSeg, edge, roundness)
    //   bokeh_lens      = (rotationRad, anisoX, anisoY, active ? 1 : 0)
    //   bokeh_intensity = intensity
    struct Kernel
    {
        bool active;      // false => shader takes the plain circular path for the OFFSET
        F32  seg;         // 2*pi / blades   (0 when no polygon)
        F32  halfSeg;     // pi / blades     (0 when no polygon)
        F32  edge;        // cos(pi / blades) -- inscribed N-gon radius factor (1 when no polygon)
        F32  roundness;   // 0 = crisp polygon .. 1 = circle
        F32  rotationRad; // aperture rotation, radians
        F32  anisoX;      // anamorphic per-axis scale, area preserving: anisoX * anisoY == 1
        F32  anisoY;
        F32  intensity;   // highlight-weight EXPONENT: 1 = classic, higher = punchier / more
                          //   defined highlights, 0 = flat. Independent of `active` (it also
                          //   intensifies a round bokeh).
    };

    // Bake the shader kernel from raw setting values. Inputs are clamped to their
    // documented, sane ranges here so an out-of-range debug setting can never feed
    // garbage into the gather: blades below 3 disable the polygon (circular) and
    // blades above 12 are capped at 12; roundness clamps to [0, 1]; anamorphicRatio
    // clamps to [0.25, 4] (1 = off); intensity clamps to [0, 8] (1 = off; the shader
    // applies it as an exponent).
    Kernel bakeKernel(S32 blades, F32 roundness, F32 rotationDeg, F32 anamorphicRatio, F32 intensity);
}

#endif // AL_DOFBOKEH_H
