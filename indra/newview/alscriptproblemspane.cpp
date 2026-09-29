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

#include "alscriptproblemspane.h"

#include "alscriptfixes.h"

#include "alcodeeditor.h"
#include "alpanefolds.h"
#include "alpanelist.h"
#include "alscriptstudioservices.h"
#include "llcheckboxctrl.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "llfloater.h"
#include "llfiltereditor.h"
#include "llmenugl.h"
#include "llpanel.h"
#include "llscrolllistitem.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <algorithm>

ALScriptProblemsPane::Made ALScriptProblemsPane::make(const Doc& doc, const ALScriptStudioServices& services, const Making& making)
{
    Made made;
    // In the theme's colours for each level, where it names them.
    const LLColor4 error_color   = doc.editor->markColor(ALCodeEditor::Mark::Error);
    const LLColor4 warning_color = doc.editor->markColor(ALCodeEditor::Mark::Warning);
    const LLColor4 note_color    = doc.editor->markColor(ALCodeEditor::Mark::Note);
    const LLColor4 runtime_color = doc.editor->markColor(ALCodeEditor::Mark::Runtime);

    const ALTextDocument& text = doc.editor->document();

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
            // In an include, or in code the preprocessor made: listed under
            // its name, not marked here.
            row.fileName = file == Doc::GENERATED ? services.words("InGeneratedCode") : making.includeName ? making.includeName(file) : file;
            made.rows.push_back(std::move(row));
            return;
        }
        made.rows.push_back(std::move(row));
        made.marks.emplace_back(line, mark);
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
        decoration.style   = mark == ALCodeEditor::Mark::Runtime || level == Doc::Level::Error ? ALCodeEditor::Decoration::Style::Squiggle
                             : level == Doc::Level::Warning                                     ? ALCodeEditor::Decoration::Style::Dashed
                                                                                                : ALCodeEditor::Decoration::Style::Dotted;
        decoration.message = origin + ": " + message;
        made.decorations.push_back(std::move(decoration));
    };

    // The analyzer's word on a line as it is now over the compiler's on
    // the text last saved: a syntax error both found is said once. Within
    // a line either way, since the two say where a statement went wrong
    // differently -- the analyzer at the missing `;`, the compiler at what
    // came after it.
    const bool analysis_current = doc.check.analysisVersion == doc.editor->document().version();
    auto       analysed_error_near = [&](S32 line) {
        for (const ALScriptProblem& problem : doc.check.analysis)
        {
            if (problem.severity == ALScriptProblem::Severity::Error && problem.file.empty() && std::abs(problem.line - line) <= 1)
            {
                return true;
            }
        }
        return false;
    };
    // The compiler's are of the text as it was saved: said so, once the
    // text has changed since, which they may no longer be true of.
    const std::string as_saved = doc.editor->isDirty() ? " " + services.words("CompilerAsSaved") : std::string();
    for (const Doc::Compiled& problem : doc.problems)
    {
        const Doc::Level level = Doc::levelOf(problem.level);
        if (analysis_current && level == Doc::Level::Error && problem.file.empty() && analysed_error_near(problem.line))
        {
            continue;
        }
        add(problem.line, problem.column, problem.hasColumn, problem.line, problem.column, Doc::markOf(level), level, services.words("OriginCompiler"),
            problem.message + as_saved, problem.file);
    }
    // The preprocessor's own word on the text as it stands, and the
    // optimizer's notes from the last run ahead of a save.
    // The optimizer's notes offer what it did as a change to the source,
    // over the text the run was made of.
    const U32  now             = doc.editor->document().version();
    const std::optional<ALScriptWeight::Target>& target = making.target;
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
            message += " " + services.words(*problem.savedBytes > 0 ? "OptimizerNoteLighter" : *problem.savedBytes < 0 ? "OptimizerNoteHeavier" : "OptimizerNoteSame", args);
        }
        add(problem.line, problem.column, true, problem.endLine, problem.endColumn, Doc::markOf(level), level,
            services.words(optimizer ? "OriginOptimizer" : "OriginPreprocessor"), message, problem.file);
        made.rows.back().key      = problem.key;
        made.rows.back().fixes    = problem.fixes;
        made.rows.back().fixesFor = version;
        if (version == now && problem.file.empty() && !problem.fixes.empty())
        {
            const bool changes = std::any_of(problem.fixes.begin(), problem.fixes.end(),
                                             [](const ALScriptFix& fix) { return fix.kind == ALScriptFix::Kind::Fix; });
            made.fixable.emplace_back(problem.line, changes);
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
    for (const ALScriptProblem& problem : doc.check.analysis)
    {
        const Doc::Shown said = Doc::analysisRow(problem, doc.language.lua, services);
        add(problem.line, problem.column, true, problem.endLine, problem.endColumn, Doc::markOf(said.level), said.level, said.origin, said.message,
            problem.file, said.lint);
        made.rows.back().key      = problem.key;
        made.rows.back().fixes    = problem.fixes;
        made.rows.back().fixesFor = doc.check.analysisVersion;
        // The gutter's word on what the line offers: a lightbulb where the
        // caret is, a round mark where a fix changes the script.
        if (analysis_current && problem.file.empty() && !problem.fixes.empty())
        {
            const bool changes = std::any_of(problem.fixes.begin(), problem.fixes.end(),
                                             [](const ALScriptFix& fix) { return fix.kind == ALScriptFix::Kind::Fix; });
            made.fixable.emplace_back(problem.line, changes);
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
            message += " " + services.words("RepeatedTimes", args);
        }
        add(line, column, problem.column >= 0, line, column, ALCodeEditor::Mark::Runtime, Doc::Level::Error, services.words("OriginRuntime"), message,
            problem.file);
    }
    if (!doc.check.definitionsError.empty())
    {
        Doc::Shown row;
        row.level   = Doc::Level::Note;
        row.origin  = services.words("OriginDefinitions");
        row.message = doc.check.definitionsError;
        made.rows.push_back(std::move(row));
    }
    // Code heavier than its target runs a script in: a warning, since
    // the number is the studio's reckoning of what the region compiles --
    // an estimate for Mono, and of the text before the optimizer where it
    // runs after -- and not the region's word. Past four fifths of it, as
    // the trailer colours it, a warning too: what is left is all the
    // script has to run in. Not where the optimizer is still to run, which
    // only makes it lighter.
    const std::optional<ALScriptWeight>& weighed = doc.weighing.weight;
    const bool current = weighed && doc.weighing.version == doc.editor->document().version();
    const bool over    = current && weighed->total > weighed->limit;
    const bool nearing = current && !over && doc.weighing.exact && weighed->total * 5 > weighed->limit * 4;
    if (over || nearing)
    {
        const ALScriptWeight&      weight = *doc.weighing.weight;
        LLStringUtil::format_map_t args;
        args["[SIZE]"]   = llformat("%.1f", (F64)weight.total / 1024.0);
        args["[LIMIT]"]  = std::to_string(weight.limit / 1024);
        args["[LEFT]"]   = nearing ? llformat("%.1f", (F64)(weight.limit - weight.total) / 1024.0) : std::string();
        args["[TARGET]"] = ALScriptWeight::nameOf(weight.target);
        Doc::Shown row;
        row.level   = Doc::Level::Warning;
        row.origin  = services.words("OriginWeight");
        const char* said = nearing         ? (weight.estimate ? "WeightNearEstimate" : "WeightNear")
                           : weight.estimate ? "WeightOverEstimate"
                           : !doc.weighing.exact ? "WeightOverBefore"
                                                 : "WeightOver";
        row.message      = services.words(said, args);
        made.rows.push_back(std::move(row));
    }
    std::stable_sort(made.rows.begin(), made.rows.end(), [](const Doc::Shown& a, const Doc::Shown& b) {
        // The script's own first, then each include's together.
        if (a.file != b.file)
        {
            return a.file < b.file;
        }
        return a.line != b.line ? a.line < b.line : a.column < b.column;
    });
    return made;
}

