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

#include "../alcodecards.h"

#include "../test/lltut.h"

namespace tut
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

    typedef test_group<alcodecards_data> alcodecards_group;
    typedef alcodecards_group::object    alcodecards_object;
    alcodecards_group                    alcodecards_instance("alcodecards");

    template<> template<>
    void alcodecards_object::test<1>()
    {
        set_test_name("a card: its problems a line or more each, a fix under them, a blank line, the head, and what follows it");
        const std::vector<ALCodeCards::Problem> problems{ { "unknown global 'x'", LLColor4::red }, { "two\nlines", LLColor4::yellow } };
        const std::vector<ALCodeFix>            fixes{ fix("Declare 'x'", "declare") };
        const std::vector<ALCodeCards::Link>    links{ { "declared at line 3", "go", LLSD("line 3") }, { "never said", "", LLSD() } };
        const std::string says = "local x: number\nA number.\n" + ALCodeCards::deprecatedNote() + "\ndeclared at line 3";
        const ALCodeCards::Composition card = ALCodeCards::compose(problems, fixes, says, links);

        ensure_equals("the text", card.text,
                      "unknown global 'x'\ntwo\nlines\nFix: Declare 'x'\n\nlocal x: number\nA number.\n" + ALCodeCards::deprecatedNote() +
                          "\ndeclared at line 3");
        ensure("each problem's lines, the second two", card.problemLines == std::vector<std::pair<S32, size_t>>{ { 0, 0 }, { 1, 1 }, { 2, 1 } });
        ensure("the fix on the line after them", card.fixLines.size() == 1 && card.fixLines[0].first == 3 && card.fixLines[0].second.asString() == "declare");
        ensure_equals("the head after the blank line", card.headLine, 5);
        ensure("the deprecation noted", card.deprecatedLines == std::vector<S32>{ 7 });
        ensure("the link on its line, the one that says nothing there none",
               card.linkLines == std::vector<std::pair<S32, size_t>>{ { 8, 0 } });
    }

    template<> template<>
    void alcodecards_object::test<2>()
    {
        set_test_name("a card of what the word is alone, and of problems alone");
        const ALCodeCards::Composition word = ALCodeCards::compose({}, {}, "ll.Say(channel: number, text: string)", {});
        ensure("the head is the first line", word.headLine == 0 && word.problemLines.empty() && word.text == "ll.Say(channel: number, text: string)");
        const ALCodeCards::Composition wrong = ALCodeCards::compose({ { "expected ')'", LLColor4::red } }, {}, std::string(), {});
        ensure("no head, no blank line", wrong.headLine == -1 && wrong.text == "expected ')'");
    }

    template<> template<>
    void alcodecards_object::test<3>()
    {
        set_test_name("a card goes under what it is about, above it where under runs off the text, and within the text's width");
        const LLRect text(0, 400, 600, 0);
        ensure_equals("no wider than the limit", ALCodeCards::widthLimit(2000), ALCodeCards::MAX_WIDTH);
        ensure_equals("nor the text less its room", ALCodeCards::widthLimit(300), 300 - 2 * ALCodeCards::PAD);
        ensure_equals("as wide as its widest line and its room", ALCodeCards::width(100, 400), 100 + 2 * ALCodeCards::PAD + 2);
        ensure_equals("never narrower than a word", ALCodeCards::width(1, 400), 40);
        ensure_equals("nor wider than allowed", ALCodeCards::width(900, 400), 400);

        const LLRect under = ALCodeCards::place(LLRect(50, 300, 120, 285), text, 200, 100);
        ensure("under the row, at its start", under == LLRect(50, 283, 250, 183));
        const LLRect above = ALCodeCards::place(LLRect(50, 60, 120, 45), text, 200, 100);
        ensure("above it where under would run off", above == LLRect(50, 162, 250, 62));
        const LLRect right = ALCodeCards::place(LLRect(550, 300, 580, 285), text, 200, 100);
        ensure("kept within the text's width", right.mLeft == 400 && right.mRight == 600);
    }

    template<> template<>
    void alcodecards_object::test<4>()
    {
        set_test_name("the analyzer's answer is kept for the word it was asked about as the text stood, and one come late is not taken");
        ALCodeCards       cards;
        const ALTextRange word(ALTextPos(2, 4), ALTextPos(2, 9));
        ensure("not asked", !cards.askedAbout(word, 7));
        cards.asking(word, 7);
        ensure("asked", cards.askedAbout(word, 7));
        ensure("not for another version of the text", !cards.askedAbout(word, 8));
        ensure("an answer for another word is not taken", !cards.heard(ALTextPos(3, 0), 7, "number", {}));
        ensure("nor one for the text as it was", !cards.heard(ALTextPos(2, 4), 6, "number", {}));
        ensure("an empty one taken, as nothing known of the word", cards.heard(ALTextPos(2, 4), 7, std::string(), {}) && cards.answer().empty());
        ensure("the word's, taken", cards.heard(ALTextPos(2, 4), 7, "local n: number", { { "declared here", "", LLSD(1) } }));
        ensure("kept", cards.answer() == "local n: number" && cards.links().size() == 1);
        cards.asking(ALTextRange(ALTextPos(4, 0), ALTextPos(4, 2)), 7);
        ensure("let go of for the next word", cards.answer().empty() && cards.links().empty());
    }

    template<> template<>
    void alcodecards_object::test<5>()
    {
        set_test_name("a signature stays for the call on the caret's line, its box above the row within the view, and its label scrolled to the parameter");
        ALCodeCards cards;
        ALCodeCards::Signature sig;
        sig.label = "f(a, b)";
        cards.showSignature(ALTextPos(3, 10), sig);
        ensure("about the call", cards.signatureFor(ALTextPos(3, 12)));
        ensure("not before where it began", !cards.signatureFor(ALTextPos(3, 9)));
        ensure("nor on another line", !cards.signatureFor(ALTextPos(4, 12)));
        cards.hideSignature();
        ensure("gone", !cards.signature() && !cards.signatureFor(ALTextPos(3, 12)));

        const LLRect view(0, 300, 400, 0);
        ensure("above the row", ALCodeCards::signatureBox(200, 40, LLRect(50, 200, 50, 184), view) == LLRect(50, 240, 250, 200));
        ensure("under it where above runs off the top",
               ALCodeCards::signatureBox(200, 40, LLRect(50, 280, 50, 264), view) == LLRect(50, 264, 250, 224));
        const LLRect wide = ALCodeCards::signatureBox(900, 40, LLRect(300, 200, 300, 184), view);
        ensure("no wider than the view, and inside it", wide.mLeft == 0 && wide.getWidth() == 400);

        ensure_equals("a label that fits is not scrolled", ALCodeCards::labelShift(100.f, 80.f, 150.f), 0.f);
        ensure_equals("one that does not, through the parameter", ALCodeCards::labelShift(300.f, 250.f, 150.f), 100.f);
        ensure_equals("never past its end", ALCodeCards::labelShift(300.f, 900.f, 150.f), 150.f);
        ensure_equals("nor before its start", ALCodeCards::labelShift(300.f, 20.f, 150.f), 0.f);
    }
}
