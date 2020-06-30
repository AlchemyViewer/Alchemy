/**
* @file alchatcommand.cpp
* @brief ALChatCommand implementation for chat input commands
*
* $LicenseInfo:firstyear=2013&license=viewerlgpl$
* Copyright (C) 2013 Drake Arconis
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

#include "alchatcommand.h"

// lib includes
#include "llcalc.h"
#include "llparcel.h"
#include "llstring.h"
#include "material_codes.h"
#include "object_flags.h"

// viewer includes
#include "alaoengine.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llagentui.h"
#include "llcommandhandler.h"
#include "llfloaterimnearbychat.h"
#include "llfloaterreg.h"
#include "llfloaterregioninfo.h"
#include "llnotificationsutil.h"
#include "llregioninfomodel.h"
#include "llstartup.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llviewermessage.h"
#include "llviewerobjectlist.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
#include "llvoavatarself.h"
#include "llvolume.h"
#include "llvolumemessage.h"

bool ALChatCommand::parseCommand(std::string data)
{
	static LLCachedControl<bool> enableChatCmd(gSavedSettings, "AlchemyChatCommandEnable", true);
	if (enableChatCmd)
	{
		utf8str_tolower(data);
		std::istringstream input(data);
		std::string cmd;

		if (!(input >> cmd))	return false;

		static LLCachedControl<std::string> sDrawDistanceCommand(gSavedSettings, "AlchemyChatCommandDrawDistance", "/dd");
		static LLCachedControl<std::string> sHeightCommand(gSavedSettings, "AlchemyChatCommandHeight", "/gth");
		static LLCachedControl<std::string> sGroundCommand(gSavedSettings, "AlchemyChatCommandGround", "/flr");
		static LLCachedControl<std::string> sPosCommand(gSavedSettings, "AlchemyChatCommandPos", "/pos");
		static LLCachedControl<std::string> sRezPlatCommand(gSavedSettings, "AlchemyChatCommandRezPlat", "/plat");
		static LLCachedControl<std::string> sHomeCommand(gSavedSettings, "AlchemyChatCommandHome", "/home");
		static LLCachedControl<std::string> sSetHomeCommand(gSavedSettings, "AlchemyChatCommandSetHome", "/sethome");
		static LLCachedControl<std::string> sCalcCommand(gSavedSettings, "AlchemyChatCommandCalc", "/calc");
		static LLCachedControl<std::string> sMaptoCommand(gSavedSettings, "AlchemyChatCommandMapto", "/mapto");
		static LLCachedControl<std::string> sClearCommand(gSavedSettings, "AlchemyChatCommandClearNearby", "/clr");
		static LLCachedControl<std::string> sRegionMsgCommand(gSavedSettings, "AlchemyChatCommandRegionMessage", "/regionmsg");
		static LLCachedControl<std::string> sSetNearbyChatChannelCmd(gSavedSettings, "AlchemyChatCommandSetChatChannel", "/setchannel");
		static LLCachedControl<std::string> sResyncAnimCommand(gSavedSettings, "AlchemyChatCommandResyncAnim", "/resync");
		static LLCachedControl<std::string> sTeleportToCam(gSavedSettings, "AlchemyChatCommandTeleportToCam", "/tp2cam");
		static LLCachedControl<std::string> sHoverHeight(gSavedSettings, "AlchemyChatCommandHoverHeight", "/hover");
		static LLCachedControl<std::string> sAOCommand(gSavedSettings, "AlchemyChatCommandAnimationOverride", "/ao");

		if (cmd == utf8str_tolower(sDrawDistanceCommand)) // dd
		{
			F32 dist;
			if (input >> dist)
			{
				dist = llclamp(dist, 16.f, 512.f);
				gSavedSettings.setF32("RenderFarClip", dist);
				gAgentCamera.mDrawDistance = dist;
				return true;
			}
		}
		else if (cmd == utf8str_tolower(sHeightCommand)) // gth
		{
			F64 z;
			if (input >> z)
			{
				LLVector3d pos_global = gAgent.getPositionGlobal();
				pos_global.mdV[VZ] = z;
				gAgent.teleportViaLocation(pos_global);
				return true;
			}
		}
		else if (cmd == utf8str_tolower(sGroundCommand)) // flr
		{
			LLVector3d pos_global = gAgent.getPositionGlobal();
			pos_global.mdV[VZ] = 0.0;
			gAgent.teleportViaLocation(pos_global);
			return true;
		}
		else if (cmd == utf8str_tolower(sPosCommand)) // pos
		{
			F64 x, y, z;
			if ((input >> x) && (input >> y) && (input >> z))
			{
				LLViewerRegion* regionp = gAgent.getRegion();
				if (regionp)
				{
					LLVector3d target_pos = regionp->getPosGlobalFromRegion(LLVector3((F32) x, (F32) y, (F32) z));
					gAgent.teleportViaLocation(target_pos);
				}
				return true;
			}
		}
		else if (cmd == utf8str_tolower(sRezPlatCommand)) // plat
		{
			F32 size;
			if (!(input >> size))
				size = static_cast<F32>(gSavedSettings.getF32("AlchemyChatCommandRezPlatSize"));

			const LLVector3& agent_pos = gAgent.getPositionAgent();
			const LLVector3 rez_pos(agent_pos.mV[VX], agent_pos.mV[VY], agent_pos.mV[VZ] - ((gAgentAvatarp->getScale().mV[VZ] / 2.f) + 0.25f + (gAgent.getVelocity().magVec() * 0.333f)));

			LLMessageSystem* msg = gMessageSystem;
			msg->newMessageFast(_PREHASH_ObjectAdd);
			msg->nextBlockFast(_PREHASH_AgentData);
			msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
			msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
			LLUUID group_id = gAgent.getGroupID();
			if (gSavedSettings.getBOOL("AlchemyRezUnderLandGroup"))
			{
				LLParcel* land_parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();
				// Is the agent in the land group
				if (gAgent.isInGroup(land_parcel->getGroupID()))
					group_id = land_parcel->getGroupID();
				// Is the agent in the land group (the group owns the land)
				else if (gAgent.isInGroup(land_parcel->getOwnerID()))
					group_id = land_parcel->getOwnerID();
			}
			msg->addUUIDFast(_PREHASH_GroupID, group_id);
			msg->nextBlockFast(_PREHASH_ObjectData);
			msg->addU8Fast(_PREHASH_PCode, LL_PCODE_VOLUME);
			msg->addU8Fast(_PREHASH_Material, LL_MCODE_STONE);
			msg->addU32Fast(_PREHASH_AddFlags, agent_pos.mV[VZ] > 4096.f ? FLAGS_CREATE_SELECTED : 0U);

			LLVolumeParams volume_params;
			volume_params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
			volume_params.setBeginAndEndS(0.f, 1.f);
			volume_params.setBeginAndEndT(0.f, 1.f);
			volume_params.setRatio(1.f, 1.f);
			volume_params.setShear(0.f, 0.f);
			LLVolumeMessage::packVolumeParams(&volume_params, msg);

			msg->addVector3Fast(_PREHASH_Scale, LLVector3(size, size, 0.25f));
			msg->addQuatFast(_PREHASH_Rotation, LLQuaternion());
			msg->addVector3Fast(_PREHASH_RayStart, rez_pos);
			msg->addVector3Fast(_PREHASH_RayEnd, rez_pos);
			msg->addUUIDFast(_PREHASH_RayTargetID, LLUUID::null);
			msg->addU8Fast(_PREHASH_BypassRaycast, TRUE);
			msg->addU8Fast(_PREHASH_RayEndIsIntersection, FALSE);
			msg->addU8Fast(_PREHASH_State, FALSE);
			msg->sendReliable(gAgent.getRegionHost());

			return true;
		}
		else if (cmd == utf8str_tolower(sHomeCommand)) // home
		{
			gAgent.teleportHome();
			return true;
		}
		else if (cmd == utf8str_tolower(sSetHomeCommand)) // sethome
		{
			gAgent.setStartPosition(START_LOCATION_ID_HOME);
			return true;
		}
		else if (cmd == utf8str_tolower(sCalcCommand)) // calc
		{
			if (data.length() > cmd.length() + 1)
			{
				F32 result = 0.f;
				std::string expr = data.substr(cmd.length() + 1);
				LLStringUtil::toUpper(expr);
				if (LLCalc::getInstance()->evalString(expr, result))
				{
					LLSD args;
					args["EXPRESSION"] = expr;
					args["RESULT"] = result;
					LLNotificationsUtil::add("ChatCommandCalc", args);
					return true;
				}
				LLNotificationsUtil::add("ChatCommandCalcFailed");
				return true;
			}
		}
		else if (cmd == utf8str_tolower(sMaptoCommand)) // mapto
		{
			const std::string::size_type length = cmd.length() + 1;
			if (data.length() > length)
			{
				const LLVector3d& pos = gAgent.getPositionGlobal();
				LLSD params;
				params.append(data.substr(length));
				params.append(fmodf(static_cast<F32>(pos.mdV[VX]), REGION_WIDTH_METERS));
				params.append(fmodf(static_cast<F32>(pos.mdV[VY]), REGION_WIDTH_METERS));
				params.append(fmodf(static_cast<F32>(pos.mdV[VZ]), REGION_HEIGHT_METERS));
				LLCommandDispatcher::dispatch("teleport", params, LLSD(), nullptr, "clicked", true);
				return true;
			}
		}
		else if (cmd == utf8str_tolower(sClearCommand))
		{
			LLFloaterIMNearbyChat* nearby_chat = LLFloaterReg::findTypedInstance<LLFloaterIMNearbyChat>("nearby_chat");
			if (nearby_chat)
			{
				nearby_chat->reloadMessages(true);
			}
			return true;
		}
		else if (cmd == "/droll")
		{
			S32 dice_sides;
			if (!(input >> dice_sides))
				dice_sides = 6;
			LLSD args;
			args["RESULT"] = (ll_rand(dice_sides) + 1);
			LLNotificationsUtil::add("ChatCommandDiceRoll", args);
			return true;
		}
		else if (cmd == utf8str_tolower(sRegionMsgCommand)) // Region Message / Dialog
		{
			if (data.length() > cmd.length() + 1)
			{
				std::string notification_message = data.substr(cmd.length() + 1);
				std::vector<std::string> strings(5, "-1");
				// [0] grid_x, unused here
				// [1] grid_y, unused here
				strings[2] = gAgentID.asString(); // [2] agent_id of sender
				// [3] senter name
				std::string name;
				LLAgentUI::buildFullname(name);
				strings[3] = name;
				strings[4] = notification_message; // [4] message
				LLRegionInfoModel::sendEstateOwnerMessage(gMessageSystem, "simulatormessage", LLFloaterRegionInfo::getLastInvoice(), strings);
				return true;
			}
		}
		else if (cmd == utf8str_tolower(sSetNearbyChatChannelCmd)) // Set nearby chat channel
		{
			S32 chan;
			if (input >> chan)
			{
				gSavedSettings.setS32("AlchemyNearbyChatChannel", chan);
				return true;
			}
		}
		else if (cmd == utf8str_tolower(sTeleportToCam))
		{
			gAgent.teleportViaLocation(gAgentCamera.getCameraPositionGlobal());
			return true;
		}
		else if (cmd == utf8str_tolower(sHoverHeight)) // Hover height
		{
			F32 height;
			if (input >> height)
			{
				gSavedPerAccountSettings.set("AvatarHoverOffsetZ",
											 llclamp<F32>(height, MIN_HOVER_Z, MAX_HOVER_Z));
				return true;
			}
		}
		else if (cmd == utf8str_tolower(sResyncAnimCommand)) // Resync Animations
		{
			for (S32 i = 0; i < gObjectList.getNumObjects(); i++)
			{
				LLViewerObject* object = gObjectList.getObject(i);
				if (object && object->isAvatar())
				{
					LLVOAvatar* avatarp = (LLVOAvatar*)object;
					if (avatarp)
					{
						for (const std::pair<LLUUID, S32> playpair : avatarp->mPlayingAnimations)
						{
							avatarp->stopMotion(playpair.first, TRUE);
							avatarp->startMotion(playpair.first);
						}
					}
				}
			}
			return true;
		}
		else if (cmd == utf8str_tolower(sAOCommand))
		{
			std::string subcmd;
			if (input >> subcmd)
			{
				if (subcmd == "on")
				{
					gSavedPerAccountSettings.setBOOL("AlchemyAOEnable", TRUE);
					return true;
				}
				else if (subcmd == "off")
				{
					gSavedPerAccountSettings.setBOOL("AlchemyAOEnable", FALSE);
					return true;
				}
				else if (subcmd == "sit")
				{
					auto ao_set = ALAOEngine::instance().getSetByName(ALAOEngine::instance().getCurrentSetName());
					if (input >> subcmd)
					{
						if (subcmd == "on")
						{
							ALAOEngine::instance().setOverrideSits(ao_set, true);

						}
						else if (subcmd == "off")
						{
							ALAOEngine::instance().setOverrideSits(ao_set, false);
						}
					}
					else
					{
						ALAOEngine::instance().setOverrideSits(ao_set, !ao_set->getSitOverride());
					}
					return true;
				}
			}
		}
	}
	return false;
}
