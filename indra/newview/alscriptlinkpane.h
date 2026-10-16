/**
 * @file alscriptlinkpane.h
 * @brief Script Studio's Link tab: the scripts of objects chosen in the Explorer, each with a file on disk proposed as its master.
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
#include "alscriptlinkscripts.h"
#include "llpanel.h"

#include <functional>
#include <string>
#include <vector>

class ALPaneList;
class ALScriptStudioServices;
class LLButton;
class LLCheckBoxCtrl;
class LLTextBox;

// The Link tab of a Script Studio window, under the editor, shown while
// there is something to link: every script of the objects or prims chosen
// in the Explorer (Link Scripts to Files), each a row with a box to tick,
// the file on disk proposed as its master -- every path in full -- how it
// was found, and whether it is what the script holds in-world. A file the
// script names that is not called what the item is is marked, being the
// script's choice and not the scripter's; where several files could be it,
// the row says so and chooses none, and Choose File, Return or a
// double-click choose one, or any file on disk. Nothing in an object the
// agent does not own is ticked unasked. Link links what is ticked, sends
// those that differ where asked, and says what it did in Output; Cancel
// links nothing. What is found and how is ALScriptLinkScripts's.
class ALScriptLinkPane : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptLinkPane, LLPanel);
    typedef ALScriptLinkScripts::Row Row;
    typedef ALScriptLinkScripts::How How;

    // What the tab asks of the window beyond its services.
    class Window
    {
    public:
        // A list to pick from, over the editors: what is chosen, and what
        // Shift-Return is pressed on.
        virtual void pick(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                          std::function<void(const std::string& value)> chosen, std::function<void(const std::string& value)> dropped) = 0;
        // A file on disk chosen through the system's picker.
        virtual void pickMasterFile(std::function<void(const std::string& path)> chosen) = 0;
        // Done with: what was linked said in Output, which is to be shown;
        // or nothing linked.
        virtual void linkPaneDone(bool linked) = 0;

    protected:
        ~Window() = default;
    };

    // Built by the skin (class="script_studio_link") in a Script Studio
    // window it finds through the view tree.
    explicit ALScriptLinkPane(const LLPanel::Params& params = getDefaultParams());
    bool postBuild() override;

    // The scripts of the prims listed, the last proposal let go of.
    void        gather(std::vector<ALScriptLinkScripts::Prim> prims);
    // Whether it holds something to link, or is finding it.
    bool        active() const { return mGather.stage() != ALScriptLinkScripts::Stage::Idle; }
    ALPaneList* list() const { return mList; }

private:
    void fill();
    std::string head() const;
    // The boxes as the rows have them now, after a click.
    void readBoxes();
    // The row chosen's file chosen: one of those found, any on disk, or
    // none.
    void chooseFile();
    void chosenFor(const ALScriptRef& ref, const std::string& value);
    void linkTicked();
    void cancel();
    // The row of a script now, by its item; the end for none.
    size_t rowOf(const ALScriptRef& ref) const;
    std::string tipOf(const Row& row) const;

    ALScriptStudioServices* mServices    = nullptr;
    Window*                 mWindow      = nullptr;
    ALPaneList*             mList        = nullptr;
    LLTextBox*              mHead        = nullptr;
    LLButton*               mChoose      = nullptr;
    LLButton*               mLink        = nullptr;
    LLButton*               mCancel      = nullptr;
    LLCheckBoxCtrl*         mSendDiffers = nullptr;
    ALScriptLinkScripts     mGather;
};
