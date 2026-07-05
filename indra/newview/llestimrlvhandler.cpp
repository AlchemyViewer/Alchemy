/**
 * @file llestimrlvhandler.cpp
 * @brief RLV handler implementation for native e-stim commands
 */

#include "llviewerprecompiledheaders.h"
#include "llestimrlvhandler.h"
#include "llestimwsmgr.h"
#include "llerror.h"
#include "rlvhelper.h"
#include <vector>
#include <string>

bool LLEstimRLVHandler::onForceCommand(const RlvCommand& rlvCmd, ERlvCmdRet& cmdRet)
{
    const std::string& behaviour = rlvCmd.getBehaviour();

    if (behaviour.rfind("estim_", 0) != 0)
    {
        return false;
    }

    auto server = LLEstimWSServer::getInstance();
    if (!server)
    {
        LL_WARNS("EstimRLV") << "E-Stim WebSocket server instance not available" << LL_ENDL;
        cmdRet = RLV_RET_FAILED;
        return true;
    }

    if (behaviour == "estim_shock")
    {
        std::string opt = rlvCmd.getOption();
        std::vector<std::string> parts;
        size_t pos = 0;
        std::string token;
        while ((pos = opt.find(':')) != std::string::npos)
        {
            token = opt.substr(0, pos);
            parts.push_back(token);
            opt.erase(0, pos + 1);
        }
        parts.push_back(opt);

        if (parts.size() >= 4)
        {
            std::string channel = parts[0];
            U32 intensity = std::stoul(parts[1]);
            U32 freq = std::stoul(parts[2]);
            U32 duration = std::stoul(parts[3]);
            server->sendShockCommand(channel, intensity, freq, duration);
            cmdRet = RLV_RET_SUCCESS;
            return true;
        }
    }
    else if (behaviour == "estim_intensity")
    {
        std::string opt = rlvCmd.getOption();
        size_t pos = opt.find(':');
        if (pos != std::string::npos)
        {
            std::string channel = opt.substr(0, pos);
            U32 intensity = std::stoul(opt.substr(pos + 1));
            server->sendStimCommand(channel, intensity);
            cmdRet = RLV_RET_SUCCESS;
            return true;
        }
    }
    else if (behaviour == "estim_freq")
    {
        std::string opt = rlvCmd.getOption();
        size_t pos = opt.find(':');
        if (pos != std::string::npos)
        {
            std::string channel = opt.substr(0, pos);
            U32 freq = std::stoul(opt.substr(pos + 1));
            server->sendStimCommand(channel, 0, freq);
            cmdRet = RLV_RET_SUCCESS;
            return true;
        }
    }
    else if (behaviour == "estim_pulse_width")
    {
        std::string opt = rlvCmd.getOption();
        size_t pos = opt.find(':');
        if (pos != std::string::npos)
        {
            std::string channel = opt.substr(0, pos);
            U32 pw = std::stoul(opt.substr(pos + 1));
            server->sendPulseConfig(channel, pw, "");
            cmdRet = RLV_RET_SUCCESS;
            return true;
        }
    }
    else if (behaviour == "estim_waveform")
    {
        std::string opt = rlvCmd.getOption();
        size_t pos = opt.find(':');
        if (pos != std::string::npos)
        {
            std::string channel = opt.substr(0, pos);
            std::string wf = opt.substr(pos + 1);
            server->sendPulseConfig(channel, 0, wf);
            cmdRet = RLV_RET_SUCCESS;
            return true;
        }
    }
    else if (behaviour == "estim_pattern")
    {
        std::string opt = rlvCmd.getOption();
        size_t pos = opt.find(':');
        if (pos != std::string::npos)
        {
            std::string channel = opt.substr(0, pos);
            U32 pat = std::stoul(opt.substr(pos + 1));
            server->sendStimCommand(channel, 0, 0, pat);
            cmdRet = RLV_RET_SUCCESS;
            return true;
        }
    }
    else if (behaviour == "estim_sensor_mode")
    {
        std::string opt = rlvCmd.getOption();
        size_t pos = opt.find(':');
        if (pos != std::string::npos)
        {
            std::string sensor = opt.substr(0, pos);
            std::string mode = opt.substr(pos + 1);
            server->sendSensorMode(sensor, mode);
            cmdRet = RLV_RET_SUCCESS;
            return true;
        }
    }
    else if (behaviour == "estim_audio_sync")
    {
        std::string opt = rlvCmd.getOption();
        bool enable = (opt == "on" || opt == "1" || opt == "true");
        server->setAudioSyncEnabled(enable);
        cmdRet = RLV_RET_SUCCESS;
        return true;
    }
    else if (behaviour == "estim_stop")
    {
        server->panicStop();
        cmdRet = RLV_RET_SUCCESS;
        return true;
    }
    else if (behaviour == "estim_trigger")
    {
        std::string opt = rlvCmd.getOption();
        std::vector<std::string> parts;
        size_t pos = 0;
        std::string token;
        while ((pos = opt.find(':')) != std::string::npos)
        {
            token = opt.substr(0, pos);
            parts.push_back(token);
            opt.erase(0, pos + 1);
        }
        parts.push_back(opt);

        if (!parts.empty() && parts[0] == "clear")
        {
            server->clearTriggers();
            cmdRet = RLV_RET_SUCCESS;
            return true;
        }
        else if (parts.size() >= 8 && parts[0] == "add")
        {
            EstimTriggerRule rule;
            rule.sensor = parts[1];
            rule.axis = parts[2];
            rule.op = parts[3];
            rule.threshold = std::stof(parts[4]);
            rule.action = parts[5] + ":" + parts[6] + ":" + parts[7];
            server->registerTrigger(rule);
            cmdRet = RLV_RET_SUCCESS;
            return true;
        }
    }

    cmdRet = RLV_RET_FAILED_UNKNOWN;
    return true;
}

