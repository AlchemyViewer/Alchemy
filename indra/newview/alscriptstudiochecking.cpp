/**
 * @file alscriptstudiochecking.cpp
 * @brief Script Studio's checking: the analyzers asked and answered, the expansion for them, imports offered, fixes made.
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

#include "alscriptstudiochecking.h"

#include "alnotecardembedded.h"
#include "alnotecardformat.h"
#include "alnotecarditems.h"
#include "alscriptfixes.h"
#include "alscriptstudioanalysis.h"
#include "alscriptstudioplaces.h"
#include "alscriptstudiosaves.h"
#include "alscriptstudioservices.h"
#include "alscriptstudioweighing.h"
#include "alscriptstudiowords.h"
#include "lldir.h"
#include "lltimer.h"

#include <algorithm>

namespace
{
    // How long after the last keystroke the analyzers are asked.
    const F64 ANALYSIS_DELAY = 0.35;
    // What makes an include's functions and globals a script to the
    // parser: a state after them. Put after the text, so that every place
    // in it is where it was; what is said of it is dropped.
    const char FRAGMENT_STATE[] = "\ndefault{state_entry(){}}\n";

    ALSyntaxKind syntaxKindOf(ALScriptSymbolKind kind)
    {
        switch (kind)
        {
            case ALScriptSymbolKind::Keyword:   return ALSyntaxKind::Keyword;
            case ALScriptSymbolKind::Variable:  return ALSyntaxKind::Variable;
            case ALScriptSymbolKind::Parameter: return ALSyntaxKind::Parameter;
            case ALScriptSymbolKind::Function:  return ALSyntaxKind::Function;
            case ALScriptSymbolKind::Field:     return ALSyntaxKind::Property;
            case ALScriptSymbolKind::Type:      return ALSyntaxKind::Type;
            case ALScriptSymbolKind::Constant:  return ALSyntaxKind::Constant;
            case ALScriptSymbolKind::Event:     return ALSyntaxKind::Event;
            case ALScriptSymbolKind::State:     return ALSyntaxKind::State;
            case ALScriptSymbolKind::Label:     return ALSyntaxKind::Label;
            case ALScriptSymbolKind::Module:    return ALSyntaxKind::Namespace;
            default:                            return ALSyntaxKind::Text;
        }
    }

    // Each parameter's place in the label, found in order.
    std::vector<std::pair<S32, S32>> spansIn(const std::string& label, const std::vector<std::string>& parameters)
    {
        std::vector<std::pair<S32, S32>> spans;
        size_t                           from = 0;
        for (const std::string& parameter : parameters)
        {
            const size_t at = parameter.empty() ? std::string::npos : label.find(parameter, from);
            if (at == std::string::npos)
            {
                spans.emplace_back(0, 0);
                continue;
            }
            spans.emplace_back(static_cast<S32>(at), static_cast<S32>(at + parameter.size()));
            from = at + parameter.size();
        }
        return spans;
    }

    // A warning that something declared is never used: LSL's, by its
    // number, and Luau's lints, by their names.
    bool unusedWarning(const ALScriptProblem& problem)
    {
        if (problem.severity != ALScriptProblem::Severity::Warning)
        {
            return false;
        }
        const std::string& code = problem.code;
        return code == "20009" || code == "LocalUnused" || code == "FunctionUnused" || code == "ImportUnused";
    }
}

using ALScriptPlaces::Declared;
using ALScriptPlaces::declaredOf;
using ALScriptPlaces::mapSpan;
using ALScriptPlaces::rangeOf;

// static
ALScriptStudioChecking::Sources& ALScriptStudioChecking::sources()
{
    static Sources sources;
    return sources;
}

ALScriptStudioChecking::ALScriptStudioChecking(ALScriptStudioServices& services, ALScriptStudioAnalysis& analysis, ALScriptStudioSaves& saves, Window& window) : mServices(services), mAnalysis(analysis), mSaves(saves), mWindow(window) {}

bool ALScriptStudioChecking::preprocessed(const Doc& doc) const
{
    return doc.loaded && !doc.notecard && (doc.envelope.has_value() || (sources().preprocessing && sources().preprocessing()));
}

ALScriptPreprocessor::Request ALScriptStudioChecking::preprocessRequest(const Doc& doc, bool with_source) const
{
    ALScriptPreprocessor::Request request;
    request.ref     = doc.ref;
    request.path    = doc.file.empty() ? std::string() : "disk:" + doc.file;
    request.name    = doc.name;
    request.assetId = doc.assetId;
    if (with_source)
    {
        request.source = doc.snapshot();
    }
    request.lua     = doc.language.lua;
    request.compileTarget = doc.language.compileTarget;
    // The optimizer's notes are read here: each says what it saved in code.
    request.weigh   = true;
    return request;
}

void ALScriptStudioChecking::expandFor(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at, const ALTextPos& to)
{
    // The question waits for the text it is about: one of its kind that
    // was already waiting is somewhere the caret or the mouse has since
    // left.
    const auto same = std::find_if(doc.check->waiting.begin(), doc.check->waiting.end(),
                                   [kind](const Doc::Check::Waiting& was) { return was.kind == kind; });
    if (same != doc.check->waiting.end())
    {
        same->at = at;
        same->to = to;
    }
    else
    {
        doc.check->waiting.push_back(Doc::Check::Waiting{ kind, at, to });
    }
    const U32 version = doc.editor->document().version();
    if (doc.check->expanding)
    {
        // One at a time: every question waiting takes the one on its way,
        // or, asked about a later text, the next, made once it answers --
        // not one a key while the text is typed.
        if (*doc.check->expanding != version)
        {
            doc.check->wanted = version;
        }
        return;
    }
    doc.check->wanted.reset();
    doc.check->expanding            = version;
    const std::weak_ptr<bool> alive = mAlive;
    const std::string         id    = doc.id;
    // The script apart from its modules, for the analyzers to check each on
    // its own.
    ALScriptPreprocessor::Request asked = preprocessRequest(doc);
    asked.apart                         = true;
    sources().expand(asked, [this, alive, id, version](const ALPreprocessor::Result& result) {
        if (alive.lock())
        {
            expandedAnswer(id, version, result);
        }
    });
}

void ALScriptStudioChecking::expandedAnswer(const std::string& id, U32 version, const ALPreprocessor::Result& result)
{
    Doc* found = mServices.findDoc(id);
    if (!found)
    {
        return;
    }
    Doc& doc = *found;
    if (doc.check->expanding && *doc.check->expanding == version)
    {
        doc.check->expanding.reset();
    }
    if (version != doc.editor->document().version())
    {
        // The text has moved on. Questions asked about it as it is now
        // take one expansion of it, the next; any others were asked about
        // the text as it was -- a hover over a word the edit may have
        // moved -- and the edit has scheduled a check of its own, so they
        // go rather than being asked of the wrong text.
        const std::optional<U32> wanted = std::exchange(doc.check->wanted, std::nullopt);
        if (wanted && *wanted == doc.editor->document().version() && !doc.check->waiting.empty())
        {
            std::vector<Doc::Check::Waiting> waiting;
            waiting.swap(doc.check->waiting);
            for (const Doc::Check::Waiting& question : waiting)
            {
                expandFor(doc, question.kind, question.at, question.to);
            }
            return;
        }
        doc.check->waiting.clear();
        return;
    }
    doc.expanded.valid      = true;
    doc.expanded.disabled   = result.disabled;
    doc.expanded.version    = version;
    doc.expanded.generation = ++doc.check->expansions;
    doc.expanded.modules.reset();
    doc.expanded.moduleMaps.clear();
    doc.expanded.bundle.reset();
    if (result.apart.valid)
    {
        // The script alone, its requires calls, and the modules they reach,
        // each the checker's own; the bundle kept to weigh.
        doc.expanded.text   = std::make_shared<const std::string>(result.apart.script.text);
        doc.expanded.map    = result.apart.script.map;
        doc.expanded.bundle = std::make_shared<const std::string>(result.text);
        auto modules        = std::make_shared<ALLuauService::Modules>();
        for (const ALPreprocessor::Result::Piece& piece : result.apart.modules)
        {
            modules->modules.push_back({ piece.key, piece.text });
            doc.expanded.moduleMaps.emplace_back(piece.key, piece.map);
        }
        for (const ALPreprocessor::Result::Resolved& resolved : result.resolved)
        {
            if (resolved.require)
            {
                modules->reaches.push_back({ resolved.from, resolved.name, resolved.path });
            }
        }
        doc.expanded.modules = std::move(modules);
    }
    else
    {
        doc.expanded.text = std::make_shared<const std::string>(result.text);
        doc.expanded.map  = result.map;
    }
    doc.expanded.elsewhere = doc.expanded.map.othersLines();
    doc.expanded.problems = result.problems;
    doc.expanded.resolved = result.resolved;
    doc.expanded.consts   = result.consts;
    // What the preprocessor found is shown with what the analyzers found.
    mAnalysis.refreshProblems(doc);
    std::vector<Doc::Check::Waiting> waiting;
    waiting.swap(doc.check->waiting);
    for (const Doc::Check::Waiting& question : waiting)
    {
        ask(doc, question.kind, question.at, question.to);
    }
}

std::string ALScriptStudioChecking::includeName(const Doc& doc, const std::string& path) const
{
    for (const Doc::Expanded* expanded : { &doc.expanded, &doc.uploaded })
    {
        const S32 file = expanded->valid ? expanded->map.fileOf(path) : -1;
        if (file >= 0)
        {
            return expanded->map.files()[file].name;
        }
    }
    std::string file;
    return ALScriptPreprocessor::fileOf(path, file) ? gDirUtilp->getBaseFileName(file) : path;
}

void ALScriptStudioChecking::ask(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at, const ALTextPos& to)
{
    if (kind == ALScriptAnalysis::Kind::Actions)
    {
        // The stretch the refactors are offered over once they come.
        doc.check->actionsAsked = ALTextRange(at, to);
    }
    if (!doc.loaded || doc.notecard)
    {
        return;
    }
    // The tip and the inspector ask the same question: the one asking of a
    // word the other was told of, as the text stands, is told the same.
    const auto read_now = [this, &doc](U32 expansion) {
        return (expansion != 0) == preprocessed(doc) && (expansion == 0 || (doc.expanded.valid && doc.expanded.generation == expansion));
    };
    if ((kind == ALScriptAnalysis::Kind::Hover || kind == ALScriptAnalysis::Kind::Inspect) && doc.check->hovered &&
        doc.check->hovered->version == doc.editor->document().version() && read_now(doc.check->hovered->expansion) &&
        doc.check->hovered->word == doc.editor->identifierAt(at).begin)
    {
        ALScriptAnalysis::Result said = doc.check->hovered->said;
        said.kind                     = kind;
        if (kind == ALScriptAnalysis::Kind::Hover)
        {
            showHover(doc, said, at);
        }
        else
        {
            mWindow.answeredElsewhere(doc, said, at);
        }
        return;
    }
    ALScriptAnalysis::Request request;
    request.kind    = kind;
    request.id      = doc.id;
    request.version = doc.editor->document().version();
    request.lua     = doc.language.lua;
    request.mono    = doc.language.compileTarget != "lsl2";
    request.text    = doc.snapshot();
    request.line    = at.line;
    request.column  = at.column;
    request.endLine   = to.line;
    request.endColumn = to.column;
    mWindow.askingOptions(request);
    if (doc.language.lua)
    {
        // What the script's `.luaurc` says, where it has one; one not in
        // hand yet is fetched, and the check made again when it is.
        // Over the scripter's own choice of lints and mode, which the
        // file overrides key by key; that choice alone where there is none.
        const ALLuauConfig                  base  = ALScriptLints::luauBase();
        const ALScriptPreprocessor::Request root  = preprocessRequest(doc, /*with_source*/ false);
        const bool                          found = sources().configOf(root, request.config, &base);
        if (!found)
        {
            request.config = base;
        }
        if (!found && kind == ALScriptAnalysis::Kind::Check && !doc.check->configAsked)
        {
            doc.check->configAsked          = true;
            const std::weak_ptr<bool> alive = mAlive;
            const std::string         id    = doc.id;
            sources().fetchConfig(root, [this, alive, id]() {
                if (Doc* asked = alive.lock() ? mServices.findDoc(id) : nullptr)
                {
                    schedule(*asked, true);
                }
            });
        }
    }
    U32 expansion = 0;
    if (preprocessed(doc))
    {
        if (!doc.expanded.valid || doc.expanded.version != request.version)
        {
            // The analyzers see what the compiler would, and expanding a
            // script is a thread's work: the question waits for it.
            expandFor(doc, kind, at, to);
            return;
        }
        // A position inside a directive has nothing there to ask about.
        expansion    = doc.expanded.generation;
        request.text       = doc.expanded.text;
        request.passedOver = doc.expanded.elsewhere;
        request.modules    = doc.expanded.modules;
        request.bundle     = doc.expanded.bundle;
        if (kind != ALScriptAnalysis::Kind::Check && kind != ALScriptAnalysis::Kind::Weigh)
        {
            const ALSourceMap::Loc loc = doc.expanded.map.toExpanded(0, at.line, at.column);
            if (!loc.found())
            {
                return;
            }
            request.line   = loc.line;
            request.column = loc.column;
            // A stretch the expansion does not carry as it stands is asked
            // about as the caret alone.
            ALSourceMap::Loc from, end;
            const ALSourceMap::Loc last = doc.expanded.map.toExpanded(0, to.line, to.column);
            const bool             kept = last.found() && last.line == loc.line && last.column > loc.column &&
                                          doc.expanded.map.verbatimSpan(loc.line, loc.column, last.column, from, end) && from.file == 0 &&
                                          from.line == at.line && from.column == at.column && end.line == to.line &&
                                          end.column == to.column;
            request.endLine   = kept ? last.line : loc.line;
            request.endColumn = kept ? last.column : loc.column;
        }
    }
    if (kind == ALScriptAnalysis::Kind::Check && request.front)
    {
        // The tab in front weighed with its check, of the same text, in
        // the same job; the rest when they come to the front, or at a save.
        request.targets = mWindow.weightTargets(doc);
        if (!request.targets.empty())
        {
            ALScriptStudioWeighing::askedWithCheck(doc, request.version);
        }
    }
    if (kind == ALScriptAnalysis::Kind::Weigh)
    {
        request.targets = mWindow.weightTargets(doc);
        if (request.targets.empty())
        {
            return;
        }
    }
    else if (lslFragment(doc))
    {
        request.text = std::make_shared<const std::string>(*request.text + FRAGMENT_STATE);
    }
    const std::weak_ptr<bool> alive = mAlive;
    mAnalysis.askAnalysis(std::move(request), [this, alive, expansion](const ALScriptAnalysis::Result& result) {
        if (alive.lock())
        {
            answered(result, expansion);
        }
    });
}

