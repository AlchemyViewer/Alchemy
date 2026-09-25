/**
 * @file alscriptstudiodoc.cpp
 * @brief One tab of Script Studio: a script, a notecard or a file, and everything the studio keeps about it.
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

#include "alscriptstudiodoc.h"

// static
const char* ALScriptStudioDoc::levelName(Level level)
{
    return level == Level::Error ? "ERROR" : level == Level::Warning ? "WARNING" : "NOTE";
}

// static
ALScriptStudioDoc::Level ALScriptStudioDoc::levelOf(const std::string& said)
{
    // The compiler's own words: anything but a warning is an error, as
    // the server has only the two.
    return said == "WARNING" || said == "WARN" ? Level::Warning : Level::Error;
}

// static
ALScriptStudioDoc::Level ALScriptStudioDoc::levelOf(ALScriptProblem::Severity severity)
{
    return severity == ALScriptProblem::Severity::Error     ? Level::Error
           : severity == ALScriptProblem::Severity::Warning ? Level::Warning
                                                            : Level::Note;
}

const ALSourceMap* ALScriptStudioDoc::runningMap() const
{
    // What the region compiled and runs is the expanded text that went up
    // from here, where one did; else -- a recompile, or a script loaded and
    // not saved since -- the text as it was last expanded, which is what
    // its envelope holds as far as this tab knows.
    if (save.sentMap())
    {
        return &*save.sentMap();
    }
    return uploaded.valid && !uploaded.disabled ? &uploaded.map : nullptr;
}

const ALScriptStudioDoc::Shown* ALScriptStudioDoc::findShown(S32 line, S32 column, const std::string& file, const std::string& message) const
{
    for (const Shown& one : shown)
    {
        if (one.line == line && one.column == column && one.file == file && one.message == message)
        {
            return &one;
        }
    }
    return nullptr;
}

std::vector<const ALScriptFix*> ALScriptStudioDoc::pickFixes(const FixPick& pick, size_t* left) const
{
    std::vector<const ALScriptFix*> taken;
    const U32                       now       = editor->document().version();
    const bool                      only_safe = pick.forSave || pick.key.empty();
    for (const Shown& one : shown)
    {
        if (!one.file.empty() || one.fixesFor != now || (!pick.key.empty() && one.key != pick.key))
        {
            continue;
        }
        for (const ALScriptFix& fix : one.fixes)
        {
            if (fix.preferred && fix.kind == ALScriptFix::Kind::Fix)
            {
                if (!only_safe || (fix.safe && !(pick.forSave && fix.removes)))
                {
                    taken.push_back(&fix);
                }
                else if (left)
                {
                    ++*left;
                }
                break;
            }
        }
    }
    // One step of edits that never meet: of two that would, the first.
    const auto begin_of = [](const ALScriptEdit& edit) { return ALTextPos(edit.line, edit.column); };
    const auto end_of   = [](const ALScriptEdit& edit) { return ALTextPos(edit.endLine, edit.endColumn); };
    std::vector<const ALScriptFix*> kept;
    std::vector<ALTextRange>        claimed;
    for (const ALScriptFix* fix : taken)
    {
        const bool meets = std::any_of(fix->edits.begin(), fix->edits.end(), [&](const ALScriptEdit& edit) {
            return std::any_of(claimed.begin(), claimed.end(), [&](const ALTextRange& range) {
                return (begin_of(edit) < range.end && range.begin < end_of(edit)) ||
                       (begin_of(edit) == end_of(edit) && range.begin == range.end && begin_of(edit) == range.begin);
            });
        });
        if (meets)
        {
            continue;
        }
        for (const ALScriptEdit& edit : fix->edits)
        {
            claimed.emplace_back(begin_of(edit), end_of(edit));
        }
        kept.push_back(fix);
    }
    return kept;
}

// static
ALCodeEditor::Mark ALScriptStudioDoc::markOf(Level level)
{
    return level == Level::Error ? ALCodeEditor::Mark::Error : level == Level::Warning ? ALCodeEditor::Mark::Warning : ALCodeEditor::Mark::Note;
}
