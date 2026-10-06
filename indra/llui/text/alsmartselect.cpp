/**
 * @file alsmartselect.cpp
 * @brief What a selection grows to, step by step, as code reads it: a name, a string, a bracket's inside and the bracket, the lines, the text.
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

#include "alsmartselect.h"

#include "albracketindex.h"
#include "alsyntaxhighlighter.h"
#include "altextchars.h"

#include <vector>

namespace ALSmartSelect
{
std::optional<ALTextRange> grow(const ALTextDocument& doc, ALSyntaxHighlighter& highlighter, ALBracketIndex& brackets, const ALTextRange& selection,
                                std::string_view member_separators)
{
    const ALTextRange        normal = selection.normalised();
    const ALTextRange        s(doc.clamp(normal.begin), doc.clamp(normal.end));
    std::vector<ALTextRange> around;
    if (s.begin.line == s.end.line)
    {
        const std::string& line = doc.line(s.begin.line);
        const S32          size = static_cast<S32>(line.size());
        // The run of such bytes the selection is in, where it is all of them.
        const auto run = [&](const auto& in) {
            for (S32 k = s.begin.column; k < s.end.column; ++k)
            {
                if (!in(line[static_cast<size_t>(k)]))
                {
                    return;
                }
            }
            S32 begin = s.begin.column;
            S32 end   = s.end.column;
            while (begin > 0 && in(line[static_cast<size_t>(begin - 1)]))
            {
                --begin;
            }
            while (end < size && in(line[static_cast<size_t>(end)]))
            {
                ++end;
            }
            if (end > begin)
            {
                around.emplace_back(ALTextPos(s.begin.line, begin), ALTextPos(s.begin.line, end));
            }
        };
        run([](char c) { return alWordByte(c); });
        run([&](char c) { return alWordByte(c) || member_separators.find(c) != std::string_view::npos; });
        // The string or comment it is in: a string's inside first.
        for (const ALSyntaxToken& token : highlighter.tokens(s.begin.line))
        {
            if (token.begin > s.begin.column || token.end < s.end.column)
            {
                continue;
            }
            const ALTextRange whole(ALTextPos(s.begin.line, token.begin), ALTextPos(s.begin.line, token.end));
            if (token.kind == ALSyntaxKind::String && token.end - token.begin >= 2 &&
                (line[static_cast<size_t>(token.begin)] == '"' || line[static_cast<size_t>(token.begin)] == '\'') &&
                line[static_cast<size_t>(token.end - 1)] == line[static_cast<size_t>(token.begin)])
            {
                around.emplace_back(ALTextPos(s.begin.line, token.begin + 1), ALTextPos(s.begin.line, token.end - 1));
                around.push_back(whole);
            }
            else if (token.kind == ALSyntaxKind::String || token.kind == ALSyntaxKind::Comment || token.kind == ALSyntaxKind::DocComment)
            {
                around.push_back(whole);
            }
        }
    }
    // Each kind's pair around it: its inside, then the pair.
    for (const char opener : { '(', '[', '{' })
    {
        ALTextPos open;
        ALTextPos close;
        if (brackets.enclosing(s.begin, opener, 1, open, ALBracketIndex::NEARBY) && brackets.match(open, close, ALBracketIndex::NEARBY) &&
            !(close < s.end))
        {
            around.emplace_back(ALTextPos(open.line, open.column + 1), close);
            around.emplace_back(open, ALTextPos(close.line, close.column + 1));
        }
    }
    around.emplace_back(doc.lineStart(s.begin.line), doc.lineEnd(s.end.line));
    around.emplace_back(doc.start(), doc.end());
    // The smallest that holds it and is more.
    const size_t               from = doc.offsetOf(s.begin);
    const size_t               to   = doc.offsetOf(s.end);
    std::optional<ALTextRange> best;
    size_t                     size = 0;
    for (const ALTextRange& range : around)
    {
        const size_t begin = doc.offsetOf(range.begin);
        const size_t end   = doc.offsetOf(range.end);
        if (begin > from || end < to || (begin == from && end == to))
        {
            continue;
        }
        if (!best || end - begin < size)
        {
            best = range;
            size = end - begin;
        }
    }
    return best;
}
}
