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

#include "altextview.h"

#include "altextchars.h"

std::string ALTextView::tabText(const ALTextPos& at) const
{
    if (!mSoftTabs)
    {
        return "\t";
    }
    const S32 column = mDocument.displayColumn(at, mTabWidth);
    return std::string(mTabWidth - (column % mTabWidth), ' ');
}

// --- indentation -------------------------------------------------------------

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
}

std::string ALTextView::leadingBlanks(S32 line) const
{
    const std::string& text = mDocument.line(line);
    size_t             n    = 0;
    while (n < text.size() && isBlank(text[n]))
    {
        ++n;
    }
    return text.substr(0, n);
}

std::string ALTextView::indentUnit(const std::string& like) const
{
    // In the blank the indentation around it is written in, else as tabs
    // are typed here.
    const bool tabs = like.empty() ? !mSoftTabs : like.front() == '\t';
    return tabs ? std::string("\t") : std::string(mTabWidth, ' ');
}

std::string ALTextView::outdented(const std::string& indent) const
{
    const S32 width = llmax(0, blanksWidth(indent, mTabWidth) - mTabWidth);
    if (indent.find('\t') != std::string::npos)
    {
        return std::string(width / mTabWidth, '\t') + std::string(width % mTabWidth, ' ');
    }
    return std::string(width, ' ');
}

std::string ALTextView::closingIndent(S32 line)
{
    const std::string  lead = leadingBlanks(line);
    const std::string& text = mDocument.line(line);
    if (lead.size() < text.size() && !alIdentifierByte(text[lead.size()]))
    {
        ALTextPos opener;
        if (closerOpenedAt(ALTextPos(line, static_cast<S32>(lead.size())), opener))
        {
            return leadingBlanks(opener.line);
        }
    }
    S32 above = line - 1;
    while (above >= 0 && leadingBlanks(above).size() == mDocument.line(above).size())
    {
        --above;
    }
    if (above < 0)
    {
        return std::string();
    }
    const std::string above_lead = leadingBlanks(above);
    std::string_view  above_text = mDocument.line(above);
    while (!above_text.empty() && isBlank(above_text.back()))
    {
        above_text.remove_suffix(1);
    }
    const ALSyntaxGrammar* grammar = mHighlighter.grammar().get();
    return grammar && grammar->opensBlock(above_text) ? above_lead : outdented(above_lead);
}

bool ALTextView::reindentLine(S32 line, const std::string& indent)
{
    const std::string lead = leadingBlanks(line);
    // Only ever out: a line put further out by hand stays where it was put.
    if (lead == indent || blanksWidth(lead, mTabWidth) <= blanksWidth(indent, mTabWidth))
    {
        return false;
    }
    return replaceAll({ { ALTextRange(ALTextPos(line, 0), ALTextPos(line, static_cast<S32>(lead.size()))), indent } });
}

void ALTextView::outdentAsTyped(llwchar typed)
{
    const AutoOutdent last = mAutoOutdent;
    mAutoOutdent           = AutoOutdent();
    const ALSyntaxGrammar* grammar = mHighlighter.grammar().get();
    if (!grammar || !grammar->indents() || hasSelection())
    {
        return;
    }
    const S32          line = mCaret.line;
    const std::string& text = mDocument.line(line);
    const std::string  lead = leadingBlanks(line);
    if (static_cast<size_t>(mCaret.column) <= lead.size())
    {
        return;
    }
    const std::string_view content(text.data() + lead.size(), mCaret.column - lead.size());
    const size_t           closes     = grammar->closesBlock(content);
    const bool             typed_word = typed < 0x80 && alIdentifierByte(static_cast<char>(typed));
    if (typed_word && last.line == line && last.column + 1 == mCaret.column && closes != content.size())
    {
        // The word went on past a closing one -- `endpoint` -- and is a
        // name: back where it was typed.
        replaceAll({ { ALTextRange(ALTextPos(line, 0), ALTextPos(line, static_cast<S32>(lead.size()))), last.indent } });
        return;
    }
    // The first thing on the line, just finished: a bracket as it is
    // typed, a word with nothing after it on the line.
    if (closes == 0 || closes != content.size())
    {
        return;
    }
    const bool word = alIdentifierByte(content.front());
    if (word && text.find_first_not_of(" \t", mCaret.column) != std::string::npos)
    {
        return;
    }
    if (reindentLine(line, closingIndent(line)) && word)
    {
        mAutoOutdent.line   = line;
        mAutoOutdent.column = mCaret.column;
        mAutoOutdent.indent = lead;
    }
}

