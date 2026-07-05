/**
 * @file llestimwsmgr.h
 * @brief Native e-stim WebSocket manager and server interface
 */

#pragma once

#include "lljsonrpcws.h"
#include "llsd.h"
#include "lluuid.h"
#include <vector>
#include <string>
#include <memory>
#include <map>

struct EstimNotificationRule
{
    std::string sensor;
    std::string axis;
    S32 channel;
    LLUUID object_id;
};

struct EstimTriggerRule
{
    std::string sensor;
    std::string axis;
    std::string op;
    F32 threshold{0.0f};
    std::string action;
    mutable bool last_state{false};
};


class LLEstimWSConnection : public LLJSONRPCConnection, public std::enable_shared_from_this<LLEstimWSConnection>
{
public:
    using ptr_t  = std::shared_ptr<LLEstimWSConnection>;
    using wptr_t = std::weak_ptr<LLEstimWSConnection>;

    LLEstimWSConnection(const LLWebsocketMgr::WSServer::ptr_t server, const LLWebsocketMgr::connection_h& handle)
        : LLJSONRPCConnection(server, handle) {}

    ~LLEstimWSConnection() override = default;

    void onOpen() override;
    void onClose() override;
};

class LLEstimWSServer : public LLJSONRPCServer
{
public:
    static constexpr char const* DEFAULT_SERVER_NAME = "estim_server";
    static constexpr U16         DEFAULT_SERVER_PORT = 9030;

    using ptr_t = std::shared_ptr<LLEstimWSServer>;
    using wptr_t = std::weak_ptr<LLEstimWSServer>;

    LLEstimWSServer(const std::string& name, U16 port, bool local_only = true);
    ~LLEstimWSServer() override = default;

    static LLEstimWSServer::ptr_t getInstance();

    void onStarted() override;
    void onStopped() override;
    void onConnectionOpened(const LLWebsocketMgr::WSConnection::ptr_t& connection) override;
    void onConnectionClosed(const LLWebsocketMgr::WSConnection::ptr_t& connection) override;

    void sendStimCommand(const std::string& channel, U32 intensity, U32 frequency = 0, U32 pattern = 0);
    void sendShockCommand(const std::string& channel, U32 intensity, U32 frequency, U32 duration_ms);
    void sendPulseConfig(const std::string& channel, U32 pulse_width, const std::string& waveform);
    void sendSensorMode(const std::string& sensor, const std::string& mode);
    void panicStop();

    void registerTrigger(const EstimTriggerRule& rule);
    void clearTriggers();
    void evaluateSensorInput(const std::string& sensor, const std::string& axis, F32 value);

    F32 getSensorValue(const std::string& sensor, const std::string& axis) const;
    const std::map<std::string, std::map<std::string, F32>>& getSensorValues() const { return mSensorValues; }

    void registerNotification(const std::string& sensor, const std::string& axis, S32 channel, const LLUUID& object_id);
    void removeNotification(const std::string& sensor, const std::string& axis, S32 channel, const LLUUID& object_id);
    void clearNotificationsForObject(const LLUUID& object_id);
    void notifyLslSubscribers(const std::string& sensor, const std::string& axis, F32 value);

    void setMaxIntensityA(U32 intensity) { mMaxIntensityA = intensity; }
    void setMaxIntensityB(U32 intensity) { mMaxIntensityB = intensity; }
    U32 getMaxIntensityA() const { return mMaxIntensityA; }
    U32 getMaxIntensityB() const { return mMaxIntensityB; }

    void testChannelA();
    void testChannelB();

    void setAudioSyncEnabled(bool enabled);
    bool isAudioSyncEnabled() const { return mAudioSyncEnabled; }

    static void processFFTData(const float* spectrum_data, int length);
    void handleFFT(const float* spectrum_data, int length);

    // Telemetry getters
    F32 getCoyoteBattery() const { return mCoyoteBattery; }
    bool getLoadA() const { return mLoadA; }
    bool getLoadB() const { return mLoadB; }
    bool isConnected() const { return mConnected; }
    const std::vector<EstimTriggerRule>& getTriggers() const { return mTriggerRules; }

protected:
    LLWebsocketMgr::WSConnection::ptr_t connectionFactory(LLWebsocketMgr::WSServer::ptr_t server,
                                                          LLWebsocketMgr::connection_h handle) override;

    void setupConnectionMethods(LLJSONRPCConnection::ptr_t connection) override;

private:
    LLSD handleSensorUpdate(const LLSD& params);
    LLSD handleTelemetry(const LLSD& params);

    std::vector<EstimTriggerRule> mTriggerRules;
    std::vector<EstimNotificationRule> mNotifications;
    std::map<std::string, std::map<std::string, F32>> mSensorValues;
    bool mAudioSyncEnabled{ false };
    bool mSafetyCutoff{ false };
    bool mNotifiedLoadDisconnect{ false };
    bool mNotifiedLowBattery{ false };

    LLEstimWSConnection::ptr_t mActiveConnection;

    F32 mCoyoteBattery{ 100.0f };
    bool mLoadA{ true };
    bool mLoadB{ true };
    bool mConnected{ false };

    U32 mMaxIntensityA{ 255 };
    U32 mMaxIntensityB{ 255 };
    U32 mChannelAIntensity{ 0 };
    U32 mChannelBIntensity{ 0 };
};
