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

#include "alscriptstudio_fixture.h"

#include "../test/lltut.h"

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;
    typedef ALScriptWeight::Target   Target;

    // The window, faked: a record of what weighing asked of it, and the
    // analyzers' questions held for a test to answer.
    struct FakeWeighingWindow : public ALScriptStudioWeighing::Window
    {
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

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            unit = std::make_unique<ALScriptStudioWeighing>(services, studio);
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

    typedef test_group<alscriptstudioweighing_data> alscriptstudioweighing_group;
    typedef alscriptstudioweighing_group::object    alscriptstudioweighing_object;
    alscriptstudioweighing_group                    alscriptstudioweighing_instance("alscriptstudioweighing");

    template<> template<>
    void alscriptstudioweighing_object::test<1>()
    {
        set_test_name("the target a tab is weighed for, none where it is not; and the others while it is in front and the Weights tab looked at");
        ALScriptStudioWeighing& unit = make();
        ensure("Mono", unit.target(tab("mono", "x")) == Target::Mono);
        ensure("no target said is Mono", unit.target(tab("none", "x", "")) == Target::Mono);
        ensure("LSO", unit.target(tab("lso", "x", "lsl2")) == Target::LSO);
        ensure("LSL on Luau", unit.target(tab("ll", "x", "lsl-luau")) == Target::LSLLuau);
        ensure("SLua", unit.target(tab("slua", "x", "luau")) == Target::SLua);
        ensure("a target with no weigher", !unit.target(tab("odd", "x", "something")));
        Doc& note  = tab("note", "x");
        note.notecard = true;
        ensure("a notecard", !unit.target(note));
        Doc& loading = tab("loading", "x");
        loading.loaded = false;
        ensure("not loaded", !unit.target(loading));
        studio.fragment = true;
        ensure("a fragment", !unit.target(tab("fragment", "x")));
        studio.fragment = false;

        services.front = 0;
        ensure("its own alone", unit.targets(*services.docs[0]).size() == 1);
        studio.shown = true;
        const std::vector<Target> all = unit.targets(*services.docs[0]);
        ensure("in front, the tab looked at: the other two as well", all.size() == 3 && all[0] == Target::Mono);
        ensure("not in front: its own", unit.targets(*services.docs[2]).size() == 1);
        services.front = 4;
        ensure("SLua has no others", unit.targets(*services.docs[4]).size() == 1);
    }

    template<> template<>
    void alscriptstudioweighing_object::test<2>()
    {
        set_test_name("weighed with its check: kept, told, and exact where nothing after the check changes it; an answer for older text dropped");
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "default {}");
        unit.weigh(a);
        ensure("asked", a.weighing.asking && studio.asked == Names{ "a" });
        ALScriptAnalysis::Result old = answerFor(a, { weightOf(Target::Mono, 100) });
        old.version -= 1;
        unit.weighed(a, old);
        ensure("older: dropped", !a.weighing.asking && !a.weighing.weight && a.weighing.all.empty());
        unit.weighed(a, answerFor(a, { weightOf(Target::Mono, 100), weightOf(Target::LSO, 200) }));
        ensure("kept, its own first", a.weighing.weight && a.weighing.weight->total == 100 && a.weighing.all.size() == 2 &&
                                          a.weighing.allVersion == versionOf(a) && a.weighing.version == versionOf(a));
        ensure("exact: no optimizer after the check", a.weighing.exact && !a.weighing.sent);
        ensure("the Problems and the save told", studio.problems == Names{ "a" } && studio.warned == Names{ "a" });
        studio.preprocessing = true;
        studio.optimizer     = true;
        unit.weighed(a, answerFor(a, { weightOf(Target::Mono, 90) }));
        ensure("the optimizer after the check: not exact", !a.weighing.exact && a.weighing.weight->total == 90);
        Doc& none = tab("none", "x");
        none.notecard = true;
        none.weighing.weight = weightOf(Target::Mono, 1);
        unit.weigh(none);
        ensure("nothing to weigh it for: its weight let go, nothing asked", !none.weighing.weight && studio.asked.size() == 1);
    }

    template<> template<>
    void alscriptstudioweighing_object::test<3>()
    {
        set_test_name("what a save sends weighed as sent, which stands over the check's of the same text; an answer for older text dropped");
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "default {}");
        unit.weighSent(a);
        ensure("nothing sent yet: nothing asked", studio.requests.empty());
        a.uploaded.valid   = true;
        a.uploaded.version = versionOf(a);
        a.uploaded.text    = "sent text";
        unit.weighSent(a);
        ensure("asked of what was sent, for its own target", studio.requests.size() == 1 && studio.requests[0].text == "sent text" &&
                                                                  studio.requests[0].targets == std::vector<Target>{ Target::Mono });
        studio.answers[0](answerFor(a, { weightOf(Target::Mono, 300) }));
        ensure("kept as sent, and exact", a.weighing.weight && a.weighing.weight->total == 300 && a.weighing.sent && a.weighing.exact);
        ensure("told", studio.problems == Names{ "a" } && studio.warned == Names{ "a" });
        unit.weighed(a, answerFor(a, { weightOf(Target::Mono, 100) }));
        ensure("the check's of the same text does not replace it", a.weighing.weight->total == 300 && a.weighing.all.size() == 1);
        a.uploaded.disabled = true;
        unit.weighSent(a);
        ensure("the preprocessor off: the text as written", studio.requests[1].text == "default {}");
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" ");
        ALScriptAnalysis::Result late = answerFor(a, { weightOf(Target::Mono, 1) });
        late.version -= 1;
        studio.answers[1](late);
        ensure("typed on since: dropped", a.weighing.weight->total == 300);
    }

    template<> template<>
    void alscriptstudioweighing_object::test<4>()
    {
        set_test_name("what the text weighs while it is the text saved is kept for the change column, target by target");
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "default {}");
        a.weighing.all        = { weightOf(Target::Mono, 100), weightOf(Target::LSO, 200) };
        a.weighing.allVersion = versionOf(a);
        unit.keepSaved(a);
        ensure("kept", a.weighing.saved.size() == 2);
        a.weighing.all = { weightOf(Target::Mono, 150) };
        unit.keepSaved(a);
        ensure("each target's replaced, the others kept", a.weighing.saved.size() == 2 && a.weighing.saved[0].total == 150 &&
                                                              a.weighing.saved[1].total == 200);
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" ");
        a.weighing.allVersion = versionOf(a);
        a.weighing.all        = { weightOf(Target::Mono, 1) };
        unit.keepSaved(a);
        ensure("not saved: nothing kept", a.weighing.saved[0].total == 150);
    }

    template<> template<>
    void alscriptstudioweighing_object::test<5>()
    {
        set_test_name("a weight shown in the editor: each part's bytes after its line, each line's heat; neither where it is off or out of date");
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
        a.weighing.weight  = weight;
        a.weighing.version = versionOf(a);
        a.weighing.exact   = true;
        unit.showInEditor(a);
        ensure("its part's bytes after its line", a.editor->noteAt(0).find("WeightNote") != std::string::npos);
        ensure("the included file's own not", a.editor->noteAt(1).empty());
        ensure("the warmest line the heat's own colour", a.editor->heatAt(0) == 1.f);
        ensure_equals("a quarter as warm by bytes, half by their square root", a.editor->heatAt(1), 0.5f);
        studio.notes = false;
        unit.showInEditor(a);
        ensure("notes off: gone, the heat kept", a.editor->noteAt(0).empty() && a.editor->heatAt(0) == 1.f);
        studio.heat = false;
        unit.showInEditor(a);
        ensure("heat off: gone", a.editor->heatAt(0) == 0.f);
        studio.notes          = true;
        studio.heat           = true;
        a.weighing.version -= 1;
        unit.showInEditor(a);
        ensure("out of date: nothing new said", a.editor->noteAt(0).empty() && a.editor->heatAt(0) == 0.f);
    }

    template<> template<>
    void alscriptstudioweighing_object::test<6>()
    {
        set_test_name("what a save would send, measured once for each text: a script's, a notecard's, nothing for a file on disk");
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "default {}");
        unit.measureAsset(a);
        ensure_equals("the script's text", a.weighing.assetBytes, size_t(10));
        a.weighing.assetBytes = 0;
        unit.measureAsset(a);
        ensure("measured once for a text", a.weighing.assetBytes == 0);
        Doc& n    = tab("n", "note text");
        n.notecard = true;
        unit.measureAsset(n);
        ensure_equals("a notecard's", n.weighing.assetBytes, size_t(9));
        Doc& f = tab("f", "file text");
        f.file = "/somewhere/f.lsl";
        unit.measureAsset(f);
        ensure_equals("a file on disk has no limit", f.weighing.assetBytes, size_t(0));
    }

    template<> template<>
    void alscriptstudioweighing_object::test<7>()
    {
        set_test_name("the fixes listed weighed once the list settles, each against the text as it stands; told what each would change");
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
        ensure("listed", e.handleKeyHere('.', MASK_CONTROL) && e.fixesOpen());
        unit.pump();
        ensure_equals("weighed: the text, and each fix's copy but the suppression's", studio.requests.size(), size_t(1));
        const ALScriptAnalysis::Request& request = studio.requests[0];
        ensure("the text first, then the copies", request.variants.size() == 3 && request.variants[0] == "integer a = 1\n" &&
                                                     request.variants[1] == "integer a = 1;\n" && request.variants[2] == "integer b = 1\n");
        ALScriptAnalysis::Result result;
        result.id            = a.id;
        result.version       = versionOf(a);
        result.variantTotals = { 100, 90, 100 };
        studio.answers[0](result);
        ensure("the lighter one said", e.fixes()[0].note.find("FixLighterEstimate") == 0);
        ensure("the same: nothing said", e.fixes()[1].note.empty() && e.fixes()[2].note.empty());
        unit.pump();
        ensure("weighed once", studio.requests.size() == 1);
    }

    template<> template<>
    void alscriptstudioweighing_object::test<8>()
    {
        set_test_name("while the Weights tab is looked at, the tab in front checked as it stands is weighed for the targets it lacks, once");
        ALScriptStudioWeighing& unit = make();
        Doc&                    a    = tab("a", "default {}");
        services.front               = 0;
        studio.shown                 = true;
        a.analysisVersion            = versionOf(a);
        a.weighing.all               = { weightOf(Target::Mono, 1) };
        a.weighing.allVersion        = versionOf(a);
        unit.pump();
        ensure("the others wanted: weighed", studio.asked == Names{ "a" } && a.weighing.asking);
        unit.pump();
        ensure("once, while it is asked", studio.asked.size() == 1);
        a.weighing.asking = false;
        a.weighing.all    = { weightOf(Target::Mono, 1), weightOf(Target::LSO, 2), weightOf(Target::LSLLuau, 3) };
        unit.pump();
        ensure("all there: nothing more", studio.asked.size() == 1);
        studio.shown          = false;
        a.weighing.all.clear();
        unit.pump();
        ensure("not looked at: nothing", studio.asked.size() == 1);
    }
}