void ALScriptStudioChecking::answered(const ALScriptAnalysis::Result& result, U32 expansion)
{
    Doc* found = mServices.findDoc(result.id);
    if (!found)
    {
        return;
    }
    Doc&      doc = *found;
    ALTextPos at(result.line, result.column);
    // Of the text as it is read now, or of nothing: an answer about an
    // expansion since dropped or replaced -- the includes came in, a
    // setting changed -- is in places no longer read that way, and one
    // about the plain text of a script now expanded, or the other way
    // round, likewise. Everything below reads the answer through what the
    // script is now. A check is asked again; the rest were about a moment
    // that has gone.
    const bool read_expanded = preprocessed(doc);
    if ((expansion != 0) != read_expanded || (expansion != 0 && (!doc.expanded.valid || doc.expanded.generation != expansion)))
    {
        if (result.kind == ALScriptAnalysis::Kind::Check && result.version == doc.editor->document().version())
        {
            schedule(doc, true);
        }
        return;
    }
    if (expansion != 0 && result.kind != ALScriptAnalysis::Kind::Check && result.kind != ALScriptAnalysis::Kind::Weigh)
    {
        // Answered about the expanded text; the editor wants the source's
        // place, which is where it asked.
        const ALSourceMap::Loc loc = doc.expanded.map.toSource(result.line, result.column);
        if (!loc.found() || loc.file != 0)
        {
            return;
        }
        at = ALTextPos(loc.line, loc.column);
    }
    // A name declared const, which the analyzers read with the word taken
    // off, said as the author declared it.
    std::optional<ALScriptAnalysis::Result> marked;
    if (expansion != 0 && (result.kind == ALScriptAnalysis::Kind::Hover || result.kind == ALScriptAnalysis::Kind::Inspect) && result.hover.found &&
        result.hover.hasDefinition)
    {
        for (const ALPreprocessor::Result::Const& declared : doc.expanded.consts)
        {
            if (declared.line == result.hover.definitionLine && declared.column == result.hover.definitionColumn)
            {
                marked.emplace(result);
                marked->hover.label = "const " + marked->hover.label;
                break;
            }
        }
    }
    const ALScriptAnalysis::Result& shown = marked ? *marked : result;
    // Kept for the other of the two to ask about the same word.
    if ((result.kind == ALScriptAnalysis::Kind::Hover || result.kind == ALScriptAnalysis::Kind::Inspect) &&
        result.version == doc.editor->document().version())
    {
        doc.check->hovered = Doc::Check::Hovered{ result.version, expansion, doc.editor->identifierAt(at).begin, shown };
    }
    switch (result.kind)
    {
        case ALScriptAnalysis::Kind::Check:
            analysed(result);
            break;
        case ALScriptAnalysis::Kind::Complete:
        {
            std::vector<ALCodeEditor::Completion> more;
            more.reserve(result.completions.size());
            for (const ALScriptCompletion& c : result.completions)
            {
                ALCodeEditor::Completion completion;
                completion.text          = c.text;
                completion.detail        = c.detail;
                completion.kind          = syntaxKindOf(c.kind);
                completion.deprecated    = c.deprecated;
                completion.documentation = ALCompletion::shared(c.documentation);
                more.push_back(std::move(completion));
            }
            doc.editor->supplyCompletions(at, std::move(more));
            break;
        }
        case ALScriptAnalysis::Kind::Hover:
            showHover(doc, shown, at);
            break;
        case ALScriptAnalysis::Kind::Signature:
        {
            if (!result.signature.found)
            {
                doc.editor->hideSignature();
                break;
            }
            ALCodeEditor::Signature signature;
            signature.label         = result.signature.label;
            signature.parameters    = spansIn(signature.label, result.signature.parameters);
            signature.active        = result.signature.active;
            signature.documentation = result.signature.documentation;
            for (const ALScriptSignature::Overload& form : result.signature.overloads)
            {
                signature.overloads.push_back({ form.label, spansIn(form.label, form.parameters) });
            }
            signature.overload = result.signature.overload;
            doc.editor->showSignature(at, std::move(signature));
            break;
        }
        case ALScriptAnalysis::Kind::Actions:
            actionsAnswered(doc, result, expansion);
            break;
        case ALScriptAnalysis::Kind::References:
        case ALScriptAnalysis::Kind::Inspect:
        case ALScriptAnalysis::Kind::Weigh:
            mWindow.answeredElsewhere(doc, shown, at);
            break;
    }
}

