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
#include "aloutputview.h"
#include "alscopebar.h"
#include "alscriptformatter.h"
#include "alscriptkeymap.h"
#include "altabstrip.h"
#include "altextsearch.h"
#include "alvimkeymap.h"
#include "llagent.h"
#include "llappviewer.h"
#include "llaudioengine.h"
#include "llavataractions.h"
#include "lldate.h"
#include "lltimer.h"
#include "llsyntaxid.h"
#include "llversioninfo.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "lldir.h"
#include "lldirpicker.h"
#include "lleditmenuhandler.h"
#include "llfocusmgr.h"
#include "llfilepicker.h"
#include "llfiltereditor.h"
#include "llfloaterreg.h"
#include "llfloatersidepanelcontainer.h"
#include "lllandmarkactions.h"
#include "lllandmarklist.h"
#include "llenvironment.h"
#include "llinventoryicon.h"
#include "llinventorymodel.h"
#include "lllayoutstack.h"
#include "llmaterialeditor.h"
#include "llpreviewtexture.h"
#include "lllineeditor.h"
#include "llmenugl.h"
#include "llnotificationsutil.h"
#include "llscrolllistctrl.h"
#include "llsdserialize.h"
#include "llselectmgr.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltooldraganddrop.h"
#include "lltrans.h"
#include "llexternaleditor.h"
#include "lllogchat.h"
#include "llscripteditorws.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewermenu.h"
#include "llweb.h"
#include "llviewermenufile.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"

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
    static LLCachedControl<bool> enabled(gSavedSettings, "ALScriptStudioEnabled", true);
    return enabled;
}

// static
ALFloaterScriptStudio* ALFloaterScriptStudio::open(const ALScriptRef& ref, const std::string& name, bool take_focus)
{
    // Open somewhere already: that window, brought forward -- if a
    // window may be shown at all, which a restriction on viewing
    // scripts decides the same way for every window.
    if (!ref.isNull())
    {
        for (LLFloater* floater : LLFloaterReg::getFloaterList("script_studio"))
        {
            ALFloaterScriptStudio* window = ALViewType::as<ALFloaterScriptStudio>(floater);
            if (window && window->indexOf(ref) != NONE)
            {
                if (!LLFloaterReg::canShowInstance("script_studio", window->getKey()))
                {
                    return nullptr;
                }
                window->openFloater(window->getKey());
                if (take_focus)
                {
                    window->setFocus(true);
                }
                window->openScript(ref, name);
                return window;
            }
        }
    }
    ALFloaterScriptStudio* studio = LLFloaterReg::showTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD(), take_focus ? TAKE_FOCUS_YES : TAKE_FOCUS_NO);
    if (studio && !ref.isNull())
    {
        studio->openScript(ref, name);
    }
    return studio;
}

// static
ALFloaterScriptStudio* ALFloaterScriptStudio::explore(const LLUUID& root)
{
    ALFloaterScriptStudio* studio = LLFloaterReg::showTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD(), TAKE_FOCUS_YES);
    if (studio && root.notNull())
    {
        studio->exploreObject(root);
    }
    return studio;
}

// static
void ALFloaterScriptStudio::savedElsewhere(const ALScriptRef& ref, const std::string& text)
{
    for (LLFloater* floater : LLFloaterReg::getFloaterList("script_studio"))
    {
        ALFloaterScriptStudio* window = ALViewType::as<ALFloaterScriptStudio>(floater);
        const size_t           index  = window ? window->indexOf(ref) : NONE;
        if (index == NONE)
        {
            continue;
        }
        Doc& doc = *window->mDocs[index];
        if (doc.notecard || !doc.loaded || doc.saving)
        {
            return;
        }
        if (doc.editor->isDirty() && doc.modifiable)
        {
            // What was typed here stays a step behind what was saved
            // there, and the tab stays unsaved.
            doc.carriedText = text;
            window->takeCarriedText(doc);
        }
        else
        {
            // As if loaded afresh: the envelope read, the expanded code
            // shown, the analyzers asked, and nothing to save.
            ALScriptWorkspace::Loaded answer;
            answer.ref        = ref;
            answer.assetId    = doc.assetId;
            answer.name       = doc.name;
            answer.text       = text;
            answer.language   = doc.language;
            answer.viewable   = true;
            answer.modifiable = doc.modifiable;
            window->loaded(answer);
        }
        return;
    }
}

// static
void ALFloaterScriptStudio::itemRemoved(const ALScriptRef& ref)
{
    for (LLFloater* floater : LLFloaterReg::getFloaterList("script_studio"))
    {
        ALFloaterScriptStudio* window = ALViewType::as<ALFloaterScriptStudio>(floater);
        const size_t           index  = window ? window->indexOf(ref) : NONE;
        if (index == NONE)
        {
            continue;
        }
        window->letGoOf(index);
        if (!window->mMain && window->mDocs.empty())
        {
            // A window popped out for it alone has nothing left to show.
            window->closeFloater();
        }
        return;
    }
}

ALFloaterScriptStudio::ALFloaterScriptStudio(const LLSD& key)
:   ALStudioFloater(key, key.asString().empty() ? std::string("ALScriptStudioState") : std::string()),
    mMain(key.asString().empty())
{
    // The main window is one and stays; a popped-out one is as many as
    // are wanted and goes when closed.
    setIsSingleInstance(mMain);
    mCommitCallbackRegistrar.add("ScriptStudio.Menu", boost::bind(&ALFloaterScriptStudio::onMenuAction, this, _2));
    mEnableCallbackRegistrar.add("ScriptStudio.Enable", boost::bind(&ALFloaterScriptStudio::onMenuEnable, this, _2));
    mEnableCallbackRegistrar.add("ScriptStudio.Check", boost::bind(&ALFloaterScriptStudio::onMenuCheck, this, _2));
}

ALFloaterScriptStudio::~ALFloaterScriptStudio() = default;

bool ALFloaterScriptStudio::matchesKey(const LLSD& key)
{
    // By key alone, main or not: the main answers to none, each other
    // window to its own.
    return LLFloater::KeyCompare::equate(key, mKey);
}

bool ALFloaterScriptStudio::postBuild()
{
    if (!mMain)
    {
        // No saved rect: a popped-out window is placed beside the one it
        // came from, and keeps nothing.
        mRectControl.clear();
        mSaveRect = false;
    }
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
    mSymbol        = getChild<ALTextView>("symbol");
    mSymbol->onLinkClicked([this](const ALTextView::Substitution& link) {
        // The declaration's line, in the script the inspector is about.
        Doc* doc = active();
        if (doc && link.value.has("line"))
        {
            doc->editor->goToLine(link.value["line"].asInteger());
            doc->editor->setFocus(true);
        }
    });
    mOutput        = getChild<ALOutputView>("output");
    mOutputFilter  = getChild<LLComboBox>("output_filter");
    mExplorer      = getChild<LLScrollListCtrl>("explorer");
    mSearchBar     = getChild<ALScopeBar>("search_bar");
    mSearchResults = getChild<LLScrollListCtrl>("search_results");
    mCompileTarget = getChild<LLComboBox>("compile_target");
    mRunning       = getChild<LLCheckBoxCtrl>("running");
    mResetButton   = getChild<LLButton>("reset_btn");
    mSaveButton    = getChild<LLButton>("save_btn");
    mSaveAllButton = getChild<LLButton>("save_all_btn");
    mUndoButton    = getChild<LLButton>("undo_btn");
    mRedoButton    = getChild<LLButton>("redo_btn");
    mFindButton    = getChild<LLButton>("find_btn");
    mFormatButton  = getChild<LLButton>("format_btn");
    mExpandedButton = getChild<LLButton>("expanded_btn");

    mTabs->onChosen(boost::bind(&ALFloaterScriptStudio::onTabChosen, this, _1));
    mTabs->onClosed(boost::bind(&ALFloaterScriptStudio::closeDocument, this, _1));
    mTabs->onMenu(boost::bind(&ALFloaterScriptStudio::showTabMenu, this, _1, _2, _3));
    mTabs->onReordered(boost::bind(&ALFloaterScriptStudio::onTabsReordered, this, _1));
    mBreadcrumb->onChose(boost::bind(&ALFloaterScriptStudio::onCrumbChosen, this, _1, _2));
    mProblems->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onProblemSelected, this));
    // The pane's filters: which levels, which source, which words.
    mProblemErrors   = getChild<LLCheckBoxCtrl>("problems_errors");
    mProblemWarnings = getChild<LLCheckBoxCtrl>("problems_warnings");
    mProblemNotes    = getChild<LLCheckBoxCtrl>("problems_notes");
    mProblemOrigin   = getChild<LLComboBox>("problems_origin");
    mProblemFilter   = getChild<LLFilterEditor>("problems_filter");
    mProblemOrigin->add(getString("OriginAny"), LLSD(""));
    for (const char* origin : { "OriginParser", "OriginTypes", "OriginLint", "OriginCompiler", "OriginPreprocessor", "OriginOptimizer", "OriginRuntime", "OriginDefinitions" })
    {
        mProblemOrigin->add(getString(origin), LLSD(getString(origin)));
    }
    mProblemOrigin->selectFirstItem();
    for (LLUICtrl* filter : { static_cast<LLUICtrl*>(mProblemErrors), static_cast<LLUICtrl*>(mProblemWarnings), static_cast<LLUICtrl*>(mProblemNotes),
                              static_cast<LLUICtrl*>(mProblemOrigin), static_cast<LLUICtrl*>(mProblemFilter) })
    {
        filter->setCommitCallback([this](LLUICtrl*, const LLSD&) { fillProblems(active()); });
    }
    mReferences->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onReferenceChosen, this));
    mOutline->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onOutlineChosen, this));
    mOutputFilter->add(getString("OutputAllObjects"), LLSD(LLUUID::null));
    mOutputFilter->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onOutputFilter, this));
    mOutput->onEntryChosen([this](const ALOutputView::Entry& entry) { onOutputChosen(entry); });
    getChild<LLButton>("output_clear")->setCommitCallback([this](LLUICtrl*, const LLSD&) { mOutput->clearEntries(); });
    // What was said before the window opened, then everything after.
    for (const ALScriptWorkspace::RuntimeEvent& event : ALScriptWorkspace::instance().recentRuntime())
    {
        runtimeEvent(event);
    }
    mRuntimeConnection = ALScriptWorkspace::instance().onRuntime([this](const ALScriptWorkspace::RuntimeEvent& event) { runtimeEvent(event); });

    buildSearchBar();
    // How many places, in the slot at the sentence's right end.
    LLTextBox::Params count(LLUICtrlFactory::getDefaultParams<LLTextBox>());
    count.name        = "search_count";
    count.rect        = LLRect(0, 22, 120, 0);
    count.font_halign = LLFontGL::RIGHT;
    count.tool_tip    = getString("SearchCountTip");
    mSearchCount      = LLUICtrlFactory::create<LLTextBox>(count);
    mSearchBar->setAdornment(mSearchCount);
    mSearchBar->onRun(boost::bind(&ALFloaterScriptStudio::search, this));
    mSearchBar->onChanged(boost::bind(&ALFloaterScriptStudio::onSearchChanged, this));
    mSearchResults->setDoubleClickCallback(boost::bind(&ALFloaterScriptStudio::onSearchResult, this));
    mExplorer->setDoubleClickCallback(boost::bind(&ALFloaterScriptStudio::onExplorerChosen, this));
    mExplorer->setRightMouseDownCallback([this](LLUICtrl*, S32 x, S32 y, MASK) { showExplorerMenu(x, y); });
    for (const char* action : { "open", "start", "stop", "reset", "refresh" })
    {
        getChild<LLButton>(std::string("explorer_") + action)->setCommitCallback([this, action](LLUICtrl*, const LLSD&) { onExplorerAction(action); });
    }
    mRunningConnection = ALScriptWorkspace::instance().onRunningState([this](const ALScriptWorkspace::RunningState& state) { runningState(state); });
    for (LLScrollListCtrl* list : { mProblems, mReferences, mSearchResults })
    {
        listMenuFor(list);
    }
    mCompileTarget->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onCompileTarget, this));
    mRunning->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onRunning, this));
    mResetButton->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onReset, this));
    mSaveButton->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (Doc* doc = active())
        {
            save(*doc);
        }
    });
    mSaveAllButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { saveAll(); });
    mUndoButton->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        undo();
        refreshToolbar();
    });
    mRedoButton->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        redo();
        refreshToolbar();
    });
    mFindButton->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (Doc* doc = active())
        {
            doc->editor->perform(ALEditorCommand::Find);
        }
    });
    mFormatButton->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (Doc* doc = active())
        {
            format(*doc, false);
        }
    });
    mExpandedButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { toggleExpanded(); });
    mCompiledConnection = ALScriptWorkspace::instance().onCompiled([this](const ALScriptWorkspace::CompileResult& result) { compiled(result); });
    // New definitions from the region: the analyzers reload, the words
    // are rebuilt, and every script is checked again.
    mDefinitionsConnection = LLSyntaxDefCache::instance().addSyntaxIDCallback([this]() {
        ALScriptAnalysis::instance().definitionsChanged();
        forgetVocabulary();
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
    // With the pins read, the objects in hand.
    refreshExplorer();
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
    pumpVim();
    ALStudioFloater::draw();
}

bool ALFloaterScriptStudio::handleKeyHere(KEY key, MASK mask)
{
    // Control-tab and control-shift-tab go round the tabs, as everywhere.
    if (key == KEY_TAB && (mask == MASK_CONTROL || mask == (MASK_CONTROL | MASK_SHIFT)))
    {
        cycleTab(mask & MASK_SHIFT ? -1 : 1);
        return true;
    }
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

ALQuickOpen* ALFloaterScriptStudio::quickOpen(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                                              std::function<void(const std::string&)> chose, LLView* anchor, S32 width, S32 height,
                                              std::function<void()> escaped, std::function<void(const std::string&)> hold)
{
    ALQuickOpen* quick = ALStudioFloater::quickOpen(std::move(candidates), placeholder, title, std::move(chose), anchor, width, height, std::move(escaped), std::move(hold));
    if (quick)
    {
        const LLUIColorTable& colors = LLUIColorTable::instance();
        quick->setColors(colors.getColor("ScriptBackground").get(), colors.getColor("ScriptText").get());
    }
    return quick;
}

bool ALFloaterScriptStudio::hasDoc(const Doc* doc) const
{
    for (const std::unique_ptr<Doc>& each : mDocs)
    {
        if (each.get() == doc)
        {
            return true;
        }
    }
    return false;
}

ALFloaterScriptStudio::Doc* ALFloaterScriptStudio::active()
{
    return mActive < mDocs.size() ? mDocs[mActive].get() : nullptr;
}

size_t ALFloaterScriptStudio::indexOf(const ALScriptRef& ref) const
{
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        if (mDocs[i]->ref == ref && mDocs[i]->file.empty())
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
    // The studio's own colours, which a theme sets as one: the legacy
    // script colours for the text and the ground, and every kind under
    // the Script prefix. By name, as a XUI file gives them, so that the
    // editor holds a handle to the table's colour and follows it; a
    // colour given as a value is copied into its components and stays
    // what it was.
    p.syntax_color_prefix        = "Script";
    p.text_color.control         = "ScriptText";
    p.text_readonly_color.control = "ScriptText";
    p.bg_color.control           = "ScriptBackground";
    p.bg_focus_color.control     = "ScriptBackground";
    p.bg_readonly_color.control  = "ScriptBgReadOnlyColor";
    p.cursor_color.control       = "ScriptCursorColor";
    p.selection_color.control    = "ScriptSelectionColor";
    p.find_match_color.control   = "ScriptFindMatchColor";
    p.bracket_match_color.control = "ScriptBracketMatchColor";
    p.gutter_color.control       = "ScriptGutterColor";
    p.line_number_color.control  = "ScriptLineNumberColor";
    p.current_line_color.control = "ScriptCurrentLineColor";
    p.fold_color.control         = "ScriptFoldColor";
    p.highlight_color.control    = "ScriptHighlightColor";
    p.changed_color.control      = "ScriptChangedColor";
    p.bracket_color_1.control    = "ScriptBracket1Color";
    p.bracket_color_2.control    = "ScriptBracket2Color";
    p.bracket_color_3.control    = "ScriptBracket3Color";
    ALCodeEditor* editor = LLUICtrlFactory::create<ALCodeEditor>(p);
    editor->setVisible(false);
    applyEditorOptions(*editor);
    mEditorHost->addChild(editor);
    return editor;
}

// static
const LLFontGL* ALFloaterScriptStudio::editorFont()
{
    static LLCachedControl<std::string> family(gSavedSettings, "ALScriptStudioFontFamily", "");
    static LLCachedControl<std::string> size(gSavedSettings, "ALScriptStudioFontSize", "");
    static LLCachedControl<std::string> style(gSavedSettings, "ALScriptStudioFontStyle", "");
    const std::string                   name = family().empty() ? std::string("Monospace") : family();
    const std::string                   how  = size().empty() ? std::string("Monospace") : size();
    const LLFontGL*                     font = LLFontGL::getFont(LLFontDescriptor(name, how, LLFontGL::getStyleFromString(style())));
    return font ? font : LLFontGL::getFontMonospace();
}

// static
void ALFloaterScriptStudio::refreshAll()
{
    for (LLFloater* floater : LLFloaterReg::getFloaterList("script_studio"))
    {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(floater))
        {
            studio->applyEditorOptions();
        }
    }
}

void ALFloaterScriptStudio::applyEditorOptions(ALCodeEditor& editor) const
{
    editor.setFont(editorFont());
    editor.keymap() = ALScriptKeymap::current();
    // Vim put over the editor, or taken away; one already there keeps
    // its marks and registers.
    if (mVimMode && !editor.modalKeymap())
    {
        auto                      vim    = std::make_unique<ALVimKeymap>();
        const LLHandle<LLFloater> handle = getHandle();
        vim->share(mVimShared);
        vim->hooks().command = [handle](ALTextView& view, const std::string& name, const std::string& args) {
            ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
            return studio && studio->vimCommand(view, name, args);
        };
        vim->hooks().format = [handle](ALTextView& view, S32 first, S32 last) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->vimFormat(view, first, last);
            }
        };
        vim->hooks().historyWindow = [handle](ALTextView& view, llwchar kind, const std::vector<std::string>& history, std::function<void(const std::string&, bool run)> chosen) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->vimHistoryWindow(view, kind, history, std::move(chosen));
            }
        };
        vim->hooks().complete = [handle](ALTextView& view, const std::string& command, std::vector<std::string>& out) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->vimComplete(view, command, out);
            }
        };
        editor.setModalKeymap(std::move(vim));
    }
    else if (!mVimMode && editor.modalKeymap())
    {
        editor.setModalKeymap(nullptr);
    }
    editor.setWordWrap(mWordWrap);
    editor.setShowLineNumbers(mLineNumbers);
    editor.setShowIndentGuides(mIndentGuides);
    editor.setRelativeLineNumbers(mRelativeNumbers);
    editor.setColorBrackets(mRainbowBrackets);
    editor.setStickyHeaders(mStickyHeaders);
    editor.setScrollMapWidth(mScrollMapWidth);
    editor.setScrollMapPreview(mScrollMapPreview);
    editor.setScrollMapOnLeft(mScrollMapLeft);
    editor.setScrollMap(mScrollMap);
    editor.setSpellCheck(mSpellCheck);
}

void ALFloaterScriptStudio::applyEditorOptions()
{
    for (std::unique_ptr<Doc>& each : mDocs)
    {
        applyEditorOptions(*each->editor);
        if (each->expandedEditor)
        {
            applyEditorOptions(*each->expandedEditor);
        }
    }
    saveState();
}

