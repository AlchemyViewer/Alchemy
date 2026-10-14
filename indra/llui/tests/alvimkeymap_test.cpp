/**
 * @file alvimkeymap_test.cpp
 * @brief The vim mode driven through key sequences, its text, caret and selection compared.
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

#include "alcodeeditor.h"
#include "alvimkeymap.h"
#include "../llclipboard.h"
#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include "llsd.h"
#include "alheadlessui_fixture.h"
#include "altextviewprobe.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    struct alvimkeymap_data
    {
        ll_test::HeadlessUI& ui     = ll_test::HeadlessUI::get();
        ALCodeEditor*        editor = nullptr;
        ALVimKeymap*         vim    = nullptr;

        ~alvimkeymap_data()
        {
            gFocusMgr.setKeyboardFocus(nullptr);
            if (editor)
            {
                editor->die();
            }
            // Reaped now, as the next frame would: test 73 empties the
            // graveyard itself, and then finds only its own views there.
            LLMortician::updateClass();
            // What a test yanked or deleted is not the next one's to put.
            if (LLClipboard::instanceExists())
            {
                LLClipboard::instance().reset();
            }
        }

        ALCodeEditor& make(const char* text, const char* syntax = "lsl", bool auto_complete = false)
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            if (editor)
            {
                editor->die();
                editor = nullptr;
            }
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name         = "editor";
            p.rect         = LLRect(0, 200, 400, 0);
            p.default_text  = text;
            p.syntax        = syntax;
            p.auto_complete = auto_complete;
            editor          = LLUICtrlFactory::create<ALCodeEditor>(p);
            editor->setFont(LLFontGL::getFontMonospace());
            editor->setFocus(true);
            auto keymap = std::make_unique<ALVimKeymap>();
            vim         = keymap.get();
            editor->setModalKeymap(std::move(keymap));
            return *editor;
        }

        // Keys as vim would see them typed: a character each, with
        // <Esc>, <CR>, <BS>, <C-x> and <Tab> spelt out.
        void keys(const char* sequence)
        {
            for (const char* c = sequence; *c;)
            {
                const char* end  = *c == '<' ? strchr(c + 1, '>') : nullptr;
                bool        named = end != nullptr && end > c + 1;
                for (const char* k = c + 1; named && k < end; ++k)
                {
                    named = isalnum(static_cast<unsigned char>(*k)) || *k == '-';
                }
                if (named)
                {
                    const std::string name(c + 1, end);
                    if (name == "Esc")
                    {
                        editor->handleKeyHere(KEY_ESCAPE, MASK_NONE);
                    }
                    else if (name == "CR")
                    {
                        if (!editor->handleKeyHere(KEY_RETURN, MASK_NONE))
                        {
                            editor->handleKeyHere(KEY_RETURN, MASK_NONE);
                        }
                    }
                    else if (name == "BS")
                    {
                        editor->handleKeyHere(KEY_BACKSPACE, MASK_NONE);
                    }
                    else if (name == "Tab")
                    {
                        editor->handleKeyHere(KEY_TAB, MASK_NONE);
                    }
                    else if (name == "Up" || name == "Down")
                    {
                        editor->handleKeyHere(name == "Up" ? KEY_UP : KEY_DOWN, MASK_NONE);
                    }
                    else if (name == "Left" || name == "Right" || name == "Home" || name == "End" || name == "Delete")
                    {
                        editor->handleKeyHere(name == "Left" ? KEY_LEFT : name == "Right" ? KEY_RIGHT : name == "Home" ? KEY_HOME : name == "End" ? KEY_END : KEY_DELETE, MASK_NONE);
                    }
                    else if (name.size() == 3 && name[0] == 'C' && name[1] == '-')
                    {
                        editor->handleKeyHere(static_cast<KEY>(toupper(name[2])), ALVimKeymap::CONTROL);
                    }
                    else
                    {
                        FAIL("unknown key " + name);
                    }
                    c = end + 1;
                    continue;
                }
                // As the viewer does: the key first, then the character.
                const KEY key = static_cast<KEY>(toupper(static_cast<unsigned char>(*c)));
                if (!editor->handleKeyHere(key, MASK_NONE))
                {
                    editor->handleUnicodeCharHere(static_cast<llwchar>(static_cast<unsigned char>(*c)));
                }
                ++c;
            }
        }

        std::string caretText() const { return llformat("%d:%d", editor->caret().line, editor->caret().column); }
        // A : line run as it is written, which keys() would read <Esc> in
        // as the key.
        void ex(const char* line) { vim->takeLine(*editor, ':', line, true); }

        // The local point of a column on a line, in the middle of its row.
        void pointOf(S32 line, S32 column, S32& x, S32& y)
        {
            const LLRect text = editor->textRect();
            S32          row;
            const F32    xrel = editor->layout().xOf(line, column, &row);
            x                 = text.mLeft + static_cast<S32>(xrel) + 1;
            y                 = text.mTop - (editor->layout().lineTop(line) + row * editor->layout().rowHeight()) - editor->layout().rowHeight() / 2;
        }
        // As the viewer does: the mouse held still between the press and
        // the release is hovered over all the same.
        void click(S32 line, S32 column, S32 slide = 0)
        {
            S32 x, y;
            pointOf(line, column, x, y);
            editor->handleMouseDown(x, y, MASK_NONE);
            editor->handleHover(x, y, MASK_NONE);
            editor->handleHover(x + slide, y, MASK_NONE);
            editor->handleMouseUp(x + slide, y, MASK_NONE);
        }
        // From a character to a character, each at the left of its glyph,
        // or at its right with `right_half`.
        void drag(S32 line, S32 column, S32 to_line, S32 to_column, bool right_half = false)
        {
            S32 x, y, x2, y2;
            pointOf(line, column, x, y);
            pointOf(to_line, to_column, x2, y2);
            if (right_half)
            {
                S32 next_x, next_y;
                pointOf(line, column + 1, next_x, next_y);
                x = next_x - 3;
                pointOf(to_line, to_column + 1, next_x, next_y);
                x2 = next_x - 3;
            }
            editor->handleMouseDown(x, y, MASK_NONE);
            editor->handleHover(x2, y2, MASK_NONE);
            editor->handleMouseUp(x2, y2, MASK_NONE);
        }
        // The text on one line, for a message that shows it whole.
        static std::string flat(std::string text)
        {
            for (char& c : text)
            {
                if (c == '\n')
                {
                    c = '|';
                }
            }
            return text;
        }
    };

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: motions: h j k l w b e 0 ^ $ gg G f t ; % with counts", "[alvimkeymap]")
    {
        make("integer count = 0;\n  float half(integer n) { return n / 2.0; }\nx\n");
        UNSCOPED_INFO("normal to begin with");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        keys("w");
        UNSCOPED_INFO("w to the next word");
        CHECK(caretText() == std::string("0:8"));
        keys("2w");
        UNSCOPED_INFO("2w two on");
        CHECK(caretText() == std::string("0:16"));
        keys("b");
        UNSCOPED_INFO("b back");
        CHECK(caretText() == std::string("0:14"));
        keys("e");
        UNSCOPED_INFO("e to the word's end");
        CHECK(caretText() == std::string("0:16"));
        keys("$");
        UNSCOPED_INFO("$ on the last character");
        CHECK(caretText() == std::string("0:17"));
        keys("0");
        UNSCOPED_INFO("0 to the start");
        CHECK(caretText() == std::string("0:0"));
        keys("j^");
        UNSCOPED_INFO("j then ^ to the first non-blank");
        CHECK(caretText() == std::string("1:2"));
        keys("fn");
        UNSCOPED_INFO("f finds the n in integer");
        CHECK(caretText() == std::string("1:14"));
        keys(";");
        UNSCOPED_INFO("; the next");
        CHECK(caretText() == std::string("1:21"));
        keys("t{");
        UNSCOPED_INFO("t stops before the brace");
        CHECK(caretText() == std::string("1:23"));
        keys("l%");
        UNSCOPED_INFO("% to the matching brace");
        CHECK(caretText() == std::string("1:42"));
        keys("gg");
        UNSCOPED_INFO("gg to the top");
        CHECK(caretText() == std::string("0:0"));
        keys("G");
        UNSCOPED_INFO("G to the last line");
        CHECK(caretText() == std::string("3:0"));
        keys("2G");
        UNSCOPED_INFO("2G to line 2");
        CHECK(caretText() == std::string("1:2"));
        keys("k3l");
        UNSCOPED_INFO("k keeps the column, then 3l");
        CHECK(caretText() == std::string("0:5"));
        keys("20l");
        UNSCOPED_INFO("l stops at the line's last character");
        CHECK(caretText() == std::string("0:17"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: operators with motions and counts: dw d2w dd cw yy p x J . and u", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two three\nfour five\nsix\n");
        keys("dw");
        UNSCOPED_INFO("dw takes the word and its space");
        CHECK(flat(e.text()) == std::string("two three|four five|six|"));
        keys("d2w");
        UNSCOPED_INFO("d2w takes two, to the line's end");
        CHECK(flat(e.text()) == std::string("|four five|six|"));
        keys("jdd");
        UNSCOPED_INFO("dd takes the line");
        CHECK(flat(e.text()) == std::string("|six|"));
        UNSCOPED_INFO("caret on the line that took its place");
        CHECK(caretText() == std::string("1:0"));
        keys("u");
        UNSCOPED_INFO("u brings it back");
        CHECK(flat(e.text()) == std::string("|four five|six|"));
        keys("cwseven<Esc>");
        UNSCOPED_INFO("cw changes the word, not the space");
        CHECK(flat(e.text()) == std::string("|seven five|six|"));
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        UNSCOPED_INFO("caret on the last character typed");
        CHECK(caretText() == std::string("1:4"));
        keys("yyjp");
        UNSCOPED_INFO("yy then p puts the line below");
        CHECK(flat(e.text()) == std::string("|seven five|six|seven five|"));
        keys("x");
        UNSCOPED_INFO("x takes a character");
        CHECK(flat(e.text()) == std::string("|seven five|six|even five|"));
        keys("..");
        UNSCOPED_INFO(". does it again, twice");
        CHECK(flat(e.text()) == std::string("|seven five|six|en five|"));
        keys("ggjJ");
        UNSCOPED_INFO("J joins with a space");
        CHECK(flat(e.text()) == std::string("|seven five six|en five|"));
        keys("3u");
        UNSCOPED_INFO("3u undoes three: the join and two repeats");
        CHECK(flat(e.text()) == std::string("|seven five|six|even five|"));
        keys("<C-r>");
        UNSCOPED_INFO("control-R redoes one");
        CHECK(flat(e.text()) == std::string("|seven five|six|ven five|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: insert mode: i a I A o O with counts and . repeating an insert", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\n");
        keys("ix<Esc>");
        UNSCOPED_INFO("i inserts before");
        CHECK(flat(e.text()) == std::string("xabc|"));
        UNSCOPED_INFO("escape steps back onto what was typed");
        CHECK(caretText() == std::string("0:0"));
        keys("ay<Esc>");
        UNSCOPED_INFO("a appends after");
        CHECK(flat(e.text()) == std::string("xyabc|"));
        keys("A!<Esc>");
        UNSCOPED_INFO("A at the line's end");
        CHECK(flat(e.text()) == std::string("xyabc!|"));
        keys("I-<Esc>");
        UNSCOPED_INFO("I at the line's start");
        CHECK(flat(e.text()) == std::string("-xyabc!|"));
        keys("onew<Esc>");
        UNSCOPED_INFO("o opens a line below");
        CHECK(flat(e.text()) == std::string("-xyabc!|new|"));
        keys("Oabove<Esc>");
        UNSCOPED_INFO("O opens a line above");
        CHECK(flat(e.text()) == std::string("-xyabc!|above|new|"));
        keys("03ihi<Esc>");
        UNSCOPED_INFO("3i types it three times");
        CHECK(flat(e.text()) == std::string("-xyabc!|hihihiabove|new|"));
        keys("j0.");
        UNSCOPED_INFO(". repeats the insert on the next line");
        CHECK(flat(e.text()) == std::string("-xyabc!|hihihiabove|hihihinew|"));
        keys("u");
        UNSCOPED_INFO("an insert is one step to undo");
        CHECK(flat(e.text()) == "-xyabc!|hihihiabove|new|");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: text objects: diw daw da( yi( dap and ci inside quotes", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("say(\"hello there\", (a + b) * 2)\n\nnext para\nline two\n\nlast\n");
        keys("fhdiw");
        UNSCOPED_INFO("diw takes the word");
        CHECK(flat(e.text()) == std::string("say(\" there\", (a + b) * 2)||next para|line two||last|"));
        keys("u");
        keys("daw");
        UNSCOPED_INFO("daw takes the word and the space after");
        CHECK(flat(e.text()) == std::string("say(\"there\", (a + b) * 2)||next para|line two||last|"));
        keys("u0fhci\"bye<Esc>");
        UNSCOPED_INFO("ci\" changes what the quotes hold");
        CHECK(flat(e.text()) == std::string("say(\"bye\", (a + b) * 2)||next para|line two||last|"));
        keys("fada(");
        UNSCOPED_INFO("da( takes the brackets and what they hold");
        CHECK(flat(e.text()) == std::string("say(\"bye\",  * 2)||next para|line two||last|"));
        keys("0f(yi(");
        UNSCOPED_INFO("yi( yanks the inside");
        CHECK(vim->registerText('"') == std::string("\"bye\",  * 2"));
        keys("2jdap");
        UNSCOPED_INFO("dap takes the paragraph and the blank after it");
        CHECK(flat(e.text()) == std::string("say(\"bye\",  * 2)||last|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: visual modes: v with an operator; V for lines; control-V blocks with I", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two\nthree four\nfive six\n");
        keys("vey");
        UNSCOPED_INFO("v e y yanks the word");
        CHECK(vim->registerText('"') == std::string("one"));
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        keys("vjd");
        UNSCOPED_INFO("v j d takes across the lines, the character under the caret too");
        CHECK(flat(e.text()) == std::string("hree four|five six|"));
        keys("Vd");
        UNSCOPED_INFO("V d takes the line");
        CHECK(flat(e.text()) == std::string("five six|"));
        keys("u");
        keys("<C-v>jIx <Esc>");
        UNSCOPED_INFO("a block insert goes on every line");
        CHECK(flat(e.text()) == std::string("x hree four|x five six|"));
        keys("gg<C-v>jld");
        UNSCOPED_INFO("a block delete takes the columns");
        CHECK(flat(e.text()) == std::string("hree four|five six|"));
        keys("v$y");
        UNSCOPED_INFO("v $ y yanks to the line's end, and its break");
        CHECK(vim->registerText('"') == std::string("hree four\n"));
        keys("vjo");
        UNSCOPED_INFO("o swaps the ends");
        CHECK(caretText() == std::string("0:0"));
        keys("<Esc>");
        {
            INFO("escape leaves visual");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK_FALSE(e.hasSelection());
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: registers; marks; the search with / n N and *; :s and :d over ranges", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("alpha beta\ngamma alpha\ndelta\n");
        keys("\"ayyj\"ap");
        UNSCOPED_INFO("a named register keeps its line");
        CHECK(flat(e.text()) == std::string("alpha beta|gamma alpha|alpha beta|delta|"));
        UNSCOPED_INFO("and \"\" names it");
        CHECK(vim->registerText('"') == std::string("alpha beta"));
        keys("ggmaG`a");
        UNSCOPED_INFO("a mark goes back to the place");
        CHECK(caretText() == std::string("0:0"));
        keys("jl'a");
        UNSCOPED_INFO("' goes to the line's first non-blank");
        CHECK(caretText() == std::string("0:0"));
        keys("/alpha<CR>");
        UNSCOPED_INFO("/ finds the next");
        CHECK(caretText() == std::string("1:6"));
        keys("n");
        UNSCOPED_INFO("n the one after");
        CHECK(caretText() == std::string("2:0"));
        keys("N");
        UNSCOPED_INFO("N back again");
        CHECK(caretText() == std::string("1:6"));
        keys("*");
        UNSCOPED_INFO("* the word under the caret, whole");
        CHECK(caretText() == std::string("2:0"));
        keys("/nothing here<CR>");
        {
            INFO("a miss is said");
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("Pattern not found") != std::string::npos);
        }
        keys(":%s/alpha/omega/g<CR>");
        UNSCOPED_INFO(":s over every line");
        CHECK(flat(e.text()) == std::string("omega beta|gamma omega|omega beta|delta|"));
        keys(":2<CR>");
        UNSCOPED_INFO(": with a number goes there");
        CHECK(caretText() == std::string("1:0"));
        keys(":3,4d<CR>");
        UNSCOPED_INFO(":d over a range");
        CHECK(flat(e.text()) == std::string("omega beta|gamma omega|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the : line reaches the hooks and refuses what nothing knows; >> and <<", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\n    b\nc\n");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return name == "w" || name == "q!" || name == "format";
        };
        keys(":w<CR>");
        keys(":q!<CR>");
        keys(":format now<CR>");
        REQUIRE(heard.size() == size_t(3));
        CHECK(heard[0] == std::string("w|"));
        UNSCOPED_INFO("q! with its bang");
        CHECK(heard[1] == std::string("q!|"));
        UNSCOPED_INFO("with its arguments");
        CHECK(heard[2] == std::string("format|now"));
        keys(":frobnicate<CR>");
        {
            INFO("an unknown command is said");
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("Not an editor command") != std::string::npos);
        }
        e.setSoftTabs(true);
        keys("gg>>");
        UNSCOPED_INFO(">> indents the line");
        CHECK(flat(e.text()) == std::string("    a|    b|c|"));
        keys("j<<");
        UNSCOPED_INFO("<< outdents");
        CHECK(flat(e.text()) == std::string("    a|b|c|"));
        keys("<Esc>");
        UNSCOPED_INFO("escape in normal mode is taken, not the floater's");
        CHECK(e.handleKeyHere(KEY_ESCAPE, MASK_NONE));
        keys("i");
        {
            INFO("insert says so");
            CHECK(vim->status() == "-- INSERT --");
            CHECK(vim->inserting());
        }
        keys("<Esc>v");
        {
            INFO("visual says so");
            CHECK(vim->status() == "-- VISUAL --");
            CHECK_FALSE(vim->inserting());
        }
        keys("<Esc>3d");
        UNSCOPED_INFO("what is pending shows");
        CHECK(vim->status() == std::string("3d"));
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a click lands on a character; a drag is visual; a click leaves it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two\nthree four\nfive six\n");
        click(0, 7);
        UNSCOPED_INFO("past the end of a line lands on its last character");
        CHECK(caretText() == std::string("0:6"));
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        drag(1, 0, 1, 4);
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        keys("d");
        UNSCOPED_INFO("and the operator takes what was dragged, up to where the drag stopped");
        CHECK(flat(e.text()) == std::string("one two| four|five six|"));
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        drag(2, 3, 2, 0);
        UNSCOPED_INFO("dragged backwards is visual too");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        keys("y");
        UNSCOPED_INFO("yanked, as far as the drag reached");
        CHECK(vim->registerText('"') == std::string("five"));
        keys("V");
        CHECK(vim->mode() == ALVimKeymap::Mode::VisualLine);
        click(0, 1);
        {
            INFO("a click leaves visual for normal, at the click");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(caretText() == "0:1");
            CHECK_FALSE(e.hasSelection());
        }
        keys("i");
        click(1, 2);
        {
            INFO("a click in insert mode stays in insert mode");
            CHECK(vim->mode() == ALVimKeymap::Mode::Insert);
            CHECK(caretText() == "1:2");
        }
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: . after a visual operation repeats over as much from the caret", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("aaaa\nbbbb\ncccc\ndddd\neeee\nffff\n");
        keys("Vjd");
        UNSCOPED_INFO("two lines gone");
        CHECK(flat(e.text()) == std::string("cccc|dddd|eeee|ffff|"));
        keys(".");
        UNSCOPED_INFO(". takes two more");
        CHECK(flat(e.text()) == std::string("eeee|ffff|"));
        keys("u.");
        e.setText("abcdef\nghijkl\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("gg0vlld");
        UNSCOPED_INFO("three characters gone");
        CHECK(flat(e.text()) == std::string("def|ghijkl|"));
        keys("j0.");
        UNSCOPED_INFO(". takes three from the caret");
        CHECK(flat(e.text()) == std::string("def|jkl|"));
        keys("gg0vlc-<Esc>");
        UNSCOPED_INFO("a change over the selection");
        CHECK(flat(e.text()) == std::string("-f|jkl|"));
        keys("j0.");
        UNSCOPED_INFO(". changes as much again with the same text");
        CHECK(flat(e.text()) == std::string("-f|-l|"));
        // A selection the mouse made repeats the same way.
        e.setText("one two\nthree four\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        drag(0, 0, 0, 2);
        keys("d");
        UNSCOPED_INFO("the dragged three gone");
        CHECK(flat(e.text()) == std::string(" two|three four|"));
        keys("j0.");
        UNSCOPED_INFO(". takes three from the caret");
        CHECK(flat(e.text()) == std::string(" two|ee four|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: macros: q records keys as text; @ and @@ play them; qA adds", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one\ntwo\nthree\nfour\n");
        keys("qaA;<Esc>j");
        UNSCOPED_INFO("recording says so while it runs");
        CHECK(vim->recording());
        keys("q");
        CHECK_FALSE(vim->recording());
        UNSCOPED_INFO("the register holds the keys as text");
        CHECK(vim->registerText('a') == std::string("A;<Esc>j"));
        UNSCOPED_INFO("the recording ran as it was typed");
        CHECK(flat(e.text()) == std::string("one;|two|three|four|"));
        keys("@a");
        UNSCOPED_INFO("played once");
        CHECK(flat(e.text()) == std::string("one;|two;|three|four|"));
        keys("2@@");
        UNSCOPED_INFO("@@ plays the last again, the count times");
        CHECK(flat(e.text()) == std::string("one;|two;|three;|four;|"));
        keys("qbI-<Esc>q");
        keys("qBA!<Esc>q");
        UNSCOPED_INFO("qB adds to b");
        CHECK(vim->registerText('b') == std::string("I-<Esc>A!<Esc>"));
        keys("gg@b");
        // The recordings ran where they were typed, on the empty last line.
        UNSCOPED_INFO("both halves played");
        CHECK(flat(e.text()) == std::string("-one;!|two;|three;|four;|-!"));
        // Playing a register that plays itself stops rather than running away.
        keys("qc@cq");
        keys("@c");
        UNSCOPED_INFO("too recursive is said");
        CHECK(vim->messageIsError());
        keys("qa");
        UNSCOPED_INFO("recording shows in the status");
        CHECK(vim->status().find("recording @a") == 0);
        keys("q");
        // The keys decode as they were encoded.
        const std::vector<ALVimKeymap::Input> back = ALVimKeymap::decodeInputs("a<lt>b<C-r><Esc>");
        REQUIRE(back.size() == size_t(5));
        {
            INFO("a literal <");
            CHECK(back[1].isChar);
            CHECK(back[1].ch == '<');
        }
        {
            INFO("a control chord");
            CHECK_FALSE(back[3].isChar);
            CHECK(back[3].key == 'R');
            CHECK(back[3].mask & ALVimKeymap::CONTROL);
        }
        UNSCOPED_INFO("and encode back");
        CHECK(ALVimKeymap::encodeInputs(back) == std::string("a<lt>b<C-r><Esc>"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: control-A and control-X change the number under or after the caret", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("x = 5; y = 10\nn = -3\nnone here\n");
        keys("<C-a>");
        UNSCOPED_INFO("the first number after the caret, up one");
        CHECK(e.document().line(0) == std::string("x = 6; y = 10"));
        UNSCOPED_INFO("the caret on its last digit");
        CHECK(caretText() == std::string("0:4"));
        keys("5<C-x>");
        UNSCOPED_INFO("down five");
        CHECK(e.document().line(0) == std::string("x = 1; y = 10"));
        keys("w<C-a>");
        UNSCOPED_INFO("the number after the caret");
        CHECK(e.document().line(0) == std::string("x = 1; y = 11"));
        keys("<C-a>");
        UNSCOPED_INFO("under the caret still");
        CHECK(e.document().line(0) == std::string("x = 1; y = 12"));
        keys("j0<C-a>");
        UNSCOPED_INFO("a minus is the number's");
        CHECK(e.document().line(1) == std::string("n = -2"));
        keys("3<C-a>");
        UNSCOPED_INFO("through zero");
        CHECK(e.document().line(1) == std::string("n = 1"));
        keys("j<C-a>");
        UNSCOPED_INFO("nothing to change is left alone");
        CHECK(e.document().line(2) == std::string("none here"));
        keys("k.");
        UNSCOPED_INFO(". repeats it");
        CHECK(e.document().line(1) == std::string("n = 4"));
        // Hex and binary as vim's nrformats reads them, the width and the
        // case kept; leading zeros keep a decimal's width too.
        e.setText("a = 0x0fF; b = 0b0111; c = 007; d = 0x9");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("0<C-a>");
        UNSCOPED_INFO("hex, wrapping over its width, in the case it had");
        CHECK(e.document().line(0) == std::string("a = 0x100; b = 0b0111; c = 007; d = 0x9"));
        keys("w<C-a>");
        UNSCOPED_INFO("binary");
        CHECK(e.document().line(0) == std::string("a = 0x100; b = 0b1000; c = 007; d = 0x9"));
        keys("f7<C-a>");
        UNSCOPED_INFO("leading zeros kept");
        CHECK(e.document().line(0) == std::string("a = 0x100; b = 0b1000; c = 008; d = 0x9"));
        keys("$<C-a>");
        UNSCOPED_INFO("a hex digit becomes a letter");
        CHECK(e.document().line(0) == std::string("a = 0x100; b = 0b1000; c = 008; d = 0xa"));
        keys("0fF<C-x>");
        UNSCOPED_INFO("under the caret inside a hex number");
        CHECK(e.document().line(0) == std::string("a = 0x0ff; b = 0b1000; c = 008; d = 0xa"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :g and :v run commands on matching lines as one undo; :normal ranges", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("keep 1\ndrop 2\nkeep 3\ndrop 4\nkeep 5\n");
        keys(":g/drop/d<CR>");
        UNSCOPED_INFO("the dropped lines gone");
        CHECK(flat(e.text()) == std::string("keep 1|keep 3|keep 5|"));
        keys("u");
        UNSCOPED_INFO("undone as one");
        CHECK(flat(e.text()) == std::string("keep 1|drop 2|keep 3|drop 4|keep 5|"));
        keys(":v/drop/d<CR>");
        UNSCOPED_INFO(":v takes the others, the empty line after the final newline not among them");
        CHECK(flat(e.text()) == std::string("drop 2|drop 4|"));
        keys("u:g/drop/s/drop/held/<CR>");
        UNSCOPED_INFO("a substitution on the lines picked out");
        CHECK(flat(e.text()) == std::string("keep 1|held 2|keep 3|held 4|keep 5|"));
        keys(":g/keep/normal A;<CR>");
        UNSCOPED_INFO(":normal types on each");
        CHECK(flat(e.text()) == std::string("keep 1;|held 2|keep 3;|held 4|keep 5;|"));
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        keys(":2,3g/held/d<CR>");
        UNSCOPED_INFO("a range narrows it");
        CHECK(flat(e.text()) == std::string("keep 1;|keep 3;|held 4|keep 5;|"));
        keys(":g/zzz/d<CR>");
        UNSCOPED_INFO("nothing matching is said");
        CHECK(vim->messageIsError());
        keys(":g/keep/<CR>");
        {
            INFO("with no command, how many is said");
            CHECK_FALSE(vim->messageIsError());
            CHECK(vim->message() == "3 lines");
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :s takes vim's replacement spelling and flags; & g& and :&& repeat it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("cat hat\ncat bat\nrat\n");
        // The pattern in vim's spelling -- groups in \( \) -- and the
        // replacement too.
        keys(":s/\\(c\\)at/[&-\\1]/<CR>");
        UNSCOPED_INFO("& is the match and \\1 a group");
        CHECK(flat(e.text()) == std::string("[cat-c] hat|cat bat|rat|"));
        keys("j:s/at/~x/g<CR>");
        UNSCOPED_INFO("~ is the last replacement, read again as one");
        CHECK(flat(e.text()) == std::string("[cat-c] hat|c[at-]x b[at-]x|rat|"));
        keys("u:s/at/A\\&B/gn<CR>");
        {
            INFO("n counts without changing");
            CHECK_FALSE(vim->messageIsError());
            CHECK(vim->message() == "2 matches on 1 line");
        }
        UNSCOPED_INFO("nothing changed");
        CHECK(flat(e.text()) == std::string("[cat-c] hat|cat bat|rat|"));
        keys(":s/at/og/<CR>");
        UNSCOPED_INFO("the first on the line");
        CHECK(flat(e.text()) == std::string("[cat-c] hat|cog bat|rat|"));
        keys("&");
        UNSCOPED_INFO("& again on the line, without the flags");
        CHECK(flat(e.text()) == std::string("[cat-c] hat|cog bog|rat|"));
        keys("uu:s/at/og/g<CR>u");
        keys("gg:&&<CR>");
        UNSCOPED_INFO(":&& does the last :s again with its flags");
        CHECK(flat(e.text()) == std::string("[cog-c] hog|cat bat|rat|"));
        keys("ug&");
        UNSCOPED_INFO("g& on every line, with the flags");
        CHECK(flat(e.text()) == std::string("[cog-c] hog|cog bog|rog|"));
        keys("u&");
        UNSCOPED_INFO("& on this line without them, as vim has it, which forgets them");
        CHECK(flat(e.text()) == std::string("[cog-c] hat|cat bat|rat|"));
        keys("u:&&<CR>");
        UNSCOPED_INFO("so :&& now has none");
        CHECK(flat(e.text()) == std::string("[cog-c] hat|cat bat|rat|"));
        keys("u");
        keys(":s/zzz/y/e<CR>");
        UNSCOPED_INFO("e says nothing where nothing matches");
        CHECK_FALSE(vim->messageIsError());
        keys(":s/zzz/y/<CR>");
        UNSCOPED_INFO("without e it does");
        CHECK(vim->messageIsError());
        keys("gg:s/hat/one\\rtwo/<CR>");
        UNSCOPED_INFO("\\r breaks the line");
        CHECK(flat(e.text()) == std::string("[cat-c] one|two|cat bat|rat|"));
        keys(":s/two/\\U&/<CR>");
        UNSCOPED_INFO("\\U upper-cases what follows");
        CHECK(flat(e.text()) == std::string("[cat-c] one|TWO|cat bat|rat|"));
        keys(":s/t/$/I<CR>");
        UNSCOPED_INFO("I matches the case as written: no lower t here");
        CHECK(vim->messageIsError());
        keys(":s/T/$/I<CR>");
        UNSCOPED_INFO("a $ is a $");
        CHECK(flat(e.text()) == std::string("[cat-c] one|$WO|cat bat|rat|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the : and / lines keep a history; q: and q/ open it; @: runs it again", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a1\na2\na3\na4\n");
        keys(":s/a/b/<CR>");
        keys("j:s/a/c/<CR>");
        keys("j:<Up>");
        UNSCOPED_INFO("Up brings the last line back");
        CHECK(vim->commandLine() == std::string("s/a/c/"));
        keys("<Up>");
        UNSCOPED_INFO("and the one before");
        CHECK(vim->commandLine() == std::string("s/a/b/"));
        keys("<Up>");
        UNSCOPED_INFO("no further");
        CHECK(vim->commandLine() == std::string("s/a/b/"));
        keys("<Down><Down>");
        UNSCOPED_INFO("Down past the newest is what was typed");
        CHECK(vim->commandLine() == std::string(""));
        keys("s/a/b<Up>");
        UNSCOPED_INFO("Up walks only the lines that start as this one");
        CHECK(vim->commandLine() == std::string("s/a/b/"));
        keys("<CR>");
        CHECK(flat(e.text()) == std::string("b1|c2|b3|a4|"));
        keys("j@:");
        UNSCOPED_INFO("@: runs the last line again");
        CHECK(flat(e.text()) == std::string("b1|c2|b3|b4|"));
        keys("gg/a4<CR>");
        keys("q/");
        UNSCOPED_INFO("q/ opens the search line");
        CHECK(vim->mode() == ALVimKeymap::Mode::Search);
        UNSCOPED_INFO("at the last search");
        CHECK(vim->commandLine() == std::string("a4"));
        keys("<Esc>q:");
        UNSCOPED_INFO("q: opens the : line");
        CHECK(vim->mode() == ALVimKeymap::Mode::Command);
        UNSCOPED_INFO("at the last command");
        CHECK(vim->commandLine() == std::string("s/a/b/"));
        keys("<Up>");
        UNSCOPED_INFO("with the history above it");
        CHECK(vim->commandLine() == std::string("s/a/c/"));
        keys("<Esc>");
        UNSCOPED_INFO("escape leaves it");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: it and at pair a tag with its own close through nesting and counts", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("<div><!-- <p> --><p>one <br/> <b>two</b> three</p></div>\n");
        keys("0fwdit");
        UNSCOPED_INFO("it inside the innermost");
        CHECK(flat(e.text()) == std::string("<div><!-- <p> --><p>one <br/> <b></b> three</p></div>|"));
        keys("u0fwd2it");
        UNSCOPED_INFO("2it a level out, the inner b of the same nesting not taken for the close");
        CHECK(flat(e.text()) == std::string("<div><!-- <p> --><p></p></div>|"));
        keys("u0fwd3at");
        UNSCOPED_INFO("3at the outer tag whole, the comment's <p> no tag");
        CHECK(flat(e.text()) == std::string("|"));
        keys("u0fwd4at");
        UNSCOPED_INFO("no fourth: nothing");
        CHECK(flat(e.text()) == std::string("<div><!-- <p> --><p>one <br/> <b>two</b> three</p></div>|"));
        e.setText("<a><b>x</b></a><a>y</a>\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("0fydat");
        UNSCOPED_INFO("the second a is its own pair");
        CHECK(flat(e.text()) == std::string("<a><b>x</b></a>|"));
        keys("0fxvatd");
        UNSCOPED_INFO("at in visual");
        CHECK(flat(e.text()) == std::string("<a></a>|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: patterns are vim's: magic levels; word bounds; zs and ze; lazy; case", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foo(bar) foo+bar Foo|bar foobar\n");
        keys(":s/foo(bar)/X/<CR>");
        UNSCOPED_INFO("brackets are themselves in magic mode");
        CHECK(flat(e.text()) == std::string("X foo+bar Foo|bar foobar|"));
        keys(":s/foo+bar/Y/<CR>");
        UNSCOPED_INFO("as is a plus");
        CHECK(flat(e.text()) == std::string("X Y Foo|bar foobar|"));
        keys(":s/Foo|bar/Z/<CR>");
        UNSCOPED_INFO("and a bar");
        CHECK(flat(e.text()) == std::string("X Y Z foobar|"));
        keys(":s/\\v(foo)(bar)/\\2\\1/<CR>");
        UNSCOPED_INFO("very magic: bare brackets group");
        CHECK(flat(e.text()) == std::string("X Y Z barfoo|"));
        keys(":s/\\<Y\\>/why/<CR>");
        UNSCOPED_INFO("word bounds");
        CHECK(flat(e.text()) == std::string("X why Z barfoo|"));
        keys(":s/bar\\zsfoo/FOO/<CR>");
        UNSCOPED_INFO("\\zs starts the match after what came before");
        CHECK(flat(e.text()) == std::string("X why Z barFOO|"));
        keys(":s/bar\\zeFOO/BAR/<CR>");
        UNSCOPED_INFO("\\ze ends it before what follows");
        CHECK(flat(e.text()) == std::string("X why Z BARFOO|"));
        keys(":s/B.\\{-}O/-/<CR>");
        UNSCOPED_INFO("\\{-} is lazy");
        CHECK(flat(e.text()) == std::string("X why Z -O|"));
        keys(":s/\\V-O\\$/[-O]/<CR>");
        UNSCOPED_INFO("very nomagic: \\$ the line's end");
        CHECK(flat(e.text()) == std::string("X why Z [-O]|"));
        keys(":s/\\V[-O]/end/<CR>");
        UNSCOPED_INFO("brackets themselves under \\V");
        CHECK(flat(e.text()) == std::string("X why Z end|"));
        keys(":s/x/lower/<CR>");
        UNSCOPED_INFO("case matters by default, as vim has it");
        CHECK(vim->messageIsError());
        keys(":s/\\cx/lower/<CR>");
        UNSCOPED_INFO("\\c ignores it");
        CHECK(flat(e.text()) == std::string("lower why Z end|"));
        keys(":set ic<CR>:s/z/zed/<CR>");
        UNSCOPED_INFO(":set ignorecase ignores it");
        CHECK(flat(e.text()) == std::string("lower why zed end|"));
        keys(":set scs<CR>:s/W/w/<CR>");
        UNSCOPED_INFO("smartcase: a capital makes it matter again");
        CHECK(vim->messageIsError());
        keys(":s/\\CWHY/w/<CR>");
        UNSCOPED_INFO("\\C too");
        CHECK(vim->messageIsError());
        keys(":s/WHY/W/i<CR>");
        UNSCOPED_INFO("the i flag overrides");
        CHECK(flat(e.text()) == std::string("lower W zed end|"));
        keys(":set noic<CR>/W<CR>");
        {
            INFO("/ is case-sensitive again, and finds the capital");
            CHECK_FALSE(vim->messageIsError());
            CHECK(caretText() == "0:6");
        }
        keys(":g/\\v^(lower)/s/end/END/<CR>");
        UNSCOPED_INFO(":g takes vim's spelling too");
        CHECK(flat(e.text()) == std::string("lower W zed END|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a pattern's ~ and optional sequence; %V %# %l %c pin its start", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("fun func function\nfun func function\nfun func function\n");
        keys(":s/fun\\%[ction]/X/g<CR>");
        UNSCOPED_INFO("\\%[ction] takes as much of the sequence as is there");
        CHECK(flat(e.text()) == std::string("X X X|fun func function|fun func function|"));
        keys("u:s/func/Y/<CR>j:s/~tion/Z/<CR>");
        UNSCOPED_INFO("~ is the last replacement, as text");
        CHECK(flat(e.text()) == std::string("fun Y function|fun func function|fun func function|"));
        UNSCOPED_INFO("nothing matched Ytion, so an error");
        CHECK(vim->messageIsError());
        keys("gg0wve<Esc>:%s/\\%Vfun/V/g<CR>");
        UNSCOPED_INFO("\\%V: only where the last visual area was");
        CHECK(flat(e.text()) == std::string("fun Y Vction|fun func function|fun func function|"));
        keys(":%s/\\%3lfun/L/g<CR>");
        UNSCOPED_INFO("\\%3l: only on line 3");
        CHECK(flat(e.text()) == std::string("fun Y Vction|fun func function|L Lc Lction|"));
        keys(":%s/\\%>1lfun\\%<3l/M/g<CR>");
        UNSCOPED_INFO("\\%>1l and \\%<3l: between");
        CHECK(flat(e.text()) == std::string("fun Y Vction|M Mc Mction|L Lc Lction|"));
        keys(":%s/\\%5cf/C/g<CR>");
        UNSCOPED_INFO("\\%5c: nothing at column 5");
        CHECK(vim->messageIsError());
        keys(":%s/\\%3cn/C/g<CR>");
        UNSCOPED_INFO("at column 3");
        CHECK(flat(e.text()) == std::string("fuC Y Vction|M Mc Mction|L Lc Lction|"));
        keys("gg0:%s/\\%#fu/H/g<CR>");
        UNSCOPED_INFO("\\%#: only at the caret");
        CHECK(flat(e.text()) == std::string("HC Y Vction|M Mc Mction|L Lc Lction|"));
        keys(":set ic<CR>");
        UNSCOPED_INFO("the setting is shared with whoever shares the state");
        CHECK(vim->shared().ignoreCase);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a place before zs is the whole match's; %^ and %$; character codes", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foobar foobar\nfoobar\nfoobar foobar\n");
        keys("0wve<Esc>:%s/\\%Vfoo\\zsbar/X/g<CR>");
        UNSCOPED_INFO("\\%V before \\zs: where the whole match starts, in the visual area");
        CHECK(flat(e.text()) == std::string("foobar fooX|foobar|foobar foobar|"));
        keys(":%s/foo\\zs\\(bar\\)/[\\1]/g<CR>");
        UNSCOPED_INFO("the group after \\zs, the replacement over the whole");
        CHECK(flat(e.text()) == std::string("foo[bar] fooX|foo[bar]|foo[bar] foo[bar]|"));
        keys(":%s/\\%^foo/START/<CR>");
        UNSCOPED_INFO("\\%^: the file's start alone");
        CHECK(flat(e.text()) == std::string("START[bar] fooX|foo[bar]|foo[bar] foo[bar]|"));
        keys(":%s/\\]\\%$/END/<CR>");
        UNSCOPED_INFO("\\%$: the file's end alone");
        CHECK(flat(e.text()) == std::string("START[bar] fooX|foo[bar]|foo[bar] foo[barEND|"));
        keys(":%s/\\%d91bar\\%x5d/_/g<CR>");
        UNSCOPED_INFO("\\%d91 and \\%x5d are the brackets");
        CHECK(flat(e.text()) == std::string("START_ fooX|foo_|foo_ foo[barEND|"));
        keys(":%s/\\%u0058/Y/<CR>");
        UNSCOPED_INFO("\\u0058 is X");
        CHECK(flat(e.text()) == std::string("START_ fooY|foo_|foo_ foo[barEND|"));
        // A block-wise area is its columns on each of its lines.
        e.setText("ab ab\nab ab\nab ab\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("gg0<C-v>jl<Esc>:%s/\\%Vab/X/g<CR>");
        UNSCOPED_INFO("\\%V on a block: the first columns of the first two lines");
        CHECK(flat(e.text()) == std::string("X ab|X ab|ab ab|"));
        keys(":%s/\\Va\\{1,2}b/Y/<CR>");
        UNSCOPED_INFO("\\{n,m} under \\V is the multi still");
        CHECK(flat(e.text()) == std::string("X Y|X Y|Y ab|"));
        e.setText("fun func function aaab\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys(":s/\\vfun%[ction]/Z/g<CR>");
        UNSCOPED_INFO("%[] under \\v");
        CHECK(flat(e.text()) == std::string("Z Z Z aaab|"));
        keys(":s/\\va{-2,}/Q/<CR>");
        UNSCOPED_INFO("{-n,} under \\v is lazy: the fewest");
        CHECK(flat(e.text()) == std::string("Z Z Z Qab|"));
        // A place that passes over the first match: the replacement is
        // still the kept match's own, groups and all.
        e.setText("foobar foobaz foobaq\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("0wve<Esc>:s/\\%Vfoo\\zs\\(ba.\\)/[&\\1]/g<CR>");
        UNSCOPED_INFO("\\%V passing over a \\zs match: & and \\1 are the kept one's");
        CHECK(flat(e.text()) == std::string("foobar foo[bazbaz] foobaq|"));
        // After a top-level \zs, & is what the match is, and the groups
        // are counted as they were written, one before the \zs too.
        e.setText("foobar\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys(":s/foo\\zsbar/[&]/<CR>");
        UNSCOPED_INFO("& after \\zs is the match alone");
        CHECK(flat(e.text()) == std::string("foo[bar]|"));
        e.setText("foobar\n");
        keys(":s/\\(f\\)oo\\zs\\(bar\\)/[\\2\\1&]/<CR>");
        UNSCOPED_INFO("a group before \\zs is still \\1");
        CHECK(flat(e.text()) == std::string("foo[barfbar]|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :s with the c flag asks about each match: y n a l and escape; one undo", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a a a a\na a\n");
        keys(":%s/a/b/gc<CR>");
        CHECK(vim->mode() == ALVimKeymap::Mode::Confirm);
        UNSCOPED_INFO("the question");
        CHECK(vim->status() == std::string("replace with b (y/n/a/q/l)?"));
        UNSCOPED_INFO("the first match selected");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)));
        keys("y");
        UNSCOPED_INFO("y: this one");
        CHECK(flat(e.text()) == std::string("b a a a|a a|"));
        UNSCOPED_INFO("the next selected, where it now is");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 3)));
        keys("n");
        UNSCOPED_INFO("n: not this one");
        CHECK(flat(e.text()) == std::string("b a a a|a a|"));
        keys("l");
        UNSCOPED_INFO("l: this one and no more");
        CHECK(flat(e.text()) == std::string("b a b a|a a|"));
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        keys(":%s/a/c/gc<CR>a");
        UNSCOPED_INFO("a: all the rest");
        CHECK(flat(e.text()) == std::string("b c b c|c c|"));
        keys(":%s/c/d/gc<CR>yy<Esc>");
        UNSCOPED_INFO("escape stops it after the two");
        CHECK(flat(e.text()) == std::string("b d b d|c c|"));
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        keys("u");
        UNSCOPED_INFO("the whole asking is one step to undo, as vim has it");
        CHECK(flat(e.text()) == std::string("b c b c|c c|"));
        // A \zs inside brackets with a place before it.
        e.setText("xab xab\nxab\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("gg0ve<Esc>:%s/\\%Vx\\(a\\zsb\\)/Y/g<CR>");
        UNSCOPED_INFO("the whole starts in the area, the match after the \\zs: only the first");
        CHECK(flat(e.text()) == std::string("xaY xab|xab|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :s asking lights the matches to come and lets control-E and Y scroll", "[alvimkeymap]")
    {
        // Enough lines under the matches for the view to scroll.
        std::string text = "a a\na\na\n";
        for (int i = 0; i < 30; ++i)
        {
            text += "x\n";
        }
        ALCodeEditor& e = make(text.c_str());
        keys(":%s/a/b/gc<CR>");
        CHECK(e.highlights().size() == size_t(4));
        keys("y");
        REQUIRE(e.highlights().size() == size_t(3));
        UNSCOPED_INFO("the lit ones are the ones to come");
        CHECK(e.highlights()[0] == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 3)));
        const S32 before = e.scrollY();
        keys("<C-E>");
        CHECK(vim->mode() == ALVimKeymap::Mode::Confirm);
        UNSCOPED_INFO("scrolled a row");
        CHECK(e.scrollY() == before + e.layout().rowHeight());
        keys("<C-Y>");
        UNSCOPED_INFO("and back");
        CHECK(e.scrollY() == before);
        keys("q");
        UNSCOPED_INFO("nothing lit once done");
        CHECK(e.highlights().empty());
        UNSCOPED_INFO("what was said yes to");
        CHECK(flat(e.text()).substr(0, 9) == std::string("b a|a|a|x"));
        // Patterns over a line's end.
        e.setText("one two\nthree four\nfive\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("gg0/two\\nthree<CR>");
        UNSCOPED_INFO("found over the break");
        CHECK(e.caret() == ALTextPos(0, 4));
        keys(":%s/two\\_sthree/joined/<CR>");
        UNSCOPED_INFO("\\_s reaches the next line, and the two lines are one");
        CHECK(flat(e.text()) == std::string("one joined four|five|"));
        keys(":%s/four\\n//<CR>");
        UNSCOPED_INFO("\\n taken out joins the lines");
        CHECK(flat(e.text()) == std::string("one joined five|"));
        // An asking :s under :g asks line by line, the :g waiting on each.
        e.setText("a x\nb\na y\na z\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys(":g/^a/s/a/A/c<CR>");
        CHECK(vim->mode() == ALVimKeymap::Mode::Confirm);
        UNSCOPED_INFO("the line's one to ask about");
        CHECK(e.highlights().size() == size_t(1));
        UNSCOPED_INFO("the first line's first");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)));
        keys("yny");
        UNSCOPED_INFO("the first and the third said yes to");
        CHECK(flat(e.text()) == std::string("A x|b|a y|A z|"));
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        keys("u");
        UNSCOPED_INFO("one step to undo for the lot");
        CHECK(flat(e.text()) == std::string("a x|b|a y|a z|"));
        // A match over lines says so in the question.
        keys(":%s/x\\nb/Q/c<CR>");
        UNSCOPED_INFO("the question says how many lines");
        CHECK(vim->status() == std::string("replace with Q (over 2 lines) (y/n/a/q/l)?"));
        keys("q");
        // The lines are run from the top down: a line with none to
        // substitute is no error under :g, as in vim.
        keys(":g/a/s/x/X/c<CR>");
        CHECK(vim->mode() == ALVimKeymap::Mode::Confirm);
        keys("y");
        UNSCOPED_INFO("the one there was");
        CHECK(flat(e.text()) == std::string("a X|b|a y|a z|"));
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        keys("u");
        // The first line asked about, then the command after it there,
        // which errors, and the :g goes no further.
        keys(":g/a/s/x/X/c|nosuch<CR>");
        UNSCOPED_INFO("asking first");
        CHECK(vim->mode() == ALVimKeymap::Mode::Confirm);
        keys("y");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        CHECK(vim->message() == std::string("E492: Not an editor command: nosuch"));
        UNSCOPED_INFO("what was said yes to made");
        CHECK(flat(e.text()) == std::string("a X|b|a y|a z|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the : line is edited in place; q: goes through the host's window", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one\n");
        std::string   line;
        S32           caret = 0;
        keys(":abc");
        {
            INFO("typing");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":abc");
            CHECK(caret == 4);
        }
        keys("<Left><Left>");
        {
            INFO("two back");
            CHECK(vim->typingLine(line, caret));
            CHECK(caret == 2);
        }
        keys("X");
        {
            INFO("put in at the cursor");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":aXbc");
            CHECK(caret == 3);
        }
        keys("<Home>");
        {
            INFO("home");
            CHECK(vim->typingLine(line, caret));
            CHECK(caret == 1);
        }
        keys("<Delete>");
        {
            INFO("the character at the cursor gone");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":Xbc");
            CHECK(caret == 1);
        }
        keys("<End><BS>");
        {
            INFO("the one before the end gone");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":Xb");
            CHECK(caret == 3);
        }
        keys(" two words<C-W>");
        {
            INFO("a word back");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":Xb two ");
        }
        keys("<C-B>");
        {
            INFO("to the start");
            CHECK(vim->typingLine(line, caret));
            CHECK(caret == 1);
        }
        keys("<C-E>q<C-U>");
        {
            INFO("to the end, then the whole line gone");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":");
            CHECK(caret == 1);
        }
        keys("<Esc>");
        // A message goes at a click, as it does at a key.
        keys(":nosuch<CR>");
        CHECK_FALSE(vim->message().empty());
        e.handleMouseDown(10, 10, MASK_NONE);
        e.handleMouseUp(10, 10, MASK_NONE);
        UNSCOPED_INFO("cleared by the click");
        CHECK(vim->message().empty());
        // q: through the host's window: the history handed over, and
        // what is picked put on the line to edit.
        keys(":set number<CR>");
        std::vector<std::string> offered;
        bool hold = false;
        vim->hooks().historyWindow = [&offered, &hold](ALTextView& view, llwchar kind, const std::vector<std::string>& history, std::function<void(const std::string&, bool)> chosen) {
            offered = history;
            chosen(history.front(), !hold);
        };
        keys("q:");
        {
            INFO("the history was offered");
            REQUIRE_FALSE(offered.empty());
            CHECK(std::find(offered.begin(), offered.end(), "set number") != offered.end());
        }
        UNSCOPED_INFO("the pick was run, as vim's window runs a row");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        // Picked to hold -- the window's Shift-Return -- it is put up to
        // edit instead.
        hold = true;
        keys("q:");
        {
            INFO("on the line to edit");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":" + offered.front());
            CHECK(caret == static_cast<S32>(line.size()));
        }
        keys("<Esc>");
        // Shift-Left and Shift-Right go a WORD at a time on the line.
        keys(":one two three");
        vim->handleKey(e, KEY_LEFT, MASK_SHIFT);
        {
            INFO("a word back");
            CHECK(vim->typingLine(line, caret));
            CHECK(caret == 9);
        }
        vim->handleKey(e, KEY_LEFT, MASK_SHIFT);
        {
            INFO("another");
            CHECK(vim->typingLine(line, caret));
            CHECK(caret == 5);
        }
        vim->handleKey(e, KEY_RIGHT, MASK_SHIFT);
        {
            INFO("and on to the next word's start");
            CHECK(vim->typingLine(line, caret));
            CHECK(caret == 9);
        }
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: Tab on the : line completes the word at the cursor; Tab walks the row", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one\n");
        std::string   line;
        S32           caret = 0;
        std::vector<std::string> items;
        S32                      chosen = -1;
        vim->hooks().complete = [](ALTextView&, const std::string& command, const std::string&, std::vector<std::string>& out) {
            if (command.empty())
            {
                out.push_back("write");
                out.push_back("wq");
                out.push_back("wall");
            }
            else if (command == "set")
            {
                out.push_back("number");
                out.push_back("nonumber");
            }
        };
        // One answer is put in, with no row.
        keys(":su<Tab>");
        CHECK(vim->typingLine(line, caret));
        CHECK(line == ":substitute");
        CHECK(caret == 11);
        UNSCOPED_INFO("no row for one");
        CHECK_FALSE(vim->menu(items, chosen));
        keys("<Esc>");
        // Several: the first on the line, the row up, Tab walking on and
        // back round to what was typed; Shift-Tab back.
        keys(":w<Tab>");
        {
            INFO("the first in order");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":wall");
        }
        {
            INFO("the row up");
            CHECK(vim->menu(items, chosen));
            REQUIRE(items.size() == 3);
            CHECK(chosen == 0);
            CHECK(items[0] == "wall");
            CHECK(items[1] == "wq");
            CHECK(items[2] == "write");
        }
        keys("<Tab>");
        {
            INFO("the next");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":wq");
            CHECK(vim->menu(items, chosen));
            CHECK(chosen == 1);
        }
        keys("<Tab><Tab>");
        {
            INFO("past the last, the word as typed");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":w");
            CHECK(vim->menu(items, chosen));
            CHECK(chosen == -1);
        }
        vim->handleKey(e, KEY_TAB, MASK_SHIFT);
        {
            INFO("Shift-Tab back to the last");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":write");
            CHECK(caret == 6);
        }
        // Escape with the row up drops it and puts the word back; the
        // line stays.
        keys("<Esc>");
        {
            INFO("the row gone, the word as typed");
            CHECK_FALSE(vim->menu(items, chosen));
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":w");
        }
        keys("<Esc>");
        UNSCOPED_INFO("then the line");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        // An argument: what the command takes, the host's and the
        // keymap's own; a range in front does not confuse the name.
        keys(":%set no<Tab>");
        {
            INFO("the options starting no");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":%set noexpandtab");
            CHECK(vim->menu(items, chosen));
            CHECK(items.size() == 8);
        }
        keys("<Tab><Tab><Tab><Tab>");
        {
            INFO("nonumber among them");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":%set nonumber");
        }
        // Typing on keeps what is on the line and lets the row go.
        keys("x");
        {
            INFO("the row gone");
            CHECK_FALSE(vim->menu(items, chosen));
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":%set nonumberx");
        }
        keys("<Esc>");
        // Nothing to offer changes nothing.
        keys(":zzz<Tab>");
        {
            INFO("as typed");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":zzz");
            CHECK_FALSE(vim->menu(items, chosen));
        }
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: j and k keep the wanted column; $ wants every end; gj and gk walk rows", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a long first line here\nab\nanother long line there\n");
        e.setCaret(ALTextPos(0, 10));
        keys("j");
        UNSCOPED_INFO("clamped to the short line");
        CHECK(e.caret() == ALTextPos(1, 1));
        keys("j");
        UNSCOPED_INFO("the column wanted again below");
        CHECK(e.caret() == ALTextPos(2, 10));
        keys("kk");
        UNSCOPED_INFO("and above");
        CHECK(e.caret() == ALTextPos(0, 10));
        keys("l");
        keys("jj");
        UNSCOPED_INFO("a sideways move sets a new wanted column");
        CHECK(e.caret() == ALTextPos(2, 11));
        keys("$");
        keys("kk");
        UNSCOPED_INFO("after $, every line's end");
        CHECK(e.caret() == ALTextPos(0, 21));
        keys("j");
        UNSCOPED_INFO("the short one's too");
        CHECK(e.caret() == ALTextPos(1, 1));
        // A count typed does not forget the column on its way.
        keys("gg05lj1j");
        UNSCOPED_INFO("through the short line with a count");
        CHECK(e.caret() == ALTextPos(2, 5));
        // Wrapped: gj goes a row down within the line.
        e.setWordWrap(true);
        e.reshape(120, 200);
        e.setCaret(ALTextPos(0, 0));
        keys("gj");
        {
            INFO("a row down, still on the first line");
            CHECK(e.caret().line == 0);
            CHECK(e.caret().column > 0);
        }
        keys("gk");
        UNSCOPED_INFO("and back");
        CHECK(e.caret() == ALTextPos(0, 0));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: dge; numbered registers hold deletes; case changes reach past ASCII", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two three\ncaf\xc3\xa9\nline three\nline four\n");
        e.setCaret(ALTextPos(0, 9));
        keys("dge");
        UNSCOPED_INFO("from the previous word's end through the caret");
        CHECK(e.document().line(0) == std::string("one twree"));
        keys("u");
        keys("jdd");
        keys("dd");
        UNSCOPED_INFO("two lines gone");
        CHECK(e.document().lineCount() == 3);
        UNSCOPED_INFO("the last delete in 1");
        CHECK(vim->registerText('1') == std::string("line three"));
        UNSCOPED_INFO("the one before in 2");
        CHECK(vim->registerText('2') == std::string("caf\xc3\xa9"));
        keys("\"2p");
        UNSCOPED_INFO("put back from 2");
        CHECK(e.document().line(2) == std::string("caf\xc3\xa9"));
        keys("0x");
        UNSCOPED_INFO("a small delete goes to -");
        CHECK(vim->registerText('-') == std::string("c"));
        keys("\"-P");
        keys("0~~~~");
        UNSCOPED_INFO("swapped past ASCII");
        CHECK(e.document().line(2) == std::string("CAF\xc3\x89"));
        keys("0gUU");
        keys("0guu");
        UNSCOPED_INFO("lowered past ASCII");
        CHECK(e.document().line(2) == std::string("caf\xc3\xa9"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a counted insert repeats what a backspace left; marks move with text", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("\nsecond\nthird\n");
        keys("3ihelo<BS>lo<Esc>");
        UNSCOPED_INFO("what stood, three times");
        CHECK(e.document().line(0) == std::string("hellohellohello"));
        keys("jma");
        keys("ggO<Esc>");
        keys("ggOnew<Esc>");
        keys("`a");
        UNSCOPED_INFO("the mark two lines further down");
        CHECK(e.caret() == ALTextPos(3, 5));
        keys("kdd");
        keys("`a");
        UNSCOPED_INFO("and back up with a line above it gone");
        CHECK(e.caret() == ALTextPos(2, 5));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :sort orders lines; :m moves them and :t copies them by address", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("pear\napple\nFig\nbanana\n");
        keys(":sort<CR>");
        UNSCOPED_INFO("sorted, capitals first as bytes are");
        CHECK(e.text() == std::string("Fig\napple\nbanana\npear\n"));
        keys(":sort! i<CR>");
        UNSCOPED_INFO("the other way round, case aside");
        CHECK(e.text() == std::string("pear\nFig\nbanana\napple\n"));
        keys(":%sort i<CR>");
        keys(":1m$<CR>");
        UNSCOPED_INFO("the first moved to the end");
        CHECK(e.text() == std::string("banana\nFig\npear\napple\n"));
        UNSCOPED_INFO("the caret on the moved line");
        CHECK(e.caret().line == 3);
        keys(":3,4m0<CR>");
        UNSCOPED_INFO("two moved to the top");
        CHECK(e.text() == std::string("pear\napple\nbanana\nFig\n"));
        keys(":1t1<CR>");
        UNSCOPED_INFO("the first copied below itself");
        CHECK(e.text() == std::string("pear\npear\napple\nbanana\nFig\n"));
        keys(":2,3m1<CR>");
        UNSCOPED_INFO("moving lines to just above themselves changes nothing");
        CHECK(e.text() == std::string("pear\npear\napple\nbanana\nFig\n"));
        keys(":1,3m2<CR>");
        UNSCOPED_INFO("moving a range into itself is refused");
        CHECK_FALSE(vim->message().empty());
        keys(":2,3> 1<CR>");
        UNSCOPED_INFO("a count after > shifts so many from the range's end");
        CHECK(e.document().line(2) == std::string("\tapple"));
        keys(":1>><CR>");
        UNSCOPED_INFO("doubled, two steps");
        CHECK(e.document().line(0) == std::string("\t\tpear"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: with completion on a command opens no list; one Escape closes it all", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("counter = count;\n", "lsl", true);
        keys("ll");
        UNSCOPED_INFO("no list for a motion inside a word");
        CHECK_FALSE(e.completionOpen());
        keys("x.");
        UNSCOPED_INFO("nor for an edit, or its repeat");
        CHECK_FALSE(e.completionOpen());
        keys("A co");
        CHECK(vim->inserting());
        UNSCOPED_INFO("the list opens as typing goes");
        CHECK(e.completionOpen());
        keys("<Esc>");
        UNSCOPED_INFO("one Escape closes the list");
        CHECK_FALSE(e.completionOpen());
        CHECK_FALSE(vim->inserting());

        keys("o");
        ALCodeEditor::Signature call;
        call.label = "llSay(integer channel, string msg)";
        e.showSignature(e.caret(), call);
        UNSCOPED_INFO("a signature while inserting");
        CHECK(e.signature() != nullptr);
        keys("<Esc>");
        {
            INFO("gone with the one Escape");
            CHECK(e.signature() == nullptr);
            CHECK_FALSE(vim->inserting());
        }
        e.showSignature(e.caret(), call);
        UNSCOPED_INFO("an answer arriving after is not shown");
        CHECK(e.signature() == nullptr);

        keys("ico");
        UNSCOPED_INFO("up again");
        CHECK(e.completionOpen());
        keys("<C-c>");
        {
            INFO("control-c leaves insert and the list");
            CHECK_FALSE(vim->inserting());
            CHECK_FALSE(e.completionOpen());
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :s fills its groups from the match where ze looks past its end", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foobar foobaz\n");
        keys(":s/\\(foo\\)\\zebar/[\\1]/<CR>");
        UNSCOPED_INFO("the group, found where what follows is bar");
        CHECK(flat(e.text()) == std::string("[foo]bar foobaz|"));
        UNSCOPED_INFO("and nothing said against it");
        CHECK_FALSE(vim->messageIsError());
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: vim turned off in insert mode leaves no undo step open", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("\n");
        keys("iab");
        CHECK(vim->inserting());
        e.setModalKeymap(nullptr);
        vim = nullptr;
        for (const char c : { 'c', 'd' })
        {
            e.handleUnicodeCharHere(static_cast<llwchar>(c));
        }
        CHECK(e.document().line(0) == std::string("abcd"));
        e.undo();
        UNSCOPED_INFO("the typing after undone alone");
        CHECK(e.document().line(0) == std::string("ab"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: an operator's count times its motion's is capped rather than wrapping", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd\ne\n");
        keys("50000d50000d");
        UNSCOPED_INFO("every line from the caret");
        CHECK(flat(e.text()) == std::string(""));
        ALCodeEditor& f = make("a\nb\nc\nd\ne\n");
        keys("50000d50000j");
        UNSCOPED_INFO("and a motion likewise");
        CHECK(flat(f.text()) == std::string(""));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :g inside :g is refused as vim refuses it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a1\nb2\na3\n");
        keys(":g/a/g/1/d<CR>");
        {
            INFO("refused");
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("E147") != std::string::npos);
        }
        UNSCOPED_INFO("nothing done");
        CHECK(flat(e.text()) == std::string("a1|b2|a3|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the clipboard: named registers never; unnamed by the setting; + always", "[alvimkeymap]")
    {
        make("one\ntwo\n");
        const auto clipboard = []() {
            std::string text;
            LLClipboard::instance().pasteFromClipboard(text);
            return text;
        };
        const std::string outside("outside");
        LLClipboard::instance().copyToClipboard(outside, 0, static_cast<S32>(outside.size()));
        keys("\"ayy");
        UNSCOPED_INFO("a named register leaves the clipboard alone");
        CHECK(clipboard() == outside);
        UNSCOPED_INFO("and \"\" names it");
        CHECK(vim->registerText('"') == std::string("one"));

        UNSCOPED_INFO("off, as vim has it");
        CHECK_FALSE(vim->shared().unnamedClipboard);
        keys("x");
        UNSCOPED_INFO("off: a delete leaves the clipboard alone");
        CHECK(clipboard() == outside);
        UNSCOPED_INFO("and keeps what it took");
        CHECK(vim->registerText('"') == std::string("o"));
        keys("p");
        UNSCOPED_INFO("which is what p puts");
        CHECK(flat(editor->text()) == std::string("noe|two|"));
        keys("\"+yy");
        UNSCOPED_INFO("\"+ is the clipboard whatever");
        CHECK(clipboard() == std::string("noe"));

        keys(":set clipboard=unnamed<CR>");
        CHECK(vim->shared().unnamedClipboard);
        keys("jyy");
        UNSCOPED_INFO("a yank goes by the clipboard");
        CHECK(clipboard() == std::string("two"));
        keys(":set clipboard=<CR>");
        CHECK_FALSE(vim->shared().unnamedClipboard);
        keys(":set clipboard=unnamed<CR>");
        keys(":set clipboard&<CR>");
        UNSCOPED_INFO("off again by default");
        CHECK_FALSE(vim->shared().unnamedClipboard);
        keys(":set clipboard=bogus<CR>");
        {
            INFO("a value vim has not is said");
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("E474") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a click past a line's end is a click; a drag from there takes the end", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two\nthree four\n");
        click(0, 7);
        {
            INFO("past the end: normal, on the last character");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(caretText() == "0:6");
            CHECK_FALSE(e.hasSelection());
        }
        click(0, 7, 40);
        {
            INFO("slid on past the end: still a click");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(caretText() == "0:6");
            CHECK_FALSE(e.hasSelection());
        }
        drag(0, 7, 0, 4);
        UNSCOPED_INFO("dragged back from past the end is visual");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        keys("y");
        UNSCOPED_INFO("and takes the last character with it");
        CHECK(vim->registerText('"') == std::string("two"));
        drag(1, 2, 1, 10);
        UNSCOPED_INFO("dragged on past the end is visual");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        keys("y");
        UNSCOPED_INFO("to the last character");
        CHECK(vim->registerText('"') == std::string("ree four"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a drag takes both end characters whichever half the pointer is on", "[alvimkeymap]")
    {
        make("hello world\n");
        drag(0, 4, 0, 1);
        keys("y");
        UNSCOPED_INFO("back from the left of the o: the o taken");
        CHECK(vim->registerText('"') == std::string("ello"));
        drag(0, 1, 0, 4, true);
        keys("y");
        UNSCOPED_INFO("on from the right of the e: the e taken");
        CHECK(vim->registerText('"') == std::string("ello"));
        drag(0, 4, 0, 1, true);
        keys("y");
        UNSCOPED_INFO("back from the right of the o, to the right of the e");
        CHECK(vim->registerText('"') == std::string("ello"));
        click(0, 4);
        {
            INFO("a click is on its character");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(caretText() == "0:4");
        }
        S32 x, y, next_x, next_y;
        pointOf(0, 4, x, y);
        pointOf(0, 5, next_x, next_y);
        editor->handleMouseDown(next_x - 3, y, MASK_NONE);
        editor->handleHover(next_x - 3, y, MASK_NONE);
        editor->handleMouseUp(next_x - 3, y, MASK_NONE);
        {
            INFO("even on its right half");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(caretText() == "0:4");
            CHECK_FALSE(editor->hasSelection());
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: insert mode's Control keys are vim's and keep the caret on the typing", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\ndef\n");
        // Control-N and Control-P ask for completions, and the caret stays
        // where it was: on a Mac they were the text system's down and up.
        keys("A<C-n>x<C-p>y<Esc>");
        UNSCOPED_INFO("typed where the caret was");
        CHECK(flat(e.text()) == std::string("abcxy|def|"));
        // Control-A the last insert again, where it was the line's start
        // on a Mac and select-all elsewhere.
        keys("jA<C-a><Esc>");
        UNSCOPED_INFO("the last insert again");
        CHECK(flat(e.text()) == std::string("abcxy|defxy|"));
        // Control-H a backspace, where it was Replace elsewhere.
        keys("A<C-h>z<Esc>");
        UNSCOPED_INFO("backspace");
        CHECK(flat(e.text()) == std::string("abcxy|defxz|"));
        // Control-Y and Control-E: the character above and below.
        make("abc\n\nxyz\n");
        keys("ji<C-y><C-y><C-e><Esc>");
        UNSCOPED_INFO("above, above, below");
        CHECK(flat(editor->text()) == std::string("abc|abz|xyz|"));
        // Control-T and Control-D: a step in and back, the caret on its
        // character.
        ALCodeEditor& t = make("x = 1;\n");
        t.setSoftTabs(true);
        keys("A<C-t>2<Esc>");
        UNSCOPED_INFO("a step in, typing on where it was");
        CHECK(flat(t.text()) == std::string("    x = 1;2|"));
        keys("A<C-d>3<Esc>");
        UNSCOPED_INFO("and back");
        CHECK(flat(t.text()) == std::string("x = 1;23|"));
        keys("I<C-d>4<Esc>");
        UNSCOPED_INFO("nothing to take back");
        CHECK(flat(t.text()) == std::string("4x = 1;23|"));
        // Control-R and a register: its text typed in, and `.` does it again.
        ALCodeEditor& r = make("foo bar\n");
        keys("\"ayiwo<C-r>a!<Esc>");
        UNSCOPED_INFO("the register put in");
        CHECK(flat(r.text()) == std::string("foo bar|foo!|"));
        keys(".");
        UNSCOPED_INFO("and again");
        CHECK(flat(r.text()) == std::string("foo bar|foo!|foo!|"));
        // Control-J and Control-M are Return.
        keys("A<C-j>q<Esc>");
        UNSCOPED_INFO("a line broken");
        CHECK(flat(r.text()) == std::string("foo bar|foo!|foo!|q|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a count makes its text once and not past a megabyte", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abcdefghijklmnopqrstuvwxyz\n");
        // Twenty-six bytes a hundred thousand times over is past it: said,
        // and nothing put.
        keys("yiw100000p");
        {
            INFO("said: " << vim->message());
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("Too large a count") != std::string::npos);
        }
        UNSCOPED_INFO("nothing put");
        CHECK(flat(e.text()) == std::string("abcdefghijklmnopqrstuvwxyz|"));
        keys("2p");
        UNSCOPED_INFO("a count within it is put");
        CHECK(flat(e.text()) ==
                      std::string("aabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzbcdefghijklmnopqrstuvwxyz|"));
        // An insert's count likewise: what was typed stands, once.
        make("\n");
        keys("100000iabcdefghijk<Esc>");
        {
            INFO("said: " << vim->message());
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("Too large a count") != std::string::npos);
        }
        UNSCOPED_INFO("typed once");
        CHECK(flat(editor->text()) == std::string("abcdefghijk|"));
        keys("u");
        UNSCOPED_INFO("and undone as one");
        CHECK(flat(editor->text()) == std::string("|"));
        // Within it, the lot in one edit, undone at once.
        keys("20000ixy<Esc>");
        UNSCOPED_INFO("forty thousand characters");
        CHECK(editor->text().size() == size_t(40001));
        keys("u");
        UNSCOPED_INFO("undone at once");
        CHECK(flat(editor->text()) == std::string("|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: an input method composes only in insert mode and takes nothing out", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("default\n{\n    state_entry()\n    {\n    }\n}\n");
        const std::string was = e.text();
        // A letter as SDL delivers it under an input method that composes
        // every key: the composition, then the letter committed, each
        // clearing what was composed before.
        auto composed = [&](char c) {
            LLPreeditor& ime = e.preeditor();
            S32          at = 0, length = 0;
            ime.getPreeditRange(&at, &length);
            ime.resetPreedit();
            ime.updatePreedit(std::string(1, c), { 1 }, { false }, 1);
            ime.resetPreedit();
            e.handleUnicodeCharHere(static_cast<llwchar>(c));
        };
        composed('j');
        composed('V');
        composed('j');
        UNSCOPED_INFO("V j over a composition: nothing gone");
        CHECK(flat(e.text()) == flat(was));
        composed('j');
        UNSCOPED_INFO("nor on the next");
        CHECK(flat(e.text()) == flat(was));
        CHECK(vim->mode() == ALVimKeymap::Mode::VisualLine);
        UNSCOPED_INFO("over lines 2 to 4");
        CHECK(caretText().substr(0, 2) == std::string("3:"));
        keys("<Esc>");
        composed('v');
        composed('k');
        UNSCOPED_INFO("nor under v");
        CHECK(flat(e.text()) == flat(was));
        keys("<Esc>");
        // Inserting, it is typing.
        keys("ggO");
        composed('x');
        keys("<Esc>");
        UNSCOPED_INFO("composed in insert mode");
        CHECK(flat(e.text()) == std::string("x|") + flat(was));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: r with Return breaks the line once for a count; r and f take Tab", "[alvimkeymap]")
    {
        make("    one two");
        keys("0ft");
        UNSCOPED_INFO("on the word");
        CHECK(caretText() == std::string("0:8"));
        keys("h");
        keys("r<CR>");
        UNSCOPED_INFO("the space a line break, the new line indented");
        CHECK(flat(editor->text()) == std::string("    one|    two"));
        UNSCOPED_INFO("the caret on its first word");
        CHECK(caretText() == std::string("1:4"));

        make("abcdef");
        keys("l3r<CR>");
        UNSCOPED_INFO("three characters, one break");
        CHECK(flat(editor->text()) == std::string("a|ef"));

        make("a b");
        keys("lr<Tab>");
        UNSCOPED_INFO("a tab for the space");
        CHECK(editor->text() == std::string("a\tb"));
        keys("0f<Tab>");
        UNSCOPED_INFO("found by f");
        CHECK(caretText() == std::string("0:1"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: `.` and a macro type an insert's Return and Backspace again", "[alvimkeymap]")
    {
        make("x");
        keys("ofoo<CR>bar<Esc>");
        CHECK(flat(editor->text()) == std::string("x|foo|bar"));
        keys(".");
        UNSCOPED_INFO("both lines again");
        CHECK(flat(editor->text()) == std::string("x|foo|bar|foo|bar"));

        make("aaaa");
        keys("s1<CR>2<Esc>");
        CHECK(flat(editor->text()) == std::string("1|2aaa"));
        keys("l.");
        UNSCOPED_INFO("and again, the break with it");
        CHECK(flat(editor->text()) == std::string("1|21|2aa"));

        make("pq<BS>");
        keys("Ayz<BS><Esc>");
        UNSCOPED_INFO("a backspace");
        CHECK(editor->text() == std::string("pq<BS>y"));
        keys(".");
        UNSCOPED_INFO("typed again");
        CHECK(editor->text() == std::string("pq<BS>yy"));

        make("p\nq");
        keys("qaA!<CR>-<Esc>q");
        UNSCOPED_INFO("recorded as typed");
        CHECK(flat(editor->text()) == std::string("p!|-|q"));
        keys("G@a");
        UNSCOPED_INFO("played with its break");
        CHECK(flat(editor->text()) == std::string("p!|-|q!|-"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: gt gT and Ctrl-^ ask the host for :tabnext :tabprevious and :buffer", "[alvimkeymap]")
    {
        make("a\nb");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("gt3gtgT2gT<C-6>4<C-6>");
        UNSCOPED_INFO("each as its command");
        CHECK(heard == std::vector<std::string>{ "tabnext|", "tabnext|3", "tabprevious|", "tabprevious|2", "buffer|#", "buffer|4" });
        heard.clear();
        keys("dgt");
        UNSCOPED_INFO("not after an operator");
        CHECK(heard.empty());
        UNSCOPED_INFO("nothing changed");
        CHECK(flat(editor->text()) == std::string("a|b"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: Tab on the : line tells the host the word typed for a folder's files", "[alvimkeymap]")
    {
        make("x");
        std::vector<std::string> asked;
        vim->hooks().complete = [&asked](ALTextView&, const std::string& command, const std::string& typed, std::vector<std::string>& out) {
            asked.push_back(command + "|" + typed);
            if (command == "e")
            {
                out.push_back("scripts/");
                out.push_back("other/");
            }
        };
        keys(":e sc<Tab>");
        UNSCOPED_INFO("the command and the word");
        CHECK(asked == std::vector<std::string>{ "e|sc" });
        UNSCOPED_INFO("completed from what it gave");
        CHECK(vim->commandLine() == std::string("e scripts/"));
        keys("<Esc>:e scripts/fo<Tab>");
        UNSCOPED_INFO("a folder's word whole");
        CHECK(asked.back() == std::string("e|scripts/fo"));
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: jumps set '' and tell the host where they began; j l and dG do not", "[alvimkeymap]")
    {
        make("one (x)\ntwo\nthree\nfour");
        std::vector<ALTextPos> from;
        vim->hooks().jumped = [&from](ALTextView&, const ALTextPos& at) { from.push_back(at); };
        keys("G");
        UNSCOPED_INFO("G from where it was");
        REQUIRE(from == std::vector<ALTextPos>{ ALTextPos(0, 0) });
        keys("''");
        {
            INFO("'' back to it");
            CHECK(editor->caret().line == 0);
            CHECK(from.back() == ALTextPos(3, 0));
        }
        keys("jl");
        UNSCOPED_INFO("j and l are no jumps");
        CHECK(from.size() == 2);
        keys("gg");
        {
            INFO("gg");
            REQUIRE(from.size() == 3);
            CHECK(from.back() == ALTextPos(1, 1));
        }
        keys("f(%");
        {
            INFO("%");
            REQUIRE(from.size() == 4);
            CHECK(from.back() == ALTextPos(0, 4));
            CHECK(editor->caret() == ALTextPos(0, 6));
        }
        keys("/thr<CR>");
        {
            INFO("a search");
            CHECK(from.size() == 5);
            CHECK(editor->caret().line == 2);
        }
        keys("mak:4<CR>");
        {
            INFO("a line on the : line");
            CHECK(from.size() == 6);
            CHECK(editor->caret().line == 3);
        }
        keys("'a");
        {
            INFO("a mark");
            CHECK(from.size() == 7);
            CHECK(editor->caret().line == 2);
        }
        keys("dG");
        UNSCOPED_INFO("not an operator's");
        CHECK(from.size() == 7);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: Ctrl-O Ctrl-I gd Ctrl-T Ctrl-G and K ask the host as its commands", "[alvimkeymap]")
    {
        make("x y");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("<C-o>2<C-o><C-i>");
        editor->handleKeyHere(']', ALVimKeymap::CONTROL);
        keys("gd<C-t><C-g>K");
        UNSCOPED_INFO("each as the host's");
        CHECK(heard == std::vector<std::string>{ "back|", "back|", "back|", "forward|", "tag|", "tag|", "pop|", "file|", "reference|" });
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :registers and :marks list as vim lists them to the host's listing", "[alvimkeymap]")
    {
        make("one\n\ttwo three");
        std::vector<std::string> listed;
        vim->hooks().listing = [&listed](ALTextView&, const std::string& text) { listed.push_back(text); };
        keys("yyj\"ayw");
        keys(":registers<CR>");
        REQUIRE(listed.size() == size_t(1));
        UNSCOPED_INFO("the heading");
        CHECK(listed[0].compare(0, 17, "Type Name Content") == 0);
        UNSCOPED_INFO("a yank of lines");
        CHECK(listed[0].find("\n  l  \"0   one^J") != std::string::npos);
        UNSCOPED_INFO("a named one, of characters, a tab as ^I");
        CHECK(listed[0].find("\n  c  \"a   ^I") != std::string::npos);
        UNSCOPED_INFO("no : line run before it");
        CHECK(listed[0].find("\":") == std::string::npos);
        keys(":di a<CR>");
        {
            INFO("only those named");
            REQUIRE(listed.size() == size_t(2));
            CHECK(listed[1].find("\"0") == std::string::npos);
            CHECK(listed[1].find("\"a") != std::string::npos);
        }
        keys(":di :<CR>");
        UNSCOPED_INFO("the : line run before this one");
        CHECK(listed.back().find("\n  c  \":   di a") != std::string::npos);
        listed.pop_back();

        keys("mb:marks<CR>");
        REQUIRE(listed.size() == size_t(3));
        CHECK(listed[2] == std::string("mark line  col file/text\n b      2    0 two three"));
        keys("G");
        keys(":marks<CR>");
        REQUIRE(listed.size() == size_t(4));
        UNSCOPED_INFO("a jump's");
        CHECK(listed[3].find("\n '      2    0 two three") != std::string::npos);

        vim->hooks().listing = nullptr;
        keys(":reg<CR>");
        UNSCOPED_INFO("without a host, the heading said");
        CHECK(vim->message() == std::string("Type Name Content"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a host's command named with underscores is one name typed or completed", "[alvimkeymap]")
    {
        make("x");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        vim->hooks().complete = [](ALTextView&, const std::string& command, const std::string&, std::vector<std::string>& out) {
            if (command.empty())
            {
                out.push_back("go_to_line");
            }
        };
        keys(":go_to_line<CR>:save_all!<CR>");
        UNSCOPED_INFO("whole, with a bang");
        CHECK(heard == std::vector<std::string>{ "go_to_line|", "save_all!|" });
        keys(":go_<Tab>");
        UNSCOPED_INFO("completed from the underscore on");
        CHECK(vim->commandLine() == std::string("go_to_line"));
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: gf finds the quoted name on the line or else the file name at the caret", "[alvimkeymap]")
    {
        make("#include \"lib/util.lsl\"\nlocal m = require('mod') -- x\nsee docs/read_me.txt now\n   \n");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("gf");
        UNSCOPED_INFO("the include's, from before it");
        CHECK(heard.back() == std::string("find|lib/util.lsl"));
        keys("fugf");
        UNSCOPED_INFO("from inside it");
        CHECK(heard.back() == std::string("find|lib/util.lsl"));
        keys("jgf");
        UNSCOPED_INFO("a require's");
        CHECK(heard.back() == std::string("find|mod"));
        keys("j0fdgf");
        UNSCOPED_INFO("a path in the text");
        CHECK(heard.back() == std::string("find|docs/read_me.txt"));
        const size_t asked = heard.size();
        keys("jgf");
        UNSCOPED_INFO("nothing there");
        CHECK(heard.size() == asked);
        CHECK(vim->message() == std::string("E446: No file name under cursor"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: next and previous problem keys ask the host; grn grr gra the editor's", "[alvimkeymap]")
    {
        ALCodeEditor&            e = make("integer count = 1;\ncount = 2;\n");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("]d3]d[d2[dgO");
        UNSCOPED_INFO("each as the host's");
        CHECK(heard == std::vector<std::string>{ "cnext|", "cnext|3", "cprevious|", "cprevious|2", "go_to_symbol|" });
        std::vector<ALEditorCommand> asked;
        e.setSymbolRequest([&asked](ALEditorCommand command, const ALTextRange&) { asked.push_back(command); });
        keys("grngrr");
        UNSCOPED_INFO("rename and the references");
        CHECK(asked == std::vector<ALEditorCommand>{ ALEditorCommand::Rename, ALEditorCommand::FindReferences });
        e.setFixProvider([](S32, std::vector<ALCodeEditor::Fix>& out) {
            ALCodeEditor::Fix fix;
            fix.title = "Put it right";
            out.push_back(fix);
        });
        keys("gra");
        UNSCOPED_INFO("the fixes at the caret");
        CHECK(e.fixesOpen());
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: gc comments as Toggle Comment does: gcc 2gcc a motion visual; . again", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one;\ntwo;\nthree;\nfour;\n");
        keys("gcc");
        {
            INFO("the line");
            CHECK(e.document().line(0).compare(0, 2, "//") == 0);
            CHECK(e.document().line(1) == "two;");
        }
        keys("gcc");
        UNSCOPED_INFO("and back");
        CHECK(e.document().line(0) == std::string("one;"));
        keys("jgcj");
        {
            INFO("with a motion, its lines");
            CHECK(e.document().line(1).compare(0, 2, "//") == 0);
            CHECK(e.document().line(2).compare(0, 2, "//") == 0);
            CHECK(e.document().line(3) == "four;");
        }
        keys(".");
        UNSCOPED_INFO(". again");
        CHECK(flat(e.text()) == std::string("one;|two;|three;|four;|"));
        keys("gg2gcc");
        {
            INFO("a count");
            CHECK(e.document().line(1).compare(0, 2, "//") == 0);
            CHECK(e.document().line(2) == "three;");
        }
        keys("u");
        UNSCOPED_INFO("one step to undo");
        CHECK(flat(e.text()) == std::string("one;|two;|three;|four;|"));
        keys("ggVjjgc");
        {
            INFO("a visual selection's lines");
            CHECK(e.document().line(0).compare(0, 2, "//") == 0);
            CHECK(e.document().line(2).compare(0, 2, "//") == 0);
            CHECK(e.document().line(3) == "four;");
        }
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: zc zo za zM zR fold blocks; zj and zk move between them; fold ends", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("default\n{\n    state_entry()\n    {\n        x();\n    }\n"
                               "    touch_start(integer n)\n    {\n        y();\n    }\n}\n");
        keys("3G");
        keys("zc");
        CHECK(e.isFolded(2));
        keys("zo");
        CHECK_FALSE(e.isFolded(2));
        keys("za");
        UNSCOPED_INFO("za closes");
        CHECK(e.isFolded(2));
        keys("za");
        UNSCOPED_INFO("and opens");
        CHECK_FALSE(e.isFolded(2));
        keys("zM");
        UNSCOPED_INFO("every block");
        CHECK(e.isFolded(0));
        keys("zR");
        CHECK_FALSE(e.isFolded(0));
        CHECK_FALSE(e.isFolded(2));
        CHECK_FALSE(e.isFolded(6));
        keys("gg");
        keys("zj");
        UNSCOPED_INFO("zj to the next block's start");
        CHECK(e.caret().line == 2);
        keys("zj");
        UNSCOPED_INFO("and the next");
        CHECK(e.caret().line == 6);
        keys("zk");
        UNSCOPED_INFO("zk to the end of the one above");
        CHECK(e.caret().line == 5);
        keys("5G]z");
        UNSCOPED_INFO("]z the end of the block around");
        CHECK(e.caret().line == 5);
        keys("[z");
        UNSCOPED_INFO("[z its start");
        CHECK(e.caret().line == 2);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the spelling motions go round the ends; z= by count or pick; zg refuses", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("teh cat\nfine\nsecond teh\n", "text");
        keys("]s");
        UNSCOPED_INFO("no spell check: said");
        CHECK(vim->message() == std::string("E756: Spell checking is not enabled"));
        e.setSpellChecker([](const std::string& word) { return word != "teh"; },
                          [](const std::string&, std::vector<std::string>& out) {
                              out.push_back("the");
                              out.push_back("ten");
                          });
        e.setSpellCheck(true);
        keys("]s");
        UNSCOPED_INFO("the next");
        CHECK(e.caret() == ALTextPos(2, 7));
        keys("]s");
        UNSCOPED_INFO("round past the end");
        CHECK(e.caret() == ALTextPos(0, 0));
        keys("[s");
        UNSCOPED_INFO("and back past the start");
        CHECK(e.caret() == ALTextPos(2, 7));
        keys("2z=");
        UNSCOPED_INFO("by a count");
        CHECK(e.document().line(2) == std::string("second ten"));
        std::vector<std::string>            offered;
        std::function<void(size_t)>         chose;
        vim->hooks().pick = [&](ALTextView&, const std::string&, const std::vector<std::string>& items,
                                std::function<void(size_t)> chosen) {
            offered = items;
            chose   = std::move(chosen);
        };
        keys("gg");
        keys("z=");
        CHECK(offered == std::vector<std::string>{ "the", "ten" });
        chose(0);
        CHECK(e.document().line(0) == std::string("the cat"));
        keys("z=");
        UNSCOPED_INFO("a word spelled right has none");
        CHECK(vim->message() == std::string("Sorry, no suggestions"));
        keys("jzg");
        UNSCOPED_INFO("only the viewer's dictionary takes a word");
        CHECK(vim->message() == std::string("E764: Cannot add this word"));

        // The one misspelling on the caret's own line, behind it: round the
        // whole text back to it.
        e.setText("x teh y teh");
        e.setCaret(ALTextPos(0, 6));
        keys("]s");
        UNSCOPED_INFO("the next after the caret");
        CHECK(e.caret() == ALTextPos(0, 8));
        keys("]s");
        UNSCOPED_INFO("round to its own line's first");
        CHECK(e.caret() == ALTextPos(0, 2));
        keys("[s");
        UNSCOPED_INFO("and back round to its last");
        CHECK(e.caret() == ALTextPos(0, 8));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :set nohlsearch clears and leaves matches unlit; :set hlsearch again", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("x a x b x\n");
        keys("/x<CR>");
        CHECK(e.highlights().size() == size_t(3));
        keys(":set nohls<CR>");
        CHECK(e.highlights().empty());
        keys("/x<CR>");
        UNSCOPED_INFO("left unlit");
        CHECK(e.highlights().empty());
        keys(":set hlsearch<CR>n");
        UNSCOPED_INFO("lit again");
        CHECK(e.highlights().size() == size_t(3));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: gn selects the search's match for an operator; cgn then .; gN back", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foo bar foo baz foo\n");
        keys("/foo<CR>gg");
        keys("cgnqux<Esc>");
        UNSCOPED_INFO("the first changed");
        CHECK(e.document().line(0) == std::string("qux bar foo baz foo"));
        keys(".");
        UNSCOPED_INFO(". the next");
        CHECK(e.document().line(0) == std::string("qux bar qux baz foo"));
        keys("dgn");
        UNSCOPED_INFO("dgn the last");
        CHECK(e.document().line(0) == std::string("qux bar qux baz "));
        keys("u0gn");
        {
            INFO("gn: selected");
            CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
            CHECK(e.selection() == ALTextRange(ALTextPos(0, 16), ALTextPos(0, 19)));
        }
        keys("<Esc>$gN");
        UNSCOPED_INFO("gN: the one it is in, or before");
        CHECK(e.selection() == ALTextRange(ALTextPos(0, 16), ALTextPos(0, 19)));
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: gi inserts where inserting last stopped as the ^ mark moves with edits", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\ndef\n");
        keys("A!<Esc>ggyyPG");
        keys("gi?<Esc>");
        UNSCOPED_INFO("where it stopped, moved down a line");
        CHECK(flat(e.text()) == std::string("abc!|abc!?|def|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: g* matches inside words; g_ the last non-blank; gp and gP move on", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foo foobar foo\n  abc  \nx\n");
        keys("*");
        UNSCOPED_INFO("* whole words only");
        CHECK(e.caret() == ALTextPos(0, 11));
        keys("gg0g*");
        UNSCOPED_INFO("g* in foobar too");
        CHECK(e.caret() == ALTextPos(0, 4));
        keys("jg_");
        UNSCOPED_INFO("g_ the last non-blank");
        CHECK(e.caret() == ALTextPos(1, 4));
        keys("0dg_");
        UNSCOPED_INFO("and an operator's");
        CHECK(e.document().line(1) == std::string("  "));
        keys("u");
        keys("3Gyykgp");
        {
            INFO("gp: a line put, the caret on the line after it");
            CHECK(e.document().line(2) == "x");
            CHECK(e.document().line(3) == "x");
            CHECK(e.caret().line == 3);
        }
        keys("ggywgP");
        {
            INFO("gP: characters put, the caret after them");
            CHECK(e.document().line(0) == "foo foo foobar foo");
            CHECK(e.caret() == ALTextPos(0, 4));
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the enclosing-bracket motions go out by counts across lines; exclusive", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("f(a, g(b), c)\n{\n  x { y }\n  z\n}\n");
        keys("fb");
        keys("[(");
        UNSCOPED_INFO("the g's");
        CHECK(e.caret() == ALTextPos(0, 6));
        keys("fb2[(");
        UNSCOPED_INFO("two out");
        CHECK(e.caret() == ALTextPos(0, 1));
        keys("fb])");
        UNSCOPED_INFO("the close after it");
        CHECK(e.caret() == ALTextPos(0, 8));
        keys("0fad])");
        UNSCOPED_INFO("up to the close, not it");
        CHECK(e.document().line(0) == std::string("f()"));
        keys("3G0fy[{");
        UNSCOPED_INFO("an inner brace");
        CHECK(e.caret() == ALTextPos(2, 4));
        keys("j]}");
        UNSCOPED_INFO("an outer one, across lines");
        CHECK(e.caret() == ALTextPos(4, 0));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: insert Ctrl-O runs one normal command; Ctrl-V puts a key in as it is", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\n");
        keys("A<C-o>");
        {
            INFO("one command waits");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().find("-- (insert) --") != std::string::npos);
        }
        keys("0");
        UNSCOPED_INFO("then inserting");
        CHECK(vim->mode() == ALVimKeymap::Mode::Insert);
        keys("X<Esc>");
        UNSCOPED_INFO("where the command left it");
        CHECK(e.document().line(0) == std::string("Xabc"));
        keys("A<C-o>:s/a/Y/<CR>");
        {
            INFO("after a : line too");
            CHECK(vim->mode() == ALVimKeymap::Mode::Insert);
            CHECK(e.document().line(0) == "XYbc");
        }
        keys("<Esc>");

        make("\n");
        keys("i<C-v><Tab>x<C-v>u00e9<C-v>(<Esc>");
        UNSCOPED_INFO("a tab, an e by its code, a bracket alone");
        CHECK(editor->document().line(0) == std::string("\tx\xC3\xA9("));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the search line lights what is typed unless noincsearch; Escape lets go", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\nxyz\nabd\n");
        keys("/ab");
        UNSCOPED_INFO("each match lit as typed");
        CHECK(e.highlights().size() == size_t(2));
        UNSCOPED_INFO("the caret where it was");
        CHECK(e.caret() == ALTextPos(0, 0));
        keys("c");
        UNSCOPED_INFO("fewer as it narrows");
        CHECK(e.highlights().size() == size_t(1));
        keys("<Esc>");
        {
            INFO("let go of");
            CHECK(e.highlights().empty());
            CHECK(e.caret() == ALTextPos(0, 0));
        }
        keys(":set nois<CR>/ab");
        UNSCOPED_INFO("not lit where it is off");
        CHECK(e.highlights().empty());
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a search's offsets by its end its start and lines; n keeps them", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one foo two\nthree foo four\nfive foo\n");
        keys("/foo/e<CR>");
        UNSCOPED_INFO("its last character");
        CHECK(e.caret() == ALTextPos(0, 6));
        keys("n");
        UNSCOPED_INFO("n keeps it, the next match's");
        CHECK(e.caret() == ALTextPos(1, 8));
        keys("N");
        UNSCOPED_INFO("N back to the one before");
        CHECK(e.caret() == ALTextPos(0, 6));
        keys("gg/foo/s+1<CR>");
        UNSCOPED_INFO("one on from its start");
        CHECK(e.caret() == ALTextPos(0, 5));
        keys("gg/foo/e-1<CR>");
        UNSCOPED_INFO("one back from its end");
        CHECK(e.caret() == ALTextPos(0, 5));
        keys("gg/foo/+1<CR>");
        UNSCOPED_INFO("the line after it");
        CHECK(e.caret() == ALTextPos(1, 0));
        keys("n");
        UNSCOPED_INFO("n: the line after the next");
        CHECK(e.caret() == ALTextPos(2, 0));
        keys("gg//b<CR>");
        UNSCOPED_INFO("the last pattern with another offset");
        CHECK(e.caret() == ALTextPos(0, 4));
        keys("gg/foo/s-2<CR>");
        UNSCOPED_INFO("two back from its start");
        CHECK(e.caret() == ALTextPos(0, 2));
        keys("n");
        UNSCOPED_INFO("n from the match, not from before it");
        CHECK(e.caret() == ALTextPos(1, 4));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :join :retab :undo :redo :put :mark :k :left :right and :center", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\n  b\n  c\nd\n");
        keys(":j<CR>");
        UNSCOPED_INFO(":j the line and the next");
        CHECK(flat(e.text()) == std::string("a b|  c|d|"));
        keys(":u<CR>");
        CHECK(flat(e.text()) == std::string("a|  b|  c|d|"));
        keys(":red<CR>");
        CHECK(flat(e.text()) == std::string("a b|  c|d|"));
        keys(":u<CR>:1,3j!<CR>");
        UNSCOPED_INFO(":j! a range, the blanks kept");
        CHECK(flat(e.text()) == std::string("a  b  c|d|"));
        keys("u:2j 3<CR>");
        UNSCOPED_INFO(":j with a count, from the range's last");
        CHECK(flat(e.text()) == std::string("a|  b c d|"));
        keys("u");

        e.setSoftTabs(false);
        e.setTabWidth(2);
        keys(":retab<CR>");
        UNSCOPED_INFO(":retab: tabs where the tabs are set so");
        CHECK(flat(e.text()) == std::string("a|\tb|\tc|d|"));
        keys(":retab 4<CR>");
        UNSCOPED_INFO("a width given sets it first");
        CHECK(flat(e.text()) == std::string("a|  b|  c|d|"));
        keys("u");

        keys("gg\"xyy:3pu x<CR>");
        UNSCOPED_INFO(":put under the line");
        CHECK(flat(e.text()) == std::string("a|\tb|\tc|a|d|"));
        keys(":1pu! x<CR>");
        UNSCOPED_INFO(":put! above it");
        CHECK(flat(e.text()) == std::string("a|a|\tb|\tc|a|d|"));

        keys(":4ma q<CR>gg'q");
        UNSCOPED_INFO(":mark");
        CHECK(e.caret().line == 3);
        keys(":2k w<CR>gg'w");
        CHECK(e.caret().line == 1);

        make("x\n   yy\n");
        keys(":%le 2<CR>");
        UNSCOPED_INFO(":left");
        CHECK(flat(editor->text()) == std::string("  x|  yy|"));
        keys(":2ri 6<CR>");
        UNSCOPED_INFO(":right");
        CHECK(editor->document().line(1) == std::string("    yy"));
        keys(":2ce 6<CR>");
        UNSCOPED_INFO(":center");
        CHECK(editor->document().line(1) == std::string("  yy"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: g; and its reverse walk the change list; '. the last change; :changes", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd\n");
        keys("g;");
        UNSCOPED_INFO("none yet");
        CHECK(vim->message() == std::string("E664: Changelist is empty"));
        keys("jA!<Esc>3GA?<Esc>gg");
        keys("g;");
        UNSCOPED_INFO("the newest");
        CHECK(e.caret().line == 2);
        keys("g;");
        UNSCOPED_INFO("the one before");
        CHECK(e.caret().line == 1);
        keys("g;");
        UNSCOPED_INFO("no older");
        CHECK(vim->message() == std::string("E662: At start of changelist"));
        keys("g,");
        UNSCOPED_INFO("newer again");
        CHECK(e.caret().line == 2);
        keys("gg'.");
        UNSCOPED_INFO("'. the last change's line");
        CHECK(e.caret().line == 2);
        std::vector<std::string> listed;
        vim->hooks().listing = [&listed](ALTextView&, const std::string& text) { listed.push_back(text); };
        keys(":changes<CR>");
        {
            INFO("listed");
            REQUIRE(listed.size() == 1);
            CHECK(listed[0].compare(0, 21, "change line  col text") == 0);
            CHECK(listed[0].find("\n>    0     3    1 c?") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: section motions go to the host's function starts and ends; af and if", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("integer f(integer x)\n{\n    return x;\n}\ndefault\n{\n    state_entry()\n    {\n        f(1);\n    }\n"
                               "    touch_start(integer n)\n    {\n        f(2);\n    }\n}\n");
        e.setFunctionProvider([](std::vector<ALTextRange>& out) {
            out = { ALTextRange(ALTextPos(10, 4), ALTextPos(13, 5)), ALTextRange(ALTextPos(0, 0), ALTextPos(3, 1)),
                    ALTextRange(ALTextPos(6, 4), ALTextPos(9, 5)) };
        });
        keys("3G]]");
        UNSCOPED_INFO("]] the next's start");
        CHECK(e.caret() == ALTextPos(6, 4));
        keys("]m");
        UNSCOPED_INFO("]m the same");
        CHECK(e.caret() == ALTextPos(10, 4));
        keys("[[");
        UNSCOPED_INFO("[[ back");
        CHECK(e.caret() == ALTextPos(6, 4));
        keys("2[m");
        UNSCOPED_INFO("2[m two back");
        CHECK(e.caret() == ALTextPos(0, 0));
        keys("][");
        UNSCOPED_INFO("][ its end");
        CHECK(e.caret() == ALTextPos(3, 0));
        keys("][");
        UNSCOPED_INFO("and the next's");
        CHECK(e.caret() == ALTextPos(9, 4));
        keys("[]");
        UNSCOPED_INFO("[] back");
        CHECK(e.caret() == ALTextPos(3, 0));
        keys("''");
        UNSCOPED_INFO("a jump, as vim's are");
        CHECK(e.caret() == ALTextPos(9, 4));

        keys("9Gdaf");
        UNSCOPED_INFO("daf its lines");
        CHECK(flat(e.text()) ==
                      std::string("integer f(integer x)|{|    return x;|}|default|{|"
                                  "    touch_start(integer n)|    {|        f(2);|    }|}|"));
        keys("u9Gdif");
        UNSCOPED_INFO("dif its body, not the brace under its header");
        CHECK(flat(e.text()) ==
                      std::string("integer f(integer x)|{|    return x;|}|default|{|    state_entry()|    {|    }|"
                                  "    touch_start(integer n)|    {|        f(2);|    }|}|"));
        keys("u9Gvaf");
        UNSCOPED_INFO("vaf selects its lines");
        CHECK(vim->mode() == ALVimKeymap::Mode::VisualLine);
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: mappings: a mapping's start is held until it fails or times out", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\n");
        ex("inoremap jk <Esc>");
        keys("ijk");
        UNSCOPED_INFO("jk left insert mode");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        UNSCOPED_INFO("nothing typed");
        CHECK(flat(e.text()) == std::string("abc|"));
        keys("ijx");
        UNSCOPED_INFO("the j typed after all, then the x");
        CHECK(flat(e.text()) == std::string("jxabc|"));
        keys("<Esc>");
        ex("set timeoutlen=0");
        keys("Aj");
        UNSCOPED_INFO("held, and shown after the mode");
        CHECK(vim->status() == std::string("-- INSERT -- j"));
        UNSCOPED_INFO("not typed yet");
        CHECK(flat(e.text()) == std::string("jxabc|"));
        vim->idle(e);
        UNSCOPED_INFO("typed once the time ran out");
        CHECK(flat(e.text()) == std::string("jxabcj|"));
        UNSCOPED_INFO("held no more");
        CHECK(vim->status() == std::string("-- INSERT --"));
        keys("<Esc>");
        ex("set notimeout");
        keys("Aj");
        vim->idle(e);
        UNSCOPED_INFO("without the timeout it waits");
        CHECK(flat(e.text()) == std::string("jxabcj|"));
        keys("k");
        {
            INFO("and the rest makes the mapping");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(flat(e.text()) == "jxabcj|");
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: mappings: map remaps and noremap does not; the leader; modes; a loop", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two\nthree\nfour\n");
        ex("nmap Q j");
        ex("nmap W Q");
        ex("nnoremap E W");
        keys("W");
        UNSCOPED_INFO("W through Q to j");
        CHECK(caretText() == std::string("1:0"));
        keys("ggE");
        UNSCOPED_INFO("E is W itself, a WORD on");
        CHECK(caretText() == std::string("0:4"));
        ex("let mapleader = ','");
        ex("nnoremap <leader>d dd");
        keys(",");
        UNSCOPED_INFO("the leader held and shown");
        CHECK(vim->status() == std::string(","));
        keys("d");
        UNSCOPED_INFO("<leader>d is dd");
        CHECK(flat(e.text()) == std::string("three|four|"));
        keys("u");
        UNSCOPED_INFO("undone as one");
        CHECK(flat(e.text()) == std::string("one two|three|four|"));
        keys("gg,x");
        UNSCOPED_INFO("a leader nothing follows goes as it is: , then x");
        CHECK(flat(e.text()) == std::string("ne two|three|four|"));
        ex("onoremap w iw");
        keys("jldw");
        UNSCOPED_INFO("dw, with w the inner word after an operator");
        CHECK(flat(e.text()) == std::string("ne two||four|"));
        ex("cnoremap <C-x> set ic");
        keys(":<C-x>");
        std::string line;
        S32         caret = 0;
        {
            INFO("the : line's own");
            CHECK(vim->typingLine(line, caret));
            CHECK(line == ":set ic");
        }
        keys("<CR>");
        UNSCOPED_INFO("and it ran");
        CHECK(vim->shared().ignoreCase);
        ex("nmap K Kj");
        keys("ggK");
        {
            INFO("what starts with its own keys does not map the first again");
            CHECK(caretText() == "1:0");
            CHECK_FALSE(vim->messageIsError());
        }
        ex("nmap t dd");
        keys("ggft");
        {
            INFO("the character f waits for is taken as it is");
            CHECK(flat(e.text()) == "ne two||four|");
            CHECK(caretText() == "0:3");
        }
        ex("nmap a b");
        ex("nmap b a");
        keys("a");
        {
            INFO("round and round is stopped");
            CHECK(vim->message().find("E223") != std::string::npos);
            CHECK(vim->messageIsError());
        }
        ex("nunmap zz");
        UNSCOPED_INFO("no such mapping");
        CHECK(vim->message().find("E31") != std::string::npos);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: mappings: macros replay through mappings; :normal maps and :normal! not", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd\n");
        ex("inoremap jk <Esc>");
        keys("qaAxjkq");
        CHECK(flat(e.text()) == std::string("ax|b|c|d|"));
        UNSCOPED_INFO("the keys as they were typed");
        CHECK(vim->registerText('a') == std::string("Axjk"));
        keys("j@a");
        {
            INFO("played through the mapping");
            CHECK(flat(e.text()) == "ax|bx|c|d|");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        }
        ex("nnoremap Q dd");
        ex("3normal Q");
        UNSCOPED_INFO(":normal maps");
        CHECK(flat(e.text()) == std::string("ax|bx|d|"));
        ex("1normal! Q");
        UNSCOPED_INFO(":normal! does not");
        CHECK(flat(e.text()) == std::string("ax|bx|d|"));
        keys("ggQ.");
        UNSCOPED_INFO(". does what the mapping did");
        CHECK(flat(e.text()) == std::string("d|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :set takes several options and queries and toggles them; view options", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("x\n");
        ex("set ic scs tm=300");
        {
            INFO("all three");
            CHECK(vim->shared().ignoreCase);
            CHECK(vim->shared().smartCase);
            CHECK(vim->shared().timeoutLength == 300);
        }
        ex("set ic? tm?");
        CHECK(vim->message() == std::string("  ignorecase    timeoutlen=300"));
        ex("set invic scs!");
        {
            INFO("toggled");
            CHECK_FALSE(vim->shared().ignoreCase);
            CHECK_FALSE(vim->shared().smartCase);
        }
        ex("set tm=x");
        {
            INFO("a number wanted");
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("E474") != std::string::npos);
        }
        ex("set clipboard+=unnamed");
        UNSCOPED_INFO("added to");
        CHECK(vim->shared().unnamedClipboard);
        ex("set clipboard-=unnamed");
        UNSCOPED_INFO("taken from");
        CHECK_FALSE(vim->shared().unnamedClipboard);
        std::string shown;
        std::string error;
        {
            INFO("tabstop");
            CHECK(ALVimKeymap::setViewOption(e, "ts=2", shown, error));
            CHECK(e.getTabWidth() == 2);
            CHECK(error.empty());
        }
        {
            INFO("shiftwidth shown");
            CHECK(ALVimKeymap::setViewOption(e, "sw?", shown, error));
            CHECK(shown == "  shiftwidth=2");
        }
        {
            INFO("expandtab");
            CHECK(ALVimKeymap::setViewOption(e, "noet", shown, error));
            CHECK_FALSE(e.getSoftTabs());
        }
        {
            INFO("out of reach");
            CHECK(ALVimKeymap::setViewOption(e, "ts=99", shown, error));
            CHECK_FALSE(error.empty());
            CHECK(e.getTabWidth() == 2);
        }
        UNSCOPED_INFO("not a view's");
        CHECK_FALSE(ALVimKeymap::setViewOption(e, "ic", shown, error));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a vimrc sets maps and options; offers the host the rest; errors by line", "[alvimkeymap]")
    {
        ALVimKeymap::Shared      shared;
        std::vector<std::string> errors;
        std::vector<std::string> offered;
        const auto               host = [&offered](const std::string& option) {
            offered.push_back(option);
            return option == "nu";
        };
        const char* vimrc = "\" a comment\n"
                            "set ignorecase smartcase ts=2 et\n"
                            "let mapleader = \"\\<Space>\"\n"
                            "nnoremap <leader>w :w<CR>\n"
                            "inoremap jk <Esc>\n"
                            "  :set nu\n"
                            "syntax on\n"
                            "silent! colorscheme desert\n"
                            "set tm=300 notimeout\n"
                            "\n"
                            "set foo\n"
                            "noremap Y\n"
                            "   \\ y$\n";
        ALVimKeymap::source(shared, vimrc, host, errors);
        {
            INFO("its options");
            CHECK(shared.ignoreCase);
            CHECK(shared.smartCase);
            CHECK(shared.timeoutLength == 300);
            CHECK_FALSE(shared.timeout);
        }
        UNSCOPED_INFO("each editor's for the host to set");
        CHECK(shared.viewOptions == std::vector<std::string>{ "ts=2", "et" });
        {
            INFO("the host offered them first, and its own");
            REQUIRE(offered.size() == 4);
            CHECK(offered[2] == "nu");
            CHECK(offered[3] == "foo");
        }
        const ALVimMappings& maps = shared.mappings;
        UNSCOPED_INFO("<leader>w with the leader a space");
        CHECK(maps.match(ALVimMappings::NORMAL, maps.keysOf("<Space>w"), true).full != nullptr);
        CHECK(maps.match(ALVimMappings::INSERT, maps.keysOf("jk"), true).full != nullptr);
        const ALVimMappings::Match y = maps.match(ALVimMappings::VISUAL, maps.keysOf("Y"), true);
        {
            INFO("a line that goes on with a backslash");
            REQUIRE(y.full);
            CHECK(ALVimMappings::shown(y.full->to) == "y$");
        }
        UNSCOPED_INFO("two not understood");
        REQUIRE(errors.size() == size_t(2));
        {
            INFO("by their lines");
            CHECK(errors[0].find("line 7:") == 0);
            CHECK(errors[0].find("E492") != std::string::npos);
            CHECK(errors[1].find("line 11:") == 0);
            CHECK(errors[1].find("E518") != std::string::npos);
        }

        std::string listing;
        std::string error;
        shared.mappings.command("nnoremap", "Q gq", false, listing, error);
        shared.command.push_back("s/a/b/");
        errors.clear();
        ALVimKeymap::source(shared, "nnoremap Z zz\n", host, errors);
        {
            INFO("read again, what it made before is gone");
            CHECK(maps.match(ALVimMappings::INSERT, maps.keysOf("jk"), true).full == nullptr);
            CHECK(maps.match(ALVimMappings::NORMAL, maps.keysOf("Z"), true).full != nullptr);
        }
        UNSCOPED_INFO("and the leader vim's");
        CHECK(maps.leader() == "\\");
        UNSCOPED_INFO("one typed kept");
        CHECK(maps.match(ALVimMappings::NORMAL, maps.keysOf("Q"), true).full != nullptr);
        UNSCOPED_INFO("and the view's options it no longer sets");
        CHECK(shared.viewOptions.empty());
        const ALVimKeymap::Shared vims;
        {
            INFO("nor the mode's: those are vim's own again");
            CHECK(shared.ignoreCase == vims.ignoreCase);
            CHECK(shared.smartCase == vims.smartCase);
            CHECK(shared.timeout == vims.timeout);
            CHECK(shared.timeoutLength == vims.timeoutLength);
        }
        UNSCOPED_INFO("the lines typed kept");
        CHECK(shared.command == std::vector<std::string>{ "s/a/b/" });
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: surround: ys puts a pair round a motion; ds takes it; cs changes it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("say hello world\n");
        keys("wysiw)");
        CHECK(flat(e.text()) == std::string("say (hello) world|"));
        UNSCOPED_INFO("the caret on the pair's start");
        CHECK(caretText() == std::string("0:4"));
        keys("ds)");
        CHECK(flat(e.text()) == std::string("say hello world|"));
        keys("ysiw(");
        UNSCOPED_INFO("ysiw( has spaces inside");
        CHECK(flat(e.text()) == std::string("say ( hello ) world|"));
        keys("ds(");
        UNSCOPED_INFO("ds( takes them too");
        CHECK(flat(e.text()) == std::string("say hello world|"));
        keys("ysiw\"");
        keys("cs\"'");
        CHECK(flat(e.text()) == std::string("say 'hello' world|"));
        keys("$ysiw]");
        UNSCOPED_INFO("ysiw] on the last word");
        CHECK(flat(e.text()) == std::string("say 'hello' [world]|"));
        keys("0.");
        UNSCOPED_INFO(". does it again here");
        CHECK(flat(e.text()) == std::string("[say] 'hello' [world]|"));
        keys("ysiw");
        UNSCOPED_INFO("waiting for the character, said");
        CHECK(vim->status() == std::string("ys"));
        keys("<Esc>");
        {
            INFO("Escape lets it go");
            CHECK(flat(e.text()) == "[say] 'hello' [world]|");
            CHECK(vim->status().empty());
        }
        keys("0x.");
        UNSCOPED_INFO("and the next command is one of its own, for . too");
        CHECK(flat(e.text()) == std::string("ay] 'hello' [world]|"));
        keys("ysiwx");
        UNSCOPED_INFO("a letter is no pair");
        CHECK(flat(e.text()) == std::string("ay] 'hello' [world]|"));
        keys("uuu");
        UNSCOPED_INFO("each undone as one step");
        CHECK(flat(e.text()) == std::string("say 'hello' [world]|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: surround: counts marks and tags taken off; yss the line; visual S", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("((a) b)\n*bold* and <b>x</b>\n  line text  \none\ntwo\n");
        keys("ll2ds)");
        UNSCOPED_INFO("2ds) the outer pair");
        CHECK(flat(e.text()) == std::string("(a) b|*bold* and <b>x</b>|  line text  |one|two|"));
        keys("jlds*");
        UNSCOPED_INFO("ds* the marks either side");
        CHECK(flat(e.text()) == std::string("(a) b|bold and <b>x</b>|  line text  |one|two|"));
        keys("$Fxdst");
        UNSCOPED_INFO("dst the tags");
        CHECK(flat(e.text()) == std::string("(a) b|bold and x|  line text  |one|two|"));
        keys("3Gyss)");
        UNSCOPED_INFO("yss the line's text, its blanks outside");
        CHECK(flat(e.text()) == std::string("(a) b|bold and x|  (line text)  |one|two|"));
        keys("4GVjS{");
        UNSCOPED_INFO("visual S round lines, on lines of their own");
        CHECK(flat(e.text()) ==
                      std::string("(a) b|bold and x|  (line text)  |{|one|two|}|"));
        keys("2Gcsbx");
        UNSCOPED_INFO("no such pair: nothing");
        CHECK(flat(e.text()) == std::string("(a) b|bold and x|  (line text)  |{|one|two|}|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: every way into insert mode is one undo step closed by escape", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two\nthree four\nfive six\n");
        for (const char* seq : { "ix<Esc>", "ay<Esc>", "Iz<Esc>", "Aw<Esc>", "onew<Esc>", "Oup<Esc>", "cwX<Esc>", "ccY<Esc>", "CZ<Esc>",
                                 "sQ<Esc>", "SR<Esc>", "Rab<Esc>", "giT<Esc>", "vlcU<Esc>", "3ihi<Esc>" })
        {
            const std::string before = flat(e.text());
            keys(seq);
            UNSCOPED_INFO(seq << ": none left open");
            REQUIRE_FALSE(e.undoJournal().inGroup());
            keys("u");
            UNSCOPED_INFO(seq << ": one step");
            REQUIRE(flat(e.text()) == before);
            keys(seq);
        }
        const std::string before_x = flat(e.text());
        keys("x");
        keys("u");
        UNSCOPED_INFO("a command after is a step of its own");
        CHECK(flat(e.text()) == before_x);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a mapping that feeds itself ends where a key fails or at E223", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd\n");
        ex("nmap j jj");
        keys("j");
        UNSCOPED_INFO("down until j fails on the last line, and back");
        CHECK(caretText() == std::string("4:0"));
        UNSCOPED_INFO("which is no error");
        CHECK_FALSE(vim->messageIsError());
        // One that never fails: an A put in, and itself again.
        ex("nmap Q iA<Esc>Q");
        keys("ggQ");
        {
            INFO("stopped: " << vim->message());
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("E223") != std::string::npos);
        }
        UNSCOPED_INFO("having done what it did meanwhile");
        CHECK(e.document().line(0).size() > 100);
        // Stopped wherever the mapping was -- here in insert mode, as vim
        // leaves it -- and a key typed after goes as typed.
        keys("<Esc>");
        UNSCOPED_INFO("vim takes keys again");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a counted macro stops at the first play whose f fails as one undo step", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a,b\nc,d\ne f\ng,h\n");
        keys("qa0f,xxjq");
        UNSCOPED_INFO("recorded, done once");
        CHECK(flat(e.text()) == std::string("a|c,d|e f|g,h|"));
        keys("100@a");
        UNSCOPED_INFO("the second play's f found no comma: it and the rest stopped");
        CHECK(flat(e.text()) == std::string("a|c|e f|g,h|"));
        keys("u");
        UNSCOPED_INFO("undone as one step");
        CHECK(flat(e.text()) == std::string("a|c,d|e f|g,h|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a macro of two hundred commands and :normal over a range are one undo", "[alvimkeymap]")
    {
        ALCodeEditor& e     = make((std::string(250, 'x') + "\none\ntwo\nthree\n").c_str());
        std::string   macro = "qa";
        for (int i = 0; i < 200; ++i)
        {
            macro += i % 2 ? "x" : "~";
        }
        macro += "q";
        keys(macro.c_str());
        const std::string recorded = e.text();
        keys("0@a");
        CHECK(e.text() != recorded);
        keys("u");
        UNSCOPED_INFO("one step back");
        CHECK(e.text() == recorded);

        ex("2,4normal Ax");
        UNSCOPED_INFO("each line");
        CHECK(flat(e.text()).substr(recorded.find('\n')) == std::string("|onex|twox|threex|"));
        keys("u");
        UNSCOPED_INFO("one step back");
        CHECK(e.text() == recorded);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a line picked from q:'s window after vim or the tab went does nothing", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one\n");
        keys(":set number<CR>");
        std::function<void(const std::string&, bool)> later;
        const auto hold = [&later](ALTextView&, llwchar, const std::vector<std::string>&, std::function<void(const std::string&, bool)> chosen) {
            later = std::move(chosen);
        };
        vim->hooks().historyWindow = hold;
        keys("q:");
        UNSCOPED_INFO("the window holds the pick");
        REQUIRE(static_cast<bool>(later));
        // The : history outlives the vim it was typed into: a line run
        // would be the last on it.
        const std::shared_ptr<ALVimKeymap::Shared> vim_shared = vim->sharedState();
        e.setModalKeymap(nullptr);
        vim = nullptr;
        later("set nonumber", true);
        UNSCOPED_INFO("vim gone: nothing put back");
        CHECK_FALSE(e.modalKeymap());
        UNSCOPED_INFO("and nothing run");
        CHECK(vim_shared->command == std::vector<std::string>{ "set number" });

        auto keymap = std::make_unique<ALVimKeymap>();
        vim         = keymap.get();
        e.setModalKeymap(std::move(keymap));
        vim->hooks().historyWindow = hold;
        later                      = nullptr;
        keys(":set number<CR>q:");
        UNSCOPED_INFO("held again");
        REQUIRE(static_cast<bool>(later));
        const std::shared_ptr<ALVimKeymap::Shared> tab_shared = vim->sharedState();
        editor->die();
        editor = nullptr;
        LLMortician::updateClass();
        later("set nonumber", true);
        UNSCOPED_INFO("the tab gone: nothing run");
        CHECK(tab_shared->command == std::vector<std::string>{ "set number" });
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: vim's Control motions ^H ^J ^N ^P ^M and ^L; Tab forward in the jumps", "[alvimkeymap]")
    {
        ALCodeEditor&            e = make("one\n  two\nthree\nfour\n");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("gg<C-N>");
        UNSCOPED_INFO("^N down");
        CHECK(e.caret().line == 1);
        keys("<C-J>");
        UNSCOPED_INFO("^J down");
        CHECK(e.caret().line == 2);
        keys("<C-P>");
        UNSCOPED_INFO("^P up");
        CHECK(e.caret().line == 1);
        keys("$<C-H>");
        UNSCOPED_INFO("^H left");
        CHECK(caretText() == std::string("1:3"));
        keys("gg<C-M>");
        UNSCOPED_INFO("^M to the next line's first character");
        CHECK(caretText() == std::string("1:2"));
        keys("<C-L>");
        keys("<Tab>");
        {
            INFO("Tab forward in the jump list");
            REQUIRE(heard.size() == 1);
            CHECK(heard[0] == "forward|");
        }
        UNSCOPED_INFO("and nothing indented, nothing changed");
        CHECK(flat(e.text()) == std::string("one|  two|three|four|"));
        keys("ggd<C-N>");
        UNSCOPED_INFO("an operator takes them as it takes j");
        CHECK(flat(e.text()) == std::string("three|four|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: ^W commands are the host's tabs; ^W^W as ^Ww; no-split ones fail", "[alvimkeymap]")
    {
        ALCodeEditor&            e = make("a\n");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("<C-W>q<C-W><C-W><C-W>W<C-W>o<C-W>n<C-W>p<C-W>c");
        UNSCOPED_INFO("each the host's");
        CHECK(heard == std::vector<std::string>({ "quit|", "tabnext|", "tabprevious|", "tabonly|", "tabnew|", "buffer|#", "close|" }));
        heard.clear();
        keys("<C-W>x");
        UNSCOPED_INFO("a split there is none of: nothing asked");
        CHECK(heard.empty());
        UNSCOPED_INFO("and x not deleting");
        CHECK(flat(e.text()) == "a|");
        keys("<C-W><Esc>x");
        {
            INFO("Escape lets it go; x is x again");
            CHECK(flat(e.text()) == "|");
            CHECK(heard.empty());
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: an unused Control letter is taken on the Mac and passed on elsewhere", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two\n");
        keys("w");
#if LL_DARWIN
        CHECK(e.handleKeyHere('K', ALVimKeymap::CONTROL));
        UNSCOPED_INFO("and the editor's ^K, to the line's end, not done");
        CHECK(flat(e.text()) == std::string("one two|"));
        UNSCOPED_INFO("^S too");
        CHECK(e.handleKeyHere('S', ALVimKeymap::CONTROL));
#else
        UNSCOPED_INFO("^S on to the window, which saves");
        CHECK_FALSE(e.handleKeyHere('S', ALVimKeymap::CONTROL));
        UNSCOPED_INFO("^K on to it, before a second key");
        CHECK_FALSE(e.handleKeyHere('K', ALVimKeymap::CONTROL));
        UNSCOPED_INFO("nothing changed");
        CHECK(flat(e.text()) == std::string("one two|"));
#endif
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: vim's highlights are apart: :noh and Escape leave the references lit", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("count count\ncount\n");
        typedef ALCodeEditor::Highlight H;
        e.setHighlights(H::References, { ALTextRange(ALTextPos(1, 0), ALTextPos(1, 5)) });
        keys(":set hls<CR>");
        keys("/count<CR>");
        UNSCOPED_INFO("the search lit");
        CHECK_FALSE(e.highlights(H::Search).empty());
        keys(":noh<CR>");
        CHECK(e.highlights(H::Search).empty());
        UNSCOPED_INFO("the references kept");
        CHECK(e.highlights(H::References).size() == 1);
        keys("gg<C-V>jl");
        UNSCOPED_INFO("the block lit");
        CHECK_FALSE(e.highlights(H::Block).empty());
        keys("<Esc>");
        CHECK(e.highlights(H::Block).empty());
        UNSCOPED_INFO("the references kept still");
        CHECK(e.highlights(H::References).size() == 1);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :g visits lines top down; d s > < under it and . @ maps are one edit", "[alvimkeymap]")
    {
        ALCodeEditor& e     = make("a\nb\nc\nd\n");
        S32           edits = 0;
        boost::signals2::scoped_connection counting = e.document().onChanged([&edits](const ALTextDocument::Edit&) { ++edits; });
        keys(":g/^/m0<CR>");
        UNSCOPED_INFO("turned over, each line moved to the top in turn");
        CHECK(flat(e.text()) == std::string("d|c|b|a|"));
        keys("u");
        UNSCOPED_INFO("one step to undo");
        CHECK(flat(e.text()) == std::string("a|b|c|d|"));
        keys(":g/^/j<CR>");
        UNSCOPED_INFO("joined in pairs: a line joined onto the one above is not visited");
        CHECK(flat(e.text()) == std::string("a b|c d|"));
        keys(":g/^/t.<CR>");
        UNSCOPED_INFO("each copied below itself once, the copy not visited");
        CHECK(flat(e.text()) == std::string("a b|a b|c d|c d|"));

        e.setText("x 1\ny\nx 2\nx 3\n");
        edits = 0;
        keys(":g/x/d<CR>");
        UNSCOPED_INFO("the lines taken out");
        CHECK(flat(e.text()) == std::string("y|"));
        UNSCOPED_INFO("in one edit");
        CHECK(edits == 1);
        UNSCOPED_INFO("the register holds the last, as one at a time leaves it");
        CHECK(vim->registerText('"') == std::string("x 3"));
        UNSCOPED_INFO("the one before it in 2");
        CHECK(vim->registerText('2') == std::string("x 2"));
        UNSCOPED_INFO("said once for the lot");
        CHECK(vim->message() == std::string("3 fewer lines"));
        UNSCOPED_INFO("the caret where the last went");
        CHECK(e.caret().line == 1);
        keys("u");
        UNSCOPED_INFO("one step to undo");
        CHECK(flat(e.text()) == std::string("x 1|y|x 2|x 3|"));

        e.setText("a\nb\nc");
        keys(":g/^/d<CR>");
        UNSCOPED_INFO("every line, the last with the break before it");
        CHECK(flat(e.text()) == std::string(""));

        e.setText("x a a\ny a\nx a\n");
        edits = 0;
        keys(":g/x/s/a/b/g<CR>");
        CHECK(flat(e.text()) == std::string("x b b|y a|x b|"));
        UNSCOPED_INFO("in one edit");
        CHECK(edits == 1);
        UNSCOPED_INFO("said once for the lot");
        CHECK(vim->message() == std::string("3 substitutions on 2 lines"));
        UNSCOPED_INFO("the caret on the last line substituted on");
        CHECK(e.caret().line == 2);
        edits = 0;
        keys(":g/x/s/b/1\\r2/<CR>");
        UNSCOPED_INFO("a break put in on each");
        CHECK(flat(e.text()) == std::string("x 1|2 b|y a|x 1|2|"));
        UNSCOPED_INFO("the caret on the last line of the last put in");
        CHECK(e.caret().line == 4);

        e.setText("x\ny\nx\n");
        e.setSoftTabs(true);
        e.setTabWidth(2);
        edits = 0;
        keys(":g/x/><CR>");
        UNSCOPED_INFO("shifted in");
        CHECK(flat(e.text()) == std::string("  x|y|  x|"));
        UNSCOPED_INFO("in one edit");
        CHECK(edits == 1);
        edits = 0;
        keys(":g/x/<<CR>");
        UNSCOPED_INFO("and out");
        CHECK(flat(e.text()) == std::string("x|y|x|"));
        UNSCOPED_INFO("in one edit");
        CHECK(edits == 1);

        e.setText("a a a\n");
        keys("gg:s/a/b/gc<CR>");
        edits = 0;
        keys("ya");
        UNSCOPED_INFO("the rest said yes to at once");
        CHECK(flat(e.text()) == std::string("b b b|"));
        UNSCOPED_INFO("the first, then the rest in one edit");
        CHECK(edits == 2);
        CHECK(vim->message() == std::string("3 substitutions on 1 line"));

        e.setText("one\ntwo\nthree\n");
        keys("ggAhello<Esc>j");
        edits = 0;
        keys(".");
        UNSCOPED_INFO("typed again");
        CHECK(flat(e.text()) == std::string("onehello|twohello|three|"));
        UNSCOPED_INFO("in one edit");
        CHECK(edits == 1);
        keys("ggqaAxyz<Esc>q");
        edits = 0;
        keys("jj@a");
        CHECK(flat(e.text()) == std::string("onehelloxyz|twohello|threexyz|"));
        UNSCOPED_INFO("in one edit");
        CHECK(edits == 1);
        ex("inoremap ;h hi there");
        edits = 0;
        keys("A;h<Esc>");
        CHECK(flat(e.text()) == std::string("onehelloxyz|twohello|threexyzhi there|"));
        UNSCOPED_INFO("in one edit");
        CHECK(edits == 1);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: bracket objects and motions skip brackets in strings as % does", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("f(\"(\", x) // )\n");
        e.setCaret(ALTextPos(0, 7));
        keys("di(");
        UNSCOPED_INFO("inside the call, not the string's bracket");
        CHECK(flat(e.text()) == std::string("f() // )|"));
        e.setText("g(a, \"(\", b)\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        e.setCaret(ALTextPos(0, 10));
        keys("[(");
        UNSCOPED_INFO("back to the call's bracket");
        CHECK(e.caret() == ALTextPos(0, 1));
        keys("%");
        UNSCOPED_INFO("and % to its partner");
        CHECK(e.caret() == ALTextPos(0, 11));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a line number typed is the one in the gutter; :g finds its own lines", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one\ntwo\nthree\nfour\n");
        e.setLineNumberBase(10);
        keys("12G");
        UNSCOPED_INFO("12G the second line");
        CHECK(e.caret().line == 1);
        keys("11gg");
        UNSCOPED_INFO("11gg the first");
        CHECK(e.caret().line == 0);
        ex("13");
        UNSCOPED_INFO(":13 the third");
        CHECK(e.caret().line == 2);
        ex("2");
        UNSCOPED_INFO("a number before the first, the first");
        CHECK(e.caret().line == 0);
        ex("g/o/d");
        UNSCOPED_INFO(":g deletes the lines it matched, not those the numbers would miss");
        CHECK(flat(e.text()) == std::string("three|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: gI inserts in the first column before the indent and . does it again", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("    one\n    two\n");
        e.setCaret(ALTextPos(0, 6));
        keys("gIx<Esc>");
        UNSCOPED_INFO("before the indent");
        CHECK(flat(e.text()) == std::string("x    one|    two|"));
        keys("j.");
        UNSCOPED_INFO("again on the next line");
        CHECK(flat(e.text()) == std::string("x    one|x    two|"));
        keys("0wgI");
        CHECK(vim->inserting());
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: every change of what the band shows moves the generation", "[alvimkeymap]")
    {
        make("one\ntwo\n");
        U32 was = vim->generation();
        const auto require_moved = [&](const std::string& what) {
            INFO(what);
            REQUIRE(vim->generation() != was);
            was = vim->generation();
        };
        keys(":");
        require_moved("the line opened");
        for (const char* key : { "s", "/", "o", "/", "x" })
        {
            keys(key);
            require_moved(std::string("typed ") + key);
        }
        keys("<BS>");
        require_moved("a character taken back");
        keys("<Esc>");
        require_moved("the line closed");
        keys("/nothing-here<CR>");
        require_moved("a pattern not found said");
        keys("j");
        require_moved("the message cleared by a key");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a spelling search out of time says so and goes on from there next time", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("all fine\nall fine\nall fine\nteh end\n", "text");
        e.setSpellChecker([](const std::string& word) { return word != "teh"; });
        e.setSpellCheck(true);
        ll_test::TextViewProbe::misspellingBudget(e, 0.f);
        keys("]s");
        CHECK(vim->message() == std::string("Still checking the spelling: search again to go on"));
        UNSCOPED_INFO("not moved");
        CHECK(e.caret() == ALTextPos(0, 0));
        keys("]s");
        keys("]s");
        UNSCOPED_INFO("still not");
        CHECK(e.caret() == ALTextPos(0, 0));
        keys("]s");
        UNSCOPED_INFO("found, a line further each time");
        CHECK(e.caret() == ALTextPos(3, 0));
        e.recheckSpelling();
        ll_test::TextViewProbe::misspellingBudget(e, ALTextView::MISSPELLING_BUDGET);
        keys("gg]s");
        UNSCOPED_INFO("with time, at once");
        CHECK(e.caret() == ALTextPos(3, 0));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: n reuses its matches and sees edits; the search line lights as typed", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one x\ntwo x\nthree x\n");
        const auto lit = [&]() { return e.highlights(ALCodeEditor::Highlight::Search).size(); };
        keys("/x<CR>");
        {
            INFO("the first");
            CHECK(e.caret() == ALTextPos(0, 4));
            CHECK(lit() == 3);
        }
        keys("n");
        {
            INFO("the next");
            CHECK(e.caret() == ALTextPos(1, 4));
            CHECK(lit() == 3);
        }
        keys("ggix<Esc>");
        keys("n");
        {
            INFO("the text as it is now");
            CHECK(e.caret() == ALTextPos(0, 5));
            CHECK(lit() == 4);
        }
        ex("noh");
        CHECK(lit() == 0);
        keys("n");
        {
            INFO("lit again by the next n");
            CHECK(e.caret() == ALTextPos(1, 4));
            CHECK(lit() == 4);
        }
        ex("noh");
        keys("/th");
        UNSCOPED_INFO("lit as typed");
        CHECK(lit() == size_t(1));
        keys("<Left><Right>");
        UNSCOPED_INFO("the same as the cursor moves");
        CHECK(lit() == size_t(1));
        keys("<BS>");
        UNSCOPED_INFO("and again as the pattern changes");
        CHECK(lit() == size_t(2));
        keys("<Esc>");
        UNSCOPED_INFO("put out when left");
        CHECK(lit() == size_t(0));
        // A pattern that stands by the caret is found afresh from wherever
        // it is: \%# only where the caret is.
        e.setCaret(ALTextPos(2, 6));
        keys("/\\%#x<CR>");
        UNSCOPED_INFO("at the caret");
        CHECK(e.caret() == ALTextPos(2, 6));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a macro longer than any mapping plays through it wherever it comes", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("l0\nl1\nl2\nl3\nl4\nl5\nl6\nl7\n");
        ex("nnoremap ,d dd");
        keys("qa,djq");
        UNSCOPED_INFO("recorded as typed");
        CHECK(vim->registerText('a') == std::string(",dj"));
        keys("3@a");
        UNSCOPED_INFO("played through the mapping each time");
        CHECK(flat(e.text()) == std::string("l1|l3|l5|l7|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :sort n puts numberless lines first; u keeps one each; i u ignores case", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("x10\nb\n-3 y\nx2\nB\nb\n");
        ex("sort n");
        UNSCOPED_INFO("by number, those with none first as they were");
        CHECK(flat(e.text()) == std::string("b|B|b|-3 y|x2|x10|"));
        ex("sort u");
        UNSCOPED_INFO("by bytes, each once");
        CHECK(flat(e.text()) == std::string("-3 y|B|b|x10|x2|"));
        ex("sort i u");
        UNSCOPED_INFO("case aside, the first of each kept");
        CHECK(flat(e.text()) == std::string("-3 y|B|x10|x2|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: what vim holds at places moves with an edit made from outside", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc\n");
        keys("jVj");
        e.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "top\n" } });
        keys("d");
        UNSCOPED_INFO("the lines it was over deleted");
        CHECK(flat(e.text()) == std::string("top|a|"));

        e.setText("x\nx\nx\n");
        e.setCaret(ALTextPos(0, 0));
        keys(":%s/x/y/c<CR>");
        keys("y");
        e.replaceAll({ { ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "z" } });
        keys("yy");
        UNSCOPED_INFO("each where its match went");
        CHECK(flat(e.text()) == std::string("y|y|zy|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :s asking is one undo step; an outside edit meanwhile splits it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("x\nx\nx\n");
        keys(":%s/x/y/c<CR>");
        keys("yna");
        UNSCOPED_INFO("said yes, no, then all");
        CHECK(flat(e.text()) == std::string("y|x|y|"));
        e.undo();
        UNSCOPED_INFO("one step");
        CHECK(flat(e.text()) == std::string("x|x|x|"));

        e.setCaret(ALTextPos(0, 0));
        keys(":%s/x/y/c<CR>");
        keys("y");
        e.replaceAll({ { ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "z" } });
        keys("yy");
        e.undo();
        UNSCOPED_INFO("the answers after it");
        CHECK(flat(e.text()) == std::string("y|x|zx|"));
        e.undo();
        UNSCOPED_INFO("what was made meanwhile, apart");
        CHECK(flat(e.text()) == std::string("y|x|x|"));
        e.undo();
        UNSCOPED_INFO("the answer before it");
        CHECK(flat(e.text()) == std::string("x|x|x|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a function key in a macro is kept by name; keys read and written back", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\n");
        ex("nmap <F5> x");
        keys("qa");
        editor->handleKeyHere(KEY_F5, MASK_NONE);
        keys("q");
        UNSCOPED_INFO("recorded by its name");
        CHECK(vim->registerText('a') == std::string("<F5>"));
        keys("@a");
        UNSCOPED_INFO("and played");
        CHECK(flat(e.text()) == std::string("c|"));
        const std::vector<ALVimKeymap::Input> back = ALVimKeymap::decodeInputs("<F12><C-F3><Space><cr>");
        {
            INFO("read back");
            REQUIRE(back.size() == 4);
            CHECK(back[0].key == KEY_F12);
            CHECK(back[1].key == KEY_F3);
            CHECK(back[1].mask & ALVimKeymap::CONTROL);
            CHECK(back[2].isChar);
            CHECK(back[2].ch == ' ');
            CHECK(back[3].key == KEY_RETURN);
        }
        UNSCOPED_INFO("and written");
        CHECK(ALVimKeymap::encodeInputs(back) == std::string("<F12><C-F3> <CR>"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :m and :t are one change each; > shifts blank lines but not empty ones", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd\n");
        ex("1,2m3");
        {
            INFO("moved under c");
            CHECK(flat(e.text()) == "c|a|b|d|");
            CHECK(e.caret().line == 2);
        }
        e.undo();
        UNSCOPED_INFO("one step back");
        CHECK(flat(e.text()) == std::string("a|b|c|d|"));
        ex("4t0");
        {
            INFO("copied to the top");
            CHECK(flat(e.text()) == "d|a|b|c|d|");
            CHECK(e.caret().line == 0);
        }
        e.undo();

        e.setText("x\n  \n\ny\n");
        e.setCaret(ALTextPos(0, 0));
        keys(">3j");
        {
            INFO("shifted");
            CHECK(e.document().line(0).size() > 1);
            CHECK(e.document().line(3).size() > 1);
        }
        UNSCOPED_INFO("a line of blanks too");
        CHECK(e.document().line(1).size() > 2);
        UNSCOPED_INFO("an empty one not");
        CHECK(e.document().line(2).empty());
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a register holding what gg and gc once stood for plays them as text", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("\x01\xee\x80\x81j\none\ntwo\n");
        keys("\"ay$");
        UNSCOPED_INFO("yanked as it is");
        CHECK(vim->registerText('a') == std::string("\x01\xee\x80\x81j"));
        keys("j");
        keys("@a");
        UNSCOPED_INFO("not gg, nor gc: j alone moved");
        CHECK(e.caret().line == 2);
        UNSCOPED_INFO("nothing commented");
        CHECK(flat(e.text()) == std::string("\x01\xee\x80\x81j|one|two|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: vim over a plain text view: searches brackets motions work; folds not", "[alvimkeymap]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name         = "plain";
        p.rect         = LLRect(0, 200, 400, 0);
        p.default_text = "";
        ALTextView* view = LLUICtrlFactory::create<ALTextView>(p);
        view->setText("f(a) {\n  x\n}\nx again\n");
        auto keymap    = std::make_unique<ALVimKeymap>();
        ALVimKeymap* v = keymap.get();
        view->setModalKeymap(std::move(keymap));
        CHECK(view->vimHost() == nullptr);
        // As the viewer types: the key first, then the character.
        const auto type = [&](const char* text) {
            for (const char* c = text; *c; ++c)
            {
                if (!view->handleKeyHere(static_cast<KEY>(toupper(static_cast<unsigned char>(*c))), MASK_NONE))
                {
                    view->handleUnicodeCharHere(static_cast<llwchar>(static_cast<unsigned char>(*c)));
                }
            }
        };
        v->takeLine(*view, '/', "x", true);
        CHECK(view->caret() == ALTextPos(1, 2));
        type("n");
        CHECK(view->caret() == ALTextPos(3, 0));
        view->setCaret(ALTextPos(0, 5));
        type("%");
        UNSCOPED_INFO("% by brackets summed afresh");
        CHECK(view->caret() == ALTextPos(2, 0));
        type("zc");
        UNSCOPED_INFO("zc folds nothing, and the text is as it was");
        CHECK(view->text() == "f(a) {\n  x\n}\nx again\n");
        view->die();
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: under lines counted from 0 as notecards are: G and :0 go by the gutter", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("zero\none\ntwo\nthree\n");
        e.setLineNumberBase(-1);
        e.setLineNumberBase(-5);
        UNSCOPED_INFO("no lower than -1");
        CHECK(e.lineNumberBase() == -1);
        keys("2G");
        UNSCOPED_INFO("2G the third line");
        CHECK(e.caret().line == 2);
        ex("0");
        UNSCOPED_INFO(":0 the first");
        CHECK(e.caret().line == 0);
        keys("G");
        keys("1G");
        UNSCOPED_INFO("1G the second");
        CHECK(e.caret().line == 1);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the search line's match comes out from under pinned headers as Find's", "[alvimkeymap]")
    {
        std::string text = "default\n{\n    state_entry()\n    {\n";
        for (int i = 0; i < 80; ++i)
        {
            text += "        llOwnerSay(\"line " + std::to_string(i) + "\");\n";
        }
        text += "    }\n}\n";
        ALCodeEditor& e = make(text.c_str());
        e.setStickyHeaders(true);
        const S32 row_h = e.layout().rowHeight();
        // Deep in the handler, so that its state and it are pinned over
        // the top; then back up to a line above the view. "line 40" is the
        // text's 45th line.
        keys("75G");
        keys("?line 40");
        const S32 shown = e.layout().lineTop(44) - e.scrollY();
        UNSCOPED_INFO("shown below the two pinned headers as it is typed: " << shown << " of " << row_h);
        CHECK(shown >= 2 * row_h);
        UNSCOPED_INFO("the caret where it was");
        CHECK(e.caret().line == 74);
        keys("<CR>");
        const S32 below = e.layout().lineTop(44) - e.scrollY();
        CHECK(e.caret().line == 44);
        UNSCOPED_INFO("and below them there: " << below << " of " << row_h);
        CHECK(below >= 2 * row_h);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: vim has one caret: typing at several stays; Escape keeps the main one", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one\ntwo\nthree");
        e.setSelections(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)) });
        keys("iab");
        UNSCOPED_INFO("typed at each");
        CHECK(e.text() == std::string("abone\nabtwo\nabthree"));
        UNSCOPED_INFO("still several while inserting");
        CHECK(e.hasOtherSelections());
        keys("<Esc>");
        UNSCOPED_INFO("one caret in normal mode");
        CHECK_FALSE(e.hasOtherSelections());
        UNSCOPED_INFO("the main one, on the last character it typed");
        CHECK(e.caret() == ALTextPos(2, 1));
        e.setSelections(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)) });
        keys("3iX<Esc>");
        UNSCOPED_INFO("once at each, the count's again at the main one only");
        CHECK(e.text() == std::string("Xabone\nXabtwo\nXXXabthree"));
        CHECK_FALSE(e.hasOtherSelections());
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: several carets: a held key and Replace go in at each; undo leaves one", "[alvimkeymap]")
    {
        ALCodeEditor& e     = make("one\ntwo");
        const auto    caret = [](S32 line, S32 column) { return ALTextRange(ALTextPos(line, column), ALTextPos(line, column)); };
        ex("inoremap jk <Esc>");
        e.setSelections(caret(1, 0), { caret(0, 0) });
        keys("ijx");
        UNSCOPED_INFO("the held j at each, then the x");
        CHECK(e.text() == std::string("jxone\njxtwo"));
        keys("<Esc>");
        e.setSelections(caret(1, 0), { caret(0, 0) });
        keys("RQ");
        UNSCOPED_INFO("overtyped at each");
        CHECK(e.text() == std::string("Qxone\nQxtwo"));
        keys("<Esc>");
        keys("u");
        CHECK(e.text() == std::string("jxone\njxtwo"));
        UNSCOPED_INFO("one caret in normal mode after it");
        CHECK_FALSE(e.hasOtherSelections());
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the registers are shared between buffers: yanks and macros go across", "[alvimkeymap]")
    {
        make("alpha\nbeta");
        const std::string outside("outside");
        LLClipboard::instance().copyToClipboard(outside, 0, static_cast<S32>(outside.size()));
        const std::shared_ptr<ALVimKeymap::Shared> shared = vim->sharedState();
        UNSCOPED_INFO("off, as vim has it");
        CHECK_FALSE(shared->unnamedClipboard);
        keys("\"ayy");
        keys("jyy");
        keys("qqA!<Esc>q");

        // Another buffer, its keymap sharing the first's state.
        make("gamma");
        vim->share(shared);
        keys("p");
        UNSCOPED_INFO("the unnamed register, from the other buffer");
        CHECK(flat(editor->text()) == std::string("gamma|beta"));
        keys("\"ap");
        UNSCOPED_INFO("a named one");
        CHECK(flat(editor->text()) == std::string("gamma|beta|alpha"));
        keys("@q");
        UNSCOPED_INFO("a macro recorded there");
        CHECK(flat(editor->text()) == std::string("gamma|beta|alpha!"));
        std::string held;
        LLClipboard::instance().pasteFromClipboard(held);
        UNSCOPED_INFO("the clipboard left alone");
        CHECK(held == outside);

        // One that shares nothing has its own.
        make("delta");
        keys("p");
        UNSCOPED_INFO("nothing to put");
        CHECK(flat(editor->text()) == std::string("delta"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: an operator before no motion or one that cannot go fails typing nothing", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foo bar\n");
        for (const char* typed : { "dn", "c&", "yz", "dv", "gUp", "ysq", "d\"" })
        {
            const std::string what(typed);
            keys(typed);
            UNSCOPED_INFO(what << " leaves the text as it was");
            REQUIRE(flat(e.text()) == std::string("foo bar|"));
            {
                INFO(what << ": normal mode, nothing pending");
                REQUIRE(vim->mode() == ALVimKeymap::Mode::Normal);
                REQUIRE(vim->status().empty());
            }
            UNSCOPED_INFO(what << ": the caret where it was");
            REQUIRE(e.caret() == ALTextPos(0, 0));
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: cw on a word's last character or a one-letter word changes it alone", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("integer i = 0;\nfoo bar\nab cd ef\n");
        e.setCaret(ALTextPos(0, 8));
        keys("cwj<Esc>");
        UNSCOPED_INFO("a one-letter word, not what follows it");
        CHECK(e.document().line(0) == std::string("integer j = 0;"));
        e.setCaret(ALTextPos(1, 2));
        keys("cwX<Esc>");
        UNSCOPED_INFO("from a word's last character, that character");
        CHECK(e.document().line(1) == std::string("foX bar"));
        e.setCaret(ALTextPos(2, 1));
        keys("c2wX<Esc>");
        UNSCOPED_INFO("c2w from there: the word it ends and the next");
        CHECK(e.document().line(2) == std::string("aX ef"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: an operator's counted words cross line ends; only the last stops", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one\ntwo three\nx\nab cd\nef gh\n");
        keys("d2w");
        UNSCOPED_INFO("d2w takes the line's word, the break and the next line's first word");
        CHECK(flat(e.text()) == std::string("three|x|ab cd|ef gh|"));
        e.setCaret(ALTextPos(2, 3));
        keys("c2wX<Esc>");
        UNSCOPED_INFO("c2w changes through the next line's first word");
        CHECK(flat(e.text()) == std::string("three|x|ab X gh|"));
        e.setCaret(ALTextPos(0, 0));
        keys("y3w");
        UNSCOPED_INFO("y3w yanks three words across two line breaks");
        CHECK(vim->registerText('0') == std::string("three\nx\nab "));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a count before the operator counts as one after it does", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a.b.c.d\n1\n2\n3\n4\n5\nabcdef\nf(a, g(b), c)\none two three\n");
        keys("2df.");
        UNSCOPED_INFO("2df. is d2f.");
        CHECK(e.document().line(0) == std::string("c.d"));
        e.setCaret(ALTextPos(4, 0));
        keys("2dG");
        UNSCOPED_INFO("2dG takes up to the second line, not to the last");
        CHECK(flat(e.text()) == std::string("c.d|5|abcdef|f(a, g(b), c)|one two three|"));
        e.setCaret(ALTextPos(2, 4));
        keys("3d|");
        UNSCOPED_INFO("3d| takes back to the third column");
        CHECK(e.document().line(2) == std::string("abef"));
        e.setCaret(ALTextPos(3, 7));
        keys("2d])");
        UNSCOPED_INFO("2d]) takes to the second close out");
        CHECK(e.document().line(3) == std::string("f(a, g()"));
        e.setCaret(ALTextPos(4, 0));
        keys("2diw");
        UNSCOPED_INFO("2diw is d2iw");
        CHECK(e.document().line(4) == std::string("two three"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: an exclusive motion to a line's first column ends with the line before", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("xa\nb\n\nc\n");
        e.setCaret(ALTextPos(0, 1));
        keys("d}");
        UNSCOPED_INFO("d} leaves the blank line it goes to");
        CHECK(flat(e.text()) == std::string("x||c|"));
        keys("u");
        e.setCaret(ALTextPos(0, 0));
        keys("y}");
        UNSCOPED_INFO("y} from the line's start yanks the lines");
        CHECK(vim->registerText('0') == std::string("xa\nb"));
        keys("p");
        UNSCOPED_INFO("which are put as lines");
        CHECK(flat(e.text()) == std::string("xa|xa|b|b||c|"));

        ALCodeEditor& marked = make("one\ntwo\nthree\n");
        keys("jjma");
        marked.setCaret(ALTextPos(0, 1));
        keys("d`a");
        UNSCOPED_INFO("d`a to a mark in the first column takes up to the end of the line before");
        CHECK(flat(marked.text()) == std::string("o|three|"));

        // $ is inclusive, though its end is past the last character: d2$
        // onto an empty line takes the break before it.
        ALCodeEditor& ended = make("ab\n\ncd\n");
        ended.setCaret(ALTextPos(0, 1));
        keys("d2$");
        UNSCOPED_INFO("d2$ onto an empty line joins it");
        CHECK(flat(ended.text()) == std::string("a|cd|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: D and d$ on an empty line take nothing and leave the registers alone", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one\n\nthree\n");
        keys("yyj");
        keys("D");
        UNSCOPED_INFO("nothing taken out");
        CHECK(flat(e.text()) == std::string("one||three|"));
        UNSCOPED_INFO("the unnamed register still holds the yank");
        CHECK(vim->registerText('"') == std::string("one"));
        keys("d$");
        UNSCOPED_INFO("d$ leaves it too");
        CHECK(vim->registerText('"') == std::string("one"));
        keys("p");
        UNSCOPED_INFO("which p puts");
        CHECK(flat(e.text()) == std::string("one||one|three|"));

        keys(":set clipboard=unnamed<CR>");
        const std::string outside("outside");
        LLClipboard::instance().copyToClipboard(outside, 0, static_cast<S32>(outside.size()));
        keys("k");
        keys("D");
        std::string held;
        LLClipboard::instance().pasteFromClipboard(held);
        UNSCOPED_INFO("with the clipboard the unnamed register, the clipboard is left alone");
        CHECK(held == outside);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :g runs a command's own address from each line; +1 alone is .+1", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nTODO 1\nb\nc\nTODO 2\nd\ne\n");
        keys(":g/TODO/.,+1d<CR>");
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO("each match and the line after it gone");
        CHECK(flat(e.text()) == std::string("a|c|e|"));

        ALCodeEditor& moved = make("x 1\ny\nx 2\n");
        keys(":g/x/.m0<CR>");
        UNSCOPED_INFO("no error for .m0");
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO("each match moved to the top in turn");
        CHECK(flat(moved.text()) == std::string("x 2|x 1|y|"));

        keys("gg:+1d<CR>");
        UNSCOPED_INFO(":+1d takes the line below");
        CHECK(flat(moved.text()) == std::string("x 2|y|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: % is 1 to $: the empty line after a final break is none of its lines", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\n");
        keys(":%s/$/;/<CR>");
        UNSCOPED_INFO(":%s/$/;/ ends the two lines, and makes no third");
        CHECK(flat(e.text()) == std::string("a;|b;|"));
        keys("u");
        keys(":%norm A;<CR>");
        UNSCOPED_INFO(":%norm A; types on the two lines alone");
        CHECK(flat(e.text()) == std::string("a;|b;|"));
        keys("u");
        keys(":%y<CR>");
        UNSCOPED_INFO(":%y yanks the two lines");
        CHECK(vim->registerText('0') == std::string("a\nb"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a visual block is the columns the reader counts: whole characters", "[alvimkeymap]")
    {
        make("\xc3\xa9\n");
        keys("<C-v>d");
        UNSCOPED_INFO("a character of two bytes cut whole");
        CHECK(editor->document().line(0) == std::string());

        make("a\xc3\xa9\nab\n");
        keys("l<C-v>jd");
        UNSCOPED_INFO("the first line's column, whole");
        CHECK(editor->document().line(0) == std::string("a"));
        UNSCOPED_INFO("the second's");
        CHECK(editor->document().line(1) == std::string("a"));

        make("\tab\nxxxxab\n");
        editor->setTabWidth(4);
        keys("l<C-v>jd");
        UNSCOPED_INFO("after a tab, the column it reaches");
        CHECK(editor->document().line(0) == std::string("\tb"));
        UNSCOPED_INFO("the same column under it");
        CHECK(editor->document().line(1) == std::string("xxxxb"));

        make("\xc3\xa9" "a\nxa\n");
        keys("l<C-v>jrx");
        UNSCOPED_INFO("r past a character of two bytes");
        CHECK(editor->document().line(0) == std::string("\xc3\xa9" "x"));
        UNSCOPED_INFO("and under it");
        CHECK(editor->document().line(1) == std::string("xx"));

        make("\xc3\xa9" "a\nxa\n");
        keys("l<C-v>jIZ<Esc>");
        UNSCOPED_INFO("I before the column, not inside the character");
        CHECK(editor->document().line(0) == std::string("\xc3\xa9" "Za"));
        UNSCOPED_INFO("and on the line under it");
        CHECK(editor->document().line(1) == std::string("xZa"));

        make("\xc3\xa9x\n");
        keys("<C-v>");
        const std::vector<ALTextRange>& lit = editor->highlights(ALCodeEditor::Highlight::Block);
        {
            INFO("lit over the whole character");
            REQUIRE(lit.size() == 1);
            CHECK(lit[0] == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        }
        keys("<Esc>");

        make("ab\ncd\n\xc3\xa9x\n\xc3\xa9y\n");
        keys("<C-v>jy2jp");
        UNSCOPED_INFO("a block put past a character of two bytes");
        CHECK(editor->document().line(2) == std::string("\xc3\xa9" "ax"));
        UNSCOPED_INFO("and on the line under it");
        CHECK(editor->document().line(3) == std::string("\xc3\xa9" "cy"));

        make("xxxxab\n\tab\nxxxxab\n");
        editor->setTabWidth(4);
        keys("4l<C-v>2j<Esc>:%s/\\%Va/Z/g<CR>");
        UNSCOPED_INFO("\\%V on a block: its column on a line a tab begins");
        CHECK(flat(editor->text()) == std::string("xxxxZb|\tZb|xxxxZb|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: visual p: lines over lines or characters; a line or block over a block", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc\n");
        keys("yyjVp");
        UNSCOPED_INFO("V p: the line replaced, and nothing more");
        CHECK(flat(e.text()) == std::string("a|a|c|"));
        UNSCOPED_INFO("the caret on the line put");
        CHECK(caretText() == std::string("1:0"));

        make("abcd\nX\n");
        keys("jyygglvlp");
        UNSCOPED_INFO("v p of a line: on a line of its own, the line broken round it");
        CHECK(flat(editor->text()) == std::string("a|X|d|X|"));

        make("abcd\nefgh\nX\n");
        keys("2jylggl<C-v>jlp");
        UNSCOPED_INFO("a block's columns each replaced by a register of one line, the rest of the lines kept");
        CHECK(flat(editor->text()) == std::string("aXd|eXh|X|"));

        make("ab\ncd\nxyz\nxyz\n");
        keys("<C-v>jy2jl<C-v>jp");
        UNSCOPED_INFO("a block register put in place of a block");
        CHECK(flat(editor->text()) == std::string("ab|cd|xaz|xcz|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: visual block A appends past the block on each line padding short ones", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abcd\nefgh\n");
        keys("l<C-v>jlAX<Esc>");
        UNSCOPED_INFO("past the block, not before its last column");
        CHECK(flat(e.text()) == std::string("abcXd|efgXh|"));

        make("ab\nlonger\n");
        keys("l<C-v>jllA;<Esc>");
        UNSCOPED_INFO("a line short of the block padded out to its edge");
        CHECK(flat(editor->text()) == std::string("ab  ;|long;er|"));

        make("ab\nlonger\n");
        keys("<C-v>j$A;<Esc>");
        UNSCOPED_INFO("with $, past each line's end");
        CHECK(flat(editor->text()) == std::string("ab;|longer;|"));

        make("longer\nab\n");
        keys("<C-v>j$");
        const std::vector<ALTextRange>& lit = editor->highlights(ALCodeEditor::Highlight::Block);
        {
            INFO("with $, lit to every line's end");
            REQUIRE(lit.size() == 2);
            CHECK(lit[0] == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 6)));
        }
        keys("h");
        {
            INFO("and no longer once the caret moves along the line, from past the last character onto it");
            REQUIRE(editor->highlights(ALCodeEditor::Highlight::Block).size() == 2);
            CHECK(editor->highlights(ALCodeEditor::Highlight::Block)[0] == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        }
        keys("$d");
        UNSCOPED_INFO("a block taken with $ is every line to its end");
        CHECK(flat(editor->text()) == std::string("||"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: / and ? extend a visual selection; Escape or a backspace go back to it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one foo two\nthree\n");
        keys("v/foo<CR>");
        UNSCOPED_INFO("still visual after the search");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        keys("d");
        UNSCOPED_INFO("d from where it began through the match's first character");
        CHECK(flat(e.text()) == std::string("oo two|three|"));

        make("one foo two\n");
        keys("$v?foo<CR>d");
        UNSCOPED_INFO("? back over the selection");
        CHECK(flat(editor->text()) == std::string("one |"));

        make("ab\ncd\nxy\n");
        keys("<C-v>/y<CR>");
        UNSCOPED_INFO("a block stays a block");
        CHECK(vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("d");
        UNSCOPED_INFO("and takes its columns down to the match");
        CHECK(flat(editor->text()) == std::string("|||"));

        make("one foo\n");
        keys("v/fo<Esc>");
        UNSCOPED_INFO("Escape back to visual");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        keys("<Esc>V/<BS>");
        UNSCOPED_INFO("a backspace on the empty line back to visual by lines");
        CHECK(vim->mode() == ALVimKeymap::Mode::VisualLine);
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: what an insert typed is read off the text after Ctrl-W and an arrow", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("\n");
        keys("3ifoo bar<C-w>baz<Esc>");
        UNSCOPED_INFO("what stood after Ctrl-W, three times");
        CHECK(e.document().line(0) == std::string("foo bazfoo bazfoo baz"));
        keys("A <C-a><Esc>");
        UNSCOPED_INFO("and Ctrl-A types it");
        CHECK(e.document().line(0) == std::string("foo bazfoo bazfoo baz foo baz"));

        make("\n");
        keys("ifoo<Left>X<Esc>");
        UNSCOPED_INFO("typed where the arrow left the caret");
        CHECK(editor->document().line(0) == std::string("foXo"));
        keys(".");
        UNSCOPED_INFO(". types again what followed the arrow alone");
        CHECK(editor->document().line(0) == std::string("foXXo"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a count before o and O opens as many lines and . does it again", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\n");
        keys("3ofoo<Esc>");
        UNSCOPED_INFO("three lines below");
        CHECK(flat(e.text()) == std::string("abc|foo|foo|foo|"));
        keys("gg2Obar<Esc>");
        UNSCOPED_INFO("two above");
        CHECK(flat(e.text()) == std::string("bar|bar|abc|foo|foo|foo|"));
        keys("G.");
        UNSCOPED_INFO(". opens two more");
        CHECK(flat(e.text()) == std::string("bar|bar|abc|foo|foo|foo|bar|bar|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: Ctrl-O's command that inserts is the insert: one Escape leaves it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\n");
        keys("A<C-o>ox<Esc>");
        {
            INFO("normal mode after one Escape");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().find("-- (insert) --") == std::string::npos);
        }
        UNSCOPED_INFO("the line opened and typed on");
        CHECK(flat(e.text()) == std::string("abc|x|"));
        keys("i<C-o>A!<Esc>");
        UNSCOPED_INFO("after A too");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        UNSCOPED_INFO("typed at the line's end");
        CHECK(flat(e.text()) == std::string("abc|x!|"));
        keys("ggi<C-o>cwyz<Esc>");
        UNSCOPED_INFO("and after cw");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        UNSCOPED_INFO("the word changed");
        CHECK(flat(e.text()) == std::string("yz|x!|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: visual r puts one character for each however many bytes it takes", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("h\xc3\xa9llo\n");
        keys("v$rx");
        UNSCOPED_INFO("five characters, five x");
        CHECK(e.document().line(0) == std::string("xxxxx"));

        make("a\xf0\x9f\x98\x80" "b\n");
        keys("v$rx");
        UNSCOPED_INFO("an emoji one");
        CHECK(editor->document().line(0) == std::string("xxx"));

        make("\xc3\xa9\n");
        keys("<C-v>rx");
        UNSCOPED_INFO("and in a block");
        CHECK(editor->document().line(0) == std::string("x"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: : over a visual selection runs over its lines; gv selects it again", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd\n");
        keys("jVj:d<CR>");
        UNSCOPED_INFO("the two lines selected deleted");
        CHECK(flat(e.text()) == std::string("a|d|"));

        make("ab\ncd\nef\n");
        keys("l<C-v>j:<Esc>");
        UNSCOPED_INFO("the block put out");
        CHECK(editor->highlights(ALCodeEditor::Highlight::Block).empty());
        keys("gv");
        {
            INFO("gv selects the block again");
            CHECK(vim->mode() == ALVimKeymap::Mode::VisualBlock);
            CHECK(editor->highlights(ALCodeEditor::Highlight::Block).size() == 2);
        }
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: q: and q/ over a selection do as : and / do; gv brings a block back", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("ab\ncd\n");
        keys("<C-v>jq:");
        UNSCOPED_INFO("q: opens the : line");
        CHECK(vim->mode() == ALVimKeymap::Mode::Command);
        UNSCOPED_INFO("the block put out");
        CHECK(e.highlights(ALCodeEditor::Highlight::Block).empty());
        keys("<Esc>gv");
        UNSCOPED_INFO("gv selects it again");
        CHECK(vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("<Esc>");

        make("one foo two\n");
        keys("vq/foo<CR>");
        UNSCOPED_INFO("q/ searches over the selection");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        keys("d");
        UNSCOPED_INFO("which runs to the match");
        CHECK(flat(editor->text()) == std::string("oo two|"));

        make("ab\ncd\n");
        keys("<C-v>jI<Esc>");
        {
            INFO("insert mode puts the block out");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(editor->highlights(ALCodeEditor::Highlight::Block).empty());
        }
        keys("gv");
        UNSCOPED_INFO("and keeps it for gv");
        CHECK(vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("<Esc>");

        make("a\na\n");
        keys(":s/a/b/c<CR>q<C-v>jg&");
        {
            INFO("an asking g& over a block puts it out");
            CHECK(vim->mode() == ALVimKeymap::Mode::Confirm);
            CHECK(editor->highlights(ALCodeEditor::Highlight::Block).empty());
        }
        keys("qgv");
        UNSCOPED_INFO("and keeps it for gv after");
        CHECK(vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a line from q:'s window ends what vim was doing as its keys would", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\n");
        keys("3ix");
        vim->takeLine(e, ':', "s/b/B/", true);
        UNSCOPED_INFO("the insert typed again for its count, then the line run");
        CHECK(e.document().line(0) == std::string("xxxaBc"));
        keys("u");
        UNSCOPED_INFO("the line's change a step of its own");
        CHECK(e.document().line(0) == std::string("xxxabc"));
        keys("u");
        UNSCOPED_INFO("and the insert's");
        CHECK(e.document().line(0) == std::string("abc"));

        make("\n");
        keys("i<C-v>");
        vim->takeLine(*editor, ':', "set ic", true);
        keys("iu0041<Esc>");
        UNSCOPED_INFO("a Ctrl-V waiting let go of with the insert");
        CHECK(editor->document().line(0) == std::string("u0041"));

        make("a a\n");
        keys(":s/a/b/gc<CR>");
        CHECK(vim->mode() == ALVimKeymap::Mode::Confirm);
        vim->takeLine(*editor, ':', "s/a/c/", true);
        UNSCOPED_INFO("the matches still to come put out");
        CHECK(editor->highlights(ALCodeEditor::Highlight::Confirm).empty());
        UNSCOPED_INFO("the line run once the asking ended");
        CHECK(editor->document().line(0) == std::string("c a"));

        make("abcd\n");
        keys("vl");
        vim->takeLine(*editor, ':', "set ic", true);
        {
            INFO("the selection let go of at its caret");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(caretText() == "0:1");
            CHECK_FALSE(editor->hasSelection());
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: p puts an empty line yy or dd took; only an unset register is empty", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\n\nb\n");
        keys("p");
        {
            INFO("a register never set: E353");
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("E353") != std::string::npos);
        }
        keys("jyyp");
        UNSCOPED_INFO("an empty line is something to put");
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO("yy then p: the empty line again below");
        CHECK(flat(e.text()) == std::string("a|||b|"));
        keys("ddp");
        UNSCOPED_INFO("dd then p: below the line that took its place");
        CHECK(flat(e.text()) == std::string("a||b||"));
        ex("set clipboard=unnamed");
        keys("yyp");
        UNSCOPED_INFO("by the clipboard, which an empty line leaves empty");
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO("put all the same");
        CHECK(flat(e.text()) == std::string("a||b|||"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: an operator over an f t F T or mark that cannot move fails whole", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc def\n");
        keys("xyiww");
        keys("cfz");
        UNSCOPED_INFO("cfz with no z: still normal mode");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        UNSCOPED_INFO("and no step to undo left open");
        CHECK_FALSE(e.undoJournal().inGroup());
        UNSCOPED_INFO("nothing changed");
        CHECK(flat(e.text()) == std::string("bc def|"));
        UNSCOPED_INFO("the register as it was");
        CHECK(vim->registerText('"') == std::string("bc"));
        keys("ytz");
        UNSCOPED_INFO("ytz keeps nothing");
        CHECK(vim->registerText('"') == std::string("bc"));
        keys("dFz");
        keys("c`q");
        UNSCOPED_INFO("c to a mark not set: still normal mode");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        UNSCOPED_INFO("still nothing changed");
        CHECK(flat(e.text()) == std::string("bc def|"));
        UNSCOPED_INFO("nor the register");
        CHECK(vim->registerText('"') == std::string("bc"));
        keys(".");
        UNSCOPED_INFO(". is the x before them");
        CHECK(flat(e.text()) == std::string("bc ef|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: counted aw and iw; aw on blanks takes the next word and keeps an indent", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foo bar baz\n");
        keys("2daw");
        UNSCOPED_INFO("2daw: two words and the blanks after them");
        CHECK(e.document().line(0) == std::string("baz"));
        keys("u");
        e.setCaret(ALTextPos(0, 4));
        keys("d2aw");
        UNSCOPED_INFO("d2aw from the middle: the blank before, none being after");
        CHECK(e.document().line(0) == std::string("foo"));
        keys("u");
        e.setCaret(ALTextPos(0, 0));
        keys("2diw");
        UNSCOPED_INFO("2diw: the word and the blank after");
        CHECK(e.document().line(0) == std::string("bar baz"));
        keys("u");
        e.setCaret(ALTextPos(0, 0));
        keys("3diw");
        UNSCOPED_INFO("3diw: to the second word's end");
        CHECK(e.document().line(0) == std::string(" baz"));
        keys("u");
        e.setCaret(ALTextPos(0, 0));
        keys("v2awd");
        UNSCOPED_INFO("v2aw selects as much");
        CHECK(e.document().line(0) == std::string("baz"));

        make("foo.x bar.y baz\n");
        keys("2daW");
        UNSCOPED_INFO("2daW by WORDs");
        CHECK(editor->document().line(0) == std::string("baz"));

        make("foo   bar baz\n");
        editor->setCaret(ALTextPos(0, 4));
        keys("daw");
        UNSCOPED_INFO("daw on blanks: they and the word after");
        CHECK(editor->document().line(0) == std::string("foo baz"));

        make("  foo\n");
        keys("^daw");
        UNSCOPED_INFO("daw on a line's only word leaves its indent");
        CHECK(editor->document().line(0) == std::string("  "));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: s and S on an empty line insert there and leave the registers alone", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\n\n\n\nxyz\n");
        keys("yiwjs");
        UNSCOPED_INFO("s on an empty line: insert mode");
        CHECK(vim->mode() == ALVimKeymap::Mode::Insert);
        keys("new<Esc>");
        UNSCOPED_INFO("what was typed there");
        CHECK(flat(e.text()) == std::string("abc|new|||xyz|"));
        keys("j3sx<Esc>");
        UNSCOPED_INFO("3s as well");
        CHECK(flat(e.text()) == std::string("abc|new|x||xyz|"));
        UNSCOPED_INFO("nothing taken, so the register as it was");
        CHECK(vim->registerText('"') == std::string("abc"));
        keys("u");
        UNSCOPED_INFO("each one step to undo");
        CHECK(flat(e.text()) == std::string("abc|new|||xyz|"));
        e.setCaret(ALTextPos(3, 0));
        keys("Sy<Esc>");
        UNSCOPED_INFO("S on an empty line inserts too");
        CHECK(flat(e.text()) == std::string("abc|new||y|xyz|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: visual O goes to a block's other corner on its line; elsewhere it is o", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abcdef\nabcdef\nabcdef\n");
        keys("l<C-v>jllO");
        {
            INFO("still the block, no line opened");
            CHECK(vim->mode() == ALVimKeymap::Mode::VisualBlock);
            CHECK(flat(e.text()) == "abcdef|abcdef|abcdef|");
        }
        UNSCOPED_INFO("the caret to the block's left, on its own line");
        CHECK(caretText() == std::string("1:1"));
        keys("O");
        UNSCOPED_INFO("and back to its right");
        CHECK(caretText() == std::string("1:3"));
        keys("jOd");
        UNSCOPED_INFO("the same columns, a line further");
        CHECK(flat(e.text()) == std::string("aef|aef|aef|"));

        make("one two\n");
        keys("vllO");
        UNSCOPED_INFO("charwise: still visual");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        UNSCOPED_INFO("the caret to the other end");
        CHECK(caretText() == std::string("0:0"));
        keys("d");
        UNSCOPED_INFO("the same characters");
        CHECK(editor->document().line(0) == std::string(" two"));

        make("a\nb\nc\nd\n");
        keys("jVjOkd");
        UNSCOPED_INFO("linewise: the caret to the first line, which k goes on from");
        CHECK(flat(editor->text()) == std::string("d|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: visual gi extends to where inserting stopped and begins no insert", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\ndef\nghi\n");
        keys("A!<Esc>jj0vgi");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        UNSCOPED_INFO("the caret where inserting stopped");
        CHECK(caretText() == std::string("0:4"));
        keys("d");
        UNSCOPED_INFO("the selection from there taken");
        CHECK(flat(e.text()) == std::string("abc!hi|"));

        make("ab\ncd\n");
        keys("<C-v>jgi");
        {
            INFO("a block stays one, with no insert to have stopped");
            CHECK(vim->mode() == ALVimKeymap::Mode::VisualBlock);
            CHECK(caretText() == "1:0");
        }
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: / and ? after an operator go up to the match; line offsets and /e", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one foo two\n");
        keys("d/foo<CR>");
        UNSCOPED_INFO("d/foo up to the match");
        CHECK(flat(e.text()) == std::string("foo two|"));
        {
            INFO("normal mode, nothing pending");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().empty());
        }
        UNSCOPED_INFO("what it took in the register");
        CHECK(vim->registerText('"') == std::string("one "));

        make("alpha beta gamma\n");
        keys("$c?beta<CR>X<Esc>");
        UNSCOPED_INFO("c?beta back to the match, the caret's character left, then typed into");
        CHECK(flat(editor->text()) == std::string("alpha Xa|"));
        UNSCOPED_INFO("normal mode after the insert");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);

        make("one foo two\n");
        keys("d/foo/e<CR>");
        UNSCOPED_INFO("/foo/e through the match's last character");
        CHECK(flat(editor->text()) == std::string(" two|"));

        make("one\ntwo\nthree foo\nfour\n");
        keys("d/foo/-<CR>");
        UNSCOPED_INFO("/foo/- the lines down to the one before the match's");
        CHECK(flat(editor->text()) == std::string("three foo|four|"));

        make("a\nb foo\nc\nd\n");
        keys("jly/foo/+1<CR>");
        UNSCOPED_INFO("/foo/+1 yanks the lines, its own whole");
        CHECK(vim->registerText('"') == std::string("b foo\nc"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: n and N after an operator are its motion; counts go that many on", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a foo b foo c foo\n");
        keys("/foo<CR>dn");
        UNSCOPED_INFO("dn up to the next match");
        CHECK(flat(e.text()) == std::string("a foo c foo|"));
        keys("$dN");
        UNSCOPED_INFO("dN back to the one before, the caret's character left");
        CHECK(flat(e.text()) == std::string("a foo c o|"));

        make("a x b x c x d\n");
        keys("2d/x<CR>");
        UNSCOPED_INFO("2d/x up to the second x");
        CHECK(flat(editor->text()) == std::string("x c x d|"));
        keys("u0d2n");
        UNSCOPED_INFO("and d2n");
        CHECK(flat(editor->text()) == std::string("x c x d|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the search line after an operator lights; Escape or a miss lets it go", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two\n");
        keys("d/o");
        CHECK(vim->mode() == ALVimKeymap::Mode::Search);
        UNSCOPED_INFO("each match lit as typed");
        CHECK(e.highlights(ALCodeEditor::Highlight::Search).size() == size_t(2));
        keys("<Esc>");
        {
            INFO("let go of, the operator with it");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().empty());
        }
        UNSCOPED_INFO("the matches put out");
        CHECK(e.highlights(ALCodeEditor::Highlight::Search).empty());
        UNSCOPED_INFO("nothing taken");
        CHECK(flat(e.text()) == std::string("one two|"));
        keys("x");
        UNSCOPED_INFO("the next key a command of its own");
        CHECK(flat(e.text()) == std::string("ne two|"));
        keys("d/<BS>x");
        UNSCOPED_INFO("a backspace on the empty line lets it go as well");
        CHECK(flat(e.text()) == std::string("e two|"));

        keys("yw");
        keys("d/zzz<CR>");
        {
            INFO("not found, and said");
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("Pattern not found: zzz") != std::string::npos);
        }
        {
            INFO("the operator let go of");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().empty());
        }
        UNSCOPED_INFO("nothing taken");
        CHECK(flat(e.text()) == std::string("e two|"));
        UNSCOPED_INFO("the register as it was");
        CHECK(vim->registerText('"') == std::string("e "));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: . after d/foo seeks foo again; a search to a first column stops before", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a foo b bar c foo d bar\n");
        keys("d/foo<CR>");
        CHECK(flat(e.text()) == std::string("foo b bar c foo d bar|"));
        keys("/bar<CR>.");
        UNSCOPED_INFO(". up to the next foo, not the next bar");
        CHECK(flat(e.text()) == std::string("foo b foo d bar|"));

        make("ab\ncd\n");
        keys("d/cd<CR>");
        UNSCOPED_INFO("from the line's start, the line whole");
        CHECK(flat(editor->text()) == std::string("cd|"));
        keys("p");
        UNSCOPED_INFO("put back as a line");
        CHECK(flat(editor->text()) == std::string("cd|ab|"));

        make("ab\ncd\n");
        keys("ld/cd<CR>");
        UNSCOPED_INFO("from inside it, to the line's end");
        CHECK(flat(editor->text()) == std::string("a|cd|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a count before a search line goes when Escape or backspace lets it go", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abcdef\n");
        keys("3/<Esc>");
        {
            INFO("Escape: nothing pending");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().empty());
        }
        keys("x");
        UNSCOPED_INFO("x after it takes one character, not three");
        CHECK(flat(e.text()) == std::string("bcdef|"));
        keys("3/<BS>");
        {
            INFO("a backspace on the empty line: nothing pending");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().empty());
        }
        keys("x");
        UNSCOPED_INFO("one character again");
        CHECK(flat(e.text()) == std::string("cdef|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: t and T to a character beside the caret stay put; ; goes past it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("f(x)(y)\n(x)\na)b)c\n");
        e.setCaret(ALTextPos(0, 2));
        keys("dt)");
        UNSCOPED_INFO("dt) beside a ): the x alone, not on to the next )");
        CHECK(e.document().line(0) == std::string("f()(y)"));
        e.setCaret(ALTextPos(1, 1));
        keys("ct)Z<Esc>");
        UNSCOPED_INFO("ct) beside the line's only ): the x changed");
        CHECK(e.document().line(1) == std::string("(Z)"));
        e.setCaret(ALTextPos(2, 0));
        keys("t)");
        UNSCOPED_INFO("t) beside a ): the caret stays");
        CHECK(caretText() == std::string("2:0"));
        keys(";");
        UNSCOPED_INFO("; after it goes on to before the next");
        CHECK(caretText() == std::string("2:2"));
        keys("$T)");
        UNSCOPED_INFO("T) beside a ): the caret stays");
        CHECK(caretText() == std::string("2:4"));
        keys(";");
        UNSCOPED_INFO("; after it goes back to after the one before");
        CHECK(caretText() == std::string("2:2"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :put puts an empty line from a register; only an unset one is empty", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\n\nb\n");
        ex("put x");
        {
            INFO("a register never set: E353");
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("E353") != std::string::npos);
        }
        UNSCOPED_INFO("and nothing put");
        CHECK(flat(e.text()) == std::string("a||b|"));
        keys("jyyk");
        ex("put");
        UNSCOPED_INFO("an empty line is something to put");
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO(":put: the empty line below");
        CHECK(flat(e.text()) == std::string("a|||b|"));
        UNSCOPED_INFO("the caret on it");
        CHECK(caretText() == std::string("1:0"));
        ex("4put!");
        UNSCOPED_INFO(":4put!: above the fourth line");
        CHECK(flat(e.text()) == std::string("a||||b|"));
        UNSCOPED_INFO("the caret on it");
        CHECK(caretText() == std::string("3:0"));
        keys("gg2yy");
        ex("$put");
        UNSCOPED_INFO("two lines, the last empty: both put");
        CHECK(flat(e.text()) == std::string("a||||b|a||"));

        make("ab\n\nx\n");
        keys("<C-v>jy");
        ex("3put");
        UNSCOPED_INFO("a block's empty last row is a line as well");
        CHECK(flat(editor->text()) == std::string("ab||x|a||"));

        make("a\n\nb\n");
        ex("set clipboard=unnamed");
        keys("jyyk");
        ex("put");
        UNSCOPED_INFO("by the clipboard, which an empty line leaves empty");
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO("put all the same");
        CHECK(flat(editor->text()) == std::string("a|||b|"));
        ex("put +");
        UNSCOPED_INFO("and by \"+");
        CHECK(flat(editor->text()) == std::string("a||||b|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the black hole register gives back nothing and never the clipboard", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc def\nx\n");
        const std::string outside("outside");
        LLClipboard::instance().copyToClipboard(outside, 0, static_cast<S32>(outside.size()));
        keys("wyiwgg");
        keys("l\"_p");
        UNSCOPED_INFO("nothing said");
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO("\"_p puts nothing");
        CHECK(flat(e.text()) == std::string("abc def|x|"));
        UNSCOPED_INFO("the caret where it was");
        CHECK(caretText() == std::string("0:1"));
        keys("\"_P");
        UNSCOPED_INFO("nor does \"_P");
        CHECK(flat(e.text()) == std::string("abc def|x|"));
        keys("\"_3p");
        UNSCOPED_INFO("nothing said of a count either");
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO("nor does a count");
        CHECK(flat(e.text()) == std::string("abc def|x|"));
        ex("set clipboard=unnamed");
        keys("\"_gP");
        UNSCOPED_INFO("nor with the clipboard the unnamed register");
        CHECK(flat(e.text()) == std::string("abc def|x|"));
        UNSCOPED_INFO("the caret still where it was");
        CHECK(caretText() == std::string("0:1"));
        ex("put _");
        UNSCOPED_INFO("nothing said by :put _");
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO(":put _: an empty line");
        CHECK(flat(e.text()) == std::string("abc def||x|"));
        UNSCOPED_INFO("the caret on it");
        CHECK(caretText() == std::string("1:0"));
        keys("ggviw\"_p");
        UNSCOPED_INFO("visual \"_p: the selection taken out, nothing put");
        CHECK(flat(e.text()) == std::string(" def||x|"));
        UNSCOPED_INFO("the caret where it was");
        CHECK(caretText() == std::string("0:0"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: counted aw and iw go across line ends and fail where the words run out", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foo bar\nbaz qux\n");
        e.setCaret(ALTextPos(0, 4));
        keys("2daw");
        UNSCOPED_INFO("2daw from a line's last word: the next line's first and its blank too");
        CHECK(flat(e.text()) == std::string("foo qux|"));
        UNSCOPED_INFO("all it took kept");
        CHECK(vim->registerText('"') == std::string("bar\nbaz "));
        keys("u");
        e.setCaret(ALTextPos(0, 4));
        keys("3daw");
        UNSCOPED_INFO("3daw: the blank before, none being after");
        CHECK(flat(e.text()) == std::string("foo|"));
        keys("u");
        e.setCaret(ALTextPos(0, 4));
        keys("3diw");
        UNSCOPED_INFO("3diw: the next line's first word and the blank after it");
        CHECK(flat(e.text()) == std::string("foo qux|"));
        keys("u");
        e.setCaret(ALTextPos(0, 4));
        keys("v3awy");
        UNSCOPED_INFO("v3aw selects as much as 3daw takes");
        CHECK(vim->registerText('"') == std::string(" bar\nbaz qux"));

        make("foo bar\nbaz");
        editor->setCaret(ALTextPos(0, 4));
        keys("2yaw");
        UNSCOPED_INFO("2yaw to the text's last word");
        CHECK(vim->registerText('"') == std::string(" bar\nbaz"));
        editor->setCaret(ALTextPos(0, 4));
        keys("3yaw");
        UNSCOPED_INFO("3yaw runs out of words: nothing yanked");
        CHECK(vim->registerText('"') == std::string(" bar\nbaz"));

        make("a.b c.d\ne.f g.h\n");
        editor->setCaret(ALTextPos(0, 4));
        keys("2daW");
        UNSCOPED_INFO("2daW by WORDs");
        CHECK(flat(editor->text()) == std::string("a.b g.h|"));

        make("foo   \nbar baz\n");
        editor->setCaret(ALTextPos(0, 4));
        keys("daw");
        UNSCOPED_INFO("daw on a line's last blanks: the next line's first word with them");
        CHECK(flat(editor->text()) == std::string("foo baz|"));

        make("bar\n\nbaz qux\n");
        keys("2diw");
        UNSCOPED_INFO("2diw from a line's start over an empty line: the lines whole");
        CHECK(flat(editor->text()) == std::string("baz qux|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: aw on an empty line takes its break and next word; iw the empty line", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foo bar\n\nbaz qux\n");
        e.setCaret(ALTextPos(1, 0));
        keys("daw");
        UNSCOPED_INFO("daw on an empty line between lines of words");
        CHECK(flat(e.text()) == std::string("foo bar| qux|"));
        UNSCOPED_INFO("what it took");
        CHECK(vim->registerText('"') == std::string("\nbaz"));
        keys("u");
        e.setCaret(ALTextPos(1, 0));
        keys("vawy");
        UNSCOPED_INFO("vaw selects as much");
        CHECK(vim->registerText('"') == std::string("\nbaz"));
        e.setCaret(ALTextPos(1, 0));
        keys("yiw");
        UNSCOPED_INFO("yiw there yanks nothing");
        CHECK(vim->registerText('"') == std::string(""));
        keys("ciwX<Esc>");
        UNSCOPED_INFO("ciw there inserts on the empty line");
        CHECK(flat(e.text()) == std::string("foo bar|X|baz qux|"));

        make("foo bar\n\n\nbaz qux\n");
        editor->setCaret(ALTextPos(1, 0));
        keys("2diw");
        UNSCOPED_INFO("2diw from an empty line over another: the lines whole");
        CHECK(flat(editor->text()) == std::string("foo bar|baz qux|"));

        make("foo bar\n");
        editor->setCaret(ALTextPos(1, 0));
        keys("yiw");
        UNSCOPED_INFO("yiw on the last line, empty: back to the line before's last character and the break");
        CHECK(vim->registerText('"') == std::string("r\n"));
        editor->setCaret(ALTextPos(1, 0));
        keys("daw");
        UNSCOPED_INFO("daw there fails");
        CHECK(flat(editor->text()) == std::string("foo bar|"));
        UNSCOPED_INFO("and keeps nothing");
        CHECK(vim->registerText('"') == std::string("r\n"));
        editor->setCaret(ALTextPos(1, 0));
        keys("diw");
        UNSCOPED_INFO("diw there takes what yiw yanked");
        CHECK(flat(editor->text()) == std::string("foo ba"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a count before / or ? goes that many matches on round the ends", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a foo b foo c foo d foo\n");
        keys("3/foo<CR>");
        UNSCOPED_INFO("3/foo the third match on");
        CHECK(caretText() == std::string("0:14"));
        keys("$3?foo<CR>");
        UNSCOPED_INFO("3?foo the third back");
        CHECK(caretText() == std::string("0:8"));
        keys("02/foo/e<CR>");
        UNSCOPED_INFO("2/foo/e the second match's last character");
        CHECK(caretText() == std::string("0:10"));
        keys("06/foo<CR>");
        UNSCOPED_INFO("6/foo round past the end to the second");
        CHECK(caretText() == std::string("0:8"));
        keys("03n");
        UNSCOPED_INFO("3n the third on, as before");
        CHECK(caretText() == std::string("0:14"));
        UNSCOPED_INFO("nothing changed");
        CHECK(flat(e.text()) == std::string("a foo b foo c foo d foo|"));

        make("x\nfoo\ny\nfoo\nz\nfoo\nw\n");
        keys("2/foo/+1<CR>");
        UNSCOPED_INFO("2/foo/+1 the line after the second");
        CHECK(caretText() == std::string("4:0"));
        keys("ggv2/foo<CR>d");
        UNSCOPED_INFO("v2/foo takes the selection to the second");
        CHECK(flat(editor->text()) == std::string("oo|z|foo|w|"));
        UNSCOPED_INFO("and d took it");
        CHECK(vim->registerText('"') == std::string("x\nfoo\ny\nf"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: * # g* and g# after an operator go up to the word's next match", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foo bar foo baz\n");
        keys("d*");
        UNSCOPED_INFO("d* up to the next foo");
        CHECK(flat(e.text()) == std::string("foo baz|"));
        {
            INFO("normal mode, nothing pending");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().empty());
        }
        UNSCOPED_INFO("what it took in the register");
        CHECK(vim->registerText('"') == std::string("foo bar "));

        make("foo bar foo baz\n");
        editor->setCaret(ALTextPos(0, 9));
        keys("d#");
        UNSCOPED_INFO("d# from inside the second foo back to the first");
        CHECK(flat(editor->text()) == std::string("oo baz|"));
        UNSCOPED_INFO("the first foo to the caret taken");
        CHECK(vim->registerText('"') == std::string("foo bar f"));

        make("foo foobar foo\n");
        keys("dg*");
        UNSCOPED_INFO("dg* the word anywhere: up to the foo in foobar");
        CHECK(flat(editor->text()) == std::string("foobar foo|"));
        make("foo foobar foo\n");
        editor->setCaret(ALTextPos(0, 11));
        keys("dg#");
        UNSCOPED_INFO("dg# back to the foo in foobar");
        CHECK(flat(editor->text()) == std::string("foo foo|"));

        make("a x b x c x d\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("d2*");
        UNSCOPED_INFO("d2* up to the second x on");
        CHECK(flat(editor->text()) == std::string("a x d|"));
        keys("u");
        editor->setCaret(ALTextPos(0, 2));
        keys("2d*");
        UNSCOPED_INFO("and 2d*");
        CHECK(flat(editor->text()) == std::string("a x d|"));

        make("foo bar foo baz foo\n");
        keys("d*n");
        UNSCOPED_INFO("n after it to the next foo");
        CHECK(caretText() == std::string("0:8"));
        make("foo bar foo baz foo qux foo\n");
        keys("d*.");
        UNSCOPED_INFO(". again from where it left");
        CHECK(flat(editor->text()) == std::string("foo qux foo|"));

        make("foo x\nfoo y\n");
        keys("d*");
        UNSCOPED_INFO("to a line's first column from its start: the line whole");
        CHECK(flat(editor->text()) == std::string("foo y|"));

        make("foo bar baz\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("d*");
        UNSCOPED_INFO("a word found nowhere else: round to its own start, behind the caret");
        CHECK(flat(editor->text()) == std::string("o bar baz|"));

        make("foo   \nbar\n");
        keys("yl");
        editor->setCaret(ALTextPos(0, 4));
        keys("d*");
        {
            INFO("no word under the caret: the operator let go of");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().empty());
        }
        UNSCOPED_INFO("nothing taken");
        CHECK(flat(editor->text()) == std::string("foo   |bar|"));
        UNSCOPED_INFO("the register as it was");
        CHECK(vim->registerText('"') == std::string("f"));

        make("foo bar foo baz\n");
        editor->setCaret(ALTextPos(0, 9));
        keys("#");
        UNSCOPED_INFO("# from inside a word to the one before it, not to its own start");
        CHECK(caretText() == std::string("0:0"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a g key after an operator that is no motion fails; g operators doubled", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two\nTHREE FOUR\nfive six\n");
        keys("jjllia<Esc>");
        e.setCaret(ALTextPos(1, 0));
        keys("yw");
        const std::string text = flat(e.text());
        UNSCOPED_INFO("inserted on the third line");
        CHECK(text == std::string("one two|THREE FOUR|fiave six|"));
        for (const char* typed : { "dgi", "cgi", "ygi", "2dgi", "dgI", "dgJ", "dgp", "dg;", "dgt", "dgv", "dgrx", "dgx", "dgc", "cgu" })
        {
            const std::string what(typed);
            e.setCaret(ALTextPos(1, 3));
            keys(typed);
            {
                INFO(what << ": normal mode, nothing pending");
                REQUIRE(vim->mode() == ALVimKeymap::Mode::Normal);
                REQUIRE(vim->status().empty());
            }
            UNSCOPED_INFO(what << ": nothing changed");
            REQUIRE(flat(e.text()) == text);
            UNSCOPED_INFO(what << ": the register as it was");
            REQUIRE(vim->registerText('"') == std::string("THREE "));
            const std::string caret = what.find("gi") != std::string::npos ? "2:3" : what == "dgI" ? "1:0" : "1:3";
            UNSCOPED_INFO(what << ": the caret");
            REQUIRE(caretText() == caret);
        }
        e.setCaret(ALTextPos(1, 3));
        keys("dguw");
        UNSCOPED_INFO("dgu let go of, the w after it a motion of its own");
        CHECK(caretText() == std::string("1:6"));
        UNSCOPED_INFO("nothing lowered");
        CHECK(flat(e.text()) == text);
        keys("gugu");
        UNSCOPED_INFO("gugu lowers the line");
        CHECK(e.document().line(1) == std::string("three four"));
        UNSCOPED_INFO("the caret at its start");
        CHECK(caretText() == std::string("1:0"));
        keys("gUgU");
        UNSCOPED_INFO("gUgU raises it");
        CHECK(e.document().line(1) == std::string("THREE FOUR"));
        keys("g~g~");
        UNSCOPED_INFO("g~g~ swaps its case");
        CHECK(e.document().line(1) == std::string("three four"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: gv after an operator gives it the last visual area; . repeats as much", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two\nthree four\n");
        keys("vl<Esc>");
        e.setCaret(ALTextPos(1, 3));
        keys("dgv");
        UNSCOPED_INFO("dgv deletes what was selected");
        CHECK(flat(e.text()) == std::string("e two|three four|"));
        {
            INFO("normal mode, nothing pending");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().empty());
        }
        UNSCOPED_INFO("the caret where it began");
        CHECK(caretText() == std::string("0:0"));
        UNSCOPED_INFO("and the register");
        CHECK(vim->registerText('"') == std::string("on"));

        make("one two\nthree four\n");
        keys("wvl<Esc>");
        editor->setCaret(ALTextPos(1, 0));
        keys("ygv");
        UNSCOPED_INFO("ygv yanks it");
        CHECK(vim->registerText('"') == std::string("tw"));
        UNSCOPED_INFO("the caret to its start");
        CHECK(caretText() == std::string("0:4"));
        keys("cgvX<Esc>");
        UNSCOPED_INFO("cgv changes it");
        CHECK(flat(editor->text()) == std::string("one Xo|three four|"));
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);

        make("one two\nthree four\n");
        keys("Vj<Esc>");
        editor->setCaret(ALTextPos(0, 1));
        keys("gUgv");
        UNSCOPED_INFO("gUgv over the lines a line-wise one took");
        CHECK(flat(editor->text()) == std::string("ONE TWO|THREE FOUR|"));
        UNSCOPED_INFO("the caret at their start");
        CHECK(caretText() == std::string("0:0"));

        make("abc def ghi\nxyz\n");
        keys("wvl<Esc>0dgv");
        UNSCOPED_INFO("dgv from before the area");
        CHECK(flat(editor->text()) == std::string("abc f ghi|xyz|"));
        keys("0.");
        UNSCOPED_INFO(". as much again from the caret");
        CHECK(flat(editor->text()) == std::string("c f ghi|xyz|"));
        UNSCOPED_INFO("what . took");
        CHECK(vim->registerText('"') == std::string("ab"));

        make("abcd\nefgh\nijkl\n");
        keys("lj<C-v>l<Esc>gg0dgv");
        UNSCOPED_INFO("dgv over a block takes its columns");
        CHECK(flat(editor->text()) == std::string("abcd|eh|ijkl|"));
        UNSCOPED_INFO("the caret at its corner");
        CHECK(caretText() == std::string("1:1"));
        UNSCOPED_INFO("the block's text");
        CHECK(vim->registerText('"') == std::string("fg"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the last visual block remembers whether it was taken with $", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abcd\nefgh\n");
        keys("l<C-v>jl<Esc>$dgv");
        UNSCOPED_INFO("a block without $, then $: dgv takes its columns alone");
        CHECK(flat(e.text()) == std::string("ad|eh|"));
        e.setText("abcdefg\nabc\n");
        e.setCaret(ALTextPos(0, 0));
        keys("l<C-v>j$<Esc>0dgv");
        UNSCOPED_INFO("a block with $, then 0: dgv takes every line to its end");
        CHECK(flat(e.text()) == std::string("a|a|"));
        e.setText("abcdefg\nabc\n");
        e.setCaret(ALTextPos(0, 0));
        keys("l<C-v>j$<Esc>0gvd");
        UNSCOPED_INFO("gv selects it to every line's end again");
        CHECK(flat(e.text()) == std::string("a|a|"));
        e.setText("abcdefg\nabc\n");
        e.setCaret(ALTextPos(0, 0));
        keys("l<C-v>j$<Esc>0:%s/\\%V[a-z]/X/g<CR>");
        UNSCOPED_INFO("\\%V over a block with $: to every line's end");
        CHECK(flat(e.text()) == std::string("aXXXXXX|aXX|"));
        e.setText("abcdefg\nabc\n");
        e.setCaret(ALTextPos(0, 0));
        keys("l<C-v>jl<Esc>$:%s/\\%V[a-z]/X/g<CR>");
        UNSCOPED_INFO("\\%V over a block without $, after $: its columns alone");
        CHECK(flat(e.text()) == std::string("aXXdefg|aXX|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: visual p keeps what it replaced in the unnamed and - or 1; P does not", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two three four\n");
        keys("3wyiwbviwp");
        UNSCOPED_INFO("viwp: the word replaced");
        CHECK(flat(e.text()) == std::string("one two four four|"));
        UNSCOPED_INFO("the caret on the last character put");
        CHECK(caretText() == std::string("0:11"));
        UNSCOPED_INFO("what it replaced in the unnamed register");
        CHECK(vim->registerText('"') == std::string("three"));
        UNSCOPED_INFO("and in -, as a small delete's");
        CHECK(vim->registerText('-') == std::string("three"));
        UNSCOPED_INFO("the yank kept in 0");
        CHECK(vim->registerText('0') == std::string("four"));
        keys("0viwp");
        UNSCOPED_INFO("a further p puts what the last replaced");
        CHECK(flat(e.text()) == std::string("three two four four|"));
        UNSCOPED_INFO("and keeps what this one replaced");
        CHECK(vim->registerText('"') == std::string("one"));

        make("one two three four\n");
        keys("3wyiwbviwP");
        UNSCOPED_INFO("viwP: the word replaced");
        CHECK(flat(editor->text()) == std::string("one two four four|"));
        UNSCOPED_INFO("the unnamed register as it was");
        CHECK(vim->registerText('"') == std::string("four"));
        UNSCOPED_INFO("and - too");
        CHECK(vim->registerText('-') == std::string());
        keys("0viwP");
        UNSCOPED_INFO("a further P puts the same again");
        CHECK(flat(editor->text()) == std::string("four two four four|"));
        UNSCOPED_INFO("the caret on its last character");
        CHECK(caretText() == std::string("0:3"));

        make("one two three four\n");
        keys("\"xyiw3wyiwbviw\"xp");
        UNSCOPED_INFO("viw\"xp: the named register put");
        CHECK(flat(editor->text()) == std::string("one two one four|"));
        UNSCOPED_INFO("what it replaced in the unnamed register all the same");
        CHECK(vim->registerText('"') == std::string("three"));
        UNSCOPED_INFO("the named one as it was");
        CHECK(vim->registerText('x') == std::string("one"));
        keys("0viw\"_p");
        UNSCOPED_INFO("viw\"_p: nothing put");
        CHECK(flat(editor->text()) == std::string(" two one four|"));
        UNSCOPED_INFO("and what it took out in the unnamed register, as vim's has it");
        CHECK(vim->registerText('"') == std::string("one"));

        make("a\nb\nc\n");
        keys("yyjVp");
        UNSCOPED_INFO("V p: the line replaced");
        CHECK(flat(editor->text()) == std::string("a|a|c|"));
        UNSCOPED_INFO("the line it replaced in the unnamed register");
        CHECK(vim->registerText('"') == std::string("b"));
        UNSCOPED_INFO("and in 1, as a delete of a line's");
        CHECK(vim->registerText('1') == std::string("b"));
        keys("p");
        UNSCOPED_INFO("p puts it, a line");
        CHECK(flat(editor->text()) == std::string("a|a|b|c|"));
        UNSCOPED_INFO("the caret on it");
        CHECK(caretText() == std::string("2:0"));

        make("abcd\nefgh\nX\n");
        keys("2jylggl<C-v>jlp");
        UNSCOPED_INFO("a block's columns replaced");
        CHECK(flat(editor->text()) == std::string("aXd|eXh|X|"));
        UNSCOPED_INFO("what the block held in the unnamed register, a line each");
        CHECK(vim->registerText('"') == std::string("bc\nfg"));
        UNSCOPED_INFO("and in 1");
        CHECK(vim->registerText('1') == std::string("bc\nfg"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: visual block p of one line skips short lines but not those reaching it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abcd\na\nabcd\nZ\n");
        keys("3j\"xylgg2l<C-v>jj\"xp");
        UNSCOPED_INFO("the short line not padded out");
        CHECK(flat(e.text()) == std::string("abZd|a|abZd|Z|"));
        UNSCOPED_INFO("the caret at the block's corner");
        CHECK(caretText() == std::string("0:2"));

        make("abcd\n\nabcd\nZ\n");
        keys("3j\"xylgg2l<C-v>jj\"xp");
        UNSCOPED_INFO("nor an empty line");
        CHECK(flat(editor->text()) == std::string("abZd||abZd|Z|"));

        make("abcd\nab\nabcd\nZ\n");
        keys("3j\"xylgg2l<C-v>jj\"xp");
        UNSCOPED_INFO("a line that ends at the block's first column has it added");
        CHECK(flat(editor->text()) == std::string("abZd|abZ|abZd|Z|"));

        make("abcd\na\nabcd\n");
        keys("gg2l<C-v>jj\"_p");
        UNSCOPED_INFO("\"_p: the block taken out, the short line as it was");
        CHECK(flat(editor->text()) == std::string("abd|a|abd|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :put and :put! leave the caret at the last put line's first non-blank", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\n  q\n r\nb\n");
        keys("j\"x2yygg");
        ex("put x");
        UNSCOPED_INFO(":put x: two lines under the first");
        CHECK(flat(e.text()) == std::string("a|  q| r|  q| r|b|"));
        UNSCOPED_INFO("the caret on the second of them");
        CHECK(caretText() == std::string("2:1"));
        ex("1put! x");
        UNSCOPED_INFO(":1put! x: two lines over the first");
        CHECK(flat(e.text()) == std::string("  q| r|a|  q| r|  q| r|b|"));
        UNSCOPED_INFO("the caret on the second of them too");
        CHECK(caretText() == std::string("1:1"));

        make("1\n2\n3\n4\na\n\n");
        keys("4j\"x2yy");
        ex("3put x");
        UNSCOPED_INFO("a line and an empty one under the third");
        CHECK(flat(editor->text()) == std::string("1|2|3|a||4|a||"));
        UNSCOPED_INFO("the caret on the empty one");
        CHECK(caretText() == std::string("4:0"));

        make("1\n2\n3\n4\n5\n");
        keys("gg2yy");
        ex("$put");
        UNSCOPED_INFO("$put: under the last line");
        CHECK(flat(editor->text()) == std::string("1|2|3|4|5|1|2|"));
        UNSCOPED_INFO("the caret on the last of them");
        CHECK(caretText() == std::string("6:0"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :0put puts above the first line as :.-1put does there; lines from 0", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\n");
        keys("j\"xyy");
        ex("0put x");
        UNSCOPED_INFO(":0put x: above the first line");
        CHECK(flat(e.text()) == std::string("b|a|b|"));
        UNSCOPED_INFO("the caret on the line put");
        CHECK(caretText() == std::string("0:0"));

        make("a\n\nb\n");
        keys("jyy");
        ex("0put");
        UNSCOPED_INFO(":0put of an empty line: above the first");
        CHECK(flat(editor->text()) == std::string("|a||b|"));
        UNSCOPED_INFO("the caret on it");
        CHECK(caretText() == std::string("0:0"));

        make("a\nb\n");
        keys("j\"xyygg");
        ex("0put! x");
        UNSCOPED_INFO(":0put! x: above the first line as well");
        CHECK(flat(editor->text()) == std::string("b|a|b|"));

        make("a\nb\n");
        keys("j\"xyygg");
        ex(".-1put x");
        UNSCOPED_INFO(":.-1put x from the first line: above it");
        CHECK(flat(editor->text()) == std::string("b|a|b|"));

        make("a\nb\n");
        keys("j\"xyy");
        ex("0,1put x");
        UNSCOPED_INFO(":0,1put x: under the range's last line");
        CHECK(flat(editor->text()) == std::string("a|b|b|"));
        UNSCOPED_INFO("the caret on the line put");
        CHECK(caretText() == std::string("1:0"));

        ALCodeEditor& zero = make("a\nb\n");
        zero.setLineNumberBase(-1);
        keys("j\"xyy");
        ex("0put x");
        UNSCOPED_INFO("lines counted from 0: :0put x under the first, the line shown as 0");
        CHECK(flat(zero.text()) == std::string("a|b|b|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a count before a register's name multiplies one after it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("ab\n");
        keys("\"ayl3\"ap");
        UNSCOPED_INFO("3\"ap: put three times");
        CHECK(flat(e.text()) == std::string("aaaab|"));
        UNSCOPED_INFO("the caret on the last put");
        CHECK(caretText() == std::string("0:3"));

        make("ab\n");
        keys("\"ayl2\"a3");
        UNSCOPED_INFO("both counts shown as they were typed");
        CHECK(vim->status() == std::string("2\"a3"));
        keys("p");
        UNSCOPED_INFO("2\"a3p: six times");
        CHECK(flat(editor->text()) == std::string("aaaaaaab|"));
        UNSCOPED_INFO("the caret on the last put");
        CHECK(caretText() == std::string("0:6"));

        make("1\n2\n3\n4\n5\n");
        keys("3\"ayy");
        UNSCOPED_INFO("3\"ayy: three lines yanked into a");
        CHECK(vim->registerText('a') == std::string("1\n2\n3"));
        keys("2\"add");
        UNSCOPED_INFO("2\"add: two lines deleted");
        CHECK(flat(editor->text()) == std::string("3|4|5|"));
        UNSCOPED_INFO("into a");
        CHECK(vim->registerText('a') == std::string("1\n2"));

        make("1\n2\n3\n4\n5\n6\n7\n8\n");
        keys("2\"ad3d");
        UNSCOPED_INFO("2\"ad3d: six lines deleted");
        CHECK(flat(editor->text()) == std::string("7|8|"));
        UNSCOPED_INFO("into a");
        CHECK(vim->registerText('a') == std::string("1\n2\n3\n4\n5\n6"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: . with a count replaces the counts either side of a register's name", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("ab\n");
        keys("\"ayl\"a3p4.");
        UNSCOPED_INFO("\"a3p then 4.: four more");
        CHECK(flat(e.text()) == std::string("aaaaaaaab|"));

        make("ab\n");
        keys("\"ayl2\"a3p4.");
        UNSCOPED_INFO("2\"a3p then 4.: four more, not twelve");
        CHECK(flat(editor->text()) == std::string("aaaaaaaaaaab|"));

        make("ab\n");
        keys("\"ayl2\"a3p.");
        UNSCOPED_INFO("2\"a3p then .: six more");
        CHECK(flat(editor->text()) == std::string("aaaaaaaaaaaaab|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :put! over a range puts above its last line as :put puts under it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("1\n2\n3\n4\n");
        keys("\"xyy");
        ex("1,3put! x");
        UNSCOPED_INFO(":1,3put! x: above the third line");
        CHECK(flat(e.text()) == std::string("1|2|1|3|4|"));
        UNSCOPED_INFO("the caret on the line put");
        CHECK(caretText() == std::string("2:0"));
        e.setText("1\n2\n3\n4\n");
        e.setCaret(ALTextPos(0, 0));
        ex("2,3put x");
        UNSCOPED_INFO(":2,3put x: under the third line");
        CHECK(flat(e.text()) == std::string("1|2|3|1|4|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a multi-line delete of characters with only blanks outside takes lines", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("bar\nbaz");
        keys("2daw");
        UNSCOPED_INFO("2daw over a line's word and the next line's: lines");
        CHECK(vim->shared().registers.fetch('"', false).linewise);
        UNSCOPED_INFO("what it took");
        CHECK(vim->registerText('"') == std::string("bar\nbaz"));
        keys("p");
        UNSCOPED_INFO("put back as lines");
        CHECK(flat(e.text()) == std::string("|bar|baz"));

        make("foo bar\n\nbaz");
        editor->setCaret(ALTextPos(1, 0));
        keys("daw");
        UNSCOPED_INFO("daw on an empty line before a line's only word: both lines");
        CHECK(flat(editor->text()) == std::string("foo bar"));
        UNSCOPED_INFO("kept as lines");
        CHECK(vim->shared().registers.fetch('"', false).linewise);

        make("foo\nbar\nx\n");
        keys("d2e");
        UNSCOPED_INFO("d2e from a line's start to the next line's end");
        CHECK(flat(editor->text()) == std::string("x|"));
        make("foo\nbar\nx\n");
        keys("2D");
        UNSCOPED_INFO("2D from a line's start");
        CHECK(flat(editor->text()) == std::string("x|"));
        make("foo\nbar\nx\n");
        keys("d/r/e<CR>");
        UNSCOPED_INFO("d/r/e to the next line's last character");
        CHECK(flat(editor->text()) == std::string("x|"));
        make("foo\nbar");
        keys("d}p");
        UNSCOPED_INFO("d} over the last paragraph, put back as lines");
        CHECK(flat(editor->text()) == std::string("|foo|bar"));
        make("(\nx)\ny\n");
        keys("d%");
        UNSCOPED_INFO("d% from a bracket that begins its line");
        CHECK(flat(editor->text()) == std::string("y|"));
        make("  (\nx)  \ny\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("d%");
        UNSCOPED_INFO("from inside the indent, to blanks after");
        CHECK(flat(editor->text()) == std::string("y|"));
        UNSCOPED_INFO("the lines whole in the register");
        CHECK(vim->registerText('"') == std::string("  (\nx)  "));

        make("foo\nbar\nx\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("d2e");
        UNSCOPED_INFO("from after a line's first non-blank: characters");
        CHECK(flat(editor->text()) == std::string("f|x|"));
        make("(\nx) z\ny\n");
        keys("d%");
        UNSCOPED_INFO("to before a line's last non-blank: characters");
        CHECK(flat(editor->text()) == std::string(" z|y|"));
        make("(\nx)\ny\n");
        keys("c%Z<Esc>");
        UNSCOPED_INFO("c%: characters");
        CHECK(flat(editor->text()) == std::string("Z|y|"));
        make("(\nx)\ny\n");
        keys("y%");
        UNSCOPED_INFO("y%: characters");
        CHECK_FALSE(vim->shared().registers.fetch('"', false).linewise);
        keys("v%d");
        UNSCOPED_INFO("v%d: characters");
        CHECK(flat(editor->text()) == std::string("|y|"));
        make("foo\nbar\nx\n");
        keys("/foo\\nbar<CR>ggdgn");
        UNSCOPED_INFO("dgn over a match of two lines: characters");
        CHECK(flat(editor->text()) == std::string("|x|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: c over an empty line's end keeps an empty register; c0 and ci( not", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\n\nxyz\n");
        keys("xyiw");
        e.setCaret(ALTextPos(1, 0));
        keys("ciwX<Esc>");
        UNSCOPED_INFO("ciw on an empty line changes it");
        CHECK(flat(e.text()) == std::string("bc|X|xyz|"));
        UNSCOPED_INFO("and keeps its end, no text");
        CHECK(vim->registerText('"') == std::string());
        UNSCOPED_INFO("in - as well");
        CHECK(vim->registerText('-') == std::string());
        UNSCOPED_INFO("0 still the yank");
        CHECK(vim->registerText('0') == std::string("bc"));

        make("abc\n\nxyz\n");
        keys("yiw");
        editor->setCaret(ALTextPos(1, 0));
        keys("CX<Esc>");
        UNSCOPED_INFO("C on an empty line changes it");
        CHECK(flat(editor->text()) == std::string("abc|X|xyz|"));
        UNSCOPED_INFO("and keeps its end");
        CHECK(vim->registerText('"') == std::string());

        make("abc\n\nxyz\n");
        keys("yiw");
        editor->setCaret(ALTextPos(1, 0));
        keys("c$X<Esc>");
        UNSCOPED_INFO("c$ there keeps it too");
        CHECK(vim->registerText('"') == std::string());

        make("abc\n\nxyz\n");
        keys("yiw");
        editor->setCaret(ALTextPos(1, 0));
        keys("\"aciWX<Esc>");
        UNSCOPED_INFO("ciW into a register named: the unnamed one says it");
        CHECK(vim->registerText('"') == std::string());
        UNSCOPED_INFO("the yank still in 0");
        CHECK(vim->registerText('0') == std::string("abc"));

        make("abc\n\nxyz\n");
        keys("yiw");
        editor->setCaret(ALTextPos(1, 0));
        keys("c0X<Esc>");
        UNSCOPED_INFO("c0 there changes it");
        CHECK(flat(editor->text()) == std::string("abc|X|xyz|"));
        UNSCOPED_INFO("and keeps nothing");
        CHECK(vim->registerText('"') == std::string("abc"));

        make("f()\n");
        keys("yiwf(ci(X<Esc>");
        UNSCOPED_INFO("ci( between two brackets");
        CHECK(flat(editor->text()) == std::string("f(X)|"));
        UNSCOPED_INFO("keeps nothing");
        CHECK(vim->registerText('"') == std::string("f"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: deletes over searches % { } and marks go in 1 and -; dw in - alone", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a foo b foo c\n");
        keys("d/foo<CR>");
        UNSCOPED_INFO("d/foo within a line: in 1");
        CHECK(vim->registerText('1') == std::string("a "));
        UNSCOPED_INFO("and in -");
        CHECK(vim->registerText('-') == std::string("a "));
        keys("dn");
        UNSCOPED_INFO("what is left");
        CHECK(flat(e.text()) == std::string("foo c|"));
        UNSCOPED_INFO("dn: in 1");
        CHECK(vim->registerText('1') == std::string("foo b "));
        UNSCOPED_INFO("the one before moved along to 2");
        CHECK(vim->registerText('2') == std::string("a "));

        make("f(x) y\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("d%");
        CHECK(vim->registerText('1') == std::string("(x)"));

        make("foo bar\n");
        editor->setCaret(ALTextPos(0, 4));
        keys("d{");
        CHECK(vim->registerText('1') == std::string("foo "));

        make("foo bar\n");
        keys("4|ma0d`a");
        CHECK(vim->registerText('1') == std::string("foo"));

        make("foo bar foo\n");
        keys("d*");
        CHECK(vim->registerText('1') == std::string("foo bar "));

        make("a foo\n");
        keys("c/foo<CR>X<Esc>");
        UNSCOPED_INFO("c/foo as well");
        CHECK(vim->registerText('1') == std::string("a "));

        make("foo bar\n");
        keys("dw");
        UNSCOPED_INFO("dw: in -");
        CHECK(vim->registerText('-') == std::string("foo "));
        UNSCOPED_INFO("and not in 1");
        CHECK(vim->registerText('1') == std::string());
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: doubled case operators keep the caret with a count; else ^ or before", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc def\nghi jkl\nmno\n");
        e.setCaret(ALTextPos(0, 3));
        keys("2gUgU");
        UNSCOPED_INFO("2gUgU raises two lines");
        CHECK(flat(e.text()) == std::string("ABC DEF|GHI JKL|mno|"));
        UNSCOPED_INFO("the caret where it was");
        CHECK(caretText() == std::string("0:3"));
        keys("gu2gu");
        UNSCOPED_INFO("gu2gu lowers them again");
        CHECK(flat(e.text()) == std::string("abc def|ghi jkl|mno|"));
        UNSCOPED_INFO("the caret still where it was");
        CHECK(caretText() == std::string("0:3"));
        keys("2g~~");
        UNSCOPED_INFO("2g~~ swaps them");
        CHECK(flat(e.text()) == std::string("ABC DEF|GHI JKL|mno|"));
        UNSCOPED_INFO("and leaves it too");
        CHECK(caretText() == std::string("0:3"));
        e.setCaret(ALTextPos(1, 5));
        keys("2guu");
        UNSCOPED_INFO("2guu from the second line");
        CHECK(flat(e.text()) == std::string("ABC DEF|ghi jkl|mno|"));
        UNSCOPED_INFO("where it was");
        CHECK(caretText() == std::string("1:5"));

        make("   abc def\nghi\n");
        editor->setCaret(ALTextPos(0, 5));
        keys("gUU");
        UNSCOPED_INFO("gUU from past the indent");
        CHECK(editor->document().line(0) == std::string("   ABC DEF"));
        UNSCOPED_INFO("the caret to the line's first non-blank");
        CHECK(caretText() == std::string("0:3"));
        editor->setCaret(ALTextPos(0, 1));
        keys("guu");
        UNSCOPED_INFO("guu from inside the indent");
        CHECK(editor->document().line(0) == std::string("   abc def"));
        UNSCOPED_INFO("the caret where it was");
        CHECK(caretText() == std::string("0:1"));
        editor->setCaret(ALTextPos(0, 7));
        keys("g~g~");
        CHECK(editor->document().line(0) == std::string("   ABC DEF"));
        UNSCOPED_INFO("the caret to the first non-blank");
        CHECK(caretText() == std::string("0:3"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: * # g* g# on a blank take the next word or else the characters there", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foo bar baz\n");
        e.setCaret(ALTextPos(0, 3));
        keys("d*");
        UNSCOPED_INFO("d* on the blank up to the bar after it, found again round the end");
        CHECK(flat(e.text()) == std::string("foobar baz|"));
        UNSCOPED_INFO("the blank taken");
        CHECK(vim->registerText('"') == std::string(" "));

        make("foo bar baz bar\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("*");
        UNSCOPED_INFO("* on the blank to the next whole bar");
        CHECK(caretText() == std::string("0:12"));
        keys("n");
        UNSCOPED_INFO("n goes on with it");
        CHECK(caretText() == std::string("0:4"));

        make("bar foo bar baz\n");
        editor->setCaret(ALTextPos(0, 7));
        keys("d#");
        UNSCOPED_INFO("d# on the blank back to the bar before the next one");
        CHECK(flat(editor->text()) == std::string(" bar baz|"));
        UNSCOPED_INFO("what d# took");
        CHECK(vim->registerText('"') == std::string("bar foo"));

        make("foo bar baz barx\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("dg*");
        UNSCOPED_INFO("dg* on the blank up to the bar in barx");
        CHECK(flat(editor->text()) == std::string("foobarx|"));

        make("x+=1\na +=\n");
        editor->setCaret(ALTextPos(1, 2));
        keys("*");
        UNSCOPED_INFO("no word after the caret: += looked for as it is, round the end");
        CHECK(caretText() == std::string("0:1"));
        editor->setCaret(ALTextPos(1, 3));
        keys("*");
        UNSCOPED_INFO("from the middle of the run, the run whole");
        CHECK(caretText() == std::string("0:1"));
        editor->setCaret(ALTextPos(1, 2));
        keys("d*");
        UNSCOPED_INFO("d* back round to the += before it");
        CHECK(flat(editor->text()) == std::string("x+=|"));

        make("x.*1\na .*\n");
        editor->setCaret(ALTextPos(1, 2));
        keys("*");
        UNSCOPED_INFO(".* looked for as itself, not as a pattern");
        CHECK(caretText() == std::string("0:1"));

        make("foo.bar foo\n");
        keys("*");
        UNSCOPED_INFO("the word is foo, as vim's classes have it, not foo.bar");
        CHECK(caretText() == std::string("0:8"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: g0 g^ gm g$ gM and go are motions alone and after an operator", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("dg0");
        UNSCOPED_INFO("dg0 back to the screen line's start, exclusive");
        CHECK(flat(e.text()) == std::string("ee four|"));
        {
            INFO("normal mode, nothing pending");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().empty());
        }
        e.setText("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("dg$");
        UNSCOPED_INFO("dg$ through its last character");
        CHECK(flat(e.text()) == std::string("thr|"));
        e.setText("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("dgm");
        UNSCOPED_INFO("dgm up to the last character, the view's middle being past it");
        CHECK(flat(e.text()) == std::string("thrr|"));
        e.setText("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("dgo");
        UNSCOPED_INFO("dgo back to the text's first byte");
        CHECK(flat(e.text()) == std::string("ee four|"));
        e.setText("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("dgM");
        UNSCOPED_INFO("dgM up to the middle of the line's text");
        CHECK(flat(e.text()) == std::string("thr four|"));
        UNSCOPED_INFO("what dgM took");
        CHECK(vim->registerText('"') == std::string("ee"));

        e.setText("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("g$");
        UNSCOPED_INFO("g$ to the last character");
        CHECK(caretText() == std::string("0:9"));
        keys("g0");
        UNSCOPED_INFO("g0 to the first");
        CHECK(caretText() == std::string("0:0"));
        keys("gm");
        UNSCOPED_INFO("gm to the last, the view's middle being past it");
        CHECK(caretText() == std::string("0:9"));
        keys("gM");
        UNSCOPED_INFO("gM to the middle of the text");
        CHECK(caretText() == std::string("0:5"));

        e.setText("  three four\n");
        e.setCaret(ALTextPos(0, 7));
        keys("g^");
        UNSCOPED_INFO("g^ to the first not blank");
        CHECK(caretText() == std::string("0:2"));
        e.setCaret(ALTextPos(0, 7));
        keys("dg^");
        UNSCOPED_INFO("dg^ back to it");
        CHECK(flat(e.text()) == std::string("   four|"));

        e.setText("0123456789\n");
        e.setCaret(ALTextPos(0, 0));
        keys("20gM");
        UNSCOPED_INFO("20gM a fifth along");
        CHECK(caretText() == std::string("0:2"));
        keys("0101gM");
        UNSCOPED_INFO("past a hundred, the middle");
        CHECK(caretText() == std::string("0:5"));
        keys("02d10gM");
        UNSCOPED_INFO("2d10gM: the counts' product, a fifth along");
        CHECK(flat(e.text()) == std::string("23456789|"));

        e.setText("abc\ndef\nghi");
        e.setCaret(ALTextPos(2, 1));
        keys("go");
        UNSCOPED_INFO("go to the first byte");
        CHECK(caretText() == std::string("0:0"));
        keys("``");
        UNSCOPED_INFO("a jump, `` going back");
        CHECK(caretText() == std::string("2:1"));
        keys("6go");
        UNSCOPED_INFO("6go, a line's break one byte");
        CHECK(caretText() == std::string("1:1"));
        keys("4go");
        UNSCOPED_INFO("a break's byte, the character before it");
        CHECK(caretText() == std::string("0:2"));
        keys("99go");
        UNSCOPED_INFO("past the end, the last character");
        CHECK(caretText() == std::string("2:2"));
        e.setCaret(ALTextPos(0, 1));
        keys("d6go");
        UNSCOPED_INFO("d6go over the break");
        CHECK(flat(e.text()) == std::string("aef|ghi"));
        e.setText("abc\ndef\nghi");
        e.setCaret(ALTextPos(1, 2));
        keys("2d3go");
        UNSCOPED_INFO("2d3go: the counts' product");
        CHECK(flat(e.text()) == std::string("abc|df|ghi"));

        // A count of g$ is screen lines down; unwrapped, each line one, and
        // the last line where they run out.
        e.setText("one\ntwo two\nthree");
        e.setCaret(ALTextPos(0, 1));
        keys("2g$");
        UNSCOPED_INFO("2g$ to the next line's last");
        CHECK(caretText() == std::string("1:6"));
        e.setCaret(ALTextPos(0, 1));
        keys("9g$");
        UNSCOPED_INFO("9g$ to the last line's");
        CHECK(caretText() == std::string("2:4"));
        e.setCaret(ALTextPos(0, 1));
        keys("d2g$");
        UNSCOPED_INFO("d2g$ through the next line's last");
        CHECK(flat(e.text()) == std::string("o|three"));

        e.setText("ab\n\ncd");
        e.setCaret(ALTextPos(1, 0));
        keys("dg$");
        UNSCOPED_INFO("dg$ on an empty line takes nothing, the break neither");
        CHECK(flat(e.text()) == std::string("ab||cd"));

        // Unwrapped and wider than the view: what is in sight.
        const std::string wide(150, 'x');
        e.setText(wide + "\n");
        e.setCaret(ALTextPos(0, 0));
        const F32 width = static_cast<F32>(e.textRect().getWidth());
        keys("g$");
        {
            INFO("g$ to the last character in sight");
            REQUIRE(e.caret().column + 1 < static_cast<S32>(wide.size()));
            CHECK(e.layout().xOf(0, e.caret().column) <= width - 1.f);
            CHECK(e.layout().xOf(0, e.caret().column + 1) > width - 1.f);
        }
        e.setCaret(ALTextPos(0, 0));
        keys("gm");
        {
            INFO("gm to the one half the view across");
            CHECK(e.layout().xOf(0, e.caret().column) <= width / 2.f);
            CHECK(e.layout().xOf(0, e.caret().column + 1) > width / 2.f);
        }
        keys("$");
        const F32 left = e.scrollX();
        keys("g0");
        {
            INFO("g0 to the first character in sight");
            CHECK(left > 0.f);
            CHECK(e.layout().xOf(0, e.caret().column) <= left);
            CHECK(e.layout().xOf(0, e.caret().column + 1) > left);
        }

        // Wrapped, a screen line is a row of the line.
        const std::string line = "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll";
        e.setText(line + "\n");
        e.setWordWrap(true);
        e.reshape(120, 200);
        const std::vector<ALTextLayout::Row> rows = e.layout().line(0).rows;
        {
            INFO("the line wraps to three rows or more, of three characters or more");
            REQUIRE(rows.size() >= 3);
            CHECK(rows[1].end - rows[1].begin >= 3);
        }
        e.setCaret(ALTextPos(0, rows[1].begin + 2));
        keys("g0");
        UNSCOPED_INFO("g0 to the row's first character");
        CHECK(e.caret() == ALTextPos(0, rows[1].begin));
        keys("g$");
        UNSCOPED_INFO("g$ to its last");
        CHECK(e.caret() == ALTextPos(0, rows[1].end - 1));
        keys("gm");
        UNSCOPED_INFO("gm on the row");
        CHECK(e.layout().rowOf(0, e.caret().column) == 1);
        e.setCaret(ALTextPos(0, rows[1].begin + 2));
        keys("2g$");
        UNSCOPED_INFO("2g$ to the end of the row below");
        CHECK(e.caret() == ALTextPos(0, rows[2].end - 1));
        e.setCaret(ALTextPos(0, rows[1].begin + 2));
        keys("d99g$");
        UNSCOPED_INFO("d99g$ runs out of rows and fails, nothing taken");
        CHECK(e.document().line(0) == line);
        e.setCaret(ALTextPos(0, rows[1].begin + 2));
        keys("dg0");
        const size_t row_begin = static_cast<size_t>(rows[1].begin);
        const size_t row_end   = static_cast<size_t>(rows[1].end);
        UNSCOPED_INFO("dg0 from the row's start");
        CHECK(e.document().line(0) == line.substr(0, row_begin) + line.substr(row_begin + 2));
        e.setText(line + "\n");
        e.setCaret(ALTextPos(0, rows[1].begin + 1));
        keys("dg$");
        UNSCOPED_INFO("dg$ through the row's last");
        CHECK(e.document().line(0) == line.substr(0, row_begin + 1) + line.substr(row_end));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: * # g* and g# put their search in the history; . of one puts it again", "[alvimkeymap]")
    {
        typedef std::vector<std::string> history_t;
        make("foo b bar c foo d bar e foo f foo\n");
        keys("d*");
        UNSCOPED_INFO("d* puts \\<foo\\> in the search history");
        CHECK(vim->shared().search == history_t{ "\\<foo\\>" });
        keys("/bar<CR>");
        keys(".");
        UNSCOPED_INFO("and . of it the word under the caret then");
        CHECK(vim->shared().search == history_t{ "\\<foo\\>", "bar", "\\<bar\\>" });

        make("foo foobar\n");
        keys("g*");
        UNSCOPED_INFO("g* the word as it is");
        CHECK(vim->shared().search == history_t{ "foo" });
        keys("0*");
        UNSCOPED_INFO("* the word whole, after it");
        CHECK(vim->shared().search == history_t{ "foo", "\\<foo\\>" });

        make("x.*1\na .*\n");
        editor->setCaret(ALTextPos(1, 2));
        keys("*");
        UNSCOPED_INFO("other characters as they are looked for");
        CHECK(vim->shared().search == history_t{ "\\.\\*" });

        make("foo x foo\n");
        keys("**#");
        UNSCOPED_INFO("the same again is there once");
        CHECK(vim->shared().search == history_t{ "\\<foo\\>" });

        make("a foo b bar c foo d bar e foo\n");
        keys("d/foo<CR>/bar<CR>.");
        UNSCOPED_INFO(". of d/foo up to the next foo");
        CHECK(flat(editor->text()) == std::string("foo b foo d bar e foo|"));
        UNSCOPED_INFO("foo moved last in the history, and there once");
        CHECK(vim->shared().search == history_t{ "bar", "foo" });
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a charwise selection past a line's end takes its break; . counts it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one\n\ntwo\n");
        e.setCaret(ALTextPos(1, 0));
        keys("vd");
        UNSCOPED_INFO("v d on an empty line takes the line");
        CHECK(flat(e.text()) == std::string("one|two|"));
        UNSCOPED_INFO("its break in the register");
        CHECK(vim->registerText('"') == std::string("\n"));
        UNSCOPED_INFO("the caret where it began");
        CHECK(caretText() == std::string("1:0"));

        make("foo\n\nbar\n");
        editor->setCaret(ALTextPos(1, 0));
        keys("viwd");
        UNSCOPED_INFO("viw d there too");
        CHECK(flat(editor->text()) == std::string("foo|bar|"));

        make("abc\n\ndef\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("vjd");
        UNSCOPED_INFO("v j onto an empty line: through its break");
        CHECK(flat(editor->text()) == std::string("adef|"));
        UNSCOPED_INFO("what it took");
        CHECK(vim->registerText('"') == std::string("bc\n\n"));

        make("abc\nq\nxyz\nr\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("vly");
        UNSCOPED_INFO("l past the last character: y takes the break");
        CHECK(vim->registerText('"') == std::string("c\n"));
        keys("vld");
        UNSCOPED_INFO("and d joins the next line on");
        CHECK(flat(editor->text()) == std::string("abq|xyz|r|"));
        keys("j0.");
        UNSCOPED_INFO(". as many again, the break counted as a character");
        CHECK(flat(editor->text()) == std::string("abq|z|r|"));
        UNSCOPED_INFO("what . took");
        CHECK(vim->registerText('"') == std::string("xy"));

        make("a\n\nb\nxyz\n");
        editor->setCaret(ALTextPos(1, 0));
        keys("vdj0.");
        UNSCOPED_INFO(". after an empty line's break: one character");
        CHECK(flat(editor->text()) == std::string("a|b|yz|"));

        make("abcdef\nab\nz\n");
        keys("vlldj0.");
        UNSCOPED_INFO(". over a line shorter than it: past its last, its break too");
        CHECK(flat(editor->text()) == std::string("def|z|"));

        make("abc\ndef\nghi\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("vl~");
        UNSCOPED_INFO("~ over the break: the text recased, nothing joined");
        CHECK(flat(editor->text()) == std::string("abC|def|ghi|"));
        keys("vlJ");
        UNSCOPED_INFO("J over lines takes no break: two lines joined, not three");
        CHECK(flat(editor->text()) == std::string("abC def|ghi|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: $ in visual goes onto the line's break; v$ y d c p and . take it", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("hree four\nfive six\n");
        keys("v$y");
        UNSCOPED_INFO("v$y yanks the line and its break");
        CHECK(vim->registerText('"') == std::string("hree four\n"));
        keys("v$d");
        UNSCOPED_INFO("v$d joins the next line on");
        CHECK(flat(e.text()) == std::string("five six|"));

        make("hree four\nfive six\n");
        keys("v$cX<Esc>");
        UNSCOPED_INFO("v$c changes the line and its break");
        CHECK(flat(editor->text()) == std::string("Xfive six|"));

        make("X\nhree four\nfive six\n");
        keys("\"aylj0v$\"ap");
        UNSCOPED_INFO("v$p puts in place of the line and its break");
        CHECK(flat(editor->text()) == std::string("X|Xfive six|"));

        make("abc\ndef\n");
        keys("v$hd");
        UNSCOPED_INFO("h from past the end onto the last character, which takes no break");
        CHECK(flat(editor->text()) == std::string("|def|"));

        make("ab\ncdefgh\nq\n");
        keys("v$d.");
        UNSCOPED_INFO(". to the end of a longer line, and its break");
        CHECK(flat(editor->text()) == std::string("q|"));

        make("longer\nab\n");
        editor->setCaret(ALTextPos(0, 5));
        keys("<C-v>j$d");
        UNSCOPED_INFO("a block taken with $ onto a shorter line: from past that line's end");
        CHECK(flat(editor->text()) == std::string("lo|ab|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a text object over a selection of more than one character extends it", "[alvimkeymap]")
    {
        const char* words = "one two three four\nfive six\n";
        // Each a selection made from a place, then yanked: what it covers.
        const auto selects = [&](const char* text, S32 line, S32 column, const char* typed) {
            make(text);
            editor->setCaret(ALTextPos(line, column));
            keys(typed);
            keys("y");
            return vim->registerText('"');
        };
        UNSCOPED_INFO("viw iw: on by the blank after");
        CHECK(selects(words, 0, 4, "viwiw") == std::string("two "));
        UNSCOPED_INFO("viw iw iw: and the next word");
        CHECK(selects(words, 0, 4, "viwiwiw") == std::string("two three"));
        UNSCOPED_INFO("vaw aw: a word and its blank more");
        CHECK(selects(words, 0, 4, "vawaw") == std::string("two three "));
        UNSCOPED_INFO("vh iw: back from a caret before the anchor, over the blank");
        CHECK(selects(words, 0, 5, "vhiw") == std::string(" tw"));
        UNSCOPED_INFO("vh iw iw: and the word before");
        CHECK(selects(words, 0, 5, "vhiwiw") == std::string("one tw"));
        UNSCOPED_INFO("vh aw aw: back by words and their blanks");
        CHECK(selects(words, 0, 9, "vhawaw") == std::string("one two th"));
        UNSCOPED_INFO("vj iw: from the caret on the next line");
        CHECK(selects(words, 0, 4, "vjiw") == std::string("two three four\nfive six"));
        UNSCOPED_INFO("vk aw: back from the caret on the line above");
        CHECK(selects(words, 1, 5, "vkaw") == std::string(" two three four\nfive s"));
        UNSCOPED_INFO("viw iw at a line's last word: the next line's first");
        CHECK(selects(words, 0, 14, "viwiw") == std::string("four\nfive"));
        UNSCOPED_INFO("vh iw iw from a line's start: back over the break");
        CHECK(selects(words, 1, 1, "vhiwiw") == std::string(" four\nfi"));
        UNSCOPED_INFO("vh iw at the text's start: nothing to take, the selection as it was");
        CHECK(selects(words, 0, 1, "vhiw") == std::string("on"));
        make(words);
        editor->setCaret(ALTextPos(0, 4));
        keys("Vjiw");
        UNSCOPED_INFO("V j iw: lines become characters");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        keys("y");
        UNSCOPED_INFO("from the caret on");
        CHECK(vim->registerText('"') == std::string("two three four\nfive six"));

        const char* brackets = "f(a (b c) d) (e)\n";
        UNSCOPED_INFO("vi( i(: inside the block around");
        CHECK(selects(brackets, 0, 5, "vi(i(") == std::string("a (b c) d"));
        UNSCOPED_INFO("va( a(: the block around");
        CHECK(selects(brackets, 0, 5, "va(a(") == std::string("(a (b c) d)"));
        UNSCOPED_INFO("vl i(: inside, more than was selected");
        CHECK(selects(brackets, 0, 5, "vli(") == std::string("b c"));
        UNSCOPED_INFO("vl from a ( i(: inside the block around it");
        CHECK(selects(brackets, 0, 4, "vli(") == std::string("a (b c) d"));
        UNSCOPED_INFO("vi( a(: the block whole");
        CHECK(selects(brackets, 0, 5, "vi(a(") == std::string("(b c)"));
        UNSCOPED_INFO("vi( i( i(: no block further out, the selection as it was");
        CHECK(selects(brackets, 0, 5, "vi(i(i(") == std::string("a (b c) d"));
        UNSCOPED_INFO("i( over an empty block: nothing");
        CHECK(selects("x () y\n", 0, 1, "vlli(") == std::string(" ()"));

        const char* tags = "<a><b>x y</b> z</a>\n";
        UNSCOPED_INFO("vit it: the tags as well");
        CHECK(selects(tags, 0, 7, "vitit") == std::string("<b>x y</b>"));
        UNSCOPED_INFO("vit it it: inside the tag around");
        CHECK(selects(tags, 0, 7, "vititit") == std::string("<b>x y</b> z"));
        UNSCOPED_INFO("vat at: the tag around");
        CHECK(selects(tags, 0, 7, "vatat") == std::string("<a><b>x y</b> z</a>"));
        UNSCOPED_INFO("vl it: inside, more than was selected");
        CHECK(selects(tags, 0, 6, "vlit") == std::string("x y"));
        UNSCOPED_INFO("it over a selection past the tag's close: the tag around");
        CHECK(selects(tags, 0, 8, "vllllllit") == std::string("<b>x y</b> z"));

        const char* quotes = "x \"ab cd\" y \"ef\" z\n";
        UNSCOPED_INFO("vi\" i\": the quotes too");
        CHECK(selects(quotes, 0, 4, "vi\"i\"") == std::string("\"ab cd\""));
        UNSCOPED_INFO("vi\" i\" i\": on to the next quoted text");
        CHECK(selects(quotes, 0, 4, "vi\"i\"i\"") == std::string("\"ab cd\" y \"ef"));
        UNSCOPED_INFO("va\" a\": on to the next, its blank after");
        CHECK(selects(quotes, 0, 4, "va\"a\"") == std::string("\"ab cd\" y \"ef\" "));
        UNSCOPED_INFO("vl i\": inside the quotes");
        CHECK(selects(quotes, 0, 4, "vli\"") == std::string("ab cd"));
        UNSCOPED_INFO("vh i\": the same, the caret before the anchor");
        CHECK(selects(quotes, 0, 5, "vhi\"") == std::string("ab cd"));
        UNSCOPED_INFO("i\" over a selection past a closing quote: to the next quoted text");
        CHECK(selects(quotes, 0, 4, "vllllllllli\"") == std::string("b cd\" y \"ef"));
        UNSCOPED_INFO("i\" over two lines: nothing");
        CHECK(selects("\"a\" b\n\"c\" d\n", 0, 4, "vji\"") == std::string("b\n\"c\" d"));

        const char* paragraphs = "a\nb\n\nc\n\n\nd\ne\n";
        UNSCOPED_INFO("vip ip: the blank line after");
        CHECK(selects(paragraphs, 0, 0, "vipip") == std::string("a\nb\n"));
        UNSCOPED_INFO("vip ip ip: and the paragraph after");
        CHECK(selects(paragraphs, 0, 0, "vipipip") == std::string("a\nb\n\nc"));
        UNSCOPED_INFO("vap ap: a paragraph and its blank lines more");
        CHECK(selects(paragraphs, 0, 0, "vapap") == std::string("a\nb\n\nc\n\n"));
        UNSCOPED_INFO("V ip on a paragraph's first line: on from it, past a paragraph of one");
        CHECK(selects(paragraphs, 3, 0, "Vip") == std::string("c\n\n"));
        UNSCOPED_INFO("vj ip: by characters, to the blank line's start");
        CHECK(selects(paragraphs, 0, 0, "vjip") == std::string("a\nb\n\n"));
        UNSCOPED_INFO("vk ip: back over the blank lines");
        CHECK(selects(paragraphs, 7, 0, "vkip") == std::string("\n\nd\ne"));
        UNSCOPED_INFO("Vj ap: on by the blank lines and the paragraph after");
        CHECK(selects("a\nx\n\nb\nc\n\nd\n", 0, 0, "Vjap") == std::string("a\nx\n\nb\nc"));
        UNSCOPED_INFO("a count past the text's end: as far as it goes");
        CHECK(selects("a\nb\n\nc\n", 0, 0, "vj5ip") == std::string("a\nb\n\nc\n"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a text object that is not there fails the macro operator and count", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\ndef\nghi\njkl\n");
        keys("qaxdi(jq");
        UNSCOPED_INFO("recorded: x done, di( found nothing, j done");
        CHECK(flat(e.text()) == std::string("bc|def|ghi|jkl|"));
        UNSCOPED_INFO("the caret on the next line");
        CHECK(caretText() == std::string("1:0"));
        keys("@a");
        UNSCOPED_INFO("@a: x, then di( fails and the j after it is not done");
        CHECK(flat(e.text()) == std::string("bc|ef|ghi|jkl|"));
        UNSCOPED_INFO("the caret still on the line");
        CHECK(caretText() == std::string("1:0"));
        keys("3@a");
        UNSCOPED_INFO("3@a: the first play's di( stops the count");
        CHECK(flat(e.text()) == std::string("bc|f|ghi|jkl|"));
        UNSCOPED_INFO("the caret still there");
        CHECK(caretText() == std::string("1:0"));

        make("abc\ndef\nghi\n");
        keys("qbvi(<Esc>xjq");
        UNSCOPED_INFO("recorded over a selection");
        CHECK(flat(editor->text()) == std::string("bc|def|ghi|"));
        keys("@b");
        UNSCOPED_INFO("@b: vi( fails, nothing after it done");
        CHECK(flat(editor->text()) == std::string("bc|def|ghi|"));
        UNSCOPED_INFO("the selection still there");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        keys("y");
        UNSCOPED_INFO("as it was");
        CHECK(vim->registerText('"') == std::string("d"));

        make("abcdef\n");
        keys("xdi(.");
        UNSCOPED_INFO(". after it does the x again");
        CHECK(flat(editor->text()) == std::string("cdef|"));
        keys("2di(x");
        UNSCOPED_INFO("the count let go of with it");
        CHECK(flat(editor->text()) == std::string("def|"));
        keys("di(w");
        UNSCOPED_INFO("and the operator: w a motion of its own");
        CHECK(flat(editor->text()) == std::string("def|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: . after c over a visual area changes as much again with the same text", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two\nthree four\n");
        keys("wvl<Esc>cgvXY<Esc>");
        UNSCOPED_INFO("cgv changes the last visual area");
        CHECK(flat(e.text()) == std::string("one XYo|three four|"));
        keys("0.");
        UNSCOPED_INFO(". as many characters again from the caret, the same text typed");
        CHECK(flat(e.text()) == std::string("XYe XYo|three four|"));
        UNSCOPED_INFO("what . took");
        CHECK(vim->registerText('"') == std::string("on"));

        make("a\nb\nc\nd\ne\n");
        keys("Vj<Esc>jjcgvX<Esc>j.");
        UNSCOPED_INFO("cgv over lines, then . over as many lines");
        CHECK(flat(editor->text()) == std::string("X|X|e|"));

        make("ab cdef gh\n");
        keys("viwcX<Esc>w.");
        UNSCOPED_INFO("viw c, then . over as many characters as the word had, not the word there");
        CHECK(flat(editor->text()) == std::string("X Xef gh|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: i< and a< take the angle brackets around the caret; % pairs none", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("f(<a <b> c>, x);\n");
        keys("0fbdi<");
        UNSCOPED_INFO("i< inside the innermost pair");
        CHECK(flat(e.text()) == std::string("f(<a <> c>, x);|"));
        keys("u0fbd2a<");
        UNSCOPED_INFO("2a< the pair a level out, whole");
        CHECK(flat(e.text()) == std::string("f(, x);|"));
        keys("u0fcda>");
        UNSCOPED_INFO("a> from after a closed pair: the one still open before it");
        CHECK(flat(e.text()) == std::string("f(, x);|"));
        keys("u0fbva<d");
        UNSCOPED_INFO("va< in visual");
        CHECK(flat(e.text()) == std::string("f(<a  c>, x);|"));
        keys("u0f<%");
        UNSCOPED_INFO("% from an angle bracket goes on to the round one after it");
        CHECK(caretText() == std::string("0:1"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: an operator over a motion that cannot move takes the empty stretch", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("ab\n\nc\n");
        keys("yljclX<Esc>");
        UNSCOPED_INFO("cl on an empty line inserts there");
        CHECK(flat(e.text()) == std::string("ab|X|c|"));
        UNSCOPED_INFO("and keeps nothing");
        CHECK(vim->registerText('"') == std::string("a"));

        make("abc\n");
        keys("chX<Esc>");
        UNSCOPED_INFO("ch at a line's start inserts there");
        CHECK(flat(editor->text()) == std::string("Xabc|"));

        make("a\n");
        keys("jcwX<Esc>");
        UNSCOPED_INFO("cw on the last line, empty");
        CHECK(flat(editor->text()) == std::string("a|X"));

        make("a\n");
        keys("yljceX<Esc>");
        UNSCOPED_INFO("ce there too");
        CHECK(flat(editor->text()) == std::string("a|X"));
        UNSCOPED_INFO("keeping the empty end it took, as C does");
        CHECK(vim->registerText('"') == std::string());

        make("abc\n");
        keys("lyl0yh");
        UNSCOPED_INFO("yh at a line's start yanks nothing");
        CHECK(vim->registerText('0') == std::string());

        make("ab\n\nc\n");
        editor->setCaret(ALTextPos(1, 0));
        ex("normal dliQ");
        UNSCOPED_INFO("dl on an empty line takes nothing and does not fail: the keys after it run");
        CHECK(flat(editor->text()) == std::string("ab|Q|c|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: an inclusive motion that ends at a line's end takes no line break", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\n\nb\n");
        keys("yl");
        e.setCaret(ALTextPos(1, 0));
        keys("dg_");
        UNSCOPED_INFO("dg_ on an empty line: nothing taken");
        CHECK(flat(e.text()) == std::string("a||b|"));
        UNSCOPED_INFO("and nothing kept");
        CHECK(vim->registerText('"') == std::string("a"));
        keys("cg_X<Esc>");
        UNSCOPED_INFO("cg_ inserts there, the next line left where it was");
        CHECK(flat(e.text()) == std::string("a|X|b|"));
        UNSCOPED_INFO("keeping the empty end");
        CHECK(vim->registerText('"') == std::string());

        make("a\n\nb\n");
        keys("yl");
        editor->setCaret(ALTextPos(1, 0));
        keys("yg_");
        UNSCOPED_INFO("yg_ yanks nothing");
        CHECK(vim->registerText('0') == std::string());

        make("ab\n\ncd\n");
        editor->setCaret(ALTextPos(1, 0));
        keys("dge");
        UNSCOPED_INFO("dge from an empty line: back to the b, the line after it kept");
        CHECK(flat(editor->text()) == std::string("a|cd|"));
        UNSCOPED_INFO("what it took");
        CHECK(vim->registerText('"') == std::string("b\n"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a counted doubled operator fails on the last line; else takes the rest", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc");
        keys("ylG");
        for (const char* typed : { "2dd", "2yy", "2>>", "2gUU", "2Y", "2ccX<Esc>", "2SX<Esc>" })
        {
            const std::string what(typed);
            keys(typed);
            UNSCOPED_INFO(what << " on the last line: nothing changed");
            REQUIRE(flat(e.text()) == std::string("a|b|c"));
            UNSCOPED_INFO(what << ": nothing kept");
            REQUIRE(vim->registerText('"') == std::string("a"));
            {
                INFO(what << ": normal mode, nothing pending");
                REQUIRE(vim->mode() == ALVimKeymap::Mode::Normal);
                REQUIRE(vim->status().empty());
            }
        }
        ex("normal 2ddiQ");
        UNSCOPED_INFO("failed, so the keys after it are not run");
        CHECK(flat(e.text()) == std::string("a|b|c"));
        keys("1dd");
        UNSCOPED_INFO("a count of one takes the last line");
        CHECK(flat(e.text()) == std::string("a|b"));

        make("a\nb\nc\nd");
        editor->setCaret(ALTextPos(1, 0));
        keys("5dd");
        UNSCOPED_INFO("5dd with three lines left takes the three");
        CHECK(flat(editor->text()) == std::string("a"));
        UNSCOPED_INFO("and keeps them");
        CHECK(vim->registerText('"') == std::string("b\nc\nd"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: p of an empty register puts nothing silently; :put puts an empty line", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("\nabc\n");
        keys("\"xyiwjl\"xp");
        UNSCOPED_INFO("nothing said: " << vim->message());
        CHECK(vim->message().empty());
        UNSCOPED_INFO("nothing put");
        CHECK(flat(e.text()) == std::string("|abc|"));
        UNSCOPED_INFO("the caret where it was");
        CHECK(caretText() == std::string("1:1"));
        keys("\"x3P");
        {
            INFO("3P neither: " << vim->message());
            CHECK(vim->message().empty());
            CHECK(flat(e.text()) == "|abc|");
        }
        ex("put x");
        UNSCOPED_INFO("nor :put");
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO(":put x: an empty line under the caret's");
        CHECK(flat(e.text()) == std::string("|abc||"));
        keys("\"zp");
        {
            INFO("a register never set: E353");
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("E353") != std::string::npos);
        }

        make("\nabc\n");
        keys("ciw<Esc>jp");
        UNSCOPED_INFO("ciw on an empty line, then p: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        UNSCOPED_INFO("and nothing put");
        CHECK(flat(editor->text()) == std::string("|abc|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: line counts of changes yanks and shifts are said past two as vim does", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd\ne\nf\n");
        keys("2yy");
        UNSCOPED_INFO("2yy: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("3yy");
        CHECK(vim->message() == std::string("3 lines yanked"));
        keys("2dd");
        UNSCOPED_INFO("2dd: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        UNSCOPED_INFO("the two gone");
        CHECK(flat(e.text()) == std::string("c|d|e|f|"));
        keys("3dd");
        CHECK(vim->message() == std::string("3 fewer lines"));

        make("a\nb\nc\n");
        keys("2>>");
        UNSCOPED_INFO("2>>: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("3>>");
        CHECK(vim->message() == std::string("3 lines >ed 1 time"));

        make("x\nx\ny\n");
        keys(":g/x/d<CR>");
        UNSCOPED_INFO("two lines taken by :g: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        UNSCOPED_INFO("both gone");
        CHECK(flat(editor->text()) == std::string("y|"));

        make("a a\nb\n");
        keys(":s/a/b/g<CR>");
        UNSCOPED_INFO("two substitutions: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        UNSCOPED_INFO("both made");
        CHECK(flat(editor->text()) == std::string("b b|b|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: visual p and P put the count's copies and leave the caret as vim does", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two three four\n");
        keys("$yiwbviw2p");
        CHECK(flat(e.text()) == std::string("one two fourfour four|"));
        UNSCOPED_INFO("the caret on the last character put");
        CHECK(caretText() == std::string("0:15"));
        UNSCOPED_INFO("what it replaced in the unnamed register");
        CHECK(vim->registerText('"') == std::string("three"));

        make("one two three four\n");
        keys("$yiwbviw2P");
        UNSCOPED_INFO("viw2P the same");
        CHECK(flat(editor->text()) == std::string("one two fourfour four|"));
        UNSCOPED_INFO("the registers as they were");
        CHECK(vim->registerText('"') == std::string("four"));

        make("a\nb\nc\n");
        keys("yyjV2p");
        UNSCOPED_INFO("yyjV2p: two lines for the one");
        CHECK(flat(editor->text()) == std::string("a|a|a|c|"));
        UNSCOPED_INFO("the caret on the first put");
        CHECK(caretText() == std::string("1:0"));

        make("abcd\nefgh\nX\n");
        keys("2jylggl<C-v>j2p");
        UNSCOPED_INFO("a block's columns each replaced by two copies");
        CHECK(flat(editor->text()) == std::string("aXXcd|eXXgh|X|"));
        UNSCOPED_INFO("the caret on the last put on its first line");
        CHECK(caretText() == std::string("0:2"));

        make("ab\ncd\nxyz\n");
        keys("vjy2jlv3p");
        UNSCOPED_INFO("characters over two lines put three times straight on");
        CHECK(flat(editor->text()) == std::string("ab|cd|xab|cab|cab|cz|"));
        UNSCOPED_INFO("the caret where they begin");
        CHECK(caretText() == std::string("2:1"));

        make("ab\n cd\nef\n");
        keys("yiwjV2p");
        UNSCOPED_INFO("characters into lines: a line each");
        CHECK(flat(editor->text()) == std::string("ab|ab|ab|ef|"));
        UNSCOPED_INFO("the caret on the first line put");
        CHECK(caretText() == std::string("1:0"));

        make("a\nbcd\nefg\n");
        keys("yyjl<C-v>j2P");
        UNSCOPED_INFO("lines into a block: the block taken out, the lines put over it twice");
        CHECK(flat(editor->text()) == std::string("a|a|a|bd|eg|"));
        UNSCOPED_INFO("the caret on the first of them");
        CHECK(caretText() == std::string("1:0"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a count before . replaces the operator's counts on both sides", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a b c d e f g h i j\n");
        keys("d3w2.");
        UNSCOPED_INFO("d3w then 2.: five words in all");
        CHECK(flat(e.text()) == std::string("f g h i j|"));

        make("a b c d e f g h i j k l m n\n");
        keys("2d3w1.");
        UNSCOPED_INFO("2d3w then 1.: one more");
        CHECK(flat(editor->text()) == std::string("h i j k l m n|"));

        make("a b c d e f g h i j\n");
        keys("c2wX<Esc>w3.");
        UNSCOPED_INFO("c2w then 3.: three words changed");
        CHECK(flat(editor->text()) == std::string("X X f g h i j|"));

        make("a b c d e f g h i j\n");
        keys("g~3ww2.");
        UNSCOPED_INFO("g~3w then 2.: two words");
        CHECK(flat(editor->text()) == std::string("A b c d e f g h i j|"));

        make("abcdef\n");
        keys("3ld0$2.");
        UNSCOPED_INFO("d0 then 2.: its 0 the motion, no count");
        CHECK(flat(editor->text()) == std::string("f|"));

        make("a b c d e f g h i j k l m n\n");
        keys("\"a2d3w1.");
        UNSCOPED_INFO("\"a2d3w then 1.: one more");
        CHECK(flat(editor->text()) == std::string("h i j k l m n|"));
        UNSCOPED_INFO("into a still");
        CHECK(vim->registerText('a') == std::string("g "));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :m and :t read a line before the first as :0put does", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc");
        ex("2t .-1");
        UNSCOPED_INFO(":2t .-1 from the first line: copied to the top");
        CHECK(flat(e.text()) == std::string("b|a|b|c"));
        UNSCOPED_INFO("the caret on the copy");
        CHECK(caretText() == std::string("0:0"));

        make("a\nb\nc");
        editor->setCaret(ALTextPos(1, 0));
        ex("3m .-2");
        UNSCOPED_INFO(":3m .-2 from the second line: moved to the top");
        CHECK(flat(editor->text()) == std::string("c|a|b"));

        make("a\nb\nc");
        ex("3m 0");
        UNSCOPED_INFO(":3m 0: to the top");
        CHECK(flat(editor->text()) == std::string("c|a|b"));

        ALCodeEditor& zero = make("a\nb\nc");
        zero.setLineNumberBase(-1);
        ex("2m 0");
        UNSCOPED_INFO("lines counted from 0: :2m 0 under the line shown as 0");
        CHECK(flat(zero.text()) == std::string("a|c|b"));
        ex("1t 0");
        UNSCOPED_INFO("and :1t 0 copies under it");
        CHECK(flat(zero.text()) == std::string("a|c|c|b"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a block yank pads a short line with blanks as wide as the block", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abcd\na\nabcd\n");
        e.setCaret(ALTextPos(0, 2));
        keys("<C-v>jjd");
        UNSCOPED_INFO("the block taken out, the short line as it was");
        CHECK(flat(e.text()) == std::string("abd|a|abd|"));
        UNSCOPED_INFO("a blank for the short line");
        CHECK(vim->registerText('"') == std::string("c\n \nc"));
        UNSCOPED_INFO("and in 1");
        CHECK(vim->registerText('1') == std::string("c\n \nc"));

        make("abcd\n\nabcd\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("<C-v>jjly");
        UNSCOPED_INFO("two blanks for an empty line under a block two wide");
        CHECK(vim->registerText('"') == std::string("bc\n  \nbc"));

        make("abcd\nab\nabcd\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("<C-v>jjy");
        UNSCOPED_INFO("a line that ends at the block's first column holds nothing of it");
        CHECK(vim->registerText('"') == std::string("c\n\nc"));

        make("abcdef\na\nabc\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("<C-v>jj$y");
        UNSCOPED_INFO("taken with $: blanks to the widest line's end");
        CHECK(vim->registerText('"') == std::string("cdef\n     \nc"));

        make("abcdefghijkl\n\t\nabcdefghijkl\n");
        editor->setCaret(ALTextPos(0, 9));
        keys("<C-v>jjy");
        UNSCOPED_INFO("a tab's line short of the block: a blank as the reader counts the block");
        CHECK(vim->registerText('"') == std::string("j\n \nj"));

        make("abcd\na\nabcd\nXY\n");
        keys("3j\"xy$gg2l<C-v>jj\"xp");
        UNSCOPED_INFO("visual p over it: the short line as it was");
        CHECK(flat(editor->text()) == std::string("abXYd|a|abXYd|XY|"));
        UNSCOPED_INFO("what it replaced kept as a delete keeps it");
        CHECK(vim->registerText('"') == std::string("c\n \nc"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a register's name before j keeps the column j wants", "[alvimkeymap]")
    {
        make("abcdef\nabcdefghij\n");
        keys("$\"aj");
        UNSCOPED_INFO("$ then \"aj: the line's end");
        CHECK(caretText() == std::string("1:9"));

        make("abcdef\nx\nabcdefghij\n");
        keys("$2\"aj");
        CHECK(caretText() == std::string("2:9"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: gj gk and g$ out of rows go as far as there are and fail", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\ndef\nghi");
        ex("normal 9gjx");
        UNSCOPED_INFO("9gj fails: the x after it not run");
        CHECK(flat(e.text()) == std::string("abc|def|ghi"));
        UNSCOPED_INFO("the caret on the last line all the same");
        CHECK(caretText() == std::string("2:0"));
        ex("normal 9gkx");
        UNSCOPED_INFO("9gk fails likewise");
        CHECK(flat(e.text()) == std::string("abc|def|ghi"));
        UNSCOPED_INFO("the caret on the first line");
        CHECK(caretText() == std::string("0:0"));

        e.setCaret(ALTextPos(0, 1));
        keys("d9gj");
        UNSCOPED_INFO("d9gj: nothing taken");
        CHECK(flat(e.text()) == std::string("abc|def|ghi"));
        UNSCOPED_INFO("the caret where gj got to");
        CHECK(caretText() == std::string("2:1"));
        {
            INFO("normal mode, nothing pending");
            CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
            CHECK(vim->status().empty());
        }
        keys("d9gk");
        UNSCOPED_INFO("d9gk: nothing taken");
        CHECK(flat(e.text()) == std::string("abc|def|ghi"));
        UNSCOPED_INFO("the caret where gk got to");
        CHECK(caretText() == std::string("0:1"));
        keys("d2gj");
        UNSCOPED_INFO("d2gj within the rows there are: from the caret to there");
        CHECK(flat(e.text()) == std::string("ahi"));

        const std::string line = "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll";
        ALCodeEditor&     w    = make(line.c_str());
        w.setWordWrap(true);
        w.reshape(120, 200);
        const std::vector<ALTextLayout::Row> rows = w.layout().line(0).rows;
        UNSCOPED_INFO("the line wraps to three rows or more");
        REQUIRE(rows.size() >= 3);
        w.setCaret(ALTextPos(0, rows[1].begin + 2));
        keys("d99g$");
        UNSCOPED_INFO("d99g$ runs out of rows and fails, nothing taken");
        CHECK(w.document().line(0) == line);
        UNSCOPED_INFO("the caret at the end of the last row all the same");
        CHECK(caretText() == "0:" + std::to_string(line.size() - 1));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a counted g$ over wrapped rows wants line ends as $ does; g$ alone not", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("abc\ndefgh\nghijklmn");
        e.setWordWrap(true);
        keys("2g$");
        UNSCOPED_INFO("2g$: the next row's last character");
        CHECK(caretText() == std::string("1:4"));
        keys("j");
        UNSCOPED_INFO("then j: the next line's end");
        CHECK(caretText() == std::string("2:7"));

        e.setCaret(ALTextPos(0, 0));
        keys("g$j");
        UNSCOPED_INFO("g$ then j: the column g$ went to");
        CHECK(caretText() == std::string("1:2"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: go reaches any byte of a long text by its count read as typed", "[alvimkeymap]")
    {
        std::string text;
        for (int i = 0; i < 1100; ++i)
        {
            text += std::string(99, 'x') + "\n";
        }
        text.pop_back();
        make(text.c_str());
        keys("105000go");
        UNSCOPED_INFO("byte 105000, the break that ends line 1050: the line's last character");
        CHECK(caretText() == std::string("1049:98"));
        keys("105001go");
        UNSCOPED_INFO("byte 105001: the first of line 1051");
        CHECK(caretText() == std::string("1050:0"));
        keys("99999999999go");
        UNSCOPED_INFO("past the end, and past the most a count is: the last character");
        CHECK(caretText() == std::string("1099:98"));
        keys("gg2d52501go");
        UNSCOPED_INFO("2d52501go: from the top to byte 105002");
        CHECK(editor->document().line(0) == std::string(98, 'x'));
        UNSCOPED_INFO("the lines before it gone");
        CHECK(editor->document().lineCount() == 50);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: J joins as many lines as its count says up to all; visual J too", "[alvimkeymap]")
    {
        std::string text;
        for (int i = 0; i < 100002; ++i)
        {
            text += "a\n";
        }
        text += "a";
        make(text.c_str());
        keys("100002J");
        UNSCOPED_INFO("100002J: the last line left");
        CHECK(editor->document().lineCount() == 2);
        UNSCOPED_INFO("the rest one line");
        CHECK(editor->document().line(0).size() == size_t(200003));

        make(text.c_str());
        keys("VGJ");
        UNSCOPED_INFO("VGJ: every line one");
        CHECK(editor->document().lineCount() == 1);
        UNSCOPED_INFO("all of them");
        CHECK(editor->document().line(0).size() == size_t(200005));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: * keeps its word whole for :s// :g// and n; * and g* go by ignorecase", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("foo food\n");
        keys("*");
        ex("%s//bar/g");
        UNSCOPED_INFO(":s// after * the word alone");
        CHECK(flat(e.text()) == std::string("bar food|"));

        make("foo food\nfood\n");
        keys("*");
        ex("g//d");
        UNSCOPED_INFO(":g// after * the lines with the word whole");
        CHECK(flat(editor->text()) == std::string("food|"));

        make("foo\nfood foo\n");
        keys("*");
        ex("s//X/");
        UNSCOPED_INFO(":s// on the line * went to");
        CHECK(flat(editor->text()) == std::string("foo|food X|"));

        make("ab foo food\nfoo\n");
        editor->setCaret(ALTextPos(0, 4));
        keys("*");
        ex("s//X/");
        keys("nn");
        UNSCOPED_INFO("n after the :s// the word whole still");
        CHECK(caretText() == std::string("0:3"));

        make("Foo foo Foo foo\n");
        ex("set ic scs");
        keys("g*nn");
        UNSCOPED_INFO("g* and its n by ignorecase alone, the capital no matter");
        CHECK(caretText() == std::string("0:12"));
        keys("0*nn");
        UNSCOPED_INFO("and *");
        CHECK(caretText() == std::string("0:12"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: t T and ; step by whole characters however many bytes each takes", "[alvimkeymap]")
    {
        make("a" "\xC3\xA9" "xbx\n");
        keys("tx");
        UNSCOPED_INFO("t onto the character of two bytes before the x");
        CHECK(caretText() == std::string("0:1"));
        keys(";");
        UNSCOPED_INFO("; past the x beside it, to before the next");
        CHECK(caretText() == std::string("0:4"));

        make("\xC3\xA9" "xax\n");
        keys("tx;");
        UNSCOPED_INFO("from a character of two bytes under the caret");
        CHECK(caretText() == std::string("0:3"));

        make("\xC3\xA9" "b" "\xC3\xA9" "a\n");
        editor->setCaret(ALTextPos(0, 5));
        keys("T");
        editor->handleUnicodeCharHere(static_cast<llwchar>(0xE9));
        UNSCOPED_INFO("T to the character of two bytes beside the caret: where it is");
        CHECK(caretText() == std::string("0:5"));
        keys(";");
        UNSCOPED_INFO("; past it, to after the one before");
        CHECK(caretText() == std::string("0:2"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a macro of over ten thousand keys plays; only mapped keys count to E223", "[alvimkeymap]")
    {
        ALCodeEditor&     e = make("\n");
        const std::string typed(10001, 'x');
        keys(("qai" + typed + "<Esc>q").c_str());
        UNSCOPED_INFO("recorded, typed once");
        CHECK(e.document().line(0).size() == static_cast<size_t>(10001));
        ex("nmap Q x");
        keys("@a");
        UNSCOPED_INFO("no recursive mapping said: " << vim->message());
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO("played whole");
        CHECK(e.document().line(0).size() == static_cast<size_t>(20002));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a visual count> too large says so and shifts nothing; else the levels", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\n");
        e.setSoftTabs(true);
        e.setTabWidth(4);
        keys("VG99999>");
        {
            INFO("said: " << vim->message());
            CHECK(vim->messageIsError());
            CHECK(vim->message().find("Too large a count") != std::string::npos);
        }
        UNSCOPED_INFO("nothing shifted");
        CHECK(flat(e.text()) == std::string("a|b|c|d|e|f|g|h|i|j|k|"));
        keys("ggVj2>");
        UNSCOPED_INFO("two levels on each line");
        CHECK(flat(e.text()) == std::string("        a|        b|c|d|e|f|g|h|i|j|k|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: Tab on the : line completes a name at the cursor and keeps the rest", "[alvimkeymap]")
    {
        make("x\n");
        keys(":10,20del<Home><Right><Tab>");
        UNSCOPED_INFO("a name put in at the cursor, nothing doubled");
        CHECK(vim->commandLine() == std::string("1center0,20del"));
        keys("<Esc>");
        UNSCOPED_INFO("Escape puts the line back as it was");
        CHECK(vim->commandLine() == std::string("10,20del"));
        keys("<Esc>:10,20<Left><Tab>");
        UNSCOPED_INFO("in a range with no name after it");
        CHECK(vim->commandLine() == std::string("10,2center0"));
        keys("<Esc><Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: the : commands are taken by any abbreviation vim takes", "[alvimkeymap]")
    {
        // Each run, and none of them unknown.
        const auto run = [this](const char* line) {
            ex(line);
            INFO(":" << line << " said: " << vim->message());
            CHECK_FALSE(vim->messageIsError());
        };
        ALCodeEditor& e = make("a b\nc\n");
        run("joi");
        UNSCOPED_INFO(":joi joins");
        CHECK(flat(e.text()) == std::string("a b c|"));
        make("a\nb\nc\n");
        run("dele");
        UNSCOPED_INFO(":dele deletes");
        CHECK(flat(editor->text()) == std::string("b|c|"));
        make("abc\n");
        run("norma x");
        UNSCOPED_INFO(":norma plays the keys");
        CHECK(flat(editor->text()) == std::string("bc|"));
        make("  a\n");
        run("lef");
        UNSCOPED_INFO(":lef to the left");
        CHECK(flat(editor->text()) == std::string("a|"));
        make("a\n");
        run("rig 5");
        UNSCOPED_INFO(":rig to the right");
        CHECK(flat(editor->text()) == std::string("    a|"));
        make("a\nb\nc\n");
        run("3mo 0");
        UNSCOPED_INFO(":mo moves");
        CHECK(flat(editor->text()) == std::string("c|a|b|"));
        make("a\nb\nc\n");
        run("3cop 0");
        UNSCOPED_INFO(":cop copies");
        CHECK(flat(editor->text()) == std::string("c|a|b|c|"));
        make("a\nxb\nc\n");
        run("gl/x/d");
        CHECK(flat(editor->text()) == std::string("a|c|"));
        make("a\nxb\nc\n");
        run("vg/x/d");
        CHECK(flat(editor->text()) == std::string("xb|"));
        make("aa\n");
        run("su/a/b/");
        CHECK(flat(editor->text()) == std::string("ba|"));
        make("a\nb\n");
        run("ya");
        run("pu");
        UNSCOPED_INFO(":ya then :pu");
        CHECK(flat(editor->text()) == std::string("a|a|b|"));
        make("a\n");
        keys("x");
        run("und");
        UNSCOPED_INFO(":und undoes");
        CHECK(flat(editor->text()) == std::string("a|"));
        make("a\nb\n");
        keys("gg");
        run("2mar a");
        keys("'a");
        UNSCOPED_INFO(":mar sets a mark");
        CHECK(caretText() == std::string("1:0"));
        run("setlo ic");
        UNSCOPED_INFO(":setlo sets");
        CHECK(vim->shared().ignoreCase);
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: insert mode's Ctrl-D takes the step < takes with spaces before a tab", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("  \tfoo\n");
        e.setSoftTabs(false);
        e.setTabWidth(4);
        keys("A<C-d><Esc>");
        UNSCOPED_INFO("two spaces and a tab, a step wide, gone whole, as vim's");
        CHECK(flat(e.text()) == std::string("foo|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: quote objects start at the last quote before the caret; counts; escapes", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("x \"ab\" y \"ef\"\n");
        e.setCaret(ALTextPos(0, 7));
        keys("di\"");
        UNSCOPED_INFO("di\" between two quoted texts takes what lies between");
        CHECK(flat(e.text()) == std::string("x \"ab\"\"ef\"|"));
        UNSCOPED_INFO("what it took");
        CHECK(vim->registerText('"') == std::string(" y "));

        make("x \"ab\" y\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("d2i\"");
        UNSCOPED_INFO("d2i\" takes the quotes, not the blanks");
        CHECK(flat(editor->text()) == std::string("x  y|"));

        make("x \"ab\",\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("da\"");
        UNSCOPED_INFO("da\" with no blank after takes the blanks before");
        CHECK(flat(editor->text()) == std::string("x,|"));

        make("x \"a\\\"b\" y\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("di\"");
        UNSCOPED_INFO("di\" passes over an escaped quote");
        CHECK(flat(editor->text()) == std::string("x \"\" y|"));
        UNSCOPED_INFO("and takes it with the rest");
        CHECK(vim->registerText('"') == std::string("a\\\"b"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a bracket object with no block around the caret takes the next one", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("x (a) y\n");
        keys("di(");
        UNSCOPED_INFO("di( before a block takes what the next one holds");
        CHECK(flat(e.text()) == std::string("x () y|"));

        make("x\n(a) y\n");
        keys("di(");
        UNSCOPED_INFO("the next block on a line after");
        CHECK(flat(editor->text()) == std::string("x|() y|"));

        make("x ) (a) y\n");
        keys("di(");
        UNSCOPED_INFO("a closer before it leaves none");
        CHECK(flat(editor->text()) == std::string("x ) (a) y|"));

        make("x (a) y\n");
        keys("vli(y");
        UNSCOPED_INFO("vi( over a selection before a block");
        CHECK(vim->registerText('"') == std::string("a"));

        make("((a))\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("d3i(");
        UNSCOPED_INFO("3i( with two blocks fails");
        CHECK(flat(editor->text()) == std::string("((a))|"));
        keys("d2i(");
        UNSCOPED_INFO("2i( takes the outer");
        CHECK(flat(editor->text()) == std::string("()|"));

        make("{\n  x\n}\n");
        editor->setCaret(ALTextPos(1, 2));
        keys("di{");
        UNSCOPED_INFO("di{ with the closer alone on its line takes the line between");
        CHECK(flat(editor->text()) == std::string("{|}|"));

        make("(\nfoo\n)\n");
        editor->setCaret(ALTextPos(1, 1));
        keys("di(");
        UNSCOPED_INFO("and with the brackets on lines of their own");
        CHECK(flat(editor->text()) == std::string("(|)|"));

        make("{x\n}\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("di{");
        UNSCOPED_INFO("from the opener's line, up to its end");
        CHECK(flat(editor->text()) == std::string("{|}|"));

        make("{\n  x\n}\n");
        editor->setCaret(ALTextPos(1, 2));
        keys("vi{y");
        UNSCOPED_INFO("vi{ takes the line's break as well");
        CHECK(vim->registerText('"') == std::string("  x\n"));

        make("x () y\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("vi(y");
        UNSCOPED_INFO("vi( on () fails, the selection as it was");
        CHECK(vim->registerText('"') == std::string(")"));

        make("x () y\n");
        keys("vi(y");
        UNSCOPED_INFO("and before one, the next block holding nothing");
        CHECK(vim->registerText('"') == std::string("x"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: word and quote objects keep a one-character block; bracket ones do not", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("one two\nthree four\n");
        e.setCaret(ALTextPos(0, 4));
        keys("<C-v>iw");
        UNSCOPED_INFO("C-v iw keeps the block");
        CHECK(vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("y");
        UNSCOPED_INFO("over the word");
        CHECK(vim->registerText('"') == std::string("two"));
        e.setCaret(ALTextPos(0, 4));
        keys("<C-v>aw");
        UNSCOPED_INFO("C-v aw too");
        CHECK(vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("<Esc>Viw");
        UNSCOPED_INFO("V iw makes it characters");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        keys("<Esc>");

        make("x \"ab\" (cd) y\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("<C-v>i\"");
        UNSCOPED_INFO("C-v i\" keeps the block");
        CHECK(vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("y");
        UNSCOPED_INFO("over what the quotes hold");
        CHECK(vim->registerText('"') == std::string("ab"));
        editor->setCaret(ALTextPos(0, 8));
        keys("<C-v>i(");
        UNSCOPED_INFO("C-v i( makes it characters");
        CHECK(vim->mode() == ALVimKeymap::Mode::Visual);
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: ap takes blank lines before when none follow or the paragraph after", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\n\nb\nc");
        e.setCaret(ALTextPos(2, 0));
        keys("dap");
        UNSCOPED_INFO("dap on the last paragraph takes the blank line before");
        CHECK(flat(e.text()) == std::string("a"));

        make("a\n\nb\nc");
        editor->setCaret(ALTextPos(2, 0));
        keys("Vapy");
        UNSCOPED_INFO("and V ap selects it so");
        CHECK(vim->registerText('"') == std::string("\nb\nc"));

        make("a\n\n\nb\nc\n\nd");
        editor->setCaret(ALTextPos(1, 0));
        keys("dap");
        UNSCOPED_INFO("dap on blank lines takes the paragraph after them");
        CHECK(flat(editor->text()) == std::string("a||d"));

        make("a\nb\n\n");
        editor->setCaret(ALTextPos(2, 0));
        keys("dap");
        UNSCOPED_INFO("and with no paragraph after them fails");
        CHECK(flat(editor->text()) == std::string("a|b||"));

        make("abc\ndef\n\nx");
        editor->setCaret(ALTextPos(0, 2));
        keys("vip<Esc>");
        UNSCOPED_INFO("vip: the caret at the start of the last line");
        CHECK(caretText() == std::string("1:0"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a tag ends at the > that is no part of a quoted attribute value", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("<a title=\"x>y\">text</a>\n");
        e.setCaret(ALTextPos(0, 17));
        keys("dit");
        UNSCOPED_INFO("dit takes what the tags hold");
        CHECK(flat(e.text()) == std::string("<a title=\"x>y\"></a>|"));

        make("<a title='x>y'>text</a>\n");
        editor->setCaret(ALTextPos(0, 5));
        keys("dit");
        UNSCOPED_INFO("from inside the attribute, its quotes single");
        CHECK(flat(editor->text()) == std::string("<a title='x>y'></a>|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a paragraph object ends at a formfeed or a paragraphs or sections macro", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("a\nb\n.PP\nc\nd");
        keys("dip");
        UNSCOPED_INFO("ip stops before .PP");
        CHECK(flat(e.text()) == std::string(".PP|c|d"));

        make("a\n.SH\nc\nd");
        editor->setCaret(ALTextPos(3, 0));
        keys("dip");
        UNSCOPED_INFO("and goes back no further than .SH");
        CHECK(flat(editor->text()) == std::string("a"));

        make("a\n\fb\nc\nd");
        editor->setCaret(ALTextPos(3, 0));
        keys("dip");
        UNSCOPED_INFO("or than a formfeed");
        CHECK(flat(editor->text()) == std::string("a"));

        make("a\n.XX\nc\nd");
        editor->setCaret(ALTextPos(3, 0));
        keys("dip");
        UNSCOPED_INFO("a macro of neither is no boundary");
        CHECK(flat(editor->text()) == std::string(""));

        make("a\nb\n.PP\nc\n\nd");
        editor->setCaret(ALTextPos(2, 0));
        keys("dap");
        UNSCOPED_INFO("ap from the macro's line, with the blank line after");
        CHECK(flat(editor->text()) == std::string("a|b|d"));

        make("a\nb\nc\n.PP\nd");
        keys("Vjipy");
        UNSCOPED_INFO("V ip taken on stops before it too");
        CHECK(vim->registerText('"') == std::string("a\nb\nc"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: gv over characters taken with $ keeps to line ends for . and j", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("ab\ncdefgh\nq\n");
        keys("v$<Esc>gvd.");
        UNSCOPED_INFO("gv d, then . to the next line's end");
        CHECK(flat(e.text()) == std::string("q|"));

        make("ab\ncdefgh\nq\n");
        keys("v$<Esc>dgv.");
        UNSCOPED_INFO("d gv, then . to the next line's end");
        CHECK(flat(editor->text()) == std::string("q|"));

        make("ab\ncdefgh\nq\n");
        keys("v$<Esc>gvjd");
        UNSCOPED_INFO("gv, then j to the next line's end");
        CHECK(flat(editor->text()) == std::string("q|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :s// and :g// after * # or g* match without smartcase as the search did", "[alvimkeymap]")
    {
        make("Foo foo foo Foo\n");
        ex("set ic scs");
        keys("*");
        ex("s//X/g");
        UNSCOPED_INFO(":s// after * takes the word in every case");
        CHECK(flat(editor->text()) == std::string("X X X X|"));

        make("Foo foo foo Foo\n");
        ex("set ic scs");
        editor->setCaret(ALTextPos(0, 12));
        keys("#");
        ex("s//X/g");
        UNSCOPED_INFO("and after #");
        CHECK(flat(editor->text()) == std::string("X X X X|"));

        make("Foo foox bar\n");
        ex("set ic scs");
        keys("g*");
        ex("s//X/g");
        UNSCOPED_INFO("and after g*");
        CHECK(flat(editor->text()) == std::string("X Xx bar|"));

        make("Foo\nfoo\nbar\nFOO\n");
        ex("set ic scs");
        keys("*");
        ex("g//d");
        UNSCOPED_INFO(":g// after * takes the lines with the word in any case");
        CHECK(flat(editor->text()) == std::string("bar|"));

        make("Foo x\nfoo foo\n");
        ex("set ic scs");
        keys("*");
        ex("s//X/");
        keys("n");
        UNSCOPED_INFO("n after that :s// still without smartcase");
        CHECK(caretText() == std::string("1:2"));

        make("Foo foo\nFoo foo\n");
        ex("set ic scs");
        keys("*");
        ex("s/Foo/X/");
        keys("j");
        ex("s//Y/g");
        UNSCOPED_INFO("a pattern typed for :s, and :s// after it, by smartcase");
        CHECK(flat(editor->text()) == std::string("X foo|Y foo|"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: after ? an escaped ? is itself and # puts escaped ones in the history", "[alvimkeymap]")
    {
        typedef std::vector<std::string> history_t;
        make("x ?? ??\n");
        editor->setCaret(ALTextPos(0, 5));
        keys("#");
        UNSCOPED_INFO("# on ?? back to the one before");
        CHECK(caretText() == std::string("0:2"));
        UNSCOPED_INFO("the history has each ? escaped");
        CHECK(vim->shared().search == history_t{ "\\?\\?" });

        make("a?b x a?b\n");
        editor->setCaret(ALTextPos(0, 8));
        keys("?a\\?b<CR>");
        UNSCOPED_INFO("?a\\?b finds a?b itself");
        CHECK(caretText() == std::string("0:6"));
        keys("n");
        UNSCOPED_INFO("and n goes on with it");
        CHECK(caretText() == std::string("0:0"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a search line matches one in the history only if both ended alike", "[alvimkeymap]")
    {
        typedef std::vector<std::string> history_t;
        make("foo foo\n");
        keys("/\\<foo\\><CR>*");
        UNSCOPED_INFO("a typed \\<foo\\> and *'s are two");
        CHECK(vim->shared().search == history_t{ "\\<foo\\>", "\\<foo\\>" });

        make("foo foo\n");
        keys("/foo<CR>?foo<CR>");
        UNSCOPED_INFO("and so are /foo and ?foo");
        CHECK(vim->shared().search == history_t{ "foo", "foo" });

        make("foo foo\n");
        keys("/foo<CR>/foo<CR>");
        UNSCOPED_INFO("where /foo twice is one");
        CHECK(vim->shared().search == history_t{ "foo" });
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: past Latin-1 a character's class is vim's for * w e and iw", "[alvimkeymap]")
    {
        typedef std::vector<std::string> history_t;
        // Three ideographs, an em dash and an emoji, as their UTF-8 bytes.
        const std::string cjk   = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
        const std::string dash  = "\xe2\x80\x94";
        const std::string emoji = "\xf0\x9f\x98\x80";
        make((cjk + "abc def\n").c_str());
        keys("w");
        UNSCOPED_INFO("w from the ideographs to the letters after them");
        CHECK(caretText() == std::string("0:9"));
        keys("0e");
        UNSCOPED_INFO("e to the last ideograph");
        CHECK(caretText() == std::string("0:6"));
        editor->setCaret(ALTextPos(0, 3));
        keys("yiw");
        UNSCOPED_INFO("iw the ideographs alone");
        CHECK(vim->registerText('"') == cjk);

        make(("a" + emoji + "b c\n").c_str());
        keys("w");
        UNSCOPED_INFO("w onto an emoji");
        CHECK(caretText() == std::string("0:1"));
        keys("w");
        UNSCOPED_INFO("and past it");
        CHECK(caretText() == std::string("0:5"));

        make(("foo" + dash + "bar x\n").c_str());
        keys("w");
        UNSCOPED_INFO("w onto a dash");
        CHECK(caretText() == std::string("0:3"));

        make((cjk + "abc " + cjk + " x\n").c_str());
        keys("*");
        UNSCOPED_INFO("* the ideographs as the word");
        CHECK(vim->shared().search == history_t{ "\\<" + cjk + "\\>" });
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: ( and ) go by sentences; is and as are the sentence objects", "[alvimkeymap]")
    {
        const char* text = "One two. Three four.  Five six.\nNext one.\n";
        make(text);
        keys(")");
        UNSCOPED_INFO(") to the next sentence");
        CHECK(caretText() == std::string("0:9"));
        keys(")");
        UNSCOPED_INFO("past the blanks after a sentence");
        CHECK(caretText() == std::string("0:22"));
        keys("(");
        UNSCOPED_INFO("( back to the one before");
        CHECK(caretText() == std::string("0:9"));
        keys("03)");
        UNSCOPED_INFO("3) on to the next line");
        CHECK(caretText() == std::string("1:0"));

        make(text);
        keys("d)");
        UNSCOPED_INFO("d) to the next sentence");
        CHECK(flat(editor->text()) == std::string("Three four.  Five six.|Next one.|"));
        UNSCOPED_INFO("in register 1, however little");
        CHECK(vim->registerText('1') == std::string("One two. "));

        make(text);
        editor->setCaret(ALTextPos(0, 12));
        keys("dis");
        UNSCOPED_INFO("dis the sentence alone");
        CHECK(flat(editor->text()) == std::string("One two.   Five six.|Next one.|"));
        make(text);
        editor->setCaret(ALTextPos(0, 12));
        keys("das");
        UNSCOPED_INFO("das with the blanks after it");
        CHECK(flat(editor->text()) == std::string("One two. Five six.|Next one.|"));
        make(text);
        editor->setCaret(ALTextPos(0, 24));
        keys("das");
        UNSCOPED_INFO("das where none come after, with those before");
        CHECK(flat(editor->text()) == std::string("One two. Three four.|Next one.|"));

        make(text);
        editor->setCaret(ALTextPos(0, 12));
        keys("visisy");
        UNSCOPED_INFO("vis is takes the blanks after on");
        CHECK(vim->registerText('"') == std::string("Three four.  "));
        editor->setCaret(ALTextPos(0, 12));
        keys("vasasy");
        UNSCOPED_INFO("vas as the next sentence too");
        CHECK(vim->registerText('"') == std::string("Three four.  Five six."));

        make("Hello world.\nnext\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("dis");
        UNSCOPED_INFO("dis on a line of one sentence takes the line");
        CHECK(flat(editor->text()) == std::string("next|"));

        make(".]");
        editor->setCaret(ALTextPos(0, 1));
        keys("das");
        UNSCOPED_INFO("das on the ] of a last line's .] takes the ] alone");
        CHECK(flat(editor->text()) == std::string("."));
        UNSCOPED_INFO("which it took");
        CHECK(vim->registerText('"') == std::string("]"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: editing keys on the : and / lines go by whole characters", "[alvimkeymap]")
    {
        make("x\n");
        keys(":e");
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x301));
        UNSCOPED_INFO("the mark typed onto the letter");
        CHECK(vim->commandLine() == std::string("e\xCC\x81"));
        keys("<BS>");
        UNSCOPED_INFO("Backspace takes the letter with its mark");
        CHECK(vim->commandLine() == std::string());

        keys("ae");
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x301));
        keys("b<Left><Left><Delete>");
        UNSCOPED_INFO("Left past the b and the marked letter, and Delete takes that whole");
        CHECK(vim->commandLine() == std::string("ab"));

        keys("<Esc>:e");
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x301));
        keys("b<Home><Right><BS>");
        UNSCOPED_INFO("Right past the letter and its mark, and Backspace there takes both");
        CHECK(vim->commandLine() == std::string("b"));

        // A man, a zero width joiner and a woman: one emoji.
        keys("<Esc>/");
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x1F468));
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x200D));
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x1F469));
        UNSCOPED_INFO("the three typed");
        CHECK(vim->commandLine() == std::string("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9"));
        keys("<BS>");
        UNSCOPED_INFO("Backspace takes the joined emoji whole");
        CHECK(vim->commandLine() == std::string());
        keys("<Esc>");
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: + Return and Ctrl-M on the last line and - on the first fail in place", "[alvimkeymap]")
    {
        make("  a f\n  bcd\n   e f");
        editor->setCaret(ALTextPos(2, 5));
        keys("+");
        UNSCOPED_INFO("+ on the last line stays");
        CHECK(caretText() == std::string("2:5"));
        keys("<CR>");
        UNSCOPED_INFO("and so does Return");
        CHECK(caretText() == std::string("2:5"));
        keys("<C-m>");
        UNSCOPED_INFO("and Ctrl-M");
        CHECK(caretText() == std::string("2:5"));
        keys("d+");
        UNSCOPED_INFO("d+ there takes nothing");
        CHECK(flat(editor->text()) == std::string("  a f|  bcd|   e f"));
        UNSCOPED_INFO("and leaves the caret");
        CHECK(caretText() == std::string("2:5"));
        editor->setCaret(ALTextPos(0, 4));
        keys("-");
        UNSCOPED_INFO("- on the first line stays");
        CHECK(caretText() == std::string("0:4"));
        keys("d-");
        UNSCOPED_INFO("d- there takes nothing");
        CHECK(flat(editor->text()) == std::string("  a f|  bcd|   e f"));
        UNSCOPED_INFO("and leaves the caret");
        CHECK(caretText() == std::string("0:4"));

        make("  a\n  bcd\n   e f\nx");
        editor->setCaret(ALTextPos(1, 3));
        keys("5+");
        UNSCOPED_INFO("5+ with two lines below to the last");
        CHECK(caretText() == std::string("3:0"));
        editor->setCaret(ALTextPos(2, 3));
        keys("5-");
        UNSCOPED_INFO("5- with two lines above to the first's first non-blank");
        CHECK(caretText() == std::string("0:2"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: j after an operator over $ keeps the column; after $ alone the ends", "[alvimkeymap]")
    {
        const char* text = "abcdef\nghijklmnop\nqrstuvwxyz";
        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("d$j");
        UNSCOPED_INFO("d$ then j");
        CHECK(caretText() == std::string("1:1"));

        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("y$j");
        UNSCOPED_INFO("y$ then j");
        CHECK(caretText() == std::string("1:2"));

        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("d2$j");
        UNSCOPED_INFO("d2$ takes the line after");
        CHECK(flat(editor->text()) == std::string("ab|qrstuvwxyz"));
        UNSCOPED_INFO("and j after it keeps the column");
        CHECK(caretText() == std::string("1:1"));

        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("c$<Esc>j");
        UNSCOPED_INFO("c$ then j");
        CHECK(caretText() == std::string("1:1"));

        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("Dj");
        UNSCOPED_INFO("D then j");
        CHECK(caretText() == std::string("1:1"));

        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("$j");
        UNSCOPED_INFO("$ then j to the next line's end");
        CHECK(caretText() == std::string("1:9"));
        keys("j");
        UNSCOPED_INFO("and on to the one after's");
        CHECK(caretText() == std::string("2:9"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a yank of more than two lines says so as vim does; blocks; characters", "[alvimkeymap]")
    {
        make("one\ntwo\nthree\nfour\nfive\nsix\n");
        keys("\"a3yy");
        CHECK(vim->message() == std::string("3 lines yanked into \"a"));
        keys("\"A3yy");
        CHECK(vim->message() == std::string("3 lines yanked into \"A"));
        keys("\"\"3yy");
        CHECK(vim->message() == std::string("3 lines yanked into \"\""));
        keys("\"a2yy");
        UNSCOPED_INFO("\"a2yy: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("\"_3yy");
        UNSCOPED_INFO("\"_3yy: nothing said: " << vim->message());
        CHECK(vim->message().empty());

        keys("<C-v>2jy");
        UNSCOPED_INFO("a block of three lines");
        CHECK(vim->message() == std::string("block of 3 lines yanked"));
        keys("<C-v>2j\"by");
        UNSCOPED_INFO("into a register");
        CHECK(vim->message() == std::string("block of 3 lines yanked into \"b"));
        keys("<C-v>jy");
        UNSCOPED_INFO("a block of two: nothing said: " << vim->message());
        CHECK(vim->message().empty());

        editor->setCaret(ALTextPos(0, 1));
        keys("v2jy");
        UNSCOPED_INFO("characters over three lines");
        CHECK(vim->message() == std::string("3 lines yanked"));
        keys("v2j\"ay");
        UNSCOPED_INFO("into a register");
        CHECK(vim->message() == std::string("3 lines yanked into \"a"));
        keys("vjy");
        UNSCOPED_INFO("over two: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("v$jjy");
        UNSCOPED_INFO("through the third line's break, the fourth counted");
        CHECK(vim->message() == std::string("4 lines yanked"));
        keys("y/four<CR>");
        UNSCOPED_INFO("y/four from the first line's second character: three lines");
        CHECK(vim->message() == std::string("3 lines yanked"));
        keys("y/three<CR>");
        UNSCOPED_INFO("y/three: two, nothing said: " << vim->message());
        CHECK(vim->message().empty());
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: puts and case = and :m over more than two lines report as vim does", "[alvimkeymap]")
    {
        const char* text = "one\ntwo\nthree\nfour\nfive\nsix\n";
        make(text);
        keys("gg3yyp");
        CHECK(vim->message() == std::string("3 more lines"));
        keys("uggyy3p");
        CHECK(vim->message() == std::string("3 more lines"));
        keys("ugg2yyp");
        UNSCOPED_INFO("2yyp: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("uggv$y3p");
        UNSCOPED_INFO("three of a line's characters and its break");
        CHECK(vim->message() == std::string("3 more lines"));
        keys("uggv$y2p");
        UNSCOPED_INFO("two of them: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("u:1,3y<CR>:put<CR>");
        UNSCOPED_INFO(":put of three lines");
        CHECK(vim->message() == std::string("3 more lines"));

        make("one\ntwo\nthree\nfour\nfive\nsix");
        keys("<C-v>3jyGp");
        UNSCOPED_INFO("a block's rows put past the last line");
        CHECK(vim->message() == std::string("3 more lines"));
        keys("uggp");
        UNSCOPED_INFO("a block put over lines there are: nothing said: " << vim->message());
        CHECK(vim->message().empty());

        make(text);
        keys("g~2j");
        CHECK(vim->message() == std::string("3 lines changed"));
        keys("ugggUj");
        UNSCOPED_INFO("gUj: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("ugg3gUU");
        CHECK(vim->message() == std::string("3 lines changed"));
        keys("uggV2jU");
        CHECK(vim->message() == std::string("3 lines changed"));
        keys("uggv2j~");
        CHECK(vim->message() == std::string("3 lines changed"));
        keys("u");

        vim->hooks().format = [](ALTextView&, S32, S32) {};
        keys("gg=2j");
        CHECK(vim->message() == std::string("3 lines indented"));
        keys("=j");
        UNSCOPED_INFO("=j: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("3==");
        CHECK(vim->message() == std::string("3 lines indented"));

        ex("1,3m$");
        CHECK(vim->message() == std::string("3 lines moved"));
        keys("u");
        ex("1,2m$");
        UNSCOPED_INFO(":1,2m$: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("u");
        ex("1,3t$");
        UNSCOPED_INFO(":1,3t$ copies three");
        CHECK(vim->message() == std::string("3 more lines"));
        keys("u");
        keys(":g/three/.,+2m0<CR>");
        UNSCOPED_INFO("moved by a :g");
        CHECK(flat(editor->text()) == std::string("three|four|five|one|two|six|"));
        UNSCOPED_INFO(":m under a :g: nothing said: " << vim->message());
        CHECK(vim->message().empty());
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: more than two substitutions on one line are said only when typed", "[alvimkeymap]")
    {
        make("a a a\nx\n");
        keys(":s/a/b/g<CR>");
        UNSCOPED_INFO("typed");
        CHECK(vim->message() == std::string("3 substitutions on 1 line"));
        keys("uqqgg:s/a/b/g<CR>q");
        keys("u@q");
        UNSCOPED_INFO("the macro made them");
        CHECK(flat(editor->text()) == std::string("b b b|x|"));
        UNSCOPED_INFO("played from a macro: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("ugg@:");
        UNSCOPED_INFO("@: made them");
        CHECK(flat(editor->text()) == std::string("b b b|x|"));
        UNSCOPED_INFO("run again by @:: nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("ug&");
        UNSCOPED_INFO("g& typed");
        CHECK(vim->message() == std::string("3 substitutions on 1 line"));
        keys("u:g/a/s/a/b/g<CR>");
        UNSCOPED_INFO("a :g made them");
        CHECK(flat(editor->text()) == std::string("b b b|x|"));
        UNSCOPED_INFO("under a :g over one line: nothing said: " << vim->message());
        CHECK(vim->message().empty());

        make("a a\na a\n");
        keys("qq:%s/a/b/g<CR>q");
        keys("u@q");
        UNSCOPED_INFO("from a macro over two lines");
        CHECK(vim->message() == std::string("4 substitutions on 2 lines"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a yank of lines leaves the caret where its motion or object began them", "[alvimkeymap]")
    {
        ALCodeEditor& e = make("  alpha beta\n  gamma delta\n  epsilon zeta\n\n  eta theta\n  iota kappa");
        const auto yank_from = [&](S32 line, S32 column, const char* sequence) {
            e.setCaret(ALTextPos(line, column));
            keys(sequence);
            return caretText();
        };
        CHECK(yank_from(1, 5, "yy") == std::string("1:5"));
        CHECK(yank_from(1, 5, "2yy") == std::string("1:5"));
        CHECK(yank_from(1, 5, "Y") == std::string("1:5"));
        CHECK(yank_from(1, 5, "yj") == std::string("1:5"));
        UNSCOPED_INFO("yk: where k went");
        CHECK(yank_from(1, 5, "yk") == std::string("0:5"));
        UNSCOPED_INFO("y-: where - went");
        CHECK(yank_from(1, 5, "y-") == std::string("0:2"));
        CHECK(yank_from(1, 5, "ygg") == std::string("0:2"));
        UNSCOPED_INFO("yip: the paragraph's start");
        CHECK(yank_from(1, 5, "yip") == std::string("0:0"));
        UNSCOPED_INFO("yip on its first line");
        CHECK(yank_from(0, 5, "yip") == std::string("0:0"));
        UNSCOPED_INFO("Vy: the line's start");
        CHECK(yank_from(1, 5, "Vy") == std::string("1:0"));
        UNSCOPED_INFO("Vjy: the first line's");
        CHECK(yank_from(1, 5, "Vjy") == std::string("1:0"));
        UNSCOPED_INFO("Vky: the caret, above the lines selected from");
        CHECK(yank_from(1, 5, "Vky") == std::string("0:5"));
        CHECK(yank_from(1, 5, "vjY") == std::string("1:0"));
        CHECK(yank_from(1, 5, "vkY") == std::string("0:5"));
        e.setCaret(ALTextPos(0, 5));
        ex("2,4y");
        UNSCOPED_INFO(":2,4y leaves the caret");
        CHECK(caretText() == std::string("0:5"));

        make("ab\nabcdefgh");
        editor->setCaret(ALTextPos(1, 6));
        keys("yk");
        UNSCOPED_INFO("yk onto a shorter line: its last character");
        CHECK(caretText() == std::string("0:1"));

        make("Hello world.\nnext");
        editor->setCaret(ALTextPos(0, 3));
        keys("yis");
        UNSCOPED_INFO("yis over the line: its start");
        CHECK(caretText() == std::string("0:0"));

        make("x \"\" y");
        editor->setCaret(ALTextPos(0, 2));
        keys("di\"");
        UNSCOPED_INFO("di\" between two quotes takes nothing");
        CHECK(flat(editor->text()) == std::string("x \"\" y"));
        UNSCOPED_INFO("and leaves the caret between them");
        CHECK(caretText() == std::string("0:3"));
        editor->setCaret(ALTextPos(0, 0));
        keys("di\"iX<Esc>");
        UNSCOPED_INFO("from before them too, and does not fail");
        CHECK(flat(editor->text()) == std::string("x \"X\" y"));

        make("f() y");
        editor->setCaret(ALTextPos(0, 1));
        keys("di(");
        UNSCOPED_INFO("di( on () at the closer");
        CHECK(caretText() == std::string("0:2"));

        make("<a></a> x");
        editor->setCaret(ALTextPos(0, 1));
        keys("dit");
        UNSCOPED_INFO("dit on an empty tag between its tags");
        CHECK(caretText() == std::string("0:3"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a counted $ from the last line fails in place as vim's cursor_down does", "[alvimkeymap]")
    {
        const char* text = "abcdefgh\nghi jkl";
        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("2$");
        UNSCOPED_INFO("2$ on the last line stays");
        CHECK(caretText() == std::string("1:2"));
        keys("k");
        UNSCOPED_INFO("k after it to the line's end");
        CHECK(caretText() == std::string("0:7"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("d2$");
        UNSCOPED_INFO("d2$ takes nothing");
        CHECK(flat(editor->text()) == std::string("abcdefgh|ghi jkl"));
        UNSCOPED_INFO("and leaves the caret");
        CHECK(caretText() == std::string("1:2"));
        keys("k");
        UNSCOPED_INFO("k after it to the line's end");
        CHECK(caretText() == std::string("0:7"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("yly2$");
        UNSCOPED_INFO("y2$ yanks nothing");
        CHECK(vim->registerText('"') == std::string("i"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("c2$");
        UNSCOPED_INFO("c2$ inserts nothing");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        keys("X");
        UNSCOPED_INFO("and the X after it is normal mode's");
        CHECK(flat(editor->text()) == std::string("abcdefgh|gi jkl"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("2D");
        UNSCOPED_INFO("2D takes nothing");
        CHECK(flat(editor->text()) == std::string("abcdefgh|ghi jkl"));
        keys("k");
        UNSCOPED_INFO("k after it to the line's end");
        CHECK(caretText() == std::string("0:7"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("2C");
        UNSCOPED_INFO("2C inserts nothing");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        UNSCOPED_INFO("and takes nothing");
        CHECK(flat(editor->text()) == std::string("abcdefgh|ghi jkl"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("2g_");
        UNSCOPED_INFO("2g_ on the last line stays");
        CHECK(caretText() == std::string("1:2"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("vl2$d");
        UNSCOPED_INFO("a visual 2$ leaves the selection as it was");
        CHECK(flat(editor->text()) == std::string("abcdefgh|ghjkl"));

        make(text);
        editor->setCaret(ALTextPos(1, 1));
        keys("<C-v>2$d");
        UNSCOPED_INFO("a block's to every line's end all the same");
        CHECK(flat(editor->text()) == std::string("abcdefgh|g"));

        make("abc def\nghi jkl\nmno pqr");
        editor->setCaret(ALTextPos(1, 2));
        keys("3$");
        UNSCOPED_INFO("3$ with one line below to its end");
        CHECK(caretText() == std::string("2:6"));
        editor->setCaret(ALTextPos(1, 2));
        keys("d3$");
        UNSCOPED_INFO("d3$ through it");
        CHECK(flat(editor->text()) == std::string("abc def|gh"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :d and :y take a register then a count; anything more is trailing", "[alvimkeymap]")
    {
        const char* text = "one\ntwo\nthree\nfour\nfive\nsix";
        make(text);
        editor->setCaret(ALTextPos(1, 1));
        ex("y a");
        CHECK(vim->registerText('a') == std::string("two"));
        UNSCOPED_INFO("the caret left where it was");
        CHECK(caretText() == std::string("1:1"));
        ex("y A 3");
        UNSCOPED_INFO(":y A 3 adds three lines");
        CHECK(vim->registerText('a') == std::string("two\ntwo\nthree\nfour"));
        UNSCOPED_INFO("and says so");
        CHECK(vim->message() == std::string("3 lines yanked into \"A"));
        ex("2,3y b 2");
        UNSCOPED_INFO(":2,3y b 2: two lines from the range's last");
        CHECK(vim->registerText('b') == std::string("three\nfour"));
        ex("y 3");
        UNSCOPED_INFO(":y 3: a count, not register 3");
        CHECK(vim->registerText('0') == std::string("two\nthree\nfour"));
        CHECK(vim->message() == std::string("3 lines yanked"));
        ex("y \"x");
        UNSCOPED_INFO("a \" begins a comment");
        CHECK(vim->registerText('0') == std::string("two"));
        UNSCOPED_INFO("and names no register");
        CHECK(vim->registerText('x') == std::string());
        ex("d x 3");
        CHECK(flat(editor->text()) == std::string("one|five|six"));
        UNSCOPED_INFO("into x");
        CHECK(vim->registerText('x') == std::string("two\nthree\nfour"));
        CHECK(vim->message() == std::string("3 fewer lines"));
        ex("d 2");
        UNSCOPED_INFO(":d 2 from the caret's line");
        CHECK(flat(editor->text()) == std::string("one"));

        make(text);
        editor->setCaret(ALTextPos(1, 1));
        ex("d a b");
        {
            INFO("more after the register");
            CHECK(vim->messageIsError());
            CHECK(vim->message() == "E488: Trailing characters: b");
        }
        UNSCOPED_INFO("takes nothing");
        CHECK(flat(editor->text()) == std::string("one|two|three|four|five|six"));
        ex("y a 3l");
        {
            INFO("more after the count");
            CHECK(vim->messageIsError());
            CHECK(vim->message() == "E488: Trailing characters: l");
        }
        UNSCOPED_INFO("yanks nothing");
        CHECK(vim->registerText('a') == std::string());
        ex("d 0");
        {
            INFO("a count of none");
            CHECK(vim->messageIsError());
            CHECK(vim->message() == "E939: Positive count required");
        }
        UNSCOPED_INFO("takes nothing either");
        CHECK(flat(editor->text()) == std::string("one|two|three|four|five|six"));
        ex("pu a b");
        {
            INFO(":put's register with more after it");
            CHECK(vim->messageIsError());
            CHECK(vim->message() == "E488: Trailing characters: b");
        }
        UNSCOPED_INFO("puts nothing");
        CHECK(flat(editor->text()) == std::string("one|two|three|four|five|six"));

        make(text);
        editor->setCaret(ALTextPos(1, 1));
        keys("\"a:y<CR>");
        UNSCOPED_INFO("a register named before the : is none of :y's");
        CHECK(vim->registerText('a') == std::string());
        UNSCOPED_INFO("which yanks into 0");
        CHECK(vim->registerText('0') == std::string("two"));

        make(text);
        keys(":g/o/d A<CR>");
        CHECK(flat(editor->text()) == std::string("three|five|six"));
        UNSCOPED_INFO("each line added to a");
        CHECK(vim->registerText('a') == std::string("one\ntwo\nfour"));
        UNSCOPED_INFO("and how many said once");
        CHECK(vim->message() == std::string("3 fewer lines"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: deletes and changes over more than two lines say how many as vim does", "[alvimkeymap]")
    {
        const char* text = "one\ntwo\nthree\nfour\nfive\nsix";
        make(text);
        editor->setCaret(ALTextPos(0, 1));
        keys("d/five<CR>");
        UNSCOPED_INFO("d/five from the first line's second character");
        CHECK(flat(editor->text()) == std::string("o|five|six"));
        CHECK(vim->message() == std::string("3 fewer lines"));

        make(text);
        editor->setCaret(ALTextPos(0, 1));
        keys("d/ive<CR>");
        UNSCOPED_INFO("d/ive into the fifth line");
        CHECK(flat(editor->text()) == std::string("oive|six"));
        CHECK(vim->message() == std::string("4 fewer lines"));

        make(text);
        editor->setCaret(ALTextPos(0, 1));
        keys("d/hree<CR>");
        UNSCOPED_INFO("d/hree into the third");
        CHECK(flat(editor->text()) == std::string("ohree|four|five|six"));
        UNSCOPED_INFO("two fewer: nothing said: " << vim->message());
        CHECK(vim->message().empty());

        make(text);
        editor->setCaret(ALTextPos(0, 1));
        keys("v3jx");
        CHECK(flat(editor->text()) == std::string("our|five|six"));
        CHECK(vim->message() == std::string("3 fewer lines"));

        make(text);
        editor->setCaret(ALTextPos(0, 1));
        keys("c/ive<CR>");
        UNSCOPED_INFO("c/ive inserts");
        CHECK(vim->mode() == ALVimKeymap::Mode::Insert);
        UNSCOPED_INFO("and says how many fewer");
        CHECK(vim->message() == std::string("4 fewer lines"));
        keys("X<Esc>");
        UNSCOPED_INFO("what it typed in their place");
        CHECK(flat(editor->text()) == std::string("oXive|six"));

        make(text);
        keys("4cc");
        UNSCOPED_INFO("4cc inserts");
        CHECK(vim->mode() == ALVimKeymap::Mode::Insert);
        UNSCOPED_INFO("the line it leaves not counted");
        CHECK(vim->message() == std::string("3 fewer lines"));
        keys("X<Esc>");
        UNSCOPED_INFO("4cc");
        CHECK(flat(editor->text()) == std::string("X|five|six"));

        make(text);
        keys("3cc");
        UNSCOPED_INFO("3cc: two fewer, nothing said: " << vim->message());
        CHECK(vim->message().empty());
        keys("<Esc>");

        make(text);
        editor->setCaret(ALTextPos(0, 1));
        keys("<C-v>3jd");
        UNSCOPED_INFO("a block over four lines");
        CHECK(flat(editor->text()) == std::string("oe|to|tree|fur|five|six"));
        UNSCOPED_INFO("takes no line: nothing said: " << vim->message());
        CHECK(vim->message().empty());

        make("a1\na2\na3\na4\n\nb");
        editor->setCaret(ALTextPos(0, 1));
        keys("d}");
        UNSCOPED_INFO("d} from the paragraph's second character");
        CHECK(flat(editor->text()) == std::string("a||b"));
        CHECK(vim->message() == std::string("3 fewer lines"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: visual p and P say how many more lines they put as a put does", "[alvimkeymap]")
    {
        const char* text = "one\ntwo\nthree\nfour\nfive\nsix";
        make(text);
        keys("3yyGVp");
        UNSCOPED_INFO("3yy then Vp");
        CHECK(flat(editor->text()) == std::string("one|two|three|four|five|one|two|three"));
        CHECK(vim->message() == std::string("3 more lines"));

        make(text);
        keys("3yyGVP");
        UNSCOPED_INFO("VP too");
        CHECK(vim->message() == std::string("3 more lines"));

        make(text);
        keys("3yyjjjV2jp");
        UNSCOPED_INFO("over as many lines");
        CHECK(flat(editor->text()) == std::string("one|two|three|one|two|three"));
        UNSCOPED_INFO("said all the same");
        CHECK(vim->message() == std::string("3 more lines"));

        make(text);
        keys("yyGV3p");
        UNSCOPED_INFO("a count's copies");
        CHECK(vim->message() == std::string("3 more lines"));

        make(text);
        keys("3yyjlvp");
        UNSCOPED_INFO("lines into a character");
        CHECK(flat(editor->text()) == std::string("one|t|one|two|three|o|three|four|five|six"));
        UNSCOPED_INFO("the line broken round them counted");
        CHECK(vim->message() == std::string("4 more lines"));

        make(text);
        editor->setCaret(ALTextPos(0, 1));
        keys("v3jyG0vp");
        UNSCOPED_INFO("characters over four lines");
        CHECK(flat(editor->text()) == std::string("one|two|three|four|five|ne|two|three|foix"));
        UNSCOPED_INFO("said by their breaks");
        CHECK(vim->message() == std::string("3 more lines"));

        make(text);
        keys("yyjV3jp");
        UNSCOPED_INFO("a line over four");
        CHECK(flat(editor->text()) == std::string("one|one|six"));
        UNSCOPED_INFO("nothing said of the lines it replaced: " << vim->message());
        CHECK(vim->message().empty());
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :put of vim's own : / and . registers and their errors when empty", "[alvimkeymap]")
    {
        make("one\ntwo");
        keys(":put :<CR>");
        UNSCOPED_INFO("no : line yet");
        CHECK(vim->message() == std::string("E30: No previous command line"));
        keys(":put .<CR>");
        UNSCOPED_INFO("nothing typed yet");
        CHECK(vim->message() == std::string("E29: No inserted text yet"));
        keys(":put /<CR>");
        UNSCOPED_INFO("no search yet");
        CHECK(vim->message() == std::string("E35: No previous regular expression"));
        UNSCOPED_INFO("nothing put");
        CHECK(flat(editor->text()) == std::string("one|two"));

        make("one\ntwo");
        keys(":s/o/0/<CR>:put :<CR>");
        UNSCOPED_INFO(": the line before");
        CHECK(flat(editor->text()) == std::string("0ne|s/o/0/|two"));
        keys(":put :<CR>");
        UNSCOPED_INFO("which is this one the second time");
        CHECK(flat(editor->text()) == std::string("0ne|s/o/0/|put :|two"));

        make("one\ntwo");
        keys("/tw<CR>:put /<CR>");
        UNSCOPED_INFO("/ the search");
        CHECK(flat(editor->text()) == std::string("one|two|tw"));

        make("one\ntwo");
        keys("Ahey<Esc>:put .<CR>");
        UNSCOPED_INFO(". typed in after the caret");
        CHECK(flat(editor->text()) == std::string("oneheyhey|two"));
        UNSCOPED_INFO("the caret on its last character");
        CHECK(caretText() == std::string("0:8"));

        make("one two\nthree");
        keys("wiab<Esc>");
        editor->setCaret(ALTextPos(0, 1));
        keys(":2put .<CR>");
        UNSCOPED_INFO("on the range's line, the column kept");
        CHECK(flat(editor->text()) == std::string("one abtwo|thabree"));
        CHECK(caretText() == std::string("1:3"));
        editor->setCaret(ALTextPos(0, 2));
        keys(":put! .<CR>");
        UNSCOPED_INFO("with ! at the caret");
        CHECK(flat(editor->text()) == std::string("onabe abtwo|thabree"));
        CHECK(caretText() == std::string("0:3"));
        editor->setCaret(ALTextPos(1, 0));
        keys(":0put .<CR>");
        UNSCOPED_INFO(":0put as :1put!");
        CHECK(flat(editor->text()) == std::string("abonabe abtwo|thabree"));

        make("a a a");
        keys(":s/a/b/<CR>:normal @:<CR>");
        UNSCOPED_INFO("@: in a line runs the one before it");
        CHECK(flat(editor->text()) == std::string("b b a"));
        keys("@:");
        UNSCOPED_INFO("and that line is never the : register");
        CHECK(flat(editor->text()) == std::string("b b b"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a block put into characters goes in as a block where they began", "[alvimkeymap]")
    {
        const char* text = "abcd\nefgh\nijklmn\nopqrst";
        make(text);
        keys("<C-v>jly3jlvlp");
        UNSCOPED_INFO("vp: past the last line, a line of blanks to the column");
        CHECK(flat(editor->text()) == std::string("abcd|efgh|ijklmn|oabrst| ef"));
        UNSCOPED_INFO("the caret at its corner");
        CHECK(caretText() == std::string("3:1"));
        const ALVimRegisters::Register taken = vim->shared().registers.fetch('"', false);
        {
            INFO("what it took in the unnamed register, as characters");
            CHECK(taken.text == "pq");
            CHECK_FALSE(taken.block);
            CHECK_FALSE(taken.linewise);
        }

        make(text);
        keys("<C-v>jly3jlvlP");
        UNSCOPED_INFO("vP the same");
        CHECK(flat(editor->text()) == std::string("abcd|efgh|ijklmn|oabrst| ef"));
        UNSCOPED_INFO("the block kept");
        CHECK(vim->shared().registers.fetch('"', false).block);

        make(text);
        keys("<C-v>jly2jlvjp");
        UNSCOPED_INFO("characters over lines");
        CHECK(flat(editor->text()) == std::string("abcd|efgh|iabqrst| ef"));
        CHECK(caretText() == std::string("2:1"));

        make(text);
        keys("<C-v>jly3jlvl2p");
        UNSCOPED_INFO("a count side by side");
        CHECK(flat(editor->text()) == std::string("abcd|efgh|ijklmn|oababrst| efef"));

        make("abcd\nefgh\nijklmn\nop");
        keys("<C-v>jly2j$vp");
        UNSCOPED_INFO("to the line's end: after its last");
        CHECK(flat(editor->text()) == std::string("abcd|efgh|ijklmab|op   ef"));
        CHECK(caretText() == std::string("2:5"));

        make("abcd\nefgh\nijklmn\no");
        keys("<C-v>jly2jllvlp");
        UNSCOPED_INFO("a short line under it padded");
        CHECK(flat(editor->text()) == std::string("abcd|efgh|ijabmn|o ef"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a | on the : line ends one command except inside :s :g and :normal", "[alvimkeymap]")
    {
        make("a c\na c");
        keys(":s/a/b/ | s/c/d/<CR>");
        UNSCOPED_INFO("both, on the line");
        CHECK(flat(editor->text()) == std::string("b d|a c"));

        make("a c");
        keys(":s/zzz/y/ | s/c/d/<CR>");
        UNSCOPED_INFO("an error ends it");
        CHECK(flat(editor->text()) == std::string("a c"));
        CHECK(vim->message() == std::string("E486: Pattern not found: zzz"));

        make("a|b c");
        keys(":s/a|b/X/|s/c/d/<CR>");
        UNSCOPED_INFO("in the pattern its own");
        CHECK(flat(editor->text()) == std::string("X d"));
        make("a c");
        keys(":s/a/x|y/|s/c/d/<CR>");
        UNSCOPED_INFO("in the replacement too");
        CHECK(flat(editor->text()) == std::string("x|y d"));
        make("a|c");
        keys(":s/a|c/z<CR>");
        UNSCOPED_INFO("and to the end where nothing closes it");
        CHECK(flat(editor->text()) == std::string("z"));

        make("a\nb\nc");
        keys(":3|d<CR>");
        UNSCOPED_INFO("after a range alone");
        CHECK(flat(editor->text()) == std::string("a|b"));
        make("a\nb\nc");
        keys(":1d|d<CR>");
        UNSCOPED_INFO("each its own range, or the caret's line");
        CHECK(flat(editor->text()) == std::string("c"));
        make("a\nb\nc");
        keys(":1d | 2d<CR>");
        UNSCOPED_INFO("a range after it");
        CHECK(flat(editor->text()) == std::string("b"));
        make("a\nb");
        keys(":1d|<CR>");
        UNSCOPED_INFO("nothing after it");
        CHECK(flat(editor->text()) == std::string("b"));

        make("a\nb\na");
        keys(":g/a/s/a/x/|s/x/y/<CR>");
        UNSCOPED_INFO(":g's command, each line");
        CHECK(flat(editor->text()) == std::string("y|b|y"));
        make("a\nb");
        keys(":normal Ax|y<CR>");
        UNSCOPED_INFO(":normal's keys");
        CHECK(flat(editor->text()) == std::string("ax|y|b"));

        make("ab");
        keys(":nnoremap Q x|s/b/c/<CR>");
        UNSCOPED_INFO("a mapping, then the next");
        CHECK(flat(editor->text()) == std::string("ac"));
        keys("Q");
        UNSCOPED_INFO("the mapping to the |");
        CHECK(flat(editor->text()) == std::string("c"));
        make("ab");
        keys(":nnoremap Q ax\\|y<CR>Q<Esc>");
        UNSCOPED_INFO("\\| a | in it");
        CHECK(flat(editor->text()) == std::string("ax|yb"));

        make("a c");
        keys(":s/a/b/c|s/c/d/<CR>");
        UNSCOPED_INFO("after an asking :s, nothing while it asks");
        CHECK(flat(editor->text()) == std::string("a c"));
        keys("y");
        UNSCOPED_INFO("the rest once it is done");
        CHECK(flat(editor->text()) == std::string("b d"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: a vimrc's line runs each command to its | until one fails", "[alvimkeymap]")
    {
        ALVimKeymap::Shared      shared;
        std::vector<std::string> errors;
        const auto               host = [](const std::string&) { return false; };
        ALVimKeymap::source(shared, "nnoremap Q x | nnoremap W ia\\|b<Esc>\nset nosuchthing | nnoremap E z\n\" a | comment\n", host, errors);
        const ALVimMappings& maps = shared.mappings;
        const ALVimMappings::Match q = maps.match(ALVimMappings::NORMAL, maps.keysOf("Q"), true);
        {
            INFO("the first");
            REQUIRE(q.full);
            CHECK(ALVimMappings::shown(q.full->to) == "x");
        }
        const ALVimMappings::Match w = maps.match(ALVimMappings::NORMAL, maps.keysOf("W"), true);
        {
            INFO("the second, its \\| a |");
            REQUIRE(w.full);
            CHECK(ALVimMappings::shown(w.full->to) == "ia<Bar>b<Esc>");
        }
        UNSCOPED_INFO("none after one that fails");
        CHECK(maps.match(ALVimMappings::NORMAL, maps.keysOf("E"), true).full == nullptr);
        UNSCOPED_INFO("the one said");
        REQUIRE(errors.size() == size_t(1));
        {
            INFO("by its line");
            CHECK(errors[0].find("line 2:") == 0);
            CHECK(errors[0].find("E518") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: :g reports what its commands made once when it is through", "[alvimkeymap]")
    {
        const char* five = "a1\nb\na2\nc\na3";
        const auto  run  = [&](const char* text, const char* line) {
            make(text);
            keys(line);
            return vim->message();
        };
        CHECK(run(five, ":g/a/d<CR>") == std::string("3 fewer lines"));
        UNSCOPED_INFO("two said of by none");
        CHECK(run("a\nb\na", ":g/a/d<CR>") == std::string());
        CHECK(run(five, ":g/a/s/a/x/<CR>") == std::string("3 substitutions on 3 lines"));
        UNSCOPED_INFO("several a line");
        CHECK(run("aa\nb\naaa", ":g/a/s/a/x/g<CR>") == std::string("5 substitutions on 2 lines"));
        UNSCOPED_INFO("none on some lines");
        CHECK(run(five, ":g/./s/a/x/<CR>") == std::string("3 substitutions on 3 lines"));
        UNSCOPED_INFO("and all made");
        CHECK(flat(editor->text()) == std::string("x1|b|x2|c|x3"));
        UNSCOPED_INFO("none on any");
        CHECK(run(five, ":g/a/s/zzz/y/<CR>") == std::string());
        UNSCOPED_INFO("and no error");
        CHECK_FALSE(vim->messageIsError());
        UNSCOPED_INFO("counted only, as substitutions");
        CHECK(run(five, ":g/./s/a//n<CR>") == std::string("3 substitutions on 3 lines"));
        CHECK(run(five, ":g/a/t.<CR>") == std::string("3 more lines"));
        UNSCOPED_INFO("each its own lines added up");
        CHECK(run("a\nb\na\nb\na\nb", ":g/a/t.|t.<CR>") == std::string("6 more lines"));
        CHECK(run("a\n1\na\n2\na\n3\na\n4", ":g/a/j<CR>") == std::string("4 fewer lines"));
        UNSCOPED_INFO(":j with a count");
        CHECK(run("a\n1\n2\na\n3\n4\na\n5\n6", ":g/a/j 3<CR>") == std::string("6 fewer lines"));
        UNSCOPED_INFO("and made");
        CHECK(flat(editor->text()) == std::string("a 1 2|a 3 4|a 5 6"));
        UNSCOPED_INFO("what each would say not said");
        CHECK(run("a\n1\n2\nx\na\n3\n4\ny", ":g/a/normal 3dd<CR>") == std::string("6 fewer lines"));
        UNSCOPED_INFO(":normal of none");
        CHECK(run(five, ":g/a/normal Ax<CR>") == std::string());
        UNSCOPED_INFO("a | after it");
        CHECK(run(five, ":g/a/d|s/b/q/<CR>") == std::string("3 fewer lines"));
        UNSCOPED_INFO("substitutions before lines");
        CHECK(run("a\na\na\na", ":g/a/s/a/x/|d<CR>") == std::string("4 substitutions on 4 lines"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: vim's own registers put by p P and Control-R and never yanked into", "[alvimkeymap]")
    {
        make("one\ntwo");
        keys(":s/o/0/<CR>\":p");
        UNSCOPED_INFO("\":p the last : line after the caret");
        CHECK(flat(editor->text()) == std::string("0s/o/0/ne|two"));
        UNSCOPED_INFO("on its last character");
        CHECK(caretText() == std::string("0:6"));
        make("one\ntwo");
        keys(":s/o/0/<CR>\":P");
        UNSCOPED_INFO("\":P before it");
        CHECK(flat(editor->text()) == std::string("s/o/0/0ne|two"));
        CHECK(caretText() == std::string("0:5"));
        make("one\ntwo");
        keys(":s/o/0/<CR>2\":p");
        UNSCOPED_INFO("a count's copies");
        CHECK(flat(editor->text()) == std::string("0s/o/0/s/o/0/ne|two"));
        make("one\ntwo");
        keys("/tw<CR>\"/p");
        UNSCOPED_INFO("\"/p the last search");
        CHECK(flat(editor->text()) == std::string("one|ttwwo"));
        UNSCOPED_INFO("on its last");
        CHECK(caretText() == std::string("1:2"));
        make("one\ntwo");
        keys("Axy<Esc>0\".p");
        UNSCOPED_INFO("\".p the last insert's text");
        CHECK(flat(editor->text()) == std::string("oxynexy|two"));
        CHECK(caretText() == std::string("0:2"));

        make("one two");
        keys(":s/o/0/<CR>\":yy");
        {
            INFO("\":yy: nothing yanked");
            CHECK_FALSE(vim->shared().registers.fetch('0', false).held);
            CHECK_FALSE(vim->shared().registers.fetch('"', false).held);
        }
        keys("\":dd\":x\":D");
        UNSCOPED_INFO("\":dd, \":x, \":D: nothing taken");
        CHECK(flat(editor->text()) == std::string("0ne two"));
        keys("\":cwZZ<Esc>");
        UNSCOPED_INFO("\":cw: nothing taken, typed where it was");
        CHECK(flat(editor->text()) == std::string("ZZ0ne two"));
        UNSCOPED_INFO("on what was typed");
        CHECK(caretText() == std::string("0:1"));

        make("one\ntwo");
        keys(":s/o/0/<CR>A<C-r>:<Esc>");
        UNSCOPED_INFO("Control-R : the last : line typed in");
        CHECK(flat(editor->text()) == std::string("0nes/o/0/|two"));
        make("one\ntwo");
        keys("/tw<CR>A<C-r>/<Esc>");
        UNSCOPED_INFO("Control-R / the last search");
        CHECK(flat(editor->text()) == std::string("one|twotw"));

        make("aab");
        keys("/a~*b<CR>");
        UNSCOPED_INFO("~ with no last replacement: vim's own error");
        CHECK(vim->message() == std::string("E33: No previous substitute regular expression"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: % and # name the host's file and its alternate; E32 and E23 without", "[alvimkeymap]")
    {
        const auto named = [this]() {
            vim->hooks().fileName = [](const ALTextView&, char name) { return std::string(name == '%' ? "Script.lsl" : "Other"); };
        };
        make("one\ntwo");
        named();
        keys("\"%p");
        UNSCOPED_INFO("\"%p the file's name after the caret");
        CHECK(flat(editor->text()) == std::string("oScript.lslne|two"));
        UNSCOPED_INFO("on its last character");
        CHECK(caretText() == std::string("0:10"));
        make("one\ntwo");
        named();
        keys("\"#P");
        UNSCOPED_INFO("\"#P the alternate's before it");
        CHECK(flat(editor->text()) == std::string("Otherone|two"));
        CHECK(caretText() == std::string("0:4"));
        make("one\ntwo");
        named();
        keys(":put %<CR>");
        UNSCOPED_INFO(":put % as a line");
        CHECK(flat(editor->text()) == std::string("one|Script.lsl|two"));
        make("one\ntwo");
        named();
        keys("A <C-r>%<Esc>");
        UNSCOPED_INFO("Control-R % typed in");
        CHECK(flat(editor->text()) == std::string("one Script.lsl|two"));

        make("one two");
        named();
        keys("\"%yy");
        {
            INFO("\"%yy: nothing yanked");
            CHECK_FALSE(vim->shared().registers.fetch('0', false).held);
            CHECK_FALSE(vim->shared().registers.fetch('"', false).held);
        }
        keys("\"#dd\"%x");
        UNSCOPED_INFO("\"#dd, \"%x: nothing taken");
        CHECK(flat(editor->text()) == std::string("one two"));
        keys("\"%cwZZ<Esc>");
        UNSCOPED_INFO("\"%cw: nothing taken, typed where it was");
        CHECK(flat(editor->text()) == std::string("ZZone two"));

        make("one\ntwo");
        named();
        std::vector<std::string> listed;
        vim->hooks().listing = [&listed](ALTextView&, const std::string& text) { listed.push_back(text); };
        keys("Axy<Esc>:s/o/0/<CR>/tw<CR>:registers<CR>");
        REQUIRE(listed.size() == size_t(1));
        UNSCOPED_INFO("the last insert, the : line, the file, the alternate, the search, in vim's order");
        CHECK(listed[0].find("\n  c  \".   xy\n  c  \":   s/o/0/\n  c  \"%   Script.lsl\n  c  \"#   Other\n  c  \"/   tw") != std::string::npos);
        keys(":reg %.<CR>");
        UNSCOPED_INFO("those named");
        CHECK(listed.back() == std::string("Type Name Content\n  c  \".   xy\n  c  \"%   Script.lsl"));

        make("one");
        keys("\"%p");
        UNSCOPED_INFO("no file: E32");
        CHECK(vim->message() == std::string("E32: No file name"));
        keys("\"#p");
        UNSCOPED_INFO("no alternate: E23");
        CHECK(vim->message() == std::string("E23: No alternate file"));
        keys(":put #<CR>");
        UNSCOPED_INFO("for :put too");
        CHECK(vim->message() == std::string("E23: No alternate file"));
        keys("A<C-r>%");
        UNSCOPED_INFO("and Control-R");
        CHECK(vim->message() == std::string("E32: No file name"));
        keys("<Esc>0vl\"%p");
        UNSCOPED_INFO("a visual put takes the selection out first");
        CHECK(flat(editor->text()) == std::string("e"));
        UNSCOPED_INFO("and says so");
        CHECK(vim->message() == std::string("E32: No file name"));
        make("one");
        keys("\".p");
        UNSCOPED_INFO("no insert yet: E29");
        CHECK(vim->message() == std::string("E29: No inserted text yet"));
        keys("\":p");
        UNSCOPED_INFO("no : line: E30");
        CHECK(vim->message() == std::string("E30: No previous command line"));
        keys("\"bp");
        UNSCOPED_INFO("a register: E353");
        CHECK(vim->message() == std::string("E353: Nothing in register b"));
    }

    TEST_CASE_METHOD(alvimkeymap_data, "alvimkeymap: an asking :s under :g asks line by line; a q and l are the line's own", "[alvimkeymap]")
    {
        const auto run = [&](const char* text, const char* line, const char* answers) {
            make(text);
            keys(line);
            keys(answers);
            return alvimkeymap_data::flat(editor->text());
        };
        UNSCOPED_INFO("y and n");
        CHECK(run("a1\nb\na2\na3", ":g/a/s/a/x/c<CR>", "yny") == std::string("x1|b|a2|x3"));
        UNSCOPED_INFO("a the line's rest");
        CHECK(run("aa1\nb\naa2\na3", ":g/a/s/a/x/gc<CR>", "annn") == std::string("xx1|b|aa2|a3"));
        UNSCOPED_INFO("q the line's");
        CHECK(run("a1\nb\na2\na3", ":g/a/s/a/x/c<CR>", "qyy") == std::string("a1|b|x2|x3"));
        UNSCOPED_INFO("l the line's");
        CHECK(run("a1\nb\na2\na3", ":g/a/s/a/x/c<CR>", "lyy") == std::string("x1|b|x2|x3"));
        UNSCOPED_INFO("said once through, as the :g's");
        CHECK(vim->message() == std::string("3 substitutions on 3 lines"));
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);

        UNSCOPED_INFO("what follows reads what the answer made");
        CHECK(run("a1\nb\na2\na3", ":g/a/s/a/x/c|s/x/y/<CR>", "yny") == std::string("y1|b|a2|y3"));
        UNSCOPED_INFO("all said together");
        CHECK(vim->message() == std::string("4 substitutions on 4 lines"));
        keys("u");
        UNSCOPED_INFO("one step to undo");
        CHECK(flat(editor->text()) == std::string("a1|b|a2|a3"));
        UNSCOPED_INFO("a :d after it");
        CHECK(run("a1\nb\na2\na3", ":g/a/s/a/x/c|d<CR>", "yny") == std::string("b"));
        UNSCOPED_INFO("its lines said");
        CHECK(vim->message() == std::string("3 fewer lines"));
        UNSCOPED_INFO("run after a q too");
        CHECK(run("a1 b\nb\na2 b\na3 b", ":g/a/s/a/x/c|s/b/B/<CR>", "nyq") == std::string("a1 B|b|x2 B|a3 B"));
        UNSCOPED_INFO("lines it adds not visited");
        CHECK(run("ax\nb\nax", ":g/a/s/x/X/c|t.<CR>", "yy") == std::string("aX|aX|b|aX|aX"));
        UNSCOPED_INFO("an asking :s after it asks in turn");
        CHECK(run("a x\nb\na x\na z", ":g/a/s/x/X/c|s/a/B/c<CR>", "ynyy") == std::string("a X|b|B X|a z"));
        UNSCOPED_INFO("the last line's still asking");
        CHECK(vim->mode() == ALVimKeymap::Mode::Confirm);
        keys("<Esc>");
        UNSCOPED_INFO("Escape its q");
        CHECK(flat(editor->text()) == std::string("a X|b|B X|a z"));
        UNSCOPED_INFO("and the :g through");
        CHECK(vim->message() == std::string("3 substitutions on 3 lines"));

        // A line handed over while it waits ends the asking and the :g,
        // what they made one step to undo.
        run("a1\nb\na2", ":g/a/s/a/x/c|s/1/9/<CR>", "y");
        UNSCOPED_INFO("waiting on the next line");
        CHECK(vim->mode() == ALVimKeymap::Mode::Confirm);
        ex("%s/b/Q/");
        UNSCOPED_INFO("the line run, nothing after the asking");
        CHECK(flat(editor->text()) == std::string("x9|Q|a2"));
        UNSCOPED_INFO("and asking no more");
        CHECK(vim->mode() == ALVimKeymap::Mode::Normal);
        keys("u");
        UNSCOPED_INFO("its step");
        CHECK(flat(editor->text()) == std::string("x9|b|a2"));
        keys("u");
        UNSCOPED_INFO("then the :g's");
        CHECK(flat(editor->text()) == std::string("a1|b|a2"));
    }
}
