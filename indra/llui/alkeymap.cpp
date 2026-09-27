/**
 * @file alkeymap.cpp
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

#include "linden_common.h"

#include "alkeymap.h"

#include <algorithm>

const char* alEditorCommandName(ALEditorCommand command)
{
    static const char* const NAMES[] = {
        "none",          "move_left",       "move_right",      "move_up",          "move_down",        "move_word_left",
        "move_word_right", "move_line_start", "move_line_end", "move_doc_start",   "move_doc_end",     "move_page_up",
        "move_page_down", "select_left",    "select_right",    "select_up",        "select_down",      "select_word_left",
        "select_word_right", "select_line_start", "select_line_end", "select_doc_start", "select_doc_end", "select_page_up",
        "select_page_down", "select_all",   "delete_left",     "delete_right",     "delete_word_left", "delete_word_right",
        "delete_to_line_start", "delete_to_line_end",
        "new_line",      "indent",          "unindent",        "undo",             "redo",             "cut",
        "copy",          "paste",           "delete",          "toggle_comment",   "duplicate_line",   "move_line_up",
        "move_line_down", "delete_line",    "fold",            "unfold",           "fold_all",         "unfold_all",
        "complete",      "signature_help", "go_to_definition", "find_references", "rename",         "find",             "replace",
        "find_next",     "find_previous",   "quick_fix",       "next_misspelling", "previous_misspelling",
        "join_lines",    "previous_change", "next_change",     "next_function",   "previous_function", "select_function",
        "go_to_bracket",
    };
    static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == static_cast<size_t>(ALEditorCommand::COUNT), "every command has a name");
    const size_t index = static_cast<size_t>(command);
    return index < static_cast<size_t>(ALEditorCommand::COUNT) ? NAMES[index] : "none";
}

std::optional<ALEditorCommand> alEditorCommandFromName(std::string_view name)
{
    for (size_t i = 1; i < static_cast<size_t>(ALEditorCommand::COUNT); ++i)
    {
        const ALEditorCommand command = static_cast<ALEditorCommand>(i);
        if (name == alEditorCommandName(command))
        {
            return command;
        }
    }
    return std::nullopt;
}

void ALKeymap::bind(KEY key, MASK mask, ALEditorCommand command)
{
    for (Binding& binding : mBindings)
    {
        if (binding.key == key && binding.mask == mask)
        {
            binding.command = command;
            return;
        }
    }
    mBindings.push_back(Binding{ key, mask, command });
}

void ALKeymap::unbind(KEY key, MASK mask)
{
    mBindings.erase(std::remove_if(mBindings.begin(), mBindings.end(),
                                   [&](const Binding& b) { return b.key == key && b.mask == mask; }),
                    mBindings.end());
}

bool ALKeymap::keysFor(ALEditorCommand command, KEY& key, MASK& mask) const
{
    bool found = false;
    for (const Binding& binding : mBindings)
    {
        if (binding.command != command)
        {
            continue;
        }
#if LL_DARWIN
        // Command-Space is the system's, and never reaches the viewer:
        // a Control binding of the same command is the one to show.
        if (binding.key == ' ' && (binding.mask & MASK_CONTROL))
        {
            if (!found)
            {
                key   = binding.key;
                mask  = binding.mask;
                found = true;
            }
            continue;
        }
#endif
        key  = binding.key;
        mask = binding.mask;
        return true;
    }
    return found;
}

ALEditorCommand ALKeymap::lookup(KEY key, MASK mask) const
{
    for (const Binding& binding : mBindings)
    {
        if (binding.key == key && binding.mask == mask)
        {
            return binding.command;
        }
    }
    return ALEditorCommand::None;
}

ALKeymap ALKeymap::standard()
{
    typedef ALEditorCommand C;
    ALKeymap map;
    const MASK word = MASK_CONTROL;
    map.bind(KEY_LEFT, MASK_NONE, C::MoveLeft);
    map.bind(KEY_RIGHT, MASK_NONE, C::MoveRight);
    map.bind(KEY_UP, MASK_NONE, C::MoveUp);
    map.bind(KEY_DOWN, MASK_NONE, C::MoveDown);
    map.bind(KEY_LEFT, word, C::MoveWordLeft);
    map.bind(KEY_RIGHT, word, C::MoveWordRight);
#if LL_DARWIN
    // Option with an arrow is a word on a Mac; elsewhere Alt with one goes
    // back and forward, as VS Code has it, which is whoever holds the
    // editor's to say.
    map.bind(KEY_LEFT, MASK_ALT, C::MoveWordLeft);
    map.bind(KEY_RIGHT, MASK_ALT, C::MoveWordRight);
#endif
    map.bind(KEY_HOME, MASK_NONE, C::MoveLineStart);
    map.bind(KEY_END, MASK_NONE, C::MoveLineEnd);
    map.bind(KEY_HOME, MASK_CONTROL, C::MoveDocStart);
    map.bind(KEY_END, MASK_CONTROL, C::MoveDocEnd);
    map.bind(KEY_PAGE_UP, MASK_NONE, C::MovePageUp);
    map.bind(KEY_PAGE_DOWN, MASK_NONE, C::MovePageDown);

    map.bind(KEY_LEFT, MASK_SHIFT, C::SelectLeft);
    map.bind(KEY_RIGHT, MASK_SHIFT, C::SelectRight);
    map.bind(KEY_UP, MASK_SHIFT, C::SelectUp);
    map.bind(KEY_DOWN, MASK_SHIFT, C::SelectDown);
    map.bind(KEY_LEFT, word | MASK_SHIFT, C::SelectWordLeft);
    map.bind(KEY_RIGHT, word | MASK_SHIFT, C::SelectWordRight);
#if LL_DARWIN
    map.bind(KEY_LEFT, MASK_ALT | MASK_SHIFT, C::SelectWordLeft);
    map.bind(KEY_RIGHT, MASK_ALT | MASK_SHIFT, C::SelectWordRight);
#endif
    map.bind(KEY_HOME, MASK_SHIFT, C::SelectLineStart);
    map.bind(KEY_END, MASK_SHIFT, C::SelectLineEnd);
    map.bind(KEY_HOME, MASK_CONTROL | MASK_SHIFT, C::SelectDocStart);
    map.bind(KEY_END, MASK_CONTROL | MASK_SHIFT, C::SelectDocEnd);
    map.bind(KEY_PAGE_UP, MASK_SHIFT, C::SelectPageUp);
    map.bind(KEY_PAGE_DOWN, MASK_SHIFT, C::SelectPageDown);
    map.bind('A', MASK_CONTROL, C::SelectAll);

    map.bind(KEY_BACKSPACE, MASK_NONE, C::DeleteLeft);
    // Shift is down half the time in code -- capitals, braces, brackets --
    // and a Backspace or a Return pressed under it is still one.
    map.bind(KEY_BACKSPACE, MASK_SHIFT, C::DeleteLeft);
    map.bind(KEY_DELETE, MASK_NONE, C::DeleteRight);
    map.bind(KEY_BACKSPACE, word, C::DeleteWordLeft);
    map.bind(KEY_DELETE, word, C::DeleteWordRight);
    map.bind(KEY_BACKSPACE, MASK_ALT, C::DeleteWordLeft);
    map.bind(KEY_DELETE, MASK_ALT, C::DeleteWordRight);
    map.bind(KEY_RETURN, MASK_NONE, C::NewLine);
    map.bind(KEY_RETURN, MASK_SHIFT, C::NewLine);
    map.bind(KEY_TAB, MASK_NONE, C::Indent);
    map.bind(KEY_TAB, MASK_SHIFT, C::Unindent);

    map.bind('Z', MASK_CONTROL, C::Undo);
    map.bind('Y', MASK_CONTROL, C::Redo);
    map.bind('Z', MASK_CONTROL | MASK_SHIFT, C::Redo);
    map.bind('X', MASK_CONTROL, C::Cut);
    map.bind('C', MASK_CONTROL, C::Copy);
    map.bind('V', MASK_CONTROL, C::Paste);
    map.bind('/', MASK_CONTROL, C::ToggleComment);
    // Where a slash is Shift-7 -- German, French, Nordic keyboards --
    // control and the key the slash is on.
    map.bind('7', MASK_CONTROL | MASK_SHIFT, C::ToggleComment);
    map.bind(KEY_UP, MASK_ALT, C::MoveLineUp);
    map.bind(KEY_DOWN, MASK_ALT, C::MoveLineDown);
    map.bind('D', MASK_CONTROL | MASK_SHIFT, C::DuplicateLine);
    map.bind('K', MASK_CONTROL | MASK_SHIFT, C::DeleteLine);
#if LL_DARWIN
    // Command-Option with a bracket, as the Mac's editors fold: Command-
    // Shift with one goes between tabs there.
    map.bind('[', MASK_CONTROL | MASK_ALT, C::Fold);
    map.bind(']', MASK_CONTROL | MASK_ALT, C::Unfold);
#else
    map.bind('[', MASK_CONTROL | MASK_SHIFT, C::Fold);
    map.bind(']', MASK_CONTROL | MASK_SHIFT, C::Unfold);
#endif
    map.bind(' ', MASK_CONTROL, C::Complete);
    map.bind(' ', MASK_CONTROL | MASK_SHIFT, C::SignatureHelp);
#if LL_DARWIN
    // The function keys need Fn on most Macs: first, and so shown in the
    // menus, the keys Xcode and the text system have for the same --
    // Control-Command-J to a definition, Control-Command-E to rename every
    // use, Control-Shift-Command-F for the uses, Command-G and Shift-
    // Command-G for the next match and the one before, Command-semicolon
    // for the next misspelling.
    constexpr MASK CONTROL_COMMAND = MASK_CONTROL | MASK_MAC_CONTROL;
    map.bind('J', CONTROL_COMMAND, C::GoToDefinition);
    map.bind('F', CONTROL_COMMAND | MASK_SHIFT, C::FindReferences);
    map.bind('E', CONTROL_COMMAND, C::Rename);
    map.bind('G', MASK_CONTROL, C::FindNext);
    map.bind('G', MASK_CONTROL | MASK_SHIFT, C::FindPrevious);
    map.bind(';', MASK_CONTROL, C::NextMisspelling);
    map.bind(';', MASK_CONTROL | MASK_SHIFT, C::PreviousMisspelling);
#endif
    map.bind(KEY_F12, MASK_NONE, C::GoToDefinition);
    map.bind(KEY_F12, MASK_SHIFT, C::FindReferences);
    map.bind(KEY_F2, MASK_NONE, C::Rename);
    map.bind('F', MASK_CONTROL, C::Find);
#if LL_DARWIN
    // Command-Option-F, as the Mac's editors have it: Command-H hides the
    // application there. Control and Alt together elsewhere is AltGr on
    // many a keyboard, which types.
    map.bind('F', MASK_CONTROL | MASK_ALT, C::Replace);
#else
    map.bind('H', MASK_CONTROL, C::Replace);
#endif
    map.bind(KEY_F3, MASK_NONE, C::FindNext);
    map.bind(KEY_F3, MASK_SHIFT, C::FindPrevious);
    // Control-. as Visual Studio and VS Code have it -- Command-. on a Mac,
    // where MASK_CONTROL is Command -- and Alt-Return as JetBrains does.
    map.bind('.', MASK_CONTROL, C::QuickFix);
    map.bind(KEY_RETURN, MASK_ALT, C::QuickFix);
    map.bind(KEY_F7, MASK_NONE, C::NextMisspelling);
    map.bind(KEY_F7, MASK_SHIFT, C::PreviousMisspelling);
    map.bind('J', MASK_CONTROL, C::JoinLines);
    map.bind(KEY_BACKSPACE, MASK_CONTROL | MASK_SHIFT, C::PreviousChange);
    map.bind(KEY_PAGE_DOWN, MASK_ALT, C::NextFunction);
    map.bind(KEY_PAGE_UP, MASK_ALT, C::PreviousFunction);
    // Control-Shift-\ as Visual Studio Code has it: Command on a Mac.
    map.bind('\\', MASK_CONTROL | MASK_SHIFT, C::GoToMatchingBracket);
#if LL_DARWIN
    // The Mac's own, where MASK_CONTROL is Command: Command with an arrow
    // goes to the ends of the line and of the text, Option with one by
    // words (above), and Command-Backspace back to the line's start.
    map.bind(KEY_LEFT, MASK_CONTROL, C::MoveLineStart);
    map.bind(KEY_RIGHT, MASK_CONTROL, C::MoveLineEnd);
    map.bind(KEY_UP, MASK_CONTROL, C::MoveDocStart);
    map.bind(KEY_DOWN, MASK_CONTROL, C::MoveDocEnd);
    map.bind(KEY_LEFT, MASK_CONTROL | MASK_SHIFT, C::SelectLineStart);
    map.bind(KEY_RIGHT, MASK_CONTROL | MASK_SHIFT, C::SelectLineEnd);
    map.bind(KEY_UP, MASK_CONTROL | MASK_SHIFT, C::SelectDocStart);
    map.bind(KEY_DOWN, MASK_CONTROL | MASK_SHIFT, C::SelectDocEnd);
    map.bind(KEY_BACKSPACE, MASK_CONTROL, C::DeleteToLineStart);
    map.bind(KEY_DELETE, MASK_CONTROL, C::DeleteToLineEnd);
    // And the Control keys every text field there takes from the text
    // system. Control-space completes, Command-space being the system's.
    map.bind('A', MASK_MAC_CONTROL, C::MoveLineStart);
    map.bind('E', MASK_MAC_CONTROL, C::MoveLineEnd);
    map.bind('B', MASK_MAC_CONTROL, C::MoveLeft);
    map.bind('F', MASK_MAC_CONTROL, C::MoveRight);
    map.bind('P', MASK_MAC_CONTROL, C::MoveUp);
    map.bind('N', MASK_MAC_CONTROL, C::MoveDown);
    map.bind('D', MASK_MAC_CONTROL, C::DeleteRight);
    map.bind('H', MASK_MAC_CONTROL, C::DeleteLeft);
    map.bind('K', MASK_MAC_CONTROL, C::DeleteToLineEnd);
    map.bind(' ', MASK_MAC_CONTROL, C::Complete);
    map.bind(' ', MASK_MAC_CONTROL | MASK_SHIFT, C::SignatureHelp);
#endif
    return map;
}
