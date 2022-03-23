/**
* @file alviewermenu.cpp
* @brief Builds menus out of items. Imagine the fast, easy, fun Alchemy style
*
* $LicenseInfo:firstyear=2013&license=viewerlgpl$
* Copyright (C) 2013 Alchemy Developer Group
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
* $/LicenseInfo$
**/

#include "llviewerprecompiledheaders.h"
#include "alviewermenu.h"

// library
#include "llclipboard.h"
#include "llfloaterreg.h"
#include "llsdserialize.h"
#include "lltrans.h"
#include "llview.h"

// newview
#include "alavataractions.h"
#include "alcinematicmode.h"
#include "alfloaterparticleeditor.h"
#include "llagent.h"
#include "llhudobject.h"
#include "llnotifications.h"
#include "llnotificationsutil.h"
#include "llselectmgr.h"
#include "llviewercontrol.h"
#include "llviewermenu.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvoavatar.h"
#include "llvoavatarself.h"

namespace
{
	bool enable_edit_particle_source()
	{
		LLObjectSelectionHandle selection = LLSelectMgr::getInstanceFast()->getSelection();
		for (LLObjectSelection::valid_root_iterator iter = selection->valid_root_begin();
			iter != selection->valid_root_end(); ++iter)
		{
			LLSelectNode* node = *iter;
			if (node->mPermissions->getOwner() == gAgent.getID())
			{
				return true;
			}
		}
		return false;
	}

	void edit_particle_source()
	{
		LLViewerObject* objectp = LLSelectMgr::getInstanceFast()->getSelection()->getPrimaryObject();
		if (objectp)
		{
			ALFloaterParticleEditor* particleEditor = LLFloaterReg::showTypedInstance<ALFloaterParticleEditor>("particle_editor", LLSD(objectp->getID()), TAKE_FOCUS_YES);
			if (particleEditor)
				particleEditor->setObject(objectp);
		}
	}

	void world_clear_effects()
	{
		LLHUDObject::markViewerEffectsDead();
	}

	void world_sync_animations()
	{
		for (S32 i = 0; i < gObjectList.getNumObjects(); ++i)
		{
			LLViewerObject* object = gObjectList.getObject(i);
			if (object)
			{
				LLVOAvatar* avatarp = object->asAvatar();
				if (avatarp)
				{
					for (const auto& playpair : avatarp->mPlayingAnimations)
					{
						avatarp->stopMotion(playpair.first, TRUE);
						avatarp->startMotion(playpair.first);
					}
				}
			}
		}
	}

	void avatar_copy_data(const LLSD& userdata)
	{
		LLViewerObject* objectp = LLSelectMgr::getInstanceFast()->getSelection()->getPrimaryObject();
		if (!objectp)
			return;

		LLVOAvatar* avatarp = find_avatar_from_object(objectp);
		if (avatarp)
		{
			ALAvatarActions::copyData(avatarp->getID(), userdata);
		}
	}

	void avatar_undeform_self()
	{
		if (!isAgentAvatarValid()) 
			return;

		gAgentAvatarp->resetSkeleton(true);
		LLMessageSystem* msg = gMessageSystem;
		msg->newMessageFast(_PREHASH_AgentAnimation);
		msg->nextBlockFast(_PREHASH_AgentData);
		msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
		msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
		msg->nextBlockFast(_PREHASH_AnimationList);
		msg->addUUIDFast(_PREHASH_AnimID, LLUUID("e5afcabe-1601-934b-7e89-b0c78cac373a"));
		msg->addBOOLFast(_PREHASH_StartAnim, TRUE);
		msg->nextBlockFast(_PREHASH_AnimationList);
		msg->addUUIDFast(_PREHASH_AnimID, LLUUID("d307c056-636e-dda6-4a3c-b3a43c431ca8"));
		msg->addBOOLFast(_PREHASH_StartAnim, TRUE);
		msg->nextBlockFast(_PREHASH_AnimationList);
		msg->addUUIDFast(_PREHASH_AnimID, LLUUID("319b4e7a-18fc-1f9e-6411-dd10326c0c7e"));
		msg->addBOOLFast(_PREHASH_StartAnim, TRUE);
		msg->nextBlockFast(_PREHASH_AnimationList);
		msg->addUUIDFast(_PREHASH_AnimID, LLUUID("f05d765d-0e01-5f9a-bfc2-fdc054757e55"));
		msg->addBOOLFast(_PREHASH_StartAnim, TRUE);
		msg->nextBlockFast(_PREHASH_PhysicalAvatarEventList);
		msg->addBinaryDataFast(_PREHASH_TypeData, nullptr, 0);
		msg->sendReliable(gAgent.getRegion()->getHost());
	}