void ALFloaterScriptStudio::openScript(const ALScriptRef& ref, const std::string& name, std::optional<std::string> carried, S32 line)
{
    const size_t already = indexOf(ref);
    if (already != NONE)
    {
        activate(already);
        return;
    }

    auto doc         = std::make_unique<Doc>();
    doc->ref         = ref;
    doc->id          = ref.id();
    doc->name        = name.empty() ? getString("Untitled") : name;
    doc->carriedText = std::move(carried);
    doc->pendingLine = line;
    doc->editor = makeEditor(doc->id, true);
    doc->editor->setText(getString("Loading"));
    Doc* raw     = doc.get();
    doc->changed = doc->editor->onTextChanged([this, raw]() {
        fillTabs();
        refreshToolbar();
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

// Text brought from another window in place of the server's, as one
// step to undo: the server's text is what undo goes back to.
void ALFloaterScriptStudio::takeCarriedText(Doc& doc)
{
    if (!doc.carriedText)
    {
        return;
    }
    if (*doc.carriedText != doc.editor->text())
    {
        doc.editor->setReadOnly(false);
        doc.editor->setSelection(ALTextRange(doc.editor->document().start(), doc.editor->document().end()));
        doc.editor->insertText(*doc.carriedText);
    }
    doc.carriedText.reset();
}

void ALFloaterScriptStudio::placeEmbeddedItems(Doc& doc, S32 first_line, S32 last_line)
{
    const ALTextDocument& text = doc.editor->document();
    for (S32 line = llmax(0, first_line); line <= last_line && line < text.lineCount() && !doc.embedded.empty(); ++line)
    {
        const std::string& bytes = text.line(line);
        for (size_t i = 0; i + 3 < bytes.size(); ++i)
        {
            const unsigned char b0 = static_cast<unsigned char>(bytes[i]);
            const unsigned char b1 = static_cast<unsigned char>(bytes[i + 1]);
            const unsigned char b2 = static_cast<unsigned char>(bytes[i + 2]);
            const unsigned char b3 = static_cast<unsigned char>(bytes[i + 3]);
            if (b0 != 0xF4 || (b1 & 0xF0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
            {
                continue;
            }
            const U32       code  = ((b0 & 7u) << 18) | ((b1 & 0x3Fu) << 12) | ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu);
            const U32       index = code - static_cast<U32>(LLTextEditor::FIRST_EMBEDDED_CHAR);
            const ALTextPos at(line, static_cast<S32>(i));
            if (index < doc.embedded.size() && doc.embedded[index].notNull() && !doc.editor->atomAt(at))
            {
                doc.editor->addAtom(embeddedAtom(doc, at, index));
            }
            i += 3;
        }
    }
}

void ALFloaterScriptStudio::placeEmbeddedItems(Doc& doc)
{
    // The format stands each item in the text as a character past the
    // last the standard assigns -- the first item's the first of them --
    // which is four bytes starting F4 in UTF-8; each becomes an atom
    // over its four bytes, so the text keeps it and a save carries it.
    std::vector<ALTextView::Atom> atoms;
    const ALTextDocument&         text = doc.editor->document();
    for (S32 line = 0; line < text.lineCount() && !doc.embedded.empty(); ++line)
    {
        const std::string& bytes = text.line(line);
        for (size_t i = 0; i + 3 < bytes.size(); ++i)
        {
            const unsigned char b0 = static_cast<unsigned char>(bytes[i]);
            const unsigned char b1 = static_cast<unsigned char>(bytes[i + 1]);
            const unsigned char b2 = static_cast<unsigned char>(bytes[i + 2]);
            const unsigned char b3 = static_cast<unsigned char>(bytes[i + 3]);
            if (b0 != 0xF4 || (b1 & 0xF0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
            {
                continue;
            }
            const U32 code  = ((b0 & 7u) << 18) | ((b1 & 0x3Fu) << 12) | ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu);
            const U32 index = code - static_cast<U32>(LLTextEditor::FIRST_EMBEDDED_CHAR);
            if (index < doc.embedded.size() && doc.embedded[index].notNull())
            {
                atoms.push_back(embeddedAtom(doc, ALTextPos(line, static_cast<S32>(i)), index));
            }
            i += 3;
        }
    }
    doc.editor->setAtoms(std::move(atoms));
}

void ALFloaterScriptStudio::carriedForSave(Doc& doc, std::string& text, std::vector<LLPointer<LLInventoryItem>>& items)
{
    text = doc.editor->text();
    items.clear();
    if (doc.embedded.empty())
    {
        return;
    }
    // Each item's new number, given in the order the text first stands
    // them; an item the text no longer stands anywhere is left behind.
    std::map<U32, U32> renumbered;
    for (size_t i = 0; i + 3 < text.size(); ++i)
    {
        const unsigned char b0 = static_cast<unsigned char>(text[i]);
        const unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
        const unsigned char b2 = static_cast<unsigned char>(text[i + 2]);
        const unsigned char b3 = static_cast<unsigned char>(text[i + 3]);
        if (b0 != 0xF4 || (b1 & 0xF0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
        {
            continue;
        }
        const U32 code  = ((b0 & 7u) << 18) | ((b1 & 0x3Fu) << 12) | ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu);
        const U32 index = code - static_cast<U32>(LLTextEditor::FIRST_EMBEDDED_CHAR);
        if (index < doc.embedded.size() && doc.embedded[index].notNull())
        {
            const auto found = renumbered.find(index);
            U32        fresh;
            if (found == renumbered.end())
            {
                fresh = static_cast<U32>(items.size());
                items.push_back(doc.embedded[index]);
                renumbered[index] = fresh;
            }
            else
            {
                fresh = found->second;
            }
            const U32 c = static_cast<U32>(LLTextEditor::FIRST_EMBEDDED_CHAR) + fresh;
            text[i + 1]  = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            text[i + 2]  = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            text[i + 3]  = static_cast<char>(0x80 | (c & 0x3F));
        }
        i += 3;
    }
}

bool ALFloaterScriptStudio::dropOnNotecard(Doc& doc, S32 x, S32 y, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip)
{
    // As the legacy notecard has it: an item from the inventory, of a
    // kind a notecard may carry, that the next owner may have whole;
    // never one out of another notecard, since only what is in the
    // inventory can be verified.
    if (LLToolDragAndDrop::getInstance()->getSource() == LLToolDragAndDrop::SOURCE_NOTECARD)
    {
        return false;
    }
    if (!doc.loaded || !doc.modifiable || doc.editor->isReadOnly())
    {
        *accept = ACCEPT_NO;
        if (tooltip.empty())
        {
            tooltip = getString("NotecardReadOnlyDrop");
        }
        return true;
    }
    bool supported = false;
    switch (type)
    {
        case DAD_SETTINGS:
            supported = LLEnvironment::instance().isExtendedEnvironmentEnabled();
            if (!supported && tooltip.empty())
            {
                tooltip = LLTrans::getString("TooltipNotecardNotAllowedTypeDrop");
            }
            break;
        case DAD_CALLINGCARD:
        case DAD_TEXTURE:
        case DAD_SOUND:
        case DAD_LANDMARK:
        case DAD_SCRIPT:
        case DAD_CLOTHING:
        case DAD_OBJECT:
        case DAD_NOTECARD:
        case DAD_BODYPART:
        case DAD_ANIMATION:
        case DAD_GESTURE:
        case DAD_MESH:
        case DAD_MATERIAL:
            supported = true;
            break;
        default:
            break;
    }
    LLInventoryItem* item = static_cast<LLInventoryItem*>(cargo);
    if (!item || !supported)
    {
        *accept = ACCEPT_NO;
        return true;
    }
    if ((item->getPermissions().getMaskNextOwner() & PERM_ITEM_UNRESTRICTED) != PERM_ITEM_UNRESTRICTED)
    {
        *accept = ACCEPT_NO;
        if (tooltip.empty())
        {
            tooltip = LLTrans::getString("TooltipNotecardOwnerRestrictedDrop");
        }
        return true;
    }
    if (!item->getPermissions().allowCopyBy(gAgentID))
    {
        // Carrying an item is copying it: one this agent may not copy
        // cannot go in, whatever the next owner would get.
        *accept = ACCEPT_NO;
        if (tooltip.empty())
        {
            tooltip = getString("NotecardDropNoCopy");
        }
        return true;
    }
    *accept = ACCEPT_YES_COPY_MULTI;
    if (drop)
    {
        // The item after the ones carried, and its character in the text
        // where the drop landed, one step to undo; the button follows the
        // edit through the document's change.
        const size_t index = doc.embedded.size();
        if (index >= static_cast<size_t>(LLTextEditor::MAX_EMBEDDED_ITEMS))
        {
            *accept = ACCEPT_NO;
            return true;
        }
        doc.embedded.push_back(item);
        // Where the drop landed -- or, for the second and later of several
        // dropped together, which come one call each in the same frame,
        // right after the one before, so that they keep their order.
        ALTextPos at = doc.editor->posAtLocal(x, y, true);
        if (doc.dropFrame == gFrameCount && doc.dropEnd.line >= 0)
        {
            at = doc.dropEnd;
        }
        const std::string placeholder = utf8str_from_cp(static_cast<llwchar>(LLTextEditor::FIRST_EMBEDDED_CHAR + index));
        doc.editor->replaceAll({ { ALTextRange(at, at), placeholder } });
        doc.dropEnd   = ALTextPos(at.line, at.column + static_cast<S32>(placeholder.size()));
        doc.dropFrame = gFrameCount;
    }
    return true;
}

ALTextView::Atom ALFloaterScriptStudio::embeddedAtom(Doc& doc, const ALTextPos& at, size_t index)
{
    const LLPointer<LLInventoryItem> item = doc.embedded[index];
    const LLFontGL*                  font = LLFontGL::getFontSansSerifSmall();
    LLStringUtil::format_map_t       args;
    args["[NAME]"] = item->getName();
    // What a press does, by the kind: opens, plays, or takes a copy.
    const char* tip = "EmbeddedItemCopyTip";
    switch (item->getType())
    {
        case LLAssetType::AT_TEXTURE:
        case LLAssetType::AT_MATERIAL:
        case LLAssetType::AT_CALLINGCARD:
        case LLAssetType::AT_LANDMARK: tip = "EmbeddedItemOpenTip"; break;
        case LLAssetType::AT_SOUND: tip = "EmbeddedItemPlayTip"; break;
        default: break;
    }
    // A button with the item's icon and name, as wide as they are.
    LLButton::Params p;
    p.name                    = "embedded_item";
    p.label                   = item->getName();
    p.font                    = font;
    p.image_overlay           = LLUI::getUIImage(LLInventoryIcon::getIconName(item->getType(), item->getInventoryType(), item->getFlags()));
    p.image_overlay_alignment = "left";
    p.tool_tip                = getString(tip, args);
    const S32 width           = font->getWidth(item->getName()) + 16 + 12;
    p.rect                    = LLRect(0, 0, width, 0);
    LLButton*         button  = LLUICtrlFactory::create<LLButton>(p);
    Doc* raw = &doc;
    button->setClickedCallback([this, raw, item](LLUICtrl*, const LLSD&) { openEmbeddedItem(*raw, item); });
    ALTextView::Atom atom;
    atom.at      = at;
    atom.length  = 4;
    atom.width   = width;
    atom.view    = button;
    atom.tooltip = p.tool_tip();
    atom.value   = static_cast<S32>(index);
    return atom;
}

bool ALFloaterScriptStudio::copyEmbeddedItem(Doc& doc, LLPointer<LLInventoryItem> item, const LLUUID& folder, U32 callback_id)
{
    if (item.isNull())
    {
        return false;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = item->getName();
    if (!doc.inAsset.count(item->getUUID()))
    {
        // The server copies out of the asset it has, which a drop is
        // only in once saved.
        setStatus(getString("NotecardCopyUnsaved", args), true);
        return false;
    }
    // As copy_inventory_from_notecard does, with an ear for the answer:
    // the request under the agent's policy, to the object's region or
    // the agent's.
    LLViewerRegion* region = nullptr;
    if (doc.ref.object.notNull())
    {
        if (LLViewerObject* object = gObjectList.findObject(doc.ref.object))
        {
            region = object->getRegion();
        }
    }
    if (!region)
    {
        region = gAgent.getRegion();
    }
    if (!region)
    {
        setStatus(getString("NotecardCopyFailed", args), true);
        return false;
    }
    LLSD body;
    body["notecard-id"] = doc.ref.item;
    body["object-id"]   = doc.ref.object;
    body["item-id"]     = item->getUUID();
    body["folder-id"]   = folder;
    body["callback-id"] = static_cast<LLSD::Integer>(callback_id);
    const LLHandle<LLFloater> handle = getHandle();
    const bool                asked  = region->requestPostCapability("CopyInventoryFromNotecard", body, nullptr, [handle, args](const LLSD& results) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            LLStringUtil::format_map_t why = args;
            why["[ERROR]"]                 = results.has(LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_MESSAGE) ?
                                                 results[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_MESSAGE].asString() :
                                                 std::string();
            studio->setStatus(studio->getString("NotecardCopyRefused", why), true);
        }
    });
    if (!asked)
    {
        setStatus(getString("NotecardCopyFailed", args), true);
    }
    return asked;
}

void ALFloaterScriptStudio::openEmbeddedItem(Doc& doc, LLPointer<LLInventoryItem> item)
{
    if (item.isNull())
    {
        return;
    }
    const ALScriptRef ref = doc.ref;
    // As the legacy notecard does: a texture or a material opens in its
    // preview, with the notecard named so that a save from there can
    // reach it; a calling card opens the profile; a sound plays; the
    // rest, and the sound once played, are offered as a copy.
    switch (item->getType())
    {
        case LLAssetType::AT_TEXTURE:
        {
            LLPreviewTexture* preview = LLFloaterReg::showTypedInstance<LLPreviewTexture>("preview_texture", LLSD(item->getAssetUUID()), TAKE_FOCUS_YES);
            if (preview)
            {
                preview->setAuxItem(item);
                preview->setNotecardInfo(ref.item, ref.object);
                if (preview->hasString("Title"))
                {
                    LLStringUtil::format_map_t args;
                    args["[NAME]"] = item->getName();
                    preview->setTitle(preview->getString("Title", args));
                }
                preview->getChild<LLUICtrl>("desc")->setValue(item->getDescription());
            }
            return;
        }
        case LLAssetType::AT_MATERIAL:
        {
            LLSD key;
            key["objectid"]   = ref.object;
            key["notecardid"] = ref.item;
            if (LLMaterialEditor* preview = LLFloaterReg::getTypedInstance<LLMaterialEditor>("material_editor", key))
            {
                preview->setAuxItem(item);
                preview->setNotecardInfo(ref.item, ref.object);
                preview->openFloater(key);
                preview->setFocus(true);
            }
            return;
        }
        case LLAssetType::AT_LANDMARK:
        {
            // The place: the landmark already in the inventory for it, or
            // a copy taken into the landmarks folder and then shown.
            auto show = [](const LLUUID& landmark_id) {
                LLSD key;
                key["type"] = "landmark";
                key["id"]   = landmark_id;
                LLFloaterSidePanelContainer::showPanel("places", key);
            };
            Doc* raw    = &doc;
            auto placed = [this, raw, item, show](LLLandmark* landmark) {
                LLVector3d where;
                if (!landmark || !landmark->getGlobalPos(where))
                {
                    return;
                }
                if (LLViewerInventoryItem* mine = LLLandmarkActions::findLandmarkForGlobalPos(where))
                {
                    show(mine->getUUID());
                    return;
                }
                if (hasDoc(raw))
                {
                    copyEmbeddedItem(*raw, item, get_folder_by_itemtype(item), gInventoryCallbacks.registerCB(new LLBoostFuncInventoryCallback(show)));
                }
            };
            if (LLLandmark* landmark = gLandmarkList.getAsset(item->getAssetUUID(), placed))
            {
                placed(landmark);
            }
            return;
        }
        case LLAssetType::AT_CALLINGCARD:
            if (!item->getDescription().empty())
            {
                LLAvatarActions::showProfile(LLUUID(item->getDescription()));
            }
            else if (item->getCreatorUUID().notNull())
            {
                LLAvatarActions::showProfile(item->getCreatorUUID());
            }
            return;
        case LLAssetType::AT_SOUND:
            if (gAudiop)
            {
                gAudiop->triggerSound(item->getAssetUUID(), gAgentID, 1.f, LLAudioEngine::AUDIO_TYPE_UI, gAgent.getPositionGlobal());
            }
            break;
        case LLAssetType::AT_SETTINGS:
            if (!LLEnvironment::instance().isInventoryEnabled())
            {
                LLNotificationsUtil::add("NoEnvironmentSettings");
                return;
            }
            break;
        default:
            break;
    }
    // A drop not yet saved is said so before the question, since the
    // answer would be no.
    if (!doc.inAsset.count(item->getUUID()))
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = item->getName();
        setStatus(getString("NotecardCopyUnsaved", args), true);
        return;
    }
    Doc* raw = &doc;
    LLNotificationsUtil::add("ConfirmItemCopy", LLSD(), LLSD(), [this, raw, item](const LLSD& notification, const LLSD& response) {
        if (LLNotificationsUtil::getSelectedOption(notification, response) == 0 && item.notNull() && hasDoc(raw))
        {
            // The server finds the folder for it.
            copyEmbeddedItem(*raw, item, LLUUID::null);
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
    doc.notecard   = answer.notecard;
    if (!answer.error.empty())
    {
        doc.editor->setText(answer.error);
        doc.editor->setReadOnly(true);
        setStatus(answer.error, true);
    }
    else if (answer.notecard)
    {
        // Plain text, with whatever the notecard carried kept to go back
        // with it; nothing to analyse or compile.
        doc.loaded   = true;
        doc.embedded = answer.embedded;
        doc.inAsset.clear();
        for (const LLPointer<LLInventoryItem>& each : doc.embedded)
        {
            if (each.notNull())
            {
                doc.inAsset.insert(each->getUUID());
            }
        }
        doc.editor->setSyntax("text");
        doc.editor->setText(answer.text);
        takeCarriedText(doc);
        placeEmbeddedItems(doc);
        doc.editor->setReadOnly(!answer.modifiable);
        {
            Doc* raw = &doc;
            doc.editor->setDropHandler([this, raw](S32 x, S32 y, MASK, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip) {
                return dropOnNotecard(*raw, x, y, drop, type, cargo, accept, tooltip);
            });
            // A placeholder put in by an edit -- a drop, an undo, a redo,
            // a paste -- gets its button as the edit lands, on the lines
            // the edit touched; the view has slid its own atoms by then.
            doc.embeddedEdits = doc.editor->document().onChanged([this, raw](const ALTextDocument::Edit& edit) {
                if (edit.inserted.find('\xF4') != std::string::npos)
                {
                    placeEmbeddedItems(*raw, edit.range.begin.line, edit.endAfter().line);
                }
            });
        }
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString(answer.modifiable ? "Loaded" : "LoadedReadOnly", args));
        if (!doc.ref.inInventory())
        {
            refreshExplorer();
        }
        if (doc.pendingLine >= 0)
        {
            if (doc.pendingColumn >= 0)
            {
                doc.editor->goTo(ALTextRange(ALTextPos(doc.pendingLine, doc.pendingColumn), ALTextPos(doc.pendingLine, doc.pendingColumn + doc.pendingLength)));
            }
            else
            {
                doc.editor->goToLine(doc.pendingLine);
            }
            doc.pendingLine   = -1;
            doc.pendingColumn = -1;
            doc.pendingLength = 0;
        }
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
        takeCarriedText(doc);
        doc.editor->setReadOnly(!answer.modifiable);
        applyPendingEdits(doc);
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
            if (doc.pendingColumn >= 0)
            {
                doc.editor->goTo(ALTextRange(ALTextPos(doc.pendingLine, doc.pendingColumn), ALTextPos(doc.pendingLine, doc.pendingColumn + doc.pendingLength)));
            }
            else
            {
                doc.editor->goToLine(doc.pendingLine);
            }
            doc.pendingLine   = -1;
            doc.pendingColumn = -1;
            doc.pendingLength = 0;
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
    if (!doc.expandedEditor)
    {
        doc.expandedEditor = makeEditor(doc.id + ":expanded", true);
        doc.expandedEditor->setVisible(false);
    }
    doc.expandedEditor->setSyntax(doc.language.lua ? "slua" : "lsl");
    teachWords(*doc.expandedEditor, doc.language.lua);
    doc.expandedEditor->setText(text);
    if (&doc == active())
    {
        refreshToolbar();
    }
}

void ALFloaterScriptStudio::toggleExpanded()
{
    Doc* doc = active();
    if (!doc || !doc->expandedEditor)
    {
        return;
    }
    doc->showingExpanded = !doc->showingExpanded;
    showEditors();
    (doc->showingExpanded ? doc->expandedEditor : doc->editor)->setFocus(true);
    refreshToolbar();
}

void ALFloaterScriptStudio::showEditors()
{
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        Doc&       doc      = *mDocs[i];
        const bool here     = i == mActive;
        const bool expanded = here && doc.showingExpanded && doc.expandedEditor;
        doc.editor->setVisible(here && !expanded);
        if (doc.expandedEditor)
        {
            doc.expandedEditor->setVisible(expanded);
        }
    }
}

// --- the preprocessor ---------------------------------------------------------------

bool ALFloaterScriptStudio::preprocessed(const Doc& doc) const
{
    return doc.loaded && !doc.notecard && (doc.envelope.has_value() || ALScriptPreprocessor::enabled());
}

ALScriptPreprocessor::Request ALFloaterScriptStudio::preprocessRequest(const Doc& doc, bool with_source) const
{
    ALScriptPreprocessor::Request request;
    request.ref     = doc.ref;
    request.path    = doc.file.empty() ? std::string() : "disk:" + doc.file;
    request.name    = doc.name;
    request.assetId = doc.assetId;
    if (with_source)
    {
        request.source = doc.editor->text();
    }
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

void ALFloaterScriptStudio::openFile(const std::string& path, bool lua, S32 line, S32 column, S32 length)
{
    const std::string id      = "disk:" + path;
    size_t            already = indexOf(id);
    if (already == NONE)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            LLStringUtil::format_map_t args;
            args["[FILE]"] = path;
            setStatus(getString("IncludeGone", args), true);
            return;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();

        auto doc  = std::make_unique<Doc>();
        doc->file = path;
        doc->id   = id;
        doc->name = gDirUtilp->getBaseFileName(path);
        // The language its extension says; else the one it was asked for
        // from, where it was; else plain text.
        const FileLanguage language  = languageOfFile(path, lua);
        doc->language.lua            = language.lua;
        doc->language.compileTarget  = language.lua ? "luau" : "mono";
        doc->notecard                = !language.script;
        doc->editor                  = makeEditor(doc->id, false);
        doc->editor->setSyntax(!language.script ? "text" : language.lua ? "slua" : "lsl");
        doc->editor->setText(buffer.str());
        doc->loaded     = true;
        doc->modifiable = true;
        Doc* raw        = doc.get();
        doc->changed    = doc->editor->onTextChanged([this, raw]() {
            fillTabs();
            refreshToolbar();
            scheduleAnalysis(*raw);
        });
        mDocs.push_back(std::move(doc));
        already = mDocs.size() - 1;
        if (language.script)
        {
            teachEditor(*mDocs[already]);
        }
        watchFile(*mDocs[already]);
        noteRecentFile(path);
        LLStringUtil::format_map_t args;
        args["[NAME]"] = mDocs[already]->name;
        setStatus(getString("Loaded", args));
        scheduleAnalysis(*mDocs[already], true);
    }
    activate(already);
    Doc& doc = *mDocs[already];
    if (line >= 0)
    {
        if (column < 0)
        {
            doc.editor->goToLine(line);
        }
        else
        {
            doc.editor->goTo(ALTextRange(ALTextPos(line, column), ALTextPos(line, column + length)));
        }
    }
    doc.editor->setFocus(true);
    fillTabs();
    refreshToolbar();
}

// static
ALFloaterScriptStudio::FileLanguage ALFloaterScriptStudio::languageOfFile(const std::string& path, bool lua_hint)
{
    std::string extension = gDirUtilp->getExtension(path);
    LLStringUtil::toLower(extension);
    FileLanguage language;
    language.said   = !extension.empty();
    language.script = extension == "lsl" || extension == "lua" || extension == "luau" || lua_hint;
    language.lua    = extension == "lua" || extension == "luau" || (extension != "lsl" && lua_hint);
    return language;
}

void ALFloaterScriptStudio::speakFileLanguage(Doc& doc, const FileLanguage& language)
{
    if (doc.notecard == !language.script && doc.language.lua == language.lua)
    {
        return;
    }
    doc.language.lua           = language.lua;
    doc.language.compileTarget = language.lua ? "luau" : "mono";
    doc.notecard               = !language.script;
    doc.editor->setSyntax(!language.script ? "text" : language.lua ? "slua" : "lsl");
    if (language.script)
    {
        teachEditor(doc);
    }
    else
    {
        // Plain text again: no words, no one to ask.
        ALCodeEditor& editor = *doc.editor;
        editor.setCompletionProvider(nullptr);
        editor.setCompletionRequest(nullptr);
        editor.setHoverProvider(nullptr);
        editor.setHoverRequest(nullptr);
        editor.setSignatureRequest(nullptr);
        editor.setSymbolRequest(nullptr);
        ALSyntaxWords& tables = editor.highlighter().words();
        for (const char* table : { "function", "event", "type", "control", "constant", "deprecated" })
        {
            tables.set(table, {});
        }
        editor.highlighter().wordsChanged();
        editor.clearMarks();
    }
    doc.outline.clear();
    mProblemStore.forget(doc.id);
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

namespace
{
    // The words of each language, built once from the definitions and
    // kept until they change.
    std::vector<ALFloaterScriptStudio::Vocab> sVocabulary[2];
    bool                                      sVocabularyBuilt[2] = { false, false };
}

// static
void ALFloaterScriptStudio::forgetVocabulary()
{
    sVocabularyBuilt[0] = sVocabularyBuilt[1] = false;
}

// static
const std::vector<ALFloaterScriptStudio::Vocab>& ALFloaterScriptStudio::vocabulary(bool lua)
{
    std::vector<Vocab>& out = sVocabulary[lua ? 1 : 0];
    if (sVocabularyBuilt[lua ? 1 : 0])
    {
        return out;
    }
    sVocabularyBuilt[lua ? 1 : 0] = true;
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

// static
void ALFloaterScriptStudio::teachWords(ALCodeEditor& editor, bool lua)
{
    const std::vector<Vocab>& words = vocabulary(lua);
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
    if (!lua)
    {
        // The preprocessor's words, which the grid's keywords do not list,
        // while their transforms are on: Firestorm's switch and case, the
        // extensions' break, continue and inline.
        std::vector<const char*> extra;
        if (gSavedSettings.getBOOL("ALScriptPreprocSwitch"))
        {
            extra.insert(extra.end(), { "switch", "case" });
        }
        if (gSavedSettings.getBOOL("ALScriptPreprocExtensions"))
        {
            extra.insert(extra.end(), { "break", "continue", "inline" });
        }
        for (const char* word : extra)
        {
            if (std::find(controls.begin(), controls.end(), word) == controls.end())
            {
                controls.push_back(word);
            }
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
}

void ALFloaterScriptStudio::teachEditor(Doc& doc)
{
    ALCodeEditor& editor = *doc.editor;
    const bool    lua    = doc.language.lua;
    teachWords(editor, lua);

    // The analyzer answers what the vocabulary cannot: the script's own
    // symbols, the types of things, what a call takes.
    Doc* raw = &doc;
    editor.setCompletionRequest([this, raw](const ALTextPos& at, std::string_view) { askAnalyzer(*raw, ALScriptAnalysis::Kind::Complete, at); });
    editor.setHoverRequest([this, raw](const ALTextPos& at, std::string_view) { askAnalyzer(*raw, ALScriptAnalysis::Kind::Hover, at); });
    editor.setSignatureRequest([this, raw](const ALTextPos& caret) { askAnalyzer(*raw, ALScriptAnalysis::Kind::Signature, caret); });
    editor.setSymbolRequest([this, raw](ALEditorCommand command, const ALTextRange& word) { askSymbol(*raw, command, word); });
    editor.setHoverProvider([lua, raw](const ALTextPos& at, std::string_view word, std::string& text) {
        // The word as the vocabulary knows it: `Say` under the mouse in
        // `ll.Say` is asked about as `ll.Say`, and `pi` in `math.pi` as
        // `math.pi`. A local, a parameter or a member the definitions do
        // not name is the analyzer's to explain.
        std::string        name(word);
        const std::string& line = raw->editor->document().line(at.line);
        S32                from = at.column;
        while (from > 0 && line[from - 1] != '.' && (isalnum(static_cast<unsigned char>(line[from - 1])) || line[from - 1] == '_'))
        {
            --from;
        }
        while (from > 0 && line[from - 1] == '.')
        {
            S32 head = from - 1;
            while (head > 0 && (isalnum(static_cast<unsigned char>(line[head - 1])) || line[head - 1] == '_'))
            {
                --head;
            }
            if (head == from - 1)
            {
                break;
            }
            name = line.substr(head, from - 1 - head) + "." + name;
            from = head;
        }
        const Vocab* known = vocabWord(lua, name);
        if (!known)
        {
            known = vocabWord(lua, word);
        }
        if (!known)
        {
            return false;
        }
        text = known->detail.empty() ? known->text : known->detail;
        if (!known->tooltip.empty())
        {
            text += "\n" + known->tooltip;
        }
        if (known->deprecated)
        {
            text += "\n(deprecated)";
        }
        return true;
    });
    editor.setCompletionProvider([this, lua](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
        auto begins = [&prefix](const std::string& word) {
            if (word.size() < prefix.size())
            {
                return false;
            }
            for (size_t i = 0; i < prefix.size(); ++i)
            {
                if (LLStringOps::toLower(word[i]) != LLStringOps::toLower(prefix[i]))
                {
                    return false;
                }
            }
            return true;
        };
        for (const Vocab& word : vocabulary(lua))
        {
            if (begins(word.text))
            {
                out.push_back(completionFor(word, lua));
            }
        }
        // A snippet by its prefix, where a bare word is being typed
        // rather than a member.
        if (prefix.find('.') == std::string_view::npos)
        {
            for (const Snippet& snippet : snippets(lua))
            {
                if (begins(snippet.prefix))
                {
                    ALCodeEditor::Completion c;
                    c.text    = snippet.prefix;
                    c.detail  = getString("SnippetDetail") + "  " + snippet.name;
                    c.kind    = ALSyntaxKind::Control;
                    c.snippet = snippet.body;
                    out.push_back(std::move(c));
                }
            }
        }
    });
}

ALCodeEditor::Completion ALFloaterScriptStudio::completionFor(const Vocab& word, bool lua) const
{
    ALCodeEditor::Completion c;
    c.text   = word.text;
    c.detail = word.detail;
    c.kind   = word.deprecated ? ALSyntaxKind::Deprecated : word.kind;
    if (word.kind == ALSyntaxKind::Event)
    {
        // A handler to fill in: LSL's with its typed parameters as the
        // detail reads them, SLua's as a function set on LLEvents.
        if (lua)
        {
            std::string params;
            for (const std::string& name : ALCodeEditor::parameterNames(word.detail, word.text))
            {
                params += (params.empty() ? "" : ", ") + name;
            }
            c.snippet = "LLEvents." + word.text + " = function(" + params + ")\n    $0\nend";
        }
        else
        {
            c.snippet = word.detail + "\n{\n    $0\n}";
        }
    }
    return c;
}

const std::vector<ALFloaterScriptStudio::Snippet>& ALFloaterScriptStudio::snippets(bool lua)
{
    std::vector<Snippet>& out = mSnippets[lua ? 1 : 0];
    if (mSnippetsLoaded[lua ? 1 : 0])
    {
        return out;
    }
    mSnippetsLoaded[lua ? 1 : 0] = true;
    const std::string file = std::string("snippets") + gDirUtilp->getDirDelimiter() + (lua ? "slua.xml" : "lsl.xml");
    for (const std::string& path : { gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, file), gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, file) })
    {
        llifstream in(path.c_str());
        if (!in.is_open())
        {
            continue;
        }
        LLSD list;
        if (LLSDSerialize::fromXML(list, in) == LLSDParser::PARSE_FAILURE || !list.isArray())
        {
            LL_WARNS("ScriptStudio") << "The snippets at " << path << " could not be read" << LL_ENDL;
            continue;
        }
        for (LLSD::array_const_iterator it = list.beginArray(); it != list.endArray(); ++it)
        {
            Snippet one;
            one.name   = (*it)["name"].asString();
            one.prefix = (*it)["prefix"].asString();
            one.detail = (*it)["detail"].asString();
            one.body   = (*it)["body"].asString();
            if (!one.name.empty() && !one.body.empty())
            {
                out.push_back(std::move(one));
            }
        }
    }
    return out;
}

void ALFloaterScriptStudio::insertFromLibrary(const std::string& what)
{
    Doc* doc = active();
    if (!doc || !doc->loaded || !doc->modifiable || doc->notecard)
    {
        return;
    }
    const bool                          lua = doc->language.lua;
    std::vector<ALQuickOpen::Candidate> candidates;
    auto                                firstLine = [](const std::string& text) {
        const size_t end = text.find('\n');
        return end == std::string::npos ? text : text.substr(0, end);
    };
    if (what == "snippet")
    {
        const std::vector<Snippet>& list = snippets(lua);
        for (size_t i = 0; i < list.size(); ++i)
        {
            ALQuickOpen::Candidate one;
            one.label  = list[i].name;
            one.detail = list[i].detail;
            one.also   = list[i].prefix;
            one.value  = std::to_string(i);
            candidates.push_back(std::move(one));
        }
    }
    else
    {
        const ALSyntaxKind         kind  = what == "function" ? ALSyntaxKind::Function : what == "event" ? ALSyntaxKind::Event : ALSyntaxKind::Constant;
        const std::vector<Vocab>&  words = vocabulary(lua);
        for (size_t i = 0; i < words.size(); ++i)
        {
            if (words[i].kind != kind)
            {
                continue;
            }
            ALQuickOpen::Candidate one;
            one.label  = words[i].text;
            one.detail = words[i].deprecated ? getString("Deprecated") : firstLine(words[i].tooltip);
            one.also   = words[i].detail;
            one.value  = std::to_string(i);
            candidates.push_back(std::move(one));
        }
    }
    if (candidates.empty())
    {
        return;
    }
    const std::string         placeholder = getString(what == "snippet" ? "InsertSnippetPlaceholder" : what == "function" ? "InsertFunctionPlaceholder" : what == "event" ? "InsertEventPlaceholder" : "InsertConstantPlaceholder");
    const LLHandle<LLFloater> handle      = getHandle();
    quickOpen(std::move(candidates), placeholder, getString("InsertTitle"), [handle, what, lua](const std::string& value) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        Doc*                   doc    = studio ? studio->active() : nullptr;
        if (!doc || !doc->loaded || !doc->modifiable)
        {
            return;
        }
        const size_t             index = static_cast<size_t>(atoi(value.c_str()));
        ALCodeEditor::Completion chosen;
        if (what == "snippet")
        {
            const std::vector<Snippet>& list = studio->snippets(lua);
            if (index >= list.size())
            {
                return;
            }
            chosen.text    = list[index].prefix;
            chosen.snippet = list[index].body;
        }
        else
        {
            const std::vector<Vocab>& words = studio->vocabulary(lua);
            if (index >= words.size())
            {
                return;
            }
            chosen = studio->completionFor(words[index], lua);
        }
        // In place of the selection, or at the caret.
        const ALTextRange selection = doc->editor->selection();
        doc->editor->complete(chosen, ALTextRange(std::min(selection.begin, selection.end), std::max(selection.begin, selection.end)));
    }, mEditorHost);
}

void ALFloaterScriptStudio::askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at)
{
    if (!doc.loaded || doc.notecard)
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
    request.semantics      = mSemanticColors;
    request.hintParameters = mInlayParameters;
    request.hintTypes      = mInlayTypes;
    if (doc.language.lua)
    {
        // What the script's `.luaurc` says, where it has one; one not in
        // hand yet is fetched, and the check made again when it is.
        const ALScriptPreprocessor::Request root  = preprocessRequest(doc, /*with_source*/ false);
        const bool                          found = ALScriptPreprocessor::instance().configOf(root, request.config);
        if (!found && kind == ALScriptAnalysis::Kind::Check && !doc.configAsked)
        {
            doc.configAsked                  = true;
            const LLHandle<LLFloater> handle = getHandle();
            const std::string         id     = doc.id;
            ALScriptPreprocessor::instance().fetchConfig(root, [handle, id]() {
                ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
                const size_t           index  = studio ? studio->indexOf(id) : NONE;
                if (index != NONE)
                {
                    studio->scheduleAnalysis(*studio->mDocs[index], true);
                }
            });
        }
    }
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
            if (!result.hover.expected.empty())
            {
                LLStringUtil::format_map_t args;
                args["[TYPE]"] = result.hover.expected;
                text += "\n" + getString("HoverExpected", args);
            }
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
    mActive = index;
    showEditors();
    (mDocs[index]->showingExpanded && mDocs[index]->expandedEditor ? mDocs[index]->expandedEditor : mDocs[index]->editor)->setFocus(true);
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
        tab.image   = LLUI::getUIImage(imageNameOf(doc));
        // A dot in the worst problem's colour, for a script with any.
        S32 errors = 0, warnings = 0;
        problemCounts(doc, errors, warnings);
        if (errors > 0 || warnings > 0)
        {
            static const LLUIColor error_color   = LLUIColorTable::instance().getColor("CodeMarkError", LLColor4::red);
            static const LLUIColor warning_color = LLUIColorTable::instance().getColor("CodeMarkWarning", LLColor4::yellow);
            tab.badge                            = errors > 0 ? error_color.get() : warning_color.get();
        }
        tab.toolTip = !doc.file.empty() ? doc.file : doc.notecard ? getString("TabNotecardTip") : doc.ref.inInventory() ? getString("TabInventoryTip") : getString("TabObjectTip");
        tabs.push_back(std::move(tab));
        if (i == mActive)
        {
            chosen = doc.id;
        }
    }
    mTabs->setTabs(std::move(tabs), chosen);
    if (!mMain)
    {
        const Doc* doc = active();
        setTitle(doc ? getString("WindowTitle") + " - " + doc->name : getString("WindowTitle"));
    }
}

void ALFloaterScriptStudio::problemCounts(const Doc& doc, S32& errors, S32& warnings) const
{
    errors = warnings = 0;
    for (const Doc::Shown& shown : doc.shown)
    {
        if (shown.level == "ERROR")
        {
            ++errors;
        }
        else if (shown.level == "WARNING" || shown.level == "WARN")
        {
            ++warnings;
        }
    }
}

void ALFloaterScriptStudio::refreshTrailer(Doc& doc)
{
    const ALTextPos             caret = doc.editor->caret();
    LLStringUtil::format_map_t args;
    args["[LINE]"]  = std::to_string(caret.line + 1);
    args["[COL]"]   = std::to_string(caret.column + 1);
    std::vector<std::string> parts;
    if (!mVimBanner.empty())
    {
        parts.push_back(mVimBanner);
    }
    parts.push_back(getString("CaretPosition", args));
    // What is selected: lines across lines, characters within one.
    const ALTextRange selection = doc.editor->selection().normalised();
    if (!selection.empty())
    {
        if (selection.begin.line != selection.end.line)
        {
            const S32 lines = selection.end.line - selection.begin.line + (selection.end.column > 0 ? 1 : 0);
            args["[COUNT]"] = std::to_string(lines);
            parts.push_back(getString(lines == 1 ? "SelectedLine" : "SelectedLines", args));
        }
        else
        {
            const S32 chars = selection.end.column - selection.begin.column;
            args["[COUNT]"] = std::to_string(chars);
            parts.push_back(getString(chars == 1 ? "SelectedChar" : "SelectedChars", args));
        }
    }
    S32 errors = 0, warnings = 0;
    problemCounts(doc, errors, warnings);
    if (errors > 0)
    {
        args["[COUNT]"] = std::to_string(errors);
        parts.push_back(getString(errors == 1 ? "ProblemError" : "ProblemErrors", args));
    }
    if (warnings > 0)
    {
        args["[COUNT]"] = std::to_string(warnings);
        parts.push_back(getString(warnings == 1 ? "ProblemWarning" : "ProblemWarnings", args));
    }
    // Joined by a middle dot with air around it; in code, since a
    // string of the skin's is trimmed of its spaces.
    std::string said;
    for (const std::string& part : parts)
    {
        said += (said.empty() ? "" : "   \xC2\xB7   ") + part;
    }
    mBreadcrumb->setTrailer(said);
}

// --- vim ----------------------------------------------------------------------------------

ALFloaterScriptStudio::Doc* ALFloaterScriptStudio::docOf(const ALTextView& view)
{
    for (std::unique_ptr<Doc>& doc : mDocs)
    {
        if (doc->editor == &view || doc->expandedEditor == &view)
        {
            return doc.get();
        }
    }
    return nullptr;
}

void ALFloaterScriptStudio::pumpVim()
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    // The mode, the : line as it is typed and what the mode says are
    // all in the band the editor draws under its text, where vim has
    // them; the bottom strip says only that vim is on, so that a reader
    // of the strip knows why the keys do what they do.
    ALVimKeymap* vim    = doc->editor ? dynamic_cast<ALVimKeymap*>(doc->editor->modalKeymap()) : nullptr;
    std::string  banner = vim ? getString("VimNormal") : std::string();
    if (banner != mVimBanner)
    {
        mVimBanner = banner;
        refreshTrailer(*doc);
    }
}

void ALFloaterScriptStudio::vimHistoryWindow(ALTextView& view, llwchar kind, const std::vector<std::string>& history, std::function<void(const std::string&, bool run)> chosen)
{
    // Vim's command-line window as a quick-open over the editor: the
    // lines entered, the last first, ranked as they are typed at; the
    // one picked runs, as the window runs the row Enter is pressed on,
    // or with Shift goes back onto the line to be edited and entered.
    // The editor takes the keyboard back either way.
    std::vector<ALQuickOpen::Candidate> candidates;
    for (size_t i = history.size(); i-- > 0;)
    {
        ALQuickOpen::Candidate c;
        c.label = history[i];
        c.value = history[i];
        candidates.push_back(std::move(c));
    }
    const LLHandle<LLUICtrl> editor = view.getHandle();
    auto                     back   = [editor]() {
        if (LLUICtrl* e = editor.get())
        {
            e->setFocus(true);
        }
    };
    LLStringUtil::format_map_t args;
    args["[KIND]"] = utf8str_from_cp(kind);
    quickOpen(std::move(candidates), getString("VimHistoryPlaceholder", args), getString("VimHistoryTitle", args),
              [chosen, back](const std::string& line) {
                  back();
                  chosen(line, true);
              },
              mEditorHost, 420, ALQuickOpen::heightForRows(llclamp(static_cast<S32>(history.size()), 1, 8)), back,
              [chosen, back](const std::string& line) {
                  back();
                  chosen(line, false);
              });
}

bool ALFloaterScriptStudio::vimCommand(ALTextView& view, const std::string& name, const std::string& args)
{
    Doc* doc = docOf(view);
    if (!doc)
    {
        return false;
    }
    if (name == "w" || name == "write" || name == "w!")
    {
        save(*doc);
        return true;
    }
    if (name == "q" || name == "quit" || name == "close")
    {
        closeDocument(doc->id);
        return true;
    }
    if (name == "q!" || name == "quit!")
    {
        const size_t index = indexOf(doc->id);
        if (index != NONE)
        {
            letGoOf(index);
        }
        return true;
    }
    if (name == "wq" || name == "x" || name == "xit" || name == "wq!" || name == "x!")
    {
        if (doc->editor->isDirty() && doc->modifiable)
        {
            doc->closeAfterSave = true;
            save(*doc);
        }
        else
        {
            closeDocument(doc->id);
        }
        return true;
    }
    if (name == "history" || name == "his")
    {
        // The lines entered, in the Output pane, where a list fits: the :
        // ones, the search ones with / or search, both with all.
        const bool        searches = args == "/" || args == "search" || args == "all";
        const bool        commands = args.empty() || args == ":" || args == "cmd" || args == "all";
        ALOutputView::Entry entry;
        entry.source = getString("OutputSourceVim");
        // A listing: its numbered rows a block at the left edge.
        entry.hang   = ALOutputView::Hang::None;
        auto list = [&](const std::vector<std::string>& lines, const char* kind) {
            entry.text = std::string(kind) + " history:";
            for (size_t i = 0; i < lines.size(); ++i)
            {
                entry.text += llformat("\n%3d  %s", static_cast<int>(i + 1), lines[i].c_str());
            }
            mOutput->append(entry);
        };
        if (commands)
        {
            list(mVimShared->command, "cmd");
        }
        if (searches)
        {
            list(mVimShared->search, "search");
        }
        showBottom("output_tab");
        return true;
    }
    if (name == "wa" || name == "wall")
    {
        saveAll();
        return true;
    }
    if (name == "qa" || name == "qall" || name == "qa!" || name == "qall!")
    {
        std::vector<std::string> ids;
        for (const std::unique_ptr<Doc>& each : mDocs)
        {
            ids.push_back(each->id);
        }
        for (const std::string& id : ids)
        {
            if (name.back() == '!')
            {
                if (const size_t index = indexOf(id); index != NONE)
                {
                    letGoOf(index);
                }
            }
            else
            {
                closeDocument(id);
            }
        }
        return true;
    }
    if (name == "set")
    {
        std::string option = args;
        const bool  off    = option.compare(0, 2, "no") == 0;
        if (off)
        {
            option.erase(0, 2);
        }
        if (option == "number" || option == "nu")
        {
            if (mLineNumbers == off)
            {
                onMenuAction(LLSD("line_numbers"));
            }
            return true;
        }
        if (option == "relativenumber" || option == "rnu")
        {
            if (mRelativeNumbers == off)
            {
                onMenuAction(LLSD("relative_numbers"));
            }
            return true;
        }
        return false;
    }
    // The studio's own, by the names its menu knows.
    static const std::set<std::string> ours{ "format", "problems", "references", "output", "search", "preferences", "pop_out", "reveal", "save_all",
                                             "revert", "external_editor", "save_file", "save_as", "load_file", "open_file", "fold_all", "unfold_all", "go_to_line" };
    if (ours.count(name))
    {
        onMenuAction(LLSD(name));
        return true;
    }
    return false;
}

void ALFloaterScriptStudio::vimComplete(ALTextView& view, const std::string& command, std::vector<std::string>& out)
{
    // The names vimCommand answers to, in their long forms, and the
    // menu's actions; what :set and :history take after them.
    static const char* NAMES[] = { "close", "history", "qall", "quit", "wall", "wq", "write", "xit",
                                   "format", "problems", "references", "output", "search", "preferences", "pop_out", "reveal", "save_all",
                                   "revert", "external_editor", "save_file", "save_as", "load_file", "open_file", "fold_all", "unfold_all", "go_to_line" };
    static const char* OPTIONS[] = { "number", "nonumber", "relativenumber", "norelativenumber" };
    static const char* KINDS[]   = { "all", "cmd", "search" };
    if (command.empty())
    {
        out.insert(out.end(), std::begin(NAMES), std::end(NAMES));
    }
    else if (command == "set" || command == "se")
    {
        out.insert(out.end(), std::begin(OPTIONS), std::end(OPTIONS));
    }
    else if (command == "history" || command == "his")
    {
        out.insert(out.end(), std::begin(KINDS), std::end(KINDS));
    }
}

void ALFloaterScriptStudio::vimFormat(ALTextView& view, S32 first, S32 last)
{
    Doc* doc = docOf(view);
    if (!doc || !doc->loaded || !doc->modifiable || doc->notecard)
    {
        return;
    }
    const ALTextDocument& text = doc->editor->document();
    doc->editor->setSelection(ALTextRange(text.lineStart(first), text.lineEnd(llmin(last, text.lineCount() - 1))));
    format(*doc, true);
}

void ALFloaterScriptStudio::showTabMenu(const std::string& value, S32 x, S32 y)
{
    if (!LLMenuGL::sMenuContainer || indexOf(value) == NONE)
    {
        return;
    }
    if (LLContextMenu* old = mTabMenuHandle.get())
    {
        old->die();
        mTabMenuHandle.markDead();
    }
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
    commit.add("Tab.Action", [this](LLUICtrl*, const LLSD& param) { onTabAction(param.asString()); });
    enable.add("Tab.Enable", [this](LLUICtrl*, const LLSD& param) {
        const std::string action = param.asString();
        if (action == "close_others")
        {
            return mDocs.size() > 1;
        }
        if (action == "close_saved")
        {
            for (const std::unique_ptr<Doc>& doc : mDocs)
            {
                if (!doc->editor->isDirty())
                {
                    return true;
                }
            }
            return false;
        }
        if (action == "reveal")
        {
            const Doc* doc = active();
            return doc && !doc->ref.inInventory();
        }
        return active() != nullptr;
    });
    LLContextMenu* menu = LLUICtrlFactory::createFromFile<LLContextMenu>("menu_script_studio_tab.xml", LLMenuGL::sMenuContainer,
                                                                          LLMenuHolderGL::child_registry_t::instance());
    if (!menu)
    {
        return;
    }
    mTabMenuHandle = menu->getHandle();
    menu->show(x, y);
    LLMenuGL::showPopup(mTabs, menu, x, y);
}

void ALFloaterScriptStudio::onTabAction(const std::string& action)
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    if (action == "close")
    {
        closeDocument(doc->id);
    }
    else if (action == "close_others" || action == "close_all" || action == "close_saved")
    {
        // Each asked about in turn where it has unsaved changes; the ids
        // gathered first, since closing moves the rest.
        std::vector<std::string> ids;
        for (const std::unique_ptr<Doc>& each : mDocs)
        {
            const bool other = each.get() != doc;
            if ((action == "close_others" && other) || action == "close_all" || (action == "close_saved" && !each->editor->isDirty()))
            {
                ids.push_back(each->id);
            }
        }
        for (const std::string& id : ids)
        {
            closeDocument(id);
        }
    }
    else if (action == "pop_out")
    {
        popOut();
    }
    else if (action == "next_tab" || action == "previous_tab")
    {
        cycleTab(action == "next_tab" ? 1 : -1);
    }
    else if (action == "indent_guides")
    {
        mIndentGuides = !mIndentGuides;
        applyEditorOptions();
    }
    else if (action == "relative_numbers")
    {
        mRelativeNumbers = !mRelativeNumbers;
        applyEditorOptions();
    }
    else if (action == "rainbow_brackets")
    {
        mRainbowBrackets = !mRainbowBrackets;
        applyEditorOptions();
    }
    else if (action == "sticky_headers")
    {
        mStickyHeaders = !mStickyHeaders;
        applyEditorOptions();
    }
    else if (action == "spell_check")
    {
        mSpellCheck = !mSpellCheck;
        applyEditorOptions();
    }
    else if (action == "reveal")
    {
        // Its row in the explorer, chosen and in view.
        mFolds.setCollapsed("explorer", false);
        for (LLScrollListItem* item : mExplorer->getAllData())
        {
            const LLSD& value = item->getValue();
            if (value.isMap() && value["item"].asUUID() == doc->ref.item && value["prim"].asUUID() == doc->ref.object)
            {
                mExplorer->deselectAllItems();
                item->setSelected(true);
                mExplorer->scrollToShowSelected();
                break;
            }
        }
    }
    else if (action == "copy_name")
    {
        LLClipboard::instance().copyToClipboard(doc->name, 0, static_cast<S32>(doc->name.size()));
    }
}

void ALFloaterScriptStudio::onTabsReordered(const std::vector<std::string>& order)
{
    const std::string active_id = mActive != NONE ? mDocs[mActive]->id : std::string();
    std::vector<std::unique_ptr<Doc>> reordered;
    for (const std::string& id : order)
    {
        for (std::unique_ptr<Doc>& doc : mDocs)
        {
            if (doc && doc->id == id)
            {
                reordered.push_back(std::move(doc));
                break;
            }
        }
    }
    // Anything the strip did not name keeps its place at the end.
    for (std::unique_ptr<Doc>& doc : mDocs)
    {
        if (doc)
        {
            reordered.push_back(std::move(doc));
        }
    }
    mDocs = std::move(reordered);
    mActive = active_id.empty() ? NONE : indexOf(active_id);
    fillTabs();
}

void ALFloaterScriptStudio::cycleTab(S32 direction)
{
    if (mDocs.size() < 2 || mActive == NONE)
    {
        return;
    }
    const size_t count = mDocs.size();
    activate((mActive + count + static_cast<size_t>(direction > 0 ? 1 : count - 1)) % count);
}

void ALFloaterScriptStudio::refreshToolbar()
{
    Doc*       doc     = active();
    const bool have    = doc && doc->loaded;
    const bool task    = doc && !doc->ref.inInventory() && !doc->notecard;
    mCompileTarget->setEnabled(have && doc->modifiable && !doc->notecard && doc->file.empty());
    mSaveButton->setEnabled(have && doc->modifiable && !doc->saving);
    bool anyDirty = false;
    for (const std::unique_ptr<Doc>& each : mDocs)
    {
        anyDirty = anyDirty || (each->editor->isDirty() && each->modifiable);
    }
    mSaveAllButton->setEnabled(anyDirty);
    mUndoButton->setEnabled(doc && doc->editor->canUndo());
    mRedoButton->setEnabled(doc && doc->editor->canRedo());
    mFindButton->setEnabled(doc != nullptr);
    mFormatButton->setEnabled(have && doc->modifiable && !doc->notecard);
    mExpandedButton->setEnabled(doc && doc->expandedEditor != nullptr);
    mExpandedButton->setToggleState(doc && doc->showingExpanded && doc->expandedEditor);
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
    if (!doc.file.empty())
    {
        saveFile(doc);
        return;
    }
    if (doc.notecard)
    {
        // What goes back: the text with the items it still stands
        // somewhere, numbered afresh, and those items alone, as the
        // legacy notecard prunes what an edit took out. The editor keeps
        // its own numbering and its whole list: a placeholder undone
        // back into the text still names its item, and the next save
        // numbers afresh from whatever the text then stands.
        std::string                             text;
        std::vector<LLPointer<LLInventoryItem>> items;
        carriedForSave(doc, text, items);
        std::string error;
        if (!ALScriptWorkspace::instance().saveNotecard(doc.ref, text, items, nullptr, error))
        {
            setStatus(error, true);
            return;
        }
        doc.saving = true;
        doc.saving_items.clear();
        for (const LLPointer<LLInventoryItem>& each : items)
        {
            doc.saving_items.push_back(each->getUUID());
        }
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString("Saving", args));
        refreshToolbar();
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
    Doc&       doc  = *mDocs[index];
    const bool ours = doc.saving;
    doc.saving      = false;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (!result.error.empty())
    {
        args["[ERROR]"] = result.error;
        setStatus(getString("SaveFailed", args), true);
        refreshToolbar();
        return;
    }
    // The text is the server's now, compiled or not -- when it was this
    // window that sent it; a recompile from the explorer sent the asset
    // as it was, and what is typed here is still to be saved.
    if (ours)
    {
        doc.editor->resetDirty();
    }
    if (result.newAssetId.notNull())
    {
        doc.assetId = result.newAssetId;
    }
    if (result.notecard)
    {
        // The asset carries what was sent, and the server can copy it out.
        if (ours)
        {
            doc.inAsset.clear();
            doc.inAsset.insert(doc.saving_items.begin(), doc.saving_items.end());
            doc.saving_items.clear();
        }
        setStatus(getString("SavedNotecard", args));
        refreshToolbar();
        fillTabs();
        if (doc.closeAfterSave)
        {
            letGoOf(index);
            if (mClosingWindow)
            {
                continueClosing();
            }
        }
        return;
    }
    doc.problems = result.diagnostics;
    if (result.success)
    {
        // A new script runs from here; what the old one said is past.
        doc.runtime.clear();
    }
    refreshProblems(doc);
    if (ours && doc.liveFile)
    {
        // The editor outside sees what was saved here, and what the
        // compiler made of it; its own save is not written back to it.
        if (!doc.externalSave)
        {
            syncExternal(doc);
        }
        doc.externalSave = false;
        logExternal(doc, result);
    }

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
        if (mClosingWindow)
        {
            continueClosing();
        }
    }
}

// --- the analyzers -------------------------------------------------------------

void ALFloaterScriptStudio::scheduleAnalysis(Doc& doc, bool now)
{
    // An LSL file on disk is a fragment -- functions and globals for an
    // include -- which is no script to the parser; a Lua one is a module,
    // which is.
    if (!doc.loaded || doc.notecard || (!doc.file.empty() && !doc.language.lua))
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
    if (!doc.language.lua)
    {
        // A parse error on one of the preprocessor's words, with its
        // transform off, is the transform's to explain.
        static LLCachedControl<bool> switches(gSavedSettings, "ALScriptPreprocSwitch", false);
        static LLCachedControl<bool> extensions(gSavedSettings, "ALScriptPreprocExtensions", false);
        const bool                   preprocessing = ALScriptPreprocessor::enabled();
        const ALTextDocument& text = doc.editor->document();
        // The transform a line's first statement is written for, by its
        // shape -- `switch (`, `case ...:`, `break;`, `break 2;`,
        // `continue;`, `inline f(` or `inline integer f(` -- and nothing
        // for a line where one of the words is a name of the script's
        // own; the word itself comes back in `word`. (A `default:` is
        // never the line the parser stops on: the switch's own line
        // comes first.)
        auto shapeOf = [&text](S32 index, std::string& word) -> const char* {
            const std::string& line = text.line(index);
            auto               isWord = [](char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_'; };
            size_t             at     = line.find_first_not_of(" \t");
            // Past a closing or opening brace and a statement's end, which
            // the words may follow on the same line.
            while (at != std::string::npos && (line[at] == '{' || line[at] == '}' || line[at] == ';'))
            {
                at = line.find_first_not_of(" \t", at + 1);
            }
            if (at == std::string::npos || !isWord(line[at]))
            {
                return nullptr;
            }
            size_t end = at;
            while (end < line.size() && isWord(line[end]))
            {
                ++end;
            }
            word                   = line.substr(at, end - at);
            const size_t rest      = line.find_first_not_of(" \t", end);
            const char   following = rest == std::string::npos ? '\0' : line[rest];
            if (word == "switch")
            {
                return following == '(' ? "PreprocHintSwitch" : nullptr;
            }
            if (word == "case")
            {
                // `case <what>:` -- the colon somewhere after, and the
                // word not used as a name would be: `case = 1;`, `case(`.
                return following != '=' && following != '(' && following != '.' && following != ';' && line.find(':', end) != std::string::npos ? "PreprocHintSwitch" : nullptr;
            }
            if (word == "break" || word == "continue")
            {
                return following == ';' || isdigit(static_cast<unsigned char>(following)) ? "PreprocHintExtensions" : nullptr;
            }
            if (word == "inline")
            {
                // `inline name(` or `inline type name(`.
                size_t k = rest;
                for (int words = 0; words < 2 && k != std::string::npos && k < line.size() && isWord(line[k]); ++words)
                {
                    while (k < line.size() && isWord(line[k]))
                    {
                        ++k;
                    }
                    k = line.find_first_not_of(" \t", k);
                    if (k != std::string::npos && line[k] == '(')
                    {
                        return "PreprocHintExtensions";
                    }
                }
                return nullptr;
            }
            return nullptr;
        };
        for (ALScriptProblem& problem : doc.analysis)
        {
            if (problem.severity != ALScriptProblem::Severity::Error || problem.line < 0 || problem.line >= text.lineCount())
            {
                continue;
            }
            // The line the parser stopped on, written for a transform; or
            // the one before it where the line is a brace on its own,
            // since a switch's brace may open on the next line.
            std::string word;
            const char* item = shapeOf(problem.line, word);
            if (!item)
            {
                const std::string& line = text.line(problem.line);
                const size_t       at   = line.find_first_not_of(" \t");
                S32                back = problem.line - 1;
                while (at != std::string::npos && line[at] == '{' && back >= 0 && text.line(back).find_first_not_of(" \t") == std::string::npos)
                {
                    --back;
                }
                if (at != std::string::npos && line[at] == '{' && back >= 0)
                {
                    item = shapeOf(back, word);
                    if (item && std::string(item) != "PreprocHintSwitch")
                    {
                        item = nullptr;
                    }
                }
            }
            if (item && std::string(item) == "PreprocHintSwitch" && preprocessing && switches)
            {
                item = nullptr;
            }
            if (item && std::string(item) == "PreprocHintExtensions" && preprocessing && extensions)
            {
                item = nullptr;
            }
            if (item)
            {
                LLStringUtil::format_map_t args;
                args["[WORD]"] = word;
                problem.message += " " + getString(item, args);
            }
        }
    }
    // What every name is and what goes beside the text, in the source's
    // places; what stands in an include is the include's.
    const bool         mapped = preprocessed(doc) && doc.expanded.valid && doc.expanded.version == result.version;
    const ALSourceMap* map    = mapped ? &doc.expanded.map : nullptr;
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
        one.kind   = token.kind == ALScriptSymbolKind::Module ? ALSyntaxKind::Variable : syntaxKindOf(token.kind);
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
        semantics.push_back(std::move(one));
    }
    doc.editor->setSemanticTokens(std::move(semantics));
    std::vector<ALCodeEditor::InlayHint> hints;
    hints.reserve(result.hints.size());
    for (const ALScriptInlayHint& hint : result.hints)
    {
        ALTextPos at(hint.line, hint.column);
        if (map)
        {
            const ALSourceMap::Loc loc = map->toSource(hint.line, hint.column);
            if (!loc.found() || loc.file != 0)
            {
                continue;
            }
            at = ALTextPos(loc.line, loc.column);
        }
        ALCodeEditor::InlayHint one;
        one.at     = at;
        one.text   = hint.text;
        one.before = hint.kind == ALScriptInlayHint::Kind::Parameter;
        hints.push_back(std::move(one));
    }
    doc.editor->setInlayHints(std::move(hints));
    if (mapped)
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
    mProblemStore.replace(doc.id, doc.shown);
    doc.editor->setDecorations(std::move(decorations));
    if (&doc == active())
    {
        fillProblems(&doc);
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
    query.rule     = mProblemOrigin ? mProblemOrigin->getValue().asString() : std::string();
    query.text     = mProblemFilter ? mProblemFilter->getText() : std::string();
    LLStringUtil::trim(query.text);
    return query;
}

void ALFloaterScriptStudio::fillProblems(const Doc* doc)
{
    mProblems->deleteAllItems();
    if (!doc)
    {
        return;
    }
    const auto selected = mProblemStore.select(problemQuery(*doc));
    for (const Doc::Shown* problem : selected.found)
    {
        // The row carries the place, so that choosing it needs no index
        // into anything that a filter reorders.
        LLSD value;
        value["line"]      = problem->line;
        value["column"]    = problem->column;
        value["hasColumn"] = problem->hasColumn;
        value["file"]      = problem->file;
        value["fileName"]  = problem->fileName;
        value["level"]     = problem->level;
        value["origin"]    = problem->origin;
        LLSD row;
        row["value"]                = value;
        row["columns"][0]["column"] = "line";
        row["columns"][0]["value"]  = (problem->fileName.empty() ? std::string() : problem->fileName + ":") +
                                     (problem->hasColumn ? llformat("%d:%d", problem->line + 1, problem->column + 1) : llformat("%d", problem->line + 1));
        row["columns"][1]["column"] = "level";
        row["columns"][1]["value"]  = problem->level;
        row["columns"][2]["column"] = "source";
        row["columns"][2]["value"]  = problem->origin;
        row["columns"][3]["column"] = "message";
        row["columns"][3]["value"]  = problem->message;
        mProblems->addElement(row);
    }
    const S32 held = mProblemStore.countIn(doc->id);
    if (held == 0)
    {
        const bool current = doc->loaded && doc->analysisVersion == doc->editor->document().version();
        mProblems->setCommentText(current ? getString("NoProblems") : std::string());
    }
    else if (selected.found.empty())
    {
        LLStringUtil::format_map_t args;
        args["[SHOWN]"] = "0";
        args["[TOTAL]"] = std::to_string(held);
        mProblems->setCommentText(getString("ProblemsShown", args));
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
    const LLSD& problem = item->getValue();
    if (!problem.isMap())
    {
        return;
    }
    const S32  line       = problem["line"].asInteger();
    const S32  column     = problem["column"].asInteger();
    const bool has_column = problem["hasColumn"].asBoolean();
    if (!problem["file"].asString().empty())
    {
        // In an include: opened in a tab of its own where it is a script
        // or a notecard in the world; a file on disk is only named.
        openIncludeAt(problem["file"].asString(), problem["fileName"].asString(), line, has_column ? column : -1, 0);
        return;
    }
    doc->editor->setCaret(ALTextPos(line, has_column ? column : 0));
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

namespace
{
    // A line of a text, trimmed, for a row of the pane.
    std::string lineOf(const std::string& text, S32 line)
    {
        size_t begin = 0;
        for (S32 l = 0; l < line && begin != std::string::npos; ++l)
        {
            begin = text.find('\n', begin);
            if (begin != std::string::npos)
            {
                ++begin;
            }
        }
        if (begin == std::string::npos)
        {
            return std::string();
        }
        size_t      end = text.find('\n', begin);
        std::string out = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        LLStringUtil::trim(out);
        return out;
    }

    std::string lineOf(const ALTextDocument& text, S32 line)
    {
        if (line < 0 || line >= text.lineCount())
        {
            return std::string();
        }
        std::string out = text.text(ALTextRange(ALTextPos(line, 0), ALTextPos(line, text.lineLength(line))));
        LLStringUtil::trim(out);
        return out;
    }
}

void ALFloaterScriptStudio::symbolAnswered(Doc& doc, const ALScriptAnalysis::Result& result)
{
    // Of another question, or of a text that has moved on.
    if (result.version != doc.symbolVersion || ALTextPos(result.line, result.column) != doc.symbolAt || doc.symbolCommand == ALEditorCommand::None)
    {
        return;
    }
    const ALEditorCommand     command = doc.symbolCommand;
    const ALScriptReferences& refs    = result.references;
    doc.symbolCommand                 = ALEditorCommand::None;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = refs.found ? refs.name : doc.editor->document().text(doc.editor->identifierAt(doc.symbolAt));
    if (!refs.found)
    {
        setStatus(getString("NoReferences", args));
        return;
    }
    // Back to the source: the declaration and each place in this script
    // or in an include, which keeps the include's identity.
    const bool         mapped        = preprocessed(doc) && doc.expanded.valid && doc.expanded.version == result.version;
    const ALSourceMap* map           = mapped ? &doc.expanded.map : nullptr;
    bool               hasDefinition = refs.hasDefinition;
    ALScriptSpan       definition    = refs.definition;
    std::string        homePath;
    std::string        homeName;
    if (hasDefinition && map)
    {
        const S32 file = mapSpan(*map, definition);
        if (file < 0)
        {
            hasDefinition = false;
        }
        else if (file > 0)
        {
            homePath = map->files()[file].path;
            homeName = map->files()[file].name;
        }
    }
    std::vector<Doc::Place> places;
    places.reserve(refs.references.size());
    for (ALScriptSpan span : refs.references)
    {
        const ALScriptSpan raw = span;
        Doc::Place         place;
        if (map)
        {
            const S32 file = mapSpan(*map, span);
            if (file < 0)
            {
                continue;
            }
            if (file > 0)
            {
                place.file     = map->files()[file].path;
                place.fileName = map->files()[file].name;
                place.text     = lineOf(doc.expanded.text, raw.line);
            }
        }
        place.span = span;
        if (place.file.empty())
        {
            place.text = lineOf(doc.editor->document(), span.line);
        }
        places.push_back(std::move(place));
    }
    switch (command)
    {
        case ALEditorCommand::GoToDefinition:
            if (!hasDefinition)
            {
                setStatus(getString("NoDefinition", args));
            }
            else if (homePath.empty())
            {
                doc.editor->goTo(rangeOf(definition));
                doc.editor->setFocus(true);
            }
            else
            {
                openIncludeAt(homePath, homeName, definition.line, definition.column, definition.endColumn - definition.column);
            }
            break;
        case ALEditorCommand::FindReferences:
        case ALEditorCommand::Rename:
            startLookup(doc, command, refs, hasDefinition, homePath, definition, std::move(places), result.version);
            break;
        default:
            break;
    }
}

// --- across the object's scripts ------------------------------------------------------

// static
void ALFloaterScriptStudio::addPlace(Doc::Lookup& lookup, Doc::Place place)
{
    const std::string key = place.file + llformat(":%d:%d", place.span.line, place.span.column);
    if (lookup.seen.insert(key).second)
    {
        lookup.places.push_back(std::move(place));
    }
}

void ALFloaterScriptStudio::startLookup(Doc& doc, ALEditorCommand command, const ALScriptReferences& refs, bool has_definition, const std::string& home_path,
                                        const ALScriptSpan& definition, std::vector<Doc::Place> places, U32 version)
{
    Doc::Lookup& lookup   = doc.lookup;
    lookup                = Doc::Lookup();
    lookup.generation     = ++mLookupGeneration;
    lookup.command        = command;
    lookup.name           = refs.name;
    lookup.hasDefinition  = has_definition;
    lookup.homePath       = home_path;
    lookup.definition     = definition;
    lookup.renamable      = refs.renamable;
    lookup.version        = version;
    lookup.versions[""]   = version;
    // What every open script's text is now, so that a rename reaching
    // one knows whether it has moved on since.
    for (const std::unique_ptr<Doc>& each : mDocs)
    {
        if (each->loaded && each.get() != &doc)
        {
            lookup.versions[ALScriptPreprocessor::pathOf(each->ref)] = each->editor->document().version();
        }
    }
    for (Doc::Place& place : places)
    {
        addPlace(lookup, std::move(place));
    }
    // Beyond this script: every other script of its object in the same
    // language, where the name is declared somewhere they can share --
    // in an include, or in this script, which another may include. Each
    // is read as it stands in an open tab, else as the region has it,
    // and passed over where it does not so much as mention the name.
    if (has_definition && !doc.ref.inInventory())
    {
        const LLHandle<LLFloater> handle     = getHandle();
        const std::string         id         = doc.id;
        const U32                 generation = lookup.generation;
        for (const ExplorerObject& object : mExplorerModel)
        {
            bool ours = false;
            for (const ExplorerPrim& prim : object.prims)
            {
                ours = ours || prim.id == doc.ref.object;
            }
            if (!ours || !object.present)
            {
                continue;
            }
            for (const ExplorerPrim& prim : object.prims)
            {
                for (const ALScriptWorkspace::Item& item : prim.items)
                {
                    const ALScriptRef ref(prim.id, item.id);
                    if (!item.script || item.lua != doc.language.lua || ref == doc.ref)
                    {
                        continue;
                    }
                    ++lookup.pending;
                    if (const size_t index = indexOf(ref); index != NONE && mDocs[index]->loaded)
                    {
                        const Doc& other = *mDocs[index];
                        lookupCandidate(id, generation, ref, other.name, other.assetId, other.editor->text());
                        continue;
                    }
                    const std::string name = item.name;
                    ALScriptWorkspace::instance().load(ref, [handle, id, generation, ref, name](const ALScriptWorkspace::Loaded& loaded) {
                        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
                        if (!studio)
                        {
                            return;
                        }
                        std::string text = loaded.text;
                        if (loaded.error.empty() && !loaded.notecard)
                        {
                            if (std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(text))
                            {
                                text = envelope->source;
                            }
                        }
                        else
                        {
                            text.clear();
                        }
                        studio->lookupCandidate(id, generation, ref, name, loaded.assetId, text);
                    });
                }
            }
        }
    }
    if (lookup.pending > 0)
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"]  = lookup.name;
        args["[COUNT]"] = std::to_string(lookup.pending);
        setStatus(getString("LookingAcross", args));
    }
    lookupSettled(doc);
}

void ALFloaterScriptStudio::lookupCandidate(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const LLUUID& asset_id,
                                            const std::string& text)
{
    const size_t index = indexOf(id);
    if (index == NONE || mDocs[index]->lookup.generation != generation)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    if (text.empty() || text.find(doc.lookup.name) == std::string::npos)
    {
        --doc.lookup.pending;
        lookupSettled(doc);
        return;
    }
    // Expanded as the compiler would see it, its includes fetched.
    ALScriptPreprocessor::Request request;
    request.ref           = ref;
    request.name          = name;
    request.assetId       = asset_id;
    request.source        = text;
    request.lua           = doc.language.lua;
    request.compileTarget = doc.language.compileTarget;
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptPreprocessor::instance().run(request, [handle, id, generation, ref, name](const ALPreprocessor::Result& result) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->lookupExpanded(id, generation, ref, name, result);
        }
    });
}

void ALFloaterScriptStudio::lookupExpanded(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name,
                                           const ALPreprocessor::Result& result)
{
    const size_t index = indexOf(id);
    if (index == NONE || mDocs[index]->lookup.generation != generation)
    {
        return;
    }
    Doc&              doc  = *mDocs[index];
    const std::string self = ALScriptPreprocessor::pathOf(ref);
    const std::string home = doc.lookup.homePath.empty() ? ALScriptPreprocessor::pathOf(doc.ref) : doc.lookup.homePath;
    // The script declaring the name, in this expansion: the script
    // itself, or one of its includes; a script that has neither cannot
    // name it.
    const S32 file = self == home ? 0 : result.map.fileOf(home);
    if (file < 0)
    {
        --doc.lookup.pending;
        lookupSettled(doc);
        return;
    }
    const ALSourceMap::Loc at = result.map.toExpanded(file, doc.lookup.definition.line, doc.lookup.definition.column);
    if (!at.found())
    {
        --doc.lookup.pending;
        lookupSettled(doc);
        return;
    }
    ALScriptAnalysis::Request request;
    request.kind    = ALScriptAnalysis::Kind::References;
    request.id      = "lookup:" + ref.id();
    request.version = generation;
    request.lua     = doc.language.lua;
    request.mono    = doc.language.compileTarget != "lsl2";
    request.text    = result.text;
    request.line    = at.line;
    request.column  = at.column;
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptAnalysis::instance().ask(std::move(request),
                                     [handle, id, generation, ref, name, map = result.map, expanded = result.text](const ALScriptAnalysis::Result& answer) {
                                         if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                                         {
                                             studio->lookupAnswered(id, generation, ref, name, map, expanded, answer);
                                         }
                                     });
}

void ALFloaterScriptStudio::lookupAnswered(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const ALSourceMap& map,
                                           const std::string& expanded, const ALScriptAnalysis::Result& result)
{
    const size_t index = indexOf(id);
    if (index == NONE || mDocs[index]->lookup.generation != generation)
    {
        return;
    }
    Doc&              doc  = *mDocs[index];
    const std::string self = ALScriptPreprocessor::pathOf(ref);
    const std::string own  = ALScriptPreprocessor::pathOf(doc.ref);
    --doc.lookup.pending;
    if (result.references.found)
    {
        for (ALScriptSpan span : result.references.references)
        {
            const ALScriptSpan raw  = span;
            const S32          file = mapSpan(map, span);
            if (file < 0)
            {
                continue;
            }
            Doc::Place place;
            place.span     = span;
            place.file     = file == 0 ? self : map.files()[file].path;
            place.fileName = file == 0 ? name : map.files()[file].name;
            place.text     = lineOf(expanded, raw.line);
            if (place.file == own)
            {
                // This script's own, which its own answer listed.
                place.file.clear();
                place.fileName.clear();
            }
            addPlace(doc.lookup, std::move(place));
        }
    }
    lookupSettled(doc);
}

void ALFloaterScriptStudio::lookupSettled(Doc& doc)
{
    Doc::Lookup& lookup = doc.lookup;
    if (lookup.pending > 0 || lookup.command == ALEditorCommand::None)
    {
        return;
    }
    const ALEditorCommand command = lookup.command;
    lookup.command                = ALEditorCommand::None;
    // This script's places first, then each other script's, in order.
    std::stable_sort(lookup.places.begin(), lookup.places.end(), [](const Doc::Place& a, const Doc::Place& b) {
        if (a.file.empty() != b.file.empty())
        {
            return a.file.empty();
        }
        if (a.fileName != b.fileName)
        {
            return a.fileName < b.fileName;
        }
        if (a.file != b.file)
        {
            return a.file < b.file;
        }
        return a.span < b.span;
    });
    std::set<std::string> files;
    for (const Doc::Place& place : lookup.places)
    {
        files.insert(place.file);
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"]  = lookup.name;
    args["[COUNT]"] = std::to_string(lookup.places.size());
    args["[FILES]"] = std::to_string(files.size());
    if (command == ALEditorCommand::FindReferences)
    {
        doc.places = lookup.places;
        std::vector<ALTextRange> lit;
        for (const Doc::Place& place : doc.places)
        {
            if (place.file.empty())
            {
                lit.push_back(rangeOf(place.span));
            }
        }
        doc.editor->setHighlights(std::move(lit));
        fillReferences(&doc);
        showBottom("references_tab");
        setStatus(getString(files.size() > 1 ? "ReferencesFoundAcross" : "ReferencesFound", args));
    }
    else if (command == ALEditorCommand::Rename)
    {
        if (lookup.renamable)
        {
            askNewName(doc);
        }
        else
        {
            setStatus(getString("NotRenamable", args));
        }
    }
}

void ALFloaterScriptStudio::askNewName(Doc& doc)
{
    const std::string         id         = doc.id;
    const U32                 generation = doc.lookup.generation;
    const std::string         old_name   = doc.lookup.name;
    const LLHandle<LLFloater> handle     = getHandle();
    ALQuickOpen* quick = quickOpen(
        {}, getString("RenamePlaceholder"), getString("RenameTitle"),
        [handle, id, generation](const std::string& typed) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->renameTo(id, generation, typed);
            }
        },
        mEditorHost, 420, 56);
    if (!quick)
    {
        return;
    }
    // The row under the field says what return will do with what is typed.
    const S32             count = static_cast<S32>(doc.lookup.places.size());
    std::set<std::string> files;
    for (const Doc::Place& place : doc.lookup.places)
    {
        files.insert(place.file);
    }
    const S32 scripts = static_cast<S32>(files.size());
    quick->onQueryChanged([handle, quick, count, scripts, old_name](const std::string& typed) {
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
        args["[FILES]"] = std::to_string(scripts);
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
            quick->setHint(studio->getString(scripts > 1 ? "RenameToAcross" : "RenameTo", args));
        }
    });
    quick->setQuery(old_name);
    quick->takeFocus();
}

