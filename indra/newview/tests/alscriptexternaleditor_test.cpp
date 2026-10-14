/**
 * @file alscriptexternaleditor_test.cpp
 * @brief Script Studio's tabs held open in an editor outside: the copy, its saves taken or held, the log, letting go.
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

#include "../alscriptexternaleditor.h"

#include "alscriptstudio_fixture.h"
#include "alscripttempfiles.h"
#include "alwatchedfile.h"

#include "fsyspath.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>

// The log's first line says when: "[when]", by the studio's tests' stub of
// the chat log's clock (alscriptstudio_stubs.cpp).

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;

    // The window, faked: a record of what the external editor asked of it.
    struct FakeExternalWindow : public ALScriptExternalEditor::Window, public al_studio_test::QuietTabs, public al_studio_test::QuietSaves
    {
        // A tab, as each role this fakes names it.
        typedef ALScriptStudioDoc Doc;

        // As the window takes it: the tab's text replaced, one step to undo.
        void takeCarriedText(Doc& doc) override
        {
            if (doc.carriedText)
            {
                taken.push_back(*doc.carriedText);
                const ALTextDocument& text = doc.editor->document();
                doc.editor->replaceAll({ { ALTextRange(ALTextPos(0, 0), text.end()), *doc.carriedText } });
                doc.carriedText.reset();
            }
        }
        void        save(Doc& doc) override { saved.push_back(doc.id); }
        std::string bridgeId(const Doc&) const override { return bridge; }
        bool        subscribe(Doc& doc) override
        {
            subscribed.push_back(doc.id);
            return bridgeThere;
        }
        void unsubscribe(const Doc& doc) override { unsubscribed.push_back(doc.id); }
        void startEditor(Doc&, const std::string& file, bool on_disk) override
        {
            started.push_back(file + (on_disk ? " on disk" : ""));
        }
        std::shared_ptr<ALScriptTempFiles::Claim> holdCopy(const std::string& path) override { return copies->claim(path); }

        // A name of its own each run: the copies go in the temp folder.
        std::string bridge      = LLUUID::generateNewID().asString();
        bool        bridgeThere = false;
        Names       taken, saved, subscribed, unsubscribed, started;
        // Held as the workspace holds them, the lists in a folder of the
        // run's own.
        std::string                        lists  = (std::filesystem::temp_directory_path() / ("alscriptexternaleditor_" + bridge)).string();
        std::optional<ALScriptTempFiles>   copies = std::make_optional<ALScriptTempFiles>(lists, "test");
    };
}

namespace
{
    struct alscriptexternaleditor_data
    {
        al_studio_test::StudioWindow            window;
        al_studio_test::FakeServices            services;
        FakeExternalWindow                      studio;
        std::unique_ptr<al_studio_test::StudioFiles> files;
        std::unique_ptr<ALScriptExternalEditor> unit;

        ~alscriptexternaleditor_data()
        {
            // The copies and logs gone with the watches.
            for (const std::unique_ptr<Doc>& doc : services.docs)
            {
                if (unit)
                {
                    unit->stop(*doc);
                }
            }
            studio.copies.reset();
            std::error_code ec;
            std::filesystem::remove_all(studio.lists, ec);
        }

        ALScriptExternalEditor& make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            files = std::make_unique<al_studio_test::StudioFiles>(services);
            unit  = std::make_unique<ALScriptExternalEditor>(services, studio, studio, files->unit, studio);
            return *unit;
        }
        // A script's tab, loaded, changeable and saved.
        Doc& tab(const std::string& id, const std::string& text)
        {
            Doc& doc       = services.addDoc(id);
            doc.name       = id;
            doc.loaded     = true;
            doc.modifiable = true;
            doc.assetId    = LLUUID::generateNewID();
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
        static std::string contents(const std::string& path)
        {
            llifstream in(fsyspath(path), std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        static void write(const std::string& path, const std::string& text) { llofstream(fsyspath(path), std::ios::binary) << text; }
        static bool exists(const std::string& path) { return std::filesystem::exists(fsyspath(path)); }
        bool        asked() const
        {
            return !services.reports.empty() && services.reports.back().actions == Names{ "take_external", "keep_here" };
        }
    };

    TEST_CASE_METHOD(alscriptexternaleditor_data, "alscriptexternaleditor: the copy is named for the tab and the bridge in the temp folder", "[alscriptexternaleditor]")
    {
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "x");
        a.name                       = "my<scr>ipt:?";
        const std::string temp       = LLFile::tmpdir();
        UNSCOPED_INFO("LSL");
        CHECK(unit.fileName(a) == temp + "sl_script_myscript_" + studio.bridge + ".lsl");
        a.language.lua = true;
        a.name         = "<>";
        UNSCOPED_INFO("SLua, and a name of nothing but those");
        CHECK(unit.fileName(a) == temp + "sl_script_" + studio.bridge + ".luau");
    }

    TEST_CASE_METHOD(alscriptexternaleditor_data, "alscriptexternaleditor: Edit Externally writes and watches a copy and starts the editor", "[alscriptexternaleditor]")
    {
        ALScriptExternalEditor& unit = make();
        studio.bridgeThere           = true;
        Doc&              a          = tab("a", "default {}");
        unit.edit(a);
        const std::string file = unit.fileName(a);
        CHECK(contents(file) == std::string("default {}"));
        REQUIRE(a.watch);
        CHECK(a.watch->path() == file);
        REQUIRE(a.external->log);
        CHECK(a.external->log->path() == file + ".log");
        CHECK(studio.subscribed == Names{ "a" });
        CHECK(a.external->subscribed);
        REQUIRE(studio.started == Names{ file });
        UNSCOPED_INFO("what it holds, known");
        CHECK(a.external->written == std::string("default {}"));

        Doc& e = tab("e", "");
        unit.edit(e);
        UNSCOPED_INFO("an empty script is one space");
        CHECK(contents(unit.fileName(e)) == std::string(" "));

        Doc& d = tab("d", "x");
        d.file = "/somewhere/d.lsl";
        unit.edit(d);
        {
            INFO("a file on disk watched where it is");
            REQUIRE(d.watch);
            CHECK(d.watch->path() == "/somewhere/d.lsl");
            CHECK(studio.started.back() == "/somewhere/d.lsl on disk");
        }
        {
            INFO("with no log, and nothing for the bridge");
            CHECK_FALSE(d.external->log);
            CHECK(studio.subscribed.size() == 2);
        }

        Doc& n    = tab("n", "x");
        n.notecard = true;
        unit.edit(n);
        {
            INFO("a notecard not");
            CHECK(studio.started.size() == 3);
            CHECK_FALSE(n.watch);
        }
    }

    TEST_CASE_METHOD(alscriptexternaleditor_data, "alscriptexternaleditor: a save outside over an unchanged tab is taken and saved from here", "[alscriptexternaleditor]")
    {
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "one");
        unit.edit(a);
        write(unit.fileName(a), "two");
        unit.changed("a", unit.fileName(a));
        CHECK(studio.taken == Names{ "two" });
        CHECK(a.editor->text() == "two");
        {
            INFO("saved from here, as the editor's");
            CHECK(studio.saved == Names{ "a" });
            CHECK(a.save.external());
        }
        UNSCOPED_INFO("what the copy holds, known");
        CHECK(a.external->written == std::string("two"));
        CHECK_FALSE(asked());
        a.editor->resetDirty();
        unit.changed("a", unit.fileName(a));
        {
            INFO("the same again, with the tab saved: nothing to take or save");
            CHECK(studio.taken.size() == 1);
            CHECK(studio.saved.size() == 1);
        }
        unit.changed("nobody", unit.fileName(a));
        UNSCOPED_INFO("a tab gone: nothing");
        CHECK(studio.taken.size() == 1);
    }

    TEST_CASE_METHOD(alscriptexternaleditor_data, "alscriptexternaleditor: a save outside over changes here is held until the author takes it", "[alscriptexternaleditor]")
    {
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "one");
        unit.edit(a);
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" here");
        write(unit.fileName(a), "two");
        unit.changed("a", unit.fileName(a));
        CHECK(studio.taken.empty());
        CHECK(a.editor->text() == "one here");
        REQUIRE(a.external->waiting);
        CHECK(*a.external->waiting == "two");
        REQUIRE(asked());
        CHECK(services.reports.back().failure);
        CHECK(services.reports.back().doc == "a");
        unit.take(a, *a.external->waiting);
        {
            INFO("taken once said");
            CHECK(studio.taken == Names{ "two" });
            CHECK_FALSE(a.external->waiting);
            CHECK(studio.saved == Names{ "a" });
        }
    }

    TEST_CASE_METHOD(alscriptexternaleditor_data, "alscriptexternaleditor: a save with CRLF endings after one taken is taken without asking", "[alscriptexternaleditor]")
    {
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "one");
        unit.edit(a);
        write(unit.fileName(a), "a\r\nb");
        unit.changed("a", unit.fileName(a));
        UNSCOPED_INFO("the first, its endings as the editor keeps them");
        CHECK(studio.taken == Names{ "a\nb" });
        write(unit.fileName(a), "a\r\nb\r\nc");
        unit.changed("a", unit.fileName(a));
        CHECK(studio.taken == Names{ "a\nb", "a\nb\nc" });
        CHECK_FALSE(asked());
    }

    TEST_CASE_METHOD(alscriptexternaleditor_data, "alscriptexternaleditor: an emptied copy is taken only if still empty a moment later", "[alscriptexternaleditor]")
    {
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "one");
        unit.edit(a);
        write(unit.fileName(a), "");
        unit.changed("a", unit.fileName(a));
        UNSCOPED_INFO("not taken at once");
        CHECK(studio.taken.empty());
        unit.changed("a", unit.fileName(a), true);
        UNSCOPED_INFO("still empty a moment later: taken");
        CHECK(studio.taken == Names{ "" });
        write(unit.fileName(a), "two");
        unit.changed("a", unit.fileName(a));
        unit.changed("a", unit.fileName(a), true);
        {
            INFO("the second step heard first: the look after takes nothing more");
            CHECK(studio.taken == Names{ "", "two" });
            CHECK(studio.saved.size() == 2);
        }
    }

    TEST_CASE_METHOD(alscriptexternaleditor_data, "alscriptexternaleditor: a sync rewrites a changed copy and the compiler's words go in the log", "[alscriptexternaleditor]")
    {
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "one");
        unit.edit(a);
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" two");
        unit.sync(a);
        CHECK(contents(unit.fileName(a)) == std::string("one two"));
        UNSCOPED_INFO("what it holds, known");
        CHECK(a.external->written == std::string("one two"));
        ALScriptCompileResult result;
        result.messages = { "(1, 2) : ERROR : Syntax error\x07" };
        unit.log(a, result);
        REQUIRE(a.external->log);
        const std::string log = contents(a.external->log->path());
        {
            INFO("the log: when, then the words, what cannot be printed dropped");
            CHECK(log.find("// [when]") == 0);
            CHECK(log.find("Syntax error\n") != std::string::npos);
            CHECK(log.find('\x07') == std::string::npos);
        }
        Doc& d = tab("d", "x");
        d.file = "/somewhere/d.lsl";
        unit.log(d, result);
        unit.sync(d);
        {
            INFO("a tab not held outside: nothing");
            CHECK_FALSE(d.external->log);
            CHECK_FALSE(d.watch);
        }
    }

    TEST_CASE_METHOD(alscriptexternaleditor_data, "alscriptexternaleditor: letting go tells the bridge and removes the watch and copy and log", "[alscriptexternaleditor]")
    {
        ALScriptExternalEditor& unit = make();
        studio.bridgeThere           = true;
        Doc& a                       = tab("a", "one");
        unit.edit(a);
        ALScriptCompileResult result;
        unit.log(a, result);
        const std::string file = unit.fileName(a);
        REQUIRE(a.external->log);
        const std::string log  = a.external->log->path();
        CHECK(exists(file));
        CHECK(exists(log));
        a.save.fromExternal(1);
        unit.stop(a);
        CHECK(studio.unsubscribed == Names{ "a" });
        CHECK_FALSE(a.external->subscribed);
        {
            INFO("the watch gone, and the studio's copy with it");
            CHECK_FALSE(a.watch);
            CHECK_FALSE(exists(file));
        }
        CHECK_FALSE(exists(log));
        CHECK_FALSE(a.external->log);
        UNSCOPED_INFO("a save under way is no longer the editor's");
        CHECK_FALSE(a.save.external());
    }

    TEST_CASE_METHOD(alscriptexternaleditor_data, "alscriptexternaleditor: keeping the tab's text over a held save lets it go and says so", "[alscriptexternaleditor]")
    {
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "one");
        unit.edit(a);
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" here");
        write(unit.fileName(a), "two");
        unit.changed("a", unit.fileName(a));
        REQUIRE(a.external->waiting);
        CHECK(*a.external->waiting == "two");
        unit.keep(a);
        CHECK_FALSE(a.external->waiting);
        CHECK(studio.taken.empty());
        CHECK(a.editor->text() == "one here");
        REQUIRE_FALSE(services.statuses.empty());
        CHECK(services.statuses.back().find("ExternalKept") == 0);
    }
}
