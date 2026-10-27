/**
 * @file alcvar_asm_probe.cpp
 * @brief A handle's read of each scalar type, one function each, for
 *        AlCVarAsmCheck.cmake to read the compiler's assembly of: one load
 *        from the slot's own address, and a return.
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

#include "cvartest/render.h"
#include "cvartest/render/shadows.h"

extern "C"
{
    F32 probe_read_f32()
    {
        return cvartest::render::draw_distance();
    }

    bool probe_read_bool()
    {
        return cvartest::render::shadows::soft();
    }

    S32 probe_read_s32()
    {
        return cvartest::render::shadows::detail();
    }

    U32 probe_read_u32()
    {
        return cvartest::render::shadows::max_maps();
    }

    F64 probe_read_f64()
    {
        return cvartest::render::shadows::bias();
    }

    S64 probe_read_s64()
    {
        return cvartest::render::frame_count();
    }
}
