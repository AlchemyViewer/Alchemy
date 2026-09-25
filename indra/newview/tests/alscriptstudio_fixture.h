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

#include "../alscriptstudiodoc.h"
#include "../alscriptstudioservices.h"

#include "alcodeeditor.h"
#include "aldockpanel.h"
#include "aljumpbar.h"
#include "aloutputview.h"
#include "alpanelist.h"
#include "alscopebar.h"
#include "altabstrip.h"
#include "llfloater.h"
#include "llpanel.h"
#include "lluictrlfactory.h"
#include "llxmlnode.h"

#include "../../llui/tests/alheadlessui_fixture.h"

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
    // The studio's window as the skin in the source tree has it, which is
    // where a unit's widgets and words are. Null where there is no UI to
    // build it with: LLUI_TEST_APP_DIR not pointing at the tree.
    class StudioWindow
    {
    public:
        StudioWindow()
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
            floater = new LLFloater(LLSD(), LLFloater::getDefaultParams());
            mStage->addChild(floater);
            if (!floater->initFloaterXML(node, mStage, "floater_script_studio.xml"))
            {
                floater = nullptr;
            }
        }
        ~StudioWindow() { delete mStage; }
        StudioWindow(const StudioWindow&)            = delete;
        StudioWindow& operator=(const StudioWindow&) = delete;

        template <class T>
        T* find(const std::string& name) const
        {
            return floater ? floater->findChild<T>(name, true) : nullptr;
        }
        LLPanel* tab(const std::string& name) const { return find<LLPanel>(name); }

        LLFloater* floater = nullptr;

    private:
        LLPanel* mStage = nullptr;
    };

    // The window's services, faked: the tabs a test makes, the window's
    // words where it has them, and a record of everything a unit said or
    // asked to go to.
    class FakeServices final : public ALScriptStudioServices
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
        void setStatus(const std::string& text, bool failure = false) override { statuses.push_back(text); }
        std::string words(const std::string& name, const LLStringUtil::format_map_t& args = LLStringUtil::format_map_t()) const override
        {
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
            if (onOpen)
            {
                onOpen(ref, name);
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
        std::vector<Opened>                             opened;
        std::vector<Went>                               went;
        std::vector<bool>                               reveals;
        // What a test does as a script is asked to open: a tab for it, say.
        std::function<void(const ALScriptRef&, const std::string&)> onOpen;

    private:
        const LLPanel* mStrings = nullptr;
    };
}

#endif // AL_ALSCRIPTSTUDIO_FIXTURE_H
