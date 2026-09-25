/**
 * @file alscriptproblemspane.cpp
 * @brief Script Studio's Problems tab: a tab's problems gathered, and listed through the pane's filters.
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
#include "alpanelist.h"
#include "llcheckboxctrl.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfloaterreg.h"
#include "llmenugl.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <algorithm>

void ALFloaterScriptStudio::refreshProblems(Doc& doc)
{
    // In the theme's colours for each level, where it names them.
    const LLColor4 error_color   = doc.editor->markColor(ALCodeEditor::Mark::Error);
    const LLColor4 warning_color = doc.editor->markColor(ALCodeEditor::Mark::Warning);
    const LLColor4 note_color    = doc.editor->markColor(ALCodeEditor::Mark::Note);
    const LLColor4 runtime_color = doc.editor->markColor(ALCodeEditor::Mark::Runtime);

    doc.shown.clear();
    doc.editor->clearMarks();
    std::vector<ALCodeEditor::Decoration> decorations;
    const ALTextDocument&                 text = doc.editor->document();

    auto add = [&](S32 line, S32 column, bool has_column, S32 end_line, S32 end_column, ALCodeEditor::Mark mark,
                   Doc::Level level, const std::string& origin, const std::string& message, const std::string& file = std::string(),
                   const std::string& lint = std::string()) {
        Doc::Shown row;
        row.lint      = lint;
        row.line      = line;
        row.column    = column;
        row.hasColumn = has_column;
        row.endLine   = end_line;
        row.endColumn = end_column;
        row.level     = level;
        row.origin    = origin;
        row.message   = message;
        row.file      = file;
        if (!file.empty())
        {
            // In an include: listed under its name, not marked here.
            row.fileName = includeName(doc, file);
            doc.shown.push_back(std::move(row));
            return;
        }
        doc.shown.push_back(std::move(row));
        if (doc.editor->markAt(line) < mark)
        {
            doc.editor->setMark(line, mark);
        }
        ALCodeEditor::Decoration decoration;
        const ALTextPos begin = text.clamp(ALTextPos(line, has_column ? column : 0));
        ALTextPos       end   = text.clamp(ALTextPos(end_line, end_column));
        if (end <= begin)
        {
            end = text.nextWord(begin);
        }
        decoration.range   = ALTextRange(begin, end);
        decoration.color   = mark == ALCodeEditor::Mark::Runtime   ? runtime_color
                             : level == Doc::Level::Error   ? error_color
                             : level == Doc::Level::Warning ? warning_color
                                                            : note_color;
        decoration.message = origin + ": " + message;
        decorations.push_back(std::move(decoration));
    };

    // The analyzer's word on a line as it is now over the compiler's on
    // the text last saved: a syntax error both found is said once.
    const bool analysis_current = doc.analysisVersion == doc.editor->document().version();
    auto       analysed_error_on = [&](S32 line) {
        for (const ALScriptProblem& problem : doc.analysis)
        {
            if (problem.severity == ALScriptProblem::Severity::Error && problem.file.empty() && problem.line == line)
            {
                return true;
            }
        }
        return false;
    };
    for (const Doc::Compiled& problem : doc.problems)
    {
        const Doc::Level level = Doc::levelOf(problem.level);
        if (analysis_current && level == Doc::Level::Error && problem.file.empty() && analysed_error_on(problem.line))
        {
            continue;
        }
        add(problem.line, problem.column, problem.hasColumn, problem.line, problem.column, Doc::markOf(level), level, getString("OriginCompiler"), problem.message,
            problem.file);
    }
    // The preprocessor's own word on the text as it stands, and the
    // optimizer's notes from the last run ahead of a save.
    // The optimizer's notes offer what it did as a change to the source,
    // over the text the run was made of.
    const U32  now             = doc.editor->document().version();
    const std::optional<ALScriptWeight::Target> target = weightTarget(doc);
    const auto preprocessorRow = [&](const ALScriptProblem& problem, U32 version) {
        const Doc::Level level     = Doc::levelOf(problem.severity);
        const bool       optimizer = problem.source == ALScriptProblem::Source::Optimizer;
        // What the lines a change is on came to less in code, beside what
        // it did to them.
        std::string message = problem.message;
        if (problem.savedBytes && target)
        {
            LLStringUtil::format_map_t args;
            args["[BYTES]"]  = std::to_string(std::abs(*problem.savedBytes));
            args["[TARGET]"] = ALScriptWeight::nameOf(*target);
            message += " " + getString(*problem.savedBytes > 0 ? "OptimizerNoteLighter" : *problem.savedBytes < 0 ? "OptimizerNoteHeavier" : "OptimizerNoteSame", args);
        }
        add(problem.line, problem.column, true, problem.endLine, problem.endColumn, Doc::markOf(level), level,
            getString(optimizer ? "OriginOptimizer" : "OriginPreprocessor"), message, problem.file);
        doc.shown.back().key      = problem.key;
        doc.shown.back().fixes    = problem.fixes;
        doc.shown.back().fixesFor = version;
        if (version == now && problem.file.empty() && !problem.fixes.empty())
        {
            const bool changes = std::any_of(problem.fixes.begin(), problem.fixes.end(),
                                             [](const ALScriptFix& fix) { return fix.kind == ALScriptFix::Kind::Fix; });
            doc.editor->setFixable(problem.line, true, changes || doc.editor->changesAt(problem.line));
        }
    };
    if (doc.expanded.valid)
    {
        for (const ALScriptProblem& problem : doc.expanded.problems)
        {
            preprocessorRow(problem, doc.expanded.version);
        }
    }
    if (doc.uploaded.valid)
    {
        for (const ALScriptProblem& problem : doc.uploaded.problems)
        {
            if (problem.source == ALScriptProblem::Source::Optimizer)
            {
                preprocessorRow(problem, doc.uploaded.version);
            }
        }
    }
    for (const ALScriptProblem& problem : doc.analysis)
    {
        const Doc::Level  level  = Doc::levelOf(problem.severity);
        const std::string origin = problem.source == ALScriptProblem::Source::Parser  ? getString("OriginParser")
                                   : problem.source == ALScriptProblem::Source::Types ? getString("OriginTypes")
                                                                                      : getString("OriginLint");
        // A Luau lint's name says what to look up or turn off; an LSL
        // error's number says nothing to whoever reads it.
        const bool        named   = !problem.code.empty() && problem.code.find_first_not_of("0123456789") != std::string::npos;
        const std::string message = named ? problem.message + " [" + problem.code + "]" : problem.message;
        add(problem.line, problem.column, true, problem.endLine, problem.endColumn, Doc::markOf(level), level, origin, message, problem.file,
            problem.source == ALScriptProblem::Source::Lint ? problem.code : std::string());
        doc.shown.back().key      = problem.key;
        doc.shown.back().fixes    = problem.fixes;
        doc.shown.back().fixesFor = doc.analysisVersion;
        // The gutter's word on what the line offers: a lightbulb where the
        // caret is, a round mark where a fix changes the script.
        if (analysis_current && problem.file.empty() && !problem.fixes.empty())
        {
            const bool changes = std::any_of(problem.fixes.begin(), problem.fixes.end(),
                                             [](const ALScriptFix& fix) { return fix.kind == ALScriptFix::Kind::Fix; });
            doc.editor->setFixable(problem.line, true, changes || doc.editor->changesAt(problem.line));
        }
    }
    for (const Doc::RuntimeProblem& problem : doc.runtime)
    {
        const S32 line   = llmax(0, problem.line);
        const S32 column = llmax(0, problem.column);
        // Said again and again, it is one problem, with how many times.
        std::string message = problem.message;
        if (problem.count > 1)
        {
            LLStringUtil::format_map_t args;
            args["[COUNT]"] = std::to_string(problem.count);
            message += " " + getString("RepeatedTimes", args);
        }
        add(line, column, problem.column >= 0, line, column, ALCodeEditor::Mark::Runtime, Doc::Level::Error, getString("OriginRuntime"), message,
            problem.file);
    }
    if (!doc.definitionsError.empty())
    {
        Doc::Shown row;
        row.level   = Doc::Level::Note;
        row.origin  = getString("OriginDefinitions");
        row.message = doc.definitionsError;
        doc.shown.push_back(std::move(row));
    }
    // Code heavier than its target runs a script in: a warning, since
    // the number is the studio's reckoning of what the region compiles --
    // an estimate for Mono, and of the text before the optimizer where it
    // runs after -- and not the region's word.
    if (doc.weight && doc.weightVersion == doc.editor->document().version() && doc.weight->total > doc.weight->limit)
    {
        const ALScriptWeight&      weight = *doc.weight;
        LLStringUtil::format_map_t args;
        args["[SIZE]"]   = llformat("%.1f", (F64)weight.total / 1024.0);
        args["[LIMIT]"]  = std::to_string(weight.limit / 1024);
        args["[TARGET]"] = ALScriptWeight::nameOf(weight.target);
        Doc::Shown row;
        row.level   = Doc::Level::Warning;
        row.origin  = getString("OriginWeight");
        row.message = getString(weight.estimate ? "WeightOverEstimate" : !doc.weightExact ? "WeightOverBefore" : "WeightOver", args);
        doc.shown.push_back(std::move(row));
    }
    std::stable_sort(doc.shown.begin(), doc.shown.end(), [](const Doc::Shown& a, const Doc::Shown& b) {
        // The script's own first, then each include's together.
        if (a.file != b.file)
        {
            return a.file < b.file;
        }
        return a.line != b.line ? a.line < b.line : a.column < b.column;
    });
    mProblemStore.replace(doc.id, doc.shown);
    doc.editor->setDecorations(std::move(decorations));
    // The pane, where it lists this script's: the one it is about, or
    // every one open.
    Doc* listed = problemsDoc();
    if (listed && (listed == &doc || (mProblemScope && mProblemScope->getValue().asString() == "all")))
    {
        fillProblems(listed);
    }
    measureAsset(doc);
    if (&doc == active())
    {
        refreshTrailer(doc);
    }
    fillTabs();
}

ALFindings<ALFloaterScriptStudio::Doc::Shown, ALFloaterScriptStudio::ProblemTraits>::Query ALFloaterScriptStudio::problemQuery(const Doc& doc) const
{
    ALFindings<Doc::Shown, ProblemTraits>::Query query;
    query.file     = doc.id;
    query.errors   = !mProblemErrors || mProblemErrors->get();
    query.warnings = !mProblemWarnings || mProblemWarnings->get();
    query.notes    = !mProblemNotes || mProblemNotes->get();
    query.fixable  = mProblemFixable && mProblemFixable->get();
    query.rule     = mProblemOrigin ? mProblemOrigin->getValue().asString() : std::string();
    query.text     = mProblemFilter ? mProblemFilter->getText() : std::string();
    LLStringUtil::trim(query.text);
    return query;
}

std::vector<const ALFloaterScriptStudio::Doc*> ALFloaterScriptStudio::problemDocs(const Doc* doc) const
{
    std::vector<const Doc*> docs;
    if (!doc)
    {
        return docs;
    }
    docs.push_back(doc);
    if (mProblemScope && mProblemScope->getValue().asString() == "all")
    {
        for (const std::unique_ptr<Doc>& other : mDocs)
        {
            if (other.get() != doc && !other->notecard)
            {
                docs.push_back(other.get());
            }
        }
    }
    return docs;
}

ALFloaterScriptStudio::Doc* ALFloaterScriptStudio::problemsDoc()
{
    const size_t index = mProblemsShownFor.empty() ? NONE : indexOf(mProblemsShownFor);
    return index != NONE ? mDocs[index].get() : active();
}

void ALFloaterScriptStudio::fillProblems(const Doc* doc)
{
    // The row chosen and how far the list was scrolled are kept through a
    // refill of the same script's: a check comes at every pause in
    // typing, and whoever is working down the list keeps their place.
    LLSD      chosen;
    const S32 scrolled = mProblems->getScrollPos();
    if (LLScrollListItem* item = mProblems->getFirstSelected())
    {
        chosen = item->getValue();
    }
    const bool same = doc && doc->id == mProblemsShownFor;
    mProblemsShownFor = doc ? doc->id : std::string();
    mProblems->deleteAllItems();
    mProblems->setCommentText(LLStringUtil::null);

    // How many of each there are before the filters: what the level
    // boxes and the tab say.
    const std::vector<const Doc*> docs = problemDocs(doc);
    S32                           errors = 0, warnings = 0, notes = 0, fixable = 0;
    for (const Doc* each : docs)
    {
        for (const Doc::Shown& shown : each->shown)
        {
            errors += shown.level == Doc::Level::Error ? 1 : 0;
            warnings += shown.level == Doc::Level::Warning ? 1 : 0;
            notes += shown.level == Doc::Level::Note ? 1 : 0;
            fixable += ProblemTraits::fixable(shown) ? 1 : 0;
        }
    }
    const S32 held = errors + warnings + notes;
    const auto label = [this](LLCheckBoxCtrl* box, const char* name, S32 count) {
        LLStringUtil::format_map_t args;
        args["[COUNT]"] = std::to_string(count);
        const std::string said = count > 0 ? getString(std::string(name) + "Count", args) : getString(name);
        if (box->getLabel() != said)
        {
            box->setLabel(said);
        }
    };
    label(mProblemErrors, "FilterErrors", errors);
    label(mProblemWarnings, "FilterWarnings", warnings);
    label(mProblemNotes, "FilterNotes", notes);
    label(mProblemFixable, "FilterFixable", fixable);
    layoutProblemFilters();
    mProblemsHeld = held;
    refreshBottomTabs();
    if (!doc)
    {
        return;
    }

    // What the filters take, by script and, within one, by the file it is
    // in: the script's own, then each include's.
    struct Group
    {
        const Doc*                       doc = nullptr;
        std::string                      file;
        std::string                      fileName;
        std::vector<const Doc::Shown*>   rows;
    };
    std::vector<Group> groups;
    S32                listed = 0;
    for (const Doc* each : docs)
    {
        const auto selected = mProblemStore.select(problemQuery(*each));
        for (const Doc::Shown* problem : selected.found)
        {
            if (groups.empty() || groups.back().doc != each || groups.back().file != problem->file)
            {
                groups.push_back({ each, problem->file, problem->fileName, {} });
            }
            groups.back().rows.push_back(problem);
            ++listed;
        }
    }

    static const LLUIColor ink = LLUIColorTable::instance().getColor("ScrollUnselectedColor", LLColor4::white);
    const bool all      = docs.size() > 1 || (mProblemScope && mProblemScope->getValue().asString() == "all");
    // Whose they are, over them, wherever that is not plain: more than one
    // script's or file's, or another script's than the one in front, which
    // a row followed into an include leaves the list on.
    const bool elsewhere = doc != active();
    const bool headings  = all || groups.size() > 1 || elsewhere;
    for (size_t group_index = 0; group_index < groups.size(); ++group_index)
    {
        const Group& group = groups[group_index];
        if (headings)
        {
            // Whose they are, over them: the script, or the include and,
            // among every script's, which script includes it.
            S32 group_errors = 0, group_warnings = 0, group_notes = 0;
            for (const Doc::Shown* problem : group.rows)
            {
                group_errors += problem->level == Doc::Level::Error ? 1 : 0;
                group_warnings += problem->level == Doc::Level::Warning ? 1 : 0;
                group_notes += problem->level == Doc::Level::Note ? 1 : 0;
            }
            std::vector<std::string> counts;
            if (group_errors > 0)
            {
                counts.push_back(counted("ProblemErrors", group_errors));
            }
            if (group_warnings > 0)
            {
                counts.push_back(counted("ProblemWarnings", group_warnings));
            }
            if (group_notes > 0)
            {
                counts.push_back(counted("ProblemNotes", group_notes));
            }
            LLStringUtil::format_map_t args;
            args["[NAME]"] = group.doc->name;
            args["[FILE]"] = group.fileName;
            std::string name = group.file.empty() ? group.doc->name : getString(all ? "ProblemsIncludedBy" : "ProblemsIncluded", args);
            for (size_t i = 0; i < counts.size(); ++i)
            {
                name += (i == 0 ? "   \xC2\xB7   " : ", ") + counts[i];
            }
            LLSD heading;
            heading["value"]["heading"]      = true;
            heading["value"]["group"]        = static_cast<S32>(group_index);
            heading["columns"][0]["column"]  = "icon";
            heading["columns"][0]["type"]    = "icon";
            heading["columns"][0]["value"]   = group.file.empty() ? imageNameOf(*group.doc) : includeImage(group.file, group.doc->language.lua);
            heading["columns"][1]["column"]  = "message";
            heading["columns"][1]["value"]   = name;
            heading["columns"][1]["color"]   = ink.get().getValue();
            heading["columns"][1]["font"]["style"] = "BOLD";
            if (LLScrollListItem* item = mProblems->addElement(heading))
            {
                item->setEnabled(false);
            }
        }
        for (const Doc::Shown* problem : group.rows)
        {
            // The row carries the place and whose it is, so that choosing
            // it needs no index into anything that a filter reorders.
            LLSD value;
            value["group"]     = static_cast<S32>(group_index);
            value["doc"]       = group.doc->id;
            value["line"]      = problem->line;
            value["column"]    = problem->column;
            value["hasColumn"] = problem->hasColumn;
            value["endLine"]   = problem->endLine;
            value["endColumn"] = problem->endColumn;
            value["file"]      = problem->file;
            value["fileName"]  = problem->fileName;
            value["level"]     = Doc::levelName(problem->level);
            value["origin"]    = problem->origin;
            value["message"]   = problem->message;
            value["lint"]      = problem->lint;
            // The column as the status line counts it, where the problem is
            // in the text open here; an include's is its byte, for want of
            // its text.
            const ALTextDocument& text   = group.doc->editor->document();
            const S32             column = problem->file.empty() && problem->line < text.lineCount()
                                               ? text.displayColumn(ALTextPos(problem->line, problem->column), group.doc->editor->getTabWidth())
                                               : problem->column;
            const std::string where = problem->hasColumn ? llformat("%d:%d", problem->line + 1, column + 1) : llformat("%d", problem->line + 1);
            const std::string level = getString(problem->level == Doc::Level::Error     ? "LevelError"
                                                 : problem->level == Doc::Level::Warning ? "LevelWarning"
                                                                                         : "LevelNote");
            // Every column carries the whole of it: a diagnostic longer
            // than the column is cut at the column's edge.
            std::string tip = level + "   \xC2\xB7   " + (problem->file.empty() ? group.doc->name : problem->fileName) + ":" + where + "\n" +
                              problem->message;
            // What would put it right, which its right-click menu offers.
            for (const ALScriptFix& fix : problem->fixes)
            {
                if (fix.kind == ALScriptFix::Kind::Fix)
                {
                    LLStringUtil::format_map_t fix_args;
                    fix_args["[TITLE]"] = fix.title;
                    tip += "\n" + getString("ProblemFixTip", fix_args);
                }
            }
            const ALCodeEditor::Mark mark = problem->origin == getString("OriginRuntime") ? ALCodeEditor::Mark::Runtime : Doc::markOf(problem->level);
            LLSD row;
            row["value"]                = value;
            row["columns"][0]["column"] = "icon";
            row["columns"][0]["type"]   = "icon";
            row["columns"][0]["value"]  = problem->level == Doc::Level::Error     ? "Problem_Error"
                                          : problem->level == Doc::Level::Warning ? "Problem_Warning"
                                                                                  : "Problem_Note";
            row["columns"][0]["color"]  = group.doc->editor->markColor(mark).getValue();
            row["columns"][1]["column"] = "message";
            row["columns"][1]["value"]  = problem->message;
            row["columns"][2]["column"] = "source";
            row["columns"][2]["value"]  = problem->origin;
            row["columns"][3]["column"] = "line";
            row["columns"][3]["value"]  = where;
            for (S32 i = 0; i < 4; ++i)
            {
                row["columns"][i]["tool_tip"] = tip;
            }
            mProblems->addElement(row);
        }
    }
    // What the filters hide, said under what they let through.
    if (listed > 0 && held > listed)
    {
        LLSD more;
        more["value"]["heading"]     = true;
        more["value"]["group"]       = static_cast<S32>(groups.size());
        more["columns"][0]["column"] = "icon";
        more["columns"][0]["value"]  = std::string();
        more["columns"][1]["column"] = "message";
        more["columns"][1]["value"]  = counted("ProblemsHidden", held - listed);
        if (LLScrollListItem* item = mProblems->addElement(more))
        {
            item->setEnabled(false);
        }
    }
    if (same)
    {
        // The same problem, found by what it says, where it is from and
        // whose it is; nearest the line it was on, since an edit above
        // moves it.
        S32 best          = -1;
        S32 best_distance = S32_MAX;
        if (chosen.isMap() && !chosen.has("heading"))
        {
            // In the order shown, which a sort by a column has changed.
            mProblems->updateSort();
            const std::vector<LLScrollListItem*> rows = mProblems->getAllData();
            for (size_t i = 0; i < rows.size(); ++i)
            {
                const LLSD& value = rows[i]->getValue();
                if (value.has("heading") || value["message"].asString() != chosen["message"].asString() ||
                    value["origin"].asString() != chosen["origin"].asString() || value["file"].asString() != chosen["file"].asString() ||
                    value["doc"].asString() != chosen["doc"].asString())
                {
                    continue;
                }
                const S32 distance = std::abs(value["line"].asInteger() - chosen["line"].asInteger());
                if (distance < best_distance)
                {
                    best          = static_cast<S32>(i);
                    best_distance = distance;
                }
            }
        }
        if (best >= 0)
        {
            mProblems->selectNthItem(best);
        }
        mProblems->setScrollPos(scrolled);
    }
    if (held == 0)
    {
        bool current = true;
        for (const Doc* each : docs)
        {
            current = current && each->loaded && each->analysisVersion == each->editor->document().version();
        }
        LLStringUtil::format_map_t named;
        named["[NAME]"] = doc->name;
        mProblems->setCommentText(!current ? std::string() : all ? getString("NoProblemsOpen") : elsewhere ? getString("NoProblemsIn", named) : getString("NoProblems"));
    }
    else if (listed == 0)
    {
        mProblems->setCommentText(counted("ProblemsAllHidden", held));
    }
}

void ALFloaterScriptStudio::layoutProblemFilters()
{
    // The level boxes as wide as what they say, the rest after them, the
    // words' box taking what is left.
    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    S32             left = 4;
    for (LLCheckBoxCtrl* box : { mProblemErrors, mProblemWarnings, mProblemNotes, mProblemFixable })
    {
        const S32 width = 24 + font->getWidth(box->getLabel());
        box->reshape(width, box->getRect().getHeight());
        box->setOrigin(left, box->getRect().mBottom);
        left += width + 4;
    }
    left += 6;
    for (LLUICtrl* combo : { static_cast<LLUICtrl*>(mProblemScope), static_cast<LLUICtrl*>(mProblemOrigin) })
    {
        combo->setOrigin(left, combo->getRect().mBottom);
        left += combo->getRect().getWidth() + 6;
    }
    left += 2;
    const LLRect filter = mProblemFilter->getRect();
    const S32    right  = mProblemFilter->getParent() ? mProblemFilter->getParent()->getRect().getWidth() - 4 : filter.mRight;
    const S32    width  = llmax(60, right - left);
    if (filter.mLeft != left || filter.getWidth() != width)
    {
        mProblemFilter->reshape(width, filter.getHeight());
        mProblemFilter->setOrigin(left, filter.mBottom);
    }
}

void ALFloaterScriptStudio::selectFirstError(bool checkers_only)
{
    mProblems->updateSort();
    const std::vector<LLScrollListItem*> rows     = mProblems->getAllData();
    const std::string                    compiler = getString("OriginCompiler");
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const LLSD& value = rows[i]->getValue();
        if (value.isMap() && !value.has("heading") && value["level"].asString() == "ERROR" && (!checkers_only || value["origin"].asString() != compiler))
        {
            mProblems->selectNthItem(static_cast<S32>(i));
            mProblems->scrollToShowSelected();
            onProblemSelected(true);
            return;
        }
    }
}

void ALFloaterScriptStudio::showProblemMenu(S32 x, S32 y)
{
    if (!LLMenuGL::sMenuContainer)
    {
        return;
    }
    // The row under the mouse is the one the menu is about; a heading is
    // about no problem.
    LLScrollListItem* hit = mProblems->hitItem(x, y);
    if (!hit || !hit->getEnabled())
    {
        return;
    }
    mProblems->selectItemAt(x, y, MASK_NONE);
    if (LLContextMenu* old = mProblemMenuHandle.get())
    {
        old->die();
        mProblemMenuHandle.markDead();
    }
    // The script the chosen problem is of, in its language, and the lint
    // it is where it is one there is a choice about.
    const auto doc_of = [this]() -> Doc* {
        LLScrollListItem* item = mProblems->getFirstSelected();
        const size_t      index = item ? indexOf(item->getValue()["doc"].asString()) : NONE;
        return index == NONE ? nullptr : mDocs[index].get();
    };
    const auto lint_of = [this, doc_of](bool& lua) -> std::string {
        Doc*              doc  = doc_of();
        LLScrollListItem* item = mProblems->getFirstSelected();
        if (!doc || !item)
        {
            return std::string();
        }
        lua                    = doc->language.lua;
        const std::string lint = item->getValue()["lint"].asString();
        for (const ALScriptLints::Lint& one : ALScriptLints::all())
        {
            if (one.lua == lua && one.id == lint)
            {
                return lint;
            }
        }
        return std::string();
    };
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
    commit.add("Problem.Action", [this](LLUICtrl*, const LLSD& param) { onProblemMenu(param.asString()); });
    enable.add("Problem.Enable", [lint_of, doc_of](LLUICtrl*, const LLSD& param) {
        const std::string what = param.asString();
        if (what == "copy" || what == "copy_where" || what == "copy_all" || what == "settings")
        {
            return true;
        }
        if (what == "clear_runtime")
        {
            const Doc* doc = doc_of();
            return doc && !doc->runtime.empty();
        }
        bool lua = false;
        return !lint_of(lua).empty();
    });
    enable.add("Problem.Check", [lint_of](LLUICtrl*, const LLSD&) {
        bool              lua  = false;
        const std::string lint = lint_of(lua);
        return !lint.empty() && ALScriptLints::level(lua, lint) == ALScriptLints::Level::Error;
    });
    // Each fix the problem offers, by what it does; and every problem of its
    // kind at once, where there is more than one to put right.
    const auto shown_of = [this]() -> const Doc::Shown* {
        LLScrollListItem* item = mProblems->getFirstSelected();
        return item ? shownOf(item->getValue()) : nullptr;
    };
    enable.add("Problem.FixVisible", [shown_of, doc_of](LLUICtrl* ctrl, const LLSD& param) {
        const Doc::Shown* shown = shown_of();
        const Doc*        doc   = doc_of();
        if (!shown || !doc || !doc->modifiable)
        {
            return false;
        }
        if (param.asString() == "kind")
        {
            return pickFixes(*doc, FixPick{ shown->key }).size() > 1;
        }
        // Where Fix All would only say what it leaves, too, so that it can.
        size_t left = 0;
        return param.asString() == "all" && pickFixes(*doc, FixPick{}, &left).size() + left > 1;
    });
    LLContextMenu* menu = LLUICtrlFactory::createFromFile<LLContextMenu>("menu_script_studio_problem.xml", LLMenuGL::sMenuContainer,
                                                                          LLMenuHolderGL::child_registry_t::instance());
    if (!menu)
    {
        return;
    }
    mProblemMenuHandle = menu->getHandle();
    const Doc::Shown* shown = shown_of();
    const Doc*        doc   = doc_of();
    // Each fix the problem offers, by what it does, first in the menu:
    // however many -- a name's guesses and the suppressions come to more
    // than a menu of fixed places held.
    if (shown && doc && doc->modifiable)
    {
        for (size_t n = 0; n < shown->fixes.size(); ++n)
        {
            LLMenuItemCallGL::Params p;
            p.name  = "fix_" + std::to_string(n);
            p.label = shown->fixes[n].title;
            LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
            const std::string action = "fix:" + std::to_string(n);
            item->setClickCallback([this, action](LLUICtrl*, const LLSD&) { onProblemMenu(action); });
            menu->insert(static_cast<S32>(n), item);
        }
    }
    menu->setItemVisible("fix_separator", shown && doc && doc->modifiable && !shown->fixes.empty());
    menu->show(x, y);
    LLMenuGL::showPopup(mProblems, menu, x, y);
}

void ALFloaterScriptStudio::onProblemMenu(const std::string& action)
{
    LLScrollListItem* item  = mProblems->getFirstSelected();
    const size_t      index = item ? indexOf(item->getValue()["doc"].asString()) : NONE;
    if (index == NONE)
    {
        return;
    }
    Doc&              doc   = *mDocs[index];
    const LLSD&       value = item->getValue();
    const std::string lint  = value["lint"].asString();
    const bool        lua   = doc.language.lua;
    // A problem as a line of text: where, what level, from whom, what.
    const auto as_text = [this](const LLSD& one) {
        const std::string name  = one["fileName"].asString().empty() ? mDocs[indexOf(one["doc"].asString())]->name : one["fileName"].asString();
        const S32         line  = one["line"].asInteger() + 1;
        const std::string where = one["hasColumn"].asBoolean() ? llformat("%s:%d:%d", name.c_str(), line, one["column"].asInteger() + 1)
                                                               : llformat("%s:%d", name.c_str(), line);
        const std::string level = one["level"].asString() == "ERROR" ? "error" : one["level"].asString() == "WARNING" ? "warning" : "note";
        return where + ": " + level + ": " + one["message"].asString() + " [" + one["origin"].asString() + "]";
    };
    const auto copy = [](const std::string& text) { LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size())); };
    if (action == "copy")
    {
        copy(value["message"].asString());
    }
    else if (action == "copy_where")
    {
        copy(as_text(value));
    }
    else if (action == "copy_all")
    {
        // Every problem the list shows, as a compiler lists them.
        std::string all;
        S32         count = 0;
        for (LLScrollListItem* row : mProblems->getAllData())
        {
            const LLSD& one = row->getValue();
            if (one.isMap() && !one.has("heading") && indexOf(one["doc"].asString()) != NONE)
            {
                all += as_text(one) + "\n";
                ++count;
            }
        }
        copy(all);
        setStatus(counted("ProblemsCopied", count));
    }
    else if (action == "clear_runtime")
    {
        // What the script said as it ran, let go of until it says it again.
        doc.runtime.clear();
        refreshProblems(doc);
    }
    else if (action == "off" && !lint.empty())
    {
        // The scripts are checked again as the setting changes.
        ALScriptLints::setLevel(lua, lint, ALScriptLints::Level::Off);
    }
    else if (action == "error" && !lint.empty())
    {
        const bool now = ALScriptLints::level(lua, lint) == ALScriptLints::Level::Error;
        ALScriptLints::setLevel(lua, lint, now ? ALScriptLints::Level::Warning : ALScriptLints::Level::Error);
    }
    else if (action == "settings")
    {
        LLFloaterReg::showInstance("script_studio_prefs", LLSD().with("tab", "lints"));
    }
    else if (action.compare(0, 4, "fix:") == 0)
    {
        // A copy: making it checks the script again and fills the list anew.
        const Doc::Shown* shown = shownOf(value);
        const size_t      n     = static_cast<size_t>(atoi(action.c_str() + 4));
        if (shown && n < shown->fixes.size())
        {
            const ALScriptFix fix     = shown->fixes[n];
            const U32         version = shown->fixesFor;
            applyFix(doc, fix, version);
        }
    }
    else if (action == "fix_kind")
    {
        if (const Doc::Shown* shown = shownOf(value))
        {
            const std::string key = shown->key;
            askFixAll(doc, FixPick{ key });
        }
    }
    else if (action == "fix_all")
    {
        askFixAll(doc, FixPick{});
    }
}
