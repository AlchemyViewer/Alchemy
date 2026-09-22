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

#include "altextview.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Vim over the text view, as a keymap with state: normal, insert, replace,
// visual, visual-line and visual-block modes; counts; the operators d c y
// > < = and g~ gu gU, composed with the motions h j k l w W b B e E 0 ^ $
// gg G { } % f F t T ; , H M L n N * # ` ' | and with the text objects iw
// aw iW aW i" a" i' a' i` a` i( a( i[ a[ i{ a{ i< a< it at ip ap; x X s S
// C D Y p P J gJ r ~ o O i a I A u Ctrl-R and . to do the last change
// again; registers, the unnamed one being the system clipboard, with 0
// for the last yank and a-z by name (A-Z to add); marks a-z, ` and '; a
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
        // the pick, which puts it on the line to edit and enter. Without
        // one, the line opens at the last entered.
        std::function<void(ALTextView& view, llwchar kind, const std::vector<std::string>& history, std::function<void(const std::string&)> chosen)> historyWindow;
    };

    ALVimKeymap();
    ~ALVimKeymap() override;

    Mode   mode() const { return mMode; }
    Hooks& hooks() { return mHooks; }
    // What was typed after : or / so far.
    const std::string& commandLine() const { return mLine; }
    bool               typingLine(std::string& line, S32& caret) const override;
    // A line put up to be edited and entered, as the history window
    // hands one back: the : or / line opened with the text on it.
    void               takeLine(ALTextView& view, llwchar kind, const std::string& text);
    // What the mode last said: a pattern not found, lines yanked, a
    // command unknown; cleared by the next key.
    std::string message() const override { return mMessage; }
    bool        messageIsError() const override { return mMessageError; }
    // A register's text, or nothing.
    std::string registerText(char name) const;

    bool        handleKey(ALTextView& view, KEY key, MASK mask) override;
    bool        handleChar(ALTextView& view, llwchar ch) override;
    bool        inserting() const override;
    std::string status() const override;
    U32         generation() const override { return mGeneration; }
    void        mouseChanged(ALTextView& view) override;

    // Whether keys are being recorded into a register, and which.
    bool recording() const { return mRecording != 0; }
    char recordingInto() const { return mRecording; }

    // What one vim shares among its buffers, which here are the keymaps
    // of one studio's editors: the lines entered on the : line and on
    // the search line, oldest first, for Up and Down on the line, q: q/
    // @: and :history; and the settings a :set changes, ignorecase and
    // smartcase. The keymap's own unless told to share another's.
    struct Shared
    {
        std::vector<std::string> command;
        std::vector<std::string> search;
        bool                     ignoreCase = false;
        bool                     smartCase  = false;
    };
    const Shared&           shared() const { return *mShared; }
    std::shared_ptr<Shared> sharedState() const { return mShared; }
    void                    share(std::shared_ptr<Shared> shared);

    // One thing typed: a character, or a key with its modifiers.
    struct Input
    {
        bool    isChar = false;
        llwchar ch     = 0;
        KEY     key    = KEY_NONE;
        MASK    mask   = MASK_NONE;
    };
    // Keys as text, the way a macro's register holds them: characters
    // as themselves, `<` as `<lt>`, keys by name -- <Esc>, <CR>, <BS>,
    // <Tab>, <Del>, <Up>, <Down>, <Left>, <Right>, <Home>, <End>,
    // <PageUp>, <PageDown> -- with C-, S- and A- before a modified one;
    // and back again.
    static std::string        encodeInputs(const std::vector<Input>& inputs);
    static std::vector<Input> decodeInputs(std::string_view text);

