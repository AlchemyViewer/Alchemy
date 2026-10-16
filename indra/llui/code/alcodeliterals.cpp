/**
 * @file alcodeliterals.cpp
 * @brief A text's string and path literals, as its grammar's tokens have them.
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

#include "alcodeliterals.h"

#include "alsaid.h"
#include "alsyntaxhighlighter.h"
#include "llstring.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

namespace
{
    bool isStringKind(ALSyntaxKind kind)
    {
        return kind == ALSyntaxKind::String || kind == ALSyntaxKind::Escape;
    }

    // A string's or a path's: an include's name is a path, a require's a
    // string.
    bool isQuotedKind(ALSyntaxKind kind)
    {
        return isStringKind(kind) || kind == ALSyntaxKind::Path;
    }

    // The run of tokens one after another of the kinds `wanted` takes that
    // a column is in -- or at the end of as well, `at_end` -- as the columns
    // of its line it begins and ends at. False where it is in none.
    bool tokenRunAt(const std::vector<ALSyntaxToken>& tokens, S32 column, bool at_end, bool (*wanted)(ALSyntaxKind), S32& begin, S32& end)
    {
        // The token the column is in, and the one it is at the end of,
        // which comes first where it is one wanted.
        const ALSyntaxToken* in     = alSyntaxTokenAt(tokens, column);
        const ALSyntaxToken* before = at_end && column > 0 ? alSyntaxTokenAt(tokens, column - 1) : nullptr;
        const ALSyntaxToken* found  = before && before != in && wanted(before->kind) ? before : in && wanted(in->kind) ? in : nullptr;
        if (!found)
        {
            return false;
        }
        const size_t at    = static_cast<size_t>(found - tokens.data());
        size_t       first = at;
        size_t       last  = at;
        while (first > 0 && wanted(tokens[first - 1].kind) && tokens[first - 1].end == tokens[first].begin)
        {
            --first;
        }
        while (last + 1 < tokens.size() && wanted(tokens[last + 1].kind) && tokens[last + 1].begin == tokens[last].end)
        {
            ++last;
        }
        begin = tokens[first].begin;
        end   = tokens[last].end;
        return true;
    }

    // What an escape stands for, in bytes: the forms both languages
    // share, Luau's numeric and codepoint ones, and whatever it is
    // written as where we do not know it -- better a number that is the
    // source's than a guess.
    S32 escapedBytes(std::string_view escape)
    {
        if (escape.size() < 2 || escape.front() != '\\')
        {
            return static_cast<S32>(escape.size());
        }
        const char after = escape[1];
        if (after == 'z')
        {
            // Luau's line continuation: it stands for nothing at all.
            return 0;
        }
        if (after == 'u' && escape.size() > 3)
        {
            // `\u{XXXX}`: the codepoint, in the bytes UTF-8 gives it.
            const size_t open = escape.find('{');
            if (open != std::string_view::npos)
            {
                const U32 code = static_cast<U32>(strtoul(std::string(escape.substr(open + 1)).c_str(), nullptr, 16));
                return code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
            }
        }
        // `\n`, `\t`, `\\`, `\"`, `\xHH`, `\ddd`: one byte each.
        return 1;
    }
}

ALCodeLiterals::ALCodeLiterals(const ALTextDocument& document, ALSyntaxHighlighter& highlighter) : mDocument(document), mHighlighter(highlighter) {}

// static
std::string ALCodeLiterals::unescaped(std::string_view written)
{
    std::string reads;
    reads.reserve(written.size());
    for (size_t i = 0; i < written.size(); ++i)
    {
        if (written[i] == '\\')
        {
            if (i + 1 == written.size())
            {
                break;
            }
            const char next = written[i + 1];
            if (next == '\\' || next == '"' || next == '\'')
            {
                reads += next;
                ++i;
                continue;
            }
        }
        reads += written[i];
    }
    return reads;
}

std::optional<ALTextRange> ALCodeLiterals::pathAt(const ALTextPos& pos) const
{
    const std::shared_ptr<const ALSyntaxGrammar> grammar = mHighlighter.grammar();
    char                                         opener  = '\0';
    const std::optional<ALTextRange>             held    = grammar ? quotedAt(pos, &opener) : std::nullopt;
    if (!held || (opener != '"' && opener != '\'' && opener != '`' && opener != '<'))
    {
        return std::nullopt;
    }
    // A string the grammar says names a file, by what comes before it.
    if (!grammar->pathString(std::string_view(mDocument.line(held->begin.line)).substr(0, held->begin.column - 1)))
    {
        return std::nullopt;
    }
    return held;
}

std::optional<ALTextRange> ALCodeLiterals::quotedAt(const ALTextPos& pos, char* opener, bool* closed) const
{
    if (pos.line < 0 || pos.line >= mDocument.lineCount())
    {
        return std::nullopt;
    }
    // The run of string and path tokens the position is in, or at the end
    // of.
    S32 begin = 0;
    S32 end   = 0;
    if (!tokenRunAt(mHighlighter.tokens(pos.line), pos.column, /*at_end*/ true, isQuotedKind, begin, end))
    {
        return std::nullopt;
    }
    const std::string& line = mDocument.line(pos.line);
    end                     = std::min(end, static_cast<S32>(line.size()));
    const char         open = begin < end ? line[begin] : '\0';
    if (open != '"' && open != '\'' && open != '`' && open != '<')
    {
        return std::nullopt;
    }
    // Between the quotes; to the line's end where the string is not
    // closed -- its last byte no quote, or a quote a backslash escapes,
    // `"Say \"` typed so far.
    const char close   = open == '<' ? '>' : open;
    S32        escapes = 0;
    while (end - 2 - escapes > begin && line[end - 2 - escapes] == '\\')
    {
        ++escapes;
    }
    const bool shut = end - 1 > begin && line[end - 1] == close && escapes % 2 == 0;
    const S32  held = shut ? end - 1 : end;
    if (pos.column <= begin || pos.column > held)
    {
        return std::nullopt;
    }
    if (opener)
    {
        *opener = open;
    }
    if (closed)
    {
        *closed = shut;
    }
    return ALTextRange(ALTextPos(pos.line, begin + 1), ALTextPos(pos.line, held));
}

