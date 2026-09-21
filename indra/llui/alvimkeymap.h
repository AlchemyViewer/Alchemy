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
// search line for / and ?; and a : line for a line number, s///, set,
// g and v over the lines a pattern picks out, normal, and whatever the
// hooks take -- w, q, wq, x, q! and the studio's own. Macros: q to
// record keys into a register, @ to play them. Ctrl-A and Ctrl-X on a
// number. The mouse: a click puts the caret on a character, a drag is
// a visual selection. It works the view through what the view exposes
// and nothing more, so the same keys drive any text view.
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
        Search
    };

    struct Hooks
    {
        // A : command the mode has no answer to itself: w, q, wq, x, q!,
        // wa, qa, and anything of the host's. True where it was taken.
        std::function<bool(ALTextView& view, const std::string& name, const std::string& args)> command;
        // The = operator over lines first to last.
        std::function<void(ALTextView& view, S32 first, S32 last)> format;
    };

    ALVimKeymap();
    ~ALVimKeymap() override;

    Mode   mode() const { return mMode; }
    Hooks& hooks() { return mHooks; }
    // What was typed after : or / so far.
    const std::string& commandLine() const { return mLine; }
    // What the mode last said: a pattern not found, lines yanked, a
    // command unknown; cleared by the next key.
    const std::string& message() const { return mMessage; }
    bool               messageIsError() const { return mMessageError; }
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
    // The search, for n and N.
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

    // The : or / line being typed, and which.
    std::string mLine;
    llwchar     mLineKind = ':';

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
