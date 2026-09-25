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

#include "../alcompletionmodel.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

namespace tut
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
        static std::vector<std::string> texts(const std::vector<ALCompletion>& list)
        {
            std::vector<std::string> out;
            for (const ALCompletion& c : list)
            {
                out.push_back(c.text);
            }
            return out;
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
    typedef test_group<alcompletionmodel_data> alcompletionmodel_group;
    typedef alcompletionmodel_group::object    alcompletionmodel_object;
    alcompletionmodel_group                    alcompletionmodel_instance("ALCompletionModel");

    template<> template<>
    void alcompletionmodel_object::test<1>()
    {
        set_test_name("ranked by how well the word matches, then what it is, then the alphabet; no more than the cap");
        std::vector<ALCompletion> list = { word("llSetPos"),
                                           word("llSay"),
                                           word("say_channel", ALSyntaxKind::Variable),
                                           word("SAY_ALL", ALSyntaxKind::Constant),
                                           word("sayWhat", ALSyntaxKind::Text),
                                           word("sayOld", ALSyntaxKind::Function),
                                           word("sayNew", ALSyntaxKind::Function) };
        list[5].deprecated = true;
        ALCompletionModel::rank(list, "say");
        ensure_equals("the start as typed first, a local before a function before a word; then either case; then a part; one not matching at all, "
                      "which the provider would not have answered, last",
                      joined(list), std::string("say_channel|sayNew|sayOld|sayWhat|SAY_ALL|llSay|llSetPos"));
        std::vector<ALCompletion> many;
        for (size_t i = 0; i < ALCompletionModel::CAP + 20; ++i)
        {
            many.push_back(word("w" + std::to_string(i)));
        }
        ALCompletionModel::rank(many, "w");
        ensure_equals("capped", many.size(), ALCompletionModel::CAP);
    }

    template<> template<>
    void alcompletionmodel_object::test<2>()
    {
        set_test_name("the document's own words after the answer: not the one being typed, a number, a shorter word or one already listed");
        const ALTextDocument text("integer total;\nto 12 tot\ntotal = tota\n");
        std::vector<ALCompletion> out = { word("totally") };
        // Typing "tota" at the end of the last line.
        ALCompletionModel::documentWords(text, ALTextPos(2, 12), "tota", out);
        ensure_equals("total once, after what was answered; not tota itself", joined(out), std::string("totally|total"));
    }

    template<> template<>
    void alcompletionmodel_object::test<3>()
    {
        set_test_name("a member asked for by its head has the head taken off; the document's words are for a bare name only");
        const ALTextDocument text("Salt = 1\nll.Sa\n");
        const bool fresh = model.narrow(ALTextPos(1, 3), ALTextPos(1, 5), "Sa", "ll", { word("ll.Say"), word("ll.Sample") }, text);
        ensure("a new identifier", fresh);
        ensure_equals("members, no head, nothing of the document's", joined(model.list()), std::string("Sample|Say"));
        ensure("the stretch replaced is the name after the dot", model.range() == ALTextRange(ALTextPos(1, 3), ALTextPos(1, 5)));
    }

    template<> template<>
    void alcompletionmodel_object::test<4>()
    {
        set_test_name("an answer later for the same identifier is merged by name, filling what was empty; one for another identifier is refused");
        const ALTextDocument text("sa\n");
        const ALTextPos      start(0, 0);
        model.narrow(start, ALTextPos(0, 2), "sa", std::string(), { word("say") }, text);
        ensure("not for another place", !model.supply(ALTextPos(3, 0), { word("sample") }));
        ALCompletion better = word("say", ALSyntaxKind::Function, "say(msg)");
        better.documentation = "Says it.";
        ensure("for this one", model.supply(start, { better, word("salt", ALSyntaxKind::Variable), word("other") }));
        const bool fresh = model.narrow(start, ALTextPos(0, 2), "sa", std::string(), { word("say") }, text);
        ensure("the same identifier", !fresh);
        ensure_equals("merged, not doubled, and what does not match left out", joined(model.list()), std::string("salt|say"));
        const ALCompletion& say = model.list()[1];
        ensure("its detail and words filled in", say.detail == "say(msg)" && say.documentation == "Says it.");
        ensure("kept through narrowing", !model.narrow(start, ALTextPos(0, 3), "sal", std::string(), {}, text) && joined(model.list()) == "salt");
        ensure("a new identifier lets the answer go", model.narrow(ALTextPos(0, 1), ALTextPos(0, 2), "a", std::string(), {}, text) && model.list().empty());
    }

    template<> template<>
    void alcompletionmodel_object::test<5>()
    {
        set_test_name("a list made again for what it was made for keeps its choice; hidden, it is made afresh; closed, the identifier is asked about again");
        const ALTextDocument text("s\n");
        ensure("first", !model.relisted("sa"));
        ensure("again", model.relisted("sa"));
        ensure("more typed", !model.relisted("say"));
        model.hide();
        ensure("hidden", !model.relisted("say"));
        model.narrow(ALTextPos(0, 0), ALTextPos(0, 1), "s", std::string(), {}, text);
        ensure("asked", model.asked() == ALTextPos(0, 0));
        model.close();
        ensure("closed", model.asked() == ALTextPos(-1, -1));
        ensure("fresh again", model.narrow(ALTextPos(0, 0), ALTextPos(0, 1), "s", std::string(), {}, text));
    }
}
