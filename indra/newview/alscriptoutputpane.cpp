/**
 * @file alscriptoutputpane.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alscriptoutputpane.h"

#include "alscriptmessages.h"
#include "alscriptstudiodoc.h"
#include "alscriptstudioservices.h"
#include "alstringmatch.h"
#include "llbutton.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llpanel.h"
#include "lluicolortable.h"

#include <algorithm>
#include <ctime>

namespace
{
    // The time of day something was said, from seconds since the epoch.
    std::string clockOf(F64 seconds_since_epoch)
    {
        const time_t when = static_cast<time_t>(seconds_since_epoch);
        struct tm    local;
#if LL_WINDOWS
        localtime_s(&local, &when);
#else
        localtime_r(&when, &local);
#endif
        char buffer[16];
        strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
        return buffer;
    }
}

ALScriptOutputPane::ALScriptOutputPane(LLPanel& tab, ALScriptStudioServices& services, Window& window) : mServices(services), mWindow(window)
{
    mView  = tab.getChild<ALOutputView>("output");
    mWhose = tab.getChild<LLComboBox>("output_filter");
    mKind  = tab.getChild<LLComboBox>("output_kind");
    mFind  = tab.getChild<LLFilterEditor>("output_find");
    mView->setPlaceholder(mServices.words("NoOutput"));
    // What the studio did has its own lane in the log, so that a busy
    // debug channel does not push it out.
    mView->setCapacity(200, 1);
    offerObject(LLUUID::null, std::string());
    for (LLUICtrl* filter : { static_cast<LLUICtrl*>(mWhose), static_cast<LLUICtrl*>(mKind), static_cast<LLUICtrl*>(mFind) })
    {
        filter->setCommitCallback([this](LLUICtrl*, const LLSD&) { this->filter(); });
    }
    mKind->selectFirstItem();
    mView->onEntryChosen([this](const ALOutputView::Entry& entry) { choose(entry); });
    tab.getChild<LLButton>("output_clear")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        // What was said goes; whose words are listened to stays.
        mView->clearEntries();
        mUnread = false;
        mWindow.outputUnreadChanged();
    });
    tab.getChild<LLButton>("output_copy")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        // What the pane shows through its filters, as it reads, on the
        // clipboard; nothing shown leaves the clipboard as it was.
        std::string all;
        S32         lines = 0;
        for (const ALOutputView::Entry& entry : mView->entries())
        {
            if (!mView->shows(entry))
            {
                continue;
            }
            all += ALOutputView::format(entry);
            all += '\n';
            ++lines;
        }
        if (lines == 0)
        {
            mServices.setStatus(mServices.words("OutputNothingToCopy"));
            return;
        }
        LLClipboard::instance().copyToClipboard(all, 0, static_cast<S32>(all.size()));
        mServices.setStatus(mServices.counted("OutputCopied", lines));
    });
}

ALScriptOutputPane::Place ALScriptOutputPane::heard(const ALScriptWorkspace::RuntimeEvent& event)
{
    static const LLUIColor runtime_color = LLUIColorTable::instance().getColor("CodeMarkRuntime", LLColor4::magenta);
    static const LLUIColor owner_color   = LLUIColorTable::instance().getColor("ObjectChatColor", LLColor4::white);

    // The object, offered in the filter the first time it speaks.
    if (event.root.notNull())
    {
        offerObject(event.root, event.objectName);
    }

    // The script, where it is open here: what runs is its expansion,
    // where the preprocessor ran, and a line the run names is the
    // expansion's -- back to the source's, or an include's.
    const ALScriptStudioDoc* open     = event.item.notNull() ? mServices.findDoc(ALScriptRef(event.prim, event.item)) : nullptr;
    const auto               where_of = [&](S32 line, S32 column) {
        Place where;
        where.line   = line;
        where.column = column;
        if (line < 0 || !open)
        {
            return where;
        }
        if (const ALSourceMap* map = open->runningMap())
        {
            const ALSourceMap::Loc loc = map->toSource(line, llmax(0, column));
            if (loc.found())
            {
                where.line   = loc.line;
                where.column = column >= 0 ? loc.column : -1;
                if (loc.file > 0)
                {
                    where.file     = map->files()[loc.file].path;
                    where.fileName = map->files()[loc.file].name;
                }
            }
        }
        return where;
    };
    const auto link_value = [&](const Place& where) {
        LLSD value;
        value["prim"]     = event.prim;
        value["item"]     = event.item;
        value["name"]     = event.scriptName;
        value["line"]     = where.line;
        value["column"]   = where.column;
        value["file"]     = where.file;
        value["fileName"] = where.fileName;
        return value;
    };
    const Place at = where_of(event.line, event.column);

    // One line of the log, or more where the script said more; the
    // script's name a link to it, at the line of a run-time error.
    ALOutputView::Entry entry;
    entry.time   = clockOf(event.time);
    entry.source = event.scriptName.empty() ? event.objectName : event.objectName + " / " + event.scriptName;
    entry.kind   = event.isError ? mServices.words("KindError") : event.channel == ALScriptWorkspace::RuntimeEvent::Channel::OwnerSay ? mServices.words("KindOwnerSay") : std::string();
    entry.text   = event.isError && !event.error.empty() ? event.error : event.message;
    while (!entry.text.empty() && (entry.text.back() == '\n' || entry.text.back() == '\r'))
    {
        entry.text.pop_back();
    }
    // What the filters go by: whose it is, and what kind of thing.
    entry.key["root"] = event.root;
    entry.key["prim"] = event.prim;
    entry.key["item"] = event.item;
    entry.key["kind"] = event.isError ? "error" : event.channel == ALScriptWorkspace::RuntimeEvent::Channel::OwnerSay ? "owner" : "debug";
    entry.key["mine"] = event.channel == ALScriptWorkspace::RuntimeEvent::Channel::OwnerSay || (event.root.notNull() && mWindow.ownsObject(event.root));
    if (event.isError)
    {
        entry.color = runtime_color.get();
        if (at.line >= 0)
        {
            LLStringUtil::format_map_t line;
            line["[LINE]"] = std::to_string(at.line + 1);
            entry.text += " " + mServices.words("OutputAtLine", line);
        }
        // The stack under it, as the VM said it: each frame of the
        // script's own chunk -- the one the error's line names, which the
        // simulator loads a script under as `lua_script`, or `lsl_script`
        // for LSL on Luau -- a link to its line. The error's own line is
        // what the entry says first.
        std::string chunk;
        for (const std::string& line : event.stack)
        {
            if (!event.error.empty() && line.size() >= event.error.size() &&
                line.compare(line.size() - event.error.size(), event.error.size(), event.error) == 0)
            {
                const size_t colon = line.find(':');
                chunk              = colon == std::string::npos ? std::string() : line.substr(0, colon);
                LLStringUtil::trim(chunk);
                continue;
            }
            if (line.empty())
            {
                continue;
            }
            entry.text += "\n" + line;
            ALScriptMessages::Frame frame;
            if (event.item.notNull() && ALScriptMessages::readStackFrame(line, frame) &&
                (frame.chunk == chunk || frame.chunk == event.scriptName || frame.chunk == "lua_script" || frame.chunk == "lsl_script"))
            {
                const Place               there = where_of(frame.line, -1);
                ALOutputView::Entry::Link link;
                link.line = static_cast<S32>(std::count(entry.text.begin(), entry.text.end(), '\n'));
                LLStringUtil::format_map_t args;
                args["[NAME]"] = there.file.empty() ? event.scriptName : there.fileName;
                args["[LINE]"] = std::to_string(there.line + 1);
                link.tooltip   = mServices.words("OutputOpenAtLine", args);
                link.value     = link_value(there);
                entry.links.push_back(std::move(link));
            }
        }
    }
    else if (event.channel == ALScriptWorkspace::RuntimeEvent::Channel::OwnerSay)
    {
        // What the owner was told, in the colour chat shows an object's
        // words in; the debug channel's in the plain ink.
        entry.color = owner_color.get();
    }
    if (event.item.notNull())
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = event.scriptName;
        args["[LINE]"] = llformat("%d", at.line + 1);
        entry.link     = true;
        entry.tooltip  = mServices.words(event.isError && at.line >= 0 ? "OutputOpenAtLine" : "OutputOpen", args);
        entry.value    = link_value(at);
    }
    mView->append(std::move(entry));

    // An error the Output tab is not showing, said on its title.
    if (event.isError)
    {
        markUnread();
    }
    return at;
}

void ALScriptOutputPane::said(const std::string& text, bool failure, const ALScriptStudioDoc* doc, const std::vector<std::string>& actions)
{
    static const LLUIColor alarm = LLUIColorTable::instance().getColor("LtOrange", LLColor4::yellow);
    ALOutputView::Entry entry;
    entry.time        = clockOf(LLDate::now().secondsSinceEpoch());
    entry.source      = mServices.words("OutputSourceStudio");
    entry.text        = text;
    entry.key["kind"] = "studio";
    entry.lane        = 1;
    if (failure)
    {
        entry.color = alarm.get();
        // Said while the Output tab is not in sight: its title says so.
        markUnread();
    }
    // The script's name where the words say it, a link to its tab.
    const size_t at = doc && !doc->name.empty() ? text.find(doc->name) : std::string::npos;
    if (at != std::string::npos)
    {
        const size_t              line_start = text.rfind('\n', at);
        ALOutputView::Entry::Link link;
        link.line  = static_cast<S32>(std::count(text.begin(), text.begin() + at, '\n'));
        link.begin = static_cast<S32>(line_start == std::string::npos ? at : at - line_start - 1);
        link.end   = link.begin + static_cast<S32>(doc->name.size());
        LLStringUtil::format_map_t args;
        args["[NAME]"]      = doc->name;
        link.tooltip        = mServices.words("OutputStudioShow", args);
        link.value["doc"]   = doc->id;
        link.value["issue"] = failure;
        entry.links.push_back(std::move(link));
    }
    // What can be done about it, done from here, each a link after the
    // words: the save asked again, tried again, a copy, a file.
    for (const std::string& action : doc ? actions : std::vector<std::string>())
    {
        const std::string key   = action == "save_anyway"     ? "SaveAnyway"
                                  : action == "retry"         ? "ActionRetry"
                                  : action == "copy"          ? "ActionCopy"
                                  : action == "take_external" ? "ActionTakeExternal"
                                  : action == "keep_here"     ? "ActionKeepHere"
                                                              : "ActionExport";
        const std::string label = mServices.words(key);
        const size_t      last  = entry.text.rfind('\n');
        const S32         line  = static_cast<S32>(std::count(entry.text.begin(), entry.text.end(), '\n'));
        entry.text += "   ";
        ALOutputView::Entry::Link link;
        link.line  = line;
        link.begin = static_cast<S32>(entry.text.size() - (last == std::string::npos ? 0 : last + 1));
        entry.text += label;
        link.end   = link.begin + static_cast<S32>(label.size());
        LLStringUtil::format_map_t args;
        args["[NAME]"]       = doc->name;
        link.tooltip         = mServices.words(key + "Tip", args);
        link.value["action"] = action;
        link.value["doc"]    = doc->id;
        entry.links.push_back(std::move(link));
    }
    mView->append(std::move(entry));
}

void ALScriptOutputPane::markUnread()
{
    if (!mWindow.outputInSight())
    {
        mUnread = true;
        mWindow.outputUnreadChanged();
    }
}

void ALScriptOutputPane::pump()
{
    // What the Output tab had not shown, it has once it is looked at.
    if (mUnread && mWindow.outputInSight())
    {
        mUnread = false;
        mWindow.outputUnreadChanged();
    }
}

void ALScriptOutputPane::openChanged()
{
    // Output listening to the scripts open here: which those are has
    // changed, and so has what it shows of what was said before.
    if (mWhose->getValue().asString() == "open")
    {
        filter();
    }
}

std::string ALScriptOutputPane::kind() const
{
    return mKind->getValue().asString();
}

void ALScriptOutputPane::showKind(const std::string& kind)
{
    mKind->selectByValue(LLSD(kind));
    filter();
}

void ALScriptOutputPane::offerObject(const LLUUID& root, const std::string& name)
{
    // Past a few dozen the list is no use to anybody, and a busy region
    // would fill it for the session: the one least lately heard from
    // gives way, unless it is the one chosen.
    constexpr size_t FILTER_OBJECTS = 40;
    const std::string chosen        = mWhose->getValue().asString();
    bool              changed       = root.isNull();
    if (root.notNull())
    {
        const auto known = std::find_if(mObjects.begin(), mObjects.end(), [&root](const auto& one) { return one.first == root; });
        if (known != mObjects.end())
        {
            // Heard from again: the last to give way.
            std::pair<LLUUID, std::string> again = *known;
            changed                              = again.second != name;
            again.second                         = name;
            mObjects.erase(known);
            mObjects.push_back(std::move(again));
            if (!changed)
            {
                return;
            }
        }
        else
        {
            if (mObjects.size() >= FILTER_OBJECTS)
            {
                const auto oldest = std::find_if(mObjects.begin(), mObjects.end(),
                                                 [&chosen](const auto& one) { return one.first.asString() != chosen; });
                if (oldest != mObjects.end())
                {
                    mObjects.erase(oldest);
                }
            }
            mObjects.emplace_back(root, name);
            changed = true;
        }
    }
    if (!changed)
    {
        return;
    }
    // The list again, by name, the choice kept.
    std::vector<std::pair<LLUUID, std::string>> ordered = mObjects;
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return LLStringUtil::compareDict(a.second, b.second) < 0; });
    mWhose->clearRows();
    mWhose->add(mServices.words("OutputAllObjects"), LLSD(""));
    mWhose->add(mServices.words("OutputOpenScripts"), LLSD("open"));
    mWhose->add(mServices.words("OutputMyObjects"), LLSD("mine"));
    mWhose->addSeparator();
    for (const auto& [id, label] : ordered)
    {
        mWhose->add(label.empty() ? mServices.words("ObjectUnnamed") : label, LLSD(id.asString()));
    }
    if (!mWhose->selectByValue(LLSD(chosen)))
    {
        mWhose->selectFirstItem();
    }
}

void ALScriptOutputPane::filter()
{
    const std::string whose = mWhose->getValue().asString();
    const std::string kind  = mKind->getValue().asString();
    std::string       words = mFind->getText();
    LLStringUtil::trim(words);
    if (whose.empty() && kind.empty() && words.empty())
    {
        mView->setFilter(nullptr);
        return;
    }
    mView->setFilter([this, whose, kind, words](const ALOutputView::Entry& entry) {
        const std::string said = entry.key["kind"].asString();
        if (!kind.empty() && said != kind)
        {
            return false;
        }
        // The studio's own words are about no object: shown whoever's
        // are, but for one object's alone.
        if (whose == "open")
        {
            if (said != "studio" && !mServices.findDoc(ALScriptRef(entry.key["prim"].asUUID(), entry.key["item"].asUUID())))
            {
                return false;
            }
        }
        else if (whose == "mine")
        {
            if (said != "studio" && !entry.key["mine"].asBoolean())
            {
                return false;
            }
        }
        else if (!whose.empty() && entry.key["root"].asString() != whose)
        {
            return false;
        }
        return words.empty() || ALStringMatch::containsNoCase(entry.text, words) || ALStringMatch::containsNoCase(entry.source, words);
    });
}

void ALScriptOutputPane::choose(const ALOutputView::Entry& entry)
{
    // What the words said could be done, done.
    if (entry.value.has("action"))
    {
        if (ALScriptStudioDoc* doc = mServices.findDoc(entry.value["doc"].asString()))
        {
            mWindow.outputAction(*doc, entry.value["action"].asString());
        }
        return;
    }
    // A line of the studio's own: the script it names, brought forward.
    if (entry.value.has("doc"))
    {
        if (ALScriptStudioDoc* doc = mServices.findDoc(entry.value["doc"].asString()))
        {
            mWindow.outputShowDoc(*doc, entry.value["issue"].asBoolean());
        }
        return;
    }
    if (entry.value["item"].asUUID().isNull())
    {
        return;
    }
    const S32 line   = entry.value["line"].asInteger();
    const S32 column = entry.value["column"].asInteger();
    // A frame in an include: the include, where it can be opened.
    if (!entry.value["file"].asString().empty())
    {
        mWindow.outputGoToInclude(entry.value["file"].asString(), entry.value["fileName"].asString(), line, column);
        return;
    }
    mWindow.outputGoTo(ALScriptRef(entry.value["prim"].asUUID(), entry.value["item"].asUUID()), entry.value["name"].asString(), line, column);
}
