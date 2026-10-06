/**
 * @file aldiffedit.h
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

#ifndef AL_ALDIFFEDIT_H
#define AL_ALDIFFEDIT_H

#include "altextdocument.h"

#include <algorithm>
#include <string>
#include <type_traits>
#include <vector>

// Lines of a text put in place of others, as one edit of the text: what a
// comparison's change taken back, a merge's conflict settled and a text
// made another all are.
namespace ALDiffEdit
{
    // Where two texts' lines first differ, and how many they then share at
    // their ends: what differs is [head, size - tail) of each.
    struct Edges
    {
        S32 head = 0;
        S32 tail = 0;
    };
    // Of so many lines and so many, told alike by their places in each:
    // wherever the lines are kept.
    template<typename Same>
    Edges edgesBy(S32 was, S32 now, Same same)
    {
        const S32 most = std::min(was, now);
        S32       head = 0;
        while (head < most && same(head, head))
        {
            ++head;
        }
        S32 tail = 0;
        while (tail < most - head && same(was - 1 - tail, now - 1 - tail))
        {
            ++tail;
        }
        return Edges{ head, tail };
    }
    // Lines or views of them alike; one text with itself the same
    // throughout, read nowhere.
    template<typename Was, typename Now>
    Edges edgesOf(const std::vector<Was>& was, const std::vector<Now>& now)
    {
        if constexpr (std::is_same_v<Was, Now>)
        {
            if (&was == &now)
            {
                return Edges{ static_cast<S32>(was.size()), 0 };
            }
        }
        return edgesBy(static_cast<S32>(was.size()), static_cast<S32>(now.size()),
                       [&](S32 w, S32 n) { return was[static_cast<size_t>(w)] == now[static_cast<size_t>(n)]; });
    }

    // `count` of a text's lines from `first`, counted from nought -- its
    // lines as ALTextDiff::split has them -- replaced by `with`: what
    // stretch of the text to put what in, and the text as it will be, its
    // lines joined by LF. A line's break goes with the lines taken out or
    // put in; at the text's end, the one before them. False where nothing
    // changes: no lines taken out and none put in.
    bool replaceLines(const std::vector<std::string>& lines, S32 first, S32 count, const std::vector<std::string>& with, ALTextRange& range,
                      std::string& put, std::string& made);
    // The one edit that makes a text's lines another's: those between
    // their edges replaced. False where they are the same.
    bool becoming(const std::vector<std::string>& was, const std::vector<std::string>& now, ALTextRange& range, std::string& put,
                  std::string& made);
}

#endif // AL_ALDIFFEDIT_H
