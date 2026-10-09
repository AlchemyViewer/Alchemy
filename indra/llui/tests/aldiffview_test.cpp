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

#include "../test/lltut.h"

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace tut
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
        static void same(ALCodeEditor* a, ALCodeEditor* b, const std::string& where)
        {
            ensure(where + ": text", a->wholeText() == b->wholeText());
            const S32 count = a->document().lineCount();
            for (S32 l = 0; l <= count; ++l)
            {
                const ALTextView::LineAnnotation& x  = a->lineAnnotation(l);
                const ALTextView::LineAnnotation& y  = b->lineAnnotation(l);
                const std::string                 at = where + ", line " + std::to_string(l);
                ensure(at + ": what is said of it", x.number == y.number && x.sign == y.sign && x.tint == y.tint && x.rulerTint == y.rulerTint);
                ensure(at + ": the rows above it", x.gap == y.gap && x.gapTint == y.gapTint && x.gapRulerTint == y.gapRulerTint && x.gapStop == y.gapStop &&
                                                       a->layout().gapRows(l) == b->layout().gapRows(l));
                ensure(at + ": hidden or not", l == count || a->layout().hidden(l) == b->layout().hidden(l));
                ensure(at + ": no bar: nobody changed it", !a->lineChanged(l));
            }
            const std::vector<ALCodeEditor::Decoration>& x = a->decorations();
            const std::vector<ALCodeEditor::Decoration>& y = b->decorations();
            ensure_equals(where + ": words", x.size(), y.size());
            for (size_t i = 0; i < x.size(); ++i)
            {
                ensure(where + ": a word", x[i].range == y[i].range && x[i].color == y[i].color && x[i].style == y[i].style);
            }
            ensure(where + ": the caret", a->caret() == b->caret());
            ensure_equals(where + ": the height", a->layout().totalHeight(), b->layout().totalHeight());
        }
        static void sameShown(ALDiffView& a, ALDiffView& b, const std::string& where)
        {
            ensure_equals(where + ": the layout shown", a.isInline(), b.isInline());
            if (a.isInline())
            {
                same(a.inlined(), b.inlined(), where + ", inline");
            }
            else
            {
                same(a.left(), b.left(), where + ", left");
                same(a.right(), b.right(), where + ", right");
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
        ensure("off", !d.ignores("whitespace"));
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(static_cast<S32>(numbersOf(d.right()).size()) - 1, 0));
        const S32 line = d.rightAtCaret().first;
        d.setIgnore("whitespace", true);
        ensure("on, and the bar's button lit", d.ignores("whitespace") && ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>("ignore"))->getToggleState());
        ensure_equals("one line changed: the word, not its indent", d.changeCount(), 1);
        ensure("the right shown as it is", d.right()->text().find("\n    state_entry()\n") != std::string::npos);
        ensure("the caret kept", d.rightAtCaret().first == line);
        const S32 say = 4;
        ensure("the changed line's word marked, not its blanks", d.right()->decorations().size() == 1);
        ensure("the line before it the same", !tinted(*d.right(), say - 1));

        ensure("case not offered", !d.offersIgnore("case"));
        d.setIgnore("case", true);
        ensure("nor taken", !d.ignores("case"));
        d.setTexts("Hello there", "hello there");
        ensure_equals("a change of case a change", d.changeCount(), 1);
        d.setOffersIgnoreCase(true);
        ensure("offered", d.offersIgnore("case"));
        d.setIgnore("case", true);
        ensure("let go of", d.ignores("case") && d.changeCount() == 0);
        d.setOffersIgnoreCase(false);
        ensure("not offered: not let go of", !d.ignores("case") && d.changeCount() == 1);
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

    template<> template<>
    void aldiffview_object::test<28>()
    {
        set_test_name("a merge: the conflicts counted on the bar, the one the caret is in settled with theirs, mine or both; let go of with new texts");
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
        ensure("no buttons to settle before a merge", !d.bar()->getChild<LLView>("take_theirs")->getVisible());
        const auto begin = [&]() {
            d.setTexts(theirs, ours);
            source->setText(ours);
            d.setMergeBase(base);
        };
        begin();
        ensure("merging", d.merging() && d.conflictCount() == 1);
        ensure("the buttons shown", d.bar()->getChild<LLView>("take_theirs")->getVisible() && d.bar()->getChild<LLView>("keep_both")->getVisible());
        ensure("the count says one left", d.bar()->countSaid().find("1 conflict left") != std::string::npos);
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(2, 0));
        ensure("not lit in ours's own change", !enabled(d, "take_theirs") && !enabled(d, "keep_mine") && !enabled(d, "keep_both"));
        ensure("nor settled from it", !d.settle(d.changeAtCaret(), ALTextMerge::Take::Theirs));
        d.right()->goTo(ALTextPos(6, 0));
        ensure("lit in the conflict", enabled(d, "take_theirs") && enabled(d, "keep_mine") && enabled(d, "keep_both"));

        press(d, "keep_both");
        ensure_equals("mine then theirs", source->text(), lines(9, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" }, { 7, "theirs 6" }, { 8, "line 7" } }));
        ensure_equals("compared again", d.rightText(), source->text());
        ensure("none left", d.conflictCount() == 0 && d.bar()->countSaid().find("no conflicts left") != std::string::npos);
        ensure_equals("one edit", asked, 1);

        begin();
        d.right()->goTo(ALTextPos(6, 0));
        press(d, "keep_mine");
        ensure("mine: nothing edited", asked == 1 && source->text() == ours && d.rightText() == ours);
        ensure("and settled", d.conflictCount() == 0);

        begin();
        d.right()->goTo(ALTextPos(6, 0));
        press(d, "take_theirs");
        ensure_equals("theirs", source->text(), lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "theirs 6" } }));
        ensure("settled", asked == 2 && d.conflictCount() == 0);

        d.setTexts(theirs, ours);
        ensure("new texts let it go", !d.merging() && !d.bar()->getChild<LLView>("take_theirs")->getVisible() &&
                                          d.bar()->countSaid().find("conflict") == std::string::npos);
        source->die();
    }

    template<> template<>
    void aldiffview_object::test<29>()
    {
        set_test_name("the left's versions: a slider and steps on the bar for two or more, each chosen told; the left made another keeps the caret; new texts let them go");
        ALDiffView& d       = make("v0\nsame\nmore", "now\nsame\nmore");
        LLView*     slider  = d.bar()->getChild<LLView>("versions");
        ensure("none at first", !slider->getVisible() && !d.bar()->getChild<LLView>("older")->getVisible());
        std::vector<S32> chosen;
        d.setVersions(1, 0, [&](S32 version) { chosen.push_back(version); });
        ensure("not for one", !slider->getVisible());
        d.setVersions(3, 1, [&](S32 version) {
            chosen.push_back(version);
            d.setLeftText("v" + std::to_string(version) + "\nsame\nmore");
        });
        ensure("shown for three", slider->getVisible() && d.bar()->getChild<LLView>("older")->getVisible() && d.bar()->versionShown() == 1);
        ensure("both steps", enabled(d, "older") && enabled(d, "newer"));

        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(2, 2));
        press(d, "newer");
        ensure("the newest told", chosen.size() == 1 && chosen[0] == 2);
        ensure_equals("made the left", d.leftText(), std::string("v2\nsame\nmore"));
        ensure("the caret kept on its line of the right", d.rightAtCaret() == std::make_pair(2, 2));
        ensure("no newer than the newest", !enabled(d, "newer") && enabled(d, "older"));
        press(d, "older");
        press(d, "older");
        ensure("then the oldest", chosen.size() == 3 && chosen[2] == 0 && d.leftText() == "v0\nsame\nmore" && !enabled(d, "older"));
        LLSlider* bar = dynamic_cast<LLSlider*>(slider);
        ensure("a slider", bar != nullptr);
        bar->setValue(2.f);
        bar->onCommit();
        ensure("the slider let go at the newest", chosen.size() == 4 && chosen[3] == 2 && d.leftText() == "v2\nsame\nmore");
        bar->onCommit();
        ensure("let go where it was: nothing told", chosen.size() == 4);

        d.setTexts("a", "b");
        ensure("new texts let them go", !slider->getVisible());
        press(d, "newer");
        ensure("nothing told", chosen.size() == 4);
    }

    template<> template<>
    void aldiffview_object::test<30>()
    {
        set_test_name("a change's lines copied from the side in front, inline those taken out; the whole as a unified diff from the left as shown");
        ALDiffView& d = make("a\nb\nc", "a\nB\nc");
        d.setTitles("old", "new");
        ensure("a copy button on the bar", d.bar()->getChild<LLView>("copy")->getVisible());
        const auto clipboard = []() {
            std::string text;
            LLClipboard::instance().pasteFromClipboard(text);
            return text;
        };
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(1, 0));
        ensure("from the right", d.copyChange(d.changeAtCaret()) && clipboard() == "B\n");
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(1, 0));
        ensure("from the left", d.copyChange(d.changeAtCaret()) && clipboard() == "b\n");
        d.setInline(true);
        d.inlined()->setFocus(true);
        ensure("inline, what is taken out", d.copyChange(0) && clipboard() == "b\n");
        d.setSwapped(true);
        ensure("swapped, what is taken out is the right's", d.copyChange(0) && clipboard() == "B\n");

        ensure_equals("as a unified diff, swapped: from the right", d.unifiedDiff(),
                      std::string("--- new\n+++ old\n@@ -1,3 +1,3 @@\n a\n-B\n+b\n c\n\\ No newline at end of file\n"));
        d.setSwapped(false);
        d.setInline(false);
        ensure("copied", d.copyUnifiedDiff());
        ensure_equals("from the left", clipboard(), std::string("--- old\n+++ new\n@@ -1,3 +1,3 @@\n a\n-b\n+B\n c\n\\ No newline at end of file\n"));

        // Lines put in alone: nothing of them on the left to copy.
        d.setTexts("a\nc", "a\nb\nc");
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(1, 0));
        ensure_equals("the caret under the gap is in it", d.changeAtCaret(), 0);
        LLClipboard::instance().copyToClipboard(std::string_view("kept"), 0, 4);
        ensure("nothing copied", !d.copyChange(0) && clipboard() == "kept");
        d.setTexts("same", "same");
        ensure("the same: no diff", d.unifiedDiff().empty() && !d.copyUnifiedDiff() && clipboard() == "kept");
    }

    template<> template<>
    void aldiffview_object::test<31>()
    {
        set_test_name("the layout not shown filled as it is shown; a side whose text is as it was not put in again, its folds as they now are");
        ALDiffView& d = make(lines(40).c_str(), lines(40, { { 5, "five" } }).c_str());
        ensure("side by side: inline not filled yet", d.inlined()->text().empty());
        d.setInline(true);
        ensure_equals("filled as it is shown", d.inlined()->text(), lines(5) + "\nline 5\nfive\n" + lines(40).substr(lines(6).size() + 1));
        ensure("its folds", hidden(d.inlined(), 20));
        d.setInline(false);

        // Typed in the right far down: the left's text the same, its runs
        // folded otherwise -- line 35 now beside the change's context.
        ensure("folded to the end", hidden(d.left(), 35) && hidden(d.left(), 20));
        const U32 left_was = d.left()->document().version();
        d.setRightText(lines(40, { { 5, "five" }, { 30, "thirty" } }));
        ensure_equals("the left not put in again", d.left()->document().version(), left_was);
        ensure("its lines folded as the runs now are", !hidden(d.left(), 35) && hidden(d.left(), 20) && !hidden(d.left(), 30));
        ensure("the right's too", !hidden(d.right(), 35) && hidden(d.right(), 20));
        d.setInline(true);
        ensure("inline filled again as it is shown", d.inlined()->text().find("thirty") != std::string::npos && !hidden(d.inlined(), 38));
    }

    template<> template<>
    void aldiffview_object::test<32>()
    {
        set_test_name("another left lets a merge go, the bar's too, and the old left's ranges; a grammar gone lets comments go, the bar unlit");
        const std::string base   = lines(8);
        const std::string theirs = lines(8, { { 6, "theirs 6" } });
        const std::string ours   = lines(8, { { 6, "mine 6" } });
        ALDiffView&       d      = make(theirs.c_str(), ours.c_str());
        d.setOnTakeBack([](const ALTextRange&, const std::string&) { return true; });
        d.setTexts(theirs, ours, { { 6, 6, 6, 6 } });
        d.setMergeBase(base);
        ensure("merging", d.merging() && d.bar()->getChild<LLView>("take_theirs")->getVisible() && d.model().ranges().size() == 1);
        d.setLeftText(lines(8, { { 6, "older 6" } }));
        ensure("merging no longer, the bar too", !d.merging() && !d.bar()->getChild<LLView>("take_theirs")->getVisible() &&
                                                       d.bar()->countSaid().find("conflict") == std::string::npos);
        ensure("the ranges let go", d.model().ranges().empty());

        d.setTexts("x = 1; // one", "x = 1; // two");
        d.setIgnore("comments", true);
        ensure("comments let go of: no change, the bar lit",
               d.ignores("comments") && d.changeCount() == 0 && ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>("ignore"))->getToggleState());
        d.setGrammar(nullptr);
        ensure("no grammar: not let go of, nor offered, the bar unlit", !d.ignores("comments") && !d.offersIgnore("comments") && d.changeCount() == 1 &&
                                                                       !ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>("ignore"))->getToggleState());
    }

    template<> template<>
    void aldiffview_object::test<33>()
    {
        set_test_name("a lone CR a line break to the comparison as to its editors: each change tinted on the line it is on");
        ALDiffView& d = make("a\rb\rc\rd", "a\rb\rC\rd");
        ensure_equals("four lines a side", d.left()->document().lineCount(), 4);
        ensure("the third tinted, its neighbours not", tinted(*d.left(), 2) && !tinted(*d.left(), 1) && !tinted(*d.left(), 3) && tinted(*d.right(), 2));
        d.setRightText("a\rb\rC\rd\re");
        ensure("typed in: the line put in the fifth", tinted(*d.right(), 4) && !tinted(*d.right(), 3) && d.right()->document().lineCount() == 5);
    }

    template<> template<>
    void aldiffview_object::test<34>()
    {
        set_test_name("copied as a unified diff, what the comparison lets go of let go of: a comment reworded or put in none where comments are");
        ALDiffView& d = make("a = 1; // one\nb = 2;\n", "a = 1; // two\nb = 3;\n");
        ensure("as they are, the comment said", d.unifiedDiff().find("-a = 1; // one\n") != std::string::npos);
        d.setIgnore("comments", true);
        const std::string diff = d.unifiedDiff();
        ensure("the change still said", diff.find("-b = 2;\n+b = 3;\n") != std::string::npos);
        ensure("the comment reworded the same, as the right has it", diff.find("-a = 1;") == std::string::npos && diff.find("\n a = 1; // two\n") != std::string::npos);
        d.setTexts("x = 1; // one", "x = 1; // two");
        ensure("a comment reworded alone: nothing to copy", d.changeCount() == 0 && d.unifiedDiff().empty());
        d.setTexts("x = 1;\n", "x = 1;\n// a note put in\n");
        ensure("a line of comment put in: nothing to copy", d.changeCount() == 0 && d.unifiedDiff().empty());
    }

    template<> template<>
    void aldiffview_object::test<35>()
    {
        set_test_name("filled again only where a rebuild laid out again: each editor as one filled whole from the same comparison, edit after edit, either side, either layout, every way of comparing");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
                const std::string where = "way " + std::to_string(way) + ", step " + std::to_string(step);
                sameShown(kept, whole, where);
            }
        }
        ensure("filled again a stretch at a time, mostly", stretches * 2 > edits);
    }

    template<> template<>
    void aldiffview_object::test<36>()
    {
        set_test_name("filled again at the text's end: the rows below the last as they now are; the line before the stretch said again where the edit took it from its end");
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
        ensure("lines put in: a stretch at the end laid out again", at_end(ALDiffModel::Column::Left));
        sameShown(kept, whole, "put in at the end");
        ensure_equals("the left's rows below its last", kept.left()->layout().gapRows(40), 2);
        both([](ALDiffView& d) { d.setRightText(aldiffview_data::lines(40, { { 5, "FIVE" } })); });
        ensure("again", at_end(ALDiffModel::Column::Left));
        sameShown(kept, whole, "taken out again");
        ensure("none now but the row of the run folded to the end", kept.left()->layout().gapRows(40) == 1 && kept.left()->lineAnnotation(40).gapStop);

        // Inline, the left's last line another: the stretch begins with the
        // line taken out, the edit takes the line before it from its end,
        // and that line's number is said again.
        both([](ALDiffView& d) {
            d.setInline(true);
            d.setTexts(aldiffview_data::lines(40, { { 5, "five" } }), aldiffview_data::lines(40, { { 5, "five" }, { 39, "thirty-nine" } }));
        });
        both([](ALDiffView& d) { d.setLeftText(aldiffview_data::lines(40, { { 5, "five" }, { 39, "line 39!" } })); });
        ensure("the left's last line: a stretch at the end laid out again", at_end(ALDiffModel::Column::Inline));
        sameShown(kept, whole, "the left's last line");
        // Typed in a line put in at the end: the edit takes the line before
        // it from its end.
        both([](ALDiffView& d) { d.setTexts(aldiffview_data::lines(40), aldiffview_data::lines(40) + "\nput in"); });
        both([](ALDiffView& d) { d.setRightText(aldiffview_data::lines(40) + "\nput in!"); });
        ensure("typed at the end: a stretch at the end laid out again", at_end(ALDiffModel::Column::Inline));
        sameShown(kept, whole, "typed at the end");
    }

    template<> template<>
    void aldiffview_object::test<37>()
    {
        set_test_name("typed far down a long text: its editor's lines coloured again from the edit, not from the top, and the other side's not at all; the same of one filled whole");
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
        ensure("again: lexed from the edit", kept.right()->highlighter().lastLexed() <= 3);
        ensure("the left not at all", kept.left()->highlighter().lastLexed() == 0);
        ensure("whole: lexed from the top", whole.right()->highlighter().lastLexed() > 2500);
        sameShown(kept, whole, "typed far down");
    }

    template<> template<>
    void aldiffview_object::test<38>()
    {
        set_test_name("a side whose lines fit its width, scrolled down, leaves the other where it was scrolled across: following copies the axis that moved");
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
        ensure_equals("the left scrolled across", d.left()->scrollX(), 40.f);
        ensure_equals("the right, with nothing past its edge, held at it", d.right()->scrollX(), 0.f);
        d.right()->setScrollY(120);
        ensure_equals("the left followed the right down", d.left()->scrollY(), 120);
        ensure_equals("and kept its place across", d.left()->scrollX(), 40.f);
        d.right()->goTo(ALTextPos(180, 0));
        d.right()->scrollToCaret();
        ensure("the caret kept in sight on the right: the left down with it", d.right()->scrollY() > 120 && d.left()->scrollY() == d.right()->scrollY());
        ensure_equals("and still across", d.left()->scrollX(), 40.f);
        d.left()->setScrollX(0.f);
        ensure_equals("the left back to its edge: the right, there already, as it was", d.right()->scrollX(), 0.f);
        d.left()->setScrollY(60);
        ensure_equals("the right followed the left up", d.right()->scrollY(), 60);
    }

    template<> template<>
    void aldiffview_object::test<39>()
    {
        set_test_name("the bar's buttons from the keyboard: Alt-F folds, Alt-S swaps, Alt-T, Alt-M and Alt-B settle, Alt-comma and Alt-period step the versions, each said on its tip; Tab to the bar, not to the source");
        const std::string base   = lines(30);
        const std::string theirs = lines(30, { { 0, "theirs 0" }, { 20, "theirs 20" } });
        const std::string ours   = lines(30, { { 0, "theirs 0" }, { 2, "mine 2" }, { 20, "mine 20" } });
        ALDiffView&       d      = make(theirs.c_str(), ours.c_str());
        S32               typed  = 0;
        d.setOnEdit([&](S32, S32) -> LLView* {
            ++typed;
            return nullptr;
        });
        LLKeyboard::setStringTranslatorFunc([](std::string_view name) { return std::string(name); });
        const auto tip = [&d](const char* name) { return d.bar()->getChild<LLView>(name)->getToolTip(); };
        const auto said = [](KEY key) { return LLKeyboard::stringFromAccelerator(MASK_ALT, key); };
        ensure("each key said on its button's tip",
               tip("fold").find(said('F')) != std::string::npos && tip("swap").find(said('S')) != std::string::npos &&
                   tip("take_theirs").find(said('T')) != std::string::npos && tip("keep_mine").find(said('M')) != std::string::npos &&
                   tip("keep_both").find(said('B')) != std::string::npos && tip("older").find(said(',')) != std::string::npos &&
                   tip("newer").find(said('.')) != std::string::npos);

        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(2, 0));
        ensure("folded at first", d.foldsSame() && d.foldedCount() > 0);
        ensure("Alt-F from a side", d.right()->handleKey('F', MASK_ALT, false));
        ensure("opened, and the button so", !d.foldsSame() && d.foldedCount() == 0 && !ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>("fold"))->getToggleState());
        d.right()->handleKey('F', MASK_ALT, false);
        ensure("and folded again", d.foldsSame() && d.foldedCount() > 0);
        ensure("Alt-S", d.right()->handleKey('S', MASK_ALT, false) && d.isSwapped());
        d.right()->handleKey('S', MASK_ALT, false);
        ensure("and back", !d.isSwapped());

        // Settling, only in a merge.
        ensure("Alt-T without a merge: not taken", !d.right()->handleKey('T', MASK_ALT, false));
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
        ensure("Alt-M", d.right()->handleKey('M', MASK_ALT, false) && d.conflictCount() == 0 && source->text() == ours);
        begin();
        ensure("Alt-T", d.right()->handleKey('T', MASK_ALT, false) && d.conflictCount() == 0 && source->text() == lines(30, { { 0, "theirs 0" }, { 2, "mine 2" }, { 20, "theirs 20" } }));
        begin();
        std::string both = ours;
        both.replace(both.find("mine 20"), 7, "mine 20\ntheirs 20");
        ensure("Alt-B", d.right()->handleKey('B', MASK_ALT, false) && d.conflictCount() == 0 && source->text() == both);
        ensure("the keyboard left on the side", d.right()->hasFocus());

        // The versions, only where there are some.
        ensure("Alt-comma without versions: not taken", !d.right()->handleKey(',', MASK_ALT, false));
        std::vector<S32> chosen;
        d.setVersions(3, 1, [&](S32 version) { chosen.push_back(version); });
        ensure("Alt-comma: older", d.right()->handleKey(',', MASK_ALT, false) && chosen == std::vector<S32>{ 0 } && d.bar()->versionShown() == 0);
        ensure("no older than the oldest", d.right()->handleKey(',', MASK_ALT, false) && chosen.size() == 1);
        ensure("Alt-period: newer", d.right()->handleKey('.', MASK_ALT, false) && chosen == std::vector<S32>({ 0, 1 }));
        ensure("and the same from the view", d.stepVersion(1) && chosen.back() == 2 && !d.stepVersion(1));

        // Tab from a side: to the bar, not typed into the source.
        const std::string was = source->text();
        ensure("Tab taken", d.right()->handleKey(KEY_TAB, MASK_NONE, false));
        LLView* focus = dynamic_cast<LLView*>(gFocusMgr.getKeyboardFocus());
        ensure("on a button of the bar", focus && focus->getParent() == d.bar() && ALViewType::as<ALFlatButton>(focus) != nullptr);
        ensure("nothing typed", typed == 0 && source->text() == was);
        d.left()->setFocus(true);
        ensure("Shift-Tab taken", d.left()->handleKey(KEY_TAB, MASK_SHIFT, false));
        ensure("on the last button: swap, with no done", gFocusMgr.getKeyboardFocus() == d.bar()->getChild<LLView>("swap"));
        ensure("nothing typed still", typed == 0 && source->text() == was);
        source->die();
    }

    template<> template<>
    void aldiffview_object::test<40>()
    {
        set_test_name("a settling undone in the source and compared again: the conflict back; redone, settled again");
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
            d.setTexts(theirs, ours);
            source->setText(ours);
            d.setMergeBase(base);
            d.right()->setFocus(true);
            d.right()->goTo(ALTextPos(6, 0));
            press(d, button);
            const std::string made = source->text();
            ensure("settled", made != ours && d.conflictCount() == 0);
            source->undo();
            ensure_equals("undone in the source", source->text(), ours);
            d.setRightText(source->text());
            ensure_equals("the conflict back", d.conflictCount(), 1);
            ensure("said on the bar", d.bar()->countSaid().find("1 conflict left") != std::string::npos);
            d.right()->goTo(ALTextPos(6, 0));
            ensure("and lit to settle again", enabled(d, "take_theirs") && d.canSettleAtCaret());
            source->redo();
            ensure_equals("redone in the source", source->text(), made);
            d.setRightText(source->text());
            ensure_equals("settled again", d.conflictCount(), 0);
        }
        source->die();
    }

    template<> template<>
    void aldiffview_object::test<41>()
    {
        set_test_name("a narrow bar gives way: the count first, then the merge's words to letters, then the slider; nothing past its left edge. The inline toggle's tip says what it does now; the fallback's reason in the count's tip");
        const std::string base   = lines(8);
        const std::string theirs = lines(8, { { 6, "theirs 6" } });
        const std::string ours   = lines(8, { { 6, "mine 6" } });
        ALDiffView&       d      = make(theirs.c_str(), ours.c_str());
        d.setOnEscape([]() {});
        d.setOnTakeBack([](const ALTextRange&, const std::string&) { return true; });
        d.setMergeBase(base);
        d.setVersions(3, 1, [](S32) {});
        LLView* count = d.bar()->getChild<LLView>("count");
        const auto glyph = [&d](const char* name) { return ALViewType::as<ALFlatButton>(d.bar()->getChild<LLView>(name))->glyph(); };
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
        ensure("wide: all of it", count->getVisible() && glyph("take_theirs") == "Theirs" && d.bar()->getChild<LLView>("versions")->getVisible());
        S32 lost_count = 0;
        S32 lost_words = 0;
        S32 lost_slider = 0;
        for (S32 width = 1000; width >= 60; width -= 10)
        {
            d.reshape(width, 300);
            ensure("nothing past the left edge", nothing_past_left());
            lost_count  = !count->getVisible() && !lost_count ? width : lost_count;
            lost_words  = glyph("take_theirs") == "T" && !lost_words ? width : lost_words;
            lost_slider = !d.bar()->getChild<LLView>("versions")->getVisible() && !lost_slider ? width : lost_slider;
        }
        ensure("each let go in turn", lost_count > lost_words && lost_words > lost_slider && lost_slider > 0);
        d.reshape(1000, 300);
        ensure("and all back", count->getVisible() && glyph("take_theirs") == "Theirs" && d.bar()->getChild<LLView>("versions")->getVisible() &&
                                   d.bar()->getChild<LLView>("take_theirs")->getVisible() && d.bar()->getChild<LLView>("done")->getVisible());

        LLView* inline_button = d.bar()->getChild<LLView>("inline");
        const std::string side_by_side = inline_button->getToolTip();
        press(d, "inline");
        ensure("inline: the tip says the way back", d.isInline() && inline_button->getToolTip() != side_by_side &&
                                                        inline_button->getToolTip().find("side by side") != std::string::npos);
        press(d, "inline");
        ensure("and back", inline_button->getToolTip() == side_by_side);

        ensure("no reason while it compares as asked", count->getToolTip().empty());
        d.bar()->setFellBack(true);
        const std::string said = d.bar()->countSaid();
        ensure("by lines, briefly", said.size() >= 8 && said.substr(said.size() - 8) == "by lines");
        ensure("the reason in the tip", !count->getToolTip().empty());
        d.bar()->setFellBack(false);
        ensure("let go", count->getToolTip().empty());
    }

    template<> template<>
    void aldiffview_object::test<42>()
    {
        set_test_name("a run opened, or every run opened, while the layout not shown was behind the model: so there too when it is shown, though filled again only where it was laid out again");
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
        ensure_equals("two runs folded", kept.foldedCount(), 2);

        // Typed in far down: side by side filled again there, inline left
        // as it was. The first run opened by the caret landing in it, the
        // caret back by the edit, and inline shown.
        both([&](ALDiffView& d) { d.setRightText(right("NINETY-FIVE!")); });
        ensure("laid out again in part", !model.relaid().whole);
        both([](ALDiffView& d) {
            d.right()->goTo(ALTextPos(20, 0));
            d.right()->goTo(ALTextPos(95, 0));
        });
        ensure("the first open, the second folded", model.foldOpen(0) && !model.foldOpen(1));
        both([](ALDiffView& d) { d.setInline(true); });
        const S32 first = model.foldFirstLine(Column::Inline, 0);
        ensure("inline: its lines shown, its row gone", !hidden(kept.inlined(), first) && !hidden(kept.inlined(), first + model.foldLines(0) - 1) &&
                                                         kept.inlined()->layout().gapRows(model.foldGapLine(Column::Inline, 0)) == 0);
        ensure("the second still folded", hidden(kept.inlined(), model.foldFirstLine(Column::Inline, 1)));
        sameShown(kept, whole, "a run opened side by side");

        // Typed in again inline, side by side left as it was, every run
        // opened as Alt-F does, and side by side shown.
        both([&](ALDiffView& d) { d.setRightText(right("NINETY-FIVE?")); });
        both([](ALDiffView& d) {
            d.setFoldSame(false);
            d.setInline(false);
        });
        ensure("side by side: every run open", kept.foldedCount() == 0 && !hidden(kept.left(), model.foldFirstLine(Column::Left, 1)) &&
                                                   !hidden(kept.right(), model.foldFirstLine(Column::Right, 1)) &&
                                                   kept.right()->layout().gapRows(model.foldGapLine(Column::Right, 1)) == 0);
        sameShown(kept, whole, "every run opened inline");
    }

    template<> template<>
    void aldiffview_object::test<43>()
    {
        set_test_name("the converter's notes beside the left's line whichever side shows it, swapped and back, inline where it is after each edit of the right; let go of with a left of another version");
        ALDiffView& d = make("a\nb\nc\nd", "a\nB\nc\nd");
        d.setNotes({ { 2, "about c", "the tip" } });
        ensure("on the left", d.left()->noteAt(2) == "about c" && d.right()->noteAt(2).empty());
        d.setSwapped(true);
        ensure("swapped: on the right, none on the left", d.right()->noteAt(2) == "about c" && d.left()->noteAt(2).empty());
        d.setSwapped(false);
        ensure("and back", d.left()->noteAt(2) == "about c" && d.right()->noteAt(2).empty());
        d.setRightText("a\nB\nput in\nc\nd");
        ensure("the right made anew: the left's as it was", d.left()->noteAt(2) == "about c");
        d.setInline(true);
        const S32 at = d.model().lineShowing(ALDiffModel::Column::Inline, true, 2);
        ensure("inline, beside where the line is", at >= 0 && d.inlined()->noteAt(at) == "about c");
        d.setRightText("a\nB\nput in\nmore\nc\nd");
        const S32 now = d.model().lineShowing(ALDiffModel::Column::Inline, true, 2);
        ensure("and where it is after another edit, nowhere else", now == at + 1 && d.inlined()->noteAt(now) == "about c" && d.inlined()->noteAt(at).empty());
        d.setLeftText("a\nb\nc\nd!");
        ensure("a left of another version: let go of", d.inlined()->noteAt(d.model().lineShowing(ALDiffModel::Column::Inline, true, 2)).empty());
    }

    template<> template<>
    void aldiffview_object::test<44>()
    {
        set_test_name("a change can be copied from the side in front only where that side has lines of it: not from the left under the gap of lines put in, though the caret is in the change, nor inline, which copies what was taken out");
        ALDiffView& d = make("one\ntwo\nthree", "one\nnew\ntwo\nthree");
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(1, 0));
        ensure_equals("the left's line under the gap: in the change", d.changeAtCaret(), 0);
        ensure("nothing of it on the left to copy", !d.canCopyChange() && !d.copyChange(d.changeAtCaret()));
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(1, 0));
        ensure("on the right, its line", d.changeAtCaret() == 0 && d.canCopyChange());
        d.right()->goTo(ALTextPos(2, 0));
        ensure("in no change, nothing", d.changeAtCaret() == -1 && !d.canCopyChange());
        d.setSwapped(true);
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(1, 0));
        ensure("swapped: the right's line on the left", d.changeAtCaret() == 0 && d.canCopyChange());
        d.setSwapped(false);
        d.setInline(true);
        d.inlined()->goTo(ALTextPos(1, 0));
        ensure("inline, nothing taken out to copy", d.changeAtCaret() == 0 && !d.canCopyChange() && !d.copyChange(0));
    }

    template<> template<>
    void aldiffview_object::test<45>()
    {
        set_test_name("a comparison's sides pin no headers over their tops: their rows are lined up by the comparison, and numbered by it");
        ALDiffView& d = make("default\n{\n    state_entry()\n    {\n    }\n}\n", "default\n{\n    state_entry()\n    {\n        x();\n    }\n}\n");
        for (ALCodeEditor* side : { d.left(), d.right(), d.inlined() })
        {
            ensure("no headers pinned on the " + side->getName() + " side", !side->getStickyHeaders());
        }
    }
}
