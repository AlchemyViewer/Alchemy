/**
 * @file alprojection.cpp
 * @brief Points through a projection and back.
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

#include "llmath.h"
#include "alprojection.h"

namespace
{
    // obj as a point through both matrices, divided by w: normalized
    // device coordinates, w = 1.
    inline LLVector4a to_ndc(const LLVector4a& obj, const LLMatrix4a& modelview, const LLMatrix4a& projection)
    {
        const LLVector4a point(alsimd::select(alsimd::mask_xyz(), obj, alsimd::set(0.f, 0.f, 0.f, 1.f)));
        LLVector4a eye, clip;
        modelview.transform4(point, eye);
        projection.transform4(eye, clip);
        clip.div(LLVector4a(alsimd::splat<3>(clip)));
        return clip;
    }

    // ndc x and y in [-1, 1] to the viewport's pixels; z and w as given.
    inline LLVector4a to_window(const LLVector4a& ndc, const S32 viewport[4])
    {
        const LLQuad unit = alsimd::fmadd(ndc, alsimd::set(0.5f, 0.5f, 1.f, 1.f), alsimd::set(0.5f, 0.5f, 0.f, 0.f));
        const LLQuad scale = alsimd::set(F32(viewport[2]), F32(viewport[3]), 1.f, 1.f);
        const LLQuad offset = alsimd::set(F32(viewport[0]), F32(viewport[1]), 0.f, 0.f);
        return alsimd::fmadd(unit, scale, offset);
    }

    // The window point's x and y to [-1, 1]; z as given, w = 1.
    inline LLVector4a from_window(const LLVector4a& win, const S32 viewport[4])
    {
        const LLQuad point = alsimd::select(alsimd::mask_xyz(), win, alsimd::set(0.f, 0.f, 0.f, 1.f));
        const LLQuad offset = alsimd::set(F32(viewport[0]), F32(viewport[1]), 0.f, 0.f);
        const LLQuad extent = alsimd::set(F32(viewport[2]), F32(viewport[3]), 1.f, 1.f);
        const LLQuad two = alsimd::set(2.f, 2.f, 1.f, 1.f);
        const LLQuad one = alsimd::set(1.f, 1.f, 0.f, 0.f);
        return alsimd::fmsub(alsimd::div(alsimd::sub(point, offset), extent), two, one);
    }

    // The normalized device point back through the inverted matrices,
    // divided by w.
    inline LLVector4a from_ndc(const LLVector4a& ndc, const LLMatrix4a& inverse)
    {
        LLVector4a obj;
        inverse.transform4(ndc, obj);
        obj.div(LLVector4a(alsimd::splat<3>(obj)));
        return obj;
    }
}

namespace alprojection
{
    LLVector4a project(const LLVector4a& obj, const LLMatrix4a& modelview, const LLMatrix4a& projection, const S32 viewport[4])
    {
        LLVector4a ndc = to_ndc(obj, modelview, projection);
        // depth from clip z: (z + 1) / 2
        ndc = alsimd::fmadd(ndc, alsimd::set(1.f, 1.f, 0.5f, 1.f), alsimd::set(0.f, 0.f, 0.5f, 0.f));
        return to_window(ndc, viewport);
    }

    LLVector4a project_zo(const LLVector4a& obj, const LLMatrix4a& modelview, const LLMatrix4a& projection, const S32 viewport[4])
    {
        return to_window(to_ndc(obj, modelview, projection), viewport);
    }

    LLVector4a unproject(const LLVector4a& win, const LLMatrix4a& inverse, const S32 viewport[4])
    {
        LLVector4a ndc = from_window(win, viewport);
        // clip z from depth: 2 * depth - 1
        ndc = alsimd::fmsub(ndc, alsimd::set(1.f, 1.f, 2.f, 1.f), alsimd::set(0.f, 0.f, 1.f, 0.f));
        return from_ndc(ndc, inverse);
    }

    LLVector4a unproject_zo(const LLVector4a& win, const LLMatrix4a& inverse, const S32 viewport[4])
    {
        return from_ndc(from_window(win, viewport), inverse);
    }

    LLVector4a unproject(const LLVector4a& win, const LLMatrix4a& modelview, const LLMatrix4a& projection, const S32 viewport[4])
    {
        LLMatrix4a inverse;
        inverse.setMul(modelview, projection);
        inverse.invert();
        return unproject(win, inverse, viewport);
    }

    LLVector4a unproject_zo(const LLVector4a& win, const LLMatrix4a& modelview, const LLMatrix4a& projection, const S32 viewport[4])
    {
        LLMatrix4a inverse;
        inverse.setMul(modelview, projection);
        inverse.invert();
        return unproject_zo(win, inverse, viewport);
    }
}
