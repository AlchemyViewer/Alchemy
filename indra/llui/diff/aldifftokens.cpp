/**
 * @file aldifftokens.cpp
 * @brief A line cut into the words two lines are compared by.
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

#include "aldifftokens.h"

#include "altextchars.h"

#include <algorithm>

bool ALDiffTokens::blank(char c)
{
    return c == ' ' || c == '\t';
}

ALTextDiff::spans_t ALDiffTokens::words(std::string_view line)
{
    ALTextDiff::spans_t out;
    size_t              i = 0;
    while (i < line.size())
    {
        const unsigned char c   = static_cast<unsigned char>(line[i]);
        size_t              end = i + 1;
        if (alWordByte(static_cast<char>(c)))
        {
            while (end < line.size() && alWordByte(line[end]))
            {
                ++end;
            }
        }
        else if (blank(static_cast<char>(c)))
        {
            while (end < line.size() && blank(line[end]))
            {
                ++end;
            }
        }
        out.emplace_back(static_cast<S32>(i), static_cast<S32>(end));
        i = end;
    }
    return out;
}

namespace
{
    typedef ALDiffTokens::Token    Token;
    typedef ALDiffTokens::tokens_t tokens_t;
    typedef ALTextDiff::Region     Region;

    bool digit(char c) { return c >= '0' && c <= '9'; }

    // The operators of more than one character that the languages compared
    // share or have, longest first: one word each.
    constexpr std::string_view OPERATORS[] = { "...", "..=", "//=", "<<=", ">>=", "==", "!=", "~=", "<=", ">=", "&&", "||", "<<", ">>", "+=",
                                               "-=",  "*=",  "/=",  "%=",  "^=",  "|=", "&=", "++", "--", "..", "::", "->", "=>" };

    void cutCode(std::string_view line, size_t from, size_t to, tokens_t& out)
    {
        size_t i = from;
        while (i < to)
        {
            const char c   = line[i];
            size_t     end = i + 1;
            if (digit(c) || (c == '.' && i + 1 < to && digit(line[i + 1]) && (i == from || !alWordByte(line[i - 1]))))
            {
                // A number: its point where a digit follows it, and its
                // exponent's sign.
                while (end < to)
                {
                    const char n = line[end];
                    if (alWordByte(n))
                    {
                        ++end;
                    }
                    else if (n == '.' && end + 1 < to && digit(line[end + 1]))
                    {
                        end += 2;
                    }
                    else if ((n == '+' || n == '-') && (line[end - 1] == 'e' || line[end - 1] == 'E') && end + 1 < to && digit(line[end + 1]) &&
                             !(end - i > 1 && (line[i + 1] == 'x' || line[i + 1] == 'X')))
                    {
                        end += 2;
                    }
                    else
                    {
                        break;
                    }
                }
            }
            else if (alWordByte(c))
            {
                while (end < to && alWordByte(line[end]))
                {
                    ++end;
                }
            }
            else if (ALDiffTokens::blank(c))
            {
                while (end < to && ALDiffTokens::blank(line[end]))
                {
                    ++end;
                }
            }
            else
            {
                for (const std::string_view op : OPERATORS)
                {
                    if (op.size() <= to - i && line.compare(i, op.size(), op) == 0)
                    {
                        end = i + op.size();
                        break;
                    }
                }
            }
            out.push_back(Token{ static_cast<S32>(i), static_cast<S32>(end), Region::Code });
            i = end;
        }
    }

    void cutProse(std::string_view line, size_t from, size_t to, Region region, tokens_t& out)
    {
        size_t i = from;
        while (i < to)
        {
            const char c   = line[i];
            size_t     end = i + 1;
            if (alWordByte(c))
            {
                // A word, and an apostrophe inside it: don't, it's.
                while (end < to && (alWordByte(line[end]) || (line[end] == '\'' && end + 1 < to && alWordByte(line[end + 1]))))
                {
                    ++end;
                }
            }
            else if (ALDiffTokens::blank(c))
            {
                while (end < to && ALDiffTokens::blank(line[end]))
                {
                    ++end;
                }
            }
            out.push_back(Token{ static_cast<S32>(i), static_cast<S32>(end), region });
            i = end;
        }
    }
}

void ALDiffTokens::cut(std::string_view line, const ALTextDiff::regions_t* regions, tokens_t& out)
{
    out.clear();
    if (!regions || regions->empty())
    {
        for (const auto& [begin, end] : words(line))
        {
            out.push_back(Token{ begin, end, Region::Code });
        }
        return;
    }
    // Each region as it is cut; what none covers, code.
    size_t at = 0;
    for (const ALTextDiff::Piece& piece : *regions)
    {
        const size_t begin = std::min(static_cast<size_t>(std::max(piece.begin, 0)), line.size());
        const size_t end   = std::min(static_cast<size_t>(std::max(piece.end, 0)), line.size());
        if (begin < at || end <= begin)
        {
            continue;
        }
        if (at < begin)
        {
            cutCode(line, at, begin, out);
        }
        if (piece.region == Region::Code)
        {
            cutCode(line, begin, end, out);
        }
        else
        {
            cutProse(line, begin, end, piece.region, out);
        }
        at = end;
    }
    if (at < line.size())
    {
        cutCode(line, at, line.size(), out);
    }
}

bool ALDiffTokens::isBlank(std::string_view line, const Token& token)
{
    return token.end > token.begin && blank(line[static_cast<size_t>(token.begin)]);
}

bool ALDiffTokens::isWord(std::string_view line, const Token& token)
{
    return token.end > token.begin && alWordByte(line[static_cast<size_t>(token.begin)]);
}

void ALDiffTokens::mark(ALTextDiff::spans_t& spans, std::string_view line, S32 begin, S32 end)
{
    if (begin >= end)
    {
        return;
    }
    if (!spans.empty() && spans.back().second <= begin)
    {
        S32 at = spans.back().second;
        while (at < begin && blank(line[static_cast<size_t>(at)]))
        {
            ++at;
        }
        if (at == begin)
        {
            spans.back().second = std::max(spans.back().second, end);
            return;
        }
    }
    spans.emplace_back(begin, end);
}
