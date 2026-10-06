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
#include "aldiffview.h"
#include "alrecovery.h"
#include "alscriptstudioservices.h"

#include <algorithm>
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

// static
std::vector<ALQuickOpen::Candidate> ALScriptStudioHistory::candidatesOf(const std::vector<ALSavedText>& saves, const LLUUID& current,
                                                                       const words_t&                                           words,
                                                                       const std::function<std::string(const std::vector<std::string>&)>& listed)
{
    std::vector<ALQuickOpen::Candidate> candidates;
    for (size_t i = 0; i < saves.size(); ++i)
    {
        const ALSavedText&       save = saves[i];
        std::vector<std::string> said = { words("HistoryBytes", static_cast<S32>(save.bytes)) };
        // Against the save before it, which is the next one listed.
        if (i + 1 < saves.size())
        {
            const S64 moved = static_cast<S64>(save.bytes) - static_cast<S64>(saves[i + 1].bytes);
            said.push_back(moved > 0   ? words("HistoryLonger", static_cast<S32>(moved))
                           : moved < 0 ? words("HistoryShorter", static_cast<S32>(-moved))
                                       : words("HistorySameLength", std::nullopt));
        }
        if (save.asset.notNull() && save.asset == current)
        {
            said.push_back(words("HistoryCurrent", std::nullopt));
        }
        ALQuickOpen::Candidate one;
        one.label  = ALRecoveryEntry::sayWhen(save.when);
        one.detail = listed(said);
        one.value  = std::to_string(i);
        candidates.push_back(std::move(one));
    }
    return candidates;
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
    std::vector<ALQuickOpen::Candidate> candidates = candidatesOf(
        saves, doc.assetId,
        [this](const char* name, std::optional<S32> count) { return count ? mServices.counted(name, *count) : mServices.words(name); },
        [this](const std::vector<std::string>& items) { return mServices.listed(items); });
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

// static
bool ALScriptStudioHistory::read(ALSavedText& saved)
{
    const std::shared_ptr<ALSaveHistory> history = ALRecovery::history();
    return saved.whole || (history && history->load(saved));
}

bool ALScriptStudioHistory::compare(Doc& doc, ALSavedText saved)
{
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    args["[WHEN]"] = ALRecoveryEntry::sayWhen(saved.when);
    if (!read(saved))
    {
        mServices.setStatus(mServices.words("HistoryUnreadable", args), true);
        return false;
    }
    const std::string theirs = mServices.words("HistorySavedAt", args);
    const std::string text   = saved.text;
    doc.historyShown         = std::move(saved);
    if (doc.loaded)
    {
        mWindow.compareWithTab(doc, text, theirs, mServices.words("CompareNow"), {});
        versions(doc);
    }
    else
    {
        // Set beside the tab's text once there is one, as the Explorer's
        // Compare is.
        const std::weak_ptr<bool> alive = mAlive;
        doc.pendingCompare              = Doc::PendingCompare{ text, theirs, mServices.words("CompareNow") };
        doc.pendingCompare->shown       = [this, alive](Doc& shown) {
            if (alive.lock())
            {
                versions(shown);
            }
        };
    }
    mWindow.refreshNotice();
    return true;
}

// static
void ALScriptStudioHistory::offerVersions(ALDiffView& view, const std::string& key, const std::string& shown,
                                          std::function<void(ALSavedText saved)> stepped, std::function<void(const ALSavedText& saved)> unreadable)
{
    const std::shared_ptr<ALSaveHistory> history = ALRecovery::history();
    if (!history || key.empty())
    {
        return;
    }
    // Listed newest first; the slider has them oldest first.
    struct Saves
    {
        std::vector<ALSavedText> saves;
        S32                      current = 0;
    };
    auto listed = std::make_shared<Saves>();
    listed->saves = history->list(key);
    std::reverse(listed->saves.begin(), listed->saves.end());
    const auto at = std::find_if(listed->saves.begin(), listed->saves.end(), [&shown](const ALSavedText& save) { return save.path == shown; });
    if (at == listed->saves.end())
    {
        return;
    }
    listed->current = static_cast<S32>(at - listed->saves.begin());
    // The view holds this, and is there whenever it is called.
    ALDiffView* const shows = &view;
    view.setVersions(static_cast<S32>(listed->saves.size()), listed->current, [listed, shows, stepped, unreadable](S32 version) {
        if (version < 0 || version >= static_cast<S32>(listed->saves.size()))
        {
            return;
        }
        // Read once and kept in the list: a slider dragged back and forth
        // passes the same saves again.
        ALSavedText& save = listed->saves[static_cast<size_t>(version)];
        if (!read(save))
        {
            unreadable(save);
            shows->showVersion(listed->current);
            return;
        }
        listed->current = version;
        stepped(save);
    });
}

// static
void ALScriptStudioHistory::letGo(Doc& doc)
{
    doc.historyShown.reset();
    if (doc.compareView)
    {
        doc.compareView->setVersions(0, 0, nullptr);
    }
}

void ALScriptStudioHistory::versions(Doc& doc)
{
    if (!doc.compareView || !doc.historyShown)
    {
        return;
    }
    const std::weak_ptr<bool> alive = mAlive;
    const std::string         id    = doc.id;
    offerVersions(
        *doc.compareView, keyOf(doc), doc.historyShown->path,
        [this, alive, id](ALSavedText saved) {
            if (alive.lock())
            {
                step(id, std::move(saved));
            }
        },
        [this, alive, id](const ALSavedText& saved) {
            const Doc* doc = alive.lock() ? mServices.findDoc(id) : nullptr;
            if (doc)
            {
                LLStringUtil::format_map_t args;
                args["[NAME]"] = doc->name;
                args["[WHEN]"] = ALRecoveryEntry::sayWhen(saved.when);
                mServices.setStatus(mServices.words("HistoryUnreadable", args), true);
            }
        });
}

void ALScriptStudioHistory::step(const std::string& id, ALSavedText saved)
{
    // Still the comparison of a save, following the tab.
    Doc* doc = mServices.findDoc(id);
    if (!doc || !doc->compareView || !doc->compareTitles || !doc->historyShown)
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"]             = doc->name;
    args["[WHEN]"]             = ALRecoveryEntry::sayWhen(saved.when);
    const std::string text     = saved.text;
    doc->historyShown          = std::move(saved);
    doc->compareTitles->theirs = mServices.words("HistorySavedAt", args);
    doc->compareView->setLeftText(text);
    mWindow.retitleCompare(*doc);
    mWindow.refreshNotice();
}
