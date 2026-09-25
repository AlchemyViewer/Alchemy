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

#include "llsd.h"
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

        ALCodeEditor& make(const char* text, const char* syntax = "lsl", bool auto_complete = false)
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
        ensure("an insert is one step to undo", [&] { keys("u"); return alvimkeymap_data::flat(e.text()) =="-xyabc!|hihihiabove|new|"; }());
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
        ensure_equals("and \"\" names it", vim->registerText('"'), std::string("alpha beta"));
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
        drag(1, 0, 1, 4);
        ensure("a drag is visual", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("d");
        ensure_equals("and the operator takes what was dragged, up to where the drag stopped", flat(e.text()), std::string("one two| four|five six|"));
        ensure("back to normal", vim->mode() == ALVimKeymap::Mode::Normal);
        drag(2, 3, 2, 0);
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
        drag(0, 0, 0, 2);
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
        ensure("a control chord", !back[3].isChar && back[3].key == 'R' && (back[3].mask & ALVimKeymap::CONTROL));
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
        // An asking :s under :g asks once, over every line's matches in
        // order, after the :g has been through them.
        e.setText("a x\nb\na y\na z\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys(":g/^a/s/a/A/c<CR>");
        ensure("asking after the g", vim->mode() == ALVimKeymap::Mode::Confirm);
        ensure_equals("three to ask about", e.highlights().size(), size_t(3));
        ensure("the first line's first", e.selection().normalised() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)));
        keys("yny");
        ensure_equals("the first and the third said yes to", flat(e.text()), std::string("A x|b|a y|A z|"));
        ensure("done", vim->mode() == ALVimKeymap::Mode::Normal);
        keys("u");
        ensure_equals("one step to undo for the lot", flat(e.text()), std::string("a x|b|a y|a z|"));
        // A match over lines says so in the question; an error part way
        // through a :g drops what was gathered and says so.
        keys(":%s/x\\nb/Q/c<CR>");
        ensure_equals("the question says how many lines", vim->status(), std::string("replace with Q (over 2 lines) (y/n/a/q/l)?"));
        keys("q");
        // The lines are run from the last up: the last gathers its edit,
        // the one before errors.
        keys(":g/a/s/z/Z/c<CR>");
        ensure("not asking", vim->mode() == ALVimKeymap::Mode::Normal);
        ensure_equals("the error, and that nothing was done", vim->message(), std::string("E486: Pattern not found: z -- nothing substituted"));
        ensure_equals("nothing was", flat(e.text()), std::string("a x|b|a y|a z|"));
    }
    template<> template<>
    void alvimkeymap_object::test<21>()
    {
        set_test_name("the : line is edited in place -- Left Right Home End Delete, Control-B E W U H -- and q: goes through the host's window where it has one");
        ALCodeEditor& e = make("one\n");
        std::string   line;
        S32           caret = 0;
        keys(":abc");
        ensure("typing", vim->typingLine(line, caret) && line == ":abc" && caret == 4);
        keys("<Left><Left>");
        ensure("two back", vim->typingLine(line, caret) && caret == 2);
        keys("X");
        ensure("put in at the cursor", vim->typingLine(line, caret) && line == ":aXbc" && caret == 3);
        keys("<Home>");
        ensure("home", vim->typingLine(line, caret) && caret == 1);
        keys("<Delete>");
        ensure("the character at the cursor gone", vim->typingLine(line, caret) && line == ":Xbc" && caret == 1);
        keys("<End><BS>");
        ensure("the one before the end gone", vim->typingLine(line, caret) && line == ":Xb" && caret == 3);
        keys(" two words<C-W>");
        ensure("a word back", vim->typingLine(line, caret) && line == ":Xb two ");
        keys("<C-B>");
        ensure("to the start", vim->typingLine(line, caret) && caret == 1);
        keys("<C-E>q<C-U>");
        ensure("to the end, then the whole line gone", vim->typingLine(line, caret) && line == ":" && caret == 1);
        keys("<Esc>");
        // A message goes at a click, as it does at a key.
        keys(":nosuch<CR>");
        ensure("said", !vim->message().empty());
        e.handleMouseDown(10, 10, MASK_NONE);
        e.handleMouseUp(10, 10, MASK_NONE);
        ensure("cleared by the click", vim->message().empty());
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
        ensure("the history was offered", !offered.empty() && std::find(offered.begin(), offered.end(), "set number") != offered.end());
        ensure("the pick was run, as vim's window runs a row", vim->mode() == ALVimKeymap::Mode::Normal);
        // Picked to hold -- the window's Shift-Return -- it is put up to
        // edit instead.
        hold = true;
        keys("q:");
        ensure("on the line to edit", vim->typingLine(line, caret) && line == ":" + offered.front() && caret == static_cast<S32>(line.size()));
        keys("<Esc>");
        // Shift-Left and Shift-Right go a WORD at a time on the line.
        keys(":one two three");
        vim->handleKey(e, KEY_LEFT, MASK_SHIFT);
        ensure("a word back", vim->typingLine(line, caret) && caret == 9);
        vim->handleKey(e, KEY_LEFT, MASK_SHIFT);
        ensure("another", vim->typingLine(line, caret) && caret == 5);
        vim->handleKey(e, KEY_RIGHT, MASK_SHIFT);
        ensure("and on to the next word's start", vim->typingLine(line, caret) && caret == 9);
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<22>()
    {
        set_test_name("Tab on the : line completes the word at the cursor, walking the choices with the row up, from what the keymap and the host know");
        ALCodeEditor& e = make("one\n");
        std::string   line;
        S32           caret = 0;
        std::vector<std::string> items;
        S32                      chosen = -1;
        vim->hooks().complete = [](ALTextView&, const std::string& command, std::vector<std::string>& out) {
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
        ensure("substitute", vim->typingLine(line, caret) && line == ":substitute" && caret == 11);
        ensure("no row for one", !vim->menu(items, chosen));
        keys("<Esc>");
        // Several: the first on the line, the row up, Tab walking on and
        // back round to what was typed; Shift-Tab back.
        keys(":w<Tab>");
        ensure("the first in order", vim->typingLine(line, caret) && line == ":wall");
        ensure("the row up", vim->menu(items, chosen) && items.size() == 3 && chosen == 0 && items[0] == "wall" && items[1] == "wq" && items[2] == "write");
        keys("<Tab>");
        ensure("the next", vim->typingLine(line, caret) && line == ":wq" && vim->menu(items, chosen) && chosen == 1);
        keys("<Tab><Tab>");
        ensure("past the last, the word as typed", vim->typingLine(line, caret) && line == ":w" && vim->menu(items, chosen) && chosen == -1);
        vim->handleKey(e, KEY_TAB, MASK_SHIFT);
        ensure("Shift-Tab back to the last", vim->typingLine(line, caret) && line == ":write" && caret == 6);
        // Escape with the row up drops it and puts the word back; the
        // line stays.
        keys("<Esc>");
        ensure("the row gone, the word as typed", !vim->menu(items, chosen) && vim->typingLine(line, caret) && line == ":w");
        keys("<Esc>");
        ensure("then the line", vim->mode() == ALVimKeymap::Mode::Normal);
        // An argument: what the command takes, the host's and the
        // keymap's own; a range in front does not confuse the name.
        keys(":%set no<Tab>");
        ensure("the options starting no", vim->typingLine(line, caret) && line == ":%set noexpandtab" && vim->menu(items, chosen) && items.size() == 5);
        keys("<Tab><Tab>");
        ensure("nonumber among them", vim->typingLine(line, caret) && line == ":%set nonumber");
        // Typing on keeps what is on the line and lets the row go.
        keys("x");
        ensure("the row gone", !vim->menu(items, chosen) && vim->typingLine(line, caret) && line == ":%set nonumberx");
        keys("<Esc>");
        // Nothing to offer changes nothing.
        keys(":zzz<Tab>");
        ensure("as typed", vim->typingLine(line, caret) && line == ":zzz" && !vim->menu(items, chosen));
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<23>()
    {
        set_test_name("j and k keep the column wanted through a short line, $ wants every end, and gj gk walk the display's rows");
        ALCodeEditor& e = make("a long first line here\nab\nanother long line there\n");
        e.setCaret(ALTextPos(0, 10));
        keys("j");
        ensure("clamped to the short line", e.caret() == ALTextPos(1, 1));
        keys("j");
        ensure("the column wanted again below", e.caret() == ALTextPos(2, 10));
        keys("kk");
        ensure("and above", e.caret() == ALTextPos(0, 10));
        keys("l");
        keys("jj");
        ensure("a sideways move sets a new wanted column", e.caret() == ALTextPos(2, 11));
        keys("$");
        keys("kk");
        ensure("after $, every line's end", e.caret() == ALTextPos(0, 21));
        keys("j");
        ensure("the short one's too", e.caret() == ALTextPos(1, 1));
        // A count typed does not forget the column on its way.
        keys("gg05lj1j");
        ensure("through the short line with a count", e.caret() == ALTextPos(2, 5));
        // Wrapped: gj goes a row down within the line.
        e.setWordWrap(true);
        e.reshape(120, 200);
        e.setCaret(ALTextPos(0, 0));
        keys("gj");
        ensure("a row down, still on the first line", e.caret().line == 0 && e.caret().column > 0);
        keys("gk");
        ensure("and back", e.caret() == ALTextPos(0, 0));
    }

    template<> template<>
    void alvimkeymap_object::test<24>()
    {
        set_test_name("dge takes to the previous word's end, the numbered registers hold the deletes, and the case changes reach past ASCII");
        ALCodeEditor& e = make("one two three\ncaf\xc3\xa9\nline three\nline four\n");
        e.setCaret(ALTextPos(0, 9));
        keys("dge");
        ensure_equals("from the previous word's end through the caret", e.document().line(0), std::string("one twree"));
        keys("u");
        keys("jdd");
        keys("dd");
        ensure_equals("two lines gone", e.document().lineCount(), 3);
        ensure_equals("the last delete in 1", vim->registerText('1'), std::string("line three"));
        ensure_equals("the one before in 2", vim->registerText('2'), std::string("caf\xc3\xa9"));
        keys("\"2p");
        ensure_equals("put back from 2", e.document().line(2), std::string("caf\xc3\xa9"));
        keys("0x");
        ensure_equals("a small delete goes to -", vim->registerText('-'), std::string("c"));
        keys("\"-P");
        keys("0~~~~");
        ensure_equals("swapped past ASCII", e.document().line(2), std::string("CAF\xc3\x89"));
        keys("0gUU");
        keys("0guu");
        ensure_equals("lowered past ASCII", e.document().line(2), std::string("caf\xc3\xa9"));
    }

    template<> template<>
    void alvimkeymap_object::test<25>()
    {
        set_test_name("a repeated insert repeats what stood after a backspace, and marks move with the text");
        ALCodeEditor& e = make("\nsecond\nthird\n");
        keys("3ihelo<BS>lo<Esc>");
        ensure_equals("what stood, three times", e.document().line(0), std::string("hellohellohello"));
        keys("jma");
        keys("ggO<Esc>");
        keys("ggOnew<Esc>");
        keys("`a");
        ensure("the mark two lines further down", e.caret() == ALTextPos(3, 5));
        keys("kdd");
        keys("`a");
        ensure("and back up with a line above it gone", e.caret() == ALTextPos(2, 5));
    }

    template<> template<>
    void alvimkeymap_object::test<26>()
    {
        set_test_name(":sort orders the lines, :m moves them and :t copies them, by the addresses the : line knows");
        ALCodeEditor& e = make("pear\napple\nFig\nbanana\n");
        keys(":sort<CR>");
        ensure_equals("sorted, capitals first as bytes are", e.text(), std::string("Fig\napple\nbanana\npear\n"));
        keys(":sort! i<CR>");
        ensure_equals("the other way round, case aside", e.text(), std::string("pear\nFig\nbanana\napple\n"));
        keys(":%sort i<CR>");
        keys(":1m$<CR>");
        ensure_equals("the first moved to the end", e.text(), std::string("banana\nFig\npear\napple\n"));
        ensure("the caret on the moved line", e.caret().line == 3);
        keys(":3,4m0<CR>");
        ensure_equals("two moved to the top", e.text(), std::string("pear\napple\nbanana\nFig\n"));
        keys(":1t1<CR>");
        ensure_equals("the first copied below itself", e.text(), std::string("pear\npear\napple\nbanana\nFig\n"));
        keys(":2,3m1<CR>");
        ensure_equals("moving lines to just above themselves changes nothing", e.text(), std::string("pear\npear\napple\nbanana\nFig\n"));
        keys(":1,3m2<CR>");
        ensure("moving a range into itself is refused", !vim->message().empty());
        keys(":2,3> 1<CR>");
        ensure_equals("a count after > shifts so many from the range's end", e.document().line(2), std::string("\tapple"));
        keys(":1>><CR>");
        ensure_equals("doubled, two steps", e.document().line(0), std::string("\t\tpear"));
    }

    template<> template<>
    void alvimkeymap_object::test<27>()
    {
        set_test_name("with completion on, a command opens no list, and one Escape closes what typing put up and leaves insert");
        ALCodeEditor& e = make("counter = count;\n", "lsl", true);
        keys("ll");
        ensure("no list for a motion inside a word", !e.completionOpen());
        keys("x.");
        ensure("nor for an edit, or its repeat", !e.completionOpen());
        keys("A co");
        ensure("inserting", vim->inserting());
        ensure("the list opens as typing goes", e.completionOpen());
        keys("<Esc>");
        ensure("one Escape closes the list", !e.completionOpen());
        ensure("and leaves insert", !vim->inserting());

        keys("o");
        ALCodeEditor::Signature call;
        call.label = "llSay(integer channel, string msg)";
        e.showSignature(e.caret(), call);
        ensure("a signature while inserting", e.signature() != nullptr);
        keys("<Esc>");
        ensure("gone with the one Escape", e.signature() == nullptr && !vim->inserting());
        e.showSignature(e.caret(), call);
        ensure("an answer arriving after is not shown", e.signature() == nullptr);

        keys("ico");
        ensure("up again", e.completionOpen());
        keys("<C-c>");
        ensure("control-c leaves insert and the list", !vim->inserting() && !e.completionOpen());
    }

    template<> template<>
    void alvimkeymap_object::test<28>()
    {
        set_test_name(":s fills its groups from the match where it stands, where \\ze looks past the match's end");
        ALCodeEditor& e = make("foobar foobaz\n");
        keys(":s/\\(foo\\)\\zebar/[\\1]/<CR>");
        ensure_equals("the group, found where what follows is bar", flat(e.text()), std::string("[foo]bar foobaz|"));
        ensure("and nothing said against it", !vim->messageIsError());
    }

    template<> template<>
    void alvimkeymap_object::test<29>()
    {
        set_test_name("vim turned off in insert mode leaves no step open: what is typed after is a step of its own");
        ALCodeEditor& e = make("\n");
        keys("iab");
        ensure("inserting", vim->inserting());
        e.setModalKeymap(nullptr);
        vim = nullptr;
        for (const char c : { 'c', 'd' })
        {
            e.handleUnicodeCharHere(static_cast<llwchar>(c));
        }
        ensure_equals("typed", e.document().line(0), std::string("abcd"));
        e.undo();
        ensure_equals("the typing after undone alone", e.document().line(0), std::string("ab"));
    }

    template<> template<>
    void alvimkeymap_object::test<30>()
    {
        set_test_name("an operator's count times its motion's is held to the most a count is, rather than wrapping round");
        ALCodeEditor& e = make("a\nb\nc\nd\ne\n");
        keys("50000d50000d");
        ensure_equals("every line from the caret", flat(e.text()), std::string(""));
        ALCodeEditor& f = make("a\nb\nc\nd\ne\n");
        keys("50000d50000j");
        ensure_equals("and a motion likewise", flat(f.text()), std::string(""));
    }

    template<> template<>
    void alvimkeymap_object::test<31>()
    {
        set_test_name(":g inside :g is refused, as vim refuses it");
        ALCodeEditor& e = make("a1\nb2\na3\n");
        keys(":g/a/g/1/d<CR>");
        ensure("refused", vim->messageIsError() && vim->message().find("E147") != std::string::npos);
        ensure_equals("nothing done", flat(e.text()), std::string("a1|b2|a3|"));
    }

    template<> template<>
    void alvimkeymap_object::test<32>()
    {
        set_test_name("a named register never touches the clipboard; the unnamed one does where the setting says; \"+ always; :set clipboard");
        make("one\ntwo\n");
        const auto clipboard = []() {
            std::string text;
            LLClipboard::instance().pasteFromClipboard(text);
            return text;
        };
        const std::string outside("outside");
        LLClipboard::instance().copyToClipboard(outside, 0, static_cast<S32>(outside.size()));
        keys("\"ayy");
        ensure_equals("a named register leaves the clipboard alone", clipboard(), outside);
        ensure_equals("and \"\" names it", vim->registerText('"'), std::string("one"));

        vim->sharedState()->unnamedClipboard = false;
        keys("x");
        ensure_equals("off: a delete leaves the clipboard alone", clipboard(), outside);
        ensure_equals("and keeps what it took", vim->registerText('"'), std::string("o"));
        keys("p");
        ensure_equals("which is what p puts", flat(editor->text()), std::string("noe|two|"));
        keys("\"+yy");
        ensure_equals("\"+ is the clipboard whatever", clipboard(), std::string("noe"));

        keys(":set clipboard=unnamed<CR>");
        ensure("on again", vim->shared().unnamedClipboard);
        keys("jyy");
        ensure_equals("a yank goes by the clipboard", clipboard(), std::string("two"));
        keys(":set clipboard=<CR>");
        ensure("and off", !vim->shared().unnamedClipboard);
        keys(":set clipboard=bogus<CR>");
        ensure("a value vim has not is said", vim->messageIsError() && vim->message().find("E474") != std::string::npos);
    }

    template<> template<>
    void alvimkeymap_object::test<33>()
    {
        set_test_name("a click past a line's end is a click, however the mouse rests or slides on past it, and a drag from there takes the last character");
        ALCodeEditor& e = make("one two\nthree four\n");
        click(0, 7);
        ensure("past the end: normal, on the last character", vim->mode() == ALVimKeymap::Mode::Normal && caretText() == "0:6" && !e.hasSelection());
        click(0, 7, 40);
        ensure("slid on past the end: still a click", vim->mode() == ALVimKeymap::Mode::Normal && caretText() == "0:6" && !e.hasSelection());
        drag(0, 7, 0, 4);
        ensure("dragged back from past the end is visual", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("y");
        ensure_equals("and takes the last character with it", vim->registerText('"'), std::string("two"));
        drag(1, 2, 1, 10);
        ensure("dragged on past the end is visual", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("y");
        ensure_equals("to the last character", vim->registerText('"'), std::string("ree four"));
    }

    template<> template<>
    void alvimkeymap_object::test<34>()
    {
        set_test_name("a drag reaches from the character pressed to the character under the pointer, both taken, whichever half of either the pointer is on");
        make("hello world\n");
        drag(0, 4, 0, 1);
        keys("y");
        ensure_equals("back from the left of the o: the o taken", vim->registerText('"'), std::string("ello"));
        drag(0, 1, 0, 4, true);
        keys("y");
        ensure_equals("on from the right of the e: the e taken", vim->registerText('"'), std::string("ello"));
        drag(0, 4, 0, 1, true);
        keys("y");
        ensure_equals("back from the right of the o, to the right of the e", vim->registerText('"'), std::string("ello"));
        click(0, 4);
        ensure("a click is on its character", vim->mode() == ALVimKeymap::Mode::Normal && caretText() == "0:4");
        S32 x, y, next_x, next_y;
        pointOf(0, 4, x, y);
        pointOf(0, 5, next_x, next_y);
        editor->handleMouseDown(next_x - 3, y, MASK_NONE);
        editor->handleHover(next_x - 3, y, MASK_NONE);
        editor->handleMouseUp(next_x - 3, y, MASK_NONE);
        ensure("even on its right half", vim->mode() == ALVimKeymap::Mode::Normal && caretText() == "0:4" && !editor->hasSelection());
    }

    template<> template<>
    void alvimkeymap_object::test<35>()
    {
        set_test_name("insert mode's Control keys are vim's, not the editor's -- none of them moves the caret off the text being typed");
        ALCodeEditor& e = make("abc\ndef\n");
        // Control-N and Control-P ask for completions, and the caret stays
        // where it was: on a Mac they were the text system's down and up.
        keys("A<C-n>x<C-p>y<Esc>");
        ensure_equals("typed where the caret was", flat(e.text()), std::string("abcxy|def|"));
        // Control-A the last insert again, where it was the line's start
        // on a Mac and select-all elsewhere.
        keys("jA<C-a><Esc>");
        ensure_equals("the last insert again", flat(e.text()), std::string("abcxy|defxy|"));
        // Control-H a backspace, where it was Replace elsewhere.
        keys("A<C-h>z<Esc>");
        ensure_equals("backspace", flat(e.text()), std::string("abcxy|defxz|"));
        // Control-Y and Control-E: the character above and below.
        make("abc\n\nxyz\n");
        keys("ji<C-y><C-y><C-e><Esc>");
        ensure_equals("above, above, below", flat(editor->text()), std::string("abc|abz|xyz|"));
        // Control-T and Control-D: a step in and back, the caret on its
        // character.
        ALCodeEditor& t = make("x = 1;\n");
        t.setSoftTabs(true);
        keys("A<C-t>2<Esc>");
        ensure_equals("a step in, typing on where it was", flat(t.text()), std::string("    x = 1;2|"));
        keys("A<C-d>3<Esc>");
        ensure_equals("and back", flat(t.text()), std::string("x = 1;23|"));
        keys("I<C-d>4<Esc>");
        ensure_equals("nothing to take back", flat(t.text()), std::string("4x = 1;23|"));
        // Control-R and a register: its text typed in, and `.` does it again.
        ALCodeEditor& r = make("foo bar\n");
        keys("\"ayiwo<C-r>a!<Esc>");
        ensure_equals("the register put in", flat(r.text()), std::string("foo bar|foo!|"));
        keys(".");
        ensure_equals("and again", flat(r.text()), std::string("foo bar|foo!|foo!|"));
        // Control-J and Control-M are Return.
        keys("A<C-j>q<Esc>");
        ensure_equals("a line broken", flat(r.text()), std::string("foo bar|foo!|foo!|q|"));
    }

    template<> template<>
    void alvimkeymap_object::test<36>()
    {
        set_test_name("a count makes its text once, and not past a megabyte");
        ALCodeEditor& e = make("abcdefghijklmnopqrstuvwxyz\n");
        // Twenty-six bytes a hundred thousand times over is past it: said,
        // and nothing put.
        keys("yiw100000p");
        ensure("said: " + vim->message(), vim->messageIsError() && vim->message().find("Too large a count") != std::string::npos);
        ensure_equals("nothing put", flat(e.text()), std::string("abcdefghijklmnopqrstuvwxyz|"));
        keys("2p");
        ensure_equals("a count within it is put", flat(e.text()),
                      std::string("aabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzbcdefghijklmnopqrstuvwxyz|"));
        // An insert's count likewise: what was typed stands, once.
        make("\n");
        keys("100000iabcdefghijk<Esc>");
        ensure("said: " + vim->message(), vim->messageIsError() && vim->message().find("Too large a count") != std::string::npos);
        ensure_equals("typed once", flat(editor->text()), std::string("abcdefghijk|"));
        keys("u");
        ensure_equals("and undone as one", flat(editor->text()), std::string("|"));
        // Within it, the lot in one edit, undone at once.
        keys("20000ixy<Esc>");
        ensure_equals("forty thousand characters", editor->text().size(), size_t(40001));
        keys("u");
        ensure_equals("undone at once", flat(editor->text()), std::string("|"));
    }

    template<> template<>
    void alvimkeymap_object::test<37>()
    {
        set_test_name("an input method composing while vim is not inserting puts nothing in and takes no selection out; in insert mode it composes");
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
        ensure_equals("V j over a composition: nothing gone", flat(e.text()), flat(was));
        composed('j');
        ensure_equals("nor on the next", flat(e.text()), flat(was));
        ensure("still in visual line mode", vim->mode() == ALVimKeymap::Mode::VisualLine);
        ensure_equals("over lines 2 to 4", caretText().substr(0, 2), std::string("3:"));
        keys("<Esc>");
        composed('v');
        composed('k');
        ensure_equals("nor under v", flat(e.text()), flat(was));
        keys("<Esc>");
        // Inserting, it is typing.
        keys("ggO");
        composed('x');
        keys("<Esc>");
        ensure_equals("composed in insert mode", flat(e.text()), std::string("x|") + flat(was));
    }

    template<> template<>
    void alvimkeymap_object::test<38>()
    {
        set_test_name("r<CR> breaks the line -- one break for a count, indented as a Return would -- and Enter and Tab are what a waiting command takes");
        make("    one two");
        keys("0ft");
        ensure_equals("on the word", caretText(), std::string("0:8"));
        keys("h");
        keys("r<CR>");
        ensure_equals("the space a line break, the new line indented", flat(editor->text()), std::string("    one|    two"));
        ensure_equals("the caret on its first word", caretText(), std::string("1:4"));

        make("abcdef");
        keys("l3r<CR>");
        ensure_equals("three characters, one break", flat(editor->text()), std::string("a|ef"));

        make("a b");
        keys("lr<Tab>");
        ensure_equals("a tab for the space", editor->text(), std::string("a\tb"));
        keys("0f<Tab>");
        ensure_equals("found by f", caretText(), std::string("0:1"));
    }
}
