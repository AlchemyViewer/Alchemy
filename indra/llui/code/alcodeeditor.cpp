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

#include "alchangepeek.h"
#include "aldiffcolors.h"
#include "aldiffedit.h"
#include "allinebreaks.h"

#include "alplace.h"
#include "alsaid.h"
#include "alsmartselect.h"
#include "alsurface.h"

#include "altextchars.h"
#include "llfocusmgr.h"
#include "lllocalcliprect.h"
#include "llstl.h"
#include "llrender2dutils.h"
#include "alchoicepopup.h"
#include "llstring.h"
#include "lltooltip.h"
#include "llui.h"
#include "lluicolortable.h"
#include "llurlaction.h"
#include "lluictrlfactory.h"
#include "llwindow.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <functional>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

static LLDefaultChildRegistry::Register<ALCodeEditor> r("code_editor");

namespace
{
    const S32 GUTTER_PAD  = 6;
    const S32 MARK_SIZE   = 10;
    // A warning's underline: dashes; a note's: dots.
    const S32 UNDERLINE_DASH     = 4;
    const S32 UNDERLINE_DASH_GAP = 2;
    const S32 UNDERLINE_DOT      = 2;
    const S32 UNDERLINE_DOT_GAP  = 2;
    const S32 MARK_INSET  = 3;
    // How far in from the gutter's edge a press is on a changed line's
    // bar, which is drawn two wide: wider than it, to be hit.
    const S32 CHANGE_BAR_HIT = 5;
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
    // The completions' popup and the fixes', by their names among the
    // editor's children.
    const char* const COMPLETION_POPUP = "completion_popup";
    const char* const FIX_POPUP        = "fix_popup";
    // What a line's problems offer, as the gutter keeps it.
    const U8  FIXES_ANY          = 1;
    const U8  FIXES_CHANGE       = 2;
    // How long the caret rests on a name before its other places are lit.
    const F32 OCCURRENCES_REST   = 0.25f;

    const char* const MARK_COLOR_NAMES[] = { "TextFgColor", "CodeMarkNote", "CodeMarkWarning", "CodeMarkError", "CodeMarkRuntime" };
    static_assert(sizeof(MARK_COLOR_NAMES) / sizeof(MARK_COLOR_NAMES[0]) == static_cast<size_t>(ALCodeEditor::Mark::COUNT), "every mark has a colour");

    // What a string or a comment is made of.
    bool quiet(ALSyntaxKind kind)
    {
        return kind == ALSyntaxKind::String || kind == ALSyntaxKind::Comment || kind == ALSyntaxKind::DocComment ||
               kind == ALSyntaxKind::Escape || kind == ALSyntaxKind::AttributeValue;
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

    // What a string's text reads as, for the names a host offers for it to
    // be matched against: a quote or a backslash escaped is itself -- the
    // escapes a host writes its names with -- and an escape only begun at
    // the end is nothing yet. Any other is left as written, which no name
    // holds.
    std::string unescaped(std::string_view written)
    {
        std::string reads;
        reads.reserve(written.size());
        for (size_t i = 0; i < written.size(); ++i)
        {
            if (written[i] == '\\')
            {
                if (i + 1 == written.size())
                {
                    break;
                }
                const char next = written[i + 1];
                if (next == '\\' || next == '"' || next == '\'')
                {
                    reads += next;
                    ++i;
                    continue;
                }
            }
            reads += written[i];
        }
        return reads;
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
    mBracketIndex(&highlighter()),
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
    mBracketIndex.attach(&document());
    // Copy and Cut with nothing selected take the line, as code editors do.
    setClipsLines(true);
    setFeatures(this);
    layout().setInlayProvider([this](S32 line, std::vector<ALTextLayout::Inlay>& out) { provideInlays(line, out); });
    mChangedConnection = onTextChanged([this]() {
        if (completionOpen())
        {
            refreshCompletion();
        }
        // What was lit may be another name now.
        clearHighlights(Highlight::Occurrences);
        mOccurrencesDue = true;
        mOccurrencesRest.reset();
        // Stepped back to the saved text: nothing is changed since.
        if (!isDirty())
        {
            std::fill(mChanged.begin(), mChanged.end(), 0);
        }
    });
    mCaretConnection = onCaretMoved([this]() {
        dropPlaceholdersLeft();
        // The parameter the caret is at, worked out here rather than asked
        // for again: the call's commas before it.
        if (mCards.signature() && mSignatureOpen.line >= 0 && signatureShown())
        {
            mCards.setSignatureActive(argumentAt(mSignatureOpen, caret()));
        }
        // The name lit again once the caret rests; put out now if it has
        // left it.
        mOccurrencesDue = true;
        mOccurrencesRest.reset();
        if (!highlights(Highlight::Occurrences).empty() && (hasSelection() || !highlighted(Highlight::Occurrences, caret())))
        {
            clearHighlights(Highlight::Occurrences);
        }
    });
}

ALCodeEditor::~ALCodeEditor()
{
    // Before the signals it listens to go with this part of the editor.
    if (mPeek)
    {
        removeChild(mPeek);
        delete mPeek;
        mPeek = nullptr;
    }
    // The layout outlives this part of the editor, and asks the provider
    // about this part's inlays; the view, its features.
    layout().setInlayProvider(nullptr);
    setFeatures(nullptr);
    clearHandlers();
}

void ALCodeEditor::clearHandlers()
{
    mProvider          = nullptr;
    mCompletionRequest = nullptr;
    mPathProvider      = nullptr;
    mPathRequest       = nullptr;
    mStringProvider    = nullptr;
    mHover             = nullptr;
    mHoverRequest      = nullptr;
    mSignatureRequest  = nullptr;
    mSymbolRequest     = nullptr;
    mLinkRequest       = nullptr;
    mFixProvider       = nullptr;
    mFunctionProvider  = nullptr;
    mFixHandler        = nullptr;
    mFixesShown        = nullptr;
    mActionRequest     = nullptr;
    mCardLinkHandler   = nullptr;
    mChangeStepper     = nullptr;
    mLineRevealer      = nullptr;
    setDropHandler(nullptr);
}

// --- marks and decorations ---------------------------------------------------

void ALCodeEditor::onEdit(const ALTextDocument::Edit& edit)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    hideCard();
    // Each run of lines an edit replaced -- one, or a batch's several.
    const std::vector<ALTextDocument::Edit::LineSpan>& spans = edit.lineSpans();
    const S32                                         lines = document().lineCount();
    mMarks.applySpans(spans, lines, Mark::None, Mark::None);
    // What the problems there offered goes with them: a check says again.
    mFixable.applySpans(spans, lines, 0, 0);
    closeFixes();
    // The lines the edit touched are changed until the next save.
    mChanged.applySpans(spans, lines, 1, 0);
    slideAsides(spans);

    // Decorations and highlights after the edit move along with the text;
    // the ones it cut into go.
    mDecorations.apply(edit);
    for (auto& layer : mHighlights)
    {
        layer.apply(edit);
    }
    mSemantics.apply(edit);
    // An inlay moves with the text it stands by: one before the text at
    // its position stays put when something is typed there, since what
    // is typed is the start of that text; one after the text before it
    // moves along, since what is typed extends that text. An edit that
    // takes the position with it takes the inlay.
    // A batch's stretches each so, the text between them untouched.
    mInlays.apply(
        edit,
        [](InlayHint& h, const ALTextDocument::Edit& e) {
            if (const std::optional<ALTextRange> taken = e.replacedAround(h.at); taken && taken->begin < h.at)
            {
                return false;
            }
            h.at = e.placed(h.at, !h.before);
            return true;
        },
        [](InlayHint&) {});
    // The stops of a snippet or a call being filled in move with the text.
    mSnippet.slide(edit);
    // So does the bracket of the call whose signature is shown; one the
    // edit took goes, and the call with it.
    if (mSignatureOpen.line >= 0)
    {
        const ALTextRange taken = edit.range.normalised();
        if (taken.end <= mSignatureOpen)
        {
            mSignatureOpen = edit.slidPast(mSignatureOpen);
        }
        else if (taken.begin <= mSignatureOpen)
        {
            mSignatureOpen = ALTextPos(-1, -1);
            hideSignature();
        }
    }

    // A closer typing put in moves with the text before it, and goes with
    // an edit that takes it.
    mAutoClosed.apply(
        edit,
        [](ALTextPos& at, const ALTextDocument::Edit& e) {
            if (e.replacedAround(at))
            {
                return false;
            }
            at = e.placed(at);
            return true;
        },
        [](ALTextPos&) {});

    // Folds slide the same way (ALFoldModel::edited), and are hidden again
    // once the command is done.
    const bool folded = !mFolds.folded().empty();
    mFolds.edited(edit, document().lineCount());
    mFoldsDirty = mFoldsDirty || folded || !mFolds.folded().empty();
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
    for (size_t i = 0; i < decorations.size(); ++i)
    {
        decorations[i].order = static_cast<U32>(i);
    }
    mDecorationsGiven = static_cast<U32>(decorations.size());
    mDecorations.assign(std::move(decorations));
}

void ALCodeEditor::setDecorations(S32 first, S32 last, std::vector<Decoration> decorations)
{
    for (Decoration& each : decorations)
    {
        each.order = mDecorationsGiven++;
    }
    mDecorations.replaceLines(first, last, std::move(decorations));
}

std::vector<const ALCodeEditor::Decoration*> ALCodeEditor::decorationsOn(S32 line) const
{
    std::vector<const Decoration*> out;
    const auto                     on = mDecorations.onLine(line);
    for (auto it = on.first; it != on.second; ++it)
    {
        out.push_back(&*it);
    }
    std::sort(out.begin(), out.end(), [](const Decoration* a, const Decoration* b) { return a->order < b->order; });
    return out;
}

void ALCodeEditor::setHighlights(Highlight layer, std::vector<ALTextRange> ranges)
{
    for (ALTextRange& range : ranges)
    {
        range = range.normalised();
    }
    mHighlights[static_cast<size_t>(layer)].assign(std::move(ranges));
}

void ALCodeEditor::clearHighlightsBefore(Highlight layer, const ALTextPos& pos)
{
    auto&      lit  = mHighlights[static_cast<size_t>(layer)];
    const auto past = std::lower_bound(lit.begin(), lit.end(), pos, [](const ALTextRange& range, const ALTextPos& p) { return range.begin < p; });
    for (auto it = past; it != lit.begin();)
    {
        it = lit.erase(--it);
    }
}

void ALCodeEditor::clearHighlights()
{
    for (auto& layer : mHighlights)
    {
        layer.clear();
    }
}

std::vector<ALTextRange> ALCodeEditor::highlights() const
{
    std::vector<ALTextRange> out;
    for (const auto& layer : mHighlights)
    {
        out.insert(out.end(), layer.begin(), layer.end());
    }
    return out;
}

void ALCodeEditor::setLightsOccurrences(bool lights)
{
    mLightsOccurrences = lights;
    if (!lights)
    {
        clearHighlights(Highlight::Occurrences);
    }
}

void ALCodeEditor::lightOccurrences()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    mOccurrencesDue = false;
    clearHighlights(Highlight::Occurrences);
    const ALTextRange name = identifierAtCaret();
    if (!mLightsOccurrences || hasSelection() || name.empty())
    {
        return;
    }
    // Not the name as a string or a comment says it.
    const auto in_code = [this](S32 line, S32 column) {
        for (const ALSyntaxToken& token : highlighter().tokens(line))
        {
            if (token.begin <= column && column < token.end)
            {
                return token.kind != ALSyntaxKind::String && token.kind != ALSyntaxKind::Escape && token.kind != ALSyntaxKind::Comment &&
                       token.kind != ALSyntaxKind::DocComment;
            }
        }
        return true;
    };
    if (!in_code(name.begin.line, name.begin.column))
    {
        return;
    }
    const std::string word  = document().text(name);
    const S32         first = firstVisibleLine();
    const S32         last  = lastVisibleLine();
    const S32         span  = llmax(1, last - first + 1);
    mOccurrencesFirst       = llmax(0, first - span);
    mOccurrencesLast        = llmin(document().lineCount() - 1, last + span);
    std::vector<ALTextRange> lit;
    for (S32 line = mOccurrencesFirst; line <= mOccurrencesLast; ++line)
    {
        const std::string& text = document().line(line);
        for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + word.size()))
        {
            const size_t end = at + word.size();
            if ((at > 0 && alIdentifierByte(text[at - 1])) || (end < text.size() && alIdentifierByte(text[end])) ||
                !in_code(line, static_cast<S32>(at)))
            {
                continue;
            }
            lit.emplace_back(ALTextPos(line, static_cast<S32>(at)), ALTextPos(line, static_cast<S32>(end)));
        }
    }
    if (lit.size() > 1)
    {
        setHighlights(Highlight::Occurrences, std::move(lit));
    }
}

