/**
 * @file alscriptexplorerpane.h
 * @brief Script Studio's explorer: the objects in hand, their prims and what each holds.
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
#include "alscriptexplorermodel.h"
#include "alscriptrecompile.h"
#include "almenuslot.h"
#include "llui.h"
#include "llhandle.h"
#include "llpanel.h"

#include <boost/signals2.hpp>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class ALScriptExplorerTree;
class ALScriptStudioServices;
struct ALScriptStudioDoc;
class LLContextMenu;
class LLFilterEditor;
class LLViewerObject;

// The Script Studio's explorer, down the left: the objects in hand -- pinned,
// selected in world, or holding a script that is open -- each prim's scripts
// and notecards listed as they are fetched, with whether each script runs.
// Its rows open, start, stop, reset, recompile, rename and delete what they
// stand for; what is chosen drags out to the inventory or into another
// prim, and what the inventory holds drops into one. The world is the
// panel's to look at; what it lists is its model's.
class ALScriptExplorerPane : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptExplorerPane, LLPanel);
    typedef ALScriptExplorerModel Model;
    typedef Model::Choice         Choice;

    // What the pane asks of the window beyond its services.
    class Window
    {
    public:
        // The explorer brought into sight, where it was folded away.
        virtual void showExplorer() = 0;
        // The pins changed, which the window keeps between sessions.
        virtual void explorerPinsChanged() = 0;
        // An item renamed here: its tab, where it is open, called so.
        virtual void itemRenamed(const ALScriptRef& ref, const std::string& name) = 0;
        // An item deleted here: its tab closed, in whichever of the studio's
        // windows holds it, and a window popped out for it alone with it.
        virtual void itemDeleted(const ALScriptRef& ref) = 0;
        // Whether an item is open with unsaved changes in any of them.
        virtual bool unsavedAnywhere(const ALScriptRef& ref) const = 0;
        // The first item open, or brought forward, and set beside the second
        // as the region has it, each under its title.
        virtual void compareItems(const ALScriptRef& first, const std::string& name, const std::string& first_title, const ALScriptRef& second,
                                  const std::string& second_title) = 0;
        // An item opened, or brought forward, and its saves offered to
        // compare with it (ALScriptStudioHistory).
        virtual void showHistory(const ALScriptRef& ref, const std::string& name) = 0;
        // A list to pick from, over the editors: what is chosen, and what
        // Shift-Return is pressed on.
        virtual void pick(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                          std::function<void(const std::string& value)> chosen, std::function<void(const std::string& value)> dropped) = 0;
        // Every script of an object checked, its problems listed.
        virtual void checkScripts(const LLUUID& root) = 0;
        // The scripts, and every script of the prims -- each prim with its
        // object's name -- recompiled for a target, each said in Output and
        // a closed one's problems listed (ALScriptRecompile).
        virtual void recompileScripts(std::vector<ALScriptRecompile::One> scripts, std::vector<std::pair<LLUUID, std::string>> prims,
                                      const std::string& target) = 0;

    protected:
        ~Window() = default;
    };

    // Built by the skin (class="script_studio_explorer") in a Script
    // Studio window it finds through the view tree, whose services it uses
    // and which it asks what it does not do itself.
    explicit ALScriptExplorerPane(const LLPanel::Params& params = getDefaultParams());
    bool postBuild() override;
    // The keys a tree of files answers to past the tree's own -- the arrows,
    // F2, typing a name -- while the tree has the keyboard: return opens
    // what is chosen, or opens and folds it; delete deletes it,
    // Command-Backspace too on a Mac. Escape, from the tree or the filter,
    // goes back to the script, rather than leaving nothing with the
    // keyboard, as a panel does, where the arrows walk the avatar.
    bool handleKeyHere(KEY key, MASK mask) override;

    // Each frame: the names the region said put in, what came in since the
    // list was filled put in it once, and the selection in world looked at.
    void pump();
    // The objects in hand listed again, and what each prim holds asked
    // where it is not known or has changed -- of every prim where `refetch`.
    // `from_region` asks every prim's region, not the objects' copies.
    void relist(bool refetch = false, bool from_region = false);
    // The same on the next frame, however many ask before it: a tab loaded
    // or closed, a script's own answer, many of them at a time.
    void relistSoon(bool refetch = false);
    // A script's row, chosen and in view, with the explorer in sight and
    // the keyboard in it: what holds it unfolded first, and the filter let
    // go of where it hides the row.
    void reveal(const ALScriptStudioDoc& doc);
    // An object pinned, so that it stays listed once it is no longer
    // selected in world; in sight; and chosen, so that the buttons act on it.
    void explore(const LLUUID& root);
    // The keyboard to the list, to walk it: the Explorer's key, and F6.
    void takeKeyboard();

    // The rows chosen, in the tree's order.
    std::vector<Choice>   choice() const;
    const Model&          model() const { return mModel; }

    // The pins, kept between sessions.
    void saveState(LLSD& state) const { mModel.saveState(state); }
    void readState(const LLSD& state) { mModel.readState(state); }

private:
    // The tree made from the model: now, or -- out of sight -- once it is
    // seen (pump).
    void fill();
    void fillWhenSeen();
    void contentsHeard(const ALScriptContents& contents);
    void rereadNames();
    void askName(const LLUUID& id);
    // What to call an object that has never said its name here: what a pin
    // or an open script remembers it by, or an ellipsis while it is asked.
    std::string nameGivenTo(const LLUUID& root) const;
    void        refold(Model::Refold refold);
    // Filled again on the next frame: asked for from within the tree's own
    // handling of a click, a drop or a rename, which the tree is not to be
    // changed under.
    void        fillSoon();

    void onChosen();
    void act(const std::string& action);
    bool enabled(const std::string& action) const;
    // Whether the agent may read a row's item: copy it, and modify a
    // script too; true for a row that is not an item.
    bool readable(const Choice& row) const;
    // Two items compared, the one open already set beside the other where
    // one is (Model::comparing); and one compared with what is picked of
    // those like it (Model::comparableWith).
    void compare(const Choice& first, const Choice& second);
    void compareWith(const Choice& row);
    void showMenu(S32 x, S32 y);
    void refreshButtons();
    // Whether a script runs, as far as the studio knows: its tab's word,
    // else the region's last answer; nothing where neither has said.
    std::optional<bool> knownRunning(const ALScriptRef& ref) const;

    // A script or notecard made in a prim, named through a dialog and
    // opened once the region lists it.
    void create(const LLUUID& prim, bool notecard, bool lua);
    void created(const ALScriptCreated& made, const std::optional<std::string>& opening);
    // The name a row was given where it stands, taken.
    void renamed(const Choice& row, const std::string& was, std::string name);
    void remove(const std::vector<Choice>& rows);
    // The rows' keys to the clipboard, one a line; the row's object or prim
    // chosen in world with the build tools on it; the row's item described,
    // through a dialog.
    void copyKeys(const std::vector<Choice>& rows);
    void editInWorld(const Choice& row);
    void describe(const Choice& row);
    // The rows' scripts, and their prims', up again for a target: "auto"
    // for each as it compiles now.
    void recompile(const std::vector<Choice>& rows, const std::string& target);
    // Start, stop, reset or restart over the rows, asked about first where
    // it reaches more than one script.
    void run(const std::string& action, const std::vector<Choice>& rows);

    // What is chosen of what a prim holds, dragged out with the viewer's
    // drag tool -- to the inventory, as the build floater's contents are --
    // from the prim of the first item chosen; false where none of it may go.
    bool startDrag();
    // What is dragged from the inventory over a row, or dropped on it: into
    // the prim the row is of, as into the build floater's contents; below
    // the rows, where the empty space's drop goes. True where it would go.
    bool drop(const LLSD& row, MASK mask, bool drop, EDragAndDropType type, void* cargo, std::string& tooltip);
    // Why a prim will not take what is carried over it, for the drag's tip.
    std::string dropRefusal(LLViewerObject* prim, EDragAndDropType type, void* cargo) const;
    // What is dragged put into a prim, as the build floater's contents
    // take it; true where it would go.
    bool dropIntoPrim(LLViewerObject* prim, MASK mask, bool drop, EDragAndDropType type, void* cargo);
    // Where a drop on the empty space goes: the prim of what is chosen,
    // where all of that is of one; the object selected in world where
    // nothing is. Nowhere, where either is more than one.
    LLViewerObject* dropTarget() const;
    // Items of one prim put into another, through the agent's inventory,
    // and said as it ends.
    void transfer(const LLUUID& from, const std::vector<LLUUID>& items, const LLUUID& to, bool running);

    ALScriptStudioServices* mServices = nullptr;
    Window*                 mWindow   = nullptr;
    ALScriptExplorerTree*   mTree     = nullptr;
    LLFilterEditor*         mFilter   = nullptr;
    Model                   mModel;
    ALMenuSlot              mMenu;
    // Answers have come that the list does not show yet: it is filled a
    // moment after it was last filled.
    bool                    mStale  = false;
    F64                     mFilled = 0.0;
    // The roots selected in world when last looked, and when; whether the
    // selection changed since.
    std::vector<LLUUID>     mRoots;
    F64                     mPolled           = 0.0;
    bool                    mSelectionChanged = false;
    // When what the prims hold is asked again after a drop, for what the
    // drop sends on its own time: a folder's items, fetched first.
    F64                     mRefetchAt = 0.0;
    // When it is listed again, after an object it lists came or went.
    F64                     mPresenceAt = 0.0;
    // What a drag from another prim has dropped so far, sent with its last.
    std::vector<LLUUID>     mTransferring;
    // Names the region said of what the list shows since the last frame.
    bool                    mNamesStale = false;
    // Listed again on the next frame, and whether every prim is asked again.
    std::optional<bool>     mRelistWanted;
    // Whether the filter has words in it, as it last committed.
    bool                    mFiltering = false;
    boost::signals2::scoped_connection mPropertiesConnection;
    boost::signals2::scoped_connection mSelectionConnection;
    boost::signals2::scoped_connection mRunningConnection;
    boost::signals2::scoped_connection mHeardConnection;
    boost::signals2::scoped_connection mRegionUsageConnection;
    boost::signals2::scoped_connection mPresenceConnection;
};