static LLPanelInjector<ALScriptProblemsPane> t_script_studio_problems("script_studio_problems");

ALScriptProblemsPane::ALScriptProblemsPane(const LLPanel::Params& params) : LLPanel(params) {}

bool ALScriptProblemsPane::postBuild()
{
    mList     = getChild<ALPaneList>("problems");
    mErrors   = getChild<LLCheckBoxCtrl>("problems_errors");
    mWarnings = getChild<LLCheckBoxCtrl>("problems_warnings");
    mNotes    = getChild<LLCheckBoxCtrl>("problems_notes");
    mFixable  = getChild<LLCheckBoxCtrl>("problems_fixable");
    mScope    = getChild<LLComboBox>("problems_scope");
    mOrigin   = getChild<LLComboBox>("problems_origin");
    mFilter   = getChild<LLFilterEditor>("problems_filter");
    // The window this is a tab of, found through the view tree, as what
    // the tab asks of it.
    LLFloater* window = getParentByType<LLFloater>();
    mServices         = dynamic_cast<ALScriptStudioServices*>(window);
    mWindow           = dynamic_cast<Window*>(window);
    if (!mServices || !mWindow)
    {
        LL_WARNS() << "The Problems tab is not in a Script Studio window" << LL_ENDL;
        return true;
    }
    // A row chosen shows its place and keeps the keyboard in the list, so
    // that the arrows walk on through them; a double-click goes there.
    mList->setCommitCallback([this](LLUICtrl*, const LLSD&) { choose(false); });
    // Return and a double-click go to the place chosen, to type there, and
    // escape goes back to the script without going anywhere. Its copying is
    // its own menu's.
    mList->setGo([this]() { choose(true); });
    mList->setBack([this]() { mServices->revealed(mList, true); });
    mList->setRightMouseDownCallback([this](LLUICtrl*, S32 x, S32 y, MASK) { showMenu(x, y); });
    // The pane's filters: whose, which levels, which source, which words.
    mOrigin->add(mServices->words("OriginAny"), LLSD(""));
    for (const char* origin : { "OriginParser", "OriginTypes", "OriginLint", "OriginCompiler", "OriginPreprocessor", "OriginOptimizer", "OriginRuntime", "OriginDefinitions",
                                "OriginWeight" })
    {
        mOrigin->add(mServices->words(origin), LLSD(mServices->words(origin)));
    }
    mOrigin->selectFirstItem();
    mScope->selectFirstItem();
    for (LLUICtrl* filter : { static_cast<LLUICtrl*>(mErrors), static_cast<LLUICtrl*>(mWarnings), static_cast<LLUICtrl*>(mNotes),
                              static_cast<LLUICtrl*>(mFixable), static_cast<LLUICtrl*>(mScope), static_cast<LLUICtrl*>(mOrigin),
                              static_cast<LLUICtrl*>(mFilter) })
    {
        filter->setCommitCallback([this](LLUICtrl*, const LLSD&) {
            fill(listed());
            mWindow->problemFiltersChanged();
        });
    }
    // Sorted by a column's title: the problems within their scripts and
    // includes, each heading over its own; a line as a number, and a
    // problem's level as how bad it is.
    mList->setGrouping([](const LLScrollListItem* item, S32& group, bool& heading) {
        group   = item->getValue()["group"].asInteger();
        heading = item->getValue().has("heading");
    });
    mList->setComparison([this](S32 column, const LLScrollListItem* a, const LLScrollListItem* b) {
        const Doc::Shown* x = shownOf(a->getValue());
        const Doc::Shown* y = shownOf(b->getValue());
        if (!x || !y)
        {
            return x ? -1 : y ? 1 : 0;
        }
        const auto place = [&]() {
            return x->line != y->line ? (x->line < y->line ? -1 : 1) : x->column < y->column ? -1 : x->column > y->column ? 1 : 0;
        };
        const auto rank = [](const Doc::Shown& one) { return one.level == Doc::Level::Error ? 0 : one.level == Doc::Level::Warning ? 1 : 2; };
        S32 said = 0;
        switch (column)
        {
            case 0: said = rank(*x) - rank(*y); break;
            case 1: said = LLStringUtil::compareDict(x->message, y->message); break;
            case 2: said = LLStringUtil::compareDict(x->origin, y->origin); break;
            default: break;
        }
        return said != 0 ? said : place();
    });
    // A row's whole story, as the pointer rests on it.
    mList->setRowTip([this](const LLScrollListItem* item) { return tipOf(item); });
    return true;
}



