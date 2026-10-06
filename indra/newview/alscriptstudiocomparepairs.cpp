/**
 * @file alscriptstudiocomparepairs.cpp
 * @brief A comparison's texts lined up by their functions, paired by what each is.
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

#include "alscriptstudiocomparepairs.h"

#include "aldiffview.h"
#include "alscriptoutlinepairs.h"
#include "alscriptstudioservices.h"

ALScriptStudioComparePairs::ALScriptStudioComparePairs(ALScriptStudioServices& services, Window& window)
:   mServices(services),
    mWindow(window)
{
}

void ALScriptStudioComparePairs::follow(Doc& doc)
{
    if (!doc.compareView || doc.notecard || !doc.compareView->model().ranges().empty())
    {
        return;
    }
    const std::string* texts[2] = { &doc.compareView->leftText(), &doc.compareView->rightText() };
    for (size_t side = 0; side < 2; ++side)
    {
        Doc::ComparePairs::Side& known = doc.comparePairs->sides[side];
        if (known.asked && *known.asked == *texts[side])
        {
            continue;
        }
        known.asked = std::make_shared<const std::string>(*texts[side]);
        ALScriptAnalysis::Request request;
        request.kind    = ALScriptAnalysis::Kind::Shape;
        request.id      = doc.id + (side ? ":compare:right" : ":compare:left");
        request.version = ++known.version;
        request.lua     = doc.language.lua;
        request.text    = known.asked;
        request.front   = true;
        mWindow.askShape(std::move(request), [this, alive = std::weak_ptr<bool>(mAlive), id = doc.id, side, text = known.asked](
                                                 const ALScriptAnalysis::Result& result) {
            if (alive.lock())
            {
                answered(id, side, text, result.outline);
            }
        });
    }
    pairUp(doc);
}

void ALScriptStudioComparePairs::answered(const std::string& id, size_t side, const std::shared_ptr<const std::string>& text,
                                          std::vector<ALScriptOutlineEntry> outline)
{
    // Still a comparison, and that text still the one last asked of.
    Doc* doc = mServices.findDoc(id);
    if (!doc || !doc->compareView || doc->comparePairs->sides[side].asked != text)
    {
        return;
    }
    Doc::ComparePairs::Side& known = doc->comparePairs->sides[side];
    known.answered                 = text;
    known.outline                  = std::move(outline);
    pairUp(*doc);
}

void ALScriptStudioComparePairs::pairUp(Doc& doc)
{
    // Both known for the texts as the comparison shows them, and no ranges
    // come since.
    const std::string* texts[2] = { &doc.compareView->leftText(), &doc.compareView->rightText() };
    for (size_t side = 0; side < 2; ++side)
    {
        const Doc::ComparePairs::Side& known = doc.comparePairs->sides[side];
        if (!known.answered || *known.answered != *texts[side])
        {
            return;
        }
    }
    if (!doc.compareView->model().ranges().empty())
    {
        return;
    }
    ALTextDiff::ranges_t pairs;
    for (const ALScriptOutlinePairs::Pair& pair : ALScriptOutlinePairs::pair(doc.comparePairs->sides[0].outline, doc.comparePairs->sides[1].outline))
    {
        ALTextDiff::Range range;
        range.leftFirst  = pair.leftFirst;
        range.leftLast   = pair.leftLast;
        range.rightFirst = pair.rightFirst;
        range.rightLast  = pair.rightLast;
        pairs.push_back(std::move(range));
    }
    doc.compareView->setPairs(std::move(pairs));
}
