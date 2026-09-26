/**
 * @file alscriptexplorerpane.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alscriptexplorerpane.h"

#include "alobjectproperties.h"
#include "alscriptexplorertree.h"
#include "alscriptstudiodoc.h"
#include "alscriptstudioservices.h"
#include "llagent.h"
#include "llbutton.h"
#include "llfiltereditor.h"
#include "llfloater.h"
#include "llmenugl.h"
#include "llnotificationsutil.h"
#include "llselectmgr.h"
#include "lltimer.h"
#include "lltooldraganddrop.h"
#include "lluictrlfactory.h"
#include "llviewerassettype.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewermenu.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "roles_constants.h"

#include <algorithm>

namespace
{
    // How often the explorer looks at what is selected in world, and how
    // often at most it is filled again while answers come in.
    const F64 EXPLORER_POLL = 1.0;
    const F64 EXPLORER_FILL = 0.2;

    // The roots selected in world, in their order.
    std::vector<LLUUID> selectedRoots()
    {
        std::vector<LLUUID>     roots;
        LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
        for (auto it = selection->valid_root_begin(); it != selection->valid_root_end(); ++it)
        {
            if (LLViewerObject* object = (*it)->getObject())
            {
                roots.push_back(object->getID());
            }
        }
        return roots;
    }

    LLViewerObject* rootOf(LLViewerObject* object)
    {
        return object && object->getRootEdit() ? object->getRootEdit() : object;
    }
}

static LLPanelInjector<ALScriptExplorerPane> t_script_studio_explorer("script_studio_explorer");

ALScriptExplorerPane::ALScriptExplorerPane(const LLPanel::Params& params) : LLPanel(params) {}

ALScriptExplorerPane::~ALScriptExplorerPane()
{
    // A menu still open calls into this pane, which is going: it goes
    // first. The menus live in the viewer's menu holder, not here.
    if (LLContextMenu* open = mMenu.get())
    {
        open->hide();
        open->die();
    }
}

bool ALScriptExplorerPane::postBuild()
{
    mTree   = getChild<ALScriptExplorerTree>("explorer_tree");
    mFilter = getChild<LLFilterEditor>("explorer_filter");
    // The window this is the explorer of, found through the view tree, as
    // what the explorer asks of it.
    LLFloater* window = getParentByType<LLFloater>();
    mServices         = dynamic_cast<ALScriptStudioServices*>(window);
    mWindow           = dynamic_cast<Window*>(window);
    if (!mServices || !mWindow)
    {
        LL_WARNS() << "The explorer is not in a Script Studio window" << LL_ENDL;
        return true;
    }
    ALScriptExplorerTree::Hooks hooks;
    hooks.opened = [this](const LLSD& row) {
        if (std::optional<Choice> one = Choice::of(row); one && one->isItem())
        {
            mServices->openScript(one->ref(), one->name);
        }
    };
    // Folded or opened by its arrow: what it now shows asked for, where it
    // was not, once the tree is done with the click.
    hooks.folded = [this](const LLSD& row, bool folded) {
        if (mModel.foldRow(row, folded) == Model::Refold::Relist)
        {
            relistSoon(false);
        }
    };
    hooks.renameable = [this](const LLSD& row) {
        const std::optional<Choice> one = Choice::of(row);
        return one && (one->isItem() || mModel.present(one->root));
    };
    hooks.renamed = [this](const LLSD& row, const std::string& name) {
        const std::optional<Choice> one = Choice::of(row);
        if (!one)
        {
            return;
        }
        // An object's row is its root prim's name; a prim's its own.
        std::string was = one->name;
        if (!one->isItem())
        {
            const LLUUID prim = one->primRow ? one->prim : one->root;
            for (const Model::Object& object : mModel.objects())
            {
                for (const Model::Prim& each : object.prims)
                {
                    if (each.id == prim)
                    {
                        was = prim == object.root ? object.name : each.name;
                    }
                }
            }
        }
        renamed(*one, was, name);
    };
    hooks.drop = [this](const LLSD& row, MASK mask, bool dropped, EDragAndDropType type, void* cargo, std::string& tooltip) {
        return drop(row, mask, dropped, type, cargo, tooltip);
    };
    hooks.drag   = [this]() { return startDrag(); };
    // The buttons follow what is chosen.
    hooks.chosen = [this]() { refreshButtons(); };
    hooks.menu   = [this](S32 x, S32 y) { showMenu(x, y); };
    mTree->setHooks(std::move(hooks));
    // A filter looks through what is folded too: what a large linkset's
    // folded prims hold is asked for once there is one.
    mFilter->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (mFilter->getText().empty())
        {
            fill();
        }
        else
        {
            relist();
        }
    });
    for (const char* action : { "open", "start", "stop", "reset", "refresh" })
    {
        getChild<LLButton>(std::string("explorer_") + action)->setCommitCallback([this, action](LLUICtrl*, const LLSD&) { act(action); });
    }
    // A new selection in world, looked at on the next frame rather than at
    // the next second's poll; the signal fires many times a frame while
    // something is edited, so it only marks it.
    mSelectionConnection = LLSelectMgr::getInstance()->mUpdateSignal.connect([this]() { mSelectionChanged = true; });
    // A name the region said of anything the list shows, whether for a
    // selection or asked here, is read on the next frame.
    mPropertiesConnection = ALObjectPropertiesCache::instance().setChangeCallback([this](const LLUUID& id) {
        if (mModel.listed(id))
        {
            mNamesStale = true;
        }
    });
    // Put in the list with the frame, once for all that answered in it: a
    // refresh asks every script whether it runs, and a linkset's hundred
    // answers were a hundred lists.
    mRunningConnection = ALScriptWorkspace::instance().onRunningState([this](const ALScriptWorkspace::RunningState& state) {
        mModel.running(state.ref, state.running);
        mStale = true;
    });
    return true;
}

bool ALScriptExplorerPane::handleKeyHere(KEY key, MASK mask)
{
    if (key == KEY_ESCAPE && mask == MASK_NONE && mServices)
    {
        mServices->revealed(mTree, true);
        return true;
    }
    if (mServices && mTree && mTree->hasFocus())
    {
        if (key == KEY_RETURN && mask == MASK_NONE)
        {
            onChosen();
            return true;
        }
        if ((key == KEY_DELETE && mask == MASK_NONE) || (key == KEY_BACKSPACE && mask == MASK_CONTROL))
        {
            if (enabled("delete"))
            {
                act("delete");
            }
            return true;
        }
    }
    return LLPanel::handleKeyHere(key, mask);
}

// --- what is listed ------------------------------------------------------------------

void ALScriptExplorerPane::pump()
{
    if (!mServices)
    {
        return;
    }
    // The names the region said since the last frame, of what the list
    // shows: read now, when the selection has taken them in too.
    if (mNamesStale)
    {
        mNamesStale = false;
        rereadNames();
    }
    if (mRelistWanted)
    {
        const bool refetch = *mRelistWanted;
        mRelistWanted.reset();
        relist(refetch);
    }
    // What came in since it was last filled -- contents, whether scripts
    // run, names -- put in the list once, however many answers there were,
    // and not at every frame while a linkset's answers stream in.
    const F64 now = LLTimer::getTotalSeconds();
    if (mStale && now >= mFilled + EXPLORER_FILL)
    {
        fill();
    }
    if (mRefetchAt > 0.0 && now >= mRefetchAt)
    {
        mRefetchAt = 0.0;
        relist(true);
    }
    if (!mSelectionChanged && now < mPolled + EXPLORER_POLL)
    {
        return;
    }
    mSelectionChanged         = false;
    mPolled                   = now;
    std::vector<LLUUID> roots = selectedRoots();
    if (roots != mRoots)
    {
        mRoots = std::move(roots);
        relist();
    }
}

void ALScriptExplorerPane::relist(bool refetch)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!mServices)
    {
        return;
    }
    Model::Listing listing;
    listing.seen = [](const LLUUID& id) -> std::optional<Model::Seen> {
        LLViewerObject* object = gObjectList.findObject(id);
        if (!object || object->isAvatar())
        {
            return std::nullopt;
        }
        LLViewerObject* root = rootOf(object);
        Model::Seen     seen;
        seen.prims.push_back({ root->getID(), ALScriptWorkspace::objectName(root, LLStringUtil::null) });
        for (const LLPointer<LLViewerObject>& child : root->getChildren())
        {
            if (child && !child->isAvatar())
            {
                seen.prims.push_back({ child->getID(), ALScriptWorkspace::objectName(child, LLStringUtil::null) });
            }
        }
        return seen;
    };
    listing.nameless = [this](const LLUUID& root) { return nameGivenTo(root); };
    listing.selected = mRoots;
    for (const ALScriptStudioDoc* doc : mServices->openDocs())
    {
        if (!doc->ref.inInventory())
        {
            listing.open.push_back(doc->ref.object);
        }
    }
    listing.unnamed = mServices->words("ObjectUnnamed");
    mModel.list(listing);
    fill();
    std::string filter = mFilter->getText();
    LLStringUtil::trim(filter);
    // What each prim holds, asked where it is not known, where the object
    // says it has changed since, or where a person asked -- not again for
    // every prim at every tab opened or closed, nor for one already asked
    // and not answered yet.
    const std::vector<LLUUID> asking = mModel.toAsk(refetch, !filter.empty(), [](const LLUUID& prim) {
        LLViewerObject* in_world = gObjectList.findObject(prim);
        return in_world && !in_world->isInventoryDirty();
    });
    const LLHandle<LLPanel> handle = getHandle();
    for (const LLUUID& prim : asking)
    {
        ALScriptWorkspace::instance().listContents(prim, [handle](const ALScriptWorkspace::Contents& contents) {
            if (ALScriptExplorerPane* pane = ALViewType::as<ALScriptExplorerPane>(handle.get()))
            {
                pane->contentsHeard(contents);
            }
        });
    }
}

void ALScriptExplorerPane::contentsHeard(const ALScriptWorkspace::Contents& contents)
{
    Model::Heard heard = mModel.contents(contents);
    if (!heard.listed)
    {
        return;
    }
    if (mModel.takePinsChanged())
    {
        mWindow->explorerPinsChanged();
    }
    for (const ALScriptRef& ref : heard.askRunning)
    {
        ALScriptWorkspace::instance().askRunning(ref);
    }
    mStale = true;
    for (Model::Opening& one : heard.opening)
    {
        mServices->openScript(one.ref, one.name, std::move(one.text));
    }
}

void ALScriptExplorerPane::rereadNames()
{
    if (mModel.rereadNames([](const LLUUID& prim) { return ALScriptWorkspace::objectName(gObjectList.findObject(prim), LLStringUtil::null); }))
    {
        mStale = true;
    }
    if (mModel.takePinsChanged())
    {
        mWindow->explorerPinsChanged();
    }
}

void ALScriptExplorerPane::askName(const LLUUID& id)
{
    // Once while it is listed: the answer comes by the object properties
    // cache, which says so, and the name is read from there. Not what is
    // selected, whose properties are on their way already.
    LLViewerObject* object = gObjectList.findObject(id);
    if (!object || !object->getRegion() || LLSelectMgr::getInstance()->getSelection()->findNode(object) || !mModel.askName(id))
    {
        return;
    }
    LLSelectMgr::getInstance()->requestObjectPropertiesFamily(object);
}

std::string ALScriptExplorerPane::nameGivenTo(const LLUUID& root) const
{
    // Never heard: what a pin remembers it by, or a script open from it.
    for (const Model::Pin& pin : mModel.pins())
    {
        if (pin.root == root && !pin.name.empty())
        {
            return pin.name;
        }
    }
    for (const ALScriptStudioDoc* doc : mServices->openDocs())
    {
        if (doc->ref.inInventory() || doc->objectName.empty())
        {
            continue;
        }
        const LLViewerObject* top = rootOf(gObjectList.findObject(doc->ref.object));
        if (top && top->getID() == root)
        {
            return doc->objectName;
        }
    }
    // Asked of the region, and on its way: said as something coming, not
    // as a name the object has.
    return mServices->words("ObjectNameComing");
}

// --- the rows ------------------------------------------------------------------------

void ALScriptExplorerPane::fill()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    mStale  = false;
    mFilled = LLTimer::getTotalSeconds();
    std::string filter = mFilter->getText();
    LLStringUtil::trim(filter);
    const std::vector<Model::Row> rows = mModel.rows(filter);
    // What a row is -- an object, a prim, a script -- is its icon, and a
    // script's state and an object out of sight follow its name, as the
    // inventory's worn items say so.
    const auto said = [this](const std::string& word) { return " (" + mServices->words(word) + ")"; };
    const auto look = [&](const Model::Row& row) {
        ALScriptExplorerTree::Look out;
        switch (row.kind)
        {
            case Model::Row::Kind::Object:
                out.label  = (row.pinned ? mServices->words("PinnedMark") : LLStringUtil::null) + row.name;
                out.suffix = row.present ? LLStringUtil::null : said("KindAway");
                out.icon   = row.many ? "Inv_Object_Multi" : "Inv_Object";
                break;
            case Model::Row::Kind::Empties:
            {
                LLStringUtil::format_map_t args;
                args["[COUNT]"] = std::to_string(row.empties);
                out.label       = mServices->words("ExplorerEmptyPrims", args);
                out.icon        = "Studio_Prim";
                break;
            }
            case Model::Row::Kind::Prim:
                out.label = row.name.empty() ? mServices->words("ObjectNameComing") : row.name;
                out.icon  = "Studio_Prim";
                break;
            case Model::Row::Kind::Item:
                out.label = row.name;
                if (row.script)
                {
                    const std::optional<bool> running = knownRunning(row.ref);
                    out.suffix = said(!running ? "StateUnknown" : *running ? "RunningYes" : "RunningNo");
                }
                out.icon = row.script ? (row.lua ? "Inv_Script_Luau" : "Inv_Script")
                           : row.name == ".luaurc" || row.name == ".lslrc" ? "Studio_Config"
                                                                           : "Inv_Notecard";
                break;
        }
        return out;
    };
    // The explorer is empty exactly when nothing is selected -- an object
    // listed has a row of its own, whatever it holds -- so what it says
    // while empty is what it is for, or that the filter found none.
    mTree->show(rows, look, filter, mServices->words(!filter.empty() && !mModel.objects().empty() ? "ExplorerNoMatch" : "NoExplorerSelection"));
    // Shown without its own name, not being selected: asked of its region.
    for (const Model::Row& row : rows)
    {
        if (row.unnamed)
        {
            askName(Model::primOf(row.value));
        }
    }
    refreshButtons();
}

void ALScriptExplorerPane::refreshButtons()
{
    for (const char* action : { "open", "start", "stop", "reset" })
    {
        if (LLButton* button = findChild<LLButton>(std::string("explorer_") + action))
        {
            button->setEnabled(enabled(action));
        }
    }
}

std::vector<ALScriptExplorerPane::Choice> ALScriptExplorerPane::choice() const
{
    std::vector<Choice> rows;
    if (!mTree)
    {
        return rows;
    }
    for (const LLSD& value : mTree->chosen())
    {
        if (std::optional<Choice> row = Choice::of(value))
        {
            rows.push_back(std::move(*row));
        }
    }
    return rows;
}

std::optional<bool> ALScriptExplorerPane::knownRunning(const ALScriptRef& ref) const
{
    if (const ALScriptStudioDoc* doc = mServices->findDoc(ref); doc && doc->running >= 0)
    {
        return doc->running != 0;
    }
    return mModel.knownRunning(ref);
}

void ALScriptExplorerPane::onChosen()
{
    // A script or notecard opened; an object, a prim or a linkset's row of
    // prims holding nothing opened where it is folded, folded where it is
    // open.
    for (const LLSD& value : mTree->chosen())
    {
        const std::optional<Choice> row = Choice::of(value);
        if (row && row->isItem())
        {
            mServices->openScript(row->ref(), row->name);
        }
        else
        {
            mTree->toggle(value);
        }
    }
}

void ALScriptExplorerPane::refold(Model::Refold refold)
{
    switch (refold)
    {
        case Model::Refold::None: break;
        case Model::Refold::Refill: fill(); break;
        // Opened: what it now shows asked for, where it was not.
        case Model::Refold::Relist: relist(); break;
    }
}

void ALScriptExplorerPane::relistSoon(bool refetch)
{
    mRelistWanted = refetch || mRelistWanted.value_or(false);
}

void ALScriptExplorerPane::fillSoon()
{
    mStale  = true;
    mFilled = 0.0;
}

void ALScriptExplorerPane::reveal(const ALScriptStudioDoc& doc)
{
    // What holds it unfolded first, and asked what it holds where, folded
    // in a large linkset, it never was, for its row to come; then its row
    // chosen, with the keyboard, the filter let go of where it hides it.
    mWindow->showExplorer();
    if (mModel.unfoldTo(doc.ref.object))
    {
        relist();
    }
    fill();
    LLSD row;
    row["prim"] = doc.ref.object;
    row["item"] = doc.ref.item;
    if (!mTree->choose(row, true) && !mFilter->getText().empty())
    {
        mFilter->setText(LLStringUtil::null);
        fill();
        mTree->choose(row, true);
    }
    refreshButtons();
}

void ALScriptExplorerPane::explore(const LLUUID& root)
{
    if (!mModel.isPinned(root))
    {
        mModel.togglePinned(root, ALScriptWorkspace::objectName(gObjectList.findObject(root), mServices->words("ObjectUnnamed")));
        mWindow->explorerPinsChanged();
    }
    mWindow->showExplorer();
    // Open, whatever it was: what it holds is what it was asked to show.
    mModel.fold(root, false, false);
    relist();
    LLSD row;
    row["root"] = root;
    mTree->choose(row, false);
    refreshButtons();
}

// --- what can be done with the rows ----------------------------------------------------

bool ALScriptExplorerPane::enabled(const std::string& action) const
{
    const std::vector<Choice> rows = choice();
    auto                      any  = [&rows](auto test) { return std::any_of(rows.begin(), rows.end(), test); };
    auto                      present = [this](const Choice& row) { return mModel.present(row.root); };
    if (action == "refresh")
    {
        return true;
    }
    if (action == "copy")
    {
        return !rows.empty();
    }
    if (action == "open")
    {
        return any([](const Choice& row) { return row.isItem(); });
    }
    if (action == "new_lsl" || action == "new_lua" || action == "new_notecard")
    {
        // One prim to put it in.
        return rows.size() == 1 && present(rows.front()) &&
               (action != "new_lua" || ALScriptWorkspace::luaEnabled(ALScriptRef(rows.front().prim, LLUUID::null)));
    }
    if (action == "rename")
    {
        // A script or notecard; or a prim or an object, which is in sight.
        return rows.size() == 1 && (rows.front().isItem() || present(rows.front()));
    }
    if (action == "delete")
    {
        return !rows.empty() && !any([](const Choice& row) { return !row.isItem(); });
    }
    if (action == "start" || action == "stop" || action == "reset" || action == "restart" || action == "recompile")
    {
        // Scripts, or whole prims and objects, which the queues walk;
        // restart is one script at a time.
        return any([&](const Choice& row) { return present(row) && (row.script || (action != "restart" && !row.isItem())); });
    }
    if (action == "teleport" || action == "zoom")
    {
        return rows.size() == 1 && present(rows.front());
    }
    if (action == "pin")
    {
        return !rows.empty();
    }
    return false;
}

void ALScriptExplorerPane::act(const std::string& action)
{
    if (action == "refresh")
    {
        mModel.forgetRunning();
        relist(true);
        return;
    }
    if (action == "copy")
    {
        mTree->copy();
        return;
    }
    const std::vector<Choice> rows = choice();
    if (rows.empty())
    {
        return;
    }
    if (action == "open")
    {
        onChosen();
    }
    else if (action == "new_lsl" || action == "new_lua" || action == "new_notecard")
    {
        create(rows.front().prim, action == "new_notecard", action == "new_lua");
    }
    else if (action == "rename")
    {
        mTree->rename();
    }
    else if (action == "delete")
    {
        remove(rows);
    }
    else if (action == "recompile")
    {
        recompile(rows);
    }
    else if (action == "start" || action == "stop" || action == "reset" || action == "restart")
    {
        // Resetting or stopping more than one script -- a whole object, a
        // linkset's worth -- is asked about first: what a script was doing
        // is not got back.
        const S32 reached = (action == "reset" || action == "stop") ? mModel.scriptsReached(rows) : 0;
        if (reached > 1)
        {
            LLSD args;
            args["COUNT"]                  = reached;
            const LLHandle<LLPanel> handle = getHandle();
            LLNotificationsUtil::add(action == "reset" ? "ScriptStudioResetScripts" : "ScriptStudioStopScripts", args, LLSD(),
                                     [handle, action, rows](const LLSD& notification, const LLSD& response) {
                                         ALScriptExplorerPane* pane = ALViewType::as<ALScriptExplorerPane>(handle.get());
                                         if (pane && LLNotificationsUtil::getSelectedOption(notification, response) == 0)
                                         {
                                             pane->run(action, rows);
                                         }
                                     });
            return;
        }
        run(action, rows);
    }
    else if (action == "teleport" || action == "zoom")
    {
        if (LLViewerObject* object = gObjectList.findObject(rows.front().root))
        {
            if (action == "teleport")
            {
                gAgent.teleportViaLocation(object->getPositionGlobal());
            }
            else
            {
                handle_zoom_to_object(object->getID());
            }
        }
    }
    else if (action == "pin")
    {
        mModel.pin(rows);
        mWindow->explorerPinsChanged();
        relist();
    }
}

void ALScriptExplorerPane::run(const std::string& action, const std::vector<Choice>& rows)
{
    // Each script chosen on its own; the prims and objects chosen through a
    // queue -- and a script chosen with its prim once, by the queue, which
    // walks it anyway. Restart is a script's alone.
    ALScriptWorkspace&                                workspace = ALScriptWorkspace::instance();
    const std::vector<std::pair<LLUUID, std::string>> prims =
        action == "restart" ? std::vector<std::pair<LLUUID, std::string>>() : mModel.containerPrims(rows);
    for (const Choice& row : rows)
    {
        if (!row.script || Model::walkedByQueue(row, prims))
        {
            continue;
        }
        const ALScriptRef ref = row.ref();
        if (action == "reset")
        {
            workspace.reset(ref);
        }
        else if (action == "restart")
        {
            // Stopped and set running again, keeping its state.
            workspace.restart(ref);
        }
        else if (workspace.setRunning(ref, action == "start"))
        {
            workspace.askRunning(ref);
        }
    }
    if (!prims.empty())
    {
        const auto kind = action == "start"  ? ALScriptWorkspace::Queue::Start
                          : action == "stop" ? ALScriptWorkspace::Queue::Stop
                                             : ALScriptWorkspace::Queue::Reset;
        std::string error;
        if (!workspace.queue(kind, prims, LLStringUtil::null, error))
        {
            mServices->report(error, true);
        }
    }
}

void ALScriptExplorerPane::showMenu(S32 x, S32 y)
{
    // At a point of the tree, which has chosen the row under it already.
    if (!LLMenuGL::sMenuContainer)
    {
        return;
    }
    if (LLContextMenu* old = mMenu.get())
    {
        old->die();
        mMenu.markDead();
    }
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
    commit.add("Explorer.Action", [this](LLUICtrl*, const LLSD& param) { act(param.asString()); });
    enable.add("Explorer.Enable", [this](LLUICtrl*, const LLSD& param) { return enabled(param.asString()); });
    enable.add("Explorer.Check", [this](LLUICtrl*, const LLSD& param) {
        const std::vector<Choice> rows = choice();
        return param.asString() == "pin" && !rows.empty() && mModel.isPinned(rows.front().root);
    });
    LLContextMenu* menu = LLUICtrlFactory::createFromFile<LLContextMenu>("menu_script_studio_explorer.xml", LLMenuGL::sMenuContainer,
                                                                          LLMenuHolderGL::child_registry_t::instance());
    if (!menu)
    {
        return;
    }
    mMenu = menu->getHandle();
    menu->show(x, y);
    LLMenuGL::showPopup(mTree, menu, x, y);
}

// --- making, renaming, deleting and recompiling ----------------------------------------

void ALScriptExplorerPane::create(const LLUUID& prim, bool notecard, bool lua)
{
    LLSD args;
    args["KIND"]                   = mServices->words(notecard ? "NewKindNotecard" : lua ? "NewKindLua" : "NewKindScript");
    args["NAME"]                   = mServices->words(notecard ? "NewNotecardName" : "NewScriptName");
    const LLHandle<LLPanel> handle = getHandle();
    LLNotificationsUtil::add("ScriptStudioNewItem", args, LLSD(), [handle, prim, notecard, lua](const LLSD& notification, const LLSD& response) {
        ALScriptExplorerPane* pane = ALViewType::as<ALScriptExplorerPane>(handle.get());
        if (!pane || LLNotificationsUtil::getSelectedOption(notification, response) != 0)
        {
            return;
        }
        std::string name = response["name"].asString();
        LLStringUtil::trim(name);
        if (name.empty())
        {
            return;
        }
        // What the new script starts with, where the scripter wrote one:
        // put in when it opens, in place of the region's, to be saved.
        // Carried with this item's own answer, so that items made one after
        // another each open with theirs.
        const std::string          text    = notecard ? std::string() : gSavedSettings.getString(lua ? "ALScriptTemplateSLua" : "ALScriptTemplateLSL");
        std::optional<std::string> opening = text.empty() ? std::nullopt : std::optional<std::string>(text);
        std::string                error;
        const bool                 asked = ALScriptWorkspace::instance().create(
            prim, notecard, lua, name,
            [handle, opening](const ALScriptWorkspace::Created& made) {
                if (ALScriptExplorerPane* again = ALViewType::as<ALScriptExplorerPane>(handle.get()))
                {
                    again->created(made, opening);
                }
            },
            error);
        if (!asked)
        {
            pane->mServices->report(error, true);
        }
    });
}

void ALScriptExplorerPane::created(const ALScriptWorkspace::Created& made, const std::optional<std::string>& opening)
{
    if (!made.error.empty())
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"]  = made.name;
        args["[ERROR]"] = made.error;
        mServices->report(mServices->words("CreateFailed", args), true);
        return;
    }
    // Opened once the prim lists it: by id where the region said, by
    // name otherwise; beside any others made and not listed yet.
    mModel.openWhenListed(made.prim, made.item, made.name, opening);
    relist(true);
}

void ALScriptExplorerPane::renamed(const Choice& row, const std::string& was, std::string name)
{
    LLStringUtil::trim(name);
    if (name.empty() || name == was)
    {
        return;
    }
    std::string error;
    if (row.isItem())
    {
        const ALScriptRef ref = row.ref();
        if (!ALScriptWorkspace::instance().rename(ref, name, error))
        {
            mServices->report(error, true);
            return;
        }
        // The tab, if it is open, and the list, once the tree is done with
        // the name typed.
        mWindow->itemRenamed(ref, name);
        relistSoon(true);
        return;
    }
    const LLUUID prim = row.primRow ? row.prim : row.root;
    if (!ALScriptWorkspace::instance().renameObject(prim, name, error))
    {
        mServices->report(error, true);
        return;
    }
    // Called so here at once, and in the pin; and asked of the region
    // again, whose answer is what the names are read from, so that the
    // next reading has it too.
    mModel.renamed(prim, name);
    if (mModel.takePinsChanged())
    {
        mWindow->explorerPinsChanged();
    }
    askName(prim);
    fillSoon();
}

void ALScriptExplorerPane::remove(const std::vector<Choice>& rows)
{
    std::vector<Choice> items;
    for (const Choice& row : rows)
    {
        if (row.isItem())
        {
            items.push_back(row);
        }
    }
    if (items.empty())
    {
        return;
    }
    LLSD args;
    args["COUNT"] = static_cast<S32>(items.size());
    args["NAME"]  = items.front().name;
    // A tab holding one with unsaved changes goes with it, in whichever
    // window it is, which the question says rather than leaving to be
    // found out.
    const bool unsaved = std::any_of(items.begin(), items.end(), [this](const Choice& row) { return mWindow->unsavedAnywhere(row.ref()); });
    // Nothing unsaved in it: one question, one item or many, which may be
    // left unasked for the rest of the session, as the build tools' is.
    // With unsaved changes it is asked every time.
    const char* question = unsaved ? (items.size() == 1 ? "ScriptStudioDeleteItemOpen" : "ScriptStudioDeleteItemsOpen") : "ScriptStudioDeleteItems";
    if (!unsaved)
    {
        LLStringUtil::format_map_t words;
        words["[NAME]"]  = items.front().name;
        words["[COUNT]"] = std::to_string(items.size());
        args["QUESTION"] = mServices->words(items.size() == 1 ? "DeleteItemAsk" : "DeleteItemsAsk", words);
    }
    const LLHandle<LLPanel> handle = getHandle();
    LLNotificationsUtil::add(question, args, LLSD(), [handle, items](const LLSD& notification, const LLSD& response) {
        ALScriptExplorerPane* pane = ALViewType::as<ALScriptExplorerPane>(handle.get());
        if (!pane || LLNotificationsUtil::getSelectedOption(notification, response) != 0)
        {
            return;
        }
        for (const Choice& row : items)
        {
            const ALScriptRef ref = row.ref();
            std::string       error;
            if (!ALScriptWorkspace::instance().remove(ref, error))
            {
                pane->mServices->report(error, true);
                continue;
            }
            // Its tab goes with it, whatever was typed there, in whichever
            // window holds it.
            pane->mWindow->itemDeleted(ref);
        }
        pane->relist(true);
    });
}

void ALScriptExplorerPane::recompile(const std::vector<Choice>& rows)
{
    // Each script chosen goes up again on its own, for what it compiles
    // for now; a prim or an object chosen has every script walked by the
    // compile queue, which reports in a window of its own. A script known
    // to be stopped stays stopped, which the standard viewer's recompile
    // does not do; one not known to be either runs after, as there.
    const LLHandle<LLPanel>                           handle = getHandle();
    const std::vector<std::pair<LLUUID, std::string>> prims  = mModel.containerPrims(rows);
    for (const Choice& row : rows)
    {
        // A script chosen with its prim is compiled once, by the queue.
        if (!row.script || Model::walkedByQueue(row, prims))
        {
            continue;
        }
        const ALScriptRef          ref  = row.ref();
        const std::string          name = row.name;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = name;
        mServices->setStatus(mServices->words("Recompiling", args));
        const std::optional<bool> running = knownRunning(ref);
        ALScriptWorkspace::instance().recompile(
            ref, "auto",
            [handle, ref, name](const ALScriptWorkspace::CompileResult& result) {
                ALScriptExplorerPane* pane = ALViewType::as<ALScriptExplorerPane>(handle.get());
                if (!pane || pane->mServices->findDoc(ref))
                {
                    // An open script hears of it through the listener, and
                    // shows what the compiler said.
                    return;
                }
                ALScriptStudioServices&    services = *pane->mServices;
                LLStringUtil::format_map_t args;
                args["[NAME]"] = name;
                if (!result.error.empty())
                {
                    args["[ERROR]"] = result.error;
                    services.report(services.words("SaveFailed", args), true);
                }
                else if (result.success)
                {
                    services.report(services.words("Compiled", args));
                }
                else
                {
                    services.report(services.counted("CompileFailed", static_cast<S32>(result.diagnostics.size()), args), true);
                }
            },
            running);
    }
    if (!prims.empty())
    {
        std::string                               error;
        std::map<std::pair<LLUUID, LLUUID>, bool> running = mModel.runningKnown();
        for (const ALScriptStudioDoc* doc : mServices->openDocs())
        {
            if (!doc->ref.inInventory() && doc->running >= 0)
            {
                running[{ doc->ref.object, doc->ref.item }] = doc->running != 0;
            }
        }
        if (!ALScriptWorkspace::instance().queue(ALScriptWorkspace::Queue::Recompile, prims, "auto", error, std::move(running)))
        {
            mServices->report(error, true);
        }
    }
}

// --- dragging out and dropping in --------------------------------------------------------

bool ALScriptExplorerPane::startDrag()
{
    // One drag comes out of one prim: the chosen items of the first chosen
    // item's prim. Each goes as the build floater's contents let it go --
    // a copy where it may be copied and given, the item itself out of an
    // object of one's own where it may not -- and nothing comes out of a
    // locked attachment, nor anything but a copy out of any attachment,
    // whose contents the region does not keep up with.
    std::vector<EDragAndDropType> types;
    uuid_vec_t                    ids;
    LLUUID                        from;
    for (const Choice& row : choice())
    {
        if (!row.isItem() || (from.notNull() && row.prim != from))
        {
            continue;
        }
        LLViewerObject*        object = gObjectList.findObject(row.prim);
        const LLInventoryItem* item   = object ? dynamic_cast<const LLInventoryItem*>(object->getInventoryObject(row.item)) : nullptr;
        if (!item)
        {
            continue;
        }
        if (!ALScriptWorkspace::takeable(object, *item))
        {
            continue;
        }
        from = row.prim;
        types.push_back(LLViewerAssetType::lookupDragAndDropType(item->getType()));
        ids.push_back(item->getUUID());
    }
    if (ids.empty())
    {
        return false;
    }
    LLToolDragAndDrop::getInstance()->beginMultiDrag(types, ids, LLToolDragAndDrop::SOURCE_WORLD, from);
    return true;
}

void ALScriptExplorerPane::transfer(const LLUUID& from, const std::vector<LLUUID>& items, const LLUUID& to, bool running)
{
    LLStringUtil::format_map_t args;
    args["[NAME]"] = ALScriptWorkspace::objectName(rootOf(gObjectList.findObject(to)), mServices->words("ObjectUnnamed"));
    mServices->setStatus(mServices->words("TransferGoing", args));
    const LLHandle<LLPanel> handle = getHandle();
    ALScriptWorkspace::instance().transfer(from, items, to, running, [handle, args](const ALScriptWorkspace::TransferResult& result) {
        ALScriptExplorerPane* pane = ALViewType::as<ALScriptExplorerPane>(handle.get());
        if (!pane)
        {
            return;
        }
        const auto listed = [](const std::vector<std::string>& names) {
            std::string out;
            for (const std::string& name : names)
            {
                out += (out.empty() ? "" : ", ") + name;
            }
            return out;
        };
        LLStringUtil::format_map_t said = args;
        said["[ERROR]"]                  = result.error;
        said["[REFUSED]"]                = listed(result.refused);
        said["[STRANDED]"]               = listed(result.stranded);
        said["[LOST]"]                   = listed(result.lost);
        ALScriptStudioServices& services = *pane->mServices;
        std::string             words    = !result.error.empty() ? services.words("TransferFailed", said)
                                           : result.moved > 0    ? services.counted("TransferDone", result.moved, said)
                                                                 : services.words("TransferNone", said);
        if (!result.refused.empty())
        {
            words += " " + services.words("TransferRefused", said);
        }
        if (!result.stranded.empty())
        {
            words += " " + services.words("TransferStranded", said);
        }
        if (!result.lost.empty())
        {
            words += " " + services.words("TransferLost", said);
        }
        services.report(words, !result.error.empty() || !result.refused.empty() || !result.stranded.empty() || !result.lost.empty());
        // The prims listed again, as after a drop from the inventory.
        pane->relist(true);
        pane->mRefetchAt = LLTimer::getTotalSeconds() + 2.0;
    });
}

LLViewerObject* ALScriptExplorerPane::dropTarget() const
{
    // What is chosen in the list, where all of it is of one prim.
    LLUUID chosen;
    for (const Choice& row : choice())
    {
        if (chosen.notNull() && row.prim != chosen)
        {
            return nullptr;
        }
        chosen = row.prim;
    }
    if (chosen.notNull())
    {
        return gObjectList.findObject(chosen);
    }
    // Nothing chosen: the object selected in world, as the build floater's
    // contents are of it -- one object, or one prim of one.
    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
    LLSelectNode*           node      = selection->getFirstRootNode(nullptr, true);
    if (!node || !node->mValid || (selection->getRootObjectCount() != 1 && selection->getObjectCount() != 1))
    {
        return nullptr;
    }
    return node->getObject();
}

bool ALScriptExplorerPane::drop(const LLSD& row, MASK mask, bool drop, EDragAndDropType type, void* cargo, std::string& tooltip)
{
    // Into the prim the row is of: an item's, a prim's own, an object's
    // root, which is where a drop on the object in world goes too; below
    // the rows, the prim of what is chosen. The row of prims holding
    // nothing is of none.
    LLViewerObject* prim = row.isDefined() ? gObjectList.findObject(Model::primOf(row)) : dropTarget();
    if (!prim)
    {
        return false;
    }
    if (!dropIntoPrim(prim, mask, drop, type, cargo))
    {
        // Why not, beside the pointer, rather than a refusal with no word.
        tooltip = dropRefusal(prim, type, cargo);
        return false;
    }
    return true;
}

std::string ALScriptExplorerPane::dropRefusal(LLViewerObject* prim, EDragAndDropType type, void* cargo) const
{
    LLStringUtil::format_map_t args;
    args["[NAME]"] = ALScriptWorkspace::objectName(rootOf(prim), mServices->words("ObjectUnnamed"));
    if (std::string refused = ALScriptWorkspace::rlvRefusal(prim, LLAssetType::AT_NONE, ALScriptWorkspace::RlvUse::Change); !refused.empty())
    {
        return refused;
    }
    if (!prim->permModify())
    {
        return mServices->words("DropNotYours", args);
    }
    if (LLToolDragAndDrop::getInstance()->getSource() == LLToolDragAndDrop::SOURCE_NOTECARD)
    {
        return mServices->words("DropFromNotecard", args);
    }
    const LLViewerInventoryItem* item = type == DAD_CATEGORY ? nullptr : static_cast<const LLViewerInventoryItem*>(cargo);
    if (item && !gAgent.allowOperation(PERM_TRANSFER, item->getPermissions(), GP_OBJECT_MANIPULATE) && !prim->permYouOwner())
    {
        return mServices->words("DropNotTransferable", args);
    }
    return mServices->words("DropRefused", args);
}

bool ALScriptExplorerPane::dropIntoPrim(LLViewerObject* prim, MASK mask, bool drop, EDragAndDropType type, void* cargo)
{
    // Nothing from a notecard, which the drag tool will not put into an
    // object.
    LLToolDragAndDrop*               tool   = LLToolDragAndDrop::getInstance();
    const LLToolDragAndDrop::ESource source = tool->getSource();
    if (source == LLToolDragAndDrop::SOURCE_NOTECARD)
    {
        return false;
    }
    if (source == LLToolDragAndDrop::SOURCE_WORLD)
    {
        // From another prim -- this list's, or the build floater's contents
        // -- by way of the agent's inventory, the only way between two
        // objects: gathered as the drag tool drops each, and sent together
        // with the last. A script goes in running unless Control is held.
        LLViewerInventoryItem* item = static_cast<LLViewerInventoryItem*>(cargo);
        const LLUUID           from = tool->getSourceID();
        const bool ok = type != DAD_CATEGORY && item && gObjectList.findObject(from) && LLToolDragAndDrop::isInventoryDropAcceptable(prim, item);
        if (!ok || !drop)
        {
            return ok;
        }
        if (tool->getCargoIndex() == 0)
        {
            mTransferring.clear();
        }
        mTransferring.push_back(item->getUUID());
        if (tool->getCargoIndex() + 1 >= static_cast<S32>(tool->getCargoCount()))
        {
            transfer(from, std::exchange(mTransferring, {}), prim->getID(), (mask & MASK_CONTROL) == 0);
        }
        return true;
    }
    // As the build floater's contents take it.
    const bool ok = tool->dropIntoContents(prim, mask, drop, type, cargo);
    if (ok && drop)
    {
        // Listed again once the tree is done with the drop, and again in a
        // moment for what a folder sends once its items are in.
        relistSoon(true);
        mRefetchAt = LLTimer::getTotalSeconds() + 2.0;
    }
    return ok;
}