ALScriptProblemsPane::~ALScriptProblemsPane()
{
    // A menu still open calls into this pane, which is going: it goes
    // first. The menus live in the viewer's menu holder, not here.
    if (LLContextMenu* open = mMenu.get())
    {
        open->die();
    }
}

void ALScriptProblemsPane::changed(const Doc& doc)
{
    mStore.replace(doc.id, doc.shown);
    // The list, where it lists this script's: the one it is about, or
    // every one open.
    Doc* shown = listed();
    if (shown && (shown == &doc || everyScript()))
    {
        fill(shown);
    }
}

void ALScriptProblemsPane::forget(const std::string& id)
{
    mStore.forget(id);
}

void ALScriptProblemsPane::closed(const std::string& id)
{
    mStore.forget(id);
    if (id == mShownFor)
    {
        mShownFor.clear();
    }
    if (id == mRowsFor)
    {
        mRowsFor.clear();
    }
}

void ALScriptProblemsPane::rekey(const std::string& from, const std::string& to)
{
    if (mShownFor == from)
    {
        mShownFor = to;
    }
    if (mRowsFor == from)
    {
        mRowsFor = to;
    }
}

ALScriptProblemsPane::Checked& ALScriptProblemsPane::checkedEntry(const ALScriptRef& ref, const std::string& name, bool lua, const std::string& where)
{
    const std::string id    = "checked:" + ref.object.asString() + ":" + ref.item.asString();
    auto              found = std::find_if(mChecked.begin(), mChecked.end(), [&id](const Checked& one) { return one.id == id; });
    Checked&          one   = found != mChecked.end() ? *found : mChecked.emplace_back();
    one.id                  = id;
    one.ref                 = ref;
    one.name                = name;
    one.where               = where;
    one.lua                 = lua;
    return one;
}

void ALScriptProblemsPane::relist(Checked& one)
{
    one.rows = one.analysed;
    one.rows.insert(one.rows.end(), one.compiled.begin(), one.compiled.end());
    mStore.replace(one.id, one.rows);
    if (everyScript())
    {
        fill(listed());
    }
}

void ALScriptProblemsPane::checkedScript(const ALScriptRef& ref, const std::string& name, bool lua, const std::vector<Doc::Shown>& rows,
                                         const std::string& where)
{
    Checked& one = checkedEntry(ref, name, lua, where);
    one.checked  = true;
    one.analysed = rows;
    relist(one);
}

void ALScriptProblemsPane::compiledScript(const ALScriptRef& ref, const std::string& name, bool lua, const std::vector<Doc::Shown>& rows,
                                          const std::string& where)
{
    const auto found = std::find_if(mChecked.begin(), mChecked.end(), [&ref](const Checked& one) { return one.ref == ref; });
    // Clean, and nothing listed of it: nothing to list.
    if (rows.empty() && found == mChecked.end())
    {
        return;
    }
    Checked& one = checkedEntry(ref, name, lua, where);
    one.compiled = rows;
    if (!one.checked && one.compiled.empty())
    {
        forgetChecked(ref);
        return;
    }
    relist(one);
}

void ALScriptProblemsPane::forgetChecked(const ALScriptRef& ref)
{
    const auto found = std::find_if(mChecked.begin(), mChecked.end(), [&ref](const Checked& one) { return one.ref == ref; });
    if (found == mChecked.end())
    {
        return;
    }
    mStore.forget(found->id);
    mChecked.erase(found);
    if (everyScript())
    {
        fill(listed());
    }
}

