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

#include <catch2/catch_test_macros.hpp>

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
        // A tab, as each role this fakes names it.
        typedef ALScriptStudioDoc Doc;

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

namespace
{
    struct alscriptstudiovim_data
    {
        // The UI, and the editors' home; the services are plain, so that
        // what is said is a word's name and its blanks.
        al_studio_test::StudioWindow                      window;
        al_studio_test::FakeServices                      services;
        ALScriptStudioCommands                            commands;
        FakeVimWindow                                     studio;
        std::unique_ptr<al_studio_test::StudioNavigation> navigation;
        std::unique_ptr<ALScriptStudioVim>                vim;
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
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
            navigation      = std::make_unique<al_studio_test::StudioNavigation>(services);
            vim             = std::make_unique<ALScriptStudioVim>(services, studio, studio, navigation->unit, commands, studio);
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

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: each : command reaches the menus' table or the window", "[alscriptstudiovim]")
    {
        ALScriptStudioVim& vim = make();
        Doc&               a   = tab("a");
        tab("b");

        for (const char* name : { "w", "write", "w!" })
        {
            CAPTURE(name);
            REQUIRE(ex(a, name));
        }
        UNSCOPED_INFO("written by the menus' Save");
        CHECK(ran == Names{ "save", "save", "save" });
        ran.clear();
        CHECK(ex(a, "q"));
        CHECK(ex(a, "wa"));
        CHECK(ex(a, "format"));
        CHECK(ex(a, "fix"));
        CHECK(ex(a, "fixall"));
        CHECK(ex(a, "problems"));
        UNSCOPED_INFO("each by its menu name");
        CHECK(ran == Names{ "close", "save_all", "format", "quick_fix", "fix_all", "problems" });
        ran.clear();
        {
            INFO("one that cannot be done now is taken, and not done");
            CHECK(ex(a, "revert"));
            CHECK(ran.empty());
        }

        {
            INFO("q! lets it go as it stands");
            CHECK(ex(a, "q!"));
            CHECK(studio.letGo == Names{ "a" });
        }
        {
            INFO("wq over nothing unsaved closes it");
            CHECK(ex(a, "wq"));
            CHECK(studio.asked == Names{ "a" });
            CHECK(studio.savedToClose.empty());
        }
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" ");
        {
            INFO("wq over something unsaved saves it to close");
            CHECK(ex(a, "wq"));
            CHECK(studio.savedToClose == Names{ "a" });
        }
        studio.savedToClose.clear();
        {
            INFO("and x the same");
            CHECK(ex(a, "x"));
            CHECK(studio.savedToClose == Names{ "a" });
        }
        {
            INFO("qa closes them all, asking once");
            CHECK(ex(a, "qa"));
            CHECK(studio.many == Names{ "a", "b" });
        }
        studio.letGo.clear();
        {
            INFO("qa! lets them all go");
            CHECK(ex(a, "qa!"));
            CHECK(studio.letGo == Names{ "a", "b" });
        }

