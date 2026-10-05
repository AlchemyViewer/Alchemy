/**
 * @file alscriptstudiocomparewith.cpp
 * @brief A Script Studio window's Compare With: whatever a tab may be set beside, in one list.
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

#include "alscriptstudiocomparewith.h"

#include "alcodeeditor.h"
#include "alrecovery.h"
#include "alscriptstudiofileio.h"
#include "alscriptstudiohistory.h"
#include "alscriptstudioservices.h"
#include "lldir.h"

#include <algorithm>
#include <cstdlib>

namespace
{
    // A row's value: what it is, and after a colon which one.
    std::string valueOf(const char* what, const std::string& which = std::string())
    {
        return which.empty() ? std::string(what) : std::string(what) + ":" + which;
    }

    // Lines a text holds, as an editor shows them: a last line break ends
    // the last line rather than starting another.
    S32 linesIn(const std::string& text)
    {
        if (text.empty())
        {
            return 0;
        }
        const S32 breaks = static_cast<S32>(std::count(text.begin(), text.end(), '\n'));
        return text.back() == '\n' ? breaks : breaks + 1;
    }
}

ALScriptStudioCompareWith::ALScriptStudioCompareWith(ALScriptStudioServices& services, Window& window) : mServices(services), mWindow(window)
{
}

// static
bool ALScriptStudioCompareWith::canCompare(const Doc& doc)
{
    return doc.loaded && doc.editor;
}

// static
size_t ALScriptStudioCompareWith::savesKept(const Doc& doc)
{
    const std::string                    key     = ALScriptStudioHistory::keyOf(doc);
    const std::shared_ptr<ALSaveHistory> history = ALRecovery::history();
    return history && !key.empty() ? history->list(key).size() : 0;
}

std::vector<ALQuickOpen::Candidate> ALScriptStudioCompareWith::candidatesFor(const Doc& doc, Offer& offer) const
{
    std::vector<ALQuickOpen::Candidate> candidates;
    const auto add = [&candidates](std::string label, std::string detail, std::string value) {
        ALQuickOpen::Candidate one;
        one.label  = std::move(label);
        // What kind of thing it is answers to its words too: "tab",
        // "file", an object's name.
        one.also   = detail;
        one.detail = std::move(detail);
        one.value  = std::move(value);
        candidates.push_back(std::move(one));
    };

    // The text as saved, where it is not the text now; and as saved
    // elsewhere, where a save came up against one.
    if (doc.editor->isDirty() && doc.editor->undoJournal().savedText())
    {
        add(mServices.words("CompareSaved"), mServices.words("CompareWithSavedDetail"), valueOf("saved"));
    }
    if (doc.savedThere)
    {
        add(mServices.words("CompareSavedThere"), mServices.words("CompareWithSavedThereDetail"), valueOf("saved_there"));
    }
    // The other tabs, as they are now, in the strip's order.
    for (const Doc* other : mServices.openDocs())
    {
        if (other != &doc && canCompare(*other))
        {
            add(other->name, mServices.words(other->unsaved() ? "CompareWithTabUnsaved" : "CompareWithTab"), valueOf("tab", other->id));
        }
    }
    if (const std::optional<std::string> clipboard = mWindow.clipboardText(); clipboard && !clipboard->empty())
    {
        add(mServices.words("CompareClipboard"), mServices.counted("CompareWithLines", linesIn(*clipboard)), valueOf("clipboard"));
    }
    add(mServices.words("CompareWithFile"), mServices.words("CompareWithFileDetail"), valueOf("file"));
    if (const size_t kept = savesKept(doc); kept > 0)
    {
        add(mServices.words("CompareWithHistory"), mServices.counted("CompareWithSaves", static_cast<S32>(kept)), valueOf("history"));
    }
    // Files opened lately, but its own; then what is like it in the
    // objects in hand.
    for (const std::string& path : mWindow.recentFiles())
    {
        if (path != doc.file)
        {
            add(gDirUtilp->getBaseFileName(path), gDirUtilp->getDirName(path), valueOf("recent", std::to_string(offer.recent.size())));
            offer.recent.push_back(path);
        }
    }
    offer.items = mWindow.itemsLike(doc);
    for (size_t i = 0; i < offer.items.size(); ++i)
    {
        add(offer.items[i].name, offer.items[i].place, valueOf("item", std::to_string(i)));
    }
    return candidates;
}

void ALScriptStudioCompareWith::show(Doc& doc)
{
    if (!canCompare(doc))
    {
        return;
    }
    auto                                offer      = std::make_shared<Offer>();
    std::vector<ALQuickOpen::Candidate> candidates = candidatesFor(doc, *offer);
    LLStringUtil::format_map_t          args;
    args["[NAME]"]                  = doc.name;
    const std::weak_ptr<bool> alive = mAlive;
    const std::string         id    = doc.id;
    mWindow.pick(
        std::move(candidates), mServices.words("CompareWithPlaceholder"), mServices.words("CompareWithTitle", args),
        [this, alive, id, offer](const std::string& value) {
            Doc* found = alive.lock() ? mServices.findDoc(id) : nullptr;
            if (found && canCompare(*found))
            {
                chosen(*found, value, *offer);
            }
        },
        nullptr);
}

void ALScriptStudioCompareWith::chosen(Doc& doc, const std::string& value, const Offer& offer)
{
    const size_t      colon = value.find(':');
    const std::string what  = value.substr(0, colon);
    const std::string which = colon == std::string::npos ? std::string() : value.substr(colon + 1);
    const size_t      index = static_cast<size_t>(std::strtoul(which.c_str(), nullptr, 10));
    if (what == "saved")
    {
        // As saved by the time it is chosen, which a save since has moved.
        const std::optional<std::string> saved = doc.editor->undoJournal().savedText();
        if (!saved)
        {
            mServices.setStatus(mServices.words("CompareNothingSaved"), true);
            return;
        }
        mWindow.compareWithTab(doc, *saved, mServices.words("CompareSaved"), std::string(), {});
    }
    else if (what == "saved_there")
    {
        if (doc.savedThere)
        {
            mWindow.compareWithTab(doc, *doc.savedThere, mServices.words("CompareSavedThere"), std::string(), {});
        }
    }
    else if (what == "tab")
    {
        // Its text as it is now, which the comparison keeps: only the
        // tab's own is followed.
        const Doc* other = mServices.findDoc(which);
        if (!other || other == &doc || !canCompare(*other))
        {
            return;
        }
        std::string title = other->name;
        if (other->unsaved())
        {
            title = mServices.words("CompareUnsaved", { { "[TITLE]", title } });
        }
        mWindow.compareWithTab(doc, other->editor->wholeText(), title, std::string(), {});
    }
    else if (what == "clipboard")
    {
        // As it is now, which a copy since may have changed.
        const std::optional<std::string> clipboard = mWindow.clipboardText();
        if (!clipboard || clipboard->empty())
        {
            mServices.setStatus(mServices.words("CompareWithClipboardEmpty"), true);
            return;
        }
        mWindow.compareWithTab(doc, *clipboard, mServices.words("CompareClipboard"), std::string(), {});
    }
    else if (what == "file")
    {
        const std::weak_ptr<bool> alive = mAlive;
        mWindow.pickFilesToOpen(false, [this, alive, id = doc.id](const std::vector<std::string>& files) {
            Doc* found = alive.lock() && !files.empty() ? mServices.findDoc(id) : nullptr;
            if (found && canCompare(*found))
            {
                compareWithFile(*found, files.front());
            }
        });
    }
    else if (what == "history")
    {
        mWindow.offerHistory(doc);
    }
    else if (what == "recent" && index < offer.recent.size())
    {
        compareWithFile(doc, offer.recent[index]);
    }
    else if (what == "item" && index < offer.items.size())
    {
        const Item& item = offer.items[index];
        mWindow.compareWithItem(doc, item, item.place.empty() ? item.name : item.place + " \xE2\x96\xB8 " + item.name);
    }
}

void ALScriptStudioCompareWith::compareWithFile(Doc& doc, const std::string& path)
{
    std::string text;
    if (!ALScriptFileIO::readWholeFile(path, text))
    {
        LLStringUtil::format_map_t args;
        args["[FILE]"] = path;
        mServices.setStatus(mServices.words(ALScriptFileIO::fileTooLarge(path) ? "FileTooLarge" : "LoadFromFileFailed", args), true);
        return;
    }
    mWindow.compareWithTab(doc, text, gDirUtilp->getBaseFileName(path), std::string(), {});
}