void ALScriptProblemsPane::clearChecked()
{
    for (const Checked& one : mChecked)
    {
        mStore.forget(one.id);
    }
    mChecked.clear();
    fill(listed());
}

void ALScriptProblemsPane::showEveryScript()
{
    if (!everyScript())
    {
        mScope->selectByValue("all");
        mWindow->problemFiltersChanged();
    }
    fill(listed());
}

std::vector<const ALScriptProblemsPane::Checked*> ALScriptProblemsPane::checkedFor()
{
    std::vector<const Checked*> out;
    if (!everyScript())
    {
        return out;
    }
    for (const Checked& one : mChecked)
    {
        // Opened since, its tab's are what is listed.
        if (!mServices->findDoc(one.ref))
        {
            out.push_back(&one);
        }
    }
    return out;
}

bool ALScriptProblemsPane::everyScript() const
{
    return mScope->getValue().asString() == "all";
}

ALScriptProblemsPane::store_t::Query ALScriptProblemsPane::query(const std::string& id) const
{
    store_t::Query query;
    query.file     = id;
    query.errors   = mErrors->get();
    query.warnings = mWarnings->get();
    query.notes    = mNotes->get();
    query.fixable  = mFixable->get();
    query.rule     = mOrigin->getValue().asString();
    query.text     = mFilter->getText();
    LLStringUtil::trim(query.text);
    return query;
}

std::vector<const ALScriptProblemsPane::Doc*> ALScriptProblemsPane::docsFor(const Doc* doc)
{
    std::vector<const Doc*> docs;
    if (!doc)
    {
        return docs;
    }
    docs.push_back(doc);
    if (everyScript())
    {
        for (const Doc* other : mServices->openDocs())
        {
            if (other != doc && !other->notecard)
            {
                docs.push_back(other);
            }
        }
    }
    return docs;
}

ALScriptProblemsPane::Doc* ALScriptProblemsPane::listed()
{
    Doc* doc = mShownFor.empty() ? nullptr : mServices->findDoc(mShownFor);
    return doc ? doc : mServices->frontDoc();
}

void ALScriptProblemsPane::choose(bool to_editor)
{
    LLScrollListItem* item = mList->getFirstSelected();
    if (!item)
    {
        return;
    }
    const LLSD&       value   = item->getValue();
    const Doc::Shown* problem = shownOf(value);
    const std::string owner   = value["owner"].asString();
    const Checked*    checked = problem ? checkedOf(owner) : nullptr;
    if (!problem || (!checked && !mServices->findDoc(owner)))
    {
        return;
    }
    Place place;
    if (checked)
    {
        place.ref  = checked->ref;
        place.name = checked->name;
    }
    else
    {
        place.doc = owner;
    }
    place.file      = problem->file;
    place.fileName  = problem->fileName;
    place.line      = problem->line;
    place.column    = problem->column;
    place.hasColumn = problem->hasColumn;
    place.endLine   = problem->endLine;
    place.endColumn = problem->endColumn;
    mWindow->problemChosen(place, to_editor);
}

void ALScriptProblemsPane::saveState(LLSD& state) const
{
    state["problem_levels"] = LLSD::emptyArray().with(0, mErrors->get()).with(1, mWarnings->get()).with(2, mNotes->get()).with(3, mFixable->get());
    state["problem_scope"]  = mScope->getValue().asString();
    state["problem_origin"] = mOrigin->getValue().asString();
}

void ALScriptProblemsPane::readState(const LLSD& state)
{
    if (state.has("problem_levels"))
    {
        const LLSD& levels = state["problem_levels"];
        mErrors->set(levels[0].asBoolean());
        mWarnings->set(levels[1].asBoolean());
        mNotes->set(levels[2].asBoolean());
        mFixable->set(levels.size() > 3 && levels[3].asBoolean());
    }
    if (state.has("problem_scope"))
    {
        mScope->selectByValue(state["problem_scope"]);
    }
    if (state.has("problem_origin") && !mOrigin->selectByValue(state["problem_origin"]))
    {
        mOrigin->selectFirstItem();
    }
}

void ALScriptProblemsPane::fill(const Doc* doc)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    mShownFor = doc ? doc->id : std::string();
    // How many of each there are before the filters: what the level
    // boxes and the tab say, which are seen with the list out of sight.
    const std::vector<const Doc*>     docs    = docsFor(doc);
    const std::vector<const Checked*> checked = checkedFor();
    S32                               errors = 0, warnings = 0, notes = 0, fixable = 0;
    const auto                        count  = [&](const std::vector<Doc::Shown>& rows) {
        for (const Doc::Shown& shown : rows)
        {
            errors += shown.level == Doc::Level::Error ? 1 : 0;
            warnings += shown.level == Doc::Level::Warning ? 1 : 0;
            notes += shown.level == Doc::Level::Note ? 1 : 0;
            fixable += Doc::ProblemTraits::fixable(shown) ? 1 : 0;
        }
    };
    for (const Doc* each : docs)
    {
        count(each->shown);
    }
    for (const Checked* each : checked)
    {
        count(each->rows);
    }
    const S32 held = errors + warnings + notes;
    const auto label = [this](LLCheckBoxCtrl* box, const char* name, S32 count) {
        LLStringUtil::format_map_t args;
        args["[COUNT]"] = std::to_string(count);
        const std::string said = count > 0 ? mServices->words(std::string(name) + "Count", args) : mServices->words(name);
        if (box->getLabel() != said)
        {
            box->setLabel(said);
        }
    };
    label(mErrors, "FilterErrors", errors);
    label(mWarnings, "FilterWarnings", warnings);
    label(mNotes, "FilterNotes", notes);
    label(mFixable, "FilterFixable", fixable);
    layoutFilters();
    mHeld = held;
    mWindow->problemCountsChanged();
    // The rows only while the list can be seen; else once it is (pump).
    if (!ALPaneFolds::inSight(this))
    {
        mRowsWanted = true;
        return;
    }
    listRows(doc);
}

