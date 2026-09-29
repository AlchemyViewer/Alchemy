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

#include "../lllogchat.h"
#include "alscriptstudio_fixture.h"

#include "fsyspath.h"

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>
#include <optional>

// The log's first line says when; the chat log's clock is the viewer's.
std::string LLLogChat::timestamp2LogString(U32, bool)
{
    return "[when]";
}

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;

    // The window, faked: a record of what the external editor asked of it.
    struct FakeExternalWindow : public ALScriptExternalEditor::Window
    {
        void watchFile(Doc& doc) override { watched.push_back(doc.id); }
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
        Names       watched, taken, saved, subscribed, unsubscribed, started;
        // Held as the workspace holds them, the lists in a folder of the
        // run's own.
        std::string                        lists  = (std::filesystem::temp_directory_path() / ("alscriptexternaleditor_" + bridge)).string();
        std::optional<ALScriptTempFiles>   copies = std::make_optional<ALScriptTempFiles>(lists, "test");
    };
}

namespace tut
{
    struct alscriptexternaleditor_data
    {
        al_studio_test::StudioWindow            window;
        al_studio_test::FakeServices            services;
        FakeExternalWindow                      studio;
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            unit = std::make_unique<ALScriptExternalEditor>(services, studio);
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

    typedef test_group<alscriptexternaleditor_data> alscriptexternaleditor_group;
    typedef alscriptexternaleditor_group::object    alscriptexternaleditor_object;
    alscriptexternaleditor_group                    alscriptexternaleditor_instance("alscriptexternaleditor");

    template<> template<>
    void alscriptexternaleditor_object::test<1>()
    {
        set_test_name("the copy's name: the tab's without what a file system refuses, the bridge's id, the language's extension, in the temp folder");
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "x");
        a.name                       = "my<scr>ipt:?";
        const std::string temp       = LLFile::tmpdir();
        ensure_equals("LSL", unit.fileName(a), temp + "sl_script_myscript_" + studio.bridge + ".lsl");
        a.language.lua = true;
        a.name         = "<>";
        ensure_equals("SLua, and a name of nothing but those", unit.fileName(a), temp + "sl_script_" + studio.bridge + ".luau");
    }

    template<> template<>
    void alscriptexternaleditor_object::test<2>()
    {
        set_test_name("Edit Externally writes the copy and watches it, tells the bridge and starts the editor; a file on disk as it is; a notecard not");
        ALScriptExternalEditor& unit = make();
        studio.bridgeThere           = true;
        Doc&              a          = tab("a", "default {}");
        unit.edit(a);
        const std::string file = unit.fileName(a);
        ensure_equals("the copy", contents(file), std::string("default {}"));
        ensure("watched", a.external.watch && a.external.watch->path() == file);
        ensure("the log beside it", a.external.log && a.external.log->path() == file + ".log");
        ensure("the bridge told", studio.subscribed == Names{ "a" } && a.external.subscribed);
        ensure("the editor started on it", studio.started == Names{ file });
        ensure_equals("what it holds, known", a.external.written, std::string("default {}"));

        Doc& e = tab("e", "");
        unit.edit(e);
        ensure_equals("an empty script is one space", contents(unit.fileName(e)), std::string(" "));

        Doc& d = tab("d", "x");
        d.file = "/somewhere/d.lsl";
        unit.edit(d);
        ensure("a file on disk watched where it is", studio.watched == Names{ "d" } && studio.started.back() == "/somewhere/d.lsl on disk");
        ensure("with no log, and nothing for the bridge", !d.external.log && studio.subscribed.size() == 2);

        Doc& n    = tab("n", "x");
        n.notecard = true;
        unit.edit(n);
        ensure("a notecard not", studio.started.size() == 3 && !n.external.watch);
    }