void ALFloaterScriptStudio::renameTo(const std::string& id, U32 generation, const std::string& new_name)
{
    const size_t index = indexOf(id);
    if (index == NONE || mDocs[index]->lookup.generation != generation)
    {
        return;
    }
    Doc&              doc      = *mDocs[index];
    const Doc::Lookup& lookup  = doc.lookup;
    const std::string old_name = lookup.name;
    std::string       name     = new_name;
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
    if (doc.editor->document().version() != lookup.version)
    {
        setStatus(getString("RenameStale", args), true);
        return;
    }
    // Each script's places together: this one's put in as one step;
    // another's the same where it is open and unchanged since it was
    // read, else opened with the change waiting for its text.
    std::map<std::string, std::vector<const Doc::Place*>> by_file;
    for (const Doc::Place& place : lookup.places)
    {
        by_file[place.file].push_back(&place);
    }
    S32 renamed = 0;
    S32 scripts = 0;
    S32 opened  = 0;
    S32 stale   = 0;
    for (const auto& [file, places] : by_file)
    {
        if (file.empty())
        {
            std::vector<std::pair<ALTextRange, std::string>> edits;
            edits.reserve(places.size());
            for (const Doc::Place* place : places)
            {
                edits.emplace_back(rangeOf(place->span), name);
            }
            if (doc.editor->replaceAll(std::move(edits)))
            {
                doc.editor->undoJournal().label("rename");
                renamed += static_cast<S32>(places.size());
                ++scripts;
            }
            continue;
        }
        ALScriptRef ref;
        if (!ALScriptPreprocessor::refOf(file, ref))
        {
            // On disk: not the studio's to change.
            ++stale;
            continue;
        }
        const size_t other_index = indexOf(ref);
        if (other_index != NONE && mDocs[other_index]->loaded)
        {
            Doc&       other   = *mDocs[other_index];
            const auto version = lookup.versions.find(file);
            if (version == lookup.versions.end() || version->second != other.editor->document().version() || !other.modifiable)
            {
                ++stale;
                continue;
            }
            // Each place where the old name still stands, since an open
            // include may not read as the expansion had it.
            const ALTextDocument&                            text = other.editor->document();
            std::vector<std::pair<ALTextRange, std::string>> edits;
            for (const Doc::Place* place : places)
            {
                const ALTextRange range = rangeOf(place->span);
                if (place->span.line < text.lineCount() && text.text(range) == old_name)
                {
                    edits.emplace_back(range, name);
                }
            }
            if (!edits.empty())
            {
                renamed += static_cast<S32>(edits.size());
                if (other.editor->replaceAll(std::move(edits)))
                {
                    other.editor->undoJournal().label("rename");
                }
                ++scripts;
            }
            continue;
        }
        // Not open: opened, with the change made once its text is in,
        // where the old name still stands at each place.
        if (other_index == NONE)
        {
            openScript(ref, places.front()->fileName);
        }
        const size_t opened_index = indexOf(ref);
        if (opened_index == NONE)
        {
            ++stale;
            continue;
        }
        Doc& other = *mDocs[opened_index];
        for (const Doc::Place* place : places)
        {
            other.pendingEdits.push_back(Doc::PendingEdit{ place->span, old_name, name });
        }
        renamed += static_cast<S32>(places.size());
        ++scripts;
        ++opened;
    }
    args["[COUNT]"] = std::to_string(renamed);
    args["[FILES]"] = std::to_string(scripts);
    args["[OPENED]"] = std::to_string(opened);
    args["[STALE]"]  = std::to_string(stale);
    if (scripts > 1 || opened > 0)
    {
        setStatus(getString(stale > 0 ? "RenamedAcrossStale" : "RenamedAcross", args), stale > 0);
    }
    else
    {
        setStatus(getString(stale > 0 ? "RenamedStale" : "Renamed", args), stale > 0);
    }
    activate(index);
    doc.editor->setFocus(true);
}

