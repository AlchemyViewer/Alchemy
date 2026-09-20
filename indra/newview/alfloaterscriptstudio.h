/**
 * @file alfloaterscriptstudio.h
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

#pragma once

#include "alcodeeditor.h"
#include "alscriptanalysis.h"
#include "alscriptenvelope.h"
#include "alscriptworkspace.h"
#include "alstudiofloater.h"

#include <boost/signals2.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ALTabStrip;
class LLButton;
class LLCheckBoxCtrl;
class LLComboBox;
class LLPanel;
class LLScrollListCtrl;

// The scripting studio of doc/SCRIPT_STUDIO.md, as far as phase 1 takes it:
// scripts from inventory and from objects open in tabs over code editors,
// saved and compiled through the workspace, with the compiler's problems in
// a pane that jumps to the line, the region's vocabulary colouring,
// completing and explaining the text, the analyzers checking it as it is
// typed, and a script the preprocessor wrapped shown as the code the
// server compiled with the author's source in a tab beside it. Its regions
// fold and come out as any studio's do. Not yet: the explorer, the output,
// the inspector.
class ALFloaterScriptStudio final : public ALStudioFloater
{
    friend class LLFloaterReg;

public:
    AL_VIEW_TYPE(ALFloaterScriptStudio, ALStudioFloater);

    // Whether scripts open here rather than in the legacy floaters: the
    // ALScriptStudioEnabled setting, which is how the two share a viewer
    // for a release.
    static bool wantsScripts();
    // The studio, with this script open in it.
    static ALFloaterScriptStudio* open(const ALScriptRef& ref, const std::string& name = std::string());

    bool postBuild() override;
    void onClose(bool app_quitting) override;
    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;
    bool undo() override;
    bool redo() override;

    void openScript(const ALScriptRef& ref, const std::string& name);

private:
    ALFloaterScriptStudio(const LLSD& key);
    ~ALFloaterScriptStudio() override;

    // One tab of the studio: a script, or the author's source of one that
    // the preprocessor wrapped, read-only beside it.
    struct Doc
    {
        ALScriptRef                                ref;
        // The script's id, or the script's id and ":source".
        std::string                                id;
        std::string                                name;
        ALCodeEditor*                              editor = nullptr;
        ALScriptWorkspace::Language                language;
        LLUUID                                     assetId;
        bool                                       loaded     = false;
        bool                                       modifiable = false;
        bool                                       saving     = false;
        bool                                       closeAfterSave = false;
        bool                                       sourceView = false;
        // The envelope the asset came in, whose expanded code the editor
        // holds, and which a save wraps the code back in.
        std::optional<ALScriptEnvelope>            envelope;
        // What the compiler said of the last save.
        std::vector<ALScriptWorkspace::Diagnostic> problems;
        // What the analyzer said of the text at analysisVersion; when the
        // next check is due, or zero; the version last asked about.
        ALScriptProblems                           analysis;
        U32                                        analysisVersion  = 0;
        U32                                        requestedVersion = 0;
        F64                                        analysisDue      = 0.0;
        std::string                                definitionsError;
        // Both, in the order the pane lists them.
        struct Shown
        {
            S32         line      = 0;
            S32         column    = 0;
            bool        hasColumn = false;
            std::string level;
            std::string origin;
            std::string message;
        };
        std::vector<Shown>                         shown;
        boost::signals2::scoped_connection         changed;
    };
    static constexpr size_t NONE = static_cast<size_t>(-1);

    // A word of the language, as the region defines it: what colours and
    // completes.
    struct Vocab
    {
        std::string  text;
        std::string  detail;
        std::string  tooltip;
        ALSyntaxKind kind = ALSyntaxKind::Text;
        bool         deprecated = false;
    };

    Doc*   active();
    size_t indexOf(const ALScriptRef& ref) const;
    size_t indexOf(std::string_view id) const;
    void   activate(size_t index);
    void   fillTabs();
    void   refreshToolbar();

    ALCodeEditor*             makeEditor(const std::string& id, bool read_only);
    void                      showSource(Doc& doc);
    const std::vector<Vocab>& vocabulary(bool lua);
    // The region's words for colouring and completing, and the analyzer
    // behind completion, hover and signature help.
    void                      teachEditor(Doc& doc);
    void                      askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at);
    void                      answered(const ALScriptAnalysis::Result& result);
    std::string               textToSave(const Doc& doc) const;

    void loaded(const ALScriptWorkspace::Loaded& answer);
    void save(Doc& doc);
    void saveAll();
    void compiled(const ALScriptWorkspace::CompileResult& result);
    void fillProblems(const Doc* doc);
    void onProblemSelected();

    // The analyzers: a check is due a moment after the last keystroke,
    // sent from draw, answered whenever the worker gets to it, and kept
    // only if the text has not moved on.
    void scheduleAnalysis(Doc& doc, bool now = false);
    void pumpAnalysis();
    void requestAnalysis(Doc& doc);
    void analysed(const ALScriptAnalysis::Result& result);
    // The marks, the squiggles and the pane, from the compiler's problems
    // and the analyzer's together.
    void refreshProblems(Doc& doc);

    void closeDocument(std::string_view id);
    void closeDocumentAnswered(const std::string& id, S32 option);
    void letGoOf(size_t index);

    void onTabChosen(const std::string& value);
    void onMenuAction(const LLSD& param);
    bool onMenuEnable(const LLSD& param);
    bool onMenuCheck(const LLSD& param);
    void onCompileTarget();
    void onRunning();
    void onReset();
    void revert(Doc& doc);
    void loadFromFile();
    void saveToFile();
    void fileChosenToLoad(const std::vector<std::string>& files);
    void fileChosenToSave(const std::vector<std::string>& files);

    void writeState(LLSD& state) const override;
    void readState(const LLSD& state) override;

    std::vector<std::unique_ptr<Doc>>  mDocs;
    size_t                             mActive = NONE;
    std::vector<Vocab>                 mVocabulary[2];
    bool                               mVocabularyBuilt[2] = { false, false };
    bool                               mWordWrap    = false;
    bool                               mLineNumbers = true;
    LLPanel*                           mEditorHost    = nullptr;
    ALTabStrip*                        mTabs          = nullptr;
    LLScrollListCtrl*                  mProblems      = nullptr;
    LLComboBox*                        mCompileTarget = nullptr;
    LLCheckBoxCtrl*                    mRunning       = nullptr;
    LLButton*                          mResetButton   = nullptr;
    LLButton*                          mSaveButton    = nullptr;
    boost::signals2::scoped_connection mCompiledConnection;
    boost::signals2::scoped_connection mDefinitionsConnection;
};
