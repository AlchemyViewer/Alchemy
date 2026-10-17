/**
 * @file alscriptstudiomasters_test.cpp
 * @brief A Script Studio window's side of scripts mastered by files on disk: what may be linked, linking a tab, and a tab giving way.
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

#include "../alscriptstudiomasters.h"
#include "../alnotecardembedded.h"

#include "alfilewrite.h"
#include "almainloop.h"
#include "alscriptstudio_fixture.h"
#include "alwatchedfile.h"
#include "fsyspath.h"
#include "llfile.h"
#include "newview_test_fakes.h"
#include "workqueue.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace
{
    typedef ALScriptStudioDoc              Doc;
    typedef ALScriptStudioMasters::Unsaved Unsaved;

    // The account's scripts mastered on disk, faked. In the viewer they are
    // ALScriptDiskMasters': the index a file under the account's folder,
    // each send through the workspace, the preprocessor and the region, and
    // the files watched. Here the links are held in memory, in the real
    // records (ALMasterLinks), and told as changed as the viewer's are;
    // nothing is written, watched or sent -- a write and a send are only
    // noted -- and an outcome is told as a test tells it.
    class FakeDiskMasters final : public ALScriptStudioMasters::DiskMasters
    {
    public:
        std::optional<ALMasterLink> linkOf(const ALScriptRef& ref) override
        {
            const ALMasterLink* link = links.of(ref.object, ref.item);
            return link ? std::optional<ALMasterLink>(*link) : std::nullopt;
        }
        std::vector<ALMasterLink> mastering(const std::string& master) override
        {
            std::vector<ALMasterLink> out;
            for (const ALMasterLink* link : links.mastering(master))
            {
                out.push_back(*link);
            }
            return out;
        }
        bool masters(const std::string& master) override { return !mastering(master).empty(); }
        void link(ALMasterLink link) override
        {
            links.put(std::move(link));
            changed();
        }
        void unlink(const ALScriptRef& ref) override
        {
            if (links.remove(ref.object, ref.item))
            {
                changed();
            }
        }
        void send(const ALScriptRef& ref, ALMasterPlan::Send kind) override { sent.emplace_back(ref, kind); }
        void wrote(const std::string& path) override { written.push_back(path); }
        boost::signals2::connection onOutcome(std::function<void(const Outcome& outcome)> heard) override { return told.connect(std::move(heard)); }
        boost::signals2::connection onChanged(std::function<void()> heard) override { return changed.connect(std::move(heard)); }
        std::vector<Outcome>        takeUnheard() override
        {
            std::vector<Outcome> out;
            out.swap(unheard);
            return out;
        }
        ALDiskIncludes blessedFor(const std::string&, bool) override { return ALDiskIncludes(); }
        std::vector<std::pair<std::string, std::string>> aliasesFor(const std::string&, bool) override { return {}; }

        ALMasterLinks                                           links;
        // What the masters asked beyond the links.
        std::vector<std::string>                                written;
        std::vector<std::pair<ALScriptRef, ALMasterPlan::Send>> sent;
        std::vector<Outcome>                                    unheard;
        boost::signals2::signal<void(const Outcome&)>           told;
        boost::signals2::signal<void()>                         changed;
    };

    // The world as a notecard's items see it, answering nothing: what a
    // tab's items need to be made, and no more.
    class NoWorld final : public ALNotecardEmbedded::World
    {
    public:
        std::string iconOf(const LLInventoryItem&) const override { return std::string(); }
        bool        draggedFromNotecard() const override { return false; }
        bool        carriesSettings() const override { return false; }
        bool        mayCopy(const LLInventoryItem&) const override { return false; }
        U32         frame() const override { return 0; }
        bool        open(const LLPointer<LLInventoryItem>&, const ALScriptRef&, std::function<void(const LLUUID&, U32)>) override { return true; }
        void        confirmCopy(std::function<void()>) override {}
        bool        askCopy(const ALScriptRef&, const LLUUID&, const LLUUID&, U32, std::function<void(const std::string&)>) override { return false; }
        void        pressedAt(S32, S32) override {}
        bool        pastDragStart(S32, S32) override { return false; }
        void        dragOut(const LLInventoryItem&, const ALScriptRef&) override {}
    };

    // The window, faked: the file picked and the question answered as a
    // test sets them, and a record of the rest. A file's tab opened holds
    // what the file does, and a tab closed goes from the window's tabs, as
    // the window's own do.
    struct FakeMastersWindow final : public ALScriptStudioMasters::Window
    {
        struct Compared
        {
            std::string doc, left, right, leftTitle, rightTitle;
        };

        FakeMastersWindow(al_studio_test::FakeServices& services_in, LLView& parent_in) : services(services_in), parent(parent_in) {}

        void pickMasterFile(std::function<void(const std::string& path)> chosen) override
        {
            ++picks;
            if (picked)
            {
                chosen(*picked);
            }
        }
        void openMasterFile(const std::string& path, bool lua) override
        {
            opened.push_back(path);
            if (services.findDoc("disk:" + path))
            {
                return;
            }
            std::string text;
            ALFileRead::whole(path, text, ALDiskIncludes::MAX_BYTES);
            Doc& doc         = services.addDoc("disk:" + path, ALScriptRef(), fsyspath(path).filename().string());
            doc.file         = path;
            doc.loaded       = true;
            doc.modifiable   = true;
            doc.language.lua = lua;
            doc.editor       = editor("editor_disk_" + std::to_string(++made), text);
        }
        void closeTab(Doc& doc) override
        {
            closed.push_back(doc.id);
            std::erase_if(services.docs, [&doc](const std::unique_ptr<Doc>& one) { return one.get() == &doc; });
        }
        void askLinkUnsaved(const Doc& doc, const std::string& path, std::function<void(Unsaved answer)> answered) override
        {
            asked.push_back(doc.id + " " + path);
            if (answer)
            {
                answered(*answer);
            }
        }
        void editMasterFile(const std::string& path, bool) override { edited.push_back(path); }
        void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                     const std::string& right_title) override
        {
            compared.push_back({ doc.id, left, right, left_title, right_title });
        }
        bool heldByBridge(const ALScriptRef&) override { return false; }
        void loadWorldText(const ALScriptRef& ref, std::function<void(const ALScriptLoaded& loaded)> loaded) override
        {
            loads.push_back(ref);
            toLoad = std::move(loaded);
        }
        void refreshNotice() override { ++notices; }

        // An editor as the studio makes one, kept by the window.
        ALCodeEditor* editor(const std::string& name, const std::string& text)
        {
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name             = name;
            p.rect             = LLRect(0, 200, 400, 0);
            p.syntax           = "lsl";
            ALCodeEditor* made_one = LLUICtrlFactory::create<ALCodeEditor>(p);
            parent.addChild(made_one);
            made_one->setText(text);
            made_one->resetDirty();
            return made_one;
        }

        al_studio_test::FakeServices&                       services;
        LLView&                                             parent;
        std::optional<std::string>                          picked;
        std::optional<Unsaved>                              answer;
        S32                                                 picks   = 0;
        S32                                                 notices = 0;
        S32                                                 made  = 0;
        std::vector<std::string>                            opened, closed, asked, edited;
        std::vector<Compared>                               compared;
        std::vector<ALScriptRef>                            loads;
        std::function<void(const ALScriptLoaded& loaded)> toLoad;
    };
}

namespace
{
    struct alscriptstudiomasters_data
    {
        al_studio_test::StudioWindow           window;
        al_studio_test::FakeServices           services;
        al_studio_test::QuietAnalysis          analysis;
        std::unique_ptr<FakeMastersWindow>     studio;
        FakeDiskMasters                        disk;
        std::unique_ptr<ALScriptStudioMasters> unit;
        NoWorld                                world;
        std::string                            folder;
        std::vector<boost::signals2::connection> wired;
        // A look at the tabs waits for the main loop, so every case here
        // has one: this file's choice, not whatever a test before it made.
        const ALMainLoop::Choice               main_loop{ &mainLoop() };

        ~alscriptstudiomasters_data()
        {
            for (boost::signals2::connection& one : wired)
            {
                one.disconnect();
            }
            unit.reset();
            services.docs.clear();
            if (!folder.empty())
            {
                std::error_code ignored;
                std::filesystem::remove_all(fsyspath(folder), ignored);
            }
        }

        // The queue standing in for the main loop's, which a look at the
        // tabs waits for: the process's, kept for the rest of the run.
        static LL::WorkQueue& mainLoop() { return newview_test::mainLoop(); }
        // Whatever was waiting for the main loop, done.
        static void settle() { mainLoop().runPending(); }

        ALScriptStudioMasters& make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            const fsyspath made = std::filesystem::temp_directory_path() / fsyspath("alscriptstudiomasters_" + LLUUID::generateNewID().asString());
            std::filesystem::create_directories(made);
            folder = made.string();
            studio = std::make_unique<FakeMastersWindow>(services, *window.floater);
            unit   = std::make_unique<ALScriptStudioMasters>(services, analysis, *studio, disk);
            return *unit;
        }

        std::string in(const std::string& name) const { return fsyspath(fsyspath(folder) / fsyspath(name)).string(); }
        static void write(const std::string& path, const std::string& text) { llofstream(fsyspath(path), std::ios::binary) << text; }
        static std::string contents(const std::string& path)
        {
            std::string text;
            ALFileRead::whole(path, text, ALDiskIncludes::MAX_BYTES);
            return text;
        }

        // An item's tab, loaded and clean, of a script in an object, or of a
        // notecard.
        Doc& itemTab(const std::string& name, const std::string& text, bool lua = false, bool notecard = false)
        {
            Doc& doc                   = services.addDoc(name, ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), name);
            doc.loaded                 = true;
            doc.modifiable             = true;
            doc.notecard               = notecard;
            doc.language.lua           = lua;
            doc.language.compileTarget = lua ? "luau" : "mono";
            doc.assetId                = LLUUID::generateNewID();
            doc.objectName             = "Door";
            doc.editor                 = studio->editor("editor_" + name, text);
            return doc;
        }
        // A tab's edits, undos and redos told to the masters, as the
        // window's own tabs' are; found again by its id, since it may give
        // way.
        void wire(const Doc& doc)
        {
            const std::string id = doc.id;
            wired.push_back(doc.editor->onTextChanged([this, id]() {
                if (Doc* found = services.findDoc(id))
                {
                    unit->textChanged(*found);
                }
            }));
        }
        // Something typed at the end of a tab's text.
        static void type(Doc& doc, const std::string& text)
        {
            doc.editor->setCaret(doc.editor->document().end());
            doc.editor->insertText(text);
        }
        // A link made from elsewhere -- another window, the Link tab -- of a
        // tab's script to a file.
        ALMasterLink linkFromElsewhere(const Doc& doc, const std::string& path)
        {
            ALMasterLink link;
            link.object   = doc.ref.object;
            link.item     = doc.ref.item;
            link.master   = path;
            link.itemName = doc.name;
            disk.link(link);
            return link;
        }

        // The last thing said; there must be something.
        const al_studio_test::FakeServices::Said& said() const
        {
            REQUIRE_FALSE(services.reports.empty());
            return services.reports.back();
        }
        static bool says(const std::string& text, const std::string& word) { return text.find(word) != std::string::npos; }
        typedef std::vector<std::string> Names;
    };

    // A tab may be linked where it is an item's, loaded, may be changed and is linked to
    // nothing yet.
    TEST_CASE_METHOD(alscriptstudiomasters_data, "alscriptstudiomasters: only a loaded changeable item's unlinked tab may be linked", "[alscriptstudiomasters]")
    {
        make();
        Doc& script = itemTab("door", "default {}\n");
        Doc& card   = itemTab("card", "notes\n", false, true);
        CHECK_FALSE(unit->canLink(nullptr));
        UNSCOPED_INFO("an item's script");
        CHECK(unit->canLink(&script));
        UNSCOPED_INFO("an item's notecard");
        CHECK(unit->canLink(&card));

        script.loaded = false;
        UNSCOPED_INFO("not while it loads");
        CHECK_FALSE(unit->canLink(&script));
        script.loaded     = true;
        script.modifiable = false;
        UNSCOPED_INFO("not where it may not be changed");
        CHECK_FALSE(unit->canLink(&script));
        script.modifiable = true;
        const ALScriptRef ref = script.ref;
        script.ref            = ALScriptRef();
        UNSCOPED_INFO("not with no item");
        CHECK_FALSE(unit->canLink(&script));
        script.ref = ref;

        studio->openMasterFile(in("a.lsl"), false);
        UNSCOPED_INFO("not a file's tab");
        CHECK_FALSE(unit->canLink(services.findDoc("disk:" + in("a.lsl"))));

        write(in("door.lsl"), "default {}\n");
        linkFromElsewhere(script, in("door.lsl"));
        UNSCOPED_INFO("not once it is linked");
        CHECK_FALSE(unit->canLink(&script));
        UNSCOPED_INFO("another file's tab masters nothing");
        CHECK_FALSE(unit->mastersAny(services.findDoc("disk:" + in("a.lsl"))));
        studio->openMasterFile(in("door.lsl"), false);
        UNSCOPED_INFO("the master's tab masters it");
        CHECK(unit->mastersAny(services.findDoc("disk:" + in("door.lsl"))));
    }

    // A notecard carrying items is not linked, and says why, before any file is picked; a script,
    // or a notecard carrying none, carries nothing.
    TEST_CASE_METHOD(alscriptstudiomasters_data, "alscriptstudiomasters: a notecard carrying items is refused with a reason before any pick", "[alscriptstudiomasters]")
    {
        make();
        Doc& script = itemTab("door", "default {}\n");
        Doc& empty  = itemTab("empty", "notes\n", false, true);
        UNSCOPED_INFO("a script carries nothing");
        CHECK_FALSE(unit->carriesItems(script));
        empty.items = std::make_shared<ALNotecardEmbedded>(*empty.editor, ALNotecardEmbedded::Holder(), world);
        UNSCOPED_INFO("a notecard with none carries nothing");
        CHECK_FALSE(unit->carriesItems(empty));
        UNSCOPED_INFO("and says nothing");
        REQUIRE(services.reports.empty());

        Doc& card  = itemTab("card", "a b\n", false, true);
        card.items = std::make_shared<ALNotecardEmbedded>(*card.editor, ALNotecardEmbedded::Holder(), world);
        LLPointer<LLInventoryItem> landmark = new LLInventoryItem();
        card.items->loaded({ landmark });
        UNSCOPED_INFO("one with an item carries it");
        CHECK(unit->carriesItems(card));
        {
            INFO("said, on its tab, as a failure");
            CHECK(says(said().text, "MasterNotecardCarries"));
            CHECK(said().failure);
            CHECK(said().doc == "card");
        }

        studio->picked = in("card.txt");
        write(in("card.txt"), "a b\n");
        unit->linkToFile(card);
        UNSCOPED_INFO("no file asked for");
        CHECK(studio->picks == 0);
        UNSCOPED_INFO("nothing linked");
        REQUIRE_FALSE(disk.linkOf(card.ref));
    }

    // A file picked of another language is refused, and said, before anything is linked: LSL, SLua,
    // a notecard's, and a name with no extension.
    TEST_CASE_METHOD(alscriptstudiomasters_data, "alscriptstudiomasters: a file picked of another language is refused and its own is linked", "[alscriptstudiomasters]")
    {
        make();
        Doc& lsl  = itemTab("door", "default {}\n");
        Doc& slua = itemTab("lamp", "print(1)\n", true);
        Doc& card = itemTab("card", "notes\n", false, true);
        // The fixture's own named by it in a lambda, which MSVC asks for.
        const auto refused = [&](Doc& doc, const std::string& name, const std::string& word) {
            alscriptstudiomasters_data::write(in(name), doc.editor->wholeText());
            studio->picked      = in(name);
            const size_t before = services.reports.size();
            unit->linkToFile(doc);
            // Read straight, not through said(): the count checked first guards it, and an assertion in
            // here would clear the message of the CHECK this is called in.
            if (disk.linkOf(doc.ref) || services.reports.size() != before + 1)
            {
                return false;
            }
            const al_studio_test::FakeServices::Said& last = services.reports.back();
            return alscriptstudiomasters_data::says(last.text, word) && last.failure && last.doc == doc.id && services.findDoc(doc.id) == &doc;
        };
        UNSCOPED_INFO("SLua for an LSL script");
        CHECK(refused(lsl, "door.luau", "MasterNotLSLFile"));
        UNSCOPED_INFO("a notecard's file for an LSL script");
        CHECK(refused(lsl, "door.txt", "MasterNotLSLFile"));
        UNSCOPED_INFO("no extension at all");
        CHECK(refused(lsl, "door", "MasterNotLSLFile"));
        UNSCOPED_INFO("LSL for an SLua script");
        CHECK(refused(slua, "lamp.lsl", "MasterNotSLuaFile"));
        UNSCOPED_INFO("a script's file for a notecard");
        CHECK(refused(card, "card.lsl", "MasterNotNotecardFile"));
        UNSCOPED_INFO("nothing closed");
        REQUIRE(studio->closed.empty());

        // Each of its own language: linked, its tab giving way.
        const ALScriptRef lsl_ref = lsl.ref, slua_ref = slua.ref, card_ref = card.ref;
        studio->picked            = in("door.lsl");
        write(in("door.lsl"), "default {}\n");
        unit->linkToFile(lsl);
        studio->picked = in("lamp.lua");
        write(in("lamp.lua"), "print(1)\n");
        unit->linkToFile(slua);
        studio->picked = in("card.notecard");
        write(in("card.notecard"), "notes\n");
        unit->linkToFile(card);
        const std::optional<ALMasterLink> a = disk.linkOf(lsl_ref);
        const std::optional<ALMasterLink> b = disk.linkOf(slua_ref);
        const std::optional<ALMasterLink> c = disk.linkOf(card_ref);
        {
            INFO("LSL");
            REQUIRE(a);
            REQUIRE(a->master == in("door.lsl"));
            REQUIRE_FALSE(a->lua);
            REQUIRE_FALSE(a->notecard);
            CHECK(a->target == "mono");
        }
        {
            INFO("SLua, by .lua too");
            REQUIRE(b);
            REQUIRE(b->lua);
            CHECK(b->target == "luau");
        }
        {
            INFO("a notecard: no language, no target");
            REQUIRE(c);
            REQUIRE(c->notecard);
            REQUIRE_FALSE(c->lua);
            REQUIRE(c->target.empty());
        }
    }

    // A clean tab linked gives way to its file's tab; where the file is what it holds the file's
    // stamp is taken, else Send File and Compare are offered there.
    TEST_CASE_METHOD(alscriptstudiomasters_data, "alscriptstudiomasters: a linked clean tab gives way and takes the stamp of a matching file", "[alscriptstudiomasters]")
    {
        make();
        Doc& same = itemTab("door", "default {}\n");
        const LLUUID      asset = same.assetId;
        const ALScriptRef ref   = same.ref;
        write(in("door.lsl"), "default {}\n");
        studio->picked = in("door.lsl");
        unit->linkToFile(same);
        REQUIRE(studio->asked.empty());
        const std::optional<ALMasterLink> link = disk.linkOf(ref);
        {
            INFO("linked as picked, from the world's asset");
            REQUIRE(link);
            REQUIRE(link->made == ALMasterLink::Made::Picked);
            REQUIRE(link->base == asset);
            REQUIRE(link->itemName == "door");
            CHECK(link->objectName == "Door");
        }
        {
            INFO("the file's stamp taken, as from a send of it");
            REQUIRE(link->stamp != 0);
            CHECK(link->stamp == ALFileStamp::of(in("door.lsl")).time);
        }
        {
            INFO("its tab closed, the file's opened in its place");
            REQUIRE(studio->closed == Names{ "door" });
            CHECK(studio->opened == Names{ in("door.lsl") });
            REQUIRE_FALSE(services.findDoc(ref));
        }
        Doc* tab = services.findDoc("disk:" + in("door.lsl"));
        {
            INFO("said there, with nothing offered");
            REQUIRE(tab);
            CHECK(says(said().text, "MasterLinked"));
            CHECK_FALSE(says(said().text, "MasterLinkDiffers"));
            CHECK(said().doc == tab->id);
            REQUIRE(said().actions.empty());
        }
        UNSCOPED_INFO("the file's tab acts on the script");
        CHECK(tab->master->offerFor == ref);
        {
            INFO("nothing written or sent");
            REQUIRE(disk.written.empty());
            REQUIRE(disk.sent.empty());
        }

        Doc& other = itemTab("lamp", "default { state_entry() {} }\n");
        write(in("lamp.lsl"), "default {}\n");
        studio->picked = in("lamp.lsl");
        const ALScriptRef other_ref = other.ref;
        unit->linkToFile(other);
        const std::optional<ALMasterLink> differing = disk.linkOf(other_ref);
        {
            INFO("a file it is not what the script holds: no stamp");
            REQUIRE(differing);
            CHECK(differing->stamp == 0);
        }
        Doc* lamp = services.findDoc("disk:" + in("lamp.lsl"));
        {
            INFO("a file it is not: said, Send File and Compare offered on the file's tab");
            REQUIRE(lamp);
            CHECK(says(said().text, "MasterLinkDiffers"));
            CHECK(said().doc == lamp->id);
            CHECK(said().actions == Names{ "master_send", "master_compare" });
        }
    }

    // A tab with unsaved changes linked is asked first: Cancel links nothing, Discard holds the
    // file up to the text as last saved, Write puts what was typed in the file.
    TEST_CASE_METHOD(alscriptstudiomasters_data, "alscriptstudiomasters: linking a tab with unsaved changes asks to cancel or discard or write", "[alscriptstudiomasters]")
    {
        make();
        // Cancel: nothing.
        Doc& door = itemTab("door", "default {}\n");
        type(door, "// mine\n");
        write(in("door.lsl"), "default {}\n");
        studio->picked = in("door.lsl");
        studio->answer = Unsaved::Cancel;
        unit->linkToFile(door);
        UNSCOPED_INFO("asked, the file named");
        CHECK(studio->asked == Names{ "door " + in("door.lsl") });
        {
            INFO("Cancel: not linked, the tab kept with what was typed, the file as it was");
            REQUIRE_FALSE(disk.linkOf(door.ref));
            REQUIRE(services.findDoc("door") == &door);
            CHECK(door.editor->isDirty());
            CHECK(contents(in("door.lsl")) == "default {}\n");
            REQUIRE(studio->closed.empty());
        }

        // Discard, the file what the world holds: as a clean tab is linked,
        // whatever was typed.
        const ALScriptRef door_ref = door.ref;
        studio->answer             = Unsaved::Discard;
        unit->linkToFile(door);
        std::optional<ALMasterLink> link = disk.linkOf(door_ref);
        {
            INFO("Discard: linked, the tab gone");
            REQUIRE(link);
            CHECK(studio->closed == Names{ "door" });
        }
        {
            INFO("the file is what the tab last saved: its stamp taken, nothing offered");
            CHECK(link->stamp == ALFileStamp::of(in("door.lsl")).time);
            CHECK_FALSE(says(said().text, "MasterLinkDiffers"));
            REQUIRE(said().actions.empty());
        }
        {
            INFO("the file untouched");
            CHECK(contents(in("door.lsl")) == "default {}\n");
            REQUIRE(disk.written.empty());
        }

        // Discard, the file what was typed and is let go of: not what the
        // world holds.
        Doc& lamp = itemTab("lamp", "default {}\n");
        type(lamp, "// mine\n");
        write(in("lamp.lsl"), "default {}\n// mine\n");
        studio->picked = in("lamp.lsl");
        const ALScriptRef lamp_ref = lamp.ref;
        unit->linkToFile(lamp);
        link = disk.linkOf(lamp_ref);
        {
            INFO("the file held up to the saved text, not to what was dropped: it differs, no stamp, Send File and Compare offered");
            REQUIRE(link);
            CHECK(link->stamp == 0);
            CHECK(says(said().text, "MasterLinkDiffers"));
            CHECK(said().actions == Names{ "master_send", "master_compare" });
        }

        // Write: what was typed in the file first, which the world does not
        // hold.
        Doc& bell = itemTab("bell", "default {}\n");
        type(bell, "// mine\n");
        write(in("bell.lsl"), "default {}\n");
        studio->picked = in("bell.lsl");
        studio->answer = Unsaved::Write;
        const ALScriptRef bell_ref = bell.ref;
        unit->linkToFile(bell);
        link = disk.linkOf(bell_ref);
        UNSCOPED_INFO("Write: the file holds what was typed");
        CHECK(contents(in("bell.lsl")) == std::string("default {}\n// mine\n"));
        UNSCOPED_INFO("the write the studio's own");
        CHECK(disk.written == Names{ in("bell.lsl") });
        {
            INFO("linked, differing from the world: no stamp, Send File and Compare offered");
            REQUIRE(link);
            CHECK(link->stamp == 0);
            CHECK(says(said().text, "MasterLinkDiffers"));
            CHECK(said().actions == Names{ "master_send", "master_compare" });
        }
        UNSCOPED_INFO("its tab gone");
        REQUIRE_FALSE(services.findDoc(bell_ref));
        Doc* tab = services.findDoc("disk:" + in("bell.lsl"));
        {
            INFO("the file's tab holds what was typed");
            REQUIRE(tab);
            CHECK(tab->editor->wholeText() == "default {}\n// mine\n");
        }
    }

    // Linked from elsewhere: a clean tab gives way to its file's once the links have changed; one
    // with something typed is kept and told once, with Compare with File, Open File and Unlink.
    TEST_CASE_METHOD(alscriptstudiomasters_data, "alscriptstudiomasters: a link from elsewhere closes a clean tab and tells a typed one once", "[alscriptstudiomasters]")
    {
        make();
        Doc& clean = itemTab("door", "default {}\n");
        Doc& dirty = itemTab("lamp", "default {}\n");
        type(dirty, "// mine\n");
        const ALScriptRef clean_ref = clean.ref;
        write(in("door.lsl"), "default {}\n");
        write(in("lamp.lsl"), "default {}\n");
        linkFromElsewhere(clean, in("door.lsl"));
        linkFromElsewhere(dirty, in("lamp.lsl"));
        {
            INFO("nothing done while whoever changed the links is");
            REQUIRE(studio->closed.empty());
            REQUIRE(services.reports.empty());
        }
        settle();
        {
            INFO("the clean one gave way to its file's");
            CHECK(studio->closed == Names{ "door" });
            REQUIRE_FALSE(services.findDoc(clean_ref));
            REQUIRE(services.findDoc("disk:" + in("door.lsl")));
        }
        const auto told = [&](const std::string& id) {
            return std::count_if(services.reports.begin(), services.reports.end(), [&](const al_studio_test::FakeServices::Said& one) {
                return one.doc == id && alscriptstudiomasters_data::says(one.text, "MasterLinkedUnsaved");
            });
        };
        UNSCOPED_INFO("the one with something typed told once");
        REQUIRE(told("lamp") == 1);
        const al_studio_test::FakeServices::Said& lamp = *std::find_if(services.reports.begin(), services.reports.end(),
                                                                         [](const al_studio_test::FakeServices::Said& one) { return one.doc == "lamp"; });
        {
            INFO("as a failure, with its file to compare with and to open, and the link to let go of");
            CHECK(lamp.failure);
            CHECK(lamp.actions == Names{ "master_compare_file", "master_open_file", "master_unlink" });
        }
        {
            INFO("and kept");
            REQUIRE(services.findDoc("lamp") == &dirty);
            CHECK(dirty.editor->isDirty());
        }

        // The links changing again: not told again.
        Doc& bell = itemTab("bell", "default {}\n");
        write(in("bell.lsl"), "default {}\n");
        linkFromElsewhere(bell, in("bell.lsl"));
        settle();
        UNSCOPED_INFO("told once only");
        CHECK(told("lamp") == 1);
        UNSCOPED_INFO("the next clean one gave way");
        REQUIRE_FALSE(services.findDoc("bell"));

        // A tab of a file gone, or of a link held, is left as it is.
        Doc& gone = itemTab("gone", "default {}\n");
        linkFromElsewhere(gone, in("nowhere.lsl"));
        settle();
        UNSCOPED_INFO("a file gone: left");
        CHECK(services.findDoc("gone") == &gone);
    }

    // A tab kept for what was typed gives way once that is saved -- its file's tab offering Send
    // File and Compare where the world now differs -- or reverted.
    TEST_CASE_METHOD(alscriptstudiomasters_data, "alscriptstudiomasters: a tab kept for what was typed gives way once saved or reverted", "[alscriptstudiomasters]")
    {
        make();
        Doc& door = itemTab("door", "default {}\n");
        type(door, "// mine\n");
        write(in("door.lsl"), "default {}\n");
        const ALScriptRef door_ref = door.ref;
        ALMasterLink      link     = linkFromElsewhere(door, in("door.lsl"));
        settle();
        UNSCOPED_INFO("the tab with something typed is kept");
        CHECK(services.findDoc("door") == &door);

        // Saved from here: the world holds what was typed, and the save
        // heard marks the link differing.
        door.editor->resetDirty();
        link.state = ALMasterLink::State::Differing;
        disk.link(link);
        UNSCOPED_INFO("not while the save is still being heard");
        CHECK(services.findDoc("door") == &door);
        settle();
        {
            INFO("clean: it gave way, whatever it was told");
            REQUIRE_FALSE(services.findDoc(door_ref));
            CHECK(studio->closed == Names{ "door" });
        }
        Doc* tab = services.findDoc("disk:" + in("door.lsl"));
        {
            INFO("its file's tab says so, offering to send the file over the world's or to compare them");
            REQUIRE(tab);
            REQUIRE(said().doc == tab->id);
            CHECK(says(said().text, "MasterGaveWay"));
            CHECK(says(said().text, "MasterLinkDiffers"));
            CHECK(said().actions == Names{ "master_send", "master_compare" });
            CHECK(tab->master->offerFor == door_ref);
        }

        // Saved with what the file last sent: the links do not change, and
        // the save heard looks again itself.
        Doc& lamp = itemTab("lamp", "default {}\n");
        type(lamp, "// mine\n");
        write(in("lamp.lsl"), "default {}\n// mine\n");
        const ALMasterLink lamp_link = linkFromElsewhere(lamp, in("lamp.lsl"));
        settle();
        lamp.editor->resetDirty();
        ALScriptSaved saved;
        saved.ref = lamp.ref;
        unit->saved(saved);
        settle();
        UNSCOPED_INFO("gave way on the save alone");
        REQUIRE_FALSE(services.findDoc(ALScriptRef(lamp_link.object, lamp_link.item)));
        {
            INFO("nothing offered: the world holds what the file does");
            CHECK(says(said().text, "MasterGaveWay"));
            REQUIRE(said().actions.empty());
        }

        // A save of a script with no tab here, or not linked: nothing.
        saved.ref = ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID());
        const size_t before = services.reports.size();
        unit->saved(saved);
        settle();
        REQUIRE(services.reports.size() == before);

        // Reverted: loaded again, clean.
        Doc& bell = itemTab("bell", "default {}\n");
        type(bell, "// mine\n");
        write(in("bell.lsl"), "default {}\n");
        const ALMasterLink bell_link = linkFromElsewhere(bell, in("bell.lsl"));
        settle();
        bell.editor->setText("default {}\n");
        unit->loaded(bell);
        settle();
        {
            INFO("reverted: gave way");
            REQUIRE_FALSE(services.findDoc(ALScriptRef(bell_link.object, bell_link.item)));
            CHECK(says(said().text, "MasterGaveWay"));
        }
    }

    // The offers on a tab kept for what was typed: its file on disk set beside it, its file's tab
    // opened beside it, the link let go of.
    TEST_CASE_METHOD(alscriptstudiomasters_data, "alscriptstudiomasters: a kept tab's offers compare with its file or open it or unlink it", "[alscriptstudiomasters]")
    {
        make();
        Doc& door = itemTab("door", "default {}\n");
        type(door, "// mine\n");
        write(in("door.lsl"), "default { touch_start(integer n) {} }\n");
        linkFromElsewhere(door, in("door.lsl"));
        settle();

        unit->offer(door, "master_compare_file");
        REQUIRE(studio->compared.size() == size_t(1));
        const FakeMastersWindow::Compared& compared = studio->compared.back();
        {
            INFO("in the tab's place: the file on disk, then what was typed");
            CHECK(compared.doc == "door");
            CHECK(compared.left == "default { touch_start(integer n) {} }\n");
            CHECK(compared.right == "default {}\n// mine\n");
        }
        {
            INFO("each named");
            CHECK(says(compared.leftTitle, "CompareMasterOnDisk"));
            CHECK(says(compared.leftTitle, "door.lsl"));
            CHECK(says(compared.rightTitle, "CompareNow"));
        }

        unit->offer(door, "master_open_file");
        Doc* tab = services.findDoc("disk:" + in("door.lsl"));
        {
            INFO("the file's tab opened, this one kept");
            REQUIRE(tab);
            CHECK(services.findDoc("door") == &door);
            REQUIRE(studio->closed.empty());
        }
        UNSCOPED_INFO("the file's tab acts on the script");
        CHECK(tab->master->offerFor == door.ref);

        std::filesystem::remove(fsyspath(in("door.lsl")));
        unit->offer(door, "master_compare_file");
        {
            INFO("a file that cannot be read: said, nothing compared");
            REQUIRE(studio->compared.size() == 1);
            CHECK(says(said().text, "MasterNotRead"));
            CHECK(said().failure);
        }

        // Not offered on a file's own tab.
        unit->offer(*tab, "master_compare_file");
        unit->offer(*tab, "master_open_file");
        {
            INFO("nothing from a file's tab");
            REQUIRE(studio->compared.size() == 1);
            REQUIRE(studio->opened.size() == 1);
        }

        unit->offer(door, "master_unlink");
        {
            INFO("let go of, and said");
            REQUIRE_FALSE(disk.linkOf(door.ref));
            CHECK(says(said().text, "MasterUnlinked"));
        }
        unit->offer(door, "master_compare_file");
        UNSCOPED_INFO("nothing to compare once unlinked");
        REQUIRE(studio->compared.size() == 1);
    }

    // The file a script names is linked by Link while nothing is typed in its tab; with something
    // typed, it is to be saved or reverted first, and nothing is written.
    TEST_CASE_METHOD(alscriptstudiomasters_data, "alscriptstudiomasters: the Link offer links a script's named file only while nothing is typed", "[alscriptstudiomasters]")
    {
        make();
        Doc& door = itemTab("door", "default {}\n");
        write(in("door.lsl"), "default {}\n");
        door.master->hinted        = in("door.lsl");
        const ALScriptRef door_ref = door.ref;
        unit->offer(door, "master_link_hint");
        const std::optional<ALMasterLink> link = disk.linkOf(door_ref);
        {
            INFO("clean: linked, as named");
            REQUIRE(link);
            REQUIRE(link->made == ALMasterLink::Made::Hint);
            CHECK(link->master == in("door.lsl"));
        }

        Doc& lamp = itemTab("lamp", "default {}\n");
        type(lamp, "// mine\n");
        write(in("lamp.lsl"), "default {}\n");
        lamp.master->hinted = in("lamp.lsl");
        unit->offer(lamp, "master_link_hint");
        {
            INFO("with something typed: not linked, nothing asked, the tab kept");
            REQUIRE_FALSE(disk.linkOf(lamp.ref));
            REQUIRE(studio->asked.empty());
            REQUIRE(services.findDoc("lamp") == &lamp);
            CHECK(lamp.editor->isDirty());
        }
        {
            INFO("said, Link offered again for after");
            CHECK(says(said().text, "MasterHintUnsaved"));
            CHECK(said().failure);
            CHECK(said().doc == "lamp");
            CHECK(said().actions == Names{ "master_link_hint" });
        }
        {
            INFO("nothing written");
            CHECK(contents(in("lamp.lsl")) == "default {}\n");
            REQUIRE(disk.written.empty());
        }

        const ALScriptRef lamp_ref = lamp.ref;
        lamp.editor->resetDirty();
        unit->offer(lamp, "master_link_hint");
        {
            INFO("saved: linked, its tab giving way");
            REQUIRE(disk.linkOf(lamp_ref));
            REQUIRE_FALSE(services.findDoc(lamp_ref));
        }
    }

    // A tab kept for what was typed, made clean by an undo, is kept and offered to switch to
    // its file, and told again when typed in; Switch to File gives way, as a revert or a save
    // of it does.
    TEST_CASE_METHOD(alscriptstudiomasters_data, "alscriptstudiomasters: a kept tab made clean by an undo is offered Switch to File", "[alscriptstudiomasters]")
    {
        make();
        Doc& door = itemTab("door", "default {}\n");
        wire(door);
        type(door, "// mine\n");
        write(in("door.lsl"), "default {}\n");
        const ALScriptRef door_ref = door.ref;
        linkFromElsewhere(door, in("door.lsl"));
        settle();
        {
            INFO("kept and told");
            CHECK(services.findDoc(door_ref) == &door);
            CHECK(says(said().text, "MasterLinkedUnsaved"));
            CHECK(said().doc == "door");
        }
        const size_t said_before = services.reports.size();
        const S32    notices     = studio->notices;

        door.editor->undo();
        UNSCOPED_INFO("clean again by the undo");
        CHECK_FALSE(door.editor->isDirty());
        {
            INFO("not closed under the author");
            CHECK(services.findDoc(door_ref) == &door);
            REQUIRE(studio->closed.empty());
        }
        {
            INFO("its notice: nothing unsaved left, Switch to File offered with the rest");
            REQUIRE(door.offer);
            REQUIRE(says(door.offer->text, "MasterLinkedClean"));
            CHECK(door.offer->actions == Names{ "master_give_way", "master_compare_file", "master_open_file", "master_unlink" });
            CHECK(studio->notices == notices + 1);
        }
        UNSCOPED_INFO("over the tab alone, not in Output");
        REQUIRE(services.reports.size() == said_before);

        // The links changing meanwhile: kept still.
        Doc& bell = itemTab("bell", "default {}\n");
        write(in("bell.lsl"), "default {}\n");
        linkFromElsewhere(bell, in("bell.lsl"));
        settle();
        {
            INFO("kept as the links change");
            CHECK(services.findDoc(door_ref) == &door);
            REQUIRE_FALSE(services.findDoc("bell"));
        }

        door.editor->redo();
        {
            INFO("typed in again by the redo: the notice back to what was typed");
            REQUIRE(door.editor->isDirty());
            REQUIRE(door.offer);
            REQUIRE(says(door.offer->text, "MasterLinkedUnsaved"));
            CHECK(door.offer->actions == Names{ "master_compare_file", "master_open_file", "master_unlink" });
        }
        door.editor->undo();
        {
            INFO("and clean again");
            REQUIRE(door.offer);
            CHECK(says(door.offer->text, "MasterLinkedClean"));
        }

        // Switch to File taken once typed in again: kept, and told so.
        type(door, "x");
        door.offer.reset();
        unit->offer(door, "master_give_way");
        {
            INFO("typed in since it was offered: kept, told so");
            REQUIRE(services.findDoc(door_ref) == &door);
            REQUIRE(door.offer);
            CHECK(says(door.offer->text, "MasterLinkedUnsaved"));
        }
        door.editor->undo();
        {
            INFO("clean again");
            REQUIRE(door.offer);
            CHECK(says(door.offer->text, "MasterLinkedClean"));
        }
        unit->offer(door, "master_give_way");
        {
            INFO("Switch to File: the file's tab in its place, said there");
            REQUIRE_FALSE(services.findDoc(door_ref));
            REQUIRE_FALSE(studio->closed.empty());
            CHECK(studio->closed.back() == "door");
            CHECK(says(said().text, "MasterGaveWay"));
            CHECK(said().doc == "disk:" + in("door.lsl"));
        }

        // Reverted once clean: gives way, as a revert does.
        Doc& lamp = itemTab("lamp", "default {}\n");
        wire(lamp);
        type(lamp, "// mine\n");
        write(in("lamp.lsl"), "default {}\n");
        const ALScriptRef lamp_ref = lamp.ref;
        linkFromElsewhere(lamp, in("lamp.lsl"));
        settle();
        lamp.editor->undo();
        {
            INFO("kept once clean");
            REQUIRE(services.findDoc(lamp_ref) == &lamp);
            REQUIRE(lamp.offer);
            CHECK(says(lamp.offer->text, "MasterLinkedClean"));
        }
        lamp.editor->setText("default {}\n");
        unit->loaded(lamp);
        settle();
        {
            INFO("reverted: gave way");
            REQUIRE_FALSE(services.findDoc(lamp_ref));
            CHECK(says(said().text, "MasterGaveWay"));
        }

        // Saved once clean: gives way, as a save does.
        Doc& gate = itemTab("gate", "default {}\n");
        wire(gate);
        type(gate, "// mine\n");
        write(in("gate.lsl"), "default {}\n");
        const ALScriptRef gate_ref = gate.ref;
        linkFromElsewhere(gate, in("gate.lsl"));
        settle();
        gate.editor->undo();
        UNSCOPED_INFO("kept once clean");
        CHECK(services.findDoc(gate_ref) == &gate);
        ALScriptSaved saved;
        saved.ref = gate_ref;
        unit->saved(saved);
        settle();
        {
            INFO("saved: gave way");
            REQUIRE_FALSE(services.findDoc(gate_ref));
            CHECK(says(said().text, "MasterGaveWay"));
        }
    }
}
