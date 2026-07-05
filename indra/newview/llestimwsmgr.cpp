/**
 * @file llestimwsmgr.cpp
 * @brief Native e-stim WebSocket manager and server implementation
 */

#include "llviewerprecompiledheaders.h"
#include "llestimwsmgr.h"
#include "rlvcommon.h"
#include "llwebsocketmgr.h"
#include "llviewercontrol.h"
#include "llnotificationsutil.h"
#ifdef LL_FMODSTUDIO
#include "llaudioengine_fmodstudio.h"
#endif
#include "llerror.h"
#include "lluictrl.h"
#include <cmath>
#include <algorithm>

//========================================================================
// LLEstimWSConnection
//========================================================================

void LLEstimWSConnection::onOpen()
{
    LLJSONRPCConnection::onOpen();
    LL_INFOS("EstimWS") << "E-Stim companion daemon connection opened" << LL_ENDL;
}

void LLEstimWSConnection::onClose()
{
    LLJSONRPCConnection::onClose();
    LL_INFOS("EstimWS") << "E-Stim companion daemon connection closed" << LL_ENDL;
}

//========================================================================
// LLEstimWSServer
//========================================================================

LLEstimWSServer::LLEstimWSServer(const std::string& name, U16 port, bool local_only)
    : LLJSONRPCServer(name, port, local_only)
{
    LL_INFOS("EstimWS") << "Created E-Stim WebSocket server on port " << port << LL_ENDL;
}

LLEstimWSServer::ptr_t LLEstimWSServer::getInstance()
{
    if (!LLWebsocketMgr::instanceExists())
    {
        return nullptr;
    }
    LLWebsocketMgr& wsmgr = LLWebsocketMgr::instance();
    return std::static_pointer_cast<LLEstimWSServer>(
        wsmgr.findServerByName(LLEstimWSServer::DEFAULT_SERVER_NAME));
}

void LLEstimWSServer::onStarted()
{
    LL_INFOS("EstimWS") << "E-Stim WebSocket server started" << LL_ENDL;
    LLUICtrl::CommitCallbackRegistry::currentRegistrar().add("Estim.Panic",
        boost::bind(&LLEstimWSServer::panicStop, this));
}

void LLEstimWSServer::onStopped()
{
    LL_INFOS("EstimWS") << "E-Stim WebSocket server stopped" << LL_ENDL;
#ifdef LL_FMODSTUDIO
    LLAudioEngine_FMODSTUDIO::sFFTCallback = nullptr;
#endif
}

void LLEstimWSServer::onConnectionOpened(const LLWebsocketMgr::WSConnection::ptr_t& connection)
{
    LLJSONRPCServer::onConnectionOpened(connection);
    mActiveConnection = std::dynamic_pointer_cast<LLEstimWSConnection>(connection);
    mConnected = true;
    mSafetyCutoff = false;
    mNotifiedLoadDisconnect = false;
    mNotifiedLowBattery = false;
    LL_INFOS("EstimWS") << "E-Stim daemon connection active" << LL_ENDL;
}

void LLEstimWSServer::onConnectionClosed(const LLWebsocketMgr::WSConnection::ptr_t& connection)
{
    LLJSONRPCServer::onConnectionClosed(connection);
    mActiveConnection.reset();
    mConnected = false;
    mSafetyCutoff = true;
    panicStop();

    LL_WARNS("EstimWS") << "E-Stim daemon connection lost! Triggering safety cutoff." << LL_ENDL;
    LLNotificationsUtil::add("EstimLoadDisconnect");
}

LLWebsocketMgr::WSConnection::ptr_t LLEstimWSServer::connectionFactory(LLWebsocketMgr::WSServer::ptr_t server,
                                                                      LLWebsocketMgr::connection_h handle)
{
    auto connection = std::make_shared<LLEstimWSConnection>(server, handle);
    setupConnectionMethods(connection);
    return connection;
}