private:
    struct Register
    {
        std::string text;
        bool        linewise = false;
        bool        block    = false;
    };
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
    // A stretch an operator works on.
    struct Span
    {
        ALTextRange range;
        bool        linewise = false;
        bool        block    = false;
    };

    bool feed(ALTextView& view, const Input& input);
    bool normal(ALTextView& view, const Input& input);
    bool insert(ALTextView& view, const Input& input);
    bool commandLine(ALTextView& view, const Input& input);

    // Normal mode's command, once the count, the register and any
    // operator have been read; false where the character is not one.
    bool command(ALTextView& view, llwchar ch);
    // A motion by its character, with the count; not ok where the
    // character is no motion.
    Motion motion(ALTextView& view, llwchar ch, S32 count, llwchar arg);
    bool   textObject(ALTextView& view, llwchar kind, llwchar what, S32 count, Span& out);
    void   applyOperator(ALTextView& view, llwchar op, const Span& span, S32 count);
    void   moveTo(ALTextView& view, const ALTextPos& to);
    // Where the caret is for a motion or a command: the visual caret in
    // a visual mode, else the view's.
    ALTextPos cursor(const ALTextView& view) const;
    bool      isVisual() const { return mMode == Mode::Visual || mMode == Mode::VisualLine || mMode == Mode::VisualBlock; }
    void   finishCommand(bool changed);
    void   clearPending();

    // Modes.
    void enterInsert(ALTextView& view, S32 count);
    void leaveInsert(ALTextView& view);
    void enterVisual(ALTextView& view, Mode which);
    void leaveVisual(ALTextView& view);
    void showVisual(ALTextView& view);
    Span visualSpan(const ALTextView& view) const;

    // Registers, the unnamed one on the clipboard.
    void     store(char name, std::string text, bool linewise, bool block, bool yanked);
    Register fetch(char name) const;
    void     put(ALTextView& view, char name, bool after, S32 count);

    // Searching, with the last pattern kept for n and N.
    bool search(ALTextView& view, const std::string& pattern, bool forward, S32 count, bool whole_word);

    // The : line.
    void runCommand(ALTextView& view, const std::string& line);
    bool substitute(ALTextView& view, S32 first, S32 last, const std::string& spec);
    // Vim's spelling of a replacement -- & for the match, \1 for a group,
    // ~ for the last replacement, \r for a line break -- as the search
    // engine's.
    std::string replacementOf(const std::string& with) const;
    // Vim's spelling of a pattern -- magic, with \( \) \| \+ \? \{ \< \>
    // \zs \ze and the classes, \v for very magic and \V for very nomagic
    // -- as the search engine's; and how its case is matched: \c and \C
    // in it, else what the caller forces, else the ignorecase and
    // smartcase settings.
    struct Pattern
    {
        std::string regex;
        bool        caseSensitive = true;
        // Where a match may start, as \%V \%# \%23l \%<23l \%>23l \%23c
        // and \%23v ask -- the last visual area, the caret, a line, a
        // column -- since the engine has no such atoms: a match anywhere
        // else is dropped. Vim ties the atom to its place in the pattern;
        // here it is the match's start, which is where they are written.
        struct Where
        {
            enum class Kind : U8
            {
                Visual,
                Caret,
                Line,
                Column,
                FileStart,
                FileEnd
            };
            Kind kind    = Kind::Line;
            // -1 before, 0 at, 1 after; the number, 1-based as vim counts.
            S32  side    = 0;
            S32  number  = 0;
            // Written after a \zs: about the match as reported, not the
            // whole of what the pattern matched.
            bool afterStart = false;
        };
        std::vector<Where> where;
        // The group the match is, where a \zs made one; the search is
        // told, and says where the whole began. Where a \zs inside
        // brackets had to be the engine's \K instead, the pattern
        // without it, whose matches end where these do and say where
        // the whole began.
        S32         matchGroup = 0;
        std::string wholeRegex;
        // Whether a match may cross a line's end: the pattern has \n or
        // a \_ class in it, which is what reaches one.
        bool        acrossLines = false;
    };
    Pattern patternOf(const std::string& vim, std::optional<bool> force_case = std::nullopt) const;
    // The matches a pattern's places allow, the rest dropped; `wholes`
    // says where each whole match began, for the places before a \zs.
    void    constrain(ALTextView& view, const Pattern& pattern, std::vector<ALTextRange>& matches, const std::vector<ALTextPos>& wholes) const;
    // The pattern's matches within a scope, the places applied, with
    // where each whole match began.
    std::vector<ALTextRange> matchesOf(ALTextView& view, const Pattern& pattern, ALTextSearchOptions options, const ALTextRange* scope, std::string& error,
                                       std::vector<ALTextPos>& wholes) const;
    // The :s asking about each match: the edits left to make, in order,
    // and the one being asked about; the text put in, for the question.
    struct Confirming
    {
        std::vector<std::pair<ALTextRange, std::string>> edits;
        size_t                                           at      = 0;
        S32                                              made    = 0;
        S32                                              lines   = 0;
        S32                                              lastLine = -1;
        // While a :g runs its command over the lines, an asking :s puts
        // its edits here and asks nothing; the asking starts, over the
        // lot in order, once the :g is through.
        bool                                             gathering = false;
    };
    Confirming mConfirming;
    bool       confirmKey(ALTextView& view, const Input& input);
    // One of the edits made, the ones after it moved by what it changed.
    void       applyConfirmed(ALTextView& view, size_t index);
    void       askNext(ALTextView& view);
    void       endConfirming(ALTextView& view);
    // The history of a line kind, and the line entered into it.
    std::vector<std::string>& historyOf(llwchar kind) { return kind == ':' ? mShared->command : mShared->search; }
    void                      remember(llwchar kind, const std::string& line);
    // g and v: the command over every line the pattern picks out, or
    // every line it does not.
    bool global(ALTextView& view, S32 first, S32 last, bool ranged, const std::string& spec, bool invert);
    // Keys fed as typed, for :normal and for a macro; false where they
    // ended in an error message.
    bool play(ALTextView& view, const std::vector<Input>& inputs);
    // The number at or after the caret on its line, changed by so much;
    // false where there is none.
    bool addToNumber(ALTextView& view, S64 by);
    // The last visual operation, for `.`: the extent it covered and the
    // keys from the operator on.
    struct VisualExtent
    {
        bool valid   = false;
        Mode mode    = Mode::Normal;
        S32  lines   = 0;
        S32  columns = 0;
        // Where in the command's inputs the operator was typed.
        size_t opAt = 0;
    };
    void noteVisualOperation(const Span& span, S32 lines_hint = -1);

    void say(const std::string& message, bool error = false);
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
    // (0 for the unnamed), an operator waiting for its motion with its
    // own count, and a character waiting for the one that completes it
    // (f, t, r, m, `, ', ", g, z, Z, i, a) with the count that came before.
    S32     mCount        = 0;
    char    mRegister     = 0;
    llwchar mOperator     = 0;
    S32     mOperatorCount = 0;
    llwchar mPending      = 0;
    // For a text object after an operator: whether the pending i or a
    // was read.
    llwchar mObjectKind   = 0;

    // f, F, t and T, for ; and ,.
    llwchar mFindChar    = 0;
    bool    mFindForward = true;
    bool    mFindTill    = false;
    // The search, for n and N; :s sets it too. How case is matched is
    // in the shared state: sensitive unless :set ignorecase says, as
    // vim's own default is.
    std::string mSearchPattern;
    bool        mSearchForward   = true;
    bool        mSearchWholeWord = false;

    std::map<char, Register>  mRegisters;
    Register                  mUnnamed;
    std::map<char, ALTextPos> mMarks;

    // Visual mode: where it started and where its caret is -- on a
    // character, which the view's selection reaches past -- and the last
    // visual selection for gv.
    ALTextPos mVisualAnchor;
    ALTextPos mVisualCaret;
    Mode      mVisualLast = Mode::Normal;
    ALTextPos mVisualLastAnchor;
    ALTextPos mVisualLastCaret;

    // Insert mode: how many times what is typed goes in, the characters
    // typed so far, and a block's lines to put them on as well.
    S32         mInsertCount = 1;
    std::string mTyped;
    bool        mBlockInsert       = false;
    S32         mBlockFirst        = 0;
    S32         mBlockLast         = 0;
    S32         mBlockColumn       = 0;
    bool        mBlockAppend       = false;
    // The char an insert began after, for a replace's backspace.
    ALTextPos   mInsertStart;

    // The : or / line being typed, and which; the lines entered before,
    // : and search apart, oldest first, for Up and Down on the line,
    // q: and @:; and where Up has walked to in them, with what was
    // typed before it was pressed, which Down comes back to. -1 while
    // not walking.
    std::string              mLine;
    llwchar                  mLineKind = ':';
    std::shared_ptr<Shared>  mShared = std::make_shared<Shared>();
    S32                      mHistoryAt = -1;
    std::string              mHistoryPrefix;
    // Where in the line the next character goes, in bytes.
    size_t                   mLineCursor = 0;
    // The last :s, for :s with nothing after it, :&, :&&, & and g&: its
    // replacement as it read once ~ was put in, and its flags.
    std::string mLastReplacement;
    std::string mLastSubstituteFlags;

    // What the last change was, as it was typed, for . -- gathered from
    // the first key of a command until the command is done, and kept
    // where the command changed the text.
    std::vector<Input> mCommandInputs;
    std::vector<Input> mLastChange;
    bool               mReplaying = false;
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

    std::string mMessage;
    bool        mMessageError = false;
};
