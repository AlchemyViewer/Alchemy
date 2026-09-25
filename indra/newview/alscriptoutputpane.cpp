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

#include "alfloaterscriptstudio.h"

#include "alcodeeditor.h"
#include "aloutputview.h"
#include "alscriptmessages.h"
#include "alstringmatch.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "lltabcontainer.h"
#include "lluicolortable.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"

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

    // A message as one row reads it.
    std::string oneLine(std::string text)
    {
        for (char& c : text)
        {
            if (c == '\n' || c == '\r' || c == '\t')
            {
                c = ' ';
            }
        }
        return text;
    }
}

void ALFloaterScriptStudio::report(const std::string& text, bool failure, const Doc* doc, const std::vector<std::string>& actions)
{
    static const LLUIColor alarm = LLUIColorTable::instance().getColor("LtOrange", LLColor4::yellow);
    setStatus(text, failure);
    ALOutputView::Entry entry;
    entry.time        = clockOf(LLDate::now().secondsSinceEpoch());
    entry.source      = getString("OutputSourceStudio");
    entry.text        = text;
    entry.key["kind"] = "studio";
    entry.lane        = 1;
    if (failure)
    {
        entry.color = alarm.get();
        // Said while the Output tab is not in sight: its title says so.
        if (mFolds.collapsed("bottom") || !mBottomTabs->getCurrentPanel() || mBottomTabs->getCurrentPanel()->getName() != "output_tab")
        {
            mOutputUnread = true;
            refreshBottomTabs();
        }
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
        link.tooltip        = getString("OutputStudioShow", args);
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
        const std::string label = getString(key);
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
        link.tooltip         = getString(key + "Tip", args);
        link.value["action"] = action;
        link.value["doc"]    = doc->id;
        entry.links.push_back(std::move(link));
    }
    mOutput->append(std::move(entry));
}

// --- what scripts say ---------------------------------------------------------------

