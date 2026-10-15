/**
 * @file alscriptlookup_test.cpp
 * @brief Script Studio's lookups across scripts, the window's side faked: the fan-out, answers dropped, renames, edits waiting.
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

// The preprocessor's header reaches the inventory model's, which does not
// include what it uses.
#include <boost/unordered_map.hpp>

#include "../alscriptlookup.h"
#include "alincludeidentity.h"
#include "almainloop.h"
#include "alteststateguard.h"
#include "llfile.h"
#include "workqueue.h"

#include "../alscriptstudiowords.h"
#include "alscriptstudio_fixture.h"
#include "llfocusmgr.h"
#include "newview_test_fakes.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <thread>

// A script's id, and what the preprocessor calls a script and a file, as
// the studio's tests' stubs of the viewer's spell them
// (alscriptpaths_stub.cpp).

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef ALScriptLookup::Found    Found;
    typedef std::vector<std::string> Names;

    // The window, faked: the scripts it offers, and each question it was
    // asked, held for the test to answer.
    struct FakeLookupWindow : public ALScriptLookup::Window, public al_studio_test::QuietTabs, public al_studio_test::QuietAnalysis
    {
        // A tab, as each role this fakes names it.
        typedef ALScriptStudioDoc Doc;

        struct Load
        {
            ALScriptRef                                                          ref;
            std::function<void(const LLUUID&, const std::optional<std::string>&)> loaded;
        };
        struct Expand
        {
            ALScriptPreprocessor::Request                              request;
            std::function<void(const ALPreprocessor::Result&)>         expanded;
        };
        struct Ask
        {
            ALScriptAnalysis::Request                                  request;
            std::function<void(const ALScriptAnalysis::Result&)>       answered;
        };

        void candidates(const Doc& doc, std::function<void(ALScriptLookup::Candidates)> told) override
        {
            asked.push_back(doc.id);
            ALScriptLookup::Candidates found;
            found.scripts  = others;
            found.unlisted = unlisted;
            if (holdCandidates)
            {
                heldCandidates = [told = std::move(told), found]() { told(found); };
                return;
            }
            told(std::move(found));
        }
        void loadSource(const ALScriptRef& ref, std::function<void(const LLUUID&, const std::optional<std::string>&)> loaded) override
        {
            loads.push_back({ ref, std::move(loaded) });
        }
        void expand(ALScriptPreprocessor::Request request, std::function<void(const ALPreprocessor::Result&)> expanded) override
        {
            expands.push_back({ std::move(request), std::move(expanded) });
        }
        void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)> answered) override
        {
            asks.push_back({ std::move(request), std::move(answered) });
        }
        ALScriptPlaces::Lines sourceLines(const std::string& path) const override
        {
            // The file as its lines were given, each at its number; asked
            // once however many places are in it.
            ++linesAsked[path];
            std::string text;
            S32         last = -1;
            for (const auto& [key, line] : lines)
            {
                const size_t hash = key.rfind('#');
                if (key.substr(0, hash) == path)
                {
                    last = llmax(last, std::stoi(key.substr(hash + 1)));
                }
            }
            if (last < 0)
            {
                return ALScriptPlaces::Lines();
            }
            for (S32 i = 0; i <= last; ++i)
            {
                const auto held = lines.find(path + "#" + std::to_string(i));
                text += (held == lines.end() ? std::string() : held->second) + (i < last ? "\n" : "");
            }
            return ALScriptPlaces::Lines(std::make_shared<const std::string>(text));
        }
        void showFound(Doc& doc, const Found& found) override
        {
            shownIn = doc.id;
            shown   = found;
            ++shows;
        }
        void askNewName(Doc& doc, std::function<std::string(const std::string&)> hint_of, std::function<void(const std::string&)> chose,
                        std::function<void(const std::string&)> preview_of) override
        {
            askedName = doc.id;
            hint      = std::move(hint_of);
            chosen    = std::move(chose);
            previewed = std::move(preview_of);
        }
        void previewRename(Doc& doc, const Found& found, const std::string& new_name, const std::string& said,
                           std::function<void(const std::vector<size_t>&)>                     apply_kept,
                           std::function<void(const std::string&, const std::vector<size_t>&)> show_changes) override
        {
            previewIn   = doc.id;
            previewOf   = found;
            previewName = new_name;
            previewSaid = said;
            apply       = std::move(apply_kept);
            changes     = std::move(show_changes);
        }
        void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                     const std::string& right_title) override
        {
            compared.push_back(doc.id + ": " + left + " | " + right + " (" + left_title + " | " + right_title + ")");
        }
        Doc* openFileTab(const std::string& path, bool lua) override
        {
            filesOpened.push_back(path);
            return whenFileOpened ? whenFileOpened(path) : nullptr;
        }
        void activate(Doc& doc) override { activated.push_back(doc.id); }
        std::vector<std::string> diskCandidates(const Doc& doc) override { return disk; }

        std::vector<ALScriptLookup::Candidate>             others;
        // How many prims did not say what they hold; the candidates held
        // back, as an object's are while its prims are asked, until told.
        S32                                                unlisted       = 0;
        bool                                               holdCandidates = false;
        std::function<void()>                              heldCandidates;
        std::map<std::string, std::string>                 lines;
        mutable std::map<std::string, S32>                 linesAsked;
        std::vector<Load>                                  loads;
        std::vector<Expand>                                expands;
        std::vector<Ask>                                   asks;
        Names                                              asked, filesOpened, activated;
        std::string                                        shownIn, askedName;
        Found                                              shown;
        S32                                                shows = 0;
        std::function<std::string(const std::string&)>     hint;
        std::function<void(const std::string&)>            chosen;
        std::function<void(const std::string&)>            previewed;
        std::string                                        previewIn, previewName, previewSaid;
        Found                                              previewOf;
        std::function<void(const std::vector<size_t>&)>    apply;
        std::function<void(const std::string&, const std::vector<size_t>&)> changes;
        Names                                              compared;
        std::function<Doc*(const std::string&)>            whenFileOpened;
        // The scripts on disk under the folders a script reads from.
        std::vector<std::string>                           disk;
    };

    ALScriptSpan span(S32 line, S32 column, S32 length)
    {
        ALScriptSpan out;
        out.line      = line;
        out.column    = column;
        out.endLine   = line;
        out.endColumn = column + length;
        return out;
    }
    Doc::Place place(const std::string& file, S32 line, S32 column, S32 length = 5, const std::string& file_name = std::string())
    {
        Doc::Place out;
        out.span     = span(line, column, length);
        out.file     = file;
        out.fileName = file_name;
        return out;
    }
    ALScriptReferences refsOf(const std::string& name, bool renamable = true)
    {
        ALScriptReferences refs;
        refs.found     = true;
        refs.name      = name;
        refs.renamable = renamable;
        return refs;
    }

    // An expansion as the preprocessor makes one: an include's lines, then
    // the script's own, each copied from where it stood, an #include line
    // making nothing.
    ALPreprocessor::Result expansion(const std::string& name, const std::string& path, const std::string& text,
                                     const std::string& inc_name = std::string(), const std::string& inc_path = std::string(),
                                     const std::string& inc_text = std::string())
    {
        ALPreprocessor::Result result;
        result.map.addFile(name, path);
        const S32 inc = inc_path.empty() ? -1 : result.map.addFile(inc_name, inc_path);
        S32       out = 0;
        const auto copy = [&](S32 file, const std::string& source) {
            std::istringstream in(source);
            std::string        line;
            for (S32 l = 0; std::getline(in, line); ++l)
            {
                if (line.rfind("#include", 0) == 0)
                {
                    continue;
                }
                if (!line.empty())
                {
                    ALSourceMap::Segment segment;
                    segment.outLine = out;
                    segment.length  = static_cast<S32>(line.size());
                    segment.file    = file;
                    segment.line    = l;
                    result.map.add(segment);
                }
                result.text += line + "\n";
                ++out;
            }
        };
        if (inc >= 0)
        {
            copy(inc, inc_text);
        }
        copy(0, text);
        result.map.finish();
        return result;
    }
    // SLua read apart: the script with its require a call, and the module
    // it reaches its own piece, each mapped line for line to its file.
    ALPreprocessor::Result apartExpansion(const std::string& name, const std::string& path, const std::string& text, const std::string& required,
                                          const std::string& mod_name, const std::string& mod_path, const std::string& mod_text)
    {
        ALPreprocessor::Result result;
        result.map.addFile(name, path);
        result.map.addFile(mod_name, mod_path);
        const auto piece = [&](const std::string& key, S32 file, const std::string& source) {
            ALPreprocessor::Result::Piece made;
            made.key  = key;
            made.text = source;
            made.map.addFile(name, path);
            made.map.addFile(mod_name, mod_path);
            std::istringstream in(source);
            std::string        line;
            for (S32 l = 0; std::getline(in, line); ++l)
            {
                ALSourceMap::Segment segment;
                segment.outLine = l;
                segment.length  = static_cast<S32>(line.size());
                segment.file    = file;
                segment.line    = l;
                made.map.add(segment);
            }
            made.map.finish();
            return made;
        };
        result.apart.valid  = true;
        result.apart.script = piece(std::string(), 0, text);
        result.apart.modules.push_back(piece(mod_path, 1, mod_text));
        result.resolved.push_back({ std::string(), required, true, mod_path });
        result.text = "-- the bundle\n";
        result.map.finish();
        return result;
    }
    ALScriptAnalysis::Result answer(std::vector<ALScriptSpan> spans)
    {
        ALScriptAnalysis::Result result;
        result.references.found      = true;
        result.references.references = std::move(spans);
        return result;
    }
    std::string joined(const Names& names)
    {
        std::string out;
        for (const std::string& name : names)
        {
            out += (out.empty() ? "" : ", ") + name;
        }
        return out;
    }
    bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
}

namespace
{
    struct alscriptlookup_data
    {
        // The viewer the units ask, attached first and let go of last.
        al_studio_test::StudioViewer viewer;
        al_studio_test::StudioWindow                      window;
        al_studio_test::FakeServices                      services;
        FakeLookupWindow                                  studio;
        std::unique_ptr<al_studio_test::StudioNavigation> navigation;
        std::unique_ptr<ALScriptLookup>                   unit;
        const LLUUID                                      object{ "11111111-1111-1111-1111-111111111111" };
        const ALScriptRef               a{ object, LLUUID("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa") };
        const ALScriptRef               b{ object, LLUUID("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb") };
        const ALScriptRef               c{ object, LLUUID("cccccccc-cccc-cccc-cccc-cccccccccccc") };
        const ALScriptRef               d{ object, LLUUID("dddddddd-dddd-dddd-dddd-dddddddddddd") };
        const std::string               A_TEXT = "integer count;\ndefault { state_entry() { count = 1; } }\n";
        const std::string               B_TEXT = "#include \"A\"\nfoo() { count = 2; }\n";

        alscriptlookup_data()
        {
            auto& sources = viewer.slots;
            sources.keywords                      = [](bool lua) {
                LLSD keywords;
                keywords["functions"][lua ? "ll.Say" : "llSay"] = LLSD().with("return", "");
                return keywords;
            };
            sources.preprocessorWords = [] { return std::vector<std::string>{ "switch" }; };
            ALScriptStudioWords::forget();
        }
        ~alscriptlookup_data()
        {
            ALScriptStudioWords::forget();
            gFocusMgr.setKeyboardFocus(nullptr);
        }
        ALScriptLookup& make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            navigation = std::make_unique<al_studio_test::StudioNavigation>(services);
            unit       = std::make_unique<ALScriptLookup>(services, studio, studio, navigation->unit, studio);
            return *unit;
        }
        Doc& tab(const std::string& id, const ALScriptRef& ref, const std::string& text, const std::string& name = std::string())
        {
            Doc& doc       = services.addDoc(id, ref, name);
            doc.loaded     = true;
            doc.modifiable = true;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name     = "editor_" + id;
            p.rect     = LLRect(0, 200, 400, 0);
            doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(doc.editor);
            doc.editor->setText(text);
            doc.editor->resetDirty();
            return doc;
        }
        // Script A in front, a lookup of `count` started in it: its own
        // places given, declared in it where `defined`.
        Doc& lookUp(ALEditorCommand command, bool defined = true, bool renamable = true)
        {
            Doc* doc = services.findDoc("a");
            if (!doc)
            {
                doc = &tab("a", a, A_TEXT, "A");
            }
            unit->start(*doc, command, refsOf("count", renamable), defined, std::string(), span(0, 8, 5),
                        { place("", 1, 26), place("", 0, 8) }, doc->editor->document().version());
            return *doc;
        }
    };

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: places kept once and sorted own first and language words reserved", "[alscriptlookup]")
    {
        Doc::Lookup lookup;
        ALScriptLookup::addPlace(lookup, place("", 1, 2));
        ALScriptLookup::addPlace(lookup, place("", 1, 2, 7));
        ALScriptLookup::addPlace(lookup, place("x", 1, 2));
        UNSCOPED_INFO("once a file and a start");
        CHECK(lookup.places.size() == size_t(2));

        std::vector<Doc::Place> places = { place("object:z", 0, 0, 5, "Beta"),  place("", 3, 0, 5, "Zeta"),
                                           place("object:y", 2, 0, 5, "Alpha"), place("object:x", 0, 0, 5, "Beta"),
                                           place("", 1, 0),                     place("object:y", 1, 0, 5, "Alpha") };
        ALScriptLookup::sortPlaces(places);
        Names order;
        for (const Doc::Place& each : places)
        {
            order.push_back(each.file + ":" + std::to_string(each.span.line));
        }
        UNSCOPED_INFO("own whatever it is called, then by name, file and place");
        CHECK(joined(order) ==
                      joined(Names{ ":1", ":3", "object:y:1", "object:y:2", "object:x:0", "object:z:0" }));

        {
            INFO("an LSL keyword");
            CHECK(ALScriptLookup::reserved(false, "state"));
            CHECK_FALSE(ALScriptLookup::reserved(true, "state"));
        }
        {
            INFO("a Luau keyword");
            CHECK(ALScriptLookup::reserved(true, "local"));
            CHECK_FALSE(ALScriptLookup::reserved(false, "local"));
        }
        {
            INFO("the preprocessor's, in LSL");
            CHECK(ALScriptLookup::reserved(false, "switch"));
            CHECK_FALSE(ALScriptLookup::reserved(true, "switch"));
        }
        {
            INFO("the definitions'");
            CHECK(ALScriptLookup::reserved(false, "llSay"));
            CHECK(ALScriptLookup::reserved(true, "ll.Say"));
        }
        {
            INFO("a name of the script's");
            CHECK_FALSE(ALScriptLookup::reserved(false, "count"));
            CHECK_FALSE(ALScriptLookup::reserved(true, "count"));
        }
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: a name declared nowhere shared is found in the script alone at once", "[alscriptlookup]")
    {
        make();
        studio.others = { { b, "B" } };
        tab("a", a, A_TEXT, "A").preview = true;
        Doc& doc      = lookUp(ALEditorCommand::FindReferences, false);
        UNSCOPED_INFO("the preview held");
        CHECK_FALSE(doc.preview);
        {
            INFO("nothing else asked");
            CHECK(studio.asked.empty());
            CHECK(studio.loads.empty());
        }
        CHECK(studio.shows == 1);
        CHECK(studio.shownIn == std::string("a"));
        CHECK(studio.shown.from == std::string("a"));
        CHECK(studio.shown.fromName == std::string("A"));
        CHECK(studio.shown.name == std::string("count"));
        {
            INFO("in order");
            REQUIRE(studio.shown.places.size() == 2);
            CHECK(studio.shown.places[0].span.line == 0);
        }
        CHECK_FALSE(studio.shown.hasDefinition);
        REQUIRE_FALSE(services.statuses.empty());
        CHECK(has(services.statuses.back(), "ReferencesFound [COUNT]=2"));
        {
            INFO("done with");
            CHECK(doc.lookup->command == ALEditorCommand::None);
            CHECK(doc.lookup->pending == 0);
        }
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: the object's other scripts are read and expanded and asked for places", "[alscriptlookup]")
    {
        make();
        tab("c", c, "default { touch_start(integer n) { } }\n", "C");
        studio.others = { { c, "C" }, { b, "B" } };
        Doc& doc      = lookUp(ALEditorCommand::FindReferences);
        UNSCOPED_INFO("candidates asked for");
        CHECK(studio.asked == Names{ "a" });
        REQUIRE(studio.loads.size() == size_t(1));
        UNSCOPED_INFO("the open one passed over");
        CHECK(studio.expands.empty());
        {
            INFO("waiting said");
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(has(services.statuses.back(), "LookingAcross [COUNT]=1 [NAME]=count"));
        }
        UNSCOPED_INFO("nothing shown yet");
        CHECK(studio.shows == 0);

        const LLUUID asset("eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee");
        studio.loads[0].loaded(asset, B_TEXT);
        REQUIRE(studio.expands.size() == size_t(1));
        const ALScriptPreprocessor::Request& request = studio.expands[0].request;
        {
            INFO("as the compiler sees it, not optimized");
            CHECK(request.ref == b);
            CHECK(request.name == "B");
            CHECK(request.assetId == asset);
            CHECK(request.sourceText() == B_TEXT);
            CHECK_FALSE(request.optimize);
        }

        studio.expands[0].expanded(
            expansion("B", ALScriptPreprocessor::pathOf(b), B_TEXT, "A", ALScriptPreprocessor::pathOf(a), "integer count;\n"));
        REQUIRE(studio.asks.size() == size_t(1));
        const ALScriptAnalysis::Request& asked = studio.asks[0].request;
        {
            INFO("where the declaration went");
            CHECK(asked.kind == ALScriptAnalysis::Kind::References);
            CHECK(asked.line == 0);
            CHECK(asked.column == 8);
        }
        UNSCOPED_INFO("by who asked and the script");
        CHECK(asked.id == "lookup:" + doc.id + ":" + b.id());
        UNSCOPED_INFO("of this lookup");
        CHECK(asked.version == doc.lookup->generation);

        studio.asks[0].answered(answer({ span(0, 8, 5), span(1, 8, 5) }));
        CHECK(studio.shows == 1);
        UNSCOPED_INFO("its own through the other's expansion left out");
        REQUIRE(studio.shown.places.size() == size_t(3));
        const Doc::Place& other = studio.shown.places[2];
        CHECK(other.file == ALScriptPreprocessor::pathOf(b));
        CHECK(other.fileName == std::string("B"));
        CHECK(other.text == std::string("foo() { count = 2; }"));
        UNSCOPED_INFO("the name in it");
        CHECK(other.at == 8);
        CHECK(studio.shown.hasDefinition);
        CHECK(has(services.statuses.back(), "ReferencesFoundAcross [COUNT]=3 [FILES]=2"));
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: an open script is expanded from its tab and an include's line read once", "[alscriptlookup]")
    {
        make();
        Doc& open     = tab("b", b, B_TEXT, "B");
        studio.others = { { b, "B" }, { c, "C" } };
        const std::string inc = "disk:/scripts/inc.lsl";
        studio.lines[inc + "#0"] = "   integer count; // kept";
        Doc& doc = services.findDoc("a") ? *services.findDoc("a") : tab("a", a, A_TEXT, "A");
        unit->start(doc, ALEditorCommand::FindReferences, refsOf("count"), true, inc, span(0, 8, 5), {}, doc.editor->document().version());
        {
            INFO("the open one expanded at once");
            REQUIRE(studio.expands.size() == 1);
            CHECK(studio.expands[0].request.sourceText() == open.editor->text());
        }
        UNSCOPED_INFO("from its tab's own copy, not another");
        CHECK(studio.expands[0].request.source == open.snapshot());
        UNSCOPED_INFO("the other read");
        REQUIRE(studio.loads.size() == size_t(1));
        {
            INFO("both waited for");
            CHECK(doc.lookup->pending == 2);
            CHECK(studio.shows == 0);
        }

        studio.expands[0].expanded(expansion("B", ALScriptPreprocessor::pathOf(b), B_TEXT, "inc.lsl", inc, "integer count;\n"));
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks[0].answered(answer({ span(0, 8, 5) }));
        {
            INFO("still one to come");
            CHECK(studio.shows == 0);
            CHECK(doc.lookup->pending == 1);
        }
        studio.loads[0].loaded(LLUUID::null, "");
        UNSCOPED_INFO("an empty one passed over");
        CHECK(studio.shows == 1);
        REQUIRE(studio.shown.places.size() == size_t(1));
        const Doc::Place& found = studio.shown.places[0];
        {
            INFO("in the include");
            CHECK(found.file == inc);
            CHECK(found.fileName == "inc.lsl");
        }
        {
            INFO("as the include reads");
            CHECK(found.text == "integer count; // kept");
            CHECK(found.at == 5);
        }
        UNSCOPED_INFO("its lines asked for once");
        CHECK(studio.linesAsked[inc] == 1);

        studio.lines.clear();
        unit->start(doc, ALEditorCommand::FindReferences, refsOf("count"), true, inc, span(0, 8, 5), {}, doc.editor->document().version());
        // This lookup's own questions answered, not the last one's.
        CHECK(studio.expands.size() == size_t(2));
        CHECK(studio.loads.size() == size_t(2));
        studio.expands.back().expanded(expansion("B", ALScriptPreprocessor::pathOf(b), B_TEXT, "inc.lsl", inc, "integer count;\n"));
        CHECK(studio.asks.size() == size_t(2));
        studio.asks.back().answered(answer({ span(0, 8, 5) }));
        studio.loads.back().loaded(LLUUID::null, "default { }\n");
        UNSCOPED_INFO("shown again");
        CHECK(studio.shows == 2);
        {
            INFO("the expansion's line, the name not placed in it");
            REQUIRE(studio.shown.places.size() == 1);
            CHECK(studio.shown.places[0].text == "integer count;");
            CHECK(studio.shown.places[0].at == -1);
        }
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: answers to an earlier lookup or after the unit is gone are dropped", "[alscriptlookup]")
    {
        make();
        studio.others = { { b, "B" } };
        Doc& doc      = lookUp(ALEditorCommand::FindReferences);
        lookUp(ALEditorCommand::FindReferences, false);
        UNSCOPED_INFO("the second shown");
        CHECK(studio.shows == 1);
        REQUIRE(studio.loads.size() == size_t(1));
        studio.loads[0].loaded(LLUUID::null, B_TEXT);
        {
            INFO("the first's answer dropped");
            CHECK(studio.expands.empty());
            CHECK(studio.shows == 1);
        }

        lookUp(ALEditorCommand::FindReferences);
        CHECK(studio.loads.size() == size_t(2));
        studio.loads.back().loaded(LLUUID::null, B_TEXT);
        REQUIRE(studio.expands.size() == size_t(1));
        studio.expands.back().expanded(expansion("B", ALScriptPreprocessor::pathOf(b), B_TEXT));
        UNSCOPED_INFO("no declaration in it: nothing asked");
        CHECK(studio.asks.empty());
        UNSCOPED_INFO("shown without it");
        CHECK(studio.shows == 2);

        const U32 generation = doc.lookup->generation;
        lookUp(ALEditorCommand::FindReferences);
        CHECK(doc.lookup->generation != generation);
        CHECK(studio.loads.size() == size_t(3));
        studio.loads.back().loaded(LLUUID::null, B_TEXT);
        CHECK(studio.expands.size() == size_t(2));
        // A lookup of another tab's, still waiting on its read, as this
        // one waits on its expansion: both current, and both gone with it.
        Doc& other = tab("x", c, A_TEXT, "X");
        unit->start(other, ALEditorCommand::FindReferences, refsOf("count"), true, std::string(), span(0, 8, 5), {},
                    other.editor->document().version());
        CHECK(studio.loads.size() == size_t(4));
        const size_t expands = studio.expands.size();
        unit.reset();
        studio.expands.back().expanded(
            expansion("B", ALScriptPreprocessor::pathOf(b), B_TEXT, "A", ALScriptPreprocessor::pathOf(a), "integer count;\n"));
        {
            INFO("nothing asked once gone");
            CHECK(studio.asks.empty());
            CHECK(studio.shows == 2);
        }
        studio.loads.back().loaded(LLUUID::null, B_TEXT);
        UNSCOPED_INFO("nothing read once gone");
        CHECK(studio.expands.size() == expands);
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: Rename says what is not renamable and hints at what return will do", "[alscriptlookup]")
    {
        make();
        lookUp(ALEditorCommand::Rename, false, false);
        {
            INFO("not renamable");
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(has(services.statuses.back(), "NotRenamable"));
            CHECK(studio.askedName.empty());
        }

        Doc& doc = lookUp(ALEditorCommand::Rename, false);
        UNSCOPED_INFO("asked in its tab");
        CHECK(studio.askedName == std::string("a"));
        ALScriptOutlineEntry entry;
        entry.name = "total";
        doc.outline.push_back(entry);
        UNSCOPED_INFO("nothing typed");
        CHECK(has(studio.hint(""), "RenameHint [COUNT]=2 [FILES]=1 [NAME]=count"));
        CHECK(has(studio.hint("9lives"), "RenameBadName"));
        CHECK(has(studio.hint("9lives"), "[NAME]=9lives"));
        CHECK(has(studio.hint("state"), "RenameReserved [NAME]=state"));
        CHECK(has(studio.hint(" count "), "RenameSame"));
        UNSCOPED_INFO("a clash said, allowed");
        CHECK(has(studio.hint("total"), "RenameClash"));
        {
            INFO("what it will do");
            CHECK(has(studio.hint("sum"), "RenameTo [COUNT]=2"));
            CHECK(has(studio.hint("sum"), "[NEW]=sum"));
        }
        services.docs.clear();
        UNSCOPED_INFO("its tab gone: nothing said");
        CHECK(studio.hint("sum").empty());
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: a rename across scripts edits open tabs and opens others with it pending", "[alscriptlookup]")
    {
        make();
        Doc& doc   = tab("a", a, A_TEXT, "A");
        Doc& open  = tab("b", b, "foo() { count = 2; }\nbar() { total = count; }\n", "B");
        Doc& moved = tab("c", c, "baz() { count = 3; }\n", "C");
        services.whenOpened = [this](const ALScriptRef& ref, const std::string& name) {
            Doc& opened = services.addDoc("d", ref, name);
            opened.modifiable = true;
        };
        studio.whenFileOpened = [this](const std::string& path) {
            return &tab("disk:" + path, ALScriptRef(), "integer count = 4;\n", "e.lsl");
        };
        const std::string pb = ALScriptPreprocessor::pathOf(b), pc = ALScriptPreprocessor::pathOf(c), pd = ALScriptPreprocessor::pathOf(d);
        unit->start(doc, ALEditorCommand::Rename, refsOf("count"), false, std::string(), span(0, 8, 5),
                    { place("", 0, 8), place("", 1, 26), place(pb, 0, 8, 5, "B"), place(pb, 1, 8, 5, "B"), place(pb, 1, 16, 5, "B"),
                      place(pc, 0, 8, 5, "C"), place(pd, 2, 4, 5, "D"), place("disk:/e.lsl", 0, 8, 5, "e.lsl"), place("weird:x", 0, 0) },
                    doc.editor->document().version());
        moved.editor->insertText("// ");
        UNSCOPED_INFO("asked");
        REQUIRE((bool)studio.chosen);
        CHECK(has(studio.hint("sum"), "RenameToAcross [COUNT]=9 [FILES]=6"));

        studio.chosen("9lives");
        REQUIRE_FALSE(services.statuses.empty());
        CHECK(has(services.statuses.back(), "RenameBadName"));
        CHECK(doc.editor->text() == A_TEXT);
        studio.chosen("state");
        CHECK(has(services.statuses.back(), "RenameReserved"));
        studio.chosen("count");
        {
            INFO("the same: nothing");
            CHECK(services.reports.empty());
            CHECK(doc.editor->text() == A_TEXT);
        }

        studio.chosen(" total ");
        UNSCOPED_INFO("here");
        CHECK(doc.editor->text() == std::string("integer total;\ndefault { state_entry() { total = 1; } }\n"));
        UNSCOPED_INFO("one step");
        CHECK(doc.editor->undoJournal().undoLabel() == std::string("rename"));
        UNSCOPED_INFO("open and unchanged: where it still stands");
        CHECK(open.editor->text() ==
                      std::string("foo() { total = 2; }\nbar() { total = total; }\n"));
        UNSCOPED_INFO("there as one step too");
        CHECK(open.editor->undoJournal().undoLabel() == std::string("rename"));
        {
            INFO("moved on since: left");
            CHECK(has(moved.editor->text(), "count"));
            CHECK_FALSE(has(moved.editor->text(), "total"));
        }
        {
            INFO("not open: opened");
            REQUIRE(services.opened.size() == 1);
            CHECK(services.opened[0].ref == d);
            CHECK(services.opened[0].name == "D");
        }
        Doc* waiting = services.findDoc("d");
        {
            INFO("the change waiting");
            REQUIRE(waiting);
            REQUIRE(waiting->pendingEdits.size() == 1);
            CHECK(waiting->pendingEdits[0].now == "total");
        }
        UNSCOPED_INFO("a file opened");
        CHECK(studio.filesOpened == Names{ "/e.lsl" });
        const Doc* file = services.findDoc("disk:/e.lsl");
        {
            INFO("and changed");
            REQUIRE(file);
            CHECK(file->editor->text() == std::string("integer total = 4;\n"));
        }
        UNSCOPED_INFO("said once");
        REQUIRE(services.reports.size() == size_t(1));
        const al_studio_test::FakeServices::Said& said = services.reports[0];
        CHECK(has(said.text, "RenamedIn [NAME]=total [PLACES]=Places [COUNT]=6 [SCRIPTS]=Scripts [COUNT]=4"));
        UNSCOPED_INFO("opened, a clause joined on");
        CHECK(has(said.text, "[SECOND]=RenamedOpened [NAME]=total [PLACES]=Places [COUNT]=6 [SCRIPTS]=Scripts [COUNT]=2"));
        UNSCOPED_INFO("left, another");
        CHECK(has(said.text, "[SECOND]=RenamedLeft [NAME]=total [PLACES]=Places [COUNT]=6 [SCRIPTS]=Scripts [COUNT]=2"));
        UNSCOPED_INFO("as the language joins clauses and ends a sentence");
        CHECK(has(said.text, "Sentence [TEXT]=JoinClauses [FIRST]=JoinClauses"));
        UNSCOPED_INFO("a failure where any was left");
        CHECK(said.failure);
        UNSCOPED_INFO("its tab brought forward");
        CHECK(studio.activated == Names{ "a" });
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: a stale rename is refused and one in this script alone is said as here", "[alscriptlookup]")
    {
        make();
        Doc& doc = lookUp(ALEditorCommand::Rename, false);
        doc.editor->insertText(" ");
        studio.chosen("total");
        REQUIRE_FALSE(services.statuses.empty());
        CHECK(has(services.statuses.back(), "RenameStale [NAME]=total"));
        CHECK(services.reports.empty());

        lookUp(ALEditorCommand::Rename, false);
        studio.chosen("total");
        REQUIRE(services.reports.size() == size_t(1));
        {
            INFO("here");
            CHECK(has(services.reports[0].text, "RenamedHere"));
            CHECK_FALSE(has(services.reports[0].text, ";"));
        }
        CHECK_FALSE(services.reports[0].failure);

        Doc& open = tab("b", b, "foo() { count = 2; }\n", "B");
        doc.editor->setText(A_TEXT);
        unit->start(doc, ALEditorCommand::Rename, refsOf("count"), false, std::string(), span(0, 8, 5),
                    { place("", 0, 8), place(ALScriptPreprocessor::pathOf(b), 0, 8, 5, "B") }, doc.editor->document().version());
        studio.chosen("sum");
        {
            INFO("in both, none opened");
            CHECK(has(services.reports.back().text, "RenamedIn"));
            CHECK_FALSE(has(services.reports.back().text, ";"));
        }
        UNSCOPED_INFO("B too");
        CHECK(open.editor->text() == std::string("foo() { sum = 2; }\n"));
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: pending edits are made where the old name stands and the rest said missed", "[alscriptlookup]")
    {
        make();
        Doc& doc   = tab("d", d, "integer count;\nx = count;\n", "D");
        doc.loaded = false;
        doc.pendingEdits = { { span(0, 8, 5), "count", "total" }, { span(1, 0, 5), "count", "total" } };
        unit->applyPendingEdits(doc);
        {
            INFO("not loaded: kept");
            CHECK(doc.pendingEdits.size() == 2);
            CHECK(doc.editor->text() == "integer count;\nx = count;\n");
        }

        doc.loaded = true;
        doc.editor->setReadOnly(true);
        unit->applyPendingEdits(doc);
        {
            INFO("made");
            CHECK(doc.pendingEdits.empty());
            CHECK(doc.editor->text() == "integer total;\nx = count;\n");
        }
        UNSCOPED_INFO("one step");
        CHECK(doc.editor->undoJournal().undoLabel() == std::string("rename"));
        {
            INFO("the one missed said");
            REQUIRE(services.reports.size() == 1);
            CHECK(has(services.reports[0].text, "RenameMissed [COUNT]=1 [NAME]=D"));
        }
        {
            INFO("of the tab, a failure");
            CHECK(services.reports[0].doc == "d");
            CHECK(services.reports[0].failure);
        }

        Doc::PendingEdit replace{ span(0, 8, 5), "total", "sum", true };
        doc.pendingEdits = { replace };
        unit->applyPendingEdits(doc);
        {
            INFO("a replace");
            CHECK(doc.editor->text() == "integer sum;\nx = count;\n");
            CHECK(doc.editor->undoJournal().undoLabel() == "replace");
        }
        UNSCOPED_INFO("nothing missed");
        CHECK(services.reports.size() == 1);

        doc.modifiable   = false;
        doc.pendingEdits = { { span(0, 8, 3), "sum", "total", true } };
        unit->applyPendingEdits(doc);
        UNSCOPED_INFO("none made");
        CHECK(doc.editor->text() == "integer sum;\nx = count;\n");
        {
            INFO("every one missed");
            REQUIRE(services.reports.size() == 2);
            CHECK(has(services.reports[1].text, "ReplaceMissed [COUNT]=1"));
        }
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: other scripts are read a few at a time and a new lookup drops the rest", "[alscriptlookup]")
    {
        make();
        const ALScriptRef e{ object, LLUUID("eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee") };
        const ALScriptRef f{ object, LLUUID("ffffffff-ffff-ffff-ffff-ffffffffffff") };
        studio.others = { { b, "B" }, { c, "C" }, { d, "D" }, { e, "E" }, { f, "F" } };
        Doc& doc      = lookUp(ALEditorCommand::FindReferences);
        UNSCOPED_INFO("no more than so many read at once");
        REQUIRE(studio.loads.size() == size_t(ALScriptLookup::AT_ONCE));
        UNSCOPED_INFO("every one waited for");
        CHECK(doc.lookup->pending == S32(5));
        // One that does not name it: passed over, and the next read.
        studio.loads[0].loaded(LLUUID::null, "default { }\n");
        UNSCOPED_INFO("the next begun");
        REQUIRE(studio.loads.size() == size_t(ALScriptLookup::AT_ONCE + 1));
        UNSCOPED_INFO("one fewer waited for");
        CHECK(doc.lookup->pending == S32(4));
        studio.loads[1].loaded(LLUUID::null, "");
        studio.loads[2].loaded(LLUUID::null, "");
        UNSCOPED_INFO("and on");
        REQUIRE(studio.loads.size() == size_t(5));
        studio.loads[3].loaded(LLUUID::null, "");
        studio.loads[4].loaded(LLUUID::null, "");
        {
            INFO("all done with: shown");
            CHECK(doc.lookup->pending == 0);
            CHECK(studio.shows == 1);
        }

        // Begun again with some still to read: what the first had not
        // begun is let go of, and its answers dropped.
        studio.loads.clear();
        lookUp(ALEditorCommand::FindReferences);
        UNSCOPED_INFO("two read");
        REQUIRE(studio.loads.size() == size_t(2));
        lookUp(ALEditorCommand::FindReferences);
        UNSCOPED_INFO("two more, for the new one");
        REQUIRE(studio.loads.size() == size_t(4));
        studio.loads[0].loaded(LLUUID::null, "");
        studio.loads[1].loaded(LLUUID::null, "");
        UNSCOPED_INFO("the first's answers begin nothing");
        REQUIRE(studio.loads.size() == size_t(4));
        studio.loads[2].loaded(LLUUID::null, "");
        UNSCOPED_INFO("the new one's go on");
        CHECK(studio.loads.size() == size_t(5));
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: folderCandidates are the folder's other scripts in the same language", "[alscriptlookup]")
    {
        const auto made = [](const char* name, LLAssetType::EType type, U32 subtype, const char* runtime) {
            LLPointer<LLInventoryItem> item = new LLInventoryItem();
            item->setUUID(LLUUID::generateNewID());
            item->rename(name);
            item->setType(type);
            item->setFlags(subtype);
            item->setRuntime(runtime);
            return item;
        };
        const LLPointer<LLInventoryItem> own     = made("door.lsl", LLAssetType::AT_LSL_TEXT, SST_LSL, "mono");
        const LLPointer<LLInventoryItem> lib     = made("lib.lsl", LLAssetType::AT_LSL_TEXT, SST_LSL, "mono");
        const LLPointer<LLInventoryItem> lua     = made("door.luau", LLAssetType::AT_LSL_TEXT, SST_LUA, "");
        const LLPointer<LLInventoryItem> onluau  = made("port.luau", LLAssetType::AT_LSL_TEXT, SST_LSL, "luau");
        const LLPointer<LLInventoryItem> readme  = made("readme", LLAssetType::AT_NOTECARD, 0, "");
        const std::vector<const LLInventoryItem*> folder = { own.get(), lib.get(), lua.get(), onluau.get(), readme.get() };

        const std::vector<ALScriptLookup::Candidate> lsl = ALScriptLookup::folderCandidates(folder, own->getUUID(), false);
        {
            INFO("LSL: the other LSL script");
            REQUIRE(lsl.size() == 1);
            CHECK(lsl[0].name == "lib.lsl");
            CHECK(lsl[0].ref.inInventory());
            CHECK(lsl[0].ref.item == lib->getUUID());
        }
        const std::vector<ALScriptLookup::Candidate> slua = ALScriptLookup::folderCandidates(folder, lua->getUUID(), true);
        {
            INFO("SLua: by runtime too");
            REQUIRE(slua.size() == 1);
            CHECK(slua[0].name == "port.luau");
        }
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: a previewed rename is made at the places kept and clashes only with code", "[alscriptlookup]")
    {
        make();
        lookUp(ALEditorCommand::Rename, false);
        UNSCOPED_INFO("previewing offered");
        REQUIRE((bool)studio.previewed);
        UNSCOPED_INFO("the prompt says Shift-Return shows it first");
        CHECK(has(studio.hint("sum"), "RenamePreviewHint"));
        studio.previewed("9lives");
        {
            INFO("no name: nothing shown");
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(has(services.statuses.back(), "RenameBadName"));
            CHECK_FALSE(studio.apply);
        }
        studio.previewed("sum");
        {
            INFO("shown from its tab");
            CHECK(studio.previewIn == "a");
            CHECK(studio.previewName == "sum");
            REQUIRE(studio.previewOf.places.size() == 2);
        }
        {
            INFO("what it will do said over them");
            CHECK(has(studio.previewSaid, "RenameTo [COUNT]=2"));
            CHECK_FALSE(has(studio.previewSaid, "RenamePreviewHint"));
        }
        studio.apply({ 1 });
        Doc* found = services.findDoc("a");
        REQUIRE(found);
        Doc& doc = *found;
        UNSCOPED_INFO("made at the one kept");
        CHECK(doc.editor->text() == std::string("integer count;\ndefault { state_entry() { sum = 1; } }\n"));

        // A local of the same name -- in no outline -- is a clash; a name in
        // a comment or a string is not.
        doc.editor->setText("integer count;\ndefault { state_entry() { integer total = count; llSay(0, \"hidden\"); } } // gone\n");
        lookUp(ALEditorCommand::Rename, false);
        {
            INFO("a local's name");
            CHECK(has(studio.hint("total"), "RenameClash"));
            CHECK(has(studio.hint("total"), "[SCRIPT]=A"));
        }
        UNSCOPED_INFO("not a string's");
        CHECK_FALSE(has(studio.hint("hidden"), "RenameClash"));
        UNSCOPED_INFO("nor a comment's");
        CHECK_FALSE(has(studio.hint("gone"), "RenameClash"));

        // A place in an include: the scripts elsewhere that include it are
        // not reached, and said to be so.
        unit->start(doc, ALEditorCommand::Rename, refsOf("count"), true, "disk:/lib.lsl", span(0, 8, 5),
                    { place("", 1, 26), place("disk:/lib.lsl", 0, 8, 5, "lib.lsl") }, doc.editor->document().version());
        UNSCOPED_INFO("out of reach said");
        CHECK(has(studio.hint("sum"), "RenameUnreached [NAMES]=lib.lsl"));

        UNSCOPED_INFO("SLua: a comment is not a mention");
        CHECK_FALSE(ALScriptLookup::mentions("local a = 1 -- total\n", "total", true));
        UNSCOPED_INFO("nor a long comment");
        CHECK_FALSE(ALScriptLookup::mentions("--[[ total ]] local b = 2", "total", true));
        UNSCOPED_INFO("nor a long string");
        CHECK_FALSE(ALScriptLookup::mentions("local s = [==[ total ]==]", "total", true));
        UNSCOPED_INFO("a local is");
        CHECK(ALScriptLookup::mentions("local total = 1", "total", true));
        UNSCOPED_INFO("not part of a longer name");
        CHECK_FALSE(ALScriptLookup::mentions("integer totals;", "total", false));
        UNSCOPED_INFO("LSL: nor in a block comment");
        CHECK_FALSE(ALScriptLookup::mentions("/* total */ integer x;", "total", false));
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: the object's scripts wait on its prims and what was missed is said", "[alscriptlookup]")
    {
        make();
        studio.others         = { { b, "B" }, { c, "C" } };
        studio.unlisted       = 2;
        studio.holdCandidates = true;
        lookUp(ALEditorCommand::FindReferences);
        CHECK(studio.asked == Names{ "a" });
        UNSCOPED_INFO("nothing read while the prims are asked");
        CHECK(studio.loads.empty());
        CHECK(studio.shows == 0);
        studio.heldCandidates();
        UNSCOPED_INFO("both read");
        REQUIRE(studio.loads.size() == size_t(2));
        studio.loads[0].loaded(LLUUID::null, std::nullopt);
        UNSCOPED_INFO("one still out");
        CHECK(studio.shows == 0);
        studio.loads[1].loaded(LLUUID::null, std::string("default { }\n"));
        CHECK(studio.shows == 1);
        REQUIRE_FALSE(services.statuses.empty());
        const std::string said = services.statuses.back();
        {
            INFO("found, then what was passed over: " + said);
            CHECK(has(said, "ReferencesFound"));
            CHECK(has(said, "LookupUnlisted [COUNT]=2"));
            CHECK(has(said, "LookupUnread [COUNT]=1 [NAMES]=B"));
        }

        // A rename says it under the prompt, all prims listed this time.
        studio.holdCandidates = false;
        studio.unlisted       = 0;
        studio.loads.clear();
        lookUp(ALEditorCommand::Rename);
        UNSCOPED_INFO("read again");
        REQUIRE(studio.loads.size() == size_t(2));
        studio.loads[0].loaded(LLUUID::null, std::nullopt);
        studio.loads[1].loaded(LLUUID::null, std::string("default { }\n"));
        UNSCOPED_INFO("a new name asked");
        REQUIRE(static_cast<bool>(studio.hint));
        const std::string hint = studio.hint("total");
        {
            INFO("the hint says it: " + hint);
            CHECK(has(hint, "LookupUnread [COUNT]=1 [NAMES]=B"));
            CHECK_FALSE(has(hint, "LookupUnlisted"));
        }
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: ALScriptPlaces::Lines gives lines without returns and none past the end", "[alscriptlookup]")
    {
        const ALScriptPlaces::Lines held(std::make_shared<const std::string>("one\r\ntwo\n\nfour"));
        {
            INFO("four lines");
            CHECK(held.has(0));
            CHECK(held.has(3));
            CHECK_FALSE(held.has(4));
            CHECK_FALSE(held.has(-1));
        }
        {
            INFO("each as it is");
            CHECK(held.line(0) == "one");
            CHECK(held.line(1) == "two");
            CHECK(held.line(2).empty());
            CHECK(held.line(3) == "four");
        }
        UNSCOPED_INFO("none past the end");
        CHECK(held.line(9).empty());
        const std::string           text = "a\nb\n";
        const ALScriptPlaces::Lines here(text);
        {
            INFO("read where it is, the last line empty");
            CHECK(here.line(1) == "b");
            CHECK(here.has(2));
            CHECK(here.line(2).empty());
            CHECK_FALSE(here.has(3));
        }
        const ALTextDocument        open("x\ny");
        const ALScriptPlaces::Lines tab(&open);
        {
            INFO("a tab's");
            CHECK(tab.line(1) == "y");
            CHECK_FALSE(tab.has(2));
        }
        UNSCOPED_INFO("nothing: no lines");
        CHECK_FALSE(ALScriptPlaces::Lines().has(0));
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: a rename preview compares an open unchanged file with its renamed text", "[alscriptlookup]")
    {
        make();
        Doc& doc  = tab("a", a, A_TEXT, "A");
        Doc& open = tab("b", b, "foo() { count = 2; }\nbar() { total = count; }\n", "B");
        const std::string pb = ALScriptPreprocessor::pathOf(b), pd = ALScriptPreprocessor::pathOf(d);
        unit->start(doc, ALEditorCommand::Rename, refsOf("count"), false, std::string(), span(0, 8, 5),
                    { place("", 0, 8), place("", 1, 26), place(pb, 0, 8, 5, "B"), place(pb, 1, 16, 5, "B"), place(pd, 2, 4, 5, "D") },
                    doc.editor->document().version());
        studio.previewed("sum");
        REQUIRE((bool)studio.changes);

        studio.changes("", { 1, 2, 3 });
        UNSCOPED_INFO("here, at the one of its own kept");
        REQUIRE(studio.compared.size() == 1U);
        UNSCOPED_INFO("beside it as it is");
        CHECK(studio.compared.back() ==
                      "a: " + std::string(A_TEXT) + " | integer count;\ndefault { state_entry() { sum = 1; } }\n (CompareNow | RenameAfter [NAME]=sum [SCRIPT]=A)");
        REQUIRE_FALSE(studio.activated.empty());
        UNSCOPED_INFO("in its tab, brought forward");
        CHECK(studio.activated.back() == "a");
        UNSCOPED_INFO("nothing changed");
        CHECK(doc.editor->text() == A_TEXT);

        studio.changes(pb, { 2, 3 });
        UNSCOPED_INFO("another open");
        CHECK(studio.compared.back() ==
                      "b: foo() { count = 2; }\nbar() { total = count; }\n | foo() { sum = 2; }\nbar() { total = sum; }\n (CompareNow | RenameAfter [NAME]=sum [SCRIPT]=B)");
        UNSCOPED_INFO("in its tab");
        CHECK(studio.activated.back() == "b");

        const size_t before = studio.compared.size();
        studio.changes(pd, { 4 });
        {
            INFO("not open: said, not shown");
            CHECK(studio.compared.size() == before);
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(has(services.statuses.back(), "RenameChangesNotHere [NAME]=sum [SCRIPT]=D"));
            CHECK(services.statusFailures.back());
        }
        open.editor->insertText("// ");
        studio.changes(pb, { 2, 3 });
        {
            INFO("changed since: said");
            CHECK(studio.compared.size() == before);
            CHECK(has(services.statuses.back(), "RenameChangesNotHere"));
        }
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: open files on disk are looked through and renamed with the script", "[alscriptlookup]")
    {
        make();
        const std::string LIB      = "disk:/s/lib.luau";
        const std::string OTHER    = "disk:/s/b.luau";
        const std::string LIB_TEXT = "local M = {}\nfunction M.twice(n) return n * 2 end\nreturn M\n";
        const std::string B_LUA    = "local lib = require(\"./lib\")\nprint(lib.twice(4))\n";
        Doc& lib        = tab(LIB, ALScriptRef(), LIB_TEXT, "lib.luau");
        lib.file        = "/s/lib.luau";
        lib.language.lua = true;
        Doc& other        = tab(OTHER, ALScriptRef(), B_LUA, "b.luau");
        other.file        = "/s/b.luau";
        other.language.lua = true;
        Doc& lsl = tab("disk:/s/c.lsl", ALScriptRef(), "integer twice;\n", "c.lsl");
        lsl.file = "/s/c.lsl";
        Doc& doc          = tab("a", a, "local lib = require(\"./lib\")\nprint(lib.twice(2))\n", "A");
        doc.language.lua  = true;
        // Script A's own answer, its modules read apart: its place, and the
        // module's declaration.
        unit->start(doc, ALEditorCommand::Rename, refsOf("twice"), true, LIB, span(1, 11, 5),
                    { place("", 1, 10), place(LIB, 1, 11, 5, "lib.luau") }, doc.editor->document().version());
        UNSCOPED_INFO("each open file of the language, as it stands in its tab");
        REQUIRE(studio.expands.size() == size_t(2));
        const ALScriptPreprocessor::Request& module = studio.expands[0].request;
        {
            INFO("the module, by its path, no item");
            CHECK(module.path == LIB);
            CHECK(module.ref.isNull());
            CHECK(module.name == "lib.luau");
            CHECK(module.sourceText() == LIB_TEXT);
        }
        CHECK(studio.expands[1].request.path == OTHER);
        CHECK(studio.expands[1].request.sourceText() == B_LUA);

        // The module read alone is the home itself; the other script has it
        // ahead of its own lines.
        studio.expands[0].expanded(expansion("lib.luau", LIB, LIB_TEXT));
        studio.expands[1].expanded(expansion("b.luau", OTHER, B_LUA, "lib.luau", LIB, LIB_TEXT));
        UNSCOPED_INFO("both asked");
        REQUIRE(studio.asks.size() == size_t(2));
        {
            INFO("the module at its declaration");
            CHECK(studio.asks[0].request.line == 1);
            CHECK(studio.asks[0].request.column == 11);
            CHECK(studio.asks[0].request.id == "lookup:a:" + LIB);
        }
        {
            INFO("the other where the module's declaration went");
            CHECK(studio.asks[1].request.line == 1);
            CHECK(studio.asks[1].request.column == 11);
        }
        studio.asks[0].answered(answer({ span(1, 11, 5) }));
        studio.asks[1].answered(answer({ span(1, 11, 5), span(4, 10, 5) }));
        UNSCOPED_INFO("asked for the new name");
        REQUIRE((bool)studio.chosen);
        UNSCOPED_INFO("each place once: the script's, the module's, the other's");
        CHECK(doc.lookup->places.size() == size_t(3));
        UNSCOPED_INFO("the other's, by its path");
        CHECK(std::any_of(doc.lookup->places.begin(), doc.lookup->places.end(), [&](const Doc::Place& place) {
                   return place.file == OTHER && place.fileName == "b.luau" && place.span.line == 1 && place.span.column == 10;
               }));

        studio.chosen("double");
        UNSCOPED_INFO("here");
        CHECK(doc.editor->text() == std::string("local lib = require(\"./lib\")\nprint(lib.double(2))\n"));
        UNSCOPED_INFO("in the module");
        CHECK(lib.editor->text() == std::string("local M = {}\nfunction M.double(n) return n * 2 end\nreturn M\n"));
        UNSCOPED_INFO("in the other script");
        CHECK(other.editor->text() == std::string("local lib = require(\"./lib\")\nprint(lib.double(4))\n"));
        {
            INFO("each its own step");
            CHECK(lib.editor->undoJournal().undoLabel() == "rename");
            CHECK(other.editor->undoJournal().undoLabel() == "rename");
        }
        UNSCOPED_INFO("another language's file left alone");
        CHECK(lsl.editor->text() == std::string("integer twice;\n"));
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: scripts on disk in the script's folders are read and opened to be renamed", "[alscriptlookup]")
    {
        make();
        // A folder of the scripter's, as the window lists it, where it
        // stands: what the walk names each file by.
        namespace fs = std::filesystem;
        const fs::path made = fs::temp_directory_path() / ("alscriptlookup_" + LLUUID::generateNewID().asString());
        fs::create_directories(made);
        const fs::path    root     = fs::canonical(made);
        const std::string LIB_TEXT = "local M = {}\nfunction M.twice(n) return n * 2 end\nreturn M\n";
        const std::string B_LUA    = "local lib = require(\"./lib\")\nprint(lib.twice(4))\n";
        const std::string C_LUA    = "print(\"nothing of it\")\n";
        const std::string lib_file = (root / "lib.luau").string();
        const std::string b_file   = (root / "b.luau").string();
        llofstream(lib_file, std::ios::binary) << LIB_TEXT;
        llofstream(b_file, std::ios::binary) << B_LUA;
        // One that does not name it, passed over as it is read.
        llofstream((root / "c.luau").string(), std::ios::binary) << C_LUA;
        studio.disk = { root.string() };
        const std::string LIB = ALIncludeIdentity::ofFile(lib_file);
        const std::string B   = ALIncludeIdentity::ofFile(b_file);
        Doc& lib         = tab(LIB, ALScriptRef(), LIB_TEXT, "lib.luau");
        lib.file         = lib_file;
        lib.language.lua = true;
        Doc& doc         = tab("a", a, "local lib = require(\"./lib\")\nprint(lib.twice(2))\n", "A");
        doc.language.lua = true;
        unit->start(doc, ALEditorCommand::Rename, refsOf("twice"), true, LIB, span(1, 11, 5),
                    { place("", 1, 10), place(LIB, 1, 11, 5, "lib.luau") }, doc.editor->document().version());
        UNSCOPED_INFO("the module open here once, the other read from disk, and the one not naming it passed over");
        REQUIRE(studio.expands.size() == size_t(2));
        {
            INFO("the module, as its tab has it");
            CHECK(studio.expands[0].request.path == LIB);
            CHECK(studio.expands[0].request.sourceText() == LIB_TEXT);
        }
        {
            INFO("the other, as it is on disk");
            CHECK(studio.expands[1].request.path == B);
            CHECK(studio.expands[1].request.name == "b.luau");
            CHECK(studio.expands[1].request.sourceText() == B_LUA);
        }
        studio.expands[0].expanded(expansion("lib.luau", LIB, LIB_TEXT));
        studio.expands[1].expanded(expansion("b.luau", B, B_LUA, "lib.luau", LIB, LIB_TEXT));
        REQUIRE(studio.asks.size() == size_t(2));
        studio.asks[0].answered(answer({ span(1, 11, 5) }));
        studio.asks[1].answered(answer({ span(1, 11, 5), span(4, 10, 5) }));
        UNSCOPED_INFO("the other's place, by its path");
        CHECK(std::any_of(doc.lookup->places.begin(), doc.lookup->places.end(), [&](const Doc::Place& place) {
                   return place.file == B && place.span.line == 1 && place.span.column == 10;
               }));

        // Renamed: opened, as a file not open is, with the change made.
        studio.whenFileOpened = [&](const std::string& path) {
            Doc& opened         = tab(ALIncludeIdentity::ofFile(path), ALScriptRef(), B_LUA, "b.luau");
            opened.file         = path;
            opened.language.lua = true;
            return &opened;
        };
        studio.chosen("double");
        UNSCOPED_INFO("opened to be renamed");
        CHECK(studio.filesOpened == Names{ b_file });
        const Doc* other = services.findDoc(B);
        {
            INFO("renamed there");
            REQUIRE(other);
            CHECK(other->editor->text() == "local lib = require(\"./lib\")\nprint(lib.double(4))\n");
        }
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: SLua read apart asks in the module and maps places back through the piece", "[alscriptlookup]")
    {
        make();
        const std::string LIB      = "disk:/s/lib.luau";
        const std::string OTHER    = "disk:/s/b.luau";
        const std::string LIB_TEXT = "local M = {}\nfunction M.twice(n) return n * 2 end\nreturn M\n";
        const std::string B_LUA    = "local lib = require(\"./lib\")\nprint(lib.twice(4))\n";
        Doc& other         = tab(OTHER, ALScriptRef(), B_LUA, "b.luau");
        other.file         = "/s/b.luau";
        other.language.lua = true;
        Doc& doc           = tab("a", a, "local lib = require(\"./lib\")\nprint(lib.twice(2))\n", "A");
        doc.language.lua   = true;
        unit->start(doc, ALEditorCommand::FindReferences, refsOf("twice"), true, LIB, span(1, 11, 5),
                    { place("", 1, 10), place(LIB, 1, 11, 5, "lib.luau") }, doc.editor->document().version());
        UNSCOPED_INFO("the other expanded");
        REQUIRE(studio.expands.size() == size_t(1));
        UNSCOPED_INFO("apart asked for");
        CHECK(studio.expands[0].request.apart);
        studio.expands[0].expanded(apartExpansion("b.luau", OTHER, B_LUA, "./lib", "lib.luau", LIB, LIB_TEXT));
        REQUIRE(studio.asks.size() == size_t(1));
        const ALScriptAnalysis::Request& asked = studio.asks[0].request;
        {
            INFO("the script alone, its require a call");
            REQUIRE(asked.text);
            CHECK(*asked.text == B_LUA);
        }
        {
            INFO("in the module, at its declaration in its own lines");
            CHECK(asked.module == LIB);
            CHECK(asked.line == 1);
            CHECK(asked.column == 11);
        }
        {
            INFO("with its modules");
            REQUIRE(asked.modules);
            REQUIRE(asked.modules->modules.size() == 1);
            CHECK(asked.modules->modules[0].key == LIB);
            REQUIRE(asked.modules->reaches.size() == 1);
            CHECK(asked.modules->reaches[0].name == "./lib");
        }

        ALScriptAnalysis::Result result = answer({ span(1, 10, 5) });
        result.references.elsewhere.push_back({ LIB, span(1, 11, 5) });
        studio.asks[0].answered(result);
        UNSCOPED_INFO("the other's place, through its piece");
        CHECK(std::any_of(doc.lookup->places.begin(), doc.lookup->places.end(), [&](const Doc::Place& place) {
                   return place.file == OTHER && place.fileName == "b.luau" && place.span.line == 1 && place.span.column == 10;
               }));
        UNSCOPED_INFO("each once: the script's, the module's, the other's");
        CHECK(doc.lookup->places.size() == size_t(3));
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: a file open under another path to it is read once as its tab has it", "[alscriptlookup]")
    {
        make();
        namespace fs = std::filesystem;
        const fs::path made = fs::temp_directory_path() / ("alscriptlookup_" + LLUUID::generateNewID().asString());
        fs::create_directories(made / "sub");
        const fs::path    root     = fs::canonical(made);
        const std::string LIB_TEXT = "local M = {}\nfunction M.twice(n) return n * 2 end\nreturn M\n";
        const std::string B_LUA    = "local lib = require(\"./lib\")\nprint(lib.twice(4))\n";
        llofstream((root / "lib.luau").string(), std::ios::binary) << LIB_TEXT;
        llofstream((root / "b.luau").string(), std::ios::binary) << B_LUA;
        studio.disk = { root.string() };
        // The module where it stands, as the analyzers name it; and its tab,
        // opened by a way round to it.
        const std::string LIB      = ALIncludeIdentity::ofFile((root / "lib.luau").string());
        const std::string lib_file = (root / "sub" / ".." / "lib.luau").string();
        const std::string LIB_TAB  = ALIncludeIdentity::ofFile(lib_file);
        const std::string TYPED    = LIB_TEXT + "-- as typed\n";
        Doc& lib         = tab(LIB_TAB, ALScriptRef(), TYPED, "lib.luau");
        lib.file         = lib_file;
        lib.language.lua = true;
        Doc& doc         = tab("a", a, "local lib = require(\"./lib\")\nprint(lib.twice(2))\n", "A");
        doc.language.lua = true;
        unit->start(doc, ALEditorCommand::FindReferences, refsOf("twice"), true, LIB, span(1, 11, 5),
                    { place("", 1, 10), place(LIB, 1, 11, 5, "lib.luau") }, doc.editor->document().version());
        UNSCOPED_INFO("the module once, and the other");
        REQUIRE(studio.expands.size() == size_t(2));
        {
            INFO("the module as its tab has it");
            CHECK(studio.expands[0].request.path == LIB_TAB);
            CHECK(studio.expands[0].request.sourceText() == TYPED);
        }
        UNSCOPED_INFO("the other from disk");
        CHECK(studio.expands[1].request.sourceText() == B_LUA);
        studio.expands[0].expanded(expansion("lib.luau", LIB_TAB, TYPED));
        UNSCOPED_INFO("the home itself, asked at its declaration");
        REQUIRE(studio.asks.size() == size_t(1));
        CHECK(studio.asks[0].request.line == 1);
        CHECK(studio.asks[0].request.column == 11);
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: folders on disk are walked off the main thread and stale walks dropped", "[alscriptlookup]")
    {
        make();
        namespace fs = std::filesystem;
        const fs::path made = fs::temp_directory_path() / ("alscriptlookup_" + LLUUID::generateNewID().asString());
        fs::create_directories(made);
        const fs::path    root  = fs::canonical(made);
        const std::string B_LUA = "local lib = require(\"./lib\")\nprint(lib.twice(4))\n";
        llofstream((root / "b.luau").string(), std::ios::binary) << B_LUA;
        studio.disk = { root.string() };
        // A main loop's queue, as the viewer's is, chosen for this test
        // alone: the queue kept, since a walk let go of may hand back to it
        // after the test.
        LL::WorkQueue&           main_loop = newview_test::mainLoop();
        const ALMainLoop::Choice chosen(&main_loop);
        const auto pumped = [&main_loop](const std::function<bool()>& until, std::chrono::milliseconds within) {
            const auto deadline = std::chrono::steady_clock::now() + within;
            while (std::chrono::steady_clock::now() < deadline)
            {
                main_loop.runPending();
                if (until())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return false;
        };
        Doc& doc         = tab("a", a, "local lib = require(\"./lib\")\nprint(lib.twice(2))\n", "A");
        doc.language.lua = true;
        const std::string LIB = "disk:/s/lib.luau";
        unit->start(doc, ALEditorCommand::FindReferences, refsOf("twice"), true, LIB, span(1, 11, 5), { place("", 1, 10) },
                    doc.editor->document().version());
        // The thread the walks run on, made with the first of them, is the
        // process's from here, as the viewer's is until it quits.
        ll_test::StateGuard::accept("the live thread pools");
        ll_test::StateGuard::accept("the live work queues");
        UNSCOPED_INFO("nothing read here");
        CHECK(studio.expands.empty());
        {
            INFO("held open for the walk");
            CHECK(doc.lookup->pending == 1);
            CHECK(studio.shows == 0);
        }
        UNSCOPED_INFO("what it read handed back");
        REQUIRE(pumped([&] { return !studio.expands.empty(); }, std::chrono::seconds(5)));
        {
            INFO("the one naming it, as it is on disk");
            REQUIRE(studio.expands.size() == 1);
            CHECK(studio.expands[0].request.sourceText() == B_LUA);
        }
        studio.expands[0].expanded(expansion("b.luau", studio.expands[0].request.path, B_LUA));
        UNSCOPED_INFO("shown once it is done with");
        CHECK(studio.shows == 1);

        // Begun again while its walk is under way: the walk let go of, and
        // only the new lookup's read looked through.
        unit->start(doc, ALEditorCommand::FindReferences, refsOf("twice"), true, LIB, span(1, 11, 5), { place("", 1, 10) },
                    doc.editor->document().version());
        unit->start(doc, ALEditorCommand::FindReferences, refsOf("twice"), true, LIB, span(1, 11, 5), { place("", 1, 10) },
                    doc.editor->document().version());
        UNSCOPED_INFO("the new one's handed back");
        REQUIRE(pumped([&] { return studio.expands.size() > 1; }, std::chrono::seconds(5)));
        pumped([] { return false; }, std::chrono::milliseconds(300));
        UNSCOPED_INFO("and nothing of the old one's");
        REQUIRE(studio.expands.size() == size_t(2));
        studio.expands[1].expanded(expansion("b.luau", studio.expands[1].request.path, B_LUA));
        UNSCOPED_INFO("shown once");
        CHECK(studio.shows == 2);

        // Begun again, and the lookups gone before what it read is back.
        unit->start(doc, ALEditorCommand::FindReferences, refsOf("twice"), true, LIB, span(1, 11, 5), { place("", 1, 10) },
                    doc.editor->document().version());
        unit.reset();
        pumped([] { return false; }, std::chrono::milliseconds(300));
        UNSCOPED_INFO("nothing comes of it");
        CHECK(studio.expands.size() == size_t(2));
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    TEST_CASE_METHOD(alscriptlookup_data, "alscriptlookup: held lines match a tab's with a lone CR a break and CRLF one", "[alscriptlookup]")
    {
        const std::string           text = "one\rtwo\r\nthree\n\rfive";
        const ALScriptPlaces::Lines held(std::make_shared<const std::string>(text));
        const ALTextDocument        open(text);
        const ALScriptPlaces::Lines tab(&open);
        {
            INFO("as many as the tab's");
            CHECK(held.has(4));
            CHECK_FALSE(held.has(5));
            CHECK(tab.has(4));
            CHECK_FALSE(tab.has(5));
        }
        for (S32 line = 0; line < 5; ++line)
        {
            CAPTURE(line);
            REQUIRE(held.line(line) == tab.line(line));
        }
        {
            INFO("each as it is");
            CHECK(held.line(1) == "two");
            CHECK(held.line(3).empty());
            CHECK(held.line(4) == "five");
        }
        const ALScriptPlaces::Lines here(text);
        {
            INFO("read where it is, the same");
            CHECK(here.line(2) == "three");
            CHECK(here.has(4));
            CHECK_FALSE(here.has(5));
        }
    }
}
