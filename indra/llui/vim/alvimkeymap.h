/**
 * @file alvimkeymap.h
 * @brief A vim mode over the text view: modes, operators, motions, text objects, registers, marks and a command line.
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

#include "albracketindex.h"
#include "altextview.h"
#include "alvimcommandline.h"
#include "alvimhost.h"
#include "alvimmappings.h"
#include "alvimpattern.h"
#include "alvimregisters.h"
#include "alvimsearch.h"
#include "lltimer.h"

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ALVimExCommands;

// Vim over the text view, as a keymap with state: normal, insert, replace,
// visual, visual-line and visual-block modes; counts; the operators d c y
// > < = and g~ gu gU, composed with the motions h j k l w W b B e E 0 ^ $
// gg G { } % f F t T ; , H M L n N * # ` ' | and with the text objects iw
// aw iW aW i" a" i' a' i` a` i( a( i[ a[ i{ a{ i< a< it at ip ap; x X s S
// C D Y p P J gJ r ~ o O i a I A u Ctrl-R and . to do the last change
// again; in insert mode Ctrl-W Ctrl-U Ctrl-H Ctrl-T Ctrl-D Ctrl-N Ctrl-P
// Ctrl-A Ctrl-R Ctrl-E Ctrl-Y, and Ctrl-J Ctrl-M Ctrl-I for Return and
// Tab; registers, shared by the keymaps that share their state, the
// unnamed one vim's own unless clipboard is unnamed, with 0 for the last
// yank and a-z by name (A-Z to add), and "+ and "* the system
// clipboard; marks a-z, ` and '; a
// search line for / and ?; and a : line for a line number, s/// with
// vim's flags and & g& :& :&& to do the last one again, set, g and v
// over the lines a pattern picks out, normal, and whatever the hooks
// take -- w, q, wq, x, q! and the studio's own. Either line keeps a
// history: Up and Down walk the lines entered before, q: and q/ open
// the line at the last one, @: runs it again. Macros: q to record keys
// into a register, @ to play them. Ctrl-A and Ctrl-X on a number. The
// mouse: a click puts the caret on a character, a drag is a visual
// selection. It works the view through what the view exposes and
// nothing more, so the same keys drive any text view.
class ALVimKeymap final : public ALModalKeymap
{
public:
    enum class Mode : U8
    {
        Normal,
        Insert,
        Replace,
        Visual,
        VisualLine,
        VisualBlock,
        Command,
        Search,
        // :s with the c flag asking about each match: y, n, a, q, l.
        Confirm
    };

    struct Hooks
    {
        // A : command the mode has no answer to itself: w, q, wq, x, q!,
        // wa, qa, and anything of the host's. True where it was taken.
        std::function<bool(ALTextView& view, const std::string& name, const std::string& args)> command;
        // The = operator over lines first to last.
        std::function<void(ALTextView& view, S32 first, S32 last)> format;
        // q: q/ and q?: the history of a line kind shown for one to be
        // picked -- vim's command-line window -- with what to call with
        // the pick: run as it is, as the window runs the row Enter is
        // pressed on, or put on the line to edit and enter. Without
        // one, the line opens at the last entered.
        std::function<void(ALTextView& view, llwchar kind, const std::vector<std::string>& history, std::function<void(const std::string&, bool run)> chosen)> historyWindow;
        // The host's words for Tab on the : line: with no command, its
        // command names; with one, what may follow it -- a :set option,
        // a :history kind, a file, given the word typed so far, whose
        // folder a file's are read from. Added to the keymap's own, and
        // cut to what was typed by the keymap.
        std::function<void(ALTextView& view, const std::string& command, const std::string& typed, std::vector<std::string>& out)> complete;
        // The caret jumped -- G, gg, a search, %, a sentence or paragraph,
        // H, M, L, a mark, a line on the : line -- from `from`: for the
        // host to keep, as vim's jump list does, for Ctrl-O to go back to.
        std::function<void(ALTextView& view, const ALTextPos& from)> jumped;
        // What :registers and :marks list, as lines, for the host to show
        // where lists go. Without one, the first line is said.
        std::function<void(ALTextView& view, const std::string& text)> listing;
        // One of a list picked -- z='s suggestions -- by its index, with
        // what to call with the one chosen. Without one, the list is said
        // and a count picks: 2z= the second.
        std::function<void(ALTextView& view, const std::string& title, const std::vector<std::string>& items,
                           std::function<void(size_t index)> chosen)>
            pick;
    };

    ALVimKeymap();
    ~ALVimKeymap() override;

    Mode   mode() const { return mMode; }
    Hooks& hooks() { return mHooks; }
    // What was typed after : or / so far.
    const std::string& commandLine() const { return mCommandLine.line; }
    bool               typingLine(std::string& line, S32& caret) const override;
    // A line put up on the : or / line, as the history window hands one
    // back: to be edited and entered, or run as it is, as vim's window
    // runs the row Enter is pressed on. An insert or an asking :s still
    // going ends first, as Escape and q end them.
    void               takeLine(ALTextView& view, llwchar kind, const std::string& text, bool run = false);
    // What the mode last said: a pattern not found, lines yanked, a
    // command unknown; cleared by the next key.
    std::string message() const override { return mMessage; }
    bool        messageIsError() const override { return mMessageError; }
    // Said where the mode says things, as its own are: what a host's :
    // command has to say of itself -- an error, in the error colour.
    void        say(const std::string& message, bool error = false);
    // The completions Tab offers on the : line, while it does.
    bool        menu(std::vector<std::string>& items, S32& chosen) const override;
    // A register's text, or nothing.
    std::string registerText(char name) const;

    bool        handleKey(ALTextView& view, KEY key, MASK mask) override;
    bool        handleChar(ALTextView& view, llwchar ch) override;
    bool        inserting() const override;
    std::string status() const override;
    U32         generation() const override { return mGeneration; }
    void        mouseChanged(ALTextView& view) override;
    // Keys held for a mapping fed as they are, or as the mapping they
    // make whole, once they have waited timeoutlen.
    void        idle(ALTextView& view) override;

    // Whether keys are being recorded into a register, and which.
    bool recording() const { return mRecording != 0; }
    char recordingInto() const { return mRecording; }

    // What one vim shares among its buffers, which here are the keymaps
    // of the studio's editors: the registers, so that what is yanked in
    // one is put in another, the system clipboard or not; the lines
    // entered on the : line and on the search line, oldest first, for Up
    // and Down on the line, q: q/
    // @: and :history; and the settings a :set changes, ignorecase and
    // smartcase, and clipboard: whether what is yanked, deleted and put
    // with no register named goes by the system clipboard, as vim's
    // clipboard=unnamed has it, or stays the editor's own -- vim's own
    // default, so that :g/x/d is not a clipboard write a line. A register
    // named -- "a -- never touches the clipboard, and "+ and "* are it
    // whatever this says. The keymap's own unless told to share another's.
    struct Shared
    {
        // a-z, 0 for the last yank, 1-9 for the last deletes of a line or
        // more, newest first, - for the last smaller one, and the unnamed
        // one (ALVimRegisters).
        ALVimRegisters           registers;
        std::vector<std::string> command;
        std::vector<std::string> search;
        bool                     ignoreCase       = false;
        bool                     smartCase        = false;
        bool                     unnamedClipboard = false;
        // Every match of the last search lit, as hlsearch has it; and the
        // matches of what is typed on the search line lit, and the next
        // brought into sight, as incsearch has it.
        bool                     highlightSearch  = true;
        bool                     incrementalSearch = true;
        // The :map family's mappings and the leaders; and how long keys
        // that may be the start of a longer mapping wait for the rest,
        // as vim's timeout and timeoutlen have it -- without the timeout,
        // for as long as it takes.
        ALVimMappings            mappings;
        bool                     timeout       = true;
        S32                      timeoutLength = 1000;
        // What the vimrc set of each editor's own options -- expandtab,
        // tabstop, shiftwidth -- as it said them, for the host to set on
        // each editor it puts vim over (setViewOption).
        std::vector<std::string> viewOptions;
        // Whether a host has read a vimrc into it yet: once is enough for
        // every keymap sharing it, and again resets what a :set changed.
        bool                     sourced = false;
    };
    const Shared&           shared() const { return *mShared; }
    std::shared_ptr<Shared> sharedState() const { return mShared; }
    void                    share(std::shared_ptr<Shared> shared);

    // A vimrc read into the shared state, a line at a time, what it made
    // before taken away first -- the mode's options vim's own again, but
    // clipboard, which is the host's; the lines typed kept: the :map
    // family and :let mapleader and maplocalleader; :set of the options
    // the mode keeps, and of each editor's own, into viewOptions; the
    // rest of :set's options offered to `host`, true where it took one;
    // comments, blank lines, lines that go on with a backslash, and
    // silent! before a line to say nothing of it. Nothing else runs: a
    // vimrc sets things up and does not edit. What was not understood, a
    // line each with its number, in `errors`.
    static void source(Shared& shared, std::string_view text, const std::function<bool(const std::string& option)>& host,
                       std::vector<std::string>& errors);
    // One of :set's options that are each view's own set on a view --
    // wrap, expandtab, tabstop, shiftwidth, softtabstop -- as it is
    // written after :set: `nowrap`, `ts=4`, `et!`, `sw?`. What a query
    // shows in `shown`, what went wrong in `error`; false where the option
    // is none of these.
    static bool setViewOption(ALTextView& view, const std::string& option, std::string& shown, std::string& error);

    // One thing typed: a character, or a key with its modifiers; and the
    // Control of vim's chords (ALVimInput).
    static constexpr MASK CONTROL = ALVimInput::CONTROL;
    typedef ALVimInput    Input;
    // Keys as text, the way a macro's register holds them: characters
    // as themselves, `<` as `<lt>`, keys by name -- <Esc>, <CR>, <BS>,
    // <Tab>, <Del>, <Up>, <Down>, <Left>, <Right>, <Home>, <End>,
    // <PageUp>, <PageDown> -- with C-, S- and A- before a modified one;
    // and back again.
    static std::string        encodeInputs(const std::vector<Input>& inputs);
    static std::vector<Input> decodeInputs(std::string_view text);

private:
    typedef ALVimRegisters::Register Register;
    // Where a motion goes, and how the stretch to there is taken.
    struct Motion
    {
        bool      ok        = false;
        ALTextPos to;
        bool      linewise  = false;
        bool      inclusive = false;
        // A motion that failed to move at all, which cancels an operator.
        bool      moved     = true;
    };
    // A stretch an operator works on. A block's columns are the reader's
    // (ALVimText::blockColumns), which the bytes of its lines need not
    // agree on: the first and the last it covers, and whether it reaches
    // every line's end, as one taken with $ does; its range runs from what
    // it holds of its first line to what it holds of its last.
    struct Span
    {
        ALTextRange range;
        bool        linewise = false;
        bool        block    = false;
        S32         left     = 0;
        S32         right    = 0;
        bool        toEnd    = false;
        // Taken from a visual selection -- gn's match is one -- which a
        // delete takes as it was selected, never as the lines it is over.
        bool        visual   = false;
    };

    // A key as typed: recorded where a macro is being, then through the
    // mappings (typeThrough).
    bool type(ALTextView& view, const Input& input);
    // A key fed as it is; or, where it is the start of a mapping or one
    // is waiting, held with the rest until they make one or cannot.
    bool typeThrough(ALTextView& view, const Input& input);
    // A key held or to be fed: whether it may be mapped, which what a
    // :noremap mapping stands for may not; and whether it is the one just
    // typed, which the view is told of.
    struct Held
    {
        Input input;
        bool  remap = true;
        bool  typed = false;
    };
    // The keys at the front of a queue fed, those that make a mapping
    // replaced by what it stands for first, until the queue is empty or
    // what is left may yet be the start of a longer mapping, which waits.
    // `final`: no more keys come -- the timeout ran out, or a macro's keys
    // are all there -- and nothing waits. `stop_on_error`: what is left is
    // dropped once one ends in an error, as a macro's keys are. `taken`:
    // where the key just typed is fed alone as it came, whether it was
    // taken -- the view types a character nobody took.
    void drain(ALTextView& view, std::deque<Held>& queue, bool final, bool stop_on_error, std::optional<bool>* taken);
    // One key fed as a mapping or a flush feeds it: its character typed by
    // this rather than left to the view, and a key vim gives no meaning
    // done as the view's keymap would do it.
    void feedMapped(ALTextView& view, const Input& input);
    // What insert mode types of the keys fed by hand -- by `.`, a macro,
    // :normal, a mapping -- is held while more characters follow, and put
    // in at once when anything else comes or the keys run out: one edit
    // for the text, as the view hears of a paste, not one a character.
    bool holds(const Input& input) const;
    void flushHeld(ALTextView& view);
    // The mapping mode keys are looked up in now: none while a command
    // waits for a character of its own -- f's, r's, the second of g's.
    U8   mapMode() const;
    // Whether a key may be mapped: a character, or a key no character
    // follows -- a chord, Escape, the arrows -- but not a modifier alone,
    // nor the Mac's Command chords, which are the menus'.
    static bool mappable(const Input& input);
    // The keys held for a mapping, spelt, for the status.
    std::string heldShown() const;

    bool feed(ALTextView& view, const Input& input);
    bool normal(ALTextView& view, const Input& input);
    // The bracket at a position and its match, through the editor where
    // the view is one, so that a bracket inside a string or a comment
    // is passed over as the editor's box passes it.
    bool matchBracketIn(ALTextView& view, const ALTextPos& from, ALTextPos& match) const;
    // Where the view's brackets pair up: the code editor's own, or one of
    // this keymap's over a plain view's text, where every bracket counts.
    // `%`, the bracket objects and [( all ask of it, by one set of rules.
    ALBracketIndex& bracketsOf(ALTextView& view) const;
    bool insert(ALTextView& view, const Input& input);
    // Insert mode's own Control keys; false where vim gives the key no
    // meaning there, and it is the view's.
    bool insertControl(ALTextView& view, const Input& input);
    // Text put in as though it were typed: part of what the insert typed,
    // for `.` and a count, as the text has it.
    void typeIn(ALTextView& view, const std::string& text);
    // What insert mode has typed: the text from where it began to the
    // caret, as it stands now.
    std::string typedText(const ALTextView& view) const;
    // The caret about to be moved off what insert mode typed -- an arrow,
    // Ctrl-O's command -- or, `gone`, moved off it already, by the mouse:
    // what was typed so far is the last insert's, where it can still be
    // read, and typing again is an insert of its own (restartInsert).
    void typingLeft(const ALTextView& view, bool gone = false);
    // Typing again once the caret was moved off what was typed: an insert
    // from the caret, as vim's arrows make it -- what a count or a block
    // types again, and what `.` repeats, are what is typed from here.
    void restartInsert(ALTextView& view);

    // Normal mode's command, once the count, the register and any
    // operator have been read; false where the character is not one. In
    // turn: the key a waiting one wants (afterPending), a count or a
    // register, an operator's motion (operatorKey), visual mode's own
    // (visualKey), everyone's (normalKey), and else a motion (motionKey).
    // visualKey and normalKey say nothing of a key that is none of theirs.
    bool                command(ALTextView& view, llwchar ch);
    bool                operatorKey(ALTextView& view, llwchar ch);
    std::optional<bool> visualKey(ALTextView& view, llwchar ch);
    std::optional<bool> normalKey(ALTextView& view, llwchar ch);
    bool                motionKey(ALTextView& view, llwchar ch);
    // An operator over a search's motion -- / and ? entered after it, n
    // and N, * and # -- from the caret to where the last search goes,
    // `forward` or back, the counts' match on, looked for from
    // `search_from` where one is given: exclusive and charwise, as vim's
    // is; lines for an offset of lines, and inclusive for one from the
    // match's end (:help search-offset). One that finds nothing fails the
    // operator.
    bool                searchMotion(ALTextView& view, bool forward, std::optional<ALTextPos> search_from = std::nullopt);
    // * # g* g#: the word under the caret looked for, whole or -- `whole`
    // false, g* and g# -- anywhere, forward or back, the count's match on;
    // from the word's start, so that the word itself is passed over. The
    // caret goes there, or an operator waiting takes the stretch to there
    // (searchMotion), and n and N go on with it. No word under the caret
    // fails the command, the operator with it.
    bool                starSearch(ALTextView& view, bool forward, bool whole);
    // A key that waits for the one after it -- a register's name, g's and
    // z's commands, r's character, a text object's kind -- and that one:
    // what the waiting key is picks a function from PENDING_KEYS, each
    // told the waiting key and the one after it. f, F, t, T, ` and ' are
    // none of the table's, and take a motion's argument (afterMotionKey).
    struct PendingKey
    {
        llwchar key;
        bool (ALVimKeymap::*take)(ALTextView& view, llwchar pending, llwchar ch);
    };
    static const PendingKey PENDING_KEYS[];
    bool afterPending(ALTextView& view, llwchar pending, llwchar ch);
    bool afterSurroundWith(ALTextView& view, llwchar pending, llwchar ch);
    bool afterChangeSurround(ALTextView& view, llwchar pending, llwchar ch);
    bool afterSurroundPair(ALTextView& view, llwchar pending, llwchar ch);
    bool afterRegisterName(ALTextView& view, llwchar pending, llwchar ch);
    bool afterMark(ALTextView& view, llwchar pending, llwchar ch);
    bool afterRecord(ALTextView& view, llwchar pending, llwchar ch);
    bool afterPlay(ALTextView& view, llwchar pending, llwchar ch);
    bool afterReplace(ALTextView& view, llwchar pending, llwchar ch);
    bool afterG(ALTextView& view, llwchar pending, llwchar ch);
    bool afterZ(ALTextView& view, llwchar pending, llwchar ch);
    bool afterGr(ALTextView& view, llwchar pending, llwchar ch);
    bool afterBracket(ALTextView& view, llwchar pending, llwchar ch);
    bool afterBigZ(ALTextView& view, llwchar pending, llwchar ch);
    bool afterWindow(ALTextView& view, llwchar pending, llwchar ch);
    bool afterObject(ALTextView& view, llwchar pending, llwchar ch);
    bool afterMotionKey(ALTextView& view, llwchar pending, llwchar ch);
    // A motion by its character, with the count; not ok where the
    // character is no motion.
    Motion motion(ALTextView& view, llwchar ch, S32 count, llwchar arg);
    bool   textObject(ALTextView& view, llwchar kind, llwchar what, S32 count, Span& out);
    void   applyOperator(ALTextView& view, llwchar op, const Span& span, S32 count);
    void   moveTo(ALTextView& view, const ALTextPos& to);
    // Where the caret is for a motion or a command: the visual caret in
    // a visual mode, and on a search line opened over one; else the
    // view's.
    ALTextPos cursor(const ALTextView& view) const;
    bool      isVisual() const { return mMode == Mode::Visual || mMode == Mode::VisualLine || mMode == Mode::VisualBlock; }
    void   finishCommand(bool changed);
    void   clearPending();
    // The count typed before a register's name and the one typed after
    // it, multiplied into the command's, as vim's are: once a key of the
    // command's own comes, which is no digit of the count.
    void   takeRegisterCount();

    // Modes. Every change of one, the parts' as well, is setMode's, which
    // takes up and lets go of what each mode holds while it lasts on the
    // way, whatever changed it: insert mode's step to undo, opened coming
    // in -- or one the caller opened already, `grouped`, where what it put
    // in first, the line `o` opens or what `c` took out, is part of the
    // step -- and closed going out, Ctrl-O's wait over once it is in; a
    // visual selection let go of, kept for gv and '< '>, a block's lit
    // columns put out with it -- but not by a search line opened over it,
    // which keeps it to go back to (mSearchVisual); and what the search
    // line lit as it was typed, put out as it is left.
    void setMode(ALTextView& view, Mode to, bool grouped = false);
    void enterInsert(ALTextView& view, S32 count, bool grouped = false);
    void leaveInsert(ALTextView& view);
    void enterVisual(ALTextView& view, Mode which);
    void leaveVisual(ALTextView& view);
    void showVisual(ALTextView& view);
    Span visualSpan(const ALTextView& view) const;
    // What a block holds of each of its lines, the first to the last
    // (ALVimText::blockPiece): what is cut, lit, replaced and put into.
    std::vector<ALTextRange> blockPieces(const ALTextView& view, const Span& span) const;

    // Registers, the unnamed one on the clipboard.
    void     store(char name, std::string text, bool linewise, bool block, bool yanked);
    Register fetch(char name) const;
    // gp and gP, `past`: the caret after what was put.
    void     put(ALTextView& view, char name, bool after, S32 count, bool past = false);
    // Says a count would make more text than it may, and how much.
    void     tooMuch(size_t bytes);

    // Vim's spelling of a pattern as the search engine's (ALVimSearch).
    typedef ALVimPattern Pattern;
    // Keys fed as typed, for :normal and for a macro, through the mappings
    // where `remap` says -- as a macro's and :normal's are, and :normal!'s
    // are not; false where one failed or said an error, and the rest were
    // not fed. Played from the top, one step to undo.
    bool play(ALTextView& view, const std::vector<Input>& inputs, bool remap);
    // The number at or after the caret on its line, changed by so much;
    // false where there is none.
    bool addToNumber(ALTextView& view, S64 by);
    // The last visual operation, for `.`: the extent it covered -- a
    // block's columns as the reader counts them, or every line's end for
    // one taken with $ -- and the keys from the operator on.
    struct VisualExtent
    {
        bool valid   = false;
        Mode mode    = Mode::Normal;
        S32  lines   = 0;
        S32  columns = 0;
        bool toEnd   = false;
        // Where in the command's inputs the operator was typed.
        size_t opAt = 0;
    };
    void noteVisualOperation(const Span& span, S32 lines_hint = -1);
    // The file named under the caret, as gf reads one; empty for none.
    std::string fileUnderCursor(const ALTextView& view) const;
    // ]s and [s: the caret to the next misspelled word, or the one before,
    // round past the ends, the count times. False where there is none.
    bool misspelling(ALTextView& view, bool forward, S32 count);
    // z=: the misspelled word at the caret put right -- by the count's
    // suggestion, or the one picked; zg: it taken into the dictionary.
    void suggest(ALTextView& view, S32 given);
    // Surround, as surround.vim has it: a stretch with a pair put round
    // it -- ys{motion}, yss, visual S -- on lines of their own where it is
    // lines; and the pair round the caret that a character names taken
    // away (ds), or changed to another's (cs, `with`; 0 to take it away),
    // the count out. False where there is no such pair.
    void surround(ALTextView& view, const Span& span, const std::string& open, const std::string& close);
    bool changeSurround(ALTextView& view, llwchar target, llwchar with, S32 count);
    // zo zc za zR zM zj zk [z ]z: the code editor's folds.
    bool foldCommand(ALTextView& view, llwchar ch, llwchar prefix);
    // [( [{ ]) ]}: the bracket the caret is inside of, open before it or
    // closed after it, the count out; false where there is none.
    bool unmatchedBracket(ALTextView& view, llwchar bracket, S32 count, ALTextPos& out) const;
    // A jump from `from`: vim's context marks, '' and ``, set there, and
    // the host told.
    void noteJump(ALTextView& view, const ALTextPos& from);

    // What the mode says, in the skin's words where the skin has them
    // (strings.xml, keys Vim*), else in vim's own English; [NAME]s
    // filled in from the map either way.
    static std::string said(const char* key, const std::string& english, const LLStringUtil::format_map_t& args = {});
    static std::string substitutionsSaid(S32 count, S32 lines);
    static std::string matchesSaid(S32 count, S32 lines);
    void bump() { ++mGeneration; }

    Mode  mMode = Mode::Normal;
    Hooks mHooks;
    U32   mGeneration = 1;

    // What normal mode has read so far: a count (0 for none), a register
    // (0 for the unnamed) with the count typed before its name, an
    // operator waiting for its motion with its own count, and a character
    // waiting for the one that completes it (f, t, r, m, `, ', ", g, z, Z,
    // i, a) with the count that came before.
    S32     mCount        = 0;
    char    mRegister     = 0;
    S32     mRegisterCount = 0;
    llwchar mOperator     = 0;
    S32     mOperatorCount = 0;
    llwchar mPending      = 0;
    // Insert mode's Ctrl-O: 2 as it is typed, 1 while the one command it
    // allows waits, then back to inserting. And its Ctrl-V: the next key
    // put in as it is -- a tab, a character, or uXXXX by its code.
    S32         mOneCommand = 0;
    bool        mLiteral    = false;
    std::string mLiteralCode;
    // For a text object after an operator: whether the pending i or a
    // was read.
    llwchar mObjectKind   = 0;
    // Surround's: the stretch ys or visual S took, while the character
    // to surround it with is waited for, which holds the command open;
    // and the pair cs is changing.
    Span    mSurroundSpan;
    bool    mSurroundWaiting = false;
    llwchar mSurroundOld     = 0;

    // f, F, t and T, for ; and ,.
    llwchar mFindChar    = 0;
    bool    mFindForward = true;
    bool    mFindTill    = false;
    // Searching: the last pattern, its matches, the search line lit.
    ALVimSearch mSearch{ *this };
    friend class ALVimSearch;

    // The marks, which move with the text as it is edited: the document
    // they are in is listened to from the first key on it.
    std::map<char, ALTextPos> mMarks;
    const ALTextDocument*     mMarksIn = nullptr;
    // A plain view's bracket index (bracketsOf).
    mutable std::unique_ptr<ALBracketIndex> mPlainBrackets;
    boost::signals2::scoped_connection mMarksSlide;
    // Everything held at a place in the text, moved with each edit made to
    // it, whoever makes it: the marks and the last visual area, the visual
    // area being made, the last match gone to, where insert mode began
    // typing, the lines a block insert goes onto, and the :s edits still to
    // be asked about -- those an edit cut through let go.
    void                      slideHeld(const ALTextDocument::Edit& edit);
    void                      followDocument(ALTextView& view);

    // The column j and k want, as vim's curswant: where the caret was
    // drawn before a vertical move, kept through short lines, or past
    // every line's end after $; forgotten by any other move. -1 for
    // none.
    S32  mWantColumn  = -1;
    bool mVerticalMove = false;

    // Visual mode: where it started and where its caret is -- on a
    // character, which the view's selection reaches past -- and the last
    // visual selection for gv, with whether a block of it was taken to
    // every line's end with $, as vim keeps its curswant.
    ALTextPos mVisualAnchor;
    ALTextPos mVisualCaret;
    Mode      mVisualLast = Mode::Normal;
    ALTextPos mVisualLastAnchor;
    ALTextPos mVisualLastCaret;
    bool      mVisualLastToEnd = false;
    // The visual mode the search line was opened over, which entering the
    // line or letting it go goes back to, what is found moving the visual
    // caret as vim's search does; normal mode for one opened from any
    // other, and while no search line is being typed (setMode).
    Mode      mSearchVisual = Mode::Normal;

    // Insert mode: how many times what is typed goes in, and whether on a
    // line of its own each time, as o and O open one; where the typing
    // began, moved with each edit, so that what was typed is read off the
    // text -- what a backspace or Ctrl-W took back gone from it, the
    // indent a Return made in it; whether the caret has been moved off it
    // since, and from where, which the key meant to move it may not have;
    // and a block's lines to put it on as well -- at its first column as
    // the reader counts them, or for A past its last, or past each line's
    // end where it was taken with $; what the last insert typed, for
    // Control-A; and whether a Control-R waits for the register to put in.
    S32         mInsertCount = 1;
    bool        mInsertOpened = false;
    ALTextPos   mInsertStart;
    bool        mInsertMoved = false;
    ALTextPos   mInsertLeftAt;
    std::string mLastTyped;
    bool        mInsertRegister = false;
    bool        mBlockInsert       = false;
    S32         mBlockFirst        = 0;
    S32         mBlockLast         = 0;
    S32         mBlockColumn       = 0;
    bool        mBlockAppend       = false;
    bool        mBlockToEnd        = false;

    // The settings and histories shared with the other buffers' keymaps.
    std::shared_ptr<Shared>  mShared = std::make_shared<Shared>();
    // The : and search lines being typed (ALVimCommandLine).
    ALVimCommandLine         mCommandLine{ *this };
    friend class ALVimCommandLine;
    // The : commands (ALVimExCommands), held apart since they name the
    // keymap's own types.
    std::unique_ptr<ALVimExCommands> mEx;
    friend class ALVimExCommands;

    // What the last change was, as it was typed, for . -- gathered from
    // the first key of a command until the command is done, and kept
    // where the command changed the text.
    std::vector<Input> mCommandInputs;
    std::vector<Input> mLastChange;
    bool               mReplaying = false;
    // The key being fed was left to the view to type, in insert mode: part
    // of what was typed, for `.` and a macro, though nobody here took it.
    bool               mTypedByView = false;
    // The characters held to put in at once (holds).
    std::string        mHeldTyped;
    // The last change, where it was an operator over a visual
    // selection: `.` selects as much again from the caret and does
    // the operator over it.
    VisualExtent       mVisualPending;
    VisualExtent       mLastVisual;

    // Macros: the register being recorded into, or 0, and what has
    // been typed since; the last one played, for @@; and how deep the
    // playing goes, so that a macro playing itself stops.
    char               mRecording = 0;
    std::vector<Input> mRecorded;
    char               mLastPlayed = 0;
    S32                mPlaying    = 0;

    // Keys typed and held while they may be the start of a mapping, and
    // since when, for the timeout; how deep feeding what mappings stand
    // for goes.
    std::deque<Held> mTypeahead;
    LLTimer          mTypeaheadSince;
    S32              mMapped = 0;

    std::string mMessage;
    bool        mMessageError = false;
    // The last key's command failed: said an error, or was a motion that
    // could not move -- f with no such character, j on the last line --
    // which vim treats as one. What stops a macro, :normal's keys, the
    // rest of a mapping and @'s count, as it does in vim.
    bool        mFailed = false;
};
