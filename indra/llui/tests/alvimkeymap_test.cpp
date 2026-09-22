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
                    else if (name == "Up" || name == "Down")
                    {
                        editor->handleKeyHere(name == "Up" ? KEY_UP : KEY_DOWN, MASK_NONE);
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

        // The local point of a column on a line, in the middle of its row.
        void pointOf(S32 line, S32 column, S32& x, S32& y)
        {
            const LLRect text = editor->textRect();
            S32          row;
            const F32    xrel = editor->layout().xOf(line, column, &row);
            x                 = text.mLeft + static_cast<S32>(xrel) + 1;
            y                 = text.mTop - (editor->layout().lineTop(line) + row * editor->layout().rowHeight()) - editor->layout().rowHeight() / 2;
        }
        void click(S32 line, S32 column)
        {
            S32 x, y;
            pointOf(line, column, x, y);
            editor->handleMouseDown(x, y, MASK_NONE);
            editor->handleMouseUp(x, y, MASK_NONE);
        }
        void drag(S32 line, S32 column, S32 to_line, S32 to_column)
        {
            S32 x, y, x2, y2;
            pointOf(line, column, x, y);
            pointOf(to_line, to_column, x2, y2);
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

    template<> template<>
    void alvimkeymap_object::test<8>()
    {
        set_test_name("the mouse: a click puts the caret on a character, a drag is a visual selection an operator takes, a click leaves visual");
        ALCodeEditor& e = make("one two\nthree four\nfive six\n");
        click(0, 7);
        ensure_equals("past the end of a line lands on its last character", caretText(), std::string("0:6"));
        ensure("still normal", vim->mode() == ALVimKeymap::Mode::Normal);
        drag(1, 0, 1, 5);
        ensure("a drag is visual", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("d");
        ensure_equals("and the operator takes what was dragged, up to where the drag stopped", flat(e.text()), std::string("one two| four|five six|"));
        ensure("back to normal", vim->mode() == ALVimKeymap::Mode::Normal);
        drag(2, 4, 2, 0);
        ensure("dragged backwards is visual too", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("y");
        ensure_equals("yanked, as far as the drag reached", vim->registerText('"'), std::string("five"));
        keys("V");
        ensure("visual line", vim->mode() == ALVimKeymap::Mode::VisualLine);
        click(0, 1);
        ensure("a click leaves visual for normal, at the click", vim->mode() == ALVimKeymap::Mode::Normal && caretText() == "0:1" && !e.hasSelection());
        keys("i");
        click(1, 2);
        ensure("a click in insert mode stays in insert mode", vim->mode() == ALVimKeymap::Mode::Insert && caretText() == "1:2");
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<9>()
    {
        set_test_name(". after a visual operation does it over as much again from the caret, however the selection was made");
        ALCodeEditor& e = make("aaaa\nbbbb\ncccc\ndddd\neeee\nffff\n");
        keys("Vjd");
        ensure_equals("two lines gone", flat(e.text()), std::string("cccc|dddd|eeee|ffff|"));
        keys(".");
        ensure_equals(". takes two more", flat(e.text()), std::string("eeee|ffff|"));
        keys("u.");
        e.setText("abcdef\nghijkl\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("gg0vlld");
        ensure_equals("three characters gone", flat(e.text()), std::string("def|ghijkl|"));
        keys("j0.");
        ensure_equals(". takes three from the caret", flat(e.text()), std::string("def|jkl|"));
        keys("gg0vlc-<Esc>");
        ensure_equals("a change over the selection", flat(e.text()), std::string("-f|jkl|"));
        keys("j0.");
        ensure_equals(". changes as much again with the same text", flat(e.text()), std::string("-f|-l|"));
        // A selection the mouse made repeats the same way.
        e.setText("one two\nthree four\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        drag(0, 0, 0, 3);
        keys("d");
        ensure_equals("the dragged three gone", flat(e.text()), std::string(" two|three four|"));
        keys("j0.");
        ensure_equals(". takes three from the caret", flat(e.text()), std::string(" two|ee four|"));
    }

    template<> template<>
    void alvimkeymap_object::test<10>()
    {
        set_test_name("macros: q records keys into a register as text, @ plays them with a count, @@ again, qA adds");
        ALCodeEditor& e = make("one\ntwo\nthree\nfour\n");
        keys("qaA;<Esc>jq");
        ensure("recording stopped", !vim->recording());
        ensure_equals("the register holds the keys as text", vim->registerText('a'), std::string("A;<Esc>j"));
        ensure_equals("the recording ran as it was typed", flat(e.text()), std::string("one;|two|three|four|"));
        ensure("recording says so while it runs", true);
        keys("@a");
        ensure_equals("played once", flat(e.text()), std::string("one;|two;|three|four|"));
        keys("2@@");
        ensure_equals("@@ plays the last again, the count times", flat(e.text()), std::string("one;|two;|three;|four;|"));
        keys("qbI-<Esc>q");
        keys("qBA!<Esc>q");
        ensure_equals("qB adds to b", vim->registerText('b'), std::string("I-<Esc>A!<Esc>"));
        keys("gg@b");
        // The recordings ran where they were typed, on the empty last line.
        ensure_equals("both halves played", flat(e.text()), std::string("-one;!|two;|three;|four;|-!"));
        // Playing a register that plays itself stops rather than running away.
        keys("qc@cq");
        keys("@c");
        ensure("too recursive is said", vim->messageIsError());
        keys("qa");
        ensure("recording shows in the status", vim->status().find("recording @a") == 0);
        keys("q");
        // The keys decode as they were encoded.
        const std::vector<ALVimKeymap::Input> back = ALVimKeymap::decodeInputs("a<lt>b<C-r><Esc>");
        ensure_equals("five inputs", back.size(), size_t(5));
        ensure("a literal <", back[1].isChar && back[1].ch == '<');
        ensure("a control chord", !back[3].isChar && back[3].key == 'R' && (back[3].mask & MASK_CONTROL));
        ensure_equals("and encode back", ALVimKeymap::encodeInputs(back), std::string("a<lt>b<C-r><Esc>"));
    }

    template<> template<>
    void alvimkeymap_object::test<11>()
    {
        set_test_name("control-A and control-X change the number under or after the caret, with counts and a minus");
        ALCodeEditor& e = make("x = 5; y = 10\nn = -3\nnone here\n");
        keys("<C-a>");
        ensure_equals("the first number after the caret, up one", e.document().line(0), std::string("x = 6; y = 10"));
        ensure_equals("the caret on its last digit", caretText(), std::string("0:4"));
        keys("5<C-x>");
        ensure_equals("down five", e.document().line(0), std::string("x = 1; y = 10"));
        keys("w<C-a>");
        ensure_equals("the number after the caret", e.document().line(0), std::string("x = 1; y = 11"));
        keys("<C-a>");
        ensure_equals("under the caret still", e.document().line(0), std::string("x = 1; y = 12"));
        keys("j0<C-a>");
        ensure_equals("a minus is the number's", e.document().line(1), std::string("n = -2"));
        keys("3<C-a>");
        ensure_equals("through zero", e.document().line(1), std::string("n = 1"));
        keys("j<C-a>");
        ensure_equals("nothing to change is left alone", e.document().line(2), std::string("none here"));
        keys("k.");
        ensure_equals(". repeats it", e.document().line(1), std::string("n = 4"));
        // Hex and binary as vim's nrformats reads them, the width and the
        // case kept; leading zeros keep a decimal's width too.
        e.setText("a = 0x0fF; b = 0b0111; c = 007; d = 0x9");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("0<C-a>");
        ensure_equals("hex, wrapping over its width, in the case it had", e.document().line(0), std::string("a = 0x100; b = 0b0111; c = 007; d = 0x9"));
        keys("w<C-a>");
        ensure_equals("binary", e.document().line(0), std::string("a = 0x100; b = 0b1000; c = 007; d = 0x9"));
        keys("f7<C-a>");
        ensure_equals("leading zeros kept", e.document().line(0), std::string("a = 0x100; b = 0b1000; c = 008; d = 0x9"));
        keys("$<C-a>");
        ensure_equals("a hex digit becomes a letter", e.document().line(0), std::string("a = 0x100; b = 0b1000; c = 008; d = 0xa"));
        keys("0fF<C-x>");
        ensure_equals("under the caret inside a hex number", e.document().line(0), std::string("a = 0x0ff; b = 0b1000; c = 008; d = 0xa"));
    }

    template<> template<>
    void alvimkeymap_object::test<12>()
    {
        set_test_name(":g and :v run a command on the lines a pattern picks out, as one undo; :normal types on a range");
        ALCodeEditor& e = make("keep 1\ndrop 2\nkeep 3\ndrop 4\nkeep 5\n");
        keys(":g/drop/d<CR>");
        ensure_equals("the dropped lines gone", flat(e.text()), std::string("keep 1|keep 3|keep 5|"));
        keys("u");
        ensure_equals("undone as one", flat(e.text()), std::string("keep 1|drop 2|keep 3|drop 4|keep 5|"));
        keys(":v/drop/d<CR>");
        ensure_equals(":v takes the others, the empty line after the final newline not among them", flat(e.text()), std::string("drop 2|drop 4|"));
        keys("u:g/drop/s/drop/held/<CR>");
        ensure_equals("a substitution on the lines picked out", flat(e.text()), std::string("keep 1|held 2|keep 3|held 4|keep 5|"));
        keys(":g/keep/normal A;<CR>");
        ensure_equals(":normal types on each", flat(e.text()), std::string("keep 1;|held 2|keep 3;|held 4|keep 5;|"));
        ensure("and ends in normal mode", vim->mode() == ALVimKeymap::Mode::Normal);
        keys(":2,3g/held/d<CR>");
        ensure_equals("a range narrows it", flat(e.text()), std::string("keep 1;|keep 3;|held 4|keep 5;|"));
        keys(":g/zzz/d<CR>");
        ensure("nothing matching is said", vim->messageIsError());
        keys(":g/keep/<CR>");
        ensure("with no command, how many is said", !vim->messageIsError() && vim->message() == "3 lines");
    }
    template<> template<>
    void alvimkeymap_object::test<13>()
    {
        set_test_name(":s takes vim's replacement spelling and flags, and & g& :& :&& do the last one again");
        ALCodeEditor& e = make("cat hat\ncat bat\nrat\n");
        // The pattern in vim's spelling -- groups in \( \) -- and the
        // replacement too.
        keys(":s/\\(c\\)at/[&-\\1]/<CR>");
        ensure_equals("& is the match and \\1 a group", flat(e.text()), std::string("[cat-c] hat|cat bat|rat|"));
        keys("j:s/at/~x/g<CR>");
        ensure_equals("~ is the last replacement, read again as one", flat(e.text()), std::string("[cat-c] hat|c[at-]x b[at-]x|rat|"));
        keys("u:s/at/A\\&B/gn<CR>");
        ensure("n counts without changing", !vim->messageIsError() && vim->message() == "2 matches on 1 line");
        ensure_equals("nothing changed", flat(e.text()), std::string("[cat-c] hat|cat bat|rat|"));
        keys(":s/at/og/<CR>");
        ensure_equals("the first on the line", flat(e.text()), std::string("[cat-c] hat|cog bat|rat|"));
        keys("&");
        ensure_equals("& again on the line, without the flags", flat(e.text()), std::string("[cat-c] hat|cog bog|rat|"));
        keys("uu:s/at/og/g<CR>u");
        keys("gg:&&<CR>");
        ensure_equals(":&& does the last :s again with its flags", flat(e.text()), std::string("[cog-c] hog|cat bat|rat|"));
        keys("ug&");
        ensure_equals("g& on every line, with the flags", flat(e.text()), std::string("[cog-c] hog|cog bog|rog|"));
        keys("u&");
        ensure_equals("& on this line without them, as vim has it, which forgets them", flat(e.text()), std::string("[cog-c] hat|cat bat|rat|"));
        keys("u:&&<CR>");
        ensure_equals("so :&& now has none", flat(e.text()), std::string("[cog-c] hat|cat bat|rat|"));
        keys("u");
        keys(":s/zzz/y/e<CR>");
        ensure("e says nothing where nothing matches", !vim->messageIsError());
        keys(":s/zzz/y/<CR>");
        ensure("without e it does", vim->messageIsError());
        keys("gg:s/hat/one\\rtwo/<CR>");
        ensure_equals("\\r breaks the line", flat(e.text()), std::string("[cat-c] one|two|cat bat|rat|"));
        keys(":s/two/\\U&/<CR>");
        ensure_equals("\\U upper-cases what follows", flat(e.text()), std::string("[cat-c] one|TWO|cat bat|rat|"));
        keys(":s/t/$/I<CR>");
        ensure("I matches the case as written: no lower t here", vim->messageIsError());
        keys(":s/T/$/I<CR>");
        ensure_equals("a $ is a $", flat(e.text()), std::string("[cat-c] one|$WO|cat bat|rat|"));
    }

    template<> template<>
    void alvimkeymap_object::test<14>()
    {
        set_test_name("the : and / lines keep a history for Up and Down, q: and q/ open the line at the last, @: runs it again");
        ALCodeEditor& e = make("a1\na2\na3\na4\n");
        keys(":s/a/b/<CR>");
        keys("j:s/a/c/<CR>");
        keys("j:<Up>");
        ensure_equals("Up brings the last line back", vim->commandLine(), std::string("s/a/c/"));
        keys("<Up>");
        ensure_equals("and the one before", vim->commandLine(), std::string("s/a/b/"));
        keys("<Up>");
        ensure_equals("no further", vim->commandLine(), std::string("s/a/b/"));
        keys("<Down><Down>");
        ensure_equals("Down past the newest is what was typed", vim->commandLine(), std::string(""));
        keys("s/a/b<Up>");
        ensure_equals("Up walks only the lines that start as this one", vim->commandLine(), std::string("s/a/b/"));
        keys("<CR>");
        ensure_equals("run", flat(e.text()), std::string("b1|c2|b3|a4|"));
        keys("j@:");
        ensure_equals("@: runs the last line again", flat(e.text()), std::string("b1|c2|b3|b4|"));
        keys("gg/a4<CR>");
        keys("q/");
        ensure("q/ opens the search line", vim->mode() == ALVimKeymap::Mode::Search);
        ensure_equals("at the last search", vim->commandLine(), std::string("a4"));
        keys("<Esc>q:");
        ensure("q: opens the : line", vim->mode() == ALVimKeymap::Mode::Command);
        ensure_equals("at the last command", vim->commandLine(), std::string("s/a/b/"));
        keys("<Up>");
        ensure_equals("with the history above it", vim->commandLine(), std::string("s/a/c/"));
        keys("<Esc>");
        ensure("escape leaves it", vim->mode() == ALVimKeymap::Mode::Normal);
    }

    template<> template<>
    void alvimkeymap_object::test<15>()
    {
        set_test_name("it and at pair a tag with its own closing one through nesting, count levels out, and skip what is no tag");
        ALCodeEditor& e = make("<div><!-- <p> --><p>one <br/> <b>two</b> three</p></div>\n");
        keys("0fwdit");
        ensure_equals("it inside the innermost", flat(e.text()), std::string("<div><!-- <p> --><p>one <br/> <b></b> three</p></div>|"));
        keys("u0fwd2it");
        ensure_equals("2it a level out, the inner b of the same nesting not taken for the close", flat(e.text()), std::string("<div><!-- <p> --><p></p></div>|"));
        keys("u0fwd3at");
        ensure_equals("3at the outer tag whole, the comment's <p> no tag", flat(e.text()), std::string("|"));
        keys("u0fwd4at");
        ensure_equals("no fourth: nothing", flat(e.text()), std::string("<div><!-- <p> --><p>one <br/> <b>two</b> three</p></div>|"));
        e.setText("<a><b>x</b></a><a>y</a>\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("0fydat");
        ensure_equals("the second a is its own pair", flat(e.text()), std::string("<a><b>x</b></a>|"));
        keys("0fxvatd");
        ensure_equals("at in visual", flat(e.text()), std::string("<a></a>|"));
    }
    template<> template<>
    void alvimkeymap_object::test<16>()
    {
        set_test_name("patterns are vim's: magic by default, \\v very magic, \\V very nomagic, \\< \\> \\zs \\ze \\{-}; case as vim has it, with \\c and :set ic scs");
        ALCodeEditor& e = make("foo(bar) foo+bar Foo|bar foobar\n");
        keys(":s/foo(bar)/X/<CR>");
        ensure_equals("brackets are themselves in magic mode", flat(e.text()), std::string("X foo+bar Foo|bar foobar|"));
        keys(":s/foo+bar/Y/<CR>");
        ensure_equals("as is a plus", flat(e.text()), std::string("X Y Foo|bar foobar|"));
        keys(":s/Foo|bar/Z/<CR>");
        ensure_equals("and a bar", flat(e.text()), std::string("X Y Z foobar|"));
        keys(":s/\\v(foo)(bar)/\\2\\1/<CR>");
        ensure_equals("very magic: bare brackets group", flat(e.text()), std::string("X Y Z barfoo|"));
        keys(":s/\\<Y\\>/why/<CR>");
        ensure_equals("word bounds", flat(e.text()), std::string("X why Z barfoo|"));
        keys(":s/bar\\zsfoo/FOO/<CR>");
        ensure_equals("\\zs starts the match after what came before", flat(e.text()), std::string("X why Z barFOO|"));
        keys(":s/bar\\zeFOO/BAR/<CR>");
        ensure_equals("\\ze ends it before what follows", flat(e.text()), std::string("X why Z BARFOO|"));
        keys(":s/B.\\{-}O/-/<CR>");
        ensure_equals("\\{-} is lazy", flat(e.text()), std::string("X why Z -O|"));
        keys(":s/\\V-O$/[-O]/<CR>");
        ensure_equals("very nomagic: only ^ and $ special", flat(e.text()), std::string("X why Z [-O]|"));
        keys(":s/\\V[-O]/end/<CR>");
        ensure_equals("brackets themselves under \\V", flat(e.text()), std::string("X why Z end|"));
        keys(":s/x/lower/<CR>");
        ensure("case matters by default, as vim has it", vim->messageIsError());
        keys(":s/\\cx/lower/<CR>");
        ensure_equals("\\c ignores it", flat(e.text()), std::string("lower why Z end|"));
        keys(":set ic<CR>:s/z/zed/<CR>");
        ensure_equals(":set ignorecase ignores it", flat(e.text()), std::string("lower why zed end|"));
        keys(":set scs<CR>:s/W/w/<CR>");
        ensure("smartcase: a capital makes it matter again", vim->messageIsError());
        keys(":s/\\CWHY/w/<CR>");
        ensure("\\C too", vim->messageIsError());
        keys(":s/WHY/W/i<CR>");
        ensure_equals("the i flag overrides", flat(e.text()), std::string("lower W zed end|"));
        keys(":set noic<CR>/W<CR>");
        ensure("/ is case-sensitive again, and finds the capital", !vim->messageIsError() && caretText() == "0:6");
        keys(":g/\\v^(lower)/s/end/END/<CR>");
        ensure_equals(":g takes vim's spelling too", flat(e.text()), std::string("lower W zed END|"));
    }
    template<> template<>
    void alvimkeymap_object::test<17>()
    {
        set_test_name("~ in a pattern is the last replacement, \\%[] an optional sequence, and \\%V \\%# \\%l \\%c say where a match may start");
        ALCodeEditor& e = make("fun func function\nfun func function\nfun func function\n");
        keys(":s/fun\\%[ction]/X/g<CR>");
        ensure_equals("\\%[ction] takes as much of the sequence as is there", flat(e.text()), std::string("X X X|fun func function|fun func function|"));
        keys("u:s/func/Y/<CR>j:s/~tion/Z/<CR>");
        ensure_equals("~ is the last replacement, as text", flat(e.text()), std::string("fun Y function|fun func function|fun func function|"));
        ensure("nothing matched Ytion, so an error", vim->messageIsError());
        keys("gg0wve<Esc>:%s/\\%Vfun/V/g<CR>");
        ensure_equals("\\%V: only where the last visual area was", flat(e.text()), std::string("fun Y Vction|fun func function|fun func function|"));
        keys(":%s/\\%3lfun/L/g<CR>");
        ensure_equals("\\%3l: only on line 3", flat(e.text()), std::string("fun Y Vction|fun func function|L Lc Lction|"));
        keys(":%s/\\%>1lfun\\%<3l/M/g<CR>");
        ensure_equals("\\%>1l and \\%<3l: between", flat(e.text()), std::string("fun Y Vction|M Mc Mction|L Lc Lction|"));
        keys(":%s/\\%5cf/C/g<CR>");
        ensure("\\%5c: nothing at column 5", vim->messageIsError());
        keys(":%s/\\%3cn/C/g<CR>");
        ensure_equals("at column 3", flat(e.text()), std::string("fuC Y Vction|M Mc Mction|L Lc Lction|"));
        keys("gg0:%s/\\%#fu/H/g<CR>");
        ensure_equals("\\%#: only at the caret", flat(e.text()), std::string("HC Y Vction|M Mc Mction|L Lc Lction|"));
        keys(":set ic<CR>");
        ensure("the setting is shared with whoever shares the state", vim->shared().ignoreCase);
    }
    template<> template<>
    void alvimkeymap_object::test<18>()
    {
        set_test_name("a place before \\zs is about the whole match, \\%^ and \\%$ are the file's ends, and \\%d \\%x \\%u are characters");
        ALCodeEditor& e = make("foobar foobar\nfoobar\nfoobar foobar\n");
        keys("0wve<Esc>:%s/\\%Vfoo\\zsbar/X/g<CR>");
        ensure_equals("\\%V before \\zs: where the whole match starts, in the visual area", flat(e.text()), std::string("foobar fooX|foobar|foobar foobar|"));
        keys(":%s/foo\\zs\\(bar\\)/[\\1]/g<CR>");
        ensure_equals("the group after \\zs, the replacement over the whole", flat(e.text()), std::string("foo[bar] fooX|foo[bar]|foo[bar] foo[bar]|"));
        keys(":%s/\\%^foo/START/<CR>");
        ensure_equals("\\%^: the file's start alone", flat(e.text()), std::string("START[bar] fooX|foo[bar]|foo[bar] foo[bar]|"));
        keys(":%s/\\]\\%$/END/<CR>");
        ensure_equals("\\%$: the file's end alone", flat(e.text()), std::string("START[bar] fooX|foo[bar]|foo[bar] foo[barEND|"));
        keys(":%s/\\%d91bar\\%x5d/_/g<CR>");
        ensure_equals("\\%d91 and \\%x5d are the brackets", flat(e.text()), std::string("START_ fooX|foo_|foo_ foo[barEND|"));
        keys(":%s/\\%u0058/Y/<CR>");
        ensure_equals("\\u0058 is X", flat(e.text()), std::string("START_ fooY|foo_|foo_ foo[barEND|"));
        // A block-wise area is its columns on each of its lines.
        e.setText("ab ab\nab ab\nab ab\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("gg0<C-v>jl<Esc>:%s/\\%Vab/X/g<CR>");
        ensure_equals("\\%V on a block: the first columns of the first two lines", flat(e.text()), std::string("X ab|X ab|ab ab|"));
        keys(":%s/\\Va\\{1,2}b/Y/<CR>");
        ensure_equals("\\{n,m} under \\V is the multi still", flat(e.text()), std::string("X Y|X Y|Y ab|"));
        e.setText("fun func function aaab\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys(":s/\\vfun%[ction]/Z/g<CR>");
        ensure_equals("%[] under \\v", flat(e.text()), std::string("Z Z Z aaab|"));
        keys(":s/\\va{-2,}/Q/<CR>");
        ensure_equals("{-n,} under \\v is lazy: the fewest", flat(e.text()), std::string("Z Z Z Qab|"));
    }
    template<> template<>
    void alvimkeymap_object::test<19>()
    {
        set_test_name(":s with the c flag asks about each match -- y n a q l -- and a place before a \\zs inside brackets is about the whole");
        ALCodeEditor& e = make("a a a a\na a\n");
        keys(":%s/a/b/gc<CR>");
        ensure("asking", vim->mode() == ALVimKeymap::Mode::Confirm);
        ensure_equals("the question", vim->status(), std::string("replace with b (y/n/a/q/l)?"));
        ensure("the first match selected", e.selection().normalised() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)));
        keys("y");
        ensure_equals("y: this one", flat(e.text()), std::string("b a a a|a a|"));
        ensure("the next selected, where it now is", e.selection().normalised() == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 3)));
        keys("n");
        ensure_equals("n: not this one", flat(e.text()), std::string("b a a a|a a|"));
        keys("l");
        ensure_equals("l: this one and no more", flat(e.text()), std::string("b a b a|a a|"));
        ensure("done", vim->mode() == ALVimKeymap::Mode::Normal);
        keys(":%s/a/c/gc<CR>a");
        ensure_equals("a: all the rest", flat(e.text()), std::string("b c b c|c c|"));
        keys(":%s/c/d/gc<CR>yy<Esc>");
        ensure_equals("escape stops it after the two", flat(e.text()), std::string("b d b d|c c|"));
        ensure("normal again", vim->mode() == ALVimKeymap::Mode::Normal);
        keys("u");
        ensure_equals("the whole asking is one step to undo, as vim has it", flat(e.text()), std::string("b c b c|c c|"));
        // A \zs inside brackets with a place before it.
        e.setText("xab xab\nxab\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("gg0ve<Esc>:%s/\\%Vx\\(a\\zsb\\)/Y/g<CR>");
        ensure_equals("the whole starts in the area, the match after the \\zs: only the first", flat(e.text()), std::string("xaY xab|xab|"));
    }
    template<> template<>
    void alvimkeymap_object::test<20>()
    {
        set_test_name("the asking lights every match still to come and lets Control-E and Control-Y scroll; \\n and \\_s reach across lines");
        // Enough lines under the matches for the view to scroll.
        std::string text = "a a\na\na\n";
        for (int i = 0; i < 30; ++i)
        {
            text += "x\n";
        }
        ALCodeEditor& e = make(text.c_str());
        keys(":%s/a/b/gc<CR>");
        ensure_equals("all four lit", e.highlights().size(), size_t(4));
        keys("y");
        ensure_equals("three left lit", e.highlights().size(), size_t(3));
        ensure("the lit ones are the ones to come", e.highlights()[0] == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 3)));
        const S32 before = e.scrollY();
        keys("<C-E>");
        ensure("still asking", vim->mode() == ALVimKeymap::Mode::Confirm);
        ensure("scrolled a row", e.scrollY() == before + e.layout().rowHeight());
        keys("<C-Y>");
        ensure("and back", e.scrollY() == before);
        keys("q");
        ensure("nothing lit once done", e.highlights().empty());
        ensure_equals("what was said yes to", flat(e.text()).substr(0, 9), std::string("b a|a|a|x"));
        // Patterns over a line's end.
        e.setText("one two\nthree four\nfive\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("gg0/two\\nthree<CR>");
        ensure("found over the break", e.caret() == ALTextPos(0, 4));
        keys(":%s/two\\_sthree/joined/<CR>");
        ensure_equals("\\_s reaches the next line, and the two lines are one", flat(e.text()), std::string("one joined four|five|"));
        keys(":%s/four\\n//<CR>");
        ensure_equals("\\n taken out joins the lines", flat(e.text()), std::string("one joined five|"));
    }
}
