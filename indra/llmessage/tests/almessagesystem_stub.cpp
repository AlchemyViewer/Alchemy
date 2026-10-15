/**
 * @file almessagesystem_stub.cpp
 * @brief An LLMessageSystem for a test that builds the code it tests alone
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

// Included by a test that stubs the members of LLMessageSystem its code calls
// rather than link llmessage. That code calls them through gMessageSystem,
// and a member called through a null pointer is undefined, even one that
// touches nothing: the test points gMessageSystem at the one this makes. Its
// constructor, and those of the members it holds, are stubbed with it, so
// none of the real one's sockets, templates or circuits are made.

#include "llcircuit.h"
#include "llpacketbuffer.h"
#include "llpacketring.h"
#include "message.h"

LLMessageSystem::LLMessageSystem(const std::string&, U32, S32, S32, S32, bool, const F32 circuit_heartbeat_interval,
                                 const F32 circuit_timeout)
:   mCircuitInfo(F32Seconds(circuit_heartbeat_interval), F32Seconds(circuit_timeout))
{
}

LLMessageSystem::~LLMessageSystem()
{
}

S32 LLMessageSystem::sendMessage(const LLHost&, LLStoredMessagePtr)
{
    return 0;
}

LLCircuit::LLCircuit(const F32Seconds, const F32Seconds)
{
}

LLCircuit::~LLCircuit()
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

namespace
{
    // Made the first time a test asks, and kept for the process.
    LLMessageSystem* stubMessageSystem()
    {
        static LLMessageSystem system("", 0, 0, 0, 0, false, 0.f, 0.f);
        return &system;
    }
}
