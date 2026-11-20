/**
 * @file allslcomments.cpp
 * @brief The comments of an LSL script read from its source alone, and those that may hold code.
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

#include "allslcomments.h"

#include <algorithm>

namespace
{
    bool blank(std::string_view text)
    {
        return text.find_first_not_of(" \t\r") == std::string_view::npos;
    }

    // Whether every string the text opens it closes, as LSL reads one: to its
    // next quote, past backslashed bytes.
    bool stringsClosed(std::string_view text)
    {
        bool in = false;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (in && text[i] == '\\')
            {
                ++i;
            }
            else if (text[i] == '"')
            {
                in = !in;
            }
        }
        return !in;
    }

    // Whether a line comment's text may be part of a run of code: not a
    // rule, a heading of slashes or stars, nor what would run past its line.
    bool runnable(std::string_view source, const ALLSLComments::Comment& c)
    {
        if (c.kind != ALLSLComments::Comment::Kind::Line || c.opener || c.closer)
        {
            return false;
        }
        const std::string_view text  = ALLSLComments::body(source, c);
        const size_t           first = text.find_first_not_of(" \t");
        return (first == std::string_view::npos || (text[first] != '/' && text[first] != '*')) && text.find("/*") == std::string_view::npos &&
               text.find("*/") == std::string_view::npos && stringsClosed(text);
    }
}

