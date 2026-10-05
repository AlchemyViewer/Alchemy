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

#include "allinebreaks.h"

#include <algorithm>

ALDiffEdit::Edges ALDiffEdit::edgesOf(const std::vector<std::string>& was, const std::vector<std::string>& now)
{
    // One text with itself: the same throughout, read nowhere.
    if (&was == &now)
    {
        return Edges{ static_cast<S32>(was.size()), 0 };
    }
    const size_t most = std::min(was.size(), now.size());
    size_t       head = 0;
    while (head < most && was[head] == now[head])
    {
        ++head;
    }
    size_t tail = 0;
    while (tail < most - head && was[was.size() - 1 - tail] == now[now.size() - 1 - tail])
    {
        ++tail;
    }
    return Edges{ static_cast<S32>(head), static_cast<S32>(tail) };
}

bool ALDiffEdit::replaceLines(const std::vector<std::string>& lines, S32 first, S32 count, const std::vector<std::string>& with, ALTextRange& range,
                              std::string& put, std::string& made)
{
    if (lines.empty() || (count <= 0 && with.empty()))
    {
        return false;
    }
    const std::string joined = ALLineBreaks::join(with);
    const S32         last   = static_cast<S32>(lines.size()) - 1;
    const auto        ends   = [&](S32 line) { return ALTextPos(line, static_cast<S32>(lines[static_cast<size_t>(line)].size())); };
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
    // The text as it will be: the lines before, those put in, and those
    // after, as the edit leaves them.
    made.clear();
    bool       started = false;
    const auto append  = [&](const std::string& line) {
        if (started)
        {
            made.push_back('\n');
        }
        made += line;
        started = true;
    };
    for (S32 n = 0; n < first && n <= last; ++n)
    {
        append(lines[static_cast<size_t>(n)]);
    }
    for (const std::string& line : with)
    {
        append(line);
    }
    for (S32 n = first + count; n <= last; ++n)
    {
        append(lines[static_cast<size_t>(n)]);
    }
    return true;
}

bool ALDiffEdit::becoming(const std::vector<std::string>& was, const std::vector<std::string>& now, ALTextRange& range, std::string& put,
                          std::string& made)
{
    const Edges                    edges = edgesOf(was, now);
    const std::vector<std::string> with(now.begin() + edges.head, now.end() - edges.tail);
    return replaceLines(was, edges.head, static_cast<S32>(was.size()) - edges.head - edges.tail, with, range, put, made);
}
