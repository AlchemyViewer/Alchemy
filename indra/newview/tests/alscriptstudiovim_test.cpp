/**
 * @file alscriptstudiovim_test.cpp
 * @brief Script Studio's side of vim over the window's commands and its side faked: the : commands, :set, :history, q:, = and the banner.
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

#include "../alscriptstudiovim.h"

#include "../alscriptstudiocommands.h"
#include "alscriptstudio_fixture.h"

#include "../test/lltut.h"

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;

    // The window, faked: a record of what vim asked of it.
    struct FakeVimWindow : public ALScriptStudioVim::Window
    {
        void closeDocument(std::string_view id) override { asked.push_back(std::string(id)); }
        void letGoOf(Doc& doc) override { letGo.push_back(doc.id); }
        void saveToClose(const std::string& id) override { savedToClose.push_back(id); }
        void closeMany(const std::vector<std::string>& ids) override { many = ids; }
        void format(Doc& doc, bool selection_only) override { formatted.push_back(doc.id + (selection_only ? " selection" : "")); }
        void output(const ALOutputView::Entry& entry) override { entries.push_back(entry); }
        void showOutput() override { ++outputShown; }
        void pickLine(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string&, S32 rows,
                      std::function<void(const std::string&)> chosen, std::function<void(const std::string&)> shifted,
                      std::function<void()> cancelled) override
        {
            offered.clear();
            for (const ALQuickOpen::Candidate& c : candidates)
            {
                offered.push_back(c.value);
            }
            asking   = placeholder;
            pickRows = rows;
            pick     = std::move(chosen);
            shift    = std::move(shifted);
            cancel   = std::move(cancelled);
        }
        void refreshTrailer(Doc&) override { ++trailers; }

        Names                                    asked, letGo, savedToClose, many, formatted, offered;
        std::vector<ALOutputView::Entry>         entries;
        S32                                      outputShown = 0, trailers = 0, pickRows = 0;
        std::string                              asking;
        std::function<void(const std::string&)> pick, shift;
        std::function<void()>                    cancel;
    };
}

namespace tut
{
    struct alscriptstudiovim_data
    {
        // The UI, and the editors' home; the services are plain, so that
        // what is said is a word's name and its blanks.
        al_studio_test::StudioWindow       window;
        al_studio_test::FakeServices       services;
        ALScriptStudioCommands             commands;
        FakeVimWindow                      studio;
        std::unique_ptr<ALScriptStudioVim> vim;
        // The table's commands run, by name; and two of its toggles.
        Names                              ran;
        bool                               lineNumbers = false, relativeNumbers = false;

        ALScriptStudioVim& make()
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            for (const char* name : { "save", "close", "save_all", "format", "quick_fix", "fix_all", "problems" })
            {
                commands.add(name, [this, name]() { ran.push_back(name); });
            }
            commands.add("revert", [this]() { ran.push_back("revert"); }, []() { return false; });
            commands.add(
                "line_numbers",
                [this]() {
                    ran.push_back("line_numbers");
                    lineNumbers = !lineNumbers;
                },
                nullptr, [this]() { return lineNumbers; });
            commands.add(
                "relative_numbers",
                [this]() {
                    ran.push_back("relative_numbers");
                    relativeNumbers = !relativeNumbers;
                },
                nullptr, [this]() { return relativeNumbers; });
            vim = std::make_unique<ALScriptStudioVim>(services, commands, studio);
            return *vim;
        }

        ALCodeEditor* editor(const std::string& name, const std::string& text)
        {
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name        = name;
            p.rect        = LLRect(0, 200, 400, 0);
            p.syntax      = "lsl";
            ALCodeEditor* made = LLUICtrlFactory::create<ALCodeEditor>(p);
            made->setFont(LLFontGL::getFontMonospace());
            window.floater->addChild(made);
            made->setText(text);
            made->resetDirty();
            return made;
        }
        // A script's tab, loaded and changeable, over an editor named as the
        // window names its tabs' editors.
        Doc& tab(const std::string& id, const std::string& text = "default {}")
        {
            Doc& doc       = services.addDoc(id);
            doc.loaded     = true;
            doc.modifiable = true;
            doc.editor     = editor("editor_" + id, text);
            return doc;
        }
        bool ex(Doc& doc, const std::string& name, const std::string& args = std::string())
        {
            return vim->command(*doc.editor, name, args);
        }
    };

    typedef test_group<alscriptstudiovim_data> alscriptstudiovim_group;
    typedef alscriptstudiovim_group::object    alscriptstudiovim_object;
    alscriptstudiovim_group                    alscriptstudiovim_instance("alscriptstudiovim");

    template<> template<>
    void alscriptstudiovim_object::test<1>()
    {
        set_test_name("each : command reaches its command: the menus' through the window's table, the rest through the window");
        ALScriptStudioVim& vim = make();
        Doc&               a   = tab("a");
        tab("b");

        for (const char* name : { "w", "write", "w!" })
        {
            ensure(name, ex(a, name));
        }
        ensure("written by the menus' Save", ran == Names{ "save", "save", "save" });
        ran.clear();
        ensure("q", ex(a, "q") && ex(a, "wa") && ex(a, "format") && ex(a, "fix") && ex(a, "fixall") && ex(a, "problems"));
        ensure("each by its menu name", ran == Names{ "close", "save_all", "format", "quick_fix", "fix_all", "problems" });
        ran.clear();
        ensure("one that cannot be done now is taken, and not done", ex(a, "revert") && ran.empty());

        ensure("q! lets it go as it stands", ex(a, "q!") && studio.letGo == Names{ "a" });
        ensure("wq over nothing unsaved closes it", ex(a, "wq") && studio.asked == Names{ "a" } && studio.savedToClose.empty());
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" ");
        ensure("wq over something unsaved saves it to close", ex(a, "x") && studio.savedToClose == Names{ "a" });
        ensure("qa closes them all, asking once", ex(a, "qa") && studio.many == Names{ "a", "b" });
        studio.letGo.clear();
        ensure("qa! lets them all go", ex(a, "qa!") && studio.letGo == Names{ "a", "b" });

        ensure("one it does not know is vim's to say", !ex(a, "frobnicate"));
        ALCodeEditor* stray = editor("stray", "x");
        ensure("nor from an editor of no tab", !vim.command(*stray, "w", std::string()) && ran.empty());
    }

    template<> template<>
    void alscriptstudiovim_object::test<2>()
    {
        set_test_name(":set turns number and relativenumber on or off through their toggles, only where they are not so already");
        make();
        Doc& a = tab("a");
        ensure("on", ex(a, "set", "number") && lineNumbers && ran == Names{ "line_numbers" });
        ensure("on already: left", ex(a, "set", "nu") && ran.size() == 1);
        ensure("off", ex(a, "set", "nonu") && !lineNumbers && ran.size() == 2);
        ensure("relative", ex(a, "set", "rnu") && relativeNumbers);
        ensure("off already: left", ex(a, "set", "norelativenumber") && !relativeNumbers && ran.size() == 4);
        ensure("an option it does not know is vim's", !ex(a, "set", "hlsearch"));
    }

    template<> template<>
    void alscriptstudiovim_object::test<3>()
    {
        set_test_name(":history lists the lines entered in the Output tab: the : ones, the searches, or both");
        ALScriptStudioVim& vim = make();
        Doc&               a   = tab("a");
        vim.shared().command   = { "w", "set nu" };
        vim.shared().search    = { "foo" };

        ensure("the : ones", ex(a, "history"));
        ensure_equals("one entry", studio.entries.size(), size_t(1));
        ensure_equals("numbered", studio.entries[0].text, std::string("cmd history:\n  1  w\n  2  set nu"));
        ensure_equals("from vim", studio.entries[0].source, std::string("OutputSourceVim"));
        ensure("the tab in sight", studio.outputShown == 1);

        studio.entries.clear();
        ensure("the searches", ex(a, "his", "/") && studio.entries.size() == 1 && studio.entries[0].text == "search history:\n  1  foo");
        studio.entries.clear();
        ensure("both", ex(a, "history", "all") && studio.entries.size() == 2);
    }

    template<> template<>
    void alscriptstudiovim_object::test<4>()
    {
        set_test_name("q: lists the lines entered, the last first; Return runs one, Shift-Return puts it back on the line");
        ALScriptStudioVim& vim = make();
        Doc&               a   = tab("a");
        std::vector<std::pair<std::string, bool>> chosen;
        vim.historyWindow(*a.editor, ':', { "first", "second", "third" },
                          [&chosen](const std::string& line, bool run) { chosen.emplace_back(line, run); });
        ensure("the last first", studio.offered == Names{ "third", "second", "first" });
        ensure_equals("as tall as they are", studio.pickRows, 3);
        ensure_equals("asking of the kind", studio.asking, std::string("VimHistoryPlaceholder [KIND]=:"));
        studio.pick("second");
        studio.shift("first");
        ensure("run, and put back", chosen == std::vector<std::pair<std::string, bool>>{ { "second", true }, { "first", false } });
        ensure("none picked is nothing chosen", (studio.cancel(), chosen.size() == 2));

        vim.historyWindow(*a.editor, '/', Names(20, "x"), [](const std::string&, bool) {});
        ensure_equals("no taller than eight", studio.pickRows, 8);
        vim.historyWindow(*a.editor, '/', Names(), [](const std::string&, bool) {});
        ensure_equals("nor shorter than one", studio.pickRows, 1);
    }

    template<> template<>
    void alscriptstudiovim_object::test<5>()
    {
        set_test_name("= formats the lines given of the source; not of the expansion, a notecard or a tab that cannot change");
        ALScriptStudioVim& vim = make();
        Doc&               a   = tab("a", "one\ntwo\nthree\nfour");
        vim.format(*a.editor, 1, 2);
        ensure("formatted, as a selection", studio.formatted == Names{ "a selection" });
        const ALTextDocument& text = a.editor->document();
        ensure("of those lines", a.editor->selection() == ALTextRange(text.lineStart(1), text.lineEnd(2)));
        vim.format(*a.editor, 2, 99);
        ensure("to the last there is", a.editor->selection() == ALTextRange(text.lineStart(2), text.lineEnd(3)));

        a.expandedEditor = editor("expanded_a", "one");
        vim.format(*a.expandedEditor, 0, 0);
        ensure("not the expansion", studio.formatted.size() == 2);
        Doc& card    = tab("card");
        card.notecard = true;
        vim.format(*card.editor, 0, 0);
        Doc& locked      = tab("locked");
        locked.modifiable = false;
        vim.format(*locked.editor, 0, 0);
        ensure("nor a notecard, nor one that cannot change", studio.formatted.size() == 2);
        ensure("the expansion's editor is its tab's", vim.docOf(*a.expandedEditor) == &a);
    }

    template<> template<>
    void alscriptstudiovim_object::test<6>()
    {
        set_test_name("a keymap connected shares the window's history and asks it; the banner says vim where the tab in front has it");
        ALScriptStudioVim& vim = make();
        Doc&               a   = tab("a");

        vim.pump();
        ensure("no vim, nothing said", vim.banner().empty() && studio.trailers == 0);

        auto keymap = std::make_unique<ALVimKeymap>();
        vim.connect(*keymap);
        ALVimKeymap& connected = *keymap;
        a.editor->setModalKeymap(std::move(keymap));
        ensure("its history the window's", connected.sharedState().get() == &vim.shared());
        ensure("a : command it leaves reaches the table",
               connected.hooks().command(*a.editor, "w", std::string()) && ran == Names{ "save" });
        std::vector<std::string> words;
        connected.hooks().complete(*a.editor, std::string(), words);
        ensure("Tab completes the studio's names", std::find(words.begin(), words.end(), "fixall") != words.end());

        vim.pump();
        ensure_equals("vim, said", vim.banner(), std::string("VimNormal"));
        ensure("the strip told", studio.trailers == 1);
        vim.pump();
        ensure("once", studio.trailers == 1);

        // Gone, the keymap's hooks do nothing.
        this->vim.reset();
        ensure("asked of nothing", !connected.hooks().command(*a.editor, "w", std::string()) && ran.size() == 1);
    }

    template<> template<>
    void alscriptstudiovim_object::test<7>()
    {
        set_test_name("Tab completes the command names, :set's options and :history's kinds");
        std::vector<std::string> names, options, kinds, none;
        ALScriptStudioVim::complete(std::string(), names);
        ALScriptStudioVim::complete("set", options);
        ALScriptStudioVim::complete("his", kinds);
        ALScriptStudioVim::complete("w", none);
        ensure("vim's own and the menus'", std::find(names.begin(), names.end(), "wall") != names.end() &&
                                               std::find(names.begin(), names.end(), "go_to_line") != names.end());
        ensure("the options", options == Names{ "number", "nonumber", "relativenumber", "norelativenumber" });
        ensure("the kinds", kinds == Names{ "all", "cmd", "search" });
        ensure("nothing after anything else", none.empty());
    }
}
