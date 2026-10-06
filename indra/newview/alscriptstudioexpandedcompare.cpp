/**
 * @file alscriptstudioexpandedcompare.cpp
 * @brief A Script Studio window's source compared with what a save sends of it, lined up by the preprocessor's map.
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

#include "alscriptstudioexpandedcompare.h"

#include "alcodeeditor.h"
#include "aldiffview.h"
#include "alscriptstudioservices.h"
#include "alsourcemap.h"

#include <functional>

ALScriptStudioExpandedCompare::ALScriptStudioExpandedCompare(ALScriptStudioServices& services, Window& window)
:   mServices(services),
    mWindow(window)
{
}

// static
ALTextDiff::ranges_t ALScriptStudioExpandedCompare::rangesOf(const ALSourceMap& map)
{
    ALTextDiff::ranges_t ranges;
    S32                  line  = -1;
    S32                  least = 0;
    S32                  most  = 0;
    const auto           flush = [&]() {
        if (line < 0)
        {
            return;
        }
        // The lines after one standing for the same source lines go with
        // it: a macro's lines, a block the optimizer wrote over several.
        if (!ranges.empty() && ranges.back().leftFirst == least && ranges.back().leftLast == most && ranges.back().rightLast == line - 1)
        {
            ranges.back().rightLast = line;
            return;
        }
        ranges.push_back({ least, most, line, line });
    };
    for (const ALSourceMap::Segment& segment : map.segments())
    {
        // The script's own; an include's lines stand for none of it.
        if (segment.file != 0)
        {
            continue;
        }
        if (segment.outLine != line)
        {
            flush();
            line  = segment.outLine;
            least = segment.line;
            most  = segment.line;
            continue;
        }
        least = llmin(least, segment.line);
        most  = llmax(most, segment.line);
    }
    flush();
    return ranges;
}

// static
bool ALScriptStudioExpandedCompare::canCompare(const Doc& doc)
{
    // A tab a save preprocesses has its expansion's view.
    return doc.loaded && doc.editor && doc.expandedEditor;
}

void ALScriptStudioExpandedCompare::compare(Doc& doc)
{
    if (!canCompare(doc))
    {
        return;
    }
    forgetClosed();
    if (doc.uploaded.valid && doc.uploaded.text && doc.uploaded.version == doc.editor->document().version())
    {
        mWaiting.erase(doc.id);
        show(doc);
        return;
    }
    mWaiting[doc.id] = shownOf(doc);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    mServices.setStatus(mServices.words("Preprocessing", args));
    mWindow.preprocess(doc);
}

// static
ALScriptStudioExpandedCompare::Shown ALScriptStudioExpandedCompare::shownOf(const Doc& doc)
{
    Shown shown;
    shown.view = doc.shownView();
    if (shown.view == Doc::View::Compare)
    {
        shown.left = std::hash<std::string>()(doc.compareView->model().leftText());
    }
    return shown;
}

void ALScriptStudioExpandedCompare::forgetClosed()
{
    boost::unordered::erase_if(mWaiting, [this](const auto& waiting) { return mServices.findDoc(waiting.first) == nullptr; });
}

void ALScriptStudioExpandedCompare::expanded(Doc& doc)
{
    forgetClosed();
    const auto waiting = mWaiting.find(doc.id);
    if (waiting == mWaiting.end())
    {
        return;
    }
    // Asked for in place of what the tab showed then: where it shows
    // something else now -- another comparison, or none -- the user has
    // moved on, and it is let go of rather than put over that.
    if (!canCompare(doc) || !(waiting->second == shownOf(doc)))
    {
        mWaiting.erase(waiting);
        return;
    }
    // Only where the run is of the text as it is. A run of an older text
    // -- one on its way when this was asked for, which kept it from
    // asking its own -- is followed by one of the text now.
    if (!doc.uploaded.valid || !doc.uploaded.text || doc.uploaded.version != doc.editor->document().version())
    {
        mWindow.preprocess(doc);
        return;
    }
    mWaiting.erase(waiting);
    show(doc);
}

void ALScriptStudioExpandedCompare::show(Doc& doc)
{
    const auto map = std::make_shared<const ALSourceMap>(doc.uploaded.map);
    mWindow.compareRanged(doc, doc.editor->wholeText(), *doc.uploaded.text, doc.name, mServices.words("ComparePreprocessed"), rangesOf(*map));
    if (!doc.compareView)
    {
        return;
    }
    // What is typed goes on in the source where the right's place came
    // from; nowhere where nothing of the source made it.
    const std::string id = doc.id;
    doc.compareView->setOnEdit([this, id, map](S32 line, S32 column) -> LLView* {
        Doc*                   found = mServices.findDoc(id);
        const ALSourceMap::Loc from  = map->toSource(line, column);
        if (!found || !found->loaded || !found->modifiable || found->editor->isReadOnly() || from.file != 0)
        {
            return nullptr;
        }
        return mWindow.typeInSource(*found, ALTextPos(from.line, from.column));
    });
}
