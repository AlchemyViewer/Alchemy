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
#include "llfloater.h"
#include "llpanel.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"

#include <map>
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
    mTargets = getChild<ALPaneList>("weights_targets");
    mParts   = getChild<ALPaneList>("weights_parts");
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
    mTargets->deleteAllItems();
    mParts->deleteAllItems();
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
    return limit ? mStrings->getString("Percent", { { "[VALUE]", llformat("%f", (F64)bytes * 100.0 / (F64)limit) } }) : std::string();
}

std::string ALScriptWeightsPane::kilobytes(size_t bytes, bool estimate) const
{
    // The number unrounded: the words round it, as the viewer's language
    // writes a number.
    LLStringUtil::format_map_t args;
    args["[SIZE]"] = llformat("%f", (F64)bytes / 1024.0);
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
    return llformat(*change > 0 ? "+%lld" : "%lld", static_cast<long long>(*change));
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
    if (!weight)
    {
        mHead->setText(std::string());
        return;
    }

    // What it comes to, and what the numbers are of.
    LLStringUtil::format_map_t args;
    args["[NAME]"]   = mShown.name;
    args["[TARGET]"] = ALScriptWeight::nameOf(weight->target);
    args["[SIZE]"]   = llformat("%f", (F64)weight->total / 1024.0);
    args["[LIMIT]"]  = std::to_string(weight->limit / 1024);
    args["[SHARE]"]  = share(weight->total, weight->limit);
    args["[ERROR]"]  = weight->error;
    std::string head = mStrings->getString(weight->total == 0 ? "WeightsHeadFailed" : weight->estimate ? "WeightsHeadEstimate" : "WeightsHead", args);
    if (weight->total > 0 && !weight->error.empty())
    {
        head += " " + mStrings->getString("WeightsHeadError", args);
    }
    if (mShown.beforeOptimizer)
    {
        const bool own = !mShown.weights.empty() && weight->target == mShown.weights.front().target;
        if (own && mShown.sent)
        {
            args["[SENT]"] = llformat("%f", (F64)*mShown.sent / 1024.0);
            head += " " + mStrings->getString("WeightsHeadSent", args);
        }
        else
        {
            head += " " + mStrings->getString("WeightsHeadBefore");
        }
    }
    mHead->setText(head);
    mHead->setToolTip(mStrings->getString("WeightsHeadTip"));

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
        row["columns"][PART_NAME]["value"]    = partName(part);
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
    if (part.line < 0)
    {
        return std::string();
    }
    if (part.file.empty())
    {
        return std::to_string(part.line + 1);
    }
    const auto named = mShown.fileNames.find(part.file);
    return llformat("%s:%d", named != mShown.fileNames.end() ? named->second.c_str() : part.file.c_str(), part.line + 1);
}

std::optional<ALScriptWeightsPane::Place> ALScriptWeightsPane::chosenPlace() const
{
    const LLScrollListItem* item = mParts->getFirstSelected();
    if (!item)
    {
        return std::nullopt;
    }
    const S32 index = item->getValue().asInteger();
    if (index < 0 || static_cast<size_t>(index) >= mRows.size() || mRows[static_cast<size_t>(index)].part.line < 0)
    {
        return std::nullopt;
    }
    const ALScriptWeight::Part& part = mRows[static_cast<size_t>(index)].part;
    Place                       place;
    place.file   = part.file;
    place.line   = part.line;
    place.column = std::max(0, part.column);
    if (!part.file.empty())
    {
        const auto named = mShown.fileNames.find(part.file);
        place.fileName   = named != mShown.fileNames.end() ? named->second : part.file;
    }
    return place;
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
            said = LLStringUtil::compareDict(partName(x.part), partName(y.part));
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
