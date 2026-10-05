/**
 * @file aldifflexer.cpp
 * @brief A text's lines read by a grammar into the regions a comparison cuts words by.
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

#include "aldifflexer.h"

#include "aldiffedit.h"

#include <algorithm>
#include <type_traits>

namespace
{
    ALTextDiff::Region regionOf(ALSyntaxKind kind)
    {
        switch (kind)
        {
            case ALSyntaxKind::Comment:
            case ALSyntaxKind::DocComment:
                return ALTextDiff::Region::Comment;
            case ALSyntaxKind::String:
            case ALSyntaxKind::Escape:
            case ALSyntaxKind::Path:
                return ALTextDiff::Region::String;
            default:
                return ALTextDiff::Region::Code;
        }
    }
}

ALDiffLexer::ALDiffLexer(std::shared_ptr<const ALSyntaxGrammar> grammar)
:   mGrammar(std::move(grammar))
{
}

// static
ALTextDiff::lexer_t ALDiffLexer::lexerOf(std::shared_ptr<ALDiffLexer> lexer)
{
    return [lexer](const std::vector<std::string>& lines) -> const std::vector<ALTextDiff::regions_t>& { return lexer->regions(lines); };
}

const std::vector<ALTextDiff::regions_t>& ALDiffLexer::regions(const std::vector<std::string>& lines)
{
    mLastRead = 0;
    // A text kept as it is, as it was: of one as long, where it first
    // differs from this, found once for whichever it is.
    ALDiffEdit::Edges edges[2];
    bool              known[2] = { false, false };
    for (size_t n = 0; n < 2; ++n)
    {
        Text& text = mTexts[n];
        if (text.starts.empty() || text.lines.size() != lines.size())
        {
            continue;
        }
        edges[n] = ALDiffEdit::edgesOf(text.lines, lines);
        known[n] = true;
        if (edges[n].head == static_cast<S32>(lines.size()))
        {
            text.used = ++mClock;
            return text.regions;
        }
    }
    const auto edgesWith = [&](size_t n) {
        if (!known[n])
        {
            edges[n] = mTexts[n].starts.empty() ? ALDiffEdit::Edges() : ALDiffEdit::edgesOf(mTexts[n].lines, lines);
            known[n] = true;
        }
        return edges[n];
    };
    const auto shares = [&](size_t n) { return mTexts[n].starts.empty() ? 0 : edgesWith(n).head + edgesWith(n).tail; };
    // Else the one read longer ago, which leaves the last answered as it
    // was: a comparison asks for its two texts in turn and holds both. The
    // other in its place first where this one shares less than half of the
    // text and the other more -- the first edit of a text compared with
    // itself -- since copying it is cheaper than reading lines again.
    const size_t older = mTexts[0].used <= mTexts[1].used ? 0 : 1;
    const size_t other = 1 - older;
    Text&        text  = mTexts[older];
    if (shares(older) * 2 < static_cast<S32>(lines.size()) && shares(other) > shares(older))
    {
        text          = mTexts[other];
        edges[older]  = edges[other];
    }
    text.used = ++mClock;
    if (text.starts.empty())
    {
        text.lines.clear();
        text.regions.clear();
        text.starts.assign(1, mGrammar ? mGrammar->initialState() : ALSyntaxState());
        edges[older] = ALDiffEdit::Edges();
    }
    // The middle of the kept text made this one's, in place: what is after
    // it moved along with the regions and the state each of its lines
    // started in, which a line read again must meet to stop.
    const size_t head     = static_cast<size_t>(edgesWith(older).head);
    const size_t tail     = static_cast<size_t>(edges[older].tail);
    const size_t old_size = text.lines.size();
    const size_t old_end  = old_size - tail;
    const size_t new_end  = lines.size() - tail;
    const auto   resize   = [&](auto& list) {
        using T = typename std::decay_t<decltype(list)>::value_type;
        if (new_end < old_end)
        {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(new_end), list.begin() + static_cast<std::ptrdiff_t>(old_end));
        }
        else if (new_end > old_end)
        {
            list.insert(list.begin() + static_cast<std::ptrdiff_t>(old_end), new_end - old_end, T());
        }
    };
    ALSyntaxState state = text.starts[head];
    resize(text.lines);
    resize(text.regions);
    resize(text.starts);
    std::copy(lines.begin() + static_cast<std::ptrdiff_t>(head), lines.begin() + static_cast<std::ptrdiff_t>(new_end),
              text.lines.begin() + static_cast<std::ptrdiff_t>(head));
    for (size_t line = head; line < lines.size(); ++line)
    {
        if (line >= new_end && text.starts[line] == state)
        {
            // Settled: the rest as they were.
            return text.regions;
        }
        text.starts[line]            = state;
        ALTextDiff::regions_t& out   = text.regions[line];
        out.clear();
        if (mGrammar)
        {
            mGrammar->lexLine(lines[line], state, mTokens, mWords);
            for (const ALSyntaxToken& token : mTokens)
            {
                const ALTextDiff::Region region = regionOf(token.kind);
                if (!out.empty() && out.back().region == region && out.back().end == token.begin)
                {
                    out.back().end = token.end;
                }
                else
                {
                    out.push_back(ALTextDiff::Piece{ token.begin, token.end, region });
                }
            }
        }
        ++mLastRead;
    }
    text.starts[lines.size()] = std::move(state);
    return text.regions;
}
