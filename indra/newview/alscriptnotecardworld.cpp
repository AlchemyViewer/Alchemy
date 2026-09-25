/**
 * @file alscriptnotecardworld.cpp
 * @brief The viewer's side of a notecard's items in Script Studio: previews, places, profiles, sounds and copies.
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

#include "alscriptnotecardtab.h"

#include "alscriptworkspace.h"
#include "llagent.h"
#include "llappviewer.h"
#include "llaudioengine.h"
#include "llavataractions.h"
#include "llcorehttputil.h"
#include "llenvironment.h"
#include "llfloaterreg.h"
#include "llfloatersidepanelcontainer.h"
#include "llinventoryfunctions.h"
#include "llinventoryicon.h"
#include "lllandmark.h"
#include "lllandmarkactions.h"
#include "lllandmarklist.h"
#include "llmaterialeditor.h"
#include "llnotificationsutil.h"
#include "llpreviewtexture.h"
#include "lltooldraganddrop.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"

namespace
{
    class ALScriptNotecardWorld final : public ALScriptNotecardTab::World
    {
    public:
        std::string iconOf(const LLInventoryItem& item) const override
        {
            return LLInventoryIcon::getIconName(item.getType(), item.getInventoryType(), item.getFlags());
        }

        bool draggedFromNotecard() const override
        {
            return LLToolDragAndDrop::getInstance()->getSource() == LLToolDragAndDrop::SOURCE_NOTECARD;
        }

        bool carriesSettings() const override { return LLEnvironment::instance().isExtendedEnvironmentEnabled(); }

        bool mayCopy(const LLInventoryItem& item) const override { return item.getPermissions().allowCopyBy(gAgentID); }

        U32 frame() const override { return gFrameCount; }

        bool open(const LLPointer<LLInventoryItem>& item, const ALScriptRef& ref, std::function<void(const LLUUID& folder, U32 callback_id)> copy) override
        {
            // As the legacy notecard does.
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
                    return true;
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
                    return true;
                }
                case LLAssetType::AT_LANDMARK:
                {
                    // The place: the landmark already in the inventory for
                    // it, or a copy taken into the landmarks folder and then
                    // shown. The asset may come long after.
                    auto show = [](const LLUUID& landmark_id) {
                        LLSD key;
                        key["type"] = "landmark";
                        key["id"]   = landmark_id;
                        LLFloaterSidePanelContainer::showPanel("places", key);
                    };
                    auto placed = [item, show, copy](LLLandmark* landmark) {
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
                        copy(get_folder_by_itemtype(item), gInventoryCallbacks.registerCB(new LLBoostFuncInventoryCallback(show)));
                    };
                    if (LLLandmark* landmark = gLandmarkList.getAsset(item->getAssetUUID(), placed))
                    {
                        placed(landmark);
                    }
                    return true;
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
                    return true;
                case LLAssetType::AT_SOUND:
                    if (gAudiop)
                    {
                        gAudiop->triggerSound(item->getAssetUUID(), gAgentID, 1.f, LLAudioEngine::AUDIO_TYPE_UI, gAgent.getPositionGlobal());
                    }
                    return false;
                case LLAssetType::AT_SETTINGS:
                    if (!LLEnvironment::instance().isInventoryEnabled())
                    {
                        LLNotificationsUtil::add("NoEnvironmentSettings");
                        return true;
                    }
                    return false;
                default:
                    return false;
            }
        }

        void confirmCopy(std::function<void()> yes) override
        {
            LLNotificationsUtil::add("ConfirmItemCopy", LLSD(), LLSD(), [yes](const LLSD& notification, const LLSD& response) {
                if (LLNotificationsUtil::getSelectedOption(notification, response) == 0)
                {
                    yes();
                }
            });
        }

        bool askCopy(const ALScriptRef& notecard, const LLUUID& item, const LLUUID& folder, U32 callback_id,
                     std::function<void(const std::string& error)> refused) override
        {
            // As copy_inventory_from_notecard does, with an ear for the
            // answer: the request under the agent's policy, to the object's
            // region or the agent's.
            LLViewerRegion* region = nullptr;
            if (notecard.object.notNull())
            {
                if (LLViewerObject* object = gObjectList.findObject(notecard.object))
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
                return false;
            }
            LLSD body;
            body["notecard-id"] = notecard.item;
            body["object-id"]   = notecard.object;
            body["item-id"]     = item;
            body["folder-id"]   = folder;
            body["callback-id"] = static_cast<LLSD::Integer>(callback_id);
            return region->requestPostCapability("CopyInventoryFromNotecard", body, nullptr, [refused](const LLSD& results) {
                refused(results.has(LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_MESSAGE)
                            ? results[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_MESSAGE].asString()
                            : std::string());
            });
        }
    };
}

// static
ALScriptNotecardTab::World& ALScriptNotecardTab::viewer()
{
    static ALScriptNotecardWorld world;
    return world;
}
