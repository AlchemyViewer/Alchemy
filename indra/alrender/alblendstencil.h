/**
 * @file alblendstencil.h
 * @brief The blended pass's bit in the screen's stencil, and the stencil states that write and test it.
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

/// The screen's depth carries stencil in every render-target pack, cleared with the G-buffer each frame, and the
/// water keeps its facts in the first two bits (alwaterstencil.h). The blended pass keeps one more, in the next.
namespace ALBlendStencil
{

/// Where a blended surface shades: set by the forward alpha pass wherever its fragments pass the depth test. The
/// G-buffer stops at the opaque surface behind a blended one, so what it knows of those pixels (the albedo the
/// exposure meter divides by) is not what is on screen there.
constexpr GLuint BLENDED_BIT = 1u << 2;

/// The forward alpha pass's draws: the blended bit set wherever a fragment passes the scene's depth test, and no
/// other bit touched.
class Mark : public ALGLStencil
{
public:
    Mark() : ALGLStencil(GL_ALWAYS, BLENDED_BIT, 0u, GL_KEEP, GL_KEEP, GL_REPLACE, BLENDED_BIT) {}
};

/// The exposure meter's mask draw: where the blended bit is set.
class OnBlended : public ALGLStencil
{
public:
    OnBlended() : ALGLStencil(GL_EQUAL, BLENDED_BIT, BLENDED_BIT, GL_KEEP, GL_KEEP, GL_KEEP, 0u) {}
};

} // namespace ALBlendStencil
