/**
 * @file altextediting.cpp
 * @brief A text view's line commands and indentation: what a command does to whole lines, and where a line belongs.
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

#include "altextchars.h"
#include "alsyntaxgrammar.h"

namespace
{
    bool isBlank(char c)
    {
        return c == ' ' || c == '\t';
    }

    // How wide a run of blanks is drawn, tabs to their stops.
    S32 blanksWidth(std::string_view blanks, S32 tab_width)
    {
        S32 width = 0;
        for (const char c : blanks)
        {
            width = c == '\t' ? (width / tab_width + 1) * tab_width : width + 1;
        }
        return width;
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
// --- indentation ------------------------------------------------------------------

std::string tabText(const ALTextDocument& doc, const ALTextPos& at, const Options& options)
{
    if (!options.softTabs)
    {
        return "\t";
    }
    const S32 column = doc.displayColumn(at, options.tabWidth);
    return std::string(options.tabWidth - (column % options.tabWidth), ' ');
}

std::string leadingBlanks(const ALTextDocument& doc, S32 line)
{
    const std::string& text = doc.line(line);
    return text.substr(0, indentationOf(text));
}

std::string indentUnit(const std::string& like, const Options& options)
{
    // In the blank the indentation around it is written in, else as tabs
    // are typed here.
    const bool tabs = like.empty() ? !options.softTabs : like.front() == '\t';
    return tabs ? std::string("\t") : std::string(options.tabWidth, ' ');
}

std::string outdented(const std::string& indent, const Options& options)
{
    const S32 width = llmax(0, blanksWidth(indent, options.tabWidth) - options.tabWidth);
    if (indent.find('\t') != std::string::npos)
    {
        return std::string(width / options.tabWidth, '\t') + std::string(width % options.tabWidth, ' ');
    }
    return std::string(width, ' ');
}

std::string closingIndent(const ALTextDocument& doc, S32 line, const ALSyntaxGrammar* grammar, const opener_t& opener, const Options& options)
{
    const std::string  lead = leadingBlanks(doc, line);
    const std::string& text = doc.line(line);
    if (lead.size() < text.size() && !alIdentifierByte(text[lead.size()]))
    {
        ALTextPos opened;
        if (opener && opener(ALTextPos(line, static_cast<S32>(lead.size())), opened))
        {
            return leadingBlanks(doc, opened.line);
        }
    }
    S32 above = line - 1;
    while (above >= 0 && leadingBlanks(doc, above).size() == doc.line(above).size())
    {
        --above;
    }
    if (above < 0)
    {
        return std::string();
    }
    const std::string above_lead = leadingBlanks(doc, above);
    std::string_view  above_text = doc.line(above);
    while (!above_text.empty() && isBlank(above_text.back()))
    {
        above_text.remove_suffix(1);
    }
    return grammar && grammar->opensBlock(above_text) ? above_lead : outdented(above_lead, options);
}

std::optional<Replacement> reindent(const ALTextDocument& doc, S32 line, const std::string& indent, const Options& options)
{
    const std::string lead = leadingBlanks(doc, line);
    // Only ever out: a line put further out by hand stays where it was put.
    if (lead == indent || blanksWidth(lead, options.tabWidth) <= blanksWidth(indent, options.tabWidth))
    {
        return std::nullopt;
    }
    return Replacement{ ALTextRange(ALTextPos(line, 0), ALTextPos(line, static_cast<S32>(lead.size()))), indent };
}

std::optional<Replacement> closingBeforeReturn(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret,
                                               const ALSyntaxGrammar* grammar, const opener_t& opener, const Options& options)
{
    if (!grammar || !grammar->indents() || anchor != caret)
    {
        return std::nullopt;
    }
    const std::string& text = doc.line(caret.line);
    const size_t       lead = leadingBlanks(doc, caret.line).size();
    if (static_cast<size_t>(caret.column) <= lead)
    {
        return std::nullopt;
    }
    const std::string_view content(text.data() + lead, caret.column - lead);
    if (!alIdentifierByte(content.front()) || grammar->closesBlock(content) != content.size())
    {
        return std::nullopt;
    }
    return reindent(doc, caret.line, closingIndent(doc, caret.line, grammar, opener, options), options);
}

Split splitLine(const ALTextDocument& doc, const ALTextRange& selection, const ALSyntaxGrammar* grammar, const Options& options)
{
    const bool rules = grammar && grammar->indents();
    // The new line starts with the indentation of the one it leaves, a
    // level further in under what opens a block.
    const ALTextRange  sel    = selection.normalised();
    const std::string& line   = doc.line(sel.begin.line);
    S32                blanks = 0;
    while (blanks < sel.begin.column && blanks < static_cast<S32>(line.size()) && isBlank(line[blanks]))
    {
        ++blanks;
    }
    const std::string indent = line.substr(0, blanks);
    std::string_view  before(line.data(), sel.begin.column);
    while (!before.empty() && isBlank(before.back()))
    {
        before.remove_suffix(1);
    }
    // What goes down with the caret, without the blanks it began with,
    // which the new indentation stands in for.
    const std::string& end_line = doc.line(sel.end.line);
    S32                skipped  = sel.end.column;
    while (skipped < static_cast<S32>(end_line.size()) && isBlank(end_line[skipped]))
    {
        ++skipped;
    }
    const std::string_view after(end_line.data() + skipped, end_line.size() - skipped);

    Split split;
    split.range = ALTextRange(sel.begin, ALTextPos(sel.end.line, skipped));
    split.text  = "\n" + indent;
    if (rules && grammar->opensBlock(before))
    {
        const std::string inner = indent + indentUnit(indent, options);
        split.text              = "\n" + inner;
        if (!after.empty() && !alIdentifierByte(after.front()) && grammar->closesBlock(after) > 0)
        {
            // Between a bracket and the one that closes it: that one on a
            // line of its own, level with the opening, and the caret on the
            // line between them.
            split.text += "\n" + indent;
            split.caret = ALTextPos(sel.begin.line + 1, static_cast<S32>(inner.size()));
        }
    }
    if (before.empty() && after.empty())
    {
        // A line of nothing but blanks keeps none of them.
        split.range.begin.column = 0;
    }
    return split;
}

Outdent outdentAsTyped(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, llwchar typed, const ALSyntaxGrammar* grammar,
                       const opener_t& opener, const AutoOutdent& last, const Options& options)
{
    Outdent out;
    if (!grammar || !grammar->indents() || anchor != caret)
    {
        return out;
    }
    const S32          line = caret.line;
    const std::string& text = doc.line(line);
    const std::string  lead = leadingBlanks(doc, line);
    if (static_cast<size_t>(caret.column) <= lead.size())
    {
        return out;
    }
    const std::string_view content(text.data() + lead.size(), caret.column - lead.size());
    const size_t           closes     = grammar->closesBlock(content);
    const bool             typed_word = typed < 0x80 && alIdentifierByte(static_cast<char>(typed));
    const ALTextRange      blanks(ALTextPos(line, 0), ALTextPos(line, static_cast<S32>(lead.size())));
    if (typed_word && last.line == line && last.column + 1 == caret.column && closes != content.size())
    {
        // The word went on past a closing one -- `endpoint` -- and is a
        // name: back where it was typed.
        out.replacement = Replacement{ blanks, last.indent };
        return out;
    }
    // The first thing on the line, just finished: a bracket as it is
    // typed, a word with nothing after it on the line.
    if (closes == 0 || closes != content.size())
    {
        return out;
    }
    const bool word = alIdentifierByte(content.front());
    if (word && text.find_first_not_of(" \t", caret.column) != std::string::npos)
    {
        return out;
    }
    out.replacement = reindent(doc, line, closingIndent(doc, line, grammar, opener, options), options);
    if (out.replacement && word)
    {
        // The caret as the line's coming out leaves it.
        out.next.line   = line;
        out.next.column = caret.column + static_cast<S32>(out.replacement->text.size()) - static_cast<S32>(lead.size());
        out.next.indent = lead;
    }
    return out;
}

// --- whole lines ------------------------------------------------------------------

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

Change indentLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, bool in, const Options& options)
{
    const auto [first, last] = selectedLines(ALTextRange(anchor, caret));
    Change change;
    // What each line gained or lost at its start.
    std::vector<S32> delta(static_cast<size_t>(last - first + 1), 0);
    for (S32 l = first; l <= last; ++l)
    {
        const std::string& line = doc.line(l);
        if (in)
        {
            if (!line.empty())
            {
                const std::string tab = options.softTabs ? std::string(options.tabWidth, ' ') : std::string("\t");
                change.replacements.push_back({ ALTextRange(ALTextPos(l, 0), ALTextPos(l, 0)), tab });
                delta[l - first] = static_cast<S32>(tab.size());
            }
        }
        else
        {
            S32 taken = 0;
            if (!line.empty() && line[0] == '\t')
            {
                taken = 1;
            }
            else
            {
                while (taken < options.tabWidth && taken < static_cast<S32>(line.size()) && line[taken] == ' ')
                {
                    ++taken;
                }
            }
            if (taken)
            {
                change.replacements.push_back({ ALTextRange(ALTextPos(l, 0), ALTextPos(l, taken)), std::string() });
                delta[l - first] = -taken;
            }
        }
    }
    // The caret and the anchor stay where they were, moved by what their
    // lines gained or lost, and never before a line's start.
    const auto moved = [&](ALTextPos pos) {
        if (pos.line >= first && pos.line <= last)
        {
            pos.column = llmax(0, pos.column + delta[pos.line - first]);
        }
        return pos;
    };
    change.selects = true;
    change.anchor  = moved(anchor);
    change.caret   = moved(caret);
    return change;
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

std::optional<Change> convertIndentation(const ALTextDocument& doc, S32 first, S32 last, bool to_spaces, S32 tab_width, S32 measured_width)
{
    tab_width               = llmax(1, tab_width);
    const S32 measure_width = measured_width > 0 ? measured_width : tab_width;
    last      = llmin(last, doc.lineCount() - 1);
    Change change;
    for (S32 line = llmax(0, first); line <= last; ++line)
    {
        // How wide the blanks are, each tab to its next stop.
        const std::string& text  = doc.line(line);
        S32                width = 0;
        size_t             end   = 0;
        for (; end < text.size() && (text[end] == ' ' || text[end] == '\t'); ++end)
        {
            width = text[end] == '\t' ? (width / measure_width + 1) * measure_width : width + 1;
        }
        const std::string again = to_spaces ? std::string(static_cast<size_t>(width), ' ')
                                            : std::string(static_cast<size_t>(width / tab_width), '\t') +
                                                  std::string(static_cast<size_t>(width % tab_width), ' ');
        if (again != text.substr(0, end))
        {
            change.replacements.push_back({ ALTextRange(ALTextPos(line, 0), ALTextPos(line, static_cast<S32>(end))), again });
        }
    }
    if (change.replacements.empty())
    {
        return std::nullopt;
    }
    change.caret = ALTextPos(llmax(0, first), 0);
    return change;
}
}
