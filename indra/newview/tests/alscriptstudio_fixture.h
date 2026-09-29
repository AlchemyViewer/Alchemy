/**
 * @file alscriptstudio_fixture.h
 * @brief What a test of a unit split out of Script Studio's window stands on.
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

#ifndef AL_ALSCRIPTSTUDIO_FIXTURE_H
#define AL_ALSCRIPTSTUDIO_FIXTURE_H

#include "linden_common.h"

#include "../alrecovery.h"
#include "../alscriptexternaleditor.h"
#include "../alscriptlookup.h"
#include "../alscriptnavigation.h"
#include "../alscriptstudioanalysis.h"
#include "../alscriptstudiofiles.h"
#include "../alscriptstudiodoc.h"
#include "../alscriptstudiorecovery.h"
#include "../alscriptstudiosaves.h"
#include "../alscriptstudioservices.h"
#include "../alscriptstudiotabs.h"
#include "../alscriptstudioweighing.h"

#include "alcodeeditor.h"
#include "aldockpanel.h"
#include "aljumpbar.h"
#include "aloutputview.h"
#include "alpanelist.h"
#include "alscopebar.h"
#include "altabstrip.h"
#include "fsyspath.h"
#include "llfloater.h"
#include "llpanel.h"
#include "lluictrlfactory.h"
#include "llxmlnode.h"

#include "../../llui/tests/alheadlessui_fixture.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it. One test binary
// includes this once.
class LLAvatarName;
const std::string gStudioTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gStudioTestAnonName;
}

namespace al_studio_test
{
    // The window's services, faked: the tabs a test makes, the window's
    // words where it has them, and a record of everything a unit said or
    // asked to go to.
    class FakeServices : public ALScriptStudioServices
    {
    public:
        struct Said
        {
            std::string              text;
            bool                     failure = false;
            // The tab it was said of, by id; empty for none.
            std::string              doc;
            std::vector<std::string> actions;
        };
        struct Opened
        {
            ALScriptRef                ref;
            std::string                name;
            std::optional<std::string> carried;
            S32                        line  = -1;
            bool                       focus = true;
        };
        struct Went
        {
            ALScriptRef ref;
            std::string name;
            S32         line   = 0;
            S32         column = 0;
            S32         length = 0;
        };

        // The window whose words `words` and `counted` say, where a test
        // has one; else a word says its own name and blanks.
        explicit FakeServices(const LLPanel* strings = nullptr) : mStrings(strings) {}

        // A tab, the first made in front.
        ALScriptStudioDoc& addDoc(const std::string& id, const ALScriptRef& ref = ALScriptRef(), const std::string& name = std::string())
        {
            docs.push_back(std::make_unique<ALScriptStudioDoc>());
            ALScriptStudioDoc& doc = *docs.back();
            doc.id                 = id;
            doc.ref                = ref;
            doc.name               = name.empty() ? id : name;
            if (front < 0)
            {
                front = 0;
            }
            return doc;
        }

        void report(const std::string& text, bool failure = false, const ALScriptStudioDoc* doc = nullptr,
                    const std::vector<std::string>& actions = {}) override
        {
            reports.push_back({ text, failure, doc ? doc->id : std::string(), actions });
        }
        void setStatus(const std::string& text, bool failure = false) override
        {
            statuses.push_back(text);
            statusFailures.push_back(failure);
        }
        std::string words(const std::string& name, const LLStringUtil::format_map_t& args = LLStringUtil::format_map_t()) const override
        {
            ++wordsSaid;
            if (mStrings && mStrings->hasString(name))
            {
                return mStrings->getString(name, args);
            }
            std::string out = name;
            for (const auto& [blank, word] : args)
            {
                out += " " + blank() + "=" + word();
            }
            return out;
        }
        // English's forms: A for one, B for many.
        std::string counted(const char* name, S32 count, LLStringUtil::format_map_t args = LLStringUtil::format_map_t()) const override
        {
            args["[COUNT]"]          = std::to_string(count);
            const std::string formed = std::string(name) + (count == 1 ? "A" : "B");
            return words(mStrings && mStrings->hasString(formed) ? formed : std::string(name), args);
        }
        ALScriptStudioDoc* frontDoc() override { return front >= 0 && front < static_cast<S32>(docs.size()) ? docs[front].get() : nullptr; }
        ALScriptStudioDoc* findDoc(std::string_view id) override
        {
            for (const std::unique_ptr<ALScriptStudioDoc>& doc : docs)
            {
                if (doc->id == id)
                {
                    return doc.get();
                }
            }
            return nullptr;
        }
        ALScriptStudioDoc* findDoc(const ALScriptRef& ref) override
        {
            for (const std::unique_ptr<ALScriptStudioDoc>& doc : docs)
            {
                if (doc->ref == ref && doc->file.empty())
                {
                    return doc.get();
                }
            }
            return nullptr;
        }
        std::vector<ALScriptStudioDoc*> openDocs() override
        {
            std::vector<ALScriptStudioDoc*> out;
            for (const std::unique_ptr<ALScriptStudioDoc>& doc : docs)
            {
                out.push_back(doc.get());
            }
            return out;
        }
        void openScript(const ALScriptRef& ref, const std::string& name, std::optional<std::string> carried = std::nullopt, S32 line = -1,
                        bool focus = true) override
        {
            opened.push_back({ ref, name, std::move(carried), line, focus });
            if (whenOpened)
            {
                whenOpened(ref, name);
            }
        }
        void goToPlace(const ALScriptRef& ref, const std::string& name, S32 line, S32 column, S32 length) override
        {
            went.push_back({ ref, name, line, column, length });
        }
        void revealed(LLUICtrl*, bool to_editor) override { reveals.push_back(to_editor); }

        std::vector<std::unique_ptr<ALScriptStudioDoc>> docs;
        S32                                             front = -1;
        std::vector<Said>                               reports;
        std::vector<std::string>                        statuses;
        // Whether each status said was a failure, in step with them.
        std::vector<bool>                               statusFailures;
        // How many words were asked for, counted and all: what a unit
        // that keeps its words says again.
        mutable S32                                     wordsSaid = 0;
        std::vector<Opened>                             opened;
        std::vector<Went>                               went;
        std::vector<bool>                               reveals;
        // What a test does as a script is asked to open: a tab for it, say.
        std::function<void(const ALScriptRef&, const std::string&)> whenOpened;

    private:
        const LLPanel* mStrings = nullptr;
    };
    // The window's tabs with nothing opened or put back, asked of and
    // saying nothing: what a unit's test's fake of its window starts from,
    // overriding what the test watches.
    struct QuietTabs : public ALScriptStudioTabs
    {
        ALScriptStudioDoc* openFileTab(const std::string&, bool) override { return nullptr; }
        void               activate(ALScriptStudioDoc&) override {}
        void               letGoOf(ALScriptStudioDoc&) override {}
        void               revert(ALScriptStudioDoc&) override {}
        void               takeCarriedText(ALScriptStudioDoc&) override {}
        void               fillTabs() override {}
        void               refreshToolbar() override {}
        void               refreshNotice() override {}
        void               refreshTrailer(ALScriptStudioDoc&) override {}
    };

    // The window's analysis with the analyzers never answering, no tab
    // preprocessed or a fragment, and no include's lines had: what a unit's
    // test's fake of its window starts from, overriding what the test
    // watches.
    struct QuietAnalysis : public ALScriptStudioAnalysis
    {
        void askAnalysis(ALScriptAnalysis::Request, std::function<void(const ALScriptAnalysis::Result&)>) override {}
        void askAnalyzer(ALScriptStudioDoc&, ALScriptAnalysis::Kind, const ALTextPos&) override {}
        void scheduleAnalysis(ALScriptStudioDoc&, bool) override {}
        bool preprocessed(const ALScriptStudioDoc&) const override { return false; }
        bool lslFragment(const ALScriptStudioDoc&) const override { return false; }
        std::string includeName(const ALScriptStudioDoc&, const std::string& path) const override { return path; }
        ALScriptPlaces::Lines sourceLines(const std::string&) const override { return ALScriptPlaces::Lines(); }
        void refreshProblems(ALScriptStudioDoc&) override {}
    };

    // The window's saving with nothing saved, stopped or warned of: what a
    // unit's test's fake of its window starts from, overriding what the
    // test watches.
    struct QuietSaves : public ALScriptStudioSaves
    {
        void save(ALScriptStudioDoc&) override {}
        void saveAsked(ALScriptStudioDoc&) override {}
        void saveToClose(const std::string&) override {}
        void stopped(ALScriptStudioDoc&) override {}
        void preprocess(ALScriptStudioDoc&) override {}
        void warnOverWeight(ALScriptStudioDoc&) override {}
    };

    // Navigation, for a unit given it (ALScriptNavigation): over tabs that
    // do nothing and a window that records the places Back and Forward
    // show -- the tab, " expanded" where it is the expansion, and the line
    // -- with no list walked, nothing worked from and no path open.
    struct StudioNavigation final : public ALScriptNavigation::Window
    {
        explicit StudioNavigation(ALScriptStudioServices& services) : unit(services, tabs, *this) {}
        void showPlace(ALScriptStudioDoc& doc, ALScriptStudioDoc::View view, const ALTextPos& at) override
        {
            shown.push_back(doc.id + (view == ALScriptStudioDoc::View::Expanded ? " expanded " : " ") + std::to_string(at.line));
        }
        bool pathOpen(const std::string&) const override { return false; }
        void choosePreview(ALPaneList*) override {}
        bool workedFrom(const ALScriptStudioDoc&) const override { return false; }
        void focusDoc(ALScriptStudioDoc&) override {}

        QuietTabs                tabs;
        ALScriptNavigation       unit;
        std::vector<std::string> shown;
    };

    // The files, for a unit given them (ALScriptStudioFiles): over tabs,
    // an analysis and saving that do nothing, with a folder of its own to
    // write in, taken out of the way as it goes; its window records the
    // names a copy was to be saved as and the tabs whose files settled,
    // picks nothing, asks nothing and has no recent list.
    struct StudioFiles final : public ALScriptStudioFiles::Window
    {
        explicit StudioFiles(ALScriptStudioServices& services)
        :   folder(fsyspath(std::filesystem::temp_directory_path() / fsyspath("alscriptstudio_" + LLUUID::generateNewID().asString())).string()),
            unit(services, tabs, analysis, saves, *this)
        {
            std::filesystem::create_directories(fsyspath(folder));
        }
        ~StudioFiles()
        {
            std::error_code ignored;
            std::filesystem::remove_all(fsyspath(folder), ignored);
        }
        void pickFilesToOpen(bool, std::function<void(const std::vector<std::string>&)>) override {}
        void pickFileToSave(const std::string& name, std::function<void(const std::vector<std::string>&)>) override { picked.push_back(name); }
        void askReload(const ALScriptStudioDoc&, std::function<void(bool)>) override {}
        void fileSettled(ALScriptStudioDoc& doc) override { settled.push_back(doc.id); }
        void fileWritten(const std::string&) override {}
        void reachChanged() override {}
        void becomeFile(ALScriptStudioDoc&, const std::string&) override {}
        LLMenuGL* recentMenu() override { return nullptr; }
        void      recentChanged() override {}
        // A path in the folder.
        std::string in(const std::string& name) const { return folder + "/" + name; }

        QuietTabs                tabs;
        QuietAnalysis            analysis;
        QuietSaves               saves;
        std::string              folder;
        ALScriptStudioFiles      unit;
        std::vector<std::string> picked, settled;
    };

    // The external editor, for a unit given it (ALScriptExternalEditor):
    // over tabs and saving that do nothing, files of its own, and a window
    // with no bridge, no copy held and no editor started.
    struct StudioExternal final : public ALScriptExternalEditor::Window
    {
        explicit StudioExternal(ALScriptStudioServices& services) : files(services), unit(services, tabs, saves, files.unit, *this) {}
        std::string bridgeId(const ALScriptStudioDoc&) const override { return std::string(); }
        bool        subscribe(ALScriptStudioDoc&) override { return false; }
        void        unsubscribe(const ALScriptStudioDoc&) override {}
        std::shared_ptr<ALScriptTempFiles::Claim> holdCopy(const std::string&) override { return nullptr; }
        void startEditor(ALScriptStudioDoc&, const std::string&, bool) override {}

        QuietTabs              tabs;
        QuietSaves             saves;
        StudioFiles            files;
        ALScriptExternalEditor unit;
    };

    // The lookups, for a unit given them (ALScriptLookup): over tabs and an
    // analysis that do nothing, the navigation given, and a window that
    // names no other scripts -- a lookup declared in one waits on them --
    // reads and expands none, and counts what it was asked to show or ask.
    struct StudioLookup final : public ALScriptLookup::Window, public QuietAnalysis
    {
        StudioLookup(ALScriptStudioServices& services, ALScriptNavigation& navigation) : unit(services, tabs, *this, navigation, *this) {}
        void candidates(const ALScriptStudioDoc&, std::function<void(ALScriptLookup::Candidates)>) override {}
        void loadSource(const ALScriptRef&, std::function<void(const LLUUID&, const std::optional<std::string>&)>) override {}
        void expand(ALScriptPreprocessor::Request, std::function<void(const ALPreprocessor::Result&)>) override {}
        void showFound(ALScriptStudioDoc&, const ALScriptLookup::Found&) override { ++shown; }
        void askNewName(ALScriptStudioDoc&, std::function<std::string(const std::string&)>, std::function<void(const std::string&)>,
                        std::function<void(const std::string&)>) override
        {
            ++named;
        }
        void previewRename(ALScriptStudioDoc&, const ALScriptLookup::Found&, const std::string&, const std::string&,
                           std::function<void(const std::vector<size_t>&)>) override
        {
        }

        QuietTabs      tabs;
        ALScriptLookup unit;
        S32            shown = 0;
        S32            named = 0;
    };

    // Recovery, for a unit given it (ALScriptStudioRecovery): over the tabs
    // a test gives -- its own fake, to see a kept text taken up -- or ones
    // that do nothing, and a store of its own, this session's, in a folder
    // of its own, taken out of the way as it goes; its window has no other
    // window to take a text up, no script in hand, and makes nothing an
    // orphan.
    struct StudioRecovery final : public ALScriptStudioRecovery::Window
    {
        explicit StudioRecovery(ALScriptStudioServices& services, ALScriptStudioTabs* given = nullptr)
        :   folder(fsyspath(std::filesystem::temp_directory_path() / fsyspath("alscriptstudio_" + LLUUID::generateNewID().asString())).string()),
            unit(services, given ? *given : tabs, *this)
        {
            std::filesystem::create_directories(fsyspath(folder));
            store = std::make_unique<ALRecoveryStore>(folder, "this-session");
            ALRecovery::useStore(store.get());
        }
        ~StudioRecovery()
        {
            ALRecovery::useStore(nullptr);
            store.reset();
            std::error_code ignored;
            std::filesystem::remove_all(fsyspath(folder), ignored);
        }
        bool recoverElsewhere(const ALRecoveryEntry&) override { return false; }
        bool scriptInHand(const ALScriptRef&) const override { return false; }
        void openOrphan(const ALRecoveryEntry&, ALScriptStudioDoc::Orphan) override {}
        void becomeOrphan(ALScriptStudioDoc&, const ALRecoveryEntry&, ALScriptStudioDoc::Orphan) override {}
        ALScriptStudioDoc::Orphan failedAs(const ALScriptStudioDoc&, ALScriptLoaded::Failure) const override
        {
            return ALScriptStudioDoc::Orphan::Unloaded;
        }
        void pick(std::vector<ALQuickOpen::Candidate>, const std::string&, const std::string&, std::function<void(const std::string&)>,
                  std::function<void(const std::string&)>) override
        {
        }

        // How many entries this session keeps for a key.
        size_t keptFor(const std::string& key) const
        {
            size_t count = 0;
            for (const ALRecoveryEntry& entry : store->list())
            {
                count += entry.key == key && entry.session == "this-session" && entry.state != ALRecoveryEntry::State::Discarded;
            }
            return count;
        }
        // A text of a tab's left by an earlier session, as this one's store
        // finds it.
        ALRecoveryEntry leftFor(const ALScriptStudioDoc& doc, const std::string& text)
        {
            ALRecoveryStore other(folder, "old-session");
            ALRecoveryEntry entry = ALScriptStudioRecovery::entryOf(doc);
            entry.text            = text;
            entry.history         = LLSD();
            entry.historyWritten.clear();
            other.write(entry);
            return store->leftFor(entry.key).value_or(ALRecoveryEntry());
        }

        QuietTabs                        tabs;
        std::string                      folder;
        std::unique_ptr<ALRecoveryStore> store;
        ALScriptStudioRecovery           unit;
    };

    // Weighing, for a unit given it (ALScriptStudioWeighing): over an
    // analysis and saving that answer nothing, recording each weighing
    // asked -- of the text as it stands, "text", or as a save sent it,
    // "sent" -- and a window with the Weights tab out of sight, the
    // optimizer off, and no notes nor heat.
    struct StudioWeighing final : public ALScriptStudioWeighing::Window, public QuietAnalysis, public QuietSaves
    {
        explicit StudioWeighing(ALScriptStudioServices& services) : unit(services, *this, *this, *this) {}
        void askWeights(ALScriptStudioDoc&) override { weighs.push_back("text"); }
        void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)>) override
        {
            weighs.push_back(request.weighing == ALScriptAnalysis::Request::Weighing::Sent ? "sent" : "asked");
        }
        bool                 optimizing() const override { return false; }
        std::string          programVersion() const override { return std::string(); }
        bool                 weightNotes() const override { return false; }
        bool                 weightHeat() const override { return false; }
        bool                 weightsShown() const override { return false; }
        ALScriptWeightsPane* weightsPane() override { return nullptr; }

        ALScriptStudioWeighing   unit;
        std::vector<std::string> weighs;
    };

    // A pane's window with nothing to fake: the services alone.
    struct NoPane
    {
    };

    // The studio's window for a test: a floater that is the window's
    // services, faked, and what a pane asks of the window, faked where a
    // test gives a fake of it -- a pane finds its window through the view
    // tree, as the window it is a tab of.
    template <class PaneWindow>
    class FakeStudio final : public LLFloater, public FakeServices, public PaneWindow
    {
    public:
        FakeStudio() : LLFloater(LLSD(), LLFloater::getDefaultParams()), FakeServices(this) {}
    };

    // The studio's window as the skin in the source tree has it, which is
    // where a unit's widgets and words are, built into a FakeStudio. Null
    // where there is no UI to build it with: LLUI_TEST_APP_DIR not pointing
    // at the tree.
    template <class PaneWindow = NoPane>
    class StudioWindowOf
    {
    public:
        StudioWindowOf()
        {
            // The UI first: a widget's parameters ask for its fonts.
            if (!ll_test::HeadlessUI::get().ok())
            {
                return;
            }
            // Its widgets made reachable: a static library links a widget's
            // registrar only with the object that holds its block.
            ALCodeEditor::Params editor;
            ALDockPanel::Params  dock;
            ALJumpBar::Params    jump;
            ALOutputView::Params output;
            ALPaneList::Params   pane;
            ALScopeBar::Params   scope;
            ALTabStrip::Params   tabs;
            (void)editor.name;
            (void)dock.name;
            (void)jump.name;
            (void)output.name;
            (void)pane.name;
            (void)scope.name;
            (void)tabs.name;
            LLXMLNodePtr node;
            if (!LLUICtrlFactory::getLayeredXMLNode("floater_script_studio.xml", node))
            {
                return;
            }
            LLPanel::Params sp(LLUICtrlFactory::getDefaultParams<LLPanel>());
            sp.name = "stage";
            sp.rect = LLRect(0, 1080, 1920, 0);
            mStage  = LLUICtrlFactory::create<LLPanel>(sp);
            floater = new FakeStudio<PaneWindow>();
            mStage->addChild(floater);
            if (!floater->initFloaterXML(node, mStage, "floater_script_studio.xml"))
            {
                floater = nullptr;
            }
            else
            {
                // Open, as the studio is while it is worked in: a pane that
                // fills only while it is seen fills here.
                floater->setVisible(true);
            }
        }
        ~StudioWindowOf() { delete mStage; }
        StudioWindowOf(const StudioWindowOf&)            = delete;
        StudioWindowOf& operator=(const StudioWindowOf&) = delete;

        template <class T>
        T* find(const std::string& name) const
        {
            return floater ? floater->template findChild<T>(name, true) : nullptr;
        }
        LLPanel* tab(const std::string& name) const { return find<LLPanel>(name); }

        // The fakes the window is: stand-ins where it could not be built,
        // which a test skips then anyway.
        FakeServices& services() { return floater ? static_cast<FakeServices&>(*floater) : mNoServices; }
        PaneWindow&   pane() { return floater ? static_cast<PaneWindow&>(*floater) : mNoPane; }

        FakeStudio<PaneWindow>* floater = nullptr;

    private:
        LLPanel*     mStage = nullptr;
        FakeServices mNoServices;
        PaneWindow   mNoPane;
    };
    using StudioWindow = StudioWindowOf<>;

    // A loaded tab of `services` over a big script (albigscript.h), in a
    // code editor made as the studio makes one and kept by `parent`: what
    // a test measures a unit's or a pane's work over, with an
    // ll_test::EditCount on its document to hold an operation to one
    // change.
    inline ALScriptStudioDoc& bigTab(FakeServices& services, LLView& parent, const std::string& id, int lines, bool lua = false)
    {
        ALScriptStudioDoc& doc = services.addDoc(id, ALScriptRef(), id + (lua ? ".luau" : ".lsl"));
        doc.loaded             = true;
        doc.modifiable         = true;
        doc.language.lua       = lua;
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name     = "editor_" + id;
        p.rect     = LLRect(0, 200, 400, 0);
        p.syntax   = lua ? "slua" : "lsl";
        doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
        doc.editor->setFont(LLFontGL::getFontMonospace());
        parent.addChild(doc.editor);
        doc.editor->setText(lua ? ll_test::bigSLua(lines) : ll_test::bigLSL(lines));
        return doc;
    }
}

#endif // AL_ALSCRIPTSTUDIO_FIXTURE_H
