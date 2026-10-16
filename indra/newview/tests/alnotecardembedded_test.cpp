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

#include "../alscriptworkspace.h"

#include "alnotecarditems.h"
#include "altextview.h"
#include "llfontgl.h"
#include "llpanel.h"
#include "llpermissions.h"
#include "lluictrlfactory.h"

#include "llbutton.h"
#include "llfocusmgr.h"

#include "../../llui/tests/alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

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
        void pressedAt(S32 x, S32 y) override { pressed = true; }
        bool pastDragStart(S32 x, S32 y) override { return past; }
        void dragOut(const LLInventoryItem& item, const ALScriptRef& notecard) override
        {
            dragged.push_back(item.getUUID());
            draggedFrom = notecard;
        }

        bool fromNotecard = false;
        bool settings     = true;
        bool copyable     = true;
        bool openedAll    = false;
        bool askable      = true;
        U32  frameNow     = 1;
        bool pressed      = false;
        bool past         = false;
        ALScriptRef draggedFrom;
        std::vector<LLUUID> dragged;

        std::vector<LLUUID>                           opened;
        std::vector<LLUUID>                           copies;
        std::vector<std::function<void()>>            confirms;
        std::function<void(const LLUUID&, U32)>      copyFromOpen;
        std::function<void(const std::string&)>       refused;
    };
}

namespace
{
    struct alnotecardembedded_data
    {
        FakeWorld                           world;
        std::shared_ptr<ALNotecardEmbedded> tab;
        LLPanel*                            stage = nullptr;
        ALTextView*                         view  = nullptr;
        ALScriptRef                         notecard;
        bool                                changeable = true;
        // What was said, and whether each was an error.
        std::vector<std::pair<std::string, bool>> said;

        ~alnotecardembedded_data()
        {
            gFocusMgr.setMouseCapture(nullptr);
            tab.reset();
            delete stage;
        }

        // A notecard's text in a view of its own, loaded, holding `text`,
        // carrying `items`, which its asset carries.
        ALNotecardEmbedded& make(const std::string& text, ALNotecardEmbedded::items_t items = {})
        {
            if (!ll_test::HeadlessUI::get().ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            notecard.object.generate();
            notecard.item.generate();
            LLPanel::Params sp(LLUICtrlFactory::getDefaultParams<LLPanel>());
            sp.name = "stage";
            sp.rect = LLRect(0, 400, 600, 0);
            stage   = LLUICtrlFactory::create<LLPanel>(sp);
            ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
            p.name = "editor";
            p.rect = LLRect(0, 200, 400, 0);
            view   = LLUICtrlFactory::create<ALTextView>(p);
            view->setFont(LLFontGL::getFontMonospace());
            stage->addChild(view);
            view->setText(text);
            ALNotecardEmbedded::Holder holder;
            holder.notecard   = [this]() { return notecard; };
            holder.changeable = [this]() { return changeable; };
            holder.say        = [this](const std::string& words, bool error) { said.emplace_back(words, error); };
            tab = std::make_shared<ALNotecardEmbedded>(*view, std::move(holder), world);
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
            const LLRect text   = view->textRect();
            tab->drop(text.mLeft + 1, text.mTop - 2, dropping, type, what.get(), &accept, tooltip);
            if (tip)
            {
                *tip = tooltip;
            }
            return accept;
        }
    };

    TEST_CASE_METHOD(alnotecardembedded_data, "ALNotecardEmbedded: a save sends only the items the text stands and renumbers them in order", "[ALNotecardEmbedded]")
    {
        const auto a = item("A"), b = item("B"), c = item("C");
        ALNotecardEmbedded& card = make("one " + at(2) + " two " + at(0) + "\n", { a, b, c });
        {
            INFO("a button over each item the text stands");
            CHECK(view->atomAt(ALTextPos(0, 4)) != nullptr);
            CHECK(view->atomAt(ALTextPos(0, 13)) != nullptr);
        }
        std::string                  text;
        ALNotecardEmbedded::items_t items;
        card.forSave(text, items);
        UNSCOPED_INFO("renumbered");
        CHECK(text == "one " + at(0) + " two " + at(1) + "\n");
        {
            INFO("C, then A; B left behind");
            REQUIRE(items.size() == 2);
            CHECK(items[0] == c);
            CHECK(items[1] == a);
        }
        UNSCOPED_INFO("the editor's own text as it was");
        CHECK(view->text() == "one " + at(2) + " two " + at(0) + "\n");
    }

