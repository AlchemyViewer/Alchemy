/**
 * @file albracketindex.cpp
 * @brief Where a text's brackets pair up, found by the line rather than by the byte.
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

#include "albracketindex.h"

#include "alsyntaxhighlighter.h"

#include <algorithm>

namespace
{
    // Which kind a bracket is: round, square, curly.
    size_t kindOf(char c)
    {
        return (c == '(' || c == ')') ? 0 : (c == '[' || c == ']') ? 1 : 2;
    }

    bool opener(char c) { return c == '(' || c == '[' || c == '{'; }

    // The last line of a search from `line` by `lines`, down or up, within
    // the text.
    S32 reachDown(S32 line, S32 lines, S32 count) { return lines >= count - line ? count - 1 : line + lines; }
    S32 reachUp(S32 line, S32 lines) { return lines >= line ? 0 : line - lines; }
}

ALBracketIndex::ALBracketIndex(ALSyntaxHighlighter* highlighter) : mHighlighter(highlighter) {}

void ALBracketIndex::attach(const ALTextDocument* document)
{
    mDocument = document;
    mConnection.disconnect();
    if (mDocument)
    {
        mConnection = const_cast<ALTextDocument*>(mDocument)->onChanged([this](const ALTextDocument::Edit& edit) { edited(edit); });
    }
    reset();
}

void ALBracketIndex::reset()
{
    mLines.assign(mDocument ? static_cast<size_t>(mDocument->lineCount()) : 0, Line());
    mDepthBefore.clear();
    mDepthKnown = 0;
}

// static
bool ALBracketIndex::bracketOf(char c, char& partner, bool& opens)
{
    switch (c)
    {
        case '(': partner = ')'; opens = true;  return true;
        case '[': partner = ']'; opens = true;  return true;
        case '{': partner = '}'; opens = true;  return true;
        case ')': partner = '('; opens = false; return true;
        case ']': partner = '['; opens = false; return true;
        case '}': partner = '{'; opens = false; return true;
        default:  return false;
    }
}

void ALBracketIndex::edited(const ALTextDocument::Edit& edit)
{
    const std::vector<ALTextDocument::Edit::LineSpan>& spans = edit.lineSpans();
    if (spans.empty() || !mDocument)
    {
        return;
    }
    mLines.applySpans(spans, mDocument->lineCount(), Line(), Line());
    // What enters the first line it touched is as it was; below, no more.
    mDepthKnown = llmin(mDepthKnown, llmax(0, spans.front().first) + 1);
}

const ALBracketIndex::Line& ALBracketIndex::lineAt(S32 line)
{
    static const Line NONE;
    if (!mDocument || line < 0 || line >= mDocument->lineCount())
    {
        return NONE;
    }
    if (mLines.size() != static_cast<size_t>(mDocument->lineCount()))
    {
        mLines.resize(static_cast<size_t>(mDocument->lineCount()));
    }
    Line&     entry    = mLines[static_cast<size_t>(line)];
    const U32 revision = mHighlighter ? mHighlighter->revision(line) : 0;
    if (entry.valid && entry.revision == revision)
    {
        return entry;
    }
    // Summed up again: its text changed, or its tokens did -- a comment
    // opened above it, say -- which may change the depth below it too.
    if (entry.valid && mDepthKnown > line + 1)
    {
        mDepthKnown = line + 1;
    }
    entry          = Line();
    entry.valid    = true;
    entry.revision = revision;
    const std::string&                text   = mDocument->line(line);
    const std::vector<ALSyntaxToken>* tokens = mHighlighter ? &mHighlighter->tokens(line) : nullptr;
    size_t                            t      = 0;
    S32                               run[3] = { 0, 0, 0 };
    S32                               all    = 0;
    for (S32 i = 0; i < static_cast<S32>(text.size()); ++i)
    {
        char partner = 0;
        bool opens   = false;
        if (!bracketOf(text[static_cast<size_t>(i)], partner, opens))
        {
            continue;
        }
        // The tokens are in order: a cursor over them, not a search.
        if (tokens)
        {
            while (t < tokens->size() && (*tokens)[t].end <= i)
            {
                ++t;
            }
            // A bracket in a string or a comment is none.
            if (t < tokens->size() && (*tokens)[t].begin <= i && alSyntaxKindIsQuiet((*tokens)[t].kind))
            {
                continue;
            }
        }
        const char   c = text[static_cast<size_t>(i)];
        const size_t k = kindOf(c);
        entry.brackets.emplace_back(i, c);
        run[k] += opens ? 1 : -1;
        entry.fromStart[k] = llmin(entry.fromStart[k], run[k]);
        all += opens ? 1 : -1;
        entry.allLowest = llmin(entry.allLowest, all);
    }
    S32 back[3] = { 0, 0, 0 };
    for (auto it = entry.brackets.rbegin(); it != entry.brackets.rend(); ++it)
    {
        const size_t k = kindOf(it->second);
        back[k] += opener(it->second) ? -1 : 1;
        entry.fromEnd[k] = llmin(entry.fromEnd[k], back[k]);
    }
    for (size_t k = 0; k < 3; ++k)
    {
        entry.net[k] = run[k];
    }
    entry.allNet = all;
    return entry;
}

const std::vector<std::pair<S32, char>>& ALBracketIndex::bracketsOn(S32 line)
{
    return lineAt(line).brackets;
}

S32 ALBracketIndex::depthBefore(S32 line)
{
    if (!mDocument || line <= 0)
    {
        return 0;
    }
    // Another grammar: every line's tokens may be other, and what was
    // summed up of them other with them.
    const void* grammar = mHighlighter ? static_cast<const void*>(mHighlighter->grammar().get()) : nullptr;
    if (grammar != mGrammar)
    {
        mGrammar = grammar;
        reset();
    }
    const S32 count = mDocument->lineCount();
    line            = llmin(line, count);
    mDepthBefore.resize(static_cast<size_t>(count) + 1, 0);
    if (mDepthKnown == 0)
    {
        mDepthBefore[0] = 0;
        mDepthKnown     = 1;
    }
    // Carried on from the last line known: a closer past none closes
    // nothing, so what a line takes away is no more than was open.
    for (S32 l = mDepthKnown - 1; l < line; ++l)
    {
        const Line& summary = lineAt(l);
        const S32   before  = mDepthBefore[static_cast<size_t>(l)];
        mDepthBefore[static_cast<size_t>(l) + 1] = before + summary.allNet - llmin(0, before + summary.allLowest);
        mDepthKnown                             = l + 2;
    }
    return mDepthBefore[static_cast<size_t>(line)];
}

S32 ALBracketIndex::depthAt(const ALTextPos& at)
{
    if (!mDocument)
    {
        return 0;
    }
    const ALTextPos pos   = mDocument->clamp(at);
    S32             depth = depthBefore(pos.line);
    for (const auto& [column, c] : lineAt(pos.line).brackets)
    {
        if (column >= pos.column)
        {
            break;
        }
        depth = llmax(0, depth + (opener(c) ? 1 : -1));
    }
    return depth;
}

bool ALBracketIndex::match(const ALTextPos& at, ALTextPos& out, S32 lines)
{
    if (!mDocument || at.line < 0 || at.line >= mDocument->lineCount())
    {
        return false;
    }
    const std::vector<std::pair<S32, char>>& here = lineAt(at.line).brackets;
    const auto found = std::lower_bound(here.begin(), here.end(), at.column, [](const std::pair<S32, char>& b, S32 column) { return b.first < column; });
    if (found == here.end() || found->first != at.column)
    {
        return false;
    }
    const size_t kind  = kindOf(found->second);
    const S32    count = mDocument->lineCount();
    S32          depth = 0;
    const auto   step  = [&](const std::pair<S32, char>& b, S32 line, bool forward) {
        if (kindOf(b.second) != kind)
        {
            return false;
        }
        depth += (opener(b.second) == forward) ? 1 : -1;
        if (depth == 0)
        {
            out = ALTextPos(line, b.first);
            return true;
        }
        return false;
    };
    if (opener(found->second))
    {
        for (auto it = found; it != here.end(); ++it)
        {
            if (step(*it, at.line, true))
            {
                return true;
            }
        }
        const S32 last = reachDown(at.line, lines, count);
        for (S32 l = at.line + 1; l <= last; ++l)
        {
            const Line& summary = lineAt(l);
            if (depth + summary.fromStart[kind] > 0)
            {
                // Not on this line: its brackets of the kind never close
                // as many as are open.
                depth += summary.net[kind];
                continue;
            }
            for (const auto& b : summary.brackets)
            {
                if (step(b, l, true))
                {
                    return true;
                }
            }
        }
        return false;
    }
    for (auto it = std::make_reverse_iterator(found + 1); it != here.rend(); ++it)
    {
        if (step(*it, at.line, false))
        {
            return true;
        }
    }
    const S32 first = reachUp(at.line, lines);
    for (S32 l = at.line - 1; l >= first; --l)
    {
        const Line& summary = lineAt(l);
        if (depth + summary.fromEnd[kind] > 0)
        {
            depth -= summary.net[kind];
            continue;
        }
        for (auto it = summary.brackets.rbegin(); it != summary.brackets.rend(); ++it)
        {
            if (step(*it, l, false))
            {
                return true;
            }
        }
    }
    return false;
}

bool ALBracketIndex::enclosing(const ALTextPos& at, char bracket, S32 count, ALTextPos& out, S32 lines)
{
    char partner = 0;
    bool opens   = false;
    if (!mDocument || count < 1 || !bracketOf(bracket, partner, opens) || at.line < 0 || at.line >= mDocument->lineCount())
    {
        return false;
    }
    const size_t kind  = kindOf(bracket);
    S32          depth = 0;
    S32          found = 0;
    // A bracket of the kind met: one the other way is nesting, one this
    // way is left open where nothing nested is open inside it.
    const auto step = [&](const std::pair<S32, char>& b, S32 line) {
        if (kindOf(b.second) != kind)
        {
            return false;
        }
        if (opener(b.second) != opens)
        {
            ++depth;
            return false;
        }
        if (depth > 0)
        {
            --depth;
            return false;
        }
        if (++found == count)
        {
            out = ALTextPos(line, b.first);
            return true;
        }
        return false;
    };
    const std::vector<std::pair<S32, char>>& here = lineAt(at.line).brackets;
    if (opens)
    {
        // Back from the place, not counting it.
        for (auto it = here.rbegin(); it != here.rend(); ++it)
        {
            if (it->first < at.column && step(*it, at.line))
            {
                return true;
            }
        }
        const S32 first = reachUp(at.line, lines);
        for (S32 l = at.line - 1; l >= first; --l)
        {
            const Line& summary = lineAt(l);
            if (depth + summary.fromEnd[kind] >= 0)
            {
                // None left open on this line.
                depth -= summary.net[kind];
                continue;
            }
            for (auto it = summary.brackets.rbegin(); it != summary.brackets.rend(); ++it)
            {
                if (step(*it, l))
                {
                    return true;
                }
            }
        }
        return false;
    }
    for (const auto& b : here)
    {
        if (b.first > at.column && step(b, at.line))
        {
            return true;
        }
    }
    const S32 last = reachDown(at.line, lines, mDocument->lineCount());
    for (S32 l = at.line + 1; l <= last; ++l)
    {
        const Line& summary = lineAt(l);
        if (depth + summary.fromStart[kind] >= 0)
        {
            depth += summary.net[kind];
            continue;
        }
        for (const auto& b : summary.brackets)
        {
            if (step(b, l))
            {
                return true;
            }
        }
    }
    return false;
}
