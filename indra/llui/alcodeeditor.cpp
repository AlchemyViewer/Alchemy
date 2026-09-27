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

#include "alplace.h"
#include "alsaid.h"
#include "alsmartselect.h"
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
        // The name lit again once the caret rests; put out now if it has
        // left it.
        mOccurrencesDue = true;
        mOccurrencesRest.reset();
        if (!highlights(Highlight::Occurrences).empty() && (hasSelection() || !highlighted(Highlight::Occurrences, caret())))
        {
            clearHighlights(Highlight::Occurrences);
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

ALCodeEditor::~ALCodeEditor()
{
    // The layout outlives this part of the editor, and asks the provider
    // about this part's inlays.
    layout().setInlayProvider(nullptr);
    clearHandlers();
}

void ALCodeEditor::clearHandlers()
{
    mProvider          = nullptr;
    mCompletionRequest = nullptr;
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
    setDropHandler(nullptr);
}

// --- marks and decorations ---------------------------------------------------

void ALCodeEditor::onEdit(const ALTextDocument::Edit& edit)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    hideCard();
    // Numbers and tints were for the text they were given with.
    mLineNumbers.clear();
    mLineTints.clear();
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
    const ALTextRange removed = edit.range.normalised();
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
    mInlays.apply(
        edit,
        [&removed](InlayHint& h, const ALTextDocument::Edit& e) {
            if (removed.empty())
            {
                if (removed.begin < h.at || (removed.begin == h.at && !h.before))
                {
                    h.at = e.slidPast(h.at);
                }
                return true;
            }
            if (removed.end <= h.at)
            {
                h.at = e.slidPast(h.at);
                return true;
            }
            return h.at <= removed.begin;
        },
        [](InlayHint&) {});
    // The stops of a snippet or a call being filled in move with the text.
    mSnippet.slide(edit);

    // A closer typing put in moves with the text before it, and goes with
    // an edit that takes it.
    mAutoClosed.apply(
        edit,
        [&removed](ALTextPos& at, const ALTextDocument::Edit& e) {
            if (removed.end <= at)
            {
                at = e.slidPast(at);
                return true;
            }
            return at < removed.begin;
        },
        [](ALTextPos&) {});

    // Folds slide the same way (ALFoldModel::edited), and are hidden again
    // once the command is done.
    const bool folded = !mFolds.folded().empty();
    mFolds.edited(edit);
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
    mDecorations.assign(std::move(decorations));
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
            const S32 shown = !mLineNumbers.empty()                    ? (line < static_cast<S32>(mLineNumbers.size()) ? mLineNumbers[static_cast<size_t>(line)] : 0)
                              : mRelativeLineNumbers && line != caret_line ? std::abs(line - caret_line)
                                                                           : line + 1;
            if (shown > 0)
            {
                font->renderUTF8(std::to_string(shown), 0, static_cast<F32>(numbers_right), static_cast<F32>(screen_top - ascent),
                                 line == caret_line ? lit : ink, LLFontGL::RIGHT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
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

void ALCodeEditor::tintRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha, std::vector<LLColor4U>& colors)
{
    // What the analyzer knows a stretch to be, over the grammar's colour
    // for it; a comment or a string keeps its own, since a name inside
    // one is not that name.
    if (!mSemantics.empty())
    {
        auto first = mSemantics.onLine(line).first;
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
    // The depth at each bracket of the line that is code, then the row's
    // glyphs that are brackets in their depth's colour.
    S32                              depth = mBracketIndex.depthBefore(line);
    std::vector<std::pair<S32, S32>> at;
    for (const auto& [column, c] : mBracketIndex.bracketsOn(line))
    {
        if (c == '(' || c == '[' || c == '{')
        {
            at.emplace_back(column, depth);
            ++depth;
        }
        else
        {
            depth = llmax(0, depth - 1);
            at.emplace_back(column, depth);
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
    for (const FoldRegion& region : foldRegions())
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
    // A tint behind each line that has one, under everything else.
    if (!mLineTints.empty())
    {
        const S32 row_h = layout().rowHeight();
        forEachVisibleRow(text, [&](S32 line, S32, S32 screen_top) {
            if (line < static_cast<S32>(mLineTints.size()) && mLineTints[static_cast<size_t>(line)].mV[VALPHA] > 0.f)
            {
                gl_rect_2d(text.mLeft, screen_top, text.mRight, screen_top - row_h, mLineTints[static_cast<size_t>(line)] % alpha);
            }
        });
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
        const std::string& text    = document().line(l);
        size_t             lead    = 0;
        const S32          columns = alBlanksWidth(text, getTabWidth(), &lead);
        if (lead < text.size())
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
    const LLColor4 wash = highlightColor() % alpha;
    for (size_t index = 0; index < mHighlights.size(); ++index)
    {
        // The name's other places more lightly: they are only what is
        // written alike, lit without being asked for.
        const LLColor4 ink = index == static_cast<size_t>(Highlight::Occurrences) ? wash % 0.5f : wash;
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
            const ALTextLayout::Row& r    = laid.rows[static_cast<size_t>(row)];
            const LLFontGL*          font = getFont();
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
    mFolds.regions(document(), getTabWidth());
}

const std::vector<ALCodeEditor::FoldRegion>& ALCodeEditor::foldRegions()
{
    return mFolds.regions(document(), getTabWidth());
}

const ALCodeEditor::FoldRegion* ALCodeEditor::regionStartingAt(S32 line)
{
    return mFolds.startingAt(document(), getTabWidth(), line);
}

const ALCodeEditor::FoldRegion* ALCodeEditor::regionAround(S32 line)
{
    return mFolds.around(document(), getTabWidth(), line);
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
    layout().setHidden(0, document().lineCount() - 1, false);
    for (const auto& [first, last] : mFolds.hidden(document(), getTabWidth()))
    {
        layout().setHidden(first, last, true);
    }
}

bool ALCodeEditor::foldAt(S32 line)
{
    const std::optional<FoldRegion> chosen = mFolds.fold(document(), getTabWidth(), line);
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
    return true;
}

bool ALCodeEditor::unfoldAt(S32 line)
{
    if (!mFolds.unfold(document(), getTabWidth(), line))
    {
        return false;
    }
    applyFolds();
    return true;
}

void ALCodeEditor::foldAll()
{
    mFolds.foldAll(document(), getTabWidth());
    applyFolds();
    if (layout().hidden(caret().line))
    {
        setCaret(document().lineEnd(layout().visibleFrom(caret().line, -1)));
    }
}

void ALCodeEditor::unfoldAll()
{
    mFolds.unfoldAll();
    applyFolds();
}

void ALCodeEditor::revealLine(S32 line)
{
    if (mFolds.reveal(document(), getTabWidth(), line))
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
            return !mFolds.folded().empty();
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
    return mCompletionList && mCompletionList->getVisible();
}

void ALCodeEditor::hideCompletionList()
{
    if (mCompletionList)
    {
        mCompletionList->setVisible(false);
        mCompletionList->setChoices({});
    }
    mCompletionModel.hide();
    hideCompletionDoc();
}

void ALCodeEditor::closeCompletion()
{
    hideCompletionList();
    mCompletionModel.close();
    mCompletionAsked = false;
    mCompletionMoved = false;
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

void ALCodeEditor::refreshCompletion()
{
    const std::string prefix = wordBeforeCaret();
    const ALTextPos   at     = caret();
    const ALTextPos   start(at.line, at.column - static_cast<S32>(prefix.size()));
    // After `ll.` the members of `ll` are wanted -- after `obj:` in SLua,
    // its methods: the head is put before the prefix for whoever answers
    // by whole names, and taken off what they answer.
    std::string head;
    char        separator = '.';
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
    if ((prefix.empty() && head.empty() && !mCompletionAsked) || hasSelection())
    {
        closeCompletion();
        return;
    }
    const std::string asked = head.empty() ? prefix : head + separator + prefix;
    // What there is to choose from, asked for once while one identifier is
    // typed, and narrowed as it grows.
    if (!mCompletionModel.pooled(start, head, prefix))
    {
        std::vector<Completion> answered;
        if (mProvider)
        {
            mProvider(at, asked, answered);
        }
        else
        {
            vocabularyCompletions(asked, answered);
        }
        mCompletionModel.pool(start, at, prefix, head, separator, std::move(answered), document());
    }
    const bool fresh = mCompletionModel.narrow(start, at, prefix);
    if (fresh && mCompletionRequest)
    {
        mCompletionRequest(start, prefix);
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

// static
LLUIImagePtr ALCodeEditor::iconOf(const Completion& completion)
{
    if (completion.icon)
    {
        return completion.icon;
    }
    static boost::unordered_flat_map<std::string, LLUIImagePtr, ll::string_hash, std::equal_to<>> looked_up;
    const std::string_view                                                                       name = ALCompletionModel::iconNameOf(completion);
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
    const std::vector<Completion>& completions = mCompletionModel.list();
    for (size_t i = 0; i < completions.size() && !was.empty(); ++i)
    {
        if (completions[i].text == was)
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
    mCompletionDocFor.clear();
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
    box.setShape(ALPlace::beside(list, width, height, local, 2));
    box.setVisible(true);
}

void ALCodeEditor::showCompletionDoc()
{
    const S32 index = chosenCompletion();
    const std::vector<Completion>& completions = mCompletionModel.list();
    if (index < 0 || index >= static_cast<S32>(completions.size()) || !completions[index].documentation || completions[index].documentation->empty())
    {
        hideCompletionDoc();
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
    if (mCompletionDoc && mCompletionDoc->getVisible() && says == mCompletionDocFor && mCompletionList->getRect() == mCompletionDocBeside)
    {
        return;
    }
    ALTextView& box = *sideBox();
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
    mCompletionDocFor    = says;
    mCompletionDocBeside = mCompletionList->getRect();
}

void ALCodeEditor::supplyCompletions(const ALTextPos& at, std::vector<Completion> more)
{
    // Only about the identifier the list is still narrowing.
    if (hasSelection() || isReadOnly() || !mCompletionModel.supply(at, std::move(more)))
    {
        return;
    }
    refreshCompletion();
}

void ALCodeEditor::placeCompletion()
{
    const LLRect local = getLocalRect();
    placeListAt(*mCompletionList, mCompletionModel.range().begin, llmin(static_cast<S32>(mCompletionModel.list().size()), COMPLETION_ROWS),
                llmin(COMPLETION_WIDTH, llmax(60, local.getWidth() - 8)));
}

void ALCodeEditor::placeListAt(ALChoiceList& list, const ALTextPos& at, S32 rows, S32 width)
{
    // Under the row, or over it where under would run off the bottom;
    // across, within the view.
    list.setShape(ALPlace::under(anchorOf(at), width, list.heightFor(rows), getLocalRect()));
}

// --- quick fixes -----------------------------------------------------------------

bool ALCodeEditor::fixesOpen() const
{
    return mFixList && mFixList->getVisible();
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
    if (mFixList)
    {
        mFixList->setVisible(false);
        mFixList->setChoices({});
    }
    mFixListModel.close();
    // The preview is the side box's, which the completions share.
    if (was)
    {
        hideCompletionDoc();
    }
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

void ALCodeEditor::fillFixList(S32 chosen)
{
    // In the editor's colours, as the completions are; a suppression, and
    // the word that there is nothing, quieter than what makes a change.
    mFixList->setBackgroundColor(paint(Paint::Widget));
    mFixList->setTextColor(textColor());
    mFixList->setSelectionColor(paint(Paint::WidgetSelection));
    mFixList->setBorderColor(paint(Paint::WidgetBorder));
    const std::vector<Fix>&           fixes = mFixListModel.fixes();
    std::vector<ALChoiceList::Choice> choices;
    S32                               widest = 0;
    S32                               noted  = 0;
    for (const Fix& fix : fixes)
    {
        ALChoiceList::Choice choice;
        choice.text = fix.title;
        choice.note = fix.note;
        if (fix.suppress || ALFixListModel::isNothing(fix))
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
    mFixList->setChoices(std::move(choices), llclamp(chosen, 0, llmax(0, static_cast<S32>(fixes.size()) - 1)));
    // Under the caret where it is on the line, else under the line's text.
    const S32          line  = mFixListModel.line();
    const ALTextPos    caret = this->caret();
    const std::string& text  = document().line(llclamp(line, 0, document().lineCount() - 1));
    const size_t       lead  = text.find_first_not_of(" \t");
    const ALTextPos    at    = caret.line == line ? caret : ALTextPos(line, lead == std::string::npos ? 0 : static_cast<S32>(lead));
    placeListAt(*mFixList, at, llmin(static_cast<S32>(fixes.size()), COMPLETION_ROWS), width);
    mFixList->setVisible(true);
    showFixPreview();
}

void ALCodeEditor::noteFixes(U32 shown, const std::vector<std::string>& notes)
{
    if (!fixesOpen() || !mFixListModel.note(shown, notes))
    {
        return;
    }
    fillFixList(mFixList->chosen());
}

void ALCodeEditor::supplyActions(const ALTextRange& at, std::vector<Fix> actions)
{
    if (isReadOnly())
    {
        return;
    }
    const bool                            open   = fixesOpen();
    std::optional<ALFixListModel::Joined> joined = mFixListModel.join(at, caret(), std::move(actions), open, open ? mFixList->chosen() : -1);
    if (joined)
    {
        showFixes(caret().line, std::move(joined->fixes), joined->chosen);
    }
}

void ALCodeEditor::showFixPreview()
{
    const S32 index = fixesOpen() ? mFixList->chosen() : -1;
    const std::vector<Fix>& fixes = mFixListModel.fixes();
    if (index < 0 || index >= static_cast<S32>(fixes.size()) || ALFixListModel::isNothing(fixes[index]))
    {
        hideCompletionDoc();
        return;
    }
    std::vector<char> kinds;
    const std::string says = ALFixListModel::previewOf(document(), fixes[index], kinds);
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
    const S32 index = mCompletionList->chosen();
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

bool ALCodeEditor::acceptCompletion()
{
    if (!completionOpen())
    {
        return false;
    }
    const S32 index = mCompletionList->chosen();
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
        if (mCards.askedAbout(word, version))
        {
            if (!mCards.answer().empty())
            {
                says  = mCards.answer();
                links = mCards.links();
                about = about.empty() ? word : ALTextRange(std::min(about.begin, word.begin), std::max(about.end, word.end));
            }
        }
        else if (mHoverRequest)
        {
            mCards.asking(word, version);
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
    showCard(about.empty() ? word : ALTextRange(std::min(about.begin, word.begin), std::max(about.end, word.end)), text, problems, mCards.links());
}

// --- signature help -------------------------------------------------------------

void ALCodeEditor::showSignature(const ALTextPos& at, Signature signature)
{
    if (!typingText())
    {
        // An answer that came after a modal keymap stopped inserting.
        return;
    }
    mCards.showSignature(at, std::move(signature));
}

void ALCodeEditor::hideSignature()
{
    mCards.hideSignature();
}

bool ALCodeEditor::signatureShown() const
{
    return mCards.signatureFor(caret());
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
    const bool       docs  = !sig.documentation.empty();
    const std::string doc_line = docs ? sig.documentation.substr(0, sig.documentation.find('\n')) : std::string();
    const S32        wanted = llmax(font->getWidth(sig.label), docs ? font->getWidth(doc_line) : 0) + 2 * SIGNATURE_PAD;
    const S32        height = line_h * (docs ? 2 : 1) + 2 * SIGNATURE_PAD;

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
    const F32 through = end > 0 ? static_cast<F32>(font->getWidth(sig.label.substr(0, static_cast<size_t>(end)))) : label_w;
    const F32 shift   = ALCodeCards::labelShift(label_w, through, static_cast<F32>(room));
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
        mCompletionList->moveChoice(key == 'N' ? 1 : -1, true);
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
                mCompletionList->moveChoice(-1, true);
                mCompletionMoved = true;
                return true;
            case KEY_DOWN:
                mCompletionList->moveChoice(1, true);
                mCompletionMoved = true;
                return true;
            case KEY_PAGE_UP:
                mCompletionList->moveChoice(-COMPLETION_ROWS, false);
                mCompletionMoved = true;
                return true;
            case KEY_PAGE_DOWN:
                mCompletionList->moveChoice(COMPLETION_ROWS, false);
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
    if (key == KEY_BACKSPACE && mask == MASK_NONE && mAutoClose && deletePair())
    {
        if (mCards.signature() && mSignatureRequest)
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
    if (taken && mCards.signature() && mSignatureRequest && (key == KEY_BACKSPACE || key == KEY_DELETE))
    {
        mSignatureRequest(caret());
    }
    return taken;
}

bool ALCodeEditor::handleUnicodeCharHere(llwchar uni_char)
{
    const bool typing   = typingText();
    const bool was_open = completionOpen();
    // The pair, the character and its outdent one key typed, one with the
    // typing around it.
    undoJournal().beginTyping(selection());
    const bool paired = typing && mAutoClose && uni_char < 0x80 && !isReadOnly() && typePair(static_cast<char>(uni_char));
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
    const bool identifier = uni_char < 0x80 && alIdentifierByte(static_cast<char>(uni_char));
    // In a comment or a string what is typed is prose: the list does not
    // open on its own there, where a Return meant as a new line would
    // otherwise put a call into the comment. Asked for, it still opens.
    const bool prose = mAutoComplete && !was_open && inProse(caret());
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
    // A call begins, moves on to its next argument, or ends.
    if (mSignatureRequest && (uni_char == '(' || uni_char == ',' || uni_char == ')' || mCards.signature()))
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
                    // Where the typing goes on from, for the key after.
                    mAutoClosed.erase(put);
                    setCaret(ALTextPos(at.line, at.column + 1));
                    undoJournal().settle(selection());
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
            undoJournal().settle(selection());
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
        undoJournal().settle(selection());
        mAutoClosed.insert(inside);
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
    if (completionOpen() && mCompletionList->getRect().pointInRect(x, y))
    {
        const LLRect& rect = mCompletionList->getRect();
        return mCompletionList->handleScrollWheel(x - rect.mLeft, y - rect.mBottom, delta);
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

void ALCodeEditor::draw()
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
    // A closer put in is typed over only on its own line.
    mAutoClosed.eraseIf([this](const ALTextPos& at) { return at.line != caret().line; });
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
        textRect().pointInRect(mMouseX, mMouseY) && !(mCompletionList && mCompletionList->getVisible() && mCompletionList->getRect().pointInRect(mMouseX, mMouseY)))
    {
        mHoverTried = true;
        hoverCardAt(mMouseX, mMouseY);
    }
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
