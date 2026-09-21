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

#include "../alcodeeditor.h"
#include "../alvimkeymap.h"
#include "../llclipboard.h"
#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"
#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>

class LLAvatarName;
const std::string gVimTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gVimTestAnonName;
}

namespace tut
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
        }

        ALCodeEditor& make(const char* text, const char* syntax = "lsl")
        {
            if (!ui.ok())
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
            p.auto_complete = false;
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
                    else if (name.size() == 3 && name[0] == 'C' && name[1] == '-')
                    {
                        editor->handleKeyHere(static_cast<KEY>(toupper(name[2])), MASK_CONTROL);
                    }
                    else
                    {
                        fail("unknown key " + name);
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

    typedef test_group<alvimkeymap_data> alvimkeymap_group;
    typedef alvimkeymap_group::object    alvimkeymap_object;
    alvimkeymap_group                    alvimkeymap_group_instance("alvimkeymap");

    template<> template<>
    void alvimkeymap_object::test<1>()
    {
        set_test_name("motions: h j k l w b e 0 ^ $ gg G f t ; % with counts, the caret on a character");
        make("integer count = 0;\n  float half(integer n) { return n / 2.0; }\nx\n");
        ensure("normal to begin with", vim->mode() == ALVimKeymap::Mode::Normal);
        keys("w");
        ensure_equals("w to the next word", caretText(), std::string("0:8"));
        keys("2w");
        ensure_equals("2w two on", caretText(), std::string("0:16"));
        keys("b");
        ensure_equals("b back", caretText(), std::string("0:14"));
        keys("e");
        ensure_equals("e to the word's end", caretText(), std::string("0:16"));
        keys("$");
        ensure_equals("$ on the last character", caretText(), std::string("0:17"));
        keys("0");
        ensure_equals("0 to the start", caretText(), std::string("0:0"));
        keys("j^");
        ensure_equals("j then ^ to the first non-blank", caretText(), std::string("1:2"));
        keys("fn");
        ensure_equals("f finds the n in integer", caretText(), std::string("1:14"));
        keys(";");
        ensure_equals("; the next", caretText(), std::string("1:21"));
        keys("t{");
        ensure_equals("t stops before the brace", caretText(), std::string("1:23"));
        keys("l%");
        ensure_equals("% to the matching brace", caretText(), std::string("1:42"));
        keys("gg");
        ensure_equals("gg to the top", caretText(), std::string("0:0"));
        keys("G");
        ensure_equals("G to the last line", caretText(), std::string("3:0"));
        keys("2G");
        ensure_equals("2G to line 2", caretText(), std::string("1:2"));
        keys("k3l");
        ensure_equals("k keeps the column, then 3l", caretText(), std::string("0:5"));
        keys("20l");
        ensure_equals("l stops at the line's last character", caretText(), std::string("0:17"));
    }

    template<> template<>
    void alvimkeymap_object::test<2>()
    {
        set_test_name("operators with motions and counts: dw d2w dd cw yy p x J u and .");
        ALCodeEditor& e = make("one two three\nfour five\nsix\n");
        keys("dw");
        ensure_equals("dw takes the word and its space", flat(e.text()), std::string("two three|four five|six|"));
        keys("d2w");
        ensure_equals("d2w takes two, to the line's end", flat(e.text()), std::string("|four five|six|"));
        keys("jdd");
        ensure_equals("dd takes the line", flat(e.text()), std::string("|six|"));
        ensure_equals("caret on the line that took its place", caretText(), std::string("1:0"));
        keys("u");
        ensure_equals("u brings it back", flat(e.text()), std::string("|four five|six|"));
        keys("cwseven<Esc>");
        ensure_equals("cw changes the word, not the space", flat(e.text()), std::string("|seven five|six|"));
        ensure("back in normal mode", vim->mode() == ALVimKeymap::Mode::Normal);
        ensure_equals("caret on the last character typed", caretText(), std::string("1:4"));
        keys("yyjp");
        ensure_equals("yy then p puts the line below", flat(e.text()), std::string("|seven five|six|seven five|"));
        keys("x");
        ensure_equals("x takes a character", flat(e.text()), std::string("|seven five|six|even five|"));
        keys("..");
        ensure_equals(". does it again, twice", flat(e.text()), std::string("|seven five|six|en five|"));
        keys("ggjJ");
        ensure_equals("J joins with a space", flat(e.text()), std::string("|seven five six|en five|"));
        keys("3u");
        ensure_equals("3u undoes three: the join and two repeats", flat(e.text()), std::string("|seven five|six|even five|"));
        keys("<C-r>");
        ensure_equals("control-R redoes one", flat(e.text()), std::string("|seven five|six|ven five|"));
    }

    template<> template<>
    void alvimkeymap_object::test<3>()
    {
        set_test_name("insert mode: i a I A o O with counts, escape stepping back, and . repeating an insert");
        ALCodeEditor& e = make("abc\n");
        keys("ix<Esc>");
        ensure_equals("i inserts before", flat(e.text()), std::string("xabc|"));
        ensure_equals("escape steps back onto what was typed", caretText(), std::string("0:0"));
        keys("ay<Esc>");
        ensure_equals("a appends after", flat(e.text()), std::string("xyabc|"));
        keys("A!<Esc>");
        ensure_equals("A at the line's end", flat(e.text()), std::string("xyabc!|"));
        keys("I-<Esc>");
        ensure_equals("I at the line's start", flat(e.text()), std::string("-xyabc!|"));
        keys("onew<Esc>");
        ensure_equals("o opens a line below", flat(e.text()), std::string("-xyabc!|new|"));
        keys("Oabove<Esc>");
        ensure_equals("O opens a line above", flat(e.text()), std::string("-xyabc!|above|new|"));
        keys("03ihi<Esc>");
        ensure_equals("3i types it three times", flat(e.text()), std::string("-xyabc!|hihihiabove|new|"));
        keys("j0.");
        ensure_equals(". repeats the insert on the next line", flat(e.text()), std::string("-xyabc!|hihihiabove|hihihinew|"));
        ensure("an insert is one step to undo", [&] { keys("u"); return flat(e.text()) == "-xyabc!|hihihiabove|new|"; }());
    }

    template<> template<>
    void alvimkeymap_object::test<4>()
    {
        set_test_name("text objects: diw daw ci\" da( yi{ and dap");
        ALCodeEditor& e = make("say(\"hello there\", (a + b) * 2)\n\nnext para\nline two\n\nlast\n");
        keys("fhdiw");
        ensure_equals("diw takes the word", flat(e.text()), std::string("say(\" there\", (a + b) * 2)||next para|line two||last|"));
        keys("u");
        keys("daw");
        ensure_equals("daw takes the word and the space after", flat(e.text()), std::string("say(\"there\", (a + b) * 2)||next para|line two||last|"));
        keys("u0fhci\"bye<Esc>");
        ensure_equals("ci\" changes what the quotes hold", flat(e.text()), std::string("say(\"bye\", (a + b) * 2)||next para|line two||last|"));
        keys("fada(");
        ensure_equals("da( takes the brackets and what they hold", flat(e.text()), std::string("say(\"bye\",  * 2)||next para|line two||last|"));
        keys("0f(yi(");
        ensure_equals("yi( yanks the inside", vim->registerText('"'), std::string("\"bye\",  * 2"));
        keys("2jdap");
        ensure_equals("dap takes the paragraph and the blank after it", flat(e.text()), std::string("say(\"bye\",  * 2)||last|"));
    }

    template<> template<>
    void alvimkeymap_object::test<5>()
    {
        set_test_name("visual modes: v with a motion and an operator, V for lines, control-V for a block with I");
        ALCodeEditor& e = make("one two\nthree four\nfive six\n");
        keys("vey");
        ensure_equals("v e y yanks the word", vim->registerText('"'), std::string("one"));
        ensure("and leaves visual", vim->mode() == ALVimKeymap::Mode::Normal);
        keys("vjd");
        ensure_equals("v j d takes across the lines, the character under the caret too", flat(e.text()), std::string("hree four|five six|"));
        keys("Vd");
        ensure_equals("V d takes the line", flat(e.text()), std::string("five six|"));
        keys("u");
        keys("<C-v>jIx <Esc>");
        ensure_equals("a block insert goes on every line", flat(e.text()), std::string("x hree four|x five six|"));
        keys("gg<C-v>jld");
        ensure_equals("a block delete takes the columns", flat(e.text()), std::string("hree four|five six|"));
        keys("v$y");
        ensure_equals("v $ y yanks to the line's end", vim->registerText('"'), std::string("hree four"));
        keys("vjo");
        ensure_equals("o swaps the ends", caretText(), std::string("0:0"));
        keys("<Esc>");
        ensure("escape leaves visual", vim->mode() == ALVimKeymap::Mode::Normal && !e.hasSelection());
    }

    template<> template<>
    void alvimkeymap_object::test<6>()
    {
        set_test_name("registers, marks and the search: \"ayy \"ap, ma `a 'a, / n N * and :s");
        ALCodeEditor& e = make("alpha beta\ngamma alpha\ndelta\n");
        keys("\"ayyj\"ap");
        ensure_equals("a named register keeps its line", flat(e.text()), std::string("alpha beta|gamma alpha|alpha beta|delta|"));
        ensure_equals("and the clipboard has it too", vim->registerText('"'), std::string("alpha beta"));
        keys("ggmaG`a");
        ensure_equals("a mark goes back to the place", caretText(), std::string("0:0"));
        keys("jl'a");
        ensure_equals("' goes to the line's first non-blank", caretText(), std::string("0:0"));
        keys("/alpha<CR>");
        ensure_equals("/ finds the next", caretText(), std::string("1:6"));
        keys("n");
        ensure_equals("n the one after", caretText(), std::string("2:0"));
        keys("N");
        ensure_equals("N back again", caretText(), std::string("1:6"));
        keys("*");
        ensure_equals("* the word under the caret, whole", caretText(), std::string("2:0"));
        keys("/nothing here<CR>");
        ensure("a miss is said", vim->messageIsError() && vim->message().find("Pattern not found") != std::string::npos);
        keys(":%s/alpha/omega/g<CR>");
        ensure_equals(":s over every line", flat(e.text()), std::string("omega beta|gamma omega|omega beta|delta|"));
        keys(":2<CR>");
        ensure_equals(": with a number goes there", caretText(), std::string("1:0"));
        keys(":3,4d<CR>");
        ensure_equals(":d over a range", flat(e.text()), std::string("omega beta|gamma omega|"));
    }

    template<> template<>
    void alvimkeymap_object::test<7>()
    {
        set_test_name("the : line reaches the hooks, indents, and refuses what nothing knows");
        ALCodeEditor& e = make("a\n    b\nc\n");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return name == "w" || name == "q!" || name == "format";
        };
        keys(":w<CR>");
        keys(":q!<CR>");
        keys(":format now<CR>");
        ensure_equals("three heard", heard.size(), size_t(3));
        ensure_equals("w", heard[0], std::string("w|"));
        ensure_equals("q! with its bang", heard[1], std::string("q!|"));
        ensure_equals("with its arguments", heard[2], std::string("format|now"));
        keys(":frobnicate<CR>");
        ensure("an unknown command is said", vim->messageIsError() && vim->message().find("Not an editor command") != std::string::npos);
        e.setSoftTabs(true);
        keys("gg>>");
        ensure_equals(">> indents the line", flat(e.text()), std::string("    a|    b|c|"));
        keys("j<<");
        ensure_equals("<< outdents", flat(e.text()), std::string("    a|b|c|"));
        keys("<Esc>");
        ensure("escape in normal mode is taken, not the floater's", e.handleKeyHere(KEY_ESCAPE, MASK_NONE));
        keys("i");
        ensure("insert says so", vim->status() == "-- INSERT --" && vim->inserting());
        keys("<Esc>v");
        ensure("visual says so", vim->status() == "-- VISUAL --" && !vim->inserting());
        keys("<Esc>3d");
        ensure_equals("what is pending shows", vim->status(), std::string("3d"));
        keys("<Esc>");
    }
}
