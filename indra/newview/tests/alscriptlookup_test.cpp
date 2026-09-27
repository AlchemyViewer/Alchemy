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

#include "../alscriptstudiowords.h"
#include "alscriptstudio_fixture.h"
#include "llfocusmgr.h"

#include "../test/lltut.h"

#include <sstream>

// A script's id is the bridge's name for it, and the bridge is the viewer's.
std::string ALScriptRef::id() const
{
    return object.asString() + "_" + item.asString();
}

// What the preprocessor calls a script and a file, as it spells them; the
// preprocessor itself is the viewer's.
bool ALScriptPreprocessor::refOf(const std::string& path, ALScriptRef& ref)
{
    if (path.rfind("object:", 0) == 0)
    {
        const size_t colon = path.find(':', 7);
        if (colon == std::string::npos)
        {
            return false;
        }
        ref.object.set(path.substr(7, colon - 7));
        ref.item.set(path.substr(colon + 1));
        return ref.object.notNull() && ref.item.notNull();
    }
    if (path.rfind("inventory:", 0) == 0)
    {
        ref.object.setNull();
        ref.item.set(path.substr(10));
        return ref.item.notNull();
    }
    return false;
}

std::string ALScriptPreprocessor::pathOf(const ALScriptRef& ref)
{
    return ref.inInventory() ? "inventory:" + ref.item.asString() : "object:" + ref.object.asString() + ":" + ref.item.asString();
}

