/**
 * @file alvimregisters_test.cpp
 * @brief Vim's registers, with a clipboard of the test's own.
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

#include "alvimregisters.h"

#include "../test/lltut.h"

namespace tut
{
    struct alvimregisters_data
    {
        ALVimRegisters registers;
        // The test's clipboard, and how often it was written.
        std::string clipboard;
        int         copies = 0;

        alvimregisters_data()
        {
            registers.setClipboard(
                [this](const std::string& text) {
                    clipboard = text;
                    ++copies;
                },
                [this](std::string& text) {
                    text = clipboard;
                    return !clipboard.empty();
                });
        }
    };

    typedef test_group<alvimregisters_data> alvimregisters_group;
    typedef alvimregisters_group::object    alvimregisters_object;
    alvimregisters_group                    alvimregisters_instance("alvimregisters");

    template<> template<>
    void alvimregisters_object::test<1>()
    {
        set_test_name("a named register keeps what goes in it, its capital adds to it, and the clipboard is left alone");
        registers.store('a', "one", false, false, true, true);
        ensure_equals("kept", registers.fetch('a', true).text, std::string("one"));
        registers.store('A', "two", false, false, true, true);
        ensure_equals("added straight on", registers.fetch('a', true).text, std::string("onetwo"));
        registers.store('A', "line", true, false, true, true);
        const ALVimRegisters::Register lines = registers.fetch('A', true);
        ensure("a line added makes it lines", lines.text == "onetwo\nline" && lines.linewise);
        ensure_equals("\"\" is what went in last", registers.fetch('"', true).text, std::string("onetwo\nline"));
        ensure_equals("the clipboard untouched", copies, 0);
    }

    template<> template<>
    void alvimregisters_object::test<2>()
    {
        set_test_name("a yank goes in 0, a delete of lines in 1 with the rest moved along, a smaller one in -, and _ keeps nothing");
        registers.store(0, "yanked", false, false, true, false);
        ensure_equals("0", registers.fetch('0', false).text, std::string("yanked"));
        for (int i = 1; i <= 10; ++i)
        {
            registers.store(0, "line " + std::to_string(i), true, false, false, false);
        }
        ensure_equals("the newest in 1", registers.fetch('1', false).text, std::string("line 10"));
        ensure_equals("the oldest kept in 9", registers.fetch('9', false).text, std::string("line 2"));
        registers.store(0, "two\nlines", false, false, false, false);
        ensure_equals("two lines taken as characters are a line or more", registers.fetch('1', false).text, std::string("two\nlines"));
        registers.store(0, "word", false, false, false, false);
        ensure_equals("a smaller one in -", registers.fetch('-', false).text, std::string("word"));
        ensure_equals("and 1 as it was", registers.fetch('1', false).text, std::string("two\nlines"));
        registers.store('_', "gone", false, false, false, false);
        ensure_equals("_ keeps nothing, and the unnamed is as it was", registers.fetch(0, false).text, std::string("word"));
    }

    template<> template<>
    void alvimregisters_object::test<3>()
    {
        set_test_name("the unnamed register is the clipboard where the setting says so, and + and * are the clipboard whatever it says");
        registers.store(0, "shared", true, false, true, true);
        ensure_equals("written to the clipboard", clipboard, std::string("shared"));
        const ALVimRegisters::Register back = registers.fetch(0, true);
        ensure("read back from it, a line as it was taken", back.text == "shared" && back.linewise);
        clipboard = "from elsewhere";
        const ALVimRegisters::Register other = registers.fetch(0, true);
        ensure("somebody else's as characters", other.text == "from elsewhere" && !other.linewise);
        ensure_equals("\"\" is the register's all the same", registers.fetch('"', true).text, std::string("shared"));

        registers.store(0, "own", false, false, true, false);
        ensure_equals("the setting off: the clipboard not written", clipboard, std::string("from elsewhere"));
        ensure_equals("and none named is the register's", registers.fetch(0, false).text, std::string("own"));
        registers.store('+', "plus", false, false, true, false);
        ensure_equals("+ is the clipboard whatever the setting", clipboard, std::string("plus"));
        ensure_equals("and reads it", registers.fetch('*', false).text, std::string("plus"));
    }

    template<> template<>
    void alvimregisters_object::test<4>()
    {
        set_test_name("a macro's keys go in its register alone, as characters, its capital adding to them");
        registers.store(0, "before", true, false, true, true);
        const int copied = copies;
        registers.record('q', "dd");
        ensure_equals("kept", registers.fetch('q', true).text, std::string("dd"));
        registers.record('Q', "j");
        const ALVimRegisters::Register macro = registers.fetch('q', true);
        ensure("added, as characters", macro.text == "ddj" && !macro.linewise);
        ensure("neither the clipboard nor the unnamed register touched", copies == copied && registers.fetch('"', true).text == "before");
    }
}
