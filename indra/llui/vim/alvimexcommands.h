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

#include "altextdocument.h"
#include "alvimkeymap.h"

#include <functional>
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

    // The : line.
    void runCommand(ALTextView& view, const std::string& line);
    bool substitute(ALTextView& view, S32 first, S32 last, const std::string& spec);
    // Vim's spelling of a replacement -- & for the match, \1 for a group,
    // ~ for the last replacement, \r for a line break -- as the search
    // engine's.
    std::string replacementOf(const std::string& with) const;
    // The position a range address names on the : line -- a number, .,
    // $, 'x, '< '>, with an offset -- read from `at` on; false where
    // the line has none there.
    bool lineAddress(ALTextView& view, const std::string& line, size_t& at, S32& out) const;

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
        // While a :g runs its command over the lines, an asking :s puts
        // its edits here and asks nothing; the asking starts, over the
        // lot in order, once the :g is through.
        bool                                             gathering = false;
    };
    Confirming confirming;
    bool       confirmKey(ALTextView& view, const ALVimInput& input);
    // One of the edits made, the ones after it moved by what it changed.
    void       applyConfirmed(ALTextView& view, size_t index);
    // All the rest, from the one asked about on, made as one edit, as :s
    // makes them without asking.
    void       applyRest(ALTextView& view);
    void       askNext(ALTextView& view);
    void       endConfirming(ALTextView& view);

    // While a :g runs a command that changes only the line it is on --
    // :d, :s, :> and :< with no lines of their own -- the command puts its
    // edits here, measured in the text as it was, and they go in at once
    // when the :g is through: one edit, heard of once, for the lot. The
    // place the caret lands is kept likewise, and what is said is added up.
    struct GlobalBatch
    {
        std::vector<std::pair<ALTextRange, std::string>> edits;
        // Where the caret goes, in the text as it was, and how many lines
        // below that once the edits are in.
        ALTextPos                                        landing;
        S32                                              landingBelow = 0;
        bool                                             landed       = false;
        S32                                              substitutions = 0;
        S32                                              substitutedLines = 0;
        S32                                              deletedLines = 0;
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

private:
    ALVimKeymap& mVim;
    // Whether a :g is running its command over lines, which another :g
    // may not do, as vim has it (E147).
    bool mInGlobal = false;
};
