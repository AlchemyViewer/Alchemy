/**
 * @file alscriptstudiocomparewith_test.cpp
 * @brief Tests for ALScriptStudioCompareWith: what a tab is offered to be set beside, and each chosen.
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

#include "../alscriptstudiocomparewith.h"

#include "../alrecovery.h"
#include "../alscriptexplorermodel.h"
#include "../alscriptstudiohistory.h"
#include "alscriptstudio_fixture.h"
#include "fsyspath.h"

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>

namespace
{
    typedef ALScriptStudioDoc               Doc;
    typedef ALScriptStudioCompareWith::Item Item;
    typedef std::vector<std::string>        Names;

    // A list as a test failing says it.
    std::string listed(const Names& names)
    {
        std::string out;
        for (const std::string& name : names)
        {
            out += (out.empty() ? "[" : ", [") + name + "]";
        }
        return out;
    }

    // Compare With's window, faked: what it was given to pick from, the
    // clipboard, the recent files and the items like a tab as a test sets
    // them, and a record of what it was asked.
    struct FakeCompareWindow : public ALScriptStudioCompareWith::Window
    {
        void compareWithTab(Doc& doc, const std::string& theirs, const std::string& their_title, const std::string& own_title,
                            const ALTextDiff::ranges_t&) override
        {
            // The tab's own text on the right, as the studio puts it.
            did.push_back("compare " + doc.id + ": " + theirs + " | " + doc.editor->wholeText() + " (" + their_title + " | " + own_title + ")");
        }
        void compareWithItem(Doc& doc, const Item& item, const std::string& title) override
        {
            did.push_back("item " + doc.id + ": " + item.name + " (" + title + ")");
        }
        void pick(std::vector<ALQuickOpen::Candidate> given, const std::string& placeholder, const std::string& title,
                  std::function<void(const std::string& value)> chosen, std::function<void(const std::string& value)>) override
        {
            candidates = std::move(given);
            hint       = placeholder;
            picked     = title;
            choose     = std::move(chosen);
            ++picks;
        }
        void pickFilesToOpen(bool several, std::function<void(const std::vector<std::string>& files)> chosen) override
        {
            manyAsked = several;
            files     = std::move(chosen);
            ++filesAsked;
        }
        void                       offerHistory(Doc& doc) override { did.push_back("history " + doc.id); }
        std::optional<std::string> clipboardText() const override { return clipboard; }
        std::vector<std::string>   recentFiles() const override { return recent; }
        std::vector<Item>          itemsLike(const Doc&) const override { return items; }

        // The values offered, in order.
        Names values() const
        {
            Names out;
            for (const ALQuickOpen::Candidate& one : candidates)
            {
                out.push_back(one.value);
            }
            return out;
        }
        // The row offered with a value; a row of nothing where none is.
        ALQuickOpen::Candidate row(const std::string& value) const
        {
            for (const ALQuickOpen::Candidate& one : candidates)
            {
                if (one.value == value)
                {
                    return one;
                }
            }
            return ALQuickOpen::Candidate();
        }

        std::optional<std::string>                            clipboard;
        std::vector<std::string>                              recent;
        std::vector<Item>                                     items;
        std::vector<ALQuickOpen::Candidate>                   candidates;
        std::string                                           hint;
        std::string                                           picked;
        std::function<void(const std::string&)>               choose;
        S32                                                   picks = 0;
        bool                                                  manyAsked = true;
        std::function<void(const std::vector<std::string>&)> files;
        S32                                                   filesAsked = 0;
        Names                                                 did;
    };
}

namespace tut
{
    struct alscriptstudiocomparewith_data
    {
        al_studio_test::StudioWindow                  window;
        FakeCompareWindow                             studio;
        std::unique_ptr<ALScriptStudioCompareWith>    unit = std::make_unique<ALScriptStudioCompareWith>(window.services(), studio);
        std::string                                   folder;
        std::shared_ptr<ALSaveHistory>                history;

        alscriptstudiocomparewith_data()
        {
            folder = fsyspath(std::filesystem::temp_directory_path() / fsyspath("alscriptcompare_" + LLUUID::generateNewID().asString())).string();
            std::filesystem::create_directories(fsyspath(folder));
            history = std::make_shared<ALSaveHistory>(folder + "/history");
            ALRecovery::useHistory(history);
        }
        ~alscriptstudiocomparewith_data()
        {
            ALRecovery::useHistory(nullptr);
            std::error_code ec;
            std::filesystem::remove_all(fsyspath(folder), ec);
        }

        al_studio_test::FakeServices& services() { return window.services(); }
        std::string                   said(const std::string& name, const LLStringUtil::format_map_t& args = {})
        {
            return services().words(name, args);
        }

        // A tab of a script in an object, changeable, holding a text as it
        // was saved; loaded or not.
        Doc& tab(const std::string& id, const std::string& text, bool loaded = true)
        {
            Doc& doc       = services().addDoc(id, ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), id);
            doc.loaded     = loaded;
            doc.modifiable = true;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name     = "editor_" + id;
            p.rect     = LLRect(0, 200, 400, 0);
            p.syntax   = "lsl";
            doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(doc.editor);
            doc.editor->setText(text);
            doc.editor->resetDirty();
            return doc;
        }
        // The tab's text changed since it was saved.
        void change(Doc& doc, const std::string& text)
        {
            doc.editor->selectAll();
            doc.editor->insertText(text);
            ensure("changed since saved", doc.editor->isDirty());
        }
        // A file in the test's folder, written as it is given.
        std::string file(const std::string& name, const std::string& bytes)
        {
            const std::string path = folder + "/" + name;
            std::ofstream     out(static_cast<const std::filesystem::path&>(fsyspath(path)), std::ios::binary);
            out << bytes;
            return path;
        }
    };

    typedef test_group<alscriptstudiocomparewith_data> alscriptstudiocomparewith_group;
    typedef alscriptstudiocomparewith_group::object    alscriptstudiocomparewith_object;
    tut::alscriptstudiocomparewith_group               alscriptstudiocomparewith_test("alscriptstudiocomparewith");

    template<> template<>
    void alscriptstudiocomparewith_object::test<1>()
    {
        set_test_name("a tab alone, as saved, is offered a file; one not loaded is offered nothing");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "default {}\n");
        ensure("it may be compared", ALScriptStudioCompareWith::canCompare(doc));
        unit->show(doc);
        ensure_equals("asked once", studio.picks, 1);
        ensure_equals("a file, and nothing else", listed(studio.values()), listed(Names{ "file" }));
        ensure_equals("labelled", studio.candidates[0].label, said("CompareWithFile"));
        LLStringUtil::format_map_t args;
        args["[NAME]"] = "door";
        ensure_equals("titled by the tab", studio.picked, said("CompareWithTitle", args));
        ensure_equals("and its hint", studio.hint, said("CompareWithPlaceholder"));

        Doc& loading = tab("lamp", "", false);
        ensure("not before it has loaded", !ALScriptStudioCompareWith::canCompare(loading));
        unit->show(loading);
        ensure_equals("not asked", studio.picks, 1);
    }

    template<> template<>
    void alscriptstudiocomparewith_object::test<2>()
    {
        set_test_name("its text as last saved offered once it has changed, and chosen beside it; said where no step reaches it");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "as saved\n");
        change(doc, "as changed\n");
        unit->show(doc);
        ensure_equals("first", studio.values().front(), std::string("saved"));
        ensure_equals("labelled", studio.candidates[0].label, said("CompareSaved"));
        studio.choose("saved");
        ensure_equals("set beside", listed(studio.did), listed(Names{ "compare door: as saved\n | as changed\n (" + said("CompareSaved") + " | )" }));

        // A change after an undo past the save: nothing reaches it.
        doc.editor->undoJournal().markNeverSaved();
        studio.choose("saved");
        ensure_equals("not compared", studio.did.size(), 1U);
        ensure_equals("said", services().statuses.back(), said("CompareNothingSaved"));
        ensure("as a failure", services().statusFailures.back());
    }

    template<> template<>
    void alscriptstudiocomparewith_object::test<3>()
    {
        set_test_name("what was saved over it elsewhere offered where a save came up against it");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "mine\n");
        unit->show(doc);
        ensure("not where none did", studio.row("saved_there").value.empty());
        doc.savedThere = "theirs\n";
        unit->show(doc);
        ensure_equals("offered", studio.row("saved_there").label, said("CompareSavedThere"));
        studio.choose("saved_there");
        ensure_equals("set beside", listed(studio.did), listed(Names{ "compare door: theirs\n | mine\n (" + said("CompareSavedThere") + " | )" }));

        // Settled since the list was made: nothing, and said.
        doc.savedThere.reset();
        studio.choose("saved_there");
        ensure_equals("nothing more", studio.did.size(), 1U);
        ensure_equals("said", services().statuses.back(), said("CompareSavedThereGone"));
        ensure("as a failure", services().statusFailures.back());
    }

    template<> template<>
    void alscriptstudiocomparewith_object::test<4>()
    {
        set_test_name("every other tab loaded offered in the strip's order, said unsaved where it is; chosen as it is now");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door", "door\n");
        Doc& lamp = tab("lamp", "lamp as saved\n");
        change(lamp, "lamp now\n");
        tab("gate", "", false);
        Doc& sign = tab("sign", "sign\n");
        unit->show(door);
        ensure_equals("the others loaded, in order", listed(studio.values()), listed(Names{ "tab:lamp", "tab:sign", "file" }));
        ensure_equals("by name", studio.row("tab:lamp").label, std::string("lamp"));
        ensure_equals("unsaved", studio.row("tab:lamp").detail, said("CompareWithTabUnsaved"));
        ensure_equals("saved", studio.row("tab:sign").detail, said("CompareWithTab"));

        // Typed since the list was made: what is set beside is the tab now.
        change(lamp, "lamp later\n");
        studio.choose("tab:lamp");
        LLStringUtil::format_map_t unsaved;
        unsaved["[TITLE]"] = "lamp";
        // Titled as a copy of it as it was, which stays as it is.
        const auto snapshot = [this](const std::string& name) { return this->said("CompareTabSnapshot", { { "[NAME]", name } }); };
        ensure_equals("unsaved, as it is now", studio.did.back(),
                      "compare door: lamp later\n | door\n (" + snapshot(said("CompareUnsaved", unsaved)) + " | )");
        studio.choose("tab:sign");
        ensure_equals("saved, by its name", studio.did.back(), "compare door: sign\n | door\n (" + snapshot("sign") + " | )");

        // Closed while the list was up: nothing, and said, by the name it
        // had in the list.
        sign.loaded = false;
        services().docs.erase(services().docs.begin() + 3);
        studio.choose("tab:sign");
        ensure_equals("nothing more", studio.did.size(), 2U);
        ensure_equals("said", services().statuses.back(), said("CompareTabClosed", { { "[NAME]", "sign" } }));
        ensure("as a failure", services().statusFailures.back());
    }

    template<> template<>
    void alscriptstudiocomparewith_object::test<5>()
    {
        set_test_name("the clipboard offered with how many lines it holds, where it holds text; chosen as it holds now");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "door\n");
        unit->show(doc);
        ensure("not without text", studio.row("clipboard").value.empty());
        studio.clipboard = std::string();
        unit->show(doc);
        ensure("not with none", studio.row("clipboard").value.empty());

        studio.clipboard = "one\ntwo\n";
        unit->show(doc);
        ensure_equals("labelled", studio.row("clipboard").label, said("CompareClipboard"));
        ensure_equals("two lines", studio.row("clipboard").detail, services().counted("CompareWithLines", 2));
        studio.clipboard = "one\ntwo\nthree";
        unit->show(doc);
        ensure_equals("a last line without a break counted", studio.row("clipboard").detail, services().counted("CompareWithLines", 3));

        studio.clipboard = "copied since\n";
        studio.choose("clipboard");
        ensure_equals("as it holds now", listed(studio.did), listed(Names{ "compare door: copied since\n | door\n (" + said("CompareClipboard") + " | )" }));
        studio.clipboard.reset();
        studio.choose("clipboard");
        ensure_equals("not once emptied", studio.did.size(), 1U);
        ensure_equals("said", services().statuses.back(), said("CompareWithClipboardEmpty"));
    }

    template<> template<>
    void alscriptstudiocomparewith_object::test<6>()
    {
        set_test_name("a file picked is read with its line breaks as an editor keeps them and set beside under its name; said where unreadable");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "door\n");
        unit->show(doc);
        studio.choose("file");
        ensure_equals("one file asked for", studio.filesAsked, 1);
        ensure("only one", !studio.manyAsked);
        studio.files({});
        ensure("none picked, nothing", studio.did.empty());

        const std::string path = file("other.lsl", "one\r\ntwo\r\n");
        studio.files({ path });
        ensure_equals("set beside", listed(studio.did), listed(Names{ "compare door: one\ntwo\n | door\n (other.lsl | )" }));

        unit->show(doc);
        studio.choose("file");
        const std::string missing = folder + "/missing.lsl";
        studio.files({ missing });
        ensure_equals("not compared", studio.did.size(), 1U);
        LLStringUtil::format_map_t args;
        args["[FILE]"] = missing;
        ensure_equals("said", services().statuses.back(), said("LoadFromFileFailed", args));
        ensure("as a failure", services().statusFailures.back());

        // The tab closed while the files were picked: nothing.
        unit->show(doc);
        studio.choose("file");
        services().docs.clear();
        studio.files({ path });
        ensure_equals("nothing more", studio.did.size(), 1U);
    }

    template<> template<>
    void alscriptstudiocomparewith_object::test<7>()
    {
        set_test_name("the files opened lately offered by name and folder, but the tab's own; one chosen set beside");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc&              doc   = tab("door", "door\n");
        const std::string own   = file("door.lsl", "door\n");
        const std::string other = file("lamp.lsl", "lamp\n");
        doc.file                = own;
        studio.recent           = { own, other };
        unit->show(doc);
        ensure_equals("the other alone", listed(studio.values()), listed(Names{ "file", "recent:0" }));
        ensure_equals("by name", studio.row("recent:0").label, std::string("lamp.lsl"));
        ensure_equals("and folder", studio.row("recent:0").detail, gDirUtilp->getDirName(other));
        studio.choose("recent:0");
        ensure_equals("set beside", listed(studio.did), listed(Names{ "compare door: lamp\n | door\n (lamp.lsl | )" }));
    }

    template<> template<>
    void alscriptstudiocomparewith_object::test<8>()
    {
        set_test_name("its local history offered with how many saves are kept, where any are; chosen, its saves listed");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "door\n");
        unit->show(doc);
        ensure("not with none kept", studio.row("history").value.empty());
        for (const char* text : { "first", "second" })
        {
            ALSavedText one;
            one.key  = ALScriptStudioHistory::keyOf(doc);
            one.text = text;
            one.when = LLDate::now();
            ensure("kept", history->keep(one));
        }
        unit->show(doc);
        ensure_equals("labelled", studio.row("history").label, said("CompareWithHistory"));
        ensure_equals("two kept", studio.row("history").detail, services().counted("CompareWithSaves", 2));
        studio.choose("history");
        ensure_equals("listed", listed(studio.did), listed(Names{ "history door" }));
    }

    template<> template<>
    void alscriptstudiocomparewith_object::test<9>()
    {
        set_test_name("the items like it in objects in hand offered by name and place, last; one chosen loaded under both");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const std::string sep = ALScriptExplorerModel::PLACE_SEPARATOR;
        Doc&              doc = tab("door", "door\n");
        studio.items = { Item{ ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), "door", "House" + sep + "Front" },
                         Item{ ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), "door", "" } };
        studio.clipboard = "x";
        unit->show(doc);
        ensure_equals("after the rest", listed(studio.values()), listed(Names{ "clipboard", "file", "item:0", "item:1" }));
        ensure_equals("by name", studio.row("item:0").label, std::string("door"));
        ensure_equals("and place", studio.row("item:0").detail, "House" + sep + "Front");
        studio.choose("item:0");
        studio.choose("item:1");
        ensure_equals("each under its place and name", listed(studio.did),
                      listed(Names{ "item door: door (House" + sep + "Front" + sep + "door)", "item door: door (door)" }));

        // Its place is what is typed to find it.
        const std::vector<size_t> ranked = ALQuickOpen::rank(studio.candidates, "front");
        ensure("found by its place", !ranked.empty() && studio.candidates[ranked.front()].value == "item:0");
    }

    template<> template<>
    void alscriptstudiocomparewith_object::test<10>()
    {
        set_test_name("a choice made after the window has gone does nothing");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc         = tab("door", "door\n");
        studio.clipboard = "copied\n";
        unit->show(doc);
        unit.reset();
        studio.choose("clipboard");
        ensure("nothing", studio.did.empty());
    }
}
