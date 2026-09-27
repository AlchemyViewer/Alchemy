/**
 * @file altextediting.cpp
 * @brief A text view's commands over whole lines: duplicate, move, delete, comment and join them.
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

#include "altextediting.h"

namespace
{
    bool isBlank(char c)
    {
        return c == ' ' || c == '\t';
    }

    // Where a line's text begins, past its blanks.
    S32 indentationOf(const std::string& line)
    {
        S32 n = 0;
        while (n < static_cast<S32>(line.size()) && isBlank(line[n]))
        {
            ++n;
        }
        return n;
    }
}

namespace ALTextEditing
{
std::pair<S32, S32> selectedLines(const ALTextRange& selection)
{
    const ALTextRange range = selection.normalised();
    S32               last  = range.end.line;
    if (last > range.begin.line && range.end.column == 0)
    {
        --last;
    }
    return { range.begin.line, last };
}

Change duplicateLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret)
{
    const auto [first, last] = selectedLines(ALTextRange(anchor, caret));
    const std::string block  = doc.text(ALTextRange(doc.lineStart(first), doc.lineEnd(last)));
    const S32         count  = last - first + 1;
    Change            change;
    change.replacements.push_back({ ALTextRange(doc.lineEnd(last), doc.lineEnd(last)), "\n" + block });
    // The caret and the selection go with the copy.
    change.selects = true;
    change.anchor  = ALTextPos(anchor.line + count, anchor.column);
    change.caret   = ALTextPos(caret.line + count, caret.column);
    return change;
}

std::optional<Change> moveLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, S32 direction)
{
    const auto [first, last] = selectedLines(ALTextRange(anchor, caret));
    if ((direction < 0 && first == 0) || (direction > 0 && last + 1 >= doc.lineCount()))
    {
        return std::nullopt;
    }
    const std::string block = doc.text(ALTextRange(doc.lineStart(first), doc.lineEnd(last)));
    Change            change;
    if (direction < 0)
    {
        const std::string above = doc.line(first - 1);
        change.replacements.push_back({ ALTextRange(doc.lineStart(first - 1), doc.lineEnd(last)), block + "\n" + above });
    }
    else
    {
        const std::string below = doc.line(last + 1);
        change.replacements.push_back({ ALTextRange(doc.lineStart(first), doc.lineEnd(last + 1)), below + "\n" + block });
    }
    change.selects = true;
    change.anchor  = ALTextPos(anchor.line + direction, anchor.column);
    change.caret   = ALTextPos(caret.line + direction, caret.column);
    return change;
}

Change deleteLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret)
{
    const auto [first, last] = selectedLines(ALTextRange(anchor, caret));
    const S32   count        = doc.lineCount();
    ALTextRange range(doc.lineStart(first), last + 1 < count ? doc.lineStart(last + 1) : doc.lineEnd(last));
    // The line the caret lands on, and how long it is, once they are gone.
    S32 line   = first;
    S32 length = last + 1 < count ? doc.lineLength(last + 1) : 0;
    if (last + 1 >= count && first > 0)
    {
        // The last line goes with the newline before it.
        range.begin = doc.lineEnd(first - 1);
        line        = first - 1;
        length      = doc.lineLength(first - 1);
    }
    Change change;
    change.replacements.push_back({ range, std::string() });
    change.caret  = ALTextPos(line, llmin(caret.column, length));
    change.anchor = change.caret;
    return change;
}

std::optional<Change> toggleComment(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, const std::string& token)
{
    const auto [first, last] = selectedLines(ALTextRange(anchor, caret));

    // Out where every line that says anything is commented; in otherwise.
    bool all_commented = true;
    bool any_text      = false;
    for (S32 l = first; l <= last; ++l)
    {
        const std::string& line = doc.line(l);
        const S32          n    = indentationOf(line);
        if (n == static_cast<S32>(line.size()))
        {
            continue;
        }
        any_text = true;
        if (line.compare(n, token.size(), token) != 0)
        {
            all_commented = false;
        }
    }
    if (!any_text)
    {
        return std::nullopt;
    }
    Change change;
    S32    caret_shift = 0;
    // What the last line gained or lost, for where a selection of the
    // lines whole ends.
    S32    last_delta  = 0;
    for (S32 l = first; l <= last; ++l)
    {
        const std::string& line = doc.line(l);
        const S32          n    = indentationOf(line);
        if (n == static_cast<S32>(line.size()))
        {
            continue;
        }
        S32 delta = 0;
        if (all_commented)
        {
            S32 taken = static_cast<S32>(token.size());
            if (n + taken < static_cast<S32>(line.size()) && line[n + taken] == ' ')
            {
                ++taken;
            }
            change.replacements.push_back({ ALTextRange(ALTextPos(l, n), ALTextPos(l, n + taken)), std::string() });
            delta = -taken;
            if (l == caret.line)
            {
                caret_shift = caret.column >= n + taken ? -taken : (caret.column > n ? n - caret.column : 0);
            }
        }
        else
        {
            change.replacements.push_back({ ALTextRange(ALTextPos(l, n), ALTextPos(l, n)), token + " " });
            delta = static_cast<S32>(token.size()) + 1;
            if (l == caret.line && caret.column >= n)
            {
                caret_shift = static_cast<S32>(token.size()) + 1;
            }
        }
        if (l == last)
        {
            last_delta = delta;
        }
    }
    if (anchor != caret)
    {
        change.selects = true;
        change.anchor  = ALTextPos(first, 0);
        change.caret   = last + 1 < doc.lineCount() ? ALTextPos(last + 1, 0) : ALTextPos(last, doc.lineLength(last) + last_delta);
    }
    else
    {
        change.caret  = ALTextPos(caret.line, caret.column + caret_shift);
        change.anchor = change.caret;
    }
    return change;
}

std::optional<Change> joinLines(const ALTextDocument& doc, S32 first, S32 last, bool keep_blanks)
{
    last = llmin(last, doc.lineCount() - 1);
    if (first < 0 || last <= first)
    {
        return std::nullopt;
    }
    // From the bottom up, so each join's place is the text's as it was.
    Change change;
    for (S32 line = last - 1; line >= first; --line)
    {
        if (keep_blanks)
        {
            change.replacements.push_back({ ALTextRange(doc.lineEnd(line), doc.lineStart(line + 1)), std::string() });
            continue;
        }
        const std::string& next  = doc.line(line + 1);
        const size_t       text  = next.find_first_not_of(" \t");
        const bool         blank = text == std::string::npos || next[text] == ')';
        const S32          at    = text == std::string::npos ? static_cast<S32>(next.size()) : static_cast<S32>(text);
        const std::string  space = blank ? std::string() : std::string(" ");
        change.replacements.push_back({ ALTextRange(doc.lineEnd(line), ALTextPos(line + 1, at)), space });
    }
    change.caret = doc.lineEnd(first);
    return change;
}
}