ALTextRange ALCodeLiterals::stringAt(const ALTextPos& at) const
{
    const ALTextDocument& doc = mDocument;
    const ALTextPos       pos = doc.clamp(at);

    auto runOn = [this](S32 line, S32 column, S32& begin, S32& end) {
        // The run of string tokens around a column, or nothing.
        return tokenRunAt(mHighlighter.tokens(line), column, /*at_end*/ false, isStringKind, begin, end);
    };

    S32 begin = 0, end = 0;
    if (!runOn(pos.line, pos.column, begin, end))
    {
        return ALTextRange();
    }
    ALTextPos from(pos.line, begin);
    ALTextPos to(pos.line, end);
    // A string that carries over the line's end -- Lua's long brackets,
    // or a line joined with a backslash -- is one literal.
    while (from.column == 0 && from.line > 0)
    {
        const S32 above = from.line - 1;
        S32       b = 0, e = 0;
        const S32 last = llmax(0, static_cast<S32>(doc.line(above).size()) - 1);
        if (!runOn(above, last, b, e) || e < static_cast<S32>(doc.line(above).size()))
        {
            break;
        }
        from = ALTextPos(above, b);
    }
    while (to.column >= static_cast<S32>(doc.line(to.line).size()) && to.line + 1 < doc.lineCount())
    {
        const S32 below = to.line + 1;
        S32       b = 0, e = 0;
        if (!runOn(below, 0, b, e) || b != 0)
        {
            break;
        }
        to = ALTextPos(below, e);
    }
    return ALTextRange(from, to);
}

std::string ALCodeLiterals::stringSize(const ALTextRange& literal) const
{
    if (literal.empty())
    {
        return std::string();
    }
    const std::string written = mDocument.text(literal);

    // What it holds: the bytes between the delimiters, with each escape
    // counted as what it stands for rather than as what it is written
    // as. The grammar has already said which stretches are escapes.
    S32 bytes = 0, characters = 0, escapes = 0;
    for (S32 line = literal.begin.line; line <= literal.end.line; ++line)
    {
        const std::string&                text   = mDocument.line(line);
        const std::vector<ALSyntaxToken>& tokens = mHighlighter.tokens(line);
        const S32                         from   = line == literal.begin.line ? literal.begin.column : 0;
        const S32                         to     = line == literal.end.line ? literal.end.column : static_cast<S32>(text.size());
        size_t                            t      = 0;
        for (S32 i = from; i < to && i < static_cast<S32>(text.size());)
        {
            while (t < tokens.size() && tokens[t].end <= i)
            {
                ++t;
            }
            if (t < tokens.size() && tokens[t].kind == ALSyntaxKind::Escape && tokens[t].begin <= i)
            {
                const S32 stop = llmin(tokens[t].end, to);
                const S32 was  = escapedBytes(std::string_view(text).substr(i, stop - i));
                bytes += was;
                characters += was > 0 ? 1 : 0;
                ++escapes;
                i = stop;
                continue;
            }
            ++bytes;
            // A byte that is not a continuation byte begins a character.
            characters += (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80 ? 1 : 0;
            ++i;
        }
        if (line < literal.end.line)
        {
            // The break itself, which the literal holds.
            bytes += 1;
            characters += 1;
        }
    }
    // The delimiters are not what the string holds: a quote at each end,
    // or Lua's brackets, which the run's own text says.
    S32 marks = 0;
    if (written.size() >= 2 && (written.front() == '"' || written.front() == '\'' || written.front() == '`') && written.back() == written.front())
    {
        marks = 2;
    }
    else if (written.size() >= 4 && written.compare(0, 2, "[[") == 0)
    {
        marks = 4;
    }
    else if (written.size() >= 6 && written.compare(0, 2, "[=") == 0)
    {
        const size_t open = written.find('[', 1);
        marks             = open != std::string::npos ? static_cast<S32>(2 * (open + 1)) : 0;
    }
    bytes      = llmax(0, bytes - marks);
    characters = llmax(0, characters - marks);

    std::string says = alSaidCount("CodeStringBytes", bytes, "[COUNT] byte", "[COUNT] bytes");
    if (characters != bytes)
    {
        says += ", " + alSaidCount("CodeStringCharacters", characters, "[COUNT] character", "[COUNT] characters");
    }
    if (escapes > 0)
    {
        LLStringUtil::format_map_t args;
        args["[COUNT]"] = std::to_string(static_cast<S32>(written.size()));
        says += ", " + alSaid("CodeStringWritten", "[COUNT] in source", args);
    }
    return says;
}
