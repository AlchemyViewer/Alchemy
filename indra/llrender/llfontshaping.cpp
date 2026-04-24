/**
 * @file llfontshaping.cpp
 * @brief HarfBuzz shaping for multi-codepoint emoji sequences.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llfontshaping.h"
#include "llfontfreetype.h"

#include <hb.h>

void LLFontShaping::shapeRun(const LLFontFreetype* root_face,
                             const LLWString&      wstr,
                             size_t                begin,
                             size_t                end,
                             std::vector<LLShapedGlyph>& out_glyphs)
{
    out_glyphs.clear();

    if (!root_face || begin >= end || end > wstr.size())
        return;

    U32 base_glyph = 0; // Unused here; selectShapingFace returns it as a side effect.
    const LLFontFreetype* face = root_face->selectShapingFace(wstr[begin], base_glyph);
    if (!face)
        return;

    hb_font_t* hb_font = face->getHbFont();
    if (!hb_font)
        return;

    hb_buffer_t* buf = hb_buffer_create();
    if (!buf)
        return;

    // LLWString is u32string, so its elements are already uint32_t code
    // points — just reinterpret-cast for HB's C API.
    const uint32_t* codepoints = reinterpret_cast<const uint32_t*>(wstr.data() + begin);
    const int       len        = static_cast<int>(end - begin);
    hb_buffer_add_utf32(buf, codepoints, len, 0, len);

    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, HB_SCRIPT_COMMON);
    hb_buffer_set_language(buf, hb_language_get_default());

    hb_shape(hb_font, buf, nullptr, 0);

    unsigned int glyph_count = 0;
    const hb_glyph_info_t*     infos     = hb_buffer_get_glyph_infos(buf, &glyph_count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buf, &glyph_count);

    out_glyphs.reserve(glyph_count);
    constexpr F32 INV_64 = 1.f / 64.f;
    for (unsigned int i = 0; i < glyph_count; ++i)
    {
        LLShapedGlyph sg;
        sg.face      = face;
        // HarfBuzz overloads the "codepoint" field: input codepoints before
        // hb_shape, output glyph indices after.
        sg.glyph_id  = infos[i].codepoint;
        // HB clusters start at 0 for the buffer; shift back into wstr coords.
        sg.cluster   = static_cast<S32>(begin + infos[i].cluster);
        sg.x_advance = positions[i].x_advance * INV_64;
        sg.y_advance = positions[i].y_advance * INV_64;
        sg.x_offset  = positions[i].x_offset  * INV_64;
        sg.y_offset  = positions[i].y_offset  * INV_64;
        out_glyphs.push_back(sg);
    }

    hb_buffer_destroy(buf);
}
