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

#include "alanchoredranges.h"
#include "alchangessincesaved.h"
#include "alcodecards.h"
#include "albracketindex.h"
#include "alcompletionmodel.h"
#include "alfixlistmodel.h"
#include "alfoldmodel.h"
#include "allinetable.h"
#include "alsnippetsession.h"
#include "altextfeatures.h"
#include "altextview.h"
#include "alvimhost.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ALChangePeek;
class ALChoicePopup;

// The text view configured for code: a gutter with line numbers, a mark
// per line for what an analyzer said about it and a marker per block that
// folds, a band under the caret's row, the bracket the caret is at and its
// match boxed, decorations over ranges -- a squiggle under a problem, a
// wash behind a find -- the places a name stands lit, and a list of
// completions under the caret.
//
// Blocks are found by indentation, which every language here writes by,
// with a brace or an `end` on the line after a block taken as part of it;
// none of the analyzers hands over regions of its own.
// Completions come from whoever is set as the provider, and until someone
// is, from the grammar's vocabulary and the words in the document.
class ALCodeEditor : public ALTextView, public ALVimHost, private ALTextFeatures
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
        Heat,              // the gutter's strip of heat, at its warmest
        COUNT
    };
    static const char* paintName(Paint which);
    LLColor4           paint(Paint which) const;
    LLColor4           selectionDrawColor() const override;

    // Something drawn over a range: a line under it, or a wash behind it.
    // Replaced whole by whoever computes them; an edit slides the ones
    // below it and drops the ones it cuts through.
    struct Decoration
    {
        // The line is told apart by its shape as well as its colour, so
        // that a reader who cannot tell the colours apart still can: a
        // wave for an error, dashes for a warning, dots for a note.
        enum class Style : U8
        {
            Squiggle,
            Dashed,
            Dotted,
            Background
        };
        bool        underlined() const { return style != Style::Background; }
        ALTextRange range;
        Style       style = Style::Squiggle;
        LLColor4    color;
        // Shown when the mouse rests on it, where there is something to say.
        std::string message;
        // Where it came among those given, which is the order they are
        // listed in -- the worst first, as a checker gives them --
        // whatever order they lie in; those given later after. Set by
        // setDecorations.
        U32         order = 0;
    };
    void                           setDecorations(std::vector<Decoration> decorations);
    // Those that begin on the lines from `first` to `last` in place of
    // those that did: a stretch of lines the host says again.
    void                           setDecorations(S32 first, S32 last, std::vector<Decoration> decorations);
    // In the order they lie in.
    const std::vector<Decoration>& decorations() const { return mDecorations.items(); }
    // Those that may lie on a line, in the order they were given: a
    // caller still asks each whether it does.
    std::vector<const Decoration*> decorationsOn(S32 line) const;

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
    const std::vector<SemanticToken>& semanticTokens() const { return mSemantics.items(); }

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
        // What a Control-double-click on it writes in at its place, where
        // the text can say it -- a type after a name declared without one
        // -- as one step to undo; a double-click alone takes the name it
        // stands beside. Empty for what it cannot: a parameter's name.
        std::string insert;
    };
    void                          setInlayHints(std::vector<InlayHint> hints);
    const std::vector<InlayHint>& inlayHints() const { return mInlays.items(); }
    // The inlay drawn under a point of the view, by its place among the
    // hints, or -1.
    S32                           inlayAtLocal(S32 x, S32 y);
    // A hint written in where it stands, as a double-click on it does.
    bool                          writeInlay(S32 index);

    // A line's share of what the script's code weighs, as a strip of heat
    // down the gutter's edge beside the text -- nothing where a line made
    // nothing, the warmest where it made the most -- with what it came to
    // in words, for the mouse. The strip is there while it is asked for,
    // heat or none, so that the text does not move as weighings come and
    // go. Replaced whole; an edit keeps a line's heat with what is left of
    // the line -- typed in, broken, or pushed down by lines made above it
    // -- and the lines it makes have none until they are said again.
    struct LineHeat
    {
        S32         line = 0;
        // From nothing to the warmest, 0 to 1.
        F32         heat = 0.f;
        std::string tip;
    };
    void setHeatShown(bool shown);
    bool heatShown() const { return mHeatShown; }
    void setLineHeat(const std::vector<LineHeat>& heat);
    F32  heatAt(S32 line) const;
    // Words drawn dim after a line's end, taking no room in the text: what
    // a function declared there weighs. After a folded block's box where
    // the line is folded. As the heat is for an edit.
    struct LineNote
    {
        S32         line = 0;
        std::string text;
        std::string tip;
    };
    void        setLineNotes(const std::vector<LineNote>& notes);
    std::string noteAt(S32 line) const;
    // The line whose note is drawn under a point of the view, or -1.
    S32         noteAtLocal(S32 x, S32 y);

    // Stretches washed over, each by what lit it, so that one does not
    // put out another: a search's matches, vim's visual block, the places
    // a substitution asks about, the places a name stands as found, the
    // other places the name under the caret stands as written, the words
    // of a line changed since it was saved, as a peek at its change marks
    // them (ALChangePeek), in the colour a comparison marks words put in.
    // Each until it is cleared -- the references also as the caret leaves
    // them all, the occurrences as it leaves the name; an edit slides them
    // as it does the decorations.
    enum class Highlight : U8
    {
        Search,
        Block,
        Confirm,
        References,
        Occurrences,
        Change,
        COUNT
    };
    void                            setHighlights(Highlight layer, std::vector<ALTextRange> ranges);
    void                            clearHighlights(Highlight layer) { mHighlights[static_cast<size_t>(layer)].clear(); }
    void                            clearHighlights();
    // Those of a layer that begin before a place let go, the rest left lit
    // as they are: what an asking :s has been through.
    void                            clearHighlightsBefore(Highlight layer, const ALTextPos& pos);
    // Vim's layers are three of these (ALVimHost), and it asks for them
    // through the host alone.
    ALVimHost*                      vimHost() override { return this; }
    void                            setLayer(Layer layer, std::vector<ALTextRange> ranges) override { setHighlights(highlightOf(layer), std::move(ranges)); }
    void                            clearLayer(Layer layer) override { clearHighlights(highlightOf(layer)); }
    void                            clearLayerBefore(Layer layer, const ALTextPos& pos) override { clearHighlightsBefore(highlightOf(layer), pos); }
    const std::vector<ALTextRange>& layer(Layer layer) const override { return highlights(highlightOf(layer)); }
    static Highlight                highlightOf(Layer layer)
    {
        switch (layer)
        {
            case Layer::Search:  return Highlight::Search;
            case Layer::Block:   return Highlight::Block;
            case Layer::Confirm: return Highlight::Confirm;
        }
        return Highlight::Search;
    }
    const std::vector<ALTextRange>& highlights(Highlight layer) const { return mHighlights[static_cast<size_t>(layer)].items(); }
    // Every layer's, in the layers' order.
    std::vector<ALTextRange>        highlights() const;
    // Whether a position is on one of a layer's.
    bool                            highlighted(Highlight layer, const ALTextPos& at) const;
    // The other places the name under the caret stands lit, washed more
    // lightly than the rest, once the caret has rested on it: in code --
    // not in a string or a comment -- on the lines in view and a view's
    // worth either side, and only where it stands more than once. Put out
    // as the caret leaves the name, or the text changes. As drawing does
    // it, or at once.
    void                            setLightsOccurrences(bool lights);
    void                            lightOccurrences();

    // The bracket the caret is at -- just before it, or under it -- and
    // its match, skipping what is inside strings and comments. False where
    // the caret is at no bracket, or the bracket has no match nearby.
    bool matchingBrackets(ALTextPos& open, ALTextPos& close);
    // The bracket at a position and its match, likewise, as far as told;
    // what vim's % and its bracket objects ask, so that they and the box
    // drawn agree. All of it is the bracket index's (ALBracketIndex),
    // which vim asks of directly for the brackets around a place.
    bool            matchBracketAt(const ALTextPos& at, ALTextPos& match, S32 lines = ALBracketIndex::NEARBY);
    ALBracketIndex& bracketIndex() override { return mBracketIndex; }

    void setShowLineNumbers(bool show);
    bool getShowLineNumbers() const { return mShowLineNumbers; }
    // The number a line shows, and the sign beside it, where its host says
    // (ALTextView::LineAnnotation): a side of a comparison, whose lines are
    // not the ones counted.
    // For a text that is part of a larger one -- a script's code under
    // its envelope, which a runtime error's line counts -- the lines
    // counted from there: the first shows base + 1. Or -1, the lines
    // counted from 0, as a script reads a notecard's.
    void                         setLineNumberBase(S32 base) { mLineNumberBase = llmax(-1, base); }
    S32                          lineNumberBase() const override { return mLineNumberBase; }
    // Whether a line was changed since the text was last saved: the
    // gutter bars it, and a save clears them all.
    bool lineChanged(S32 line) const;
    // The text's changes since it was saved, exactly, with the lines of
    // both (ALChangesSinceSaved): worked out again only where the text or
    // its history moved; none where nothing says what was saved.
    std::shared_ptr<const ALChangesSinceSaved::Known> changesSinceSaved() { return mSinceSaved.of(document(), undoJournal()); }
    // A peek at the change since the text was saved that a line is in
    // (ALChangePeek): its lines as they were, in a gap under them, with
    // the change taken back and the steps to the others. What a press on
    // a line's bar does. False where the line is in none.
    bool          peekChange(S32 line);
    ALChangePeek* changePeek() const { return mPeek; }
    // The changed line whose bar, at the gutter's edge, a point is on: what
    // a press there peeks at. -1 for none, and without a gutter.
    S32           changeBarAt(S32 x, S32 y);
    // A peek open closed: the text is saved, and what it shows was the
    // change since the save before.
    void          closePeek();
    void resetDirty() override;
    void markSavedAt(const ALTextUndo::SavePoint& point) override;
    // Nothing saved is known to measure against: every line barred.
    void markUnsaved() override;
    // The bars as the text differs from a saved one, where the text came
    // in whole rather than edit by edit -- put back from a crash, say: the
    // lines from the first that differs to the last, counted from each end;
    // where lines were only taken away, the line they were taken from.
    void barChangesSince(std::string_view saved);
    void setShowFoldMarkers(bool show);
    // Whether its blocks fold at all, by any command or vim's: not a side
    // of a comparison, whose rows must stay beside the other side's.
    void setFoldable(bool foldable);
    bool foldable() const { return mFoldable; }
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
    // The same into a list the caller keeps, as a frame's drawing does.
    void               blanksOn(S32 line, S32 from, S32 to, std::vector<Blank>& out) const;
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
    // `end` go when it is folded (ALFoldModel).
    typedef ALFoldModel::Region FoldRegion;
    // Every block in the text, by start line, found again when the text
    // has changed.
    const std::vector<FoldRegion>& foldRegions() override;
    // The block that starts at the line, else the innermost one around
    // it. False where there is none, or it is already that way. Unfolding
    // where no block is folded shows what the host hid next to the line
    // instead -- a comparison's run folded away above it or below it --
    // as the host sees fit.
    bool foldAt(S32 line) override;
    bool unfoldAt(S32 line) override;
    void foldAll() override;
    void unfoldAll() override;
    // Whether a folded block starts at the line.
    bool isFolded(S32 line) const override;

    // How many brackets are open at a line's start, found from the top
    // once and kept until an edit above it; and at a place of a line. A
    // bracket in a string or a comment is none.
    S32 bracketDepthBefore(S32 line) { return mBracketIndex.depthBefore(line); }
    S32 bracketDepthAt(const ALTextPos& at) { return mBracketIndex.depthAt(at); }

    // --- completion --------------------------------------------------------------

    // A word offered to complete what is typed (ALCompletion), and how well
    // what was typed matches a word (ALCompletionModel::matchTier). The
    // icon a kind wears on the list, looked up once each by this editor and
    // let go of with it, before the viewer lets go of its images.
    typedef ALCompletion Completion;
    static S32   matchTier(std::string_view word, std::string_view typed) { return ALCompletionModel::matchTier(word, typed); }
    LLUIImagePtr iconOf(const Completion& completion);
    // A mark's glyph in the gutter: the Problems list's icon for its
    // severity, drawn in the mark's colour.
    LLUIImagePtr markIcon(Mark mark);
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
    // The same in a string that names a file (pathAt): asked of the path
    // typed so far, `./lib/ut`, and answering the names that could follow
    // its last slash -- each one's whole path and whether it is a folder
    // (ALCompletion::path, folder) -- with no words of the document's.
    // Neither the provider of names nor its request is asked there; with
    // neither of these, or at several carets, a string is prose as any
    // other.
    void setPathProvider(completion_provider_t provider) { mPathProvider = std::move(provider); }
    void setPathRequest(completion_request_t request) { mPathRequest = std::move(request); }
    // The same in any other string quoted with `"` or `'`, where the host
    // names something to stand for the whole of what it holds -- an item
    // of the object's where a call wants one by its name: asked of what
    // the string holds before the position, as its escapes read it --
    // `Door o`, `Say "` of `Say \"` -- and answering each with the text
    // the string is to hold, escaped for its quote (ALCompletion::path),
    // with no words of the document's; each matched by its name as it
    // reads. The list opens on its own at the quote and as the string is
    // typed, where anything is answered; where nothing is, a string is
    // prose as any other, and what else completes in it still does. Not
    // at several carets, where a string is prose too.
    void setStringProvider(completion_provider_t provider) { mStringProvider = std::move(provider); }
    // What the host names in strings may have changed -- an object's
    // contents heard: where the caret is in a string and no list is open,
    // the host asked again, and the list opened where it now names
    // anything, as typing there would have opened it.
    void reaskString();
    // `words` false where whoever answered says the document's own words
    // are no use there (ALCompletionModel::supply).
    void supplyCompletions(const ALTextPos& at, std::vector<Completion> more, bool words = true);
    bool completionOpen() const;
    void closeCompletion();
    // Whether the list opens on its own as an identifier is typed, or only
    // when asked for; after how many letters; and whether Return may take
    // the chosen one, as Tab does, or always starts a new line. Where it
    // may, it takes it only once the list has been moved through, or where
    // taking it changes the text: a word typed out whole and Return is a
    // new line, not the list's.
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
    const std::vector<Completion>& completions() const { return mCompletionModel.list(); }
    S32                            chosenCompletion() const;
    // What accepting does, for whoever has a completion in hand without
    // the list: the completion in place of the range. Where there are
    // several carets it goes in at each -- in place of what reads as the
    // range does on either side of it, else at it -- each with its own
    // indentation, and each caret on its first parameter or stop, with
    // none to Tab through.
    void complete(const Completion& chosen, const ALTextRange& range);

    // --- snippets --------------------------------------------------------------------

    // A body in place of the selection, or at the caret: its lines after
    // the first indented as the caret's line is; `${1:text}`, `${1}` and
    // `$1` its placeholders, tabbed through in order of their numbers --
    // one inside another's text as well, `${1:a ${2:b}}` -- and `$0`, or
    // `${0:text}` with its text chosen, where the caret lands past the
    // last. A number that comes again is a mirror of the first: it shows
    // what the first holds, and takes what was typed over the first as
    // Tab or Escape leaves it. `$$` or `\$` a dollar, `\}` a brace.
    void insertSnippet(std::string_view body);

    // --- the name at the caret -----------------------------------------------------

    // Asked, on F12, shift-F12 or F2, or the menu, to find the definition
    // or the references of the identifier the caret is on, or to rename
    // it: the command, and the identifier's range. Whoever answers goes
    // there, lights the places, or asks for the new name and calls
    // replaceAll. Nothing is asked with the caret on no identifier.
    typedef std::function<void(ALEditorCommand command, const ALTextRange& word)> symbol_request_t;
    void setSymbolRequest(symbol_request_t request) { mSymbolRequest = std::move(request); }
    // What a place leads to of itself, asked before the identifier there
    // by Go to Definition and Control-click: an include's name, a
    // module's, anywhere on the line that names it. The stretch it covers,
    // or an empty one for nothing; and gone to where `follow`.
    typedef std::function<ALTextRange(const ALTextPos& at, bool follow)> link_request_t;
    void setLinkRequest(link_request_t request) { mLinkRequest = std::move(request); }
    // The identifier at a position -- letters, digits and underscores,
    // which is narrower than a word to the document, where a dot between
    // letters joins them as it does in "e.g." -- or an empty range. The
    // caret's is the one it is on or at the end of.
    ALTextRange identifierAt(const ALTextPos& pos) const;
    ALTextRange identifierAtCaret() const;
    // Whether what was typed just before a position is in a comment or a
    // string, by the grammar's tokens: prose, which nothing completes.
    bool        inProse(const ALTextPos& at);
    // Whether what is typed at a position completes all the same, where
    // the grammar says (ALSyntaxGrammar::completesIn).
    bool        completesInProse(const ALTextPos& at);
    // The whole string literal a position is in, quotes and all: the run
    // of string and escape tokens around it, carried across lines while
    // one begins or ends inside a string, so that a long string is one
    // literal rather than a line of one. Empty where the position is not
    // in a string.
    ALTextRange stringAt(const ALTextPos& pos) const;
    // What a string that names a file holds -- between its quotes, or to
    // the line's end where it is not closed; empty where it holds nothing
    // yet -- where a position is in it or at its end: a string the grammar
    // says names one, by what comes before its opening quote
    // (ALSyntaxGrammar::pathString). None anywhere else.
    std::optional<ALTextRange> pathAt(const ALTextPos& pos);
    // The same of any string or path on its line, the grammar not asked:
    // what it holds, the byte that opens it -- a quote, or an include's
    // `<` -- and whether it is closed on its line.
    std::optional<ALTextRange> quotedAt(const ALTextPos& pos, char* opener = nullptr, bool* closed = nullptr);
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
    const std::vector<ALTextRange>& placeholders() const { return mSnippet.stops(); }
    S32                             placeholderAt() const { return mSnippet.at(); }
    void                            setPlaceholders(std::vector<ALTextRange> ranges, const ALTextPos& after);
    bool                            nextPlaceholder(S32 direction);
    void                            clearPlaceholders();
    // Everything typing puts up let go of at once: the list, the
    // signature and the stops.
    void                            dropTyping();

    // Every place of a text changed at once, each a selection of its own.
    // Select Next Occurrence takes the name at the caret, then adds the
    // next place after the main selection, going round, which becomes the
    // main one; Change All selects every place at once, the main one kept
    // main and the others it had let go. A name taken so matches whole
    // names only.
    bool                            selectNextOccurrence();
    bool                            changeAllOccurrences();
    // The names of a signature's parameters, and where their list opens
    // (ALSnippetSession).
    static std::vector<std::string> parameterNames(std::string_view detail, std::string_view name = std::string_view())
    {
        return ALSnippetSession::parameterNames(detail, name);
    }
    static size_t parameterListAt(std::string_view detail, std::string_view name) { return ALSnippetSession::parameterListAt(detail, name); }

    // --- quick fixes -------------------------------------------------------------------

    // What would put right a problem on a line (ALCodeFix).
    typedef ALCodeFix Fix;
    typedef std::function<void(S32 line, std::vector<Fix>& out)> fix_provider_t;
    void setFixProvider(fix_provider_t provider) { mFixProvider = std::move(provider); }

    // --- functions -------------------------------------------------------------------

    // The text's functions -- a script's functions and events -- as the host
    // knows them: each's whole stretch. What Next Function, Previous
    // Function and Select Function go by, and vim's [[ ]] [m ]m, af and if.
    typedef std::function<void(std::vector<ALTextRange>& out)> function_provider_t;
    void setFunctionProvider(function_provider_t provider) { mFunctionProvider = std::move(provider); }
    // The host's, in order of where they start; none without a host.
    std::vector<ALTextRange> functions() const;
    // The start of the next function after a place, or of the last one
    // before it; or with `ends`, the end.
    std::optional<ALTextRange> functionFrom(const ALTextPos& at, bool forward, bool ends) const override;
    // The innermost function holding a stretch and more besides.
    std::optional<ALTextRange> functionAround(const ALTextRange& range) const override;
    // A comparison's changes, where the text is a side of one: what vim's
    // ]c and [c step through, answered by the comparison. Elsewhere, the
    // text's changes since it was saved (ALChangePeek::stepFrom).
    typedef std::function<bool(bool forward)> change_stepper_t;
    void setChangeStepper(change_stepper_t stepper) { mChangeStepper = std::move(stepper); }
    bool stepChange(bool forward) override;
    // The lines a host hides (ALTextLayout::HiddenBy::Host) -- a
    // comparison's runs the same -- shown by the host as it sees fit where
    // the caret lands on one, or unfolds next to one; what it leaves
    // hidden where the caret lands is shown anyway.
    typedef std::function<void(S32 line)> line_revealer_t;
    void setLineRevealer(line_revealer_t revealer) { mLineRevealer = std::move(revealer); }
    typedef std::function<void(const LLSD& value)> fix_handler_t;
    void setFixHandler(fix_handler_t handler) { mFixHandler = std::move(handler); }
    // What the problems on a line offer: any fix at all -- a suppression
    // among them -- brings the lightbulb up on the caret's line; one that
    // changes the script makes the line's mark round rather than square.
    // Cleared with the marks, and slid with them by an edit.
    void setFixable(S32 line, bool any, bool changes);
    bool fixableAt(S32 line) const;
    bool changesAt(S32 line) const;
    // The fixes of a line listed under it -- the preferred first, a
    // suppression last -- each previewed beside the list as it is chosen,
    // as the lines it touches read now and would read after; Return, Tab
    // or a double click takes one. False where the line has none.
    bool                    openFixes(S32 line);
    bool                    fixesOpen() const;
    // The dictionary's words for the misspelling at the caret, where `line`
    // is the caret's: fixes the editor makes itself when one is taken.
    void                    spellingFixes(S32 line, std::vector<Fix>& fixes);
    void                    closeFixes();
    const std::vector<Fix>& fixes() const { return mFixListModel.fixes(); }
    // What a fix would make of the lines it touches, and the preview of it
    // (ALFixListModel).
    static std::string fixedLines(const ALTextDocument& text, const Fix& fix, S32& first, S32& last)
    {
        return ALFixListModel::fixedLines(text, fix, first, last);
    }
    static std::string previewOf(const ALTextDocument& text, const Fix& fix, std::vector<char>& kinds)
    {
        return ALFixListModel::previewOf(text, fix, kinds);
    }
    // Asked, as a quick fix opens the list, what could be done at the
    // caret, or to the stretch chosen, that no problem asks for; the
    // answer comes back through supplyActions when it is ready, the list
    // showing the line's fixes meanwhile.
    typedef std::function<void(const ALTextRange& at)> action_request_t;
    void setActionRequest(action_request_t request) { mActionRequest = std::move(request); }
    // The refactors for the place last asked about: joined to the fixes
    // listed, or listed alone where the line has none -- or, where there is
    // nothing at all, a word that says so. Dropped where the list has
    // closed since, or the caret moved.
    void supplyActions(const ALTextRange& at, std::vector<Fix> actions);
    // Told whenever the list is made or made again -- opened, or joined by
    // the refactors -- with which showing of it this is and what it lists,
    // for notes that take a while to say; noteFixes puts them after their
    // fixes, in the order listed, and drops them where the list has been
    // made again or closed since.
    typedef std::function<void(U32 shown, const std::vector<Fix>& fixes)> fixes_shown_t;
    void setFixesShown(fixes_shown_t told) { mFixesShown = std::move(told); }
    void noteFixes(U32 shown, const std::vector<std::string>& notes);
    // Whether the refactors asked for are still to come, and will make the
    // list again when they do.
    bool actionsAwaited() const { return mFixListModel.awaited(); }

    // --- hover -------------------------------------------------------------------

    // Asked what to say about the word the mouse rests on; answers into
    // `text` and true where there is something. A decoration's message
    // comes first where the mouse is on one; and where there is a hover
    // request, the analyzer's answer comes before this, which is said
    // where the analyzer answers nothing (supplyHover with no text).
    typedef std::function<bool(const ALTextPos& at, std::string_view word, std::string& text)> hover_provider_t;
    void setHoverProvider(hover_provider_t provider) { mHover = std::move(provider); }
    // Asked when the provider had nothing to say, for an answer that
    // comes later through supplyHover; shown if the mouse is still there.
    typedef std::function<void(const ALTextPos& at, std::string_view word)> hover_request_t;
    void setHoverRequest(hover_request_t request) { mHoverRequest = std::move(request); }
    // A line of what a card says that is a way somewhere -- where the word
    // was declared -- by its words: a link on the card that hands its
    // value to the handler, and the card goes.
    typedef ALCodeCards::Link CardLink;
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
    typedef ALCodeCards::Problem CardProblem;
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
    typedef ALCodeCards::Signature Signature;
    // Asked for the call at the caret when an opening bracket or a comma
    // is typed, and again at every change while one is shown; the answer
    // comes through showSignature, or hideSignature for none.
    typedef std::function<void(const ALTextPos& caret)> signature_request_t;
    void setSignatureRequest(signature_request_t request) { mSignatureRequest = std::move(request); }
    // Every handler, provider and request a host gave it let go of: for a
    // host done with it before it goes -- a tab closed, whose editor dies
    // with the frame -- so that nothing reaches what they held on the way.
    void clearHandlers();
    void showSignature(const ALTextPos& caret, Signature signature);
    void hideSignature();
    // Which argument of the call whose bracket opens at `open` a place is
    // in: the commas of the call's own before it, in code.
    S32  argumentAt(const ALTextPos& open, const ALTextPos& at);
    // Shown, and still about the call the caret is in: on its line, and
    // not before where it began.
    bool signatureShown() const;
    const Signature* signature() const { return mCards.signature(); }

    // Its own work before a frame -- folds settled, a stale signature let
    // go of, the name under the caret lit, a card for the mouse at rest --
    // then the view's.
    void pump() override;
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
    // --- ALTextFeatures ----------------------------------------------------------

    bool performFeature(ALEditorCommand command) override;
    bool canPerformFeature(ALEditorCommand command) const override;
    bool offersSymbols() const override { return static_cast<bool>(mSymbolRequest); }
    void revealLine(S32 line) override;
    // A line next to this one that the host hides, above first; -1 for
    // none, or no host to show it.
    S32  hostHiddenBeside(S32 line) const;
    bool mapMark(S32 line, LLColor4& color) const override;
    U32  marksRevision() const override { return mMarksRevision; }
    bool closerOpenedAt(const ALTextPos& closer, ALTextPos& opener) override;
    // The features' commands, by what they are about: folding; the code's
    // structure, the functions a host knows of and a bracket's partner;
    // completion; what the call at the caret takes, asked for again; the
    // fixes for the problem at the caret; and the name at the caret -- its
    // definition, its references, a new name. Each with whether it could
    // be done.
    bool performFold(ALEditorCommand command);
    bool canFold(ALEditorCommand command) const;
    bool performFunction(ALEditorCommand command);
    // Where Go to Matching Bracket goes, or false for nowhere.
    bool bracketToGoTo(ALTextPos& to);
    // What Expand Selection would grow the selection to (ALSmartSelect).
    std::optional<ALTextRange> grownSelection();
    bool canFunction(ALEditorCommand command) const;
    bool complete();
    bool signatureHelp();
    bool quickFix();
    bool canQuickFix() const;
    bool performSymbol(ALEditorCommand command);
    bool canSymbol(ALEditorCommand command) const;

