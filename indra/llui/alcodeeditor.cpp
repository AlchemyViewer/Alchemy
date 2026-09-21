/**
 * @file alcodeeditor.cpp
 * @brief The text view as a code editor: a gutter, marks, a matched bracket, squiggles, folds, completion.
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

#include "alcodeeditor.h"

#include "llfocusmgr.h"
#include "llrender2dutils.h"
#include "alchoicelist.h"
#include "llstring.h"
#include "lluicolortable.h"
#include "llurlaction.h"
#include "lluictrlfactory.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <optional>
#include <cmath>

static LLDefaultChildRegistry::Register<ALCodeEditor> r("code_editor");

namespace
{
    const S32 GUTTER_PAD  = 6;
    const S32 MARK_SIZE   = 6;
    const S32 MARK_INSET  = 3;
    const S32 FOLD_COLUMN = 12;
    const S32 FOLD_MARKER = 7;
    const S32 FOLD_BOX_GAP = 6;
    const S32 COMPLETION_WIDTH   = 360;
    const S32 COMPLETION_ROWS    = 8;
    const S32 COMPLETION_AUTO_AT = 2;
    const size_t COMPLETION_CAP  = 200;
    const S32    SIGNATURE_PAD   = 6;

    const char* const MARK_COLOR_NAMES[] = { "TextFgColor", "CodeMarkNote", "CodeMarkWarning", "CodeMarkError", "CodeMarkRuntime" };
    static_assert(sizeof(MARK_COLOR_NAMES) / sizeof(MARK_COLOR_NAMES[0]) == static_cast<size_t>(ALCodeEditor::Mark::COUNT), "every mark has a colour");

    // The bracket a character is, if any: its partner, and which way to look.
    bool bracketOf(char c, char& partner, bool& opens)
    {
        switch (c)
        {
            case '(': partner = ')'; opens = true;  return true;
            case '[': partner = ']'; opens = true;  return true;
            case '{': partner = '}'; opens = true;  return true;
            case ')': partner = '('; opens = false; return true;
            case ']': partner = '['; opens = false; return true;
            case '}': partner = '{'; opens = false; return true;
            default:  return false;
        }
    }

    bool quiet(ALSyntaxKind kind)
    {
        return kind == ALSyntaxKind::String || kind == ALSyntaxKind::Comment || kind == ALSyntaxKind::DocComment ||
               kind == ALSyntaxKind::Escape || kind == ALSyntaxKind::AttributeValue;
    }

    // Where a position past an edit ends up once the edit is made.
    bool identifierByte(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    // A line with nothing but whitespace, or nothing.
    std::string_view trimmed(const std::string& line)
    {
        size_t begin = line.find_first_not_of(" \t\r");
        if (begin == std::string::npos)
        {
            return std::string_view();
        }
        size_t end = line.find_last_not_of(" \t\r");
        return std::string_view(line).substr(begin, end - begin + 1);
    }

    // What closes a block on a line of its own, and so belongs to the
    // block above it.
    bool closesBlock(std::string_view text)
    {
        static const char* const CLOSERS[] = { "}", "};", "})", "});", "end", "end)", "end,", "end);", ")", ");", "]", "],", "];" };
        for (const char* closer : CLOSERS)
        {
            if (text == closer)
            {
                return true;
            }
        }
        return false;
    }

    ALSyntaxKind kindOfTable(std::string_view table)
    {
        if (table == "function") return ALSyntaxKind::Function;
        if (table == "event") return ALSyntaxKind::Event;
        if (table == "type") return ALSyntaxKind::Type;
        if (table == "constant") return ALSyntaxKind::Constant;
        if (table == "control") return ALSyntaxKind::Control;
        if (table == "keyword") return ALSyntaxKind::Keyword;
        if (table == "deprecated") return ALSyntaxKind::Deprecated;
        return ALSyntaxKind::Text;
    }
}

ALCodeEditor::Params::Params()
:   show_line_numbers("show_line_numbers", true),
    show_fold_markers("show_fold_markers", true),
    highlight_current_line("highlight_current_line", true),
    match_brackets("match_brackets", true),
    auto_complete("auto_complete", true),
    gutter_color("gutter_color"),
    line_number_color("line_number_color"),
    current_line_color("current_line_color"),
    bracket_match_color("bracket_match_color"),
    fold_color("fold_color"),
    highlight_color("highlight_color"),
    changed_color("changed_color"),
    bracket_color_1("bracket_color_1"),
    bracket_color_2("bracket_color_2"),
    bracket_color_3("bracket_color_3")
{
}

ALCodeEditor::ALCodeEditor(const Params& p)
:   ALTextView(p),
    mShowLineNumbers(p.show_line_numbers),
    mShowFoldMarkers(p.show_fold_markers),
    mHighlightCurrentLine(p.highlight_current_line),
    mMatchBrackets(p.match_brackets),
    mAutoComplete(p.auto_complete),
    mGutterColor(p.gutter_color),
    mLineNumberColor(p.line_number_color),
    mCurrentLineColor(p.current_line_color),
    mBracketMatchColor(p.bracket_match_color),
    mFoldColor(p.fold_color),
    mHighlightColor(p.highlight_color),
    mChangedColor(p.changed_color),
    mGutterColorSet(p.gutter_color.isProvided()),
    mLineNumberColorSet(p.line_number_color.isProvided()),
    mCurrentLineColorSet(p.current_line_color.isProvided()),
    mFoldColorSet(p.fold_color.isProvided()),
    mHighlightColorSet(p.highlight_color.isProvided()),
    mChangedColorSet(p.changed_color.isProvided())
{
    mBracketColorsSet = p.bracket_color_1.isProvided() && p.bracket_color_2.isProvided() && p.bracket_color_3.isProvided();
    if (mBracketColorsSet)
    {
        mBracketColors[0] = p.bracket_color_1;
        mBracketColors[1] = p.bracket_color_2;
        mBracketColors[2] = p.bracket_color_3;
    }
    for (size_t mark = 0; mark < static_cast<size_t>(Mark::COUNT); ++mark)
    {
        mMarkColors[mark] = LLUIColorTable::instance().getColor(MARK_COLOR_NAMES[mark], LLColor4::red);
    }
    mMarks.assign(document().lineCount(), Mark::None);
    mEditConnection    = document().onChanged([this](const ALTextDocument::Edit& edit) { onEdit(edit); });
    layout().setInlayProvider([this](S32 line, std::vector<ALTextLayout::Inlay>& out) { provideInlays(line, out); });
    mChangedConnection = onTextChanged([this]() {
        if (completionOpen())
        {
            refreshCompletion();
        }
    });

    // The list of completions, made once and shown when there is
    // something to choose; a child, so it draws over the text and goes
    // where the view goes. On the text engine, as the hover card is: the
    // completions in the editor's face and the colours of their kinds,
    // their details in the reading face after them.
    ALChoiceList::Params list(LLUICtrlFactory::getDefaultParams<ALChoiceList>());
    list.name("completions");
    list.rect(LLRect(0, 10, 10, 0));
    list.visible(false);
    list.follows.flags(FOLLOWS_NONE);
    list.mouse_opaque(true);
    list.font(getFont());
    list.bg_visible(true);
    list.bg_color(LLUIColorTable::instance().getColor("CodeCompletionBgColor", LLColor4::black));
    list.bg_readonly_color(LLUIColorTable::instance().getColor("CodeCompletionBgColor", LLColor4::black));
    list.text_readonly_color(textColor());
    list.context_menu(std::string());
    list.h_pad(4);
    list.v_pad(2);
    mCompletionList = LLUICtrlFactory::create<ALChoiceList>(list);
    mCompletionList->onPicked([this](S32) { acceptCompletion(); });
    addChild(mCompletionList);
}

ALCodeEditor::~ALCodeEditor() = default;

// --- marks and decorations ---------------------------------------------------

void ALCodeEditor::onEdit(const ALTextDocument::Edit& edit)
{
    hideCard();
    const S32 first = llclamp(edit.range.begin.line, 0, static_cast<S32>(mMarks.size()));
    const S32 last  = llclamp(edit.range.end.line, first, static_cast<S32>(mMarks.size()) - 1);
    const S32 made  = 1 + static_cast<S32>(std::count(edit.inserted.begin(), edit.inserted.end(), '\n'));
    if (first < static_cast<S32>(mMarks.size()))
    {
        mMarks.erase(mMarks.begin() + first, mMarks.begin() + last + 1);
    }
    mMarks.insert(mMarks.begin() + first, made, Mark::None);
    mMarks.resize(document().lineCount(), Mark::None);
    // What is known of bracket depth below the edit is known no more.
    mDepthValid = llmin(mDepthValid, first);
    // The lines the edit touched are changed until the next save.
    mChanged.resize(llmax(mChanged.size(), static_cast<size_t>(last + 1)), 0);
    mChanged.erase(mChanged.begin() + first, mChanged.begin() + last + 1);
    mChanged.insert(mChanged.begin() + first, made, 1);
    mChanged.resize(document().lineCount(), 0);

    // Decorations and highlights after the edit move along with the text;
    // the ones it cut into go.
    const S32         delta   = made - (last - first + 1);
    const ALTextRange removed = edit.range.normalised();
    mDecorations.erase(std::remove_if(mDecorations.begin(), mDecorations.end(), [&](Decoration& d) { return !edit.slide(d.range); }), mDecorations.end());
    mHighlights.erase(std::remove_if(mHighlights.begin(), mHighlights.end(), [&](ALTextRange& r) { return !edit.slide(r); }), mHighlights.end());
    mSemantics.erase(std::remove_if(mSemantics.begin(), mSemantics.end(), [&](SemanticToken& t) { return !edit.slide(t.range); }), mSemantics.end());
    // An inlay moves with the text it stands by: one before the text at
    // its position stays put when something is typed there, since what
    // is typed is the start of that text; one after the text before it
    // moves along, since what is typed extends that text. An edit that
    // takes the position with it takes the inlay.
    mInlays.erase(std::remove_if(mInlays.begin(), mInlays.end(),
                                 [&](InlayHint& h) {
                                     if (removed.empty())
                                     {
                                         if (removed.begin < h.at || (removed.begin == h.at && !h.before))
                                         {
                                             h.at = edit.slidPast(h.at);
                                         }
                                         return false;
                                     }
                                     if (removed.end <= h.at)
                                     {
                                         h.at = edit.slidPast(h.at);
                                         return false;
                                     }
                                     return !(h.at <= removed.begin);
                                 }),
                  mInlays.end());
    // The placeholder being typed over becomes what was typed; the others
    // move with the text, and one the edit cut into goes.
    if (!mPlaceholders.empty())
    {
        for (S32 i = 0; i < static_cast<S32>(mPlaceholders.size());)
        {
            ALTextRange& r = mPlaceholders[i];
            if (i == mPlaceholderAt && r.begin <= removed.begin && removed.end <= r.end)
            {
                r.end = edit.slidPast(r.end);
                ++i;
            }
            else if (edit.slide(r))
            {
                ++i;
            }
            else
            {
                mPlaceholders.erase(mPlaceholders.begin() + i);
                if (mPlaceholderAt > i)
                {
                    --mPlaceholderAt;
                }
                else if (mPlaceholderAt == i)
                {
                    mPlaceholderAt = -1;
                }
            }
        }
        if (mPlaceholdersAfter.line == removed.end.line || mPlaceholdersAfter.line > removed.end.line)
        {
            if (removed.end <= mPlaceholdersAfter)
            {
                mPlaceholdersAfter = edit.slidPast(mPlaceholdersAfter);
            }
        }
        if (mPlaceholders.empty() || mPlaceholderAt < 0)
        {
            clearPlaceholders();
        }
    }

    // Folds slide the same way. One that starts on the edit's first line
    // stays: typing on a block's first line is not opening the block.
    mFolded.erase(std::remove_if(mFolded.begin(), mFolded.end(), [&](S32 start) { return start > first && start <= last; }), mFolded.end());
    for (S32& start : mFolded)
    {
        if (start > last)
        {
            start += delta;
        }
    }
    mRegionsValid = false;
    if (!mFolded.empty())
    {
        applyFolds();
    }
}

void ALCodeEditor::setMark(S32 line, Mark mark)
{
    if (line >= 0 && line < static_cast<S32>(mMarks.size()))
    {
        mMarks[line] = mark;
    }
}

ALCodeEditor::Mark ALCodeEditor::markAt(S32 line) const
{
    return (line >= 0 && line < static_cast<S32>(mMarks.size())) ? mMarks[line] : Mark::None;
}

void ALCodeEditor::clearMarks()
{
    std::fill(mMarks.begin(), mMarks.end(), Mark::None);
}

void ALCodeEditor::setDecorations(std::vector<Decoration> decorations)
{
    mDecorations = std::move(decorations);
}

void ALCodeEditor::setHighlights(std::vector<ALTextRange> ranges)
{
    mHighlights = std::move(ranges);
    for (ALTextRange& range : mHighlights)
    {
        range = range.normalised();
    }
}

bool ALCodeEditor::highlighted(const ALTextPos& at) const
{
    for (const ALTextRange& range : mHighlights)
    {
        if (range.begin <= at && at <= range.end)
        {
            return true;
        }
    }
    return false;
}

// --- brackets ----------------------------------------------------------------

bool ALCodeEditor::matchingBrackets(ALTextPos& open, ALTextPos& close)
{
    const ALTextPos at = caret();
    const std::string& line = document().line(at.line);
    // The bracket just before the caret, else the one under it.
    ALTextPos from = at;
    char      c = 0, partner = 0;
    bool      opens = false;
    if (at.column > 0 && bracketOf(line[at.column - 1], partner, opens))
    {
        from = ALTextPos(at.line, at.column - 1);
        c    = line[at.column - 1];
    }
    else if (at.column < static_cast<S32>(line.size()) && bracketOf(line[at.column], partner, opens))
    {
        c = line[at.column];
    }
    else
    {
        return false;
    }

    // Whether a byte of a line is inside a string or a comment.
    auto quietAt = [&](S32 l, S32 column) {
        for (const ALSyntaxToken& token : highlighter().tokens(l))
        {
            if (token.begin <= column && column < token.end)
            {
                return quiet(token.kind);
            }
        }
        return false;
    };
    if (quietAt(from.line, from.column))
    {
        return false;
    }

    S32 depth = 0;
    if (opens)
    {
        for (S32 l = from.line; l < document().lineCount(); ++l)
        {
            const std::string& text = document().line(l);
            for (S32 i = (l == from.line ? from.column : 0); i < static_cast<S32>(text.size()); ++i)
            {
                if ((text[i] == c || text[i] == partner) && !quietAt(l, i))
                {
                    depth += (text[i] == c) ? 1 : -1;
                    if (depth == 0)
                    {
                        open  = from;
                        close = ALTextPos(l, i);
                        return true;
                    }
                }
            }
        }
    }
    else
    {
        for (S32 l = from.line; l >= 0; --l)
        {
            const std::string& text = document().line(l);
            for (S32 i = (l == from.line ? from.column : static_cast<S32>(text.size()) - 1); i >= 0; --i)
            {
                if ((text[i] == c || text[i] == partner) && !quietAt(l, i))
                {
                    depth += (text[i] == c) ? 1 : -1;
                    if (depth == 0)
                    {
                        open  = ALTextPos(l, i);
                        close = from;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// --- colours -----------------------------------------------------------------

namespace
{
    // So far from the background towards the ink, opaque.
    LLColor4 towards(const LLColor4& from, const LLColor4& to, F32 amount)
    {
        LLColor4 mixed = lerp(from, to, amount);
        mixed.mV[VALPHA] = 1.f;
        return mixed;
    }
}

LLColor4 ALCodeEditor::gutterColor() const
{
    return mGutterColorSet ? mGutterColor.get() : towards(backgroundColor(), textColor(), 0.06f);
}

LLColor4 ALCodeEditor::lineNumberColor() const
{
    return mLineNumberColorSet ? mLineNumberColor.get() : towards(backgroundColor(), textColor(), 0.5f);
}

LLColor4 ALCodeEditor::currentLineColor() const
{
    if (mCurrentLineColorSet)
    {
        return mCurrentLineColor.get();
    }
    LLColor4 wash = textColor();
    wash.mV[VALPHA] = 0.06f;
    return wash;
}

LLColor4 ALCodeEditor::foldColor() const
{
    return mFoldColorSet ? mFoldColor.get() : towards(backgroundColor(), textColor(), 0.6f);
}

LLColor4 ALCodeEditor::changedColor() const
{
    return mChangedColorSet ? mChangedColor.get() : towards(backgroundColor(), LLColor4(0.35f, 0.6f, 0.95f, 1.f), 0.9f);
}

bool ALCodeEditor::lineChanged(S32 line) const
{
    return line >= 0 && line < static_cast<S32>(mChanged.size()) && mChanged[static_cast<size_t>(line)] != 0;
}

void ALCodeEditor::resetDirty()
{
    ALTextView::resetDirty();
    std::fill(mChanged.begin(), mChanged.end(), 0);
}

LLColor4 ALCodeEditor::highlightColor() const
{
    if (mHighlightColorSet)
    {
        return mHighlightColor.get();
    }
    LLColor4 wash = textColor();
    wash.mV[VALPHA] *= 0.18f;
    return wash;
}

// --- the gutter --------------------------------------------------------------

void ALCodeEditor::setShowLineNumbers(bool show)
{
    mShowLineNumbers = show;
    reshape(getRect().getWidth(), getRect().getHeight());
}

void ALCodeEditor::setShowFoldMarkers(bool show)
{
    mShowFoldMarkers = show;
    reshape(getRect().getWidth(), getRect().getHeight());
}

S32 ALCodeEditor::gutterWidth() const
{
    if (!getFont())
    {
        return 0;
    }
    S32 width = 0;
    if (mShowLineNumbers)
    {
        S32 digits = 1;
        for (S32 n = document().lineCount(); n >= 10; n /= 10)
        {
            ++digits;
        }
        digits = llmax(digits, 3);
        width += MARK_INSET + MARK_SIZE + GUTTER_PAD + digits * getFont()->getWidth("8") + GUTTER_PAD;
    }
    if (mShowFoldMarkers)
    {
        width += FOLD_COLUMN;
    }
    return width;
}

void ALCodeEditor::goToLine(S32 line)
{
    setCaret(document().lineStart(line));
}

void ALCodeEditor::drawGutter(const LLRect& text, F32 alpha)
{
    const S32 width = gutterWidth();
    if (width <= 0)
    {
        return;
    }
    const LLRect local = getLocalRect();
    const LLRect gutter(leftEdge(), local.mTop, leftEdge() + width, local.mBottom);
    gl_rect_2d(gutter, gutterColor() % alpha);

    const LLFontGL* font   = getFont();
    const S32       row_h  = layout().rowHeight();
    const S32       ascent = llround(font->getAscenderHeight());
    const S32       numbers_right = gutter.mRight - (mShowFoldMarkers ? FOLD_COLUMN : 0) - GUTTER_PAD;
    const LLColor4  ink     = lineNumberColor() % alpha;
    const LLColor4  lit     = textColor() % alpha;
    const LLColor4  fold    = foldColor() % alpha;
    const LLColor4  changed = changedColor() % alpha;
    const S32       caret_line = caret().line;
    if (mShowFoldMarkers)
    {
        ensureRegions();
    }
    // The block under the mouse in the fold column shows how far it
    // runs: a line down the column from its first row to its last.
    const FoldRegion* shown = mShowFoldMarkers && mGutterHover && mGutterHoverLine >= 0 ? regionStartingAt(mGutterHoverLine) : nullptr;
    S32               guide_top = 0, guide_bottom = 0;
    forEachVisibleRow(text, [&](S32 line, S32 row, S32 screen_top) {
        if (lineChanged(line))
        {
            gl_rect_2d(gutter.mLeft, screen_top, gutter.mLeft + 2, screen_top - row_h, changed);
        }
        if (shown && line >= shown->start && line <= shown->end)
        {
            guide_top    = guide_top == 0 ? screen_top : guide_top;
            guide_bottom = screen_top - row_h;
        }
        if (row != 0)
        {
            return;
        }
        if (mShowLineNumbers)
        {
            // The caret's line in the text's own ink, the rest quieter;
            // counted from the caret's line where that is asked for.
            const S32 shown = mRelativeLineNumbers && line != caret_line ? std::abs(line - caret_line) : line + 1;
            font->renderUTF8(std::to_string(shown), 0, static_cast<F32>(numbers_right), static_cast<F32>(screen_top - ascent),
                             line == caret_line ? lit : ink, LLFontGL::RIGHT, LLFontGL::BASELINE);
            const Mark mark = markAt(line);
            if (mark != Mark::None)
            {
                const S32 y = screen_top - (row_h - MARK_SIZE) / 2;
                gl_rect_2d(gutter.mLeft + MARK_INSET, y, gutter.mLeft + MARK_INSET + MARK_SIZE, y - MARK_SIZE,
                           mMarkColors[static_cast<size_t>(mark)].get() % alpha);
            }
        }
        // A marker at a block's first line: pointing right at a folded
        // one, always; pointing down at an open one, while the mouse is
        // over the gutter, so that the gutter is quiet otherwise.
        if (mShowFoldMarkers && regionStartingAt(line) && (isFolded(line) || mGutterHover))
        {
            const S32 cx = gutter.mRight - FOLD_COLUMN / 2;
            const S32 cy = screen_top - row_h / 2;
            const S32 h  = FOLD_MARKER / 2;
            if (isFolded(line))
            {
                gl_triangle_2d(cx - h + 1, cy + h, cx - h + 1, cy - h, cx + h - 1, cy, fold, true);
            }
            else
            {
                gl_triangle_2d(cx - h, cy + h - 1, cx + h, cy + h - 1, cx, cy - h + 1, fold, true);
            }
        }
    });
    if (shown && guide_top != 0 && !isFolded(shown->start))
    {
        const S32 cx = gutter.mRight - FOLD_COLUMN / 2;
        gl_rect_2d(cx, guide_top - row_h + FOLD_MARKER / 2, cx + 1, guide_bottom, fold);
    }
    // The headers pinned over the text have their numbers pinned over
    // the gutter, on the same band.
    const std::vector<S32> pinned = stickyLines();
    if (!pinned.empty() && mShowLineNumbers)
    {
        const S32    rows = static_cast<S32>(pinned.size());
        const LLRect band(gutter.mLeft, text.mTop, gutter.mRight, text.mTop - rows * row_h);
        gl_rect_2d(band, towards(backgroundColor(), textColor(), 0.06f) % alpha, true);
        gl_rect_2d(band.mLeft, band.mBottom, band.mRight, band.mBottom - 1, fold % 0.6f, true);
        for (S32 i = 0; i < rows; ++i)
        {
            const S32 line  = pinned[static_cast<size_t>(i)];
            const S32 shown_number = mRelativeLineNumbers && line != caret_line ? std::abs(line - caret_line) : line + 1;
            font->renderUTF8(std::to_string(shown_number), 0, static_cast<F32>(numbers_right), static_cast<F32>(text.mTop - i * row_h - ascent), ink,
                             LLFontGL::RIGHT, LLFontGL::BASELINE);
        }
    }
}

// --- brackets by depth --------------------------------------------------------------

namespace
{
    // Whether a byte is a bracket, and which way it faces.
    S32 bracketDelta(char c)
    {
        switch (c)
        {
            case '(': case '[': case '{': return 1;
            case ')': case ']': case '}': return -1;
            default: return 0;
        }
    }
}

S32 ALCodeEditor::bracketDepthBefore(S32 line)
{
    const S32 count = document().lineCount();
    if (line <= 0 || line > count)
    {
        return 0;
    }
    mDepthBefore.resize(static_cast<size_t>(count) + 1, 0);
    if (mDepthValid == 0)
    {
        mDepthBefore[0] = 0;
        mDepthValid     = 1;
    }
    // Carried on from the last line known, over what the grammar calls
    // punctuation or an operator; a bracket in a string or a comment is
    // none.
    for (S32 l = mDepthValid - 1; l < line; ++l)
    {
        S32                               depth  = mDepthBefore[static_cast<size_t>(l)];
        const std::string&                text   = document().line(l);
        const std::vector<ALSyntaxToken>& tokens = highlighter().tokens(l);
        for (const ALSyntaxToken& token : tokens)
        {
            if (token.kind != ALSyntaxKind::Punctuation && token.kind != ALSyntaxKind::Operator)
            {
                continue;
            }
            for (S32 b = token.begin; b < token.end && b < static_cast<S32>(text.size()); ++b)
            {
                depth = llmax(0, depth + bracketDelta(text[static_cast<size_t>(b)]));
            }
        }
        mDepthBefore[static_cast<size_t>(l) + 1] = depth;
        mDepthValid                             = l + 2;
    }
    return mDepthBefore[static_cast<size_t>(line)];
}

void ALCodeEditor::tintRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha, std::vector<LLColor4U>& colors)
{
    // What the analyzer knows a stretch to be, over the grammar's colour
    // for it; a comment or a string keeps its own, since a name inside
    // one is not that name.
    if (!mSemantics.empty())
    {
        auto first = std::lower_bound(mSemantics.begin(), mSemantics.end(), line, [](const SemanticToken& t, S32 l) { return t.range.end.line < l; });
        if (first != mSemantics.end() && first->range.begin.line <= line)
        {
            const std::vector<ALSyntaxToken>& grammar = highlighter().tokens(line);
            auto                              literal = [&](S32 cluster) {
                for (const ALSyntaxToken& g : grammar)
                {
                    if (g.begin <= cluster && cluster < g.end)
                    {
                        return g.kind == ALSyntaxKind::Comment || g.kind == ALSyntaxKind::DocComment || g.kind == ALSyntaxKind::String
                               || g.kind == ALSyntaxKind::Escape || g.kind == ALSyntaxKind::Preprocessor;
                    }
                }
                return false;
            };
            auto token = first;
            for (size_t k = 0; k < colors.size(); ++k)
            {
                const ALTextLayout::Glyph& glyph = laid.glyphs[row.glyphBegin + k];
                if (glyph.inlay >= 0)
                {
                    continue;
                }
                const ALTextPos at(line, glyph.cluster);
                while (token != mSemantics.end() && token->range.begin.line <= line && token->range.end <= at)
                {
                    ++token;
                }
                if (token == mSemantics.end() || token->range.begin.line > line || !(token->range.begin <= at && at < token->range.end))
                {
                    continue;
                }
                if (literal(glyph.cluster))
                {
                    continue;
                }
                colors[k] = LLColor4U(colorForKind(token->kind) % alpha);
            }
        }
    }
    if (!mColorBrackets || !mBracketColorsSet)
    {
        return;
    }
    // The depth at each bracket of the line, then the row's glyphs that
    // are brackets in their depth's colour.
    const std::string&                text   = document().line(line);
    const std::vector<ALSyntaxToken>& tokens = highlighter().tokens(line);
    S32                               depth  = bracketDepthBefore(line);
    std::vector<std::pair<S32, S32>>  at;
    for (const ALSyntaxToken& token : tokens)
    {
        if (token.kind != ALSyntaxKind::Punctuation && token.kind != ALSyntaxKind::Operator)
        {
            continue;
        }
        for (S32 b = token.begin; b < token.end && b < static_cast<S32>(text.size()); ++b)
        {
            const S32 delta = bracketDelta(text[static_cast<size_t>(b)]);
            if (delta > 0)
            {
                at.emplace_back(b, depth);
                ++depth;
            }
            else if (delta < 0)
            {
                depth = llmax(0, depth - 1);
                at.emplace_back(b, depth);
            }
        }
    }
    if (at.empty())
    {
        return;
    }
    size_t next = 0;
    for (size_t k = 0; k < colors.size(); ++k)
    {
        const S32 cluster = laid.glyphs[row.glyphBegin + k].cluster;
        while (next < at.size() && at[next].first < cluster)
        {
            ++next;
        }
        if (next < at.size() && at[next].first == cluster)
        {
            colors[k] = LLColor4U(mBracketColors[static_cast<size_t>(at[next].second % 3)].get() % alpha);
        }
    }
}

// --- what the analyzer knows ----------------------------------------------------------

void ALCodeEditor::setSemanticTokens(std::vector<SemanticToken> tokens)
{
    std::stable_sort(tokens.begin(), tokens.end(), [](const SemanticToken& a, const SemanticToken& b) { return a.range.begin < b.range.begin; });
    mSemantics = std::move(tokens);
}

void ALCodeEditor::setInlayHints(std::vector<InlayHint> hints)
{
    std::stable_sort(hints.begin(), hints.end(), [](const InlayHint& a, const InlayHint& b) { return a.at < b.at; });
    // The lines whose inlays are not what they were are laid out again;
    // the rest keep their layout.
    auto same_line = [](const std::vector<InlayHint>& list, size_t& i, S32 line, std::vector<const InlayHint*>& out) {
        out.clear();
        while (i < list.size() && list[i].at.line == line)
        {
            out.push_back(&list[i++]);
        }
    };
    size_t                       was = 0, now = 0;
    std::vector<const InlayHint*> old_line, new_line;
    while (was < mInlays.size() || now < hints.size())
    {
        const S32 line = llmin(was < mInlays.size() ? mInlays[was].at.line : S32_MAX, now < hints.size() ? hints[now].at.line : S32_MAX);
        same_line(mInlays, was, line, old_line);
        same_line(hints, now, line, new_line);
        bool changed = old_line.size() != new_line.size();
        for (size_t k = 0; !changed && k < old_line.size(); ++k)
        {
            changed = old_line[k]->at != new_line[k]->at || old_line[k]->text != new_line[k]->text || old_line[k]->before != new_line[k]->before;
        }
        if (changed)
        {
            layout().invalidateLine(line);
        }
    }
    mInlays = std::move(hints);
}

namespace
{
    // Around the word of an inlay, in pixels: the pill's inset from the
    // text either side, and the room inside it.
    constexpr F32 INLAY_GAP = 2.f;
    constexpr F32 INLAY_PAD = 3.f;
}

F32 ALCodeEditor::inlayWidth(const InlayHint& hint) const
{
    const LLFontGL* font = getFont();
    if (!font || hint.text.empty())
    {
        return 0.f;
    }
    return font->getWidthF32(hint.text) + 2.f * (INLAY_GAP + INLAY_PAD);
}

void ALCodeEditor::provideInlays(S32 line, std::vector<ALTextLayout::Inlay>& out) const
{
    auto first = std::lower_bound(mInlays.begin(), mInlays.end(), line, [](const InlayHint& h, S32 l) { return h.at.line < l; });
    for (auto it = first; it != mInlays.end() && it->at.line == line; ++it)
    {
        ALTextLayout::Inlay inlay;
        inlay.column = it->at.column;
        inlay.width  = inlayWidth(*it);
        inlay.before = it->before;
        inlay.id     = static_cast<S32>(it - mInlays.begin());
        out.push_back(inlay);
    }
}

// --- headers pinned at the top ------------------------------------------------------

std::vector<S32> ALCodeEditor::stickyLines()
{
    std::vector<S32> lines;
    if (!mStickyHeaders)
    {
        return lines;
    }
    // The first row on screen, and the blocks around its line that start
    // above it: their first lines, outermost first, the innermost few.
    const S32 top_line = posAtLocal(textRect().mLeft, textRect().mTop - 1, false).line;
    ensureRegions();
    for (const FoldRegion& region : mRegions)
    {
        if (region.start < top_line && region.end >= top_line && !isFolded(region.start))
        {
            lines.push_back(region.start);
        }
    }
    // Nested blocks start later: the list is already outer to inner; the
    // innermost three are the ones worth the room.
    if (lines.size() > 3)
    {
        lines.erase(lines.begin(), lines.end() - 3);
    }
    return lines;
}

S32 ALCodeEditor::stickyRows()
{
    return static_cast<S32>(stickyLines().size());
}

void ALCodeEditor::drawAfterRows(const LLRect& text)
{
    const std::vector<S32> lines = stickyLines();
    if (lines.empty())
    {
        return;
    }
    const F32 alpha = getDrawContext().mAlpha;
    const S32 row_h = layout().rowHeight();
    const S32 rows  = static_cast<S32>(lines.size());
    // On the gutter's ground, a shade off the text's, with a line under
    // the last; each header's first row, at the left as the text is.
    const LLRect band(text.mLeft, text.mTop, text.mRight, text.mTop - rows * row_h);
    gl_rect_2d(band, towards(backgroundColor(), textColor(), 0.06f) % alpha, true);
    gl_rect_2d(band.mLeft, band.mBottom, band.mRight, band.mBottom - 1, foldColor() % (0.6f * alpha), true);
    const F32 left = static_cast<F32>(text.mLeft) - scrollX();
    for (S32 i = 0; i < rows; ++i)
    {
        drawRowAt(lines[static_cast<size_t>(i)], 0, left, text.mTop - i * row_h, alpha);
    }
}

void ALCodeEditor::drawBeforeRows(const LLRect& text)
{
    const F32 alpha = getDrawContext().mAlpha;
    if (mHighlightCurrentLine && keyboardOnText() && !hasSelection())
    {
        S32 row;
        layout().xOf(caret().line, caret().column, &row);
        const S32 top = screenTopOf(text, caret().line, row);
        gl_rect_2d(text.mLeft, top, text.mRight, top - layout().rowHeight(), currentLineColor() % alpha);
    }
    drawGutter(text, alpha);
}

// --- over the rows -------------------------------------------------------------

LLRect ALCodeEditor::foldBoxOf(S32 line, const LLRect& text)
{
    if (!isFolded(line))
    {
        return LLRect();
    }
    const ALTextLayout::Line& laid = layout().line(line);
    if (laid.rows.empty())
    {
        return LLRect();
    }
    const ALTextLayout::Row& last  = laid.rows.back();
    const S32                row   = static_cast<S32>(laid.rows.size()) - 1;
    const S32                top   = screenTopOf(text, line, row);
    const S32                row_h = layout().rowHeight();
    const S32                x0    = static_cast<S32>(static_cast<F32>(text.mLeft) - scrollX() + last.xStart + last.width) + FOLD_BOX_GAP;
    const S32                w     = getFont()->getWidth(foldBoxText(line)) + 8;
    return LLRect(x0, top - 1, x0 + w, top - row_h + 1);
}

// What a folded block's box says: how many lines are folded away.
std::string ALCodeEditor::foldBoxText(S32 line)
{
    const FoldRegion* region = regionStartingAt(line);
    const S32         hidden = region ? region->end - region->start : 0;
    return hidden > 0 ? "... " + std::to_string(hidden) : std::string("...");
}

S32 ALCodeEditor::indentOf(S32 line) const
{
    const S32 count = document().lineCount();
    for (S32 l = line; l < count && l < line + 200; ++l)
    {
        const std::string& text = document().line(l);
        S32                columns = 0;
        bool               blank   = true;
        for (char c : text)
        {
            if (c == ' ')
            {
                ++columns;
            }
            else if (c == '\t')
            {
                columns += getTabWidth() - columns % getTabWidth();
            }
            else
            {
                blank = false;
                break;
            }
        }
        if (!blank)
        {
            return columns;
        }
    }
    return 0;
}

void ALCodeEditor::drawRowExtras(S32 line, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha)
{
    const S32 row_h = layout().rowHeight();
    if (mShowIndentGuides && row == 0)
    {
        // One faint line per level within the indentation, at the tab
        // stops; a blank line takes the next line's, so a block's guides
        // run unbroken through it.
        const S32 indent = indentOf(line);
        const S32 tab    = getTabWidth();
        if (indent >= tab && tab > 0)
        {
            const LLColor4 guide = foldColor() % (0.35f * alpha);
            const F32      space = layout().columnWidth();
            for (S32 column = tab; column < indent; column += tab)
            {
                const S32 x = static_cast<S32>(left + space * static_cast<F32>(column));
                if (x >= text.mLeft && x < text.mRight)
                {
                    gl_rect_2d(x, screen_top, x + 1, screen_top - row_h, guide);
                }
            }
        }
    }
    if (!mHighlights.empty())
    {
        const LLColor4 wash = highlightColor() % alpha;
        for (const ALTextRange& range : mHighlights)
        {
            F32 x0, x1;
            if (spanOnRow(line, row, range, x0, x1))
            {
                gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, wash);
            }
        }
    }
    for (S32 i = 0; i < static_cast<S32>(mPlaceholders.size()); ++i)
    {
        F32 x0, x1;
        if (spanOnRow(line, row, mPlaceholders[i], x0, x1))
        {
            gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, highlightColor() % alpha);
            if (i == mPlaceholderAt)
            {
                gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, mBracketMatchColor.get() % alpha, false);
            }
        }
    }
    // The words beside the text, each in a pill the layout made room for.
    {
        const ALTextLayout::Line& laid = layout().line(line);
        if (row >= 0 && row < static_cast<S32>(laid.rows.size()))
        {
            const ALTextLayout::Row& r    = laid.rows[static_cast<size_t>(row)];
            const LLFontGL*          font = getFont();
            for (size_t k = r.glyphBegin; k < r.glyphEnd && font; ++k)
            {
                const ALTextLayout::Glyph& glyph = laid.glyphs[k];
                if (glyph.inlay < 0 || glyph.inlay >= static_cast<S32>(mInlays.size()))
                {
                    continue;
                }
                const InlayHint& hint = mInlays[static_cast<size_t>(glyph.inlay)];
                const F32        x0   = left + glyph.pen - r.xStart + INLAY_GAP;
                const F32        x1   = left + glyph.pen - r.xStart + glyph.advance - INLAY_GAP;
                if (x1 <= static_cast<F32>(text.mLeft) || x0 >= static_cast<F32>(text.mRight))
                {
                    continue;
                }
                const LLColor4 ink    = textColor();
                const LLColor4 ground = backgroundColor();
                const LLColor4 pill   = lerp(ground, ink, 0.12f) % alpha;
                const LLColor4 word   = lerp(ground, ink, 0.65f) % alpha;
                gl_rect_2d(static_cast<S32>(x0), screen_top - 1, static_cast<S32>(x1), screen_top - row_h + 1, pill);
                font->renderUTF8(hint.text, 0, x0 + INLAY_PAD, static_cast<F32>(screen_top - llround(font->getAscenderHeight())), word, LLFontGL::LEFT,
                                 LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
            }
        }
    }
    // A line through what is deprecated.
    if (!mSemantics.empty())
    {
        auto first = std::lower_bound(mSemantics.begin(), mSemantics.end(), line, [](const SemanticToken& t, S32 l) { return t.range.end.line < l; });
        for (auto it = first; it != mSemantics.end() && it->range.begin.line <= line; ++it)
        {
            F32 x0, x1;
            if (it->strike && spanOnRow(line, row, it->range, x0, x1))
            {
                const S32 y = screen_top - row_h / 2;
                gl_rect_2d(static_cast<S32>(left + x0), y + 1, static_cast<S32>(left + x1), y, colorForKind(it->kind) % alpha);
            }
        }
    }
    for (const Decoration& d : mDecorations)
    {
        F32 x0, x1;
        if (!spanOnRow(line, row, d.range, x0, x1))
        {
            continue;
        }
        if (d.style == Decoration::Style::Background)
        {
            gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, d.color % alpha);
        }
        else
        {
            drawSquiggle(left + x0, left + x1, screen_top - row_h + 2, d.color % alpha);
        }
    }
    if (mMatchBrackets && keyboardOnText())
    {
        ALTextPos open, close;
        if (matchingBrackets(open, close))
        {
            for (const ALTextPos& at : { open, close })
            {
                F32 x0, x1;
                if (spanOnRow(line, row, ALTextRange(at, document().nextCluster(at)), x0, x1))
                {
                    gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, mBracketMatchColor.get() % alpha, false);
                }
            }
        }
    }
    // A folded block says so after its first line.
    if (row + 1 == layout().rowCount(line) && isFolded(line))
    {
        const LLRect box = foldBoxOf(line, text);
        if (box.notEmpty())
        {
            const LLColor4 ink = foldColor() % alpha;
            gl_rect_2d(box, ink, false);
            getFont()->renderUTF8(foldBoxText(line), 0, static_cast<F32>(box.mLeft + 4), static_cast<F32>(screen_top - llround(getFont()->getAscenderHeight())), ink, LLFontGL::LEFT, LLFontGL::BASELINE);
        }
    }
}

// --- folding -----------------------------------------------------------------

void ALCodeEditor::ensureRegions()
{
    if (mRegionsValid && mRegionsVersion == document().version())
    {
        return;
    }
    mRegions.clear();
    const S32        count = document().lineCount();
    const S32        tab   = getTabWidth();
    std::vector<S32> indent(count, -1);
    for (S32 l = 0; l < count; ++l)
    {
        const std::string& line  = document().line(l);
        S32                n     = 0;
        bool               blank = true;
        for (char c : line)
        {
            if (c == ' ')
            {
                ++n;
            }
            else if (c == '\t')
            {
                n = (n / tab + 1) * tab;
            }
            else if (c != '\r')
            {
                blank = false;
                break;
            }
        }
        indent[l] = blank ? -1 : n;
    }
    // A block is a line and the deeper lines after it, with a line of
    // nothing going with whichever side keeps the block whole, and the
    // closer on the line after -- a brace, an `end` -- taken as part of it.
    std::vector<S32> end_of(count, -1);
    for (S32 l = 0; l < count; ++l)
    {
        if (indent[l] < 0)
        {
            continue;
        }
        S32 end = l;
        S32 k   = l + 1;
        for (; k < count; ++k)
        {
            if (indent[k] < 0)
            {
                continue;
            }
            if (indent[k] > indent[l])
            {
                end = k;
            }
            else
            {
                break;
            }
        }
        if (end == l)
        {
            continue;
        }
        if (k < count && indent[k] == indent[l] && closesBlock(trimmed(document().line(k))))
        {
            end = k;
        }
        end_of[l] = end;
    }
    // A brace on a line of its own is its header's: `default` and the
    // `{` under it fold as one block, from the header.
    for (S32 l = 0; l < count; ++l)
    {
        if (end_of[l] < 0 || trimmed(document().line(l)) != "{")
        {
            continue;
        }
        S32 header = l - 1;
        while (header >= 0 && indent[header] < 0)
        {
            --header;
        }
        if (header >= 0 && indent[header] == indent[l] && end_of[header] < 0)
        {
            end_of[header] = end_of[l];
            end_of[l]      = -1;
        }
    }
    for (S32 l = 0; l < count; ++l)
    {
        if (end_of[l] > l)
        {
            mRegions.push_back(FoldRegion{ l, end_of[l] });
        }
    }
    mRegionsVersion = document().version();
    mRegionsValid   = true;
}

const std::vector<ALCodeEditor::FoldRegion>& ALCodeEditor::foldRegions()
{
    ensureRegions();
    return mRegions;
}

const ALCodeEditor::FoldRegion* ALCodeEditor::regionStartingAt(S32 line)
{
    ensureRegions();
    const auto it = std::lower_bound(mRegions.begin(), mRegions.end(), line,
                                     [](const FoldRegion& region, S32 l) { return region.start < l; });
    return (it != mRegions.end() && it->start == line) ? &*it : nullptr;
}

const ALCodeEditor::FoldRegion* ALCodeEditor::regionAround(S32 line)
{
    ensureRegions();
    // The innermost: of those that hold the line, the one that starts last.
    const FoldRegion* found = nullptr;
    for (const FoldRegion& region : mRegions)
    {
        if (region.start >= line)
        {
            break;
        }
        if (region.end >= line)
        {
            found = &region;
        }
    }
    return found;
}

bool ALCodeEditor::isFolded(S32 line) const
{
    return std::binary_search(mFolded.begin(), mFolded.end(), line);
}

void ALCodeEditor::applyFolds()
{
    ensureRegions();
    layout().setHidden(0, document().lineCount() - 1, false);
    // A fold whose block is gone is gone with it.
    mFolded.erase(std::remove_if(mFolded.begin(), mFolded.end(), [&](S32 start) { return regionStartingAt(start) == nullptr; }), mFolded.end());
    for (S32 start : mFolded)
    {
        const FoldRegion* region = regionStartingAt(start);
        layout().setHidden(region->start + 1, region->end, true);
    }
}

bool ALCodeEditor::foldAt(S32 line)
{
    const FoldRegion* region = regionStartingAt(line);
    if (!region)
    {
        region = regionAround(line);
    }
    if (!region || isFolded(region->start))
    {
        return false;
    }
    const FoldRegion chosen = *region;
    mFolded.insert(std::upper_bound(mFolded.begin(), mFolded.end(), chosen.start), chosen.start);
    // The caret cannot stay in what is folded away.
    const ALTextRange sel = selection().normalised();
    if ((sel.begin.line > chosen.start && sel.begin.line <= chosen.end) || (sel.end.line > chosen.start && sel.end.line <= chosen.end))
    {
        setCaret(document().lineEnd(chosen.start));
    }
    applyFolds();
    return true;
}

bool ALCodeEditor::unfoldAt(S32 line)
{
    S32 start = -1;
    if (isFolded(line))
    {
        start = line;
    }
    else
    {
        // The innermost folded block around the line -- which, since a
        // folded block's lines are hidden, is the one whose first line
        // this is a descendant of.
        for (S32 folded : mFolded)
        {
            const FoldRegion* region = regionStartingAt(folded);
            if (region && region->start < line && line <= region->end)
            {
                start = folded;
            }
        }
    }
    if (start < 0)
    {
        return false;
    }
    mFolded.erase(std::remove(mFolded.begin(), mFolded.end(), start), mFolded.end());
    applyFolds();
    return true;
}

void ALCodeEditor::foldAll()
{
    ensureRegions();
    mFolded.clear();
    for (const FoldRegion& region : mRegions)
    {
        mFolded.push_back(region.start);
    }
    applyFolds();
    if (layout().hidden(caret().line))
    {
        setCaret(document().lineEnd(layout().visibleFrom(caret().line, -1)));
    }
}

void ALCodeEditor::unfoldAll()
{
    mFolded.clear();
    applyFolds();
}

void ALCodeEditor::revealLine(S32 line)
{
    // Every folded block the line is inside opens.
    bool changed = false;
    for (S32 i = static_cast<S32>(mFolded.size()) - 1; i >= 0; --i)
    {
        const FoldRegion* region = regionStartingAt(mFolded[i]);
        if (!region || (region->start < line && line <= region->end))
        {
            mFolded.erase(mFolded.begin() + i);
            changed = true;
        }
    }
    if (changed)
    {
        applyFolds();
    }
    else
    {
        ALTextView::revealLine(line);
    }
}

bool ALCodeEditor::performFold(ALEditorCommand command)
{
    switch (command)
    {
        case ALEditorCommand::Fold:
            return foldAt(caret().line);
        case ALEditorCommand::Unfold:
            return unfoldAt(caret().line);
        case ALEditorCommand::FoldAll:
            foldAll();
            return true;
        case ALEditorCommand::UnfoldAll:
            unfoldAll();
            return true;
        default:
            return false;
    }
}

bool ALCodeEditor::canFold(ALEditorCommand command) const
{
    ALCodeEditor* self = const_cast<ALCodeEditor*>(this);
    switch (command)
    {
        case ALEditorCommand::Fold:
        {
            const FoldRegion* region = self->regionStartingAt(caret().line);
            if (!region)
            {
                region = self->regionAround(caret().line);
            }
            return region && !isFolded(region->start);
        }
        case ALEditorCommand::Unfold:
            return !mFolded.empty();
        case ALEditorCommand::FoldAll:
            return mFolded.size() < self->foldRegions().size();
        case ALEditorCommand::UnfoldAll:
            return !mFolded.empty();
        default:
            return false;
    }
}

// --- completion --------------------------------------------------------------

bool ALCodeEditor::completionOpen() const
{
    return mCompletionList && mCompletionList->getVisible();
}

void ALCodeEditor::hideCompletionList()
{
    if (mCompletionList)
    {
        mCompletionList->setVisible(false);
        mCompletionList->setChoices({});
    }
    mCompletions.clear();
}

void ALCodeEditor::closeCompletion()
{
    hideCompletionList();
    mCompletionAsked = ALTextPos(-1, -1);
    mSupplied.clear();
}

S32 ALCodeEditor::chosenCompletion() const
{
    return completionOpen() ? mCompletionList->chosen() : -1;
}

void ALCodeEditor::vocabularyCompletions(std::string_view prefix, std::vector<Completion>& out)
{
    std::vector<std::pair<std::string, std::string>> words;
    if (highlighter().grammar())
    {
        highlighter().grammar()->collectWords(prefix, words);
    }
    highlighter().words().collect(prefix, words);
    for (auto& [word, table] : words)
    {
        Completion c;
        c.text   = word;
        c.kind   = kindOfTable(table);
        c.detail = alSyntaxKindName(c.kind);
        out.push_back(std::move(c));
    }
}

void ALCodeEditor::documentCompletions(const ALTextPos& at, std::string_view prefix, std::vector<Completion>& out)
{
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> seen;
    for (const Completion& c : out)
    {
        seen.insert(c.text);
    }
    auto begins = [&](std::string_view word) {
        if (word.size() < prefix.size())
        {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); ++i)
        {
            if (LLStringOps::toLower(word[i]) != LLStringOps::toLower(prefix[i]))
            {
                return false;
            }
        }
        return true;
    };
    // The document's own words, other than the one being typed.
    const S32 count = document().lineCount();
    for (S32 l = 0; l < count && out.size() < COMPLETION_CAP; ++l)
    {
        const std::string& line = document().line(l);
        size_t             i    = 0;
        while (i < line.size())
        {
            if (!identifierByte(line[i]))
            {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < line.size() && identifierByte(line[j]))
            {
                ++j;
            }
            const bool typing = (l == at.line && static_cast<S32>(j) == at.column);
            if (!typing && (line[i] < '0' || line[i] > '9'))
            {
                std::string_view word(line.data() + i, j - i);
                if (word.size() > prefix.size() && begins(word) && seen.insert(std::string(word)).second)
                {
                    Completion c;
                    c.text = std::string(word);
                    out.push_back(std::move(c));
                }
            }
            i = j;
        }
    }
}

void ALCodeEditor::refreshCompletion()
{
    const std::string prefix = wordBeforeCaret();
    const ALTextPos   at     = caret();
    const ALTextPos   start(at.line, at.column - static_cast<S32>(prefix.size()));
    // After `ll.` the members of `ll` are wanted: the head is put before
    // the prefix for whoever answers by whole names, and taken off what
    // they answer.
    std::string head;
    if (start.column >= 2 && document().line(start.line)[start.column - 1] == '.')
    {
        const ALTextRange before = identifierAt(ALTextPos(start.line, start.column - 2));
        if (!before.empty() && before.end.column == start.column - 1)
        {
            head = document().text(before);
        }
    }
    if ((prefix.empty() && head.empty()) || hasSelection())
    {
        closeCompletion();
        return;
    }
    const bool fresh = start != mCompletionAsked;
    if (fresh)
    {
        mCompletionAsked = start;
        mSupplied.clear();
    }
    mCompletionHead = head;
    mCompletions.clear();
    const std::string asked = head.empty() ? prefix : head + "." + prefix;
    if (mProvider)
    {
        mProvider(at, asked, mCompletions);
    }
    else
    {
        vocabularyCompletions(asked, mCompletions);
    }
    if (!head.empty())
    {
        const std::string dotted = head + ".";
        for (Completion& c : mCompletions)
        {
            if (c.text.compare(0, dotted.size(), dotted) == 0)
            {
                c.text.erase(0, dotted.size());
            }
        }
    }
    // What was answered about this word, narrowed to the prefix as typed
    // now; what was known already keeps its place, and what is new about
    // it fills what was empty.
    auto begins = [&](const std::string& word) {
        if (word.size() < prefix.size())
        {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); ++i)
        {
            if (LLStringOps::toLower(word[i]) != LLStringOps::toLower(prefix[i]))
            {
                return false;
            }
        }
        return true;
    };
    for (const Completion& c : mSupplied)
    {
        if (!begins(c.text))
        {
            continue;
        }
        bool known = false;
        for (Completion& have : mCompletions)
        {
            if (have.text == c.text)
            {
                known = true;
                if (have.detail.empty())
                {
                    have.detail = c.detail;
                    have.kind   = c.kind;
                }
                break;
            }
        }
        if (!known)
        {
            mCompletions.push_back(c);
        }
    }
    if (head.empty())
    {
        documentCompletions(at, prefix, mCompletions);
    }
    if (fresh && mCompletionRequest)
    {
        mCompletionRequest(start, prefix);
    }
    // What matches the case typed comes first; then the alphabet.
    std::stable_sort(mCompletions.begin(), mCompletions.end(), [&](const Completion& a, const Completion& b) {
        const bool a_exact = a.text.compare(0, prefix.size(), prefix) == 0;
        const bool b_exact = b.text.compare(0, prefix.size(), prefix) == 0;
        if (a_exact != b_exact)
        {
            return a_exact;
        }
        return a.text < b.text;
    });
    if (mCompletions.size() > COMPLETION_CAP)
    {
        mCompletions.resize(COMPLETION_CAP);
    }
    if (mCompletions.empty())
    {
        hideCompletionList();
        return;
    }
    mCompletionRange = ALTextRange(ALTextPos(at.line, at.column - static_cast<S32>(prefix.size())), at);
    listCompletions();
}

// static
const char* ALCodeEditor::badgeOf(const Completion& completion)
{
    if (!completion.snippet.empty())
    {
        return "s";
    }
    switch (completion.kind)
    {
        case ALSyntaxKind::Function:     return "f";
        case ALSyntaxKind::Event:        return "e";
        case ALSyntaxKind::Constant:     return "c";
        case ALSyntaxKind::Keyword:
        case ALSyntaxKind::Control:      return "k";
        case ALSyntaxKind::Type:         return "T";
        case ALSyntaxKind::Variable:     return "v";
        case ALSyntaxKind::Parameter:    return "p";
        case ALSyntaxKind::Property:     return ".";
        case ALSyntaxKind::Label:        return "L";
        case ALSyntaxKind::Preprocessor: return "#";
        case ALSyntaxKind::Tag:          return "<";
        case ALSyntaxKind::Attribute:    return "@";
        case ALSyntaxKind::Deprecated:   return "!";
        default:                         return "w";
    }
}

void ALCodeEditor::listCompletions()
{
    const S32 was = llmax(0, mCompletionList->chosen());
    if (mCompletionList->getFont() != getFont())
    {
        mCompletionList->setFont(getFont());
    }
    std::vector<ALChoiceList::Choice> choices;
    choices.reserve(mCompletions.size());
    for (const Completion& c : mCompletions)
    {
        ALChoiceList::Choice choice;
        choice.text  = c.text;
        choice.note  = c.detail;
        choice.icon  = c.icon;
        choice.badge = badgeOf(c);
        if (c.kind != ALSyntaxKind::Text)
        {
            choice.color = colorForKind(c.kind);
        }
        choices.push_back(std::move(choice));
    }
    // The list is shaped to the width its column is laid out for, then
    // to the rows it holds.
    const LLRect local = getLocalRect();
    const S32    width = llmin(COMPLETION_WIDTH, llmax(60, local.getWidth() - 8));
    mCompletionList->setShape(LLRect(0, 40, width, 0));
    mCompletionList->setChoices(std::move(choices), was);
    placeCompletion();
    mCompletionList->setVisible(true);
}

void ALCodeEditor::supplyCompletions(const ALTextPos& at, std::vector<Completion> more)
{
    // Only about the identifier the list is still narrowing.
    if (at != mCompletionAsked || hasSelection() || isReadOnly())
    {
        return;
    }
    mSupplied = std::move(more);
    refreshCompletion();
}

void ALCodeEditor::placeCompletion()
{
    const LLRect text  = textRect();
    const LLRect local = getLocalRect();
    const S32    row_h = layout().rowHeight();
    S32          row;
    const F32    x      = layout().xOf(mCompletionRange.begin.line, mCompletionRange.begin.column, &row);
    const S32    top    = screenTopOf(text, mCompletionRange.begin.line, row);
    const S32    rows   = llmin(static_cast<S32>(mCompletions.size()), COMPLETION_ROWS);
    const S32    height = mCompletionList->heightFor(rows);
    const S32    width  = llmin(COMPLETION_WIDTH, llmax(60, local.getWidth() - 8));
    S32          left   = static_cast<S32>(static_cast<F32>(text.mLeft) - scrollX() + x);
    left                = llclamp(left, local.mLeft, llmax(local.mLeft, local.mRight - width));
    LLRect rect;
    if (top - row_h - height >= local.mBottom || top + height > local.mTop)
    {
        // Under the row, or over it where under would run off the bottom.
        rect = LLRect(left, top - row_h, left + width, top - row_h - height);
    }
    else
    {
        rect = LLRect(left, top + height, left + width, top);
    }
    mCompletionList->setShape(rect);
}

void ALCodeEditor::openCompletion()
{
    refreshCompletion();
}

// --- the name at the caret ---------------------------------------------------------

ALTextRange ALCodeEditor::identifierAt(const ALTextPos& at) const
{
    const ALTextDocument& doc = document();
    const ALTextPos       pos = doc.clamp(at);
    const std::string     line = doc.text(ALTextRange(ALTextPos(pos.line, 0), ALTextPos(pos.line, doc.lineLength(pos.line))));
    const S32             n    = static_cast<S32>(line.size());
    if (pos.column >= n || !identifierByte(line[pos.column]))
    {
        return ALTextRange();
    }
    S32 begin = pos.column;
    S32 end   = pos.column + 1;
    while (begin > 0 && identifierByte(line[begin - 1]))
    {
        --begin;
    }
    while (end < n && identifierByte(line[end]))
    {
        ++end;
    }
    if (line[begin] >= '0' && line[begin] <= '9')
    {
        // A number.
        return ALTextRange();
    }
    return ALTextRange(ALTextPos(pos.line, begin), ALTextPos(pos.line, end));
}

ALTextRange ALCodeEditor::identifierAtCaret() const
{
    ALTextRange word = identifierAt(caret());
    if (word.empty() && caret().column > 0)
    {
        // At the end of one.
        word = identifierAt(ALTextPos(caret().line, caret().column - 1));
    }
    return word;
}

bool ALCodeEditor::mapMark(S32 line, LLColor4& color) const
{
    const Mark mark = markAt(line);
    if (mark == Mark::None)
    {
        return false;
    }
    color = mMarkColors[static_cast<size_t>(mark)].get();
    return true;
}

bool ALCodeEditor::canSymbol(ALEditorCommand command) const
{
    if (!mSymbolRequest || (command == ALEditorCommand::Rename && isReadOnly()))
    {
        return false;
    }
    return !identifierAtCaret().empty();
}

bool ALCodeEditor::performSymbol(ALEditorCommand command)
{
    if (!canSymbol(command))
    {
        return false;
    }
    closeCompletion();
    mSymbolRequest(command, identifierAtCaret());
    return true;
}

bool ALCodeEditor::complete()
{
    if (isReadOnly())
    {
        return false;
    }
    openCompletion();
    return true;
}

bool ALCodeEditor::acceptCompletion()
{
    if (!completionOpen())
    {
        return false;
    }
    const S32 index = mCompletionList->chosen();
    if (index < 0 || index >= static_cast<S32>(mCompletions.size()))
    {
        closeCompletion();
        return false;
    }
    const Completion  chosen = mCompletions[index];
    const ALTextRange range  = mCompletionRange;
    closeCompletion();
    complete(chosen, range);
    return true;
}

void ALCodeEditor::complete(const Completion& chosen, const ALTextRange& range)
{
    setSelection(range);
    if (!chosen.snippet.empty())
    {
        insertSnippet(chosen.snippet);
        setFocus(true);
        return;
    }
    // A function called: its brackets, unless they are there already,
    // with the caret between them where it takes anything, and the
    // signature asked for.
    const std::string& line   = document().line(range.end.line);
    const bool         called = chosen.kind == ALSyntaxKind::Function && !(range.end.column < static_cast<S32>(line.size()) && line[range.end.column] == '(');
    if (!called)
    {
        insertText(chosen.text);
    }
    else
    {
        // Its parameters as placeholders, where the detail names them;
        // else the caret between the brackets where it takes anything.
        const std::vector<std::string> names = parameterNames(chosen.detail, chosen.text);
        const size_t open  = parameterListAt(chosen.detail, chosen.text);
        size_t       after = open == std::string::npos ? std::string::npos : chosen.detail.find_first_not_of(' ', open + 1);
        const bool   takes = open == std::string::npos || after == std::string::npos || chosen.detail[after] != ')';
        std::string  call  = chosen.text + "(";
        std::vector<ALTextRange> places;
        const ALTextPos begin = range.begin;
        for (size_t i = 0; i < names.size(); ++i)
        {
            if (i > 0)
            {
                call += ", ";
            }
            const S32 from = begin.column + static_cast<S32>(call.size());
            call += names[i];
            places.emplace_back(ALTextPos(begin.line, from), ALTextPos(begin.line, from + static_cast<S32>(names[i].size())));
        }
        call += ")";
        insertText(call);
        if (!places.empty())
        {
            setPlaceholders(std::move(places), caret());
        }
        else if (takes)
        {
            setCaret(ALTextPos(caret().line, caret().column - 1));
        }
        if (takes && mSignatureRequest)
        {
            mSignatureRequest(caret());
        }
    }
    setFocus(true);
}

// --- snippets ------------------------------------------------------------------------

void ALCodeEditor::insertSnippet(std::string_view body)
{
    if (isReadOnly())
    {
        return;
    }
    // Where it goes, and how far in that line is, which every line of
    // the body after the first follows.
    const ALTextRange selection = this->selection();
    const ALTextPos   at        = std::min(selection.begin, selection.end);
    const std::string& line     = document().line(at.line);
    const std::string  indent   = line.substr(0, std::min(line.size(), line.find_first_not_of(" \t")));
    // The body read: the text as it will stand, and each placeholder's
    // place in it, by number.
    struct Place
    {
        S32         number;
        ALTextRange range;
    };
    std::vector<Place>       places;
    std::optional<ALTextPos> end;
    std::string              text;
    ALTextPos                pos = at;
    auto                     put = [&](char ch) {
        text += ch;
        if (ch == '\n')
        {
            ++pos.line;
            pos.column = 0;
            text += indent;
            pos.column += static_cast<S32>(indent.size());
        }
        else
        {
            ++pos.column;
        }
    };
    for (size_t i = 0; i < body.size(); ++i)
    {
        const char ch = body[i];
        if (ch != '$' || i + 1 >= body.size())
        {
            put(ch);
            continue;
        }
        if (body[i + 1] == '$')
        {
            put('$');
            ++i;
            continue;
        }
        // $n, ${n} or ${n:text}
        size_t      j       = i + 1;
        const bool  braced  = body[j] == '{';
        if (braced)
        {
            ++j;
        }
        size_t      digits  = j;
        while (digits < body.size() && isdigit(static_cast<unsigned char>(body[digits])))
        {
            ++digits;
        }
        if (digits == j)
        {
            put(ch);
            continue;
        }
        const S32   number  = atoi(std::string(body.substr(j, digits - j)).c_str());
        std::string content;
        size_t      after   = digits;
        if (braced)
        {
            const size_t close = body.find('}', digits);
            if (close == std::string_view::npos)
            {
                put(ch);
                continue;
            }
            if (body[digits] == ':')
            {
                content = std::string(body.substr(digits + 1, close - digits - 1));
            }
            after = close + 1;
        }
        if (number == 0)
        {
            end = pos;
        }
        else
        {
            const ALTextPos from = pos;
            for (char c : content)
            {
                put(c);
            }
            places.push_back(Place{ number, ALTextRange(from, pos) });
        }
        i = after - 1;
    }
    insertText(text);
    std::stable_sort(places.begin(), places.end(), [](const Place& a, const Place& b) { return a.number < b.number; });
    std::vector<ALTextRange> ranges;
    for (const Place& place : places)
    {
        ranges.push_back(place.range);
    }
    const ALTextPos landing = end.value_or(pos);
    if (!ranges.empty())
    {
        setPlaceholders(std::move(ranges), landing);
    }
    else
    {
        setCaret(landing);
    }
}

// --- placeholders ------------------------------------------------------------------

// static
size_t ALCodeEditor::parameterListAt(std::string_view detail, std::string_view name)
{
    if (!name.empty())
    {
        const std::string called = std::string(name) + "(";
        if (const size_t at = detail.find(called); at != std::string_view::npos)
        {
            return at + name.size();
        }
    }
    return detail.find('(');
}

// static
std::vector<std::string> ALCodeEditor::parameterNames(std::string_view detail, std::string_view name)
{
    std::vector<std::string> names;
    const size_t             open = parameterListAt(detail, name);
    if (open == std::string::npos)
    {
        return names;
    }
    // To the bracket that closes it, minding the ones inside.
    size_t close = open + 1;
    for (S32 depth = 1; close < detail.size() && depth > 0; ++close)
    {
        if (detail[close] == '(')
        {
            ++depth;
        }
        else if (detail[close] == ')')
        {
            if (--depth == 0)
            {
                break;
            }
        }
    }
    if (close >= detail.size())
    {
        return names;
    }
    const std::string_view inside = detail.substr(open + 1, close - open - 1);
    size_t                 at     = 0;
    S32                    depth  = 0;
    std::string            piece;
    auto take = [&]() {
        // "integer channel", "channel: number", "...any" or "channel".
        size_t a = piece.find_first_not_of(' ');
        size_t z = piece.find_last_not_of(' ');
        if (a == std::string::npos)
        {
            return;
        }
        std::string one = piece.substr(a, z - a + 1);
        if (const size_t colon = one.find(':'); colon != std::string::npos)
        {
            one = one.substr(0, colon);
        }
        else if (const size_t space = one.rfind(' '); space != std::string::npos)
        {
            one = one.substr(space + 1);
        }
        while (!one.empty() && one.back() == '?')
        {
            one.pop_back();
        }
        if (one.rfind("...", 0) == 0)
        {
            one = "...";
        }
        if (!one.empty())
        {
            names.push_back(one);
        }
    };
    for (; at < inside.size(); ++at)
    {
        const char c = inside[at];
        if (c == '(' || c == '<' || c == '{' || c == '[')
        {
            ++depth;
        }
        else if (c == ')' || c == '>' || c == '}' || c == ']')
        {
            --depth;
        }
        if (c == ',' && depth == 0)
        {
            take();
            piece.clear();
        }
        else
        {
            piece.push_back(c);
        }
    }
    take();
    return names;
}

void ALCodeEditor::setPlaceholders(std::vector<ALTextRange> ranges, const ALTextPos& after)
{
    mPlaceholders      = std::move(ranges);
    mPlaceholdersAfter = after;
    mPlaceholderAt     = mPlaceholders.empty() ? -1 : 0;
    if (mPlaceholderAt >= 0)
    {
        setSelection(mPlaceholders[0]);
    }
}

bool ALCodeEditor::nextPlaceholder(S32 direction)
{
    if (mPlaceholders.empty())
    {
        return false;
    }
    const S32 to = mPlaceholderAt + direction;
    if (to < 0)
    {
        return false;
    }
    if (to >= static_cast<S32>(mPlaceholders.size()))
    {
        // Past the last: after the call, done.
        const ALTextPos after = mPlaceholdersAfter;
        clearPlaceholders();
        setCaret(after);
        return true;
    }
    mPlaceholderAt = to;
    setSelection(mPlaceholders[to]);
    if (mSignatureRequest)
    {
        mSignatureRequest(caret());
    }
    return true;
}

void ALCodeEditor::clearPlaceholders()
{
    mPlaceholders.clear();
    mPlaceholderAt = -1;
}

// --- input -------------------------------------------------------------------

bool ALCodeEditor::handleKeyHere(KEY key, MASK mask)
{
    hideCard();
    if (!mPlaceholders.empty() && !completionOpen())
    {
        if (key == KEY_TAB && (mask == MASK_NONE || mask == MASK_SHIFT))
        {
            if (nextPlaceholder(mask == MASK_SHIFT ? -1 : 1))
            {
                return true;
            }
        }
        else if (key == KEY_ESCAPE && mask == MASK_NONE)
        {
            clearPlaceholders();
            return true;
        }
    }
    if (completionOpen() && mask == MASK_NONE)
    {
        switch (key)
        {
            case KEY_ESCAPE:
                closeCompletion();
                return true;
            case KEY_UP:
                mCompletionList->moveChoice(-1, true);
                return true;
            case KEY_DOWN:
                mCompletionList->moveChoice(1, true);
                return true;
            case KEY_PAGE_UP:
                mCompletionList->moveChoice(-COMPLETION_ROWS, false);
                return true;
            case KEY_PAGE_DOWN:
                mCompletionList->moveChoice(COMPLETION_ROWS, false);
                return true;
            case KEY_RETURN:
            case KEY_TAB:
                acceptCompletion();
                return true;
            case KEY_LEFT:
            case KEY_RIGHT:
            case KEY_HOME:
            case KEY_END:
                closeCompletion();
                break;
            default:
                break;
        }
    }
    else if (completionOpen() && key != KEY_BACKSPACE)
    {
        closeCompletion();
    }
    if (mSignature && key == KEY_ESCAPE && mask == MASK_NONE)
    {
        hideSignature();
        return true;
    }
    const bool taken = ALTextView::handleKeyHere(key, mask);
    if (taken && mSignature && mSignatureRequest && (key == KEY_BACKSPACE || key == KEY_DELETE))
    {
        mSignatureRequest(caret());
    }
    return taken;
}

bool ALCodeEditor::handleUnicodeCharHere(llwchar uni_char)
{
    const bool was_open = completionOpen();
    if (!ALTextView::handleUnicodeCharHere(uni_char))
    {
        return false;
    }
    const bool identifier = uni_char < 0x80 && identifierByte(static_cast<char>(uni_char));
    if (uni_char == '.' && mAutoComplete && caret().column >= 2 && !identifierAt(ALTextPos(caret().line, caret().column - 2)).empty())
    {
        // A member is coming: what there is to choose from, at once.
        openCompletion();
    }
    else if (!identifier)
    {
        closeCompletion();
    }
    else if (!was_open && mAutoComplete && static_cast<S32>(wordBeforeCaret().size()) >= COMPLETION_AUTO_AT)
    {
        openCompletion();
    }
    // A call begins, moves on to its next argument, or ends.
    if (mSignatureRequest && (uni_char == '(' || uni_char == ',' || uni_char == ')' || mSignature))
    {
        mSignatureRequest(caret());
    }
    return true;
}

bool ALCodeEditor::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (mCompletionList && mCompletionList->getVisible() && mCompletionList->getRect().pointInRect(x, y))
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }
    if (cardShown() && mCard->getRect().pointInRect(x, y))
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }
    hideCard();
    closeCompletion();
    clearPlaceholders();
    const LLRect text         = textRect();
    const S32    gutter_right = leftEdge() + gutterWidth();
    if (mShowFoldMarkers && x < gutter_right && x >= gutter_right - FOLD_COLUMN)
    {
        const S32 line = posAtLocal(text.mLeft, y, false).line;
        if (regionStartingAt(line))
        {
            if (isFolded(line))
            {
                unfoldAt(line);
            }
            else
            {
                foldAt(line);
            }
            return true;
        }
    }
    if (text.pointInRect(x, y))
    {
        // A header pinned at the top goes to its line.
        const std::vector<S32> pinned = stickyLines();
        const S32              row    = (text.mTop - y) / llmax(1, layout().rowHeight());
        if (row >= 0 && row < static_cast<S32>(pinned.size()))
        {
            goToLine(pinned[static_cast<size_t>(row)]);
            return true;
        }
    }
    if (x >= leftEdge() && x < gutter_right && text.mBottom <= y && y <= text.mTop)
    {
        // A line number chooses its line, whole; with shift, from the
        // selection's anchor to it.
        const S32 line = posAtLocal(text.mLeft, y, false).line;
        const S32 last = document().lineCount() - 1;
        const ALTextPos from(line, 0);
        const ALTextPos to = line < last ? ALTextPos(line + 1, 0) : ALTextPos(line, static_cast<S32>(document().line(line).size()));
        setFocus(true);
        if ((mask & MASK_SHIFT) && hasSelection())
        {
            const ALTextRange was = selection();
            setSelection(ALTextRange(was.begin, was.begin <= from ? to : from));
        }
        else
        {
            setSelection(ALTextRange(from, to));
        }
        return true;
    }
    if (!mFolded.empty() && text.pointInRect(x, y))
    {
        const S32 line = posAtLocal(x, y, false).line;
        if (isFolded(line) && foldBoxOf(line, text).pointInRect(x, y))
        {
            unfoldAt(line);
            return true;
        }
    }
    return ALTextView::handleMouseDown(x, y, mask);
}

bool ALCodeEditor::handleToolTip(S32 x, S32 y, MASK mask)
{
    const LLRect text = textRect();
    // A mark in the gutter says what is on its line: every problem there.
    if (x >= leftEdge() && x < leftEdge() + gutterWidth() && text.mBottom <= y && y <= text.mTop)
    {
        const S32   line = posAtLocal(text.mLeft, y, false).line;
        std::string says;
        for (const Decoration& d : mDecorations)
        {
            const ALTextRange range = d.range.normalised();
            if (!d.message.empty() && range.begin.line <= line && line <= range.end.line)
            {
                says += (says.empty() ? "" : "\n") + d.message;
            }
        }
        if (says.empty())
        {
            return ALTextView::handleToolTip(x, y, mask);
        }
        showCard(ALTextRange(ALTextPos(line, 0), ALTextPos(line, 0)), says);
        return true;
    }
    if (cardShown() && mCard->getRect().pointInRect(x, y))
    {
        // Resting on the card itself: it stays, and says nothing more.
        return true;
    }
    if (!text.pointInRect(x, y) || (mCompletionList && mCompletionList->getVisible() && mCompletionList->getRect().pointInRect(x, y)))
    {
        return ALTextView::handleToolTip(x, y, mask);
    }
    const ALTextPos at = posAtLocal(x, y, false);
    std::string     says;
    ALTextRange     about;
    // A problem under the mouse says what it is; else the word does.
    for (const Decoration& d : mDecorations)
    {
        const ALTextRange range = d.range.normalised();
        if (!d.message.empty() && range.begin <= at && at < range.end)
        {
            says  = d.message;
            about = range;
            break;
        }
    }
    const ALTextRange word = identifierAt(at);
    if (says.empty() && mHover && !word.empty())
    {
        if (mHover(at, document().text(word), says))
        {
            about = word;
        }
    }
    if (says.empty() && !word.empty())
    {
        // What the analyzer said of this word, if it was asked and the
        // text has not moved on since; else asked now, for an answer
        // that shows when it comes, or the next time the mouse rests
        // here.
        const U32 version = document().version();
        if (word == mHoverAsked && version == mHoverAskedVersion)
        {
            if (!mHoverAnswer.empty())
            {
                says  = mHoverAnswer;
                about = word;
            }
        }
        else if (mHoverRequest)
        {
            mHoverAsked        = word;
            mHoverAskedVersion = version;
            mHoverAnswer.clear();
            mHoverRequest(word.begin, document().text(word));
        }
    }
    if (says.empty())
    {
        return ALTextView::handleToolTip(x, y, mask);
    }
    showCard(about, says);
    return true;
}

void ALCodeEditor::showCard(const ALTextRange& about, const std::string& says)
{
    static const LLUIColor warning = LLUIColorTable::instance().getColor("CodeMarkWarning", LLColor4::yellow);
    static const LLUIColor ground  = LLUIColorTable::instance().getColor("CodeCompletionBgColor", LLColor4::black);
    const S32              MAX_WIDTH = 560;
    const S32              PAD       = 6;
    if (!mCard)
    {
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name        = "hover_card";
        p.rect        = LLRect(0, 20, MAX_WIDTH, 0);
        p.read_only   = true;
        p.word_wrap   = true;
        p.tab_stop    = false;
        p.takes_focus = false;
        p.mouse_opaque = true;
        p.font        = LLFontGL::getFontSansSerif();
        p.bg_visible  = true;
        p.bg_color    = ground;
        p.bg_readonly_color = ground;
        p.text_readonly_color = textColor();
        p.h_pad       = PAD;
        p.v_pad       = PAD - 2;
        p.context_menu = std::string();
        mCard         = LLUICtrlFactory::create<ALTextView>(p);
        mCard->setVisible(false);
        mCard->onLinkClicked([](const ALTextView::Substitution& link) {
            if (!link.url.empty())
            {
                LLUrlAction::clickAction(link.url, false);
            }
        });
        addChild(mCard);
    }
    if (cardShown() && about == mCardAbout && says == mCard->text())
    {
        // The mouse resting on again: the card is up already.
        return;
    }
    mCardAbout = about;
    // The words: the first line in the editor's face, a note about
    // deprecation in the warning colour, every URL a link.
    mCard->setText(says);
    std::vector<ALTextView::Style> styles;
    ALTextView::Style              head;
    head.range = ALTextRange(ALTextPos(0, 0), mCard->document().lineEnd(0));
    head.font  = getFont();
    styles.push_back(head);
    const S32 lines = mCard->document().lineCount();
    for (S32 line = 0; line < lines; ++line)
    {
        if (mCard->document().line(line).find("(deprecated)") != std::string::npos)
        {
            ALTextView::Style note;
            note.range = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
            note.color = warning.get();
            styles.push_back(note);
        }
    }
    mCard->setStyles(std::move(styles));
    for (S32 line = 0; line < lines; ++line)
    {
        mCard->linkUrlsOn(line);
    }
    // Its size: as wide as its widest line up to the limit, and as tall
    // as the lines wrapped at that width come to.
    const LLRect text  = textRect();
    const S32    limit = llmin(MAX_WIDTH, llmax(80, text.getWidth() - 2 * PAD));
    mCard->setShape(LLRect(0, 40, limit, 0));
    mCard->setWordWrap(false);
    const S32 widest = static_cast<S32>(mCard->layout().contentWidth()) + 2 * PAD + 2;
    mCard->setWordWrap(true);
    const S32 width = llmin(limit, llmax(40, widest));
    mCard->setShape(LLRect(0, 40, width, 0));
    for (S32 line = 0; line < lines; ++line)
    {
        mCard->layout().line(line);
    }
    const S32 height = mCard->layout().totalHeight() + 2 * (PAD - 2) + 2;
    // Where: under the row of what it is about, at its start; above it
    // where under would run off the bottom; within the text's width.
    S32       row;
    layout().xOf(about.begin.line, about.begin.column, &row);
    const S32 top   = screenTopOf(text, about.begin.line, row);
    const S32 row_h = layout().rowHeightOf(about.begin.line, row);
    F32       x0, x1;
    mCardAnchor = LLRect(text.mLeft, top, text.mRight, top - row_h);
    if (spanOnRow(about.begin.line, row, about, x0, x1) && !about.empty())
    {
        const F32 left     = static_cast<F32>(text.mLeft) - scrollX();
        mCardAnchor.mLeft  = static_cast<S32>(left + x0);
        mCardAnchor.mRight = static_cast<S32>(left + x1);
    }
    S32 x = llclamp(mCardAnchor.mLeft, text.mLeft, llmax(text.mLeft, text.mRight - width));
    S32 y = mCardAnchor.mBottom - 2;
    if (y - height < text.mBottom && mCardAnchor.mTop + 2 + height <= text.mTop)
    {
        y = mCardAnchor.mTop + 2 + height;
    }
    mCard->setShape(LLRect(x, y, x + width, y - height));
    mCard->setVisible(true);
}

bool ALCodeEditor::handleScrollWheel(S32 x, S32 y, LLScrollDelta delta)
{
    if (cardShown() && mCard->getRect().pointInRect(x, y))
    {
        return mCard->handleScrollWheel(x - mCard->getRect().mLeft, y - mCard->getRect().mBottom, delta);
    }
    if (completionOpen() && mCompletionList->getRect().pointInRect(x, y))
    {
        const LLRect& rect = mCompletionList->getRect();
        return mCompletionList->handleScrollWheel(x - rect.mLeft, y - rect.mBottom, delta);
    }
    hideCard();
    return ALTextView::handleScrollWheel(x, y, delta);
}

void ALCodeEditor::hideCard()
{
    if (mCard && mCard->getVisible())
    {
        mCard->setVisible(false);
    }
    mCardAnchor = LLRect();
}

bool ALCodeEditor::cardShown() const
{
    return mCard && mCard->getVisible();
}

void ALCodeEditor::supplyHover(const ALTextPos& at, const std::string& text)
{
    if (text.empty() || mHoverAsked.empty() || at != mHoverAsked.begin || document().version() != mHoverAskedVersion)
    {
        return;
    }
    // Kept for the word, and shown now if the mouse is still on it.
    mHoverAnswer = text;
    if (mMouseX < 0 || !textRect().pointInRect(mMouseX, mMouseY))
    {
        return;
    }
    const ALTextPos   under = posAtLocal(mMouseX, mMouseY, false);
    const ALTextRange word  = identifierAt(under);
    if (word != mHoverAsked)
    {
        return;
    }
    showCard(word, text);
}

bool ALCodeEditor::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    // An identifier, as code reads one; the document's word otherwise.
    const ALTextRange word = textRect().pointInRect(x, y) && sameClickSpot(x, y) ? identifierAt(posAtLocal(x, y, false)) : ALTextRange();
    if (word.empty())
    {
        return ALTextView::handleDoubleClick(x, y, mask);
    }
    setFocus(true);
    setSelection(word);
    armTripleClick();
    if (modalKeymap())
    {
        modalKeymap()->mouseChanged(*this);
    }
    return true;
}

bool ALCodeEditor::handleHover(S32 x, S32 y, MASK mask)
{
    mMouseX = x;
    mMouseY = y;
    if (cardShown())
    {
        const LLRect card = mCard->getRect();
        if (card.pointInRect(x, y))
        {
            // On the card: its links light up, its cursor shows.
            mCard->handleHover(x - card.mLeft, y - card.mBottom, mask);
            return true;
        }
        if (!mCardAnchor.pointInRect(x, y))
        {
            hideCard();
        }
    }
    const S32 gutter_right = leftEdge() + gutterWidth();
    mGutterHover           = gutterWidth() > 0 && x >= leftEdge() && x < gutter_right && textRect().mBottom <= y && y <= textRect().mTop;
    mGutterHoverLine       = mGutterHover ? posAtLocal(textRect().mLeft, y, false).line : -1;
    return ALTextView::handleHover(x, y, mask);
}

void ALCodeEditor::onMouseLeave(S32 x, S32 y, MASK mask)
{
    mGutterHover     = false;
    mGutterHoverLine = -1;
    hideCard();
    ALTextView::onMouseLeave(x, y, mask);
}

// --- signature help -------------------------------------------------------------

void ALCodeEditor::showSignature(const ALTextPos& at, Signature signature)
{
    mSignature   = std::move(signature);
    mSignatureAt = at;
}

void ALCodeEditor::hideSignature()
{
    mSignature.reset();
}

bool ALCodeEditor::signatureShown() const
{
    return mSignature && caret().line == mSignatureAt.line && !(caret() < mSignatureAt);
}

void ALCodeEditor::drawSignature(const LLRect& text)
{
    if (!mSignature || mSignature->label.empty())
    {
        return;
    }
    const Signature& sig   = *mSignature;
    const LLFontGL*  font  = getFont();
    const F32        alpha = getDrawContext().mAlpha;
    const S32        row_h = layout().rowHeight();
    const S32        line_h = font->getLineHeight();
    const bool       docs  = !sig.documentation.empty();
    const std::string doc_line = docs ? sig.documentation.substr(0, sig.documentation.find('\n')) : std::string();
    const S32        width = llmax(font->getWidth(sig.label), docs ? font->getWidth(doc_line) : 0) + 2 * SIGNATURE_PAD;
    const S32        height = line_h * (docs ? 2 : 1) + 2 * SIGNATURE_PAD;

    // Above the caret's row, left with the call's column, kept inside the
    // view; under the row where above would run off the top.
    S32       row;
    const F32 x    = layout().xOf(mSignatureAt.line, mSignatureAt.column, &row);
    const S32 top  = screenTopOf(text, mSignatureAt.line, row);
    const LLRect local = getLocalRect();
    S32       left = llclamp(static_cast<S32>(static_cast<F32>(text.mLeft) - scrollX() + x), local.mLeft, llmax(local.mLeft, local.mRight - width));
    LLRect    box  = (top + height <= local.mTop) ? LLRect(left, top + height, left + width, top)
                                                  : LLRect(left, top - row_h, left + width, top - row_h - height);

    const LLColor4 bg     = towards(backgroundColor(), textColor(), 0.08f) % alpha;
    const LLColor4 border = foldColor() % alpha;
    const LLColor4 ink    = textColor() % alpha;
    const LLColor4 active = mBracketMatchColor.get() % alpha;
    const LLColor4 faint  = lineNumberColor() % alpha;
    gl_rect_2d(box, bg);
    gl_rect_2d(box, border, false);

    // The label in three pieces, the active parameter in its own colour.
    const F32 baseline = static_cast<F32>(box.mTop - SIGNATURE_PAD - llround(font->getAscenderHeight()));
    F32       pen      = static_cast<F32>(box.mLeft + SIGNATURE_PAD);
    S32       begin = -1, end = -1;
    if (sig.active >= 0 && sig.active < static_cast<S32>(sig.parameters.size()))
    {
        begin = sig.parameters[sig.active].first;
        end   = sig.parameters[sig.active].second;
    }
    auto piece = [&](S32 from, S32 to, const LLColor4& color) {
        if (to <= from)
        {
            return;
        }
        const std::string part = sig.label.substr(from, to - from);
        font->renderUTF8(part, 0, pen, baseline, color, LLFontGL::LEFT, LLFontGL::BASELINE);
        pen += static_cast<F32>(font->getWidth(part));
    };
    if (begin >= 0 && end > begin && end <= static_cast<S32>(sig.label.size()))
    {
        piece(0, begin, ink);
        piece(begin, end, active);
        piece(end, static_cast<S32>(sig.label.size()), ink);
    }
    else
    {
        piece(0, static_cast<S32>(sig.label.size()), ink);
    }
    if (docs)
    {
        font->renderUTF8(doc_line, 0, static_cast<F32>(box.mLeft + SIGNATURE_PAD), baseline - static_cast<F32>(line_h), faint, LLFontGL::LEFT, LLFontGL::BASELINE);
    }
}

void ALCodeEditor::draw()
{
    // A signature is about a call on the caret's line; anywhere else it
    // is stale, and so are the placeholders of a call the caret has left.
    if (mSignature && !signatureShown())
    {
        hideSignature();
    }
    if (!mPlaceholders.empty() && caret().line != mPlaceholders.front().begin.line)
    {
        clearPlaceholders();
    }
    ALTextView::draw();
    if (mSignature)
    {
        drawSignature(textRect());
    }
}

void ALCodeEditor::onFocusLost()
{
    closeCompletion();
    ALTextView::onFocusLost();
}
