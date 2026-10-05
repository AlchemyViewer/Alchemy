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

#include <algorithm>

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
    // A text kept as it is, as it was.
    for (Text& text : mTexts)
    {
        if (!text.starts.empty() && text.lines == lines)
        {
            text.used = ++mClock;
            return text.regions;
        }
    }
    // Else the one read longer ago, which leaves the last answered as it
    // was: a comparison asks for its two texts in turn and holds both. The
    // other in its place first where it shares more with this text -- the
    // first edit of a text compared with itself -- since copying it is
    // cheaper than reading lines again.
    const auto shares = [&lines](const Text& text) {
        size_t n = 0;
        while (n < lines.size() && n < text.lines.size() && lines[n] == text.lines[n])
        {
            ++n;
        }
        size_t m = 0;
        while (m < lines.size() - n && m < text.lines.size() - n && lines[lines.size() - 1 - m] == text.lines[text.lines.size() - 1 - m])
        {
            ++m;
        }
        return text.starts.empty() ? 0 : n + m;
    };
    const size_t older = mTexts[0].used <= mTexts[1].used ? 0 : 1;
    Text&        text  = mTexts[older];
    if (shares(mTexts[1 - older]) > shares(text))
    {
        text = mTexts[1 - older];
    }
    text.used = ++mClock;
    if (text.starts.empty())
    {
        text.lines.clear();
        text.regions.clear();
        text.starts.assign(1, mGrammar ? mGrammar->initialState() : ALSyntaxState());
    }
    // What the two share at the start and then at the end; the middle of
    // the kept text made this one's.
    const size_t old_size = text.lines.size();
    size_t       head     = 0;
    while (head < lines.size() && head < old_size && lines[head] == text.lines[head])
    {
        ++head;
    }
    size_t tail = 0;
    while (tail < lines.size() - head && tail < old_size - head && lines[lines.size() - 1 - tail] == text.lines[old_size - 1 - tail])
    {
        ++tail;
    }
    const size_t old_end = old_size - tail;
    const size_t new_end = lines.size() - tail;
    text.lines.erase(text.lines.begin() + static_cast<std::ptrdiff_t>(head), text.lines.begin() + static_cast<std::ptrdiff_t>(old_end));
    text.lines.insert(text.lines.begin() + static_cast<std::ptrdiff_t>(head), lines.begin() + static_cast<std::ptrdiff_t>(head),
                      lines.begin() + static_cast<std::ptrdiff_t>(new_end));
    // The lines after the middle as they were: each's regions and the state
    // it started in, which a line read again must meet to stop.
    std::vector<ALSyntaxState>         kept_starts(text.starts.begin() + static_cast<std::ptrdiff_t>(old_end), text.starts.end());
    std::vector<ALTextDiff::regions_t> kept_regions(std::make_move_iterator(text.regions.begin() + static_cast<std::ptrdiff_t>(old_end)),
                                                    std::make_move_iterator(text.regions.end()));
    text.starts.resize(head + 1);
    text.regions.resize(head);
    for (size_t line = head; line < lines.size(); ++line)
    {
        if (line >= new_end && text.starts[line] == kept_starts[line - new_end])
        {
            // Settled: the rest as they were.
            const auto kept = static_cast<std::ptrdiff_t>(line - new_end);
            text.regions.insert(text.regions.end(), std::make_move_iterator(kept_regions.begin() + kept), std::make_move_iterator(kept_regions.end()));
            text.starts.insert(text.starts.end(), kept_starts.begin() + kept + 1, kept_starts.end());
            return text.regions;
        }
        ALSyntaxState          state = text.starts[line];
        ALTextDiff::regions_t& out   = text.regions.emplace_back();
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
        text.starts.push_back(std::move(state));
        ++mLastRead;
    }
    return text.regions;
}
