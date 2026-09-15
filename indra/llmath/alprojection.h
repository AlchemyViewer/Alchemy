/**
 * @file alprojection.h
 * @brief Points through a projection and back: object space to the window
 *        and the window to object space, in either depth convention.
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

#ifndef AL_PROJECTION_H
#define AL_PROJECTION_H

#include "llmatrix4a.h"

// A window point is {x, y, depth}: x and y in the pixels of a viewport
// {x, y, width, height}, depth in [0, 1]. The plain forms take clip z in
// [-1, 1] and map it to that depth; the _zo forms take clip z already in
// [0, 1] and leave it. Every result carries w = 1.
namespace alprojection
{
    // The object point obj through modelview then projection to the window.
    LLVector4a project(const LLVector4a& obj, const LLMatrix4a& modelview, const LLMatrix4a& projection, const S32 viewport[4]);
    LLVector4a project_zo(const LLVector4a& obj, const LLMatrix4a& modelview, const LLMatrix4a& projection, const S32 viewport[4]);

    // The window point win back to object space through inverse, which is
    // the inverse of modelview then projection: what setInverse gives for
    // setMul(modelview, projection).
    LLVector4a unproject(const LLVector4a& win, const LLMatrix4a& inverse, const S32 viewport[4]);
    LLVector4a unproject_zo(const LLVector4a& win, const LLMatrix4a& inverse, const S32 viewport[4]);

    // The same, inverting the two matrices here. A caller with several
    // points against one camera inverts once and calls the form above.
    LLVector4a unproject(const LLVector4a& win, const LLMatrix4a& modelview, const LLMatrix4a& projection, const S32 viewport[4]);
    LLVector4a unproject_zo(const LLVector4a& win, const LLMatrix4a& modelview, const LLMatrix4a& projection, const S32 viewport[4]);
}

#endif
