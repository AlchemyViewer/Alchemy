/**
 * @file llconversationlog_test.cpp
 * @brief Conversation Log discovery tests.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header: almost always required for newview cpp files
#include "../llviewerprecompiledheaders.h"
// Class to test
#include "../llconversationlog.h"
// Dependencies
#include "../llviewercontrol.h"
#include "llcontrol.h"
#include "llsingleton.h"

#include <catch2/catch_test_macros.hpp>

// -------------------------------------------------------------------------------------------
// Stubbing: Declarations required to link and run the class being tested
// gSavedSettings is newview_test_settings' group.

LLControlGroup gSavedPerAccountSettings("test per-account settings");
LLUUID gAgentID("00000000-0000-0000-0000-000000000001");

// The fixture needs only the no-session branches of the viewer services reached
// by LLConversationLog; these doubles keep that boundary isolated.
LLIMModel::LLIMModel()
{
}

LLIMModel::LLIMSession* LLIMModel::findIMSession(const LLUUID&) const
{
    return nullptr;
}

bool LLIMModel::LLIMSession::isOutgoingAdHoc() const
{
    return false;
}

LLUUID LLIMModel::LLIMSession::generateOutgoingAdHocHash() const
{
    return mSessionID;
}

LLIMMgr::LLIMMgr()
{
}

void LLIMMgr::addSessionObserver(LLIMSessionObserver*)
{
}

void LLIMMgr::removeSessionObserver(LLIMSessionObserver*)
{
}

LLAvatarTracker LLAvatarTracker::sInstance;

LLAvatarTracker::LLAvatarTracker()
:   mTrackingData(nullptr),
    mTrackedAgentValid(false),
    mModifyMask(0),
    mIsNotifyObservers(false)
{
}

LLAvatarTracker::~LLAvatarTracker()
{
}

void LLAvatarTracker::addObserver(LLFriendObserver*)
{
}

void LLAvatarTracker::removeObserver(LLFriendObserver*)
{
}

void LLLogChat::renameLogFile(const std::string&, const std::string&)
{
}

LLFloaterIMSession* LLFloaterIMSession::findInstance(const LLUUID&)
{
    return nullptr;
}

boost::signals2::connection LLFloaterIMSession::setIMFloaterShowedCallback(
    const floater_showed_signal_t::slot_type&)
{
    return boost::signals2::connection();
}

// End Stubbing
// -------------------------------------------------------------------------------------------

// LLConversationLog befriends this name, at global scope: private access to fixture state,
// since production initialization binds global services.
struct LLConversationLogTestAccess
{
    static void reset(bool logging_enabled)
    {
        LLConversationLog& log = LLConversationLog::instance();
        log.mConversations.clear();
        log.mLoggingEnabled = logging_enabled;
    }

    static void setOfflineMessages(const LLUUID& session_id, bool has_offline_messages)
    {
        LLConversationLog& log = LLConversationLog::instance();
        for (LLConversation& conversation : log.mConversations)
        {
            if (conversation.getSessionID() == session_id)
            {
                conversation.setOfflineMessages(has_offline_messages);
                return;
            }
        }
    }
};

namespace
{
    // Capture both observer contracts so timestamp updates stay targeted to one row.
    struct ConversationLogObserver : public LLConversationLogObserver
    {
        void changed() override
        {
            ++mRefreshes;
        }

        void changed(const LLUUID& session_id, U32 mask) override
        {
            ++mParticularChanges;
            mSessionID = session_id;
            mMask = mask;
        }

        S32 mRefreshes = 0;
        S32 mParticularChanges = 0;
        LLUUID mSessionID;
        U32 mMask = 0;
    };

    // An observer on the log for as long as it is in scope, a failed check included.
    struct ObservedLog
    {
        ObservedLog(LLConversationLog& log, LLConversationLogObserver& observer)
        :   mLog(log),
            mObserver(observer)
        {
            mLog.addObserver(&mObserver);
        }
        ~ObservedLog()
        {
            mLog.removeObserver(&mObserver);
        }
        ObservedLog(const ObservedLog&)            = delete;
        ObservedLog& operator=(const ObservedLog&) = delete;

        LLConversationLog&         mLog;
        LLConversationLogObserver& mObserver;
    };

    struct ConversationLogFixture
    {
        ConversationLogFixture()
        {
            // What the log reads of the settings, as an undeclared control reads: a 12-hour
            // timestamp, and no transcripts kept, so that cache() writes no conversation.log.
            if (!gSavedSettings.controlExists("Use24HourClock"))
            {
                gSavedSettings.declareBOOL("Use24HourClock", false, "Declared In Code", LLControlVariable::PERSIST_NO);
            }
            if (!gSavedPerAccountSettings.controlExists("KeepConversationLogTranscripts"))
            {
                gSavedPerAccountSettings.declareS32("KeepConversationLogTranscripts", 0, "Declared In Code", LLControlVariable::PERSIST_NO);
            }

            // A different device starts without locally created conversation metadata.
            LLConversationLogTestAccess::reset(true);
        }

        ~ConversationLogFixture()
        {
            LLConversationLogTestAccess::reset(false);
        }

        ConversationLogFixture(const ConversationLogFixture&)            = delete;
        ConversationLogFixture& operator=(const ConversationLogFixture&) = delete;
    };

    TEST_CASE_METHOD(ConversationLogFixture, "LLConversationLog: a service archive with no live session adds one P2P row with its metadata", "[LLConversationLog]")
    {
        const LLUUID resident_id("00000000-0000-0000-0000-000000000002");
        const LLUUID session_id = gAgentID ^ resident_id;
        const U64Seconds archived_time(LLUnits::Seconds::fromValue(1787756400.0));

        // A durable service archive has resident metadata but no live IM session.
        LLConversationLog& log = LLConversationLog::instance();
        log.addServiceConversation(
            session_id,
            "Bridie Linden",
            "bridie.linden",
            resident_id,
            archived_time);

        // Repeated archive publication must not duplicate the Conversation Log row.
        log.addServiceConversation(
            session_id,
            "Bridie Linden",
            "bridie.linden",
            resident_id,
            archived_time);

        const LLConversation* discovered = log.getConversation(session_id);
        UNSCOPED_INFO("remote direct conversation is discoverable");
        REQUIRE(discovered != nullptr);
        UNSCOPED_INFO("remote conversation is unique");
        CHECK(log.getConversations().size() == size_t(1));
        UNSCOPED_INFO("remote conversation is P2P");
        CHECK(discovered->getConversationType() == LLIMModel::LLIMSession::P2P_SESSION);
        UNSCOPED_INFO("remote archive time retained");
        CHECK(discovered->getTime() == archived_time);
        UNSCOPED_INFO("remote participant retained");
        CHECK(discovered->getParticipantID() == resident_id);
        UNSCOPED_INFO("remote name retained");
        CHECK(discovered->getConversationName() == std::string("Bridie Linden"));
        UNSCOPED_INFO("remote transcript stem retained");
        CHECK(discovered->getHistoryFileName() == std::string("bridie.linden"));
    }

    TEST_CASE_METHOD(ConversationLogFixture, "LLConversationLog: a newer service archive advances only the time and notifies that one row", "[LLConversationLog]")
    {
        const LLUUID resident_id("00000000-0000-0000-0000-000000000002");
        const LLUUID changed_resident_id("00000000-0000-0000-0000-000000000003");
        const LLUUID session_id = gAgentID ^ resident_id;
        const U64Seconds original_time(LLUnits::Seconds::fromValue(1787756400.0));
        const U64Seconds newer_time(LLUnits::Seconds::fromValue(1787929200.0));
        const U64Seconds older_time(LLUnits::Seconds::fromValue(1787666400.0));

        LLConversationLog& log = LLConversationLog::instance();
        log.addServiceConversation(
            session_id,
            "Bridie Linden",
            "bridie.linden",
            resident_id,
            original_time);
        LLConversationLogTestAccess::setOfflineMessages(session_id, true);

        // A newer archive advances only the timestamp. Equal and older publications
        // remain silent so stale service data cannot regress or duplicate the row.
        ConversationLogObserver observer;
        {
            ObservedLog observed(log, observer);
            log.addServiceConversation(
                session_id,
                "Changed Name",
                "changed.filename",
                changed_resident_id,
                newer_time);
            log.addServiceConversation(
                session_id,
                "Changed Again",
                "changed.again",
                changed_resident_id,
                newer_time);
            log.addServiceConversation(
                session_id,
                "Older Name",
                "older.filename",
                changed_resident_id,
                older_time);
        }

        const LLConversation* updated = log.getConversation(session_id);
        UNSCOPED_INFO("updated conversation remains present");
        REQUIRE(updated != nullptr);
        UNSCOPED_INFO("updated conversation remains unique");
        CHECK(log.getConversations().size() == size_t(1));
        UNSCOPED_INFO("newer archive time applied");
        CHECK(updated->getTime() == newer_time);
        UNSCOPED_INFO("formatted archive time refreshed");
        CHECK(updated->getTimestamp() == LLConversation::createTimestamp(newer_time));
        UNSCOPED_INFO("existing participant preserved");
        CHECK(updated->getParticipantID() == resident_id);
        UNSCOPED_INFO("existing name preserved");
        CHECK(updated->getConversationName() == std::string("Bridie Linden"));
        UNSCOPED_INFO("existing transcript stem preserved");
        CHECK(updated->getHistoryFileName() == std::string("bridie.linden"));
        UNSCOPED_INFO("existing offline state preserved");
        CHECK(updated->hasOfflineMessages());
        UNSCOPED_INFO("timestamp update avoids full refresh");
        CHECK(observer.mRefreshes == S32(0));
        UNSCOPED_INFO("only newer time notifies");
        CHECK(observer.mParticularChanges == S32(1));
        UNSCOPED_INFO("timestamp update identifies session");
        CHECK(observer.mSessionID == session_id);
        UNSCOPED_INFO("timestamp update identifies change");
        CHECK(observer.mMask == U32(LLConversationLogObserver::CHANGED_TIME));
    }

    TEST_CASE_METHOD(ConversationLogFixture, "LLConversationLog: service history adds no row while the log is disabled", "[LLConversationLog]")
    {
        LLConversationLogTestAccess::reset(false);

        // Service history must not create metadata when Conversation Log privacy is disabled.
        const LLUUID resident_id("00000000-0000-0000-0000-000000000002");
        LLConversationLog& log = LLConversationLog::instance();
        log.addServiceConversation(
            gAgentID ^ resident_id,
            "Bridie Linden",
            "bridie.linden",
            resident_id,
            U64Seconds(LLUnits::Seconds::fromValue(1787756400.0)));

        UNSCOPED_INFO("disabled Conversation Log remains empty");
        CHECK(log.isLogEmpty());
    }
}
