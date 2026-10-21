/**
 * @file llappearance_test_fakes.cpp
 * @brief What llappearance's tests link in place of the viewer
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

// One definition each of what llappearance asks the viewer for, linked into
// every test binary that links the texture layers. A test file holds none of
// its own: two copies in one binary would not link.

#include "linden_common.h"

#include "llglslshader.h"

// The texture layers draw their alpha masks through it; the viewer and the
// appearance utility each define their own. Nothing under test draws one.
LLGLSLShader gAlphaMaskProgram;
