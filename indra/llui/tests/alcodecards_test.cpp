/**
 * @file alcodecards_test.cpp
 * @brief What a code editor's cards say and where they go, with nothing laid out or drawn.
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

#include "alcodecards.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alcodecards_data
    {
        static ALCodeFix fix(const std::string& title, const std::string& value)
        {
            ALCodeFix one;
            one.title = title;
            one.value = value;
            return one;
        }
    };

    TEST_CASE_METHOD(alcodecards_data, "alcodecards: compose lays out problems then fixes then a blank line and the head", "[alcodecards]")
    {
        const std::vector<ALCodeCards::Problem> problems{ { "unknown global 'x'", LLColor4::red }, { "two\nlines", LLColor4::yellow } };
        const std::vector<ALCodeFix>            fixes{ fix("Declare 'x'", "declare") };
        const std::vector<ALCodeCards::Link>    links{ { "declared at line 3", "go", LLSD("line 3") }, { "never said", "", LLSD() } };
        const std::string says = "local x: number\nA number.\n" + ALCodeCards::deprecatedNote() + "\ndeclared at line 3";
        const ALCodeCards::Composition card = ALCodeCards::compose(problems, fixes, says, links);

        CHECK(card.text == "unknown global 'x'\ntwo\nlines\nFix: Declare 'x'\n\nlocal x: number\nA number.\n" + ALCodeCards::deprecatedNote() +
                               "\ndeclared at line 3");
        UNSCOPED_INFO("each problem's lines, the second two");
        CHECK(card.problemLines == std::vector<std::pair<S32, size_t>>{ { 0, 0 }, { 1, 1 }, { 2, 1 } });
        {
            INFO("the fix on the line after them");
            REQUIRE(card.fixLines.size() == 1);
            CHECK(card.fixLines[0].first == 3);
            CHECK(card.fixLines[0].second.asString() == "declare");
        }
        UNSCOPED_INFO("the head after the blank line");
        CHECK(card.headLine == 5);
        UNSCOPED_INFO("the deprecation noted");
        CHECK(card.deprecatedLines == std::vector<S32>{ 7 });
        UNSCOPED_INFO("the link on its line, the one that says nothing there none");
        CHECK(card.linkLines == std::vector<std::pair<S32, size_t>>{ { 8, 0 } });
    }

    TEST_CASE_METHOD(alcodecards_data, "alcodecards: a card of the word alone or of problems alone", "[alcodecards]")
    {
        const ALCodeCards::Composition word = ALCodeCards::compose({}, {}, "ll.Say(channel: number, text: string)", {});
        {
            INFO("the head is the first line");
            CHECK(word.headLine == 0);
            CHECK(word.problemLines.empty());
            CHECK(word.text == "ll.Say(channel: number, text: string)");
        }
        const ALCodeCards::Composition wrong = ALCodeCards::compose({ { "expected ')'", LLColor4::red } }, {}, std::string(), {});
        {
            INFO("no head, no blank line");
            CHECK(wrong.headLine == -1);
            CHECK(wrong.text == "expected ')'");
        }
    }

    TEST_CASE_METHOD(alcodecards_data, "alcodecards: card widths are limited and a card goes under or above its word", "[alcodecards]")
    {
        const LLRect text(0, 400, 600, 0);
        UNSCOPED_INFO("no wider than the limit");
        CHECK(ALCodeCards::widthLimit(2000) == ALCodeCards::MAX_WIDTH);
        UNSCOPED_INFO("nor the text less its room");
        CHECK(ALCodeCards::widthLimit(300) == 300 - 2 * ALCodeCards::PAD);
        UNSCOPED_INFO("as wide as its widest line and its room");
        CHECK(ALCodeCards::width(100, 400) == 100 + 2 * ALCodeCards::PAD + 2);
        UNSCOPED_INFO("never narrower than a word");
        CHECK(ALCodeCards::width(1, 400) == 40);
        UNSCOPED_INFO("nor wider than allowed");
        CHECK(ALCodeCards::width(900, 400) == 400);

        const LLRect under = ALCodeCards::place(LLRect(50, 300, 120, 285), text, 200, 100);
        UNSCOPED_INFO("under the row, at its start");
        CHECK(under == LLRect(50, 283, 250, 183));
        const LLRect above = ALCodeCards::place(LLRect(50, 60, 120, 45), text, 200, 100);
        UNSCOPED_INFO("above it where under would run off");
        CHECK(above == LLRect(50, 162, 250, 62));
        const LLRect right = ALCodeCards::place(LLRect(550, 300, 580, 285), text, 200, 100);
        {
            INFO("kept within the text's width");
            CHECK(right.mLeft == 400);
            CHECK(right.mRight == 600);
        }
    }

    TEST_CASE_METHOD(alcodecards_data, "alcodecards: an answer is taken only for the word and text version asked about", "[alcodecards]")
    {
        ALCodeCards       cards;
        const ALTextRange word(ALTextPos(2, 4), ALTextPos(2, 9));
        CHECK_FALSE(cards.askedAbout(word, 7));
        cards.asking(word, 7);
        CHECK(cards.askedAbout(word, 7));
        UNSCOPED_INFO("not for another version of the text");
        CHECK_FALSE(cards.askedAbout(word, 8));
        UNSCOPED_INFO("an answer for another word is not taken");
        CHECK_FALSE(cards.heard(ALTextPos(3, 0), 7, "number", {}));
        UNSCOPED_INFO("nor one for the text as it was");
        CHECK_FALSE(cards.heard(ALTextPos(2, 4), 6, "number", {}));
        {
            INFO("an empty one taken, as nothing known of the word");
            CHECK(cards.heard(ALTextPos(2, 4), 7, std::string(), {}));
            CHECK(cards.answer().empty());
        }
        UNSCOPED_INFO("the word's, taken");
        CHECK(cards.heard(ALTextPos(2, 4), 7, "local n: number", { { "declared here", "", LLSD(1) } }));
        CHECK(cards.answer() == "local n: number");
        CHECK(cards.links().size() == 1);
        cards.asking(ALTextRange(ALTextPos(4, 0), ALTextPos(4, 2)), 7);
        {
            INFO("let go of for the next word");
            CHECK(cards.answer().empty());
            CHECK(cards.links().empty());
        }
    }

    TEST_CASE_METHOD(alcodecards_data, "alcodecards: a signature is kept for its call and placed and scrolled to fit", "[alcodecards]")
    {
        ALCodeCards cards;
        ALCodeCards::Signature sig;
        sig.label = "f(a, b)";
        cards.showSignature(ALTextPos(3, 10), sig);
        UNSCOPED_INFO("about the call");
        CHECK(cards.signatureFor(ALTextPos(3, 12)));
        UNSCOPED_INFO("not before where it began");
        CHECK_FALSE(cards.signatureFor(ALTextPos(3, 9)));
        UNSCOPED_INFO("nor on another line");
        CHECK_FALSE(cards.signatureFor(ALTextPos(4, 12)));
        cards.hideSignature();
        CHECK_FALSE(cards.signature());
        CHECK_FALSE(cards.signatureFor(ALTextPos(3, 12)));

        const LLRect view(0, 300, 400, 0);
        UNSCOPED_INFO("above the row");
        CHECK(ALCodeCards::signatureBox(200, 40, LLRect(50, 200, 50, 184), view) == LLRect(50, 240, 250, 200));
        UNSCOPED_INFO("under it where above runs off the top");
        CHECK(ALCodeCards::signatureBox(200, 40, LLRect(50, 280, 50, 264), view) == LLRect(50, 264, 250, 224));
        const LLRect wide = ALCodeCards::signatureBox(900, 40, LLRect(300, 200, 300, 184), view);
        {
            INFO("no wider than the view, and inside it");
            CHECK(wide.mLeft == 0);
            CHECK(wide.getWidth() == 400);
        }

        UNSCOPED_INFO("a label that fits is not scrolled");
        CHECK(ALCodeCards::labelShift(100.f, 80.f, 150.f) == 0.f);
        UNSCOPED_INFO("one that does not, through the parameter");
        CHECK(ALCodeCards::labelShift(300.f, 250.f, 150.f) == 100.f);
        UNSCOPED_INFO("never past its end");
        CHECK(ALCodeCards::labelShift(300.f, 900.f, 150.f) == 150.f);
        UNSCOPED_INFO("nor before its start");
        CHECK(ALCodeCards::labelShift(300.f, 20.f, 150.f) == 0.f);
    }

    TEST_CASE_METHOD(alcodecards_data, "alcodecards: with no problems a card's first fix is its first line", "[alcodecards]")
    {
        const std::string              says = "integer x\n" + ALCodeCards::deprecatedNote();
        const ALCodeCards::Composition card = ALCodeCards::compose({}, { fix("Declare 'x'", "declare") }, says, {});
        UNSCOPED_INFO("no blank line above the fix");
        CHECK(card.text == "Fix: Declare 'x'\n\ninteger x\n" + ALCodeCards::deprecatedNote());
        {
            INFO("the fix on the first line");
            REQUIRE(card.fixLines.size() == 1);
            CHECK(card.fixLines[0].first == 0);
        }
        UNSCOPED_INFO("the head after the blank line");
        CHECK(card.headLine == 2);
        UNSCOPED_INFO("the deprecation on the line it is on");
        CHECK(card.deprecatedLines == std::vector<S32>{ 3 });
    }
}