        UNSCOPED_INFO("one it does not know is vim's to say");
        CHECK_FALSE(ex(a, "frobnicate"));
        ALCodeEditor* stray = editor("stray", "x");
        {
            INFO("nor from an editor of no tab");
            CHECK_FALSE(vim.command(*stray, "w", std::string()));
            CHECK(ran.empty());
        }
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: :set toggles number and relativenumber only when they differ", "[alscriptstudiovim]")
    {
        make();
        Doc& a = tab("a");
        CHECK(ex(a, "set", "number"));
        CHECK(lineNumbers);
        CHECK(ran == Names{ "line_numbers" });
        {
            INFO("on already: left");
            CHECK(ex(a, "set", "nu"));
            CHECK(ran.size() == 1);
        }
        CHECK(ex(a, "set", "nonu"));
        CHECK_FALSE(lineNumbers);
        CHECK(ran.size() == 2);
        CHECK(ex(a, "set", "rnu"));
        CHECK(relativeNumbers);
        {
            INFO("on: turned off");
            CHECK(ex(a, "set", "norelativenumber"));
            CHECK_FALSE(relativeNumbers);
            CHECK(ran.size() == 4);
        }
        {
            INFO("off already: left");
            CHECK(ex(a, "set", "nornu"));
            CHECK_FALSE(relativeNumbers);
            CHECK(ran.size() == 4);
        }
        UNSCOPED_INFO("an option it does not know is vim's");
        CHECK_FALSE(ex(a, "set", "hlsearch"));
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: :history lists command or search lines or both in Output", "[alscriptstudiovim]")
    {
        ALScriptStudioVim& vim = make();
        Doc&               a   = tab("a");
        vim.shared().command   = { "w", "set nu" };
        vim.shared().search    = { "foo" };

        UNSCOPED_INFO("the : ones");
        CHECK(ex(a, "history"));
        REQUIRE(studio.entries.size() == size_t(1));
        CHECK(studio.entries[0].text == std::string("cmd history:\n  1  w\n  2  set nu"));
        CHECK(studio.entries[0].source == std::string("OutputSourceVim"));
        UNSCOPED_INFO("the tab in sight");
        CHECK(studio.outputShown == 1);

        studio.entries.clear();
        {
            INFO("the searches");
            CHECK(ex(a, "his", "/"));
            REQUIRE(studio.entries.size() == 1);
            CHECK(studio.entries[0].text == "search history:\n  1  foo");
        }
        studio.entries.clear();
        CHECK(ex(a, "history", "all"));
        CHECK(studio.entries.size() == 2);
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: q: lists lines last first to run or to put back on the line", "[alscriptstudiovim]")
    {
        ALScriptStudioVim& vim = make();
        Doc&               a   = tab("a");
        std::vector<std::pair<std::string, bool>> chosen;
        vim.historyWindow(*a.editor, ':', { "first", "second", "third" },
                          [&chosen](const std::string& line, bool run) { chosen.emplace_back(line, run); });
        UNSCOPED_INFO("the last first");
        CHECK(studio.offered == Names{ "third", "second", "first" });
        UNSCOPED_INFO("as tall as they are");
        CHECK(studio.pickRows == 3);
        UNSCOPED_INFO("asking of the kind");
        CHECK(studio.asking == std::string("VimHistoryPlaceholder [KIND]=:"));
        studio.pick("second");
        studio.shift("first");
        UNSCOPED_INFO("run, and put back");
        CHECK(chosen == std::vector<std::pair<std::string, bool>>{ { "second", true }, { "first", false } });
        UNSCOPED_INFO("none picked is nothing chosen");
        CHECK((studio.cancel(), chosen.size() == 2));

        vim.historyWindow(*a.editor, '/', Names(20, "x"), [](const std::string&, bool) {});
        UNSCOPED_INFO("no taller than eight");
        CHECK(studio.pickRows == 8);
        vim.historyWindow(*a.editor, '/', Names(), [](const std::string&, bool) {});
        UNSCOPED_INFO("nor shorter than one");
        CHECK(studio.pickRows == 1);
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: = formats the given lines of a changeable script's source only", "[alscriptstudiovim]")
    {
        ALScriptStudioVim& vim = make();
        Doc&               a   = tab("a", "one\ntwo\nthree\nfour");
        vim.format(*a.editor, 1, 2);
        UNSCOPED_INFO("formatted, as a selection");
        CHECK(studio.formatted == Names{ "a selection" });
        const ALTextDocument& text = a.editor->document();
        UNSCOPED_INFO("of those lines");
        CHECK(a.editor->selection() == ALTextRange(text.lineStart(1), text.lineEnd(2)));
        vim.format(*a.editor, 2, 99);
        UNSCOPED_INFO("to the last there is");
        CHECK(a.editor->selection() == ALTextRange(text.lineStart(2), text.lineEnd(3)));

        a.expandedEditor = editor("expanded_a", "one");
        vim.format(*a.expandedEditor, 0, 0);
        UNSCOPED_INFO("not the expansion");
        CHECK(studio.formatted.size() == 2);
        Doc& card    = tab("card");
        card.notecard = true;
        vim.format(*card.editor, 0, 0);
        Doc& locked      = tab("locked");
        locked.modifiable = false;
        vim.format(*locked.editor, 0, 0);
        UNSCOPED_INFO("nor a notecard, nor one that cannot change");
        CHECK(studio.formatted.size() == 2);
        UNSCOPED_INFO("the expansion's editor is its tab's");
        CHECK(vim.docOf(*a.expandedEditor) == &a);
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: a connected keymap shares the window's history and shows the banner", "[alscriptstudiovim]")
    {
        ALScriptStudioVim& vim = make();
        Doc&               a   = tab("a");

        vim.pump();
        {
            INFO("no vim, nothing said");
            CHECK(vim.banner().empty());
            CHECK(studio.trailers == 0);
        }

        auto keymap = std::make_unique<ALVimKeymap>();
        vim.connect(*keymap);
        ALVimKeymap& connected = *keymap;
        a.editor->setModalKeymap(std::move(keymap));
        UNSCOPED_INFO("its history the window's");
        CHECK(connected.sharedState().get() == &vim.shared());
        {
            INFO("a : command it leaves reaches the table");
            CHECK(connected.hooks().command(*a.editor, "w", std::string()));
            CHECK(ran == Names{ "save" });
        }
        std::vector<std::string> words;
        connected.hooks().complete(*a.editor, std::string(), std::string(), words);
        UNSCOPED_INFO("Tab completes the studio's names");
        CHECK(std::find(words.begin(), words.end(), "fixall") != words.end());

        vim.pump();
        CHECK(vim.banner() == std::string("VimNormal"));
        UNSCOPED_INFO("the strip told");
        CHECK(studio.trailers == 1);
        vim.pump();
        UNSCOPED_INFO("told once");
        CHECK(studio.trailers == 1);

        // Gone, the keymap's hooks do nothing.
        this->vim.reset();
        {
            INFO("asked of nothing");
            CHECK_FALSE(connected.hooks().command(*a.editor, "w", std::string()));
            CHECK(ran.size() == 1);
        }
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: Tab completes commands and their options and kinds and tab names", "[alscriptstudiovim]")
    {
        ALScriptStudioVim&       vim = make();
        tab("alpha");
        tab("beta");
        std::vector<std::string> names, options, kinds, tabs, none;
        vim.complete(std::string(), std::string(), names);
        vim.complete("set", std::string(), options);
        vim.complete("his", std::string(), kinds);
        vim.complete("bd", std::string(), tabs);
        vim.complete("q", std::string(), none);
        {
            INFO("vim's own and the menus'");
            CHECK(std::find(names.begin(), names.end(), "wall") != names.end());
            CHECK(std::find(names.begin(), names.end(), "go_to_line") != names.end());
        }
        CHECK(options == Names{ "number", "nonumber", "relativenumber", "norelativenumber", "spell", "nospell", "list", "nolist" });
        CHECK(kinds == Names{ "all", "cmd", "search" });
        CHECK(tabs == Names{ "alpha", "beta" });
        UNSCOPED_INFO("nothing after anything else");
        CHECK(none.empty());
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: :b and :tab commands move between tabs round the strip", "[alscriptstudiovim]")
    {
        make();
        ALVimKeymap* vim = fourTabs();
        CHECK(go("bn") == std::string("beta"));
        CHECK(go("bnext", "2") == std::string("delta"));
        UNSCOPED_INFO("round past the end");
        CHECK(go("bn") == std::string("alpha"));
        UNSCOPED_INFO(":bp round past the start");
        CHECK(go("bp") == std::string("delta"));
        CHECK(go("bN", "3") == std::string("alpha"));
        CHECK(go("b", "3") == std::string("gamma"));
        UNSCOPED_INFO(":b # the one before");
        CHECK(go("b", "#") == std::string("alpha"));
        UNSCOPED_INFO("and back");
        CHECK(go("buffer", "#") == std::string("gamma"));
        UNSCOPED_INFO(":b by a part of its name, in any case");
        CHECK(go("b", "ELT") == std::string("delta"));
        CHECK(go("b", "beta") == std::string("beta"));
        CHECK(go("bf") == std::string("alpha"));
        CHECK(go("blast") == std::string("delta"));
        CHECK(go("tabfirst") == std::string("alpha"));
        CHECK(go("tabl") == std::string("delta"));
        UNSCOPED_INFO(":tabn round past the end");
        CHECK(go("tabn") == std::string("alpha"));
        CHECK(go("tabn", "3") == std::string("gamma"));
        CHECK(go("tabnext", "+1") == std::string("delta"));
        CHECK(go("tabn", "$") == std::string("delta"));
        CHECK(go("tabp", "2") == std::string("beta"));
        UNSCOPED_INFO(":tabN round past the start");
        CHECK(go("tabN", "2") == std::string("delta"));

        UNSCOPED_INFO(":b a -- in every name");
        CHECK(go("b", "a") == std::string("delta"));
        UNSCOPED_INFO("said as vim says it");
        CHECK(vim->message() == std::string("VimTabsMatch [NAME]=a"));
        CHECK(vim->messageIsError());
        go("b", "zeta");
        CHECK(vim->message() == std::string("VimNoTabMatch [NAME]=zeta"));
        go("b", "9");
        CHECK(vim->message() == std::string("VimNoSuchTab [NUMBER]=9"));
        go("tabn", "9");
        UNSCOPED_INFO("nor to go to");
        CHECK(vim->message() == std::string("VimBadArgument"));
        REQUIRE(services.frontDoc());
        UNSCOPED_INFO("still where it was");
        CHECK(services.frontDoc()->id == std::string("delta"));
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: :bd and :tabclose and :tabonly close tabs and :tabmove moves one", "[alscriptstudiovim]")
    {
        make();
        ALVimKeymap* vim = fourTabs();
        go("b", "beta");
        {
            INFO(":bd the one in front, asked about");
            CHECK(go("bd") == "beta");
            CHECK(studio.asked == Names{ "beta" });
        }
        UNSCOPED_INFO(":bd! let go of");
        CHECK((go("bd!"), studio.letGo == Names{ "beta" }));
        UNSCOPED_INFO(":bd by number");
        CHECK((go("bd", "3"), studio.asked == Names{ "beta", "gamma" }));
        UNSCOPED_INFO(":tabclose! by number");
        CHECK((go("tabclose!", "4"), studio.letGo == Names{ "beta", "delta" }));
        UNSCOPED_INFO(":tabonly asks about the others at once");
        CHECK((go("tabonly"), studio.many == Names{ "alpha", "gamma", "delta" }));
        studio.letGo.clear();
        UNSCOPED_INFO(":tabonly! lets them go");
        CHECK((go("tabo!"), studio.letGo == Names{ "alpha", "gamma", "delta" }));

        UNSCOPED_INFO(":tabm 0 to the start");
        CHECK((go("tabm", "0"), order()) == std::string("beta alpha gamma delta"));
        UNSCOPED_INFO(":tabm to the end");
        CHECK((go("tabmove"), order()) == std::string("alpha gamma delta beta"));
        UNSCOPED_INFO(":tabm 2 after the second");
        CHECK((go("tabm", "2"), order()) == std::string("alpha gamma beta delta"));
        CHECK((go("tabm", "-1"), order()) == std::string("alpha beta gamma delta"));
        UNSCOPED_INFO(":tabm 3 after the third, past itself");
        CHECK((go("tabm", "3"), order()) == std::string("alpha gamma beta delta"));
        REQUIRE(services.frontDoc());
        UNSCOPED_INFO("still in front");
        CHECK(services.frontDoc()->id == std::string("beta"));
        go("tabm", "+5");
        UNSCOPED_INFO("not past the end");
        CHECK(vim->message() == std::string("VimBadArgument"));
        UNSCOPED_INFO("where it was");
        CHECK(order() == std::string("alpha gamma beta delta"));
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: :ls lists tabs with their flags and lines as vim lists buffers", "[alscriptstudiovim]")
    {
        make();
        fourTabs();
        go("b", "gamma");
        go("b", "beta");
        Doc& gamma = *services.findDoc(std::string_view("gamma"));
        gamma.editor->setCaret(gamma.editor->document().end());
        gamma.editor->insertText("\n\n");
        services.findDoc(std::string_view("delta"))->modifiable = false;
        CHECK(ex(*services.docs[0], "ls"));
        REQUIRE(studio.entries.size() == size_t(1));
        CHECK(studio.entries[0].text ==
              std::string("tabs:\n"
                          "  1  h   \"alpha\"                        line 1\n"
                          "  2 %a   \"beta\"                         line 1\n"
                          "  3 #h + \"gamma\"                        line 3\n"
                          "  4  h-  \"delta\"                        line 1"));
        UNSCOPED_INFO("the tab in sight");
        CHECK(studio.outputShown == 1);
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: :e opens a file from the tab's folders or goes to a tab or reverts", "[alscriptstudiovim]")
    {
        ALScriptStudioVim& vim = make();
        files();
        Doc&         a     = tab("alpha");
        ALVimKeymap* said  = vimOver(a);
        a.language.lua     = true;
        tab("beta");
        vim.pump();

        CHECK(ex(a, "e", "main.lsl"));
        {
            INFO("an LSL file as LSL");
            REQUIRE(studio.openedFiles.size() == 1);
            CHECK(studio.openedFiles.back() == std::make_pair(in("main.lsl"), false));
        }
        ex(a, "edit", "lib/util.luau");
        {
            INFO("in a folder under it, as SLua");
            REQUIRE(studio.openedFiles.size() == 2);
            CHECK(studio.openedFiles.back() == std::make_pair(in("lib/util.luau"), true));
        }
        ex(a, "e", "notes.txt");
        {
            INFO("anything else as the tab it was named from is");
            REQUIRE(studio.openedFiles.size() == 3);
            CHECK(studio.openedFiles.back() == std::make_pair(in("notes.txt"), true));
        }
        ex(a, "e", in("main.lsl"));
        {
            INFO("where a whole path says");
            REQUIRE(studio.openedFiles.size() == 4);
            CHECK(studio.openedFiles.back().first == in("main.lsl"));
        }
        ex(a, "tabe", "main.lsl");
        UNSCOPED_INFO(":tabedit the same");
        CHECK(studio.openedFiles.size() == 5);
        ex(a, "e", "nowhere.lsl");
        UNSCOPED_INFO("nowhere: said");
        CHECK(said->message() == std::string("VimNoFile [FILE]=nowhere.lsl"));
        UNSCOPED_INFO("and nothing opened");
        CHECK(studio.openedFiles.size() == 5);
        ex(a, "tabnew");
        UNSCOPED_INFO(":tabnew wants a file");
        CHECK(said->message() == std::string("VimArgumentRequired"));

        ex(a, "e", "beta");
        REQUIRE(services.frontDoc());
        UNSCOPED_INFO("a tab by its name");
        CHECK(services.frontDoc()->id == std::string("beta"));
        vim.pump();
        ex(a, "e", "#");
        REQUIRE(services.frontDoc());
        UNSCOPED_INFO("# the one before");
        CHECK(services.frontDoc()->id == std::string("alpha"));
        Doc& main = tab("mainfile");
        main.file = in("main.lsl");
        ex(a, "e", "main.lsl");
        {
            INFO("a file open already is gone to");
            CHECK(services.frontDoc() == &main);
            CHECK(studio.openedFiles.size() == 5);
        }

        ex(a, "e");
        UNSCOPED_INFO("alone: read again");
        CHECK(studio.reverted == Names{ "alpha" });
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText(" ");
        ex(a, "e");
        UNSCOPED_INFO("not over unsaved work");
        CHECK(said->message() == std::string("VimNotSaved"));
        UNSCOPED_INFO("left");
        CHECK(studio.reverted.size() == 1);
        ex(a, "e!");
        UNSCOPED_INFO("but with !");
        CHECK(studio.reverted == Names{ "alpha", "alpha" });
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: :r reads a file in and :w and :update and :wqa write", "[alscriptstudiovim]")
    {
        make();
        files();
        Doc&         a    = tab("alpha", "one\ntwo");
        ALVimKeymap* said = vimOver(a);
        ex(a, "r", "lib/util.luau");
        UNSCOPED_INFO("below the line, its own last break dropped");
        CHECK(a.editor->text() == std::string("one\nlocal x = 1\nreturn x\ntwo"));
        UNSCOPED_INFO("the caret on the first line put in");
        CHECK(a.editor->caret() == ALTextPos(1, 0));
        ex(a, "read", "nowhere");
        UNSCOPED_INFO("nowhere: said");
        CHECK(said->message() == std::string("VimNoFile [FILE]=nowhere"));
        ex(a, "r");
        UNSCOPED_INFO("no file of its own");
        CHECK(said->message() == std::string("VimNoFileName"));
        a.expandedEditor = editor("expanded_alpha", "x");
        {
            INFO("not into the expansion");
            CHECK(vim->command(*a.expandedEditor, "r", "main.lsl"));
            CHECK(a.expandedEditor->text() == "x");
        }

        ex(a, "w", "out.lsl");
        CHECK(contents(in("out.lsl")) == a.editor->text());
        const std::string written = "VimWritten [BYTES]=" + std::to_string(a.editor->text().size()) + " [FILE]=" + in("out.lsl") + " [LINES]=4 [QUOTED]=\"" + in("out.lsl") + "\"";
        CHECK(said->message() == written);
        CHECK_FALSE(said->messageIsError());
        UNSCOPED_INFO("the tab left as it was");
        CHECK(ran.empty());
        a.editor->setCaret(a.editor->document().end());
        a.editor->insertText("!");
        ex(a, "w", "out.lsl");
        UNSCOPED_INFO("not over one there");
        CHECK(said->message() == std::string("VimFileExists"));
        UNSCOPED_INFO("left");
        CHECK(contents(in("out.lsl")) != a.editor->text());
        ex(a, "w!", "out.lsl");
        UNSCOPED_INFO("but with !");
        CHECK(contents(in("out.lsl")) == a.editor->text());
        ex(a, "w", "!ls");
        UNSCOPED_INFO("no shell");
        CHECK(said->message() == std::string("VimBadArgument"));

        ex(a, "update");
        UNSCOPED_INFO(":update over unsaved work saves");
        CHECK(ran == Names{ "save" });
        a.editor->resetDirty();
        ex(a, "up");
        UNSCOPED_INFO("over none, nothing");
        CHECK(ran.size() == 1);
        tab("beta");
        type(a, "?");
        ex(a, "wqa");
        {
            INFO(":wqa saves the unsaved to close, and closes the rest");
            CHECK(studio.savedToClose == Names{ "alpha" });
            CHECK(studio.asked == Names{ "beta" });
        }
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: Tab after a file command offers files and folders from the tab's folders", "[alscriptstudiovim]")
    {
        ALScriptStudioVim& vim = make();
        files();
        tab("alpha");
        auto offered = [&vim](const char* command, const std::string& typed) {
            std::vector<std::string> out;
            vim.complete(command, typed, out);
            std::sort(out.begin(), out.end());
            return out;
        };
        UNSCOPED_INFO("the folder's");
        CHECK(offered("e", std::string()) == Names{ "lib/", "main.lsl", "notes.txt" });
        UNSCOPED_INFO("in a folder under it");
        CHECK(offered("edit", "lib/u") == Names{ "lib/util.luau" });
        UNSCOPED_INFO("by the start of a name");
        CHECK(offered("r", "ma") == Names{ "main.lsl" });
        UNSCOPED_INFO("after :w");
        CHECK(offered("w", "n") == Names{ "notes.txt" });
        UNSCOPED_INFO("hidden where a dot is typed");
        CHECK(offered("e", ".") == Names{ ".hidden" });
        UNSCOPED_INFO("where a whole path says");
        CHECK(offered("tabe", folder + "/li") == Names{ folder + "/lib/" });
        UNSCOPED_INFO("not after anything else");
        CHECK(offered("q", std::string()).empty());
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: the quickfix and location commands step through the Problems tab", "[alscriptstudiovim]")
    {
        make();
        Doc&         a    = tab("a");
        ALVimKeymap* said = vimOver(a);
        ex(a, "cn");
        UNSCOPED_INFO("none: said");
        CHECK(said->message() == std::string("VimNoErrors"));
        UNSCOPED_INFO("nothing run");
        CHECK(ran.empty());

        problemsThere = true;
        CHECK(ex(a, "cn"));
        CHECK(ex(a, "cnext", "2"));
        CHECK(ex(a, "lne"));
        UNSCOPED_INFO("each a step");
        CHECK(ran == Names(4, "next_problem"));
        ran.clear();
        CHECK(ex(a, "cp"));
        CHECK(ex(a, "cN"));
        CHECK(ex(a, "lprevious", "2"));
        UNSCOPED_INFO("each a step back");
        CHECK(ran == Names(4, "previous_problem"));
        ex(a, "cn", "x");
        UNSCOPED_INFO("a count that is none");
        CHECK(said->message() == std::string("VimBadArgument"));

        for (const auto& [name, args] : { std::pair{ "cc", "3" }, std::pair{ "cc", "" }, std::pair{ "ll", "2" },
                                          std::pair{ "cfirst", "" }, std::pair{ "cr", "" }, std::pair{ "lfir", "" },
                                          std::pair{ "clast", "" }, std::pair{ "lla", "" } })
        {
            CAPTURE(name, args);
            REQUIRE(ex(a, name, args));
        }
        UNSCOPED_INFO("by number: 0 at the caret, 1 the first, -1 the last");
        CHECK(studio.problemNumbers == std::vector<S32>{ 3, 0, 2, 1, 1, 1, -1, -1 });
        studio.hasProblems = false;
        ex(a, "cc", "1");
        UNSCOPED_INFO("none to go to: said");
        CHECK(said->message() == std::string("VimNoErrors"));

        ran.clear();
        ex(a, "copen");
        {
            INFO(":copen shows the tab");
            CHECK(ran == Names{ "problems" });
            CHECK(problemsShown);
        }
        ex(a, "cw");
        UNSCOPED_INFO("shown already: left");
        CHECK(ran.size() == 1);
        ex(a, "ccl");
        {
            INFO(":cclose folds it");
            CHECK(ran.size() == 2);
            CHECK_FALSE(problemsShown);
        }
        ex(a, "lcl");
        UNSCOPED_INFO("folded already: left");
        CHECK(ran.size() == 2);
        ex(a, "clist");
        UNSCOPED_INFO(":clist shows it");
        CHECK(problemsShown);
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: vim's jumps and keys and :file and listings go through the window", "[alscriptstudiovim]")
    {
        make();
        Doc&         a    = tab("a", "one\ntwo\nthree\nfour");
        ALVimKeymap* said = vimOver(a);
        a.expandedEditor  = editor("expanded_a", "x\ny");

        said->hooks().jumped(*a.editor, ALTextPos(2, 0));
        said->hooks().jumped(*a.expandedEditor, ALTextPos(1, 0));
        navigation->unit.goBack(false);
        navigation->unit.goBack(false);
        UNSCOPED_INFO("a jump kept where it began, in the view it was in");
        CHECK(navigation->shown == Names{ "a expanded 1", "a 2" });

        {
            INFO("Ctrl-O, Ctrl-I, K");
            CHECK(said->hooks().command(*a.editor, "back", std::string()));
            CHECK(said->hooks().command(*a.editor, "forward", std::string()));
            CHECK(said->hooks().command(*a.editor, "reference", std::string()));
        }
        CHECK(ex(a, "tag"));
        CHECK(ex(a, "po"));
        UNSCOPED_INFO("each its command");
        CHECK(ran == Names{ "back", "forward", "reference", "go_to_definition", "back" });
        ex(a, "tag", "llSay");
        UNSCOPED_INFO("a name to go to is not taken");
        CHECK(said->message() == std::string("VimBadArgument"));

        a.editor->setCaret(ALTextPos(1, 0));
        ex(a, "file");
        UNSCOPED_INFO(":file");
        CHECK(said->message() == std::string("VimFileInfo [LINES]=4 [NAME]=a [PERCENT]=50 [QUOTED]=\"a\" [STATE]="));
        CHECK_FALSE(said->messageIsError());
        a.editor->insertText("!");
        ex(a, "f");
        UNSCOPED_INFO("unsaved");
        CHECK(said->message() == std::string("VimFileInfo [LINES]=4 [NAME]=a [PERCENT]=50 [QUOTED]=\"a\" [STATE]=VimStateModified "));

        said->hooks().listing(*a.editor, "mark line  col file/text");
        {
            INFO("a listing to Output");
            REQUIRE(studio.entries.size() == 1);
            CHECK(studio.entries[0].text == "mark line  col file/text");
            CHECK(studio.outputShown == 1);
        }
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: :find opens an include or else a file as :e would or says it is nowhere", "[alscriptstudiovim]")
    {
        make();
        files();
        Doc&         a    = tab("a");
        ALVimKeymap* said = vimOver(a);
        studio.included   = { "lib/util.lsl" };
        CHECK(ex(a, "find", "lib/util.lsl"));
        {
            INFO("the include, by the name written");
            CHECK(studio.includedAsked == Names{ "lib/util.lsl" });
            CHECK(studio.openedFiles.empty());
        }
        ex(a, "fin", "main.lsl");
        {
            INFO("not an include: a file where :e would find it");
            REQUIRE(studio.openedFiles.size() == 1);
            CHECK(studio.openedFiles.back().first == in("main.lsl"));
        }
        ex(a, "tabfind", "nowhere.lsl");
        CHECK(said->message() == std::string("VimNotInPath [NAME]=nowhere.lsl"));
        ex(a, "find");
        UNSCOPED_INFO("a name wanted");
        CHECK(said->message() == std::string("VimArgumentRequired"));
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: :set spell and :set list and z= and gO go through the window", "[alscriptstudiovim]")
    {
        make();
        Doc&         a    = tab("a");
        ALVimKeymap* said = vimOver(a);
        CHECK(ex(a, "set", "spell"));
        CHECK(spellCheck);
        CHECK(ran == Names{ "spell_check" });
        {
            INFO("on already: left");
            CHECK(ex(a, "set", "spell"));
            CHECK(ran.size() == 1);
        }
        CHECK(ex(a, "set", "nospell"));
        CHECK_FALSE(spellCheck);
        CHECK(ex(a, "set", "list"));
        CHECK(ex(a, "set", "nolist"));
        UNSCOPED_INFO("the blanks all, then none");
        CHECK(ran == Names{ "spell_check", "spell_check", "blanks_all", "blanks_none" });

        std::vector<size_t> chose;
        said->hooks().pick(*a.editor, "teh", { "the", "ten" }, [&chose](size_t index) { chose.push_back(index); });
        UNSCOPED_INFO("offered, in order");
        CHECK(studio.offered == Names{ "0", "1" });
        UNSCOPED_INFO("asking of the word");
        CHECK(studio.asking == std::string("VimSuggestPlaceholder"));
        UNSCOPED_INFO("as tall as they are");
        CHECK(studio.pickRows == 2);
        studio.pick("1");
        UNSCOPED_INFO("the one picked");
        CHECK(chose == std::vector<size_t>{ 1 });

        ran.clear();
        {
            INFO("gO");
            CHECK(said->hooks().command(*a.editor, "go_to_symbol", std::string()));
            CHECK(ran == Names{ "go_to_symbol" });
        }
    }

    TEST_CASE_METHOD(alscriptstudiovim_data, "alscriptstudiovim: the vimrc sets the window's toggles and each editor and reports the rest", "[alscriptstudiovim]")
    {
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
        UNSCOPED_INFO("not read yet");
        CHECK_FALSE(vim->sourced());
        vim->source();
        CHECK(vim->sourced());
        CHECK(lineNumbers);
        CHECK(wrapped);
        UNSCOPED_INFO("the window's editors told");
        CHECK(studio.editorsRefreshed == 1);
        const ALVimMappings& maps = vim->shared().mappings;
        {
            INFO("its mapping, and its leader");
            CHECK(maps.match(ALVimMappings::INSERT, maps.keysOf("jk"), true).full);
            CHECK(maps.leader() == ",");
        }
        {
            INFO("what it did not take in the Output tab, not brought up");
            REQUIRE_FALSE(studio.entries.empty());
            CHECK(studio.entries.back().text.find("line 4:") != std::string::npos);
            CHECK(studio.entries.back().text.find("the vimrc") != std::string::npos);
            CHECK(studio.outputShown == 0);
        }
        vim->applyViewOptions(*a.editor);
        {
            INFO("each editor's own set on it");
            CHECK(a.editor->getTabWidth() == 2);
            CHECK(a.editor->getSoftTabs());
        }

        // :set's ? shown, and the toggles.
        ALVimKeymap* said = vimOver(a);
        CHECK(ex(a, "set", "nu?"));
        CHECK(said->message() == "  number");
        CHECK(ex(a, "set", "invwrap"));
        CHECK_FALSE(wrapped);

        // :source reads it again, or a tab as it; :e $MYVIMRC opens it.
        studio.vimrcText = "nnoremap Q gq\n";
        CHECK(ex(a, "so"));
        CHECK(maps.match(ALVimMappings::NORMAL, maps.keysOf("Q"), true).full);
        CHECK_FALSE(maps.match(ALVimMappings::INSERT, maps.keysOf("jk"), true).full);
        type(a, "\nbad line");
        const bool         read  = ex(a, "source", "%");
        const std::string& entry = studio.entries.back().text;
        {
            INFO(":so % reads the tab, and brings up what it did not take");
            CHECK(read);
            CHECK(studio.outputShown == 1);
            CHECK(entry.find("line 2:") != std::string::npos);
            CHECK(entry.find("the vimrc") == std::string::npos);
        }
        CHECK(ex(a, "e", "$MYVIMRC"));
        CHECK(studio.vimrcEdited == 1);
        {
            INFO(":so of a file that is not there");
            CHECK(ex(a, "so", "nowhere.vim"));
            CHECK(said->messageIsError());
        }
    }
}
