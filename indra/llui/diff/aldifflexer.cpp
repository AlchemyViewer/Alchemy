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
#include <iterator>

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

S32 ALDiffLexer::Source::find(const std::string& line, const ALSyntaxState& state)
{
    // A line said often -- a closing brace -- tried at so many places.
    constexpr S32 MOST_TRIED = 8;
    const auto    take       = [this](size_t at) {
        mNext = at + 1;
        return static_cast<S32>(at);
    };
    const std::vector<std::string>&   lines  = mText.lines;
    const std::vector<ALSyntaxState>& starts = mText.starts;
    if (mNext < lines.size() && lines[mNext] == line && starts[mNext] == state)
    {
        return take(mNext);
    }
    // Past lines put in or taken out: by its text, the nearest place at or
    // past the next looked for that starts as it does.
    if (!mIndexed)
    {
        mIndexed = true;
        mPlaces.reserve(lines.size());
        for (size_t at = 0; at < lines.size(); ++at)
        {
            mPlaces[lines[at]].push_back(static_cast<S32>(at));
        }
    }
    const auto found = mPlaces.find(std::string_view(line));
    if (found == mPlaces.end())
    {
        return -1;
    }
    const std::vector<S32>& at    = found->second;
    S32                     tried = 0;
    for (auto it = std::lower_bound(at.begin(), at.end(), static_cast<S32>(mNext)); it != at.end() && tried < MOST_TRIED; ++it, ++tried)
    {
        if (starts[static_cast<size_t>(*it)] == state)
        {
            return take(static_cast<size_t>(*it));
        }
    }
    return -1;
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
    // Else in the place of the one read longer ago, which leaves the last
    // answered as it was: a comparison asks for its two texts in turn and
    // holds both. Read again in place where that is mostly this text; else
    // whole, taking what either held where it can.
    const size_t older = mTexts[0].used <= mTexts[1].used ? 0 : 1;
    Text&        text  = mTexts[older];
    if (!text.starts.empty())
    {
        const ALDiffEdit::Edges edged = known[older] ? edges[older] : ALDiffEdit::edgesOf(text.lines, lines);
        if (static_cast<size_t>(edged.head + edged.tail) * 2 >= lines.size())
        {
            readAgain(text, lines, edged);
            text.used = ++mClock;
            return text.regions;
        }
    }
    readWhole(text, lines, mTexts[1 - older]);
    text.used = ++mClock;
    return text.regions;
}

void ALDiffLexer::readAgain(Text& text, const std::vector<std::string>& lines, const ALDiffEdit::Edges& edges)
{
    // The middle of the kept text made this one's, in place: what is after
    // it moved along with the regions and the state each of its lines
    // started in, which a line read again must meet to stop.
    const size_t head     = static_cast<size_t>(edges.head);
    const size_t tail     = static_cast<size_t>(edges.tail);
    const size_t old_size = text.lines.size();
    const size_t old_end  = old_size - tail;
    const size_t new_end  = lines.size() - tail;
    const auto   resize   = [&](auto& list) { ALDiffEdit::resizeEdited(list, static_cast<S32>(old_end), static_cast<S32>(new_end)); };
    ALSyntaxState state = text.starts[head];
    // What it held between its edges set aside, to take lines from.
    Text middle;
    middle.lines.assign(std::make_move_iterator(text.lines.begin() + static_cast<std::ptrdiff_t>(head)),
                        std::make_move_iterator(text.lines.begin() + static_cast<std::ptrdiff_t>(old_end)));
    middle.regions.assign(std::make_move_iterator(text.regions.begin() + static_cast<std::ptrdiff_t>(head)),
                          std::make_move_iterator(text.regions.begin() + static_cast<std::ptrdiff_t>(old_end)));
    middle.starts.assign(text.starts.begin() + static_cast<std::ptrdiff_t>(head), text.starts.begin() + static_cast<std::ptrdiff_t>(old_end) + 1);
    Source held(middle);
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
            return;
        }
        text.starts[line] = state;
        if (line < new_end)
        {
            readOrTake(lines[line], state, text.regions[line], &held, 1);
        }
        else
        {
            read(lines[line], state, text.regions[line]);
        }
    }
    text.starts[lines.size()] = std::move(state);
}

void ALDiffLexer::readWhole(Text& text, const std::vector<std::string>& lines, const Text& other)
{
    const Text was = std::move(text);
    text           = Text();
    text.lines     = lines;
    text.regions.resize(lines.size());
    text.starts.resize(lines.size() + 1);
    Source        sources[2] = { Source(was), Source(other) };
    ALSyntaxState state      = mGrammar ? mGrammar->initialState() : ALSyntaxState();
    for (size_t line = 0; line < lines.size(); ++line)
    {
        text.starts[line] = state;
        readOrTake(lines[line], state, text.regions[line], sources, 2);
    }
    text.starts[lines.size()] = std::move(state);
}

void ALDiffLexer::readOrTake(const std::string& line, ALSyntaxState& state, ALTextDiff::regions_t& out, Source* sources, size_t count)
{
    for (size_t n = 0; n < count; ++n)
    {
        if (const S32 at = sources[n].find(line, state); at >= 0)
        {
            out   = sources[n].text().regions[static_cast<size_t>(at)];
            state = sources[n].text().starts[static_cast<size_t>(at) + 1];
            return;
        }
    }
    read(line, state, out);
}

void ALDiffLexer::read(const std::string& line, ALSyntaxState& state, ALTextDiff::regions_t& out)
{
    out.clear();
    if (mGrammar)
    {
        mGrammar->lexLine(line, state, mTokens, mWords);
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