void ALScriptProblemsPane::pump()
{
    if (mRowsWanted && ALPaneFolds::inSight(this))
    {
        listRows(listed());
    }
}

void ALScriptProblemsPane::listRows(const Doc* doc)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    mRowsWanted = false;
    mRowsFor    = doc ? doc->id : std::string();
    mList->setEmpty(LLStringUtil::null, LLStringUtil::null);
    const std::vector<const Doc*>     docs    = docsFor(doc);
    const std::vector<const Checked*> checked = checkedFor();
    const S32                         held    = mHeld;
    std::vector<ALPaneList::Row>      rows;
    if (!doc && checked.empty())
    {
        mList->setRows(std::move(rows));
        return;
    }

    // What the filters take, by script and, within one, by the file it is
    // in: the script's own, then each include's; a script no tab holds, an
    // object's check reached, after the open ones. No more than ROWS_MOST
    // of them, the rest counted.
    struct Group
    {
        const Doc*                     doc     = nullptr;
        const Checked*                 checked = nullptr;
        // Whose they are, as the store knows them.
        std::string                    owner;
        std::string                    file;
        std::string                    fileName;
        std::vector<const Doc::Shown*> rows;
        // Where each is among its owner's, as the store has them.
        std::vector<size_t>            index;
    };
    std::vector<Group> groups;
    S32                listed   = 0;
    S32                unlisted = 0;
    const auto         take     = [&](const Doc* each, const Checked* one, const std::string& owner) {
        const auto selected = mStore.select(query(owner));
        for (size_t i = 0; i < selected.found.size(); ++i)
        {
            const Doc::Shown* problem = selected.found[i];
            if (listed >= ROWS_MOST)
            {
                ++unlisted;
                continue;
            }
            if (groups.empty() || groups.back().doc != each || groups.back().checked != one || groups.back().file != problem->file)
            {
                groups.push_back({ each, one, owner, problem->file, problem->fileName, {}, {} });
            }
            groups.back().rows.push_back(problem);
            groups.back().index.push_back(selected.index[i]);
            ++listed;
        }
    };
    for (const Doc* each : docs)
    {
        take(each, nullptr, each->id);
    }
    for (const Checked* one : checked)
    {
        take(nullptr, one, one->id);
    }
    // Where a tab is open, its editor paints a row's mark in the theme's
    // colour; a script no tab holds takes the same.
    const ALCodeEditor* painter = nullptr;
    for (const Doc* each : mServices->openDocs())
    {
        painter = painter ? painter : each->editor;
    }

    static const LLUIColor ink = LLUIColorTable::instance().getColor("ScrollUnselectedColor", LLColor4::white);
    const bool all      = docs.size() + checked.size() > 1 || everyScript();
    // Whose they are, over them, wherever that is not plain: more than one
    // script's or file's, or another script's than the one in front, which
    // a row followed into an include leaves the list on.
    const bool elsewhere = doc != mServices->frontDoc();
    const bool headings  = all || groups.size() > 1 || elsewhere;
    const std::string runtime = mServices->words("OriginRuntime");
    const auto cell = [](const char* column, const std::string& value, const char* type = "text") {
        LLScrollListCell::Params one;
        one.column = column;
        one.type   = type;
        one.value  = value;
        return one;
    };
    // A row goes on being the same row while it says the same of the same
    // file -- not where, which an edit above it moves -- and one said twice
    // is told apart by which time it is.
    boost::unordered_flat_map<std::string, S32, ll::string_hash, std::equal_to<>> said;
    rows.reserve(static_cast<size_t>(listed) + groups.size() + 2);
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
                counts.push_back(mServices->counted("ProblemErrors", group_errors));
            }
            if (group_warnings > 0)
            {
                counts.push_back(mServices->counted("ProblemWarnings", group_warnings));
            }
            if (group_notes > 0)
            {
                counts.push_back(mServices->counted("ProblemNotes", group_notes));
            }
            LLStringUtil::format_map_t args;
            args["[NAME]"]   = group.doc ? group.doc->name : group.checked->name;
            args["[FILE]"]   = group.fileName;
            args["[OBJECT]"] = group.checked ? group.checked->where : std::string();
            std::string name = !group.file.empty() ? mServices->words(all ? "ProblemsIncludedBy" : "ProblemsIncluded", args)
                               : group.doc     ? group.doc->name
                                               : mServices->words("ProblemsChecked", args);
            for (size_t i = 0; i < counts.size(); ++i)
            {
                name += (i == 0 ? "   \xC2\xB7   " : ", ") + counts[i];
            }
            ALPaneList::Row heading;
            heading.key                = "#" + group.owner + "\x1f" + group.file;
            heading.value["heading"]   = true;
            heading.value["group"]     = static_cast<S32>(group_index);
            heading.enabled            = false;
            LLScrollListCell::Params title = cell("message", name);
            title.color                = ink.get();
            title.font                 = LLFontGL::getFontSansSerifSmallBold();
            heading.cells              = { cell("icon", group.doc ? mWindow->problemIcon(*group.doc, group.file) : mWindow->scriptIcon(group.checked->lua, group.file), "icon"),
                                           title };
            rows.push_back(std::move(heading));
        }
        for (size_t k = 0; k < group.rows.size(); ++k)
        {
            const Doc::Shown* problem = group.rows[k];
            // The row carries whose it is and where the store has it: what
            // it says is the store's, looked up as it is asked for.
            ALPaneList::Row row;
            row.key = group.owner + "\x1f" + Doc::levelName(problem->level) + "\x1f" + problem->origin + "\x1f" + problem->file + "\x1f" +
                      problem->message;
            if (const S32 times = said[row.key]++; times > 0)
            {
                row.key += "\x1f" + std::to_string(times);
            }
            row.value["group"] = static_cast<S32>(group_index);
            row.value["owner"] = group.owner;
            row.value["index"] = static_cast<S32>(group.index[k]);
            const ALCodeEditor::Mark mark = problem->origin == runtime ? ALCodeEditor::Mark::Runtime : Doc::markOf(problem->level);
            LLScrollListCell::Params icon = cell("icon",
                                                 problem->level == Doc::Level::Error     ? "Problem_Error"
                                                 : problem->level == Doc::Level::Warning ? "Problem_Warning"
                                                                                         : "Problem_Note",
                                                 "icon");
            if (const ALCodeEditor* paints = group.doc ? group.doc->editor : painter)
            {
                icon.color = paints->markColor(mark);
            }
            row.cells = { icon, cell("message", problem->message), cell("source", problem->origin), cell("line", whereOf(group.doc, *problem)) };
            rows.push_back(std::move(row));
        }
    }
    // What the list leaves out, said under what it lists: past what it
    // lists at most, and what the filters hide.
    const auto more = [&](const char* key, const std::string& words) {
        ALPaneList::Row row;
        row.key              = key;
        row.value["heading"] = true;
        row.value["group"]   = static_cast<S32>(groups.size());
        row.enabled          = false;
        row.cells            = { cell("icon", std::string(), "icon"), cell("message", words) };
        rows.push_back(std::move(row));
    };
    if (unlisted > 0)
    {
        more("#unlisted", mServices->counted("ProblemsUnlisted", unlisted));
    }
    if (listed > 0 && held > listed + unlisted)
    {
        more("#hidden", mServices->counted("ProblemsHidden", held - listed - unlisted));
    }
    mList->setRows(std::move(rows));
    if (held == 0)
    {
        bool current = true;
        for (const Doc* each : docs)
        {
            current = current && each->loaded && each->check.analysisVersion == each->editor->document().version();
        }
        LLStringUtil::format_map_t named;
        named["[NAME]"] = doc ? doc->name : std::string();
        mList->setEmpty(!current     ? std::string()
                        : all       ? mServices->words("NoProblemsOpen")
                        : elsewhere ? mServices->words("NoProblemsIn", named)
                                    : mServices->words("NoProblems"),
                        LLStringUtil::null);
    }
    else if (listed == 0)
    {
        mList->setEmpty(mServices->counted("ProblemsAllHidden", held), LLStringUtil::null);
    }
}

