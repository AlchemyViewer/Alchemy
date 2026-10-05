/**
 * @file aldiffedit.cpp
 * @brief Lines of a text put in place of others, as one edit of it.
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

#include "aldiffedit.h"

bool ALDiffEdit::replaceLines(const std::string& text, const std::vector<std::string>& lines, S32 first, S32 count,
                              const std::vector<std::string>& with, ALTextRange& range, std::string& put, std::string& made)
{
    if (lines.empty() || (count <= 0 && with.empty()))
    {
        return false;
    }
    std::string joined;
    for (size_t n = 0; n < with.size(); ++n)
    {
        joined += (n ? "\n" : "") + with[n];
    }
    const S32  last = static_cast<S32>(lines.size()) - 1;
    const auto ends = [&](S32 line) { return ALTextPos(line, static_cast<S32>(lines[static_cast<size_t>(line)].size())); };
    put.clear();
    if (count > 0 && !with.empty())
    {
        range = ALTextRange(ALTextPos(first, 0), ends(first + count - 1));
        put   = joined;
    }
    else if (count > 0)
    {
        const S32 after = first + count;
        range           = after <= last ? ALTextRange(ALTextPos(first, 0), ALTextPos(after, 0))
                          : first > 0   ? ALTextRange(ends(first - 1), ends(last))
                                        : ALTextRange(ALTextPos(0, 0), ends(last));
    }
    else
    {
        range = first <= last ? ALTextRange(ALTextPos(first, 0), ALTextPos(first, 0)) : ALTextRange(ends(last), ends(last));
        put   = first <= last ? joined + "\n" : "\n" + joined;
    }
    // The text as it will be, by where each line of it starts.
    std::vector<size_t> starts{ 0 };
    for (size_t at = text.find('\n'); at != std::string::npos; at = text.find('\n', at + 1))
    {
        starts.push_back(at + 1);
    }
    const auto offset = [&](const ALTextPos& pos) { return starts[static_cast<size_t>(pos.line)] + static_cast<size_t>(pos.column); };
    made              = text.substr(0, offset(range.begin)) + put + text.substr(offset(range.end));
    return true;
}
