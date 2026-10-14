/**
 * @file alscriptstudiofiles_test.cpp
 * @brief Script Studio's files on disk: languages, the recent lists, writing, changes outside, loading, Save As and the pickers.
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

#include "../alscriptstudiofiles.h"
#include "../alscriptstudiofileio.h"

#include "alfilewrite.h"
#include "alscriptstudio_fixture.h"
#include "llmenugl.h"

#include "fsyspath.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;
    typedef std::function<void(const std::vector<std::string>&)> Chosen;

    // The window, faked: a record of what the files asked of it.
    struct FakeFilesWindow : public ALScriptStudioFiles::Window, public al_studio_test::QuietTabs, public al_studio_test::QuietAnalysis, public al_studio_test::QuietSaves
    {
        // A tab, as each role this fakes names it.
        typedef ALScriptStudioDoc Doc;

        Doc* openFileTab(const std::string& path, bool) override
        {
            opened.push_back(path);
            return nullptr;
        }
        void activate(Doc& doc) override { activated.push_back(doc.id); }
        void revert(Doc& doc) override { reverted.push_back(doc.id); }
        // As the window takes it: the tab's text replaced, and clean.
        void takeCarriedText(Doc& doc) override
        {
            if (doc.carriedText)
            {
                taken.push_back(*doc.carriedText);
                doc.editor->setText(*doc.carriedText);
                doc.carriedText.reset();
            }
        }
        void scheduleAnalysis(Doc& doc, bool now) override { checked.push_back(doc.id + (now ? " now" : "")); }
        void pickFilesToOpen(bool several, Chosen chosen) override
        {
            pickedSeveral = several;
            toOpen        = std::move(chosen);
        }
        void pickFileToSave(const std::string& name, Chosen chosen) override
        {
            savedAs = name;
            toSave  = std::move(chosen);
        }
        void askReload(const Doc& doc, std::function<void(bool)> answered) override
        {
            asked.push_back(doc.id);
            answer = std::move(answered);
        }
        void fileSettled(Doc& doc) override
        {
            settled.push_back(doc.id);
            doc.editor->resetDirty();
        }
        void      stopped(Doc& doc) override { stops.push_back(doc.id); }
        void      fileWritten(Doc& doc) override { written.push_back(doc.file); }
        void      reachChanged() override { ++reachChanges; }
        void      becomeFile(Doc& doc, const std::string& path) override
        {
            became.push_back(path);
            doc.file = path;
            doc.id   = "disk:" + path;
        }
        LLMenuGL* recentMenu() override { return menu; }
        void      recentChanged() override { ++recentTold; }

        Names                     opened, activated, reverted, taken, checked, asked, settled, stops, written, became;
        S32                       reachChanges = 0;
        bool                      pickedSeveral = false;
        Chosen                    toOpen, toSave;
        std::string               savedAs;
        std::function<void(bool)> answer;
        LLMenuGL*                 menu       = nullptr;
        S32                       recentTold = 0;
    };
}

namespace
{
    struct alscriptstudiofiles_data
    {
        al_studio_test::StudioWindow         window;
        al_studio_test::FakeServices         services;
        FakeFilesWindow                      studio;
        std::unique_ptr<ALScriptStudioFiles> unit;
        std::string                          folder;

        ~alscriptstudiofiles_data()
        {
            unit.reset();
            services.docs.clear();
            if (!folder.empty())
            {
                std::error_code ignored;
                std::filesystem::remove_all(fsyspath(folder), ignored);
            }
        }

        ALScriptStudioFiles& make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            const std::string name = "alscriptstudiofiles_" + LLUUID::generateNewID().asString();
            const fsyspath    made = std::filesystem::temp_directory_path() / fsyspath(name);
            folder              = made.string();
            std::filesystem::create_directories(made);
            unit = std::make_unique<ALScriptStudioFiles>(services, studio, studio, studio, studio);
            return *unit;
        }
        std::string in(const std::string& name) const { return fsyspath(fsyspath(folder) / fsyspath(name)).string(); }
        // A tab of a file in the folder, holding what the file holds.
        Doc& fileTab(const std::string& name, const std::string& text)
        {
            const std::string path = in(name);
            write(path, text);
            Doc& doc       = services.addDoc("disk:" + path);
            doc.file       = path;
            doc.name       = name;
            doc.loaded     = true;
            doc.modifiable = true;
            doc.editor     = editor("editor_" + name, text);
            return doc;
        }
        Doc& scriptTab(const std::string& id, const std::string& text)
        {
            Doc& doc       = services.addDoc(id, ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), id);
            doc.name       = id;
            doc.loaded     = true;
            doc.modifiable = true;
            doc.editor     = editor("editor_" + id, text);
            return doc;
        }
        ALCodeEditor* editor(const std::string& name, const std::string& text)
        {
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name             = name;
            p.rect             = LLRect(0, 200, 400, 0);
            p.syntax           = "lsl";
            ALCodeEditor* made = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(made);
            made->setText(text);
            made->resetDirty();
            return made;
        }
        static std::string contents(const std::string& path)
        {
            llifstream in(fsyspath(path), std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        static void write(const std::string& path, const std::string& text) { llofstream(fsyspath(path), std::ios::binary) << text; }
        const al_studio_test::FakeServices::Said& said() const { return services.reports.back(); }
    };

    TEST_CASE_METHOD(alscriptstudiofiles_data, "alscriptstudiofiles: what a file's name says it holds", "[alscriptstudiofiles]")
    {
        typedef ALScriptStudioFiles F;
        {
            INFO("LSL");
            CHECK(F::languageOf("/a/b.lsl", false).script);
            CHECK_FALSE(F::languageOf("/a/b.lsl", true).lua);
        }
        {
            INFO("SLua by .luau and .lua, whatever the case");
            CHECK(F::languageOf("/a/b.LUAU", false).lua);
            CHECK(F::languageOf("/a/b.lua", false).lua);
        }
        {
            INFO("text");
            CHECK_FALSE(F::languageOf("/a/b.txt", false).script);
            CHECK(F::languageOf("/a/b.txt", false).said);
        }
        {
            INFO("no extension: what it was asked for as");
            CHECK(F::languageOf("/a/b", true).script);
            CHECK(F::languageOf("/a/b", true).lua);
            CHECK_FALSE(F::languageOf("/a/b", true).said);
        }
    }

    TEST_CASE_METHOD(alscriptstudiofiles_data, "alscriptstudiofiles: the recent lists keep ten newest first each once and survive the state", "[alscriptstudiofiles]")
    {
        ALScriptStudioFiles& unit = make();
        for (int i = 0; i < 12; ++i)
        {
            unit.noteFile("/f" + std::to_string(i));
        }
        unit.noteFile("/f5");
        REQUIRE(unit.recentFiles().size() == size_t(10));
        const Names& files = unit.recentFiles();
        {
            INFO("newest first, the one noted again moved up");
            CHECK(files[0] == "/f5");
            CHECK(files[1] == "/f11");
            CHECK(std::count(files.begin(), files.end(), "/f5") == 1);
        }
        Doc& a = scriptTab("a", "x");
        Doc& b = scriptTab("b", "y");
        unit.noteScript(a);
        unit.noteScript(b);
        unit.noteScript(a);
        {
            INFO("scripts each once, newest first");
            REQUIRE(unit.recentScripts().size() == 2);
            CHECK(unit.recentScripts()[0].ref == a.ref);
        }
        Doc& f = fileTab("f.lsl", "z");
        unit.noteScript(f);
        UNSCOPED_INFO("a file's tab is no script");
        CHECK(unit.recentScripts().size() == 2);
        UNSCOPED_INFO("the window told each time");
        CHECK(studio.recentTold == 16);

        LLSD state;
        unit.writeState(state);
        ALScriptStudioFiles again(services, studio, studio, studio, studio);
        again.readState(state);
        {
            INFO("read back as written");
            CHECK(again.recentFiles() == unit.recentFiles());
            REQUIRE(again.recentScripts().size() == 2);
            CHECK(again.recentScripts()[1].ref == b.ref);
            CHECK(again.recentScripts()[1].name == "b");
        }
        unit.clearRecent();
        {
            INFO("cleared");
            CHECK(unit.recentFiles().empty());
            CHECK(unit.recentScripts().empty());
            CHECK(studio.recentTold == 17);
        }
        LLSD odd;
        odd["recent_files"] = LLSD().with(0, "/x").with(1, "").with(2, "/x").with(3, "/y");
        odd["recent_scripts"] = LLSD().with(0, LLSD().with("object", LLUUID::null).with("item", LLUUID::null).with("name", "none"));
        again.readState(odd);
        {
            INFO("read back: each once, none empty, no script without what holds it");
            CHECK(again.recentFiles() == Names{ "/x", "/y" });
            CHECK(again.recentScripts().empty());
        }
    }

    TEST_CASE_METHOD(alscriptstudiofiles_data, "alscriptstudiofiles: Open Recent lists scripts then files and a folder where names clash", "[alscriptstudiofiles]")
    {
        ALScriptStudioFiles& unit = make();
        LLMenuGL::Params     p;
        p.name      = "open_recent";
        studio.menu = LLUICtrlFactory::create<LLMenuGL>(p);
        // Not shown: an item chosen from a shown menu hides the viewer's
        // menus, which there are none of here.
        studio.menu->setVisible(false);
        unit.fillMenu();
        CHECK(studio.menu->findChild<LLView>("no_recent") != nullptr);
        unit.noteFile("/one/same.lsl");
        unit.noteFile("/two/same.lsl");
        unit.noteFile("/two/other.lsl");
        {
            INFO("files alone: no heading");
            CHECK(studio.menu->findChild<LLView>("recent_heading_RecentFiles") == nullptr);
            CHECK(studio.menu->findChild<LLView>("recent_/two/other.lsl") != nullptr);
        }
        Doc& a = scriptTab("a", "x");
        unit.noteScript(a);
        {
            INFO("both: headings");
            CHECK(studio.menu->findChild<LLView>("recent_heading_RecentScripts") != nullptr);
            CHECK(studio.menu->findChild<LLView>("recent_heading_RecentFiles") != nullptr);
        }
        LLMenuItemCallGL* same = studio.menu->findChild<LLMenuItemCallGL>("recent_/one/same.lsl");
        {
            INFO("a folder after a name two share");
            REQUIRE(same);
            CHECK(same->getLabel().find("(/one)") != std::string::npos);
        }
        LLMenuItemCallGL* other = studio.menu->findChild<LLMenuItemCallGL>("recent_/two/other.lsl");
        {
            INFO("one alone, by its name");
            REQUIRE(other);
            CHECK(other->getLabel() == "other.lsl");
        }
        other->onCommit();
        UNSCOPED_INFO("picked: opened");
        CHECK(studio.opened == Names{ "/two/other.lsl" });
        LLMenuItemCallGL* script = studio.menu->findChild<LLMenuItemCallGL>("recent_" + a.ref.id());
        {
            INFO("a script by its name");
            REQUIRE(script);
            CHECK(script->getLabel() == "a");
        }
        script->onCommit();
        {
            INFO("picked: opened as a script");
            REQUIRE_FALSE(services.opened.empty());
            CHECK(services.opened.back().ref == a.ref);
        }
        LLMenuItemCallGL* clear = studio.menu->findChild<LLMenuItemCallGL>("clear_recent");
        REQUIRE(clear != nullptr);
        clear->onCommit();
        {
            INFO("the clear item clears");
            CHECK(unit.recentFiles().empty());
            CHECK(studio.menu->findChild<LLView>("no_recent") != nullptr);
        }
        studio.menu->die();
        studio.menu = nullptr;
    }

    TEST_CASE_METHOD(alscriptstudiofiles_data, "alscriptstudiofiles: writing a tab's file settles it and a failed write stops the save", "[alscriptstudiofiles]")
    {
        ALScriptStudioFiles& unit = make();
        Doc&                 f    = fileTab("f.lsl", "old");
        unit.watch(f);
        f.editor->setText("new");
        unit.write(f);
        CHECK(contents(f.file) == std::string("new"));
        {
            INFO("its own write is no change to the watch");
            REQUIRE(f.watch);
            CHECK_FALSE(f.watch->check());
            CHECK_FALSE(f.watch->check());
        }
        {
            INFO("said, settled, told of");
            CHECK_FALSE(said().failure);
            CHECK(studio.settled == Names{ f.id });
            CHECK(studio.written == Names{ f.file });
        }
        Doc& g = fileTab("g.lsl", "x");
        g.file = in("no/such/folder/g.lsl");
        unit.write(g);
        {
            INFO("will not go: said and stopped, not settled");
            CHECK(said().failure);
            CHECK(studio.stops == Names{ g.id });
            CHECK(studio.settled.size() == 1);
        }
    }

    TEST_CASE_METHOD(alscriptstudiofiles_data, "alscriptstudiofiles: an outside change is taken into a clean tab and asked about otherwise", "[alscriptstudiofiles]")
    {
        ALScriptStudioFiles& unit = make();
        Doc&                 f    = fileTab("f.lsl", "one");
        unit.watch(f);
        {
            INFO("watched");
            REQUIRE(f.watch);
            CHECK(f.watch->path() == f.file);
        }
        write(f.file, "two");
        unit.changedOutside(f.id, f.file);
        {
            INFO("clean: taken, settled, said");
            CHECK(studio.taken == Names{ "two" });
            CHECK(studio.settled == Names{ f.id });
            CHECK_FALSE(said().failure);
        }
        // Read in, not written: what it masters is the masters' watch's to
        // send, not the tab's.
        UNSCOPED_INFO("not taken for a write of the studio's");
        CHECK(studio.written.empty());
        unit.changedOutside(f.id, f.file);
        UNSCOPED_INFO("the same again: nothing");
        CHECK(studio.taken.size() == 1);

        f.editor->setCaret(f.editor->document().end());
        f.editor->insertText(" typed");
        write(f.file, "three");
        unit.changedOutside(f.id, f.file);
        {
            INFO("not clean: said and asked, not taken");
            CHECK(said().failure);
            CHECK(studio.asked == Names{ f.id });
            CHECK(studio.taken.size() == 1);
        }
        unit.changedOutside(f.id, f.file);
        UNSCOPED_INFO("asked once while the question is up");
        CHECK(studio.asked.size() == 1);
        studio.answer(false);
        {
            INFO("kept: nothing read");
            CHECK(studio.reverted.empty());
            CHECK_FALSE(f.askingReload);
        }
        unit.changedOutside(f.id, f.file);
        studio.answer(true);
        UNSCOPED_INFO("read again");
        CHECK(studio.reverted == Names{ f.id });
        unit.changedOutside("nobody", f.file);
        const S32 before = studio.reachChanges;
        std::filesystem::remove(fsyspath(f.file));
        unit.changedOutside(f.id, f.file);
        UNSCOPED_INFO("gone or unknown: nothing");
        CHECK(studio.asked.size() == 2);
        UNSCOPED_INFO("but what is in reach looked at again");
        CHECK(studio.reachChanges == before + 1);
    }

    TEST_CASE_METHOD(alscriptstudiofiles_data, "alscriptstudiofiles: Open picks several and Load and Insert one for the tab asked from", "[alscriptstudiofiles]")
    {
        ALScriptStudioFiles& unit = make();
        unit.openFromDisk();
        CHECK(studio.pickedSeveral);
        studio.toOpen({ "/a.lsl", "/b.luau" });
        UNSCOPED_INFO("each opened");
        CHECK(studio.opened == Names{ "/a.lsl", "/b.luau" });

        Doc& a = scriptTab("a", "first");
        write(in("load.txt"), "loaded");
        unit.load(false);
        CHECK_FALSE(studio.pickedSeveral);
        Doc& b = scriptTab("b", "other");
        services.front = 1;
        studio.toOpen({ in("load.txt") });
        {
            INFO("the tab asked from, brought forward, its text replaced");
            CHECK(a.editor->text() == "loaded");
            CHECK(b.editor->text() == "other");
            CHECK(studio.activated == Names{ "a" });
        }
        services.front = 1;
        b.editor->setCaret(ALTextPos(0, 2));
        unit.load(true);
        studio.toOpen({ in("load.txt") });
        UNSCOPED_INFO("inserted at the caret");
        CHECK(b.editor->text() == std::string("otloadedher"));
        unit.chosenToLoad("b", { in("nothing.txt") }, false);
        {
            INFO("unreadable: said");
            CHECK_FALSE(services.statuses.empty());
            CHECK(b.editor->text() == "otloadedher");
        }
        b.modifiable = false;
        unit.chosenToLoad("b", { in("load.txt") }, false);
        UNSCOPED_INFO("unchangeable: left");
        CHECK(b.editor->text() == "otloadedher");
    }

    TEST_CASE_METHOD(alscriptstudiofiles_data, "alscriptstudiofiles: Save to File writes a copy and Save As makes the tab the new file", "[alscriptstudiofiles]")
    {
        ALScriptStudioFiles& unit = make();
        scriptTab("s", "script text");
        services.front = 0;
        unit.saveCopy();
        UNSCOPED_INFO("named after the tab");
        CHECK(studio.savedAs == std::string("s"));
        studio.toSave({ in("copy.lsl") });
        {
            INFO("a copy written, said");
            CHECK(contents(in("copy.lsl")) == "script text");
            CHECK_FALSE(said().failure);
        }

        Doc& f         = fileTab("f.lsl", "file text");
        services.front = 1;
        unit.watch(f);
        unit.saveAs();
        studio.toSave({ in("g.luau") });
        UNSCOPED_INFO("written there");
        CHECK(contents(in("g.luau")) == std::string("file text"));
        {
            INFO("the tab that file now");
            CHECK(studio.became == Names{ in("g.luau") });
            CHECK(f.file == in("g.luau"));
        }
        {
            INFO("watched there");
            REQUIRE(f.watch);
            CHECK(f.watch->path() == in("g.luau"));
        }
        {
            INFO("noted, settled, checked");
            REQUIRE_FALSE(unit.recentFiles().empty());
            CHECK(unit.recentFiles().front() == in("g.luau"));
            REQUIRE_FALSE(studio.settled.empty());
            CHECK(studio.settled.back() == f.id);
            REQUIRE_FALSE(studio.checked.empty());
            CHECK(studio.checked.back() == f.id + " now");
        }
        UNSCOPED_INFO("told of as written");
        CHECK(studio.written == Names{ in("g.luau") });
        f.editor->setText("changed");
        unit.chosenToSaveAs(f.id, { f.file });
        {
            INFO("its own file again: a save");
            CHECK(contents(f.file) == "changed");
            CHECK(studio.became.size() == 1);
        }
        Doc& other = fileTab("other.lsl", "o");
        unit.chosenToSaveAs(f.id, { other.file });
        {
            INFO("a file another tab has: said, and nothing written");
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(services.statuses.back().find("FileOpenElsewhere") == 0);
            CHECK(contents(other.file) == "o");
            CHECK(studio.became.size() == 1);
        }
    }

    TEST_CASE_METHOD(alscriptstudiofiles_data, "alscriptstudiofiles: a file with a non-ASCII path is written and read back with LF endings", "[alscriptstudiofiles]")
    {
        make();
        // A user's folder and a script's name as a German speaker might
        // have them: on Windows, read in the ANSI code page, no such file.
        const fsyspath    folder_path = fsyspath(in("J\xC3\xBCrgen"));
        std::filesystem::create_directories(folder_path);
        const std::string path = fsyspath(folder_path / fsyspath("\xC3\x9C" "berpr\xC3\xBC" "fung.lsl")).string();
        CHECK(ALFileWrite::temp(path, "default\r\n{\r\n}\r\n"));
        std::string text;
        REQUIRE(ALScriptFileIO::readWholeFile(path, text));
        UNSCOPED_INFO("as an editor keeps it");
        CHECK(text == std::string("default\n{\n}\n"));
        UNSCOPED_INFO("and where it was asked to be");
        CHECK(std::filesystem::exists(fsyspath(path)));
    }

    TEST_CASE_METHOD(alscriptstudiofiles_data, "alscriptstudiofiles: pruneRecent drops scripts said gone and files no longer on disk", "[alscriptstudiofiles]")
    {
        ALScriptStudioFiles& unit = make();
        CHECK(ALFileWrite::temp(in("here.lsl"), "x"));
        unit.noteFile(in("here.lsl"));
        unit.noteFile(in("gone.lsl"));
        Doc& a = scriptTab("a", "x");
        Doc& b = scriptTab("b", "y");
        unit.noteScript(a);
        unit.noteScript(b);
        const S32 told = studio.recentTold;
        {
            INFO("the file no longer there let go of");
            CHECK(unit.pruneRecent([](const ALScriptRef&) { return false; }));
            CHECK(unit.recentFiles() == Names{ in("here.lsl") });
            CHECK(unit.recentScripts().size() == 2);
            CHECK(studio.recentTold == told + 1);
        }
        {
            INFO("nothing more gone, nothing done");
            CHECK_FALSE(unit.pruneRecent([](const ALScriptRef&) { return false; }));
            CHECK(studio.recentTold == told + 1);
        }
        const ALScriptRef lost = b.ref;
        CHECK(unit.pruneRecent([lost](const ALScriptRef& ref) { return ref == lost; }));
        {
            INFO("the gone script let go of");
            REQUIRE(unit.recentScripts().size() == 1);
            CHECK(unit.recentScripts()[0].ref == a.ref);
        }
        UNSCOPED_INFO("the file still there kept");
        CHECK(unit.recentFiles() == Names{ in("here.lsl") });
    }

    TEST_CASE_METHOD(alscriptstudiofiles_data, "alscriptstudiofiles: textSyntaxOf picks a grammar by extension in any case or else text", "[alscriptstudiofiles]")
    {
        // The grammars are the source tree's, which the UI is pointed at.
        if (!ll_test::HeadlessUI::get().ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        typedef ALScriptStudioFiles F;
        CHECK(F::textSyntaxOf("/a/s.XML") == std::string("xml"));
        UNSCOPED_INFO("a skin's file");
        CHECK(F::textSyntaxOf("/a/s.xui") == std::string("xml"));
        CHECK(F::textSyntaxOf("/a/s.json") == std::string("json"));
        UNSCOPED_INFO("a notecard of settings");
        CHECK(F::textSyntaxOf("/a/door.ini") == std::string("config"));
        UNSCOPED_INFO("and another way of naming one");
        CHECK(F::textSyntaxOf("/a/door.CFG") == std::string("config"));
        CHECK(F::textSyntaxOf("/a/s.txt") == std::string("text"));
        UNSCOPED_INFO("a name no grammar knows");
        CHECK(F::textSyntaxOf("/a/s.dat") == std::string("text"));
        UNSCOPED_INFO("and none");
        CHECK(F::textSyntaxOf("/a/s") == std::string("text"));
    }
}