void ALFloaterScriptStudio::applyPendingEdits(Doc& doc)
{
    if (doc.pendingEdits.empty() || !doc.loaded)
    {
        return;
    }
    std::vector<Doc::PendingEdit> edits;
    edits.swap(doc.pendingEdits);
    const ALTextDocument&                            text = doc.editor->document();
    std::vector<std::pair<ALTextRange, std::string>> changes;
    S32                                              missed = 0;
    for (const Doc::PendingEdit& edit : edits)
    {
        const ALTextRange range = rangeOf(edit.span);
        if (edit.span.line < text.lineCount() && text.text(range) == edit.was)
        {
            changes.emplace_back(range, edit.now);
        }
        else
        {
            ++missed;
        }
    }
    if (!changes.empty() && doc.modifiable)
    {
        doc.editor->setReadOnly(false);
        if (doc.editor->replaceAll(std::move(changes)))
        {
            doc.editor->undoJournal().label("rename");
        }
    }
    if (missed > 0)
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"]  = doc.name;
        args["[COUNT]"] = std::to_string(missed);
        setStatus(getString("RenameMissed", args), true);
    }
}

// --- an editor outside ----------------------------------------------------------------

namespace
{
    // The temp file an external editor is given, watched for its saves;
    // gone from disk with it.
    class StudioLiveFile final : public LLLiveFile
    {
    public:
        typedef std::function<void(const std::string& filename)> changed_t;

