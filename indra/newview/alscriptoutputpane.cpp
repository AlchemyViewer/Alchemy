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

#include "allinebreaks.h"
#include "alscriptmessages.h"
#include "alscriptstudiodoc.h"
#include "alscriptstudiopane.h"
#include "alscriptstudioservices.h"
#include "alstringmatch.h"
#include "llbutton.h"
#include "llfloater.h"
#include "llfontgl.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llpanel.h"
#include "lluicolortable.h"

#include <boost/unordered/unordered_flat_set.hpp>

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

    // The row over the log: its ends' margin and the gap between its
    // controls; what a button adds to its words; how narrow the two lists
    // may go, still saying which is picked; and how wide the words' box
    // stays before they do, and at the least.
    constexpr S32 ROW_EDGE    = 2;
    constexpr S32 ROW_GAP     = 6;
    constexpr S32 BUTTON_PAD  = 20;
    constexpr S32 WHOSE_LEAST = 100;
    constexpr S32 KIND_LEAST  = 80;
    constexpr S32 FIND_WANTS  = 120;
    constexpr S32 FIND_LEAST  = 60;
}

static LLPanelInjector<ALScriptOutputPane> t_script_studio_output("script_studio_output");

ALScriptOutputPane::ALScriptOutputPane(const LLPanel::Params& params) : LLPanel(params) {}

bool ALScriptOutputPane::postBuild()
{
    mView  = getChild<ALOutputView>("output");
    mWhose = getChild<LLComboBox>("output_filter");
    mKind  = getChild<LLComboBox>("output_kind");
    mFind  = getChild<LLFilterEditor>("output_find");
    mClear = getChild<LLButton>("output_clear");
    mCopy  = getChild<LLButton>("output_copy");
    mWhoseWidth = mWhose->getRect().getWidth();
    mKindWidth  = mKind->getRect().getWidth();
    layoutRow();
    // The window this is a tab of, found through the view tree, as what
    // the tab asks of it.
    if (!ALScriptStudioPane::findWindow(*this, "The Output tab", mServices, mWindow))
    {
        return true;
    }
    mView->setPlaceholder(mServices->words("NoOutput"));
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
    mClear->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        // What was said goes; whose words are listened to stays.
        mView->clearEntries();
        mUnread = false;
        mWindow->outputUnreadChanged();
    });
    mCopy->setCommitCallback([this](LLUICtrl*, const LLSD&) {
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
            mServices->setStatus(mServices->words("OutputNothingToCopy"));
            return;
        }
        LLClipboard::instance().copyToClipboard(all, 0, static_cast<S32>(all.size()));
        mServices->setStatus(mServices->counted("OutputCopied", lines));
    });
    return true;
}



