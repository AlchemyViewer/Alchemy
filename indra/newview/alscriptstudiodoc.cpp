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

// static
ALCodeEditor::Mark ALScriptStudioDoc::markOf(Level level)
{
    return level == Level::Error ? ALCodeEditor::Mark::Error : level == Level::Warning ? ALCodeEditor::Mark::Warning : ALCodeEditor::Mark::Note;
}
