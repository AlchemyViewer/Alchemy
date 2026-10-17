/**
 * @file alwaterstencil.h
 * @brief The water's bits in the screen's stencil, and the stencil states that write and test them.
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

#pragma once

#include "llgl.h"
#include "llglstates.h"

/// The screen's depth carries stencil in every render-target pack (LLPipeline::allocateScreenBufferInternal), cleared
/// with the G-buffer each frame. The water keeps its per-pixel facts there, each a bit, each a scoped state below.
namespace ALWaterStencil
{

/// Where a PASS_INVISIBLE prim removes the water: set by the exclusion pass, just before the water.
constexpr GLuint EXCLUDE_BIT = 1u << 0;

/// The water's own pixels: set by every water draw where it shades, read by the haze pass to converge the water's
/// in-scatter on the sky behind it.
constexpr GLuint SURFACE_BIT = 1u << 1;

/// The exclusion pass: the exclude bit set wherever a fragment passes the scene's depth test.
class Exclude : public ALGLStencil
{
public:
    Exclude() : ALGLStencil(GL_ALWAYS, EXCLUDE_BIT, 0u, GL_KEEP, GL_KEEP, GL_REPLACE, EXCLUDE_BIT) {}
};

/// The water's draws: where the exclude bit is clear, setting the surface bit where they shade. One reference serves
/// both: compared under the read mask it is the exclude bit's 0, and written under the write mask the surface bit.
class Surface : public ALGLStencil
{
public:
    Surface() : ALGLStencil(GL_EQUAL, SURFACE_BIT, EXCLUDE_BIT, GL_KEEP, GL_KEEP, GL_REPLACE, SURFACE_BIT) {}
};

/// The water's haze: where the exclude bit is clear, writing nothing.
class NotExcluded : public ALGLStencil
{
public:
    NotExcluded() : ALGLStencil(GL_EQUAL, 0, EXCLUDE_BIT, GL_KEEP, GL_KEEP, GL_KEEP, 0u) {}
};

/// The surface seen from below where an exclusion prim covers it, drawn undistorted: where the exclude bit is set.
class Excluded : public ALGLStencil
{
public:
    Excluded() : ALGLStencil(GL_EQUAL, EXCLUDE_BIT, EXCLUDE_BIT, GL_KEEP, GL_KEEP, GL_KEEP, 0u) {}
};

/// The haze pass's draw for the water's pixels: where the surface bit is set.
class OnWater : public ALGLStencil
{
public:
    OnWater() : ALGLStencil(GL_EQUAL, SURFACE_BIT, SURFACE_BIT, GL_KEEP, GL_KEEP, GL_KEEP, 0u) {}
};

/// The haze pass's draw for everything else: where the surface bit is clear.
class OffWater : public ALGLStencil
{
public:
    OffWater() : ALGLStencil(GL_EQUAL, 0, SURFACE_BIT, GL_KEEP, GL_KEEP, GL_KEEP, 0u) {}
};

} // namespace ALWaterStencil
