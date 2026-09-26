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

#include "../test/lltut.h"

#include <chrono>
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

        std::vector<Object> objectsListed() const override { return objects; }
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
        void   fetchForSearch(const ALScriptRef& ref, U32 generation, const std::string& where) override { fetches.push_back({ ref, generation, where }); }
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

        std::vector<Object>      objects;
        LLUUID                   inHand;
        Doc*                     elsewhere = nullptr;
        std::vector<Fetch>       fetches;
        std::vector<std::string> applied;
        std::vector<LLSD>        asked;
        std::vector<Chosen>      chosen;
    };

    LLUUID fresh()
    {
        LLUUID id;
        id.generate();
        return id;
    }
}

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            // The tab as the skin built it.
            pane = window.find<ALScriptSearchPane>("search_tab");
            ensure("the skin builds the tab as the pane", pane != nullptr);
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
    typedef test_group<alscriptsearchpane_data> alscriptsearchpane_group;
    typedef alscriptsearchpane_group::object    alscriptsearchpane_object;
    alscriptsearchpane_group                    alscriptsearchpane_instance("ALScriptSearchPane");

    template <>
    template <>
    void alscriptsearchpane_object::test<1>()
    {
        set_test_name("places found in the scripts open as they stand, and in an object's as fetched, a script open elsewhere as it stands there");
        ALScriptSearchPane& out  = make();
        Doc&                door = doc("door", "timer one\nnothing\n  timer two\n");
        find("timer");
        ensure_equals("one script", out.search().found().size(), size_t(1));
        ensure_equals("two places", out.search().found()[0].places.size(), size_t(2));
        ensure_equals("a row each", out.list()->getItemCount(), 2);
        // The list's keys: return goes to the place chosen, to type there,
        // escape back to the script; and the rows copy.
        out.list()->selectFirstItem();
        ensure("return", out.list()->handleKeyHere(KEY_RETURN, MASK_NONE) && !studio.chosen.empty() && studio.chosen.back().toEditor);
        ensure("escape", out.list()->handleKeyHere(KEY_ESCAPE, MASK_NONE) && services.reveals.back());
        ensure("copied", out.list()->handleKeyHere('C', MASK_CONTROL));
        ensure_equals("the line as listed, trimmed", out.search().found()[0].lines[1], std::string("timer two"));
        ensure_equals("counted", counted(), services.words("SearchCount", { { "[HITS]", services.counted("Matches", 2) },
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
        ensure_equals("the open one and the one elsewhere", out.search().found().size(), size_t(2));
        ensure_equals("where it is elsewhere, said", out.search().found()[1].where, std::string("Thing: there.lsl"));
        ensure_equals("the rest fetched", studio.fetches.size(), size_t(1));
        ensure("the one asked", studio.fetches[0].ref == away);
        ensure_equals("counting while it comes", counted(), services.words("SearchCounting", { { "[HITS]", services.counted("Matches", 3) },
                                                                                              { "[FILES]", services.counted("Files", 2) },
                                                                                              { "[SHOWN]", "2000" } }));
        out.fetched(studio.fetches[0].generation, "Thing", away, "away.lsl", std::string("x\ntimer fetched\n"), false);
        ensure_equals("fetched, found", out.search().found().size(), size_t(3));
        ensure_equals("its text kept, for a replace", out.search().found()[2].text, std::string("x\ntimer fetched\n"));
        ensure_equals("none to wait for", out.search().pending(), 0);

        // No object in hand: none guessed at.
        studio.inHand.setNull();
        find("timer", "object");
        ensure_equals("said", counted(), services.words("SearchNoObject"));
    }

    template <>
    template <>
    void alscriptsearchpane_object::test<2>()
    {
        set_test_name("a later search drops what an earlier one is answered");
        ALScriptSearchPane& out = make();
        const ALScriptRef   away(fresh(), fresh());
        const LLUUID        root = fresh();
        studio.objects           = { { root, "Thing", { away } } };
        find("timer", "listed");
        find("other", "listed");
        ensure_equals("asked twice", studio.fetches.size(), size_t(2));
        out.fetched(studio.fetches[0].generation, "Thing", away, "away.lsl", std::string("timer\nother\n"), false);
        ensure("the first search's answer dropped", out.search().found().empty());
        ensure_equals("still waiting on the second's", out.search().pending(), 1);
        out.fetched(studio.fetches[1].generation, "Thing", away, "away.lsl", std::string("timer\nother\n"), false);
        ensure_equals("the second's kept", out.search().found().size(), size_t(1));
        ensure_equals("for what it looked for", out.search().found()[0].places[0].begin.line, 1);
    }

    template <>
    template <>
    void alscriptsearchpane_object::test<3>()
    {
        set_test_name("a script typed in since the search is not replaced over, and is searched again a moment later");
        ALScriptSearchPane& out  = make();
        Doc&                door = doc("door", "timer one\ntimer two\n");
        find("timer");
        door.editor->setCaret(ALTextPos(0, 0));
        door.editor->insertText("x");
        out.replaceAll();
        ensure_equals("left alone", door.editor->text(), std::string("xtimer one\ntimer two\n"));
        ensure("said so", !services.reports.empty() && services.reports.back().failure);

        // Searched again once the typing has rested: its places are where
        // they are now.
        find("timer");
        door.editor->setCaret(ALTextPos(0, 0));
        door.editor->insertText("yy");
        out.typedIn(door);
        out.pump();
        ensure_equals("not at once", out.search().found()[0].places[0].begin.column, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        out.pump();
        ensure_equals("then, where it is now", out.search().found()[0].places[0].begin.column, 3);
        out.replaceAll();
        ensure_equals("and replaced", door.editor->text(), std::string("yyx one\n two\n"));
        door.editor->undo();
        ensure_equals("as one step", door.editor->text(), std::string("yyxtimer one\ntimer two\n"));
    }

    template <>
    template <>
    void alscriptsearchpane_object::test<4>()
    {
        set_test_name("Replace All, asked first: an open script's places as one step, a script not open opened with the change unsaved, a notecard left alone");
        ALScriptSearchPane& out  = make();
        Doc&                door = doc("door", "llSay(0, \"timer\");\n");
        const ALScriptRef   away(fresh(), fresh());
        const ALScriptRef   card(fresh(), fresh());
        const LLUUID        root = door.ref.object;
        studio.objects           = { { root, "Thing", { door.ref, away, card } } };
        studio.inHand            = root;
        find("timer", "object");
        out.fetched(studio.fetches[0].generation, "Thing", away, "away.lsl", std::string("timer\n"), false);
        out.fetched(studio.fetches[1].generation, "Thing", card, "card", std::string("a timer\n"), true);
        ensure_equals("three found", out.search().found().size(), size_t(3));
        services.whenOpened = [this](const ALScriptRef& ref, const std::string&) {
            Doc& opened = doc("away", "timer\n");
            opened.ref  = ref;
        };

        window.find<LLLineEditor>("search_replacement")->setText(std::string("clock"));
        out.askReplaceAll();
        ensure_equals("asked", studio.asked.size(), size_t(1));
        ensure_equals("of the scripts, not the notecard", studio.asked[0]["SCRIPTS"].asString(), services.counted("Scripts", 2));
        ensure_equals("the open one, replaced", door.editor->text(), std::string("llSay(0, \"clock\");\n"));
        ensure_equals("the one not open, opened", services.opened.size(), size_t(1));
        ensure("by its item", services.opened[0].ref == away);
        const Doc* opened = services.findDoc(away);
        ensure("a tab for it", opened != nullptr);
        ensure_equals("its change waiting on its text", opened->pendingEdits.size(), size_t(1));
        ensure_equals("which it replaces", opened->pendingEdits[0].now, std::string("clock"));
        ensure_equals("and is made", studio.applied.size(), size_t(1));
        ensure("the notecard not opened", services.opened.size() == 1);
        ensure_equals("said", services.reports.back().text,
                      services.words("SearchReplaced", { { "[PLACES]", services.counted("Places", 2) }, { "[SCRIPTS]", services.counted("Scripts", 2) } }) + "; " +
                          services.words("SearchReplacedOpened", { { "[PLACES]", services.counted("Places", 2) }, { "[SCRIPTS]", services.counted("Scripts", 1) } }) +
                          ".");
    }

    template <>
    template <>
    void alscriptsearchpane_object::test<5>()
    {
        set_test_name("what Replace All does with a script found, by where it is now");
        using Step = ALScriptSearch::Step;
        using At   = ALScriptSearch::Now::At;
        ALScriptSearch::Found one;
        one.ref     = ALScriptRef(fresh(), fresh());
        one.version = 7;
        one.places  = { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)) };
        ALTextDocument text("timer\n");
        ALScriptSearch::Now now;

        ensure("closed, with no text kept: left", ALScriptSearch::step(one, now) == Step::Leave);
        one.text = "timer\n";
        ensure("closed, with its text: opened", ALScriptSearch::step(one, now) == Step::Open);
        ALScriptSearch::Found file = one;
        file.ref                   = ALScriptRef();
        ensure("a file closed since: left", ALScriptSearch::step(file, now) == Step::Leave);

        now.at         = At::Here;
        now.loaded     = true;
        now.modifiable = true;
        now.text       = &text;
        ensure("here, at another version: left", ALScriptSearch::step(one, now) == Step::Leave);
        one.version = text.version();
        ensure("here, as searched: replaced", ALScriptSearch::step(one, now) == Step::Replace);
        now.modifiable = false;
        ensure("here, not to be changed: left", ALScriptSearch::step(one, now) == Step::Leave);
        now.modifiable = true;
        now.notecard   = true;
        ensure("here, a notecard: skipped", ALScriptSearch::step(one, now) == Step::Skip);
        now.notecard = false;

        now.at = At::Elsewhere;
        ensure("elsewhere, reading as it did: replaced", ALScriptSearch::step(one, now) == Step::Replace);
        ALTextDocument typed("timer!\n");
        now.text = &typed;
        ensure("elsewhere, typed in since: left", ALScriptSearch::step(one, now) == Step::Leave);

        one.notecard = true;
        ensure("found in a notecard: skipped", ALScriptSearch::step(one, now) == Step::Skip);
        one.notecard = false;
        one.places.clear();
        ensure("nothing found: skipped", ALScriptSearch::step(one, now) == Step::Skip);
    }
}