	void object_copy_key()
	{
		LLViewerObject* objectp = LLSelectMgr::getInstanceFast()->getSelection()->getPrimaryObject();
		if (!objectp)
			return;

		const LLUUID& object_id = objectp->getID();
		LLWString idwstr = utf8string_to_wstring(object_id.asString());
		LLClipboard::instance().copyToClipboard(idwstr,0, idwstr.size());
	}

	bool can_teleport_to()
	{
		LLVOAvatar* avatarp = find_avatar_from_object(LLSelectMgr::getInstanceFast()->getSelection()->getPrimaryObject());
		if (avatarp)
		{
			return ALAvatarActions::canTeleportTo(avatarp->getID());
		}
		return false;
	}

	void teleport_to()
	{
		LLVOAvatar* avatarp = find_avatar_from_object(LLSelectMgr::getInstanceFast()->getSelection()->getPrimaryObject());
		if (avatarp)
		{
			ALAvatarActions::teleportTo(avatarp->getID());
		}
	}

	bool can_manage_avatar_estate()
	{
		LLVOAvatar* avatarp = find_avatar_from_object(LLSelectMgr::getInstanceFast()->getSelection()->getPrimaryObject());
		if (avatarp)
		{
			return ALAvatarActions::canManageAvatarsEstate(avatarp->getID());
		}
		return false;
	}

	void manage_estate(const LLSD& param)
	{
		LLVOAvatar* avatarp = find_avatar_from_object(LLSelectMgr::getInstanceFast()->getSelection()->getPrimaryObject());
		if (avatarp)
		{
			S32 action = param.asInteger();
			switch (action)
			{
			case 0:
				ALAvatarActions::estateTeleportHome(avatarp->getID());
				break;
			case 1:
				ALAvatarActions::estateKick(avatarp->getID());
				break;
			case 2:
				ALAvatarActions::estateBan(avatarp->getID());
				break;
			}
		}
	}

	void confirm_cinematic_mode(const LLSD& notification, const LLSD& response)
	{
		S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
		if (option == 0) // OK
		{
			ALCinematicMode::toggle();
		}
	}

	bool toggle_cinematic_mode()
	{
		LLNotification::Params params("CinematicConfirmHideUI");
		params.functor.function(boost::bind(&confirm_cinematic_mode, _1, _2));
		LLSD substitutions;
		substitutions["SHORTCUT"] = "Alt+Shift+C";
		params.substitutions = substitutions;
		if (!ALCinematicMode::isEnabled())
		{
			// hiding, so show notification
			LLNotifications::instance().add(params);
		}
		else
		{
			LLNotifications::instance().forceResponse(params, 0);
		}
		return true;
	}

	bool is_powerful_wizard()
	{
		LLViewerObject* objpos = LLSelectMgr::getInstanceFast()->getSelection()->getFirstRootObject();
		if (objpos)
		{
			if (objpos->permYouOwner() && gSavedSettings.getBOOL("AlchemyPowerfulWizard"))
				return true;
		}
		return false;
	}


	void object_explode()
	{
		LLViewerObject* objpos = LLSelectMgr::getInstanceFast()->getSelection()->getFirstRootObject();
		if (objpos)
		{
			if (!objpos->permYouOwner())
			{
				LLNotificationsUtil::add("AlchemyUnpoweredWizard", LLSD());
				return;
			}

			LLNotificationsUtil::add("AlchemyExplosions", LLSD());

			/*
				NOTE: oh god how did this get here
			*/
			LLSelectMgr::getInstanceFast()->selectionUpdateTemporary(1);//set temp to TRUE
			LLSelectMgr::getInstanceFast()->selectionUpdatePhysics(1);
			LLSelectMgr::getInstanceFast()->sendDelink();
			LLSelectMgr::getInstanceFast()->deselectAll();
		}
	}