void ALScriptStudioChecking::showHover(Doc& doc, const ALScriptAnalysis::Result& said, const ALTextPos& at)
{
    if (!said.hover.found)
    {
        // Nothing: the editor says what the definitions say, if
        // anything.
        doc.editor->supplyHover(at, std::string());
        return;
    }
    std::string text = said.hover.label;
    // Where it was declared, as the inspector says it: a link there.
    std::vector<ALCodeEditor::CardLink> links;
    const Declared                      declared = declaredOf(doc, said, preprocessed(doc));
    if (declared.line >= 0)
    {
        LLStringUtil::format_map_t args;
        args["[LINE]"]          = std::to_string(declared.line + 1);
        args["[FILE]"]          = declared.name;
        const std::string where = mServices.words(declared.path.empty() ? "InspectDeclared" : "InspectDeclaredIn", args);
        text += "\n" + where;
        links.push_back({ where, mServices.words("InspectDeclaredTip"), declared.value() });
    }
    if (!said.hover.expected.empty())
    {
        LLStringUtil::format_map_t args;
        args["[TYPE]"] = said.hover.expected;
        text += "\n" + mServices.words("HoverExpected", args);
    }
    if (!said.hover.documentation.empty())
    {
        text += "\n" + said.hover.documentation;
    }
    else if (!said.hover.hasDefinition)
    {
        // A builtin the analyzer knows by its declaration alone --
        // LSL's: what the definitions say of it, its delay, its
        // god mode.
        const ALTextRange word = doc.editor->identifierAt(at);
        if (const ALScriptStudioWords::Vocab* known = ALScriptStudioWords::word(doc.language.lua, doc.editor->document().text(word)))
        {
            const std::string notes = ALScriptStudioWords::notesOf(*known);
            if (!notes.empty())
            {
                text += "\n" + notes;
            }
        }
    }
    if (!said.hover.link.empty())
    {
        text += "\n" + said.hover.link;
    }
    doc.editor->supplyHover(at, text, std::move(links));
}

