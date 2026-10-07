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
#include "../alscriptmasteradopt.h"
#include "../alscriptmasterfanout.h"
#include "../alscriptmastertoasts.h"
#include "../alscriptmasterwatch.h"

#include "alfilewrite.h"
#include "alscriptstudio_fixture.h"
#include "alserialworker.h"
#include "alwatchedfile.h"
#include "fsyspath.h"
#include "llfile.h"
#include "workqueue.h"

#include "../test/lltut.h"

#include <filesystem>

// --- the account's links, held in memory -------------------------------------
//
// ALScriptDiskMasters is the viewer's: its index is a file under the
// account's folder, and it sends through the workspace, the preprocessor and
// the region, watches files and says things in toasts, none of which a test
// can link. What the masters unit asks of it is defined here instead: the
// links themselves, kept in the real records (ALMasterLinks) and told as
// changed as the viewer's are, with nothing written, watched or sent -- a
// write and a send are only noted.

namespace
{
    // What the masters asked of the index beyond its links.
    struct IndexAsked
    {
        std::vector<std::string>                                 wrote;
        std::vector<std::pair<ALScriptRef, ALMasterPlan::Send>> sent;
    };
    IndexAsked gIndexAsked;
}

ALScriptDiskMasters::ALScriptDiskMasters() {}
ALScriptDiskMasters::~ALScriptDiskMasters() = default;
// Nothing written, so nothing waits to be as the links go.
void ALScriptDiskMasters::cleanupSingleton() {}
ALScriptMasterWatch::~ALScriptMasterWatch() = default;

std::optional<ALMasterLink> ALScriptDiskMasters::linkOf(const ALScriptRef& ref)
{
    const ALMasterLink* link = mLinks.of(ref.object, ref.item);
    return link ? std::optional<ALMasterLink>(*link) : std::nullopt;
}

std::vector<ALMasterLink> ALScriptDiskMasters::mastering(const std::string& master)
{
    std::vector<ALMasterLink> out;
    for (const ALMasterLink* link : mLinks.mastering(master))
    {
        out.push_back(*link);
    }
    return out;
}

void ALScriptDiskMasters::link(ALMasterLink link)
{
    std::vector<ALMasterLink> made;
    made.push_back(std::move(link));
    this->link(std::move(made));
}

void ALScriptDiskMasters::link(std::vector<ALMasterLink> made)
{
    if (made.empty())
    {
        return;
    }
    for (ALMasterLink& one : made)
    {
        mLinks.put(std::move(one));
    }
    mChanged();
}

void ALScriptDiskMasters::unlink(const ALScriptRef& ref)
{
    if (mLinks.remove(ref.object, ref.item))
    {
        mChanged();
    }
}

void ALScriptDiskMasters::wrote(const std::string& path)
{
    gIndexAsked.wrote.push_back(path);
}

void ALScriptDiskMasters::send(const ALScriptRef& ref, ALMasterPlan::Send kind)
{
    gIndexAsked.sent.emplace_back(ref, kind);
}

std::vector<ALScriptDiskMasters::Outcome> ALScriptDiskMasters::takeUnheard()
{
    std::vector<Outcome> out;
    out.swap(mUnheard);
    return out;
}

// static
ALDiskIncludes ALScriptDiskMasters::blessedFor(const std::string&, bool)
{
    return ALDiskIncludes();
}

// static
std::vector<std::pair<std::string, std::string>> ALScriptDiskMasters::aliasesFor(const std::string&, bool)
{
    return {};
}

namespace
{
    typedef ALScriptStudioDoc              Doc;
    typedef ALScriptStudioMasters::Unsaved Unsaved;

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
        S32                                                 picks = 0;
        S32                                                 made  = 0;
        std::vector<std::string>                            opened, closed, asked, edited;
        std::vector<Compared>                               compared;
        std::vector<ALScriptRef>                            loads;
        std::function<void(const ALScriptLoaded& loaded)> toLoad;
    };
}

namespace tut
{
    struct alscriptstudiomasters_data
    {
        al_studio_test::StudioWindow           window;
        al_studio_test::FakeServices           services;
        al_studio_test::QuietAnalysis          analysis;
        std::unique_ptr<FakeMastersWindow>     studio;
        std::unique_ptr<ALScriptStudioMasters> unit;
        NoWorld                                world;
        std::string                            folder;

