/**
 * @file alnotecarditems.h
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

#pragma once

#include "stdtypes.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The notecard format stands each item a notecard carries in its text as a
// character past the last the standard assigns, the first item's the first
// of them -- LLTextEditor::FIRST_EMBEDDED_CHAR, which alnotecarditems.cpp
// checks this agrees with. Every one of them is four bytes in UTF-8, the
// first of them F4, so a text is read and renumbered in its bytes, where
// the editor keeps it.
namespace ALNotecardItems
{
    constexpr U32    FIRST_CHAR = 0x100000;
    constexpr size_t CHAR_BYTES = 4;
    // As many items as there are such characters.
    constexpr size_t MOST       = 0x10000;

    // The item the character starting at a byte stands for, or -1 where no
    // such character starts there.
    S32 itemAt(std::string_view bytes, size_t at);
    // The character that stands for an item, as the text holds it.
    std::string charOf(size_t item);
    // Every such character in the bytes, in order: where it starts and
    // the item it stands for.
    void forEach(std::string_view bytes, const std::function<void(size_t at, size_t item)>& each);
    // The text as a save sends it: every item it stands that `carried` says
    // is carried, numbered afresh from nought in the order the text first
    // stands it, and any other such character left as it is. Answers the
    // number each had before, in their new order: the items to send.
    std::vector<size_t> renumber(std::string& text, const std::function<bool(size_t item)>& carried);
}