        // A temp file of the studio's own goes with the watch; a file
        // the author keeps on disk stays.
        StudioLiveFile(const std::string& path, changed_t changed, bool ours)
        :   LLLiveFile(path, 1.f),
            mChanged(std::move(changed)),
            mOurs(ours)
        {
        }
        ~StudioLiveFile() override
        {
            if (mOurs)
            {
                LLFile::remove(filename());
            }
        }

        // The next change is one made here, not to be taken as the
        // editor's.
        void ignoreNextUpdate() { mIgnoreNext = true; }

    protected:
        bool loadFile() override
        {
            if (mIgnoreNext)
            {
                mIgnoreNext = false;
                return true;
            }
            if (mChanged)
            {
                mChanged(filename());
            }
            return true;
        }

    private:
        changed_t mChanged;
        bool      mOurs;
        bool      mIgnoreNext = false;
    };

    bool writeWhole(const std::string& path, const std::string& text)
    {
        LLFILE* file = LLFile::fopen(path, LLFILE_MODE("wb"));
        if (!file)
        {
            return false;
        }
        // An empty script is stored as one space, as it always was.
        const std::string& out = text.empty() ? std::string(" ") : text;
        fputs(out.c_str(), file);
        fclose(file);
        return true;
    }
}

// static
std::string ALFloaterScriptStudio::externalFileName(const Doc& doc)
{
    // As the old editor named it, so that the bridge's script.list and
    // whoever reads the temp folder find the same file: the name
    // without what a file system refuses, the subscription id, and the
    // language's extension.
    static const std::set<char> forbidden{ '<', '>', ':', '"', '\\', '/', '|', '?', '*' };
    std::string                 name = doc.name;
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return forbidden.count(c) > 0; }), name.end());
    const std::string hash      = LLScriptEditorWSServer::buildScriptSubscriptionId(doc.ref.object, doc.ref.item);
    const std::string extension = doc.language.lua ? ".luau" : ".lsl";
    return std::string(LLFile::tmpdir()) + "sl_script_" + (name.empty() ? std::string() : name + "_") + hash + extension;
}

void ALFloaterScriptStudio::editExternally(Doc& doc)
{
    if (!doc.loaded || !doc.modifiable || doc.notecard)
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    // The file, written afresh -- the editor may have been closed on an
    // old one -- and watched. A file on disk is edited where it is.
    const bool        on_disk  = !doc.file.empty();
    const std::string filename = on_disk ? doc.file : externalFileName(doc);
    if (!on_disk && !writeWhole(filename, doc.editor->text()))
    {
        args["[FILE]"] = filename;
        setStatus(getString("ExternalWriteFailed", args), true);
        return;
    }
    if (on_disk)
    {
        // Watched since it was opened; a save there comes in as any
        // outside change does.
        watchFile(doc);
    }
    else if (!doc.liveFile || doc.liveFile->filename() != filename)
    {
        doc.liveFile.reset();
        const LLHandle<LLFloater> handle = getHandle();
        const std::string         id     = doc.id;
        auto                      watch  = std::make_unique<StudioLiveFile>(
            filename,
            [handle, id](const std::string& file) {
                if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                {
                    studio->externalChanged(id, file);
                }
            },
            true);
        watch->addToEventTimer();
        doc.liveFile = std::move(watch);
    }
    else
    {
        static_cast<StudioLiveFile*>(doc.liveFile.get())->ignoreNextUpdate();
    }
    doc.liveLog = on_disk ? std::string() : filename + ".log";

    // The bridge, so that VS Code can subscribe to the script and hear
    // what the compiler says of it; a file on disk is nothing to it.
    const bool                       tight  = !on_disk && LLScriptEditorWSServer::isTightIntegration();
    LLScriptEditorWSServer::ptr_t    server = !on_disk && LLScriptEditorWSServer::isEnabled() ? LLScriptEditorWSServer::ensureServerRunning() : nullptr;
    if (server)
    {
        const std::string script_id = LLScriptEditorWSServer::buildScriptSubscriptionId(doc.ref.object, doc.ref.item);
        doc.subscribed = server->subscribeScriptEditor(doc.ref.object, doc.ref.item, doc.name, getHandle(), script_id, doc.language.lua);
    }
    if (tight)
    {
        if (!server)
        {
            LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", LLTrans::getString("ExternalEditorFailedToStart")));
            return;
        }
        LLUUID root_id;
        if (LLViewerObject* object = doc.ref.inInventory() ? nullptr : gObjectList.findObject(doc.ref.object))
        {
            root_id = object->getRootEdit() ? object->getRootEdit()->getID() : object->getID();
        }
        if (!LLScriptEditorWSServer::launchVSCode(root_id, doc.ref.item))
        {
            LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", LLTrans::getString("VSCodeLaunchFailed")));
            return;
        }
        setStatus(getString("ExternalOpenedVSCode", args));
        return;
    }
    LLExternalEditor             editor;
    LLExternalEditor::EErrorCode status = editor.setCommand("LL_SCRIPT_EDITOR");
    if (status != LLExternalEditor::EC_SUCCESS)
    {
        const std::string message = status == LLExternalEditor::EC_NOT_SPECIFIED ? LLTrans::getString("ExternalEditorNotSet")
                                                                                   : LLExternalEditor::getErrorMessage(status);
        LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", message));
        return;
    }
    status = editor.run(filename, doc.editor->caret().line + 1);
    if (status != LLExternalEditor::EC_SUCCESS)
    {
        LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", LLExternalEditor::getErrorMessage(status)));
        return;
    }
    setStatus(getString("ExternalOpened", args));
}

void ALFloaterScriptStudio::externalChanged(const std::string& id, const std::string& file)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    if (!doc.loaded || !doc.modifiable)
    {
        return;
    }
    std::string text = readWholeFile(file);
    if (text.empty())
    {
        // Gone, or empty: an editor saving a file in two steps leaves it
        // so for a moment; nothing to take yet.
        return;
    }
    if (text != doc.editor->text())
    {
        // The editor's text, as one step to undo; then saved from here,
        // over whatever a check finds, since the editor outside is where
        // the author is looking.
        doc.carriedText = text;
        takeCarriedText(doc);
    }
    if (!doc.editor->isDirty() && doc.assetId.notNull())
    {
        return;
    }
    doc.externalSave    = true;
    doc.saveAnywayUntil = LLTimer::getTotalSeconds() + 3600.0;
    save(doc);
}

void ALFloaterScriptStudio::syncExternal(Doc& doc)
{
    if (!doc.liveFile)
    {
        return;
    }
    const std::string filename = doc.liveFile->filename();
    if (!gDirUtilp->fileExists(filename))
    {
        return;
    }
    static_cast<StudioLiveFile*>(doc.liveFile.get())->ignoreNextUpdate();
    writeWhole(filename, doc.editor->text());
}

void ALFloaterScriptStudio::logExternal(Doc& doc, const ALScriptWorkspace::CompileResult& result)
{
    if (doc.liveLog.empty())
    {
        return;
    }
    llofstream file(doc.liveLog.c_str());
    if (!file.is_open())
    {
        return;
    }
    file << "// " << LLLogChat::timestamp2LogString(0, true) << "\n\n";
    if (result.success)
    {
        file << LLTrans::getString("CompileSuccessful") << "\n" << LLTrans::getString("SaveComplete") << "\n";
    }
    for (const std::string& message : result.messages)
    {
        std::string line = message;
        LLStringUtil::stripNonprintable(line);
        file << line << "\n";
    }
}

void ALFloaterScriptStudio::stopExternal(Doc& doc)
{
    if (doc.subscribed)
    {
        if (LLScriptEditorWSServer::ptr_t server = LLScriptEditorWSServer::getServer())
        {
            const std::string script_id = LLScriptEditorWSServer::buildScriptSubscriptionId(doc.ref.object, doc.ref.item);
            server->sendUnsubscribeScriptEditor(script_id);
            server->unsubscribeEditor(script_id);
        }
        doc.subscribed = false;
    }
    doc.liveFile.reset();
    if (!doc.liveLog.empty())
    {
        LLFile::remove(doc.liveLog);
        doc.liveLog.clear();
    }
    doc.externalSave = false;
}

void ALFloaterScriptStudio::watchFile(Doc& doc)
{
    if (doc.file.empty() || doc.liveFile)
    {
        return;
    }
    // The file watched for changes made outside, whoever makes them: an
    // editor the studio started, or anything else.
    const LLHandle<LLFloater> handle = getHandle();
    const std::string         id     = doc.id;
    auto                      watch  = std::make_unique<StudioLiveFile>(
        doc.file,
        [handle, id](const std::string& file) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChangedOutside(id, file);
            }
        },
        false);
    watch->addToEventTimer();
    doc.liveFile = std::move(watch);
}