void ALTextView::newLine()
{
    const ALSyntaxGrammar* grammar = mHighlighter.grammar().get();
    const bool             rules   = grammar && grammar->indents();
    mUndo.beginGroup();
    if (rules && !hasSelection())
    {
        // A closing word the line is, finished by the Return rather than
        // by a character after it: out first, as the character would have
        // brought it.
        const std::string& text = mDocument.line(mCaret.line);
        const size_t       lead = leadingBlanks(mCaret.line).size();
        if (static_cast<size_t>(mCaret.column) > lead)
        {
            const std::string_view content(text.data() + lead, mCaret.column - lead);
            if (alIdentifierByte(content.front()) && grammar->closesBlock(content) == content.size())
            {
                reindentLine(mCaret.line, closingIndent(mCaret.line));
            }
        }
    }

    // The new line starts with the indentation of the one it leaves, a
    // level further in under what opens a block.
    const ALTextRange  sel    = selection().normalised();
    const std::string& line   = mDocument.line(sel.begin.line);
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
    const std::string& end_line = mDocument.line(sel.end.line);
    S32                skipped  = sel.end.column;
    while (skipped < static_cast<S32>(end_line.size()) && isBlank(end_line[skipped]))
    {
        ++skipped;
    }
    const std::string_view after(end_line.data() + skipped, end_line.size() - skipped);

    ALTextRange range(sel.begin, ALTextPos(sel.end.line, skipped));
    std::string text  = "\n" + indent;
    ALTextPos   caret(-1, -1);
    if (rules && grammar->opensBlock(before))
    {
        const std::string inner = indent + indentUnit(indent);
        text                    = "\n" + inner;
        if (!after.empty() && !alIdentifierByte(after.front()) && grammar->closesBlock(after) > 0)
        {
            // Between a bracket and the one that closes it: that one on a
            // line of its own, level with the opening, and the caret on the
            // line between them.
            text += "\n" + indent;
            caret = ALTextPos(sel.begin.line + 1, static_cast<S32>(inner.size()));
        }
    }
    if (before.empty() && after.empty())
    {
        // A line of nothing but blanks keeps none of them.
        range.begin.column = 0;
    }
    setSelection(range);
    insertText(text);
    if (caret.line >= 0)
    {
        setCaret(caret);
        mUndo.settle(selection());
    }
    mUndo.endGroup();
}

std::pair<S32, S32> ALTextView::selectedLines() const
{
    const ALTextRange range = selection().normalised();
    S32               last  = range.end.line;
    if (last > range.begin.line && range.end.column == 0)
    {
        --last;
    }
    return { range.begin.line, last };
}

void ALTextView::indentLines(bool in)
{
    const auto [first, last]   = selectedLines();
    const ALTextPos caret_was  = mCaret;
    const ALTextPos anchor_was = mAnchor;
    // What each line gained or lost at its start.
    std::vector<S32> delta(static_cast<size_t>(last - first + 1), 0);
    mUndo.beginGroup();
    for (S32 l = first; l <= last; ++l)
    {
        const std::string& line = mDocument.line(l);
        if (in)
        {
            if (!line.empty())
            {
                const std::string tab = mSoftTabs ? std::string(mTabWidth, ' ') : std::string("\t");
                edit(ALTextRange(ALTextPos(l, 0), ALTextPos(l, 0)), tab);
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
                while (taken < mTabWidth && taken < static_cast<S32>(line.size()) && line[taken] == ' ')
                {
                    ++taken;
                }
            }
            if (taken)
            {
                edit(ALTextRange(ALTextPos(l, 0), ALTextPos(l, taken)), std::string_view());
                delta[l - first] = -taken;
            }
        }
    }
    mUndo.endGroup();
    // The caret and the anchor stay where they were, moved by what their
    // lines gained or lost, and never before a line's start.
    const auto moved = [&](ALTextPos pos) {
        if (pos.line >= first && pos.line <= last)
        {
            pos.column = llmax(0, pos.column + delta[pos.line - first]);
        }
        return mDocument.clamp(pos);
    };
    placeSelection(moved(anchor_was), moved(caret_was));
    afterEdit();
}

void ALTextView::duplicateLines()
{
    const auto [first, last] = selectedLines();
    const std::string block  = mDocument.text(ALTextRange(mDocument.lineStart(first), mDocument.lineEnd(last)));
    const ALTextPos   caret  = mCaret;
    const ALTextPos   anchor = mAnchor;
    const S32         count  = last - first + 1;
    mUndo.beginGroup();
    edit(ALTextRange(mDocument.lineEnd(last), mDocument.lineEnd(last)), "\n" + block);
    mUndo.endGroup();
    // The caret and the selection go with the copy.
    placeSelection(ALTextPos(anchor.line + count, anchor.column), ALTextPos(caret.line + count, caret.column));
    afterEdit();
}

