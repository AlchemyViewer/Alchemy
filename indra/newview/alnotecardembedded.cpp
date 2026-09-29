/**
 * @file alnotecardembedded.cpp
 * @brief A notecard's items in its text: buttons in the text, dropped in, saved, opened and copied out.
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

#include "alnotecardembedded.h"

#include "alnotecarditems.h"
#include "alsaid.h"
#include "alscriptworkspace.h"
#include "llbutton.h"
#include "llfontgl.h"
#include "lltrans.h"
#include "lluictrlfactory.h"

namespace
{
    // An item's button, which a drag takes out of the text: pressed, it
    // says where; the mouse moved while held, it asks whether the item is
    // dragged out from there.
    class ItemButton final : public LLButton
    {
    public:
        typedef LLButton::Params Params;

        std::function<void(S32 screen_x, S32 screen_y)> pressedAt;
        std::function<bool(S32 screen_x, S32 screen_y)> draggedTo;

        bool handleMouseDown(S32 x, S32 y, MASK mask) override
        {
            const bool handled = LLButton::handleMouseDown(x, y, mask);
            mHeld              = hasMouseCapture();
            if (mHeld && pressedAt)
            {
                S32 screen_x = 0, screen_y = 0;
                localPointToScreen(x, y, &screen_x, &screen_y);
                pressedAt(screen_x, screen_y);
            }
            return handled;
        }
        bool handleHover(S32 x, S32 y, MASK mask) override
        {
            if (mHeld && hasMouseCapture() && draggedTo)
            {
                S32 screen_x = 0, screen_y = 0;
                localPointToScreen(x, y, &screen_x, &screen_y);
                if (draggedTo(screen_x, screen_y))
                {
                    mHeld = false;
                    return true;
                }
            }
            return LLButton::handleHover(x, y, mask);
        }
        bool handleMouseUp(S32 x, S32 y, MASK mask) override
        {
            mHeld = false;
            return LLButton::handleMouseUp(x, y, mask);
        }

    protected:
        friend class ::LLUICtrlFactory;
        ItemButton(const Params& p) : LLButton(p) {}

    private:
        bool mHeld = false;
    };
}

ALNotecardEmbedded::ALNotecardEmbedded(ALTextView& view, Holder holder, World& world)
    : mView(view), mHolder(std::move(holder)), mWorld(world)
{
}

void ALNotecardEmbedded::loaded(items_t items)
{
    mItems = std::move(items);
    mInAsset.clear();
    for (const LLPointer<LLInventoryItem>& each : mItems)
    {
        if (each.notNull())
        {
            mInAsset.insert(each->getUUID());
        }
    }
}

void ALNotecardEmbedded::take(items_t items)
{
    mItems = std::move(items);
}

void ALNotecardEmbedded::saved(const std::vector<LLUUID>& sent)
{
    mInAsset.clear();
    mInAsset.insert(sent.begin(), sent.end());
}

void ALNotecardEmbedded::wire()
{
    mView.setDropHandler([this](S32 x, S32 y, MASK, bool dropping, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip) {
        return drop(x, y, dropping, type, cargo, accept, tooltip);
    });
    // The view has slid its own atoms by the time the edit is heard.
    mEdits = mView.document().onChanged([this](const ALTextDocument::Edit& edit) {
        LL_PROFILE_ZONE_NAMED_CATEGORY_SCRIPTDEV("notecard items placed");
        if (edit.inserted.find('\xF4') != std::string::npos)
        {
            place(edit.range.begin.line, edit.endAfter().line);
        }
    });
}

void ALNotecardEmbedded::place(S32 first_line, S32 last_line)
{
    const ALTextDocument& text = mView.document();
    for (S32 line = llmax(0, first_line); line <= last_line && line < text.lineCount() && !mItems.empty(); ++line)
    {
        ALNotecardItems::forEach(text.line(line), [&](size_t column, size_t index) {
            const ALTextPos at(line, static_cast<S32>(column));
            if (index < mItems.size() && mItems[index].notNull() && !mView.atomAt(at))
            {
                mView.addAtom(atomFor(at, index));
            }
        });
    }
}

void ALNotecardEmbedded::place()
{
    // Each item's character (ALNotecardItems) becomes an atom over its
    // four bytes, so the text keeps it and a save carries it.
    std::vector<ALTextView::Atom> atoms;
    const ALTextDocument&         text = mView.document();
    for (S32 line = 0; line < text.lineCount() && !mItems.empty(); ++line)
    {
        ALNotecardItems::forEach(text.line(line), [&](size_t column, size_t index) {
            if (index < mItems.size() && mItems[index].notNull())
            {
                atoms.push_back(atomFor(ALTextPos(line, static_cast<S32>(column)), index));
            }
        });
    }
    mView.setAtoms(std::move(atoms));
}

void ALNotecardEmbedded::forSave(std::string& text, items_t& items) const
{
    text = mView.text();
    items.clear();
    if (mItems.empty())
    {
        return;
    }
    const std::vector<size_t> order = ALNotecardItems::renumber(text, [this](size_t index) {
        return index < mItems.size() && mItems[index].notNull();
    });
    for (const size_t index : order)
    {
        items.push_back(mItems[index]);
    }
}

bool ALNotecardEmbedded::drop(S32 x, S32 y, bool dropping, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip)
{
    // Only what is in the inventory can be verified.
    if (mWorld.draggedFromNotecard())
    {
        return false;
    }
    if (!mHolder.changeable() || mView.isReadOnly())
    {
        *accept = ACCEPT_NO;
        if (tooltip.empty())
        {
            tooltip = alSaid("NotecardReadOnlyDrop", "This notecard cannot be changed");
        }
        return true;
    }
    bool supported = false;
    switch (type)
    {
        case DAD_SETTINGS:
            supported = mWorld.carriesSettings();
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
    if (!mWorld.mayCopy(*item))
    {
        // One this agent may not copy cannot go in, whatever the next
        // owner would get.
        *accept = ACCEPT_NO;
        if (tooltip.empty())
        {
            tooltip = alSaid("NotecardDropNoCopy", "You cannot copy this item, so it cannot be added to the notecard");
        }
        return true;
    }
    *accept = ACCEPT_YES_COPY_MULTI;
    if (dropping)
    {
        // The item after the ones carried, and its character in the text
        // where the drop landed, one step to undo; the button follows the
        // edit through the document's change.
        const size_t index = mItems.size();
        if (index >= ALNotecardItems::MOST)
        {
            *accept = ACCEPT_NO;
            return true;
        }
        mItems.push_back(item);
        // Where the drop landed -- or, for the second and later of several
        // dropped together, right after the one before, so that they keep
        // their order.
        ALTextPos at = mView.posAtLocal(x, y, true);
        if (mDropFrame == mWorld.frame() && mDropEnd.line >= 0)
        {
            at = mDropEnd;
        }
        const std::string placeholder = ALNotecardItems::charOf(index);
        if (!mView.replaceAll({ { ALTextRange(at, at), placeholder } }))
        {
            // No room left in the text for it.
            mItems.pop_back();
            *accept = ACCEPT_NO;
            return true;
        }
        mDropEnd   = ALTextPos(at.line, at.column + static_cast<S32>(placeholder.size()));
        mDropFrame = mWorld.frame();
    }
    return true;
}

ALTextView::Atom ALNotecardEmbedded::atomFor(const ALTextPos& at, size_t index)
{
    const LLPointer<LLInventoryItem> item = mItems[index];
    const LLFontGL*                  font = LLFontGL::getFontSansSerifSmall();
    LLStringUtil::format_map_t       args;
    args["[NAME]"] = item->getName();
    // What a press does, by the kind: opens, plays, or takes a copy.
    std::string tip;
    switch (item->getType())
    {
        case LLAssetType::AT_TEXTURE:
        case LLAssetType::AT_MATERIAL:
        case LLAssetType::AT_CALLINGCARD:
        case LLAssetType::AT_LANDMARK: tip = alSaid("EmbeddedItemOpenTip", "[NAME], embedded in the notecard: click to open it", args); break;
        case LLAssetType::AT_SOUND:
            tip = alSaid("EmbeddedItemPlayTip", "[NAME], embedded in the notecard: click to play it and copy it to your inventory", args);
            break;
        default: tip = alSaid("EmbeddedItemCopyTip", "[NAME], embedded in the notecard: click to copy it to your inventory", args); break;
    }
    // A button with the item's icon and name, as wide as they are.
    LLButton::Params p;
    p.name                    = "embedded_item";
    p.label                   = item->getName();
    p.font                    = font;
    p.image_overlay           = LLUI::getUIImage(mWorld.iconOf(*item));
    p.image_overlay_alignment = "left";
    p.tool_tip                = tip;
    const S32 width           = font->getWidth(item->getName()) + 16 + 12;
    p.rect                    = LLRect(0, 0, width, 0);
    ItemButton* button        = LLUICtrlFactory::create<ItemButton>(p);
    button->setClickedCallback([this, item](LLUICtrl*, const LLSD&) { open(item); });
    button->pressedAt = [this](S32 screen_x, S32 screen_y) { mWorld.pressedAt(screen_x, screen_y); };
    button->draggedTo = [this, item](S32 screen_x, S32 screen_y) { return dragOut(item, screen_x, screen_y); };
    ALTextView::Atom atom;
    atom.at      = at;
    atom.length  = 4;
    atom.width   = width;
    atom.view    = button;
    atom.tooltip = p.tool_tip();
    atom.value   = static_cast<S32>(index);
    return atom;
}

bool ALNotecardEmbedded::copy(LLPointer<LLInventoryItem> item, const LLUUID& folder, U32 callback_id)
{
    if (item.isNull())
    {
        return false;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = item->getName();
    if (!mInAsset.count(item->getUUID()))
    {
        // The server copies out of the asset it has, which a drop is only
        // in once saved.
        mHolder.say(alSaid("NotecardCopyUnsaved", "Save the notecard first: [NAME] can only be copied from a saved notecard", args), true);
        return false;
    }
    std::weak_ptr<ALNotecardEmbedded> weak  = weak_from_this();
    const bool                         asked = mWorld.askCopy(mHolder.notecard(), item->getUUID(), folder, callback_id, [weak, args](const std::string& error) {
        if (std::shared_ptr<ALNotecardEmbedded> self = weak.lock())
        {
            LLStringUtil::format_map_t why = args;
            why["[ERROR]"]                 = error;
            self->mHolder.say(alSaid("NotecardCopyRefused", "[NAME] could not be copied: [ERROR]", why), true);
        }
    });
    if (!asked)
    {
        mHolder.say(alSaid("NotecardCopyFailed", "[NAME] could not be copied: not connected to a region", args), true);
    }
    return asked;
}

bool ALNotecardEmbedded::dragOut(const LLPointer<LLInventoryItem>& item, S32 screen_x, S32 screen_y)
{
    if (item.isNull() || !mWorld.pastDragStart(screen_x, screen_y))
    {
        return false;
    }
    // The server copies out of the asset it has, which a drop is only in
    // once saved.
    if (!mInAsset.count(item->getUUID()))
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = item->getName();
        mHolder.say(alSaid("NotecardCopyUnsaved", "Save the notecard first: [NAME] can only be copied from a saved notecard", args), true);
        return true;
    }
    mWorld.dragOut(*item, mHolder.notecard());
    return true;
}

void ALNotecardEmbedded::open(LLPointer<LLInventoryItem> item)
{
    if (item.isNull())
    {
        return;
    }
    // The answers may come long after, the tab gone by then.
    std::weak_ptr<ALNotecardEmbedded> weak = weak_from_this();
    if (mWorld.open(item, mHolder.notecard(), [weak, item](const LLUUID& folder, U32 callback_id) {
            if (std::shared_ptr<ALNotecardEmbedded> self = weak.lock())
            {
                self->copy(item, folder, callback_id);
            }
        }))
    {
        return;
    }
    // A drop not yet saved is said so before the question, since the
    // answer would be no.
    if (!mInAsset.count(item->getUUID()))
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = item->getName();
        mHolder.say(alSaid("NotecardCopyUnsaved", "Save the notecard first: [NAME] can only be copied from a saved notecard", args), true);
        return;
    }
    // The server finds the folder for it.
    mWorld.confirmCopy([weak, item]() {
        if (std::shared_ptr<ALNotecardEmbedded> self = weak.lock())
        {
            self->copy(item, LLUUID::null);
        }
    });
}

// static
LLSD ALNotecardEmbedded::asLLSD(const items_t& items)
{
    LLSD out = LLSD::emptyArray();
    for (const LLPointer<LLInventoryItem>& item : items)
    {
        out.append(item.notNull() ? item->asLLSD() : LLSD());
    }
    return out;
}

// static
ALNotecardEmbedded::items_t ALNotecardEmbedded::fromLLSD(const LLSD& items)
{
    items_t out;
    for (LLSD::array_const_iterator it = items.beginArray(); it != items.endArray(); ++it)
    {
        LLPointer<LLInventoryItem> item;
        if (it->isMap())
        {
            item = new LLInventoryItem();
            if (!item->fromLLSD(*it))
            {
                item = nullptr;
            }
        }
        out.push_back(item);
    }
    return out;
}