    TEST_CASE_METHOD(alnotecardembedded_data, "ALNotecardEmbedded: items dropped together land in a row and a later drop at the pointer", "[ALNotecardEmbedded]")
    {
        const auto a = item("A"), b = item("B"), c = item("C");
        make("text\n");
        const LLRect    rect  = view->textRect();
        const ALTextPos where = view->posAtLocal(rect.mLeft + 1, rect.mTop - 2, true);
        CHECK(drop(a, true) == ACCEPT_YES_COPY_MULTI);
        CHECK(drop(b, true) == ACCEPT_YES_COPY_MULTI);
        const std::string line = view->document().line(where.line);
        UNSCOPED_INFO("A, then B after it");
        CHECK(line.substr(where.column, 8) == at(0) + at(1));
        {
            INFO("each with its button");
            REQUIRE(view->atomAt(ALTextPos(where.line, where.column)));
            REQUIRE(view->atomAt(ALTextPos(where.line, where.column + 4)));
        }
        world.frameNow = 2;
        drop(c, true);
        UNSCOPED_INFO("a new frame's drop lands where the pointer is");
        CHECK(view->document().line(where.line).substr(where.column, 4) == at(2));
        REQUIRE(tab->items().size() == size_t(3));
        view->undo();
        UNSCOPED_INFO("one undo takes the last away");
        CHECK(view->document().line(where.line).substr(where.column, 8) == at(0) + at(1));
    }

    TEST_CASE_METHOD(alnotecardembedded_data, "ALNotecardEmbedded: a drop of what a notecard may not carry is refused and says why", "[ALNotecardEmbedded]")
    {
        make("text\n");
        std::string tip;
        {
            INFO("not whole to the next owner");
            CHECK(drop(item("A", false), false, &tip) == ACCEPT_NO);
            REQUIRE_FALSE(tip.empty());
        }
        world.copyable = false;
        UNSCOPED_INFO("not the agent's to copy");
        CHECK(drop(item("B"), false, &tip) == ACCEPT_NO);
        UNSCOPED_INFO("said so");
        REQUIRE_FALSE(tip.empty());
        world.copyable = true;
        world.settings = false;
        UNSCOPED_INFO("settings where the grid has none");
        CHECK(drop(item("C"), false, &tip, DAD_SETTINGS) == ACCEPT_NO);
        world.fromNotecard = true;
        EAcceptance accept = ACCEPT_YES_COPY_MULTI;
        std::string ignored;
        UNSCOPED_INFO("out of another notecard: left to someone else");
        CHECK_FALSE(tab->drop(1, 1, false, DAD_NOTECARD, item("D").get(), &accept, ignored));
        world.fromNotecard = false;
        changeable         = false;
        UNSCOPED_INFO("a notecard that may not be changed");
        CHECK(drop(item("E"), false, &tip) == ACCEPT_NO);
        UNSCOPED_INFO("said so");
        REQUIRE_FALSE(tip.empty());
        UNSCOPED_INFO("nothing taken");
        REQUIRE(tab->items().empty());
    }

    TEST_CASE_METHOD(alnotecardembedded_data, "ALNotecardEmbedded: only the saved asset's items copy out and a late answer does nothing", "[ALNotecardEmbedded]")
    {
        const auto saved = item("Saved"), dropped = item("Dropped");
        ALNotecardEmbedded& card = make(at(0) + "\n", { saved });
        drop(dropped, true);

        UNSCOPED_INFO("a drop not saved: refused");
        CHECK_FALSE(card.copy(dropped, LLUUID::null));
        {
            INFO("said so, as an error");
            REQUIRE_FALSE(said.empty());
            CHECK(said.back().second);
            REQUIRE(said.back().first.find("Dropped") != std::string::npos);
        }
        UNSCOPED_INFO("nothing asked of the region");
        REQUIRE(world.copies.empty());
        {
            INFO("one saved: asked");
            CHECK(card.copy(saved, LLUUID::null));
            REQUIRE(world.copies.size() == 1);
            CHECK(world.copies.back() == saved->getUUID());
        }
        world.refused("no such item");
        {
            INFO("the region's no, said");
            CHECK(said.back().second);
            REQUIRE(said.back().first.find("Saved") != std::string::npos);
            REQUIRE(said.back().first.find("no such item") != std::string::npos);
        }

        // Saved with only the drop in it.
        card.saved({ dropped->getUUID() });
        UNSCOPED_INFO("the drop now");
        CHECK(card.copy(dropped, LLUUID::null));
        UNSCOPED_INFO("the old one no longer");
        CHECK_FALSE(card.copy(saved, LLUUID::null));

        // Pressed: offered, where the world does not take it all.
        card.open(dropped);
        UNSCOPED_INFO("asked whether to copy");
        REQUIRE(world.confirms.size() == 1);
        world.confirms.back()();
        UNSCOPED_INFO("and copied on yes");
        CHECK(world.copies.back() == dropped->getUUID());
        const size_t copies = world.copies.size();
        card.open(saved);
        UNSCOPED_INFO("not offered for what cannot be copied");
        REQUIRE(world.confirms.size() == 1);

        // The tab gone before the answers: nothing happens.
        card.open(dropped);
        const std::function<void()> late = world.confirms.back();
        tab.reset();
        late();
        UNSCOPED_INFO("no copy asked for a tab that has gone");
        REQUIRE(world.copies.size() == copies);
    }

