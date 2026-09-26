/**
 * @file alscriptnavigation.h
 * @brief Script Studio's navigation: the places gone from, back and forward, and previews opened as a list is walked.
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

#include "alscriptstudiodoc.h"
#include "llsd.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

class ALPaneList;
class ALScriptStudioServices;
class LLUICtrl;

// The places jumped from, to go back to and forward again: a place in a
// tab by its id, in the view it was in. Once for a line, the fifty latest,
// and the way forward gone once a new place is noted.
class ALNavHistory
{
public:
    struct Place
    {
        std::string                doc;
        ALTextPos                  at;
        ALScriptStudioDoc::View    view = ALScriptStudioDoc::View::Source;
    };
    static constexpr size_t PLACES = 50;

    void note(const Place& place);
    // The place to go to, back or forward, the ones in tabs closed since
    // -- which `open` says are not -- passed over and dropped; `here`, where
    // there is somewhere, put on the way the other way. None where there is
    // nowhere left to go.
    std::optional<Place> take(bool forward, const std::optional<Place>& here, const std::function<bool(const std::string& doc)>& open);
    bool                 canGo(bool forward) const { return !(forward ? mForward : mBack).empty(); }
    // A tab known by another id from here on.
    void                 rekey(const std::string& was, const std::string& id);

private:
    std::vector<Place> mBack;
    std::vector<Place> mForward;
};

// A Script Studio window's navigation, apart from the popovers that pick
// where to go, which are the window's: the places jumped from, with Back
// and Forward over them, a walk down a pane's list one jump; and the
// previews a pane's list opens as it is walked -- a row stayed on a moment
// opened as a preview, which the next such takes the place of, and held
// once anything is done in it.
class ALScriptNavigation
{
public:
    typedef ALScriptStudioDoc Doc;
    typedef ALNavHistory::Place Place;

    // What navigation asks of the window beyond its services.
    class Window
    {
    public:
        // A place gone to: its tab in front, in its view, the caret there,
        // the keyboard given to it.
        virtual void showPlace(Doc& doc, Doc::View view, const ALTextPos& at) = 0;
        // Whether a script or a file an include's path names is open in a
        // tab here.
        virtual bool pathOpen(const std::string& path) const = 0;
        // A row a list has stayed on chosen as a preview, as the list's
        // pane chooses one.
        virtual void choosePreview(ALPaneList* list) = 0;
        // Whether a tab is being worked from -- a pane listing its problems,
        // the places a name was looked up from it -- which holds a preview.
        virtual bool workedFrom(const Doc& doc) const = 0;
        // A tab let go of as it stands; the tabs' strip filled again; and
        // the keyboard given to the view a tab shows.
        virtual void letGoOf(Doc& doc)   = 0;
        virtual void fillTabs()          = 0;
        virtual void focusDoc(Doc& doc)  = 0;

    protected:
        ~Window() = default;
    };

    ALScriptNavigation(ALScriptStudioServices& services, Window& window);

    // A jump from where the caret is in the tab in front; `walking`, a walk
    // down a pane's list, which is one jump until the editor has the
    // keyboard again (walked).
    void noteJump(bool walking = false);
    void walked() { mWalking = false; }
    bool walking() const { return mWalking; }
    void remember(const Place& place) { mHistory.note(place); }
    // Back, or Forward again.
    void goBack(bool forward);
    bool canGo(bool forward) const { return mHistory.canGo(forward); }
    // A tab known by another id from here on.
    void rekey(const std::string& was, const std::string& id) { mHistory.rekey(was, id); }

    // A row of a pane's list walked to that would open a tab: opened once
    // the list has stayed on it a moment, as a preview. False where it is
    // open already, or a preview is being opened, and it opens at once.
    bool deferOpen(ALPaneList* list, const std::string& path);
    // Each frame: the row stayed on opened, where the list still has it and
    // the keyboard.
    void pumpSettle();
    // What is opened now opens as a preview: while a row stayed on opens.
    bool openingPreview() const { return mOpenPreview > 0; }
    // The preview there is let go of, for another to take its place; or
    // held, where anything was done in it or it is worked from.
    void closePreview();
    // A tab a preview no longer: kept.
    void holdPreview(Doc& doc);
    // A list's row chosen and its place shown: the keyboard left in the
    // list to walk on, or, `to_editor`, the tab held and given it.
    void revealed(LLUICtrl* list, bool to_editor);

private:
    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    ALNavHistory            mHistory;
    bool                    mWalking = false;
    // A row stayed on being opened; the list whose row is being waited on,
    // what the row was and when it is to be opened.
    S32                     mOpenPreview = 0;
    bool                    mSettled     = false;
    ALPaneList*             mSettleList  = nullptr;
    LLSD                    mSettleValue;
    F64                     mSettleDue   = 0.0;
};