void ALScriptStudioChecking::actionsAnswered(Doc& doc, const ALScriptAnalysis::Result& result, U32 expansion)
{
    if (result.version != doc.editor->document().version())
    {
        return;
    }
    // In the source's places: through the expansion where there is one,
    // each kept only where all of it lands in the script's own text.
    ALScriptProblem held;
    held.fixes = result.actions;
    if (expansion != 0)
    {
        ALScriptFixes::mapThrough(doc.expanded.map, held);
    }
    // Nor what lands past the script's end: a fragment is asked about with
    // a state of the studio's own after it.
    const ALTextDocument& text = doc.editor->document();
    std::erase_if(held.fixes, [&text](const ALScriptFix& fix) {
        return std::any_of(fix.edits.begin(), fix.edits.end(), [&text](const ALScriptEdit& edit) {
            const ALTextPos begin(edit.line, edit.column), end(edit.endLine, edit.endColumn);
            return text.clamp(begin) != begin || text.clamp(end) != end;
        });
    });
    doc.check->actions        = std::move(held.fixes);
    doc.check->actionsVersion = result.version;
    std::vector<ALCodeEditor::Fix> offered;
    for (size_t i = 0; i < doc.check->actions.size(); ++i)
    {
        const ALScriptFix& action = doc.check->actions[i];
        ALCodeEditor::Fix  one;
        one.title    = action.title;
        one.refactor = true;
        for (const ALScriptEdit& edit : action.edits)
        {
            one.edits.emplace_back(ALTextRange(ALTextPos(edit.line, edit.column), ALTextPos(edit.endLine, edit.endColumn)), edit.text);
        }
        one.value["doc"]    = doc.id;
        one.value["action"] = static_cast<S32>(i);
        offered.push_back(std::move(one));
    }
    doc.editor->supplyActions(doc.check->actionsAsked, std::move(offered));
}

bool ALScriptStudioChecking::lslFragment(const Doc& doc) const
{
    if (doc.file.empty() || doc.language.lua || doc.notecard)
    {
        return false;
    }
    // Walked once for each text it is asked of.
    ALCodeEditor& editor  = *doc.editor;
    const U32     version = editor.document().version();
    if (doc.check->fragment && doc.check->fragment->first == version)
    {
        return doc.check->fragment->second;
    }
    // A default state, by the grammar's tokens: `default` then `{`, past
    // blanks and comments, the brace on the same line or a later one.
    const auto has_default = [&editor]() {
        bool      waiting = false;
        const S32 lines   = editor.document().lineCount();
        for (S32 line = 0; line < lines; ++line)
        {
            const std::string& text = editor.document().line(line);
            for (const ALSyntaxToken& token : editor.highlighter().tokens(line))
            {
                const std::string_view word = std::string_view(text).substr(token.begin, token.end - token.begin);
                if (token.kind == ALSyntaxKind::Comment || token.kind == ALSyntaxKind::DocComment ||
                    word.find_first_not_of(" \t") == std::string_view::npos)
                {
                    continue;
                }
                if (waiting && word.front() == '{')
                {
                    return true;
                }
                waiting = token.kind == ALSyntaxKind::Control && word == "default";
            }
        }
        return false;
    };
    doc.check->fragment = std::make_pair(version, !has_default());
    return doc.check->fragment->second;
}

void ALScriptStudioChecking::schedule(Doc& doc, bool now)
{
    // A file on disk is checked as what it is: a Lua module, or an LSL
    // script, or an LSL include, which is checked with a state put after
    // it (lslFragment); a notecard in the world as scripts read it; any
    // other text not at all.
    if (!doc.loaded || (doc.notecard && !doc.itemNotecard()))
    {
        return;
    }
    doc.check->analysisDue = now ? 1.0 : static_cast<F64>(LLTimer::getTotalSeconds()) + ANALYSIS_DELAY;
}

void ALScriptStudioChecking::settingsChanged(bool words, F64 now)
{
    mPreprocessorDue   = now + ANALYSIS_DELAY;
    mPreprocessorWords = mPreprocessorWords || words;
}

void ALScriptStudioChecking::pump(F64 now)
{
    // The preprocessor's settings changed a moment ago: what the analyzers
    // see changes with them, and what the editors colour as the
    // transforms' words.
    if (mPreprocessorDue > 0.0 && now >= mPreprocessorDue)
    {
        const bool words   = mPreprocessorWords;
        mPreprocessorDue   = 0.0;
        mPreprocessorWords = false;
        for (Doc* doc : mServices.openDocs())
        {
            doc->expanded.valid = false;
            // What a run made to be sent is made differently now: the check
            // weighs the text again, and the run that follows as it is sent.
            ALScriptStudioWeighing::sendsDifferently(*doc);
            if (words && !doc->notecard && !doc->language.lua)
            {
                ALScriptStudioWords::teach(*doc->editor, false);
            }
            if (preprocessed(*doc))
            {
                mSaves.preprocess(*doc);
            }
            schedule(*doc, true);
        }
    }
    // The tab in front as soon as it is due; the others one a frame, the
    // longest due first, so that many tabs opened or restored at once are
    // not as many whole-text checks asked in one frame.
    const Doc* front = mServices.frontDoc();
    Doc*       next  = nullptr;
    for (Doc* doc : mServices.openDocs())
    {
        if (doc->check->analysisDue <= 0.0 || now < doc->check->analysisDue)
        {
            continue;
        }
        // Quick, and nothing to wait on.
        if (doc->itemNotecard())
        {
            checkNotecard(*doc);
            continue;
        }
        if (doc == front)
        {
            askCheck(*doc, now);
        }
        else if (!next || doc->check->analysisDue < next->check->analysisDue)
        {
            next = doc;
        }
    }
    if (next)
    {
        askCheck(*next, now);
    }
}

void ALScriptStudioChecking::askCheck(Doc& doc, F64 now)
{
    doc.check->analysisDue      = 0.0;
    doc.check->requestedVersion = doc.editor->document().version();
    doc.check->askedAt          = now;
    ask(doc, ALScriptAnalysis::Kind::Check, ALTextPos(), ALTextPos());
}

void ALScriptStudioChecking::checkNotecard(Doc& doc)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    doc.check->analysisDue          = 0.0;
    const ALTextDocument& text      = doc.editor->document();
    ALScriptProblems      problems;
    // Every line is EOF to a script where the notecard carries anything:
    // said once, at the first item.
    if (doc.items && !doc.items->items().empty())
    {
        for (S32 line = 0; line < text.lineCount() && problems.empty(); ++line)
        {
            ALNotecardItems::forEach(text.line(line), [&](size_t column, size_t) {
                if (!problems.empty())
                {
                    return;
                }
                ALScriptProblem problem;
                problem.severity  = ALScriptProblem::Severity::Warning;
                problem.source    = ALScriptProblem::Source::Lint;
                problem.line      = problem.endLine = line;
                problem.column    = static_cast<S32>(column);
                problem.endColumn = static_cast<S32>(column + ALNotecardItems::CHAR_BYTES);
                problem.message   = mServices.words("NotecardItemsUnread");
                problems.push_back(std::move(problem));
            });
        }
    }
    // What a script is given of a long line: its first so many bytes.
    for (const S32 line : ALNotecardFormat::linesPast(doc.editor->wholeText()))
    {
        const S32                  length = static_cast<S32>(text.line(line).size());
        LLStringUtil::format_map_t args;
        args["[BYTES]"] = std::to_string(ALNotecardFormat::READ_LINE_BYTES);
        args["[SIZE]"]  = std::to_string(length);
        ALScriptProblem problem;
        problem.severity  = ALScriptProblem::Severity::Warning;
        problem.source    = ALScriptProblem::Source::Lint;
        problem.line      = problem.endLine = line;
        problem.column    = static_cast<S32>(ALNotecardFormat::READ_LINE_BYTES);
        problem.endColumn = length;
        problem.message   = mServices.words("NotecardLineCut", args);
        problems.push_back(std::move(problem));
    }
    doc.check->analysis        = std::move(problems);
    doc.check->analysisVersion = text.version();
    doc.outline = doc.grammar == "json" ? ALNotecardFormat::outline(doc.editor->wholeText()) : std::vector<ALScriptOutlineEntry>();
    mAnalysis.refreshProblems(doc);
    mWindow.showOutline(doc);
}

