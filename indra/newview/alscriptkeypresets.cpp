/**
 * @file alscriptkeypresets.cpp
 * @brief Script Studio's keys as other editors have them, to put over its own.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptkeypresets.h"

#include "llkeyboard.h"

#include <algorithm>

namespace
{
    typedef ALScriptKeyPresets::Binding B;

    constexpr MASK C = MASK_CONTROL; // Control; Command on a Mac
    constexpr MASK S = MASK_SHIFT;
    constexpr MASK A = MASK_ALT;     // Alt; Option on a Mac
#if LL_DARWIN
    constexpr MASK MC = MASK_MAC_CONTROL; // the Mac's own Control key
#endif

    ALKeyChord key(KEY k, MASK m = MASK_NONE)
    {
        return ALKeyChord{ k, m };
    }

#if !LL_DARWIN
    // Two keys in turn: `lead`, then `k`.
    ALKeyChord two(KEY lead, MASK lead_mask, KEY k, MASK m = MASK_NONE)
    {
        return ALKeyChord{ k, m, lead, lead_mask };
    }
#endif

    // Visual Studio Code's own standard keys, which the studio's mostly
    // are: where they are not.
    std::vector<B> vscode()
    {
        return {
            // Copy Line Down.
            { "duplicate_line", { key(KEY_DOWN, S | A) } },
            { "word_wrap", { key('Z', A) } },
            { "explorer", { key('E', C | S) } },
            { "problems", { key('M', C | S) } },
            { "output", { key('U', C | S) } },
#if LL_DARWIN
            { "save_all", { key('S', C | A) } },
#endif
        };
    }

#if !LL_DARWIN
    // Visual Studio 2022's General profile. Its two-key commands that are
    // an editor's -- Control-K, Control-C to comment, Control-M, Control-M
    // to fold -- keep the studio's one key: an editor's command answers
    // to one key at a time.
    std::vector<B> visualStudio()
    {
        return {
            { "save_all", { key('S', C | S) } },
            { "close", { key(KEY_F4, C) } },
            // Go To All, and Go To Member.
            { "quick_open", { key('T', C), key(',', C) } },
            { "go_to_symbol", { key('\\', A) } },
            // Feature search.
            { "command_palette", { key('Q', C) } },
            { "format", { two('K', C, 'D', C) } },
            { "format_selection", { two('K', C, 'F', C) } },
            { "word_wrap", { two('E', C, 'W', C) } },
            { "insert_snippet", { two('K', C, 'X', C) } },
            { "duplicate_line", { key('D', C) } },
            // Insert Next Matching Caret, and Carets at All Matching.
            { "select_next_occurrence", { key('.', S | A) } },
            { "change_all_occurrences", { key(';', S | A) } },
            { "delete_line", { key('L', C | S) } },
            // Complete Word, and List Members.
            { "complete", { key(' ', C), key('J', C) } },
            { "go_to_bracket", { key(']', C) } },
            { "insert_line_above", { key(KEY_RETURN, C) } },
            { "insert_line_below", { key(KEY_RETURN, C | S) } },
            { "expand_selection", { key('=', S | A), key(KEY_EQUALS, S | A) } },
            { "shrink_selection", { key('-', S | A) } },
            { "next_problem", { key(KEY_F12, C | S), key(KEY_F8) } },
            { "previous_problem", { key(KEY_F8, S) } },
            { "back", { key('-', C) } },
            { "forward", { key('-', C | S) } },
            { "zoom_in", { key('.', C | S) } },
            { "zoom_out", { key(',', C | S) } },
            // Solution Explorer, the Error List, Output.
            { "explorer", { key('L', C | A) } },
            { "problems", { two('\\', C, 'E') } },
            { "output", { key('O', C | A) } },
            { "next_tab", { key(KEY_PAGE_DOWN, C | A), key(KEY_TAB, C) } },
            { "previous_tab", { key(KEY_PAGE_UP, C | A), key(KEY_TAB, C | S) } },
        };
    }
#endif

    // The JetBrains IDEs' own keymap -- IntelliJ IDEA's, Rider's, CLion's
    // -- on Windows and Linux, and their macOS keymap on a Mac.
    std::vector<B> jetbrains()
    {
#if LL_DARWIN
        return {
            { "delete_line", { key(KEY_BACKSPACE, C) } },
            { "duplicate_line", { key('D', C) } },
            { "select_next_occurrence", { key('G', MC) } },
            { "change_all_occurrences", { key('G', MC | C) } },
            { "go_to_line", { key('L', C) } },
            { "join_lines", { key('J', MC | S) } },
            { "move_line_up", { key(KEY_UP, A | S) } },
            { "move_line_down", { key(KEY_DOWN, A | S) } },
            // Extend and Shrink Selection.
            { "expand_selection", { key(KEY_UP, A) } },
            { "shrink_selection", { key(KEY_DOWN, A) } },
            { "signature_help", { key('P', C) } },
            // Go to File, File Structure, Find Action.
            { "quick_open", { key('O', C | S) } },
            { "go_to_symbol", { key(KEY_F12, C) } },
            { "command_palette", { key('A', C | S) } },
            { "go_to_definition", { key('B', C) } },
            { "find_references", { key(KEY_F7, A) } },
            { "rename", { key(KEY_F6, S) } },
            { "quick_fix", { key(KEY_RETURN, A) } },
            { "format", { key('L', C | A) } },
            { "replace", { key('R', C) } },
            { "back", { key('[', C) } },
            { "forward", { key(']', C) } },
            { "fold", { key('-', C), key(KEY_SUBTRACT, C) } },
            { "unfold", { key('=', C), key(KEY_EQUALS, C), key(KEY_ADD, C) } },
            { "fold_all", { key('-', C | S), key(KEY_SUBTRACT, C | S) } },
            { "unfold_all", { key('=', C | S), key(KEY_EQUALS, C | S), key(KEY_ADD, C | S) } },
            { "zoom_in", { key('.', A | S) } },
            { "zoom_out", { key(',', A | S) } },
            // Next and Previous Highlighted Error.
            { "next_problem", { key(KEY_F2) } },
            { "previous_problem", { key(KEY_F2, S) } },
            // Start New Line, and Start New Line Before Current.
            { "insert_line_below", { key(KEY_RETURN, S) } },
            { "insert_line_above", { key(KEY_RETURN, C | A) } },
            // Recent Files.
            { "all_tabs", { key('E', C) } },
            // The tool windows: Project, Find, Run, Problems, Structure.
            { "explorer", { key('1', C) } },
            { "search", { key('3', C) } },
            { "output", { key('4', C) } },
            { "problems", { key('6', C) } },
            { "inspector", { key('7', C) } },
        };
#else
        return {
            // Control-Y deletes the line there.
            { "redo", { key('Z', C | S) } },
            { "delete_line", { key('Y', C) } },
            { "duplicate_line", { key('D', C) } },
            { "select_next_occurrence", { key('J', A) } },
            { "change_all_occurrences", { key('J', C | A | S) } },
            { "join_lines", { key('J', C | S) } },
            { "move_line_up", { key(KEY_UP, A | S) } },
            { "move_line_down", { key(KEY_DOWN, A | S) } },
            // Extend and Shrink Selection: Control-W, so Control-F4
            // closes.
            { "expand_selection", { key('W', C) } },
            { "shrink_selection", { key('W', C | S) } },
            { "close", { key(KEY_F4, C) } },
            { "signature_help", { key('P', C) } },
            // Go to File and Go to Class; New... on Alt-Insert.
            { "quick_open", { key('N', C | S), key('N', C) } },
            { "new_script", { key(KEY_INSERT, A) } },
            // File Structure, Find Action.
            { "go_to_symbol", { key(KEY_F12, C) } },
            { "command_palette", { key('A', C | S) } },
            { "go_to_definition", { key('B', C) } },
            { "find_references", { key(KEY_F7, A) } },
            { "rename", { key(KEY_F6, S) } },
            { "quick_fix", { key(KEY_RETURN, A) } },
            { "format", { key('L', C | A) } },
            { "replace", { key('R', C) } },
            { "back", { key(KEY_LEFT, C | A) } },
            { "forward", { key(KEY_RIGHT, C | A) } },
            // Next and Previous Method.
            { "next_function", { key(KEY_DOWN, A) } },
            { "previous_function", { key(KEY_UP, A) } },
            { "go_to_bracket", { key('M', C | S) } },
            { "fold", { key('-', C), key(KEY_SUBTRACT, C) } },
            { "unfold", { key('=', C), key(KEY_EQUALS, C), key(KEY_ADD, C) } },
            { "fold_all", { key('-', C | S), key(KEY_SUBTRACT, C | S) } },
            { "unfold_all", { key('=', C | S), key(KEY_EQUALS, C | S), key(KEY_ADD, C | S) } },
            { "zoom_in", { key('.', A | S) } },
            { "zoom_out", { key(',', A | S) } },
            // Next and Previous Highlighted Error.
            { "next_problem", { key(KEY_F2) } },
            { "previous_problem", { key(KEY_F2, S) } },
            // Start New Line, and Start New Line Before Current.
            { "insert_line_below", { key(KEY_RETURN, S) } },
            { "insert_line_above", { key(KEY_RETURN, C | A) } },
            // Recent Files; the editor's tabs on Alt with an arrow.
            { "all_tabs", { key('E', C) } },
            { "next_tab", { key(KEY_RIGHT, A) } },
            { "previous_tab", { key(KEY_LEFT, A) } },
            // The tool windows: Project, Find, Run, Problems, Structure.
            { "explorer", { key('1', A) } },
            { "search", { key('3', A) } },
            { "output", { key('4', A) } },
            { "problems", { key('6', A) } },
            { "inspector", { key('7', A) } },
            // Quick Documentation.
            { "reference", { key('Q', C) } },
        };
#endif
    }

#if LL_DARWIN
    // Xcode 16's own.
    std::vector<B> xcode()
    {
        return {
            // Open Quickly, Show Document Items, Jump to Line.
            { "quick_open", { key('O', C | S) } },
            { "go_to_symbol", { key('6', MC) } },
            { "go_to_line", { key('L', C) } },
            // Edit All in Scope, and Select Next Occurrence.
            { "change_all_occurrences", { key('E', MC | C) } },
            { "select_next_occurrence", { key('E', C | A) } },
            { "duplicate_line", { key('D', C) } },
            { "move_line_up", { key('[', C | A) } },
            { "move_line_down", { key(']', C | A) } },
            { "fold", { key(KEY_LEFT, C | A) } },
            { "unfold", { key(KEY_RIGHT, C | A) } },
            { "fold_all", { key(KEY_LEFT, C | A | S) } },
            { "unfold_all", { key(KEY_RIGHT, C | A | S) } },
            // Re-Indent.
            { "format_selection", { key('I', MC) } },
            { "back", { key(KEY_LEFT, MC | C) } },
            { "forward", { key(KEY_RIGHT, MC | C) } },
            // The navigator, the Find and Issue navigators, the debug
            // area, the inspectors.
            { "explorer", { key('0', C) } },
            { "search", { key('4', C) } },
            { "problems", { key('5', C) } },
            { "output", { key('Y', C | S) } },
            { "inspector", { key('0', C | A) } },
        };
    }
#endif
}

namespace ALScriptKeyPresets
{
    const std::vector<Preset>& all()
    {
        static const std::vector<Preset> presets{
            { STANDARD, {} },
            { "vscode", vscode() },
#if LL_DARWIN
            { "xcode", xcode() },
#else
            { "visual_studio", visualStudio() },
#endif
            { "jetbrains", jetbrains() },
        };
        return presets;
    }

    const Preset* find(std::string_view id)
    {
        const std::vector<Preset>& presets = all();
        const auto found = std::find_if(presets.begin(), presets.end(), [&](const Preset& one) { return one.id == id; });
        return found == presets.end() ? nullptr : &*found;
    }
}
