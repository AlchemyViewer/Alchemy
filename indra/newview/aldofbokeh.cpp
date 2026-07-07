/**
 * @file aldofbokeh.cpp
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

#include "linden_common.h"

#include "aldofbokeh.h"

#include "llmath.h"

namespace ALDoFBokeh
{
    Kernel bakeKernel(S32 blades, F32 roundness, F32 rotationDeg, F32 anamorphicRatio, F32 intensity)
    {
        // Clamp to documented ranges up front: a bad debug-setting value must
        // never reach the gather shader. Fewer than 3 blades has no polygon.
        const S32 clampedBlades = (blades < 3) ? 0 : llmin(blades, 12);
        roundness               = llclamp(roundness, 0.f, 1.f);
        anamorphicRatio         = llclamp(anamorphicRatio, 0.25f, 4.f);
        intensity               = llclamp(intensity, 0.f, 16.f);

        Kernel k;

        if (clampedBlades >= 3)
        {
            const F32 n = (F32)clampedBlades;
            k.seg     = 2.f * F_PI / n;
            k.halfSeg = F_PI / n;
            k.edge    = cosf(k.halfSeg); // inscribed radius of the N-gon
        }
        else
        {
            // Neutral constants the shader reads as "circular".
            k.seg     = 0.f;
            k.halfSeg = 0.f;
            k.edge    = 1.f;
        }

        k.roundness   = roundness;
        k.rotationRad = rotationDeg * DEG_TO_RAD;

        // Area-preserving anamorphic squeeze: stretch one axis and shrink the
        // other by the same factor so overall blur energy stays constant.
        const F32 s = sqrtf(anamorphicRatio);
        k.anisoX = 1.f / s;
        k.anisoY = s;

        // Highlight-weight scale for the gather. Independent of the offset reshape
        // (it also intensifies a plain round bokeh); 1.0 reproduces the classic
        // weighting exactly.
        k.intensity = intensity;

        // Only mark the kernel active when it actually reshapes the OFFSET, so the
        // default (no polygon, ratio 1.0) leaves the gather on its circular path.
        // Intensity is deliberately excluded -- it is applied regardless.
        k.active = (clampedBlades >= 3) || (anamorphicRatio != 1.f);

        return k;
    }
}