// static
std::vector<ALLSLComments::Comment> ALLSLComments::scan(std::string_view src)
{
    std::vector<Comment> out;
    S32                  line   = 1;
    S32                  column = 1;
    const auto           step   = [&](char c) {
        if (c == '\n')
        {
            ++line;
            column = 1;
        }
        else
        {
            ++column;
        }
    };
    // The openers since the last closer.
    std::vector<size_t> pending;
    for (size_t i = 0; i < src.size();)
    {
        const char c = src[i];
        if (c == '"')
        {
            step(src[i++]);
            while (i < src.size() && src[i] != '"')
            {
                if (src[i] == '\\' && i + 1 < src.size())
                {
                    step(src[i++]);
                }
                step(src[i++]);
            }
            if (i < src.size())
            {
                step(src[i++]);
            }
            continue;
        }
        if (c != '/' || i + 1 >= src.size() || (src[i + 1] != '/' && src[i + 1] != '*'))
        {
            step(src[i++]);
            continue;
        }
        Comment k;
        k.begin  = i;
        k.line   = line;
        k.column = column;
        // A closer is one whatever is before it, and so is a block comment
        // that ends in one, the toggle turned off; an opener is one where
        // something after it closes it, and a comment that begins with a
        // star where nothing does.
        bool closes = false;
        if (src[i + 1] == '/')
        {
            k.kind = Comment::Kind::Line;
            k.end  = std::min(src.find('\n', i), src.size());
            std::string_view text = src.substr(i + 2, k.end - i - 2);
            while (!text.empty() && (text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
            {
                text.remove_suffix(1);
            }
            if (text.substr(0, 2) == "*/")
            {
                k.closer = true;
                closes   = true;
            }
            else if (!text.empty() && text[0] == '*' && (text.size() == 1 || (text[1] != '*' && text[1] != '/')) &&
                     text.find("*/") == std::string_view::npos)
            {
                // A word after the star or not, but no rule of stars, nor a
                // block comment closed on its line once a / is out.
                pending.push_back(out.size());
            }
        }
        else
        {
            k.kind              = Comment::Kind::Block;
            const size_t close  = src.find("*/", i + 2);
            k.closed            = close != std::string_view::npos;
            k.end               = k.closed ? close + 2 : src.size();
            const std::string_view text = src.substr(i + 2, (k.closed ? close : src.size()) - i - 2);
            k.toggled           = text.size() >= 2 && text.substr(text.size() - 2) == "//" && text.find('\n') != std::string_view::npos;
            closes              = k.toggled;
        }
        while (i < k.end)
        {
            step(src[i++]);
        }
        k.endLine = line;
        if (closes)
        {
            for (size_t opener : pending)
            {
                out[opener].opener   = true;
                out[opener].closedBy = out.size();
            }
            pending.clear();
        }
        const size_t starts = k.begin == 0 ? 0 : src.rfind('\n', k.begin - 1) + 1;
        const size_t ends   = std::min(src.find('\n', k.end), src.size());
        k.alone             = blank(src.substr(starts, k.begin - starts)) && blank(src.substr(k.end, ends - k.end));
        out.push_back(k);
    }
    return out;
}

// static
std::string_view ALLSLComments::body(std::string_view source, const Comment& comment)
{
    if (comment.kind == Comment::Kind::Line)
    {
        return source.substr(comment.begin + 2, comment.end - comment.begin - 2);
    }
    const size_t     inner = comment.end - comment.begin - 2 - (comment.closed ? 2 : 0);
    std::string_view text  = source.substr(comment.begin + 2, inner);
    if (comment.toggled)
    {
        text.remove_suffix(2);
    }
    return text;
}

// static
bool ALLSLComments::mayBeCode(std::string_view text)
{
    return text.find_first_of(";{}(") != std::string_view::npos;
}

// static
std::vector<ALLSLComments::Unit> ALLSLComments::units(std::string_view source, const std::vector<Comment>& comments)
{
    std::vector<Unit> out;
    for (size_t k = 0; k < comments.size(); ++k)
    {
        const Comment& c = comments[k];
        Unit           unit;
        unit.first   = k;
        unit.last    = k;
        unit.begin   = c.begin;
        unit.end     = c.end;
        unit.line    = c.line;
        unit.column  = c.column;
        unit.endLine = c.endLine;
        if (c.kind == Comment::Kind::Line)
        {
            if (!runnable(source, c))
            {
                continue;
            }
            if (c.alone)
            {
                // Those under it, each alone on the next line.
                bool code = mayBeCode(body(source, c));
                while (unit.last + 1 < comments.size())
                {
                    const Comment& next = comments[unit.last + 1];
                    if (!next.alone || next.line != comments[unit.last].endLine + 1 || !runnable(source, next))
                    {
                        break;
                    }
                    ++unit.last;
                    code |= mayBeCode(body(source, next));
                }
                k = unit.last;
                if (!code)
                {
                    continue;
                }
                unit.shape   = Unit::Shape::Lines;
                unit.end     = comments[unit.last].end;
                unit.endLine = comments[unit.last].endLine;
                out.push_back(unit);
                continue;
            }
            // After code: not after another comment on its line, which is
            // no statement's.
            if ((k > 0 && comments[k - 1].endLine == c.line) || !mayBeCode(body(source, c)))
            {
                continue;
            }
            unit.shape = Unit::Shape::Trailing;
            out.push_back(unit);
            continue;
        }
        const std::string_view text  = body(source, c);
        const size_t           first = text.find_first_not_of(" \t\r\n");
        if (!c.closed || !c.alone || first == std::string_view::npos || text[first] == '*' || text[first] == '/' ||
            text.find("/*") != std::string_view::npos || !stringsClosed(text) || !mayBeCode(text))
        {
            continue;
        }
        unit.shape = c.toggled ? Unit::Shape::Toggle : Unit::Shape::Block;
        out.push_back(unit);
    }
    return out;
}

// static
std::vector<ALLSLComments::Unit> ALLSLComments::runOf(const Unit& run, std::string_view source, const std::vector<Comment>& comments, bool lines)
{
    std::vector<Unit> out;
    if (run.shape != Unit::Shape::Lines)
    {
        return out;
    }
    for (size_t k = run.first; k <= run.last; ++k)
    {
        if (!mayBeCode(body(source, comments[k])))
        {
            continue;
        }
        size_t last = k;
        while (!lines && last < run.last && mayBeCode(body(source, comments[last + 1])))
        {
            ++last;
        }
        Unit unit    = run;
        unit.first   = k;
        unit.last    = last;
        unit.begin   = comments[k].begin;
        unit.end     = comments[last].end;
        unit.line    = comments[k].line;
        unit.column  = comments[k].column;
        unit.endLine = comments[last].endLine;
        out.push_back(unit);
        k = last;
    }
    return out;
}

// static
void ALLSLComments::uncomment(std::string& source, const std::vector<Comment>& comments, const Unit& unit)
{
    for (size_t k = unit.first; k <= unit.last && k < comments.size(); ++k)
    {
        const Comment& c = comments[k];
        source[c.begin]     = ' ';
        source[c.begin + 1] = ' ';
        if (c.kind == Comment::Kind::Block && c.closed)
        {
            const size_t marks = c.toggled ? 4 : 2;
            for (size_t i = c.end - marks; i < c.end; ++i)
            {
                source[i] = ' ';
            }
        }
    }
}

// static
std::string ALLSLComments::uncommented(std::string_view source, const std::vector<Comment>& comments, const Unit& unit)
{
    std::string out(source);
    uncomment(out, comments, unit);
    return out;
}

// static
char ALLSLComments::freeByte(std::string_view source, char after)
{
    for (char c = static_cast<char>(after + 1); c > 0 && c < ' '; ++c)
    {
        if (c != '\t' && c != '\n' && c != '\r' && source.find(c) == std::string_view::npos)
        {
            return c;
        }
    }
    return 0;
}

// static
std::vector<char> ALLSLComments::freeBytes(std::string_view source, size_t count, char but)
{
    std::vector<char> out;
    for (char c = ' ' - 1; c > 0 && out.size() < count; --c)
    {
        if (c != '\t' && c != '\n' && c != '\r' && c != but && source.find(c) == std::string_view::npos)
        {
            out.push_back(c);
        }
    }
    return out;
}
