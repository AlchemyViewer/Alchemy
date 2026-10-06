/**
 * @file aldifffill.cpp
 * @brief A comparison's editor told of a column of it, or of what changed.
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

#include "aldifffill.h"

#include "alcodeeditor.h"
#include "aldiffcolors.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace
{
    typedef ALDiffModel::Column         Column;
    typedef ALTextView::LineAnnotation  LineAnnotation;
    typedef ALCodeEditor::Decoration    Decoration;

    // The colours a column is told in, as the colour table has them now.
    struct Tints
    {
        typedef ALDiffColors::Name Name;
        const LLColor4 out      = ALDiffColors::get(Name::Removed).get();
        const LLColor4 in       = ALDiffColors::get(Name::Added).get();
        const LLColor4 padding  = ALDiffColors::get(Name::Padding).get();
        const LLColor4 outWords = ALDiffColors::get(Name::RemovedWord).get();
        const LLColor4 inWords  = ALDiffColors::get(Name::AddedWord).get();
        // The ruler's: each side's own change, and beside a gap, the other's.
        const LLColor4 outMark  = ALDiffColors::get(Name::RemovedMark).get();
        const LLColor4 inMark   = ALDiffColors::get(Name::AddedMark).get();
        // A block moved, at either end: neither red nor green.
        const LLColor4 moved     = ALDiffColors::get(Name::Moved).get();
        const LLColor4 movedMark = ALDiffColors::get(Name::MovedMark).get();
    };

    // The rows of nothing above a line, or below the last, beside lines
    // the other side has: marked on the ruler as theirs.
    void sayGap(LineAnnotation& said, S32 gap, Column column, const Tints& tints)
    {
        said.gap = gap;
        if (gap > 0)
        {
            said.gapTint      = tints.padding;
            said.gapRulerTint = column == Column::Left ? tints.inMark : tints.outMark;
        }
    }

    // What is said of a line: its number, where the text is not its own;
    // its tint, its mark on the ruler and its sign, as it was taken out or
    // put in; and the rows of nothing above it.
    LineAnnotation saidOf(const ALDiffModel::Line& line, Column column, const Tints& tints)
    {
        LineAnnotation said;
        const bool     gone = line.kind == ALDiffModel::Kind::Removed;
        const bool     made = line.kind == ALDiffModel::Kind::Added;
        if (column == Column::Inline)
        {
            said.number = line.number;
        }
        said.sign      = line.sign;
        said.tint      = line.move >= 0 ? tints.moved : gone ? tints.out : made ? tints.in : LLColor4::transparent;
        said.rulerTint = line.move >= 0 ? tints.movedMark : gone ? tints.outMark : made ? tints.inMark : LLColor4::transparent;
        sayGap(said, line.padding, column, tints);
        return said;
    }

    LineAnnotation saidBelow(const ALDiffModel& model, Column column, const Tints& tints)
    {
        LineAnnotation said;
        sayGap(said, model.endPadding(column), column, tints);
        return said;
    }

    // The words of a line that changed.
    void addWords(const ALDiffModel::Line& line, S32 at, const Tints& tints, std::vector<Decoration>& words)
    {
        for (const auto& [begin, end] : line.words)
        {
            Decoration d;
            d.range = ALTextRange(ALTextPos(at, begin), ALTextPos(at, end));
            d.style = Decoration::Style::Background;
            d.color = line.kind == ALDiffModel::Kind::Removed ? tints.outWords : tints.inWords;
            words.push_back(d);
        }
    }
}

void ALDiffFill::whole(ALCodeEditor& side, const ALDiffModel& model, Column column)
{
    const Tints                 tints;
    const S32                   count = model.lineCount(column);
    std::vector<LineAnnotation> said;
    std::vector<Decoration>     words;
    said.reserve(static_cast<size_t>(count) + 1);
    for (S32 l = 0; l < count; ++l)
    {
        const ALDiffModel::Line& line = model.line(column, l);
        said.push_back(saidOf(line, column, tints));
        addWords(line, l, tints, words);
    }
    said.push_back(saidBelow(model, column, tints));
    // The text first, where it changed: a new text clears what is said of
    // its lines. A side's text is the whole of one of the texts, so only
    // the one edited changes; one as it was keeps its place, but not the
    // lines the folds hid, which are others now.
    if (side.document().wholeText() != model.text(column))
    {
        side.setText(model.text(column));
    }
    else
    {
        side.layout().setHidden(ALTextLayout::HiddenBy::Host, 0, side.document().lineCount() - 1, false);
    }
    side.setLineAnnotations(std::move(said));
    side.setDecorations(std::move(words));
}

bool ALDiffFill::again(ALCodeEditor& side, const ALDiffModel& model, Column column)
{
    const ALDiffModel::Relaid& relaid   = model.relaid();
    const size_t               c        = static_cast<size_t>(column);
    const S32                  first    = relaid.first[c];
    const S32                  was      = relaid.was[c];
    const S32                  now      = relaid.now[c];
    const S32                  count    = model.lineCount(column);
    ALTextDocument&            document = side.document();
    const S32                  had      = document.lineCount();
    if (relaid.whole || count == 0 || first + now > count || had != count - now + was)
    {
        return false;
    }
    // The stretch's lines in the text now, which start where they did:
    // the lines before them are the same lines.
    const std::string_view        text = model.text(column);
    size_t                        at   = 0;
    std::vector<std::string_view> lines;
    for (S32 l = 0; l < first; ++l)
    {
        at += static_cast<size_t>(document.lineLength(l)) + 1;
    }
    lines.reserve(static_cast<size_t>(now));
    for (S32 n = 0; n < now; ++n)
    {
        if (at > text.size())
        {
            return false;
        }
        const size_t end = std::min(text.find('\n', at), text.size());
        lines.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    // Past the text's end exactly where the stretch is at the end of it.
    if ((first + now == count) != (at == text.size() + 1))
    {
        return false;
    }
    // Of those, the ones that differ from what the editor has there, put
    // in in place of the others: a line before the stretch, or after it,
    // is the line it was; and so is the line the stretch ends beside,
    // which the edit takes from its start, or, at the end of the text, the
    // one it begins beside, from its end.
    S32 head = 0;
    while (head < was && head < now && document.line(first + head) == lines[static_cast<size_t>(head)])
    {
        ++head;
    }
    S32 tail = 0;
    while (tail < was - head && tail < now - head && document.line(first + was - 1 - tail) == lines[static_cast<size_t>(now - 1 - tail)])
    {
        ++tail;
    }
    S32 from = first;
    if (head + tail < std::max(was, now))
    {
        const S32   put_at = first + head;
        const S32   out    = was - head - tail;
        const S32   in     = now - head - tail;
        std::string put;
        ALTextRange over;
        if (put_at + out < had)
        {
            over = ALTextRange(ALTextPos(put_at, 0), ALTextPos(put_at + out, 0));
            for (S32 n = 0; n < in; ++n)
            {
                put += lines[static_cast<size_t>(head + n)];
                put += '\n';
            }
        }
        else if (put_at > 0)
        {
            over = ALTextRange(ALTextPos(put_at - 1, document.lineLength(put_at - 1)), document.end());
            for (S32 n = 0; n < in; ++n)
            {
                put += '\n';
                put += lines[static_cast<size_t>(head + n)];
            }
            from = std::min(from, put_at - 1);
        }
        else
        {
            over = ALTextRange(document.start(), document.end());
            for (S32 n = 0; n < in; ++n)
            {
                put += n ? "\n" : "";
                put += lines[static_cast<size_t>(head + n)];
            }
        }
        side.replaceText(over, put);
        if (document.lineCount() != count)
        {
            return false;
        }
    }
    // What is said of the lines from the first the edit touched to the
    // first after the stretch, whose rows of nothing above it are those now
    // waiting there, or below the last, where the stretch is at the end;
    // the numbers of those after moved along first. Their words, and none
    // of them hidden: the folds there are applied again after.
    const Tints tints;
    const S32   after = first + now;
    if (column == Column::Inline)
    {
        side.renumberLines(after, relaid.numbered[c]);
    }
    std::vector<Decoration> words;
    for (S32 l = from; l <= after; ++l)
    {
        if (l == count)
        {
            side.setLineAnnotation(l, saidBelow(model, column, tints));
            break;
        }
        const ALDiffModel::Line& line = model.line(column, l);
        side.setLineAnnotation(l, saidOf(line, column, tints));
        addWords(line, l, tints, words);
    }
    side.setDecorations(from, after, std::move(words));
    side.layout().setHidden(ALTextLayout::HiddenBy::Host, from, after - 1, false);
    return true;
}
