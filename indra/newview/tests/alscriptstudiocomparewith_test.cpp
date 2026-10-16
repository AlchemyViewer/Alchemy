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

#include <catch2/catch_test_macros.hpp>

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

namespace
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
        void require_changed(Doc& doc, const std::string& text)
        {
            doc.editor->selectAll();
            doc.editor->insertText(text);
            INFO("changed since saved");
            REQUIRE(doc.editor->isDirty());
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

    TEST_CASE_METHOD(alscriptstudiocomparewith_data, "alscriptstudiocomparewith: a saved tab alone is offered a file and an unloaded one nothing", "[alscriptstudiocomparewith]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "default {}\n");
        CHECK(ALScriptStudioCompareWith::canCompare(doc));
        unit->show(doc);
        UNSCOPED_INFO("asked once");
        CHECK(studio.picks == 1);
        REQUIRE(listed(studio.values()) == listed(Names{ "file" }));
        CHECK(studio.candidates[0].label == said("CompareWithFile"));
        LLStringUtil::format_map_t args;
        args["[NAME]"] = "door";
        UNSCOPED_INFO("titled by the tab");
        CHECK(studio.picked == said("CompareWithTitle", args));
        CHECK(studio.hint == said("CompareWithPlaceholder"));

        Doc& loading = tab("lamp", "", false);
        UNSCOPED_INFO("not before it has loaded");
        CHECK_FALSE(ALScriptStudioCompareWith::canCompare(loading));
        unit->show(loading);
        UNSCOPED_INFO("not asked");
        CHECK(studio.picks == 1);
    }

    TEST_CASE_METHOD(alscriptstudiocomparewith_data, "alscriptstudiocomparewith: a changed tab is offered its saved text unless no step reaches it", "[alscriptstudiocomparewith]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "as saved\n");
        require_changed(doc, "as changed\n");
        unit->show(doc);
        REQUIRE_FALSE(studio.candidates.empty());
        CHECK(studio.values().front() == std::string("saved"));
        CHECK(studio.candidates[0].label == said("CompareSaved"));
        studio.choose("saved");
        CHECK(listed(studio.did) == listed(Names{ "compare door: as saved\n | as changed\n (" + said("CompareSaved") + " | )" }));

        // A change after an undo past the save: nothing reaches it.
        doc.editor->undoJournal().markNeverSaved();
        studio.choose("saved");
        UNSCOPED_INFO("not compared");
        CHECK(studio.did.size() == 1U);
        REQUIRE_FALSE(services().statuses.empty());
        CHECK(services().statuses.back() == said("CompareNothingSaved"));
        UNSCOPED_INFO("as a failure");
        CHECK(services().statusFailures.back());
    }

    TEST_CASE_METHOD(alscriptstudiocomparewith_data, "alscriptstudiocomparewith: what was saved over it elsewhere is offered until it is settled", "[alscriptstudiocomparewith]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "mine\n");
        unit->show(doc);
        UNSCOPED_INFO("not where none did");
        CHECK(studio.row("saved_there").value.empty());
        doc.savedThere = "theirs\n";
        unit->show(doc);
        CHECK(studio.row("saved_there").label == said("CompareSavedThere"));
        studio.choose("saved_there");
        CHECK(listed(studio.did) == listed(Names{ "compare door: theirs\n | mine\n (" + said("CompareSavedThere") + " | )" }));

        // Settled since the list was made: nothing, and said.
        doc.savedThere.reset();
        studio.choose("saved_there");
        UNSCOPED_INFO("nothing more");
        CHECK(studio.did.size() == 1U);
        REQUIRE_FALSE(services().statuses.empty());
        CHECK(services().statuses.back() == said("CompareSavedThereGone"));
        UNSCOPED_INFO("as a failure");
        CHECK(services().statusFailures.back());
    }

    TEST_CASE_METHOD(alscriptstudiocomparewith_data, "alscriptstudiocomparewith: other loaded tabs are offered in order and compared as they are now", "[alscriptstudiocomparewith]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& door = tab("door", "door\n");
        Doc& lamp = tab("lamp", "lamp as saved\n");
        require_changed(lamp, "lamp now\n");
        tab("gate", "", false);
        Doc& sign = tab("sign", "sign\n");
        unit->show(door);
        UNSCOPED_INFO("the others loaded, in order");
        CHECK(listed(studio.values()) == listed(Names{ "tab:lamp", "tab:sign", "file" }));
        CHECK(studio.row("tab:lamp").label == std::string("lamp"));
        CHECK(studio.row("tab:lamp").detail == said("CompareWithTabUnsaved"));
        CHECK(studio.row("tab:sign").detail == said("CompareWithTab"));

        // Typed since the list was made: what is set beside is the tab now.
        require_changed(lamp, "lamp later\n");
        studio.choose("tab:lamp");
        LLStringUtil::format_map_t unsaved;
        unsaved["[TITLE]"] = "lamp";
        // Titled as a copy of it as it was, which stays as it is.
        const auto snapshot = [this](const std::string& name) { return this->said("CompareTabSnapshot", { { "[NAME]", name } }); };
        REQUIRE_FALSE(studio.did.empty());
        UNSCOPED_INFO("unsaved, as it is now");
        CHECK(studio.did.back() ==
              "compare door: lamp later\n | door\n (" + snapshot(said("CompareUnsaved", unsaved)) + " | )");
        studio.choose("tab:sign");
        UNSCOPED_INFO("saved, by its name");
        CHECK(studio.did.back() == "compare door: sign\n | door\n (" + snapshot("sign") + " | )");

        // Closed while the list was up: nothing, and said, by the name it
        // had in the list.
        sign.loaded = false;
        services().docs.erase(services().docs.begin() + 3);
        studio.choose("tab:sign");
        UNSCOPED_INFO("nothing more");
        CHECK(studio.did.size() == 2U);
        REQUIRE_FALSE(services().statuses.empty());
        CHECK(services().statuses.back() == said("CompareTabClosed", { { "[NAME]", "sign" } }));
        UNSCOPED_INFO("as a failure");
        CHECK(services().statusFailures.back());
    }

    TEST_CASE_METHOD(alscriptstudiocomparewith_data, "alscriptstudiocomparewith: the clipboard is offered with its line count when it holds text", "[alscriptstudiocomparewith]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "door\n");
        unit->show(doc);
        UNSCOPED_INFO("not without text");
        CHECK(studio.row("clipboard").value.empty());
        studio.clipboard = std::string();
        unit->show(doc);
        UNSCOPED_INFO("not with none");
        CHECK(studio.row("clipboard").value.empty());

        studio.clipboard = "one\ntwo\n";
        unit->show(doc);
        CHECK(studio.row("clipboard").label == said("CompareClipboard"));
        CHECK(studio.row("clipboard").detail == services().counted("CompareWithLines", 2));
        studio.clipboard = "one\ntwo\nthree";
        unit->show(doc);
        UNSCOPED_INFO("a last line without a break counted");
        CHECK(studio.row("clipboard").detail == services().counted("CompareWithLines", 3));

        studio.clipboard = "copied since\n";
        studio.choose("clipboard");
        UNSCOPED_INFO("as it holds now");
        CHECK(listed(studio.did) == listed(Names{ "compare door: copied since\n | door\n (" + said("CompareClipboard") + " | )" }));
        studio.clipboard.reset();
        studio.choose("clipboard");
        UNSCOPED_INFO("not once emptied");
        CHECK(studio.did.size() == 1U);
        REQUIRE_FALSE(services().statuses.empty());
        CHECK(services().statuses.back() == said("CompareWithClipboardEmpty"));
    }

    TEST_CASE_METHOD(alscriptstudiocomparewith_data, "alscriptstudiocomparewith: a picked file is compared with LF line breaks or said unreadable", "[alscriptstudiocomparewith]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "door\n");
        unit->show(doc);
        studio.choose("file");
        UNSCOPED_INFO("one file asked for");
        CHECK(studio.filesAsked == 1);
        UNSCOPED_INFO("only one");
        CHECK_FALSE(studio.manyAsked);
        studio.files({});
        UNSCOPED_INFO("none picked, nothing");
        CHECK(studio.did.empty());

        const std::string path = file("other.lsl", "one\r\ntwo\r\n");
        studio.files({ path });
        UNSCOPED_INFO("its line breaks as an editor keeps them");
        CHECK(listed(studio.did) == listed(Names{ "compare door: one\ntwo\n | door\n (other.lsl | )" }));

        unit->show(doc);
        studio.choose("file");
        const std::string missing = folder + "/missing.lsl";
        studio.files({ missing });
        UNSCOPED_INFO("not compared");
        CHECK(studio.did.size() == 1U);
        LLStringUtil::format_map_t args;
        args["[FILE]"] = missing;
        REQUIRE_FALSE(services().statuses.empty());
        CHECK(services().statuses.back() == said("LoadFromFileFailed", args));
        UNSCOPED_INFO("as a failure");
        CHECK(services().statusFailures.back());

        // The tab closed while the files were picked: nothing.
        unit->show(doc);
        studio.choose("file");
        services().docs.clear();
        studio.files({ path });
        UNSCOPED_INFO("nothing more");
        CHECK(studio.did.size() == 1U);
    }

    TEST_CASE_METHOD(alscriptstudiocomparewith_data, "alscriptstudiocomparewith: recent files but the tab's own are offered by name and folder", "[alscriptstudiocomparewith]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc&              doc   = tab("door", "door\n");
        const std::string own   = file("door.lsl", "door\n");
        const std::string other = file("lamp.lsl", "lamp\n");
        doc.file                = own;
        studio.recent           = { own, other };
        unit->show(doc);
        UNSCOPED_INFO("the other alone");
        CHECK(listed(studio.values()) == listed(Names{ "file", "recent:0" }));
        CHECK(studio.row("recent:0").label == std::string("lamp.lsl"));
        CHECK(studio.row("recent:0").detail == gDirUtilp->getDirName(other));
        studio.choose("recent:0");
        CHECK(listed(studio.did) == listed(Names{ "compare door: lamp\n | door\n (lamp.lsl | )" }));
    }

    TEST_CASE_METHOD(alscriptstudiocomparewith_data, "alscriptstudiocomparewith: local history is offered with its save count when it has any", "[alscriptstudiocomparewith]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc = tab("door", "door\n");
        unit->show(doc);
        UNSCOPED_INFO("not with none kept");
        CHECK(studio.row("history").value.empty());
        for (const char* text : { "first", "second" })
        {
            ALSavedText one;
            one.key  = ALScriptStudioHistory::keyOf(doc);
            one.text = text;
            one.when = LLDate::now();
            CAPTURE(text);
            REQUIRE(history->keep(one));
        }
        unit->show(doc);
        CHECK(studio.row("history").label == said("CompareWithHistory"));
        CHECK(studio.row("history").detail == services().counted("CompareWithSaves", 2));
        studio.choose("history");
        CHECK(listed(studio.did) == listed(Names{ "history door" }));
    }

    TEST_CASE_METHOD(alscriptstudiocomparewith_data, "alscriptstudiocomparewith: items like it are offered last by name and place and found by place", "[alscriptstudiocomparewith]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const std::string sep = ALScriptExplorerModel::PLACE_SEPARATOR;
        Doc&              doc = tab("door", "door\n");
        studio.items = { Item{ ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), "door", "House" + sep + "Front" },
                         Item{ ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), "door", "" } };
        studio.clipboard = "x";
        unit->show(doc);
        UNSCOPED_INFO("after the rest");
        CHECK(listed(studio.values()) == listed(Names{ "clipboard", "file", "item:0", "item:1" }));
        CHECK(studio.row("item:0").label == std::string("door"));
        CHECK(studio.row("item:0").detail == "House" + sep + "Front");
        studio.choose("item:0");
        studio.choose("item:1");
        UNSCOPED_INFO("each under its place and name");
        CHECK(listed(studio.did) ==
              listed(Names{ "item door: door (House" + sep + "Front" + sep + "door)", "item door: door (door)" }));

        // Its place is what is typed to find it.
        const std::vector<size_t> ranked = ALQuickOpen::rank(studio.candidates, "front");
        {
            INFO("found by its place");
            REQUIRE_FALSE(ranked.empty());
            CHECK(studio.candidates[ranked.front()].value == "item:0");
        }
    }

    TEST_CASE_METHOD(alscriptstudiocomparewith_data, "alscriptstudiocomparewith: a choice made after the window has gone does nothing", "[alscriptstudiocomparewith]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Doc& doc         = tab("door", "door\n");
        studio.clipboard = "copied\n";
        unit->show(doc);
        unit.reset();
        studio.choose("clipboard");
        CHECK(studio.did.empty());
    }
}
