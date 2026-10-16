/**
 * @file lltextutil.cpp
 * @brief Misc text-related auxiliary methods
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "lltextutil.h"

#include "lluicolor.h"
#include "lltextbox.h"
#include "llurlmatch.h"

std::function<bool(LLUrlMatch*, LLTextBase*)> LLTextUtil::TextHelpers::iconCallbackCreationFunction = nullptr;

void LLTextUtil::textboxSetHighlightedVal(LLTextBox *txtbox, const LLStyle::Params& normal_style, const std::string& text, const std::string& hl)
{
    static LLUIColor sFilterTextColor = LLUIColorTable::instance().getColor("FilterTextColor", LLColor4::green);

    if (hl.empty())
    {
        txtbox->setText(text, normal_style);
        return;
    }

    std::string text_uc = text;
    LLStringUtil::toUpper(text_uc);

    const size_t cased_begin = text_uc.find(hl);
    if (cased_begin == std::string::npos)
    {
        txtbox->setText(text, normal_style);
        return;
    }

    // The match was found in the uppercased copy, and uppercasing is not
    // length-preserving -- sharp s grows a byte, and so do the ligatures --
    // so both ends have to come back through the fold before they can index
    // `text`. Taking them straight across highlights the wrong characters,
    // and substr throws outright once the copy has grown past the original.
    const size_t hl_begin = utf8str_bytes_from_cased_bytes(text, cased_begin, true);
    const size_t hl_end   = utf8str_bytes_from_cased_bytes(text, cased_begin + hl.size(), true);
    const size_t hl_len   = hl_end - hl_begin;

    LLStyle::Params hl_style = normal_style;
    hl_style.color = sFilterTextColor;

    // Slice before clearing. `text` is routinely the box's own content --
    // LLAvatarListItem passes mAvatarName->getText() straight back in -- and
    // setText() empties the string it refers to, so every offset taken above
    // would then index a string that no longer holds anything.
    const std::string before = text.substr(0, hl_begin);
    const std::string match  = text.substr(hl_begin, hl_len);
    const std::string after  = text.substr(hl_begin + hl_len);

    txtbox->setText(LLStringUtil::null); // clear text
    txtbox->appendText(before, false, normal_style);
    txtbox->appendText(match,  false, hl_style);
    txtbox->appendText(after,  false, normal_style);
}

void LLTextUtil::textboxSetGreyedVal(LLTextBox *txtbox, const LLStyle::Params& normal_style, const std::string& text, const std::string& greyed)
{
    static LLUIColor sGreyedTextColor = LLUIColorTable::instance().getColor("Gray", LLColor4::grey);

    size_t greyed_begin = 0, greyed_len = greyed.size();

    if (greyed_len == 0 || (greyed_begin = text.find(greyed)) == std::string::npos)
    {
        txtbox->setText(text, normal_style);
        return;
    }

    LLStyle::Params greyed_style = normal_style;
    greyed_style.color = sGreyedTextColor;

    // Slice before clearing -- see textboxSetHighlightedVal above.
    const std::string before = text.substr(0, greyed_begin);
    const std::string match  = text.substr(greyed_begin, greyed_len);
    const std::string after  = text.substr(greyed_begin + greyed_len);

    txtbox->setText(LLStringUtil::null); // clear text
    txtbox->appendText(before, false, normal_style);
    txtbox->appendText(match,  false, greyed_style);
    txtbox->appendText(after,  false, normal_style);
}

bool LLTextUtil::processUrlMatch(LLUrlMatch* match,LLTextBase* text_base, bool is_content_trusted)
{
    if (match == 0 || text_base == 0)
        return false;

    if(match->getID() != LLUUID::null && TextHelpers::iconCallbackCreationFunction)
    {
        bool segment_created = TextHelpers::iconCallbackCreationFunction(match,text_base);
        if(segment_created)
            return true;
    }

    // output an optional icon before the Url
    if (is_content_trusted && !match->getIcon().empty() )
    {
        LLUIImagePtr image = LLUI::getUIImage(match->getIcon());
        if (image)
        {
            LLStyle::Params icon;
            icon.image = image;
            // Text will be replaced during rendering with the icon,
            // but string cannot be empty or the segment won't be
            // added (or drawn).
            text_base->appendImageSegment(icon);

            return true;
        }
    }

    return false;
}

// EOF
