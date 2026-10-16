/**
 * @file alscriptsearchpane_test.cpp
 * @brief Script Studio's Search tab, over the studio's own window: places found in open and fetched scripts, searched again as they are typed in, and replaced.
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

#include "../alscriptsearchpane.h"

#include "alpanelist.h"
#include "alscopebar.h"
#include "llfontgl.h"
#include "lllineeditor.h"
#include "lltextbox.h"

#include "alscriptstudio_fixture.h"
#include "lltabcontainer.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    typedef ALScriptSearchPane::Doc Doc;

    // The window's side, answered as a test says, and what was asked of it
    // kept.
    class FakeWindow : public ALScriptSearchPane::Window
    {
    public:
        struct Fetch
        {
            ALScriptRef ref;
            U32         generation = 0;
            std::string where;
        };
        struct Chosen
        {
            std::string doc;
            ALTextRange place;
            bool        toEditor = false;
        };

        // The objects as the test lists them: the one in hand alone where
        // asked for; told on the spot, or held until the test says.
        void listObjects(const LLUUID& only, std::function<void(std::vector<Object>)> told) override
        {
            std::vector<Object> out;
            for (const Object& one : objects)
            {
                if (only.isNull() || one.root == only)
                {
                    out.push_back(one);
                }
            }
            if (holdListing)
            {
                heldListing = [told = std::move(told), out]() { told(out); };
                return;
            }
            told(std::move(out));
        }
        std::string         objectName(const LLUUID& root) const override
        {
            for (const Object& one : objects)
            {
                if (one.root == root)
                {
                    return one.name;
                }
            }
            return std::string();
        }
        std::string whereIs(const Doc&) const override { return std::string(); }
        LLUUID      objectInHand() const override { return inHand; }
        // The tests' scripts are each in a prim of its own, its own root.
        LLUUID rootOf(const ALScriptRef& ref) const override { return ref.object; }
        Doc*   openElsewhere(const ALScriptRef& ref) override { return elsewhere && elsewhere->ref == ref ? elsewhere : nullptr; }
        std::vector<InventoryItem> inventoryItems() override { return inventory; }
        std::vector<InventoryItem> inventory;
        void   fetchForSearch(const ALScriptRef& ref, U32 generation, const std::string& where) override { fetches.push_back({ ref, generation, where }); }
        // Sought here and now, as the thread would, but for when.
        void matchApart(std::shared_ptr<const std::string> text, const std::string& query, const ALTextSearchOptions& options,
                        std::function<void(ALScriptSearch::Matched)> matched) override
        {
            ++apart;
            matched(ALScriptSearch::match(ALTextDocument(*text), query, options));
        }
        S32 apart = 0;
        void   applyPendingEdits(Doc& doc) override { applied.push_back(doc.id); }
        void   confirmReplaceAll(const LLSD& args, std::function<void()> yes) override
        {
            asked.push_back(args);
            yes();
        }
        void searchResultChosen(const ALScriptSearch::Found& one, const ALTextRange& place, bool to_editor) override
        {
            chosen.push_back({ one.doc, place, to_editor });
        }
        // What each script, by its name, includes; and who was asked about.
        std::vector<Included> includesOf(const ALScriptRef&, const std::string&, const std::string& name, const std::string&, bool) override
        {
            askedIncludes.push_back(name);
            const auto it = includes.find(name);
            return it == includes.end() ? std::vector<Included>() : it->second;
        }

        std::vector<Object>      objects;
        bool                     holdListing = false;
        std::function<void()>    heldListing;
        LLUUID                   inHand;
        Doc*                     elsewhere = nullptr;
        std::vector<Fetch>       fetches;
        std::vector<std::string> applied;
        std::vector<LLSD>        asked;
        std::vector<Chosen>      chosen;
        std::map<std::string, std::vector<Included>> includes;
        std::vector<std::string>                     askedIncludes;
    };

    LLUUID fresh()
    {
        LLUUID id;
        id.generate();
        return id;
    }
}

namespace
{
    struct alscriptsearchpane_data
    {
        // The window the pane finds through the view tree: the fakes.
        al_studio_test::StudioWindowOf<FakeWindow> window;
        al_studio_test::FakeServices&             services = window.services();
        FakeWindow&                               studio   = window.pane();
        ALScriptSearchPane*          pane = nullptr;

        ALScriptSearchPane& make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            // The tab as the skin built it.
            pane = window.find<ALScriptSearchPane>("search_tab");
            UNSCOPED_INFO("the skin builds the tab as the pane");
            REQUIRE(pane != nullptr);
            // In sight, its tab the one chosen, as when it is worked in.
            LLTabContainer* bottom = window.find<LLTabContainer>("bottom_tabs");
            REQUIRE(bottom != nullptr);
            bottom->selectTabByName("search_tab");
            return *pane;
        }

        // A tab over an editor of its own, holding `text`, of a script in a
        // prim of its own: one of this window's, or of another's.
        std::vector<std::unique_ptr<Doc>> other;
        Doc& doc(const std::string& id, const std::string& text, bool notecard = false, bool elsewhere = false)
        {
            Doc* made = nullptr;
            if (elsewhere)
            {
                other.push_back(std::make_unique<Doc>());
                made       = other.back().get();
                made->id   = id;
                made->ref  = ALScriptRef(fresh(), fresh());
                made->name = id + ".lsl";
            }
            else
            {
                made = &services.addDoc(id, ALScriptRef(fresh(), fresh()), id + ".lsl");
            }
            Doc& d       = *made;
            d.loaded     = true;
            d.modifiable = true;
            d.notecard   = notecard;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name   = "editor_" + id;
            p.rect   = LLRect(0, 200, 400, 0);
            p.syntax = "lsl";
            d.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            d.editor->setFont(LLFontGL::getFontMonospace());
            window.floater->addChild(d.editor);
            d.editor->setText(text);
            return d;
        }

        void find(const std::string& words, const std::string& scope = "open")
        {
            pane->bar()->setValue("query", words);
            pane->bar()->setValue("scope", scope);
            pane->run();
        }
        std::string counted() const { return pane->count()->getText(); }
    };

    TEST_CASE_METHOD(alscriptsearchpane_data, "ALScriptSearchPane: open scripts are searched as they stand and an object's are fetched", "[ALScriptSearchPane]")
    {
        ALScriptSearchPane& out  = make();
        Doc&                door = doc("door", "timer one\nnothing\n  timer two\n");
        find("timer");
        UNSCOPED_INFO("one script");
        REQUIRE(out.search().found().size() == size_t(1));
        UNSCOPED_INFO("two places");
        REQUIRE(out.search().found()[0].places.size() == size_t(2));
        UNSCOPED_INFO("a row each");
        CHECK(out.list()->getItemCount() == 2);
        // The list's keys: return goes to the place chosen, to type there,
        // escape back to the script; and the rows copy.
        out.list()->selectFirstItem();
        {
            INFO("return");
            CHECK(out.list()->handleKeyHere(KEY_RETURN, MASK_NONE));
            REQUIRE_FALSE(studio.chosen.empty());
            CHECK(studio.chosen.back().toEditor);
        }
        {
            INFO("escape");
            CHECK(out.list()->handleKeyHere(KEY_ESCAPE, MASK_NONE));
            REQUIRE_FALSE(services.reveals.empty());
            CHECK(services.reveals.back());
        }
        UNSCOPED_INFO("copied");
        CHECK(out.list()->handleKeyHere('C', MASK_CONTROL));
        REQUIRE(out.search().found()[0].lines.size() == size_t(2));
        UNSCOPED_INFO("the line as listed, trimmed");
        CHECK(out.search().found()[0].lines[1] == std::string("timer two"));
        CHECK(counted() == services.words("SearchCount", { { "[HITS]", services.counted("Matches", 2) },
                                                                            { "[FILES]", services.counted("Files", 1) },
                                                                            { "[SHOWN]", "2000" } }));

        // An object's: the open one as it stands, another open elsewhere as
        // it stands there, the rest fetched.
        Doc&              there = doc("there", "timer elsewhere\n", false, true);
        const ALScriptRef away(fresh(), fresh());
        const LLUUID      root = fresh();
        studio.elsewhere       = &there;
        studio.objects = { { root, "Thing", { door.ref, there.ref, away } } };
        studio.inHand  = root;
        find("timer", "object");
        UNSCOPED_INFO("the open one and the one elsewhere");
        REQUIRE(out.search().found().size() == size_t(2));
        UNSCOPED_INFO("where it is elsewhere, said");
        CHECK(out.search().found()[1].where == std::string("Thing: there.lsl"));
        UNSCOPED_INFO("the rest fetched");
        REQUIRE(studio.fetches.size() == size_t(1));
        UNSCOPED_INFO("the one asked");
        CHECK(studio.fetches[0].ref == away);
        UNSCOPED_INFO("counting while it comes");
        CHECK(counted() == services.words("SearchCounting", { { "[HITS]", services.counted("Matches", 3) },
                                                                                              { "[FILES]", services.counted("Files", 2) },
                                                                                              { "[SHOWN]", "2000" } }));
        out.fetched(studio.fetches[0].generation, "Thing", away, "away.lsl", std::make_shared<const std::string>("x\ntimer fetched\n"), false);
        UNSCOPED_INFO("fetched, found");
        REQUIRE(out.search().found().size() == size_t(3));
        UNSCOPED_INFO("its text kept, for a replace");
        CHECK(out.search().found()[2].text == std::string("x\ntimer fetched\n"));
        UNSCOPED_INFO("none to wait for");
        CHECK(out.search().pending() == 0);

        // No object in hand: none guessed at.
        studio.inHand.setNull();
        find("timer", "object");
        CHECK(counted() == services.words("SearchNoObject"));
    }

    TEST_CASE_METHOD(alscriptsearchpane_data, "ALScriptSearchPane: a later search drops the answers to an earlier one", "[ALScriptSearchPane]")
    {
        ALScriptSearchPane& out = make();
        const ALScriptRef   away(fresh(), fresh());
        const LLUUID        root = fresh();
        studio.objects           = { { root, "Thing", { away } } };
        find("timer", "listed");
        find("other", "listed");
        UNSCOPED_INFO("asked twice");
        REQUIRE(studio.fetches.size() == size_t(2));
        out.fetched(studio.fetches[0].generation, "Thing", away, "away.lsl", std::make_shared<const std::string>("timer\nother\n"), false);
        UNSCOPED_INFO("the first search's answer dropped");
        CHECK(out.search().found().empty());
        UNSCOPED_INFO("still waiting on the second's");
        CHECK(out.search().pending() == 1);
        out.fetched(studio.fetches[1].generation, "Thing", away, "away.lsl", std::make_shared<const std::string>("timer\nother\n"), false);
        UNSCOPED_INFO("the second's kept");
        REQUIRE(out.search().found().size() == size_t(1));
        REQUIRE_FALSE(out.search().found()[0].places.empty());
        UNSCOPED_INFO("for what it looked for");
        CHECK(out.search().found()[0].places[0].begin.line == 1);
    }

    TEST_CASE_METHOD(alscriptsearchpane_data, "ALScriptSearchPane: a script typed in since the search is left alone and searched again", "[ALScriptSearchPane]")
    {
        ALScriptSearchPane& out  = make();
        Doc&                door = doc("door", "timer one\ntimer two\n");
        find("timer");
        door.editor->setCaret(ALTextPos(0, 0));
        door.editor->insertText("x");
        out.replaceAll();
        UNSCOPED_INFO("left alone");
        CHECK(door.editor->text() == std::string("xtimer one\ntimer two\n"));
        {
            INFO("said so");
            REQUIRE_FALSE(services.reports.empty());
            CHECK(services.reports.back().failure);
        }

        // Searched again once the typing has rested: its places are where
        // they are now.
        find("timer");
        door.editor->setCaret(ALTextPos(0, 0));
        door.editor->insertText("yy");
        out.typedIn(door);
        out.pump();
        // A script searched again keeps its place in found(), whether it
        // holds places now or none: found() sized once, its places after
        // each pump.
        REQUIRE(out.search().found().size() == size_t(1));
        REQUIRE_FALSE(out.search().found()[0].places.empty());
        UNSCOPED_INFO("not at once");
        CHECK(out.search().found()[0].places[0].begin.column == 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        LLTabContainer* tabs = window.find<LLTabContainer>("bottom_tabs");
        REQUIRE(tabs != nullptr);
        tabs->selectTabByName("problems_tab");
        out.pump();
        REQUIRE_FALSE(out.search().found()[0].places.empty());
        UNSCOPED_INFO("due, but out of sight: waiting");
        CHECK(out.search().found()[0].places[0].begin.column == 1);
        tabs->selectTabByName("search_tab");
        out.pump();
        REQUIRE_FALSE(out.search().found()[0].places.empty());
        UNSCOPED_INFO("then, seen, where it is now");
        CHECK(out.search().found()[0].places[0].begin.column == 3);
        out.replaceAll();
        UNSCOPED_INFO("and replaced");
        CHECK(door.editor->text() == std::string("yyx one\n two\n"));
        door.editor->undo();
        UNSCOPED_INFO("as one step");
        CHECK(door.editor->text() == std::string("yyxtimer one\ntimer two\n"));
    }

    TEST_CASE_METHOD(alscriptsearchpane_data, "ALScriptSearchPane: Replace All asks then edits open scripts and opens the rest unsaved", "[ALScriptSearchPane]")
    {
        ALScriptSearchPane& out  = make();
        Doc&                door = doc("door", "llSay(0, \"timer\");\n");
        const ALScriptRef   away(fresh(), fresh());
        const ALScriptRef   card(fresh(), fresh());
        const LLUUID        root = door.ref.object;
        studio.objects           = { { root, "Thing", { door.ref, away, card } } };
        studio.inHand            = root;
        find("timer", "object");
        REQUIRE(studio.fetches.size() == size_t(2));
        out.fetched(studio.fetches[0].generation, "Thing", away, "away.lsl", std::make_shared<const std::string>("timer\n"), false);
        out.fetched(studio.fetches[1].generation, "Thing", card, "card", std::make_shared<const std::string>("a timer\n"), true);
        UNSCOPED_INFO("three found");
        CHECK(out.search().found().size() == size_t(3));
        services.whenOpened = [this, card](const ALScriptRef& ref, const std::string&) {
            Doc& opened = ref == card ? doc("card", "a timer\n", true) : doc("away", "timer\n");
            opened.ref  = ref;
        };

        window.find<LLLineEditor>("search_replacement")->setText(std::string("clock"));
        out.askReplaceAll();
        UNSCOPED_INFO("asked");
        REQUIRE(studio.asked.size() == size_t(1));
        UNSCOPED_INFO("of the scripts and the notecard");
        CHECK(studio.asked[0]["SCRIPTS"].asString() == services.counted("ScriptsNotecards", 3));
        UNSCOPED_INFO("the open one, replaced");
        CHECK(door.editor->text() == std::string("llSay(0, \"clock\");\n"));
        UNSCOPED_INFO("the two not open, opened");
        REQUIRE(services.opened.size() == size_t(2));
        {
            INFO("by their items");
            CHECK(services.opened[0].ref == away);
            CHECK(services.opened[1].ref == card);
        }
        const Doc* opened = services.findDoc(away);
        UNSCOPED_INFO("a tab for it");
        REQUIRE(opened != nullptr);
        UNSCOPED_INFO("its change waiting on its text");
        REQUIRE(opened->pendingEdits.size() == size_t(1));
        UNSCOPED_INFO("which it replaces");
        CHECK(opened->pendingEdits[0].now == std::string("clock"));
        const Doc* notecard = services.findDoc(card);
        {
            INFO("the notecard's too");
            REQUIRE(notecard);
            REQUIRE(notecard->pendingEdits.size() == 1);
            CHECK(notecard->pendingEdits[0].now == "clock");
        }
        UNSCOPED_INFO("and each is made");
        CHECK(studio.applied.size() == size_t(2));
        REQUIRE_FALSE(services.reports.empty());
        CHECK(services.reports.back().text ==
                      services.words("SearchReplaced", { { "[PLACES]", services.counted("Places", 3) }, { "[SCRIPTS]", services.counted("ScriptsNotecards", 3) } }) +
                          "; " +
                          services.words("SearchReplacedOpened",
                                         { { "[PLACES]", services.counted("Places", 3) }, { "[SCRIPTS]", services.counted("ScriptsNotecards", 2) } }) +
                          ".");
    }

    TEST_CASE_METHOD(alscriptsearchpane_data, "ALScriptSearchPane: Replace All's step for a found script follows where it is now", "[ALScriptSearchPane]")
    {
        using Step = ALScriptSearch::Step;
        using At   = ALScriptSearch::Now::At;
        ALScriptSearch::Found one;
        one.ref     = ALScriptRef(fresh(), fresh());
        one.version = 7;
        one.places  = { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)) };
        ALTextDocument text("timer\n");
        ALScriptSearch::Now now;

        UNSCOPED_INFO("closed, with no text kept: left");
        CHECK(ALScriptSearch::step(one, now) == Step::Leave);
        one.text = "timer\n";
        UNSCOPED_INFO("closed, with its text: opened");
        CHECK(ALScriptSearch::step(one, now) == Step::Open);
        ALScriptSearch::Found file = one;
        file.ref                   = ALScriptRef();
        UNSCOPED_INFO("a file closed since: left");
        CHECK(ALScriptSearch::step(file, now) == Step::Leave);

        now.at         = At::Here;
        now.loaded     = true;
        now.modifiable = true;
        now.text       = &text;
        UNSCOPED_INFO("here, at another version: left");
        CHECK(ALScriptSearch::step(one, now) == Step::Leave);
        one.version = text.version();
        UNSCOPED_INFO("here, as searched: replaced");
        CHECK(ALScriptSearch::step(one, now) == Step::Replace);
        now.modifiable = false;
        UNSCOPED_INFO("here, not to be changed: left");
        CHECK(ALScriptSearch::step(one, now) == Step::Leave);
        now.modifiable = true;
        now.plainText  = true;
        UNSCOPED_INFO("here, a text file: skipped");
        CHECK(ALScriptSearch::step(one, now) == Step::Skip);
        now.plainText = false;

        now.at = At::Elsewhere;
        UNSCOPED_INFO("elsewhere, reading as it did: replaced");
        CHECK(ALScriptSearch::step(one, now) == Step::Replace);
        ALTextDocument typed("timer!\n");
        now.text = &typed;
        UNSCOPED_INFO("elsewhere, typed in since: left");
        CHECK(ALScriptSearch::step(one, now) == Step::Leave);

        one.notecard = true;
        now.text     = &text;
        UNSCOPED_INFO("found in a notecard in the world: replaced as a script is");
        CHECK(ALScriptSearch::step(one, now) == Step::Replace);
        ALScriptSearch::Found text_file = one;
        text_file.ref                   = ALScriptRef();
        UNSCOPED_INFO("found in a text file: skipped");
        CHECK(ALScriptSearch::step(text_file, now) == Step::Skip);
        one.notecard = false;
        one.places.clear();
        UNSCOPED_INFO("nothing found: skipped");
        CHECK(ALScriptSearch::step(one, now) == Step::Skip);
    }

    TEST_CASE_METHOD(alscriptsearchpane_data, "ALScriptSearchPane: included files are searched once each on request and left by Replace All", "[ALScriptSearchPane]")
    {
        ALScriptSearchPane& out = make();
        doc("door", "#include \"lib\"\ntimer\n");
        doc("gate", "#include \"lib\"\n");
        const std::vector<FakeWindow::Included> lib{ { "disk:/scripts/lib.lsl", "lib.lsl", "timer in lib\n" } };
        studio.includes["door.lsl"] = lib;
        studio.includes["gate.lsl"] = lib;
        find("timer");
        UNSCOPED_INFO("alone: the script's own");
        CHECK(out.search().found().size() == size_t(1));
        UNSCOPED_INFO("nothing asked of what it includes");
        CHECK(studio.askedIncludes.empty());
        out.bar()->setValue("includes", "yes");
        const S32 before = studio.apart;
        find("timer");
        UNSCOPED_INFO("and the file it includes, once for the two");
        REQUIRE(out.search().found().size() == size_t(2));
        {
            INFO("sought apart, once, and back");
            CHECK(studio.apart == before + 1);
            CHECK(out.search().pending() == 0);
        }
        const ALScriptSearch::Found& found = out.search().found()[1];
        UNSCOPED_INFO("by its identity");
        CHECK(found.file == std::string("disk:/scripts/lib.lsl"));
        {
            INFO("said to be included");
            CHECK(found.where.find(services.words("SearchIncluded")) != std::string::npos);
            CHECK(found.where.find("lib.lsl") != std::string::npos);
        }
        UNSCOPED_INFO("a row for its place");
        CHECK(out.list()->getItemCount() == 2);
        UNSCOPED_INFO("Replace All leaves it");
        CHECK(ALScriptSearch::step(found, ALScriptSearch::Now()) == ALScriptSearch::Step::Leave);
    }

    TEST_CASE_METHOD(alscriptsearchpane_data, "ALScriptSearchPane: an object is searched once listed and the count says what was passed over", "[ALScriptSearchPane]")
    {
        ALScriptSearchPane& out = make();
        const ALScriptRef   one(fresh(), fresh());
        const ALScriptRef   two(fresh(), fresh());
        const LLUUID        root = fresh();
        ALScriptSearchPane::Window::Object thing;
        thing.root          = root;
        thing.name          = "Thing";
        thing.items         = { one, two };
        thing.unlisted      = 3;
        studio.objects      = { thing };
        studio.inHand       = root;
        studio.holdListing  = true;
        find("timer", "object");
        UNSCOPED_INFO("nothing fetched while its prims are asked");
        CHECK(studio.fetches.empty());
        UNSCOPED_INFO("waited for");
        CHECK(out.search().pending() == 1);
        studio.heldListing();
        UNSCOPED_INFO("both fetched");
        REQUIRE(studio.fetches.size() == size_t(2));
        out.fetched(studio.fetches[0].generation, "Thing", one, "one.lsl", std::make_shared<const std::string>("timer\n"), false);
        out.fetched(studio.fetches[1].generation, "Thing", two, "two.lsl", nullptr, false);
        UNSCOPED_INFO("none to wait for");
        CHECK(out.search().pending() == 0);
        UNSCOPED_INFO("one read");
        CHECK(out.search().found().size() == size_t(1));
        const std::string said = services.words("SearchCount", { { "[HITS]", services.counted("Matches", 1) },
                                                                 { "[FILES]", services.counted("Files", 1) },
                                                                 { "[SHOWN]", "2000" } });
        const std::string missed = services.words("SearchBoth", { { "[FIRST]", services.counted("SearchUnlisted", 3) },
                                                                  { "[SECOND]", services.counted("SearchUnread", 1) } });
        UNSCOPED_INFO("what was passed over, after what was found");
        CHECK(counted() ==
                      services.words("SearchPassedOver", { { "[SAID]", said }, { "[MISSED]", missed } }));

        // A search begun again lets the held listing go: its answer is an
        // earlier search's.
        studio.fetches.clear();
        find("timer", "object");
        std::function<void()> first = std::move(studio.heldListing);
        find("other", "object");
        first();
        UNSCOPED_INFO("the earlier listing dropped");
        CHECK(studio.fetches.empty());
        studio.heldListing();
        UNSCOPED_INFO("the later one searched");
        CHECK(studio.fetches.size() == size_t(2));
    }

    TEST_CASE_METHOD(alscriptsearchpane_data, "ALScriptSearchPane: Sources keeps a script by its text and drops the oldest past the limit", "[ALScriptSearchPane]")
    {
        // The viewer's one store: emptied before the case and after it,
        // however it ends.
        struct Emptied
        {
            ALScriptSearch::Sources& sources;
            ~Emptied() { sources.clear(); }
        };
        ALScriptSearch::Sources& sources = ALScriptSearch::Sources::instance();
        sources.clear();
        const Emptied emptied{ sources };
        const ALScriptRef one(LLUUID::generateNewID(), LLUUID::generateNewID());
        const LLUUID      asset = LLUUID::generateNewID();
        sources.keep(one, LLUUID::null, { std::make_shared<const std::string>("timer"), "one.lsl", false });
        UNSCOPED_INFO("which text unknown: not kept");
        CHECK(sources.bytes() == 0);
        sources.keep(one, asset, { std::make_shared<const std::string>("timer"), "one.lsl", false });
        const ALScriptSearch::Sources::Source* kept = sources.find(one, asset);
        {
            INFO("found by its text");
            REQUIRE(kept);
            CHECK(*kept->text == "timer");
            CHECK(kept->name == "one.lsl");
        }
        {
            INFO("not for another");
            CHECK_FALSE(sources.find(one, LLUUID::generateNewID()));
            CHECK_FALSE(sources.find(one, LLUUID::null));
        }
        sources.keep(one, LLUUID::generateNewID(), { std::make_shared<const std::string>("saved since"), "one.lsl", false });
        {
            INFO("saved since: in its place");
            CHECK_FALSE(sources.find(one, asset));
            CHECK(sources.bytes() == std::string("saved since").size());
        }

        sources.clear();
        const size_t              big = ALScriptSearch::Sources::MOST / 3 + 1;
        std::vector<ALScriptRef>  refs;
        std::vector<LLUUID>       assets;
        for (S32 i = 0; i < 3; ++i)
        {
            refs.emplace_back(LLUUID::generateNewID(), LLUUID::generateNewID());
            assets.push_back(LLUUID::generateNewID());
            if (i == 2)
            {
                UNSCOPED_INFO("the first used again");
                REQUIRE(sources.find(refs[0], assets[0]) != nullptr);
            }
            sources.keep(refs[i], assets[i], { std::make_shared<const std::string>(big, 'x'), "big", false });
        }
        UNSCOPED_INFO("within the most");
        CHECK(sources.bytes() <= ALScriptSearch::Sources::MOST);
        {
            INFO("the one used longest ago gone");
            CHECK_FALSE(sources.find(refs[1], assets[1]));
            CHECK(sources.find(refs[0], assets[0]));
            CHECK(sources.find(refs[2], assets[2]));
        }
    }

    TEST_CASE_METHOD(alscriptsearchpane_data, "ALScriptSearchPane: a fetched script is sought apart and an earlier search's is dropped", "[ALScriptSearchPane]")
    {
        ALScriptSearchPane& out  = make();
        const ALScriptRef   away(LLUUID::generateNewID(), LLUUID::generateNewID());
        studio.objects           = { { LLUUID::generateNewID(), "Thing", { away } } };
        find("timer", "listed");
        REQUIRE(studio.fetches.size() == size_t(1));
        out.fetched(studio.fetches[0].generation, "Thing", away, "away.lsl", std::make_shared<const std::string>("timer\n"), false);
        {
            INFO("sought apart");
            CHECK(studio.apart == 1);
            CHECK(out.search().found().size() == 1);
            CHECK(out.search().pending() == 0);
        }
        const S32 before = studio.apart;
        out.fetched(studio.fetches[0].generation + 7, "Thing", away, "away.lsl", std::make_shared<const std::string>("timer\n"), false);
        UNSCOPED_INFO("another search's: not sought");
        CHECK(studio.apart == before);
    }

    TEST_CASE_METHOD(alscriptsearchpane_data, "ALScriptSearchPane: inventory search reads open scripts as they stand and fetches the rest", "[ALScriptSearchPane]")
    {
        ALScriptSearchPane& out  = make();
        Doc&                door = doc("door", "timer here\n");
        door.ref                 = ALScriptRef(LLUUID::null, fresh());
        const ALScriptRef   away(LLUUID::null, fresh());
        studio.inventory         = { { door.ref, "door" }, { away, "away.lsl" } };
        find("timer", "inventory");
        {
            INFO("the open one as it stands");
            REQUIRE(out.search().found().size() == 1);
            CHECK(out.search().found()[0].doc == door.id);
        }
        UNSCOPED_INFO("the other fetched");
        REQUIRE(studio.fetches.size() == size_t(1));
        {
            INFO("from the inventory");
            CHECK(studio.fetches[0].ref == away);
            CHECK(studio.fetches[0].where == window.services().words("SearchInventoryWhere"));
        }
        out.fetched(studio.fetches[0].generation, studio.fetches[0].where, away, "away.lsl", std::make_shared<const std::string>("a timer\n"), false);
        {
            INFO("both found");
            REQUIRE(out.search().found().size() == 2);
            CHECK(out.search().pending() == 0);
        }
        door.editor->setCaret(ALTextPos(0, 0));
        door.editor->insertText("x");
        out.typedIn(door);
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        out.pump();
        REQUIRE_FALSE(out.search().found()[0].places.empty());
        UNSCOPED_INFO("typed in: searched again");
        CHECK(out.search().found()[0].places[0].begin.column == 1);
    }
}