void ALScriptStudioChecking::analysed(const ALScriptAnalysis::Result& result)
{
    Doc* found = mServices.findDoc(result.id);
    if (!found)
    {
        return;
    }
    Doc& doc = *found;
    // Of a text that has moved on: the check of the newer text follows.
    if (result.version != doc.editor->document().version())
    {
        return;
    }
    takeProblems(doc, result);
    // What every name is and what goes beside the text, in the source's
    // places; what stands in an include is the include's.
    const bool mapped = preprocessed(doc) && doc.expanded.valid && doc.expanded.version == result.version;
    takeColours(doc, result, mapped ? &doc.expanded.map : nullptr);
    if (mapped)
    {
        mapProblems(doc);
        mapOutline(doc);
    }
    // What the check weighed, where it was asked to.
    if (!result.weights.empty())
    {
        mWindow.weighed(doc, result);
    }
    checked(doc);
}

void ALScriptStudioChecking::takeProblems(Doc& doc, const ALScriptAnalysis::Result& result)
{
    doc.check->analysis        = result.problems;
    doc.check->analysisVersion = result.version;
    // Kept as said, for the lints chosen again to filter afresh.
    if (!doc.language.lua)
    {
        doc.check->unfiltered = result.problems;
    }
    filterProblems(doc);
    doc.check->definitionsError = result.definitionsError;
    // A script mid-edit is answered from a copy mended to parse; one past
    // mending answers nothing, and what it declares, what its names are
    // and what goes beside them are then not nothing but what they last
    // were: the outline, the breadcrumb, the colours and the hints keep
    // what they knew -- slid along by the edits since -- until it is
    // understood again.
    if (result.understood)
    {
        doc.outline = result.outline;
    }
    // An include checked with a state after it: what it declares, and
    // not the state put after it.
    if (!lslFragment(doc) || !result.understood)
    {
        return;
    }
    const S32 own_lines = fragmentLines(doc, result.version);
    doc.outline.erase(std::remove_if(doc.outline.begin(), doc.outline.end(),
                                     [own_lines](const ALScriptOutlineEntry& entry) { return entry.nameSpan.line >= own_lines; }),
                      doc.outline.end());
}

S32 ALScriptStudioChecking::fragmentLines(const Doc& doc, U32 version) const
{
    const bool mapped_now = preprocessed(doc) && doc.expanded.valid && doc.expanded.version == version;
    return mapped_now ? static_cast<S32>(std::count(doc.expanded.text->begin(), doc.expanded.text->end(), '\n')) + 1
                      : doc.editor->document().lineCount();
}

void ALScriptStudioChecking::filterProblems(Doc& doc)
{
    // LSL's warnings as the scripter chose them; Luau's lints were chosen
    // in the configuration the check ran with.
    if (!doc.language.lua)
    {
        ALScriptLints::apply(doc.check->analysis);
    }
    // An include checked with a state after it: what is said of the
    // state, and that what it declares goes unused, is not the include's.
    if (lslFragment(doc))
    {
        const S32 own_lines = fragmentLines(doc, doc.check->analysisVersion);
        doc.check->analysis.erase(std::remove_if(doc.check->analysis.begin(), doc.check->analysis.end(),
                                                [own_lines](const ALScriptProblem& problem) {
                                                    return problem.line >= own_lines || unusedWarning(problem);
                                                }),
                                 doc.check->analysis.end());
    }
}

void ALScriptStudioChecking::relint(Doc& doc)
{
    const U32 version = doc.editor->document().version();
    if (doc.language.lua || !doc.loaded || doc.notecard || doc.check->analysisVersion != version)
    {
        schedule(doc, true);
        return;
    }
    doc.check->analysis = doc.check->unfiltered;
    filterProblems(doc);
    if (preprocessed(doc) && doc.expanded.valid && doc.expanded.version == version)
    {
        mapProblems(doc);
    }
    showProblems(doc);
}

void ALScriptStudioChecking::takeColours(Doc& doc, const ALScriptAnalysis::Result& result, const ALSourceMap* map)
{
    // A script past mending keeps the colours and hints it had.
    if (!result.understood)
    {
        return;
    }
    std::vector<ALCodeEditor::SemanticToken> semantics;
    semantics.reserve(result.semantics.size());
    for (ALScriptSemanticToken token : result.semantics)
    {
        if (map && mapSpan(*map, token.span) != 0)
        {
            continue;
        }
        ALCodeEditor::SemanticToken one;
        one.range  = rangeOf(token.span);
        one.kind   = syntaxKindOf(token.kind);
        one.strike = (token.modifiers & ALScriptSemanticToken::Deprecated) != 0;
        if (one.kind == ALSyntaxKind::Text)
        {
            continue;
        }
        if (one.strike)
        {
            one.kind = ALSyntaxKind::Deprecated;
        }
        else if (token.kind == ALScriptSymbolKind::Variable && (token.modifiers & ALScriptSemanticToken::ReadOnly))
        {
            one.kind = ALSyntaxKind::Constant;
        }
        else if (token.kind == ALScriptSymbolKind::Variable && (token.modifiers & ALScriptSemanticToken::Global))
        {
            // A name of the whole script apart from a block's: what an
            // assignment far from its declaration is most often about.
            one.kind = ALSyntaxKind::GlobalVariable;
        }
        semantics.push_back(std::move(one));
    }
    doc.editor->setSemanticTokens(std::move(semantics));
    std::vector<ALCodeEditor::InlayHint> hints;
    hints.reserve(result.hints.size());
    for (const ALScriptInlayHint& hint : result.hints)
    {
        ALTextPos at(hint.line, hint.column);
        // Written in only where the place is the script's own text as it
        // stands, not a macro's making.
        bool writable = hint.writable;
        if (map)
        {
            const ALSourceMap::Loc loc = map->toSource(hint.line, hint.column);
            if (!loc.found() || loc.file != 0)
            {
                continue;
            }
            at = ALTextPos(loc.line, loc.column);
            ALSourceMap::Loc begin, end;
            writable = writable && map->verbatimSpan(hint.line, hint.column, hint.column, begin, end) && begin.file == 0 &&
                       begin.line == loc.line && begin.column == loc.column;
        }
        ALCodeEditor::InlayHint one;
        one.at     = at;
        one.text   = hint.text;
        one.before = hint.kind == ALScriptInlayHint::Kind::Parameter;
        if (writable)
        {
            one.insert = hint.text;
        }
        hints.push_back(std::move(one));
    }
    doc.editor->setInlayHints(std::move(hints));
}