void LLEstimWSServer::setupConnectionMethods(LLJSONRPCConnection::ptr_t connection)
{
    LLJSONRPCServer::setupConnectionMethods(connection);

    auto estim_conn = std::dynamic_pointer_cast<LLEstimWSConnection>(connection);
    if (estim_conn)
    {
        std::weak_ptr<LLEstimWSServer> that = std::static_pointer_cast<LLEstimWSServer>(LLJSONRPCServer::shared_from_this());

        estim_conn->registerMethod("sensor_update",
            [that](const std::string&, const LLSD&, const LLSD& params) -> LLSD
            {
                if (auto server = that.lock())
                {
                    return server->handleSensorUpdate(params);
                }
                return LLSD();
            });

        estim_conn->registerMethod("telemetry",
            [that](const std::string&, const LLSD&, const LLSD& params) -> LLSD
            {
                if (auto server = that.lock())
                {
                    return server->handleTelemetry(params);
                }
                return LLSD();
            });

        estim_conn->registerMethod("get_triggers",
            [that](const std::string&, const LLSD&, const LLSD& params) -> LLSD
            {
                if (auto server = that.lock())
                {
                    LLSD list = LLSD::emptyArray();
                    for (const auto& rule : server->getTriggers())
                    {
                        LLSD r = LLSD::emptyMap();
                        r["sensor"] = rule.sensor;
                        r["axis"] = rule.axis;
                        r["operator"] = rule.op;
                        r["threshold"] = (F32)rule.threshold;
                        r["rule_action"] = rule.action;
                        list.append(r);
                    }
                    return list;
                }
                return LLSD();
            });
    }
}

LLSD LLEstimWSServer::handleSensorUpdate(const LLSD& params)
{
    std::string sensor = params["sensor"].asString();
    std::string axis = params["axis"].asString();
    F32 value = (F32)params["value"].asReal();

    bool is_new = (mSensorValues.find(sensor) == mSensorValues.end() ||
                   mSensorValues[sensor].find(axis) == mSensorValues[sensor].end());
    F32 old_value = is_new ? -999999.0f : mSensorValues[sensor][axis];
    mSensorValues[sensor][axis] = value;

    evaluateSensorInput(sensor, axis, value);

    if (is_new || value != old_value)
    {
        notifyLslSubscribers(sensor, axis, value);
    }

    broadcastNotification("sensor_update", params);

    LLSD response = LLSD::emptyMap();
    response["status"] = "success";
    return response;
}

LLSD LLEstimWSServer::handleTelemetry(const LLSD& params)
{
    if (params.has("battery"))
    {
        mCoyoteBattery = (F32)params["battery"].asReal();
    }
    else if (params.has("battery_coyote"))
    {
        mCoyoteBattery = (F32)params["battery_coyote"].asReal();
    }

    if (params.has("load_a"))
    {
        mLoadA = params["load_a"].asBoolean();
    }
    if (params.has("load_b"))
    {
        mLoadB = params["load_b"].asBoolean();
    }

    // Safety cutoff check:
    // Temporarily disabled due to dodgy detection
    /*
    if (!mLoadA || !mLoadB)
    {
        if (!mSafetyCutoff)
        {
            mSafetyCutoff = true;
            panicStop();
            if (!mNotifiedLoadDisconnect)
            {
                LLNotificationsUtil::add("EstimLoadDisconnect");
                mNotifiedLoadDisconnect = true;
            }
        }
    }
    else
    {
        mSafetyCutoff = false;
        mNotifiedLoadDisconnect = false;
    }
    */

    // Battery low check:
    if (mCoyoteBattery < 10.0f)
    {
        if (!mNotifiedLowBattery)
        {
            LLNotificationsUtil::add("EstimLowBattery");
            mNotifiedLowBattery = true;
        }
    }
    else
    {
        mNotifiedLowBattery = false;
    }

    broadcastNotification("telemetry", params);

    LLSD response = LLSD::emptyMap();
    response["status"] = "success";
    return response;
}

void LLEstimWSServer::sendStimCommand(const std::string& channel, U32 intensity, U32 frequency, U32 pattern)
{
    U32 max_cap = gSavedSettings.getU32("EstimMaxIntensityCap");
    U32 channel_cap = (channel == "b") ? mMaxIntensityB : mMaxIntensityA;
    U32 capped_intensity = std::min(intensity, std::min(max_cap, channel_cap));

    if (mSafetyCutoff)
    {
        capped_intensity = 0;
    }

    if (channel == "a")
    {
        mChannelAIntensity = capped_intensity;
    }
    else if (channel == "b")
    {
        mChannelBIntensity = capped_intensity;
    }

    if (mActiveConnection && mActiveConnection->isConnected())
    {
        LLSD params = LLSD::emptyMap();
        params["command"] = "stimulate";
        params["channel"] = channel;
        params["intensity"] = (S32)capped_intensity;
        if (frequency > 0)
        {
            params["frequency"] = (S32)frequency;
        }
        if (pattern > 0)
        {
            params["pattern"] = (S32)pattern;
        }
        mActiveConnection->notify("stimulate", params);
    }
}