bool ALCodeEditor::highlighted(Highlight layer, const ALTextPos& at) const
{
    const auto on = mHighlights[static_cast<size_t>(layer)].onLine(at.line);
    for (auto it = on.first; it != on.second; ++it)
    {
        if (it->begin <= at && at <= it->end)
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
    if (at.column > 0 && ALBracketIndex::bracketOf(line[at.column - 1], partner, opens))
    {
        from = ALTextPos(at.line, at.column - 1);
    }
    else if (at.column < static_cast<S32>(line.size()) && ALBracketIndex::bracketOf(line[at.column], partner, opens))
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

bool ALCodeEditor::matchBracketAt(const ALTextPos& from, ALTextPos& match, S32 lines)
{
    return mBracketIndex.match(from, match, lines);
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

bool ALCodeEditor::stepChange(bool forward)
{
    return mChangeStepper ? mChangeStepper(forward) : ALChangePeek::stepFrom(*this, forward);
}

bool ALCodeEditor::peekChange(S32 line)
{
    if (!mPeek)
    {
        mPeek = new ALChangePeek(*this);
        addChild(mPeek);
    }
    return mPeek->showAt(line);
}

S32 ALCodeEditor::changeBarAt(S32 x, S32 y)
{
    const LLRect text = textRect();
    if (gutterWidth() <= 0 || x < leftEdge() || x >= leftEdge() + CHANGE_BAR_HIT || y < text.mBottom || y > text.mTop)
    {
        return -1;
    }
    const S32 line = posAtLocal(text.mLeft, y, false).line;
    return lineChanged(line) ? line : -1;
}

bool ALCodeEditor::lineChanged(S32 line) const
{
    return line >= 0 && line < static_cast<S32>(mChanged.size()) && mChanged[static_cast<size_t>(line)] != 0;
}

void ALCodeEditor::resetDirty()
{
    ALTextView::resetDirty();
    std::fill(mChanged.begin(), mChanged.end(), 0);
    closePeek();
}

void ALCodeEditor::closePeek()
{
    // What a peek shows is the change since the save before.
    if (mPeek && mPeek->isOpen())
    {
        mPeek->close();
    }
}

void ALCodeEditor::markUnsaved()
{
    ALTextView::markUnsaved();
    mChanged.assign(static_cast<size_t>(document().lineCount()), 1);
}

void ALCodeEditor::barChangesSince(std::string_view saved)
{
    // The saved text's lines read as the document reads its own, whatever
    // ends them; what lies between where the two first differ and where
    // they end alike barred.
    const std::vector<std::string> was = ALLineBreaks::split(saved);
    std::vector<std::string>       lines;
    lines.reserve(static_cast<size_t>(document().lineCount()));
    for (S32 l = 0; l < document().lineCount(); ++l)
    {
        lines.push_back(document().line(l));
    }
    const auto [first, last]             = ALDiffEdit::edgesOf(was, lines);
    const size_t now                     = lines.size();
    const size_t head                    = static_cast<size_t>(first);
    const size_t tail                    = static_cast<size_t>(last);
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
    closePeek();
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
        for (S32 n = document().lineCount() + mLineNumberBase; n >= 10; n /= 10)
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
    // One caret there, as goTo leaves.
    singleSelection();
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
    // What goes by row only where the rows are: a row scrolled partly out
    // of sight cut at the text's edge, not drawn over what is above or
    // below the view, or over the band.
    LLLocalClipRect clip(LLRect(gutter.mLeft, text.mTop, gutter.mRight, text.mBottom));

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
    // The numbers are placed as they come and drawn together after the
    // rest, one call for all of them rather than one for each.
    mNumberGlyphs.clear();
    mNumberColours.clear();
    mNumberRuns.clear();
    const auto number = [&](S32 value, F32 right, F32 baseline, const LLColor4& colour) {
        char       digits[16];
        const S32  length = snprintf(digits, sizeof(digits), "%d", value);
        const F32  shift  = font->placeGlyphs(std::string_view(digits, static_cast<size_t>(llmax(0, length))), LLFontGL::RIGHT, mNumberScratch);
        const auto ink    = LLColor4U(colour);
        mNumberRuns.push_back(LLFontGL::GlyphRun{ nullptr, nullptr, mNumberScratch.size(), right + shift, baseline });
        mNumberGlyphs.insert(mNumberGlyphs.end(), mNumberScratch.begin(), mNumberScratch.end());
        mNumberColours.insert(mNumberColours.end(), mNumberScratch.size(), ink);
    };
    const auto draw_numbers = [&]() {
        size_t at = 0;
        for (LLFontGL::GlyphRun& run : mNumberRuns)
        {
            run.glyphs = mNumberGlyphs.data() + at;
            run.colors = mNumberColours.data() + at;
            at += run.count;
        }
        font->renderGlyphRuns(mNumberRuns.data(), mNumberRuns.size());
        mNumberRuns.clear();
        mNumberGlyphs.clear();
        mNumberColours.clear();
    };
    // The host's sign for a line, centred on a column, in the numbers' ink.
    const auto draw_sign = [&](char sign, S32 cx, S32 screen_top, const LLColor4& colour) {
        const char* glyph = sign == '-' ? "\xE2\x88\x92" : sign == '+' ? "+" : sign == '>' ? "\xC2\xBB" : "~";
        font->renderUTF8(glyph, 0, cx, screen_top - ascent, colour, LLFontGL::HCENTER, LLFontGL::BASELINE);
    };
    // Down every row of a line: the bar of a line changed, and the heat of
    // one that made something, at least faintly and the warmest in the
    // heat's own colour; all in one batch.
    gGL.getTextureSlot(0)->unbind();
    gGL.begin(LLRender::TRIANGLES);
    forEachVisibleRow(text, [&](S32 line, S32 row, S32 screen_top) {
        if (lineChanged(line))
        {
            gl_rect_2d_in_batch(gutter.mLeft, screen_top, gutter.mLeft + 2, screen_top - row_h, changed);
        }
        if (mHeatShown)
        {
            if (const F32 heat = heatAt(line); heat > 0.f)
            {
                gl_rect_2d_in_batch(fold_right + 1, screen_top, gutter.mRight - 1, screen_top - row_h, warm % (alpha * (0.15f + 0.85f * llclamp(heat, 0.f, 1.f))));
            }
        }
    });
    gGL.end();
    forEachVisibleRow(text, [&](S32 line, S32 row, S32 screen_top) {
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
            // counted from the caret's line where that is asked for; the
            // host's number where it says one.
            const LineAnnotation& said  = lineAnnotation(line);
            const S32             shown = said.number != LineAnnotation::OWN_NUMBER ? said.number
                                          : mRelativeLineNumbers && line != caret_line ? std::abs(line - caret_line)
                                                                                       : line + 1 + mLineNumberBase;
            if (shown > 0)
            {
                number(shown, static_cast<F32>(numbers_right), static_cast<F32>(screen_top - ascent), line == caret_line ? lit : ink);
            }
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
                // Its severity's own shape, as the Problems list has it --
                // a cross in a ring for an error, run-time or found, a
                // triangle for a warning, a ring with a stroke for a note
                // -- so that the marks tell apart without their colours;
                // and a dot at its corner where a fix would put it right,
                // the lightbulb's promise away from the caret.
                const S32    y = screen_top - (row_h - MARK_SIZE) / 2;
                const LLRect box(gutter.mLeft + MARK_INSET, y, gutter.mLeft + MARK_INSET + MARK_SIZE, y - MARK_SIZE);
                if (const LLUIImagePtr icon = markIcon(mark); icon.notNull())
                {
                    icon->draw(box, markColor(mark) % alpha);
                }
                else
                {
                    gl_rect_2d(box, markColor(mark) % alpha);
                }
                if (changesAt(line) && !isReadOnly())
                {
                    gGL.color4fv(lit.mV);
                    gl_circle_2d(static_cast<F32>(box.mRight), static_cast<F32>(box.mBottom) + 1.f, 2.f, 8, true);
                }
            }
            else if (said.sign)
            {
                // The host's sign, where the mark goes.
                draw_sign(said.sign, gutter.mLeft + MARK_INSET + MARK_SIZE / 2, screen_top, line == caret_line ? lit : ink);
            }
        }
        // A marker at a block's first line: pointing right at a folded
        // one, always; pointing down at an open one, while the mouse is
        // over the gutter, so that the gutter is quiet otherwise.
        const bool marker = mShowFoldMarkers && regionStartingAt(line) && (isFolded(line) || mGutterHover);
        if (!mShowLineNumbers && !marker && mShowFoldMarkers)
        {
            // Without the numbers there is no mark column: the host's sign
            // in the fold column instead, where no marker stands.
            if (const char sign = lineAnnotation(line).sign; sign)
            {
                draw_sign(sign, fold_right - FOLD_COLUMN / 2, screen_top, line == caret_line ? lit : ink);
            }
        }
        if (marker)
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
    draw_numbers();
    // The headers pinned over the text have their numbers pinned over
    // the gutter, on the same band.
    const std::vector<S32> pinned = stickyLines(true);
    if (!pinned.empty() && mShowLineNumbers)
    {
        const S32    rows = static_cast<S32>(pinned.size());
        const LLRect band(gutter.mLeft, text.mTop, gutter.mRight, text.mTop - rows * row_h);
        gl_rect_2d(band, paint(Paint::StickyHeader) % alpha, true);
        gl_rect_2d(band.mLeft, band.mBottom, band.mRight, band.mBottom - 1, fold % 0.6f, true);
        for (S32 i = 0; i < rows; ++i)
        {
            const S32 line  = pinned[static_cast<size_t>(i)];
            const S32 shown_number = mRelativeLineNumbers && line != caret_line ? std::abs(line - caret_line) : line + 1 + mLineNumberBase;
            number(shown_number, static_cast<F32>(numbers_right), static_cast<F32>(text.mTop - i * row_h - ascent), ink);
        }
        draw_numbers();
    }
}

void ALCodeEditor::tintRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha, std::vector<LLColor4U>& colors)
{
    // What the analyzer knows a stretch to be, over the grammar's colour
    // for it; a comment or a string keeps its own, since a name inside
    // one is not that name.
    if (!mSemantics.empty())
    {
        const auto on    = mSemantics.onLine(line);
        auto       first = on.first;
        if (first != mSemantics.end() && first->range.begin.line <= line)
        {
            // Where the glyphs go forward through the line, from the first
            // token that reaches the row, and the grammar's tokens walked
            // alongside; else from the line's first, and the grammar asked
            // afresh for each glyph.
            const std::vector<ALSyntaxToken>& grammar = highlighter().tokens(line);
            size_t                            g_at    = 0;
            if (laid.ordered)
            {
                const ALTextPos row_start(line, row.begin);
                first = std::partition_point(first, on.second, [&](const SemanticToken& t) { return t.range.end <= row_start; });
                g_at  = static_cast<size_t>(std::partition_point(grammar.begin(), grammar.end(), [&](const ALSyntaxToken& g) { return g.end <= row.begin; }) -
                                           grammar.begin());
            }
            auto literal = [&](S32 cluster) {
                if (!laid.ordered)
                {
                    g_at = 0;
                }
                while (g_at < grammar.size() && grammar[g_at].end <= cluster)
                {
                    ++g_at;
                }
                if (g_at < grammar.size() && grammar[g_at].begin <= cluster)
                {
                    const ALSyntaxKind kind = grammar[g_at].kind;
                    return kind == ALSyntaxKind::Comment || kind == ALSyntaxKind::DocComment || kind == ALSyntaxKind::String || kind == ALSyntaxKind::Escape ||
                           kind == ALSyntaxKind::Preprocessor;
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
    // The depth at each bracket of the line that is code, worked out once
    // for the line's rows; then the row's glyphs that are brackets in their
    // depth's colour.
    BracketDepths&    depths  = mBracketDepths;
    const void* const grammar = highlighter().grammar().get();
    if (depths.line != line || depths.version != document().version() || depths.grammar != grammar)
    {
        depths.line    = line;
        depths.version = document().version();
        depths.grammar = grammar;
        depths.at.clear();
        S32 depth = mBracketIndex.depthBefore(line);
        for (const auto& [column, c] : mBracketIndex.bracketsOn(line))
        {
            if (c == '(' || c == '[' || c == '{')
            {
                depths.at.emplace_back(column, depth);
                ++depth;
            }
            else
            {
                depth = llmax(0, depth - 1);
                depths.at.emplace_back(column, depth);
            }
        }
    }
    const std::vector<std::pair<S32, S32>>& at = depths.at;
    if (at.empty())
    {
        return;
    }
    size_t next = laid.ordered ? static_cast<size_t>(std::lower_bound(at.begin(), at.end(), std::make_pair(row.begin, S32_MIN)) - at.begin()) : 0;
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
    mSemantics.assign(std::move(tokens));
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
        same_line(mInlays.items(), was, line, old_line);
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
    mInlays.assign(std::move(hints));
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
        // Which of the line's own it is: what stays true while the line's
        // layout does (see ALTextLayout::Inlay).
        inlay.id     = static_cast<S32>(it - first);
        out.push_back(inlay);
    }
}

S32 ALCodeEditor::inlayIndexOf(S32 line, S32 id) const
{
    if (id < 0)
    {
        return -1;
    }
    const auto first = std::lower_bound(mInlays.begin(), mInlays.end(), line, [](const InlayHint& h, S32 l) { return h.at.line < l; });
    const auto it    = first + std::min<std::ptrdiff_t>(id, mInlays.end() - first);
    return it != mInlays.end() && it->at.line == line ? static_cast<S32>(it - mInlays.begin()) : -1;
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
        const S32                  index = inlayIndexOf(line, glyph.inlay);
        if (index < 0)
        {
            continue;
        }
        const F32 x0 = glyph.pen - r.xStart + INLAY_GAP;
        const F32 x1 = glyph.pen - r.xStart + glyph.advance - INLAY_GAP;
        if (x_rel >= x0 && x_rel < x1)
        {
            return index;
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

void ALCodeEditor::slideAsides(const std::vector<ALTextDocument::Edit::LineSpan>& spans)
{
    if (mAsides.empty())
    {
        return;
    }
    mAsides.resize(llmax(mAsides.size(), static_cast<size_t>(llmax(spans.back().last, 0) + 1)));
    // What is left of a line keeps its heat and its note: the line a run
    // begins inside -- typed in, or broken in two -- or, where it begins
    // at a line's start, the line it ends in, pushed down by the lines
    // made above it or pulled up over the lines taken; each where it is
    // once the runs before it have moved it.
    std::vector<std::pair<S32, Aside>> keep;
    keep.reserve(spans.size());
    S32 shift = 0;
    for (const ALTextDocument::Edit::LineSpan& span : spans)
    {
        const S32 first = llmax(0, span.first);
        const S32 last  = llmax(first, span.last);
        if (span.firstColumn > 0)
        {
            keep.emplace_back(first + shift, mAsides[static_cast<size_t>(first)]);
        }
        else
        {
            keep.emplace_back(first + shift + span.made - 1, mAsides[static_cast<size_t>(last)]);
        }
        shift += span.made - (last - first + 1);
    }
    mAsides.applySpans(spans, llmax(document().lineCount(), 0), Aside(), Aside());
    for (const auto& [row, aside] : keep)
    {
        if (row >= 0 && row < static_cast<S32>(mAsides.size()))
        {
            mAsides[static_cast<size_t>(row)] = aside;
        }
    }
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
    std::vector<InlayHint> kept = mInlays.items();
    mInlays.erase(mInlays.begin() + index);
    layout().invalidateLine(hint.at.line);
    if (!replaceAll({ { ALTextRange(hint.at, hint.at), hint.insert } }))
    {
        mInlays.assign(std::move(kept));
        layout().invalidateLine(hint.at.line);
        return false;
    }
    undoJournal().label("refactor");
    return true;
}

// --- headers pinned at the top ------------------------------------------------------

std::vector<S32> ALCodeEditor::stickyLines(bool fresh)
{
    std::vector<S32> lines;
    if (!mStickyHeaders)
    {
        return lines;
    }
    // The first row on screen, and the blocks around its line that start
    // above it: their first lines, outermost first, the innermost few --
    // kept for as long as the text, the top line and the folds are, and
    // between frames for as long as the top line is.
    const S32 top_line = posAtLocal(textRect().mLeft, textRect().mTop - 1, false).line;
    if (mStickyValid && mStickyTop == top_line && (!fresh || (mStickyVersion == document().version() && mStickyFolded == mFolds.folded())))
    {
        return mSticky;
    }
    // The blocks open there, walked back from it: nothing below the top
    // line is read for them.
    for (const S32 start : folds().openAt(document(), getTabWidth(), top_line, 8))
    {
        if (!isFolded(start))
        {
            lines.push_back(start);
        }
    }
    // Nested blocks start later: the list is already outer to inner; the
    // innermost three are the ones worth the room.
    if (lines.size() > 3)
    {
        lines.erase(lines.begin(), lines.end() - 3);
    }
    mSticky        = lines;
    mStickyVersion = document().version();
    mStickyTop     = top_line;
    mStickyFolded  = mFolds.folded();
    mStickyValid   = true;
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
    const std::vector<S32> lines = stickyLines(true);
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
        // The caret's row: its line's, or the gap's it stands in.
        S32 row;
        layout().xOf(caret().line, caret().column, &row);
        const S32 gap = caretGap();
        const S32 top = gap >= 0 ? text.mTop - (layout().gapTop(gap) - scrollY()) : screenTopOf(text, caret().line, row);
        // Drawn before the text's clip, and the caret's line may be
        // scrolled partly or wholly out of sight.
        LLLocalClipRect clip(text);
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

S32 ALCodeEditor::indentOf(S32 line)
{
    return folds().indentOf(document(), getTabWidth(), line);
}

std::vector<ALCodeEditor::Blank> ALCodeEditor::blanksOn(S32 line, S32 within_from, S32 within_to) const
{
    std::vector<Blank> out;
    blanksOn(line, within_from, within_to, out);
    return out;
}

void ALCodeEditor::blanksOn(S32 line, S32 within_from, S32 within_to, std::vector<Blank>& out) const
{
    out.clear();
    if (mShowWhitespace == Whitespace::None || line < 0 || line >= document().lineCount())
    {
        return;
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
            return;
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
            return;
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
    // Only what this row covers, and of that what is in sight: a line
    // wrapped into many rows, or scrolled across, would otherwise be
    // walked whole for each.
    const ALTextLayout::Row   row    = rowInSight(laid, laid.rows[static_cast<size_t>(r)], text);
    std::vector<Blank>&       blanks = mBlankScratch;
    blanksOn(line, row.begin, row.end, blanks);
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
    const LLColor4 wash = highlightColor() % alpha;
    for (size_t index = 0; index < mHighlights.size(); ++index)
    {
        // The name's other places more lightly: they are only what is
        // written alike, lit without being asked for. A change's words as a
        // comparison marks words put in.
        static const LLUIColor changed = ALDiffColors::get(ALDiffColors::Name::AddedWord);
        const LLColor4 ink = index == static_cast<size_t>(Highlight::Occurrences) ? wash % 0.5f
                             : index == static_cast<size_t>(Highlight::Change)    ? changed.get() % alpha
                                                                                   : wash;
        const auto     on  = mHighlights[index].onLine(line);
        for (auto it = on.first; it != on.second; ++it)
        {
            F32 x0, x1;
            if (spanOnRow(line, row, *it, x0, x1))
            {
                gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, ink);
            }
        }
    }
    for (S32 i = 0; i < static_cast<S32>(mSnippet.stops().size()); ++i)
    {
        F32 x0, x1;
        if (spanOnRow(line, row, mSnippet.stops()[i], x0, x1))
        {
            gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, highlightColor() % alpha);
            if (i == mSnippet.at())
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
            const ALTextLayout::Row r    = rowInSight(laid, laid.rows[static_cast<size_t>(row)], text);
            const LLFontGL*         font = getFont();
            for (size_t k = r.glyphBegin; k < r.glyphEnd && font; ++k)
            {
                const ALTextLayout::Glyph& glyph = laid.glyphs[k];
                const S32                  index = inlayIndexOf(line, glyph.inlay);
                if (index < 0)
                {
                    continue;
                }
                const InlayHint& hint = mInlays[static_cast<size_t>(index)];
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
        const auto on = mSemantics.onLine(line);
        for (auto it = on.first; it != on.second; ++it)
        {
            F32 x0, x1;
            if (it->strike && spanOnRow(line, row, it->range, x0, x1))
            {
                const S32 y = screen_top - row_h / 2;
                gl_rect_2d(static_cast<S32>(left + x0), y + 1, static_cast<S32>(left + x1), y, colorForKind(it->kind) % alpha);
            }
        }
    }
    const auto decorated = mDecorations.onLine(line);
    for (auto it = decorated.first; it != decorated.second; ++it)
    {
        const Decoration& d = *it;
        F32               x0, x1;
        if (!spanOnRow(line, row, d.range, x0, x1))
        {
            continue;
        }
        if (d.style == Decoration::Style::Background)
        {
            gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, d.color % alpha);
        }
        else if (d.style == Decoration::Style::Squiggle)
        {
            squiggle(left + x0, left + x1, squiggleMiddle(screen_top, layout().line(line).rows[static_cast<size_t>(row)].ascent), d.color % alpha,
                     text);
        }
        else
        {
            // Dashes or dots two pixels high, counted from where the range
            // begins so that they do not crawl as it scrolls sideways;
            // as much of them as is in sight.
            const bool dashed = d.style == Decoration::Style::Dashed;
            const S32  on     = dashed ? UNDERLINE_DASH : UNDERLINE_DOT;
            const S32  period = on + (dashed ? UNDERLINE_DASH_GAP : UNDERLINE_DOT_GAP);
            const S32  from   = static_cast<S32>(left + x0);
            const S32  to     = static_cast<S32>(left + x1);
            const S32  y      = screen_top - row_h + 2;
            const LLColor4 ink = d.color % alpha;
            gGL.getTextureSlot(0)->unbind();
            gGL.begin(LLRender::TRIANGLES);
            for (S32 x = from; x < to; x += period)
            {
                const S32 a = llmax(x, text.mLeft);
                const S32 b = llmin(llmin(x + on, to), text.mRight);
                if (a < b)
                {
                    gl_rect_2d_in_batch(a, y + 1, b, y - 1, ink);
                }
            }
            gGL.end();
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
    folds().regions(document(), getTabWidth());
}

const std::vector<ALCodeEditor::FoldRegion>& ALCodeEditor::foldRegions()
{
    static const std::vector<FoldRegion> none;
    return mFoldable ? folds().regions(document(), getTabWidth()) : none;
}

void ALCodeEditor::setFoldable(bool foldable)
{
    if (!foldable && mFoldable)
    {
        unfoldAll();
    }
    mFoldable = foldable;
}

const ALCodeEditor::FoldRegion* ALCodeEditor::regionStartingAt(S32 line)
{
    return folds().startingAt(document(), getTabWidth(), line);
}

const ALCodeEditor::FoldRegion* ALCodeEditor::regionAround(S32 line)
{
    return folds().around(document(), getTabWidth(), line);
}

bool ALCodeEditor::isFolded(S32 line) const
{
    return mFolds.isFolded(line);
}

void ALCodeEditor::settleFolds()
{
    if (mFoldsDirty)
    {
        applyFolds();
    }
}

void ALCodeEditor::applyFolds()
{
    mFoldsDirty = false;
    // The layout slides what it hides with an edit and shows every line an
    // edit makes, so once what it hides has changed, the lines last hidden
    // by folds are no guide to the lines it has folded: each line is set
    // as the folds have it, where it is not so already. With no folds then
    // or now, it has none folded.
    std::vector<std::pair<S32, S32>> hidden = folds().hidden(document(), getTabWidth());
    ALTextLayout&                    lines  = layout();
    if (hidden == mHiddenByFolds && (hidden.empty() || lines.hiddenRevision() == mHiddenByFoldsAt))
    {
        return;
    }
    // In the order the folds start in; one inside another ends inside it.
    const S32 count   = lines.lineCount();
    size_t    next    = 0;
    S32       through = -1;
    for (S32 l = 0; l < count; ++l)
    {
        for (; next < hidden.size() && hidden[next].first <= l; ++next)
        {
            through = llmax(through, hidden[next].second);
        }
        const bool folded = l <= through;
        if (lines.hiddenBy(l, ALTextLayout::HiddenBy::Folds) != folded)
        {
            lines.setHidden(ALTextLayout::HiddenBy::Folds, l, l, folded);
        }
    }
    mHiddenByFolds.swap(hidden);
    mHiddenByFoldsAt = lines.hiddenRevision();
}

ALFoldModel& ALCodeEditor::folds()
{
    // Told the grammar's syntax again where the grammar has changed: code
    // by its brackets and its block words; prose, and no grammar, by
    // indentation.
    const ALSyntaxGrammar* grammar = highlighter().grammar().get();
    if (grammar != mFoldGrammar)
    {
        mFoldGrammar = grammar;
        mFolds.setLineComment(grammar ? grammar->lineComment() : std::string());
        if (grammar && !grammar->prose())
        {
            mFolds.setSyntax([this](S32 line, std::vector<ALFoldModel::Block>& out) { foldBlocksOn(line, out); },
                             [this](S32 line) { return highlighter().revision(line); });
        }
        else
        {
            mFolds.setSyntax(nullptr, nullptr);
        }
    }
    return mFolds;
}

void ALCodeEditor::foldBlocksOn(S32 line, std::vector<ALFoldModel::Block>& out)
{
    typedef ALFoldModel::Event Event;
    // The brackets that are code.
    for (const auto& [column, c] : mBracketIndex.bracketsOn(line))
    {
        out.push_back({ column, (c == '(' || c == '[' || c == '{') ? Event::Open : Event::Close });
    }
    // And the grammar's block words, where it has them, as code.
    const ALSyntaxGrammar* grammar = highlighter().grammar().get();
    if (!grammar || (grammar->foldWords().opens.empty() && grammar->foldWords().closes.empty()))
    {
        return;
    }
    const ALSyntaxGrammar::FoldWords& words = grammar->foldWords();
    const std::string&                text  = document().line(line);
    const auto in = [](const std::vector<std::string>& list, std::string_view word) { return std::find(list.begin(), list.end(), word) != list.end(); };
    bool joined = false;
    for (const ALSyntaxToken& token : highlighter().tokens(line))
    {
        if (token.kind == ALSyntaxKind::String || token.kind == ALSyntaxKind::Comment || token.kind == ALSyntaxKind::DocComment ||
            token.end > static_cast<S32>(text.size()))
        {
            continue;
        }
        const std::string_view word(text.data() + token.begin, static_cast<size_t>(token.end - token.begin));
        if (in(words.middles, word))
        {
            out.push_back({ token.begin, Event::Middle });
            joined = in(words.joined, word);
        }
        else if (in(words.opens, word))
        {
            // `elseif x then`: the elseif opened it.
            if (!joined)
            {
                out.push_back({ token.begin, Event::Open });
            }
            joined = false;
        }
        else if (in(words.closes, word))
        {
            out.push_back({ token.begin, Event::Close });
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const ALFoldModel::Block& a, const ALFoldModel::Block& b) { return a.column < b.column; });
}

bool ALCodeEditor::foldAt(S32 line)
{
    if (!mFoldable)
    {
        return false;
    }
    const std::optional<FoldRegion> chosen = folds().fold(document(), getTabWidth(), line);
    if (!chosen)
    {
        return false;
    }
    // The caret cannot stay in what is folded away.
    const ALTextRange sel = selection().normalised();
    if ((sel.begin.line > chosen->start && sel.begin.line <= chosen->end) || (sel.end.line > chosen->start && sel.end.line <= chosen->end))
    {
        setCaret(document().lineEnd(chosen->start));
    }
    applyFolds();
    caretsOutOfFolds();
    return true;
}

void ALCodeEditor::caretsOutOfFolds()
{
    if (!layout().anyHidden())
    {
        return;
    }
    // Each one in what is folded away a caret at the end of the line above
    // it that shows -- the fold's own line; those that meet there become
    // one. All at once: the main one put alone would open again the folds
    // the others are in.
    size_t                   main  = 0;
    std::vector<ALTextRange> all   = selectionsInOrder(&main);
    bool                     moved = false;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const ALTextRange range = all[i].normalised();
        if (!layout().hidden(range.begin.line) && !layout().hidden(range.end.line))
        {
            continue;
        }
        const S32 hidden = layout().hidden(range.begin.line) ? range.begin.line : range.end.line;
        const S32 above  = layout().visibleFrom(hidden, -1);
        const S32 below  = layout().visibleFrom(hidden, 1);
        const ALTextPos to = above >= 0 ? document().lineEnd(above) : ALTextPos(below >= 0 ? below : 0, 0);
        all[i]             = ALTextRange(to, to);
        moved              = true;
    }
    if (moved)
    {
        placeSelections(all, main);
        scrollToCaret();
    }
}

bool ALCodeEditor::unfoldAt(S32 line)
{
    if (!folds().unfold(document(), getTabWidth(), line))
    {
        const S32 beside = hostHiddenBeside(line);
        if (beside < 0)
        {
            return false;
        }
        mLineRevealer(beside);
        return !layout().hidden(beside);
    }
    applyFolds();
    return true;
}

S32 ALCodeEditor::hostHiddenBeside(S32 line) const
{
    if (!mLineRevealer)
    {
        return -1;
    }
    const ALTextLayout& lines = layout();
    for (const S32 at : { line - 1, line + 1 })
    {
        if (lines.hiddenBy(at, ALTextLayout::HiddenBy::Host))
        {
            return at;
        }
    }
    return -1;
}

void ALCodeEditor::foldAll()
{
    if (!mFoldable)
    {
        return;
    }
    folds().foldAll(document(), getTabWidth());
    applyFolds();
    caretsOutOfFolds();
}

void ALCodeEditor::unfoldAll()
{
    mFolds.unfoldAll();
    applyFolds();
}

void ALCodeEditor::revealLine(S32 line)
{
    // Each owner shows what it hid its own way: the host as it sees fit --
    // a comparison opens the run the line is in, on both sides -- and the
    // folds the block it is in.
    ALTextLayout& lines = layout();
    if (lines.hiddenBy(line, ALTextLayout::HiddenBy::Host) && mLineRevealer)
    {
        mLineRevealer(line);
    }
    if (lines.hiddenBy(line, ALTextLayout::HiddenBy::Folds) && folds().reveal(document(), getTabWidth(), line))
    {
        applyFolds();
    }
    // Whatever still hides it lets it go: the caret is on it.
    if (lines.hidden(line))
    {
        lines.setHidden(ALTextLayout::HiddenBy::Any, line, line, false);
    }
}

bool ALCodeEditor::performFeature(ALEditorCommand command)
{
    typedef ALEditorCommand C;
    switch (command)
    {
        case C::Fold:
        case C::Unfold:
        case C::FoldAll:
        case C::UnfoldAll:
            return performFold(command);
        case C::NextFunction:
        case C::PreviousFunction:
        case C::SelectFunction:
        case C::GoToMatchingBracket:
        case C::ExpandSelection:
        case C::ShrinkSelection:
        case C::SelectNextOccurrence:
        case C::ChangeAllOccurrences:
            return performFunction(command);
        case C::Complete:
            return complete();
        case C::SignatureHelp:
            return signatureHelp();
        case C::QuickFix:
            return quickFix();
        case C::GoToDefinition:
        case C::FindReferences:
        case C::Rename:
            return performSymbol(command);
        default:
            return false;
    }
}

bool ALCodeEditor::canPerformFeature(ALEditorCommand command) const
{
    typedef ALEditorCommand C;
    switch (command)
    {
        case C::Fold:
        case C::Unfold:
        case C::FoldAll:
        case C::UnfoldAll:
            return canFold(command);
        case C::NextFunction:
        case C::PreviousFunction:
        case C::SelectFunction:
        case C::GoToMatchingBracket:
        case C::ExpandSelection:
        case C::ShrinkSelection:
        case C::SelectNextOccurrence:
        case C::ChangeAllOccurrences:
            return canFunction(command);
        case C::QuickFix:
            return canQuickFix();
        case C::GoToDefinition:
        case C::FindReferences:
        case C::Rename:
            return canSymbol(command);
        default:
            return false;
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

std::vector<ALTextRange> ALCodeEditor::functions() const
{
    std::vector<ALTextRange> out;
    if (mFunctionProvider)
    {
        mFunctionProvider(out);
    }
    std::sort(out.begin(), out.end(), [](const ALTextRange& a, const ALTextRange& b) { return a.begin < b.begin; });
    return out;
}

std::optional<ALTextRange> ALCodeEditor::functionFrom(const ALTextPos& at, bool forward, bool ends) const
{
    // The nearest start, or end, past the place that way.
    std::optional<ALTextRange> best;
    for (const ALTextRange& one : functions())
    {
        const ALTextPos mark = ends ? one.end : one.begin;
        const bool      past = forward ? at < mark : mark < at;
        const ALTextPos kept = best ? (ends ? best->end : best->begin) : mark;
        if (past && (!best || (forward ? mark < kept : kept < mark)))
        {
            best = one;
        }
    }
    return best;
}

std::optional<ALTextRange> ALCodeEditor::functionAround(const ALTextRange& range) const
{
    // The one holding it that starts last, not the stretch itself.
    std::optional<ALTextRange> best;
    for (const ALTextRange& one : functions())
    {
        const bool holds = !(range.begin < one.begin) && !(one.end < range.end) && !(one == range);
        if (holds && (!best || best->begin < one.begin || (best->begin == one.begin && one.end < best->end)))
        {
            best = one;
        }
    }
    return best;
}

bool ALCodeEditor::performFunction(ALEditorCommand command)
{
    if (command == ALEditorCommand::SelectNextOccurrence)
    {
        return selectNextOccurrence();
    }
    if (command == ALEditorCommand::ChangeAllOccurrences)
    {
        return changeAllOccurrences();
    }
    if (command == ALEditorCommand::ExpandSelection)
    {
        const ALTextRange                was   = selection();
        const std::optional<ALTextRange> grown = grownSelection();
        if (!grown)
        {
            return false;
        }
        // Kept to go back through, while nothing else has moved it.
        if (mGrownFrom.empty() || !(was == mGrownTo))
        {
            mGrownFrom.clear();
        }
        mGrownFrom.push_back(was);
        setSelection(*grown);
        mGrownTo = selection();
        scrollToCaret();
        return true;
    }
    if (command == ALEditorCommand::ShrinkSelection)
    {
        if (!canFunction(command))
        {
            return false;
        }
        const ALTextRange back = mGrownFrom.back();
        mGrownFrom.pop_back();
        setSelection(back);
        mGrownTo = selection();
        scrollToCaret();
        return true;
    }
    if (command == ALEditorCommand::GoToMatchingBracket)
    {
        ALTextPos to;
        if (!bracketToGoTo(to))
        {
            return false;
        }
        setCaret(to);
        scrollToCaret();
        return true;
    }
    if (command == ALEditorCommand::SelectFunction)
    {
        // The innermost around the selection; again, the one around that.
        const std::optional<ALTextRange> around = functionAround(selection().normalised());
        if (!around)
        {
            return false;
        }
        setSelection(*around);
        scrollToCaret();
        return true;
    }
    const std::optional<ALTextRange> to = functionFrom(caret(), command == ALEditorCommand::NextFunction, false);
    if (!to)
    {
        return false;
    }
    setCaret(to->begin);
    scrollToCaret();
    return true;
}

bool ALCodeEditor::bracketToGoTo(ALTextPos& to)
{
    // The partner of the bracket under the caret, or else just before it,
    // the caret put on the partner, so that going again comes back; else
    // the closer of the innermost pair around the caret, of whichever
    // kind opens nearest. Asked for, so as far as it takes.
    const ALTextPos    at      = caret();
    const std::string& line    = document().line(at.line);
    char               partner = 0;
    bool               opens   = false;
    if (at.column < static_cast<S32>(line.size()) && ALBracketIndex::bracketOf(line[at.column], partner, opens) &&
        mBracketIndex.match(at, to, ALBracketIndex::ANYWHERE))
    {
        return true;
    }
    if (at.column > 0 && ALBracketIndex::bracketOf(line[at.column - 1], partner, opens) &&
        mBracketIndex.match(ALTextPos(at.line, at.column - 1), to, ALBracketIndex::ANYWHERE))
    {
        return true;
    }
    ALTextPos nearest(-1, -1);
    for (const char opener : { '(', '[', '{' })
    {
        ALTextPos found;
        if (mBracketIndex.enclosing(at, opener, 1, found, ALBracketIndex::ANYWHERE) && (nearest.line < 0 || nearest < found))
        {
            nearest = found;
        }
    }
    return nearest.line >= 0 && mBracketIndex.match(nearest, to, ALBracketIndex::ANYWHERE);
}

bool ALCodeEditor::canFunction(ALEditorCommand command) const
{
    if (command == ALEditorCommand::GoToMatchingBracket || command == ALEditorCommand::ExpandSelection)
    {
        return true;
    }
    if (command == ALEditorCommand::SelectNextOccurrence || command == ALEditorCommand::ChangeAllOccurrences)
    {
        return !isReadOnly();
    }
    if (command == ALEditorCommand::ShrinkSelection)
    {
        return !mGrownFrom.empty() && selection() == mGrownTo;
    }
    if (!mFunctionProvider)
    {
        return false;
    }
    if (command == ALEditorCommand::SelectFunction)
    {
        return functionAround(selection().normalised()).has_value();
    }
    return functionFrom(caret(), command == ALEditorCommand::NextFunction, false).has_value();
}

std::optional<ALTextRange> ALCodeEditor::grownSelection()
{
    const ALSyntaxGrammar* grammar = highlighter().grammar().get();
    return ALSmartSelect::grow(document(), highlighter(), mBracketIndex, selection(), grammar ? grammar->memberSeparators() : std::string_view("."));
}

bool ALCodeEditor::canFold(ALEditorCommand command) const
{
    ALCodeEditor* self = const_cast<ALCodeEditor*>(this);
    if (!mFoldable)
    {
        // What the host hid beside the caret is the host's to show, whether
        // the text folds or not.
        return command == ALEditorCommand::Unfold && hostHiddenBeside(caret().line) >= 0;
    }
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
            return !mFolds.folded().empty() || hostHiddenBeside(caret().line) >= 0;
        case ALEditorCommand::FoldAll:
            return mFolds.folded().size() < self->foldRegions().size();
        case ALEditorCommand::UnfoldAll:
            return !mFolds.folded().empty();
        default:
            return false;
    }
}

// --- completion --------------------------------------------------------------

bool ALCodeEditor::completionOpen() const
{
    const ALChoicePopup* popup = popupFound(COMPLETION_POPUP);
    return popup && popup->listShown();
}

bool ALCodeEditor::onCompletionList(S32 x, S32 y) const
{
    const ALChoicePopup* popup = popupFound(COMPLETION_POPUP);
    return popup && popup->onList(x, y);
}

ALChoicePopup* ALCodeEditor::popupFound(std::string_view name) const
{
    return findChild<ALChoicePopup>(name, false);
}

ALChoicePopup& ALCodeEditor::makePopup(const char* name, const char* list, const char* side, bool menu_like)
{
    // A child, so it draws over the text and goes where the view goes; as
    // large as the view, so that the list and the box beside it are put in
    // the view's own coordinates.
    ALChoicePopup::Params p;
    p.name      = name;
    p.rect      = getLocalRect();
    p.visible   = false;
    p.list_name = list;
    p.side_name = side;
    p.menu_like = menu_like;
    p.font      = getFont();
    ALChoicePopup* made = LLUICtrlFactory::create<ALChoicePopup>(p);
    addChild(made);
    return *made;
}

void ALCodeEditor::dress(ALChoicePopup& popup)
{
    // A list is one of the things the editor floats over its text, so it
    // wears the editor's colours rather than the skin's. It took them from
    // the colour table instead, which is a table the script themes never
    // touch: on a light theme the completions stayed dark.
    popup.setLook(getFont(), paint(Paint::Widget), textColor(), paint(Paint::WidgetSelection), paint(Paint::WidgetBorder));
}

ALChoicePopup& ALCodeEditor::completionPopup()
{
    // Made the first time there is something to choose: most editors --
    // a tab not looked at, a read-only view -- never show one.
    if (ALChoicePopup* found = popupFound(COMPLETION_POPUP))
    {
        return *found;
    }
    ALChoicePopup& made = makePopup(COMPLETION_POPUP, "completions", "completion_doc", false);
    made.list().onPicked([this](S32) { acceptCompletion(); });
    made.list().onChosen([this](S32) {
        if (completionOpen())
        {
            showCompletionDoc();
        }
    });
    return made;
}

void ALCodeEditor::hideCompletionList()
{
    if (ALChoicePopup* popup = popupFound(COMPLETION_POPUP))
    {
        popup->hideList();
    }
    mCompletionModel.hide();
}

void ALCodeEditor::closeCompletion()
{
    hideCompletionList();
    mCompletionModel.close();
    mCompletionAsked  = false;
    mCompletionMoved  = false;
    mCompletionString = false;
    mCompletionPath   = false;
}

S32 ALCodeEditor::chosenCompletion() const
{
    return completionOpen() ? popupFound(COMPLETION_POPUP)->list().chosen() : -1;
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

void ALCodeEditor::refreshCompletion()
{
    const ALTextPos                  at   = caret();
    // A path's list and a string's at one caret only: at several, what is
    // chosen goes in at each as a name does (completionAt), not as a path or
    // as what a string holds, and a string there is prose as any other.
    const bool                       one  = !hasOtherSelections();
    const std::optional<ALTextRange> path = one && (mPathProvider || mPathRequest) ? pathAt(at) : std::nullopt;
    std::string                      prefix;
    std::string                      head;
    char                             separator = '.';
    ALTextPos                        start;
    std::string                      asked;
    // What the list drew from before, which a list for a string does not
    // narrow as words nor words as one.
    const bool                       was_string = mCompletionString;
    const bool                       was_path   = mCompletionPath;
    mCompletionString                           = one && !path && stringOffers(at, start, prefix);
    mCompletionPath                             = path.has_value();
    // A list shown for a string or a path the caret has left -- its opening
    // quote taken back, its closing one typed -- goes with it, rather than
    // become one of every word for nothing typed.
    if ((was_string || was_path) && !mCompletionString && !mCompletionPath && completionOpen())
    {
        closeCompletion();
        return;
    }
    if (mCompletionString)
    {
        // In a string the host names something for, all it holds before
        // the caret, to be put in whole.
        separator = '\0';
        asked     = prefix;
    }
    else if (path)
    {
        // In a string that names a file, the name after its last slash --
        // the whole, where there is none -- with what comes before that as
        // its head: `ut` and `./lib/` of `./lib/ut`. Asked of the whole.
        const std::string& line = document().line(at.line);
        S32                from = at.column;
        while (from > path->begin.column && line[from - 1] != '/')
        {
            --from;
        }
        start     = ALTextPos(at.line, from);
        prefix    = line.substr(from, at.column - from);
        head      = line.substr(path->begin.column, from - path->begin.column);
        separator = '\0';
        asked     = head + prefix;
    }
    else
    {
        prefix = wordBeforeCaret();
        start  = ALTextPos(at.line, at.column - static_cast<S32>(prefix.size()));
        // After `ll.` the members of `ll` are wanted -- after `obj:` in
        // SLua, its methods: the head is put before the prefix for whoever
        // answers by whole names, and taken off what they answer.
        if (start.column >= 2)
        {
            const std::string members = highlighter().grammar() ? highlighter().grammar()->memberSeparators() : std::string(".");
            const char        before  = document().line(start.line)[start.column - 1];
            const ALTextRange name    = identifierAt(ALTextPos(start.line, start.column - 2));
            if (members.find(before) != std::string::npos && !name.empty() && name.end.column == start.column - 1)
            {
                head      = document().text(name);
                separator = before;
            }
        }
        asked = head.empty() ? prefix : head + separator + prefix;
    }
    if ((prefix.empty() && head.empty() && !mCompletionAsked) || hasSelection())
    {
        closeCompletion();
        return;
    }
    // What there is to choose from, asked for once while one identifier is
    // typed, and narrowed as it grows; a string's, as it was asked.
    if (!mCompletionString && (was_string || !mCompletionModel.pooled(start, head, prefix)))
    {
        std::vector<Completion> answered;
        if (path)
        {
            if (mPathProvider)
            {
                mPathProvider(at, asked, answered);
            }
        }
        else if (mProvider)
        {
            mProvider(at, asked, answered);
        }
        else
        {
            vocabularyCompletions(asked, answered);
        }
        mCompletionModel.pool(start, at, prefix, head, separator, std::move(answered), document(), /*with_words*/ !path);
    }
    const bool                  fresh   = mCompletionModel.narrow(start, at, prefix);
    const completion_request_t& request = path ? mPathRequest : mCompletionRequest;
    if (fresh && request && !mCompletionString)
    {
        // A path's of the whole typed so far, as its provider is asked.
        request(start, path ? asked : prefix);
    }
    if (mCompletionModel.list().empty())
    {
        hideCompletionList();
        return;
    }
    // The same list again, an answer joined to it, keeps what was chosen
    // in it; a list for more typed starts from the best, not moved through.
    const bool same  = mCompletionModel.relisted(asked);
    const bool again = completionOpen() && same;
    if (!again)
    {
        mCompletionMoved = false;
    }
    listCompletions(again);
}

void ALCodeEditor::reaskString()
{
    if (!mStringProvider || !mAutoComplete || completionOpen() || !hasFocus() || hasSelection() || hasOtherSelections() || !typingText())
    {
        return;
    }
    ALTextPos   start;
    std::string typed;
    mCompletionString = false;
    if (stringOffers(caret(), start, typed))
    {
        mCompletionString = true;
        openCompletion(true);
    }
}

bool ALCodeEditor::stringOffers(const ALTextPos& at, ALTextPos& start, std::string& typed)
{
    char                             opener = '\0';
    const std::optional<ALTextRange> held   = mStringProvider ? quotedAt(at, &opener) : std::nullopt;
    if (!held || (opener != '"' && opener != '\''))
    {
        return false;
    }
    start = held->begin;
    // As it reads, which the names are matched against: `Say "` of
    // `Say \"`, for the name `Say "hi"`.
    typed = unescaped(std::string_view(document().line(at.line)).substr(start.column, at.column - start.column));
    if (mCompletionString && mCompletionModel.pooled(start, std::string(), typed))
    {
        return true;
    }
    std::vector<Completion> answered;
    mStringProvider(at, typed, answered);
    if (answered.empty())
    {
        return false;
    }
    mCompletionModel.pool(start, at, typed, std::string(), '\0', std::move(answered), document(), /*with_words*/ false);
    return true;
}

LLUIImagePtr ALCodeEditor::markIcon(Mark mark)
{
    const char* name = mark == Mark::Error || mark == Mark::Runtime ? "Problem_Error" : mark == Mark::Warning ? "Problem_Warning" : "Problem_Note";
    auto        found = mIcons.find(std::string_view(name));
    if (found == mIcons.end())
    {
        found = mIcons.emplace(std::string(name), LLUI::getUIImage(name)).first;
    }
    return found->second;
}

LLUIImagePtr ALCodeEditor::iconOf(const Completion& completion)
{
    if (completion.icon)
    {
        return completion.icon;
    }
    const std::string_view name  = ALCompletionModel::iconNameOf(completion);
    auto                   found = mIcons.find(name);
    if (found == mIcons.end())
    {
        found = mIcons.emplace(std::string(name), LLUI::getUIImage(name)).first;
    }
    return found->second;
}

void ALCodeEditor::listCompletions(bool keep_choice)
{
    ALChoicePopup& popup = completionPopup();
    ALChoiceList&  list  = popup.list();
    // What was chosen, by its word: an answer joined to the list may put
    // rows above it, and the row under the finger must stay the word the
    // finger is on.
    std::string was;
    if (keep_choice && list.chosen() >= 0 && list.chosen() < list.count())
    {
        was = list.choices()[list.chosen()].text;
    }
    S32 chosen = 0;
    const std::vector<Completion>& completions = mCompletionModel.list();
    for (size_t i = 0; i < completions.size() && !was.empty(); ++i)
    {
        if (completions[i].text == was)
        {
            chosen = static_cast<S32>(i);
            break;
        }
    }
    dress(popup);
    std::vector<ALChoiceList::Choice> choices;
    choices.reserve(completions.size());
    for (const Completion& c : completions)
    {
        ALChoiceList::Choice choice;
        choice.text  = c.text;
        choice.note  = c.detail;
        choice.icon  = iconOf(c);
        choice.badge = ALCompletionModel::badgeOf(c);
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
    // to the rows it holds, under the word being completed.
    const LLRect local = getLocalRect();
    const S32    width = llmin(COMPLETION_WIDTH, llmax(60, local.getWidth() - 8));
    popup.fill(std::move(choices), chosen, width);
    popup.showUnder(anchorOf(mCompletionModel.range().begin), llmin(static_cast<S32>(completions.size()), COMPLETION_ROWS), width);
    showCompletionDoc();
}

void ALCodeEditor::showCompletionDoc()
{
    ALChoicePopup* popup = popupFound(COMPLETION_POPUP);
    const S32 index = chosenCompletion();
    const std::vector<Completion>& completions = mCompletionModel.list();
    if (index < 0 || index >= static_cast<S32>(completions.size()) || !completions[index].documentation || completions[index].documentation->empty())
    {
        if (popup)
        {
            popup->hideSide();
        }
        return;
    }
    const Completion& c = completions[index];
    // Its declaration as code, then what it does in the reading face.
    std::string says = c.detail.empty() ? c.text : c.detail;
    if (c.deprecated)
    {
        says += "\n" + deprecatedNote();
    }
    says += "\n" + *c.documentation;
    // The same row chosen as the list is made again -- a letter more
    // typed, an answer joined to it -- beside a list where it was: the box
    // as it stands, not lexed and read for links again.
    if (popup->sideSays(says))
    {
        return;
    }
    dress(*popup);
    ALTextView& box = popup->side();
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
    popup->placeSide();
    popup->sideSaid(says);
}

void ALCodeEditor::supplyCompletions(const ALTextPos& at, std::vector<Completion> more, bool words)
{
    // Only about the identifier the list is still narrowing.
    if (hasSelection() || isReadOnly() || !mCompletionModel.supply(at, std::move(more), words))
    {
        return;
    }
    refreshCompletion();
}

// --- quick fixes -----------------------------------------------------------------

bool ALCodeEditor::fixesOpen() const
{
    const ALChoicePopup* popup = popupFound(FIX_POPUP);
    return popup && popup->listShown();
}

void ALCodeEditor::closeFixes()
{
    const bool was = fixesOpen();
    // Nothing shown, listed or awaited: nothing to close, which is what
    // nearly every edit finds.
    if (!was && !mFixListModel.awaited() && mFixListModel.line() < 0 && mFixListModel.fixes().empty())
    {
        return;
    }
    if (ALChoicePopup* popup = popupFound(FIX_POPUP))
    {
        popup->hideList();
    }
    mFixListModel.close();
}

bool ALCodeEditor::openFixes(S32 line)
{
    closeFixes();
    if (isReadOnly() || line < 0 || line >= document().lineCount())
    {
        return false;
    }
    std::vector<Fix> fixes;
    if (mFixProvider)
    {
        mFixProvider(line, fixes);
    }
    spellingFixes(line, fixes);
    if (fixes.empty())
    {
        return false;
    }
    ALFixListModel::rank(fixes);
    showFixes(line, std::move(fixes), 0);
    return true;
}

void ALCodeEditor::showFixes(S32 line, std::vector<Fix> fixes, S32 chosen)
{
    closeCompletion();
    hideSignature();
    hideCard();
    const U32 shown = mFixListModel.show(line, std::move(fixes));
    fillFixList(chosen);
    if (mFixesShown)
    {
        mFixesShown(shown, mFixListModel.fixes());
    }
}

ALChoicePopup& ALCodeEditor::fixPopup()
{
    // The fixes offered at a problem, on a list of their own made the same
    // way the first time there are any: a fix is not a completion, and the
    // one list open at a time keeps its own keys.
    if (ALChoicePopup* found = popupFound(FIX_POPUP))
    {
        return *found;
    }
    // As a menu: the mouse over a fix previews it, and a click makes it.
    ALChoicePopup& made = makePopup(FIX_POPUP, "fixes", "fix_preview", true);
    made.list().onPicked([this](S32 index) { takeFix(index); });
    made.list().onChosen([this](S32) {
        if (fixesOpen())
        {
            showFixPreview();
        }
    });
    return made;
}

void ALCodeEditor::fillFixList(S32 chosen)
{
    // In the editor's colours, as the completions are; a suppression, and
    // the word that there is nothing, quieter than what makes a change.
    ALChoicePopup& popup = fixPopup();
    dress(popup);
    const std::vector<Fix>&           fixes = mFixListModel.fixes();
    std::vector<ALChoiceList::Choice> choices;
    for (const Fix& fix : fixes)
    {
        ALChoiceList::Choice choice;
        choice.text = fix.title;
        choice.note = fix.note;
        if (fix.suppress || ALFixListModel::isNothing(fix))
        {
            choice.color = paint(Paint::InlayHint);
        }
        choices.push_back(std::move(choice));
    }
    // As wide as its fixes and their notes, within the view: laid out at
    // the most there is room for, then measured.
    const S32 room = llmax(120, getLocalRect().getWidth() - 8);
    popup.fill(std::move(choices), llclamp(chosen, 0, llmax(0, static_cast<S32>(fixes.size()) - 1)), room);
    const S32 width = llclamp(popup.list().widthFor(), 120, room);
    // Under the caret where it is on the line, else under the line's text.
    const S32          line  = mFixListModel.line();
    const ALTextPos    caret = this->caret();
    const std::string& text  = document().line(llclamp(line, 0, document().lineCount() - 1));
    const size_t       lead  = text.find_first_not_of(" \t");
    const ALTextPos    at    = caret.line == line ? caret : ALTextPos(line, lead == std::string::npos ? 0 : static_cast<S32>(lead));
    popup.showUnder(anchorOf(at), llmin(static_cast<S32>(fixes.size()), COMPLETION_ROWS), width);
    showFixPreview();
}

void ALCodeEditor::noteFixes(U32 shown, const std::vector<std::string>& notes)
{
    if (!fixesOpen() || !mFixListModel.note(shown, notes))
    {
        return;
    }
    fillFixList(fixPopup().list().chosen());
}

void ALCodeEditor::supplyActions(const ALTextRange& at, std::vector<Fix> actions)
{
    if (isReadOnly())
    {
        return;
    }
    const bool                            open   = fixesOpen();
    std::optional<ALFixListModel::Joined> joined = mFixListModel.join(at, caret(), std::move(actions), open, open ? fixPopup().list().chosen() : -1);
    if (joined)
    {
        showFixes(caret().line, std::move(joined->fixes), joined->chosen);
    }
}

void ALCodeEditor::showFixPreview()
{
    ALChoicePopup*          popup = popupFound(FIX_POPUP);
    const S32               index = fixesOpen() ? popup->list().chosen() : -1;
    const std::vector<Fix>& fixes = mFixListModel.fixes();
    if (index < 0 || index >= static_cast<S32>(fixes.size()) || ALFixListModel::isNothing(fixes[index]))
    {
        if (popup)
        {
            popup->hideSide();
        }
        return;
    }
    std::vector<char> kinds;
    const std::string says = ALFixListModel::previewOf(document(), fixes[index], kinds);
    dress(*popup);
    ALTextView& box = popup->side();
    box.setText(says);
    // As a comparison shows it (ALDiffView): each line on the band its
    // kind is tinted, what goes as plain code faded, what comes coloured
    // as code.
    const LLColor4 gone_band = ALDiffColors::get(ALDiffColors::Name::Removed).get();
    const LLColor4 come_band = ALDiffColors::get(ALDiffColors::Name::Added).get();
    LLColor4       faded     = textColor();
    faded.mV[VALPHA] *= 0.7f;
    std::vector<ALTextView::Style> styles;
    std::vector<LineAnnotation>    lines;
    for (S32 line = 0; line < box.document().lineCount() && line < static_cast<S32>(kinds.size()); ++line)
    {
        const char kind = kinds[static_cast<size_t>(line)];
        LineAnnotation& said = lines.emplace_back();
        said.tint            = kind == '-' ? gone_band : kind == '+' ? come_band : LLColor4::transparent;
        if (kind == '-')
        {
            ALTextView::Style gone;
            gone.range = ALTextRange(ALTextPos(line, 0), box.document().lineEnd(line));
            gone.font  = getFont();
            gone.color = faded;
            styles.push_back(gone);
        }
        else if (kind == '+')
        {
            styleAsCode(box, line, styles);
        }
    }
    box.setStyles(std::move(styles));
    box.setLineAnnotations(std::move(lines));
    popup->placeSide();
}

void ALCodeEditor::takeFix(S32 index)
{
    const std::vector<Fix>& fixes = mFixListModel.fixes();
    if (index < 0 || index >= static_cast<S32>(fixes.size()))
    {
        return;
    }
    // Made by whoever gave it, which knows whether the text is still the
    // one it was made for.
    const LLSD value   = fixes[index].value;
    const bool nothing = ALFixListModel::isNothing(fixes[index]);
    closeFixes();
    // The dictionary's, made here: the word at the caret put right, or
    // taken into it.
    if (value.isMap() && (value.has("spelling") || value.has("spelling_add")))
    {
        refreshSuggestions();
        if (value.has("spelling"))
        {
            replaceWithSuggestion(static_cast<U32>(value["spelling"].asInteger()));
        }
        else
        {
            addToDictionary();
        }
        return;
    }
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
    const ALTextRange at = selection().normalised();
    mFixListModel.ask(at, caret());
    mActionRequest(at);
    return true;
}

void ALCodeEditor::spellingFixes(S32 line, std::vector<Fix>& fixes)
{
    // The misspelled word at the caret, where the list is its line's: the
    // dictionary's first few words for it, and it taken in.
    ALTextRange word;
    if (caret().line != line || !misspelledAt(caret(), &word))
    {
        return;
    }
    refreshSuggestions();
    const U32 count = llmin(getSuggestionCount(), U32(5));
    for (U32 i = 0; i < count; ++i)
    {
        Fix fix;
        fix.title = alSaid("CodeFixSpelling", "Change to \"[WORD]\"", { { "[WORD]", getSuggestion(i) } });
        fix.edits.emplace_back(word, getSuggestion(i));
        fix.value = LLSD().with("spelling", static_cast<S32>(i));
        fixes.push_back(std::move(fix));
    }
    if (canAddToDictionary())
    {
        Fix fix;
        fix.title    = alSaid("CodeFixAddWord", "Add \"[WORD]\" to the dictionary", { { "[WORD]", document().text(word) } });
        fix.refactor = true;
        fix.value    = LLSD().with("spelling_add", true);
        fixes.push_back(std::move(fix));
    }
}

bool ALCodeEditor::canQuickFix() const
{
    if (isReadOnly())
    {
        return false;
    }
    // A misspelling at the caret has the dictionary's words for it; the
    // check of a line is kept once made, which is why it is not const.
    if (const_cast<ALCodeEditor*>(this)->misspelledAt(caret()))
    {
        return true;
    }
    if (!mFixHandler)
    {
        return false;
    }
    return mActionRequest || (mFixProvider && fixableAt(caret().line));
}

void ALCodeEditor::openCompletion(bool asked)
{
    mCompletionAsked = mCompletionAsked || asked;
    refreshCompletion();
}

bool ALCodeEditor::returnAccepts() const
{
    if (!mAcceptOnEnter || !completionOpen())
    {
        return false;
    }
    if (mCompletionMoved)
    {
        return true;
    }
    // Not moved through: taken only where it changes what is typed.
    const S32 index = chosenCompletion();
    if (index < 0 || index >= static_cast<S32>(mCompletionModel.list().size()))
    {
        return false;
    }
    return mCompletionModel.list()[index].text != document().text(mCompletionModel.range());
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

    // A string's or a path's: an include's name is a path, a require's a
    // string.
    bool isQuotedKind(ALSyntaxKind kind)
    {
        return isStringKind(kind) || kind == ALSyntaxKind::Path;
    }

    // The run of tokens one after another of the kinds `wanted` takes that
    // a column is in -- or at the end of as well, `at_end` -- as the columns
    // of its line it begins and ends at. False where it is in none.
    bool tokenRunAt(const std::vector<ALSyntaxToken>& tokens, S32 column, bool at_end, bool (*wanted)(ALSyntaxKind), S32& begin, S32& end)
    {
        size_t at = tokens.size();
        for (size_t t = 0; t < tokens.size(); ++t)
        {
            if (wanted(tokens[t].kind) && tokens[t].begin <= column && (column < tokens[t].end || (at_end && column == tokens[t].end)))
            {
                at = t;
                break;
            }
        }
        if (at == tokens.size())
        {
            return false;
        }
        size_t first = at;
        size_t last  = at;
        while (first > 0 && wanted(tokens[first - 1].kind) && tokens[first - 1].end == tokens[first].begin)
        {
            --first;
        }
        while (last + 1 < tokens.size() && wanted(tokens[last + 1].kind) && tokens[last + 1].begin == tokens[last].end)
        {
            ++last;
        }
        begin = tokens[first].begin;
        end   = tokens[last].end;
        return true;
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

std::optional<ALTextRange> ALCodeEditor::pathAt(const ALTextPos& pos)
{
    const std::shared_ptr<const ALSyntaxGrammar> grammar = highlighter().grammar();
    char                                         opener  = '\0';
    const std::optional<ALTextRange>             held    = grammar ? quotedAt(pos, &opener) : std::nullopt;
    if (!held || (opener != '"' && opener != '\'' && opener != '`' && opener != '<'))
    {
        return std::nullopt;
    }
    // A string the grammar says names a file, by what comes before it.
    if (!grammar->pathString(std::string_view(document().line(held->begin.line)).substr(0, held->begin.column - 1)))
    {
        return std::nullopt;
    }
    return held;
}

std::optional<ALTextRange> ALCodeEditor::quotedAt(const ALTextPos& pos, char* opener, bool* closed)
{
    if (pos.line < 0 || pos.line >= document().lineCount())
    {
        return std::nullopt;
    }
    // The run of string and path tokens the position is in, or at the end
    // of.
    S32 begin = 0;
    S32 end   = 0;
    if (!tokenRunAt(highlighter().tokens(pos.line), pos.column, /*at_end*/ true, isQuotedKind, begin, end))
    {
        return std::nullopt;
    }
    const std::string& line = document().line(pos.line);
    end                     = std::min(end, static_cast<S32>(line.size()));
    const char         open = begin < end ? line[begin] : '\0';
    if (open != '"' && open != '\'' && open != '`' && open != '<')
    {
        return std::nullopt;
    }
    // Between the quotes; to the line's end where the string is not
    // closed -- its last byte no quote, or a quote a backslash escapes,
    // `"Say \"` typed so far.
    const char close   = open == '<' ? '>' : open;
    S32        escapes = 0;
    while (end - 2 - escapes > begin && line[end - 2 - escapes] == '\\')
    {
        ++escapes;
    }
    const bool shut = end - 1 > begin && line[end - 1] == close && escapes % 2 == 0;
    const S32  held = shut ? end - 1 : end;
    if (pos.column <= begin || pos.column > held)
    {
        return std::nullopt;
    }
    if (opener)
    {
        *opener = open;
    }
    if (closed)
    {
        *closed = shut;
    }
    return ALTextRange(ALTextPos(pos.line, begin + 1), ALTextPos(pos.line, held));
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

bool ALCodeEditor::completesInProse(const ALTextPos& at)
{
    const std::shared_ptr<const ALSyntaxGrammar> grammar = highlighter().grammar();
    if (!grammar || at.line < 0 || at.line >= document().lineCount())
    {
        return false;
    }
    const std::string& line = document().line(at.line);
    return grammar->completesIn(std::string_view(line).substr(0, std::min(line.size(), static_cast<size_t>(std::max(0, at.column)))));
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
        return tokenRunAt(me.highlighter().tokens(line), column, /*at_end*/ false, isStringKind, begin, end);
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
        says += ", " + alSaid("CodeStringWritten", "[COUNT] in source", args);
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
    if (command == ALEditorCommand::GoToDefinition && mLinkRequest && !mLinkRequest(caret(), false).empty())
    {
        return true;
    }
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
    // A name that leads somewhere of itself goes there: an include's.
    if (command == ALEditorCommand::GoToDefinition && mLinkRequest && !mLinkRequest(caret(), true).empty())
    {
        return true;
    }
    // Asked for by the link, which has gone -- with nobody to ask about a
    // name, or no name to ask about.
    if (!mSymbolRequest || identifierAtCaret().empty())
    {
        return false;
    }
    mSymbolRequest(command, identifierAtCaret());
    return true;
}

bool ALCodeEditor::complete()
{
    if (isReadOnly())
    {
        return false;
    }
    openCompletion(true);
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

namespace
{
    // How a completion taken is written as a call, where the identifier it
    // replaces ends at `end` of `line`: not at all where it is no call, or
    // the brackets are there already; else with the parameters its detail
    // names, to tab through, and whether the caret goes between the
    // brackets. As whoever answered says, where they say: an empty pair
    // with the caret after it, or the caret between them; else as its kind
    // and its detail read.
    struct CallShape
    {
        bool                     called = false;
        bool                     takes  = false;
        std::vector<std::string> names;
    };

    CallShape callShapeOf(const ALCompletion& chosen, const std::string& line, S32 end)
    {
        CallShape  shape;
        const bool already = end < static_cast<S32>(line.size()) && line[end] == '(';
        switch (chosen.brackets)
        {
            case ALCompletion::Brackets::Guess:
                shape.called = chosen.kind == ALSyntaxKind::Function;
                break;
            case ALCompletion::Brackets::None:
                shape.called = false;
                break;
            default:
                shape.called = true;
                break;
        }
        shape.called = shape.called && !already;
        if (!shape.called || chosen.brackets == ALCompletion::Brackets::After)
        {
            return shape;
        }
        shape.names        = ALSnippetSession::parameterNames(chosen.detail, chosen.text);
        const size_t open  = ALSnippetSession::parameterListAt(chosen.detail, chosen.text);
        const size_t after = open == std::string::npos ? std::string::npos : chosen.detail.find_first_not_of(' ', open + 1);
        shape.takes        = chosen.brackets == ALCompletion::Brackets::Inside || open == std::string::npos || after == std::string::npos ||
                      chosen.detail[after] != ')';
        return shape;
    }
}

bool ALCodeEditor::acceptCompletion()
{
    if (!completionOpen())
    {
        return false;
    }
    const S32 index = chosenCompletion();
    if (index < 0 || index >= static_cast<S32>(mCompletionModel.list().size()))
    {
        closeCompletion();
        return false;
    }
    const Completion  chosen = mCompletionModel.list()[index];
    const ALTextRange range  = mCompletionModel.range();
    closeCompletion();
    complete(chosen, range);
    return true;
}

void ALCodeEditor::complete(const Completion& chosen, const ALTextRange& range)
{
    // In a string that names a file, or one the host names something for:
    // a whole path, or what it named, in place of what the string holds,
    // its closing quote kept; else the name in place of the one typed. In
    // one not closed, which runs on to the line's end, only what it holds
    // up to the caret, and the quote closed after it: what follows the
    // caret is the script's. A folder is followed by a slash, and the list
    // again, inside the quote.
    if ((!chosen.path.empty() || chosen.folder) && !hasOtherSelections())
    {
        char                             opener = '\0';
        bool                             closed = true;
        const std::optional<ALTextRange> held   = quotedAt(range.end, &opener, &closed);
        const bool                       closes = held && !closed;
        ALTextRange                      over   = range;
        std::string                      put    = chosen.text;
        if (!chosen.path.empty() && held)
        {
            over = closed ? *held : ALTextRange(held->begin, range.end);
            put  = chosen.path;
        }
        if (chosen.folder && (put.empty() || put.back() != '/'))
        {
            put += '/';
        }
        setSelection(over);
        insertText(closes ? put + (opener == '<' ? '>' : opener) : put);
        if (closes && chosen.folder)
        {
            setCaret(ALTextPos(caret().line, caret().column - 1));
        }
        setFocus(true);
        if (chosen.folder)
        {
            openCompletion(true);
        }
        return;
    }
    if (hasOtherSelections())
    {
        // What it replaces either side of the main caret, which each other
        // caret has replaced where its text reads the same.
        const ALTextPos   at     = caret();
        const std::string before = range.begin < at ? document().text(ALTextRange(range.begin, at)) : std::string();
        const std::string after  = at < range.end ? document().text(ALTextRange(at, range.end)) : std::string();
        undoJournal().beginGroup();
        editEach([&](size_t, const ALTextRange& selection) {
            ALTextRange over = selection.normalised();
            if (over.empty())
            {
                const ALTextPos from(over.end.line, over.end.column - static_cast<S32>(before.size()));
                const ALTextPos to(over.end.line, over.end.column + static_cast<S32>(after.size()));
                if (from.column >= 0 && document().text(ALTextRange(from, over.end)) == before && document().text(ALTextRange(over.end, to)) == after)
                {
                    over = ALTextRange(from, to);
                }
            }
            return std::optional<ALTextEditing::Change>(completionAt(chosen, over));
        });
        undoJournal().endGroup();
        setFocus(true);
        return;
    }
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
    const CallShape shape = callShapeOf(chosen, document().line(range.end.line), range.end.column);
    if (!shape.called)
    {
        insertText(chosen.text);
    }
    else
    {
        // Its parameters as placeholders, where the detail names them;
        // else the caret between the brackets where it takes anything.
        std::string              call = chosen.text + "(";
        std::vector<ALTextRange> places;
        const ALTextPos          begin = range.begin;
        for (size_t i = 0; i < shape.names.size(); ++i)
        {
            if (i > 0)
            {
                call += ", ";
            }
            const S32 from = begin.column + static_cast<S32>(call.size());
            call += shape.names[i];
            places.emplace_back(ALTextPos(begin.line, from), ALTextPos(begin.line, from + static_cast<S32>(shape.names[i].size())));
        }
        call += ")";
        insertText(call);
        if (!places.empty())
        {
            setPlaceholders(std::move(places), caret());
        }
        else if (shape.takes)
        {
            setCaret(ALTextPos(caret().line, caret().column - 1));
        }
        if (shape.takes && mSignatureRequest)
        {
            mSignatureRequest(caret());
        }
    }
    setFocus(true);
}

ALTextEditing::Change ALCodeEditor::completionAt(const Completion& chosen, const ALTextRange& over)
{
    ALTextEditing::Change one;
    if (!chosen.snippet.empty())
    {
        // Indented as the line it goes into, as insertSnippet has it; the
        // caret on its first stop.
        const std::string&                line     = document().line(over.begin.line);
        const std::string                 indent   = line.substr(0, std::min(line.size(), line.find_first_not_of(" \t")));
        const ALSnippetSession::Expansion expanded = ALSnippetSession::expand(chosen.snippet, over.begin, indent,
                                                                              ALTextIndent::indentUnit(indent, { getTabWidth(), getSoftTabs() }));
        const ALTextRange                 land     = expanded.stops.empty() ? expanded.landing : expanded.stops.front();
        one.replacements.push_back({ over, expanded.text });
        one.selects = true;
        one.anchor  = land.begin;
        one.caret   = land.end;
        return one;
    }
    // A function called, as complete() calls it: its first parameter
    // chosen, else the caret between its brackets where it takes anything.
    const CallShape shape = callShapeOf(chosen, document().line(over.end.line), over.end.column);
    if (!shape.called)
    {
        one.replacements.push_back({ over, chosen.text });
        one.caret = ALTextEditing::endOf(over.begin, chosen.text);
        return one;
    }
    const std::vector<std::string>& names = shape.names;
    const bool                      takes = shape.takes;
    std::string                     call  = chosen.text + "(";
    for (size_t i = 0; i < names.size(); ++i)
    {
        call += (i > 0 ? ", " : "") + names[i];
    }
    call += ")";
    one.replacements.push_back({ over, call });
    const ALTextPos end = ALTextEditing::endOf(over.begin, call);
    if (!names.empty())
    {
        const S32 from = over.begin.column + static_cast<S32>(chosen.text.size()) + 1;
        one.selects    = true;
        one.anchor     = ALTextPos(over.begin.line, from);
        one.caret      = ALTextPos(over.begin.line, from + static_cast<S32>(names.front().size()));
    }
    else
    {
        one.caret = takes ? ALTextPos(end.line, end.column - 1) : end;
    }
    return one;
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
    const ALTextRange           selection = this->selection();
    const ALTextPos             at        = std::min(selection.begin, selection.end);
    const std::string&          line      = document().line(at.line);
    const std::string           indent    = line.substr(0, std::min(line.size(), line.find_first_not_of(" \t")));
    ALSnippetSession::Expansion expanded  = ALSnippetSession::expand(body, at, indent, ALTextIndent::indentUnit(indent, { getTabWidth(), getSoftTabs() }));
    insertText(expanded.text);
    const ALTextRange landing = expanded.landing;
    if (expanded.stops.empty())
    {
        setSelection(landing);
        return;
    }
    // $0's own text chosen as the caret lands there, where it is on one
    // line.
    mSnippet.start(std::move(expanded.stops), landing.begin, std::move(expanded.mirrors),
                   landing.begin.line == landing.end.line ? landing.end.column - landing.begin.column : 0);
    setSelection(mSnippet.stops()[0]);
}

void ALCodeEditor::syncMirrors(S32 index)
{
    // As one edit, one step to undo, and the selection as it was.
    std::string            wanted;
    const std::vector<S32> order = mSnippet.staleMirrors(index, document(), wanted);
    if (order.empty())
    {
        return;
    }
    const ALTextRange                                was = selection();
    std::vector<std::pair<ALTextRange, std::string>> edits;
    edits.reserve(order.size());
    for (const S32 k : order)
    {
        edits.emplace_back(mSnippet.mirrors()[static_cast<size_t>(k)].range, wanted);
    }
    // All of them as one edit.
    undoJournal().beginGroup();
    mSnippet.syncingAll();
    editMany(std::move(edits), was.end);
    mSnippet.syncing(-1);
    undoJournal().endGroup();
    placeSelection(document().clamp(was.begin), document().clamp(was.end));
    afterEdit();
}

std::vector<ALTextRange> ALCodeEditor::placesOf(const std::string& wanted, bool whole, const ALTextPos& from, size_t most,
                                                const std::vector<ALTextRange>& taken_in) const
{
    std::vector<ALTextRange> out;
    const ALTextDocument&    doc   = document();
    const S32                count = doc.lineCount();
    if (wanted.empty() || wanted.find('\n') != std::string::npos)
    {
        return out;
    }
    // Taken already: any over one of the selections.
    const auto taken = [&taken_in](const ALTextRange& range) {
        return std::any_of(taken_in.begin(), taken_in.end(), [&range](const ALTextRange& one) {
            const ALTextRange r = one.normalised();
            return range.begin < r.end && r.begin < range.end;
        });
    };
    // Each line once, from `from` round to it again.
    for (S32 step = 0; step <= count && out.size() < most; ++step)
    {
        const S32          line  = (from.line + step) % count;
        const std::string& text  = doc.line(line);
        const size_t       start = step == 0 ? static_cast<size_t>(from.column) : 0;
        const size_t       stop  = step == count ? static_cast<size_t>(from.column) : text.size();
        for (size_t at = text.find(wanted, start); at != std::string::npos && at + wanted.size() <= stop && out.size() < most;
             at = text.find(wanted, at + 1))
        {
            const size_t end = at + wanted.size();
            if (whole && ((at > 0 && alIdentifierByte(text[at - 1])) || (end < text.size() && alIdentifierByte(text[end]))))
            {
                continue;
            }
            const ALTextRange range(ALTextPos(line, static_cast<S32>(at)), ALTextPos(line, static_cast<S32>(end)));
            if (!taken(range) && std::none_of(out.begin(), out.end(), [&](const ALTextRange& r) { return range.begin < r.end && r.begin < range.end; }))
            {
                out.push_back(range);
            }
        }
    }
    return out;
}

bool ALCodeEditor::selectNextOccurrence()
{
    const ALTextRange taken = selection().normalised();
    if (taken.empty())
    {
        // The name at the caret, to go on from.
        ALTextRange name = identifierAtCaret();
        if (name.empty())
        {
            name = document().wordAt(caret());
        }
        if (name.empty())
        {
            return false;
        }
        // And at every other caret, its own.
        size_t                   main = 0;
        std::vector<ALTextRange> all  = selectionsInOrder(&main);
        for (ALTextRange& one : all)
        {
            if (one.empty())
            {
                const ALTextRange word = identifierAt(one.end);
                one                    = word.empty() ? document().wordAt(one.end) : word;
            }
        }
        all[main] = name;
        placeSelections(all, main);
        setSelection(selection());
        mOccurrenceName = selection().normalised();
        return true;
    }
    if (taken.begin.line != taken.end.line || isReadOnly())
    {
        return false;
    }
    // After the main selection, going round, past those taken: the next
    // one the main one, so that they are taken in turn.
    std::vector<ALTextRange>       all   = selectionsInOrder();
    const bool                     whole = taken == mOccurrenceName;
    const std::vector<ALTextRange> next  = placesOf(document().text(taken), whole, taken.end, 1, all);
    if (next.empty())
    {
        return false;
    }
    all.push_back(next.front());
    placeSelections(all, all.size() - 1);
    setSelection(selection());
    mOccurrenceName = whole ? selection().normalised() : ALTextRange();
    return true;
}

bool ALCodeEditor::changeAllOccurrences()
{
    ALTextRange taken = selection().normalised();
    bool        whole = taken == mOccurrenceName;
    if (taken.empty())
    {
        taken = identifierAtCaret();
        whole = true;
    }
    if (taken.empty() || taken.begin.line != taken.end.line || isReadOnly())
    {
        return false;
    }
    // Every place, the one taken the main selection; the others there
    // were let go of.
    std::vector<ALTextRange> all = placesOf(document().text(taken), whole, taken.end, std::numeric_limits<size_t>::max(), { taken });
    if (all.empty())
    {
        return false;
    }
    all.push_back(taken);
    placeSelections(all, all.size() - 1);
    setSelection(selection());
    return true;
}

void ALCodeEditor::dropPlaceholdersLeft()
{
    if (!mSnippet.active() || mSnippet.syncing())
    {
        return;
    }
    if (!mSnippet.reaches(caret().line))
    {
        clearPlaceholders();
    }
}

// --- placeholders ------------------------------------------------------------------

void ALCodeEditor::setPlaceholders(std::vector<ALTextRange> ranges, const ALTextPos& after)
{
    mSnippet.start(std::move(ranges), after);
    if (mSnippet.at() >= 0)
    {
        setSelection(mSnippet.stops()[0]);
    }
}

bool ALCodeEditor::nextPlaceholder(S32 direction)
{
    if (!mSnippet.active())
    {
        return false;
    }
    const S32 to = mSnippet.at() + direction;
    if (to < 0)
    {
        return false;
    }
    // What was typed over the one being left, in its mirrors.
    syncMirrors(mSnippet.at());
    if (to >= static_cast<S32>(mSnippet.stops().size()))
    {
        // Past the last: after the call, done -- $0's text chosen.
        const ALTextPos after   = mSnippet.after();
        const S32       landing = mSnippet.landing();
        clearPlaceholders();
        setSelection(ALTextRange(after, ALTextPos(after.line, after.column + landing)));
        return true;
    }
    mSnippet.moveTo(to);
    setSelection(mSnippet.stops()[to]);
    if (mSignatureRequest)
    {
        mSignatureRequest(caret());
    }
    return true;
}

void ALCodeEditor::clearPlaceholders()
{
    mSnippet.clear();
}

// --- cards -------------------------------------------------------------------

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
    // The analyzer's word first, where there is one to ask: it tells a
    // local from a builtin of the same name, and a field from a global.
    // What the definitions say of the word is said where the analyzer has
    // nothing, or where there is no analyzer to ask.
    std::string fallback;
    const bool  known = says.empty() && mHover && !word.empty() && mHover(at, document().text(word), fallback);
    mHoverFallback    = known ? fallback : std::string();
    if (says.empty() && !word.empty())
    {
        // What the analyzer said of this word, if it was asked and the
        // text has not moved on since; else asked now, for an answer
        // that shows when it comes, or the next time the mouse rests
        // here.
        const U32 version = document().version();
        if (!mHoverRequest)
        {
            says = mHoverFallback;
        }
        else if (mCards.askedAbout(word, version))
        {
            says  = mCards.answer().empty() ? mHoverFallback : mCards.answer();
            links = mCards.answer().empty() ? std::vector<CardLink>() : mCards.links();
        }
        else
        {
            mCards.asking(word, version);
            mHoverRequest(word.begin, document().text(word));
        }
        if (!says.empty())
        {
            about = about.empty() ? word : ALTextRange(std::min(about.begin, word.begin), std::max(about.end, word.end));
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
    for (const Decoration* each : decorationsOn(at.line))
    {
        const Decoration& d     = *each;
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
    return ALCodeCards::deprecatedNote();
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
    const S32              PAD       = ALCodeCards::PAD;
    if (!mCard)
    {
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name        = "hover_card";
        p.rect        = LLRect(0, 20, ALCodeCards::MAX_WIDTH, 0);
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
    // What would put the problems right: the fixes of the line they are
    // on, best first.
    std::vector<Fix> fixes;
    if (!problems.empty() && mFixProvider && mFixHandler && !isReadOnly())
    {
        mFixProvider(about.begin.line, fixes);
        ALFixListModel::rank(fixes);
    }
    const ALCodeCards::Composition card = ALCodeCards::compose(problems, fixes, says, links);
    const std::string&             all  = card.text;
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
    for (const auto& [line, problem] : card.problemLines)
    {
        ALTextView::Style one;
        one.range = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
        one.color = problems[problem].color;
        styles.push_back(one);
    }
    if (card.headLine >= 0)
    {
        // The name in the head coloured as the text colours it where the
        // analyzer said what it is: a global, a parameter, a function of
        // the script's own, which the grammar alone does not know.
        const ALTextRange word = mMouseX >= 0 ? identifierAt(posAtLocal(mMouseX, mMouseY, false)) : identifierAt(about.begin);
        styleAsCode(*mCard, card.headLine, styles, document().text(word), word.empty() ? ALSyntaxKind::Text : semanticKindAt(word.begin));
    }
    const S32 lines = mCard->document().lineCount();
    for (const S32 line : card.deprecatedLines)
    {
        ALTextView::Style note;
        note.range = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
        note.color = markColor(Mark::Warning);
        styles.push_back(note);
    }
    mCard->setStyles(std::move(styles));
    for (const auto& [line, value] : card.fixLines)
    {
        ALTextView::Substitution fix;
        fix.range = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
        fix.link  = true;
        fix.value = LLSD().with("fix", value);
        mCard->addSubstitution(std::move(fix));
    }
    // The caller's own links, each on the line that says it, below the
    // head; then every URL.
    for (const auto& [line, link] : card.linkLines)
    {
        ALTextView::Substitution way;
        way.range   = ALTextRange(ALTextPos(line, 0), mCard->document().lineEnd(line));
        way.link    = true;
        way.tooltip = links[link].tooltip;
        way.value   = links[link].value;
        mCard->addSubstitution(std::move(way));
    }
    for (S32 line = 0; line < lines; ++line)
    {
        mCard->linkUrlsOn(line);
    }
    // Its size: as wide as its widest line up to the limit, and as tall
    // as the lines wrapped at that width come to.
    const LLRect text  = textRect();
    const S32    limit = ALCodeCards::widthLimit(text.getWidth());
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
    const S32 width = ALCodeCards::width(static_cast<S32>(mCard->layout().contentWidth()), limit);
    mCard->setWordWrap(true);
    mCard->setShape(LLRect(0, 40, width, 0));
    for (S32 line = 0; line < lines; ++line)
    {
        mCard->layout().line(line);
    }
    const S32 height = ALCodeCards::height(mCard->layout().totalHeight());
    // Where: under the row of what it is about, at its start; above it
    // where under would run off the bottom; within the text's width.
    mCardAnchor = anchorOf(about);
    mCard->setShape(ALCodeCards::place(mCardAnchor, text, width, height));
    mCard->setVisible(true);
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
    // Kept for the word, and shown now if the mouse is still on it.
    if (!mCards.heard(at, document().version(), text, std::move(links)) || mMouseX < 0 || !textRect().pointInRect(mMouseX, mMouseY))
    {
        return;
    }
    const ALTextPos   under = posAtLocal(mMouseX, mMouseY, false);
    const ALTextRange word  = identifierAt(under);
    if (word != mCards.asked())
    {
        return;
    }
    ALTextRange                    about;
    const std::vector<CardProblem> problems = problemsUnder(under, about);
    // Nothing from the analyzer: what the definitions say, where they say
    // anything.
    const std::string& says = text.empty() ? mHoverFallback : text;
    if (says.empty() && problems.empty())
    {
        return;
    }
    showCard(about.empty() ? word : ALTextRange(std::min(about.begin, word.begin), std::max(about.end, word.end)), says, problems,
             text.empty() ? std::vector<CardLink>() : mCards.links());
}

// --- signature help -------------------------------------------------------------

void ALCodeEditor::showSignature(const ALTextPos& at, Signature signature)
{
    if (!typingText())
    {
        // An answer that came after a modal keymap stopped inserting.
        return;
    }
    // The call it is about, by its bracket, which it stays shown inside.
    ALTextPos open;
    mSignatureOpen = mBracketIndex.enclosing(at, '(', 1, open, ALBracketIndex::NEARBY) ? open : ALTextPos(-1, -1);
    mCards.showSignature(at, std::move(signature));
}

void ALCodeEditor::hideSignature()
{
    mCards.hideSignature();
    mSignatureOpen = ALTextPos(-1, -1);
}

bool ALCodeEditor::signatureShown() const
{
    if (!mCards.signature() || mSignatureOpen.line < 0)
    {
        return mCards.signatureFor(caret());
    }
    // Inside the call's brackets, on whichever of its lines.
    const ALTextPos at = caret();
    if (!(mSignatureOpen < at))
    {
        return false;
    }
    ALTextPos close;
    return !const_cast<ALBracketIndex&>(mBracketIndex).match(mSignatureOpen, close, ALBracketIndex::NEARBY) || !(close < at);
}

S32 ALCodeEditor::argumentAt(const ALTextPos& open, const ALTextPos& at)
{
    S32 depth  = 0;
    S32 commas = 0;
    for (S32 line = open.line; line <= at.line && line < document().lineCount(); ++line)
    {
        const std::string& text   = document().line(line);
        const auto&        tokens = highlighter().tokens(line);
        size_t             t      = 0;
        const S32          from   = line == open.line ? open.column + 1 : 0;
        const S32          to     = line == at.line ? llmin(at.column, static_cast<S32>(text.size())) : static_cast<S32>(text.size());
        for (S32 i = from; i < to; ++i)
        {
            const char c = text[static_cast<size_t>(i)];
            if (c != ',' && c != '(' && c != ')' && c != '[' && c != ']' && c != '{' && c != '}')
            {
                continue;
            }
            // Not what a string or a comment says.
            while (t < tokens.size() && tokens[t].end <= i)
            {
                ++t;
            }
            if (t < tokens.size() && tokens[t].begin <= i &&
                (tokens[t].kind == ALSyntaxKind::String || tokens[t].kind == ALSyntaxKind::Comment || tokens[t].kind == ALSyntaxKind::DocComment))
            {
                continue;
            }
            if (c == ',')
            {
                commas += depth == 0 ? 1 : 0;
            }
            else
            {
                depth += (c == '(' || c == '[' || c == '{') ? 1 : -1;
            }
        }
    }
    return commas;
}

void ALCodeEditor::drawSignature(const LLRect& text)
{
    const Signature* shown = mCards.signature();
    if (!shown || shown->label.empty())
    {
        return;
    }
    const S32        SIGNATURE_PAD = ALCodeCards::SIGNATURE_PAD;
    const Signature& sig   = *shown;
    const LLFontGL*  font  = getFont();
    const F32        alpha = getDrawContext().mAlpha;
    const S32        line_h = font->getLineHeight();
    // Its pieces measured once for the signature as it stands, and again
    // only when it, its active parameter or the font moves.
    SignatureShown& measured = mSignatureShown;
    if (measured.label != sig.label || measured.documentation != sig.documentation || measured.overload != sig.overload ||
        measured.overloads != sig.overloads.size() || measured.active != sig.active || measured.font != font)
    {
        measured.label         = sig.label;
        measured.documentation = sig.documentation;
        measured.overload      = sig.overload;
        measured.overloads     = sig.overloads.size();
        measured.active        = sig.active;
        measured.font          = font;
        // Which form of the function, of how many, where it has several:
        // Up and Down go through them.
        const std::string counter = sig.overloads.size() > 1 ? llformat("%d/%d  ", sig.overload + 1, static_cast<S32>(sig.overloads.size())) : std::string();
        measured.docs             = !sig.documentation.empty() || !counter.empty();
        measured.docLine          = counter + sig.documentation.substr(0, sig.documentation.find('\n'));
        measured.labelWidth       = font->getWidth(sig.label);
        measured.docWidth         = measured.docs ? font->getWidth(measured.docLine) : 0;
        measured.begin            = -1;
        measured.end              = -1;
        if (sig.active >= 0 && sig.active < static_cast<S32>(sig.parameters.size()))
        {
            measured.begin = sig.parameters[sig.active].first;
            measured.end   = sig.parameters[sig.active].second;
        }
        measured.throughWidth = measured.end > 0 ? font->getWidth(sig.label.substr(0, static_cast<size_t>(measured.end))) : measured.labelWidth;
        // The label in three pieces, the active parameter the middle one;
        // or whole, where no parameter is active.
        const S32  size    = static_cast<S32>(sig.label.size());
        const bool split   = measured.begin >= 0 && measured.end > measured.begin && measured.end <= size;
        const S32  cuts[4] = { 0, split ? measured.begin : size, split ? measured.end : size, size };
        for (S32 i = 0; i < 3; ++i)
        {
            measured.pieces[i] = sig.label.substr(static_cast<size_t>(cuts[i]), static_cast<size_t>(cuts[i + 1] - cuts[i]));
            measured.widths[i] = measured.pieces[i].empty() ? 0 : font->getWidth(measured.pieces[i]);
        }
    }
    const bool         docs     = measured.docs;
    const std::string& doc_line = measured.docLine;
    const S32          wanted   = llmax(measured.labelWidth, docs ? measured.docWidth : 0) + 2 * SIGNATURE_PAD;
    const S32          height   = line_h * (docs ? 2 : 1) + 2 * SIGNATURE_PAD;

    // Above the caret's row, left with the call's column, kept inside the
    // view; under the row where above would run off the top.
    const LLRect box  = ALCodeCards::signatureBox(wanted, height, anchorOf(mCards.signatureAt()), getLocalRect());
    const S32    room = box.getWidth() - 2 * SIGNATURE_PAD;

    const LLColor4 ink    = textColor() % alpha;
    const LLColor4 active = mBracketMatchColor.get() % alpha;
    const LLColor4 faint  = lineNumberColor() % alpha;
    gl_rect_2d(box, paint(Paint::Widget) % alpha, true);
    gl_rect_2d(box, paint(Paint::WidgetBorder) % alpha, false);

    // The label in three pieces, the active parameter in its own colour.
    const F32 baseline = static_cast<F32>(box.mTop - SIGNATURE_PAD - llround(font->getAscenderHeight()));
    // Where the label is longer than the room, it is scrolled so that the
    // parameter being filled in is in the box. The head of a signature is
    // the part already typed, so it is the part to give up; what runs off
    // the left edge under the clip reads as more of it being there.
    const F32 label_w = static_cast<F32>(measured.labelWidth);
    const F32 through = static_cast<F32>(measured.throughWidth);
    const F32 shift   = ALCodeCards::labelShift(label_w, through, static_cast<F32>(room));
    F32       pen     = static_cast<F32>(box.mLeft + SIGNATURE_PAD) - shift;
    {
        LLLocalClipRect clip(LLRect(box.mLeft + SIGNATURE_PAD, box.mTop - 1, box.mRight - SIGNATURE_PAD, box.mBottom + 1));
        for (S32 i = 0; i < 3; ++i)
        {
            if (!measured.pieces[i].empty())
            {
                font->renderUTF8(measured.pieces[i], 0, pen, baseline, i == 1 ? active : ink, LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::NORMAL,
                                 LLFontGL::NO_SHADOW);
                pen += static_cast<F32>(measured.widths[i]);
            }
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
    // A peek open steps by the keys a comparison steps by, ahead of the
    // misspellings they otherwise walk.
    if (key == KEY_F7 && (mask == MASK_NONE || mask == MASK_SHIFT) && mPeek && mPeek->isOpen())
    {
        mPeek->step(mask == MASK_NONE);
        return true;
    }
    // Escape closes a peek open once nothing else here would take it: the
    // find bar, a selection, other carets, an atom's view, a modal
    // keymap's command line.
    const auto escapes_peek = [&]() {
        std::string typed;
        S32         at = 0;
        return key == KEY_ESCAPE && mask == MASK_NONE && mPeek && mPeek->isOpen() && !findShown() && !hasSelection() && !hasOtherSelections() &&
               !atomViewFocused() && !(modalKeymap() && modalKeymap()->typingLine(typed, at));
    };
    // At several carets the signature and a snippet's stops are not in
    // play, each being about one place; the list is the main caret's.
    if (hasOtherSelections() && typingText())
    {
        hideSignature();
        clearPlaceholders();
    }
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
                    fixPopup().list().moveChoice(-1, true);
                    return true;
                case KEY_DOWN:
                    fixPopup().list().moveChoice(1, true);
                    return true;
                case KEY_PAGE_UP:
                    fixPopup().list().moveChoice(-COMPLETION_ROWS, false);
                    return true;
                case KEY_PAGE_DOWN:
                    fixPopup().list().moveChoice(COMPLETION_ROWS, false);
                    return true;
                case KEY_RETURN:
                case KEY_TAB:
                    takeFix(fixPopup().list().chosen());
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
        if (escapes_peek())
        {
            // What the keymap has pending let go of as well.
            ALTextView::handleKeyHere(key, mask);
            closePeek();
            return true;
        }
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
    if (mSnippet.active() && !completionOpen())
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
            syncMirrors(mSnippet.at());
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
        completionPopup().list().moveChoice(key == 'N' ? 1 : -1, true);
        mCompletionMoved = true;
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
                completionPopup().list().moveChoice(-1, true);
                mCompletionMoved = true;
                return true;
            case KEY_DOWN:
                completionPopup().list().moveChoice(1, true);
                mCompletionMoved = true;
                return true;
            case KEY_PAGE_UP:
                completionPopup().list().moveChoice(-COMPLETION_ROWS, false);
                mCompletionMoved = true;
                return true;
            case KEY_PAGE_DOWN:
                completionPopup().list().moveChoice(COMPLETION_ROWS, false);
                mCompletionMoved = true;
                return true;
            case KEY_RETURN:
                if (!returnAccepts())
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
    if (mCards.signature() && key == KEY_ESCAPE && mask == MASK_NONE)
    {
        hideSignature();
        return true;
    }
    // Another form of the function, where it has several.
    if (mCards.signature() && !completionOpen() && mask == MASK_NONE && (key == KEY_UP || key == KEY_DOWN) && signatureShown() &&
        mCards.stepOverload(key == KEY_DOWN ? 1 : -1))
    {
        return true;
    }
    // What a Backspace or a Delete takes: a bracket or a comma changes the
    // call, which is asked about again; anything else only an argument.
    char taking = 0;
    if ((key == KEY_BACKSPACE || key == KEY_DELETE) && mask == MASK_NONE && !hasSelection())
    {
        const std::string& line = document().line(caret().line);
        const S32          at   = key == KEY_BACKSPACE ? caret().column - 1 : caret().column;
        taking                  = at >= 0 && at < static_cast<S32>(line.size()) ? line[static_cast<size_t>(at)] : 0;
    }
    const bool call_changes = hasSelection() || taking == '(' || taking == ',' || taking == ')';
    if (key == KEY_BACKSPACE && mask == MASK_NONE && mAutoClose && (hasOtherSelections() ? deletePairs() : deletePair()))
    {
        if (mCards.signature() && mSignatureRequest && call_changes)
        {
            mSignatureRequest(caret());
        }
        return true;
    }
    if (escapes_peek())
    {
        closePeek();
        return true;
    }
    const bool taken = ALTextView::handleKeyHere(key, mask);
    if (!typingText())
    {
        // The key left the inserting mode: Control-[ or Control-C in vim.
        dropTyping();
        return taken;
    }
    if (taken && mCards.signature() && mSignatureRequest && (key == KEY_BACKSPACE || key == KEY_DELETE) && call_changes)
    {
        mSignatureRequest(caret());
    }
    return taken;
}

bool ALCodeEditor::handleUnicodeCharHere(llwchar uni_char)
{
    const bool typing   = typingText();
    const bool was_open = completionOpen();
    // At several carets the signature and a snippet's stops, each about
    // one place, go; the list stays, the main caret's, and what is chosen
    // from it goes in at each.
    const bool several = typing && hasOtherSelections();
    if (several)
    {
        hideSignature();
        clearPlaceholders();
    }
    // The pair, the character and its outdent one key typed, one with the
    // typing around it.
    undoJournal().beginTyping(selection());
    const bool paired = typing && mAutoClose && uni_char < 0x80 && !isReadOnly() &&
                        (several ? typePairs(static_cast<char>(uni_char)) : typePair(static_cast<char>(uni_char)));
    const bool typed  = paired || ALTextView::handleUnicodeCharHere(uni_char);
    undoJournal().endTyping();
    if (!typed)
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
    // In a string that names a file -- a require's, an include's -- what
    // is typed completes as a path, where anyone answers there: the list
    // at its opening quote and after each slash, narrowed as a name is
    // typed. Its closing quote leaves the string, and the list with it. A
    // list open on it already was narrowed as the text changed.
    if (mAutoComplete && !several && (mPathProvider || mPathRequest) && pathAt(caret()))
    {
        if (was_open && mCompletionPath)
        {
            mCompletionAsked = true;
        }
        else
        {
            openCompletion(true);
        }
        return true;
    }
    // In any other string the host names something for -- an item's name,
    // where the call wants one -- the same: the list at its opening quote,
    // narrowed as it is typed.
    if (ALTextPos start; mAutoComplete && !several && mStringProvider)
    {
        std::string typed;
        if (stringOffers(caret(), start, typed))
        {
            if (was_open && mCompletionString)
            {
                mCompletionAsked = true;
            }
            else
            {
                mCompletionString = true;
                openCompletion(true);
            }
            return true;
        }
    }
    const bool identifier = uni_char < 0x80 && alIdentifierByte(static_cast<char>(uni_char));
    // In a comment or a string what is typed is prose: the list does not
    // open on its own there, where a Return meant as a new line would
    // otherwise put a call into the comment. Asked for, it still opens;
    // and on its own where the grammar says what is typed there is code
    // all the same, as SLua's `--!strict` is.
    const bool prose = mAutoComplete && !was_open && inProse(caret()) && !completesInProse(caret());
    // What comes between a name and its member: a dot, and SLua's colon.
    const std::string members = highlighter().grammar() ? highlighter().grammar()->memberSeparators() : std::string(".");
    const bool        member  = uni_char < 0x80 && members.find(static_cast<char>(uni_char)) != std::string::npos;
    if (member && mAutoComplete && !prose && caret().column >= 2 && !identifierAt(ALTextPos(caret().line, caret().column - 2)).empty())
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
    // A call begins, moves on to its next argument, or ends: asked again
    // then, and only then -- within an argument, the parameter shown
    // follows the caret here.
    if (!several && mSignatureRequest && (uni_char == '(' || uni_char == ',' || uni_char == ')'))
    {
        mSignatureRequest(caret());
    }
    return true;
}

std::optional<ALCodeEditor::Paired> ALCodeEditor::pairAt(const ALTextRange& selection, char c)
{
    const std::shared_ptr<const ALSyntaxGrammar> grammar = highlighter().grammar();
    if (!grammar || grammar->pairs().empty())
    {
        return std::nullopt;
    }
    const auto&        pairs = grammar->pairs();
    const ALTextPos    at    = selection.end;
    const std::string& line  = document().line(at.line);
    const char         next  = at.column < static_cast<S32>(line.size()) ? line[at.column] : '\0';
    // Over the closer typing put in, rather than a second one.
    if (selection.empty() && next == c && std::find(mAutoClosed.begin(), mAutoClosed.end(), at) != mAutoClosed.end())
    {
        for (const auto& [open, close] : pairs)
        {
            if (close == c)
            {
                Paired paired;
                paired.over         = true;
                paired.change.caret = ALTextPos(at.line, at.column + 1);
                return paired;
            }
        }
    }
    for (const auto& [open, close] : pairs)
    {
        if (c != open)
        {
            continue;
        }
        if (!selection.empty())
        {
            // The selection wrapped in the pair, and still chosen inside it.
            const ALTextRange sel   = selection.normalised();
            const std::string inner = std::string(1, open) + document().text(sel);
            Paired            paired;
            paired.change.replacements.push_back({ sel, inner + std::string(1, close) });
            paired.change.selects = true;
            paired.change.anchor  = ALTextPos(sel.begin.line, sel.begin.column + 1);
            paired.change.caret   = ALTextEditing::endOf(sel.begin, inner);
            return paired;
        }
        // Not in a comment or a string, where it is prose; and only before
        // a blank, the line's end, or what closes or ends -- a bracket
        // opened before a word is about that word.
        if (inProse(at))
        {
            return std::nullopt;
        }
        bool room = next == '\0' || isspace(static_cast<unsigned char>(next)) || strchr(";,", next) != nullptr;
        for (const auto& [o, closer] : pairs)
        {
            room = room || (next == closer && o != closer);
        }
        if (!room)
        {
            return std::nullopt;
        }
        // A quote after a letter is an apostrophe, and after itself is the
        // end of an empty string.
        if (open == close && at.column > 0 && (alIdentifierByte(line[at.column - 1]) || line[at.column - 1] == open))
        {
            return std::nullopt;
        }
        Paired paired;
        paired.change.replacements.push_back({ ALTextRange(at, at), std::string(1, open) + std::string(1, close) });
        paired.change.caret = ALTextPos(at.line, at.column + 1);
        paired.closes       = true;
        return paired;
    }
    return std::nullopt;
}

bool ALCodeEditor::typePair(char c)
{
    const std::optional<Paired> paired = pairAt(selection(), c);
    if (!paired)
    {
        return false;
    }
    if (paired->over)
    {
        // Where the typing goes on from, for the key after.
        mAutoClosed.eraseIf([at = caret()](const ALTextPos& closer) { return closer == at; });
        setCaret(paired->change.caret);
        undoJournal().settle(selection());
        return true;
    }
    insertText(paired->change.replacements.front().text);
    if (paired->change.selects)
    {
        setSelection(ALTextRange(paired->change.anchor, paired->change.caret));
    }
    else
    {
        setCaret(paired->change.caret);
    }
    undoJournal().settle(selection());
    if (paired->closes)
    {
        mAutoClosed.insert(caret());
    }
    return true;
}

bool ALCodeEditor::typePairs(char c)
{
    // Worked out at each before any is made.
    const std::vector<ALTextRange>     all = selectionsInOrder();
    std::vector<std::optional<Paired>> paired;
    paired.reserve(all.size());
    bool any = false;
    for (const ALTextRange& one : all)
    {
        paired.push_back(pairAt(one, c));
        any = any || paired.back().has_value();
    }
    if (!any)
    {
        return false;
    }
    // The closers typed over are let go of before the edit moves them.
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (paired[i] && paired[i]->over)
        {
            mAutoClosed.eraseIf([at = all[i].end](const ALTextPos& closer) { return closer == at; });
        }
    }
    const std::string plain(1, c);
    editEach([&](size_t i, const ALTextRange& selection) {
        if (paired[i])
        {
            return std::optional<ALTextEditing::Change>(paired[i]->change);
        }
        const ALTextRange     over = selection.normalised();
        ALTextEditing::Change one;
        one.replacements.push_back({ over, plain });
        one.caret = ALTextEditing::endOf(over.begin, plain);
        return std::optional<ALTextEditing::Change>(std::move(one));
    });
    // Each closer put in, before the caret that was put inside its pair.
    const std::vector<ALTextRange> now = selectionsInOrder();
    if (now.size() == all.size())
    {
        for (size_t i = 0; i < now.size(); ++i)
        {
            if (paired[i] && paired[i]->closes)
            {
                mAutoClosed.insert(now[i].end);
            }
        }
        // A plain one typed where none paired, brought out as it would be
        // were it typed alone.
        outdentEach(static_cast<llwchar>(static_cast<unsigned char>(c)), [&paired](size_t i) { return !paired[i]; });
    }
    undoJournal().settle(selection());
    return true;
}

std::optional<ALTextRange> ALCodeEditor::pairAround(const ALTextPos& at)
{
    const std::shared_ptr<const ALSyntaxGrammar> grammar = highlighter().grammar();
    if (!grammar || at.column == 0 || std::find(mAutoClosed.begin(), mAutoClosed.end(), at) == mAutoClosed.end())
    {
        return std::nullopt;
    }
    const std::string& line = document().line(at.line);
    if (at.column >= static_cast<S32>(line.size()))
    {
        return std::nullopt;
    }
    for (const auto& [open, close] : grammar->pairs())
    {
        if (line[at.column - 1] == open && line[at.column] == close)
        {
            return ALTextRange(ALTextPos(at.line, at.column - 1), ALTextPos(at.line, at.column + 1));
        }
    }
    return std::nullopt;
}

bool ALCodeEditor::deletePair()
{
    if (hasSelection())
    {
        return false;
    }
    const std::optional<ALTextRange> pair = pairAround(caret());
    if (!pair)
    {
        return false;
    }
    mAutoClosed.eraseIf([at = caret()](const ALTextPos& closer) { return closer == at; });
    setSelection(*pair);
    insertText(std::string());
    return true;
}

bool ALCodeEditor::deletePairs()
{
    const std::vector<ALTextRange>          all = selectionsInOrder();
    std::vector<std::optional<ALTextRange>> pairs;
    pairs.reserve(all.size());
    bool any = false;
    for (const ALTextRange& one : all)
    {
        pairs.push_back(one.empty() ? pairAround(one.end) : std::nullopt);
        any = any || pairs.back().has_value();
        if (pairs.back())
        {
            mAutoClosed.eraseIf([at = one.end](const ALTextPos& closer) { return closer == at; });
        }
    }
    if (!any)
    {
        return false;
    }
    // One Backspace at each: the pair where there is one, else what a
    // Backspace takes there.
    undoJournal().beginTyping(selection(), true);
    editEach([&](size_t i, const ALTextRange& selection) -> std::optional<ALTextEditing::Change> {
        const std::optional<ALTextRange> range = pairs[i] ? pairs[i] : erasedBy(ALEditorCommand::DeleteLeft, selection);
        if (!range || range->empty())
        {
            return std::nullopt;
        }
        ALTextEditing::Change one;
        one.replacements.push_back({ range->normalised(), std::string() });
        one.caret = range->normalised().begin;
        return one;
    });
    undoJournal().endTyping();
    return true;
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
    // A changed line's bar, at the gutter's edge: a peek at its change.
    if (const S32 line = changeBarAt(x, y); line >= 0 && peekChange(line))
    {
        return true;
    }
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
        singleSelection();
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
    if (!mFolds.folded().empty() && text.pointInRect(x, y))
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
    if (mask == MASK_CONTROL && (mSymbolRequest || mLinkRequest) && text.pointInRect(x, y))
    {
        const ALTextPos at = posAtLocal(x, y, false);
        singleSelection();
        if (mLinkRequest && !mLinkRequest(at, false).empty())
        {
            setFocus(true);
            placeCaret(at, false);
            mLinkRequest(at, true);
            return true;
        }
        const ALTextRange word = mSymbolRequest ? identifierAt(at) : ALTextRange();
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
    if (mWheeled)
    {
        return true;
    }
    const LLRect text = textRect();
    // A changed line's bar says what a press on it does.
    if (changeBarAt(x, y) >= 0)
    {
        LLToolTipMgr::instance().show(alSaid("CodePeekChange", "Peek at this change"));
        return true;
    }
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
        for (const Decoration* each : decorationsOn(line))
        {
            const Decoration& d     = *each;
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
        // About the line, from its mark: the card stays while the mouse is
        // on the line's row in the gutter as well as beside it.
        mCardAnchor.mLeft = llmin(mCardAnchor.mLeft, leftEdge());
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
        LLToolTipMgr::instance().show(alSaid("CodeInlayWriteMac", "Command-double-click to insert '[TEXT]'", args));
#else
        LLToolTipMgr::instance().show(alSaid("CodeInlayWrite", "Ctrl-double-click to insert '[TEXT]'", args));
#endif
        return true;
    }
    if (!text.pointInRect(x, y) || onCompletionList(x, y))
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

bool ALCodeEditor::handleScrollWheel(S32 x, S32 y, LLScrollDelta delta)
{
    // A list of fixes is about a row the scroll takes away.
    closeFixes();
    // The card under the mouse scrolls while it has more to show that
    // way; one that has not -- all of it in sight, or its end reached --
    // goes, and the text scrolls on.
    if (cardShown() && mCard->getRect().pointInRect(x, y) && mCard->canScrollY(delta.mPrecise > 0.f ? 1 : -1))
    {
        return mCard->handleScrollWheel(x - mCard->getRect().mLeft, y - mCard->getRect().mBottom, delta);
    }
    if (onCompletionList(x, y))
    {
        // The popup is as large as the editor, so the list's rect is in the
        // editor's coordinates.
        ALChoiceList& list = completionPopup().list();
        const LLRect& rect = list.getRect();
        return list.handleScrollWheel(x - rect.mLeft, y - rect.mBottom, delta);
    }
    hideCard();
    mWheeled = true;
    return ALTextView::handleScrollWheel(x, y, delta);
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
        mWheeled    = false;
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
    const bool taken = ALTextView::handleHover(x, y, mask);
    // A changed line's bar is pressed for a peek at its change.
    if (changeBarAt(x, y) >= 0)
    {
        if (LLWindow* window = getWindow())
        {
            window->setCursor(UI_CURSOR_HAND);
        }
        return true;
    }
    return taken;
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

void ALCodeEditor::pump()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    // Edits made outside a command -- the whole text set -- fold again here.
    settleFolds();
    // A signature is about a call on the caret's line; anywhere else it
    // is stale. (The placeholders are let go of as the caret leaves their
    // lines, where it moves: dropPlaceholdersLeft.)
    if (mCards.signature() && !signatureShown())
    {
        hideSignature();
    }
    // A closer put in is typed over only on its own line: a caret's.
    mAutoClosed.eraseIf([this](const ALTextPos& at) {
        if (at.line == caret().line)
        {
            return false;
        }
        const std::vector<ALTextRange>& others = otherSelections();
        return std::none_of(others.begin(), others.end(), [&at](const ALTextRange& other) { return other.end.line == at.line; });
    });
    // The name under the caret lit once the caret has rested, and again
    // where the view has scrolled past the lines it was lit over.
    if (mLightsOccurrences &&
        ((mOccurrencesDue && mOccurrencesRest.getElapsedTimeF32() >= OCCURRENCES_REST) ||
         (!highlights(Highlight::Occurrences).empty() && (firstVisibleLine() < mOccurrencesFirst || lastVisibleLine() > mOccurrencesLast))))
    {
        lightOccurrences();
    }
    // The mouse rested long enough on the text: its card, once.
    if (mHoverCards && mHoverDelay >= 0.f && !mHoverTried && !mWheeled && mMouseX >= 0 && mMouseRest.getElapsedTimeF32() >= mHoverDelay && !cardShown() &&
        textRect().pointInRect(mMouseX, mMouseY) && !onCompletionList(mMouseX, mMouseY))
    {
        mHoverTried = true;
        hoverCardAt(mMouseX, mMouseY);
    }
    ALTextView::pump();
    // In its gap as the text is scrolled, before the children are drawn:
    // after the view has settled its scroll for a gap opened or shut.
    if (mPeek && mPeek->isOpen())
    {
        mPeek->place();
    }
}

void ALCodeEditor::draw()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    ALTextView::draw();
    if (mCards.signature())
    {
        drawSignature(textRect());
    }
    if (cardShown())
    {
        // Over the card rather than under it, so that its own ground
        // cannot paint the frame out along the edge it shares.
        gl_rect_2d(mCard->getRect(), paint(Paint::WidgetBorder) % getDrawContext().mAlpha, false);
    }
}

void ALCodeEditor::onFocusLost()
{
    closeCompletion();
    closeFixes();
    ALTextView::onFocusLost();
}