void ALScriptStudioChecking::mapProblems(Doc& doc)
{
    mapBack(doc.check->analysis, doc.expanded.map, doc.expanded.moduleMaps);
}

// static
void ALScriptStudioChecking::mapBack(std::vector<ALScriptProblem>& problems, const ALSourceMap& map,
                                     const std::vector<std::pair<std::string, ALSourceMap>>& module_maps)
{
    // Back to the source: a problem in an include keeps its file, and
    // what an include declares is the include's to outline. What an
    // include declares and this script does not use is no problem of
    // this script's: a library is meant to hold more than any one
    // script calls, and every script including it would be told so.
    problems.erase(std::remove_if(problems.begin(), problems.end(),
                                  [&map](const ALScriptProblem& problem) {
                                      if (!problem.file.empty() || !unusedWarning(problem))
                                      {
                                          return false;
                                      }
                                      const ALSourceMap::Loc loc = map.toSource(problem.line, problem.column);
                                      return loc.found() && loc.file > 0;
                                  }),
                   problems.end());
    for (ALScriptProblem& problem : problems)
    {
        // One the checker found in a module the script requires, in the
        // module's lines: back to the file it came of, through its own map.
        if (!problem.file.empty())
        {
            problem.fixes.clear();
            const auto own = std::find_if(module_maps.begin(), module_maps.end(),
                                          [&problem](const auto& module) { return module.first == problem.file; });
            if (own == module_maps.end())
            {
                continue;
            }
            ALScriptSpan span;
            span.line      = problem.line;
            span.column    = problem.column;
            span.endLine   = problem.endLine;
            span.endColumn = problem.endColumn;
            const S32 file = mapSpan(own->second, span);
            if (file >= 0)
            {
                problem.file      = own->second.files()[file].path;
                problem.line      = span.line;
                problem.column    = span.column;
                problem.endLine   = span.endLine;
                problem.endColumn = span.endColumn;
            }
            continue;
        }
        ALScriptSpan span;
        span.line      = problem.line;
        span.column    = problem.column;
        span.endLine   = problem.endLine;
        span.endColumn = problem.endColumn;
        const S32 file = mapSpan(map, span);
        if (file < 0)
        {
            // In code the preprocessor made: said so, at the expansion's
            // line, rather than at whatever line of the source has that
            // number.
            problem.file = Doc::GENERATED;
            problem.fixes.clear();
            continue;
        }
        problem.line      = span.line;
        problem.column    = span.column;
        problem.endLine   = span.endLine;
        problem.endColumn = span.endColumn;
        if (file > 0)
        {
            // An include's text is not this tab's to change.
            problem.file = map.files()[file].path;
            problem.fixes.clear();
        }
        else
        {
            ALScriptFixes::mapThrough(map, problem);
        }
    }
}

void ALScriptStudioChecking::mapOutline(Doc& doc)
{
    const ALSourceMap&                map = doc.expanded.map;
    std::vector<ALScriptOutlineEntry> outline;
    for (ALScriptOutlineEntry entry : doc.outline)
    {
        if (mapSpan(map, entry.nameSpan) == 0 && mapSpan(map, entry.span) == 0)
        {
            outline.push_back(std::move(entry));
        }
    }
    doc.outline = std::move(outline);
}

void ALScriptStudioChecking::checked(Doc& doc)
{
    showProblems(doc);
    mWindow.showOutline(doc);
    if (doc.check->fixAllAfterCheck && doc.check->analysisVersion == doc.editor->document().version())
    {
        FixPick pick;
        pick.key = *doc.check->fixAllAfterCheck;
        doc.check->fixAllAfterCheck.reset();
        askFixAll(doc, pick);
    }
    if (doc.save.checked())
    {
        mSaves.save(doc);
    }
}

void ALScriptStudioChecking::showProblems(Doc& doc)
{
    // In the source's places now, where the words are, and where a comment
    // may say a lint is wanted.
    offerImports(doc);
    noLint(doc);
    if (doc.language.lua)
    {
        explainRequires(doc);
    }
    else
    {
        explainTransformWords(doc);
    }
    mAnalysis.refreshProblems(doc);
}

void ALScriptStudioChecking::offerImports(Doc& doc)
{
    // Only where a require or an include is read: a script the
    // preprocessor runs over, or a module or an include, which is read
    // into one.
    if (!(preprocessed(doc) || doc.notecard))
    {
        return;
    }
    const bool lua     = doc.language.lua;
    const auto unknown = [lua](const ALScriptProblem& problem) {
        if (!problem.file.empty() || problem.args.size() != 1)
        {
            return false;
        }
        return lua ? problem.key == "LuauUnknownGlobal" || problem.key == "LuauLintUnknownGlobal" : problem.key == "LSLUndeclared";
    };
    if (std::none_of(doc.check->analysis.begin(), doc.check->analysis.end(), unknown))
    {
        return;
    }
    const ALScriptPreprocessor::Request request = preprocessRequest(doc, /*with_source*/ false);
    const std::string self = ALScriptModules::identity(request.path.empty() ? ALScriptPreprocessor::pathOf(request.ref) : request.path);
    const std::string&                  text = doc.editor->wholeText();
    // The texts of the script's language open here, as they are being
    // written: each version's own, shared, which what it gives is kept for.
    const auto open = [this, &doc, lua]() {
        std::vector<ALScriptModules::Open> out;
        for (const Doc* other : mServices.openDocs())
        {
            if (other != &doc && other->loaded && other->language.lua == lua)
            {
                const std::string path = other->file.empty() ? ALScriptPreprocessor::pathOf(other->ref) : "disk:" + other->file;
                out.push_back({ path, other->name, other->editor->document().version(), other->snapshot() });
            }
        }
        return out;
    };
    // The text's lines found once, for every import offered over it.
    const ALScriptFixes::Lines lines(text);
    const auto                 offer = [&lines, lua](ALScriptProblem& problem, const std::string& module, bool field) {
        if (lua)
        {
            ALScriptFixes::offerRequire(problem, lines, module, field);
        }
        else
        {
            ALScriptFixes::offerInclude(problem, lines, module);
        }
    };
    // What the index knows of the names asked for: each module so named,
    // or that exports or declares one of them.
    std::vector<std::string> names;
    for (const ALScriptProblem& problem : doc.check->analysis)
    {
        if (unknown(problem) && std::find(names.begin(), names.end(), problem.args[0]) == names.end())
        {
            names.push_back(problem.args[0]);
        }
    }
    // What is in reach looked for again on its own thread now and then: the
    // script checked again once what it finds is new.
    const std::weak_ptr<bool>                  waiting = mAlive;
    const std::string                          waiter  = doc.id;
    const std::vector<ALScriptModules::Module> modules = sources().modules(request, open, names, [this, waiting, waiter]() {
        if (Doc* asked = waiting.lock() ? mServices.findDoc(waiter) : nullptr)
        {
            schedule(*asked, true);
        }
    });
    bool                                       given_all = true;
    for (ALScriptProblem& problem : doc.check->analysis)
    {
        if (!unknown(problem))
        {
            continue;
        }
        const std::string& name   = problem.args[0];
        const size_t       before = problem.fixes.size();
        // A SLua module by the name itself, wherever a require of it finds
        // one, its text in hand or not yet. An LSL include's name says
        // nothing of what it declares.
        if (lua)
        {
            ALPreprocessor::Ask ask;
            ask.name    = name;
            ask.require = true;
            ALPreprocessor::Include     found;
            const ALPreprocessor::Found named = sources().lookUp(request, ask, found);
            if (named != ALPreprocessor::Found::No && !found.path.empty() && ALScriptModules::identity(found.path) != self)
            {
                offer(problem, name, false);
            }
        }
        // Then what the index knows: a module so named under another name,
        // and each that exports or declares the name.
        for (const ALScriptModules::Module& module : modules)
        {
            if (problem.fixes.size() - before >= 4)
            {
                break;
            }
            if (lua && module.name == name)
            {
                offer(problem, module.require, false);
            }
            else if (std::find(module.exports.begin(), module.exports.end(), name) != module.exports.end())
            {
                offer(problem, module.require, true);
            }
        }
        given_all = given_all && problem.fixes.size() > before;
        // In the viewer's words, as the analyzer's own fixes were said.
        for (size_t i = before; i < problem.fixes.size(); ++i)
        {
            problem.fixes[i].title = alScriptKeyedWords(problem.fixes[i].key, problem.fixes[i].args, problem.fixes[i].title);
        }
        // One module meant, by its very name or what it gives: that, before
        // a guess at a spelling.
        if (problem.fixes.size() == before + 1)
        {
            for (ALScriptFix& fix : problem.fixes)
            {
                fix.preferred = false;
            }
            problem.fixes.back().preferred = true;
        }
    }
    // A name nothing in hand gives may be given by what is near the script
    // in the world and not fetched yet: fetched, and the script checked
    // again once it is in.
    if (!given_all)
    {
        const std::weak_ptr<bool> alive = mAlive;
        const std::string         id    = doc.id;
        sources().fetchNearby(request, [this, alive, id]() {
            if (Doc* asked = alive.lock() ? mServices.findDoc(id) : nullptr)
            {
                schedule(*asked, true);
            }
        });
    }
}

