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
        ALDiffView& d = make("a\nb\nc", "a\nx\nc\nd");
        ensure_equals("the left as it is", d.left()->text(), std::string("a\nb\nc"));
        ensure_equals("the right as it is", d.right()->text(), std::string("a\nx\nc\nd"));
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
        ALDiffView& d = make("a\nb\nc", "a\nx\nc\nd", true);
        ensure_equals("one text", d.inlined()->text(), std::string("a\nb\nx\nc\nd"));
        ensure("the line taken out without a number", numbersOf(d.inlined()) == std::vector<S32>{ 1, 0, 2, 3, 4 });
        ensure("tinted", tinted(*d.inlined(), 1) && tinted(*d.inlined(), 2) && !tinted(*d.inlined(), 3) && tinted(*d.inlined(), 4));
        ensure("the one shown", d.shown() == d.inlined() && d.inlined()->getVisible() && !d.left()->getVisible());
        d.setInline(false);
        ensure("side by side again", d.left()->getVisible() && d.right()->getVisible() && !d.inlined()->getVisible());
        ensure_equals("as before", d.left()->text(), std::string("a\nb\nc"));
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
        d.setTexts("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}", "-- written\n\nll.Say(0, \"hi\")", { { 4, 2 } });
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
        ALDiffView& d = make("a\nb\nc", "a\nx\nc\nd");
        d.setTitles("Saved", "Now");
        press(d, "swap");
        ensure("swapped, and lit", d.isSwapped() && ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>("swap"))->getToggleState());
        ensure_equals("the right's text on the left", d.left()->text(), std::string("a\nx\nc\nd"));
        ensure_equals("the left's on the right", d.right()->text(), std::string("a\nb\nc"));
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
        ensure_equals("inline, the right's lines taken out", d.inlined()->text(), std::string("a\nx\nb\nc\nd"));
        d.inlined()->goTo(ALTextPos(1, 1));
        ensure("a line of the right's, inline, is its own", d.rightAtCaret() == std::make_pair(1, 1));
        d.inlined()->goTo(ALTextPos(2, 1));
        ensure("one of the left's: the right's next, from its start", d.rightAtCaret() == std::make_pair(2, 0));
        d.setInline(false);
        d.setSwapped(false);
        ensure_equals("back as it was", d.left()->text(), std::string("a\nb\nc"));
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
        set_test_name("a long run the same folds beyond three lines of context, both sides, to a row of nothing after it; each text as it is, a copy takes it all; unfolding beside it, the bar, and the caret landing in it open it");
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

        d.right()->goTo(ALTextPos(24, 0));
        ensure("unfolding offered under its row", d.right()->canPerform(ALEditorCommand::Unfold));
        ensure("unfolded", d.right()->perform(ALEditorCommand::Unfold));
        ensure("opened, both sides", d.foldedCount() == 0 && !hidden(d.left(), 6) && !hidden(d.right(), 23));
        ensure("its row gone, the caret where it was", d.right()->layout().gapRows(24) == 0 && d.right()->caret().line == 24);
        ensure("nothing more to unfold", !d.right()->canPerform(ALEditorCommand::Unfold));

        press(d, "fold");
        ensure("the bar: folding off, nothing folded", !d.foldsSame() && d.foldedCount() == 0);
        d.right()->goTo(ALTextPos(12, 0));
        press(d, "fold");
        ensure("and on: folded again", d.foldsSame() && d.foldedCount() == 1 && hidden(d.left(), 6));
        ensure("the caret, on a line folded away, under the run's row", d.right()->caret().line == 24);

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
        ensure("no context before it, its row the gap above the context after", hidden(d.right(), 0) && hidden(d.right(), 15) && !hidden(d.right(), 16));
        ensure("the gap", gapsOf(d.right())[16] == 1 && d.right()->layout().lineTop(16) == d.right()->layout().rowHeight());

        d.setTexts(lines(15).c_str(), lines(15, { { 0, "zero" }, { 14, "fourteen" } }).c_str());
        ensure_equals("thirteen between changes: seven beyond context, not enough", d.foldCount(), 0);
        d.setTexts(lines(16).c_str(), lines(16, { { 0, "zero" }, { 15, "fifteen" } }).c_str());
        ensure_equals("fourteen: eight, enough", d.foldCount(), 1);
        d.setTexts(left.c_str(), left.c_str());
        ensure_equals("nothing changed: nothing folded", d.foldCount(), 0);

        d.setTexts(left.c_str(), right.c_str());
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
        ALDiffView& d      = make("a\nb\nc\ne", "a\nx\nc\nd\ne");
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
        ALDiffView& d = make("a\nb\nc\ne\nf", "a\nx\nc\nd\ne");
        ensure("the left: the line changed, the one taken out", signsOf(d.left()) == std::string("\0~\0\0-", 5));
        ensure("the right: the line changed, the one put in", signsOf(d.right()) == std::string("\0~\0+\0", 5));
        d.setInline(true);
        ensure("inline: taken out and put in", signsOf(d.inlined()) == std::string("\0-+\0+\0-", 7));
    }

    template<> template<>
    void aldiffview_object::test<17>()
    {
        set_test_name("the right made anew: compared again, the caret on its line where it went, the runs as open as they were, the anchors carried with their lines, changed or not");
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

        d.setTexts("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}", "-- written\n\nll.Say(0, \"hi\")", { { 4, 2 } });
        d.setRightText("-- written\n-- and more\n\nll.Say(0, \"hi\")");
        ensure("the LSL's call beside the SLua's, a line further down", beside(d, 4, 3));
        d.setRightText("-- written\n-- and more\n\nll.Say(0, \"bye\")");
        ensure("its line changed: beside it still", beside(d, 4, 3));
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

        d.setTexts("a\nb\n", "a\n");
        source->setText("a\n");
        ensure("a line put back before the text's last, empty, line", d.takeBack(0) && source->text() == "a\nb\n");
        d.setTexts("a", "a\nz");
        source->setText("a\nz");
        ensure("a last line taken out, with the break before it", d.takeBack(0) && source->text() == "a");
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
        d.setTexts("one\ntwo\nthree", "one\n2\nthree");
        source->setText("one\n2\nthree");
        const LLRect frame = d.right()->getRect();
        const LLRect text  = d.right()->textRect();
        const S32    row_h = d.right()->layout().lineHeight(1);
        const S32    y     = frame.mBottom + text.mTop - (d.right()->layout().lineTop(1) - d.right()->scrollY()) - row_h / 2;
        const S32    gap_x = (d.left()->getRect().mRight + frame.mLeft) / 2;
        d.handleMouseDown(gap_x, y + row_h, MASK_NONE);
        ensure("beside a line the same, nothing", d.changeCount() == 1);
        ensure("the arrow pressed", d.handleMouseDown(gap_x, y, MASK_NONE));
        ensure("taken back", d.changeCount() == 0 && source->text() == "one\ntwo\nthree");
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
        set_test_name("whitespace let go of from the bar: re-indented lines the same, shown as they are, the caret kept; case only where offered");
        ALDiffView& d = make("default\n{\nstate_entry()\n{\nllSay(0, \"a\");\n}\n}", "default\n{\n    state_entry()\n    {\n        llSay(0,  \"b\");\n    }\n}");
        ensure_equals("as they are: the re-indented lines changed", d.changeCount(), 1);
        ensure("off", !d.ignoresWhitespace());
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(numbersOf(d.right()).size() - 1, 0));
        const S32 line = d.rightAtCaret().first;
        press(d, "ignore_whitespace");
        ensure("on, and lit", d.ignoresWhitespace() && ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>("ignore_whitespace"))->getToggleState());
        ensure_equals("one line changed: the word, not its indent", d.changeCount(), 1);
        ensure("the right shown as it is", d.right()->text().find("\n    state_entry()\n") != std::string::npos);
        ensure("the caret kept", d.rightAtCaret().first == line);
        const S32 say = 4;
        ensure("the changed line's word marked, not its blanks", d.right()->decorations().size() == 1);
        ensure("the line before it the same", !tinted(*d.right(), say - 1));

        ensure("case not offered", !d.bar()->getChild<LLView>("ignore_case")->getVisible());
        d.setTexts("Hello there", "hello there");
        ensure_equals("a change of case a change", d.changeCount(), 1);
        d.setOffersIgnoreCase(true);
        ensure("offered", d.bar()->getChild<LLView>("ignore_case")->getVisible());
        press(d, "ignore_case");
        ensure("let go of", d.ignoresCase() && d.changeCount() == 0);
        d.setOffersIgnoreCase(false);
        ensure("not offered: not let go of", !d.ignoresCase() && d.changeCount() == 1);
    }
}
