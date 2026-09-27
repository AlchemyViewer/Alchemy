/**
 * @file altextview.h
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

#pragma once

#include "alanchoredranges.h"
#include "alkeymap.h"
#include "alsyntaxhighlighter.h"
#include "altextdocument.h"
#include "altextediting.h"
#include "altextfind.h"
#include "altextindent.h"
#include "altextspelling.h"
#include "altextlayout.h"
#include "altextsearch.h"
#include "altextundo.h"
#include "lleditmenuhandler.h"
#include "llframetimer.h"
#include "llpreeditor.h"
#include "llspellcheckmenuhandler.h"
#include "lluicolor.h"
#include "lluictrl.h"
#include "lluiimage.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ALFindBar;
class LLContextMenu;

// A view of a document: the lines laid out and drawn, only the ones in
// sight, a caret and a selection in them, a keymap that turns keys into
// commands, and a grammar that colours what it shows. Text is drawn from
// glyph runs the layout shaped once, so a frame costs the rows on screen
// and nothing that is not. The document, the undo journal, the highlighter
// and the layout are its own, and reachable, for whatever is built over it
// -- the code editor first.
//
// An input method composes into it through LLPreeditor, as the legacy
// editors do; a right click shows the menu its file names; lines a
// subclass hides (folding) take no room and the caret passes over them.
// A find and replace bar sits over its top right corner when asked for.
// The scrollbars are its own, drawn over the text: a ruler down the
// right that always shows where the caret is and the marks a subclass
// gives, with a thumb that fades once the mouse has left and the text
// has settled; and a thumb along the bottom for a text wider than the
// view. Or the ruler can be a map of the text instead, with the lines
// drawn small and the rows on screen as a window over it.
//
// Three layers over the text besides the grammar's colours: substitutions,
// which show a stretch as other words or as a link without the text
// changing under anyone's positions; atoms, which stand an image or a
// child view in the text where a placeholder is; and the spell check,
// which squiggles the words the dictionary lacks -- in comments and
// strings where there is a grammar, everywhere where there is not --
// and offers the dictionary's suggestions on the right-click menu.
class ALTextView;

// A keymap with a mind of its own -- a vim mode -- told each key and each
// character typed before the plain keymap and the text see them, keeping
// whatever state it needs and working the view through what it exposes.
// The view draws a block for the caret while such a keymap is not
// putting what is typed into the text.
class ALModalKeymap
{
public:
    virtual ~ALModalKeymap() = default;
    // True where the key or the character was taken.
    virtual bool handleKey(ALTextView& view, KEY key, MASK mask) = 0;
    virtual bool handleChar(ALTextView& view, llwchar ch)        = 0;
    // Whether typed characters go into the text.
    virtual bool inserting() const = 0;
    // What a status line says of it: the mode, and what is pending.
    virtual std::string status() const = 0;
    // A line being typed into the keymap -- vim's : and / lines -- and
    // where its caret is in it, in bytes; false while none is. The view
    // shows it in a band under the text, as vim has its command line
    // under the buffer, with the status and the message when none is.
    virtual bool typingLine(std::string& line, S32& caret) const { return false; }
    // What the keymap last said -- a pattern not found, lines yanked --
    // until the next key; whether it was an error.
    virtual std::string message() const { return std::string(); }
    virtual bool        messageIsError() const { return false; }
    // A row of choices offered over the line -- vim's wildmenu, the
    // completions of what is being typed -- and which of them is on the
    // line, or -1 for none; false while there is no such row.
    virtual bool menu(std::vector<std::string>& items, S32& chosen) const { return false; }
    // Goes up with every change of state, for whoever shows the status.
    virtual U32 generation() const = 0;
    // The mouse put the caret somewhere, or dragged a selection: the
    // keymap's own idea of where things are is told.
    virtual void mouseChanged(ALTextView& view) {}
    // Each time the view is drawn: for what waits on time rather than on a
    // key -- vim's keys held for a mapping, until timeoutlen.
    virtual void idle(ALTextView& view) {}
};

class ALTextView : public LLUICtrl, public LLEditMenuHandler, public LLSpellCheckMenuHandler, protected LLPreeditor
{
public:
    AL_VIEW_TYPE(ALTextView, LLUICtrl);

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<LLUIColor>   text_color;
        Optional<LLUIColor>   text_readonly_color;
        Optional<LLUIColor>   bg_color;
        Optional<LLUIColor>   bg_readonly_color;
        // Behind the text while it has the keyboard, as the legacy editors
        // show it.
        Optional<LLUIColor>   bg_focus_color;
        Optional<LLUIColor>   cursor_color;
        Optional<LLUIColor>   selection_color;
        Optional<bool>        bg_visible;
        Optional<bool>        read_only;
        Optional<bool>        word_wrap;
        // A tab typed is spaces to the next stop rather than a tab.
        Optional<bool>        soft_tabs;
        Optional<S32>         tab_width;
        Optional<S32>         h_pad;
        Optional<S32>         v_pad;
        // The grammar to colour by, by the name in its file.
        Optional<std::string> syntax;
        // What the colour table calls each kind's colour: this before the
        // kind's name, "Syntax" unless a skin says, so that one set of
        // editors can be themed apart from another. A name the table
        // lacks falls back to the "Syntax" one.
        Optional<std::string> syntax_color_prefix;
        Optional<std::string> default_text;
        // The file the right-click menu is built from; none for no menu.
        Optional<std::string> context_menu;
        // Behind every match of what the find bar looks for.
        Optional<LLUIColor>   find_match_color;
        // The vertical scrollbar as a map of the text: whether, how wide,
        // whether resting on it shows the lines there, and which side --
        // on the left it is left of the gutter.
        Optional<bool>        scroll_map;
        Optional<S32>         scroll_map_width;
        Optional<bool>        scroll_map_preview;
        Optional<bool>        scroll_map_left;
        // A link is drawn in this, underlined while the mouse is on it.
        Optional<LLUIColor>   link_color;
        // Whether the words the dictionary lacks are squiggled, where the
        // viewer's spell check is on at all.
        Optional<bool>        spellcheck;
        Optional<LLUIColor>   spell_error_color;
        // Whether a click gives the view the keyboard; a card over an
        // editor leaves it where it was.
        Optional<bool>        takes_focus;
        // Whether an Escape with nothing here to let go of goes on to the
        // view it is in, for one that answers it -- a comparison going
        // back to the text it was made from. Otherwise the view keeps it,
        // and the keyboard with it.
        Optional<bool>        pass_escape;
        // Said, dimly, in place of the text while there is none: what
        // will appear here, or what to type.
        Optional<std::string> placeholder;

        Params();
    };

    ~ALTextView() override;

    // --- the text ------------------------------------------------------------

    void        setText(std::string_view text);
    // The text put in with the steps that led to it (ALTextUndo::asLLSD),
    // to take back and forward again: false, and nothing changed -- the
    // text, its steps, its saved mark -- where the history is not of it.
    bool        setTextWithHistory(std::string_view text, const LLSD& history);
    std::string text() const { return mDocument.text(); }
    // The whole text without a copy, kept by the document until the next
    // edit: for a caller that only reads it, and reads it before anything
    // is changed.
    const std::string& wholeText() const { return mDocument.wholeText(); }
    void        setValue(const LLSD& value) override;
    LLSD        getValue() const override;

    ALTextDocument&            document() { return mDocument; }
    const ALTextDocument&      document() const { return mDocument; }
    ALTextLayout&              layout() { return mLayout; }
    const ALTextLayout&        layout() const { return mLayout; }
    ALSyntaxHighlighter&       highlighter() { return mHighlighter; }
    const ALSyntaxHighlighter& highlighter() const { return mHighlighter; }
    ALTextUndo&                undoJournal() { return mUndo; }

    // The grammars on disk, read the first time anything asks.
    static ALSyntaxLibrary& syntaxLibrary();
    void                    setGrammar(std::shared_ptr<const ALSyntaxGrammar> grammar);
    void                    setSyntax(std::string_view grammar_name);

    void            setFont(const LLFontGL* font);
    const LLFontGL* getFont() const override { return mFont; }
    void            setReadOnly(bool read_only);
    bool            isReadOnly() const { return mReadOnly; }
    void            setWordWrap(bool wrap);
    bool            getWordWrap() const { return mWordWrap; }
    // How the text is indented -- by tabs or by spaces, and how wide a
    // tab or a level is -- and where that was said: the defaults the view
    // was given; the text itself, as it was put in whole, where the view
    // reads it; or chosen for this text -- vim's :set, a pick from the
    // strip -- which a new text put in keeps.
    enum class IndentFrom : U8
    {
        Defaults,
        Text,
        Chosen,
    };
    void            setIndentDefaults(S32 tab_width, bool soft_tabs);
    void            setReadsIndentation(bool reads);
    bool            readsIndentation() const { return mReadsIndentation; }
    // Whether lines pasted into a line's indentation are brought to where
    // they go (ALTextIndent::planPaste), where the grammar indents.
    void            setReindentsPaste(bool reindents) { mReindentsPaste = reindents; }
    // Whether Copy and Cut with nothing selected take the caret's whole
    // line, which a paste with nothing selected then puts in above the
    // caret's line, as a line.
    void            setClipsLines(bool clips) { mClipsLines = clips; }
    void            setTabWidth(S32 spaces);
    S32             getTabWidth() const { return mTabWidth; }
    void            setSoftTabs(bool soft);
    bool            getSoftTabs() const { return mSoftTabs; }
    IndentFrom      indentFrom() const { return mIndentFrom; }
    // What was chosen forgotten: the text's own again, where it says and
    // the view reads it, else the defaults.
    void            readIndentation();
    // What is behind the text now -- read-only, focused or neither -- and
    // what the text is drawn in, for whatever draws beside them.
    const LLColor4& backgroundColor() const;
    // The ground under the text, where a view is themed after it was
    // made: a hover card follows the script colours, which the colour
    // table may change while it is open.
    void            setBackgroundColor(const LLUIColor& color) { mBgColor = mBgReadOnlyColor = mBgFocusColor = color; }
    void            setTextColor(const LLUIColor& color) { mTextColor = mTextReadOnlyColor = color; }
    void            setSelectionColor(const LLUIColor& color) { mSelectionColor = color; }
    // Whether the keyboard is on the text itself, rather than on the
    // find bar's field inside the view: what the caret, the caret's
    // line and the matched bracket follow. hasFocus counts a child.
    bool keyboardOnText() const;
    const LLColor4& textColor() const { return (mReadOnly ? mTextReadOnlyColor : mTextColor).get(); }
    const LLColor4& selectionColor() const { return mSelectionColor.get(); }
    // The selection as drawn now: its colour, or a subclass's for a view
    // the keyboard has left.
    virtual LLColor4 selectionDrawColor() const { return mSelectionColor.get(); }
    // How the caret is drawn where a modal keymap does not say -- a line
    // before the character, a block over it, a bar under it -- and whether
    // it blinks.
    enum class CaretStyle : U8
    {
        Line,
        Block,
        Underline
    };
    void       setCaretStyle(CaretStyle style) { mCaretStyle = style; }
    CaretStyle getCaretStyle() const { return mCaretStyle; }
    void       setCaretBlink(bool blink) { mCaretBlink = blink; }
    bool       getCaretBlink() const { return mCaretBlink; }
    // What the view's syntax colours are named under in the colour table:
    // "Syntax", or "Script" for Script Studio's editors.
    const std::string& colorPrefix() const { return mColorPrefix; }
    // The colour a kind is drawn in here.
    const LLColor4& colorForKind(ALSyntaxKind kind) const;
    // The colour table's name for a kind's colour under a prefix:
    // "SyntaxComment", "ScriptComment". Text has none, being the view's
    // own text colour.
    static std::string kindColorName(std::string_view prefix, ALSyntaxKind kind);
    // Whether the text has changed since it was set or saved.
    bool            isDirty() const override { return !mUndo.isPristine(); }
    void            resetDirty() override { mUndo.markSaved(); }
    // Unsaved from here, whatever the journal was told: nothing it can
    // step to was ever saved. Through the virtual, so that what a subclass
    // keeps about changes since the last save says so too.
    virtual void    markUnsaved() { mUndo.markNeverSaved(); mChanged(); }
    // For a save whose answer comes later: where the text stands when it
    // is sent, and that text marked saved once the answer comes, whatever
    // was typed meanwhile staying unsaved.
    ALTextUndo::SavePoint savePoint() { return mUndo.savePoint(); }
    virtual void          markSavedAt(const ALTextUndo::SavePoint& point) { mUndo.markSaved(point); }

    // --- the caret and the selection -----------------------------------------

    const ALTextPos& caret() const { return mCaret; }
    // The selection runs from the anchor to the caret; extending keeps the
    // anchor where it is.
    void        setCaret(ALTextPos pos, bool extend = false);
    ALTextRange selection() const { return ALTextRange(mAnchor, mCaret); }
    bool        hasSelection() const { return mAnchor != mCaret; }
    void        setSelection(const ALTextRange& range);
    std::string selectedText() const { return mDocument.text(selection()); }
    // The identifier the caret is at the end of: letters, digits and
    // underscores back from the caret. Empty at anything else.
    std::string wordBeforeCaret() const;

    // --- editing, through the undo journal -----------------------------------

    // In place of the selection, or at the caret.
    void insertText(std::string_view text);
    void deleteRange(const ALTextRange& range);
    // Several ranges of the text as it stands, each replaced by its
    // string, as one step to undo: what a rename is. The ranges must not
    // overlap. The caret keeps its place in the text around it. False
    // where nothing changed.
    bool replaceAll(std::vector<std::pair<ALTextRange, std::string>> edits);
    bool perform(ALEditorCommand command);
    // Whether a command would do anything now: what a menu asks.
    bool canPerform(ALEditorCommand command) const;
    ALKeymap&       keymap() { return mKeymap; }
    const ALKeymap& keymap() const { return mKeymap; }
    // A keymap with state, ahead of the plain one; none puts the plain
    // one first again.
    void           setModalKeymap(std::unique_ptr<ALModalKeymap> keymap);
    ALModalKeymap* modalKeymap() const { return mModal.get(); }
    // Whether a character typed now goes into the text: always, but for a
    // modal keymap outside its inserting modes, where it is a command.
    bool           typingText() const { return !mModal || mModal->inserting(); }

    // The caret put at a place, or a stretch selected, and brought into
    // view: where a list of places sends it.
    void goTo(const ALTextPos& pos);
    void goTo(const ALTextRange& range);

    // --- what a stretch shows --------------------------------------------------

    // A stretch of the text shown as something other than itself, without
    // the text changing under anyone's positions: a URL as its label, a
    // key as the name it resolved to, or the text as it is, as a link.
    // The caret passes over it whole. Replaced whole, or added one at a
    // time as a log grows; an edit slides them and drops the ones it
    // cuts through.
    struct Substitution
    {
        enum class Underline : U8
        {
            Hover,
            Always,
            Never
        };
        ALTextRange range;
        // In place of the text; empty leaves the text as it is.
        std::string shown;
        bool        link = false;
        Underline   underline = Underline::Hover;
        std::string tooltip;
        // The URL a link is, where it is one: a right click on it shows
        // the registry's menu for the URL, with the actions bound to it.
        std::string url;
        // Handed back when the link is followed.
        LLSD        value;
    };
    void                             setSubstitutions(std::vector<Substitution> substitutions);
    void                             addSubstitution(Substitution substitution);
    void                             clearSubstitutions() { setSubstitutions({}); }
    const std::vector<Substitution>& substitutions() const { return mSubstitutions.items(); }
    // The one over a position, or null.
    const Substitution*              substitutionAt(const ALTextPos& pos) const;
    // What the stretch at a range shows, changed: a name that arrived.
    // False where no substitution starts there.
    bool                             relabel(const ALTextRange& range, const std::string& shown);
    typedef boost::signals2::signal<void(const Substitution&)> link_signal_t;
    // A link followed: clicked, and let go of without a drag.
    boost::signals2::connection onLinkClicked(const link_signal_t::slot_type& slot) { return mLinkClicked.connect(slot); }
    // Every URL on a line, from a column on, made a link through the URL
    // registry: labelled as it labels it, relabelled when a name arrives,
    // with its tooltip, its underline and its URL. How many were made.
    S32 linkUrlsOn(S32 line, S32 from = 0);
    // The links linkUrlsOn would make of a line's text, found but not
    // added, for whoever keeps them to lay again: at `line`, from a
    // column on. A name that arrives later relabels the view's links to
    // the URL, and is told to `labelled` where one is given.
    typedef std::function<void(const std::string& url, const std::string& label)> labelled_t;
    std::vector<Substitution> urlLinks(const std::string& text, S32 line, S32 from = 0, labelled_t labelled = labelled_t());

    // --- styles ----------------------------------------------------------------

    // A stretch in a font of its own, a colour of its own, or both: a
    // heading in a heavier face, a note in the reading face, a warning
    // in the warning colour. Or a stretch bold, italic or underlined,
    // by LLFontGL's flags, which the view resolves to the registry's
    // face for the style -- the bold face of the view's font, or of the
    // font given -- and draws the underline of itself; a chat's names
    // and emotes ask this way. The rows it reaches are as tall as it
    // asks, every font on a row sharing its baseline. Replaced whole;
    // an edit slides them and drops the ones it cuts through.
    struct Style
    {
        ALTextRange             range;
        const LLFontGL*         font = nullptr;
        std::optional<LLColor4> color;
        // LLFontGL::BOLD, ITALIC and UNDERLINE, or none.
        U8                      flags = 0;
    };
    void                      setStyles(std::vector<Style> styles);
    // One more, put in its place among the others; dropped where it
    // would overlap one. What a log adds as it grows, a line at a time.
    void                      addStyle(Style style);
    void                      clearStyles() { setStyles({}); }
    const std::vector<Style>& styles() const { return mStyles.items(); }

    // --- atoms ---------------------------------------------------------------

    // The object replacement character, U+FFFC: what stands in the text
    // where an atom is, unless the atom says its placeholder is longer.
    static const std::string& atomPlaceholder();
    // Something in the text that is not text: a placeholder the document
    // holds, shown as an image or as a child view in a box of the atom's
    // width and a row's height. A view given becomes this view's child,
    // placed in the box while the box is on screen and hidden while it
    // is not, and goes with the atom. The caret passes over an atom
    // whole; an edit that takes its placeholder takes it.
    struct Atom
    {
        ALTextPos    at;
        // The placeholder's bytes.
        S32          length = 3;
        // The box, in pixels; a height of zero is the row's own, and a
        // taller one makes the row taller, the text sitting at its bottom.
        S32          width  = 0;
        S32          height = 0;
        LLUIImagePtr image;
        LLView*      view = nullptr;
        std::string  tooltip;
        // Handed back when the atom is clicked.
        LLSD         value;
    };
    void                     setAtoms(std::vector<Atom> atoms);
    void                     addAtom(Atom atom);
    void                     clearAtoms() { setAtoms({}); }
    const std::vector<Atom>& atoms() const { return mAtoms.items(); }
    const Atom*              atomAt(const ALTextPos& pos) const;
    ALTextRange              atomRange(const Atom& atom) const { return ALTextRange(atom.at, ALTextPos(atom.at.line, atom.at.column + atom.length)); }
    typedef boost::signals2::signal<void(const Atom&)> atom_signal_t;
    // An atom shown as an image was clicked; one shown as a view takes
    // its own clicks.
    boost::signals2::connection onAtomClicked(const atom_signal_t::slot_type& slot) { return mAtomClicked.connect(slot); }
    // Every atom's view put where its box is on the screen, or hidden
    // where the box is not: what a frame does before it draws, and what
    // a click on one needs done before the first frame.
    void placeAtomViews();
    // The keyboard, among the atoms' views: from the text, Tab goes to
    // the first view after the caret and Shift-Tab to the last before
    // it, where the text is read-only and takes no tab of its own -- F6
    // and Shift-F6 do the same from a text that is edited, where a Tab
    // is a tab; from a view, Tab and Shift-Tab go on to the next and
    // back to the one before, and past the ends back to the text, as
    // does Escape. A view that loses its box -- scrolled away, its atom
    // gone -- hands the keyboard back to the text. Whether one of them
    // has the keyboard.
    bool atomViewFocused() const;
    // The keyboard moved to the next or the previous atom's view from
    // wherever it is, or back to the text past the ends; false with none.
    bool focusAtomView(bool forward);

    // --- the spell check -------------------------------------------------------

    void setSpellCheck(bool check);
    // Whether words are checked here: asked for, and the viewer's spell
    // check is on. The menu handler's own question.
    bool getSpellCheck() const override;
    // Who says whether a word is spelled right, and what it might have
    // been: the viewer's dictionary unless told otherwise, which a test
    // is. Only the dictionary takes a word in or lets one pass.
    typedef ALTextSpelling::checker_t   spell_checker_t;
    typedef ALTextSpelling::suggester_t spell_suggester_t;
    void setSpellChecker(spell_checker_t checker, spell_suggester_t suggester = nullptr);
    // The dictionary's suggestions for the misspelling at the caret,
    // gathered again: what the right-click menu offers.
    void refreshSuggestions();
    // The words the dictionary lacks on a line, as ranges of it, checked
    // now if they were not.
    const std::vector<std::pair<S32, S32>>& misspellings(S32 line);
    // Whether a position is in one, and the word there.
    bool        misspelledAt(const ALTextPos& pos, ALTextRange* word = nullptr);
    // Everything is checked again: the dictionary changed.
    void        recheckSpelling();
    // The next misspelled word after a place, or the last before it, line
    // by line round past the ends to the place's own line again; none
    // where there is none, or no spell check.
    std::optional<ALTextRange> misspellingFrom(const ALTextPos& from, bool forward);
    // Next Misspelling and Previous Misspelling: the word selected, from
    // the selection's end or its start. False where there is none.
    bool goToMisspelling(bool forward);
    // A change worked out over the document (ALTextEditing) made, as one
    // step to undo, and the selection it says after.
    void apply(const ALTextEditing::Change& change);
    // --- the change list --------------------------------------------------------

    // Where the text was changed, oldest first, as vim's change list keeps
    // them: one a line for changes one after another on it, each sliding
    // with the text after, the last hundred.
    const std::vector<ALTextPos>& changes() const { return mChanges; }
    // Where in it the caret was last taken: its size past the newest.
    S32 changeAt() const { return mChangeAt; }
    // The caret to the place of a change so many older (negative) or newer
    // than where it was last taken: Last Edit Location and vim's g; and g,.
    // False past either end.
    bool goToChange(S32 steps);

    // Convert Indentation: the leading blanks of lines first through last
    // made of spaces, or of tabs as far as they go, at the view's tab
    // width. False where nothing changed.
    bool convertIndentation(S32 first, S32 last, bool to_spaces, S32 measured_width = 0);

    // --- LLSpellCheckMenuHandler ---------------------------------------------

    const std::string& getSuggestion(U32 index) const override;
    U32                getSuggestionCount() const override;
    void               replaceWithSuggestion(U32 index) override;
    void               addToDictionary() override;
    bool               canAddToDictionary() const override;
    void               addToIgnore() override;
    bool               canAddToIgnore() const override;

    // --- scrolling -----------------------------------------------------------

    void scrollToCaret();
    void scrollToLine(S32 line);
    S32  firstVisibleLine();
    S32  lastVisibleLine();
    // The rows a page holds.
    S32  rowsPerPage() const;
    S32  scrollY() const { return mScrollY; }
    void setScrollY(S32 y);
    // Whether there is more of the text to scroll to that way: below for
    // a positive direction, above for a negative.
    bool canScrollY(S32 direction);
    F32  scrollX() const { return mScrollX; }
    void setScrollX(F32 x);
    bool hasHorizontalScrollbar() const;

    typedef boost::signals2::signal<void()> changed_signal_t;
    boost::signals2::connection onTextChanged(const changed_signal_t::slot_type& slot) { return mChanged.connect(slot); }
    // Every time the caret lands somewhere else, however it got there.
    boost::signals2::connection onCaretMoved(const changed_signal_t::slot_type& slot) { return mCaretMoved.connect(slot); }

    // --- the input method ------------------------------------------------------

    bool        hasPreedit() const { return mPreeditLength > 0; }
    ALTextRange preeditRange() const;
    // The view as what a window composes into. The window is handed it
    // when the view takes focus; a test hands itself.
    LLPreeditor& preeditor() { return *this; }

    // --- the context menu ------------------------------------------------------

    void showContextMenu(S32 x, S32 y);
    // The URL registry's menu for a URL, at a point: what a right click
    // on a link shows. False where the registry has no menu for it.
    bool showUrlMenu(S32 x, S32 y, const std::string& url);

    // --- find and replace ------------------------------------------------------

    // The bar, shown over the top right corner with the query seeded from
    // a selection of a line or less; with the replace row unfolded where
    // asked and the text may change. Every match is washed as the bar's
    // query changes; the current one is the selection.
    void       showFind(bool with_replace);
    void       hideFind();
    bool       findShown() const;
    ALFindBar* findBar() { return mFindBar; }
    const std::vector<ALTextRange>& findMatches()
    {
        settleFind();
        return mFind.matches();
    }
    S32 findCurrent()
    {
        settleFind();
        return mFind.current();
    }
    // The next match selected and brought into view, or the one before;
    // round the ends. False with none.
    bool findNext(bool forward);
    // The current match replaced by the bar's replacement and the next
    // found; or, with no current match, the next found. How many, for
    // every match as one step to undo.
    bool replaceMatch();
    S32  replaceAllMatches();

    // --- the scrollbar as a map ------------------------------------------------

    void   setScrollMap(bool map);
    bool   scrollMap() const { return mScrollMap; }
    void   setScrollMapWidth(S32 width);
    S32    scrollMapWidth() const { return mScrollMapWidth; }
    void   setScrollMapPreview(bool preview) { mScrollMapPreview = preview; }
    bool   scrollMapPreview() const { return mScrollMapPreview; }
    void   setScrollMapOnLeft(bool left);
    bool   scrollMapOnLeft() const { return mScrollMapLeft; }
    // Where the map is drawn, or an empty rect without one.
    LLRect mapRect() const;
    // Where the gutter and the text begin: past the map when it is on
    // the left.
    S32    leftEdge() const;

    // --- LLEditMenuHandler ---------------------------------------------------

    LLView* asView() override { return this; }
    void    undo() override;
    bool    canUndo() const override { return !mReadOnly && mUndo.canUndo(); }
    void    redo() override;
    bool    canRedo() const override { return !mReadOnly && mUndo.canRedo(); }
    void    cut() override;
    bool    canCut() const override { return !mReadOnly && (hasSelection() || mClipsLines); }
    void    copy() override;
    bool    canCopy() const override { return hasSelection() || mClipsLines; }
    void    paste() override;
    bool    canPaste() const override;
    void    doDelete() override;
    bool    canDoDelete() const override { return !mReadOnly && hasSelection(); }
    void    selectAll() override;
    bool    canSelectAll() const override { return !mDocument.empty(); }
    void    deselect() override;
    bool    canDeselect() const override { return hasSelection(); }

    // --- LLView --------------------------------------------------------------

    void draw() override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    bool handleKeyHere(KEY key, MASK mask) override;
    bool handleUnicodeCharHere(llwchar uni_char) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    // The primary selection -- what was last selected, here or anywhere --
    // put where the middle button is pressed, as X11's text boxes and the
    // viewer's own do; where the window has none, as on Windows and the
    // Mac, nothing.
    bool handleMiddleMouseDown(S32 x, S32 y, MASK mask) override;
    void onMouseLeave(S32 x, S32 y, MASK mask) override;
    bool handleRightMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    bool handleDoubleClick(S32 x, S32 y, MASK mask) override;
    bool handleScrollWheel(S32 x, S32 y, LLScrollDelta delta) override;
    // Told of the wheel turned with Control held -- Command on a Mac --
    // by so many steps, up positive: whoever holds the view zooms its
    // text. Without it, Control and the wheel scroll.
    void setOnZoomWheel(std::function<void(S32 steps)> zoom) { mZoomWheel = std::move(zoom); }
    bool handleScrollHWheel(S32 x, S32 y, LLScrollDelta delta) override;
    bool handleToolTip(S32 x, S32 y, MASK mask) override;
    bool handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType cargo_type, void* cargo_data, EAcceptance* accept,
                           std::string& tooltip_msg) override;
    void onMouseCaptureLost() override;
    void setFocus(bool focus) override;
    void onFocusLost() override;
    bool acceptsTextInput() const override { return !mReadOnly; }

    // Whoever wants what is dragged onto the text -- an inventory item
    // onto a notecard, say: asked on the hover and again on the drop,
    // with the point, the cargo and its kind, and answers whether the
    // drop is its and how it is accepted; the text passes the rest by.
    typedef std::function<bool(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip)> drop_handler_t;
    void setDropHandler(drop_handler_t handler) { mDropHandler = std::move(handler); }

    // What is said in place of the text while there is none.
    void               setPlaceholder(const std::string& text) { mPlaceholder = text; }
    const std::string& placeholder() const { return mPlaceholder; }

    // What a tab typed at a place puts in: a tab, or spaces to the next
    // stop where tabs are soft.
    std::string tabText(const ALTextPos& at) const;
    // The rect the text is drawn in.
    LLRect textRect() const;
    // The view less the band a modal keymap has under the text: where
    // the text, the gutter and the bars are.
    LLRect bodyRect() const;
    // The band's height, zero without a modal keymap.
    S32    bandHeight() const;
    // The position under a point of the view, on a cluster boundary.
    ALTextPos posAtLocal(S32 x, S32 y, bool round);
    // And back: where a place in the text is, local to the view -- its
    // row, top to bottom, and across, the place itself, as wide as
    // nothing. For a stretch, its span on the row it begins on, or the
    // whole of that row where it shows nothing there. What a list, a card
    // or a tip is put beside.
    LLRect anchorOf(const ALTextPos& at);
    LLRect anchorOf(const ALTextRange& range);
    // A drag of the mouse under way -- from its press, or a shift-press
    // from the anchor -- and the characters under its two ends: what a
    // keymap whose caret stands on a character, not between two, takes a
    // drag to reach, both ends included. The view's own selection runs
    // between boundaries, and which half of a character the press fell on
    // it rounds away.
    bool      mouseDragging() const { return mSelecting; }
    ALTextPos dragFromCharacter() const { return mDragFromChar; }
    ALTextPos dragToCharacter() const { return mDragToChar; }
    // Comments the selected lines out with the grammar's line comment, or
    // back in where they all are. False without a grammar that has one.
    bool toggleComment();

protected:
    friend class LLUICtrlFactory;
    ALTextView(const Params& p);

    // What a subclass adds to the picture: room at the left of the text
    // for a gutter, whatever it draws there and under the rows before they
    // are drawn, and whatever it draws over each row after its glyphs --
    // given the top of the row's text band, which is a font line tall.
    virtual S32  leftInset() const { return 0; }
    // How much of the top of the text is drawn over at a point across the
    // view, which a line scrolled up to is brought below: the find bar,
    // where the point is under it, and in a subclass what it pins there.
    // Asked again as the view scrolls, since what is pinned may change.
    virtual S32  coveredAbove(S32 local_x);
    // A view of its own under a point, drawn over the text -- the find bar,
    // a list, a card, a view the text holds -- which has the mouse before
    // the text does; or nothing.
    LLView*      overlayAt(S32 x, S32 y) { return childFromPoint(x, y); }
    // An edit command done -- typed, replaced, undone -- before the caret
    // is brought into sight: whatever a subclass left for after its edits,
    // done once rather than at each.
    virtual void editsDone() {}
    // The text changed other than by an edit command -- a log laid out
    // again -- so that the find bar looks for its query again.
    void         findChanged();
    virtual void drawBeforeRows(const LLRect& text) {}
    virtual void drawRowExtras(S32 line, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha) {}
    // Over every row, still clipped to the text: what floats above the
    // text, such as headers pinned at the top.
    virtual void drawAfterRows(const LLRect& text) {}
    // A row's glyph colours, after the kinds have coloured them, for a
    // subclass with colours of its own for some glyphs.
    virtual void tintRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha, std::vector<LLColor4U>& colors) {}
    // A line's row drawn at a place, coloured as it is in the text: what
    // a header pinned at the top is drawn with.
    void drawRowAt(S32 line, S32 row, F32 left, S32 screen_top, F32 alpha);
    // What a subclass does about folding: the caret has landed on a
    // hidden line and it must be seen; a fold command was given; whether
    // one could be.
    virtual void revealLine(S32 line) { mLayout.setHidden(line, line, false); }
    virtual bool performFold(ALEditorCommand command) { return false; }
    virtual bool canFold(ALEditorCommand command) const { return false; }
    // And about the code's structure: the functions a code editor's host
    // knows of, and a bracket's partner.
    virtual bool performFunction(ALEditorCommand command) { return false; }
    virtual bool canFunction(ALEditorCommand command) const { return false; }
    // Whether a click lands where the last one did, which is what makes
    // it the next of a run; and the run armed for a third click, once a
    // subclass has taken a double click as its own.
    bool sameClickSpot(S32 x, S32 y) const;
    void armTripleClick();
    // Completion was asked for.
    virtual bool complete() { return false; }
    // What the call at the caret takes, asked for again.
    virtual bool signatureHelp() { return false; }
    // The fixes for the problem at the caret offered, and whether there are
    // any to offer.
    virtual bool quickFix() { return false; }
    virtual bool canQuickFix() const { return false; }
    // What the map shows beside a line: a mark's colour, where the
    // subclass has one for it; and a count that moves on whenever the
    // marks do, so that the lines with one are not looked for every frame.
    virtual bool mapMark(S32 line, LLColor4& color) const { return false; }
    virtual U32  marksRevision() const { return 0; }
    // The bracket a closing one at a place closes, where the subclass
    // knows how to match them past strings and comments.
    virtual bool closerOpenedAt(const ALTextPos& closer, ALTextPos& opener) { return false; }
    // The x span of a range on a row, if it touches the row; a range past
    // the line's end reaches a little past the last glyph.
    bool spanOnRow(S32 line, S32 row, const ALTextRange& range, F32& x0, F32& x1);
    // A wavy line from x0 to x1 with its middle at y.
    void drawSquiggle(F32 x0, F32 x1, S32 y, const LLColor4& color);
    // Something was asked about the name at the caret: its definition,
    // its references, a new name; whether it could be.
    virtual bool performSymbol(ALEditorCommand command) { return false; }
    virtual bool canSymbol(ALEditorCommand command) const { return false; }
    // Whether anyone answers those questions of this view at all: the
    // right-click menu leaves them out of a view nobody answers them for.
    virtual bool offersSymbols() const { return false; }
    // The screen y of the top of a line's row -- the row's own top; a row
    // a box made taller than the font's line holds its text at its
    // bottom -- and every row on screen in turn, for a subclass drawing
    // beside them.
    S32  screenTopOf(const LLRect& text, S32 line, S32 row);
    void forEachVisibleRow(const LLRect& text, const std::function<void(S32 line, S32 row, S32 screen_top)>& visit);
    // Every change goes through here: the document, the journal, the
    // caret, and whoever is listening.
    ALTextDocument::Edit edit(const ALTextRange& range, std::string_view text);
    // Several stretches replaced as one edit (ALTextDocument::replaceMany),
    // each at a place of the text as it is, none over another: one
    // notification to every listener, one edit for the journal, and the
    // caret put once, at `caret` in the text as it is after.
    ALTextDocument::Edit editMany(std::vector<std::pair<ALTextRange, std::string>> edits, const ALTextPos& caret);
    void                 afterEdit();
    void                 placeCaret(const ALTextPos& pos, bool extend);
    // The selection put somewhere, anchor and caret at once, told to
    // whoever follows the caret where either moved.
    void                 placeSelection(const ALTextPos& anchor, const ALTextPos& caret);
    // A range measured with a composition standing in the text, measured
    // as though it were not.
    ALTextRange          withoutComposition(const ALTextRange& range) const;

    // --- LLPreeditor ---------------------------------------------------------

    void resetPreedit() override;
    void updatePreedit(std::string_view preedit_string, const segment_lengths_t& preedit_segment_lengths,
                       const standouts_t& preedit_standouts, S32 caret_position) override;
    void markAsPreedit(S32 position, S32 length) override;
    void getPreeditRange(S32* position, S32* length) const override;
    void getSelectionRange(S32* position, S32* length) const override;
    bool getPreeditLocation(S32 query_offset, LLCoordGL* coord, LLRect* bounds, LLRect* control) const override;
    S32  getPreeditFontSize() const override;
    const std::string& getPreeditStringUtf8() const override;

private:
    void                 moveVertically(S32 rows, bool extend);
    // Return: the line split with the new one indented as the grammar
    // says, and a closing word before the caret brought out first.
    void                 newLine();
    // A character just typed that finishes what closes a block, as the
    // first thing on its line: the line brought out to where it belongs;
    // and a word so brought out that goes on into a longer one put back.
    void                 outdentAsTyped(llwchar typed);
    // What ALTextIndent works a command out with -- the view's tabs, and
    // the bracket a closing one closes, where a subclass can match one --
    // and what it or ALTextEditing works out done, as one step to undo.
    ALTextIndent::Options  editingOptions() const;
    // Where a word's motion from a place ends: by code's runs where the
    // grammar is code, by the prose's words otherwise; with `parts`, by a
    // name's parts too, in either.
    ALTextPos              wordStep(const ALTextPos& from, bool forward, bool parts) const;
    ALTextIndent::opener_t openerOf();

    void                 allowLanguageInput(bool allow);
    // Whether what an input method composes goes into the text: not in a
    // read-only one, nor while a modal keymap is between its inserting
    // modes, where what is typed is a command -- and a composition there
    // would put text in, or take vim's visual selection out as one typed
    // over. The window is told as that changes, while the view has focus.
    bool                 takesComposition() const { return !mReadOnly && (!mModal || mModal->inserting()); }
    void                 syncLanguageInput();

    void syncScrollbar();
    // The bars: where each is, where its thumb is on it, how faded they
    // are, and the scroll a drag or a press on one asks for.
    LLRect rulerRect() const;
    LLRect hBarRect() const;
    LLRect vThumb(const LLRect& track);
    LLRect hThumb(const LLRect& track);
    F32    barAlpha() const;
    void   scrollToRulerY(S32 y, S32 offset);
    void   scrollToBarX(S32 x, S32 offset);
    void   drawBars(F32 alpha);
    void drawRows(const LLRect& text);
    void placeFindBar();
    // The bar's query looked for through the text again: at once, or once
    // edits stop coming for a moment, the matches sliding with the text
    // until then; and at once where they are about to be used.
    void refreshFind();
    // The bar's query or its choices changed.
    void queryChanged();
    // The bar told what was found.
    void findCounted();
    void settleFind()
    {
        if (mFind.isStale())
        {
            refreshFind();
        }
        // A worker's matches waited for: whatever asks acts on the text as
        // it is.
        if (mFind.searching() && mFind.collect(mDocument, selection().normalised(), true))
        {
            findCounted();
        }
    }
    // The map: the lines it shows, hidden ones left out, and how far its
    // window is scrolled; the line at a y of it; the view scrolled so a
    // y of it is in the middle.
    S32  mapScroll(const LLRect& map);
    S32  mapLineAt(S32 y);
    void scrollToMapY(S32 y);
    void drawMap(F32 alpha);
    // The lines around the one under the mouse on the map, drawn beside
    // it in the view's own face and colours while the mouse is there.
    void drawMapPreview(F32 alpha);
    void drawPreedit(S32 line, const ALTextLayout::Row& row, S32 screen_top, F32 left, F32 alpha);
    void colorRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha);
    // The links, the atoms and the misspellings of a row, drawn over its glyphs.
    void drawLayers(S32 line, const ALTextLayout::Line& laid, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha);
    // Laying out is a cache fill, which a const query may cause.
    ALTextLayout& lay() const { return const_cast<ALTextLayout&>(mLayout); }
    // The layers through an edit: what is after it slides, what it cut
    // through goes, and the lines it touched are checked again.
    void onDocumentEdit(const ALTextDocument::Edit& edit);
    // What the layout is told of a line's substitutions and atoms, and
    // of its stretches in fonts of their own.
    void provideSubstitutions(S32 line, std::vector<ALTextLayout::Substitution>& out) const;
    void provideRuns(S32 line, std::vector<ALTextLayout::Run>& out) const;
    // The first style that reaches a line, by the styles' order.
    std::vector<Style>::const_iterator firstStyleOn(S32 line) const;
    // A position inside what is shown as one thing, moved out to the side
    // it came from -- past it going forward from `from`, before it going
    // back -- else as it is.
    ALTextPos snapped(const ALTextPos& pos, const ALTextPos& from) const;
    // The substitution or the atom under a local point, if the point is
    // on its glyphs.
    const Substitution* linkAtLocal(S32 x, S32 y);
    const Atom*         atomAtLocal(S32 x, S32 y);
    // The atom whose view has the keyboard, or -1; and the keyboard taken
    // back from a view about to lose its box.
    S32                 focusedAtom() const;
    void                letGoOfAtomView(LLView* view);

    ALTextDocument      mDocument;
    ALTextUndo          mUndo;
    ALSyntaxHighlighter mHighlighter;
    ALTextLayout        mLayout;
    ALKeymap            mKeymap;
    std::unique_ptr<ALModalKeymap> mModal;
    // The band under the text a modal keymap has: its line, or its
    // status and message.
    void drawBand(F32 alpha);
    // Where the mouse rests on the map, or -1: what the preview is of.
    S32                 mMapHoverY = -1;
    const LLFontGL*     mFont       = nullptr;
    // Whether the text needs a bar each way; since when the bars were
    // last wanted in sight; and which one a drag has hold of, by how far
    // into its thumb it was taken.
    bool         mNeedV = false;
    bool         mNeedH = false;
    LLFrameTimer mBarShown;
    enum class BarDrag : U8
    {
        None,
        Vertical,
        Horizontal
    };
    BarDrag      mBarDrag       = BarDrag::None;
    S32          mBarDragOffset = 0;

    LLUIColor mTextColor;
    LLUIColor mTextReadOnlyColor;
    LLUIColor mBgColor;
    LLUIColor mBgReadOnlyColor;
    LLUIColor mBgFocusColor;
    LLUIColor mCursorColor;
    LLUIColor mSelectionColor;
    std::string mColorPrefix;
    std::array<LLUIColor, static_cast<size_t>(ALSyntaxKind::COUNT)> mKindColors;

    bool mBgVisible  = true;
    bool mTakesFocus = true;
    bool mPassEscape = false;
    bool mReadOnly   = false;
    // What the window was last told of the input method.
    bool mLanguageInput = false;
    bool mWordWrap  = false;
    // The line a closing word last brought out, where the caret stood
    // after it, and the blanks it had: undone if the next character makes
    // the word a longer one.
    ALTextIndent::AutoOutdent mAutoOutdent;
    bool mSoftTabs  = false;
    S32  mTabWidth  = 4;
    // What the text is indented by where it says nothing of its own, and
    // whether a text put in whole is asked.
    ALTextIndent::Options mIndentDefaults;
    IndentFrom            mIndentFrom         = IndentFrom::Defaults;
    bool                  mReadsIndentation   = false;
    bool                  mReindentsPaste     = false;
    bool                  mClipsLines         = false;
    // What the last Copy or Cut of a whole line put on the clipboard, in
    // any view: pasted as a line while the clipboard still holds it.
    static std::string    sClippedLine;
    void                  useIndentation(const ALTextIndent::Options& options, IndentFrom from);
    S32  mHPad      = 4;
    S32  mVPad      = 2;

    ALTextPos mCaret;
    ALTextPos mAnchor;
    // The x the caret wants when it moves between rows, or negative.
    F32          mDesiredX = -1.f;
    S32          mScrollY  = 0;
    F32          mScrollX  = 0.f;
    // What a wheel moved that did not make a whole pixel yet.
    F32          mWheelRemainder = 0.f;
    std::function<void(S32)> mZoomWheel;
    F32          mZoomRemainder = 0.f;
    bool         mSelecting = false;
    // What is selected becomes the primary selection: at once, or, while a
    // drag goes on, once it is let go of.
    void         offerPrimary();
    bool         mPrimaryStale = false;
    // A drag reaches from where the press put the anchor, not from where a
    // modal keymap then moved the caret -- vim's normal mode takes a click
    // past a line's end back onto its last character -- and only once the
    // mouse is over another place than it was.
    ALTextPos    mDragAnchor;
    ALTextPos    mDragAt;
    // The characters under the press and under the pointer, whichever half
    // of each it is on.
    ALTextPos    mDragFromChar;
    ALTextPos    mDragToChar;
    // Where the pointer was last seen in a drag, and when the text last
    // scrolled on under one held past its top or bottom, which it does
    // whether the mouse moves or not.
    S32          mDragX = 0;
    S32          mDragY = 0;
    F64          mDragScrolled = 0.0;
    // The selection taken to the pointer, the text scrolled on a step
    // where it is past the top or the bottom.
    void         dragSelectTo(S32 x, S32 y);
    LLFrameTimer mBlink;
    CaretStyle   mCaretStyle = CaretStyle::Line;
    bool         mCaretBlink = true;
    // The second and the third click of a run count only where they land
    // by the first: the window tells clicks apart by time alone, and two
    // quick clicks in two places are two clicks.
    LLFrameTimer mTripleClick;
    S32          mClickX = -1000;
    S32          mClickY = -1000;
    bool         mChangedSinceFocus = false;

    // The composition, where one is in progress: where it starts, how many
    // bytes of it there are, where each clause ends counted from its
    // start, which clauses stand out, and what it overwrote.
    ALTextPos        mPreeditBegin;
    S32              mPreeditLength = 0;
    std::vector<S32> mPreeditSegmentEnds;
    standouts_t      mPreeditStandouts;
    std::string      mPreeditOverwritten;

    std::string             mContextMenuFile;
    LLHandle<LLContextMenu> mContextMenuHandle;
    LLHandle<LLContextMenu> mUrlMenuHandle;

    ALFindBar*               mFindBar = nullptr;
    // What the bar's query found, kept in step with the text.
    ALTextFind               mFind;
    LLUIColor                mFindMatchColor;

    bool             mScrollMap        = false;
    S32              mScrollMapWidth   = 80;
    bool             mScrollMapPreview = true;
    bool             mScrollMapLeft    = false;
    bool             mDraggingMap      = false;
    // The lines the map shows, as of the layout's hidden revision and the
    // line count; and the ruler's lines with a mark, as of the text's
    // version and the marks' revision.
    std::vector<S32> mMapLines;
    bool             mMapLinesValid    = false;
    U32              mMapLinesRevision = 0;
    S32              mMapLinesCount    = 0;
    std::vector<S32> mRulerMarkLines;
    bool             mRulerMarksValid    = false;
    // Each pixel row of the ruler's track with a match on it, as last found:
    // for which matches, which track and which text's height.
    std::vector<U8> mRulerRows;
    bool            mRulerRowsValid      = false;
    U32             mRulerRowsGeneration = 0;
    S32             mRulerRowsTop        = 0;
    S32             mRulerRowsHeight     = 0;
    S32             mRulerRowsTotal      = 0;
    U32              mRulerMarksVersion  = 0;
    U32              mRulerMarksRevision = 0;

    std::vector<LLColor4U> mColorScratch;
    changed_signal_t       mChanged;
    changed_signal_t       mCaretMoved;

    boost::signals2::scoped_connection mDocumentConnection;
    // In order of where they start, none over another.
    ALAnchoredRanges<Substitution>     mSubstitutions;
    // An atom is at a place, and over its placeholder's bytes.
    struct AtomRange
    {
        ALTextRange operator()(const Atom& atom) const { return ALTextRange(atom.at, ALTextPos(atom.at.line, atom.at.column + atom.length)); }
    };
    ALAnchoredRanges<Atom, AtomRange>  mAtoms;
    ALAnchoredRanges<Style>            mStyles;
    LLUIColor                          mLinkColor;
    link_signal_t                      mLinkClicked;
    drop_handler_t                     mDropHandler;
    atom_signal_t                      mAtomClicked;
    // The link or the atom the mouse is on, by index, or -1; and the
    // link or the atom a press landed on, which a release on the same
    // follows.
    S32                                mHoverLink    = -1;
    S32                                mHoverAtom    = -1;
    S32                                mPressedLink  = -1;
    S32                                mPressedAtom  = -1;

    // The spell check: whether it was asked for; who checks a word; the
    // words each line lacks, found when the line is drawn and kept until
    // the line changes or the dictionary does; the word at the caret is
    // left alone for a moment after it was typed.
    bool                                    mSpellCheck = false;
    ALTextSpelling                          mSpelling;
    // The change list (changes), and where in it the caret was last taken.
    std::vector<ALTextPos>                  mChanges;
    S32                                     mChangeAt = 0;
    LLUIColor                               mSpellErrorColor;
    std::string                             mPlaceholder;
    LLFrameTimer                            mSpellTimer;
    boost::signals2::scoped_connection      mSpellSettingsConnection;
    std::vector<LLVector2>                  mSquiggleScratch;
};