    TEST_CASE_METHOD(alnotecardembedded_data, "ALNotecardEmbedded: asLLSD and fromLLSD keep each item in its place and a gap as a gap", "[ALNotecardEmbedded]")
    {
        const auto a = item("A"), b = item("B");
        const LLSD                        kept = ALNotecardEmbedded::asLLSD({ a, LLPointer<LLInventoryItem>(), b });
        const ALNotecardEmbedded::items_t back = ALNotecardEmbedded::fromLLSD(kept);
        UNSCOPED_INFO("each in its place");
        REQUIRE(back.size() == size_t(3));
        UNSCOPED_INFO("a missing one stays missing");
        REQUIRE(back[1].isNull());
        {
            INFO("the rest as they were");
            REQUIRE(back[0].notNull());
            CHECK(back[0]->getUUID() == a->getUUID());
            REQUIRE(back[2].notNull());
            CHECK(back[2]->getName() == "B");
        }
    }

    TEST_CASE_METHOD(alnotecardembedded_data, "ALNotecardEmbedded: an item button dragged far drags a saved item out but not an unsaved one", "[ALNotecardEmbedded]")
    {
        const auto saved = item("Saved"), dropped = item("Dropped");
        make(at(0) + "\n", { saved });
        drop(dropped, true);
        // Each item's button, by the item it stands for: the drop went in
        // ahead of the saved one.
        auto buttonOf = [this](S32 index) -> LLButton* {
            for (const ALTextView::Atom& atom : view->atoms())
            {
                if (atom.value.asInteger() == index)
                {
                    return dynamic_cast<LLButton*>(atom.view);
                }
            }
            return nullptr;
        };
        LLButton* button = buttonOf(0);
        UNSCOPED_INFO("a button over the saved item");
        REQUIRE(button != nullptr);
        button->handleMouseDown(2, 2, MASK_NONE);
        UNSCOPED_INFO("the press measured from");
        CHECK(world.pressed);
        button->handleHover(3, 3, MASK_NONE);
        UNSCOPED_INFO("not far enough: no drag");
        REQUIRE(world.dragged.empty());
        world.past = true;
        button->handleHover(20, 3, MASK_NONE);
        {
            INFO("far enough: dragged out");
            REQUIRE(world.dragged.size() == 1);
            CHECK(world.dragged.back() == saved->getUUID());
        }
        {
            INFO("out of this notecard");
            CHECK(world.draggedFrom.item == notecard.item);
            CHECK(world.draggedFrom.object == notecard.object);
        }
        button->handleMouseUp(20, 3, MASK_NONE);

        LLButton* unsaved = buttonOf(1);
        UNSCOPED_INFO("a button over the drop");
        REQUIRE(unsaved != nullptr);
        said.clear();
        unsaved->handleMouseDown(2, 2, MASK_NONE);
        unsaved->handleHover(20, 3, MASK_NONE);
        UNSCOPED_INFO("a drop not saved is not dragged out");
        REQUIRE(world.dragged.size() == 1);
        {
            INFO("and a save is said to be needed");
            REQUIRE_FALSE(said.empty());
            CHECK(said.back().second);
            REQUIRE(said.back().first.find("Dropped") != std::string::npos);
        }
        unsaved->handleMouseUp(20, 3, MASK_NONE);
    }

    TEST_CASE_METHOD(alnotecardembedded_data, "ALNotecardEmbedded: a line holding an item grows to fit its icon and its neighbours do not", "[ALNotecardEmbedded]")
    {
        ALNotecardEmbedded& card = make("above\nhere " + at(0) + " it is\nbelow\n", { item("A") });
        (void)card;
        const S32 text_h = view->layout().rowHeight();
        UNSCOPED_INFO("a row an inventory icon is taller than: " << text_h);
        CHECK(text_h < 16 + 4);
        view->placeAtomViews();
        UNSCOPED_INFO("the item's line: the icon, a pixel either side, and a pixel off the rows");
        CHECK(view->layout().lineHeight(1) == 16 + 4);
        UNSCOPED_INFO("the line above as it was");
        CHECK(view->layout().lineHeight(0) == text_h);
        UNSCOPED_INFO("and the line below");
        CHECK(view->layout().lineHeight(2) == text_h);
        const ALTextView::Atom* atom = view->atomAt(ALTextPos(1, 5));
        {
            INFO("the item's button");
            REQUIRE(atom);
            REQUIRE(atom->view);
        }
        UNSCOPED_INFO("as tall as its line");
        CHECK(atom->view->getRect().getHeight() == 16 + 4);
    }
}
