/**
 * @file alscriptstudioweighing_test.cpp
 * @brief Script Studio's weighing: targets, answers kept or dropped, what a save sends, the saved weights, notes and heat, fixes.
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

#include "../alscriptstudioweighing.h"

#include "../alscriptstudiochecking.h"

#include "alscriptstudio_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;
    typedef ALScriptWeight::Target   Target;

    // The window, faked: a record of what weighing asked of it, and the
    // analyzers' questions held for a test to answer.
    struct FakeWeighingWindow : public ALScriptStudioWeighing::Window, public al_studio_test::QuietAnalysis, public al_studio_test::QuietSaves
    {
        // A tab, as each role this fakes names it.
        typedef ALScriptStudioDoc Doc;

        void askWeights(Doc& doc) override { asked.push_back(doc.id); }
        void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)> answered) override
        {
            requests.push_back(std::move(request));
            answers.push_back(std::move(answered));
        }
        bool        lslFragment(const Doc&) const override { return fragment; }
        bool        preprocessed(const Doc&) const override { return preprocessing; }
        std::string includeName(const Doc&, const std::string& path) const override { return "include " + path; }
        bool        optimizing() const override { return optimizer; }
        std::string programVersion() const override { return "Alchemy Test 1.0"; }
        bool        weightNotes() const override { return notes; }
        bool        weightHeat() const override { return heat; }
        void        refreshProblems(Doc& doc) override { problems.push_back(doc.id); }
        void        warnOverWeight(Doc& doc) override { warned.push_back(doc.id); }
        bool        weightsShown() const override { return shown; }
        ALScriptWeightsPane* weightsPane() override { return nullptr; }

        bool fragment = false, preprocessing = false, optimizer = false, notes = true, heat = true, shown = false;
        Names asked, problems, warned;
        std::vector<ALScriptAnalysis::Request>                                   requests;
        std::vector<std::function<void(const ALScriptAnalysis::Result&)>> answers;
    };

    ALScriptWeight weightOf(Target target, size_t total)
    {
        ALScriptWeight weight;
        weight.target   = target;
        weight.compiled = true;
        weight.total    = total;
        weight.limit    = 64 * 1024;
        return weight;
    }
}

namespace
{
    struct alscriptstudioweighing_data
    {
        al_studio_test::StudioWindow            window;
        al_studio_test::FakeServices            services;
        FakeWeighingWindow                      studio;
        std::unique_ptr<ALScriptStudioWeighing> unit;

        ALScriptStudioWeighing& make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            unit = std::make_unique<ALScriptStudioWeighing>(services, studio, studio, studio);
            return *unit;
        }
        // A script's tab, loaded, compiled for `target` -- "luau" for SLua.
        Doc& tab(const std::string& id, const std::string& text, const std::string& target = "mono")
        {
            Doc& doc                   = services.addDoc(id);
            doc.name                   = id;
            doc.loaded                 = true;
            doc.modifiable             = true;
            doc.language.lua           = target == "luau";
            doc.language.compileTarget = target;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name     = "editor_" + id;
            p.rect     = LLRect(0, 200, 400, 0);
            p.syntax   = doc.language.lua ? "slua" : "lsl";
            doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(doc.editor);
            doc.editor->setText(text);
            doc.editor->resetDirty();
            return doc;
        }
        static U32 versionOf(const Doc& doc) { return doc.editor->document().version(); }
        // The analyzers' answer to the last question, for its tab as it stands.
        ALScriptAnalysis::Result answerFor(const Doc& doc, std::vector<ALScriptWeight> weights) const
        {
            ALScriptAnalysis::Result result;
            result.id      = doc.id;
            result.version = versionOf(doc);
            result.weights = std::move(weights);
            return result;
        }
    };

    TEST_CASE_METHOD(alscriptstudioweighing_data, "alscriptstudioweighing: the target a tab is weighed for and none where it has no weigher", "[alscriptstudioweighing]")
    {
        ALScriptStudioWeighing& unit = make();
        UNSCOPED_INFO("Mono");
        CHECK(unit.target(tab("mono", "x")) == Target::Mono);
        UNSCOPED_INFO("no target said is Mono");
        CHECK(unit.target(tab("none", "x", "")) == Target::Mono);
        UNSCOPED_INFO("LSO");
        CHECK(unit.target(tab("lso", "x", "lsl2")) == Target::LSO);
        UNSCOPED_INFO("LSL on Luau");
        CHECK(unit.target(tab("ll", "x", "lsl-luau")) == Target::LSLLuau);
        UNSCOPED_INFO("SLua");
        CHECK(unit.target(tab("slua", "x", "luau")) == Target::SLua);
        UNSCOPED_INFO("a target with no weigher");
        REQUIRE_FALSE(unit.target(tab("odd", "x", "something")));
        Doc& note  = tab("note", "x");
        note.notecard = true;
        UNSCOPED_INFO("a notecard");
        REQUIRE_FALSE(unit.target(note));
        Doc& loading = tab("loading", "x");
        loading.loaded = false;
        UNSCOPED_INFO("not loaded");
        REQUIRE_FALSE(unit.target(loading));
        studio.fragment = true;
        UNSCOPED_INFO("a fragment");
        REQUIRE_FALSE(unit.target(tab("fragment", "x")));
        studio.fragment = false;

        services.front = 0;
        UNSCOPED_INFO("its own alone");
        REQUIRE(unit.targets(*services.docs[0]).size() == 1);
        studio.shown = true;
        const std::vector<Target> all = unit.targets(*services.docs[0]);
        {
            INFO("in front, the tab looked at: the other two as well");
            REQUIRE(all.size() == 3);
            CHECK(all[0] == Target::Mono);
        }
        UNSCOPED_INFO("not in front: its own");
        REQUIRE(unit.targets(*services.docs[2]).size() == 1);
        services.front = 4;
        UNSCOPED_INFO("SLua has no others");
        REQUIRE(unit.targets(*services.docs[4]).size() == 1);
    }

    TEST_CASE_METHOD(alscriptstudioweighing_data, "alscriptstudioweighing: a weight with its check is kept and told and an older answer dropped", "[alscriptstudioweighing]")
    {
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "default {}");
        unit.weigh(a);
        {
            INFO("asked");
            CHECK(a.weighing->asking);
            CHECK(studio.asked == Names{ "a" });
        }
        ALScriptAnalysis::Result old = answerFor(a, { weightOf(Target::Mono, 100) });
        old.version -= 1;
        unit.weighed(a, old);
        {
            INFO("older: dropped");
            REQUIRE_FALSE(a.weighing->asking);
            REQUIRE_FALSE(a.weighing->weight);
            REQUIRE(a.weighing->all.empty());
        }
        unit.weighed(a, answerFor(a, { weightOf(Target::Mono, 100), weightOf(Target::LSO, 200) }));
        {
            INFO("kept, its own first");
            REQUIRE(a.weighing->weight);
            REQUIRE(a.weighing->weight->total == 100);
            REQUIRE(a.weighing->all.size() == 2);
            REQUIRE(a.weighing->allVersion == versionOf(a));
            CHECK(a.weighing->version == versionOf(a));
        }
        {
            INFO("exact: no optimizer after the check");
            REQUIRE(a.weighing->exact);
            CHECK_FALSE(a.weighing->sent);
        }
        {
            INFO("the Problems and the save told");
            CHECK(studio.problems == Names{ "a" });
            CHECK(studio.warned == Names{ "a" });
        }
        studio.preprocessing = true;
        studio.optimizer     = true;
        unit.weighed(a, answerFor(a, { weightOf(Target::Mono, 90) }));
        {
            INFO("the optimizer after the check: not exact");
            REQUIRE_FALSE(a.weighing->exact);
            CHECK(a.weighing->weight->total == 90);
        }
        Doc& none = tab("none", "x");
        none.notecard = true;
        none.weighing->weight = weightOf(Target::Mono, 1);
        unit.weigh(none);
        {
            INFO("nothing to weigh it for: its weight let go, nothing asked");
            REQUIRE_FALSE(none.weighing->weight);
            REQUIRE(studio.asked.size() == 1);
        }
    }

    TEST_CASE_METHOD(alscriptstudioweighing_data, "alscriptstudioweighing: a sent text's weight stands over the check's of the same text", "[alscriptstudioweighing]")
    {
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "default {}");
        unit.weighSent(a);
        UNSCOPED_INFO("nothing sent yet: nothing asked");
        REQUIRE(studio.requests.empty());
        a.uploaded.valid   = true;
        a.uploaded.version = versionOf(a);
        a.uploaded.text    = std::make_shared<const std::string>("sent text");
        unit.weighSent(a);
        {
            INFO("asked of what was sent, for its own target");
            REQUIRE(studio.requests.size() == 1);
            CHECK(*studio.requests[0].text == "sent text");
            CHECK(studio.requests[0].targets == std::vector<Target>{ Target::Mono });
        }
        studio.answers[0](answerFor(a, { weightOf(Target::Mono, 300) }));
        {
            INFO("kept as sent, and exact");
            REQUIRE(a.weighing->weight);
            REQUIRE(a.weighing->weight->total == 300);
            REQUIRE(a.weighing->sent);
            CHECK(a.weighing->exact);
        }
        {
            INFO("told");
            CHECK(studio.problems == Names{ "a" });
            CHECK(studio.warned == Names{ "a" });
        }
        unit.weighed(a, answerFor(a, { weightOf(Target::Mono, 100) }));
        {
            INFO("the check's of the same text does not replace it");
            REQUIRE(a.weighing->weight->total == 300);
            REQUIRE(a.weighing->all.size() == 1);
        }
        a.uploaded.disabled = true;
        unit.weighSent(a);
        UNSCOPED_INFO("the preprocessor off: the text as written");
        CHECK(*studio.requests[1].text == "default {}");
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" ");
        ALScriptAnalysis::Result late = answerFor(a, { weightOf(Target::Mono, 1) });
        late.version -= 1;
        studio.answers[1](late);
        UNSCOPED_INFO("typed on since: dropped");
        CHECK(a.weighing->weight->total == 300);
    }

    TEST_CASE_METHOD(alscriptstudioweighing_data, "alscriptstudioweighing: the saved text's weight is kept target by target for the change column", "[alscriptstudioweighing]")
    {
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "default {}");
        a.weighing->all        = { weightOf(Target::Mono, 100), weightOf(Target::LSO, 200) };
        a.weighing->allVersion = versionOf(a);
        unit.keepSaved(a);
        UNSCOPED_INFO("kept");
        REQUIRE(a.weighing->saved.size() == 2);
        a.weighing->all = { weightOf(Target::Mono, 150) };
        unit.keepSaved(a);
        {
            INFO("each target's replaced, the others kept");
            REQUIRE(a.weighing->saved.size() == 2);
            REQUIRE(a.weighing->saved[0].total == 150);
            CHECK(a.weighing->saved[1].total == 200);
        }
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" ");
        a.weighing->allVersion = versionOf(a);
        a.weighing->all        = { weightOf(Target::Mono, 1) };
        unit.keepSaved(a);
        UNSCOPED_INFO("not saved: nothing kept");
        CHECK(a.weighing->saved[0].total == 150);
    }

    TEST_CASE_METHOD(alscriptstudioweighing_data, "alscriptstudioweighing: a weight shows each part's bytes and each line's heat in the editor", "[alscriptstudioweighing]")
    {
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "integer f() { return 1; }\ndefault {}\n");
        ALScriptWeight          weight = weightOf(Target::Mono, 100);
        ALScriptWeight::Part    part;
        part.kind  = ALScriptWeight::Part::Kind::Function;
        part.name  = "f";
        part.bytes = 40;
        part.line  = 0;
        weight.parts.push_back(part);
        part.file = "lib.lsl";
        part.line = 1;
        weight.parts.push_back(part);
        weight.lines = { { 0, 40, "" }, { 1, 10, "" }, { 1, 99, "lib.lsl" } };
        a.weighing->weight  = weight;
        a.weighing->version = versionOf(a);
        a.weighing->exact   = true;
        unit.showInEditor(a);
        UNSCOPED_INFO("its part's bytes after its line");
        REQUIRE(a.editor->noteAt(0).find("WeightNote") != std::string::npos);
        UNSCOPED_INFO("the included file's own not");
        REQUIRE(a.editor->noteAt(1).empty());
        UNSCOPED_INFO("the warmest line the heat's own colour");
        CHECK(a.editor->heatAt(0) == 1.f);
        UNSCOPED_INFO("a quarter as warm by bytes, half by their square root");
        CHECK(a.editor->heatAt(1) == 0.5f);
        studio.notes = false;
        unit.showInEditor(a);
        {
            INFO("notes off: gone, the heat kept");
            REQUIRE(a.editor->noteAt(0).empty());
            CHECK(a.editor->heatAt(0) == 1.f);
        }
        studio.heat = false;
        unit.showInEditor(a);
        UNSCOPED_INFO("heat off: gone");
        CHECK(a.editor->heatAt(0) == 0.f);
        studio.notes          = true;
        studio.heat           = true;
        a.weighing->version -= 1;
        unit.showInEditor(a);
        {
            INFO("out of date: nothing new said");
            REQUIRE(a.editor->noteAt(0).empty());
            CHECK(a.editor->heatAt(0) == 0.f);
        }
    }

    TEST_CASE_METHOD(alscriptstudioweighing_data, "alscriptstudioweighing: a save's size is measured once a text and not for a file on disk", "[alscriptstudioweighing]")
    {
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "default {}");
        unit.measureAsset(a);
        UNSCOPED_INFO("the script's text");
        CHECK(a.weighing->assetBytes == size_t(10));
        a.weighing->assetBytes = 0;
        unit.measureAsset(a);
        UNSCOPED_INFO("measured once for a text");
        CHECK(a.weighing->assetBytes == 0);
        Doc& n    = tab("n", "note text");
        n.notecard = true;
        unit.measureAsset(n);
        UNSCOPED_INFO("a notecard's");
        CHECK(n.weighing->assetBytes == size_t(9));
        Doc& f = tab("f", "file text");
        f.file = "/somewhere/f.lsl";
        unit.measureAsset(f);
        UNSCOPED_INFO("a file on disk has no limit");
        CHECK(f.weighing->assetBytes == size_t(0));
        // Preprocessed: the envelope a save would send, measured as long as
        // it would be made.
        Doc& p                 = tab("p", "#define X 1\ndefault { /* x */ }");
        studio.preprocessing   = true;
        p.expanded.valid       = true;
        p.expanded.version     = versionOf(p);
        p.expanded.generation  = 1;
        p.expanded.text        = std::make_shared<const std::string>("default { }\n");
        unit.measureAsset(p);
        UNSCOPED_INFO("the envelope's length");
        REQUIRE(p.weighing->assetBytes == p.envelopeFor(*p.expanded.text, studio.programVersion()).wrap().size());
    }

    TEST_CASE_METHOD(alscriptstudioweighing_data, "alscriptstudioweighing: listed fixes are weighed once settled and told what each would change", "[alscriptstudioweighing]")
    {
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "integer a = 1\n");
        ALCodeEditor&           e    = *a.editor;
        e.setFixProvider([](S32 line, std::vector<ALCodeEditor::Fix>& out) {
            ALCodeEditor::Fix semi;
            semi.title     = "Insert ';'";
            semi.preferred = true;
            semi.value     = "semi";
            semi.edits.emplace_back(ALTextRange(ALTextPos(0, 13), ALTextPos(0, 13)), ";");
            ALCodeEditor::Fix other;
            other.title = "Rename it";
            other.value = "other";
            other.edits.emplace_back(ALTextRange(ALTextPos(0, 8), ALTextPos(0, 9)), "b");
            ALCodeEditor::Fix suppress;
            suppress.title    = "Suppress it";
            suppress.suppress = true;
            suppress.value    = "suppress";
            suppress.edits.emplace_back(ALTextRange(ALTextPos(0, 13), ALTextPos(0, 13)), "  // NOLINT");
            out = { semi, other, suppress };
        });
        e.setFixesShown([&unit, &a](U32 shown, const std::vector<ALCodeEditor::Fix>& fixes) { unit.fixesShown(a.id, shown, fixes); });
        e.setFixable(0, true, true);
        e.setFocus(true);
        {
            INFO("listed");
            CHECK(e.handleKeyHere('.', MASK_CONTROL));
            CHECK(e.fixesOpen());
        }
        unit.pump();
        UNSCOPED_INFO("weighed: the text, and each fix's copy but the suppression's");
        REQUIRE(studio.requests.size() == size_t(1));
        const ALScriptAnalysis::Request& request = studio.requests[0];
        {
            INFO("the text first, then the copies");
            REQUIRE(request.variants.size() == 3);
            CHECK(request.variants[0] == "integer a = 1\n");
            CHECK(request.variants[1] == "integer a = 1;\n");
            CHECK(request.variants[2] == "integer b = 1\n");
        }
        ALScriptAnalysis::Result result;
        result.id            = a.id;
        result.version       = versionOf(a);
        result.variantTotals = { 100, 90, 100 };
        studio.answers[0](result);
        UNSCOPED_INFO("the lighter one said");
        CHECK(e.fixes()[0].note.find("FixLighterEstimate") == 0);
        {
            INFO("the same: nothing said");
            REQUIRE(e.fixes()[1].note.empty());
            REQUIRE(e.fixes()[2].note.empty());
        }
        unit.pump();
        UNSCOPED_INFO("weighed once");
        REQUIRE(studio.requests.size() == 1);
    }

    TEST_CASE_METHOD(alscriptstudioweighing_data, "alscriptstudioweighing: the tab in front is weighed once for the targets it lacks", "[alscriptstudioweighing]")
    {
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "default {}");
        services.front               = 0;
        studio.shown                 = true;
        a.check->analysisVersion     = versionOf(a);
        a.weighing->all              = { weightOf(Target::Mono, 1) };
        a.weighing->allVersion       = versionOf(a);
        a.weighing->askedFor         = versionOf(a);
        unit.pump();
        {
            INFO("the others wanted: weighed");
            CHECK(studio.asked == Names{ "a" });
            CHECK(a.weighing->asking);
        }
        unit.pump();
        UNSCOPED_INFO("once, while it is asked");
        REQUIRE(studio.asked.size() == 1);
        a.weighing->asking = false;
        a.weighing->all    = { weightOf(Target::Mono, 1), weightOf(Target::LSO, 2), weightOf(Target::LSLLuau, 3) };
        unit.pump();
        UNSCOPED_INFO("all there: nothing more");
        REQUIRE(studio.asked.size() == 1);
        studio.shown          = false;
        a.weighing->all.clear();
        unit.pump();
        UNSCOPED_INFO("not looked at: nothing");
        REQUIRE(studio.asked.size() == 1);
    }

    TEST_CASE_METHOD(alscriptstudioweighing_data, "alscriptstudioweighing: a tab come to the front unweighed is weighed once for its text", "[alscriptstudioweighing]")
    {
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "default {}");
        Doc&                    b    = tab("b", "default {}");
        a.check->analysisVersion     = versionOf(a);
        b.check->analysisVersion     = versionOf(b);
        a.weighing->askedFor         = versionOf(a);
        services.front               = 0;
        unit.pump();
        UNSCOPED_INFO("weighed with its check already: nothing");
        REQUIRE(studio.asked.empty());
        services.front = 1;
        unit.pump();
        {
            INFO("come to the front: weighed");
            CHECK(studio.asked == Names{ "b" });
            CHECK(ALScriptStudioWeighing::asking(b));
        }
        unit.pump();
        UNSCOPED_INFO("once");
        REQUIRE(studio.asked.size() == 1);
        // Typed in while it was weighed: the answer for the older text is
        // passed over, and the check of the new one weighs it again.
        b.editor->insertText("x");
        UNSCOPED_INFO("not waited on");
        CHECK_FALSE(ALScriptStudioWeighing::asking(b));
        unit.pump();
        UNSCOPED_INFO("not before its check");
        REQUIRE(studio.asked.size() == 1);
        b.check->analysisVersion = versionOf(b);
        unit.pump();
        UNSCOPED_INFO("then, for the new text");
        CHECK(studio.asked == (Names{ "b", "b" }));
    }
}
