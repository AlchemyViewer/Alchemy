/**
 * @file alscriptweightspane.cpp
 * @brief The Script Studio's Weights tab: what a script's code weighs, for each target and by part.
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

#include "linden_common.h"

#include "alscriptweightspane.h"

#include "alpanelist.h"
#include "alrecoverystore.h"
#include "llbutton.h"
#include "llfloater.h"
#include "llpanel.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <fmt/format.h>

#include <map>
#include <numeric>
#include <tuple>

namespace
{
    // The columns, as the skin names them, in its order.
    enum TargetColumn : S32
    {
        TARGET_NAME,
        TARGET_CODE,
        TARGET_SHARE,
        TARGET_CHANGE
    };
    enum PartColumn : S32
    {
        PART_NAME,
        PART_KIND,
        PART_BYTES,
        PART_SHARE,
        PART_CHANGE,
        PART_LINE
    };
    enum StringColumn : S32
    {
        STRING_TEXT,
        STRING_BYTES,
        STRING_USES,
        STRING_CHANGE,
        STRING_LINE
    };
    // How many of a shared start's strings its tip names.
    constexpr size_t STRINGS_NAMED = 6;

    // A string as a row shows it: quoted, on one line, its start where it
    // is long, never cut inside a character.
    std::string shownText(std::string_view text)
    {
        return ALScriptWeigh::quoted(text, 48);
    }

    // What a part is across two weighings of the same script: which part
    // it is, not where it is, which an edit above it moves -- and, among
    // parts of one kind, name and file, which of them in order, since
    // every anonymous function has the same empty name.
    using Identity = std::tuple<ALScriptWeight::Part::Kind, std::string, std::string, std::string, size_t>;
    Identity identity(const ALScriptWeight::Part& part, size_t nth)
    {
        return { part.kind, part.within, part.name, part.file, nth };
    }

    // Which of the parts like it each part is, in order.
    std::vector<size_t> nths(const std::vector<ALScriptWeight::Part>& parts)
    {
        std::map<Identity, size_t> seen;
        std::vector<size_t>        out;
        out.reserve(parts.size());
        for (const ALScriptWeight::Part& part : parts)
        {
            out.push_back(seen[identity(part, 0)]++);
        }
        return out;
    }

}

static LLPanelInjector<ALScriptWeightsPane> t_script_studio_weights("script_studio_weights");

ALScriptWeightsPane::ALScriptWeightsPane(const LLPanel::Params& params) : LLPanel(params) {}

bool ALScriptWeightsPane::postBuild()
{
    // The words it says things in: the window's.
    if (const LLFloater* window = getParentByType<LLFloater>())
    {
        mStrings = window;
    }
    mHead    = getChild<LLTextBox>("weights_head");
    mTargets      = getChild<ALPaneList>("weights_targets");
    mParts        = getChild<ALPaneList>("weights_parts");
    mStringsPanel = getChild<LLView>("weights_strings_panel");
    mStringsList  = getChild<ALPaneList>("weights_strings");
    mStringsList->setComparison([this](S32 column, const LLScrollListItem* a, const LLScrollListItem* b) { return compareStrings(column, a, b); });
    mKeepStart = getChild<LLButton>("weights_keep_start");
    mKeepStart->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (const StringRow* start = chosenStart(); start && mKeepStartCall && (!mCanKeepStart || mCanKeepStart()))
        {
            // Copied: what the window does may refill the list.
            const std::string              text    = start->text;
            const std::vector<std::string> strings = start->strings;
            mKeepStartCall(text, strings);
        }
    });
    // A target chosen shows its parts; the script's own until then.
    mTargets->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (LLScrollListItem* item = mTargets->getFirstSelected())
        {
            mChosen = static_cast<ALScriptWeight::Target>(item->getValue().asInteger());
            fillParts();
        }
    });
    // Sorted by a column's title as what it says: bytes, shares and
    // changes as numbers, a place by file and line; the compiler's order
    // where two are the same.
    mParts->setComparison([this](S32 column, const LLScrollListItem* a, const LLScrollListItem* b) { return compare(column, a, b); });
    mTargets->setComparison([this](S32 column, const LLScrollListItem* a, const LLScrollListItem* b) {
        const auto of = [this](const LLScrollListItem* item) -> const ALScriptWeight* {
            for (const ALScriptWeight& weight : mShown.weights)
            {
                if (static_cast<S32>(weight.target) == item->getValue().asInteger())
                {
                    return &weight;
                }
            }
            return nullptr;
        };
        const ALScriptWeight* x = of(a);
        const ALScriptWeight* y = of(b);
        if (!x || !y)
        {
            return 0;
        }
        const auto order = [](F64 p, F64 q) { return p < q ? -1 : p > q ? 1 : 0; };
        switch (column)
        {
            case TARGET_CODE:
                return order((F64)x->total, (F64)y->total);
            case TARGET_SHARE:
                return order(x->limit ? (F64)x->total / (F64)x->limit : 0.0, y->limit ? (F64)y->total / (F64)y->limit : 0.0);
            default:
                return order((F64)x->target, (F64)y->target);
        }
    });
    return true;
}

void ALScriptWeightsPane::show(Shown shown)
{
    const bool same = shown.id == mShown.id;
    mShown          = std::move(shown);
    if (!same || !chosen())
    {
        mChosen = mShown.weights.empty() ? ALScriptWeight::Target::SLua : mShown.weights.front().target;
    }
    fillTargets();
    fillParts();
}

void ALScriptWeightsPane::showNothing(const std::string& why)
{
    mShown = Shown();
    mRows.clear();
    mStringRows.clear();
    mTargets->deleteAllItems();
    mParts->deleteAllItems();
    mStringsList->deleteAllItems();
    mStringsPanel->setVisible(false);
    mHead->setText(why);
    mHead->setToolTip(std::string());
}

const ALScriptWeight* ALScriptWeightsPane::chosen() const
{
    for (const ALScriptWeight& weight : mShown.weights)
    {
        if (weight.target == mChosen)
        {
            return &weight;
        }
    }
    return nullptr;
}

const ALScriptWeight* ALScriptWeightsPane::savedFor(ALScriptWeight::Target target) const
{
    for (const ALScriptWeight& weight : mShown.saved)
    {
        if (weight.target == target && weight.compiled)
        {
            return &weight;
        }
    }
    return nullptr;
}

std::string ALScriptWeightsPane::share(size_t bytes, size_t limit) const
{
    // As a percentage, as the viewer's language writes one.
    return limit ? mStrings->getString("Percent", { { "[VALUE]", fmt::format("{:f}", (F64)bytes * 100.0 / (F64)limit) } }) : std::string();
}

std::string ALScriptWeightsPane::kilobytes(size_t bytes, bool estimate) const
{
    // The number unrounded: the words round it, as the viewer's language
    // writes a number.
    LLStringUtil::format_map_t args;
    args["[SIZE]"] = fmt::format("{:f}", (F64)bytes / 1024.0);
    return mStrings->getString(estimate ? "WeightsKilobytesEstimate" : "WeightsKilobytes", args);
}

std::string ALScriptWeightsPane::changeText(const std::optional<S64>& change, bool fresh) const
{
    if (fresh)
    {
        return mStrings->getString("WeightsNew");
    }
    if (!change || *change == 0)
    {
        return std::string();
    }
    return fmt::format("{:+}", *change);
}

void ALScriptWeightsPane::fillTargets()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    mTargets->deleteAllItems();
    // Side by side, the script's own first and in bold: what each would
    // cost it, which is how to choose.
    for (size_t i = 0; i < mShown.weights.size(); ++i)
    {
        const ALScriptWeight&      weight = mShown.weights[i];
        const ALScriptWeight*      saved  = savedFor(weight.target);
        LLStringUtil::format_map_t args;
        args["[TARGET]"] = ALScriptWeight::nameOf(weight.target);
        args["[BYTES]"]  = std::to_string(weight.total);
        args["[MAX]"]    = std::to_string(weight.limit);
        args["[ERROR]"]  = weight.error;
        const bool  said = weight.total > 0;
        std::string tip  = mStrings->getString(!said ? "WeightsTargetFailedTip" : weight.estimate ? "WeightsTargetEstimateTip" : "WeightsTargetTip", args);
        if (said && !weight.error.empty())
        {
            tip = mStrings->getString("JoinSentences", { { "[FIRST]", tip }, { "[SECOND]", mStrings->getString("WeightsTargetErrorTip", args) } });
        }
        if (i == 0)
        {
            tip += " " + mStrings->getString("WeightsTargetOwnTip");
        }
        LLSD row;
        row["value"]                             = static_cast<S32>(weight.target);
        row["columns"][TARGET_NAME]["column"]    = "target";
        row["columns"][TARGET_NAME]["value"]     = ALScriptWeight::nameOf(weight.target);
        row["columns"][TARGET_CODE]["column"]    = "code";
        row["columns"][TARGET_CODE]["value"]     = said ? kilobytes(weight.total, weight.estimate) : mStrings->getString("WeightsNone");
        row["columns"][TARGET_SHARE]["column"]   = "share";
        row["columns"][TARGET_SHARE]["value"]    = said ? share(weight.total, weight.limit) : std::string();
        row["columns"][TARGET_CHANGE]["column"]  = "change";
        row["columns"][TARGET_CHANGE]["value"]   = said && saved ? changeText(S64(weight.total) - S64(saved->total), false) : std::string();
        for (S32 c = TARGET_NAME; c <= TARGET_CHANGE; ++c)
        {
            row["columns"][c]["tool_tip"] = tip;
            if (i == 0)
            {
                row["columns"][c]["font"]["style"] = "BOLD";
            }
        }
        mTargets->addElement(row);
    }
    mTargets->selectByValue(LLSD(static_cast<S32>(mChosen)));
}

void ALScriptWeightsPane::fillParts()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    const S32 scrolled = mParts->getScrollPos();
    const S32 was      = mParts->getFirstSelected() ? mParts->getFirstSelected()->getValue().asInteger() : -1;
    // What was chosen, by which part it is, since the rows are numbered
    // afresh.
    std::optional<Identity> held;
    if (was >= 0 && static_cast<size_t>(was) < mRows.size())
    {
        held = identity(mRows[static_cast<size_t>(was)].part, mRows[static_cast<size_t>(was)].nth);
    }
    mParts->deleteAllItems();
    mRows.clear();
    const ALScriptWeight* weight = chosen();
    fillStrings();
    if (!weight)
    {
        mHead->setText(std::string());
        return;
    }

    // What it comes to, and what the numbers are of.
    LLStringUtil::format_map_t args;
    args["[NAME]"]   = mShown.name;
    args["[TARGET]"] = ALScriptWeight::nameOf(weight->target);
    args["[SIZE]"]   = fmt::format("{:f}", (F64)weight->total / 1024.0);
    args["[LIMIT]"]  = std::to_string(weight->limit / 1024);
    args["[SHARE]"]  = share(weight->total, weight->limit);
    args["[ERROR]"]  = weight->error;
    std::string head = mStrings->getString(weight->total == 0 ? "WeightsHeadFailed" : weight->estimate ? "WeightsHeadEstimate" : "WeightsHead", args);
    if (weight->total > 0 && !weight->error.empty())
    {
        head += " " + mStrings->getString("WeightsHeadError", args);
    }
    // A compiler that records no lines -- LSL's for Luau -- leaves the
    // heat beside the text with nothing to show: said, rather than left to
    // look like a script whose lines weigh nothing.
    if (weight->target == ALScriptWeight::Target::LSLLuau && weight->compiled && weight->total > 0 && weight->lines.empty())
    {
        head += " " + mStrings->getString("WeightsHeadNoLines", args);
    }
    if (mShown.beforeOptimizer)
    {
        const bool own = !mShown.weights.empty() && weight->target == mShown.weights.front().target;
        if (own && mShown.sent)
        {
            args["[SENT]"] = fmt::format("{:f}", (F64)*mShown.sent / 1024.0);
            head += " " + mStrings->getString("WeightsHeadSent", args);
        }
        else
        {
            head += " " + mStrings->getString("WeightsHeadBefore");
        }
    }
    if (mShown.region && mShown.region->hasMemory())
    {
        args["[RESERVED]"] = fmt::format("{:f}", (F64)mShown.region->memory / 1024.0);
        args["[URLS]"]     = std::to_string(mShown.region->urls);
        args["[WHEN]"]     = ALRecoveryEntry::sayWhen(mShown.region->when);
        head += " " + mStrings->getString(mShown.region->urls > 0 ? "WeightsHeadRegionUrls" : "WeightsHeadRegion", args);
    }
    // And the time its scripts take, where the region tells an estate
    // manager.
    if (mShown.region && mShown.region->hasTime())
    {
        args["[TIME]"] = fmt::format("{:.3f}", mShown.region->time);
        args["[WHEN]"] = ALRecoveryEntry::sayWhen(mShown.region->timeWhen);
        head += " " + mStrings->getString("WeightsHeadRegionTime", args);
    }
    mHead->setText(head);
    // The tip says how each of what the head says is counted.
    std::string tip = mStrings->getString("WeightsHeadTip");
    if (mShown.region && mShown.region->hasMemory())
    {
        tip += " " + mStrings->getString("WeightsHeadRegionTip");
    }
    if (mShown.region && mShown.region->hasTime())
    {
        tip += " " + mStrings->getString("WeightsHeadRegionTimeTip");
    }
    mHead->setToolTip(tip);

    // Each part, with how it moved since the text was last saved, where it
    // was weighed for this target then.
    const ALScriptWeight*      saved = savedFor(weight->target);
    std::map<Identity, size_t> before;
    if (saved)
    {
        const std::vector<size_t> nth = nths(saved->parts);
        for (size_t i = 0; i < saved->parts.size(); ++i)
        {
            before[identity(saved->parts[i], nth[i])] = saved->parts[i].bytes;
        }
    }
    const std::string         state_tip = mStrings->getString("WeightsStateTip");
    const std::string         frame_tip = mStrings->getString("WeightsFrameTip");
    S32                       select    = -1;
    const std::vector<size_t> nth       = nths(weight->parts);
    for (size_t i = 0; i < weight->parts.size(); ++i)
    {
        const ALScriptWeight::Part& part = weight->parts[i];
        Row                         one;
        one.part = part;
        one.nth  = nth[i];
        if (saved)
        {
            const auto found = before.find(identity(part, one.nth));
            one.fresh        = found == before.end();
            if (!one.fresh)
            {
                one.change = S64(part.bytes) - S64(found->second);
            }
        }
        one.name        = partName(part);
        const S32 index = static_cast<S32>(mRows.size());
        if (held && identity(part, one.nth) == *held)
        {
            select = index;
        }
        const std::string tip = part.kind == ALScriptWeight::Part::Kind::State   ? state_tip
                                : part.kind == ALScriptWeight::Part::Kind::Frame ? frame_tip
                                                                                 : std::string();
        LLSD row;
        row["value"]                          = index;
        row["columns"][PART_NAME]["column"]   = "part";
        row["columns"][PART_NAME]["value"]    = one.name;
        row["columns"][PART_KIND]["column"]   = "kind";
        row["columns"][PART_KIND]["value"]    = kindName(part.kind);
        row["columns"][PART_BYTES]["column"]  = "bytes";
        row["columns"][PART_BYTES]["value"]   = std::to_string(part.bytes);
        row["columns"][PART_SHARE]["column"]  = "share";
        row["columns"][PART_SHARE]["value"]   = share(part.bytes, weight->limit);
        row["columns"][PART_CHANGE]["column"] = "change";
        row["columns"][PART_CHANGE]["value"]  = changeText(one.change, one.fresh);
        row["columns"][PART_LINE]["column"]   = "line";
        row["columns"][PART_LINE]["value"]    = where(part);
        if (!tip.empty())
        {
            for (S32 c = PART_NAME; c <= PART_LINE; ++c)
            {
                row["columns"][c]["tool_tip"] = tip;
            }
        }
        mRows.push_back(std::move(one));
        mParts->addElement(row);
    }
    mParts->updateSort();
    if (select >= 0)
    {
        mParts->selectByValue(LLSD(select));
    }
    mParts->setScrollPos(scrolled);
}

std::string ALScriptWeightsPane::partName(const ALScriptWeight::Part& part) const
{
    // What the target spends on the script as a whole, in the viewer's
    // words rather than the weigher's.
    static const std::pair<const char*, const char*> WORDS[] = { { "registers", "WeightsPartRegisters" },
                                                                 { "assembly", "WeightsPartAssembly" },
                                                                 { "globals", "WeightsPartGlobals" },
                                                                 { "script", "WeightsPartScript" },
                                                                 { "strings", "WeightsPartStrings" } };
    if (part.kind == ALScriptWeight::Part::Kind::Frame || part.kind == ALScriptWeight::Part::Kind::Constant)
    {
        for (const auto& [word, key] : WORDS)
        {
            if (part.name == word)
            {
                return mStrings->getString(key);
            }
        }
    }
    if (part.name.empty())
    {
        const auto handler = part.file.empty() ? mShown.handlers.find(part.line) : mShown.handlers.end();
        return handler != mShown.handlers.end() ? handler->second : mStrings->getString("WeightsPartUnnamed");
    }
    if (!part.within.empty())
    {
        LLStringUtil::format_map_t args;
        args["[EVENT]"] = part.name;
        args["[STATE]"] = part.within;
        return mStrings->getString("WeightsPartHandler", args);
    }
    return part.name;
}

std::string ALScriptWeightsPane::kindName(ALScriptWeight::Part::Kind kind) const
{
    switch (kind)
    {
        case ALScriptWeight::Part::Kind::Function:
            return mStrings->getString("WeightsKindFunction");
        case ALScriptWeight::Part::Kind::Handler:
            return mStrings->getString("WeightsKindHandler");
        case ALScriptWeight::Part::Kind::State:
            return mStrings->getString("WeightsKindState");
        case ALScriptWeight::Part::Kind::Global:
            return mStrings->getString("WeightsKindGlobal");
        case ALScriptWeight::Part::Kind::Constant:
            return mStrings->getString("WeightsKindConstant");
        case ALScriptWeight::Part::Kind::Frame:
        default:
            return mStrings->getString("WeightsKindFrame");
    }
}

std::string ALScriptWeightsPane::where(const ALScriptWeight::Part& part) const
{
    return whereAt(part.file, part.line);
}

std::string ALScriptWeightsPane::whereAt(const std::string& file, S32 line) const
{
    if (line < 0)
    {
        return std::string();
    }
    if (file.empty())
    {
        return std::to_string(line + 1);
    }
    const auto named = mShown.fileNames.find(file);
    return fmt::format("{}:{}", named != mShown.fileNames.end() ? named->second : file, line + 1);
}

std::optional<ALScriptWeightsPane::Place> ALScriptWeightsPane::chosenPlace(const ALPaneList* list) const
{
    const LLScrollListItem* item = list ? list->getFirstSelected() : nullptr;
    if (!item)
    {
        return std::nullopt;
    }
    const size_t index = static_cast<size_t>(std::max(0, item->getValue().asInteger()));
    Place        place;
    if (list == mStringsList && index < mStringRows.size())
    {
        place.file = mStringRows[index].file;
        place.line = mStringRows[index].line;
    }
    else if (list == mParts && index < mRows.size())
    {
        place.file   = mRows[index].part.file;
        place.line   = mRows[index].part.line;
        place.column = std::max(0, mRows[index].part.column);
    }
    else
    {
        return std::nullopt;
    }
    if (place.line < 0)
    {
        return std::nullopt;
    }
    if (!place.file.empty())
    {
        const auto named = mShown.fileNames.find(place.file);
        place.fileName   = named != mShown.fileNames.end() ? named->second : place.file;
    }
    return place;
}

void ALScriptWeightsPane::fillStrings()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    const S32 scrolled = mStringsList->getScrollPos();
    // What was chosen, by what it is, since the rows are made afresh.
    std::optional<std::pair<bool, std::string>> held;
    if (const LLScrollListItem* item = mStringsList->getFirstSelected())
    {
        const size_t was = static_cast<size_t>(std::max(0, item->getValue().asInteger()));
        if (was < mStringRows.size())
        {
            held = std::make_pair(mStringRows[was].start, mStringRows[was].text);
        }
    }
    mStringsList->deleteAllItems();
    mStringRows.clear();
    // A Luau target's table of strings; no other target has one.
    const ALScriptWeight* weight = chosen();
    const bool            luau   = weight && weight->compiled &&
                          (weight->target == ALScriptWeight::Target::SLua || weight->target == ALScriptWeight::Target::LSLLuau);
    mStringsPanel->setVisible(luau);
    if (!luau)
    {
        return;
    }
    // What was weighed as the text was last saved, by text.
    const ALScriptWeight*                                                                    saved = savedFor(weight->target);
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>                 saved_strings;
    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>>         saved_starts;
    if (saved)
    {
        for (const ALScriptWeight::String& one : saved->strings)
        {
            saved_strings.insert(one.text);
        }
        for (const ALScriptWeight::SharedStart& one : saved->sharedStarts)
        {
            saved_starts.emplace(one.start, one.saved);
        }
    }
    // Which start each string shares, by its place in the weight's.
    boost::unordered_flat_map<size_t, size_t> start_of;
    for (size_t i = 0; i < weight->sharedStarts.size(); ++i)
    {
        for (const size_t s : weight->sharedStarts[i].strings)
        {
            start_of.emplace(s, i);
        }
    }
    S32        select = -1;
    const auto add    = [&](StringRow one, const std::string& uses, const std::string& tip, bool italic) {
        const S32 index = static_cast<S32>(mStringRows.size());
        if (held && held->first == one.start && held->second == one.text)
        {
            select = index;
        }
        LLSD row;
        row["value"]                            = index;
        row["columns"][STRING_TEXT]["column"]   = "string";
        row["columns"][STRING_TEXT]["value"]    = one.name;
        row["columns"][STRING_BYTES]["column"]  = "bytes";
        row["columns"][STRING_BYTES]["value"]   = one.start ? mStrings->getString("WeightsSaves", { { "[BYTES]", std::to_string(-one.bytes) } })
                                                            : std::to_string(one.bytes);
        row["columns"][STRING_USES]["column"]   = "uses";
        row["columns"][STRING_USES]["value"]    = uses;
        row["columns"][STRING_CHANGE]["column"] = "change";
        row["columns"][STRING_CHANGE]["value"]  = changeText(one.change, one.fresh);
        row["columns"][STRING_LINE]["column"]   = "line";
        row["columns"][STRING_LINE]["value"]    = whereAt(one.file, one.line);
        for (S32 c = STRING_TEXT; c <= STRING_LINE; ++c)
        {
            row["columns"][c]["tool_tip"] = tip;
            if (italic)
            {
                row["columns"][c]["font"]["style"] = "ITALIC";
            }
        }
        mStringRows.push_back(std::move(one));
        mStringsList->addElement(row);
    };
    // The starts several strings share first, best first: each at the
    // first of its strings, the script's own before an include's.
    for (size_t i = 0; i < weight->sharedStarts.size(); ++i)
    {
        const ALScriptWeight::SharedStart& shared = weight->sharedStarts[i];
        StringRow                          one;
        one.start = true;
        one.index = i;
        one.text  = shared.start;
        one.bytes = -static_cast<S64>(shared.saved);
        for (const size_t s : shared.strings)
        {
            if (s >= weight->strings.size())
            {
                continue;
            }
            const ALScriptWeight::String& with = weight->strings[s];
            one.uses += with.loads;
            const bool earlier = with.line >= 0 && (one.line < 0 || std::make_pair(!with.file.empty(), with.line) < std::make_pair(!one.file.empty(), one.line));
            if (earlier)
            {
                one.line = with.line;
                one.file = with.file;
            }
        }
        // How it moved as its bytes are said, as less: a saving grown since
        // is a change down, as a part grown lighter is.
        if (saved)
        {
            const auto before = saved_starts.find(shared.start);
            one.fresh         = before == saved_starts.end();
            if (!one.fresh)
            {
                one.change = S64(before->second) - S64(shared.saved);
            }
        }
        // Its strings, named in its tip: the first few, and how many more.
        std::string named;
        for (const size_t s : shared.strings)
        {
            if (s >= weight->strings.size())
            {
                continue;
            }
            one.strings.push_back(weight->strings[s].text);
            if (one.strings.size() <= STRINGS_NAMED)
            {
                named = named.empty() ? shownText(weight->strings[s].text)
                                      : mStrings->getString("JoinList", { { "[FIRST]", named }, { "[SECOND]", shownText(weight->strings[s].text) } });
            }
        }
        if (one.strings.size() > STRINGS_NAMED)
        {
            named = mStrings->getString("WeightsSharedStartMore",
                                        { { "[STRINGS]", named }, { "[COUNT]", std::to_string(one.strings.size() - STRINGS_NAMED) } });
        }
        LLStringUtil::format_map_t args;
        args["[START]"]  = shownText(shared.start);
        args["[COUNT]"]  = std::to_string(shared.strings.size());
        args["[LENGTH]"] = std::to_string(shared.start.size());
        args["[SAVED]"]  = std::to_string(shared.saved);
        one.name         = mStrings->getString("WeightsSharedStart", args);
        const std::string uses = std::to_string(one.uses);
        const std::string tip  = mStrings->getString("JoinSentences", { { "[FIRST]", mStrings->getString("WeightsSharedStartTip", args) },
                                                                        { "[SECOND]", mStrings->getString("WeightsSharedStartStrings", { { "[STRINGS]", named } }) } });
        add(std::move(one), uses, tip, true);
    }
    // Then each string, the heaviest first.
    std::vector<size_t> order(weight->strings.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [weight](size_t a, size_t b) { return weight->strings[a].bytes > weight->strings[b].bytes; });
    for (const size_t s : order)
    {
        const ALScriptWeight::String& string = weight->strings[s];
        StringRow                     one;
        one.index = s;
        one.text  = string.text;
        one.name  = shownText(string.text);
        one.bytes = static_cast<S64>(string.bytes);
        one.uses  = string.uses;
        one.line  = string.line;
        one.file  = string.file;
        one.fresh = saved && !saved_strings.contains(string.text);
        LLStringUtil::format_map_t args;
        args["[BYTES]"] = std::to_string(string.bytes);
        args["[USES]"]  = std::to_string(string.uses);
        args["[LOADS]"] = std::to_string(string.loads);
        std::string tip = mStrings->getString(string.name && string.uses == 0 ? "WeightsStringNameTip" : "WeightsStringTip", args);
        // Where it shares a start, which.
        if (const auto shares = start_of.find(s); shares != start_of.end())
        {
            const ALScriptWeight::SharedStart& shared = weight->sharedStarts[shares->second];
            tip = mStrings->getString("JoinSentences",
                                      { { "[FIRST]", tip },
                                        { "[SECOND]", mStrings->getString("WeightsStringShares", { { "[START]", shownText(shared.start) },
                                                                                                   { "[COUNT]", std::to_string(shared.strings.size() - 1) } }) } });
        }
        add(std::move(one), string.name && string.uses == 0 ? mStrings->getString("WeightsStringName") : std::to_string(string.uses), tip, false);
    }
    mStringsList->updateSort();
    if (select >= 0)
    {
        mStringsList->selectByValue(LLSD(select));
    }
    mStringsList->setScrollPos(scrolled);
}

const ALScriptWeightsPane::StringRow* ALScriptWeightsPane::chosenStart() const
{
    const ALScriptWeight*   weight = chosen();
    const LLScrollListItem* item   = mStringsList->getFirstSelected();
    if (!weight || weight->target != ALScriptWeight::Target::SLua || !item)
    {
        return nullptr;
    }
    const size_t index = static_cast<size_t>(std::max(0, item->getValue().asInteger()));
    return index < mStringRows.size() && mStringRows[index].start ? &mStringRows[index] : nullptr;
}

void ALScriptWeightsPane::draw()
{
    // What can be kept once is what is chosen, which a click or a key may
    // change at any time, in a script that can be written to: asked as it
    // is drawn.
    mKeepStart->setEnabled(mKeepStartCall && chosenStart() && (!mCanKeepStart || mCanKeepStart()));
    LLPanel::draw();
}

S32 ALScriptWeightsPane::compareStrings(S32 column, const LLScrollListItem* a, const LLScrollListItem* b) const
{
    const size_t i = static_cast<size_t>(a->getValue().asInteger());
    const size_t j = static_cast<size_t>(b->getValue().asInteger());
    if (i >= mStringRows.size() || j >= mStringRows.size())
    {
        return 0;
    }
    const StringRow& x     = mStringRows[i];
    const StringRow& y     = mStringRows[j];
    const auto       order = [](S64 p, S64 q) { return p < q ? -1 : p > q ? 1 : 0; };
    // The shared starts together, whichever column.
    S32 said = order(!x.start, !y.start);
    if (said != 0)
    {
        return said;
    }
    switch (column)
    {
        case STRING_TEXT:
            said = LLStringUtil::compareDict(x.text, y.text);
            break;
        case STRING_BYTES:
            said = order(x.bytes, y.bytes);
            break;
        case STRING_USES:
            said = order(S64(x.uses), S64(y.uses));
            break;
        case STRING_CHANGE:
            // One new since the save moved by all its bytes as they are
            // said: a string by its own, a start by what it saves, as less.
            said = order(x.fresh ? x.bytes : x.change.value_or(0), y.fresh ? y.bytes : y.change.value_or(0));
            break;
        case STRING_LINE:
            said = order(x.line < 0, y.line < 0);
            if (said == 0)
            {
                said = x.file != y.file ? LLStringUtil::compareDict(x.file, y.file) : order(x.line, y.line);
            }
            break;
        default:
            break;
    }
    return said != 0 ? said : order(S64(i), S64(j));
}

S32 ALScriptWeightsPane::compare(S32 column, const LLScrollListItem* a, const LLScrollListItem* b) const
{
    const size_t i = static_cast<size_t>(a->getValue().asInteger());
    const size_t j = static_cast<size_t>(b->getValue().asInteger());
    if (i >= mRows.size() || j >= mRows.size())
    {
        return 0;
    }
    const Row& x     = mRows[i];
    const Row& y     = mRows[j];
    const auto order = [](S64 p, S64 q) { return p < q ? -1 : p > q ? 1 : 0; };
    S32        said  = 0;
    switch (column)
    {
        case PART_NAME:
            said = LLStringUtil::compareDict(x.name, y.name);
            break;
        case PART_KIND:
            said = order(S64(x.part.kind), S64(y.part.kind));
            break;
        case PART_BYTES:
        case PART_SHARE:
            said = order(S64(x.part.bytes), S64(y.part.bytes));
            break;
        case PART_CHANGE:
            // A part new since the save moved by all of it.
            said = order(x.fresh ? S64(x.part.bytes) : x.change.value_or(0), y.fresh ? S64(y.part.bytes) : y.change.value_or(0));
            break;
        case PART_LINE:
            // Placed before unplaced, the script's own before its includes'.
            said = order(x.part.line < 0, y.part.line < 0);
            if (said == 0)
            {
                said = x.part.file != y.part.file ? LLStringUtil::compareDict(x.part.file, y.part.file) : order(x.part.line, y.part.line);
            }
            break;
        default:
            break;
    }
    return said != 0 ? said : order(S64(i), S64(j));
}
