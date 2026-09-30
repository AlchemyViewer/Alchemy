/**
 * @file alscriptstudiohistory.cpp
 * @brief A Script Studio window's way back to what its items were saved as.
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

#include "alscriptstudiohistory.h"

#include "alcodeeditor.h"
#include "alrecovery.h"
#include "alscriptstudioservices.h"

#include <cstdlib>

ALScriptStudioHistory::ALScriptStudioHistory(ALScriptStudioServices& services, Window& window) : mServices(services), mWindow(window)
{
}

// static
std::string ALScriptStudioHistory::keyOf(const Doc& doc)
{
    // As the workspace keeps them: by the item, in its object or not.
    return doc.file.empty() && doc.ref.item.notNull() ? ALRecoveryStore::keyOf(doc.ref.object, doc.ref.item, std::string()) : std::string();
}

void ALScriptStudioHistory::show(Doc& doc)
{
    const std::string              key     = keyOf(doc);
    std::shared_ptr<ALSaveHistory> history = ALRecovery::history();
    std::vector<ALSavedText>       saves;
    if (history && !key.empty())
    {
        saves = history->list(key);
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (saves.empty())
    {
        mServices.setStatus(mServices.words("HistoryNone", args));
        return;
    }
    std::vector<ALQuickOpen::Candidate> candidates;
    for (size_t i = 0; i < saves.size(); ++i)
    {
        const ALSavedText&       save = saves[i];
        std::vector<std::string> said = { mServices.counted("HistoryBytes", static_cast<S32>(save.bytes)) };
        // Against the save before it, which is the next one listed.
        if (i + 1 < saves.size())
        {
            const S64 moved = static_cast<S64>(save.bytes) - static_cast<S64>(saves[i + 1].bytes);
            said.push_back(moved > 0   ? mServices.counted("HistoryLonger", static_cast<S32>(moved))
                           : moved < 0 ? mServices.counted("HistoryShorter", static_cast<S32>(-moved))
                                       : mServices.words("HistorySameLength"));
        }
        if (save.asset.notNull() && save.asset == doc.assetId)
        {
            said.push_back(mServices.words("HistoryCurrent"));
        }
        ALQuickOpen::Candidate one;
        one.label  = ALRecoveryEntry::sayWhen(save.when);
        one.detail = mServices.listed(said);
        one.value  = std::to_string(i);
        candidates.push_back(std::move(one));
    }
    const std::weak_ptr<bool> alive = mAlive;
    const std::string         id    = doc.id;
    mWindow.pick(
        std::move(candidates), mServices.words("HistoryPlaceholder"), mServices.words("HistoryTitle", args),
        [this, alive, id, saves](const std::string& value) {
            const size_t index = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10));
            Doc*         doc   = alive.lock() ? mServices.findDoc(id) : nullptr;
            if (doc && index < saves.size())
            {
                compare(*doc, saves[index]);
            }
        },
        nullptr);
}

bool ALScriptStudioHistory::compare(Doc& doc, ALSavedText saved)
{
    std::shared_ptr<ALSaveHistory> history = ALRecovery::history();
    LLStringUtil::format_map_t     args;
    args["[NAME]"] = doc.name;
    args["[WHEN]"] = ALRecoveryEntry::sayWhen(saved.when);
    if (!saved.whole && (!history || !history->load(saved)))
    {
        mServices.setStatus(mServices.words("HistoryUnreadable", args), true);
        return false;
    }
    const std::string theirs = mServices.words("HistorySavedAt", args);
    const std::string text   = saved.text;
    doc.historyShown         = std::move(saved);
    if (doc.loaded)
    {
        mWindow.compare(doc, text, doc.editor->wholeText(), theirs, mServices.words("CompareNow"));
    }
    else
    {
        // Set beside the tab's text once there is one, as the Explorer's
        // Compare is.
        doc.pendingCompare = Doc::PendingCompare{ text, theirs, mServices.words("CompareNow") };
    }
    mWindow.refreshNotice();
    return true;
}
