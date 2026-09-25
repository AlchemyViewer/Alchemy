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
    // options, :history's kinds.
    static void complete(const std::string& command, std::vector<std::string>& out);
    // `=` over lines: the source's, formatted, where it was given there.
    void format(ALTextView& view, S32 first, S32 last);
    // q:, q/ and q?: the lines entered, the last first, in a list over the
    // editor; the one picked runs, or with Shift goes back onto the line.
    void historyWindow(ALTextView& view, llwchar kind, const std::vector<std::string>& history,
                       std::function<void(const std::string&, bool run)> chosen);

    // The tab an editor is of, its source's or its expansion's.
    Doc* docOf(const ALTextView& view);

private:
    ALScriptStudioServices&              mServices;
    ALScriptStudioCommands&              mCommands;
    Window&                              mWindow;
    std::shared_ptr<ALVimKeymap::Shared> mShared = std::make_shared<ALVimKeymap::Shared>();
    std::string                          mBanner;
    // Held while this is, for a keymap's hooks to know it still is.
    std::shared_ptr<bool>                mAlive = std::make_shared<bool>(true);
};
