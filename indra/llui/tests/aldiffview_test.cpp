/**
 * @file aldiffview_test.cpp
 * @brief Two texts compared, side by side or inline.
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

#include "../aldiffview.h"

#include "../alcodeeditor.h"
#include "../aldiffbar.h"
#include "../alflatbutton.h"
#include "../alvimkeymap.h"
#include "../llclipboard.h"
#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

class LLAvatarName;
const std::string gDiffTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gDiffTestAnonName;
}

namespace tut
{
    struct aldiffview_data
    {
        ll_test::HeadlessUI& ui   = ll_test::HeadlessUI::get();
        ALDiffView*          view = nullptr;

        ~aldiffview_data()
        {
            gFocusMgr.setKeyboardFocus(nullptr);
            if (view)
            {
                view->die();
            }
        }

        ALDiffView& make(const char* left, const char* right, bool inline_view = false)
        {
            if (!ui.ok())
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALDiffView::Params p(LLUICtrlFactory::getDefaultParams<ALDiffView>());
            p.name        = "diff";
            p.rect        = LLRect(0, 300, 600, 0);
            p.syntax      = "lsl";
            p.inline_view = inline_view;
            view          = LLUICtrlFactory::create<ALDiffView>(p);
            view->setTexts(left, right);
            return *view;
        }

        static void press(ALDiffView& d, const char* name) { ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>(name))->press(); }
        static bool enabled(ALDiffView& d, const char* name) { return d.bar()->getChild<LLView>(name)->getEnabled(); }

        // The lines "line 0" on, so many, with some of them said otherwise.
        static std::string lines(S32 count, std::initializer_list<std::pair<S32, const char*>> changed = {})
        {
            std::string text;
            for (S32 n = 0; n < count; ++n)
            {
                std::string line = "line " + std::to_string(n);
                for (const auto& [at, said] : changed)
                {
                    if (at == n)
                    {
                        line = said;
                    }
                }
                text += (n ? "\n" : "") + line;
            }
            return text;
        }

        static bool hidden(ALCodeEditor* side, S32 row) { return side->layout().hidden(row); }

        static bool tinted(const ALCodeEditor& side, S32 line)
        {
            return line < static_cast<S32>(side.lineTints().size()) && side.lineTints()[static_cast<size_t>(line)].mV[VALPHA] > 0.f;
        }
    };
    typedef test_group<aldiffview_data> aldiffview_group;
    typedef aldiffview_group::object    aldiffview_object;
    aldiffview_group                    aldiffview_instance("aldiffview");

    template<> template<>
    void aldiffview_object::test<1>()
    {
        set_test_name("side by side: the lines lined up, a line one side has beside an empty one without a number, and each change tinted");
        ALDiffView& d = make("a\nb\nc", "a\nx\nc\nd");
        ensure_equals("the left, with a line to stand beside d", d.left()->text(), std::string("a\nb\nc\n"));
        ensure_equals("the right as it is", d.right()->text(), std::string("a\nx\nc\nd"));
        ensure("numbered as each text's own, the empty line none", d.left()->lineNumbers() == std::vector<S32>{ 1, 2, 3, 0 });
        ensure("the right's", d.right()->lineNumbers() == std::vector<S32>{ 1, 2, 3, 4 });
        ensure("what is the same untinted", !tinted(*d.left(), 0) && !tinted(*d.right(), 2));
        ensure("what was taken out, and what was put in, tinted", tinted(*d.left(), 1) && tinted(*d.right(), 1) && tinted(*d.right(), 3));
        ensure("the empty line too, quieter", tinted(*d.left(), 3));
        ensure_equals("two changes", d.changeCount(), 2);
        ensure("the word changed marked on each side", d.left()->decorations().size() == 1 && d.right()->decorations().size() == 1);
        ensure("neither side can be changed", d.left()->isReadOnly() && d.right()->isReadOnly());
    }

    template<> template<>
    void aldiffview_object::test<2>()
    {
        set_test_name("inline: what was taken out above what was put in, numbered as the right; and back side by side");
        ALDiffView& d = make("a\nb\nc", "a\nx\nc\nd", true);
        ensure_equals("one text", d.inlined()->text(), std::string("a\nb\nx\nc\nd"));
        ensure("the line taken out without a number", d.inlined()->lineNumbers() == std::vector<S32>{ 1, 0, 2, 3, 4 });
        ensure("tinted", tinted(*d.inlined(), 1) && tinted(*d.inlined(), 2) && !tinted(*d.inlined(), 3) && tinted(*d.inlined(), 4));
        ensure("the one shown", d.shown() == d.inlined() && d.inlined()->getVisible() && !d.left()->getVisible());
        d.setInline(false);
        ensure("side by side again", d.left()->getVisible() && d.right()->getVisible() && !d.inlined()->getVisible());
        ensure_equals("lined up as before", d.left()->text(), std::string("a\nb\nc\n"));
    }

    template<> template<>
    void aldiffview_object::test<3>()
    {
        set_test_name("F7 goes to the next change, Shift-F7 to the one before; Escape tells whoever shows it");
        ALDiffView& d = make("one\ntwo\nthree\nfour\nfive\nsix", "one\n2\nthree\nfour\nfive\nsix\nseven");
        d.right()->setFocus(true);
        ensure("taken", d.handleKey(KEY_F7, MASK_NONE, false));
        ensure_equals("the first change", d.right()->caret().line, 1);
        d.handleKey(KEY_F7, MASK_NONE, false);
        ensure_equals("the next", d.right()->caret().line, 6);
        ensure("none after", !d.goToChange(true));
        d.handleKey(KEY_F7, MASK_SHIFT, false);
        ensure_equals("back", d.right()->caret().line, 1);
        ensure_equals("the other side lined up with it", d.left()->caret().line, 1);
        bool told = false;
        d.setOnEscape([&told]() { told = true; });
        ensure("escape", d.handleKey(KEY_ESCAPE, MASK_NONE, false) && told);
    }

    template<> template<>
    void aldiffview_object::test<4>()
    {
        set_test_name("the same texts: no change, nothing tinted; and titles over each side");
        ALDiffView& d = make("same\ntext", "same\ntext");
        ensure_equals("no changes", d.changeCount(), 0);
        ensure("nothing tinted", !tinted(*d.left(), 0) && !tinted(*d.right(), 1));
        d.setTitles("Compiled", "Made from the source");
        ensure_equals("the left's", d.getChild<LLUICtrl>("left_title")->getValue().asString(), std::string("Compiled"));
        ensure_equals("the right's", d.getChild<LLUICtrl>("right_title")->getValue().asString(), std::string("Made from the source"));
    }

    template<> template<>
    void aldiffview_object::test<5>()
    {
        set_test_name("Escape from a side: a selection let go of first, then the comparison's; kept with nobody to tell");
        ALDiffView& d = make("one\ntwo", "one\n2");
        S32         told = 0;
        d.setOnEscape([&told]() { ++told; });
        d.right()->setFocus(true);
        d.right()->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        ensure("the selection first", d.right()->handleKey(KEY_ESCAPE, MASK_NONE, false) && !d.right()->hasSelection() && told == 0);
        ensure("then the comparison's", d.right()->handleKey(KEY_ESCAPE, MASK_NONE, false) && told == 1);
        d.setOnEscape(nullptr);
        ensure("nobody to tell: kept, and the keyboard with it", d.right()->handleKey(KEY_ESCAPE, MASK_NONE, false) && d.right()->hasFocus());
    }

    template<> template<>
    void aldiffview_object::test<6>()
    {
        set_test_name("anchored: lines known to stand for each other side by side however they differ; and typing goes to whoever shows it, at the right's line");
        ALDiffView& d = make("", "");
        d.setTexts("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}", "-- written\n\nll.Say(0, \"hi\")", { { 4, 2 } });
        const std::vector<S32>& left_numbers  = d.left()->lineNumbers();
        const std::vector<S32>& right_numbers = d.right()->lineNumbers();
        S32                     say           = -1;
        for (S32 row = 0; row < static_cast<S32>(left_numbers.size()); ++row)
        {
            if (left_numbers[static_cast<size_t>(row)] == 5)
            {
                say = row;
            }
        }
        ensure("the LSL's call shown", say >= 0 && say < static_cast<S32>(right_numbers.size()));
        ensure_equals("beside the SLua's", right_numbers[static_cast<size_t>(say)], 3);

        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name                 = "source";
        p.rect                 = LLRect(0, 100, 300, 0);
        ALCodeEditor* source   = LLUICtrlFactory::create<ALCodeEditor>(p);
        source->setText("-- written\n\nll.Say(0, \"hi\")");
        S32 at_line = -1;
        S32 at_column = -1;
        d.setOnEdit([&](S32 line, S32 column) -> LLView* {
            at_line   = line;
            at_column = column;
            source->goTo(ALTextPos(line, column));
            return source;
        });
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(say, 2));
        ensure("a character typed on the right", d.right()->handleUnicodeChar('x', false));
        ensure("told where: the right's line and column", at_line == 2 && at_column == 2);
        ensure_equals("typed there instead", source->text(), std::string("-- written\n\nllx.Say(0, \"hi\")"));
        ensure("the comparison as it was", d.right()->isReadOnly() && d.right()->text().find("llx") == std::string::npos);

        // From the left, the line beside it, from its start; from a line
        // the right has none of, the next it has.
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(say, 5));
        ensure("a line broken on the left", d.left()->handleKey(KEY_RETURN, MASK_NONE, false));
        ensure("the right's line beside it, its start", at_line == 2 && at_column == 0);
        ensure_equals("broken there", source->text(), std::string("-- written\n\n\nllx.Say(0, \"hi\")"));
        d.left()->goTo(ALTextPos(0, 0));
        ensure_equals("before any line of the right: its first after", d.rightAtCaret().first, 0);

        // Nobody to tell: nothing typed anywhere.
        d.setOnEdit(nullptr);
        at_line = -1;
        d.right()->setFocus(true);
        d.right()->handleUnicodeChar('y', false);
        ensure("nothing told, nothing typed", at_line == -1 && source->text() == "-- written\n\n\nllx.Say(0, \"hi\")");
        source->die();
    }

    template<> template<>
    void aldiffview_object::test<7>()
    {
        set_test_name("a copy takes each side's text as it is: not the empty lines that line the sides up");
        ALDiffView& d = make("one\nfour", "one\ntwo\nthree\nfour\nfive");
        ensure_equals("lined up", d.left()->text(), std::string("one\n\n\nfour\n"));
        LLClipboard& clipboard = LLClipboard::instance();
        std::string  copied;

        d.left()->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(4, 0)));
        d.left()->copy();
        clipboard.pasteFromClipboard(copied);
        ensure_equals("all of the left, as its text has it", copied, std::string("one\nfour"));

        d.left()->setSelection(ALTextRange(ALTextPos(0, 1), ALTextPos(2, 0)));
        d.left()->copy();
        clipboard.pasteFromClipboard(copied);
        ensure_equals("into the gap: the line's break, which the text has", copied, std::string("ne\n"));

        d.right()->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(4, 4)));
        d.right()->copy();
        clipboard.pasteFromClipboard(copied);
        ensure_equals("the right, which has no gap, whole", copied, std::string("one\ntwo\nthree\nfour\nfive"));
    }

    template<> template<>
    void aldiffview_object::test<8>()
    {
        set_test_name("the bar: how many changes, which the caret is in, and the steps from it; Alt-Down and Alt-Up as F7 and Shift-F7");
        ALDiffView& d = make("one\ntwo\nthree\nfour\nfive\nsix", "one\n2\nthree\nfour\nfive\nsix\nseven");
        d.right()->setFocus(true);
        ensure_equals("in none: how many", d.bar()->countSaid(), std::string("2 changes"));
        ensure("nothing before the first line, something after", !enabled(d, "previous") && enabled(d, "next"));
        ensure("Alt-Down, let go by the side, which cannot move lines", d.right()->handleKey(KEY_DOWN, MASK_ALT, false));
        ensure_equals("the first change", d.right()->caret().line, 1);
        ensure_equals("in it", d.bar()->countSaid(), std::string("Change 1 of 2"));
        ensure("none before it", !enabled(d, "previous") && enabled(d, "next"));
        press(d, "next");
        ensure_equals("the next, from the bar", d.right()->caret().line, 6);
        ensure_equals("said", d.bar()->countSaid(), std::string("Change 2 of 2"));
        ensure("none after it", enabled(d, "previous") && !enabled(d, "next"));
        ensure("the keyboard in the side", d.right()->hasFocus());
        d.right()->goTo(ALTextPos(3, 0));
        ensure_equals("between them: how many again", d.bar()->countSaid(), std::string("2 changes"));
        ensure("Alt-Up", d.right()->handleKey(KEY_UP, MASK_ALT, false) && d.right()->caret().line == 1);
        press(d, "previous");
        ensure_equals("none before the first: where it was", d.right()->caret().line, 1);

        d.setTexts("same", "same");
        ensure_equals("none at all", d.bar()->countSaid(), std::string("No changes"));
        ensure("nowhere to go", !enabled(d, "previous") && !enabled(d, "next"));
    }

    template<> template<>
    void aldiffview_object::test<9>()
    {
        set_test_name("the bar's done only where there is somewhere to go back to; its inline toggle turns the view and says so");
        ALDiffView& d = make("a\nb", "a\nc");
        LLView*     done = d.bar()->getChild<LLView>("done");
        ensure("nobody to tell: no done", !done->getVisible());
        S32 told = 0;
        d.setOnEscape([&told]() { ++told; });
        ensure("done", done->getVisible());
        press(d, "done");
        ensure_equals("told, as for Escape", told, 1);

        S32 said = -1;
        d.setOnInline([&said](bool inlined) { said = inlined ? 1 : 0; });
        ALFlatButton* inlined = ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>("inline"));
        ensure("side by side: not lit", !inlined->getToggleState());
        inlined->press();
        ensure("inline", d.isInline() && said == 1 && inlined->getToggleState());
        inlined->press();
        ensure("and back", !d.isInline() && said == 0 && !inlined->getToggleState());
        d.setInline(true);
        ensure("lit as the view is, however it was turned", inlined->getToggleState());
    }

    template<> template<>
    void aldiffview_object::test<10>()
    {
        set_test_name("swapped: the right shown on the left, its titles with it; and what is typed still goes to the right's text, at its line");
        ALDiffView& d = make("a\nb\nc", "a\nx\nc\nd");
        d.setTitles("Saved", "Now");
        press(d, "swap");
        ensure("swapped, and lit", d.isSwapped() && ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>("swap"))->getToggleState());
        ensure_equals("the right's text on the left", d.left()->text(), std::string("a\nx\nc\nd"));
        ensure_equals("the left's on the right, lined up", d.right()->text(), std::string("a\nb\nc\n"));
        ensure("its lines now the ones put in", tinted(*d.right(), 1) && !tinted(*d.right(), 0));
        ensure_equals("the titles with them", d.getChild<LLUICtrl>("left_title")->getValue().asString(), std::string("Now"));
        ensure_equals("both", d.getChild<LLUICtrl>("right_title")->getValue().asString(), std::string("Saved"));

        S32 at_line   = -1;
        S32 at_column = -1;
        d.setOnEdit([&](S32 line, S32 column) -> LLView* {
            at_line   = line;
            at_column = column;
            return nullptr;
        });
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(1, 1));
        d.left()->handleUnicodeChar('y', false);
        ensure("from the side showing the right's text: its line and column", at_line == 1 && at_column == 1);
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(3, 0));
        d.right()->handleUnicodeChar('y', false);
        ensure("from the other, the right's line beside it, its start", at_line == 3 && at_column == 0);

        d.setInline(true);
        ensure_equals("inline, the right's lines taken out", d.inlined()->text(), std::string("a\nx\nb\nc\nd"));
        d.inlined()->goTo(ALTextPos(1, 1));
        ensure("a line of the right's, inline, is its own", d.rightAtCaret() == std::make_pair(1, 1));
        d.inlined()->goTo(ALTextPos(2, 1));
        ensure("one of the left's: the right's next, from its start", d.rightAtCaret() == std::make_pair(2, 0));
        d.setInline(false);
        d.setSwapped(false);
        ensure_equals("back as it was", d.left()->text(), std::string("a\nb\nc\n"));
        ensure_equals("titles too", d.getChild<LLUICtrl>("left_title")->getValue().asString(), std::string("Saved"));
    }

    template<> template<>
    void aldiffview_object::test<11>()
    {
        set_test_name("inline and back, swapped and back: the caret kept on the right's line and column it was on");
        ALDiffView& d = make("one\ntwo\nthree\nfour", "one\n2\nthree\nfour\nfive");
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(2, 3));
        d.setInline(true);
        ensure("inline: below the line taken out", d.inlined()->caret().line == 3 && d.inlined()->caret().column == 3);
        ensure("the keyboard with it", d.inlined()->hasFocus());
        d.setInline(false);
        ensure("side by side again", d.right()->caret().line == 2 && d.right()->caret().column == 3);
        d.setSwapped(true);
        ensure("swapped: on the left, which shows the right's text", d.left()->caret().line == 2 && d.left()->caret().column == 3);
        ensure("the other side beside it", d.right()->caret().line == 2);
        ensure_equals("still the right's line", d.rightAtCaret().first, 2);
    }

    template<> template<>
    void aldiffview_object::test<12>()
    {
        set_test_name("vim over a side: ]c and [c step through the changes, a count as far as there are; Escape with nothing begun leaves the comparison");
        ALDiffView& d = make("one\ntwo\nthree\nfour\nfive\nsix\nseven", "one\n2\nthree\nfour\n5\nsix\nseven\neight");
        d.right()->setModalKeymap(std::make_unique<ALVimKeymap>());
        d.right()->setFocus(true);
        const auto typed = [&d](const char* keys) {
            for (const char* c = keys; *c; ++c)
            {
                d.right()->handleUnicodeChar(static_cast<llwchar>(*c), false);
            }
        };
        typed("]c");
        ensure_equals("the first change", d.right()->caret().line, 1);
        typed("]c");
        ensure_equals("the next", d.right()->caret().line, 4);
        typed("[c");
        ensure_equals("back", d.right()->caret().line, 1);
        typed("5]c");
        ensure_equals("a count, as far as there are", d.right()->caret().line, 7);
        ensure("nothing typed anywhere", d.right()->text().find(']') == std::string::npos);
        ensure("j moves, as in a text that cannot be changed", (typed("gg"), typed("j"), d.right()->caret().line == 1));

        S32 told = 0;
        d.setOnEscape([&told]() { ++told; });
        typed("3");
        ensure("Escape with a count begun lets the count go", d.right()->handleKey(KEY_ESCAPE, MASK_NONE, false) && told == 0);
        ensure("then the comparison's", d.right()->handleKey(KEY_ESCAPE, MASK_NONE, false) && told == 1);
    }

    template<> template<>
    void aldiffview_object::test<13>()
    {
        set_test_name("a long run the same folds beyond three lines of context, both sides, to one row; numbers stay true, a copy takes it all; Return, the bar, and the caret landing in it open it");
        const std::string left  = lines(30);
        const std::string right = lines(30, { { 2, "two" }, { 27, "twenty-seven" } });
        ALDiffView&       d     = make(left.c_str(), right.c_str());
        ensure("one fold, folded", d.foldCount() == 1 && d.foldedCount() == 1);
        // Rows: 0-1 the same, 2 a change, 3-5 context, 6 the fold's row,
        // 7-24 what it hides, 25-27 context, 28 a change, 29-30 the same.
        for (ALCodeEditor* side : { d.left(), d.right() })
        {
            ensure("context shown, the row shown", !hidden(side, 5) && !hidden(side, 6) && !hidden(side, 25));
            ensure("the run hidden", hidden(side, 7) && hidden(side, 16) && hidden(side, 24));
            ensure("its row a line of neither text", side->lineNumbers()[6] == 0 && side->spacerLine(6));
            ensure_equals("numbers true past it", side->lineNumbers()[7], 7);
        }
        LLClipboard& clipboard = LLClipboard::instance();
        std::string  copied;
        d.left()->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(30, 7)));
        d.left()->copy();
        clipboard.pasteFromClipboard(copied);
        ensure_equals("a copy over it takes what it hides, and not its row", copied, left);

        d.right()->setFocus(true);
        d.handleKey(KEY_F7, MASK_NONE, false);
        d.handleKey(KEY_F7, MASK_NONE, false);
        ensure_equals("the changes stepped through over it", d.right()->caret().line, 28);

        d.right()->goTo(ALTextPos(6, 0));
        ensure("Return on its row", d.right()->handleKey(KEY_RETURN, MASK_NONE, false));
        ensure("opened, both sides", d.foldedCount() == 0 && !hidden(d.left(), 7) && !hidden(d.right(), 24));
        ensure("its row gone, the caret on the first line it hid", hidden(d.right(), 6) && d.right()->caret().line == 7);

        press(d, "fold");
        ensure("the bar: folding off, nothing folded", !d.foldsSame() && d.foldedCount() == 0);
        press(d, "fold");
        ensure("and on: folded again", d.foldsSame() && d.foldedCount() == 1 && hidden(d.left(), 7));
        ensure("the caret, on a line folded away, on the row for it", d.right()->caret().line == 6);

        d.left()->goTo(ALTextPos(12, 0));
        ensure("the caret landing in it opens it, both sides", d.foldedCount() == 0 && !hidden(d.right(), 12) && d.left()->caret().line == 12);
    }

    template<> template<>
    void aldiffview_object::test<14>()
    {
        set_test_name("folds at the ends without context outside, none too short or where nothing changed; a click on a row opens it; inline and swapped, as open as they were");
        const std::string left  = lines(20);
        const std::string right = lines(20, { { 19, "nineteen" } });
        ALDiffView&       d     = make(left.c_str(), right.c_str());
        ensure_equals("one, at the start", d.foldCount(), 1);
        ensure("its row first, no context before it", !hidden(d.right(), 0) && hidden(d.right(), 1) && hidden(d.right(), 16) && !hidden(d.right(), 17));
        ensure_equals("the first line it hides numbered as itself", d.right()->lineNumbers()[1], 1);

        d.setTexts(lines(15).c_str(), lines(15, { { 0, "zero" }, { 14, "fourteen" } }).c_str());
        ensure_equals("thirteen between changes: seven beyond context, not enough", d.foldCount(), 0);
        d.setTexts(lines(16).c_str(), lines(16, { { 0, "zero" }, { 15, "fifteen" } }).c_str());
        ensure_equals("fourteen: eight, enough", d.foldCount(), 1);
        d.setTexts(left.c_str(), left.c_str());
        ensure_equals("nothing changed: nothing folded", d.foldCount(), 0);

        d.setTexts(left.c_str(), right.c_str());
        ALCodeEditor* side = d.right();
        const LLRect  frame = side->getRect();
        const LLRect  text  = side->textRect();
        const S32     y     = frame.mBottom + text.mTop - (side->layout().lineTop(0) - side->scrollY()) - side->layout().lineHeight(0) / 2;
        ensure("a click on its row", d.handleMouseDown(frame.mLeft + text.mLeft + 10, y, MASK_NONE));
        ensure("opened, the caret on its first line, the side with the keyboard", d.foldedCount() == 0 && side->caret().line == 1 && side->hasFocus());
        ensure("a click on a line of text is the side's", (d.handleMouseDown(frame.mLeft + text.mLeft + 10, y, MASK_NONE), d.foldedCount() == 0));

        d.setInline(true);
        ensure("inline: as open as it was", d.foldCount() == 1 && d.foldedCount() == 0 && !hidden(d.inlined(), 1));
        d.setFoldSame(true);
        ensure("folded, inline", hidden(d.inlined(), 1) && !hidden(d.inlined(), 0));
        d.inlined()->goTo(ALTextPos(0, 0));
        d.setInline(false);
        ensure("side by side, folded, the caret on its row", d.foldedCount() == 1 && d.right()->caret().line == 0);
        d.setSwapped(true);
        ensure("swapped, folded still", d.foldedCount() == 1 && hidden(d.left(), 1) && hidden(d.right(), 1));
    }
}