void ALFloaterScriptStudio::fileChangedOutside(const std::string& id, const std::string& file)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (doc.editor->isDirty())
    {
        // What is typed here is not thrown away for it; the author is told.
        setStatus(getString("FileChangedOutside", args), true);
        return;
    }
    const std::string text = readWholeFile(file);
    if (text == doc.editor->text())
    {
        return;
    }
    // Taken as one step to undo, and clean, since it is what the file is.
    doc.carriedText = text;
    takeCarriedText(doc);
    fileSettled(doc);
    setStatus(getString("FileReloaded", args));
}

void ALFloaterScriptStudio::saveFile(Doc& doc)
{
    if (doc.liveFile)
    {
        // The watcher on the file: this write is not an outside change.
        static_cast<StudioLiveFile*>(doc.liveFile.get())->ignoreNextUpdate();
    }
    std::ofstream out(doc.file, std::ios::binary);
    out << doc.editor->text();
    LLStringUtil::format_map_t args;
    args["[PATH]"] = doc.file;
    if (!out.good())
    {
        setStatus(getString("SaveToFileFailed", args), true);
        return;
    }
    setStatus(getString("SavedToFile", args));
    fileSettled(doc);
}

void ALFloaterScriptStudio::fileSettled(Doc& doc)
{
    doc.editor->resetDirty();
    // The scripts that include it see the file as it is now.
    for (std::unique_ptr<Doc>& each : mDocs)
    {
        if (each.get() != &doc && each->file.empty() && preprocessed(*each))
        {
            each->expanded.valid = false;
            preprocess(*each, false);
            scheduleAnalysis(*each);
        }
    }
    fillTabs();
    refreshToolbar();
    if (doc.closeAfterSave)
    {
        const size_t index = indexOf(doc.id);
        if (index != NONE)
        {
            letGoOf(index);
        }
        if (mClosingWindow)
        {
            continueClosing();
        }
    }
}

void ALFloaterScriptStudio::fillReferences(const Doc* doc)
{
    mReferences->deleteAllItems();
    if (!doc || doc->places.empty())
    {
        return;
    }
    for (size_t i = 0; i < doc->places.size(); ++i)
    {
        const Doc::Place& place = doc->places[i];
        LLSD              row;
        row["value"]                = static_cast<S32>(i);
        row["columns"][0]["column"] = "where";
        row["columns"][0]["value"]  = place.file.empty() ? doc->name : place.fileName;
        row["columns"][1]["column"] = "line";
        row["columns"][1]["value"]  = llformat("%d:%d", place.span.line + 1, place.span.column + 1);
        row["columns"][2]["column"] = "text";
        row["columns"][2]["value"]  = place.text;
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
    if (index >= doc->places.size())
    {
        return;
    }
    const Doc::Place& place = doc->places[index];
    if (place.file.empty())
    {
        doc->editor->goTo(rangeOf(place.span));
        doc->editor->setFocus(true);
    }
    else
    {
        openIncludeAt(place.file, place.fileName, place.span.line, place.span.column, place.span.endColumn - place.span.column);
    }
}

void ALFloaterScriptStudio::openIncludeAt(const std::string& path, const std::string& name, S32 line, S32 column, S32 length)
{
    ALScriptRef ref;
    std::string file;
    if (ALScriptPreprocessor::refOf(path, ref))
    {
        const LLInventoryItem* item = ref.inInventory() ? gInventory.getItem(ref.item)
                                      : gObjectList.findObject(ref.object) ? gObjectList.findObject(ref.object)->getInventoryItem(ref.item)
                                                                           : nullptr;
        if (!item)
        {
            LLStringUtil::format_map_t args;
            args["[FILE]"] = name;
            setStatus(getString("IncludeGone", args));
            return;
        }
        goToPlace(ref, name, line, column, length);
    }
    else if (ALScriptPreprocessor::fileOf(path, file))
    {
        const Doc* asking = active();
        openFile(file, asking && asking->language.lua, line, column, length);
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
        mEditorHost, 420, ALQuickOpen::heightForRows(1),
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
    if (!doc || !doc->loaded || doc->notecard)
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
            // No name here; what is wrong here, if anything, still is.
            doc->inspectAt = ALTextPos(-1, -1);
            showSymbol(problemsAt(*doc, caret));
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
        LLStringUtil::format_map_t args;
        if (result.hover.hasDefinition)
        {
            args["[LINE]"] = std::to_string(result.hover.definitionLine + 1);
            text += "\n" + getString("InspectDeclared", args);
        }
        if (!result.hover.expected.empty())
        {
            args["[TYPE]"] = result.hover.expected;
            text += "\n" + getString("HoverExpected", args);
        }
        if (!result.hover.typeDetail.empty())
        {
            text += "\n\n" + result.hover.typeDetail;
        }
        std::string documentation = result.hover.documentation;
        std::string link          = result.hover.link;
        // What the keyword file says, where the analyzer has no words of
        // its own: LSL's declarations come without any.
        const Vocab* word = documentation.empty() ? vocabWord(doc.language.lua, doc.editor->document().text(doc.editor->identifierAtCaret())) : nullptr;
        if (word)
        {
            documentation = word->tooltip;
            if (link.empty())
            {
                link = helpUrl(doc.language.lua, word->text);
            }
        }
        if (!documentation.empty())
        {
            text += "\n\n" + documentation;
        }
        if (!link.empty())
        {
            text += "\n" + link;
        }
    }
    // What is wrong where the caret is, said under the name.
    const std::string problems = problemsAt(doc, doc.inspectAt);
    if (!problems.empty())
    {
        text += (text.empty() ? "" : "\n\n") + problems;
    }
    showSymbol(text, result.hover.found && result.hover.hasDefinition ? result.hover.definitionLine : -1);
}

void ALFloaterScriptStudio::showSymbol(const std::string& text, S32 declared_line)
{
    mSymbol->setText(text);
    const S32 lines = mSymbol->document().lineCount();
    for (S32 line = 0; line < lines; ++line)
    {
        mSymbol->linkUrlsOn(line);
    }
    // The declaration's line comes right after the name.
    if (declared_line >= 0 && lines > 1 && mSymbol->document().lineLength(1) > 0)
    {
        ALTextView::Substitution to_line;
        to_line.range         = ALTextRange(ALTextPos(1, 0), mSymbol->document().lineEnd(1));
        to_line.link          = true;
        to_line.tooltip       = getString("InspectDeclaredTip");
        to_line.value["line"] = declared_line;
        mSymbol->addSubstitution(std::move(to_line));
    }
}

std::string ALFloaterScriptStudio::problemsAt(const Doc& doc, const ALTextPos& at) const
{
    // From the checkers and the compiler alike: whatever is squiggled
    // under the position, with what it says.
    std::string problems;
    for (const ALCodeEditor::Decoration& decoration : doc.editor->decorations())
    {
        if (decoration.style == ALCodeEditor::Decoration::Style::Squiggle && !decoration.message.empty() && decoration.range.contains(at))
        {
            LLStringUtil::format_map_t args;
            args["[MESSAGE]"] = decoration.message;
            problems += (problems.empty() ? "" : "\n") + getString("InspectProblem", args);
        }
    }
    return problems;
}

// --- the reference ----------------------------------------------------------------------

// static
const ALFloaterScriptStudio::Vocab* ALFloaterScriptStudio::vocabWord(bool lua, std::string_view name)
{
    if (name.empty())
    {
        return nullptr;
    }
    for (const Vocab& word : vocabulary(lua))
    {
        if (word.text == name)
        {
            return &word;
        }
    }
    return nullptr;
}

// static
std::string ALFloaterScriptStudio::helpUrl(bool lua, const std::string& word)
{
    // The wiki's page for an LSL name, which a SLua ll.Name shares; the
    // Luau library's own page for its libraries; the SLua portal for the
    // rest.
    if (!lua || word.compare(0, 3, "ll.") == 0)
    {
        std::string page = word;
        if (lua)
        {
            page.erase(2, 1);
        }
        LLUIString url(gSavedSettings.getString("LSLHelpURL"));
        url.setArg("[LSL_STRING]", page.empty() ? std::string("LSL_Portal") : page);
        return url.getString();
    }
    for (const char* library : { "bit32.", "buffer.", "coroutine.", "debug.", "math.", "os.", "string.", "table.", "utf8." })
    {
        if (word.compare(0, strlen(library), library) == 0)
        {
            return "https://luau.org/library";
        }
    }
    return "https://wiki.secondlife.com/wiki/Lua_Alpha";
}

void ALFloaterScriptStudio::showReference(const Vocab& word, bool lua)
{
    mFolds.setCollapsed("inspector", false);
    std::string text = word.detail.empty() ? word.text : word.detail;
    if (word.deprecated)
    {
        text += "  (" + getString("Deprecated") + ")";
    }
    if (!word.tooltip.empty())
    {
        text += "\n\n" + word.tooltip;
    }
    text += "\n" + helpUrl(lua, word.text);
    showSymbol(text);
}

void ALFloaterScriptStudio::reference(Doc& doc)
{
    mFolds.setCollapsed("inspector", false);
    const ALTextRange word = doc.editor->identifierAtCaret();
    const std::string name = doc.editor->document().text(word);
    if (const Vocab* known = vocabWord(doc.language.lua, name))
    {
        showReference(*known, doc.language.lua);
        return;
    }
    // A word of the script's own: what the analyzer knows of it, asked
    // for now rather than a moment after the caret settles.
    if (!word.empty())
    {
        doc.inspectAt      = word.begin;
        doc.inspectVersion = doc.editor->document().version();
        doc.inspectDue     = 0.0;
        askAnalyzer(doc, ALScriptAnalysis::Kind::Inspect, word.begin);
    }
    else
    {
        setStatus(getString("NoReference"));
    }
}

void ALFloaterScriptStudio::browseReference()
{
    Doc*       doc = active();
    const bool lua = doc ? doc->language.lua : false;
    std::vector<ALQuickOpen::Candidate> candidates;
    const std::vector<Vocab>&           words = vocabulary(lua);
    for (size_t i = 0; i < words.size(); ++i)
    {
        if (words[i].kind != ALSyntaxKind::Function && words[i].kind != ALSyntaxKind::Event && words[i].kind != ALSyntaxKind::Constant)
        {
            continue;
        }
        ALQuickOpen::Candidate one;
        one.label  = words[i].text;
        one.detail = kindName(words[i].kind == ALSyntaxKind::Function ? ALScriptSymbolKind::Function
                              : words[i].kind == ALSyntaxKind::Event  ? ALScriptSymbolKind::Event
                                                                       : ALScriptSymbolKind::Constant);
        one.also   = words[i].tooltip.substr(0, words[i].tooltip.find('\n'));
        one.value  = std::to_string(i);
        candidates.push_back(std::move(one));
    }
    if (candidates.empty())
    {
        return;
    }
    const LLHandle<LLFloater> handle = getHandle();
    quickOpen(std::move(candidates), getString("ReferencePlaceholder"), getString("ReferenceTitle"), [handle, lua](const std::string& value) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        if (!studio)
        {
            return;
        }
        const std::vector<Vocab>& words = studio->vocabulary(lua);
        const size_t              index = static_cast<size_t>(atoi(value.c_str()));
        if (index < words.size())
        {
            studio->showReference(words[index], lua);
        }
    }, mEditorHost);
}

// static
const char* ALFloaterScriptStudio::imageNameOf(const Doc& doc)
{
    if (!doc.notecard)
    {
        return doc.language.lua ? "Inv_Script_Luau" : "Inv_Script";
    }
    if (doc.name == ".luaurc" || doc.name == ".lslrc")
    {
        return "Studio_Config";
    }
    return doc.file.empty() ? "Inv_Notecard" : "Studio_File";
}

// static
const char* ALFloaterScriptStudio::imageNameOf(ALScriptSymbolKind kind)
{
    switch (kind)
    {
        case ALScriptSymbolKind::Keyword:   return "Symbol_Keyword";
        case ALScriptSymbolKind::Variable:  return "Symbol_Variable";
        case ALScriptSymbolKind::Parameter: return "Symbol_Parameter";
        case ALScriptSymbolKind::Function:  return "Symbol_Function";
        case ALScriptSymbolKind::Field:     return "Symbol_Field";
        case ALScriptSymbolKind::Type:      return "Symbol_Type";
        case ALScriptSymbolKind::Constant:  return "Symbol_Constant";
        case ALScriptSymbolKind::Event:     return "Symbol_Event";
        case ALScriptSymbolKind::State:
        case ALScriptSymbolKind::Label:     return "Symbol_Label";
        case ALScriptSymbolKind::Module:    return "Symbol_Module";
    }
    return "Symbol_Word";
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
        row["columns"][0]["column"] = "icon";
        row["columns"][0]["type"]   = "icon";
        row["columns"][0]["value"]  = imageNameOf(entry.kind);
        row["columns"][1]["column"] = "symbol";
        row["columns"][1]["value"]  = std::string(static_cast<size_t>(entry.depth) * 4, ' ') + entry.name;
        row["columns"][1]["tool_tip"] = entry.detail;
        row["columns"][2]["column"] = "kind";
        row["columns"][2]["value"]  = kindName(entry.kind);
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
    refreshTrailer(doc);
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

// --- find in files -------------------------------------------------------------------

// Said as a sentence rather than as a form:
//
//     Find `timer` as [text] [ignoring case] in [open scripts]
void ALFloaterScriptStudio::buildSearchBar()
{
    std::vector<ALScopeBar::Segment> said;

    ALScopeBar::Segment find;
    find.kind = ALScopeBar::Segment::Kind::Word;
    find.text = getString("SearchFind");
    said.push_back(find);

    ALScopeBar::Segment query;
    query.kind    = ALScopeBar::Segment::Kind::Field;
    query.name    = "query";
    query.text    = getString("SearchPlaceholder");
    query.toolTip = getString("SearchQueryTip");
    said.push_back(query);

    ALScopeBar::Segment as;
    as.kind = ALScopeBar::Segment::Kind::Word;
    as.text = getString("SearchAs");
    said.push_back(as);

    ALScopeBar::Segment how;
    how.kind    = ALScopeBar::Segment::Kind::Choice;
    how.name    = "how";
    how.toolTip = getString("SearchHowTip");
    how.choices = { { getString("SearchText"), "text" }, { getString("SearchWord"), "word" }, { getString("SearchPattern"), "pattern" } };
    said.push_back(how);

    ALScopeBar::Segment letters;
    letters.kind    = ALScopeBar::Segment::Kind::Choice;
    letters.name    = "case";
    letters.toolTip = getString("SearchCaseTip");
    letters.choices = { { getString("SearchAnyCase"), "any" }, { getString("SearchThisCase"), "exact" } };
    said.push_back(letters);

    ALScopeBar::Segment in;
    in.kind = ALScopeBar::Segment::Kind::Word;
    in.text = getString("SearchIn");
    said.push_back(in);

    ALScopeBar::Segment where;
    where.kind    = ALScopeBar::Segment::Kind::Choice;
    where.name    = "scope";
    where.toolTip = getString("SearchScopeTip");
    where.choices = { { getString("SearchOpen"), "open" }, { getString("SearchThisObject"), "object" }, { getString("SearchListed"), "listed" } };
    said.push_back(where);

    mSearchBar->setSentence(std::move(said));
}

void ALFloaterScriptStudio::findInFiles()
{
    showBottom("search_tab");
    if (LLLineEditor* field = mSearchBar->findChild<LLLineEditor>("query"))
    {
        // What is selected in the editor is what is most likely meant.
        if (Doc* doc = active())
        {
            const ALTextRange selection = doc->editor->selection();
            if (!selection.empty() && selection.begin.line == selection.end.line)
            {
                const ALTextRange ordered(std::min(selection.begin, selection.end), std::max(selection.begin, selection.end));
                mSearchBar->setValue("query", doc->editor->document().text(ordered));
            }
        }
        field->setFocus(true);
        field->selectAll();
    }
}

// A dropdown moved: the same words asked about again. Typing waits for
// return, since a search over an object's contents fetches what it has
// not got.
void ALFloaterScriptStudio::onSearchChanged()
{
    const std::string query = mSearchBar->valueOf("query");
    if (!query.empty() && query == mSearchQuery)
    {
        search();
    }
}

void ALFloaterScriptStudio::search()
{
    ++mSearchGeneration;
    mSearchPending = 0;
    mSearchHits    = 0;
    mSearchFiles   = 0;
    mSearchQuery   = mSearchBar->valueOf("query");
    mSearchResults->deleteAllItems();
    if (mSearchQuery.empty())
    {
        mSearchCount->setText(LLStringUtil::null);
        return;
    }
    const std::string scope = mSearchBar->valueOf("scope");
    // The scripts open are searched as they stand, wherever they are; an
    // object's contents as the region has them, but for a script open
    // from it, which is searched as it stands too.
    auto searchOpen = [this](const Doc& doc) {
        if (!doc.loaded)
        {
            return;
        }
        searchDocument(doc.ref, doc.name, doc.ref.inInventory() ? LLStringUtil::null : objectNameOf(gObjectList.findObject(doc.ref.object), getString("ObjectUnnamed")),
                       doc.editor->document());
    };
    if (scope == "open")
    {
        for (const std::unique_ptr<Doc>& doc : mDocs)
        {
            searchOpen(*doc);
        }
        searchSettled();
        return;
    }
    // This object: the active script's, else the first listed.
    LLUUID only;
    if (scope == "object")
    {
        if (Doc* doc = active(); doc && !doc->ref.inInventory())
        {
            if (LLViewerObject* object = gObjectList.findObject(doc->ref.object))
            {
                only = object->getRootEdit() ? object->getRootEdit()->getID() : object->getID();
            }
        }
        if (only.isNull() && !mExplorerModel.empty())
        {
            only = mExplorerModel.front().root;
        }
        if (only.isNull())
        {
            searchSettled();
            return;
        }
    }
    const U32                 generation = mSearchGeneration;
    const LLHandle<LLFloater> handle     = getHandle();
    for (const ExplorerObject& object : mExplorerModel)
    {
        if (!object.present || (only.notNull() && object.root != only))
        {
            continue;
        }
        for (const ExplorerPrim& prim : object.prims)
        {
            for (const ALScriptWorkspace::Item& item : prim.items)
            {
                const ALScriptRef ref(prim.id, item.id);
                if (const size_t index = indexOf(ref); index != NONE)
                {
                    searchOpen(*mDocs[index]);
                    continue;
                }
                ++mSearchPending;
                ALScriptWorkspace::instance().load(ref, [handle, generation, where = object.name](const ALScriptWorkspace::Loaded& loaded) {
                    if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                    {
                        studio->searchLoaded(generation, where, loaded);
                    }
                });
            }
        }
    }
    searchSettled();
}

void ALFloaterScriptStudio::searchLoaded(U32 generation, const std::string& where, const ALScriptWorkspace::Loaded& loaded)
{
    if (generation != mSearchGeneration)
    {
        return;
    }
    --mSearchPending;
    if (loaded.error.empty())
    {
        // A wrapped script is searched as its author wrote it.
        std::string text = loaded.text;
        if (!loaded.notecard)
        {
            if (std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(text))
            {
                text = envelope->source;
            }
        }
        searchDocument(loaded.ref, loaded.name, where, ALTextDocument(text));
    }
    searchSettled();
}

void ALFloaterScriptStudio::searchDocument(const ALScriptRef& ref, const std::string& name, const std::string& where, const ALTextDocument& text)
{
    ALTextSearchOptions options;
    options.caseSensitive = mSearchBar->valueOf("case") == "exact";
    options.wholeWord     = mSearchBar->valueOf("how") == "word";
    options.regex         = mSearchBar->valueOf("how") == "pattern";
    std::string                    error;
    const std::vector<ALTextRange> matches = ALTextSearch::matches(text, mSearchQuery, options, nullptr, &error);
    if (!error.empty())
    {
        mSearchCount->setText(getString("SearchBadPattern"));
        return;
    }
    if (matches.empty())
    {
        return;
    }
    ++mSearchFiles;
    for (const ALTextRange& match : matches)
    {
        ++mSearchHits;
        LLSD value;
        value["taskid"] = ref.object;
        value["itemid"] = ref.item;
        value["name"]   = name;
        value["line"]   = match.begin.line;
        value["column"] = match.begin.column;
        value["length"] = match.end.column - match.begin.column;
        std::string snippet = text.line(match.begin.line);
        LLStringUtil::trim(snippet);
        LLSD row;
        row["value"]                = value;
        row["columns"][0]["column"] = "where";
        row["columns"][0]["value"]  = where.empty() ? name : where + ": " + name;
        row["columns"][1]["column"] = "line";
        row["columns"][1]["value"]  = std::to_string(match.begin.line + 1);
        row["columns"][2]["column"] = "text";
        row["columns"][2]["value"]  = snippet;
        mSearchResults->addElement(row);
    }
}

void ALFloaterScriptStudio::searchSettled()
{
    LLStringUtil::format_map_t args;
    args["[HITS]"]  = std::to_string(mSearchHits);
    args["[FILES]"] = std::to_string(mSearchFiles);
    mSearchCount->setText(getString(mSearchPending > 0 ? "SearchCounting" : mSearchHits ? "SearchCount" : "SearchNone", args));
}

void ALFloaterScriptStudio::onSearchResult()
{
    const LLScrollListItem* item = mSearchResults->getFirstSelected();
    if (!item)
    {
        return;
    }
    const LLSD& value = item->getValue();
    goToPlace(ALScriptRef(value["taskid"].asUUID(), value["itemid"].asUUID()), value["name"].asString(), value["line"].asInteger(),
              value["column"].asInteger(), value["length"].asInteger());
}

void ALFloaterScriptStudio::goToPlace(const ALScriptRef& ref, const std::string& name, S32 line, S32 column, S32 length)
{
    size_t index = indexOf(ref);
    if (index == NONE)
    {
        openScript(ref, name);
        index = indexOf(ref);
        if (index == NONE)
        {
            return;
        }
    }
    else
    {
        activate(index);
    }
    Doc& doc = *mDocs[index];
    if (doc.loaded)
    {
        if (column < 0)
        {
            doc.editor->goToLine(line);
        }
        else
        {
            doc.editor->goTo(ALTextRange(ALTextPos(line, column), ALTextPos(line, column + length)));
        }
        doc.editor->setFocus(true);
    }
    else
    {
        doc.pendingLine   = line;
        doc.pendingColumn = column;
        doc.pendingLength = length;
    }
}

// --- windows ---------------------------------------------------------------------------

bool ALFloaterScriptStudio::canClose()
{
    if (mMain || mDocs.empty())
    {
        return true;
    }
    mClosingWindow = true;
    continueClosing();
    return mDocs.empty();
}

void ALFloaterScriptStudio::continueClosing()
{
    while (mClosingWindow && !mDocs.empty())
    {
        Doc& doc = *mDocs.front();
        if (doc.editor->isDirty() && doc.modifiable)
        {
            // Asked; the answer carries on from here, or stops.
            closeDocument(doc.id);
            return;
        }
        letGoOf(0);
    }
    if (mClosingWindow && mDocs.empty())
    {
        mClosingWindow = false;
        closeFloater();
    }
}

void ALFloaterScriptStudio::popOut()
{
    Doc* doc = active();
    if (!doc || !doc->loaded)
    {
        return;
    }
    // A window of its own, placed beside this one; the script opens
    // there with whatever was typed here, and goes from here without a
    // word, since nothing is lost.
    ALFloaterScriptStudio* window = LLFloaterReg::getTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD(LLUUID::generateNewID().asString()));
    if (!window)
    {
        return;
    }
    window->openFloater(window->getKey());
    LLRect rect = getRect();
    rect.translate(40, -40);
    window->setShape(rect);
    gFloaterView->adjustToFitScreen(window, false);
    const std::optional<std::string> carried = doc->editor->isDirty() ? std::optional<std::string>(doc->editor->text()) : std::nullopt;
    window->openScript(doc->ref, doc->name, carried, doc->editor->caret().line);
    window->setFocus(true);
    letGoOf(mActive);
}