void LLEstimWSServer::sendShockCommand(const std::string& channel, U32 intensity, U32 frequency, U32 duration_ms)
{
    U32 max_cap = gSavedSettings.getU32("EstimMaxIntensityCap");
    U32 channel_cap = (channel == "b") ? mMaxIntensityB : mMaxIntensityA;
    U32 capped_intensity = std::min(intensity, std::min(max_cap, channel_cap));

    if (mSafetyCutoff)
    {
        capped_intensity = 0;
    }

    if (mActiveConnection && mActiveConnection->isConnected())
    {
        LLSD params = LLSD::emptyMap();
        params["command"] = "shock";
        params["channel"] = channel;
        params["intensity"] = (S32)capped_intensity;
        params["frequency"] = (S32)frequency;
        params["duration"] = (S32)duration_ms;
        mActiveConnection->notify("shock", params);
    }
}

void LLEstimWSServer::sendPulseConfig(const std::string& channel, U32 pulse_width, const std::string& waveform)
{
    if (mActiveConnection && mActiveConnection->isConnected())
    {
        LLSD params = LLSD::emptyMap();
        params["command"] = "pulse_config";
        params["channel"] = channel;
        params["pulse_width"] = (S32)pulse_width;
        params["waveform"] = waveform;
        mActiveConnection->notify("pulse_config", params);
    }
}

void LLEstimWSServer::sendSensorMode(const std::string& sensor, const std::string& mode)
{
    if (mActiveConnection && mActiveConnection->isConnected())
    {
        LLSD params = LLSD::emptyMap();
        params["command"] = "sensor_mode";
        params["sensor"] = sensor;
        params["mode"] = mode;
        mActiveConnection->notify("sensor_mode", params);
    }
}

void LLEstimWSServer::panicStop()
{
    mChannelAIntensity = 0;
    mChannelBIntensity = 0;

    if (mActiveConnection && mActiveConnection->isConnected())
    {
        LLSD params = LLSD::emptyMap();
        params["command"] = "stop";
        mActiveConnection->notify("stop", params);
    }
}

void LLEstimWSServer::registerTrigger(const EstimTriggerRule& rule)
{
    mTriggerRules.push_back(rule);

    LLSD params = LLSD::emptyMap();
    params["action"] = "add";
    params["sensor"] = rule.sensor;
    params["axis"] = rule.axis;
    params["operator"] = rule.op;
    params["threshold"] = (F32)rule.threshold;
    params["rule_action"] = rule.action;
    broadcastNotification("trigger_update", params);
}

void LLEstimWSServer::clearTriggers()
{
    mTriggerRules.clear();

    LLSD params = LLSD::emptyMap();
    params["action"] = "clear";
    broadcastNotification("trigger_update", params);
}

void LLEstimWSServer::evaluateSensorInput(const std::string& sensor, const std::string& axis, F32 value)
{
    for (const auto& rule : mTriggerRules)
    {
        if (rule.sensor == sensor && rule.axis == axis)
        {
            bool condition_met = false;
            if (rule.op == ">")
            {
                condition_met = (value > rule.threshold);
            }
            else if (rule.op == "<")
            {
                condition_met = (value < rule.threshold);
            }
            else if (rule.op == "==")
            {
                condition_met = (std::abs(value - rule.threshold) < 0.001f);
            }

            if (condition_met)
            {
                if (!rule.last_state)
                {
                    rule.last_state = true;

                    std::vector<std::string> parts;
                    size_t pos = 0;
                    std::string token;
                    std::string s = rule.action;
                    while ((pos = s.find(':')) != std::string::npos)
                    {
                        token = s.substr(0, pos);
                        parts.push_back(token);
                        s.erase(0, pos + 1);
                    }
                    parts.push_back(s);

                    if (parts.size() >= 3)
                    {
                        std::string act = parts[0];
                        if (act == "intensity")
                        {
                            std::string chan = parts[1];
                            U32 val = std::stoul(parts[2]);
                            sendStimCommand(chan, val);
                        }
                        else if (act == "freq")
                        {
                            std::string chan = parts[1];
                            U32 val = std::stoul(parts[2]);
                            sendStimCommand(chan, 0, val);
                        }
                        else if (act == "pattern")
                        {
                            std::string chan = parts[1];
                            U32 val = std::stoul(parts[2]);
                            sendStimCommand(chan, 0, 0, val);
                        }
                        else if (act == "lsl")
                        {
                            std::string reply_chan = parts[1];
                            std::string message = parts[2];
                            RlvUtil::sendChatReply(reply_chan, message);
                        }
                    }
                }
            }
            else
            {
                rule.last_state = false;
            }
        }
    }
}

