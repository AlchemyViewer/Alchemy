/**
 * @file llcircuit_test.cpp
 * @brief LLCircuitData's averaged ping and the resend timeout it gives.
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

#include "linden_common.h"

#include "llcircuit.h"

#include "llpacketbuffer.h"
#include "llpacketring.h"
#include "llpounceable.h"
#include "lltransfermanager.h"
#include "message.h"

#include "altestmatchers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

// The circuit reads its times from the message system's clock, which here is
// the test's own.
namespace
{
    F64Seconds sMessageTime;
}

F64Seconds LLMessageSystem::getMessageTimeSeconds(const bool)
{
    return sMessageTime;
}

U64Microseconds LLMessageSystem::getMessageTimeUsecs(const bool)
{
    return sMessageTime;
}

// The rest of llmessage that llcircuit.cpp calls. A circuit makes its throttle
// group, and as it goes ends its host's transfers and reads whether the
// message system logs verbosely; the rest is reached only by sending,
// resending or acking a packet, which no case does. The message system a
// circuit reads is made with none of the real one's sockets or templates.
LLPounceable<LLMessageSystem*, LLPounceableStatic> gMessageSystem;

LLMessageSystem::LLMessageSystem(const std::string&, U32, S32, S32, S32, bool, const F32 circuit_heartbeat_interval,
                                 const F32 circuit_timeout)
:   mVerboseLog(false),
    mCircuitInfo(F32Seconds(circuit_heartbeat_interval), F32Seconds(circuit_timeout))
{
}

LLMessageSystem::~LLMessageSystem()
{
}

LLPacketRing::LLPacketRing()
{
}

LLPacketRing::~LLPacketRing()
{
}

LLPacketBuffer::~LLPacketBuffer()
{
}

void LLMessageSystem::newMessageFast(const char*)
{
}

void LLMessageSystem::nextBlockFast(const char*)
{
}

void LLMessageSystem::addU8Fast(const char*, U8)
{
}

void LLMessageSystem::addU32Fast(const char*, U32)
{
}

S32 LLMessageSystem::sendMessage(const LLHost&)
{
    return 0;
}

S32 LLMessageSystem::sendMessage(const LLHost&, LLStoredMessagePtr)
{
    return 0;
}

bool LLMessageSystem::sendPacketToSocket(const char*, S32, LLHost)
{
    return false;
}

LLTransferManager gTransferManager;

LLTransferManager::LLTransferManager()
{
}

LLTransferManager::~LLTransferManager()
{
}

void LLTransferManager::cleanupConnection(const LLHost&)
{
}

LLThrottleGroup::LLThrottleGroup()
{
}

bool LLThrottleGroup::checkOverflow(S32, F32)
{
    return false;
}

bool LLThrottleGroup::throttleOverflow(S32, F32)
{
    return false;
}

bool LLThrottleGroup::dynamicAdjust()
{
    return false;
}

LLReliablePacket::LLReliablePacket(S32, U8*, S32, LLReliablePacketParams*)
:   mBuffer(nullptr),
    mBufferLength(0)
{
}

std::string LLHost::getIPandPort() const
{
    return {};
}

std::ostream& operator<<(std::ostream& os, const LLHost&)
{
    return os;
}

char const* const _PREHASH_ID = "ID";
char const* const _PREHASH_OldestUnacked = "OldestUnacked";
char const* const _PREHASH_PacketAck = "PacketAck";
char const* const _PREHASH_Packets = "Packets";
char const* const _PREHASH_PingID = "PingID";
char const* const _PREHASH_StartPingCheck = "StartPingCheck";

namespace
{
    using ll_test::WithinRounding;

    // The heartbeat and timeout the message system's tests start it with.
    const F32Seconds HEARTBEAT(5.f);
    const F32Seconds TIMEOUT(100.f);

    // The clock starts at 1024 s, and every ping starts on a whole second
    // below 2048 s: there a double rounds a whole second and 0.2 s up, so an
    // answer 0.2 s later measures 200 ms rather than truncating to 199.
    const F64Seconds CLOCK_START(1024.0);

    // gMessageSystem is the stub while this lives, and what it was before
    // once it ends; the clock is at its start.
    class StubMessageSystemScope
    {
    public:
        StubMessageSystemScope()
            : mPrior(gMessageSystem)
        {
            static LLMessageSystem system("", 0, 0, 0, 0, false, HEARTBEAT.value(), TIMEOUT.value());
            gMessageSystem = &system;
            sMessageTime = CLOCK_START;
        }

        ~StubMessageSystemScope()
        {
            gMessageSystem = mPrior;
        }

        StubMessageSystemScope(const StubMessageSystemScope&) = delete;
        StubMessageSystemScope& operator=(const StubMessageSystemScope&) = delete;

    private:
        LLMessageSystem* mPrior;
    };

    // What the message system waits before resending a reliable packet on
    // the circuit, as it works it out when it sends the packet and again
    // when it resends it.
    F32Seconds resend_timeout(LLCircuitData& circuit)
    {
        return llmax(LL_MINIMUM_RELIABLE_TIMEOUT_SECONDS,
                     F32Seconds(LL_RELIABLE_TIMEOUT_FACTOR * circuit.getPingDelayAveraged()));
    }

    // A ping sent at `sent` and answered `round_trip` later, with no other
    // ping in flight. The circuit numbers a ping only as it sends one itself,
    // so the last one sent is still number 0.
    void answer_ping(LLCircuitData& circuit, F64Seconds sent, F64Seconds round_trip)
    {
        sMessageTime = sent;
        circuit.pingTimerStart();
        sMessageTime = sent + round_trip;
        circuit.pingTimerStop(0);
    }

    TEST_CASE("llcircuit: an answered ping moves the averaged ping toward its round trip", "[llcircuit]")
    {
        StubMessageSystemScope message_system;
        LLCircuitData circuit(LLHost(), 0, HEARTBEAT, TIMEOUT);
        REQUIRE(circuit.getPingDelayAveraged() == F32Milliseconds(INITIAL_PING_VALUE_MSEC));

        answer_ping(circuit, CLOCK_START, F64Seconds(0.2));
        CHECK(circuit.getPingsInTransit() == 0);
        CHECK(circuit.getPingDelay() == U32Milliseconds(200));

        // A fifth of the way from 1000 ms to 200 ms, and five times that.
        CHECK_THAT(circuit.getPingDelayAveraged().value(), WithinRounding(840.f, 2000.f));
        CHECK_THAT(resend_timeout(circuit).value(), WithinRounding(4.2f, 10.f));

        // Each answer a heartbeat later moves it a fifth of the rest of the way.
        const S32 answers = 20;
        for (S32 answer = 1; answer < answers; ++answer)
        {
            answer_ping(circuit, CLOCK_START + answer * HEARTBEAT, F64Seconds(0.2));
        }
        const F32 settled = 200.f + 800.f * std::pow(0.8f, F32(answers));
        CHECK_THAT(circuit.getPingDelayAveraged().value(), WithinRounding(settled, 2000.f));
        CHECK_THAT(resend_timeout(circuit).value(), WithinRounding(5.f * settled / 1000.f, 10.f));
    }

    TEST_CASE("llcircuit: a slow answer raises the averaged ping to its round trip at once", "[llcircuit]")
    {
        StubMessageSystemScope message_system;
        LLCircuitData circuit(LLHost(), 0, HEARTBEAT, TIMEOUT);

        answer_ping(circuit, CLOCK_START, F64Seconds(1.5));
        CHECK_THAT(circuit.getPingDelayAveraged().value(), WithinRounding(1500.f, 2000.f));
        CHECK_THAT(resend_timeout(circuit).value(), WithinRounding(7.5f, 10.f));

        // and falls back a fifth of the way at the next fast one.
        answer_ping(circuit, CLOCK_START + HEARTBEAT, F64Seconds(0.2));
        CHECK_THAT(circuit.getPingDelayAveraged().value(), WithinRounding(1240.f, 2000.f));
    }
}
