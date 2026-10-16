/**
 * @file alchangepeek.cpp
 * @brief A peek at a change from the code editor: its lines as saved, in a gap under them.
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

#include "alchangepeek.h"

#include "alcodeeditor.h"
#include "aldiffcolors.h"
#include "aldiffedit.h"
#include "alflatbutton.h"
#include "alsaid.h"
#include "alsurface.h"
#include "allinepairs.h"
#include "altextdiff.h"

#include "lllocalcliprect.h"
#include "llrender2dutils.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"

namespace
{
    constexpr S32 ROW     = 20;
    constexpr S32 PAD     = 2;
    constexpr S32 SMALL_W = 22;
}

// static
S32 ALChangePeek::changeAt(const std::vector<Change>& changes, S32 line)
{
    for (size_t i = 0; i < changes.size(); ++i)
    {
        const Change& c = changes[i];
        if ((c.nowCount > 0 && line >= c.now && line < c.now + c.nowCount) || (c.nowCount == 0 && line == c.now))
        {
            return static_cast<S32>(i);
        }
    }
    // Lines taken out from the text's end: the bar is on the line before.
    for (size_t i = 0; i < changes.size(); ++i)
    {
        if (changes[i].nowCount == 0 && line == changes[i].now - 1)
        {
            return static_cast<S32>(i);
        }
    }
    return -1;
}

// static
bool ALChangePeek::stepFrom(ALCodeEditor& host, bool forward)
{
    const std::shared_ptr<const ALChangesSinceSaved::Known> known = host.changesSinceSaved();
    if (!known)
    {
        return false;
    }
    // The start of the next change after the caret's line, or of the last
    // before it: from within a change, back is to its own start.
    const std::vector<Change>& changes = known->changes;
    const S32                 line    = host.caret().line;
    const Change*             to      = nullptr;
    for (const Change& c : changes)
    {
        if (forward ? c.now > line : c.now < line)
        {
            to = &c;
            if (forward)
            {
                break;
            }
        }
    }
    if (!to)
    {
        return false;
    }
    const S32 at = llmin(to->now, host.document().lineCount() - 1);
    host.goTo(ALTextPos(at, 0));
    if (ALChangePeek* peek = host.changePeek(); peek && peek->isOpen())
    {
        peek->showAt(at);
    }
    return true;
}

ALChangePeek::ALChangePeek(ALCodeEditor& host)
:   LLPanel(LLPanel::getDefaultParams()),
    mHost(host)
{
    setName("change_peek");
    setVisible(false);
    setMouseOpaque(true);
    setFollows(FOLLOWS_NONE);

    LLTextBox::Params tp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
    tp.name          = "said";
    tp.rect          = LLRect(0, ROW, 100, 0);
    tp.h_pad         = 4;
    tp.v_pad         = 3;
    tp.font          = LLFontGL::getFontSansSerifSmall();
    tp.use_ellipses  = true;
    tp.follows.flags = FOLLOWS_NONE;
    mSaid            = LLUICtrlFactory::create<LLTextBox>(tp);
    addChild(mSaid);

    const LLColor4 ink  = host.textColor();
    const LLColor4 lit  = ALSurface::chosen(host.backgroundColor(), ink);
    const auto     flat = [&](const std::string& name, const std::string& glyph, const std::string& tip) {
        ALFlatButton::Params fp;
        fp.name            = name;
        fp.rect            = LLRect(0, ROW, SMALL_W, 0);
        fp.tool_tip        = tip;
        fp.follows.flags   = FOLLOWS_NONE;
        ALFlatButton* made = new ALFlatButton(fp, glyph, false, ink, lit);
        addChild(made);
        return made;
    };
    mTakeBack = flat("take_back", "\xE2\x86\xB6", alSaid("PeekTakeBack", "Take this change back"));
    mPrevious = flat("previous", "\xE2\x86\x91", alSaid("PeekPrevious", "Previous change"));
    mNext     = flat("next", "\xE2\x86\x93", alSaid("PeekNext", "Next change"));
    mClose    = flat("close", "\xC3\x97", alSaid("PeekClose", "Close"));
    mTakeBack->setCommitCallback([this](LLUICtrl*, const LLSD&) { takeBack(); });
    mPrevious->setCommitCallback([this](LLUICtrl*, const LLSD&) { step(false); });
    mNext->setCommitCallback([this](LLUICtrl*, const LLSD&) { step(true); });
    mClose->setCommitCallback([this](LLUICtrl*, const LLSD&) { close(); });
    // The keys the editor steps and closes it by, as a comparison's bar
    // has them.
    mPrevious->setKey(KEY_F7, MASK_SHIFT);
    mNext->setKey(KEY_F7, MASK_NONE);
    mClose->setKey(KEY_ESCAPE, MASK_NONE);

    // The lines as saved: read only, unfolded, unwrapped, in the editor's
    // grammar, face and colours.
    ALCodeEditor::Params ep(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
    ep.name          = "peek_saved";
    ep.rect          = LLRect(0, ROW * 2, 100, 0);
    ep.read_only     = true;
    ep.word_wrap     = false;
    ep.follows.flags = FOLLOWS_NONE;
    mSaved           = LLUICtrlFactory::create<ALCodeEditor>(ep);
    mSaved->setFoldable(false);
    addChild(mSaved);

    // An edit of the text from anywhere but here: what is shown is of the
    // text before it.
    mChangedConnection = host.onTextChanged([this]() {
        if (!mEditing && isOpen())
        {
            close();
        }
    });
    // Heard after the editor, which listened to its text first, has slid
    // the gaps; and before the edit is told as a change, which closes it.
    mEditConnection = host.document().onChanged([this](const ALTextDocument::Edit& edit) { slideGap(edit); });
}

ALChangePeek::~ALChangePeek() = default;

bool ALChangePeek::showAt(S32 line)
{
    std::shared_ptr<const ALChangesSinceSaved::Known> known = mHost.changesSinceSaved();
    if (!known)
    {
        close();
        return false;
    }
    mKnown       = std::move(known);
    const S32 at = changeAt(mKnown->changes, line);
    if (at < 0)
    {
        close();
        return false;
    }
    mShown = at;
    fill();
    return true;
}

bool ALChangePeek::step(bool forward)
{
    if (!isOpen())
    {
        return false;
    }
    const S32 to = mShown + (forward ? 1 : -1);
    if (to < 0 || to >= changeCount())
    {
        return false;
    }
    mShown = to;
    fill();
    // Its lines in sight, the caret on the first.
    mHost.goTo(ALTextPos(llmin(change().now, mHost.document().lineCount() - 1), 0));
    return true;
}

bool ALChangePeek::takeBack()
{
    if (!isOpen() || mHost.isReadOnly())
    {
        return false;
    }
    const Change                   c = change();
    const std::vector<std::string> was(mKnown->saved.begin() + c.saved, mKnown->saved.begin() + c.saved + c.savedCount);
    ALTextRange                    range;
    std::string                    put;
    std::string                    made;
    // Over the text as it is: the lines known, where it has not moved.
    const bool same = mKnown->version == mHost.document().version();
    if (!ALDiffEdit::replaceLines(same ? mKnown->now : ALTextDiff::split(mHost.wholeText()), c.now, c.nowCount, was, range, put, made))
    {
        return false;
    }
    shutGap();
    unmarkWords();
    mEditing        = true;
    const bool done = mHost.replaceAll({ { range, put } });
    mEditing        = false;
    if (!done)
    {
        fill();
        return false;
    }
    // On to the change that was after it, now at this one's place.
    const S32 next = mShown;
    mShown         = -1;
    mKnown         = mHost.changesSinceSaved();
    if (next < changeCount())
    {
        mShown = next;
        fill();
    }
    else
    {
        close();
    }
    return true;
}

void ALChangePeek::close()
{
    shutGap();
    unmarkWords();
    mShown = -1;
    setVisible(false);
}

std::string ALChangePeek::said() const
{
    if (!isOpen())
    {
        return std::string();
    }
    const Change&                    c = change();
    const LLStringUtil::format_map_t args{ { "[CURRENT]", std::to_string(mShown + 1) }, { "[TOTAL]", std::to_string(changeCount()) } };
    return c.savedCount == 0 ? alSaid("PeekAdded", "Added since saved \xC2\xB7 [CURRENT] of [TOTAL]", args)
           : c.nowCount == 0 ? alSaid("PeekRemoved", "Taken out since saved \xC2\xB7 [CURRENT] of [TOTAL]", args)
                             : alSaid("PeekChanged", "Changed since saved \xC2\xB7 [CURRENT] of [TOTAL]", args);
}

S32 ALChangePeek::height() const
{
    const S32 rows = isOpen() ? llmin(change().savedCount, MOST_ROWS) : 0;
    return PAD + ROW + (rows > 0 ? PAD + rows * mHost.layout().rowHeight() + PAD : 0) + PAD;
}

void ALChangePeek::fill()
{
    const Change& c = change();
    // What it was, numbered as it was, tinted as lines taken out are.
    std::string text;
    for (S32 n = 0; n < c.savedCount; ++n)
    {
        text += (n ? "\n" : "") + mKnown->saved[static_cast<size_t>(c.saved + n)];
    }
    mSaved->setFont(mHost.getFont());
    mSaved->setGrammar(mHost.highlighter().grammar());
    mSaved->setBackgroundColor(mHost.backgroundColor());
    mSaved->setTextColor(mHost.textColor());
    mSaved->setText(text);
    const LLColor4 removed = ALDiffColors::get(ALDiffColors::Name::Removed).get();
    std::vector<ALTextView::LineAnnotation> lines(static_cast<size_t>(c.savedCount));
    for (S32 n = 0; n < c.savedCount; ++n)
    {
        lines[static_cast<size_t>(n)].number = c.saved + n + 1;
        lines[static_cast<size_t>(n)].sign   = '-';
        lines[static_cast<size_t>(n)].tint   = removed;
    }
    mSaved->setLineAnnotations(std::move(lines));
    mSaved->setVisible(c.savedCount > 0);
    markWords();
    mSaid->setText(said());
    mSaid->setColor(mHost.textColor());
    mTakeBack->setEnabled(!mHost.isReadOnly());
    mPrevious->setEnabled(mShown > 0);
    mNext->setEnabled(mShown + 1 < changeCount());
    // Under its lines as they are; for lines only taken out, where they
    // were taken from.
    const S32 rows = (height() + mHost.layout().rowHeight() - 1) / llmax(1, mHost.layout().rowHeight());
    openGap(c.now + c.nowCount, rows);
    setVisible(true);
    place();
}

void ALChangePeek::markWords()
{
    // Each line as saved beside the line now it became, as a comparison
    // pairs them; their words that differ marked on each.
    const Change&    c = change();
    std::vector<S32> gone;
    std::vector<S32> made;
    for (S32 n = 0; n < c.savedCount; ++n)
    {
        gone.push_back(c.saved + n);
    }
    for (S32 n = 0; n < c.nowCount; ++n)
    {
        made.push_back(c.now + n);
    }
    const LLColor4 out_words = ALDiffColors::get(ALDiffColors::Name::RemovedWord).get();
    std::vector<ALCodeEditor::Decoration> was;
    std::vector<ALTextRange>              now;
    for (const auto& [at_gone, at_made] : ALLinePairs::pair(mKnown->saved, mKnown->now, gone, made))
    {
        const S32           saved_line = gone[static_cast<size_t>(at_gone)];
        const S32           now_line   = made[static_cast<size_t>(at_made)];
        ALTextDiff::spans_t out;
        ALTextDiff::spans_t in;
        ALTextDiff::words(mKnown->saved[static_cast<size_t>(saved_line)], mKnown->now[static_cast<size_t>(now_line)], out, in);
        for (const auto& [begin, end] : out)
        {
            ALCodeEditor::Decoration d;
            d.range = ALTextRange(ALTextPos(saved_line - c.saved, begin), ALTextPos(saved_line - c.saved, end));
            d.style = ALCodeEditor::Decoration::Style::Background;
            d.color = out_words;
            was.push_back(d);
        }
        for (const auto& [begin, end] : in)
        {
            now.emplace_back(ALTextPos(now_line, begin), ALTextPos(now_line, end));
        }
    }
    mSaved->setDecorations(std::move(was));
    mHost.setHighlights(ALCodeEditor::Highlight::Change, std::move(now));
}

void ALChangePeek::unmarkWords()
{
    mHost.clearHighlights(ALCodeEditor::Highlight::Change);
}

void ALChangePeek::openGap(S32 line, S32 rows)
{
    shutGap();
    ALTextView::LineAnnotation said = mHost.lineAnnotation(line);
    mGapWas                         = said.gap;
    mGapLine                        = line;
    said.gap += rows;
    mHost.setLineAnnotation(line, said);
}

void ALChangePeek::shutGap()
{
    if (mGapLine < 0)
    {
        return;
    }
    ALTextView::LineAnnotation said = mHost.lineAnnotation(mGapLine);
    said.gap                        = mGapWas;
    mHost.setLineAnnotation(mGapLine, said);
    mGapLine = -1;
    mGapWas  = 0;
}

void ALChangePeek::slideGap(const ALTextDocument::Edit& edit)
{
    const std::vector<ALTextDocument::Edit::LineSpan>& spans = edit.lineSpans();
    if (mGapLine < 0 || spans.empty())
    {
        return;
    }
    // Under the last line, the gap is the text's end's, which stays there.
    const S32 lines = mHost.document().lineCount();
    if (mGapLine >= lines - spans.back().shiftAfter)
    {
        mGapLine = lines;
        return;
    }
    S32 moved = 0;
    for (const ALTextDocument::Edit::LineSpan& span : spans)
    {
        if (mGapLine < span.first)
        {
            break;
        }
        if (mGapLine <= span.last)
        {
            // On the first line the edit replaced, the gap stays above the
            // first line it made there; on a line after that, it went.
            if (mGapLine == span.first && span.made > 0)
            {
                mGapLine = span.first + moved;
            }
            else
            {
                mGapLine = -1;
                mGapWas  = 0;
            }
            return;
        }
        moved = span.shiftAfter;
    }
    mGapLine += moved;
}

void ALChangePeek::place()
{
    if (!isOpen() || mGapLine < 0)
    {
        return;
    }
    // Across the text, down from its gap's top; its own gap's rows, under
    // whatever gap the line had before.
    const LLRect  text   = mHost.textRect();
    ALTextLayout& layout = mHost.layout();
    const S32     top    = text.mTop - (layout.gapTop(mGapLine) + mGapWas * layout.rowHeight() - mHost.scrollY());
    const S32     tall   = height();
    const S32     bottom = top - tall;
    // Its own rectangle only the part of it in the text, which is where it
    // takes the mouse: scrolled partly out, the rest goes on past its
    // edges, drawn cut there, and a press there is the view's. What is
    // inside it placed as the whole of it has them, from the bottom cut off.
    const S32 shown_top    = llmin(top, text.mTop);
    const S32 shown_bottom = llmin(llmax(bottom, text.mBottom), shown_top);
    mCut                   = shown_bottom - bottom;
    setShape(LLRect(text.mLeft, shown_top, text.mRight, shown_bottom));
    const S32 width = text.getWidth();
    const S32 head  = tall - mCut;
    S32       right = width - PAD;
    for (ALFlatButton* button : { mClose, mNext, mPrevious, mTakeBack })
    {
        button->setShape(LLRect(right - SMALL_W, head - PAD, right, head - PAD - ROW));
        right -= SMALL_W + 1;
    }
    mSaid->setShape(LLRect(PAD, head - PAD, llmax(PAD, right - PAD), head - PAD - ROW));
    mSaved->setShape(LLRect(PAD, head - PAD - ROW - PAD, width - PAD, PAD - mCut));
}

void ALChangePeek::draw()
{
    // Within the text it stands in: scrolled partly out of it, cut there.
    const LLRect    text = mHost.textRect();
    LLLocalClipRect clip(LLRect(text.mLeft - getRect().mLeft, text.mTop - getRect().mBottom, text.mRight - getRect().mLeft,
                                text.mBottom - getRect().mBottom));
    const F32       alpha = getDrawContext().mAlpha;
    const LLColor4  paper = mHost.backgroundColor();
    const LLColor4  ink   = mHost.textColor();
    // The whole of it, of which its rectangle is the part in sight.
    const LLRect    whole(0, height() - mCut, getRect().getWidth(), -mCut);
    gl_rect_2d(whole, ALSurface::ground(paper, ink) % alpha);
    gl_rect_2d(whole, ALSurface::frame(ink, alpha), false);
    LLPanel::draw();
}

bool ALChangePeek::handleKeyHere(KEY key, MASK mask)
{
    if (key == KEY_ESCAPE && mask == MASK_NONE)
    {
        close();
        mHost.setFocus(true);
        return true;
    }
    if (key == KEY_F7 && (mask == MASK_NONE || mask == MASK_SHIFT))
    {
        step(mask == MASK_NONE);
        return true;
    }
    return LLPanel::handleKeyHere(key, mask);
}
