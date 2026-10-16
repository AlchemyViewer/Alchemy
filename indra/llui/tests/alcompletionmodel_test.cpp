/**
 * @file alcompletionmodel_test.cpp
 * @brief What a code editor offers to complete a word with, and in what order.
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

#include "alcompletionmodel.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    struct alcompletionmodel_data
    {
        ALCompletionModel model;

        static ALCompletion word(const std::string& text, ALSyntaxKind kind = ALSyntaxKind::Function, const std::string& detail = std::string())
        {
            ALCompletion c;
            c.text   = text;
            c.kind   = kind;
            c.detail = detail;
            return c;
        }
        static std::string joined(const std::vector<ALCompletion>& list)
        {
            std::string out;
            for (const ALCompletion& c : list)
            {
                out += (out.empty() ? "" : "|") + c.text;
            }
            return out;
        }
    };

    TEST_CASE_METHOD(alcompletionmodel_data, "ALCompletionModel: rank orders by match then kind then name and stops at the cap", "[ALCompletionModel]")
    {
        std::vector<ALCompletion> list = { word("llSetPos"),
                                           word("llSay"),
                                           word("say_channel", ALSyntaxKind::Variable),
                                           word("SAY_ALL", ALSyntaxKind::Constant),
                                           word("sayWhat", ALSyntaxKind::Text),
                                           word("sayOld", ALSyntaxKind::Function),
                                           word("sayNew", ALSyntaxKind::Function) };
        list[5].deprecated = true;
        ALCompletionModel::rank(list, "say");
        UNSCOPED_INFO("the start as typed first, a local before a function before a word; then either case; then a part; one not matching at all, "
                      "which the provider would not have answered, last");
        CHECK(joined(list) == std::string("say_channel|sayNew|sayOld|sayWhat|SAY_ALL|llSay|llSetPos"));
        std::vector<ALCompletion> many;
        for (size_t i = 0; i < ALCompletionModel::CAP + 20; ++i)
        {
            many.push_back(word("w" + std::to_string(i)));
        }
        ALCompletionModel::rank(many, "w");
        CHECK(many.size() == ALCompletionModel::CAP);
    }

    TEST_CASE_METHOD(alcompletionmodel_data, "ALCompletionModel: documentWords adds the document's other words after the answer", "[ALCompletionModel]")
    {
        const ALTextDocument text("integer total;\nto 12 tot\ntotal = tota\n");
        std::vector<ALCompletion> out = { word("totally") };
        // Typing "tota" at the end of the last line.
        ALCompletionModel::documentWords(text, ALTextPos(2, 12), "tota", out);
        UNSCOPED_INFO("total once, after what was answered; not tota itself");
        CHECK(joined(out) == std::string("totally|total"));
    }

    TEST_CASE_METHOD(alcompletionmodel_data, "ALCompletionModel: a member list drops the head and leaves out the document's words", "[ALCompletionModel]")
    {
        const ALTextDocument text("Salt = 1\nll.Sa\n");
        const bool fresh = model.narrow(ALTextPos(1, 3), ALTextPos(1, 5), "Sa", "ll", { word("ll.Say"), word("ll.Sample") }, text);
        UNSCOPED_INFO("a new identifier");
        CHECK(fresh);
        UNSCOPED_INFO("members, no head, nothing of the document's");
        CHECK(joined(model.list()) == std::string("Sample|Say"));
        UNSCOPED_INFO("the stretch replaced is the name after the dot");
        CHECK(model.range() == ALTextRange(ALTextPos(1, 3), ALTextPos(1, 5)));
    }

    TEST_CASE_METHOD(alcompletionmodel_data, "ALCompletionModel: a later answer merges by name for the same identifier only", "[ALCompletionModel]")
    {
        const ALTextDocument text("sa\n");
        const ALTextPos      start(0, 0);
        model.narrow(start, ALTextPos(0, 2), "sa", std::string(), { word("say") }, text);
        UNSCOPED_INFO("not for another place");
        CHECK_FALSE(model.supply(ALTextPos(3, 0), { word("sample") }));
        ALCompletion better = word("say", ALSyntaxKind::Function, "say(msg)");
        better.documentation = ALCompletion::shared("Says it.");
        UNSCOPED_INFO("for this one");
        CHECK(model.supply(start, { better, word("salt", ALSyntaxKind::Variable), word("other") }));
        const bool fresh = model.narrow(start, ALTextPos(0, 2), "sa", std::string(), { word("say") }, text);
        UNSCOPED_INFO("the same identifier");
        CHECK_FALSE(fresh);
        UNSCOPED_INFO("merged, not doubled, and what does not match left out");
        REQUIRE(joined(model.list()) == std::string("salt|say"));
        const ALCompletion& say = model.list()[1];
        {
            INFO("its detail and words filled in");
            CHECK(say.detail == "say(msg)");
            REQUIRE(say.documentation);
            CHECK(*say.documentation == "Says it.");
        }
        {
            INFO("kept through narrowing");
            CHECK_FALSE(model.narrow(start, ALTextPos(0, 3), "sal", std::string(), {}, text));
            CHECK(joined(model.list()) == "salt");
        }
        {
            INFO("a new identifier lets the answer go");
            CHECK(model.narrow(ALTextPos(0, 1), ALTextPos(0, 2), "a", std::string(), {}, text));
            CHECK(model.list().empty());
        }
    }

    TEST_CASE_METHOD(alcompletionmodel_data, "ALCompletionModel: relisted holds for the same text until hidden and close forgets the ask", "[ALCompletionModel]")
    {
        const ALTextDocument text("s\n");
        UNSCOPED_INFO("first");
        CHECK_FALSE(model.relisted("sa"));
        UNSCOPED_INFO("again");
        CHECK(model.relisted("sa"));
        UNSCOPED_INFO("more typed");
        CHECK_FALSE(model.relisted("say"));
        model.hide();
        UNSCOPED_INFO("hidden");
        CHECK_FALSE(model.relisted("say"));
        model.narrow(ALTextPos(0, 0), ALTextPos(0, 1), "s", std::string(), {}, text);
        CHECK(model.asked() == ALTextPos(0, 0));
        model.close();
        UNSCOPED_INFO("closed");
        CHECK(model.asked() == ALTextPos(-1, -1));
        UNSCOPED_INFO("fresh again");
        CHECK(model.narrow(ALTextPos(0, 0), ALTextPos(0, 1), "s", std::string(), {}, text));
    }

    TEST_CASE_METHOD(alcompletionmodel_data, "ALCompletionModel: a pool narrows as its word grows and rank keeps the best up to the cap", "[ALCompletionModel]")
    {
        ALTextDocument text("integer total;\ninteger totals;\nto");
        const ALTextPos start(2, 0);
        model.pool(start, ALTextPos(2, 2), "to", std::string(), '.', { word("toString"), word("touch", ALSyntaxKind::Event) }, text);
        {
            INFO("for the word as it grows");
            CHECK(model.pooled(start, std::string(), "to"));
            CHECK(model.pooled(start, std::string(), "tot"));
        }
        UNSCOPED_INFO("not where it shrinks past it");
        CHECK_FALSE(model.pooled(start, std::string(), "t"));
        {
            INFO("nor for another word, or a member");
            CHECK_FALSE(model.pooled(ALTextPos(1, 0), std::string(), "tot"));
            CHECK_FALSE(model.pooled(start, "ll", "to"));
        }
        model.narrow(start, ALTextPos(2, 3), "tot");
        UNSCOPED_INFO("the document's words too, narrowed");
        CHECK(joined(model.list()) == std::string("total|totals"));
        model.narrow(start, ALTextPos(2, 5), "total");
        UNSCOPED_INFO("one no longer than what is typed left out");
        CHECK(joined(model.list()) == std::string("totals"));

        std::vector<ALCompletion> many;
        for (int i = 299; i >= 0; --i)
        {
            char name[8];
            snprintf(name, sizeof(name), "w%03d", i);
            many.push_back(word(name, ALSyntaxKind::Variable));
        }
        ALCompletionModel::rank(many, "w");
        REQUIRE(many.size() == ALCompletionModel::CAP);
        {
            INFO("the best of them, in order");
            CHECK(many.front().text == "w000");
            CHECK(many.back().text == "w199");
        }
    }

    TEST_CASE_METHOD(alcompletionmodel_data, "ALCompletionModel: what fits ranks first among equal matches and an answer says what fits", "[ALCompletionModel]")
    {
        std::vector<ALCompletion> list = { word("count", ALSyntaxKind::Variable), word("cost", ALSyntaxKind::Variable), word("colour", ALSyntaxKind::Variable),
                                           word("covers", ALSyntaxKind::Function), word("core", ALSyntaxKind::Variable) };
        list[3].fits = true;
        list[4].fits = true;
        ALCompletionModel::rank(list, "co");
        UNSCOPED_INFO("the two that fit first, a local before a function; then the rest");
        CHECK(joined(list) == std::string("core|covers|colour|cost|count"));
        list = { word("count", ALSyntaxKind::Variable), word("acorn", ALSyntaxKind::Variable) };
        list[1].fits = true;
        ALCompletionModel::rank(list, "co");
        UNSCOPED_INFO("but a better match beats a fit");
        CHECK(joined(list) == std::string("count|acorn"));

        // The provider's word, joined later by the analyzer's answer for it.
        const ALTextDocument text("pr\n");
        const ALTextPos      start(0, 0);
        model.narrow(start, ALTextPos(0, 2), "pr", std::string(), { word("print"), word("pairs") }, text);
        REQUIRE_FALSE(model.list().empty());
        UNSCOPED_INFO("guessed until told");
        CHECK(model.list()[0].brackets == ALCompletion::Brackets::Guess);
        ALCompletion told = word("print");
        told.fits         = true;
        told.brackets     = ALCompletion::Brackets::None;
        model.supply(start, { told });
        model.narrow(start, ALTextPos(0, 2), "pr", std::string(), {}, text);
        {
            INFO("told it fits, and where its brackets go");
            REQUIRE_FALSE(model.list().empty());
            CHECK(model.list()[0].text == "print");
            CHECK(model.list()[0].fits);
            CHECK(model.list()[0].brackets == ALCompletion::Brackets::None);
        }
    }

    TEST_CASE_METHOD(alcompletionmodel_data, "ALCompletionModel: an answer without words leaves them out until the next identifier", "[ALCompletionModel]")
    {
        const ALTextDocument text("local number_of = 1\nlocal n: nu\n");
        const ALTextPos      start(1, 9);
        model.narrow(start, ALTextPos(1, 11), "nu", std::string(), {}, text);
        UNSCOPED_INFO("the document's word, asked nothing yet");
        CHECK(joined(model.list()) == std::string("number_of"));
        UNSCOPED_INFO("told");
        CHECK(model.supply(start, { word("number", ALSyntaxKind::Type) }, /*words*/ false));
        model.narrow(start, ALTextPos(1, 11), "nu", std::string(), {}, text);
        UNSCOPED_INFO("the type alone");
        CHECK(joined(model.list()) == std::string("number"));
        model.narrow(ALTextPos(1, 6), ALTextPos(1, 7), "n", std::string(), {}, text);
        UNSCOPED_INFO("another identifier has them again");
        CHECK(joined(model.list()).find("number_of") != std::string::npos);
    }

    TEST_CASE_METHOD(alcompletionmodel_data, "ALCompletionModel: in a string the match reads escapes but the range covers them", "[ALCompletionModel]")
    {
        // `Say \"` typed in a string, matched as `Say "`.
        const ALTextDocument text("llPlaySound(\"Say \\\"\n");
        const ALTextPos      start(0, 13);
        model.pool(start, ALTextPos(0, 19), "Say \"", std::string(), '\0', { word("Say \"hi\"", ALSyntaxKind::String), word("Door", ALSyntaxKind::String) },
                   text, /*with_words*/ false);
        model.narrow(start, ALTextPos(0, 19), "Say \"");
        UNSCOPED_INFO("matched as it reads");
        CHECK(joined(model.list()) == std::string("Say \"hi\""));
        UNSCOPED_INFO("all the string holds up to the caret replaced");
        CHECK(model.range() == ALTextRange(start, ALTextPos(0, 19)));
    }

    // Offered, it would be what Return takes, which doubles its tail.
    TEST_CASE_METHOD(alcompletionmodel_data, "ALCompletionModel: the identifier the caret is inside is not offered for itself", "[ALCompletionModel]")
    {
        const ALTextDocument text("x = foobar;\nfoobaz = 1\n");
        // `foo` typed with the caret inside `foobar`.
        std::vector<ALCompletion> out;
        ALCompletionModel::documentWords(text, ALTextPos(0, 7), "foo", out);
        UNSCOPED_INFO("another word that matches, not the one the caret is in");
        CHECK(joined(out) == std::string("foobaz"));
        model.narrow(ALTextPos(0, 4), ALTextPos(0, 7), "foo", std::string(), {}, text);
        UNSCOPED_INFO("nor on the list");
        CHECK(joined(model.list()) == std::string("foobaz"));
        out.clear();
        ALCompletionModel::documentWords(text, ALTextPos(1, 3), "foo", out);
        UNSCOPED_INFO("offered where the caret is in another");
        CHECK(joined(out) == std::string("foobar"));
    }
}