std::string ALScriptProblemsPane::whereOf(const Doc* doc, const Doc::Shown& problem) const
{
    // The column as the status line counts it, where the problem is in the
    // text open here; an include's, or a script's no tab holds, is its
    // byte, for want of its text.
    const ALCodeEditor* editor = doc ? doc->editor : nullptr;
    const S32           column = editor && problem.file.empty() && problem.line < editor->document().lineCount()
                                     ? editor->document().displayColumn(ALTextPos(problem.line, problem.column), editor->getTabWidth())
                                     : problem.column;
    return problem.hasColumn ? llformat("%d:%d", problem.line + 1, column + 1) : llformat("%d", problem.line + 1);
}

const ALScriptProblemsPane::Doc::Shown* ALScriptProblemsPane::shownOf(const LLSD& value) const
{
    if (!value.isMap() || value.has("heading") || !value.has("owner"))
    {
        return nullptr;
    }
    return mStore.at(value["owner"].asString(), static_cast<size_t>(value["index"].asInteger()));
}

const ALScriptProblemsPane::Checked* ALScriptProblemsPane::checkedOf(const std::string& owner) const
{
    const auto found = std::find_if(mChecked.begin(), mChecked.end(), [&owner](const Checked& one) { return one.id == owner; });
    return found != mChecked.end() ? &*found : nullptr;
}

