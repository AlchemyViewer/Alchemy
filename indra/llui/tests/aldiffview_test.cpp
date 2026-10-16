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
#include "../llslider.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    struct aldiffview_data
    {
        ll_test::HeadlessUI& ui   = ll_test::HeadlessUI::get();
        ALDiffView*          view = nullptr;

        ~aldiffview_data()
        {
            gFocusMgr.setKeyboardFocus(nullptr);
            for (ALDiffView* each : { view, twin })
            {
                if (each)
                {
                    each->die();
                }
            }
            // Reaped now, as the next frame would: a test that empties the
            // graveyard itself then finds only its own views there.
            LLMortician::updateClass();
        }

        ALDiffView& make(const char* left, const char* right, bool inline_view = false)
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

        // The bar's button of that name. getChild makes a stand-in for a
        // name the bar lacks, which is no button.
        static ALFlatButton& require_button(ALDiffView& d, const char* name)
        {
            ALFlatButton* button = ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>(name));
            REQUIRE(button != nullptr);
            return *button;
        }
        static void press(ALDiffView& d, const char* name) { require_button(d, name).press(); }
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

        // A second comparison, whose rebuilds lay out all of it again and
        // fill its editors whole: what one filled again only where it was
        // laid out again is held to.
        ALDiffView* twin = nullptr;
        ALDiffView& makeWhole()
        {
            ALDiffView::Params p(LLUICtrlFactory::getDefaultParams<ALDiffView>());
            p.name   = "whole";
            p.rect   = LLRect(0, 300, 600, 0);
            p.syntax = "lsl";
            twin     = LLUICtrlFactory::create<ALDiffView>(p);
            twin->setKeepsLayout(false);
            return *twin;
        }

        // What an editor holds and is told, all of it, as another's.
        static void require_same(ALCodeEditor* a, ALCodeEditor* b, const std::string& where)
        {
            INFO(where);
            REQUIRE(a->wholeText() == b->wholeText());
            const S32 count = a->document().lineCount();
            for (S32 l = 0; l <= count; ++l)
            {
                const ALTextView::LineAnnotation& x = a->lineAnnotation(l);
                const ALTextView::LineAnnotation& y = b->lineAnnotation(l);
                CAPTURE(l);
                {
                    INFO("what is said of it");
                    REQUIRE(x.number == y.number);
                    REQUIRE(x.sign == y.sign);
                    REQUIRE(x.tint == y.tint);
                    REQUIRE(x.rulerTint == y.rulerTint);
                }
                {
                    INFO("the rows above it");
                    REQUIRE(x.gap == y.gap);
                    REQUIRE(x.gapTint == y.gapTint);
                    REQUIRE(x.gapRulerTint == y.gapRulerTint);
                    REQUIRE(x.gapStop == y.gapStop);
                    REQUIRE(a->layout().gapRows(l) == b->layout().gapRows(l));
                }
                UNSCOPED_INFO("hidden or not");
                REQUIRE((l == count || a->layout().hidden(l) == b->layout().hidden(l)));
                UNSCOPED_INFO("no bar: nobody changed it");
                REQUIRE_FALSE(a->lineChanged(l));
            }
            const std::vector<ALCodeEditor::Decoration>& x = a->decorations();
            const std::vector<ALCodeEditor::Decoration>& y = b->decorations();
            UNSCOPED_INFO("the words marked");
            REQUIRE(x.size() == y.size());
            for (size_t i = 0; i < x.size(); ++i)
            {
                CAPTURE(i);
                REQUIRE(x[i].range == y[i].range);
                REQUIRE(x[i].color == y[i].color);
                REQUIRE(x[i].style == y[i].style);
            }
            REQUIRE(a->caret() == b->caret());
            REQUIRE(a->layout().totalHeight() == b->layout().totalHeight());
        }
        static void require_same_shown(ALDiffView& a, ALDiffView& b, const std::string& where)
        {
            UNSCOPED_INFO(where << ": the layout shown");
            REQUIRE(a.isInline() == b.isInline());
            if (a.isInline())
            {
                require_same(a.inlined(), b.inlined(), where + ", inline");
            }
            else
            {
                require_same(a.left(), b.left(), where + ", left");
                require_same(a.right(), b.right(), where + ", right");
            }
        }

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

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: side by side shows each text lined up by gaps with its changes tinted", "[aldiffview]")
    {
        ALDiffView& d = make("a\nb = 1\nc", "a\nb = 2\nc\nd");
        UNSCOPED_INFO("the left as it is");
        CHECK(d.left()->text() == std::string("a\nb = 1\nc"));
        UNSCOPED_INFO("the right as it is");
        CHECK(d.right()->text() == std::string("a\nb = 2\nc\nd"));
        UNSCOPED_INFO("numbered as each text's own");
        CHECK(numbersOf(d.left()) == std::vector<S32>{ 1, 2, 3 });
        CHECK(numbersOf(d.right()) == std::vector<S32>{ 1, 2, 3, 4 });
        UNSCOPED_INFO("a row of nothing below the left, to stand beside d");
        CHECK(gapsOf(d.left()) == std::vector<S32>{ 0, 0, 0, 1 });
        UNSCOPED_INFO("none on the right");
        CHECK(gapsOf(d.right()) == std::vector<S32>{ 0, 0, 0, 0, 0 });
        {
            INFO("lined up");
            CHECK(beside(d, 2, 2));
            CHECK(d.left()->layout().totalHeight() == d.right()->layout().totalHeight());
        }
        {
            INFO("what is the same untinted");
            CHECK_FALSE(tinted(*d.left(), 0));
            CHECK_FALSE(tinted(*d.right(), 2));
        }
        {
            INFO("what was taken out, and what was put in, tinted");
            CHECK(tinted(*d.left(), 1));
            CHECK(tinted(*d.right(), 1));
            CHECK(tinted(*d.right(), 3));
        }
        UNSCOPED_INFO("the gap too, quieter");
        CHECK(d.left()->lineAnnotation(3).gapTint.mV[VALPHA] > 0.f);
        CHECK(d.changeCount() == 2);
        {
            INFO("the word changed marked on each side");
            CHECK(d.left()->decorations().size() == 1);
            CHECK(d.right()->decorations().size() == 1);
        }
        {
            INFO("neither side can be changed");
            CHECK(d.left()->isReadOnly());
            CHECK(d.right()->isReadOnly());
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: inline puts lines taken out above those put in numbered as the right", "[aldiffview]")
    {
        ALDiffView& d = make("a\nb = 1\nc", "a\nb = 2\nc\nd", true);
        CHECK(d.inlined()->text() == std::string("a\nb = 1\nb = 2\nc\nd"));
        UNSCOPED_INFO("the line taken out without a number");
        CHECK(numbersOf(d.inlined()) == std::vector<S32>{ 1, 0, 2, 3, 4 });
        CHECK(tinted(*d.inlined(), 1));
        CHECK(tinted(*d.inlined(), 2));
        CHECK_FALSE(tinted(*d.inlined(), 3));
        CHECK(tinted(*d.inlined(), 4));
        {
            INFO("the one shown");
            CHECK(d.shown() == d.inlined());
            CHECK(d.inlined()->getVisible());
            CHECK_FALSE(d.left()->getVisible());
        }
        d.setInline(false);
        {
            INFO("side by side again");
            CHECK(d.left()->getVisible());
            CHECK(d.right()->getVisible());
            CHECK_FALSE(d.inlined()->getVisible());
        }
        UNSCOPED_INFO("as before");
        CHECK(d.left()->text() == std::string("a\nb = 1\nc"));
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: F7 and Shift-F7 step through the changes and Escape tells the owner", "[aldiffview]")
    {
        ALDiffView& d = make("one\ntwo\nthree\nfour\nfive\nsix", "one\n2\nthree\nfour\nfive\nsix\nseven");
        d.right()->setFocus(true);
        UNSCOPED_INFO("taken");
        CHECK(d.handleKey(KEY_F7, MASK_NONE, false));
        UNSCOPED_INFO("the first change");
        CHECK(d.right()->caret().line == 1);
        d.handleKey(KEY_F7, MASK_NONE, false);
        UNSCOPED_INFO("the next");
        CHECK(d.right()->caret().line == 6);
        UNSCOPED_INFO("none after");
        CHECK_FALSE(d.goToChange(true));
        d.handleKey(KEY_F7, MASK_SHIFT, false);
        UNSCOPED_INFO("back");
        CHECK(d.right()->caret().line == 1);
        UNSCOPED_INFO("the other side lined up with it");
        CHECK(d.left()->caret().line == 1);
        bool told = false;
        d.setOnEscape([&told]() { told = true; });
        CHECK(d.handleKey(KEY_ESCAPE, MASK_NONE, false));
        CHECK(told);
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: same texts show no change and titles take a row only when given", "[aldiffview]")
    {
        ALDiffView& d = make("same\ntext", "same\ntext");
        CHECK(d.changeCount() == 0);
        CHECK_FALSE(tinted(*d.left(), 0));
        CHECK_FALSE(tinted(*d.right(), 1));
        const S32 under_bar = d.getRect().getHeight() - ALDiffBar::wantedHeight();
        {
            INFO("no titles: no row for them, the sides up to the bar");
            CHECK(d.right()->getRect().mTop == under_bar);
            CHECK_FALSE(d.getChild<LLView>("left_title")->getVisible());
            CHECK_FALSE(d.getChild<LLView>("right_title")->getVisible());
        }
        d.setTitles("Compiled", "Made from the source");
        {
            INFO("titles: a row for them over the sides");
            CHECK(d.right()->getRect().mTop < under_bar);
            CHECK(d.getChild<LLView>("left_title")->getVisible());
            CHECK(d.getChild<LLView>("right_title")->getVisible());
        }
        CHECK(d.getChild<LLUICtrl>("left_title")->getValue().asString() == std::string("Compiled"));
        CHECK(d.getChild<LLUICtrl>("right_title")->getValue().asString() == std::string("Made from the source"));
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: Escape from a side drops a selection first and then tells the owner", "[aldiffview]")
    {
        ALDiffView& d = make("one\ntwo", "one\n2");
        S32         told = 0;
        d.setOnEscape([&told]() { ++told; });
        d.right()->setFocus(true);
        d.right()->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        {
            INFO("the selection first");
            CHECK(d.right()->handleKey(KEY_ESCAPE, MASK_NONE, false));
            CHECK_FALSE(d.right()->hasSelection());
            CHECK(told == 0);
        }
        {
            INFO("then the comparison's");
            CHECK(d.right()->handleKey(KEY_ESCAPE, MASK_NONE, false));
            CHECK(told == 1);
        }
        d.setOnEscape(nullptr);
        {
            INFO("nobody to tell: kept, and the keyboard with it");
            CHECK(d.right()->handleKey(KEY_ESCAPE, MASK_NONE, false));
            CHECK(d.right()->hasFocus());
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: anchored lines sit side by side and typing goes to the owner's source", "[aldiffview]")
    {
        ALDiffView& d = make("", "");
        d.setTexts("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}", "-- written\n\nll.Say(0, \"hi\")", { { 4, 4, 2, 2 } });
        UNSCOPED_INFO("the LSL's call beside the SLua's");
        CHECK(beside(d, 4, 2));

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
        UNSCOPED_INFO("a character typed on the right");
        CHECK(d.right()->handleUnicodeChar('x', false));
        {
            INFO("told where: the right's line and column");
            CHECK(at_line == 2);
            CHECK(at_column == 2);
        }
        UNSCOPED_INFO("typed there instead");
        CHECK(source->text() == std::string("-- written\n\nllx.Say(0, \"hi\")"));
        {
            INFO("the comparison as it was");
            CHECK(d.right()->isReadOnly());
            CHECK(d.right()->text().find("llx") == std::string::npos);
        }

        // From the left, the line beside it, from its start; from a line
        // the right has none of, the next it has.
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(4, 5));
        UNSCOPED_INFO("a line broken on the left");
        CHECK(d.left()->handleKey(KEY_RETURN, MASK_NONE, false));
        {
            INFO("the right's line beside it, its start");
            CHECK(at_line == 2);
            CHECK(at_column == 0);
        }
        UNSCOPED_INFO("broken there");
        CHECK(source->text() == std::string("-- written\n\n\nllx.Say(0, \"hi\")"));
        d.left()->goTo(ALTextPos(0, 0));
        UNSCOPED_INFO("before any line of the right: its first after");
        CHECK(d.rightAtCaret().first == 0);

        // Nobody to tell: nothing typed anywhere.
        d.setOnEdit(nullptr);
        at_line = -1;
        d.right()->setFocus(true);
        d.right()->handleUnicodeChar('y', false);
        {
            INFO("nothing told, nothing typed");
            CHECK(at_line == -1);
            CHECK(source->text() == "-- written\n\n\nllx.Say(0, \"hi\")");
        }
        source->die();
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a copy takes a side's own text without the gaps that line it up", "[aldiffview]")
    {
        ALDiffView& d = make("one\nfour", "one\ntwo\nthree\nfour\nfive");
        UNSCOPED_INFO("the left as it is");
        CHECK(d.left()->text() == std::string("one\nfour"));
        {
            INFO("lined up by gaps");
            CHECK(gapsOf(d.left()) == std::vector<S32>{ 0, 2, 1 });
            CHECK(beside(d, 1, 3));
        }
        LLClipboard& clipboard = LLClipboard::instance();
        std::string  copied;

        d.left()->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 4)));
        d.left()->copy();
        clipboard.pasteFromClipboard(copied);
        UNSCOPED_INFO("all of the left, as its text has it");
        CHECK(copied == std::string("one\nfour"));

        d.left()->setSelection(ALTextRange(ALTextPos(0, 1), ALTextPos(1, 0)));
        d.left()->copy();
        clipboard.pasteFromClipboard(copied);
        UNSCOPED_INFO("over the gap: the line's break, which the text has");
        CHECK(copied == std::string("ne\n"));

        d.right()->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(4, 4)));
        d.right()->copy();
        clipboard.pasteFromClipboard(copied);
        UNSCOPED_INFO("the right, which has no gap, whole");
        CHECK(copied == std::string("one\ntwo\nthree\nfour\nfive"));
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: the bar counts the changes and steps them as Alt-Down and Alt-Up do", "[aldiffview]")
    {
        ALDiffView& d = make("one\ntwo\nthree\nfour\nfive\nsix", "one\n2\nthree\nfour\nfive\nsix\nseven");
        d.right()->setFocus(true);
        UNSCOPED_INFO("in none: how many");
        CHECK(d.bar()->countSaid() == std::string("2 changes"));
        {
            INFO("nothing before the first line, something after");
            CHECK_FALSE(enabled(d, "previous"));
            CHECK(enabled(d, "next"));
        }
        UNSCOPED_INFO("Alt-Down, let go by the side, which cannot move lines");
        CHECK(d.right()->handleKey(KEY_DOWN, MASK_ALT, false));
        UNSCOPED_INFO("the first change");
        CHECK(d.right()->caret().line == 1);
        UNSCOPED_INFO("in it");
        CHECK(d.bar()->countSaid() == std::string("Change 1 of 2"));
        {
            INFO("none before it");
            CHECK_FALSE(enabled(d, "previous"));
            CHECK(enabled(d, "next"));
        }
        press(d, "next");
        UNSCOPED_INFO("the next, from the bar");
        CHECK(d.right()->caret().line == 6);
        CHECK(d.bar()->countSaid() == std::string("Change 2 of 2"));
        {
            INFO("none after it");
            CHECK(enabled(d, "previous"));
            CHECK_FALSE(enabled(d, "next"));
        }
        UNSCOPED_INFO("the keyboard in the side");
        CHECK(d.right()->hasFocus());
        d.right()->goTo(ALTextPos(3, 0));
        UNSCOPED_INFO("between them: how many again");
        CHECK(d.bar()->countSaid() == std::string("2 changes"));
        {
            INFO("Alt-Up");
            CHECK(d.right()->handleKey(KEY_UP, MASK_ALT, false));
            CHECK(d.right()->caret().line == 1);
        }
        press(d, "previous");
        UNSCOPED_INFO("none before the first: where it was");
        CHECK(d.right()->caret().line == 1);

        d.setTexts("same", "same");
        UNSCOPED_INFO("none at all");
        CHECK(d.bar()->countSaid() == std::string("No changes"));
        {
            INFO("nowhere to go");
            CHECK_FALSE(enabled(d, "previous"));
            CHECK_FALSE(enabled(d, "next"));
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: the bar shows done only with an owner and lights its inline toggle", "[aldiffview]")
    {
        ALDiffView& d = make("a\nb", "a\nc");
        LLView*     done = d.bar()->getChild<LLView>("done");
        UNSCOPED_INFO("nobody to tell: no done");
        CHECK_FALSE(done->getVisible());
        S32 told = 0;
        d.setOnEscape([&told]() { ++told; });
        CHECK(done->getVisible());
        press(d, "done");
        UNSCOPED_INFO("told, as for Escape");
        CHECK(told == 1);

        S32 said = -1;
        d.setOnInline([&said](bool inlined) { said = inlined ? 1 : 0; });
        ALFlatButton* inlined = &require_button(d, "inline");
        UNSCOPED_INFO("side by side: not lit");
        CHECK_FALSE(inlined->getToggleState());
        inlined->press();
        {
            INFO("inline");
            CHECK(d.isInline());
            CHECK(said == 1);
            CHECK(inlined->getToggleState());
        }
        inlined->press();
        {
            INFO("and back");
            CHECK_FALSE(d.isInline());
            CHECK(said == 0);
            CHECK_FALSE(inlined->getToggleState());
        }
        d.setInline(true);
        UNSCOPED_INFO("lit as the view is, however it was turned");
        CHECK(inlined->getToggleState());
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: swapped shows the right on the left and typing still goes to the right", "[aldiffview]")
    {
        ALDiffView& d = make("a\nb = 1\nc", "a\nb = 2\nc\nd");
        d.setTitles("Saved", "Now");
        press(d, "swap");
        {
            INFO("swapped, and lit");
            CHECK(d.isSwapped());
            CHECK(require_button(d, "swap").getToggleState());
        }
        UNSCOPED_INFO("the right's text on the left");
        CHECK(d.left()->text() == std::string("a\nb = 2\nc\nd"));
        UNSCOPED_INFO("the left's on the right");
        CHECK(d.right()->text() == std::string("a\nb = 1\nc"));
        UNSCOPED_INFO("lined up, the gap below it");
        CHECK(gapsOf(d.right()) == std::vector<S32>{ 0, 0, 0, 1 });
        {
            INFO("its lines now the ones put in");
            CHECK(tinted(*d.right(), 1));
            CHECK_FALSE(tinted(*d.right(), 0));
        }
        UNSCOPED_INFO("the titles with them");
        CHECK(d.getChild<LLUICtrl>("left_title")->getValue().asString() == std::string("Now"));
        CHECK(d.getChild<LLUICtrl>("right_title")->getValue().asString() == std::string("Saved"));

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
        {
            INFO("from the side showing the right's text: its line and column");
            CHECK(at_line == 1);
            CHECK(at_column == 1);
        }
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(2, 1));
        d.right()->handleUnicodeChar('y', false);
        {
            INFO("from the other, the right's line beside it, its start");
            CHECK(at_line == 2);
            CHECK(at_column == 0);
        }

        d.setInline(true);
        UNSCOPED_INFO("inline, the right's lines taken out");
        CHECK(d.inlined()->text() == std::string("a\nb = 2\nb = 1\nc\nd"));
        d.inlined()->goTo(ALTextPos(1, 1));
        UNSCOPED_INFO("a line of the right's, inline, is its own");
        CHECK(d.rightAtCaret() == std::make_pair(1, 1));
        d.inlined()->goTo(ALTextPos(2, 1));
        UNSCOPED_INFO("one of the left's: the right's next, from its start");
        CHECK(d.rightAtCaret() == std::make_pair(2, 0));
        d.setInline(false);
        d.setSwapped(false);
        UNSCOPED_INFO("back as it was");
        CHECK(d.left()->text() == std::string("a\nb = 1\nc"));
        UNSCOPED_INFO("titles too");
        CHECK(d.getChild<LLUICtrl>("left_title")->getValue().asString() == std::string("Saved"));
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: inline and swapped keep the caret on the right's line and column", "[aldiffview]")
    {
        ALDiffView& d = make("one\ntwo\nthree\nfour", "one\n2\nthree\nfour\nfive");
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(2, 3));
        d.setInline(true);
        {
            INFO("inline: below the line taken out");
            CHECK(d.inlined()->caret().line == 3);
            CHECK(d.inlined()->caret().column == 3);
        }
        UNSCOPED_INFO("the keyboard with it");
        CHECK(d.inlined()->hasFocus());
        d.setInline(false);
        {
            INFO("side by side again");
            CHECK(d.right()->caret().line == 2);
            CHECK(d.right()->caret().column == 3);
        }
        d.setSwapped(true);
        {
            INFO("swapped: on the left, which shows the right's text");
            CHECK(d.left()->caret().line == 2);
            CHECK(d.left()->caret().column == 3);
        }
        UNSCOPED_INFO("the other side beside it");
        CHECK(d.right()->caret().line == 2);
        UNSCOPED_INFO("still the right's line");
        CHECK(d.rightAtCaret().first == 2);
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: vim change jumps step through the changes and Escape leaves the view", "[aldiffview]")
    {
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
        UNSCOPED_INFO("the first change");
        CHECK(d.right()->caret().line == 1);
        typed("]c");
        UNSCOPED_INFO("the next");
        CHECK(d.right()->caret().line == 4);
        typed("[c");
        UNSCOPED_INFO("back");
        CHECK(d.right()->caret().line == 1);
        typed("5]c");
        UNSCOPED_INFO("a count, as far as there are");
        CHECK(d.right()->caret().line == 7);
        UNSCOPED_INFO("nothing typed anywhere");
        CHECK(d.right()->text().find(']') == std::string::npos);
        UNSCOPED_INFO("j moves, as in a text that cannot be changed");
        CHECK((typed("gg"), typed("j"), d.right()->caret().line == 1));

        S32 told = 0;
        d.setOnEscape([&told]() { ++told; });
        typed("3");
        {
            INFO("Escape with a count begun lets the count go");
            CHECK(d.right()->handleKey(KEY_ESCAPE, MASK_NONE, false));
            CHECK(told == 0);
        }
        {
            INFO("then the comparison's");
            CHECK(d.right()->handleKey(KEY_ESCAPE, MASK_NONE, false));
            CHECK(told == 1);
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a long unchanged run folds past three lines of context and reopens", "[aldiffview]")
    {
        const std::string left  = lines(30);
        const std::string right = lines(30, { { 2, "two" }, { 27, "twenty-seven" } });
        ALDiffView&       d     = make(left.c_str(), right.c_str());
        {
            INFO("one fold, folded");
            CHECK(d.foldCount() == 1);
            CHECK(d.foldedCount() == 1);
        }
        // Each side's lines: 0-1 the same, 2 a change, 3-5 context, 6-23
        // what it hides, its row the gap above 24, 24-26 context, 27 a
        // change, 28-29 the same.
        for (ALCodeEditor* side : { d.left(), d.right() })
        {
            CAPTURE(side->getName());
            {
                INFO("context shown");
                REQUIRE_FALSE(hidden(side, 5));
                REQUIRE_FALSE(hidden(side, 24));
            }
            {
                INFO("the run hidden");
                REQUIRE(hidden(side, 6));
                REQUIRE(hidden(side, 15));
                REQUIRE(hidden(side, 23));
            }
            {
                INFO("its row a gap above the line after it");
                REQUIRE(side->layout().gapRows(24) == 1);
                REQUIRE(side->layout().gapRows(6) == 0);
            }
            UNSCOPED_INFO("the text as it is");
            REQUIRE(side->text() == (side == d.left() ? left : right));
        }
        UNSCOPED_INFO("lined up past it");
        CHECK(beside(d, 25, 25));
        LLClipboard& clipboard = LLClipboard::instance();
        std::string  copied;
        d.left()->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(29, 7)));
        d.left()->copy();
        clipboard.pasteFromClipboard(copied);
        UNSCOPED_INFO("a copy over it takes what it hides");
        CHECK(copied == left);

        d.right()->setFocus(true);
        d.handleKey(KEY_F7, MASK_NONE, false);
        d.handleKey(KEY_F7, MASK_NONE, false);
        UNSCOPED_INFO("the changes stepped through over it");
        CHECK(d.right()->caret().line == 27);

        // Down from the line over the run onto its row, on and back; Return
        // there opens it.
        ALCodeEditor* side = d.right();
        side->goTo(ALTextPos(5, 3));
        {
            INFO("down onto its row");
            CHECK(side->perform(ALEditorCommand::MoveDown));
            CHECK(side->caretGap() == 24);
            CHECK(side->caret() == ALTextPos(24, 0));
        }
        {
            INFO("on down, to the line under it, as far across");
            CHECK(side->perform(ALEditorCommand::MoveDown));
            CHECK(side->caretGap() == -1);
            CHECK(side->caret() == ALTextPos(24, 3));
        }
        {
            INFO("up onto it again");
            CHECK(side->perform(ALEditorCommand::MoveUp));
            CHECK(side->caretGap() == 24);
        }
        {
            INFO("and on up over it");
            CHECK(side->perform(ALEditorCommand::MoveUp));
            CHECK(side->caretGap() == -1);
            CHECK(side->caret() == ALTextPos(5, 3));
        }
        side->perform(ALEditorCommand::MoveDown);
        UNSCOPED_INFO("Return on its row");
        CHECK(side->handleKey(KEY_RETURN, MASK_NONE, false));
        {
            INFO("opened, both sides");
            CHECK(d.foldedCount() == 0);
            CHECK_FALSE(hidden(d.left(), 6));
            CHECK_FALSE(hidden(side, 23));
        }
        {
            INFO("its row gone, the caret on the first line it hid");
            CHECK(side->layout().gapRows(24) == 0);
            CHECK(side->caret().line == 6);
            CHECK(side->caretGap() == -1);
        }

        press(d, "fold");
        {
            INFO("the bar: folding off, nothing folded");
            CHECK_FALSE(d.foldsSame());
            CHECK(d.foldedCount() == 0);
        }
        side->goTo(ALTextPos(12, 0));
        press(d, "fold");
        {
            INFO("and on: folded again");
            CHECK(d.foldsSame());
            CHECK(d.foldedCount() == 1);
            CHECK(hidden(d.left(), 6));
        }
        UNSCOPED_INFO("the caret, on a line folded away, under the run's row");
        CHECK(side->caret().line == 24);

        // Unfolding on the line beside it, as a fold of a script is.
        UNSCOPED_INFO("unfolding offered under its row");
        CHECK(side->canPerform(ALEditorCommand::Unfold));
        CHECK(side->perform(ALEditorCommand::Unfold));
        {
            INFO("opened, the caret where it was");
            CHECK(d.foldedCount() == 0);
            CHECK(side->layout().gapRows(24) == 0);
            CHECK(side->caret().line == 24);
        }
        UNSCOPED_INFO("nothing more to unfold");
        CHECK_FALSE(side->canPerform(ALEditorCommand::Unfold));
        d.setFoldSame(false);
        d.setFoldSame(true);

        d.left()->goTo(ALTextPos(12, 0));
        {
            INFO("the caret landing in it opens it, both sides");
            CHECK(d.foldedCount() == 0);
            CHECK_FALSE(hidden(d.right(), 12));
            CHECK(d.left()->caret().line == 12);
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a fold at the start has its row above the context and opens on a click", "[aldiffview]")
    {
        const std::string left  = lines(20);
        const std::string right = lines(20, { { 19, "nineteen" } });
        ALDiffView&       d     = make(left.c_str(), right.c_str());
        UNSCOPED_INFO("one, at the start");
        CHECK(d.foldCount() == 1);
        {
            INFO("no context before it, its row the gap above the context after");
            CHECK(hidden(d.right(), 0));
            CHECK(hidden(d.right(), 15));
            CHECK_FALSE(hidden(d.right(), 16));
        }
        {
            INFO("the gap");
            CHECK(d.right()->layout().gapRows(16) == 1);
            CHECK(d.right()->layout().lineTop(16) == d.right()->layout().rowHeight());
        }

        ALCodeEditor* side  = d.right();
        const LLRect  frame = side->getRect();
        const LLRect  text  = side->textRect();
        const S32     y     = frame.mBottom + text.mTop - (side->layout().gapTop(16) - side->scrollY()) - side->layout().rowHeight() / 2;
        UNSCOPED_INFO("the point in its gap");
        CHECK(side->gapAtLocal(y - frame.mBottom) == 16);
        UNSCOPED_INFO("a click on its row");
        CHECK(d.handleMouseDown(frame.mLeft + text.mLeft + 10, y, MASK_NONE));
        {
            INFO("opened, the caret on its first line, the side with the keyboard");
            CHECK(d.foldedCount() == 0);
            CHECK(side->caret().line == 0);
            CHECK(side->hasFocus());
        }
        UNSCOPED_INFO("a click on a line of text is the side's");
        CHECK((d.handleMouseDown(frame.mLeft + text.mLeft + 10, y, MASK_NONE), d.foldedCount() == 0));

        d.setInline(true);
        {
            INFO("inline: as open as it was");
            CHECK(d.foldCount() == 1);
            CHECK(d.foldedCount() == 0);
            CHECK_FALSE(hidden(d.inlined(), 1));
        }
        d.setFoldSame(true);
        {
            INFO("folded, inline");
            CHECK(hidden(d.inlined(), 1));
            CHECK_FALSE(hidden(d.inlined(), 16));
            CHECK(d.inlined()->layout().gapRows(16) == 1);
        }
        d.inlined()->goTo(ALTextPos(16, 0));
        d.setInline(false);
        {
            INFO("side by side, folded, the caret under its row");
            CHECK(d.foldedCount() == 1);
            CHECK(d.right()->caret().line == 16);
        }
        d.setSwapped(true);
        {
            INFO("swapped, folded still");
            CHECK(d.foldedCount() == 1);
            CHECK(hidden(d.left(), 1));
            CHECK(hidden(d.right(), 1));
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: each side's ruler marks its own changes and the other's beside a gap", "[aldiffview]")
    {
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
        {
            INFO("nothing the same marked");
            CHECK_FALSE(marked(d.left(), 0));
            CHECK_FALSE(marked(d.right(), 0));
            CHECK_FALSE(marked(d.right(), 2));
            CHECK_FALSE(marked(d.left(), 3));
        }
        {
            INFO("a line changed: taken out on the left, put in on the right");
            CHECK(red(d.left(), 1));
            CHECK_FALSE(red(d.right(), 1));
        }
        {
            INFO("a line put in: on the right, and on the left by its gap");
            CHECK(marked(d.right(), 3));
            CHECK_FALSE(red(d.right(), 3));
            CHECK(gap_marked(d.left(), 3));
        }
        d.setInline(true);
        {
            INFO("inline: the line taken out, and the two put in");
            CHECK(red(d.inlined(), 1));
            CHECK_FALSE(red(d.inlined(), 2));
            CHECK_FALSE(red(d.inlined(), 4));
            CHECK_FALSE(marked(d.inlined(), 3));
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a sign beside each number marks a line changed or taken out or put in", "[aldiffview]")
    {
        ALDiffView& d = make("a\nb = 1\nc\ne\nf", "a\nb = 2\nc\nd\ne");
        UNSCOPED_INFO("the left: the line changed, the one taken out");
        CHECK(signsOf(d.left()) == std::string("\0~\0\0-", 5));
        UNSCOPED_INFO("the right: the line changed, the one put in");
        CHECK(signsOf(d.right()) == std::string("\0~\0+\0", 5));
        d.setInline(true);
        UNSCOPED_INFO("inline: taken out and put in");
        CHECK(signsOf(d.inlined()) == std::string("\0-+\0+\0-", 7));
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a new right is compared again keeping the caret's line and the open runs", "[aldiffview]")
    {
        const std::string left  = lines(30);
        const std::string right = lines(30, { { 2, "two" }, { 27, "twenty-seven" } });
        ALDiffView&       d     = make(left.c_str(), right.c_str());
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(10, 0));
        UNSCOPED_INFO("the run opened");
        CHECK(d.foldedCount() == 0);
        // The right's line 25: context below the run.
        d.right()->goTo(ALTextPos(25, 2));
        CHECK(d.rightAtCaret() == std::make_pair(25, 2));
        d.setRightText("inserted\n" + right);
        UNSCOPED_INFO("compared again: a change more");
        CHECK(d.changeCount() == 3);
        UNSCOPED_INFO("the caret on the same line, one further down");
        CHECK(d.rightAtCaret() == std::make_pair(26, 2));
        {
            INFO("the run as open as it was");
            CHECK(d.foldCount() == 1);
            CHECK(d.foldedCount() == 0);
        }
        d.setRightText(right);
        d.setFoldSame(true);
        d.right()->goTo(ALTextPos(24, 0));
        d.setRightText("inserted\n" + right);
        {
            INFO("under a folded row: under it again, folded");
            CHECK(d.foldedCount() == 1);
            CHECK(d.right()->caret().line == 25);
            CHECK(d.right()->layout().gapRows(25) == 1);
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: taking a change back is one edit of the right's text then compared again", "[aldiffview]")
    {
        ALDiffView& d = make("a\nb\nc\nd\ne", "a\nB\nc\nx\ny\nd");
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name               = "source";
        p.rect               = LLRect(0, 100, 300, 0);
        ALCodeEditor* source = LLUICtrlFactory::create<ALCodeEditor>(p);
        source->setText("a\nB\nc\nx\ny\nd");
        {
            INFO("nothing offered without anyone to make it");
            CHECK_FALSE(d.canTakeBack());
            CHECK_FALSE(d.takeBack(0));
            CHECK_FALSE(d.bar()->getChild<LLView>("take_back")->getVisible());
        }
        const S32 narrow = d.right()->getRect().mLeft - d.left()->getRect().mRight;
        S32       asked  = 0;
        d.setOnTakeBack([&](const ALTextRange& range, const std::string& text) {
            ++asked;
            return source->replaceAll({ { range, text } });
        });
        {
            INFO("offered");
            CHECK(d.canTakeBack());
            CHECK(d.bar()->getChild<LLView>("take_back")->getVisible());
        }
        UNSCOPED_INFO("a gap wide enough for the arrows");
        CHECK(d.right()->getRect().mLeft - d.left()->getRect().mRight > narrow);
        CHECK(d.changeCount() == 3);

        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(4, 0));
        UNSCOPED_INFO("the lines put in taken out");
        CHECK(d.takeBack(d.changeAtCaret()));
        UNSCOPED_INFO("the right's text");
        CHECK(source->text() == std::string("a\nB\nc\nd"));
        UNSCOPED_INFO("compared again");
        CHECK(d.rightText() == source->text());
        UNSCOPED_INFO("a change fewer");
        CHECK(d.changeCount() == 2);
        UNSCOPED_INFO("the caret where they were");
        CHECK(d.rightAtCaret().first == 3);
        UNSCOPED_INFO("the line taken out at the end put back");
        CHECK(d.takeBack(1));
        UNSCOPED_INFO("after the last line");
        CHECK(source->text() == std::string("a\nB\nc\nd\ne"));
        UNSCOPED_INFO("the line changed made as it was");
        CHECK(d.takeBack(0));
        UNSCOPED_INFO("all as the left");
        CHECK(source->text() == std::string("a\nb\nc\nd\ne"));
        {
            INFO("nothing left to take back");
            CHECK(d.changeCount() == 0);
            CHECK_FALSE(d.takeBack(0));
        }
        UNSCOPED_INFO("each one edit");
        CHECK(asked == 3);
        source->die();
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: take back works swapped and when refused and from an arrow in the gap", "[aldiffview]")
    {
        ALDiffView& d = make("one\ntwo\nthree", "one\nx\ny\nthree");
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name               = "source";
        p.rect               = LLRect(0, 100, 300, 0);
        ALCodeEditor* source = LLUICtrlFactory::create<ALCodeEditor>(p);
        source->setText("one\nx\ny\nthree");
        bool allow = false;
        d.setOnTakeBack([&](const ALTextRange& range, const std::string& text) { return allow && source->replaceAll({ { range, text } }); });
        {
            INFO("refused");
            CHECK_FALSE(d.takeBack(0));
            CHECK(d.changeCount() == 1);
            CHECK(d.rightText() == "one\nx\ny\nthree");
        }
        d.setSwapped(true);
        allow = true;
        {
            INFO("swapped, the right's two lines made the left's one");
            CHECK(d.takeBack(0));
            CHECK(source->text() == "one\ntwo\nthree");
            CHECK(d.changeCount() == 0);
        }

        d.setSwapped(false);
        d.setTexts("one\nx = 2\nthree", "one\nx = two\nthree");
        source->setText("one\nx = two\nthree");
        const LLRect frame = d.right()->getRect();
        const LLRect text  = d.right()->textRect();
        const S32    row_h = d.right()->layout().lineHeight(1);
        const S32    y     = frame.mBottom + text.mTop - (d.right()->layout().lineTop(1) - d.right()->scrollY()) - row_h / 2;
        const S32    gap_x = (d.left()->getRect().mRight + frame.mLeft) / 2;
        d.handleMouseDown(gap_x, y + row_h, MASK_NONE);
        UNSCOPED_INFO("beside a line the same, nothing");
        CHECK(d.changeCount() == 1);
        UNSCOPED_INFO("the arrow pressed");
        CHECK(d.handleMouseDown(gap_x, y, MASK_NONE));
        {
            INFO("taken back");
            CHECK(d.changeCount() == 0);
            CHECK(source->text() == "one\nx = 2\nthree");
        }
        source->die();
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a side neither folds nor wraps so its rows stay lined up", "[aldiffview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
            CAPTURE(each->getName());
            {
                INFO("no blocks to fold");
                REQUIRE_FALSE(each->foldable());
                REQUIRE(each->foldRegions().empty());
            }
            {
                INFO("no command folds");
                REQUIRE_FALSE(each->canPerform(ALEditorCommand::FoldAll));
                REQUIRE_FALSE(each->canPerform(ALEditorCommand::Fold));
            }
            each->foldAll();
            UNSCOPED_INFO("nothing hidden");
            REQUIRE_FALSE(each->layout().anyHidden());
            UNSCOPED_INFO("unwrapped");
            REQUIRE_FALSE(each->getWordWrap());
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: ignoring whitespace matches re-indented lines and case needs offering", "[aldiffview]")
    {
        ALDiffView& d = make("default\n{\nstate_entry()\n{\nllSay(0, \"a\");\n}\n}", "default\n{\n    state_entry()\n    {\n        llSay(0,  \"b\");\n    }\n}");
        UNSCOPED_INFO("as they are: the re-indented lines changed");
        CHECK(d.changeCount() == 1);
        CHECK_FALSE(d.ignores("whitespace"));
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(static_cast<S32>(numbersOf(d.right()).size()) - 1, 0));
        const S32 line = d.rightAtCaret().first;
        d.setIgnore("whitespace", true);
        {
            INFO("on, and the bar's button lit");
            CHECK(d.ignores("whitespace"));
            CHECK(require_button(d, "ignore").getToggleState());
        }
        UNSCOPED_INFO("one line changed: the word, not its indent");
        CHECK(d.changeCount() == 1);
        UNSCOPED_INFO("the right shown as it is");
        CHECK(d.right()->text().find("\n    state_entry()\n") != std::string::npos);
        UNSCOPED_INFO("the caret kept");
        CHECK(d.rightAtCaret().first == line);
        const S32 say = 4;
        UNSCOPED_INFO("the changed line's word marked, not its blanks");
        CHECK(d.right()->decorations().size() == 1);
        UNSCOPED_INFO("the line before it the same");
        CHECK_FALSE(tinted(*d.right(), say - 1));

        UNSCOPED_INFO("case not offered");
        CHECK_FALSE(d.offersIgnore("case"));
        d.setIgnore("case", true);
        UNSCOPED_INFO("nor taken");
        CHECK_FALSE(d.ignores("case"));
        d.setTexts("Hello there", "hello there");
        UNSCOPED_INFO("a change of case a change");
        CHECK(d.changeCount() == 1);
        d.setOffersIgnoreCase(true);
        CHECK(d.offersIgnore("case"));
        d.setIgnore("case", true);
        {
            INFO("let go of");
            CHECK(d.ignores("case"));
            CHECK(d.changeCount() == 0);
        }
        d.setOffersIgnoreCase(false);
        {
            INFO("not offered: not let go of");
            CHECK_FALSE(d.ignores("case"));
            CHECK(d.changeCount() == 1);
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: the sides scroll together down and across and inline scrolls alone", "[aldiffview]")
    {
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
        UNSCOPED_INFO("the right followed the left down");
        CHECK(d.right()->scrollY() == 300);
        d.right()->setScrollY(120);
        UNSCOPED_INFO("and the left the right");
        CHECK(d.left()->scrollY() == 120);
        d.right()->setScrollX(40.f);
        UNSCOPED_INFO("across");
        CHECK(d.left()->scrollX() == 40.f);
        d.left()->goTo(ALTextPos(180, 0));
        d.left()->scrollToCaret();
        {
            INFO("the caret kept in sight on one side: the other with it");
            CHECK(d.left()->scrollY() > 300);
            CHECK(d.right()->scrollY() == d.left()->scrollY());
        }
        d.setInline(true);
        const S32 left_at = d.left()->scrollY();
        d.inlined()->setScrollY(10);
        UNSCOPED_INFO("inline, the sides not shown left where they were");
        CHECK(d.left()->scrollY() == left_at);
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: the bar counts for the side that has the keyboard", "[aldiffview]")
    {
        ALDiffView& d = make("one\ntwo\nthree\nfour\nfive\nsix", "one\n2\nthree\nfour\nfive\nsix\nseven");
        d.right()->setFocus(true);
        d.left()->goTo(ALTextPos(1, 0));
        d.right()->goTo(ALTextPos(3, 0));
        UNSCOPED_INFO("the right in front, between changes");
        CHECK(d.bar()->countSaid() == std::string("2 changes"));
        d.left()->setFocus(true);
        UNSCOPED_INFO("the keyboard to the left: its change");
        CHECK(d.bar()->countSaid() == std::string("Change 1 of 2"));
        d.right()->setFocus(true);
        UNSCOPED_INFO("and back");
        CHECK(d.bar()->countSaid() == std::string("2 changes"));
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a moved block gets its own tint and a sign that goes to its other end", "[aldiffview]")
    {
        const std::string block = "llOwnerSay(\"a block of lines\");\nllOwnerSay(\"moved as one\");";
        const std::string left  = block + "\nstay one\nstay two\nstay three";
        const std::string right = "stay one\nstay two\nstay three\n" + block;
        ALDiffView&       d     = make(left.c_str(), right.c_str());
        const LLColor4&   moved = d.left()->lineAnnotation(0).tint;
        const LLColor4&   gone  = d.left()->lineAnnotation(0).rulerTint;
        {
            INFO("tinted, as neither red nor green");
            CHECK(moved.mV[VALPHA] > 0.f);
            CHECK(moved.mV[VBLUE] > moved.mV[VRED]);
            CHECK(moved.mV[VBLUE] > moved.mV[VGREEN]);
        }
        UNSCOPED_INFO("marked on the ruler likewise");
        CHECK(gone.mV[VBLUE] > gone.mV[VRED]);
        UNSCOPED_INFO("signed");
        CHECK(signsOf(d.left()) == std::string(">>\0\0\0", 5));

        // A click in the left's gutter beside its second line moved.
        ALCodeEditor* side  = d.left();
        const LLRect  frame = side->getRect();
        const LLRect  text  = side->textRect();
        const S32     row_h = side->layout().lineHeight(1);
        const S32     y     = frame.mBottom + text.mTop - (side->layout().lineTop(1) - side->scrollY()) - row_h / 2;
        UNSCOPED_INFO("taken");
        CHECK(d.handleMouseDown(frame.mLeft + text.mLeft / 2, y, MASK_NONE));
        {
            INFO("the right's caret on the other end, which has the keyboard");
            CHECK(d.right()->caret().line == 4);
            CHECK(d.right()->hasFocus());
        }
        // In the text rather than the gutter: not taken as a step.
        d.left()->setFocus(true);
        d.handleMouseDown(frame.mLeft + text.mLeft + 20, y, MASK_NONE);
        {
            INFO("a click in the text is the text's");
            CHECK(d.right()->caret().line == 4);
            CHECK_FALSE(d.right()->hasFocus());
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: the ignore menu offers comments with a grammar and turns on its own item", "[aldiffview]")
    {
        ALDiffView& d = make("x = 1; // one\n\ny = 2;", "x = 1; // uno\ny = 2;   ");
        // A menu holder, as the viewer's window has one.
        LLMenuHolderGL::Params hp;
        hp.name                  = "menu_holder";
        hp.rect                  = LLRect(0, 1080, 1920, 0);
        hp.mouse_opaque          = false;
        LLMenuHolderGL* holder   = LLUICtrlFactory::create<LLMenuHolderGL>(hp);
        std::optional<ll_test::MenuContainerScope> menus;
        menus.emplace(holder);
        press(d, "ignore");
        LLContextMenu* menu = holder->findChild<LLContextMenu>("menu_diff_ignore");
        UNSCOPED_INFO("shown");
        REQUIRE(menu != nullptr);
        menu->arrangeAndClear();
        LLMenuItemGL* comments = menu->findChild<LLMenuItemGL>("comments");
        LLMenuItemGL* letter_case = menu->findChild<LLMenuItemGL>("case");
        LLMenuItemGL* trailing = menu->findChild<LLMenuItemGL>("trailing");
        REQUIRE(comments != nullptr);
        REQUIRE(letter_case != nullptr);
        REQUIRE(trailing != nullptr);
        UNSCOPED_INFO("comments offered, with LSL's grammar");
        CHECK(comments->getVisible());
        UNSCOPED_INFO("case not, for code");
        CHECK_FALSE(letter_case->getVisible());
        trailing->onCommit();
        {
            INFO("an item turns its own on");
            CHECK(d.ignores("trailing"));
            CHECK_FALSE(d.ignores("whitespace"));
        }
        d.setIgnore("comments", true);
        d.setIgnore("blank_lines", true);
        UNSCOPED_INFO("a comment reworded, a blank line gone and blanks at an end: no change");
        CHECK(d.changeCount() == 0);
        const ALFlatButton& ignore = require_button(d, "ignore");
        UNSCOPED_INFO("lit");
        CHECK(ignore.getToggleState());
        menus.reset();
        LLMortician::updateClass();
        delete holder;
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: LSL beside SLua links the range at the caret and shows converter notes", "[aldiffview]")
    {
        const char* lsl  = "default\n{\n    touch_start(integer d)\n    {\n        if (d > 1) llSay(0, \"many\");\n        llSay(0, \"one\");\n    }\n}";
        const char* slua = "LLEvents:on(\"touch_start\", function(detected)\n    local d = #detected\n    if d > 1 then\n        ll.Say(0, \"many\")\n    end\n"
                           "    ll.Say(0, \"one\")\nend)";
        ALDiffView& d = make("", "");
        d.setTexts(lsl, slua, { { 0, 7, 0, 6 }, { 2, 6, 0, 6 }, { 4, 4, 2, 4 }, { 4, 4, 3, 3 }, { 5, 5, 5, 5 } });
        UNSCOPED_INFO("nothing yet: the caret at the right's top, in the state");
        CHECK(d.linkedRange() == 0);
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(4, 0));
        UNSCOPED_INFO("an LSL if: linked to its SLua");
        CHECK(d.linkedRange() == 2);
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(5, 0));
        UNSCOPED_INFO("a SLua call: linked to its LSL");
        CHECK(d.linkedRange() == 4);
        d.setInline(true);
        UNSCOPED_INFO("inline: none");
        CHECK(d.linkedRange() == -1);
        d.setInline(false);

        d.setNotes({ { 4, "LSL: integer division rounds toward zero", "the whole note" } });
        UNSCOPED_INFO("beside the LSL line");
        CHECK(d.left()->noteAt(4).find("integer division") != std::string::npos);
        UNSCOPED_INFO("not on the SLua");
        CHECK(d.right()->noteAt(4).empty());
        const S32 inlined = d.model().lineShowing(ALDiffModel::Column::Inline, true, 4);
        {
            INFO("inline, beside where the line is");
            CHECK(inlined >= 0);
            CHECK_FALSE(d.inlined()->noteAt(inlined).empty());
        }
        d.setRightText(std::string("-- one more\n") + slua);
        UNSCOPED_INFO("kept as the right is made anew");
        CHECK_FALSE(d.left()->noteAt(4).empty());
        d.setTexts(lsl, slua);
        UNSCOPED_INFO("let go of with new texts");
        CHECK(d.left()->noteAt(4).empty());
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: vim search and command lines typed over a side never reach the source", "[aldiffview]")
    {
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
                {
                    INFO("the keyboard in the comparison");
                    REQUIRE(in);
                    REQUIRE((in == &d || d.hasChild(in->getName(), true)));
                }
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
        UNSCOPED_INFO("found");
        CHECK(d.right()->caret().line == 4);
        UNSCOPED_INFO("nothing handed to the source");
        CHECK(edits == 0);
        {
            INFO("still the comparison's side, with the keyboard");
            CHECK(d.right()->hasFocus());
            CHECK(d.right()->getVisible());
        }
        typed("?thr\n");
        UNSCOPED_INFO("back");
        CHECK(d.right()->caret().line == 2);
        typed(":3\n");
        {
            INFO("a : line too");
            CHECK(d.right()->caret().line == 2);
            CHECK(edits == 0);
        }
        d.left()->setFocus(true);
        typed("/fou\n");
        {
            INFO("from the left");
            CHECK(d.left()->caret().line == 3);
            CHECK(edits == 0);
        }
        d.setInline(true);
        d.inlined()->setFocus(true);
        typed("/six\n");
        {
            INFO("inline");
            CHECK(d.inlined()->caret().line == d.inlined()->document().lineCount() - 1);
            CHECK(edits == 0);
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a merge counts its conflicts and settles one with theirs or mine or both", "[aldiffview]")
    {
        // Theirs changed lines 0 and 6, ours lines 2 and 6; begun with
        // theirs's line 0 taken.
        const std::string base   = lines(8);
        const std::string theirs = lines(8, { { 0, "theirs 0" }, { 6, "theirs 6" } });
        const std::string ours   = lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" } });
        ALDiffView&       d      = make(theirs.c_str(), ours.c_str());
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name               = "source";
        p.rect               = LLRect(0, 100, 300, 0);
        ALCodeEditor* source = LLUICtrlFactory::create<ALCodeEditor>(p);
        S32           asked  = 0;
        d.setOnTakeBack([&](const ALTextRange& range, const std::string& text) {
            ++asked;
            return source->replaceAll({ { range, text } });
        });
        UNSCOPED_INFO("no buttons to settle before a merge");
        CHECK_FALSE(d.bar()->getChild<LLView>("take_theirs")->getVisible());
        const auto begin = [&]() {
            d.setTexts(theirs, ours);
            source->setText(ours);
            d.setMergeBase(base);
        };
        begin();
        {
            INFO("merging");
            CHECK(d.merging());
            CHECK(d.conflictCount() == 1);
        }
        {
            INFO("the buttons shown");
            CHECK(d.bar()->getChild<LLView>("take_theirs")->getVisible());
            CHECK(d.bar()->getChild<LLView>("keep_both")->getVisible());
        }
        UNSCOPED_INFO("the count says one left");
        CHECK(d.bar()->countSaid().find("1 conflict left") != std::string::npos);
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(2, 0));
        {
            INFO("not lit in ours's own change");
            CHECK_FALSE(enabled(d, "take_theirs"));
            CHECK_FALSE(enabled(d, "keep_mine"));
            CHECK_FALSE(enabled(d, "keep_both"));
        }
        UNSCOPED_INFO("nor settled from it");
        CHECK_FALSE(d.settle(d.changeAtCaret(), ALTextMerge::Take::Theirs));
        d.right()->goTo(ALTextPos(6, 0));
        {
            INFO("lit in the conflict");
            CHECK(enabled(d, "take_theirs"));
            CHECK(enabled(d, "keep_mine"));
            CHECK(enabled(d, "keep_both"));
        }

        press(d, "keep_both");
        UNSCOPED_INFO("mine then theirs");
        CHECK(source->text() == lines(9, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" }, { 7, "theirs 6" }, { 8, "line 7" } }));
        UNSCOPED_INFO("compared again");
        CHECK(d.rightText() == source->text());
        {
            INFO("none left");
            CHECK(d.conflictCount() == 0);
            CHECK(d.bar()->countSaid().find("no conflicts left") != std::string::npos);
        }
        UNSCOPED_INFO("one edit");
        CHECK(asked == 1);

        begin();
        d.right()->goTo(ALTextPos(6, 0));
        press(d, "keep_mine");
        {
            INFO("mine: nothing edited");
            CHECK(asked == 1);
            CHECK(source->text() == ours);
            CHECK(d.rightText() == ours);
        }
        UNSCOPED_INFO("and settled");
        CHECK(d.conflictCount() == 0);

        begin();
        d.right()->goTo(ALTextPos(6, 0));
        press(d, "take_theirs");
        UNSCOPED_INFO("theirs");
        CHECK(source->text() == lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "theirs 6" } }));
        {
            INFO("settled");
            CHECK(asked == 2);
            CHECK(d.conflictCount() == 0);
        }

        d.setTexts(theirs, ours);
        {
            INFO("new texts let it go");
            CHECK_FALSE(d.merging());
            CHECK_FALSE(d.bar()->getChild<LLView>("take_theirs")->getVisible());
            CHECK(d.bar()->countSaid().find("conflict") == std::string::npos);
        }
        source->die();
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: the left's versions get a slider and steps and each one chosen is told", "[aldiffview]")
    {
        ALDiffView& d       = make("v0\nsame\nmore", "now\nsame\nmore");
        LLView*     slider  = d.bar()->getChild<LLView>("versions");
        {
            INFO("none at first");
            CHECK_FALSE(slider->getVisible());
            CHECK_FALSE(d.bar()->getChild<LLView>("older")->getVisible());
        }
        std::vector<S32> chosen;
        d.setVersions(1, 0, [&](S32 version) { chosen.push_back(version); });
        UNSCOPED_INFO("not for one");
        CHECK_FALSE(slider->getVisible());
        d.setVersions(3, 1, [&](S32 version) {
            chosen.push_back(version);
            d.setLeftText("v" + std::to_string(version) + "\nsame\nmore");
        });
        {
            INFO("shown for three");
            CHECK(slider->getVisible());
            CHECK(d.bar()->getChild<LLView>("older")->getVisible());
            CHECK(d.bar()->versionShown() == 1);
        }
        {
            INFO("both steps");
            CHECK(enabled(d, "older"));
            CHECK(enabled(d, "newer"));
        }

        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(2, 2));
        press(d, "newer");
        {
            INFO("the newest told");
            REQUIRE(chosen.size() == 1);
            CHECK(chosen[0] == 2);
        }
        UNSCOPED_INFO("made the left");
        CHECK(d.leftText() == std::string("v2\nsame\nmore"));
        UNSCOPED_INFO("the caret kept on its line of the right");
        CHECK(d.rightAtCaret() == std::make_pair(2, 2));
        {
            INFO("no newer than the newest");
            CHECK_FALSE(enabled(d, "newer"));
            CHECK(enabled(d, "older"));
        }
        press(d, "older");
        press(d, "older");
        {
            INFO("then the oldest");
            REQUIRE(chosen.size() == 3);
            CHECK(chosen[2] == 0);
            CHECK(d.leftText() == "v0\nsame\nmore");
            CHECK_FALSE(enabled(d, "older"));
        }
        LLSlider* bar = dynamic_cast<LLSlider*>(slider);
        UNSCOPED_INFO("a slider");
        REQUIRE(bar != nullptr);
        bar->setValue(2.f);
        bar->onCommit();
        {
            INFO("the slider let go at the newest");
            REQUIRE(chosen.size() == 4);
            CHECK(chosen[3] == 2);
            CHECK(d.leftText() == "v2\nsame\nmore");
        }
        bar->onCommit();
        UNSCOPED_INFO("let go where it was: nothing told");
        CHECK(chosen.size() == 4);

        d.setTexts("a", "b");
        UNSCOPED_INFO("new texts let them go");
        CHECK_FALSE(slider->getVisible());
        press(d, "newer");
        UNSCOPED_INFO("nothing told");
        CHECK(chosen.size() == 4);
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a change copies from the side in front and the whole as a unified diff", "[aldiffview]")
    {
        ALDiffView& d = make("a\nb\nc", "a\nB\nc");
        d.setTitles("old", "new");
        UNSCOPED_INFO("a copy button on the bar");
        CHECK(d.bar()->getChild<LLView>("copy")->getVisible());
        const auto clipboard = []() {
            std::string text;
            LLClipboard::instance().pasteFromClipboard(text);
            return text;
        };
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(1, 0));
        {
            INFO("from the right");
            CHECK(d.copyChange(d.changeAtCaret()));
            CHECK(clipboard() == "B\n");
        }
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(1, 0));
        {
            INFO("from the left");
            CHECK(d.copyChange(d.changeAtCaret()));
            CHECK(clipboard() == "b\n");
        }
        d.setInline(true);
        d.inlined()->setFocus(true);
        {
            INFO("inline, what is taken out");
            CHECK(d.copyChange(0));
            CHECK(clipboard() == "b\n");
        }
        d.setSwapped(true);
        {
            INFO("swapped, what is taken out is the right's");
            CHECK(d.copyChange(0));
            CHECK(clipboard() == "B\n");
        }

        UNSCOPED_INFO("as a unified diff, swapped: from the right");
        CHECK(d.unifiedDiff() ==
                      std::string("--- new\n+++ old\n@@ -1,3 +1,3 @@\n a\n-B\n+b\n c\n\\ No newline at end of file\n"));
        d.setSwapped(false);
        d.setInline(false);
        CHECK(d.copyUnifiedDiff());
        UNSCOPED_INFO("from the left");
        CHECK(clipboard() == std::string("--- old\n+++ new\n@@ -1,3 +1,3 @@\n a\n-b\n+B\n c\n\\ No newline at end of file\n"));

        // Lines put in alone: nothing of them on the left to copy.
        d.setTexts("a\nc", "a\nb\nc");
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(1, 0));
        UNSCOPED_INFO("the caret under the gap is in it");
        CHECK(d.changeAtCaret() == 0);
        LLClipboard::instance().copyToClipboard(std::string_view("kept"), 0, 4);
        {
            INFO("nothing copied");
            CHECK_FALSE(d.copyChange(0));
            CHECK(clipboard() == "kept");
        }
        d.setTexts("same", "same");
        {
            INFO("the same: no diff");
            CHECK(d.unifiedDiff().empty());
            CHECK_FALSE(d.copyUnifiedDiff());
            CHECK(clipboard() == "kept");
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: the layout not shown fills when shown and an unchanged side is not reset", "[aldiffview]")
    {
        ALDiffView& d = make(lines(40).c_str(), lines(40, { { 5, "five" } }).c_str());
        UNSCOPED_INFO("side by side: inline not filled yet");
        CHECK(d.inlined()->text().empty());
        d.setInline(true);
        UNSCOPED_INFO("filled as it is shown");
        CHECK(d.inlined()->text() == lines(5) + "\nline 5\nfive\n" + lines(40).substr(lines(6).size() + 1));
        UNSCOPED_INFO("its folds");
        CHECK(hidden(d.inlined(), 20));
        d.setInline(false);

        // Typed in the right far down: the left's text the same, its runs
        // folded otherwise -- line 35 now beside the change's context.
        {
            INFO("folded to the end");
            CHECK(hidden(d.left(), 35));
            CHECK(hidden(d.left(), 20));
        }
        const U32 left_was = d.left()->document().version();
        d.setRightText(lines(40, { { 5, "five" }, { 30, "thirty" } }));
        UNSCOPED_INFO("the left not put in again");
        CHECK(d.left()->document().version() == left_was);
        {
            INFO("its lines folded as the runs now are");
            CHECK_FALSE(hidden(d.left(), 35));
            CHECK(hidden(d.left(), 20));
            CHECK_FALSE(hidden(d.left(), 30));
        }
        {
            INFO("the right's too");
            CHECK_FALSE(hidden(d.right(), 35));
            CHECK(hidden(d.right(), 20));
        }
        d.setInline(true);
        {
            INFO("inline filled again as it is shown");
            CHECK(d.inlined()->text().find("thirty") != std::string::npos);
            CHECK_FALSE(hidden(d.inlined(), 38));
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a new left ends the merge and no grammar stops ignoring comments", "[aldiffview]")
    {
        const std::string base   = lines(8);
        const std::string theirs = lines(8, { { 6, "theirs 6" } });
        const std::string ours   = lines(8, { { 6, "mine 6" } });
        ALDiffView&       d      = make(theirs.c_str(), ours.c_str());
        d.setOnTakeBack([](const ALTextRange&, const std::string&) { return true; });
        d.setTexts(theirs, ours, { { 6, 6, 6, 6 } });
        d.setMergeBase(base);
        {
            INFO("merging");
            CHECK(d.merging());
            CHECK(d.bar()->getChild<LLView>("take_theirs")->getVisible());
            CHECK(d.model().ranges().size() == 1);
        }
        d.setLeftText(lines(8, { { 6, "older 6" } }));
        {
            INFO("merging no longer, the bar too");
            CHECK_FALSE(d.merging());
            CHECK_FALSE(d.bar()->getChild<LLView>("take_theirs")->getVisible());
            CHECK(d.bar()->countSaid().find("conflict") == std::string::npos);
        }
        UNSCOPED_INFO("the ranges let go");
        CHECK(d.model().ranges().empty());

        d.setTexts("x = 1; // one", "x = 1; // two");
        d.setIgnore("comments", true);
        {
            INFO("comments let go of: no change, the bar lit");
            CHECK(d.ignores("comments"));
            CHECK(d.changeCount() == 0);
            CHECK(require_button(d, "ignore").getToggleState());
        }
        d.setGrammar(nullptr);
        {
            INFO("no grammar: not let go of, nor offered, the bar unlit");
            CHECK_FALSE(d.ignores("comments"));
            CHECK_FALSE(d.offersIgnore("comments"));
            CHECK(d.changeCount() == 1);
            CHECK_FALSE(require_button(d, "ignore").getToggleState());
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a lone CR breaks lines for the comparison as for its editors", "[aldiffview]")
    {
        ALDiffView& d = make("a\rb\rc\rd", "a\rb\rC\rd");
        UNSCOPED_INFO("four lines a side");
        CHECK(d.left()->document().lineCount() == 4);
        {
            INFO("the third tinted, its neighbours not");
            CHECK(tinted(*d.left(), 2));
            CHECK_FALSE(tinted(*d.left(), 1));
            CHECK_FALSE(tinted(*d.left(), 3));
            CHECK(tinted(*d.right(), 2));
        }
        d.setRightText("a\rb\rC\rd\re");
        {
            INFO("typed in: the line put in the fifth");
            CHECK(tinted(*d.right(), 4));
            CHECK_FALSE(tinted(*d.right(), 3));
            CHECK(d.right()->document().lineCount() == 5);
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a unified diff leaves out the comment changes being ignored", "[aldiffview]")
    {
        ALDiffView& d = make("a = 1; // one\nb = 2;\n", "a = 1; // two\nb = 3;\n");
        UNSCOPED_INFO("as they are, the comment said");
        CHECK(d.unifiedDiff().find("-a = 1; // one\n") != std::string::npos);
        d.setIgnore("comments", true);
        const std::string diff = d.unifiedDiff();
        UNSCOPED_INFO("the change still said");
        CHECK(diff.find("-b = 2;\n+b = 3;\n") != std::string::npos);
        {
            INFO("the comment reworded the same, as the right has it");
            CHECK(diff.find("-a = 1;") == std::string::npos);
            CHECK(diff.find("\n a = 1; // two\n") != std::string::npos);
        }
        d.setTexts("x = 1; // one", "x = 1; // two");
        {
            INFO("a comment reworded alone: nothing to copy");
            CHECK(d.changeCount() == 0);
            CHECK(d.unifiedDiff().empty());
        }
        d.setTexts("x = 1;\n", "x = 1;\n// a note put in\n");
        {
            INFO("a line of comment put in: nothing to copy");
            CHECK(d.changeCount() == 0);
            CHECK(d.unifiedDiff().empty());
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: filling only what was laid out again matches filling whole through edits", "[aldiffview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        U32        seed = 20261006;
        const auto next = [&seed](U32 below) {
            seed = seed * 1103515245U + 12345U;
            return below ? (seed >> 16) % below : 0U;
        };
        // Lines of code, braces and blank lines among them, changed at a
        // line, lines put in or taken out, a block moved; at the start and
        // at the end too.
        std::vector<std::string> base;
        for (S32 n = 0; n < 120; ++n)
        {
            base.push_back(n % 9 == 0 ? std::string("}") : n % 11 == 0 ? std::string() : "x = " + std::to_string(n) + ";");
        }
        const auto joined = [](const std::vector<std::string>& lines) {
            std::string out;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                out += (i ? "\n" : "") + lines[i];
            }
            return out;
        };
        const auto edited = [&](std::vector<std::string> lines) {
            const size_t at = next(static_cast<U32>(lines.size() + 1));
            switch (next(5))
            {
                case 0:
                    if (at < lines.size())
                    {
                        lines[at] += " + 1";
                    }
                    break;
                case 1:
                    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), next(3) + 1, "y = " + std::to_string(next(1000)) + ";");
                    break;
                case 2:
                    if (at < lines.size() && lines.size() > 4)
                    {
                        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(at), lines.begin() + static_cast<std::ptrdiff_t>(std::min(lines.size(), at + next(3) + 1)));
                    }
                    break;
                case 3:
                    if (lines.size() > 30)
                    {
                        const size_t             from = next(static_cast<U32>(lines.size() - 12));
                        std::vector<std::string> block(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(from + 8));
                        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(from + 8));
                        const size_t to = next(static_cast<U32>(lines.size()));
                        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(to), block.begin(), block.end());
                    }
                    break;
                default:
                    // The last line, or the first, said otherwise.
                    lines[next(2) ? lines.size() - 1 : 0] += " // end";
                    break;
            }
            return lines;
        };
        ALDiffView& kept  = make("", "");
        ALDiffView& whole = makeWhole();
        // Each edit an editor of the one kept is given: a stretch of it, or
        // all of it.
        S32                                            stretches = 0;
        S32                                            edits     = 0;
        std::vector<boost::signals2::scoped_connection> heard;
        for (ALCodeEditor* side : { kept.left(), kept.right(), kept.inlined() })
        {
            heard.emplace_back(side->document().onChanged([&, side](const ALTextDocument::Edit& edit) {
                S32 had = side->document().lineCount();
                for (const ALTextDocument::Edit::LineSpan& span : edit.lineSpans())
                {
                    had += span.last - span.first + 1 - span.made;
                }
                const ALTextDocument::Edit::LineSpan& span = edit.lineSpans().front();
                ++edits;
                stretches += span.first > 0 || span.last < had - 1 ? 1 : 0;
            }));
        }
        for (S32 way = 0; way < 6; ++way)
        {
            ALTextDiff::Likeness like;
            like.ignoreBlankLines = way == 2;
            std::vector<std::string> left  = edited(edited(base));
            std::vector<std::string> right = edited(edited(edited(base)));
            ALTextDiff::ranges_t     ranges;
            if (way == 5)
            {
                // Anchored as a conversion is, a stretch at a time.
                for (S32 n = 0; n < 40; n += 4)
                {
                    ranges.push_back({ n, n + 1, n, n + 1 });
                }
            }
            for (ALDiffView* each : { &kept, &whole })
            {
                each->setInline(way == 1);
                each->setLikeness(like);
                each->setAlgorithm(way == 4 ? ALTextDiff::Algorithm::Structural : ALTextDiff::Algorithm::Histogram);
                each->setTexts(joined(left), joined(right), ranges);
                each->setSwapped(way == 3);
            }
            for (S32 step = 0; step < 40; ++step)
            {
                // The right typed in, mostly; the left another now and then.
                const bool on_left = way != 5 && next(4) == 0;
                (on_left ? left : right) = edited(on_left ? left : right);
                const S32 reveal = static_cast<S32>(next(6));
                for (ALDiffView* each : { &kept, &whole })
                {
                    if (on_left)
                    {
                        each->setLeftText(joined(left));
                    }
                    else
                    {
                        each->setRightText(joined(right));
                    }
                    if (step % 7 == 6)
                    {
                        // The other layout, filled as it is shown.
                        each->setInline(!each->isInline());
                    }
                }
                ALCodeEditor* front = kept.isInline() ? kept.inlined() : kept.right();
                if (reveal == 0)
                {
                    // A run folded opened, as the caret landing in it does.
                    for (S32 line = 0; line < front->document().lineCount(); ++line)
                    {
                        if (front->layout().hidden(line))
                        {
                            for (ALDiffView* each : { &kept, &whole })
                            {
                                (each->isInline() ? each->inlined() : each->right())->goTo(ALTextPos(line, 0));
                            }
                            break;
                        }
                    }
                }
                CAPTURE(way, step);
                require_same_shown(kept, whole, "edited");
            }
        }
        UNSCOPED_INFO("filled again a stretch at a time, mostly");
        CHECK(stretches * 2 > edits);
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a stretch laid out again at the text's end matches a whole layout", "[aldiffview]")
    {
        ALDiffView& kept  = make("", "");
        ALDiffView& whole = makeWhole();
        // Whether the last rebuild laid out again only a stretch, at the
        // end of a column.
        const auto at_end = [&](ALDiffModel::Column column) {
            const ALDiffModel::Relaid& relaid = kept.model().relaid();
            const size_t               c      = static_cast<size_t>(column);
            return !relaid.whole && relaid.first[c] + relaid.now[c] == kept.model().lineCount(column);
        };
        const auto both = [&](const std::function<void(ALDiffView&)>& done) {
            done(kept);
            done(whole);
        };

        // Lines put in at the end of the right, and taken out again: the
        // left's rows below its last line as many as they are.
        both([](ALDiffView& d) { d.setTexts(aldiffview_data::lines(40, { { 5, "five" } }), aldiffview_data::lines(40, { { 5, "FIVE" } })); });
        both([](ALDiffView& d) { d.setRightText(aldiffview_data::lines(40, { { 5, "FIVE" } }) + "\nput in\nand again"); });
        UNSCOPED_INFO("lines put in: a stretch at the end laid out again");
        CHECK(at_end(ALDiffModel::Column::Left));
        require_same_shown(kept, whole, "put in at the end");
        UNSCOPED_INFO("the left's rows below its last");
        CHECK(kept.left()->layout().gapRows(40) == 2);
        both([](ALDiffView& d) { d.setRightText(aldiffview_data::lines(40, { { 5, "FIVE" } })); });
        UNSCOPED_INFO("lines taken out: a stretch at the end laid out again");
        CHECK(at_end(ALDiffModel::Column::Left));
        require_same_shown(kept, whole, "taken out again");
        {
            INFO("none now but the row of the run folded to the end");
            CHECK(kept.left()->layout().gapRows(40) == 1);
            CHECK(kept.left()->lineAnnotation(40).gapStop);
        }

        // Inline, the left's last line another: the stretch begins with the
        // line taken out, the edit takes the line before it from its end,
        // and that line's number is said again.
        both([](ALDiffView& d) {
            d.setInline(true);
            d.setTexts(aldiffview_data::lines(40, { { 5, "five" } }), aldiffview_data::lines(40, { { 5, "five" }, { 39, "thirty-nine" } }));
        });
        both([](ALDiffView& d) { d.setLeftText(aldiffview_data::lines(40, { { 5, "five" }, { 39, "line 39!" } })); });
        UNSCOPED_INFO("the left's last line: a stretch at the end laid out again");
        CHECK(at_end(ALDiffModel::Column::Inline));
        require_same_shown(kept, whole, "the left's last line");
        // Typed in a line put in at the end: the edit takes the line before
        // it from its end.
        both([](ALDiffView& d) { d.setTexts(aldiffview_data::lines(40), aldiffview_data::lines(40) + "\nput in"); });
        both([](ALDiffView& d) { d.setRightText(aldiffview_data::lines(40) + "\nput in!"); });
        UNSCOPED_INFO("typed at the end: a stretch at the end laid out again");
        CHECK(at_end(ALDiffModel::Column::Inline));
        require_same_shown(kept, whole, "typed at the end");
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: typing far down relexes the edited side from the edit and not the other", "[aldiffview]")
    {
        const std::string left = lines(3000, { { 5, "five" } });
        ALDiffView&       kept = make(left.c_str(), lines(3000).c_str());
        ALDiffView&       whole = makeWhole();
        whole.setTexts(left, lines(3000));
        for (ALDiffView* each : { &kept, &whole })
        {
            for (ALCodeEditor* side : { each->left(), each->right() })
            {
                side->highlighter().tokens(2999);
            }
            each->setRightText(lines(3000, { { 2500, "typed" } }));
            each->right()->highlighter().tokens(2502);
            each->left()->highlighter().tokens(2999);
        }
        UNSCOPED_INFO("again: lexed from the edit");
        CHECK(kept.right()->highlighter().lastLexed() <= 3);
        UNSCOPED_INFO("the left not at all");
        CHECK(kept.left()->highlighter().lastLexed() == 0);
        UNSCOPED_INFO("whole: lexed from the top");
        CHECK(whole.right()->highlighter().lastLexed() > 2500);
        require_same_shown(kept, whole, "typed far down");
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: following another side's scroll copies only the axis that moved", "[aldiffview]")
    {
        std::string left;
        std::string right;
        for (S32 n = 0; n < 200; ++n)
        {
            const std::string line = "line " + std::to_string(n);
            left += (n ? "\n" : "") + line + " " + std::string(120, 'x');
            right += (n ? "\n" : "") + (n == 100 ? std::string("changed") : line);
        }
        ALDiffView& d = make(left.c_str(), right.c_str());
        d.setFoldSame(false);
        d.left()->setScrollX(40.f);
        UNSCOPED_INFO("the left scrolled across");
        CHECK(d.left()->scrollX() == 40.f);
        UNSCOPED_INFO("the right, with nothing past its edge, held at it");
        CHECK(d.right()->scrollX() == 0.f);
        d.right()->setScrollY(120);
        UNSCOPED_INFO("the left followed the right down");
        CHECK(d.left()->scrollY() == 120);
        UNSCOPED_INFO("and kept its place across");
        CHECK(d.left()->scrollX() == 40.f);
        d.right()->goTo(ALTextPos(180, 0));
        d.right()->scrollToCaret();
        {
            INFO("the caret kept in sight on the right: the left down with it");
            CHECK(d.right()->scrollY() > 120);
            CHECK(d.left()->scrollY() == d.right()->scrollY());
        }
        UNSCOPED_INFO("and still across");
        CHECK(d.left()->scrollX() == 40.f);
        d.left()->setScrollX(0.f);
        UNSCOPED_INFO("the left back to its edge: the right, there already, as it was");
        CHECK(d.right()->scrollX() == 0.f);
        d.left()->setScrollY(60);
        UNSCOPED_INFO("the right followed the left up");
        CHECK(d.right()->scrollY() == 60);
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: the bar's Alt keys work from a side as tips say and Tab goes to the bar", "[aldiffview]")
    {
        const std::string base   = lines(30);
        const std::string theirs = lines(30, { { 0, "theirs 0" }, { 20, "theirs 20" } });
        const std::string ours   = lines(30, { { 0, "theirs 0" }, { 2, "mine 2" }, { 20, "mine 20" } });
        ALDiffView&       d      = make(theirs.c_str(), ours.c_str());
        S32               typed  = 0;
        d.setOnEdit([&](S32, S32) -> LLView* {
            ++typed;
            return nullptr;
        });
        ll_test::KeyNamesScope key_names([](std::string_view name) { return std::string(name); });
        const auto tip = [&d](const char* name) { return d.bar()->getChild<LLView>(name)->getToolTip(); };
        const auto said = [](KEY key) { return LLKeyboard::stringFromAccelerator(MASK_ALT, key); };
        {
            INFO("each key said on its button's tip");
            CHECK(tip("fold").find(said('F')) != std::string::npos);
            CHECK(tip("swap").find(said('S')) != std::string::npos);
            CHECK(tip("take_theirs").find(said('T')) != std::string::npos);
            CHECK(tip("keep_mine").find(said('M')) != std::string::npos);
            CHECK(tip("keep_both").find(said('B')) != std::string::npos);
            CHECK(tip("older").find(said(',')) != std::string::npos);
            CHECK(tip("newer").find(said('.')) != std::string::npos);
        }

        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(2, 0));
        {
            INFO("folded at first");
            CHECK(d.foldsSame());
            CHECK(d.foldedCount() > 0);
        }
        UNSCOPED_INFO("Alt-F from a side");
        CHECK(d.right()->handleKey('F', MASK_ALT, false));
        {
            INFO("opened, and the button so");
            CHECK_FALSE(d.foldsSame());
            CHECK(d.foldedCount() == 0);
            CHECK_FALSE(require_button(d, "fold").getToggleState());
        }
        d.right()->handleKey('F', MASK_ALT, false);
        {
            INFO("and folded again");
            CHECK(d.foldsSame());
            CHECK(d.foldedCount() > 0);
        }
        {
            INFO("Alt-S");
            CHECK(d.right()->handleKey('S', MASK_ALT, false));
            CHECK(d.isSwapped());
        }
        d.right()->handleKey('S', MASK_ALT, false);
        UNSCOPED_INFO("and back");
        CHECK_FALSE(d.isSwapped());

        // Settling, only in a merge.
        UNSCOPED_INFO("Alt-T without a merge: not taken");
        CHECK_FALSE(d.right()->handleKey('T', MASK_ALT, false));
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name               = "source";
        p.rect               = LLRect(0, 100, 300, 0);
        ALCodeEditor* source = LLUICtrlFactory::create<ALCodeEditor>(p);
        d.setOnTakeBack([&](const ALTextRange& range, const std::string& text) { return source->replaceAll({ { range, text } }); });
        const auto begin = [&]() {
            d.setTexts(theirs, ours);
            source->setText(ours);
            d.setMergeBase(base);
            d.right()->setFocus(true);
            d.right()->goTo(ALTextPos(20, 0));
        };
        begin();
        {
            INFO("Alt-M");
            CHECK(d.right()->handleKey('M', MASK_ALT, false));
            CHECK(d.conflictCount() == 0);
            CHECK(source->text() == ours);
        }
        begin();
        {
            INFO("Alt-T");
            CHECK(d.right()->handleKey('T', MASK_ALT, false));
            CHECK(d.conflictCount() == 0);
            CHECK(source->text() == lines(30, { { 0, "theirs 0" }, { 2, "mine 2" }, { 20, "theirs 20" } }));
        }
        begin();
        std::string both = ours;
        both.replace(both.find("mine 20"), 7, "mine 20\ntheirs 20");
        {
            INFO("Alt-B");
            CHECK(d.right()->handleKey('B', MASK_ALT, false));
            CHECK(d.conflictCount() == 0);
            CHECK(source->text() == both);
        }
        UNSCOPED_INFO("the keyboard left on the side");
        CHECK(d.right()->hasFocus());

        // The versions, only where there are some.
        UNSCOPED_INFO("Alt-comma without versions: not taken");
        CHECK_FALSE(d.right()->handleKey(',', MASK_ALT, false));
        std::vector<S32> chosen;
        d.setVersions(3, 1, [&](S32 version) { chosen.push_back(version); });
        {
            INFO("Alt-comma: older");
            CHECK(d.right()->handleKey(',', MASK_ALT, false));
            CHECK(chosen == std::vector<S32>{ 0 });
            CHECK(d.bar()->versionShown() == 0);
        }
        {
            INFO("no older than the oldest");
            CHECK(d.right()->handleKey(',', MASK_ALT, false));
            CHECK(chosen.size() == 1);
        }
        {
            INFO("Alt-period: newer");
            CHECK(d.right()->handleKey('.', MASK_ALT, false));
            CHECK(chosen == std::vector<S32>({ 0, 1 }));
        }
        {
            INFO("and the same from the view");
            CHECK(d.stepVersion(1));
            CHECK(chosen.back() == 2);
            CHECK_FALSE(d.stepVersion(1));
        }

        // Tab from a side: to the bar, not typed into the source.
        const std::string was = source->text();
        UNSCOPED_INFO("Tab taken");
        CHECK(d.right()->handleKey(KEY_TAB, MASK_NONE, false));
        LLView* focus = dynamic_cast<LLView*>(gFocusMgr.getKeyboardFocus());
        {
            INFO("on a button of the bar");
            REQUIRE(focus);
            CHECK(focus->getParent() == d.bar());
            CHECK(ALViewType::as<ALFlatButton>(focus) != nullptr);
        }
        {
            INFO("nothing typed");
            CHECK(typed == 0);
            CHECK(source->text() == was);
        }
        d.left()->setFocus(true);
        UNSCOPED_INFO("Shift-Tab taken");
        CHECK(d.left()->handleKey(KEY_TAB, MASK_SHIFT, false));
        UNSCOPED_INFO("on the last button: swap, with no done");
        CHECK(gFocusMgr.getKeyboardFocus() == d.bar()->getChild<LLView>("swap"));
        {
            INFO("nothing typed still");
            CHECK(typed == 0);
            CHECK(source->text() == was);
        }
        source->die();
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a settling undone brings its conflict back and redone settles it again", "[aldiffview]")
    {
        const std::string base   = lines(8);
        const std::string theirs = lines(8, { { 0, "theirs 0" }, { 6, "theirs 6" } });
        const std::string ours   = lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" } });
        ALDiffView&       d      = make(theirs.c_str(), ours.c_str());
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name               = "source";
        p.rect               = LLRect(0, 100, 300, 0);
        ALCodeEditor* source = LLUICtrlFactory::create<ALCodeEditor>(p);
        d.setOnTakeBack([&](const ALTextRange& range, const std::string& text) { return source->replaceAll({ { range, text } }); });
        for (const char* button : { "take_theirs", "keep_both" })
        {
            CAPTURE(button);
            d.setTexts(theirs, ours);
            source->setText(ours);
            d.setMergeBase(base);
            d.right()->setFocus(true);
            d.right()->goTo(ALTextPos(6, 0));
            press(d, button);
            const std::string made = source->text();
            {
                INFO("settled");
                REQUIRE(made != ours);
                REQUIRE(d.conflictCount() == 0);
            }
            source->undo();
            UNSCOPED_INFO("undone in the source");
            REQUIRE(source->text() == ours);
            d.setRightText(source->text());
            UNSCOPED_INFO("the conflict back");
            REQUIRE(d.conflictCount() == 1);
            UNSCOPED_INFO("said on the bar");
            REQUIRE(d.bar()->countSaid().find("1 conflict left") != std::string::npos);
            d.right()->goTo(ALTextPos(6, 0));
            {
                INFO("and lit to settle again");
                REQUIRE(enabled(d, "take_theirs"));
                REQUIRE(d.canSettleAtCaret());
            }
            source->redo();
            UNSCOPED_INFO("redone in the source");
            REQUIRE(source->text() == made);
            d.setRightText(source->text());
            UNSCOPED_INFO("settled again");
            REQUIRE(d.conflictCount() == 0);
        }
        source->die();
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a narrowing bar gives way in turn and its tips say what it does and why", "[aldiffview]")
    {
        const std::string base   = lines(8);
        const std::string theirs = lines(8, { { 6, "theirs 6" } });
        const std::string ours   = lines(8, { { 6, "mine 6" } });
        ALDiffView&       d      = make(theirs.c_str(), ours.c_str());
        d.setOnEscape([]() {});
        d.setOnTakeBack([](const ALTextRange&, const std::string&) { return true; });
        d.setMergeBase(base);
        d.setVersions(3, 1, [](S32) {});
        LLView* count = d.bar()->getChild<LLView>("count");
        const ALFlatButton& take_theirs = require_button(d, "take_theirs");
        const auto nothing_past_left = [&d]() {
            for (LLView* child : *d.bar()->getChildList())
            {
                if (child->getVisible() && child->getRect().mLeft < 0)
                {
                    return false;
                }
            }
            return true;
        };
        d.reshape(1000, 300);
        {
            INFO("wide: all of it");
            CHECK(count->getVisible());
            CHECK(take_theirs.glyph() == "Theirs");
            CHECK(d.bar()->getChild<LLView>("versions")->getVisible());
        }
        S32 lost_count = 0;
        S32 lost_words = 0;
        S32 lost_slider = 0;
        for (S32 width = 1000; width >= 60; width -= 10)
        {
            d.reshape(width, 300);
            CAPTURE(width);
            UNSCOPED_INFO("nothing past the left edge");
            REQUIRE(nothing_past_left());
            lost_count  = !count->getVisible() && !lost_count ? width : lost_count;
            lost_words  = take_theirs.glyph() == "T" && !lost_words ? width : lost_words;
            lost_slider = !d.bar()->getChild<LLView>("versions")->getVisible() && !lost_slider ? width : lost_slider;
        }
        {
            INFO("each let go in turn");
            CHECK(lost_count > lost_words);
            CHECK(lost_words > lost_slider);
            CHECK(lost_slider > 0);
        }
        d.reshape(1000, 300);
        {
            INFO("and all back");
            CHECK(count->getVisible());
            CHECK(take_theirs.glyph() == "Theirs");
            CHECK(d.bar()->getChild<LLView>("versions")->getVisible());
            CHECK(d.bar()->getChild<LLView>("take_theirs")->getVisible());
            CHECK(d.bar()->getChild<LLView>("done")->getVisible());
        }

        LLView* inline_button = d.bar()->getChild<LLView>("inline");
        const std::string side_by_side = inline_button->getToolTip();
        press(d, "inline");
        {
            INFO("inline: the tip says the way back");
            CHECK(d.isInline());
            CHECK(inline_button->getToolTip() != side_by_side);
            CHECK(inline_button->getToolTip().find("side by side") != std::string::npos);
        }
        press(d, "inline");
        UNSCOPED_INFO("and back");
        CHECK(inline_button->getToolTip() == side_by_side);

        UNSCOPED_INFO("no reason while it compares as asked");
        CHECK(count->getToolTip().empty());
        d.bar()->setFellBack(true);
        const std::string said = d.bar()->countSaid();
        {
            INFO("by lines, briefly");
            REQUIRE(said.size() >= 8);
            CHECK(said.substr(said.size() - 8) == "by lines");
        }
        UNSCOPED_INFO("the reason in the tip");
        CHECK_FALSE(count->getToolTip().empty());
        d.bar()->setFellBack(false);
        UNSCOPED_INFO("let go");
        CHECK(count->getToolTip().empty());
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: runs opened while a layout was hidden are open when it is shown", "[aldiffview]")
    {
        typedef ALDiffModel::Column Column;
        const std::string left  = lines(100, { { 5, "five" }, { 50, "fifty" }, { 95, "ninety-five" } });
        const auto        right = [](const char* last) { return aldiffview_data::lines(100, { { 5, "FIVE" }, { 50, "FIFTY" }, { 95, last } }); };
        ALDiffView&       kept  = make(left.c_str(), right("NINETY-FIVE").c_str());
        ALDiffView&       whole = makeWhole();
        whole.setTexts(left, right("NINETY-FIVE"));
        const auto both = [&](const std::function<void(ALDiffView&)>& done) {
            done(kept);
            done(whole);
        };
        const ALDiffModel& model = kept.model();
        // Both layouts filled: inline, then side by side again. Two runs
        // folded, lines 9 to 46 and 54 to 91 of each side.
        both([](ALDiffView& d) {
            d.setInline(true);
            d.setInline(false);
        });
        CHECK(kept.foldedCount() == 2);

        // Typed in far down: side by side filled again there, inline left
        // as it was. The first run opened by the caret landing in it, the
        // caret back by the edit, and inline shown.
        both([&](ALDiffView& d) { d.setRightText(right("NINETY-FIVE!")); });
        UNSCOPED_INFO("laid out again in part");
        CHECK_FALSE(model.relaid().whole);
        both([](ALDiffView& d) {
            d.right()->goTo(ALTextPos(20, 0));
            d.right()->goTo(ALTextPos(95, 0));
        });
        {
            INFO("the first open, the second folded");
            CHECK(model.foldOpen(0));
            CHECK_FALSE(model.foldOpen(1));
        }
        both([](ALDiffView& d) { d.setInline(true); });
        const S32 first = model.foldFirstLine(Column::Inline, 0);
        {
            INFO("inline: its lines shown, its row gone");
            CHECK_FALSE(hidden(kept.inlined(), first));
            CHECK_FALSE(hidden(kept.inlined(), first + model.foldLines(0) - 1));
            CHECK(kept.inlined()->layout().gapRows(model.foldGapLine(Column::Inline, 0)) == 0);
        }
        UNSCOPED_INFO("the second still folded");
        CHECK(hidden(kept.inlined(), model.foldFirstLine(Column::Inline, 1)));
        require_same_shown(kept, whole, "a run opened side by side");

        // Typed in again inline, side by side left as it was, every run
        // opened as Alt-F does, and side by side shown.
        both([&](ALDiffView& d) { d.setRightText(right("NINETY-FIVE?")); });
        both([](ALDiffView& d) {
            d.setFoldSame(false);
            d.setInline(false);
        });
        {
            INFO("side by side: every run open");
            CHECK(kept.foldedCount() == 0);
            CHECK_FALSE(hidden(kept.left(), model.foldFirstLine(Column::Left, 1)));
            CHECK_FALSE(hidden(kept.right(), model.foldFirstLine(Column::Right, 1)));
            CHECK(kept.right()->layout().gapRows(model.foldGapLine(Column::Right, 1)) == 0);
        }
        require_same_shown(kept, whole, "every run opened inline");
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: notes stay by the left's line swapped or inline and go with a new left", "[aldiffview]")
    {
        ALDiffView& d = make("a\nb\nc\nd", "a\nB\nc\nd");
        d.setNotes({ { 2, "about c", "the tip" } });
        {
            INFO("on the left");
            CHECK(d.left()->noteAt(2) == "about c");
            CHECK(d.right()->noteAt(2).empty());
        }
        d.setSwapped(true);
        {
            INFO("swapped: on the right, none on the left");
            CHECK(d.right()->noteAt(2) == "about c");
            CHECK(d.left()->noteAt(2).empty());
        }
        d.setSwapped(false);
        {
            INFO("and back");
            CHECK(d.left()->noteAt(2) == "about c");
            CHECK(d.right()->noteAt(2).empty());
        }
        d.setRightText("a\nB\nput in\nc\nd");
        UNSCOPED_INFO("the right made anew: the left's as it was");
        CHECK(d.left()->noteAt(2) == "about c");
        d.setInline(true);
        const S32 at = d.model().lineShowing(ALDiffModel::Column::Inline, true, 2);
        {
            INFO("inline, beside where the line is");
            CHECK(at >= 0);
            CHECK(d.inlined()->noteAt(at) == "about c");
        }
        d.setRightText("a\nB\nput in\nmore\nc\nd");
        const S32 now = d.model().lineShowing(ALDiffModel::Column::Inline, true, 2);
        {
            INFO("and where it is after another edit, nowhere else");
            CHECK(now == at + 1);
            CHECK(d.inlined()->noteAt(now) == "about c");
            CHECK(d.inlined()->noteAt(at).empty());
        }
        d.setLeftText("a\nb\nc\nd!");
        UNSCOPED_INFO("a left of another version: let go of");
        CHECK(d.inlined()->noteAt(d.model().lineShowing(ALDiffModel::Column::Inline, true, 2)).empty());
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a change is copied only from a side in front that has lines of it", "[aldiffview]")
    {
        ALDiffView& d = make("one\ntwo\nthree", "one\nnew\ntwo\nthree");
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(1, 0));
        UNSCOPED_INFO("the left's line under the gap: in the change");
        CHECK(d.changeAtCaret() == 0);
        {
            INFO("nothing of it on the left to copy");
            CHECK_FALSE(d.canCopyChange());
            CHECK_FALSE(d.copyChange(d.changeAtCaret()));
        }
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(1, 0));
        {
            INFO("on the right, its line");
            CHECK(d.changeAtCaret() == 0);
            CHECK(d.canCopyChange());
        }
        d.right()->goTo(ALTextPos(2, 0));
        {
            INFO("in no change, nothing");
            CHECK(d.changeAtCaret() == -1);
            CHECK_FALSE(d.canCopyChange());
        }
        d.setSwapped(true);
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(1, 0));
        {
            INFO("swapped: the right's line on the left");
            CHECK(d.changeAtCaret() == 0);
            CHECK(d.canCopyChange());
        }
        d.setSwapped(false);
        d.setInline(true);
        d.inlined()->goTo(ALTextPos(1, 0));
        {
            INFO("inline, nothing taken out to copy");
            CHECK(d.changeAtCaret() == 0);
            CHECK_FALSE(d.canCopyChange());
            CHECK_FALSE(d.copyChange(0));
        }
    }

    TEST_CASE_METHOD(aldiffview_data, "aldiffview: a comparison's sides pin no sticky headers over their tops", "[aldiffview]")
    {
        ALDiffView& d = make("default\n{\n    state_entry()\n    {\n    }\n}\n", "default\n{\n    state_entry()\n    {\n        x();\n    }\n}\n");
        for (ALCodeEditor* side : { d.left(), d.right(), d.inlined() })
        {
            CAPTURE(side->getName());
            REQUIRE_FALSE(side->getStickyHeaders());
        }
    }
}