        ~alscriptstudiomasters_data()
        {
            unit.reset();
            services.docs.clear();
            // The links let go of with the test, for the next to start from
            // none.
            ALScriptDiskMasters::deleteSingleton();
            if (!folder.empty())
            {
                std::error_code ignored;
                std::filesystem::remove_all(fsyspath(folder), ignored);
            }
        }

        // The main loop's queue, as the viewer's is, which a look at the
        // tabs waits for: kept for the rest of the run, since what was posted
        // to it for a test gone finds nobody.
        static LL::WorkQueue& mainLoop()
        {
            static LL::WorkQueue queue("mainloop", 1024);
            return queue;
        }
        // Whatever was waiting for the main loop, done.
        static void settle() { mainLoop().runPending(); }

        ALScriptStudioMasters& make()
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            mainLoop();
            gIndexAsked         = IndexAsked();
            const fsyspath made = std::filesystem::temp_directory_path() / fsyspath("alscriptstudiomasters_" + LLUUID::generateNewID().asString());
            std::filesystem::create_directories(made);
            folder = made.string();
            studio = std::make_unique<FakeMastersWindow>(services, *window.floater);
            unit   = std::make_unique<ALScriptStudioMasters>(services, analysis, *studio);
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
        // Something typed at the end of a tab's text.
        static void type(Doc& doc, const std::string& text)
        {
            doc.editor->setCaret(doc.editor->document().end());
            doc.editor->insertText(text);
        }
        // A link made from elsewhere -- another window, the Link tab -- of a
        // tab's script to a file.
        static ALMasterLink linkFromElsewhere(const Doc& doc, const std::string& path)
        {
            ALMasterLink link;
            link.object   = doc.ref.object;
            link.item     = doc.ref.item;
            link.master   = path;
            link.itemName = doc.name;
            ALScriptDiskMasters::instance().link(link);
            return link;
        }

        const al_studio_test::FakeServices::Said& said() const { return services.reports.back(); }
        static bool says(const std::string& text, const std::string& word) { return text.find(word) != std::string::npos; }
        typedef std::vector<std::string> Names;
    };

    typedef test_group<alscriptstudiomasters_data> alscriptstudiomasters_group;
    typedef alscriptstudiomasters_group::object    alscriptstudiomasters_object;
    alscriptstudiomasters_group                    alscriptstudiomasters_instance("alscriptstudiomasters");

    template<> template<>
    void alscriptstudiomasters_object::test<1>()
    {
        set_test_name("a tab may be linked where it is an item's, loaded, may be changed and is linked to nothing yet");
        make();
        Doc& script = itemTab("door", "default {}\n");
        Doc& card   = itemTab("card", "notes\n", false, true);
        ensure("no tab", !ALScriptStudioMasters::canLink(nullptr));
        ensure("an item's script", ALScriptStudioMasters::canLink(&script));
        ensure("an item's notecard", ALScriptStudioMasters::canLink(&card));

        script.loaded = false;
        ensure("not while it loads", !ALScriptStudioMasters::canLink(&script));
        script.loaded     = true;
        script.modifiable = false;
        ensure("not where it may not be changed", !ALScriptStudioMasters::canLink(&script));
        script.modifiable = true;
        const ALScriptRef ref = script.ref;
        script.ref            = ALScriptRef();
        ensure("not with no item", !ALScriptStudioMasters::canLink(&script));
        script.ref = ref;

        studio->openMasterFile(in("a.lsl"), false);
        ensure("not a file's tab", !ALScriptStudioMasters::canLink(services.findDoc("disk:" + in("a.lsl"))));

        write(in("door.lsl"), "default {}\n");
        linkFromElsewhere(script, in("door.lsl"));
        ensure("not once it is linked", !ALScriptStudioMasters::canLink(&script));
        ensure("another file's tab masters nothing", !unit->mastersAny(services.findDoc("disk:" + in("a.lsl"))));
        studio->openMasterFile(in("door.lsl"), false);
        ensure("the master's tab masters it", unit->mastersAny(services.findDoc("disk:" + in("door.lsl"))));
    }

