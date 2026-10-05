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

#include "alkeychord.h"

#include "altextchars.h"
#include "altextfeatures.h"
#include "altextruler.h"
#include "alsurface.h"
#include "alviewtype.h"
#include "llclipboard.h"
#include "lldir.h"
#include "llfocusmgr.h"
#include "llimage.h"
#include "llimagegl.h"
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
    // The bars: the bottom thumb's height, how long the bars stay in
    // sight once the mouse has left and the text has settled, and how
    // long they take to go; and the find bar's width.
    const S32 HBAR_H         = 8;
    const F32 BAR_HOLD       = 1.0f;
    const F32 BAR_FADE       = 0.5f;
    const S32 FIND_BAR_WIDTH = 460;

    const S32 H_MARGIN              = 16;
    const F32 CARET_MARGIN          = 8.f;

    // What each kind's colour is called in the colour table, after the
    // prefix; Text is the view's own text colour.
    const char* const KIND_COLOR_SUFFIXES[] = {
        "",           "Comment",     "DocComment",   "String",    "Escape",         "Number",    "Keyword",
        "Control",    "Type",        "Constant",     "Function",  "Event",          "Label",     "Operator",
        "Punctuation", "Preprocessor", "Tag",        "Attribute", "AttributeValue", "Entity",    "Variable",
        "Parameter",  "Property",    "Deprecated",   "Invalid",     "Namespace",      "State",     "GlobalVariable",
        "Path",
    };
    static_assert(sizeof(KIND_COLOR_SUFFIXES) / sizeof(KIND_COLOR_SUFFIXES[0]) == static_cast<size_t>(ALSyntaxKind::COUNT), "every kind has a colour");

    // Whether a command changes the text, which a read-only view refuses.
    // Every command is named, with no default, so that one added to the
    // list is a build that fails here until someone says which it is.
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
            case ALEditorCommand::JoinLines:
            case ALEditorCommand::Complete:
            case ALEditorCommand::Rename:
            case ALEditorCommand::QuickFix:
            case ALEditorCommand::InsertLineBelow:
            case ALEditorCommand::InsertLineAbove:
                return true;
            case ALEditorCommand::None:
            case ALEditorCommand::MoveLeft:
            case ALEditorCommand::MoveRight:
            case ALEditorCommand::MoveUp:
            case ALEditorCommand::MoveDown:
            case ALEditorCommand::MoveWordLeft:
            case ALEditorCommand::MoveWordRight:
            case ALEditorCommand::MoveLineStart:
            case ALEditorCommand::MoveLineEnd:
            case ALEditorCommand::MoveDocStart:
            case ALEditorCommand::MoveDocEnd:
            case ALEditorCommand::MovePageUp:
            case ALEditorCommand::MovePageDown:
            case ALEditorCommand::SelectLeft:
            case ALEditorCommand::SelectRight:
            case ALEditorCommand::SelectUp:
            case ALEditorCommand::SelectDown:
            case ALEditorCommand::SelectWordLeft:
            case ALEditorCommand::SelectWordRight:
            case ALEditorCommand::SelectLineStart:
            case ALEditorCommand::SelectLineEnd:
            case ALEditorCommand::SelectDocStart:
            case ALEditorCommand::SelectDocEnd:
            case ALEditorCommand::SelectPageUp:
            case ALEditorCommand::SelectPageDown:
            case ALEditorCommand::SelectAll:
            case ALEditorCommand::Copy:
            case ALEditorCommand::Fold:
            case ALEditorCommand::Unfold:
            case ALEditorCommand::FoldAll:
            case ALEditorCommand::UnfoldAll:
            case ALEditorCommand::SignatureHelp:
            case ALEditorCommand::GoToDefinition:
            case ALEditorCommand::FindReferences:
            case ALEditorCommand::Find:
            case ALEditorCommand::Replace:
            case ALEditorCommand::NextMisspelling:
            case ALEditorCommand::PreviousMisspelling:
            case ALEditorCommand::PreviousChange:
            case ALEditorCommand::NextChange:
            case ALEditorCommand::NextFunction:
            case ALEditorCommand::PreviousFunction:
            case ALEditorCommand::SelectFunction:
            case ALEditorCommand::GoToMatchingBracket:
            case ALEditorCommand::SelectLine:
            case ALEditorCommand::MoveSubwordLeft:
            case ALEditorCommand::MoveSubwordRight:
            case ALEditorCommand::SelectSubwordLeft:
            case ALEditorCommand::SelectSubwordRight:
            case ALEditorCommand::ExpandSelection:
            case ALEditorCommand::ShrinkSelection:
            case ALEditorCommand::SelectNextOccurrence:
            case ALEditorCommand::ChangeAllOccurrences:
            case ALEditorCommand::AddCaretAbove:
            case ALEditorCommand::AddCaretBelow:
            case ALEditorCommand::ColumnSelectLeft:
            case ALEditorCommand::ColumnSelectRight:
            case ALEditorCommand::ColumnSelectUp:
            case ALEditorCommand::ColumnSelectDown:
            case ALEditorCommand::FindNext:
            case ALEditorCommand::FindPrevious:
            case ALEditorCommand::COUNT:
                return false;
        }
        return false;
    }

    // The layout knows a substitution and an atom by one id each; atoms
    // are numbered from here so that the two are told apart.
    const S32 ATOM_ID_BASE = 1 << 30;
    S32       atomId(size_t index) { return ATOM_ID_BASE + static_cast<S32>(index); }
    bool      isAtomId(S32 id) { return id >= ATOM_ID_BASE; }

    // How long the word at the caret is left unmarked after it was typed.
    const F32 SPELL_SETTLE_SECONDS = 1.5f;
    // A squiggle: a wave a pixel high either way and six pixels long,
    // drawn as a line a pixel and a half wide, whose edges fade over a
    // pixel more. A pixel was too thin to see in a pale colour; six rather
    // than five so that the crests bend no tighter than the line is wide.
    // The texture is taller than the wave and its fade, so that a sampler
    // between texels, which wraps, never blends its top into its bottom.
    constexpr F32 SQUIGGLE_AMPLITUDE = 1.f;
    constexpr S32 SQUIGGLE_WAVE      = 6;
    constexpr S32 SQUIGGLE_HEIGHT    = 8;
    constexpr F32 SQUIGGLE_HALF      = 0.75f;
    constexpr F32 SQUIGGLE_FEATHER   = 1.f;
    // Its middle this far under the baseline: its line, a pixel and three
    // quarters above the middle at the crests, stops a pixel short of the
    // baseline, and only the fade reaches the pixel under it. Its middle
    // on the row's bottom, as it was, lay on the baseline wherever the
    // font's lines are closer than its letters are tall, and hid a
    // period's foot; under it, the wave crosses the descenders, and where
    // they reach into the next row, it reaches there with them.
    constexpr S32 SQUIGGLE_BELOW = 3;

    // One wave of a squiggle, which a squiggle repeats along its length: so
    // that one is a quad, however long, rather than a ribbon of triangles
    // for each pixel of it. White, with as much alpha at each texel as the
    // line covers of it, and drawn in the squiggle's colour. Texels are as
    // many to a point as the UI's scale has pixels, so that the line stays
    // as sharp on a screen that scales it; remade when the scale changes.
    LLImageGL* squiggleTexture()
    {
        static LLPointer<LLImageGL> texture;
        static S32                  made_at = 0;
        const S32                   scale   = llclamp(static_cast<S32>(ceilf(LLUI::getScaleFactor().mV[VX])), 1, 4);
        if (texture && made_at == scale)
        {
            return texture;
        }
        const S32             width  = SQUIGGLE_WAVE * scale;
        const S32             height = SQUIGGLE_HEIGHT * scale;
        LLPointer<LLImageRaw> raw    = new LLImageRaw(static_cast<U16>(width), static_cast<U16>(height), 4);
        U8*                   data   = raw->getData();
        const F32             step   = 2.f * F_PI / static_cast<F32>(SQUIGGLE_WAVE);
        for (S32 row = 0; row < height; ++row)
        {
            for (S32 column = 0; column < width; ++column)
            {
                // The texel's middle, in points from the wave's start and
                // its middle, and how near the wave comes to it: looked
                // for along a wave either side, finely enough that the
                // fade is smooth.
                const F32 x    = (static_cast<F32>(column) + 0.5f) / static_cast<F32>(scale);
                const F32 y    = (static_cast<F32>(row) + 0.5f) / static_cast<F32>(scale) - 0.5f * static_cast<F32>(SQUIGGLE_HEIGHT);
                F32       nearest = F32_MAX;
                for (F32 along = x - static_cast<F32>(SQUIGGLE_WAVE); along <= x + static_cast<F32>(SQUIGGLE_WAVE); along += 0.01f)
                {
                    const F32 dx = x - along;
                    const F32 dy = y - SQUIGGLE_AMPLITUDE * sinf(along * step);
                    nearest      = llmin(nearest, dx * dx + dy * dy);
                }
                const F32 cover = llclamp((SQUIGGLE_HALF + SQUIGGLE_FEATHER - sqrtf(nearest)) / SQUIGGLE_FEATHER, 0.f, 1.f);
                U8*       texel = data + (static_cast<size_t>(row) * width + column) * 4;
                texel[0] = texel[1] = texel[2] = 255;
                texel[3] = static_cast<U8>(llround(cover * 255.f));
            }
        }
        texture = new LLImageGL(raw, false);
        made_at = scale;
        return texture;
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
    pass_escape("pass_escape", false),
    placeholder("placeholder")
{
}

namespace
{
    // A layer over the text that holds the atoms' views and draws them
    // only within itself: one scrolled partly out of sight is cut at the
    // text's edge, rather than drawn over the gutter, the band, or what is
    // beside the view. The mouse passes through it to what is under it,
    // an atom's view or the text.
    class ALAtomLayer : public LLView
    {
    public:
        explicit ALAtomLayer(const LLView::Params& p) : LLView(p) {}

        void draw() override
        {
            LLLocalClipRect clip(getLocalRect());
            LLView::draw();
        }
    };
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
    // The bar down the side, before anything else the view holds, so that
    // all of that is drawn over it.
    ALTextRuler::Params ruler;
    ruler.name          = "ruler";
    ruler.view          = this;
    ruler.visible       = false;
    ruler.follows.flags = FOLLOWS_NONE;
    mRuler              = LLUICtrlFactory::create<ALTextRuler>(ruler);
    addChild(mRuler);
    // Then the atoms' layer, over the text and under all else.
    LLView::Params layer(LLUICtrlFactory::getDefaultParams<LLView>());
    layer.name          = "atoms";
    layer.mouse_opaque  = false;
    layer.follows.flags = FOLLOWS_NONE;
    mAtomLayer          = new ALAtomLayer(layer);
    addChild(mAtomLayer);

    const S32 tab_width = p.tab_width;
    mTabWidth           = llmax(1, tab_width);
    mIndentDefaults     = { mTabWidth, mSoftTabs };
    mFindMatchColor     = p.find_match_color.isProvided() ? p.find_match_color() : LLUIColorTable::instance().getColor("TextFindMatchColor", LLColor4(1.f, 0.7f, 0.2f, 0.4f));
    mScrollMap          = p.scroll_map;
    mScrollMapWidth     = llmax(20, static_cast<S32>(p.scroll_map_width));
    mScrollMapPreview   = p.scroll_map_preview;
    mScrollMapLeft      = p.scroll_map_left;
    {
        const LLUIColorTable& colors = LLUIColorTable::instance();
        const std::string     prefix = p.syntax_color_prefix();
        mColorPrefix                 = prefix;
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
    mPassEscape      = p.pass_escape;

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
            mAtomLayer->removeChild(atom.view);
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
    // One caret in a new text, before the edit that puts it in slides the
    // others to its start for nothing.
    mCarets.clear();
    mDocument.setText(text);
    mUndo.clear();
    // A new text has no changes to go back to.
    mChanges.clear();
    mChangeAt = 0;
    // The layers were about the text that was; the edit that replaced
    // it has dropped what it cut through, which is everything, and a
    // substitution over an empty text is nothing.
    setSubstitutions({});
    setAtoms({});
    setStyles({});
    setLineAnnotations({});
    // Through the virtual, so that what a subclass keeps about changes
    // since the last save -- the gutter's bars -- starts clean too.
    resetDirty();
    mCaret = mAnchor = mDocument.start();
    mDesiredX         = -1.f;
    mScrollY          = 0;
    mScrollX          = 0.f;
    mScrollAsked      = true;
    mChangedSinceFocus = false;
    if (mIndentFrom != IndentFrom::Chosen)
    {
        readIndentation();
    }
    syncScrollbar();
    mChanged();
}

bool ALTextView::setTextWithHistory(std::string_view text, const LLSD& history)
{
    // Read against the text first, once: the journal can only be given a
    // history over the text it is of, and putting the text in clears what
    // this one had, which a history that does not fit would then have
    // taken for nothing.
    std::optional<ALTextUndo::History> read = ALTextUndo::historyFrom(history, text);
    if (!read)
    {
        return false;
    }
    setText(text);
    mUndo.restore(std::move(*read));
    return true;
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
    syncLanguageInput();
}

void ALTextView::setWordWrap(bool wrap)
{
    mWordWrap = wrap;
    mScrollX  = 0.f;
    mLayout.setWrapWidth(wrap ? textRect().getWidth() : 0);
    syncScrollbar();
}

void ALTextView::setSideScroll(bool scroll)
{
    mSideScroll = scroll;
    mScrollX    = 0.f;
    syncScrollbar();
}

void ALTextView::setTabWidth(S32 spaces)
{
    useIndentation({ spaces, mSoftTabs }, IndentFrom::Chosen);
}

void ALTextView::setSoftTabs(bool soft)
{
    useIndentation({ mTabWidth, soft }, IndentFrom::Chosen);
}

void ALTextView::setIndentDefaults(S32 tab_width, bool soft_tabs)
{
    mIndentDefaults = { llmax(1, tab_width), soft_tabs };
    // A text of tabs takes their width from the defaults too.
    if (mIndentFrom != IndentFrom::Chosen)
    {
        readIndentation();
    }
}

void ALTextView::setReadsIndentation(bool reads)
{
    if (mReadsIndentation == reads)
    {
        return;
    }
    mReadsIndentation = reads;
    if (mIndentFrom != IndentFrom::Chosen)
    {
        readIndentation();
    }
}

void ALTextView::readIndentation()
{
    const std::optional<ALTextIndent::Options> own =
        mReadsIndentation ? ALTextIndent::detect(mDocument, mIndentDefaults.tabWidth) : std::nullopt;
    useIndentation(own ? *own : mIndentDefaults, own ? IndentFrom::Text : IndentFrom::Defaults);
}

void ALTextView::useIndentation(const ALTextIndent::Options& options, IndentFrom from)
{
    mIndentFrom = from;
    mSoftTabs   = options.softTabs;
    const S32 width = llmax(1, options.tabWidth);
    if (width != mTabWidth)
    {
        mTabWidth = width;
        mLayout.setTabWidth(mTabWidth);
    }
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

LLView* ALTextView::overlayAt(S32 x, S32 y)
{
    // The frontmost child under the point that would take the mouse. Not
    // the ruler, which is beside the text; and a child that only holds
    // others -- a list and the box beside it, as large as the view and
    // seen through -- only where one of those is.
    for (LLView* child : *getChildList())
    {
        const S32 local_x = x - child->getRect().mLeft;
        const S32 local_y = y - child->getRect().mBottom;
        if (child == mRuler || !child->getVisible() || !child->pointInView(local_x, local_y))
        {
            continue;
        }
        if (child->getMouseOpaque() || child->childFromPoint(local_x, local_y))
        {
            return child;
        }
    }
    return nullptr;
}

bool ALTextView::rulerAt(S32 x, S32 y)
{
    return mRuler->getVisible() && mRuler->getRect().pointInRect(x, y) && !overlayAt(x, y);
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
        rect.mRight -= ALTextRuler::WIDTH;
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
    // Heights above the view moved since it was last scrolled: back to the
    // line it was on, as far into it as it was; or at the very top, there
    // still, rows put over the first line shown with it.
    if (!mScrollAsked && mLayout.heightsRevision() != mAnchorHeights && mDocument.lineCount() > 0)
    {
        const S32 line = llclamp(mAnchorLine, 0, mDocument.lineCount() - 1);
        mScrollY       = mAnchorAtTop ? 0 : mLayout.lineTop(line) + llclamp(mAnchorOffset, -mLayout.gapHeight(line), llmax(0, mLayout.lineHeight(line) - 1));
    }
    // The ruler takes room the wrap width depends on, so the need for it
    // is decided again once the first decision has been applied.
    for (S32 pass = 0; pass < 2; ++pass)
    {
        const LLRect text   = textRect();
        const bool   need_v = !mScrollMap && mLayout.totalHeight() > llmax(1, text.getHeight());
        const bool   need_h =
            mSideScroll && !mWordWrap && mLayout.contentWidth() + static_cast<F32>(H_MARGIN) > static_cast<F32>(text.getWidth());
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
    const S32 content = mWordWrap || !mSideScroll ? 0 : static_cast<S32>(ceilf(mLayout.contentWidth())) + H_MARGIN;
    mScrollX          = llclamp(mScrollX, 0.f, static_cast<F32>(llmax(0, content - width)));
    // Where the top of the view is now, in the text.
    mAnchorLine    = mLayout.lineAtY(mScrollY);
    mAnchorOffset  = mScrollY - mLayout.lineTop(mAnchorLine);
    mAnchorAtTop   = mScrollY == 0;
    mAnchorHeights = mLayout.heightsRevision();
    mScrollAsked   = false;
    placeRuler();
    if (mScrollY != mToldScrollY || mScrollX != mToldScrollX)
    {
        mToldScrollY = mScrollY;
        mToldScrollX = mScrollX;
        mScrolled();
    }
}

void ALTextView::placeRuler()
{
    const LLRect where = mScrollMap ? mapRect() : rulerRect();
    if (where.notEmpty() && where != mRuler->getRect())
    {
        mRuler->setShape(where);
    }
    mRuler->setVisible(where.notEmpty());
}

void ALTextView::setScrollY(S32 y)
{
    if (y != mScrollY)
    {
        mBarShown.reset();
    }
    mScrollY     = llmax(0, y);
    mScrollAsked = true;
    syncScrollbar();
}

bool ALTextView::canScrollY(S32 direction)
{
    const S32 most = llmax(0, mLayout.totalHeight() - llmax(1, textRect().getHeight()));
    return direction > 0 ? mScrollY < most : direction < 0 && mScrollY > 0;
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
    scrollToShow(mCaret);
}

void ALTextView::scrollToShow(const ALTextPos& pos)
{
    const S32 row_h = mLayout.rowHeight();
    if (row_h <= 0)
    {
        return;
    }
    S32       row;
    const F32 x      = mLayout.xOf(pos.line, pos.column, &row);
    const S32 top    = mLayout.lineTop(pos.line) + mLayout.rowTop(pos.line, row);
    const S32 height = mLayout.rowHeightOf(pos.line, row);
    const LLRect text = textRect();
    const S32 page = llmax(height, text.getHeight());
    // Across first: where it is across says what covers it above.
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
    if (top + height > mScrollY + page)
    {
        mScrollY = top + height - page;
    }
    // Up, below what is drawn over the top where it is -- the find bar,
    // the lines pinned there -- which is asked again at each place tried,
    // since what is pinned is what the top of the view is inside. A line's
    // first row scrolled up to brings the gap above it with it, where
    // there is room for both: what stands for what is not there.
    const S32 local_x = text.mLeft + static_cast<S32>(x - mScrollX);
    const S32 gap     = row == 0 ? mLayout.gapHeight(pos.line) : 0;
    for (S32 tries = 0; tries < 4 && mScrollY > 0; ++tries)
    {
        const S32 covered = llmin(llmax(0, coveredAbove(local_x)), llmax(0, page - height));
        if (top >= mScrollY + covered)
        {
            break;
        }
        mScrollY = llmax(0, top - covered - llmin(gap, llmax(0, page - height - covered)));
    }
    if (top < mScrollY)
    {
        mScrollY = top - llmin(gap, llmax(0, page - height));
    }
    mScrollY     = llmax(0, mScrollY);
    mScrollAsked = true;
    syncScrollbar();
}

S32 ALTextView::coveredAbove(S32 local_x)
{
    if (!findShown())
    {
        return 0;
    }
    const LLRect bar  = mFindBar->getRect();
    const LLRect text = textRect();
    return local_x >= bar.mLeft && local_x <= bar.mRight ? llmax(0, text.mTop - bar.mBottom) : 0;
}

void ALTextView::scrollToLine(S32 line)
{
    mScrollY     = mLayout.lineTop(line);
    mScrollAsked = true;
    syncScrollbar();
}

void ALTextView::trimLayout()
{
    // So many past what is in sight, however much that is: a window tall
    // enough to show more lines than that is not trimmed every frame.
    const S32 first = firstVisibleLine();
    const S32 last  = lastVisibleLine();
    if (mLayout.linesHeld() > LAYOUT_HELD_MOST + (last - first + 1))
    {
        mLayout.trim(first - LAYOUT_KEPT_AROUND, last + LAYOUT_KEPT_AROUND);
    }
}

void ALTextView::onVisibilityChange(bool new_visibility)
{
    if (!new_visibility)
    {
        publishPrimary();
        mLayout.trim(0, -1);
    }
    LLUICtrl::onVisibilityChange(new_visibility);
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
    // A keymap let go of in the middle of something -- vim in its insert
    // mode, asking about substitutions -- leaves the group it opened open,
    // and every edit after would be one step with what came before. None is
    // the view's own between calls, so every one open is closed.
    if (mModal)
    {
        mUndo.closeGroups();
    }
    mModal = std::move(keymap);
    syncLanguageInput();
}

// --- the caret ---------------------------------------------------------------

void ALTextView::placeCaret(const ALTextPos& pos, bool extend)
{
    const ALTextPos caret = snapped(mDocument.clamp(pos), mCaret);
    placeSelection(extend ? mAnchor : caret, caret);
}

void ALTextView::placeSelection(const ALTextPos& anchor, const ALTextPos& caret)
{
    const ALTextRange was      = selection();
    const bool        was_gap  = mCaretGap >= 0;
    mCaretGap                  = -1;
    mAnchor                    = mDocument.clamp(anchor);
    mCaret                = snapped(mDocument.clamp(caret), was.end);
    // The others it now meets become part of it.
    if (!mCarets.empty())
    {
        ALTextRange main(mAnchor, mCaret);
        if (mCarets.merge(main))
        {
            mAnchor = main.begin;
            mCaret  = main.end;
        }
    }
    // A caret on a line folded away is not drawn: its line is shown, the
    // others' as the main one's, where any line is hidden at all.
    const auto reveal = [this](S32 line) {
        if (!mLayout.hidden(line))
        {
            return;
        }
        if (mFeatures)
        {
            mFeatures->revealLine(line);
        }
        else
        {
            mLayout.setHidden(ALTextLayout::HiddenBy::Any, line, line, false);
        }
    };
    reveal(mCaret.line);
    if (!mCarets.empty() && mLayout.anyHidden())
    {
        std::vector<S32> lines;
        lines.reserve(mCarets.size());
        for (const ALTextRange& other : mCarets)
        {
            lines.push_back(other.end.line);
        }
        for (const S32 line : lines)
        {
            reveal(line);
        }
    }
    mBlink.reset();
    if (selection() != was)
    {
        // Offered once a frame, not at every step: a selection of the
        // whole text moved a line at a time is otherwise copied whole at
        // each, and a drag's once it is let go of.
        if (hasSelection())
        {
            mPrimaryStale = true;
        }
    }
    // Out of a gap onto the line under it is a move, though the place in
    // the text is the one it had.
    if (selection() != was || was_gap)
    {
        mCaretMoved();
    }
}

void ALTextView::publishPrimary()
{
    if (mPrimaryStale && !mSelecting)
    {
        offerPrimary();
    }
}

void ALTextView::offerPrimary()
{
    mPrimaryStale = false;
    if (hasSelection())
    {
        const std::string text = selectedText();
        LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size()), true);
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
    placeSelection(range.begin, range.end);
    mDesiredX = -1.f;
    scrollToCaret();
}

// --- several carets ------------------------------------------------------------

std::vector<ALTextRange> ALTextView::clamped(std::vector<ALTextRange> selections) const
{
    for (ALTextRange& one : selections)
    {
        one = ALTextRange(mDocument.clamp(one.begin), mDocument.clamp(one.end));
    }
    return selections;
}

void ALTextView::addSelection(const ALTextRange& range)
{
    mCarets.add(ALTextRange(mDocument.clamp(range.begin), mDocument.clamp(range.end)));
    // Put where it is again, to take in the new one where they meet.
    placeSelection(mAnchor, mCaret);
}

void ALTextView::setSelections(const ALTextRange& main, std::vector<ALTextRange> others)
{
    mCarets.assign(clamped(std::move(others)));
    setSelection(main);
}

bool ALTextView::singleSelection()
{
    if (mCarets.empty())
    {
        return false;
    }
    mCarets.clear();
    mBlink.reset();
    return true;
}

void ALTextView::toggleCaret(const ALTextPos& pos)
{
    const ALTextPos          at   = mDocument.clamp(pos);
    size_t                   main = 0;
    std::vector<ALTextRange> all  = selectionsInOrder(&main);
    // The one there -- a caret at it, or a selection over it -- taken
    // away, but for the last; the main one, the one before it then.
    for (size_t i = 0; i < all.size(); ++i)
    {
        const ALTextRange range = all[i].normalised();
        if (range.begin <= at && at <= range.end)
        {
            if (all.size() == 1)
            {
                return;
            }
            all.erase(all.begin() + static_cast<std::ptrdiff_t>(i));
            placeSelections(all, main == i ? (i > 0 ? i - 1 : 0) : (main > i ? main - 1 : main));
            return;
        }
    }
    all.emplace_back(at, at);
    placeSelections(all, all.size() - 1);
}

bool ALTextView::addCarets(S32 direction)
{
    size_t                         main = 0;
    const std::vector<ALTextRange> all  = selectionsInOrder(&main);
    // Each caret's x where a motion or this left them, else where it is:
    // a column grown past a short line keeps to its column.
    std::vector<F32> xs = desiredXs(all);
    std::vector<ALTextRange> placed = all;
    for (size_t i = 0; i < all.size(); ++i)
    {
        F32             x  = xs[i];
        const ALTextPos to = rowsFrom(all[i].end, direction, x);
        xs[i]              = x >= 0.f ? x : xs[i];
        // Past the first or the last row there is nothing to add.
        if (x >= 0.f)
        {
            placed.emplace_back(to, to);
            xs.push_back(x);
        }
    }
    if (placed.size() == all.size())
    {
        return false;
    }
    // The furthest that way the main one, for the view to follow.
    size_t furthest = 0;
    for (size_t i = 1; i < placed.size(); ++i)
    {
        if (direction < 0 ? placed[i].end < placed[furthest].end : placed[furthest].end < placed[i].end)
        {
            furthest = i;
        }
    }
    placeSelections(placed, furthest);
    // Each keeps its x, those that met another the first's.
    std::vector<ALTextRange> now = selectionsInOrder();
    std::vector<F32>         now_xs(now.size(), -1.f);
    for (size_t i = 0; i < now.size(); ++i)
    {
        for (size_t k = 0; k < placed.size(); ++k)
        {
            if (placed[k].end == now[i].end)
            {
                now_xs[i] = xs[k];
                break;
            }
        }
    }
    mEachDesired.selections = std::move(now);
    mEachDesired.xs         = std::move(now_xs);
    mEachDesired.under      = laidOut();
    mDesiredX               = xs[furthest];
    scrollToCaret();
    return true;
}

ALTextView::ColumnCorner ALTextView::cornerAt(const ALTextPos& pos)
{
    ColumnCorner corner;
    corner.line = pos.line;
    corner.x    = mLayout.xOf(pos.line, pos.column, &corner.row);
    return corner;
}

ALTextView::ColumnCorner ALTextView::cornerAtLocal(S32 x, S32 y)
{
    const LLRect text = textRect();
    ColumnCorner corner;
    if (mLayout.rowHeight() <= 0 || mDocument.lineCount() == 0)
    {
        return corner;
    }
    const S32 doc_y = (text.mTop - y) + mScrollY;
    corner.line     = mLayout.lineAtY(llmax(0, doc_y));
    corner.row      = mLayout.rowAtY(corner.line, doc_y - mLayout.lineTop(corner.line));
    corner.x        = llmax(0.f, static_cast<F32>(x - text.mLeft) + mScrollX);
    return corner;
}

bool ALTextView::stepRow(S32& line, S32& row, S32 direction)
{
    if (direction < 0)
    {
        if (row > 0)
        {
            --row;
            return true;
        }
        const S32 above = mLayout.visibleFrom(line - 1, -1);
        if (above < 0)
        {
            return false;
        }
        line = above;
        row  = mLayout.rowCount(line) - 1;
        return true;
    }
    if (row + 1 < mLayout.rowCount(line))
    {
        ++row;
        return true;
    }
    const S32 below = mLayout.visibleFrom(line + 1, 1);
    if (below < 0)
    {
        return false;
    }
    line = below;
    row  = 0;
    return true;
}

ALTextView::LaidOut ALTextView::laidOut()
{
    return { mLayout.wrapWidth(), mLayout.columnWidth() };
}

std::vector<F32> ALTextView::desiredXs(const std::vector<ALTextRange>& all)
{
    return mEachDesired.selections == all && mEachDesired.under == laidOut() ? mEachDesired.xs : std::vector<F32>(all.size(), -1.f);
}

std::pair<ALTextView::ColumnCorner, ALTextView::ColumnCorner> ALTextView::columnCorners()
{
    // Its own corners while it stands as it was put, in the layout it was
    // put in: zoomed or wrapped again since, its x's and rows are another
    // layout's.
    if (!mColumn.selections.empty() && selectionsInOrder() == mColumn.selections && mColumn.under == laidOut())
    {
        return { mColumn.from, mColumn.to };
    }
    return { cornerAt(mAnchor), cornerAt(mCaret) };
}

void ALTextView::selectColumn(const ColumnCorner& from, const ColumnCorner& to)
{
    const bool down = from.line < to.line || (from.line == to.line && from.row <= to.row);
    // From the higher corner's row down to the lower's, each row's columns
    // at the two x's, nearest as drawn: past a row's end, its end.
    S32 line = down ? from.line : to.line;
    S32 row  = down ? from.row : to.row;
    const ColumnCorner& last = down ? to : from;
    std::vector<ALTextRange> rows;
    while (true)
    {
        row = llclamp(row, 0, llmax(0, mLayout.rowCount(line) - 1));
        rows.emplace_back(ALTextPos(line, mLayout.columnAt(line, row, from.x, true)), ALTextPos(line, mLayout.columnAt(line, row, to.x, true)));
        if (line > last.line || (line == last.line && row >= last.row) || !stepRow(line, row, 1))
        {
            break;
        }
    }
    placeSelections(rows, down ? rows.size() - 1 : 0);
    mColumn.from       = from;
    mColumn.to         = to;
    mColumn.selections = selectionsInOrder();
    mColumn.under      = laidOut();
    // Each caret keeps the column's x between rows, as carets added above
    // and below do, so that moving them keeps the column past short rows.
    mEachDesired.selections = mColumn.selections;
    mEachDesired.xs.assign(mColumn.selections.size(), to.x);
    mEachDesired.under = mColumn.under;
    mDesiredX = to.x;
    scrollToCaret();
}

bool ALTextView::growColumn(S32 columns, S32 rows)
{
    if (mLayout.rowHeight() <= 0 || mDocument.lineCount() == 0)
    {
        return false;
    }
    auto [from, to] = columnCorners();
    if (rows != 0 && !stepRow(to.line, to.row, rows))
    {
        return false;
    }
    if (columns < 0 && to.x <= 0.f && rows == 0)
    {
        return false;
    }
    // A column is a space's advance: a tab or a wide character is crossed
    // in as many steps as it is drawn wide, its edge the nearest.
    to.x = llmax(0.f, to.x + static_cast<F32>(columns) * mLayout.columnWidth());
    selectColumn(from, to);
    return true;
}

S32 ALTextView::selectAllMatches()
{
    settleFind();
    const std::vector<ALTextRange>& matches = mFind.matches();
    if (matches.empty())
    {
        return 0;
    }
    // Every match selected, the current one the main one, and the
    // keyboard to the text to type over them.
    const S32 current = llclamp(mFind.current(), 0, static_cast<S32>(matches.size()) - 1);
    placeSelections(std::vector<ALTextRange>(matches.begin(), matches.end()), static_cast<size_t>(current));
    mDesiredX = -1.f;
    scrollToCaret();
    if (mTakesFocus)
    {
        setFocus(true);
    }
    return static_cast<S32>(matches.size());
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
    std::vector<Substitution> links = urlLinks(mDocument.line(line), line, from);
    for (Substitution& link : links)
    {
        addSubstitution(std::move(link));
    }
    return static_cast<S32>(links.size());
}

std::vector<ALTextView::Substitution> ALTextView::urlLinks(const std::string& text, S32 line, S32 from, labelled_t labelled)
{
    std::vector<Substitution> out;
    if (from < 0 || from >= static_cast<S32>(text.size()))
    {
        return out;
    }
    const LLHandle<ALTextView> self = getDerivedHandle<ALTextView>();
    // A name that arrives later goes to every link of the URL it is for,
    // and to whoever keeps them.
    const auto relabelled = [self, labelled](const std::string& url, const std::string& label, const std::string&) {
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
        if (labelled)
        {
            labelled(url, label);
        }
    };
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
        out.push_back(std::move(link));
        rest = rest.substr(match.getEnd() + 1);
        at   = end;
    }
    return out;
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
                mAtomLayer->removeChild(atom.view);
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
        if (atom.view && atom.view->getParent() != mAtomLayer)
        {
            mAtomLayer->addChild(atom.view);
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
        if (atom.view && atom.view->getParent() != mAtomLayer)
        {
            atom.view->die();
        }
        return;
    }
    if (atom.view && atom.view->getParent() != mAtomLayer)
    {
        mAtomLayer->addChild(atom.view);
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
    // The layer over the text as it now is; the atoms' boxes in it.
    if (mAtomLayer->getRect() != text)
    {
        mAtomLayer->setShape(text);
    }
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
                const F32    left = -mScrollX;
                const LLRect box(static_cast<S32>(left + x0), top - text.mBottom, static_cast<S32>(left + x1), top - height - text.mBottom);
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
    return mSpellCheck && !mReadOnly && mSpelling.available();
}

void ALTextView::setSpellChecker(spell_checker_t checker, spell_suggester_t suggester)
{
    mSpelling.setChecker(std::move(checker), std::move(suggester));
}

void ALTextView::recheckSpelling()
{
    mSpelling.recheck();
}

std::optional<ALTextRange> ALTextView::misspellingFrom(const ALTextPos& from, bool forward, bool* cut)
{
    if (cut)
    {
        *cut = false;
    }
    const S32 lines = mDocument.lineCount();
    if (!getSpellCheck() || lines == 0)
    {
        return std::nullopt;
    }
    // A real-time clock, read only after a line that had to be checked: a
    // line checked already costs next to nothing to look at again, and the
    // search always gets at least one line further.
    LLTimer spent;
    spent.setTimerExpirySec(mMisspellingBudget);
    for (S32 i = 0; i <= lines; ++i)
    {
        const S32   line  = ((forward ? from.line + i : from.line - i) % lines + lines) % lines;
        // The place's own line first, after it or before it; and last, once
        // round, what is left of it.
        const bool  own   = i == 0;
        const bool  again  = i == lines;
        const U32   checks = mSpelling.checks();
        const auto& words  = misspellings(line);
        auto found = [&](const std::pair<S32, S32>& word) {
            return ALTextRange(ALTextPos(line, word.first), ALTextPos(line, word.second));
        };
        if (forward)
        {
            for (const auto& word : words)
            {
                if ((own && word.first > from.column) || (again && word.first <= from.column) || (!own && !again))
                {
                    return found(word);
                }
            }
        }
        else
        {
            for (auto it = words.rbegin(); it != words.rend(); ++it)
            {
                if ((own && it->first < from.column) || (again && it->first >= from.column) || (!own && !again))
                {
                    return found(*it);
                }
            }
        }
        if (i < lines && mSpelling.checks() != checks && spent.hasExpired())
        {
            if (cut)
            {
                *cut = true;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool ALTextView::goToChange(S32 steps)
{
    const S32 to = mChangeAt + steps;
    if (steps == 0 || to < 0 || to >= static_cast<S32>(mChanges.size()))
    {
        return false;
    }
    mChangeAt = to;
    placeCaret(mDocument.clamp(mChanges[static_cast<size_t>(to)]), false);
    scrollToCaret();
    return true;
}

bool ALTextView::convertIndentation(S32 first, S32 last, bool to_spaces, S32 measured_width)
{
    if (mReadOnly)
    {
        return false;
    }
    const std::optional<ALTextEditing::Change> change =
        ALTextIndent::convertIndentation(mDocument, first, last, to_spaces, getTabWidth(), measured_width);
    if (!change)
    {
        return false;
    }
    // The caret where it was, on its line.
    ALTextEditing::Change keep = *change;
    keep.caret                 = mCaret;
    apply(keep);
    return true;
}

bool ALTextView::goToMisspelling(bool forward)
{
    mMisspellingSought.reset();
    const ALTextRange                selected = selection().normalised();
    bool                             cut      = false;
    const std::optional<ALTextRange> word     = misspellingFrom(forward ? selected.end : selected.begin, forward, &cut);
    if (!word)
    {
        if (cut)
        {
            mMisspellingSought = MisspellingSought{ forward, selection(), mDocument.version() };
        }
        return cut;
    }
    setSelection(*word);
    scrollToCaret();
    return true;
}

void ALTextView::seekMisspelling()
{
    const MisspellingSought sought = *mMisspellingSought;
    mMisspellingSought.reset();
    if (sought.version == mDocument.version() && sought.selection == selection() && getSpellCheck())
    {
        goToMisspelling(sought.forward);
    }
}

const std::vector<std::pair<S32, S32>>& ALTextView::misspellings(S32 line)
{
    return mSpelling.misspellings(mDocument, mHighlighter, line, getSpellCheck());
}

bool ALTextView::misspelledAt(const ALTextPos& pos, ALTextRange* word)
{
    return mSpelling.misspelledAt(mDocument, mHighlighter, pos, getSpellCheck(), word);
}

void ALTextView::refreshSuggestions()
{
    mSpelling.suggestAt(mDocument, mHighlighter, mCaret, getSpellCheck());
}

const std::string& ALTextView::getSuggestion(U32 index) const
{
    return index < mSpelling.suggestions().size() ? mSpelling.suggestions()[index] : LLStringUtil::null;
}

U32 ALTextView::getSuggestionCount() const
{
    return static_cast<U32>(mSpelling.suggestions().size());
}

void ALTextView::replaceWithSuggestion(U32 index)
{
    if (mReadOnly)
    {
        return;
    }
    if (const std::optional<std::pair<ALTextRange, std::string>> taken = mSpelling.take(index); taken && !edit(taken->first, taken->second).nothing())
    {
        afterEdit();
    }
}

void ALTextView::addToDictionary()
{
    if (canAddToDictionary())
    {
        mSpelling.addToDictionary(mDocument);
    }
}

bool ALTextView::canAddToDictionary() const
{
    return mSpelling.canTeach(getSpellCheck());
}

void ALTextView::addToIgnore()
{
    if (canAddToIgnore())
    {
        mSpelling.addToIgnore(mDocument);
    }
}

bool ALTextView::canAddToIgnore() const
{
    return canAddToDictionary();
}

void ALTextView::onDocumentEdit(const ALTextDocument::Edit& edit)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    // The change list: what it holds slides with the text, a place the edit
    // took some of to where it began; and the edit's own place joins it, in
    // the place of the last where that was on the same line.
    const ALTextRange removed = edit.range.normalised();
    for (ALTextPos& at : mChanges)
    {
        at = edit.placed(at);
    }
    if (!mChanges.empty() && mChanges.back().line == removed.begin.line)
    {
        mChanges.back() = removed.begin;
    }
    else
    {
        mChanges.push_back(removed.begin);
        if (mChanges.size() > 100)
        {
            mChanges.erase(mChanges.begin());
        }
    }
    mChangeAt = static_cast<S32>(mChanges.size());
    // The line the top of the view is on slides with the text, so that an
    // edit above it leaves the view on what it showed.
    mAnchorLine = edit.placed(ALTextPos(mAnchorLine, 0)).line;
    // The lines the edit touched are checked again when they are next
    // drawn; the ones below slide.
    mSpelling.edited(edit, mDocument.lineCount());
    mSpellTimer.reset();
    // What a host said of each line slides with the lines; a line the
    // edit made says nothing, but for the rows above it: a gap above a
    // line typed in, or replaced, stays above the first line made there.
    if (annotated())
    {
        std::vector<std::pair<S32, LineAnnotation>> gaps;
        if (mAnyGap)
        {
            S32 moved = 0;
            for (const ALTextDocument::Edit::LineSpan& span : edit.lineSpans())
            {
                if (span.made > 0 && span.first >= 0 && span.first < static_cast<S32>(mAnnotations.size()) &&
                    mAnnotations[static_cast<size_t>(span.first)].gap > 0)
                {
                    gaps.emplace_back(span.first + moved, mAnnotations[static_cast<size_t>(span.first)]);
                }
                moved += span.made - (span.last - span.first + 1);
            }
        }
        mAnnotations.applySpans(edit.lineSpans(), mDocument.lineCount(), LineAnnotation(), LineAnnotation());
        for (const auto& [line, was] : gaps)
        {
            if (line >= 0 && line < static_cast<S32>(mAnnotations.size()))
            {
                LineAnnotation& now = mAnnotations[static_cast<size_t>(line)];
                now.gap             = was.gap;
                now.gapTint         = was.gapTint;
                now.gapRulerTint    = was.gapRulerTint;
            }
        }
        ++mAnnotationsRevision;
        if (mAnyGap)
        {
            mLayout.gapsChanged();
        }
    }
    // What the find bar found slides with the text, and so do the carets
    // besides the main one, which whoever made the edit puts.
    mFind.edited(edit);
    mCarets.apply(edit);

    // The layers: what is after the edit slides with the text, what it
    // cut through goes.
    if (!mSubstitutions.empty())
    {
        mSubstitutions.apply(edit);
        mHoverLink   = -1;
        mPressedLink = -1;
    }
    mStyles.apply(edit);
    if (!mAtoms.empty())
    {
        // An atom cut through takes its view with it.
        mAtoms.apply(
            edit,
            [this](Atom& atom, const ALTextDocument::Edit& e) {
                ALTextRange range = atomRange(atom);
                if (!e.slide(range))
                {
                    return false;
                }
                atom.at = range.begin;
                return true;
            },
            [this](Atom& atom) {
                if (atom.view)
                {
                    letGoOfAtomView(atom.view);
                    mAtomLayer->removeChild(atom.view);
                    atom.view->die();
                }
            });
        mHoverAtom   = -1;
        mPressedAtom = -1;
    }
}

ALTextPos ALTextView::rowsFrom(const ALTextPos& from, S32 rows, F32& desired_x)
{
    if (mLayout.rowHeight() <= 0)
    {
        return from;
    }
    S32       row;
    const F32 x = mLayout.xOf(from.line, from.column, &row);
    if (desired_x < 0.f)
    {
        desired_x = x;
    }
    S32 line = from.line;
    for (; rows != 0; rows += rows < 0 ? 1 : -1)
    {
        if (!stepRow(line, row, rows))
        {
            desired_x = -1.f;
            return rows < 0 ? mDocument.start() : mDocument.end();
        }
    }
    return ALTextPos(line, mLayout.columnAt(line, row, desired_x, true));
}

namespace
{
    bool isMotion(ALEditorCommand command)
    {
        typedef ALEditorCommand C;
        return (command >= C::MoveLeft && command <= C::MovePageDown) || (command >= C::SelectLeft && command <= C::SelectPageDown) ||
               (command >= C::MoveSubwordLeft && command <= C::SelectSubwordRight);
    }

    bool isVertical(ALEditorCommand command)
    {
        typedef ALEditorCommand C;
        switch (command)
        {
            case C::MoveUp:
            case C::MoveDown:
            case C::MovePageUp:
            case C::MovePageDown:
            case C::SelectUp:
            case C::SelectDown:
            case C::SelectPageUp:
            case C::SelectPageDown:
                return true;
            default:
                return false;
        }
    }
}

ALTextRange ALTextView::moved(ALEditorCommand command, const ALTextRange& selection, F32& desired_x)
{
    typedef ALEditorCommand C;
    const bool        extend = (command >= C::SelectLeft && command <= C::SelectPageDown) || command == C::SelectSubwordLeft ||
                               command == C::SelectSubwordRight;
    const ALTextPos&  caret  = selection.end;
    const ALTextRange range  = selection.normalised();
    // An arrow with a selection and no shift collapses it to that end.
    const bool        collapse = !extend && !range.empty();
    ALTextPos         to       = caret;
    switch (command)
    {
        case C::MoveLeft:
        case C::SelectLeft:
            to = collapse ? range.begin : mDocument.prevCluster(caret);
            break;
        case C::MoveRight:
        case C::SelectRight:
            to = collapse ? range.end : mDocument.nextCluster(caret);
            break;
        case C::MoveUp:
        case C::SelectUp:
            to = rowsFrom(caret, -1, desired_x);
            break;
        case C::MoveDown:
        case C::SelectDown:
            to = rowsFrom(caret, 1, desired_x);
            break;
        case C::MovePageUp:
        case C::SelectPageUp:
            to = rowsFrom(caret, -rowsPerPage(), desired_x);
            break;
        case C::MovePageDown:
        case C::SelectPageDown:
            to = rowsFrom(caret, rowsPerPage(), desired_x);
            break;
        case C::MoveWordLeft:
        case C::SelectWordLeft:
            to = wordStep(caret, false, false);
            break;
        case C::MoveWordRight:
        case C::SelectWordRight:
            to = wordStep(caret, true, false);
            break;
        case C::MoveSubwordLeft:
        case C::SelectSubwordLeft:
            to = wordStep(caret, false, true);
            break;
        case C::MoveSubwordRight:
        case C::SelectSubwordRight:
            to = wordStep(caret, true, true);
            break;
        case C::MoveLineStart:
        case C::SelectLineStart:
        {
            // To the first thing on the line, or to the line's start from
            // there.
            const std::string& line   = mDocument.line(caret.line);
            S32                indent = 0;
            while (indent < static_cast<S32>(line.size()) && (line[indent] == ' ' || line[indent] == '\t'))
            {
                ++indent;
            }
            to = ALTextPos(caret.line, caret.column == indent ? 0 : indent);
            break;
        }
        case C::MoveLineEnd:
        case C::SelectLineEnd:
            to = mDocument.lineEnd(caret.line);
            break;
        case C::MoveDocStart:
        case C::SelectDocStart:
            to = mDocument.start();
            break;
        case C::MoveDocEnd:
        case C::SelectDocEnd:
            to = mDocument.end();
            break;
        default:
            return selection;
    }
    if (!isVertical(command))
    {
        desired_x = -1.f;
    }
    to = snapped(mDocument.clamp(to), caret);
    return ALTextRange(extend ? selection.begin : to, to);
}

// --- editing -------------------------------------------------------------------

ALTextRange ALTextView::withoutComposition(const ALTextRange& range) const
{
    // A place measured with the composition standing in the text, measured
    // as though it were not: after it on its line, back by what it added --
    // what it wrote over put back -- and inside it, at its start.
    const auto without = [this](const ALTextPos& pos) {
        if (!hasPreedit() || pos.line != mPreeditBegin.line || pos <= mPreeditBegin)
        {
            return pos;
        }
        if (pos.column < mPreeditBegin.column + mPreeditLength)
        {
            return mPreeditBegin;
        }
        return ALTextPos(pos.line, pos.column - mPreeditLength + static_cast<S32>(mPreeditOverwritten.size()));
    };
    return ALTextRange(without(range.begin), without(range.end));
}

ALTextDocument::Edit ALTextView::edit(const ALTextRange& range_in, std::string_view text)
{
    // A composition is not text yet, and the journal knows nothing of it:
    // taken out before anything the journal keeps is done, so that every
    // step is of the text without it, and the range, measured with it in,
    // measured without it. The input method composes on at the caret.
    ALTextRange range = range_in;
    if (hasPreedit())
    {
        range = withoutComposition(range_in);
        resetPreedit();
    }
    const ALTextRange        before = selection();
    std::vector<ALTextRange> others = mCarets.selections();
    ALTextDocument::Edit     done   = mDocument.replace(range, fitting(range, text));
    if (done.nothing())
    {
        return done;
    }
    const ALTextPos after = mDocument.clamp(done.endAfter());
    mUndo.record(done, before, after, LLTimer::getElapsedSeconds(), std::move(others));
    placeCaret(after, false);
    return done;
}

ALTextDocument::Edit ALTextView::editMany(std::vector<std::pair<ALTextRange, std::string>> edits, const ALTextPos& caret)
{
    if (hasPreedit())
    {
        for (auto& one : edits)
        {
            one.first = withoutComposition(one.first.normalised());
        }
        resetPreedit();
    }
    if (!fits(edits))
    {
        return ALTextDocument::Edit();
    }
    const ALTextRange        before = selection();
    std::vector<ALTextRange> others = mCarets.selections();
    ALTextDocument::Edit     done   = mDocument.replaceMany(std::move(edits));
    if (done.nothing())
    {
        return done;
    }
    const ALTextPos after = mDocument.clamp(caret);
    mUndo.record(done, before, after, LLTimer::getElapsedSeconds(), std::move(others));
    placeCaret(after, false);
    return done;
}

std::string_view ALTextView::fitting(const ALTextRange& over, std::string_view text)
{
    if (mMaxBytes == 0)
    {
        return text;
    }
    const ALTextRange range = over.normalised();
    const size_t      taken = mDocument.offsetOf(range.end) - mDocument.offsetOf(range.begin);
    const size_t      left  = mDocument.byteCount() - taken;
    if (text.size() <= taken || left + text.size() <= mMaxBytes)
    {
        return text;
    }
    // As much as there is room for, cut where a character starts -- a
    // notecard's item is one character too, and goes whole or not at all.
    size_t room = mMaxBytes > left ? mMaxBytes - left : 0;
    while (room > 0 && room < text.size() && (static_cast<unsigned char>(text[room]) & 0xC0) == 0x80)
    {
        --room;
    }
    full();
    return text.substr(0, room);
}

bool ALTextView::fits(const std::vector<std::pair<ALTextRange, std::string>>& edits)
{
    if (mMaxBytes == 0)
    {
        return true;
    }
    size_t taken = 0, put = 0;
    for (const auto& [over, text] : edits)
    {
        const ALTextRange range = over.normalised();
        taken += mDocument.offsetOf(range.end) - mDocument.offsetOf(range.begin);
        put += text.size();
    }
    if (put <= taken || mDocument.byteCount() - taken + put <= mMaxBytes)
    {
        return true;
    }
    full();
    return false;
}

void ALTextView::full()
{
    make_ui_sound("UISndBadKeystroke");
    mFull();
}

void ALTextView::afterEdit()
{
    editsDone();
    // Where the change left the selections, for a redo to put them back
    // there; nothing once a step has been taken back or forward.
    mUndo.settle(selection(), mCarets.selections());
    mDesiredX          = -1.f;
    mChangedSinceFocus = true;
    mBlink.reset();
    scrollToCaret();
    if (findShown())
    {
        mFind.stale();
        mFindBar->setCount(mFind.current(), static_cast<S32>(mFind.count()), mFind.error(), mFind.capped());
    }
    mChanged();
}

void ALTextView::insertText(std::string_view text)
{
    if (mReadOnly)
    {
        return;
    }
    const ALTextRange over = selection().normalised();
    if (!edit(over, text).nothing())
    {
        afterEdit();
    }
    else if (!over.empty() && !text.empty())
    {
        // What was selected typed or pasted over with the same text: the
        // text is as it was, and the caret goes past it, as it would have.
        placeCaret(over.end, false);
        mDesiredX = -1.f;
        scrollToCaret();
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
        one.first = withoutComposition(one.first.normalised());
    }
    if (hasPreedit())
    {
        // Measured without it above; taken out now, before the caret is.
        resetPreedit();
    }
    // In the text's order: by where each begins, then where it ends, and
    // two alike in the order given -- so that, made from the last, what is
    // put in at a place stands before a stretch replaced from there, and
    // two put in at one place stand in the order given; as ALScriptFixes
    // applies a fix, and the fix preview shows it.
    std::stable_sort(edits.begin(), edits.end(),
                     [](const auto& a, const auto& b) { return a.first.begin < b.first.begin || (a.first.begin == b.first.begin && a.first.end < b.first.end); });

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

    // As one edit, which every listener hears once.
    mUndo.beginGroup();
    const bool any = !editMany(std::move(edits), caret).nothing();
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
    singleSelection();
    setCaret(mDocument.clamp(pos));
}

void ALTextView::goTo(const ALTextRange& range)
{
    singleSelection();
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

// --- line commands and indentation ----------------------------------------------

ALTextPos ALTextView::wordStep(const ALTextPos& from, bool forward, bool parts) const
{
    // Code by its names and marks, as a double-click takes a name; prose,
    // and a text with no grammar, by its words.
    const ALSyntaxGrammar* grammar = mHighlighter.grammar().get();
    if (parts || (grammar && !grammar->prose()))
    {
        return forward ? mDocument.nextCodeWord(from, parts) : mDocument.prevCodeWord(from, parts);
    }
    return forward ? mDocument.nextWord(from) : mDocument.prevWord(from);
}

ALTextIndent::Options ALTextView::editingOptions() const
{
    ALTextIndent::Options options;
    options.tabWidth = mTabWidth;
    options.softTabs = mSoftTabs;
    return options;
}

ALTextIndent::opener_t ALTextView::openerOf()
{
    return [this](const ALTextPos& closer, ALTextPos& opener) { return mFeatures && mFeatures->closerOpenedAt(closer, opener); };
}

void ALTextView::apply(const ALTextEditing::Change& change)
{
    // One edit however many lines it touches: an indent of every line is
    // one notification, not one a line.
    std::vector<std::pair<ALTextRange, std::string>> edits;
    edits.reserve(change.replacements.size());
    for (const ALTextEditing::Replacement& one : change.replacements)
    {
        edits.emplace_back(one.range, one.text);
    }
    // Not made at all, the caret left where it was, where the text has
    // no room for it.
    if (!fits(edits))
    {
        return;
    }
    mUndo.beginGroup();
    editMany(std::move(edits), change.caret);
    mUndo.endGroup();
    if (change.selects)
    {
        placeSelection(change.anchor, change.caret);
    }
    else
    {
        placeCaret(change.caret, false);
    }
    afterEdit();
}

std::string ALTextView::tabText(const ALTextPos& at) const
{
    return ALTextIndent::tabText(mDocument, at, editingOptions());
}

bool ALTextView::commentBefore(const ALTextPos& at)
{
    if (!mHighlighter.grammar() || at.column <= 0)
    {
        return false;
    }
    for (const ALSyntaxToken& token : mHighlighter.tokens(at.line))
    {
        if (token.begin < at.column && at.column <= token.end)
        {
            return token.kind == ALSyntaxKind::Comment || token.kind == ALSyntaxKind::DocComment;
        }
    }
    return false;
}

// --- at every selection ------------------------------------------------------------

std::vector<ALTextRange> ALTextView::selectionsInOrder(size_t* main) const
{
    const ALTextRange              mine  = selection();
    const ALTextPos                begin = mine.normalised().begin;
    const std::vector<ALTextRange>& others = mCarets.selections();
    // Where the main one goes among the others: none of them begins where
    // it does, or they would be one.
    const auto at = std::lower_bound(others.begin(), others.end(), begin,
                                     [](const ALTextRange& other, const ALTextPos& p) { return other.normalised().begin < p; });
    std::vector<ALTextRange> all;
    all.reserve(others.size() + 1);
    all.insert(all.end(), others.begin(), at);
    if (main)
    {
        *main = all.size();
    }
    all.push_back(mine);
    all.insert(all.end(), at, others.end());
    return all;
}

bool ALTextView::anySelected() const
{
    return hasSelection() || std::any_of(mCarets.begin(), mCarets.end(), [](const ALTextRange& other) { return !other.empty(); });
}

void ALTextView::placeSelections(const std::vector<ALTextRange>& selections, size_t main)
{
    std::vector<ALTextRange> others;
    others.reserve(selections.size());
    for (size_t i = 0; i < selections.size(); ++i)
    {
        if (i != main)
        {
            others.push_back(selections[i]);
        }
    }
    mCarets.assign(clamped(std::move(others)));
    placeSelection(selections[main].begin, selections[main].end);
}

bool ALTextView::applyGroups(const std::vector<ALTextRange>& selections, size_t main, std::vector<ALTextEditing::Group> groups)
{
    ALTextEditing::Combined combined = ALTextEditing::combine(std::move(groups), selections.size());
    std::vector<std::pair<ALTextRange, std::string>> edits;
    edits.reserve(combined.replacements.size());
    for (ALTextEditing::Replacement& one : combined.replacements)
    {
        edits.emplace_back(one.range, std::move(one.text));
    }
    // Made whole or not at all: a change of several stretches that the text
    // has no room for is not made.
    ALTextDocument::Edit done;
    if (!edits.empty())
    {
        if (!fits(edits))
        {
            return false;
        }
        done = editMany(std::move(edits), combined.selections[main] ? combined.selections[main]->end : mCaret);
    }
    std::vector<ALTextRange> placed(selections.size());
    for (size_t i = 0; i < selections.size(); ++i)
    {
        if (combined.selections[i])
        {
            placed[i] = *combined.selections[i];
        }
        else
        {
            placed[i] = done.nothing() ? selections[i] : ALTextCarets::slid(selections[i], done);
        }
    }
    if (!edits.empty() && done.nothing())
    {
        // Nothing changed: each where it was.
        return false;
    }
    placeSelections(placed, main);
    if (done.nothing())
    {
        mDesiredX = -1.f;
        scrollToCaret();
        return false;
    }
    afterEdit();
    return true;
}

bool ALTextView::editEach(const each_change_t& change)
{
    // The steps are of the text without a composition in it, and so are
    // the selections each is worked out at.
    if (hasPreedit())
    {
        resetPreedit();
    }
    size_t                         main = 0;
    const std::vector<ALTextRange> all  = selectionsInOrder(&main);
    std::vector<ALTextEditing::Group> groups;
    for (size_t i = 0; i < all.size(); ++i)
    {
        std::optional<ALTextEditing::Change> one = change(i, all[i]);
        if (!one)
        {
            continue;
        }
        ALTextEditing::Group group;
        group.replacements = std::move(one->replacements);
        group.placed.emplace_back(i, one->selects ? ALTextRange(one->anchor, one->caret) : ALTextRange(one->caret, one->caret));
        groups.push_back(std::move(group));
    }
    return applyGroups(all, main, std::move(groups));
}

bool ALTextView::editGroups(const groups_t& groups)
{
    if (hasPreedit())
    {
        resetPreedit();
    }
    size_t                         main = 0;
    const std::vector<ALTextRange> all  = selectionsInOrder(&main);
    mUndo.beginGroup();
    const bool done = applyGroups(all, main, groups(all));
    mUndo.endGroup();
    return done;
}

std::optional<ALTextRange> ALTextView::erasedBy(ALEditorCommand command, const ALTextRange& selection) const
{
    typedef ALEditorCommand C;
    if (!selection.empty())
    {
        return selection.normalised();
    }
    const ALTextPos& at = selection.end;
    switch (command)
    {
        case C::DeleteLeft:
            if (at != mDocument.start())
            {
                // In a line's indentation, a level's spaces at once.
                const std::optional<ALTextPos> level = ALTextIndent::backspaceFrom(mDocument, at, editingOptions());
                return ALTextRange(level ? *level : mDocument.prevCluster(at), at);
            }
            break;
        case C::DeleteRight:
            if (at != mDocument.end())
            {
                return ALTextRange(at, mDocument.nextCluster(at));
            }
            break;
        case C::DeleteWordLeft:
            return ALTextRange(wordStep(at, false, false), at);
        case C::DeleteWordRight:
            return ALTextRange(at, wordStep(at, true, false));
        case C::DeleteToLineStart:
            // Back to the line's start, or the break before it from there.
            if (at.column > 0)
            {
                return ALTextRange(ALTextPos(at.line, 0), at);
            }
            if (at != mDocument.start())
            {
                return ALTextRange(mDocument.prevCluster(at), at);
            }
            break;
        case C::DeleteToLineEnd:
            // On to the line's end, or the break after it from there.
            if (at != mDocument.lineEnd(at.line))
            {
                return ALTextRange(at, mDocument.lineEnd(at.line));
            }
            if (at != mDocument.end())
            {
                return ALTextRange(at, mDocument.nextCluster(at));
            }
            break;
        default:
            break;
    }
    return std::nullopt;
}

void ALTextView::typeText(std::string_view text)
{
    if (mReadOnly || text.empty())
    {
        return;
    }
    if (hasOtherSelections())
    {
        typeAtEach(text, 0);
    }
    else
    {
        insertText(text);
    }
}

void ALTextView::typeAtEach(std::string_view text, llwchar typed)
{
    const std::string put(text);
    editEach([&put](size_t, const ALTextRange& selection) {
        const ALTextRange     over = selection.normalised();
        ALTextEditing::Change one;
        one.replacements.push_back({ over, put });
        one.caret = ALTextEditing::endOf(over.begin, put);
        return std::optional<ALTextEditing::Change>(std::move(one));
    });
    if (typed)
    {
        outdentEach(typed);
    }
}

void ALTextView::outdentEach(llwchar typed, const std::function<bool(size_t)>& at)
{
    // A word so brought out is not kept to be put back, as one caret's is.
    mAutoOutdent = ALTextIndent::AutoOutdent();
    const ALSyntaxGrammar* grammar = mHighlighter.grammar().get();
    editEach([&](size_t i, const ALTextRange& selection) -> std::optional<ALTextEditing::Change> {
        if (!selection.empty() || (at && !at(i)))
        {
            return std::nullopt;
        }
        const ALTextIndent::Outdent outdent = ALTextIndent::outdentAsTyped(mDocument, selection.begin, selection.end, typed, grammar, openerOf(),
                                                                           ALTextIndent::AutoOutdent(), editingOptions());
        if (!outdent.replacement)
        {
            return std::nullopt;
        }
        ALTextEditing::Change one;
        one.replacements.push_back(*outdent.replacement);
        one.caret = ALTextEditing::placedThrough(one.replacements, selection.end);
        return one;
    });
}

std::optional<bool> ALTextView::performAtEach(ALEditorCommand command)
{
    typedef ALEditorCommand C;
    if (isMotion(command))
    {
        return moveEach(command);
    }
    switch (command)
    {
        case C::DeleteLeft:
        case C::DeleteRight:
        case C::DeleteWordLeft:
        case C::DeleteWordRight:
        case C::DeleteToLineStart:
        case C::DeleteToLineEnd:
            return deleteEach(command);
        case C::NewLine:
            newLineEach();
            return true;
        case C::Indent:
            // A selection, however small, indents its lines; a caret puts a
            // tab in -- but for one on a line a selection indents, which
            // goes in with the line rather than putting a tab before what
            // the selection holds.
            editGroups([this](const std::vector<ALTextRange>& all) {
                std::vector<std::pair<S32, S32>> indented;
                for (const ALTextRange& one : all)
                {
                    if (!one.empty())
                    {
                        indented.push_back(ALTextEditing::selectedLines(one));
                    }
                }
                const auto onIndented = [&indented](S32 line) {
                    return std::ranges::any_of(indented, [line](const std::pair<S32, S32>& lines) { return line >= lines.first && line <= lines.second; });
                };
                std::vector<ALTextRange> selected;
                std::vector<size_t>      at;
                std::vector<bool>        taken(all.size(), false);
                for (size_t i = 0; i < all.size(); ++i)
                {
                    if (!all[i].empty() || onIndented(all[i].end.line))
                    {
                        selected.push_back(all[i]);
                        at.push_back(i);
                        taken[i] = true;
                    }
                }
                std::vector<ALTextEditing::Group> groups = ALTextIndent::indentLines(mDocument, selected, true, editingOptions());
                for (ALTextEditing::Group& group : groups)
                {
                    for (auto& placed : group.placed)
                    {
                        placed.first = at[placed.first];
                    }
                }
                for (size_t i = 0; i < all.size(); ++i)
                {
                    if (!taken[i])
                    {
                        const std::string    tab = tabText(all[i].end);
                        const ALTextPos      end = ALTextEditing::endOf(all[i].end, tab);
                        ALTextEditing::Group group;
                        group.replacements.push_back({ all[i], tab });
                        group.placed.emplace_back(i, ALTextRange(end, end));
                        groups.push_back(std::move(group));
                    }
                }
                return groups;
            });
            return true;
        case C::Unindent:
            editGroups([this](const std::vector<ALTextRange>& all) { return ALTextIndent::indentLines(mDocument, all, false, editingOptions()); });
            return true;
        case C::DuplicateLine:
            editGroups([this](const std::vector<ALTextRange>& all) { return ALTextEditing::duplicateLines(mDocument, all); });
            return true;
        case C::MoveLineUp:
        case C::MoveLineDown:
        {
            const S32 direction = command == C::MoveLineUp ? -1 : 1;
            editGroups([this, direction](const std::vector<ALTextRange>& all) { return ALTextEditing::moveLines(mDocument, all, direction); });
            return true;
        }
        case C::DeleteLine:
            editGroups([this](const std::vector<ALTextRange>& all) { return ALTextEditing::deleteLines(mDocument, all); });
            return true;
        case C::JoinLines:
            return editGroups([this](const std::vector<ALTextRange>& all) { return ALTextEditing::joinLines(mDocument, all); });
        case C::InsertLineAbove:
            // A line above each line a caret is on, as far in as that line,
            // however many carets are on it.
            editGroups([this](const std::vector<ALTextRange>& all) {
                std::vector<std::pair<S32, size_t>> lines;
                for (size_t i = 0; i < all.size(); ++i)
                {
                    lines.emplace_back(all[i].end.line, i);
                }
                std::sort(lines.begin(), lines.end());
                std::vector<ALTextEditing::Group> groups;
                S32                               indent = 0;
                for (size_t k = 0; k < lines.size(); ++k)
                {
                    const S32 line = lines[k].first;
                    if (k == 0 || line != lines[k - 1].first)
                    {
                        const std::string blanks = ALTextIndent::leadingBlanks(mDocument, line);
                        const ALTextPos   start  = mDocument.lineStart(line);
                        indent                   = static_cast<S32>(blanks.size());
                        groups.emplace_back();
                        groups.back().replacements.push_back({ ALTextRange(start, start), blanks + "\n" });
                    }
                    const ALTextPos caret(line, indent);
                    groups.back().placed.emplace_back(lines[k].second, ALTextRange(caret, caret));
                }
                return groups;
            });
            scrollToCaret();
            return true;
        case C::InsertLineBelow:
        {
            // As Return at each line's end would make it.
            size_t                   main = 0;
            std::vector<ALTextRange> all  = selectionsInOrder(&main);
            for (ALTextRange& one : all)
            {
                const ALTextPos end = mDocument.lineEnd(one.end.line);
                one                 = ALTextRange(end, end);
            }
            placeSelections(all, main);
            newLineEach();
            scrollToCaret();
            return true;
        }
        case C::SelectLine:
        {
            // Each selection's lines whole, with their breaks.
            size_t                   main = 0;
            std::vector<ALTextRange> all  = selectionsInOrder(&main);
            for (ALTextRange& one : all)
            {
                const ALTextRange range = one.normalised();
                const S32         past  = range.end.line + 1;
                one = ALTextRange(mDocument.lineStart(range.begin.line),
                                  past < mDocument.lineCount() ? mDocument.lineStart(past) : mDocument.lineEnd(mDocument.lineCount() - 1));
            }
            placeSelections(all, main);
            mDesiredX = -1.f;
            scrollToCaret();
            return true;
        }
        // Done as they always are: those that add carets or grow a column,
        // the clipboard's, the comment's and completion know of the others;
        // select all, undo and redo put them as they put the main one;
        // folding and the find bar shown leave them be.
        case C::None:
        case C::COUNT:
        case C::AddCaretAbove:
        case C::AddCaretBelow:
        case C::ColumnSelectLeft:
        case C::ColumnSelectRight:
        case C::ColumnSelectUp:
        case C::ColumnSelectDown:
        case C::SelectNextOccurrence:
        case C::ChangeAllOccurrences:
        case C::Complete:
        case C::ToggleComment:
        case C::Cut:
        case C::Copy:
        case C::Paste:
        case C::Delete:
        case C::SelectAll:
        case C::Undo:
        case C::Redo:
        case C::Fold:
        case C::Unfold:
        case C::FoldAll:
        case C::UnfoldAll:
        case C::Find:
        case C::Replace:
            return std::nullopt;
        default:
            // At the main one alone: the others let go.
            singleSelection();
            return std::nullopt;
    }
}

bool ALTextView::moveEach(ALEditorCommand command)
{
    size_t                         main = 0;
    const std::vector<ALTextRange> all  = selectionsInOrder(&main);
    // The x each caret keeps between rows, where the last motion left them
    // and they have not moved since; else each from where it is.
    const bool       vertical = isVertical(command);
    std::vector<F32> xs       = vertical ? desiredXs(all) : std::vector<F32>(all.size(), -1.f);
    std::vector<ALTextRange> placed(all.size());
    for (size_t i = 0; i < all.size(); ++i)
    {
        placed[i] = moved(command, all[i], xs[i]);
    }
    placeSelections(placed, main);
    mDesiredX    = xs[main];
    mEachDesired = EachDesired();
    if (vertical)
    {
        std::vector<ALTextRange> now = selectionsInOrder();
        // Kept only while each is the one it was: none merged.
        if (now.size() == all.size())
        {
            mEachDesired.selections = std::move(now);
            mEachDesired.xs         = std::move(xs);
            mEachDesired.under      = laidOut();
        }
    }
    scrollToCaret();
    return true;
}

bool ALTextView::deleteEach(ALEditorCommand command)
{
    // A key that erases, as one: a run of them one step to undo.
    mUndo.beginTyping(selection(), true);
    editEach([this, command](size_t, const ALTextRange& selection) -> std::optional<ALTextEditing::Change> {
        const std::optional<ALTextRange> range = erasedBy(command, selection);
        if (!range || range->empty())
        {
            return std::nullopt;
        }
        ALTextEditing::Change one;
        one.replacements.push_back({ range->normalised(), std::string() });
        one.caret = range->normalised().begin;
        return one;
    });
    mUndo.endTyping();
    return true;
}

bool ALTextView::deleteSelectedEach()
{
    mUndo.beginGroup();
    const bool done = editEach([](size_t, const ALTextRange& selection) -> std::optional<ALTextEditing::Change> {
        if (selection.empty())
        {
            return std::nullopt;
        }
        ALTextEditing::Change one;
        one.replacements.push_back({ selection.normalised(), std::string() });
        one.caret = selection.normalised().begin;
        return one;
    });
    mUndo.endGroup();
    return done;
}

void ALTextView::newLineEach()
{
    const ALSyntaxGrammar* grammar = mHighlighter.grammar().get();
    // A key typed, whatever it takes: one with the typing around it.
    mUndo.beginTyping(selection());
    // First what the Return finishes, brought out at each caret.
    editEach([&](size_t, const ALTextRange& selection) -> std::optional<ALTextEditing::Change> {
        const std::optional<ALTextEditing::Replacement> closing =
            ALTextIndent::closingBeforeReturn(mDocument, selection.begin, selection.end, grammar, openerOf(), editingOptions());
        if (!closing)
        {
            return std::nullopt;
        }
        ALTextEditing::Change one;
        one.replacements.push_back(*closing);
        one.caret = ALTextEditing::placedThrough(one.replacements, selection.end);
        return one;
    });
    // Then each line split there.
    editEach([&](size_t, const ALTextRange& selection) {
        const bool                in_comment = selection.empty() && commentBefore(selection.end);
        const ALTextIndent::Split split      = ALTextIndent::splitLine(mDocument, selection, grammar, editingOptions(), in_comment);
        const ALTextRange         over       = split.range.normalised();
        ALTextEditing::Change     one;
        one.replacements.push_back({ over, split.text });
        one.caret = split.caret ? *split.caret : ALTextEditing::endOf(over.begin, split.text);
        return std::optional<ALTextEditing::Change>(std::move(one));
    });
    mUndo.endTyping();
}

void ALTextView::newLine()
{
    const ALSyntaxGrammar* grammar = mHighlighter.grammar().get();
    // A key typed, whatever it takes: one with the typing around it.
    mUndo.beginTyping(selection());
    if (const std::optional<ALTextEditing::Replacement> closing =
            ALTextIndent::closingBeforeReturn(mDocument, mAnchor, mCaret, grammar, openerOf(), editingOptions()))
    {
        replaceAll({ { closing->range, closing->text } });
    }
    // In a comment, as the token before the caret says: one that goes on
    // begins the new line as its lines do.
    const bool                in_comment = !hasSelection() && commentBefore(mCaret);
    const ALTextIndent::Split split      = ALTextIndent::splitLine(mDocument, selection(), grammar, editingOptions(), in_comment);
    setSelection(split.range);
    insertText(split.text);
    if (split.caret)
    {
        setCaret(*split.caret);
        mUndo.settle(selection());
    }
    mUndo.endTyping();
}

void ALTextView::outdentAsTyped(llwchar typed)
{
    const ALTextIndent::Outdent outdent =
        ALTextIndent::outdentAsTyped(mDocument, mAnchor, mCaret, typed, mHighlighter.grammar().get(), openerOf(), mAutoOutdent, editingOptions());
    mAutoOutdent = ALTextIndent::AutoOutdent();
    if (outdent.replacement && replaceAll({ { outdent.replacement->range, outdent.replacement->text } }))
    {
        mAutoOutdent = outdent.next;
    }
}

bool ALTextView::toggleComment()
{
    if (mReadOnly || !mHighlighter.grammar() || mHighlighter.grammar()->lineComment().empty())
    {
        return false;
    }
    if (hasOtherSelections())
    {
        // In or out the same way at every selection.
        const std::string token = mHighlighter.grammar()->lineComment();
        editGroups([this, &token](const std::vector<ALTextRange>& all) { return ALTextEditing::toggleComment(mDocument, all, token); });
        return true;
    }
    if (const std::optional<ALTextEditing::Change> change = ALTextEditing::toggleComment(mDocument, mAnchor, mCaret, mHighlighter.grammar()->lineComment()))
    {
        apply(*change);
    }
    return true;
}

bool ALTextView::perform(ALEditorCommand command)
{
    typedef ALEditorCommand C;
    if (mReadOnly && editsText(command))
    {
        return false;
    }
    // At every selection, where there are several.
    if (hasOtherSelections())
    {
        if (const std::optional<bool> done = performAtEach(command))
        {
            return *done;
        }
    }
    switch (command)
    {
        case C::None:
        case C::COUNT:
            return false;
        case C::MoveLeft:
        case C::MoveRight:
        case C::MoveUp:
        case C::MoveDown:
        case C::MoveWordLeft:
        case C::MoveWordRight:
        case C::MoveLineStart:
        case C::MoveLineEnd:
        case C::MoveDocStart:
        case C::MoveDocEnd:
        case C::MovePageUp:
        case C::MovePageDown:
        case C::SelectLeft:
        case C::SelectRight:
        case C::SelectUp:
        case C::SelectDown:
        case C::SelectWordLeft:
        case C::SelectWordRight:
        case C::SelectLineStart:
        case C::SelectLineEnd:
        case C::SelectDocStart:
        case C::SelectDocEnd:
        case C::SelectPageUp:
        case C::SelectPageDown:
        case C::MoveSubwordLeft:
        case C::MoveSubwordRight:
        case C::SelectSubwordLeft:
        case C::SelectSubwordRight:
        {
            if ((command == C::MoveUp || command == C::MoveDown) && stepGap(command == C::MoveDown))
            {
                return true;
            }
            const ALTextRange to = moved(command, selection(), mDesiredX);
            placeSelection(to.begin, to.end);
            scrollToCaret();
            return true;
        }
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
                // In a line's indentation, a level's spaces at once.
                const std::optional<ALTextPos> level = ALTextIndent::backspaceFrom(mDocument, mCaret, editingOptions());
                deleteRange(ALTextRange(level ? *level : mDocument.prevCluster(mCaret), mCaret));
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
            deleteRange(hasSelection() ? selection() : ALTextRange(wordStep(mCaret, false, false), mCaret));
            return true;
        case C::DeleteWordRight:
            deleteRange(hasSelection() ? selection() : ALTextRange(mCaret, wordStep(mCaret, true, false)));
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
            newLine();
            return true;
        case C::Indent:
            // A selection, however small, indents its lines; a caret
            // alone puts a tab in.
            if (hasSelection())
            {
                apply(ALTextIndent::indentLines(mDocument, mAnchor, mCaret, true, editingOptions()));
            }
            else
            {
                insertText(tabText(mCaret));
            }
            return true;
        case C::Unindent:
            apply(ALTextIndent::indentLines(mDocument, mAnchor, mCaret, false, editingOptions()));
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
            apply(ALTextEditing::duplicateLines(mDocument, mAnchor, mCaret));
            return true;
        case C::MoveLineUp:
        case C::MoveLineDown:
            if (const std::optional<ALTextEditing::Change> change = ALTextEditing::moveLines(mDocument, mAnchor, mCaret, command == C::MoveLineUp ? -1 : 1))
            {
                apply(*change);
            }
            return true;
        case C::DeleteLine:
            apply(ALTextEditing::deleteLines(mDocument, mAnchor, mCaret));
            return true;
        case C::PreviousChange:
        case C::NextChange:
            return goToChange(command == C::PreviousChange ? -1 : 1);
        case C::NextFunction:
        case C::PreviousFunction:
        case C::SelectFunction:
        case C::GoToMatchingBracket:
        case C::ExpandSelection:
        case C::ShrinkSelection:
        case C::SelectNextOccurrence:
        case C::ChangeAllOccurrences:
            return mFeatures && mFeatures->performFeature(command);
        case C::JoinLines:
        {
            // The lines selected, or the caret's and the next.
            const auto [first, last] = ALTextEditing::selectedLines(ALTextRange(mAnchor, mCaret).normalised());
            if (const std::optional<ALTextEditing::Change> join = ALTextEditing::joinLines(mDocument, first, llmax(last, first + 1), false))
            {
                apply(*join);
                return true;
            }
            return false;
        }
        case C::InsertLineBelow:
            // As Return at the line's end would make it.
            placeCaret(mDocument.lineEnd(mCaret.line), false);
            newLine();
            scrollToCaret();
            return true;
        case C::InsertLineAbove:
        {
            // As far in as the caret's line.
            const std::string indent = ALTextIndent::leadingBlanks(mDocument, mCaret.line);
            const ALTextPos   start  = mDocument.lineStart(mCaret.line);
            const ALTextPos   caret(mCaret.line, static_cast<S32>(indent.size()));
            mUndo.beginGroup();
            editMany({ { ALTextRange(start, start), indent + "\n" } }, caret);
            mUndo.endGroup();
            placeCaret(caret, false);
            afterEdit();
            scrollToCaret();
            return true;
        }
        case C::SelectLine:
        {
            // The lines the selection reaches, whole with their breaks; so
            // lines selected whole already take the next with them.
            const ALTextRange range = selection().normalised();
            const S32         past  = range.end.line + 1;
            const ALTextPos   end   = past < mDocument.lineCount() ? mDocument.lineStart(past) : mDocument.lineEnd(mDocument.lineCount() - 1);
            placeSelection(mDocument.lineStart(range.begin.line), end);
            mDesiredX = -1.f;
            scrollToCaret();
            return true;
        }
        case C::Fold:
        case C::Unfold:
        case C::FoldAll:
        case C::UnfoldAll:
        case C::Complete:
        case C::SignatureHelp:
        case C::QuickFix:
        case C::GoToDefinition:
        case C::FindReferences:
        case C::Rename:
            return mFeatures && mFeatures->performFeature(command);
        case C::NextMisspelling:
        case C::PreviousMisspelling:
            return goToMisspelling(command == C::NextMisspelling);
        case C::AddCaretAbove:
        case C::AddCaretBelow:
            return !mReadOnly && addCarets(command == C::AddCaretAbove ? -1 : 1);
        case C::ColumnSelectLeft:
            return !mReadOnly && growColumn(-1, 0);
        case C::ColumnSelectRight:
            return !mReadOnly && growColumn(1, 0);
        case C::ColumnSelectUp:
            return !mReadOnly && growColumn(0, -1);
        case C::ColumnSelectDown:
            return !mReadOnly && growColumn(0, 1);
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
        case C::GoToDefinition:
        case C::FindReferences:
        case C::Rename:
            return mFeatures && mFeatures->canPerformFeature(command);
        case C::QuickFix:
            return !mReadOnly && mFeatures && mFeatures->canPerformFeature(command);
        case C::NextMisspelling:
        case C::PreviousMisspelling:
            return getSpellCheck();
        case C::NextFunction:
        case C::PreviousFunction:
        case C::SelectFunction:
        case C::GoToMatchingBracket:
        case C::ExpandSelection:
        case C::ShrinkSelection:
        case C::SelectNextOccurrence:
        case C::ChangeAllOccurrences:
            return mFeatures && mFeatures->canPerformFeature(command);
        case C::AddCaretAbove:
        case C::AddCaretBelow:
        case C::ColumnSelectLeft:
        case C::ColumnSelectRight:
        case C::ColumnSelectUp:
        case C::ColumnSelectDown:
            // A read-only text shows no caret to add to or grow from.
            return !mReadOnly;
        case C::PreviousChange:
            return mChangeAt > 0;
        case C::NextChange:
            return mChangeAt + 1 < static_cast<S32>(mChanges.size());
        default:
            return !(mReadOnly && editsText(command));
    }
}

S32 ALTextView::screenTopOf(const LLRect& text, S32 line, S32 row)
{
    return text.mTop - (mLayout.lineTop(line) + mLayout.rowTop(line, row) - mScrollY);
}

LLRect ALTextView::anchorOf(const ALTextPos& at)
{
    const LLRect text = textRect();
    S32          row;
    const F32    x    = mLayout.xOf(at.line, at.column, &row);
    const S32    top  = screenTopOf(text, at.line, row);
    const S32    left = static_cast<S32>(static_cast<F32>(text.mLeft) - mScrollX + x);
    return LLRect(left, top, left, top - mLayout.rowHeightOf(at.line, row));
}

LLRect ALTextView::anchorOf(const ALTextRange& range)
{
    const LLRect text = textRect();
    S32          row;
    mLayout.xOf(range.begin.line, range.begin.column, &row);
    const S32 top = screenTopOf(text, range.begin.line, row);
    LLRect    anchor(text.mLeft, top, text.mRight, top - mLayout.rowHeightOf(range.begin.line, row));
    F32       x0, x1;
    if (!range.empty() && spanOnRow(range.begin.line, row, range, x0, x1))
    {
        const F32 left = static_cast<F32>(text.mLeft) - mScrollX;
        anchor.mLeft   = static_cast<S32>(left + x0);
        anchor.mRight  = static_cast<S32>(left + x1);
    }
    return anchor;
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

void ALTextView::forEachVisibleGap(const LLRect& text, const std::function<void(S32, S32, S32)>& visit)
{
    if (!mAnyGap || mLayout.rowHeight() <= 0)
    {
        return;
    }
    const S32 count    = mDocument.lineCount();
    const S32 bottom_y = mScrollY + text.getHeight();
    const auto shown   = [&](S32 line) {
        const S32 top    = mLayout.gapTop(line);
        const S32 height = mLayout.gapHeight(line);
        if (height > 0 && top < bottom_y && top + height > mScrollY)
        {
            visit(line, text.mTop - (top - mScrollY), height);
        }
        return top < bottom_y;
    };
    for (S32 line = mLayout.lineAtY(mScrollY); line < count; ++line)
    {
        if (!mLayout.hidden(line) && !shown(line))
        {
            return;
        }
    }
    shown(count);
}

// --- LLEditMenuHandler ---------------------------------------------------------

void ALTextView::undo()
{
    if (mReadOnly)
    {
        return;
    }
    // The steps are of the text without a composition in it.
    if (hasPreedit())
    {
        resetPreedit();
    }
    std::vector<ALTextRange> others;
    if (std::optional<ALTextRange> selected = mUndo.undo(&others))
    {
        mCarets.assign(clamped(std::move(others)));
        placeSelection(selected->begin, selected->end);
        afterEdit();
    }
}

void ALTextView::redo()
{
    if (mReadOnly)
    {
        return;
    }
    if (hasPreedit())
    {
        resetPreedit();
    }
    std::vector<ALTextRange> others;
    if (std::optional<ALTextRange> selected = mUndo.redo(&others))
    {
        mCarets.assign(clamped(std::move(others)));
        placeSelection(selected->begin, selected->end);
        afterEdit();
    }
}

// static
std::string ALTextView::sClippedLine;

void ALTextView::cut()
{
    if (!canCut())
    {
        return;
    }
    copy();
    if (hasOtherSelections())
    {
        // What each selected; or where none selected anything, each caret's
        // line whole.
        if (anySelected())
        {
            deleteSelectedEach();
        }
        else
        {
            editGroups([this](const std::vector<ALTextRange>& all) { return ALTextEditing::deleteLines(mDocument, all); });
        }
        return;
    }
    if (!hasSelection())
    {
        // The whole line, the caret on the one that takes its place.
        apply(ALTextEditing::deleteLines(mDocument, mCaret, mCaret));
        return;
    }
    deleteRange(selection());
}

void ALTextView::copy()
{
    if (hasOtherSelections())
    {
        // Each selection's text, one to a line in the order they begin: or
        // a caret's whole line where lines are clipped and none selects
        // anything -- what a cut takes -- else nothing of it.
        std::string text;
        bool        any      = false;
        const bool  selected = anySelected();
        for (const ALTextRange& one : selectionsInOrder())
        {
            if (one.empty() && (!mClipsLines || selected))
            {
                continue;
            }
            if (any)
            {
                text += '\n';
            }
            text += one.empty() ? mDocument.line(one.end.line) : mDocument.text(one);
            any = true;
        }
        if (any)
        {
            sClippedLine.clear();
            LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size()));
        }
        return;
    }
    if (!hasSelection())
    {
        if (!mClipsLines)
        {
            return;
        }
        // The caret's line whole, with its break, to go in as a line.
        sClippedLine = mDocument.line(mCaret.line) + "\n";
        LLClipboard::instance().copyToClipboard(sClippedLine, 0, static_cast<S32>(sClippedLine.size()));
        return;
    }
    sClippedLine.clear();
    const std::string text = selectedText();
    LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size()));
}

void ALTextView::setLineAnnotations(std::vector<LineAnnotation> lines)
{
    // One more than the text has lines: the gap below it.
    const size_t count = static_cast<size_t>(mDocument.lineCount());
    mEndAnnotation     = lines.size() > count ? lines[count] : LineAnnotation();
    lines.resize(std::min(lines.size(), count));
    const bool had_gap = mAnyGap;
    mAnyGap = mEndAnnotation.gap > 0 || std::any_of(lines.begin(), lines.end(), [](const LineAnnotation& line) { return line.gap > 0; });
    mAnnotations.assign(std::make_move_iterator(lines.begin()), std::make_move_iterator(lines.end()));
    ++mAnnotationsRevision;
    if (mAnyGap || had_gap)
    {
        provideGaps();
    }
}

void ALTextView::setLineAnnotation(S32 line, const LineAnnotation& said)
{
    const S32 count = mDocument.lineCount();
    if (line < 0 || line > count)
    {
        return;
    }
    const S32 had = lineAnnotation(line).gap;
    if (line == count)
    {
        mEndAnnotation = said;
    }
    else
    {
        if (static_cast<S32>(mAnnotations.size()) <= line)
        {
            mAnnotations.resize(static_cast<size_t>(line) + 1);
        }
        mAnnotations[static_cast<size_t>(line)] = said;
    }
    ++mAnnotationsRevision;
    if (said.gap != had)
    {
        if (!mAnyGap && said.gap > 0)
        {
            mAnyGap = true;
            provideGaps();
        }
        else
        {
            mLayout.gapChanged(line);
        }
    }
}

const ALTextView::LineAnnotation& ALTextView::lineAnnotation(S32 line) const
{
    static const LineAnnotation nothing;
    if (line == mDocument.lineCount())
    {
        return mEndAnnotation;
    }
    return line >= 0 && line < static_cast<S32>(mAnnotations.size()) ? mAnnotations[static_cast<size_t>(line)] : nothing;
}

void ALTextView::provideGaps()
{
    // Asked of the layout only where some line has one.
    if (mAnyGap)
    {
        mLayout.setGapProvider([this](S32 line) { return lineAnnotation(line).gap; });
    }
    else
    {
        mLayout.setGapProvider(nullptr);
    }
}

S32 ALTextView::caretGap() const
{
    // A gap since let go of, or hidden with its line, holds the caret no
    // longer.
    return mCaretGap >= 0 && gapStops(mCaretGap) ? mCaretGap : -1;
}

bool ALTextView::gapStops(S32 line) const
{
    return mAnyGap && lineAnnotation(line).gapStop && mLayout.gapRows(line) > 0;
}

bool ALTextView::stepGap(bool down)
{
    const S32 count = mDocument.lineCount();
    if (const S32 gap = caretGap(); gap >= 0)
    {
        // On out of it: down to the line under it, up to the last row of
        // the line over it; nowhere, where there is none.
        const S32 line = down ? (gap < count ? gap : -1) : mLayout.visibleFrom(gap - 1, -1);
        if (line >= 0)
        {
            const S32       row = down ? 0 : mLayout.rowCount(line) - 1;
            const ALTextPos to(line, mLayout.columnAt(line, row, llmax(0.f, mDesiredX), true));
            placeSelection(to, to);
            scrollToCaret();
        }
        return true;
    }
    if (!mAnyGap || hasSelection())
    {
        return false;
    }
    // Into the stop the step would pass over: under the caret's line's last
    // row, or over its first.
    S32       row;
    const F32 x   = mLayout.xOf(mCaret.line, mCaret.column, &row);
    S32       gap = -1;
    if (down)
    {
        if (row + 1 < mLayout.rowCount(mCaret.line))
        {
            return false;
        }
        const S32 next = mLayout.visibleFrom(mCaret.line + 1, 1);
        gap            = next >= 0 ? next : count;
    }
    else if (row == 0)
    {
        gap = mCaret.line;
    }
    if (gap < 0 || !gapStops(gap))
    {
        return false;
    }
    if (mDesiredX < 0.f)
    {
        mDesiredX = x;
    }
    const ALTextPos at = gap < count ? ALTextPos(gap, 0) : mDocument.end();
    placeSelection(at, at);
    mCaretGap = gap;
    mCaretMoved();
    // The gap in sight, as a row is.
    const S32 top    = mLayout.gapTop(gap);
    const S32 height = mLayout.gapHeight(gap);
    const S32 page   = llmax(1, textRect().getHeight());
    if (top < mScrollY)
    {
        setScrollY(top);
    }
    else if (top + height > mScrollY + page)
    {
        setScrollY(top + height - page);
    }
    return true;
}

S32 ALTextView::gapAtLocal(S32 y)
{
    if (!mAnyGap)
    {
        return -1;
    }
    return mLayout.gapAtY((textRect().mTop - y) + mScrollY);
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
    if (hasOtherSelections())
    {
        // Its lines, as the text will have them.
        LLStringUtil::replaceString(text, "\r\n", "\n");
        LLStringUtil::replaceChar(text, '\r', '\n');
        std::vector<std::string> lines;
        for (size_t from = 0;;)
        {
            const size_t end = text.find('\n', from);
            lines.push_back(text.substr(from, end == std::string::npos ? std::string::npos : end - from));
            if (end == std::string::npos)
            {
                break;
            }
            from = end + 1;
        }
        // One line to each selection where there is a line for each -- a
        // break after the last not counted -- and the whole to each
        // otherwise.
        const size_t count = mCarets.size() + 1;
        if (lines.size() == count + 1 && lines.back().empty())
        {
            lines.pop_back();
        }
        const bool one_each = lines.size() == count;
        mUndo.beginGroup();
        editEach([&](size_t index, const ALTextRange& selection) {
            const std::string&    put  = one_each ? lines[index] : text;
            const ALTextRange     over = selection.normalised();
            ALTextEditing::Change one;
            one.replacements.push_back({ over, put });
            one.caret = ALTextEditing::endOf(over.begin, put);
            return std::optional<ALTextEditing::Change>(std::move(one));
        });
        mUndo.endGroup();
        return;
    }
    // A line copied whole goes in as a line, above the caret's, the caret
    // where it was in its own.
    if (mClipsLines && !hasSelection() && !sClippedLine.empty() && text == sClippedLine)
    {
        const ALTextPos caret(mCaret.line + 1, mCaret.column);
        mUndo.beginGroup();
        editMany({ { ALTextRange(mDocument.lineStart(mCaret.line), mDocument.lineStart(mCaret.line)), text } }, caret);
        mUndo.endGroup();
        placeCaret(caret, false);
        afterEdit();
        return;
    }
    // Lines pasted into a line's indentation brought to where they go, as
    // a step of its own: Undo takes back the re-indenting first, and
    // leaves the paste as it was copied.
    const ALTextRange                           into = selection().normalised();
    // Cut to what there is room for first, so that the lines re-indented
    // are the lines that went in.
    text.resize(fitting(into, text).size());
    const std::optional<ALTextIndent::PastePlan> plan =
        mReindentsPaste ? ALTextIndent::planPaste(mDocument, into, text, mHighlighter.grammar().get(), editingOptions()) : std::nullopt;
    mUndo.beginGroup();
    insertText(text);
    mUndo.endGroup();
    if (plan)
    {
        if (const std::optional<ALTextEditing::Change> change = ALTextIndent::reindentPasted(mDocument, into.begin, *plan, mCaret, editingOptions()))
        {
            apply(*change);
        }
    }
}

void ALTextView::doDelete()
{
    if (!canDoDelete())
    {
        return;
    }
    if (hasOtherSelections())
    {
        deleteSelectedEach();
        return;
    }
    deleteRange(selection());
}

void ALTextView::selectAll()
{
    placeSelection(mDocument.start(), mDocument.end());
    mDesiredX = -1.f;
}

void ALTextView::deselect()
{
    placeSelection(mCaret, mCaret);
}

// --- the input method ----------------------------------------------------------

void ALTextView::allowLanguageInput(bool allow)
{
    mLanguageInput = allow;
    if (LLWindow* window = getWindow())
    {
        window->allowLanguageTextInput(this, allow);
    }
}

void ALTextView::syncLanguageInput()
{
    const bool allow = hasFocus() && takesComposition();
    if (allow != mLanguageInput)
    {
        allowLanguageInput(allow);
    }
}

ALTextRange ALTextView::preeditRange() const
{
    return ALTextRange(mPreeditBegin, ALTextPos(mPreeditBegin.line, mPreeditBegin.column + mPreeditLength));
}

void ALTextView::resetPreedit()
{
    // A composition begun over a selection types over it -- but not over
    // one a modal keymap made, which is not being typed into.
    if (hasSelection() && !hasPreedit() && takesComposition())
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
    if (!takesComposition())
    {
        // What was being composed goes; nothing new is: the character it
        // commits is the keymap's.
        resetPreedit();
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
    if (!takesComposition())
    {
        return;
    }
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
    // Each command's keys beside it, as this view's keymap has them now:
    // the person's own, where they changed them.
    for (LLView* child : *menu->getChildList())
    {
        LLMenuItemGL* item = dynamic_cast<LLMenuItemGL*>(child);
        const std::optional<ALEditorCommand> command = item ? alEditorCommandFromName(item->getName()) : std::nullopt;
        if (!command)
        {
            continue;
        }
        KEY  key  = KEY_NONE;
        MASK mask = MASK_NONE;
        mKeymap.keysFor(*command, key, mask);
        item->setShownAccelerator(key, mask);
    }
    // The dictionary's items, where the menu has them: the suggestions
    // show themselves, the rest are shown here.
    const bool misspelled = getSpellCheck() && !mSpelling.suggestedFor().empty();
    menu->setItemVisible("Suggestion Separator", misspelled && !mSpelling.suggestions().empty());
    menu->setItemVisible("Add to Dictionary", misspelled);
    menu->setItemVisible("Add to Ignore", misspelled);
    menu->setItemVisible("Spellcheck Separator", misspelled);
    // What the name the click landed on is about, where anyone would say.
    for (const char* symbol : { "go_to_definition", "find_references", "rename", "Symbol Separator" })
    {
        menu->setItemVisible(symbol, mFeatures && mFeatures->offersSymbols());
    }
    // What would put right a problem at the caret, where there is one.
    const bool fixes = !mReadOnly && mFeatures && mFeatures->canPerformFeature(ALEditorCommand::QuickFix);
    menu->setItemVisible("quick_fix", fixes);
    menu->setItemVisible("Fix Separator", fixes);
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
        mFindBar->onChanged([this]() { queryChanged(); });
        mFindBar->onNext([this]() { findNext(true); });
        mFindBar->onPrevious([this]() { findNext(false); });
        mFindBar->onReplace([this]() { replaceMatch(); });
        mFindBar->onReplaceAll([this]() { replaceAllMatches(); });
        mFindBar->onSelectAll([this]() { selectAllMatches(); });
        mFindBar->onClose([this]() { hideFind(); });
    }
    // Seeded with what is selected, when that is a line's worth or less,
    // or with the word the caret is in when nothing is; written as a
    // pattern that finds it, where the bar is looking for patterns. Never
    // with an atom's placeholder, which is no text to type or find: a
    // selection holding an atom -- a notecard's item -- leaves the last
    // query, and the word stops at an atom.
    const ALTextRange sel = selection().normalised();
    std::string       seed;
    const auto        holds_atom = [this](const ALTextRange& range) {
        const auto first =
            std::lower_bound(mAtoms.begin(), mAtoms.end(), range.begin, [](const Atom& a, const ALTextPos& p) { return a.at < p; });
        return first != mAtoms.end() && first->at < range.end;
    };
    if (!sel.empty() && sel.begin.line == sel.end.line && sel.end.column - sel.begin.column < 200 && !holds_atom(sel))
    {
        seed = mDocument.text(sel);
    }
    else if (sel.empty())
    {
        const std::string& line  = mDocument.line(mCaret.line);
        size_t             begin = static_cast<size_t>(llclamp(mCaret.column, 0, static_cast<S32>(line.size())));
        size_t             end   = begin;
        const auto         word  = [&](size_t at) {
            return alWordByte(line[at]) && !atomAt(ALTextPos(mCaret.line, static_cast<S32>(at)));
        };
        while (begin > 0 && word(begin - 1))
        {
            --begin;
        }
        while (end < line.size() && word(end))
        {
            ++end;
        }
        seed = line.substr(begin, end - begin);
    }
    if (!seed.empty())
    {
        if (mFindBar->options().regex)
        {
            std::string escaped;
            for (const char c : seed)
            {
                if (strchr("\\^$.|?*+()[]{}", c))
                {
                    escaped += '\\';
                }
                escaped += c;
            }
            seed.swap(escaped);
        }
        // Quietly: the bar is looked through once, below.
        mFindBar->setQuery(seed, false);
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
    mFind.clear();
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

void ALTextView::queryChanged()
{
    // A text of a script's size looked through at once, as the query is
    // typed; a longer one once the query has stopped changing for a
    // moment, as it is after an edit, not at every key.
    constexpr size_t AT_ONCE = 256 * 1024;
    if (mDocument.byteCount() > AT_ONCE)
    {
        mFind.stale();
        return;
    }
    refreshFind();
}

void ALTextView::findChanged()
{
    if (findShown())
    {
        mFind.stale();
    }
}

void ALTextView::refreshFind()
{
    if (!findShown())
    {
        mFind.clear();
        return;
    }
    mFind.search(mDocument, mFindBar->query(), mFindBar->options(), mFindBar->inSelection(), selection().normalised());
    // Said once a worker's are in, where they are on their way.
    if (!mFind.searching())
    {
        findCounted();
    }
}

void ALTextView::findCounted()
{
    if (mFindBar)
    {
        mFindBar->setCount(mFind.current(), static_cast<S32>(mFind.count()), mFind.error(), mFind.capped());
    }
}

bool ALTextView::findNext(bool forward)
{
    if (!findShown())
    {
        showFind(false);
    }
    settleFind();
    if (mFind.count() == 0)
    {
        return false;
    }
    const ALTextRange sel  = selection().normalised();
    const ALTextPos   from = hasSelection() ? (forward ? sel.end : sel.begin) : mCaret;
    const S32         index = mFind.nearest(from, forward);
    if (index < 0)
    {
        return false;
    }
    const ALTextRange& match = mFind.matches()[static_cast<size_t>(index)];
    // Round the text's end: on to one before where it started, or back to
    // one at or after it.
    const S32 wrapped = forward ? (match.begin < from ? 1 : 0) : (match.begin < from ? 0 : -1);
    mFind.setCurrent(index);
    // The match alone, the other carets let go, from the bar's buttons as
    // from the keys.
    singleSelection();
    setSelection(match);
    mFindBar->setCount(mFind.current(), static_cast<S32>(mFind.count()), mFind.error(), mFind.capped(), wrapped);
    return true;
}

bool ALTextView::replaceMatch()
{
    if (mReadOnly || !findShown())
    {
        return false;
    }
    settleFind();
    const S32 current = mFind.current();
    if (current < 0 || current >= static_cast<S32>(mFind.count()) || mFind.matches()[static_cast<size_t>(current)] != selection().normalised())
    {
        return findNext(true);
    }
    const ALTextRange match = mFind.matches()[static_cast<size_t>(current)];
    const std::string with  = ALTextSearch::replacement(mDocument, match, mFindBar->query(), mFindBar->options(), mFindBar->replacement());
    setSelection(match);
    insertText(with);
    findNext(true);
    return true;
}

S32 ALTextView::replaceAllMatches()
{
    settleFind();
    if (mReadOnly || !findShown() || mFind.count() == 0)
    {
        return 0;
    }
    // Every match, not only those the bar lists.
    std::vector<std::pair<ALTextRange, std::string>> edits = mFind.replacements(mDocument, mFindBar->query(), mFindBar->options(), mFindBar->replacement());
    const S32                                        count = static_cast<S32>(edits.size());
    // The matches let go of before the edits rather than slid through each
    // of them: every one is cut through by its own replacement, and the
    // text is looked through again once the edits settle. Where nothing
    // changed, they stay as they were.
    const S32                current = mFind.current();
    std::vector<ALTextRange> matches = mFind.take();
    if (replaceAll(std::move(edits)))
    {
        return count;
    }
    mFind.restore(std::move(matches), current);
    return 0;
}

// --- the bars ---------------------------------------------------------------------------

LLRect ALTextView::rulerRect() const
{
    if (mScrollMap || !mNeedV)
    {
        return LLRect();
    }
    const LLRect local = bodyRect();
    return LLRect(local.mRight - ALTextRuler::WIDTH, local.mTop, local.mRight, local.mBottom);
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

LLRect ALTextView::hThumb(const LLRect& track)
{
    const S32 content = llmax(1, static_cast<S32>(ceilf(mLayout.contentWidth())) + H_MARGIN);
    const S32 width   = llmax(1, textRect().getWidth());
    const S32 track_w = llmax(1, track.getWidth());
    const S32 thumb_w = llclamp(track_w * width / content, llmin(ALTextRuler::THUMB_MIN, track_w), track_w);
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
    // The thumb along the bottom, while wanted; the ruler down the side is
    // its own view (ALTextRuler).
    const F32    shown = barAlpha() * alpha;
    const LLRect bar   = hBarRect();
    if (bar.notEmpty() && shown > 0.f)
    {
        gl_rect_2d(hThumb(bar), textColor() % (0.35f * shown));
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
    // From the first token that reaches the row, where the glyphs go
    // forward through the line: a long line wraps into many rows.
    const auto ends_before = [&](const ALSyntaxToken& token) { return token.end <= row.begin; };
    size_t     t = laid.ordered ? static_cast<size_t>(std::partition_point(tokens.begin(), tokens.end(), ends_before) - tokens.begin()) : 0;
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

ALTextLayout::Row ALTextView::rowInSight(const ALTextLayout::Line& laid, const ALTextLayout::Row& row, const LLRect& text) const
{
    // A glyph may reach past its advance -- an italic's overhang, a wide
    // mark -- so those just past the text's edges are drawn as well.
    constexpr F32 OVERHANG = 16.f;
    return ALTextLayout::rowWithin(laid, row, mScrollX - OVERHANG, mScrollX + static_cast<F32>(text.getWidth()) + OVERHANG);
}

void ALTextView::drawSquiggle(F32 x0, F32 x1, S32 y, const LLColor4& color, const LLRect& clip)
{
    const Squiggle one{ x0, x1, y, color, clip.mLeft, clip.mRight };
    drawSquiggles(&one, 1);
}

void ALTextView::drawSquiggles(const Squiggle* squiggles, size_t count)
{
    LLImageGL* wave = squiggleTexture();
    if (count == 0 || !wave || !wave->getHasGLTexture())
    {
        return;
    }
    gGL.getTextureSlot(0)->bindSampled(wave, ALSamplers::BilinearWrap);
    gGL.begin(LLRender::TRIANGLES);
    for (size_t i = 0; i < count; ++i)
    {
        // As much of it as is in sight, the wave kept where it began.
        const Squiggle& squiggle = squiggles[i];
        const F32       from     = llmax(squiggle.x0, static_cast<F32>(squiggle.clipLeft));
        const F32       to       = llmin(squiggle.x1, static_cast<F32>(squiggle.clipRight));
        if (from >= to)
        {
            continue;
        }
        const F32 u0     = (from - squiggle.x0) / static_cast<F32>(SQUIGGLE_WAVE);
        const F32 u1     = (to - squiggle.x0) / static_cast<F32>(SQUIGGLE_WAVE);
        const F32 bottom = static_cast<F32>(squiggle.y - SQUIGGLE_HEIGHT / 2);
        const F32 top    = static_cast<F32>(squiggle.y + SQUIGGLE_HEIGHT / 2);
        gGL.color4fv(squiggle.color.mV);
        gGL.texCoord2f(u0, 1.f);
        gGL.vertex2f(from, top);
        gGL.texCoord2f(u0, 0.f);
        gGL.vertex2f(from, bottom);
        gGL.texCoord2f(u1, 0.f);
        gGL.vertex2f(to, bottom);
        gGL.texCoord2f(u0, 1.f);
        gGL.vertex2f(from, top);
        gGL.texCoord2f(u1, 0.f);
        gGL.vertex2f(to, bottom);
        gGL.texCoord2f(u1, 1.f);
        gGL.vertex2f(to, top);
    }
    gGL.end();
    // What is drawn next without a texture of its own gets the white one.
    gGL.getTextureSlot(0)->unbind();
}

// static
S32 ALTextView::squiggleMiddle(S32 text_top, S32 ascent)
{
    return text_top - ascent - SQUIGGLE_BELOW;
}

void ALTextView::squiggle(F32 x0, F32 x1, S32 y, const LLColor4& color, const LLRect& clip)
{
    if (mQueueSquiggles)
    {
        mSquiggles.push_back(Squiggle{ x0, x1, y, color, clip.mLeft, clip.mRight });
    }
    else
    {
        drawSquiggle(x0, x1, y, color, clip);
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
        const ALTextLayout::Row seen = rowInSight(laid, row, text);
        for (size_t k = seen.glyphBegin; k < seen.glyphEnd; ++k)
        {
            const ALTextLayout::Glyph& glyph = laid.glyphs[k];
            if (!isAtomId(glyph.substitution))
            {
                continue;
            }
            // By where the glyph stands rather than by the index it was
            // laid out with, which atoms made or taken away before it on
            // another line have moved on since.
            const Atom* found = atomAt(ALTextPos(line, glyph.cluster));
            if (!found || found->at.column != glyph.cluster)
            {
                continue;
            }
            const Atom& atom = *found;
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
                squiggle(left + x0, left + x1, squiggleMiddle(screen_top, row.ascent), mSpellErrorColor.get() % alpha, text);
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
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
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
    const bool caret_on    = show_caret && (!mCaretBlink || blink < BLINK_DELAY || (static_cast<S32>(blink * 2.f) & 1));
    const S32  in_gap      = caretGap();
    const ALTextRange sel  = selection().normalised();
    const F32  space       = mLayout.xOf(0, 0) + 6.f;  // what a selected line end is drawn as

    const F32  left        = static_cast<F32>(text.mLeft) - mScrollX;

    // The rows in sight, walked in three passes -- what is behind the
    // text, the text, and what is over it -- so that each pass goes to GL
    // as a batch, rather than every row making draws of its own. A row's
    // things over the text lie over the next row's text, where they reach
    // it, rather than under.
    mRowsSeen.clear();
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
            mRowsSeen.push_back(RowSeen{ line, static_cast<S32>(r), row_screen_top, row_screen_top - row.textTop() });
        }
    }

    // Behind the text: the selections and what the find bar found.
    const std::vector<ALTextRange>& matches   = mFind.matches();
    const LLColor4                  sel_color = selectionDrawColor() % alpha;
    gGL.getTextureSlot(0)->unbind();
    gGL.begin(LLRender::TRIANGLES);
    for (const RowSeen& seen : mRowsSeen)
    {
        const S32                 line   = seen.line;
        const ALTextLayout::Line& laid   = mLayout.line(line);
        const ALTextLayout::Row&  row    = laid.rows[static_cast<size_t>(seen.row)];
        const S32                 length = mDocument.lineLength(line);
        // A selection's band on the row, in order, from its start or the
        // row's to its end or the row's; past the line's end where it goes
        // on to the next.
        const auto band = [&](const ALTextRange& range) {
            if (range.empty() || line < range.begin.line || range.end.line < line)
            {
                return;
            }
            const S32  sel_begin = range.begin.line < line ? 0 : range.begin.column;
            const S32  sel_end   = range.end.line > line ? length + 1 : range.end.column;
            const S32  lo        = llmax(sel_begin, row.begin);
            const bool last_row  = (static_cast<size_t>(seen.row) + 1 == laid.rows.size());
            const S32  hi        = llmin(sel_end, last_row ? length + 1 : row.end);
            if (lo < hi)
            {
                const F32 x0 = mLayout.xOf(line, lo);
                const F32 x1 = hi > length ? row.width + space : (hi >= row.end && !last_row ? row.width : mLayout.xOf(line, hi));
                gl_rect_2d_in_batch(static_cast<S32>(left + x0), seen.rowScreenTop, static_cast<S32>(left + x1), seen.rowScreenTop - row.height,
                                    sel_color);
            }
        };
        band(sel);
        for (auto [it, end] = mCarets.onLine(line); it != end; ++it)
        {
            band(it->normalised());
        }
        if (!matches.empty())
        {
            auto first = std::lower_bound(matches.begin(), matches.end(), line, [](const ALTextRange& m, S32 l) { return m.end.line < l; });
            for (auto it = first; it != matches.end() && it->begin.line <= line; ++it)
            {
                F32 x0, x1;
                if (spanOnRow(line, seen.row, *it, x0, x1))
                {
                    gl_rect_2d_in_batch(static_cast<S32>(left + x0), seen.rowScreenTop, static_cast<S32>(left + x1), seen.rowScreenTop - row.height,
                                        mFindMatchColor.get() % alpha);
                }
            }
        }
    }
    gGL.end();

    // The text: every row's glyphs, as many as are in sight, in one call.
    mGlyphRuns.clear();
    mRunColours.clear();
    mRunColourAt.clear();
    for (const RowSeen& seen : mRowsSeen)
    {
        const ALTextLayout::Line& laid        = mLayout.line(seen.line);
        const ALTextLayout::Row&  row         = laid.rows[static_cast<size_t>(seen.row)];
        const ALTextLayout::Row   in_sight    = rowInSight(laid, row, text);
        const size_t              glyph_count = in_sight.glyphEnd - in_sight.glyphBegin;
        if (!glyph_count)
        {
            continue;
        }
        colorRow(seen.line, laid, in_sight, alpha);
        tintRow(seen.line, laid, in_sight, alpha, mColorScratch);
        mRunColourAt.push_back(mRunColours.size());
        mRunColours.insert(mRunColours.end(), mColorScratch.begin(), mColorScratch.begin() + static_cast<std::ptrdiff_t>(glyph_count));
        mGlyphRuns.push_back(LLFontGL::GlyphRun{ &laid.placed[in_sight.glyphBegin], nullptr, glyph_count, left - row.xStart,
                                                 static_cast<F32>(seen.screenTop - row.ascent) });
    }
    for (size_t i = 0; i < mGlyphRuns.size(); ++i)
    {
        mGlyphRuns[i].colors = mRunColours.data() + mRunColourAt[i];
    }
    mFont->renderGlyphRuns(mGlyphRuns.data(), mGlyphRuns.size());

    // Over the text; the squiggles, which share a texture, after the rest
    // of it and together.
    mQueueSquiggles = true;
    for (const RowSeen& seen : mRowsSeen)
    {
        const S32                 line           = seen.line;
        const size_t              r              = static_cast<size_t>(seen.row);
        const ALTextLayout::Line& laid           = mLayout.line(line);
        const ALTextLayout::Row&  row            = laid.rows[r];
        const S32                 row_screen_top = seen.rowScreenTop;
        const S32                 screen_top     = seen.screenTop;
        if (hasPreedit() && line == mPreeditBegin.line)
        {
            drawPreedit(line, row, screen_top, left, alpha);
        }

        drawLayers(line, laid, static_cast<S32>(r), text, row_screen_top, left, alpha);
        drawRowExtras(line, static_cast<S32>(r), text, screen_top, left, alpha);

        // The carets on the row, every one as the main one is.
        if (caret_on)
        {
            const auto caret = [&](const ALTextPos& at) {
                if (at.line != line)
                {
                    return;
                }
                S32       at_row = 0;
                const F32 at_x   = mLayout.xOf(at.line, at.column, &at_row);
                if (at_row != static_cast<S32>(r))
                {
                    return;
                }
                const S32  x     = static_cast<S32>(left + at_x);
                const bool modal = mModal && !mModal->inserting();
                if (modal || mCaretStyle != CaretStyle::Line)
                {
                    // A block over the cluster the caret is on, as a modal
                    // editor's is; a space's width past the line's end.
                    const ALTextPos next  = mDocument.nextCluster(at);
                    const F32       cell  = llmax(4.f, mLayout.columnWidth());
                    F32             right = next.line == at.line && next != at ? mLayout.xOf(at.line, next.column) : at_x + cell;
                    if (right <= at_x)
                    {
                        right = at_x + cell;
                    }
                    if (!modal && mCaretStyle == CaretStyle::Underline)
                    {
                        // A bar under the cluster, as thick as the line caret is wide.
                        mCaretBoxes.push_back({ LLRect(x, row_screen_top - row.height + CARET_WIDTH, static_cast<S32>(left + right), row_screen_top - row.height),
                                                mCursorColor.get() % alpha });
                    }
                    else
                    {
                        mCaretBoxes.push_back({ LLRect(x, row_screen_top, static_cast<S32>(left + right), row_screen_top - row.height),
                                                mCursorColor.get() % (0.55f * alpha) });
                    }
                }
                else
                {
                    mCaretBoxes.push_back({ LLRect(x, row_screen_top, x + CARET_WIDTH, row_screen_top - row.height), mCursorColor.get() % alpha });
                }
            };
            if (in_gap < 0)
            {
                caret(mCaret);
            }
            for (auto [it, end] = mCarets.onLine(line); it != end; ++it)
            {
                caret(it->end);
            }
        }
    }
    // A caret standing in a gap, at the start of its first row.
    if (caret_on && in_gap >= 0)
    {
        const S32 top = text.mTop - (mLayout.gapTop(in_gap) - mScrollY);
        const S32 x   = static_cast<S32>(left);
        mCaretBoxes.push_back({ LLRect(x, top, x + CARET_WIDTH, top - mLayout.rowHeight()), mCursorColor.get() % alpha });
    }
    // The carets together, over every row's layers.
    if (!mCaretBoxes.empty())
    {
        gGL.getTextureSlot(0)->unbind();
        gGL.begin(LLRender::TRIANGLES);
        for (const CaretBox& box : mCaretBoxes)
        {
            gl_rect_2d_in_batch(box.rect.mLeft, box.rect.mTop, box.rect.mRight, box.rect.mBottom, box.color);
        }
        gGL.end();
        mCaretBoxes.clear();
    }
    mQueueSquiggles = false;
    drawSquiggles(mSquiggles.data(), mSquiggles.size());
    mSquiggles.clear();
}

void ALTextView::dragSelectTo(S32 x, S32 y)
{
    mDragX = x;
    mDragY = y;
    // Past the top or the bottom, a step every twentieth of a second: a
    // row, and a row more for every row's height further past.
    const LLRect text  = textRect();
    const S32    row_h = llmax(1, mLayout.rowHeight());
    const F64    now   = LLTimer::getTotalSeconds();
    const S32    past  = y > text.mTop ? y - text.mTop : y < text.mBottom ? text.mBottom - y : 0;
    if (past > 0 && now >= mDragScrolled + 0.05)
    {
        mDragScrolled   = now;
        const S32 rows  = 1 + past / row_h;
        setScrollY(mScrollY + (y > text.mTop ? -rows : rows) * row_h);
    }
    if (mColumnDragging)
    {
        const ColumnCorner corner = cornerAtLocal(x, y);
        if (corner == mColumn.to)
        {
            return;
        }
        selectColumn(mColumn.from, corner);
        if (mModal)
        {
            mModal->mouseChanged(*this);
        }
        return;
    }
    const ALTextPos at        = posAtLocal(x, y, true);
    const ALTextPos character = posAtLocal(x, y, false);
    if (at == mDragAt && character == mDragToChar)
    {
        return;
    }
    mDragAt     = at;
    mDragToChar = character;
    placeSelection(mDragAnchor, at);
    mDesiredX = -1.f;
    if (mModal)
    {
        mModal->mouseChanged(*this);
    }
}

void ALTextView::pump()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    // A modal keymap's keys that wait on time; then its mode, moved by
    // something other than a key -- the mouse dragging vim into visual
    // mode, the time run out on keys held.
    if (mModal)
    {
        mModal->idle(*this);
    }
    syncLanguageInput();
    // A drag held past the top or the bottom scrolls on with the mouse
    // still, as a text field's does.
    if (mSelecting && hasMouseCapture())
    {
        const LLRect text = textRect();
        if (mDragY > text.mTop || mDragY < text.mBottom)
        {
            dragSelectTo(mDragX, mDragY);
        }
    }
    // The find bar in the view's colours as they are now: a theme chosen
    // while it is open recolours it, as it does the text.
    if (mFindBar && mFindBar->getVisible() && mFindBarColors != LLUIColorTable::instance().generation())
    {
        mFindBarColors = LLUIColorTable::instance().generation();
        mFindBar->setColors(backgroundColor(), textColor());
    }
    // The find bar's query looked for through the text again once edits
    // have stopped coming for a moment, not at every keystroke; and a
    // worker's matches taken as they come in.
    if (mFind.due())
    {
        refreshFind();
    }
    if (mFind.searching() && mFind.collect(mDocument, selection().normalised()))
    {
        findCounted();
    }
    if (mMisspellingSought)
    {
        seekMisspelling();
    }
    publishPrimary();
    syncScrollbar();
    trimLayout();
}

void ALTextView::draw()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    pump();
    const F32 alpha = getDrawContext().mAlpha;
    if (mBgVisible)
    {
        gl_rect_2d(getLocalRect(), backgroundColor() % alpha);
    }
    const LLRect text = textRect();
    // A tint behind each line that has one, and each gap, under
    // everything else.
    if (annotated())
    {
        // The first and last rows in sight may be partly out of it.
        LLLocalClipRect clip(text);
        const S32 row_h = layout().rowHeight();
        forEachVisibleRow(text, [&](S32 line, S32, S32 screen_top) {
            const LLColor4& tint = lineAnnotation(line).tint;
            if (tint.mV[VALPHA] > 0.f)
            {
                gl_rect_2d(text.mLeft, screen_top, text.mRight, screen_top - row_h, tint % alpha);
            }
        });
        forEachVisibleGap(text, [&](S32 line, S32 screen_top, S32 height) {
            const LLColor4& tint = lineAnnotation(line).gapTint;
            if (tint.mV[VALPHA] > 0.f)
            {
                gl_rect_2d(text.mLeft, screen_top, text.mRight, screen_top - height, tint % alpha);
            }
        });
    }
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
    // The map's preview over everything the view holds, the find bar and
    // a list or a card too.
    if (mRuler->getVisible())
    {
        LLUI::pushMatrix();
        LLUI::translate(static_cast<F32>(mRuler->getRect().mLeft), static_cast<F32>(mRuler->getRect().mBottom));
        mRuler->drawPreview(alpha);
        LLUI::popMatrix();
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
    // Its words and caret within it, however long the line typed.
    std::optional<LLLocalClipRect> clip(std::in_place, band);
    const LLFontGL* font = getFont();
    if (!font)
    {
        return;
    }
    // What to show, read from the keymap and measured only when it has
    // moved on since.
    BandShown& band_shown = mBandShown;
    if (band_shown.keymap != mModal.get() || band_shown.generation != mModal->generation() || band_shown.font != font)
    {
        band_shown.keymap     = mModal.get();
        band_shown.generation = mModal->generation();
        band_shown.font       = font;
        std::string line;
        S32         caret     = 0;
        band_shown.typing     = mModal->typingLine(line, caret);
        band_shown.shown      = band_shown.typing ? line : mModal->message();
        band_shown.error      = !band_shown.typing && mModal->messageIsError();
        band_shown.status     = !band_shown.typing && band_shown.shown.empty();
        if (band_shown.status)
        {
            band_shown.shown = mModal->status();
        }
        band_shown.caretX = band_shown.typing ? font->getWidth(band_shown.shown.substr(0, static_cast<size_t>(llclamp(caret, 0, static_cast<S32>(band_shown.shown.size())))))
                                              : 0;
        band_shown.items.clear();
        band_shown.lefts.clear();
        band_shown.widths.clear();
        band_shown.chosen = -1;
        band_shown.gap    = font->getWidth("  ");
        if (band_shown.typing && mModal->menu(band_shown.items, band_shown.chosen))
        {
            S32 at = 0;
            for (const std::string& item : band_shown.items)
            {
                const S32 width = font->getWidth(item);
                band_shown.lefts.push_back(at);
                band_shown.widths.push_back(width);
                at += width + band_shown.gap;
            }
        }
    }
    const bool         typing = band_shown.typing;
    const std::string& shown  = band_shown.shown;
    const LLColor4     colour = band_shown.status ? lerp(paper, ink, 0.8f) : band_shown.error ? mSpellErrorColor.get() : ink;
    const S32 x = local.mLeft + mHPad + 2;
    const S32 y = band.mBottom + 2 + static_cast<S32>(font->getDescenderHeight());
    font->renderUTF8(shown, 0, static_cast<F32>(x), static_cast<F32>(y), colour % alpha, LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                     S32_MAX, band.getWidth() - mHPad * 2 - 4, nullptr, true);
    // The block caret at the caret's byte, blinking as the text's does.
    const F32 blink = mBlink.getElapsedTimeF32();
    if (typing && keyboardOnText() && gFocusMgr.getAppHasFocus() && (blink < BLINK_DELAY || (static_cast<S32>(blink * 2.f) & 1)))
    {
        const S32 at    = band_shown.caretX;
        const S32 width = llmax(2, band_shown.gap / 2);
        gl_rect_2d(x + at, band.mTop - 2, x + at + width, band.mBottom + 2, ink % (0.6f * alpha));
    }
    // The keymap's row of choices, over the band on the text's last row
    // -- vim's wildmenu -- the one on the line set in the ink with the
    // ink for its ground, and the row slid left so that it shows.
    const std::vector<std::string>& items  = band_shown.items;
    const std::vector<S32>&         lefts  = band_shown.lefts;
    const S32                       chosen = band_shown.chosen;
    if (typing && !items.empty())
    {
        const S32    gap = band_shown.gap;
        const LLRect row(local.mLeft, band.mTop + mLayout.rowHeight() + 2, local.mRight, band.mTop);
        // Its own row, over the text's last; slid left so that the one
        // chosen shows, those before it cut at the view's edge.
        clip.reset();
        clip.emplace(row);
        gl_rect_2d(row, lerp(paper, ink, 0.12f) % alpha);
        S32 slide = 0;
        if (chosen >= 0 && chosen < static_cast<S32>(items.size()))
        {
            const S32 right = x + lefts[chosen] + band_shown.widths[chosen] + gap / 2;
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
                gl_rect_2d(left - gap / 4, row.mTop - 1, left + band_shown.widths[i] + gap / 4, row.mBottom + 1, ink % (0.85f * alpha));
            }
            font->renderUTF8(items[i], 0, static_cast<F32>(left), static_cast<F32>(baseline), (on ? paper : ink) % alpha, LLFontGL::LEFT, LLFontGL::BASELINE,
                             LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX, row.mRight - left, nullptr, false);
        }
    }
}

// --- input ---------------------------------------------------------------------

bool ALTextView::handleKey(KEY key, MASK mask, bool called_from_parent)
{
    LLUICtrl* const focus = dynamic_cast<LLUICtrl*>(gFocusMgr.getKeyboardFocus());
    if (called_from_parent || !focus || focus == this || !focus->acceptsTextInput() || !focus->hasAncestor(this))
    {
        return LLUICtrl::handleKey(key, mask, called_from_parent);
    }
    switch (const ALEditorCommand command = mKeymap.lookup(key, mask))
    {
        case ALEditorCommand::Find:
        case ALEditorCommand::Replace:
        case ALEditorCommand::FindNext:
        case ALEditorCommand::FindPrevious:
            return perform(command);
        default:
            break;
    }
    LLView* const parent = getParent();
    return parent && parent->handleKey(key, mask, false);
}

bool ALTextView::handleKeyHere(KEY key, MASK mask)
{
    // Shift-F10 or the Menu key: the menu a right click at the caret would
    // open, the caret brought into sight first.
    if (ALKeyChords::isContextMenuKey(key, mask) && !mContextMenuFile.empty())
    {
        scrollToCaret();
        const LLRect text  = textRect();
        const S32    row_h = llmax(1, mLayout.rowHeight());
        S32          row   = 0;
        const F32    cx    = lay().xOf(mCaret.line, mCaret.column, &row);
        const S32    top   = text.mTop - (lay().lineTop(mCaret.line) + lay().rowTop(mCaret.line, row) - mScrollY);
        const S32    x     = llclamp(text.mLeft + static_cast<S32>(cx - mScrollX), text.mLeft, text.mRight - 1);
        const S32    y     = llclamp(top - row_h, text.mBottom, text.mTop - 1);
        refreshSuggestions();
        showContextMenu(x, y);
        return true;
    }
    // Escape closes the find bar, but for a modal keymap's inserting
    // mode, which it leaves first; the next Escape closes the bar.
    const bool inserting = mModal && mModal->inserting();
    if (key == KEY_ESCAPE && mask == MASK_NONE && findShown() && !inserting)
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
        syncLanguageInput();
        return true;
    }
    const ALEditorCommand command = mKeymap.lookup(key, mask);
    if (command == ALEditorCommand::None)
    {
        // Escape, once nothing else here took it -- the find bar, a list,
        // a card, a modal keymap: the selection let go of, and the
        // keyboard kept. Passed on, the panel the text is in would take
        // the keyboard away, out into the world, where the arrows walk.
        if (key == KEY_ESCAPE && mask == MASK_NONE)
        {
            // Back to one caret first, its selection kept; then that let go.
            if (singleSelection())
            {
                return true;
            }
            const bool selected = hasSelection();
            if (selected)
            {
                setCaret(mCaret);
            }
            return selected || !mPassEscape;
        }
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
        syncLanguageInput();
        return true;
    }
    if (mReadOnly)
    {
        return false;
    }
    mUndo.beginTyping(selection());
    if (hasOtherSelections())
    {
        typeAtEach(utf8str_from_cp(uni_char), uni_char);
    }
    else
    {
        insertText(utf8str_from_cp(uni_char));
        outdentAsTyped(uni_char);
    }
    mUndo.endTyping();
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
    // The bar along the bottom: its thumb taken hold of where it was
    // pressed, or brought to where the track was. The ruler down the side
    // has had its press, as a child of the view.
    if (const LLRect bar = hBarRect(); bar.notEmpty() && bar.pointInRect(x, y) && barAlpha() > 0.f)
    {
        const LLRect thumb = hThumb(bar);
        mDraggingBar       = true;
        mBarDragOffset     = thumb.pointInRect(x, y) ? x - thumb.mLeft : thumb.getWidth() / 2;
        gFocusMgr.setMouseCapture(this);
        scrollToBarX(x, mBarDragOffset);
        return true;
    }
    const bool same_spot = llabs(x - mClickX) <= CLICK_SLOP && llabs(y - mClickY) <= CLICK_SLOP;
    mClickX              = x;
    mClickY              = y;
    if (!mTripleClick.hasExpired() && same_spot)
    {
        // The third click takes the line; through placeCaret, so that
        // whoever follows the caret hears of it.
        const S32 line = mCaret.line;
        placeSelection(mDocument.lineStart(line), line + 1 < mDocument.lineCount() ? mDocument.lineStart(line + 1) : mDocument.lineEnd(line));
        mDesiredX  = -1.f;
        mSelecting = false;
        if (mModal)
        {
            mModal->mouseChanged(*this);
        }
        return true;
    }
    mTripleClick.stop();
    // Alt-click -- Option on a Mac -- a caret more, or the one there taken
    // away; any other click one caret again.
    if (mask == MASK_ALT && !mReadOnly)
    {
        toggleCaret(posAtLocal(x, y, true));
        mDesiredX = -1.f;
        if (mModal)
        {
            mModal->mouseChanged(*this);
        }
        return true;
    }
    // Shift-Alt -- Shift-Option on a Mac -- a column from the anchor to the
    // press, as Shift-click takes a selection from it, grown as the mouse
    // is dragged.
    if (mask == (MASK_SHIFT | MASK_ALT) && !mReadOnly)
    {
        mPressedLink = -1;
        mPressedAtom = -1;
        selectColumn(columnCorners().first, cornerAtLocal(x, y));
        mColumnDragging = true;
        mSelecting      = true;
        mDragX          = x;
        mDragY          = y;
        gFocusMgr.setMouseCapture(this);
        if (mModal)
        {
            mModal->mouseChanged(*this);
        }
        return true;
    }
    singleSelection();
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
    mDragAt       = posAtLocal(x, y, true);
    mDragToChar   = posAtLocal(x, y, false);
    mDragFromChar = (mask & MASK_SHIFT) != 0 ? mAnchor : mDragToChar;
    placeCaret(mDragAt, (mask & MASK_SHIFT) != 0);
    mDragAnchor = mAnchor;
    mDesiredX   = -1.f;
    mSelecting  = true;
    mDragX      = x;
    mDragY      = y;
    gFocusMgr.setMouseCapture(this);
    if (mModal)
    {
        mModal->mouseChanged(*this);
    }
    return true;
}

bool ALTextView::handleMiddleMouseDown(S32 x, S32 y, MASK mask)
{
    // Over the text only, not its map or bars; nor while a drag is held.
    const bool on_text = textRect().pointInRect(x, y) && !(mScrollMap && mapRect().pointInRect(x, y));
    // What is selected here is the primary selection by now, though the
    // frame that offers it has not come.
    publishPrimary();
    if (mReadOnly || mSelecting || mDraggingBar || mRuler->dragging() || !on_text || !LLClipboard::instance().isTextAvailable(true))
    {
        return LLUICtrl::handleMiddleMouseDown(x, y, mask);
    }
    std::string text;
    if (!LLClipboard::instance().pasteFromClipboard(text, true) || text.empty())
    {
        return true;
    }
    if (mTakesFocus)
    {
        setFocus(true);
    }
    // Where the press is, leaving what is selected in place rather than
    // replacing it, as X11 does: the selection goes, the text it had stays
    // the primary one; and one caret.
    singleSelection();
    placeCaret(posAtLocal(x, y, true), false);
    mDesiredX = -1.f;
    mUndo.beginGroup();
    insertText(text);
    mUndo.endGroup();
    if (mModal)
    {
        mModal->mouseChanged(*this);
    }
    return true;
}

bool ALTextView::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    if (overlayAt(x, y))
    {
        return LLUICtrl::handleRightMouseDown(x, y, mask);
    }
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
    // A bar or the map held, or under the mouse: the arrow, as over any
    // scroll bar, not the text's cursor.
    const auto arrow = []() {
        if (LLWindow* window = getWindow())
        {
            window->setCursor(UI_CURSOR_ARROW);
        }
        return true;
    };
    if (mDraggingBar && hasMouseCapture())
    {
        scrollToBarX(x, mBarDragOffset);
        return arrow();
    }
    if (mSelecting && hasMouseCapture())
    {
        dragSelectTo(x, y);
        return true;
    }
    // On something over the text, it has the mouse: its buttons light and
    // its cursor shows, not the text's.
    // And on the ruler or the map, which says what the mouse is there.
    if (overlayAt(x, y) || rulerAt(x, y))
    {
        mHoverLink = -1;
        mHoverAtom = -1;
        return LLUICtrl::handleHover(x, y, mask);
    }
    const LLRect bar = hBarRect();
    if (bar.notEmpty() && bar.pointInRect(x, y) && barAlpha() > 0.f)
    {
        mHoverLink = -1;
        mHoverAtom = -1;
        return arrow();
    }
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
    mHoverLink = -1;
    mHoverAtom = -1;
    LLUICtrl::onMouseLeave(x, y, mask);
}

bool ALTextView::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (mDraggingBar)
    {
        mDraggingBar = false;
        gFocusMgr.setMouseCapture(nullptr);
        return true;
    }
    if (mSelecting)
    {
        mSelecting      = false;
        mColumnDragging = false;
        gFocusMgr.setMouseCapture(nullptr);
        if (mPrimaryStale)
        {
            offerPrimary();
        }
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
    placeSelection(word.begin, word.end);
    mDesiredX       = -1.f;
    mSelecting      = false;
    mColumnDragging = false;
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
    // Control zooms, a step a notch, a trackpad's glide gathered into
    // steps: the wheel down is away from the reader, smaller.
    if (mZoomWheel && gKeyboard && (gKeyboard->currentMask(true) & MASK_CONTROL))
    {
        mZoomRemainder -= delta.mPrecise;
        const S32 steps = static_cast<S32>(mZoomRemainder);
        mZoomRemainder -= static_cast<F32>(steps);
        if (steps != 0)
        {
            mZoomWheel(steps);
        }
        return true;
    }
    // Shift turns the wheel sideways, where there is a sideways to go.
    if (!mWordWrap && gKeyboard && (gKeyboard->currentMask(true) & MASK_SHIFT))
    {
        return handleScrollHWheel(x, y, delta);
    }
    // By the wheel's own fractions -- a trackpad's glide, a fine wheel's
    // steps -- three rows to a notch, what is less than a pixel kept for
    // the next.
    const F32 pixels = delta.mPrecise * static_cast<F32>(WHEEL_ROWS * mLayout.rowHeight()) + mWheelRemainder;
    const S32 whole  = static_cast<S32>(pixels);
    mWheelRemainder  = pixels - static_cast<F32>(whole);
    setScrollY(mScrollY + whole);
    return true;
}

bool ALTextView::handleScrollHWheel(S32 x, S32 y, LLScrollDelta delta)
{
    if (mWordWrap)
    {
        return false;
    }
    setScrollX(mScrollX + delta.mPrecise * static_cast<F32>(WHEEL_ROWS * mLayout.rowHeight()));
    return true;
}

void ALTextView::onMouseCaptureLost()
{
    mSelecting      = false;
    mColumnDragging = false;
    mDraggingBar    = false;
}

bool ALTextView::handleToolTip(S32 x, S32 y, MASK mask)
{
    if (overlayAt(x, y))
    {
        return LLUICtrl::handleToolTip(x, y, mask);
    }
    // No tip on the ruler, nor on the map, which previews its lines as
    // the mouse moves over it.
    if (rulerAt(x, y))
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
        LLRect sticky;
        localRectToScreen(anchorOf(about), &sticky);
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
        allowLanguageInput(takesComposition());
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
    publishPrimary();
    if (mChangedSinceFocus)
    {
        mChangedSinceFocus = false;
        onCommit();
    }
    LLUICtrl::onFocusLost();
}
