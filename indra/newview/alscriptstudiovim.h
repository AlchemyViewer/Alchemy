/**
 * @file alscriptstudiovim.h
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

#pragma once

#include "alquickopen.h"
#include "aloutputview.h"
#include "alscriptstudiodoc.h"
#include "alvimkeymap.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ALScriptStudioCommands;
class ALScriptStudioServices;
class ALTextView;

// A Script Studio window's side of the vim mode its editors may have: the
// history and options every one of its editors shares; the : commands the
// mode leaves to the studio, run through the window's command table where
// the menus have them; `=` over lines; the history window q: opens; and the
// word the strip under the editor says while vim is on. The keymap itself,
// and whether the mode is on, are the editors' and the window's.
class ALScriptStudioVim
{
public:
    typedef ALScriptStudioDoc Doc;

    // What vim asks of the window beyond its services and its commands.
    class Window
    {
    public:
        // A tab closed: asked about first where it is unsaved; let go of as
        // it stands, nothing asked; or saved, and closed once the save
        // comes back. Several at once, the unsaved asked about in one
        // question.
        virtual void closeDocument(std::string_view id)             = 0;
        virtual void letGoOf(Doc& doc)                              = 0;
        virtual void saveToClose(const std::string& id)             = 0;
        virtual void closeMany(const std::vector<std::string>& ids) = 0;
        // The selection formatted, or the whole text.
        virtual void format(Doc& doc, bool selection_only) = 0;
        // An entry in the Output tab; and the tab brought into sight.
        virtual void output(const ALOutputView::Entry& entry) = 0;
        virtual void showOutput()                             = 0;
        // A line picked from a list over the editors, as tall as `rows`:
        // Return on one, Shift-Return, or none picked.
        virtual void pickLine(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                              S32 rows, std::function<void(const std::string& line)> chosen,
                              std::function<void(const std::string& line)> shifted, std::function<void()> cancelled) = 0;
        // The strip under the editor said again.
        virtual void refreshTrailer(Doc& doc) = 0;
        // A tab brought to the front; and the tabs put in this order, by id.
        virtual void activate(Doc& doc)                                  = 0;
        virtual void reorderTabs(const std::vector<std::string>& order) = 0;
        // A tab's text put back as it was last saved or loaded, what it
        // held set aside first, nothing asked; and whether it can be.
        virtual void revert(Doc& doc)                  = 0;
        virtual bool revertible(const Doc& doc) const = 0;
        // A file on disk opened in a tab where it is, read as the language
        // its extension says, else `lua`; null where it could not be.
        virtual Doc* openFileTab(const std::string& path, bool lua) = 0;
        // A file's text read whole, its line endings as an editor here
        // keeps them; and a text written to one whole. False where it
        // could not be.
        virtual bool readFile(const std::string& path, std::string& text)        = 0;
        virtual bool writeFile(const std::string& path, const std::string& text) = 0;
        // The folders a file named without one is looked for in, in order:
        // the folder of the tab's own file, where it is one, then the
        // include folders.
        virtual std::vector<std::string> fileFolders(const Doc& doc) const = 0;
        // The caret to a problem of the tab's by its number, counted from 1
        // or back from -1, past either end the one at that end; 0 the one
        // at the caret or after it. False where it has none.
        virtual bool goToProblemNumber(Doc& doc, S32 number) = 0;
        // A place in a tab's source or expansion to come back to with Back,
        // where a vim jump began.
        virtual void jumpedFrom(Doc& doc, const ALTextView& view, const ALTextPos& from) = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioVim(ALScriptStudioServices& services, ALScriptStudioCommands& commands, Window& window);

    // A keymap put over one of the window's editors: sharing its history
    // and options with the rest, and asking this what it leaves.
    void connect(ALVimKeymap& vim);
    // The history and options every editor of the window shares.
    ALVimKeymap::Shared& shared() { return *mShared; }

    // Each frame: the word the strip under the editor says where the
    // editor in front has vim, said again where it changed.
    void               pump();
    const std::string& banner() const { return mBanner; }
    void               clearBanner() { mBanner.clear(); }

    // A : command the mode does not answer itself, typed in one of the
    // window's editors: the studio's, by the names vim and its menus know
    // them by. False where it knows it no more than vim did.
    bool command(ALTextView& view, const std::string& name, const std::string& args);
    // The words Tab completes on the : line: the command names, :set's
    // options, :history's kinds, the tabs' names after :b and :bd, and the
    // files and folders `typed` may be the start of after :e, :r and :w.
    void complete(const std::string& command, const std::string& typed, std::vector<std::string>& out);
    // `=` over lines: the source's, formatted, where it was given there.
    void format(ALTextView& view, S32 first, S32 last);
    // q:, q/ and q?: the lines entered, the last first, in a list over the
    // editor; the one picked runs, or with Shift goes back onto the line.
    void historyWindow(ALTextView& view, llwchar kind, const std::vector<std::string>& history,
                       std::function<void(const std::string&, bool run)> chosen);

    // The tab an editor is of, its source's or its expansion's.
    Doc* docOf(const ALTextView& view);
    // The tab `which` names, as :b and :bd take one: its number in the
    // strip, # the tab in front before this one, % the one in front, else
    // its name or a part of one, in any case, that no other has. Null
    // where none is, or more than one, which is said.
    Doc* tabNamed(ALTextView& view, const std::string& which);

private:
    // The window's tabs as vim's buffers and tab pages: :ls, :b, :bn, :bp,
    // :bf, :bl, :bd, :tabnext, :tabprevious, :tabfirst, :tablast,
    // :tabclose, :tabonly, :tabmove. False for any other command.
    bool tabCommand(ALTextView& view, const std::string& name, const std::string& args);
    void listTabs();
    // A tab's files on disk: :e and :e! with or without one, :tabedit and
    // :tabnew, :r, :w with one, :update, :wqa. False for any other command.
    bool fileCommand(ALTextView& view, Doc& doc, const std::string& name, const std::string& args);
    // A file named on the : line, where it is: as it stands where it says
    // its whole way, ~ the home folder; else in the tab's fileFolders, the
    // first that has it -- or, `existing` false, where it would be made,
    // in the first. Empty where it is nowhere.
    std::string pathOf(const Doc& doc, const std::string& name, bool existing);
    // The files and folders on disk a word typed may be the start of.
    void completeFile(const std::string& typed, std::vector<std::string>& out);
    // The Problems tab as vim's quickfix and location lists, which are one
    // list here, the tab in front's: :cn, :cp, :cc, :cfirst, :clast,
    // :copen, :cclose, :clist and the :l ones. False for any other.
    bool problemCommand(ALTextView& view, Doc& doc, const std::string& name, const std::string& args);
    // An entry for the Output tab, of vim's.
    ALOutputView::Entry listing() const;
    // Said where vim says things, as an error or not; in the status line
    // where the view has no vim.
    void fail(ALTextView& view, const std::string& message) { say(view, message, true); }
    void say(ALTextView& view, const std::string& message, bool error = false);

    ALScriptStudioServices&              mServices;
    ALScriptStudioCommands&              mCommands;
    Window&                              mWindow;
    std::shared_ptr<ALVimKeymap::Shared> mShared = std::make_shared<ALVimKeymap::Shared>();
    std::string                          mBanner;
    // The tab in front as pump last saw it, and the one before it, by id.
    std::string                          mCurrent;
    std::string                          mAlternate;
    // Held while this is, for a keymap's hooks to know it still is.
    std::shared_ptr<bool>                mAlive = std::make_shared<bool>(true);
};