	void object_destroy()
	{
		LLViewerObject* objpos = LLSelectMgr::getInstanceFast()->getSelection()->getFirstRootObject();
		if (objpos)
		{
			if (!objpos->permYouOwner())
			{
				LLNotificationsUtil::add("AlchemyUnpoweredWizard", LLSD());
				return;
			}

			LLNotificationsUtil::add("AlchemyDestroyObject", LLSD());

			/*
				NOTE: Temporary objects, when thrown off world/put off world,
				do not report back to the viewer, nor go to lost and found.

				So we do selectionUpdateTemporary(1)
			*/
			LLSelectMgr::getInstanceFast()->selectionUpdateTemporary(1);//set temp to TRUE
			LLVector3 pos = objpos->getPosition();//get the x and the y
			pos.mV[VZ] = FLT_MAX;//create the z
			objpos->setPositionParent(pos);//set the x y z
			LLSelectMgr::getInstanceFast()->sendMultipleUpdate(UPD_POSITION);//send the data
		}
	}

	void object_force_delete()
	{
		LLViewerObject* objpos = LLSelectMgr::getInstanceFast()->getSelection()->getFirstRootObject();
		if (objpos)
		{
			if (!objpos->permYouOwner())
			{
				LLNotificationsUtil::add("AlchemyUnpoweredWizard", LLSD());
				return;
			}
			LLSelectMgr::getInstanceFast()->selectForceDelete();

		}
	}

	void spawn_debug_simfeatures()
	{
		if (LLViewerRegion* regionp = gAgent.getRegion())
		{
			LLSD sim_features, args;
			std::stringstream features_str;
			regionp->getSimulatorFeatures(sim_features);
			LLSDSerialize::toPrettyXML(sim_features, features_str);
			args["title"] = llformat("%s - %s", LLTrans::getString("SimulatorFeaturesTitle").c_str(), regionp->getName().c_str());
			args["data"] = features_str.str();
			LLFloaterReg::showInstance("generic_text", args);
		}
	}
}

////////////////////////////////////////////////////////

void ALViewerMenu::initialize_menus()
{
	LLUICtrl::EnableCallbackRegistry::Registrar& enable = LLUICtrl::EnableCallbackRegistry::currentRegistrar();
	enable.add("Alchemy.PowerfulWizard", [](LLUICtrl* ctrl, const LLSD& param) { return is_powerful_wizard(); });
	enable.add("Avatar.EnableManageEstate", [](LLUICtrl* ctrl, const LLSD& param) { return can_manage_avatar_estate(); });
	enable.add("Avatar.EnableTeleportTo", [](LLUICtrl* ctrl, const LLSD& param) { return can_teleport_to(); });
	enable.add("Object.EnableEditParticles", [](LLUICtrl* ctrl, const LLSD& param) { return enable_edit_particle_source(); });

	LLUICtrl::CommitCallbackRegistry::Registrar& commit = LLUICtrl::CommitCallbackRegistry::currentRegistrar();
	commit.add("Avatar.CopyData",		[](LLUICtrl* ctrl, const LLSD& param) { avatar_copy_data(param); });
	commit.add("Avatar.ManageEstate", [](LLUICtrl* ctrl, const LLSD& param) { manage_estate(param); });
	commit.add("Avatar.TeleportTo", [](LLUICtrl* ctrl, const LLSD& param) { teleport_to(); });

	commit.add("Advanced.DebugSimFeatures", [](LLUICtrl* ctrl, const LLSD& param) { spawn_debug_simfeatures(); });

	commit.add("Object.CopyID", [](LLUICtrl* ctrl, const LLSD& param) { object_copy_key(); });
	commit.add("Object.EditParticles",	[](LLUICtrl* ctrl, const LLSD& param) { edit_particle_source(); });
	commit.add("Object.AlchemyExplode", [](LLUICtrl* ctrl, const LLSD& param) { object_explode(); });
	commit.add("Object.AlchemyDestroy", [](LLUICtrl* ctrl, const LLSD& param) { object_destroy(); });
	commit.add("Object.AlchemyForceDelete", [](LLUICtrl* ctrl, const LLSD& param) { object_force_delete(); });

	commit.add("Tools.UndeformSelf", [](LLUICtrl* ctrl, const LLSD& param) { avatar_undeform_self(); });

	commit.add("World.ClearEffects",	[](LLUICtrl* ctrl, const LLSD& param) { world_clear_effects(); });
	commit.add("World.SyncAnimations",	[](LLUICtrl* ctrl, const LLSD& param) { world_sync_animations(); });

	commit.add("View.ToggleCinematicMode", [](LLUICtrl* ctrl, const LLSD& param) { toggle_cinematic_mode(); });
}