    template<> template<>
    void alscriptstudiomasters_object::test<2>()
    {
        set_test_name("a notecard carrying items is not linked, and says why, before any file is picked; a script, or a notecard carrying none, carries nothing");
        make();
        Doc& script = itemTab("door", "default {}\n");
        Doc& empty  = itemTab("empty", "notes\n", false, true);
        ensure("a script carries nothing", !unit->carriesItems(script));
        empty.items = std::make_shared<ALNotecardEmbedded>(*empty.editor, ALNotecardEmbedded::Holder(), world);
        ensure("a notecard with none carries nothing", !unit->carriesItems(empty));
        ensure("and says nothing", services.reports.empty());

        Doc& card  = itemTab("card", "a b\n", false, true);
        card.items = std::make_shared<ALNotecardEmbedded>(*card.editor, ALNotecardEmbedded::Holder(), world);
        LLPointer<LLInventoryItem> landmark = new LLInventoryItem();
        card.items->loaded({ landmark });
        ensure("one with an item carries it", unit->carriesItems(card));
        ensure("said, on its tab, as a failure", says(said().text, "MasterNotecardCarries") && said().failure && said().doc == "card");

        studio->picked = in("card.txt");
        write(in("card.txt"), "a b\n");
        unit->linkToFile(card);
        ensure_equals("no file asked for", studio->picks, 0);
        ensure("nothing linked", !ALScriptDiskMasters::instance().linkOf(card.ref));
    }

