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
#include "altabstrip.h"
#include "llagent.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llfilepicker.h"
#include "llfloaterreg.h"
#include "lllayoutstack.h"
#include "llmenugl.h"
#include "llnotificationsutil.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lltrans.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llviewermenufile.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"

#include <fstream>

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
    mFolds.bind(this, { { "problems", "bottom_panel", "fold_problems", getString("PaneProblems") } });
    mFolds.onChanged([this]() { saveState(); });

    mEditorHost    = getChild<LLPanel>("editor_panel");
    mTabs          = getChild<ALTabStrip>("tabs");
    mProblems      = getChild<LLScrollListCtrl>("problems");
    mCompileTarget = getChild<LLComboBox>("compile_target");
    mRunning       = getChild<LLCheckBoxCtrl>("running");
    mResetButton   = getChild<LLButton>("reset_btn");
    mSaveButton    = getChild<LLButton>("save_btn");

    mTabs->onChosen(boost::bind(&ALFloaterScriptStudio::onTabChosen, this, _1));
    mTabs->onClosed(boost::bind(&ALFloaterScriptStudio::closeDocument, this, _1));
    mProblems->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onProblemSelected, this));
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
        if (mDocs[i]->ref == ref)
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
        if (mDocs[i]->ref.id() == id)
        {
            return i;
        }
    }
    return NONE;
}

void ALFloaterScriptStudio::openScript(const ALScriptRef& ref, const std::string& name)
{
    const size_t already = indexOf(ref);
    if (already != NONE)
    {
        activate(already);
        return;
    }

    auto doc  = std::make_unique<Doc>();
    doc->ref  = ref;
    doc->name = name.empty() ? getString("Untitled") : name;

    ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
    p.name = "editor_" + ref.id();
    p.rect = mEditorHost->getLocalRect();
    p.follows.flags(FOLLOWS_ALL);
    p.read_only         = true;
    p.word_wrap         = mWordWrap;
    p.show_line_numbers = mLineNumbers;
    p.soft_tabs         = true;
    doc->editor         = LLUICtrlFactory::create<ALCodeEditor>(p);
    doc->editor->setText(getString("Loading"));
    doc->editor->setVisible(false);
    mEditorHost->addChild(doc->editor);
    doc->changed = doc->editor->onTextChanged([this]() { fillTabs(); });

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
        doc.editor->setText(answer.text);
        doc.editor->setReadOnly(!answer.modifiable);
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString(answer.modifiable ? "Loaded" : "LoadedReadOnly", args));
    }
    fillTabs();
    if (index == mActive)
    {
        refreshToolbar();
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
        tab.value   = doc.ref.id();
        tab.dirty   = doc.editor->isDirty();
        tab.toolTip = doc.ref.inInventory() ? getString("TabInventoryTip") : getString("TabObjectTip");
        tabs.push_back(std::move(tab));
        if (i == mActive)
        {
            chosen = doc.ref.id();
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
    ALScriptWorkspace::SaveOptions options;
    options.compileTarget = mCompileTarget->getValue().asString();
    if (options.compileTarget.empty())
    {
        options.compileTarget = doc.language.compileTarget;
    }
    options.running = doc.ref.inInventory() || mRunning->get();
    std::string error;
    if (!ALScriptWorkspace::instance().save(doc.ref, doc.editor->text(), options, nullptr, error))
    {
        setStatus(error, true);
        return;
    }
    doc.saving = true;
    doc.problems.clear();
    doc.editor->clearMarks();
    doc.editor->setDecorations({});
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

    std::vector<ALCodeEditor::Decoration> decorations;
    static const LLUIColor error_color = LLUIColorTable::instance().getColor("CodeMarkError", LLColor4::red);
    for (const ALScriptWorkspace::Diagnostic& problem : doc.problems)
    {
        const bool error = problem.level != "WARNING";
        const ALCodeEditor::Mark mark = error ? ALCodeEditor::Mark::Error : ALCodeEditor::Mark::Warning;
        if (doc.editor->markAt(problem.line) < mark)
        {
            doc.editor->setMark(problem.line, mark);
        }
        ALCodeEditor::Decoration decoration;
        const ALTextPos at(problem.line, problem.hasColumn ? problem.column : 0);
        decoration.range = ALTextRange(doc.editor->document().clamp(at), doc.editor->document().nextWord(at));
        decoration.color = error_color.get();
        decorations.push_back(std::move(decoration));
    }
    doc.editor->setDecorations(std::move(decorations));

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
        fillProblems(&doc);
        refreshToolbar();
    }
    fillTabs();
    if (doc.closeAfterSave)
    {
        letGoOf(index);
    }
}

