/**
 * @file alnotecardformat.cpp
 * @brief What a notecard's text is -- plain, JSON or settings -- how a script reads its lines, and a JSON notecard's outline.
 *
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

#include "alnotecardformat.h"

#include <algorithm>
#include <optional>

namespace ALNotecardFormat
{
namespace
{
    bool blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

    std::string_view trimmed(std::string_view line)
    {
        while (!line.empty() && blank(line.front()))
        {
            line.remove_prefix(1);
        }
        while (!line.empty() && blank(line.back()))
        {
            line.remove_suffix(1);
        }
        return line;
    }

    // key = value, or key: value: a key that is not blank and holds no =
    // or :, then one of them.
    bool keyLine(std::string_view line)
    {
        const size_t at = line.find_first_of("=:");
        return at != std::string_view::npos && at > 0 && !trimmed(line.substr(0, at)).empty();
    }
}

std::string guess(std::string_view text)
{
    const std::string_view whole = trimmed(text);
    if (!whole.empty() && (whole.front() == '{' || whole.front() == '['))
    {
        return "json";
    }
    size_t said = 0, keyed = 0;
    size_t from = 0;
    while (from <= text.size())
    {
        size_t end = text.find('\n', from);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        const std::string_view line = trimmed(text.substr(from, end - from));
        from                        = end + 1;
        if (line.empty() || line.front() == '#' || line.front() == ';' || line.starts_with("//"))
        {
            continue;
        }
        ++said;
        if ((line.front() == '[' && line.back() == ']') || keyLine(line))
        {
            ++keyed;
        }
    }
    // Most of what it says, and more than once.
    return keyed >= 2 && keyed * 5 >= said * 3 ? "config" : "text";
}

std::vector<S32> linesPast(std::string_view text, size_t bytes)
{
    std::vector<S32> out;
    S32              line = 0;
    size_t           from = 0;
    while (from <= text.size())
    {
        size_t end = text.find('\n', from);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        if (end - from > bytes)
        {
            out.push_back(line);
        }
        from = end + 1;
        ++line;
    }
    return out;
}

namespace
{
    // What a script reads a notecard with, the name given first.
    constexpr std::string_view READERS[] = { "llGetNotecardLineSync",     "llGetNotecardLine",     "llGetNumberOfNotecardLines",
                                             "ll.GetNotecardLineSync", "ll.GetNotecardLine", "ll.GetNumberOfNotecardLines" };

    bool nameChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.'; }

    // A string opening at a quote: its end past the closing quote, and what
    // it says, its escapes taken out; none where it does not close.
    std::optional<std::pair<size_t, std::string>> stringAt(std::string_view line, size_t open)
    {
        const char  quote = line[open];
        std::string said;
        for (size_t i = open + 1; i < line.size(); ++i)
        {
            if (line[i] == '\\' && i + 1 < line.size())
            {
                said += line[++i];
                continue;
            }
            if (line[i] == quote)
            {
                return std::make_pair(i + 1, said);
            }
            said += line[i];
        }
        return std::nullopt;
    }

    // Whether what comes before a place is a reader's name and its bracket,
    // blanks allowed about the bracket.
    bool readerBefore(std::string_view line, size_t at)
    {
        size_t i = at;
        while (i > 0 && blank(line[i - 1]))
        {
            --i;
        }
        if (i == 0 || line[i - 1] != '(')
        {
            return false;
        }
        --i;
        while (i > 0 && blank(line[i - 1]))
        {
            --i;
        }
        size_t first = i;
        while (first > 0 && nameChar(line[first - 1]))
        {
            --first;
        }
        const std::string_view called = line.substr(first, i - first);
        return std::find(std::begin(READERS), std::end(READERS), called) != std::end(READERS);
    }
}

std::optional<Named> namedAt(std::string_view line, S32 column)
{
    // Each string of the line in turn, until the one that holds the column.
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] != '"' && line[i] != '\'')
        {
            continue;
        }
        const auto string = stringAt(line, i);
        if (!string)
        {
            return std::nullopt;
        }
        const size_t end = string->first;
        if (static_cast<size_t>(column) >= i && static_cast<size_t>(column) <= end)
        {
            if (!readerBefore(line, i))
            {
                return std::nullopt;
            }
            return Named{ string->second, static_cast<S32>(i), static_cast<S32>(end) };
        }
        i = end - 1;
    }
    return std::nullopt;
}

std::vector<ALScriptSpan> readersOf(std::string_view text, std::string_view name)
{
    std::vector<ALScriptSpan> out;
    S32                       line_no = 0;
    size_t                    from    = 0;
    while (from <= text.size())
    {
        size_t end = text.find('\n', from);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        const std::string_view line = text.substr(from, end - from);
        for (size_t i = 0; i < line.size(); ++i)
        {
            if (line[i] != '"' && line[i] != '\'')
            {
                continue;
            }
            const auto string = stringAt(line, i);
            if (!string)
            {
                break;
            }
            if (string->second == name && readerBefore(line, i))
            {
                ALScriptSpan span;
                span.line = span.endLine = line_no;
                span.column              = static_cast<S32>(i);
                span.endColumn           = static_cast<S32>(string->first);
                out.push_back(span);
            }
            i = string->first - 1;
        }
        from = end + 1;
        ++line_no;
    }
    return out;
}

std::vector<ALScriptOutlineEntry> outline(std::string_view text, size_t most)
{
    std::vector<ALScriptOutlineEntry> out;
    // Where the reading is, by line and byte.
    size_t at = 0;
    S32    line = 0, column = 0;
    auto   step = [&]() {
        if (text[at] == '\n')
        {
            ++line;
            column = 0;
        }
        else
        {
            ++column;
        }
        ++at;
    };
    auto skipBlanks = [&]() {
        while (at < text.size() && blank(text[at]))
        {
            step();
        }
    };
    // Each object or array open, with the entry it is the value of, where
    // it is one, and how many values an array has begun.
    struct Open
    {
        bool                  array = false;
        std::optional<size_t> entry;
        S32                   count = 0;
    };
    std::vector<Open> open;
    // A key read and its colon passed, waiting for its value; or a value
    // begun in an array, named by its place.
    struct Pending
    {
        std::string  name;
        ALScriptSpan nameSpan;
    };
    std::optional<Pending> pending;
    auto depth = [&]() { return static_cast<S32>(open.size()) - 1; };
    // A value's entry begun at the reading's place; its end set once the
    // value has been read.
    auto begin = [&](const Pending& key) -> std::optional<size_t> {
        if (out.size() >= most)
        {
            return std::nullopt;
        }
        ALScriptOutlineEntry entry;
        entry.name     = key.name;
        entry.kind     = ALScriptSymbolKind::Field;
        entry.nameSpan = key.nameSpan;
        entry.span     = key.nameSpan;
        entry.depth    = llmax(0, depth());
        out.push_back(std::move(entry));
        return out.size() - 1;
    };
    auto end = [&](size_t index) {
        out[index].span.endLine   = line;
        out[index].span.endColumn = column;
    };

    while (at < text.size() && out.size() < most)
    {
        skipBlanks();
        if (at >= text.size())
        {
            break;
        }
        const char c = text[at];
        // A value in an array: the objects and arrays in it outlined by
        // their places.
        if (!pending && !open.empty() && open.back().array && c != ']' && c != ',')
        {
            const S32 index = open.back().count++;
            if (c == '{' || c == '[')
            {
                Pending element;
                element.name              = "[" + std::to_string(index) + "]";
                element.nameSpan.line     = element.nameSpan.endLine = line;
                element.nameSpan.column   = column;
                element.nameSpan.endColumn = column + 1;
                pending                   = element;
            }
        }
        if (c == '{' || c == '[')
        {
            Open one;
            one.array = c == '[';
            if (pending)
            {
                one.entry = begin(*pending);
                if (one.entry)
                {
                    out[*one.entry].detail = one.array ? "[...]" : "{...}";
                }
                pending.reset();
            }
            open.push_back(one);
            step();
            continue;
        }
        if (c == '}' || c == ']')
        {
            step();
            if (!open.empty())
            {
                if (open.back().entry)
                {
                    end(*open.back().entry);
                }
                open.pop_back();
            }
            pending.reset();
            continue;
        }
        if (c == ',' || c == ':')
        {
            step();
            continue;
        }
        // A string or a scalar: read whole.
        const S32    first_line = line, first_column = column;
        const size_t first      = at;
        if (c == '"')
        {
            step();
            while (at < text.size() && text[at] != '"' && text[at] != '\n')
            {
                if (text[at] == '\\' && at + 1 < text.size())
                {
                    step();
                }
                step();
            }
            if (at < text.size() && text[at] == '"')
            {
                step();
            }
        }
        else
        {
            while (at < text.size() && !blank(text[at]) && text[at] != ',' && text[at] != ':' && text[at] != '}' && text[at] != ']' &&
                   text[at] != '{' && text[at] != '[')
            {
                step();
            }
            if (at == first)
            {
                step();
                continue;
            }
        }
        const std::string_view read = text.substr(first, at - first);
        // In an object with nothing pending, a string followed by a colon
        // is a key.
        if (!pending && !open.empty() && !open.back().array && c == '"')
        {
            skipBlanks();
            if (at < text.size() && text[at] == ':')
            {
                Pending key;
                key.name               = std::string(read.substr(1, read.size() >= 2 && read.back() == '"' ? read.size() - 2 : read.size() - 1));
                key.nameSpan.line      = first_line;
                key.nameSpan.column    = first_column;
                key.nameSpan.endLine   = line;
                key.nameSpan.endColumn = first_column + static_cast<S32>(read.size());
                pending                = key;
            }
            continue;
        }
        // A key's value that holds nothing more.
        if (pending)
        {
            if (const std::optional<size_t> entry = begin(*pending))
            {
                constexpr size_t SHOWN = 40;
                out[*entry].detail     = read.size() > SHOWN ? std::string(read.substr(0, SHOWN)) + "..." : std::string(read);
                end(*entry);
            }
            pending.reset();
        }
    }
    // What was left open ends where the text does.
    for (const Open& one : open)
    {
        if (one.entry)
        {
            end(*one.entry);
        }
    }
    return out;
}
}
