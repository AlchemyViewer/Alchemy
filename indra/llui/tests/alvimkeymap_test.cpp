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
    typedef test_group<alvimkeymap_data, 100> alvimkeymap_group;
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
}