void ALFloaterScriptStudio::fillProblems(const Doc* doc)
{
    mProblems->deleteAllItems();
    if (!doc)
    {
        return;
    }
    for (size_t i = 0; i < doc->problems.size(); ++i)
    {
        const ALScriptWorkspace::Diagnostic& problem = doc->problems[i];
        LLSD                                 row;
        row["value"]                    = static_cast<S32>(i);
        row["columns"][0]["column"]     = "line";
        row["columns"][0]["value"]      = problem.hasColumn ? llformat("%d:%d", problem.line + 1, problem.column + 1) : llformat("%d", problem.line + 1);
        row["columns"][1]["column"]     = "level";
        row["columns"][1]["value"]      = problem.level;
        row["columns"][2]["column"]     = "message";
        row["columns"][2]["value"]      = problem.message;
        mProblems->addElement(row);
    }
    if (doc->problems.empty())
    {
        mProblems->setCommentText(doc->loaded && !doc->editor->isDirty() ? getString("NoProblems") : std::string());
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
    if (index >= doc->problems.size())
    {
        return;
    }
    const ALScriptWorkspace::Diagnostic& problem = doc->problems[index];
    doc->editor->setCaret(ALTextPos(problem.line, problem.hasColumn ? problem.column : 0));
    doc->editor->setFocus(true);
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
    Doc& doc = *mDocs[index];
    doc.changed.release();
    mEditorHost->removeChild(doc.editor);
    doc.editor->die();
    mDocs.erase(mDocs.begin() + index);
    if (mDocs.empty())
    {
        mActive = NONE;
        fillTabs();
        refreshToolbar();
        fillProblems(nullptr);
    }
    else
    {
        activate(llmin(index, mDocs.size() - 1));
    }
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
    else if (doc && action == "cut")
    {
        doc->editor->cut();
    }
    else if (doc && action == "copy")
    {
        doc->editor->copy();
    }
    else if (doc && action == "paste")
    {
        doc->editor->paste();
    }
    else if (doc && action == "select_all")
    {
        doc->editor->selectAll();
    }
    else if (doc && action == "toggle_comment")
    {
        doc->editor->toggleComment();
    }
    else if (action == "word_wrap")
    {
        mWordWrap = !mWordWrap;
        for (std::unique_ptr<Doc>& each : mDocs)
        {
            each->editor->setWordWrap(mWordWrap);
        }
        saveState();
    }
    else if (action == "line_numbers")
    {
        mLineNumbers = !mLineNumbers;
        for (std::unique_ptr<Doc>& each : mDocs)
        {
            each->editor->setShowLineNumbers(mLineNumbers);
        }
        saveState();
    }
    else if (action == "problems")
    {
        mFolds.toggle("problems");
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
    if (action == "close" || action == "select_all")
    {
        return doc != nullptr;
    }
    if (action == "load_file" || action == "paste" || action == "toggle_comment")
    {
        return doc && doc->modifiable;
    }
    if (action == "undo")
    {
        return doc && doc->editor->canUndo();
    }
    if (action == "redo")
    {
        return doc && doc->editor->canRedo();
    }
    if (action == "cut")
    {
        return doc && doc->editor->canCut();
    }
    if (action == "copy")
    {
        return doc && doc->editor->canCopy();
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
    if (action == "problems")
    {
        return !mFolds.collapsed("problems");
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
}
