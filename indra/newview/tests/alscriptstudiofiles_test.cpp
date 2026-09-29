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

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>

// A script's id is the bridge's name for it, and the bridge is the viewer's.
std::string ALScriptRef::id() const
{
    return object.asString() + "_" + item.asString();
}

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;
    typedef std::function<void(const std::vector<std::string>&)> Chosen;

    // The window, faked: a record of what the files asked of it.
    struct FakeFilesWindow : public ALScriptStudioFiles::Window, public al_studio_test::QuietTabs
    {
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
        void      saveStopped(Doc& doc) override { stopped.push_back(doc.id); }
        void      fileWritten(const std::string& path) override { written.push_back(path); }
        void      reachChanged() override { ++reachChanges; }
        void      becomeFile(Doc& doc, const std::string& path) override
        {
            became.push_back(path);
            doc.file = path;
            doc.id   = "disk:" + path;
        }
        LLMenuGL* recentMenu() override { return menu; }
        void      recentChanged() override { ++recentTold; }

        Names                     opened, activated, reverted, taken, checked, asked, settled, stopped, written, became;
        S32                       reachChanges = 0;
        bool                      pickedSeveral = false;
        Chosen                    toOpen, toSave;
        std::string               savedAs;
        std::function<void(bool)> answer;
        LLMenuGL*                 menu       = nullptr;
        S32                       recentTold = 0;
    };
}

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            const std::string name = "alscriptstudiofiles_" + LLUUID::generateNewID().asString();
            const fsyspath    made = std::filesystem::temp_directory_path() / fsyspath(name);
            folder              = made.string();
            std::filesystem::create_directories(made);
            unit = std::make_unique<ALScriptStudioFiles>(services, studio, studio);
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

    typedef test_group<alscriptstudiofiles_data> alscriptstudiofiles_group;
    typedef alscriptstudiofiles_group::object    alscriptstudiofiles_object;
    alscriptstudiofiles_group                    alscriptstudiofiles_instance("alscriptstudiofiles");

    template<> template<>
    void alscriptstudiofiles_object::test<1>()
    {
        set_test_name("what a file's name says it holds, and the grammar a file that is no script is coloured by");
        typedef ALScriptStudioFiles F;
        ensure("LSL", F::languageOf("/a/b.lsl", false).script && !F::languageOf("/a/b.lsl", true).lua);
        ensure("SLua by .luau and .lua, whatever the case", F::languageOf("/a/b.LUAU", false).lua && F::languageOf("/a/b.lua", false).lua);
        ensure("text", !F::languageOf("/a/b.txt", false).script && F::languageOf("/a/b.txt", false).said);
        ensure("no extension: what it was asked for as", F::languageOf("/a/b", true).script && F::languageOf("/a/b", true).lua &&
                                                             !F::languageOf("/a/b", true).said);
        ensure("XML, XUI and JSON coloured, the rest text",
               F::textSyntaxOf("/a/s.XML") == "xml" && F::textSyntaxOf("/a/s.xui") == "xml" && F::textSyntaxOf("/a/s.json") == "json" &&
                   F::textSyntaxOf("/a/s.txt") == "text");
    }

    template<> template<>
    void alscriptstudiofiles_object::test<2>()
    {
        set_test_name("the recent lists: newest first, each once, ten at most; a file tab not a script; cleared; kept with the state");
        ALScriptStudioFiles& unit = make();
        for (int i = 0; i < 12; ++i)
        {
            unit.noteFile("/f" + std::to_string(i));
        }
        unit.noteFile("/f5");
        ensure_equals("ten", unit.recentFiles().size(), size_t(10));
        const Names& files = unit.recentFiles();
        ensure("newest first, the one noted again moved up",
               files[0] == "/f5" && files[1] == "/f11" && std::count(files.begin(), files.end(), "/f5") == 1);
        Doc& a = scriptTab("a", "x");
        Doc& b = scriptTab("b", "y");
        unit.noteScript(a);
        unit.noteScript(b);
        unit.noteScript(a);
        ensure("scripts each once, newest first", unit.recentScripts().size() == 2 && unit.recentScripts()[0].ref == a.ref);
        Doc& f = fileTab("f.lsl", "z");
        unit.noteScript(f);
        ensure("a file's tab is no script", unit.recentScripts().size() == 2);
        ensure_equals("the window told each time", studio.recentTold, 16);

        LLSD state;
        unit.writeState(state);
        ALScriptStudioFiles again(services, studio, studio);
        again.readState(state);
        ensure("read back as written", again.recentFiles() == unit.recentFiles() && again.recentScripts().size() == 2 &&
                                           again.recentScripts()[1].ref == b.ref && again.recentScripts()[1].name == "b");
        unit.clearRecent();
        ensure("cleared", unit.recentFiles().empty() && unit.recentScripts().empty() && studio.recentTold == 17);
        LLSD odd;
        odd["recent_files"] = LLSD().with(0, "/x").with(1, "").with(2, "/x").with(3, "/y");
        odd["recent_scripts"] = LLSD().with(0, LLSD().with("object", LLUUID::null).with("item", LLUUID::null).with("name", "none"));
        again.readState(odd);
        ensure("read back: each once, none empty, no script without what holds it",
               again.recentFiles() == Names{ "/x", "/y" } && again.recentScripts().empty());
    }

    template<> template<>
    void alscriptstudiofiles_object::test<3>()
    {
        set_test_name("Open Recent filled: scripts then files under their headings, a folder where two share a name; nothing to say so");
        ALScriptStudioFiles& unit = make();
        LLMenuGL::Params     p;
        p.name      = "open_recent";
        studio.menu = LLUICtrlFactory::create<LLMenuGL>(p);
        // Not shown: an item chosen from a shown menu hides the viewer's
        // menus, which there are none of here.
        studio.menu->setVisible(false);
        unit.fillMenu();
        ensure("nothing", studio.menu->findChild<LLView>("no_recent") != nullptr);
        unit.noteFile("/one/same.lsl");
        unit.noteFile("/two/same.lsl");
        unit.noteFile("/two/other.lsl");
        ensure("files alone: no heading", studio.menu->findChild<LLView>("recent_heading_RecentFiles") == nullptr &&
                                            studio.menu->findChild<LLView>("recent_/two/other.lsl") != nullptr);
        Doc& a = scriptTab("a", "x");
        unit.noteScript(a);
        ensure("both: headings", studio.menu->findChild<LLView>("recent_heading_RecentScripts") != nullptr &&
                                   studio.menu->findChild<LLView>("recent_heading_RecentFiles") != nullptr);
        LLMenuItemCallGL* same = studio.menu->findChild<LLMenuItemCallGL>("recent_/one/same.lsl");
        ensure("a folder after a name two share", same && same->getLabel().find("(/one)") != std::string::npos);
        LLMenuItemCallGL* other = studio.menu->findChild<LLMenuItemCallGL>("recent_/two/other.lsl");
        ensure("one alone, by its name", other && other->getLabel() == "other.lsl");
        other->onCommit();
        ensure("picked: opened", studio.opened == Names{ "/two/other.lsl" });
        LLMenuItemCallGL* script = studio.menu->findChild<LLMenuItemCallGL>("recent_" + a.ref.id());
        ensure("a script by its name", script && script->getLabel() == "a");
        script->onCommit();
        ensure("picked: opened as a script", !services.opened.empty() && services.opened.back().ref == a.ref);
        studio.menu->findChild<LLMenuItemCallGL>("clear_recent")->onCommit();
        ensure("the clear item clears", unit.recentFiles().empty() && studio.menu->findChild<LLView>("no_recent") != nullptr);
        studio.menu->die();
        studio.menu = nullptr;
    }

    template<> template<>
    void alscriptstudiofiles_object::test<4>()
    {
        set_test_name("a tab's file written: said, settled and told of; one that will not go said, and the save stopped");
        ALScriptStudioFiles& unit = make();
        Doc&                 f    = fileTab("f.lsl", "old");
        unit.watch(f);
        f.editor->setText("new");
        unit.write(f);
        ensure_equals("written", contents(f.file), std::string("new"));
        ensure("its own write is no change to the watch", !f.watch->check() && !f.watch->check());
        ensure("said, settled, told of", !said().failure && studio.settled == Names{ f.id } && studio.written == Names{ f.file });
        Doc& g = fileTab("g.lsl", "x");
        g.file = in("no/such/folder/g.lsl");
        unit.write(g);
        ensure("will not go: said and stopped, not settled",
               said().failure && studio.stopped == Names{ g.id } && studio.settled.size() == 1);
    }

    template<> template<>
    void alscriptstudiofiles_object::test<5>()
    {
        set_test_name("a file changed outside is taken where the tab is clean; asked about once where it is not, and read again if so answered");
        ALScriptStudioFiles& unit = make();
        Doc&                 f    = fileTab("f.lsl", "one");
        unit.watch(f);
        ensure("watched", f.watch && f.watch->path() == f.file);
        write(f.file, "two");
        unit.changedOutside(f.id, f.file);
        ensure("clean: taken, settled, said", studio.taken == Names{ "two" } && studio.settled == Names{ f.id } && !said().failure);
        unit.changedOutside(f.id, f.file);
        ensure("the same again: nothing", studio.taken.size() == 1);

        f.editor->setCaret(f.editor->document().end());
        f.editor->insertText(" typed");
        write(f.file, "three");
        unit.changedOutside(f.id, f.file);
        ensure("not clean: said and asked, not taken", said().failure && studio.asked == Names{ f.id } && studio.taken.size() == 1);
        unit.changedOutside(f.id, f.file);
        ensure("asked once while the question is up", studio.asked.size() == 1);
        studio.answer(false);
        ensure("kept: nothing read", studio.reverted.empty() && !f.askingReload);
        unit.changedOutside(f.id, f.file);
        studio.answer(true);
        ensure("read again", studio.reverted == Names{ f.id });
        unit.changedOutside("nobody", f.file);
        const S32 before = studio.reachChanges;
        std::filesystem::remove(fsyspath(f.file));
        unit.changedOutside(f.id, f.file);
        ensure("gone or unknown: nothing", studio.asked.size() == 2);
        ensure("but what is in reach looked at again", studio.reachChanges == before + 1);
    }

    template<> template<>
    void alscriptstudiofiles_object::test<6>()
    {
        set_test_name("the pickers: Open several, Load and Insert one for the tab asked from, whatever is in front when the answer comes");
        ALScriptStudioFiles& unit = make();
        unit.openFromDisk();
        ensure("several", studio.pickedSeveral);
        studio.toOpen({ "/a.lsl", "/b.luau" });
        ensure("each opened", studio.opened == Names{ "/a.lsl", "/b.luau" });

        Doc& a = scriptTab("a", "first");
        write(in("load.txt"), "loaded");
        unit.load(false);
        ensure("one", !studio.pickedSeveral);
        Doc& b = scriptTab("b", "other");
        services.front = 1;
        studio.toOpen({ in("load.txt") });
        ensure("the tab asked from, brought forward, its text replaced", a.editor->text() == "loaded" && b.editor->text() == "other" &&
                                                                             studio.activated == Names{ "a" });
        services.front = 1;
        b.editor->setCaret(ALTextPos(0, 2));
        unit.load(true);
        studio.toOpen({ in("load.txt") });
        ensure_equals("inserted at the caret", b.editor->text(), std::string("otloadedher"));
        unit.chosenToLoad("b", { in("nothing.txt") }, false);
        ensure("unreadable: said", !services.statuses.empty() && b.editor->text() == "otloadedher");
        b.modifiable = false;
        unit.chosenToLoad("b", { in("load.txt") }, false);
        ensure("unchangeable: left", b.editor->text() == "otloadedher");
    }

    template<> template<>
    void alscriptstudiofiles_object::test<7>()
    {
        set_test_name("Save to File writes a copy; Save As makes the tab the new file, watched there and noted; the same file is a save");
        ALScriptStudioFiles& unit = make();
        Doc&                 s    = scriptTab("s", "script text");
        services.front            = 0;
        unit.saveCopy();
        ensure_equals("named after the tab", studio.savedAs, std::string("s"));
        studio.toSave({ in("copy.lsl") });
        ensure("a copy written, said", contents(in("copy.lsl")) == "script text" && !said().failure);

        Doc& f         = fileTab("f.lsl", "file text");
        services.front = 1;
        unit.watch(f);
        unit.saveAs();
        studio.toSave({ in("g.luau") });
        ensure_equals("written there", contents(in("g.luau")), std::string("file text"));
        ensure("the tab that file now", studio.became == Names{ in("g.luau") } && f.file == in("g.luau"));
        ensure("watched there", f.watch && f.watch->path() == in("g.luau"));
        ensure("noted, settled, checked", unit.recentFiles().front() == in("g.luau") && studio.settled.back() == f.id &&
                                               studio.checked.back() == f.id + " now");
        f.editor->setText("changed");
        unit.chosenToSaveAs(f.id, { f.file });
        ensure("its own file again: a save", contents(f.file) == "changed" && studio.became.size() == 1);
        Doc& other = fileTab("other.lsl", "o");
        unit.chosenToSaveAs(f.id, { other.file });
        ensure("a file another tab has: said, and nothing written", !services.statuses.empty() &&
                                                                        services.statuses.back().find("FileOpenElsewhere") == 0 &&
                                                                        contents(other.file) == "o" && studio.became.size() == 1);
    }

    template<> template<>
    void alscriptstudiofiles_object::test<8>()
    {
        set_test_name("a file whose name and folder go past ASCII is written and read back, its line endings as an editor keeps them");
        make();
        // A user's folder and a script's name as a German speaker might
        // have them: on Windows, read in the ANSI code page, no such file.
        const fsyspath    folder_path = fsyspath(in("J\xC3\xBCrgen"));
        std::filesystem::create_directories(folder_path);
        const std::string path = fsyspath(folder_path / fsyspath("\xC3\x9C" "berpr\xC3\xBC" "fung.lsl")).string();
        ensure("written", ALFileWrite::temp(path, "default\r\n{\r\n}\r\n"));
        std::string text;
        ensure("read back", ALScriptFileIO::readWholeFile(path, text));
        ensure_equals("as an editor keeps it", text, std::string("default\n{\n}\n"));
        ensure("and where it was asked to be", std::filesystem::exists(fsyspath(path)));
    }

    template<> template<>
    void alscriptstudiofiles_object::test<9>()
    {
        set_test_name("what was opened lately and cannot be had any more is let go of: a script said gone, a file no longer on disk");
        ALScriptStudioFiles& unit = make();
        ensure("written", ALFileWrite::temp(in("here.lsl"), "x"));
        unit.noteFile(in("here.lsl"));
        unit.noteFile(in("gone.lsl"));
        Doc& a = scriptTab("a", "x");
        Doc& b = scriptTab("b", "y");
        unit.noteScript(a);
        unit.noteScript(b);
        const S32 told = studio.recentTold;
        ensure("the file no longer there let go of", unit.pruneRecent([](const ALScriptRef&) { return false; }) &&
                                                          unit.recentFiles() == Names{ in("here.lsl") } && unit.recentScripts().size() == 2 &&
                                                          studio.recentTold == told + 1);
        ensure("nothing more gone, nothing done", !unit.pruneRecent([](const ALScriptRef&) { return false; }) && studio.recentTold == told + 1);
        const ALScriptRef lost = b.ref;
        ensure("pruned", unit.pruneRecent([lost](const ALScriptRef& ref) { return ref == lost; }));
        ensure("the gone script let go of", unit.recentScripts().size() == 1 && unit.recentScripts()[0].ref == a.ref);
        ensure("the file still there kept", unit.recentFiles() == Names{ in("here.lsl") });
    }
}
