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

#include "../test/lltut.h"

#include <string>

class LLAvatarName;
const std::string gVimTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gVimTestAnonName;
}

namespace ll_test
{
    // How long a search for a misspelling may check lines for.
    struct TextViewProbe
    {
        static void misspellingBudget(ALTextView& view, F32 seconds) { view.mMisspellingBudget = seconds; }
    };
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

    // More than TUT's fifty a group holds by default, which runs the first
    // fifty and says nothing of the rest: keep this above the highest test.
    typedef test_group<alvimkeymap_data, 210> alvimkeymap_group;
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
        ensure_equals("v $ y yanks to the line's end, and its break", vim->registerText('"'), std::string("hree four\n"));
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
        keys(":s/\\V-O\\$/[-O]/<CR>");
        ensure_equals("very nomagic: \\$ the line's end", flat(e.text()), std::string("X why Z [-O]|"));
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
        // A place that passes over the first match: the replacement is
        // still the kept match's own, groups and all.
        e.setText("foobar foobaz foobaq\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys("0wve<Esc>:s/\\%Vfoo\\zs\\(ba.\\)/[&\\1]/g<CR>");
        ensure_equals("\\%V passing over a \\zs match: & and \\1 are the kept one's", flat(e.text()), std::string("foobar foo[bazbaz] foobaq|"));
        // After a top-level \zs, & is what the match is, and the groups
        // are counted as they were written, one before the \zs too.
        e.setText("foobar\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        keys(":s/foo\\zsbar/[&]/<CR>");
        ensure_equals("& after \\zs is the match alone", flat(e.text()), std::string("foo[bar]|"));
        e.setText("foobar\n");
        keys(":s/\\(f\\)oo\\zs\\(bar\\)/[\\2\\1&]/<CR>");
        ensure_equals("a group before \\zs is still \\1", flat(e.text()), std::string("foo[barfbar]|"));
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
        // The lines are run from the top down: the first gathers its
        // edit, the next errors.
        keys(":g/a/s/x/X/c<CR>");
        ensure("not asking", vim->mode() == ALVimKeymap::Mode::Normal);
        ensure_equals("the error, and that nothing was done", vim->message(), std::string("E486: Pattern not found: x -- nothing substituted"));
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
        ensure("the options starting no",
               vim->typingLine(line, caret) && line == ":%set noexpandtab" && vim->menu(items, chosen) && items.size() == 8);
        keys("<Tab><Tab><Tab><Tab>");
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

        ensure("off, as vim has it", !vim->shared().unnamedClipboard);
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
        keys(":set clipboard=unnamed<CR>");
        keys(":set clipboard&<CR>");
        ensure("off again by default", !vim->shared().unnamedClipboard);
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

    template<> template<>
    void alvimkeymap_object::test<39>()
    {
        set_test_name("`.` and a macro type an insert's Return, Tab and Backspace again");
        make("x");
        keys("ofoo<CR>bar<Esc>");
        ensure_equals("inserted", flat(editor->text()), std::string("x|foo|bar"));
        keys(".");
        ensure_equals("both lines again", flat(editor->text()), std::string("x|foo|bar|foo|bar"));

        make("aaaa");
        keys("s1<CR>2<Esc>");
        ensure_equals("substituted", flat(editor->text()), std::string("1|2aaa"));
        keys("l.");
        ensure_equals("and again, the break with it", flat(editor->text()), std::string("1|21|2aa"));

        make("pq<BS>");
        keys("Ayz<BS><Esc>");
        ensure_equals("a backspace", editor->text(), std::string("pq<BS>y"));
        keys(".");
        ensure_equals("typed again", editor->text(), std::string("pq<BS>yy"));

        make("p\nq");
        keys("qaA!<CR>-<Esc>q");
        ensure_equals("recorded as typed", flat(editor->text()), std::string("p!|-|q"));
        keys("G@a");
        ensure_equals("played with its break", flat(editor->text()), std::string("p!|-|q!|-"));
    }

    template<> template<>
    void alvimkeymap_object::test<40>()
    {
        set_test_name("gt, gT and Ctrl-^ ask the host for its tabs as :tabnext, :tabprevious and :buffer, with the count");
        make("a\nb");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("gt3gtgT2gT<C-6>4<C-6>");
        ensure("each as its command",
               heard == std::vector<std::string>{ "tabnext|", "tabnext|3", "tabprevious|", "tabprevious|2", "buffer|#", "buffer|4" });
        heard.clear();
        keys("dgt");
        ensure("not after an operator", heard.empty());
        ensure_equals("nothing changed", flat(editor->text()), std::string("a|b"));
    }

    template<> template<>
    void alvimkeymap_object::test<41>()
    {
        set_test_name("Tab on the : line tells the host the word typed, which a file's folder is read by");
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
        ensure("the command and the word", asked == std::vector<std::string>{ "e|sc" });
        ensure_equals("completed from what it gave", vim->commandLine(), std::string("e scripts/"));
        keys("<Esc>:e scripts/fo<Tab>");
        ensure_equals("a folder's word whole", asked.back(), std::string("e|scripts/fo"));
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<42>()
    {
        set_test_name("vim's jumps set '' and tell the host where they began: G, gg, %, a search, a mark, a line on the : line; not j, w or an operator's");
        make("one (x)\ntwo\nthree\nfour");
        std::vector<ALTextPos> from;
        vim->hooks().jumped = [&from](ALTextView&, const ALTextPos& at) { from.push_back(at); };
        keys("G");
        ensure("G from where it was", from == std::vector<ALTextPos>{ ALTextPos(0, 0) });
        keys("''");
        ensure("'' back to it", editor->caret().line == 0 && from.back() == ALTextPos(3, 0));
        keys("jl");
        ensure("j and l are no jumps", from.size() == 2);
        keys("gg");
        ensure("gg", from.size() == 3 && from.back() == ALTextPos(1, 1));
        keys("f(%");
        ensure("%", from.size() == 4 && from.back() == ALTextPos(0, 4) && editor->caret() == ALTextPos(0, 6));
        keys("/thr<CR>");
        ensure("a search", from.size() == 5 && editor->caret().line == 2);
        keys("mak:4<CR>");
        ensure("a line on the : line", from.size() == 6 && editor->caret().line == 3);
        keys("'a");
        ensure("a mark", from.size() == 7 && editor->caret().line == 2);
        keys("dG");
        ensure("not an operator's", from.size() == 7);
    }

    template<> template<>
    void alvimkeymap_object::test<43>()
    {
        set_test_name("Ctrl-O and Ctrl-I go back and forward, Ctrl-] and gd to the definition, Ctrl-T back from it, Ctrl-G and K: the host's");
        make("x y");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("<C-o>2<C-o><C-i>");
        editor->handleKeyHere(']', ALVimKeymap::CONTROL);
        keys("gd<C-t><C-g>K");
        ensure("each as the host's",
               heard == std::vector<std::string>{ "back|", "back|", "back|", "forward|", "tag|", "tag|", "pop|", "file|", "reference|" });
    }

    template<> template<>
    void alvimkeymap_object::test<44>()
    {
        set_test_name(":registers and :marks list what is held and set, as vim lists them, to the host's listing");
        make("one\n\ttwo three");
        std::vector<std::string> listed;
        vim->hooks().listing = [&listed](ALTextView&, const std::string& text) { listed.push_back(text); };
        keys("yyj\"ayw");
        keys(":registers<CR>");
        ensure_equals("one listing", listed.size(), size_t(1));
        ensure("the heading", listed[0].compare(0, 17, "Type Name Content") == 0);
        ensure("a yank of lines", listed[0].find("\n  l  \"0   one^J") != std::string::npos);
        ensure("a named one, of characters, a tab as ^I", listed[0].find("\n  c  \"a   ^I") != std::string::npos);
        ensure("the last : line", listed[0].find("\n  c  \":   registers") != std::string::npos);
        keys(":di a<CR>");
        ensure("only those named", listed[1].find("\"0") == std::string::npos && listed[1].find("\"a") != std::string::npos);

        keys("mb:marks<CR>");
        ensure_equals("marks", listed[2], std::string("mark line  col file/text\n b      2    0 two three"));
        keys("G");
        keys(":marks<CR>");
        ensure("a jump's", listed[3].find("\n '      2    0 two three") != std::string::npos);

        vim->hooks().listing = nullptr;
        keys(":reg<CR>");
        ensure_equals("without a host, the heading said", vim->message(), std::string("Type Name Content"));
    }

    template<> template<>
    void alvimkeymap_object::test<45>()
    {
        set_test_name("a host's command named with underscores is one name on the : line, typed or completed");
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
        ensure("whole, with a bang", heard == std::vector<std::string>{ "go_to_line|", "save_all!|" });
        keys(":go_<Tab>");
        ensure_equals("completed from the underscore on", vim->commandLine(), std::string("go_to_line"));
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<46>()
    {
        set_test_name("gf runs :find on the name under the caret: a quoted one it is in or before on the line, else the file name around it");
        make("#include \"lib/util.lsl\"\nlocal m = require('mod') -- x\nsee docs/read_me.txt now\n   \n");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("gf");
        ensure_equals("the include's, from before it", heard.back(), std::string("find|lib/util.lsl"));
        keys("fugf");
        ensure_equals("from inside it", heard.back(), std::string("find|lib/util.lsl"));
        keys("jgf");
        ensure_equals("a require's", heard.back(), std::string("find|mod"));
        keys("j0fdgf");
        ensure_equals("a path in the text", heard.back(), std::string("find|docs/read_me.txt"));
        const size_t asked = heard.size();
        keys("jgf");
        ensure("nothing there", heard.size() == asked);
        ensure_equals("said", vim->message(), std::string("E446: No file name under cursor"));
    }

    template<> template<>
    void alvimkeymap_object::test<47>()
    {
        set_test_name("]d and [d step through the problems as :cnext and :cprevious; grn, grr and gra are the editor's; gO the host's symbols");
        ALCodeEditor&            e = make("integer count = 1;\ncount = 2;\n");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("]d3]d[d2[dgO");
        ensure("each as the host's",
               heard == std::vector<std::string>{ "cnext|", "cnext|3", "cprevious|", "cprevious|2", "go_to_symbol|" });
        std::vector<ALEditorCommand> asked;
        e.setSymbolRequest([&asked](ALEditorCommand command, const ALTextRange&) { asked.push_back(command); });
        keys("grngrr");
        ensure("rename and the references",
               asked == std::vector<ALEditorCommand>{ ALEditorCommand::Rename, ALEditorCommand::FindReferences });
        e.setFixProvider([](S32, std::vector<ALCodeEditor::Fix>& out) {
            ALCodeEditor::Fix fix;
            fix.title = "Put it right";
            out.push_back(fix);
        });
        keys("gra");
        ensure("the fixes at the caret", e.fixesOpen());
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<48>()
    {
        set_test_name("gc comments lines as Toggle Comment does: gcc a line, 2gcc two, gc with a motion, gc over a visual selection, and . again");
        ALCodeEditor& e = make("one;\ntwo;\nthree;\nfour;\n");
        keys("gcc");
        ensure("the line", e.document().line(0).compare(0, 2, "//") == 0 && e.document().line(1) == "two;");
        keys("gcc");
        ensure_equals("and back", e.document().line(0), std::string("one;"));
        keys("jgcj");
        ensure("with a motion, its lines", e.document().line(1).compare(0, 2, "//") == 0 && e.document().line(2).compare(0, 2, "//") == 0 &&
                                               e.document().line(3) == "four;");
        keys(".");
        ensure_equals(". again", flat(e.text()), std::string("one;|two;|three;|four;|"));
        keys("gg2gcc");
        ensure("a count", e.document().line(1).compare(0, 2, "//") == 0 && e.document().line(2) == "three;");
        keys("u");
        ensure_equals("one step to undo", flat(e.text()), std::string("one;|two;|three;|four;|"));
        keys("ggVjjgc");
        ensure("a visual selection's lines",
               e.document().line(0).compare(0, 2, "//") == 0 && e.document().line(2).compare(0, 2, "//") == 0 && e.document().line(3) == "four;");
        ensure("visual left", vim->mode() == ALVimKeymap::Mode::Normal);
    }

    template<> template<>
    void alvimkeymap_object::test<49>()
    {
        set_test_name("zc zo za zM zR fold the code editor's blocks; zj and zk move between them; [z and ]z to the ends of the one around");
        ALCodeEditor& e = make("default\n{\n    state_entry()\n    {\n        x();\n    }\n"
                               "    touch_start(integer n)\n    {\n        y();\n    }\n}\n");
        keys("3G");
        keys("zc");
        ensure("closed", e.isFolded(2));
        keys("zo");
        ensure("open", !e.isFolded(2));
        keys("za");
        ensure("za closes", e.isFolded(2));
        keys("za");
        ensure("and opens", !e.isFolded(2));
        keys("zM");
        ensure("every block", e.isFolded(0));
        keys("zR");
        ensure("none", !e.isFolded(0) && !e.isFolded(2) && !e.isFolded(6));
        keys("gg");
        keys("zj");
        ensure_equals("zj to the next block's start", e.caret().line, 2);
        keys("zj");
        ensure_equals("and the next", e.caret().line, 6);
        keys("zk");
        ensure_equals("zk to the end of the one above", e.caret().line, 5);
        keys("5G]z");
        ensure_equals("]z the end of the block around", e.caret().line, 5);
        keys("[z");
        ensure_equals("[z its start", e.caret().line, 2);
    }

    template<> template<>
    void alvimkeymap_object::test<50>()
    {
        set_test_name("]s and [s go to the misspelled words round past the ends; z= puts one right by a count or a pick; zg and no spell check say why");
        ALCodeEditor& e = make("teh cat\nfine\nsecond teh\n", "text");
        keys("]s");
        ensure_equals("no spell check: said", vim->message(), std::string("E756: Spell checking is not enabled"));
        e.setSpellChecker([](const std::string& word) { return word != "teh"; },
                          [](const std::string&, std::vector<std::string>& out) {
                              out.push_back("the");
                              out.push_back("ten");
                          });
        e.setSpellCheck(true);
        keys("]s");
        ensure("the next", e.caret() == ALTextPos(2, 7));
        keys("]s");
        ensure("round past the end", e.caret() == ALTextPos(0, 0));
        keys("[s");
        ensure("and back past the start", e.caret() == ALTextPos(2, 7));
        keys("2z=");
        ensure_equals("by a count", e.document().line(2), std::string("second ten"));
        std::vector<std::string>            offered;
        std::function<void(size_t)>         chose;
        vim->hooks().pick = [&](ALTextView&, const std::string&, const std::vector<std::string>& items,
                                std::function<void(size_t)> chosen) {
            offered = items;
            chose   = std::move(chosen);
        };
        keys("gg");
        keys("z=");
        ensure("offered", offered == std::vector<std::string>{ "the", "ten" });
        chose(0);
        ensure_equals("picked", e.document().line(0), std::string("the cat"));
        keys("z=");
        ensure_equals("a word spelled right has none", vim->message(), std::string("Sorry, no suggestions"));
        keys("jzg");
        ensure_equals("only the viewer's dictionary takes a word", vim->message(), std::string("E764: Cannot add this word"));

        // The one misspelling on the caret's own line, behind it: round the
        // whole text back to it.
        e.setText("x teh y teh");
        e.setCaret(ALTextPos(0, 6));
        keys("]s");
        ensure("the next after the caret", e.caret() == ALTextPos(0, 8));
        keys("]s");
        ensure("round to its own line's first", e.caret() == ALTextPos(0, 2));
        keys("[s");
        ensure("and back round to its last", e.caret() == ALTextPos(0, 8));
    }

    template<> template<>
    void alvimkeymap_object::test<51>()
    {
        set_test_name(":set nohlsearch leaves a search's matches unlit, and clears them; :set hlsearch lights them again");
        ALCodeEditor& e = make("x a x b x\n");
        keys("/x<CR>");
        ensure_equals("lit", e.highlights().size(), size_t(3));
        keys(":set nohls<CR>");
        ensure("cleared", e.highlights().empty());
        keys("/x<CR>");
        ensure("left unlit", e.highlights().empty());
        keys(":set hlsearch<CR>n");
        ensure_equals("lit again", e.highlights().size(), size_t(3));
    }

    template<> template<>
    void alvimkeymap_object::test<52>()
    {
        set_test_name("gn selects the last search's match here or next, and an operator takes it: cgn then . for the next; gN back");
        ALCodeEditor& e = make("foo bar foo baz foo\n");
        keys("/foo<CR>gg");
        keys("cgnqux<Esc>");
        ensure_equals("the first changed", e.document().line(0), std::string("qux bar foo baz foo"));
        keys(".");
        ensure_equals(". the next", e.document().line(0), std::string("qux bar qux baz foo"));
        keys("dgn");
        ensure_equals("dgn the last", e.document().line(0), std::string("qux bar qux baz "));
        keys("u0gn");
        ensure("gn: selected",
               vim->mode() == ALVimKeymap::Mode::Visual && e.selection() == ALTextRange(ALTextPos(0, 16), ALTextPos(0, 19)));
        keys("<Esc>$gN");
        ensure("gN: the one it is in, or before", e.selection() == ALTextRange(ALTextPos(0, 16), ALTextPos(0, 19)));
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<53>()
    {
        set_test_name("gi inserts again where inserting last stopped, as the ^ mark keeps it through edits above");
        ALCodeEditor& e = make("abc\ndef\n");
        keys("A!<Esc>ggyyPG");
        keys("gi?<Esc>");
        ensure_equals("where it stopped, moved down a line", flat(e.text()), std::string("abc!|abc!?|def|"));
    }

    template<> template<>
    void alvimkeymap_object::test<54>()
    {
        set_test_name("g* and g# as * and # but not whole words; g_ the last non-blank; gp and gP leave the caret after what they put");
        ALCodeEditor& e = make("foo foobar foo\n  abc  \nx\n");
        keys("*");
        ensure("* whole words only", e.caret() == ALTextPos(0, 11));
        keys("gg0g*");
        ensure("g* in foobar too", e.caret() == ALTextPos(0, 4));
        keys("jg_");
        ensure("g_ the last non-blank", e.caret() == ALTextPos(1, 4));
        keys("0dg_");
        ensure_equals("and an operator's", e.document().line(1), std::string("  "));
        keys("u");
        keys("3Gyykgp");
        ensure("gp: a line put, the caret on the line after it",
               e.document().line(2) == "x" && e.document().line(3) == "x" && e.caret().line == 3);
        keys("ggywgP");
        ensure("gP: characters put, the caret after them", e.document().line(0) == "foo foo foobar foo" && e.caret() == ALTextPos(0, 4));
    }

    template<> template<>
    void alvimkeymap_object::test<55>()
    {
        set_test_name("[( [{ ]) ]} go to the bracket the caret is inside of, a count out, across lines; and are an operator's, exclusive");
        ALCodeEditor& e = make("f(a, g(b), c)\n{\n  x { y }\n  z\n}\n");
        keys("fb");
        keys("[(");
        ensure("the g's", e.caret() == ALTextPos(0, 6));
        keys("fb2[(");
        ensure("two out", e.caret() == ALTextPos(0, 1));
        keys("fb])");
        ensure("the close after it", e.caret() == ALTextPos(0, 8));
        keys("0fad])");
        ensure_equals("up to the close, not it", e.document().line(0), std::string("f()"));
        keys("3G0fy[{");
        ensure("an inner brace", e.caret() == ALTextPos(2, 4));
        keys("j]}");
        ensure("an outer one, across lines", e.caret() == ALTextPos(4, 0));
    }

    template<> template<>
    void alvimkeymap_object::test<56>()
    {
        set_test_name("insert mode's Ctrl-O runs one command of normal mode and inserts again; Ctrl-V puts in the next key as it is");
        ALCodeEditor& e = make("abc\n");
        keys("A<C-o>");
        ensure("one command waits", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().find("-- (insert) --") != std::string::npos);
        keys("0");
        ensure("then inserting", vim->mode() == ALVimKeymap::Mode::Insert);
        keys("X<Esc>");
        ensure_equals("where the command left it", e.document().line(0), std::string("Xabc"));
        keys("A<C-o>:s/a/Y/<CR>");
        ensure("after a : line too", vim->mode() == ALVimKeymap::Mode::Insert && e.document().line(0) == "XYbc");
        keys("<Esc>");

        make("\n");
        keys("i<C-v><Tab>x<C-v>u00e9<C-v>(<Esc>");
        ensure_equals("a tab, an e by its code, a bracket alone", editor->document().line(0), std::string("\tx\xC3\xA9("));
    }

    template<> template<>
    void alvimkeymap_object::test<57>()
    {
        set_test_name("the search line lights what is typed so far, as incsearch has it; Escape lets it go; :set noincsearch leaves it alone");
        ALCodeEditor& e = make("abc\nxyz\nabd\n");
        keys("/ab");
        ensure_equals("each match lit as typed", e.highlights().size(), size_t(2));
        ensure("the caret where it was", e.caret() == ALTextPos(0, 0));
        keys("c");
        ensure_equals("fewer as it narrows", e.highlights().size(), size_t(1));
        keys("<Esc>");
        ensure("let go of", e.highlights().empty() && e.caret() == ALTextPos(0, 0));
        keys(":set nois<CR>/ab");
        ensure("not lit where it is off", e.highlights().empty());
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<58>()
    {
        set_test_name("a search's offset: /x/e and e-1 by its end, s+1 and b by its start, +1 lines on; n keeps it, going on from the match");
        ALCodeEditor& e = make("one foo two\nthree foo four\nfive foo\n");
        keys("/foo/e<CR>");
        ensure("its last character", e.caret() == ALTextPos(0, 6));
        keys("n");
        ensure("n keeps it, the next match's", e.caret() == ALTextPos(1, 8));
        keys("N");
        ensure("N back to the one before", e.caret() == ALTextPos(0, 6));
        keys("gg/foo/s+1<CR>");
        ensure("one on from its start", e.caret() == ALTextPos(0, 5));
        keys("gg/foo/e-1<CR>");
        ensure("one back from its end", e.caret() == ALTextPos(0, 5));
        keys("gg/foo/+1<CR>");
        ensure("the line after it", e.caret() == ALTextPos(1, 0));
        keys("n");
        ensure("n: the line after the next", e.caret() == ALTextPos(2, 0));
        keys("gg//b<CR>");
        ensure("the last pattern with another offset", e.caret() == ALTextPos(0, 4));
        keys("gg/foo/s-2<CR>");
        ensure("two back from its start", e.caret() == ALTextPos(0, 2));
        keys("n");
        ensure("n from the match, not from before it", e.caret() == ALTextPos(1, 4));
    }

    template<> template<>
    void alvimkeymap_object::test<59>()
    {
        set_test_name(":join, :retab, :undo and :redo, :put, :mark and :k, :left, :right and :center");
        ALCodeEditor& e = make("a\n  b\n  c\nd\n");
        keys(":j<CR>");
        ensure_equals(":j the line and the next", flat(e.text()), std::string("a b|  c|d|"));
        keys(":u<CR>");
        ensure_equals(":u", flat(e.text()), std::string("a|  b|  c|d|"));
        keys(":red<CR>");
        ensure_equals(":red", flat(e.text()), std::string("a b|  c|d|"));
        keys(":u<CR>:1,3j!<CR>");
        ensure_equals(":j! a range, the blanks kept", flat(e.text()), std::string("a  b  c|d|"));
        keys("u:2j 3<CR>");
        ensure_equals(":j with a count, from the range's last", flat(e.text()), std::string("a|  b c d|"));
        keys("u");

        e.setSoftTabs(false);
        e.setTabWidth(2);
        keys(":retab<CR>");
        ensure_equals(":retab: tabs where the tabs are set so", flat(e.text()), std::string("a|\tb|\tc|d|"));
        keys(":retab 4<CR>");
        ensure_equals("a width given sets it first", flat(e.text()), std::string("a|  b|  c|d|"));
        keys("u");

        keys("gg\"xyy:3pu x<CR>");
        ensure_equals(":put under the line", flat(e.text()), std::string("a|\tb|\tc|a|d|"));
        keys(":1pu! x<CR>");
        ensure_equals(":put! above it", flat(e.text()), std::string("a|a|\tb|\tc|a|d|"));

        keys(":4ma q<CR>gg'q");
        ensure_equals(":mark", e.caret().line, 3);
        keys(":2k w<CR>gg'w");
        ensure_equals(":k", e.caret().line, 1);

        make("x\n   yy\n");
        keys(":%le 2<CR>");
        ensure_equals(":left", flat(editor->text()), std::string("  x|  yy|"));
        keys(":2ri 6<CR>");
        ensure_equals(":right", editor->document().line(1), std::string("    yy"));
        keys(":2ce 6<CR>");
        ensure_equals(":center", editor->document().line(1), std::string("  yy"));
    }

    template<> template<>
    void alvimkeymap_object::test<60>()
    {
        set_test_name("g; and g, go back and on through the change list; '. is the last change; :changes lists them");
        ALCodeEditor& e = make("a\nb\nc\nd\n");
        keys("g;");
        ensure_equals("none yet", vim->message(), std::string("E664: Changelist is empty"));
        keys("jA!<Esc>3GA?<Esc>gg");
        keys("g;");
        ensure_equals("the newest", e.caret().line, 2);
        keys("g;");
        ensure_equals("the one before", e.caret().line, 1);
        keys("g;");
        ensure_equals("no older", vim->message(), std::string("E662: At start of changelist"));
        keys("g,");
        ensure_equals("newer again", e.caret().line, 2);
        keys("gg'.");
        ensure_equals("'. the last change's line", e.caret().line, 2);
        std::vector<std::string> listed;
        vim->hooks().listing = [&listed](ALTextView&, const std::string& text) { listed.push_back(text); };
        keys(":changes<CR>");
        ensure("listed", listed.size() == 1 && listed[0].compare(0, 21, "change line  col text") == 0 &&
                             listed[0].find("\n>    0     3    1 c?") != std::string::npos);
    }

    template<> template<>
    void alvimkeymap_object::test<61>()
    {
        set_test_name("[[ ]] [m ]m go to the host's functions' starts, [] ][ [M ]M their ends; an operator takes them; af and if a function and its body");
        ALCodeEditor& e = make("integer f(integer x)\n{\n    return x;\n}\ndefault\n{\n    state_entry()\n    {\n        f(1);\n    }\n"
                               "    touch_start(integer n)\n    {\n        f(2);\n    }\n}\n");
        e.setFunctionProvider([](std::vector<ALTextRange>& out) {
            out = { ALTextRange(ALTextPos(10, 4), ALTextPos(13, 5)), ALTextRange(ALTextPos(0, 0), ALTextPos(3, 1)),
                    ALTextRange(ALTextPos(6, 4), ALTextPos(9, 5)) };
        });
        keys("3G]]");
        ensure("]] the next's start", e.caret() == ALTextPos(6, 4));
        keys("]m");
        ensure("]m the same", e.caret() == ALTextPos(10, 4));
        keys("[[");
        ensure("[[ back", e.caret() == ALTextPos(6, 4));
        keys("2[m");
        ensure("2[m two back", e.caret() == ALTextPos(0, 0));
        keys("][");
        ensure("][ its end", e.caret() == ALTextPos(3, 0));
        keys("][");
        ensure("and the next's", e.caret() == ALTextPos(9, 4));
        keys("[]");
        ensure("[] back", e.caret() == ALTextPos(3, 0));
        keys("''");
        ensure("a jump, as vim's are", e.caret() == ALTextPos(9, 4));

        keys("9Gdaf");
        ensure_equals("daf its lines", flat(e.text()),
                      std::string("integer f(integer x)|{|    return x;|}|default|{|"
                                  "    touch_start(integer n)|    {|        f(2);|    }|}|"));
        keys("u9Gdif");
        ensure_equals("dif its body, not the brace under its header", flat(e.text()),
                      std::string("integer f(integer x)|{|    return x;|}|default|{|    state_entry()|    {|    }|"
                                  "    touch_start(integer n)|    {|        f(2);|    }|}|"));
        keys("u9Gvaf");
        ensure("vaf selects its lines", vim->mode() == ALVimKeymap::Mode::VisualLine);
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<62>()
    {
        set_test_name("mappings: keys that may be the start of one held and shown; typed as they are when what follows makes none, or the time runs out");
        ALCodeEditor& e = make("abc\n");
        ex("inoremap jk <Esc>");
        keys("ijk");
        ensure("jk left insert mode", vim->mode() == ALVimKeymap::Mode::Normal);
        ensure_equals("nothing typed", flat(e.text()), std::string("abc|"));
        keys("ijx");
        ensure_equals("the j typed after all, then the x", flat(e.text()), std::string("jxabc|"));
        keys("<Esc>");
        ex("set timeoutlen=0");
        keys("Aj");
        ensure_equals("held, and shown after the mode", vim->status(), std::string("-- INSERT -- j"));
        ensure_equals("not typed yet", flat(e.text()), std::string("jxabc|"));
        vim->idle(e);
        ensure_equals("typed once the time ran out", flat(e.text()), std::string("jxabcj|"));
        ensure_equals("held no more", vim->status(), std::string("-- INSERT --"));
        keys("<Esc>");
        ex("set notimeout");
        keys("Aj");
        vim->idle(e);
        ensure_equals("without the timeout it waits", flat(e.text()), std::string("jxabcj|"));
        keys("k");
        ensure("and the rest makes the mapping", vim->mode() == ALVimKeymap::Mode::Normal && flat(e.text()) == "jxabcj|");
    }

    template<> template<>
    void alvimkeymap_object::test<63>()
    {
        set_test_name("mappings: map maps again and noremap does not, the leader, operator-pending's and the : line's own, and one that goes round");
        ALCodeEditor& e = make("one two\nthree\nfour\n");
        ex("nmap Q j");
        ex("nmap W Q");
        ex("nnoremap E W");
        keys("W");
        ensure_equals("W through Q to j", caretText(), std::string("1:0"));
        keys("ggE");
        ensure_equals("E is W itself, a WORD on", caretText(), std::string("0:4"));
        ex("let mapleader = ','");
        ex("nnoremap <leader>d dd");
        keys(",");
        ensure_equals("the leader held and shown", vim->status(), std::string(","));
        keys("d");
        ensure_equals("<leader>d is dd", flat(e.text()), std::string("three|four|"));
        keys("u");
        ensure_equals("undone as one", flat(e.text()), std::string("one two|three|four|"));
        keys("gg,x");
        ensure_equals("a leader nothing follows goes as it is: , then x", flat(e.text()), std::string("ne two|three|four|"));
        ex("onoremap w iw");
        keys("jldw");
        ensure_equals("dw, with w the inner word after an operator", flat(e.text()), std::string("ne two||four|"));
        ex("cnoremap <C-x> set ic");
        keys(":<C-x>");
        std::string line;
        S32         caret = 0;
        ensure("the : line's own", vim->typingLine(line, caret) && line == ":set ic");
        keys("<CR>");
        ensure("and it ran", vim->shared().ignoreCase);
        ex("nmap K Kj");
        keys("ggK");
        ensure("what starts with its own keys does not map the first again", caretText() == "1:0" && !vim->messageIsError());
        ex("nmap t dd");
        keys("ggft");
        ensure("the character f waits for is taken as it is", flat(e.text()) == "ne two||four|" && caretText() == "0:3");
        ex("nmap a b");
        ex("nmap b a");
        keys("a");
        ensure("round and round is stopped", vim->message().find("E223") != std::string::npos && vim->messageIsError());
        ex("nunmap zz");
        ensure("no such mapping", vim->message().find("E31") != std::string::npos);
    }

    template<> template<>
    void alvimkeymap_object::test<64>()
    {
        set_test_name("mappings: a macro records the keys typed and plays them through the mappings; :normal maps them, :normal! not; . repeats what one did");
        ALCodeEditor& e = make("a\nb\nc\nd\n");
        ex("inoremap jk <Esc>");
        keys("qaAxjkq");
        ensure_equals("typed", flat(e.text()), std::string("ax|b|c|d|"));
        ensure_equals("the keys as they were typed", vim->registerText('a'), std::string("Axjk"));
        keys("j@a");
        ensure("played through the mapping", flat(e.text()) == "ax|bx|c|d|" && vim->mode() == ALVimKeymap::Mode::Normal);
        ex("nnoremap Q dd");
        ex("3normal Q");
        ensure_equals(":normal maps", flat(e.text()), std::string("ax|bx|d|"));
        ex("1normal! Q");
        ensure_equals(":normal! does not", flat(e.text()), std::string("ax|bx|d|"));
        keys("ggQ.");
        ensure_equals(". does what the mapping did", flat(e.text()), std::string("d|"));
    }

    template<> template<>
    void alvimkeymap_object::test<65>()
    {
        set_test_name(":set takes several options, queries them and toggles them; the view's own set on a view");
        ALCodeEditor& e = make("x\n");
        ex("set ic scs tm=300");
        ensure("all three", vim->shared().ignoreCase && vim->shared().smartCase && vim->shared().timeoutLength == 300);
        ex("set ic? tm?");
        ensure_equals("shown", vim->message(), std::string("  ignorecase    timeoutlen=300"));
        ex("set invic scs!");
        ensure("toggled", !vim->shared().ignoreCase && !vim->shared().smartCase);
        ex("set tm=x");
        ensure("a number wanted", vim->messageIsError() && vim->message().find("E474") != std::string::npos);
        ex("set clipboard+=unnamed");
        ensure("added to", vim->shared().unnamedClipboard);
        ex("set clipboard-=unnamed");
        ensure("taken from", !vim->shared().unnamedClipboard);
        std::string shown;
        std::string error;
        ensure("tabstop", ALVimKeymap::setViewOption(e, "ts=2", shown, error) && e.getTabWidth() == 2 && error.empty());
        ensure("shiftwidth shown", ALVimKeymap::setViewOption(e, "sw?", shown, error) && shown == "  shiftwidth=2");
        ensure("expandtab", ALVimKeymap::setViewOption(e, "noet", shown, error) && !e.getSoftTabs());
        ensure("out of reach", ALVimKeymap::setViewOption(e, "ts=99", shown, error) && !error.empty() && e.getTabWidth() == 2);
        ensure("not a view's", !ALVimKeymap::setViewOption(e, "ic", shown, error));
    }

    template<> template<>
    void alvimkeymap_object::test<66>()
    {
        set_test_name("a vimrc: mappings, the leader and options taken, the host offered the rest, and what is not understood said by its line");
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
        ensure("its options", shared.ignoreCase && shared.smartCase && shared.timeoutLength == 300 && !shared.timeout);
        ensure("each editor's for the host to set", shared.viewOptions == std::vector<std::string>{ "ts=2", "et" });
        ensure("the host offered them first, and its own", offered.size() == 4 && offered[2] == "nu" && offered[3] == "foo");
        const ALVimMappings& maps = shared.mappings;
        ensure("<leader>w with the leader a space", maps.match(ALVimMappings::NORMAL, maps.keysOf("<Space>w"), true).full != nullptr);
        ensure("jk", maps.match(ALVimMappings::INSERT, maps.keysOf("jk"), true).full != nullptr);
        const ALVimMappings::Match y = maps.match(ALVimMappings::VISUAL, maps.keysOf("Y"), true);
        ensure("a line that goes on with a backslash", y.full && ALVimMappings::shown(y.full->to) == "y$");
        ensure_equals("two not understood", errors.size(), size_t(2));
        ensure("by their lines", errors[0].find("line 7:") == 0 && errors[0].find("E492") != std::string::npos &&
                                     errors[1].find("line 11:") == 0 && errors[1].find("E518") != std::string::npos);

        std::string listing;
        std::string error;
        shared.mappings.command("nnoremap", "Q gq", false, listing, error);
        shared.command.push_back("s/a/b/");
        errors.clear();
        ALVimKeymap::source(shared, "nnoremap Z zz\n", host, errors);
        ensure("read again, what it made before is gone", maps.match(ALVimMappings::INSERT, maps.keysOf("jk"), true).full == nullptr &&
                                                            maps.match(ALVimMappings::NORMAL, maps.keysOf("Z"), true).full != nullptr);
        ensure("and the leader vim's", maps.leader() == "\\");
        ensure("one typed kept", maps.match(ALVimMappings::NORMAL, maps.keysOf("Q"), true).full != nullptr);
        ensure("and the view's options it no longer sets", shared.viewOptions.empty());
        const ALVimKeymap::Shared vims;
        ensure("nor the mode's: those are vim's own again", shared.ignoreCase == vims.ignoreCase && shared.smartCase == vims.smartCase &&
                                                              shared.timeout == vims.timeout && shared.timeoutLength == vims.timeoutLength);
        ensure("the lines typed kept", shared.command == std::vector<std::string>{ "s/a/b/" });
    }

    template<> template<>
    void alvimkeymap_object::test<67>()
    {
        set_test_name("surround: ys puts a pair round a motion's stretch, an opening bracket with spaces; ds takes it away, cs changes it; . again");
        ALCodeEditor& e = make("say hello world\n");
        keys("wysiw)");
        ensure_equals("ysiw)", flat(e.text()), std::string("say (hello) world|"));
        ensure_equals("the caret on the pair's start", caretText(), std::string("0:4"));
        keys("ds)");
        ensure_equals("ds)", flat(e.text()), std::string("say hello world|"));
        keys("ysiw(");
        ensure_equals("ysiw( has spaces inside", flat(e.text()), std::string("say ( hello ) world|"));
        keys("ds(");
        ensure_equals("ds( takes them too", flat(e.text()), std::string("say hello world|"));
        keys("ysiw\"");
        keys("cs\"'");
        ensure_equals("cs\"'", flat(e.text()), std::string("say 'hello' world|"));
        keys("$ysiw]");
        ensure_equals("ysiw] on the last word", flat(e.text()), std::string("say 'hello' [world]|"));
        keys("0.");
        ensure_equals(". does it again here", flat(e.text()), std::string("[say] 'hello' [world]|"));
        keys("ysiw");
        ensure_equals("waiting for the character, said", vim->status(), std::string("ys"));
        keys("<Esc>");
        ensure("Escape lets it go", flat(e.text()) == "[say] 'hello' [world]|" && vim->status().empty());
        keys("0x.");
        ensure_equals("and the next command is one of its own, for . too", flat(e.text()), std::string("ay] 'hello' [world]|"));
        keys("ysiwx");
        ensure_equals("a letter is no pair", flat(e.text()), std::string("ay] 'hello' [world]|"));
        keys("uuu");
        ensure_equals("each undone as one step", flat(e.text()), std::string("say 'hello' [world]|"));
    }

    template<> template<>
    void alvimkeymap_object::test<68>()
    {
        set_test_name("surround: a count out, a mark's pair and a tag's taken away; yss the line's text; visual S round lines");
        ALCodeEditor& e = make("((a) b)\n*bold* and <b>x</b>\n  line text  \none\ntwo\n");
        keys("ll2ds)");
        ensure_equals("2ds) the outer pair", flat(e.text()), std::string("(a) b|*bold* and <b>x</b>|  line text  |one|two|"));
        keys("jlds*");
        ensure_equals("ds* the marks either side", flat(e.text()), std::string("(a) b|bold and <b>x</b>|  line text  |one|two|"));
        keys("$Fxdst");
        ensure_equals("dst the tags", flat(e.text()), std::string("(a) b|bold and x|  line text  |one|two|"));
        keys("3Gyss)");
        ensure_equals("yss the line's text, its blanks outside", flat(e.text()), std::string("(a) b|bold and x|  (line text)  |one|two|"));
        keys("4GVjS{");
        ensure_equals("visual S round lines, on lines of their own", flat(e.text()),
                      std::string("(a) b|bold and x|  (line text)  |{|one|two|}|"));
        keys("2Gcsbx");
        ensure_equals("no such pair: nothing", flat(e.text()), std::string("(a) b|bold and x|  (line text)  |{|one|two|}|"));
    }

    template<> template<>
    void alvimkeymap_object::test<69>()
    {
        set_test_name("every way into insert mode one step to undo, and none left open once escape leaves it: i a I A o O c C s S R gi and visual c");
        ALCodeEditor& e = make("one two\nthree four\nfive six\n");
        for (const char* seq : { "ix<Esc>", "ay<Esc>", "Iz<Esc>", "Aw<Esc>", "onew<Esc>", "Oup<Esc>", "cwX<Esc>", "ccY<Esc>", "CZ<Esc>",
                                 "sQ<Esc>", "SR<Esc>", "Rab<Esc>", "giT<Esc>", "vlcU<Esc>", "3ihi<Esc>" })
        {
            const std::string before = flat(e.text());
            keys(seq);
            ensure(std::string(seq) + ": none left open", !e.undoJournal().inGroup());
            keys("u");
            ensure_equals(std::string(seq) + ": one step", flat(e.text()), before);
            keys(seq);
        }
        const std::string before_x = flat(e.text());
        keys("x");
        keys("u");
        ensure_equals("a command after is a step of its own", flat(e.text()), before_x);
    }

    template<> template<>
    void alvimkeymap_object::test<70>()
    {
        set_test_name("a mapping that feeds itself ends where a key of it fails, or at the keys one draining may feed, with E223");
        ALCodeEditor& e = make("a\nb\nc\nd\n");
        ex("nmap j jj");
        keys("j");
        ensure_equals("down until j fails on the last line, and back", caretText(), std::string("4:0"));
        ensure("which is no error", !vim->messageIsError());
        // One that never fails: an A put in, and itself again.
        ex("nmap Q iA<Esc>Q");
        keys("ggQ");
        ensure("stopped: " + vim->message(), vim->messageIsError() && vim->message().find("E223") != std::string::npos);
        ensure("having done what it did meanwhile", e.document().line(0).size() > 100);
        // Stopped wherever the mapping was -- here in insert mode, as vim
        // leaves it -- and a key typed after goes as typed.
        keys("<Esc>");
        ensure("vim takes keys again", vim->mode() == ALVimKeymap::Mode::Normal);
    }

    template<> template<>
    void alvimkeymap_object::test<71>()
    {
        set_test_name("a count of a macro stops at the first play whose f fails, and is one step to undo");
        ALCodeEditor& e = make("a,b\nc,d\ne f\ng,h\n");
        keys("qa0f,xxjq");
        ensure_equals("recorded, done once", flat(e.text()), std::string("a|c,d|e f|g,h|"));
        keys("100@a");
        ensure_equals("the second play's f found no comma: it and the rest stopped", flat(e.text()), std::string("a|c|e f|g,h|"));
        keys("u");
        ensure_equals("undone as one step", flat(e.text()), std::string("a|c,d|e f|g,h|"));
    }

    template<> template<>
    void alvimkeymap_object::test<72>()
    {
        set_test_name("a macro of two hundred commands is one step to undo, and so is :normal over a range");
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
        ensure("played", e.text() != recorded);
        keys("u");
        ensure_equals("one step back", e.text(), recorded);

        ex("2,4normal Ax");
        ensure_equals("each line", flat(e.text()).substr(recorded.find('\n')), std::string("|onex|twox|threex|"));
        keys("u");
        ensure_equals("one step back", e.text(), recorded);
    }

    template<> template<>
    void alvimkeymap_object::test<73>()
    {
        set_test_name("a line picked from q:'s window after vim went, or the tab, does nothing");
        ALCodeEditor& e = make("one\n");
        keys(":set number<CR>");
        std::function<void(const std::string&, bool)> later;
        const auto hold = [&later](ALTextView&, llwchar, const std::vector<std::string>&, std::function<void(const std::string&, bool)> chosen) {
            later = std::move(chosen);
        };
        vim->hooks().historyWindow = hold;
        keys("q:");
        ensure("the window holds the pick", static_cast<bool>(later));
        e.setModalKeymap(nullptr);
        vim = nullptr;
        later("set nonumber", true);
        ensure("vim gone: nothing", !e.modalKeymap());

        auto keymap = std::make_unique<ALVimKeymap>();
        vim         = keymap.get();
        e.setModalKeymap(std::move(keymap));
        vim->hooks().historyWindow = hold;
        later                      = nullptr;
        keys(":set number<CR>q:");
        ensure("held again", static_cast<bool>(later));
        editor->die();
        editor = nullptr;
        LLMortician::updateClass();
        later("set nonumber", true);
        ensure("the tab gone: nothing", true);
    }

    template<> template<>
    void alvimkeymap_object::test<74>()
    {
        set_test_name("vim's Control motions: ^H left, ^J and ^N down, ^P up, ^M to the next line's first; ^L nothing; Tab forward in the jumps, indenting nothing");
        ALCodeEditor&            e = make("one\n  two\nthree\nfour\n");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("gg<C-N>");
        ensure_equals("^N down", e.caret().line, 1);
        keys("<C-J>");
        ensure_equals("^J down", e.caret().line, 2);
        keys("<C-P>");
        ensure_equals("^P up", e.caret().line, 1);
        keys("$<C-H>");
        ensure_equals("^H left", caretText(), std::string("1:3"));
        keys("gg<C-M>");
        ensure_equals("^M to the next line's first character", caretText(), std::string("1:2"));
        keys("<C-L>");
        keys("<Tab>");
        ensure("Tab forward in the jump list", heard.size() == 1 && heard[0] == "forward|");
        ensure_equals("and nothing indented, nothing changed", flat(e.text()), std::string("one|  two|three|four|"));
        keys("ggd<C-N>");
        ensure_equals("an operator takes them as it takes j", flat(e.text()), std::string("three|four|"));
    }

    template<> template<>
    void alvimkeymap_object::test<75>()
    {
        set_test_name("^W and a window command, as the host's tabs; ^W^W as ^Ww; one there is none of taken, and failed");
        ALCodeEditor&            e = make("a\n");
        std::vector<std::string> heard;
        vim->hooks().command = [&heard](ALTextView&, const std::string& name, const std::string& args) {
            heard.push_back(name + "|" + args);
            return true;
        };
        keys("<C-W>q<C-W><C-W><C-W>W<C-W>o<C-W>n<C-W>p<C-W>c");
        ensure("each the host's", heard == std::vector<std::string>({ "quit|", "tabnext|", "tabprevious|", "tabonly|", "tabnew|", "buffer|#", "close|" }));
        heard.clear();
        keys("<C-W>x");
        ensure("a split there is none of: nothing asked", heard.empty());
        ensure("and x not deleting", flat(e.text()) == "a|");
        keys("<C-W><Esc>x");
        ensure("Escape lets it go; x is x again", flat(e.text()) == "|" && heard.empty());
    }

    template<> template<>
    void alvimkeymap_object::test<76>()
    {
        set_test_name("a Control letter vim has no use for: on the Mac nobody's, the editor's own under it too; elsewhere the window's");
        ALCodeEditor& e = make("one two\n");
        keys("w");
#if LL_DARWIN
        ensure("taken", e.handleKeyHere('K', ALVimKeymap::CONTROL));
        ensure_equals("and the editor's ^K, to the line's end, not done", flat(e.text()), std::string("one two|"));
        ensure("^S too", e.handleKeyHere('S', ALVimKeymap::CONTROL));
#else
        ensure("^S on to the window, which saves", !e.handleKeyHere('S', ALVimKeymap::CONTROL));
        ensure("^K on to it, before a second key", !e.handleKeyHere('K', ALVimKeymap::CONTROL));
        ensure_equals("nothing changed", flat(e.text()), std::string("one two|"));
#endif
    }

    template<> template<>
    void alvimkeymap_object::test<77>()
    {
        set_test_name("vim's search and block lit apart from a name's references: :noh and leaving visual block leave those");
        ALCodeEditor& e = make("count count\ncount\n");
        typedef ALCodeEditor::Highlight H;
        e.setHighlights(H::References, { ALTextRange(ALTextPos(1, 0), ALTextPos(1, 5)) });
        keys(":set hls<CR>");
        keys("/count<CR>");
        ensure("the search lit", !e.highlights(H::Search).empty());
        keys(":noh<CR>");
        ensure("put out", e.highlights(H::Search).empty());
        ensure("the references kept", e.highlights(H::References).size() == 1);
        keys("gg<C-V>jl");
        ensure("the block lit", !e.highlights(H::Block).empty());
        keys("<Esc>");
        ensure("put out", e.highlights(H::Block).empty());
        ensure("the references kept still", e.highlights(H::References).size() == 1);
    }

    template<> template<>
    void alvimkeymap_object::test<78>()
    {
        set_test_name(":g visits its lines from the top down, a line's mark gone with it; its d, s, > and < go in as one edit, as :s///c's a does and "
                      "what `.`, a macro and a mapping type");
        ALCodeEditor& e     = make("a\nb\nc\nd\n");
        S32           edits = 0;
        boost::signals2::scoped_connection counting = e.document().onChanged([&edits](const ALTextDocument::Edit&) { ++edits; });
        keys(":g/^/m0<CR>");
        ensure_equals("turned over, each line moved to the top in turn", flat(e.text()), std::string("d|c|b|a|"));
        keys("u");
        ensure_equals("one step to undo", flat(e.text()), std::string("a|b|c|d|"));
        keys(":g/^/j<CR>");
        ensure_equals("joined in pairs: a line joined onto the one above is not visited", flat(e.text()), std::string("a b|c d|"));
        keys(":g/^/t.<CR>");
        ensure_equals("each copied below itself once, the copy not visited", flat(e.text()), std::string("a b|a b|c d|c d|"));

        e.setText("x 1\ny\nx 2\nx 3\n");
        edits = 0;
        keys(":g/x/d<CR>");
        ensure_equals("the lines taken out", flat(e.text()), std::string("y|"));
        ensure_equals("in one edit", edits, 1);
        ensure_equals("the register holds the last, as one at a time leaves it", vim->registerText('"'), std::string("x 3"));
        ensure_equals("the one before it in 2", vim->registerText('2'), std::string("x 2"));
        ensure_equals("said once for the lot", vim->message(), std::string("3 fewer lines"));
        ensure_equals("the caret where the last went", e.caret().line, 1);
        keys("u");
        ensure_equals("one step to undo", flat(e.text()), std::string("x 1|y|x 2|x 3|"));

        e.setText("a\nb\nc");
        keys(":g/^/d<CR>");
        ensure_equals("every line, the last with the break before it", flat(e.text()), std::string(""));

        e.setText("x a a\ny a\nx a\n");
        edits = 0;
        keys(":g/x/s/a/b/g<CR>");
        ensure_equals("substituted", flat(e.text()), std::string("x b b|y a|x b|"));
        ensure_equals("in one edit", edits, 1);
        ensure_equals("said once for the lot", vim->message(), std::string("3 substitutions on 2 lines"));
        ensure_equals("the caret on the last line substituted on", e.caret().line, 2);
        edits = 0;
        keys(":g/x/s/b/1\\r2/<CR>");
        ensure_equals("a break put in on each", flat(e.text()), std::string("x 1|2 b|y a|x 1|2|"));
        ensure_equals("the caret on the last line of the last put in", e.caret().line, 4);

        e.setText("x\ny\nx\n");
        e.setSoftTabs(true);
        e.setTabWidth(2);
        edits = 0;
        keys(":g/x/><CR>");
        ensure_equals("shifted in", flat(e.text()), std::string("  x|y|  x|"));
        ensure_equals("in one edit", edits, 1);
        edits = 0;
        keys(":g/x/<<CR>");
        ensure_equals("and out", flat(e.text()), std::string("x|y|x|"));
        ensure_equals("in one edit", edits, 1);

        e.setText("a a a\n");
        keys("gg:s/a/b/gc<CR>");
        edits = 0;
        keys("ya");
        ensure_equals("the rest said yes to at once", flat(e.text()), std::string("b b b|"));
        ensure_equals("the first, then the rest in one edit", edits, 2);
        ensure_equals("said", vim->message(), std::string("3 substitutions on 1 line"));

        e.setText("one\ntwo\nthree\n");
        keys("ggAhello<Esc>j");
        edits = 0;
        keys(".");
        ensure_equals("typed again", flat(e.text()), std::string("onehello|twohello|three|"));
        ensure_equals("in one edit", edits, 1);
        keys("ggqaAxyz<Esc>q");
        edits = 0;
        keys("jj@a");
        ensure_equals("played", flat(e.text()), std::string("onehelloxyz|twohello|threexyz|"));
        ensure_equals("in one edit", edits, 1);
        ex("inoremap ;h hi there");
        edits = 0;
        keys("A;h<Esc>");
        ensure_equals("mapped", flat(e.text()), std::string("onehelloxyz|twohello|threexyzhi there|"));
        ensure_equals("in one edit", edits, 1);
    }

    template<> template<>
    void alvimkeymap_object::test<79>()
    {
        set_test_name("the bracket objects and [( pass over a bracket in a string or a comment, as % does");
        ALCodeEditor& e = make("f(\"(\", x) // )\n");
        e.setCaret(ALTextPos(0, 7));
        keys("di(");
        ensure_equals("inside the call, not the string's bracket", flat(e.text()), std::string("f() // )|"));
        e.setText("g(a, \"(\", b)\n");
        vim->handleKey(e, KEY_ESCAPE, MASK_NONE);
        e.setCaret(ALTextPos(0, 10));
        keys("[(");
        ensure("back to the call's bracket", e.caret() == ALTextPos(0, 1));
        keys("%");
        ensure("and % to its partner", e.caret() == ALTextPos(0, 11));
    }

    template<> template<>
    void alvimkeymap_object::test<80>()
    {
        set_test_name("a line number typed is one shown: under lines counted from 10, 12G, 11gg and :13 go where the gutter says, and :g still finds its own lines");
        ALCodeEditor& e = make("one\ntwo\nthree\nfour\n");
        e.setLineNumberBase(10);
        keys("12G");
        ensure_equals("12G the second line", e.caret().line, 1);
        keys("11gg");
        ensure_equals("11gg the first", e.caret().line, 0);
        ex("13");
        ensure_equals(":13 the third", e.caret().line, 2);
        ex("2");
        ensure_equals("a number before the first, the first", e.caret().line, 0);
        ex("g/o/d");
        ensure_equals(":g deletes the lines it matched, not those the numbers would miss", flat(e.text()), std::string("three|"));
    }

    template<> template<>
    void alvimkeymap_object::test<81>()
    {
        set_test_name("gI inserts in the first column, before the indent, and . does it again");
        ALCodeEditor& e = make("    one\n    two\n");
        e.setCaret(ALTextPos(0, 6));
        keys("gIx<Esc>");
        ensure_equals("before the indent", flat(e.text()), std::string("x    one|    two|"));
        keys("j.");
        ensure_equals("again on the next line", flat(e.text()), std::string("x    one|x    two|"));
        keys("0wgI");
        ensure("in insert mode", vim->inserting());
    }
    template<> template<>
    void alvimkeymap_object::test<82>()
    {
        set_test_name("every change of what the band shows moves the generation: each key of a : line, a message, Escape");
        make("one\ntwo\n");
        U32 was = vim->generation();
        const auto moved = [&](const std::string& what) {
            ensure(what, vim->generation() != was);
            was = vim->generation();
        };
        keys(":");
        moved("the line opened");
        for (const char* key : { "s", "/", "o", "/", "x" })
        {
            keys(key);
            moved(std::string("typed ") + key);
        }
        keys("<BS>");
        moved("a character taken back");
        keys("<Esc>");
        moved("the line closed");
        keys("/nothing-here<CR>");
        moved("a pattern not found said");
        keys("j");
        moved("the message cleared by a key");
    }

    template<> template<>
    void alvimkeymap_object::test<83>()
    {
        set_test_name("]s run out of time says so, not that there is none, and ]s again goes on from there");
        ALCodeEditor& e = make("all fine\nall fine\nall fine\nteh end\n", "text");
        e.setSpellChecker([](const std::string& word) { return word != "teh"; });
        e.setSpellCheck(true);
        ll_test::TextViewProbe::misspellingBudget(e, 0.f);
        keys("]s");
        ensure_equals("said", vim->message(), std::string("Still checking the spelling: search again to go on"));
        ensure("not moved", e.caret() == ALTextPos(0, 0));
        keys("]s");
        keys("]s");
        ensure("still not", e.caret() == ALTextPos(0, 0));
        keys("]s");
        ensure("found, a line further each time", e.caret() == ALTextPos(3, 0));
        e.recheckSpelling();
        ll_test::TextViewProbe::misspellingBudget(e, ALTextView::MISSPELLING_BUDGET);
        keys("gg]s");
        ensure("with time, at once", e.caret() == ALTextPos(3, 0));
    }

    template<> template<>
    void alvimkeymap_object::test<84>()
    {
        set_test_name("n goes through the matches found once, sees an edit, lights them again after :noh; the search line lights as it is typed, not as the cursor moves");
        ALCodeEditor& e = make("one x\ntwo x\nthree x\n");
        const auto lit = [&]() { return e.highlights(ALCodeEditor::Highlight::Search).size(); };
        keys("/x<CR>");
        ensure("the first", e.caret() == ALTextPos(0, 4) && lit() == 3);
        keys("n");
        ensure("the next", e.caret() == ALTextPos(1, 4) && lit() == 3);
        keys("ggix<Esc>");
        keys("n");
        ensure("the text as it is now", e.caret() == ALTextPos(0, 5) && lit() == 4);
        ex("noh");
        ensure("put out", lit() == 0);
        keys("n");
        ensure("lit again by the next n", e.caret() == ALTextPos(1, 4) && lit() == 4);
        ex("noh");
        keys("/th");
        ensure_equals("lit as typed", lit(), size_t(1));
        keys("<Left><Right>");
        ensure_equals("the same as the cursor moves", lit(), size_t(1));
        keys("<BS>");
        ensure_equals("and again as the pattern changes", lit(), size_t(2));
        keys("<Esc>");
        ensure_equals("put out when left", lit(), size_t(0));
        // A pattern that stands by the caret is found afresh from wherever
        // it is: \%# only where the caret is.
        e.setCaret(ALTextPos(2, 6));
        keys("/\\%#x<CR>");
        ensure("at the caret", e.caret() == ALTextPos(2, 6));
    }

    template<> template<>
    void alvimkeymap_object::test<85>()
    {
        set_test_name("a macro longer than any mapping plays through a mapping at every place it comes");
        ALCodeEditor& e = make("l0\nl1\nl2\nl3\nl4\nl5\nl6\nl7\n");
        ex("nnoremap ,d dd");
        keys("qa,djq");
        ensure_equals("recorded as typed", vim->registerText('a'), std::string(",dj"));
        keys("3@a");
        ensure_equals("played through the mapping each time", flat(e.text()), std::string("l1|l3|l5|l7|"));
    }

    template<> template<>
    void alvimkeymap_object::test<86>()
    {
        set_test_name(":sort n puts the lines without a number first, u keeps one of each, i u one of each case aside");
        ALCodeEditor& e = make("x10\nb\n-3 y\nx2\nB\nb\n");
        ex("sort n");
        ensure_equals("by number, those with none first as they were", flat(e.text()), std::string("b|B|b|-3 y|x2|x10|"));
        ex("sort u");
        ensure_equals("by bytes, each once", flat(e.text()), std::string("-3 y|B|b|x10|x2|"));
        ex("sort i u");
        ensure_equals("case aside, the first of each kept", flat(e.text()), std::string("-3 y|B|x10|x2|"));
    }

    template<> template<>
    void alvimkeymap_object::test<87>()
    {
        set_test_name("what vim holds at places moves with an edit made from outside: the visual area being made, the :s edits still to ask about");
        ALCodeEditor& e = make("a\nb\nc\n");
        keys("jVj");
        e.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "top\n" } });
        keys("d");
        ensure_equals("the lines it was over deleted", flat(e.text()), std::string("top|a|"));

        e.setText("x\nx\nx\n");
        e.setCaret(ALTextPos(0, 0));
        keys(":%s/x/y/c<CR>");
        keys("y");
        e.replaceAll({ { ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "z" } });
        keys("yy");
        ensure_equals("each where its match went", flat(e.text()), std::string("y|y|zy|"));
    }

    template<> template<>
    void alvimkeymap_object::test<88>()
    {
        set_test_name(":s asking is one step to undo, the answers after something made meanwhile a step apart from it");
        ALCodeEditor& e = make("x\nx\nx\n");
        keys(":%s/x/y/c<CR>");
        keys("yna");
        ensure_equals("said yes, no, then all", flat(e.text()), std::string("y|x|y|"));
        e.undo();
        ensure_equals("one step", flat(e.text()), std::string("x|x|x|"));

        e.setCaret(ALTextPos(0, 0));
        keys(":%s/x/y/c<CR>");
        keys("y");
        e.replaceAll({ { ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "z" } });
        keys("yy");
        e.undo();
        ensure_equals("the answers after it", flat(e.text()), std::string("y|x|zx|"));
        e.undo();
        ensure_equals("what was made meanwhile, apart", flat(e.text()), std::string("y|x|x|"));
        e.undo();
        ensure_equals("the answer before it", flat(e.text()), std::string("x|x|x|"));
    }

    template<> template<>
    void alvimkeymap_object::test<89>()
    {
        set_test_name("a function key typed into a macro is kept by its name and plays; keys written and read back in the one notation");
        ALCodeEditor& e = make("abc\n");
        ex("nmap <F5> x");
        keys("qa");
        editor->handleKeyHere(KEY_F5, MASK_NONE);
        keys("q");
        ensure_equals("recorded by its name", vim->registerText('a'), std::string("<F5>"));
        keys("@a");
        ensure_equals("and played", flat(e.text()), std::string("c|"));
        const std::vector<ALVimKeymap::Input> back = ALVimKeymap::decodeInputs("<F12><C-F3><Space><cr>");
        ensure("read back", back.size() == 4 && back[0].key == KEY_F12 && back[1].key == KEY_F3 && (back[1].mask & ALVimKeymap::CONTROL) &&
                                back[2].isChar && back[2].ch == ' ' && back[3].key == KEY_RETURN);
        ensure_equals("and written", ALVimKeymap::encodeInputs(back), std::string("<F12><C-F3> <CR>"));
    }

    template<> template<>
    void alvimkeymap_object::test<90>()
    {
        set_test_name(":m and :t are one change each; > shifts a line of blanks and leaves an empty line, as vim does");
        ALCodeEditor& e = make("a\nb\nc\nd\n");
        ex("1,2m3");
        ensure("moved under c", flat(e.text()) == "c|a|b|d|" && e.caret().line == 2);
        e.undo();
        ensure_equals("one step back", flat(e.text()), std::string("a|b|c|d|"));
        ex("4t0");
        ensure("copied to the top", flat(e.text()) == "d|a|b|c|d|" && e.caret().line == 0);
        e.undo();

        e.setText("x\n  \n\ny\n");
        e.setCaret(ALTextPos(0, 0));
        keys(">3j");
        ensure("shifted", e.document().line(0).size() > 1 && e.document().line(3).size() > 1);
        ensure("a line of blanks too", e.document().line(1).size() > 2);
        ensure("an empty one not", e.document().line(2).empty());
    }

    template<> template<>
    void alvimkeymap_object::test<91>()
    {
        set_test_name("a register holding what gg and gc once stood for plays them as characters, not as the commands");
        ALCodeEditor& e = make("\x01\xee\x80\x81j\none\ntwo\n");
        keys("\"ay$");
        ensure_equals("yanked as it is", vim->registerText('a'), std::string("\x01\xee\x80\x81j"));
        keys("j");
        keys("@a");
        ensure_equals("not gg, nor gc: j alone moved", e.caret().line, 2);
        ensure_equals("nothing commented", flat(e.text()), std::string("\x01\xee\x80\x81j|one|two|"));
    }

    template<> template<>
    void alvimkeymap_object::test<92>()
    {
        set_test_name("vim over a plain text view, which is no host: searches, brackets and motions work; folds and functions are nothing");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("no host", view->vimHost() == nullptr);
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
        ensure("searched", view->caret() == ALTextPos(1, 2));
        type("n");
        ensure("n", view->caret() == ALTextPos(3, 0));
        view->setCaret(ALTextPos(0, 5));
        type("%");
        ensure("% by brackets summed afresh", view->caret() == ALTextPos(2, 0));
        type("zc");
        ensure("zc folds nothing, and the text is as it was", view->text() == "f(a) {\n  x\n}\nx again\n");
        view->die();
    }

    template<> template<>
    void alvimkeymap_object::test<93>()
    {
        set_test_name("under lines counted from 0, as a notecard's are read: 2G is the third line, 1G the second, and :0 the first");
        ALCodeEditor& e = make("zero\none\ntwo\nthree\n");
        e.setLineNumberBase(-1);
        ensure_equals("no lower than -1", (e.setLineNumberBase(-5), e.lineNumberBase()), -1);
        keys("2G");
        ensure_equals("2G the third line", e.caret().line, 2);
        ex("0");
        ensure_equals(":0 the first", e.caret().line, 0);
        keys("G");
        keys("1G");
        ensure_equals("1G the second", e.caret().line, 1);
    }

    template<> template<>
    void alvimkeymap_object::test<94>()
    {
        set_test_name("the match the search line shows as it is typed, and the one it goes to, come out from under the pinned headers, as Find's do");
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
        ensure("shown below the two pinned headers as it is typed: " + std::to_string(shown) + " of " + std::to_string(row_h), shown >= 2 * row_h);
        ensure("the caret where it was", e.caret().line == 74);
        keys("<CR>");
        const S32 below = e.layout().lineTop(44) - e.scrollY();
        ensure_equals("gone to", e.caret().line, 44);
        ensure("and below them there: " + std::to_string(below) + " of " + std::to_string(row_h), below >= 2 * row_h);
    }

    template<> template<>
    void alvimkeymap_object::test<95>()
    {
        set_test_name("vim has one caret: what insert mode typed at several stays, Escape leaves the main caret alone, stepped back onto it, and a count types again there only");
        ALCodeEditor& e = make("one\ntwo\nthree");
        e.setSelections(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)) });
        keys("iab");
        ensure_equals("typed at each", e.text(), std::string("abone\nabtwo\nabthree"));
        ensure("still several while inserting", e.hasOtherSelections());
        keys("<Esc>");
        ensure("one caret in normal mode", !e.hasOtherSelections());
        ensure("the main one, on the last character it typed", e.caret() == ALTextPos(2, 1));
        e.setSelections(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)) });
        keys("3iX<Esc>");
        ensure_equals("once at each, the count's again at the main one only", e.text(), std::string("Xabone\nXabtwo\nXXXabthree"));
        ensure("one caret", !e.hasOtherSelections());
    }

    template<> template<>
    void alvimkeymap_object::test<96>()
    {
        set_test_name("with several carets: a key held for a mapping goes in at each when let go, Replace overtypes at each, and an undo leaves normal mode one caret");
        ALCodeEditor& e     = make("one\ntwo");
        const auto    caret = [](S32 line, S32 column) { return ALTextRange(ALTextPos(line, column), ALTextPos(line, column)); };
        ex("inoremap jk <Esc>");
        e.setSelections(caret(1, 0), { caret(0, 0) });
        keys("ijx");
        ensure_equals("the held j at each, then the x", e.text(), std::string("jxone\njxtwo"));
        keys("<Esc>");
        e.setSelections(caret(1, 0), { caret(0, 0) });
        keys("RQ");
        ensure_equals("overtyped at each", e.text(), std::string("Qxone\nQxtwo"));
        keys("<Esc>");
        keys("u");
        ensure_equals("undone", e.text(), std::string("jxone\njxtwo"));
        ensure("one caret in normal mode after it", !e.hasOtherSelections());
    }

    template<> template<>
    void alvimkeymap_object::test<97>()
    {
        set_test_name("the registers are the shared vim's: what is yanked in one buffer is put in another, the clipboard off, and a macro played there");
        make("alpha\nbeta");
        const std::string outside("outside");
        LLClipboard::instance().copyToClipboard(outside, 0, static_cast<S32>(outside.size()));
        const std::shared_ptr<ALVimKeymap::Shared> shared = vim->sharedState();
        ensure("off, as vim has it", !shared->unnamedClipboard);
        keys("\"ayy");
        keys("jyy");
        keys("qqA!<Esc>q");

        // Another buffer, its keymap sharing the first's state.
        make("gamma");
        vim->share(shared);
        keys("p");
        ensure_equals("the unnamed register, from the other buffer", flat(editor->text()), std::string("gamma|beta"));
        keys("\"ap");
        ensure_equals("a named one", flat(editor->text()), std::string("gamma|beta|alpha"));
        keys("@q");
        ensure_equals("a macro recorded there", flat(editor->text()), std::string("gamma|beta|alpha!"));
        std::string held;
        LLClipboard::instance().pasteFromClipboard(held);
        ensure_equals("the clipboard left alone", held, outside);

        // One that shares nothing has its own.
        make("delta");
        keys("p");
        ensure_equals("nothing to put", flat(editor->text()), std::string("delta"));
    }

    template<> template<>
    void alvimkeymap_object::test<98>()
    {
        set_test_name("an operator before a key that is no motion, or a motion that cannot go, fails and types nothing: dn before any search, c& yz dv gUp ysq d\"");
        ALCodeEditor& e = make("foo bar\n");
        for (const char* typed : { "dn", "c&", "yz", "dv", "gUp", "ysq", "d\"" })
        {
            const std::string what(typed);
            keys(typed);
            ensure_equals(what + " leaves the text as it was", flat(e.text()), std::string("foo bar|"));
            ensure(what + ": normal mode, nothing pending", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
            ensure(what + ": the caret where it was", e.caret() == ALTextPos(0, 0));
        }
    }

    template<> template<>
    void alvimkeymap_object::test<99>()
    {
        set_test_name("cw on a word's last character, or on a one-letter word, changes that word alone; c2w there the next as well");
        ALCodeEditor& e = make("integer i = 0;\nfoo bar\nab cd ef\n");
        e.setCaret(ALTextPos(0, 8));
        keys("cwj<Esc>");
        ensure_equals("a one-letter word, not what follows it", e.document().line(0), std::string("integer j = 0;"));
        e.setCaret(ALTextPos(1, 2));
        keys("cwX<Esc>");
        ensure_equals("from a word's last character, that character", e.document().line(1), std::string("foX bar"));
        e.setCaret(ALTextPos(2, 1));
        keys("c2wX<Esc>");
        ensure_equals("c2w from there: the word it ends and the next", e.document().line(2), std::string("aX ef"));
    }

    template<> template<>
    void alvimkeymap_object::test<100>()
    {
        set_test_name("an operator's words go on across a line's end where the count does: only the last word stops at its line's end");
        ALCodeEditor& e = make("one\ntwo three\nx\nab cd\nef gh\n");
        keys("d2w");
        ensure_equals("d2w takes the line's word, the break and the next line's first word", flat(e.text()), std::string("three|x|ab cd|ef gh|"));
        e.setCaret(ALTextPos(2, 3));
        keys("c2wX<Esc>");
        ensure_equals("c2w changes through the next line's first word", flat(e.text()), std::string("three|x|ab X gh|"));
        e.setCaret(ALTextPos(0, 0));
        keys("y3w");
        ensure_equals("y3w yanks three words across two line breaks", vim->registerText('0'), std::string("three\nx\nab "));
    }

    template<> template<>
    void alvimkeymap_object::test<101>()
    {
        set_test_name("a count before the operator counts for f t F T, G, |, ]) and the text objects as one after it does");
        ALCodeEditor& e = make("a.b.c.d\n1\n2\n3\n4\n5\nabcdef\nf(a, g(b), c)\none two three\n");
        keys("2df.");
        ensure_equals("2df. is d2f.", e.document().line(0), std::string("c.d"));
        e.setCaret(ALTextPos(4, 0));
        keys("2dG");
        ensure_equals("2dG takes up to the second line, not to the last", flat(e.text()), std::string("c.d|5|abcdef|f(a, g(b), c)|one two three|"));
        e.setCaret(ALTextPos(2, 4));
        keys("3d|");
        ensure_equals("3d| takes back to the third column", e.document().line(2), std::string("abef"));
        e.setCaret(ALTextPos(3, 7));
        keys("2d])");
        ensure_equals("2d]) takes to the second close out", e.document().line(3), std::string("f(a, g()"));
        e.setCaret(ALTextPos(4, 0));
        keys("2diw");
        ensure_equals("2diw is d2iw", e.document().line(4), std::string("two three"));
    }

    template<> template<>
    void alvimkeymap_object::test<102>()
    {
        set_test_name("an exclusive motion an operator takes to a line's first column ends with the line before: d} keeps the blank line, y} from a line's start yanks lines");
        ALCodeEditor& e = make("xa\nb\n\nc\n");
        e.setCaret(ALTextPos(0, 1));
        keys("d}");
        ensure_equals("d} leaves the blank line it goes to", flat(e.text()), std::string("x||c|"));
        keys("u");
        e.setCaret(ALTextPos(0, 0));
        keys("y}");
        ensure_equals("y} from the line's start yanks the lines", vim->registerText('0'), std::string("xa\nb"));
        keys("p");
        ensure_equals("which are put as lines", flat(e.text()), std::string("xa|xa|b|b||c|"));

        ALCodeEditor& marked = make("one\ntwo\nthree\n");
        keys("jjma");
        marked.setCaret(ALTextPos(0, 1));
        keys("d`a");
        ensure_equals("d`a to a mark in the first column takes up to the end of the line before", flat(marked.text()), std::string("o|three|"));

        // $ is inclusive, though its end is past the last character: d2$
        // onto an empty line takes the break before it.
        ALCodeEditor& ended = make("ab\n\ncd\n");
        ended.setCaret(ALTextPos(0, 1));
        keys("d2$");
        ensure_equals("d2$ onto an empty line joins it", flat(ended.text()), std::string("a|cd|"));
    }

    template<> template<>
    void alvimkeymap_object::test<103>()
    {
        set_test_name("D and d$ on an empty line take nothing, and leave the registers and the clipboard as they were");
        ALCodeEditor& e = make("one\n\nthree\n");
        keys("yyj");
        keys("D");
        ensure_equals("nothing taken out", flat(e.text()), std::string("one||three|"));
        ensure_equals("the unnamed register still holds the yank", vim->registerText('"'), std::string("one"));
        keys("d$");
        ensure_equals("d$ leaves it too", vim->registerText('"'), std::string("one"));
        keys("p");
        ensure_equals("which p puts", flat(e.text()), std::string("one||one|three|"));

        keys(":set clipboard=unnamed<CR>");
        const std::string outside("outside");
        LLClipboard::instance().copyToClipboard(outside, 0, static_cast<S32>(outside.size()));
        keys("k");
        keys("D");
        std::string held;
        LLClipboard::instance().pasteFromClipboard(held);
        ensure_equals("with the clipboard the unnamed register, the clipboard is left alone", held, outside);
    }

    template<> template<>
    void alvimkeymap_object::test<104>()
    {
        set_test_name(":g runs a command with an address of its own as it is written, from each line: .,+1d and .m0; and +1 alone is .+1");
        ALCodeEditor& e = make("a\nTODO 1\nb\nc\nTODO 2\nd\ne\n");
        keys(":g/TODO/.,+1d<CR>");
        ensure("no error", !vim->messageIsError());
        ensure_equals("each match and the line after it gone", flat(e.text()), std::string("a|c|e|"));

        ALCodeEditor& moved = make("x 1\ny\nx 2\n");
        keys(":g/x/.m0<CR>");
        ensure("no error for .m0", !vim->messageIsError());
        ensure_equals("each match moved to the top in turn", flat(moved.text()), std::string("x 2|x 1|y|"));

        keys("gg:+1d<CR>");
        ensure_equals(":+1d takes the line below", flat(moved.text()), std::string("x 2|y|"));
    }

    template<> template<>
    void alvimkeymap_object::test<105>()
    {
        set_test_name("% is 1,$: the empty line after a final line break is none of its lines, for :s, :normal and :y");
        ALCodeEditor& e = make("a\nb\n");
        keys(":%s/$/;/<CR>");
        ensure_equals(":%s/$/;/ ends the two lines, and makes no third", flat(e.text()), std::string("a;|b;|"));
        keys("u");
        keys(":%norm A;<CR>");
        ensure_equals(":%norm A; types on the two lines alone", flat(e.text()), std::string("a;|b;|"));
        keys("u");
        keys(":%y<CR>");
        ensure_equals(":%y yanks the two lines", vim->registerText('0'), std::string("a\nb"));
    }

    template<> template<>
    void alvimkeymap_object::test<106>()
    {
        set_test_name("a visual block is the columns the reader counts: what it cuts, lights, replaces, types onto and puts is whole characters, wherever the bytes before them put them");
        make("\xc3\xa9\n");
        keys("<C-v>d");
        ensure_equals("a character of two bytes cut whole", editor->document().line(0), std::string());

        make("a\xc3\xa9\nab\n");
        keys("l<C-v>jd");
        ensure_equals("the first line's column, whole", editor->document().line(0), std::string("a"));
        ensure_equals("the second's", editor->document().line(1), std::string("a"));

        make("\tab\nxxxxab\n");
        editor->setTabWidth(4);
        keys("l<C-v>jd");
        ensure_equals("after a tab, the column it reaches", editor->document().line(0), std::string("\tb"));
        ensure_equals("the same column under it", editor->document().line(1), std::string("xxxxb"));

        make("\xc3\xa9" "a\nxa\n");
        keys("l<C-v>jrx");
        ensure_equals("r past a character of two bytes", editor->document().line(0), std::string("\xc3\xa9" "x"));
        ensure_equals("and under it", editor->document().line(1), std::string("xx"));

        make("\xc3\xa9" "a\nxa\n");
        keys("l<C-v>jIZ<Esc>");
        ensure_equals("I before the column, not inside the character", editor->document().line(0), std::string("\xc3\xa9" "Za"));
        ensure_equals("and on the line under it", editor->document().line(1), std::string("xZa"));

        make("\xc3\xa9x\n");
        keys("<C-v>");
        const std::vector<ALTextRange>& lit = editor->highlights(ALCodeEditor::Highlight::Block);
        ensure("lit over the whole character", lit.size() == 1 && lit[0] == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        keys("<Esc>");

        make("ab\ncd\n\xc3\xa9x\n\xc3\xa9y\n");
        keys("<C-v>jy2jp");
        ensure_equals("a block put past a character of two bytes", editor->document().line(2), std::string("\xc3\xa9" "ax"));
        ensure_equals("and on the line under it", editor->document().line(3), std::string("\xc3\xa9" "cy"));

        make("xxxxab\n\tab\nxxxxab\n");
        editor->setTabWidth(4);
        keys("4l<C-v>2j<Esc>:%s/\\%Va/Z/g<CR>");
        ensure_equals("\\%V on a block: its column on a line a tab begins", flat(editor->text()), std::string("xxxxZb|\tZb|xxxxZb|"));
    }

    template<> template<>
    void alvimkeymap_object::test<107>()
    {
        set_test_name("visual p: lines replaced by a register's lines and no blank one more, lines put into part of a line on lines of their own, and what a block holds of each line replaced");
        ALCodeEditor& e = make("a\nb\nc\n");
        keys("yyjVp");
        ensure_equals("V p: the line replaced, and nothing more", flat(e.text()), std::string("a|a|c|"));
        ensure_equals("the caret on the line put", caretText(), std::string("1:0"));

        make("abcd\nX\n");
        keys("jyygglvlp");
        ensure_equals("v p of a line: on a line of its own, the line broken round it", flat(editor->text()), std::string("a|X|d|X|"));

        make("abcd\nefgh\nX\n");
        keys("2jylggl<C-v>jlp");
        ensure_equals("a block's columns each replaced by a register of one line, the rest of the lines kept", flat(editor->text()), std::string("aXd|eXh|X|"));

        make("ab\ncd\nxyz\nxyz\n");
        keys("<C-v>jy2jl<C-v>jp");
        ensure_equals("a block register put in place of a block", flat(editor->text()), std::string("ab|cd|xaz|xcz|"));
    }

    template<> template<>
    void alvimkeymap_object::test<108>()
    {
        set_test_name("visual block A types past the block's last column on every line, a short line padded out to it, and past every line's end for a block taken with $");
        ALCodeEditor& e = make("abcd\nefgh\n");
        keys("l<C-v>jlAX<Esc>");
        ensure_equals("past the block, not before its last column", flat(e.text()), std::string("abcXd|efgXh|"));

        make("ab\nlonger\n");
        keys("l<C-v>jllA;<Esc>");
        ensure_equals("a line short of the block padded out to its edge", flat(editor->text()), std::string("ab  ;|long;er|"));

        make("ab\nlonger\n");
        keys("<C-v>j$A;<Esc>");
        ensure_equals("with $, past each line's end", flat(editor->text()), std::string("ab;|longer;|"));

        make("longer\nab\n");
        keys("<C-v>j$");
        const std::vector<ALTextRange>& lit = editor->highlights(ALCodeEditor::Highlight::Block);
        ensure("with $, lit to every line's end", lit.size() == 2 && lit[0] == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 6)));
        keys("h");
        ensure("and no longer once the caret moves along the line, from past the last character onto it",
               editor->highlights(ALCodeEditor::Highlight::Block).size() == 2 &&
                   editor->highlights(ALCodeEditor::Highlight::Block)[0] == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        keys("$d");
        ensure_equals("a block taken with $ is every line to its end", flat(editor->text()), std::string("||"));
    }

    template<> template<>
    void alvimkeymap_object::test<109>()
    {
        set_test_name("/ and ? over a visual selection move its caret, and it stays for the operator after; Escape and a backspace on the empty line go back to it");
        ALCodeEditor& e = make("one foo two\nthree\n");
        keys("v/foo<CR>");
        ensure("still visual after the search", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("d");
        ensure_equals("d from where it began through the match's first character", flat(e.text()), std::string("oo two|three|"));

        make("one foo two\n");
        keys("$v?foo<CR>d");
        ensure_equals("? back over the selection", flat(editor->text()), std::string("one |"));

        make("ab\ncd\nxy\n");
        keys("<C-v>/y<CR>");
        ensure("a block stays a block", vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("d");
        ensure_equals("and takes its columns down to the match", flat(editor->text()), std::string("|||"));

        make("one foo\n");
        keys("v/fo<Esc>");
        ensure("Escape back to visual", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("<Esc>V/<BS>");
        ensure("a backspace on the empty line back to visual by lines", vim->mode() == ALVimKeymap::Mode::VisualLine);
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<110>()
    {
        set_test_name("what an insert typed is read off the text: a count types again what Ctrl-W left standing, and after an arrow . types again only what followed it");
        ALCodeEditor& e = make("\n");
        keys("3ifoo bar<C-w>baz<Esc>");
        ensure_equals("what stood after Ctrl-W, three times", e.document().line(0), std::string("foo bazfoo bazfoo baz"));
        keys("A <C-a><Esc>");
        ensure_equals("and Ctrl-A types it", e.document().line(0), std::string("foo bazfoo bazfoo baz foo baz"));

        make("\n");
        keys("ifoo<Left>X<Esc>");
        ensure_equals("typed where the arrow left the caret", editor->document().line(0), std::string("foXo"));
        keys(".");
        ensure_equals(". types again what followed the arrow alone", editor->document().line(0), std::string("foXXo"));
    }

    template<> template<>
    void alvimkeymap_object::test<111>()
    {
        set_test_name("a count before o and O opens as many lines, each with what was typed, and . does it again");
        ALCodeEditor& e = make("abc\n");
        keys("3ofoo<Esc>");
        ensure_equals("three lines below", flat(e.text()), std::string("abc|foo|foo|foo|"));
        keys("gg2Obar<Esc>");
        ensure_equals("two above", flat(e.text()), std::string("bar|bar|abc|foo|foo|foo|"));
        keys("G.");
        ensure_equals(". opens two more", flat(e.text()), std::string("bar|bar|abc|foo|foo|foo|bar|bar|"));
    }

    template<> template<>
    void alvimkeymap_object::test<112>()
    {
        set_test_name("Ctrl-O's one command that inserts itself -- o, A, cw -- is the insert: one Escape leaves it for normal mode");
        ALCodeEditor& e = make("abc\n");
        keys("A<C-o>ox<Esc>");
        ensure("normal mode after one Escape", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().find("-- (insert) --") == std::string::npos);
        ensure_equals("the line opened and typed on", flat(e.text()), std::string("abc|x|"));
        keys("i<C-o>A!<Esc>");
        ensure("after A too", vim->mode() == ALVimKeymap::Mode::Normal);
        ensure_equals("typed at the line's end", flat(e.text()), std::string("abc|x!|"));
        keys("ggi<C-o>cwyz<Esc>");
        ensure("and after cw", vim->mode() == ALVimKeymap::Mode::Normal);
        ensure_equals("the word changed", flat(e.text()), std::string("yz|x!|"));
    }

    template<> template<>
    void alvimkeymap_object::test<113>()
    {
        set_test_name("visual r puts one character in place of each character, however many bytes it takes");
        ALCodeEditor& e = make("h\xc3\xa9llo\n");
        keys("v$rx");
        ensure_equals("five characters, five x", e.document().line(0), std::string("xxxxx"));

        make("a\xf0\x9f\x98\x80" "b\n");
        keys("v$rx");
        ensure_equals("an emoji one", editor->document().line(0), std::string("xxx"));

        make("\xc3\xa9\n");
        keys("<C-v>rx");
        ensure_equals("and in a block", editor->document().line(0), std::string("x"));
    }

    template<> template<>
    void alvimkeymap_object::test<114>()
    {
        set_test_name(": typed over a visual selection is over its lines: '<,'> are the selection it let go of, which gv selects again");
        ALCodeEditor& e = make("a\nb\nc\nd\n");
        keys("jVj:d<CR>");
        ensure_equals("the two lines selected deleted", flat(e.text()), std::string("a|d|"));

        make("ab\ncd\nef\n");
        keys("l<C-v>j:<Esc>");
        ensure("the block put out", editor->highlights(ALCodeEditor::Highlight::Block).empty());
        keys("gv");
        ensure("gv selects the block again", vim->mode() == ALVimKeymap::Mode::VisualBlock && editor->highlights(ALCodeEditor::Highlight::Block).size() == 2);
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<115>()
    {
        set_test_name("q: over a visual selection lets it go as : does, q/ keeps it as / does, and a block left for insert mode or an asking g& is put out: gv selects it again");
        ALCodeEditor& e = make("ab\ncd\n");
        keys("<C-v>jq:");
        ensure("q: opens the : line", vim->mode() == ALVimKeymap::Mode::Command);
        ensure("the block put out", e.highlights(ALCodeEditor::Highlight::Block).empty());
        keys("<Esc>gv");
        ensure("gv selects it again", vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("<Esc>");

        make("one foo two\n");
        keys("vq/foo<CR>");
        ensure("q/ searches over the selection", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("d");
        ensure_equals("which runs to the match", flat(editor->text()), std::string("oo two|"));

        make("ab\ncd\n");
        keys("<C-v>jI<Esc>");
        ensure("insert mode puts the block out", vim->mode() == ALVimKeymap::Mode::Normal && editor->highlights(ALCodeEditor::Highlight::Block).empty());
        keys("gv");
        ensure("and keeps it for gv", vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("<Esc>");

        make("a\na\n");
        keys(":s/a/b/c<CR>q<C-v>jg&");
        ensure("an asking g& over a block puts it out", vim->mode() == ALVimKeymap::Mode::Confirm && editor->highlights(ALCodeEditor::Highlight::Block).empty());
        keys("qgv");
        ensure("and keeps it for gv after", vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<116>()
    {
        set_test_name("a line handed back by q:'s window ends what vim was doing as its keys would: an insert as Escape does, an asking :s as q does, a selection as : does");
        ALCodeEditor& e = make("abc\n");
        keys("3ix");
        vim->takeLine(e, ':', "s/b/B/", true);
        ensure_equals("the insert typed again for its count, then the line run", e.document().line(0), std::string("xxxaBc"));
        keys("u");
        ensure_equals("the line's change a step of its own", e.document().line(0), std::string("xxxabc"));
        keys("u");
        ensure_equals("and the insert's", e.document().line(0), std::string("abc"));

        make("\n");
        keys("i<C-v>");
        vim->takeLine(*editor, ':', "set ic", true);
        keys("iu0041<Esc>");
        ensure_equals("a Ctrl-V waiting let go of with the insert", editor->document().line(0), std::string("u0041"));

        make("a a\n");
        keys(":s/a/b/gc<CR>");
        ensure("asking", vim->mode() == ALVimKeymap::Mode::Confirm);
        vim->takeLine(*editor, ':', "s/a/c/", true);
        ensure("the matches still to come put out", editor->highlights(ALCodeEditor::Highlight::Confirm).empty());
        ensure_equals("the line run once the asking ended", editor->document().line(0), std::string("c a"));

        make("abcd\n");
        keys("vl");
        vim->takeLine(*editor, ':', "set ic", true);
        ensure("the selection let go of at its caret", vim->mode() == ALVimKeymap::Mode::Normal && caretText() == "0:1" && !editor->hasSelection());
    }

    template<> template<>
    void alvimkeymap_object::test<117>()
    {
        set_test_name("p puts an empty line that yy or dd took, through the clipboard as well; only a register never set holds nothing");
        ALCodeEditor& e = make("a\n\nb\n");
        keys("p");
        ensure("a register never set: E353", vim->messageIsError() && vim->message().find("E353") != std::string::npos);
        keys("jyyp");
        ensure("an empty line is something to put", !vim->messageIsError());
        ensure_equals("yy then p: the empty line again below", flat(e.text()), std::string("a|||b|"));
        keys("ddp");
        ensure_equals("dd then p: below the line that took its place", flat(e.text()), std::string("a||b||"));
        ex("set clipboard=unnamed");
        keys("yyp");
        ensure("by the clipboard, which an empty line leaves empty", !vim->messageIsError());
        ensure_equals("put all the same", flat(e.text()), std::string("a||b|||"));
    }

    template<> template<>
    void alvimkeymap_object::test<118>()
    {
        set_test_name("an operator over f, t, F, T or a mark that cannot move fails whole: nothing changed, no insert, the register and . as they were");
        ALCodeEditor& e = make("abc def\n");
        keys("xyiww");
        keys("cfz");
        ensure("cfz with no z: still normal mode", vim->mode() == ALVimKeymap::Mode::Normal);
        ensure("and no step to undo left open", !e.undoJournal().inGroup());
        ensure_equals("nothing changed", flat(e.text()), std::string("bc def|"));
        ensure_equals("the register as it was", vim->registerText('"'), std::string("bc"));
        keys("ytz");
        ensure_equals("ytz keeps nothing", vim->registerText('"'), std::string("bc"));
        keys("dFz");
        keys("c`q");
        ensure("c to a mark not set: still normal mode", vim->mode() == ALVimKeymap::Mode::Normal);
        ensure_equals("still nothing changed", flat(e.text()), std::string("bc def|"));
        ensure_equals("nor the register", vim->registerText('"'), std::string("bc"));
        keys(".");
        ensure_equals(". is the x before them", flat(e.text()), std::string("bc ef|"));
    }

    template<> template<>
    void alvimkeymap_object::test<119>()
    {
        set_test_name("a count of aw counts words with their blanks, of iw words and the blanks between alike; aw on blanks takes the word after, and leaves an indent");
        ALCodeEditor& e = make("foo bar baz\n");
        keys("2daw");
        ensure_equals("2daw: two words and the blanks after them", e.document().line(0), std::string("baz"));
        keys("u");
        e.setCaret(ALTextPos(0, 4));
        keys("d2aw");
        ensure_equals("d2aw from the middle: the blank before, none being after", e.document().line(0), std::string("foo"));
        keys("u");
        e.setCaret(ALTextPos(0, 0));
        keys("2diw");
        ensure_equals("2diw: the word and the blank after", e.document().line(0), std::string("bar baz"));
        keys("u");
        e.setCaret(ALTextPos(0, 0));
        keys("3diw");
        ensure_equals("3diw: to the second word's end", e.document().line(0), std::string(" baz"));
        keys("u");
        e.setCaret(ALTextPos(0, 0));
        keys("v2awd");
        ensure_equals("v2aw selects as much", e.document().line(0), std::string("baz"));

        make("foo.x bar.y baz\n");
        keys("2daW");
        ensure_equals("2daW by WORDs", editor->document().line(0), std::string("baz"));

        make("foo   bar baz\n");
        editor->setCaret(ALTextPos(0, 4));
        keys("daw");
        ensure_equals("daw on blanks: they and the word after", editor->document().line(0), std::string("foo baz"));

        make("  foo\n");
        keys("^daw");
        ensure_equals("daw on a line's only word leaves its indent", editor->document().line(0), std::string("  "));
    }

    template<> template<>
    void alvimkeymap_object::test<120>()
    {
        set_test_name("s on an empty line inserts there, with a count before it too, the registers left as they were; S as it did");
        ALCodeEditor& e = make("abc\n\n\n\nxyz\n");
        keys("yiwjs");
        ensure("s on an empty line: insert mode", vim->mode() == ALVimKeymap::Mode::Insert);
        keys("new<Esc>");
        ensure_equals("what was typed there", flat(e.text()), std::string("abc|new|||xyz|"));
        keys("j3sx<Esc>");
        ensure_equals("3s as well", flat(e.text()), std::string("abc|new|x||xyz|"));
        ensure_equals("nothing taken, so the register as it was", vim->registerText('"'), std::string("abc"));
        keys("u");
        ensure_equals("each one step to undo", flat(e.text()), std::string("abc|new|||xyz|"));
        e.setCaret(ALTextPos(3, 0));
        keys("Sy<Esc>");
        ensure_equals("S on an empty line inserts too", flat(e.text()), std::string("abc|new||y|xyz|"));
    }

    template<> template<>
    void alvimkeymap_object::test<121>()
    {
        set_test_name("visual O: in a block the other corner on the caret's own line, the columns traded; in charwise and linewise visual as o");
        ALCodeEditor& e = make("abcdef\nabcdef\nabcdef\n");
        keys("l<C-v>jllO");
        ensure("still the block, no line opened", vim->mode() == ALVimKeymap::Mode::VisualBlock && flat(e.text()) == "abcdef|abcdef|abcdef|");
        ensure_equals("the caret to the block's left, on its own line", caretText(), std::string("1:1"));
        keys("O");
        ensure_equals("and back to its right", caretText(), std::string("1:3"));
        keys("jOd");
        ensure_equals("the same columns, a line further", flat(e.text()), std::string("aef|aef|aef|"));

        make("one two\n");
        keys("vllO");
        ensure("charwise: still visual", vim->mode() == ALVimKeymap::Mode::Visual);
        ensure_equals("the caret to the other end", caretText(), std::string("0:0"));
        keys("d");
        ensure_equals("the same characters", editor->document().line(0), std::string(" two"));

        make("a\nb\nc\nd\n");
        keys("jVjOkd");
        ensure_equals("linewise: the caret to the first line, which k goes on from", flat(editor->text()), std::string("d|"));
    }

    template<> template<>
    void alvimkeymap_object::test<122>()
    {
        set_test_name("visual gi is vim's: the selection goes on to where inserting last stopped, and no insert begins");
        ALCodeEditor& e = make("abc\ndef\nghi\n");
        keys("A!<Esc>jj0vgi");
        ensure("still visual", vim->mode() == ALVimKeymap::Mode::Visual);
        ensure_equals("the caret where inserting stopped", caretText(), std::string("0:4"));
        keys("d");
        ensure_equals("the selection from there taken", flat(e.text()), std::string("abc!hi|"));

        make("ab\ncd\n");
        keys("<C-v>jgi");
        ensure("a block stays one, with no insert to have stopped", vim->mode() == ALVimKeymap::Mode::VisualBlock && caretText() == "1:0");
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<130>()
    {
        set_test_name("/ and ? after an operator are its motion, up to the match and not into it; an offset of lines makes it lines, /e takes the match's end");
        ALCodeEditor& e = make("one foo two\n");
        keys("d/foo<CR>");
        ensure_equals("d/foo up to the match", flat(e.text()), std::string("foo two|"));
        ensure("normal mode, nothing pending", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
        ensure_equals("what it took in the register", vim->registerText('"'), std::string("one "));

        make("alpha beta gamma\n");
        keys("$c?beta<CR>X<Esc>");
        ensure_equals("c?beta back to the match, the caret's character left, then typed into", flat(editor->text()), std::string("alpha Xa|"));
        ensure("normal mode after the insert", vim->mode() == ALVimKeymap::Mode::Normal);

        make("one foo two\n");
        keys("d/foo/e<CR>");
        ensure_equals("/foo/e through the match's last character", flat(editor->text()), std::string(" two|"));

        make("one\ntwo\nthree foo\nfour\n");
        keys("d/foo/-<CR>");
        ensure_equals("/foo/- the lines down to the one before the match's", flat(editor->text()), std::string("three foo|four|"));

        make("a\nb foo\nc\nd\n");
        keys("jly/foo/+1<CR>");
        ensure_equals("/foo/+1 yanks the lines, its own whole", vim->registerText('"'), std::string("b foo\nc"));
    }

    template<> template<>
    void alvimkeymap_object::test<131>()
    {
        set_test_name("n and N after an operator are its motion, the last search's again; a count before the operator or after it is the match that many on");
        ALCodeEditor& e = make("a foo b foo c foo\n");
        keys("/foo<CR>dn");
        ensure_equals("dn up to the next match", flat(e.text()), std::string("a foo c foo|"));
        keys("$dN");
        ensure_equals("dN back to the one before, the caret's character left", flat(e.text()), std::string("a foo c o|"));

        make("a x b x c x d\n");
        keys("2d/x<CR>");
        ensure_equals("2d/x up to the second x", flat(editor->text()), std::string("x c x d|"));
        keys("u0d2n");
        ensure_equals("and d2n", flat(editor->text()), std::string("x c x d|"));
    }

    template<> template<>
    void alvimkeymap_object::test<132>()
    {
        set_test_name("the search line after an operator lights as a plain one does; Escape or a backspace on the empty line lets the operator go, and a search that finds nothing fails it");
        ALCodeEditor& e = make("one two\n");
        keys("d/o");
        ensure("the search line", vim->mode() == ALVimKeymap::Mode::Search);
        ensure_equals("each match lit as typed", e.highlights(ALCodeEditor::Highlight::Search).size(), size_t(2));
        keys("<Esc>");
        ensure("let go of, the operator with it", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
        ensure("the matches put out", e.highlights(ALCodeEditor::Highlight::Search).empty());
        ensure_equals("nothing taken", flat(e.text()), std::string("one two|"));
        keys("x");
        ensure_equals("the next key a command of its own", flat(e.text()), std::string("ne two|"));
        keys("d/<BS>x");
        ensure_equals("a backspace on the empty line lets it go as well", flat(e.text()), std::string("e two|"));

        keys("yw");
        keys("d/zzz<CR>");
        ensure("not found, and said", vim->messageIsError() && vim->message().find("Pattern not found: zzz") != std::string::npos);
        ensure("the operator let go of", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
        ensure_equals("nothing taken", flat(e.text()), std::string("e two|"));
        ensure_equals("the register as it was", vim->registerText('"'), std::string("e "));
    }

    template<> template<>
    void alvimkeymap_object::test<133>()
    {
        set_test_name(". after d/foo looks for foo again, whatever was searched for since; a search an operator takes to a line's first column ends with the line before, and is lines from a line's start");
        ALCodeEditor& e = make("a foo b bar c foo d bar\n");
        keys("d/foo<CR>");
        ensure_equals("d/foo", flat(e.text()), std::string("foo b bar c foo d bar|"));
        keys("/bar<CR>.");
        ensure_equals(". up to the next foo, not the next bar", flat(e.text()), std::string("foo b foo d bar|"));

        make("ab\ncd\n");
        keys("d/cd<CR>");
        ensure_equals("from the line's start, the line whole", flat(editor->text()), std::string("cd|"));
        keys("p");
        ensure_equals("put back as a line", flat(editor->text()), std::string("cd|ab|"));

        make("ab\ncd\n");
        keys("ld/cd<CR>");
        ensure_equals("from inside it, to the line's end", flat(editor->text()), std::string("a|cd|"));
    }

    template<> template<>
    void alvimkeymap_object::test<134>()
    {
        set_test_name("a count typed before a search line goes with it, let go of by Escape or by a backspace on its empty line");
        ALCodeEditor& e = make("abcdef\n");
        keys("3/<Esc>");
        ensure("Escape: nothing pending", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
        keys("x");
        ensure_equals("x after it takes one character, not three", flat(e.text()), std::string("bcdef|"));
        keys("3/<BS>");
        ensure("a backspace on the empty line: nothing pending", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
        keys("x");
        ensure_equals("one character again", flat(e.text()), std::string("cdef|"));
    }

    template<> template<>
    void alvimkeymap_object::test<135>()
    {
        set_test_name("t and T to a character beside the caret stay where they are, so an operator takes the character under it; ; after them goes past it");
        ALCodeEditor& e = make("f(x)(y)\n(x)\na)b)c\n");
        e.setCaret(ALTextPos(0, 2));
        keys("dt)");
        ensure_equals("dt) beside a ): the x alone, not on to the next )", e.document().line(0), std::string("f()(y)"));
        e.setCaret(ALTextPos(1, 1));
        keys("ct)Z<Esc>");
        ensure_equals("ct) beside the line's only ): the x changed", e.document().line(1), std::string("(Z)"));
        e.setCaret(ALTextPos(2, 0));
        keys("t)");
        ensure_equals("t) beside a ): the caret stays", caretText(), std::string("2:0"));
        keys(";");
        ensure_equals("; after it goes on to before the next", caretText(), std::string("2:2"));
        keys("$T)");
        ensure_equals("T) beside a ): the caret stays", caretText(), std::string("2:4"));
        keys(";");
        ensure_equals("; after it goes back to after the one before", caretText(), std::string("2:2"));
    }

    template<> template<>
    void alvimkeymap_object::test<136>()
    {
        set_test_name(":put puts an empty line a register holds, alone or last of several, through the clipboard as well; only a register never set holds nothing");
        ALCodeEditor& e = make("a\n\nb\n");
        ex("put x");
        ensure("a register never set: E353", vim->messageIsError() && vim->message().find("E353") != std::string::npos);
        ensure_equals("and nothing put", flat(e.text()), std::string("a||b|"));
        keys("jyyk");
        ex("put");
        ensure("an empty line is something to put", !vim->messageIsError());
        ensure_equals(":put: the empty line below", flat(e.text()), std::string("a|||b|"));
        ensure_equals("the caret on it", caretText(), std::string("1:0"));
        ex("4put!");
        ensure_equals(":4put!: above the fourth line", flat(e.text()), std::string("a||||b|"));
        ensure_equals("the caret on it", caretText(), std::string("3:0"));
        keys("gg2yy");
        ex("$put");
        ensure_equals("two lines, the last empty: both put", flat(e.text()), std::string("a||||b|a||"));

        make("ab\n\nx\n");
        keys("<C-v>jy");
        ex("3put");
        ensure_equals("a block's empty last row is a line as well", flat(editor->text()), std::string("ab||x|a||"));

        make("a\n\nb\n");
        ex("set clipboard=unnamed");
        keys("jyyk");
        ex("put");
        ensure("by the clipboard, which an empty line leaves empty", !vim->messageIsError());
        ensure_equals("put all the same", flat(editor->text()), std::string("a|||b|"));
        ex("put +");
        ensure_equals("and by \"+", flat(editor->text()), std::string("a||||b|"));
    }

    template<> template<>
    void alvimkeymap_object::test<137>()
    {
        set_test_name("the black hole gives back nothing, never the clipboard: \"_p and \"_P put nothing and say nothing, :put _ an empty line, visual \"_p takes the selection out");
        ALCodeEditor& e = make("abc def\nx\n");
        const std::string outside("outside");
        LLClipboard::instance().copyToClipboard(outside, 0, static_cast<S32>(outside.size()));
        keys("wyiwgg");
        keys("l\"_p");
        ensure("nothing said", !vim->messageIsError());
        ensure_equals("\"_p puts nothing", flat(e.text()), std::string("abc def|x|"));
        ensure_equals("the caret where it was", caretText(), std::string("0:1"));
        keys("\"_P");
        ensure_equals("nor does \"_P", flat(e.text()), std::string("abc def|x|"));
        keys("\"_3p");
        ensure("nothing said of a count either", !vim->messageIsError());
        ensure_equals("nor does a count", flat(e.text()), std::string("abc def|x|"));
        ex("set clipboard=unnamed");
        keys("\"_gP");
        ensure_equals("nor with the clipboard the unnamed register", flat(e.text()), std::string("abc def|x|"));
        ensure_equals("the caret still where it was", caretText(), std::string("0:1"));
        ex("put _");
        ensure("nothing said by :put _", !vim->messageIsError());
        ensure_equals(":put _: an empty line", flat(e.text()), std::string("abc def||x|"));
        ensure_equals("the caret on it", caretText(), std::string("1:0"));
        keys("ggviw\"_p");
        ensure_equals("visual \"_p: the selection taken out, nothing put", flat(e.text()), std::string(" def||x|"));
        ensure_equals("the caret where it was", caretText(), std::string("0:0"));
    }

    template<> template<>
    void alvimkeymap_object::test<138>()
    {
        set_test_name("a count of aw or iw goes on across a line's end, the break no character of its own, and fails where the words run out; aw on a line's last blanks takes the next line's first word");
        ALCodeEditor& e = make("foo bar\nbaz qux\n");
        e.setCaret(ALTextPos(0, 4));
        keys("2daw");
        ensure_equals("2daw from a line's last word: the next line's first and its blank too", flat(e.text()), std::string("foo qux|"));
        ensure_equals("all it took kept", vim->registerText('"'), std::string("bar\nbaz "));
        keys("u");
        e.setCaret(ALTextPos(0, 4));
        keys("3daw");
        ensure_equals("3daw: the blank before, none being after", flat(e.text()), std::string("foo|"));
        keys("u");
        e.setCaret(ALTextPos(0, 4));
        keys("3diw");
        ensure_equals("3diw: the next line's first word and the blank after it", flat(e.text()), std::string("foo qux|"));
        keys("u");
        e.setCaret(ALTextPos(0, 4));
        keys("v3awy");
        ensure_equals("v3aw selects as much as 3daw takes", vim->registerText('"'), std::string(" bar\nbaz qux"));

        make("foo bar\nbaz");
        editor->setCaret(ALTextPos(0, 4));
        keys("2yaw");
        ensure_equals("2yaw to the text's last word", vim->registerText('"'), std::string(" bar\nbaz"));
        editor->setCaret(ALTextPos(0, 4));
        keys("3yaw");
        ensure_equals("3yaw runs out of words: nothing yanked", vim->registerText('"'), std::string(" bar\nbaz"));

        make("a.b c.d\ne.f g.h\n");
        editor->setCaret(ALTextPos(0, 4));
        keys("2daW");
        ensure_equals("2daW by WORDs", flat(editor->text()), std::string("a.b g.h|"));

        make("foo   \nbar baz\n");
        editor->setCaret(ALTextPos(0, 4));
        keys("daw");
        ensure_equals("daw on a line's last blanks: the next line's first word with them", flat(editor->text()), std::string("foo baz|"));

        make("bar\n\nbaz qux\n");
        keys("2diw");
        ensure_equals("2diw from a line's start over an empty line: the lines whole", flat(editor->text()), std::string("baz qux|"));
    }

    template<> template<>
    void alvimkeymap_object::test<139>()
    {
        set_test_name("aw on an empty line takes its break and the next line's first word, and fails on the last line; iw there is the empty line, or on the last goes back over the break");
        ALCodeEditor& e = make("foo bar\n\nbaz qux\n");
        e.setCaret(ALTextPos(1, 0));
        keys("daw");
        ensure_equals("daw on an empty line between lines of words", flat(e.text()), std::string("foo bar| qux|"));
        ensure_equals("what it took", vim->registerText('"'), std::string("\nbaz"));
        keys("u");
        e.setCaret(ALTextPos(1, 0));
        keys("vawy");
        ensure_equals("vaw selects as much", vim->registerText('"'), std::string("\nbaz"));
        e.setCaret(ALTextPos(1, 0));
        keys("yiw");
        ensure_equals("yiw there yanks nothing", vim->registerText('"'), std::string(""));
        keys("ciwX<Esc>");
        ensure_equals("ciw there inserts on the empty line", flat(e.text()), std::string("foo bar|X|baz qux|"));

        make("foo bar\n\n\nbaz qux\n");
        editor->setCaret(ALTextPos(1, 0));
        keys("2diw");
        ensure_equals("2diw from an empty line over another: the lines whole", flat(editor->text()), std::string("foo bar|baz qux|"));

        make("foo bar\n");
        editor->setCaret(ALTextPos(1, 0));
        keys("yiw");
        ensure_equals("yiw on the last line, empty: back to the line before's last character and the break", vim->registerText('"'), std::string("r\n"));
        editor->setCaret(ALTextPos(1, 0));
        keys("daw");
        ensure_equals("daw there fails", flat(editor->text()), std::string("foo bar|"));
        ensure_equals("and keeps nothing", vim->registerText('"'), std::string("r\n"));
        editor->setCaret(ALTextPos(1, 0));
        keys("diw");
        ensure_equals("diw there takes what yiw yanked", flat(editor->text()), std::string("foo ba"));
    }

    template<> template<>
    void alvimkeymap_object::test<140>()
    {
        set_test_name("a count before / or ? is the match that many on, round past the ends, its offset taken from that one; over a visual selection too");
        ALCodeEditor& e = make("a foo b foo c foo d foo\n");
        keys("3/foo<CR>");
        ensure_equals("3/foo the third match on", caretText(), std::string("0:14"));
        keys("$3?foo<CR>");
        ensure_equals("3?foo the third back", caretText(), std::string("0:8"));
        keys("02/foo/e<CR>");
        ensure_equals("2/foo/e the second match's last character", caretText(), std::string("0:10"));
        keys("06/foo<CR>");
        ensure_equals("6/foo round past the end to the second", caretText(), std::string("0:8"));
        keys("03n");
        ensure_equals("3n the third on, as before", caretText(), std::string("0:14"));
        ensure_equals("nothing changed", flat(e.text()), std::string("a foo b foo c foo d foo|"));

        make("x\nfoo\ny\nfoo\nz\nfoo\nw\n");
        keys("2/foo/+1<CR>");
        ensure_equals("2/foo/+1 the line after the second", caretText(), std::string("4:0"));
        keys("ggv2/foo<CR>d");
        ensure_equals("v2/foo takes the selection to the second", flat(editor->text()), std::string("oo|z|foo|w|"));
        ensure_equals("and d took it", vim->registerText('"'), std::string("x\nfoo\ny\nf"));
    }

    template<> template<>
    void alvimkeymap_object::test<141>()
    {
        set_test_name("* # g* g# after an operator are its motion, up to the word's next match and not into it, looked for from the word's start; n goes on with it and . looks again; # from inside a word goes to the one before");
        ALCodeEditor& e = make("foo bar foo baz\n");
        keys("d*");
        ensure_equals("d* up to the next foo", flat(e.text()), std::string("foo baz|"));
        ensure("normal mode, nothing pending", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
        ensure_equals("what it took in the register", vim->registerText('"'), std::string("foo bar "));

        make("foo bar foo baz\n");
        editor->setCaret(ALTextPos(0, 9));
        keys("d#");
        ensure_equals("d# from inside the second foo back to the first", flat(editor->text()), std::string("oo baz|"));
        ensure_equals("the first foo to the caret taken", vim->registerText('"'), std::string("foo bar f"));

        make("foo foobar foo\n");
        keys("dg*");
        ensure_equals("dg* the word anywhere: up to the foo in foobar", flat(editor->text()), std::string("foobar foo|"));
        make("foo foobar foo\n");
        editor->setCaret(ALTextPos(0, 11));
        keys("dg#");
        ensure_equals("dg# back to the foo in foobar", flat(editor->text()), std::string("foo foo|"));

        make("a x b x c x d\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("d2*");
        ensure_equals("d2* up to the second x on", flat(editor->text()), std::string("a x d|"));
        keys("u");
        editor->setCaret(ALTextPos(0, 2));
        keys("2d*");
        ensure_equals("and 2d*", flat(editor->text()), std::string("a x d|"));

        make("foo bar foo baz foo\n");
        keys("d*n");
        ensure_equals("n after it to the next foo", caretText(), std::string("0:8"));
        make("foo bar foo baz foo qux foo\n");
        keys("d*.");
        ensure_equals(". again from where it left", flat(editor->text()), std::string("foo qux foo|"));

        make("foo x\nfoo y\n");
        keys("d*");
        ensure_equals("to a line's first column from its start: the line whole", flat(editor->text()), std::string("foo y|"));

        make("foo bar baz\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("d*");
        ensure_equals("a word found nowhere else: round to its own start, behind the caret", flat(editor->text()), std::string("o bar baz|"));

        make("foo   \nbar\n");
        keys("yl");
        editor->setCaret(ALTextPos(0, 4));
        keys("d*");
        ensure("no word under the caret: the operator let go of", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
        ensure_equals("nothing taken", flat(editor->text()), std::string("foo   |bar|"));
        ensure_equals("the register as it was", vim->registerText('"'), std::string("f"));

        make("foo bar foo baz\n");
        editor->setCaret(ALTextPos(0, 9));
        keys("#");
        ensure_equals("# from inside a word to the one before it, not to its own start", caretText(), std::string("0:0"));
    }

    template<> template<>
    void alvimkeymap_object::test<142>()
    {
        set_test_name("a g key after an operator that is no motion fails it, nothing changed and nothing begun -- gi and gI put the caret where vim's do first; a g operator doubled is the line");
        ALCodeEditor& e = make("one two\nTHREE FOUR\nfive six\n");
        keys("jjllia<Esc>");
        e.setCaret(ALTextPos(1, 0));
        keys("yw");
        const std::string text = flat(e.text());
        ensure_equals("inserted on the third line", text, std::string("one two|THREE FOUR|fiave six|"));
        for (const char* typed : { "dgi", "cgi", "ygi", "2dgi", "dgI", "dgJ", "dgp", "dg;", "dgt", "dgv", "dgrx", "dgx", "dgc", "cgu" })
        {
            const std::string what(typed);
            e.setCaret(ALTextPos(1, 3));
            keys(typed);
            ensure(what + ": normal mode, nothing pending", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
            ensure_equals(what + ": nothing changed", flat(e.text()), text);
            ensure_equals(what + ": the register as it was", vim->registerText('"'), std::string("THREE "));
            const std::string caret = what.find("gi") != std::string::npos ? "2:3" : what == "dgI" ? "1:0" : "1:3";
            ensure_equals(what + ": the caret", caretText(), caret);
        }
        e.setCaret(ALTextPos(1, 3));
        keys("dguw");
        ensure_equals("dgu let go of, the w after it a motion of its own", caretText(), std::string("1:6"));
        ensure_equals("nothing lowered", flat(e.text()), text);
        keys("gugu");
        ensure_equals("gugu lowers the line", e.document().line(1), std::string("three four"));
        ensure_equals("the caret at its start", caretText(), std::string("1:0"));
        keys("gUgU");
        ensure_equals("gUgU raises it", e.document().line(1), std::string("THREE FOUR"));
        keys("g~g~");
        ensure_equals("g~g~ swaps its case", e.document().line(1), std::string("three four"));
    }

    template<> template<>
    void alvimkeymap_object::test<143>()
    {
        set_test_name("gv after an operator gives it the last visual area as it was selected, characters, lines or a block, and . does it over as much from the caret");
        ALCodeEditor& e = make("one two\nthree four\n");
        keys("vl<Esc>");
        e.setCaret(ALTextPos(1, 3));
        keys("dgv");
        ensure_equals("dgv deletes what was selected", flat(e.text()), std::string("e two|three four|"));
        ensure("normal mode, nothing pending", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
        ensure_equals("the caret where it began", caretText(), std::string("0:0"));
        ensure_equals("and the register", vim->registerText('"'), std::string("on"));

        make("one two\nthree four\n");
        keys("wvl<Esc>");
        editor->setCaret(ALTextPos(1, 0));
        keys("ygv");
        ensure_equals("ygv yanks it", vim->registerText('"'), std::string("tw"));
        ensure_equals("the caret to its start", caretText(), std::string("0:4"));
        keys("cgvX<Esc>");
        ensure_equals("cgv changes it", flat(editor->text()), std::string("one Xo|three four|"));
        ensure("back in normal mode", vim->mode() == ALVimKeymap::Mode::Normal);

        make("one two\nthree four\n");
        keys("Vj<Esc>");
        editor->setCaret(ALTextPos(0, 1));
        keys("gUgv");
        ensure_equals("gUgv over the lines a line-wise one took", flat(editor->text()), std::string("ONE TWO|THREE FOUR|"));
        ensure_equals("the caret at their start", caretText(), std::string("0:0"));

        make("abc def ghi\nxyz\n");
        keys("wvl<Esc>0dgv");
        ensure_equals("dgv from before the area", flat(editor->text()), std::string("abc f ghi|xyz|"));
        keys("0.");
        ensure_equals(". as much again from the caret", flat(editor->text()), std::string("c f ghi|xyz|"));
        ensure_equals("what . took", vim->registerText('"'), std::string("ab"));

        make("abcd\nefgh\nijkl\n");
        keys("lj<C-v>l<Esc>gg0dgv");
        ensure_equals("dgv over a block takes its columns", flat(editor->text()), std::string("abcd|eh|ijkl|"));
        ensure_equals("the caret at its corner", caretText(), std::string("1:1"));
        ensure_equals("the block's text", vim->registerText('"'), std::string("fg"));
    }

    template<> template<>
    void alvimkeymap_object::test<144>()
    {
        set_test_name("the last visual block keeps whether it was taken to every line's end with $, for {op}gv, gv and \\%V, whatever went to a line's end since");
        ALCodeEditor& e = make("abcd\nefgh\n");
        keys("l<C-v>jl<Esc>$dgv");
        ensure_equals("a block without $, then $: dgv takes its columns alone", flat(e.text()), std::string("ad|eh|"));
        e.setText("abcdefg\nabc\n");
        e.setCaret(ALTextPos(0, 0));
        keys("l<C-v>j$<Esc>0dgv");
        ensure_equals("a block with $, then 0: dgv takes every line to its end", flat(e.text()), std::string("a|a|"));
        e.setText("abcdefg\nabc\n");
        e.setCaret(ALTextPos(0, 0));
        keys("l<C-v>j$<Esc>0gvd");
        ensure_equals("gv selects it to every line's end again", flat(e.text()), std::string("a|a|"));
        e.setText("abcdefg\nabc\n");
        e.setCaret(ALTextPos(0, 0));
        keys("l<C-v>j$<Esc>0:%s/\\%V[a-z]/X/g<CR>");
        ensure_equals("\\%V over a block with $: to every line's end", flat(e.text()), std::string("aXXXXXX|aXX|"));
        e.setText("abcdefg\nabc\n");
        e.setCaret(ALTextPos(0, 0));
        keys("l<C-v>jl<Esc>$:%s/\\%V[a-z]/X/g<CR>");
        ensure_equals("\\%V over a block without $, after $: its columns alone", flat(e.text()), std::string("aXXdefg|aXX|"));
    }

    template<> template<>
    void alvimkeymap_object::test<145>()
    {
        set_test_name("visual p puts what it replaced in the unnamed register, and in - or 1, whichever register it put, so a further p puts that; visual P leaves the registers as they were");
        ALCodeEditor& e = make("one two three four\n");
        keys("3wyiwbviwp");
        ensure_equals("viwp: the word replaced", flat(e.text()), std::string("one two four four|"));
        ensure_equals("the caret on the last character put", caretText(), std::string("0:11"));
        ensure_equals("what it replaced in the unnamed register", vim->registerText('"'), std::string("three"));
        ensure_equals("and in -, as a small delete's", vim->registerText('-'), std::string("three"));
        ensure_equals("the yank kept in 0", vim->registerText('0'), std::string("four"));
        keys("0viwp");
        ensure_equals("a further p puts what the last replaced", flat(e.text()), std::string("three two four four|"));
        ensure_equals("and keeps what this one replaced", vim->registerText('"'), std::string("one"));

        make("one two three four\n");
        keys("3wyiwbviwP");
        ensure_equals("viwP: the word replaced", flat(editor->text()), std::string("one two four four|"));
        ensure_equals("the unnamed register as it was", vim->registerText('"'), std::string("four"));
        ensure_equals("and - too", vim->registerText('-'), std::string());
        keys("0viwP");
        ensure_equals("a further P puts the same again", flat(editor->text()), std::string("four two four four|"));
        ensure_equals("the caret on its last character", caretText(), std::string("0:3"));

        make("one two three four\n");
        keys("\"xyiw3wyiwbviw\"xp");
        ensure_equals("viw\"xp: the named register put", flat(editor->text()), std::string("one two one four|"));
        ensure_equals("what it replaced in the unnamed register all the same", vim->registerText('"'), std::string("three"));
        ensure_equals("the named one as it was", vim->registerText('x'), std::string("one"));
        keys("0viw\"_p");
        ensure_equals("viw\"_p: nothing put", flat(editor->text()), std::string(" two one four|"));
        ensure_equals("and what it took out in the unnamed register, as vim's has it", vim->registerText('"'), std::string("one"));

        make("a\nb\nc\n");
        keys("yyjVp");
        ensure_equals("V p: the line replaced", flat(editor->text()), std::string("a|a|c|"));
        ensure_equals("the line it replaced in the unnamed register", vim->registerText('"'), std::string("b"));
        ensure_equals("and in 1, as a delete of a line's", vim->registerText('1'), std::string("b"));
        keys("p");
        ensure_equals("p puts it, a line", flat(editor->text()), std::string("a|a|b|c|"));
        ensure_equals("the caret on it", caretText(), std::string("2:0"));

        make("abcd\nefgh\nX\n");
        keys("2jylggl<C-v>jlp");
        ensure_equals("a block's columns replaced", flat(editor->text()), std::string("aXd|eXh|X|"));
        ensure_equals("what the block held in the unnamed register, a line each", vim->registerText('"'), std::string("bc\nfg"));
        ensure_equals("and in 1", vim->registerText('1'), std::string("bc\nfg"));
    }

    template<> template<>
    void alvimkeymap_object::test<146>()
    {
        set_test_name("visual block p of one line leaves a line that stops short of the block as it is, and adds to one that reaches its first column");
        ALCodeEditor& e = make("abcd\na\nabcd\nZ\n");
        keys("3j\"xylgg2l<C-v>jj\"xp");
        ensure_equals("the short line not padded out", flat(e.text()), std::string("abZd|a|abZd|Z|"));
        ensure_equals("the caret at the block's corner", caretText(), std::string("0:2"));

        make("abcd\n\nabcd\nZ\n");
        keys("3j\"xylgg2l<C-v>jj\"xp");
        ensure_equals("nor an empty line", flat(editor->text()), std::string("abZd||abZd|Z|"));

        make("abcd\nab\nabcd\nZ\n");
        keys("3j\"xylgg2l<C-v>jj\"xp");
        ensure_equals("a line that ends at the block's first column has it added", flat(editor->text()), std::string("abZd|abZ|abZd|Z|"));

        make("abcd\na\nabcd\n");
        keys("gg2l<C-v>jj\"_p");
        ensure_equals("\"_p: the block taken out, the short line as it was", flat(editor->text()), std::string("abd|a|abd|"));
    }

    template<> template<>
    void alvimkeymap_object::test<147>()
    {
        set_test_name(":put and :put! leave the caret on the last line put, at its first non-blank");
        ALCodeEditor& e = make("a\n  q\n r\nb\n");
        keys("j\"x2yygg");
        ex("put x");
        ensure_equals(":put x: two lines under the first", flat(e.text()), std::string("a|  q| r|  q| r|b|"));
        ensure_equals("the caret on the second of them", caretText(), std::string("2:1"));
        ex("1put! x");
        ensure_equals(":1put! x: two lines over the first", flat(e.text()), std::string("  q| r|a|  q| r|  q| r|b|"));
        ensure_equals("the caret on the second of them too", caretText(), std::string("1:1"));

        make("1\n2\n3\n4\na\n\n");
        keys("4j\"x2yy");
        ex("3put x");
        ensure_equals("a line and an empty one under the third", flat(editor->text()), std::string("1|2|3|a||4|a||"));
        ensure_equals("the caret on the empty one", caretText(), std::string("4:0"));

        make("1\n2\n3\n4\n5\n");
        keys("gg2yy");
        ex("$put");
        ensure_equals("$put: under the last line", flat(editor->text()), std::string("1|2|3|4|5|1|2|"));
        ensure_equals("the caret on the last of them", caretText(), std::string("6:0"));
    }

    template<> template<>
    void alvimkeymap_object::test<148>()
    {
        set_test_name(":0put puts above the first line, under the line before it, as :.-1put does from the first line; under lines counted from 0, :0put is under the line shown as 0");
        ALCodeEditor& e = make("a\nb\n");
        keys("j\"xyy");
        ex("0put x");
        ensure_equals(":0put x: above the first line", flat(e.text()), std::string("b|a|b|"));
        ensure_equals("the caret on the line put", caretText(), std::string("0:0"));

        make("a\n\nb\n");
        keys("jyy");
        ex("0put");
        ensure_equals(":0put of an empty line: above the first", flat(editor->text()), std::string("|a||b|"));
        ensure_equals("the caret on it", caretText(), std::string("0:0"));

        make("a\nb\n");
        keys("j\"xyygg");
        ex("0put! x");
        ensure_equals(":0put! x: above the first line as well", flat(editor->text()), std::string("b|a|b|"));

        make("a\nb\n");
        keys("j\"xyygg");
        ex(".-1put x");
        ensure_equals(":.-1put x from the first line: above it", flat(editor->text()), std::string("b|a|b|"));

        make("a\nb\n");
        keys("j\"xyy");
        ex("0,1put x");
        ensure_equals(":0,1put x: under the range's last line", flat(editor->text()), std::string("a|b|b|"));
        ensure_equals("the caret on the line put", caretText(), std::string("1:0"));

        ALCodeEditor& zero = make("a\nb\n");
        zero.setLineNumberBase(-1);
        keys("j\"xyy");
        ex("0put x");
        ensure_equals("lines counted from 0: :0put x under the first, the line shown as 0", flat(zero.text()), std::string("a|b|b|"));
    }

    template<> template<>
    void alvimkeymap_object::test<149>()
    {
        set_test_name("a count typed before a register's name counts, multiplied by one typed after it, for p, yy, dd and an operator's motion");
        ALCodeEditor& e = make("ab\n");
        keys("\"ayl3\"ap");
        ensure_equals("3\"ap: put three times", flat(e.text()), std::string("aaaab|"));
        ensure_equals("the caret on the last put", caretText(), std::string("0:3"));

        make("ab\n");
        keys("\"ayl2\"a3");
        ensure_equals("both counts shown as they were typed", vim->status(), std::string("2\"a3"));
        keys("p");
        ensure_equals("2\"a3p: six times", flat(editor->text()), std::string("aaaaaaab|"));
        ensure_equals("the caret on the last put", caretText(), std::string("0:6"));

        make("1\n2\n3\n4\n5\n");
        keys("3\"ayy");
        ensure_equals("3\"ayy: three lines yanked into a", vim->registerText('a'), std::string("1\n2\n3"));
        keys("2\"add");
        ensure_equals("2\"add: two lines deleted", flat(editor->text()), std::string("3|4|5|"));
        ensure_equals("into a", vim->registerText('a'), std::string("1\n2"));

        make("1\n2\n3\n4\n5\n6\n7\n8\n");
        keys("2\"ad3d");
        ensure_equals("2\"ad3d: six lines deleted", flat(editor->text()), std::string("7|8|"));
        ensure_equals("into a", vim->registerText('a'), std::string("1\n2\n3\n4\n5\n6"));
    }

    template<> template<>
    void alvimkeymap_object::test<150>()
    {
        set_test_name(". with a count puts it in place of the counts on either side of a register's name; without one, . counts as they did");
        ALCodeEditor& e = make("ab\n");
        keys("\"ayl\"a3p4.");
        ensure_equals("\"a3p then 4.: four more", flat(e.text()), std::string("aaaaaaaab|"));

        make("ab\n");
        keys("\"ayl2\"a3p4.");
        ensure_equals("2\"a3p then 4.: four more, not twelve", flat(editor->text()), std::string("aaaaaaaaaaab|"));

        make("ab\n");
        keys("\"ayl2\"a3p.");
        ensure_equals("2\"a3p then .: six more", flat(editor->text()), std::string("aaaaaaaaaaaaab|"));
    }

    template<> template<>
    void alvimkeymap_object::test<151>()
    {
        set_test_name(":put! over a range puts above its last line, as :put puts under it");
        ALCodeEditor& e = make("1\n2\n3\n4\n");
        keys("\"xyy");
        ex("1,3put! x");
        ensure_equals(":1,3put! x: above the third line", flat(e.text()), std::string("1|2|1|3|4|"));
        ensure_equals("the caret on the line put", caretText(), std::string("2:0"));
        e.setText("1\n2\n3\n4\n");
        e.setCaret(ALTextPos(0, 0));
        ex("2,3put x");
        ensure_equals(":2,3put x: under the third line", flat(e.text()), std::string("1|2|3|1|4|"));
    }

    template<> template<>
    void alvimkeymap_object::test<152>()
    {
        set_test_name("a delete of characters over more than one line, nothing but blanks before it on its first and after it on its last, takes those lines whole; "
                      "not one that starts or ends inside a line, nor a change, a yank or a visual selection");
        ALCodeEditor& e = make("bar\nbaz");
        keys("2daw");
        ensure("2daw over a line's word and the next line's: lines", vim->shared().registers.fetch('"', false).linewise);
        ensure_equals("what it took", vim->registerText('"'), std::string("bar\nbaz"));
        keys("p");
        ensure_equals("put back as lines", flat(e.text()), std::string("|bar|baz"));

        make("foo bar\n\nbaz");
        editor->setCaret(ALTextPos(1, 0));
        keys("daw");
        ensure_equals("daw on an empty line before a line's only word: both lines", flat(editor->text()), std::string("foo bar"));
        ensure("kept as lines", vim->shared().registers.fetch('"', false).linewise);

        make("foo\nbar\nx\n");
        keys("d2e");
        ensure_equals("d2e from a line's start to the next line's end", flat(editor->text()), std::string("x|"));
        make("foo\nbar\nx\n");
        keys("2D");
        ensure_equals("2D from a line's start", flat(editor->text()), std::string("x|"));
        make("foo\nbar\nx\n");
        keys("d/r/e<CR>");
        ensure_equals("d/r/e to the next line's last character", flat(editor->text()), std::string("x|"));
        make("foo\nbar");
        keys("d}p");
        ensure_equals("d} over the last paragraph, put back as lines", flat(editor->text()), std::string("|foo|bar"));
        make("(\nx)\ny\n");
        keys("d%");
        ensure_equals("d% from a bracket that begins its line", flat(editor->text()), std::string("y|"));
        make("  (\nx)  \ny\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("d%");
        ensure_equals("from inside the indent, to blanks after", flat(editor->text()), std::string("y|"));
        ensure_equals("the lines whole in the register", vim->registerText('"'), std::string("  (\nx)  "));

        make("foo\nbar\nx\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("d2e");
        ensure_equals("from after a line's first non-blank: characters", flat(editor->text()), std::string("f|x|"));
        make("(\nx) z\ny\n");
        keys("d%");
        ensure_equals("to before a line's last non-blank: characters", flat(editor->text()), std::string(" z|y|"));
        make("(\nx)\ny\n");
        keys("c%Z<Esc>");
        ensure_equals("c%: characters", flat(editor->text()), std::string("Z|y|"));
        make("(\nx)\ny\n");
        keys("y%");
        ensure("y%: characters", !vim->shared().registers.fetch('"', false).linewise);
        keys("v%d");
        ensure_equals("v%d: characters", flat(editor->text()), std::string("|y|"));
        make("foo\nbar\nx\n");
        keys("/foo\\nbar<CR>ggdgn");
        ensure_equals("dgn over a match of two lines: characters", flat(editor->text()), std::string("|x|"));
    }

    template<> template<>
    void alvimkeymap_object::test<153>()
    {
        set_test_name("c over an empty line's end taken inclusively -- ciw, C, c$ there -- keeps it, an empty register, in - as well; c0 there and ci( between two brackets keep nothing");
        ALCodeEditor& e = make("abc\n\nxyz\n");
        keys("xyiw");
        e.setCaret(ALTextPos(1, 0));
        keys("ciwX<Esc>");
        ensure_equals("ciw on an empty line changes it", flat(e.text()), std::string("bc|X|xyz|"));
        ensure_equals("and keeps its end, no text", vim->registerText('"'), std::string());
        ensure_equals("in - as well", vim->registerText('-'), std::string());
        ensure_equals("0 still the yank", vim->registerText('0'), std::string("bc"));

        make("abc\n\nxyz\n");
        keys("yiw");
        editor->setCaret(ALTextPos(1, 0));
        keys("CX<Esc>");
        ensure_equals("C on an empty line changes it", flat(editor->text()), std::string("abc|X|xyz|"));
        ensure_equals("and keeps its end", vim->registerText('"'), std::string());

        make("abc\n\nxyz\n");
        keys("yiw");
        editor->setCaret(ALTextPos(1, 0));
        keys("c$X<Esc>");
        ensure_equals("c$ there keeps it too", vim->registerText('"'), std::string());

        make("abc\n\nxyz\n");
        keys("yiw");
        editor->setCaret(ALTextPos(1, 0));
        keys("\"aciWX<Esc>");
        ensure_equals("ciW into a register named: the unnamed one says it", vim->registerText('"'), std::string());
        ensure_equals("the yank still in 0", vim->registerText('0'), std::string("abc"));

        make("abc\n\nxyz\n");
        keys("yiw");
        editor->setCaret(ALTextPos(1, 0));
        keys("c0X<Esc>");
        ensure_equals("c0 there changes it", flat(editor->text()), std::string("abc|X|xyz|"));
        ensure_equals("and keeps nothing", vim->registerText('"'), std::string("abc"));

        make("f()\n");
        keys("yiwf(ci(X<Esc>");
        ensure_equals("ci( between two brackets", flat(editor->text()), std::string("f(X)|"));
        ensure_equals("keeps nothing", vim->registerText('"'), std::string("f"));
    }

    template<> template<>
    void alvimkeymap_object::test<154>()
    {
        set_test_name("a delete over a search, n and N, * and #, %, { and } or ` to a mark goes in 1 however little it takes, those before it moving along, "
                      "and in - as well; one over w goes in - alone");
        ALCodeEditor& e = make("a foo b foo c\n");
        keys("d/foo<CR>");
        ensure_equals("d/foo within a line: in 1", vim->registerText('1'), std::string("a "));
        ensure_equals("and in -", vim->registerText('-'), std::string("a "));
        keys("dn");
        ensure_equals("what is left", flat(e.text()), std::string("foo c|"));
        ensure_equals("dn: in 1", vim->registerText('1'), std::string("foo b "));
        ensure_equals("the one before moved along to 2", vim->registerText('2'), std::string("a "));

        make("f(x) y\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("d%");
        ensure_equals("d%", vim->registerText('1'), std::string("(x)"));

        make("foo bar\n");
        editor->setCaret(ALTextPos(0, 4));
        keys("d{");
        ensure_equals("d{", vim->registerText('1'), std::string("foo "));

        make("foo bar\n");
        keys("4|ma0d`a");
        ensure_equals("d`a", vim->registerText('1'), std::string("foo"));

        make("foo bar foo\n");
        keys("d*");
        ensure_equals("d*", vim->registerText('1'), std::string("foo bar "));

        make("a foo\n");
        keys("c/foo<CR>X<Esc>");
        ensure_equals("c/foo as well", vim->registerText('1'), std::string("a "));

        make("foo bar\n");
        keys("dw");
        ensure_equals("dw: in -", vim->registerText('-'), std::string("foo "));
        ensure_equals("and not in 1", vim->registerText('1'), std::string());
    }

    template<> template<>
    void alvimkeymap_object::test<155>()
    {
        set_test_name("gUU, guu, g~~ and gUgU with a count leave the caret where it was; without one, on the line's first non-blank, or where it was before it");
        ALCodeEditor& e = make("abc def\nghi jkl\nmno\n");
        e.setCaret(ALTextPos(0, 3));
        keys("2gUgU");
        ensure_equals("2gUgU raises two lines", flat(e.text()), std::string("ABC DEF|GHI JKL|mno|"));
        ensure_equals("the caret where it was", caretText(), std::string("0:3"));
        keys("gu2gu");
        ensure_equals("gu2gu lowers them again", flat(e.text()), std::string("abc def|ghi jkl|mno|"));
        ensure_equals("the caret still where it was", caretText(), std::string("0:3"));
        keys("2g~~");
        ensure_equals("2g~~ swaps them", flat(e.text()), std::string("ABC DEF|GHI JKL|mno|"));
        ensure_equals("and leaves it too", caretText(), std::string("0:3"));
        e.setCaret(ALTextPos(1, 5));
        keys("2guu");
        ensure_equals("2guu from the second line", flat(e.text()), std::string("ABC DEF|ghi jkl|mno|"));
        ensure_equals("where it was", caretText(), std::string("1:5"));

        make("   abc def\nghi\n");
        editor->setCaret(ALTextPos(0, 5));
        keys("gUU");
        ensure_equals("gUU from past the indent", editor->document().line(0), std::string("   ABC DEF"));
        ensure_equals("the caret to the line's first non-blank", caretText(), std::string("0:3"));
        editor->setCaret(ALTextPos(0, 1));
        keys("guu");
        ensure_equals("guu from inside the indent", editor->document().line(0), std::string("   abc def"));
        ensure_equals("the caret where it was", caretText(), std::string("0:1"));
        editor->setCaret(ALTextPos(0, 7));
        keys("g~g~");
        ensure_equals("g~g~", editor->document().line(0), std::string("   ABC DEF"));
        ensure_equals("the caret to the first non-blank", caretText(), std::string("0:3"));
    }

    template<> template<>
    void alvimkeymap_object::test<156>()
    {
        set_test_name("* # g* g# on a blank take the word after it on the line, and where no word follows the other characters there, as they are; a word is one by vim's classes");
        ALCodeEditor& e = make("foo bar baz\n");
        e.setCaret(ALTextPos(0, 3));
        keys("d*");
        ensure_equals("d* on the blank up to the bar after it, found again round the end", flat(e.text()), std::string("foobar baz|"));
        ensure_equals("the blank taken", vim->registerText('"'), std::string(" "));

        make("foo bar baz bar\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("*");
        ensure_equals("* on the blank to the next whole bar", caretText(), std::string("0:12"));
        keys("n");
        ensure_equals("n goes on with it", caretText(), std::string("0:4"));

        make("bar foo bar baz\n");
        editor->setCaret(ALTextPos(0, 7));
        keys("d#");
        ensure_equals("d# on the blank back to the bar before the next one", flat(editor->text()), std::string(" bar baz|"));
        ensure_equals("what d# took", vim->registerText('"'), std::string("bar foo"));

        make("foo bar baz barx\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("dg*");
        ensure_equals("dg* on the blank up to the bar in barx", flat(editor->text()), std::string("foobarx|"));

        make("x+=1\na +=\n");
        editor->setCaret(ALTextPos(1, 2));
        keys("*");
        ensure_equals("no word after the caret: += looked for as it is, round the end", caretText(), std::string("0:1"));
        editor->setCaret(ALTextPos(1, 3));
        keys("*");
        ensure_equals("from the middle of the run, the run whole", caretText(), std::string("0:1"));
        editor->setCaret(ALTextPos(1, 2));
        keys("d*");
        ensure_equals("d* back round to the += before it", flat(editor->text()), std::string("x+=|"));

        make("x.*1\na .*\n");
        editor->setCaret(ALTextPos(1, 2));
        keys("*");
        ensure_equals(".* looked for as itself, not as a pattern", caretText(), std::string("0:1"));

        make("foo.bar foo\n");
        keys("*");
        ensure_equals("the word is foo, as vim's classes have it, not foo.bar", caretText(), std::string("0:8"));
    }

    template<> template<>
    void alvimkeymap_object::test<157>()
    {
        set_test_name("g0 g^ gm g$ gM and go are motions, alone and after an operator: the screen line's first character, its first not blank, its middle and its last, the middle of the line's text, and a byte of the text");
        ALCodeEditor& e = make("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("dg0");
        ensure_equals("dg0 back to the screen line's start, exclusive", flat(e.text()), std::string("ee four|"));
        ensure("normal mode, nothing pending", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
        e.setText("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("dg$");
        ensure_equals("dg$ through its last character", flat(e.text()), std::string("thr|"));
        e.setText("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("dgm");
        ensure_equals("dgm up to the last character, the view's middle being past it", flat(e.text()), std::string("thrr|"));
        e.setText("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("dgo");
        ensure_equals("dgo back to the text's first byte", flat(e.text()), std::string("ee four|"));
        e.setText("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("dgM");
        ensure_equals("dgM up to the middle of the line's text", flat(e.text()), std::string("thr four|"));
        ensure_equals("what dgM took", vim->registerText('"'), std::string("ee"));

        e.setText("three four\n");
        e.setCaret(ALTextPos(0, 3));
        keys("g$");
        ensure_equals("g$ to the last character", caretText(), std::string("0:9"));
        keys("g0");
        ensure_equals("g0 to the first", caretText(), std::string("0:0"));
        keys("gm");
        ensure_equals("gm to the last, the view's middle being past it", caretText(), std::string("0:9"));
        keys("gM");
        ensure_equals("gM to the middle of the text", caretText(), std::string("0:5"));

        e.setText("  three four\n");
        e.setCaret(ALTextPos(0, 7));
        keys("g^");
        ensure_equals("g^ to the first not blank", caretText(), std::string("0:2"));
        e.setCaret(ALTextPos(0, 7));
        keys("dg^");
        ensure_equals("dg^ back to it", flat(e.text()), std::string("   four|"));

        e.setText("0123456789\n");
        e.setCaret(ALTextPos(0, 0));
        keys("20gM");
        ensure_equals("20gM a fifth along", caretText(), std::string("0:2"));
        keys("0101gM");
        ensure_equals("past a hundred, the middle", caretText(), std::string("0:5"));
        keys("02d10gM");
        ensure_equals("2d10gM: the counts' product, a fifth along", flat(e.text()), std::string("23456789|"));

        e.setText("abc\ndef\nghi");
        e.setCaret(ALTextPos(2, 1));
        keys("go");
        ensure_equals("go to the first byte", caretText(), std::string("0:0"));
        keys("``");
        ensure_equals("a jump, `` going back", caretText(), std::string("2:1"));
        keys("6go");
        ensure_equals("6go, a line's break one byte", caretText(), std::string("1:1"));
        keys("4go");
        ensure_equals("a break's byte, the character before it", caretText(), std::string("0:2"));
        keys("99go");
        ensure_equals("past the end, the last character", caretText(), std::string("2:2"));
        e.setCaret(ALTextPos(0, 1));
        keys("d6go");
        ensure_equals("d6go over the break", flat(e.text()), std::string("aef|ghi"));
        e.setText("abc\ndef\nghi");
        e.setCaret(ALTextPos(1, 2));
        keys("2d3go");
        ensure_equals("2d3go: the counts' product", flat(e.text()), std::string("abc|df|ghi"));

        // A count of g$ is screen lines down; unwrapped, each line one, and
        // the last line where they run out.
        e.setText("one\ntwo two\nthree");
        e.setCaret(ALTextPos(0, 1));
        keys("2g$");
        ensure_equals("2g$ to the next line's last", caretText(), std::string("1:6"));
        e.setCaret(ALTextPos(0, 1));
        keys("9g$");
        ensure_equals("9g$ to the last line's", caretText(), std::string("2:4"));
        e.setCaret(ALTextPos(0, 1));
        keys("d2g$");
        ensure_equals("d2g$ through the next line's last", flat(e.text()), std::string("o|three"));

        e.setText("ab\n\ncd");
        e.setCaret(ALTextPos(1, 0));
        keys("dg$");
        ensure_equals("dg$ on an empty line takes nothing, the break neither", flat(e.text()), std::string("ab||cd"));

        // Unwrapped and wider than the view: what is in sight.
        const std::string wide(150, 'x');
        e.setText(wide + "\n");
        e.setCaret(ALTextPos(0, 0));
        const F32 width = static_cast<F32>(e.textRect().getWidth());
        keys("g$");
        ensure("g$ to the last character in sight", e.caret().column + 1 < static_cast<S32>(wide.size()) && e.layout().xOf(0, e.caret().column) <= width - 1.f &&
                                                    e.layout().xOf(0, e.caret().column + 1) > width - 1.f);
        e.setCaret(ALTextPos(0, 0));
        keys("gm");
        ensure("gm to the one half the view across", e.layout().xOf(0, e.caret().column) <= width / 2.f && e.layout().xOf(0, e.caret().column + 1) > width / 2.f);
        keys("$");
        const F32 left = e.scrollX();
        keys("g0");
        ensure("g0 to the first character in sight", left > 0.f && e.layout().xOf(0, e.caret().column) <= left && e.layout().xOf(0, e.caret().column + 1) > left);

        // Wrapped, a screen line is a row of the line.
        const std::string line = "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll";
        e.setText(line + "\n");
        e.setWordWrap(true);
        e.reshape(120, 200);
        const std::vector<ALTextLayout::Row> rows = e.layout().line(0).rows;
        ensure("the line wraps to three rows or more, of three characters or more", rows.size() >= 3 && rows[1].end - rows[1].begin >= 3);
        e.setCaret(ALTextPos(0, rows[1].begin + 2));
        keys("g0");
        ensure("g0 to the row's first character", e.caret() == ALTextPos(0, rows[1].begin));
        keys("g$");
        ensure("g$ to its last", e.caret() == ALTextPos(0, rows[1].end - 1));
        keys("gm");
        ensure("gm on the row", e.layout().rowOf(0, e.caret().column) == 1);
        e.setCaret(ALTextPos(0, rows[1].begin + 2));
        keys("2g$");
        ensure("2g$ to the end of the row below", e.caret() == ALTextPos(0, rows[2].end - 1));
        e.setCaret(ALTextPos(0, rows[1].begin + 2));
        keys("d99g$");
        ensure_equals("d99g$ runs out of rows and fails, nothing taken", e.document().line(0), line);
        e.setCaret(ALTextPos(0, rows[1].begin + 2));
        keys("dg0");
        const size_t row_begin = static_cast<size_t>(rows[1].begin);
        const size_t row_end   = static_cast<size_t>(rows[1].end);
        ensure_equals("dg0 from the row's start", e.document().line(0), line.substr(0, row_begin) + line.substr(row_begin + 2));
        e.setText(line + "\n");
        e.setCaret(ALTextPos(0, rows[1].begin + 1));
        keys("dg$");
        ensure_equals("dg$ through the row's last", e.document().line(0), line.substr(0, row_begin + 1) + line.substr(row_end));
    }

    template<> template<>
    void alvimkeymap_object::test<158>()
    {
        set_test_name("* # g* g# put what they look for in the search history, a whole word as \\<word\\>, and . of an operator over one puts what it finds then; . of d/foo puts foo last, once");
        typedef std::vector<std::string> history_t;
        make("foo b bar c foo d bar e foo f foo\n");
        keys("d*");
        ensure("d* puts \\<foo\\> in the search history", vim->shared().search == history_t{ "\\<foo\\>" });
        keys("/bar<CR>");
        keys(".");
        ensure("and . of it the word under the caret then", vim->shared().search == history_t{ "\\<foo\\>", "bar", "\\<bar\\>" });

        make("foo foobar\n");
        keys("g*");
        ensure("g* the word as it is", vim->shared().search == history_t{ "foo" });
        keys("0*");
        ensure("* the word whole, after it", vim->shared().search == history_t{ "foo", "\\<foo\\>" });

        make("x.*1\na .*\n");
        editor->setCaret(ALTextPos(1, 2));
        keys("*");
        ensure("other characters as they are looked for", vim->shared().search == history_t{ "\\.\\*" });

        make("foo x foo\n");
        keys("**#");
        ensure("the same again is there once", vim->shared().search == history_t{ "\\<foo\\>" });

        make("a foo b bar c foo d bar e foo\n");
        keys("d/foo<CR>/bar<CR>.");
        ensure_equals(". of d/foo up to the next foo", flat(editor->text()), std::string("foo b foo d bar e foo|"));
        ensure("foo moved last in the history, and there once", vim->shared().search == history_t{ "bar", "foo" });
    }

    template<> template<>
    void alvimkeymap_object::test<159>()
    {
        set_test_name("a charwise selection that ends past a line's last character takes the line's break, and . counts it as a character; an operator over lines takes none");
        ALCodeEditor& e = make("one\n\ntwo\n");
        e.setCaret(ALTextPos(1, 0));
        keys("vd");
        ensure_equals("v d on an empty line takes the line", flat(e.text()), std::string("one|two|"));
        ensure_equals("its break in the register", vim->registerText('"'), std::string("\n"));
        ensure_equals("the caret where it began", caretText(), std::string("1:0"));

        make("foo\n\nbar\n");
        editor->setCaret(ALTextPos(1, 0));
        keys("viwd");
        ensure_equals("viw d there too", flat(editor->text()), std::string("foo|bar|"));

        make("abc\n\ndef\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("vjd");
        ensure_equals("v j onto an empty line: through its break", flat(editor->text()), std::string("adef|"));
        ensure_equals("what it took", vim->registerText('"'), std::string("bc\n\n"));

        make("abc\nq\nxyz\nr\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("vly");
        ensure_equals("l past the last character: y takes the break", vim->registerText('"'), std::string("c\n"));
        keys("vld");
        ensure_equals("and d joins the next line on", flat(editor->text()), std::string("abq|xyz|r|"));
        keys("j0.");
        ensure_equals(". as many again, the break counted as a character", flat(editor->text()), std::string("abq|z|r|"));
        ensure_equals("what . took", vim->registerText('"'), std::string("xy"));

        make("a\n\nb\nxyz\n");
        editor->setCaret(ALTextPos(1, 0));
        keys("vdj0.");
        ensure_equals(". after an empty line's break: one character", flat(editor->text()), std::string("a|b|yz|"));

        make("abcdef\nab\nz\n");
        keys("vlldj0.");
        ensure_equals(". over a line shorter than it: past its last, its break too", flat(editor->text()), std::string("def|z|"));

        make("abc\ndef\nghi\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("vl~");
        ensure_equals("~ over the break: the text recased, nothing joined", flat(editor->text()), std::string("abC|def|ghi|"));
        keys("vlJ");
        ensure_equals("J over lines takes no break: two lines joined, not three", flat(editor->text()), std::string("abC def|ghi|"));
    }

    template<> template<>
    void alvimkeymap_object::test<160>()
    {
        set_test_name("$ in a visual mode goes past the line's last character, onto its break: v$ y, d, c and p take the line through it, . goes to whatever end a line has, and h steps back onto the last");
        ALCodeEditor& e = make("hree four\nfive six\n");
        keys("v$y");
        ensure_equals("v$y yanks the line and its break", vim->registerText('"'), std::string("hree four\n"));
        keys("v$d");
        ensure_equals("v$d joins the next line on", flat(e.text()), std::string("five six|"));

        make("hree four\nfive six\n");
        keys("v$cX<Esc>");
        ensure_equals("v$c changes the line and its break", flat(editor->text()), std::string("Xfive six|"));

        make("X\nhree four\nfive six\n");
        keys("\"aylj0v$\"ap");
        ensure_equals("v$p puts in place of the line and its break", flat(editor->text()), std::string("X|Xfive six|"));

        make("abc\ndef\n");
        keys("v$hd");
        ensure_equals("h from past the end onto the last character, which takes no break", flat(editor->text()), std::string("|def|"));

        make("ab\ncdefgh\nq\n");
        keys("v$d.");
        ensure_equals(". to the end of a longer line, and its break", flat(editor->text()), std::string("q|"));

        make("longer\nab\n");
        editor->setCaret(ALTextPos(0, 5));
        keys("<C-v>j$d");
        ensure_equals("a block taken with $ onto a shorter line: from past that line's end", flat(editor->text()), std::string("lo|ab|"));
    }

    template<> template<>
    void alvimkeymap_object::test<161>()
    {
        set_test_name("a text object over a visual selection of more than one character takes it on: words from the caret, forward or back; brackets, tags and quotes out to the next; paragraphs on from the caret's line");
        const char* words = "one two three four\nfive six\n";
        // Each a selection made from a place, then yanked: what it covers.
        const auto selects = [&](const char* text, S32 line, S32 column, const char* typed) {
            make(text);
            editor->setCaret(ALTextPos(line, column));
            keys(typed);
            keys("y");
            return vim->registerText('"');
        };
        ensure_equals("viw iw: on by the blank after", selects(words, 0, 4, "viwiw"), std::string("two "));
        ensure_equals("viw iw iw: and the next word", selects(words, 0, 4, "viwiwiw"), std::string("two three"));
        ensure_equals("vaw aw: a word and its blank more", selects(words, 0, 4, "vawaw"), std::string("two three "));
        ensure_equals("vh iw: back from a caret before the anchor, over the blank", selects(words, 0, 5, "vhiw"), std::string(" tw"));
        ensure_equals("vh iw iw: and the word before", selects(words, 0, 5, "vhiwiw"), std::string("one tw"));
        ensure_equals("vh aw aw: back by words and their blanks", selects(words, 0, 9, "vhawaw"), std::string("one two th"));
        ensure_equals("vj iw: from the caret on the next line", selects(words, 0, 4, "vjiw"), std::string("two three four\nfive six"));
        ensure_equals("vk aw: back from the caret on the line above", selects(words, 1, 5, "vkaw"), std::string(" two three four\nfive s"));
        ensure_equals("viw iw at a line's last word: the next line's first", selects(words, 0, 14, "viwiw"), std::string("four\nfive"));
        ensure_equals("vh iw iw from a line's start: back over the break", selects(words, 1, 1, "vhiwiw"), std::string(" four\nfi"));
        ensure_equals("vh iw at the text's start: nothing to take, the selection as it was", selects(words, 0, 1, "vhiw"), std::string("on"));
        make(words);
        editor->setCaret(ALTextPos(0, 4));
        keys("Vjiw");
        ensure("V j iw: lines become characters", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("y");
        ensure_equals("from the caret on", vim->registerText('"'), std::string("two three four\nfive six"));

        const char* brackets = "f(a (b c) d) (e)\n";
        ensure_equals("vi( i(: inside the block around", selects(brackets, 0, 5, "vi(i("), std::string("a (b c) d"));
        ensure_equals("va( a(: the block around", selects(brackets, 0, 5, "va(a("), std::string("(a (b c) d)"));
        ensure_equals("vl i(: inside, more than was selected", selects(brackets, 0, 5, "vli("), std::string("b c"));
        ensure_equals("vl from a ( i(: inside the block around it", selects(brackets, 0, 4, "vli("), std::string("a (b c) d"));
        ensure_equals("vi( a(: the block whole", selects(brackets, 0, 5, "vi(a("), std::string("(b c)"));
        ensure_equals("vi( i( i(: no block further out, the selection as it was", selects(brackets, 0, 5, "vi(i(i("), std::string("a (b c) d"));
        ensure_equals("i( over an empty block: nothing", selects("x () y\n", 0, 1, "vlli("), std::string(" ()"));

        const char* tags = "<a><b>x y</b> z</a>\n";
        ensure_equals("vit it: the tags as well", selects(tags, 0, 7, "vitit"), std::string("<b>x y</b>"));
        ensure_equals("vit it it: inside the tag around", selects(tags, 0, 7, "vititit"), std::string("<b>x y</b> z"));
        ensure_equals("vat at: the tag around", selects(tags, 0, 7, "vatat"), std::string("<a><b>x y</b> z</a>"));
        ensure_equals("vl it: inside, more than was selected", selects(tags, 0, 6, "vlit"), std::string("x y"));
        ensure_equals("it over a selection past the tag's close: the tag around", selects(tags, 0, 8, "vllllllit"), std::string("<b>x y</b> z"));

        const char* quotes = "x \"ab cd\" y \"ef\" z\n";
        ensure_equals("vi\" i\": the quotes too", selects(quotes, 0, 4, "vi\"i\""), std::string("\"ab cd\""));
        ensure_equals("vi\" i\" i\": on to the next quoted text", selects(quotes, 0, 4, "vi\"i\"i\""), std::string("\"ab cd\" y \"ef"));
        ensure_equals("va\" a\": on to the next, its blank after", selects(quotes, 0, 4, "va\"a\""), std::string("\"ab cd\" y \"ef\" "));
        ensure_equals("vl i\": inside the quotes", selects(quotes, 0, 4, "vli\""), std::string("ab cd"));
        ensure_equals("vh i\": the same, the caret before the anchor", selects(quotes, 0, 5, "vhi\""), std::string("ab cd"));
        ensure_equals("i\" over a selection past a closing quote: to the next quoted text", selects(quotes, 0, 4, "vllllllllli\""), std::string("b cd\" y \"ef"));
        ensure_equals("i\" over two lines: nothing", selects("\"a\" b\n\"c\" d\n", 0, 4, "vji\""), std::string("b\n\"c\" d"));

        const char* paragraphs = "a\nb\n\nc\n\n\nd\ne\n";
        ensure_equals("vip ip: the blank line after", selects(paragraphs, 0, 0, "vipip"), std::string("a\nb\n"));
        ensure_equals("vip ip ip: and the paragraph after", selects(paragraphs, 0, 0, "vipipip"), std::string("a\nb\n\nc"));
        ensure_equals("vap ap: a paragraph and its blank lines more", selects(paragraphs, 0, 0, "vapap"), std::string("a\nb\n\nc\n\n"));
        ensure_equals("V ip on a paragraph's first line: on from it, past a paragraph of one", selects(paragraphs, 3, 0, "Vip"), std::string("c\n\n"));
        ensure_equals("vj ip: by characters, to the blank line's start", selects(paragraphs, 0, 0, "vjip"), std::string("a\nb\n\n"));
        ensure_equals("vk ip: back over the blank lines", selects(paragraphs, 7, 0, "vkip"), std::string("\n\nd\ne"));
        ensure_equals("Vj ap: on by the blank lines and the paragraph after", selects("a\nx\n\nb\nc\n\nd\n", 0, 0, "Vjap"), std::string("a\nx\n\nb\nc"));
        ensure_equals("a count past the text's end: as far as it goes", selects("a\nb\n\nc\n", 0, 0, "vj5ip"), std::string("a\nb\n\nc\n"));
    }

    template<> template<>
    void alvimkeymap_object::test<162>()
    {
        set_test_name("a text object that is not there fails: a macro stops at it, the operator and the count are let go of, and . does the change before it again");
        ALCodeEditor& e = make("abc\ndef\nghi\njkl\n");
        keys("qaxdi(jq");
        ensure_equals("recorded: x done, di( found nothing, j done", flat(e.text()), std::string("bc|def|ghi|jkl|"));
        ensure_equals("the caret on the next line", caretText(), std::string("1:0"));
        keys("@a");
        ensure_equals("@a: x, then di( fails and the j after it is not done", flat(e.text()), std::string("bc|ef|ghi|jkl|"));
        ensure_equals("the caret still on the line", caretText(), std::string("1:0"));
        keys("3@a");
        ensure_equals("3@a: the first play's di( stops the count", flat(e.text()), std::string("bc|f|ghi|jkl|"));
        ensure_equals("the caret still there", caretText(), std::string("1:0"));

        make("abc\ndef\nghi\n");
        keys("qbvi(<Esc>xjq");
        ensure_equals("recorded over a selection", flat(editor->text()), std::string("bc|def|ghi|"));
        keys("@b");
        ensure_equals("@b: vi( fails, nothing after it done", flat(editor->text()), std::string("bc|def|ghi|"));
        ensure("the selection still there", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("y");
        ensure_equals("as it was", vim->registerText('"'), std::string("d"));

        make("abcdef\n");
        keys("xdi(.");
        ensure_equals(". after it does the x again", flat(editor->text()), std::string("cdef|"));
        keys("2di(x");
        ensure_equals("the count let go of with it", flat(editor->text()), std::string("def|"));
        keys("di(w");
        ensure_equals("and the operator: w a motion of its own", flat(editor->text()), std::string("def|"));
    }

    template<> template<>
    void alvimkeymap_object::test<163>()
    {
        set_test_name(". after c over a visual area -- cgv, or c over a selection -- changes as much again from the caret with what was typed, as . after any visual operation does");
        ALCodeEditor& e = make("one two\nthree four\n");
        keys("wvl<Esc>cgvXY<Esc>");
        ensure_equals("cgv changes the last visual area", flat(e.text()), std::string("one XYo|three four|"));
        keys("0.");
        ensure_equals(". as many characters again from the caret, the same text typed", flat(e.text()), std::string("XYe XYo|three four|"));
        ensure_equals("what . took", vim->registerText('"'), std::string("on"));

        make("a\nb\nc\nd\ne\n");
        keys("Vj<Esc>jjcgvX<Esc>j.");
        ensure_equals("cgv over lines, then . over as many lines", flat(editor->text()), std::string("X|X|e|"));

        make("ab cdef gh\n");
        keys("viwcX<Esc>w.");
        ensure_equals("viw c, then . over as many characters as the word had, not the word there", flat(editor->text()), std::string("X Xef gh|"));
    }

    template<> template<>
    void alvimkeymap_object::test<164>()
    {
        set_test_name("i< and a< take the angle brackets around the caret, nesting counted and a count out, while % pairs no angle bracket");
        ALCodeEditor& e = make("f(<a <b> c>, x);\n");
        keys("0fbdi<");
        ensure_equals("i< inside the innermost pair", flat(e.text()), std::string("f(<a <> c>, x);|"));
        keys("u0fbd2a<");
        ensure_equals("2a< the pair a level out, whole", flat(e.text()), std::string("f(, x);|"));
        keys("u0fcda>");
        ensure_equals("a> from after a closed pair: the one still open before it", flat(e.text()), std::string("f(, x);|"));
        keys("u0fbva<d");
        ensure_equals("va< in visual", flat(e.text()), std::string("f(<a  c>, x);|"));
        keys("u0f<%");
        ensure_equals("% from an angle bracket goes on to the round one after it", caretText(), std::string("0:1"));
    }

    template<> template<>
    void alvimkeymap_object::test<165>()
    {
        set_test_name("an operator over h, l or a word motion that cannot move takes the empty stretch where it stays: c inserts there, y yanks nothing, d takes nothing, and none fails");
        ALCodeEditor& e = make("ab\n\nc\n");
        keys("yljclX<Esc>");
        ensure_equals("cl on an empty line inserts there", flat(e.text()), std::string("ab|X|c|"));
        ensure_equals("and keeps nothing", vim->registerText('"'), std::string("a"));

        make("abc\n");
        keys("chX<Esc>");
        ensure_equals("ch at a line's start inserts there", flat(editor->text()), std::string("Xabc|"));

        make("a\n");
        keys("jcwX<Esc>");
        ensure_equals("cw on the last line, empty", flat(editor->text()), std::string("a|X"));

        make("a\n");
        keys("yljceX<Esc>");
        ensure_equals("ce there too", flat(editor->text()), std::string("a|X"));
        ensure_equals("keeping the empty end it took, as C does", vim->registerText('"'), std::string());

        make("abc\n");
        keys("lyl0yh");
        ensure_equals("yh at a line's start yanks nothing", vim->registerText('0'), std::string());

        make("ab\n\nc\n");
        editor->setCaret(ALTextPos(1, 0));
        ex("normal dliQ");
        ensure_equals("dl on an empty line takes nothing and does not fail: the keys after it run", flat(editor->text()), std::string("ab|Q|c|"));
    }

    template<> template<>
    void alvimkeymap_object::test<166>()
    {
        set_test_name("an inclusive motion that ends at a line's end takes no line break: dg_ on an empty line takes nothing, cg_ keeps its empty end, yg_ yanks nothing, and dge from one goes back to the word's end alone");
        ALCodeEditor& e = make("a\n\nb\n");
        keys("yl");
        e.setCaret(ALTextPos(1, 0));
        keys("dg_");
        ensure_equals("dg_ on an empty line: nothing taken", flat(e.text()), std::string("a||b|"));
        ensure_equals("and nothing kept", vim->registerText('"'), std::string("a"));
        keys("cg_X<Esc>");
        ensure_equals("cg_ inserts there, the next line left where it was", flat(e.text()), std::string("a|X|b|"));
        ensure_equals("keeping the empty end", vim->registerText('"'), std::string());

        make("a\n\nb\n");
        keys("yl");
        editor->setCaret(ALTextPos(1, 0));
        keys("yg_");
        ensure_equals("yg_ yanks nothing", vim->registerText('0'), std::string());

        make("ab\n\ncd\n");
        editor->setCaret(ALTextPos(1, 0));
        keys("dge");
        ensure_equals("dge from an empty line: back to the b, the line after it kept", flat(editor->text()), std::string("a|cd|"));
        ensure_equals("what it took", vim->registerText('"'), std::string("b\n"));
    }

    template<> template<>
    void alvimkeymap_object::test<167>()
    {
        set_test_name("a count past one on a doubled operator -- dd yy cc >> gUU, Y and S -- fails from the last line, and from an earlier one takes the lines there are");
        ALCodeEditor& e = make("a\nb\nc");
        keys("ylG");
        for (const char* typed : { "2dd", "2yy", "2>>", "2gUU", "2Y", "2ccX<Esc>", "2SX<Esc>" })
        {
            const std::string what(typed);
            keys(typed);
            ensure_equals(what + " on the last line: nothing changed", flat(e.text()), std::string("a|b|c"));
            ensure_equals(what + ": nothing kept", vim->registerText('"'), std::string("a"));
            ensure(what + ": normal mode, nothing pending", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
        }
        ex("normal 2ddiQ");
        ensure_equals("failed, so the keys after it are not run", flat(e.text()), std::string("a|b|c"));
        keys("1dd");
        ensure_equals("a count of one takes the last line", flat(e.text()), std::string("a|b"));

        make("a\nb\nc\nd");
        editor->setCaret(ALTextPos(1, 0));
        keys("5dd");
        ensure_equals("5dd with three lines left takes the three", flat(editor->text()), std::string("a"));
        ensure_equals("and keeps them", vim->registerText('"'), std::string("b\nc\nd"));
    }

    template<> template<>
    void alvimkeymap_object::test<168>()
    {
        set_test_name("p of a register set to no text -- yiw or ciw on an empty line -- puts nothing and says nothing, and :put puts an empty line; a register never set is still nothing");
        ALCodeEditor& e = make("\nabc\n");
        keys("\"xyiwjl\"xp");
        ensure("nothing said: " + vim->message(), vim->message().empty());
        ensure_equals("nothing put", flat(e.text()), std::string("|abc|"));
        ensure_equals("the caret where it was", caretText(), std::string("1:1"));
        keys("\"x3P");
        ensure("3P neither: " + vim->message(), vim->message().empty() && flat(e.text()) == "|abc|");
        ex("put x");
        ensure("nor :put", !vim->messageIsError());
        ensure_equals(":put x: an empty line under the caret's", flat(e.text()), std::string("|abc||"));
        keys("\"zp");
        ensure("a register never set: E353", vim->messageIsError() && vim->message().find("E353") != std::string::npos);

        make("\nabc\n");
        keys("ciw<Esc>jp");
        ensure("ciw on an empty line, then p: nothing said: " + vim->message(), vim->message().empty());
        ensure_equals("and nothing put", flat(editor->text()), std::string("|abc|"));
    }

    template<> template<>
    void alvimkeymap_object::test<169>()
    {
        set_test_name("how many lines a change took, yanked or shifted is said for more than two, as vim's report has it, and not for two; and so are substitutions");
        ALCodeEditor& e = make("a\nb\nc\nd\ne\nf\n");
        keys("2yy");
        ensure("2yy: nothing said: " + vim->message(), vim->message().empty());
        keys("3yy");
        ensure_equals("3yy", vim->message(), std::string("3 lines yanked"));
        keys("2dd");
        ensure("2dd: nothing said: " + vim->message(), vim->message().empty());
        ensure_equals("the two gone", flat(e.text()), std::string("c|d|e|f|"));
        keys("3dd");
        ensure_equals("3dd", vim->message(), std::string("3 fewer lines"));

        make("a\nb\nc\n");
        keys("2>>");
        ensure("2>>: nothing said: " + vim->message(), vim->message().empty());
        keys("3>>");
        ensure_equals("3>>", vim->message(), std::string("3 lines >ed 1 time"));

        make("x\nx\ny\n");
        keys(":g/x/d<CR>");
        ensure("two lines taken by :g: nothing said: " + vim->message(), vim->message().empty());
        ensure_equals("both gone", flat(editor->text()), std::string("y|"));

        make("a a\nb\n");
        keys(":s/a/b/g<CR>");
        ensure("two substitutions: nothing said: " + vim->message(), vim->message().empty());
        ensure_equals("both made", flat(editor->text()), std::string("b b|b|"));
    }

    template<> template<>
    void alvimkeymap_object::test<170>()
    {
        set_test_name("visual p and P put the count's copies -- characters straight on, lines and anything into lines a line each, a block's columns side by side -- the caret where vim leaves it");
        ALCodeEditor& e = make("one two three four\n");
        keys("$yiwbviw2p");
        ensure_equals("viw2p", flat(e.text()), std::string("one two fourfour four|"));
        ensure_equals("the caret on the last character put", caretText(), std::string("0:15"));
        ensure_equals("what it replaced in the unnamed register", vim->registerText('"'), std::string("three"));

        make("one two three four\n");
        keys("$yiwbviw2P");
        ensure_equals("viw2P the same", flat(editor->text()), std::string("one two fourfour four|"));
        ensure_equals("the registers as they were", vim->registerText('"'), std::string("four"));

        make("a\nb\nc\n");
        keys("yyjV2p");
        ensure_equals("yyjV2p: two lines for the one", flat(editor->text()), std::string("a|a|a|c|"));
        ensure_equals("the caret on the first put", caretText(), std::string("1:0"));

        make("abcd\nefgh\nX\n");
        keys("2jylggl<C-v>j2p");
        ensure_equals("a block's columns each replaced by two copies", flat(editor->text()), std::string("aXXcd|eXXgh|X|"));
        ensure_equals("the caret on the last put on its first line", caretText(), std::string("0:2"));

        make("ab\ncd\nxyz\n");
        keys("vjy2jlv3p");
        ensure_equals("characters over two lines put three times straight on", flat(editor->text()), std::string("ab|cd|xab|cab|cab|cz|"));
        ensure_equals("the caret where they begin", caretText(), std::string("2:1"));

        make("ab\n cd\nef\n");
        keys("yiwjV2p");
        ensure_equals("characters into lines: a line each", flat(editor->text()), std::string("ab|ab|ab|ef|"));
        ensure_equals("the caret on the first line put", caretText(), std::string("1:0"));

        make("a\nbcd\nefg\n");
        keys("yyjl<C-v>j2P");
        ensure_equals("lines into a block: the block taken out, the lines put over it twice", flat(editor->text()), std::string("a|a|a|bd|eg|"));
        ensure_equals("the caret on the first of them", caretText(), std::string("1:0"));
    }

    template<> template<>
    void alvimkeymap_object::test<171>()
    {
        set_test_name("a count before . takes the place of the count typed after an operator as well as those before it: d3w then 2. deletes two words more");
        ALCodeEditor& e = make("a b c d e f g h i j\n");
        keys("d3w2.");
        ensure_equals("d3w then 2.: five words in all", flat(e.text()), std::string("f g h i j|"));

        make("a b c d e f g h i j k l m n\n");
        keys("2d3w1.");
        ensure_equals("2d3w then 1.: one more", flat(editor->text()), std::string("h i j k l m n|"));

        make("a b c d e f g h i j\n");
        keys("c2wX<Esc>w3.");
        ensure_equals("c2w then 3.: three words changed", flat(editor->text()), std::string("X X f g h i j|"));

        make("a b c d e f g h i j\n");
        keys("g~3ww2.");
        ensure_equals("g~3w then 2.: two words", flat(editor->text()), std::string("A b c d e f g h i j|"));

        make("abcdef\n");
        keys("3ld0$2.");
        ensure_equals("d0 then 2.: its 0 the motion, no count", flat(editor->text()), std::string("f|"));

        make("a b c d e f g h i j k l m n\n");
        keys("\"a2d3w1.");
        ensure_equals("\"a2d3w then 1.: one more", flat(editor->text()), std::string("h i j k l m n|"));
        ensure_equals("into a still", vim->registerText('a'), std::string("g "));
    }

    template<> template<>
    void alvimkeymap_object::test<172>()
    {
        set_test_name(":m and :t read a line before the first as :0put does -- 0 where lines are counted from 1, .-1 from the first line, the top -- and under lines counted from 0, 0 as the first line");
        ALCodeEditor& e = make("a\nb\nc");
        ex("2t .-1");
        ensure_equals(":2t .-1 from the first line: copied to the top", flat(e.text()), std::string("b|a|b|c"));
        ensure_equals("the caret on the copy", caretText(), std::string("0:0"));

        make("a\nb\nc");
        editor->setCaret(ALTextPos(1, 0));
        ex("3m .-2");
        ensure_equals(":3m .-2 from the second line: moved to the top", flat(editor->text()), std::string("c|a|b"));

        make("a\nb\nc");
        ex("3m 0");
        ensure_equals(":3m 0: to the top", flat(editor->text()), std::string("c|a|b"));

        ALCodeEditor& zero = make("a\nb\nc");
        zero.setLineNumberBase(-1);
        ex("2m 0");
        ensure_equals("lines counted from 0: :2m 0 under the line shown as 0", flat(zero.text()), std::string("a|c|b"));
        ex("1t 0");
        ensure_equals("and :1t 0 copies under it", flat(zero.text()), std::string("a|c|c|b"));
    }

    template<> template<>
    void alvimkeymap_object::test<173>()
    {
        set_test_name("a block keeps blanks as wide as it for a line that stops short of it, as vim's yank does -- to the widest line's end for one taken with $ -- and a line that reaches its first column what it has");
        ALCodeEditor& e = make("abcd\na\nabcd\n");
        e.setCaret(ALTextPos(0, 2));
        keys("<C-v>jjd");
        ensure_equals("the block taken out, the short line as it was", flat(e.text()), std::string("abd|a|abd|"));
        ensure_equals("a blank for the short line", vim->registerText('"'), std::string("c\n \nc"));
        ensure_equals("and in 1", vim->registerText('1'), std::string("c\n \nc"));

        make("abcd\n\nabcd\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("<C-v>jjly");
        ensure_equals("two blanks for an empty line under a block two wide", vim->registerText('"'), std::string("bc\n  \nbc"));

        make("abcd\nab\nabcd\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("<C-v>jjy");
        ensure_equals("a line that ends at the block's first column holds nothing of it", vim->registerText('"'), std::string("c\n\nc"));

        make("abcdef\na\nabc\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("<C-v>jj$y");
        ensure_equals("taken with $: blanks to the widest line's end", vim->registerText('"'), std::string("cdef\n     \nc"));

        make("abcdefghijkl\n\t\nabcdefghijkl\n");
        editor->setCaret(ALTextPos(0, 9));
        keys("<C-v>jjy");
        ensure_equals("a tab's line short of the block: a blank as the reader counts the block", vim->registerText('"'), std::string("j\n \nj"));

        make("abcd\na\nabcd\nXY\n");
        keys("3j\"xy$gg2l<C-v>jj\"xp");
        ensure_equals("visual p over it: the short line as it was", flat(editor->text()), std::string("abXYd|a|abXYd|XY|"));
        ensure_equals("what it replaced kept as a delete keeps it", vim->registerText('"'), std::string("c\n \nc"));
    }

    template<> template<>
    void alvimkeymap_object::test<174>()
    {
        set_test_name("a register's name typed before a motion keeps the column j and k want, with a count before it as well: $ then \"aj goes to the next line's end");
        make("abcdef\nabcdefghij\n");
        keys("$\"aj");
        ensure_equals("$ then \"aj: the line's end", caretText(), std::string("1:9"));

        make("abcdef\nx\nabcdefghij\n");
        keys("$2\"aj");
        ensure_equals("$ then 2\"aj", caretText(), std::string("2:9"));
    }

    template<> template<>
    void alvimkeymap_object::test<175>()
    {
        set_test_name("gj, gk and g$ that run out of rows before the count's go as far as there are and fail, an operator's with them, the caret left where they got to");
        ALCodeEditor& e = make("abc\ndef\nghi");
        ex("normal 9gjx");
        ensure_equals("9gj fails: the x after it not run", flat(e.text()), std::string("abc|def|ghi"));
        ensure_equals("the caret on the last line all the same", caretText(), std::string("2:0"));
        ex("normal 9gkx");
        ensure_equals("9gk fails likewise", flat(e.text()), std::string("abc|def|ghi"));
        ensure_equals("the caret on the first line", caretText(), std::string("0:0"));

        e.setCaret(ALTextPos(0, 1));
        keys("d9gj");
        ensure_equals("d9gj: nothing taken", flat(e.text()), std::string("abc|def|ghi"));
        ensure_equals("the caret where gj got to", caretText(), std::string("2:1"));
        ensure("normal mode, nothing pending", vim->mode() == ALVimKeymap::Mode::Normal && vim->status().empty());
        keys("d9gk");
        ensure_equals("d9gk: nothing taken", flat(e.text()), std::string("abc|def|ghi"));
        ensure_equals("the caret where gk got to", caretText(), std::string("0:1"));
        keys("d2gj");
        ensure_equals("d2gj within the rows there are: from the caret to there", flat(e.text()), std::string("ahi"));

        const std::string line = "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll";
        ALCodeEditor&     w    = make(line.c_str());
        w.setWordWrap(true);
        w.reshape(120, 200);
        const std::vector<ALTextLayout::Row> rows = w.layout().line(0).rows;
        ensure("the line wraps to three rows or more", rows.size() >= 3);
        w.setCaret(ALTextPos(0, rows[1].begin + 2));
        keys("d99g$");
        ensure_equals("d99g$ runs out of rows and fails, nothing taken", w.document().line(0), line);
        ensure_equals("the caret at the end of the last row all the same", caretText(), "0:" + std::to_string(line.size() - 1));
    }

    template<> template<>
    void alvimkeymap_object::test<176>()
    {
        set_test_name("g$ with a count over wrapped rows wants every line's end after it, as $ does, so j goes to the next line's end; one row's g$ its own column");
        ALCodeEditor& e = make("abc\ndefgh\nghijklmn");
        e.setWordWrap(true);
        keys("2g$");
        ensure_equals("2g$: the next row's last character", caretText(), std::string("1:4"));
        keys("j");
        ensure_equals("then j: the next line's end", caretText(), std::string("2:7"));

        e.setCaret(ALTextPos(0, 0));
        keys("g$j");
        ensure_equals("g$ then j: the column g$ went to", caretText(), std::string("1:2"));
    }

    template<> template<>
    void alvimkeymap_object::test<177>()
    {
        set_test_name("go reaches any byte of a long text, its count read as typed and not held to what a count may do; an operator's count times it as well");
        std::string text;
        for (int i = 0; i < 1100; ++i)
        {
            text += std::string(99, 'x') + "\n";
        }
        text.pop_back();
        make(text.c_str());
        keys("105000go");
        ensure_equals("byte 105000, the break that ends line 1050: the line's last character", caretText(), std::string("1049:98"));
        keys("105001go");
        ensure_equals("byte 105001: the first of line 1051", caretText(), std::string("1050:0"));
        keys("99999999999go");
        ensure_equals("past the end, and past the most a count is: the last character", caretText(), std::string("1099:98"));
        keys("gg2d52501go");
        ensure_equals("2d52501go: from the top to byte 105002", editor->document().line(0), std::string(98, 'x'));
        ensure_equals("the lines before it gone", editor->document().lineCount(), 50);
    }

    template<> template<>
    void alvimkeymap_object::test<178>()
    {
        set_test_name("J joins as many lines as its count says, read as typed, and no more than there are: a visual J over more lines than any other command's count is held to joins them all");
        std::string text;
        for (int i = 0; i < 100002; ++i)
        {
            text += "a\n";
        }
        text += "a";
        make(text.c_str());
        keys("100002J");
        ensure_equals("100002J: the last line left", editor->document().lineCount(), 2);
        ensure_equals("the rest one line", editor->document().line(0).size(), size_t(200003));

        make(text.c_str());
        keys("VGJ");
        ensure_equals("VGJ: every line one", editor->document().lineCount(), 1);
        ensure_equals("all of them", editor->document().line(0).size(), size_t(200005));
    }

    template<> template<>
    void alvimkeymap_object::test<179>()
    {
        set_test_name("* keeps its word as \\<word\\>, so :s//, :g// and n after them take it whole; and what * and g* look for goes by ignorecase alone, their n too");
        ALCodeEditor& e = make("foo food\n");
        keys("*");
        ex("%s//bar/g");
        ensure_equals(":s// after * the word alone", flat(e.text()), std::string("bar food|"));

        make("foo food\nfood\n");
        keys("*");
        ex("g//d");
        ensure_equals(":g// after * the lines with the word whole", flat(editor->text()), std::string("food|"));

        make("foo\nfood foo\n");
        keys("*");
        ex("s//X/");
        ensure_equals(":s// on the line * went to", flat(editor->text()), std::string("foo|food X|"));

        make("ab foo food\nfoo\n");
        editor->setCaret(ALTextPos(0, 4));
        keys("*");
        ex("s//X/");
        keys("nn");
        ensure_equals("n after the :s// the word whole still", caretText(), std::string("0:3"));

        make("Foo foo Foo foo\n");
        ex("set ic scs");
        keys("g*nn");
        ensure_equals("g* and its n by ignorecase alone, the capital no matter", caretText(), std::string("0:12"));
        keys("0*nn");
        ensure_equals("and *", caretText(), std::string("0:12"));
    }

    template<> template<>
    void alvimkeymap_object::test<180>()
    {
        set_test_name("t and T, and ; and , after them, step by whole characters, however many bytes each takes: ; goes past the one beside the caret");
        make("a" "\xC3\xA9" "xbx\n");
        keys("tx");
        ensure_equals("t onto the character of two bytes before the x", caretText(), std::string("0:1"));
        keys(";");
        ensure_equals("; past the x beside it, to before the next", caretText(), std::string("0:4"));

        make("\xC3\xA9" "xax\n");
        keys("tx;");
        ensure_equals("from a character of two bytes under the caret", caretText(), std::string("0:3"));

        make("\xC3\xA9" "b" "\xC3\xA9" "a\n");
        editor->setCaret(ALTextPos(0, 5));
        keys("T");
        editor->handleUnicodeCharHere(static_cast<llwchar>(0xE9));
        ensure_equals("T to the character of two bytes beside the caret: where it is", caretText(), std::string("0:5"));
        keys(";");
        ensure_equals("; past it, to after the one before", caretText(), std::string("0:2"));
    }

    template<> template<>
    void alvimkeymap_object::test<181>()
    {
        set_test_name("a macro of more than ten thousand keys plays whole with a mapping about: only the keys mappings feed count towards E223");
        ALCodeEditor&     e = make("\n");
        const std::string typed(10001, 'x');
        keys(("qai" + typed + "<Esc>q").c_str());
        ensure_equals("recorded, typed once", e.document().line(0).size(), static_cast<size_t>(10001));
        ex("nmap Q x");
        keys("@a");
        ensure("no recursive mapping said: " + vim->message(), !vim->messageIsError());
        ensure_equals("played whole", e.document().line(0).size(), static_cast<size_t>(20002));
    }

    template<> template<>
    void alvimkeymap_object::test<182>()
    {
        set_test_name("a visual {count}> that would put in more than a count may make says so and shifts nothing, as a put does; within it, the count's levels");
        ALCodeEditor& e = make("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\n");
        e.setSoftTabs(true);
        e.setTabWidth(4);
        keys("VG99999>");
        ensure("said: " + vim->message(), vim->messageIsError() && vim->message().find("Too large a count") != std::string::npos);
        ensure_equals("nothing shifted", flat(e.text()), std::string("a|b|c|d|e|f|g|h|i|j|k|"));
        keys("ggVj2>");
        ensure_equals("two levels on each line", flat(e.text()), std::string("        a|        b|c|d|e|f|g|h|i|j|k|"));
    }

    template<> template<>
    void alvimkeymap_object::test<183>()
    {
        set_test_name("Tab on the : line with the cursor back in the range completes a name where the cursor is, the rest of the line kept after it, as vim's does");
        make("x\n");
        keys(":10,20del<Home><Right><Tab>");
        ensure_equals("a name put in at the cursor, nothing doubled", vim->commandLine(), std::string("1center0,20del"));
        keys("<Esc>");
        ensure_equals("Escape puts the line back as it was", vim->commandLine(), std::string("10,20del"));
        keys("<Esc>:10,20<Left><Tab>");
        ensure_equals("in a range with no name after it", vim->commandLine(), std::string("10,2center0"));
        keys("<Esc><Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<184>()
    {
        set_test_name("the : commands are taken by any of their names vim takes, from the least to the whole: :joi :dele :norma :lef :rig :mo :cop :gl :vg :su :ya :pu :und :mar :setlo");
        ALCodeEditor& e = make("a b\nc\n");
        ex("joi");
        ensure_equals(":joi joins", flat(e.text()), std::string("a b c|"));
        make("a\nb\nc\n");
        ex("dele");
        ensure_equals(":dele deletes", flat(editor->text()), std::string("b|c|"));
        make("abc\n");
        ex("norma x");
        ensure_equals(":norma plays the keys", flat(editor->text()), std::string("bc|"));
        make("  a\n");
        ex("lef");
        ensure_equals(":lef to the left", flat(editor->text()), std::string("a|"));
        make("a\n");
        ex("rig 5");
        ensure_equals(":rig to the right", flat(editor->text()), std::string("    a|"));
        make("a\nb\nc\n");
        ex("3mo 0");
        ensure_equals(":mo moves", flat(editor->text()), std::string("c|a|b|"));
        make("a\nb\nc\n");
        ex("3cop 0");
        ensure_equals(":cop copies", flat(editor->text()), std::string("c|a|b|c|"));
        make("a\nxb\nc\n");
        ex("gl/x/d");
        ensure_equals(":gl", flat(editor->text()), std::string("a|c|"));
        make("a\nxb\nc\n");
        ex("vg/x/d");
        ensure_equals(":vg", flat(editor->text()), std::string("xb|"));
        make("aa\n");
        ex("su/a/b/");
        ensure_equals(":su", flat(editor->text()), std::string("ba|"));
        make("a\nb\n");
        ex("ya");
        ex("pu");
        ensure_equals(":ya then :pu", flat(editor->text()), std::string("a|a|b|"));
        make("a\n");
        keys("x");
        ex("und");
        ensure_equals(":und undoes", flat(editor->text()), std::string("a|"));
        ensure("none of them unknown: " + vim->message(), !vim->messageIsError());
        make("a\nb\n");
        keys("gg");
        ex("2mar a");
        keys("'a");
        ensure_equals(":mar sets a mark", caretText(), std::string("1:0"));
        ex("setlo ic");
        ensure(":setlo sets", vim->shared().ignoreCase);
    }

    template<> template<>
    void alvimkeymap_object::test<185>()
    {
        set_test_name("insert mode's Ctrl-D takes the step < takes: spaces before a tab go with it, as wide as they are drawn");
        ALCodeEditor& e = make("  \tfoo\n");
        e.setSoftTabs(false);
        e.setTabWidth(4);
        keys("A<C-d><Esc>");
        ensure_equals("two spaces and a tab, a step wide, gone whole, as vim's", flat(e.text()), std::string("foo|"));
    }

    template<> template<>
    void alvimkeymap_object::test<186>()
    {
        set_test_name("a quote object goes from the last quote before the caret, so between two quoted texts it is what lies between; a count of two takes the quotes, a\" the blanks before where none follow, and a quote after a backslash closes nothing");
        ALCodeEditor& e = make("x \"ab\" y \"ef\"\n");
        e.setCaret(ALTextPos(0, 7));
        keys("di\"");
        ensure_equals("di\" between two quoted texts takes what lies between", flat(e.text()), std::string("x \"ab\"\"ef\"|"));
        ensure_equals("what it took", vim->registerText('"'), std::string(" y "));

        make("x \"ab\" y\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("d2i\"");
        ensure_equals("d2i\" takes the quotes, not the blanks", flat(editor->text()), std::string("x  y|"));

        make("x \"ab\",\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("da\"");
        ensure_equals("da\" with no blank after takes the blanks before", flat(editor->text()), std::string("x,|"));

        make("x \"a\\\"b\" y\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("di\"");
        ensure_equals("di\" passes over an escaped quote", flat(editor->text()), std::string("x \"\" y|"));
        ensure_equals("and takes it with the rest", vim->registerText('"'), std::string("a\\\"b"));
    }

    template<> template<>
    void alvimkeymap_object::test<187>()
    {
        set_test_name("a bracket object with no block around the caret takes the next one, on across lines; a count past the blocks there are fails; inside, a closer alone on its line leaves the lines that hold the brackets, and i( on () fails in visual mode");
        ALCodeEditor& e = make("x (a) y\n");
        keys("di(");
        ensure_equals("di( before a block takes what the next one holds", flat(e.text()), std::string("x () y|"));

        make("x\n(a) y\n");
        keys("di(");
        ensure_equals("the next block on a line after", flat(editor->text()), std::string("x|() y|"));

        make("x ) (a) y\n");
        keys("di(");
        ensure_equals("a closer before it leaves none", flat(editor->text()), std::string("x ) (a) y|"));

        make("x (a) y\n");
        keys("vli(y");
        ensure_equals("vi( over a selection before a block", vim->registerText('"'), std::string("a"));

        make("((a))\n");
        editor->setCaret(ALTextPos(0, 2));
        keys("d3i(");
        ensure_equals("3i( with two blocks fails", flat(editor->text()), std::string("((a))|"));
        keys("d2i(");
        ensure_equals("2i( takes the outer", flat(editor->text()), std::string("()|"));

        make("{\n  x\n}\n");
        editor->setCaret(ALTextPos(1, 2));
        keys("di{");
        ensure_equals("di{ with the closer alone on its line takes the line between", flat(editor->text()), std::string("{|}|"));

        make("(\nfoo\n)\n");
        editor->setCaret(ALTextPos(1, 1));
        keys("di(");
        ensure_equals("and with the brackets on lines of their own", flat(editor->text()), std::string("(|)|"));

        make("{x\n}\n");
        editor->setCaret(ALTextPos(0, 1));
        keys("di{");
        ensure_equals("from the opener's line, up to its end", flat(editor->text()), std::string("{|}|"));

        make("{\n  x\n}\n");
        editor->setCaret(ALTextPos(1, 2));
        keys("vi{y");
        ensure_equals("vi{ takes the line's break as well", vim->registerText('"'), std::string("  x\n"));

        make("x () y\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("vi(y");
        ensure_equals("vi( on () fails, the selection as it was", vim->registerText('"'), std::string(")"));

        make("x () y\n");
        keys("vi(y");
        ensure_equals("and before one, the next block holding nothing", vim->registerText('"'), std::string("x"));
    }

    template<> template<>
    void alvimkeymap_object::test<188>()
    {
        set_test_name("a word or quote object over a block of one character keeps the block, where a bracket object makes it characters and V iw does too");
        ALCodeEditor& e = make("one two\nthree four\n");
        e.setCaret(ALTextPos(0, 4));
        keys("<C-v>iw");
        ensure("C-v iw keeps the block", vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("y");
        ensure_equals("over the word", vim->registerText('"'), std::string("two"));
        e.setCaret(ALTextPos(0, 4));
        keys("<C-v>aw");
        ensure("C-v aw too", vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("<Esc>Viw");
        ensure("V iw makes it characters", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("<Esc>");

        make("x \"ab\" (cd) y\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("<C-v>i\"");
        ensure("C-v i\" keeps the block", vim->mode() == ALVimKeymap::Mode::VisualBlock);
        keys("y");
        ensure_equals("over what the quotes hold", vim->registerText('"'), std::string("ab"));
        editor->setCaret(ALTextPos(0, 8));
        keys("<C-v>i(");
        ensure("C-v i( makes it characters", vim->mode() == ALVimKeymap::Mode::Visual);
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<189>()
    {
        set_test_name("ap with no blank lines after takes those before; ap on blank lines takes the paragraph after them, and fails with none; vip leaves the caret at its last line's start");
        ALCodeEditor& e = make("a\n\nb\nc");
        e.setCaret(ALTextPos(2, 0));
        keys("dap");
        ensure_equals("dap on the last paragraph takes the blank line before", flat(e.text()), std::string("a"));

        make("a\n\nb\nc");
        editor->setCaret(ALTextPos(2, 0));
        keys("Vapy");
        ensure_equals("and V ap selects it so", vim->registerText('"'), std::string("\nb\nc"));

        make("a\n\n\nb\nc\n\nd");
        editor->setCaret(ALTextPos(1, 0));
        keys("dap");
        ensure_equals("dap on blank lines takes the paragraph after them", flat(editor->text()), std::string("a||d"));

        make("a\nb\n\n");
        editor->setCaret(ALTextPos(2, 0));
        keys("dap");
        ensure_equals("and with no paragraph after them fails", flat(editor->text()), std::string("a|b||"));

        make("abc\ndef\n\nx");
        editor->setCaret(ALTextPos(0, 2));
        keys("vip<Esc>");
        ensure_equals("vip: the caret at the start of the last line", caretText(), std::string("1:0"));
    }

    template<> template<>
    void alvimkeymap_object::test<190>()
    {
        set_test_name("a tag ends at the > that is no part of a quoted attribute value");
        ALCodeEditor& e = make("<a title=\"x>y\">text</a>\n");
        e.setCaret(ALTextPos(0, 17));
        keys("dit");
        ensure_equals("dit takes what the tags hold", flat(e.text()), std::string("<a title=\"x>y\"></a>|"));

        make("<a title='x>y'>text</a>\n");
        editor->setCaret(ALTextPos(0, 5));
        keys("dit");
        ensure_equals("from inside the attribute, its quotes single", flat(editor->text()), std::string("<a title='x>y'></a>|"));
    }

    template<> template<>
    void alvimkeymap_object::test<191>()
    {
        set_test_name("a paragraph object ends where a formfeed or an nroff macro of 'paragraphs' or 'sections' begins another");
        ALCodeEditor& e = make("a\nb\n.PP\nc\nd");
        keys("dip");
        ensure_equals("ip stops before .PP", flat(e.text()), std::string(".PP|c|d"));

        make("a\n.SH\nc\nd");
        editor->setCaret(ALTextPos(3, 0));
        keys("dip");
        ensure_equals("and goes back no further than .SH", flat(editor->text()), std::string("a"));

        make("a\n\fb\nc\nd");
        editor->setCaret(ALTextPos(3, 0));
        keys("dip");
        ensure_equals("or than a formfeed", flat(editor->text()), std::string("a"));

        make("a\n.XX\nc\nd");
        editor->setCaret(ALTextPos(3, 0));
        keys("dip");
        ensure_equals("a macro of neither is no boundary", flat(editor->text()), std::string(""));

        make("a\nb\n.PP\nc\n\nd");
        editor->setCaret(ALTextPos(2, 0));
        keys("dap");
        ensure_equals("ap from the macro's line, with the blank line after", flat(editor->text()), std::string("a|b|d"));

        make("a\nb\nc\n.PP\nd");
        keys("Vjipy");
        ensure_equals("V ip taken on stops before it too", vim->registerText('"'), std::string("a\nb\nc"));
    }

    template<> template<>
    void alvimkeymap_object::test<192>()
    {
        set_test_name("gv and {op}gv over characters taken with $ keep going to the end, for j and for .");
        ALCodeEditor& e = make("ab\ncdefgh\nq\n");
        keys("v$<Esc>gvd.");
        ensure_equals("gv d, then . to the next line's end", flat(e.text()), std::string("q|"));

        make("ab\ncdefgh\nq\n");
        keys("v$<Esc>dgv.");
        ensure_equals("d gv, then . to the next line's end", flat(editor->text()), std::string("q|"));

        make("ab\ncdefgh\nq\n");
        keys("v$<Esc>gvjd");
        ensure_equals("gv, then j to the next line's end", flat(editor->text()), std::string("q|"));
    }

    template<> template<>
    void alvimkeymap_object::test<193>()
    {
        set_test_name(":s// and :g// after * # or g* match as the search did, without smartcase, and so does n after them; after a pattern typed for :s, :s// goes by smartcase again");
        make("Foo foo foo Foo\n");
        ex("set ic scs");
        keys("*");
        ex("s//X/g");
        ensure_equals(":s// after * takes the word in every case", flat(editor->text()), std::string("X X X X|"));

        make("Foo foo foo Foo\n");
        ex("set ic scs");
        editor->setCaret(ALTextPos(0, 12));
        keys("#");
        ex("s//X/g");
        ensure_equals("and after #", flat(editor->text()), std::string("X X X X|"));

        make("Foo foox bar\n");
        ex("set ic scs");
        keys("g*");
        ex("s//X/g");
        ensure_equals("and after g*", flat(editor->text()), std::string("X Xx bar|"));

        make("Foo\nfoo\nbar\nFOO\n");
        ex("set ic scs");
        keys("*");
        ex("g//d");
        ensure_equals(":g// after * takes the lines with the word in any case", flat(editor->text()), std::string("bar|"));

        make("Foo x\nfoo foo\n");
        ex("set ic scs");
        keys("*");
        ex("s//X/");
        keys("n");
        ensure_equals("n after that :s// still without smartcase", caretText(), std::string("1:2"));

        make("Foo foo\nFoo foo\n");
        ex("set ic scs");
        keys("*");
        ex("s/Foo/X/");
        keys("j");
        ex("s//Y/g");
        ensure_equals("a pattern typed for :s, and :s// after it, by smartcase", flat(editor->text()), std::string("X foo|Y foo|"));
    }

    template<> template<>
    void alvimkeymap_object::test<194>()
    {
        set_test_name("after ?, \\? is a ? itself, as vim reads it, and # puts a ? into the search history so");
        typedef std::vector<std::string> history_t;
        make("x ?? ??\n");
        editor->setCaret(ALTextPos(0, 5));
        keys("#");
        ensure_equals("# on ?? back to the one before", caretText(), std::string("0:2"));
        ensure("the history has each ? escaped", vim->shared().search == history_t{ "\\?\\?" });

        make("a?b x a?b\n");
        editor->setCaret(ALTextPos(0, 8));
        keys("?a\\?b<CR>");
        ensure_equals("?a\\?b finds a?b itself", caretText(), std::string("0:6"));
        keys("n");
        ensure_equals("and n goes on with it", caretText(), std::string("0:0"));
    }

    template<> template<>
    void alvimkeymap_object::test<195>()
    {
        set_test_name("a search line is the same as one in the history only where both ended alike: typed after /, after ?, or put there by * and #");
        typedef std::vector<std::string> history_t;
        make("foo foo\n");
        keys("/\\<foo\\><CR>*");
        ensure("a typed \\<foo\\> and *'s are two", vim->shared().search == history_t{ "\\<foo\\>", "\\<foo\\>" });

        make("foo foo\n");
        keys("/foo<CR>?foo<CR>");
        ensure("and so are /foo and ?foo", vim->shared().search == history_t{ "foo", "foo" });

        make("foo foo\n");
        keys("/foo<CR>/foo<CR>");
        ensure("where /foo twice is one", vim->shared().search == history_t{ "foo" });
    }

    template<> template<>
    void alvimkeymap_object::test<196>()
    {
        set_test_name("past Latin-1 a character's class is vim's: ideographs, emoji and punctuation each a word apart, for w e iw and *");
        typedef std::vector<std::string> history_t;
        // Three ideographs, an em dash and an emoji, as their UTF-8 bytes.
        const std::string cjk   = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
        const std::string dash  = "\xe2\x80\x94";
        const std::string emoji = "\xf0\x9f\x98\x80";
        make((cjk + "abc def\n").c_str());
        keys("w");
        ensure_equals("w from the ideographs to the letters after them", caretText(), std::string("0:9"));
        keys("0e");
        ensure_equals("e to the last ideograph", caretText(), std::string("0:6"));
        editor->setCaret(ALTextPos(0, 3));
        keys("yiw");
        ensure_equals("iw the ideographs alone", vim->registerText('"'), cjk);

        make(("a" + emoji + "b c\n").c_str());
        keys("w");
        ensure_equals("w onto an emoji", caretText(), std::string("0:1"));
        keys("w");
        ensure_equals("and past it", caretText(), std::string("0:5"));

        make(("foo" + dash + "bar x\n").c_str());
        keys("w");
        ensure_equals("w onto a dash", caretText(), std::string("0:3"));

        make((cjk + "abc " + cjk + " x\n").c_str());
        keys("*");
        ensure("* the ideographs as the word", vim->shared().search == history_t{ "\\<" + cjk + "\\>" });
    }

    template<> template<>
    void alvimkeymap_object::test<197>()
    {
        set_test_name("( and ) go by sentences, with counts and after an operator, whose delete goes in register 1; is and as are the sentence objects, a visual selection taken on by them");
        const char* text = "One two. Three four.  Five six.\nNext one.\n";
        make(text);
        keys(")");
        ensure_equals(") to the next sentence", caretText(), std::string("0:9"));
        keys(")");
        ensure_equals("past the blanks after a sentence", caretText(), std::string("0:22"));
        keys("(");
        ensure_equals("( back to the one before", caretText(), std::string("0:9"));
        keys("03)");
        ensure_equals("3) on to the next line", caretText(), std::string("1:0"));

        make(text);
        keys("d)");
        ensure_equals("d) to the next sentence", flat(editor->text()), std::string("Three four.  Five six.|Next one.|"));
        ensure_equals("in register 1, however little", vim->registerText('1'), std::string("One two. "));

        make(text);
        editor->setCaret(ALTextPos(0, 12));
        keys("dis");
        ensure_equals("dis the sentence alone", flat(editor->text()), std::string("One two.   Five six.|Next one.|"));
        make(text);
        editor->setCaret(ALTextPos(0, 12));
        keys("das");
        ensure_equals("das with the blanks after it", flat(editor->text()), std::string("One two. Five six.|Next one.|"));
        make(text);
        editor->setCaret(ALTextPos(0, 24));
        keys("das");
        ensure_equals("das where none come after, with those before", flat(editor->text()), std::string("One two. Three four.|Next one.|"));

        make(text);
        editor->setCaret(ALTextPos(0, 12));
        keys("visisy");
        ensure_equals("vis is takes the blanks after on", vim->registerText('"'), std::string("Three four.  "));
        editor->setCaret(ALTextPos(0, 12));
        keys("vasasy");
        ensure_equals("vas as the next sentence too", vim->registerText('"'), std::string("Three four.  Five six."));

        make("Hello world.\nnext\n");
        editor->setCaret(ALTextPos(0, 3));
        keys("dis");
        ensure_equals("dis on a line of one sentence takes the line", flat(editor->text()), std::string("next|"));

        make(".]");
        editor->setCaret(ALTextPos(0, 1));
        keys("das");
        ensure_equals("das on the ] of a last line's .] takes the ] alone", flat(editor->text()), std::string("."));
        ensure_equals("which it took", vim->registerText('"'), std::string("]"));
    }

    template<> template<>
    void alvimkeymap_object::test<198>()
    {
        set_test_name("on the : and / lines Backspace, Delete, Left and Right go by characters as the text's caret does: a letter with its marks, an emoji with what joins it");
        make("x\n");
        keys(":e");
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x301));
        ensure_equals("the mark typed onto the letter", vim->commandLine(), std::string("e\xCC\x81"));
        keys("<BS>");
        ensure_equals("Backspace takes the letter with its mark", vim->commandLine(), std::string());

        keys("ae");
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x301));
        keys("b<Left><Left><Delete>");
        ensure_equals("Left past the b and the marked letter, and Delete takes that whole", vim->commandLine(), std::string("ab"));

        keys("<Esc>:e");
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x301));
        keys("b<Home><Right><BS>");
        ensure_equals("Right past the letter and its mark, and Backspace there takes both", vim->commandLine(), std::string("b"));

        // A man, a zero width joiner and a woman: one emoji.
        keys("<Esc>/");
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x1F468));
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x200D));
        editor->handleUnicodeCharHere(static_cast<llwchar>(0x1F469));
        ensure_equals("the three typed", vim->commandLine(), std::string("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9"));
        keys("<BS>");
        ensure_equals("Backspace takes the joined emoji whole", vim->commandLine(), std::string());
        keys("<Esc>");
    }

    template<> template<>
    void alvimkeymap_object::test<199>()
    {
        set_test_name("+, Return and Ctrl-M on the last line and - on the first fail where the caret is, an operator with them; a count past the end goes as far as there are lines");
        make("  a f\n  bcd\n   e f");
        editor->setCaret(ALTextPos(2, 5));
        keys("+");
        ensure_equals("+ on the last line stays", caretText(), std::string("2:5"));
        keys("<CR>");
        ensure_equals("and so does Return", caretText(), std::string("2:5"));
        keys("<C-m>");
        ensure_equals("and Ctrl-M", caretText(), std::string("2:5"));
        keys("d+");
        ensure_equals("d+ there takes nothing", flat(editor->text()), std::string("  a f|  bcd|   e f"));
        ensure_equals("and leaves the caret", caretText(), std::string("2:5"));
        editor->setCaret(ALTextPos(0, 4));
        keys("-");
        ensure_equals("- on the first line stays", caretText(), std::string("0:4"));
        keys("d-");
        ensure_equals("d- there takes nothing", flat(editor->text()), std::string("  a f|  bcd|   e f"));
        ensure_equals("and leaves the caret", caretText(), std::string("0:4"));

        make("  a\n  bcd\n   e f\nx");
        editor->setCaret(ALTextPos(1, 3));
        keys("5+");
        ensure_equals("5+ with two lines below to the last", caretText(), std::string("3:0"));
        editor->setCaret(ALTextPos(2, 3));
        keys("5-");
        ensure_equals("5- with two lines above to the first's first non-blank", caretText(), std::string("0:2"));
    }

    template<> template<>
    void alvimkeymap_object::test<200>()
    {
        set_test_name("j after an operator over $ keeps the caret's column, as vim forgets the line's end once the operator is done; after $ alone it goes on to every line's end");
        const char* text = "abcdef\nghijklmnop\nqrstuvwxyz";
        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("d$j");
        ensure_equals("d$ then j", caretText(), std::string("1:1"));

        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("y$j");
        ensure_equals("y$ then j", caretText(), std::string("1:2"));

        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("d2$j");
        ensure_equals("d2$ takes the line after", flat(editor->text()), std::string("ab|qrstuvwxyz"));
        ensure_equals("and j after it keeps the column", caretText(), std::string("1:1"));

        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("c$<Esc>j");
        ensure_equals("c$ then j", caretText(), std::string("1:1"));

        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("Dj");
        ensure_equals("D then j", caretText(), std::string("1:1"));

        make(text);
        editor->setCaret(ALTextPos(0, 2));
        keys("$j");
        ensure_equals("$ then j to the next line's end", caretText(), std::string("1:9"));
        keys("j");
        ensure_equals("and on to the one after's", caretText(), std::string("2:9"));
    }

    template<> template<>
    void alvimkeymap_object::test<201>()
    {
        set_test_name("a yank of more than two lines says so as vim's does: into the register named, a block's as a block, and characters over the lines they reach");
        make("one\ntwo\nthree\nfour\nfive\nsix\n");
        keys("\"a3yy");
        ensure_equals("\"a3yy", vim->message(), std::string("3 lines yanked into \"a"));
        keys("\"A3yy");
        ensure_equals("\"A3yy", vim->message(), std::string("3 lines yanked into \"A"));
        keys("\"\"3yy");
        ensure_equals("\"\"3yy", vim->message(), std::string("3 lines yanked into \"\""));
        keys("\"a2yy");
        ensure("\"a2yy: nothing said: " + vim->message(), vim->message().empty());
        keys("\"_3yy");
        ensure("\"_3yy: nothing said: " + vim->message(), vim->message().empty());

        keys("<C-v>2jy");
        ensure_equals("a block of three lines", vim->message(), std::string("block of 3 lines yanked"));
        keys("<C-v>2j\"by");
        ensure_equals("into a register", vim->message(), std::string("block of 3 lines yanked into \"b"));
        keys("<C-v>jy");
        ensure("a block of two: nothing said: " + vim->message(), vim->message().empty());

        editor->setCaret(ALTextPos(0, 1));
        keys("v2jy");
        ensure_equals("characters over three lines", vim->message(), std::string("3 lines yanked"));
        keys("v2j\"ay");
        ensure_equals("into a register", vim->message(), std::string("3 lines yanked into \"a"));
        keys("vjy");
        ensure("over two: nothing said: " + vim->message(), vim->message().empty());
        keys("v$jjy");
        ensure_equals("through the third line's break, the fourth counted", vim->message(), std::string("4 lines yanked"));
        keys("y/four<CR>");
        ensure_equals("y/four from the first line's second character: three lines", vim->message(), std::string("3 lines yanked"));
        keys("y/three<CR>");
        ensure("y/three: two, nothing said: " + vim->message(), vim->message().empty());
    }

    template<> template<>
    void alvimkeymap_object::test<202>()
    {
        set_test_name("more than two lines put say how many more, and a case operator, = and :m over them how many they changed, indented and moved, as vim's report has them");
        const char* text = "one\ntwo\nthree\nfour\nfive\nsix\n";
        make(text);
        keys("gg3yyp");
        ensure_equals("3yyp", vim->message(), std::string("3 more lines"));
        keys("uggyy3p");
        ensure_equals("yy3p", vim->message(), std::string("3 more lines"));
        keys("ugg2yyp");
        ensure("2yyp: nothing said: " + vim->message(), vim->message().empty());
        keys("uggv$y3p");
        ensure_equals("three of a line's characters and its break", vim->message(), std::string("3 more lines"));
        keys("uggv$y2p");
        ensure("two of them: nothing said: " + vim->message(), vim->message().empty());
        keys("u:1,3y<CR>:put<CR>");
        ensure_equals(":put of three lines", vim->message(), std::string("3 more lines"));

        make("one\ntwo\nthree\nfour\nfive\nsix");
        keys("<C-v>3jyGp");
        ensure_equals("a block's rows put past the last line", vim->message(), std::string("3 more lines"));
        keys("uggp");
        ensure("a block put over lines there are: nothing said: " + vim->message(), vim->message().empty());

        make(text);
        keys("g~2j");
        ensure_equals("g~2j", vim->message(), std::string("3 lines changed"));
        keys("ugggUj");
        ensure("gUj: nothing said: " + vim->message(), vim->message().empty());
        keys("ugg3gUU");
        ensure_equals("3gUU", vim->message(), std::string("3 lines changed"));
        keys("uggV2jU");
        ensure_equals("V2jU", vim->message(), std::string("3 lines changed"));
        keys("uggv2j~");
        ensure_equals("v2j~", vim->message(), std::string("3 lines changed"));
        keys("u");

        vim->hooks().format = [](ALTextView&, S32, S32) {};
        keys("gg=2j");
        ensure_equals("=2j", vim->message(), std::string("3 lines indented"));
        keys("=j");
        ensure("=j: nothing said: " + vim->message(), vim->message().empty());
        keys("3==");
        ensure_equals("3==", vim->message(), std::string("3 lines indented"));

        ex("1,3m$");
        ensure_equals(":1,3m$", vim->message(), std::string("3 lines moved"));
        keys("u");
        ex("1,2m$");
        ensure(":1,2m$: nothing said: " + vim->message(), vim->message().empty());
        keys("u");
        ex("1,3t$");
        ensure_equals(":1,3t$ copies three", vim->message(), std::string("3 more lines"));
        keys("u");
        keys(":g/three/.,+2m0<CR>");
        ensure_equals("moved by a :g", flat(editor->text()), std::string("three|four|five|one|two|six|"));
        ensure(":m under a :g: nothing said: " + vim->message(), vim->message().empty());
    }

    template<> template<>
    void alvimkeymap_object::test<203>()
    {
        set_test_name("more than two substitutions on one line are said only where the : line was typed, as vim's do_sub_msg has it: not from a macro, by @: or under a :g; over more lines they are said however the line came");
        make("a a a\nx\n");
        keys(":s/a/b/g<CR>");
        ensure_equals("typed", vim->message(), std::string("3 substitutions on 1 line"));
        keys("uqqgg:s/a/b/g<CR>q");
        keys("u@q");
        ensure_equals("the macro made them", flat(editor->text()), std::string("b b b|x|"));
        ensure("played from a macro: nothing said: " + vim->message(), vim->message().empty());
        keys("ugg@:");
        ensure_equals("@: made them", flat(editor->text()), std::string("b b b|x|"));
        ensure("run again by @:: nothing said: " + vim->message(), vim->message().empty());
        keys("ug&");
        ensure_equals("g& typed", vim->message(), std::string("3 substitutions on 1 line"));
        keys("u:g/a/s/a/b/g<CR>");
        ensure_equals("a :g made them", flat(editor->text()), std::string("b b b|x|"));
        ensure("under a :g over one line: nothing said: " + vim->message(), vim->message().empty());

        make("a a\na a\n");
        keys("qq:%s/a/b/g<CR>q");
        keys("u@q");
        ensure_equals("from a macro over two lines", vim->message(), std::string("4 substitutions on 2 lines"));
    }

    template<> template<>
    void alvimkeymap_object::test<204>()
    {
        set_test_name("a yank of lines leaves the caret at their start as its motion or object began it -- where it was for yy, Y and :y, the column k kept, the line's start for ip and for lines selected from it -- and a delete over an object that holds nothing leaves it at the object");
        ALCodeEditor& e = make("  alpha beta\n  gamma delta\n  epsilon zeta\n\n  eta theta\n  iota kappa");
        const auto yank_from = [&](S32 line, S32 column, const char* sequence) {
            e.setCaret(ALTextPos(line, column));
            keys(sequence);
            return caretText();
        };
        ensure_equals("yy", yank_from(1, 5, "yy"), std::string("1:5"));
        ensure_equals("2yy", yank_from(1, 5, "2yy"), std::string("1:5"));
        ensure_equals("Y", yank_from(1, 5, "Y"), std::string("1:5"));
        ensure_equals("yj", yank_from(1, 5, "yj"), std::string("1:5"));
        ensure_equals("yk: where k went", yank_from(1, 5, "yk"), std::string("0:5"));
        ensure_equals("y-: where - went", yank_from(1, 5, "y-"), std::string("0:2"));
        ensure_equals("ygg", yank_from(1, 5, "ygg"), std::string("0:2"));
        ensure_equals("yip: the paragraph's start", yank_from(1, 5, "yip"), std::string("0:0"));
        ensure_equals("yip on its first line", yank_from(0, 5, "yip"), std::string("0:0"));
        ensure_equals("Vy: the line's start", yank_from(1, 5, "Vy"), std::string("1:0"));
        ensure_equals("Vjy: the first line's", yank_from(1, 5, "Vjy"), std::string("1:0"));
        ensure_equals("Vky: the caret, above the lines selected from", yank_from(1, 5, "Vky"), std::string("0:5"));
        ensure_equals("vjY", yank_from(1, 5, "vjY"), std::string("1:0"));
        ensure_equals("vkY", yank_from(1, 5, "vkY"), std::string("0:5"));
        e.setCaret(ALTextPos(0, 5));
        ex("2,4y");
        ensure_equals(":2,4y leaves the caret", caretText(), std::string("0:5"));

        make("ab\nabcdefgh");
        editor->setCaret(ALTextPos(1, 6));
        keys("yk");
        ensure_equals("yk onto a shorter line: its last character", caretText(), std::string("0:1"));

        make("Hello world.\nnext");
        editor->setCaret(ALTextPos(0, 3));
        keys("yis");
        ensure_equals("yis over the line: its start", caretText(), std::string("0:0"));

        make("x \"\" y");
        editor->setCaret(ALTextPos(0, 2));
        keys("di\"");
        ensure_equals("di\" between two quotes takes nothing", flat(editor->text()), std::string("x \"\" y"));
        ensure_equals("and leaves the caret between them", caretText(), std::string("0:3"));
        editor->setCaret(ALTextPos(0, 0));
        keys("di\"iX<Esc>");
        ensure_equals("from before them too, and does not fail", flat(editor->text()), std::string("x \"X\" y"));

        make("f() y");
        editor->setCaret(ALTextPos(0, 1));
        keys("di(");
        ensure_equals("di( on () at the closer", caretText(), std::string("0:2"));

        make("<a></a> x");
        editor->setCaret(ALTextPos(0, 1));
        keys("dit");
        ensure_equals("dit on an empty tag between its tags", caretText(), std::string("0:3"));
    }

    template<> template<>
    void alvimkeymap_object::test<205>()
    {
        set_test_name("a count's $ from the last line fails where the caret is, as vim's cursor_down() does: d2$, y2$, c2$, 2D, 2C and 2g_ take nothing, and k after any wants the line's end; from a line above it goes as far as there are");
        const char* text = "abcdefgh\nghi jkl";
        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("2$");
        ensure_equals("2$ on the last line stays", caretText(), std::string("1:2"));
        keys("k");
        ensure_equals("k after it to the line's end", caretText(), std::string("0:7"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("d2$");
        ensure_equals("d2$ takes nothing", flat(editor->text()), std::string("abcdefgh|ghi jkl"));
        ensure_equals("and leaves the caret", caretText(), std::string("1:2"));
        keys("k");
        ensure_equals("k after it to the line's end", caretText(), std::string("0:7"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("yly2$");
        ensure_equals("y2$ yanks nothing", vim->registerText('"'), std::string("i"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("c2$");
        ensure("c2$ inserts nothing", vim->mode() == ALVimKeymap::Mode::Normal);
        keys("X");
        ensure_equals("and the X after it is normal mode's", flat(editor->text()), std::string("abcdefgh|gi jkl"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("2D");
        ensure_equals("2D takes nothing", flat(editor->text()), std::string("abcdefgh|ghi jkl"));
        keys("k");
        ensure_equals("k after it to the line's end", caretText(), std::string("0:7"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("2C");
        ensure("2C inserts nothing", vim->mode() == ALVimKeymap::Mode::Normal);
        ensure_equals("and takes nothing", flat(editor->text()), std::string("abcdefgh|ghi jkl"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("2g_");
        ensure_equals("2g_ on the last line stays", caretText(), std::string("1:2"));

        make(text);
        editor->setCaret(ALTextPos(1, 2));
        keys("vl2$d");
        ensure_equals("a visual 2$ leaves the selection as it was", flat(editor->text()), std::string("abcdefgh|ghjkl"));

        make(text);
        editor->setCaret(ALTextPos(1, 1));
        keys("<C-v>2$d");
        ensure_equals("a block's to every line's end all the same", flat(editor->text()), std::string("abcdefgh|g"));

        make("abc def\nghi jkl\nmno pqr");
        editor->setCaret(ALTextPos(1, 2));
        keys("3$");
        ensure_equals("3$ with one line below to its end", caretText(), std::string("2:6"));
        editor->setCaret(ALTextPos(1, 2));
        keys("d3$");
        ensure_equals("d3$ through it", flat(editor->text()), std::string("abc def|gh"));
    }

    template<> template<>
    void alvimkeymap_object::test<206>()
    {
        set_test_name(":d and :y take a register and then a count after them as vim's do -- a capital adding to its register, a digit the count's, a \" the start of a comment -- and anything more after them, or after :put's register, is trailing characters");
        const char* text = "one\ntwo\nthree\nfour\nfive\nsix";
        make(text);
        editor->setCaret(ALTextPos(1, 1));
        ex("y a");
        ensure_equals(":y a", vim->registerText('a'), std::string("two"));
        ensure_equals("the caret left where it was", caretText(), std::string("1:1"));
        ex("y A 3");
        ensure_equals(":y A 3 adds three lines", vim->registerText('a'), std::string("two\ntwo\nthree\nfour"));
        ensure_equals("and says so", vim->message(), std::string("3 lines yanked into \"A"));
        ex("2,3y b 2");
        ensure_equals(":2,3y b 2: two lines from the range's last", vim->registerText('b'), std::string("three\nfour"));
        ex("y 3");
        ensure_equals(":y 3: a count, not register 3", vim->registerText('0'), std::string("two\nthree\nfour"));
        ensure_equals("said", vim->message(), std::string("3 lines yanked"));
        ex("y \"x");
        ensure_equals("a \" begins a comment", vim->registerText('0'), std::string("two"));
        ensure_equals("and names no register", vim->registerText('x'), std::string());
        ex("d x 3");
        ensure_equals(":d x 3", flat(editor->text()), std::string("one|five|six"));
        ensure_equals("into x", vim->registerText('x'), std::string("two\nthree\nfour"));
        ensure_equals("said", vim->message(), std::string("3 fewer lines"));
        ex("d 2");
        ensure_equals(":d 2 from the caret's line", flat(editor->text()), std::string("one"));

        make(text);
        editor->setCaret(ALTextPos(1, 1));
        ex("d a b");
        ensure("more after the register", vim->messageIsError() && vim->message() == "E488: Trailing characters: b");
        ensure_equals("takes nothing", flat(editor->text()), std::string("one|two|three|four|five|six"));
        ex("y a 3l");
        ensure("more after the count", vim->messageIsError() && vim->message() == "E488: Trailing characters: l");
        ensure_equals("yanks nothing", vim->registerText('a'), std::string());
        ex("d 0");
        ensure("a count of none", vim->messageIsError() && vim->message() == "E939: Positive count required");
        ensure_equals("takes nothing either", flat(editor->text()), std::string("one|two|three|four|five|six"));
        ex("pu a b");
        ensure(":put's register with more after it", vim->messageIsError() && vim->message() == "E488: Trailing characters: b");
        ensure_equals("puts nothing", flat(editor->text()), std::string("one|two|three|four|five|six"));

        make(text);
        editor->setCaret(ALTextPos(1, 1));
        keys("\"a:y<CR>");
        ensure_equals("a register named before the : is none of :y's", vim->registerText('a'), std::string());
        ensure_equals("which yanks into 0", vim->registerText('0'), std::string("two"));

        make(text);
        keys(":g/o/d A<CR>");
        ensure_equals(":g/o/d A", flat(editor->text()), std::string("three|five|six"));
        ensure_equals("each line added to a", vim->registerText('a'), std::string("one\ntwo\nfour"));
        ensure_equals("and how many said once", vim->message(), std::string("3 fewer lines"));
    }
}