void ALScriptStudioChecking::explainTransformWords(Doc& doc)
{
    // A parse error on one of the preprocessor's words, with its transform
    // off, is the transform's to explain (ALPreprocessor::transformAt).
    using Transform                     = ALPreprocessor::Transform;
    const bool            preprocessing = sources().preprocessing && sources().preprocessing();
    const ALTextDocument& text          = doc.editor->document();
    const auto            line          = [&text](S32 index) { return std::string_view(text.line(index)); };
    for (ALScriptProblem& problem : doc.check->analysis)
    {
        if (problem.severity != ALScriptProblem::Severity::Error || !problem.file.empty())
        {
            continue;
        }
        std::string     word;
        const Transform transform = ALPreprocessor::transformAt(line, text.lineCount(), problem.line, word);
        // `inline` and `const` are taken off whenever the preprocessor
        // runs, whether the extensions are on or not.
        const bool      marker    = transform == Transform::Extensions && (word == "inline" || word == "const");
        const bool      on        = preprocessing && transform != Transform::None && (marker || sources().transformOn(transform));
        if (transform == Transform::None || on)
        {
            continue;
        }
        LLStringUtil::format_map_t args;
        args["[WORD]"] = word;
        problem.message +=
            " " + mServices.words(transform == Transform::Switch ? "PreprocHintSwitch"
                                  : marker                       ? (word == "const" ? "PreprocHintConst" : "PreprocHintInline")
                                                                 : "PreprocHintExtensions",
                                  args);
    }
}

void ALScriptStudioChecking::explainRequires(Doc& doc)
{
    // Luau's own globals have a require, so nothing else says so: the
    // script runs until the call, and stops there. A file is not what goes
    // up, and a notecard is read into a script that is preprocessed.
    if (doc.notecard || !doc.file.empty() || preprocessed(doc))
    {
        return;
    }
    for (const ALPreprocessor::Required& required : ALPreprocessor::requiresIn(doc.editor->wholeText()))
    {
        ALScriptProblem problem;
        problem.severity  = ALScriptProblem::Severity::Warning;
        problem.source    = ALScriptProblem::Source::Preprocessor;
        problem.line      = required.line;
        problem.column    = required.column;
        problem.endLine   = required.endLine;
        problem.endColumn = required.endColumn;
        LLStringUtil::format_map_t args;
        args["[NAME]"]  = required.name;
        problem.message = mServices.words("RequireNotPreprocessed", args);
        doc.check->analysis.push_back(std::move(problem));
    }
}

void ALScriptStudioChecking::slideProblems(Doc& doc, const ALTextDocument::Edit& edit)
{
    if (doc.problems.empty() && doc.runtime.empty())
    {
        return;
    }
    // A problem on a line the edit replaced goes; one after moves along,
    // by each of a batch's runs before it.
    auto slide = [&edit](auto& list) {
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&edit](auto& problem) {
                                      if (!problem.file.empty())
                                      {
                                          return false;
                                      }
                                      const S32 after = edit.lineAfter(problem.line);
                                      if (after < 0)
                                      {
                                          return true;
                                      }
                                      problem.line = after;
                                      return false;
                                  }),
                   list.end());
    };
    // The editor slides its own marks and squiggles as the edit lands,
    // and drops those on the lines it touched; the list is made again
    // from these at the check the edit has scheduled, when what the
    // analyzer said is about the same text.
    slide(doc.problems);
    slide(doc.runtime);
}

void ALScriptStudioChecking::slideOutline(Doc& doc, const ALTextDocument::Edit& edit)
{
    // What the last check said the script declares, moved with each edit
    // until the next check says it again -- which, for a script past
    // mending, is not until it is mended: each symbol grows and shrinks
    // with what is typed inside it, and moves with what is typed before.
    const auto stretch = [&edit](ALScriptSpan& span) {
        const ALTextRange moved = edit.stretched(rangeOf(span));
        span.line               = moved.begin.line;
        span.column             = moved.begin.column;
        span.endLine            = moved.end.line;
        span.endColumn          = moved.end.column;
    };
    for (ALScriptOutlineEntry& entry : doc.outline)
    {
        stretch(entry.span);
        stretch(entry.nameSpan);
    }
}

void ALScriptStudioChecking::noLint(Doc& doc)
{
    // The script's own lines, as they stand at the check: a problem in an
    // include is said of text this tab does not hold.
    const ALTextDocument& text     = doc.editor->document();
    const bool            lua      = doc.language.lua;
    const auto            ours     = [&text](const ALScriptProblem& problem) {
        return problem.file.empty() && problem.line >= 0 && problem.line < text.lineCount();
    };
    const auto above = [&text](const ALScriptProblem& problem) {
        return problem.line > 0 ? std::string_view(text.line(problem.line - 1)) : std::string_view();
    };
    doc.check->analysis.erase(std::remove_if(doc.check->analysis.begin(), doc.check->analysis.end(),
                                            [&](const ALScriptProblem& problem) {
                                                return ours(problem) &&
                                                       ALScriptFixes::suppressed(problem, text.line(problem.line), above(problem), lua);
                                            }),
                             doc.check->analysis.end());
    for (ALScriptProblem& problem : doc.check->analysis)
    {
        if (!ours(problem))
        {
            continue;
        }
        if (std::optional<ALScriptFix> fix = ALScriptFixes::suppression(problem, text.line(problem.line), lua))
        {
            fix->title = alScriptKeyedWords(fix->key, fix->args, fix->title);
            problem.fixes.push_back(std::move(*fix));
        }
    }
}

