/**
 * @file alscriptlinkpane.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alscriptlinkpane.h"

#include "alpanelist.h"
#include "alscriptstudiopane.h"
#include "alscriptstudioservices.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "lldir.h"
#include "lltextbox.h"
#include "lluicolortable.h"

#include <algorithm>

static LLPanelInjector<ALScriptLinkPane> t_script_studio_link("script_studio_link");

namespace
{
    typedef ALScriptLinkScripts::World World;
    typedef ALScriptLinkScripts::Stage Stage;

    // A file in the picker, by its name; its path beside it.
    std::string fileNameOf(const std::string& path)
    {
        return gDirUtilp ? gDirUtilp->getBaseFileName(path) : path;
    }
}

ALScriptLinkPane::ALScriptLinkPane(const LLPanel::Params& params) : LLPanel(params), mGather([this]() { fill(); }) {}

bool ALScriptLinkPane::postBuild()
{
    mList        = getChild<ALPaneList>("link_list");
    mHead        = getChild<LLTextBox>("link_head");
    mChoose      = getChild<LLButton>("link_choose");
    mLink        = getChild<LLButton>("link_do");
    mCancel      = getChild<LLButton>("link_cancel");
    mSendDiffers = getChild<LLCheckBoxCtrl>("link_send_differing");
    // The window this is a tab of, found through the view tree, as what
    // the tab asks of it.
    if (!ALScriptStudioPane::findWindow(*this, "The Link tab", mServices, mWindow))
    {
        return true;
    }
    // A box ticked is read as the row is chosen; the buttons follow the
    // row chosen.
    mList->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        readBoxes();
        fill();
    });
    mList->setSpace([this]() {
        const LLScrollListItem* item  = mList->getFirstSelected();
        const size_t            index = item ? static_cast<size_t>(item->getValue().asInteger()) : mGather.rows().size();
        if (index < mGather.rows().size())
        {
            mGather.tick(index, !mGather.rows()[index].ticked);
        }
    });
    // Return and a double-click choose the row's file; escape goes back to
    // the script.
    mList->setGo([this]() { chooseFile(); });
    mList->setBack([this]() { mServices->revealed(mList, true); });
    mList->setCopyable(true);
    mList->setRowTip([this](const LLScrollListItem* item) {
        const size_t index = static_cast<size_t>(item->getValue().asInteger());
        return index < mGather.rows().size() ? tipOf(mGather.rows()[index]) : std::string();
    });
    mChoose->setCommitCallback([this](LLUICtrl*, const LLSD&) { chooseFile(); });
    mLink->setCommitCallback([this](LLUICtrl*, const LLSD&) { linkTicked(); });
    mCancel->setCommitCallback([this](LLUICtrl*, const LLSD&) { cancel(); });
    fill();
    return true;
}

void ALScriptLinkPane::gather(std::vector<ALScriptLinkScripts::Prim> prims)
{
    mGather.start(std::move(prims));
}

size_t ALScriptLinkPane::rowOf(const ALScriptRef& ref) const
{
    const std::vector<Row>& rows = mGather.rows();
    const auto              at   = std::find_if(rows.begin(), rows.end(), [&ref](const Row& row) { return row.ref == ref; });
    return static_cast<size_t>(at - rows.begin());
}

std::string ALScriptLinkPane::head() const
{
    typedef LLStringUtil::format_map_t Args;
    const ALScriptLinkScripts::Tally& tally   = mGather.tally();
    const std::string                 objects = mServices->listed(mGather.objects());
    switch (mGather.stage())
    {
        case Stage::Idle: return std::string();
        case Stage::Listing: return mServices->words("LinkHeadListing", Args{ { "[OBJECTS]", objects } });
        case Stage::Reading:
            return mServices->words("LinkHeadReading", Args{ { "[OBJECTS]", objects },
                                                            { "[DONE]", std::to_string(tally.read) },
                                                            { "[COUNT]", std::to_string(tally.toRead) } });
        case Stage::Matching: return mServices->counted("LinkHeadMatching", static_cast<S32>(mGather.rows().size()));
        case Stage::Done: break;
    }
    // What was found, then what of the scripts is not listed, and why.
    std::string said = mGather.rows().empty() ? mServices->words("LinkHeadNone", Args{ { "[OBJECTS]", objects } })
                                              : mServices->counted("LinkHeadFound", static_cast<S32>(mGather.rows().size()), Args{ { "[OBJECTS]", objects } });
    std::vector<std::string> aside;
    if (tally.linked > 0)
    {
        aside.push_back(mServices->counted("LinkHeadAlready", tally.linked));
    }
    if (tally.unreadable > 0)
    {
        aside.push_back(mServices->counted("LinkHeadUnread", tally.unreadable));
    }
    if (tally.unlisted > 0)
    {
        aside.push_back(mServices->counted("LinkHeadUnlisted", tally.unlisted));
    }
    if (mGather.asking())
    {
        aside.push_back(mServices->words("LinkHeadChecking"));
    }
    return mServices->sentence(mServices->clauses(said, mServices->listed(aside)));
}

std::string ALScriptLinkPane::tipOf(const Row& row) const
{
    LLStringUtil::format_map_t args;
    args["[NAME]"]  = row.name;
    args["[PLACE]"] = row.place;
    args["[HINT]"]  = row.hint;
    args["[FILE]"]  = row.file;
    std::vector<std::string> said;
    said.push_back(row.place.empty() ? row.name : mServices->words("ScriptInObject", { { "[NAME]", row.name }, { "[OBJECT]", row.place } }));
    if (!row.hint.empty())
    {
        args["[WHY]"] = row.hintWhy;
        said.push_back(mServices->words(row.hintWhy.empty() ? "LinkTipHint" : "LinkTipHintWhy", args));
    }
    if (row.named)
    {
        said.push_back(mServices->words("LinkTipNamed", args));
    }
    if (row.how == How::Record)
    {
        said.push_back(mServices->words(row.own ? "LinkTipOwnRecord" : "LinkTipRecord", args));
    }
    if (row.file.empty() && !row.choices.empty())
    {
        said.push_back(mServices->counted(row.choicesHow == How::Record ? "LinkTipRecords" : "LinkTipChoices", static_cast<S32>(row.choices.size()), args));
    }
    if (row.world == World::Unknown)
    {
        args["[WHY]"] = row.worldWhy;
        said.push_back(mServices->words(row.worldWhy.empty() ? "LinkTipUnknown" : "LinkTipUnknownWhy", args));
    }
    if (!row.owned)
    {
        said.push_back(mServices->words("LinkTipNotOwned", args));
    }
    std::string tip;
    for (const std::string& line : said)
    {
        tip += (tip.empty() ? "" : "\n") + line;
    }
    return tip;
}

void ALScriptLinkPane::fill()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!mServices)
    {
        return;
    }
    mHead->setText(head());
    // The words every row of a kind says, looked up once for all of them.
    const std::string how_hint    = mServices->words("LinkHowHint");
    const std::string how_named   = mServices->words("LinkHowNamed");
    const std::string how_record  = mServices->words("LinkHowRecord");
    const std::string how_own     = mServices->words("LinkHowOwnRecord");
    const std::string how_name    = mServices->words("LinkHowName");
    const std::string how_picked  = mServices->words("LinkHowPicked");
    const std::string none_found  = mServices->words("LinkFileNone");
    const std::string world_asked = mServices->words("LinkWorldAsking");
    const std::string world_same  = mServices->words("LinkWorldSame");
    const std::string world_other = mServices->words("LinkWorldDiffers");
    const std::string world_dunno = mServices->words("LinkWorldUnknown");
    static const LLUIColor caution = LLUIColorTable::instance().getColor("AlertCautionTextColor", LLColor4::yellow);
    // Where it is said by its prim too: in a prim not the root, or among
    // the scripts of more than one object.
    const bool many_objects = mGather.objects().size() > 1;
    const auto cell         = [](const char* column, const LLSD& value, const char* type = "text") {
        LLScrollListCell::Params one;
        one.column = column;
        one.type   = type;
        one.value  = value;
        return one;
    };
    const std::vector<Row>&      all = mGather.rows();
    std::vector<ALPaneList::Row> rows;
    rows.reserve(all.size());
    for (size_t i = 0; i < all.size(); ++i)
    {
        const Row&  row = all[i];
        std::string how;
        switch (row.file.empty() ? row.choicesHow : row.how)
        {
            case How::None: break;
            case How::Hint: how = row.named ? how_named : how_hint; break;
            case How::Record: how = row.own && !row.file.empty() ? how_own : how_record; break;
            case How::Name: how = how_name; break;
            case How::Picked: how = how_picked; break;
        }
        std::string world;
        if (!row.file.empty())
        {
            switch (row.world)
            {
                case World::Unasked:
                case World::Asking: world = world_asked; break;
                case World::Same: world = world_same; break;
                case World::Differs: world = world_other; break;
                case World::Unknown: world = world_dunno; break;
            }
        }
        const bool  placed = many_objects || row.ref.object != row.root;
        std::string file   = row.file;
        if (file.empty())
        {
            file = row.choices.empty() ? none_found : mServices->counted("LinkFileChoose", static_cast<S32>(row.choices.size()));
        }
        ALPaneList::Row one;
        one.key   = row.ref.id();
        one.value = static_cast<S32>(i);
        one.cells = { row.file.empty() ? cell("box", std::string()) : cell("box", LLSD(row.ticked), "checkbox"),
                      cell("script", placed ? mServices->words("ScriptInObject", { { "[NAME]", row.name }, { "[OBJECT]", row.place } }) : row.name),
                      cell("file", file), cell("how", how), cell("world", world) };
        // The script's choice, not the scripter's: marked, to be looked at.
        if (row.named)
        {
            one.cells[3].color = caution.get();
        }
        rows.push_back(std::move(one));
    }
    mList->setRows(std::move(rows));
    // Choosing and linking once what is found is in, and not while it is
    // being linked.
    const bool ready = mGather.stage() == Stage::Done;
    mChoose->setEnabled(ready && mList->getFirstSelected() != nullptr);
    mLink->setEnabled(ready && std::any_of(all.begin(), all.end(), [](const Row& row) { return row.ticked && !row.file.empty(); }));
}

void ALScriptLinkPane::readBoxes()
{
    // Taken first, then told: each tick fills the list again.
    std::vector<std::pair<size_t, bool>> ticks;
    for (const LLScrollListItem* item : mList->getAllData())
    {
        const size_t            index = static_cast<size_t>(item->getValue().asInteger());
        const LLScrollListCell* box   = item->getColumn(0);
        if (index < mGather.rows().size() && !mGather.rows()[index].file.empty() && box)
        {
            ticks.emplace_back(index, box->getValue().asBoolean());
        }
    }
    for (const auto& [index, ticked] : ticks)
    {
        mGather.tick(index, ticked);
    }
}

void ALScriptLinkPane::chooseFile()
{
    const LLScrollListItem* item  = mList->getFirstSelected();
    const size_t            index = item ? static_cast<size_t>(item->getValue().asInteger()) : mGather.rows().size();
    if (mGather.stage() != Stage::Done || index >= mGather.rows().size())
    {
        return;
    }
    const Row&              row    = mGather.rows()[index];
    const ALScriptRef       ref    = row.ref;
    const LLHandle<LLPanel> handle = getHandle();
    // Nothing found to choose among: any file on disk, at once.
    if (row.choices.empty())
    {
        mWindow->pickMasterFile([handle, ref](const std::string& path) {
            if (ALScriptLinkPane* pane = ALViewType::as<ALScriptLinkPane>(handle.get()))
            {
                pane->chosenFor(ref, "picked:" + path);
            }
        });
        return;
    }
    // Those found, each by its name with its path in full beside it, then
    // any file on disk.
    std::vector<ALQuickOpen::Candidate> candidates;
    for (const std::string& choice : row.choices)
    {
        ALQuickOpen::Candidate one;
        one.label  = fileNameOf(choice);
        one.detail = choice;
        one.value  = "file:" + choice;
        candidates.push_back(std::move(one));
    }
    ALQuickOpen::Candidate any;
    any.label  = mServices->words("LinkChoosePick");
    any.detail = mServices->words("LinkChoosePickDetail");
    any.value  = "pick";
    candidates.push_back(std::move(any));
    LLStringUtil::format_map_t args;
    args["[NAME]"] = row.name;
    mWindow->pick(std::move(candidates), mServices->words("LinkChoosePlaceholder"), mServices->words("LinkChooseTitle", args),
                  [handle, ref](const std::string& value) {
                      ALScriptLinkPane* pane = ALViewType::as<ALScriptLinkPane>(handle.get());
                      if (!pane)
                      {
                          return;
                      }
                      if (value == "pick")
                      {
                          pane->mWindow->pickMasterFile([handle, ref](const std::string& path) {
                              if (ALScriptLinkPane* again = ALViewType::as<ALScriptLinkPane>(handle.get()))
                              {
                                  again->chosenFor(ref, "picked:" + path);
                              }
                          });
                          return;
                      }
                      pane->chosenFor(ref, value);
                  },
                  nullptr);
}

void ALScriptLinkPane::chosenFor(const ALScriptRef& ref, const std::string& value)
{
    // By its item: the rows may have been gathered again since.
    const size_t index = rowOf(ref);
    if (index >= mGather.rows().size())
    {
        return;
    }
    const Row&  row    = mGather.rows()[index];
    const bool  picked = value.rfind("picked:", 0) == 0;
    const std::string file = value.substr(value.find(':') + 1);
    if (!mGather.choose(index, file, picked ? How::Picked : row.choicesHow == How::None ? How::Name : row.choicesHow))
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = row.name;
        args["[FILE]"] = fileNameOf(file);
        mServices->setStatus(mServices->words(row.lua ? "MasterNotSLuaFile" : "MasterNotLSLFile", args), true);
    }
}

void ALScriptLinkPane::linkTicked()
{
    const ALScriptLinkScripts::Linked linked = mGather.link(mSendDiffers->get());
    if (linked.ones.empty())
    {
        return;
    }
    // What was linked, each to its file in full, in Output; and how many
    // of them are on their way up.
    std::vector<std::string> each;
    S32                      sent = 0;
    for (const ALScriptLinkScripts::Linked::One& one : linked.ones)
    {
        const std::string name =
            one.place.empty() ? one.name : mServices->words("ScriptInObject", { { "[NAME]", one.name }, { "[OBJECT]", one.place } });
        each.push_back(mServices->words("LinkedOne", { { "[NAME]", name }, { "[FILE]", one.file } }));
        sent += one.sent ? 1 : 0;
    }
    std::string said = mServices->counted("LinkedScripts", static_cast<S32>(linked.ones.size()), { { "[LIST]", mServices->listed(each) } });
    if (sent > 0)
    {
        said = mServices->sentences(said, mServices->counted("LinkedSending", sent));
    }
    mServices->report(said);
    fill();
    mWindow->linkPaneDone(true);
}

void ALScriptLinkPane::cancel()
{
    mGather.cancel();
    fill();
    mWindow->linkPaneDone(false);
}