// --- formatting ------------------------------------------------------------------------

void ALFloaterScriptStudio::format(Doc& doc, bool selection_only)
{
    if (!doc.loaded || !doc.modifiable || doc.notecard)
    {
        return;
    }
    const ALTextDocument& document = doc.editor->document();
    const S32             count    = document.lineCount();
    S32                   first    = 0;
    S32                   last     = count - 1;
    if (selection_only)
    {
        const ALTextRange selection = doc.editor->selection();
        const ALTextPos   from      = std::min(selection.begin, selection.end);
        const ALTextPos   to        = std::max(selection.begin, selection.end);
        first                       = from.line;
        // A selection ending at a line's start does not mean that line.
        last = to.line > from.line && to.column == 0 ? to.line - 1 : to.line;
    }
    ALScriptFormatter::Options options;
    options.lua = doc.language.lua;
    const std::string text      = document.text();
    const std::string formatted = ALScriptFormatter::formatLines(text, options, first, last);
    // Line for line, since only some lines were asked for and the
    // formatter keeps every line's number that way; each line that
    // changed is one edit, and the whole one step.
    std::vector<std::string> lines;
    {
        size_t at = 0;
        while (at <= formatted.size())
        {
            const size_t nl = formatted.find('\n', at);
            if (nl == std::string::npos)
            {
                lines.push_back(formatted.substr(at));
                break;
            }
            lines.push_back(formatted.substr(at, nl - at));
            at = nl + 1;
        }
    }
    if (static_cast<S32>(lines.size()) != count)
    {
        LL_WARNS("ScriptStudio") << "The formatter changed the line count: " << lines.size() << " for " << count << LL_ENDL;
        return;
    }
    std::vector<std::pair<ALTextRange, std::string>> edits;
    std::vector<bool>                                 gone(static_cast<size_t>(count), false);
    if (!selection_only)
    {
        // Runs of blank lines beyond a few, and every blank line at the
        // end, taken out.
        auto blank = [&lines](S32 i) { return lines[static_cast<size_t>(i)].find_first_not_of(" \t") == std::string::npos; };
        S32  run   = 0;
        for (S32 i = 0; i < count; ++i)
        {
            run = blank(i) ? run + 1 : 0;
            if (run > options.maxBlankLines && i + 1 < count)
            {
                gone[static_cast<size_t>(i)] = true;
            }
        }
        for (S32 i = count - 1; i > 0 && blank(i); --i)
        {
            // The last line is what follows the final newline; blank
            // lines before it go, and it stays as the file's end.
            if (i + 1 < count)
            {
                gone[static_cast<size_t>(i)] = true;
            }
        }
    }
    for (S32 i = first; i <= last; ++i)
    {
        const std::string& was = document.line(i);
        if (gone[static_cast<size_t>(i)])
        {
            edits.emplace_back(ALTextRange(ALTextPos(i, 0), ALTextPos(i + 1, 0)), std::string());
        }
        else if (was != lines[static_cast<size_t>(i)])
        {
            edits.emplace_back(ALTextRange(ALTextPos(i, 0), ALTextPos(i, static_cast<S32>(was.size()))), lines[static_cast<size_t>(i)]);
        }
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (edits.empty() || !doc.editor->replaceAll(std::move(edits)))
    {
        setStatus(getString("FormattedAlready", args));
        return;
    }
    setStatus(getString(selection_only ? "FormattedSelection" : "Formatted", args));
}

// --- what scripts say ---------------------------------------------------------------

void ALFloaterScriptStudio::runtimeEvent(const ALScriptWorkspace::RuntimeEvent& event)
{
    static const LLUIColor runtime_color = LLUIColorTable::instance().getColor("CodeMarkRuntime", LLColor4::magenta);
    static const LLUIColor owner_color   = LLUIColorTable::instance().getColor("ObjectChatColor", LLColor4::white);

    // The object, offered in the filter the first time it speaks.
    if (event.root.notNull() && mOutputObjects.emplace(event.root, event.objectName).second)
    {
        mOutputFilter->add(event.objectName, LLSD(event.root));
    }

    // One line of the log, or more where the script said more; the
    // script's name a link to it, at the line of a run-time error.
    ALOutputView::Entry entry;
    entry.time   = clockOf(event.time);
    entry.source = event.scriptName.empty() ? event.objectName : event.objectName + " / " + event.scriptName;
    entry.kind   = event.isError ? getString("KindError") : event.channel == ALScriptWorkspace::RuntimeEvent::Channel::OwnerSay ? getString("KindOwnerSay") : std::string();
    entry.text   = event.isError && !event.error.empty() ? event.error : event.message;
    while (!entry.text.empty() && (entry.text.back() == '\n' || entry.text.back() == '\r'))
    {
        entry.text.pop_back();
    }
    if (event.isError)
    {
        entry.color = runtime_color.get();
        if (event.line >= 0)
        {
            entry.text += llformat(" (line %d)", event.line + 1);
        }
    }
    else if (event.channel == ALScriptWorkspace::RuntimeEvent::Channel::OwnerSay)
    {
        // What the owner was told, in the colour chat shows an object's
        // words in; the debug channel's in the plain ink.
        entry.color = owner_color.get();
    }
    entry.key = event.root;
    if (event.item.notNull())
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = event.scriptName;
        args["[LINE]"] = llformat("%d", event.line + 1);
        entry.link            = true;
        entry.tooltip         = getString(event.isError && event.line >= 0 ? "OutputOpenAtLine" : "OutputOpen", args);
        entry.value["prim"]   = event.prim;
        entry.value["item"]   = event.item;
        entry.value["name"]   = event.scriptName;
        entry.value["line"]   = event.line;
        entry.value["column"] = event.column;
    }
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
        mOutput->setFilter([root](const ALOutputView::Entry& entry) { return entry.key.asUUID() == root; });
    }
}

void ALFloaterScriptStudio::onOutputChosen(const ALOutputView::Entry& entry)
{
    if (entry.value["item"].asUUID().isNull())
    {
        return;
    }
    const ALScriptRef ref(entry.value["prim"].asUUID(), entry.value["item"].asUUID());
    const S32         line   = entry.value["line"].asInteger();
    const S32         column = entry.value["column"].asInteger();
    size_t            index  = indexOf(ref);
    if (index == NONE)
    {
        // The script it names, opened; the line once it has loaded.
        openScript(ref, entry.value["name"].asString());
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
    if (!wanted || doc.notecard)
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
    // The first of the checkers' errors among the rows as the filters
    // have them; where a filter hides them all, the row is the first error
    // the store holds.
    const std::vector<LLScrollListItem*> rows = mProblems->getAllData();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const LLSD& value = rows[i]->getValue();
        if (value.isMap() && value["level"].asString() == "ERROR" && value["origin"].asString() != getString("OriginCompiler"))
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
    auto known = [this](const LLUUID& root) -> ExplorerObject* {
        for (ExplorerObject& each : mExplorerModel)
        {
            if (each.root == root)
            {
                return &each;
            }
        }
        return nullptr;
    };
    auto add = [this, known](LLViewerObject* object) -> ExplorerObject* {
        if (!object || object->isAvatar())
        {
            return nullptr;
        }
        LLViewerObject* root = object->getRootEdit() ? object->getRootEdit() : object;
        if (ExplorerObject* already = known(root->getID()))
        {
            return already;
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
        return &mExplorerModel.back();
    };
    // Pinned first, so that they keep their place; one that is not
    // around is listed by name. Then what is selected, then the objects
    // of the scripts open.
    for (const Pinned& pin : mPinned)
    {
        if (ExplorerObject* object = add(gObjectList.findObject(pin.root)))
        {
            object->pinned = true;
        }
        else if (!known(pin.root))
        {
            ExplorerObject away;
            away.root    = pin.root;
            away.name    = pin.name.empty() ? getString("ObjectUnnamed") : pin.name;
            away.pinned  = true;
            away.present = false;
            mExplorerModel.push_back(std::move(away));
        }
    }
    for (const LLUUID& root : mExplorerRoots)
    {
        add(gObjectList.findObject(root));
    }
    for (const std::unique_ptr<Doc>& doc : mDocs)
    {
        if (!doc->ref.inInventory())
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
                    if (object.pinned)
                    {
                        // The name a pin is remembered by is the object's
                        // latest.
                        for (Pinned& pin : mPinned)
                        {
                            if (pin.root == object.root && pin.name != object.name)
                            {
                                pin.name = object.name;
                                saveState();
                            }
                        }
                    }
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
            // A new item, waited for: opened now that it is listed.
            if (mOpenWhenListedPrim == prim.id)
            {
                for (const ALScriptWorkspace::Item& item : prim.items)
                {
                    if (mOpenWhenListedItem.notNull() ? item.id == mOpenWhenListedItem : item.name == mOpenWhenListedName)
                    {
                        mOpenWhenListedPrim.setNull();
                        mOpenWhenListedItem.setNull();
                        mOpenWhenListedName.clear();
                        openScript(ALScriptRef(prim.id, item.id), item.name);
                        break;
                    }
                }
            }
            return;
        }
    }
}

void ALFloaterScriptStudio::fillExplorer()
{
    // What was chosen stays chosen, by what it stands for rather than
    // where it sat.
    std::vector<ExplorerRow> chosen = explorerChoice();
    const S32                scroll = mExplorer->getScrollPos();
    mExplorer->deleteAllItems();
    auto row = [&](const LLSD& value, const char* image, const std::string& name, const std::string& kind, const std::string& run, const std::string& tip) {
        LLSD r;
        r["value"]                  = value;
        r["columns"][0]["column"]   = "icon";
        r["columns"][0]["type"]     = "icon";
        r["columns"][0]["value"]    = image;
        r["columns"][0]["tool_tip"] = tip;
        for (S32 i = 1; i < 4; ++i)
        {
            r["columns"][i]["column"]   = i == 1 ? "name" : i == 2 ? "kind" : "run";
            r["columns"][i]["value"]    = i == 1 ? name : i == 2 ? kind : run;
            r["columns"][i]["tool_tip"] = tip;
        }
        return mExplorer->addElement(r);
    };
    auto wasChosen = [&chosen](const LLSD& value) {
        const LLUUID root = value["root"].asUUID();
        const LLUUID prim = value.has("prim") ? value["prim"].asUUID() : root;
        const LLUUID item = value["item"].asUUID();
        for (const ExplorerRow& each : chosen)
        {
            if (each.root == root && each.prim == prim && each.item == item)
            {
                return true;
            }
        }
        return false;
    };
    for (const ExplorerObject& object : mExplorerModel)
    {
        LLSD at;
        at["root"] = object.root;
        const std::string pin  = object.pinned ? getString("PinnedMark") : LLStringUtil::null;
        const bool        many = object.prims.size() > 1;
        LLStringUtil::format_map_t args;
        args["[NAME]"]   = object.name;
        args["[OBJECT]"] = object.name;
        args["[COUNT]"]  = std::to_string(object.prims.size());
        LLScrollListItem* line = row(at, many ? "Inv_Object_Multi" : "Inv_Object", pin + object.name,
                                     getString(!object.present ? "KindAway" : many ? "KindLinkset" : "KindObject"), LLStringUtil::null,
                                     getString(object.present ? "RowObjectTip" : "RowAwayTip", args));
        line->setSelected(wasChosen(at));
        const std::string indent = many ? "        " : "    ";
        for (const ExplorerPrim& prim : object.prims)
        {
            if (many)
            {
                at["prim"]     = prim.id;
                args["[NAME]"] = prim.name.empty() ? getString("ObjectUnnamed") : prim.name;
                line = row(at, "Studio_Prim", "    " + (prim.name.empty() ? getString("ObjectUnnamed") : prim.name), getString("KindPrim"), LLStringUtil::null,
                           getString("RowPrimTip", args));
                line->setSelected(wasChosen(at));
            }
            for (const ALScriptWorkspace::Item& item : prim.items)
            {
                LLSD value;
                value["root"]   = object.root;
                value["prim"]   = prim.id;
                value["item"]   = item.id;
                value["name"]   = item.name;
                value["script"] = item.script;
                value["lua"]    = item.lua;
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
                    run = getString(state < 0 ? "StateUnknown" : state ? "RunningYes" : "RunningNo");
                }
                const std::string kind = getString(item.script ? (item.lua ? "KindLua" : "KindScript") : "KindNotecard");
                args["[NAME]"]         = item.name;
                args["[KIND]"]         = kind;
                args["[STATE]"]        = run;
                const char* image = item.script ? (item.lua ? "Inv_Script_Luau" : "Inv_Script") : item.name == ".luaurc" || item.name == ".lslrc" ? "Studio_Config" : "Inv_Notecard";
                line = row(value, image, indent + item.name, kind, run, getString(item.script ? "RowScriptTip" : "RowNotecardTip", args));
                line->setSelected(wasChosen(value));
            }
        }
    }
    mExplorer->setScrollPos(scroll);
}

std::vector<ALFloaterScriptStudio::ExplorerRow> ALFloaterScriptStudio::explorerChoice() const
{
    std::vector<ExplorerRow> rows;
    for (const LLScrollListItem* item : mExplorer->getAllSelected())
    {
        const LLSD& value = item->getValue();
        if (!value.isMap())
        {
            continue;
        }
        ExplorerRow row;
        row.root   = value["root"].asUUID();
        row.prim   = value["prim"].asUUID();
        row.item   = value["item"].asUUID();
        row.name   = value["name"].asString();
        row.script = value["script"].asBoolean();
        row.lua    = value["lua"].asBoolean();
        if (row.prim.isNull())
        {
            row.prim = row.root;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<std::pair<LLUUID, std::string>> ALFloaterScriptStudio::containerPrims(const std::vector<ExplorerRow>& rows) const
{
    std::vector<std::pair<LLUUID, std::string>> prims;
    auto                                        take = [&prims](const LLUUID& id, const std::string& name) {
        for (const auto& known : prims)
        {
            if (known.first == id)
            {
                return;
            }
        }
        prims.emplace_back(id, name);
    };
    for (const ExplorerRow& row : rows)
    {
        if (row.isItem())
        {
            continue;
        }
        for (const ExplorerObject& object : mExplorerModel)
        {
            if (object.root != row.root || !object.present)
            {
                continue;
            }
            for (const ExplorerPrim& prim : object.prims)
            {
                if (row.prim == row.root || prim.id == row.prim)
                {
                    take(prim.id, prim.name.empty() ? object.name : prim.name);
                }
            }
        }
    }
    return prims;
}

void ALFloaterScriptStudio::onExplorerChosen()
{
    for (const ExplorerRow& row : explorerChoice())
    {
        if (row.isItem())
        {
            openScript(row.ref(), row.name);
        }
    }
}

bool ALFloaterScriptStudio::explorerActionEnabled(const std::string& action) const
{
    const std::vector<ExplorerRow> rows = explorerChoice();
    auto                           any  = [&rows](auto test) {
        for (const ExplorerRow& row : rows)
        {
            if (test(row))
            {
                return true;
            }
        }
        return false;
    };
    auto present = [this](const ExplorerRow& row) {
        for (const ExplorerObject& object : mExplorerModel)
        {
            if (object.root == row.root)
            {
                return object.present;
            }
        }
        return false;
    };
    if (action == "refresh")
    {
        return true;
    }
    if (action == "copy")
    {
        return mExplorer->canCopy();
    }
    if (action == "open")
    {
        return any([](const ExplorerRow& row) { return row.isItem(); });
    }
    if (action == "new_lsl" || action == "new_lua" || action == "new_notecard")
    {
        // One prim to put it in.
        return rows.size() == 1 && present(rows.front()) && (action != "new_lua" || luaEnabledFor(ALScriptRef(rows.front().prim, LLUUID::null)));
    }
    if (action == "rename")
    {
        return rows.size() == 1 && rows.front().isItem();
    }
    if (action == "delete")
    {
        return !rows.empty() && !any([](const ExplorerRow& row) { return !row.isItem(); });
    }
    if (action == "start" || action == "stop" || action == "reset" || action == "restart" || action == "recompile")
    {
        // Scripts, or whole prims and objects, which the queues walk;
        // restart is one script at a time.
        return any([&](const ExplorerRow& row) { return present(row) && (row.script || (action != "restart" && !row.isItem())); });
    }
    if (action == "teleport" || action == "zoom")
    {
        return rows.size() == 1 && present(rows.front());
    }
    if (action == "pin")
    {
        return !rows.empty();
    }
    return false;
}

void ALFloaterScriptStudio::onExplorerAction(const std::string& action)
{
    if (action == "refresh")
    {
        mRunningKnown.clear();
        refreshExplorer();
        return;
    }
    if (action == "copy")
    {
        mExplorer->copy();
        return;
    }
    const std::vector<ExplorerRow> rows = explorerChoice();
    if (rows.empty())
    {
        return;
    }
    ALScriptWorkspace& workspace = ALScriptWorkspace::instance();
    if (action == "open")
    {
        onExplorerChosen();
    }
    else if (action == "new_lsl" || action == "new_lua" || action == "new_notecard")
    {
        explorerCreate(rows.front().prim, action == "new_notecard", action == "new_lua");
    }
    else if (action == "rename")
    {
        if (rows.front().isItem())
        {
            explorerRename(rows.front());
        }
    }
    else if (action == "delete")
    {
        explorerDelete(rows);
    }
    else if (action == "recompile")
    {
        explorerRecompile(rows);
    }
    else if (action == "start" || action == "stop" || action == "reset" || action == "restart")
    {
        // Each script chosen on its own; the prims and objects chosen
        // through a queue.
        for (const ExplorerRow& row : rows)
        {
            if (!row.script)
            {
                continue;
            }
            const ALScriptRef ref = row.ref();
            if (action == "reset")
            {
                workspace.reset(ref);
            }
            else if (action == "restart")
            {
                // Stopped and set running again, keeping its state.
                if (workspace.setRunning(ref, false) && workspace.setRunning(ref, true))
                {
                    workspace.askRunning(ref);
                }
            }
            else if (workspace.setRunning(ref, action == "start"))
            {
                workspace.askRunning(ref);
            }
        }
        if (action != "restart")
        {
            const std::vector<std::pair<LLUUID, std::string>> prims = containerPrims(rows);
            if (!prims.empty())
            {
                const auto  kind = action == "start" ? ALScriptWorkspace::Queue::Start : action == "stop" ? ALScriptWorkspace::Queue::Stop : ALScriptWorkspace::Queue::Reset;
                std::string error;
                if (!workspace.queue(kind, prims, LLStringUtil::null, error))
                {
                    setStatus(error, true);
                }
            }
        }
    }
    else if (action == "teleport" || action == "zoom")
    {
        if (LLViewerObject* object = gObjectList.findObject(rows.front().root))
        {
            if (action == "teleport")
            {
                gAgent.teleportViaLocation(object->getPositionGlobal());
            }
            else
            {
                handle_zoom_to_object(object->getID());
            }
        }
    }
    else if (action == "pin")
    {
        // Every object among the rows, pinned if the first is not, else
        // let go.
        const bool          pinning = !isPinned(rows.front().root);
        std::vector<LLUUID> done;
        for (const ExplorerRow& row : rows)
        {
            if (std::find(done.begin(), done.end(), row.root) != done.end() || isPinned(row.root) == pinning)
            {
                continue;
            }
            done.push_back(row.root);
            std::string name;
            for (const ExplorerObject& object : mExplorerModel)
            {
                if (object.root == row.root)
                {
                    name = object.name;
                }
            }
            togglePinned(row.root, name);
        }
        saveState();
        refreshExplorer();
    }
}

void ALFloaterScriptStudio::showExplorerMenu(S32 x, S32 y)
{
    if (!LLMenuGL::sMenuContainer)
    {
        return;
    }
    // The row under the mouse is the choice, unless it is among what
    // was chosen already; the empty part of the list chooses nothing,
    // and the menu offers what needs nothing.
    LLScrollListItem* hit = mExplorer->hitItem(x, y);
    if (hit && !hit->getSelected())
    {
        mExplorer->selectItemAt(x, y, MASK_NONE);
    }
    else if (!hit)
    {
        mExplorer->deselectAllItems();
    }
    if (LLContextMenu* old = mExplorerMenuHandle.get())
    {
        old->die();
        mExplorerMenuHandle.markDead();
    }
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
    commit.add("Explorer.Action", [this](LLUICtrl*, const LLSD& param) { onExplorerAction(param.asString()); });
    enable.add("Explorer.Enable", [this](LLUICtrl*, const LLSD& param) { return explorerActionEnabled(param.asString()); });
    enable.add("Explorer.Check", [this](LLUICtrl*, const LLSD& param) {
        const std::vector<ExplorerRow> rows = explorerChoice();
        return param.asString() == "pin" && !rows.empty() && isPinned(rows.front().root);
    });
    LLContextMenu* menu = LLUICtrlFactory::createFromFile<LLContextMenu>("menu_script_studio_explorer.xml", LLMenuGL::sMenuContainer,
                                                                          LLMenuHolderGL::child_registry_t::instance());
    if (!menu)
    {
        return;
    }
    mExplorerMenuHandle = menu->getHandle();
    menu->show(x, y);
    LLMenuGL::showPopup(mExplorer, menu, x, y);
}

void ALFloaterScriptStudio::explorerCreate(const LLUUID& prim, bool notecard, bool lua)
{
    LLSD args;
    args["KIND"] = getString(notecard ? "NewKindNotecard" : lua ? "NewKindLua" : "NewKindScript");
    args["NAME"] = getString(notecard ? "NewNotecardName" : "NewScriptName");
    const LLHandle<LLFloater> handle = getHandle();
    LLNotificationsUtil::add("ScriptStudioNewItem", args, LLSD(), [handle, prim, notecard, lua](const LLSD& notification, const LLSD& response) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        if (!studio || LLNotificationsUtil::getSelectedOption(notification, response) != 0)
        {
            return;
        }
        std::string name = response["name"].asString();
        LLStringUtil::trim(name);
        if (name.empty())
        {
            return;
        }
        std::string error;
        const bool  asked = ALScriptWorkspace::instance().create(prim, notecard, lua, name, [handle](const ALScriptWorkspace::Created& made) {
            if (ALFloaterScriptStudio* again = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                again->explorerCreated(made);
            }
        }, error);
        if (!asked)
        {
            studio->setStatus(error, true);
        }
    });
}

void ALFloaterScriptStudio::explorerCreated(const ALScriptWorkspace::Created& made)
{
    if (!made.error.empty())
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"]  = made.name;
        args["[ERROR]"] = made.error;
        setStatus(getString("CreateFailed", args), true);
        return;
    }
    // Opened once the prim lists it: by id where the region said, by
    // name otherwise.
    mOpenWhenListedPrim = made.prim;
    mOpenWhenListedItem = made.item;
    mOpenWhenListedName = made.name;
    refreshExplorer();
}

void ALFloaterScriptStudio::explorerRename(const ExplorerRow& row)
{
    LLSD args;
    args["NAME"] = row.name;
    const LLHandle<LLFloater> handle = getHandle();
    const ALScriptRef         ref    = row.ref();
    LLNotificationsUtil::add("ScriptStudioRenameItem", args, LLSD(), [handle, ref, was = row.name](const LLSD& notification, const LLSD& response) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        if (!studio || LLNotificationsUtil::getSelectedOption(notification, response) != 0)
        {
            return;
        }
        std::string name = response["name"].asString();
        LLStringUtil::trim(name);
        if (name.empty() || name == was)
        {
            return;
        }
        std::string error;
        if (!ALScriptWorkspace::instance().rename(ref, name, error))
        {
            studio->setStatus(error, true);
            return;
        }
        // The tab, if it is open, and the list.
        if (const size_t index = studio->indexOf(ref); index != NONE)
        {
            studio->mDocs[index]->name = name;
            studio->fillTabs();
        }
        studio->refreshExplorer();
    });
}

