/**
 * @file alscriptoutputpane.h
 * @brief Script Studio's Output tab: what scripts say, and what the studio did.
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

#include "aloutputview.h"
#include "alscriptworkspace.h"
#include "llpanel.h"
#include "lluuid.h"

#include <string>
#include <utility>
#include <vector>

struct ALScriptStudioDoc;
class ALScriptStudioServices;
class LLComboBox;
class LLFilterEditor;

// The Script Studio's Output tab: what scripts say -- to their owner, on
// the debug channel, and their run-time errors with the stack under them
// -- and what the studio did, each a line of the log that links to what it
// is about; filtered by whose words they are, what kind of thing, and what
// they say. An error or a failure said while the tab is not in sight is
// unread until it is. It lists and filters; going where a link goes, and
// doing what one asks, is the window's.
class ALScriptOutputPane : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptOutputPane, LLPanel);

    // What the pane asks of the window beyond its services.
    class Window
    {
    public:
        // Whether the Output tab is in sight: the bottom panes open, and
        // it the one in front.
        virtual bool outputInSight() const = 0;
        // Whether something is unread has changed, which the tab's title
        // says.
        virtual void outputUnreadChanged() = 0;
        // Whether an object in world is the agent's.
        virtual bool ownsObject(const LLUUID& root) const = 0;
        // What a link asks, done. A thing to be done about a tab, by the
        // name `said` gave it: save_anyway, retry, copy, export,
        // take_external, keep_here.
        virtual void outputAction(ALScriptStudioDoc& doc, const std::string& action) = 0;
        // A tab the studio's words name, brought forward; with its
        // problems, where the words were of a failure.
        virtual void outputShowDoc(ALScriptStudioDoc& doc, bool problems) = 0;
        // A script a run said something from, gone to: at a line of it,
        // where the line is known -- `running`, one the region counts in
        // what it runs, which the window reads back to the source once it
        // can -- or at a line of one of its includes.
        virtual void outputGoTo(const ALScriptRef& ref, const std::string& name, S32 line, S32 column, bool running) = 0;
        virtual void outputGoToInclude(const std::string& file, const std::string& file_name, S32 line, S32 column) = 0;

    protected:
        ~Window() = default;
    };

    // Where a line a run names is in the text: the script's own where
    // `file` is empty, else one of its includes'; -1s where it is not
    // known.
    struct Place
    {
        S32         line   = -1;
        S32         column = -1;
        std::string file;
        std::string fileName;
        // Still the line the region counts: the script not open, its map
        // not known, or the place in code the preprocessor made.
        bool        running = false;
    };

    // Built by the skin, as the tab (class="script_studio_output"), in a
    // Script Studio window it finds through the view tree, whose services
    // it uses and which it asks what it does not do itself.
    explicit ALScriptOutputPane(const LLPanel::Params& params = getDefaultParams());
    bool postBuild() override;

    // What a script said: a line of the log, or more where it said more,
    // its object offered in the filter the first time it speaks. What
    // runs is the expansion, where the preprocessor ran, so a line the run
    // names is the expansion's: said as the source's, or an include's,
    // where the script is open here. The place its error names, which the
    // window marks on the script's line.
    Place heard(const ALScriptWorkspace::RuntimeEvent& event);
    // What the studio did, kept where it can be read again. The tab's name
    // where the words say it is a link to it; after them, a link for each
    // thing to be done about it (Window::outputAction).
    void said(const std::string& text, bool failure, const ALScriptStudioDoc* doc, const std::vector<std::string>& actions);
    // An entry's link followed: gone to, or what it asks done.
    void choose(const ALOutputView::Entry& entry);

    bool unread() const { return mUnread; }
    // Each frame: what the tab had not shown, it has once it is in sight.
    void pump();
    // Which scripts are open here changed, which the filter by them
    // follows.
    void openChanged();

    // The kind of thing shown, as the window keeps it between sessions.
    std::string kind() const;
    void        showKind(const std::string& kind);

    // The objects offered in the filter, the one heard from most lately
    // last.
    const std::vector<std::pair<LLUUID, std::string>>& objects() const { return mObjects; }
    ALOutputView*                                      view() const { return mView; }

private:
    // An object offered, the one least lately heard from giving way past a
    // few dozen.
    void offerObject(const LLUUID& root, const std::string& name);
    // The filters, over whose words, of what kind, with what in them, as
    // one filter.
    void filter();
    // An error or a failure said: unread, where the tab is not in sight.
    void markUnread();

    ALScriptStudioServices*                     mServices = nullptr;
    Window*                                     mWindow   = nullptr;
    ALOutputView*                               mView  = nullptr;
    LLComboBox*                                 mWhose = nullptr;
    LLComboBox*                                 mKind  = nullptr;
    LLFilterEditor*                             mFind  = nullptr;
    bool                                        mUnread = false;
    std::vector<std::pair<LLUUID, std::string>> mObjects;
};
