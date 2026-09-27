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

#include "lldate.h"
#include "llfocusmgr.h"

#include <algorithm>
#include <utility>

// static
const std::string ALScriptStudioDoc::GENERATED = "generated:";

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

bool ALScriptStudioDoc::hasKeyboard() const
{
    for (const LLView* view : { static_cast<const LLView*>(editor), static_cast<const LLView*>(expandedEditor), static_cast<const LLView*>(compareView) })
    {
        if (view && gFocusMgr.childHasKeyboardFocus(view))
        {
            return true;
        }
    }
    return false;
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

S32 ALScriptStudioDoc::runningCodeLine() const
{
    return runningMap() && envelope ? envelope->codeLine() : 0;
}

ALScriptStudioDoc::RunningPlace ALScriptStudioDoc::placeOfRunning(S32 line, S32 column) const
{
    RunningPlace where;
    where.line   = line;
    where.column = column;
    const ALSourceMap* map = line >= 0 ? runningMap() : nullptr;
    if (!map)
    {
        return where;
    }
    // Counted by the region with the envelope's lines above the code; the
    // map is of the code.
    const ALSourceMap::Loc loc = map->toSource(line - runningCodeLine(), llmax(0, column));
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
    else
    {
        where.line      = llmax(0, line - runningCodeLine());
        where.generated = true;
    }
    return where;
}

void ALScriptStudioDoc::heardRuntime(const RuntimeProblem& running, bool hold)
{
    if (hold)
    {
        runtimeHeld.push_back(running);
        return;
    }
    const RunningPlace where = placeOfRunning(running.line, running.column);
    RuntimeProblem     problem;
    problem.line    = where.line;
    problem.column  = where.column;
    problem.file    = where.generated ? GENERATED : where.file;
    problem.message = running.message;
    problem.count   = running.count;
    const auto same = std::find_if(runtime.begin(), runtime.end(), [&problem](const RuntimeProblem& one) {
        return one.line == problem.line && one.column == problem.column && one.file == problem.file && one.message == problem.message;
    });
    if (same != runtime.end())
    {
        same->count += problem.count;
        return;
    }
    runtime.push_back(std::move(problem));
    // A script failing many ways at once is failing: the oldest go past a
    // few dozen.
    constexpr size_t RUNTIME_PROBLEMS = 50;
    if (runtime.size() > RUNTIME_PROBLEMS)
    {
        runtime.erase(runtime.begin());
    }
}

void ALScriptStudioDoc::placeHeldRuntime()
{
    for (const RuntimeProblem& running : std::exchange(runtimeHeld, {}))
    {
        heardRuntime(running, false);
    }
}

ALScriptEnvelope ALScriptStudioDoc::envelopeFor(const std::string& expanded_text, const std::string& program) const
{
    // Every field its own: the last envelope's texts are not copied in only
    // to be written over.
    ALScriptEnvelope out;
    out.lua              = language.lua;
    out.source           = editor ? editor->text() : std::string();
    out.expanded         = expanded_text;
    out.compileTarget    = language.compileTarget;
    out.programVersion   = program;
    out.lastCompiled     = LLDate::now().asString();
    return out;
}

std::shared_ptr<const std::string> ALScriptStudioDoc::snapshot() const
{
    if (!editor)
    {
        return std::make_shared<const std::string>();
    }
    const U32                          version = editor->document().version();
    std::shared_ptr<const std::string> held    = snapshotHeld.lock();
    if (!held || snapshotVersion != version)
    {
        held            = std::make_shared<const std::string>(editor->wholeText());
        snapshotHeld    = held;
        snapshotVersion = version;
    }
    return held;
}

// static
std::optional<ALScriptStudioDoc::Named> ALScriptStudioDoc::namedIn(const ALTextDocument& text, const ALTextPos& at, bool lua,
                                                                   const std::vector<ALPreprocessor::Required>& calls)
{
    if (at.line < 0 || at.line >= text.lineCount())
    {
        return std::nullopt;
    }
    // #include "name" or #include <name>, blanks allowed about the #.
    const std::string& line = text.line(at.line);
    size_t             i    = line.find_first_not_of(" \t");
    if (i != std::string::npos && line[i] == '#')
    {
        i = line.find_first_not_of(" \t", i + 1);
        if (i != std::string::npos && line.compare(i, 7, "include") == 0)
        {
            i = line.find_first_not_of(" \t", i + 7);
            if (i != std::string::npos && (line[i] == '"' || line[i] == '<'))
            {
                const size_t close = line.find(line[i] == '"' ? '"' : '>', i + 1);
                if (close != std::string::npos && close > i + 1)
                {
                    Named named;
                    named.name  = line.substr(i + 1, close - i - 1);
                    named.range = ALTextRange(ALTextPos(at.line, 0), ALTextPos(at.line, static_cast<S32>(line.size())));
                    return named;
                }
            }
        }
    }
    if (!lua)
    {
        return std::nullopt;
    }
    for (const ALPreprocessor::Required& call : calls)
    {
        const ALTextRange stretch(ALTextPos(call.line, call.column), ALTextPos(call.endLine, call.endColumn));
        if (!(at < stretch.begin) && !(stretch.end < at))
        {
            Named named;
            named.name    = call.name;
            named.require = true;
            named.range   = stretch;
            return named;
        }
    }
    return std::nullopt;
}

std::optional<ALScriptStudioDoc::Named> ALScriptStudioDoc::namedAt(const ALTextPos& at) const
{
    if (!editor)
    {
        return std::nullopt;
    }
    const ALTextDocument& text = editor->document();
    if (language.lua && requiresOf != text.version())
    {
        requiresFound = ALPreprocessor::requiresIn(text.text());
        requiresOf    = text.version();
    }
    return namedIn(text, at, language.lua, requiresFound);
}

std::string ALScriptStudioDoc::foundAs(const std::string& name, std::optional<bool> require) const
{
    // The script's own asks, of the last expansion for the analyzers and
    // of the last save's run: an include's file is what either found.
    for (const Expanded* run : { &expanded, &uploaded })
    {
        if (!run->valid)
        {
            continue;
        }
        for (const ALPreprocessor::Result::Resolved& each : run->resolved)
        {
            if (each.from.empty() && each.name == name && (!require || each.require == *require))
            {
                return each.path;
            }
        }
    }
    return std::string();
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
