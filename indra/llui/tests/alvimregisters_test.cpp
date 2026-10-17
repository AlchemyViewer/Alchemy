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

#include <catch2/catch_test_macros.hpp>

namespace
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

    TEST_CASE_METHOD(alvimregisters_data, "alvimregisters: a named register keeps its text and its capital adds to it", "[alvimregisters]")
    {
        registers.store('a', "one", false, false, true, true);
        CHECK(registers.fetch('a', true).text == std::string("one"));
        registers.store('A', "two", false, false, true, true);
        UNSCOPED_INFO("added straight on");
        CHECK(registers.fetch('a', true).text == std::string("onetwo"));
        registers.store('A', "line", true, false, true, true);
        const ALVimRegisters::Register lines = registers.fetch('A', true);
        {
            INFO("a line added makes it lines");
            CHECK(lines.text == "onetwo\nline");
            CHECK(lines.linewise);
        }
        UNSCOPED_INFO("\"\" is what went in last");
        CHECK(registers.fetch('"', true).text == std::string("onetwo\nline"));
        UNSCOPED_INFO("the clipboard untouched");
        CHECK(copies == 0);
    }

    // A yank goes in 0, a delete of lines in 1 with the rest moved along, a
    // smaller one in -, and _ keeps nothing.
    TEST_CASE_METHOD(alvimregisters_data, "alvimregisters: yanks go in 0 and deletes of lines shift through 1 and small ones go in -", "[alvimregisters]")
    {
        registers.store(0, "yanked", false, false, true, false);
        CHECK(registers.fetch('0', false).text == std::string("yanked"));
        for (int i = 1; i <= 10; ++i)
        {
            registers.store(0, "line " + std::to_string(i), true, false, false, false);
        }
        UNSCOPED_INFO("the newest in 1");
        CHECK(registers.fetch('1', false).text == std::string("line 10"));
        UNSCOPED_INFO("the oldest kept in 9");
        CHECK(registers.fetch('9', false).text == std::string("line 2"));
        registers.store(0, "two\nlines", false, false, false, false);
        UNSCOPED_INFO("two lines taken as characters are a line or more");
        CHECK(registers.fetch('1', false).text == std::string("two\nlines"));
        registers.store(0, "word", false, false, false, false);
        UNSCOPED_INFO("a smaller one in -");
        CHECK(registers.fetch('-', false).text == std::string("word"));
        UNSCOPED_INFO("and 1 as it was");
        CHECK(registers.fetch('1', false).text == std::string("two\nlines"));
        registers.store('_', "gone", false, false, false, false);
        UNSCOPED_INFO("_ keeps nothing, and the unnamed is as it was");
        CHECK(registers.fetch(0, false).text == std::string("word"));
    }

    TEST_CASE_METHOD(alvimregisters_data, "alvimregisters: the unnamed register is the clipboard if set and + and * always are", "[alvimregisters]")
    {
        registers.store(0, "shared", true, false, true, true);
        UNSCOPED_INFO("written to the clipboard");
        CHECK(clipboard == std::string("shared"));
        const ALVimRegisters::Register back = registers.fetch(0, true);
        {
            INFO("read back from it, a line as it was taken");
            CHECK(back.text == "shared");
            CHECK(back.linewise);
        }
        clipboard = "from elsewhere";
        const ALVimRegisters::Register other = registers.fetch(0, true);
        {
            INFO("somebody else's as characters");
            CHECK(other.text == "from elsewhere");
            CHECK_FALSE(other.linewise);
        }
        UNSCOPED_INFO("\"\" is the register's all the same");
        CHECK(registers.fetch('"', true).text == std::string("shared"));

        registers.store(0, "own", false, false, true, false);
        UNSCOPED_INFO("the setting off: the clipboard not written");
        CHECK(clipboard == std::string("from elsewhere"));
        UNSCOPED_INFO("and none named is the register's");
        CHECK(registers.fetch(0, false).text == std::string("own"));
        registers.store('+', "plus", false, false, true, false);
        UNSCOPED_INFO("+ is the clipboard whatever the setting");
        CHECK(clipboard == std::string("plus"));
        UNSCOPED_INFO("and reads it");
        CHECK(registers.fetch('*', false).text == std::string("plus"));
    }

    // A macro's keys go in its register alone, as characters, its capital adding
    // to them.
    TEST_CASE_METHOD(alvimregisters_data, "alvimregisters: a macro's keys go in its register alone as characters", "[alvimregisters]")
    {
        registers.store(0, "before", true, false, true, true);
        const int copied = copies;
        registers.record('q', "dd");
        CHECK(registers.fetch('q', true).text == std::string("dd"));
        registers.record('Q', "j");
        const ALVimRegisters::Register macro = registers.fetch('q', true);
        {
            INFO("added, as characters");
            CHECK(macro.text == "ddj");
            CHECK_FALSE(macro.linewise);
        }
        {
            INFO("neither the clipboard nor the unnamed register touched");
            CHECK(copies == copied);
            CHECK(registers.fetch('"', true).text == "before");
        }
    }

    // An empty line is kept as a line, and read back so through the clipboard it
    // leaves empty; a register never set is no line.
    TEST_CASE_METHOD(alvimregisters_data, "alvimregisters: an empty line is kept as a line even through an empty clipboard", "[alvimregisters]")
    {
        const ALVimRegisters::Register unset = registers.fetch('a', false);
        {
            INFO("never set: no text, and no line");
            CHECK(unset.text.empty());
            CHECK_FALSE(unset.linewise);
        }
        registers.store(0, "", true, false, true, true);
        UNSCOPED_INFO("kept as a line");
        CHECK(registers.fetch('0', true).linewise);
        UNSCOPED_INFO("the clipboard written, and empty");
        CHECK(clipboard == std::string());
        const ALVimRegisters::Register back = registers.fetch(0, true);
        {
            INFO("read back through the clipboard as the line it was");
            CHECK(back.text.empty());
            CHECK(back.linewise);
        }
    }

    // A delete of a line or more into a named register goes in 1 as well, the
    // rest moved along, and so does one over a search; a smaller one and a yank
    // leave 1 alone.
    TEST_CASE_METHOD(alvimregisters_data, "alvimregisters: a delete of lines into a named register goes in 1 as well", "[alvimregisters]")
    {
        registers.store(0, "older", true, false, false, false);
        registers.store('a', "one", true, false, false, false);
        UNSCOPED_INFO("the line in a");
        CHECK(registers.fetch('a', false).text == std::string("one"));
        UNSCOPED_INFO("and in 1");
        CHECK(registers.fetch('1', false).text == std::string("one"));
        UNSCOPED_INFO("the one before moved along to 2");
        CHECK(registers.fetch('2', false).text == std::string("older"));
        registers.store('A', "two", true, false, false, false);
        UNSCOPED_INFO("A adds to a");
        CHECK(registers.fetch('a', false).text == std::string("one\ntwo"));
        UNSCOPED_INFO("1 the line taken, alone");
        CHECK(registers.fetch('1', false).text == std::string("two"));
        UNSCOPED_INFO("\"\" what a holds");
        CHECK(registers.fetch('"', false).text == std::string("one\ntwo"));
        registers.store('b', "two\nlines", false, false, false, false);
        UNSCOPED_INFO("characters over two lines are a line or more");
        CHECK(registers.fetch('1', false).text == std::string("two\nlines"));
        registers.store('c', "word ", false, false, false, false, true);
        UNSCOPED_INFO("a smaller one over a search in 1 too");
        CHECK(registers.fetch('1', false).text == std::string("word "));
        UNSCOPED_INFO("but not in -");
        CHECK(registers.fetch('-', false).text.empty());
        registers.store('d', "small", false, false, false, false);
        UNSCOPED_INFO("a smaller one: 1 as it was");
        CHECK(registers.fetch('1', false).text == std::string("word "));
        registers.store('e', "yanked\nlines", true, false, true, false);
        UNSCOPED_INFO("a yank: 1 as it was");
        CHECK(registers.fetch('1', false).text == std::string("word "));
        UNSCOPED_INFO("and 0 never set");
        CHECK(registers.fetch('0', false).text.empty());
    }

    // A digit or - named keeps it as a letter does, and neither 0, the small
    // delete register nor the clipboard takes it too.
    TEST_CASE_METHOD(alvimregisters_data, "alvimregisters: a digit or - named keeps text as a letter does and nothing else takes it", "[alvimregisters]")
    {
        registers.store(0, "Z0", false, false, true, true);
        registers.store(0, "Zm", false, false, false, true);
        const int copied = copies;
        registers.store('-', "one two", true, false, true, true);
        const ALVimRegisters::Register small = registers.fetch('-', true);
        {
            INFO("\"-yy: the line in -");
            CHECK(small.text == "one two");
            CHECK(small.linewise);
        }
        UNSCOPED_INFO("0 as it was");
        CHECK(registers.fetch('0', true).text == std::string("Z0"));
        UNSCOPED_INFO("\"\" says it");
        CHECK(registers.fetch('"', true).text == std::string("one two"));
        registers.store('5', "one two", true, false, true, true);
        UNSCOPED_INFO("\"5yy: the line in 5");
        CHECK(registers.fetch('5', true).text == std::string("one two"));
        UNSCOPED_INFO("0 still as it was");
        CHECK(registers.fetch('0', true).text == std::string("Z0"));
        registers.store('5', "one ", false, false, false, true);
        UNSCOPED_INFO("\"5dw: the word in 5");
        CHECK(registers.fetch('5', true).text == std::string("one "));
        UNSCOPED_INFO("not in -");
        CHECK(registers.fetch('-', true).text == std::string("one two"));
        UNSCOPED_INFO("the clipboard untouched");
        CHECK(copies == copied);
        registers.store(0, "Z1", true, false, false, false);
        registers.store('1', "line", true, false, false, false);
        UNSCOPED_INFO("\"1dd: the line in 1");
        CHECK(registers.fetch('1', false).text == std::string("line"));
        UNSCOPED_INFO("and moved along to 2 with it");
        CHECK(registers.fetch('2', false).text == std::string("line"));
        registers.store('-', "lines", true, false, false, false);
        UNSCOPED_INFO("\"-dd: in -");
        CHECK(registers.fetch('-', false).text == std::string("lines"));
        UNSCOPED_INFO("and in 1");
        CHECK(registers.fetch('1', false).text == std::string("lines"));
    }
}
