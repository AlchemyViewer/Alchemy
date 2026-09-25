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
#include "aldiskincludes.h"
#include "alfilewrite.h"
#include "fsyspath.h"
#include "alnotecarditems.h"
#include "alscriptmodules.h"
#include "alscriptpreprocessor.h"
#include "alscriptweightspane.h"
#include "alscriptstudiofileio.h"
#include "alscriptstudiovimrc.h"
#include "alemptystate.h"
#include "aljumpbar.h"
#include "aloutputview.h"
#include "alpanelist.h"
#include "llsdutil.h"
#include "alscopebar.h"
#include "alscriptfixes.h"
#include "alscriptformatter.h"
#include "alscriptkeymap.h"
#include "alscriptmessages.h"
#include "altabstrip.h"
#include "altextsearch.h"
#include "alvimkeymap.h"
#include "llagent.h"
#include "llappviewer.h"
#include "lldate.h"
#include "lltimer.h"
#include "llsyntaxid.h"
#include "llversioninfo.h"
#include "llbutton.h"
#include "llcallbacklist.h"
#include "llcheckboxctrl.h"
#include "alsaid.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "lldir.h"
#include "lldirpicker.h"
#include "lleditmenuhandler.h"
#include "llfocusmgr.h"
#include "llfilepicker.h"
#include "llfiltereditor.h"
#include "llfloaterperms.h"
#include "llexperiencecache.h"
#include "llfloaterreg.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "lllayoutstack.h"
#include "lllineeditor.h"
#include "llmenugl.h"
#include "llnotecard.h"
#include "llnotificationsutil.h"
#include "llscrolllistctrl.h"
#include "llsdserialize.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltrans.h"
#include "llexternaleditor.h"
#include "lllogchat.h"
#include "llscripteditorws.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llviewerassettype.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llweb.h"
#include "llviewermenufile.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
// [RLVa:KB]
#include "rlvhandler.h"
#include "rlvlocks.h"
// [/RLVa:KB]

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace
{
    // How long after the last keystroke the analyzers are asked.
    const F64 ANALYSIS_DELAY = 0.35;
    // What makes an include's functions and globals a script to the
    // parser: a state after them. Put after the text, so that every place
    // in it is where it was; what is said of it is dropped.
    const char FRAGMENT_STATE[] = "\ndefault{state_entry(){}}\n";
    // How long a tab to be restored waits for its object or its item to be
    // in hand after the window is built: long enough for what is near to
    // come into view after a login.
    const F64 RESTORE_WAIT = 5.0 * 60.0;
}

using ALScriptFileIO::fileTooLarge;
using ALScriptFileIO::readWholeFile;
using ALScriptFileIO::StudioLiveFile;
using ALScriptFileIO::writeTempFile;

namespace
{
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

namespace
{
    // The studio window the keyboard was last in.
    LLHandle<LLFloater> sLastWorkedIn;
    // Where a window made by tearing a tab off is put against the point it
    // was let go of: its tabs under the mouse, a little in from its left.
    constexpr S32 TORN_OFFSET_X = 60;
    constexpr S32 TORN_OFFSET_Y = 30;
}

// static
ALFloaterScriptStudio* ALFloaterScriptStudio::lastWorkedIn()
{
    ALFloaterScriptStudio* window = ALViewType::as<ALFloaterScriptStudio>(sLastWorkedIn.get());
    return window && window->getVisible() ? window : nullptr;
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
    // scripts decides the same way for every window. Else the window last
    // worked in, as a script window used to open over the last one.
    ALFloaterScriptStudio* window = ref.isNull() ? nullptr : holderOf(ref, std::string());
    if (!window && !ref.isNull())
    {
        window = lastWorkedIn();
    }
    if (window)
    {
        if (!LLFloaterReg::canShowInstance("script_studio", window->getKey()))
        {
            return nullptr;
        }
        // A floater takes the keyboard as it opens unless told otherwise.
        const bool auto_focus = window->getAutoFocus();
        window->setAutoFocus(auto_focus && take_focus);
        window->openFloater(window->getKey());
        window->setAutoFocus(auto_focus);
        if (take_focus)
        {
            window->setFocus(true);
        }
        window->openScript(ref, name, std::nullopt, -1, take_focus);
        return window;
    }
    // Made first where it is to open without the keyboard -- a script
    // handed over while someone types elsewhere, which would otherwise
    // take the rest of their line -- so that it can be told so before it
    // opens; shown through the registry, which says whether it may be.
    ALFloaterScriptStudio* quiet      = take_focus ? nullptr : LLFloaterReg::getTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD());
    const bool             auto_focus = quiet && quiet->getAutoFocus();
    if (quiet)
    {
        quiet->setAutoFocus(false);
    }
    ALFloaterScriptStudio* studio = LLFloaterReg::showTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD(), take_focus ? TAKE_FOCUS_YES : TAKE_FOCUS_NO);
    if (quiet)
    {
        quiet->setAutoFocus(auto_focus);
    }
    if (studio && !ref.isNull())
    {
        studio->openScript(ref, name, std::nullopt, -1, take_focus);
    }
    return studio;
}

// static
void ALFloaterScriptStudio::editSnippets(bool lua)
{
    const std::string path = ALScriptSnippets::path(lua);
    if (!LLFile::isfile(path))
    {
        // A first one to copy from; the file says how they are written.
        ALScriptSnippets::Snippet example;
        example.name   = "Say to the owner";
        example.prefix = "ownersay";
        example.detail = "A message to whoever owns the object";
        example.body   = lua ? "ll.OwnerSay(${1:\"message\"})$0" : "llOwnerSay(${1:\"message\"});$0";
        ALScriptSnippets::saveOwn(lua, { example });
    }
    if (ALFloaterScriptStudio* studio = LLFloaterReg::showTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD(), TAKE_FOCUS_YES))
    {
        // As the XML it is, whichever language it holds snippets for.
        studio->openFile(path, false);
    }
}

// static
void ALFloaterScriptStudio::openVimrc()
{
    if (ALFloaterScriptStudio* studio = LLFloaterReg::showTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD(), TAKE_FOCUS_YES))
    {
        studio->editVimrc();
    }
}

std::string ALFloaterScriptStudio::vimrc(std::string& whence)
{
    ALScriptStudioVimrc& vimrc = ALScriptStudioVimrc::instance();
    whence                     = vimrc.notecard().notNull() ? vimrc.notecardName() : ALScriptStudioVimrc::filePath();
    return vimrc.text();
}

void ALFloaterScriptStudio::editVimrc()
{
    ALScriptStudioVimrc& vimrc = ALScriptStudioVimrc::instance();
    if (const LLUUID item = vimrc.notecard(); item.notNull())
    {
        openScript(ALScriptRef(LLUUID::null, item), vimrc.notecardName());
        return;
    }
    const std::string path = ALScriptStudioVimrc::filePath();
    if (!LLFile::isfile(path) && !ALFileWrite::whole(path, getString("VimrcNewFile")))
    {
        LLStringUtil::format_map_t args;
        args["[FILE]"] = path;
        setStatus(getString("VimrcNotMade", args), true);
        return;
    }
    openFile(path, false);
}

// static
ALFloaterScriptStudio* ALFloaterScriptStudio::explore(const LLUUID& root)
{
    ALFloaterScriptStudio* studio = LLFloaterReg::showTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD(), TAKE_FOCUS_YES);
    if (studio && root.notNull())
    {
        studio->mExplorerPane->explore(root);
    }
    return studio;
}

// static
void ALFloaterScriptStudio::savedElsewhere(const ALScriptRef& ref, const std::string& text, const LLUUID& asset_id)
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
        if (doc.notecard || !doc.loaded || doc.save.sending())
        {
            continue;
        }
        if (doc.editor->isDirty() && doc.modifiable)
        {
            // What was typed here stays a step behind what was saved
            // there, and the tab stays unsaved -- over the asset that save
            // made, which a kept text is now measured against.
            if (asset_id.notNull())
            {
                doc.assetId = asset_id;
            }
            doc.carriedText = text;
            window->takeCarriedText(doc);
        }
        else
        {
            // As if loaded afresh: the envelope read, the expanded code
            // shown, the analyzers asked, and nothing to save -- the caret
            // and the view where the author left them.
            doc.keepCaret  = doc.editor->caret();
            doc.keepScroll = doc.editor->scrollY();
            ALScriptWorkspace::Loaded answer;
            answer.ref        = ref;
            answer.assetId    = asset_id.notNull() ? asset_id : doc.assetId;
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
        Doc&                       doc = *window->mDocs[index];
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        if (doc.loaded && doc.modifiable && doc.editor->isDirty())
        {
            // What was typed is not the deletion's to take: the tab stays,
            // its text kept on disk, and says what can be done with it.
            doc.orphan          = Doc::Orphan::Removed;
            doc.noticeDismissed = false;
            window->mRecovery.keep(doc);
            window->report(window->getString("OrphanRemovedKept", args), true, &doc, { "copy", "export" });
            window->refreshNotice();
            window->refreshToolbar();
            return;
        }
        window->letGoOf(index);
        window->report(window->getString("ItemRemovedClosed", args));
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
    // The menus' items by name, in the table of what each does.
    addCommands();
    mCommitCallbackRegistrar.add("ScriptStudio.Menu", [this](LLUICtrl*, const LLSD& name) { mCommands.run(name.asString()); });
    mEnableCallbackRegistrar.add("ScriptStudio.Enable", [this](LLUICtrl*, const LLSD& name) { return mCommands.enabled(name.asString()); });
    mEnableCallbackRegistrar.add("ScriptStudio.Check", [this](LLUICtrl*, const LLSD& name) { return mCommands.checked(name.asString()); });
}

ALFloaterScriptStudio::~ALFloaterScriptStudio()
{
    // A menu still open calls into this window, which is going: it goes
    // first. The menus live in the viewer's menu holder, not here.
    for (LLHandle<LLContextMenu>* menu : { &mTabMenuHandle, &mListMenuHandle })
    {
        if (LLContextMenu* open = menu->get())
        {
            // Out of sight at once; gone once the frame is done with it.
            open->hide();
            open->die();
        }
    }
    // Going with unsaved text still in a tab -- the viewer made to go
    // without asking, as it does with no region to say goodbye to -- the
    // text is written as it stands, for the next login to offer back.
    // Straight to the store: nothing here is to be said any more.
    ALScriptRecoveryStore* store = ALScriptStudioRecovery::store();
    for (const std::unique_ptr<Doc>& doc : mDocs)
    {
        if (store && doc->editor && !doc->recoveryKey.empty() && doc->loaded && doc->modifiable && !doc->carriedText && doc->editor->isDirty())
        {
            store->write(ALScriptStudioRecovery::entryOf(*doc));
        }
    }
}

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
    // The toolbar's tips say their keys as the menus have them, in the
    // platform's own spelling: Ctrl+S here, the Command symbol on a Mac --
    // kept as the skin wrote them, since a key rebound says them again.
    mKeyTips = { { "save_btn", { "save" } },
                 { "save_all_btn", { "save_all" } },
                 { "undo_btn", { "undo" } },
                 { "redo_btn", { "redo" } },
                 { "find_btn", { "find", "find_in_files" } },
                 { "format_btn", { "format" } },
                 { "expanded_btn", { "expanded" } },
                 { "fold_explorer", { "explorer" } },
                 { "fold_bottom", { "problems", "references", "output", "search" } },
                 { "fold_inspector", { "inspector" } } };
    for (const auto& [control, items] : mKeyTips)
    {
        if (LLView* view = findChild<LLView>(control))
        {
            mKeyTipTexts[control] = view->getToolTip();
        }
    }
    setStatusLine(getChild<LLTextBox>("status"));
    mFolds.bind(this, { { "explorer", "explorer_panel", "fold_explorer", getString("PaneExplorer") },
                        { "bottom", "bottom_panel", "fold_bottom", getString("PaneBottom") },
                        { "inspector", "inspector_panel", "fold_inspector", getString("PaneInspector") } });
    mFolds.onChanged([this]() { saveState(); });

    mEditorHost    = getChild<LLPanel>("editor_panel");
    // What the window says with no script open. The editors are made as
    // scripts are opened and die with them, so with none there is nothing
    // in this panel at all: a toolbar over a hole, saying nothing about
    // where a script comes from. Made once, over the whole of the host,
    // and shown whenever the host is otherwise empty.
    {
        ALEmptyState::Params ep(LLUICtrlFactory::getDefaultParams<ALEmptyState>());
        ep.name               = "no_docs";
        ep.rect               = mEditorHost->getLocalRect();
        ep.follows.flags      = FOLLOWS_ALL;
        ep.background_visible = false;
        ep.visible            = false;
        mNoDocs               = LLUICtrlFactory::create<ALEmptyState>(ep);
        mEditorHost->addChild(mNoDocs);
        mNoDocs->say(getString("NoScriptOpenHeadline"), getString("NoScriptOpenSentence"), getString("NoScriptOpenAction"), getString("NoScriptOpenNew"));
        mNoDocs->onAction([this]() { mCommands.run("open_file"); });
        mNoDocs->onSecondAction([this]() { mCommands.run("new_script"); });
    }
    mTabs          = getChild<ALTabStrip>("tabs");
    mBreadcrumb    = getChild<ALJumpBar>("breadcrumb");
    mNoticePanel   = getChild<LLLayoutPanel>("editor_notice");
    mNoticeText    = getChild<LLTextBox>("notice_text");
    mNoticeFirst   = getChild<LLButton>("notice_first");
    mNoticeSecond  = getChild<LLButton>("notice_second");
    mNoticeFirst->setCommitCallback([this](LLUICtrl*, const LLSD&) { onNoticeAction(mNoticeActions[0]); });
    mNoticeSecond->setCommitCallback([this](LLUICtrl*, const LLSD&) { onNoticeAction(mNoticeActions[1]); });
    getChild<LLButton>("notice_close")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onNoticeAction("close"); });
    mBottomTabs    = getChild<LLTabContainer>("bottom_tabs");
    mReferences    = getChild<ALPaneList>("references");
    mOutline       = getChild<ALPaneList>("outline");
    mWeightsPane   = getChild<ALScriptWeightsPane>("weights_tab");
    mWeightsParts  = mWeightsPane->partsList();
    mSymbol        = getChild<ALTextView>("symbol");
    // The declaration, in the script the inspector is about or in the
    // include it was declared in.
    mSymbol->onLinkClicked([this](const ALTextView::Substitution& link) { goToDeclared(link.value); });
    mReferencesHead = getChild<LLTextBox>("references_head");
    mOutlineFilter  = getChild<LLFilterEditor>("outline_filter");
    mOutlineFilter->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (Doc* doc = active())
        {
            refreshOutline(*doc);
        }
    });
    mExplorerPane  = getChild<ALScriptExplorerPane>("explorer_pane");
    mCompileTarget = getChild<LLComboBox>("compile_target");
    mRunning       = getChild<LLCheckBoxCtrl>("running");
    mExperience    = getChild<LLComboBox>("experience");
    mExperienceProfile = getChild<LLButton>("experience_profile");
    mResetButton   = getChild<LLButton>("reset_btn");
    mSaveButton    = getChild<LLButton>("save_btn");
    mSaveAllButton = getChild<LLButton>("save_all_btn");
    mUndoButton    = getChild<LLButton>("undo_btn");
    mRedoButton    = getChild<LLButton>("redo_btn");
    mFindButton    = getChild<LLButton>("find_btn");
    mFormatButton  = getChild<LLButton>("format_btn");
    mExpandedButton = getChild<LLButton>("expanded_btn");
    // After the lookups: a menu item's key shown again asks its check,
    // which reads the panes.
    showEditorKeys();

    mTabs->onChosen(boost::bind(&ALFloaterScriptStudio::onTabChosen, this, _1));
    mTabs->onClosed(boost::bind(&ALFloaterScriptStudio::closeDocument, this, _1));
    mTabs->onMenu(boost::bind(&ALFloaterScriptStudio::showTabMenu, this, _1, _2, _3));
    mTabs->onReordered(boost::bind(&ALFloaterScriptStudio::onTabsReordered, this, _1));
    mTabs->onTorn([this](const std::string& id, S32 x, S32 y) { onTabTorn(id, x, y); });
    mTabs->onListAsked([this]() { showAllTabs(); });
    // A preview double-clicked is held.
    mTabs->onHeld([this](const std::string& id) {
        const size_t index = indexOf(id);
        if (index != NONE)
        {
            holdPreview(*mDocs[index]);
        }
    });
    mBreadcrumb->onChose(boost::bind(&ALFloaterScriptStudio::onCrumbChosen, this, _1, _2));
    mBreadcrumb->onTrailerChosen([this](const std::string& value) { onTrailerChosen(value); });
    mProblemsPane = getChild<ALScriptProblemsPane>("problems_tab");
    mReferences->setCommitCallback([this](LLUICtrl*, const LLSD&) { onReferenceChosen(false); });
    mReferences->setDoubleClickCallback([this]() { onReferenceChosen(true); });
    mWeightsParts->setCommitCallback([this](LLUICtrl*, const LLSD&) { onWeightChosen(false); });
    mWeightsParts->setDoubleClickCallback([this]() { onWeightChosen(true); });
    mOutline->setCommitCallback([this](LLUICtrl*, const LLSD&) { onOutlineChosen(false); });
    mOutline->setDoubleClickCallback([this]() { onOutlineChosen(true); });
    mOutputPane = getChild<ALScriptOutputPane>("output_tab");
    // What was said before the window opened, then everything after.
    for (const ALScriptWorkspace::RuntimeEvent& event : ALScriptWorkspace::instance().recentRuntime())
    {
        runtimeEvent(event);
    }
    mRuntimeConnection = ALScriptWorkspace::instance().onRuntime([this](const ALScriptWorkspace::RuntimeEvent& event) { runtimeEvent(event); });

    mSearchPane = getChild<ALScriptSearchPane>("search_tab");
    // In a pane's list, return goes to the place chosen, to type there, and
    // escape goes back to the script without going anywhere -- asked of
    // the list first, since the panel it is in takes escape to mean
    // nothing is to have the keyboard.
    const std::pair<ALPaneList*, std::function<void()>> lists[] = { { mProblemsPane->list(), [this]() { mProblemsPane->choose(true); } },
                                                                          { mReferences, [this]() { onReferenceChosen(true); } },
                                                                          { mOutline, [this]() { onOutlineChosen(true); } },
                                                                          { mSearchPane->list(), [this]() { mSearchPane->choose(true); } },
                                                                          { mWeightsParts, [this]() { onWeightChosen(true); } } };
    for (const auto& [list, go] : lists)
    {
        list->setKeyHandler([this, list, go](KEY key, MASK mask) {
            // The outline's left and right fold and open, as a tree's do.
            if (list == mOutline && mask == MASK_NONE && (key == KEY_LEFT || key == KEY_RIGHT))
            {
                LLScrollListItem* item = mOutline->getFirstSelected();
                if (!item)
                {
                    return false;
                }
                const size_t index = static_cast<size_t>(item->getValue().asInteger());
                foldOutline(index, key == KEY_LEFT);
                return true;
            }
            if (mask != MASK_NONE || (key != KEY_RETURN && key != KEY_ESCAPE))
            {
                return false;
            }
            if (key == KEY_RETURN)
            {
                go();
            }
            else
            {
                revealed(list, true);
            }
            return true;
        });
    }
    mReferences->setComparison([this](S32 column, const LLScrollListItem* a, const LLScrollListItem* b) {
        const size_t i = static_cast<size_t>(a->getValue().asInteger());
        const size_t j = static_cast<size_t>(b->getValue().asInteger());
        if (i >= mFound.places.size() || j >= mFound.places.size())
        {
            return 0;
        }
        const Doc::Place& x    = mFound.places[i];
        const Doc::Place& y    = mFound.places[j];
        const auto        cell = [](const LLScrollListItem* item, S32 at) { return item->getColumn(at) ? item->getColumn(at)->getValue().asString() : std::string(); };
        S32               said = 0;
        switch (column)
        {
            case 0: said = LLStringUtil::compareDict(cell(a, 0), cell(b, 0)); break;
            case 2: said = cell(b, 2).size() < cell(a, 2).size() ? -1 : cell(b, 2).size() > cell(a, 2).size() ? 1 : 0; break;
            case 3: said = LLStringUtil::compareDict(x.text, y.text); break;
            default: break;
        }
        if (said == 0 && column != 1 && x.file != y.file)
        {
            said = LLStringUtil::compareDict(cell(a, 0), cell(b, 0));
        }
        return said != 0 ? said : x.span < y.span ? -1 : y.span < x.span ? 1 : 0;
    });
    mOutlineSort = getChild<LLComboBox>("outline_sort");
    mOutlineSort->selectFirstItem();
    mOutlineSort->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (Doc* doc = active())
        {
            refreshOutline(*doc);
        }
        saveState();
    });
    mRunningConnection = ALScriptWorkspace::instance().onRunningState([this](const ALScriptWorkspace::RunningState& state) { runningState(state); });
    // Copy from the lists with no menu of their own; the problems' has
    // its own copying, and a right-click there would bring up both, the
    // copy menu over it.
    for (LLScrollListCtrl* list : { static_cast<LLScrollListCtrl*>(mReferences), static_cast<LLScrollListCtrl*>(mSearchPane->list()) })
    {
        listMenuFor(list);
    }
    mCompileTarget->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onCompileTarget, this));
    mRunning->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onRunning, this));
    mExperience->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onExperience, this));
    mExperienceProfile->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (const Doc* doc = active(); doc && doc->experience.notNull())
        {
            LLFloaterReg::showInstance("experience_profile", doc->experience, true);
        }
    });
    mResetButton->setCommitCallback(boost::bind(&ALFloaterScriptStudio::onReset, this));
    mSaveButton->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (Doc* doc = active())
        {
            mSaving.saveAsked(*doc);
        }
    });
    mSaveAllButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { mSaving.saveAll(); });
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
            doc->shownText()->perform(ALEditorCommand::Find);
        }
    });
    mFormatButton->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (Doc* doc = active())
        {
            format(*doc, false);
        }
    });
    mExpandedButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { toggleExpanded(); });
    mCompiledConnection =
        ALScriptWorkspace::instance().onCompiled([this](const ALScriptWorkspace::CompileResult& result) { mSaving.compiled(result); });
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

    // The lints chosen again, the mode, the solver or how long a check may
    // take: every script checked again.
    for (const char* setting : { "ALScriptLintLevels", "ALScriptLuauMode", "ALScriptLuauSolver", "ALScriptLuauCheckSeconds" })
    {
        if (LLControlVariable* control = gSavedSettings.getControl(setting))
        {
            mSettingConnections.emplace_back(control->getSignal()->connect([this](LLControlVariable*, const LLSD&, const LLSD&) {
                for (std::unique_ptr<Doc>& doc : mDocs)
                {
                    if (doc->loaded)
                    {
                        scheduleAnalysis(*doc, true);
                    }
                }
            }));
        }
    }

    // The preprocessor's settings, from the menu here or the preferences:
    // every script expanded and checked again, and the transforms' words
    // coloured as they now are -- a moment after the last change, since a
    // field typed in changes its setting at every key.
    for (const char* setting :
         { "ALScriptPreprocEnabled", "ALScriptPreprocSwitch", "ALScriptPreprocLazyLists", "ALScriptPreprocCompress", "ALScriptPreprocOptimizer",
           "ALScriptPreprocOptimizerShrinkNames", "ALScriptPreprocOptimizerAddStrings", "ALScriptPreprocOptimizerInlining", "ALScriptPreprocExtensions",
           "ALScriptPreprocDiskIncludes", "ALScriptPreprocDiskIncludeFolder", "ALScriptPreprocIncludeOrder", "ALScriptPreprocDefines" })
    {
        if (LLControlVariable* control = gSavedSettings.getControl(setting))
        {
            const bool words = std::string_view(setting) == "ALScriptPreprocSwitch" || std::string_view(setting) == "ALScriptPreprocExtensions";
            mSettingConnections.emplace_back(control->getSignal()->connect([this, words](LLControlVariable*, const LLSD&, const LLSD&) {
                mPreprocessorDue   = LLTimer::getTotalSeconds() + ANALYSIS_DELAY;
                mPreprocessorWords = mPreprocessorWords || words;
            }));
        }
    }

    // Whether vim's unnamed register is the system clipboard, as the
    // setting says, for every editor of this window; :set clipboard
    // changes it for the session, and the setting changed again says so.
    mVim.shared().unnamedClipboard = gSavedSettings.getBOOL("ALScriptStudioVimClipboard");
    if (LLControlVariable* control = gSavedSettings.getControl("ALScriptStudioVimClipboard"))
    {
        mSettingConnections.emplace_back(control->getSignal()->connect(
            [this](LLControlVariable*, const LLSD& value, const LLSD&) { mVim.shared().unnamedClipboard = value.asBoolean(); }));
    }
    // The vimrc read again into this window's vim whenever it changes: its
    // file saved, a notecard dropped on the preferences' box, or saved.
    mVimrcConnection = ALScriptStudioVimrc::instance().onChanged([this]() {
        if (mVim.sourced())
        {
            mVim.source();
        }
    });

    loadState();
    // Unsaved text kept against a crash, a lost connection noticed, and what
    // holds each tab looked at, whether the window is shown or not: a
    // window closed or hidden a moment after typing still writes what was
    // typed.
    {
        const LLHandle<LLFloater> handle = getHandle();
        doPeriodically(
            [handle]() {
                ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
                if (!studio)
                {
                    return true;
                }
                studio->pumpRecovery();
                return false;
            },
            0.25f);
    }
    // With the pins read, the objects in hand.
    mExplorerPane->relist();
    refreshToolbar();
    fillTabs();
    // A window opened with nothing in it says so from the first frame:
    // nothing else calls this until a script is activated.
    showEditors();
    // The tabs open when the window was last put away, where they can still
    // be had; then, once this window is built, the windows popped out of it
    // then, each where it was, and what was kept on purpose at the quit --
    // after them, so that each goes to the window that has its tab.
    if (mMain)
    {
        restoreTabs(mRestoreTabs);
        mRestoreTabs                     = LLSD();
        const LLSD                windows = std::exchange(mRestoreWindows, LLSD());
        const LLHandle<LLFloater> handle  = getHandle();
        doOnIdleOneTime([handle, windows]() {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->restoreWindows(windows);
                studio->reopenKept();
            }
        });
    }
    return true;
}

void ALFloaterScriptStudio::restoreTabs(const LLSD& open)
{
    // Where they can still be had: an inventory item that is still there, a
    // file that is, an object's item that the object in sight lists; one
    // not in hand yet waits a while for it. What was unsaved in them was
    // asked about before the viewer quit.
    if (!open.isMap())
    {
        return;
    }
    const LLSD  tabs   = open["tabs"];
    const S32   chosen = open["active"].asInteger();
    std::string chosen_id;
    for (S32 i = 0; i < tabs.size(); ++i)
    {
        const LLSD& one = tabs[i];
        std::string id;
        if (one.has("file"))
        {
            const std::string path = one["file"].asString();
            if (LLFile::isfile(path))
            {
                openFile(path, one["lua"].asBoolean());
                id = "disk:" + path;
            }
        }
        else
        {
            const ALScriptRef      ref(one["object"].asUUID(), one["item"].asUUID());
            LLViewerObject*        object = ref.inInventory() ? nullptr : gObjectList.findObject(ref.object);
            const LLInventoryItem* item   = ref.inInventory() ? gInventory.getItem(ref.item) : object ? object->getInventoryItem(ref.item) : nullptr;
            if (item && !ref.isNull())
            {
                openScript(ref, item->getName());
                id = ref.id();
            }
            else if (!ref.isNull())
            {
                // Not in hand yet -- its object not in view, or what it
                // holds not asked, the inventory still coming: opened
                // when it is, for a while (pumpRestores).
                mPendingRestores.push_back(PendingRestore{ ref, LLTimer::getTotalSeconds() + RESTORE_WAIT, false, viewNamed(one["view"].asString()) });
            }
        }
        // Showing what it showed, once there is that to show.
        if (const size_t at = id.empty() ? NONE : indexOf(id); at != NONE)
        {
            showView(*mDocs[at], viewNamed(one["view"].asString()));
        }
        if (i == chosen)
        {
            chosen_id = id;
        }
    }
    if (const size_t index = chosen_id.empty() ? NONE : indexOf(chosen_id); index != NONE)
    {
        activate(index);
    }
}

void ALFloaterScriptStudio::restoreWindows(const LLSD& windows)
{
    for (LLSD::array_const_iterator it = windows.beginArray(); it != windows.endArray(); ++it)
    {
        // Asked about as any window opened is, and opened without the
        // keyboard: the author opened this one.
        const LLSD key(LLUUID::generateNewID().asString());
        if (!LLFloaterReg::canShowInstance("script_studio", key))
        {
            continue;
        }
        ALFloaterScriptStudio* window = LLFloaterReg::getTypedInstance<ALFloaterScriptStudio>("script_studio", key);
        if (!window)
        {
            continue;
        }
        const bool auto_focus = window->getAutoFocus();
        window->setAutoFocus(false);
        window->openFloater(key);
        window->setAutoFocus(auto_focus);
        // Where it was, and back on the screen where that has changed.
        const LLSD& rect = (*it)["rect"];
        if (rect.isArray() && rect.size() == 4)
        {
            window->setShape(LLRect(rect[0].asInteger(), rect[3].asInteger(), rect[2].asInteger(), rect[1].asInteger()));
            gFloaterView->adjustToFitScreen(window, false);
        }
        window->restoreTabs((*it)["open"]);
        // Nothing of it to be had now or later: no window.
        if (window->mDocs.empty() && window->mPendingRestores.empty())
        {
            window->closeFloater();
            continue;
        }
        window->mRestoring = true;
    }
}

void ALFloaterScriptStudio::reopenKept()
{
    // What was kept on purpose at the last quit opens again, with its
    // unsaved changes, into its restored tab or a tab of its own.
    ALScriptRecoveryStore* store = ALScriptStudioRecovery::store();
    if (!store)
    {
        return;
    }
    S32 reopened = 0;
    for (const ALScriptRecoveryEntry& entry : store->left())
    {
        if (entry.state == ALScriptRecoveryEntry::State::Kept)
        {
            mRecovery.recover(entry);
            ++reopened;
        }
    }
    if (reopened > 0)
    {
        report(counted("RecoveryReopened", reopened));
    }
}

void ALFloaterScriptStudio::closeFloater(bool app_quitting)
{
    if (mClosingWindow)
    {
        // Asked already; the answer carries on from there.
        return;
    }
    mAppQuitting        = app_quitting;
    S32         unsaved = 0;
    std::string one;
    for (const std::unique_ptr<Doc>& doc : mDocs)
    {
        if (doc->loaded && doc->modifiable && doc->unsaved())
        {
            ++unsaved;
            one = doc->name;
        }
    }
    // The viewer going with the connection lost: nothing can be saved and
    // nothing is asked. Every unsaved text is kept, for the next login to
    // offer back.
    if (app_quitting && gDisconnected && unsaved > 0)
    {
        bool kept = true;
        for (std::unique_ptr<Doc>& doc : mDocs)
        {
            kept = mRecovery.keep(*doc) && kept;
        }
        if (kept)
        {
            mTabsAtQuit = openTabs();
            while (!mDocs.empty())
            {
                letGoOf(mDocs.size() - 1, true);
            }
            ALStudioFloater::closeFloater(app_quitting);
            return;
        }
        // Not all of it could be written: asked, as it is when connected,
        // rather than let go of unasked.
    }
    // The viewer quitting, this window shown or hidden: what it holds
    // unsaved is asked about, in sight, the quit waiting on the answer --
    // saved, kept for next time, or let go of.
    if (app_quitting && unsaved > 0)
    {
        mTabsAtQuit = openTabs();
        if (!getVisible())
        {
            openFloater(getKey());
        }
        setFocus(true);
        mClosingWindow = true;
        LLSD args;
        args["COUNT"] = unsaved;
        args["NAME"]  = one;
        const LLHandle<LLFloater> handle = getHandle();
        LLNotificationsUtil::add(unsaved > 1 ? "ScriptStudioQuitUnsavedMany" : "ScriptStudioQuitUnsaved", args, LLSD(),
                                 [handle](const LLSD& notification, const LLSD& response) {
                                     if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                                     {
                                         studio->quitAnswered(LLNotificationsUtil::getSelectedOption(notification, response));
                                     }
                                 });
        return;
    }
    // A popped-out window closed: what is unsaved in it asked about, one
    // question for several; the main window only hides, keeping its tabs.
    if (!mMain && unsaved > 0)
    {
        mClosingWindow = true;
        if (unsaved > 1)
        {
            LLSD args;
            args["COUNT"] = unsaved;
            const LLHandle<LLFloater> handle = getHandle();
            LLNotificationsUtil::add("ScriptStudioSaveChangesMany", args, LLSD(),
                                     [handle](const LLSD& notification, const LLSD& response) {
                                         if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                                         {
                                             studio->closeWindowAnswered(LLNotificationsUtil::getSelectedOption(notification, response));
                                         }
                                     });
        }
        else
        {
            continueClosing();
        }
        return;
    }
    if (!mMain)
    {
        // What it had open, for the main window to open next time, where
        // the viewer is going.
        if (app_quitting && !mTabsAtQuit.isMap())
        {
            mTabsAtQuit = openTabs();
        }
        while (!mDocs.empty())
        {
            letGoOf(mDocs.size() - 1);
        }
    }
    ALStudioFloater::closeFloater(app_quitting);
}

bool ALFloaterScriptStudio::quittingOnUs() const
{
    return mClosingWindow && LLAppViewer::instance()->quitRequested();
}

void ALFloaterScriptStudio::stopClosing()
{
    // The quit was waiting on this window: it waits no longer, and the
    // author can quit again once the script is seen to.
    if (quittingOnUs())
    {
        LLAppViewer::instance()->abortQuit();
        // The panes out in windows of their own were put back as the quit
        // closed those windows: out again, as the author had them.
        mFolds.putBackOut();
    }
    mClosingWindow = false;
    mAppQuitting   = false;
    mTabsAtQuit    = LLSD();
}

void ALFloaterScriptStudio::closeTabs(std::string_view which, const Doc* keep)
{
    // The unsaved among them asked about in one question; the ids gathered
    // first, since closing moves the rest.
    std::vector<std::string> ids;
    for (const std::unique_ptr<Doc>& each : mDocs)
    {
        if ((which == "close_others" && each.get() != keep) || which == "close_all" || (which == "close_saved" && !each->unsaved()))
        {
            ids.push_back(each->id);
        }
    }
    if (!ids.empty())
    {
        closeMany(ids);
    }
}

void ALFloaterScriptStudio::letGoOf(Doc& doc)
{
    if (const size_t index = indexOf(doc.id); index != NONE)
    {
        letGoOf(index);
    }
}

void ALFloaterScriptStudio::saveToClose(const std::string& id)
{
    mSaving.saveToClose(id);
}

void ALFloaterScriptStudio::output(const ALOutputView::Entry& entry)
{
    mOutputPane->view()->append(entry);
}

void ALFloaterScriptStudio::showOutput()
{
    showBottom("output_tab");
}

void ALFloaterScriptStudio::reorderTabs(const std::vector<std::string>& order)
{
    onTabsReordered(order);
}

bool ALFloaterScriptStudio::readFile(const std::string& path, std::string& text)
{
    return readWholeFile(path, text);
}

bool ALFloaterScriptStudio::writeFile(const std::string& path, const std::string& text)
{
    return ALFileWrite::whole(path, text);
}

void ALFloaterScriptStudio::jumpedFrom(Doc& doc, const ALTextView& view, const ALTextPos& from)
{
    rememberPlace(NavPlace{ doc.id, from, &view == doc.expandedEditor ? Doc::View::Expanded : Doc::View::Source });
}

std::vector<std::string> ALFloaterScriptStudio::fileFolders(const Doc& doc) const
{
    std::vector<std::string> folders;
    if (!doc.file.empty())
    {
        folders.push_back(fsyspath(fsyspath(doc.file).parent_path()).string());
    }
    for (const std::string& folder : ALScriptPreprocessor::includeFolders())
    {
        folders.push_back(folder);
    }
    return folders;
}

void ALFloaterScriptStudio::pickLine(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder,
                                     const std::string& title, S32 rows, std::function<void(const std::string& line)> chosen,
                                     std::function<void(const std::string& line)> shifted, std::function<void()> cancelled)
{
    quickOpen(std::move(candidates), placeholder, title, std::move(chosen), mEditorHost, 420, ALQuickOpen::heightForRows(rows),
              std::move(cancelled), std::move(shifted));
}

ALScriptStudioSaving::Options ALFloaterScriptStudio::saveOptions() const
{
    static LLCachedControl<bool> hold(gSavedSettings, "ALScriptStudioPreflight", false);
    ALScriptStudioSaving::Options options;
    options.fix          = gSavedSettings.getBOOL("ALScriptFixOnSave");
    options.format       = gSavedSettings.getBOOL("ALScriptFormatOnSave");
    options.trim         = gSavedSettings.getBOOL("ALScriptTrimOnSave");
    options.holdOnErrors = hold;
    options.compress     = gSavedSettings.getBOOL("ALScriptPreprocCompress");
    options.program      = LLVersionInfo::instance().getChannelAndVersion();
    return options;
}

void ALFloaterScriptStudio::tidy(Doc& doc, bool fix, bool format_it, bool trim)
{
    // The safe fixes first, while the text is still the one they were made
    // for.
    if (fix)
    {
        fixAll(doc, FixPick{ std::string(), true });
    }
    if (format_it)
    {
        format(doc, false);
    }
    if (trim)
    {
        trimTrailing(doc);
    }
}

bool ALFloaterScriptStudio::send(const Doc& doc, const std::string& text, const ALScriptWorkspace::SaveOptions& options, std::string& error)
{
    return ALScriptWorkspace::instance().save(doc.ref, text, options, nullptr, error);
}

bool ALFloaterScriptStudio::sendNotecard(const Doc& doc, const std::string& text, const std::vector<LLPointer<LLInventoryItem>>& items,
                                         std::string& error)
{
    return ALScriptWorkspace::instance().saveNotecard(doc.ref, text, items, nullptr, error);
}

void ALFloaterScriptStudio::runPreprocessor(const Doc& doc, std::function<void(const ALPreprocessor::Result&)> answer)
{
    ALScriptPreprocessor::instance().run(preprocessRequest(doc), std::move(answer));
}

void ALFloaterScriptStudio::keepForRecovery(Doc& doc)
{
    mRecovery.keep(doc);
}

void ALFloaterScriptStudio::showProblems()
{
    showBottom("problems_tab");
}

void ALFloaterScriptStudio::selectFirstError(bool checkers_only)
{
    mProblemsPane->selectFirstError(checkers_only);
}

void ALFloaterScriptStudio::onClose(bool app_quitting)
{
    // A popped-out window going with the viewer: what it had open is the
    // main window's to open next time, as the main window's own tabs are.
    if (!mMain && mTabsAtQuit.isMap() && LLAppViewer::instance()->quitRequested())
    {
        if (ALFloaterScriptStudio* main = LLFloaterReg::findTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD()); main && main != this)
        {
            const LLRect r = isMinimized() && !getExpandedRect().isEmpty() ? getExpandedRect() : getRect();
            LLSD         window;
            window["rect"] = LLSD::emptyArray().with(0, r.mLeft).with(1, r.mBottom).with(2, r.mRight).with(3, r.mTop);
            window["open"] = mTabsAtQuit;
            main->adoptWindowAtQuit(window);
        }
    }
    ALStudioFloater::onClose(app_quitting);
}

void ALFloaterScriptStudio::adoptWindowAtQuit(const LLSD& window)
{
    mWindowsAtQuit.append(window);
    saveState();
}

void ALFloaterScriptStudio::draw()
{
    if (hasFocus())
    {
        sLastWorkedIn = getHandle();
    }
    pumpPreprocessor();
    pumpAnalysis();
    pumpCaret();
    mExplorerPane->pump();
    mVim.pump();
    if (mVimMode)
    {
        ALScriptStudioVimrc::instance().check();
    }
    mSearchPane->pump();
    pumpSettle();
    refreshUndoLabels();
    if (mPlacesStale)
    {
        fillReferences();
    }
    mOutputPane->pump();
    // The fix list last shown, weighed once nothing more is coming to it;
    // dropped once it has closed.
    if (mFixesToWeigh)
    {
        const size_t index = indexOf(mFixesToWeigh->id);
        Doc*         doc   = index != NONE ? mDocs[index].get() : nullptr;
        if (!doc || !doc->editor->fixesOpen())
        {
            mFixesToWeigh.reset();
        }
        else if (!doc->editor->actionsAwaited())
        {
            const FixesToWeigh asked = std::move(*mFixesToWeigh);
            mFixesToWeigh.reset();
            weighFixes(*doc, asked.shown, asked.fixes);
        }
    }
    // The Weights tab, filled while it is looked at: with what came since,
    // or with the script now in front; and that script weighed for the
    // targets beside its own, which the tab alone asks for.
    const bool weights_shown = weightsShown();
    if (weights_shown)
    {
        Doc* doc = active();
        if (mWeightsStale || !mWeightsWereShown || (doc ? doc->id : std::string()) != mWeightsPane->shownId())
        {
            mWeightsStale = false;
            refreshWeights();
        }
        if (doc && !doc->weighing && doc->analysisVersion == doc->editor->document().version())
        {
            for (const ALScriptWeight::Target target : weighedTargets(*doc))
            {
                const bool held = doc->weightsVersion == doc->editor->document().version() &&
                                  std::any_of(doc->weights.begin(), doc->weights.end(), [target](const ALScriptWeight& w) { return w.target == target; });
                if (!held)
                {
                    weigh(*doc);
                    break;
                }
            }
        }
    }
    mWeightsWereShown = weights_shown;
    ALStudioFloater::draw();
}

bool ALFloaterScriptStudio::handleKeyHere(KEY key, MASK mask)
{
    // Control-tab and control-shift-tab go round the tabs, as everywhere --
    // the Mac's own Control key there, Command-Tab being the system's.
    if (key == KEY_TAB && ((mask & ~MASK_SHIFT) == MASK_CONTROL || (mask & ~MASK_SHIFT) == MASK_MAC_CONTROL))
    {
        cycleTab(mask & MASK_SHIFT ? -1 : 1);
        return true;
    }
#if LL_DARWIN
    // Command-G goes to a line through the menu, and the Mac's Control-G,
    // which it answered to before, still does.
    if (key == 'G' && mask == MASK_MAC_CONTROL)
    {
        goToLine();
        return true;
    }
#endif
    if (handleMenuAccelerator(key, mask) || handleUndoKeys(key, mask))
    {
        return true;
    }
    return ALStudioFloater::handleKeyHere(key, mask);
}

bool ALFloaterScriptStudio::undo()
{
    // The view in front's own steps: the expansion, being read, has none,
    // and the source's are not to be taken back out of sight.
    Doc*          doc  = active();
    ALCodeEditor* text = doc ? doc->shownText() : nullptr;
    if (!text || !text->canUndo())
    {
        return false;
    }
    text->undo();
    return true;
}

bool ALFloaterScriptStudio::redo()
{
    Doc*          doc  = active();
    ALCodeEditor* text = doc ? doc->shownText() : nullptr;
    if (!text || !text->canRedo())
    {
        return false;
    }
    text->redo();
    return true;
}

// --- documents -----------------------------------------------------------------

ALQuickOpen* ALFloaterScriptStudio::quickOpen(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                                              std::function<void(const std::string&)> chose, LLView* anchor, S32 width, S32 height,
                                              std::function<void()> escaped, std::function<void(const std::string&)> hold,
                                              std::function<void()> left)
{
    ALQuickOpen* quick = ALStudioFloater::quickOpen(std::move(candidates), placeholder, title, std::move(chose), anchor, width, height,
                                                    std::move(escaped), std::move(hold), std::move(left));
    if (quick)
    {
        const LLUIColorTable& colors = LLUIColorTable::instance();
        quick->setColors(colors.getColor("ScriptBackground").get(), colors.getColor("ScriptText").get());
    }
    return quick;
}

std::string ALFloaterScriptStudio::counted(const char* name, S32 count, LLStringUtil::format_map_t args) const
{
    args["[COUNT]"] = std::to_string(count);
    const std::string formed = std::string(name) + LLTrans::countForm(alSaidLanguage(), count);
    return getString(hasString(formed) ? formed : std::string(name), args);
}

std::string ALFloaterScriptStudio::words(const std::string& name, const LLStringUtil::format_map_t& args) const
{
    return getString(name, args);
}

ALScriptStudioDoc* ALFloaterScriptStudio::findDoc(std::string_view id)
{
    const size_t index = indexOf(id);
    return index != NONE ? mDocs[index].get() : nullptr;
}

ALScriptStudioDoc* ALFloaterScriptStudio::findDoc(const ALScriptRef& ref)
{
    const size_t index = indexOf(ref);
    return index != NONE ? mDocs[index].get() : nullptr;
}

std::vector<ALScriptStudioDoc*> ALFloaterScriptStudio::openDocs()
{
    std::vector<ALScriptStudioDoc*> docs;
    docs.reserve(mDocs.size());
    for (const std::unique_ptr<Doc>& doc : mDocs)
    {
        docs.push_back(doc.get());
    }
    return docs;
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

void ALFloaterScriptStudio::reindexDocs()
{
    mByDocId.clear();
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        mByDocId.emplace(mDocs[i]->id, i);
    }
    // Output listening to the scripts open here: which those are has
    // changed.
    if (mOutputPane)
    {
        mOutputPane->openChanged();
    }
}

void ALFloaterScriptStudio::rekeyDoc(Doc& doc, const std::string& id)
{
    const std::string was = doc.id;
    doc.id                = id;
    doc.editor->setName("editor_" + id);
    if (doc.expandedEditor)
    {
        doc.expandedEditor->setName("editor_" + id + ":expanded");
    }
    // Found by its new name from here on, by the index every answer asks
    // through, and by whatever the panes and the history held it by.
    reindexDocs();
    const auto follow = [&was, &id](std::string& held) {
        if (held == was)
        {
            held = id;
        }
    };
    mProblemsPane->rekey(was, id);
    follow(mFound.from);
    for (std::vector<NavPlace>* places : { &mBack, &mForward })
    {
        for (NavPlace& place : *places)
        {
            follow(place.doc);
        }
    }
    mSearchPane->rekey(was, id);
    for (const std::unique_ptr<Doc>& other : mDocs)
    {
        follow(other->copyOf);
    }
}

size_t ALFloaterScriptStudio::indexOf(std::string_view id) const
{
    // By the index, which every answer that comes back asks through.
    const auto found = mByDocId.find(id);
    return found == mByDocId.end() ? NONE : found->second;
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
    p.soft_tabs         = gSavedSettings.getBOOL("ALScriptStudioInsertSpaces");
    p.tab_width         = llclamp(gSavedSettings.getS32("ALScriptStudioTabWidth"), 1, 16);
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
    // Where a hover card says a name was declared, gone to when pressed.
    editor->setCardLinkHandler([this](const LLSD& value) { goToDeclared(value); });
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

// static
void ALFloaterScriptStudio::applyTypingOptions(ALCodeEditor& editor)
{
    editor.setSoftTabs(gSavedSettings.getBOOL("ALScriptStudioInsertSpaces"));
    editor.setTabWidth(llclamp(gSavedSettings.getS32("ALScriptStudioTabWidth"), 1, 16));
    editor.setAutoComplete(gSavedSettings.getBOOL("ALScriptStudioAutoComplete"));
    editor.setCompleteAfter(gSavedSettings.getS32("ALScriptStudioCompleteAfter"));
    editor.setAcceptOnEnter(gSavedSettings.getBOOL("ALScriptStudioAcceptOnEnter"));
    editor.setAutoClose(gSavedSettings.getBOOL("ALScriptStudioAutoClose"));
    const std::string caret = gSavedSettings.getString("ALScriptStudioCaretStyle");
    editor.setCaretStyle(caret == "block" ? ALTextView::CaretStyle::Block : caret == "underline" ? ALTextView::CaretStyle::Underline : ALTextView::CaretStyle::Line);
    editor.setCaretBlink(gSavedSettings.getBOOL("ALScriptStudioCaretBlink"));
    editor.setHoverCards(gSavedSettings.getBOOL("ALScriptStudioHoverCards"));
    editor.setHoverDelay(llclamp(gSavedSettings.getF32("ALScriptStudioHoverDelay"), 0.f, 5.f));
}

void ALFloaterScriptStudio::applyEditorOptions(ALCodeEditor& editor)
{
    editor.setFont(editorFont());
    editor.keymap() = ALScriptKeymap::current();
    // Vim put over the editor, or taken away; one already there keeps
    // its marks and registers. The vimrc is read before the first.
    if (mVimMode && !mVim.sourced())
    {
        mVim.source();
    }
    if (mVimMode && !editor.modalKeymap())
    {
        auto vim = std::make_unique<ALVimKeymap>();
        mVim.connect(*vim);
        editor.setModalKeymap(std::move(vim));
    }
    else if (!mVimMode && editor.modalKeymap())
    {
        editor.setModalKeymap(nullptr);
    }
    editor.setWordWrap(mWordWrap);
    editor.setShowLineNumbers(mLineNumbers);
    applyTypingOptions(editor);
    editor.setShowIndentGuides(mIndentGuides);
    editor.setShowWhitespace(mWhitespace);
    editor.setRelativeLineNumbers(mRelativeNumbers);
    editor.setColorBrackets(mRainbowBrackets);
    editor.setStickyHeaders(mStickyHeaders);
    // The heat is of the script's own lines, which the Preprocessed view
    // does not show.
    editor.setHeatShown(mWeightHeat && !LLStringUtil::endsWith(editor.getName(), ":expanded"));
    editor.setScrollMapWidth(mScrollMapWidth);
    editor.setScrollMapPreview(mScrollMapPreview);
    editor.setScrollMapOnLeft(mScrollMapLeft);
    editor.setScrollMap(mScrollMap);
    editor.setSpellCheck(mSpellCheck);
    // The vimrc's own tabs and indents over the studio's.
    if (mVimMode)
    {
        mVim.applyViewOptions(editor);
    }
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
    showEditorKeys();
    saveState();
}

void ALFloaterScriptStudio::openScript(const ALScriptRef& ref, const std::string& name, std::optional<std::string> carried, S32 line, bool focus)
{
    const size_t already = indexOf(ref);
    if (already == NONE && !carried)
    {
        // Open in another window: that one, brought forward, since two tabs
        // of one script would each save over the other, and keep one file
        // against a crash between them.
        if (ALFloaterScriptStudio* holder = holderOf(ref, std::string()); holder && holder != this)
        {
            holder->openFloater(holder->getKey());
            if (focus)
            {
                holder->setFocus(true);
            }
            holder->openScript(ref, name, std::nullopt, line, focus);
            return;
        }
    }
    if (already != NONE)
    {
        // Asked for outright, a preview is held; and called what it is
        // called now, renamed since it was opened.
        if (mOpenPreview == 0)
        {
            holdPreview(*mDocs[already]);
        }
        activate(already, focus);
        renameDoc(*mDocs[already], name);
        return;
    }
    const bool preview = mOpenPreview > 0;
    if (preview)
    {
        closePreview();
    }

    auto doc         = std::make_unique<Doc>();
    doc->preview     = preview;
    doc->ref         = ref;
    doc->id          = ref.id();
    doc->name        = name.empty() ? getString("Untitled") : name;
    doc->carriedText = std::move(carried);
    doc->pendingLine = line;
    doc->editor = makeEditor(doc->id, true);
    // What the editor says while there is nothing in it yet. Put in as
    // text it became a real line, numbered in the gutter and undone back
    // to by the first edit; the document is empty until the script
    // arrives, and should read as empty.
    doc->editor->setPlaceholder(getString("Loading"));
    wireDoc(*doc);
    // Kept against a crash under this; and what an earlier session left of
    // it, offered in the notice.
    doc->recoveryKey = ALScriptRecoveryStore::keyOf(ref.object, ref.item, std::string());
    if (ALScriptRecoveryStore* store = ALScriptStudioRecovery::store())
    {
        doc->recoverable = store->leftFor(doc->recoveryKey);
    }

    mDocs.push_back(std::move(doc));
    reindexDocs();
    activate(mDocs.size() - 1, focus);

    const LLHandle<LLFloater> handle = getHandle();
    ALScriptWorkspace::instance().load(ref, [handle](const ALScriptWorkspace::Loaded& answer) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->loaded(answer);
        }
    });
}

ALScriptNotecardTab& ALFloaterScriptStudio::notecardItems(Doc& doc, bool fresh)
{
    if (!doc.items || fresh)
    {
        doc.items = std::make_shared<ALScriptNotecardTab>(doc, *this, ALScriptNotecardTab::viewer());
    }
    return *doc.items;
}

void ALFloaterScriptStudio::wireDoc(Doc& doc)
{
    Doc* raw    = &doc;
    doc.changed = doc.editor->onTextChanged([this, raw]() {
        // Typed in, a preview is held.
        if (raw->preview && raw->editor->isDirty())
        {
            raw->preview = false;
        }
        // The strip and the toolbar made again only where the tab's facts
        // moved -- its unsaved dot, once in a stretch of typing -- and
        // otherwise only what a keystroke does move: undo and redo.
        const size_t index = indexOf(raw->id);
        if (index >= mTabFacts.size() || mTabFacts[index] != tabFactsOf(*raw))
        {
            fillTabs();
            refreshToolbar();
        }
        else if (raw == active())
        {
            mUndoButton->setEnabled(raw->shownText()->canUndo());
            mRedoButton->setEnabled(raw->shownText()->canRedo());
        }
        scheduleAnalysis(*raw);
        mSearchPane->typedIn(*raw);
        mRecovery.schedule(*raw);
    });
    doc.placedEdits = doc.editor->document().onChanged([this, raw](const ALTextDocument::Edit& edit) {
        slideProblems(*raw, edit);
        slidePlaces(*raw, edit);
        slideOutline(*raw, edit);
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
    // A kept text changed from an older version than the one loaded: the
    // script was saved since -- in another viewer, by someone else -- and
    // saving this replaces what was saved then.
    if (doc.recovering && doc.recovering->baseAsset.notNull() && doc.assetId.notNull() && doc.recovering->baseAsset != doc.assetId)
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        report(getString("RecoveryStale", args), true, &doc);
    }
    // Kept text with its history: put back as it was, steps and all, into
    // a tab that holds nothing of its own. One typed in since it opened
    // takes it as one more step, so that what was typed is a step back.
    if (doc.recovering && *doc.carriedText == doc.recovering->text && mRecovery.restoreHistory(doc, *doc.recovering))
    {
        doc.carriedText.reset();
        return;
    }
    if (*doc.carriedText != doc.editor->text())
    {
        doc.editor->setReadOnly(false);
        doc.editor->setSelection(ALTextRange(doc.editor->document().start(), doc.editor->document().end()));
        doc.editor->insertText(*doc.carriedText);
        // Barred where it differs from the saved text, not over the whole
        // of it, as the one edit that put it in would have it.
        if (const std::optional<std::string> saved = doc.editor->undoJournal().savedText())
        {
            doc.editor->barChangesSince(*saved);
        }
    }
    doc.carriedText.reset();
}

void ALFloaterScriptStudio::goToPending(Doc& doc)
{
    if (doc.pendingLine < 0)
    {
        return;
    }
    ALCodeEditor& source = sourceInFront(doc);
    if (doc.pendingColumn >= 0)
    {
        source.goTo(ALTextRange(ALTextPos(doc.pendingLine, doc.pendingColumn), ALTextPos(doc.pendingLine, doc.pendingColumn + doc.pendingLength)));
    }
    else
    {
        source.goToLine(doc.pendingLine);
    }
    doc.pendingLine   = -1;
    doc.pendingColumn = -1;
    doc.pendingLength = 0;
}

void ALFloaterScriptStudio::loaded(const ALScriptWorkspace::Loaded& answer)
{
    const size_t index = indexOf(answer.ref);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    // Read again for a revert, and not to be had -- the fetch failed: the
    // tab as it was, its text and whether it could be changed, rather than
    // the error where the text was; and kept against a crash again, which
    // the revert let go of.
    if (const std::optional<bool> could_change = std::exchange(doc.reverting, std::nullopt); could_change && !answer.error.empty())
    {
        doc.loaded     = true;
        doc.modifiable = *could_change;
        doc.editor->setReadOnly(!doc.modifiable);
        doc.keepCaret  = ALTextPos(-1, -1);
        mRecovery.keep(doc);
        LLStringUtil::format_map_t args;
        args["[NAME]"]  = doc.name;
        args["[ERROR]"] = answer.error;
        report(getString("RevertFailed", args), true, &doc);
        fillTabs();
        if (index == mActive)
        {
            refreshToolbar();
        }
        return;
    }
    if (!answer.name.empty())
    {
        doc.name = answer.name;
    }
    doc.assetId          = answer.assetId;
    doc.language         = answer.language;
    doc.targetChosen     = false;
    doc.experienceChosen = false;
    doc.modifiable       = answer.modifiable;
    doc.notecard         = answer.notecard;
    // What was picked for the next save in the window it came from, picked
    // here, over what the item says.
    if (const std::optional<std::string> target = std::exchange(doc.carriedTarget, std::nullopt); target && answer.error.empty())
    {
        doc.language.compileTarget = *target;
        doc.targetChosen           = true;
    }
    if (const std::optional<LLUUID> experience = std::exchange(doc.carriedExperience, std::nullopt); experience && answer.error.empty())
    {
        doc.experience       = *experience;
        doc.experienceChosen = true;
    }
    if (doc.recovering && (!answer.error.empty() || !answer.modifiable))
    {
        // Opened to take up a kept text, and what it came from cannot be
        // had, or may no longer be changed: the tab holds the text on its
        // own, unsaved, and says why -- and is loaded again later, further
        // apart each time, where that may go differently.
        using Failure                     = ALScriptWorkspace::Loaded::Failure;
        const ALScriptRecoveryEntry entry = *doc.recovering;
        doc.carriedText.reset();
        doc.carriedEmbedded.reset();
        doc.loadFailure  = answer.error.empty() ? Failure::NotPermitted : answer.failure;
        doc.loadError    = answer.error;
        doc.nextReattach = LLTimer::getTotalSeconds() + ALScriptRecoveryRetry::delayAfter(++doc.reattachTries);
        becomeOrphan(doc, entry, failedAs(doc, doc.loadFailure));
        if (doc.loadFailure == Failure::NotPermitted)
        {
            LLStringUtil::format_map_t args;
            args["[NAME]"] = doc.name;
            report(getString("OrphanLockedKept", args), true, &doc, { "copy", "export" });
        }
        else
        {
            report(answer.error, true, &doc, { "copy", "export" });
        }
        return;
    }
    doc.loadFailure = answer.failure;
    doc.loadError   = answer.error;
    if (answer.error.empty())
    {
        doc.reattachTries = 0;
    }
    if (!answer.error.empty())
    {
        // Said where the text would be, as the editor says it is loading:
        // no line of it numbered or coloured as though it were the script.
        doc.editor->setText(LLStringUtil::null);
        doc.editor->setPlaceholder(answer.error);
        doc.editor->setReadOnly(true);
        report(answer.error, true, &doc);
    }
    else if (answer.notecard)
    {
        // Plain text, with whatever the notecard carried kept to go back
        // with it; nothing to analyse or compile.
        doc.loaded                 = true;
        ALScriptNotecardTab& items = notecardItems(doc);
        items.loaded(answer.embedded);
        doc.editor->setSyntax("text");
        doc.editor->setText(answer.text);
        // A kept or copied text says its items by their places in the list
        // it was kept with, which the text now put in is read against.
        if (doc.carriedEmbedded)
        {
            items.take(std::move(*doc.carriedEmbedded));
            doc.carriedEmbedded.reset();
        }
        takeCarriedText(doc);
        items.place();
        doc.editor->setReadOnly(!answer.modifiable);
        items.wire();
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString(answer.modifiable ? "Loaded" : "LoadedReadOnly", args));
        if (!doc.ref.inInventory())
        {
            mExplorerPane->relist();
        }
        goToPending(doc);
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
        // A copy compiles for what its original did.
        if (!doc.targetOnLoad.empty())
        {
            doc.language.compileTarget = doc.targetOnLoad;
            doc.targetOnLoad.clear();
        }
        // A copy of a wrapped script is wrapped as its source was.
        if (doc.wrapOnLoad)
        {
            doc.wrapOnLoad = false;
            if (!doc.envelope)
            {
                doc.envelope = ALScriptEnvelope();
            }
        }
        // Loaded again with nothing wrapped round it, and nothing to
        // expand it afresh: what the other editor holds was compiled from
        // a text the script no longer is.
        if (!preprocessed(doc))
        {
            dropExpanded(doc);
        }
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
            mSaving.preprocess(doc);
        }
        scheduleAnalysis(doc, true);
        if (!doc.ref.inInventory())
        {
            // Whether it runs, what it compiles for and what experience it
            // runs under, which the region knows better than the text
            // does; and its object in the explorer.
            ALScriptWorkspace::instance().askRunning(doc.ref);
            if (!doc.notecard)
            {
                askExperienceOf(doc);
            }
            mExplorerPane->relist();
        }
        goToPending(doc);
    }
    // Loaded again -- reverted, or saved by an editor outside -- the caret
    // and the view where they were, rather than at the top.
    if (doc.keepCaret.line >= 0 && answer.error.empty())
    {
        doc.editor->setSelection(ALTextRange(doc.editor->document().clamp(doc.keepCaret), doc.editor->document().clamp(doc.keepCaret)));
        doc.editor->setScrollY(doc.keepScroll);
    }
    doc.keepCaret = ALTextPos(-1, -1);
    if (answer.error.empty())
    {
        // In: an empty script is empty, not still loading.
        doc.editor->setPlaceholder(LLStringUtil::null);
        noteRecentScript(doc);
        // Where it is, while it is in sight, for a kept text to say later.
        if (LLViewerObject* object = doc.ref.inInventory() ? nullptr : gObjectList.findObject(doc.ref.object))
        {
            LLViewerObject* root = object->getRootEdit() ? object->getRootEdit() : object;
            doc.objectName       = ALScriptWorkspace::objectName(root, doc.objectName);
            doc.regionName       = object->getRegion() ? object->getRegion()->getName() : doc.regionName;
        }
        // What was carried in is kept against a crash now, and an entry it
        // came from let go of; a text that came in clean has nothing kept.
        mRecovery.keep(doc);
        // A copy saved into the inventory from another tab: saved at once,
        // over whatever a check would find, since keeping it is the point.
        if (doc.saveOnLoad && doc.modifiable)
        {
            doc.saveOnLoad = false;
            doc.save.letAllPast(doc.editor->document().version());
            mSaving.save(doc);
        }
    }
    fillTabs();
    if (index == mActive)
    {
        refreshToolbar();
        // What a kept text offered here says of it may turn on what loaded.
        refreshNotice();
    }
}

void ALFloaterScriptStudio::showExpanded(Doc& doc, const std::string& text)
{
    const bool made = !doc.expandedEditor;
    if (made)
    {
        doc.expandedEditor = makeEditor(doc.id + ":expanded", true);
        doc.expandedEditor->setVisible(false);
    }
    doc.expandedEditor->setSyntax(doc.language.lua ? "slua" : "lsl");
    teachWords(*doc.expandedEditor, doc.language.lua);
    doc.expandedEditor->setText(text);
    if (&doc != active())
    {
        return;
    }
    // A tab that asked for the expansion before there was one -- brought
    // back from the last session, or moved into a window of its own --
    // shows it now that there is.
    if (made && doc.view == Doc::View::Expanded)
    {
        showView(doc, Doc::View::Expanded);
    }
    else
    {
        refreshToolbar();
    }
}

void ALFloaterScriptStudio::dropExpanded(Doc& doc)
{
    if (!doc.expandedEditor)
    {
        return;
    }
    // The source in its place first, with the keyboard where it was.
    showView(doc, Doc::View::Source);
    mEditorHost->removeChild(doc.expandedEditor);
    doc.expandedEditor->die();
    doc.expandedEditor = nullptr;
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
    const bool to_expanded = doc->shownView() != Doc::View::Expanded;
    showView(*doc, to_expanded ? Doc::View::Expanded : Doc::View::Source, true);
    // The expansion is made on loading and saving; the source changed
    // since is expanded again, and shows as it comes.
    if (to_expanded && doc->loaded && (!doc->uploaded.valid || doc->uploaded.version != doc->editor->document().version()))
    {
        mSaving.preprocess(*doc);
    }
}

void ALFloaterScriptStudio::showView(Doc& doc, Doc::View view, bool focus)
{
    // Asked of whichever editor is on screen, which is not always the view
    // asked for until now: a tab brought back asking for its expansion
    // shows its source until the expansion comes.
    const bool      had_keys = (doc.editor && doc.editor->hasFocus()) || (doc.expandedEditor && doc.expandedEditor->hasFocus());
    const Doc::View was      = doc.shownView();
    doc.view                 = view;
    if (&doc != active())
    {
        return;
    }
    // The pane already showing it: a tab brought back asking for the source
    // it shows, a jump back into the view in front.
    if (doc.shownView() == was && doc.shownText()->getVisible())
    {
        if (focus)
        {
            focusShown(doc);
        }
        return;
    }
    showEditors();
    if (focus || had_keys)
    {
        focusShown(doc);
    }
    // The bars read the caret of the view in front, which is another caret
    // now, wherever it stands: seen afresh on the next frame (pumpCaret).
    doc.caretSeen = ALTextPos(-1, -1);
    refreshToolbar();
}

ALCodeEditor& ALFloaterScriptStudio::sourceInFront(Doc& doc)
{
    if (doc.shownView() != Doc::View::Source)
    {
        showView(doc, Doc::View::Source);
    }
    return *doc.editor;
}

// static
void ALFloaterScriptStudio::focusShown(Doc& doc)
{
    if (ALCodeEditor* text = doc.shownText())
    {
        text->setFocus(true);
    }
}

// static
const char* ALFloaterScriptStudio::viewName(Doc::View view)
{
    return view == Doc::View::Expanded ? "expanded" : "source";
}

// static
ALFloaterScriptStudio::Doc::View ALFloaterScriptStudio::viewNamed(const std::string& name)
{
    return name == "expanded" ? Doc::View::Expanded : Doc::View::Source;
}

void ALFloaterScriptStudio::showEditors()
{
    if (mNoDocs)
    {
        mNoDocs->setVisible(mDocs.empty());
    }
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        Doc&            doc   = *mDocs[i];
        const bool      here  = i == mActive;
        const Doc::View shown = doc.shownView();
        doc.editor->setVisible(here && shown == Doc::View::Source);
        if (doc.expandedEditor)
        {
            doc.expandedEditor->setVisible(here && shown == Doc::View::Expanded);
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
    request.compileTarget = doc.language.compileTarget;
    // The optimizer's notes are read here: each says what it saved in code.
    request.weigh   = true;
    return request;
}

void ALFloaterScriptStudio::expandFor(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at, const ALTextPos& to)
{
    // The question waits for the text it is about: one of its kind that
    // was already waiting is somewhere the caret or the mouse has since
    // left.
    const auto same = std::find_if(doc.waiting.begin(), doc.waiting.end(), [kind](const Doc::Waiting& was) { return was.kind == kind; });
    if (same != doc.waiting.end())
    {
        same->at = at;
        same->to = to;
    }
    else
    {
        doc.waiting.push_back(Doc::Waiting{ kind, at, to });
    }
    const U32 version = doc.editor->document().version();
    if (doc.expanding && *doc.expanding == version)
    {
        // Already on its way; every question waiting takes the one answer.
        return;
    }
    doc.expanding                    = version;
    const LLHandle<LLFloater> handle = getHandle();
    const std::string         id     = doc.id;
    ALScriptPreprocessor::instance().expand(preprocessRequest(doc), [handle, id, version](const ALPreprocessor::Result& result) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->expandedAnswer(id, version, result);
        }
    });
}

void ALFloaterScriptStudio::expandedAnswer(const std::string& id, U32 version, const ALPreprocessor::Result& result)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    if (doc.expanding && *doc.expanding == version)
    {
        doc.expanding.reset();
    }
    if (version != doc.editor->document().version())
    {
        // The text has moved on. Whatever was waiting was asked about
        // the text as it was -- a hover over a word the edit may have
        // moved -- and the edit has scheduled a check of its own, so
        // the questions go rather than being asked of the wrong text.
        doc.waiting.clear();
        return;
    }
    doc.expanded.valid      = true;
    doc.expanded.disabled   = result.disabled;
    doc.expanded.version    = version;
    doc.expanded.generation = ++doc.expansions;
    doc.expanded.text     = result.text;
    doc.expanded.map      = result.map;
    doc.expanded.problems = result.problems;
    doc.expanded.resolved = result.resolved;
    // What the preprocessor found is shown with what the analyzers found.
    refreshProblems(doc);
    std::vector<Doc::Waiting> waiting;
    waiting.swap(doc.waiting);
    for (const Doc::Waiting& question : waiting)
    {
        askAnalyzer(doc, question.kind, question.at, question.to);
    }
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
    // Open in another window: that one, as a script is.
    if (indexOf("disk:" + path) == NONE)
    {
        if (ALFloaterScriptStudio* holder = holderOf(ALScriptRef(), path); holder && holder != this)
        {
            holder->openFloater(holder->getKey());
            holder->setFocus(true);
            holder->openFileHere(path, lua, line, column, length);
            return;
        }
    }
    openFileHere(path, lua, line, column, length);
}

void ALFloaterScriptStudio::openFileHere(const std::string& path, bool lua, S32 line, S32 column, S32 length)
{
    const std::string id      = "disk:" + path;
    size_t            already = indexOf(id);
    if (already != NONE && mOpenPreview == 0)
    {
        holdPreview(*mDocs[already]);
    }
    if (already == NONE)
    {
        std::string text;
        if (!readWholeFile(path, text))
        {
            LLStringUtil::format_map_t args;
            args["[FILE]"] = path;
            setStatus(getString(fileTooLarge(path) ? "FileTooLarge" : "IncludeGone", args), true);
            return;
        }

        const bool preview = mOpenPreview > 0;
        if (preview)
        {
            closePreview();
        }
        auto doc     = std::make_unique<Doc>();
        doc->preview = preview;
        doc->file    = path;
        doc->id      = id;
        doc->name = gDirUtilp->getBaseFileName(path);
        // The language its extension says; else the one it was asked for
        // from, where it was; else plain text.
        const FileLanguage language  = languageOfFile(path, lua);
        doc->language.lua            = language.lua;
        doc->language.compileTarget  = language.lua ? "luau" : "mono";
        doc->notecard                = !language.script;
        doc->editor                  = makeEditor(doc->id, false);
        doc->editor->setSyntax(!language.script ? textSyntaxOf(path) : language.lua ? "slua" : "lsl");
        doc->editor->setText(text);
        doc->loaded     = true;
        doc->modifiable = true;
        wireDoc(*doc);
        doc->recoveryKey = ALScriptRecoveryStore::keyOf(LLUUID::null, LLUUID::null, path);
        if (ALScriptRecoveryStore* store = ALScriptStudioRecovery::store())
        {
            doc->recoverable = store->leftFor(doc->recoveryKey);
        }
        mDocs.push_back(std::move(doc));
        reindexDocs();
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
        ALCodeEditor& source = sourceInFront(doc);
        if (column < 0)
        {
            source.goToLine(line);
        }
        else
        {
            source.goTo(ALTextRange(ALTextPos(line, column), ALTextPos(line, column + length)));
        }
    }
    focusShown(doc);
    fillTabs();
    refreshToolbar();
}

// static
std::string ALFloaterScriptStudio::textSyntaxOf(const std::string& path)
{
    // A file that is no script: coloured where it is XML or JSON, which
    // the studio has grammars for -- its snippets are XML -- else text.
    std::string extension = gDirUtilp->getExtension(path);
    LLStringUtil::toLower(extension);
    return extension == "xml" || extension == "xui" ? "xml" : extension == "json" ? "json" : "text";
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
    doc.editor->setSyntax(!language.script ? textSyntaxOf(doc.file) : language.lua ? "slua" : "lsl");
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
    mProblemsPane->forget(doc.id);
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
             // One more folder to look in, after those there are.
             std::vector<std::string> now = ALScriptPreprocessor::includeFolders();
             if (std::find(now.begin(), now.end(), folders.front()) == now.end())
             {
                 now.push_back(folders.front());
             }
             ALScriptPreprocessor::setIncludeFolders(now);
             gSavedSettings.setBOOL("ALScriptPreprocDiskIncludes", true);
         },
         ALScriptPreprocessor::includeFolders().empty() ? std::string() : ALScriptPreprocessor::includeFolders().back()))
        ->getFile();
}

// --- the language's words --------------------------------------------------------

namespace
{
    // The words of each language, built once from the definitions and
    // kept until they change.
    std::vector<ALFloaterScriptStudio::Vocab> sVocabulary[2];
    bool                                      sVocabularyBuilt[2] = { false, false };
    // How many times each has been built: what an index over one knows it
    // by, since the list built again is the same list, and may be as long.
    U32                                       sVocabularyBuilds[2] = { 0, 0 };
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
    ++sVocabularyBuilds[lua ? 1 : 0];
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
    // The script's functions and events, where the analyzer last found them:
    // what Next Function and Select Function go by, and vim's [[ and af.
    editor.setFunctionProvider([raw](std::vector<ALTextRange>& out) {
        for (const ALScriptOutlineEntry& entry : raw->outline)
        {
            if (entry.kind == ALScriptSymbolKind::Function || entry.kind == ALScriptSymbolKind::Event)
            {
                out.emplace_back(ALTextPos(entry.span.line, entry.span.column), ALTextPos(entry.span.endLine, entry.span.endColumn));
            }
        }
    });
    // An include's name, or a module's, leads to its file of itself: Go to
    // Definition and Control-click open it, anywhere on its line or call.
    editor.setLinkRequest([this, raw](const ALTextPos& at, bool follow) {
        const std::optional<Doc::Named> named = raw->namedAt(at);
        if (!named)
        {
            return ALTextRange();
        }
        if (follow && !openIncluded(*raw, named->name, named->require))
        {
            LLStringUtil::format_map_t args;
            args["[NAME]"] = named->name;
            setStatus(getString(named->require ? "ModuleNotFoundHere" : "IncludeNotFoundHere", args), true);
        }
        return named->range;
    });
    // What would put right the problems on a line, as the list, the card
    // and the gutter offer them; the one taken made here, where it is known
    // whether the text is still the one the fixes were made for.
    editor.setFixProvider([this, raw](S32 line, std::vector<ALCodeEditor::Fix>& out) { fixesOn(*raw, line, out); });
    editor.setFixesShown([this, raw](U32 shown, const std::vector<ALCodeEditor::Fix>& fixes) { mFixesToWeigh = FixesToWeigh{ raw->id, shown, fixes }; });
    editor.setActionRequest([this, raw](const ALTextRange& at) {
        raw->actionsAsked = at;
        askAnalyzer(*raw, ALScriptAnalysis::Kind::Actions, at.begin, at.end);
    });
    editor.setFixHandler([this, raw](const LLSD& value) {
        if (value.has("action"))
        {
            const size_t n = static_cast<size_t>(value["action"].asInteger());
            if (n < raw->actions.size())
            {
                const ALScriptFix action = raw->actions[n];
                applyFix(*raw, action, raw->actionsVersion);
            }
            return;
        }
        const Doc::Shown* shown = shownOf(value);
        const size_t      n     = static_cast<size_t>(value["fix"].asInteger());
        if (shown && n < shown->fixes.size())
        {
            const ALScriptFix fix     = shown->fixes[n];
            const U32         version = shown->fixesFor;
            applyFix(*raw, fix, version);
        }
    });
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
            text += "\n" + ALCodeEditor::deprecatedNote();
        }
        return true;
    });
    editor.setCompletionProvider([this, lua, raw](const ALTextPos& at, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
        auto begins = [&prefix](const std::string& word) { return ALCodeEditor::matchTier(word, prefix) >= 0; };
        // An LSL event's handler goes straight inside a state and nowhere
        // else, so a handler is offered only there; asked once, and only
        // if one matches.
        std::optional<bool> in_state;
        for (const Vocab& word : vocabulary(lua))
        {
            if (!begins(word.text))
            {
                continue;
            }
            if (!lua && word.kind == ALSyntaxKind::Event)
            {
                if (!in_state)
                {
                    in_state = inStateBody(*raw->editor, at);
                }
                if (!*in_state)
                {
                    continue;
                }
            }
            out.push_back(completionFor(word, lua));
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
                    c.text          = snippet.prefix;
                    c.detail        = getString("SnippetDetail") + "  " + snippet.name;
                    c.kind          = ALSyntaxKind::Control;
                    c.snippet       = snippet.body;
                    c.documentation = snippet.detail;
                    out.push_back(std::move(c));
                }
            }
        }
    });
}

// static
bool ALFloaterScriptStudio::inStateBody(ALCodeEditor& editor, const ALTextPos& at)
{
    // The blocks open at the position, each by what opened it: a state's
    // is the one after `default` or `state name`. Read off the grammar's
    // tokens, so that a brace in a string or a comment is none.
    std::vector<bool> open;
    std::string       before[2];
    for (S32 line = 0; line <= at.line && line < editor.document().lineCount(); ++line)
    {
        const std::string& text = editor.document().line(line);
        for (const ALSyntaxToken& token : editor.highlighter().tokens(line))
        {
            if (line == at.line && token.begin >= at.column)
            {
                break;
            }
            if (token.kind == ALSyntaxKind::Comment || token.kind == ALSyntaxKind::DocComment)
            {
                continue;
            }
            const std::string_view word = std::string_view(text).substr(token.begin, token.end - token.begin);
            if (word.find_first_not_of(" \t") == std::string_view::npos)
            {
                continue;
            }
            if (token.kind == ALSyntaxKind::Punctuation)
            {
                for (const char c : word)
                {
                    if (c == '{')
                    {
                        open.push_back(before[0] == "default" || before[1] == "state");
                    }
                    else if (c == '}' && !open.empty())
                    {
                        open.pop_back();
                    }
                }
            }
            before[1] = std::move(before[0]);
            before[0] = std::string(word);
        }
    }
    return !open.empty() && open.back();
}

ALCodeEditor::Completion ALFloaterScriptStudio::completionFor(const Vocab& word, bool lua) const
{
    ALCodeEditor::Completion c;
    c.text          = word.text;
    c.detail        = word.detail;
    c.kind          = word.kind;
    c.deprecated    = word.deprecated;
    c.documentation = word.tooltip;
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
            // By what it is rather than where it stands in a list that
            // may be made again while the picker is up -- the scripter's
            // own saved meanwhile, new definitions from a region.
            one.value  = list[i].name + '\n' + list[i].prefix;
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
            one.value  = words[i].text;
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
        ALCodeEditor::Completion chosen;
        if (what == "snippet")
        {
            const std::vector<Snippet>& list  = studio->snippets(lua);
            const auto                  found = std::find_if(list.begin(), list.end(),
                                                             [&value](const Snippet& one) { return one.name + '\n' + one.prefix == value; });
            if (found == list.end())
            {
                return;
            }
            chosen.text    = found->prefix;
            chosen.snippet = found->body;
        }
        else
        {
            const Vocab* word = vocabWord(lua, value);
            if (!word)
            {
                return;
            }
            chosen = studio->completionFor(*word, lua);
        }
        // In place of the selection, or at the caret.
        const ALTextRange selection = doc->editor->selection();
        doc->editor->complete(chosen, ALTextRange(std::min(selection.begin, selection.end), std::max(selection.begin, selection.end)));
    }, mEditorHost);
}

void ALFloaterScriptStudio::askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& at, const ALTextPos& to)
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
    request.endLine   = to.line;
    request.endColumn = to.column;
    request.semantics      = mSemanticColors;
    request.hintParameters = mInlayParameters;
    request.hintTypes      = mInlayTypes;
    if (doc.language.lua)
    {
        // What the script's `.luaurc` says, where it has one; one not in
        // hand yet is fetched, and the check made again when it is.
        // Over the scripter's own choice of lints and mode, which the
        // file overrides key by key; that choice alone where there is none.
        const ALLuauConfig                  base  = ALScriptLints::luauBase();
        const ALScriptPreprocessor::Request root  = preprocessRequest(doc, /*with_source*/ false);
        const bool                          found = ALScriptPreprocessor::instance().configOf(root, request.config, &base);
        if (!found)
        {
            request.config = base;
        }
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
        request.text = doc.expanded.text;
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
                                          from.line == at.line && from.column == at.column && end.line == to.line && end.column == to.column;
            request.endLine   = kept ? last.line : loc.line;
            request.endColumn = kept ? last.column : loc.column;
        }
    }
    if (kind == ALScriptAnalysis::Kind::Weigh)
    {
        request.targets = weighedTargets(doc);
        if (request.targets.empty())
        {
            return;
        }
    }
    else if (lslFragment(doc))
    {
        request.text += FRAGMENT_STATE;
    }
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptAnalysis::instance().ask(std::move(request), [handle, expansion](const ALScriptAnalysis::Result& result) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->answered(result, expansion);
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
}

void ALFloaterScriptStudio::answered(const ALScriptAnalysis::Result& result, U32 expansion)
{
    const size_t index = indexOf(result.id);
    if (index == NONE)
    {
        return;
    }
    Doc&      doc = *mDocs[index];
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
            scheduleAnalysis(doc, true);
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
                completion.documentation = c.documentation;
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
            // Where it was declared, as the inspector says it: a link there.
            std::vector<ALCodeEditor::CardLink> links;
            const Declared                      declared = declaredOf(doc, result);
            if (declared.line >= 0)
            {
                LLStringUtil::format_map_t args;
                args["[LINE]"]          = std::to_string(declared.line + 1);
                args["[FILE]"]          = declared.name;
                const std::string where = getString(declared.path.empty() ? "InspectDeclared" : "InspectDeclaredIn", args);
                text += "\n" + where;
                links.push_back({ where, getString("InspectDeclaredTip"), declared.value() });
            }
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
            doc.editor->supplyHover(at, text, std::move(links));
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
            symbolAnswered(doc, result, at);
            break;
        case ALScriptAnalysis::Kind::Inspect:
            inspected(doc, result, at);
            break;
        case ALScriptAnalysis::Kind::Actions:
            actionsAnswered(doc, result, expansion);
            break;
        case ALScriptAnalysis::Kind::Weigh:
            weighed(doc, result);
            break;
    }
}

std::optional<ALScriptWeight::Target> ALFloaterScriptStudio::weightTarget(const Doc& doc) const
{
    if (!doc.loaded || doc.notecard || lslFragment(doc))
    {
        return std::nullopt;
    }
    if (doc.language.lua)
    {
        return ALScriptWeight::Target::SLua;
    }
    const std::string& target = doc.language.compileTarget;
    return target == "lsl2"                     ? std::optional(ALScriptWeight::Target::LSO)
           : target == "lsl-luau"               ? std::optional(ALScriptWeight::Target::LSLLuau)
           : target == "mono" || target.empty() ? std::optional(ALScriptWeight::Target::Mono)
                                                : std::nullopt;
}

void ALFloaterScriptStudio::weigh(Doc& doc)
{
    if (!weightTarget(doc))
    {
        doc.weight.reset();
        return;
    }
    doc.weighing = true;
    askAnalyzer(doc, ALScriptAnalysis::Kind::Weigh, ALTextPos());
}

void ALFloaterScriptStudio::weighed(Doc& doc, const ALScriptAnalysis::Result& result)
{
    doc.weighing = false;
    if (result.version != doc.editor->document().version() || result.weights.empty())
    {
        return;
    }
    // In the source's places, through the expansion the question was
    // asked over -- which answered() has made sure is the one there is.
    doc.weights.clear();
    for (const ALScriptWeight& weight : result.weights)
    {
        doc.weights.push_back(preprocessed(doc) && doc.expanded.valid ? weight.inSource(doc.expanded.map) : weight);
    }
    doc.weightsVersion = result.version;
    keepSavedWeights(doc);
    if (&doc == active())
    {
        mWeightsStale = true;
    }
    // What a preprocessor's run made to be sent, weighed, says more of the
    // same text than its check does: it stands until the text changes.
    if (!doc.weightSent || doc.weightVersion != result.version)
    {
        doc.weight        = doc.weights.front();
        doc.weightVersion = result.version;
        doc.weightSent    = false;
        // What was weighed is what a save compiles where the preprocessor
        // does not run, or runs without the optimizer, which comes after
        // the check's expansion: SLua's is never optimized.
        doc.weightExact = !preprocessed(doc) || doc.language.lua || !gSavedSettings.getBOOL("ALScriptPreprocOptimizer");
        refreshProblems(doc);
        showWeightsInEditor(doc);
    }
    mSaving.warnOverWeight(doc);
}

void ALFloaterScriptStudio::weighSent(Doc& doc)
{
    const std::optional<ALScriptWeight::Target> target = weightTarget(doc);
    if (!target || !doc.uploaded.valid || doc.uploaded.version != doc.editor->document().version())
    {
        return;
    }
    ALScriptAnalysis::Request request;
    request.kind    = ALScriptAnalysis::Kind::Weigh;
    request.id      = doc.id;
    request.version = doc.uploaded.version;
    request.lua     = doc.language.lua;
    request.text    = doc.uploaded.disabled ? doc.editor->text() : doc.uploaded.text;
    request.targets = { *target };
    // What was weighed kept with the question, its map for the places: a
    // run since -- a setting changed while it was weighed -- is of another
    // text, and weighs its own.
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptAnalysis::instance().ask(std::move(request), [handle, sent = doc.uploaded](const ALScriptAnalysis::Result& result) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(result.id) : NONE;
        if (index != NONE)
        {
            studio->weighedSent(*studio->mDocs[index], result, sent);
        }
    });
}

void ALFloaterScriptStudio::weighedSent(Doc& doc, const ALScriptAnalysis::Result& result, const Doc::Expanded& sent)
{
    const U32 version = doc.editor->document().version();
    if (result.version != version)
    {
        // The text moved on while it was weighed.
        return;
    }
    const ALScriptWeight* weight = result.weights.empty() ? nullptr : &result.weights.front();
    if (weight)
    {
        doc.weight        = weight->inSource(sent.map);
        doc.weightVersion = result.version;
        doc.weightExact   = true;
        doc.weightSent    = true;
        refreshProblems(doc);
        showWeightsInEditor(doc);
        if (&doc == active())
        {
            mWeightsStale = true;
        }
    }
    mSaving.warnOverWeight(doc);
}

std::vector<ALScriptWeight::Target> ALFloaterScriptStudio::weighedTargets(const Doc& doc) const
{
    const std::optional<ALScriptWeight::Target> own = weightTarget(doc);
    if (!own)
    {
        return {};
    }
    std::vector<ALScriptWeight::Target> targets = { *own };
    // Three compiles where one would do are the analyzer's time that a
    // completion waits behind: the other two only while they are looked
    // at.
    const bool in_front = mActive < mDocs.size() && mDocs[mActive].get() == &doc;
    if (!doc.language.lua && in_front && weightsShown())
    {
        for (const ALScriptWeight::Target other : { ALScriptWeight::Target::LSO, ALScriptWeight::Target::Mono, ALScriptWeight::Target::LSLLuau })
        {
            if (other != *own)
            {
                targets.push_back(other);
            }
        }
    }
    return targets;
}

void ALFloaterScriptStudio::keepSavedWeights(Doc& doc)
{
    if (doc.editor->isDirty() || doc.weightsVersion != doc.editor->document().version())
    {
        return;
    }
    // Each target's in place of what it weighed before, the others kept: a
    // target weighed only while the tab was looked at is counted from what
    // it was then.
    for (const ALScriptWeight& weight : doc.weights)
    {
        auto held = std::find_if(doc.weightsSaved.begin(), doc.weightsSaved.end(), [&weight](const ALScriptWeight& w) { return w.target == weight.target; });
        if (held != doc.weightsSaved.end())
        {
            *held = weight;
        }
        else
        {
            doc.weightsSaved.push_back(weight);
        }
    }
    if (&doc == active())
    {
        mWeightsStale = true;
    }
}

void ALFloaterScriptStudio::showWeightsInEditor(Doc& doc)
{
    if (!doc.editor)
    {
        return;
    }
    if (!mWeightNotes)
    {
        doc.editor->setLineNotes({});
    }
    if (!mWeightHeat)
    {
        doc.editor->setLineHeat({});
    }
    if ((!mWeightNotes && !mWeightHeat) || !doc.weight || doc.weightVersion != doc.editor->document().version())
    {
        return;
    }
    const ALScriptWeight&      weight = *doc.weight;
    LLStringUtil::format_map_t args;
    args["[TARGET]"] = ALScriptWeight::nameOf(weight.target);
    args["[LIMIT]"]  = std::to_string(weight.limit / 1024);
    const auto share = [&weight](size_t bytes) { return weight.limit ? llformat("%.1f%%", (F64)bytes * 100.0 / (F64)weight.limit) : std::string(); };
    // Said as weighed before the optimizer, where it was.
    const std::string before = doc.weightExact ? std::string() : " " + getString("WeightsHeadBefore");
    if (mWeightNotes)
    {
        // Each function, handler, state and global of the script's own, by
        // the line it is declared on; more than one on a line each by name.
        std::map<S32, std::vector<const ALScriptWeight::Part*>> by_line;
        for (const ALScriptWeight::Part& part : weight.parts)
        {
            const bool declared = part.kind == ALScriptWeight::Part::Kind::Function || part.kind == ALScriptWeight::Part::Kind::Handler ||
                                  part.kind == ALScriptWeight::Part::Kind::State || part.kind == ALScriptWeight::Part::Kind::Global;
            if (declared && part.file.empty() && part.line >= 0 && part.bytes > 0)
            {
                by_line[part.line].push_back(&part);
            }
        }
        std::vector<ALCodeEditor::LineNote> notes;
        for (const auto& [line, parts] : by_line)
        {
            ALCodeEditor::LineNote note;
            note.line = line;
            for (const ALScriptWeight::Part* part : parts)
            {
                LLStringUtil::format_map_t said = args;
                said["[BYTES]"]                 = std::to_string(part->bytes);
                said["[SHARE]"]                 = share(part->bytes);
                if (part->name.empty())
                {
                    said["[NAME]"] = getString("WeightsPartUnnamed");
                }
                else if (!part->within.empty())
                {
                    LLStringUtil::format_map_t handler;
                    handler["[EVENT]"] = part->name;
                    handler["[STATE]"] = part->within;
                    said["[NAME]"]     = getString("WeightsPartHandler", handler);
                }
                else
                {
                    said["[NAME]"] = part->name;
                }
                std::string bytes = getString(weight.estimate ? "WeightNoteEstimate" : "WeightNote", said);
                if (parts.size() > 1)
                {
                    said["[NOTE]"] = bytes;
                    bytes          = getString("WeightNoteNamed", said);
                }
                note.text += (note.text.empty() ? "" : "  \xC2\xB7  ") + bytes;
                note.tip += (note.tip.empty() ? "" : "\n") + getString(weight.estimate ? "WeightNoteEstimateTip" : "WeightNoteTip", said);
            }
            note.tip += before;
            notes.push_back(std::move(note));
        }
        doc.editor->setLineNotes(notes);
    }
    if (mWeightHeat)
    {
        // Each of the script's own lines by the most any line came to, so
        // that the warmest is the heat's own colour; by its square root,
        // so that a line of a tenth of that is still seen.
        size_t most = 0;
        for (const ALScriptWeight::Line& line : weight.lines)
        {
            most = line.file.empty() ? std::max(most, line.bytes) : most;
        }
        std::vector<ALCodeEditor::LineHeat> heat;
        for (const ALScriptWeight::Line& line : weight.lines)
        {
            if (!line.file.empty() || line.bytes == 0 || most == 0)
            {
                continue;
            }
            LLStringUtil::format_map_t said = args;
            said["[LINE]"]                  = std::to_string(line.line + 1);
            said["[BYTES]"]                 = std::to_string(line.bytes);
            said["[SHARE]"]                 = share(line.bytes);
            heat.push_back({ line.line, std::sqrt(static_cast<F32>(line.bytes) / static_cast<F32>(most)),
                             getString(weight.estimate ? "WeightHeatEstimateTip" : "WeightHeatTip", said) + before });
        }
        doc.editor->setLineHeat(heat);
    }
}

bool ALFloaterScriptStudio::editedCopy(const Doc& doc, const std::vector<std::pair<ALTextRange, std::string>>& edits, std::string& out) const
{
    ALScriptFix fix;
    for (const auto& [range, text] : edits)
    {
        const ALTextRange at = range.normalised();
        fix.edits.push_back({ at.begin.line, at.begin.column, at.end.line, at.end.column, text });
    }
    if (preprocessed(doc) && !ALScriptFixes::intoExpansion(doc.expanded.map, fix))
    {
        return false;
    }
    const std::optional<std::string> made = ALScriptFixes::apply(preprocessed(doc) ? doc.expanded.text : doc.editor->text(), fix);
    if (!made)
    {
        return false;
    }
    out = *made;
    return true;
}

void ALFloaterScriptStudio::weighFixes(Doc& doc, U32 shown, const std::vector<ALCodeEditor::Fix>& fixes)
{
    const std::optional<ALScriptWeight::Target> target = weightTarget(doc);
    const U32                                   version = doc.editor->document().version();
    if (!target || (preprocessed(doc) && (!doc.expanded.valid || doc.expanded.version != version)))
    {
        return;
    }
    ALScriptAnalysis::Request request;
    request.kind    = ALScriptAnalysis::Kind::Weigh;
    request.id      = doc.id;
    request.version = version;
    request.lua     = doc.language.lua;
    request.targets = { *target };
    // What the analyzers read as it stands first, weighed with the rest so
    // that each is measured against the same weigher at the same moment.
    request.variants.push_back(preprocessed(doc) ? doc.expanded.text : doc.editor->text());
    std::vector<S32> variant_of(fixes.size(), -1);
    for (size_t i = 0; i < fixes.size(); ++i)
    {
        std::string copy;
        if (!fixes[i].suppress && !fixes[i].edits.empty() && editedCopy(doc, fixes[i].edits, copy))
        {
            variant_of[i] = static_cast<S32>(request.variants.size());
            request.variants.push_back(std::move(copy));
        }
    }
    if (request.variants.size() < 2)
    {
        return;
    }
    const LLHandle<LLFloater> handle = getHandle();
    const ALScriptWeight::Target weighed_for = *target;
    ALScriptAnalysis::instance().ask(std::move(request), [handle, shown, variant_of, weighed_for](const ALScriptAnalysis::Result& result) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(result.id) : NONE;
        if (index == NONE)
        {
            return;
        }
        Doc& doc = *studio->mDocs[index];
        if (result.version != doc.editor->document().version() || result.variantTotals.empty() || result.variantTotals.front() == 0)
        {
            return;
        }
        // What each would come to less, or more; nothing where it is the
        // same, or did not come to anything.
        const S64                  base     = S64(result.variantTotals.front());
        const bool                 estimate = weighed_for == ALScriptWeight::Target::Mono;
        std::vector<std::string>   notes(variant_of.size());
        LLStringUtil::format_map_t args;
        args["[TARGET]"] = ALScriptWeight::nameOf(weighed_for);
        for (size_t i = 0; i < variant_of.size(); ++i)
        {
            const S32 at = variant_of[i];
            if (at <= 0 || static_cast<size_t>(at) >= result.variantTotals.size() || result.variantTotals[static_cast<size_t>(at)] == 0)
            {
                continue;
            }
            const S64 change = S64(result.variantTotals[static_cast<size_t>(at)]) - base;
            if (change == 0)
            {
                continue;
            }
            args["[BYTES]"] = std::to_string(std::abs(change));
            notes[i]        = studio->getString(change < 0 ? (estimate ? "FixLighterEstimate" : "FixLighter") : (estimate ? "FixHeavierEstimate" : "FixHeavier"), args);
        }
        doc.editor->noteFixes(shown, notes);
    });
}

void ALFloaterScriptStudio::actionsAnswered(Doc& doc, const ALScriptAnalysis::Result& result, U32 expansion)
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
    doc.actions        = std::move(held.fixes);
    doc.actionsVersion = result.version;
    std::vector<ALCodeEditor::Fix> offered;
    for (size_t i = 0; i < doc.actions.size(); ++i)
    {
        const ALScriptFix& action = doc.actions[i];
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
    doc.editor->supplyActions(doc.actionsAsked, std::move(offered));
}

void ALFloaterScriptStudio::activate(size_t index, bool focus)
{
    if (index >= mDocs.size())
    {
        return;
    }
    mActive = index;
    showEditors();
    // Asked for, or the keyboard is in this window already -- in the
    // editor just hidden, where it would type into a tab out of sight.
    if (focus || gFocusMgr.childHasKeyboardFocus(this))
    {
        focusShown(*mDocs[index]);
    }
    fillTabs();
    refreshToolbar();
    // The problems are the script's in front, but for a tab opened by
    // following one of another script's: the list stays that script's,
    // to go on down. The references are whichever script's asked.
    if (mHoldPanes == 0 || mProblemsPane->listedId().empty() || indexOf(mProblemsPane->listedId()) == NONE)
    {
        mProblemsPane->fill(mDocs[index].get());
    }
    refreshOutline(*mDocs[index]);
    // The inspector is about this script now: told again once the caret
    // is seen. The bar at the bottom says so now -- a notecard's too, whose
    // caret is not watched -- rather than keep the last tab's path until
    // the caret moves.
    mDocs[index]->caretSeen = ALTextPos(-1, -1);
    mDocs[index]->inspectAt = ALTextPos(-1, -1);
    mSymbol->setText(LLStringUtil::null);
    refreshBreadcrumb(*mDocs[index]);
    refreshNotice();
}

void ALFloaterScriptStudio::fillTabs()
{
    // What the strip would say now: the facts a tab is drawn from. The
    // strip is filled only where one of them moved, since a keystroke
    // asks for this and a keystroke changes none of them but the dirty
    // mark.
    std::vector<TabFacts> facts;
    facts.reserve(mDocs.size());
    for (const std::unique_ptr<Doc>& doc : mDocs)
    {
        facts.push_back(tabFactsOf(*doc));
    }
    if (facts == mTabFacts && mTabFactsActive == mActive)
    {
        return;
    }
    mTabFacts       = facts;
    mTabFactsActive = mActive;

    std::vector<ALTabStrip::Tab> tabs;
    std::string                  chosen;
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        const Doc&      doc = *mDocs[i];
        ALTabStrip::Tab tab;
        tab.label   = doc.name;
        tab.value   = doc.id;
        tab.dirty   = facts[i].dirty;
        tab.preview = facts[i].preview;
        tab.image   = LLUI::getUIImage(facts[i].image);
        // A dot in the worst problem's colour, for a script with any.
        const S32 errors = facts[i].errors, warnings = facts[i].warnings;
        if (errors > 0 || warnings > 0)
        {
            static const LLUIColor error_color   = LLUIColorTable::instance().getColor("CodeMarkError", LLColor4::red);
            static const LLUIColor warning_color = LLUIColorTable::instance().getColor("CodeMarkWarning", LLColor4::yellow);
            tab.badge                            = errors > 0 ? error_color.get() : warning_color.get();
        }
        // The name first, then where it is. The strip halves a name that
        // does not fit, and the name it cut is the one thing you hover a
        // cut tab to read; saying only where the script lives answered a
        // question nobody had asked.
        const std::string where = !doc.file.empty() ? doc.file
                                  : doc.notecard    ? getString("TabNotecardTip")
                                  : doc.ref.inInventory() ? getString("TabInventoryTip")
                                                          : getString("TabObjectTip");
        tab.toolTip = doc.name + "\n" + where;
        if (doc.loaded && !doc.modifiable)
        {
            tab.toolTip += "\n" + getString("TabReadOnlyTip");
        }
        tabs.push_back(std::move(tab));
        if (i == mActive)
        {
            chosen = doc.id;
        }
    }
    mTabs->setTabs(std::move(tabs), chosen);
    // Every window says which script is in front, as a window of one did.
    const Doc* doc = active();
    setTitle(doc ? getString("WindowTitle") + " - " + doc->name : getString("WindowTitle"));
}

ALFloaterScriptStudio::TabFacts ALFloaterScriptStudio::tabFactsOf(const Doc& doc) const
{
    TabFacts facts;
    facts.id       = doc.id;
    facts.name     = doc.name;
    facts.dirty    = doc.unsaved();
    facts.preview  = doc.preview;
    facts.readOnly = doc.loaded && !doc.modifiable;
    facts.image    = imageNameOf(doc);
    problemCounts(doc, facts.errors, facts.warnings);
    return facts;
}

void ALFloaterScriptStudio::problemCounts(const Doc& doc, S32& errors, S32& warnings) const
{
    errors = warnings = 0;
    for (const Doc::Shown& shown : doc.shown)
    {
        errors += shown.level == Doc::Level::Error ? 1 : 0;
        warnings += shown.level == Doc::Level::Warning ? 1 : 0;
    }
}

void ALFloaterScriptStudio::refreshTrailer(Doc& doc)
{
    // The view in front's caret: the expansion's own line, while it is
    // the one being read.
    const ALCodeEditor&         shown = *doc.shownText();
    const ALTextPos             caret = shown.caret();
    const ALTextDocument&       text  = shown.document();
    LLStringUtil::format_map_t args;
    args["[LINE]"]  = std::to_string(caret.line + 1);
    // Where the caret is as it is seen -- a character a column, a tab to
    // its stop -- rather than its byte in the line.
    args["[COL]"]   = std::to_string(text.displayColumn(caret, shown.getTabWidth()) + 1);
    std::vector<ALJumpBar::TrailerPart> parts;
    // A script that may be read and not changed says so for as long as it
    // is in front, not only in the status line as it arrives.
    if (doc.loaded && !doc.modifiable)
    {
        parts.push_back({ getString("TrailerReadOnly"), std::string(), getString("TrailerReadOnlyTip") });
    }
    if (!mVim.banner().empty())
    {
        parts.push_back({ mVim.banner(), std::string(), std::string() });
    }
    parts.push_back({ getString("CaretPosition", args), "line", mTrailerLineTip });
    // What is selected: lines across lines, characters within one.
    const ALTextRange selection = shown.selection().normalised();
    if (!selection.empty())
    {
        if (selection.begin.line != selection.end.line)
        {
            const S32 lines = selection.end.line - selection.begin.line + (selection.end.column > 0 ? 1 : 0);
            parts.push_back({ counted("SelectedLines", lines), std::string(), std::string() });
        }
        else
        {
            // Characters as they are seen, not the bytes they are
            // written in; a tab is one.
            parts.push_back({ counted("SelectedChars", text.displayColumn(selection.end, 1) - text.displayColumn(selection.begin, 1)), std::string(),
                              std::string() });
        }
    }
    S32 errors = 0, warnings = 0;
    problemCounts(doc, errors, warnings);
    if (errors > 0)
    {
        parts.push_back({ counted("ProblemErrors", errors), "problems", mTrailerProblemsTip });
    }
    if (warnings > 0)
    {
        parts.push_back({ counted("ProblemWarnings", warnings), "problems", mTrailerProblemsTip });
    }
    // While the Preprocessed view is in front, where the optimizer ran over
    // the text as it stands: what its code weighed before the optimizer and
    // after, the whole of what that view shows it did.
    const bool optimized = doc.shownView() == Doc::View::Expanded && doc.uploaded.valid && doc.uploaded.version == doc.editor->document().version() &&
                           doc.uploaded.codeBefore > 0 && doc.uploaded.codeAfter > 0 && weightTarget(doc);
    if (optimized)
    {
        const ALScriptWeight::Target target = *weightTarget(doc);
        const size_t                 limit  = ALScriptWeight::limitOf(target);
        LLStringUtil::format_map_t   args;
        args["[TARGET]"]      = ALScriptWeight::nameOf(target);
        args["[BEFORE]"]      = llformat("%.1f", (F64)doc.uploaded.codeBefore / 1024.0);
        args["[AFTER]"]       = llformat("%.1f", (F64)doc.uploaded.codeAfter / 1024.0);
        args["[LIMIT]"]       = std::to_string(limit / 1024);
        args["[BYTESBEFORE]"] = std::to_string(doc.uploaded.codeBefore);
        args["[BYTESAFTER]"]  = std::to_string(doc.uploaded.codeAfter);
        args["[MAX]"]         = std::to_string(limit);
        const bool estimate   = target == ALScriptWeight::Target::Mono;
        ALJumpBar::TrailerPart part{ getString(estimate ? "TrailerOptimizedEstimate" : "TrailerOptimized", args), std::string(),
                                     getString(estimate ? "TrailerOptimizedEstimateTip" : "TrailerOptimizedTip", args) };
        if (doc.uploaded.codeAfter > limit)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Error);
        }
        else if (doc.uploaded.codeAfter * 5 > limit * 4)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Warning);
        }
        parts.push_back(std::move(part));
    }
    // What its code weighs for its target, against what the target runs
    // it in: in the warning colour past four fifths, the error's past it.
    if (!optimized && doc.weight && doc.weight->total > 0)
    {
        const ALScriptWeight&      weight = *doc.weight;
        LLStringUtil::format_map_t args;
        args["[TARGET]"] = ALScriptWeight::nameOf(weight.target);
        args["[SIZE]"]   = llformat("%.1f", (F64)weight.total / 1024.0);
        args["[LIMIT]"]  = std::to_string(weight.limit / 1024);
        args["[BYTES]"]  = std::to_string(weight.total);
        args["[MAX]"]    = std::to_string(weight.limit);
        std::string tip  = getString(weight.estimate ? "TrailerWeightEstimateTip" : "TrailerWeightTip", args);
        if (!doc.weightExact)
        {
            tip += " " + getString("TrailerWeightBeforeTip");
        }
        else if (doc.weightSent)
        {
            tip += " " + getString("TrailerWeightSentTip");
        }
        ALJumpBar::TrailerPart part{ getString(weight.estimate ? "TrailerWeightEstimate" : "TrailerWeight", args), std::string(), tip };
        if (weight.total > weight.limit)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Error);
        }
        else if (weight.total * 5 > weight.limit * 4)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Warning);
        }
        parts.push_back(std::move(part));
    }
    // What a save would send, once it is past half of what a script -- or
    // a notecard's text -- may be: in the warning colour past nine tenths,
    // the error's past the whole, where a save is refused.
    const size_t LIMIT = doc.notecard ? static_cast<size_t>(LLNotecard::MAX_SIZE) : ALScriptEnvelope::MAX_ASSET_BYTES;
    if (doc.assetBytes * 2 > LIMIT)
    {
        LLStringUtil::format_map_t size;
        size["[SIZE]"]  = std::to_string((doc.assetBytes + 1023) / 1024);
        size["[LIMIT]"] = std::to_string(LIMIT / 1024);
        size["[BYTES]"] = std::to_string(doc.assetBytes);
        size["[MAX]"]   = std::to_string(LIMIT);
        size["[OVER]"]  = std::to_string(doc.assetBytes > LIMIT ? doc.assetBytes - LIMIT : 0);
        const char*            tip = doc.notecard ? (doc.assetBytes > LIMIT ? "TrailerNotecardSizeOverTip" : "TrailerNotecardSizeTip")
                                                      : (doc.assetBytes > LIMIT ? "TrailerSizeOverTip" : "TrailerSizeTip");
        ALJumpBar::TrailerPart part{ getString("TrailerSize", size), std::string(), getString(tip, size) };
        if (doc.assetBytes > LIMIT)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Error);
        }
        else if (doc.assetBytes * 10 > LIMIT * 9)
        {
            part.color = doc.editor->markColor(ALCodeEditor::Mark::Warning);
        }
        parts.push_back(std::move(part));
    }
    // Which of the two it is, where the script has an expansion to show:
    // pressed, the other.
    if (doc.expandedEditor)
    {
        const bool expanded = doc.shownView() == Doc::View::Expanded;
        parts.push_back({ getString(expanded ? "TrailerExpanded" : "TrailerSource"), "expanded", expanded ? mTrailerExpandedTip : mTrailerSourceTip });
    }
    // Joined by a middle dot with air around it; in code, since a
    // string of the skin's is trimmed of its spaces.
    std::vector<ALJumpBar::TrailerPart> said;
    for (ALJumpBar::TrailerPart& part : parts)
    {
        if (!said.empty())
        {
            said.push_back({ "   \xC2\xB7   ", std::string(), std::string() });
        }
        said.push_back(std::move(part));
    }
    mBreadcrumb->setTrailer(std::move(said));
}

void ALFloaterScriptStudio::measureAsset(Doc& doc)
{
    const U32 version   = doc.editor->document().version();
    const U32 expansion = doc.expanded.valid ? doc.expanded.generation : 0;
    if (doc.assetMeasured == std::make_pair(version, expansion))
    {
        return;
    }
    doc.assetMeasured = std::make_pair(version, expansion);
    doc.assetBytes    = 0;
    // A file on disk is saved to the disk, which has no limit.
    if (!doc.loaded || !doc.file.empty())
    {
        return;
    }
    // A notecard's text against the notecard's own limit.
    if (doc.notecard)
    {
        doc.assetBytes = doc.editor->document().byteCount();
        return;
    }
    const std::string text = doc.editor->text();
    doc.assetBytes         = text.size();
    if (preprocessed(doc) && doc.expanded.valid && doc.expanded.version == version && !doc.expanded.disabled)
    {
        ALScriptEnvelope envelope = doc.envelope ? *doc.envelope : ALScriptEnvelope();
        envelope.lua              = doc.language.lua;
        envelope.source           = text;
        envelope.expanded         = doc.expanded.text;
        envelope.compileTarget    = doc.language.compileTarget;
        envelope.programVersion   = LLVersionInfo::instance().getChannelAndVersion();
        envelope.lastCompiled     = LLDate::now().asString();
        doc.assetBytes            = envelope.wrap().size();
    }
}

void ALFloaterScriptStudio::onTrailerChosen(const std::string& value)
{
    if (value == "line")
    {
        goToLine();
    }
    else if (value == "problems")
    {
        showBottom("problems_tab", true);
    }
    else if (value == "expanded")
    {
        toggleExpanded();
    }
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
        if (action == "keep")
        {
            const Doc* doc = active();
            return doc && doc->preview;
        }
        return active() != nullptr;
    });
    LLContextMenu* menu = LLUICtrlFactory::createFromFile<LLContextMenu>("menu_script_studio_tab.xml", LLMenuGL::sMenuContainer,
                                                                          LLMenuHolderGL::child_registry_t::instance());
    if (!menu)
    {
        return;
    }
    // Each other studio window open, to move the tab into, after moving it
    // to a window of its own.
    S32 at = 0;
    for (U32 i = 0; i < menu->getItemCount(); ++i)
    {
        if (menu->getItem(static_cast<S32>(i)) && menu->getItem(static_cast<S32>(i))->getName() == "pop_out")
        {
            at = static_cast<S32>(i) + 1;
        }
    }
    for (LLFloater* floater : LLFloaterReg::getFloaterList("script_studio"))
    {
        ALFloaterScriptStudio* other = ALViewType::as<ALFloaterScriptStudio>(floater);
        if (!other || other == this || !other->getVisible())
        {
            continue;
        }
        LLStringUtil::format_map_t args;
        args["[WINDOW]"] = other->getTitle();
        LLMenuItemCallGL::Params p;
        p.name                   = "move_to_" + other->getKey().asString();
        p.label                  = getString("MoveToWindow", args);
        LLMenuItemCallGL*         item   = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
        const LLHandle<LLFloater> target = other->getHandle();
        item->setClickCallback([this, target](LLUICtrl*, const LLSD&) {
            moveActiveTo(ALViewType::as<ALFloaterScriptStudio>(target.get()));
        });
        menu->insert(at++, item);
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
    // What only the tab's menu has; the rest are the menu bar's commands,
    // by the same names, about the tab in front, which a right-click chose.
    if (action == "keep")
    {
        holdPreview(*doc);
    }
    else if (action == "close_others" || action == "close_all" || action == "close_saved")
    {
        closeTabs(action, doc);
    }
    else if (action == "copy_name")
    {
        LLClipboard::instance().copyToClipboard(doc->name, 0, static_cast<S32>(doc->name.size()));
    }
    else
    {
        mCommands.run(action);
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
    reindexDocs();
    mActive = active_id.empty() ? NONE : indexOf(active_id);
    fillTabs();
}

void ALFloaterScriptStudio::moveTab(S32 direction)
{
    if (mActive == NONE)
    {
        return;
    }
    const S64 to = static_cast<S64>(mActive) + (direction > 0 ? 1 : -1);
    if (to < 0 || to >= static_cast<S64>(mDocs.size()))
    {
        return;
    }
    // As a drag along the strip would have left them.
    std::vector<std::string> order;
    for (const std::unique_ptr<Doc>& doc : mDocs)
    {
        order.push_back(doc->id);
    }
    std::swap(order[mActive], order[static_cast<size_t>(to)]);
    onTabsReordered(order);
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
    // What it compiles for is a script's in the world: a notecard, a file
    // on disk and no tab at all have nothing to say there.
    const bool script  = doc && !doc->notecard && doc->file.empty();
    mCompileTarget->setVisible(script);
    mCompileTarget->setEnabled(have && script && doc->modifiable);
    // Whether anything is unsaved: the one fact here that every
    // keystroke can move, and the only one that costs a walk.
    mSaveButton->setEnabled(have && doc->modifiable && !doc->save.sending());
    bool anyDirty = false;
    for (const std::unique_ptr<Doc>& each : mDocs)
    {
        anyDirty = anyDirty || (each->unsaved() && each->modifiable);
    }
    mSaveAllButton->setEnabled(anyDirty);
    mUndoButton->setEnabled(doc && doc->shownText()->canUndo());
    mRedoButton->setEnabled(doc && doc->shownText()->canRedo());
    mFindButton->setEnabled(doc != nullptr);
    mFormatButton->setEnabled(have && doc->modifiable && !doc->notecard && doc->shownView() == Doc::View::Source);
    mExpandedButton->setEnabled(doc && doc->expandedEditor != nullptr);
    mExpandedButton->setToggleState(doc && doc->shownView() == Doc::View::Expanded);
    mRunning->setVisible(task);
    mResetButton->setVisible(task);
    refreshExperience();
    if (task)
    {
        mRunning->set(doc->running == 1);
    }
    if (have && script)
    {
        // The targets of the script's own language: Lua is a Lua script's
        // one, and the LSL machines an LSL script's, LSL on Luau where the
        // region runs Luau.
        const bool region_lua = ALScriptWorkspace::luaEnabled(doc->ref);
        const bool lua        = doc->language.lua;
        for (const std::string target : { "mono", "lsl2", "lsl-luau", "luau" })
        {
            if (LLScrollListItem* item = mCompileTarget->findItemByValue(target))
            {
                item->setEnabled(lua ? target == "luau" : target != "luau" && (target != "lsl-luau" || region_lua));
            }
        }
        mCompileTarget->setValue(doc->language.compileTarget);
    }
    // The breadcrumb runs up to whatever of the script's own controls are
    // showing at the strip's right, and no further: where none are, it has
    // the strip.
    if (mBreadcrumb)
    {
        const LLView* first = task && mExperience->getVisible() ? static_cast<const LLView*>(mExperience)
                              : task                            ? static_cast<const LLView*>(mResetButton)
                              : script                          ? static_cast<const LLView*>(mCompileTarget)
                                                                : nullptr;
        const S32     right = first ? first->getRect().mLeft - 6 : mBreadcrumb->getParent()->getRect().getWidth();
        const LLRect  crumbs = mBreadcrumb->getRect();
        if (crumbs.mRight != right && right > crumbs.mLeft)
        {
            mBreadcrumb->reshape(right - crumbs.mLeft, crumbs.getHeight());
            mBreadcrumb->setOrigin(crumbs.mLeft, crumbs.mBottom);
        }
    }
}

void ALFloaterScriptStudio::onTabChosen(const std::string& value)
{
    activate(indexOf(value));
}

// --- saving and compiling ------------------------------------------------------

// --- the analyzers -------------------------------------------------------------

bool ALFloaterScriptStudio::lslFragment(const Doc& doc) const
{
    if (doc.file.empty() || doc.language.lua || doc.notecard)
    {
        return false;
    }
    // A default state, by the grammar's tokens: `default` then `{`, past
    // blanks and comments, the brace on the same line or a later one.
    ALCodeEditor&   editor  = *doc.editor;
    bool            waiting = false;
    const S32       lines   = editor.document().lineCount();
    for (S32 line = 0; line < lines; ++line)
    {
        const std::string& text = editor.document().line(line);
        for (const ALSyntaxToken& token : editor.highlighter().tokens(line))
        {
            const std::string_view word = std::string_view(text).substr(token.begin, token.end - token.begin);
            if (token.kind == ALSyntaxKind::Comment || token.kind == ALSyntaxKind::DocComment || word.find_first_not_of(" \t") == std::string_view::npos)
            {
                continue;
            }
            if (waiting && word.front() == '{')
            {
                return false;
            }
            waiting = token.kind == ALSyntaxKind::Control && word == "default";
        }
    }
    return true;
}

void ALFloaterScriptStudio::scheduleAnalysis(Doc& doc, bool now)
{
    // A file on disk is checked as what it is: a Lua module, or an LSL
    // script, or an LSL include, which is checked with a state put after
    // it (lslFragment).
    if (!doc.loaded || doc.notecard)
    {
        return;
    }
    doc.analysisDue = now ? 1.0 : static_cast<F64>(LLTimer::getTotalSeconds()) + ANALYSIS_DELAY;
}

void ALFloaterScriptStudio::pumpPreprocessor()
{
    if (mPreprocessorDue <= 0.0 || LLTimer::getTotalSeconds() < mPreprocessorDue)
    {
        return;
    }
    const bool words   = mPreprocessorWords;
    mPreprocessorDue   = 0.0;
    mPreprocessorWords = false;
    // What the analyzers see changes with the settings, and what the
    // editors colour as the transforms' words.
    for (std::unique_ptr<Doc>& doc : mDocs)
    {
        doc->expanded.valid = false;
        // What a run made to be sent is made differently now: the check
        // weighs the text again, and the run that follows as it is sent.
        doc->weightSent = false;
        if (words && !doc->notecard && !doc->language.lua)
        {
            teachWords(*doc->editor, false);
        }
        if (preprocessed(*doc))
        {
            mSaving.preprocess(*doc);
        }
        scheduleAnalysis(*doc, true);
    }
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

namespace
{
    // A warning that something declared is never used: LSL's, by its
    // number, and Luau's lints, by their names.
    bool unusedWarning(const ALScriptProblem& problem)
    {
        return problem.severity == ALScriptProblem::Severity::Warning &&
               (problem.code == "20009" || problem.code == "LocalUnused" || problem.code == "FunctionUnused" || problem.code == "ImportUnused");
    }
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
    // LSL's warnings as the scripter chose them; Luau's lints were chosen
    // in the configuration the check ran with.
    if (!doc.language.lua)
    {
        ALScriptLints::apply(doc.analysis);
    }
    doc.definitionsError = result.definitionsError;
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
    // An include checked with a state after it: what is said of the
    // state, and that what it declares goes unused, is not the include's.
    const bool fragment = lslFragment(doc);
    if (fragment)
    {
        const bool mapped_now = preprocessed(doc) && doc.expanded.valid && doc.expanded.version == result.version;
        const S32  own_lines  = mapped_now ? static_cast<S32>(std::count(doc.expanded.text.begin(), doc.expanded.text.end(), '\n')) + 1
                                           : doc.editor->document().lineCount();
        doc.analysis.erase(std::remove_if(doc.analysis.begin(), doc.analysis.end(),
                                          [own_lines](const ALScriptProblem& problem) { return problem.line >= own_lines || unusedWarning(problem); }),
                           doc.analysis.end());
        if (result.understood)
        {
            doc.outline.erase(std::remove_if(doc.outline.begin(), doc.outline.end(),
                                             [own_lines](const ALScriptOutlineEntry& entry) { return entry.nameSpan.line >= own_lines; }),
                              doc.outline.end());
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
    if (result.understood)
    {
        doc.editor->setSemanticTokens(std::move(semantics));
    }
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
            writable = writable && map->verbatimSpan(hint.line, hint.column, hint.column, begin, end) && begin.file == 0 && begin.line == loc.line &&
                       begin.column == loc.column;
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
    if (result.understood)
    {
        doc.editor->setInlayHints(std::move(hints));
    }
    if (mapped)
    {
        // Back to the source: a problem in an include keeps its file, and
        // what an include declares is the include's to outline. What an
        // include declares and this script does not use is no problem of
        // this script's: a library is meant to hold more than any one
        // script calls, and every script including it would be told so.
        const ALSourceMap& map = doc.expanded.map;
        doc.analysis.erase(std::remove_if(doc.analysis.begin(), doc.analysis.end(),
                                          [&map](const ALScriptProblem& problem) {
                                              if (!unusedWarning(problem))
                                              {
                                                  return false;
                                              }
                                              const ALSourceMap::Loc loc = map.toSource(problem.line, problem.column);
                                              return loc.found() && loc.file > 0;
                                          }),
                           doc.analysis.end());
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
    refreshProblems(doc);
    refreshOutline(doc);
    // Weighed a moment after, of the same text.
    weigh(doc);
    if (doc.fixAllAfterCheck && doc.analysisVersion == doc.editor->document().version())
    {
        FixPick pick;
        pick.key = *doc.fixAllAfterCheck;
        doc.fixAllAfterCheck.reset();
        askFixAll(doc, pick);
    }
    if (doc.save.checked())
    {
        mSaving.save(doc);
    }
}

void ALFloaterScriptStudio::offerImports(Doc& doc)
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
    if (std::none_of(doc.analysis.begin(), doc.analysis.end(), unknown))
    {
        return;
    }
    const ALScriptPreprocessor::Request request = preprocessRequest(doc, /*with_source*/ false);
    const std::string self = ALScriptModules::identity(request.path.empty() ? ALScriptPreprocessor::pathOf(request.ref) : request.path);
    const std::string                   text         = doc.editor->text();
    ALScriptPreprocessor&               preprocessor = ALScriptPreprocessor::instance();
    // The texts of the script's language open here, as they are being
    // written.
    const auto open = [this, &doc, lua]() {
        std::vector<ALScriptModules::Open> out;
        for (const auto& other : mDocs)
        {
            if (other.get() != &doc && other->loaded && other->language.lua == lua)
            {
                out.push_back({ other->file.empty() ? ALScriptPreprocessor::pathOf(other->ref) : "disk:" + other->file, other->name, other->editor->text() });
            }
        }
        return out;
    };
    const auto offer = [&text, lua](ALScriptProblem& problem, const std::string& module, bool field) {
        if (lua)
        {
            ALScriptFixes::offerRequire(problem, text, module, field);
        }
        else
        {
            ALScriptFixes::offerInclude(problem, text, module);
        }
    };
    // What the index knows of the names asked for: each module so named,
    // or that exports or declares one of them.
    std::vector<std::string> names;
    for (const ALScriptProblem& problem : doc.analysis)
    {
        if (unknown(problem) && std::find(names.begin(), names.end(), problem.args[0]) == names.end())
        {
            names.push_back(problem.args[0]);
        }
    }
    const std::vector<ALScriptModules::Module> modules = ALScriptModules::instance().giving(request, open, names);
    bool                                       given_all = true;
    for (ALScriptProblem& problem : doc.analysis)
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
            const ALPreprocessor::Found named = preprocessor.lookUp(request, ask, found);
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
        const LLHandle<LLFloater> handle = getHandle();
        const std::string         id     = doc.id;
        ALScriptModules::instance().fetchNearby(request, [handle, id]() {
            ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
            const size_t           index  = studio ? studio->indexOf(id) : NONE;
            if (index != NONE)
            {
                studio->scheduleAnalysis(*studio->mDocs[index], true);
            }
        });
    }
}

void ALFloaterScriptStudio::explainTransformWords(Doc& doc)
{
    // A parse error on one of the preprocessor's words, with its transform
    // off, is the transform's to explain (ALPreprocessor::transformAt).
    static LLCachedControl<bool> switches(gSavedSettings, "ALScriptPreprocSwitch", false);
    static LLCachedControl<bool> extensions(gSavedSettings, "ALScriptPreprocExtensions", false);
    using Transform                     = ALPreprocessor::Transform;
    const bool            preprocessing = ALScriptPreprocessor::enabled();
    const ALTextDocument& text          = doc.editor->document();
    const auto            line          = [&text](S32 index) { return std::string_view(text.line(index)); };
    for (ALScriptProblem& problem : doc.analysis)
    {
        if (problem.severity != ALScriptProblem::Severity::Error || !problem.file.empty())
        {
            continue;
        }
        std::string     word;
        const Transform transform = ALPreprocessor::transformAt(line, text.lineCount(), problem.line, word);
        const bool      on        = preprocessing && (transform == Transform::Switch ? switches() : extensions());
        if (transform == Transform::None || on)
        {
            continue;
        }
        LLStringUtil::format_map_t args;
        args["[WORD]"] = word;
        problem.message += " " + getString(transform == Transform::Switch ? "PreprocHintSwitch" : "PreprocHintExtensions", args);
    }
}

void ALFloaterScriptStudio::explainRequires(Doc& doc)
{
    // Luau's own globals have a require, so nothing else says so: the
    // script runs until the call, and stops there. A file is not what goes
    // up, and a notecard is read into a script that is preprocessed.
    if (doc.notecard || !doc.file.empty() || preprocessed(doc))
    {
        return;
    }
    for (const ALPreprocessor::Required& required : ALPreprocessor::requiresIn(doc.editor->text()))
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
        problem.message = getString("RequireNotPreprocessed", args);
        doc.analysis.push_back(std::move(problem));
    }
}

void ALFloaterScriptStudio::slideProblems(Doc& doc, const ALTextDocument::Edit& edit)
{
    if (doc.problems.empty() && doc.runtime.empty())
    {
        return;
    }
    const ALTextRange range = edit.range.normalised();
    const S32         first = range.begin.line;
    const S32         last  = range.end.line;
    const S32         delta = static_cast<S32>(std::count(edit.inserted.begin(), edit.inserted.end(), '\n')) - (last - first);
    auto              slide = [&](auto& list) {
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](auto& problem) {
                                      if (!problem.file.empty() || problem.line < first)
                                      {
                                          return false;
                                      }
                                      if (problem.line <= last)
                                      {
                                          return true;
                                      }
                                      problem.line += delta;
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

void ALFloaterScriptStudio::slideOutline(Doc& doc, const ALTextDocument::Edit& edit)
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

void ALFloaterScriptStudio::refreshProblems(Doc& doc)
{
    ALScriptProblemsPane::Making making;
    making.target      = weightTarget(doc);
    making.includeName = [this, &doc](const std::string& path) { return includeName(doc, path); };
    ALScriptProblemsPane::Made made = ALScriptProblemsPane::make(doc, *this, making);
    doc.shown                       = std::move(made.rows);
    doc.editor->clearMarks();
    for (const auto& [line, mark] : made.marks)
    {
        if (doc.editor->markAt(line) < mark)
        {
            doc.editor->setMark(line, mark);
        }
    }
    // The gutter's word on what a line offers: a lightbulb where the caret
    // is, a round mark where a fix changes the script.
    for (const auto& [line, changes] : made.fixable)
    {
        doc.editor->setFixable(line, true, changes || doc.editor->changesAt(line));
    }
    doc.editor->setDecorations(std::move(made.decorations));
    mProblemsPane->changed(doc);
    measureAsset(doc);
    if (&doc == active())
    {
        refreshTrailer(doc);
    }
    fillTabs();
}

std::string ALFloaterScriptStudio::problemIcon(const Doc& doc, const std::string& include) const
{
    return include.empty() ? imageNameOf(doc) : includeImage(include, doc.language.lua);
}

void ALFloaterScriptStudio::fixAllOfKind(Doc& doc, const std::string& key)
{
    askFixAll(doc, FixPick{ key });
}

bool ALFloaterScriptStudio::isLint(bool lua, const std::string& id) const
{
    for (const ALScriptLints::Lint& one : ALScriptLints::all())
    {
        if (one.lua == lua && one.id == id)
        {
            return true;
        }
    }
    return false;
}

ALScriptLints::Level ALFloaterScriptStudio::lintLevel(bool lua, const std::string& id) const
{
    return ALScriptLints::level(lua, id);
}

void ALFloaterScriptStudio::setLintLevel(bool lua, const std::string& id, ALScriptLints::Level level)
{
    // The scripts are checked again as the setting changes.
    ALScriptLints::setLevel(lua, id, level);
}

void ALFloaterScriptStudio::showLintSettings()
{
    LLFloaterReg::showInstance("script_studio_prefs", LLSD().with("tab", "lints"));
}

void ALFloaterScriptStudio::refreshBottomTabs()
{
    if (!mBottomTabs)
    {
        return;
    }
    const auto title = [this](const char* tab, const std::string& said) {
        LLPanel* panel = mBottomTabs->getChild<LLPanel>(tab);
        const S32 index = mBottomTabs->getIndexForPanel(panel);
        if (index >= 0 && mBottomTabs->getPanelTitle(index) != said)
        {
            mBottomTabs->setPanelTitle(index, said);
        }
    };
    LLStringUtil::format_map_t args;
    const S32 held  = mProblemsPane ? mProblemsPane->held() : 0;
    args["[COUNT]"] = std::to_string(held);
    title("problems_tab", getString(held > 0 ? "TabProblemsCount" : "TabProblems", args));
    args["[COUNT]"] = std::to_string(mFound.places.size());
    title("references_tab", getString(mFound.places.empty() ? "TabReferences" : "TabReferencesCount", args));
    title("output_tab", getString(mOutputPane && mOutputPane->unread() ? "TabOutputUnread" : "TabOutput"));
}

std::vector<ALTextPos> ALFloaterScriptStudio::problemPlaces(const Doc& doc) const
{
    // The script's own problems, each place once and in order; not the
    // note about the definitions, which is about no place.
    const std::string      definitions = getString("OriginDefinitions");
    std::vector<ALTextPos> places;
    for (const Doc::Shown& row : doc.shown)
    {
        if (row.file.empty() && row.origin != definitions)
        {
            places.push_back(doc.editor->document().clamp(ALTextPos(row.line, row.hasColumn ? row.column : 0)));
        }
    }
    std::sort(places.begin(), places.end());
    places.erase(std::unique(places.begin(), places.end()), places.end());
    return places;
}

void ALFloaterScriptStudio::goToProblemAt(Doc& doc, const ALTextPos& to)
{
    noteJump();
    ALCodeEditor& source = sourceInFront(doc);
    source.goTo(ALTextRange(to, to));
    source.setFocus(true);
    showProblemCard(doc, to);
}

bool ALFloaterScriptStudio::goToProblemNumber(Doc& doc, S32 number)
{
    const std::vector<ALTextPos> places = problemPlaces(doc);
    if (places.empty())
    {
        return false;
    }
    // Counted from 1, or back from -1 the last, past either end the one
    // at that end; 0 the one at the caret or the next after it.
    const S32 count = static_cast<S32>(places.size());
    S32       index = number > 0 ? llmin(number, count) - 1 : llmax(count + number, 0);
    if (number == 0)
    {
        const ALTextPos caret = sourceInFront(doc).selection().normalised().begin;
        index = static_cast<S32>(std::lower_bound(places.begin(), places.end(), caret) - places.begin()) % count;
    }
    goToProblemAt(doc, places[index]);
    return true;
}

void ALFloaterScriptStudio::goToProblem(Doc& doc, S32 direction)
{
    const std::vector<ALTextPos> places = problemPlaces(doc);
    if (places.empty())
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString("NoProblemsHere", args));
        return;
    }
    // From the caret, round past the end to the other: the source's, where
    // the problems are.
    ALCodeEditor&     source    = sourceInFront(doc);
    const ALTextRange selection = source.selection().normalised();
    ALTextPos         to        = direction > 0 ? places.front() : places.back();
    if (direction > 0)
    {
        const auto next = std::upper_bound(places.begin(), places.end(), selection.begin);
        if (next != places.end())
        {
            to = *next;
        }
    }
    else
    {
        const auto next = std::lower_bound(places.begin(), places.end(), selection.begin);
        if (next != places.begin())
        {
            to = *(next - 1);
        }
    }
    goToProblemAt(doc, to);
}

void ALFloaterScriptStudio::showProblemCard(Doc& doc, const ALTextPos& at)
{
    // What is wrong there, in the card the mouse would bring up.
    std::vector<ALCodeEditor::CardProblem> problems;
    ALTextRange                            about;
    for (const ALCodeEditor::Decoration& decoration : doc.editor->decorations())
    {
        const ALTextRange range = decoration.range.normalised();
        if (!decoration.message.empty() && range.begin <= at && (at < range.end || range.begin == at))
        {
            problems.push_back({ decoration.message, decoration.color });
            about = about.empty() ? range : ALTextRange(std::min(about.begin, range.begin), std::max(about.end, range.end));
        }
    }
    if (!problems.empty())
    {
        doc.editor->showCard(about, std::string(), problems);
    }
}

void ALFloaterScriptStudio::noLint(Doc& doc)
{
    // The script's own lines, as they stand at the check: a problem in an
    // include is said of text this tab does not hold.
    const ALTextDocument& text     = doc.editor->document();
    const bool            lua      = doc.language.lua;
    const auto            ours     = [&text](const ALScriptProblem& problem) {
        return problem.file.empty() && problem.line >= 0 && problem.line < text.lineCount();
    };
    doc.analysis.erase(std::remove_if(doc.analysis.begin(), doc.analysis.end(),
                                      [&](const ALScriptProblem& problem) {
                                          return ours(problem) && ALScriptFixes::suppressed(problem, text.line(problem.line),
                                                                                            problem.line > 0 ? std::string_view(text.line(problem.line - 1)) : std::string_view(),
                                                                                            lua);
                                      }),
                       doc.analysis.end());
    for (ALScriptProblem& problem : doc.analysis)
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

bool ALFloaterScriptStudio::applyFix(Doc& doc, const ALScriptFix& fix, U32 version)
{
    if (!doc.loaded || !doc.modifiable || fix.edits.empty())
    {
        return false;
    }
    // In the places of the text it was made over: a text typed in since is
    // checked again, and its fixes offered afresh.
    ALCodeEditor&         source = sourceInFront(doc);
    const ALTextDocument& text   = source.document();
    if (version != text.version())
    {
        setStatus(getString("FixStale"), true);
        scheduleAnalysis(doc, true);
        return false;
    }
    std::vector<std::pair<ALTextRange, std::string>> edits;
    for (const ALScriptEdit& edit : fix.edits)
    {
        const ALTextRange range(ALTextPos(edit.line, edit.column), ALTextPos(edit.endLine, edit.endColumn));
        if (text.clamp(range.begin) != range.begin || text.clamp(range.end) != range.end)
        {
            setStatus(getString("FixStale"), true);
            scheduleAnalysis(doc, true);
            return false;
        }
        edits.emplace_back(range, edit.text);
    }
    if (!source.replaceAll(std::move(edits)))
    {
        return false;
    }
    source.undoJournal().label(fix.kind == ALScriptFix::Kind::Refactor ? "refactor" : "fix");
    setStatus(fix.title);
    scheduleAnalysis(doc, true);
    return true;
}

// static
void ALFloaterScriptStudio::askFixAll(Doc& doc, const FixPick& pick)
{
    // Asked of a text not checked yet -- typed in a moment ago -- whose
    // problems are not known: checked first, and asked again then, rather
    // than said to have nothing to fix.
    if (!pick.forSave && doc.loaded && !doc.notecard && doc.analysisVersion != doc.editor->document().version())
    {
        doc.fixAllAfterCheck = pick.key;
        scheduleAnalysis(doc, true);
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString("FixChecking", args));
        return;
    }
    size_t                                left  = 0;
    const std::vector<const ALScriptFix*> fixes = doc.pickFixes(pick, &left);
    // What is not safe to make without a look, said to be left for one.
    const std::string left_said = left > 0 ? counted("FixesLeft", static_cast<S32>(left)) : std::string();
    if (fixes.size() < 2)
    {
        if (!fixes.empty())
        {
            if (applyFix(doc, *fixes.front(), doc.editor->document().version()) && !left_said.empty())
            {
                setStatus(fixes.front()->title + " " + left_said);
            }
        }
        else
        {
            setStatus(left_said.empty() ? getString("FixNone") : getString("FixNoneSafe") + " " + left_said);
        }
        return;
    }
    // Asked first, as Replace All asks: many changes at once, said as many.
    LLSD args;
    args["FIXES"]                    = counted("Fixes", static_cast<S32>(fixes.size()));
    args["NAME"]                     = doc.name;
    args["EXAMPLE"]                  = fixes.front()->title;
    args["LEFT"]                     = left_said.empty() ? std::string() : " " + left_said;
    const LLHandle<LLFloater> handle = getHandle();
    const std::string         id     = doc.id;
    LLNotificationsUtil::add("ScriptStudioFixAll", args, LLSD(), [handle, id, pick](const LLSD& notification, const LLSD& response) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(id) : NONE;
        if (index != NONE && LLNotificationsUtil::getSelectedOption(notification, response) == 0)
        {
            studio->fixAll(*studio->mDocs[index], pick);
        }
    });
}

bool ALFloaterScriptStudio::fixAll(Doc& doc, const FixPick& pick)
{
    if (!doc.loaded || !doc.modifiable)
    {
        return false;
    }
    // Only the fixes made over the text as it stands (pickFixes). Made on
    // a save, into the source where it stands, whichever view is in front,
    // as the formatting and the trimming a save makes are; asked for,
    // with the source brought forward, to be seen.
    ALCodeEditor&                                    source = pick.forSave ? *doc.editor : sourceInFront(doc);
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
    setStatus(counted("FixesMade", static_cast<S32>(fixes.size())));
    scheduleAnalysis(doc, true);
    return true;
}

void ALFloaterScriptStudio::fixesOn(const Doc& doc, S32 line, std::vector<ALCodeEditor::Fix>& out) const
{
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

const ALFloaterScriptStudio::Doc::Shown* ALFloaterScriptStudio::shownOf(const LLSD& value) const
{
    const size_t index = indexOf(value["doc"].asString());
    return index == NONE ? nullptr : mDocs[index]->findShown(value["line"].asInteger(), value["column"].asInteger(), value["file"].asString(), value["message"].asString());
}

void ALFloaterScriptStudio::showEditorKeys()
{
    applyMenuKeys();
    refreshKeyTips();
}

void ALFloaterScriptStudio::applyMenuKeys()
{
    LLMenuBarGL* bar = menuBar();
    if (!bar)
    {
        return;
    }
    // The menus' own commands answer to the keys a person gave them, or
    // the standard's.
    for (const ALScriptKeymap::MenuCommand& command : ALScriptKeymap::menuCommands())
    {
        if (LLMenuItemGL* item = bar->findChild<LLMenuItemGL>(command.item, true))
        {
            const auto [key, mask] = ALScriptKeymap::menuKey(command.item);
            item->setShownAccelerator(key, mask);
        }
    }
    // A menu item that gives an editor's command shows, and answers to, the
    // keymap's first key for it, or none: the editor has a key before the
    // menus do, so a key the keymap took elsewhere is not the menu's.
    const ALKeymap keymap = ALScriptKeymap::current();
    const std::function<void(LLView*)> walk = [&](LLView* menu) {
        for (LLView* child : *menu->getChildList())
        {
            if (LLMenuItemBranchGL* branch = dynamic_cast<LLMenuItemBranchGL*>(child))
            {
                if (LLMenuGL* under = branch->getBranch())
                {
                    walk(under);
                }
                continue;
            }
            LLMenuItemGL* item = dynamic_cast<LLMenuItemGL*>(child);
            const std::optional<ALEditorCommand> command = item ? alEditorCommandFromName(item->getName()) : std::nullopt;
            if (!command)
            {
                continue;
            }
            KEY  key  = KEY_NONE;
            MASK mask = MASK_NONE;
            keymap.keysFor(*command, key, mask);
            item->setShownAccelerator(key, mask);
        }
    };
    walk(bar);
}

void ALFloaterScriptStudio::refreshKeyTips()
{
    LLMenuBarGL* bar = menuBar();
    if (!bar)
    {
        return;
    }
    // Each tip as the skin wrote it, its keys said as the menus have them
    // now; a command with none loses the brackets that would hold them.
    for (const auto& [control, items] : mKeyTips)
    {
        LLView*    view = findChild<LLView>(control);
        const auto was  = mKeyTipTexts.find(control);
        if (!view || was == mKeyTipTexts.end())
        {
            continue;
        }
        std::string tip = was->second;
        S32         n   = 0;
        for (const std::string& name : items)
        {
            ++n;
            const LLMenuItemGL* item  = bar->findChild<LLMenuItemGL>(name, true);
            const std::string   keys  = item ? item->getAcceleratorString() : std::string();
            const std::string   field = n == 1 ? std::string("[KEYS]") : "[KEYS" + std::to_string(n) + "]";
            if (keys.empty())
            {
                LLStringUtil::replaceString(tip, " (" + field + ")", std::string());
                LLStringUtil::replaceString(tip, "; " + field + " ", "; ");
            }
            LLStringUtil::replaceString(tip, field, keys);
        }
        view->setToolTip(tip);
    }
    // What the words past the breadcrumb do when pressed, with the keys
    // that do the same; asked on every move of the caret, so said here.
    for (auto [item_name, tip, keyless, out] : { std::make_tuple("go_to_line", "TrailerLineTip", "TrailerLineTipNoKeys", &mTrailerLineTip),
                                                std::make_tuple("problems", "TrailerProblemsTip", "TrailerProblemsTipNoKeys", &mTrailerProblemsTip),
                                                std::make_tuple("expanded", "TrailerSourceTip", "TrailerSourceTipNoKeys", &mTrailerSourceTip),
                                                std::make_tuple("expanded", "TrailerExpandedTip", "TrailerExpandedTipNoKeys", &mTrailerExpandedTip) })
    {
        const LLMenuItemGL*        item = bar->findChild<LLMenuItemGL>(item_name, true);
        const std::string          keys = item ? item->getAcceleratorString() : std::string();
        LLStringUtil::format_map_t args;
        args["[KEYS]"] = keys;
        *out           = getString(keys.empty() ? keyless : tip, args);
    }
    if (Doc* doc = active())
    {
        refreshTrailer(*doc);
    }
}

void ALFloaterScriptStudio::refreshUndoLabels()
{
    // Named for the step, where the step has a name the studio gave it --
    // and plain where a field with the keyboard has the step to take.
    Doc*                     doc   = active();
    const LLEditMenuHandler* field = focusedEditHandler();
    const bool               ours  = !field || (doc && (field == doc->editor || field == doc->expandedEditor));
    const std::string        undo  = doc && (ours || !field->canUndo()) ? doc->shownText()->undoJournal().undoLabel() : std::string();
    const std::string        redo  = doc && (ours || !field->canRedo()) ? doc->shownText()->undoJournal().redoLabel() : std::string();
    if (undo == mUndoSaid && redo == mRedoSaid)
    {
        return;
    }
    mUndoSaid       = undo;
    mRedoSaid       = redo;
    const auto what = [this](const std::string& label) {
        return label == "rename"     ? getString("UndoWhatRename")
               : label == "format"   ? getString("UndoWhatFormat")
               : label == "replace"  ? getString("UndoWhatReplace")
               : label == "fix"      ? getString("UndoWhatFix")
               : label == "refactor" ? getString("UndoWhatRefactor")
                                     : std::string();
    };
    sayUndoRedo(what(undo), what(redo));
}

void ALFloaterScriptStudio::problemChosen(const ALScriptProblemsPane::Place& place, bool to_editor)
{
    const size_t index = indexOf(place.doc);
    if (index == NONE)
    {
        return;
    }
    const S32  line       = place.line;
    const S32  column     = place.column;
    const bool has_column = place.hasColumn;
    if (!to_editor && deferOpen(mProblemsPane->list(), place.file))
    {
        return;
    }
    noteJump(!to_editor);
    ++mHoldPanes;
    if (!place.file.empty())
    {
        // In an include: opened in a tab of its own where it is a script
        // or a notecard in the world, or a file on disk.
        openIncludeAt(place.file, place.fileName, line, has_column ? column : -1, 0);
    }
    else
    {
        if (index != mActive)
        {
            activate(index);
        }
        // What the problem is about, selected, with what it says in the
        // card the mouse would bring up there.
        Doc&            doc   = *mDocs[index];
        const ALTextPos begin = doc.editor->document().clamp(ALTextPos(line, has_column ? column : 0));
        const S32       end_line = place.endLine;
        ALTextPos       end      = end_line >= 0 ? doc.editor->document().clamp(ALTextPos(end_line, place.endColumn)) : begin;
        if (end < begin)
        {
            end = begin;
        }
        sourceInFront(doc).goTo(ALTextRange(begin, end));
        showProblemCard(doc, begin);
    }
    --mHoldPanes;
    revealed(mProblemsPane->list(), to_editor);
}

bool ALFloaterScriptStudio::deferOpen(ALPaneList* list, const std::string& path)
{
    // Once the list has stayed on it -- the arrows walking past each row
    // they go by -- it is opened, as a preview, which the next one opened
    // so takes the place of.
    if (mSettled || path.empty())
    {
        return false;
    }
    ALScriptRef ref;
    std::string file;
    const bool  open = ALScriptPreprocessor::refOf(path, ref) ? indexOf(ref) != NONE
                       : ALScriptPreprocessor::fileOf(path, file) ? indexOf("disk:" + file) != NONE
                                                                   : true;
    if (open)
    {
        return false;
    }
    constexpr F64     SETTLE = 0.35;
    LLScrollListItem* item   = list->getFirstSelected();
    mSettleList              = list;
    mSettleValue             = item ? item->getValue() : LLSD();
    mSettleDue               = LLTimer::getTotalSeconds() + SETTLE;
    return true;
}

void ALFloaterScriptStudio::pumpSettle()
{
    if (!mSettleList || LLTimer::getTotalSeconds() < mSettleDue)
    {
        return;
    }
    ALPaneList* list = mSettleList;
    mSettleList      = nullptr;
    // Still there, and still being walked: gone from, or left for
    // something else, it is not opened.
    LLScrollListItem* item = list->getFirstSelected();
    if (!item || !list->hasFocus() || !llsd_equals(item->getValue(), mSettleValue))
    {
        return;
    }
    mSettled = true;
    ++mOpenPreview;
    if (list == mProblemsPane->list())
    {
        mProblemsPane->choose(false);
    }
    else if (list == mReferences)
    {
        onReferenceChosen(false);
    }
    else if (list == mSearchPane->list())
    {
        mSearchPane->choose(false);
    }
    else if (list == mWeightsParts)
    {
        onWeightChosen(false);
    }
    --mOpenPreview;
    mSettled = false;
}

void ALFloaterScriptStudio::closePreview()
{
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        Doc& doc = *mDocs[i];
        if (!doc.preview)
        {
            continue;
        }
        // One a pane is listing -- its problems, the places a name was
        // looked up from it -- is being worked from, and stays.
        if (doc.editor->isDirty() || doc.save.sending() || doc.id == mProblemsPane->listedId() || doc.id == mFound.from)
        {
            holdPreview(doc);
            return;
        }
        // Nothing held there: the next look takes its place.
        letGoOf(i);
        return;
    }
}

void ALFloaterScriptStudio::holdPreview(Doc& doc)
{
    if (doc.preview)
    {
        doc.preview = false;
        fillTabs();
    }
}

void ALFloaterScriptStudio::revealed(LLUICtrl* list, bool to_editor)
{
    // To the script to type there, held where it was a preview, or back
    // to the list to walk on.
    if (to_editor)
    {
        mSettleList = nullptr;
        if (Doc* doc = active())
        {
            holdPreview(*doc);
            focusShown(*doc);
        }
        return;
    }
    list->setFocus(true);
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
    // A line of a text, as it is, for a row of a pane.
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
        size_t end = text.find('\n', begin);
        if (end != std::string::npos && end > begin && text[end - 1] == '\r')
        {
            --end;
        }
        return text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    }

    std::string lineOf(const ALTextDocument& text, S32 line)
    {
        if (line < 0 || line >= text.lineCount())
        {
            return std::string();
        }
        return text.line(line);
    }
}

// static
void ALFloaterScriptStudio::placeText(Doc::Place& place, const std::string& line)
{
    // Trimmed for the row, and the name's place moved with the trimming.
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos)
    {
        place.text.clear();
        place.at = -1;
        return;
    }
    const size_t last = line.find_last_not_of(" \t\r");
    place.text        = line.substr(first, last - first + 1);
    const S32 at      = place.span.column - static_cast<S32>(first);
    place.at          = at >= 0 && at < static_cast<S32>(place.text.size()) ? at : -1;
}

const char* ALFloaterScriptStudio::includeImage(const std::string& path, bool lua) const
{
    // What the include is, where the item can be found: a notecard, or a
    // script in the language asking; a file on disk.
    ALScriptRef ref;
    if (!ALScriptPreprocessor::refOf(path, ref))
    {
        return "Studio_File";
    }
    LLViewerObject*        object = ref.inInventory() ? nullptr : gObjectList.findObject(ref.object);
    const LLInventoryItem* item   = ref.inInventory() ? gInventory.getItem(ref.item) : object ? object->getInventoryItem(ref.item) : nullptr;
    if (item && item->getType() == LLAssetType::AT_NOTECARD)
    {
        return "Inv_Notecard";
    }
    return lua ? "Inv_Script_Luau" : "Inv_Script";
}

LLSD ALFloaterScriptStudio::Declared::value() const
{
    LLSD out;
    out["line"]   = line;
    out["column"] = column;
    out["path"]   = path;
    out["name"]   = name;
    return out;
}

ALFloaterScriptStudio::Declared ALFloaterScriptStudio::declaredOf(const Doc& doc, const ALScriptAnalysis::Result& result) const
{
    // Where the analyzer read it declared: in the expansion, where the
    // preprocessor ran, and so back to the source -- this script's, or an
    // include's.
    Declared declared;
    if (!result.hover.found || !result.hover.hasDefinition)
    {
        return declared;
    }
    declared.line   = result.hover.definitionLine;
    declared.column = result.hover.definitionColumn;
    if (preprocessed(doc))
    {
        const ALSourceMap::Loc loc = doc.expanded.map.toSource(declared.line, declared.column);
        declared.line              = loc.found() ? loc.line : -1;
        declared.column            = loc.found() ? loc.column : -1;
        if (loc.found() && loc.file > 0)
        {
            declared.path = doc.expanded.map.files()[loc.file].path;
            declared.name = doc.expanded.map.files()[loc.file].name;
        }
    }
    return declared;
}

void ALFloaterScriptStudio::goToDeclared(const LLSD& value)
{
    Doc* doc = active();
    if (!doc || !value.has("line"))
    {
        return;
    }
    const S32 line   = value["line"].asInteger();
    const S32 column = value["column"].asInteger();
    noteJump();
    if (!value["path"].asString().empty())
    {
        openIncludeAt(value["path"].asString(), value["name"].asString(), line, column, 0);
        return;
    }
    ALCodeEditor& source = sourceInFront(*doc);
    source.goTo(ALTextPos(line, llmax(0, column)));
    source.setFocus(true);
}

bool ALFloaterScriptStudio::sourceLine(const std::string& path, S32 line, std::string& out) const
{
    // In its tab, as it stands there, where it is open.
    for (const std::unique_ptr<Doc>& doc : mDocs)
    {
        if (doc->loaded && (doc->file.empty() ? ALScriptPreprocessor::pathOf(doc->ref) : doc->id) == path)
        {
            if (line < 0 || line >= doc->editor->document().lineCount())
            {
                return false;
            }
            out = doc->editor->document().line(line);
            return true;
        }
    }
    std::string text;
    if (!ALScriptPreprocessor::instance().heldText(path, text))
    {
        return false;
    }
    out = lineOf(text, line);
    return true;
}

void ALFloaterScriptStudio::symbolAnswered(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at)
{
    // Of another question, or of a text that has moved on. Where it was
    // asked is the source's place, which the result's own is not where
    // the preprocessor ran and an include moved the lines.
    if (result.version != doc.symbolVersion || at != doc.symbolAt || doc.symbolCommand == ALEditorCommand::None)
    {
        return;
    }
    const ALEditorCommand     command = doc.symbolCommand;
    const ALScriptReferences& refs    = result.references;
    doc.symbolCommand                 = ALEditorCommand::None;
    LLStringUtil::format_map_t args;
    const std::string          name = refs.found ? refs.name : doc.editor->document().text(doc.editor->identifierAt(doc.symbolAt));
    args["[NAME]"]                  = name;
    // A word of the language has no definition in the script to go to --
    // where the script has not made one of its own: its reference is where
    // it is defined.
    const Vocab* known = command == ALEditorCommand::GoToDefinition ? vocabWord(doc.language.lua, name) : nullptr;
    if (known && (!refs.found || !refs.hasDefinition))
    {
        mFolds.setCollapsed("inspector", false);
        showReference(*known, doc.language.lua);
        return;
    }
    if (!refs.found)
    {
        // Nothing known because nothing could be read, which the syntax
        // errors in the problems explain, or nothing known of this name.
        setStatus(getString(result.understood ? "NothingKnown" : "NothingKnownBroken", args), !result.understood);
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
            }
        }
        place.span = span;
        // The line as it was written: this script's, or the include's
        // where it is in hand; the expansion's, with its macros put in
        // place, only where it is not.
        std::string line;
        if (place.file.empty())
        {
            placeText(place, lineOf(doc.editor->document(), span.line));
        }
        else if (sourceLine(place.file, span.line, line))
        {
            placeText(place, line);
        }
        else
        {
            placeText(place, lineOf(doc.expanded.text, raw.line));
            place.at = -1;
        }
        places.push_back(std::move(place));
    }
    switch (command)
    {
        case ALEditorCommand::GoToDefinition:
            if (hasDefinition)
            {
                noteJump();
            }
            if (!hasDefinition)
            {
                setStatus(getString("NoDefinition", args));
            }
            else if (homePath.empty())
            {
                ALCodeEditor& source = sourceInFront(doc);
                source.goTo(rangeOf(definition));
                source.setFocus(true);
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

namespace
{
    // What a loaded script's author wrote: the source out of the
    // envelope where one wrapped it, the text as it came otherwise, and
    // nothing where it could not be read.
    std::string sourceOf(const ALScriptWorkspace::Loaded& loaded)
    {
        if (!loaded.error.empty() || loaded.notecard)
        {
            return std::string();
        }
        std::string text = loaded.text;
        if (std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(text))
        {
            text = envelope->source;
        }
        return text;
    }
}

void ALFloaterScriptStudio::startLookup(Doc& doc, ALEditorCommand command, const ALScriptReferences& refs, bool has_definition, const std::string& home_path,
                                        const ALScriptSpan& definition, std::vector<Doc::Place> places, U32 version)
{
    // A script names are looked up from is being worked in: a preview of
    // it is held, so that following what it finds does not close it.
    holdPreview(doc);
    Doc::Lookup& lookup   = doc.lookup;
    lookup                = Doc::Lookup();
    lookup.generation     = ++mLookupGeneration;
    const std::string id_of_lookup   = doc.id;
    const U32         lookup_generation = lookup.generation;
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
    // Each by the path the preprocessor's map calls it by: an item's, or
    // a file's, which is its tab's id.
    for (const std::unique_ptr<Doc>& each : mDocs)
    {
        if (each->loaded && each.get() != &doc)
        {
            lookup.versions[each->file.empty() ? ALScriptPreprocessor::pathOf(each->ref) : each->id] = each->editor->document().version();
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
    // One held while the fan-out is built, so that a candidate answered
    // on the spot -- a script already open -- cannot bring the count to
    // nothing and finish the lookup with the others still to be asked.
    ++lookup.pending;
    if (has_definition && !doc.ref.inInventory())
    {
        const LLHandle<LLFloater> handle     = getHandle();
        const std::string         id         = doc.id;
        const U32                 generation = lookup.generation;
        for (const ALScriptExplorerModel::Object& object : mExplorerPane->model().objects())
        {
            bool ours = false;
            for (const ALScriptExplorerModel::Prim& prim : object.prims)
            {
                ours = ours || prim.id == doc.ref.object;
            }
            if (!ours || !object.present)
            {
                continue;
            }
            for (const ALScriptExplorerModel::Prim& prim : object.prims)
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
                        const Doc&        other = *mDocs[index];
                        const std::string name  = other.name;
                        const LLUUID      asset = other.assetId;
                        const std::string text  = other.editor->text();
                        lookupCandidate(id, generation, ref, name, asset, text);
                        continue;
                    }
                    const std::string name = item.name;
                    ALScriptWorkspace::instance().load(ref, [handle, id, generation, ref, name](const ALScriptWorkspace::Loaded& loaded) {
                        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
                        if (!studio)
                        {
                            return;
                        }
                        studio->lookupCandidate(id, generation, ref, name, loaded.assetId, sourceOf(loaded));
                    });
                }
            }
        }
    }
    // What the held one stood for: the doc is found again, since a
    // candidate answered on the spot may have opened a tab.
    const size_t still = indexOf(id_of_lookup);
    if (still == NONE || mDocs[still]->lookup.generation != lookup_generation)
    {
        return;
    }
    Doc&         now    = *mDocs[still];
    Doc::Lookup& theirs = now.lookup;
    --theirs.pending;
    if (theirs.pending > 0)
    {
        setStatus(counted("LookingAcross", theirs.pending, { { "[NAME]", theirs.name } }));
    }
    lookupSettled(now);
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
    // Expanded as the compiler would see it, not optimized: a name is
    // being looked for, and the optimizer may rename it or take it away
    // -- with shrinknames on it renames every one.
    request.optimize      = false;
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptPreprocessor::instance().run(request, [handle, id, generation, ref, name, text](const ALPreprocessor::Result& result) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->lookupExpanded(id, generation, ref, name, text, result);
        }
    });
}

void ALFloaterScriptStudio::lookupExpanded(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name,
                                           const std::string& source, const ALPreprocessor::Result& result)
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
    ALScriptAnalysis::instance().ask(std::move(request), [handle, id, generation, ref, name, source, map = result.map,
                                                          expanded = result.text](const ALScriptAnalysis::Result& answer) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->lookupAnswered(id, generation, ref, name, map, source, expanded, answer);
        }
    });
}

void ALFloaterScriptStudio::lookupAnswered(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const ALSourceMap& map,
                                           const std::string& source, const std::string& expanded, const ALScriptAnalysis::Result& result)
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
            // This script's own, read through the other's expansion of it
            // -- the region's copy, which is not what is typed here: its
            // own answer has them, in the text as it stands.
            if (file > 0 && map.files()[file].path == own)
            {
                continue;
            }
            Doc::Place place;
            place.span     = span;
            place.file     = file == 0 ? self : map.files()[file].path;
            place.fileName = file == 0 ? name : map.files()[file].name;
            // As the other script, or its include, was written.
            std::string line;
            if (file == 0)
            {
                placeText(place, lineOf(source, span.line));
            }
            else if (sourceLine(place.file, span.line, line))
            {
                placeText(place, line);
            }
            else
            {
                placeText(place, lineOf(expanded, raw.line));
                place.at = -1;
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
    args["[FILES]"] = std::to_string(files.size());
    if (command == ALEditorCommand::FindReferences)
    {
        mFound               = References();
        mFound.from          = doc.id;
        mFound.fromName      = doc.name;
        mFound.name          = lookup.name;
        mFound.places        = lookup.places;
        mFound.hasDefinition = lookup.hasDefinition;
        mFound.home          = lookup.homePath;
        mFound.definition    = lookup.definition;
        std::vector<ALTextRange> lit;
        for (const Doc::Place& place : mFound.places)
        {
            if (place.file.empty())
            {
                lit.push_back(rangeOf(place.span));
            }
        }
        doc.editor->setHighlights(std::move(lit));
        fillReferences();
        showBottom("references_tab");
        setStatus(counted(files.size() > 1 ? "ReferencesFoundAcross" : "ReferencesFound", static_cast<S32>(lookup.places.size()), args));
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
    quick->onQueryChanged([handle, quick, count, scripts, old_name, id](const std::string& typed) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(id) : NONE;
        if (index == NONE)
        {
            return;
        }
        const Doc& doc = *studio->mDocs[index];
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
        else if (studio->reservedName(doc, name))
        {
            args["[NAME]"] = name;
            quick->setHint(studio->getString("RenameReserved", args));
        }
        else if (name == old_name)
        {
            quick->setHint(studio->getString("RenameSame", args));
        }
        else if (std::any_of(doc.outline.begin(), doc.outline.end(), [&name](const ALScriptOutlineEntry& entry) { return entry.name == name; }))
        {
            // Allowed, since a name in another scope may be meant; said.
            quick->setHint(studio->getString("RenameClash", args));
        }
        else
        {
            quick->setHint(scripts > 1 ? studio->getString("RenameToAcross", args) : studio->counted("RenameTo", count, args));
        }
    });
    quick->setQuery(old_name);
    quick->takeFocus();
}

bool ALFloaterScriptStudio::reservedName(const Doc& doc, const std::string& name) const
{
    // The language's own words, which a name of the script's cannot be:
    // its keywords and types, the preprocessor's words while their
    // transforms are on, and every function, event and constant the
    // definitions give.
    static const std::set<std::string> LSL{ "default", "state", "event", "jump", "return", "if", "else", "for", "do", "while", "print",
                                           "integer", "float", "string", "key", "vector", "rotation", "quaternion", "list" };
    static const std::set<std::string> LUAU{ "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in",
                                            "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while", "continue" };
    if (doc.language.lua ? LUAU.count(name) > 0 : LSL.count(name) > 0)
    {
        return true;
    }
    if (!doc.language.lua)
    {
        if ((name == "switch" || name == "case") && gSavedSettings.getBOOL("ALScriptPreprocSwitch"))
        {
            return true;
        }
        if ((name == "break" || name == "continue" || name == "inline") && gSavedSettings.getBOOL("ALScriptPreprocExtensions"))
        {
            return true;
        }
    }
    return vocabWord(doc.language.lua, name) != nullptr;
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
    if (reservedName(doc, name))
    {
        setStatus(getString("RenameReserved", args), true);
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
    // Another script or file open here, unchanged since it was read: each
    // place where the old name still stands, since an open include may
    // not read as the expansion had it. False where it has moved on, or
    // may not be changed.
    const auto rename_open = [&](Doc& other, const std::string& file, const std::vector<const Doc::Place*>& places) {
        const auto version = lookup.versions.find(file);
        if (version == lookup.versions.end() || version->second != other.editor->document().version() || !other.modifiable)
        {
            return false;
        }
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
        const S32 count = static_cast<S32>(edits.size());
        if (count > 0 && other.editor->replaceAll(std::move(edits)))
        {
            other.editor->undoJournal().label("rename");
            renamed += count;
            ++scripts;
        }
        return true;
    };
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
        std::string path;
        if (!ALScriptPreprocessor::refOf(file, ref))
        {
            // A file on disk -- one the preprocessor read from a folder
            // it may read -- changed in its tab, as any script is, and
            // written only when that tab is saved; opened where it is not
            // open, with the change waiting unsaved, as a script is.
            if (!ALScriptPreprocessor::fileOf(file, path))
            {
                ++stale;
                continue;
            }
            size_t at = indexOf(file);
            if (at != NONE)
            {
                stale += rename_open(*mDocs[at], file, places) ? 0 : 1;
                continue;
            }
            openFile(path, doc.language.lua);
            at = indexOf(file);
            if (at == NONE)
            {
                // Gone, or opened in another window.
                ++stale;
                continue;
            }
            // Read as it is on disk now, which the expansion may not have
            // been: each place where the old name still stands.
            Doc& other = *mDocs[at];
            for (const Doc::Place* place : places)
            {
                other.pendingEdits.push_back(Doc::PendingEdit{ place->span, old_name, name });
            }
            // Counted as a script not open is, each place asked for; the
            // ones that no longer stand are said by the edits' own report.
            renamed += static_cast<S32>(places.size());
            applyPendingEdits(other);
            ++scripts;
            ++opened;
            continue;
        }
        const size_t other_index = indexOf(ref);
        if (other_index != NONE && mDocs[other_index]->loaded)
        {
            stale += rename_open(*mDocs[other_index], file, places) ? 0 : 1;
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
    // What was done, a clause for each thing there is to say, each count in
    // its own form.
    args["[PLACES]"]  = counted("Places", renamed);
    args["[SCRIPTS]"] = counted("Scripts", scripts);
    std::string said  = getString(scripts > 1 || opened > 0 ? "RenamedIn" : "RenamedHere", args);
    if (opened > 0)
    {
        args["[SCRIPTS]"] = counted("Scripts", opened);
        said += "; " + getString("RenamedOpened", args);
    }
    if (stale > 0)
    {
        args["[SCRIPTS]"] = counted("Scripts", stale);
        said += "; " + getString("RenamedLeft", args);
    }
    report(said + ".", stale > 0);
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
    const bool replace = !edits.empty() && edits.front().replace;
    if (!changes.empty() && !doc.modifiable)
    {
        // Nothing may be changed here: every place was missed.
        missed += static_cast<S32>(changes.size());
        changes.clear();
    }
    if (!changes.empty())
    {
        doc.editor->setReadOnly(false);
        if (doc.editor->replaceAll(std::move(changes)))
        {
            doc.editor->undoJournal().label(replace ? "replace" : "rename");
        }
    }
    if (missed > 0)
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        report(counted(replace ? "ReplaceMissed" : "RenameMissed", missed, args), true, &doc);
    }
}

void ALFloaterScriptStudio::save(Doc& doc)
{
    mSaving.save(doc);
}

std::string ALFloaterScriptStudio::bridgeId(const Doc& doc) const
{
    return LLScriptEditorWSServer::buildScriptSubscriptionId(doc.ref.object, doc.ref.item);
}

bool ALFloaterScriptStudio::subscribe(Doc& doc)
{
    LLScriptEditorWSServer::ptr_t server = LLScriptEditorWSServer::isEnabled() ? LLScriptEditorWSServer::ensureServerRunning() : nullptr;
    return server && server->subscribeScriptEditor(doc.ref.object, doc.ref.item, doc.name, getHandle(), bridgeId(doc), doc.language.lua);
}

void ALFloaterScriptStudio::unsubscribe(const Doc& doc)
{
    if (LLScriptEditorWSServer::ptr_t server = LLScriptEditorWSServer::getServer())
    {
        const std::string script_id = bridgeId(doc);
        server->sendUnsubscribeScriptEditor(script_id);
        server->unsubscribeEditor(script_id);
    }
}

void ALFloaterScriptStudio::startEditor(Doc& doc, const std::string& filename, bool on_disk)
{
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (!on_disk && LLScriptEditorWSServer::isTightIntegration())
    {
        // VS Code itself, which subscribes over the bridge the copy was
        // told to.
        if (!LLScriptEditorWSServer::isEnabled() || !LLScriptEditorWSServer::getServer())
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
        report(getString("ExternalOpenedVSCode", args), false, &doc);
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
    report(getString("ExternalOpened", args), false, &doc);
}

void ALFloaterScriptStudio::watchFile(Doc& doc)
{
    if (doc.file.empty() || doc.external.watch)
    {
        return;
    }
    // The file watched for changes made outside, whoever makes them: an
    // editor the studio started, or anything else.
    const LLHandle<LLFloater> handle = getHandle();
    const std::string         id     = doc.id;
    doc.external.watch               = std::make_unique<StudioLiveFile>(
        doc.file,
        [handle, id](const std::string& file) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChangedOutside(id, file);
            }
        },
        false);
}

void ALFloaterScriptStudio::fileChangedOutside(const std::string& id, const std::string& file)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    // Gone, or not to be read: nothing to take. Deleted, the tab keeps
    // what it holds and the check on what holds each tab says the file
    // is gone; mid-save by something that writes it in two steps, it is
    // heard again once it is back.
    std::string text;
    if (!readWholeFile(file, text))
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (doc.editor->isDirty())
    {
        // What is typed here is not thrown away for it: the author is told,
        // and asked which to keep -- once, however often it changes while
        // the question is up.
        report(getString("FileChangedOutside", args), true, &doc);
        if (doc.askingReload)
        {
            return;
        }
        doc.askingReload = true;
        LLSD question;
        question["NAME"]                 = doc.name;
        const LLHandle<LLFloater> handle = getHandle();
        LLNotificationsUtil::add("ScriptStudioFileChanged", question, LLSD(), [handle, id](const LLSD& notification, const LLSD& response) {
            ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
            const size_t           index  = studio ? studio->indexOf(id) : NONE;
            if (index == NONE)
            {
                return;
            }
            Doc& asked         = *studio->mDocs[index];
            asked.askingReload = false;
            if (LLNotificationsUtil::getSelectedOption(notification, response) == 0)
            {
                // What is on disk, as one step to undo, and clean.
                studio->revert(asked);
            }
        });
        return;
    }
    if (text == doc.editor->text())
    {
        return;
    }
    // Taken as one step to undo, and clean, since it is what the file is.
    doc.carriedText = text;
    takeCarriedText(doc);
    fileSettled(doc);
    report(getString("FileReloaded", args), false, &doc);
}

void ALFloaterScriptStudio::saveFile(Doc& doc)
{
    doc.save.done();
    LLStringUtil::format_map_t args;
    args["[PATH]"] = doc.file;
    if (!ALFileWrite::whole(doc.file, doc.editor->text()))
    {
        report(getString("SaveToFileFailed", args), true, &doc);
        mSaving.stopped(doc);
        return;
    }
    if (doc.external.watch)
    {
        // The watcher on the file: this write is not an outside change.
        doc.external.watch->seen();
    }
    report(getString("SavedToFile", args), false, &doc);
    fileSettled(doc);
    // The scripter's snippets, offered as saved from here on; the vimrc
    // read again at once.
    for (bool lua : { false, true })
    {
        if (doc.file == ALScriptSnippets::path(lua))
        {
            ALScriptSnippets::forget(lua);
        }
    }
    if (doc.file == ALScriptStudioVimrc::filePath())
    {
        ALScriptStudioVimrc::instance().check(true);
    }
}

void ALFloaterScriptStudio::fileSettled(Doc& doc)
{
    doc.editor->resetDirty();
    keepSavedWeights(doc);
    mRecovery.keep(doc);
    // The scripts that include it see the file as it is now: those whose
    // last expansion read it, and those that one may have been wanted by
    // -- an expansion with a problem, an include not found say -- not
    // every script open, each expanded again for a file it never reads.
    const std::string saved = ALScriptModules::identity("disk:" + doc.file);
    const auto        reads = [&saved](const Doc& each) {
        if (!each.expanded.valid || !each.expanded.problems.empty())
        {
            return true;
        }
        const std::vector<ALSourceMap::File>& files = each.expanded.map.files();
        return std::any_of(files.begin(), files.end(), [&saved](const ALSourceMap::File& file) { return ALScriptModules::identity(file.path) == saved; });
    };
    for (std::unique_ptr<Doc>& each : mDocs)
    {
        if (each.get() != &doc && each->file.empty() && preprocessed(*each) && reads(*each))
        {
            each->expanded.valid = false;
            mSaving.preprocess(*each);
            scheduleAnalysis(*each);
        }
    }
    fillTabs();
    refreshToolbar();
    if (doc.save.closeAfter())
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

void ALFloaterScriptStudio::fillReferences()
{
    // The row chosen and the scroll kept through a refill, which an edit
    // moving the places asks for.
    mPlacesStale        = false;
    const S32 scrolled  = mReferences->getScrollPos();
    const S32 chosen    = mReferences->getFirstSelected() ? mReferences->getFirstSelected()->getValue().asInteger() : -1;
    mReferences->deleteAllItems();
    refreshBottomTabs();
    if (mFound.places.empty())
    {
        // A list with nothing in it and nothing to say is a pane that
        // looks broken; this one is empty until it is asked a question,
        // so it says which question.
        mReferencesHead->setText(getString("NoReferences"));
        return;
    }
    // What was looked up, and where it was found, over the places: the
    // status line says it once, and the next thing said takes it away.
    std::set<std::string> files;
    for (const Doc::Place& place : mFound.places)
    {
        files.insert(place.file);
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"]  = mFound.name;
    args["[FILES]"] = std::to_string(files.size());
    mReferencesHead->setText(counted(files.size() > 1 ? "ReferencesFoundAcross" : "ReferencesFound", static_cast<S32>(mFound.places.size()), args));

    LLStringUtil::format_map_t named;
    named["[NAME]"] = mFound.name;
    const std::string declared_tip = getString("ReferenceDeclarationTip", named);
    for (size_t i = 0; i < mFound.places.size(); ++i)
    {
        const Doc::Place& place = mFound.places[i];
        // The declaration, marked: in the script it was looked up from
        // where no include declares it, else in the include that does.
        const std::string& in_file     = place.file;
        const bool         declaration = mFound.hasDefinition && place.span.line == mFound.definition.line &&
                                 place.span.column == mFound.definition.column && in_file == mFound.home;
        LLSD row;
        row["value"]                = static_cast<S32>(i);
        row["columns"][0]["column"] = "where";
        row["columns"][0]["value"]  = place.file.empty() ? mFound.fromName : place.fileName;
        row["columns"][1]["column"] = "line";
        row["columns"][1]["value"]  = llformat("%d:%d", place.span.line + 1, place.span.column + 1);
        row["columns"][2]["column"] = "role";
        row["columns"][2]["value"]  = declaration ? getString("ReferenceDeclaration") : std::string();
        row["columns"][3]["column"] = "text";
        row["columns"][3]["value"]  = place.text;
        if (declaration)
        {
            for (S32 c = 0; c < 4; ++c)
            {
                row["columns"][c]["font"]["style"] = "BOLD";
                row["columns"][c]["tool_tip"]      = declared_tip;
            }
        }
        LLScrollListItem* item = mReferences->addElement(row);
        // The name, lit where it stands in the line.
        if (item && place.at >= 0)
        {
            if (LLScrollListCell* text = item->getColumn(3))
            {
                text->highlightText(place.at, static_cast<S32>(mFound.name.size()));
            }
        }
    }
    if (chosen >= 0)
    {
        mReferences->selectByValue(LLSD(llmin(chosen, static_cast<S32>(mFound.places.size()) - 1)));
    }
    mReferences->setScrollPos(scrolled);
}

void ALFloaterScriptStudio::slidePlaces(Doc& doc, const ALTextDocument::Edit& edit)
{
    if (mFound.places.empty())
    {
        return;
    }
    // This script's places: its own, where it was looked up from, or the
    // ones in it as an include or another of the object's scripts.
    const std::string path    = doc.file.empty() ? ALScriptPreprocessor::pathOf(doc.ref) : doc.id;
    const size_t      before  = mFound.places.size();
    const S32         first   = edit.range.normalised().begin.line;
    const S32         last    = edit.rangeAfter().normalised().end.line;
    bool              changed = false;
    const auto        mine    = [&](const std::string& file) { return file.empty() ? doc.id == mFound.from : file == path; };
    const auto        slide   = [&](ALScriptSpan& span) {
        ALTextRange       range(ALTextPos(span.line, span.column), ALTextPos(span.endLine, span.endColumn));
        const ALTextRange was = range;
        if (!edit.slide(range))
        {
            return false;
        }
        if (range != was)
        {
            span.line      = range.begin.line;
            span.column    = range.begin.column;
            span.endLine   = range.end.line;
            span.endColumn = range.end.column;
            changed        = true;
        }
        return true;
    };
    mFound.places.erase(std::remove_if(mFound.places.begin(), mFound.places.end(),
                                       [&](Doc::Place& place) {
                                           if (!mine(place.file))
                                           {
                                               return false;
                                           }
                                           // The name itself edited: no longer a place it stands.
                                           if (!slide(place.span))
                                           {
                                               return true;
                                           }
                                           // Its line's words, where the edit was on it.
                                           if (place.span.line >= first && place.span.line <= last)
                                           {
                                               placeText(place, doc.editor->document().line(place.span.line));
                                               changed = true;
                                           }
                                           return false;
                                       }),
                        mFound.places.end());
    if (mFound.hasDefinition && mine(mFound.home))
    {
        mFound.hasDefinition = slide(mFound.definition);
    }
    // Refilled once this frame is drawn, however many edits it has.
    mPlacesStale = mPlacesStale || changed || mFound.places.size() != before;
}

void ALFloaterScriptStudio::onReferenceChosen(bool to_editor)
{
    LLScrollListItem* item = mReferences->getFirstSelected();
    if (!item)
    {
        return;
    }
    const size_t index = static_cast<size_t>(item->getValue().asInteger());
    if (index >= mFound.places.size())
    {
        return;
    }
    const Doc::Place place = mFound.places[index];
    if (!to_editor && deferOpen(mReferences, place.file))
    {
        return;
    }
    noteJump(!to_editor);
    ++mHoldPanes;
    if (place.file.empty())
    {
        // The script it was looked up from, whichever tab is in front.
        const size_t from = indexOf(mFound.from);
        if (from != NONE)
        {
            if (from != mActive)
            {
                activate(from);
            }
            sourceInFront(*mDocs[from]).goTo(rangeOf(place.span));
        }
    }
    else
    {
        openIncludeAt(place.file, place.fileName, place.span.line, place.span.column, place.span.endColumn - place.span.column);
    }
    --mHoldPanes;
    revealed(mReferences, to_editor);
}

bool ALFloaterScriptStudio::weightsShown() const
{
    const LLPanel* current = mBottomTabs ? mBottomTabs->getCurrentPanel() : nullptr;
    return mWeightsPane && !mFolds.collapsed("bottom") && current && current->getName() == "weights_tab" && getVisible() && !isMinimized();
}

void ALFloaterScriptStudio::refreshWeights()
{
    Doc* doc = active();
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc ? doc->name : std::string();
    if (!doc)
    {
        mWeightsPane->showNothing(getString("WeightsNoScript"));
        return;
    }
    if (doc->loaded && !weightTarget(*doc))
    {
        mWeightsPane->showNothing(getString("WeightsNoTarget", args));
        return;
    }
    if (!doc->loaded || doc->weights.empty())
    {
        mWeightsPane->showNothing(getString("WeightsNotYet", args));
        return;
    }
    ALScriptWeightsPane::Shown shown;
    shown.id      = doc->id;
    shown.name    = doc->name;
    shown.weights = doc->weights;
    shown.saved   = doc->weightsSaved;
    // Weighed as the check has the text, which is before the optimizer
    // where one runs; what a save sends beside it, where it has been
    // weighed of the text as it stands.
    shown.beforeOptimizer = preprocessed(*doc) && !doc->language.lua && gSavedSettings.getBOOL("ALScriptPreprocOptimizer");
    if (doc->weight && doc->weightSent && doc->weightVersion == doc->weightsVersion)
    {
        shown.sent = doc->weight->total;
    }
    for (const ALScriptWeight& weight : doc->weights)
    {
        for (const ALScriptWeight::Part& part : weight.parts)
        {
            if (!part.file.empty() && !shown.fileNames.contains(part.file))
            {
                shown.fileNames[part.file] = includeName(*doc, part.file);
            }
        }
    }
    mWeightsPane->show(std::move(shown));
}

void ALFloaterScriptStudio::onWeightChosen(bool to_editor)
{
    const std::optional<ALScriptWeightsPane::Place> place = mWeightsPane->chosenPlace();
    if (!place)
    {
        return;
    }
    if (!to_editor && deferOpen(mWeightsParts, place->file))
    {
        return;
    }
    noteJump(!to_editor);
    ++mHoldPanes;
    if (place->file.empty())
    {
        // The script the tab is about, whichever is in front by now.
        const size_t index = indexOf(mWeightsPane->shownId());
        if (index != NONE)
        {
            if (index != mActive)
            {
                activate(index);
            }
            sourceInFront(*mDocs[index]).goTo(ALTextPos(place->line, place->column));
        }
    }
    else
    {
        openIncludeAt(place->file, place->fileName, place->line, place->column, 0);
    }
    --mHoldPanes;
    revealed(mWeightsParts, to_editor);
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


bool ALFloaterScriptStudio::openIncluded(Doc& doc, const std::string& name, std::optional<bool> require)
{
    // As the last run of the preprocessor over the script found it.
    if (const std::string path = doc.foundAs(name, require); !path.empty())
    {
        noteJump();
        openIncludeAt(path, name, 0, 0, 0);
        return true;
    }
    // Else where the preprocessor would look on disk -- the tab's own
    // file's folder, then the include folders -- for an include, or a
    // module with its extensions and a folder's init.
    const std::vector<bool> kinds = require ? std::vector<bool>{ *require } : std::vector<bool>{ false, true };
    for (const std::string& folder : fileFolders(doc))
    {
        for (const bool as_require : kinds)
        {
            for (const std::string& candidate : ALDiskIncludes::namesFor(name, doc.language.lua, as_require))
            {
                const fsyspath  path((fsyspath(folder) / fsyspath(candidate)).lexically_normal());
                std::error_code ec;
                if (std::filesystem::is_regular_file(path, ec))
                {
                    noteJump();
                    openFile(path.string(), doc.language.lua);
                    return true;
                }
            }
        }
    }
    return false;
}

void ALFloaterScriptStudio::noteJump(bool walking)
{
    // A walk down a pane's list is one jump, from where the caret was
    // before it began; it ends when the editor has the keyboard again.
    if (walking && mWalking)
    {
        return;
    }
    mWalking = walking;
    Doc* doc = active();
    if (!doc || !doc->loaded)
    {
        return;
    }
    rememberPlace(NavPlace{ doc->id, doc->shownText()->caret(), doc->shownView() });
}

void ALFloaterScriptStudio::rememberPlace(const NavPlace& place)
{
    mForward.clear();
    // Another jump from the same line is not another place to go back to.
    if (!mBack.empty() && mBack.back().doc == place.doc && mBack.back().view == place.view && mBack.back().at.line == place.at.line)
    {
        return;
    }
    mBack.push_back(place);
    constexpr size_t PLACES = 50;
    if (mBack.size() > PLACES)
    {
        mBack.erase(mBack.begin());
    }
}

void ALFloaterScriptStudio::goBack(bool forward)
{
    std::vector<NavPlace>& from = forward ? mForward : mBack;
    std::vector<NavPlace>& to   = forward ? mBack : mForward;
    while (!from.empty())
    {
        const NavPlace place = from.back();
        from.pop_back();
        // A tab closed since is passed over.
        const size_t index = indexOf(place.doc);
        if (index == NONE)
        {
            continue;
        }
        if (Doc* here = active(); here && here->loaded)
        {
            to.push_back(NavPlace{ here->id, here->shownText()->caret(), here->shownView() });
        }
        mWalking = false;
        if (index != mActive)
        {
            activate(index);
        }
        // In the view it was in, where the tab still has it.
        Doc& doc = *mDocs[index];
        showView(doc, place.view);
        ALCodeEditor& text = *doc.shownText();
        text.goTo(text.document().clamp(place.at));
        text.setFocus(true);
        return;
    }
}

void ALFloaterScriptStudio::goToLine()
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    // A line of the view in front: of the expansion, while it is the one
    // being read.
    const std::string         id     = doc->id;
    const Doc::View           view   = doc->shownView();
    const ALTextPos           was    = doc->shownText()->caret();
    const LLHandle<LLFloater> handle = getHandle();
    // The editor at the place typed, while it is typed; return leaves it
    // there, and so does looking away, escape puts it back.
    auto docOf = [handle, id]() -> Doc* {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(id) : NONE;
        return index == NONE ? nullptr : studio->mDocs[index].get();
    };
    auto placeOf = [](const Doc& doc, const std::string& typed, S32& line, S32& column) {
        placeTyped(typed, line, column);
        const S32 count = doc.shownText()->document().lineCount();
        return line >= 1 && line <= count;
    };
    ALQuickOpen* quick = quickOpen(
        {}, getString("GoToLinePlaceholder"), getString("GoToLineTitle"),
        [handle, docOf, placeOf, was, view](const std::string& typed) {
            Doc* doc = docOf();
            if (!doc)
            {
                return;
            }
            ALCodeEditor& text = *doc->shownText();
            S32           line, column;
            if (placeOf(*doc, typed, line, column))
            {
                // Gone from where the caret was before the line was typed,
                // which the preview has moved it from since.
                if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                {
                    studio->rememberPlace(NavPlace{ doc->id, was, view });
                }
                text.goTo(column > 0 ? text.document().posAtDisplayColumn(line - 1, column - 1, text.getTabWidth()) : ALTextPos(line - 1, 0));
            }
            else
            {
                text.goTo(was);
            }
            text.setFocus(true);
        },
        mEditorHost, 420, ALQuickOpen::heightForRows(1),
        [docOf, was]() {
            if (Doc* doc = docOf())
            {
                doc->shownText()->goTo(was);
            }
        },
        {},
        // Looked away from: the line it went to stands, since that is
        // what was looked at, and the way back from it is kept, as a
        // line gone to by Return keeps it.
        [handle, docOf, was, view]() {
            ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
            Doc*                   doc    = docOf();
            if (studio && doc && doc->shownText()->caret() != was)
            {
                studio->rememberPlace(NavPlace{ doc->id, was, view });
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
        ALCodeEditor&              text = *doc->shownText();
        S32                        line, column;
        const bool                 there = placeOf(*doc, typed, line, column);
        LLStringUtil::format_map_t args;
        args["[COUNT]"] = std::to_string(text.document().lineCount());
        args["[LINE]"]  = std::to_string(line);
        args["[COL]"]   = std::to_string(column);
        std::string trimmed = typed;
        LLStringUtil::trim(trimmed);
        if (trimmed.empty())
        {
            quick->setHint(studio->getString("GoToLineHint", args));
            text.goTo(was);
        }
        else if (there)
        {
            quick->setHint(studio->getString(column > 0 ? "GoToLineGoColumn" : "GoToLineGo", args));
            text.goTo(column > 0 ? text.document().posAtDisplayColumn(line - 1, column - 1, text.getTabWidth()) : ALTextPos(line - 1, 0));
        }
        else
        {
            quick->setHint(studio->getString("GoToLineNone", args));
        }
    });
    quick->setQuery(std::string());
}

void ALFloaterScriptStudio::showCommandPalette()
{
    showQuickOpen(true);
}

std::vector<ALQuickOpen::Candidate> ALFloaterScriptStudio::paletteCommands()
{
    std::vector<ALQuickOpen::Candidate> candidates;
    LLMenuBarGL*                        bar = menuBar();
    if (!bar)
    {
        return candidates;
    }
    // Every command the menus hold that could be given now, by the path
    // of menus it is under, with its keys beside it: the menus searched
    // by name rather than walked.
    std::function<void(LLView*, const std::string&)> collect = [&](LLView* menu, const std::string& path) {
        for (LLView* child : *menu->getChildList())
        {
            if (LLMenuItemBranchGL* branch = dynamic_cast<LLMenuItemBranchGL*>(child))
            {
                if (LLMenuGL* under = branch->getBranch())
                {
                    collect(under, path + branch->getLabel() + " \xE2\x80\xBA ");
                }
                continue;
            }
            LLMenuItemGL* item = dynamic_cast<LLMenuItemGL*>(child);
            if (!item || dynamic_cast<LLMenuItemSeparatorGL*>(item) || item->getLabel().empty() || item->getName() == "command_palette" ||
                item->getName() == "quick_open")
            {
                continue;
            }
            // Enabled as the menu would show it on opening.
            item->buildDrawLabel();
            if (!item->getEnabled() || !item->getVisible())
            {
                continue;
            }
            ALQuickOpen::Candidate one;
            one.label  = path + item->getLabel();
            one.detail = item->getAcceleratorString();
            one.value  = "cmd:" + item->getName();
            // A toggle says which way it is set, before its keys: the menu
            // shows a mark, and a row here has none.
            if (dynamic_cast<LLMenuItemCheckGL*>(item))
            {
                const std::string state = getString(mCommands.checked(item->getName()) ? "PaletteOn" : "PaletteOff");
                one.detail              = one.detail.empty() ? state : state + "   " + one.detail;
            }
            candidates.push_back(std::move(one));
        }
    };
    collect(bar, std::string());
    return candidates;
}

std::vector<ALQuickOpen::Candidate> ALFloaterScriptStudio::paletteScripts(std::vector<GoTo>& targets)
{
    std::vector<ALQuickOpen::Candidate> candidates;
    std::set<std::string>               listed;
    const auto add = [&](GoTo target, const std::string& label, const std::string& detail) {
        ALQuickOpen::Candidate one;
        one.label  = label;
        one.detail = detail;
        one.value  = "go:" + std::to_string(targets.size());
        candidates.push_back(std::move(one));
        targets.push_back(std::move(target));
    };
    // Every tab open, this window's first, another window's said by its
    // title.
    std::vector<ALFloaterScriptStudio*> windows = { this };
    for (LLFloater* floater : LLFloaterReg::getFloaterList("script_studio"))
    {
        ALFloaterScriptStudio* other = ALViewType::as<ALFloaterScriptStudio>(floater);
        if (other && other != this && other->getVisible())
        {
            windows.push_back(other);
        }
    }
    for (ALFloaterScriptStudio* window : windows)
    {
        for (const std::unique_ptr<Doc>& each : window->mDocs)
        {
            const Doc&        doc   = *each;
            const std::string where = !doc.file.empty()         ? doc.file
                                      : doc.notecard            ? getString("TabNotecardTip")
                                      : doc.ref.inInventory()   ? getString("TabInventoryTip")
                                      : !doc.objectName.empty() ? doc.objectName
                                                                : getString("TabObjectTip");
            GoTo target;
            target.kind   = GoTo::Kind::Tab;
            target.window = window->getHandle();
            target.id     = doc.id;
            add(std::move(target), doc.name, window == this ? where : window->getTitle() + "  \xC2\xB7  " + where);
            listed.insert(doc.file.empty() ? doc.ref.id() : "disk:" + doc.file);
        }
    }
    // The scripts and notecards of the objects the explorer shows, open or
    // not yet.
    for (const ALScriptExplorerModel::Object& object : mExplorerPane->model().objects())
    {
        for (const ALScriptExplorerModel::Prim& prim : object.prims)
        {
            for (const ALScriptWorkspace::Item& item : prim.items)
            {
                const ALScriptRef ref(prim.id, item.id);
                if (!listed.insert(ref.id()).second)
                {
                    continue;
                }
                GoTo target;
                target.kind = GoTo::Kind::Script;
                target.ref  = ref;
                target.name = item.name;
                add(std::move(target), item.name, prim.name.empty() || prim.name == object.name ? object.name : object.name + " \xE2\x80\xBA " + prim.name);
            }
        }
    }
    // Then what was opened lately and is not open now.
    for (const Recent& recent : mRecentScripts)
    {
        if (!listed.insert(recent.ref.id()).second)
        {
            continue;
        }
        GoTo target;
        target.kind = GoTo::Kind::Script;
        target.ref  = recent.ref;
        target.name = recent.name;
        add(std::move(target), recent.name, getString("QuickOpenRecent"));
    }
    for (const std::string& path : mRecentFiles)
    {
        if (!listed.insert("disk:" + path).second)
        {
            continue;
        }
        GoTo target;
        target.kind = GoTo::Kind::File;
        target.path = path;
        add(std::move(target), gDirUtilp->getBaseFileName(path), path);
    }
    // Last, every script and notecard in the inventory, outside the Trash,
    // with the folder it is in. A link is its item, listed once.
    if (gInventory.isInventoryUsable())
    {
        LLInventoryModel::cat_array_t  folders;
        LLInventoryModel::item_array_t items;
        LLIsOneOfTypes                 wanted({ LLAssetType::AT_LSL_TEXT, LLAssetType::AT_NOTECARD });
        gInventory.collectDescendentsIf(gInventory.getRootFolderID(), folders, items, LLInventoryModel::EXCLUDE_TRASH, wanted);
        std::map<LLUUID, std::string> paths;
        const auto path_of = [&paths](const LLUUID& folder) -> const std::string& {
            if (const auto known = paths.find(folder); known != paths.end())
            {
                return known->second;
            }
            std::vector<std::string> names;
            for (const LLViewerInventoryCategory* at = gInventory.getCategory(folder);
                 at && at->getUUID() != gInventory.getRootFolderID(); at = gInventory.getCategory(at->getParentUUID()))
            {
                names.push_back(at->getName());
            }
            std::string path;
            for (auto name = names.rbegin(); name != names.rend(); ++name)
            {
                path += (path.empty() ? "" : " \xE2\x80\xBA ") + *name;
            }
            return paths.emplace(folder, path.empty() ? LLTrans::getString("InvFolder My Inventory") : path).first->second;
        };
        for (const LLPointer<LLViewerInventoryItem>& item : items)
        {
            const ALScriptRef ref(LLUUID::null, item->getLinkedUUID());
            if (!listed.insert(ref.id()).second)
            {
                continue;
            }
            const LLViewerInventoryItem* real = gInventory.getItem(ref.item);
            if (!real)
            {
                continue;
            }
            GoTo target;
            target.kind = GoTo::Kind::Script;
            target.ref  = ref;
            target.name = real->getName();
            add(std::move(target), real->getName(), path_of(real->getParentUUID()));
        }
    }
    return candidates;
}

void ALFloaterScriptStudio::showQuickOpen(bool commands)
{
    auto                                targets        = std::make_shared<std::vector<GoTo>>();
    std::vector<ALQuickOpen::Candidate> command_list   = paletteCommands();
    std::vector<ALQuickOpen::Candidate> script_list    = paletteScripts(*targets);
    const LLHandle<LLFloater>           handle         = getHandle();
    ALQuickOpen*                        quick          = quickOpen(commands ? command_list : script_list, getString("QuickOpenPlaceholder"),
                                                                   getString("QuickOpenTitle"), [handle, targets](const std::string& value) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        if (!studio)
        {
            return;
        }
        if (value.compare(0, 4, "cmd:") == 0)
        {
            LLMenuBarGL* bar = studio->menuBar();
            if (LLMenuItemGL* item = bar ? bar->findChild<LLMenuItemGL>(value.substr(4), true) : nullptr)
            {
                if (Doc* doc = studio->active())
                {
                    // The command is about the view the palette came up over.
                    studio->focusShown(*doc);
                }
                item->onCommit();
            }
            return;
        }
        if (value.compare(0, 3, "go:") != 0)
        {
            return;
        }
        const size_t at = static_cast<size_t>(std::atoi(value.c_str() + 3));
        if (at >= targets->size())
        {
            return;
        }
        const GoTo& to = (*targets)[at];
        switch (to.kind)
        {
            case GoTo::Kind::Tab:
                if (ALFloaterScriptStudio* window = ALViewType::as<ALFloaterScriptStudio>(to.window.get()))
                {
                    const size_t index = window->indexOf(to.id);
                    if (index != NONE)
                    {
                        window->openFloater(window->getKey());
                        window->setFocus(true);
                        window->activate(index, true);
                    }
                }
                break;
            case GoTo::Kind::Script:
                studio->openScript(to.ref, to.name);
                break;
            case GoTo::Kind::File:
                studio->openFile(to.path, false);
                break;
        }
    }, mEditorHost);
    if (!quick)
    {
        return;
    }
    // A `>` typed at the start asks for the commands, and taking it away for
    // the scripts again, as in Visual Studio Code.
    quick->setPrefix(">");
    auto shown           = std::make_shared<bool>(commands);
    mQuickModeConnection = quick->onQueryChanged(
        [quick, shown, command_list = std::move(command_list), script_list = std::move(script_list)](const std::string& typed) {
            const bool now = !typed.empty() && typed.front() == '>';
            if (now != *shown)
            {
                *shown = now;
                quick->setCandidates(now ? command_list : script_list);
            }
        });
    // Asked for one way while it is up the other: the `>` put in, or taken
    // out, with the list to match.
    if (commands)
    {
        quick->setQuery(">");
    }
    else if (!quick->query().empty() && quick->query().front() == '>')
    {
        quick->setQuery(std::string());
    }
}

void ALFloaterScriptStudio::showAllTabs()
{
    if (mDocs.empty())
    {
        return;
    }
    // Every tab by its name, as the strip has it, with where it lives or
    // that it is unsaved beside it: what the strip's list button offers
    // once the tabs run past it, and the Go menu at any time.
    std::vector<ALQuickOpen::Candidate> candidates;
    for (const ALTabStrip::Tab& tab : mTabs->tabs())
    {
        ALQuickOpen::Candidate one;
        one.label  = tab.label;
        one.detail = tab.dirty ? getString("TabUnsaved") : tab.detail;
        one.value  = tab.value;
        candidates.push_back(std::move(one));
    }
    const LLHandle<LLFloater> handle = getHandle();
    quickOpen(std::move(candidates), getString("AllTabsPlaceholder"), getString("AllTabsTitle"), [handle](const std::string& value) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->onTabChosen(value);
            if (Doc* doc = studio->active())
            {
                focusShown(*doc);
            }
        }
    }, mEditorHost);
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
        one.value  = outlineValue(*doc, i);
        candidates.push_back(std::move(one));
    }
    // The script it was asked over, by its id: a check answering while the
    // list is up replaces the outline it was made from.
    const LLHandle<LLFloater> handle = getHandle();
    const std::string         id     = doc->id;
    quickOpen(std::move(candidates), getString("GoToSymbolPlaceholder"), getString("GoToSymbolTitle"), [handle, id](const std::string& value) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           at     = studio ? studio->indexOf(id) : NONE;
        Doc*                   doc    = at != NONE ? studio->mDocs[at].get() : nullptr;
        const size_t           index  = doc ? outlineEntryOf(*doc, value) : NONE;
        if (index != NONE)
        {
            studio->noteJump();
            if (at != studio->mActive)
            {
                studio->activate(at);
            }
            ALCodeEditor& source = studio->sourceInFront(*doc);
            source.goTo(rangeOf(doc->outline[index].nameSpan));
            source.setFocus(true);
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
    // The caret of the view in front, which the trailer reads. What the
    // breadcrumb, the lit references and the inspector say is of the
    // source, and waits while the expansion is being read.
    const ALCodeEditor& shown  = *doc->shownText();
    const bool          source = doc->shownView() == Doc::View::Source;
    const ALTextPos     caret  = shown.caret();
    const F64           now    = LLTimer::getTotalSeconds();
    if (mWalking && shown.hasFocus())
    {
        mWalking = false;
    }
    if (caret != doc->caretSeen)
    {
        doc->caretSeen  = caret;
        doc->inspectDue = source ? now + ANALYSIS_DELAY : 0.0;
        refreshBreadcrumb(*doc);
        // The lit places go once the caret has left them all.
        if (source && !doc->editor->highlights().empty() && !doc->editor->highlighted(caret))
        {
            doc->editor->clearHighlights();
        }
    }
    if (source && doc->inspectDue > 0.0 && now >= doc->inspectDue)
    {
        doc->inspectDue = 0.0;
        const ALTextRange word    = doc->editor->identifierAtCaret();
        const U32         version = doc->editor->document().version();
        if (word.empty())
        {
            // No name here: what is wrong here, where anything is, and
            // otherwise the last name's words stay, rather than the pane
            // blanking at every space and bracket the caret passes.
            const std::string problems = problemsAt(*doc, caret);
            if (!problems.empty())
            {
                doc->inspectAt = ALTextPos(-1, -1);
                showSymbol(problems);
            }
        }
        else if (word.begin != doc->inspectAt || version != doc->inspectVersion)
        {
            doc->inspectAt      = word.begin;
            doc->inspectVersion = version;
            askAnalyzer(*doc, ALScriptAnalysis::Kind::Inspect, word.begin);
        }
    }
}

void ALFloaterScriptStudio::inspected(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at)
{
    // Still about the word the caret is on, and this script: by the
    // source's place it was asked about, which the result's is not where
    // the analyzer read the expansion.
    if (&doc != active() || at != doc.inspectAt)
    {
        return;
    }
    std::string      text;
    std::vector<S32> code_lines;
    Declared         declared;
    auto             lines_so_far = [&text]() { return static_cast<S32>(std::count(text.begin(), text.end(), '\n')); };
    if (result.hover.found)
    {
        text = result.hover.label;
        code_lines.push_back(0);
        LLStringUtil::format_map_t args;
        declared = declaredOf(doc, result);
        if (declared.line >= 0)
        {
            args["[LINE]"] = std::to_string(declared.line + 1);
            args["[FILE]"] = declared.name;
            text += "\n" + getString(declared.path.empty() ? "InspectDeclared" : "InspectDeclaredIn", args);
        }
        if (!result.hover.expected.empty())
        {
            args["[TYPE]"] = result.hover.expected;
            text += "\n" + getString("HoverExpected", args);
        }
        if (!result.hover.typeDetail.empty())
        {
            text += "\n\n";
            const S32 first = lines_so_far();
            text += result.hover.typeDetail;
            for (S32 line = first; line <= lines_so_far(); ++line)
            {
                code_lines.push_back(line);
            }
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
    showSymbol(text, declared, code_lines);
}

void ALFloaterScriptStudio::showSymbol(const std::string& text, const Declared& declared, const std::vector<S32>& code_lines)
{
    mSymbol->setText(text);
    std::vector<ALTextView::Style> styles;
    if (Doc* doc = active(); doc && !code_lines.empty())
    {
        // The declaration read as code: its words in the colours the
        // script's own text gives them.
        const ALTextRange word = doc->editor->identifierAtCaret();
        const std::string name = doc->editor->document().text(word);
        const ALSyntaxKind kind = word.empty() ? ALSyntaxKind::Text : doc->editor->semanticKindAt(word.begin);
        for (const S32 line : code_lines)
        {
            if (line < mSymbol->document().lineCount())
            {
                doc->editor->styleAsCode(*mSymbol, line, styles, name, kind);
            }
        }
    }
    mSymbol->setStyles(std::move(styles));
    const S32 lines = mSymbol->document().lineCount();
    for (S32 line = 0; line < lines; ++line)
    {
        mSymbol->linkUrlsOn(line);
    }
    // The declaration's line comes right after the name.
    if (declared.line >= 0 && lines > 1 && mSymbol->document().lineLength(1) > 0)
    {
        ALTextView::Substitution to_line;
        to_line.range           = ALTextRange(ALTextPos(1, 0), mSymbol->document().lineEnd(1));
        to_line.link            = true;
        to_line.tooltip         = getString("InspectDeclaredTip");
        to_line.value           = declared.value();
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
    // By an index over the words, built with them and thrown away with
    // them: this is asked on every hover, every completion and every
    // settling of the caret, over some hundreds of words.
    static boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> index[2];
    static U32                                                                              indexed[2] = { 0, 0 };
    const std::vector<Vocab>&                                                               words      = vocabulary(lua);
    const size_t                                                                            which      = lua ? 1 : 0;
    if (indexed[which] != sVocabularyBuilds[which])
    {
        index[which].clear();
        index[which].reserve(words.size());
        for (size_t i = 0; i < words.size(); ++i)
        {
            index[which].emplace(words[i].text, i);
        }
        indexed[which] = sVocabularyBuilds[which];
    }
    const auto found = index[which].find(name);
    return found == index[which].end() ? nullptr : &words[found->second];
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
        text += "\n" + ALCodeEditor::deprecatedNote();
    }
    if (!word.tooltip.empty())
    {
        text += "\n\n" + word.tooltip;
    }
    text += "\n" + helpUrl(lua, word.text);
    showSymbol(text, Declared(), { 0 });
}

void ALFloaterScriptStudio::reference(Doc& doc)
{
    mFolds.setCollapsed("inspector", false);
    // The word at the caret of the view in front: a word of the language
    // reads the same in the expansion as in the source.
    const ALCodeEditor& shown = *doc.shownText();
    const ALTextRange   word  = shown.identifierAtCaret();
    const std::string   name  = shown.document().text(word);
    if (const Vocab* known = vocabWord(doc.language.lua, name))
    {
        showReference(*known, doc.language.lua);
        return;
    }
    // A word of the script's own: what the analyzer knows of it, asked
    // for now rather than a moment after the caret settles -- of the
    // source, whose places the analyzer answers in.
    if (!word.empty() && doc.shownView() == Doc::View::Source)
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
        // By the word, which new definitions arriving meanwhile keep.
        one.value  = words[i].text;
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
        if (const Vocab* word = vocabWord(lua, value))
        {
            studio->showReference(*word, lua);
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
    // The symbols as a tree, each under what holds it -- the outline is
    // flat, each entry after its holder one deeper -- keyed by the names
    // down to it, so that a fold outlives a check that numbers them anew.
    const size_t count = doc.outline.size();
    std::vector<std::vector<size_t>> children(count);
    std::vector<size_t>              roots;
    mOutlineKeys.assign(count, std::string());
    mOutlineParents.assign(count, false);
    {
        std::vector<size_t> holders;
        for (size_t i = 0; i < count; ++i)
        {
            const S32 depth = doc.outline[i].depth;
            while (!holders.empty() && doc.outline[holders.back()].depth >= depth)
            {
                holders.pop_back();
            }
            if (holders.empty())
            {
                roots.push_back(i);
                mOutlineKeys[i] = doc.outline[i].name;
            }
            else
            {
                children[holders.back()].push_back(i);
                mOutlineParents[holders.back()] = true;
                mOutlineKeys[i]                 = mOutlineKeys[holders.back()] + "\x1f" + doc.outline[i].name;
            }
            holders.push_back(i);
        }
    }
    // In the order asked for, under each holder: as written, by name, or
    // by kind and then name.
    const std::string sort = mOutlineSort ? mOutlineSort->getValue().asString() : std::string("order");
    const auto        before = [&](size_t a, size_t b) {
        const ALScriptOutlineEntry& x = doc.outline[a];
        const ALScriptOutlineEntry& y = doc.outline[b];
        if (sort == "kind" && x.kind != y.kind)
        {
            return static_cast<S32>(x.kind) < static_cast<S32>(y.kind);
        }
        if (sort == "name" || sort == "kind")
        {
            const S32 said = LLStringUtil::compareDict(x.name, y.name);
            if (said != 0)
            {
                return said < 0;
            }
        }
        return a < b;
    };
    std::string filter = mOutlineFilter ? mOutlineFilter->getText() : std::string();
    LLStringUtil::trim(filter);
    // The rows: through the filter, every symbol with the letters, flat;
    // else the tree, down to what is folded shut.
    struct Row
    {
        size_t index = 0;
        S32    depth = 0;
    };
    std::vector<Row> rows;
    if (!filter.empty())
    {
        std::vector<size_t> matched;
        for (size_t i = 0; i < count; ++i)
        {
            if (ALStringMatch::containsNoCase(doc.outline[i].name, filter))
            {
                matched.push_back(i);
            }
        }
        std::stable_sort(matched.begin(), matched.end(), before);
        for (const size_t i : matched)
        {
            rows.push_back({ i, 0 });
        }
    }
    else
    {
        std::function<void(std::vector<size_t>, S32)> walk = [&](std::vector<size_t> level, S32 depth) {
            std::stable_sort(level.begin(), level.end(), before);
            for (const size_t i : level)
            {
                rows.push_back({ i, depth });
                if (!children[i].empty() && !doc.outlineFolded.contains(mOutlineKeys[i]))
                {
                    walk(children[i], depth + 1);
                }
            }
        };
        walk(roots, 0);
    }
    // What the rows say. A check comes at every pause in typing and most
    // change nothing the outline shows; the list is only made again where
    // something did, and then keeps its scroll, so that whoever is
    // reading down it is not sent back to the top.
    const std::string open   = getString("ArrowOpen");
    const std::string folded = getString("ArrowFolded");
    std::vector<std::string> said;
    said.reserve(rows.size() + 1);
    for (const Row& row : rows)
    {
        const ALScriptOutlineEntry& entry = doc.outline[row.index];
        const bool parent = filter.empty() && mOutlineParents[row.index];
        const std::string arrow = !parent ? std::string("   ") : doc.outlineFolded.contains(mOutlineKeys[row.index]) ? folded : open;
        said.push_back(std::string(static_cast<size_t>(row.depth) * 4, ' ') + arrow + entry.name + "|" +
                       std::to_string(static_cast<S32>(entry.kind)) + "|" + std::to_string(row.index) + "|" + entry.detail);
    }
    said.push_back(doc.id);
    if (said != mOutlineSaid)
    {
        mOutlineSaid     = said;
        const S32 scroll = mOutline->getScrollPos();
        mOutline->deleteAllItems();
        for (size_t r = 0; r < rows.size(); ++r)
        {
            const ALScriptOutlineEntry& entry = doc.outline[rows[r].index];
            LLSD                        row;
            row["value"]                = static_cast<S32>(rows[r].index);
            row["columns"][0]["column"] = "icon";
            row["columns"][0]["type"]   = "icon";
            row["columns"][0]["value"]  = imageNameOf(entry.kind);
            row["columns"][0]["tool_tip"] = kindName(entry.kind);
            row["columns"][1]["column"] = "symbol";
            // Nested under what holds it, with the arrow that folds what
            // it holds; what it is, and its declaration, on the mouse.
            row["columns"][1]["value"]    = said[r].substr(0, said[r].find('|'));
            row["columns"][1]["tool_tip"] = entry.detail.empty() ? kindName(entry.kind) : kindName(entry.kind) + "\n" + entry.detail;
            mOutline->addElement(row);
        }
        mOutline->setScrollPos(scroll);
    }
    mOutline->setCommentText(doc.outline.empty() ? getString(doc.loaded ? "NoOutline" : "NoOutlineYet")
                             : rows.empty()      ? getString("OutlineNoMatch")
                                                 : LLStringUtil::null);
    refreshBreadcrumb(doc);
    followCaretInOutline(doc);
}

bool ALFloaterScriptStudio::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // An arrow in the outline folds its symbol.
    if (mask == MASK_NONE && mOutline && mOutline->isInVisibleChain())
    {
        S32 lx = 0, ly = 0;
        localPointToOtherView(x, y, &lx, &ly, mOutline);
        size_t index = 0;
        if (mOutline->pointInView(lx, ly) && outlineArrowAt(lx, ly, index))
        {
            foldOutline(index);
            return true;
        }
    }
    return ALStudioFloater::handleMouseDown(x, y, mask);
}

bool ALFloaterScriptStudio::outlineArrowAt(S32 x, S32 y, size_t& index)
{
    LLScrollListItem* item = mOutline->hitItem(x, y);
    if (!item)
    {
        return false;
    }
    index = static_cast<size_t>(item->getValue().asInteger());
    if (index >= mOutlineParents.size() || !mOutlineParents[index])
    {
        return false;
    }
    // The arrow comes after the symbol's indent, from the name column's
    // edge: as far as the indent and the arrow go.
    const LLScrollListCell* name = item->getColumn(1);
    const std::string       text = name ? name->getValue().asString() : std::string();
    const size_t            arrow_end = text.find_first_not_of(' ') == std::string::npos ? 0 : text.find_first_not_of(' ') + getString("ArrowOpen").size();
    const LLScrollListColumn* icon = mOutline->getColumn("icon");
    const S32 left  = mOutline->getItemListRect().mLeft + (icon ? icon->getWidth() : 0) + mOutline->getColumnPadding();
    const S32 right = left + LLFontGL::getFontSansSerifSmall()->getWidth(text.substr(0, arrow_end)) + 4;
    return x >= left - 2 && x <= right;
}

void ALFloaterScriptStudio::foldOutline(size_t index, std::optional<bool> folded)
{
    Doc* doc = active();
    if (!doc || index >= mOutlineKeys.size())
    {
        return;
    }
    const std::string& key   = mOutlineKeys[index];
    const bool         shut  = doc->outlineFolded.contains(key);
    const bool         want  = folded.value_or(!shut);
    if (!mOutlineParents[index] || want == shut)
    {
        // Left on a symbol with nothing to fold: to what holds it, shown
        // as any row walked to is.
        if (folded.has_value() && *folded)
        {
            const size_t at = key.rfind('\x1f');
            if (at != std::string::npos)
            {
                const std::string holder = key.substr(0, at);
                for (size_t i = 0; i < mOutlineKeys.size(); ++i)
                {
                    if (mOutlineKeys[i] == holder && mOutline->selectByValue(LLSD(static_cast<S32>(i))))
                    {
                        mOutline->scrollToShowSelected();
                        onOutlineChosen(false);
                        break;
                    }
                }
            }
        }
        return;
    }
    if (want)
    {
        doc->outlineFolded.insert(key);
    }
    else
    {
        doc->outlineFolded.erase(key);
    }
    refreshOutline(*doc);
    mOutline->selectByValue(LLSD(static_cast<S32>(index)));
}

void ALFloaterScriptStudio::followCaretInOutline(Doc& doc)
{
    if (&doc != active())
    {
        return;
    }
    // The innermost symbol the caret is in, as the breadcrumb found it.
    if (doc.crumbPath.empty())
    {
        mOutline->deselectAllItems(true);
        return;
    }
    LLScrollListItem* now = mOutline->getFirstSelected();
    for (auto step = doc.crumbPath.rbegin(); step != doc.crumbPath.rend(); ++step)
    {
        const S32 index = static_cast<S32>(*step);
        if (now && now->getValue().asInteger() == index)
        {
            return;
        }
        if (mOutline->selectByValue(LLSD(index)))
        {
            mOutline->scrollToShowSelected();
            return;
        }
    }
    mOutline->deselectAllItems(true);
}

void ALFloaterScriptStudio::refreshBreadcrumb(Doc& doc)
{
    if (&doc != active())
    {
        return;
    }
    const ALTextPos               caret = doc.editor->caret();
    // The path the caret is in: which outline entry at each depth holds
    // it. The crumbs are built from the outline, which a caret move
    // does not touch, so the bar is told only where the path itself has
    // changed -- and it is asked on every key. The outline is the
    // source's, so while the expansion is in front, whose lines are other
    // lines, the path is the script alone.
    std::vector<size_t> path;
    if (doc.shownView() == Doc::View::Source)
    {
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
            path.push_back(found);
            parent = found;
        }
    }
    if (mCrumbsShownFor == doc.id && doc.crumbsOf == doc.analysisVersion && doc.crumbPath == path && doc.crumbName == doc.name)
    {
        // The same steps over the same outline: only the trailer, which
        // says where the caret is.
        refreshTrailer(doc);
        return;
    }
    doc.crumbsOf  = doc.analysisVersion;
    doc.crumbPath = path;
    doc.crumbName = doc.name;
    followCaretInOutline(doc);

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
            root.alternatives.emplace_back(doc.outline[i].name, outlineValue(doc, i));
        }
    }
    crumbs.push_back(std::move(root));

    // Then each symbol on the path, the outermost first, offering the
    // others at its depth in the same holder.
    size_t parent = NONE;
    for (S32 depth = 0; depth < static_cast<S32>(path.size()); ++depth)
    {
        const size_t found = path[static_cast<size_t>(depth)];
        ALJumpBar::Crumb crumb;
        crumb.label    = doc.outline[found].name;
        crumb.value    = outlineValue(doc, found);
        args["[NAME]"] = crumb.label;
        crumb.toolTip  = getString("CrumbTip", args);
        for (size_t i = 0; i < doc.outline.size(); ++i)
        {
            const ALScriptOutlineEntry& entry = doc.outline[i];
            if (entry.depth == depth && (parent == NONE || within(entry.span, doc.outline[parent].span)))
            {
                crumb.alternatives.emplace_back(entry.name, outlineValue(doc, i));
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
    mCrumbsShownFor = doc.id;
    refreshTrailer(doc);
}

void ALFloaterScriptStudio::renameDoc(Doc& doc, const std::string& name)
{
    if (name.empty() || name == doc.name)
    {
        return;
    }
    doc.name = name;
    fillTabs();
    refreshBreadcrumb(doc);
}

void ALFloaterScriptStudio::onCrumbChosen(size_t, const std::string& value)
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    noteJump();
    ALCodeEditor& source = sourceInFront(*doc);
    if (value == "top")
    {
        source.goTo(ALTextPos(0, 0));
    }
    else if (const size_t index = outlineEntryOf(*doc, value); index != NONE)
    {
        source.goTo(rangeOf(doc->outline[index].nameSpan));
    }
    source.setFocus(true);
}

// static
std::string ALFloaterScriptStudio::outlineValue(const Doc& doc, size_t index)
{
    return std::to_string(index) + '\n' + doc.outline[index].name;
}

// static
size_t ALFloaterScriptStudio::outlineEntryOf(const Doc& doc, const std::string& value)
{
    // Where it was, if what is there now has its name; else the first of
    // its name; else nothing.
    const size_t      cut   = value.find('\n');
    const std::string name  = cut == std::string::npos ? std::string() : value.substr(cut + 1);
    const size_t      index = static_cast<size_t>(atoi(value.c_str()));
    if (index < doc.outline.size() && doc.outline[index].name == name)
    {
        return index;
    }
    for (size_t i = 0; i < doc.outline.size(); ++i)
    {
        if (doc.outline[i].name == name)
        {
            return i;
        }
    }
    return NONE;
}

void ALFloaterScriptStudio::onOutlineChosen(bool to_editor)
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
        noteJump(!to_editor);
        sourceInFront(*doc).goTo(rangeOf(doc->outline[index].nameSpan));
        revealed(mOutline, to_editor);
    }
}

void ALFloaterScriptStudio::showBottom(const char* tab, bool focus)
{
    mFolds.setCollapsed("bottom", false);
    mBottomTabs->selectTabByName(tab);
    if (!focus)
    {
        return;
    }
    // Asked for by its key: the keyboard to the list, to walk it.
    const std::string name(tab);
    LLUICtrl* list = name == "problems_tab" ? static_cast<LLUICtrl*>(mProblemsPane->list())
                     : name == "references_tab" ? static_cast<LLUICtrl*>(mReferences)
                     : name == "output_tab"     ? static_cast<LLUICtrl*>(mOutputPane->view())
                     : name == "weights_tab"    ? static_cast<LLUICtrl*>(mWeightsParts)
                                                : nullptr;
    if (list)
    {
        list->setFocus(true);
        if (LLScrollListCtrl* rows = dynamic_cast<LLScrollListCtrl*>(list); rows && !rows->getFirstSelected())
        {
            // Somewhere to start from: the first row there is to choose,
            // in the order shown.
            rows->updateSort();
            const std::vector<LLScrollListItem*> all = rows->getAllData();
            for (size_t i = 0; i < all.size(); ++i)
            {
                if (all[i]->getEnabled())
                {
                    rows->selectNthItem(static_cast<S32>(i));
                    break;
                }
            }
        }
    }
}

void ALFloaterScriptStudio::report(const std::string& text, bool failure, const Doc* doc, const std::vector<std::string>& actions)
{
    setStatus(text, failure);
    mOutputPane->said(text, failure, doc, actions);
}

// --- find in files -------------------------------------------------------------------

void ALFloaterScriptStudio::findInFiles()
{
    showBottom("search_tab");
    mSearchPane->focusQuery(active());
}

std::string ALFloaterScriptStudio::whereIs(const Doc& doc) const
{
    // What a row says an open script is in: its object, by the name it has
    // now; nothing for one in the inventory or on disk.
    return doc.ref.inInventory() ? LLStringUtil::null : ALScriptWorkspace::objectName(gObjectList.findObject(doc.ref.object), getString("ObjectUnnamed"));
}

std::vector<ALScriptSearchPane::Window::Object> ALFloaterScriptStudio::objectsListed() const
{
    std::vector<ALScriptSearchPane::Window::Object> listed;
    for (const ALScriptExplorerModel::Object& object : mExplorerPane->model().objects())
    {
        if (!object.present)
        {
            continue;
        }
        ALScriptSearchPane::Window::Object one;
        one.root = object.root;
        one.name = object.name;
        for (const ALScriptExplorerModel::Prim& prim : object.prims)
        {
            for (const ALScriptWorkspace::Item& item : prim.items)
            {
                one.items.emplace_back(prim.id, item.id);
            }
        }
        listed.push_back(std::move(one));
    }
    return listed;
}

std::string ALFloaterScriptStudio::objectName(const LLUUID& root) const
{
    std::string name;
    for (const ALScriptExplorerModel::Object& one : mExplorerPane->model().objects())
    {
        if (one.root == root)
        {
            name = one.name;
        }
    }
    return name;
}

LLUUID ALFloaterScriptStudio::objectInHand() const
{
    // The active script's, else the one chosen in the explorer.
    LLUUID only;
    if (const Doc* doc = mActive < mDocs.size() ? mDocs[mActive].get() : nullptr; doc && !doc->ref.inInventory())
    {
        if (LLViewerObject* object = gObjectList.findObject(doc->ref.object))
        {
            only = object->getRootEdit() ? object->getRootEdit()->getID() : object->getID();
        }
    }
    if (only.isNull())
    {
        const std::vector<ALScriptExplorerPane::Choice> rows = mExplorerPane->choice();
        if (!rows.empty())
        {
            only = rows.front().root;
        }
    }
    return only;
}

LLUUID ALFloaterScriptStudio::rootOf(const ALScriptRef& ref) const
{
    if (ref.inInventory() || ref.isNull())
    {
        return LLUUID::null;
    }
    LLViewerObject* object = gObjectList.findObject(ref.object);
    return object ? (object->getRootEdit() ? object->getRootEdit()->getID() : object->getID()) : LLUUID::null;
}

ALFloaterScriptStudio::Doc* ALFloaterScriptStudio::openElsewhere(const ALScriptRef& ref)
{
    ALFloaterScriptStudio* holder = indexOf(ref) == NONE ? holderOf(ref, std::string()) : nullptr;
    return holder && holder != this ? holder->mDocs[holder->indexOf(ref)].get() : nullptr;
}

void ALFloaterScriptStudio::fetchForSearch(const ALScriptRef& ref, U32 generation, const std::string& where)
{
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptWorkspace::instance().load(ref, [handle, generation, where](const ALScriptWorkspace::Loaded& loaded) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            // A wrapped script is searched as its author wrote it, and that
            // text kept for a replace to work over.
            std::optional<std::string> text;
            if (loaded.error.empty())
            {
                text = loaded.notecard ? loaded.text : sourceOf(loaded);
            }
            studio->mSearchPane->fetched(generation, where, loaded.ref, loaded.name, text, loaded.notecard);
        }
    });
}

void ALFloaterScriptStudio::confirmReplaceAll(const LLSD& args, std::function<void()> yes)
{
    const LLHandle<LLFloater> handle = getHandle();
    LLNotificationsUtil::add("ScriptStudioReplaceAll", args, LLSD(), [handle, yes](const LLSD& notification, const LLSD& response) {
        if (handle.get() && LLNotificationsUtil::getSelectedOption(notification, response) == 0)
        {
            yes();
        }
    });
}

void ALFloaterScriptStudio::searchResultChosen(const ALScriptSearch::Found& one, const ALTextRange& match, bool to_editor)
{
    const bool gone = !one.doc.empty() && indexOf(one.doc) == NONE;
    if (gone && one.ref.isNull())
    {
        // A file on disk, closed since it was searched: its tab is how it
        // was gone to, and it has no item to open it by.
        return;
    }
    // Open in another window: gone to there when asked, and not while the
    // list is walked, which would take the keyboard from the list.
    if (!to_editor && one.doc.empty() && !one.ref.isNull() && indexOf(one.ref) == NONE)
    {
        if (ALFloaterScriptStudio* holder = holderOf(one.ref, std::string()); holder && holder != this)
        {
            return;
        }
    }
    if (!to_editor && (one.doc.empty() || gone) && deferOpen(mSearchPane->list(), ALScriptPreprocessor::pathOf(one.ref)))
    {
        return;
    }
    noteJump(!to_editor);
    ++mHoldPanes;
    // A script open when it was searched -- a file on disk among them,
    // which has no item to open it by -- is gone to in its tab.
    const size_t open = one.doc.empty() ? NONE : indexOf(one.doc);
    if (open != NONE)
    {
        if (open != mActive)
        {
            activate(open);
        }
        sourceInFront(*mDocs[open]).goTo(match);
    }
    else
    {
        const S32 length = match.end.line == match.begin.line ? match.end.column - match.begin.column : 0;
        goToPlace(one.ref, one.name, match.begin.line, match.begin.column, length);
    }
    --mHoldPanes;
    revealed(mSearchPane->list(), to_editor);
}

void ALFloaterScriptStudio::goToPlace(const ALScriptRef& ref, const std::string& name, S32 line, S32 column, S32 length)
{
    size_t index = indexOf(ref);
    // Open in another window: gone to there.
    if (ALFloaterScriptStudio* holder = index == NONE ? holderOf(ref, std::string()) : nullptr; holder && holder != this)
    {
        holder->openFloater(holder->getKey());
        holder->setFocus(true);
        holder->goToPlace(ref, name, line, column, length);
        return;
    }
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
        ALCodeEditor& source = sourceInFront(doc);
        if (column < 0)
        {
            source.goToLine(line);
        }
        else
        {
            source.goTo(ALTextRange(ALTextPos(line, column), ALTextPos(line, column + length)));
        }
        source.setFocus(true);
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
    // Asked already, and waiting on an answer or a save. The asking is
    // closeFloater's, which knows whether the viewer is quitting.
    return !mClosingWindow;
}

void ALFloaterScriptStudio::quitAnswered(S32 option)
{
    switch (option)
    {
        case 0:
            // Saved, each tab going as its save comes back.
            closeWindowAnswered(0);
            break;
        case 1:
        {
            // Kept, to be opened again with the studio next time; the quit
            // called off where any of it could not be written, since it
            // would go with the viewer.
            bool kept = true;
            for (std::unique_ptr<Doc>& doc : mDocs)
            {
                if (doc->loaded && doc->modifiable && doc->editor->isDirty())
                {
                    kept = mRecovery.keep(*doc, ALScriptRecoveryEntry::State::Kept) && kept;
                }
            }
            if (!kept)
            {
                report(getString("KeepFailed"), true);
                stopClosing();
                break;
            }
            while (!mDocs.empty())
            {
                letGoOf(mDocs.size() - 1, true);
            }
            mClosingWindow = false;
            closeFloater(true);
            break;
        }
        case 2:
            // Let go of -- set aside a week all the same.
            closeWindowAnswered(1);
            break;
        default:
            stopClosing();
            break;
    }
}

void ALFloaterScriptStudio::closeWindowAnswered(S32 option)
{
    if (option != 0 && option != 1)
    {
        stopClosing();
        return;
    }
    // The clean ones go now; the unsaved are saved, each closing as its
    // save comes back, or let go of.
    std::vector<std::string> saving;
    for (size_t i = mDocs.size(); i-- > 0;)
    {
        Doc& doc = *mDocs[i];
        if (option == 0 && doc.unsaved() && doc.modifiable)
        {
            saving.push_back(doc.id);
        }
        else
        {
            letGoOf(i);
        }
    }
    // Every one set to close once saved before any is saved: a file's save
    // is done on the spot, and the close carries on from it, asking about
    // whatever unsaved tab is not yet set to go -- one of these, asked
    // about again after Save was said for all of them.
    for (const std::string& id : saving)
    {
        if (const size_t index = indexOf(id); index != NONE)
        {
            mDocs[index]->save.setCloseAfter(true);
        }
    }
    for (const std::string& id : saving)
    {
        if (mClosingWindow)
        {
            mSaving.saveToClose(id);
        }
        else if (const size_t index = indexOf(id); index != NONE)
        {
            // A save before it stopped the close: this one stays, as it was.
            mDocs[index]->save.setCloseAfter(false);
        }
    }
    if (mClosingWindow && mDocs.empty())
    {
        mClosingWindow = false;
        closeFloater();
    }
}

void ALFloaterScriptStudio::continueClosing()
{
    size_t i = 0;
    while (mClosingWindow && i < mDocs.size())
    {
        Doc& doc = *mDocs[i];
        if (doc.save.closeAfter())
        {
            // On its way: it goes when its save comes back.
            ++i;
            continue;
        }
        if (doc.unsaved() && doc.modifiable)
        {
            // Asked; the answer carries on from here, or stops.
            closeDocument(doc.id);
            return;
        }
        letGoOf(i);
    }
    if (mClosingWindow && mDocs.empty())
    {
        mClosingWindow = false;
        closeFloater();
    }
}

void ALFloaterScriptStudio::popOut(std::optional<LLCoordGL> screen)
{
    Doc* doc = active();
    if (!doc || !doc->loaded || !movable(*doc))
    {
        return;
    }
    // A window of its own, placed beside this one; the script opens
    // there with whatever was typed here, and goes from here without a
    // word, since nothing is lost.
    // A window of its own is a window shown, and asked about as opening one
    // is: a restriction on viewing scripts says no to it as to any.
    const LLSD key(LLUUID::generateNewID().asString());
    if (!LLFloaterReg::canShowInstance("script_studio", key))
    {
        return;
    }
    ALFloaterScriptStudio* window = LLFloaterReg::getTypedInstance<ALFloaterScriptStudio>("script_studio", key);
    if (!window)
    {
        return;
    }
    window->openFloater(window->getKey());
    LLRect rect = getRect();
    if (screen)
    {
        // Its tabs under the mouse, as the tab was carried there.
        S32 x = 0, y = 0;
        gFloaterView->screenPointToLocal(screen->mX, screen->mY, &x, &y);
        rect.setLeftTopAndSize(x - TORN_OFFSET_X, y + TORN_OFFSET_Y, rect.getWidth(), rect.getHeight());
    }
    else
    {
        rect.translate(40, -40);
    }
    window->setShape(rect);
    gFloaterView->adjustToFitScreen(window, false);
    if (!moveActiveTo(window))
    {
        // It could not go -- a file that could not be read there: the tab
        // stays here, and the empty window goes.
        window->closeFloater();
    }
}

void ALFloaterScriptStudio::onTabTorn(const std::string& id, S32 screen_x, S32 screen_y)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    if (index != mActive)
    {
        activate(index);
    }
    // Dropped on another studio window: moved into it, as the tab menu
    // would move it.
    for (LLFloater* floater : LLFloaterReg::getFloaterList("script_studio"))
    {
        ALFloaterScriptStudio* other = ALViewType::as<ALFloaterScriptStudio>(floater);
        if (other && other != this && other->getVisible() && !other->isMinimized() && other->calcScreenRect().pointInRect(screen_x, screen_y))
        {
            moveActiveTo(other);
            return;
        }
    }
    // Back over its own window, where it came from: nothing.
    if (calcScreenRect().pointInRect(screen_x, screen_y))
    {
        return;
    }
    // The only tab: this window taken there, rather than a second one made
    // and this left holding nothing.
    if (mDocs.size() == 1)
    {
        S32 x = 0, y = 0;
        gFloaterView->screenPointToLocal(screen_x, screen_y, &x, &y);
        LLRect rect = getRect();
        rect.setLeftTopAndSize(x - TORN_OFFSET_X, y + TORN_OFFSET_Y, rect.getWidth(), rect.getHeight());
        setShape(rect);
        gFloaterView->adjustToFitScreen(this, false);
        return;
    }
    popOut(LLCoordGL(screen_x, screen_y));
}

bool ALFloaterScriptStudio::movable(const Doc& doc)
{
    // Not while a save of it is on its way: the answer comes to whichever
    // window holds the tab then, where the save is not its own, and a close
    // waiting on it here would wait on a tab gone from here.
    if (doc.saveUnderway())
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString("PopOutWhileSaving", args), true);
        return false;
    }
    return true;
}

bool ALFloaterScriptStudio::moveActiveTo(ALFloaterScriptStudio* window)
{
    Doc* doc = active();
    if (!doc || !doc->loaded || !window || window == this || !movable(*doc))
    {
        return false;
    }
    // What it holds goes with it whole -- the text, the steps that led to
    // it to take back, the caret -- as a kept tab comes back next session.
    ALScriptRecoveryEntry moving = ALScriptStudioRecovery::entryOf(*doc);
    moving.path.clear();
    if (!doc->file.empty())
    {
        // A file on disk has no item to be fetched by: it is read there
        // from where it is, and what was typed here goes over it, unsaved.
        window->openFileHere(doc->file, doc->language.lua);
        const size_t moved = window->indexOf("disk:" + doc->file);
        if (moved == NONE)
        {
            return false;
        }
        Doc& there        = *window->mDocs[moved];
        there.recovering  = moving;
        there.carriedText = moving.text;
        window->takeCarriedText(there);
        there.recovering.reset();
        window->showView(there, doc->view);
    }
    else
    {
        window->openScript(doc->ref, doc->name, moving.text);
        if (const size_t moved = window->indexOf(doc->ref); moved != NONE)
        {
            Doc& there = *window->mDocs[moved];
            there.recovering = moving;
            // And what was picked for its next save.
            if (doc->targetChosen)
            {
                there.carriedTarget = doc->language.compileTarget;
            }
            if (doc->experienceChosen)
            {
                there.carriedExperience = doc->experience;
            }
            ALScriptNotecardTab::carry(*doc, there);
            // Showing what it showed here, once its expansion comes there.
            window->showView(there, doc->view);
        }
    }
    window->openFloater(window->getKey());
    window->setFocus(true);
    letGoOf(mActive, true);
    return true;
}

// --- recovery ------------------------------------------------------------------------

// static
void ALFloaterScriptStudio::offerRecovery()
{
    ALScriptStudioRecovery::offer([]() -> ALScriptStudioRecovery* {
        ALFloaterScriptStudio* studio = LLFloaterReg::showTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD(), TAKE_FOCUS_YES);
        return studio ? &studio->mRecovery : nullptr;
    });
}

bool ALFloaterScriptStudio::recoverElsewhere(const ALScriptRecoveryEntry& entry)
{
    ALFloaterScriptStudio* holder = holderOf(ALScriptRef(entry.object, entry.item), entry.file);
    if (!holder || holder == this)
    {
        return false;
    }
    holder->openFloater(holder->getKey());
    holder->setFocus(true);
    holder->mRecovery.recover(entry);
    return true;
}

ALFloaterScriptStudio::Doc* ALFloaterScriptStudio::openFileTab(const std::string& path, bool lua)
{
    openFile(path, lua);
    const size_t index = indexOf("disk:" + path);
    return index != NONE ? mDocs[index].get() : nullptr;
}

bool ALFloaterScriptStudio::scriptInHand(const ALScriptRef& ref) const
{
    if (ref.inInventory())
    {
        return gInventory.getItem(ref.item) != nullptr;
    }
    LLViewerObject* object = gObjectList.findObject(ref.object);
    return object && !object->isDead();
}

void ALFloaterScriptStudio::activate(Doc& doc)
{
    if (const size_t index = indexOf(doc.id); index != NONE)
    {
        activate(index);
    }
}

void ALFloaterScriptStudio::tabsChanged()
{
    fillTabs();
    refreshToolbar();
}

void ALFloaterScriptStudio::pick(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                                 std::function<void(const std::string& value)> chosen, std::function<void(const std::string& value)> dropped)
{
    quickOpen(std::move(candidates), placeholder, title, std::move(chosen), mEditorHost, 0, 0, {}, std::move(dropped));
}

void ALFloaterScriptStudio::pumpRestores()
{
    if (mPendingRestores.empty() && !mRestoring)
    {
        return;
    }
    const F64 now = LLTimer::getTotalSeconds();
    // Decided first and done after: opening a tab or asking what a prim
    // holds can answer on the spot, and the answer changes the list.
    struct Opening
    {
        ALScriptRef ref;
        std::string name;
        Doc::View   view;
    };
    std::vector<Opening>     opening;
    std::vector<ALScriptRef> asking;
    for (auto it = mPendingRestores.begin(); it != mPendingRestores.end();)
    {
        PendingRestore& one = *it;
        if (now > one.until || holderOf(one.ref, std::string()))
        {
            // Too late, or opened some other way meanwhile.
            it = mPendingRestores.erase(it);
            continue;
        }
        LLViewerObject*        object = one.ref.inInventory() ? nullptr : gObjectList.findObject(one.ref.object);
        const LLInventoryItem* item   = one.ref.inInventory() ? gInventory.getItem(one.ref.item) : object ? object->getInventoryItem(one.ref.item) : nullptr;
        if (item)
        {
            opening.push_back(Opening{ one.ref, item->getName(), one.view });
            it = mPendingRestores.erase(it);
            continue;
        }
        if (object && !one.asked)
        {
            one.asked = true;
            asking.push_back(one.ref);
        }
        ++it;
    }
    for (const Opening& one : opening)
    {
        // Beside what is open, without the keyboard: the author may be
        // typing somewhere by now.
        openScript(one.ref, one.name, std::nullopt, -1, false);
        if (const size_t at = indexOf(one.ref); at != NONE)
        {
            showView(*mDocs[at], one.view);
        }
    }
    // A window made to restore what it had, with nothing of it to be had
    // now nor coming: no window.
    if (mRestoring && mPendingRestores.empty())
    {
        mRestoring = false;
        if (!mMain && mDocs.empty() && opening.empty())
        {
            closeFloater();
            return;
        }
    }
    const LLHandle<LLFloater> handle = getHandle();
    for (const ALScriptRef& ref : asking)
    {
        ALScriptWorkspace::instance().listContents(ref.object, [handle, ref](const ALScriptWorkspace::Contents& contents) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->restoreListed(ref, contents);
            }
        });
    }
}

void ALFloaterScriptStudio::restoreListed(const ALScriptRef& ref, const ALScriptWorkspace::Contents& contents)
{
    const auto waiting = std::find_if(mPendingRestores.begin(), mPendingRestores.end(), [&ref](const PendingRestore& one) { return one.ref == ref; });
    if (waiting == mPendingRestores.end())
    {
        return;
    }
    const auto item = std::find_if(contents.items.begin(), contents.items.end(), [&ref](const ALScriptWorkspace::Item& one) { return one.id == ref.item; });
    if (item != contents.items.end())
    {
        const std::string name = item->name;
        const Doc::View   view = waiting->view;
        mPendingRestores.erase(waiting);
        openScript(ref, name, std::nullopt, -1, false);
        if (const size_t at = indexOf(ref); at != NONE)
        {
            showView(*mDocs[at], view);
        }
    }
    else if (contents.fetched)
    {
        // The object says what it holds, and it is not among it.
        mPendingRestores.erase(waiting);
    }
    else
    {
        // No answer: asked again while there is time.
        waiting->asked = false;
    }
}

void ALFloaterScriptStudio::pumpRecovery()
{
    pumpRestores();
    mRecovery.pump();
    const F64 now = LLTimer::getTotalSeconds();
    // The connection lost: nothing can be saved, and the viewer may go
    // without asking anything, so every unsaved text is written now.
    if (gDisconnected && !mOffline)
    {
        mOffline = true;
        S32 kept = 0;
        for (std::unique_ptr<Doc>& doc : mDocs)
        {
            if (doc->loaded && doc->modifiable && doc->editor->isDirty())
            {
                mRecovery.keep(*doc);
                ++kept;
            }
        }
        if (kept > 0)
        {
            report(counted("OfflineKept", kept), true);
        }
        checkOrphans();
    }
    if (now >= mOrphansChecked + 1.0)
    {
        mOrphansChecked = now;
        checkOrphans();
    }
}

ALFloaterScriptStudio::Doc::Orphan ALFloaterScriptStudio::orphanOf(const Doc& doc) const
{
    if (!doc.loaded)
    {
        return Doc::Orphan::None;
    }
    if (!doc.file.empty())
    {
        return LLFile::isfile(doc.file) ? Doc::Orphan::None : Doc::Orphan::FileGone;
    }
    if (doc.ref.isNull())
    {
        return Doc::Orphan::None;
    }
    if (gDisconnected)
    {
        return Doc::Orphan::Offline;
    }
    // A kept text over a script that may no longer be changed, or that
    // could not be loaded, stays that while there is an item: nothing here
    // says it has changed, and loading it again is tried on its own terms.
    const auto held = [&doc](Doc::Orphan seen) {
        const bool stays = doc.orphan == Doc::Orphan::Locked || doc.orphan == Doc::Orphan::Unloaded;
        return stays && (seen == Doc::Orphan::None || seen == Doc::Orphan::Trashed) ? doc.orphan : seen;
    };
    if (doc.ref.inInventory())
    {
        if (!gInventory.getItem(doc.ref.item))
        {
            return Doc::Orphan::Removed;
        }
        const LLUUID trash = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
        return held(trash.notNull() && gInventory.isObjectDescendentOf(doc.ref.item, trash) ? Doc::Orphan::Trashed : Doc::Orphan::None);
    }
    LLViewerObject* object = gObjectList.findObject(doc.ref.object);
    if (!object || object->isDead())
    {
        return Doc::Orphan::Away;
    }
    // Gone from its object where the region has said what the prim holds;
    // while that is being asked again, as it was.
    for (const ALScriptExplorerModel::Object& one : mExplorerPane->model().objects())
    {
        for (const ALScriptExplorerModel::Prim& prim : one.prims)
        {
            if (prim.id != doc.ref.object)
            {
                continue;
            }
            if (!prim.fetched)
            {
                return held(doc.orphan == Doc::Orphan::Removed ? Doc::Orphan::Removed : Doc::Orphan::None);
            }
            const bool there = std::any_of(prim.items.begin(), prim.items.end(), [&doc](const ALScriptWorkspace::Item& item) { return item.id == doc.ref.item; });
            return held(there ? Doc::Orphan::None : Doc::Orphan::Removed);
        }
    }
    return held(doc.orphan == Doc::Orphan::Removed ? Doc::Orphan::Removed : Doc::Orphan::None);
}

ALFloaterScriptStudio::Doc::Orphan ALFloaterScriptStudio::failedAs(const Doc& doc, ALScriptWorkspace::Loaded::Failure failure) const
{
    using Failure = ALScriptWorkspace::Loaded::Failure;
    switch (failure)
    {
        case Failure::NotPermitted:
            return Doc::Orphan::Locked;
        case Failure::Unreadable:
        case Failure::Fetch:
            return Doc::Orphan::Unloaded;
        default:
            break;
    }
    // Gone: from the inventory or its object, or its object out of sight.
    if (doc.ref.inInventory())
    {
        return Doc::Orphan::Removed;
    }
    LLViewerObject* object = gObjectList.findObject(doc.ref.object);
    return object && !object->isDead() ? Doc::Orphan::Removed : Doc::Orphan::Away;
}

void ALFloaterScriptStudio::reattach(Doc& doc)
{
    // What the tab holds carried over what the item has, with its history,
    // as a kept text is taken up: the item loaded under it at last, so that
    // what it is saved as is what the item is -- its language and target,
    // whether it runs, whether it may be changed, the items a notecard's
    // asset carries. Written first, so that nothing typed is only in the
    // tab while it loads.
    mRecovery.keep(doc);
    ALScriptRecoveryEntry holding = ALScriptStudioRecovery::entryOf(doc);
    doc.detached                  = false;
    doc.recovering                = holding;
    doc.carriedText               = holding.text;
    ALScriptNotecardTab::carry(doc, doc);
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

// static
ALFloaterScriptStudio* ALFloaterScriptStudio::holderOf(const ALScriptRef& ref, const std::string& file)
{
    for (LLFloater* floater : LLFloaterReg::getFloaterList("script_studio"))
    {
        ALFloaterScriptStudio* window = ALViewType::as<ALFloaterScriptStudio>(floater);
        if (window && (file.empty() ? window->indexOf(ref) : window->indexOf("disk:" + file)) != NONE)
        {
            return window;
        }
    }
    return nullptr;
}

void ALFloaterScriptStudio::checkOrphans()
{
    bool changed = false;
    for (std::unique_ptr<Doc>& each : mDocs)
    {
        Doc& doc = *each;
        // Where it is, while it is in sight, for a kept text to say later.
        if (LLViewerObject* object = doc.ref.inInventory() || !doc.file.empty() ? nullptr : gObjectList.findObject(doc.ref.object))
        {
            LLViewerObject* root = object->getRootEdit() ? object->getRootEdit() : object;
            doc.objectName       = ALScriptWorkspace::objectName(root, doc.objectName);
            if (object->getRegion())
            {
                doc.regionName = object->getRegion()->getName();
            }
        }
        // Renamed where it lives since it was opened -- in the inventory, in
        // its object: called so here too.
        if (doc.loaded && doc.file.empty() && !doc.ref.isNull())
        {
            LLViewerObject*        holder = doc.ref.inInventory() ? nullptr : gObjectList.findObject(doc.ref.object);
            const LLInventoryItem* item   = doc.ref.inInventory() ? gInventory.getItem(doc.ref.item)
                                            : holder              ? holder->getInventoryItem(doc.ref.item)
                                                                  : nullptr;
            if (item && !item->getName().empty())
            {
                renameDoc(doc, item->getName());
            }
        }
        const Doc::Orphan was       = doc.orphan;
        const Doc::Orphan now_seen  = orphanOf(doc);
        // Out of sight for a moment is not gone: an object at the edge of
        // what is in view comes and goes, and a region crossing takes it
        // away and gives it back.
        constexpr F64 AWAY_AFTER = 3.0;
        if (now_seen == Doc::Orphan::Away && was != Doc::Orphan::Away)
        {
            const F64 now = LLTimer::getTotalSeconds();
            if (doc.awaySince <= 0.0)
            {
                doc.awaySince = now;
            }
            if (now - doc.awaySince < AWAY_AFTER)
            {
                continue;
            }
        }
        else if (now_seen != Doc::Orphan::Away)
        {
            doc.awaySince = 0.0;
        }
        doc.orphan = now_seen;
        if (doc.orphan == was)
        {
            continue;
        }
        changed             = true;
        doc.noticeDismissed = false;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        const bool lost = doc.orphan == Doc::Orphan::Away || doc.orphan == Doc::Orphan::Removed;
        if (lost && doc.modifiable && doc.editor->isDirty())
        {
            // What was typed is on disk now, not only in the tab, and the
            // Output says what can be done with it.
            mRecovery.keep(doc);
            report(getString(doc.orphan == Doc::Orphan::Away ? "OrphanAwayKept" : "OrphanRemovedKept", args), true, &doc,
                   doc.file.empty() ? std::vector<std::string>{ "copy", "export" } : std::vector<std::string>{ "export" });
        }
        else if ((was == Doc::Orphan::Away || was == Doc::Orphan::Removed || was == Doc::Orphan::Offline) && doc.orphan == Doc::Orphan::None &&
                 !doc.detached)
        {
            // A detached tab is loaded now, and what the load says is said.
            report(getString("OrphanBack", args), false, &doc);
        }
    }
    // Detached tabs whose item is in reach loaded under what they hold, and
    // a fetch that failed on the way tried again -- each only once its next
    // try is due, so that a load that fails however often it is tried is
    // tried a few times, further apart each time, and then waits for a
    // person to ask. Loaded once the walk is done, since an answer can come
    // at once.
    const F64                now = LLTimer::getTotalSeconds();
    std::vector<std::string> reattaching;
    for (const std::unique_ptr<Doc>& each : mDocs)
    {
        const Doc& doc   = *each;
        const bool ready = doc.orphan == Doc::Orphan::None ||
                           (doc.orphan == Doc::Orphan::Unloaded && doc.loadFailure == ALScriptWorkspace::Loaded::Failure::Fetch);
        if (doc.detached && doc.loaded && ready && ALScriptRecoveryRetry::mayTry(doc.reattachTries) && now >= doc.nextReattach)
        {
            reattaching.push_back(doc.id);
        }
    }
    for (const std::string& id : reattaching)
    {
        if (const size_t index = indexOf(id); index != NONE)
        {
            reattach(*mDocs[index]);
        }
    }
    if (changed)
    {
        refreshNotice();
        refreshToolbar();
    }
}

void ALFloaterScriptStudio::refreshNotice()
{
    if (!mNoticePanel)
    {
        return;
    }
    // What the tab in front has to reckon with, the most pressing first,
    // and up to two things to be done about it: each an action and the
    // name of its words, whose tip is the same name with Tip after it.
    Doc*        doc = active();
    std::string text;
    std::pair<std::string, std::string> buttons[2];
    if (doc && doc->recoverable)
    {
        // Said so where the script was saved since the text was kept.
        LLStringUtil::format_map_t args;
        args["[WHEN]"]   = doc->recoverable->whenSaid();
        const bool stale = doc->recoverable->baseAsset.notNull() && doc->assetId.notNull() && doc->recoverable->baseAsset != doc->assetId;
        text             = getString(stale ? "NoticeRecoverableStale" : "NoticeRecoverable", args);
        buttons[0]       = { "restore", "NoticeRestore" };
        buttons[1]       = { "discard_left", "NoticeDiscard" };
    }
    else if (doc && !doc->noticeDismissed)
    {
        LLStringUtil::format_map_t args;
        args["[FILE]"] = doc->file;
        switch (doc->orphan)
        {
            case Doc::Orphan::Away:
                text       = getString("NoticeAway");
                buttons[0] = { "copy", "NoticeCopy" };
                buttons[1] = { "export", "NoticeExport" };
                break;
            case Doc::Orphan::Removed:
                text       = getString(doc->ref.inInventory() ? "NoticeRemovedInventory" : "NoticeRemoved");
                buttons[0] = { "copy", "NoticeCopy" };
                buttons[1] = { "export", "NoticeExport" };
                break;
            case Doc::Orphan::Offline:
                text       = getString("NoticeOffline");
                buttons[0] = { "export", "NoticeExport" };
                break;
            case Doc::Orphan::Trashed:
                text = getString("NoticeTrashed");
                break;
            case Doc::Orphan::Locked:
                text       = getString("NoticeLocked");
                buttons[0] = { "copy", "NoticeCopy" };
                buttons[1] = { "export", "NoticeExport" };
                break;
            case Doc::Orphan::Unloaded:
            {
                LLStringUtil::format_map_t why;
                why["[ERROR]"] = doc->loadError;
                text           = getString("NoticeUnloaded", why);
                buttons[0]     = { "retry_load", "NoticeTryAgain" };
                buttons[1]     = { "copy", "NoticeCopy" };
                break;
            }
            case Doc::Orphan::FileGone:
                text       = getString("NoticeFileGone", args);
                buttons[0] = { "save", "NoticeSaveAgain" };
                break;
            default:
                break;
        }
    }
    mNoticePanel->setVisible(!text.empty());
    if (text.empty())
    {
        return;
    }
    mNoticeText->setText(text);
    mNoticeText->setToolTip(text);
    // The buttons as wide as their words, from the right, the way out
    // last; the words have what is left.
    const LLFontGL* font  = LLFontGL::getFontSansSerifSmall();
    S32             right = mNoticePanel->getRect().getWidth() - 4 - 22 - 6;
    LLButton*       shown[2] = { mNoticeFirst, mNoticeSecond };
    for (S32 i = 1; i >= 0; --i)
    {
        LLButton*   button = shown[i];
        const auto& [id, label] = buttons[i];
        mNoticeActions[i]       = id;
        button->setVisible(!id.empty());
        if (id.empty())
        {
            continue;
        }
        const std::string said  = getString(label);
        const S32         width = font->getWidth(said) + 24;
        button->setLabel(said);
        button->setToolTip(getString(label + "Tip"));
        const LLRect was = button->getRect();
        button->setShape(LLRect(right - width, was.mTop, right, was.mBottom));
        right -= width + 4;
    }
    const LLRect words = mNoticeText->getRect();
    mNoticeText->setShape(LLRect(words.mLeft, words.mTop, llmax(words.mLeft + 40, right - 6), words.mBottom));
}

void ALFloaterScriptStudio::onNoticeAction(const std::string& action)
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc->name;
    if (action == "close")
    {
        // Hidden until there is something else to say; a kept text from an
        // earlier session stays offered, under File > Recover Unsaved
        // Changes, once the notice is gone.
        doc->noticeDismissed = true;
        doc->recoverable.reset();
    }
    else if (action == "restore" && doc->recoverable)
    {
        const ALScriptRecoveryEntry entry = *doc->recoverable;
        doc->recoverable.reset();
        mRecovery.takeUp(*doc, entry);
        // A tab left holding it on its own has said why instead.
        if (doc->orphan == Doc::Orphan::None)
        {
            report(getString("RecoveryRestored", args), false, doc);
        }
    }
    else if (action == "discard_left" && doc->recoverable)
    {
        if (ALScriptRecoveryStore* store = ALScriptStudioRecovery::store())
        {
            store->discard(*doc->recoverable);
        }
        doc->recoverable.reset();
        report(getString("RecoveryDiscarded", args), false, doc);
    }
    else if (action == "retry_load" && doc->detached && doc->loaded)
    {
        // Asked for: tried now, and a few more times after if it fails.
        doc->reattachTries = 0;
        reattach(*doc);
    }
    else if (action == "copy")
    {
        saveCopyToInventory(*doc);
    }
    else if (action == "export")
    {
        saveToFile();
    }
    else if (action == "save")
    {
        mSaving.saveAsked(*doc);
    }
    refreshNotice();
}

void ALFloaterScriptStudio::becomeOrphan(Doc& doc, const ALScriptRecoveryEntry& entry, Doc::Orphan orphan)
{
    // Nothing loaded under it: the kept text is the tab's, unsaved, with
    // what its script was -- the language, the target, the envelope -- and
    // a notecard's items.
    doc.loaded                 = true;
    doc.modifiable             = true;
    doc.detached               = doc.file.empty();
    doc.notecard               = entry.notecard;
    doc.language.lua           = entry.lua;
    doc.language.compileTarget = !entry.compileTarget.empty() ? entry.compileTarget : entry.lua ? "luau" : "mono";
    doc.assetId                = entry.baseAsset;
    doc.objectName             = entry.objectName;
    doc.regionName             = entry.region;
    doc.orphan                 = orphan;
    doc.noticeDismissed        = false;
    if (entry.wrapped && !entry.notecard)
    {
        if (!doc.envelope)
        {
            doc.envelope = ALScriptEnvelope();
        }
    }
    else
    {
        doc.envelope.reset();
    }
    doc.editor->setPlaceholder(LLStringUtil::null);
    doc.editor->setSyntax(entry.notecard ? (doc.file.empty() ? std::string("text") : textSyntaxOf(doc.file)) : entry.lua ? "slua" : "lsl");
    if (!entry.notecard)
    {
        teachEditor(doc);
    }
    if (entry.notecard && doc.file.empty())
    {
        // Nothing the asset carries is in reach: a list of its own.
        ALScriptNotecardTab& items = notecardItems(doc, true);
        items.take(ALScriptNotecardTab::fromLLSD(entry.embedded));
        items.wire();
    }
    doc.editor->setText(entry.text);
    // Its history, where it has one and it fits the text; nothing it
    // reaches was saved anywhere this tab can reach.
    if (entry.history.isMap())
    {
        doc.editor->undoJournal().fromLLSD(entry.history);
    }
    if (entry.notecard && doc.file.empty())
    {
        doc.items->place();
    }
    doc.editor->setReadOnly(false);
    doc.editor->markUnsaved();
    if (entry.caretLine >= 0)
    {
        doc.editor->goTo(doc.editor->document().clamp(ALTextPos(entry.caretLine, entry.caretColumn)));
    }
    // Kept by this session from here, and the entry it came from let go of
    // once this tab's own is written -- named only now, so that nothing
    // the text going in set off could let it go first.
    doc.recovering = entry;
    mRecovery.keep(doc);
    scheduleAnalysis(doc, true);
    fillTabs();
    refreshToolbar();
    refreshNotice();
}

void ALFloaterScriptStudio::openOrphan(const ALScriptRecoveryEntry& entry, Doc::Orphan orphan)
{
    auto doc         = std::make_unique<Doc>();
    doc->file        = entry.file;
    doc->ref         = entry.file.empty() ? ALScriptRef(entry.object, entry.item) : ALScriptRef();
    doc->id          = entry.file.empty() ? doc->ref.id() : "disk:" + entry.file;
    doc->name        = !entry.name.empty() ? entry.name : !entry.file.empty() ? gDirUtilp->getBaseFileName(entry.file) : getString("Untitled");
    doc->recoveryKey = entry.key;
    doc->editor      = makeEditor(doc->id, false);
    wireDoc(*doc);
    mDocs.push_back(std::move(doc));
    reindexDocs();
    const size_t index = mDocs.size() - 1;
    becomeOrphan(*mDocs[index], entry, orphan);
    activate(index);
}

void ALFloaterScriptStudio::saveCopyToInventory(Doc& doc)
{
    if (!doc.loaded || !doc.file.empty())
    {
        return;
    }
    // A new item of the same kind with what the tab holds -- a notecard's
    // items with it, numbered as a save numbers them -- opened, saved at
    // once, and this tab closed once it is.
    const bool                              notecard = doc.notecard;
    const bool                              lua      = doc.language.lua;
    std::string                             text;
    std::vector<LLPointer<LLInventoryItem>> items;
    if (doc.items)
    {
        doc.items->forSave(text, items);
    }
    else
    {
        text = doc.editor->text();
    }
    const std::string         id      = doc.id;
    const U32                 version = doc.editor->document().version();
    const std::string         name    = doc.name;
    const std::string         target  = doc.language.compileTarget;
    const bool                wrapped = doc.envelope.has_value();
    const LLHandle<LLFloater> handle  = getHandle();
    // The account's default permissions for a new item of the kind, as
    // every other way of making one gives them.
    LLPointer<LLBoostFuncInventoryCallback> made = new LLBoostFuncInventoryCallback(notecard ? create_notecard_cb : create_script_cb);
    made->addOnFireFunc([handle, id, version, text, items, target, wrapped, notecard](const LLUUID& item_id) {
        ALFloaterScriptStudio*       studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const LLViewerInventoryItem* item   = item_id.notNull() ? gInventory.getItem(item_id) : nullptr;
        if (!studio || !item)
        {
            return;
        }
        const ALScriptRef ref(LLUUID::null, item_id);
        studio->openScript(ref, item->getName(), text);
        const size_t index = studio->indexOf(ref);
        if (index == NONE)
        {
            return;
        }
        Doc& copy                  = *studio->mDocs[index];
        copy.saveOnLoad            = true;
        copy.wrapOnLoad            = wrapped;
        copy.copyOf                = id;
        copy.copyOfVersion         = version;
        copy.targetOnLoad          = target;
        if (notecard)
        {
            copy.carriedEmbedded = items;
        }
    });
    std::string desc;
    LLViewerAssetType::generateDescriptionFor(notecard ? LLAssetType::AT_NOTECARD : LLAssetType::AT_LSL_TEXT, desc);
    create_inventory_item(gAgent.getID(), gAgent.getSessionID(),
                          gInventory.findCategoryUUIDForType(notecard ? LLFolderType::FT_NOTECARD : LLFolderType::FT_LSL_TEXT), LLTransactionID::tnull, name, desc,
                          notecard ? LLAssetType::AT_NOTECARD : LLAssetType::AT_LSL_TEXT, notecard ? LLInventoryType::IT_NOTECARD : LLInventoryType::IT_LSL,
                          notecard ? NO_INV_SUBTYPE : lua ? SST_LUA : SST_LSL, LLFloaterPerms::getNextOwnerPerms(notecard ? "Notecards" : "Scripts"), made);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = name;
    report(getString("CopyingTo", args), false, &doc);
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
    options.lua           = doc.language.lua;
    options.indent        = llclamp(gSavedSettings.getS32("ALScriptStudioTabWidth"), 1, 16);
    options.tabs          = !gSavedSettings.getBOOL("ALScriptStudioInsertSpaces");
    options.maxBlankLines = llclamp(gSavedSettings.getS32("ALScriptFormatBlankLines"), 0, 10);
    options.spacing       = gSavedSettings.getBOOL("ALScriptFormatSpacing");
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
        // Said, rather than the command doing nothing where it was asked.
        LL_WARNS("ScriptStudio") << "The formatter changed the line count: " << lines.size() << " for " << count << LL_ENDL;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString("FormatFailed", args), true);
        return;
    }
    std::vector<std::pair<ALTextRange, std::string>> edits;
    std::vector<bool>                                 gone(static_cast<size_t>(count), false);
    // A line that begins inside a string -- a long string, an LSL string
    // with a break written into it -- is the string's to the letter, and
    // so are the blanks before a break inside one.
    const std::vector<bool> in_string      = ALScriptFormatter::breaksInStrings(text, options.lua);
    const auto              starts_in_text = [&in_string](S32 i) { return i > 0 && static_cast<size_t>(i - 1) < in_string.size() && in_string[static_cast<size_t>(i - 1)]; };
    const auto              ends_in_text   = [&in_string](S32 i) { return static_cast<size_t>(i) < in_string.size() && in_string[static_cast<size_t>(i)]; };
    if (!selection_only)
    {
        // Runs of blank lines beyond a few, and every blank line at the
        // end, taken out.
        auto blank = [&lines](S32 i) { return lines[static_cast<size_t>(i)].find_first_not_of(" \t") == std::string::npos; };
        S32  run   = 0;
        for (S32 i = 0; i < count; ++i)
        {
            run = blank(i) && !starts_in_text(i) ? run + 1 : 0;
            if (run > options.maxBlankLines && i + 1 < count)
            {
                gone[static_cast<size_t>(i)] = true;
            }
        }
        for (S32 i = count - 1; i > 0 && blank(i) && !starts_in_text(i); --i)
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
        if (starts_in_text(i))
        {
            continue;
        }
        if (gone[static_cast<size_t>(i)])
        {
            edits.emplace_back(ALTextRange(ALTextPos(i, 0), ALTextPos(i + 1, 0)), std::string());
            continue;
        }
        std::string now = lines[static_cast<size_t>(i)];
        if (ends_in_text(i))
        {
            // Its indentation may change; the blanks it ends in may not.
            const size_t kept_now = now.find_last_not_of(" \t");
            const size_t kept_was = was.find_last_not_of(" \t");
            now = now.substr(0, kept_now == std::string::npos ? 0 : kept_now + 1) + was.substr(kept_was == std::string::npos ? 0 : kept_was + 1);
        }
        if (was != now)
        {
            edits.emplace_back(ALTextRange(ALTextPos(i, 0), ALTextPos(i, static_cast<S32>(was.size()))), now);
        }
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (edits.empty() || !doc.editor->replaceAll(std::move(edits)))
    {
        setStatus(getString("FormattedAlready", args));
        return;
    }
    doc.editor->undoJournal().label("format");
    setStatus(getString(selection_only ? "FormattedSelection" : "Formatted", args));
}

void ALFloaterScriptStudio::trimTrailing(Doc& doc)
{
    if (!doc.loaded || !doc.modifiable || doc.notecard)
    {
        return;
    }
    // Every line's blanks at its end, the one the caret is on too, as
    // one step to undo; but for those before a break inside a string,
    // which are the string's.
    const ALTextDocument&                            document  = doc.editor->document();
    const std::vector<bool>                          in_string = ALScriptFormatter::breaksInStrings(document.text(), doc.language.lua);
    std::vector<std::pair<ALTextRange, std::string>> edits;
    for (S32 line = 0; line < document.lineCount(); ++line)
    {
        if (static_cast<size_t>(line) < in_string.size() && in_string[static_cast<size_t>(line)])
        {
            continue;
        }
        const std::string& text = document.line(line);
        const size_t       kept = text.find_last_not_of(" \t");
        const size_t       end  = kept == std::string::npos ? 0 : kept + 1;
        if (end < text.size())
        {
            edits.emplace_back(ALTextRange(ALTextPos(line, static_cast<S32>(end)), ALTextPos(line, static_cast<S32>(text.size()))), std::string());
        }
    }
    if (!edits.empty())
    {
        doc.editor->replaceAll(std::move(edits));
    }
}

// --- what scripts say ---------------------------------------------------------------

void ALFloaterScriptStudio::runtimeEvent(const ALScriptWorkspace::RuntimeEvent& event)
{
    const ALScriptOutputPane::Place at = mOutputPane->heard(event);

    // A run-time error in a script that is open marks its line: said
    // again, as a script failing in a timer says it every tick, it is
    // the same problem, counted.
    const size_t open = event.item.notNull() ? indexOf(ALScriptRef(event.prim, event.item)) : NONE;
    if (event.isError && open != NONE)
    {
        Doc&                doc = *mDocs[open];
        Doc::RuntimeProblem problem;
        problem.line    = at.line;
        problem.column  = at.column;
        problem.file    = at.file;
        problem.message = event.error.empty() ? oneLine(event.message) : event.error;
        const auto same = std::find_if(doc.runtime.begin(), doc.runtime.end(), [&problem](const Doc::RuntimeProblem& one) {
            return one.line == problem.line && one.column == problem.column && one.file == problem.file && one.message == problem.message;
        });
        if (same != doc.runtime.end())
        {
            ++same->count;
        }
        else
        {
            doc.runtime.push_back(std::move(problem));
            // A script failing many ways at once is failing: the oldest go
            // past a few dozen.
            constexpr size_t RUNTIME_PROBLEMS = 50;
            if (doc.runtime.size() > RUNTIME_PROBLEMS)
            {
                doc.runtime.erase(doc.runtime.begin());
            }
        }
        refreshProblems(doc);
    }
}

bool ALFloaterScriptStudio::outputInSight() const
{
    return !mFolds.collapsed("bottom") && mBottomTabs->getCurrentPanel() && mBottomTabs->getCurrentPanel()->getName() == "output_tab";
}

bool ALFloaterScriptStudio::ownsObject(const LLUUID& root) const
{
    const LLViewerObject* object = gObjectList.findObject(root);
    return object && object->permYouOwner();
}

void ALFloaterScriptStudio::outputAction(Doc& doc, const std::string& action)
{
    // What the words said could be done, done: a save held over what was
    // found asked again -- saved, as a second save would have been, where
    // the text is still what was held -- a failed one tried again, a copy
    // into the inventory, a file.
    activate(indexOf(doc.id));
    if (action == "save_anyway")
    {
        mSaving.saveAsked(doc);
    }
    else if (action == "retry")
    {
        mSaving.save(doc);
    }
    else if (action == "copy")
    {
        saveCopyToInventory(doc);
    }
    else if (action == "export")
    {
        saveToFile();
    }
    else if (action == "take_external" && doc.external.waiting)
    {
        // What was typed here a step back in the undo.
        mExternal.take(doc, *doc.external.waiting);
    }
    else if (action == "keep_here" && doc.external.waiting)
    {
        // The external editor's save not sent; its copy is written from
        // here at the next save.
        doc.external.waiting.reset();
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString("ExternalKept", args));
    }
}

void ALFloaterScriptStudio::outputShowDoc(Doc& doc, bool problems)
{
    activate(indexOf(doc.id));
    if (problems)
    {
        showBottom("problems_tab");
    }
}

void ALFloaterScriptStudio::outputGoTo(const ALScriptRef& ref, const std::string& name, S32 line, S32 column)
{
    noteJump();
    size_t index = indexOf(ref);
    if (index == NONE)
    {
        // The script it names, opened; the line once it has loaded.
        openScript(ref, name);
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
        ALCodeEditor& source = sourceInFront(*mDocs[index]);
        source.goTo(ALTextPos(line, llmax(0, column)));
        source.setFocus(true);
    }
}

void ALFloaterScriptStudio::outputGoToInclude(const std::string& file, const std::string& file_name, S32 line, S32 column)
{
    noteJump();
    openIncludeAt(file, file_name, line, column, 0);
}

// --- before a save --------------------------------------------------------------------

// --- what the explorer asks of the window -------------------------------------------

void ALFloaterScriptStudio::showExplorer()
{
    mFolds.setCollapsed("explorer", false);
}

void ALFloaterScriptStudio::itemRenamed(const ALScriptRef& ref, const std::string& name)
{
    if (const size_t index = indexOf(ref); index != NONE)
    {
        renameDoc(*mDocs[index], name);
    }
}

void ALFloaterScriptStudio::itemDeleted(const ALScriptRef& ref)
{
    // Its tab goes with it, whatever was typed there, in whichever window
    // holds it; a window popped out for it alone goes too.
    if (ALFloaterScriptStudio* holder = holderOf(ref, std::string()))
    {
        holder->letGoOf(holder->indexOf(ref));
        if (holder != this && !holder->mMain && holder->mDocs.empty())
        {
            holder->closeFloater();
        }
    }
}

bool ALFloaterScriptStudio::unsavedAnywhere(const ALScriptRef& ref) const
{
    const ALFloaterScriptStudio* holder = holderOf(ref, std::string());
    const size_t                 index  = holder ? holder->indexOf(ref) : NONE;
    return index != NONE && holder->mDocs[index]->editor->isDirty() && holder->mDocs[index]->modifiable;
}

void ALFloaterScriptStudio::runningState(const ALScriptWorkspace::RunningState& state)
{
    const size_t index = indexOf(state.ref);
    if (index == NONE)
    {
        return;
    }
    Doc& doc    = *mDocs[index];
    doc.running = state.running ? 1 : 0;
    // What it compiles for, as the region knows it, but for one picked
    // here and not saved yet, which is what the next save sends.
    if (!state.compileTarget.empty() && !doc.targetChosen)
    {
        doc.language.compileTarget = state.compileTarget;
    }
    if (&doc == active())
    {
        refreshToolbar();
    }
}

// --- a new script in the inventory ----------------------------------------------------

void ALFloaterScriptStudio::newInventoryScript(bool lua)
{
    // Named first, as one made in a prim is; then made in the inventory's
    // scripts folder, in the language asked for rather than whichever the
    // region would pick, and opened here with the scripter's template in
    // it once the inventory has it.
    LLSD args;
    args["KIND"]                     = getString(lua ? "NewScriptLua" : "NewScriptLSL");
    args["NAME"]                     = getString("NewScriptName");
    const LLHandle<LLFloater> handle = getHandle();
    LLNotificationsUtil::add("ScriptStudioNewItem", args, LLSD(), [handle, lua](const LLSD& notification, const LLSD& response) {
        if (!ALViewType::as<ALFloaterScriptStudio>(handle.get()) || LLNotificationsUtil::getSelectedOption(notification, response) != 0)
        {
            return;
        }
        std::string name = response["name"].asString();
        LLStringUtil::trim(name);
        if (name.empty())
        {
            return;
        }
        const std::string opening = gSavedSettings.getString(lua ? "ALScriptTemplateSLua" : "ALScriptTemplateLSL");
        LLPointer<LLBoostFuncInventoryCallback> made = new LLBoostFuncInventoryCallback(create_script_cb);
        made->addOnFireFunc([handle, opening](const LLUUID& item_id) {
            ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
            const LLViewerInventoryItem* item = item_id.notNull() ? gInventory.getItem(item_id) : nullptr;
            if (!studio || !item)
            {
                return;
            }
            studio->openScript(ALScriptRef(LLUUID::null, item_id), item->getName(),
                               opening.empty() ? std::nullopt : std::optional<std::string>(opening));
        });
        std::string desc;
        LLViewerAssetType::generateDescriptionFor(LLAssetType::AT_LSL_TEXT, desc);
        create_inventory_item(gAgent.getID(), gAgent.getSessionID(), gInventory.findCategoryUUIDForType(LLFolderType::FT_LSL_TEXT), LLTransactionID::tnull,
                              name, desc, LLAssetType::AT_LSL_TEXT, LLInventoryType::IT_LSL, lua ? SST_LUA : SST_LSL,
                              LLFloaterPerms::getNextOwnerPerms("Scripts"), made);
    });
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
        // The rows, not what is said over or under them: a heading, or how
        // many more were not listed, is a row nobody can choose.
        std::string text;
        for (LLScrollListItem* item : mListMenuFor->getAllData())
        {
            if (item->getEnabled())
            {
                text += item->getContentsCSV() + "\n";
            }
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
    if (doc.unsaved() && doc.modifiable)
    {
        LLSD args;
        args["NAME"]                     = doc.name;
        const LLHandle<LLFloater> handle = getHandle();
        LLNotificationsUtil::add("ScriptStudioSaveChanges", args, LLSD(),
                                 [handle, id = std::string(id)](const LLSD& notification, const LLSD& response) {
                                     if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                                     {
                                         studio->closeDocumentAnswered(id, LLNotificationsUtil::getSelectedOption(notification, response));
                                     }
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
            mSaving.saveToClose(id);
            break;
        case 1:  // don't save
            letGoOf(index);
            if (mClosingWindow)
            {
                continueClosing();
            }
            break;
        default:  // cancel
            stopClosing();
            break;
    }
}

void ALFloaterScriptStudio::closeMany(const std::vector<std::string>& ids)
{
    // The saved ones go now; the unsaved are one question between them,
    // or the one question the one of them asks.
    std::vector<std::string> unsaved;
    for (const std::string& id : ids)
    {
        const size_t index = indexOf(id);
        if (index == NONE)
        {
            continue;
        }
        if (mDocs[index]->unsaved() && mDocs[index]->modifiable)
        {
            unsaved.push_back(id);
        }
        else
        {
            letGoOf(index);
        }
    }
    if (unsaved.size() == 1)
    {
        closeDocument(unsaved.front());
        return;
    }
    if (unsaved.empty())
    {
        return;
    }
    LLSD args;
    args["COUNT"] = static_cast<S32>(unsaved.size());
    const LLHandle<LLFloater> handle = getHandle();
    LLNotificationsUtil::add("ScriptStudioSaveChangesMany", args, LLSD(), [handle, unsaved](const LLSD& notification, const LLSD& response) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const S32              option = LLNotificationsUtil::getSelectedOption(notification, response);
        if (!studio || (option != 0 && option != 1))
        {
            return;
        }
        for (const std::string& id : unsaved)
        {
            const size_t index = studio->indexOf(id);
            if (index == NONE)
            {
                continue;
            }
            if (option == 1)
            {
                studio->letGoOf(index);
                continue;
            }
            studio->mSaving.saveToClose(id);
        }
    });
}

void ALFloaterScriptStudio::letGoOf(size_t index, bool keep)
{
    if (index >= mDocs.size())
    {
        return;
    }
    {
        Doc& doc = *mDocs[index];
        // Unsaved text thrown away -- Don't Save, a deletion, :q! -- is set
        // aside among the discarded, where File > Recover Unsaved Changes
        // has it for a week; a saved tab's entry forgotten. A kept one is
        // left as it is: another window has it, or the next session. An
        // entry the tab took up goes with it only once what the tab holds
        // is safe -- set aside, saved, or the same as saved -- so that a
        // failed write, or a text nobody could save, loses nothing.
        ALScriptRecoveryStore* store = ALScriptStudioRecovery::store();
        if (!keep && store && !doc.recoveryKey.empty())
        {
            ALScriptRecoveryStore::Parting parting;
            parting.key      = doc.recoveryKey;
            parting.tookUp   = doc.recovering;
            parting.carrying = doc.carriedText.has_value();
            parting.settled  = doc.loaded && doc.modifiable;
            if (!parting.carrying && parting.settled && doc.editor->isDirty())
            {
                parting.unsaved = ALScriptStudioRecovery::entryOf(doc);
            }
            store->letGo(parting);
        }
        mExternal.stop(doc);
        mProblemsPane->closed(doc.id);
        if (doc.id == mFound.from)
        {
            // Its own places cannot be gone to with it closed.
            mFound = References();
            fillReferences();
        }
        // Off the editor now: it is deleted with the frame, after the Doc
        // this callback points at has gone. A connection lets go safely
        // even of a signal gone before it.
        doc.changed.disconnect();
        mEditorHost->removeChild(doc.editor);
        doc.editor->die();
        if (doc.expandedEditor)
        {
            mEditorHost->removeChild(doc.expandedEditor);
            doc.expandedEditor->die();
        }
        mDocs.erase(mDocs.begin() + index);
        reindexDocs();
    }
    if (mDocs.empty())
    {
        mActive = NONE;
        showEditors();
        fillTabs();
        refreshToolbar();
        mProblemsPane->fill(nullptr);
        mOutline->deleteAllItems();
        mBreadcrumb->setPath({});
        mBreadcrumb->setTrailer(LLStringUtil::null);
        mCrumbsShownFor.clear();
        mSymbol->setText(LLStringUtil::null);
    }
    else
    {
        activate(llmin(index, mDocs.size() - 1));
    }
    mExplorerPane->relist();
}

// --- the menu and the toolbar ---------------------------------------------------

void ALFloaterScriptStudio::addCommands()
{
    addFileCommands();
    addEditCommands();
    addInsertCommands();
    addGoCommands();
    addViewCommands();
    addBuildCommands();
    addHelpCommands();
}

void ALFloaterScriptStudio::addEditorCommand(const std::string& name, ALEditorCommand command, bool changes)
{
    // The view in front's, as every command of the text's own is: the
    // expansion being read says it cannot, where the source out of
    // sight would have done it unseen.
    mCommands.add(
        name,
        [this, command]() {
            if (Doc* doc = active())
            {
                doc->shownText()->perform(command);
            }
        },
        [this, command, changes]() {
            Doc* doc = active();
            return doc && (!changes || doc->modifiable) && doc->shownText()->canPerform(command);
        });
}

void ALFloaterScriptStudio::addFileCommands()
{
    mCommands.add("new_script", [this]() { newInventoryScript(false); });
    mCommands.add(
        "new_lua_script", [this]() { newInventoryScript(true); }, []() { return ALScriptWorkspace::luaEnabled(ALScriptRef()); });
    mCommands.add(
        "save",
        [this]() {
            if (Doc* doc = active())
            {
                mSaving.saveAsked(*doc);
            }
        },
        [this]() {
            Doc* doc = active();
            return doc && doc->loaded && doc->modifiable;
        });
    mCommands.add(
        "save_all", [this]() { mSaving.saveAll(); },
        [this]() {
            for (const std::unique_ptr<Doc>& each : mDocs)
            {
                if (each->unsaved() && each->modifiable)
                {
                    return true;
                }
            }
            return false;
        });
    mCommands.add(
        "revert",
        [this]() {
            if (Doc* doc = active())
            {
                askRevert(*doc);
            }
        },
        [this]() {
            Doc* doc = active();
            return doc && revertible(*doc);
        });
    mCommands.add("open_file", [this]() { openFileFromDisk(); });
    mCommands.add(
        "recover", [this]() { mRecovery.show(); },
        []() {
            const ALScriptRecoveryStore* store = ALScriptStudioRecovery::store();
            return store && store->hasOffers();
        });
    mCommands.add(
        "insert_file", [this]() { loadFromFile(true); },
        [this]() {
            Doc* doc = active();
            return doc && doc->loaded && doc->modifiable;
        });
    mCommands.add(
        "load_file", [this]() { loadFromFile(); },
        [this]() {
            Doc* doc = active();
            return doc && doc->loaded && doc->modifiable;
        });
    mCommands.add(
        "save_file", [this]() { saveToFile(); },
        [this]() {
            Doc* doc = active();
            return doc && doc->loaded;
        });
    mCommands.add(
        "save_as", [this]() { saveFileAs(); },
        [this]() {
            Doc* doc = active();
            return doc && doc->loaded && !doc->file.empty();
        });
    mCommands.add(
        "external_editor",
        [this]() {
            if (Doc* doc = active())
            {
                mExternal.edit(*doc);
            }
        },
        [this]() {
            Doc* doc = active();
            return doc && doc->loaded && doc->modifiable && !doc->notecard;
        });
    mCommands.add("preferences", []() { LLFloaterReg::showInstance("script_studio_prefs"); });
    mCommands.add(
        "pop_out", [this]() { popOut(); },
        [this]() {
            Doc* doc = active();
            return doc && doc->loaded;
        });
    mCommands.add(
        "close",
        [this]() {
            if (Doc* doc = active())
            {
                // By the tab's own id: a file on disk has no item to be named by.
                closeDocument(doc->id);
            }
        },
        [this]() { return active() != nullptr; });
    // The tab menu's closes, about the tab in front.
    for (const char* name : { "close_others", "close_saved", "close_all" })
    {
        mCommands.add(
            name, [this, name]() { closeTabs(name, active()); },
            [this, name]() {
                const std::string_view which(name);
                const auto             saved = [](const std::unique_ptr<Doc>& each) { return !each->unsaved(); };
                return which == "close_all" ? !mDocs.empty() : which == "close_others" ? mDocs.size() > 1 : std::any_of(mDocs.begin(), mDocs.end(), saved);
            });
    }
    // The Open Recent list's last item, made with the list.
    mCommands.addUnlisted("clear_recent", [this]() {
        mRecentFiles.clear();
        mRecentScripts.clear();
        fillRecentMenu();
        saveState();
    });
    // The tab strip's menu's.
    mCommands.addUnlisted("reveal", [this]() {
        if (Doc* doc = active(); doc && !doc->ref.inInventory())
        {
            mExplorerPane->reveal(*doc);
        }
    });
}

void ALFloaterScriptStudio::addEditCommands()
{
    for (const auto& [name, forward] : { std::pair{ "undo", false }, std::pair{ "redo", true } })
    {
        mCommands.add(
            name,
            [this, forward]() {
                // The field with the keyboard -- the search box, a filter -- where it
                // has a step to take; the script otherwise.
                LLEditMenuHandler* field = focusedEditHandler();
                if (field && (forward ? field->canRedo() : field->canUndo()))
                {
                    forward ? field->redo() : field->undo();
                }
                else
                {
                    forward ? redo() : undo();
                }
            },
            [this, forward]() {
                Doc*               doc   = active();
                LLEditMenuHandler* field = focusedEditHandler();
                return (field && (forward ? field->canRedo() : field->canUndo())) || (doc && (forward ? doc->shownText()->canRedo() : doc->shownText()->canUndo()));
            });
    }
    // Whatever has the keyboard: a list of problems is worth copying
    // as much as the text is.
    const auto handler = [this]() {
        LLEditMenuHandler* handler = focusedEditHandler();
        if (Doc* doc = active(); !handler && doc)
        {
            handler = doc->shownText();
        }
        return handler;
    };
    for (const auto& [name, act, can] : { std::tuple{ "cut", &LLEditMenuHandler::cut, &LLEditMenuHandler::canCut },
                                          std::tuple{ "copy", &LLEditMenuHandler::copy, &LLEditMenuHandler::canCopy },
                                          std::tuple{ "paste", &LLEditMenuHandler::paste, &LLEditMenuHandler::canPaste },
                                          std::tuple{ "select_all", &LLEditMenuHandler::selectAll, &LLEditMenuHandler::canSelectAll } })
    {
        mCommands.add(
            name,
            [handler, act]() {
                if (LLEditMenuHandler* h = handler())
                {
                    (h->*act)();
                }
            },
            [handler, can]() {
                LLEditMenuHandler* h = handler();
                return h && (h->*can)();
            });
    }
    // The editor's own, by the names the keymap has; those that change
    // the text only where the tab may be changed.
    addEditorCommand("toggle_comment", ALEditorCommand::ToggleComment, true);
    addEditorCommand("duplicate_line", ALEditorCommand::DuplicateLine, true);
    addEditorCommand("delete_line", ALEditorCommand::DeleteLine, true);
    addEditorCommand("move_line_up", ALEditorCommand::MoveLineUp, true);
    addEditorCommand("move_line_down", ALEditorCommand::MoveLineDown, true);
    addEditorCommand("rename", ALEditorCommand::Rename, false);
    addEditorCommand("quick_fix", ALEditorCommand::QuickFix, true);
    addEditorCommand("next_misspelling", ALEditorCommand::NextMisspelling, false);
    addEditorCommand("join_lines", ALEditorCommand::JoinLines, true);
    addEditorCommand("previous_change", ALEditorCommand::PreviousChange, false);
    addEditorCommand("next_change", ALEditorCommand::NextChange, false);
    addEditorCommand("next_function", ALEditorCommand::NextFunction, false);
    addEditorCommand("previous_function", ALEditorCommand::PreviousFunction, false);
    addEditorCommand("select_function", ALEditorCommand::SelectFunction, false);
    // The whole script's indentation made of spaces, or of tabs.
    for (const auto& [name, spaces] : { std::pair{ "indent_spaces", true }, std::pair{ "indent_tabs", false } })
    {
        mCommands.add(
            name,
            [this, spaces]() {
                if (Doc* doc = active())
                {
                    doc->editor->convertIndentation(0, doc->editor->document().lineCount() - 1, spaces);
                }
            },
            [this]() {
                Doc* doc = active();
                return doc && doc->loaded && doc->modifiable && doc->shownView() == Doc::View::Source;
            });
    }
    addEditorCommand("previous_misspelling", ALEditorCommand::PreviousMisspelling, false);
    // What changes the text, or asks the analyzers about a place in it, is
    // the source's to do: while the expansion is in front, it is read.
    for (const auto& [name, command] : { std::pair{ "complete", ALEditorCommand::Complete }, std::pair{ "signature_help", ALEditorCommand::SignatureHelp } })
    {
        mCommands.add(
            name,
            [this, command]() {
                if (Doc* doc = active())
                {
                    doc->shownText()->perform(command);
                }
            },
            [this]() {
                Doc* doc = active();
                return doc && doc->shownView() == Doc::View::Source && doc->modifiable;
            });
    }
    mCommands.add(
        "fix_all",
        [this]() {
            if (Doc* doc = active())
            {
                askFixAll(*doc, FixPick{});
            }
        },
        [this]() {
            Doc*   doc  = active();
            size_t left = 0;
            return doc && doc->loaded && doc->modifiable && (!doc->pickFixes(FixPick{}, &left).empty() || left > 0);
        });
    for (const auto& [name, selection] : { std::pair{ "format", false }, std::pair{ "format_selection", true } })
    {
        mCommands.add(
            name,
            [this, selection]() {
                if (Doc* doc = active())
                {
                    format(*doc, selection);
                }
            },
            [this, selection]() {
                Doc* doc = active();
                return doc && doc->shownView() == Doc::View::Source && doc->loaded && doc->modifiable && !doc->notecard &&
                       (!selection || !doc->editor->selection().empty());
            });
    }
    for (const auto& [name, command] : { std::pair{ "find", ALEditorCommand::Find }, std::pair{ "replace", ALEditorCommand::Replace },
                                         std::pair{ "find_next", ALEditorCommand::FindNext }, std::pair{ "find_previous", ALEditorCommand::FindPrevious } })
    {
        mCommands.add(
            name,
            [this, command]() {
                if (Doc* doc = active())
                {
                    doc->shownText()->perform(command);
                }
            },
            [this]() { return active() != nullptr; });
    }
    mCommands.add("find_in_files", [this]() { findInFiles(); });
}

void ALFloaterScriptStudio::addInsertCommands()
{
    for (const char* name : { "insert_snippet", "insert_function", "insert_event", "insert_constant" })
    {
        mCommands.add(
            name, [this, what = std::string(name).substr(7)]() { insertFromLibrary(what); },
            [this]() {
                Doc* doc = active();
                return doc && doc->shownView() == Doc::View::Source && doc->loaded && doc->modifiable && !doc->notecard;
            });
    }
}

void ALFloaterScriptStudio::addGoCommands()
{
    mCommands.add("back", [this]() { goBack(false); }, [this]() { return !mBack.empty(); });
    mCommands.add("forward", [this]() { goBack(true); }, [this]() { return !mForward.empty(); });
    mCommands.add("quick_open", [this]() { showQuickOpen(false); });
    mCommands.add(
        "go_to_line",
        [this]() {
            if (active())
            {
                goToLine();
            }
        },
        [this]() { return active() != nullptr; });
    mCommands.add(
        "go_to_symbol",
        [this]() {
            if (active())
            {
                goToSymbol();
            }
        },
        [this]() {
            Doc* doc = active();
            return doc && !doc->outline.empty();
        });
    addEditorCommand("go_to_definition", ALEditorCommand::GoToDefinition, false);
    addEditorCommand("find_references", ALEditorCommand::FindReferences, false);
    for (const auto& [name, step] : { std::pair{ "next_problem", 1 }, std::pair{ "previous_problem", -1 } })
    {
        mCommands.add(
            name,
            [this, step]() {
                if (Doc* doc = active())
                {
                    goToProblem(*doc, step);
                }
            },
            [this]() {
                Doc* doc = active();
                return doc && doc->loaded && !doc->shown.empty();
            });
    }
    mCommands.add("next_tab", [this]() { cycleTab(1); });
    // The tab in front before this one, as vim's Ctrl-^ has it.
    mCommands.add(
        "last_tab",
        [this]() {
            if (Doc* doc = mVim.alternateTab())
            {
                activate(*doc);
            }
        },
        [this]() { return mVim.alternateTab() != nullptr; });
    mCommands.add("previous_tab", [this]() { cycleTab(-1); });
    mCommands.add("all_tabs", [this]() { showAllTabs(); });
    mCommands.add("move_tab_left", [this]() { moveTab(-1); });
    mCommands.add("move_tab_right", [this]() { moveTab(1); });
    mCommands.add("focus_tabs", [this]() { mTabs->setFocus(true); });
}

void ALFloaterScriptStudio::addViewCommands()
{
    mCommands.add("command_palette", [this]() { showCommandPalette(); });
    // The editors' options, each on or off.
    for (const auto& [name, flag] : { std::pair{ "word_wrap", &mWordWrap }, std::pair{ "line_numbers", &mLineNumbers },
                                      std::pair{ "relative_numbers", &mRelativeNumbers }, std::pair{ "indent_guides", &mIndentGuides },
                                      std::pair{ "rainbow_brackets", &mRainbowBrackets }, std::pair{ "sticky_headers", &mStickyHeaders },
                                      std::pair{ "spell_check", &mSpellCheck }, std::pair{ "map_preview", &mScrollMapPreview },
                                      std::pair{ "map_left", &mScrollMapLeft } })
    {
        mCommands.add(
            name,
            [this, flag]() {
                *flag = !*flag;
                applyEditorOptions();
            },
            nullptr, [flag]() { return *flag; });
    }
    for (const auto& [name, shown] :
         { std::pair{ "blanks_none", ALCodeEditor::Whitespace::None }, std::pair{ "blanks_selection", ALCodeEditor::Whitespace::Selection },
           std::pair{ "blanks_trailing", ALCodeEditor::Whitespace::Trailing }, std::pair{ "blanks_all", ALCodeEditor::Whitespace::All } })
    {
        mCommands.add(
            name,
            [this, shown]() {
                mWhitespace = shown;
                applyEditorOptions();
            },
            nullptr, [this, shown]() { return mWhitespace == shown; });
    }
    mCommands.add(
        "vim_mode",
        [this]() {
            mVimMode = !mVimMode;
            applyEditorOptions();
            mVim.clearBanner();
            if (Doc* each = active())
            {
                refreshTrailer(*each);
            }
            saveState();
        },
        nullptr, [this]() { return mVimMode; });
    // Off at once; on with the next check, which is now.
    for (const auto& [name, flag] : { std::pair{ "semantic_colors", &mSemanticColors }, std::pair{ "inlay_parameters", &mInlayParameters },
                                      std::pair{ "inlay_types", &mInlayTypes } })
    {
        mCommands.add(
            name,
            [this, flag]() {
                *flag = !*flag;
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
            },
            nullptr, [flag]() { return *flag; });
    }
    for (const auto& [name, flag] : { std::pair{ "weight_notes", &mWeightNotes }, std::pair{ "weight_heat", &mWeightHeat } })
    {
        mCommands.add(
            name,
            [this, flag]() {
                *flag = !*flag;
                applyEditorOptions();
                for (std::unique_ptr<Doc>& each : mDocs)
                {
                    showWeightsInEditor(*each);
                }
                saveState();
            },
            nullptr, [flag]() { return *flag; });
    }
    for (const auto& [name, map] : { std::pair{ "scroll_bar", false }, std::pair{ "scroll_map", true } })
    {
        mCommands.add(
            name,
            [this, map]() {
                mScrollMap = map;
                applyEditorOptions();
            },
            nullptr, [this, map]() { return mScrollMap == map; });
    }
    // Each width on for the widths nearest it: a width saved by hand is
    // in one band or another.
    for (const auto& [name, width] : { std::pair{ "map_narrow", 60 }, std::pair{ "map_medium", 90 }, std::pair{ "map_wide", 130 } })
    {
        mCommands.add(
            name,
            [this, width]() {
                mScrollMapWidth = width;
                applyEditorOptions();
            },
            nullptr, [this, width]() { return (mScrollMapWidth <= 60 ? 60 : mScrollMapWidth >= 130 ? 130 : 90) == width; });
    }
    addEditorCommand("fold", ALEditorCommand::Fold, false);
    addEditorCommand("unfold", ALEditorCommand::Unfold, false);
    addEditorCommand("fold_all", ALEditorCommand::FoldAll, false);
    addEditorCommand("unfold_all", ALEditorCommand::UnfoldAll, false);
    for (const char* region : { "explorer", "inspector" })
    {
        mCommands.add(
            region, [this, region]() { mFolds.toggle(region); }, nullptr, [this, region]() { return !mFolds.collapsed(region); });
    }
    // The panel under the editor's tabs: shown; or the panel folded when
    // it is the tab showing.
    for (const auto& [name, tab] : { std::pair{ "problems", "problems_tab" }, std::pair{ "references", "references_tab" },
                                     std::pair{ "output", "output_tab" }, std::pair{ "search", "search_tab" }, std::pair{ "weights", "weights_tab" } })
    {
        mCommands.add(
            name,
            [this, name, tab]() {
                if (mCommands.checked(name))
                {
                    mFolds.setCollapsed("bottom", true);
                }
                else if (std::string_view(name) == "search")
                {
                    showBottom(tab);
                }
                else
                {
                    showBottom(tab, true);
                }
            },
            nullptr,
            [this, tab]() {
                const LLPanel* current = mBottomTabs->getCurrentPanel();
                return !mFolds.collapsed("bottom") && current && current->getName() == tab;
            });
    }
    mCommands.add(
        "expanded", [this]() { toggleExpanded(); },
        [this]() {
            Doc* doc = active();
            return doc && doc->expandedEditor != nullptr;
        },
        [this]() {
            const Doc* doc = active();
            return doc && doc->shownView() == Doc::View::Expanded;
        });
}

void ALFloaterScriptStudio::addBuildCommands()
{
    mCommands.add(
        "preprocess",
        [this]() {
            if (Doc* doc = active(); doc && doc->loaded && !doc->notecard)
            {
                mSaving.preprocess(*doc);
            }
        },
        [this]() {
            Doc* doc = active();
            return doc && doc->loaded && !doc->notecard && !doc->preprocessing;
        });
    // The settings each turns on and off, heard as the preferences'
    // changes are (pumpPreprocessor).
    for (const auto& [name, setting] :
         { std::pair{ "preflight", "ALScriptStudioPreflight" }, std::pair{ "preproc_enabled", "ALScriptPreprocEnabled" },
           std::pair{ "preproc_disk", "ALScriptPreprocDiskIncludes" }, std::pair{ "preproc_switch", "ALScriptPreprocSwitch" },
           std::pair{ "preproc_lazy", "ALScriptPreprocLazyLists" }, std::pair{ "preproc_compress", "ALScriptPreprocCompress" },
           std::pair{ "preproc_extensions", "ALScriptPreprocExtensions" }, std::pair{ "preproc_optimize", "ALScriptPreprocOptimizer" },
           std::pair{ "preproc_shrink", "ALScriptPreprocOptimizerShrinkNames" }, std::pair{ "preproc_addstrings", "ALScriptPreprocOptimizerAddStrings" },
           std::pair{ "preproc_inline", "ALScriptPreprocOptimizerInlining" } })
    {
        mCommands.add(
            name, [setting]() { gSavedSettings.setBOOL(setting, !gSavedSettings.getBOOL(setting)); }, nullptr,
            [setting]() { return gSavedSettings.getBOOL(setting); });
    }
    mCommands.add("preproc_folder", [this]() { chooseIncludeFolder(); });
}

void ALFloaterScriptStudio::addHelpCommands()
{
    const auto script = [this]() {
        Doc* doc = active();
        return doc && doc->loaded && !doc->notecard;
    };
    mCommands.add(
        "reference",
        [this]() {
            if (Doc* doc = active())
            {
                reference(*doc);
            }
        },
        script);
    mCommands.add("browse_reference", [this]() { browseReference(); });
    mCommands.add(
        "wiki",
        [this]() {
            Doc* doc = active();
            if (!doc)
            {
                return;
            }
            // The wiki has pages for the language's words, not the script's:
            // one of the script's own names is said to have none, rather than
            // opening a page that is not there. No word at all is the portal.
            const ALCodeEditor& shown = *doc->shownText();
            const std::string   word  = shown.document().text(shown.identifierAtCaret());
            if (!word.empty() && !vocabWord(doc->language.lua, word))
            {
                LLStringUtil::format_map_t args;
                args["[NAME]"] = word;
                setStatus(getString("NoWikiPage", args));
            }
            else
            {
                LLWeb::loadURL(helpUrl(doc->language.lua, word));
            }
        },
        script);
}

void ALFloaterScriptStudio::onCompileTarget()
{
    if (Doc* doc = active())
    {
        const std::string target = mCompileTarget->getValue().asString();
        doc->language.compileTarget = target;
        // Picked, and to stand until it is saved over whatever the region
        // says the script compiles for meanwhile. What the script is --
        // its grammar, its words -- stays what the item says.
        doc->targetChosen = true;
        // An unsaved change, as the tab says.
        fillTabs();
        refreshToolbar();
    }
}

void ALFloaterScriptStudio::askExperienceOf(Doc& doc)
{
    const LLHandle<LLFloater> handle = getHandle();
    const std::string         id     = doc.id;
    doc.experienceAsking             = true;
    ALScriptWorkspace::instance().askExperience(doc.ref, [handle, id](const std::optional<LLUUID>& experience) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(id) : NONE;
        if (index == NONE)
        {
            return;
        }
        Doc& doc             = *studio->mDocs[index];
        doc.experienceAsking = false;
        // One picked meanwhile stands, as a picked target does.
        if (experience && !doc.experienceChosen)
        {
            doc.experience      = *experience;
            doc.experienceKnown = true;
        }
        if (&doc == studio->active())
        {
            studio->refreshToolbar();
        }
    });
    ALScriptWorkspace::instance().askOwnExperiences([handle](const std::vector<LLUUID>&) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->refreshToolbar();
        }
    });
}

void ALFloaterScriptStudio::refreshExperience()
{
    // Shown for a script in an object that runs under one, or whose
    // agent has one to give it: most have none, and the strip is narrow.
    Doc*                       doc   = active();
    const bool                 task  = doc && doc->loaded && !doc->ref.inInventory() && !doc->notecard;
    const std::vector<LLUUID>& own   = ALScriptWorkspace::instance().ownExperiences();
    // And for one whose region would not say, since a save cannot go
    // until it is known or picked.
    const bool                 lost  = task && !doc->experienceKnown && !doc->experienceChosen && !doc->experienceAsking;
    const bool                 shown = task && (doc->experience.notNull() || !own.empty() || lost);
    mExperience->setVisible(shown);
    // Its profile beside it, where it has one.
    mExperienceProfile->setVisible(shown && doc->experience.notNull());
    if (!shown)
    {
        mExperienceMadeOf.clear();
        return;
    }
    // An experience by its name, once the cache has it; asked for once,
    // and the list made again as it comes.
    const auto name_of = [this](const LLUUID& id) {
        const LLSD& experience = LLExperienceCache::instance().get(id);
        if (!experience.has(LLExperienceCache::NAME))
        {
            if (mExperienceNamesAsked.insert(id).second)
            {
                const LLHandle<LLFloater> handle = getHandle();
                LLExperienceCache::instance().get(id, [handle](const LLSD&) {
                    if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                    {
                        studio->refreshExperience();
                    }
                });
            }
            return getString("ExperienceLoading");
        }
        const std::string name = experience[LLExperienceCache::NAME].asString();
        return name.empty() ? LLTrans::getString("ExperienceNameUntitled") : name;
    };
    // What can be picked: none, the ones the agent contributes to by name,
    // and the one it runs under where that is not one of them; only what
    // it has, where it may not be changed.
    const bool known = doc->experienceKnown || doc->experienceChosen;
    std::vector<std::pair<std::string, LLUUID>> offered;
    for (const LLUUID& id : own)
    {
        offered.emplace_back(name_of(id), id);
    }
    if (doc->experience.notNull() && std::find(own.begin(), own.end(), doc->experience) == own.end())
    {
        offered.emplace_back(name_of(doc->experience), doc->experience);
    }
    std::stable_sort(offered.begin(), offered.end(), [](const auto& a, const auto& b) { return LLStringUtil::compareDict(a.first, b.first) < 0; });
    std::string made = doc->id + (known ? "|known" : doc->experienceAsking ? "|asking" : "|unknown") + (doc->modifiable ? "|mod" : "") + "|" +
                       doc->experience.asString();
    for (const auto& [label, id] : offered)
    {
        made += "|" + id.asString() + "=" + label;
    }
    if (made == mExperienceMadeOf)
    {
        return;
    }
    mExperienceMadeOf = made;
    mExperience->removeall();
    if (!known)
    {
        // Until the region says, nothing to pick from: a save meanwhile
        // asks it again rather than say none. Where it would not say, what
        // it runs under may be picked -- none, or one of the agent's --
        // for the save to set, since a save that asks again may meet the
        // same.
        mExperience->add(getString(doc->experienceAsking ? "ExperienceAsking" : "ExperienceUnknown"), LLSD("unknown"));
        const bool pick = !doc->experienceAsking && doc->modifiable;
        if (pick)
        {
            mExperience->add(getString("ExperienceNone"), LLSD("none"));
            for (const auto& [label, id] : offered)
            {
                mExperience->add(label, LLSD(id.asString()));
            }
        }
        mExperience->setValue(LLSD("unknown"));
        mExperience->setEnabled(pick);
        return;
    }
    const std::string current = doc->experience.isNull() ? std::string("none") : doc->experience.asString();
    mExperience->add(getString("ExperienceNone"), LLSD("none"), ADD_BOTTOM, doc->modifiable || current == "none");
    for (const auto& [label, id] : offered)
    {
        mExperience->add(label, LLSD(id.asString()), ADD_BOTTOM, doc->modifiable || id == doc->experience);
    }
    mExperience->setValue(LLSD(current));
    mExperience->setEnabled(true);
}

void ALFloaterScriptStudio::onExperience()
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    const std::string value   = mExperience->getValue().asString();
    const bool        known   = doc->experienceKnown || doc->experienceChosen;
    const std::string current = !known ? std::string("unknown") : doc->experience.isNull() ? std::string("none") : doc->experience.asString();
    const LLUUID picked = value == "none" ? LLUUID::null : LLUUID(value);
    // Where it is not known, anything picked is a choice, none included.
    if (value == "unknown" || (known && picked == doc->experience) || !doc->modifiable)
    {
        mExperience->setValue(LLSD(current));
        return;
    }
    // Picked, and to stand until it is saved, as a compile target does:
    // the experience is set by the upload, and nothing else sets it.
    doc->experience       = picked;
    doc->experienceChosen = true;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc->name;
    args["[EXPERIENCE]"] = mExperience->getSelectedItemLabel();
    setStatus(getString(picked.isNull() ? "ExperienceClearedOnSave" : "ExperienceSetOnSave", args));
    // An unsaved change, as the tab says.
    fillTabs();
    refreshToolbar();
}

void ALFloaterScriptStudio::onRunning()
{
    if (Doc* doc = active(); doc && !doc->ref.inInventory())
    {
        if (!ALScriptWorkspace::instance().setRunning(doc->ref, mRunning->get()))
        {
            mRunning->set(!mRunning->get());
        }
        // The script's own, which its next save keeps.
        doc->running = mRunning->get() ? 1 : 0;
    }
}

void ALFloaterScriptStudio::onReset()
{
    if (Doc* doc = active(); doc && !doc->ref.inInventory())
    {
        ALScriptWorkspace::instance().reset(doc->ref);
    }
}

bool ALFloaterScriptStudio::revertible(const Doc& doc) const
{
    // Something to read again: not a tab holding a kept text on its own,
    // nor one whose item, object or file is gone or out of reach -- where
    // a revert set the text aside and left the error in its place.
    using Orphan = Doc::Orphan;
    return doc.loaded && doc.modifiable && !doc.detached && doc.orphan != Orphan::Away && doc.orphan != Orphan::Removed &&
           doc.orphan != Orphan::Offline && doc.orphan != Orphan::FileGone;
}

void ALFloaterScriptStudio::askRevert(Doc& doc)
{
    if (!revertible(doc))
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        setStatus(getString("RevertOutOfReach", args), true);
        return;
    }
    if (!doc.loaded || !doc.editor->isDirty())
    {
        // Nothing to lose: read again at once.
        revert(doc);
        return;
    }
    LLSD args;
    args["NAME"]                     = doc.name;
    const LLHandle<LLFloater> handle = getHandle();
    const std::string         id     = doc.id;
    LLNotificationsUtil::add("ScriptStudioRevert", args, LLSD(), [handle, id](const LLSD& notification, const LLSD& response) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(id) : NONE;
        if (index != NONE && LLNotificationsUtil::getSelectedOption(notification, response) == 0)
        {
            studio->revert(*studio->mDocs[index]);
        }
    });
}

void ALFloaterScriptStudio::revert(Doc& doc)
{
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    // What the revert throws away is set aside first, among the discarded,
    // for File > Recover Unsaved Changes to have for a week -- once there is
    // something to revert to: a file that cannot be read leaves the tab as
    // it was, kept against a crash as it was.
    const auto set_aside = [this, &doc]() {
        if (doc.loaded && doc.modifiable && doc.editor->isDirty())
        {
            mRecovery.setAside(doc);
        }
    };
    if (!doc.file.empty())
    {
        // A file: what is on disk now, as one step to undo, and clean --
        // the caret and the view where they were.
        std::string text;
        if (!readWholeFile(doc.file, text))
        {
            args["[FILE]"] = doc.file;
            setStatus(getString(fileTooLarge(doc.file) ? "FileTooLarge" : "IncludeGone", args), true);
            return;
        }
        set_aside();
        const ALTextPos caret  = doc.editor->caret();
        const S32       scroll = doc.editor->scrollY();
        doc.carriedText        = std::move(text);
        takeCarriedText(doc);
        doc.editor->setSelection(ALTextRange(doc.editor->document().clamp(caret), doc.editor->document().clamp(caret)));
        doc.editor->setScrollY(scroll);
        fileSettled(doc);
        report(getString("Reverted", args), false, &doc);
        return;
    }
    // From the world: loaded again, the caret put back where it was once
    // the text is in -- or, where it cannot be had, the tab as it was.
    set_aside();
    doc.reverting  = doc.modifiable;
    doc.keepCaret  = doc.editor->caret();
    doc.keepScroll = doc.editor->scrollY();
    doc.loaded     = false;
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

// Each picker answers for the tab it was asked from, by its id: the tab
// in front when the answer comes may be another -- a script handed over,
// a tab a pane opened -- or none, the one asked from closed meanwhile.

void ALFloaterScriptStudio::loadFromFile(bool insert)
{
    const Doc* doc = active();
    if (!doc)
    {
        return;
    }
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle, id = doc->id, insert](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChosenToLoad(id, files, insert);
            }
        },
        LLFilePicker::FFLOAD_SCRIPT, false);
}

void ALFloaterScriptStudio::fileChosenToLoad(const std::string& id, const std::vector<std::string>& files, bool insert)
{
    const size_t index = indexOf(id);
    if (index == NONE || files.empty())
    {
        return;
    }
    Doc& doc = *mDocs[index];
    // Over the text as loaded, where it may be changed: over one still
    // loading, the load would put its own back over it.
    if (!doc.loaded || !doc.modifiable)
    {
        return;
    }
    std::string text;
    if (!readWholeFile(files.front(), text))
    {
        LLStringUtil::format_map_t args;
        args["[FILE]"] = files.front();
        setStatus(getString(fileTooLarge(files.front()) ? "FileTooLarge" : "LoadFromFileFailed", args), true);
        return;
    }
    if (text.empty())
    {
        return;
    }
    // In place of the text, or where the caret is, as one step to undo.
    activate(index);
    if (!insert)
    {
        doc.editor->selectAll();
    }
    doc.editor->insertText(text);
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
        [handle, id = doc->id](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChosenToSave(id, files);
            }
        },
        LLFilePicker::FFSAVE_SCRIPT, doc->name);
}

void ALFloaterScriptStudio::fileChosenToSave(const std::string& id, const std::vector<std::string>& files)
{
    const size_t index = indexOf(id);
    Doc*         doc   = index == NONE ? nullptr : mDocs[index].get();
    if (!doc || files.empty())
    {
        return;
    }
    const bool                 written = ALFileWrite::whole(files.front(), doc->editor->text());
    LLStringUtil::format_map_t args;
    args["[PATH]"] = files.front();
    report(getString(written ? "SavedToFile" : "SaveToFileFailed", args), !written, doc);
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
        [handle, id = doc->id](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChosenToSaveAs(id, files);
            }
        },
        LLFilePicker::FFSAVE_SCRIPT, doc->name);
}

void ALFloaterScriptStudio::fileChosenToSaveAs(const std::string& id, const std::vector<std::string>& files)
{
    const size_t index = indexOf(id);
    Doc*         doc   = index == NONE ? nullptr : mDocs[index].get();
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
    if (!ALFileWrite::whole(path, doc->editor->text()))
    {
        report(getString("SaveToFileFailed", args), true, doc);
        return;
    }
    // The tab is the new file from here on: keyed by it, named after
    // it, watched for changes to it, its problems its own, and in the
    // language its name says.
    mProblemsPane->forget(doc->id);
    doc->external.watch.reset();
    if (ALScriptRecoveryStore* store = ALScriptStudioRecovery::store(); store && !doc->recoveryKey.empty())
    {
        store->forget(doc->recoveryKey);
    }
    doc->recoveryKey = ALScriptRecoveryStore::keyOf(LLUUID::null, LLUUID::null, path);
    doc->file = path;
    doc->name = gDirUtilp->getBaseFileName(path);
    rekeyDoc(*doc, "disk:" + path);
    if (const FileLanguage said = languageOfFile(path, false); said.said)
    {
        speakFileLanguage(*doc, said);
    }
    watchFile(*doc);
    noteRecentFile(path);
    report(getString("SavedToFile", args), false, doc);
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

void ALFloaterScriptStudio::noteRecentScript(const Doc& doc)
{
    // A script or a notecard from the world or the inventory: a file is
    // noted as a file.
    if (!doc.file.empty() || doc.ref.isNull())
    {
        return;
    }
    const size_t MOST = 10;
    mRecentScripts.erase(std::remove_if(mRecentScripts.begin(), mRecentScripts.end(), [&doc](const Recent& one) { return one.ref == doc.ref; }),
                         mRecentScripts.end());
    mRecentScripts.insert(mRecentScripts.begin(), Recent{ doc.ref, doc.name });
    if (mRecentScripts.size() > MOST)
    {
        mRecentScripts.resize(MOST);
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
    // The scripts and notecards opened lately, then the files, each under
    // a word saying which where there are both.
    const LLHandle<LLFloater> handle = getHandle();
    const auto heading = [menu](const std::string& label) {
        LLMenuItemCallGL::Params p;
        p.name  = "recent_heading_" + label;
        p.label = label;
        LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
        item->setEnabled(false);
        menu->addChild(item);
    };
    if (!mRecentScripts.empty())
    {
        if (!mRecentFiles.empty())
        {
            heading(getString("RecentScripts"));
        }
        for (const Recent& one : mRecentScripts)
        {
            LLMenuItemCallGL::Params p;
            p.name  = "recent_" + one.ref.id();
            p.label = one.name;
            LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
            const ALScriptRef ref  = one.ref;
            const std::string name = one.name;
            item->setClickCallback([handle, ref, name](LLUICtrl*, const LLSD&) {
                if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                {
                    studio->openScript(ref, name);
                }
            });
            menu->addChild(item);
        }
        if (!mRecentFiles.empty())
        {
            LLMenuItemSeparatorGL::Params sep;
            menu->addChild(LLUICtrlFactory::create<LLMenuItemSeparatorGL>(sep));
            heading(getString("RecentFiles"));
        }
    }
    if (mRecentFiles.empty() && mRecentScripts.empty())
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
            studio->mCommands.run("clear_recent");
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
    state["whitespace"]       = static_cast<S32>(mWhitespace);
    state["relative_numbers"] = mRelativeNumbers;
    state["rainbow_brackets"] = mRainbowBrackets;
    state["sticky_headers"]   = mStickyHeaders;
    state["vim_mode"]         = mVimMode;
    state["spell_check"]      = mSpellCheck;
    state["semantic_colors"]  = mSemanticColors;
    state["inlay_parameters"] = mInlayParameters;
    state["inlay_types"]      = mInlayTypes;
    state["weight_notes"]     = mWeightNotes;
    state["weight_heat"]      = mWeightHeat;
    state["scroll_map"]   = mScrollMap;
    state["map_width"]    = mScrollMapWidth;
    state["map_preview"]  = mScrollMapPreview;
    state["map_left"]     = mScrollMapLeft;
    if (mExplorerPane)
    {
        mExplorerPane->saveState(state);
    }
    LLSD recent = LLSD::emptyArray();
    for (const std::string& path : mRecentFiles)
    {
        recent.append(path);
    }
    state["recent_files"] = recent;
    LLSD scripts = LLSD::emptyArray();
    for (const Recent& one : mRecentScripts)
    {
        LLSD entry;
        entry["object"] = one.ref.object;
        entry["item"]   = one.ref.item;
        entry["name"]   = one.name;
        scripts.append(entry);
    }
    state["recent_scripts"] = scripts;
    // The tabs, to be opened again next time where they can be -- as they
    // were when the quit began, which closes them one by one as it asks
    // about them.
    state["open"] = mTabsAtQuit.isMap() ? mTabsAtQuit : openTabs();
    // And, the viewer going, the windows popped out of this one: what each
    // had open and where it was, to be opened so again.
    if (mWindowsAtQuit.size() > 0 && LLAppViewer::instance()->quitRequested())
    {
        state["windows"] = mWindowsAtQuit;
    }
    // What the panes were set to list: the problems' levels, whose, and
    // from where; what kind of output; how the search matches and where;
    // and which of the bottom tabs was in front.
    if (mProblemsPane)
    {
        mProblemsPane->saveState(state);
    }
    if (mOutputPane)
    {
        state["output_kind"] = mOutputPane->kind();
    }
    if (mSearchPane)
    {
        mSearchPane->saveState(state);
    }
    if (mBottomTabs && mBottomTabs->getCurrentPanel())
    {
        state["bottom_tab"] = mBottomTabs->getCurrentPanel()->getName();
    }
    // How each list was sorted, by a column's title, and the outline.
    for (auto [name, list] : { std::make_pair("problems", mProblemsPane ? mProblemsPane->list() : nullptr), std::make_pair("references", mReferences), std::make_pair("search", mSearchPane ? mSearchPane->list() : nullptr) })
    {
        if (list && !list->getSortColumnName().empty())
        {
            state["sort"][name]["column"] = list->getSortColumnName();
            state["sort"][name]["up"]     = list->getSortAscending();
        }
    }
    if (mOutlineSort)
    {
        state["outline_sort"] = mOutlineSort->getValue().asString();
    }
}

LLSD ALFloaterScriptStudio::openTabs() const
{
    LLSD tabs = LLSD::emptyArray();
    S32  chosen = -1;
    for (size_t i = 0; i < mDocs.size(); ++i)
    {
        const Doc& doc = *mDocs[i];
        LLSD       tab;
        if (!doc.file.empty())
        {
            tab["file"] = doc.file;
            tab["lua"]  = doc.language.lua;
        }
        else if (!doc.ref.isNull())
        {
            tab["object"] = doc.ref.object;
            tab["item"]   = doc.ref.item;
        }
        else
        {
            continue;
        }
        // The view it asked for, where that is not the source: a tab
        // reading its expansion reads it again when it comes back.
        if (doc.view != Doc::View::Source)
        {
            tab["view"] = viewName(doc.view);
        }
        if (i == mActive)
        {
            chosen = static_cast<S32>(tabs.size());
        }
        tabs.append(tab);
    }
    LLSD open;
    open["tabs"]   = tabs;
    open["active"] = chosen;
    return open;
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
    if (state.has("whitespace"))
    {
        mWhitespace = static_cast<ALCodeEditor::Whitespace>(
            llclamp(state["whitespace"].asInteger(), static_cast<S32>(ALCodeEditor::Whitespace::None), static_cast<S32>(ALCodeEditor::Whitespace::All)));
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
    if (state.has("weight_notes"))
    {
        mWeightNotes = state["weight_notes"].asBoolean();
    }
    if (state.has("weight_heat"))
    {
        mWeightHeat = state["weight_heat"].asBoolean();
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
    if (mExplorerPane)
    {
        mExplorerPane->readState(state);
    }
    if (mProblemsPane)
    {
        mProblemsPane->readState(state);
    }
    if (state.has("output_kind") && mOutputPane)
    {
        mOutputPane->showKind(state["output_kind"].asString());
    }
    if (state.has("search") && mSearchPane)
    {
        mSearchPane->readState(state);
    }
    if (state.has("bottom_tab") && mBottomTabs)
    {
        mBottomTabs->selectTabByName(state["bottom_tab"].asString());
    }
    for (auto [name, list] : { std::make_pair("problems", mProblemsPane ? mProblemsPane->list() : nullptr), std::make_pair("references", mReferences), std::make_pair("search", mSearchPane ? mSearchPane->list() : nullptr) })
    {
        if (list && state["sort"].has(name))
        {
            list->sortByColumn(state["sort"][name]["column"].asString(), state["sort"][name]["up"].asBoolean());
        }
    }
    if (state.has("outline_sort") && mOutlineSort)
    {
        mOutlineSort->selectByValue(state["outline_sort"]);
    }
    if (state.has("recent_scripts"))
    {
        mRecentScripts.clear();
        for (LLSD::array_const_iterator it = state["recent_scripts"].beginArray(); it != state["recent_scripts"].endArray(); ++it)
        {
            const ALScriptRef ref((*it)["object"].asUUID(), (*it)["item"].asUUID());
            if (!ref.isNull())
            {
                mRecentScripts.push_back(Recent{ ref, (*it)["name"].asString() });
            }
        }
    }
    if (state.has("open"))
    {
        mRestoreTabs = state["open"];
    }
    if (state.has("windows"))
    {
        mRestoreWindows = state["windows"];
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