F32 LLEstimWSServer::getSensorValue(const std::string& sensor, const std::string& axis) const
{
    auto it_s = mSensorValues.find(sensor);
    if (it_s != mSensorValues.end())
    {
        auto it_a = it_s->second.find(axis);
        if (it_a != it_s->second.end())
        {
            return it_a->second;
        }
    }
    return 0.0f;
}

void LLEstimWSServer::registerNotification(const std::string& sensor, const std::string& axis, S32 channel, const LLUUID& object_id)
{
    for (const auto& rule : mNotifications)
    {
        if (rule.sensor == sensor && rule.axis == axis && rule.channel == channel && rule.object_id == object_id)
        {
            return;
        }
    }
    mNotifications.push_back({sensor, axis, channel, object_id});
}

void LLEstimWSServer::removeNotification(const std::string& sensor, const std::string& axis, S32 channel, const LLUUID& object_id)
{
    mNotifications.erase(
        std::remove_if(mNotifications.begin(), mNotifications.end(),
            [&](const EstimNotificationRule& rule) {
                return rule.sensor == sensor && rule.axis == axis && rule.channel == channel && rule.object_id == object_id;
            }),
        mNotifications.end());
}

void LLEstimWSServer::clearNotificationsForObject(const LLUUID& object_id)
{
    mNotifications.erase(
        std::remove_if(mNotifications.begin(), mNotifications.end(),
            [&](const EstimNotificationRule& rule) {
                return rule.object_id == object_id;
            }),
        mNotifications.end());
}

void LLEstimWSServer::notifyLslSubscribers(const std::string& sensor, const std::string& axis, F32 value)
{
    for (const auto& rule : mNotifications)
    {
        if (rule.sensor == sensor && rule.axis == axis)
        {
            std::string val_str;
            if (axis == "button")
            {
                val_str = std::to_string((int)value);
            }
            else
            {
                val_str = llformat("%.6f", value);
            }
            std::string reply = sensor + ":" + axis + "=" + val_str;
            RlvUtil::sendChatReply(rule.channel, reply);
        }
    }
}

void LLEstimWSServer::setAudioSyncEnabled(bool enabled)
{
    mAudioSyncEnabled = enabled;
#ifdef LL_FMODSTUDIO
    if (enabled)
    {
        LLAudioEngine_FMODSTUDIO::sFFTCallback = &LLEstimWSServer::processFFTData;
    }
    else
    {
        if (LLAudioEngine_FMODSTUDIO::sFFTCallback == &LLEstimWSServer::processFFTData)
        {
            LLAudioEngine_FMODSTUDIO::sFFTCallback = nullptr;
        }
    }
#endif
}

void LLEstimWSServer::processFFTData(const float* spectrum_data, int length)
{
    if (getInstance() && getInstance()->isAudioSyncEnabled())
    {
        getInstance()->handleFFT(spectrum_data, length);
    }
}

void LLEstimWSServer::handleFFT(const float* spectrum_data, int length)
{
    static LLTimer throttle_timer;
    if (throttle_timer.getElapsedTimeF32() < 0.050f)
    {
        return;
    }
    throttle_timer.reset();

    float bass = 0.0f;
    int bass_bins = std::min(10, length);
    for (int i = 0; i < bass_bins; ++i)
    {
        bass += spectrum_data[i];
    }
    if (bass_bins > 0) bass /= bass_bins;

    float mid_high = 0.0f;
    int mid_high_start = bass_bins;
    int mid_high_end = std::min(length / 2, length);
    int mid_high_bins = mid_high_end - mid_high_start;
    for (int i = mid_high_start; i < mid_high_end; ++i)
    {
        mid_high += spectrum_data[i];
    }
    if (mid_high_bins > 0) mid_high /= mid_high_bins;

    U32 intensity_a = static_cast<U32>(bass * 1000.0f);
    intensity_a = std::min(intensity_a, (U32)255);

    U32 freq_b = 10 + static_cast<U32>(mid_high * 1000.0f);
    freq_b = std::min(std::max(freq_b, (U32)10), (U32)150);

    sendStimCommand("a", intensity_a);
    sendStimCommand("b", mChannelBIntensity, freq_b);
}

void LLEstimWSServer::testChannelA()
{
    sendStimCommand("a", std::min(10u, mMaxIntensityA), 50, 0);
}

void LLEstimWSServer::testChannelB()
{
    sendStimCommand("b", std::min(10u, mMaxIntensityB), 50, 0);
}
