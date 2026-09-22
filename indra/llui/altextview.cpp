/**
 * @file altextview.cpp
 * @brief A text view: a document laid out and drawn, with a caret in it.
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
#include "alsurface.h"
#include "alviewtype.h"
#include "llclipboard.h"
#include "lldir.h"
#include "llfocusmgr.h"
#include "llkeyboard.h"
#include "lllocalcliprect.h"
#include "llmenugl.h"
#include "llrender2dutils.h"
#include "alfindbar.h"
#include "llspellcheck.h"
#include "lltooltip.h"
#include "llurlaction.h"
#include "llurlmatch.h"
#include "llurlregistry.h"
#include "llstring.h"
#include "lltimer.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llwindow.h"

static LLDefaultChildRegistry::Register<ALTextView> r("text_view");

namespace
{
    const F32 BLINK_DELAY           = 1.f;
    const S32 CARET_WIDTH           = 2;
    const F32 TRIPLE_CLICK_INTERVAL = 0.3f;
    // How far apart two clicks may land and still be one run of clicks.
    const S32 CLICK_SLOP = 4;
    const S32 WHEEL_ROWS            = 3;
    // Room past the widest line before a horizontal scrollbar is needed,
    // and what the caret keeps between itself and an edge.
    // The bars: the ruler's width down the right, the bottom thumb's
    // height, the least a thumb may be, how long the bars stay in sight
    // once the mouse has left and the text has settled, and how long
    // they take to go.
    const S32 RULER_W   = 12;
    const S32 HBAR_H    = 8;
    const S32 THUMB_MIN = 24;
    const F32 BAR_HOLD  = 1.0f;
    const F32 BAR_FADE  = 0.5f;

    // The map: a line's height and a character's width in it, and the
    // find bar's width.
    const S32 MAP_LINE_H     = 2;
    const S32 MAP_CHAR_W     = 1;
    const S32 MAP_PAD        = 2;
    const S32 MAP_MARK_W     = 3;
    const S32 FIND_BAR_WIDTH = 460;

    const S32 H_MARGIN              = 16;
    const F32 CARET_MARGIN          = 8.f;

    // What each kind's colour is called in the colour table, after the
    // prefix; Text is the view's own text colour.
    const char* const KIND_COLOR_SUFFIXES[] = {
        "",           "Comment",     "DocComment",   "String",    "Escape",         "Number",    "Keyword",
        "Control",    "Type",        "Constant",     "Function",  "Event",          "Label",     "Operator",
        "Punctuation", "Preprocessor", "Tag",        "Attribute", "AttributeValue", "Entity",    "Variable",
        "Parameter",  "Property",    "Deprecated",   "Invalid",
    };
    static_assert(sizeof(KIND_COLOR_SUFFIXES) / sizeof(KIND_COLOR_SUFFIXES[0]) == static_cast<size_t>(ALSyntaxKind::COUNT), "every kind has a colour");

    bool editsText(ALEditorCommand command)
    {
        switch (command)
        {
            case ALEditorCommand::DeleteLeft:
            case ALEditorCommand::DeleteRight:
            case ALEditorCommand::DeleteWordLeft:
            case ALEditorCommand::DeleteWordRight:
            case ALEditorCommand::DeleteToLineStart:
            case ALEditorCommand::DeleteToLineEnd:
            case ALEditorCommand::NewLine:
            case ALEditorCommand::Indent:
            case ALEditorCommand::Unindent:
            case ALEditorCommand::Undo:
            case ALEditorCommand::Redo:
            case ALEditorCommand::Cut:
            case ALEditorCommand::Paste:
            case ALEditorCommand::Delete:
            case ALEditorCommand::ToggleComment:
            case ALEditorCommand::DuplicateLine:
            case ALEditorCommand::MoveLineUp:
            case ALEditorCommand::MoveLineDown:
            case ALEditorCommand::DeleteLine:
            case ALEditorCommand::Complete:
            case ALEditorCommand::Rename:
                return true;
            default:
                return false;
        }
    }

    // The layout knows a substitution and an atom by one id each; atoms
    // are numbered from here so that the two are told apart.
    const S32 ATOM_ID_BASE = 1 << 30;
    S32       atomId(size_t index) { return ATOM_ID_BASE + static_cast<S32>(index); }
    bool      isAtomId(S32 id) { return id >= ATOM_ID_BASE; }

    // How long the word at the caret is left unmarked after it was typed.
    const F32 SPELL_SETTLE_SECONDS = 1.5f;
    const F32 SQUIGGLE_AMPLITUDE   = 1.f;
    const F32 SQUIGGLE_WAVE        = 5.f;

    // A word the spell check has no business with: code rather than
    // prose -- a digit in it, an underscore, a capital after the first
    // letter -- or too short to be wrong.
    bool proseWord(std::string_view word)
    {
        if (word.size() < 3)
        {
            return false;
        }
        bool lower_seen = false;
        for (size_t i = 0; i < word.size(); ++i)
        {
            const char c = word[i];
            if ((c >= '0' && c <= '9') || c == '_')
            {
                return false;
            }
            if (c >= 'A' && c <= 'Z' && lower_seen)
            {
                return false;
            }
            if (c >= 'a' && c <= 'z')
            {
                lower_seen = true;
            }
        }
        return true;
    }
}

ALTextView::Params::Params()
:   text_color("text_color"),
    text_readonly_color("text_readonly_color"),
    bg_color("bg_color"),
    bg_readonly_color("bg_readonly_color"),
    bg_focus_color("bg_focus_color"),
    cursor_color("cursor_color"),
    selection_color("selection_color"),
    bg_visible("bg_visible", true),
    read_only("read_only", false),
    word_wrap("word_wrap", false),
    soft_tabs("soft_tabs", false),
    tab_width("tab_width", 4),
    h_pad("h_pad", 4),
    v_pad("v_pad", 2),
    syntax("syntax"),
    syntax_color_prefix("syntax_color_prefix", "Syntax"),
    default_text("default_text"),
    context_menu("context_menu"),
    find_match_color("find_match_color"),
    scroll_map("scroll_map", false),
    scroll_map_width("scroll_map_width", 80),
    scroll_map_preview("scroll_map_preview", true),
    scroll_map_left("scroll_map_left", false),
    link_color("link_color"),
    spellcheck("spellcheck", false),
    spell_error_color("spell_error_color"),
    takes_focus("takes_focus", true),
    placeholder("placeholder")
{
}

ALTextView::ALTextView(const Params& p)
:   LLUICtrl(p),
    mUndo(mDocument),
    mKeymap(ALKeymap::standard()),
    mFont(p.font),
    mTextColor(p.text_color),
    mTextReadOnlyColor(p.text_readonly_color),
    mBgColor(p.bg_color),
    mBgReadOnlyColor(p.bg_readonly_color),
    mBgFocusColor(p.bg_focus_color.isProvided() ? p.bg_focus_color() : p.bg_color()),
    mCursorColor(p.cursor_color),
    mSelectionColor(p.selection_color),
    mBgVisible(p.bg_visible),
    mReadOnly(p.read_only),
    mWordWrap(p.word_wrap),
    mSoftTabs(p.soft_tabs),
    mHPad(p.h_pad),
    mVPad(p.v_pad),
    mContextMenuFile(p.context_menu.isProvided() ? p.context_menu() : std::string())
{
    const S32 tab_width = p.tab_width;
    mTabWidth           = llmax(1, tab_width);
    mFindMatchColor     = p.find_match_color.isProvided() ? p.find_match_color() : LLUIColorTable::instance().getColor("TextFindMatchColor", LLColor4(1.f, 0.7f, 0.2f, 0.4f));
    mScrollMap          = p.scroll_map;
    mScrollMapWidth     = llmax(20, static_cast<S32>(p.scroll_map_width));
    mScrollMapPreview   = p.scroll_map_preview;
    mScrollMapLeft      = p.scroll_map_left;
    {
        const LLUIColorTable& colors = LLUIColorTable::instance();
        const std::string     prefix = p.syntax_color_prefix();
        mKindColors[0]               = mTextColor;
        for (size_t kind = 1; kind < mKindColors.size(); ++kind)
        {
            std::string name = kindColorName(prefix, static_cast<ALSyntaxKind>(kind));
            if (!colors.colorExists(name))
            {
                name = kindColorName("Syntax", static_cast<ALSyntaxKind>(kind));
            }
            mKindColors[kind] = colors.getColor(name, mTextColor.get());
        }
    }

    mLinkColor       = p.link_color.isProvided() ? p.link_color() : LLUIColorTable::instance().getColor("HTMLLinkColor", LLColor4(0.4f, 0.6f, 1.f, 1.f));
    mSpellErrorColor = p.spell_error_color.isProvided() ? p.spell_error_color() : LLUIColorTable::instance().getColor("TextSpellErrorColor", LLColor4(1.f, 0.f, 0.f, 0.8f));
    mPlaceholder     = p.placeholder.isProvided() ? p.placeholder() : std::string();
    mSpellCheck      = p.spellcheck;
    mTakesFocus      = p.takes_focus;

    mHighlighter.attach(&mDocument);
    mLayout.attach(&mDocument);
    mLayout.setFont(mFont);
    mLayout.setTabWidth(mTabWidth);
    mLayout.setSubstitutionProvider([this](S32 line, std::vector<ALTextLayout::Substitution>& out) { provideSubstitutions(line, out); });
    mLayout.setRunProvider([this](S32 line, std::vector<ALTextLayout::Run>& out) { provideRuns(line, out); });
    mDocumentConnection = mDocument.onChanged([this](const ALTextDocument::Edit& edit) { onDocumentEdit(edit); });
    if (mSpellCheck)
    {
        mSpellSettingsConnection = LLSpellChecker::setSettingsChangeCallback([this]() { recheckSpelling(); });
    }

    if (p.syntax.isProvided())
    {
        const std::string& grammar_name = p.syntax;
        setSyntax(grammar_name);
    }
    if (p.default_text.isProvided())
    {
        const std::string& initial = p.default_text;
        setText(initial);
    }
    if (mWordWrap)
    {
        mLayout.setWrapWidth(textRect().getWidth());
    }
}

ALTextView::~ALTextView()
{
    if (LLContextMenu* menu = mContextMenuHandle.get())
    {
        menu->die();
        mContextMenuHandle.markDead();
    }
    if (LLContextMenu* menu = mUrlMenuHandle.get())
    {
        menu->die();
        mUrlMenuHandle.markDead();
    }
    for (Atom& atom : mAtoms)
    {
        if (atom.view)
        {
            removeChild(atom.view);
            atom.view->die();
            atom.view = nullptr;
        }
    }
    if (LLWindow* window = getWindow())
    {
        window->allowLanguageTextInput(this, false);
    }
    if (gEditMenuHandler == this)
    {
        gEditMenuHandler = nullptr;
    }
}

// --- the text ----------------------------------------------------------------

void ALTextView::setText(std::string_view text)
{
    mPreeditLength = 0;
    mPreeditSegmentEnds.clear();
    mPreeditStandouts.clear();
    mPreeditOverwritten.clear();
    mDocument.setText(text);
    mUndo.clear();
    // The layers were about the text that was; the edit that replaced
    // it has dropped what it cut through, which is everything, and a
    // substitution over an empty text is nothing.
    setSubstitutions({});
    setAtoms({});
    setStyles({});
    // Through the virtual, so that what a subclass keeps about changes
    // since the last save -- the gutter's bars -- starts clean too.
    resetDirty();
    mCaret = mAnchor = mDocument.start();
    mDesiredX         = -1.f;
    mScrollY          = 0;
    mScrollX          = 0.f;
    mChangedSinceFocus = false;
    syncScrollbar();
    mChanged();
}

void ALTextView::setValue(const LLSD& value)
{
    setText(value.asString());
}

LLSD ALTextView::getValue() const
{
    return LLSD(text());
}

ALSyntaxLibrary& ALTextView::syntaxLibrary()
{
    static ALSyntaxLibrary library;
    static bool            loaded = false;
    if (!loaded)
    {
        loaded = true;
        if (gDirUtilp)
        {
            library.loadDirectory(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "syntax"));
        }
    }
    return library;
}

void ALTextView::setGrammar(std::shared_ptr<const ALSyntaxGrammar> grammar)
{
    mHighlighter.setGrammar(std::move(grammar));
    recheckSpelling();
}

void ALTextView::setSyntax(std::string_view grammar_name)
{
    setGrammar(syntaxLibrary().find(grammar_name));
}

void ALTextView::setFont(const LLFontGL* font)
{
    mFont = font ? font : LLFontGL::getFontMonospace();
    mLayout.setFont(mFont);
    syncScrollbar();
}

void ALTextView::setReadOnly(bool read_only)
{
    mReadOnly = read_only;
    if (hasFocus())
    {
        allowLanguageInput(!read_only);
    }
}

void ALTextView::setWordWrap(bool wrap)
{
    mWordWrap = wrap;
    mScrollX  = 0.f;
    mLayout.setWrapWidth(wrap ? textRect().getWidth() : 0);
    syncScrollbar();
}

void ALTextView::setTabWidth(S32 spaces)
{
    mTabWidth = llmax(1, spaces);
    mLayout.setTabWidth(mTabWidth);
}

// --- geometry ----------------------------------------------------------------

S32 ALTextView::bandHeight() const
{
    return mModal ? mLayout.rowHeight() + 4 : 0;
}

LLRect ALTextView::bodyRect() const
{
    LLRect rect = getLocalRect();
    rect.mBottom += bandHeight();
    return rect;
}

LLRect ALTextView::textRect() const
{
    LLRect rect = bodyRect();
    rect.mLeft  = leftEdge() + mHPad + leftInset();
    rect.mRight -= mHPad;
    rect.mTop -= mVPad;
    rect.mBottom += mVPad;
    if (mScrollMap && !mScrollMapLeft)
    {
        rect.mRight -= mScrollMapWidth;
    }
    else if (mNeedV)
    {
        rect.mRight -= RULER_W;
    }
    return rect;
}

S32 ALTextView::rowsPerPage() const
{
    const S32 row = mLayout.rowHeight();
    return row > 0 ? llmax(1, textRect().getHeight() / row) : 1;
}

void ALTextView::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    if (mWordWrap)
    {
        mLayout.setWrapWidth(textRect().getWidth());
    }
    syncScrollbar();
    placeFindBar();
}

ALTextPos ALTextView::posAtLocal(S32 x, S32 y, bool round)
{
    const LLRect text  = textRect();
    const S32    row_h = mLayout.rowHeight();
    if (row_h <= 0 || mDocument.lineCount() == 0)
    {
        return mDocument.start();
    }
    const S32 doc_y = (text.mTop - y) + mScrollY;
    const S32 line  = mLayout.lineAtY(llmax(0, doc_y));
    const S32 top   = mLayout.lineTop(line);
    const S32 row   = mLayout.rowAtY(line, doc_y - top);
    const F32 x_rel = static_cast<F32>(x - text.mLeft) + mScrollX;
    return mDocument.clamp(ALTextPos(line, mLayout.columnAt(line, row, x_rel, round)));
}

// --- scrolling -----------------------------------------------------------------

bool ALTextView::hasHorizontalScrollbar() const
{
    return mNeedH;
}

void ALTextView::syncScrollbar()
{
    // The ruler takes room the wrap width depends on, so the need for it
    // is decided again once the first decision has been applied.
    for (S32 pass = 0; pass < 2; ++pass)
    {
        const LLRect text   = textRect();
        const bool   need_v = !mScrollMap && mLayout.totalHeight() > llmax(1, text.getHeight());
        const bool   need_h = !mWordWrap && mLayout.contentWidth() + static_cast<F32>(H_MARGIN) > static_cast<F32>(text.getWidth());
        const bool   changed = need_v != mNeedV || need_h != mNeedH;
        mNeedV               = need_v;
        mNeedH               = need_h;
        if (mWordWrap)
        {
            mLayout.setWrapWidth(textRect().getWidth());
        }
        if (!changed)
        {
            break;
        }
    }
    const LLRect text = textRect();
    const S32    page = llmax(1, text.getHeight());
    mScrollY          = llclamp(mScrollY, 0, llmax(0, mLayout.totalHeight() - page));
    const S32 width   = llmax(1, text.getWidth());
    const S32 content = mWordWrap ? 0 : static_cast<S32>(ceilf(mLayout.contentWidth())) + H_MARGIN;
    mScrollX          = llclamp(mScrollX, 0.f, static_cast<F32>(llmax(0, content - width)));
}

void ALTextView::setScrollY(S32 y)
{
    if (y != mScrollY)
    {
        mBarShown.reset();
    }
    mScrollY = llmax(0, y);
    syncScrollbar();
}

void ALTextView::setScrollX(F32 x)
{
    if (x != mScrollX)
    {
        mBarShown.reset();
    }
    mScrollX = llmax(0.f, x);
    syncScrollbar();
}

void ALTextView::scrollToCaret()
{
    const S32 row_h = mLayout.rowHeight();
    if (row_h <= 0)
    {
        return;
    }
    S32       row;
    const F32 x      = mLayout.xOf(mCaret.line, mCaret.column, &row);
    const S32 top    = mLayout.lineTop(mCaret.line) + mLayout.rowTop(mCaret.line, row);
    const S32 height = mLayout.rowHeightOf(mCaret.line, row);
    const LLRect text = textRect();
    const S32 page = llmax(height, text.getHeight());
    if (top < mScrollY)
    {
        mScrollY = top;
    }
    else if (top + height > mScrollY + page)
    {
        mScrollY = top + height - page;
    }
    mScrollY = llmax(0, mScrollY);
    if (mWordWrap)
    {
        mScrollX = 0.f;
    }
    else
    {
        const F32 width = static_cast<F32>(llmax(1, text.getWidth()));
        if (x < mScrollX + CARET_MARGIN)
        {
            mScrollX = llmax(0.f, x - CARET_MARGIN);
        }
        else if (x + CARET_MARGIN > mScrollX + width)
        {
            mScrollX = x + CARET_MARGIN - width;
        }
    }
    syncScrollbar();
}

void ALTextView::scrollToLine(S32 line)
{
    mScrollY = mLayout.lineTop(line);
    syncScrollbar();
}

S32 ALTextView::firstVisibleLine()
{
    return mLayout.lineAtY(mScrollY);
}

S32 ALTextView::lastVisibleLine()
{
    const S32 row = mLayout.rowHeight();
    return mLayout.lineAtY(mScrollY + llmax(0, textRect().getHeight() - (row > 0 ? row : 1)));
}

void ALTextView::setModalKeymap(std::unique_ptr<ALModalKeymap> keymap)
{
    mModal = std::move(keymap);
}

// --- the caret ---------------------------------------------------------------

void ALTextView::placeCaret(const ALTextPos& pos, bool extend)
{
    const ALTextPos was = mCaret;
    mCaret              = snapped(mDocument.clamp(pos), was);
    if (!extend)
    {
        mAnchor = mCaret;
    }
    if (mLayout.hidden(mCaret.line))
    {
        revealLine(mCaret.line);
    }
    mBlink.reset();
    if (mCaret != was)
    {
        mCaretMoved();
    }
}

void ALTextView::setCaret(ALTextPos pos, bool extend)
{
    placeCaret(pos, extend);
    mDesiredX = -1.f;
    scrollToCaret();
}

void ALTextView::setSelection(const ALTextRange& range)
{
    mAnchor = mDocument.clamp(range.begin);
    placeCaret(range.end, true);
    mDesiredX = -1.f;
    scrollToCaret();
}

std::string ALTextView::wordBeforeCaret() const
{
    const std::string& line  = mDocument.line(mCaret.line);
    S32                begin = llmin(mCaret.column, static_cast<S32>(line.size()));
    while (begin > 0 && alIdentifierByte(line[begin - 1]))
    {
        --begin;
    }
    return line.substr(begin, mCaret.column - begin);
}

// --- what a stretch shows -------------------------------------------------------

void ALTextView::setSubstitutions(std::vector<Substitution> substitutions)
{
    for (const Substitution& sub : mSubstitutions)
    {
        mLayout.invalidateLine(sub.range.begin.line);
    }
    for (Substitution& sub : substitutions)
    {
        sub.range = sub.range.normalised();
    }
    std::stable_sort(substitutions.begin(), substitutions.end(), [](const Substitution& a, const Substitution& b) { return a.range.begin < b.range.begin; });
    // Within a line, in order, none over another: a stretch is one line's.
    mSubstitutions.clear();
    for (Substitution& sub : substitutions)
    {
        if (sub.range.begin.line != sub.range.end.line || sub.range.empty())
        {
            continue;
        }
        if (!mSubstitutions.empty() && sub.range.begin < mSubstitutions.back().range.end)
        {
            continue;
        }
        mLayout.invalidateLine(sub.range.begin.line);
        mSubstitutions.push_back(std::move(sub));
    }
    mHoverLink   = -1;
    mPressedLink = -1;
}

void ALTextView::addSubstitution(Substitution substitution)
{
    substitution.range = substitution.range.normalised();
    if (substitution.range.begin.line != substitution.range.end.line || substitution.range.empty())
    {
        return;
    }
    const auto at = std::lower_bound(mSubstitutions.begin(), mSubstitutions.end(), substitution.range.begin,
                                     [](const Substitution& s, const ALTextPos& p) { return s.range.begin < p; });
    if ((at != mSubstitutions.end() && at->range.begin < substitution.range.end) || (at != mSubstitutions.begin() && substitution.range.begin < (at - 1)->range.end))
    {
        return;
    }
    mLayout.invalidateLine(substitution.range.begin.line);
    mSubstitutions.insert(at, std::move(substitution));
    mHoverLink   = -1;
    mPressedLink = -1;
}

const ALTextView::Substitution* ALTextView::substitutionAt(const ALTextPos& pos) const
{
    auto after = std::upper_bound(mSubstitutions.begin(), mSubstitutions.end(), pos, [](const ALTextPos& p, const Substitution& s) { return p < s.range.begin; });
    if (after == mSubstitutions.begin())
    {
        return nullptr;
    }
    const Substitution& sub = *(after - 1);
    return pos < sub.range.end ? &sub : nullptr;
}

bool ALTextView::relabel(const ALTextRange& range, const std::string& shown)
{
    const ALTextRange wanted = range.normalised();
    const auto        at     = std::lower_bound(mSubstitutions.begin(), mSubstitutions.end(), wanted.begin,
                                                [](const Substitution& s, const ALTextPos& p) { return s.range.begin < p; });
    if (at == mSubstitutions.end() || at->range.begin != wanted.begin)
    {
        return false;
    }
    if (at->shown != shown)
    {
        at->shown = shown;
        mLayout.invalidateLine(at->range.begin.line);
    }
    return true;
}

S32 ALTextView::linkUrlsOn(S32 line, S32 from)
{
    if (line < 0 || line >= mDocument.lineCount())
    {
        return 0;
    }
    const std::string& text = mDocument.line(line);
    if (from < 0 || from >= static_cast<S32>(text.size()))
    {
        return 0;
    }
    const LLHandle<ALTextView> self = getDerivedHandle<ALTextView>();
    // A name that arrives later goes to every link of the URL it is for.
    const auto relabelled = [self](const std::string& url, const std::string& label, const std::string&) {
        ALTextView* view = self.get();
        if (!view)
        {
            return;
        }
        for (const Substitution& sub : view->mSubstitutions)
        {
            if (sub.link && sub.url == url)
            {
                view->relabel(sub.range, label);
            }
        }
    };
    S32         made = 0;
    std::string rest = text.substr(static_cast<size_t>(from));
    S32         at   = from;
    LLUrlMatch  match;
    while (!rest.empty() && LLUrlRegistry::instance().findUrl(rest, match, relabelled))
    {
        const S32 begin = at + static_cast<S32>(match.getStart());
        const S32 end   = at + static_cast<S32>(match.getEnd()) + 1;
        if (end <= begin)
        {
            break;
        }
        Substitution link;
        link.range     = ALTextRange(ALTextPos(line, begin), ALTextPos(line, end));
        link.link      = true;
        link.tooltip   = match.getTooltip();
        link.url       = match.getUrl();
        link.underline = match.getUnderline() == LLStyle::UNDERLINE_ALWAYS ? Substitution::Underline::Always
                         : match.getUnderline() == LLStyle::UNDERLINE_NEVER ? Substitution::Underline::Never
                                                                             : Substitution::Underline::Hover;
        const std::string matched = text.substr(static_cast<size_t>(begin), static_cast<size_t>(end - begin));
        if (!match.getLabel().empty() && match.getLabel() != matched)
        {
            link.shown = match.getLabel();
        }
        addSubstitution(std::move(link));
        ++made;
        rest = rest.substr(match.getEnd() + 1);
        at   = end;
    }
    return made;
}

// --- styles ----------------------------------------------------------------------

void ALTextView::setStyles(std::vector<Style> styles)
{
    auto touched = [this](const Style& style) {
        const ALTextRange r = style.range.normalised();
        for (S32 line = r.begin.line; line <= r.end.line; ++line)
        {
            mLayout.invalidateLine(line);
        }
    };
    for (const Style& style : mStyles)
    {
        touched(style);
    }
    for (Style& style : styles)
    {
        style.range = style.range.normalised();
    }
    std::stable_sort(styles.begin(), styles.end(), [](const Style& a, const Style& b) { return a.range.begin < b.range.begin; });
    mStyles.clear();
    for (Style& style : styles)
    {
        // Bold and italic are the registry's face for them, of the font
        // the style names or the view's own; where the registry has no
        // such face the text stays in the font it was.
        if (const U8 face = style.flags & (LLFontGL::BOLD | LLFontGL::ITALIC); face && (style.font || mFont))
        {
            style.font = (style.font ? style.font : mFont)->faceFor(face);
        }
        if (style.range.empty() || (!style.font && !style.color && !(style.flags & LLFontGL::UNDERLINE)))
        {
            continue;
        }
        if (!mStyles.empty() && style.range.begin < mStyles.back().range.end)
        {
            continue;
        }
        touched(style);
        mStyles.push_back(std::move(style));
    }
}

void ALTextView::addStyle(Style style)
{
    style.range = style.range.normalised();
    if (const U8 face = style.flags & (LLFontGL::BOLD | LLFontGL::ITALIC); face && (style.font || mFont))
    {
        style.font = (style.font ? style.font : mFont)->faceFor(face);
    }
    if (style.range.empty() || (!style.font && !style.color && !(style.flags & LLFontGL::UNDERLINE)))
    {
        return;
    }
    const auto at = std::lower_bound(mStyles.begin(), mStyles.end(), style.range.begin, [](const Style& s, const ALTextPos& p) { return s.range.begin < p; });
    if ((at != mStyles.end() && at->range.begin < style.range.end) || (at != mStyles.begin() && style.range.begin < (at - 1)->range.end))
    {
        return;
    }
    for (S32 line = style.range.begin.line; line <= style.range.end.line; ++line)
    {
        mLayout.invalidateLine(line);
    }
    mStyles.insert(at, std::move(style));
}

std::vector<ALTextView::Style>::const_iterator ALTextView::firstStyleOn(S32 line) const
{
    // The styles are in order and none over another, so their ends are
    // in order too: the first that reaches the line is found by them.
    return std::lower_bound(mStyles.begin(), mStyles.end(), ALTextPos(line, 0), [](const Style& s, const ALTextPos& p) { return s.range.end <= p; });
}

void ALTextView::provideRuns(S32 line, std::vector<ALTextLayout::Run>& out) const
{
    const S32 length = mDocument.lineLength(line);
    for (auto it = firstStyleOn(line); it != mStyles.end() && it->range.begin.line <= line; ++it)
    {
        const Style& style = *it;
        if (!style.font)
        {
            continue;
        }
        ALTextLayout::Run run;
        run.begin = style.range.begin.line == line ? style.range.begin.column : 0;
        run.end   = style.range.end.line == line ? llmin(style.range.end.column, length) : length;
        run.font  = style.font;
        if (run.end > run.begin)
        {
            out.push_back(run);
        }
    }
}

// --- atoms ---------------------------------------------------------------------

// static
const std::string& ALTextView::atomPlaceholder()
{
    static const std::string placeholder("\xEF\xBF\xBC");
    return placeholder;
}

void ALTextView::setAtoms(std::vector<Atom> atoms)
{
    for (Atom& atom : mAtoms)
    {
        mLayout.invalidateLine(atom.at.line);
        if (atom.view)
        {
            // A view given again is kept; the rest go with their atoms.
            const bool kept = std::any_of(atoms.begin(), atoms.end(), [&](const Atom& a) { return a.view == atom.view; });
            if (!kept)
            {
                letGoOfAtomView(atom.view);
                removeChild(atom.view);
                atom.view->die();
            }
            atom.view = nullptr;
        }
    }
    std::stable_sort(atoms.begin(), atoms.end(), [](const Atom& a, const Atom& b) { return a.at < b.at; });
    mAtoms.clear();
    for (Atom& atom : atoms)
    {
        atom.length = llmax(1, atom.length);
        if (!mAtoms.empty() && atom.at < atomRange(mAtoms.back()).end)
        {
            continue;
        }
        if (atom.view && atom.view->getParent() != this)
        {
            addChild(atom.view);
        }
        if (atom.view)
        {
            atom.view->setVisible(false);
        }
        mLayout.invalidateLine(atom.at.line);
        mAtoms.push_back(std::move(atom));
    }
    mHoverAtom   = -1;
    mPressedAtom = -1;
}

void ALTextView::addAtom(Atom atom)
{
    atom.length   = llmax(1, atom.length);
    const auto at = std::lower_bound(mAtoms.begin(), mAtoms.end(), atom.at, [](const Atom& a, const ALTextPos& p) { return a.at < p; });
    if ((at != mAtoms.end() && at->at < atomRange(atom).end) || (at != mAtoms.begin() && atom.at < atomRange(*(at - 1)).end))
    {
        if (atom.view && atom.view->getParent() != this)
        {
            atom.view->die();
        }
        return;
    }
    if (atom.view && atom.view->getParent() != this)
    {
        addChild(atom.view);
    }
    if (atom.view)
    {
        atom.view->setVisible(false);
    }
    mLayout.invalidateLine(atom.at.line);
    mAtoms.insert(at, std::move(atom));
    mHoverAtom   = -1;
    mPressedAtom = -1;
}

const ALTextView::Atom* ALTextView::atomAt(const ALTextPos& pos) const
{
    auto after = std::upper_bound(mAtoms.begin(), mAtoms.end(), pos, [](const ALTextPos& p, const Atom& a) { return p < a.at; });
    if (after == mAtoms.begin())
    {
        return nullptr;
    }
    const Atom& atom = *(after - 1);
    return pos < atomRange(atom).end ? &atom : nullptr;
}

void ALTextView::provideSubstitutions(S32 line, std::vector<ALTextLayout::Substitution>& out) const
{
    const S32 length = mDocument.lineLength(line);
    auto      first  = std::lower_bound(mSubstitutions.begin(), mSubstitutions.end(), ALTextPos(line, 0),
                                        [](const Substitution& s, const ALTextPos& p) { return s.range.begin < p; });
    for (auto it = first; it != mSubstitutions.end() && it->range.begin.line == line; ++it)
    {
        if (it->shown.empty())
        {
            // The text as it is, which needs nothing of the layout.
            continue;
        }
        ALTextLayout::Substitution sub;
        sub.begin = it->range.begin.column;
        sub.end   = llmin(it->range.end.column, length);
        sub.shown = it->shown;
        sub.id    = static_cast<S32>(it - mSubstitutions.begin());
        out.push_back(std::move(sub));
    }
    auto first_atom = std::lower_bound(mAtoms.begin(), mAtoms.end(), ALTextPos(line, 0), [](const Atom& a, const ALTextPos& p) { return a.at < p; });
    for (auto it = first_atom; it != mAtoms.end() && it->at.line == line; ++it)
    {
        ALTextLayout::Substitution box;
        box.begin  = it->at.column;
        box.end    = llmin(it->at.column + it->length, length);
        box.width  = static_cast<F32>(llmax(0, it->width));
        box.height = llmax(0, it->height);
        box.id     = atomId(static_cast<size_t>(it - mAtoms.begin()));
        out.push_back(std::move(box));
    }
}

void ALTextView::placeAtomViews()
{
    const LLRect text  = textRect();
    const S32    row_h = mLayout.rowHeight();
    for (Atom& atom : mAtoms)
    {
        if (!atom.view)
        {
            continue;
        }
        // Its box: where the row is on the screen, if it is, and where the
        // layout put the gap on the row.
        bool placed = false;
        // Its line first, before its row is measured: an atom on a line
        // scrolled away has no box, and there may be hundreds of them.
        const bool line_in_sight = row_h > 0 && atom.at.line < mDocument.lineCount() && !mLayout.hidden(atom.at.line) &&
                                   mLayout.lineTop(atom.at.line) < mScrollY + text.getHeight() &&
                                   mLayout.lineTop(atom.at.line) + mLayout.lineHeight(atom.at.line) > mScrollY;
        if (line_in_sight)
        {
            S32       row;
            const F32 x0     = mLayout.xOf(atom.at.line, atom.at.column, &row);
            const F32 x1     = mLayout.xOf(atom.at.line, atomRange(atom).end.column);
            const S32 top    = screenTopOf(text, atom.at.line, row);
            const S32 height = mLayout.rowHeightOf(atom.at.line, row);
            if (top > text.mBottom && top - height < text.mTop)
            {
                const F32    left = static_cast<F32>(text.mLeft) - mScrollX;
                const LLRect box(static_cast<S32>(left + x0), top, static_cast<S32>(left + x1), top - height);
                if (atom.view->getRect() != box)
                {
                    atom.view->setShape(box);
                }
                placed = true;
            }
        }
        if (!placed && atom.view->getVisible())
        {
            letGoOfAtomView(atom.view);
        }
        atom.view->setVisible(placed);
    }
}

S32 ALTextView::focusedAtom() const
{
    for (size_t i = 0; i < mAtoms.size(); ++i)
    {
        if (mAtoms[i].view && gFocusMgr.childHasKeyboardFocus(mAtoms[i].view))
        {
            return static_cast<S32>(i);
        }
    }
    return -1;
}

bool ALTextView::atomViewFocused() const
{
    return focusedAtom() >= 0;
}

void ALTextView::letGoOfAtomView(LLView* view)
{
    if (!view || !gFocusMgr.childHasKeyboardFocus(view))
    {
        return;
    }
    if (mTakesFocus)
    {
        setFocus(true);
    }
    else
    {
        gFocusMgr.releaseFocusIfNeeded(view);
    }
}

bool ALTextView::focusAtomView(bool forward)
{
    const S32 from = focusedAtom();
    // From the text: the first view at or past the caret, or the last at
    // or before it. From a view: the one after or before it; past the
    // ends, the text.
    S32 index = -1;
    if (from >= 0)
    {
        index = from + (forward ? 1 : -1);
    }
    else
    {
        for (size_t i = 0; i < mAtoms.size(); ++i)
        {
            const bool past = forward ? mAtoms[i].at >= mCaret : mAtoms[i].at <= mCaret;
            if (past && mAtoms[i].view)
            {
                index = static_cast<S32>(i);
                if (forward)
                {
                    break;
                }
            }
        }
    }
    for (; index >= 0 && index < static_cast<S32>(mAtoms.size()); index += forward ? 1 : -1)
    {
        LLView* view = mAtoms[static_cast<size_t>(index)].view;
        if (!view)
        {
            continue;
        }
        // Brought into view first, so that its box is on the screen
        // before it is looked at.
        placeCaret(mAtoms[static_cast<size_t>(index)].at, false);
        mDesiredX = -1.f;
        scrollToCaret();
        placeAtomViews();
        // A control takes it, or the first control in it; a plain view is
        // passed over.
        LLUICtrl* ctrl = dynamic_cast<LLUICtrl*>(view);
        if (!ctrl || !ctrl->getEnabled())
        {
            continue;
        }
        if (!ctrl->focusFirstItem(false, false))
        {
            ctrl->setFocus(true);
        }
        if (!gFocusMgr.childHasKeyboardFocus(view))
        {
            continue;
        }
        return true;
    }
    if (from >= 0)
    {
        // Past the ends: back to the text.
        if (mTakesFocus)
        {
            setFocus(true);
        }
        else
        {
            gFocusMgr.releaseFocusIfNeeded(mAtoms[static_cast<size_t>(from)].view);
        }
        return true;
    }
    return false;
}

ALTextPos ALTextView::snapped(const ALTextPos& pos, const ALTextPos& from) const
{
    if (const Substitution* sub = substitutionAt(pos); sub && pos != sub->range.begin)
    {
        return pos > from ? sub->range.end : sub->range.begin;
    }
    if (const Atom* atom = atomAt(pos); atom && pos != atom->at)
    {
        return pos > from ? atomRange(*atom).end : atom->at;
    }
    return pos;
}

const ALTextView::Substitution* ALTextView::linkAtLocal(S32 x, S32 y)
{
    const LLRect text = textRect();
    if (mSubstitutions.empty() || !text.pointInRect(x, y) || (text.mTop - y) + mScrollY >= mLayout.totalHeight())
    {
        return nullptr;
    }
    const ALTextPos     at  = posAtLocal(x, y, false);
    const Substitution* sub = substitutionAt(at);
    if (!sub || !sub->link)
    {
        return nullptr;
    }
    // On its glyphs, not merely on its line past its end.
    S32       row;
    const F32 x0 = mLayout.xOf(at.line, sub->range.begin.column, &row);
    const F32 x1 = mLayout.xOf(at.line, sub->range.end.column);
    const F32 xr = static_cast<F32>(x - text.mLeft) + mScrollX;
    return xr >= x0 && xr < x1 ? sub : nullptr;
}

const ALTextView::Atom* ALTextView::atomAtLocal(S32 x, S32 y)
{
    const LLRect text = textRect();
    if (mAtoms.empty() || !text.pointInRect(x, y) || (text.mTop - y) + mScrollY >= mLayout.totalHeight())
    {
        return nullptr;
    }
    const ALTextPos at   = posAtLocal(x, y, false);
    const Atom*     atom = atomAt(at);
    if (!atom)
    {
        return nullptr;
    }
    S32       row;
    const F32 x0 = mLayout.xOf(at.line, atom->at.column, &row);
    const F32 x1 = mLayout.xOf(at.line, atomRange(*atom).end.column);
    const F32 xr = static_cast<F32>(x - text.mLeft) + mScrollX;
    return xr >= x0 && xr < x1 ? atom : nullptr;
}

// --- the spell check -------------------------------------------------------------

void ALTextView::setSpellCheck(bool check)
{
    if (check == mSpellCheck)
    {
        return;
    }
    mSpellCheck = check;
    if (check && !mSpellSettingsConnection.connected())
    {
        mSpellSettingsConnection = LLSpellChecker::setSettingsChangeCallback([this]() { recheckSpelling(); });
    }
    recheckSpelling();
}

bool ALTextView::getSpellCheck() const
{
    return mSpellCheck && !mReadOnly && (mSpellChecker || LLSpellChecker::getUseSpellCheck());
}

void ALTextView::setSpellChecker(spell_checker_t checker, spell_suggester_t suggester)
{
    mSpellChecker   = std::move(checker);
    mSpellSuggester = std::move(suggester);
    recheckSpelling();
}

void ALTextView::recheckSpelling()
{
    mSpellLines.clear();
    mSuggestions.clear();
    mSuggestedFor = ALTextRange();
}

void ALTextView::checkLine(S32 line)
{
    if (static_cast<size_t>(line) >= mSpellLines.size())
    {
        mSpellLines.resize(static_cast<size_t>(mDocument.lineCount()));
    }
    SpellLine& checked = mSpellLines[static_cast<size_t>(line)];
    checked.valid      = true;
    checked.words.clear();
    if (!getSpellCheck())
    {
        return;
    }
    const std::string& text = mDocument.line(line);
    // What is prose: everything, without a grammar or with one that
    // says so; comments and strings otherwise.
    auto check_stretch = [&](S32 begin, S32 end) {
        size_t at = static_cast<size_t>(begin);
        while (at < static_cast<size_t>(end))
        {
            const auto word = utf8str_next_word_range(text, at);
            if (word.first >= word.second || word.first >= static_cast<size_t>(end))
            {
                break;
            }
            const size_t word_end = llmin(word.second, static_cast<size_t>(end));
            at                    = word.second;
            const std::string_view piece(text.data() + word.first, word_end - word.first);
            if (!proseWord(piece))
            {
                continue;
            }
            const std::string spelled(piece);
            const bool        ok = mSpellChecker ? mSpellChecker(spelled) : LLSpellChecker::instance().checkSpelling(spelled);
            if (!ok)
            {
                checked.words.emplace_back(static_cast<S32>(word.first), static_cast<S32>(word_end));
            }
        }
    };
    const std::shared_ptr<const ALSyntaxGrammar> grammar = mHighlighter.grammar();
    if (!grammar || grammar->prose())
    {
        check_stretch(0, static_cast<S32>(text.size()));
        return;
    }
    for (const ALSyntaxToken& token : mHighlighter.tokens(line))
    {
        if (token.kind == ALSyntaxKind::Comment || token.kind == ALSyntaxKind::DocComment || token.kind == ALSyntaxKind::String)
        {
            check_stretch(token.begin, token.end);
        }
    }
}

const std::vector<std::pair<S32, S32>>& ALTextView::misspellings(S32 line)
{
    static const std::vector<std::pair<S32, S32>> none;
    if (line < 0 || line >= mDocument.lineCount())
    {
        return none;
    }
    if (static_cast<size_t>(line) >= mSpellLines.size() || !mSpellLines[static_cast<size_t>(line)].valid)
    {
        checkLine(line);
    }
    return mSpellLines[static_cast<size_t>(line)].words;
}

bool ALTextView::misspelledAt(const ALTextPos& pos, ALTextRange* word)
{
    for (const auto& [begin, end] : misspellings(pos.line))
    {
        if (begin <= pos.column && pos.column <= end)
        {
            if (word)
            {
                *word = ALTextRange(ALTextPos(pos.line, begin), ALTextPos(pos.line, end));
            }
            return true;
        }
    }
    return false;
}

void ALTextView::refreshSuggestions()
{
    mSuggestions.clear();
    mSuggestedFor = ALTextRange();
    ALTextRange word;
    if (!getSpellCheck() || !misspelledAt(mCaret, &word))
    {
        return;
    }
    mSuggestedFor = word;
    if (mSpellSuggester)
    {
        mSpellSuggester(mDocument.text(word), mSuggestions);
    }
    else if (!mSpellChecker && LLSpellChecker::instanceExists())
    {
        LLSpellChecker::instance().getSuggestions(mDocument.text(word), mSuggestions);
    }
}

const std::string& ALTextView::getSuggestion(U32 index) const
{
    return index < mSuggestions.size() ? mSuggestions[index] : LLStringUtil::null;
}

U32 ALTextView::getSuggestionCount() const
{
    return static_cast<U32>(mSuggestions.size());
}

void ALTextView::replaceWithSuggestion(U32 index)
{
    if (index >= mSuggestions.size() || mSuggestedFor.empty() || mReadOnly)
    {
        return;
    }
    const ALTextRange word       = mSuggestedFor;
    const std::string suggestion = mSuggestions[index];
    mSuggestions.clear();
    mSuggestedFor = ALTextRange();
    if (!edit(word, suggestion).nothing())
    {
        afterEdit();
    }
}

void ALTextView::addToDictionary()
{
    if (canAddToDictionary())
    {
        LLSpellChecker::instance().addToCustomDictionary(mDocument.text(mSuggestedFor));
        recheckSpelling();
    }
}

bool ALTextView::canAddToDictionary() const
{
    return getSpellCheck() && !mSuggestedFor.empty() && !mSpellChecker && LLSpellChecker::instanceExists();
}

void ALTextView::addToIgnore()
{
    if (canAddToIgnore())
    {
        LLSpellChecker::instance().addToIgnoreList(mDocument.text(mSuggestedFor));
        recheckSpelling();
    }
}

bool ALTextView::canAddToIgnore() const
{
    return canAddToDictionary();
}

void ALTextView::onDocumentEdit(const ALTextDocument::Edit& edit)
{
    // The lines the edit touched are checked again when they are next
    // drawn; the ones below slide.
    {
        const S32 count = static_cast<S32>(mSpellLines.size());
        const S32 first = llclamp(edit.range.begin.line, 0, count);
        const S32 last  = llclamp(edit.range.end.line, first, count - 1);
        const S32 made  = 1 + static_cast<S32>(std::count(edit.inserted.begin(), edit.inserted.end(), '\n'));
        if (first < count)
        {
            mSpellLines.erase(mSpellLines.begin() + first, mSpellLines.begin() + last + 1);
        }
        mSpellLines.insert(mSpellLines.begin() + llmin(first, static_cast<S32>(mSpellLines.size())), made, SpellLine());
        mSpellLines.resize(static_cast<size_t>(mDocument.lineCount()));
        mSpellTimer.reset();
    }
    mSuggestions.clear();
    mSuggestedFor = ALTextRange();

    // The layers: what is after the edit slides with the text, what it
    // cut through goes.
    if (!mSubstitutions.empty())
    {
        mSubstitutions.erase(std::remove_if(mSubstitutions.begin(), mSubstitutions.end(), [&](Substitution& s) { return !edit.slide(s.range); }), mSubstitutions.end());
        mHoverLink   = -1;
        mPressedLink = -1;
    }
    if (!mStyles.empty())
    {
        mStyles.erase(std::remove_if(mStyles.begin(), mStyles.end(), [&](Style& s) { return !edit.slide(s.range); }), mStyles.end());
    }
    if (!mAtoms.empty())
    {
        for (size_t i = 0; i < mAtoms.size();)
        {
            ALTextRange range = atomRange(mAtoms[i]);
            if (edit.slide(range))
            {
                mAtoms[i].at = range.begin;
                ++i;
            }
            else
            {
                if (mAtoms[i].view)
                {
                    letGoOfAtomView(mAtoms[i].view);
                    removeChild(mAtoms[i].view);
                    mAtoms[i].view->die();
                }
                mAtoms.erase(mAtoms.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
        mHoverAtom   = -1;
        mPressedAtom = -1;
    }
}

void ALTextView::moveVertically(S32 rows, bool extend)
{
    const S32 row_h = mLayout.rowHeight();
    if (row_h <= 0)
    {
        return;
    }
    S32       row;
    const F32 x = mLayout.xOf(mCaret.line, mCaret.column, &row);
    if (mDesiredX < 0.f)
    {
        mDesiredX = x;
    }
    S32 line = mCaret.line;
    for (; rows < 0; ++rows)
    {
        if (row > 0)
        {
            --row;
        }
        else if (const S32 above = mLayout.visibleFrom(line - 1, -1); above >= 0)
        {
            line = above;
            row  = mLayout.rowCount(line) - 1;
        }
        else
        {
            placeCaret(mDocument.start(), extend);
            mDesiredX = -1.f;
            scrollToCaret();
            return;
        }
    }
    for (; rows > 0; --rows)
    {
        if (row + 1 < mLayout.rowCount(line))
        {
            ++row;
        }
        else if (const S32 below = mLayout.visibleFrom(line + 1, 1); below >= 0)
        {
            line = below;
            row  = 0;
        }
        else
        {
            placeCaret(mDocument.end(), extend);
            mDesiredX = -1.f;
            scrollToCaret();
            return;
        }
    }
    placeCaret(ALTextPos(line, mLayout.columnAt(line, row, mDesiredX, true)), extend);
    scrollToCaret();
}

// --- editing -------------------------------------------------------------------

ALTextDocument::Edit ALTextView::edit(const ALTextRange& range, std::string_view text)
{
    const ALTextPos      before = mCaret;
    ALTextDocument::Edit done   = mDocument.replace(range, text);
    if (done.nothing())
    {
        return done;
    }
    const ALTextPos after = mDocument.clamp(done.endAfter());
    mUndo.record(done, before, after, LLTimer::getElapsedSeconds());
    placeCaret(after, false);
    return done;
}

void ALTextView::afterEdit()
{
    mDesiredX          = -1.f;
    mChangedSinceFocus = true;
    mBlink.reset();
    scrollToCaret();
    refreshFind();
    mChanged();
}

void ALTextView::insertText(std::string_view text)
{
    if (mReadOnly)
    {
        return;
    }
    if (!edit(selection(), text).nothing())
    {
        afterEdit();
    }
}

bool ALTextView::replaceAll(std::vector<std::pair<ALTextRange, std::string>> edits)
{
    if (mReadOnly || edits.empty())
    {
        return false;
    }
    for (auto& one : edits)
    {
        one.first = one.first.normalised();
    }
    std::sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.first.begin < b.first.begin; });

    // Where the caret ends up: past every replacement before it on its
    // line, its column moves by what each grew or shrank; inside one, it
    // keeps its place in what replaced it, or the end of it. A
    // replacement across lines above it moves its line.
    const ALTextPos was   = mCaret;
    ALTextPos       caret = was;
    for (const auto& [range, text] : edits)
    {
        const S32 lines_in = static_cast<S32>(std::count(text.begin(), text.end(), '\n'));
        const S32 lines_was = range.end.line - range.begin.line;
        ALTextPos end_after;
        if (lines_in == 0)
        {
            end_after = ALTextPos(range.begin.line, range.begin.column + static_cast<S32>(text.size()));
        }
        else
        {
            end_after = ALTextPos(range.begin.line + lines_in, static_cast<S32>(text.size() - text.rfind('\n') - 1));
        }
        if (range.end <= was)
        {
            if (range.end.line == was.line)
            {
                caret.column += end_after.column - range.end.column;
            }
            caret.line += lines_in - lines_was;
        }
        else if (range.begin < was)
        {
            if (lines_in == 0 && lines_was == 0)
            {
                caret.column = range.begin.column + llmin(was.column - range.begin.column, static_cast<S32>(text.size()));
            }
            else
            {
                caret = end_after;
            }
            break;
        }
    }

    // From the last to the first, so no replacement moves another.
    mUndo.beginGroup();
    bool any = false;
    for (auto it = edits.rbegin(); it != edits.rend(); ++it)
    {
        if (!edit(it->first, it->second).nothing())
        {
            any = true;
        }
    }
    mUndo.endGroup();
    if (!any)
    {
        return false;
    }
    placeCaret(mDocument.clamp(caret), false);
    afterEdit();
    return true;
}

void ALTextView::goTo(const ALTextPos& pos)
{
    setCaret(mDocument.clamp(pos));
}

void ALTextView::goTo(const ALTextRange& range)
{
    setSelection(ALTextRange(mDocument.clamp(range.begin), mDocument.clamp(range.end)));
}

void ALTextView::deleteRange(const ALTextRange& range)
{
    if (mReadOnly)
    {
        return;
    }
    if (!edit(range, std::string_view()).nothing())
    {
        afterEdit();
    }
}

std::string ALTextView::tabText(const ALTextPos& at) const
{
    if (!mSoftTabs)
    {
        return "\t";
    }
    const S32 column = mDocument.displayColumn(at, mTabWidth);
    return std::string(mTabWidth - (column % mTabWidth), ' ');
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
    mAnchor = moved(anchor_was);
    mCaret  = moved(caret_was);
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
    mAnchor = ALTextPos(anchor.line + count, anchor.column);
    placeCaret(ALTextPos(caret.line + count, caret.column), true);
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
    mAnchor = ALTextPos(anchor.line + direction, anchor.column);
    placeCaret(ALTextPos(caret.line + direction, caret.column), true);
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

bool ALTextView::perform(ALEditorCommand command)
{
    typedef ALEditorCommand C;
    const bool extend = command >= C::SelectLeft && command <= C::SelectPageDown;
    auto move = [&](const ALTextPos& to) {
        placeCaret(to, extend);
        mDesiredX = -1.f;
        scrollToCaret();
        return true;
    };
    // An arrow with a selection and no shift collapses it to that end.
    auto collapse = [&](bool to_begin) {
        if (!hasSelection() || extend)
        {
            return false;
        }
        const ALTextRange range = selection().normalised();
        return move(to_begin ? range.begin : range.end);
    };
    if (mReadOnly && editsText(command))
    {
        return false;
    }
    switch (command)
    {
        case C::None:
        case C::COUNT:
            return false;
        case C::MoveLeft:
        case C::SelectLeft:
            return collapse(true) || move(mDocument.prevCluster(mCaret));
        case C::MoveRight:
        case C::SelectRight:
            return collapse(false) || move(mDocument.nextCluster(mCaret));
        case C::MoveUp:
        case C::SelectUp:
            moveVertically(-1, extend);
            return true;
        case C::MoveDown:
        case C::SelectDown:
            moveVertically(1, extend);
            return true;
        case C::MoveWordLeft:
        case C::SelectWordLeft:
            return move(mDocument.prevWord(mCaret));
        case C::MoveWordRight:
        case C::SelectWordRight:
            return move(mDocument.nextWord(mCaret));
        case C::MoveLineStart:
        case C::SelectLineStart:
        {
            // To the first thing on the line, or to the line's start from
            // there.
            const std::string& line   = mDocument.line(mCaret.line);
            S32                indent = 0;
            while (indent < static_cast<S32>(line.size()) && (line[indent] == ' ' || line[indent] == '\t'))
            {
                ++indent;
            }
            return move(ALTextPos(mCaret.line, mCaret.column == indent ? 0 : indent));
        }
        case C::MoveLineEnd:
        case C::SelectLineEnd:
            return move(mDocument.lineEnd(mCaret.line));
        case C::MoveDocStart:
        case C::SelectDocStart:
            return move(mDocument.start());
        case C::MoveDocEnd:
        case C::SelectDocEnd:
            return move(mDocument.end());
        case C::MovePageUp:
        case C::SelectPageUp:
            moveVertically(-rowsPerPage(), extend);
            return true;
        case C::MovePageDown:
        case C::SelectPageDown:
            moveVertically(rowsPerPage(), extend);
            return true;
        case C::SelectAll:
            selectAll();
            return true;
        case C::DeleteLeft:
            if (hasSelection())
            {
                deleteRange(selection());
            }
            else if (mCaret != mDocument.start())
            {
                deleteRange(ALTextRange(mDocument.prevCluster(mCaret), mCaret));
            }
            return true;
        case C::DeleteRight:
            if (hasSelection())
            {
                deleteRange(selection());
            }
            else if (mCaret != mDocument.end())
            {
                deleteRange(ALTextRange(mCaret, mDocument.nextCluster(mCaret)));
            }
            return true;
        case C::DeleteWordLeft:
            deleteRange(hasSelection() ? selection() : ALTextRange(mDocument.prevWord(mCaret), mCaret));
            return true;
        case C::DeleteWordRight:
            deleteRange(hasSelection() ? selection() : ALTextRange(mCaret, mDocument.nextWord(mCaret)));
            return true;
        case C::DeleteToLineStart:
            // Back to the line's start, or the break before it from there,
            // as Command-Backspace on the Mac.
            if (hasSelection())
            {
                deleteRange(selection());
            }
            else if (mCaret.column > 0)
            {
                deleteRange(ALTextRange(ALTextPos(mCaret.line, 0), mCaret));
            }
            else if (mCaret != mDocument.start())
            {
                deleteRange(ALTextRange(mDocument.prevCluster(mCaret), mCaret));
            }
            return true;
        case C::DeleteToLineEnd:
            // On to the line's end, or the break after it from there, as
            // Control-K on the Mac.
            if (hasSelection())
            {
                deleteRange(selection());
            }
            else if (mCaret != mDocument.lineEnd(mCaret.line))
            {
                deleteRange(ALTextRange(mCaret, mDocument.lineEnd(mCaret.line)));
            }
            else if (mCaret != mDocument.end())
            {
                deleteRange(ALTextRange(mCaret, mDocument.nextCluster(mCaret)));
            }
            return true;
        case C::NewLine:
        {
            // The new line starts with the indentation of the one it leaves.
            const ALTextPos    at   = selection().normalised().begin;
            const std::string& line = mDocument.line(at.line);
            std::string        text = "\n";
            S32                n    = 0;
            while (n < at.column && n < static_cast<S32>(line.size()) && (line[n] == ' ' || line[n] == '\t'))
            {
                ++n;
            }
            text.append(line, 0, n);
            insertText(text);
            return true;
        }
        case C::Indent:
            // A selection, however small, indents its lines; a caret
            // alone puts a tab in.
            if (hasSelection())
            {
                indentLines(true);
            }
            else
            {
                insertText(tabText(mCaret));
            }
            return true;
        case C::Unindent:
            indentLines(false);
            return true;
        case C::Undo:
            undo();
            return true;
        case C::Redo:
            redo();
            return true;
        case C::Cut:
            cut();
            return true;
        case C::Copy:
            copy();
            return true;
        case C::Paste:
            paste();
            return true;
        case C::Delete:
            doDelete();
            return true;
        case C::ToggleComment:
            return toggleComment();
        case C::DuplicateLine:
            duplicateLines();
            return true;
        case C::MoveLineUp:
            moveLines(-1);
            return true;
        case C::MoveLineDown:
            moveLines(1);
            return true;
        case C::DeleteLine:
            deleteLines();
            return true;
        case C::Fold:
        case C::Unfold:
        case C::FoldAll:
        case C::UnfoldAll:
            return performFold(command);
        case C::Complete:
            return complete();
        case C::GoToDefinition:
        case C::FindReferences:
        case C::Rename:
            return performSymbol(command);
        case C::Find:
            showFind(false);
            return true;
        case C::Replace:
            showFind(!mReadOnly);
            return true;
        case C::FindNext:
            return findNext(true);
        case C::FindPrevious:
            return findNext(false);
    }
    return false;
}

bool ALTextView::canPerform(ALEditorCommand command) const
{
    typedef ALEditorCommand C;
    switch (command)
    {
        case C::Undo:
            return canUndo();
        case C::Redo:
            return canRedo();
        case C::Cut:
            return canCut();
        case C::Copy:
            return canCopy();
        case C::Paste:
            return canPaste();
        case C::Delete:
            return canDoDelete();
        case C::SelectAll:
            return canSelectAll();
        case C::ToggleComment:
            return !mReadOnly && mHighlighter.grammar() && !mHighlighter.grammar()->lineComment().empty();
        case C::Fold:
        case C::Unfold:
        case C::FoldAll:
        case C::UnfoldAll:
            return canFold(command);
        case C::GoToDefinition:
        case C::FindReferences:
        case C::Rename:
            return canSymbol(command);
        default:
            return !(mReadOnly && editsText(command));
    }
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
        mAnchor = ALTextPos(first, 0);
        mCaret  = last + 1 < mDocument.lineCount() ? ALTextPos(last + 1, 0) : mDocument.lineEnd(last);
    }
    else
    {
        placeCaret(ALTextPos(caret_was.line, caret_was.column + caret_shift), false);
    }
    afterEdit();
    return true;
}

S32 ALTextView::screenTopOf(const LLRect& text, S32 line, S32 row)
{
    return text.mTop - (mLayout.lineTop(line) + mLayout.rowTop(line, row) - mScrollY);
}

void ALTextView::forEachVisibleRow(const LLRect& text, const std::function<void(S32, S32, S32)>& visit)
{
    const S32 row_h = mLayout.rowHeight();
    if (row_h <= 0)
    {
        return;
    }
    const S32 count    = mDocument.lineCount();
    const S32 bottom_y = mScrollY + text.getHeight();
    for (S32 line = mLayout.lineAtY(mScrollY); line < count; ++line)
    {
        if (mLayout.hidden(line))
        {
            continue;
        }
        const ALTextLayout::Line& laid = mLayout.line(line);
        const S32                 top  = mLayout.lineTop(line);
        if (top >= bottom_y)
        {
            break;
        }
        for (size_t r = 0; r < laid.rows.size(); ++r)
        {
            const S32 row_top = top + laid.rows[r].top;
            if (row_top + laid.rows[r].height <= mScrollY)
            {
                continue;
            }
            if (row_top >= bottom_y)
            {
                break;
            }
            visit(line, static_cast<S32>(r), text.mTop - (row_top - mScrollY));
        }
    }
}

// --- LLEditMenuHandler ---------------------------------------------------------

void ALTextView::undo()
{
    if (mReadOnly)
    {
        return;
    }
    if (std::optional<ALTextPos> caret = mUndo.undo())
    {
        placeCaret(*caret, false);
        afterEdit();
    }
}

void ALTextView::redo()
{
    if (mReadOnly)
    {
        return;
    }
    if (std::optional<ALTextPos> caret = mUndo.redo())
    {
        placeCaret(*caret, false);
        afterEdit();
    }
}

void ALTextView::cut()
{
    if (!canCut())
    {
        return;
    }
    copy();
    deleteRange(selection());
}

void ALTextView::copy()
{
    if (!hasSelection())
    {
        return;
    }
    const std::string text = selectedText();
    LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size()));
}

bool ALTextView::canPaste() const
{
    return !mReadOnly && LLClipboard::instance().isTextAvailable();
}

void ALTextView::paste()
{
    if (!canPaste())
    {
        return;
    }
    std::string text;
    if (!LLClipboard::instance().pasteFromClipboard(text))
    {
        return;
    }
    mUndo.beginGroup();
    insertText(text);
    mUndo.endGroup();
}

void ALTextView::doDelete()
{
    if (canDoDelete())
    {
        deleteRange(selection());
    }
}

void ALTextView::selectAll()
{
    mAnchor   = mDocument.start();
    mCaret    = mDocument.end();
    mDesiredX = -1.f;
    mBlink.reset();
}

void ALTextView::deselect()
{
    mAnchor = mCaret;
}

// --- the input method ----------------------------------------------------------

void ALTextView::allowLanguageInput(bool allow)
{
    if (LLWindow* window = getWindow())
    {
        window->allowLanguageTextInput(this, allow);
    }
}

ALTextRange ALTextView::preeditRange() const
{
    return ALTextRange(mPreeditBegin, ALTextPos(mPreeditBegin.line, mPreeditBegin.column + mPreeditLength));
}

void ALTextView::resetPreedit()
{
    if (hasSelection() && !hasPreedit())
    {
        deleteRange(selection());
    }
    if (!hasPreedit())
    {
        return;
    }
    // Put back what was there: nothing, or what an overwrite took. None of
    // this is an edit the journal sees; the composition was never text.
    mDocument.replace(preeditRange(), mPreeditOverwritten);
    const ALTextPos begin = mPreeditBegin;
    mPreeditLength        = 0;
    mPreeditSegmentEnds.clear();
    mPreeditStandouts.clear();
    mPreeditOverwritten.clear();
    placeCaret(begin, false);
    mChanged();
}

void ALTextView::updatePreedit(std::string_view preedit_string, const segment_lengths_t& preedit_segment_lengths,
                               const standouts_t& preedit_standouts, S32 caret_position)
{
    if (mReadOnly)
    {
        return;
    }
    if (LLWindow* window = getWindow())
    {
        window->hideCursorUntilMouseMove();
    }
    resetPreedit();

    // A composition is one line of text; a newline in one would put the
    // rest of it where this cannot count it.
    std::string composed(preedit_string);
    std::replace(composed.begin(), composed.end(), '\n', ' ');

    const ALTextPos at = mCaret;
    ALTextPos       end = at;
    if (gKeyboard && LL_KIM_OVERWRITE == gKeyboard->getInsertMode())
    {
        // As much of the line as the composition covers, in whole
        // characters, so the overwrite never ends inside one.
        const size_t want = composed.size();
        while (end.line == at.line && static_cast<size_t>(end.column - at.column) < want && end.column < mDocument.lineLength(at.line))
        {
            const ALTextPos next = mDocument.nextCluster(end);
            if (next.line != at.line || static_cast<size_t>(next.column - at.column) > want)
            {
                break;
            }
            end = next;
        }
        mPreeditOverwritten = mDocument.text(ALTextRange(at, end));
    }
    else
    {
        mPreeditOverwritten.clear();
    }
    mDocument.replace(ALTextRange(at, end), composed);

    mPreeditBegin  = at;
    mPreeditLength = static_cast<S32>(composed.size());
    mPreeditSegmentEnds.clear();
    S32 sum = 0;
    for (S32 length : preedit_segment_lengths)
    {
        sum += llmax(0, length);
        mPreeditSegmentEnds.push_back(llmin(sum, mPreeditLength));
    }
    if (mPreeditSegmentEnds.empty() || mPreeditSegmentEnds.back() < mPreeditLength)
    {
        mPreeditSegmentEnds.push_back(mPreeditLength);
    }
    mPreeditStandouts = preedit_standouts;
    while (mPreeditStandouts.size() < mPreeditSegmentEnds.size())
    {
        mPreeditStandouts.push_back(false);
    }

    placeCaret(ALTextPos(at.line, at.column + llclamp(caret_position, 0, mPreeditLength)), false);
    mDesiredX = -1.f;
    scrollToCaret();
    mChanged();
}

void ALTextView::markAsPreedit(S32 position, S32 length)
{
    if (hasPreedit())
    {
        LL_WARNS() << "markAsPreedit with a composition in progress" << LL_ENDL;
    }
    const ALTextPos begin = mDocument.posAt(static_cast<size_t>(llmax(0, position)));
    ALTextPos       end   = mDocument.posAt(static_cast<size_t>(llmax(0, position + length)));
    if (end.line != begin.line)
    {
        end = mDocument.lineEnd(begin.line);
    }
    deselect();
    placeCaret(begin, false);
    mPreeditSegmentEnds.clear();
    mPreeditStandouts.clear();
    mPreeditOverwritten.clear();
    mPreeditBegin  = begin;
    mPreeditLength = llmax(0, end.column - begin.column);
    if (mPreeditLength > 0)
    {
        mPreeditSegmentEnds.push_back(mPreeditLength);
        mPreeditStandouts.push_back(false);
        if (gKeyboard && LL_KIM_OVERWRITE == gKeyboard->getInsertMode())
        {
            mPreeditOverwritten = mDocument.text(preeditRange());
        }
    }
}

void ALTextView::getPreeditRange(S32* position, S32* length) const
{
    if (hasPreedit())
    {
        *position = static_cast<S32>(mDocument.offsetOf(mPreeditBegin));
        *length   = mPreeditLength;
    }
    else
    {
        *position = static_cast<S32>(mDocument.offsetOf(mCaret));
        *length   = 0;
    }
}

void ALTextView::getSelectionRange(S32* position, S32* length) const
{
    const ALTextRange range = selection().normalised();
    *position               = static_cast<S32>(mDocument.offsetOf(range.begin));
    *length                 = static_cast<S32>(mDocument.offsetOf(range.end)) - *position;
}

bool ALTextView::getPreeditLocation(S32 query_offset, LLCoordGL* coord, LLRect* bounds, LLRect* control) const
{
    const LLRect text = textRect();
    if (control)
    {
        LLRect screen;
        localRectToScreen(text, &screen);
        LLUI::getInstance()->screenRectToGL(screen, control);
    }
    const ALTextPos begin = hasPreedit() ? mPreeditBegin : mCaret;
    const ALTextPos query = query_offset >= 0 ? ALTextPos(begin.line, begin.column + query_offset) : mCaret;
    if (query.line != begin.line || query.column < begin.column || query.column > begin.column + mPreeditLength)
    {
        return false;
    }
    const S32 row_h = mLayout.rowHeight();
    if (row_h <= 0)
    {
        return false;
    }
    S32       row;
    const F32 qx  = lay().xOf(query.line, query.column, &row);
    const S32 top = text.mTop - (lay().lineTop(query.line) + lay().rowTop(query.line, row) + (lay().rowHeightOf(query.line, row) - row_h) - mScrollY);
    if (top > text.mTop || top - row_h < text.mBottom)
    {
        return false;
    }
    const F32 left = static_cast<F32>(text.mLeft) - mScrollX;
    if (coord)
    {
        S32 sx, sy;
        localPointToScreen(static_cast<S32>(left + qx), top - row_h / 2, &sx, &sy);
        LLUI::getInstance()->screenPointToGL(sx, sy, &coord->mX, &coord->mY);
    }
    if (bounds)
    {
        S32       row_begin, row_end;
        const F32 x0 = lay().xOf(begin.line, begin.column, &row_begin);
        F32       x1 = lay().xOf(begin.line, begin.column + mPreeditLength, &row_end);
        if (row_end != row_begin)
        {
            x1 = lay().line(begin.line).rows[row_begin].width;
        }
        LLRect local(static_cast<S32>(left + x0), top, static_cast<S32>(left + x1), top - row_h);
        LLRect screen;
        localRectToScreen(local, &screen);
        LLUI::getInstance()->screenRectToGL(screen, bounds);
    }
    return true;
}

S32 ALTextView::getPreeditFontSize() const
{
    return mFont ? ll_round(mFont->getLineHeight() * LLUI::getScaleFactor().mV[VY]) : 0;
}

const std::string& ALTextView::getPreeditStringUtf8() const
{
    return mDocument.wholeText();
}

// --- the context menu ------------------------------------------------------------

void ALTextView::showContextMenu(S32 x, S32 y)
{
    if (mContextMenuFile.empty() || !LLMenuGL::sMenuContainer)
    {
        return;
    }
    LLContextMenu* menu = mContextMenuHandle.get();
    if (!menu)
    {
        // The menu's actions name commands, and come back to whichever view
        // showed it; the view may be gone by then, so they hold a handle.
        const LLHandle<ALTextView>                          self = getDerivedHandle<ALTextView>();
        LLUICtrl::CommitCallbackRegistry::ScopedRegistrar   commit;
        LLUICtrl::EnableCallbackRegistry::ScopedRegistrar   enable;
        commit.add("TextView.Perform", [self](LLUICtrl*, const LLSD& param) {
            ALTextView* view = self.get();
            if (std::optional<ALEditorCommand> command = alEditorCommandFromName(param.asStringRef()); view && command)
            {
                view->perform(*command);
            }
        });
        enable.add("TextView.Enable", [self](LLUICtrl*, const LLSD& param) {
            ALTextView* view = self.get();
            if (std::optional<ALEditorCommand> command = alEditorCommandFromName(param.asStringRef()); view && command)
            {
                return view->canPerform(*command);
            }
            return false;
        });
        menu = LLUICtrlFactory::createFromFile<LLContextMenu>(mContextMenuFile, LLMenuGL::sMenuContainer,
                                                              LLMenuHolderGL::child_registry_t::instance());
        if (!menu)
        {
            LL_WARNS() << "No context menu from " << mContextMenuFile << " for " << getName() << LL_ENDL;
            return;
        }
        mContextMenuHandle = menu->getHandle();
    }
    gEditMenuHandler = this;
    // The dictionary's items, where the menu has them: the suggestions
    // show themselves, the rest are shown here.
    const bool misspelled = getSpellCheck() && !mSuggestedFor.empty();
    menu->setItemVisible("Suggestion Separator", misspelled && !mSuggestions.empty());
    menu->setItemVisible("Add to Dictionary", misspelled);
    menu->setItemVisible("Add to Ignore", misspelled);
    menu->setItemVisible("Spellcheck Separator", misspelled);
    S32 screen_x, screen_y;
    localPointToScreen(x, y, &screen_x, &screen_y);
    menu->show(screen_x, screen_y, this);
}

bool ALTextView::showUrlMenu(S32 x, S32 y, const std::string& url)
{
    LLUrlMatch match;
    if (!LLMenuGL::sMenuContainer || !LLUrlRegistry::instance().findUrl(url, match) || match.getMenuName().empty())
    {
        return false;
    }
    // The actions the registry's menus name, each over this URL -- by
    // value, the menu outliving whoever asked for it.
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar registrar;
    registrar.add("Url.Open", [url](LLUICtrl*, const LLSD&) { LLUrlAction::openURL(url); });
    registrar.add("Url.OpenInternal", [url](LLUICtrl*, const LLSD&) { LLUrlAction::openURLInternal(url); });
    registrar.add("Url.OpenExternal", [url](LLUICtrl*, const LLSD&) { LLUrlAction::openURLExternal(url); });
    registrar.add("Url.Execute", [url](LLUICtrl*, const LLSD&) { LLUrlAction::executeSLURL(url, true); });
    registrar.add("Url.Block", [url](LLUICtrl*, const LLSD&) { LLUrlAction::blockObject(url); });
    registrar.add("Url.Unblock", [url](LLUICtrl*, const LLSD&) { LLUrlAction::unblockObject(url); });
    registrar.add("Url.Teleport", [url](LLUICtrl*, const LLSD&) { LLUrlAction::teleportToLocation(url); });
    registrar.add("Url.ShowProfile", [url](LLUICtrl*, const LLSD&) { LLUrlAction::showProfile(url); });
    registrar.add("Url.AddFriend", [url](LLUICtrl*, const LLSD&) { LLUrlAction::addFriend(url); });
    registrar.add("Url.RemoveFriend", [url](LLUICtrl*, const LLSD&) { LLUrlAction::removeFriend(url); });
    registrar.add("Url.ReportAbuse", [url](LLUICtrl*, const LLSD&) { LLUrlAction::reportAbuse(url); });
    registrar.add("Url.ReportAbuseObj", [url](LLUICtrl*, const LLSD&) { LLUrlAction::reportAbuseObj(url); });
    registrar.add("Url.SendIM", [url](LLUICtrl*, const LLSD&) { LLUrlAction::sendIM(url); });
    registrar.add("Url.ZoomInObject", [url](LLUICtrl*, const LLSD&) { LLUrlAction::zoomInObject(url); });
    registrar.add("Url.ShowOnMap", [url](LLUICtrl*, const LLSD&) { LLUrlAction::showLocationOnMap(url); });
    registrar.add("Url.ShowParcelOnMap", [url](LLUICtrl*, const LLSD&) { LLUrlAction::showParcelOnMap(url); });
    registrar.add("Url.CopyLabel", [url](LLUICtrl*, const LLSD&) { LLUrlAction::copyLabelToClipboard(url); });
    registrar.add("Url.CopyUrl", [url](LLUICtrl*, const LLSD&) { LLUrlAction::copyURLToClipboard(url); });
    registrar.add("Url.CopyUUID", [url](LLUICtrl*, const LLSD&) { LLUrlAction::copyUUIDToClipboard(url); });
    if (LLContextMenu* old = mUrlMenuHandle.get())
    {
        old->die();
        mUrlMenuHandle.markDead();
    }
    LLContextMenu* menu = LLUICtrlFactory::createFromFile<LLContextMenu>(match.getMenuName(), LLMenuGL::sMenuContainer,
                                                                          LLMenuHolderGL::child_registry_t::instance());
    if (!menu)
    {
        return false;
    }
    mUrlMenuHandle = menu->getHandle();
    // Whether the agent is a friend, the object blocked or near: what
    // the viewer installed says.
    LLUrlAction::adjustMenu(menu, url);
    S32 screen_x, screen_y;
    localPointToScreen(x, y, &screen_x, &screen_y);
    menu->show(screen_x, screen_y, this);
    return true;
}

// --- find and replace ------------------------------------------------------------

void ALTextView::showFind(bool with_replace)
{
    if (!mFindBar)
    {
        ALFindBar::Params p(LLUICtrlFactory::getDefaultParams<ALFindBar>());
        p.name = "find_bar";
        p.rect = LLRect(0, 30, 200, 0);
        p.follows.flags(FOLLOWS_TOP | FOLLOWS_RIGHT);
        mFindBar = LLUICtrlFactory::create<ALFindBar>(p);
        addChild(mFindBar);
        mFindBar->onChanged([this]() { refreshFind(); });
        mFindBar->onNext([this]() { findNext(true); });
        mFindBar->onPrevious([this]() { findNext(false); });
        mFindBar->onReplace([this]() { replaceMatch(); });
        mFindBar->onReplaceAll([this]() { replaceAllMatches(); });
        mFindBar->onClose([this]() { hideFind(); });
    }
    // Seeded with what is selected, when that is a line's worth or less.
    const ALTextRange sel = selection().normalised();
    if (!sel.empty() && sel.begin.line == sel.end.line && sel.end.column - sel.begin.column < 200)
    {
        mFindBar->setQuery(mDocument.text(sel));
    }
    mFindBar->setReplaceAllowed(!mReadOnly);
    mFindBar->setReplaceShown(with_replace);
    mFindBar->setVisible(true);
    placeFindBar();
    refreshFind();
    mFindBar->focusQuery();
}

void ALTextView::hideFind()
{
    if (!findShown())
    {
        return;
    }
    mFindBar->setVisible(false);
    mMatches.clear();
    mMatch = -1;
    setFocus(true);
}

bool ALTextView::findShown() const
{
    return mFindBar && mFindBar->getVisible();
}

void ALTextView::placeFindBar()
{
    if (!mFindBar)
    {
        return;
    }
    const LLRect local  = getLocalRect();
    const LLRect text   = textRect();
    const S32    width  = llclamp(FIND_BAR_WIDTH, 120, llmax(120, text.getWidth() - 12));
    const S32    height = mFindBar->wantedHeight();
    const S32    right  = text.mRight - 6;
    mFindBar->setShape(LLRect(right - width, local.mTop - 4, right, local.mTop - 4 - height));
    mFindBar->setColors(backgroundColor(), textColor());
}

void ALTextView::refreshFind()
{
    if (!findShown())
    {
        return;
    }
    if (mFindBar->inSelection())
    {
        if (!mFindInSelection)
        {
            mFindScope       = selection().normalised();
            mFindInSelection = true;
        }
    }
    else
    {
        mFindInSelection = false;
    }
    mMatches = ALTextSearch::matches(mDocument, mFindBar->query(), mFindBar->options(), mFindInSelection ? &mFindScope : nullptr, &mFindError);
    // The current one is the match the selection is.
    mMatch                = -1;
    const ALTextRange sel = selection().normalised();
    for (size_t i = 0; i < mMatches.size(); ++i)
    {
        if (mMatches[i] == sel)
        {
            mMatch = static_cast<S32>(i);
            break;
        }
    }
    mFindBar->setCount(mMatch, static_cast<S32>(mMatches.size()), mFindError);
}

bool ALTextView::findNext(bool forward)
{
    if (!findShown())
    {
        showFind(false);
    }
    if (mMatches.empty())
    {
        return false;
    }
    const ALTextRange sel  = selection().normalised();
    const ALTextPos   from = hasSelection() ? (forward ? sel.end : sel.begin) : mCaret;
    const S32         index = ALTextSearch::nearest(mMatches, from, forward);
    if (index < 0)
    {
        return false;
    }
    mMatch = index;
    setSelection(mMatches[index]);
    mFindBar->setCount(mMatch, static_cast<S32>(mMatches.size()), mFindError);
    return true;
}

bool ALTextView::replaceMatch()
{
    if (mReadOnly || !findShown())
    {
        return false;
    }
    if (mMatch < 0 || mMatch >= static_cast<S32>(mMatches.size()) || mMatches[mMatch] != selection().normalised())
    {
        return findNext(true);
    }
    const ALTextRange match = mMatches[mMatch];
    const std::string with  = ALTextSearch::replacement(mDocument, match, mFindBar->query(), mFindBar->options(), mFindBar->replacement());
    setSelection(match);
    insertText(with);
    findNext(true);
    return true;
}

S32 ALTextView::replaceAllMatches()
{
    if (mReadOnly || !findShown() || mMatches.empty())
    {
        return 0;
    }
    std::vector<std::pair<ALTextRange, std::string>> edits;
    edits.reserve(mMatches.size());
    for (const ALTextRange& match : mMatches)
    {
        edits.emplace_back(match, ALTextSearch::replacement(mDocument, match, mFindBar->query(), mFindBar->options(), mFindBar->replacement()));
    }
    const S32 count = static_cast<S32>(edits.size());
    return replaceAll(std::move(edits)) ? count : 0;
}

// --- the bars ---------------------------------------------------------------------------

LLRect ALTextView::rulerRect() const
{
    if (mScrollMap || !mNeedV)
    {
        return LLRect();
    }
    const LLRect local = bodyRect();
    return LLRect(local.mRight - RULER_W, local.mTop, local.mRight, local.mBottom);
}

LLRect ALTextView::hBarRect() const
{
    if (!mNeedH)
    {
        return LLRect();
    }
    const LLRect text = textRect();
    return LLRect(text.mLeft, text.mBottom + HBAR_H, text.mRight, text.mBottom);
}

LLRect ALTextView::vThumb(const LLRect& track)
{
    const S32 total   = llmax(1, mLayout.totalHeight());
    const S32 page    = llmax(1, textRect().getHeight());
    const S32 track_h = llmax(1, track.getHeight());
    const S32 thumb_h = llclamp(track_h * page / total, llmin(THUMB_MIN, track_h), track_h);
    const S32 range   = llmax(1, total - page);
    const S32 travel  = track_h - thumb_h;
    const S32 top     = track.mTop - static_cast<S32>(static_cast<F32>(mScrollY) / static_cast<F32>(range) * static_cast<F32>(travel));
    return LLRect(track.mLeft + 2, top, track.mRight - 2, top - thumb_h);
}

LLRect ALTextView::hThumb(const LLRect& track)
{
    const S32 content = llmax(1, static_cast<S32>(ceilf(mLayout.contentWidth())) + H_MARGIN);
    const S32 width   = llmax(1, textRect().getWidth());
    const S32 track_w = llmax(1, track.getWidth());
    const S32 thumb_w = llclamp(track_w * width / content, llmin(THUMB_MIN, track_w), track_w);
    const S32 range   = llmax(1, content - width);
    const S32 travel  = track_w - thumb_w;
    const S32 left    = track.mLeft + static_cast<S32>(mScrollX / static_cast<F32>(range) * static_cast<F32>(travel));
    return LLRect(left, track.mTop - 2, left + thumb_w, track.mBottom + 2);
}

F32 ALTextView::barAlpha() const
{
    const F32 since = mBarShown.getElapsedTimeF32();
    if (since < BAR_HOLD)
    {
        return 1.f;
    }
    return llclamp(1.f - (since - BAR_HOLD) / BAR_FADE, 0.f, 1.f);
}

void ALTextView::scrollToRulerY(S32 y, S32 offset)
{
    const LLRect track = rulerRect();
    if (track.isEmpty())
    {
        return;
    }
    const LLRect thumb  = vThumb(track);
    const S32    travel = llmax(1, track.getHeight() - thumb.getHeight());
    const S32    total  = llmax(1, mLayout.totalHeight());
    const S32    page   = llmax(1, textRect().getHeight());
    const S32    top    = y - offset;
    setScrollY(static_cast<S32>(static_cast<F32>(track.mTop - top) / static_cast<F32>(travel) * static_cast<F32>(llmax(0, total - page))));
}

void ALTextView::scrollToBarX(S32 x, S32 offset)
{
    const LLRect track = hBarRect();
    if (track.isEmpty())
    {
        return;
    }
    const LLRect thumb   = hThumb(track);
    const S32    travel  = llmax(1, track.getWidth() - thumb.getWidth());
    const S32    content = llmax(1, static_cast<S32>(ceilf(mLayout.contentWidth())) + H_MARGIN);
    const S32    width   = llmax(1, textRect().getWidth());
    const S32    left    = x - offset;
    setScrollX(static_cast<F32>(left - track.mLeft) / static_cast<F32>(travel) * static_cast<F32>(llmax(0, content - width)));
}

void ALTextView::drawBars(F32 alpha)
{
    const LLColor4& ink   = textColor();
    const F32       shown = barAlpha() * alpha;
    // The ruler down the right: the caret and the marks always, the
    // thumb while wanted.
    const LLRect ruler = rulerRect();
    if (ruler.notEmpty())
    {
        gl_rect_2d(ruler, ink % (0.04f * alpha));
        const S32 total   = llmax(1, mLayout.totalHeight());
        const S32 track_h = llmax(1, ruler.getHeight());
        const auto yOf    = [&](S32 line) { return ruler.mTop - static_cast<S32>(static_cast<F32>(mLayout.lineTop(line)) / static_cast<F32>(total) * static_cast<F32>(track_h)); };
        const S32 middle  = ruler.mLeft + RULER_W / 2;
        const S32 count   = mDocument.lineCount();
        LLColor4  mark;
        for (S32 line = 0; line < count; ++line)
        {
            if (mapMark(line, mark))
            {
                const S32 y = yOf(line);
                gl_rect_2d(middle, y, ruler.mRight - 2, y - 2, mark % alpha);
            }
        }
        for (const ALTextRange& match : mMatches)
        {
            const S32 y = yOf(match.begin.line);
            gl_rect_2d(ruler.mLeft + 2, y, middle, y - 2, mFindMatchColor.get() % alpha);
        }
        // The blip: where the caret is.
        const S32 caret_y = yOf(mCaret.line);
        gl_rect_2d(ruler.mLeft + 2, caret_y, ruler.mRight - 2, caret_y - 2, mCursorColor.get() % alpha);
        if (shown > 0.f)
        {
            gl_rect_2d(vThumb(ruler), ink % (0.35f * shown));
        }
    }
    const LLRect bar = hBarRect();
    if (bar.notEmpty() && shown > 0.f)
    {
        gl_rect_2d(hThumb(bar), ink % (0.35f * shown));
    }
}

// --- the scrollbar as a map --------------------------------------------------------

void ALTextView::setScrollMap(bool map)
{
    mScrollMap = map;
    syncScrollbar();
    placeFindBar();
}

void ALTextView::setScrollMapWidth(S32 width)
{
    mScrollMapWidth = llmax(20, width);
    syncScrollbar();
    placeFindBar();
}

void ALTextView::setScrollMapOnLeft(bool left)
{
    mScrollMapLeft = left;
    syncScrollbar();
    placeFindBar();
}

LLRect ALTextView::mapRect() const
{
    if (!mScrollMap)
    {
        return LLRect();
    }
    const LLRect local = bodyRect();
    return mScrollMapLeft ? LLRect(local.mLeft, local.mTop, local.mLeft + mScrollMapWidth, local.mBottom)
                          : LLRect(local.mRight - mScrollMapWidth, local.mTop, local.mRight, local.mBottom);
}

S32 ALTextView::leftEdge() const
{
    return getLocalRect().mLeft + (mScrollMap && mScrollMapLeft ? mScrollMapWidth : 0);
}

S32 ALTextView::mapScroll(const LLRect& map)
{
    // The lines the map shows, and how far its window is down them: as
    // far, in proportion, as the text is scrolled.
    mMapLines.clear();
    const S32 count = mDocument.lineCount();
    for (S32 line = 0; line < count; ++line)
    {
        if (!mLayout.hidden(line))
        {
            mMapLines.push_back(line);
        }
    }
    const S32 doc_h = static_cast<S32>(mMapLines.size()) * MAP_LINE_H;
    const S32 map_h = map.getHeight() - 2 * MAP_PAD;
    if (doc_h <= map_h)
    {
        return 0;
    }
    const S32 total = mLayout.totalHeight();
    const S32 page  = llmax(1, textRect().getHeight());
    const F32 how   = total > page ? static_cast<F32>(mScrollY) / static_cast<F32>(total - page) : 0.f;
    return static_cast<S32>(llclamp(how, 0.f, 1.f) * static_cast<F32>(doc_h - map_h));
}

S32 ALTextView::mapLineAt(S32 y)
{
    const LLRect map = mapRect();
    if (map.isEmpty() || mDocument.lineCount() == 0)
    {
        return -1;
    }
    const S32 scroll  = mapScroll(map);
    const S32 ordinal = llclamp((map.mTop - MAP_PAD - y + scroll) / MAP_LINE_H, 0, static_cast<S32>(mMapLines.size()) - 1);
    return mMapLines.empty() ? -1 : mMapLines[ordinal];
}

void ALTextView::drawMapPreview(F32 alpha)
{
    const LLRect map  = mapRect();
    const S32    line = mapLineAt(mMapHoverY);
    if (map.isEmpty() || line < 0 || !mFont)
    {
        return;
    }
    // Seven lines around the one under the mouse, in a box beside the
    // map, on the view's ground a shade towards its ink, the line under
    // the mouse washed as the caret's line is; kept on screen.
    const S32 PAD    = 6;
    const S32 GUTTER = 8;
    const S32 count  = mDocument.lineCount();
    const S32 first  = llmax(0, line - 3);
    const S32 last   = llmin(count - 1, line + 3);
    const S32 row_h  = mLayout.rowHeight();
    const S32 rows   = last - first + 1;
    const LLRect local = getLocalRect();
    const S32    width = llmin(560, llmax(160, local.getWidth() - map.getWidth() - 3 * PAD));
    const S32    height = rows * row_h + 2 * PAD;
    S32          top    = llmin(local.mTop - PAD, mMapHoverY + height / 2);
    top                 = llmax(top, local.mBottom + PAD + height);
    const S32    right  = map.mLeft - PAD;
    const LLRect box(right - width, top, right, top - height);
    const LLColor4& bg    = backgroundColor();
    const LLColor4& ink   = textColor();
    const LLColor4  faint = ALSurface::shade(bg, ink, 0.45f) % alpha;
    const LLColor4  wash  = ALSurface::shade(bg, ink, 0.14f) % alpha;
    ALSurface::draw(box, bg, ink, alpha);
    LLLocalClipRect clip(LLRect(box.mLeft + 1, box.mTop - 1, box.mRight - 1, box.mBottom + 1));
    // The numbers take the room the widest needs.
    const std::string widest  = std::to_string(last + 1);
    const S32         numbers = static_cast<S32>(mFont->getWidth(widest)) + GUTTER;
    S32               y       = box.mTop - PAD;
    for (S32 l = first; l <= last; ++l, y -= row_h)
    {
        if (l == line)
        {
            gl_rect_2d(LLRect(box.mLeft + 1, y, box.mRight - 1, y - row_h), wash, true);
        }
        const S32 baseline = y - row_h + static_cast<S32>(mFont->getDescenderHeight()) + 1;
        mFont->renderUTF8(std::to_string(l + 1), 0, static_cast<F32>(box.mLeft + PAD + numbers - GUTTER), static_cast<F32>(baseline),
                          faint, LLFontGL::RIGHT, LLFontGL::BOTTOM, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
        // The line in its colours, token by token, tabs as spaces.
        const std::string&                text   = mDocument.line(l);
        const std::vector<ALSyntaxToken>& tokens = mHighlighter.tokens(l);
        F32                               x      = static_cast<F32>(box.mLeft + PAD + numbers);
        const F32                         limit  = static_cast<F32>(box.mRight - PAD);
        auto                              run    = [&](S32 begin, S32 end, const LLColor4& color) {
            if (begin >= end || x >= limit)
            {
                return;
            }
            std::string piece = text.substr(static_cast<size_t>(begin), static_cast<size_t>(end - begin));
            for (size_t at = piece.find('\t'); at != std::string::npos; at = piece.find('\t', at + 4))
            {
                piece.replace(at, 1, "    ");
            }
            F32 right_x = x;
            mFont->renderUTF8(piece, 0, x, static_cast<F32>(baseline), color % alpha, LLFontGL::LEFT, LLFontGL::BOTTOM, LLFontGL::NORMAL,
                              LLFontGL::NO_SHADOW, S32_MAX, static_cast<S32>(limit - x), &right_x, false);
            x = right_x;
        };
        S32 at = 0;
        for (const ALSyntaxToken& token : tokens)
        {
            run(at, token.begin, ink);
            run(token.begin, token.end, token.kind == ALSyntaxKind::Text ? ink : colorForKind(token.kind));
            at = token.end;
        }
        run(at, static_cast<S32>(text.size()), ink);
    }
}

void ALTextView::scrollToMapY(S32 y)
{
    const S32 line = mapLineAt(y);
    if (line < 0)
    {
        return;
    }
    const S32 page = llmax(1, textRect().getHeight());
    setScrollY(mLayout.lineTop(line) - page / 2);
}

void ALTextView::drawMap(F32 alpha)
{
    const LLRect map = mapRect();
    if (map.isEmpty())
    {
        return;
    }
    const LLColor4& bg   = backgroundColor();
    const LLColor4& ink  = textColor();
    LLColor4        base = bg;
    for (S32 i = 0; i < 3; ++i)
    {
        base.mV[i] = bg.mV[i] + (ink.mV[i] - bg.mV[i]) * 0.05f;
    }
    gl_rect_2d(map, base % alpha);

    const S32 scroll = mapScroll(map);
    const S32 rows   = static_cast<S32>(mMapLines.size());
    if (rows == 0)
    {
        return;
    }
    const S32 map_h  = map.getHeight() - 2 * MAP_PAD;
    const S32 first  = scroll / MAP_LINE_H;
    const S32 last   = llmin(rows - 1, (scroll + map_h) / MAP_LINE_H);
    const S32 inner_left  = map.mLeft + MAP_PAD + (mScrollMapLeft ? MAP_MARK_W : 0);
    const S32 inner_right = map.mRight - MAP_PAD - (mScrollMapLeft ? 0 : MAP_MARK_W);
    const S32 mark_left   = mScrollMapLeft ? map.mLeft + 1 : map.mRight - MAP_MARK_W;
    LLLocalClipRect clip(map);
    for (S32 o = first; o <= last; ++o)
    {
        const S32 line  = mMapLines[o];
        const S32 top   = map.mTop - MAP_PAD - (o * MAP_LINE_H - scroll);
        const S32 bottom = top - MAP_LINE_H;
        const std::string&                text   = mDocument.line(line);
        const std::vector<ALSyntaxToken>& tokens = mHighlighter.tokens(line);
        // Each run of glyphs, a rectangle in its kind's ink.
        size_t t   = 0;
        S32    col = 0;
        S32    run_from = -1;
        ALSyntaxKind run_kind = ALSyntaxKind::Text;
        auto flush = [&](S32 to) {
            if (run_from < 0)
            {
                return;
            }
            const S32 x0 = llmin(inner_right, inner_left + run_from * MAP_CHAR_W);
            const S32 x1 = llmin(inner_right, inner_left + to * MAP_CHAR_W);
            if (x1 > x0)
            {
                const LLColor4& kind_ink = run_kind == ALSyntaxKind::Text ? ink : colorForKind(run_kind);
                gl_rect_2d(x0, top, x1, bottom, kind_ink % (alpha * (run_kind == ALSyntaxKind::Text ? 0.45f : 0.7f)));
            }
            run_from = -1;
        };
        for (size_t i = 0; i < text.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if ((c & 0xC0) == 0x80)
            {
                continue;  // the rest of a character
            }
            while (t < tokens.size() && tokens[t].end <= static_cast<S32>(i))
            {
                ++t;
            }
            const ALSyntaxKind kind = t < tokens.size() && tokens[t].begin <= static_cast<S32>(i) ? tokens[t].kind : ALSyntaxKind::Text;
            if (c == '\t')
            {
                flush(col);
                col = (col / mTabWidth + 1) * mTabWidth;
            }
            else if (c == ' ')
            {
                flush(col);
                ++col;
            }
            else
            {
                if (run_from >= 0 && kind != run_kind)
                {
                    flush(col);
                }
                if (run_from < 0)
                {
                    run_from = col;
                    run_kind = kind;
                }
                ++col;
            }
            if (inner_left + col * MAP_CHAR_W > inner_right)
            {
                break;
            }
        }
        flush(col);
        // A mark beside the line, and a match in it.
        LLColor4 mark;
        if (mapMark(line, mark))
        {
            gl_rect_2d(mark_left, top, mark_left + MAP_MARK_W, bottom - 1, mark % alpha);
        }
        if (!mMatches.empty())
        {
            auto found = std::lower_bound(mMatches.begin(), mMatches.end(), line, [](const ALTextRange& m, S32 l) { return m.end.line < l; });
            if (found != mMatches.end() && found->begin.line <= line)
            {
                const S32 tick_left = mScrollMapLeft ? map.mRight - MAP_MARK_W : map.mLeft + 1;
                gl_rect_2d(tick_left, top, tick_left + MAP_MARK_W, bottom - 1, mFindMatchColor.get() % alpha);
            }
        }
        if (line == mCaret.line)
        {
            gl_rect_2d(inner_left, top, inner_right, top - 1, mCursorColor.get() % (alpha * 0.6f));
        }
    }
    // The rows on screen, as a window over the map.
    const S32 page        = llmax(1, textRect().getHeight());
    const S32 top_line    = mLayout.lineAtY(mScrollY);
    const S32 bottom_line = mLayout.lineAtY(mScrollY + page - 1);
    const auto ordinal    = [&](S32 line) { return static_cast<S32>(std::lower_bound(mMapLines.begin(), mMapLines.end(), line) - mMapLines.begin()); };
    const S32 y0 = map.mTop - MAP_PAD - (ordinal(top_line) * MAP_LINE_H - scroll);
    const S32 y1 = map.mTop - MAP_PAD - ((ordinal(bottom_line) + 1) * MAP_LINE_H - scroll);
    gl_rect_2d(map.mLeft, y0, map.mRight, y1, ink % (alpha * 0.12f));
    gl_rect_2d(map.mLeft, y0, map.mRight, y1, ink % (alpha * 0.3f), false);
}

// --- drawing -------------------------------------------------------------------

bool ALTextView::spanOnRow(S32 line, S32 row, const ALTextRange& range_in, F32& x0, F32& x1)
{
    const ALTextRange range = range_in.normalised();
    if (line < range.begin.line || line > range.end.line)
    {
        return false;
    }
    const ALTextLayout::Line& laid = mLayout.line(line);
    if (row < 0 || row >= static_cast<S32>(laid.rows.size()))
    {
        return false;
    }
    const ALTextLayout::Row& r        = laid.rows[row];
    const S32                length   = mDocument.lineLength(line);
    const bool               last_row = (row + 1 == static_cast<S32>(laid.rows.size()));
    const S32                lo       = llmax(range.begin.line < line ? 0 : range.begin.column, r.begin);
    const S32                hi       = llmin(range.end.line > line ? length + 1 : range.end.column, last_row ? length + 1 : r.end);
    if (lo > hi || (lo == hi && !range.empty()))
    {
        return false;
    }
    x0 = mLayout.xOf(line, lo);
    x1 = hi > length ? r.width + 6.f : (hi >= r.end && !last_row ? r.width : mLayout.xOf(line, hi));
    if (x1 <= x0)
    {
        x1 = x0 + 4.f;
    }
    return true;
}

const LLColor4& ALTextView::colorForKind(ALSyntaxKind kind) const
{
    return mKindColors[static_cast<size_t>(kind)].get();
}

// static
std::string ALTextView::kindColorName(std::string_view prefix, ALSyntaxKind kind)
{
    const size_t index = static_cast<size_t>(kind);
    if (index == 0 || index >= static_cast<size_t>(ALSyntaxKind::COUNT))
    {
        return std::string();
    }
    return std::string(prefix) + KIND_COLOR_SUFFIXES[index];
}

const LLColor4& ALTextView::backgroundColor() const
{
    return mReadOnly ? mBgReadOnlyColor.get() : hasFocus() ? mBgFocusColor.get() : mBgColor.get();
}

bool ALTextView::keyboardOnText() const
{
    return gFocusMgr.getKeyboardFocus() == this;
}

void ALTextView::drawRowAt(S32 line, S32 r, F32 left, S32 screen_top, F32 alpha)
{
    if (!mFont || line < 0 || line >= mDocument.lineCount())
    {
        return;
    }
    const ALTextLayout::Line& laid = mLayout.line(line);
    if (r < 0 || r >= static_cast<S32>(laid.rows.size()))
    {
        return;
    }
    const ALTextLayout::Row& row         = laid.rows[static_cast<size_t>(r)];
    const size_t             glyph_count = row.glyphEnd - row.glyphBegin;
    if (!glyph_count)
    {
        return;
    }
    colorRow(line, laid, row, alpha);
    tintRow(line, laid, row, alpha, mColorScratch);
    mFont->renderGlyphs(&laid.placed[row.glyphBegin], mColorScratch.data(), glyph_count, left - row.xStart, static_cast<F32>(screen_top - row.ascent));
}

void ALTextView::colorRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha)
{
    const size_t count = row.glyphEnd - row.glyphBegin;
    mColorScratch.resize(count);
    const LLColor4                    base   = textColor() % alpha;
    const std::vector<ALSyntaxToken>& tokens = mHighlighter.tokens(line);
    size_t                            t      = 0;
    for (size_t k = 0; k < count; ++k)
    {
        const S32 cluster = laid.glyphs[row.glyphBegin + k].cluster;
        while (t < tokens.size() && tokens[t].end <= cluster)
        {
            ++t;
        }
        if (t < tokens.size() && tokens[t].begin <= cluster && tokens[t].kind != ALSyntaxKind::Text)
        {
            mColorScratch[k] = LLColor4U(colorForKind(tokens[t].kind) % alpha);
        }
        else
        {
            mColorScratch[k] = LLColor4U(base);
        }
    }
    // A style's colour over the grammar's.
    for (auto it = firstStyleOn(line); it != mStyles.end() && it->range.begin.line <= line; ++it)
    {
        const Style& style = *it;
        if (!style.color)
        {
            continue;
        }
        const LLColor4U ink(*style.color % alpha);
        const S32       from = style.range.begin.line == line ? style.range.begin.column : 0;
        const S32       to   = style.range.end.line == line ? style.range.end.column : S32_MAX;
        for (size_t k = 0; k < count; ++k)
        {
            const S32 cluster = laid.glyphs[row.glyphBegin + k].cluster;
            if (cluster >= from && cluster < to)
            {
                mColorScratch[k] = ink;
            }
        }
    }
    // A link in its own colour, over whatever the grammar made of it.
    if (!mSubstitutions.empty())
    {
        const LLColor4U link(mLinkColor.get() % alpha);
        auto            it = std::lower_bound(mSubstitutions.begin(), mSubstitutions.end(), ALTextPos(line, row.begin),
                                              [](const Substitution& s, const ALTextPos& p) { return s.range.end <= p; });
        for (; it != mSubstitutions.end() && it->range.begin.line == line && it->range.begin.column < row.end; ++it)
        {
            if (!it->link)
            {
                continue;
            }
            for (size_t k = 0; k < count; ++k)
            {
                const S32 cluster = laid.glyphs[row.glyphBegin + k].cluster;
                if (cluster >= it->range.begin.column && cluster < it->range.end.column)
                {
                    mColorScratch[k] = link;
                }
            }
        }
    }
}

void ALTextView::drawSquiggle(F32 x0, F32 x1, S32 y, const LLColor4& color)
{
    mSquiggleScratch.clear();
    for (F32 x = x0; x <= x1; x += 1.f)
    {
        mSquiggleScratch.emplace_back(x, static_cast<F32>(y) + SQUIGGLE_AMPLITUDE * sinf((x - x0) * (2.f * F_PI / SQUIGGLE_WAVE)));
    }
    if (mSquiggleScratch.size() >= 2)
    {
        gl_polyline_2d(mSquiggleScratch, color, 1.f);
    }
}

void ALTextView::drawLayers(S32 line, const ALTextLayout::Line& laid, S32 r, const LLRect& text, S32 row_top, F32 left, F32 alpha)
{
    const ALTextLayout::Row& row        = laid.rows[static_cast<size_t>(r)];
    const S32                row_h      = row.textHeight;
    // The text's band at the bottom of the row; a box has the whole.
    const S32                screen_top = row_top - row.textTop();
    // The links underlined: the one the mouse is on, and the ones that
    // always are.
    if (!mSubstitutions.empty())
    {
        auto it = std::lower_bound(mSubstitutions.begin(), mSubstitutions.end(), ALTextPos(line, row.begin),
                                   [](const Substitution& s, const ALTextPos& p) { return s.range.end <= p; });
        for (; it != mSubstitutions.end() && it->range.begin.line == line && it->range.begin.column < row.end; ++it)
        {
            const bool hovered = mHoverLink == static_cast<S32>(it - mSubstitutions.begin());
            if (!it->link || it->underline == Substitution::Underline::Never || (it->underline == Substitution::Underline::Hover && !hovered))
            {
                continue;
            }
            F32 x0, x1;
            if (spanOnRow(line, r, it->range, x0, x1))
            {
                const S32 y = screen_top - row.ascent - 2;
                gl_rect_2d(static_cast<S32>(left + x0), y + 1, static_cast<S32>(left + x1), y, mLinkColor.get() % alpha);
            }
        }
    }
    // The styles that underline, in the style's colour or the text's.
    for (auto it = firstStyleOn(line); it != mStyles.end() && it->range.begin.line <= line; ++it)
    {
        const Style& style = *it;
        if (!(style.flags & LLFontGL::UNDERLINE))
        {
            continue;
        }
        F32 x0, x1;
        if (spanOnRow(line, r, style.range, x0, x1))
        {
            const S32 y = screen_top - row.ascent - 2;
            gl_rect_2d(static_cast<S32>(left + x0), y + 1, static_cast<S32>(left + x1), y, (style.color ? *style.color : textColor()) % alpha);
        }
    }
    // The atoms on the row, each in its box: an image drawn there, or a
    // view put there.
    if (!mAtoms.empty())
    {
        for (size_t k = row.glyphBegin; k < row.glyphEnd; ++k)
        {
            const ALTextLayout::Glyph& glyph = laid.glyphs[k];
            if (!isAtomId(glyph.substitution))
            {
                continue;
            }
            const size_t index = static_cast<size_t>(glyph.substitution - ATOM_ID_BASE);
            if (index >= mAtoms.size())
            {
                continue;
            }
            const Atom& atom = mAtoms[index];
            if (atom.view || !atom.image)
            {
                // A view was put in its box before the rows were drawn.
                continue;
            }
            const S32    x0 = static_cast<S32>(left + glyph.pen - row.xStart);
            const S32    x1 = static_cast<S32>(left + glyph.pen - row.xStart + glyph.advance);
            const LLRect box(x0, row_top, x1, row_top - row.height);
            atom.image->draw(box, LLColor4::white % alpha);
        }
    }
    // The words the dictionary lacks, squiggled; the one being typed is
    // left alone for a moment.
    if (getSpellCheck())
    {
        const bool settling = keyboardOnText() && mSpellTimer.getElapsedTimeF32() < SPELL_SETTLE_SECONDS;
        for (const auto& [begin, end] : misspellings(line))
        {
            if (settling && mCaret.line == line && begin <= mCaret.column && mCaret.column <= end)
            {
                continue;
            }
            F32 x0, x1;
            if (spanOnRow(line, r, ALTextRange(ALTextPos(line, begin), ALTextPos(line, end)), x0, x1))
            {
                drawSquiggle(left + x0, left + x1, screen_top - row_h + 2, mSpellErrorColor.get() % alpha);
            }
        }
    }
}

void ALTextView::drawPreedit(S32 line, const ALTextLayout::Row& row, S32 screen_top, F32 left, F32 alpha)
{
    static LLUICachedControl<S32> marker_thickness("UIPreeditMarkerThickness", 1);
    static LLUICachedControl<S32> standout_thickness("UIPreeditStandoutThickness", 2);
    const S32      row_h = mLayout.rowHeight();
    const LLColor4 ink   = textColor() % alpha;
    S32            from  = mPreeditBegin.column;
    for (size_t i = 0; i < mPreeditSegmentEnds.size(); ++i)
    {
        const S32 to = mPreeditBegin.column + mPreeditSegmentEnds[i];
        const S32 lo = llmax(from, row.begin);
        const S32 hi = llmin(to, row.end);
        from         = to;
        if (lo >= hi)
        {
            continue;
        }
        const F32 x0        = mLayout.xOf(line, lo);
        const F32 x1        = mLayout.xOf(line, hi);
        const S32 thickness = llmax(1, static_cast<S32>(i < mPreeditStandouts.size() && mPreeditStandouts[i] ? standout_thickness : marker_thickness));
        const S32 y         = screen_top - row_h + 1;
        gl_rect_2d(static_cast<S32>(left + x0), y + thickness, static_cast<S32>(left + x1), y, ink);
    }
}

void ALTextView::drawRows(const LLRect& text)
{
    const S32 row_h = mLayout.rowHeight();
    if (row_h <= 0 || !mFont)
    {
        return;
    }
    const F32  alpha       = getDrawContext().mAlpha;
    const S32  count       = mDocument.lineCount();
    const S32  bottom_y    = mScrollY + text.getHeight();
    const bool show_caret  = keyboardOnText() && gFocusMgr.getAppHasFocus() && !mReadOnly;
    const F32  blink       = mBlink.getElapsedTimeF32();
    const bool caret_on    = show_caret && (blink < BLINK_DELAY || (static_cast<S32>(blink * 2.f) & 1));
    const ALTextRange sel  = selection().normalised();
    const bool has_sel     = !sel.empty();
    S32        caret_row   = 0;
    const F32  caret_x     = mLayout.xOf(mCaret.line, mCaret.column, &caret_row);
    const F32  space       = mLayout.xOf(0, 0) + 6.f;  // what a selected line end is drawn as

    for (S32 line = mLayout.lineAtY(mScrollY); line < count; ++line)
    {
        if (mLayout.hidden(line))
        {
            continue;
        }
        const ALTextLayout::Line& laid = mLayout.line(line);
        const S32                 top  = mLayout.lineTop(line);
        if (top >= bottom_y)
        {
            break;
        }
        const S32 length = mDocument.lineLength(line);
        for (size_t r = 0; r < laid.rows.size(); ++r)
        {
            const ALTextLayout::Row& row     = laid.rows[r];
            const S32                row_top = top + row.top;
            if (row_top + row.height <= mScrollY)
            {
                continue;
            }
            if (row_top >= bottom_y)
            {
                break;
            }
            // The row's top on the screen, and the top of its text's band,
            // which is the row's bottom part where a box made it taller.
            const S32 row_screen_top = text.mTop - (row_top - mScrollY);
            const S32 screen_top     = row_screen_top - row.textTop();
            const F32 left           = static_cast<F32>(text.mLeft) - mScrollX;

            // The selection behind the row.
            if (has_sel && sel.begin.line <= line && line <= sel.end.line)
            {
                const S32 sel_begin = sel.begin.line < line ? 0 : sel.begin.column;
                const S32 sel_end   = sel.end.line > line ? length + 1 : sel.end.column;
                const S32 lo        = llmax(sel_begin, row.begin);
                const bool last_row = (r + 1 == laid.rows.size());
                const S32 hi        = llmin(sel_end, last_row ? length + 1 : row.end);
                if (lo < hi)
                {
                    const F32 x0 = mLayout.xOf(line, lo) ;
                    const F32 x1 = hi > length ? row.width + space : (hi >= row.end && !last_row ? row.width : mLayout.xOf(line, hi));
                    gl_rect_2d(static_cast<S32>(left + x0), row_screen_top, static_cast<S32>(left + x1), row_screen_top - row.height, mSelectionColor.get() % alpha);
                }
            }

            // What the find bar found, behind the row.
            if (!mMatches.empty())
            {
                auto first = std::lower_bound(mMatches.begin(), mMatches.end(), line, [](const ALTextRange& m, S32 l) { return m.end.line < l; });
                for (auto it = first; it != mMatches.end() && it->begin.line <= line; ++it)
                {
                    F32 x0, x1;
                    if (spanOnRow(line, static_cast<S32>(r), *it, x0, x1))
                    {
                        gl_rect_2d(static_cast<S32>(left + x0), row_screen_top, static_cast<S32>(left + x1), row_screen_top - row.height, mFindMatchColor.get() % alpha);
                    }
                }
            }

            // The glyphs.
            const size_t glyph_count = row.glyphEnd - row.glyphBegin;
            if (glyph_count)
            {
                colorRow(line, laid, row, alpha);
                tintRow(line, laid, row, alpha, mColorScratch);
                mFont->renderGlyphs(&laid.placed[row.glyphBegin], mColorScratch.data(), glyph_count,
                                    left - row.xStart, static_cast<F32>(screen_top - row.ascent));
            }

            if (hasPreedit() && line == mPreeditBegin.line)
            {
                drawPreedit(line, row, screen_top, left, alpha);
            }

            drawLayers(line, laid, static_cast<S32>(r), text, row_screen_top, left, alpha);
            drawRowExtras(line, static_cast<S32>(r), text, screen_top, left, alpha);

            // The caret.
            if (caret_on && line == mCaret.line && static_cast<S32>(r) == caret_row)
            {
                const S32 x = static_cast<S32>(left + caret_x);
                if (mModal && !mModal->inserting())
                {
                    // A block over the cluster the caret is on, as a modal
                    // editor's is; a space's width past the line's end.
                    const ALTextPos next  = mDocument.nextCluster(mCaret);
                    const F32       cell  = llmax(4.f, mLayout.columnWidth());
                    F32             right = next.line == mCaret.line && next != mCaret ? mLayout.xOf(mCaret.line, next.column) : caret_x + cell;
                    if (right <= caret_x)
                    {
                        right = caret_x + cell;
                    }
                    gl_rect_2d(x, row_screen_top, static_cast<S32>(left + right), row_screen_top - row.height, mCursorColor.get() % (0.55f * alpha));
                }
                else
                {
                    gl_rect_2d(x, row_screen_top, x + CARET_WIDTH, row_screen_top - row.height, mCursorColor.get() % alpha);
                }
            }
        }
    }
}

void ALTextView::draw()
{
    syncScrollbar();
    const F32 alpha = getDrawContext().mAlpha;
    if (mBgVisible)
    {
        gl_rect_2d(getLocalRect(), backgroundColor() % alpha);
    }
    const LLRect text = textRect();
    drawBeforeRows(text);
    placeAtomViews();
    {
        LLLocalClipRect clip(text);
        drawRows(text);
        drawAfterRows(text);
        if (mDocument.empty() && !mPlaceholder.empty() && mFont)
        {
            // What will be here, said where the first line would be, in
            // the ink dimmed.
            const F32 baseline = static_cast<F32>(text.mTop - llround(mFont->getAscenderHeight()));
            mFont->renderUTF8(mPlaceholder, 0, static_cast<F32>(text.mLeft), baseline, lerp(backgroundColor(), textColor(), 0.5f) % alpha, LLFontGL::LEFT,
                              LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX, text.getWidth(), nullptr, true);
        }
    }
    if (mScrollMap)
    {
        drawMap(alpha);
    }
    drawBars(alpha);
    if (mModal)
    {
        drawBand(alpha);
    }
    if (findShown())
    {
        placeFindBar();
    }
    LLUICtrl::draw();
    if (mMapHoverY >= 0 && !mDraggingMap)
    {
        drawMapPreview(alpha);
    }
}

void ALTextView::drawBand(F32 alpha)
{
    // The keymap's line under the text, where vim has its command line:
    // what is being typed after : or /, with a block caret at its end;
    // else what the keymap last said, an error in the error colour; else
    // the mode and what is pending. On a ground a shade off the text's,
    // under a hairline, so that it reads as a strip of its own.
    const LLRect    local = getLocalRect();
    const LLRect    band(local.mLeft, local.mBottom + bandHeight(), local.mRight, local.mBottom);
    const LLColor4& paper = backgroundColor();
    const LLColor4& ink   = textColor();
    gl_rect_2d(band, ALSurface::ground(paper, ink) % alpha);
    gl_rect_2d(band.mLeft, band.mTop, band.mRight, band.mTop - 1, ALSurface::frame(ink, alpha));
    const LLFontGL* font = getFont();
    if (!font)
    {
        return;
    }
    std::string line;
    S32         caret    = 0;
    const bool  typing   = mModal->typingLine(line, caret);
    std::string shown    = typing ? line : mModal->message();
    LLColor4    colour   = typing || !mModal->messageIsError() ? ink : mSpellErrorColor.get();
    if (!typing && shown.empty())
    {
        shown  = mModal->status();
        colour = lerp(paper, ink, 0.8f);
    }
    const S32 x = local.mLeft + mHPad + 2;
    const S32 y = band.mBottom + 2 + static_cast<S32>(font->getDescenderHeight());
    font->renderUTF8(shown, 0, static_cast<F32>(x), static_cast<F32>(y), colour % alpha, LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                     S32_MAX, band.getWidth() - mHPad * 2 - 4, nullptr, true);
    // The block caret at the caret's byte, blinking as the text's does.
    const F32 blink = mBlink.getElapsedTimeF32();
    if (typing && keyboardOnText() && gFocusMgr.getAppHasFocus() && (blink < BLINK_DELAY || (static_cast<S32>(blink * 2.f) & 1)))
    {
        const S32 at    = font->getWidth(shown.substr(0, static_cast<size_t>(llclamp(caret, 0, static_cast<S32>(shown.size())))));
        const S32 width = llmax(2, static_cast<S32>(font->getWidth(" ")));
        gl_rect_2d(x + at, band.mTop - 2, x + at + width, band.mBottom + 2, ink % (0.6f * alpha));
    }
    // The keymap's row of choices, over the band on the text's last row
    // -- vim's wildmenu -- the one on the line set in the ink with the
    // ink for its ground, and the row slid left so that it shows.
    std::vector<std::string> items;
    S32                      chosen = -1;
    if (typing && mModal->menu(items, chosen) && !items.empty())
    {
        const S32    gap = font->getWidth("  ");
        const LLRect row(local.mLeft, band.mTop + mLayout.rowHeight() + 2, local.mRight, band.mTop);
        gl_rect_2d(row, lerp(paper, ink, 0.12f) % alpha);
        std::vector<S32> lefts;
        S32              at = 0;
        for (const std::string& item : items)
        {
            lefts.push_back(at);
            at += font->getWidth(item) + gap;
        }
        S32 slide = 0;
        if (chosen >= 0 && chosen < static_cast<S32>(items.size()))
        {
            const S32 right = x + lefts[chosen] + font->getWidth(items[chosen]) + gap / 2;
            slide           = llmax(0, right - row.mRight);
        }
        const S32 baseline = row.mBottom + 1 + static_cast<S32>(font->getDescenderHeight());
        for (size_t i = 0; i < items.size(); ++i)
        {
            const S32 left = x + lefts[i] - slide;
            if (left >= row.mRight)
            {
                break;
            }
            const bool on = static_cast<S32>(i) == chosen;
            if (on)
            {
                gl_rect_2d(left - gap / 4, row.mTop - 1, left + font->getWidth(items[i]) + gap / 4, row.mBottom + 1, ink % (0.85f * alpha));
            }
            font->renderUTF8(items[i], 0, static_cast<F32>(left), static_cast<F32>(baseline), (on ? paper : ink) % alpha, LLFontGL::LEFT, LLFontGL::BASELINE,
                             LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX, row.mRight - left, nullptr, false);
        }
    }
}

// --- input ---------------------------------------------------------------------

bool ALTextView::handleKeyHere(KEY key, MASK mask)
{
    if (key == KEY_ESCAPE && mask == MASK_NONE && findShown())
    {
        hideFind();
        return true;
    }
    // The keyboard among the atoms' views: from one of them, Tab moves
    // on and Escape comes back to the text; from a read-only text, which
    // has no tab of its own, Tab goes into them, and from any text F6.
    if (((key == KEY_TAB || key == KEY_F6) && (mask & ~MASK_SHIFT) == MASK_NONE) || (key == KEY_ESCAPE && mask == MASK_NONE))
    {
        const bool from_view = atomViewFocused();
        if (from_view && key == KEY_ESCAPE)
        {
            if (mTakesFocus)
            {
                setFocus(true);
            }
            return true;
        }
        if (key != KEY_ESCAPE && (from_view || mReadOnly || key == KEY_F6) && focusAtomView((mask & MASK_SHIFT) == 0))
        {
            return true;
        }
    }
    if (mModal && mModal->handleKey(*this, key, mask))
    {
        mBlink.reset();
        return true;
    }
    const ALEditorCommand command = mKeymap.lookup(key, mask);
    if (command == ALEditorCommand::None)
    {
        return false;
    }
    if (mReadOnly && editsText(command))
    {
        // Somebody else's: a Tab moves on, a Return closes.
        return false;
    }
    if (!perform(command))
    {
        return false;
    }
    if (mModal && !mModal->inserting())
    {
        // A key the modal keymap left to the plain one -- Command-A on
        // the Mac, a paste -- moved the caret or the selection under it,
        // as the mouse does.
        mModal->mouseChanged(*this);
    }
    mBlink.reset();
    return true;
}

bool ALTextView::handleUnicodeCharHere(llwchar uni_char)
{
    if (uni_char < 0x20 || uni_char == 0x7F)
    {
        return false;
    }
    if (mModal && mModal->handleChar(*this, uni_char))
    {
        mBlink.reset();
        return true;
    }
    if (mReadOnly)
    {
        return false;
    }
    insertText(utf8str_from_cp(uni_char));
    if (LLWindow* window = getWindow())
    {
        window->hideCursorUntilMouseMove();
    }
    return true;
}

bool ALTextView::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (LLUICtrl::handleMouseDown(x, y, mask))
    {
        return true;
    }
    if (mTakesFocus)
    {
        setFocus(true);
    }
    if (mScrollMap && mapRect().pointInRect(x, y))
    {
        // The map: the view goes where it is pressed, and follows a drag.
        mDraggingMap = true;
        gFocusMgr.setMouseCapture(this);
        scrollToMapY(y);
        return true;
    }
    // A bar: its thumb taken hold of where it was pressed, or brought to
    // where the track was.
    if (const LLRect ruler = rulerRect(); ruler.notEmpty() && ruler.pointInRect(x, y))
    {
        const LLRect thumb = vThumb(ruler);
        mBarDrag           = BarDrag::Vertical;
        mBarDragOffset     = thumb.pointInRect(x, y) ? y - thumb.mTop : -thumb.getHeight() / 2;
        gFocusMgr.setMouseCapture(this);
        scrollToRulerY(y, mBarDragOffset);
        return true;
    }
    if (const LLRect bar = hBarRect(); bar.notEmpty() && bar.pointInRect(x, y) && barAlpha() > 0.f)
    {
        const LLRect thumb = hThumb(bar);
        mBarDrag           = BarDrag::Horizontal;
        mBarDragOffset     = thumb.pointInRect(x, y) ? x - thumb.mLeft : thumb.getWidth() / 2;
        gFocusMgr.setMouseCapture(this);
        scrollToBarX(x, mBarDragOffset);
        return true;
    }
    const bool near = llabs(x - mClickX) <= CLICK_SLOP && llabs(y - mClickY) <= CLICK_SLOP;
    mClickX         = x;
    mClickY         = y;
    if (!mTripleClick.hasExpired() && near)
    {
        // The third click takes the line; through placeCaret, so that
        // whoever follows the caret hears of it.
        const S32 line = mCaret.line;
        mAnchor        = mDocument.lineStart(line);
        placeCaret(line + 1 < mDocument.lineCount() ? mDocument.lineStart(line + 1) : mDocument.lineEnd(line), true);
        mDesiredX  = -1.f;
        mSelecting = false;
        if (mModal)
        {
            mModal->mouseChanged(*this);
        }
        return true;
    }
    mTripleClick.stop();
    // A link or an atom under the press is followed on the release, if
    // the release is on it too.
    mPressedLink = -1;
    mPressedAtom = -1;
    if (const Substitution* link = linkAtLocal(x, y))
    {
        mPressedLink = static_cast<S32>(link - mSubstitutions.data());
    }
    else if (const Atom* atom = atomAtLocal(x, y); atom && !atom->view)
    {
        mPressedAtom = static_cast<S32>(atom - mAtoms.data());
    }
    placeCaret(posAtLocal(x, y, true), (mask & MASK_SHIFT) != 0);
    mDesiredX  = -1.f;
    mSelecting = true;
    gFocusMgr.setMouseCapture(this);
    if (mModal)
    {
        mModal->mouseChanged(*this);
    }
    return true;
}

bool ALTextView::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    // A link that is a URL has the registry's menu for it.
    if (const Substitution* link = linkAtLocal(x, y); link && !link->url.empty())
    {
        if (mTakesFocus)
        {
            setFocus(true);
        }
        if (showUrlMenu(x, y, link->url))
        {
            return true;
        }
    }
    if (mContextMenuFile.empty() || !textRect().pointInRect(x, y))
    {
        return LLUICtrl::handleRightMouseDown(x, y, mask);
    }
    if (mTakesFocus)
    {
        setFocus(true);
    }
    // The click puts the caret where it landed, unless it landed in the
    // selection, which is what the menu is then about.
    const ALTextPos   at  = posAtLocal(x, y, true);
    const ALTextRange sel = selection().normalised();
    if (!hasSelection() || at < sel.begin || sel.end < at)
    {
        placeCaret(at, false);
        mDesiredX = -1.f;
    }
    refreshSuggestions();
    showContextMenu(x, y);
    return true;
}

bool ALTextView::handleHover(S32 x, S32 y, MASK mask)
{
    // The mouse is here: the bars stay in sight.
    mBarShown.reset();
    if (mBarDrag != BarDrag::None && hasMouseCapture())
    {
        if (mBarDrag == BarDrag::Vertical)
        {
            scrollToRulerY(y, mBarDragOffset);
        }
        else
        {
            scrollToBarX(x, mBarDragOffset);
        }
        return true;
    }
    if (mDraggingMap && hasMouseCapture())
    {
        scrollToMapY(y);
        return true;
    }
    if (mSelecting && hasMouseCapture())
    {
        const LLRect text  = textRect();
        const S32    row_h = mLayout.rowHeight();
        if (y > text.mTop)
        {
            setScrollY(mScrollY - row_h);
        }
        else if (y < text.mBottom)
        {
            setScrollY(mScrollY + row_h);
        }
        placeCaret(posAtLocal(x, y, true), true);
        mDesiredX = -1.f;
        if (mModal)
        {
            mModal->mouseChanged(*this);
        }
        return true;
    }
    // Resting on the map previews the lines there, as long as it rests.
    const LLRect map = mapRect();
    mMapHoverY       = mScrollMap && mScrollMapPreview && map.notEmpty() && map.pointInRect(x, y) ? y : -1;
    if (textRect().pointInRect(x, y))
    {
        const Substitution* link = linkAtLocal(x, y);
        const Atom*         atom = link ? nullptr : atomAtLocal(x, y);
        mHoverLink               = link ? static_cast<S32>(link - mSubstitutions.data()) : -1;
        mHoverAtom               = atom ? static_cast<S32>(atom - mAtoms.data()) : -1;
        if (LLWindow* window = getWindow())
        {
            window->setCursor(link || (atom && !atom->view) ? UI_CURSOR_HAND : UI_CURSOR_IBEAM);
        }
        return true;
    }
    mHoverLink = -1;
    mHoverAtom = -1;
    return LLUICtrl::handleHover(x, y, mask);
}

void ALTextView::onMouseLeave(S32 x, S32 y, MASK mask)
{
    mMapHoverY = -1;
    mHoverLink = -1;
    mHoverAtom = -1;
    LLUICtrl::onMouseLeave(x, y, mask);
}

bool ALTextView::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (mBarDrag != BarDrag::None)
    {
        mBarDrag = BarDrag::None;
        gFocusMgr.setMouseCapture(nullptr);
        return true;
    }
    if (mDraggingMap)
    {
        mDraggingMap = false;
        gFocusMgr.setMouseCapture(nullptr);
        return true;
    }
    if (mSelecting)
    {
        mSelecting = false;
        gFocusMgr.setMouseCapture(nullptr);
        // The link or the atom pressed, let go of on the same without a
        // drag: followed.
        const S32 pressed_link = mPressedLink;
        const S32 pressed_atom = mPressedAtom;
        mPressedLink           = -1;
        mPressedAtom           = -1;
        if (!hasSelection())
        {
            if (const Substitution* link = linkAtLocal(x, y); link && pressed_link == static_cast<S32>(link - mSubstitutions.data()))
            {
                const Substitution followed = *link;
                mLinkClicked(followed);
            }
            else if (const Atom* atom = atomAtLocal(x, y); atom && pressed_atom == static_cast<S32>(atom - mAtoms.data()))
            {
                const Atom clicked = *atom;
                mAtomClicked(clicked);
            }
        }
        return true;
    }
    return LLUICtrl::handleMouseUp(x, y, mask);
}

bool ALTextView::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType cargo_type, void* cargo_data, EAcceptance* accept,
                                   std::string& tooltip_msg)
{
    if (mDropHandler && mDropHandler(x, y, mask, drop, cargo_type, cargo_data, accept, tooltip_msg))
    {
        return true;
    }
    return LLUICtrl::handleDragAndDrop(x, y, mask, drop, cargo_type, cargo_data, accept, tooltip_msg);
}

bool ALTextView::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    if (LLUICtrl::handleDoubleClick(x, y, mask))
    {
        return true;
    }
    if (!sameClickSpot(x, y))
    {
        // Quick, but somewhere else: a click, and the start of a drag.
        return handleMouseDown(x, y, mask);
    }
    setFocus(true);
    const ALTextRange word = mDocument.wordAt(posAtLocal(x, y, false));
    mAnchor                = word.begin;
    placeCaret(word.end, true);
    mDesiredX  = -1.f;
    mSelecting = false;
    armTripleClick();
    if (mModal)
    {
        mModal->mouseChanged(*this);
    }
    return true;
}

bool ALTextView::sameClickSpot(S32 x, S32 y) const
{
    return llabs(x - mClickX) <= CLICK_SLOP && llabs(y - mClickY) <= CLICK_SLOP;
}

void ALTextView::armTripleClick()
{
    mTripleClick.setTimerExpirySec(TRIPLE_CLICK_INTERVAL);
}

bool ALTextView::handleScrollWheel(S32 x, S32 y, LLScrollDelta delta)
{
    setScrollY(mScrollY + delta.mClicks * WHEEL_ROWS * mLayout.rowHeight());
    return true;
}

bool ALTextView::handleScrollHWheel(S32 x, S32 y, LLScrollDelta delta)
{
    if (mWordWrap)
    {
        return false;
    }
    setScrollX(mScrollX + static_cast<F32>(delta.mClicks * WHEEL_ROWS * mLayout.rowHeight()));
    return true;
}

void ALTextView::onMouseCaptureLost()
{
    mSelecting   = false;
    mDraggingMap = false;
    mBarDrag     = BarDrag::None;
}

bool ALTextView::handleToolTip(S32 x, S32 y, MASK mask)
{
    // The map has its preview drawn as the mouse moves; no tip there.
    const LLRect map = mapRect();
    if (mScrollMap && mScrollMapPreview && map.notEmpty() && map.pointInRect(x, y))
    {
        return true;
    }
    // A link or an atom says what it is, while the mouse is on it.
    std::string says;
    ALTextRange about;
    if (const Substitution* link = linkAtLocal(x, y))
    {
        says  = link->tooltip;
        about = link->range;
    }
    else if (const Atom* atom = atomAtLocal(x, y))
    {
        says  = atom->tooltip;
        about = atomRange(*atom);
    }
    if (!says.empty())
    {
        const LLRect text = textRect();
        S32          row;
        mLayout.xOf(about.begin.line, about.begin.column, &row);
        const S32 top = screenTopOf(text, about.begin.line, row);
        F32       x0, x1;
        LLRect    local(text.mLeft, top, text.mRight, top - mLayout.rowHeightOf(about.begin.line, row));
        if (spanOnRow(about.begin.line, row, about, x0, x1))
        {
            local.mLeft  = static_cast<S32>(static_cast<F32>(text.mLeft) - mScrollX + x0);
            local.mRight = static_cast<S32>(static_cast<F32>(text.mLeft) - mScrollX + x1);
        }
        LLRect sticky;
        localRectToScreen(local, &sticky);
        LLToolTip::Params tip;
        tip.message     = says;
        tip.sticky_rect = sticky;
        LLToolTipMgr::instance().show(tip);
        return true;
    }
    return LLUICtrl::handleToolTip(x, y, mask);
}

void ALTextView::setFocus(bool focus)
{
    // The keyboard to the view itself: a control with a child that has
    // the keyboard counts as having it, which is right for a panel and
    // wrong here, where the find bar's field is the child and a click in
    // the text means the text.
    if (focus && getEnabled() && gFocusMgr.getKeyboardFocus() != this)
    {
        gFocusMgr.setKeyboardFocus(this);
    }
    LLUICtrl::setFocus(focus);
    if (focus)
    {
        gEditMenuHandler   = this;
        mChangedSinceFocus = false;
        mBlink.reset();
        allowLanguageInput(!mReadOnly);
    }
    else
    {
        allowLanguageInput(false);
        if (gEditMenuHandler == this)
        {
            gEditMenuHandler = nullptr;
        }
    }
}

void ALTextView::onFocusLost()
{
    allowLanguageInput(false);
    if (mChangedSinceFocus)
    {
        mChangedSinceFocus = false;
        onCommit();
    }
    LLUICtrl::onFocusLost();
}
