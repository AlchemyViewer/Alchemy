/**
 * @file aluigl_fixture.h
 * @brief The one hidden window and GL context llui's GL tests draw into
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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

#ifndef AL_ALUIGL_FIXTURE_H
#define AL_ALUIGL_FIXTURE_H

#include "../../llrender/tests/llheadlessgl_fixture.h"

namespace ll_test
{
    // A hidden window, its GL context current, and the 2D renderer and UI
    // shader set up in it: llrender's shared context, made by the first GL
    // test that asks and kept for the process, so every test file in a
    // binary shares the one. A second would make another window, take the
    // context, put the renderer's singletons through their setup again and,
    // at exit, quit SDL while the first was still alive.
    inline HeadlessGL& uiGL()
    {
        return sharedHeadlessGL();
    }
}

#endif // AL_ALUIGL_FIXTURE_H
