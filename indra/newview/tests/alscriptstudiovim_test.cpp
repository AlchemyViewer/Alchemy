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
        // As the window does: the tab in front, and the strip's order.
        void activate(Doc& doc) override
        {
            for (size_t i = 0; i < services->docs.size(); ++i)
            {
                if (services->docs[i].get() == &doc)
                {
                    services->front = static_cast<S32>(i);
                }
            }
        }
        void reorderTabs(const std::vector<std::string>& order) override
        {
            const std::string front = services->frontDoc() ? services->frontDoc()->id : std::string();
            std::vector<std::unique_ptr<Doc>> reordered;
            for (const std::string& id : order)
            {
                for (std::unique_ptr<Doc>& doc : services->docs)
                {
                    if (doc && doc->id == id)
                    {
                        reordered.push_back(std::move(doc));
                    }
                }
            }
            services->docs = std::move(reordered);
            if (Doc* doc = services->findDoc(front))
            {
                activate(*doc);
            }
        }

        al_studio_test::FakeServices*            services = nullptr;
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
            studio.services = &services;
            vim             = std::make_unique<ALScriptStudioVim>(services, commands, studio);
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
        // Four tabs, alpha in front, with vim over alpha's editor to say
        // what goes wrong.
        ALVimKeymap* fourTabs()
        {
            for (const char* id : { "alpha", "beta", "gamma", "delta" })
            {
                tab(id);
            }
            auto         keymap = std::make_unique<ALVimKeymap>();
            ALVimKeymap* said   = keymap.get();
            vim->connect(*keymap);
            services.docs[0]->editor->setModalKeymap(std::move(keymap));
            vim->pump();
            return said;
        }
        // A : command typed in alpha's editor, and the tab in front after it,
        // which the window saw.
        std::string go(const std::string& name, const std::string& args = std::string())
        {
            vim->command(*services.findDoc(std::string_view("alpha"))->editor, name, args);
            vim->pump();
            return services.frontDoc() ? services.frontDoc()->id : std::string();
        }
        std::string order() const
        {
            std::string out;
            for (const std::unique_ptr<Doc>& doc : services.docs)
            {
                out += (out.empty() ? "" : " ") + doc->id;
            }
            return out;
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
        set_test_name("Tab completes the command names, :set's options, :history's kinds and the tabs' names");
        ALScriptStudioVim&       vim = make();
        tab("alpha");
        tab("beta");
        std::vector<std::string> names, options, kinds, tabs, none;
        vim.complete(std::string(), names);
        vim.complete("set", options);
        vim.complete("his", kinds);
        vim.complete("bd", tabs);
        vim.complete("w", none);
        ensure("vim's own and the menus'", std::find(names.begin(), names.end(), "wall") != names.end() &&
                                               std::find(names.begin(), names.end(), "go_to_line") != names.end());
        ensure("the options", options == Names{ "number", "nonumber", "relativenumber", "norelativenumber" });
        ensure("the kinds", kinds == Names{ "all", "cmd", "search" });
        ensure("the tabs", tabs == Names{ "alpha", "beta" });
        ensure("nothing after anything else", none.empty());
    }

    template<> template<>
    void alscriptstudiovim_object::test<8>()
    {
        set_test_name("the tabs are vim's buffers and tab pages: :bn and :bp round the strip, :b by number, name or #, first and last");
        make();
        ALVimKeymap* vim = fourTabs();
        ensure_equals(":bn", go("bn"), std::string("beta"));
        ensure_equals(":bnext 2", go("bnext", "2"), std::string("delta"));
        ensure_equals("round past the end", go("bn"), std::string("alpha"));
        ensure_equals(":bp round past the start", go("bp"), std::string("delta"));
        ensure_equals(":bN 3", go("bN", "3"), std::string("alpha"));
        ensure_equals(":b 3", go("b", "3"), std::string("gamma"));
        ensure_equals(":b # the one before", go("b", "#"), std::string("alpha"));
        ensure_equals("and back", go("buffer", "#"), std::string("gamma"));
        ensure_equals(":b by a part of its name, in any case", go("b", "ELT"), std::string("delta"));
        ensure_equals(":b by its whole name", go("b", "beta"), std::string("beta"));
        ensure_equals(":bf", go("bf"), std::string("alpha"));
        ensure_equals(":blast", go("blast"), std::string("delta"));
        ensure_equals(":tabfirst", go("tabfirst"), std::string("alpha"));
        ensure_equals(":tabl", go("tabl"), std::string("delta"));
        ensure_equals(":tabn round past the end", go("tabn"), std::string("alpha"));
        ensure_equals(":tabn 3 the third", go("tabn", "3"), std::string("gamma"));
        ensure_equals(":tabn +1", go("tabnext", "+1"), std::string("delta"));
        ensure_equals(":tabn $", go("tabn", "$"), std::string("delta"));
        ensure_equals(":tabp 2", go("tabp", "2"), std::string("beta"));
        ensure_equals(":tabN round past the start", go("tabN", "2"), std::string("delta"));

        ensure_equals(":b a -- in every name", go("b", "a"), std::string("delta"));
        ensure_equals("said as vim says it", vim->message(), std::string("VimTabsMatch [NAME]=a"));
        ensure("an error", vim->messageIsError());
        go("b", "zeta");
        ensure_equals("none", vim->message(), std::string("VimNoTabMatch [NAME]=zeta"));
        go("b", "9");
        ensure_equals("no ninth", vim->message(), std::string("VimNoSuchTab [NUMBER]=9"));
        go("tabn", "9");
        ensure_equals("nor to go to", vim->message(), std::string("VimBadArgument"));
        ensure_equals("still where it was", services.frontDoc()->id, std::string("delta"));
    }

    template<> template<>
    void alscriptstudiovim_object::test<9>()
    {
        set_test_name(":bd and :tabclose close a tab, asked about or with ! let go of; :tabonly the others; :tabmove moves the one in front");
        make();
        ALVimKeymap* vim = fourTabs();
        go("b", "beta");
        ensure(":bd the one in front, asked about", go("bd") == "beta" && studio.asked == Names{ "beta" });
        ensure(":bd! let go of", (go("bd!"), studio.letGo == Names{ "beta" }));
        ensure(":bd by number", (go("bd", "3"), studio.asked == Names{ "beta", "gamma" }));
        ensure(":tabclose! by number", (go("tabclose!", "4"), studio.letGo == Names{ "beta", "delta" }));
        ensure(":tabonly asks about the others at once", (go("tabonly"), studio.many == Names{ "alpha", "gamma", "delta" }));
        studio.letGo.clear();
        ensure(":tabonly! lets them go", (go("tabo!"), studio.letGo == Names{ "alpha", "gamma", "delta" }));

        ensure_equals(":tabm 0 to the start", (go("tabm", "0"), order()), std::string("beta alpha gamma delta"));
        ensure_equals(":tabm to the end", (go("tabmove"), order()), std::string("alpha gamma delta beta"));
        ensure_equals(":tabm 2 after the second", (go("tabm", "2"), order()), std::string("alpha gamma beta delta"));
        ensure_equals(":tabm -1", (go("tabm", "-1"), order()), std::string("alpha beta gamma delta"));
        ensure_equals(":tabm 3 after the third, past itself", (go("tabm", "3"), order()), std::string("alpha gamma beta delta"));
        ensure_equals("still in front", services.frontDoc()->id, std::string("beta"));
        go("tabm", "+5");
        ensure_equals("not past the end", vim->message(), std::string("VimBadArgument"));
        ensure_equals("where it was", order(), std::string("alpha gamma beta delta"));
    }

    template<> template<>
    void alscriptstudiovim_object::test<10>()
    {
        set_test_name(":ls lists the tabs as vim lists its buffers: the one in front, the one before, unsaved, unchangeable, and their lines");
        make();
        fourTabs();
        go("b", "gamma");
        go("b", "beta");
        Doc& gamma = *services.findDoc(std::string_view("gamma"));
        gamma.editor->setCaret(gamma.editor->document().end());
        gamma.editor->insertText("\n\n");
        services.findDoc(std::string_view("delta"))->modifiable = false;
        ensure("taken", ex(*services.docs[0], "ls"));
        ensure_equals("one entry", studio.entries.size(), size_t(1));
        ensure_equals("the tabs", studio.entries[0].text,
                      std::string("tabs:\n"
                                  "  1  h   \"alpha\"                        line 1\n"
                                  "  2 %a   \"beta\"                         line 1\n"
                                  "  3 #h + \"gamma\"                        line 3\n"
                                  "  4  h-  \"delta\"                        line 1"));
        ensure("the tab in sight", studio.outputShown == 1);
    }
}