bool ALScriptStudioChecking::applyFix(Doc& doc, const ALScriptFix& fix, U32 version)
{
    if (!doc.loaded || !doc.modifiable || fix.edits.empty())
    {
        return false;
    }
    // In the places of the text it was made over: a text typed in since is
    // checked again, and its fixes offered afresh.
    ALCodeEditor&         source = mWindow.editorInFront(doc);
    const ALTextDocument& text   = source.document();
    if (version != text.version())
    {
        mServices.setStatus(mServices.words("FixStale"), true);
        schedule(doc, true);
        return false;
    }
    std::vector<std::pair<ALTextRange, std::string>> edits;
    for (const ALScriptEdit& edit : fix.edits)
    {
        const ALTextRange range(ALTextPos(edit.line, edit.column), ALTextPos(edit.endLine, edit.endColumn));
        if (text.clamp(range.begin) != range.begin || text.clamp(range.end) != range.end)
        {
            mServices.setStatus(mServices.words("FixStale"), true);
            schedule(doc, true);
            return false;
        }
        edits.emplace_back(range, edit.text);
    }
    if (!source.replaceAll(std::move(edits)))
    {
        return false;
    }
    source.undoJournal().label(fix.kind == ALScriptFix::Kind::Refactor ? "refactor" : "fix");
    mServices.setStatus(fix.title);
    schedule(doc, true);
    return true;
}

void ALScriptStudioChecking::askFixAll(Doc& doc, const FixPick& pick)
{
    // Asked of a text not checked yet -- typed in a moment ago -- whose
    // problems are not known: checked first, and asked again then, rather
    // than said to have nothing to fix.
    if (!pick.forSave && doc.loaded && !doc.notecard && doc.check->analysisVersion != doc.editor->document().version())
    {
        doc.check->fixAllAfterCheck = pick.key;
        schedule(doc, true);
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        mServices.setStatus(mServices.words("FixChecking", args));
        return;
    }
    mWindow.settleProblems(doc);
    size_t                                left  = 0;
    const std::vector<const ALScriptFix*> fixes = doc.pickFixes(pick, &left);
    // What is not safe to make without a look, said to be left for one.
    const std::string left_said = left > 0 ? mServices.counted("FixesLeft", static_cast<S32>(left)) : std::string();
    if (fixes.size() < 2)
    {
        if (!fixes.empty())
        {
            if (applyFix(doc, *fixes.front(), doc.editor->document().version()) && !left_said.empty())
            {
                mServices.setStatus(fixes.front()->title + " " + left_said);
            }
        }
        else
        {
            mServices.setStatus(left_said.empty() ? mServices.words("FixNone") : mServices.words("FixNoneSafe") + " " + left_said);
        }
        return;
    }
    // Asked first, as Replace All asks: many changes at once, said as many.
    LLSD args;
    args["FIXES"]                   = mServices.counted("Fixes", static_cast<S32>(fixes.size()));
    args["NAME"]                    = doc.name;
    args["EXAMPLE"]                 = fixes.front()->title;
    args["LEFT"]                    = left_said.empty() ? std::string() : " " + left_said;
    const std::weak_ptr<bool> alive = mAlive;
    const std::string         id    = doc.id;
    mWindow.confirmFixAll(args, [this, alive, id, pick]() {
        if (Doc* asked = alive.lock() ? mServices.findDoc(id) : nullptr)
        {
            fixAll(*asked, pick);
        }
    });
}

bool ALScriptStudioChecking::fixAll(Doc& doc, const FixPick& pick)
{
    if (!doc.loaded || !doc.modifiable)
    {
        return false;
    }
    // Only the fixes made over the text as it stands (pickFixes). Made on
    // a save, into the source where it stands, whichever view is in front,
    // as the formatting and the trimming a save makes are; asked for,
    // with the source brought forward, to be seen.
    mWindow.settleProblems(doc);
    ALCodeEditor&                                    source = pick.forSave ? *doc.editor : mWindow.editorInFront(doc);
    const std::vector<const ALScriptFix*>            fixes  = doc.pickFixes(pick);
    std::vector<std::pair<ALTextRange, std::string>> edits;
    for (const ALScriptFix* fix : fixes)
    {
        for (const ALScriptEdit& edit : fix->edits)
        {
            edits.emplace_back(ALTextRange(ALTextPos(edit.line, edit.column), ALTextPos(edit.endLine, edit.endColumn)), edit.text);
        }
    }
    // Every one of them one step to undo: they were made over one check,
    // and none meets another.
    if (edits.empty() || !source.replaceAll(std::move(edits)))
    {
        return false;
    }
    source.undoJournal().label("fix");
    mServices.setStatus(mServices.counted("FixesMade", static_cast<S32>(fixes.size())));
    schedule(doc, true);
    return true;
}

void ALScriptStudioChecking::fixesOn(Doc& doc, S32 line, std::vector<ALCodeEditor::Fix>& out)
{
    mWindow.settleProblems(doc);
    // Only over the text they were made in: a text typed in since has other
    // places, and is checked again a moment later.
    const U32 now = doc.editor->document().version();
    for (const Doc::Shown& shown : doc.shown)
    {
        if (!shown.file.empty() || shown.line != line || shown.fixesFor != now)
        {
            continue;
        }
        for (size_t i = 0; i < shown.fixes.size(); ++i)
        {
            const ALScriptFix&   fix = shown.fixes[i];
            ALCodeEditor::Fix    one;
            one.title     = fix.title;
            one.preferred = fix.preferred;
            one.suppress  = fix.kind == ALScriptFix::Kind::Suppress;
            for (const ALScriptEdit& edit : fix.edits)
            {
                one.edits.emplace_back(ALTextRange(ALTextPos(edit.line, edit.column), ALTextPos(edit.endLine, edit.endColumn)), edit.text);
            }
            // The problem by what the pane's rows carry, and the fix by its
            // place among the problem's.
            one.value["doc"]     = doc.id;
            one.value["line"]    = shown.line;
            one.value["column"]  = shown.column;
            one.value["file"]    = shown.file;
            one.value["message"] = shown.message;
            one.value["fix"]     = static_cast<S32>(i);
            out.push_back(std::move(one));
        }
    }
}

const ALScriptStudioChecking::Doc::Shown* ALScriptStudioChecking::shownOf(const LLSD& value) const
{
    const Doc* doc = mServices.findDoc(value["doc"].asString());
    if (!doc)
    {
        return nullptr;
    }
    return doc->findShown(value["line"].asInteger(), value["column"].asInteger(), value["file"].asString(), value["message"].asString());
}
