/**
 * @file alnotecardembedded_test.cpp
 * @brief A notecard's items in its Script Studio tab, over a fake of the viewer's side.
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

#include "linden_common.h"

#include "../alnotecardembedded.h"

#include "alnotecarditems.h"
#include "llfontgl.h"
#include "llpermissions.h"

#include "alscriptstudio_fixture.h"

#include "../test/lltut.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
    // The viewer's side, answered as a test says, and what was asked of it
    // kept.
    class FakeWorld final : public ALNotecardEmbedded::World
    {
    public:
        std::string iconOf(const LLInventoryItem&) const override { return "Inv_Notecard"; }
        bool        draggedFromNotecard() const override { return fromNotecard; }
        bool        carriesSettings() const override { return settings; }
        bool        mayCopy(const LLInventoryItem&) const override { return copyable; }
        U32         frame() const override { return frameNow; }
        bool        open(const LLPointer<LLInventoryItem>& item, const ALScriptRef&, std::function<void(const LLUUID&, U32)> copy) override
        {
            opened.push_back(item->getUUID());
            copyFromOpen = std::move(copy);
            return openedAll;
        }
        void confirmCopy(std::function<void()> yes) override { confirms.push_back(std::move(yes)); }
        bool askCopy(const ALScriptRef&, const LLUUID& item, const LLUUID&, U32, std::function<void(const std::string&)> refused) override
        {
            copies.push_back(item);
            this->refused = std::move(refused);
            return askable;
        }

        bool fromNotecard = false;
        bool settings     = true;
        bool copyable     = true;
        bool openedAll    = false;
        bool askable      = true;
        U32  frameNow     = 1;

        std::vector<LLUUID>                           opened;
        std::vector<LLUUID>                           copies;
        std::vector<std::function<void()>>            confirms;
        std::function<void(const LLUUID&, U32)>      copyFromOpen;
        std::function<void(const std::string&)>       refused;
    };
}

namespace tut
{
    struct alnotecardembedded_data
    {
        al_studio_test::StudioWindow       window;
        al_studio_test::FakeServices       services{ window.floater };
        FakeWorld                          world;
        std::shared_ptr<ALNotecardEmbedded> tab;
        ALScriptStudioDoc*                 doc = nullptr;

        // A notecard's tab over an editor of its own, loaded, holding
        // `text`, carrying `items`, which its asset carries.
        ALNotecardEmbedded& make(const std::string& text, ALNotecardEmbedded::items_t items = {})
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            LLUUID object, item;
            object.generate();
            item.generate();
            doc             = &services.addDoc("card", ALScriptRef(object, item), "Card");
            doc->notecard   = true;
            doc->loaded     = true;
            doc->modifiable = true;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name       = "editor";
            p.rect       = LLRect(0, 200, 400, 0);
            p.syntax     = "text";
            doc->editor  = LLUICtrlFactory::create<ALCodeEditor>(p);
            doc->editor->setFont(LLFontGL::getFontMonospace());
            window.floater->addChild(doc->editor);
            doc->editor->setText(text);
            tab       = std::make_shared<ALNotecardEmbedded>(*doc, services, world);
            doc->items = tab;
            tab->loaded(std::move(items));
            tab->place();
            tab->wire();
            return *tab;
        }

        static LLPointer<LLInventoryItem> item(const std::string& name, bool whole_to_next_owner = true)
        {
            LLPointer<LLInventoryItem> one = new LLInventoryItem();
            LLUUID                     id;
            id.generate();
            one->setUUID(id);
            one->rename(name);
            one->setType(LLAssetType::AT_NOTECARD);
            one->setInventoryType(LLInventoryType::IT_NOTECARD);
            LLPermissions perm;
            perm.init(LLUUID::null, LLUUID::null, LLUUID::null, LLUUID::null);
            perm.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, whole_to_next_owner ? PERM_ALL : PERM_MOVE);
            one->setPermissions(perm);
            return one;
        }

        static std::string at(size_t index) { return ALNotecardItems::charOf(index); }

        // A drop at the start of the text, the frame as the world says.
        EAcceptance drop(const LLPointer<LLInventoryItem>& what, bool dropping, std::string* tip = nullptr, EDragAndDropType type = DAD_NOTECARD)
        {
            EAcceptance accept  = ACCEPT_NO;
            std::string tooltip;
            const LLRect text   = doc->editor->textRect();
            tab->drop(text.mLeft + 1, text.mTop - 2, dropping, type, what.get(), &accept, tooltip);
            if (tip)
            {
                *tip = tooltip;
            }
            return accept;
        }
    };
    typedef test_group<alnotecardembedded_data> alnotecardembedded_group;
    typedef alnotecardembedded_group::object    alnotecardembedded_object;
    alnotecardembedded_group                    alnotecardembedded_instance("ALNotecardEmbedded");

    template<> template<>
    void alnotecardembedded_object::test<1>()
    {
        set_test_name("a save sends only the items the text still stands, numbered afresh in the order it stands them");
        const auto a = item("A"), b = item("B"), c = item("C");
        ALNotecardEmbedded& card = make("one " + at(2) + " two " + at(0) + "\n", { a, b, c });
        ensure("a button over each item the text stands", doc->editor->atomAt(ALTextPos(0, 4)) != nullptr);
        std::string                  text;
        ALNotecardEmbedded::items_t items;
        card.forSave(text, items);
        ensure_equals("renumbered", text, "one " + at(0) + " two " + at(1) + "\n");
        ensure("C, then A; B left behind", items.size() == 2 && items[0] == c && items[1] == a);
        ensure_equals("the editor's own text as it was", doc->editor->text(), "one " + at(2) + " two " + at(0) + "\n");
    }

    template<> template<>
    void alnotecardembedded_object::test<2>()
    {
        set_test_name("several items dropped together land one after another; a later drop lands at the pointer");
        const auto a = item("A"), b = item("B"), c = item("C");
        make("text\n");
        const LLRect    rect  = doc->editor->textRect();
        const ALTextPos where = doc->editor->posAtLocal(rect.mLeft + 1, rect.mTop - 2, true);
        ensure("taken", drop(a, true) == ACCEPT_YES_COPY_MULTI);
        ensure("taken", drop(b, true) == ACCEPT_YES_COPY_MULTI);
        const std::string line = doc->editor->document().line(where.line);
        ensure_equals("A, then B after it", line.substr(where.column, 8), at(0) + at(1));
        ensure("each with its button", doc->editor->atomAt(ALTextPos(where.line, where.column)) && doc->editor->atomAt(ALTextPos(where.line, where.column + 4)));
        world.frameNow = 2;
        drop(c, true);
        ensure_equals("a new frame's drop lands where the pointer is", doc->editor->document().line(where.line).substr(where.column, 4), at(2));
        ensure_equals("three carried", tab->items().size(), size_t(3));
        ensure("one undo takes the last away", doc->editor->undoJournal().canUndo());
    }

    template<> template<>
    void alnotecardembedded_object::test<3>()
    {
        set_test_name("a drop is refused, and says why, for what a notecard may not carry");
        make("text\n");
        std::string tip;
        ensure("not whole to the next owner", drop(item("A", false), false, &tip) == ACCEPT_NO && !tip.empty());
        world.copyable = false;
        ensure("not the agent's to copy", drop(item("B"), false, &tip) == ACCEPT_NO);
        ensure_equals("said so", tip, services.words("NotecardDropNoCopy"));
        world.copyable = true;
        world.settings = false;
        ensure("settings where the grid has none", drop(item("C"), false, &tip, DAD_SETTINGS) == ACCEPT_NO);
        world.fromNotecard = true;
        EAcceptance accept = ACCEPT_YES_COPY_MULTI;
        std::string ignored;
        ensure("out of another notecard: left to someone else", !tab->drop(1, 1, false, DAD_NOTECARD, item("D").get(), &accept, ignored));
        world.fromNotecard = false;
        doc->modifiable    = false;
        ensure("a notecard that may not be changed", drop(item("E"), false, &tip) == ACCEPT_NO);
        ensure_equals("said so", tip, services.words("NotecardReadOnlyDrop"));
        ensure("nothing taken", tab->items().empty());
    }

    template<> template<>
    void alnotecardembedded_object::test<4>()
    {
        set_test_name("only what the saved asset carries is copied out; a save changes what that is; an answer after the tab has gone does nothing");
        const auto saved = item("Saved"), dropped = item("Dropped");
        ALNotecardEmbedded& card = make(at(0) + "\n", { saved });
        drop(dropped, true);

        ensure("a drop not saved: refused", !card.copy(dropped, LLUUID::null));
        ensure("said so", !services.statuses.empty() && services.statuses.back() == services.words("NotecardCopyUnsaved", { { "[NAME]", "Dropped" } }));
        ensure("nothing asked of the region", world.copies.empty());
        ensure("one saved: asked", card.copy(saved, LLUUID::null) && world.copies.size() == 1 && world.copies.back() == saved->getUUID());
        world.refused("no such item");
        ensure_equals("the region's no, said", services.statuses.back(), services.words("NotecardCopyRefused", { { "[NAME]", "Saved" }, { "[ERROR]", "no such item" } }));

        // Saved with only the drop in it.
        card.saved({ dropped->getUUID() });
        ensure("the drop now", card.copy(dropped, LLUUID::null));
        ensure("the old one no longer", !card.copy(saved, LLUUID::null));

        // Pressed: offered, where the world does not take it all.
        card.open(dropped);
        ensure("asked whether to copy", world.confirms.size() == 1);
        world.confirms.back()();
        ensure("and copied on yes", world.copies.back() == dropped->getUUID());
        const size_t copies = world.copies.size();
        card.open(saved);
        ensure("not offered for what cannot be copied", world.confirms.size() == 1);

        // The tab gone before the answers: nothing happens.
        card.open(dropped);
        const std::function<void()> late = world.confirms.back();
        doc->items.reset();
        tab.reset();
        late();
        ensure("no copy asked for a tab that has gone", world.copies.size() == copies);
    }

    template<> template<>
    void alnotecardembedded_object::test<5>()
    {
        set_test_name("a tab's items carried to another, and kept and read back in their places");
        const auto a = item("A"), b = item("B");
        make(at(0) + at(1) + "\n", { a, b });
        ALScriptStudioDoc& there = services.addDoc("there");
        ALNotecardEmbedded::carry(*doc, there);
        ensure("carried", there.carriedEmbedded && there.carriedEmbedded->size() == 2 && (*there.carriedEmbedded)[1] == b);
        ALScriptStudioDoc& script = services.addDoc("script");
        ALScriptStudioDoc& after  = services.addDoc("after");
        ALNotecardEmbedded::carry(script, after);
        ensure("a script's tab carries nothing", !after.carriedEmbedded);

        const LLSD                         kept = ALNotecardEmbedded::asLLSD({ a, LLPointer<LLInventoryItem>(), b });
        const ALNotecardEmbedded::items_t back = ALNotecardEmbedded::fromLLSD(kept);
        ensure_equals("each in its place", back.size(), size_t(3));
        ensure("a missing one stays missing", back[1].isNull());
        ensure("the rest as they were", back[0]->getUUID() == a->getUUID() && back[2]->getName() == "B");
    }
}
