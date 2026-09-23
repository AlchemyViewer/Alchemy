/**
 * @file alnotecarditems.cpp
 * @brief The characters a notecard's text stands its items by.
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

#include "alnotecarditems.h"

#include "lltexteditor.h"

#include <boost/unordered/unordered_flat_map.hpp>

// The characters are read here and made by the text editor the legacy
// notecard uses: the two must agree.
static_assert(ALNotecardItems::FIRST_CHAR == LLTextEditor::FIRST_EMBEDDED_CHAR &&
                  ALNotecardItems::MOST == static_cast<size_t>(LLTextEditor::MAX_EMBEDDED_ITEMS),
              "a notecard's item characters are numbered one way");

namespace ALNotecardItems
{
S32 itemAt(std::string_view bytes, size_t at)
{
    if (at + CHAR_BYTES > bytes.size())
    {
        return -1;
    }
    const unsigned char b0 = static_cast<unsigned char>(bytes[at]);
    const unsigned char b1 = static_cast<unsigned char>(bytes[at + 1]);
    const unsigned char b2 = static_cast<unsigned char>(bytes[at + 2]);
    const unsigned char b3 = static_cast<unsigned char>(bytes[at + 3]);
    // F4 8x: the sixteenth plane and nothing past it, which is where they
    // all are and where the standard stops.
    if (b0 != 0xF4 || (b1 & 0xF0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
    {
        return -1;
    }
    const U32 code = ((b0 & 7u) << 18) | ((b1 & 0x3Fu) << 12) | ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu);
    return static_cast<S32>(code - FIRST_CHAR);
}

std::string charOf(size_t item)
{
    llassert(item < MOST);
    const U32   code = FIRST_CHAR + static_cast<U32>(item);
    std::string bytes(CHAR_BYTES, '\0');
    bytes[0] = static_cast<char>(0xF0 | (code >> 18));
    bytes[1] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    bytes[2] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    bytes[3] = static_cast<char>(0x80 | (code & 0x3F));
    return bytes;
}

void forEach(std::string_view bytes, const std::function<void(size_t at, size_t item)>& each)
{
    for (size_t i = 0; i + CHAR_BYTES <= bytes.size(); ++i)
    {
        const S32 item = itemAt(bytes, i);
        if (item >= 0)
        {
            each(i, static_cast<size_t>(item));
            // Past the rest of it: none of its bytes starts another.
            i += CHAR_BYTES - 1;
        }
    }
}

std::vector<size_t> renumber(std::string& text, const std::function<bool(size_t item)>& carried)
{
    std::vector<size_t>                       order;
    boost::unordered_flat_map<size_t, size_t> fresh;
    std::vector<std::pair<size_t, size_t>>    rewrites;
    forEach(text, [&](size_t at, size_t item) {
        if (!carried(item))
        {
            return;
        }
        const auto [found, first] = fresh.try_emplace(item, order.size());
        if (first)
        {
            order.push_back(item);
        }
        rewrites.emplace_back(at, found->second);
    });
    // Every character the same width, so each goes where the old one was.
    for (const auto& [at, item] : rewrites)
    {
        text.replace(at, CHAR_BYTES, charOf(item));
    }
    return order;
}
}