bool LLEstimRLVHandler::onAddRemCommand(const RlvCommand& rlvCmd, ERlvCmdRet& cmdRet)
{
    const std::string& behaviour = rlvCmd.getBehaviour();

    if (behaviour.rfind("estim_", 0) != 0)
    {
        return false;
    }

    auto server = LLEstimWSServer::getInstance();
    if (!server)
    {
        cmdRet = RLV_RET_FAILED;
        return true;
    }

    if (behaviour == "estim_notify")
    {
        std::string opt = rlvCmd.getOption();
        std::vector<std::string> parts;
        size_t pos = 0;
        std::string token;
        while ((pos = opt.find(':')) != std::string::npos)
        {
            token = opt.substr(0, pos);
            parts.push_back(token);
            opt.erase(0, pos + 1);
        }
        parts.push_back(opt);

        if (parts.size() == 3)
        {
            std::string sensor = parts[0];
            std::string axis = parts[1];
            S32 channel = 0;
            if (LLStringUtil::convertToS32(parts[2], channel))
            {
                if (rlvCmd.getParamType() == RLV_TYPE_ADD)
                {
                    server->registerNotification(sensor, axis, channel, rlvCmd.getObjectID());
                }
                else if (rlvCmd.getParamType() == RLV_TYPE_REMOVE)
                {
                    server->removeNotification(sensor, axis, channel, rlvCmd.getObjectID());
                }
                cmdRet = RLV_RET_SUCCESS;
                return true;
            }
        }
    }

    cmdRet = RLV_RET_SUCCESS;
    return true;
}

bool LLEstimRLVHandler::onReplyCommand(const RlvCommand& rlvCmd, ERlvCmdRet& cmdRet)
{
    const std::string& behaviour = rlvCmd.getBehaviour();

    if (behaviour != "estim_getval")
    {
        return false;
    }

    auto server = LLEstimWSServer::getInstance();
    if (!server)
    {
        cmdRet = RLV_RET_FAILED;
        return true;
    }

    std::string opt = rlvCmd.getOption();
    size_t pos = opt.find(':');
    if (pos != std::string::npos)
    {
        std::string sensor = opt.substr(0, pos);
        std::string axis = opt.substr(pos + 1);
        F32 value = server->getSensorValue(sensor, axis);

        std::string reply;
        if (axis == "button")
        {
            reply = std::to_string((int)value);
        }
        else
        {
            reply = llformat("%.6f", value);
        }

        RlvUtil::sendChatReply(rlvCmd.getParam(), reply);
        cmdRet = RLV_RET_SUCCESS;
        return true;
    }

    cmdRet = RLV_RET_FAILED_PARAM;
    return true;
}

bool LLEstimRLVHandler::onClearCommand(const RlvCommand& rlvCmd, ERlvCmdRet& cmdRet)
{
    auto server = LLEstimWSServer::getInstance();
    if (server)
    {
        server->clearNotificationsForObject(rlvCmd.getObjectID());
    }
    cmdRet = RLV_RET_SUCCESS;
    return true;
}