    template<> template<>
    void alscriptstudiomasters_object::test<3>()
    {
        set_test_name("a file picked of another language is refused, and said, before anything is linked: LSL, SLua, a notecard's, and a name with no extension");
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
            return !ALScriptDiskMasters::instance().linkOf(doc.ref) && services.reports.size() == before + 1 &&
                   alscriptstudiomasters_data::says(said().text, word) && said().failure && said().doc == doc.id && services.findDoc(doc.id) == &doc;
        };
        ensure("SLua for an LSL script", refused(lsl, "door.luau", "MasterNotLSLFile"));
        ensure("a notecard's file for an LSL script", refused(lsl, "door.txt", "MasterNotLSLFile"));
        ensure("no extension at all", refused(lsl, "door", "MasterNotLSLFile"));
        ensure("LSL for an SLua script", refused(slua, "lamp.lsl", "MasterNotSLuaFile"));
        ensure("a script's file for a notecard", refused(card, "card.lsl", "MasterNotNotecardFile"));
        ensure("nothing closed", studio->closed.empty());

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
        const std::optional<ALMasterLink> a = ALScriptDiskMasters::instance().linkOf(lsl_ref);
        const std::optional<ALMasterLink> b = ALScriptDiskMasters::instance().linkOf(slua_ref);
        const std::optional<ALMasterLink> c = ALScriptDiskMasters::instance().linkOf(card_ref);
        ensure("LSL", a && a->master == in("door.lsl") && !a->lua && !a->notecard && a->target == "mono");
        ensure("SLua, by .lua too", b && b->lua && b->target == "luau");
        ensure("a notecard: no language, no target", c && c->notecard && !c->lua && c->target.empty());
    }

    template<> template<>
    void alscriptstudiomasters_object::test<4>()
    {
        set_test_name("a clean tab linked: it gives way to its file's tab; where the file is what it holds the file's stamp is taken, else Send File and Compare are offered there");
        make();
        Doc& same = itemTab("door", "default {}\n");
        const LLUUID      asset = same.assetId;
        const ALScriptRef ref   = same.ref;
        write(in("door.lsl"), "default {}\n");
        studio->picked = in("door.lsl");
        unit->linkToFile(same);
        ensure("nothing asked", studio->asked.empty());
        const std::optional<ALMasterLink> link = ALScriptDiskMasters::instance().linkOf(ref);
        ensure("linked as picked, from the world's asset", link && link->made == ALMasterLink::Made::Picked && link->base == asset &&
                                                              link->itemName == "door" && link->objectName == "Door");
        ensure("the file's stamp taken, as from a send of it", link->stamp != 0 && link->stamp == ALFileStamp::of(in("door.lsl")).time);
        ensure("its tab closed, the file's opened in its place", studio->closed == Names{ "door" } && studio->opened == Names{ in("door.lsl") } &&
                                                                     !services.findDoc(ref));
        Doc* tab = services.findDoc("disk:" + in("door.lsl"));
        ensure("said there, with nothing offered", tab && says(said().text, "MasterLinked") && !says(said().text, "MasterLinkDiffers") &&
                                                       said().doc == tab->id && said().actions.empty());
        ensure("the file's tab acts on the script", tab->master->offerFor == ref);
        ensure("nothing written or sent", gIndexAsked.wrote.empty() && gIndexAsked.sent.empty());

        Doc& other = itemTab("lamp", "default { state_entry() {} }\n");
        write(in("lamp.lsl"), "default {}\n");
        studio->picked = in("lamp.lsl");
        const ALScriptRef other_ref = other.ref;
        unit->linkToFile(other);
        const std::optional<ALMasterLink> differing = ALScriptDiskMasters::instance().linkOf(other_ref);
        ensure("a file it is not what the script holds: no stamp", differing && differing->stamp == 0);
        Doc* lamp = services.findDoc("disk:" + in("lamp.lsl"));
        ensure("a file it is not: said, Send File and Compare offered on the file's tab",
               lamp && says(said().text, "MasterLinkDiffers") && said().doc == lamp->id &&
                   said().actions == Names{ "master_send", "master_compare" });
    }

    template<> template<>
    void alscriptstudiomasters_object::test<5>()
    {
        set_test_name("a tab with unsaved changes linked: asked first; Cancel links nothing, Discard holds the file up to the text as last saved, Write puts what was typed in the file");
        make();
        // Cancel: nothing.
        Doc& door = itemTab("door", "default {}\n");
        type(door, "// mine\n");
        write(in("door.lsl"), "default {}\n");
        studio->picked = in("door.lsl");
        studio->answer = Unsaved::Cancel;
        unit->linkToFile(door);
        ensure("asked, the file named", studio->asked == Names{ "door " + in("door.lsl") });
        ensure("Cancel: not linked, the tab kept with what was typed, the file as it was",
               !ALScriptDiskMasters::instance().linkOf(door.ref) && services.findDoc("door") == &door && door.editor->isDirty() &&
                   contents(in("door.lsl")) == "default {}\n" && studio->closed.empty());

        // Discard, the file what the world holds: as a clean tab is linked,
        // whatever was typed.
        const ALScriptRef door_ref = door.ref;
        studio->answer             = Unsaved::Discard;
        unit->linkToFile(door);
        std::optional<ALMasterLink> link = ALScriptDiskMasters::instance().linkOf(door_ref);
        ensure("Discard: linked, the tab gone", link && studio->closed == Names{ "door" });
        ensure("the file is what the tab last saved: its stamp taken, nothing offered",
               link->stamp == ALFileStamp::of(in("door.lsl")).time && !says(said().text, "MasterLinkDiffers") && said().actions.empty());
        ensure("the file untouched", contents(in("door.lsl")) == "default {}\n" && gIndexAsked.wrote.empty());

        // Discard, the file what was typed and is let go of: not what the
        // world holds.
        Doc& lamp = itemTab("lamp", "default {}\n");
        type(lamp, "// mine\n");
        write(in("lamp.lsl"), "default {}\n// mine\n");
        studio->picked = in("lamp.lsl");
        const ALScriptRef lamp_ref = lamp.ref;
        unit->linkToFile(lamp);
        link = ALScriptDiskMasters::instance().linkOf(lamp_ref);
        ensure("the file held up to the saved text, not to what was dropped: it differs, no stamp, Send File and Compare offered",
               link && link->stamp == 0 && says(said().text, "MasterLinkDiffers") && said().actions == Names{ "master_send", "master_compare" });

        // Write: what was typed in the file first, which the world does not
        // hold.
        Doc& bell = itemTab("bell", "default {}\n");
        type(bell, "// mine\n");
        write(in("bell.lsl"), "default {}\n");
        studio->picked = in("bell.lsl");
        studio->answer = Unsaved::Write;
        const ALScriptRef bell_ref = bell.ref;
        unit->linkToFile(bell);
        link = ALScriptDiskMasters::instance().linkOf(bell_ref);
        ensure_equals("Write: the file holds what was typed", contents(in("bell.lsl")), std::string("default {}\n// mine\n"));
        ensure("the write the studio's own", gIndexAsked.wrote == Names{ in("bell.lsl") });
        ensure("linked, differing from the world: no stamp, Send File and Compare offered",
               link && link->stamp == 0 && says(said().text, "MasterLinkDiffers") && said().actions == Names{ "master_send", "master_compare" });
        ensure("its tab gone", !services.findDoc(bell_ref));
        Doc* tab = services.findDoc("disk:" + in("bell.lsl"));
        ensure("the file's tab holds what was typed", tab && tab->editor->wholeText() == "default {}\n// mine\n");
    }

    template<> template<>
    void alscriptstudiomasters_object::test<6>()
    {
        set_test_name("linked from elsewhere: a clean tab gives way to its file's once the links have changed; one with something typed is kept and told once, with Compare with File, Open File and Unlink");
        make();
        Doc& clean = itemTab("door", "default {}\n");
        Doc& dirty = itemTab("lamp", "default {}\n");
        type(dirty, "// mine\n");
        const ALScriptRef clean_ref = clean.ref;
        write(in("door.lsl"), "default {}\n");
        write(in("lamp.lsl"), "default {}\n");
        linkFromElsewhere(clean, in("door.lsl"));
        linkFromElsewhere(dirty, in("lamp.lsl"));
        ensure("nothing done while whoever changed the links is", studio->closed.empty() && services.reports.empty());
        settle();
        ensure("the clean one gave way to its file's", studio->closed == Names{ "door" } && !services.findDoc(clean_ref) &&
                                                         services.findDoc("disk:" + in("door.lsl")));
        const auto told = [&](const std::string& id) {
            return std::count_if(services.reports.begin(), services.reports.end(), [&](const al_studio_test::FakeServices::Said& one) {
                return one.doc == id && alscriptstudiomasters_data::says(one.text, "MasterLinkedUnsaved");
            });
        };
        ensure_equals("the one with something typed told once", told("lamp"), 1);
        const al_studio_test::FakeServices::Said& lamp = *std::find_if(services.reports.begin(), services.reports.end(),
                                                                         [](const al_studio_test::FakeServices::Said& one) { return one.doc == "lamp"; });
        ensure("as a failure, with its file to compare with and to open, and the link to let go of",
               lamp.failure && lamp.actions == Names{ "master_compare_file", "master_open_file", "master_unlink" });
        ensure("and kept", services.findDoc("lamp") == &dirty && dirty.editor->isDirty());

        // The links changing again: not told again.
        Doc& bell = itemTab("bell", "default {}\n");
        write(in("bell.lsl"), "default {}\n");
        linkFromElsewhere(bell, in("bell.lsl"));
        settle();
        ensure_equals("told once only", told("lamp"), 1);
        ensure("the next clean one gave way", !services.findDoc("bell"));

        // A tab of a file gone, or of a link held, is left as it is.
        Doc& gone = itemTab("gone", "default {}\n");
        linkFromElsewhere(gone, in("nowhere.lsl"));
        settle();
        ensure("a file gone: left", services.findDoc("gone") == &gone);
    }

    template<> template<>
    void alscriptstudiomasters_object::test<7>()
    {
        set_test_name("a tab kept for what was typed gives way once that is saved -- its file's tab offering Send File and Compare where the world now differs -- or reverted");
        make();
        Doc& door = itemTab("door", "default {}\n");
        type(door, "// mine\n");
        write(in("door.lsl"), "default {}\n");
        const ALScriptRef door_ref = door.ref;
        ALMasterLink      link     = linkFromElsewhere(door, in("door.lsl"));
        settle();
        ensure("kept", services.findDoc("door") == &door);

        // Saved from here: the world holds what was typed, and the save
        // heard marks the link differing.
        door.editor->resetDirty();
        link.state = ALMasterLink::State::Differing;
        ALScriptDiskMasters::instance().link(link);
        ensure("not while the save is still being heard", services.findDoc("door") == &door);
        settle();
        ensure("clean: it gave way, whatever it was told", !services.findDoc(door_ref) && studio->closed == Names{ "door" });
        Doc* tab = services.findDoc("disk:" + in("door.lsl"));
        ensure("its file's tab says so, offering to send the file over the world's or to compare them",
               tab && said().doc == tab->id && says(said().text, "MasterGaveWay") && says(said().text, "MasterLinkDiffers") &&
                   said().actions == Names{ "master_send", "master_compare" } && tab->master->offerFor == door_ref);

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
        ensure("gave way on the save alone", !services.findDoc(ALScriptRef(lamp_link.object, lamp_link.item)));
        ensure("nothing offered: the world holds what the file does", says(said().text, "MasterGaveWay") && said().actions.empty());

        // A save of a script with no tab here, or not linked: nothing.
        saved.ref = ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID());
        const size_t before = services.reports.size();
        unit->saved(saved);
        settle();
        ensure("nothing", services.reports.size() == before);

        // Reverted: loaded again, clean.
        Doc& bell = itemTab("bell", "default {}\n");
        type(bell, "// mine\n");
        write(in("bell.lsl"), "default {}\n");
        const ALMasterLink bell_link = linkFromElsewhere(bell, in("bell.lsl"));
        settle();
        bell.editor->setText("default {}\n");
        unit->loaded(bell);
        settle();
        ensure("reverted: gave way", !services.findDoc(ALScriptRef(bell_link.object, bell_link.item)) && says(said().text, "MasterGaveWay"));
    }

    template<> template<>
    void alscriptstudiomasters_object::test<8>()
    {
        set_test_name("the offers on a tab kept for what was typed: its file on disk set beside it, its file's tab opened beside it, the link let go of");
        make();
        Doc& door = itemTab("door", "default {}\n");
        type(door, "// mine\n");
        write(in("door.lsl"), "default { touch_start(integer n) {} }\n");
        linkFromElsewhere(door, in("door.lsl"));
        settle();

        unit->offer(door, "master_compare_file");
        ensure_equals("compared once", studio->compared.size(), size_t(1));
        const FakeMastersWindow::Compared& compared = studio->compared.back();
        ensure("in the tab's place: the file on disk, then what was typed", compared.doc == "door" &&
                                                                              compared.left == "default { touch_start(integer n) {} }\n" &&
                                                                              compared.right == "default {}\n// mine\n");
        ensure("each named", says(compared.leftTitle, "CompareMasterOnDisk") && says(compared.leftTitle, "door.lsl") &&
                                 says(compared.rightTitle, "CompareNow"));

        unit->offer(door, "master_open_file");
        Doc* tab = services.findDoc("disk:" + in("door.lsl"));
        ensure("the file's tab opened, this one kept", tab && services.findDoc("door") == &door && studio->closed.empty());
        ensure("the file's tab acts on the script", tab->master->offerFor == door.ref);

        std::filesystem::remove(fsyspath(in("door.lsl")));
        unit->offer(door, "master_compare_file");
        ensure("a file that cannot be read: said, nothing compared", studio->compared.size() == 1 && says(said().text, "MasterNotRead") && said().failure);

        // Not offered on a file's own tab.
        unit->offer(*tab, "master_compare_file");
        unit->offer(*tab, "master_open_file");
        ensure("nothing from a file's tab", studio->compared.size() == 1 && studio->opened.size() == 1);

        unit->offer(door, "master_unlink");
        ensure("let go of, and said", !ALScriptDiskMasters::instance().linkOf(door.ref) && says(said().text, "MasterUnlinked"));
        unit->offer(door, "master_compare_file");
        ensure("nothing to compare once unlinked", studio->compared.size() == 1);
    }

    template<> template<>
    void alscriptstudiomasters_object::test<9>()
    {
        set_test_name("the file a script names is linked by Link while nothing is typed in its tab; with something typed, it is to be saved or reverted first, and nothing is written");
        make();
        Doc& door = itemTab("door", "default {}\n");
        write(in("door.lsl"), "default {}\n");
        door.master->hinted        = in("door.lsl");
        const ALScriptRef door_ref = door.ref;
        unit->offer(door, "master_link_hint");
        const std::optional<ALMasterLink> link = ALScriptDiskMasters::instance().linkOf(door_ref);
        ensure("clean: linked, as named", link && link->made == ALMasterLink::Made::Hint && link->master == in("door.lsl"));

        Doc& lamp = itemTab("lamp", "default {}\n");
        type(lamp, "// mine\n");
        write(in("lamp.lsl"), "default {}\n");
        lamp.master->hinted = in("lamp.lsl");
        unit->offer(lamp, "master_link_hint");
        ensure("with something typed: not linked, nothing asked, the tab kept",
               !ALScriptDiskMasters::instance().linkOf(lamp.ref) && studio->asked.empty() && services.findDoc("lamp") == &lamp &&
                   lamp.editor->isDirty());
        ensure("said, Link offered again for after", says(said().text, "MasterHintUnsaved") && said().failure && said().doc == "lamp" &&
                                                        said().actions == Names{ "master_link_hint" });
        ensure("nothing written", contents(in("lamp.lsl")) == "default {}\n" && gIndexAsked.wrote.empty());

        const ALScriptRef lamp_ref = lamp.ref;
        lamp.editor->resetDirty();
        unit->offer(lamp, "master_link_hint");
        ensure("saved: linked, its tab giving way", ALScriptDiskMasters::instance().linkOf(lamp_ref) && !services.findDoc(lamp_ref));
    }
}
