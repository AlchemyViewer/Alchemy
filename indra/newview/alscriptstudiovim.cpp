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
#include "alfloaterscriptstudio.h"
#include "aloutputview.h"
#include "alquickopen.h"
#include "alscriptoutputpane.h"
#include "alvimkeymap.h"
#include <algorithm>

namespace
{
    // The studio's own commands a : line gives by the names its menu knows
    // them by: what vimCommand runs and vimComplete offers, one list.
    const char* const VIM_MENU_COMMANDS[] = { "format", "problems", "references", "output", "search", "preferences", "pop_out",
                                              "reveal", "save_all", "revert", "external_editor", "save_file", "save_as", "load_file",
                                              "open_file", "fold_all", "unfold_all", "go_to_line", "quick_fix", "fix_all", "weights" };
}

ALFloaterScriptStudio::Doc* ALFloaterScriptStudio::docOf(const ALTextView& view)
{
    // The editor carries its document's id in its name, which is what
    // makeEditor gave it; the index answers the rest.
    const std::string& name = view.getName();
    if (name.compare(0, 7, "editor_") == 0)
    {
        if (const size_t index = indexOf(std::string_view(name).substr(7)); index != NONE)
        {
            return mDocs[index].get();
        }
    }
    for (std::unique_ptr<Doc>& doc : mDocs)
    {
        if (doc->editor == &view || doc->expandedEditor == &view)
        {
            return doc.get();
        }
    }
    return nullptr;
}

void ALFloaterScriptStudio::pumpVim()
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    // The mode, the : line as it is typed and what the mode says are
    // all in the band the editor draws under its text, where vim has
    // them; the bottom strip says only that vim is on, so that a reader
    // of the strip knows why the keys do what they do.
    ALCodeEditor* shown = doc->shownText();
    ALVimKeymap*  vim   = shown ? dynamic_cast<ALVimKeymap*>(shown->modalKeymap()) : nullptr;
    std::string  banner = vim ? getString("VimNormal") : std::string();
    if (banner != mVimBanner)
    {
        mVimBanner = banner;
        refreshTrailer(*doc);
    }
}

void ALFloaterScriptStudio::vimHistoryWindow(ALTextView& view, llwchar kind, const std::vector<std::string>& history, std::function<void(const std::string&, bool run)> chosen)
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
    quickOpen(std::move(candidates), getString("VimHistoryPlaceholder", args), getString("VimHistoryTitle", args),
              [chosen, back](const std::string& line) {
                  back();
                  chosen(line, true);
              },
              mEditorHost, 420, ALQuickOpen::heightForRows(llclamp(static_cast<S32>(history.size()), 1, 8)), back,
              [chosen, back](const std::string& line) {
                  back();
                  chosen(line, false);
              });
}

bool ALFloaterScriptStudio::vimCommand(ALTextView& view, const std::string& name, const std::string& args)
{
    Doc* doc = docOf(view);
    if (!doc)
    {
        return false;
    }
    if (name == "w" || name == "write" || name == "w!")
    {
        mSaving.saveAsked(*doc);
        return true;
    }
    if (name == "q" || name == "quit" || name == "close")
    {
        closeDocument(doc->id);
        return true;
    }
    if (name == "q!" || name == "quit!")
    {
        const size_t index = indexOf(doc->id);
        if (index != NONE)
        {
            letGoOf(index);
        }
        return true;
    }
    if (name == "wq" || name == "x" || name == "xit" || name == "wq!" || name == "x!")
    {
        if (doc->unsaved() && doc->modifiable)
        {
            mSaving.saveToClose(doc->id);
        }
        else
        {
            closeDocument(doc->id);
        }
        return true;
    }
    if (name == "history" || name == "his")
    {
        // The lines entered, in the Output pane, where a list fits: the :
        // ones, the search ones with / or search, both with all.
        const bool        searches = args == "/" || args == "search" || args == "all";
        const bool        commands = args.empty() || args == ":" || args == "cmd" || args == "all";
        ALOutputView::Entry entry;
        entry.source      = getString("OutputSourceVim");
        entry.key["kind"] = "studio";
        entry.lane        = 1;
        // A listing: its numbered rows a block at the left edge.
        entry.hang   = ALOutputView::Hang::None;
        auto list = [&](const std::vector<std::string>& lines, const char* kind) {
            entry.text = std::string(kind) + " history:";
            for (size_t i = 0; i < lines.size(); ++i)
            {
                entry.text += llformat("\n%3d  %s", static_cast<int>(i + 1), lines[i].c_str());
            }
            mOutputPane->view()->append(entry);
        };
        if (commands)
        {
            list(mVimShared->command, "cmd");
        }
        if (searches)
        {
            list(mVimShared->search, "search");
        }
        showBottom("output_tab");
        return true;
    }
    if (name == "wa" || name == "wall")
    {
        mSaving.saveAll();
        return true;
    }
    if (name == "qa" || name == "qall" || name == "qa!" || name == "qall!")
    {
        std::vector<std::string> ids;
        for (const std::unique_ptr<Doc>& each : mDocs)
        {
            ids.push_back(each->id);
        }
        if (name.back() != '!')
        {
            closeMany(ids);
            return true;
        }
        for (const std::string& id : ids)
        {
            if (const size_t index = indexOf(id); index != NONE)
            {
                letGoOf(index);
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
        if (option == "number" || option == "nu")
        {
            if (mLineNumbers == off)
            {
                mCommands.run("line_numbers");
            }
            return true;
        }
        if (option == "relativenumber" || option == "rnu")
        {
            if (mRelativeNumbers == off)
            {
                mCommands.run("relative_numbers");
            }
            return true;
        }
        return false;
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

void ALFloaterScriptStudio::vimComplete(ALTextView& view, const std::string& command, std::vector<std::string>& out)
{
    // The names vimCommand answers to, in their long forms, and the
    // menu's actions; what :set and :history take after them.
    static const char* NAMES[]   = { "close", "fix", "fixall", "history", "qall", "quit", "wall", "wq", "write", "xit" };
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
}

void ALFloaterScriptStudio::vimFormat(ALTextView& view, S32 first, S32 last)
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
    format(*doc, true);
}
