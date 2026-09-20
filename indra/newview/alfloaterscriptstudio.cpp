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
#include "lldate.h"
#include "lltimer.h"
#include "llsyntaxid.h"
#include "llversioninfo.h"
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

#include <algorithm>
#include <fstream>

namespace
{
    // How long after the last keystroke the analyzers are asked.
    const F64 ANALYSIS_DELAY = 0.35;
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
    mEditorHost->addChild(editor);
    return editor;
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
        // A script the preprocessor wrapped: the editor holds what the
        // server compiled, and the source goes in a tab of its own.
        doc.envelope = ALScriptEnvelope::parse(answer.text);
        if (doc.envelope)
        {
            doc.editor->setText(doc.envelope->expanded);
            if (!doc.envelope->compileTarget.empty())
            {
                doc.language.compileTarget = doc.envelope->compileTarget;
            }
            showSource(doc);
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
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString(answer.modifiable ? "Loaded" : "LoadedReadOnly", args));
        scheduleAnalysis(doc, true);
    }
    fillTabs();
    if (index == mActive)
    {
        refreshToolbar();
    }
}

void ALFloaterScriptStudio::showSource(Doc& doc)
{
    const std::string id    = doc.id + ":source";
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
    Doc& source = *mDocs[index];
    LLStringUtil::format_map_t args;
    args["[NAME]"]  = doc.name;
    source.name     = getString("SourceTabName", args);
    source.language = doc.language;
    source.editor->setSyntax(doc.language.lua ? "slua" : "lsl");
    teachEditor(source);
    source.editor->setText(doc.envelope->source);
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
                    const std::string returns = attrs.get("return").asString();
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
    Doc&            doc = *mDocs[index];
    const ALTextPos at(result.line, result.column);
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
    }
}

std::string ALFloaterScriptStudio::textToSave(const Doc& doc) const
{
    if (!doc.envelope)
    {
        return doc.editor->text();
    }
    // Back in its envelope, with the source as it was, so Firestorm opens
    // what we save; the lines that say who wrote it and when are ours.
    ALScriptEnvelope envelope = *doc.envelope;
    envelope.expanded         = doc.editor->text();
    envelope.compileTarget    = mCompileTarget->getValue().asString();
    if (envelope.compileTarget.empty())
    {
        envelope.compileTarget = doc.language.compileTarget;
    }
    envelope.programVersion = LLVersionInfo::instance().getChannelAndVersion();
    envelope.lastCompiled   = LLDate::now().asString();
    return envelope.wrap();
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
        tab.value   = doc.id;
        tab.dirty   = doc.editor->isDirty();
        tab.toolTip = doc.sourceView ? getString("TabSourceTip") : doc.ref.inInventory() ? getString("TabInventoryTip") : getString("TabObjectTip");
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
    if (!ALScriptWorkspace::instance().save(doc.ref, textToSave(doc), options, nullptr, error))
    {
        setStatus(error, true);
        return;
    }
    doc.saving = true;
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
    refreshProblems(doc);
}

void ALFloaterScriptStudio::refreshProblems(Doc& doc)
{
    static const LLUIColor error_color   = LLUIColorTable::instance().getColor("CodeMarkError", LLColor4::red);
    static const LLUIColor warning_color = LLUIColorTable::instance().getColor("CodeMarkWarning", LLColor4::yellow);
    static const LLUIColor note_color    = LLUIColorTable::instance().getColor("CodeMarkNote", LLColor4::blue);

    doc.shown.clear();
    doc.editor->clearMarks();
    std::vector<ALCodeEditor::Decoration> decorations;
    const ALTextDocument&                 text = doc.editor->document();

    auto add = [&](S32 line, S32 column, bool has_column, S32 end_line, S32 end_column, ALCodeEditor::Mark mark,
                   const std::string& level, const std::string& origin, const std::string& message) {
        Doc::Shown row;
        row.line      = line;
        row.column    = column;
        row.hasColumn = has_column;
        row.level     = level;
        row.origin    = origin;
        row.message   = message;
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
        decoration.color   = (mark == ALCodeEditor::Mark::Error ? error_color : mark == ALCodeEditor::Mark::Warning ? warning_color : note_color).get();
        decoration.message = origin + ": " + message;
        decorations.push_back(std::move(decoration));
    };

    for (const ALScriptWorkspace::Diagnostic& problem : doc.problems)
    {
        const bool error = problem.level != "WARNING";
        add(problem.line, problem.column, problem.hasColumn, problem.line, problem.column,
            error ? ALCodeEditor::Mark::Error : ALCodeEditor::Mark::Warning, problem.level, getString("OriginCompiler"), problem.message);
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
        add(problem.line, problem.column, true, problem.endLine, problem.endColumn, mark, level, origin, message);
    }
    if (!doc.definitionsError.empty())
    {
        Doc::Shown row;
        row.level   = "NOTE";
        row.origin  = getString("OriginDefinitions");
        row.message = doc.definitionsError;
        doc.shown.push_back(std::move(row));
    }
    std::stable_sort(doc.shown.begin(), doc.shown.end(),
                     [](const Doc::Shown& a, const Doc::Shown& b) { return a.line != b.line ? a.line < b.line : a.column < b.column; });
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
        row["columns"][0]["value"]  = problem.hasColumn ? llformat("%d:%d", problem.line + 1, problem.column + 1) : llformat("%d", problem.line + 1);
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
    // A script's source tab goes with the script. Highest index first, so
    // the other stays where it was found.
    std::vector<size_t> going{ index };
    if (!mDocs[index]->sourceView)
    {
        if (const size_t companion = indexOf(mDocs[index]->id + ":source"); companion != NONE)
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
    if (action == "load_file" || action == "paste" || action == "toggle_comment" || action == "complete")
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
