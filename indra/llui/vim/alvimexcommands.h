/**
 * @file alvimexcommands.h
 * @brief Vim's : commands: running them, :s and its asking, :g, :set, :registers and the vimrc.
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
#include "altextdocument.h"
#include "alvimkeymap.h"

#include <boost/signals2/connection.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Vim's : commands, for the vim keymap (ALVimKeymap): a line entered on the
// : line run -- its range read, the command found and done -- among them
// :s with the asking a c flag makes, :g over the lines a pattern picks,
// :set, :registers and :marks; and a vimrc read into the keymap's shared
// state. What the commands do to the text and the caret, and what they
// say, they do and say through the keymap.
class ALVimExCommands
{
public:
    explicit ALVimExCommands(ALVimKeymap& vim) : mVim(vim) {}

    // The : line: its commands in turn, each to the | that ends it, as
    // vim's :bar has them, an error ending the line there.
    void runCommand(ALTextView& view, const std::string& line);
    // The : line run as one entered on it, by a key from the keyboard or
    // not -- a macro's, :normal's, a mapping's -- as vim's KeyTyped has it:
    // a :s over one line says how many it made only where it was typed.
    void runEntered(ALTextView& view, const std::string& line, bool typed);
    bool substitute(ALTextView& view, S32 first, S32 last, const std::string& spec);
    // Vim's spelling of a replacement -- & for the match, \1 for a group,
    // ~ for the last replacement, \r for a line break -- as the search
    // engine's.
    std::string replacementOf(const std::string& with) const;
    // The position a range address names on the : line -- a number, .,
    // $, 'x, '< '>, with an offset, or an offset alone from the caret's
    // line -- read from `at` on; false where the line has none there.
    // `before_first`, where given, says whether it named a line before
    // the first -- 0, where lines are numbered from one -- which is the
    // first line for most commands, and the one :put puts under.
    bool lineAddress(ALTextView& view, const std::string& line, size_t& at, S32& out, bool* before_first = nullptr) const;

    // The :s asking about each match: the edits left to make, in order,
    // and the one being asked about; the text put in, for the question.
    struct Confirming
    {
        std::vector<std::pair<ALTextRange, std::string>> edits;
        size_t                                           at      = 0;
        S32                                              made    = 0;
        S32                                              lines   = 0;
        S32                                              lastLine = -1;
        // Whether the ones still to come are lit: once, when the asking
        // starts, then let go of as it passes them.
        bool                                             lit = false;
        // The step to undo that every edit said yes to goes into, as the
        // :s is one step: gone on with at each answer, the group closed
        // while the question waits, so that nothing made meanwhile -- a
        // format on save -- joins it (ALTextUndo::resumeGroup).
        U64                                              undoStep = 0;
        // What follows the asking :s on its : line, after a |: run once
        // the asking is done, as vim runs it after.
        std::string                                      then;
    };
    Confirming confirming;
    bool       confirmKey(ALTextView& view, const ALVimInput& input);
    // One of the edits made, the ones after it moved by what it changed.
    void       applyConfirmed(ALTextView& view, size_t index);
    // All the rest, from the one asked about on, made as one edit, as :s
    // makes them without asking.
    void       applyRest(ALTextView& view);
    void       askNext(ALTextView& view);
    // The asking done, and what follows it on its : line run -- or, a :g
    // waiting on it, the rest of that line's commands and then the :g's
    // lines after it -- where `go_on` says; without it, a :g waiting is
    // let go of, its lines not visited, what it made one step to undo.
    void       endConfirming(ALTextView& view, bool go_on = true);

    // While a :g runs a command that changes only the line it is on --
    // :d, :s, :> and :< with no lines of their own -- the command puts its
    // edits here, measured in the text as it was, and they go in at once
    // when the :g is through: one edit, heard of once, for the lot. The
    // place the caret lands is kept likewise.
    struct GlobalBatch
    {
        std::vector<std::pair<ALTextRange, std::string>> edits;
        // Where the caret goes, in the text as it was, and how many lines
        // below that once the edits are in.
        ALTextPos                                        landing;
        S32                                              landingBelow = 0;
        bool                                             landed       = false;
        // An asking :s, which asks line by line and so is no batch's: the
        // :g walks its lines instead.
        bool                                             asks         = false;
    };
    GlobalBatch* globalBatch = nullptr;
    // Whether a :g's command is one it batches.
    static bool globalBatches(const std::string& command);
    void        applyGlobalBatch(ALTextView& view, GlobalBatch& batch);
    // g and v: the command over every line the pattern picks out, or
    // every line it does not.
    bool global(ALTextView& view, S32 first, S32 last, bool ranged, const std::string& spec, bool invert);

    // :registers and :marks, listed as vim lists them, of the names given
    // or of all.
    void listRegisters(ALTextView& view, const std::string& names);
    void listMarks(ALTextView& view, const std::string& names);
    void list(ALTextView& view, const std::string& text);

    // A vimrc read into the shared state, and one of each view's own
    // options set on a view (ALVimKeymap::source and setViewOption, which
    // say what each does).
    static void source(ALVimKeymap::Shared& shared, std::string_view text, const std::function<bool(const std::string& option)>& host,
                       std::vector<std::string>& errors);
    static bool setViewOption(ALTextView& view, const std::string& option, std::string& shown, std::string& error);

    // The last :s, for :s with nothing after it, :&, :&&, & and g&: its
    // replacement as it read once ~ was put in, and its flags.
    std::string lastReplacement;
    std::string lastSubstituteFlags;

    // Whether a :g is running its command over lines, which says nothing
    // until it is through, as vim's global_busy has it.
    bool inGlobal() const { return mInGlobal; }

private:
    // One command of a : line, no | ending it.
    void runOneCommand(ALTextView& view, const std::string& line);

    // A :g visiting its lines one at a time, from the top down, the
    // topmost left each time, as vim marks them: a mark moves with the
    // text and goes with its line. An asking :s on one of them -- asked
    // line by line, as vim asks, each line's a, q and l its own -- makes
    // the :g wait, the step it makes closed while the question stands;
    // once the asking is done, the rest of that line's commands run and
    // the :g goes on (endConfirming), in the same step.
    struct GlobalWalk
    {
        std::string                        command;
        // Whether the command has an address of its own, and runs as it is
        // written rather than given the line's number.
        bool                               addressed   = false;
        ALAnchoredRanges<ALTextPos>        marks;
        boost::signals2::scoped_connection following;
        S32                                linesBefore = 0;
    };
    // The lines still to visit, until there are none, an error, or an
    // asking :s; the :g finished where it is through.
    bool walkGlobal(ALTextView& view);
    void pauseGlobal(ALTextView& view);
    // What the :g made said once, as vim's :g says it, and its step closed.
    bool finishGlobal(ALTextView& view, S32 lines_before);

    ALVimKeymap& mVim;
    // Whether a :g is running its command over lines, which another :g
    // may not do, as vim has it (E147); and what its :s commands made,
    // and over how many lines, said once it is through.
    bool mInGlobal = false;
    S32  mGlobalSubstitutions = 0;
    S32  mGlobalSubstitutedLines = 0;
    std::unique_ptr<GlobalWalk> mGlobalWalk;
    // Whether the line being run was entered by a key typed (runEntered).
    bool mLineTyped = false;
};
