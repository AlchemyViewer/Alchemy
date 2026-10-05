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

#include <algorithm>
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

std::optional<ALTextPos> backspaceFrom(const ALTextDocument& doc, const ALTextPos& at, const Options& options)
{
    const std::string& text = doc.line(at.line);
    if (at.column < 2 || at.column > static_cast<S32>(text.size()) || text[at.column - 1] != ' ')
    {
        return std::nullopt;
    }
    size_t lead = 0;
    const S32 width = alBlanksWidth(std::string_view(text).substr(0, static_cast<size_t>(at.column)), options.tabWidth, &lead);
    if (lead < static_cast<size_t>(at.column))
    {
        return std::nullopt;
    }
    // Back to the stop before the caret's column, over spaces alone.
    const S32 tab  = llmax(1, options.tabWidth);
    const S32 stop = (width - 1) / tab * tab;
    S32       from = at.column;
    while (from > 0 && text[from - 1] == ' ' && at.column - from < width - stop)
    {
        --from;
    }
    return at.column - from > 1 ? std::optional<ALTextPos>(ALTextPos(at.line, from)) : std::nullopt;
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

namespace
{
    // The text with its trailing blanks gone.
    std::string_view trimmedEnd(std::string_view text)
    {
        while (!text.empty() && isBlank(text.back()))
        {
            text.remove_suffix(1);
        }
        return text;
    }

    // The nearest line above one with anything on it, or -1.
    S32 filledAbove(const ALTextDocument& doc, S32 line)
    {
        S32 above = line - 1;
        while (above >= 0 && doc.line(above).find_first_not_of(" \t\r") == std::string::npos)
        {
            --above;
        }
        return above;
    }

    // The head that sent a line in for its one statement -- `if (a)` over
    // it -- or the outermost of a run of them, each further out than the
    // last; where there is none, nothing.
    std::optional<std::string> onceHead(const ALTextDocument& doc, S32 line, const std::string& indent, const ALSyntaxGrammar* grammar,
                                        S32 tab_width)
    {
        std::optional<std::string> head;
        S32                        width = alBlanksWidth(indent, tab_width);
        for (S32 above = filledAbove(doc, line); above >= 0; above = filledAbove(doc, above))
        {
            const std::string_view text = trimmedEnd(doc.line(above));
            size_t                 lead = 0;
            const S32              own  = alBlanksWidth(text, tab_width, &lead);
            if (own >= width || !grammar->opensOnce(text))
            {
                break;
            }
            head  = std::string(text.substr(0, lead));
            width = own;
        }
        return head;
    }

    // Whether a block opened on a line `width` in is closed below it
    // already: the first line past its body, as far in or less, is as far
    // in and closes a block.
    bool closedBelow(const ALTextDocument& doc, S32 line, S32 width, const ALSyntaxGrammar* grammar, S32 tab_width)
    {
        for (S32 l = line + 1; l < doc.lineCount(); ++l)
        {
            const std::string& text = doc.line(l);
            size_t             lead = 0;
            const S32          own  = alBlanksWidth(text, tab_width, &lead);
            if (text.find_first_not_of(" \t\r", lead) == std::string::npos || own > width)
            {
                continue;
            }
            return own == width && grammar->closesBlock(std::string_view(text).substr(lead)) > 0;
        }
        return false;
    }
}

Split splitLine(const ALTextDocument& doc, const ALTextRange& selection, const ALSyntaxGrammar* grammar, const Options& options, bool in_comment)
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
    // A comment that goes on: the new line begins as its text says.
    if (in_comment && grammar && static_cast<size_t>(blanks) < before.size())
    {
        const std::string& with = grammar->commentContinues(before.substr(static_cast<size_t>(blanks)));
        if (!with.empty())
        {
            split.text += with;
            return split;
        }
    }
    if (rules && grammar->opensBlock(before))
    {
        const std::string inner = indent + indentUnit(indent, options);
        split.text              = "\n" + inner;
        const bool         closer = !after.empty() && !alIdentifierByte(after.front()) && grammar->closesBlock(after) > 0;
        const std::string& end    = grammar->blockEnd(before);
        if (!end.empty() && (after.empty() || closer) && !closedBelow(doc, sel.end.line, alBlanksWidth(indent, options.tabWidth), grammar, options.tabWidth))
        {
            // A block the language closes with a word, not closed below:
            // the word on a line of its own, level with the opening and
            // before any bracket that followed the caret -- `end)` -- and
            // the caret on the line between them.
            split.text += "\n" + indent + end;
            split.caret = ALTextPos(sel.begin.line + 1, static_cast<S32>(inner.size()));
        }
        else if (closer)
        {
            // Between a bracket and the one that closes it: that one on a
            // line of its own, level with the opening, and the caret on the
            // line between them.
            split.text += "\n" + indent;
            split.caret = ALTextPos(sel.begin.line + 1, static_cast<S32>(inner.size()));
        }
    }
    else if (rules && grammar->opensOnce(before) && !grammar->keptLevelWithOnce(after))
    {
        // A head that sends only the next line in: `if (x)` with no brace.
        split.text = "\n" + indent + indentUnit(indent, options);
    }
    else if (rules && static_cast<size_t>(blanks) < before.size())
    {
        // Past the one statement such a head sent in: back out to it.
        if (const std::optional<std::string> head = onceHead(doc, sel.begin.line, indent, grammar, options.tabWidth))
        {
            split.text = "\n" + *head;
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
    // A comment's end typed on a line Return began for it, nothing else
    // written: the blank before it taken away -- ` * /` made ` */`.
    if (content.size() >= 3 && content[content.size() - 2] == ' ')
    {
        const std::string_view begun = content.substr(0, content.size() - 1);
        const std::string      ended = std::string(content.substr(0, content.size() - 2)) + content.back();
        if (grammar->commentContinues(begun) == begun && grammar->endsComment(ended))
        {
            out.replacement = Replacement{ ALTextRange(ALTextPos(line, caret.column - 2), ALTextPos(line, caret.column - 1)), std::string() };
            return out;
        }
    }
    // The brace a head's block goes on with, on a line of its own under a
    // head that sent the line in for one statement: level with the head.
    if (content.size() == 1 && grammar->keptLevelWithOnce(content))
    {
        const S32 above = filledAbove(doc, line);
        if (above >= 0 && grammar->opensOnce(trimmedEnd(doc.line(above))))
        {
            const std::string head = leadingBlanks(doc, above);
            if (alBlanksWidth(head, options.tabWidth) < alBlanksWidth(lead, options.tabWidth))
            {
                out.replacement = Replacement{ blanks, head };
            }
        }
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

// --- pasted lines ---------------------------------------------------------------

std::optional<PastePlan> planPaste(const ALTextDocument& doc, const ALTextRange& selection, std::string_view pasted, const ALSyntaxGrammar* grammar,
                                   const Options& options)
{
    if (!grammar || !grammar->indents() || pasted.find('\n') == std::string_view::npos)
    {
        return std::nullopt;
    }
    const ALTextRange  sel  = selection.normalised();
    const std::string& line = doc.line(sel.begin.line);
    size_t             lead = 0;
    const S32          own  = alBlanksWidth(line, options.tabWidth, &lead);
    if (static_cast<size_t>(sel.begin.column) > lead)
    {
        return std::nullopt;
    }
    const S32 level = llmax(1, options.tabWidth);
    PastePlan plan;
    for (size_t from = 0;;)
    {
        const size_t           nl   = pasted.find('\n', from);
        const std::string_view text = pasted.substr(from, nl == std::string_view::npos ? std::string_view::npos : nl - from);
        size_t                 blanks = 0;
        const S32              width  = alBlanksWidth(text, options.tabWidth, &blanks);
        plan.widths.push_back(text.find_first_not_of(" \t\r", blanks) == std::string_view::npos ? -1 : width);
        if (nl == std::string_view::npos)
        {
            break;
        }
        from = nl + 1;
    }
    const auto first = std::find_if(plan.widths.begin(), plan.widths.end(), [](S32 width) { return width >= 0; });
    if (first == plan.widths.end())
    {
        return std::nullopt;
    }
    // Copied from where a line's text begins: the rest say how far in it was.
    plan.ref = *first;
    if (*first == 0)
    {
        S32 shallowest = -1;
        for (auto it = first + 1; it != plan.widths.end(); ++it)
        {
            if (*it >= 0 && (shallowest < 0 || *it < shallowest))
            {
                shallowest = *it;
            }
        }
        plan.ref = llmax(0, shallowest);
    }
    // What stays on the line after the paste.
    const std::string& end_line = doc.line(sel.end.line);
    const bool rest = end_line.find_first_not_of(" \t\r", static_cast<size_t>(sel.end.column)) != std::string::npos;
    plan.restoreRest = rest && pasted.back() == '\n';
    if (rest || lead < line.size())
    {
        plan.base = own;
        return plan;
    }
    // Nothing else on the line: where the line above says a line goes.
    S32 above = sel.begin.line - 1;
    while (above >= 0 && doc.line(above).find_first_not_of(" \t\r") == std::string::npos)
    {
        --above;
    }
    if (above >= 0)
    {
        std::string_view above_text = doc.line(above);
        while (!above_text.empty() && isBlank(above_text.back()))
        {
            above_text.remove_suffix(1);
        }
        plan.base = alBlanksWidth(above_text, options.tabWidth) + (grammar->opensBlock(above_text) ? level : 0);
    }
    std::string_view head = pasted.substr(pasted.find_first_not_of(" \t\r\n"));
    head = head.substr(0, head.find('\n'));
    if (grammar->closesBlock(head) > 0)
    {
        plan.base = llmax(0, plan.base - level);
    }
    return plan;
}

std::optional<Change> reindentPasted(const ALTextDocument& doc, const ALTextPos& at, const PastePlan& plan, const ALTextPos& caret,
                                     const Options& options)
{
    Change change;
    S32    moved = 0;
    const size_t first = static_cast<size_t>(std::find_if(plan.widths.begin(), plan.widths.end(), [](S32 width) { return width >= 0; }) - plan.widths.begin());
    for (size_t k = 0; k < plan.widths.size(); ++k)
    {
        const S32 line = at.line + static_cast<S32>(k);
        if (line >= doc.lineCount())
        {
            break;
        }
        const bool last = k + 1 == plan.widths.size();
        S32        width;
        if (last && plan.restoreRest)
        {
            width = plan.base;
        }
        else if (plan.widths[k] < 0)
        {
            // A blank line pasted keeps no blanks; the line it went into
            // keeps what it had before the paste, and what followed it
            // there, but blanks alone.
            if (k == 0 || doc.line(line).find_first_not_of(" \t") != std::string::npos)
            {
                continue;
            }
            width = 0;
        }
        else
        {
            width = k == first ? plan.base : llmax(0, plan.base + plan.widths[k] - plan.ref);
        }
        const std::string& text   = doc.line(line);
        size_t             lead   = 0;
        alBlanksWidth(text, options.tabWidth, &lead);
        const std::string wanted = blanksOf(width, options.tabWidth, !options.softTabs);
        if (text.compare(0, lead, wanted) == 0 && wanted.size() == lead)
        {
            continue;
        }
        change.replacements.push_back({ ALTextRange(ALTextPos(line, 0), ALTextPos(line, static_cast<S32>(lead))), wanted });
        if (line == caret.line)
        {
            moved = static_cast<S32>(wanted.size()) - static_cast<S32>(lead);
        }
    }
    if (change.replacements.empty())
    {
        return std::nullopt;
    }
    change.caret = ALTextPos(caret.line, llmax(0, caret.column + moved));
    return change;
}

// --- whole lines ------------------------------------------------------------------

namespace
{
    // What each line first through last gained or lost at its start, by
    // the replacements of a shift of them.
    std::vector<S32> shiftOf(const Change& change, S32 first, S32 last, bool in)
    {
        std::vector<S32> delta(static_cast<size_t>(last - first + 1), 0);
        for (const ALTextEditing::Replacement& replacement : change.replacements)
        {
            delta[static_cast<size_t>(replacement.range.begin.line - first)] = in ? static_cast<S32>(replacement.text.size()) : -replacement.range.end.column;
        }
        return delta;
    }

    // A place where it was, moved by what its line gained or lost, and
    // never before the line's start.
    ALTextPos movedWith(ALTextPos pos, const std::vector<S32>& delta, S32 first)
    {
        if (pos.line >= first && pos.line < first + static_cast<S32>(delta.size()))
        {
            pos.column = llmax(0, pos.column + delta[static_cast<size_t>(pos.line - first)]);
        }
        return pos;
    }
}

Change indentLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, bool in, const Options& options)
{
    const auto [first, last] = ALTextEditing::selectedLines(ALTextRange(anchor, caret));
    Change                 change = shiftLines(doc, first, last, 1, in, options);
    const std::vector<S32> delta  = shiftOf(change, first, last, in);
    change.selects                = true;
    change.anchor                 = movedWith(anchor, delta, first);
    change.caret                  = movedWith(caret, delta, first);
    return change;
}

std::vector<ALTextEditing::Group> indentLines(const ALTextDocument& doc, const std::vector<ALTextRange>& selections, bool in, const Options& options)
{
    std::vector<ALTextEditing::Group> groups;
    for (const ALTextEditing::LineRun& run : ALTextEditing::lineRuns(selections, false))
    {
        Change change = shiftLines(doc, run.first, run.last, 1, in, options);
        if (change.replacements.empty())
        {
            continue;
        }
        const std::vector<S32> delta = shiftOf(change, run.first, run.last, in);
        ALTextEditing::Group   group;
        group.replacements = std::move(change.replacements);
        for (const size_t i : run.selections)
        {
            group.placed.emplace_back(i, ALTextRange(movedWith(selections[i].begin, delta, run.first), movedWith(selections[i].end, delta, run.first)));
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

Change shiftLines(const ALTextDocument& doc, S32 first, S32 last, S32 levels, bool in, const Options& options)
{
    levels          = llmax(1, levels);
    const S32 width = llmax(1, options.tabWidth);
    Change    change;
    for (S32 l = llmax(0, first); l <= last && l < doc.lineCount(); ++l)
    {
        const std::string& line = doc.line(l);
        if (in)
        {
            if (!line.empty())
            {
                const std::string level = options.softTabs ? std::string(width, ' ') : std::string("\t");
                std::string       text;
                for (S32 n = 0; n < levels; ++n)
                {
                    text += level;
                }
                change.replacements.push_back({ ALTextRange(ALTextPos(l, 0), ALTextPos(l, 0)), std::move(text) });
            }
            continue;
        }
        // Out: each level a tab, or up to a tab's width of spaces.
        S32 taken = 0;
        for (S32 n = 0; n < levels; ++n)
        {
            if (taken < static_cast<S32>(line.size()) && line[taken] == '\t')
            {
                ++taken;
                continue;
            }
            S32 spaces = 0;
            while (spaces < width && taken + spaces < static_cast<S32>(line.size()) && line[taken + spaces] == ' ')
            {
                ++spaces;
            }
            if (spaces == 0)
            {
                break;
            }
            taken += spaces;
        }
        if (taken > 0)
        {
            change.replacements.push_back({ ALTextRange(ALTextPos(l, 0), ALTextPos(l, taken)), std::string() });
        }
    }
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
