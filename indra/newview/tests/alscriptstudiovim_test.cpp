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

#include "fsyspath.h"

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>
#include <set>

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;

    // The window, faked: a record of what vim asked of it.
    struct FakeVimWindow : public ALScriptStudioVim::Window, public al_studio_test::QuietTabs, public al_studio_test::QuietSaves
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

        void revert(Doc& doc) override { reverted.push_back(doc.id); }
        bool revertible(const Doc&) const override { return true; }
        Doc* openFileTab(const std::string& path, bool lua) override
        {
            openedFiles.emplace_back(path, lua);
            return nullptr;
        }
        bool readFile(const std::string& path, std::string& text) override
        {
            llifstream in(path, std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return bool(in) || in.eof();
        }
        bool writeFile(const std::string& path, const std::string& text) override
        {
            llofstream out(path, std::ios::binary);
            out << text;
            return bool(out);
        }
        std::vector<std::string> fileFolders(const Doc&) const override { return folders; }
        bool goToProblemNumber(Doc&, S32 number) override
        {
            problemNumbers.push_back(number);
            return hasProblems;
        }
        std::vector<S32> problemNumbers;
        bool             hasProblems = true;
        void jumpedFrom(Doc& doc, const ALTextView& view, const ALTextPos& from) override
        {
            jumps.push_back(doc.id + (&view == doc.expandedEditor ? " expanded " : " ") + std::to_string(from.line));
        }
        Names jumps;
        bool openIncluded(Doc&, const std::string& name, std::optional<bool>) override
        {
            includedAsked.push_back(name);
            return included.count(name) != 0;
        }
        Names                 includedAsked;
        std::set<std::string> included;
        std::string vimrc(std::string& whence) override
        {
            whence = "the vimrc";
            return vimrcText;
        }
        void        editVimrc() override { ++vimrcEdited; }
        void        refreshEditors() override { ++editorsRefreshed; }
        std::string vimrcText;
        S32         vimrcEdited = 0, editorsRefreshed = 0;

        al_studio_test::FakeServices*               services = nullptr;
        std::vector<std::string>                    folders;
        std::vector<std::pair<std::string, bool>>   openedFiles;
        Names                                       reverted;
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
        bool                               problemsThere = false, problemsShown = false, spellCheck = false;
        // A folder of files, where a test makes one.
        std::string                        folder;

        ~alscriptstudiovim_data()
        {
            if (!folder.empty())
            {
                std::error_code ignored;
                std::filesystem::remove_all(fsyspath(folder), ignored);
            }
        }
        // The folder, with main.lsl, notes.txt, lib/util.luau and a hidden
        // file; the tabs' folder to look for files in.
        void files()
        {
            const fsyspath made = std::filesystem::temp_directory_path() / fsyspath("alscriptstudiovim_" + LLUUID::generateNewID().asString());
            folder              = made.string();
            std::filesystem::create_directories(fsyspath(folder + "/lib"));
            for (const auto& [name, text] : { std::pair{ "main.lsl", "default {}\n" }, std::pair{ "notes.txt", "a note" },
                                              std::pair{ "lib/util.luau", "local x = 1\nreturn x\n" }, std::pair{ ".hidden", "" } })
            {
                llofstream(folder + "/" + name, std::ios::binary) << text;
            }
            studio.folders = { folder };
        }
        std::string in(const std::string& name) const { return fsyspath((fsyspath(folder) / fsyspath(name)).lexically_normal()).string(); }
        static std::string contents(const std::string& path)
        {
            llifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        // What vim over a tab's editor says, where one is put over it.
        ALVimKeymap* vimOver(Doc& doc)
        {
            auto         keymap = std::make_unique<ALVimKeymap>();
            ALVimKeymap* over   = keymap.get();
            vim->connect(*keymap);
            doc.editor->setModalKeymap(std::move(keymap));
            return over;
        }

        ALScriptStudioVim& make()
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            for (const char* name : { "save", "close", "save_all", "format", "quick_fix", "fix_all", "back", "forward", "go_to_definition",
                                      "reference", "go_to_symbol", "blanks_all", "blanks_none" })
            {
                commands.add(name, [this, name]() { ran.push_back(name); });
            }
            for (const char* name : { "next_problem", "previous_problem" })
            {
                commands.add(name, [this, name]() { ran.push_back(name); }, [this]() { return problemsThere; });
            }
            commands.add(
                "spell_check",
                [this]() {
                    ran.push_back("spell_check");
                    spellCheck = !spellCheck;
                },
                nullptr, [this]() { return spellCheck; });
            commands.add(
                "problems",
                [this]() {
                    ran.push_back("problems");
                    problemsShown = !problemsShown;
                },
                nullptr, [this]() { return problemsShown; });
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
            vim             = std::make_unique<ALScriptStudioVim>(services, studio, studio, commands, studio);
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
        static void type(Doc& doc, const std::string& text)
        {
            doc.editor->setCaret(doc.editor->document().end());
            doc.editor->insertText(text);
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
        connected.hooks().complete(*a.editor, std::string(), std::string(), words);
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
        vim.complete(std::string(), std::string(), names);
        vim.complete("set", std::string(), options);
        vim.complete("his", std::string(), kinds);
        vim.complete("bd", std::string(), tabs);
        vim.complete("q", std::string(), none);
        ensure("vim's own and the menus'", std::find(names.begin(), names.end(), "wall") != names.end() &&
                                               std::find(names.begin(), names.end(), "go_to_line") != names.end());
        ensure("the options",
               options == Names{ "number", "nonumber", "relativenumber", "norelativenumber", "spell", "nospell", "list", "nolist" });
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

    template<> template<>
    void alscriptstudiovim_object::test<11>()
    {
        set_test_name(":e opens a file where the tab's folders have it, or a tab by its name or file, # the one before; alone, reads the tab again");
        ALScriptStudioVim& vim = make();
        files();
        Doc&         a     = tab("alpha");
        ALVimKeymap* said  = vimOver(a);
        a.language.lua     = true;
        tab("beta");
        vim.pump();

        ensure("taken", ex(a, "e", "main.lsl"));
        ensure("an LSL file as LSL", studio.openedFiles.size() == 1 && studio.openedFiles.back() == std::make_pair(in("main.lsl"), false));
        ex(a, "edit", "lib/util.luau");
        ensure("in a folder under it, as SLua",
               studio.openedFiles.size() == 2 && studio.openedFiles.back() == std::make_pair(in("lib/util.luau"), true));
        ex(a, "e", "notes.txt");
        ensure("anything else as the tab it was named from is",
               studio.openedFiles.size() == 3 && studio.openedFiles.back() == std::make_pair(in("notes.txt"), true));
        ex(a, "e", in("main.lsl"));
        ensure("where a whole path says", studio.openedFiles.size() == 4 && studio.openedFiles.back().first == in("main.lsl"));
        ex(a, "tabe", "main.lsl");
        ensure(":tabedit the same", studio.openedFiles.size() == 5);
        ex(a, "e", "nowhere.lsl");
        ensure_equals("nowhere: said", said->message(), std::string("VimNoFile [FILE]=nowhere.lsl"));
        ensure("and nothing opened", studio.openedFiles.size() == 5);
        ex(a, "tabnew");
        ensure_equals(":tabnew wants a file", said->message(), std::string("VimArgumentRequired"));

        ex(a, "e", "beta");
        ensure_equals("a tab by its name", services.frontDoc()->id, std::string("beta"));
        vim.pump();
        ex(a, "e", "#");
        ensure_equals("# the one before", services.frontDoc()->id, std::string("alpha"));
        Doc& main = tab("mainfile");
        main.file = in("main.lsl");
        ex(a, "e", "main.lsl");
        ensure("a file open already is gone to", services.frontDoc() == &main && studio.openedFiles.size() == 5);

        ex(a, "e");
        ensure("alone: read again", studio.reverted == Names{ "alpha" });
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" ");
        ex(a, "e");
        ensure_equals("not over unsaved work", said->message(), std::string("VimNotSaved"));
        ensure("left", studio.reverted.size() == 1);
        ex(a, "e!");
        ensure("but with !", studio.reverted == Names{ "alpha", "alpha" });
    }

    template<> template<>
    void alscriptstudiovim_object::test<12>()
    {
        set_test_name(":r puts a file below the caret's line; :w writes the text to one, over one there only with !; :update, :wqa");
        make();
        files();
        Doc&         a    = tab("alpha", "one\ntwo");
        ALVimKeymap* said = vimOver(a);
        ex(a, "r", "lib/util.luau");
        ensure_equals("below the line, its own last break dropped", a.editor->text(), std::string("one\nlocal x = 1\nreturn x\ntwo"));
        ensure("the caret on the first line put in", a.editor->caret() == ALTextPos(1, 0));
        ex(a, "read", "nowhere");
        ensure_equals("nowhere: said", said->message(), std::string("VimNoFile [FILE]=nowhere"));
        ex(a, "r");
        ensure_equals("no file of its own", said->message(), std::string("VimNoFileName"));
        a.expandedEditor = editor("expanded_alpha", "x");
        ensure("not into the expansion", vim->command(*a.expandedEditor, "r", "main.lsl") && a.expandedEditor->text() == "x");

        ex(a, "w", "out.lsl");
        ensure_equals("written", contents(in("out.lsl")), a.editor->text());
        const std::string written = "VimWritten [BYTES]=" + std::to_string(a.editor->text().size()) + " [FILE]=" + in("out.lsl") + " [LINES]=4 [QUOTED]=\"" + in("out.lsl") + "\"";
        ensure_equals("and said", said->message(), written);
        ensure("not as an error", !said->messageIsError());
        ensure("the tab left as it was", ran.empty());
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText("!");
        ex(a, "w", "out.lsl");
        ensure_equals("not over one there", said->message(), std::string("VimFileExists"));
        ensure("left", contents(in("out.lsl")) != a.editor->text());
        ex(a, "w!", "out.lsl");
        ensure_equals("but with !", contents(in("out.lsl")), a.editor->text());
        ex(a, "w", "!ls");
        ensure_equals("no shell", said->message(), std::string("VimBadArgument"));

        ex(a, "update");
        ensure(":update over unsaved work saves", ran == Names{ "save" });
        a.editor->resetDirty();
        ex(a, "up");
        ensure("over none, nothing", ran.size() == 1);
        tab("beta");
        type(a, "?");
        ex(a, "wqa");
        ensure(":wqa saves the unsaved to close, and closes the rest",
               studio.savedToClose == Names{ "alpha" } && studio.asked == Names{ "beta" });
    }

    template<> template<>
    void alscriptstudiovim_object::test<13>()
    {
        set_test_name("Tab after :e, :r and :w offers the files and folders in the tabs' folders, or where a whole path says; hidden ones where asked");
        ALScriptStudioVim& vim = make();
        files();
        tab("alpha");
        auto offered = [&vim](const char* command, const std::string& typed) {
            std::vector<std::string> out;
            vim.complete(command, typed, out);
            std::sort(out.begin(), out.end());
            return out;
        };
        ensure("the folder's", offered("e", std::string()) == Names{ "lib/", "main.lsl", "notes.txt" });
        ensure("in a folder under it", offered("edit", "lib/u") == Names{ "lib/util.luau" });
        ensure("by the start of a name", offered("r", "ma") == Names{ "main.lsl" });
        ensure("after :w", offered("w", "n") == Names{ "notes.txt" });
        ensure("hidden where a dot is typed", offered("e", ".") == Names{ ".hidden" });
        ensure("where a whole path says", offered("tabe", folder + "/li") == Names{ folder + "/lib/" });
        ensure("not after anything else", offered("q", std::string()).empty());
    }

    template<> template<>
    void alscriptstudiovim_object::test<14>()
    {
        set_test_name("the Problems tab is vim's quickfix and location lists: :cn and :cp step, :cc by number, :cfirst, :clast, :copen, :cclose");
        make();
        Doc&         a    = tab("a");
        ALVimKeymap* said = vimOver(a);
        ex(a, "cn");
        ensure_equals("none: said", said->message(), std::string("VimNoErrors"));
        ensure("nothing run", ran.empty());

        problemsThere = true;
        ensure(":cn, :cnext 2, :lne", ex(a, "cn") && ex(a, "cnext", "2") && ex(a, "lne"));
        ensure("each a step", ran == Names(4, "next_problem"));
        ran.clear();
        ensure(":cp, :cN, :lprevious 2", ex(a, "cp") && ex(a, "cN") && ex(a, "lprevious", "2"));
        ensure("each a step back", ran == Names(4, "previous_problem"));
        ex(a, "cn", "x");
        ensure_equals("a count that is none", said->message(), std::string("VimBadArgument"));

        for (const auto& [name, args] : { std::pair{ "cc", "3" }, std::pair{ "cc", "" }, std::pair{ "ll", "2" },
                                          std::pair{ "cfirst", "" }, std::pair{ "cr", "" }, std::pair{ "lfir", "" },
                                          std::pair{ "clast", "" }, std::pair{ "lla", "" } })
        {
            ensure(name, ex(a, name, args));
        }
        ensure("by number: 0 at the caret, 1 the first, -1 the last",
               studio.problemNumbers == std::vector<S32>{ 3, 0, 2, 1, 1, 1, -1, -1 });
        studio.hasProblems = false;
        ex(a, "cc", "1");
        ensure_equals("none to go to: said", said->message(), std::string("VimNoErrors"));

        ran.clear();
        ex(a, "copen");
        ensure(":copen shows the tab", ran == Names{ "problems" } && problemsShown);
        ex(a, "cw");
        ensure("shown already: left", ran.size() == 1);
        ex(a, "ccl");
        ensure(":cclose folds it", ran.size() == 2 && !problemsShown);
        ex(a, "lcl");
        ensure("folded already: left", ran.size() == 2);
        ex(a, "clist");
        ensure(":clist shows it", problemsShown);
    }

    template<> template<>
    void alscriptstudiovim_object::test<15>()
    {
        set_test_name("vim's jumps are the window's to go back to; Ctrl-O, Ctrl-I, K, :tag and :pop through the table; :file; lists to Output");
        make();
        Doc&         a    = tab("a", "one\ntwo\nthree\nfour");
        ALVimKeymap* said = vimOver(a);
        a.expandedEditor  = editor("expanded_a", "x\ny");

        said->hooks().jumped(*a.editor, ALTextPos(2, 0));
        said->hooks().jumped(*a.expandedEditor, ALTextPos(1, 0));
        ensure("a jump kept where it began, in the view it was in", studio.jumps == Names{ "a 2", "a expanded 1" });

        ensure("Ctrl-O, Ctrl-I, K", said->hooks().command(*a.editor, "back", std::string()) &&
                                        said->hooks().command(*a.editor, "forward", std::string()) &&
                                        said->hooks().command(*a.editor, "reference", std::string()));
        ensure(":tag, :pop", ex(a, "tag") && ex(a, "po"));
        ensure("each its command", ran == Names{ "back", "forward", "reference", "go_to_definition", "back" });
        ex(a, "tag", "llSay");
        ensure_equals("a name to go to is not taken", said->message(), std::string("VimBadArgument"));

        a.editor->setCaret(ALTextPos(1, 0));
        ex(a, "file");
        ensure_equals(":file", said->message(), std::string("VimFileInfo [LINES]=4 [NAME]=a [PERCENT]=50 [QUOTED]=\"a\" [STATE]="));
        ensure("not as an error", !said->messageIsError());
        a.editor->insertText("!");
        ex(a, "f");
        ensure_equals("unsaved", said->message(), std::string("VimFileInfo [LINES]=4 [NAME]=a [PERCENT]=50 [QUOTED]=\"a\" [STATE]=VimStateModified "));

        said->hooks().listing(*a.editor, "mark line  col file/text");
        ensure("a listing to Output",
               studio.entries.size() == 1 && studio.entries[0].text == "mark line  col file/text" && studio.outputShown == 1);
    }

    template<> template<>
    void alscriptstudiovim_object::test<16>()
    {
        set_test_name(":find opens an include or a module the script names, else a file as :e finds one, else says it is nowhere");
        make();
        files();
        Doc&         a    = tab("a");
        ALVimKeymap* said = vimOver(a);
        studio.included   = { "lib/util.lsl" };
        ensure("taken", ex(a, "find", "lib/util.lsl"));
        ensure("the include, by the name written", studio.includedAsked == Names{ "lib/util.lsl" } && studio.openedFiles.empty());
        ex(a, "fin", "main.lsl");
        ensure("not an include: a file where :e would find it",
               studio.openedFiles.size() == 1 && studio.openedFiles.back().first == in("main.lsl"));
        ex(a, "tabfind", "nowhere.lsl");
        ensure_equals("nowhere", said->message(), std::string("VimNotInPath [NAME]=nowhere.lsl"));
        ex(a, "find");
        ensure_equals("a name wanted", said->message(), std::string("VimArgumentRequired"));
    }

    template<> template<>
    void alscriptstudiovim_object::test<17>()
    {
        set_test_name(":set spell and :set list are the window's; z= picks from a list over the editor; gO the symbols");
        make();
        Doc&         a    = tab("a");
        ALVimKeymap* said = vimOver(a);
        ensure(":set spell", ex(a, "set", "spell") && spellCheck && ran == Names{ "spell_check" });
        ensure("on already: left", ex(a, "set", "spell") && ran.size() == 1);
        ensure(":set nospell", ex(a, "set", "nospell") && !spellCheck);
        ensure(":set list, :set nolist", ex(a, "set", "list") && ex(a, "set", "nolist"));
        ensure("the blanks all, then none", ran == Names{ "spell_check", "spell_check", "blanks_all", "blanks_none" });

        std::vector<size_t> chose;
        said->hooks().pick(*a.editor, "teh", { "the", "ten" }, [&chose](size_t index) { chose.push_back(index); });
        ensure("offered, in order", studio.offered == Names{ "0", "1" });
        ensure_equals("asking of the word", studio.asking, std::string("VimSuggestPlaceholder"));
        ensure_equals("as tall as they are", studio.pickRows, 2);
        studio.pick("1");
        ensure("the one picked", chose == std::vector<size_t>{ 1 });

        ran.clear();
        ensure("gO", said->hooks().command(*a.editor, "go_to_symbol", std::string()) && ran == Names{ "go_to_symbol" });
    }

    template<> template<>
    void alscriptstudiovim_object::test<18>()
    {
        set_test_name("the vimrc read into the window's vim: the window's options through its toggles, each editor's set on it, what it did not take to Output");
        make();
        bool wrapped = false;
        commands.add(
            "word_wrap",
            [this, &wrapped]() {
                ran.push_back("word_wrap");
                wrapped = !wrapped;
            },
            nullptr, [&wrapped]() { return wrapped; });
        Doc& a           = tab("a");
        studio.vimrcText = "set nu wrap ts=2 et\nlet mapleader = ','\ninoremap jk <Esc>\nbogus\n";
        ensure("not read yet", !vim->sourced());
        vim->source();
        ensure("read", vim->sourced() && lineNumbers && wrapped);
        ensure("the window's editors told", studio.editorsRefreshed == 1);
        const ALVimMappings& maps = vim->shared().mappings;
        ensure("its mapping, and its leader", maps.match(ALVimMappings::INSERT, maps.keysOf("jk"), true).full && maps.leader() == ",");
        ensure("what it did not take in the Output tab, not brought up", !studio.entries.empty() &&
                                                                        studio.entries.back().text.find("line 4:") != std::string::npos &&
                                                                        studio.entries.back().text.find("the vimrc") != std::string::npos &&
                                                                        studio.outputShown == 0);
        vim->applyViewOptions(*a.editor);
        ensure("each editor's own set on it", a.editor->getTabWidth() == 2 && a.editor->getSoftTabs());

        // :set's ? shown, and the toggles.
        ALVimKeymap* said = vimOver(a);
        ensure(":set nu?", ex(a, "set", "nu?") && said->message() == "  number");
        ensure(":set invwrap", ex(a, "set", "invwrap") && !wrapped);

        // :source reads it again, or a tab as it; :e $MYVIMRC opens it.
        studio.vimrcText = "nnoremap Q gq\n";
        ensure(":so", ex(a, "so") && maps.match(ALVimMappings::NORMAL, maps.keysOf("Q"), true).full &&
                          !maps.match(ALVimMappings::INSERT, maps.keysOf("jk"), true).full);
        type(a, "\nbad line");
        const bool         read  = ex(a, "source", "%");
        const std::string& entry = studio.entries.back().text;
        ensure(":so % reads the tab, and brings up what it did not take",
               read && studio.outputShown == 1 && entry.find("line 2:") != std::string::npos && entry.find("the vimrc") == std::string::npos);
        ensure(":e $MYVIMRC", ex(a, "e", "$MYVIMRC") && studio.vimrcEdited == 1);
        ensure(":so of a file that is not there", ex(a, "so", "nowhere.vim") && said->messageIsError());
    }
}
