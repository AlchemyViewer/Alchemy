/**
 * @file llcircuit_test.cpp
 * @brief LLCircuitData's averaged ping, and the acks it collects.
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
#include <vector>

// The circuit reads its times from the message system's clock, which here is
// the test's own, and sends its acks through the message system, which here
// records the IDs each PacketAck carries.
namespace
{
    F64Seconds sMessageTime;
    std::vector<TPACKETID> sAckedIDs;
    std::vector<TPACKETID> sMessageIDs;
}

F64Seconds LLMessageSystem::getMessageTimeSeconds(const bool)
{
    return sMessageTime;
}

U64Microseconds LLMessageSystem::getMessageTimeUsecs(const bool)
{
    return sMessageTime;
}

void LLMessageSystem::newMessageFast(const char*)
{
    sMessageIDs.clear();
}

void LLMessageSystem::nextBlockFast(const char*)
{
}

void LLMessageSystem::addU8Fast(const char*, U8)
{
}

void LLMessageSystem::addU32Fast(const char*, U32 u)
{
    sMessageIDs.push_back(u);
}

S32 LLMessageSystem::sendMessage(const LLHost&)
{
    sAckedIDs.insert(sAckedIDs.end(), sMessageIDs.begin(), sMessageIDs.end());
    return 0;
}

// The rest of llmessage that llcircuit.cpp calls. A circuit makes its throttle
// group, and as it goes ends its host's transfers and reads whether the
// message system logs verbosely; the rest is reached only by sending,
// resending or acking a reliable packet, which no case does. The message
// system a circuit reads is made with none of the real one's sockets or
// templates.
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

namespace ll_test
{
    // What the message system does with a circuit's acks as packets come in
    // from its host and go out to it.
    struct CircuitProbe
    {
        // A reliable packet came in, to be acked.
        static void receiveReliable(LLCircuitData& circuit, TPACKETID id)
        {
            circuit.collectRAck(id);
        }

        // A packet went out with every waiting ack appended to it, taken off
        // the circuit's list as LLMessageSystem::sendMessage takes them.
        static void carryAcks(LLCircuitData& circuit)
        {
            circuit.mAcks.erase(circuit.mAcks.begin(), circuit.mAcks.end());
        }

        // A reliable packet was handled, its number counted in and recorded
        // as LLMessageSystem::checkMessages counts and records it.
        static void handled(LLCircuitData& circuit, TPACKETID id)
        {
            circuit.checkPacketInID(id, false);
            circuit.recordReliablePacket(id);
        }

        // How many reliable packets the circuit remembers.
        static size_t recentCount(const LLCircuitData& circuit)
        {
            return circuit.mRecentlyReceivedReliablePackets.size();
        }

        static bool checkDuplicate(LLCircuitData& circuit, TPACKETID id, bool resent)
        {
            return circuit.checkDuplicate(id, resent);
        }

        // A packet's number was counted in, the packet not marked resent.
        static void checkIn(LLCircuitData& circuit, TPACKETID id)
        {
            circuit.checkPacketInID(id, false);
        }

        // The numbers a gap skipped that have not come, lowest first.
        static std::vector<TPACKETID> missing(const LLCircuitData& circuit)
        {
            std::vector<TPACKETID> ids;
            for (const auto& number : circuit.mPotentialLostPackets)
            {
                ids.push_back(number.first);
            }
            return ids;
        }
    };
}

namespace
{
    using ll_test::CircuitProbe;
    using ll_test::WithinRounding;

    // The heartbeat and timeout the message system's tests start it with.
    const F32Seconds HEARTBEAT(5.f);
    const F32Seconds TIMEOUT(100.f);

    // The clock starts at 1024 s, and every ping starts on a whole second
    // below 2048 s: there a double rounds a whole second and 0.2 s up, so an
    // answer 0.2 s later measures 200 ms rather than truncating to 199.
    const F64Seconds CLOCK_START(1024.0);

    // How long the viewer lets acks gather before it sends them in a
    // PacketAck of their own (AckCollectTime).
    const F32 ACK_COLLECT_TIME = 0.1f;

    // gMessageSystem is the stub while this lives, and what it was before
    // once it ends, with no circuit left on its lists; the clock is at its
    // start.
    class StubMessageSystemScope
    {
    public:
        StubMessageSystemScope()
            : mPrior(gMessageSystem)
        {
            static LLMessageSystem system("", 0, 0, 0, 0, false, HEARTBEAT.value(), TIMEOUT.value());
            gMessageSystem = &system;
            sMessageTime = CLOCK_START;
            sAckedIDs.clear();
        }

        ~StubMessageSystemScope()
        {
            gMessageSystem->mCircuitInfo.mSendAckMap.clear();
            gMessageSystem->mCircuitInfo.mUnackedCircuitMap.clear();
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

    // Waits until the circuit is `seconds` older than it was at `since`, by
    // its own age, which is what it times its acks by.
    void wait_until_aged(const LLCircuitData& circuit, F64 since, F64 seconds)
    {
        while (circuit.getAgeInSeconds() - since <= seconds)
        {
            ms_sleep(5);
        }
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

    TEST_CASE("llcircuit: an ack after a packet carried the last ones waits the collect time", "[llcircuit]")
    {
        StubMessageSystemScope message_system;
        LLCircuitData circuit(LLHost(), 0, HEARTBEAT, TIMEOUT);
        LLCircuit& circuits = gMessageSystem->mCircuitInfo;

        // An ack waits, a packet to the host carries it, and the next frame
        // finds the circuit with none to send.
        const F64 first_ack = circuit.getAgeInSeconds();
        CircuitProbe::receiveReliable(circuit, 1);
        CircuitProbe::carryAcks(circuit);
        circuits.sendAcks(ACK_COLLECT_TIME);
        CHECK(circuits.mSendAckMap.empty());
        CHECK(sAckedIDs.empty());

        // Well past the collect time after the first, another ack comes in,
        // and waits the collect time from its own arrival
        wait_until_aged(circuit, first_ack, 2.0 * ACK_COLLECT_TIME);
        const F64 second_ack = circuit.getAgeInSeconds();
        CircuitProbe::receiveReliable(circuit, 2);
        circuits.sendAcks(ACK_COLLECT_TIME);
        CHECK(sAckedIDs.empty());
        CHECK(circuits.mSendAckMap.size() == 1);

        // before it goes in a PacketAck.
        wait_until_aged(circuit, second_ack, ACK_COLLECT_TIME);
        circuits.sendAcks(ACK_COLLECT_TIME);
        CHECK(sAckedIDs == std::vector<TPACKETID>{ 2 });
        CHECK(circuits.mSendAckMap.empty());
    }

    TEST_CASE("llcircuit: acks gather for the collect time and go in one PacketAck", "[llcircuit]")
    {
        StubMessageSystemScope message_system;
        LLCircuitData circuit(LLHost(), 0, HEARTBEAT, TIMEOUT);
        LLCircuit& circuits = gMessageSystem->mCircuitInfo;

        const F64 first_ack = circuit.getAgeInSeconds();
        CircuitProbe::receiveReliable(circuit, 1);
        CircuitProbe::receiveReliable(circuit, 2);
        circuits.sendAcks(ACK_COLLECT_TIME);
        CHECK(sAckedIDs.empty());

        wait_until_aged(circuit, first_ack, ACK_COLLECT_TIME);
        CircuitProbe::receiveReliable(circuit, 3);
        circuits.sendAcks(ACK_COLLECT_TIME);
        CHECK(sAckedIDs == std::vector<TPACKETID>{ 1, 2, 3 });
        CHECK(circuits.mSendAckMap.empty());
    }

    TEST_CASE("llcircuit: an unmarked packet is a duplicate only of a number handled moments ago", "[llcircuit]")
    {
        StubMessageSystemScope message_system;
        LLCircuitData circuit(LLHost(), 0, HEARTBEAT, TIMEOUT);
        CircuitProbe::handled(circuit, 7);

        // A copy the network made comes unmarked, a resend marked.
        CHECK(CircuitProbe::checkDuplicate(circuit, 7, false));
        CHECK(CircuitProbe::checkDuplicate(circuit, 7, true));
        CHECK_FALSE(CircuitProbe::checkDuplicate(circuit, 8, false));
        CHECK_FALSE(CircuitProbe::checkDuplicate(circuit, 8, true));

        sMessageTime += F64Seconds(4.0);
        CHECK(CircuitProbe::checkDuplicate(circuit, 7, false));

        // Long after, an unmarked packet with that number is a fresh one from
        // numbering the sender started over; a resend still matches.
        sMessageTime += F64Seconds(6.0);
        CHECK_FALSE(CircuitProbe::checkDuplicate(circuit, 7, false));
        CHECK(CircuitProbe::checkDuplicate(circuit, 7, true));
    }

    TEST_CASE("llcircuit: a peer that never pings leaves only the newest reliable packets remembered", "[llcircuit]")
    {
        StubMessageSystemScope message_system;
        LLCircuitData circuit(LLHost(), 0, HEARTBEAT, TIMEOUT);

        const TPACKETID PAST_THE_BOUND = 100;
        const TPACKETID COUNT = static_cast<TPACKETID>(LL_MAX_RECENT_RELIABLE_PACKETS) + PAST_THE_BOUND;
        for (TPACKETID id = 0; id < COUNT; ++id)
        {
            CircuitProbe::handled(circuit, id);
        }

        CHECK(CircuitProbe::recentCount(circuit) == LL_MAX_RECENT_RELIABLE_PACKETS);
        CHECK_FALSE(CircuitProbe::checkDuplicate(circuit, 0, true));
        CHECK_FALSE(CircuitProbe::checkDuplicate(circuit, PAST_THE_BOUND - 1, true));
        CHECK(CircuitProbe::checkDuplicate(circuit, PAST_THE_BOUND, true));
        CHECK(CircuitProbe::checkDuplicate(circuit, COUNT - 1, true));
    }

    TEST_CASE("llcircuit: the reliable packets forgotten across a wrap are the ones before it", "[llcircuit]")
    {
        StubMessageSystemScope message_system;
        // Enough numbers before the wrap to fill the list, and a few after.
        const TPACKETID FIRST = LL_MAX_OUT_PACKET_ID - static_cast<TPACKETID>(LL_MAX_RECENT_RELIABLE_PACKETS);
        LLCircuitData circuit(LLHost(), FIRST, HEARTBEAT, TIMEOUT);

        for (TPACKETID id = FIRST; id < LL_MAX_OUT_PACKET_ID; ++id)
        {
            CircuitProbe::handled(circuit, id);
        }
        const TPACKETID AFTER_THE_WRAP = 10;
        for (TPACKETID id = 0; id < AFTER_THE_WRAP; ++id)
        {
            CircuitProbe::handled(circuit, id);
        }

        CHECK(CircuitProbe::recentCount(circuit) == LL_MAX_RECENT_RELIABLE_PACKETS);
        CHECK(CircuitProbe::checkDuplicate(circuit, 0, true));
        CHECK(CircuitProbe::checkDuplicate(circuit, AFTER_THE_WRAP - 1, true));
        CHECK_FALSE(CircuitProbe::checkDuplicate(circuit, FIRST, true));
        CHECK_FALSE(CircuitProbe::checkDuplicate(circuit, FIRST + AFTER_THE_WRAP - 1, true));
        CHECK(CircuitProbe::checkDuplicate(circuit, FIRST + AFTER_THE_WRAP, true));
    }

    TEST_CASE("llcircuit: a ping keeps a number handled moments ago that its sender has had acked", "[llcircuit]")
    {
        StubMessageSystemScope message_system;
        LLCircuitData circuit(LLHost(), 0, HEARTBEAT, TIMEOUT);
        CircuitProbe::handled(circuit, 7);
        CircuitProbe::handled(circuit, 8);
        CircuitProbe::handled(circuit, 9);

        // A second on, the sender has had 7 acked and pings with 8 its oldest
        // unacked; a resend of 7 that crossed the ack, or a copy the network
        // made, may still be on its way.
        sMessageTime += F64Seconds(1.0);
        circuit.clearDuplicateList(8);
        CHECK(CircuitProbe::checkDuplicate(circuit, 7, false));
        CHECK(CircuitProbe::checkDuplicate(circuit, 7, true));

        // A ping five seconds after those copies lets 7 go, and keeps 8, which
        // it does not pass.
        sMessageTime += F64Seconds(5.0);
        circuit.clearDuplicateList(8);
        CHECK_FALSE(CircuitProbe::checkDuplicate(circuit, 7, true));
        CHECK(CircuitProbe::checkDuplicate(circuit, 8, true));
    }

    TEST_CASE("llcircuit: each copy of a handled packet restarts the time a ping keeps its number", "[llcircuit]")
    {
        StubMessageSystemScope message_system;
        LLCircuitData circuit(LLHost(), 0, HEARTBEAT, TIMEOUT);
        const F64Seconds handled_at = sMessageTime;
        CircuitProbe::handled(circuit, 7);
        CircuitProbe::handled(circuit, 8);
        CircuitProbe::handled(circuit, 9);

        // The ack for 7 is lost, and the sender resends it.
        sMessageTime = handled_at + F64Seconds(4.0);
        CHECK(CircuitProbe::checkDuplicate(circuit, 7, true));

        // The ack for that resend reaches the sender, which pings with 8 its
        // oldest unacked, while a resend of 7 it sent before the ack came is
        // still on its way.
        sMessageTime = handled_at + F64Seconds(6.5);
        circuit.clearDuplicateList(8);
        sMessageTime = handled_at + F64Seconds(7.0);
        CHECK(CircuitProbe::checkDuplicate(circuit, 7, true));

        // A ping five seconds after that last copy lets 7 go.
        sMessageTime = handled_at + F64Seconds(12.0);
        circuit.clearDuplicateList(8);
        CHECK_FALSE(CircuitProbe::checkDuplicate(circuit, 7, true));
    }

    TEST_CASE("llcircuit: a number past the 24 bits opens no gap", "[llcircuit]")
    {
        StubMessageSystemScope message_system;
        LLCircuitData circuit(LLHost(), 0, HEARTBEAT, TIMEOUT);
        CircuitProbe::checkIn(circuit, 5);

        // The wire carries 32 bits and numbers wrap at 24. This number's low
        // bits are the one expected next, so it looks no distance ahead, yet
        // counting up from the number expected never reaches it.
        CircuitProbe::checkIn(circuit, LL_MAX_OUT_PACKET_ID + 6);
        CHECK(CircuitProbe::missing(circuit).empty());

        // Nor does the highest number the wire can carry open one.
        CircuitProbe::checkIn(circuit, 0xFFFFFFFF);
        CHECK(CircuitProbe::missing(circuit).empty());

        // Nor the first number past the top, from just below it.
        CircuitProbe::checkIn(circuit, LL_MAX_OUT_PACKET_ID - 3);
        CircuitProbe::checkIn(circuit, LL_MAX_OUT_PACKET_ID);
        CHECK(CircuitProbe::missing(circuit).empty());

        // Gaps are still counted after them.
        CircuitProbe::checkIn(circuit, 20);
        CircuitProbe::checkIn(circuit, 23);
        CHECK(CircuitProbe::missing(circuit) == std::vector<TPACKETID>{ 21, 22 });
    }

    TEST_CASE("llcircuit: a gap across the wrap leaves its numbers missing", "[llcircuit]")
    {
        StubMessageSystemScope message_system;
        LLCircuitData circuit(LLHost(), 0, HEARTBEAT, TIMEOUT);
        CircuitProbe::checkIn(circuit, 0);
        CircuitProbe::checkIn(circuit, LL_MAX_OUT_PACKET_ID - 3);
        CircuitProbe::checkIn(circuit, 2);
        CHECK(CircuitProbe::missing(circuit)
              == std::vector<TPACKETID>{ 0, 1, LL_MAX_OUT_PACKET_ID - 2, LL_MAX_OUT_PACKET_ID - 1 });
    }
}
