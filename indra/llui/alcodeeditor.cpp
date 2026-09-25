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

#include "alsaid.h"
#include "alsurface.h"

#include "altextchars.h"
#include "llfocusmgr.h"
#include "lllocalcliprect.h"
#include "llstl.h"
#include "llrender2dutils.h"
#include "alchoicelist.h"
#include "llstring.h"
#include "lltooltip.h"
#include "llui.h"
#include "lluicolortable.h"
#include "llurlaction.h"
#include "lluictrlfactory.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <functional>

#include <algorithm>
#include <cmath>
#include <optional>

static LLDefaultChildRegistry::Register<ALCodeEditor> r("code_editor");

namespace
{
    const S32 GUTTER_PAD  = 6;
    const S32 MARK_SIZE   = 6;
    const S32 MARK_INSET  = 3;
    const S32 FOLD_COLUMN = 12;
    const S32 FOLD_MARKER = 7;
    const S32 FOLD_BOX_GAP = 6;
    // The gutter's strip of heat, at its edge beside the text, with a
    // pixel of the gutter on either side; and how far after a line's end
    // its note begins.
    const S32 HEAT_COLUMN  = 5;
    const S32 NOTE_GAP     = 16;
    const S32 COMPLETION_WIDTH   = 360;
    const S32 COMPLETION_ROWS    = 8;
    // The box beside a list: a completion's documentation, a fix's preview.
    const S32 SIDE_WIDTH         = 320;
    const S32 SIDE_PAD           = 6;
    // What a line's problems offer, as the gutter keeps it.
    const U8  FIXES_ANY          = 1;
    const U8  FIXES_CHANGE       = 2;
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
    mFixable.assign(document().lineCount(), 0);
    mEditConnection    = document().onChanged([this](const ALTextDocument::Edit& edit) { onEdit(edit); });
    layout().setInlayProvider([this](S32 line, std::vector<ALTextLayout::Inlay>& out) { provideInlays(line, out); });
    mChangedConnection = onTextChanged([this]() {
        if (completionOpen())
        {
            refreshCompletion();
        }
        // Stepped back to the saved text: nothing is changed since.
        if (!isDirty())
        {
            std::fill(mChanged.begin(), mChanged.end(), 0);
        }
    });
    mCaretConnection = onCaretMoved([this]() { dropPlaceholdersLeft(); });

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
    mCompletionList->onChosen([this](S32) {
        if (completionOpen())
        {
            showCompletionDoc();
        }
    });
    addChild(mCompletionList);

