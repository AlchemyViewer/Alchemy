/**
 * @file alfloaterscriptstudio.cpp
 * @brief Script Studio: the scripts open in the viewer, edited, saved and compiled in one window.
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
#include "alscriptpreprocessor.h"
#include "aljumpbar.h"
#include "aloutputlist.h"
#include "altabstrip.h"
#include "llagent.h"
#include "lldate.h"
#include "lltimer.h"
#include "llsyntaxid.h"
#include "llversioninfo.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "lldirpicker.h"
#include "lleditmenuhandler.h"
#include "llfocusmgr.h"
#include "llfilepicker.h"
#include "llfloaterreg.h"
#include "llinventorymodel.h"
#include "lllayoutstack.h"
#include "llmenugl.h"
#include "llnotificationsutil.h"
#include "llscrolllistctrl.h"
#include "llselectmgr.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltrans.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llviewermenufile.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"

#include <algorithm>
#include <ctime>
#include <fstream>

namespace
{
    // How long after the last keystroke the analyzers are asked.
    const F64 ANALYSIS_DELAY = 0.35;
    // How often the explorer looks at what is selected in world.
    const F64 EXPLORER_POLL = 1.0;
    // How long a second save goes ahead over what the check found.
    const F64 SAVE_ANYWAY = 10.0;
}

namespace
{
    // Whether the region a script lives in runs Lua, which is whether the
    // Lua targets are offered.
    bool luaEnabledFor(const ALScriptRef& ref)
    {
        LLViewerRegion* region = nullptr;
        if (LLViewerObject* object = gObjectList.findObject(ref.object))
        {
            region = object->getRegion();
        }
        if (!region)
        {
            region = gAgent.getRegion();
        }
        if (region && region->simulatorFeaturesReceived())
        {
            LLSD features;
            region->getSimulatorFeatures(features);
            return features["LuaScriptsEnabled"].asBoolean();
        }
        return false;
    }

    std::string readWholeFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::string   text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return text;
    }

    ALTextRange rangeOf(const ALScriptSpan& span)
    {
        return ALTextRange(ALTextPos(span.line, span.column), ALTextPos(span.endLine, span.endColumn));
    }

    // Whether a span holds a position, its ends included.
    bool holds(const ALScriptSpan& span, const ALTextPos& pos)
    {
        const ALTextPos begin(span.line, span.column);
        const ALTextPos end(span.endLine, span.endColumn);
        return begin <= pos && pos <= end;
    }

    // Whether a span lies within another.
    bool within(const ALScriptSpan& inner, const ALScriptSpan& outer)
    {
        return holds(outer, ALTextPos(inner.line, inner.column)) && holds(outer, ALTextPos(inner.endLine, inner.endColumn));
    }

    // The time of day something was said, from seconds since the epoch.
    std::string clockOf(F64 seconds_since_epoch)
    {
        const time_t when = static_cast<time_t>(seconds_since_epoch);
        struct tm    local;
#if LL_WINDOWS
        localtime_s(&local, &when);
#else
        localtime_r(&when, &local);
#endif
        char buffer[16];
        strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
        return buffer;
    }

    // A message as one row reads it.
    std::string oneLine(std::string text)
    {
        for (char& c : text)
        {
            if (c == '\n' || c == '\r' || c == '\t')
            {
                c = ' ';
            }
        }
        return text;
    }

    // What an object is called, where the viewer knows: its name value,
    // or the selection's name for it.
    std::string objectNameOf(LLViewerObject* object, const std::string& fallback)
    {
        if (!object)
        {
            return fallback;
        }
        if (LLNameValue* nv = object->getNVPair("Name"); nv && nv->getString() && nv->getString()[0])
        {
            return nv->getString();
        }
        if (LLSelectNode* node = LLSelectMgr::getInstance()->getSelection()->findNode(object); node && !node->mName.empty())
        {
            return node->mName;
        }
        return fallback;
    }

    // The roots selected in world, in their order.
    std::vector<LLUUID> selectedRoots()
    {
        std::vector<LLUUID>     roots;
        LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
        for (auto it = selection->valid_root_begin(); it != selection->valid_root_end(); ++it)
        {
            if (LLViewerObject* object = (*it)->getObject())
            {
                roots.push_back(object->getID());
            }
        }
        return roots;
    }

    // "12" or "12:5", as a person types a place: the line and the column
    // from one, zero where there is none or it is not a number.
    void placeTyped(const std::string& text, S32& line, S32& column)
    {
        line = column = 0;
        std::string_view rest(text);
        while (!rest.empty() && rest.front() == ' ')
        {
            rest.remove_prefix(1);
        }
        while (!rest.empty() && rest.front() == ':')
        {
            rest.remove_prefix(1);
        }
        size_t digits = 0;
        while (digits < rest.size() && isdigit(static_cast<unsigned char>(rest[digits])))
        {
            ++digits;
        }
        if (digits == 0)
        {
            return;
        }
        line = static_cast<S32>(std::strtol(std::string(rest.substr(0, digits)).c_str(), nullptr, 10));
        rest.remove_prefix(digits);
        if (rest.empty() || (rest.front() != ':' && rest.front() != ','))
        {
            return;
        }
        rest.remove_prefix(1);
        while (!rest.empty() && rest.front() == ' ')
        {
            rest.remove_prefix(1);
        }
        digits = 0;
        while (digits < rest.size() && isdigit(static_cast<unsigned char>(rest[digits])))
        {
            ++digits;
        }
        if (digits > 0)
        {
            column = static_cast<S32>(std::strtol(std::string(rest.substr(0, digits)).c_str(), nullptr, 10));
        }
    }

    // A name as both languages spell one: a letter or an underscore, then
    // letters, digits and underscores.
    bool isIdentifier(const std::string& text)
    {
        if (text.empty() || (!isalpha(static_cast<unsigned char>(text[0])) && text[0] != '_'))
        {
            return false;
        }
        for (const char c : text)
        {
            if (!isalnum(static_cast<unsigned char>(c)) && c != '_')
            {
                return false;
            }
        }
        return true;
    }
}

// static
bool ALFloaterScriptStudio::wantsScripts()
{
    static LLCachedControl<bool> enabled(gSavedSettings, "ALScriptStudioEnabled", false);
    return enabled;
}

// static
ALFloaterScriptStudio* ALFloaterScriptStudio::open(const ALScriptRef& ref, const std::string& name)
{
    ALFloaterScriptStudio* studio = LLFloaterReg::showTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD(), TAKE_FOCUS_YES);
    if (studio && !ref.isNull())
    {
        studio->openScript(ref, name);
    }
    return studio;
}

ALFloaterScriptStudio::ALFloaterScriptStudio(const LLSD& key)
:   ALStudioFloater(key, "ALScriptStudioState")
{
    mCommitCallbackRegistrar.add("ScriptStudio.Menu", boost::bind(&ALFloaterScriptStudio::onMenuAction, this, _2));
    mEnableCallbackRegistrar.add("ScriptStudio.Enable", boost::bind(&ALFloaterScriptStudio::onMenuEnable, this, _2));
    mEnableCallbackRegistrar.add("ScriptStudio.Check", boost::bind(&ALFloaterScriptStudio::onMenuCheck, this, _2));
}

ALFloaterScriptStudio::~ALFloaterScriptStudio() = default;

bool ALFloaterScriptStudio::postBuild()
{
    setMenuBar(getChild<LLMenuBarGL>("studio_menu"));
    setStatusLine(getChild<LLTextBox>("status"));
    mFolds.bind(this, { { "explorer", "explorer_panel", "fold_explorer", getString("PaneExplorer") },
                        { "bottom", "bottom_panel", "fold_bottom", getString("PaneBottom") },
                        { "inspector", "inspector_panel", "fold_inspector", getString("PaneInspector") } });
    mFolds.onChanged([this]() { saveState(); });

    mEditorHost    = getChild<LLPanel>("editor_panel");
    mTabs          = getChild<ALTabStrip>("tabs");
    mBreadcrumb    = getChild<ALJumpBar>("breadcrumb");
    mBottomTabs    = getChild<LLTabContainer>("bottom_tabs");
    mProblems      = getChild<LLScrollListCtrl>("problems");
    mReferences    = getChild<LLScrollListCtrl>("references");
    mOutline       = getChild<LLScrollListCtrl>("outline");
    mSymbol        = getChild<LLTextEditor>("symbol");
    mOutput        = getChild<ALOutputList>("output");
    mOutputFilter  = getChild<LLComboBox>("output_filter");
    mExplorer      = getChild<LLScrollListCtrl>("explorer");
    mCompileTarget = getChild<LLComboBox>("compile_target");
    mRunning       = getChild<LLCheckBoxCtrl>("running");
    mResetButton   = getChild<LLButton>("reset_btn");
    mSaveButton    = getChild<LLButton>("save_btn");

    mTabs->onChosen(boost::bind(&ALFloaterScriptStudio::onTabChosen, this, _1));
    mTabs->onClosed(boost::bind(&ALFloaterScriptStudio::closeDocument, this, _1));
    mBreadcrumb->onChose(boost::bind(&ALFloaterScriptStudio::onCrumbChosen, this, _1, _2));
    mProblems->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onProblemSelected, this));
    mReferences->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onReferenceChosen, this));
    mOutline->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onOutlineChosen, this));
    mOutputFilter->add(getString("OutputAllObjects"), LLSD(LLUUID::null));
    mOutputFilter->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onOutputFilter, this));
    mOutput->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onOutputChosen, this));
    getChild<LLButton>("output_clear")->setCommitCallback([this](LLUICtrl*, const LLSD&) { mOutput->clearEntries(); });
    // What was said before the window opened, then everything after.
    for (const ALScriptWorkspace::RuntimeEvent& event : ALScriptWorkspace::instance().recentRuntime())
    {
        runtimeEvent(event);
    }
    mRuntimeConnection = ALScriptWorkspace::instance().onRuntime([this](const ALScriptWorkspace::RuntimeEvent& event) { runtimeEvent(event); });

    mExplorer->setDoubleClickCallback(boost::bind(&ALFloaterScriptStudio::onExplorerChosen, this));
    for (const char* action : { "open", "start", "stop", "reset", "refresh" })
    {
        getChild<LLButton>(std::string("explorer_") + action)->setCommitCallback([this, action](LLUICtrl*, const LLSD&) { onExplorerAction(action); });
    }
    mRunningConnection = ALScriptWorkspace::instance().onRunningState([this](const ALScriptWorkspace::RunningState& state) { runningState(state); });
    for (LLScrollListCtrl* list : { mProblems, mReferences, static_cast<LLScrollListCtrl*>(mOutput), mExplorer })
    {
        listMenuFor(list);
    }
    refreshExplorer();
    mCompileTarget->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onCompileTarget, this));
    mRunning->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onRunning, this));
    mResetButton->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onReset, this));
    mSaveButton->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (Doc* doc = active())
        {
            save(*doc);
        }
    });
    mCompiledConnection = ALScriptWorkspace::instance().onCompiled([this](const ALScriptWorkspace::CompileResult& result) { compiled(result); });
    // New definitions from the region: the analyzers reload, the words
    // are rebuilt, and every script is checked again.
    mDefinitionsConnection = LLSyntaxDefCache::instance().addSyntaxIDCallback([this]() {
        ALScriptAnalysis::instance().definitionsChanged();
        mVocabularyBuilt[0] = mVocabularyBuilt[1] = false;
        for (std::unique_ptr<Doc>& doc : mDocs)
        {
            if (doc->loaded)
            {
                teachEditor(*doc);
                scheduleAnalysis(*doc, true);
            }
        }
    });

    loadState();
    refreshToolbar();
    fillTabs();
    return true;
}

void ALFloaterScriptStudio::onClose(bool app_quitting)
{
    ALStudioFloater::onClose(app_quitting);
}

void ALFloaterScriptStudio::draw()
{
    pumpAnalysis();
    pumpCaret();
    pumpExplorer();
    ALStudioFloater::draw();
}

bool ALFloaterScriptStudio::handleKeyHere(KEY key, MASK mask)
{
    if (handleMenuAccelerator(key, mask) || handleUndoKeys(key, mask))
    {
        return true;
    }
    return ALStudioFloater::handleKeyHere(key, mask);
}

bool ALFloaterScriptStudio::undo()
{
    Doc* doc = active();
    if (!doc || !doc->editor->canUndo())
    {
        return false;
    }
    doc->editor->undo();
    return true;
}

bool ALFloaterScriptStudio::redo()
{
    Doc* doc = active();
    if (!doc || !doc->editor->canRedo())
    {
        return false;
    }
    doc->editor->redo();
    return true;
}

// --- documents -----------------------------------------------------------------

ALFloaterScriptStudio::Doc* ALFloaterScriptStudio::active()
{
    return mActive < mDocs.size() ? mDocs[mActive].get() : nullptr;
}

size_t ALFloaterScriptStudio::indexOf(const ALScriptRef& ref) const
{
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        if (mDocs[i]->ref == ref && !mDocs[i]->sourceView)
        {
            return i;
        }
    }
    return NONE;
}

size_t ALFloaterScriptStudio::indexOf(std::string_view id) const
{
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        if (mDocs[i]->id == id)
        {
            return i;
        }
    }
    return NONE;
}

ALCodeEditor* ALFloaterScriptStudio::makeEditor(const std::string& id, bool read_only)
{
    ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
    p.name = "editor_" + id;
    p.rect = mEditorHost->getLocalRect();
    p.follows.flags(FOLLOWS_ALL);
    p.read_only         = read_only;
    p.word_wrap         = mWordWrap;
    p.show_line_numbers = mLineNumbers;
    p.soft_tabs         = true;
    ALCodeEditor* editor = LLUICtrlFactory::create<ALCodeEditor>(p);
    editor->setVisible(false);
    applyEditorOptions(*editor);
    mEditorHost->addChild(editor);
    return editor;
}

void ALFloaterScriptStudio::applyEditorOptions(ALCodeEditor& editor) const
{
    editor.setWordWrap(mWordWrap);
    editor.setShowLineNumbers(mLineNumbers);
    editor.setScrollMapWidth(mScrollMapWidth);
    editor.setScrollMapPreview(mScrollMapPreview);
    editor.setScrollMapOnLeft(mScrollMapLeft);
    editor.setScrollMap(mScrollMap);
}

void ALFloaterScriptStudio::applyEditorOptions()
{
    for (std::unique_ptr<Doc>& each : mDocs)
    {
        applyEditorOptions(*each->editor);
    }
    saveState();
}

void ALFloaterScriptStudio::openScript(const ALScriptRef& ref, const std::string& name)
{
    const size_t already = indexOf(ref);
    if (already != NONE)
    {
        activate(already);
        return;
    }

    auto doc    = std::make_unique<Doc>();
    doc->ref    = ref;
    doc->id     = ref.id();
    doc->name   = name.empty() ? getString("Untitled") : name;
    doc->editor = makeEditor(doc->id, true);
    doc->editor->setText(getString("Loading"));
    Doc* raw     = doc.get();
    doc->changed = doc->editor->onTextChanged([this, raw]() {
        fillTabs();
        scheduleAnalysis(*raw);
        // A run-time error was about the text as it was.
        if (!raw->runtime.empty())
        {
            raw->runtime.clear();
            refreshProblems(*raw);
        }
    });

    mDocs.push_back(std::move(doc));
    activate(mDocs.size() - 1);

    const LLHandle<LLFloater> handle = getHandle();
    ALScriptWorkspace::instance().load(ref, [handle](const ALScriptWorkspace::Loaded& answer) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->loaded(answer);
        }
    });
}

void ALFloaterScriptStudio::loaded(const ALScriptWorkspace::Loaded& answer)
{
    const size_t index = indexOf(answer.ref);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    if (!answer.name.empty())
    {
        doc.name = answer.name;
    }
    doc.assetId    = answer.assetId;
    doc.language   = answer.language;
    doc.modifiable = answer.modifiable;
    if (!answer.error.empty())
    {
        doc.editor->setText(answer.error);
        doc.editor->setReadOnly(true);
        setStatus(answer.error, true);
    }
    else
    {
        doc.loaded = true;
        doc.editor->setSyntax(answer.language.lua ? "slua" : "lsl");
        teachEditor(doc);
        // A script the preprocessor wrapped: the editor holds the source
        // the author wrote, and what the server compiled goes in a tab
        // of its own.
        doc.envelope = ALScriptEnvelope::parse(answer.text);
        if (doc.envelope)
        {
            doc.editor->setText(doc.envelope->source);
            if (!doc.envelope->compileTarget.empty())
            {
                doc.language.compileTarget = doc.envelope->compileTarget;
            }
            showExpanded(doc, doc.envelope->expanded);
        }
        else
        {
            doc.editor->setText(answer.text);
            const std::string directive = ALScriptEnvelope::directiveOf(answer.text, answer.language.lua);
            if (!directive.empty())
            {
                doc.language.compileTarget = directive;
            }
        }
        doc.editor->setReadOnly(!answer.modifiable);
        doc.expanded.valid = false;
        doc.uploaded.valid = false;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString(answer.modifiable ? "Loaded" : "LoadedReadOnly", args));
        if (preprocessed(doc))
        {
            // Its includes fetched now, so that the analyzers have them,
            // and the expanded code shown as it would be uploaded.
            preprocess(doc, false);
        }
        scheduleAnalysis(doc, true);
        if (!doc.ref.inInventory())
        {
            // Whether it runs, and what it compiles for, which the region
            // knows better than the text does; and its object in the explorer.
            ALScriptWorkspace::instance().askRunning(doc.ref);
            refreshExplorer();
        }
        if (doc.pendingLine >= 0)
        {
            doc.editor->goToLine(doc.pendingLine);
            doc.pendingLine = -1;
        }
    }
    fillTabs();
    if (index == mActive)
    {
        refreshToolbar();
    }
}

void ALFloaterScriptStudio::showExpanded(Doc& doc, const std::string& text)
{
    const std::string id    = doc.id + ":expanded";
    size_t            index = indexOf(id);
    if (index == NONE)
    {
        auto source        = std::make_unique<Doc>();
        source->ref        = doc.ref;
        source->id         = id;
        source->sourceView = true;
        source->loaded     = true;
        source->editor     = makeEditor(id, true);
        // Beside the script it came from.
        const size_t after = indexOf(doc.id) + 1;
        mDocs.insert(mDocs.begin() + after, std::move(source));
        index = after;
        if (mActive != NONE && mActive >= after)
        {
            ++mActive;
        }
    }
    Doc& expanded = *mDocs[index];
    LLStringUtil::format_map_t args;
    args["[NAME]"]    = doc.name;
    expanded.name     = getString("ExpandedTabName", args);
    expanded.language = doc.language;
    expanded.editor->setSyntax(doc.language.lua ? "slua" : "lsl");
    teachEditor(expanded);
    expanded.editor->setText(text);
    fillTabs();
}

// --- the preprocessor ---------------------------------------------------------------

bool ALFloaterScriptStudio::preprocessed(const Doc& doc) const
{
    return doc.loaded && !doc.sourceView && (doc.envelope.has_value() || ALScriptPreprocessor::enabled());
}

ALScriptPreprocessor::Request ALFloaterScriptStudio::preprocessRequest(const Doc& doc) const
{
    ALScriptPreprocessor::Request request;
    request.ref     = doc.ref;
    request.name    = doc.name;
    request.assetId = doc.assetId;
    request.source  = doc.editor->text();
    request.lua     = doc.language.lua;
    request.compileTarget = mCompileTarget->getValue().asString();
    if (request.compileTarget.empty())
    {
        request.compileTarget = doc.language.compileTarget;
    }
    return request;
}

const ALFloaterScriptStudio::Doc::Expanded& ALFloaterScriptStudio::expandedFor(Doc& doc)
{
    const U32 version = doc.editor->document().version();
    if (!doc.expanded.valid || doc.expanded.version != version)
    {
        ALPreprocessor::Result result = ALScriptPreprocessor::instance().runNow(preprocessRequest(doc));
        doc.expanded.valid            = true;
        doc.expanded.disabled         = result.disabled;
        doc.expanded.version          = version;
        doc.expanded.text             = std::move(result.text);
        doc.expanded.map              = std::move(result.map);
        doc.expanded.problems         = std::move(result.problems);
    }
    return doc.expanded;
}

void ALFloaterScriptStudio::preprocess(Doc& doc, bool then_save)
{
    if (doc.preprocessing)
    {
        return;
    }
    doc.preprocessing = true;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    setStatus(getString("Preprocessing", args));
    const LLHandle<LLFloater> handle  = getHandle();
    const std::string         id      = doc.id;
    const U32                 version = doc.editor->document().version();
    ALScriptPreprocessor::instance().run(preprocessRequest(doc), [handle, id, version, then_save](const ALPreprocessor::Result& result) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->preprocessedAnswer(id, version, then_save, result);
        }
    });
}

void ALFloaterScriptStudio::preprocessedAnswer(const std::string& id, U32 version, bool then_save, const ALPreprocessor::Result& result)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc          = *mDocs[index];
    doc.preprocessing = false;
    // What a save would upload, shown; the analyzers' own expansion is
    // made again now that every include is in, without the optimizer.
    doc.uploaded.valid    = true;
    doc.uploaded.disabled = result.disabled;
    doc.uploaded.version  = version;
    doc.uploaded.text     = result.text;
    doc.uploaded.map      = result.map;
    doc.uploaded.problems = result.problems;
    doc.expanded.valid    = false;
    showExpanded(doc, result.text);
    refreshProblems(doc);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (version != doc.editor->document().version())
    {
        // The text moved on while the includes came: analysed again, and
        // saved again from the start if that was the point.
        doc.expanded.valid = false;
        scheduleAnalysis(doc, true);
        if (then_save)
        {
            save(doc);
        }
        return;
    }
    scheduleAnalysis(doc, true);
    if (!then_save)
    {
        if (!result.pending.empty())
        {
            args["[COUNT]"] = std::to_string(result.pending.size());
            setStatus(getString("PreprocessedPending", args), true);
        }
        else
        {
            setStatus(getString("Preprocessed", args));
        }
        return;
    }
    const F64 now = LLTimer::getTotalSeconds();
    if (result.hasErrors() && doc.saveAnywayUntil <= now)
    {
        S32 errors = 0;
        for (const ALScriptProblem& problem : result.problems)
        {
            errors += problem.severity == ALScriptProblem::Severity::Error ? 1 : 0;
        }
        args["[COUNT]"] = std::to_string(errors);
        setStatus(getString("PreprocessErrors", args), true);
        doc.saveAnywayUntil = now + SAVE_ANYWAY;
        showBottom("problems_tab");
        return;
    }
    if (result.disabled)
    {
        // `//fspreprocessor off`: the text goes up as it is, as
        // Firestorm sends it.
        upload(doc, doc.editor->text());
        return;
    }
    // In the envelope, with the source as written, so Firestorm opens
    // what we save; the lines that say who wrote it and when are ours.
    ALScriptEnvelope envelope;
    if (doc.envelope)
    {
        envelope = *doc.envelope;
    }
    envelope.lua           = doc.language.lua;
    envelope.source        = doc.editor->text();
    envelope.expanded      = result.text;
    envelope.compileTarget = mCompileTarget->getValue().asString();
    if (envelope.compileTarget.empty())
    {
        envelope.compileTarget = doc.language.compileTarget;
    }
    envelope.programVersion = LLVersionInfo::instance().getChannelAndVersion();
    envelope.lastCompiled   = LLDate::now().asString();
    doc.envelope            = envelope;
    upload(doc, envelope.wrap());
}

// static
S32 ALFloaterScriptStudio::mapSpan(const ALSourceMap& map, ALScriptSpan& span)
{
    const ALSourceMap::Loc begin = map.toSource(span.line, span.column);
    if (!begin.found())
    {
        return -1;
    }
    const ALSourceMap::Loc end = map.toSource(span.endLine, span.endColumn);
    span.line                  = begin.line;
    span.column                = begin.column;
    if (end.found() && end.file == begin.file && (end.line > begin.line || (end.line == begin.line && end.column > begin.column)))
    {
        span.endLine   = end.line;
        span.endColumn = end.column;
    }
    else
    {
        span.endLine   = begin.line;
        span.endColumn = begin.column;
    }
    return begin.file;
}

std::string ALFloaterScriptStudio::includeName(const Doc& doc, const std::string& path) const
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

void ALFloaterScriptStudio::chooseIncludeFolder()
{
    const LLHandle<LLFloater> handle = getHandle();
    (new LLDirPickerThread(
         [handle](const std::vector<std::string>& folders, std::string) {
             if (folders.empty() || !ALViewType::as<ALFloaterScriptStudio>(handle.get()))
             {
                 return;
             }
             gSavedSettings.setString("ALScriptPreprocDiskIncludeFolder", folders.front());
             gSavedSettings.setBOOL("ALScriptPreprocDiskIncludes", true);
         },
         gSavedSettings.getString("ALScriptPreprocDiskIncludeFolder")))
        ->getFile();
}

// --- the language's words --------------------------------------------------------

const std::vector<ALFloaterScriptStudio::Vocab>& ALFloaterScriptStudio::vocabulary(bool lua)
{
    std::vector<Vocab>& out = mVocabulary[lua ? 1 : 0];
    if (mVocabularyBuilt[lua ? 1 : 0])
    {
        return out;
    }
    mVocabularyBuilt[lua ? 1 : 0] = true;
    out.clear();
    const LLSD keywords = lua ? LLSyntaxDefCache::instance().getLuaKeywords() : LLSyntaxDefCache::instance().getLSLKeywords();
    if (!keywords.isMap())
    {
        return out;
    }
    auto firstLine = [](const std::string& text) {
        const size_t end = text.find('\n');
        return end == std::string::npos ? text : text.substr(0, end);
    };
    auto arguments = [](const LLSD& args) {
        std::string list;
        auto        one = [&](const std::string& name, const LLSD& attrs) {
            if (!list.empty())
            {
                list += ", ";
            }
            const std::string type = attrs.get("type").asString();
            list += type.empty() ? name : type + " " + name;
        };
        if (args.isArray())
        {
            for (LLSD::array_const_iterator it = args.beginArray(); it != args.endArray(); ++it)
            {
                if (it->isMap())
                {
                    for (LLSD::map_const_iterator arg = it->beginMap(); arg != it->endMap(); ++arg)
                    {
                        one(arg->first, arg->second);
                    }
                }
            }
        }
        else if (args.isMap())
        {
            for (LLSD::map_const_iterator arg = args.beginMap(); arg != args.endMap(); ++arg)
            {
                one(arg->first, arg->second);
            }
        }
        return list;
    };
    for (LLSD::map_const_iterator group = keywords.beginMap(); group != keywords.endMap(); ++group)
    {
        if (!group->second.isMap())
        {
            continue;
        }
        const std::string& name = group->first;
        ALSyntaxKind       kind;
        if (name == "functions")
        {
            kind = ALSyntaxKind::Function;
        }
        else if (name == "events")
        {
            kind = ALSyntaxKind::Event;
        }
        else if (name == "types")
        {
            kind = ALSyntaxKind::Type;
        }
        else if (name == "controls")
        {
            kind = ALSyntaxKind::Control;
        }
        else if (name.compare(0, 9, "constants") == 0)
        {
            kind = ALSyntaxKind::Constant;
        }
        else
        {
            continue;
        }
        for (LLSD::map_const_iterator entry = group->second.beginMap(); entry != group->second.endMap(); ++entry)
        {
            const LLSD& attrs = entry->second;
            Vocab       word;
            word.text       = entry->first;
            word.kind       = kind;
            word.tooltip    = attrs.get("tooltip").asString();
            word.deprecated = attrs.has("deprecated") && (attrs["deprecated"].asBoolean() || attrs["deprecated"].asString() == "true");
            switch (kind)
            {
                case ALSyntaxKind::Function:
                {
                    // Lua says "()" for a function that returns nothing,
                    // which is nothing worth reading before the name.
                    std::string returns = attrs.get("return").asString();
                    if (returns == "()")
                    {
                        returns.clear();
                    }
                    word.detail = (returns.empty() ? std::string() : returns + " ") + word.text + "(" + arguments(attrs.get("arguments")) + ")";
                    break;
                }
                case ALSyntaxKind::Event:
                    word.detail = word.text + "(" + arguments(attrs.get("arguments")) + ")";
                    break;
                case ALSyntaxKind::Constant:
                {
                    const std::string type  = attrs.get("type").asString();
                    const std::string value = attrs.get("value").asString();
                    word.detail             = (type.empty() ? std::string() : type + " ") + word.text + (value.empty() ? std::string() : " = " + value);
                    break;
                }
                default:
                    word.detail = firstLine(attrs.get("tooltip").asString());
                    break;
            }
            out.push_back(std::move(word));
        }
    }
    std::sort(out.begin(), out.end(), [](const Vocab& a, const Vocab& b) { return a.text < b.text; });
    return out;
}

void ALFloaterScriptStudio::teachEditor(Doc& doc)
{
    ALCodeEditor&             editor = *doc.editor;
    const bool                lua    = doc.language.lua;
    const std::vector<Vocab>& words  = vocabulary(lua);
    std::vector<std::string>  functions, events, types, controls, constants, deprecated;
    for (const Vocab& word : words)
    {
        if (word.deprecated)
        {
            deprecated.push_back(word.text);
            continue;
        }
        switch (word.kind)
        {
            case ALSyntaxKind::Function: functions.push_back(word.text); break;
            case ALSyntaxKind::Event:    events.push_back(word.text); break;
            case ALSyntaxKind::Type:     types.push_back(word.text); break;
            case ALSyntaxKind::Control:  controls.push_back(word.text); break;
            case ALSyntaxKind::Constant: constants.push_back(word.text); break;
            default: break;
        }
    }
    ALSyntaxWords& tables = editor.highlighter().words();
    tables.set("function", std::move(functions));
    tables.set("event", std::move(events));
    tables.set("type", std::move(types));
    tables.set("control", std::move(controls));
    tables.set("constant", std::move(constants));
    tables.set("deprecated", std::move(deprecated));
    editor.highlighter().wordsChanged();

    // The analyzer answers what the vocabulary cannot: the script's own
    // symbols, the types of things, what a call takes.
    Doc* raw = &doc;
    editor.setCompletionRequest([this, raw](const ALTextPos& at, std::string_view) { askAnalyzer(*raw, ALScriptAnalysis::Kind::Complete, at); });
    editor.setHoverRequest([this, raw](const ALTextPos& at, std::string_view) { askAnalyzer(*raw, ALScriptAnalysis::Kind::Hover, at); });
    editor.setSignatureRequest([this, raw](const ALTextPos& caret) { askAnalyzer(*raw, ALScriptAnalysis::Kind::Signature, caret); });
    editor.setSymbolRequest([this, raw](ALEditorCommand command, const ALTextRange& word) { askSymbol(*raw, command, word); });
    editor.setHoverProvider([this, lua](const ALTextPos&, std::string_view word, std::string& text) {
        const std::vector<Vocab>& words = vocabulary(lua);
        // A member of ll or a local is the analyzer's to explain.
        if (lua)
        {
            return false;
        }
        const auto it = std::lower_bound(words.begin(), words.end(), word,
                                         [](const Vocab& v, std::string_view w) { return v.text < w; });
        if (it == words.end() || it->text != word)
        {
            return false;
        }
        text = it->detail.empty() ? it->text : it->detail;
        if (!it->tooltip.empty())
        {
            text += "\n" + it->tooltip;
        }
        if (it->deprecated)
        {
            text += "\n(deprecated)";
        }
        return true;
    });
    editor.setCompletionProvider([this, lua](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
        for (const Vocab& word : vocabulary(lua))
        {
            if (word.text.size() < prefix.size())
            {
                continue;
            }
            bool match = true;
            for (size_t i = 0; i < prefix.size() && match; ++i)
            {
                match = LLStringOps::toLower(word.text[i]) == LLStringOps::toLower(prefix[i]);
            }
            if (match)
            {
                ALCodeEditor::Completion c;
                c.text   = word.text;
                c.detail = word.detail;
                c.kind   = word.deprecated ? ALSyntaxKind::Deprecated : word.kind;
                out.push_back(std::move(c));
            }
        }
    });
}

void ALFloaterScriptStudio::askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at)
{
    if (!doc.loaded)
    {
        return;
    }
    ALScriptAnalysis::Request request;
    request.kind    = kind;
    request.id      = doc.id;
    request.version = doc.editor->document().version();
    request.lua     = doc.language.lua;
    request.mono    = doc.language.compileTarget != "lsl2";
    request.text    = doc.editor->text();
    request.line    = at.line;
    request.column  = at.column;
    if (preprocessed(doc))
    {
        // The analyzers see what the compiler would; a position inside a
        // directive has nothing there to ask about.
        const Doc::Expanded& expanded = expandedFor(doc);
        request.text                  = expanded.text;
        if (kind != ALScriptAnalysis::Kind::Check)
        {
            const ALSourceMap::Loc loc = expanded.map.toExpanded(0, at.line, at.column);
            if (!loc.found())
            {
                return;
            }
            request.line   = loc.line;
            request.column = loc.column;
        }
    }
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptAnalysis::instance().ask(std::move(request), [handle](const ALScriptAnalysis::Result& result) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->answered(result);
        }
    });
}

namespace
{
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
            case ALScriptSymbolKind::State:     return ALSyntaxKind::Label;
            case ALScriptSymbolKind::Label:     return ALSyntaxKind::Label;
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
}

void ALFloaterScriptStudio::answered(const ALScriptAnalysis::Result& result)
{
    const size_t index = indexOf(result.id);
    if (index == NONE)
    {
        return;
    }
    Doc&      doc = *mDocs[index];
    ALTextPos at(result.line, result.column);
    if (preprocessed(doc) && result.kind != ALScriptAnalysis::Kind::Check)
    {
        // Answered about the expanded text; the editor wants the source's
        // place, which is where it asked.
        if (!doc.expanded.valid || doc.expanded.version != result.version)
        {
            return;
        }
        const ALSourceMap::Loc loc = doc.expanded.map.toSource(result.line, result.column);
        if (!loc.found() || loc.file != 0)
        {
            return;
        }
        at = ALTextPos(loc.line, loc.column);
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
                completion.text   = c.text;
                completion.detail = c.detail;
                completion.kind   = c.deprecated ? ALSyntaxKind::Deprecated : syntaxKindOf(c.kind);
                more.push_back(std::move(completion));
            }
            doc.editor->supplyCompletions(at, std::move(more));
            break;
        }
        case ALScriptAnalysis::Kind::Hover:
        {
            if (!result.hover.found)
            {
                break;
            }
            std::string text = result.hover.label;
            if (!result.hover.documentation.empty())
            {
                text += "\n" + result.hover.documentation;
            }
            if (!result.hover.link.empty())
            {
                text += "\n" + result.hover.link;
            }
            doc.editor->supplyHover(at, text);
            break;
        }
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
            doc.editor->showSignature(at, std::move(signature));
            break;
        }
        case ALScriptAnalysis::Kind::References:
            symbolAnswered(doc, result);
            break;
        case ALScriptAnalysis::Kind::Inspect:
            inspected(doc, result);
            break;
    }
}

void ALFloaterScriptStudio::activate(size_t index)
{
    if (index >= mDocs.size())
    {
        return;
    }
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        mDocs[i]->editor->setVisible(i == index);
    }
    mActive = index;
    mDocs[index]->editor->setFocus(true);
    fillTabs();
    refreshToolbar();
    fillProblems(mDocs[index].get());
    fillReferences(mDocs[index].get());
    refreshOutline(*mDocs[index]);
    // The inspector is about this script now: told again once the caret
    // is seen.
    mDocs[index]->caretSeen = ALTextPos(-1, -1);
    mDocs[index]->inspectAt = ALTextPos(-1, -1);
    mSymbol->setText(LLStringUtil::null);
}

void ALFloaterScriptStudio::fillTabs()
{
    std::vector<ALTabStrip::Tab> tabs;
    std::string                  chosen;
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        const Doc&      doc = *mDocs[i];
        ALTabStrip::Tab tab;
        tab.label   = doc.name;
        tab.value   = doc.id;
        tab.dirty   = doc.editor->isDirty();
        tab.toolTip = doc.sourceView ? getString("TabExpandedTip") : doc.ref.inInventory() ? getString("TabInventoryTip") : getString("TabObjectTip");
        tabs.push_back(std::move(tab));
        if (i == mActive)
        {
            chosen = doc.id;
        }
    }
    mTabs->setTabs(std::move(tabs), chosen);
}

void ALFloaterScriptStudio::refreshToolbar()
{
    Doc*       doc     = active();
    const bool have    = doc && doc->loaded;
    const bool task    = doc && !doc->ref.inInventory();
    mCompileTarget->setEnabled(have && doc->modifiable);
    mSaveButton->setEnabled(have && doc->modifiable && !doc->saving);
    mRunning->setVisible(task);
    mResetButton->setVisible(task);
    if (task)
    {
        mRunning->set(doc->running == 1);
    }
    if (have)
    {
        const bool lua = luaEnabledFor(doc->ref);
        if (LLScrollListItem* item = mCompileTarget->findItemByValue("luau"))
        {
            item->setEnabled(lua);
        }
        if (LLScrollListItem* item = mCompileTarget->findItemByValue("lsl-luau"))
        {
            item->setEnabled(lua);
        }
        mCompileTarget->setValue(doc->language.compileTarget);
    }
}

void ALFloaterScriptStudio::onTabChosen(const std::string& value)
{
    activate(indexOf(value));
}

// --- saving and compiling ------------------------------------------------------

void ALFloaterScriptStudio::save(Doc& doc)
{
    if (!doc.loaded || !doc.modifiable || doc.saving)
    {
        return;
    }
    if (!preflight(doc))
    {
        return;
    }
    if (preprocessed(doc))
    {
        // Expanded first, with its includes fetched; the upload follows.
        preprocess(doc, true);
        return;
    }
    doc.uploaded.valid = false;
    upload(doc, doc.editor->text());
}

void ALFloaterScriptStudio::upload(Doc& doc, const std::string& text)
{
    ALScriptWorkspace::SaveOptions options;
    options.compileTarget = mCompileTarget->getValue().asString();
    if (options.compileTarget.empty())
    {
        options.compileTarget = doc.language.compileTarget;
    }
    options.running = doc.ref.inInventory() || mRunning->get();
    std::string error;
    if (!ALScriptWorkspace::instance().save(doc.ref, text, options, nullptr, error))
    {
        setStatus(error, true);
        return;
    }
    doc.saveAnywayUntil = 0.0;
    doc.saving          = true;
    doc.problems.clear();
    refreshProblems(doc);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    setStatus(getString("Saving", args));
    refreshToolbar();
}

void ALFloaterScriptStudio::saveAll()
{
    for (std::unique_ptr<Doc>& doc : mDocs)
    {
        if (doc->editor->isDirty())
        {
            save(*doc);
        }
    }
}

void ALFloaterScriptStudio::compiled(const ALScriptWorkspace::CompileResult& result)
{
    const size_t index = indexOf(result.ref);
    if (index == NONE)
    {
        return;
    }
    Doc& doc   = *mDocs[index];
    doc.saving = false;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (!result.error.empty())
    {
        args["[ERROR]"] = result.error;
        setStatus(getString("SaveFailed", args), true);
        refreshToolbar();
        return;
    }
    // The text is the server's now, compiled or not.
    doc.editor->resetDirty();
    if (result.newAssetId.notNull())
    {
        doc.assetId = result.newAssetId;
    }
    doc.problems = result.diagnostics;
    if (result.success)
    {
        // A new script runs from here; what the old one said is past.
        doc.runtime.clear();
    }
    refreshProblems(doc);

    if (result.success)
    {
        setStatus(getString("Compiled", args));
    }
    else
    {
        args["[COUNT]"] = std::to_string(doc.problems.size());
        setStatus(getString("CompileFailed", args), true);
    }
    if (index == mActive)
    {
        refreshToolbar();
    }
    fillTabs();
    if (doc.closeAfterSave)
    {
        letGoOf(index);
    }
}

// --- the analyzers -------------------------------------------------------------

void ALFloaterScriptStudio::scheduleAnalysis(Doc& doc, bool now)
{
    if (!doc.loaded || doc.sourceView)
    {
        return;
    }
    doc.analysisDue = now ? 1.0 : static_cast<F64>(LLTimer::getTotalSeconds()) + ANALYSIS_DELAY;
}

void ALFloaterScriptStudio::pumpAnalysis()
{
    const F64 now = LLTimer::getTotalSeconds();
    for (std::unique_ptr<Doc>& doc : mDocs)
    {
        if (doc->analysisDue > 0.0 && now >= doc->analysisDue)
        {
            doc->analysisDue = 0.0;
            requestAnalysis(*doc);
        }
    }
}

void ALFloaterScriptStudio::requestAnalysis(Doc& doc)
{
    doc.requestedVersion = doc.editor->document().version();
    askAnalyzer(doc, ALScriptAnalysis::Kind::Check, ALTextPos());
}

void ALFloaterScriptStudio::analysed(const ALScriptAnalysis::Result& result)
{
    const size_t index = indexOf(result.id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    // Of a text that has moved on: the check of the newer text follows.
    if (result.version != doc.editor->document().version())
    {
        return;
    }
    doc.analysis         = result.problems;
    doc.analysisVersion  = result.version;
    doc.definitionsError = result.definitionsError;
    doc.outline          = result.outline;
    if (preprocessed(doc) && doc.expanded.valid && doc.expanded.version == result.version)
    {
        // Back to the source: a problem in an include keeps its file, and
        // what an include declares is the include's to outline.
        const ALSourceMap& map = doc.expanded.map;
        for (ALScriptProblem& problem : doc.analysis)
        {
            ALScriptSpan span;
            span.line      = problem.line;
            span.column    = problem.column;
            span.endLine   = problem.endLine;
            span.endColumn = problem.endColumn;
            const S32 file = mapSpan(map, span);
            if (file < 0)
            {
                continue;
            }
            problem.line      = span.line;
            problem.column    = span.column;
            problem.endLine   = span.endLine;
            problem.endColumn = span.endColumn;
            if (file > 0)
            {
                problem.file = map.files()[file].path;
            }
        }
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
    refreshProblems(doc);
    refreshOutline(doc);
    if (doc.saveAfterCheck)
    {
        doc.saveAfterCheck = false;
        save(doc);
    }
}

void ALFloaterScriptStudio::refreshProblems(Doc& doc)
{
    static const LLUIColor error_color   = LLUIColorTable::instance().getColor("CodeMarkError", LLColor4::red);
    static const LLUIColor warning_color = LLUIColorTable::instance().getColor("CodeMarkWarning", LLColor4::yellow);
    static const LLUIColor note_color    = LLUIColorTable::instance().getColor("CodeMarkNote", LLColor4::blue);
    static const LLUIColor runtime_color = LLUIColorTable::instance().getColor("CodeMarkRuntime", LLColor4::magenta);

    doc.shown.clear();
    doc.editor->clearMarks();
    std::vector<ALCodeEditor::Decoration> decorations;
    const ALTextDocument&                 text = doc.editor->document();

    auto add = [&](S32 line, S32 column, bool has_column, S32 end_line, S32 end_column, ALCodeEditor::Mark mark,
                   const std::string& level, const std::string& origin, const std::string& message, const std::string& file = std::string()) {
        Doc::Shown row;
        row.line      = line;
        row.column    = column;
        row.hasColumn = has_column;
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
        decoration.color   = (mark == ALCodeEditor::Mark::Error     ? error_color
                              : mark == ALCodeEditor::Mark::Warning ? warning_color
                              : mark == ALCodeEditor::Mark::Runtime ? runtime_color
                                                                    : note_color)
                                 .get();
        decoration.message = origin + ": " + message;
        decorations.push_back(std::move(decoration));
    };

    for (const ALScriptWorkspace::Diagnostic& problem : doc.problems)
    {
        const bool  error  = problem.level != "WARNING";
        S32         line   = problem.line;
        S32         column = problem.column;
        std::string file;
        if (doc.uploaded.valid && !doc.uploaded.disabled)
        {
            // The compiler read the expanded text.
            const ALSourceMap::Loc loc = doc.uploaded.map.toSource(line, column);
            if (loc.found())
            {
                line   = loc.line;
                column = loc.column;
                if (loc.file > 0)
                {
                    file = doc.uploaded.map.files()[loc.file].path;
                }
            }
        }
        add(line, column, problem.hasColumn, line, column, error ? ALCodeEditor::Mark::Error : ALCodeEditor::Mark::Warning, problem.level,
            getString("OriginCompiler"), problem.message, file);
    }
    // The preprocessor's own word on the text as it stands, and the
    // optimizer's notes from the last run ahead of a save.
    const auto preprocessorRow = [&](const ALScriptProblem& problem) {
        const ALCodeEditor::Mark mark  = problem.severity == ALScriptProblem::Severity::Error     ? ALCodeEditor::Mark::Error
                                         : problem.severity == ALScriptProblem::Severity::Warning ? ALCodeEditor::Mark::Warning
                                                                                                  : ALCodeEditor::Mark::Note;
        const std::string        level = problem.severity == ALScriptProblem::Severity::Error     ? "ERROR"
                                         : problem.severity == ALScriptProblem::Severity::Warning ? "WARNING"
                                                                                                  : "NOTE";
        const bool optimizer = problem.source == ALScriptProblem::Source::Optimizer;
        add(problem.line, problem.column, true, problem.endLine, problem.endColumn, mark, level,
            getString(optimizer ? "OriginOptimizer" : "OriginPreprocessor"), problem.message, problem.file);
    };
    if (doc.expanded.valid)
    {
        for (const ALScriptProblem& problem : doc.expanded.problems)
        {
            preprocessorRow(problem);
        }
    }
    if (doc.uploaded.valid)
    {
        for (const ALScriptProblem& problem : doc.uploaded.problems)
        {
            if (problem.source == ALScriptProblem::Source::Optimizer)
            {
                preprocessorRow(problem);
            }
        }
    }
    for (const ALScriptProblem& problem : doc.analysis)
    {
        const ALCodeEditor::Mark mark = problem.severity == ALScriptProblem::Severity::Error     ? ALCodeEditor::Mark::Error
                                        : problem.severity == ALScriptProblem::Severity::Warning ? ALCodeEditor::Mark::Warning
                                                                                                 : ALCodeEditor::Mark::Note;
        const std::string level  = problem.severity == ALScriptProblem::Severity::Error     ? "ERROR"
                                   : problem.severity == ALScriptProblem::Severity::Warning ? "WARNING"
                                                                                            : "NOTE";
        const std::string origin = problem.source == ALScriptProblem::Source::Parser  ? getString("OriginParser")
                                   : problem.source == ALScriptProblem::Source::Types ? getString("OriginTypes")
                                                                                      : getString("OriginLint");
        const std::string message = problem.code.empty() ? problem.message : problem.message + " [" + problem.code + "]";
        add(problem.line, problem.column, true, problem.endLine, problem.endColumn, mark, level, origin, message, problem.file);
    }
    for (const Doc::RuntimeProblem& problem : doc.runtime)
    {
        const S32 line   = llmax(0, problem.line);
        const S32 column = llmax(0, problem.column);
        add(line, column, problem.column >= 0, line, column, ALCodeEditor::Mark::Runtime, "ERROR", getString("OriginRuntime"), problem.message);
    }
    if (!doc.definitionsError.empty())
    {
        Doc::Shown row;
        row.level   = "NOTE";
        row.origin  = getString("OriginDefinitions");
        row.message = doc.definitionsError;
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
    doc.editor->setDecorations(std::move(decorations));
    if (&doc == active())
    {
        fillProblems(&doc);
    }
}

void ALFloaterScriptStudio::fillProblems(const Doc* doc)
{
    mProblems->deleteAllItems();
    if (!doc)
    {
        return;
    }
    for (size_t i = 0; i < doc->shown.size(); ++i)
    {
        const Doc::Shown& problem = doc->shown[i];
        LLSD              row;
        row["value"]                = static_cast<S32>(i);
        row["columns"][0]["column"] = "line";
        row["columns"][0]["value"]  = (problem.fileName.empty() ? std::string() : problem.fileName + ":") +
                                     (problem.hasColumn ? llformat("%d:%d", problem.line + 1, problem.column + 1) : llformat("%d", problem.line + 1));
        row["columns"][1]["column"] = "level";
        row["columns"][1]["value"]  = problem.level;
        row["columns"][2]["column"] = "source";
        row["columns"][2]["value"]  = problem.origin;
        row["columns"][3]["column"] = "message";
        row["columns"][3]["value"]  = problem.message;
        mProblems->addElement(row);
    }
    if (doc->shown.empty())
    {
        const bool current = doc->loaded && doc->analysisVersion == doc->editor->document().version();
        mProblems->setCommentText(current ? getString("NoProblems") : std::string());
    }
}

void ALFloaterScriptStudio::onProblemSelected()
{
    Doc*              doc  = active();
    LLScrollListItem* item = mProblems->getFirstSelected();
    if (!doc || !item)
    {
        return;
    }
    const size_t index = static_cast<size_t>(item->getValue().asInteger());
    if (index >= doc->shown.size())
    {
        return;
    }
    const Doc::Shown& problem = doc->shown[index];
    if (!problem.file.empty())
    {
        // In an include: opened in a tab of its own where it is a script
        // or a notecard in the world; a file on disk is only named.
        ALScriptRef ref;
        std::string file;
        if (ALScriptPreprocessor::refOf(problem.file, ref))
        {
            const LLInventoryItem* item = ref.inInventory() ? gInventory.getItem(ref.item)
                                          : gObjectList.findObject(ref.object) ? gObjectList.findObject(ref.object)->getInventoryItem(ref.item)
                                                                               : nullptr;
            if (!item || item->getType() != LLAssetType::AT_LSL_TEXT)
            {
                LLStringUtil::format_map_t args;
                args["[FILE]"] = problem.fileName;
                setStatus(getString("IncludeIsNotecard", args));
                return;
            }
            openScript(ref, problem.fileName);
            if (const size_t opened = indexOf(ref); opened != NONE)
            {
                Doc& include = *mDocs[opened];
                if (include.loaded)
                {
                    include.editor->goToLine(problem.line);
                }
                else
                {
                    include.pendingLine = problem.line;
                }
            }
        }
        else if (ALScriptPreprocessor::fileOf(problem.file, file))
        {
            LLStringUtil::format_map_t args;
            args["[FILE]"] = file;
            setStatus(getString("IncludeOnDisk", args));
        }
        return;
    }
    doc->editor->setCaret(ALTextPos(problem.line, problem.hasColumn ? problem.column : 0));
    doc->editor->setFocus(true);
}

// --- the name at the caret ----------------------------------------------------------

void ALFloaterScriptStudio::askSymbol(Doc& doc, ALEditorCommand command, const ALTextRange& word)
{
    doc.symbolCommand = command;
    doc.symbolVersion = doc.editor->document().version();
    doc.symbolAt      = word.begin;
    askAnalyzer(doc, ALScriptAnalysis::Kind::References, word.begin);
}

void ALFloaterScriptStudio::symbolAnswered(Doc& doc, const ALScriptAnalysis::Result& result)
{
    // Of another question, or of a text that has moved on.
    if (result.version != doc.symbolVersion || ALTextPos(result.line, result.column) != doc.symbolAt || doc.symbolCommand == ALEditorCommand::None)
    {
        return;
    }
    const ALEditorCommand command = doc.symbolCommand;
    ALScriptReferences    refs    = result.references;
    doc.symbolCommand             = ALEditorCommand::None;
    if (preprocessed(doc) && doc.expanded.valid && doc.expanded.version == result.version)
    {
        // Back to the source; what stands in an include is not this
        // script's to go to or rename.
        const ALSourceMap& map = doc.expanded.map;
        if (refs.hasDefinition && mapSpan(map, refs.definition) != 0)
        {
            refs.hasDefinition = false;
            refs.renamable     = false;
        }
        std::vector<ALScriptSpan> kept;
        for (ALScriptSpan span : refs.references)
        {
            if (mapSpan(map, span) == 0)
            {
                kept.push_back(span);
            }
            else
            {
                refs.renamable = false;
            }
        }
        refs.references = std::move(kept);
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = refs.found ? refs.name : doc.editor->document().text(doc.editor->identifierAt(doc.symbolAt));
    if (!refs.found)
    {
        setStatus(getString("NoReferences", args));
        return;
    }
    args["[COUNT]"] = std::to_string(refs.references.size());
    switch (command)
    {
        case ALEditorCommand::GoToDefinition:
            if (refs.hasDefinition)
            {
                doc.editor->goTo(rangeOf(refs.definition));
                doc.editor->setFocus(true);
            }
            else
            {
                setStatus(getString("NoDefinition", args));
            }
            break;
        case ALEditorCommand::FindReferences:
        {
            doc.references = refs;
            std::vector<ALTextRange> lit;
            lit.reserve(refs.references.size());
            for (const ALScriptSpan& span : refs.references)
            {
                lit.push_back(rangeOf(span));
            }
            doc.editor->setHighlights(std::move(lit));
            fillReferences(&doc);
            showBottom("references_tab");
            setStatus(getString("ReferencesFound", args));
            break;
        }
        case ALEditorCommand::Rename:
            if (refs.renamable)
            {
                askNewName(doc, refs);
            }
            else
            {
                setStatus(getString("NotRenamable", args));
            }
            break;
        default:
            break;
    }
}

void ALFloaterScriptStudio::askNewName(Doc& doc, const ALScriptReferences& refs)
{
    const std::string         id       = doc.id;
    const U32                 version  = doc.symbolVersion;
    const std::string         old_name = refs.name;
    std::vector<ALTextRange>  places;
    places.reserve(refs.references.size());
    for (const ALScriptSpan& span : refs.references)
    {
        places.push_back(rangeOf(span));
    }
    const LLHandle<LLFloater> handle = getHandle();
    ALQuickOpen* quick = quickOpen(
        {}, getString("RenamePlaceholder"), getString("RenameTitle"),
        [handle, id, version, places, old_name](const std::string& typed) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->renameTo(id, version, places, old_name, typed);
            }
        },
        mEditorHost, 420, 56);
    if (!quick)
    {
        return;
    }
    // The row under the field says what return will do with what is typed.
    const S32 count = static_cast<S32>(places.size());
    quick->onQueryChanged([handle, quick, count, old_name](const std::string& typed) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        if (!studio)
        {
            return;
        }
        std::string name = typed;
        LLStringUtil::trim(name);
        LLStringUtil::format_map_t args;
        args["[NAME]"]  = old_name;
        args["[NEW]"]   = name;
        args["[COUNT]"] = std::to_string(count);
        if (name.empty())
        {
            quick->setHint(studio->getString("RenameHint", args));
        }
        else if (!isIdentifier(name))
        {
            args["[NAME]"] = name;
            quick->setHint(studio->getString("RenameBadName", args));
        }
        else if (name == old_name)
        {
            quick->setHint(studio->getString("RenameSame", args));
        }
        else
        {
            quick->setHint(studio->getString("RenameTo", args));
        }
    });
    quick->setQuery(old_name);
    quick->takeFocus();
}

void ALFloaterScriptStudio::renameTo(const std::string& id, U32 version, const std::vector<ALTextRange>& places, const std::string& old_name, const std::string& new_name)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc&        doc  = *mDocs[index];
    std::string name = new_name;
    LLStringUtil::trim(name);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = name;
    if (!isIdentifier(name))
    {
        setStatus(getString("RenameBadName", args), true);
        return;
    }
    if (name == old_name)
    {
        doc.editor->setFocus(true);
        return;
    }
    if (doc.editor->document().version() != version)
    {
        setStatus(getString("RenameStale", args), true);
        return;
    }
    std::vector<std::pair<ALTextRange, std::string>> edits;
    edits.reserve(places.size());
    for (const ALTextRange& place : places)
    {
        edits.emplace_back(place, name);
    }
    args["[COUNT]"] = std::to_string(edits.size());
    if (doc.editor->replaceAll(std::move(edits)))
    {
        doc.editor->undoJournal().label("rename");
        setStatus(getString("Renamed", args));
    }
    doc.editor->setFocus(true);
}

void ALFloaterScriptStudio::fillReferences(const Doc* doc)
{
    mReferences->deleteAllItems();
    if (!doc || !doc->references.found)
    {
        return;
    }
    const ALTextDocument& text = doc->editor->document();
    for (size_t i = 0; i < doc->references.references.size(); ++i)
    {
        const ALScriptSpan& span = doc->references.references[i];
        std::string         line = text.text(ALTextRange(ALTextPos(span.line, 0), ALTextPos(span.line, text.lineLength(span.line))));
        LLStringUtil::trim(line);
        LLSD row;
        row["value"]                = static_cast<S32>(i);
        row["columns"][0]["column"] = "line";
        row["columns"][0]["value"]  = llformat("%d:%d", span.line + 1, span.column + 1);
        row["columns"][1]["column"] = "text";
        row["columns"][1]["value"]  = line;
        mReferences->addElement(row);
    }
}

void ALFloaterScriptStudio::onReferenceChosen()
{
    Doc*              doc  = active();
    LLScrollListItem* item = mReferences->getFirstSelected();
    if (!doc || !item)
    {
        return;
    }
    const size_t index = static_cast<size_t>(item->getValue().asInteger());
    if (index < doc->references.references.size())
    {
        doc->editor->goTo(rangeOf(doc->references.references[index]));
        doc->editor->setFocus(true);
    }
}

void ALFloaterScriptStudio::goToLine()
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    const std::string         id     = doc->id;
    const ALTextPos           was    = doc->editor->caret();
    const LLHandle<LLFloater> handle = getHandle();
    // The editor at the place typed, while it is typed; return leaves it
    // there, escape puts it back.
    auto docOf = [handle, id]() -> Doc* {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(id) : NONE;
        return index == NONE ? nullptr : studio->mDocs[index].get();
    };
    auto placeOf = [](const Doc& doc, const std::string& typed, S32& line, S32& column) {
        placeTyped(typed, line, column);
        const S32 count = doc.editor->document().lineCount();
        return line >= 1 && line <= count;
    };
    ALQuickOpen* quick = quickOpen(
        {}, getString("GoToLinePlaceholder"), getString("GoToLineTitle"),
        [docOf, placeOf, was](const std::string& typed) {
            Doc* doc = docOf();
            if (!doc)
            {
                return;
            }
            S32 line, column;
            if (placeOf(*doc, typed, line, column))
            {
                doc->editor->goTo(ALTextPos(line - 1, llmax(0, column - 1)));
            }
            else
            {
                doc->editor->goTo(was);
            }
            doc->editor->setFocus(true);
        },
        mEditorHost, 420, 56,
        [docOf, was]() {
            if (Doc* doc = docOf())
            {
                doc->editor->goTo(was);
            }
        });
    if (!quick)
    {
        return;
    }
    quick->onQueryChanged([handle, docOf, placeOf, quick, was](const std::string& typed) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        Doc*                   doc    = docOf();
        if (!studio || !doc)
        {
            return;
        }
        S32                        line, column;
        const bool                 there = placeOf(*doc, typed, line, column);
        LLStringUtil::format_map_t args;
        args["[COUNT]"] = std::to_string(doc->editor->document().lineCount());
        args["[LINE]"]  = std::to_string(line);
        args["[COL]"]   = std::to_string(column);
        std::string trimmed = typed;
        LLStringUtil::trim(trimmed);
        if (trimmed.empty())
        {
            quick->setHint(studio->getString("GoToLineHint", args));
            doc->editor->goTo(was);
        }
        else if (there)
        {
            quick->setHint(studio->getString(column > 0 ? "GoToLineGoColumn" : "GoToLineGo", args));
            doc->editor->goTo(ALTextPos(line - 1, llmax(0, column - 1)));
        }
        else
        {
            quick->setHint(studio->getString("GoToLineNone", args));
        }
    });
    quick->setQuery(std::string());
}

void ALFloaterScriptStudio::goToSymbol()
{
    Doc* doc = active();
    if (!doc || doc->outline.empty())
    {
        return;
    }
    std::vector<ALQuickOpen::Candidate> candidates;
    candidates.reserve(doc->outline.size());
    for (size_t i = 0; i < doc->outline.size(); ++i)
    {
        const ALScriptOutlineEntry& entry = doc->outline[i];
        ALQuickOpen::Candidate      one;
        one.label  = entry.name;
        one.detail = entry.detail.empty() ? kindName(entry.kind) : kindName(entry.kind) + "  " + entry.detail;
        one.value  = std::to_string(i);
        candidates.push_back(std::move(one));
    }
    const LLHandle<LLFloater> handle = getHandle();
    quickOpen(std::move(candidates), getString("GoToSymbolPlaceholder"), getString("GoToSymbolTitle"), [handle](const std::string& value) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        Doc*                   doc    = studio ? studio->active() : nullptr;
        const size_t           index  = static_cast<size_t>(atoi(value.c_str()));
        if (doc && index < doc->outline.size())
        {
            doc->editor->goTo(rangeOf(doc->outline[index].nameSpan));
            doc->editor->setFocus(true);
        }
    }, mEditorHost);
}

// --- the outline, the breadcrumb and the inspector -----------------------------------

void ALFloaterScriptStudio::pumpCaret()
{
    Doc* doc = active();
    if (!doc || !doc->loaded || doc->sourceView)
    {
        return;
    }
    const ALTextPos caret = doc->editor->caret();
    const F64       now   = LLTimer::getTotalSeconds();
    if (caret != doc->caretSeen)
    {
        doc->caretSeen  = caret;
        doc->inspectDue = now + ANALYSIS_DELAY;
        refreshBreadcrumb(*doc);
        // The lit places go once the caret has left them all.
        if (!doc->editor->highlights().empty() && !doc->editor->highlighted(caret))
        {
            doc->editor->clearHighlights();
        }
    }
    if (doc->inspectDue > 0.0 && now >= doc->inspectDue)
    {
        doc->inspectDue = 0.0;
        const ALTextRange word    = doc->editor->identifierAtCaret();
        const U32         version = doc->editor->document().version();
        if (word.empty())
        {
            doc->inspectAt = ALTextPos(-1, -1);
            mSymbol->setText(LLStringUtil::null);
        }
        else if (word.begin != doc->inspectAt || version != doc->inspectVersion)
        {
            doc->inspectAt      = word.begin;
            doc->inspectVersion = version;
            askAnalyzer(*doc, ALScriptAnalysis::Kind::Inspect, word.begin);
        }
    }
}

void ALFloaterScriptStudio::inspected(Doc& doc, const ALScriptAnalysis::Result& result)
{
    // Still about the word the caret is on, and this script.
    if (&doc != active() || ALTextPos(result.line, result.column) != doc.inspectAt)
    {
        return;
    }
    std::string text;
    if (result.hover.found)
    {
        text = result.hover.label;
        if (!result.hover.documentation.empty())
        {
            text += "\n\n" + result.hover.documentation;
        }
        if (!result.hover.link.empty())
        {
            text += "\n" + result.hover.link;
        }
    }
    mSymbol->setText(text);
}

std::string ALFloaterScriptStudio::kindName(ALScriptSymbolKind kind) const
{
    switch (kind)
    {
        case ALScriptSymbolKind::Keyword:   return getString("KindKeyword");
        case ALScriptSymbolKind::Variable:  return getString("KindVariable");
        case ALScriptSymbolKind::Parameter: return getString("KindParameter");
        case ALScriptSymbolKind::Function:  return getString("KindFunction");
        case ALScriptSymbolKind::Field:     return getString("KindField");
        case ALScriptSymbolKind::Type:      return getString("KindType");
        case ALScriptSymbolKind::Constant:  return getString("KindConstant");
        case ALScriptSymbolKind::Event:     return getString("KindEvent");
        case ALScriptSymbolKind::State:     return getString("KindState");
        case ALScriptSymbolKind::Label:     return getString("KindLabel");
        case ALScriptSymbolKind::Module:    return getString("KindModule");
    }
    return std::string();
}

void ALFloaterScriptStudio::refreshOutline(Doc& doc)
{
    if (&doc != active())
    {
        return;
    }
    mOutline->deleteAllItems();
    for (size_t i = 0; i < doc.outline.size(); ++i)
    {
        const ALScriptOutlineEntry& entry = doc.outline[i];
        LLSD                        row;
        row["value"]                = static_cast<S32>(i);
        row["columns"][0]["column"] = "symbol";
        row["columns"][0]["value"]  = std::string(static_cast<size_t>(entry.depth) * 4, ' ') + entry.name;
        row["columns"][0]["tool_tip"] = entry.detail;
        row["columns"][1]["column"] = "kind";
        row["columns"][1]["value"]  = kindName(entry.kind);
        mOutline->addElement(row);
    }
    refreshBreadcrumb(doc);
}

void ALFloaterScriptStudio::refreshBreadcrumb(Doc& doc)
{
    if (&doc != active())
    {
        return;
    }
    const ALTextPos               caret = doc.editor->caret();
    std::vector<ALJumpBar::Crumb> crumbs;
    LLStringUtil::format_map_t    args;

    // The script itself, offering what it declares at the top.
    ALJumpBar::Crumb root;
    root.label     = doc.name;
    root.value     = "top";
    args["[NAME]"] = doc.name;
    root.toolTip   = getString("CrumbRootTip", args);
    for (size_t i = 0; i < doc.outline.size(); ++i)
    {
        if (doc.outline[i].depth == 0)
        {
            root.alternatives.emplace_back(doc.outline[i].name, std::to_string(i));
        }
    }
    crumbs.push_back(std::move(root));

    // Then each symbol around the caret, the outermost first: at each
    // depth the last entry that holds the caret, within the one before,
    // offering the others at its depth in the same holder.
    size_t parent = NONE;
    for (S32 depth = 0;; ++depth)
    {
        size_t found = NONE;
        for (size_t i = 0; i < doc.outline.size(); ++i)
        {
            const ALScriptOutlineEntry& entry = doc.outline[i];
            if (entry.depth == depth && holds(entry.span, caret) && (parent == NONE || within(entry.span, doc.outline[parent].span)))
            {
                found = i;
            }
        }
        if (found == NONE)
        {
            break;
        }
        ALJumpBar::Crumb crumb;
        crumb.label    = doc.outline[found].name;
        crumb.value    = std::to_string(found);
        args["[NAME]"] = crumb.label;
        crumb.toolTip  = getString("CrumbTip", args);
        for (size_t i = 0; i < doc.outline.size(); ++i)
        {
            const ALScriptOutlineEntry& entry = doc.outline[i];
            if (entry.depth == depth && (parent == NONE || within(entry.span, doc.outline[parent].span)))
            {
                crumb.alternatives.emplace_back(entry.name, std::to_string(i));
            }
        }
        if (crumb.alternatives.size() < 2)
        {
            crumb.alternatives.clear();
        }
        crumbs.push_back(std::move(crumb));
        parent = found;
    }
    mBreadcrumb->setPath(std::move(crumbs));
    args["[LINE]"] = std::to_string(caret.line + 1);
    args["[COL]"]  = std::to_string(caret.column + 1);
    mBreadcrumb->setTrailer(getString("CaretPosition", args));
}

void ALFloaterScriptStudio::onCrumbChosen(size_t, const std::string& value)
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    if (value == "top")
    {
        doc->editor->goTo(ALTextPos(0, 0));
    }
    else
    {
        const size_t index = static_cast<size_t>(atoi(value.c_str()));
        if (index < doc->outline.size())
        {
            doc->editor->goTo(rangeOf(doc->outline[index].nameSpan));
        }
    }
    doc->editor->setFocus(true);
}

void ALFloaterScriptStudio::onOutlineChosen()
{
    Doc*              doc  = active();
    LLScrollListItem* item = mOutline->getFirstSelected();
    if (!doc || !item)
    {
        return;
    }
    const size_t index = static_cast<size_t>(item->getValue().asInteger());
    if (index < doc->outline.size())
    {
        doc->editor->goTo(rangeOf(doc->outline[index].nameSpan));
        doc->editor->setFocus(true);
    }
}

void ALFloaterScriptStudio::showBottom(const char* tab)
{
    mFolds.setCollapsed("bottom", false);
    mBottomTabs->selectTabByName(tab);
}

// --- what scripts say ---------------------------------------------------------------

void ALFloaterScriptStudio::runtimeEvent(const ALScriptWorkspace::RuntimeEvent& event)
{
    static const LLUIColor runtime_color = LLUIColorTable::instance().getColor("CodeMarkRuntime", LLColor4::magenta);

    // The object, offered in the filter the first time it speaks.
    if (event.root.notNull() && mOutputObjects.emplace(event.root, event.objectName).second)
    {
        mOutputFilter->add(event.objectName, LLSD(event.root));
    }

    ALOutputList::Entry entry;
    entry.time   = clockOf(event.time);
    entry.source = event.scriptName.empty() ? event.objectName : event.objectName + " / " + event.scriptName;
    entry.kind   = getString(event.isError ? "KindError" : event.channel == ALScriptWorkspace::RuntimeEvent::Channel::OwnerSay ? "KindOwnerSay" : "KindDebug");
    entry.text   = oneLine(event.isError && !event.error.empty() ? event.error : event.message);
    if (event.isError)
    {
        entry.color = runtime_color.get();
        if (event.line >= 0)
        {
            entry.text += llformat(" (line %d)", event.line + 1);
        }
    }
    entry.key             = event.root;
    entry.value["prim"]   = event.prim;
    entry.value["item"]   = event.item;
    entry.value["name"]   = event.scriptName;
    entry.value["line"]   = event.line;
    entry.value["column"] = event.column;
    mOutput->append(std::move(entry));

    // A run-time error in a script that is open marks its line.
    if (event.isError && event.item.notNull())
    {
        const size_t index = indexOf(ALScriptRef(event.prim, event.item));
        if (index != NONE)
        {
            Doc::RuntimeProblem problem;
            problem.line    = event.line;
            problem.column  = event.column;
            problem.message = event.error.empty() ? oneLine(event.message) : event.error;
            mDocs[index]->runtime.push_back(std::move(problem));
            refreshProblems(*mDocs[index]);
        }
    }
}

void ALFloaterScriptStudio::onOutputFilter()
{
    const LLUUID root = mOutputFilter->getValue().asUUID();
    if (root.isNull())
    {
        mOutput->setFilter(nullptr);
    }
    else
    {
        mOutput->setFilter([root](const ALOutputList::Entry& entry) { return entry.key.asUUID() == root; });
    }
}

void ALFloaterScriptStudio::onOutputChosen()
{
    const ALOutputList::Entry* entry = mOutput->chosen();
    if (!entry || entry->value["item"].asUUID().isNull())
    {
        return;
    }
    const ALScriptRef ref(entry->value["prim"].asUUID(), entry->value["item"].asUUID());
    const S32         line   = entry->value["line"].asInteger();
    const S32         column = entry->value["column"].asInteger();
    size_t            index  = indexOf(ref);
    if (index == NONE)
    {
        // The script it names, opened; the line once it has loaded.
        openScript(ref, entry->value["name"].asString());
        index = indexOf(ref);
        if (index != NONE)
        {
            mDocs[index]->pendingLine = line;
        }
        return;
    }
    activate(index);
    if (line >= 0)
    {
        mDocs[index]->editor->goTo(ALTextPos(line, llmax(0, column)));
        mDocs[index]->editor->setFocus(true);
    }
}

// --- before a save --------------------------------------------------------------------

bool ALFloaterScriptStudio::preflight(Doc& doc)
{
    static LLCachedControl<bool> wanted(gSavedSettings, "ALScriptStudioPreflight", true);
    if (!wanted || doc.sourceView)
    {
        return true;
    }
    const F64 now = LLTimer::getTotalSeconds();
    if (doc.saveAnywayUntil > now)
    {
        // Asked twice: over whatever was found, by the check here and by
        // the preprocessor after it; the upload closes the window.
        return true;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (doc.analysisVersion != doc.editor->document().version())
    {
        // Checked first; the save follows the answer.
        doc.saveAfterCheck = true;
        scheduleAnalysis(doc, true);
        setStatus(getString("Preflight", args));
        return false;
    }
    S32 errors = 0;
    for (const ALScriptProblem& problem : doc.analysis)
    {
        if (problem.severity == ALScriptProblem::Severity::Error)
        {
            ++errors;
        }
    }
    if (doc.expanded.valid && doc.expanded.version == doc.analysisVersion)
    {
        for (const ALScriptProblem& problem : doc.expanded.problems)
        {
            if (problem.severity == ALScriptProblem::Severity::Error)
            {
                ++errors;
            }
        }
    }
    if (errors == 0)
    {
        return true;
    }
    args["[COUNT]"] = std::to_string(errors);
    setStatus(getString("PreflightErrors", args), true);
    doc.saveAnywayUntil = now + SAVE_ANYWAY;
    // The first of them, in sight.
    showBottom("problems_tab");
    for (size_t i = 0; i < doc.shown.size(); ++i)
    {
        if (doc.shown[i].level == "ERROR" && doc.shown[i].origin != getString("OriginCompiler"))
        {
            mProblems->selectNthItem(static_cast<S32>(i));
            onProblemSelected();
            break;
        }
    }
    return false;
}

// --- the explorer -----------------------------------------------------------------

void ALFloaterScriptStudio::pumpExplorer()
{
    const F64 now = LLTimer::getTotalSeconds();
    if (now < mExplorerPolled + EXPLORER_POLL)
    {
        return;
    }
    mExplorerPolled = now;
    std::vector<LLUUID> roots = selectedRoots();
    if (roots != mExplorerRoots)
    {
        mExplorerRoots = std::move(roots);
        refreshExplorer();
    }
}

void ALFloaterScriptStudio::refreshExplorer()
{
    mExplorerModel.clear();
    auto add = [this](LLViewerObject* object) {
        if (!object || object->isAvatar())
        {
            return;
        }
        LLViewerObject* root = object->getRootEdit() ? object->getRootEdit() : object;
        for (const ExplorerObject& known : mExplorerModel)
        {
            if (known.root == root->getID())
            {
                return;
            }
        }
        ExplorerObject one;
        one.root = root->getID();
        one.name = objectNameOf(root, getString("ObjectUnnamed"));
        ExplorerPrim first;
        first.id   = root->getID();
        first.name = one.name;
        one.prims.push_back(std::move(first));
        for (const LLPointer<LLViewerObject>& child : root->getChildren())
        {
            if (child && !child->isAvatar())
            {
                ExplorerPrim prim;
                prim.id   = child->getID();
                prim.name = objectNameOf(child, LLStringUtil::null);
                one.prims.push_back(std::move(prim));
            }
        }
        mExplorerModel.push_back(std::move(one));
    };
    // In hand: what is selected, then the objects of the scripts open.
    for (const LLUUID& root : mExplorerRoots)
    {
        add(gObjectList.findObject(root));
    }
    for (const std::unique_ptr<Doc>& doc : mDocs)
    {
        if (!doc->ref.inInventory() && !doc->sourceView)
        {
            add(gObjectList.findObject(doc->ref.object));
        }
    }
    fillExplorer();
    const LLHandle<LLFloater> handle = getHandle();
    for (const ExplorerObject& object : mExplorerModel)
    {
        for (const ExplorerPrim& prim : object.prims)
        {
            ALScriptWorkspace::instance().listContents(prim.id, [handle](const ALScriptWorkspace::Contents& contents) {
                if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                {
                    studio->explorerContents(contents);
                }
            });
        }
    }
}

void ALFloaterScriptStudio::explorerContents(const ALScriptWorkspace::Contents& contents)
{
    for (ExplorerObject& object : mExplorerModel)
    {
        for (ExplorerPrim& prim : object.prims)
        {
            if (prim.id != contents.prim)
            {
                continue;
            }
            prim.fetched = contents.fetched;
            prim.items   = contents.items;
            if (!contents.name.empty())
            {
                prim.name = contents.name;
                if (prim.id == object.root)
                {
                    object.name = contents.name;
                }
            }
            for (const ALScriptWorkspace::Item& item : prim.items)
            {
                if (item.script && !mRunningKnown.count({ prim.id, item.id }))
                {
                    ALScriptWorkspace::instance().askRunning(ALScriptRef(prim.id, item.id));
                }
            }
            fillExplorer();
            return;
        }
    }
}

void ALFloaterScriptStudio::fillExplorer()
{
    const S32 was = mExplorer->getFirstSelectedIndex();
    mExplorer->deleteAllItems();
    auto row = [&](const LLSD& value, const std::string& name, const std::string& kind, const std::string& run) {
        LLSD r;
        r["value"]                = value;
        r["columns"][0]["column"] = "name";
        r["columns"][0]["value"]  = name;
        r["columns"][1]["column"] = "kind";
        r["columns"][1]["value"]  = kind;
        r["columns"][2]["column"] = "run";
        r["columns"][2]["value"]  = run;
        mExplorer->addElement(r);
    };
    for (const ExplorerObject& object : mExplorerModel)
    {
        LLSD at;
        at["root"] = object.root;
        row(at, object.name, getString("KindObject"), LLStringUtil::null);
        const bool        many   = object.prims.size() > 1;
        const std::string indent = many ? "        " : "    ";
        for (const ExplorerPrim& prim : object.prims)
        {
            if (many)
            {
                at["prim"] = prim.id;
                row(at, "    " + (prim.name.empty() ? getString("ObjectUnnamed") : prim.name), getString("KindPrim"), LLStringUtil::null);
            }
            for (const ALScriptWorkspace::Item& item : prim.items)
            {
                LLSD value;
                value["root"]   = object.root;
                value["prim"]   = prim.id;
                value["item"]   = item.id;
                value["name"]   = item.name;
                value["script"] = item.script;
                std::string run;
                if (item.script)
                {
                    S32          state = -1;
                    const size_t index = indexOf(ALScriptRef(prim.id, item.id));
                    if (index != NONE)
                    {
                        state = mDocs[index]->running;
                    }
                    if (state < 0)
                    {
                        const auto known = mRunningKnown.find({ prim.id, item.id });
                        if (known != mRunningKnown.end())
                        {
                            state = known->second ? 1 : 0;
                        }
                    }
                    if (state >= 0)
                    {
                        run = getString(state ? "RunningYes" : "RunningNo");
                    }
                }
                row(value, indent + item.name, getString(item.script ? (item.lua ? "KindLua" : "KindScript") : "KindNotecard"), run);
            }
        }
    }
    if (was >= 0 && was < mExplorer->getItemCount())
    {
        mExplorer->selectNthItem(was);
    }
}

bool ALFloaterScriptStudio::explorerChoice(ALScriptRef& ref, std::string& name) const
{
    const LLScrollListItem* item = mExplorer->getFirstSelected();
    if (!item)
    {
        return false;
    }
    const LLSD& value = item->getValue();
    if (!value.isMap() || !value.has("item") || !value["script"].asBoolean())
    {
        return false;
    }
    ref  = ALScriptRef(value["prim"].asUUID(), value["item"].asUUID());
    name = value["name"].asString();
    return true;
}

void ALFloaterScriptStudio::onExplorerChosen()
{
    ALScriptRef ref;
    std::string name;
    if (explorerChoice(ref, name))
    {
        openScript(ref, name);
    }
}

void ALFloaterScriptStudio::onExplorerAction(const std::string& action)
{
    if (action == "refresh")
    {
        mRunningKnown.clear();
        refreshExplorer();
        return;
    }
    ALScriptRef ref;
    std::string name;
    if (!explorerChoice(ref, name))
    {
        return;
    }
    if (action == "open")
    {
        openScript(ref, name);
    }
    else if (action == "start" || action == "stop")
    {
        if (ALScriptWorkspace::instance().setRunning(ref, action == "start"))
        {
            ALScriptWorkspace::instance().askRunning(ref);
        }
    }
    else if (action == "reset")
    {
        ALScriptWorkspace::instance().reset(ref);
    }
}

void ALFloaterScriptStudio::runningState(const ALScriptWorkspace::RunningState& state)
{
    mRunningKnown[{ state.ref.object, state.ref.item }] = state.running;
    const size_t index = indexOf(state.ref);
    if (index != NONE)
    {
        Doc& doc    = *mDocs[index];
        doc.running = state.running ? 1 : 0;
        if (!state.compileTarget.empty())
        {
            doc.language.compileTarget = state.compileTarget;
        }
        if (&doc == active())
        {
            refreshToolbar();
        }
    }
    fillExplorer();
}

// --- copying from a list -------------------------------------------------------------

// static
LLEditMenuHandler* ALFloaterScriptStudio::focusedEditHandler()
{
    return dynamic_cast<LLEditMenuHandler*>(gFocusMgr.getKeyboardFocus());
}

void ALFloaterScriptStudio::listMenuFor(LLScrollListCtrl* list)
{
    list->setRightMouseDownCallback([this, list](LLUICtrl*, S32 x, S32 y, MASK) { showListMenu(list, x, y); });
}

void ALFloaterScriptStudio::showListMenu(LLScrollListCtrl* list, S32 x, S32 y)
{
    if (!LLMenuGL::sMenuContainer)
    {
        return;
    }
    if (LLContextMenu* old = mListMenuHandle.get())
    {
        old->die();
        mListMenuHandle.markDead();
    }
    mListMenuFor = list;
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
    commit.add("List.Copy", [this](LLUICtrl*, const LLSD&) {
        if (mListMenuFor)
        {
            mListMenuFor->copy();
        }
    });
    commit.add("List.CopyAll", [this](LLUICtrl*, const LLSD&) {
        if (!mListMenuFor)
        {
            return;
        }
        std::string text;
        for (LLScrollListItem* item : mListMenuFor->getAllData())
        {
            text += item->getContentsCSV() + "\n";
        }
        LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size()));
    });
    enable.add("List.Enable", [this](LLUICtrl*, const LLSD& param) {
        if (!mListMenuFor)
        {
            return false;
        }
        return param.asString() == "copy" ? mListMenuFor->canCopy() : mListMenuFor->getItemCount() > 0;
    });
    LLContextMenu* menu = LLUICtrlFactory::createFromFile<LLContextMenu>("menu_list_copy.xml", LLMenuGL::sMenuContainer, LLMenuHolderGL::child_registry_t::instance());
    if (!menu)
    {
        return;
    }
    mListMenuHandle = menu->getHandle();
    menu->show(x, y);
    LLMenuGL::showPopup(list, menu, x, y);
}

// --- closing ---------------------------------------------------------------------

void ALFloaterScriptStudio::closeDocument(std::string_view id)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    if (doc.editor->isDirty() && doc.modifiable)
    {
        LLSD args;
        args["[NAME]"] = doc.name;
        LLNotificationsUtil::add("ScriptStudioSaveChanges", args, LLSD(),
                                 [this, id = std::string(id)](const LLSD& notification, const LLSD& response) {
                                     closeDocumentAnswered(id, LLNotificationsUtil::getSelectedOption(notification, response));
                                 });
        return;
    }
    letGoOf(index);
}

void ALFloaterScriptStudio::closeDocumentAnswered(const std::string& id, S32 option)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    switch (option)
    {
        case 0:  // save
            mDocs[index]->closeAfterSave = true;
            save(*mDocs[index]);
            break;
        case 1:  // don't save
            letGoOf(index);
            break;
        default:  // cancel
            break;
    }
}

void ALFloaterScriptStudio::letGoOf(size_t index)
{
    if (index >= mDocs.size())
    {
        return;
    }
    // A script's expanded tab goes with the script. Highest index first,
    // so the other stays where it was found.
    std::vector<size_t> going{ index };
    if (!mDocs[index]->sourceView)
    {
        if (const size_t companion = indexOf(mDocs[index]->id + ":expanded"); companion != NONE)
        {
            going.push_back(companion);
        }
    }
    std::sort(going.rbegin(), going.rend());
    for (size_t i : going)
    {
        Doc& doc = *mDocs[i];
        doc.changed.release();
        mEditorHost->removeChild(doc.editor);
        doc.editor->die();
        mDocs.erase(mDocs.begin() + i);
    }
    if (mDocs.empty())
    {
        mActive = NONE;
        fillTabs();
        refreshToolbar();
        fillProblems(nullptr);
        fillReferences(nullptr);
        mOutline->deleteAllItems();
        mBreadcrumb->setPath({});
        mBreadcrumb->setTrailer(LLStringUtil::null);
        mSymbol->setText(LLStringUtil::null);
    }
    else
    {
        activate(llmin(index, mDocs.size() - 1));
    }
    refreshExplorer();
}

// --- the menu and the toolbar ---------------------------------------------------

void ALFloaterScriptStudio::onMenuAction(const LLSD& param)
{
    const std::string action = param.asString();
    Doc*              doc    = active();
    if (action == "save")
    {
        if (doc)
        {
            save(*doc);
        }
    }
    else if (action == "save_all")
    {
        saveAll();
    }
    else if (action == "revert")
    {
        if (doc)
        {
            revert(*doc);
        }
    }
    else if (action == "close")
    {
        if (doc)
        {
            closeDocument(doc->ref.id());
        }
    }
    else if (action == "load_file")
    {
        loadFromFile();
    }
    else if (action == "save_file")
    {
        saveToFile();
    }
    else if (action == "undo")
    {
        undo();
    }
    else if (action == "redo")
    {
        redo();
    }
    else if (action == "cut" || action == "copy" || action == "paste" || action == "select_all")
    {
        // Whatever has the keyboard: a list of problems is worth copying
        // as much as the text is.
        LLEditMenuHandler* handler = focusedEditHandler();
        if (!handler && doc)
        {
            handler = doc->editor;
        }
        if (handler)
        {
            if (action == "cut")
            {
                handler->cut();
            }
            else if (action == "copy")
            {
                handler->copy();
            }
            else if (action == "paste")
            {
                handler->paste();
            }
            else
            {
                handler->selectAll();
            }
        }
    }
    else if (doc && action == "toggle_comment")
    {
        doc->editor->toggleComment();
    }
    else if (doc && action == "complete")
    {
        doc->editor->perform(ALEditorCommand::Complete);
    }
    else if (doc && action == "fold")
    {
        doc->editor->perform(ALEditorCommand::Fold);
    }
    else if (doc && action == "unfold")
    {
        doc->editor->perform(ALEditorCommand::Unfold);
    }
    else if (doc && action == "fold_all")
    {
        doc->editor->perform(ALEditorCommand::FoldAll);
    }
    else if (doc && action == "unfold_all")
    {
        doc->editor->perform(ALEditorCommand::UnfoldAll);
    }
    else if (doc && action == "go_to_definition")
    {
        doc->editor->perform(ALEditorCommand::GoToDefinition);
    }
    else if (doc && action == "find_references")
    {
        doc->editor->perform(ALEditorCommand::FindReferences);
    }
    else if (doc && action == "rename")
    {
        doc->editor->perform(ALEditorCommand::Rename);
    }
    else if (doc && action == "go_to_line")
    {
        goToLine();
    }
    else if (doc && action == "go_to_symbol")
    {
        goToSymbol();
    }
    else if (doc && action == "find")
    {
        doc->editor->perform(ALEditorCommand::Find);
    }
    else if (doc && action == "replace")
    {
        doc->editor->perform(ALEditorCommand::Replace);
    }
    else if (doc && action == "find_next")
    {
        doc->editor->perform(ALEditorCommand::FindNext);
    }
    else if (doc && action == "find_previous")
    {
        doc->editor->perform(ALEditorCommand::FindPrevious);
    }
    else if (action == "word_wrap")
    {
        mWordWrap = !mWordWrap;
        applyEditorOptions();
    }
    else if (action == "line_numbers")
    {
        mLineNumbers = !mLineNumbers;
        applyEditorOptions();
    }
    else if (action == "scroll_bar" || action == "scroll_map")
    {
        mScrollMap = action == "scroll_map";
        applyEditorOptions();
    }
    else if (action == "map_narrow" || action == "map_medium" || action == "map_wide")
    {
        mScrollMapWidth = action == "map_narrow" ? 60 : action == "map_medium" ? 90 : 130;
        applyEditorOptions();
    }
    else if (action == "map_preview")
    {
        mScrollMapPreview = !mScrollMapPreview;
        applyEditorOptions();
    }
    else if (action == "map_left")
    {
        mScrollMapLeft = !mScrollMapLeft;
        applyEditorOptions();
    }
    else if (action == "problems" || action == "references" || action == "output")
    {
        // The tab, shown; or the pane folded when it is the tab showing.
        const char* tab = action == "problems" ? "problems_tab" : action == "references" ? "references_tab" : "output_tab";
        if (onMenuCheck(param))
        {
            mFolds.setCollapsed("bottom", true);
        }
        else
        {
            showBottom(tab);
        }
    }
    else if (action == "inspector")
    {
        mFolds.toggle("inspector");
    }
    else if (action == "explorer")
    {
        mFolds.toggle("explorer");
    }
    else if (action == "preflight")
    {
        gSavedSettings.setBOOL("ALScriptStudioPreflight", !gSavedSettings.getBOOL("ALScriptStudioPreflight"));
    }
    else if (action == "preprocess")
    {
        if (Doc* doc = active(); doc && doc->loaded && !doc->sourceView)
        {
            preprocess(*doc, false);
        }
    }
    else if (action == "preproc_enabled" || action == "preproc_switch" || action == "preproc_lazy" || action == "preproc_compress" ||
             action == "preproc_disk" || action == "preproc_optimize" || action == "preproc_shrink" || action == "preproc_addstrings")
    {
        const char* setting = action == "preproc_enabled"    ? "ALScriptPreprocEnabled"
                              : action == "preproc_switch"   ? "ALScriptPreprocSwitch"
                              : action == "preproc_lazy"     ? "ALScriptPreprocLazyLists"
                              : action == "preproc_compress" ? "ALScriptPreprocCompress"
                              : action == "preproc_optimize" ? "ALScriptPreprocOptimizer"
                              : action == "preproc_shrink"   ? "ALScriptPreprocOptimizerShrinkNames"
                              : action == "preproc_addstrings" ? "ALScriptPreprocOptimizerAddStrings"
                                                               : "ALScriptPreprocDiskIncludes";
        gSavedSettings.setBOOL(setting, !gSavedSettings.getBOOL(setting));
        // What the analyzers see changes with the setting.
        for (std::unique_ptr<Doc>& doc : mDocs)
        {
            doc->expanded.valid = false;
            if (preprocessed(*doc))
            {
                preprocess(*doc, false);
            }
            scheduleAnalysis(*doc, true);
        }
    }
    else if (action == "preproc_folder")
    {
        chooseIncludeFolder();
    }
}

bool ALFloaterScriptStudio::onMenuEnable(const LLSD& param)
{
    const std::string action = param.asString();
    Doc*              doc    = active();
    if (action == "save" || action == "revert" || action == "save_file")
    {
        return doc && doc->loaded && (action == "save_file" || doc->modifiable);
    }
    if (action == "save_all")
    {
        for (const std::unique_ptr<Doc>& each : mDocs)
        {
            if (each->editor->isDirty() && each->modifiable)
            {
                return true;
            }
        }
        return false;
    }
    if (action == "close")
    {
        return doc != nullptr;
    }
    if (action == "cut" || action == "copy" || action == "paste" || action == "select_all")
    {
        LLEditMenuHandler* handler = focusedEditHandler();
        if (!handler && doc)
        {
            handler = doc->editor;
        }
        if (!handler)
        {
            return false;
        }
        return action == "cut" ? handler->canCut() : action == "copy" ? handler->canCopy() : action == "paste" ? handler->canPaste() : handler->canSelectAll();
    }
    if (action == "preprocess")
    {
        return doc && doc->loaded && !doc->sourceView && !doc->preprocessing;
    }
    if (action == "load_file" || action == "toggle_comment" || action == "complete")
    {
        return doc && doc->modifiable;
    }
    if (action == "fold")
    {
        return doc && doc->editor->canPerform(ALEditorCommand::Fold);
    }
    if (action == "unfold")
    {
        return doc && doc->editor->canPerform(ALEditorCommand::Unfold);
    }
    if (action == "fold_all")
    {
        return doc && doc->editor->canPerform(ALEditorCommand::FoldAll);
    }
    if (action == "unfold_all")
    {
        return doc && doc->editor->canPerform(ALEditorCommand::UnfoldAll);
    }
    if (action == "go_to_definition")
    {
        return doc && doc->editor->canPerform(ALEditorCommand::GoToDefinition);
    }
    if (action == "find_references")
    {
        return doc && doc->editor->canPerform(ALEditorCommand::FindReferences);
    }
    if (action == "rename")
    {
        return doc && doc->editor->canPerform(ALEditorCommand::Rename);
    }
    if (action == "go_to_line" || action == "find" || action == "replace" || action == "find_next" || action == "find_previous")
    {
        return doc != nullptr;
    }
    if (action == "go_to_symbol")
    {
        return doc && !doc->outline.empty();
    }
    if (action == "undo")
    {
        return doc && doc->editor->canUndo();
    }
    if (action == "redo")
    {
        return doc && doc->editor->canRedo();
    }
    return true;
}

bool ALFloaterScriptStudio::onMenuCheck(const LLSD& param)
{
    const std::string action = param.asString();
    if (action == "word_wrap")
    {
        return mWordWrap;
    }
    if (action == "line_numbers")
    {
        return mLineNumbers;
    }
    if (action == "scroll_bar")
    {
        return !mScrollMap;
    }
    if (action == "scroll_map")
    {
        return mScrollMap;
    }
    if (action == "map_narrow")
    {
        return mScrollMapWidth <= 60;
    }
    if (action == "map_medium")
    {
        return mScrollMapWidth > 60 && mScrollMapWidth < 130;
    }
    if (action == "map_wide")
    {
        return mScrollMapWidth >= 130;
    }
    if (action == "map_preview")
    {
        return mScrollMapPreview;
    }
    if (action == "map_left")
    {
        return mScrollMapLeft;
    }
    if (action == "problems" || action == "references" || action == "output")
    {
        const LLPanel* current = mBottomTabs->getCurrentPanel();
        const char*    tab     = action == "problems" ? "problems_tab" : action == "references" ? "references_tab" : "output_tab";
        return !mFolds.collapsed("bottom") && current && current->getName() == tab;
    }
    if (action == "inspector")
    {
        return !mFolds.collapsed("inspector");
    }
    if (action == "explorer")
    {
        return !mFolds.collapsed("explorer");
    }
    if (action == "preflight")
    {
        return gSavedSettings.getBOOL("ALScriptStudioPreflight");
    }
    if (action == "preproc_enabled")
    {
        return gSavedSettings.getBOOL("ALScriptPreprocEnabled");
    }
    if (action == "preproc_switch")
    {
        return gSavedSettings.getBOOL("ALScriptPreprocSwitch");
    }
    if (action == "preproc_lazy")
    {
        return gSavedSettings.getBOOL("ALScriptPreprocLazyLists");
    }
    if (action == "preproc_compress")
    {
        return gSavedSettings.getBOOL("ALScriptPreprocCompress");
    }
    if (action == "preproc_disk")
    {
        return gSavedSettings.getBOOL("ALScriptPreprocDiskIncludes");
    }
    if (action == "preproc_optimize")
    {
        return gSavedSettings.getBOOL("ALScriptPreprocOptimizer");
    }
    if (action == "preproc_shrink")
    {
        return gSavedSettings.getBOOL("ALScriptPreprocOptimizerShrinkNames");
    }
    if (action == "preproc_addstrings")
    {
        return gSavedSettings.getBOOL("ALScriptPreprocOptimizerAddStrings");
    }
    return false;
}

void ALFloaterScriptStudio::onCompileTarget()
{
    if (Doc* doc = active())
    {
        const std::string target = mCompileTarget->getValue().asString();
        doc->language.compileTarget = target;
        // What the script is stays what the item says; only where LSL is
        // compiled changes with the target.
        if (!doc->language.lua)
        {
            doc->editor->setSyntax("lsl");
        }
    }
}

void ALFloaterScriptStudio::onRunning()
{
    if (Doc* doc = active(); doc && !doc->ref.inInventory())
    {
        if (!ALScriptWorkspace::instance().setRunning(doc->ref, mRunning->get()))
        {
            mRunning->set(!mRunning->get());
        }
    }
}

void ALFloaterScriptStudio::onReset()
{
    if (Doc* doc = active(); doc && !doc->ref.inInventory())
    {
        ALScriptWorkspace::instance().reset(doc->ref);
    }
}

void ALFloaterScriptStudio::revert(Doc& doc)
{
    doc.loaded = false;
    doc.editor->setReadOnly(true);
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptWorkspace::instance().load(doc.ref, [handle](const ALScriptWorkspace::Loaded& answer) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->loaded(answer);
        }
    });
}

void ALFloaterScriptStudio::loadFromFile()
{
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChosenToLoad(files);
            }
        },
        LLFilePicker::FFLOAD_SCRIPT, false);
}

void ALFloaterScriptStudio::fileChosenToLoad(const std::vector<std::string>& files)
{
    Doc* doc = active();
    if (!doc || files.empty())
    {
        return;
    }
    const std::string text = readWholeFile(files.front());
    if (text.empty())
    {
        return;
    }
    doc->editor->selectAll();
    doc->editor->insertText(text);
}

void ALFloaterScriptStudio::saveToFile()
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChosenToSave(files);
            }
        },
        LLFilePicker::FFSAVE_SCRIPT, doc->name);
}

void ALFloaterScriptStudio::fileChosenToSave(const std::vector<std::string>& files)
{
    Doc* doc = active();
    if (!doc || files.empty())
    {
        return;
    }
    std::ofstream out(files.front(), std::ios::binary);
    out << doc->editor->text();
    LLStringUtil::format_map_t args;
    args["[PATH]"] = files.front();
    setStatus(getString(out.good() ? "SavedToFile" : "SaveToFileFailed", args), !out.good());
}

// --- state ---------------------------------------------------------------------

void ALFloaterScriptStudio::writeState(LLSD& state) const
{
    state["word_wrap"]    = mWordWrap;
    state["line_numbers"] = mLineNumbers;
    state["scroll_map"]   = mScrollMap;
    state["map_width"]    = mScrollMapWidth;
    state["map_preview"]  = mScrollMapPreview;
    state["map_left"]     = mScrollMapLeft;
}

void ALFloaterScriptStudio::readState(const LLSD& state)
{
    if (state.has("word_wrap"))
    {
        mWordWrap = state["word_wrap"].asBoolean();
    }
    if (state.has("line_numbers"))
    {
        mLineNumbers = state["line_numbers"].asBoolean();
    }
    if (state.has("scroll_map"))
    {
        mScrollMap = state["scroll_map"].asBoolean();
    }
    if (state.has("map_width"))
    {
        mScrollMapWidth = llmax(20, state["map_width"].asInteger());
    }
    if (state.has("map_preview"))
    {
        mScrollMapPreview = state["map_preview"].asBoolean();
    }
    if (state.has("map_left"))
    {
        mScrollMapLeft = state["map_left"].asBoolean();
    }
}
