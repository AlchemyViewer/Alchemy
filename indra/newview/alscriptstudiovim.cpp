/**
 * @file alscriptstudiovim.cpp
 * @brief Script Studio's side of vim: the : commands the mode does not answer itself, its = over lines, its history window and its banner.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptstudiovim.h"

#include "alcodeeditor.h"
#include "alscriptstudiocommands.h"
#include "alscriptstudioservices.h"
#include "fsyspath.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace
{
    // The studio's own commands a : line gives by the names its menu knows
    // them by: what command runs and complete offers, one list.
    const char* const VIM_MENU_COMMANDS[] = { "format", "problems", "references", "output", "search", "preferences", "pop_out",
                                              "reveal", "save_all", "revert", "external_editor", "save_file", "save_as", "load_file",
                                              "open_file", "fold_all", "unfold_all", "go_to_line", "quick_fix", "fix_all", "weights" };

    // Whether a command's name is one of vim's, as vim reads its names: any
    // of it from the least it may be shortened to, `least`, to the whole.
    bool abbreviates(const std::string& name, const char* least, const char* whole)
    {
        const size_t shortest = strlen(least);
        return name.size() >= shortest && name.size() <= strlen(whole) && std::string_view(whole).substr(0, name.size()) == name;
    }

    // A count or a tab's number: digits alone; -1 for anything else.
    S32 numberOf(const std::string& text)
    {
        if (text.empty() || text.size() > 6 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        {
            return -1;
        }
        return std::atoi(text.c_str());
    }
}

ALScriptStudioVim::ALScriptStudioVim(ALScriptStudioServices& services, ALScriptStudioCommands& commands, Window& window)
    : mServices(services), mCommands(commands), mWindow(window)
{
}

void ALScriptStudioVim::connect(ALVimKeymap& vim)
{
    vim.share(mShared);
    const std::weak_ptr<bool> alive = mAlive;
    vim.hooks().command             = [this, alive](ALTextView& view, const std::string& name, const std::string& args) {
        return alive.lock() && command(view, name, args);
    };
    vim.hooks().format = [this, alive](ALTextView& view, S32 first, S32 last) {
        if (alive.lock())
        {
            format(view, first, last);
        }
    };
    vim.hooks().historyWindow = [this, alive](ALTextView& view, llwchar kind, const std::vector<std::string>& history,
                                              std::function<void(const std::string&, bool run)> chosen) {
        if (alive.lock())
        {
            historyWindow(view, kind, history, std::move(chosen));
        }
    };
    vim.hooks().complete = [this, alive](ALTextView&, const std::string& command, const std::string& typed, std::vector<std::string>& out) {
        if (alive.lock())
        {
            complete(command, typed, out);
        }
    };
}

ALScriptStudioVim::Doc* ALScriptStudioVim::docOf(const ALTextView& view)
{
    // The editor carries its document's id in its name, which is what
    // makeEditor gave it; the rest are found by what they are.
    const std::string& name = view.getName();
    if (name.compare(0, 7, "editor_") == 0)
    {
        if (Doc* doc = mServices.findDoc(std::string_view(name).substr(7)))
        {
            return doc;
        }
    }
    for (Doc* doc : mServices.openDocs())
    {
        if (doc->editor == &view || doc->expandedEditor == &view)
        {
            return doc;
        }
    }
    return nullptr;
}

void ALScriptStudioVim::pump()
{
    Doc* doc = mServices.frontDoc();
    if (!doc)
    {
        return;
    }
    // The tab in front before this one is the alternate, # to :b and
    // Ctrl-^.
    if (doc->id != mCurrent)
    {
        if (!mCurrent.empty())
        {
            mAlternate = mCurrent;
        }
        mCurrent = doc->id;
    }
    // The mode, the : line as it is typed and what the mode says are
    // all in the band the editor draws under its text, where vim has
    // them; the bottom strip says only that vim is on, so that a reader
    // of the strip knows why the keys do what they do.
    ALCodeEditor* shown  = doc->shownText();
    ALVimKeymap*  vim    = shown ? dynamic_cast<ALVimKeymap*>(shown->modalKeymap()) : nullptr;
    std::string   banner = vim ? mServices.words("VimNormal") : std::string();
    if (banner != mBanner)
    {
        mBanner = banner;
        mWindow.refreshTrailer(*doc);
    }
}

void ALScriptStudioVim::historyWindow(ALTextView& view, llwchar kind, const std::vector<std::string>& history,
                                      std::function<void(const std::string&, bool run)> chosen)
{
    // Vim's command-line window as a quick-open over the editor: the
    // lines entered, the last first, ranked as they are typed at; the
    // one picked runs, as the window runs the row Enter is pressed on,
    // or with Shift goes back onto the line to be edited and entered.
    // The editor takes the keyboard back either way.
    std::vector<ALQuickOpen::Candidate> candidates;
    for (size_t i = history.size(); i-- > 0;)
    {
        ALQuickOpen::Candidate c;
        c.label = history[i];
        c.value = history[i];
        candidates.push_back(std::move(c));
    }
    const LLHandle<LLUICtrl> editor = view.getHandle();
    auto                     back   = [editor]() {
        if (LLUICtrl* e = editor.get())
        {
            e->setFocus(true);
        }
    };
    LLStringUtil::format_map_t args;
    args["[KIND]"] = utf8str_from_cp(kind);
    mWindow.pickLine(
        std::move(candidates), mServices.words("VimHistoryPlaceholder", args), mServices.words("VimHistoryTitle", args),
        llclamp(static_cast<S32>(history.size()), 1, 8),
        [chosen, back](const std::string& line) {
            back();
            chosen(line, true);
        },
        [chosen, back](const std::string& line) {
            back();
            chosen(line, false);
        },
        back);
}

bool ALScriptStudioVim::command(ALTextView& view, const std::string& name, const std::string& args)
{
    Doc* doc = docOf(view);
    if (!doc)
    {
        return false;
    }
    if (fileCommand(view, *doc, name, args) || problemCommand(view, *doc, name, args))
    {
        return true;
    }
    // The tab typed in is the one in front, which the menus' commands are
    // about: those that have one go through it.
    if (name == "w" || name == "write" || name == "w!")
    {
        mCommands.run("save");
        return true;
    }
    if (name == "q" || name == "quit" || name == "close")
    {
        mCommands.run("close");
        return true;
    }
    if (name == "q!" || name == "quit!")
    {
        mWindow.letGoOf(*doc);
        return true;
    }
    if (name == "wq" || name == "x" || name == "xit" || name == "wq!" || name == "x!")
    {
        if (doc->unsaved() && doc->modifiable)
        {
            mWindow.saveToClose(doc->id);
        }
        else
        {
            mWindow.closeDocument(doc->id);
        }
        return true;
    }
    if (name == "history" || name == "his")
    {
        // The lines entered, in the Output pane, where a list fits: the :
        // ones, the search ones with / or search, both with all.
        const bool          searches = args == "/" || args == "search" || args == "all";
        const bool          commands = args.empty() || args == ":" || args == "cmd" || args == "all";
        ALOutputView::Entry entry    = listing();
        auto                list     = [&](const std::vector<std::string>& lines, const char* kind) {
            entry.text = std::string(kind) + " history:";
            for (size_t i = 0; i < lines.size(); ++i)
            {
                entry.text += llformat("\n%3d  %s", static_cast<int>(i + 1), lines[i].c_str());
            }
            mWindow.output(entry);
        };
        if (commands)
        {
            list(mShared->command, "cmd");
        }
        if (searches)
        {
            list(mShared->search, "search");
        }
        mWindow.showOutput();
        return true;
    }
    if (name == "wa" || name == "wall")
    {
        mCommands.run("save_all");
        return true;
    }
    if (tabCommand(view, name, args))
    {
        return true;
    }
    if (name == "qa" || name == "qall" || name == "qa!" || name == "qall!")
    {
        std::vector<std::string> ids;
        for (const Doc* each : mServices.openDocs())
        {
            ids.push_back(each->id);
        }
        if (name.back() != '!')
        {
            mWindow.closeMany(ids);
            return true;
        }
        for (const std::string& id : ids)
        {
            if (Doc* each = mServices.findDoc(id))
            {
                mWindow.letGoOf(*each);
            }
        }
        return true;
    }
    if (name == "set")
    {
        std::string option = args;
        const bool  off    = option.compare(0, 2, "no") == 0;
        if (off)
        {
            option.erase(0, 2);
        }
        const char* toggled = option == "number" || option == "nu"          ? "line_numbers"
                              : option == "relativenumber" || option == "rnu" ? "relative_numbers"
                                                                              : nullptr;
        if (!toggled)
        {
            return false;
        }
        if (mCommands.checked(toggled) == off)
        {
            mCommands.run(toggled);
        }
        return true;
    }
    // The studio's own, by the names its menu knows, where the menu would
    // give them: `:format` typed in the expansion being read is not a
    // format of the source out of sight. Vim has no fixes of its own:
    // `:fix` lists the caret's, and `:fixall` makes every preferred one.
    const std::string menu_name = name == "fix" ? "quick_fix" : name == "fixall" ? "fix_all" : name;
    if (std::any_of(std::begin(VIM_MENU_COMMANDS), std::end(VIM_MENU_COMMANDS), [&menu_name](const char* command) { return menu_name == command; }))
    {
        mCommands.runIfEnabled(menu_name);
        return true;
    }
    return false;
}

void ALScriptStudioVim::complete(const std::string& command, const std::string& typed, std::vector<std::string>& out)
{
    // The names command answers to, in their long forms, and the menu's
    // actions; what :set and :history take after them, and the tabs'
    // names after :b and :bd.
    static const char* NAMES[] = { "bNext",   "bdelete",   "bfirst",    "blast",   "bnext",     "bprevious", "brewind",  "buffer",
                                   "buffers", "bunload",   "bwipeout",  "cNext",   "cclose",    "cfirst",    "clast",    "clist",
                                   "close",   "cnext",     "copen",     "cprevious", "crewind", "cwindow",   "edit",     "files",
                                   "fix",     "fixall",    "history",   "lNext",   "lclose",    "lfirst",    "llast",    "llist",
                                   "lnext",   "lopen",     "lprevious", "lrewind", "ls",        "lwindow",   "qall",     "quit",
                                   "read",    "tabNext",   "tabclose",  "tabedit", "tabfirst",  "tablast",   "tabmove",  "tabnew",
                                   "tabnext", "tabonly",   "tabprevious", "tabrewind", "update", "wall",     "wq",       "wqall",
                                   "write",   "xall",      "xit" };
    static const char* OPTIONS[] = { "number", "nonumber", "relativenumber", "norelativenumber" };
    static const char* KINDS[]   = { "all", "cmd", "search" };
    if (command.empty())
    {
        out.insert(out.end(), std::begin(NAMES), std::end(NAMES));
        out.insert(out.end(), std::begin(VIM_MENU_COMMANDS), std::end(VIM_MENU_COMMANDS));
    }
    else if (command == "set" || command == "se")
    {
        out.insert(out.end(), std::begin(OPTIONS), std::end(OPTIONS));
    }
    else if (command == "history" || command == "his")
    {
        out.insert(out.end(), std::begin(KINDS), std::end(KINDS));
    }
    else if (abbreviates(command, "b", "buffer") || abbreviates(command, "bd", "bdelete") || abbreviates(command, "bw", "bwipeout") ||
             abbreviates(command, "bun", "bunload"))
    {
        for (const Doc* doc : mServices.openDocs())
        {
            out.push_back(doc->name);
        }
    }
    else if (abbreviates(command, "e", "edit") || abbreviates(command, "tabe", "tabedit") || command == "tabnew" ||
             abbreviates(command, "r", "read") || abbreviates(command, "w", "write"))
    {
        completeFile(typed, out);
    }
}

void ALScriptStudioVim::format(ALTextView& view, S32 first, S32 last)
{
    // The lines of the source, where `=` was given there: the expansion's
    // lines are other lines, and it is not to be changed.
    Doc* doc = docOf(view);
    if (!doc || &view != doc->editor || !doc->loaded || !doc->modifiable || doc->notecard)
    {
        return;
    }
    const ALTextDocument& text = doc->editor->document();
    doc->editor->setSelection(ALTextRange(text.lineStart(first), text.lineEnd(llmin(last, text.lineCount() - 1))));
    mWindow.format(*doc, true);
}

ALScriptStudioVim::Doc* ALScriptStudioVim::tabNamed(ALTextView& view, const std::string& which)
{
    const std::vector<Doc*>    tabs = mServices.openDocs();
    LLStringUtil::format_map_t args;
    if (which.empty() || which == "%")
    {
        return mServices.frontDoc();
    }
    if (which == "#")
    {
        Doc* doc = mAlternate.empty() ? nullptr : mServices.findDoc(mAlternate);
        if (!doc)
        {
            fail(view, mServices.words("VimNoAlternate"));
        }
        return doc;
    }
    if (const S32 number = numberOf(which); number >= 0)
    {
        if (number >= 1 && number <= static_cast<S32>(tabs.size()))
        {
            return tabs[number - 1];
        }
        args["[NUMBER]"] = which;
        fail(view, mServices.words("VimNoSuchTab", args));
        return nullptr;
    }
    // A name: whole first, else a part of one, in any case, that is one
    // tab's alone.
    for (Doc* doc : tabs)
    {
        if (doc->name == which)
        {
            return doc;
        }
    }
    std::string wanted = which;
    LLStringUtil::toLower(wanted);
    std::vector<Doc*> found;
    for (Doc* doc : tabs)
    {
        std::string name = doc->name;
        LLStringUtil::toLower(name);
        if (name.find(wanted) != std::string::npos)
        {
            found.push_back(doc);
        }
    }
    if (found.size() == 1)
    {
        return found.front();
    }
    args["[NAME]"] = which;
    fail(view, mServices.words(found.empty() ? "VimNoTabMatch" : "VimTabsMatch", args));
    return nullptr;
}

bool ALScriptStudioVim::tabCommand(ALTextView& view, const std::string& name_in, const std::string& args)
{
    // The bang is its own: :bd! and :tabclose! let a tab go as it stands,
    // where without one an unsaved tab is asked about.
    const bool              bang  = !name_in.empty() && name_in.back() == '!';
    const std::string       name  = bang ? name_in.substr(0, name_in.size() - 1) : name_in;
    const std::vector<Doc*> tabs  = mServices.openDocs();
    Doc*                    front = mServices.frontDoc();
    const S32               count = static_cast<S32>(tabs.size());
    const S32               at    = static_cast<S32>(std::find(tabs.begin(), tabs.end(), front) - tabs.begin());
    auto go = [&](S32 index) {
        if (index >= 0 && index < count)
        {
            mWindow.activate(*tabs[index]);
        }
    };
    // Along the strip, round past either end, as vim's buffers and tab
    // pages go.
    auto step = [&](S32 by) {
        if (front && count > 0)
        {
            go(((at + by) % count + count) % count);
        }
    };
    auto invalid = [&]() { fail(view, mServices.words("VimBadArgument")); };
    auto close   = [&](Doc& doc) {
        if (bang)
        {
            mWindow.letGoOf(doc);
        }
        else
        {
            mWindow.closeDocument(doc.id);
        }
    };

    if (name == "ls" || name == "buffers" || name == "files")
    {
        listTabs();
        return true;
    }
    if (abbreviates(name, "b", "buffer"))
    {
        if (Doc* doc = tabNamed(view, args))
        {
            mWindow.activate(*doc);
        }
        return true;
    }
    if (abbreviates(name, "bn", "bnext") || abbreviates(name, "bN", "bNext") || abbreviates(name, "bp", "bprevious") ||
        abbreviates(name, "tabp", "tabprevious") || abbreviates(name, "tabN", "tabNext"))
    {
        const S32 by = args.empty() ? 1 : numberOf(args);
        if (by < 0)
        {
            invalid();
            return true;
        }
        step(abbreviates(name, "bn", "bnext") ? by : -by);
        return true;
    }
    if (abbreviates(name, "bf", "bfirst") || abbreviates(name, "br", "brewind") || abbreviates(name, "tabfir", "tabfirst") ||
        abbreviates(name, "tabr", "tabrewind"))
    {
        go(0);
        return true;
    }
    if (abbreviates(name, "bl", "blast") || abbreviates(name, "tabl", "tablast"))
    {
        go(count - 1);
        return true;
    }
    if (abbreviates(name, "bd", "bdelete") || abbreviates(name, "bw", "bwipeout") || abbreviates(name, "bun", "bunload") ||
        abbreviates(name, "tabc", "tabclose"))
    {
        if (Doc* doc = tabNamed(view, args))
        {
            close(*doc);
        }
        return true;
    }
    if (abbreviates(name, "tabn", "tabnext"))
    {
        // The next, round past the end; the Nth; N on or back; the last.
        if (args.empty())
        {
            step(1);
            return true;
        }
        S32 to = -1;
        if (args == "$")
        {
            to = count - 1;
        }
        else if (args[0] == '+' || args[0] == '-')
        {
            const S32 by = numberOf(args.substr(1));
            to           = by < 0 ? -1 : at + (args[0] == '+' ? by : -by);
        }
        else if (const S32 number = numberOf(args); number >= 1)
        {
            to = number - 1;
        }
        if (to < 0 || to >= count)
        {
            invalid();
            return true;
        }
        go(to);
        return true;
    }
    if (abbreviates(name, "tabo", "tabonly"))
    {
        std::vector<std::string> others;
        for (const Doc* doc : tabs)
        {
            if (doc != front)
            {
                others.push_back(doc->id);
            }
        }
        if (!front || others.empty())
        {
            return true;
        }
        if (!bang)
        {
            mWindow.closeMany(others);
            return true;
        }
        for (const std::string& id : others)
        {
            if (Doc* doc = mServices.findDoc(id))
            {
                mWindow.letGoOf(*doc);
            }
        }
        return true;
    }
    if (abbreviates(name, "tabm", "tabmove"))
    {
        // To the end; after the Nth as they stand, 0 the first; N on or
        // back.
        if (!front)
        {
            return true;
        }
        S32 to = -1;
        if (args.empty() || args == "$")
        {
            to = count - 1;
        }
        else if (args[0] == '+' || args[0] == '-')
        {
            const S32 by = numberOf(args.substr(1));
            to           = by < 0 ? -1 : at + (args[0] == '+' ? by : -by);
        }
        else if (const S32 after = numberOf(args); after >= 0 && after <= count)
        {
            to = after == 0 ? 0 : after - 1 < at ? after : after - 1;
        }
        if (to < 0 || to >= count)
        {
            invalid();
            return true;
        }
        std::vector<std::string> order;
        for (const Doc* doc : tabs)
        {
            order.push_back(doc->id);
        }
        order.erase(order.begin() + at);
        order.insert(order.begin() + to, front->id);
        mWindow.reorderTabs(order);
        return true;
    }
    return false;
}

void ALScriptStudioVim::listTabs()
{
    // As vim's :ls lists its buffers: the number each is gone to by, % the
    // one in front and # the one before it, a shown or h not, - one that
    // cannot be changed, + one unsaved; and the line its caret is on.
    const Doc*          front     = mServices.frontDoc();
    const Doc*          alternate = mAlternate.empty() ? nullptr : mServices.findDoc(mAlternate);
    ALOutputView::Entry entry     = listing();
    entry.text                    = "tabs:";
    S32 number                    = 0;
    for (const Doc* doc : mServices.openDocs())
    {
        const char  mark = doc == front ? '%' : doc == alternate ? '#' : ' ';
        std::string row  = llformat("%3d %c%c%c%c \"%s\"", ++number, mark, doc == front ? 'a' : 'h', doc->modifiable ? ' ' : '-',
                                    doc->unsaved() ? '+' : ' ', doc->name.c_str());
        row.resize(std::max<size_t>(row.size() + 1, 40), ' ');
        entry.text += "\n" + row + llformat("line %d", doc->editor ? doc->editor->caret().line + 1 : 0);
    }
    mWindow.output(entry);
    mWindow.showOutput();
}

ALOutputView::Entry ALScriptStudioVim::listing() const
{
    ALOutputView::Entry entry;
    entry.source      = mServices.words("OutputSourceVim");
    entry.key["kind"] = "studio";
    entry.lane        = 1;
    // A listing: its numbered rows a block at the left edge.
    entry.hang = ALOutputView::Hang::None;
    return entry;
}

void ALScriptStudioVim::say(ALTextView& view, const std::string& message, bool error)
{
    if (ALVimKeymap* vim = dynamic_cast<ALVimKeymap*>(view.modalKeymap()))
    {
        vim->say(message, error);
    }
    else
    {
        mServices.setStatus(message, error);
    }
}

namespace
{
    // What a name on the : line may break its folders with.
#if LL_WINDOWS
    constexpr const char* SEPARATORS = "/\\";
    constexpr const char* HOME       = "USERPROFILE";
#else
    constexpr const char* SEPARATORS = "/";
    constexpr const char* HOME       = "HOME";
#endif

    // A name starting with ~ as in the home folder.
    std::string withHome(const std::string& name)
    {
        const bool home = name == "~" || (name.size() > 1 && name[0] == '~' && strchr(SEPARATORS, name[1]));
        return home ? LLStringUtil::getenv(HOME) + name.substr(1) : name;
    }
    bool isFile(const std::filesystem::path& path)
    {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec);
    }
}

std::string ALScriptStudioVim::pathOf(const Doc& doc, const std::string& name, bool existing)
{
    const fsyspath given(withHome(name));
    if (given.is_absolute())
    {
        return fsyspath(given.lexically_normal()).string();
    }
    const std::vector<std::string> folders = mWindow.fileFolders(doc);
    for (const std::string& folder : folders)
    {
        const std::filesystem::path path = (fsyspath(folder) / given).lexically_normal();
        if (isFile(path))
        {
            return fsyspath(path).string();
        }
    }
    return existing || folders.empty() ? std::string() : fsyspath((fsyspath(folders.front()) / given).lexically_normal()).string();
}

void ALScriptStudioVim::completeFile(const std::string& typed, std::vector<std::string>& out)
{
    // The folder part of what was typed as it was typed, which every word
    // offered starts with, and the start of a name in it; the folder read
    // where the name says, or in each of the front tab's folders.
    const size_t      slash  = typed.find_last_of(SEPARATORS);
    const std::string folder = slash == std::string::npos ? std::string() : typed.substr(0, slash + 1);
    const std::string leaf   = typed.substr(folder.size());
    std::vector<std::filesystem::path> reads;
    const fsyspath                     given(withHome(folder));
    if (given.is_absolute())
    {
        reads.push_back(given);
    }
    else if (const Doc* doc = mServices.frontDoc())
    {
        for (const std::string& each : mWindow.fileFolders(*doc))
        {
            reads.push_back(fsyspath(each) / given);
        }
    }
    // A folder of thousands offers the first few hundred.
    constexpr size_t MOST = 500;
    for (const std::filesystem::path& read : reads)
    {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(read, ec), end; !ec && it != end && out.size() < MOST; it.increment(ec))
        {
            const std::string name = fsyspath(it->path().filename()).string();
            // Hidden ones where a dot is typed for them.
            if (name.compare(0, leaf.size(), leaf) != 0 || (name[0] == '.' && (leaf.empty() || leaf[0] != '.')))
            {
                continue;
            }
            std::error_code kind;
            out.push_back(folder + name + (it->is_directory(kind) ? "/" : ""));
        }
    }
}

bool ALScriptStudioVim::fileCommand(ALTextView& view, Doc& doc, const std::string& name_in, const std::string& args)
{
    const bool                 bang = !name_in.empty() && name_in.back() == '!';
    const std::string          name = bang ? name_in.substr(0, name_in.size() - 1) : name_in;
    LLStringUtil::format_map_t words;
    words["[FILE]"] = args;

    const bool tab_form = abbreviates(name, "tabe", "tabedit") || name == "tabnew";
    if (abbreviates(name, "e", "edit") || tab_form)
    {
        if (args.empty())
        {
            // The tab's own again, as it was last saved -- over unsaved work
            // only with !, which sets that aside first.
            if (tab_form)
            {
                fail(view, mServices.words("VimArgumentRequired"));
            }
            else if (doc.unsaved() && !bang)
            {
                fail(view, mServices.words("VimNotSaved"));
            }
            else if (mWindow.revertible(doc))
            {
                mWindow.revert(doc);
            }
            return true;
        }
        if (args == "#")
        {
            if (Doc* alternate = tabNamed(view, args))
            {
                mWindow.activate(*alternate);
            }
            return true;
        }
        // A tab by its name, or a file's by where it is; else the file,
        // opened in a tab of its own.
        for (Doc* each : mServices.openDocs())
        {
            if (each->name == args)
            {
                mWindow.activate(*each);
                return true;
            }
        }
        const std::string path = pathOf(doc, args, true);
        if (path.empty())
        {
            fail(view, mServices.words("VimNoFile", words));
            return true;
        }
        for (Doc* each : mServices.openDocs())
        {
            if (each->file == path)
            {
                mWindow.activate(*each);
                return true;
            }
        }
        // Read as its extension says, else as the tab it was named from.
        std::string extension = fsyspath(fsyspath(path).extension()).string();
        LLStringUtil::toLower(extension);
        mWindow.openFileTab(path, extension == ".lua" || extension == ".luau" || (extension != ".lsl" && doc.language.lua));
        return true;
    }
    if (abbreviates(name, "r", "read"))
    {
        // Below the caret's line, as one step to undo: the file named, or
        // the tab's own.
        if (&view != doc.editor || !doc.loaded || !doc.modifiable)
        {
            fail(view, mServices.words("VimCannotChange"));
            return true;
        }
        if (args.empty() && doc.file.empty())
        {
            fail(view, mServices.words("VimNoFileName"));
            return true;
        }
        const std::string path = args.empty() ? doc.file : pathOf(doc, args, true);
        std::string       text;
        if (path.empty() || !mWindow.readFile(path, text))
        {
            words["[FILE]"] = args.empty() ? doc.file : args;
            fail(view, mServices.words("VimNoFile", words));
            return true;
        }
        // The file's last line break is the line's own.
        if (!text.empty() && text.back() == '\n')
        {
            text.pop_back();
        }
        const ALTextDocument& d    = doc.editor->document();
        const S32             line = doc.editor->caret().line;
        doc.editor->setSelection(ALTextRange(d.lineEnd(line), d.lineEnd(line)));
        doc.editor->insertText("\n" + text);
        doc.editor->setCaret(ALTextPos(line + 1, 0));
        return true;
    }
    if (abbreviates(name, "w", "write") && !args.empty())
    {
        // The text as the view has it, to a file, the tab left as it is;
        // over one there already only with !. No shell, no appending.
        if (args[0] == '!' || args.compare(0, 2, ">>") == 0)
        {
            fail(view, mServices.words("VimBadArgument"));
            return true;
        }
        const std::string path = pathOf(doc, args, false);
        if (path.empty())
        {
            fail(view, mServices.words("VimNoFile", words));
            return true;
        }
        std::error_code ec;
        if (!bang && std::filesystem::exists(fsyspath(path), ec))
        {
            fail(view, mServices.words("VimFileExists"));
            return true;
        }
        const std::string text = view.document().text();
        words["[FILE]"]        = path;
        if (!mWindow.writeFile(path, text))
        {
            fail(view, mServices.words("VimCannotWrite", words));
            return true;
        }
        words["[LINES]"] = std::to_string(view.document().lineCount());
        words["[BYTES]"] = std::to_string(text.size());
        say(view, mServices.words("VimWritten", words));
        return true;
    }
    if (abbreviates(name, "up", "update"))
    {
        if (doc.unsaved())
        {
            mCommands.run("save");
        }
        return true;
    }
    if (name == "wqa" || name == "wqall" || name == "xa" || name == "xall")
    {
        // :wq over each: the unsaved saved and closed once they are, the
        // rest closed now.
        std::vector<std::string> ids;
        for (const Doc* each : mServices.openDocs())
        {
            ids.push_back(each->id);
        }
        for (const std::string& id : ids)
        {
            if (Doc* each = mServices.findDoc(id))
            {
                if (each->unsaved() && each->modifiable)
                {
                    mWindow.saveToClose(id);
                }
                else
                {
                    mWindow.closeDocument(id);
                }
            }
        }
        return true;
    }
    return false;
}

bool ALScriptStudioVim::problemCommand(ALTextView& view, Doc& doc, const std::string& name, const std::string& args)
{
    // Each of vim's list commands, as its quickfix and location list names
    // it.
    auto either = [&name](const char* least, const char* whole, const char* l_least, const char* l_whole) {
        return abbreviates(name, least, whole) || abbreviates(name, l_least, l_whole);
    };
    const bool next     = either("cn", "cnext", "lne", "lnext");
    const bool previous = either("cp", "cprevious", "lp", "lprevious") || either("cN", "cNext", "lN", "lNext");
    if (next || previous)
    {
        const char* step  = next ? "next_problem" : "previous_problem";
        const S32   times = args.empty() ? 1 : numberOf(args);
        if (times < 1)
        {
            fail(view, mServices.words("VimBadArgument"));
            return true;
        }
        if (!mCommands.enabled(step))
        {
            fail(view, mServices.words("VimNoErrors"));
            return true;
        }
        for (S32 n = 0; n < times; ++n)
        {
            mCommands.run(step);
        }
        return true;
    }
    const bool first = either("cfir", "cfirst", "lfir", "lfirst") || either("cr", "crewind", "lr", "lrewind");
    const bool last  = either("cla", "clast", "lla", "llast");
    if (name == "cc" || name == "ll" || first || last)
    {
        const S32 number = first ? 1 : last ? -1 : args.empty() ? 0 : numberOf(args);
        if (number < 0 && !last)
        {
            fail(view, mServices.words("VimBadArgument"));
        }
        else if (!mWindow.goToProblemNumber(doc, number))
        {
            fail(view, mServices.words("VimNoErrors"));
        }
        return true;
    }
    // The tab shown, folded: the table's toggle, where it is not so already.
    const bool open = either("cope", "copen", "lop", "lopen") || either("cw", "cwindow", "lw", "lwindow") ||
                      either("cl", "clist", "lli", "llist");
    const bool close = either("ccl", "cclose", "lcl", "lclose");
    if (open || close)
    {
        if (mCommands.checked("problems") != open)
        {
            mCommands.run("problems");
        }
        return true;
    }
    return false;
}
