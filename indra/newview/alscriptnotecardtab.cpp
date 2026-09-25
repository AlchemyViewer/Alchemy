/**
 * @file alscriptnotecardtab.cpp
 * @brief A notecard's items in its Script Studio tab: buttons in the text, dropped in, saved, opened and copied out.
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
#include "alfilewrite.h"
#include "alnotecarditems.h"
#include "alscriptmodules.h"
#include "alscriptpreprocessor.h"
#include "alscriptweightspane.h"
#include "alemptystate.h"
#include "aljumpbar.h"
#include "aloutputview.h"
#include "alobjectproperties.h"
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
#include "llaudioengine.h"
#include "llavataractions.h"
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
#include "llfloatersidepanelcontainer.h"
#include "lllandmarkactions.h"
#include "lllandmarklist.h"
#include "llenvironment.h"
#include "llinventoryfunctions.h"
#include "llinventoryicon.h"
#include "llinventorymodel.h"
#include "lllayoutstack.h"
#include "llmaterialeditor.h"
#include "llpreviewtexture.h"
#include "lllineeditor.h"
#include "llmenugl.h"
#include "llnotecard.h"
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
#include "llviewerassettype.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewermenu.h"
#include "llweb.h"
#include "llviewermenufile.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "rlvhandler.h"
#include "rlvlocks.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void ALFloaterScriptStudio::wireNotecard(Doc& doc)
{
    Doc* raw = &doc;
    doc.editor->setDropHandler([this, raw](S32 x, S32 y, MASK, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip) {
        return dropOnNotecard(*raw, x, y, drop, type, cargo, accept, tooltip);
    });
    // A placeholder put in by an edit -- a drop, an undo, a redo, a paste --
    // gets its button as the edit lands, on the lines the edit touched; the
    // view has slid its own atoms by then.
    doc.embeddedEdits = doc.editor->document().onChanged([this, raw](const ALTextDocument::Edit& edit) {
        if (edit.inserted.find('\xF4') != std::string::npos)
        {
            placeEmbeddedItems(*raw, edit.range.begin.line, edit.endAfter().line);
        }
    });
}

void ALFloaterScriptStudio::placeEmbeddedItems(Doc& doc, S32 first_line, S32 last_line)
{
    const ALTextDocument& text = doc.editor->document();
    for (S32 line = llmax(0, first_line); line <= last_line && line < text.lineCount() && !doc.embedded.empty(); ++line)
    {
        ALNotecardItems::forEach(text.line(line), [&](size_t column, size_t index) {
            const ALTextPos at(line, static_cast<S32>(column));
            if (index < doc.embedded.size() && doc.embedded[index].notNull() && !doc.editor->atomAt(at))
            {
                doc.editor->addAtom(embeddedAtom(doc, at, index));
            }
        });
    }
}

void ALFloaterScriptStudio::placeEmbeddedItems(Doc& doc)
{
    // Each item's character (ALNotecardItems) becomes an atom over its
    // four bytes, so the text keeps it and a save carries it.
    std::vector<ALTextView::Atom> atoms;
    const ALTextDocument&         text = doc.editor->document();
    for (S32 line = 0; line < text.lineCount() && !doc.embedded.empty(); ++line)
    {
        ALNotecardItems::forEach(text.line(line), [&](size_t column, size_t index) {
            if (index < doc.embedded.size() && doc.embedded[index].notNull())
            {
                atoms.push_back(embeddedAtom(doc, ALTextPos(line, static_cast<S32>(column)), index));
            }
        });
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
    // Each item numbered afresh in the order the text first stands them;
    // an item the text no longer stands anywhere is left behind.
    const std::vector<size_t> order = ALNotecardItems::renumber(text, [&doc](size_t index) {
        return index < doc.embedded.size() && doc.embedded[index].notNull();
    });
    for (const size_t index : order)
    {
        items.push_back(doc.embedded[index]);
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
        const std::string placeholder = ALNotecardItems::charOf(index);
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
            // The asset may come long after, the window or the tab gone by
            // then: both found again by what they are.
            const LLHandle<LLFloater> handle = getHandle();
            const std::string         id     = doc.id;
            auto placed = [handle, id, item, show](LLLandmark* landmark) {
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
                ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
                const size_t           index  = studio ? studio->indexOf(id) : NONE;
                if (index != NONE)
                {
                    studio->copyEmbeddedItem(*studio->mDocs[index], item, get_folder_by_itemtype(item),
                                             gInventoryCallbacks.registerCB(new LLBoostFuncInventoryCallback(show)));
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
    const LLHandle<LLFloater> handle = getHandle();
    const std::string         id     = doc.id;
    LLNotificationsUtil::add("ConfirmItemCopy", LLSD(), LLSD(), [handle, id, item](const LLSD& notification, const LLSD& response) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(id) : NONE;
        if (LLNotificationsUtil::getSelectedOption(notification, response) == 0 && item.notNull() && index != NONE)
        {
            // The server finds the folder for it.
            studio->copyEmbeddedItem(*studio->mDocs[index], item, LLUUID::null);
        }
    });
}

// static
LLSD ALFloaterScriptStudio::itemsAsLLSD(const std::vector<LLPointer<LLInventoryItem>>& items)
{
    // Each in its place, a missing one kept as nothing, since the text
    // says an item by where it stands in the list.
    LLSD out = LLSD::emptyArray();
    for (const LLPointer<LLInventoryItem>& item : items)
    {
        out.append(item.notNull() ? item->asLLSD() : LLSD());
    }
    return out;
}

// static
std::vector<LLPointer<LLInventoryItem>> ALFloaterScriptStudio::itemsFrom(const LLSD& items)
{
    std::vector<LLPointer<LLInventoryItem>> out;
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
