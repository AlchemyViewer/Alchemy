/**
* @file alchatcommand.cpp
* @brief ALChatCommand implementation for chat input commands
*
* $LicenseInfo:firstyear=2013&license=viewerlgpl$
* Copyright (C) Rye Mutt <rye@alchemyviewer.org>
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
#include "aoengine.h"
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
#include "llviewernetwork.h"
#include "llviewerobjectlist.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
#include "llvoavatarself.h"
#include "llvolume.h"
#include "llvolumemessage.h"

#include <iterator>

namespace
{
constexpr char COMMAND_AO[] = "AlchemyChatCommandAnimationOverride";
constexpr char COMMAND_CALC[] = "AlchemyChatCommandCalc";
constexpr char COMMAND_CLEAR[] = "AlchemyChatCommandClearNearby";
constexpr char COMMAND_DRAW_DISTANCE[] = "AlchemyChatCommandDrawDistance";
constexpr char COMMAND_ENABLE[] = "AlchemyChatCommandEnable";
constexpr char COMMAND_GROUND[] = "AlchemyChatCommandGround";
constexpr char COMMAND_HEIGHT[] = "AlchemyChatCommandHeight";
constexpr char COMMAND_HOME[] = "AlchemyChatCommandHome";
constexpr char COMMAND_HOVER_HEIGHT[] = "AlchemyChatCommandHoverHeight";
constexpr char COMMAND_MAP_TO[] = "AlchemyChatCommandMapto";
constexpr char COMMAND_POSITION[] = "AlchemyChatCommandPos";
constexpr char COMMAND_REGION_MESSAGE[] = "AlchemyChatCommandRegionMessage";
constexpr char COMMAND_RESYNC_ANIMATIONS[] = "AlchemyChatCommandResyncAnim";
constexpr char COMMAND_REZ_PLATFORM[] = "AlchemyChatCommandRezPlat";
constexpr char COMMAND_SET_CHAT_CHANNEL[] = "AlchemyChatCommandSetChatChannel";
constexpr char COMMAND_SET_HOME[] = "AlchemyChatCommandSetHome";
constexpr char COMMAND_TELEPORT_TO_CAMERA[] = "AlchemyChatCommandTeleportToCam";
constexpr char DICE_ROLL_TRIGGER[] = "/droll";
constexpr char SEND_MENU_TRIGGER[] = "/sendmenu";

struct ConfiguredCommand
{
    const char* setting;
    const char* description;
    bool accepts_arguments;
};

constexpr ConfiguredCommand CONFIGURED_COMMANDS[] = {
    { COMMAND_AO, "Animation override: on|off|sit [on|off]", true },
    { COMMAND_CALC, "Calculate: expression", true },
    { COMMAND_CLEAR, "Clear nearby chat", false },
    { COMMAND_DRAW_DISTANCE, "Set draw distance: meters", true },
    { COMMAND_GROUND, "Teleport to ground", false },
    { COMMAND_HEIGHT, "Teleport to height: meters", true },
    { COMMAND_HOME, "Teleport home", false },
    { COMMAND_HOVER_HEIGHT, "Set hover height: meters", true },
    { COMMAND_MAP_TO, "Teleport to region: name", true },
    { COMMAND_POSITION, "Teleport to position: x y z", true },
    { COMMAND_REGION_MESSAGE, "Send region message: message", true },
    { COMMAND_RESYNC_ANIMATIONS, "Resync animations", false },
    { COMMAND_REZ_PLATFORM, "Rez a platform: [size]", true },
    { COMMAND_SET_CHAT_CHANNEL, "Set nearby chat channel: channel", true },
    { COMMAND_SET_HOME, "Set home location", false },
    { COMMAND_TELEPORT_TO_CAMERA, "Teleport to camera", false },
};
}

std::vector<ALChatCommand::AutocompleteCommand> ALChatCommand::getAutocompleteCommands()
{
    if (!gSavedSettings.getBOOL(COMMAND_ENABLE))
    {
        return {};
    }

    std::vector<AutocompleteCommand> commands;
    commands.reserve(std::size(CONFIGURED_COMMANDS) + 2);
    for (const ConfiguredCommand& command : CONFIGURED_COMMANDS)
    {
        commands.push_back({
            gSavedSettings.getString(command.setting),
            command.description,
            command.accepts_arguments });
    }
    commands.push_back({ DICE_ROLL_TRIGGER, "Roll a die: [sides]", true });
    commands.push_back({ SEND_MENU_TRIGGER, "Reply to a dialog menu: channel button", true });
    return commands;
}

bool ALChatCommand::parseCommand(std::string data)
{
    static LLCachedControl<bool> enableChatCmd(gSavedSettings, COMMAND_ENABLE, true);
    if (enableChatCmd)
    {
        utf8str_tolower(data);
        std::istringstream input(data);
        std::string cmd;

        if (!(input >> cmd))    return false;

        static LLCachedControl<std::string> sDrawDistanceCommand(gSavedSettings, COMMAND_DRAW_DISTANCE, "/dd");
        static LLCachedControl<std::string> sHeightCommand(gSavedSettings, COMMAND_HEIGHT, "/gth");
        static LLCachedControl<std::string> sGroundCommand(gSavedSettings, COMMAND_GROUND, "/flr");
        static LLCachedControl<std::string> sPosCommand(gSavedSettings, COMMAND_POSITION, "/pos");
        static LLCachedControl<std::string> sRezPlatCommand(gSavedSettings, COMMAND_REZ_PLATFORM, "/plat");
        static LLCachedControl<std::string> sHomeCommand(gSavedSettings, COMMAND_HOME, "/home");
        static LLCachedControl<std::string> sSetHomeCommand(gSavedSettings, COMMAND_SET_HOME, "/sethome");
        static LLCachedControl<std::string> sCalcCommand(gSavedSettings, COMMAND_CALC, "/calc");
        static LLCachedControl<std::string> sMaptoCommand(gSavedSettings, COMMAND_MAP_TO, "/mapto");
        static LLCachedControl<std::string> sClearCommand(gSavedSettings, COMMAND_CLEAR, "/clr");
        static LLCachedControl<std::string> sRegionMsgCommand(gSavedSettings, COMMAND_REGION_MESSAGE, "/regionmsg");
        static LLCachedControl<std::string> sSetNearbyChatChannelCmd(gSavedSettings, COMMAND_SET_CHAT_CHANNEL, "/setchannel");
        static LLCachedControl<std::string> sResyncAnimCommand(gSavedSettings, COMMAND_RESYNC_ANIMATIONS, "/resync");
        static LLCachedControl<std::string> sTeleportToCam(gSavedSettings, COMMAND_TELEPORT_TO_CAMERA, "/tp2cam");
        static LLCachedControl<std::string> sHoverHeight(gSavedSettings, COMMAND_HOVER_HEIGHT, "/hover");
        static LLCachedControl<std::string> sAOCommand(gSavedSettings, COMMAND_AO, "/ao");

        if (cmd == utf8str_tolower(sDrawDistanceCommand()))  // dd
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
        else if (cmd == utf8str_tolower(sHeightCommand()))  // gth
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
        else if (cmd == utf8str_tolower(sGroundCommand()))  // flr
        {
            LLVector3d pos_global = gAgent.getPositionGlobal();
            pos_global.mdV[VZ] = 0.0;
            gAgent.teleportViaLocation(pos_global);
            return true;
        }
        else if (cmd == utf8str_tolower(sPosCommand()))  // pos
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
        else if (cmd == utf8str_tolower(sRezPlatCommand()))  // plat
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
            LLUUID group_id = gAgent.getGroupForRezzing();
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
            msg->addU8Fast(_PREHASH_BypassRaycast, true);
            msg->addU8Fast(_PREHASH_RayEndIsIntersection, false);
            msg->addU8Fast(_PREHASH_State, false);
            msg->sendReliable(gAgent.getRegionHost());

            return true;
        }
        else if (cmd == utf8str_tolower(sHomeCommand()))  // home
        {
            gAgent.teleportHome();
            return true;
        }
        else if (cmd == utf8str_tolower(sSetHomeCommand()))  // sethome
        {
            gAgent.setStartPosition(START_LOCATION_ID_HOME);
            return true;
        }
        else if (cmd == utf8str_tolower(sCalcCommand()))  // calc
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
        else if (cmd == utf8str_tolower(sMaptoCommand()))  // mapto
        {
            const std::string::size_type length = cmd.length() + 1;
            if (data.length() > length)
            {
                const LLVector3d& pos = gAgent.getPositionGlobal();
                LLSD params;
                params.append(data.substr(length));
                params.append(fmodf(static_cast<F32>(pos.mdV[VX]), REGION_WIDTH_METERS));
                params.append(fmodf(static_cast<F32>(pos.mdV[VY]), REGION_WIDTH_METERS));
                params.append(static_cast<F32>(pos.mdV[VZ]));
                LLCommandDispatcher::dispatch("teleport", params, LLSD(), LLGridManager::getInstance()->getGrid(), nullptr, "clicked", true);
                return true;
            }
        }
        else if (cmd == utf8str_tolower(sClearCommand()))
        {
            LLFloaterIMNearbyChat* nearby_chat = LLFloaterReg::findTypedInstance<LLFloaterIMNearbyChat>("nearby_chat");
            if (nearby_chat)
            {
                nearby_chat->reloadMessages(true);
            }
            return true;
        }
        else if (cmd == DICE_ROLL_TRIGGER)
        {
            S32 dice_sides;
            if (!(input >> dice_sides))
                dice_sides = 6;
            LLSD args;
            args["RESULT"] = (ll_rand(dice_sides) + 1);
            LLNotificationsUtil::add("ChatCommandDiceRoll", args);
            return true;
        }
        else if (cmd == utf8str_tolower(sRegionMsgCommand())) // Region Message / Dialog
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
        else if (cmd == utf8str_tolower(sSetNearbyChatChannelCmd()))  // Set nearby chat channel
        {
            S32 chan;
            if (input >> chan)
            {
                gSavedSettings.setS32("AlchemyNearbyChatChannel", chan);
                return true;
            }
        }
        else if (cmd == utf8str_tolower(sTeleportToCam()))
        {
            gAgent.teleportViaLocation(gAgentCamera.getCameraPositionGlobal());
            return true;
        }
        else if (cmd == utf8str_tolower(sHoverHeight()))  // Hover height
        {
            F32 height;
            if (input >> height)
            {
                gSavedPerAccountSettings.set("AvatarHoverOffsetZ",
                                             llclamp<F32>(height, MIN_HOVER_Z, MAX_HOVER_Z));
                return true;
            }
        }
        else if (cmd == utf8str_tolower(sResyncAnimCommand()))  // Resync Animations
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
                            avatarp->stopMotion(playpair.first, true);
                            avatarp->startMotion(playpair.first);
                        }
                    }
                }
            }
            return true;
        }
        else if (cmd == utf8str_tolower(sAOCommand()))
        {
            std::string subcmd;
            if (input >> subcmd)
            {
                if (subcmd == "on")
                {
                    gSavedPerAccountSettings.setBOOL("AlchemyAOEnable", true);
                    return true;
                }
                else if (subcmd == "off")
                {
                    gSavedPerAccountSettings.setBOOL("AlchemyAOEnable", false);
                    return true;
                }
                else if (subcmd == "sit")
                {
                    auto ao_set = AOEngine::instance().getSetByName(AOEngine::instance().getCurrentSetName());
                    if (input >> subcmd)
                    {
                        if (subcmd == "on")
                        {
                            AOEngine::instance().setOverrideSits(ao_set, true);

                        }
                        else if (subcmd == "off")
                        {
                            AOEngine::instance().setOverrideSits(ao_set, false);
                        }
                    }
                    else
                    {
                        AOEngine::instance().setOverrideSits(ao_set, !ao_set->getSitOverride());
                    }
                    return true;
                }
            }
        }
        else if (cmd == SEND_MENU_TRIGGER)
        {
            S32 channel;
            if (!(input >> channel))
                return false;
            std::string button;
            if (!(input >> button))
                return false;
            LLMessageSystem* msg = gMessageSystem;
            msg->newMessageFast(_PREHASH_ScriptDialogReply);
            msg->nextBlockFast(_PREHASH_AgentData);
            msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
            msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
            msg->nextBlockFast(_PREHASH_Data);
            msg->addUUIDFast(_PREHASH_ObjectID, gAgent.getID());
            msg->addS32(_PREHASH_ChatChannel, channel);
            msg->addS32Fast(_PREHASH_ButtonIndex, 0);
            msg->addStringFast(_PREHASH_ButtonLabel, button);
            gAgent.sendReliableMessage();
            return true;
        }
    }
    return false;
}