std::string ALScriptProblemsPane::tipOf(const LLScrollListItem* item) const
{
    const LLSD&       value   = item->getValue();
    const Doc::Shown* problem = shownOf(value);
    if (!problem)
    {
        return std::string();
    }
    // The whole of it, which a column cuts at its edge: its level, where,
    // what it says, and what would put it right, which its menu offers.
    const std::string owner = value["owner"].asString();
    const Doc*        doc   = mServices->findDoc(owner);
    const Checked*    one   = doc ? nullptr : checkedOf(owner);
    const std::string level = mServices->words(problem->level == Doc::Level::Error     ? "LevelError"
                                               : problem->level == Doc::Level::Warning ? "LevelWarning"
                                                                                       : "LevelNote");
    std::string tip = level + "   \xC2\xB7   " + (!problem->file.empty() ? problem->fileName : doc ? doc->name : one ? one->name : std::string()) + ":" +
                      whereOf(doc, *problem) + "\n" + problem->message;
    for (const ALScriptFix& fix : problem->fixes)
    {
        if (fix.kind == ALScriptFix::Kind::Fix)
        {
            LLStringUtil::format_map_t fix_args;
            fix_args["[TITLE]"] = fix.title;
            tip += "\n" + mServices->words("ProblemFixTip", fix_args);
        }
    }
    return tip;
}

void ALScriptProblemsPane::layoutFilters()
{
    // The level boxes as wide as what they say, the rest after them, the
    // words' box taking what is left.
    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    S32             left = 4;
    for (LLCheckBoxCtrl* box : { mErrors, mWarnings, mNotes, mFixable })
    {
        const S32 width = 24 + font->getWidth(box->getLabel());
        box->reshape(width, box->getRect().getHeight());
        box->setOrigin(left, box->getRect().mBottom);
        left += width + 4;
    }
    left += 6;
    for (LLUICtrl* combo : { static_cast<LLUICtrl*>(mScope), static_cast<LLUICtrl*>(mOrigin) })
    {
        combo->setOrigin(left, combo->getRect().mBottom);
        left += combo->getRect().getWidth() + 6;
    }
    left += 2;
    const LLRect filter = mFilter->getRect();
    const S32    right  = mFilter->getParent() ? mFilter->getParent()->getRect().getWidth() - 4 : filter.mRight;
    const S32    width  = llmax(60, right - left);
    if (filter.mLeft != left || filter.getWidth() != width)
    {
        mFilter->reshape(width, filter.getHeight());
        mFilter->setOrigin(left, filter.mBottom);
    }
}

void ALScriptProblemsPane::selectFirstError(bool checkers_only)
{
    // Listed now, seen or not: the row is chosen in the list.
    if (mRowsWanted)
    {
        listRows(listed());
    }
    mList->updateSort();
    const std::vector<LLScrollListItem*> rows     = mList->getAllData();
    const std::string                    compiler = mServices->words("OriginCompiler");
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const Doc::Shown* problem = shownOf(rows[i]->getValue());
        if (problem && problem->level == Doc::Level::Error && (!checkers_only || problem->origin != compiler))
        {
            mList->selectNthItem(static_cast<S32>(i));
            mList->scrollToShowSelected();
            choose(true);
            return;
        }
    }
}

ALScriptProblemsPane::Doc* ALScriptProblemsPane::chosenDoc() const
{
    LLScrollListItem* item = mList->getFirstSelected();
    return item && item->getValue().isMap() ? mServices->findDoc(item->getValue()["owner"].asString()) : nullptr;
}

const ALScriptProblemsPane::Doc::Shown* ALScriptProblemsPane::chosenShown() const
{
    LLScrollListItem* item = mList->getFirstSelected();
    const Doc*        doc  = chosenDoc();
    if (!item || !doc)
    {
        return nullptr;
    }
    return shownOf(item->getValue());
}

std::string ALScriptProblemsPane::chosenLint(bool& lua) const
{
    // The lint the chosen problem is, in its script's language, where it
    // is one there is a choice about.
    const Doc*        doc  = chosenDoc();
    LLScrollListItem* item = mList->getFirstSelected();
    if (!doc || !item)
    {
        return std::string();
    }
    lua                       = doc->language.lua;
    const Doc::Shown* problem = shownOf(item->getValue());
    const std::string lint    = problem ? problem->lint : std::string();
    return !lint.empty() && mWindow->isLint(lua, lint) ? lint : std::string();
}

bool ALScriptProblemsPane::enabled(const std::string& what) const
{
    if (what == "copy" || what == "copy_where" || what == "copy_all" || what == "settings")
    {
        return true;
    }
    if (what == "clear_runtime")
    {
        const Doc* doc = chosenDoc();
        return doc && !doc->runtime.empty();
    }
    bool lua = false;
    return !chosenLint(lua).empty();
}

bool ALScriptProblemsPane::lintIsError() const
{
    bool              lua  = false;
    const std::string lint = chosenLint(lua);
    return !lint.empty() && mWindow->lintLevel(lua, lint) == ALScriptLints::Level::Error;
}

bool ALScriptProblemsPane::fixShown(const std::string& which) const
{
    // Every problem of its kind at once, where there is more than one to
    // put right; and Fix All where it would only say what it leaves, too,
    // so that it can.
    const Doc::Shown* shown = chosenShown();
    const Doc*        doc   = chosenDoc();
    if (!shown || !doc || !doc->modifiable)
    {
        return false;
    }
    if (which == "kind")
    {
        // A problem of no kind -- the compiler's -- has none to fix along
        // with it: an empty kind would be every kind.
        return !shown->key.empty() && doc->pickFixes(Doc::FixPick{ shown->key }).size() > 1;
    }
    size_t left = 0;
    return which == "all" && doc->pickFixes(Doc::FixPick{}, &left).size() + left > 1;
}