void ALTextView::moveLines(S32 direction)
{
    const auto [first, last] = selectedLines();
    if ((direction < 0 && first == 0) || (direction > 0 && last + 1 >= mDocument.lineCount()))
    {
        return;
    }
    const std::string block  = mDocument.text(ALTextRange(mDocument.lineStart(first), mDocument.lineEnd(last)));
    const ALTextPos   caret  = mCaret;
    const ALTextPos   anchor = mAnchor;
    mUndo.beginGroup();
    if (direction < 0)
    {
        const std::string above = mDocument.line(first - 1);
        edit(ALTextRange(mDocument.lineStart(first - 1), mDocument.lineEnd(last)), block + "\n" + above);
    }
    else
    {
        const std::string below = mDocument.line(last + 1);
        edit(ALTextRange(mDocument.lineStart(first), mDocument.lineEnd(last + 1)), below + "\n" + block);
    }
    mUndo.endGroup();
    placeSelection(ALTextPos(anchor.line + direction, anchor.column), ALTextPos(caret.line + direction, caret.column));
    afterEdit();
}

void ALTextView::deleteLines()
{
    const auto [first, last] = selectedLines();
    ALTextRange range(mDocument.lineStart(first), last + 1 < mDocument.lineCount() ? mDocument.lineStart(last + 1) : mDocument.lineEnd(last));
    if (last + 1 >= mDocument.lineCount() && first > 0)
    {
        // The last line goes with the newline before it.
        range.begin = mDocument.lineEnd(first - 1);
    }
    const S32 column = mCaret.column;
    mUndo.beginGroup();
    edit(range, std::string_view());
    mUndo.endGroup();
    const S32 line = llmin(first, mDocument.lineCount() - 1);
    placeCaret(ALTextPos(line, llmin(column, mDocument.lineLength(line))), false);
    afterEdit();
}

bool ALTextView::toggleComment()
{
    if (mReadOnly || !mHighlighter.grammar() || mHighlighter.grammar()->lineComment().empty())
    {
        return false;
    }
    const std::string& token   = mHighlighter.grammar()->lineComment();
    const auto [first, last]   = selectedLines();
    const bool had_selection   = hasSelection();
    const ALTextPos caret_was  = mCaret;

    // Out where every line that says anything is commented; in otherwise.
    auto indentation = [&](const std::string& line) {
        S32 n = 0;
        while (n < static_cast<S32>(line.size()) && (line[n] == ' ' || line[n] == '\t'))
        {
            ++n;
        }
        return n;
    };
    bool all_commented = true;
    bool any_text      = false;
    for (S32 l = first; l <= last; ++l)
    {
        const std::string& line = mDocument.line(l);
        const S32          n    = indentation(line);
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
        return true;
    }
    S32 caret_shift = 0;
    mUndo.beginGroup();
    for (S32 l = first; l <= last; ++l)
    {
        const std::string& line = mDocument.line(l);
        const S32          n    = indentation(line);
        if (n == static_cast<S32>(line.size()))
        {
            continue;
        }
        if (all_commented)
        {
            S32 taken = static_cast<S32>(token.size());
            if (n + taken < static_cast<S32>(line.size()) && line[n + taken] == ' ')
            {
                ++taken;
            }
            edit(ALTextRange(ALTextPos(l, n), ALTextPos(l, n + taken)), std::string_view());
            if (l == caret_was.line)
            {
                caret_shift = caret_was.column >= n + taken ? -taken : (caret_was.column > n ? n - caret_was.column : 0);
            }
        }
        else
        {
            edit(ALTextRange(ALTextPos(l, n), ALTextPos(l, n)), token + " ");
            if (l == caret_was.line && caret_was.column >= n)
            {
                caret_shift = static_cast<S32>(token.size()) + 1;
            }
        }
    }
    mUndo.endGroup();
    if (had_selection)
    {
        placeSelection(ALTextPos(first, 0), last + 1 < mDocument.lineCount() ? ALTextPos(last + 1, 0) : mDocument.lineEnd(last));
    }
    else
    {
        placeCaret(ALTextPos(caret_was.line, caret_was.column + caret_shift), false);
    }
    afterEdit();
    return true;
}
