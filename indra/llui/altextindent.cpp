/**
 * @file altextindent.cpp
 * @brief Where a line's indentation belongs, as it is typed and as whole lines are shifted.
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

#include "altextindent.h"

#include "altextchars.h"
#include "alsyntaxgrammar.h"

#include <array>
#include <cstdlib>

namespace
{
    bool isBlank(char c)
    {
        return c == ' ' || c == '\t';
    }

    // Blanks as wide as asked: tabs, then spaces for what is left; or
    // spaces alone.
    std::string blanksOf(S32 width, S32 tab_width, bool tabs)
    {
        tab_width = llmax(1, tab_width);
        return tabs ? std::string(static_cast<size_t>(width / tab_width), '\t') + std::string(static_cast<size_t>(width % tab_width), ' ')
                    : std::string(static_cast<size_t>(width), ' ');
    }
}

namespace ALTextIndent
{
// --- the text's own ------------------------------------------------------------

std::optional<Options> detect(const ALTextDocument& doc, S32 tab_width, S32 lines)
{
    // How many lines begin with a tab, and with a space; and how often
    // a line steps in or out from the last line with anything on it by
    // each number of spaces.
    S32                  by_tabs   = 0;
    S32                  by_spaces = 0;
    std::array<S32, 9>   steps{};
    S32                  previous  = 0;
    const S32            count     = llmin(doc.lineCount(), llmax(0, lines));
    for (S32 l = 0; l < count; ++l)
    {
        const std::string& text = doc.line(l);
        size_t             lead = 0;
        alBlanksWidth(text, 1, &lead);
        if (lead == text.size() || (text[lead] == '*' && lead > 0 && text[lead - 1] == ' '))
        {
            continue;
        }
        if (text[0] == '\t')
        {
            ++by_tabs;
            previous = -1;
            continue;
        }
        const S32 spaces = text.find_first_not_of(' ') < lead ? -1 : static_cast<S32>(lead);
        if (spaces > 0)
        {
            ++by_spaces;
        }
        if (spaces >= 0 && previous >= 0)
        {
            const S32 step = std::abs(spaces - previous);
            if (step >= 2 && step < static_cast<S32>(steps.size()))
            {
                ++steps[static_cast<size_t>(step)];
            }
        }
        previous = spaces;
    }
    if (by_tabs == by_spaces)
    {
        return std::nullopt;
    }
    Options out;
    out.tabWidth = llmax(1, tab_width);
    out.softTabs = by_spaces > by_tabs;
    if (out.softTabs)
    {
        // The step seen most, four before two before eight before the
        // rest where they are seen as often.
        S32 best = 0;
        for (const S32 step : { 4, 2, 8, 3, 6, 5, 7 })
        {
            if (steps[static_cast<size_t>(step)] > (best ? steps[static_cast<size_t>(best)] : 0))
            {
                best = step;
            }
        }
        if (best)
        {
            out.tabWidth = best;
        }
    }
    return out;
}

// --- as it is typed ------------------------------------------------------------

std::string tabText(const ALTextDocument& doc, const ALTextPos& at, const Options& options)
{
    if (!options.softTabs)
    {
        return "\t";
    }
    const S32 column = doc.displayColumn(at, options.tabWidth);
    return std::string(static_cast<size_t>(alNextTabStop(column, options.tabWidth) - column), ' ');
}

std::string leadingBlanks(const ALTextDocument& doc, S32 line)
{
    const std::string& text = doc.line(line);
    size_t             n    = 0;
    alBlanksWidth(text, 1, &n);
    return text.substr(0, n);
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
    const S32 width = llmax(0, alBlanksWidth(indent, options.tabWidth) - options.tabWidth);
    return blanksOf(width, options.tabWidth, indent.find('\t') != std::string::npos);
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
    if (lead == indent || alBlanksWidth(lead, options.tabWidth) <= alBlanksWidth(indent, options.tabWidth))
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

Change indentLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, bool in, const Options& options)
{
    const auto [first, last] = ALTextEditing::selectedLines(ALTextRange(anchor, caret));
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

std::optional<Change> convertIndentation(const ALTextDocument& doc, S32 first, S32 last, bool to_spaces, S32 tab_width, S32 measured_width)
{
    tab_width               = llmax(1, tab_width);
    const S32 measure_width = measured_width > 0 ? measured_width : tab_width;
    last      = llmin(last, doc.lineCount() - 1);
    Change change;
    for (S32 line = llmax(0, first); line <= last; ++line)
    {
        const std::string& text  = doc.line(line);
        size_t             end   = 0;
        const S32          width = alBlanksWidth(text, measure_width, &end);
        const std::string  again = blanksOf(width, tab_width, !to_spaces);
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