    template<> template<>
    void alscriptexternaleditor_object::test<3>()
    {
        set_test_name("a save outside over nothing changed here is taken, as one step, and saved from here");
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "one");
        unit.edit(a);
        write(unit.fileName(a), "two");
        unit.changed("a", unit.fileName(a));
        ensure("taken", studio.taken == Names{ "two" } && a.editor->text() == "two");
        ensure("saved from here, as the editor's", studio.saved == Names{ "a" } && a.save.external());
        ensure_equals("what the copy holds, known", a.external.written, std::string("two"));
        ensure("nothing asked", !asked());
        a.editor->resetDirty();
        unit.changed("a", unit.fileName(a));
        ensure("the same again, with the tab saved: nothing to take or save", studio.taken.size() == 1 && studio.saved.size() == 1);
        unit.changed("nobody", unit.fileName(a));
        ensure("a tab gone: nothing", studio.taken.size() == 1);
    }

    template<> template<>
    void alscriptexternaleditor_object::test<4>()
    {
        set_test_name("a save outside over changes made here is held and asked about; taken when the author says so");
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "one");
        unit.edit(a);
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" here");
        write(unit.fileName(a), "two");
        unit.changed("a", unit.fileName(a));
        ensure("not taken", studio.taken.empty() && a.editor->text() == "one here");
        ensure("held", a.external.waiting && *a.external.waiting == "two");
        ensure("and asked about", asked() && services.reports.back().failure && services.reports.back().doc == "a");
        unit.take(a, *a.external.waiting);
        ensure("taken once said", studio.taken == Names{ "two" } && !a.external.waiting && studio.saved == Names{ "a" });
    }

    template<> template<>
    void alscriptexternaleditor_object::test<5>()
    {
        set_test_name("a save with CRLF endings after one taken is taken, not asked about");
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "one");
        unit.edit(a);
        write(unit.fileName(a), "a\r\nb");
        unit.changed("a", unit.fileName(a));
        ensure("the first, its endings as the editor keeps them", studio.taken == Names{ "a\nb" });
        write(unit.fileName(a), "a\r\nb\r\nc");
        unit.changed("a", unit.fileName(a));
        ensure("the second taken too", studio.taken == Names{ "a\nb", "a\nb\nc" } && !asked());
    }

    template<> template<>
    void alscriptexternaleditor_object::test<6>()
    {
        set_test_name("an emptied copy waits a moment; looked at again, taken if still empty, and left where the editor's second step came");
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "one");
        unit.edit(a);
        write(unit.fileName(a), "");
        unit.changed("a", unit.fileName(a));
        ensure("not taken at once", studio.taken.empty());
        unit.changed("a", unit.fileName(a), true);
        ensure("still empty a moment later: taken", studio.taken == Names{ "" });
        write(unit.fileName(a), "two");
        unit.changed("a", unit.fileName(a));
        unit.changed("a", unit.fileName(a), true);
        ensure("the second step heard first: the look after takes nothing more",
               studio.taken == Names{ "", "two" } && studio.saved.size() == 2);
    }

    template<> template<>
    void alscriptexternaleditor_object::test<7>()
    {
        set_test_name("after a save from here the copy is written again where it holds something else; the compiler's words go in the log");
        ALScriptExternalEditor& unit = make();
        Doc&                    a    = tab("a", "one");
        unit.edit(a);
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" two");
        unit.sync(a);
        ensure_equals("written again", contents(unit.fileName(a)), std::string("one two"));
        ensure_equals("and known", a.external.written, std::string("one two"));
        ALScriptCompileResult result;
        result.messages = { "(1, 2) : ERROR : Syntax error\x07" };
        unit.log(a, result);
        const std::string log = contents(a.external.log->path());
        ensure("the log: when, then the words, what cannot be printed dropped",
               log.find("// [when]") == 0 && log.find("Syntax error\n") != std::string::npos && log.find('\x07') == std::string::npos);
        Doc& d = tab("d", "x");
        d.file = "/somewhere/d.lsl";
        unit.log(d, result);
        unit.sync(d);
        ensure("a tab not held outside: nothing", !d.external.log && !d.external.watch);
    }

    template<> template<>
    void alscriptexternaleditor_object::test<8>()
    {
        set_test_name("letting go tells the bridge, and the watch, the copy and the log go");
        ALScriptExternalEditor& unit = make();
        studio.bridgeThere           = true;
        Doc& a                       = tab("a", "one");
        unit.edit(a);
        ALScriptCompileResult result;
        unit.log(a, result);
        const std::string file = unit.fileName(a);
        const std::string log  = a.external.log->path();
        ensure("both there", exists(file) && exists(log));
        a.save.fromExternal(1);
        unit.stop(a);
        ensure("the bridge told", studio.unsubscribed == Names{ "a" } && !a.external.subscribed);
        ensure("the watch gone, and the studio's copy with it", !a.external.watch && !exists(file));
        ensure("the log gone", !exists(log) && !a.external.log);
        ensure("a save under way is no longer the editor's", !a.save.external());
    }
}
