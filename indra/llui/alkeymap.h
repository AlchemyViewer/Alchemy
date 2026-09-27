/**
 * @file alkeymap.h
 * @brief What a key does in a text view: the commands, and the keys bound to them.
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

#include "stdtypes.h"
#include "indra_constants.h"

#include <optional>
#include <string_view>
#include <vector>

// Everything a text view can be told to do from the keyboard. A keymap
// turns a key and its modifiers into one of these; the view does the rest,
// so a second keymap -- a vim mode -- is a second table over the same
// commands and nothing more.
enum class ALEditorCommand : U8
{
    None,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    MoveWordLeft,
    MoveWordRight,
    MoveLineStart,
    MoveLineEnd,
    MoveDocStart,
    MoveDocEnd,
    MovePageUp,
    MovePageDown,
    SelectLeft,
    SelectRight,
    SelectUp,
    SelectDown,
    SelectWordLeft,
    SelectWordRight,
    SelectLineStart,
    SelectLineEnd,
    SelectDocStart,
    SelectDocEnd,
    SelectPageUp,
    SelectPageDown,
    SelectAll,
    DeleteLeft,
    DeleteRight,
    DeleteWordLeft,
    DeleteWordRight,
    DeleteToLineStart,
    DeleteToLineEnd,
    NewLine,
    Indent,
    Unindent,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    Delete,
    ToggleComment,
    DuplicateLine,
    MoveLineUp,
    MoveLineDown,
    DeleteLine,
    Fold,
    Unfold,
    FoldAll,
    UnfoldAll,
    Complete,
    SignatureHelp,
    GoToDefinition,
    FindReferences,
    Rename,
    Find,
    Replace,
    FindNext,
    FindPrevious,
    // What would put right the problem at the caret, offered as a list.
    QuickFix,
    NextMisspelling,
    PreviousMisspelling,
    JoinLines,
    PreviousChange,
    NextChange,
    NextFunction,
    PreviousFunction,
    SelectFunction,
    // To the bracket paired with the one at the caret, or to the closer
    // of the innermost pair around it.
    GoToMatchingBracket,
    // A line under the caret's, or over it, indented where it goes, the
    // caret on it whatever it was in the middle of; and the caret's line
    // selected whole, and again the next with it.
    InsertLineBelow,
    InsertLineAbove,
    SelectLine,
    COUNT
};

const char*                    alEditorCommandName(ALEditorCommand command);
std::optional<ALEditorCommand> alEditorCommandFromName(std::string_view name);

class ALKeymap
{
public:
    struct Binding
    {
        KEY             key;
        MASK            mask;
        ALEditorCommand command;
    };

    // A key with exactly these modifiers does this; a binding of the same
    // key and modifiers is replaced.
    void bind(KEY key, MASK mask, ALEditorCommand command);
    void unbind(KEY key, MASK mask);
    ALEditorCommand lookup(KEY key, MASK mask) const;
    // The first keys bound to a command, for a menu to show beside it;
    // false where none are.
    bool keysFor(ALEditorCommand command, KEY& key, MASK& mask) const;

    const std::vector<Binding>& bindings() const { return mBindings; }

    // The viewer's conventions: arrows, home and end, page up and down,
    // shift to select and control for words and the document's ends,
    // control with Z, Y, X, C, V and A. Control is the command key on a
    // Mac, where alt with an arrow is a word as well. Then the editor's
    // own: control-slash comments, alt-up and alt-down move lines,
    // control-shift-D duplicates one and control-shift-K deletes one,
    // control-shift with a square bracket folds and unfolds,
    // control-space completes, F12 goes to a definition, shift-F12 finds
    // the references, F2 renames, control-F finds, control-H (and on a
    // Mac, which keeps command-H, command-option-F) replaces, and F3 and
    // shift-F3 go to the next and the previous match. Backspace and Return
    // held with shift are still Backspace and Return.
    static ALKeymap standard();

private:
    std::vector<Binding> mBindings;
};