private:
    void onEdit(const ALTextDocument::Edit& edit);
    void drawGutter(const LLRect& text, F32 alpha);
    void drawAfterRows(const LLRect& text) override;
    void tintRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha, std::vector<LLColor4U>& colors) override;
    // The lines pinned at the top for the view as scrolled now, outer to
    // inner; and the number of rows they take.
    // Worked out afresh as the view is drawn, which reads the lines above
    // the top anyway; asked for between -- a scroll to the caret, a click
    // -- as last drawn, so that a batch's edit is not lexed for them.
    std::vector<S32> stickyLines(bool fresh = false);
    S32              stickyRows();
    // What is drawn over the top of the text: the pinned headers too.
    S32              coveredAbove(S32 local_x) override;

    void              ensureRegions();
    void              applyFolds();
    // The folds hidden again once, after an edit command or before a draw,
    // rather than inside every edit.
    void              settleFolds();
    void              editsDone() override { settleFolds(); }
    bool              mFoldsDirty = false;
    const FoldRegion* regionStartingAt(S32 line);
    const FoldRegion* regionAround(S32 line);
    // The box drawn after a folded block's first line, in local
    // coordinates, or an empty rect.
    LLRect foldBoxOf(S32 line, const LLRect& text);
    std::string foldBoxText(S32 line);
    // Where a line's note is drawn, likewise.
    LLRect noteBoxOf(S32 line, const LLRect& text);
    // The gutter's strip of heat, and the fold column beside it: how wide
    // the one is, and where the other ends.
    S32 heatWidth() const;
    // How far a line is indented, in columns; a blank line as the next
    // line that is not, so that the guides run through it.
    S32 indentOf(S32 line);

    // The list opened: as typing opens it, or asked for (Control-Space),
    // when it lists what could go at the caret with nothing typed.
    void openCompletion(bool asked = false);
    void refreshCompletion();
    // Whether the host names anything for the string a position is in
    // (setStringProvider): what it answered pooled for the list, or
    // pooled already; and where what the string holds starts, and what it
    // holds before the position.
    bool stringOffers(const ALTextPos& at, ALTextPos& start, std::string& typed);
    // Whether Return takes the chosen completion (setAcceptOnEnter).
    bool returnAccepts() const;
    // The fix chosen, previewed beside the list.
    void        showFixPreview();
    void        takeFix(S32 index);
    // The list made of `fixes`, under `line`, with one chosen; and the list
    // filled and placed from what it holds, which a note put in fills again.
    void        showFixes(S32 line, std::vector<Fix> fixes, S32 chosen);
    void        fillFixList(S32 chosen);
    // The list shown; the one chosen kept by its word where the list is
    // still about what was typed, else the best.
    void listCompletions(bool keep_choice);
    // What the chosen completion does, beside the list, or nothing.
    void showCompletionDoc();
    // Out of sight, but still about the word it was asked about, for an
    // answer that may yet come.
    void hideCompletionList();
    // The completions' list and the fixes', each with the box beside it
    // (ALChoicePopup): found among the editor's children by name, or null
    // before the first is shown; made the first time each is wanted, most
    // editors -- a tab not looked at, a read-only view -- never showing
    // either; and dressed in the editor's face and colours.
    ALChoicePopup* popupFound(std::string_view name) const;
    ALChoicePopup& makePopup(const char* name, const char* list, const char* side, bool menu_like);
    ALChoicePopup& completionPopup();
    ALChoicePopup& fixPopup();
    void           dress(ALChoicePopup& popup);
    // Whether a point of the editor is on the completions' list, shown.
    bool           onCompletionList(S32 x, S32 y) const;
    void drawSignature(const LLRect& text);
    // The card for the problems and the word at a point of the text, where
    // there is anything to say; what the tooltip and the resting mouse
    // both ask.
    bool hoverCardAt(S32 x, S32 y);
    // A pair typed as it was typed: the opener closed, the closer typed
    // over, a selection wrapped. False where the character is for the
    // text as ever. At every selection where there are several, each as
    // it would be on its own, a character as ever at those it pairs
    // nothing at; false where it pairs nothing at any.
    bool typePair(char c);
    bool typePairs(char c);
    // A completion in place of a stretch, as a change: the snippet, the
    // call or the name, and where the selection is after.
    ALTextEditing::Change completionAt(const Completion& chosen, const ALTextRange& over);
    // What a character typed at a selection does as a pair: what it
    // replaces and where the selection goes, as a change; whether that
    // goes over a closer typing put in, or puts one in at the caret after.
    // Nothing where the character is for the text as ever.
    struct Paired
    {
        ALTextEditing::Change change;
        bool                  over   = false;
        bool                  closes = false;
    };
    std::optional<Paired> pairAt(const ALTextRange& selection, char c);
    // The pair around the caret that one Backspace takes away, if the
    // closer is one typing put in: at the caret, or as one Backspace at
    // every caret where there are several, the pairs at those it finds
    // one at. False where it finds none.
    bool deletePair();
    bool deletePairs();
    std::optional<ALTextRange> pairAround(const ALTextPos& at);
    // The problems squiggled under a position, and the stretch they span.
    std::vector<CardProblem> problemsUnder(const ALTextPos& at, ALTextRange& about) const;
    void vocabularyCompletions(std::string_view prefix, std::vector<Completion>& out);

    // The colours as drawn now: a skin's where it gave one, else mixed
    // from the text's.
    LLColor4 gutterColor() const;
    LLColor4 lineNumberColor() const;
    LLColor4 currentLineColor() const;
    LLColor4 foldColor() const;
    LLColor4 highlightColor() const;
    LLColor4 changedColor() const;

    bool mShowLineNumbers      = true;
    S32                   mLineNumberBase = 0;
    bool mShowFoldMarkers      = true;
    bool mFoldable             = true;
    bool mHighlightCurrentLine = true;
    bool mMatchBrackets        = true;
    bool mAutoComplete         = true;
    S32  mCompleteAfter        = 2;
    bool mAcceptOnEnter        = true;
    bool mAutoClose            = false;
    // The closers typing put in, which a closer typed goes over and a
    // Backspace takes with its opener; they move with the edits, and go
    // when the caret leaves their line.
    ALAnchoredRanges<ALTextPos> mAutoClosed;
    bool     mHoverCards = true;
    F32      mHoverDelay = -1.f;
    // How long the mouse has rested, and whether the card was asked for
    // since it last moved.
    LLFrameTimer mMouseRest;
    bool         mHoverTried = false;
    // What the hover provider said of the word asked about, shown where
    // the analyzer answers nothing.
    std::string  mHoverFallback;
    // The wheel turned the text under a still mouse: what it brought
    // there is not what the mouse rested on, and no card or tip comes
    // until the mouse moves.
    bool         mWheeled = false;

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
    // Where the brackets pair up, and how deep each line starts.
    ALBracketIndex   mBracketIndex;
    // The depth at each bracket of the last line drawn, by column, for the
    // rest of its rows: under the text and the grammar they were found in.
    struct BracketDepths
    {
        S32                              line    = -1;
        U32                              version = 0;
        const void*                      grammar = nullptr;
        std::vector<std::pair<S32, S32>> at;
    };
    BracketDepths    mBracketDepths;
    // The gutter's numbers as a frame places them, drawn in one call: kept
    // from frame to frame rather than made for each.
    std::vector<LLFontGL::Placed>   mNumberScratch;
    std::vector<LLFontGL::Placed>   mNumberGlyphs;
    std::vector<LLColor4U>          mNumberColours;
    std::vector<LLFontGL::GlyphRun> mNumberRuns;
    std::vector<Blank>              mBlankScratch;
    // The signature card's pieces as last measured, for as long as the
    // signature, its active parameter and the font hold.
    struct SignatureShown
    {
        std::string     label;
        std::string     documentation;
        S32             overload  = -1;
        size_t          overloads = 0;
        S32             active    = -1;
        const LLFontGL* font      = nullptr;
        bool            docs      = false;
        std::string     docLine;
        S32             labelWidth   = 0;
        S32             docWidth     = 0;
        S32             throughWidth = 0;
        S32             begin = -1, end = -1;
        std::string     pieces[3];
        S32             widths[3] = { 0, 0, 0 };
    };
    SignatureShown                  mSignatureShown;
    // Whether the name under the caret is to be lit, since when it is due,
    // and the lines it was lit over.
    bool         mLightsOccurrences = true;
    bool         mOccurrencesDue    = false;
    LLFrameTimer mOccurrencesRest;
    S32          mOccurrencesFirst  = 0;
    S32          mOccurrencesLast   = -1;
    // What Expand Selection grew the selection from, step by step, to go
    // back through while the selection is still what it grew to.
    std::vector<ALTextRange> mGrownFrom;
    ALTextRange              mGrownTo;
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
    ALLineTable<Mark>                  mMarks;
    // Moves on as marks are set or cleared, for the ruler's list of them.
    U32                                mMarksRevision = 0;
    // One per line: changed since the last save.
    ALLineTable<U8>                    mChanged;
    // The mouse over the gutter, and the line it is on there: the fold
    // markers of open blocks show while it is, and the block under it
    // shows its extent.
    bool                               mGutterHover     = false;
    // The peek at a change, made the first time one is asked for; a child
    // let go of before the rest of the editor is.
    ALChangePeek*                      mPeek            = nullptr;
    ALChangesSinceSaved                mSinceSaved;
    S32                                mGutterHoverLine = -1;
    ALAnchoredRanges<Decoration>       mDecorations;
    // How many decorations have been given since the last were given whole.
    U32                                mDecorationsGiven = 0;
    std::array<ALAnchoredRanges<ALTextRange>, static_cast<size_t>(Highlight::COUNT)> mHighlights;
    ALAnchoredRanges<SemanticToken>    mSemantics;
    // An inlay is at a place, not over a range.
    struct InlayAt
    {
        ALTextRange operator()(const InlayHint& hint) const { return ALTextRange(hint.at, hint.at); }
    };
    ALAnchoredRanges<InlayHint, InlayAt> mInlays;
    // Beside each line: its heat and its note, and what they say to the
    // mouse; empty until either is set.
    struct Aside
    {
        F32         heat = 0.f;
        std::string heatTip;
        std::string note;
        std::string noteTip;
    };
    ALLineTable<Aside>                 mAsides;
    bool                               mHeatShown = false;
    void slideAsides(const std::vector<ALTextDocument::Edit::LineSpan>& spans);
    // What the layout is told about a line's inlays.
    void provideInlays(S32 line, std::vector<ALTextLayout::Inlay>& out) const;
    // The hint a line's glyph stands for, by the id provideInlays gave it,
    // as its place among all the hints; -1 where the line has no such.
    S32  inlayIndexOf(S32 line, S32 id) const;
    F32  inlayWidth(const InlayHint& hint) const;

    // The blocks that fold, and which are folded: by the syntax the
    // grammar gives -- brackets that are code, and its block words -- and
    // its line comment's regions, told again when the grammar changes.
    ALFoldModel             mFolds;
    const void*             mFoldGrammar = nullptr;
    ALFoldModel&            folds();
    void                    foldBlocksOn(S32 line, std::vector<ALFoldModel::Block>& out);
    // The sticky headers as last worked out: for which text, which top
    // line and which folds.
    std::vector<S32>        mSticky;
    U32                     mStickyVersion = 0;
    S32                     mStickyTop     = -1;
    std::vector<S32>        mStickyFolded;
    bool                    mStickyValid   = false;
    // The lines last hidden by folds, and the layout's hidden revision
    // then: while neither has moved, the layout hides what the folds do.
    std::vector<std::pair<S32, S32>> mHiddenByFolds;
    U32                              mHiddenByFoldsAt = 0;

    completion_provider_t   mProvider;
    completion_request_t    mCompletionRequest;
    completion_provider_t   mPathProvider;
    completion_request_t    mPathRequest;
    completion_provider_t   mStringProvider;
    hover_provider_t        mHover;
    hover_request_t         mHoverRequest;
    signature_request_t     mSignatureRequest;
    // The bracket the call shown opens with, or none: the signature stays
    // while the caret is inside the call, across its lines.
    ALTextPos               mSignatureOpen{ -1, -1 };
    symbol_request_t        mSymbolRequest;
    link_request_t          mLinkRequest;
    // The icons the completions wear, by name (iconOf).
    boost::unordered_flat_map<std::string, LLUIImagePtr, ll::string_hash, std::equal_to<>> mIcons;
    fix_provider_t          mFixProvider;
    function_provider_t     mFunctionProvider;
    change_stepper_t        mChangeStepper;
    line_revealer_t         mLineRevealer;
    fix_handler_t           mFixHandler;
    // What the list holds (ALFixListModel); who is told each showing of
    // it, and who is asked for the refactors.
    ALFixListModel          mFixListModel;
    fixes_shown_t           mFixesShown;
    action_request_t        mActionRequest;
    // One per line, as the marks are: what its problems offer.
    ALLineTable<U8>         mFixable;
    // What the list offers, as it narrows (ALCompletionModel).
    ALCompletionModel       mCompletionModel;
    // Whether the list was asked for, which it lists for with nothing
    // typed; and whether it has been moved through since it last started
    // from its best.
    bool                    mCompletionAsked = false;
    bool                    mCompletionMoved = false;
    // Whether what the list draws from is what the host named for a
    // string (stringOffers), or the names in a path's folder (pathAt),
    // rather than the words: a list for either goes as the caret leaves
    // the string.
    bool                    mCompletionString = false;
    bool                    mCompletionPath   = false;
    // The stops of a snippet or a call being filled in (ALSnippetSession).
    ALSnippetSession         mSnippet;
    // Each placeholder's mirrors made what it holds, as one step of its
    // own.
    void                     syncMirrors(S32 index);
    // The selections folding has hidden, the main one among them, each a
    // caret at the end of the fold's line.
    void                     caretsOutOfFolds();
    // The places after `from` that read as `wanted`, going round, whole
    // names only where asked, none over any of `taken` -- the selections
    // there are; at most `most` of them.
    std::vector<ALTextRange> placesOf(const std::string& wanted, bool whole, const ALTextPos& from, size_t most,
                                      const std::vector<ALTextRange>& taken) const;
    // The name Select Next Occurrence took at the caret, whose places are
    // whole names only: the main selection while it is one of them.
    ALTextRange              mOccurrenceName;
    // The placeholders let go of where the caret has left the lines they
    // and where the call or the snippet ends are on.
    void                     dropPlaceholdersLeft();
    boost::signals2::scoped_connection mCaretConnection;
    // The word the analyzer was last asked about, and what it answered,
    // kept for the text at the version it was answered for, so that the
    // mouse coming back to the word finds the answer waiting; and the
    // signature shown.
    ALCodeCards             mCards;
    card_link_t             mCardLinkHandler;
    S32                     mMouseX = -1;
    S32                     mMouseY = -1;
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