bool ALScriptPreprocessor::fileOf(const std::string& path, std::string& file)
{
    if (path.rfind("disk:", 0) != 0)
    {
        return false;
    }
    file = path.substr(5);
    return !file.empty();
}

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef ALScriptLookup::Found    Found;
    typedef std::vector<std::string> Names;

    // The window, faked: the scripts it offers, and each question it was
    // asked, held for the test to answer.
    struct FakeLookupWindow : public ALScriptLookup::Window
    {
        struct Load
        {
            ALScriptRef                                                ref;
            std::function<void(const LLUUID&, const std::string&)>     loaded;
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

        std::vector<ALScriptLookup::Candidate> candidates(const Doc& doc) override
        {
            asked.push_back(doc.id);
            return others;
        }
        void loadSource(const ALScriptRef& ref, std::function<void(const LLUUID&, const std::string&)> loaded) override
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
        bool sourceLine(const std::string& path, S32 line, std::string& out) const override
        {
            const auto held = lines.find(path + "#" + std::to_string(line));
            if (held == lines.end())
            {
                return false;
            }
            out = held->second;
            return true;
        }
        void holdPreview(Doc& doc) override { held.push_back(doc.id); }
        void showFound(Doc& doc, const Found& found) override
        {
            shownIn = doc.id;
            shown   = found;
            ++shows;
        }
        void askNewName(Doc& doc, std::function<std::string(const std::string&)> hint_of,
                        std::function<void(const std::string&)> chose) override
        {
            askedName = doc.id;
            hint      = std::move(hint_of);
            chosen    = std::move(chose);
        }
        Doc* openFileTab(const std::string& path, bool lua) override
        {
            filesOpened.push_back(path);
            return whenFileOpened ? whenFileOpened(path) : nullptr;
        }
        void activate(Doc& doc) override { activated.push_back(doc.id); }

        std::vector<ALScriptLookup::Candidate>             others;
        std::map<std::string, std::string>                 lines;
        std::vector<Load>                                  loads;
        std::vector<Expand>                                expands;
        std::vector<Ask>                                   asks;
        Names                                              asked, held, filesOpened, activated;
        std::string                                        shownIn, askedName;
        Found                                              shown;
        S32                                                shows = 0;
        std::function<std::string(const std::string&)>     hint;
        std::function<void(const std::string&)>            chosen;
        std::function<Doc*(const std::string&)>            whenFileOpened;
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

namespace tut
{
    struct alscriptlookup_data
    {
        al_studio_test::StudioWindow    window;
        al_studio_test::FakeServices    services;
        FakeLookupWindow                studio;
        std::unique_ptr<ALScriptLookup> unit;
        const LLUUID                    object{ "11111111-1111-1111-1111-111111111111" };
        const ALScriptRef               a{ object, LLUUID("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa") };
        const ALScriptRef               b{ object, LLUUID("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb") };
        const ALScriptRef               c{ object, LLUUID("cccccccc-cccc-cccc-cccc-cccccccccccc") };
        const ALScriptRef               d{ object, LLUUID("dddddddd-dddd-dddd-dddd-dddddddddddd") };
        const std::string               A_TEXT = "integer count;\ndefault { state_entry() { count = 1; } }\n";
        const std::string               B_TEXT = "#include \"A\"\nfoo() { count = 2; }\n";

        alscriptlookup_data()
        {
            ALScriptStudioWords::Sources& sources = ALScriptStudioWords::sources();
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
            ALScriptStudioWords::sources() = ALScriptStudioWords::Sources();
            ALScriptStudioWords::forget();
            gFocusMgr.setKeyboardFocus(nullptr);
        }
        ALScriptLookup& make()
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            unit = std::make_unique<ALScriptLookup>(services, studio);
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

    typedef test_group<alscriptlookup_data> alscriptlookup_group;
    typedef alscriptlookup_group::object    alscriptlookup_object;
    alscriptlookup_group                    alscriptlookup_instance("alscriptlookup");

    template<> template<>
    void alscriptlookup_object::test<1>()
    {
        set_test_name("places once each, the script's own first then each other's by name; the language's own words reserved");
        Doc::Lookup lookup;
        ALScriptLookup::addPlace(lookup, place("", 1, 2));
        ALScriptLookup::addPlace(lookup, place("", 1, 2, 7));
        ALScriptLookup::addPlace(lookup, place("x", 1, 2));
        ensure_equals("once a file and a start", lookup.places.size(), size_t(2));

        std::vector<Doc::Place> places = { place("object:z", 0, 0, 5, "Beta"),  place("", 3, 0, 5, "Zeta"),
                                           place("object:y", 2, 0, 5, "Alpha"), place("object:x", 0, 0, 5, "Beta"),
                                           place("", 1, 0),                     place("object:y", 1, 0, 5, "Alpha") };
        ALScriptLookup::sortPlaces(places);
        Names order;
        for (const Doc::Place& each : places)
        {
            order.push_back(each.file + ":" + std::to_string(each.span.line));
        }
        ensure_equals("own whatever it is called, then by name, file and place", joined(order),
                      joined(Names{ ":1", ":3", "object:y:1", "object:y:2", "object:x:0", "object:z:0" }));

        ensure("an LSL keyword", ALScriptLookup::reserved(false, "state") && !ALScriptLookup::reserved(true, "state"));
        ensure("a Luau keyword", ALScriptLookup::reserved(true, "local") && !ALScriptLookup::reserved(false, "local"));
        ensure("the preprocessor's, in LSL", ALScriptLookup::reserved(false, "switch") && !ALScriptLookup::reserved(true, "switch"));
        ensure("the definitions'", ALScriptLookup::reserved(false, "llSay") && ALScriptLookup::reserved(true, "ll.Say"));
        ensure("a name of the script's", !ALScriptLookup::reserved(false, "count") && !ALScriptLookup::reserved(true, "count"));
    }

    template<> template<>
    void alscriptlookup_object::test<2>()
    {
        set_test_name("a name declared nowhere shared looked up in the script alone, answered at once and shown, the preview held");
        make();
        studio.others = { { b, "B" } };
        Doc& doc      = lookUp(ALEditorCommand::FindReferences, false);
        ensure("the preview held", studio.held == Names{ "a" });
        ensure("nothing else asked", studio.asked.empty() && studio.loads.empty());
        ensure_equals("shown", studio.shows, 1);
        ensure_equals("in its tab", studio.shownIn, std::string("a"));
        ensure_equals("from", studio.shown.from, std::string("a"));
        ensure_equals("named", studio.shown.fromName, std::string("A"));
        ensure_equals("the name", studio.shown.name, std::string("count"));
        ensure("in order", studio.shown.places.size() == 2 && studio.shown.places[0].span.line == 0);
        ensure("not declared", !studio.shown.hasDefinition);
        ensure("said", !services.statuses.empty() && has(services.statuses.back(), "ReferencesFound [COUNT]=2"));
        ensure("done with", doc.lookup.command == ALEditorCommand::None && doc.lookup.pending == 0);
    }

    template<> template<>
    void alscriptlookup_object::test<3>()
    {
        set_test_name("the object's others: one open without the name passed over, one read, expanded, asked, and its place added");
        make();
        tab("c", c, "default { touch_start(integer n) { } }\n", "C");
        studio.others = { { c, "C" }, { b, "B" } };
        Doc& doc      = lookUp(ALEditorCommand::FindReferences);
        ensure("candidates asked for", studio.asked == Names{ "a" });
        ensure_equals("one to read", studio.loads.size(), size_t(1));
        ensure("the open one passed over", studio.expands.empty());
        ensure("waiting said", has(services.statuses.back(), "LookingAcross [COUNT]=1 [NAME]=count"));
        ensure_equals("nothing shown yet", studio.shows, 0);

        const LLUUID asset("eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee");
        studio.loads[0].loaded(asset, B_TEXT);
        ensure_equals("expanded", studio.expands.size(), size_t(1));
        const ALScriptPreprocessor::Request& request = studio.expands[0].request;
        ensure("as the compiler sees it, not optimized",
               request.ref == b && request.name == "B" && request.assetId == asset && request.source == B_TEXT && !request.optimize);

        studio.expands[0].expanded(
            expansion("B", ALScriptPreprocessor::pathOf(b), B_TEXT, "A", ALScriptPreprocessor::pathOf(a), "integer count;\n"));
        ensure_equals("asked", studio.asks.size(), size_t(1));
        const ALScriptAnalysis::Request& asked = studio.asks[0].request;
        ensure("where the declaration went", asked.kind == ALScriptAnalysis::Kind::References && asked.line == 0 && asked.column == 8);
        ensure_equals("by who asked and the script", asked.id, "lookup:" + doc.id + ":" + b.id());
        ensure_equals("of this lookup", asked.version, doc.lookup.generation);

        studio.asks[0].answered(answer({ span(0, 8, 5), span(1, 8, 5) }));
        ensure_equals("shown", studio.shows, 1);
        ensure_equals("its own through the other's expansion left out", studio.shown.places.size(), size_t(3));
        const Doc::Place& other = studio.shown.places[2];
        ensure_equals("in B", other.file, ALScriptPreprocessor::pathOf(b));
        ensure_equals("named", other.fileName, std::string("B"));
        ensure_equals("as written", other.text, std::string("foo() { count = 2; }"));
        ensure_equals("the name in it", other.at, 8);
        ensure("declared", studio.shown.hasDefinition);
        ensure("across scripts", has(services.statuses.back(), "ReferencesFoundAcross [COUNT]=3 [FILES]=2"));
    }

    template<> template<>
    void alscriptlookup_object::test<4>()
    {
        set_test_name("an open script with the name answered on the spot, the others still counted; an include's line where it is had");
        make();
        Doc& open     = tab("b", b, B_TEXT, "B");
        studio.others = { { b, "B" }, { c, "C" } };
        const std::string inc = "disk:/scripts/inc.lsl";
        studio.lines[inc + "#0"] = "   integer count; // kept";
        Doc& doc = services.findDoc("a") ? *services.findDoc("a") : tab("a", a, A_TEXT, "A");
        unit->start(doc, ALEditorCommand::FindReferences, refsOf("count"), true, inc, span(0, 8, 5), {}, doc.editor->document().version());
        ensure("the open one expanded at once", studio.expands.size() == 1 && studio.expands[0].request.source == open.editor->text());
        ensure_equals("the other read", studio.loads.size(), size_t(1));
        ensure("both waited for", doc.lookup.pending == 2 && studio.shows == 0);

        studio.expands[0].expanded(expansion("B", ALScriptPreprocessor::pathOf(b), B_TEXT, "inc.lsl", inc, "integer count;\n"));
        studio.asks[0].answered(answer({ span(0, 8, 5) }));
        ensure("still one to come", studio.shows == 0 && doc.lookup.pending == 1);
        studio.loads[0].loaded(LLUUID::null, "");
        ensure_equals("an empty one passed over", studio.shows, 1);
        ensure_equals("one place", studio.shown.places.size(), size_t(1));
        const Doc::Place& found = studio.shown.places[0];
        ensure("in the include", found.file == inc && found.fileName == "inc.lsl");
        ensure("as the include reads", found.text == "integer count; // kept" && found.at == 5);

        studio.lines.clear();
        unit->start(doc, ALEditorCommand::FindReferences, refsOf("count"), true, inc, span(0, 8, 5), {}, doc.editor->document().version());
        studio.expands.back().expanded(expansion("B", ALScriptPreprocessor::pathOf(b), B_TEXT, "inc.lsl", inc, "integer count;\n"));
        studio.asks.back().answered(answer({ span(0, 8, 5) }));
        studio.loads.back().loaded(LLUUID::null, "default { }\n");
        ensure_equals("shown again", studio.shows, 2);
        ensure("the expansion's line, the name not placed in it",
               studio.shown.places.size() == 1 && studio.shown.places[0].text == "integer count;" && studio.shown.places[0].at == -1);
    }

    template<> template<>
    void alscriptlookup_object::test<5>()
    {
        set_test_name("answers to an earlier lookup, or after the lookups are gone, dropped; a script not naming the home passed over");
        make();
        studio.others = { { b, "B" } };
        Doc& doc      = lookUp(ALEditorCommand::FindReferences);
        lookUp(ALEditorCommand::FindReferences, false);
        ensure_equals("the second shown", studio.shows, 1);
        studio.loads[0].loaded(LLUUID::null, B_TEXT);
        ensure("the first's answer dropped", studio.expands.empty() && studio.shows == 1);

        lookUp(ALEditorCommand::FindReferences);
        studio.loads.back().loaded(LLUUID::null, B_TEXT);
        studio.expands.back().expanded(expansion("B", ALScriptPreprocessor::pathOf(b), B_TEXT));
        ensure("no declaration in it: nothing asked", studio.asks.empty());
        ensure_equals("shown without it", studio.shows, 2);

        const U32 generation = doc.lookup.generation;
        lookUp(ALEditorCommand::FindReferences);
        ensure("a new generation", doc.lookup.generation != generation);
        studio.loads.back().loaded(LLUUID::null, B_TEXT);
        // A lookup of another tab's, still waiting on its read, as this
        // one waits on its expansion: both current, and both gone with it.
        Doc& other = tab("x", c, A_TEXT, "X");
        unit->start(other, ALEditorCommand::FindReferences, refsOf("count"), true, std::string(), span(0, 8, 5), {},
                    other.editor->document().version());
        const size_t expands = studio.expands.size();
        unit.reset();
        studio.expands.back().expanded(
            expansion("B", ALScriptPreprocessor::pathOf(b), B_TEXT, "A", ALScriptPreprocessor::pathOf(a), "integer count;\n"));
        ensure("nothing asked once gone", studio.asks.empty() && studio.shows == 2);
        studio.loads.back().loaded(LLUUID::null, B_TEXT);
        ensure("nothing read once gone", studio.expands.size() == expands);
    }

    template<> template<>
    void alscriptlookup_object::test<6>()
    {
        set_test_name("Rename: one that may not be said so; else the new name asked, the row under it saying what return will do");
        make();
        lookUp(ALEditorCommand::Rename, false, false);
        ensure("not renamable", has(services.statuses.back(), "NotRenamable") && studio.askedName.empty());

        Doc& doc = lookUp(ALEditorCommand::Rename, false);
        ensure_equals("asked in its tab", studio.askedName, std::string("a"));
        ALScriptOutlineEntry entry;
        entry.name = "total";
        doc.outline.push_back(entry);
        ensure("nothing typed", has(studio.hint(""), "RenameHint [COUNT]=2 [FILES]=1 [NAME]=count"));
        ensure("no name", has(studio.hint("9lives"), "RenameBadName") && has(studio.hint("9lives"), "[NAME]=9lives"));
        ensure("reserved", has(studio.hint("state"), "RenameReserved [COUNT]=2 [FILES]=1 [NAME]=state"));
        ensure("the same", has(studio.hint(" count "), "RenameSame"));
        ensure("a clash said, allowed", has(studio.hint("total"), "RenameClash"));
        ensure("what it will do", has(studio.hint("sum"), "RenameTo [COUNT]=2") && has(studio.hint("sum"), "[NEW]=sum"));
        services.docs.clear();
        ensure("its tab gone: nothing said", studio.hint("sum").empty());
    }

    template<> template<>
    void alscriptlookup_object::test<7>()
    {
        set_test_name("a rename across scripts: here as one step, open ones unchanged since, others opened with it waiting, the rest left");
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
        ensure("asked", (bool)studio.chosen);
        ensure("across scripts", has(studio.hint("sum"), "RenameToAcross [COUNT]=9 [FILES]=6"));

        studio.chosen("9lives");
        ensure("no name", has(services.statuses.back(), "RenameBadName") && doc.editor->text() == A_TEXT);
        studio.chosen("state");
        ensure("reserved", has(services.statuses.back(), "RenameReserved"));
        studio.chosen("count");
        ensure("the same: nothing", services.reports.empty() && doc.editor->text() == A_TEXT);

        studio.chosen(" total ");
        ensure_equals("here", doc.editor->text(), std::string("integer total;\ndefault { state_entry() { total = 1; } }\n"));
        ensure_equals("one step", doc.editor->undoJournal().undoLabel(), std::string("rename"));
        ensure_equals("open and unchanged: where it still stands", open.editor->text(),
                      std::string("foo() { total = 2; }\nbar() { total = total; }\n"));
        ensure_equals("there as one step too", open.editor->undoJournal().undoLabel(), std::string("rename"));
        ensure("moved on since: left", has(moved.editor->text(), "count") && !has(moved.editor->text(), "total"));
        ensure("not open: opened", services.opened.size() == 1 && services.opened[0].ref == d && services.opened[0].name == "D");
        Doc* waiting = services.findDoc("d");
        ensure("the change waiting", waiting && waiting->pendingEdits.size() == 1 && waiting->pendingEdits[0].now == "total");
        ensure("a file opened", studio.filesOpened == Names{ "/e.lsl" });
        ensure_equals("and changed", services.findDoc("disk:/e.lsl")->editor->text(), std::string("integer total = 4;\n"));
        ensure_equals("said once", services.reports.size(), size_t(1));
        const al_studio_test::FakeServices::Said& said = services.reports[0];
        ensure("the counts", has(said.text, "RenamedIn [NAME]=total [PLACES]=Places [COUNT]=6 [SCRIPTS]=Scripts [COUNT]=4"));
        ensure("opened", has(said.text, "; RenamedOpened [NAME]=total [PLACES]=Places [COUNT]=6 [SCRIPTS]=Scripts [COUNT]=2"));
        ensure("left", has(said.text, "; RenamedLeft [NAME]=total [PLACES]=Places [COUNT]=6 [SCRIPTS]=Scripts [COUNT]=2."));
        ensure("a failure where any was left", said.failure);
        ensure("its tab brought forward", studio.activated == Names{ "a" });
    }

    template<> template<>
    void alscriptlookup_object::test<8>()
    {
        set_test_name("a rename of a script that has moved on since the lookup refused, and one in only this script said as here");
        make();
        Doc& doc = lookUp(ALEditorCommand::Rename, false);
        doc.editor->insertText(" ");
        studio.chosen("total");
        ensure("stale", has(services.statuses.back(), "RenameStale [NAME]=total") && services.reports.empty());

        lookUp(ALEditorCommand::Rename, false);
        studio.chosen("total");
        ensure_equals("said", services.reports.size(), size_t(1));
        ensure("here", has(services.reports[0].text, "RenamedHere") && !has(services.reports[0].text, ";"));
        ensure("not a failure", !services.reports[0].failure);

        Doc& open = tab("b", b, "foo() { count = 2; }\n", "B");
        doc.editor->setText(A_TEXT);
        unit->start(doc, ALEditorCommand::Rename, refsOf("count"), false, std::string(), span(0, 8, 5),
                    { place("", 0, 8), place(ALScriptPreprocessor::pathOf(b), 0, 8, 5, "B") }, doc.editor->document().version());
        studio.chosen("sum");
        ensure("in both, none opened", has(services.reports.back().text, "RenamedIn") && !has(services.reports.back().text, ";"));
        ensure_equals("B too", open.editor->text(), std::string("foo() { sum = 2; }\n"));
    }

    template<> template<>
    void alscriptlookup_object::test<9>()
    {
        set_test_name("edits waiting for a text made where the old name stands, the rest said missed; none where nothing may change");
        make();
        Doc& doc   = tab("d", d, "integer count;\nx = count;\n", "D");
        doc.loaded = false;
        doc.pendingEdits = { { span(0, 8, 5), "count", "total" }, { span(1, 0, 5), "count", "total" } };
        unit->applyPendingEdits(doc);
        ensure("not loaded: kept", doc.pendingEdits.size() == 2 && doc.editor->text() == "integer count;\nx = count;\n");

        doc.loaded = true;
        doc.editor->setReadOnly(true);
        unit->applyPendingEdits(doc);
        ensure("made", doc.pendingEdits.empty() && doc.editor->text() == "integer total;\nx = count;\n");
        ensure_equals("one step", doc.editor->undoJournal().undoLabel(), std::string("rename"));
        ensure("the one missed said", services.reports.size() == 1 && has(services.reports[0].text, "RenameMissed [COUNT]=1 [NAME]=D"));
        ensure("of the tab, a failure", services.reports[0].doc == "d" && services.reports[0].failure);

        Doc::PendingEdit replace{ span(0, 8, 5), "total", "sum", true };
        doc.pendingEdits = { replace };
        unit->applyPendingEdits(doc);
        ensure("a replace", doc.editor->text() == "integer sum;\nx = count;\n" && doc.editor->undoJournal().undoLabel() == "replace");
        ensure("nothing missed", services.reports.size() == 1);

        doc.modifiable   = false;
        doc.pendingEdits = { { span(0, 8, 3), "sum", "total", true } };
        unit->applyPendingEdits(doc);
        ensure("none made", doc.editor->text() == "integer sum;\nx = count;\n");
        ensure("every one missed", services.reports.size() == 2 && has(services.reports[1].text, "ReplaceMissed [COUNT]=1"));
    }
}
