/**
 * @file alcodeeditor.h
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

#pragma once

#include "altextview.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ALChoiceList;

// The text view configured for code: a gutter with line numbers, a mark
// per line for what an analyzer said about it and a marker per block that
// folds, a band under the caret's row, the bracket the caret is at and its
// match boxed, decorations over ranges -- a squiggle under a problem, a
// wash behind a find -- the places a name stands lit, and a list of
// completions under the caret.
//
// Blocks are found by indentation, which every language here writes by,
// with a brace or an `end` on the line after a block taken as part of it;
// an analyzer that knows better will hand over regions in phase 2.
// Completions come from whoever is set as the provider, and until someone
// is, from the grammar's vocabulary and the words in the document.
class ALCodeEditor : public ALTextView
{
public:
    AL_VIEW_TYPE(ALCodeEditor, ALTextView);

    struct Params : public LLInitParam::Block<Params, ALTextView::Params>
    {
        Optional<bool>      show_line_numbers;
        Optional<bool>      show_fold_markers;
        Optional<bool>      highlight_current_line;
        Optional<bool>      match_brackets;
        // The list opens on its own once an identifier is two letters in.
        Optional<bool>      auto_complete;
        // The gutter, its numbers, the band under the caret's row and the
        // fold markers are mixed from the text's own background and
        // colour unless a skin names them, so they follow the theme and
        // the focus as the text does.
        Optional<LLUIColor> gutter_color;
        Optional<LLUIColor> line_number_color;
        Optional<LLUIColor> current_line_color;
        Optional<LLUIColor> bracket_match_color;
        Optional<LLUIColor> fold_color;
        // Behind every place a name stands, once its references were
        // asked for; mixed from the text's unless a skin names it.
        Optional<LLUIColor> highlight_color;
        // The bar beside a line changed since the text was last saved.
        Optional<LLUIColor> changed_color;
        // Brackets by depth, one colour per level, round again after the
        // last; none given leaves brackets in the punctuation colour.
        Optional<LLUIColor> bracket_color_1, bracket_color_2, bracket_color_3;

        Params();
    };

    ~ALCodeEditor() override;

    // What the gutter says about a line: the worst of what is on it.
    enum class Mark : U8
    {
        None,
        Note,
        Warning,
        Error,
        Runtime,
        COUNT
    };

    void setMark(S32 line, Mark mark);
    Mark markAt(S32 line) const;
    void clearMarks();
    // A mark's colour: a theme's, where it names one, else the skin's
    // CodeMark colour. What a squiggle of that level is drawn in too.
    LLColor4 markColor(Mark mark) const;

    // The colours a theme may name that a skin need not, looked up under
    // the view's colour prefix -- ScriptIndentGuideColor -- and, where
    // the table has none, mixed from the view's own colours as they
    // always were. Each is what a modern editor's theme sets apart.
    enum class Paint : U8
    {
        ActiveLineNumber,  // the caret's line's number
        IndentGuide,
        Whitespace,        // the marks for blanks
        InlayHint,         // an inlay's word
        InlayHintBg,       // and its pill
        StickyHeader,      // the band the pinned headers are on
        Widget,            // the ground of the hover card, the completions, their documentation, a signature
        WidgetBorder,
        WidgetSelection,   // the completion chosen
        Error,
        Warning,
        Note,
        RuntimeError,
        SelectionInactive, // the selection once the keyboard has left
        COUNT
    };
    static const char* paintName(Paint which);
    LLColor4           paint(Paint which) const;
    LLColor4           selectionDrawColor() const override;

    // Something drawn over a range: a squiggle under it, or a wash behind
    // it. Replaced whole by whoever computes them; an edit slides the ones
    // below it and drops the ones it cuts through.
    struct Decoration
    {
        enum class Style : U8
        {
            Squiggle,
            Background
        };
        ALTextRange range;
        Style       style = Style::Squiggle;
        LLColor4    color;
        // Shown when the mouse rests on it, where there is something to say.
        std::string message;
    };
    void                           setDecorations(std::vector<Decoration> decorations);
    const std::vector<Decoration>& decorations() const { return mDecorations; }

    // A stretch coloured by what an analyzer knows it to be, over what
    // the grammar coloured it: a parameter, a local, a field, a type, a
    // call to something deprecated, which is struck through. Replaced
    // whole; an edit slides them as it does the decorations.
    struct SemanticToken
    {
        ALTextRange  range;
        ALSyntaxKind kind   = ALSyntaxKind::Variable;
        bool         strike = false;
    };
    void                              setSemanticTokens(std::vector<SemanticToken> tokens);
    const std::vector<SemanticToken>& semanticTokens() const { return mSemantics; }

    // A word shown beside the text without being in it -- a parameter's
    // name before the argument it is given, a type after a name declared
    // without one -- in a dim pill the text makes room for. Before the
    // text at its position, so the caret there sits after it, or after
    // the text before its position, with the caret before it. Replaced
    // whole; an edit drops the ones on its lines and slides the rest.
    struct InlayHint
    {
        ALTextPos   at;
        std::string text;
        bool        before = true;
    };
    void                          setInlayHints(std::vector<InlayHint> hints);
    const std::vector<InlayHint>& inlayHints() const { return mInlays; }

    // The places a name stands, washed over until they are cleared or
    // the caret leaves them all; an edit slides them as it does the
    // decorations.
    void                            setHighlights(std::vector<ALTextRange> ranges);
    void                            clearHighlights() { mHighlights.clear(); }
    const std::vector<ALTextRange>& highlights() const { return mHighlights; }
    // Whether a position is on one of them.
    bool                            highlighted(const ALTextPos& at) const;

    // The bracket the caret is at -- just before it, or under it -- and
    // its match, skipping what is inside strings and comments. False where
    // the caret is at no bracket, or the bracket has no match.
    bool matchingBrackets(ALTextPos& open, ALTextPos& close);
    // The bracket at a position and its match, likewise; what vim's %
    // and its bracket objects ask, so that they and the box drawn agree.
    bool matchBracketAt(const ALTextPos& at, ALTextPos& match);

    void setShowLineNumbers(bool show);
    bool getShowLineNumbers() const { return mShowLineNumbers; }
    // Whether a line was changed since the text was last saved: the
    // gutter bars it, and a save clears them all.
    bool lineChanged(S32 line) const;
    void resetDirty() override;
    void markSavedAt(const ALTextUndo::SavePoint& point) override;
    void setShowFoldMarkers(bool show);
    bool getShowFoldMarkers() const { return mShowFoldMarkers; }
    // A faint line down each level of indentation, so that a block's
    // extent is seen without counting spaces.
    void setShowIndentGuides(bool show) { mShowIndentGuides = show; }
    bool getShowIndentGuides() const { return mShowIndentGuides; }
    // Where the blank parts of a line are drawn as marks of their own: a
    // dot in the middle of a space's column, an arrow across a tab to the
    // stop it reached, and a ring around a no-break space -- the one that
    // arrives by being pasted, looks exactly like a space, and stops a
    // script compiling with a message about a character nobody can see.
    //
    // Never; only under the selection, which is the way to ask about a
    // particular line without the rest of the script filling with dots;
    // only where they trail a line, which is the kind worth deleting;
    // or everywhere.
    enum class Whitespace
    {
        None,
        Selection,
        Trailing,
        All
    };
    void       setShowWhitespace(Whitespace how) { mShowWhitespace = how; }
    Whitespace getShowWhitespace() const { return mShowWhitespace; }
    // Which bytes of a line would be marked in the mode set, in order and
    // without overlap: the drawing walks the glyphs on a row, this walks
    // the line, and the test walks this.
    struct Blank
    {
        S32  begin = 0;
        S32  end   = 0;
        char kind  = ' ';   // ' ', '\t', or 'n' for a no-break space
    };
    // `from` and `to` narrow the scan to a stretch of the line, which is
    // how the drawing asks about one row of a wrapped line without
    // walking the whole of it once per row.
    std::vector<Blank> blanksOn(S32 line, S32 from = 0, S32 to = S32_MAX) const;
    // Line numbers counted from the caret's line, which is numbered as
    // itself: what a jump of so many lines reads off.
    void setRelativeLineNumbers(bool relative) { mRelativeLineNumbers = relative; }
    bool getRelativeLineNumbers() const { return mRelativeLineNumbers; }
    // Each pair of brackets in the colour of its depth, so that which
    // closes which is read by colour.
    void setColorBrackets(bool color) { mColorBrackets = color; }
    bool getColorBrackets() const { return mColorBrackets; }
    // The line that opens the block the top of the view is inside,
    // pinned at the top while the block runs on below: the function, the
    // state, the event, the loop, outermost first, up to a few.
    void setStickyHeaders(bool sticky) { mStickyHeaders = sticky; }
    bool getStickyHeaders() const { return mStickyHeaders; }
    S32  gutterWidth() const;

    // Zero-based; the caret goes to the start of the line and the line
    // comes into view.
    void goToLine(S32 line);

    // --- folding ---------------------------------------------------------------

    // A block: the line it starts on stays in sight, the lines through
    // `end` go when it is folded.
    struct FoldRegion
    {
        S32 start = 0;
        S32 end   = 0;
    };
    // Every block in the text, by start line, found again when the text
    // has changed.
    const std::vector<FoldRegion>& foldRegions();
    // The block that starts at the line, else the innermost one around
    // it. False where there is none, or it is already that way.
    bool foldAt(S32 line);
    bool unfoldAt(S32 line);
    void foldAll();
    void unfoldAll();
    // Whether a folded block starts at the line.
    bool isFolded(S32 line) const;

    // --- completion --------------------------------------------------------------

    struct Completion
    {
        std::string  text;
        std::string  detail;
        ALSyntaxKind kind = ALSyntaxKind::Text;
        // A snippet rather than a word: accepting it puts this body in
        // place of the prefix, with its placeholders to tab through.
        std::string  snippet;
        // What it does, shown beside the list while it is the one chosen.
        std::string  documentation;
        // Struck from the language: marked so on the list and last in it,
        // but completed as what it is -- a function with its brackets.
        bool         deprecated = false;
        // The mark before it on the list, where the provider has one;
        // else the icon of its kind, or a badge where the icons are not
        // to be had.
        LLUIImagePtr icon;
    };
    // The icon a kind wears on the list -- Symbol_Function and the rest,
    // looked up once each -- and the badge, a letter, where there is no
    // image provider to look them up in.
    // How well what was typed matches a word, best first, or -1 for not
    // at all: 0 its start as typed, 1 its start in either case, 2 a run
    // of it from where one of its parts begins -- `Say` in `llSay`,
    // `listen` in `llListen` -- and 3 a letter at the start of each of
    // several parts, runs of each after -- `setpos` or `sp` in
    // `llSetPos`. The parts begin after an underscore or a dot, at a
    // capital after a small letter, and at a digit.
    static S32          matchTier(std::string_view word, std::string_view typed);
    static const char*  iconNameOf(const Completion& completion);
    static LLUIImagePtr iconOf(const Completion& completion);
    static const char*  badgeOf(const Completion& completion);
    // Asked for what could go at a position, given the identifier typed
    // so far; answers into `out`, already narrowed to the prefix. The
    // words of the document itself are added after whatever it answers,
    // and the whole is ordered: the case typed first, then the alphabet.
    typedef std::function<void(const ALTextPos& at, std::string_view prefix, std::vector<Completion>& out)> completion_provider_t;
    void setCompletionProvider(completion_provider_t provider) { mProvider = std::move(provider); }
    // Told when the list opens at a new identifier, for whoever answers
    // later: an analyzer on another thread. What it answers comes back
    // through supplyCompletions, and is merged in if the list is still
    // about the same identifier.
    typedef std::function<void(const ALTextPos& at, std::string_view prefix)> completion_request_t;
    void setCompletionRequest(completion_request_t request) { mCompletionRequest = std::move(request); }
    void supplyCompletions(const ALTextPos& at, std::vector<Completion> more);
    bool completionOpen() const;
    void closeCompletion();
    // Whether the list opens on its own as an identifier is typed, or only
    // when asked for; after how many letters; and whether Return takes the
    // chosen one, as Tab does, or starts a new line.
    void setAutoComplete(bool on) { mAutoComplete = on; }
    bool getAutoComplete() const { return mAutoComplete; }
    void setCompleteAfter(S32 letters) { mCompleteAfter = llclamp(letters, 1, 9); }
    S32  getCompleteAfter() const { return mCompleteAfter; }
    void setAcceptOnEnter(bool on) { mAcceptOnEnter = on; }
    bool getAcceptOnEnter() const { return mAcceptOnEnter; }

    // --- brackets and quotes typed in pairs ------------------------------------

    // Whether an opening bracket or quote the grammar pairs is closed as it
    // is typed, with the caret between; the closer typed over rather than
    // doubled while it is the one put in; a selection wrapped in the pair
    // typed; and both taken away by one Backspace between them.
    void setAutoClose(bool on) { mAutoClose = on; }
    bool getAutoClose() const { return mAutoClose; }
    // Puts the chosen completion in place of the prefix; a function comes
    // with its brackets, the caret between them where it takes anything,
    // and the call's signature asked for. False with none open.
    bool                           acceptCompletion();
    const std::vector<Completion>& completions() const { return mCompletions; }
    S32                            chosenCompletion() const;
    // What accepting does, for whoever has a completion in hand without
    // the list: the completion in place of the range.
    void complete(const Completion& chosen, const ALTextRange& range);

    // --- snippets --------------------------------------------------------------------

    // A body in place of the selection, or at the caret: its lines after
    // the first indented as the caret's line is; `${1:text}`, `${1}` and
    // `$1` its placeholders, tabbed through in order of their numbers,
    // and `$0` where the caret lands past the last; `$$` a dollar.
    void insertSnippet(std::string_view body);

    // --- the name at the caret -----------------------------------------------------

    // Asked, on F12, shift-F12 or F2, or the menu, to find the definition
    // or the references of the identifier the caret is on, or to rename
    // it: the command, and the identifier's range. Whoever answers goes
    // there, lights the places, or asks for the new name and calls
    // replaceAll. Nothing is asked with the caret on no identifier.
    typedef std::function<void(ALEditorCommand command, const ALTextRange& word)> symbol_request_t;
    void setSymbolRequest(symbol_request_t request) { mSymbolRequest = std::move(request); }
    // The identifier at a position -- letters, digits and underscores,
    // which is narrower than a word to the document, where a dot between
    // letters joins them as it does in "e.g." -- or an empty range. The
    // caret's is the one it is on or at the end of.
    ALTextRange identifierAt(const ALTextPos& pos) const;
    ALTextRange identifierAtCaret() const;
    // Whether what was typed just before a position is in a comment or a
    // string, by the grammar's tokens: prose, which nothing completes.
    bool        inProse(const ALTextPos& at);
    // The whole string literal a position is in, quotes and all: the run
    // of string and escape tokens around it, carried across lines while
    // one begins or ends inside a string, so that a long string is one
    // literal rather than a line of one. Empty where the position is not
    // in a string.
    ALTextRange stringAt(const ALTextPos& pos) const;
    // What to say about one: its size, which is what a scripter wants of
    // a string and what the type alone never says -- the bytes it comes
    // to, the characters where they are not the same number, and what it
    // is written as where the escapes make that longer.
    std::string stringSize(const ALTextRange& literal) const;

    // --- placeholders ----------------------------------------------------------------

    // The parameters of a call just completed, each a stretch to type
    // over: the first is selected, tab selects the next and shift-tab the
    // one before, past the last the caret lands after the call, and
    // escape or a click lets them go. Typing over one keeps it as what
    // was typed until tab leaves it.
    const std::vector<ALTextRange>& placeholders() const { return mPlaceholders; }
    S32                             placeholderAt() const { return mPlaceholderAt; }
    void                            setPlaceholders(std::vector<ALTextRange> ranges, const ALTextPos& after);
    bool                            nextPlaceholder(S32 direction);
    void                            clearPlaceholders();
    // Everything typing puts up let go of at once: the list, the
    // signature and the stops.
    void                            dropTyping();
    // The names of a signature's parameters, from how a completion's
    // detail reads: "integer llSay(integer channel, string msg)" or
    // "(channel: number, msg: string) -> ()". The list is the bracket
    // after the name where the detail has the name -- a return type may
    // have brackets of its own before it -- else the first.
    static std::vector<std::string> parameterNames(std::string_view detail, std::string_view name = std::string_view());
    // Where that list opens in the detail, or npos.
    static size_t parameterListAt(std::string_view detail, std::string_view name);

    // --- hover -------------------------------------------------------------------

    // Asked what to say about the word the mouse rests on; answers into
    // `text` and true where there is something. A decoration's message
    // comes first where the mouse is on one.
    typedef std::function<bool(const ALTextPos& at, std::string_view word, std::string& text)> hover_provider_t;
    void setHoverProvider(hover_provider_t provider) { mHover = std::move(provider); }
    // Asked when the provider had nothing to say, for an answer that
    // comes later through supplyHover; shown if the mouse is still there.
    typedef std::function<void(const ALTextPos& at, std::string_view word)> hover_request_t;
    void setHoverRequest(hover_request_t request) { mHoverRequest = std::move(request); }
    // A line of what a card says that is a way somewhere -- where the word
    // was declared -- by its words: a link on the card that hands its
    // value to the handler, and the card goes.
    struct CardLink
    {
        std::string line;
        std::string tooltip;
        LLSD        value;
    };
    void supplyHover(const ALTextPos& at, const std::string& text, std::vector<CardLink> links = {});
    typedef std::function<void(const LLSD& value)> card_link_t;
    void setCardLinkHandler(card_link_t handler) { mCardLinkHandler = std::move(handler); }
    // What is said of the problems and the word the mouse rests on, in a
    // card over the text: each problem first, in its squiggle's colour;
    // then what the word is -- its first line in the editor's own face,
    // coloured as the text would colour it, and whatever follows in the
    // reading face -- a note about deprecation in the warning colour,
    // and every URL a link. It stays while the mouse is on what it is
    // about or on the card, and goes with a key, an edit, a scroll or a
    // click elsewhere.
    struct CardProblem
    {
        std::string message;
        LLColor4    color;
    };
    void        showCard(const ALTextRange& about, const std::string& says, const std::vector<CardProblem>& problems = {},
                         const std::vector<CardLink>& links = {});
    // Whether the mouse resting on the text brings up a card at all, and
    // after how long, in seconds; below zero, when the tooltip manager
    // asks, which is the viewer's own tooltip delay.
    void        setHoverCards(bool on) { mHoverCards = on; }
    bool        getHoverCards() const { return mHoverCards; }
    void        setHoverDelay(F32 seconds) { mHoverDelay = seconds; }
    F32         getHoverDelay() const { return mHoverDelay; }
    void        hideCard();
    bool        cardShown() const;
    ALTextView* card() const { return mCard; }
    // A line of another view styled as code: in this editor's face, each
    // token in the colour this editor's grammar and theme give it, and
    // `name`, where given, in the colour of `kind` -- a declaration's
    // name, which the grammar alone does not know.
    void        styleAsCode(const ALTextView& view, S32 line, std::vector<ALTextView::Style>& styles, std::string_view name = std::string_view(),
                            ALSyntaxKind kind = ALSyntaxKind::Text);
    // What a card says of something deprecated, on a line of its own:
    // whoever writes the words puts this, and the card colours it.
    static const std::string& deprecatedNote();
    // What the analyzer made of the name at a position, else Text.
    ALSyntaxKind semanticKindAt(const ALTextPos& at) const;

    // --- signature help ------------------------------------------------------------

    // The call the caret is in, drawn above its row: the label with the
    // parameter the caret is at picked out, and a line of documentation.
    struct Signature
    {
        std::string                          label;
        // Each parameter's span in the label, in bytes.
        std::vector<std::pair<S32, S32>>     parameters;
        S32                                  active = 0;
        std::string                          documentation;
    };
    // Asked for the call at the caret when an opening bracket or a comma
    // is typed, and again at every change while one is shown; the answer
    // comes through showSignature, or hideSignature for none.
    typedef std::function<void(const ALTextPos& caret)> signature_request_t;
    void setSignatureRequest(signature_request_t request) { mSignatureRequest = std::move(request); }
    void showSignature(const ALTextPos& caret, Signature signature);
    void hideSignature();
    // Shown, and still about the call the caret is in: on its line, and
    // not before where it began.
    bool signatureShown() const;
    const Signature* signature() const { return mSignature ? &*mSignature : nullptr; }

    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;
    bool handleUnicodeCharHere(llwchar uni_char) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleDoubleClick(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    void onMouseLeave(S32 x, S32 y, MASK mask) override;
    bool handleToolTip(S32 x, S32 y, MASK mask) override;
    bool handleScrollWheel(S32 x, S32 y, LLScrollDelta delta) override;
    void onFocusLost() override;

protected:
    friend class LLUICtrlFactory;
    ALCodeEditor(const Params& p);

    S32  leftInset() const override { return gutterWidth(); }
    void drawBeforeRows(const LLRect& text) override;
    void drawRowExtras(S32 line, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha) override;
    // The blank marks on one row, over the glyphs the layout placed, so
    // that a tab is marked across the width it actually took.
    void drawWhitespace(S32 line, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha);
    void revealLine(S32 line) override;
    bool performFold(ALEditorCommand command) override;
    bool canFold(ALEditorCommand command) const override;
    bool complete() override;
    bool signatureHelp() override;
    bool performSymbol(ALEditorCommand command) override;
    bool canSymbol(ALEditorCommand command) const override;
    bool mapMark(S32 line, LLColor4& color) const override;
    bool closerOpenedAt(const ALTextPos& closer, ALTextPos& opener) override;

private:
    void onEdit(const ALTextDocument::Edit& edit);
    void drawGutter(const LLRect& text, F32 alpha);
    void drawAfterRows(const LLRect& text) override;
    void tintRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha, std::vector<LLColor4U>& colors) override;
    // The depth of brackets open at a line's start, found from the top
    // and kept until an edit above it.
    S32  bracketDepthBefore(S32 line);
    // The lines pinned at the top for the view as scrolled now, outer to
    // inner; and the number of rows they take.
    std::vector<S32> stickyLines();
    S32              stickyRows();

    void              ensureRegions();
    void              applyFolds();
    const FoldRegion* regionStartingAt(S32 line);
    const FoldRegion* regionAround(S32 line);
    // The box drawn after a folded block's first line, in local
    // coordinates, or an empty rect.
    LLRect foldBoxOf(S32 line, const LLRect& text);
    std::string foldBoxText(S32 line);
    // How far a line is indented, in columns; a blank line as the next
    // line that is not, so that the guides run through it.
    S32 indentOf(S32 line) const;

    void openCompletion();
    void refreshCompletion();
    void placeCompletion();
    // The list shown; the one chosen kept by its word where the list is
    // still about what was typed, else the best.
    void listCompletions(bool keep_choice);
    // What the chosen completion does, beside the list, or nothing.
    void showCompletionDoc();
    void hideCompletionDoc();
    // Out of sight, but still about the word it was asked about, for an
    // answer that may yet come.
    void hideCompletionList();
    void drawSignature(const LLRect& text);
    // The card for the problems and the word at a point of the text, where
    // there is anything to say; what the tooltip and the resting mouse
    // both ask.
    bool hoverCardAt(S32 x, S32 y);
    // A pair typed as it was typed: the opener closed, the closer typed
    // over, a selection wrapped. False where the character is for the
    // text as ever.
    bool typePair(char c);
    // The pair around the caret that one Backspace takes away, if the
    // closer is one typing put in.
    bool deletePair();
    // The problems squiggled under a position, and the stretch they span.
    std::vector<CardProblem> problemsUnder(const ALTextPos& at, ALTextRange& about) const;
    void vocabularyCompletions(std::string_view prefix, std::vector<Completion>& out);
    void documentCompletions(const ALTextPos& at, std::string_view prefix, std::vector<Completion>& out);

    // The colours as drawn now: a skin's where it gave one, else mixed
    // from the text's.
    LLColor4 gutterColor() const;
    LLColor4 lineNumberColor() const;
    LLColor4 currentLineColor() const;
    LLColor4 foldColor() const;
    LLColor4 highlightColor() const;
    LLColor4 changedColor() const;

    bool mShowLineNumbers      = true;
    bool mShowFoldMarkers      = true;
    bool mHighlightCurrentLine = true;
    bool mMatchBrackets        = true;
    bool mAutoComplete         = true;
    S32  mCompleteAfter        = 2;
    bool mAcceptOnEnter        = true;
    bool mAutoClose            = false;
    // The closers typing put in, which a closer typed goes over and a
    // Backspace takes with its opener; they move with the edits, and go
    // when the caret leaves their line.
    std::vector<ALTextPos> mAutoClosed;
    bool     mHoverCards = true;
    F32      mHoverDelay = -1.f;
    // How long the mouse has rested, and whether the card was asked for
    // since it last moved.
    LLFrameTimer mMouseRest;
    bool         mHoverTried = false;

    LLUIColor mGutterColor;
    LLUIColor mLineNumberColor;
    LLUIColor mCurrentLineColor;
    LLUIColor mBracketMatchColor;
    LLUIColor mFoldColor;
    LLUIColor mHighlightColor;
    LLUIColor mChangedColor;
    bool      mShowIndentGuides    = true;
    // Under the selection by default: nothing changes about a script
    // sitting there, and the marks are there the moment anything is
    // picked out, which is when they are wanted.
    Whitespace mShowWhitespace      = Whitespace::Selection;
    bool      mRelativeLineNumbers = false;
    bool      mColorBrackets       = true;
    bool      mStickyHeaders       = true;
    LLUIColor mBracketColors[3];
    bool      mBracketColorsSet    = false;
    // Depth entering each line, valid for the first mDepthValid lines.
    std::vector<S32> mDepthBefore;
    S32              mDepthValid = 0;
    bool      mGutterColorSet      = false;
    bool      mLineNumberColorSet  = false;
    bool      mCurrentLineColorSet = false;
    bool      mFoldColorSet        = false;
    bool      mHighlightColorSet   = false;
    bool      mChangedColorSet     = false;
    LLUIColor mMarkColors[static_cast<size_t>(Mark::COUNT)];
    // The table's colour for each Paint that it has one for, looked up
    // again when the table changes.
    mutable std::array<std::optional<LLUIColor>, static_cast<size_t>(Paint::COUNT)> mPaint;
    mutable U32                                                                     mPaintGeneration = 0;
    mutable bool                                                                    mPaintLooked     = false;

    boost::signals2::scoped_connection mEditConnection;
    boost::signals2::scoped_connection mChangedConnection;
    std::vector<Mark>                  mMarks;
    // One per line: changed since the last save.
    std::vector<U8>                    mChanged;
    // The mouse over the gutter, and the line it is on there: the fold
    // markers of open blocks show while it is, and the block under it
    // shows its extent.
    bool                               mGutterHover     = false;
    S32                                mGutterHoverLine = -1;
    std::vector<Decoration>            mDecorations;
    std::vector<ALTextRange>           mHighlights;
    std::vector<SemanticToken>         mSemantics;
    std::vector<InlayHint>             mInlays;
    // What the layout is told about a line's inlays.
    void provideInlays(S32 line, std::vector<ALTextLayout::Inlay>& out) const;
    F32  inlayWidth(const InlayHint& hint) const;

    std::vector<FoldRegion> mRegions;
    U32                     mRegionsVersion = 0;
    bool                    mRegionsValid   = false;
    // The start lines of the blocks that are folded, in order.
    std::vector<S32>        mFolded;

    completion_provider_t   mProvider;
    completion_request_t    mCompletionRequest;
    hover_provider_t        mHover;
    hover_request_t         mHoverRequest;
    signature_request_t     mSignatureRequest;
    symbol_request_t        mSymbolRequest;
    ALChoiceList*           mCompletionList = nullptr;
    ALTextView*             mCompletionDoc  = nullptr;
    // What the list was last made for, the head and the prefix: a new
    // list for the same is the old one with an answer joined to it.
    std::string             mListedFor;
    std::vector<Completion> mCompletions;
    // The identifier the list is narrowing, which the choice replaces;
    // and the name before the dot before it, where there is one, whose
    // members are what is being asked for.
    ALTextRange             mCompletionRange;
    std::string             mCompletionHead;
    std::vector<ALTextRange> mPlaceholders;
    S32                      mPlaceholderAt = -1;
    ALTextPos                mPlaceholdersAfter;
    // Where the last request was made, so a late answer is known for
    // what it is about, and what was answered, kept through every
    // narrowing until the list closes.
    ALTextPos               mCompletionAsked{ -1, -1 };
    std::vector<Completion> mSupplied;
    // The word the analyzer was last asked about, and what it answered,
    // kept for the text at the version it was answered for, so that the
    // mouse coming back to the word finds the answer waiting.
    ALTextRange             mHoverAsked;
    U32                     mHoverAskedVersion = 0;
    std::string             mHoverAnswer;
    std::vector<CardLink>   mHoverLinks;
    card_link_t             mCardLinkHandler;
    S32                     mMouseX = -1;
    S32                     mMouseY = -1;
    std::optional<Signature> mSignature;
    ALTextPos               mSignatureAt;
    // The card, made the first time it is wanted; what it is about, and
    // that as a rect of the view, which the mouse may rest on as on the
    // card.
    ALTextView*             mCard = nullptr;
    ALTextRange             mCardAbout;
    // The matched pair as last found, for the frame: asked once before
    // the rows are drawn rather than by every row, since finding it may
    // read to the end of the text.
    struct Brackets
    {
        U32       version = 0;
        ALTextPos caret{ -1, -1 };
        bool      matched = false;
        ALTextPos open;
        ALTextPos close;
    };
    Brackets                mBrackets;
    LLRect                  mCardAnchor;
};