ALScriptOutputPane::Place ALScriptOutputPane::heard(const ALScriptRuntimeEvent& event)
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
    // expansion's -- back to the source's, or an include's. Where it is
    // not, or its map is not known yet, the line is the one the region
    // counts, and a link to it says so, to be read back once it is.
    const ALScriptStudioDoc* open     = event.item.notNull() ? mServices->findDoc(ALScriptRef(event.prim, event.item)) : nullptr;
    const auto               where_of = [&](S32 line, S32 column) {
        Place where;
        where.line    = line;
        where.column  = column;
        where.running = line >= 0;
        if (!open || !open->runningMap())
        {
            return where;
        }
        const ALScriptStudioDoc::RunningPlace place = open->placeOfRunning(line, column);
        if (!place.generated)
        {
            where.line     = place.line;
            where.column   = place.column;
            where.file     = place.file;
            where.fileName = place.fileName;
            where.running  = false;
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
        value["running"]  = where.running;
        return value;
    };
    const Place at = where_of(event.line, event.column);

    // One line of the log, or more where the script said more; the
    // script's name a link to it, at the line of a run-time error.
    using Channel = ALScriptRuntimeEvent::Channel;
    ALOutputView::Entry entry;
    entry.time = clockOf(event.time);
    // Whose: the object, the prim of it that spoke where that is not its
    // root, and the script.
    entry.source = event.objectName;
    if (!event.primName.empty())
    {
        entry.source += " \xE2\x96\xB8 " + event.primName;
    }
    if (!event.scriptName.empty())
    {
        entry.source += " / " + event.scriptName;
    }
    entry.kind = event.isError                        ? mServices->words("KindError")
                 : event.channel == Channel::OwnerSay ? mServices->words("KindOwnerSay")
                 : event.channel == Channel::Said     ? mServices->words("KindSaid")
                 : event.channel == Channel::SaidTo   ? mServices->words("KindSaidTo")
                 : event.channel == Channel::Instant  ? mServices->words("KindInstant")
                                                      : std::string();
    // Its breaks as the log reads them, so that the frames' links below
    // count the lines the log shows.
    entry.text   = ALLineBreaks::withLineFeeds(event.isError && !event.error.empty() ? event.error : event.message);
    while (!entry.text.empty() && entry.text.back() == '\n')
    {
        entry.text.pop_back();
    }
    // What the filters go by: whose it is, and what kind of thing.
    entry.key["root"] = event.root;
    entry.key["prim"] = event.prim;
    entry.key["item"] = event.item;
    // What an object says aloud or in an IM is heard only from the agent's
    // own objects.
    const bool aloud  = event.channel == Channel::Said || event.channel == Channel::SaidTo || event.channel == Channel::Instant;
    entry.key["kind"] = event.isError                         ? "error"
                        : event.channel == Channel::OwnerSay  ? "owner"
                        : event.channel == Channel::Instant   ? "im"
                        : aloud                               ? "said"
                                                              : "debug";
    entry.key["mine"] = event.channel == Channel::OwnerSay || aloud || (event.root.notNull() && mWindow->ownsObject(event.root));
    if (event.isError)
    {
        entry.color = runtime_color.get();
        if (at.line >= 0)
        {
            LLStringUtil::format_map_t line;
            line["[LINE]"] = std::to_string(at.line + 1);
            entry.text += " " + mServices->words("OutputAtLine", line);
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
            entry.text += "\n" + ALLineBreaks::withLineFeeds(line);
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
                link.tooltip   = mServices->words("OutputOpenAtLine", args);
                link.value     = link_value(there);
                entry.links.push_back(std::move(link));
            }
        }
    }
    else if (event.channel == Channel::OwnerSay || aloud)
    {
        // What the owner was told, in the colour chat shows an object's
        // words in; the debug channel's in the plain ink.
        entry.color = owner_color.get();
    }
    if (event.item.notNull())
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = event.scriptName;
        args["[LINE]"] = std::to_string(at.line + 1);
        entry.link     = true;
        entry.tooltip  = mServices->words(event.isError && at.line >= 0 ? "OutputOpenAtLine" : "OutputOpen", args);
        entry.value    = link_value(at);
    }
    mView->append(std::move(entry));

    // An error the Output tab is not showing, said on its title -- where
    // the filters let it through: a stranger's, filtered out, is not news.
    if (event.isError && mView->shows(mView->entries().back()))
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
    entry.source      = mServices->words("OutputSourceStudio");
    // Its breaks as the log reads them, so that the links below count the
    // lines the log shows.
    entry.text        = ALLineBreaks::withLineFeeds(text);
    entry.key["kind"] = "studio";
    entry.lane        = 1;
    if (failure)
    {
        entry.color = alarm.get();
    }
    // The script's name where the words say it, a link to its tab.
    const size_t at = doc && !doc->name.empty() ? entry.text.find(doc->name) : std::string::npos;
    if (at != std::string::npos)
    {
        const size_t              line_start = entry.text.rfind('\n', at);
        ALOutputView::Entry::Link link;
        link.line  = static_cast<S32>(std::count(entry.text.begin(), entry.text.begin() + at, '\n'));
        link.begin = static_cast<S32>(line_start == std::string::npos ? at : at - line_start - 1);
        link.end   = link.begin + static_cast<S32>(doc->name.size());
        LLStringUtil::format_map_t args;
        args["[NAME]"]      = doc->name;
        link.tooltip        = mServices->words("OutputStudioShow", args);
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
                                  : action == "take_saved"    ? "ActionTakeSaved"
                                  : action == "keep_saved"    ? "ActionKeepSaved"
                                  : action == "compare_saved" ? "ActionCompareSaved"
                                  : action == "merge_saved"   ? "ActionMergeSaved"
                                  : action == "reload_world"  ? "ActionReloadWorld"
                                  : action == "compare_world" ? "ActionCompareWorld"
                                  : action == "merge_world"   ? "ActionMergeWorld"
                                  : action == "show_compare"  ? "ActionShowCompare"
                                  : action == "apply_fixes"   ? "ActionApplyFixes"
                                  : action == "make_strict"   ? "ActionMakeStrict"
                                  : action == "master_link_hint"    ? "ActionMasterLink"
                                  : action == "master_send"         ? "ActionMasterSend"
                                  : action == "master_compare"      ? "ActionMasterCompare"
                                  : action == "master_compare_file" ? "ActionMasterCompareFile"
                                  : action == "master_give_way"     ? "ActionMasterGiveWay"
                                  : action == "master_open_file"    ? "ActionMasterOpenFile"
                                  : action == "master_unlink"       ? "ActionMasterUnlink"
                                  : action == "master_find"         ? "ActionMasterFind"
                                                              : "ActionExport";
        const std::string label = mServices->words(key);
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
        link.tooltip         = mServices->words(key + "Tip", args);
        link.value["action"] = action;
        link.value["doc"]    = doc->id;
        entry.links.push_back(std::move(link));
    }
    mView->append(std::move(entry));
    // A failure said while the Output tab is not in sight, and the filters
    // let it through: its title says so.
    if (failure && mView->shows(mView->entries().back()))
    {
        markUnread();
    }
}

void ALScriptOutputPane::markUnread()
{
    // Said on the title once, not once more for every line after.
    if (!mUnread && !mWindow->outputInSight())
    {
        mUnread = true;
        mWindow->outputUnreadChanged();
    }
}

