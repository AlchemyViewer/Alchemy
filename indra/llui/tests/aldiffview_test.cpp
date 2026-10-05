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

#include "aldiffview.h"

#include "alcodeeditor.h"
#include "aldiffbar.h"
#include "alflatbutton.h"
#include "alvimkeymap.h"
#include "../llclipboard.h"
#include "../llfocusmgr.h"
#include "../llmenugl.h"
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

        static bool hidden(ALCodeEditor* side, S32 line) { return side->layout().hidden(line); }

        // What a side shows of its lines, a line each: the numbers in its
        // gutter, its signs ('\0' for none); and the rows of nothing above
        // each, and below the last.
        static std::vector<S32> numbersOf(const ALCodeEditor* side)
        {
            std::vector<S32> out;
            for (S32 line = 0; line < side->document().lineCount(); ++line)
            {
                const S32 said = side->lineAnnotation(line).number;
                out.push_back(said == ALTextView::LineAnnotation::OWN_NUMBER ? line + 1 : said);
            }
            return out;
        }
        static std::vector<S32> gapsOf(ALCodeEditor* side)
        {
            std::vector<S32> out;
            for (S32 line = 0; line <= side->document().lineCount(); ++line)
            {
                out.push_back(side->layout().gapRows(line));
            }
            return out;
        }
        // Whether two lines, one each side, are beside each other.
        static bool beside(ALDiffView& d, S32 left, S32 right) { return d.left()->layout().lineTop(left) == d.right()->layout().lineTop(right); }
        static std::string signsOf(const ALCodeEditor* side)
        {
            std::string out;
            for (S32 row = 0; row < side->document().lineCount(); ++row)
            {
                out.push_back(side->lineAnnotation(row).sign);
            }
            return out;
        }

        static bool tinted(const ALCodeEditor& side, S32 line)
        {
            return side.lineAnnotation(line).tint.mV[VALPHA] > 0.f;
        }
    };
    typedef test_group<aldiffview_data> aldiffview_group;
    typedef aldiffview_group::object    aldiffview_object;
    aldiffview_group                    aldiffview_instance("aldiffview");

    template<> template<>
    void aldiffview_object::test<1>()
    {
        set_test_name("side by side: each text as it is, lined up, a line one side has beside a gap on the other, and each change tinted");
        ALDiffView& d = make("a\nb = 1\nc", "a\nb = 2\nc\nd");
        ensure_equals("the left as it is", d.left()->text(), std::string("a\nb = 1\nc"));
        ensure_equals("the right as it is", d.right()->text(), std::string("a\nb = 2\nc\nd"));
        ensure("numbered as each text's own", numbersOf(d.left()) == std::vector<S32>{ 1, 2, 3 });
        ensure("the right's", numbersOf(d.right()) == std::vector<S32>{ 1, 2, 3, 4 });
        ensure("a row of nothing below the left, to stand beside d", gapsOf(d.left()) == std::vector<S32>{ 0, 0, 0, 1 });
        ensure("none on the right", gapsOf(d.right()) == std::vector<S32>{ 0, 0, 0, 0, 0 });
        ensure("lined up", beside(d, 2, 2) && d.left()->layout().totalHeight() == d.right()->layout().totalHeight());
        ensure("what is the same untinted", !tinted(*d.left(), 0) && !tinted(*d.right(), 2));
        ensure("what was taken out, and what was put in, tinted", tinted(*d.left(), 1) && tinted(*d.right(), 1) && tinted(*d.right(), 3));
        ensure("the gap too, quieter", d.left()->lineAnnotation(3).gapTint.mV[VALPHA] > 0.f);
        ensure_equals("two changes", d.changeCount(), 2);
        ensure("the word changed marked on each side", d.left()->decorations().size() == 1 && d.right()->decorations().size() == 1);
        ensure("neither side can be changed", d.left()->isReadOnly() && d.right()->isReadOnly());
    }

    template<> template<>
    void aldiffview_object::test<2>()
    {
        set_test_name("inline: what was taken out above what was put in, numbered as the right; and back side by side");
        ALDiffView& d = make("a\nb = 1\nc", "a\nb = 2\nc\nd", true);
        ensure_equals("one text", d.inlined()->text(), std::string("a\nb = 1\nb = 2\nc\nd"));
        ensure("the line taken out without a number", numbersOf(d.inlined()) == std::vector<S32>{ 1, 0, 2, 3, 4 });
        ensure("tinted", tinted(*d.inlined(), 1) && tinted(*d.inlined(), 2) && !tinted(*d.inlined(), 3) && tinted(*d.inlined(), 4));
        ensure("the one shown", d.shown() == d.inlined() && d.inlined()->getVisible() && !d.left()->getVisible());
        d.setInline(false);
        ensure("side by side again", d.left()->getVisible() && d.right()->getVisible() && !d.inlined()->getVisible());
        ensure_equals("as before", d.left()->text(), std::string("a\nb = 1\nc"));
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
        set_test_name("the same texts: no change, nothing tinted; and titles over each side, with no room taken where there are none");
        ALDiffView& d = make("same\ntext", "same\ntext");
        ensure_equals("no changes", d.changeCount(), 0);
        ensure("nothing tinted", !tinted(*d.left(), 0) && !tinted(*d.right(), 1));
        const S32 under_bar = d.getRect().getHeight() - ALDiffBar::wantedHeight();
        ensure("no titles: no row for them, the sides up to the bar",
               d.right()->getRect().mTop == under_bar && !d.getChild<LLView>("left_title")->getVisible() && !d.getChild<LLView>("right_title")->getVisible());
        d.setTitles("Compiled", "Made from the source");
        ensure("titles: a row for them over the sides",
               d.right()->getRect().mTop < under_bar && d.getChild<LLView>("left_title")->getVisible() && d.getChild<LLView>("right_title")->getVisible());
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
        d.setTexts("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}", "-- written\n\nll.Say(0, \"hi\")", { { 4, 4, 2, 2 } });
        ensure("the LSL's call beside the SLua's", beside(d, 4, 2));

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
        d.right()->goTo(ALTextPos(2, 2));
        ensure("a character typed on the right", d.right()->handleUnicodeChar('x', false));
        ensure("told where: the right's line and column", at_line == 2 && at_column == 2);
        ensure_equals("typed there instead", source->text(), std::string("-- written\n\nllx.Say(0, \"hi\")"));
        ensure("the comparison as it was", d.right()->isReadOnly() && d.right()->text().find("llx") == std::string::npos);

        // From the left, the line beside it, from its start; from a line
        // the right has none of, the next it has.
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(4, 5));
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
        set_test_name("a copy takes each side's text as it is: the gaps that line the sides up are no part of it");
        ALDiffView& d = make("one\nfour", "one\ntwo\nthree\nfour\nfive");
        ensure_equals("the left as it is", d.left()->text(), std::string("one\nfour"));
        ensure("lined up by gaps", gapsOf(d.left()) == std::vector<S32>{ 0, 2, 1 } && beside(d, 1, 3));
        LLClipboard& clipboard = LLClipboard::instance();
        std::string  copied;

        d.left()->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 4)));
        d.left()->copy();
        clipboard.pasteFromClipboard(copied);
        ensure_equals("all of the left, as its text has it", copied, std::string("one\nfour"));

        d.left()->setSelection(ALTextRange(ALTextPos(0, 1), ALTextPos(1, 0)));
        d.left()->copy();
        clipboard.pasteFromClipboard(copied);
        ensure_equals("over the gap: the line's break, which the text has", copied, std::string("ne\n"));

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
        ALDiffView& d = make("a\nb = 1\nc", "a\nb = 2\nc\nd");
        d.setTitles("Saved", "Now");
        press(d, "swap");
        ensure("swapped, and lit", d.isSwapped() && ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>("swap"))->getToggleState());
        ensure_equals("the right's text on the left", d.left()->text(), std::string("a\nb = 2\nc\nd"));
        ensure_equals("the left's on the right", d.right()->text(), std::string("a\nb = 1\nc"));
        ensure("lined up, the gap below it", gapsOf(d.right()) == std::vector<S32>{ 0, 0, 0, 1 });
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
        d.right()->goTo(ALTextPos(2, 1));
        d.right()->handleUnicodeChar('y', false);
        ensure("from the other, the right's line beside it, its start", at_line == 2 && at_column == 0);

        d.setInline(true);
        ensure_equals("inline, the right's lines taken out", d.inlined()->text(), std::string("a\nb = 2\nb = 1\nc\nd"));
        d.inlined()->goTo(ALTextPos(1, 1));
        ensure("a line of the right's, inline, is its own", d.rightAtCaret() == std::make_pair(1, 1));
        d.inlined()->goTo(ALTextPos(2, 1));
        ensure("one of the left's: the right's next, from its start", d.rightAtCaret() == std::make_pair(2, 0));
        d.setInline(false);
        d.setSwapped(false);
        ensure_equals("back as it was", d.left()->text(), std::string("a\nb = 1\nc"));
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
        set_test_name("a long run the same folds beyond three lines of context, both sides, to a row of nothing after it; each text as it is, a copy takes it all; the caret stops on its row, and Return there, unfolding beside it, the bar, and the caret landing in it open it");
        const std::string left  = lines(30);
        const std::string right = lines(30, { { 2, "two" }, { 27, "twenty-seven" } });
        ALDiffView&       d     = make(left.c_str(), right.c_str());
        ensure("one fold, folded", d.foldCount() == 1 && d.foldedCount() == 1);
        // Each side's lines: 0-1 the same, 2 a change, 3-5 context, 6-23
        // what it hides, its row the gap above 24, 24-26 context, 27 a
        // change, 28-29 the same.
        for (ALCodeEditor* side : { d.left(), d.right() })
        {
            ensure("context shown", !hidden(side, 5) && !hidden(side, 24));
            ensure("the run hidden", hidden(side, 6) && hidden(side, 15) && hidden(side, 23));
            ensure("its row a gap above the line after it", side->layout().gapRows(24) == 1 && side->layout().gapRows(6) == 0);
            ensure("the text as it is", side->text() == (side == d.left() ? left : right));
        }
        ensure("lined up past it", beside(d, 25, 25));
        LLClipboard& clipboard = LLClipboard::instance();
        std::string  copied;
        d.left()->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(29, 7)));
        d.left()->copy();
        clipboard.pasteFromClipboard(copied);
        ensure_equals("a copy over it takes what it hides", copied, left);

        d.right()->setFocus(true);
        d.handleKey(KEY_F7, MASK_NONE, false);
        d.handleKey(KEY_F7, MASK_NONE, false);
        ensure_equals("the changes stepped through over it", d.right()->caret().line, 27);

        // Down from the line over the run onto its row, on and back; Return
        // there opens it.
        ALCodeEditor* side = d.right();
        side->goTo(ALTextPos(5, 3));
        ensure("down onto its row", side->perform(ALEditorCommand::MoveDown) && side->caretGap() == 24 && side->caret() == ALTextPos(24, 0));
        ensure("on down, to the line under it, as far across", side->perform(ALEditorCommand::MoveDown) && side->caretGap() == -1 && side->caret() == ALTextPos(24, 3));
        ensure("up onto it again", side->perform(ALEditorCommand::MoveUp) && side->caretGap() == 24);
        ensure("and on up over it", side->perform(ALEditorCommand::MoveUp) && side->caretGap() == -1 && side->caret() == ALTextPos(5, 3));
        side->perform(ALEditorCommand::MoveDown);
        ensure("Return on its row", side->handleKey(KEY_RETURN, MASK_NONE, false));
        ensure("opened, both sides", d.foldedCount() == 0 && !hidden(d.left(), 6) && !hidden(side, 23));
        ensure("its row gone, the caret on the first line it hid", side->layout().gapRows(24) == 0 && side->caret().line == 6 && side->caretGap() == -1);

        press(d, "fold");
        ensure("the bar: folding off, nothing folded", !d.foldsSame() && d.foldedCount() == 0);
        side->goTo(ALTextPos(12, 0));
        press(d, "fold");
        ensure("and on: folded again", d.foldsSame() && d.foldedCount() == 1 && hidden(d.left(), 6));
        ensure("the caret, on a line folded away, under the run's row", side->caret().line == 24);

        // Unfolding on the line beside it, as a fold of a script is.
        ensure("unfolding offered under its row", side->canPerform(ALEditorCommand::Unfold));
        ensure("unfolded", side->perform(ALEditorCommand::Unfold));
        ensure("opened, the caret where it was", d.foldedCount() == 0 && side->layout().gapRows(24) == 0 && side->caret().line == 24);
        ensure("nothing more to unfold", !side->canPerform(ALEditorCommand::Unfold));
        d.setFoldSame(false);
        d.setFoldSame(true);

        d.left()->goTo(ALTextPos(12, 0));
        ensure("the caret landing in it opens it, both sides", d.foldedCount() == 0 && !hidden(d.right(), 12) && d.left()->caret().line == 12);
    }

    template<> template<>
    void aldiffview_object::test<14>()
    {
        set_test_name("a fold at the start, its row the gap above the context after it; a click on its row opens it; inline and swapped, as open as they were");
        const std::string left  = lines(20);
        const std::string right = lines(20, { { 19, "nineteen" } });
        ALDiffView&       d     = make(left.c_str(), right.c_str());
        ensure_equals("one, at the start", d.foldCount(), 1);
        ensure("no context before it, its row the gap above the context after", hidden(d.right(), 0) && hidden(d.right(), 15) && !hidden(d.right(), 16));
        ensure("the gap", gapsOf(d.right())[16] == 1 && d.right()->layout().lineTop(16) == d.right()->layout().rowHeight());

        ALCodeEditor* side  = d.right();
        const LLRect  frame = side->getRect();
        const LLRect  text  = side->textRect();
        const S32     y     = frame.mBottom + text.mTop - (side->layout().gapTop(16) - side->scrollY()) - side->layout().rowHeight() / 2;
        ensure("the point in its gap", side->gapAtLocal(y - frame.mBottom) == 16);
        ensure("a click on its row", d.handleMouseDown(frame.mLeft + text.mLeft + 10, y, MASK_NONE));
        ensure("opened, the caret on its first line, the side with the keyboard", d.foldedCount() == 0 && side->caret().line == 0 && side->hasFocus());
        ensure("a click on a line of text is the side's", (d.handleMouseDown(frame.mLeft + text.mLeft + 10, y, MASK_NONE), d.foldedCount() == 0));

        d.setInline(true);
        ensure("inline: as open as it was", d.foldCount() == 1 && d.foldedCount() == 0 && !hidden(d.inlined(), 1));
        d.setFoldSame(true);
        ensure("folded, inline", hidden(d.inlined(), 1) && !hidden(d.inlined(), 16) && gapsOf(d.inlined())[16] == 1);
        d.inlined()->goTo(ALTextPos(16, 0));
        d.setInline(false);
        ensure("side by side, folded, the caret under its row", d.foldedCount() == 1 && d.right()->caret().line == 16);
        d.setSwapped(true);
        ensure("swapped, folded still", d.foldedCount() == 1 && hidden(d.left(), 1) && hidden(d.right(), 1));
    }

    template<> template<>
    void aldiffview_object::test<15>()
    {
        set_test_name("the ruler down each side marks its own changes and, beside a gap, the other side's: either alone shows them all; inline, each line's");
        ALDiffView& d      = make("a\nb = 1\nc\ne", "a\nb = 2\nc\nd\ne");
        const auto  marked = [](const ALCodeEditor* side, S32 row) {
            return side->lineAnnotation(row).rulerTint.mV[VALPHA] > 0.f;
        };
        const auto  red = [](const ALCodeEditor* side, S32 row) {
            const LLColor4& c = side->lineAnnotation(row).rulerTint;
            return c.mV[VRED] > c.mV[VGREEN];
        };
        const auto  gap_marked = [](const ALCodeEditor* side, S32 line) {
            const LLColor4& c = side->lineAnnotation(line).gapRulerTint;
            return c.mV[VALPHA] > 0.f && c.mV[VGREEN] > c.mV[VRED];
        };
        ensure("nothing the same marked", !marked(d.left(), 0) && !marked(d.right(), 0) && !marked(d.right(), 2) && !marked(d.left(), 3));
        ensure("a line changed: taken out on the left, put in on the right", red(d.left(), 1) && !red(d.right(), 1));
        ensure("a line put in: on the right, and on the left by its gap", marked(d.right(), 3) && !red(d.right(), 3) && gap_marked(d.left(), 3));
        d.setInline(true);
        ensure("inline: the line taken out, and the two put in", red(d.inlined(), 1) && !red(d.inlined(), 2) && !red(d.inlined(), 4) && !marked(d.inlined(), 3));
    }

    template<> template<>
    void aldiffview_object::test<16>()
    {
        set_test_name("beside the numbers, what each line is, so a change reads without its colour: ~ changed, - taken out, + put in");
        ALDiffView& d = make("a\nb = 1\nc\ne\nf", "a\nb = 2\nc\nd\ne");
        ensure("the left: the line changed, the one taken out", signsOf(d.left()) == std::string("\0~\0\0-", 5));
        ensure("the right: the line changed, the one put in", signsOf(d.right()) == std::string("\0~\0+\0", 5));
        d.setInline(true);
        ensure("inline: taken out and put in", signsOf(d.inlined()) == std::string("\0-+\0+\0-", 7));
    }

    template<> template<>
    void aldiffview_object::test<17>()
    {
        set_test_name("the right made anew: compared again, the caret on its line where it went, the runs as open as they were");
        const std::string left  = lines(30);
        const std::string right = lines(30, { { 2, "two" }, { 27, "twenty-seven" } });
        ALDiffView&       d     = make(left.c_str(), right.c_str());
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(10, 0));
        ensure("the run opened", d.foldedCount() == 0);
        // The right's line 25: context below the run.
        d.right()->goTo(ALTextPos(25, 2));
        ensure("on the right's line 25", d.rightAtCaret() == std::make_pair(25, 2));
        d.setRightText("inserted\n" + right);
        ensure_equals("compared again: a change more", d.changeCount(), 3);
        ensure("the caret on the same line, one further down", d.rightAtCaret() == std::make_pair(26, 2));
        ensure("the run as open as it was", d.foldCount() == 1 && d.foldedCount() == 0);
        d.setRightText(right);
        d.setFoldSame(true);
        d.right()->goTo(ALTextPos(24, 0));
        d.setRightText("inserted\n" + right);
        ensure("under a folded row: under it again, folded", d.foldedCount() == 1 && d.right()->caret().line == 25 && d.right()->layout().gapRows(25) == 1);
    }

    template<> template<>
    void aldiffview_object::test<18>()
    {
        set_test_name("a change taken back -- lines put in, a line taken out at the end, a line changed -- as one edit of the right's text, compared again after");
        ALDiffView& d = make("a\nb\nc\nd\ne", "a\nB\nc\nx\ny\nd");
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name               = "source";
        p.rect               = LLRect(0, 100, 300, 0);
        ALCodeEditor* source = LLUICtrlFactory::create<ALCodeEditor>(p);
        source->setText("a\nB\nc\nx\ny\nd");
        ensure("nothing offered without anyone to make it", !d.canTakeBack() && !d.takeBack(0) && !d.bar()->getChild<LLView>("take_back")->getVisible());
        const S32 narrow = d.right()->getRect().mLeft - d.left()->getRect().mRight;
        S32       asked  = 0;
        d.setOnTakeBack([&](const ALTextRange& range, const std::string& text) {
            ++asked;
            return source->replaceAll({ { range, text } });
        });
        ensure("offered", d.canTakeBack() && d.bar()->getChild<LLView>("take_back")->getVisible());
        ensure("a gap wide enough for the arrows", d.right()->getRect().mLeft - d.left()->getRect().mRight > narrow);
        ensure_equals("three changes", d.changeCount(), 3);

        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(4, 0));
        ensure("the lines put in taken out", d.takeBack(d.changeAtCaret()));
        ensure_equals("the right's text", source->text(), std::string("a\nB\nc\nd"));
        ensure_equals("compared again", d.rightText(), source->text());
        ensure_equals("a change fewer", d.changeCount(), 2);
        ensure_equals("the caret where they were", d.rightAtCaret().first, 3);
        ensure("the line taken out at the end put back", d.takeBack(1));
        ensure_equals("after the last line", source->text(), std::string("a\nB\nc\nd\ne"));
        ensure("the line changed made as it was", d.takeBack(0));
        ensure_equals("all as the left", source->text(), std::string("a\nb\nc\nd\ne"));
        ensure("nothing left to take back", d.changeCount() == 0 && !d.takeBack(0));
        ensure_equals("each one edit", asked, 3);
        source->die();
    }

    template<> template<>
    void aldiffview_object::test<19>()
    {
        set_test_name("taking back, swapped: still the right's text, with the left's lines; refused, nothing changes; an arrow in the gap at each change takes it back");
        ALDiffView& d = make("one\ntwo\nthree", "one\nx\ny\nthree");
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name               = "source";
        p.rect               = LLRect(0, 100, 300, 0);
        ALCodeEditor* source = LLUICtrlFactory::create<ALCodeEditor>(p);
        source->setText("one\nx\ny\nthree");
        bool allow = false;
        d.setOnTakeBack([&](const ALTextRange& range, const std::string& text) { return allow && source->replaceAll({ { range, text } }); });
        ensure("refused", !d.takeBack(0) && d.changeCount() == 1 && d.rightText() == "one\nx\ny\nthree");
        d.setSwapped(true);
        allow = true;
        ensure("swapped, the right's two lines made the left's one", d.takeBack(0) && source->text() == "one\ntwo\nthree" && d.changeCount() == 0);

        d.setSwapped(false);
        d.setTexts("one\nx = 2\nthree", "one\nx = two\nthree");
        source->setText("one\nx = two\nthree");
        const LLRect frame = d.right()->getRect();
        const LLRect text  = d.right()->textRect();
        const S32    row_h = d.right()->layout().lineHeight(1);
        const S32    y     = frame.mBottom + text.mTop - (d.right()->layout().lineTop(1) - d.right()->scrollY()) - row_h / 2;
        const S32    gap_x = (d.left()->getRect().mRight + frame.mLeft) / 2;
        d.handleMouseDown(gap_x, y + row_h, MASK_NONE);
        ensure("beside a line the same, nothing", d.changeCount() == 1);
        ensure("the arrow pressed", d.handleMouseDown(gap_x, y, MASK_NONE));
        ensure("taken back", d.changeCount() == 0 && source->text() == "one\nx = 2\nthree");
        source->die();
    }

    template<> template<>
    void aldiffview_object::test<20>()
    {
        set_test_name("a side folds nothing of its own, by any command, and wraps no line, whatever it is made with: its rows stay beside the other's");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALDiffView::Params p(LLUICtrlFactory::getDefaultParams<ALDiffView>());
        p.name                 = "diff";
        p.rect                 = LLRect(0, 300, 600, 0);
        p.syntax               = "lsl";
        ALCodeEditor::Params side(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        side.word_wrap         = true;
        p.side                 = side;
        view                   = LLUICtrlFactory::create<ALDiffView>(p);
        view->setTexts("default\n{\n    state_entry()\n    {\n        a();\n    }\n}", "default\n{\n    state_entry()\n    {\n        b();\n    }\n}");
        for (ALCodeEditor* each : { view->left(), view->right(), view->inlined() })
        {
            ensure("no blocks to fold", !each->foldable() && each->foldRegions().empty());
            ensure("no command folds", !each->canPerform(ALEditorCommand::FoldAll) && !each->canPerform(ALEditorCommand::Fold));
            each->foldAll();
            ensure("nothing hidden", !each->layout().anyHidden());
            ensure("unwrapped", !each->getWordWrap());
        }
    }

    template<> template<>
    void aldiffview_object::test<21>()
    {
        set_test_name("whitespace let go of: re-indented lines the same, shown as they are, the caret kept, the bar lit; case only where offered");
        ALDiffView& d = make("default\n{\nstate_entry()\n{\nllSay(0, \"a\");\n}\n}", "default\n{\n    state_entry()\n    {\n        llSay(0,  \"b\");\n    }\n}");
        ensure_equals("as they are: the re-indented lines changed", d.changeCount(), 1);
        ensure("off", !d.ignoresWhitespace());
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(numbersOf(d.right()).size() - 1, 0));
        const S32 line = d.rightAtCaret().first;
        d.setIgnore("whitespace", true);
        ensure("on, and the bar's button lit", d.ignoresWhitespace() && ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>("ignore"))->getToggleState());
        ensure_equals("one line changed: the word, not its indent", d.changeCount(), 1);
        ensure("the right shown as it is", d.right()->text().find("\n    state_entry()\n") != std::string::npos);
        ensure("the caret kept", d.rightAtCaret().first == line);
        const S32 say = 4;
        ensure("the changed line's word marked, not its blanks", d.right()->decorations().size() == 1);
        ensure("the line before it the same", !tinted(*d.right(), say - 1));

        ensure("case not offered", !d.offersIgnore("case"));
        d.setIgnore("case", true);
        ensure("nor taken", !d.ignoresCase());
        d.setTexts("Hello there", "hello there");
        ensure_equals("a change of case a change", d.changeCount(), 1);
        d.setOffersIgnoreCase(true);
        ensure("offered", d.offersIgnore("case"));
        d.setIgnore("case", true);
        ensure("let go of", d.ignoresCase() && d.changeCount() == 0);
        d.setOffersIgnoreCase(false);
        ensure("not offered: not let go of", !d.ignoresCase() && d.changeCount() == 1);
    }

    template<> template<>
    void aldiffview_object::test<22>()
    {
        set_test_name("the sides scroll together as either is scrolled, there and then, down and across, with no frame drawn; inline, the one alone");
        std::string left;
        std::string right;
        for (S32 n = 0; n < 200; ++n)
        {
            const std::string line = "line " + std::to_string(n) + " " + std::string(120, 'x');
            left += (n ? "\n" : "") + line;
            right += (n ? "\n" : "") + (n == 100 ? std::string("changed") : line);
        }
        ALDiffView& d = make(left.c_str(), right.c_str());
        d.setFoldSame(false);
        d.left()->setScrollY(300);
        ensure_equals("the right followed the left down", d.right()->scrollY(), 300);
        d.right()->setScrollY(120);
        ensure_equals("and the left the right", d.left()->scrollY(), 120);
        d.right()->setScrollX(40.f);
        ensure_equals("across", d.left()->scrollX(), 40.f);
        d.left()->goTo(ALTextPos(180, 0));
        d.left()->scrollToCaret();
        ensure("the caret kept in sight on one side: the other with it", d.left()->scrollY() > 300 && d.right()->scrollY() == d.left()->scrollY());
        d.setInline(true);
        const S32 left_at = d.left()->scrollY();
        d.inlined()->setScrollY(10);
        ensure_equals("inline, the sides not shown left where they were", d.left()->scrollY(), left_at);
    }

    template<> template<>
    void aldiffview_object::test<23>()
    {
        set_test_name("the bar is of the side the keyboard is in, as the keyboard goes from one to the other, with no frame drawn");
        ALDiffView& d = make("one\ntwo\nthree\nfour\nfive\nsix", "one\n2\nthree\nfour\nfive\nsix\nseven");
        d.right()->setFocus(true);
        d.left()->goTo(ALTextPos(1, 0));
        d.right()->goTo(ALTextPos(3, 0));
        ensure_equals("the right in front, between changes", d.bar()->countSaid(), std::string("2 changes"));
        d.left()->setFocus(true);
        ensure_equals("the keyboard to the left: its change", d.bar()->countSaid(), std::string("Change 1 of 2"));
        d.right()->setFocus(true);
        ensure_equals("and back", d.bar()->countSaid(), std::string("2 changes"));
    }

    template<> template<>
    void aldiffview_object::test<24>()
    {
        set_test_name("a block moved: tinted as neither taken out nor put in, signed, marked on the ruler; a click on its sign goes to its other end");
        const std::string block = "llOwnerSay(\"a block of lines\");\nllOwnerSay(\"moved as one\");";
        const std::string left  = block + "\nstay one\nstay two\nstay three";
        const std::string right = "stay one\nstay two\nstay three\n" + block;
        ALDiffView&       d     = make(left.c_str(), right.c_str());
        const LLColor4&   moved = d.left()->lineAnnotation(0).tint;
        const LLColor4&   gone  = d.left()->lineAnnotation(0).rulerTint;
        ensure("tinted, as neither red nor green", moved.mV[VALPHA] > 0.f && moved.mV[VBLUE] > moved.mV[VRED] && moved.mV[VBLUE] > moved.mV[VGREEN]);
        ensure("marked on the ruler likewise", gone.mV[VBLUE] > gone.mV[VRED]);
        ensure_equals("signed", signsOf(d.left()), std::string(">>\0\0\0", 5));

        // A click in the left's gutter beside its second line moved.
        ALCodeEditor* side  = d.left();
        const LLRect  frame = side->getRect();
        const LLRect  text  = side->textRect();
        const S32     row_h = side->layout().lineHeight(1);
        const S32     y     = frame.mBottom + text.mTop - (side->layout().lineTop(1) - side->scrollY()) - row_h / 2;
        ensure("taken", d.handleMouseDown(frame.mLeft + text.mLeft / 2, y, MASK_NONE));
        ensure("the right's caret on the other end, which has the keyboard", d.right()->caret().line == 4 && d.right()->hasFocus());
        // In the text rather than the gutter: not taken as a step.
        d.left()->setFocus(true);
        d.handleMouseDown(frame.mLeft + text.mLeft + 20, y, MASK_NONE);
        ensure("a click in the text is the text's", d.right()->caret().line == 4 && !d.right()->hasFocus());
    }

    template<> template<>
    void aldiffview_object::test<25>()
    {
        set_test_name("the bar's menu of what to let go of: each checked as it is, comments with a grammar, case where offered; an item turns its own");
        ALDiffView& d = make("x = 1; // one\n\ny = 2;", "x = 1; // uno\ny = 2;   ");
        // A menu holder, as the viewer's window has one.
        LLMenuHolderGL::Params hp;
        hp.name                  = "menu_holder";
        hp.rect                  = LLRect(0, 1080, 1920, 0);
        hp.mouse_opaque          = false;
        LLMenuHolderGL* holder   = LLUICtrlFactory::create<LLMenuHolderGL>(hp);
        LLMenuGL::sMenuContainer = holder;
        press(d, "ignore");
        LLContextMenu* menu = holder->findChild<LLContextMenu>("menu_diff_ignore");
        ensure("shown", menu != nullptr);
        if (menu)
        {
            menu->arrangeAndClear();
            ensure("comments offered, with LSL's grammar", menu->findChild<LLMenuItemGL>("comments")->getVisible());
            ensure("case not, for code", !menu->findChild<LLMenuItemGL>("case")->getVisible());
            LLMenuItemGL* trailing = menu->findChild<LLMenuItemGL>("trailing");
            trailing->onCommit();
            ensure("an item turns its own on", d.ignores("trailing") && !d.ignores("whitespace"));
        }
        d.setIgnore("comments", true);
        d.setIgnore("blank_lines", true);
        ensure_equals("a comment reworded, a blank line gone and blanks at an end: no change", d.changeCount(), 0);
        ensure("lit", ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>("ignore"))->getToggleState());
        LLMenuGL::sMenuContainer = nullptr;
        LLMortician::updateClass();
        delete holder;
    }

    template<> template<>
    void aldiffview_object::test<26>()
    {
        set_test_name("LSL beside SLua: the caret on a line links the range it is in, from either side; inline none; the converter's notes beside the LSL, inline too");
        const char* lsl  = "default\n{\n    touch_start(integer d)\n    {\n        if (d > 1) llSay(0, \"many\");\n        llSay(0, \"one\");\n    }\n}";
        const char* slua = "LLEvents:on(\"touch_start\", function(detected)\n    local d = #detected\n    if d > 1 then\n        ll.Say(0, \"many\")\n    end\n"
                           "    ll.Say(0, \"one\")\nend)";
        ALDiffView& d = make("", "");
        d.setTexts(lsl, slua, { { 0, 7, 0, 6 }, { 2, 6, 0, 6 }, { 4, 4, 2, 4 }, { 4, 4, 3, 3 }, { 5, 5, 5, 5 } });
        ensure_equals("nothing yet: the caret at the right's top, in the state", d.linkedRange(), 0);
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(4, 0));
        ensure_equals("an LSL if: linked to its SLua", d.linkedRange(), 2);
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(5, 0));
        ensure_equals("a SLua call: linked to its LSL", d.linkedRange(), 4);
        d.setInline(true);
        ensure_equals("inline: none", d.linkedRange(), -1);
        d.setInline(false);

        d.setNotes({ { 4, "LSL: integer division rounds toward zero", "the whole note" } });
        ensure("beside the LSL line", d.left()->noteAt(4).find("integer division") != std::string::npos);
        ensure("not on the SLua", d.right()->noteAt(4).empty());
        const S32 inlined = d.model().lineShowing(ALDiffModel::Column::Inline, true, 4);
        ensure("inline, beside where the line is", inlined >= 0 && !d.inlined()->noteAt(inlined).empty());
        d.setRightText(std::string("-- one more\n") + slua);
        ensure("kept as the right is made anew", !d.left()->noteAt(4).empty());
        d.setTexts(lsl, slua);
        ensure("let go of with new texts", d.left()->noteAt(4).empty());
    }

    template<> template<>
    void aldiffview_object::test<27>()
    {
        set_test_name("vim over a side, typed as the window types: / searches it, Return entering it, and nothing goes to the source; nor ? or :; from either side or inline");
        ALDiffView& d = make("one\ntwo\nthree\nfour\nfive", "one\n2\nthree\nfour\nfive\nsix");
        S32 edits = 0;
        d.setOnEdit([&edits](S32, S32) -> LLView* {
            ++edits;
            return nullptr;
        });
        // Each side vim's, as the studio gives them.
        for (ALCodeEditor* side : { d.left(), d.right(), d.inlined() })
        {
            side->setModalKeymap(std::make_unique<ALVimKeymap>());
        }
        d.right()->setFocus(true);
        // As the window types: the key, and its character whether or not
        // the key was taken -- Return as its key alone -- each from the view
        // with the keyboard, up to the comparison where it is not taken.
        const auto typed = [&d](const char* keys) {
            for (const char* c = keys; *c; ++c)
            {
                LLView* in = dynamic_cast<LLView*>(gFocusMgr.getKeyboardFocus());
                ensure("the keyboard in the comparison", in && (in == &d || d.hasChild(in->getName(), true)));
                if (*c == '\n')
                {
                    in->handleKey(KEY_RETURN, MASK_NONE, false);
                    continue;
                }
                in->handleKey(static_cast<KEY>(toupper(static_cast<unsigned char>(*c))), MASK_NONE, false);
                in->handleUnicodeChar(static_cast<llwchar>(static_cast<unsigned char>(*c)), false);
            }
        };
        typed("/fiv\n");
        ensure_equals("found", d.right()->caret().line, 4);
        ensure_equals("nothing handed to the source", edits, 0);
        ensure("still the comparison's side, with the keyboard", d.right()->hasFocus() && d.right()->getVisible());
        typed("?thr\n");
        ensure_equals("back", d.right()->caret().line, 2);
        typed(":3\n");
        ensure("a : line too", d.right()->caret().line == 2 && edits == 0);
        d.left()->setFocus(true);
        typed("/fou\n");
        ensure("from the left", d.left()->caret().line == 3 && edits == 0);
        d.setInline(true);
        d.inlined()->setFocus(true);
        typed("/six\n");
        ensure("inline", d.inlined()->caret().line == d.inlined()->document().lineCount() - 1 && edits == 0);
    }
}