void ALFloaterScriptStudio::explorerDelete(const std::vector<ExplorerRow>& rows)
{
    std::vector<ExplorerRow> items;
    for (const ExplorerRow& row : rows)
    {
        if (row.isItem())
        {
            items.push_back(row);
        }
    }
    if (items.empty())
    {
        return;
    }
    LLSD args;
    args["COUNT"] = static_cast<S32>(items.size());
    args["NAME"]  = items.front().name;
    const LLHandle<LLFloater> handle = getHandle();
    LLNotificationsUtil::add(items.size() == 1 ? "ScriptStudioDeleteItem" : "ScriptStudioDeleteItems", args, LLSD(),
                             [handle, items](const LLSD& notification, const LLSD& response) {
                                 ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
                                 if (!studio || LLNotificationsUtil::getSelectedOption(notification, response) != 0)
                                 {
                                     return;
                                 }
                                 for (const ExplorerRow& row : items)
                                 {
                                     const ALScriptRef ref = row.ref();
                                     std::string       error;
                                     if (!ALScriptWorkspace::instance().remove(ref, error))
                                     {
                                         studio->setStatus(error, true);
                                         continue;
                                     }
                                     // Its tab goes with it, whatever was typed there.
                                     if (const size_t index = studio->indexOf(ref); index != NONE)
                                     {
                                         studio->letGoOf(index);
                                     }
                                 }
                                 studio->refreshExplorer();
                             });
}

void ALFloaterScriptStudio::explorerRecompile(const std::vector<ExplorerRow>& rows)
{
    // Each script chosen goes up again on its own, for what it compiles
    // for now; a prim or an object chosen has every script walked by the
    // compile queue, which reports in a window of its own.
    const LLHandle<LLFloater> handle = getHandle();
    for (const ExplorerRow& row : rows)
    {
        if (!row.script)
        {
            continue;
        }
        const ALScriptRef ref  = row.ref();
        const std::string name = row.name;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = name;
        setStatus(getString("Recompiling", args));
        ALScriptWorkspace::instance().recompile(ref, "auto", [handle, ref, name](const ALScriptWorkspace::CompileResult& result) {
            ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
            if (!studio || studio->indexOf(ref) != NONE)
            {
                // An open script hears of it through the listener, and
                // shows what the compiler said.
                return;
            }
            LLStringUtil::format_map_t args;
            args["[NAME]"] = name;
            if (!result.error.empty())
            {
                args["[ERROR]"] = result.error;
                studio->setStatus(studio->getString("SaveFailed", args), true);
            }
            else if (result.success)
            {
                studio->setStatus(studio->getString("Compiled", args));
            }
            else
            {
                args["[COUNT]"] = std::to_string(result.diagnostics.size());
                studio->setStatus(studio->getString("CompileFailed", args), true);
            }
        });
    }
    const std::vector<std::pair<LLUUID, std::string>> prims = containerPrims(rows);
    if (!prims.empty())
    {
        std::string error;
        if (!ALScriptWorkspace::instance().queue(ALScriptWorkspace::Queue::Recompile, prims, "auto", error))
        {
            setStatus(error, true);
        }
    }
}

void ALFloaterScriptStudio::exploreObject(const LLUUID& root)
{
    // Pinned, so that it stays listed once it is no longer selected in
    // world; in sight; and chosen, so that the buttons act on it.
    if (!isPinned(root))
    {
        togglePinned(root, objectNameOf(gObjectList.findObject(root), getString("ObjectUnnamed")));
        saveState();
    }
    mFolds.setCollapsed("explorer", false);
    refreshExplorer();
    mExplorer->deselectAllItems();
    for (LLScrollListItem* item : mExplorer->getAllData())
    {
        const LLSD& value = item->getValue();
        if (value.isMap() && !value.has("prim") && !value.has("item") && value["root"].asUUID() == root)
        {
            item->setSelected(true);
            break;
        }
    }
    mExplorer->scrollToShowSelected();
    onExplorerChosen();
}

bool ALFloaterScriptStudio::isPinned(const LLUUID& root) const
{
    for (const Pinned& pin : mPinned)
    {
        if (pin.root == root)
        {
            return true;
        }
    }
    return false;
}

void ALFloaterScriptStudio::togglePinned(const LLUUID& root, const std::string& name)
{
    const auto found = std::find_if(mPinned.begin(), mPinned.end(), [&root](const Pinned& pin) { return pin.root == root; });
    if (found != mPinned.end())
    {
        mPinned.erase(found);
    }
    else
    {
        mPinned.push_back(Pinned{ root, name });
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
            if (mClosingWindow)
            {
                continueClosing();
            }
            break;
        default:  // cancel
            mClosingWindow = false;
            break;
    }
}

void ALFloaterScriptStudio::letGoOf(size_t index)
{
    if (index >= mDocs.size())
    {
        return;
    }
    {
        Doc& doc = *mDocs[index];
        stopExternal(doc);
        mProblemStore.forget(doc.id);
        doc.changed.release();
        mEditorHost->removeChild(doc.editor);
        doc.editor->die();
        if (doc.expandedEditor)
        {
            mEditorHost->removeChild(doc.expandedEditor);
            doc.expandedEditor->die();
        }
        mDocs.erase(mDocs.begin() + index);
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
    else if (action == "external_editor")
    {
        if (doc)
        {
            editExternally(*doc);
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
    else if (action == "open_file")
    {
        openFileFromDisk();
    }
    else if (action == "save_file")
    {
        saveToFile();
    }
    else if (action == "save_as")
    {
        saveFileAs();
    }
    else if (action == "clear_recent")
    {
        mRecentFiles.clear();
        fillRecentMenu();
        saveState();
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
    else if (action == "preferences")
    {
        LLFloaterReg::showInstance("script_studio_prefs");
    }
    else if (doc && action == "reference")
    {
        reference(*doc);
    }
    else if (action == "browse_reference")
    {
        browseReference();
    }
    else if (doc && action == "wiki")
    {
        LLWeb::loadURL(helpUrl(doc->language.lua, doc->editor->document().text(doc->editor->identifierAtCaret())));
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
    else if (action == "find_in_files")
    {
        findInFiles();
    }
    else if (action == "expanded")
    {
        toggleExpanded();
    }
    else if (action == "pop_out")
    {
        popOut();
    }
    else if (action == "next_tab" || action == "previous_tab")
    {
        cycleTab(action == "next_tab" ? 1 : -1);
    }
    else if (action == "indent_guides")
    {
        mIndentGuides = !mIndentGuides;
        applyEditorOptions();
    }
    else if (action == "relative_numbers")
    {
        mRelativeNumbers = !mRelativeNumbers;
        applyEditorOptions();
    }
    else if (action == "rainbow_brackets")
    {
        mRainbowBrackets = !mRainbowBrackets;
        applyEditorOptions();
    }
    else if (action == "sticky_headers")
    {
        mStickyHeaders = !mStickyHeaders;
        applyEditorOptions();
    }
    else if (action == "spell_check")
    {
        mSpellCheck = !mSpellCheck;
        applyEditorOptions();
    }
    else if (action == "vim_mode")
    {
        mVimMode = !mVimMode;
        applyEditorOptions();
        mVimBanner.clear();
        if (Doc* each = active())
        {
            refreshTrailer(*each);
        }
        saveState();
    }
    else if (action == "semantic_colors" || action == "inlay_parameters" || action == "inlay_types")
    {
        bool& flag = action == "semantic_colors" ? mSemanticColors : action == "inlay_parameters" ? mInlayParameters : mInlayTypes;
        flag       = !flag;
        // Off at once; on with the next check, which is now.
        for (std::unique_ptr<Doc>& each : mDocs)
        {
            if (!mSemanticColors)
            {
                each->editor->setSemanticTokens({});
            }
            if (!mInlayParameters && !mInlayTypes)
            {
                each->editor->setInlayHints({});
            }
            scheduleAnalysis(*each, true);
        }
        saveState();
    }
    else if (doc && (action == "format" || action == "format_selection"))
    {
        format(*doc, action == "format_selection");
    }
    else if (action == "insert_snippet" || action == "insert_function" || action == "insert_event" || action == "insert_constant")
    {
        insertFromLibrary(action.substr(7));
    }
    else if (action == "problems" || action == "references" || action == "output" || action == "search")
    {
        // The tab, shown; or the pane folded when it is the tab showing.
        const char* tab = action == "problems" ? "problems_tab" : action == "references" ? "references_tab" : action == "output" ? "output_tab" : "search_tab";
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
        if (Doc* doc = active(); doc && doc->loaded && !doc->notecard)
        {
            preprocess(*doc, false);
        }
    }
    else if (action == "preproc_enabled" || action == "preproc_switch" || action == "preproc_lazy" || action == "preproc_compress" ||
             action == "preproc_disk" || action == "preproc_optimize" || action == "preproc_shrink" || action == "preproc_addstrings" ||
             action == "preproc_inline" || action == "preproc_extensions")
    {
        const char* setting = action == "preproc_enabled"    ? "ALScriptPreprocEnabled"
                              : action == "preproc_switch"   ? "ALScriptPreprocSwitch"
                              : action == "preproc_lazy"     ? "ALScriptPreprocLazyLists"
                              : action == "preproc_compress" ? "ALScriptPreprocCompress"
                              : action == "preproc_optimize" ? "ALScriptPreprocOptimizer"
                              : action == "preproc_shrink"   ? "ALScriptPreprocOptimizerShrinkNames"
                              : action == "preproc_addstrings" ? "ALScriptPreprocOptimizerAddStrings"
                              : action == "preproc_inline"     ? "ALScriptPreprocOptimizerInlining"
                              : action == "preproc_extensions" ? "ALScriptPreprocExtensions"
                                                               : "ALScriptPreprocDiskIncludes";
        gSavedSettings.setBOOL(setting, !gSavedSettings.getBOOL(setting));
        // What the analyzers see changes with the setting, and what the
        // editors colour as the transforms' words.
        const bool words = action == "preproc_switch" || action == "preproc_extensions";
        for (std::unique_ptr<Doc>& doc : mDocs)
        {
            doc->expanded.valid = false;
            if (words && !doc->notecard && !doc->language.lua)
            {
                teachWords(*doc->editor, false);
            }
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
    if (action == "save_as")
    {
        return doc && doc->loaded && !doc->file.empty();
    }
    if (action == "external_editor")
    {
        return doc && doc->loaded && doc->modifiable && !doc->notecard;
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
        return doc && doc->loaded && !doc->notecard && !doc->preprocessing;
    }
    if (action == "load_file" || action == "toggle_comment" || action == "complete")
    {
        return doc && doc->modifiable;
    }
    if (action == "reference" || action == "wiki")
    {
        return doc && doc->loaded && !doc->notecard;
    }
    if (action == "expanded")
    {
        return doc && doc->expandedEditor != nullptr;
    }
    if (action == "pop_out")
    {
        return doc && doc->loaded;
    }
    if (action == "format" || action == "insert_snippet" || action == "insert_function" || action == "insert_event" || action == "insert_constant")
    {
        return doc && doc->loaded && doc->modifiable && !doc->notecard;
    }
    if (action == "format_selection")
    {
        return doc && doc->loaded && doc->modifiable && !doc->notecard && !doc->editor->selection().empty();
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
    if (action == "expanded")
    {
        const Doc* doc = active();
        return doc && doc->showingExpanded && doc->expandedEditor;
    }
    if (action == "word_wrap")
    {
        return mWordWrap;
    }
    if (action == "indent_guides")
    {
        return mIndentGuides;
    }
    if (action == "relative_numbers")
    {
        return mRelativeNumbers;
    }
    if (action == "rainbow_brackets")
    {
        return mRainbowBrackets;
    }
    if (action == "sticky_headers")
    {
        return mStickyHeaders;
    }
    if (action == "vim_mode")
    {
        return mVimMode;
    }
    if (action == "spell_check")
    {
        return mSpellCheck;
    }
    if (action == "semantic_colors")
    {
        return mSemanticColors;
    }
    if (action == "inlay_parameters")
    {
        return mInlayParameters;
    }
    if (action == "inlay_types")
    {
        return mInlayTypes;
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
    if (action == "problems" || action == "references" || action == "output" || action == "search")
    {
        const LLPanel* current = mBottomTabs->getCurrentPanel();
        const char*    tab     = action == "problems" ? "problems_tab" : action == "references" ? "references_tab" : action == "output" ? "output_tab" : "search_tab";
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
    if (action == "preproc_inline")
    {
        return gSavedSettings.getBOOL("ALScriptPreprocOptimizerInlining");
    }
    if (action == "preproc_extensions")
    {
        return gSavedSettings.getBOOL("ALScriptPreprocExtensions");
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

void ALFloaterScriptStudio::openFileFromDisk()
{
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                for (const std::string& file : files)
                {
                    studio->openFile(file, false);
                }
            }
        },
        LLFilePicker::FFLOAD_SCRIPT, true);
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

void ALFloaterScriptStudio::saveFileAs()
{
    Doc* doc = active();
    if (!doc || doc->file.empty())
    {
        return;
    }
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChosenToSaveAs(files);
            }
        },
        LLFilePicker::FFSAVE_SCRIPT, doc->name);
}

void ALFloaterScriptStudio::fileChosenToSaveAs(const std::vector<std::string>& files)
{
    Doc* doc = active();
    if (!doc || doc->file.empty() || files.empty())
    {
        return;
    }
    const std::string path = files.front();
    if (path == doc->file)
    {
        saveFile(*doc);
        return;
    }
    LLStringUtil::format_map_t args;
    args["[PATH]"] = path;
    if (indexOf("disk:" + path) != NONE)
    {
        // Open in another tab already: that tab is the file, not this one.
        args["[FILE]"] = path;
        setStatus(getString("FileOpenElsewhere", args), true);
        return;
    }
    std::ofstream out(path, std::ios::binary);
    out << doc->editor->text();
    if (!out.good())
    {
        setStatus(getString("SaveToFileFailed", args), true);
        return;
    }
    // The tab is the new file from here on: keyed by it, named after
    // it, watched for changes to it, its problems its own, and in the
    // language its name says.
    mProblemStore.forget(doc->id);
    doc->liveFile.reset();
    doc->file = path;
    doc->id   = "disk:" + path;
    doc->name = gDirUtilp->getBaseFileName(path);
    doc->editor->setName("editor_" + doc->id);
    if (doc->expandedEditor)
    {
        doc->expandedEditor->setName("editor_" + doc->id + ":expanded");
    }
    if (const FileLanguage said = languageOfFile(path, false); said.said)
    {
        speakFileLanguage(*doc, said);
    }
    watchFile(*doc);
    noteRecentFile(path);
    setStatus(getString("SavedToFile", args));
    fileSettled(*doc);
    scheduleAnalysis(*doc, true);
}

void ALFloaterScriptStudio::noteRecentFile(const std::string& path)
{
    const size_t MOST = 10;
    mRecentFiles.erase(std::remove(mRecentFiles.begin(), mRecentFiles.end(), path), mRecentFiles.end());
    mRecentFiles.insert(mRecentFiles.begin(), path);
    if (mRecentFiles.size() > MOST)
    {
        mRecentFiles.resize(MOST);
    }
    fillRecentMenu();
    saveState();
}

void ALFloaterScriptStudio::fillRecentMenu()
{
    LLMenuGL* menu = menuBar() ? menuBar()->findChild<LLMenuGL>("open_recent") : nullptr;
    if (!menu)
    {
        return;
    }
    menu->empty();
    if (mRecentFiles.empty())
    {
        LLMenuItemCallGL::Params none;
        none.name  = "no_recent";
        none.label = getString("NoRecentFiles");
        LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(none);
        item->setEnabled(false);
        menu->addChild(item);
        return;
    }
    // Each file by its name, the folder after it where two share a name;
    // the callback bound here rather than looked up by name, since the
    // registry answers for whichever studio registered last.
    const LLHandle<LLFloater> handle = getHandle();
    for (const std::string& path : mRecentFiles)
    {
        const std::string name  = gDirUtilp->getBaseFileName(path);
        const bool        twice = std::count_if(mRecentFiles.begin(), mRecentFiles.end(),
                                                [&name](const std::string& other) { return gDirUtilp->getBaseFileName(other) == name; }) > 1;
        LLMenuItemCallGL::Params p;
        p.name  = "recent_" + path;
        p.label = twice ? name + "  (" + gDirUtilp->getDirName(path) + ")" : name;
        LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
        item->setClickCallback([handle, path](LLUICtrl*, const LLSD&) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->openFile(path, false);
            }
        });
        menu->addChild(item);
    }
    LLMenuItemSeparatorGL::Params sep;
    menu->addChild(LLUICtrlFactory::create<LLMenuItemSeparatorGL>(sep));
    LLMenuItemCallGL::Params clear;
    clear.name  = "clear_recent";
    clear.label = getString("ClearRecentFiles");
    LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(clear);
    item->setClickCallback([handle](LLUICtrl*, const LLSD&) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->onMenuAction(LLSD("clear_recent"));
        }
    });
    menu->addChild(item);
}

// --- state ---------------------------------------------------------------------

void ALFloaterScriptStudio::writeState(LLSD& state) const
{
    state["word_wrap"]    = mWordWrap;
    state["line_numbers"] = mLineNumbers;
    state["indent_guides"]    = mIndentGuides;
    state["relative_numbers"] = mRelativeNumbers;
    state["rainbow_brackets"] = mRainbowBrackets;
    state["sticky_headers"]   = mStickyHeaders;
    state["vim_mode"]         = mVimMode;
    state["spell_check"]      = mSpellCheck;
    state["semantic_colors"]  = mSemanticColors;
    state["inlay_parameters"] = mInlayParameters;
    state["inlay_types"]      = mInlayTypes;
    state["scroll_map"]   = mScrollMap;
    state["map_width"]    = mScrollMapWidth;
    state["map_preview"]  = mScrollMapPreview;
    state["map_left"]     = mScrollMapLeft;
    LLSD pinned = LLSD::emptyArray();
    for (const Pinned& pin : mPinned)
    {
        LLSD one;
        one["id"]   = pin.root;
        one["name"] = pin.name;
        pinned.append(one);
    }
    state["pinned"] = pinned;
    LLSD recent = LLSD::emptyArray();
    for (const std::string& path : mRecentFiles)
    {
        recent.append(path);
    }
    state["recent_files"] = recent;
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
    if (state.has("indent_guides"))
    {
        mIndentGuides = state["indent_guides"].asBoolean();
    }
    if (state.has("relative_numbers"))
    {
        mRelativeNumbers = state["relative_numbers"].asBoolean();
    }
    if (state.has("rainbow_brackets"))
    {
        mRainbowBrackets = state["rainbow_brackets"].asBoolean();
    }
    if (state.has("sticky_headers"))
    {
        mStickyHeaders = state["sticky_headers"].asBoolean();
    }
    if (state.has("vim_mode"))
    {
        mVimMode = state["vim_mode"].asBoolean();
    }
    if (state.has("spell_check"))
    {
        mSpellCheck = state["spell_check"].asBoolean();
    }
    if (state.has("semantic_colors"))
    {
        mSemanticColors = state["semantic_colors"].asBoolean();
    }
    if (state.has("inlay_parameters"))
    {
        mInlayParameters = state["inlay_parameters"].asBoolean();
    }
    if (state.has("inlay_types"))
    {
        mInlayTypes = state["inlay_types"].asBoolean();
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
    if (state.has("pinned"))
    {
        mPinned.clear();
        for (LLSD::array_const_iterator it = state["pinned"].beginArray(); it != state["pinned"].endArray(); ++it)
        {
            const LLUUID id = (*it)["id"].asUUID();
            if (id.notNull() && !isPinned(id))
            {
                mPinned.push_back(Pinned{ id, (*it)["name"].asString() });
            }
        }
    }
    if (state.has("recent_files"))
    {
        mRecentFiles.clear();
        for (LLSD::array_const_iterator it = state["recent_files"].beginArray(); it != state["recent_files"].endArray(); ++it)
        {
            const std::string path = it->asString();
            if (!path.empty() && std::find(mRecentFiles.begin(), mRecentFiles.end(), path) == mRecentFiles.end())
            {
                mRecentFiles.push_back(path);
            }
        }
    }
    fillRecentMenu();
}