void ALScriptOutputPane::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
    layoutRow();
}

void ALScriptOutputPane::layoutRow()
{
    if (!mCopy)
    {
        return;
    }
    // The buttons as wide as their words, against the right edge; the
    // words' box has what is left of the middle, and where that is too
    // little, whose words narrows first and then the kind, as the Problems
    // filter row gives its box the rest.
    const LLFontGL* font  = LLFontGL::getFontSansSerifSmall();
    S32             right = getRect().getWidth() - ROW_EDGE;
    for (LLButton* button : { mCopy, mClear })
    {
        const S32    width = font->getWidth(button->getLabelUnselected()) + BUTTON_PAD;
        const LLRect was   = button->getRect();
        button->setShape(LLRect(right - width, was.mTop, right, was.mBottom));
        right -= width + ROW_GAP;
    }
    const S32 middle = right - ROW_EDGE;
    S32       whose  = mWhoseWidth;
    S32       kind   = mKindWidth;
    S32       short_by = whose + kind + 2 * ROW_GAP + FIND_WANTS - middle;
    if (short_by > 0)
    {
        const S32 from_whose = llclamp(short_by, 0, llmax(0, whose - WHOSE_LEAST));
        whose -= from_whose;
        short_by -= from_whose;
        kind -= llclamp(short_by, 0, llmax(0, kind - KIND_LEAST));
    }
    S32 left = ROW_EDGE;
    for (auto [list, width] : { std::pair<LLUICtrl*, S32>{ mWhose, whose }, std::pair<LLUICtrl*, S32>{ mKind, kind } })
    {
        const LLRect was = list->getRect();
        list->setShape(LLRect(left, was.mTop, left + width, was.mBottom));
        left += width + ROW_GAP;
    }
    const LLRect was = mFind->getRect();
    mFind->setShape(LLRect(left, was.mTop, left + llmax(FIND_LEAST, right - left), was.mBottom));
}

void ALScriptOutputPane::pump()
{
    // What the Output tab had not shown, it has once it is looked at.
    if (mUnread && mWindow->outputInSight())
    {
        mUnread = false;
        mWindow->outputUnreadChanged();
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
    mWhose->add(mServices->words("OutputAllObjects"), LLSD(""));
    mWhose->add(mServices->words("OutputOpenScripts"), LLSD("open"));
    mWhose->add(mServices->words("OutputMyObjects"), LLSD("mine"));
    mWhose->addSeparator();
    for (const auto& [id, label] : ordered)
    {
        mWhose->add(label.empty() ? mServices->words("ObjectUnnamed") : label, LLSD(id.asString()));
    }
    if (!mWhose->selectByValue(LLSD(chosen)))
    {
        mWhose->selectFirstItem();
    }
}

void ALScriptOutputPane::filter()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    const std::string whose = mWhose->getValue().asString();
    const std::string kind  = mKind->getValue().asString();
    std::string       words = mFind->getText();
    LLStringUtil::trim(words);
    if (whose.empty() && kind.empty() && words.empty())
    {
        mView->setFilter(nullptr);
        return;
    }
    // The prims the scripts open here are in: what a script says with no
    // header naming it -- llOwnerSay, the debug channel, chat -- is known
    // only by the prim that said it.
    boost::unordered_flat_set<LLUUID> open_prims;
    if (whose == "open")
    {
        for (const ALScriptStudioDoc* doc : mServices->openDocs())
        {
            if (!doc->ref.inInventory() && doc->file.empty())
            {
                open_prims.insert(doc->ref.object);
            }
        }
    }
    mView->setFilter([this, whose, kind, words, open_prims = std::move(open_prims)](const ALOutputView::Entry& entry) {
        const std::string said = entry.key["kind"].asString();
        if (!kind.empty() && said != kind)
        {
            return false;
        }
        // The studio's own words are about no object: shown whoever's
        // are, but for one object's alone.
        if (whose == "open")
        {
            const LLUUID item = entry.key["item"].asUUID();
            const LLUUID prim = entry.key["prim"].asUUID();
            if (said != "studio" && (item.notNull() ? !mServices->findDoc(ALScriptRef(prim, item)) : !open_prims.contains(prim)))
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
        if (ALScriptStudioDoc* doc = mServices->findDoc(entry.value["doc"].asString()))
        {
            mWindow->outputAction(*doc, entry.value["action"].asString());
        }
        return;
    }
    // A line of the studio's own: the script it names, brought forward.
    if (entry.value.has("doc"))
    {
        if (ALScriptStudioDoc* doc = mServices->findDoc(entry.value["doc"].asString()))
        {
            mWindow->outputShowDoc(*doc, entry.value["issue"].asBoolean());
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
        mWindow->outputGoToInclude(entry.value["file"].asString(), entry.value["fileName"].asString(), line, column);
        return;
    }
    mWindow->outputGoTo(ALScriptRef(entry.value["prim"].asUUID(), entry.value["item"].asUUID()), entry.value["name"].asString(), line, column,
                        entry.value["running"].asBoolean());
}