    // The fixes offered at a problem, on a list of their own made the same
    // way: a fix is not a completion, and the one list open at a time
    // keeps its own keys.
    list.name("fixes");
    mFixList = LLUICtrlFactory::create<ALChoiceList>(list);
    mFixList->onPicked([this](S32 index) { takeFix(index); });
    mFixList->onChosen([this](S32) {
        if (fixesOpen())
        {
            showFixPreview();
        }
    });
    addChild(mFixList);
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
    // What the problems there offered goes with them: a check says again.
    mFixable.resize(llmax(mFixable.size(), static_cast<size_t>(last + 1)), 0);
    if (first < static_cast<S32>(mFixable.size()))
    {
        mFixable.erase(mFixable.begin() + first, mFixable.begin() + last + 1);
    }
    mFixable.insert(mFixable.begin() + first, made, 0);
    mFixable.resize(document().lineCount(), 0);
    closeFixes();
    // What is known of bracket depth below the edit is known no more.
    mDepthValid = llmin(mDepthValid, first);
    // The lines the edit touched are changed until the next save.
    mChanged.resize(llmax(mChanged.size(), static_cast<size_t>(last + 1)), 0);
    mChanged.erase(mChanged.begin() + first, mChanged.begin() + last + 1);
    mChanged.insert(mChanged.begin() + first, made, 1);
    mChanged.resize(document().lineCount(), 0);
    slideAsides(edit, made);

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
    // move with the text, and one the edit cut into goes, with its mirrors.
    // A mirror being brought up is what the edit put in.
    if (!mPlaceholders.empty())
    {
        for (S32 k = 0; k < static_cast<S32>(mMirrors.size());)
        {
            Mirror& mirror = mMirrors[static_cast<size_t>(k)];
            if (k == mSyncingMirror)
            {
                mirror.range = edit.rangeAfter();
                ++k;
            }
            else if (edit.slide(mirror.range))
            {
                ++k;
            }
            else
            {
                mMirrors.erase(mMirrors.begin() + k);
                if (mSyncingMirror > k)
                {
                    --mSyncingMirror;
                }
            }
        }
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
                mMirrors.erase(std::remove_if(mMirrors.begin(), mMirrors.end(), [i](const Mirror& m) { return m.of == i; }), mMirrors.end());
                for (Mirror& m : mMirrors)
                {
                    if (m.of > i)
                    {
                        --m.of;
                    }
                }
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

    // A closer typing put in moves with the text before it, and goes with
    // an edit that takes it.
    mAutoClosed.erase(std::remove_if(mAutoClosed.begin(), mAutoClosed.end(),
                                     [&](ALTextPos& at) {
                                         if (removed.end <= at)
                                         {
                                             at = edit.slidPast(at);
                                             return false;
                                         }
                                         return !(at < removed.begin);
                                     }),
                      mAutoClosed.end());

    // Folds slide the same way. One that starts on the edit's first line
    // stays: typing on a block's first line is not opening the block. One
    // on its last line stays too where the edit ended at that line's start
    // and left it a line of its own -- whole lines taken from above a
    // folded block -- and moves up with it.
    const bool last_kept = last > first && removed.end.column == 0 &&
                           (edit.inserted.empty() ? removed.begin.column == 0 : edit.inserted.back() == '\n');
    mFolded.erase(std::remove_if(mFolded.begin(), mFolded.end(),
                                 [&](S32 start) { return start > first && (start < last || (start == last && !last_kept)); }),
                  mFolded.end());
    for (S32& start : mFolded)
    {
        if (start > last || (start == last && last_kept))
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
    if (line >= 0 && line < static_cast<S32>(mMarks.size()) && mMarks[line] != mark)
    {
        mMarks[line] = mark;
        ++mMarksRevision;
    }
}

ALCodeEditor::Mark ALCodeEditor::markAt(S32 line) const
{
    return (line >= 0 && line < static_cast<S32>(mMarks.size())) ? mMarks[line] : Mark::None;
}

// static
const char* ALCodeEditor::paintName(Paint which)
{
    static const char* const NAMES[] = {
        "ActiveLineNumberColor", "IndentGuideColor", "WhitespaceColor", "InlayHintColor", "InlayHintBgColor", "StickyHeaderColor", "WidgetColor",
        "WidgetBorderColor", "WidgetSelectionColor", "ErrorColor", "WarningColor", "NoteColor", "RuntimeErrorColor", "SelectionInactiveColor", "HeatColor",
    };
    static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == static_cast<size_t>(Paint::COUNT), "every paint has a name");
    const size_t index = static_cast<size_t>(which);
    return index < static_cast<size_t>(Paint::COUNT) ? NAMES[index] : "";
}

LLColor4 ALCodeEditor::paint(Paint which) const
{
    const LLUIColorTable& table = LLUIColorTable::instance();
    if (!mPaintLooked || mPaintGeneration != table.generation())
    {
        mPaintLooked     = true;
        mPaintGeneration = table.generation();
        for (size_t i = 0; i < mPaint.size(); ++i)
        {
            const std::string name = colorPrefix() + paintName(static_cast<Paint>(i));
            mPaint[i]              = table.colorExists(name) ? std::optional<LLUIColor>(table.getColor(name)) : std::nullopt;
        }
    }
    if (const std::optional<LLUIColor>& named = mPaint[static_cast<size_t>(which)])
    {
        return named->get();
    }
    const LLColor4& ink   = textColor();
    const LLColor4& paper = backgroundColor();
    switch (which)
    {
        case Paint::ActiveLineNumber:  return ink;
        case Paint::IndentGuide:       return foldColor() % 0.35f;
        case Paint::Whitespace:        return foldColor() % 0.55f;
        case Paint::InlayHint:         return lerp(paper, ink, 0.65f);
        case Paint::InlayHintBg:       return lerp(paper, ink, 0.12f);
        case Paint::StickyHeader:
        case Paint::Widget:            return ALSurface::ground(paper, ink);
        case Paint::WidgetBorder:      return ALSurface::frame(ink);
        case Paint::WidgetSelection:   return ALSurface::chosen(paper, ink);
        case Paint::Error:             return mMarkColors[static_cast<size_t>(Mark::Error)].get();
        case Paint::Warning:           return mMarkColors[static_cast<size_t>(Mark::Warning)].get();
        case Paint::Note:              return mMarkColors[static_cast<size_t>(Mark::Note)].get();
        case Paint::RuntimeError:      return mMarkColors[static_cast<size_t>(Mark::Runtime)].get();
        case Paint::Heat:              return paint(Paint::Warning);
        case Paint::SelectionInactive:
        {
            // Quieter than the selection the keys act on, as every modern
            // editor has it: nearer the ground, its own alpha kept.
            const LLColor4& selection = selectionColor();
            LLColor4        quiet     = lerp(paper, selection, 0.55f);
            quiet.mV[VALPHA]          = selection.mV[VALPHA];
            return quiet;
        }
        default:                       return ink;
    }
}

LLColor4 ALCodeEditor::markColor(Mark mark) const
{
    switch (mark)
    {
        case Mark::Error:   return paint(Paint::Error);
        case Mark::Warning: return paint(Paint::Warning);
        case Mark::Note:    return paint(Paint::Note);
        case Mark::Runtime: return paint(Paint::RuntimeError);
        default:            return mMarkColors[static_cast<size_t>(mark)].get();
    }
}

LLColor4 ALCodeEditor::selectionDrawColor() const
{
    return keyboardOnText() ? selectionColor() : paint(Paint::SelectionInactive);
}

void ALCodeEditor::clearMarks()
{
    std::fill(mMarks.begin(), mMarks.end(), Mark::None);
    std::fill(mFixable.begin(), mFixable.end(), 0);
    ++mMarksRevision;
}

void ALCodeEditor::setFixable(S32 line, bool any, bool changes)
{
    if (line < 0 || line >= document().lineCount())
    {
        return;
    }
    mFixable.resize(document().lineCount(), 0);
    mFixable[line] = static_cast<U8>((any || changes ? FIXES_ANY : 0) | (changes ? FIXES_CHANGE : 0));
}

bool ALCodeEditor::fixableAt(S32 line) const
{
    return line >= 0 && line < static_cast<S32>(mFixable.size()) && (mFixable[line] & FIXES_ANY) != 0;
}

bool ALCodeEditor::changesAt(S32 line) const
{
    return line >= 0 && line < static_cast<S32>(mFixable.size()) && (mFixable[line] & FIXES_CHANGE) != 0;
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
    const ALTextPos    at   = caret();
    const std::string& line = document().line(at.line);
    // The bracket just before the caret, else the one under it.
    char partner = 0;
    bool opens   = false;
    ALTextPos from;
    if (at.column > 0 && bracketOf(line[at.column - 1], partner, opens))
    {
        from = ALTextPos(at.line, at.column - 1);
    }
    else if (at.column < static_cast<S32>(line.size()) && bracketOf(line[at.column], partner, opens))
    {
        from = at;
    }
    else
    {
        return false;
    }
    ALTextPos match;
    if (!matchBracketAt(from, match))
    {
        return false;
    }
    open  = opens ? from : match;
    close = opens ? match : from;
    return true;
}

bool ALCodeEditor::closerOpenedAt(const ALTextPos& closer, ALTextPos& opener)
{
    return matchBracketAt(closer, opener) && opener < closer;
}

bool ALCodeEditor::matchBracketAt(const ALTextPos& from, ALTextPos& match)
{
    const ALTextDocument& doc  = document();
    const std::string&    line = doc.line(from.line);
    char                  c = 0, partner = 0;
    bool                  opens = false;
    if (from.column < 0 || from.column >= static_cast<S32>(line.size()) || !bracketOf(line[from.column], partner, opens))
    {
        return false;
    }
    c = line[from.column];

    // A line's bytes walked with a cursor over its tokens, which are in
    // order, so that whether a byte is inside a string or a comment
    // costs the tokens once per line rather than once per byte.
    S32 depth = 0;
    if (opens)
    {
        for (S32 l = from.line; l < doc.lineCount(); ++l)
        {
            const std::string&                text   = doc.line(l);
            const std::vector<ALSyntaxToken>& tokens = highlighter().tokens(l);
            size_t                            t      = 0;
            for (S32 i = (l == from.line ? from.column : 0); i < static_cast<S32>(text.size()); ++i)
            {
                if (text[i] != c && text[i] != partner)
                {
                    continue;
                }
                while (t < tokens.size() && tokens[t].end <= i)
                {
                    ++t;
                }
                if (t < tokens.size() && tokens[t].begin <= i && quiet(tokens[t].kind))
                {
                    if (l == from.line && i == from.column)
                    {
                        return false;
                    }
                    continue;
                }
                depth += (text[i] == c) ? 1 : -1;
                if (depth == 0)
                {
                    match = ALTextPos(l, i);
                    return true;
                }
            }
        }
    }
    else
    {
        for (S32 l = from.line; l >= 0; --l)
        {
            const std::string&                text   = doc.line(l);
            const std::vector<ALSyntaxToken>& tokens = highlighter().tokens(l);
            size_t                            t      = tokens.size();
            for (S32 i = (l == from.line ? from.column : static_cast<S32>(text.size()) - 1); i >= 0; --i)
            {
                if (text[i] != c && text[i] != partner)
                {
                    continue;
                }
                while (t > 0 && tokens[t - 1].begin > i)
                {
                    --t;
                }
                if (t > 0 && tokens[t - 1].end > i && quiet(tokens[t - 1].kind))
                {
                    if (l == from.line && i == from.column)
                    {
                        return false;
                    }
                    continue;
                }
                depth += (text[i] == c) ? 1 : -1;
                if (depth == 0)
                {
                    match = ALTextPos(l, i);
                    return true;
                }
            }
        }
    }
    return false;
}

// --- colours -----------------------------------------------------------------


LLColor4 ALCodeEditor::gutterColor() const
{
    return mGutterColorSet ? mGutterColor.get() : ALSurface::ground(backgroundColor(), textColor());
}

LLColor4 ALCodeEditor::lineNumberColor() const
{
    return mLineNumberColorSet ? mLineNumberColor.get() : ALSurface::shade(backgroundColor(), textColor(), 0.5f);
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
    return mFoldColorSet ? mFoldColor.get() : ALSurface::shade(backgroundColor(), textColor(), 0.6f);
}

LLColor4 ALCodeEditor::changedColor() const
{
    return mChangedColorSet ? mChangedColor.get() : ALSurface::shade(backgroundColor(), LLColor4(0.35f, 0.6f, 0.95f, 1.f), 0.9f);
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

void ALCodeEditor::markUnsaved()
{
    ALTextView::markUnsaved();
    mChanged.assign(static_cast<size_t>(document().lineCount()), 1);
}

void ALCodeEditor::barChangesSince(std::string_view saved)
{
    std::vector<std::string_view> was;
    for (size_t at = 0;;)
    {
        const size_t nl = saved.find('\n', at);
        was.push_back(saved.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at));
        if (nl == std::string_view::npos)
        {
            break;
        }
        at = nl + 1;
    }
    const size_t now  = static_cast<size_t>(document().lineCount());
    size_t       head = 0;
    while (head < now && head < was.size() && document().line(static_cast<S32>(head)) == was[head])
    {
        ++head;
    }
    size_t tail = 0;
    while (tail < now - head && tail < was.size() - head && document().line(static_cast<S32>(now - 1 - tail)) == was[was.size() - 1 - tail])
    {
        ++tail;
    }
    mChanged.assign(now, 0);
    std::fill(mChanged.begin() + static_cast<std::ptrdiff_t>(head), mChanged.end() - static_cast<std::ptrdiff_t>(tail), 1);
    if (head + tail == now && was.size() != now && now > 0)
    {
        // Lines taken away and nothing else: the line they were taken from
        // barred, as the edit that took them would bar it.
        mChanged[std::min(head, now - 1)] = 1;
    }
}

void ALCodeEditor::markSavedAt(const ALTextUndo::SavePoint& point)
{
    ALTextView::markSavedAt(point);
    // The bars go where the text is the saved one; where more was typed
    // meanwhile they stay, which marks a line or two too many rather than
    // one too few.
    if (!isDirty())
    {
        std::fill(mChanged.begin(), mChanged.end(), 0);
    }
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
    return width + heatWidth();
}

S32 ALCodeEditor::heatWidth() const
{
    return mHeatShown ? HEAT_COLUMN : 0;
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
    const LLRect local = bodyRect();
    const LLRect gutter(leftEdge(), local.mTop, leftEdge() + width, local.mBottom);
    gl_rect_2d(gutter, gutterColor() % alpha);

    const LLFontGL* font   = getFont();
    const S32       row_h  = layout().rowHeight();
    const S32       ascent = ll_round(font->getAscenderHeight());
    const S32       fold_right    = gutter.mRight - heatWidth();
    const S32       numbers_right = fold_right - (mShowFoldMarkers ? FOLD_COLUMN : 0) - GUTTER_PAD;
    const LLColor4  warm          = paint(Paint::Heat);
    const LLColor4  ink     = lineNumberColor() % alpha;
    const LLColor4  lit     = paint(Paint::ActiveLineNumber) % alpha;
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
        // Down every row of the line, a line that made something at least
        // faintly and the warmest in the heat's own colour.
        if (mHeatShown)
        {
            if (const F32 heat = heatAt(line); heat > 0.f)
            {
                gl_rect_2d(fold_right + 1, screen_top, gutter.mRight - 1, screen_top - row_h, warm % (alpha * (0.15f + 0.85f * llclamp(heat, 0.f, 1.f))));
            }
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
                             line == caret_line ? lit : ink, LLFontGL::RIGHT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
            const Mark mark = markAt(line);
            if (line == caret_line && fixableAt(line) && !isReadOnly())
            {
                // The lightbulb: what the problems here offer is a press on
                // it, or Control-., away. Ringed in the mark's colour where
                // what it is about is an error.
                const F32 cx = static_cast<F32>(gutter.mLeft + MARK_INSET) + MARK_SIZE / 2.f;
                const F32 r  = llmax(3.f, llmin(row_h / 2.f - 2.f, 5.f));
                const F32 cy = static_cast<F32>(screen_top) - row_h / 2.f + 1.f;
                if (mark == Mark::Error || mark == Mark::Runtime)
                {
                    gGL.color4fv((markColor(mark) % alpha).mV);
                    gl_circle_2d(cx, cy, r + 1.5f, 16, true);
                }
                gGL.color4fv((paint(Paint::Warning) % alpha).mV);
                gl_circle_2d(cx, cy, r, 16, true);
                gl_rect_2d(static_cast<S32>(cx - r / 2.f), static_cast<S32>(cy - r + 1.f), static_cast<S32>(cx + r / 2.f + 1.f),
                           static_cast<S32>(cy - r - 2.f), ink);
            }
            else if (mark != Mark::None)
            {
                const S32 y = screen_top - (row_h - MARK_SIZE) / 2;
                if (changesAt(line) && !isReadOnly())
                {
                    // Round where a fix would put it right.
                    gGL.color4fv((markColor(mark) % alpha).mV);
                    gl_circle_2d(static_cast<F32>(gutter.mLeft + MARK_INSET) + MARK_SIZE / 2.f, static_cast<F32>(y) - MARK_SIZE / 2.f,
                                 MARK_SIZE / 2.f + 0.5f, 12, true);
                }
                else
                {
                    gl_rect_2d(gutter.mLeft + MARK_INSET, y, gutter.mLeft + MARK_INSET + MARK_SIZE, y - MARK_SIZE, markColor(mark) % alpha);
                }
            }
        }
        // A marker at a block's first line: pointing right at a folded
        // one, always; pointing down at an open one, while the mouse is
        // over the gutter, so that the gutter is quiet otherwise.
        if (mShowFoldMarkers && regionStartingAt(line) && (isFolded(line) || mGutterHover))
        {
            const S32 cx = fold_right - FOLD_COLUMN / 2;
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
        const S32 cx = fold_right - FOLD_COLUMN / 2;
        gl_rect_2d(cx, guide_top - row_h + FOLD_MARKER / 2, cx + 1, guide_bottom, fold);
    }
    // The headers pinned over the text have their numbers pinned over
    // the gutter, on the same band.
    const std::vector<S32> pinned = stickyLines();
    if (!pinned.empty() && mShowLineNumbers)
    {
        const S32    rows = static_cast<S32>(pinned.size());
        const LLRect band(gutter.mLeft, text.mTop, gutter.mRight, text.mTop - rows * row_h);
        gl_rect_2d(band, paint(Paint::StickyHeader) % alpha, true);
        gl_rect_2d(band.mLeft, band.mBottom, band.mRight, band.mBottom - 1, fold % 0.6f, true);
        for (S32 i = 0; i < rows; ++i)
        {
            const S32 line  = pinned[static_cast<size_t>(i)];
            const S32 shown_number = mRelativeLineNumbers && line != caret_line ? std::abs(line - caret_line) : line + 1;
            font->renderUTF8(std::to_string(shown_number), 0, static_cast<F32>(numbers_right), static_cast<F32>(text.mTop - i * row_h - ascent), ink,
                             LLFontGL::RIGHT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
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

S32 ALCodeEditor::inlayAtLocal(S32 x, S32 y)
{
    const LLRect text  = textRect();
    const S32    row_h = layout().rowHeight();
    if (mInlays.empty() || row_h <= 0 || !text.pointInRect(x, y) || document().lineCount() == 0)
    {
        return -1;
    }
    // Not under a header pinned at the top, which stands over what it hides.
    if ((text.mTop - y) / row_h < static_cast<S32>(stickyLines().size()))
    {
        return -1;
    }
    // As the pills are drawn: the row under the point, and the gap each
    // inlay's glyph holds, less the pill's inset.
    const S32 doc_y = (text.mTop - y) + scrollY();
    const S32 line  = layout().lineAtY(llmax(0, doc_y));
    const S32 row   = layout().rowAtY(line, doc_y - layout().lineTop(line));
    const ALTextLayout::Line& laid = layout().line(line);
    if (row < 0 || row >= static_cast<S32>(laid.rows.size()))
    {
        return -1;
    }
    const ALTextLayout::Row& r     = laid.rows[static_cast<size_t>(row)];
    const F32                x_rel = static_cast<F32>(x - text.mLeft) + scrollX();
    for (size_t k = r.glyphBegin; k < r.glyphEnd; ++k)
    {
        const ALTextLayout::Glyph& glyph = laid.glyphs[k];
        if (glyph.inlay < 0 || glyph.inlay >= static_cast<S32>(mInlays.size()))
        {
            continue;
        }
        const F32 x0 = glyph.pen - r.xStart + INLAY_GAP;
        const F32 x1 = glyph.pen - r.xStart + glyph.advance - INLAY_GAP;
        if (x_rel >= x0 && x_rel < x1)
        {
            return glyph.inlay;
        }
    }
    return -1;
}

void ALCodeEditor::setHeatShown(bool shown)
{
    if (mHeatShown != shown)
    {
        mHeatShown = shown;
        // The gutter is another width: the text moves over.
        reshape(getRect().getWidth(), getRect().getHeight());
    }
}

void ALCodeEditor::setLineHeat(const std::vector<LineHeat>& heat)
{
    for (Aside& aside : mAsides)
    {
        aside.heat = 0.f;
        aside.heatTip.clear();
    }
    for (const LineHeat& one : heat)
    {
        if (one.line < 0 || one.line >= document().lineCount())
        {
            continue;
        }
        mAsides.resize(llmax(mAsides.size(), static_cast<size_t>(document().lineCount())));
        mAsides[static_cast<size_t>(one.line)].heat    = one.heat;
        mAsides[static_cast<size_t>(one.line)].heatTip = one.tip;
    }
}

F32 ALCodeEditor::heatAt(S32 line) const
{
    return line >= 0 && line < static_cast<S32>(mAsides.size()) ? mAsides[static_cast<size_t>(line)].heat : 0.f;
}

void ALCodeEditor::setLineNotes(const std::vector<LineNote>& notes)
{
    for (Aside& aside : mAsides)
    {
        aside.note.clear();
        aside.noteTip.clear();
    }
    for (const LineNote& one : notes)
    {
        if (one.line < 0 || one.line >= document().lineCount())
        {
            continue;
        }
        mAsides.resize(llmax(mAsides.size(), static_cast<size_t>(document().lineCount())));
        mAsides[static_cast<size_t>(one.line)].note    = one.text;
        mAsides[static_cast<size_t>(one.line)].noteTip = one.tip;
    }
}

std::string ALCodeEditor::noteAt(S32 line) const
{
    return line >= 0 && line < static_cast<S32>(mAsides.size()) ? mAsides[static_cast<size_t>(line)].note : std::string();
}

S32 ALCodeEditor::noteAtLocal(S32 x, S32 y)
{
    const LLRect text = textRect();
    if (mAsides.empty() || !text.pointInRect(x, y) || document().lineCount() == 0)
    {
        return -1;
    }
    const S32 line = posAtLocal(text.mLeft, y, false).line;
    return noteBoxOf(line, text).pointInRect(x, y) ? line : -1;
}

void ALCodeEditor::slideAsides(const ALTextDocument::Edit& edit, S32 made)
{
    if (mAsides.empty())
    {
        return;
    }
    const ALTextRange range = edit.range.normalised();
    const S32         first = llmax(0, range.begin.line);
    const S32         last  = llmax(first, range.end.line);
    mAsides.resize(llmax(mAsides.size(), static_cast<size_t>(last + 1)));
    const Aside from_first = mAsides[static_cast<size_t>(first)];
    const Aside from_last  = mAsides[static_cast<size_t>(last)];
    mAsides.erase(mAsides.begin() + first, mAsides.begin() + last + 1);
    mAsides.insert(mAsides.begin() + first, static_cast<size_t>(made), Aside());
    // What is left of a line keeps its heat and its note: the line the
    // edit begins inside -- typed in, or broken in two -- or, where the
    // edit begins at a line's start, the line it ends in, pushed down by
    // the lines made above it or pulled up over the lines taken.
    if (range.begin.column > 0)
    {
        mAsides[static_cast<size_t>(first)] = from_first;
    }
    else
    {
        mAsides[static_cast<size_t>(first + made - 1)] = from_last;
    }
    mAsides.resize(static_cast<size_t>(llmax(document().lineCount(), 0)));
}

bool ALCodeEditor::writeInlay(S32 index)
{
    if (index < 0 || index >= static_cast<S32>(mInlays.size()) || mInlays[index].insert.empty() || isReadOnly())
    {
        return false;
    }
    const InlayHint hint = mInlays[static_cast<size_t>(index)];
    if (document().clamp(hint.at) != hint.at)
    {
        return false;
    }
    // The text says it now, so the hint goes; an insertion at a hint's
    // place would only slide it along.
    std::vector<InlayHint> kept = mInlays;
    mInlays.erase(mInlays.begin() + index);
    layout().invalidateLine(hint.at.line);
    if (!replaceAll({ { ALTextRange(hint.at, hint.at), hint.insert } }))
    {
        mInlays = std::move(kept);
        layout().invalidateLine(hint.at.line);
        return false;
    }
    undoJournal().label("refactor");
    return true;
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

S32 ALCodeEditor::coveredAbove(S32 local_x)
{
    return llmax(ALTextView::coveredAbove(local_x), stickyRows() * layout().rowHeight());
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
    gl_rect_2d(band, paint(Paint::StickyHeader) % alpha, true);
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
    // The matched pair for this frame, found again only where the text
    // or the caret moved since the last.
    if (mMatchBrackets && keyboardOnText())
    {
        if (mBrackets.version != document().version() || mBrackets.caret != caret())
        {
            mBrackets.version = document().version();
            mBrackets.caret   = caret();
            mBrackets.matched = matchingBrackets(mBrackets.open, mBrackets.close);
        }
    }
    else
    {
        mBrackets.matched = false;
        mBrackets.caret   = ALTextPos(-1, -1);
    }
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

LLRect ALCodeEditor::noteBoxOf(S32 line, const LLRect& text)
{
    if (line < 0 || line >= static_cast<S32>(mAsides.size()) || mAsides[static_cast<size_t>(line)].note.empty() || layout().rowCount(line) <= 0)
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
    const LLRect             fold  = foldBoxOf(line, text);
    const S32 x0 = fold.notEmpty() ? fold.mRight + NOTE_GAP : static_cast<S32>(static_cast<F32>(text.mLeft) - scrollX() + last.xStart + last.width) + NOTE_GAP;
    const S32 w  = getFont()->getWidth(mAsides[static_cast<size_t>(line)].note);
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

std::vector<ALCodeEditor::Blank> ALCodeEditor::blanksOn(S32 line, S32 within_from, S32 within_to) const
{
    std::vector<Blank> out;
    if (mShowWhitespace == Whitespace::None || line < 0 || line >= document().lineCount())
    {
        return out;
    }
    const std::string& source = document().line(line);
    const S32          length = static_cast<S32>(source.size());
    auto               nbspAt = [&source, length](S32 i) {
        return i + 1 < length && static_cast<unsigned char>(source[static_cast<size_t>(i)]) == 0xC2 &&
               static_cast<unsigned char>(source[static_cast<size_t>(i) + 1]) == 0xA0;
    };
    // What part of the line the mode is asking about, within what the
    // caller asked about.
    S32 from = llmax(0, within_from), to = llmin(length, within_to);
    if (mShowWhitespace == Whitespace::Selection)
    {
        const ALTextRange sel = selection().normalised();
        if (sel.empty() || line < sel.begin.line || line > sel.end.line)
        {
            return out;
        }
        from = llmax(from, sel.begin.line == line ? sel.begin.column : 0);
        to   = llmin(to, sel.end.line == line ? sel.end.column : length);
    }
    else if (mShowWhitespace == Whitespace::Trailing)
    {
        // What the line ends in and would be the better for losing. A
        // line that is nothing but blanks is all trailing, which is what
        // it is: there is nothing there for them to trail.
        S32 tail = length;
        while (tail > 0)
        {
            const char last = source[static_cast<size_t>(tail) - 1];
            if (last == ' ' || last == '\t')
            {
                --tail;
            }
            else if (tail > 1 && nbspAt(tail - 2))
            {
                tail -= 2;
            }
            else
            {
                break;
            }
        }
        if (tail >= length)
        {
            return out;
        }
        from = llmax(from, tail);
    }
    for (S32 i = llmax(0, from); i < to;)
    {
        const unsigned char c = static_cast<unsigned char>(source[static_cast<size_t>(i)]);
        if (c == ' ')
        {
            out.push_back({ i, i + 1, ' ' });
            ++i;
        }
        else if (c == '\t')
        {
            out.push_back({ i, i + 1, '\t' });
            ++i;
        }
        else if (nbspAt(i))
        {
            out.push_back({ i, i + 2, 'n' });
            i += 2;
        }
        else
        {
            ++i;
        }
    }
    return out;
}

void ALCodeEditor::drawWhitespace(S32 line, S32 r, const LLRect& text, S32 screen_top, F32 left, F32 alpha)
{
    if (mShowWhitespace == Whitespace::None)
    {
        return;
    }
    const ALTextLayout::Line& laid = layout().line(line);
    if (r < 0 || r >= static_cast<S32>(laid.rows.size()))
    {
        return;
    }
    const ALTextLayout::Row& row = laid.rows[static_cast<size_t>(r)];
    // Only what this row covers: a line wrapped into many rows would
    // otherwise be walked once for each of them.
    std::vector<Blank> blanks = blanksOn(line, row.begin, row.end);
    if (blanks.empty())
    {
        return;
    }
    const S32                row_h = layout().rowHeight();
    const S32                mid   = screen_top - row_h / 2;
    const LLColor4           mark  = paint(Paint::Whitespace) % alpha;
    const LLColor4           alarm = markColor(Mark::Warning) % (0.8f * alpha);
    const S32                dot   = llclamp(row_h / 8, 1, 3);

    // The glyphs the layout placed and the blanks the line holds, walked
    // together: both are in order, and a mark belongs where its own glyph
    // was put, which is the only way a tab is drawn across the width it
    // actually took rather than the width a tab is guessed to be.
    size_t b = 0;
    for (size_t k = row.glyphBegin; k < row.glyphEnd && b < blanks.size(); ++k)
    {
        const ALTextLayout::Glyph& glyph = laid.glyphs[k];
        if (glyph.substitution >= 0 || glyph.inlay >= 0)
        {
            // Something standing in the text's place, not a byte of it.
            continue;
        }
        while (b < blanks.size() && blanks[b].begin < glyph.cluster)
        {
            ++b;
        }
        if (b >= blanks.size() || blanks[b].begin != glyph.cluster)
        {
            continue;
        }
        const Blank blank = blanks[b++];
        const F32   x0    = left + glyph.pen - row.xStart;
        const F32   x1    = x0 + glyph.advance;
        if (x1 < static_cast<F32>(text.mLeft) || x0 >= static_cast<F32>(text.mRight))
        {
            continue;
        }
        const S32 cx = static_cast<S32>((x0 + x1) * 0.5f);
        if (blank.kind == ' ')
        {
            gl_rect_2d(cx - dot / 2, mid + (dot + 1) / 2, cx - dot / 2 + dot, mid + (dot + 1) / 2 - dot, mark);
        }
        else if (blank.kind == '\t')
        {
            // An arrow the width of the stop it reached, so that how far a
            // tab carried is read off rather than counted.
            const F32 head = llclamp((x1 - x0) * 0.25f, 2.f, 4.f);
            const F32 a0   = x0 + 2.f;
            const F32 a1   = llmax(a0 + 1.f, x1 - 2.f);
            gl_rect_2d(static_cast<S32>(a0), mid + 1, static_cast<S32>(a1), mid, mark);
            const F32                    y = static_cast<F32>(mid) + 0.5f;
            const std::vector<LLVector2> point{ { a1 - head, y + head }, { a1, y }, { a1 - head, y - head } };
            gl_polyline_2d(point, mark, 1.f);
        }
        else
        {
            // A no-break space: a ring, in the colour a warning wears,
            // because it is one. It reads as a space, and the compiler
            // will not have it.
            const S32 side = llclamp(row_h / 3, 3, 7);
            gl_rect_2d(cx - side / 2, mid + side / 2, cx - side / 2 + side, mid + side / 2 - side, alarm, false);
        }
    }
}

void ALCodeEditor::drawRowExtras(S32 line, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha)
{
    const S32 row_h = layout().rowHeight();
    drawWhitespace(line, row, text, screen_top, left, alpha);
    if (mShowIndentGuides && row == 0)
    {
        // One faint line per level within the indentation, at the tab
        // stops; a blank line takes the next line's, so a block's guides
        // run unbroken through it.
        const S32 indent = indentOf(line);
        const S32 tab    = getTabWidth();
        if (indent >= tab && tab > 0)
        {
            const LLColor4 guide = paint(Paint::IndentGuide) % alpha;
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
                const LLColor4 pill   = paint(Paint::InlayHintBg) % alpha;
                const LLColor4 word   = paint(Paint::InlayHint) % alpha;
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
    if (mBrackets.matched && (line == mBrackets.open.line || line == mBrackets.close.line))
    {
        for (const ALTextPos& at : { mBrackets.open, mBrackets.close })
        {
            F32 x0, x1;
            if (at.line == line && spanOnRow(line, row, ALTextRange(at, document().nextCluster(at)), x0, x1))
            {
                gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, mBracketMatchColor.get() % alpha, false);
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
            getFont()->renderUTF8(foldBoxText(line), 0, static_cast<F32>(box.mLeft + 4), static_cast<F32>(screen_top - llround(getFont()->getAscenderHeight())), ink, LLFontGL::LEFT, LLFontGL::BASELINE,
                                  LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
        }
    }
    // Its note, dim, after all of that; as much of it as is in view.
    if (row + 1 == layout().rowCount(line) && line >= 0 && line < static_cast<S32>(mAsides.size()) && !mAsides[static_cast<size_t>(line)].note.empty())
    {
        const LLRect box = noteBoxOf(line, text);
        if (box.notEmpty() && box.mLeft < text.mRight)
        {
            getFont()->renderUTF8(mAsides[static_cast<size_t>(line)].note, 0, static_cast<F32>(box.mLeft),
                                  static_cast<F32>(screen_top - llround(getFont()->getAscenderHeight())), paint(Paint::InlayHint) % alpha, LLFontGL::LEFT,
                                  LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX, text.mRight - box.mLeft);
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
    mListedFor.clear();
    hideCompletionDoc();
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
    // Every word, matched as the document's own are -- a part of it, the
    // letters of its parts -- rather than by its start alone.
    std::vector<std::pair<std::string, std::string>> words;
    if (highlighter().grammar())
    {
        highlighter().grammar()->collectWords(std::string_view(), words);
    }
    highlighter().words().collect(std::string_view(), words);
    for (auto& [word, table] : words)
    {
        if (matchTier(word, prefix) < 0)
        {
            continue;
        }
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
    auto begins = [&](std::string_view word) { return matchTier(word, prefix) >= 0; };
    // The document's own words, other than the one being typed.
    const S32 count = document().lineCount();
    for (S32 l = 0; l < count && out.size() < COMPLETION_CAP; ++l)
    {
        const std::string& line = document().line(l);
        size_t             i    = 0;
        while (i < line.size())
        {
            if (!alIdentifierByte(line[i]))
            {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < line.size() && alIdentifierByte(line[j]))
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
    // it fills what was empty. Each found by its name, not by a walk of
    // the list for each answer.
    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> listed;
    if (!mSupplied.empty())
    {
        listed.reserve(mCompletions.size() + mSupplied.size());
        for (size_t i = 0; i < mCompletions.size(); ++i)
        {
            listed.emplace(mCompletions[i].text, i);
        }
    }
    for (const Completion& c : mSupplied)
    {
        if (matchTier(c.text, prefix) < 0)
        {
            continue;
        }
        if (const auto known = listed.find(c.text); known != listed.end())
        {
            Completion& have = mCompletions[known->second];
            if (have.detail.empty())
            {
                have.detail = c.detail;
                have.kind   = c.kind;
            }
            if (have.documentation.empty())
            {
                have.documentation = c.documentation;
            }
            continue;
        }
        listed.emplace(c.text, mCompletions.size());
        mCompletions.push_back(c);
    }
    if (head.empty())
    {
        documentCompletions(at, prefix, mCompletions);
    }
    if (fresh && mCompletionRequest)
    {
        mCompletionRequest(start, prefix);
    }
    // The best match first: the start of the word as typed, then in
    // either case, then a part of it, then letters of its parts. Among
    // equals the script's own names -- a parameter, a local, a field --
    // then the language's words, then its constants, then what is
    // deprecated, then the document's bare words; then the alphabet.
    auto rank = [](const Completion& c) {
        if (c.deprecated || c.kind == ALSyntaxKind::Deprecated)
        {
            return 3;
        }
        switch (c.kind)
        {
            case ALSyntaxKind::Parameter:
            case ALSyntaxKind::Variable:
            case ALSyntaxKind::GlobalVariable:
            case ALSyntaxKind::Property:
                return 0;
            case ALSyntaxKind::Constant:
                return 2;
            case ALSyntaxKind::Text:
                return 4;
            default:
                return 1;
        }
    };
    struct Sorted
    {
        S32 tier;
        S32 rank;
    };
    std::vector<std::pair<Sorted, Completion>> sorted;
    sorted.reserve(mCompletions.size());
    for (Completion& c : mCompletions)
    {
        const S32 tier = prefix.empty() ? 0 : matchTier(c.text, prefix);
        sorted.push_back({ { tier < 0 ? 9 : tier, rank(c) }, std::move(c) });
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        if (a.first.tier != b.first.tier)
        {
            return a.first.tier < b.first.tier;
        }
        if (a.first.rank != b.first.rank)
        {
            return a.first.rank < b.first.rank;
        }
        return a.second.text < b.second.text;
    });
    mCompletions.clear();
    for (auto& [order, c] : sorted)
    {
        mCompletions.push_back(std::move(c));
    }
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
    // The same list again, an answer joined to it, keeps what was chosen
    // in it; a list for more typed starts from the best.
    const bool same = completionOpen() && asked == mListedFor;
    mListedFor      = asked;
    listCompletions(same);
}

// static
LLUIImagePtr ALCodeEditor::iconOf(const Completion& completion)
{
    if (completion.icon)
    {
        return completion.icon;
    }
    static boost::unordered_flat_map<std::string, LLUIImagePtr, ll::string_hash, std::equal_to<>> looked_up;
    const std::string_view                                                                       name = iconNameOf(completion);
    auto                                                                                         found = looked_up.find(name);
    if (found == looked_up.end())
    {
        found = looked_up.emplace(std::string(name), LLUI::getUIImage(std::string(name))).first;
    }
    return found->second;
}

void ALCodeEditor::listCompletions(bool keep_choice)
{
    // What was chosen, by its word: an answer joined to the list may put
    // rows above it, and the row under the finger must stay the word the
    // finger is on.
    std::string was;
    if (keep_choice && mCompletionList->chosen() >= 0 && mCompletionList->chosen() < mCompletionList->count())
    {
        was = mCompletionList->choices()[mCompletionList->chosen()].text;
    }
    S32 chosen = 0;
    for (size_t i = 0; i < mCompletions.size() && !was.empty(); ++i)
    {
        if (mCompletions[i].text == was)
        {
            chosen = static_cast<S32>(i);
            break;
        }
    }
    if (mCompletionList->getFont() != getFont())
    {
        mCompletionList->setFont(getFont());
    }
    // The list is one of the things the editor floats over its text, so
    // it wears the editor's colours rather than the skin's. It took them
    // from the colour table instead, which is a table the script themes
    // never touch: on a light theme the completions stayed dark.
    mCompletionList->setBackgroundColor(paint(Paint::Widget));
    mCompletionList->setTextColor(textColor());
    mCompletionList->setSelectionColor(paint(Paint::WidgetSelection));
    mCompletionList->setBorderColor(paint(Paint::WidgetBorder));
    std::vector<ALChoiceList::Choice> choices;
    choices.reserve(mCompletions.size());
    for (const Completion& c : mCompletions)
    {
        ALChoiceList::Choice choice;
        choice.text  = c.text;
        choice.note  = c.detail;
        choice.icon  = iconOf(c);
        choice.badge = badgeOf(c);
        if (c.deprecated)
        {
            choice.color = colorForKind(ALSyntaxKind::Deprecated);
        }
        else if (c.kind != ALSyntaxKind::Text)
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
    mCompletionList->setChoices(std::move(choices), chosen);
    placeCompletion();
    mCompletionList->setVisible(true);
    showCompletionDoc();
}

void ALCodeEditor::hideCompletionDoc()
{
    if (mCompletionDoc)
    {
        mCompletionDoc->setVisible(false);
    }
}

ALTextView* ALCodeEditor::sideBox()
{
    const LLColor4 ground = paint(Paint::Widget);
    if (!mCompletionDoc)
    {
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name                = "completion_doc";
        p.rect                = LLRect(0, 20, SIDE_WIDTH, 0);
        p.read_only           = true;
        p.word_wrap           = true;
        p.tab_stop            = false;
        p.takes_focus         = false;
        p.mouse_opaque        = true;
        p.font                = LLFontGL::getFontSansSerif();
        p.bg_visible          = true;
        p.bg_color            = ground;
        p.bg_readonly_color   = ground;
        p.text_readonly_color = textColor();
        p.h_pad               = SIDE_PAD;
        p.v_pad               = SIDE_PAD - 2;
        p.context_menu        = std::string();
        mCompletionDoc        = LLUICtrlFactory::create<ALTextView>(p);
        mCompletionDoc->setVisible(false);
        mCompletionDoc->onLinkClicked([](const ALTextView::Substitution& link) {
            if (!link.url.empty())
            {
                LLUrlAction::clickAction(link.url, false);
            }
        });
        addChild(mCompletionDoc);
    }
    mCompletionDoc->setBackgroundColor(ground);
    mCompletionDoc->setTextColor(textColor());
    return mCompletionDoc;
}

void ALCodeEditor::placeSideBox(const LLRect& list)
{
    // Beside the list where there is room, on the right or else the
    // left; else under it, or over it; as tall as it says, up to a
    // limit, the rest cut.
    ALTextView&  box   = *mCompletionDoc;
    const LLRect local = getLocalRect();
    const S32    width = llmin(SIDE_WIDTH, llmax(120, local.getWidth() - 8));
    const S32    lines = box.document().lineCount();
    box.setShape(LLRect(0, 40, width, 0));
    for (S32 line = 0; line < lines; ++line)
    {
        box.layout().line(line);
    }
    const S32 height = llmin(box.layout().totalHeight() + 2 * (SIDE_PAD - 2) + 2, llmax(list.getHeight(), 200));
    LLRect    rect;
    if (list.mRight + 2 + width <= local.mRight)
    {
        rect = LLRect(list.mRight + 2, list.mTop, list.mRight + 2 + width, list.mTop - height);
    }
    else if (list.mLeft - 2 - width >= local.mLeft)
    {
        rect = LLRect(list.mLeft - 2 - width, list.mTop, list.mLeft - 2, list.mTop - height);
    }
    else if (list.mBottom - 2 - height >= local.mBottom)
    {
        rect = LLRect(list.mLeft, list.mBottom - 2, list.mLeft + width, list.mBottom - 2 - height);
    }
    else
    {
        rect = LLRect(list.mLeft, list.mTop + 2 + height, list.mLeft + width, list.mTop + 2);
    }
    // Kept within the view, top and bottom.
    if (rect.mBottom < local.mBottom)
    {
        rect.translate(0, local.mBottom - rect.mBottom);
    }
    if (rect.mTop > local.mTop)
    {
        rect.translate(0, local.mTop - rect.mTop);
    }
    box.setShape(rect);
    box.setVisible(true);
}

void ALCodeEditor::showCompletionDoc()
{
    const S32 index = chosenCompletion();
    if (index < 0 || index >= static_cast<S32>(mCompletions.size()) || mCompletions[index].documentation.empty())
    {
        hideCompletionDoc();
        return;
    }
    const Completion& c   = mCompletions[index];
    ALTextView&       box = *sideBox();
    // Its declaration as code, then what it does in the reading face.
    std::string says = c.detail.empty() ? c.text : c.detail;
    if (c.deprecated)
    {
        says += "\n" + deprecatedNote();
    }
    says += "\n" + c.documentation;
    box.setText(says);
    std::vector<ALTextView::Style> styles;
    styleAsCode(box, 0, styles, c.text, c.kind);
    if (c.deprecated)
    {
        ALTextView::Style note;
        note.range = ALTextRange(ALTextPos(1, 0), box.document().lineEnd(1));
        note.color = markColor(Mark::Warning);
        styles.push_back(note);
    }
    box.setStyles(std::move(styles));
    const S32 lines = box.document().lineCount();
    for (S32 line = 0; line < lines; ++line)
    {
        box.linkUrlsOn(line);
    }
    placeSideBox(mCompletionList->getRect());
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
    const LLRect local = getLocalRect();
    placeListAt(*mCompletionList, mCompletionRange.begin, llmin(static_cast<S32>(mCompletions.size()), COMPLETION_ROWS),
                llmin(COMPLETION_WIDTH, llmax(60, local.getWidth() - 8)));
}

void ALCodeEditor::placeListAt(ALChoiceList& list, const ALTextPos& at, S32 rows, S32 width)
{
    const LLRect text   = textRect();
    const LLRect local  = getLocalRect();
    const S32    row_h  = layout().rowHeight();
    S32          row;
    const F32    x      = layout().xOf(at.line, at.column, &row);
    const S32    top    = screenTopOf(text, at.line, row);
    const S32    height = list.heightFor(rows);
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
    list.setShape(rect);
}

// --- quick fixes -----------------------------------------------------------------

namespace
{
    // The word that there is nothing to offer, which the list shows as it
    // shows a fix: nothing to make, and nothing to hand back.
    bool isNothing(const ALCodeEditor::Fix& fix)
    {
        return fix.edits.empty() && fix.value.isUndefined();
    }
}

bool ALCodeEditor::fixesOpen() const
{
    return mFixList && mFixList->getVisible();
}

void ALCodeEditor::closeFixes()
{
    const bool was = fixesOpen();
    if (mFixList)
    {
        mFixList->setVisible(false);
        mFixList->setChoices({});
    }
    mFixes.clear();
    mFixLine       = -1;
    mActionsWanted = false;
    // The preview is the side box's, which the completions share.
    if (was)
    {
        hideCompletionDoc();
    }
}

bool ALCodeEditor::openFixes(S32 line)
{
    closeFixes();
    if (!mFixProvider || isReadOnly() || line < 0 || line >= document().lineCount())
    {
        return false;
    }
    std::vector<Fix> fixes;
    mFixProvider(line, fixes);
    if (fixes.empty())
    {
        return false;
    }
    rankFixes(fixes);
    showFixes(line, std::move(fixes), 0);
    return true;
}

void ALCodeEditor::showFixes(S32 line, std::vector<Fix> fixes, S32 chosen)
{
    closeCompletion();
    hideSignature();
    hideCard();
    mFixes   = std::move(fixes);
    mFixLine = line;
    fillFixList(chosen);
    ++mFixShowing;
    if (mFixesShown)
    {
        mFixesShown(mFixShowing, mFixes);
    }
}

void ALCodeEditor::fillFixList(S32 chosen)
{
    // In the editor's colours, as the completions are; a suppression, and
    // the word that there is nothing, quieter than what makes a change.
    mFixList->setBackgroundColor(paint(Paint::Widget));
    mFixList->setTextColor(textColor());
    mFixList->setSelectionColor(paint(Paint::WidgetSelection));
    mFixList->setBorderColor(paint(Paint::WidgetBorder));
    std::vector<ALChoiceList::Choice> choices;
    S32                               widest = 0;
    S32                               noted  = 0;
    for (const Fix& fix : mFixes)
    {
        ALChoiceList::Choice choice;
        choice.text = fix.title;
        choice.note = fix.note;
        if (fix.suppress || isNothing(fix))
        {
            choice.color = paint(Paint::InlayHint);
        }
        widest = llmax(widest, getFont()->getWidth(fix.title));
        noted  = fix.note.empty() ? noted : llmax(noted, getFont()->getWidth(fix.note));
        choices.push_back(std::move(choice));
    }
    const LLRect local = getLocalRect();
    const S32    width = llclamp(widest + (noted > 0 ? noted + 24 : 0) + 24, 120, llmax(120, local.getWidth() - 8));
    mFixList->setShape(LLRect(0, 40, width, 0));
    mFixList->setChoices(std::move(choices), llclamp(chosen, 0, llmax(0, static_cast<S32>(mFixes.size()) - 1)));
    // Under the caret where it is on the line, else under the line's text.
    const S32          line  = mFixLine;
    const ALTextPos    caret = this->caret();
    const std::string& text  = document().line(llclamp(line, 0, document().lineCount() - 1));
    const size_t       lead  = text.find_first_not_of(" \t");
    const ALTextPos    at    = caret.line == line ? caret : ALTextPos(line, lead == std::string::npos ? 0 : static_cast<S32>(lead));
    placeListAt(*mFixList, at, llmin(static_cast<S32>(mFixes.size()), COMPLETION_ROWS), width);
    mFixList->setVisible(true);
    showFixPreview();
}

void ALCodeEditor::noteFixes(U32 shown, const std::vector<std::string>& notes)
{
    if (shown != mFixShowing || !fixesOpen() || notes.size() != mFixes.size())
    {
        return;
    }
    for (size_t i = 0; i < notes.size(); ++i)
    {
        mFixes[i].note = notes[i];
    }
    fillFixList(mFixList->chosen());
}

void ALCodeEditor::supplyActions(const ALTextRange& at, std::vector<Fix> actions)
{
    if (!mActionsWanted || !(at == mActionsFor) || !(caret() == mActionsCaret) || isReadOnly())
    {
        return;
    }
    mActionsWanted = false;
    const S32        line = caret().line;
    std::vector<Fix> fixes;
    std::string      was;
    if (fixesOpen())
    {
        fixes = mFixes;
        const S32 chosen = mFixList->chosen();
        if (chosen >= 0 && chosen < static_cast<S32>(fixes.size()))
        {
            was = fixes[chosen].title;
        }
    }
    for (Fix& action : actions)
    {
        action.refactor = true;
        fixes.push_back(std::move(action));
    }
    if (fixes.empty())
    {
        // Asked for, and nothing to offer: said, rather than nothing
        // happening at all.
        Fix none;
        none.title = alSaid("CodeFixNone", "Nothing to fix or refactor here");
        fixes.push_back(std::move(none));
    }
    else if (fixesOpen() && fixes.size() == mFixes.size())
    {
        return;
    }
    rankFixes(fixes);
    // The choice stays on what was chosen: the refactors come in while the
    // list is already being read.
    S32 chosen = 0;
    for (size_t i = 0; i < fixes.size() && !was.empty(); ++i)
    {
        if (fixes[i].title == was)
        {
            chosen = static_cast<S32>(i);
            break;
        }
    }
    showFixes(line, std::move(fixes), chosen);
}

// static
void ALCodeEditor::rankFixes(std::vector<Fix>& fixes)
{
    const auto rank = [](const Fix& fix) { return fix.suppress ? 3 : fix.refactor ? 2 : fix.preferred ? 0 : 1; };
    std::stable_sort(fixes.begin(), fixes.end(), [&rank](const Fix& a, const Fix& b) { return rank(a) < rank(b); });
}

// static
std::string ALCodeEditor::fixedLines(const ALTextDocument& text, const Fix& fix, S32& first, S32& last)
{
    first = S32_MAX;
    last  = -1;
    for (const auto& [range, with] : fix.edits)
    {
        const ALTextRange ordered = range.normalised();
        first                     = llmin(first, ordered.begin.line);
        last                      = llmax(last, ordered.end.line);
    }
    if (last < 0 || first >= text.lineCount())
    {
        first = last = 0;
        return std::string();
    }
    first = llmax(0, first);
    last  = llmin(last, text.lineCount() - 1);
    std::string         block;
    std::vector<size_t> starts;
    for (S32 line = first; line <= last; ++line)
    {
        starts.push_back(block.size());
        block += text.line(line);
        if (line < last)
        {
            block += "\n";
        }
    }
    // In the order the editor makes them (ALTextView::replaceAll), from
    // the last back, so that each one's places are still the text's as
    // it was.
    std::vector<std::pair<ALTextRange, std::string>> edits = fix.edits;
    for (auto& one : edits)
    {
        one.first = one.first.normalised();
    }
    std::stable_sort(edits.begin(), edits.end(),
                     [](const auto& a, const auto& b) { return a.first.begin < b.first.begin || (a.first.begin == b.first.begin && a.first.end < b.first.end); });
    const auto offset = [&](const ALTextPos& at) {
        const S32 line = llclamp(at.line, first, last);
        return starts[line - first] + static_cast<size_t>(llclamp(at.column, 0, static_cast<S32>(text.line(line).size())));
    };
    for (auto it = edits.rbegin(); it != edits.rend(); ++it)
    {
        const auto& [range, with] = *it;
        const ALTextRange ordered = range.normalised();
        const size_t      from    = offset(ordered.begin);
        const size_t      to      = offset(ordered.end);
        if (to >= from && to <= block.size())
        {
            block.replace(from, to - from, with);
        }
    }
    return block;
}

// static
std::string ALCodeEditor::previewOf(const ALTextDocument& text, const Fix& fix, std::vector<char>& kinds)
{
    // The lines it touches as they read and as they would, as a diff has
    // them: what goes in the error colour, what comes coloured as code. A
    // stretch for each place it changes, edits a few lines apart or less
    // together, and an ellipsis for what lies between -- a global put in
    // at the top for a use two hundred lines down is two short stretches,
    // not the two hundred lines.
    std::vector<std::pair<ALTextRange, std::string>> edits = fix.edits;
    for (auto& one : edits)
    {
        one.first = one.first.normalised();
    }
    std::stable_sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.first.begin < b.first.begin; });
    std::vector<Fix> stretches;
    S32              reach = -1;
    for (const auto& one : edits)
    {
        if (stretches.empty() || one.first.begin.line > reach + 2)
        {
            stretches.emplace_back();
        }
        stretches.back().edits.push_back(one);
        reach = llmax(reach, one.first.end.line);
    }
    std::string says;
    kinds.clear();
    const auto add = [&says, &kinds](char kind, const std::string& line) {
        says += (says.empty() ? "" : "\n") + (kind == ' ' ? line : std::string(1, kind) + " " + line);
        kinds.push_back(kind);
    };
    for (size_t k = 0; k < stretches.size(); ++k)
    {
        if (k > 0)
        {
            add(' ', "\u2026");
        }
        S32               first = 0, last = 0;
        const std::string after = fixedLines(text, stretches[k], first, last);
        for (S32 line = first; line <= last && line < text.lineCount(); ++line)
        {
            add('-', text.line(line));
        }
        std::string_view rest = after;
        while (true)
        {
            const size_t cut = rest.find('\n');
            add('+', std::string(rest.substr(0, cut)));
            if (cut == std::string_view::npos)
            {
                break;
            }
            rest.remove_prefix(cut + 1);
        }
    }
    return says;
}

void ALCodeEditor::showFixPreview()
{
    const S32 index = fixesOpen() ? mFixList->chosen() : -1;
    if (index < 0 || index >= static_cast<S32>(mFixes.size()) || isNothing(mFixes[index]))
    {
        hideCompletionDoc();
        return;
    }
    std::vector<char> kinds;
    const std::string says = previewOf(document(), mFixes[index], kinds);
    ALTextView& box = *sideBox();
    box.setText(says);
    std::vector<ALTextView::Style> styles;
    for (S32 line = 0; line < box.document().lineCount() && line < static_cast<S32>(kinds.size()); ++line)
    {
        if (kinds[static_cast<size_t>(line)] == '-')
        {
            ALTextView::Style gone;
            gone.range = ALTextRange(ALTextPos(line, 0), box.document().lineEnd(line));
            gone.color = markColor(Mark::Error);
            styles.push_back(gone);
        }
        else if (kinds[static_cast<size_t>(line)] == '+')
        {
            styleAsCode(box, line, styles);
        }
    }
    box.setStyles(std::move(styles));
    placeSideBox(mFixList->getRect());
}

void ALCodeEditor::takeFix(S32 index)
{
    if (index < 0 || index >= static_cast<S32>(mFixes.size()))
    {
        return;
    }
    // Made by whoever gave it, which knows whether the text is still the
    // one it was made for.
    const LLSD value   = mFixes[index].value;
    const bool nothing = isNothing(mFixes[index]);
    closeFixes();
    if (mFixHandler && !nothing)
    {
        mFixHandler(value);
    }
}

bool ALCodeEditor::quickFix()
{
    const bool opened = openFixes(caret().line);
    if (!mActionRequest || !mFixHandler || isReadOnly())
    {
        return opened;
    }
    // The refactors come later, from the analyzer's thread: joined to the
    // list when they do, or making it where the line has no fixes.
    mActionsFor    = selection().normalised();
    mActionsCaret  = caret();
    mActionsWanted = true;
    mActionRequest(mActionsFor);
    return true;
}

bool ALCodeEditor::canQuickFix() const
{
    if (!mFixHandler || isReadOnly())
    {
        return false;
    }
    return mActionRequest || (mFixProvider && fixableAt(caret().line));
}

void ALCodeEditor::openCompletion()
{
    refreshCompletion();
}

// --- the name at the caret ---------------------------------------------------------

ALTextRange ALCodeEditor::identifierAt(const ALTextPos& at) const
{
    const ALTextDocument& doc  = document();
    const ALTextPos       pos  = doc.clamp(at);
    const std::string&    line = doc.line(pos.line);
    const S32             n    = static_cast<S32>(line.size());
    if (pos.column >= n || !alIdentifierByte(line[pos.column]))
    {
        return ALTextRange();
    }
    S32 begin = pos.column;
    S32 end   = pos.column + 1;
    while (begin > 0 && alIdentifierByte(line[begin - 1]))
    {
        --begin;
    }
    while (end < n && alIdentifierByte(line[end]))
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

namespace
{
    bool isStringKind(ALSyntaxKind kind)
    {
        return kind == ALSyntaxKind::String || kind == ALSyntaxKind::Escape;
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

bool ALCodeEditor::inProse(const ALTextPos& at)
{
    // The byte before the position: what was just typed, where a string
    // or a comment runs to the end of its line and a position past the
    // end is at no token.
    const S32 column = at.column - 1;
    if (column < 0 || at.line < 0 || at.line >= document().lineCount())
    {
        return false;
    }
    for (const ALSyntaxToken& token : highlighter().tokens(at.line))
    {
        if (token.begin <= column && column < token.end)
        {
            return quiet(token.kind);
        }
    }
    return false;
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

ALTextRange ALCodeEditor::stringAt(const ALTextPos& at) const
{
    const ALTextDocument& doc = document();
    const ALTextPos       pos = doc.clamp(at);
    ALCodeEditor&         me  = const_cast<ALCodeEditor&>(*this);

    auto runOn = [&me](S32 line, S32 column, S32& begin, S32& end) {
        // The run of string tokens around a column, or nothing.
        const std::vector<ALSyntaxToken>& tokens = me.highlighter().tokens(line);
        size_t                            at_t   = tokens.size();
        for (size_t t = 0; t < tokens.size(); ++t)
        {
            if (tokens[t].begin <= column && column < tokens[t].end && isStringKind(tokens[t].kind))
            {
                at_t = t;
                break;
            }
        }
        if (at_t == tokens.size())
        {
            return false;
        }
        size_t first = at_t, last = at_t;
        while (first > 0 && isStringKind(tokens[first - 1].kind) && tokens[first - 1].end == tokens[first].begin)
        {
            --first;
        }
        while (last + 1 < tokens.size() && isStringKind(tokens[last + 1].kind) && tokens[last + 1].begin == tokens[last].end)
        {
            ++last;
        }
        begin = tokens[first].begin;
        end   = tokens[last].end;
        return true;
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

std::string ALCodeEditor::stringSize(const ALTextRange& literal) const
{
    if (literal.empty())
    {
        return std::string();
    }
    const std::string written = document().text(literal);
    ALCodeEditor&     me      = const_cast<ALCodeEditor&>(*this);

    // What it holds: the bytes between the delimiters, with each escape
    // counted as what it stands for rather than as what it is written
    // as. The grammar has already said which stretches are escapes.
    S32 bytes = 0, characters = 0, escapes = 0;
    for (S32 line = literal.begin.line; line <= literal.end.line; ++line)
    {
        const std::string&                text   = document().line(line);
        const std::vector<ALSyntaxToken>& tokens = me.highlighter().tokens(line);
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
        says += ", " + alSaid("CodeStringWritten", "[COUNT] as written", args);
    }
    return says;
}

bool ALCodeEditor::mapMark(S32 line, LLColor4& color) const
{
    const Mark mark = markAt(line);
    if (mark == Mark::None)
    {
        return false;
    }
    color = markColor(mark);
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

bool ALCodeEditor::signatureHelp()
{
    if (!mSignatureRequest)
    {
        return false;
    }
    mSignatureRequest(caret());
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
    const ALTextRange  selection = this->selection();
    const ALTextPos    at        = std::min(selection.begin, selection.end);
    const std::string& line      = document().line(at.line);
    const std::string  indent    = line.substr(0, std::min(line.size(), line.find_first_not_of(" \t")));
    // The body read: the text as it will stand, and each placeholder's
    // place in it, by number, with what it held as written.
    struct Place
    {
        S32         number;
        ALTextRange range;
    };
    std::vector<Place>                          places;
    boost::unordered_flat_map<S32, std::string> held;
    std::optional<ALTextRange>                  end;
    std::string                                 text;
    ALTextPos                                   pos = at;
    auto                                        put = [&](char ch) {
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
    // Where the brace closing a placeholder opened at `open` is, minding
    // the ones inside it and the escaped; npos where it is never closed.
    auto closing = [&body](size_t open) {
        S32 depth = 0;
        for (size_t k = open; k < body.size(); ++k)
        {
            if (body[k] == '\\' && k + 1 < body.size() && (body[k + 1] == '$' || body[k + 1] == '}'))
            {
                ++k;
            }
            else if (body[k] == '$' && k + 1 < body.size() && body[k + 1] == '{')
            {
                ++depth;
                ++k;
            }
            else if (body[k] == '}' && --depth == 0)
            {
                return k;
            }
        }
        return std::string_view::npos;
    };
    // The body from `i` to `stop`, placeholders within placeholders read
    // the same way.
    std::function<void(size_t, size_t)> read = [&](size_t i, size_t stop) {
        while (i < stop)
        {
            const char ch = body[i];
            if (ch == '\\' && i + 1 < stop && (body[i + 1] == '$' || body[i + 1] == '}'))
            {
                put(body[i + 1]);
                i += 2;
                continue;
            }
            if (ch != '$' || i + 1 >= stop)
            {
                put(ch);
                ++i;
                continue;
            }
            if (body[i + 1] == '$')
            {
                put('$');
                i += 2;
                continue;
            }
            // $n, ${n} or ${n:text}
            const bool braced = body[i + 1] == '{';
            size_t     digits = i + (braced ? 2 : 1);
            size_t     past   = digits;
            while (past < stop && isdigit(static_cast<unsigned char>(body[past])))
            {
                ++past;
            }
            const size_t close = braced ? closing(i) : past;
            if (past == digits || (braced && (close == std::string_view::npos || close > stop || (body[past] != ':' && body[past] != '}'))))
            {
                put(ch);
                ++i;
                continue;
            }
            const S32       number = atoi(std::string(body.substr(digits, past - digits)).c_str());
            const ALTextPos from   = pos;
            const size_t    began  = text.size();
            if (braced && body[past] == ':')
            {
                read(past + 1, close);
            }
            else if (const auto first = held.find(number); first != held.end() && number != 0)
            {
                // A mirror written bare shows what its first holds, as it
                // stands: indented already.
                for (const char c : first->second)
                {
                    text += c;
                    if (c == '\n')
                    {
                        ++pos.line;
                        pos.column = 0;
                    }
                    else
                    {
                        ++pos.column;
                    }
                }
            }
            if (number == 0)
            {
                end = ALTextRange(from, pos);
            }
            else
            {
                places.push_back(Place{ number, ALTextRange(from, pos) });
                held.emplace(number, text.substr(began));
            }
            i = braced ? close + 1 : past;
        }
    };
    read(0, body.size());
    insertText(text);
    // Each number's first place the stop, the rest its mirrors; the stops
    // in the order of their numbers.
    std::stable_sort(places.begin(), places.end(), [](const Place& a, const Place& b) { return a.number < b.number; });
    std::vector<ALTextRange> ranges;
    std::vector<Mirror>      mirrors;
    for (size_t k = 0; k < places.size(); ++k)
    {
        if (k > 0 && places[k].number == places[k - 1].number)
        {
            mirrors.push_back(Mirror{ static_cast<S32>(ranges.size()) - 1, places[k].range });
            continue;
        }
        ranges.push_back(places[k].range);
    }
    const ALTextRange landing = end.value_or(ALTextRange(pos, pos));
    if (!ranges.empty())
    {
        setPlaceholders(std::move(ranges), landing.begin);
        mMirrors = std::move(mirrors);
        // $0's own text chosen as the caret lands there, where it is on
        // one line.
        mLandingLength = landing.begin.line == landing.end.line ? landing.end.column - landing.begin.column : 0;
    }
    else
    {
        setSelection(landing);
    }
}

void ALCodeEditor::syncMirrors(S32 index)
{
    if (index < 0 || index >= static_cast<S32>(mPlaceholders.size()) || mMirrors.empty())
    {
        return;
    }
    // From the last to the first, so that none moves one still to do; one
    // step to undo, and the selection as it was.
    const std::string        wanted = document().text(mPlaceholders[static_cast<size_t>(index)]);
    std::vector<S32>         order;
    for (S32 k = 0; k < static_cast<S32>(mMirrors.size()); ++k)
    {
        if (mMirrors[static_cast<size_t>(k)].of == index && document().text(mMirrors[static_cast<size_t>(k)].range) != wanted)
        {
            order.push_back(k);
        }
    }
    if (order.empty())
    {
        return;
    }
    std::sort(order.begin(), order.end(), [this](S32 a, S32 b) { return mMirrors[static_cast<size_t>(b)].range.begin < mMirrors[static_cast<size_t>(a)].range.begin; });
    const ALTextRange was = selection();
    undoJournal().beginGroup();
    for (const S32 k : order)
    {
        mSyncingMirror = k;
        edit(mMirrors[static_cast<size_t>(k)].range, wanted);
    }
    mSyncingMirror = -1;
    undoJournal().endGroup();
    placeSelection(document().clamp(was.begin), document().clamp(was.end));
    afterEdit();
}

void ALCodeEditor::dropPlaceholdersLeft()
{
    if (mPlaceholders.empty() || mSyncingMirror >= 0)
    {
        return;
    }
    // Anywhere from the first stop's line to where the call or the
    // snippet ends is still filling it in: a snippet's stops may be on
    // several lines.
    S32 first = mPlaceholdersAfter.line;
    S32 last  = mPlaceholdersAfter.line;
    for (const ALTextRange& r : mPlaceholders)
    {
        first = llmin(first, r.begin.line);
        last  = llmax(last, r.end.line);
    }
    if (caret().line < first || caret().line > last)
    {
        clearPlaceholders();
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
    mMirrors.clear();
    mLandingLength     = 0;
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
    // What was typed over the one being left, in its mirrors.
    syncMirrors(mPlaceholderAt);
    if (to >= static_cast<S32>(mPlaceholders.size()))
    {
        // Past the last: after the call, done -- $0's text chosen.
        const ALTextPos after   = mPlaceholdersAfter;
        const S32       landing = mLandingLength;
        clearPlaceholders();
        setSelection(ALTextRange(after, ALTextPos(after.line, after.column + landing)));
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
    mMirrors.clear();
    mLandingLength = 0;
    mPlaceholders.clear();
    mPlaceholderAt = -1;
}

// --- input -------------------------------------------------------------------

void ALCodeEditor::dropTyping()
{
    closeCompletion();
    hideSignature();
    clearPlaceholders();
}

bool ALCodeEditor::handleKeyHere(KEY key, MASK mask)
{
    hideCard();
    // The fixes listed take the keys that walk them and take one, in any
    // mode a modal keymap is in -- the list was asked for; any other key
    // lets them go and is the text's.
    if (fixesOpen())
    {
        if (mask == MASK_NONE)
        {
            switch (key)
            {
                case KEY_ESCAPE:
                    closeFixes();
                    return true;
                case KEY_UP:
                    mFixList->moveChoice(-1, true);
                    return true;
                case KEY_DOWN:
                    mFixList->moveChoice(1, true);
                    return true;
                case KEY_PAGE_UP:
                    mFixList->moveChoice(-COMPLETION_ROWS, false);
                    return true;
                case KEY_PAGE_DOWN:
                    mFixList->moveChoice(COMPLETION_ROWS, false);
                    return true;
                case KEY_RETURN:
                case KEY_TAB:
                    takeFix(mFixList->chosen());
                    return true;
                default:
                    break;
            }
        }
        closeFixes();
    }
    // Outside a modal keymap's inserting modes a key is a command, and
    // nothing the typing puts up -- the list, the signature, a snippet's
    // stops -- is in play: the keymap has the key, whatever it is.
    if (!typingText())
    {
        dropTyping();
        return ALTextView::handleKeyHere(key, mask);
    }
    // Under a modal keymap one Escape closes whatever the typing has up
    // and leaves the inserting mode as well, as vim's own popup menu has
    // it, rather than taking a second press to reach the keymap.
    if (key == KEY_ESCAPE && mask == MASK_NONE && modalKeymap())
    {
        dropTyping();
        return ALTextView::handleKeyHere(key, mask);
    }
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
            syncMirrors(mPlaceholderAt);
            clearPlaceholders();
            return true;
        }
    }
    // Under a modal keymap Control-N and Control-P walk the list, as they
    // walk vim's own popup menu -- the Control key itself on a Mac, not
    // Command.
#if LL_DARWIN
    constexpr MASK REAL_CONTROL = MASK_MAC_CONTROL;
#else
    constexpr MASK REAL_CONTROL = MASK_CONTROL;
#endif
    if (completionOpen() && modalKeymap() && mask == REAL_CONTROL && (key == 'N' || key == 'P'))
    {
        mCompletionList->moveChoice(key == 'N' ? 1 : -1, true);
        return true;
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
                if (!mAcceptOnEnter)
                {
                    // A new line, as the key says; the list goes.
                    closeCompletion();
                    break;
                }
                acceptCompletion();
                return true;
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
    else if (completionOpen() && key != KEY_BACKSPACE && !(mask == MASK_SHIFT && key >= 0x20 && key < KEY_SPECIAL))
    {
        // A capital is a character like any other: what it is decides,
        // when it comes, rather than the shift closing the list under it.
        closeCompletion();
    }
    if (mSignature && key == KEY_ESCAPE && mask == MASK_NONE)
    {
        hideSignature();
        return true;
    }
    if (key == KEY_BACKSPACE && mask == MASK_NONE && mAutoClose && deletePair())
    {
        if (mSignature && mSignatureRequest)
        {
            mSignatureRequest(caret());
        }
        return true;
    }
    const bool taken = ALTextView::handleKeyHere(key, mask);
    if (!typingText())
    {
        // The key left the inserting mode: Control-[ or Control-C in vim.
        dropTyping();
        return taken;
    }
    if (taken && mSignature && mSignatureRequest && (key == KEY_BACKSPACE || key == KEY_DELETE))
    {
        mSignatureRequest(caret());
    }
    return taken;
}

bool ALCodeEditor::handleUnicodeCharHere(llwchar uni_char)
{
    const bool typing   = typingText();
    const bool was_open = completionOpen();
    const bool paired   = typing && mAutoClose && uni_char < 0x80 && !isReadOnly() && typePair(static_cast<char>(uni_char));
    if (!paired && !ALTextView::handleUnicodeCharHere(uni_char))
    {
        return false;
    }
    if (!typing || !typingText())
    {
        // A command to a modal keymap -- a motion, an operator, a line
        // being typed -- put nothing in to complete or to call.
        dropTyping();
        return true;
    }
    const bool identifier = uni_char < 0x80 && alIdentifierByte(static_cast<char>(uni_char));
    // In a comment or a string what is typed is prose: the list does not
    // open on its own there, where a Return meant as a new line would
    // otherwise put a call into the comment. Asked for, it still opens.
    const bool prose = mAutoComplete && !was_open && inProse(caret());
    if (uni_char == '.' && mAutoComplete && !prose && caret().column >= 2 && !identifierAt(ALTextPos(caret().line, caret().column - 2)).empty())
    {
        // A member is coming: what there is to choose from, at once.
        openCompletion();
    }
    else if (!identifier)
    {
        closeCompletion();
    }
    else if (!was_open && mAutoComplete && !prose && static_cast<S32>(wordBeforeCaret().size()) >= mCompleteAfter)
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

bool ALCodeEditor::typePair(char c)
{
    const std::shared_ptr<const ALSyntaxGrammar> grammar = highlighter().grammar();
    if (!grammar || grammar->pairs().empty())
    {
        return false;
    }
    const auto&        pairs = grammar->pairs();
    const ALTextPos    at    = caret();
    const std::string& line  = document().line(at.line);
    const char         next  = at.column < static_cast<S32>(line.size()) ? line[at.column] : '\0';
    // Over the closer typing put in, rather than a second one.
    if (!hasSelection() && next == c)
    {
        const auto put = std::find(mAutoClosed.begin(), mAutoClosed.end(), at);
        if (put != mAutoClosed.end())
        {
            for (const auto& [open, close] : pairs)
            {
                if (close == c)
                {
                    mAutoClosed.erase(put);
                    setCaret(ALTextPos(at.line, at.column + 1));
                    return true;
                }
            }
        }
    }
    for (const auto& [open, close] : pairs)
    {
        if (c != open)
        {
            continue;
        }
        if (hasSelection())
        {
            // The selection wrapped in the pair, and still chosen inside it.
            const ALTextRange sel   = selection().normalised();
            const std::string inner = document().text(sel);
            insertText(std::string(1, open) + inner + std::string(1, close));
            const ALTextPos end = caret();
            setSelection(ALTextRange(ALTextPos(sel.begin.line, sel.begin.column + 1), ALTextPos(end.line, end.column - 1)));
            return true;
        }
        // Not in a comment or a string, where it is prose; and only before
        // a blank, the line's end, or what closes or ends -- a bracket
        // opened before a word is about that word.
        if (inProse(at))
        {
            return false;
        }
        bool room = next == '\0' || isspace(static_cast<unsigned char>(next)) || strchr(";,", next) != nullptr;
        for (const auto& [o, closer] : pairs)
        {
            room = room || (next == closer && o != closer);
        }
        if (!room)
        {
            return false;
        }
        // A quote after a letter is an apostrophe, and after itself is the
        // end of an empty string.
        if (open == close && at.column > 0 && (alIdentifierByte(line[at.column - 1]) || line[at.column - 1] == open))
        {
            return false;
        }
        insertText(std::string(1, open) + std::string(1, close));
        const ALTextPos inside(at.line, at.column + 1);
        setCaret(inside);
        mAutoClosed.push_back(inside);
        return true;
    }
    return false;
}

bool ALCodeEditor::deletePair()
{
    const std::shared_ptr<const ALSyntaxGrammar> grammar = highlighter().grammar();
    const ALTextPos                              at      = caret();
    const auto                                   put     = std::find(mAutoClosed.begin(), mAutoClosed.end(), at);
    if (!grammar || hasSelection() || put == mAutoClosed.end() || at.column == 0)
    {
        return false;
    }
    const std::string& line = document().line(at.line);
    if (at.column >= static_cast<S32>(line.size()))
    {
        return false;
    }
    for (const auto& [open, close] : grammar->pairs())
    {
        if (line[at.column - 1] == open && line[at.column] == close)
        {
            mAutoClosed.erase(put);
            setSelection(ALTextRange(ALTextPos(at.line, at.column - 1), ALTextPos(at.line, at.column + 1)));
            insertText(std::string());
            return true;
        }
    }
    return false;
}

bool ALCodeEditor::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // Something drawn over the text that is a view of its own -- the find
    // bar, a list, the card -- has the press before the text, the gutter
    // and the pinned headers: the find bar's arrows are over the headers.
    if (overlayAt(x, y))
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }
    hideCard();
    closeCompletion();
    closeFixes();
    clearPlaceholders();
    mAutoClosed.clear();
    const LLRect text         = textRect();
    const S32    gutter_right = leftEdge() + gutterWidth();
    // The mark column of a line whose problems offer fixes: its fixes,
    // listed as Control-. would list them.
    if (mShowLineNumbers && x >= leftEdge() && x < leftEdge() + MARK_INSET + MARK_SIZE + GUTTER_PAD / 2)
    {
        const S32 line = posAtLocal(text.mLeft, y, false).line;
        // On the caret's line the lightbulb, which is Control-.: the
        // refactors asked for with the fixes.
        if (fixableAt(line) && (line == caret().line ? quickFix() : openFixes(line)))
        {
            return true;
        }
    }
    if (mShowFoldMarkers && x < gutter_right - heatWidth() && x >= gutter_right - heatWidth() - FOLD_COLUMN)
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
    // Control-click on a name -- Command-click on a Mac -- goes to where it
    // is declared, as in every editor of code.
    if (mask == MASK_CONTROL && mSymbolRequest && text.pointInRect(x, y))
    {
        const ALTextRange word = identifierAt(posAtLocal(x, y, false));
        if (!word.empty())
        {
            setFocus(true);
            placeCaret(word.begin, false);
            performSymbol(ALEditorCommand::GoToDefinition);
            return true;
        }
    }
    return ALTextView::handleMouseDown(x, y, mask);
}

bool ALCodeEditor::handleToolTip(S32 x, S32 y, MASK mask)
{
    // Over the find bar or a list, theirs; the card says nothing more.
    if (overlayAt(x, y) && !(cardShown() && mCard->getRect().pointInRect(x, y)))
    {
        return ALTextView::handleToolTip(x, y, mask);
    }
    const LLRect text = textRect();
    // A mark in the gutter says what is on its line: every problem there;
    // the strip of heat at its edge, what the line came to.
    if (x >= leftEdge() && x < leftEdge() + gutterWidth() && text.mBottom <= y && y <= text.mTop)
    {
        const S32 line = posAtLocal(text.mLeft, y, false).line;
        if (mHeatShown && x >= leftEdge() + gutterWidth() - heatWidth())
        {
            if (line >= 0 && line < static_cast<S32>(mAsides.size()) && !mAsides[static_cast<size_t>(line)].heatTip.empty())
            {
                LLToolTipMgr::instance().show(mAsides[static_cast<size_t>(line)].heatTip);
                return true;
            }
            return ALTextView::handleToolTip(x, y, mask);
        }
        std::vector<CardProblem> problems;
        for (const Decoration& d : mDecorations)
        {
            const ALTextRange range = d.range.normalised();
            if (!d.message.empty() && range.begin.line <= line && line <= range.end.line)
            {
                problems.push_back({ d.message, d.color });
            }
        }
        if (problems.empty())
        {
            return ALTextView::handleToolTip(x, y, mask);
        }
        showCard(ALTextRange(ALTextPos(line, 0), ALTextPos(line, 0)), std::string(), problems);
        return true;
    }
    if (cardShown() && mCard->getRect().pointInRect(x, y))
    {
        // Resting on the card itself: it stays, and says nothing more.
        return true;
    }
    // A note after a line: what it stands for, at more length.
    if (const S32 line = noteAtLocal(x, y); line >= 0 && !mAsides[static_cast<size_t>(line)].noteTip.empty())
    {
        LLToolTipMgr::instance().show(mAsides[static_cast<size_t>(line)].noteTip);
        return true;
    }
    // A hint the text can say: how it is written in.
    if (const S32 inlay = isReadOnly() ? -1 : inlayAtLocal(x, y); inlay >= 0 && !mInlays[static_cast<size_t>(inlay)].insert.empty())
    {
        LLStringUtil::format_map_t args;
        args["[TEXT]"] = mInlays[static_cast<size_t>(inlay)].insert;
#if LL_DARWIN
        LLToolTipMgr::instance().show(alSaid("CodeInlayWriteMac", "Command-double-click to write '[TEXT]' in", args));
#else
        LLToolTipMgr::instance().show(alSaid("CodeInlayWrite", "Ctrl-double-click to write '[TEXT]' in", args));
#endif
        return true;
    }
    if (!text.pointInRect(x, y) || (mCompletionList && mCompletionList->getVisible() && mCompletionList->getRect().pointInRect(x, y)))
    {
        return ALTextView::handleToolTip(x, y, mask);
    }
    if (!mHoverCards)
    {
        return ALTextView::handleToolTip(x, y, mask);
    }
    if (mHoverDelay >= 0.f && mMouseRest.getElapsedTimeF32() < mHoverDelay)
    {
        // The card comes when the mouse has rested as long as was asked,
        // which draw watches for, rather than at the tooltip's own time.
        return true;
    }
    return hoverCardAt(x, y) || ALTextView::handleToolTip(x, y, mask);
}

bool ALCodeEditor::hoverCardAt(S32 x, S32 y)
{
    const ALTextPos at = posAtLocal(x, y, false);
    // A problem under the mouse says what it is, and the word what it is
    // as well: what is wrong with a call is read against what it takes.
    ALTextRange                    about;
    const std::vector<CardProblem> problems = problemsUnder(at, about);
    std::string                    says;
    std::vector<CardLink>          links;
    const ALTextRange              word = identifierAt(at);
    // A string literal says its own size, anywhere in it -- the space
    // after its comma as much as the word before -- since what the
    // analyzer has to say about one is that it is a string, which the
    // quotes said already.
    {
        const ALTextRange literal = stringAt(at);
        const std::string size    = stringSize(literal);
        if (!size.empty())
        {
            says  = alSaid("CodeStringHead", "string") + "\n" + size;
            about = about.empty() ? literal : ALTextRange(std::min(about.begin, literal.begin), std::max(about.end, literal.end));
        }
    }
    if (says.empty() && mHover && !word.empty())
    {
        if (mHover(at, document().text(word), says))
        {
            about = about.empty() ? word : ALTextRange(std::min(about.begin, word.begin), std::max(about.end, word.end));
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
                links = mHoverLinks;
                about = about.empty() ? word : ALTextRange(std::min(about.begin, word.begin), std::max(about.end, word.end));
            }
        }
        else if (mHoverRequest)
        {
            mHoverAsked        = word;
            mHoverAskedVersion = version;
            mHoverAnswer.clear();
            mHoverLinks.clear();
            mHoverRequest(word.begin, document().text(word));
        }
    }
    if (says.empty() && problems.empty())
    {
        return false;
    }
    showCard(about, says, problems, links);
    return true;
}

std::vector<ALCodeEditor::CardProblem> ALCodeEditor::problemsUnder(const ALTextPos& at, ALTextRange& about) const
{
    std::vector<CardProblem> problems;
    for (const Decoration& d : mDecorations)
    {
        const ALTextRange range = d.range.normalised();
        if (!d.message.empty() && range.begin <= at && at < range.end)
        {
            problems.push_back({ d.message, d.color });
            about = about.empty() ? range : ALTextRange(std::min(about.begin, range.begin), std::max(about.end, range.end));
        }
    }
    return problems;
}

// static
const std::string& ALCodeEditor::deprecatedNote()
{
    static const std::string note = alSaid("CodeDeprecated", "(deprecated)");
    return note;
}

ALSyntaxKind ALCodeEditor::semanticKindAt(const ALTextPos& at) const
{
    auto found = std::upper_bound(mSemantics.begin(), mSemantics.end(), at, [](const ALTextPos& pos, const SemanticToken& t) { return pos < t.range.begin; });
    if (found == mSemantics.begin())
    {
        return ALSyntaxKind::Text;
    }
    --found;
    return found->range.begin <= at && at < found->range.end ? found->kind : ALSyntaxKind::Text;
}

void ALCodeEditor::styleAsCode(const ALTextView& view, S32 line, std::vector<ALTextView::Style>& styles, std::string_view name, ALSyntaxKind kind)
{
    const std::string& text = view.document().line(line);
    std::vector<ALSyntaxToken> tokens;
    if (std::shared_ptr<const ALSyntaxGrammar> grammar = highlighter().grammar())
    {
        ALSyntaxState state = grammar->initialState();
        grammar->lexLine(text, state, tokens, highlighter().words());
    }
    if (tokens.empty())
    {
        ALTextView::Style whole;
        whole.range = ALTextRange(ALTextPos(line, 0), ALTextPos(line, static_cast<S32>(text.size())));
        whole.font  = getFont();
        styles.push_back(whole);
        return;
    }
    // The tokens cover the line without a gap, so each carries the face.
    for (const ALSyntaxToken& token : tokens)
    {
        ALTextView::Style one;
        one.range = ALTextRange(ALTextPos(line, token.begin), ALTextPos(line, token.end));
        one.font  = getFont();
        ALSyntaxKind shown = token.kind;
        if (shown == ALSyntaxKind::Text && kind != ALSyntaxKind::Text && !name.empty() && std::string_view(text).substr(token.begin, token.end - token.begin) == name)
        {
            shown = kind;
        }
        if (shown != ALSyntaxKind::Text)
        {
            one.color = colorForKind(shown);
        }
        styles.push_back(std::move(one));
    }
}

void ALCodeEditor::showCard(const ALTextRange& about, const std::string& says, const std::vector<CardProblem>& problems,
                            const std::vector<CardLink>& links)
{
    // The card is one of the studio's small floating things, and they
    // all have the one look: the ground a shade off the text's own, and
    // a frame in a quarter of the ink, which `draw` puts on over it.
    // Taken afresh each time, since the colour table may have moved.
    const LLColor4         ground    = paint(Paint::Widget);
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
        mCard->onLinkClicked([this](const ALTextView::Substitution& link) {
            if (!link.url.empty())
            {
                LLUrlAction::clickAction(link.url, false);
                return;
            }
            // A fix: made by whoever gave it, and the card goes, being
            // about a problem the fix is to take away.
            if (link.value.isMap() && link.value.has("fix"))
            {
                const LLSD value = link.value["fix"];
                hideCard();
                if (mFixHandler)
                {
                    mFixHandler(value);
                }
                return;
            }
            // A way somewhere the caller gave: gone to, and the card with
            // it, since it is about where the caret is no longer.
            if (link.value.isDefined() && mCardLinkHandler)
            {
                const LLSD value = link.value;
                hideCard();
                mCardLinkHandler(value);
            }
        });
        addChild(mCard);
    }
    mCard->setBackgroundColor(ground);
    mCard->setTextColor(textColor());
    // The problems, a line or more each, then a blank line, then what the
    // word is, its first line the head.
    std::string                                 all;
    std::vector<std::pair<S32, const LLColor4*>> problem_lines;
    S32                                         line_count = 0;
    for (const CardProblem& problem : problems)
    {
        if (!all.empty())
        {
            all += "\n";
        }
        all += problem.message;
        const S32 made = 1 + static_cast<S32>(std::count(problem.message.begin(), problem.message.end(), '\n'));
        for (S32 i = 0; i < made; ++i)
        {
            problem_lines.emplace_back(line_count++, &problem.color);
        }
    }
    // What would put them right, each a link on a line of its own under
    // them that makes it: the fixes of the line they are on.
    std::vector<std::pair<S32, LLSD>> fix_lines;
    if (!problems.empty() && mFixProvider && mFixHandler && !isReadOnly())
    {
        std::vector<Fix> fixes;
        mFixProvider(about.begin.line, fixes);
        rankFixes(fixes);
        for (const Fix& fix : fixes)
        {
            LLStringUtil::format_map_t args;
            args["[TITLE]"] = fix.title;
            all += "\n" + alSaid("CodeFixLink", "Fix: [TITLE]", args);
            fix_lines.emplace_back(line_count++, fix.value);
        }
    }
    S32 head_line = -1;
    if (!says.empty())
    {
        if (!all.empty())
        {
            all += "\n\n";
            line_count += 1;
        }
        head_line = line_count;
        all += says;
    }
    if (cardShown() && about == mCardAbout && all == mCard->text())
    {
        // The mouse resting on again: the card is up already.
        return;
    }
    mCardAbout = about;
    // The words: each problem in its colour, the head as code, a note
    // about deprecation in the warning colour, every URL a link.
    mCard->setText(all);
    std::vector<ALTextView::Style> styles;
    for (const auto& [line, color] : problem_lines)
    {
        ALTextView::Style one;
        one.range = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
        one.color = *color;
        styles.push_back(one);
    }
    if (head_line >= 0)
    {
        // The name in the head coloured as the text colours it where the
        // analyzer said what it is: a global, a parameter, a function of
        // the script's own, which the grammar alone does not know.
        const ALTextRange word = mMouseX >= 0 ? identifierAt(posAtLocal(mMouseX, mMouseY, false)) : identifierAt(about.begin);
        styleAsCode(*mCard, head_line, styles, document().text(word), word.empty() ? ALSyntaxKind::Text : semanticKindAt(word.begin));
    }
    const S32 lines = mCard->document().lineCount();
    for (S32 line = head_line + 1; line < lines && head_line >= 0; ++line)
    {
        if (mCard->document().line(line).find(deprecatedNote()) != std::string::npos)
        {
            ALTextView::Style note;
            note.range = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
            note.color = markColor(Mark::Warning);
            styles.push_back(note);
        }
    }
    mCard->setStyles(std::move(styles));
    for (const auto& [line, value] : fix_lines)
    {
        ALTextView::Substitution fix;
        fix.range = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
        fix.link  = true;
        fix.value = LLSD().with("fix", value);
        mCard->addSubstitution(std::move(fix));
    }
    // The caller's own links, each on the line that says it, below the
    // head; then every URL.
    for (const CardLink& link : links)
    {
        for (S32 line = llmax(0, head_line + 1); line < lines; ++line)
        {
            if (mCard->document().line(line) == link.line)
            {
                ALTextView::Substitution way;
                way.range   = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
                way.link    = true;
                way.tooltip = link.tooltip;
                way.value   = link.value;
                mCard->addSubstitution(std::move(way));
                break;
            }
        }
    }
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
    // Laid out before it is measured: the layout guesses the width of a
    // line it has not done yet by the width of a space, which is far
    // narrower than the face the head line is in, and the card would
    // come out a few characters wide and wrap the one word in it. These
    // are a handful of lines, not a script.
    for (S32 line = 0; line < lines; ++line)
    {
        mCard->layout().line(line);
    }
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
    // A list of fixes is about a row the scroll takes away.
    closeFixes();
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

void ALCodeEditor::supplyHover(const ALTextPos& at, const std::string& text, std::vector<CardLink> links)
{
    if (text.empty() || mHoverAsked.empty() || at != mHoverAsked.begin || document().version() != mHoverAskedVersion)
    {
        return;
    }
    // Kept for the word, and shown now if the mouse is still on it.
    mHoverAnswer = text;
    mHoverLinks  = std::move(links);
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
    ALTextRange                    about;
    const std::vector<CardProblem> problems = problemsUnder(under, about);
    showCard(about.empty() ? word : ALTextRange(std::min(about.begin, word.begin), std::max(about.end, word.end)), text, problems, mHoverLinks);
}

bool ALCodeEditor::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    if (overlayAt(x, y))
    {
        return ALTextView::handleDoubleClick(x, y, mask);
    }
    // A hint the text can say, written in where it stands with Control
    // (Command on a Mac) held: a type after a name declared without one.
    // A double-click alone on it takes the name it stands beside, as one
    // meant for the name, and landing on the hint drawn after it, would:
    // the text is not changed by a click that only meant to choose.
    const S32 inlay = inlayAtLocal(x, y);
    if (inlay >= 0)
    {
        const InlayHint& hint = mInlays[static_cast<size_t>(inlay)];
        setFocus(true);
        if ((mask & MASK_CONTROL) && !isReadOnly() && !hint.insert.empty())
        {
            writeInlay(inlay);
            return true;
        }
        const ALTextPos   beside = hint.before || hint.at.column == 0 ? hint.at : document().prevCluster(hint.at);
        const ALTextRange name   = identifierAt(beside);
        if (!name.empty())
        {
            setSelection(name);
            armTripleClick();
            return true;
        }
    }
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
    if (x != mMouseX || y != mMouseY)
    {
        // Moved: the rest the card waits for starts again.
        mMouseRest.reset();
        mHoverTried = false;
    }
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
    // Over the find bar or a list, the mouse is not resting on the text
    // under it: no card comes for what is hidden there.
    if (overlayAt(x, y))
    {
        mMouseX          = -1;
        mMouseY          = -1;
        mGutterHover     = false;
        mGutterHoverLine = -1;
        return ALTextView::handleHover(x, y, mask);
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
    mMouseX          = -1;
    mMouseY          = -1;
    hideCard();
    ALTextView::onMouseLeave(x, y, mask);
}

// --- signature help -------------------------------------------------------------

void ALCodeEditor::showSignature(const ALTextPos& at, Signature signature)
{
    if (!typingText())
    {
        // An answer that came after a modal keymap stopped inserting.
        return;
    }
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
    const S32        wanted = llmax(font->getWidth(sig.label), docs ? font->getWidth(doc_line) : 0) + 2 * SIGNATURE_PAD;
    const S32        height = line_h * (docs ? 2 : 1) + 2 * SIGNATURE_PAD;

    // Above the caret's row, left with the call's column, kept inside the
    // view; under the row where above would run off the top.
    S32       row;
    const F32 x    = layout().xOf(mSignatureAt.line, mSignatureAt.column, &row);
    const S32 top  = screenTopOf(text, mSignatureAt.line, row);
    const LLRect local = getLocalRect();
    // No wider than the view. A box sized to a signature longer than the
    // window ran off the right edge and was cut there by the view's own
    // rect, silently -- and a call with a long list of parameters is the
    // one whose signature was worth reading.
    const S32 width = llmin(wanted, llmax(4 * SIGNATURE_PAD, local.getWidth()));
    const S32 room  = width - 2 * SIGNATURE_PAD;
    S32       left = llclamp(static_cast<S32>(static_cast<F32>(text.mLeft) - scrollX() + x), local.mLeft, llmax(local.mLeft, local.mRight - width));
    LLRect    box  = (top + height <= local.mTop) ? LLRect(left, top + height, left + width, top)
                                                  : LLRect(left, top - row_h, left + width, top - row_h - height);

    const LLColor4 ink    = textColor() % alpha;
    const LLColor4 active = mBracketMatchColor.get() % alpha;
    const LLColor4 faint  = lineNumberColor() % alpha;
    gl_rect_2d(box, paint(Paint::Widget) % alpha, true);
    gl_rect_2d(box, paint(Paint::WidgetBorder) % alpha, false);

    // The label in three pieces, the active parameter in its own colour.
    const F32 baseline = static_cast<F32>(box.mTop - SIGNATURE_PAD - llround(font->getAscenderHeight()));
    S32       begin = -1, end = -1;
    if (sig.active >= 0 && sig.active < static_cast<S32>(sig.parameters.size()))
    {
        begin = sig.parameters[sig.active].first;
        end   = sig.parameters[sig.active].second;
    }
    // Where the label is longer than the room, it is scrolled so that the
    // parameter being filled in is in the box. The head of a signature is
    // the part already typed, so it is the part to give up; what runs off
    // the left edge under the clip reads as more of it being there.
    const F32 label_w = static_cast<F32>(font->getWidth(sig.label));
    F32       shift   = 0.f;
    if (label_w > static_cast<F32>(room))
    {
        const F32 through = end > 0 ? static_cast<F32>(font->getWidth(sig.label.substr(0, static_cast<size_t>(end)))) : label_w;
        shift = llclamp(through - static_cast<F32>(room), 0.f, label_w - static_cast<F32>(room));
    }
    F32 pen = static_cast<F32>(box.mLeft + SIGNATURE_PAD) - shift;
    auto piece = [&](S32 from, S32 to, const LLColor4& color) {
        if (to <= from)
        {
            return;
        }
        const std::string part = sig.label.substr(from, to - from);
        font->renderUTF8(part, 0, pen, baseline, color, LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
        pen += static_cast<F32>(font->getWidth(part));
    };
    {
        LLLocalClipRect clip(LLRect(box.mLeft + SIGNATURE_PAD, box.mTop - 1, box.mRight - SIGNATURE_PAD, box.mBottom + 1));
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
    }
    if (docs)
    {
        // The documentation's first line reads from its own start, so it
        // ends in an ellipsis rather than being scrolled.
        font->renderUTF8(doc_line, 0, static_cast<F32>(box.mLeft + SIGNATURE_PAD), baseline - static_cast<F32>(line_h), faint,
                         LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX, room, nullptr, true);
    }
}

void ALCodeEditor::draw()
{
    // A signature is about a call on the caret's line; anywhere else it
    // is stale. (The placeholders are let go of as the caret leaves their
    // lines, where it moves: dropPlaceholdersLeft.)
    if (mSignature && !signatureShown())
    {
        hideSignature();
    }
    // A closer put in is typed over only on its own line.
    mAutoClosed.erase(std::remove_if(mAutoClosed.begin(), mAutoClosed.end(), [this](const ALTextPos& at) { return at.line != caret().line; }),
                      mAutoClosed.end());
    // The mouse rested long enough on the text: its card, once.
    if (mHoverCards && mHoverDelay >= 0.f && !mHoverTried && mMouseX >= 0 && mMouseRest.getElapsedTimeF32() >= mHoverDelay && !cardShown() &&
        textRect().pointInRect(mMouseX, mMouseY) && !(mCompletionList && mCompletionList->getVisible() && mCompletionList->getRect().pointInRect(mMouseX, mMouseY)))
    {
        mHoverTried = true;
        hoverCardAt(mMouseX, mMouseY);
    }
    ALTextView::draw();
    if (mSignature)
    {
        drawSignature(textRect());
    }
    if (cardShown())
    {
        // Over the card rather than under it, so that its own ground
        // cannot paint the frame out along the edge it shares.
        gl_rect_2d(mCard->getRect(), paint(Paint::WidgetBorder) % getDrawContext().mAlpha, false);
    }
    if (mCompletionDoc && mCompletionDoc->getVisible())
    {
        gl_rect_2d(mCompletionDoc->getRect(), paint(Paint::WidgetBorder) % getDrawContext().mAlpha, false);
    }
}

void ALCodeEditor::onFocusLost()
{
    closeCompletion();
    closeFixes();
    ALTextView::onFocusLost();
}