void ALScriptProblemsPane::showMenu(S32 x, S32 y)
{
    if (!LLMenuGL::sMenuContainer)
    {
        return;
    }
    // The row under the mouse is the one the menu is about; a heading is
    // about no problem.
    LLScrollListItem* hit = mList->hitItem(x, y);
    if (!hit || !hit->getEnabled())
    {
        return;
    }
    mList->selectItemAt(x, y, MASK_NONE);
    if (LLContextMenu* old = mMenu.get())
    {
        old->die();
        mMenu.markDead();
    }
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
    commit.add("Problem.Action", [this](LLUICtrl*, const LLSD& param) { act(param.asString()); });
    enable.add("Problem.Enable", [this](LLUICtrl*, const LLSD& param) { return enabled(param.asString()); });
    enable.add("Problem.Check", [this](LLUICtrl*, const LLSD&) { return lintIsError(); });
    enable.add("Problem.FixVisible", [this](LLUICtrl*, const LLSD& param) { return fixShown(param.asString()); });
    LLContextMenu* menu = LLUICtrlFactory::createFromFile<LLContextMenu>("menu_script_studio_problem.xml", LLMenuGL::sMenuContainer,
                                                                          LLMenuHolderGL::child_registry_t::instance());
    if (!menu)
    {
        return;
    }
    mMenu = menu->getHandle();
    const Doc::Shown* shown = chosenShown();
    const Doc*        doc   = chosenDoc();
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
            item->setClickCallback([this, action](LLUICtrl*, const LLSD&) { act(action); });
            menu->insert(static_cast<S32>(n), item);
        }
    }
    menu->setItemVisible("fix_separator", shown && doc && doc->modifiable && !shown->fixes.empty());
    menu->show(x, y);
    LLMenuGL::showPopup(mList, menu, x, y);
}

void ALScriptProblemsPane::act(const std::string& action)
{
    LLScrollListItem* item = mList->getFirstSelected();
    Doc*              of   = chosenDoc();
    if (!item || !of)
    {
        return;
    }
    Doc&              doc     = *of;
    const LLSD&       value   = item->getValue();
    const Doc::Shown* chosen  = shownOf(value);
    const std::string lint    = chosen ? chosen->lint : std::string();
    const bool        lua     = doc.language.lua;
    // A problem as a line of text: where, what level, from whom, what.
    const auto as_text = [](const Doc& whose, const Doc::Shown& one) {
        const std::string name  = !one.fileName.empty() ? one.fileName : whose.name;
        const std::string where = one.hasColumn ? llformat("%s:%d:%d", name.c_str(), one.line + 1, one.column + 1) : llformat("%s:%d", name.c_str(), one.line + 1);
        const std::string level = one.level == Doc::Level::Error ? "error" : one.level == Doc::Level::Warning ? "warning" : "note";
        return where + ": " + level + ": " + one.message + " [" + one.origin + "]";
    };
    const auto copy = [](const std::string& text) { LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size())); };
    if (action == "copy")
    {
        copy(chosen ? chosen->message : std::string());
    }
    else if (action == "copy_where")
    {
        copy(chosen ? as_text(doc, *chosen) : std::string());
    }
    else if (action == "copy_all")
    {
        // Every problem the list shows, as a compiler lists them.
        std::string all;
        S32         count = 0;
        for (LLScrollListItem* row : mList->getAllData())
        {
            const Doc::Shown* one   = shownOf(row->getValue());
            const Doc*        whose = one ? mServices->findDoc(row->getValue()["owner"].asString()) : nullptr;
            if (whose)
            {
                all += as_text(*whose, *one) + "\n";
                ++count;
            }
        }
        copy(all);
        mServices->setStatus(mServices->counted("ProblemsCopied", count));
    }
    else if (action == "clear_runtime")
    {
        // What the script said as it ran, let go of until it says it again.
        doc.runtime.clear();
        doc.runtimeHeld.clear();
        mWindow->runtimeCleared(doc);
        mWindow->refreshProblems(doc);
    }
    else if (action == "off" && !lint.empty())
    {
        // The scripts are checked again as the setting changes.
        mWindow->setLintLevel(lua, lint, ALScriptLints::Level::Off);
    }
    else if (action == "error" && !lint.empty())
    {
        const bool now = mWindow->lintLevel(lua, lint) == ALScriptLints::Level::Error;
        mWindow->setLintLevel(lua, lint, now ? ALScriptLints::Level::Warning : ALScriptLints::Level::Error);
    }
    else if (action == "settings")
    {
        mWindow->showLintSettings();
    }
    else if (action.compare(0, 4, "fix:") == 0)
    {
        // A copy: making it checks the script again and fills the list anew.
        const Doc::Shown* shown = chosenShown();
        const size_t      n     = static_cast<size_t>(atoi(action.c_str() + 4));
        if (shown && n < shown->fixes.size())
        {
            const ALScriptFix fix     = shown->fixes[n];
            const U32         version = shown->fixesFor;
            mWindow->applyFix(doc, fix, version);
        }
    }
    else if (action == "fix_kind")
    {
        if (const Doc::Shown* shown = chosenShown(); shown && !shown->key.empty())
        {
            const std::string key = shown->key;
            mWindow->fixAllOfKind(doc, key);
        }
    }
    else if (action == "fix_all")
    {
        mWindow->fixAllOfKind(doc, std::string());
    }
}