void ALFloaterScriptStudio::runtimeEvent(const ALScriptWorkspace::RuntimeEvent& event)
{
    static const LLUIColor runtime_color = LLUIColorTable::instance().getColor("CodeMarkRuntime", LLColor4::magenta);
    static const LLUIColor owner_color   = LLUIColorTable::instance().getColor("ObjectChatColor", LLColor4::white);

    // The object, offered in the filter the first time it speaks.
    if (event.root.notNull())
    {
        offerOutputObject(event.root, event.objectName);
    }

    // The script, where it is open here: what runs is its expansion,
    // where the preprocessor ran, and a line the run names is the
    // expansion's -- back to the source's, or an include's.
    const size_t open = event.item.notNull() ? indexOf(ALScriptRef(event.prim, event.item)) : NONE;
    struct Where
    {
        S32         line   = -1;
        S32         column = -1;
        std::string file;
        std::string fileName;
    };
    const auto where_of = [&](S32 line, S32 column) {
        Where where;
        where.line   = line;
        where.column = column;
        if (line < 0 || open == NONE)
        {
            return where;
        }
        if (const ALSourceMap* map = runningMap(*mDocs[open]))
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
    const auto link_value = [&](const Where& where) {
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
    const Where at = where_of(event.line, event.column);

    // One line of the log, or more where the script said more; the
    // script's name a link to it, at the line of a run-time error.
    ALOutputView::Entry entry;
    entry.time   = clockOf(event.time);
    entry.source = event.scriptName.empty() ? event.objectName : event.objectName + " / " + event.scriptName;
    entry.kind   = event.isError ? getString("KindError") : event.channel == ALScriptWorkspace::RuntimeEvent::Channel::OwnerSay ? getString("KindOwnerSay") : std::string();
    entry.text   = event.isError && !event.error.empty() ? event.error : event.message;
    while (!entry.text.empty() && (entry.text.back() == '\n' || entry.text.back() == '\r'))
    {
        entry.text.pop_back();
    }
    // What the filters go by: whose it is, and what kind of thing.
    LLViewerObject* root = event.root.notNull() ? gObjectList.findObject(event.root) : nullptr;
    entry.key["root"] = event.root;
    entry.key["prim"] = event.prim;
    entry.key["item"] = event.item;
    entry.key["kind"] = event.isError ? "error" : event.channel == ALScriptWorkspace::RuntimeEvent::Channel::OwnerSay ? "owner" : "debug";
    entry.key["mine"] = event.channel == ALScriptWorkspace::RuntimeEvent::Channel::OwnerSay || (root && root->permYouOwner());
    if (event.isError)
    {
        entry.color = runtime_color.get();
        if (at.line >= 0)
        {
            LLStringUtil::format_map_t line;
            line["[LINE]"] = std::to_string(at.line + 1);
            entry.text += " " + getString("OutputAtLine", line);
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
                const Where               there = where_of(frame.line, -1);
                ALOutputView::Entry::Link link;
                link.line = static_cast<S32>(std::count(entry.text.begin(), entry.text.end(), '\n'));
                LLStringUtil::format_map_t args;
                args["[NAME]"] = there.file.empty() ? event.scriptName : there.fileName;
                args["[LINE]"] = std::to_string(there.line + 1);
                link.tooltip   = getString("OutputOpenAtLine", args);
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
        entry.tooltip  = getString(event.isError && at.line >= 0 ? "OutputOpenAtLine" : "OutputOpen", args);
        entry.value    = link_value(at);
    }
    mOutput->append(std::move(entry));

    // An error the Output tab is not showing, said on its title.
    if (event.isError && (mFolds.collapsed("bottom") || !mBottomTabs->getCurrentPanel() || mBottomTabs->getCurrentPanel()->getName() != "output_tab"))
    {
        mOutputUnread = true;
        refreshBottomTabs();
    }

    // A run-time error in a script that is open marks its line: said
    // again, as a script failing in a timer says it every tick, it is
    // the same problem, counted.
    if (event.isError && open != NONE)
    {
        Doc&                doc = *mDocs[open];
        Doc::RuntimeProblem problem;
        problem.line    = at.line;
        problem.column  = at.column;
        problem.file    = at.file;
        problem.message = event.error.empty() ? oneLine(event.message) : event.error;
        const auto same = std::find_if(doc.runtime.begin(), doc.runtime.end(), [&problem](const Doc::RuntimeProblem& one) {
            return one.line == problem.line && one.column == problem.column && one.file == problem.file && one.message == problem.message;
        });
        if (same != doc.runtime.end())
        {
            ++same->count;
        }
        else
        {
            doc.runtime.push_back(std::move(problem));
            // A script failing many ways at once is failing: the oldest go
            // past a few dozen.
            constexpr size_t RUNTIME_PROBLEMS = 50;
            if (doc.runtime.size() > RUNTIME_PROBLEMS)
            {
                doc.runtime.erase(doc.runtime.begin());
            }
        }
        refreshProblems(doc);
    }
}

void ALFloaterScriptStudio::offerOutputObject(const LLUUID& root, const std::string& name)
{
    // Past a few dozen the list is no use to anybody, and a busy region
    // would fill it for the session: the one least lately heard from
    // gives way, unless it is the one chosen.
    constexpr size_t FILTER_OBJECTS = 40;
    const std::string chosen        = mOutputFilter->getValue().asString();
    bool              changed       = root.isNull();
    if (root.notNull())
    {
        const auto known = std::find_if(mOutputObjects.begin(), mOutputObjects.end(), [&root](const auto& one) { return one.first == root; });
        if (known != mOutputObjects.end())
        {
            // Heard from again: the last to give way.
            std::pair<LLUUID, std::string> again = *known;
            changed                              = again.second != name;
            again.second                         = name;
            mOutputObjects.erase(known);
            mOutputObjects.push_back(std::move(again));
            if (!changed)
            {
                return;
            }
        }
        else
        {
            if (mOutputObjects.size() >= FILTER_OBJECTS)
            {
                const auto oldest = std::find_if(mOutputObjects.begin(), mOutputObjects.end(),
                                                 [&chosen](const auto& one) { return one.first.asString() != chosen; });
                if (oldest != mOutputObjects.end())
                {
                    mOutputObjects.erase(oldest);
                }
            }
            mOutputObjects.emplace_back(root, name);
            changed = true;
        }
    }
    if (!changed)
    {
        return;
    }
    // The list again, by name, the choice kept.
    std::vector<std::pair<LLUUID, std::string>> ordered = mOutputObjects;
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return LLStringUtil::compareDict(a.second, b.second) < 0; });
    mOutputFilter->clearRows();
    mOutputFilter->add(getString("OutputAllObjects"), LLSD(""));
    mOutputFilter->add(getString("OutputOpenScripts"), LLSD("open"));
    mOutputFilter->add(getString("OutputMyObjects"), LLSD("mine"));
    mOutputFilter->addSeparator();
    for (const auto& [id, label] : ordered)
    {
        mOutputFilter->add(label.empty() ? getString("ObjectUnnamed") : label, LLSD(id.asString()));
    }
    if (!mOutputFilter->selectByValue(LLSD(chosen)))
    {
        mOutputFilter->selectFirstItem();
    }
}

void ALFloaterScriptStudio::onOutputFilter()
{
    const std::string whose = mOutputFilter->getValue().asString();
    const std::string kind  = mOutputKind ? mOutputKind->getValue().asString() : std::string();
    std::string       words = mOutputFind ? mOutputFind->getText() : std::string();
    LLStringUtil::trim(words);
    if (whose.empty() && kind.empty() && words.empty())
    {
        mOutput->setFilter(nullptr);
        return;
    }
    mOutput->setFilter([this, whose, kind, words](const ALOutputView::Entry& entry) {
        const std::string said = entry.key["kind"].asString();
        if (!kind.empty() && said != kind)
        {
            return false;
        }
        // The studio's own words are about no object: shown whoever's
        // are, but for one object's alone.
        if (whose == "open")
        {
            if (said != "studio" && indexOf(ALScriptRef(entry.key["prim"].asUUID(), entry.key["item"].asUUID())) == NONE)
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

void ALFloaterScriptStudio::onOutputChosen(const ALOutputView::Entry& entry)
{
    // What the words said could be done, done: a save held over what was
    // found asked again -- saved, as a second save would have been, where
    // the text is still what was held -- a failed one tried again, a copy
    // into the inventory, a file.
    if (entry.value.has("action"))
    {
        const std::string action = entry.value["action"].asString();
        const size_t      index  = indexOf(entry.value["doc"].asString());
        if (index == NONE)
        {
            return;
        }
        activate(index);
        Doc& doc = *mDocs[index];
        if (action == "save_anyway")
        {
            saveAsked(doc);
        }
        else if (action == "retry")
        {
            save(doc);
        }
        else if (action == "copy")
        {
            saveCopyToInventory(doc);
        }
        else if (action == "export")
        {
            saveToFile();
        }
        else if (action == "take_external" && doc.externalWaiting)
        {
            // What was typed here a step back in the undo.
            takeExternal(doc, *doc.externalWaiting);
        }
        else if (action == "keep_here" && doc.externalWaiting)
        {
            // The external editor's save not sent; its copy is written from
            // here at the next save.
            doc.externalWaiting.reset();
            LLStringUtil::format_map_t args;
            args["[NAME]"] = doc.name;
            setStatus(getString("ExternalKept", args));
        }
        return;
    }
    // A line of the studio's own: the script it names, brought forward.
    if (entry.value.has("doc"))
    {
        const size_t index = indexOf(entry.value["doc"].asString());
        if (index != NONE)
        {
            activate(index);
            if (entry.value["issue"].asBoolean())
            {
                showBottom("problems_tab");
            }
        }
        return;
    }
    if (entry.value["item"].asUUID().isNull())
    {
        return;
    }
    const S32 line   = entry.value["line"].asInteger();
    const S32 column = entry.value["column"].asInteger();
    noteJump();
    // A frame in an include: the include, where it can be opened.
    if (!entry.value["file"].asString().empty())
    {
        openIncludeAt(entry.value["file"].asString(), entry.value["fileName"].asString(), line, column, 0);
        return;
    }
    const ALScriptRef ref(entry.value["prim"].asUUID(), entry.value["item"].asUUID());
    size_t            index = indexOf(ref);
    if (index == NONE)
    {
        // The script it names, opened; the line once it has loaded.
        openScript(ref, entry.value["name"].asString());
        index = indexOf(ref);
        if (index != NONE)
        {
            mDocs[index]->pendingLine = line;
        }
        return;
    }
    activate(index);
    if (line >= 0)
    {
        ALCodeEditor& source = sourceInFront(*mDocs[index]);
        source.goTo(ALTextPos(line, llmax(0, column)));
        source.setFocus(true);
    }
}
