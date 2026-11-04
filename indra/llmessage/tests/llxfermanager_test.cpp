/**
 * @file llxfermanager_test.cpp
 * @brief Tests of LLXferManager's file requests.
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

#include "lldir.h"
#include "llfile.h"
#include "llmessageconfig.h"
#include "llsd.h"
#include "lltimer.h"
#include "message.h"
#include "net.h"

#include "../llxfermanager.h"
#include "../llxfer_file.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    TEST_CASE("LLXferManager: a remote file name shorter than its suffix is requested", "[LLXferManager]")
    {
        // The simulator names the remote file, and may give it fewer
        // characters than ".tmp". With no download allowed to start, the
        // request is only queued, and nothing is sent.
        LLXferManager manager;
        manager.setMaxIncomingXfers(0);
        for (const char* remote : { "", "a", "ab", "abc", ".tmp", "a.tmp" })
        {
            INFO("remote file name '" << remote << "'");
            U64 id = 0;
            CHECK_NOTHROW(id = manager.requestFile("llxfermanager_test_local.tmp", remote, LL_PATH_CACHE, LLHost(),
                                                   true, nullptr, nullptr));
            CHECK(id != 0);
        }
    }

    TEST_CASE("LLXferManager: a local file name shorter than its suffix makes a file transfer", "[LLXferManager]")
    {
        for (const char* local : { "a", "ab", "abc" })
        {
            INFO("local file name '" << local << "'");
            CHECK_NOTHROW(LLXfer_File(local, true, -1));
        }
    }

    // The xfer messages, as the template the viewer reads has them.
    const char* const XFER_TEMPLATE =
        "version 2.0\n"
        "{\n"
        "    RequestXfer Low 156 NotTrusted Zerocoded\n"
        "    {\n"
        "        XferID Single\n"
        "        { ID U64 }\n"
        "        { Filename Variable 1 }\n"
        "        { FilePath U8 }\n"
        "        { DeleteOnCompletion BOOL }\n"
        "        { UseBigPackets BOOL }\n"
        "        { VFileID LLUUID }\n"
        "        { VFileType S16 }\n"
        "    }\n"
        "}\n"
        "{\n"
        "    SendXferPacket High 18 NotTrusted Unencoded\n"
        "    {\n"
        "        XferID Single\n"
        "        { ID U64 }\n"
        "        { Packet U32 }\n"
        "    }\n"
        "    {\n"
        "        DataPacket Single\n"
        "        { Data Variable 2 }\n"
        "    }\n"
        "}\n"
        "{\n"
        "    ConfirmXferPacket High 19 NotTrusted Unencoded\n"
        "    {\n"
        "        XferID Single\n"
        "        { ID U64 }\n"
        "        { Packet U32 }\n"
        "    }\n"
        "}\n"
        "{\n"
        "    AbortXfer Low 157 NotTrusted Unencoded\n"
        "    {\n"
        "        XferID Single\n"
        "        { ID U64 }\n"
        "        { Result S32 }\n"
        "    }\n"
        "}\n";

    constexpr U32 LAST_PACKET = 0x80000000;

    // A file of the test's own name, removed when this ends. Its name ends in
    // .tmp, in the system's temporary folder.
    class TempFile
    {
    public:
        TempFile()
            : mPath(gDirUtilp->getTempFilename())
        {
        }

        ~TempFile()
        {
            LLFile::remove(mPath, ENOENT);
        }

        TempFile(const TempFile&) = delete;
        TempFile& operator=(const TempFile&) = delete;

        const std::string& path() const { return mPath; }

    private:
        std::string mPath;
    };

    // The message template file a session's system reads as it is made.
    class XferTemplateFile : public TempFile
    {
    public:
        XferTemplateFile()
        {
            llofstream file(path().c_str());
            file << XFER_TEMPLATE;
        }
    };

    // A socket standing for the simulator, on a port the OS picks.
    class SimulatorSocket
    {
    public:
        SimulatorSocket()
        {
            start_net(mSocket, mPort);
        }

        ~SimulatorSocket()
        {
            end_net(mSocket);
        }

        SimulatorSocket(const SimulatorSocket&) = delete;
        SimulatorSocket& operator=(const SimulatorSocket&) = delete;

        bool isOpen() const { return mSocket >= 0; }
        LLHost host() const { return LLHost("127.0.0.1", mPort); }

        bool send(const std::vector<U8>& packet, const LLHost& to) const
        {
            return send_packet(mSocket, (const char*)packet.data(), (int)packet.size(), to.getAddress(), to.getPort());
        }

        // The next packet to arrive within seconds, or nothing.
        std::vector<U8> receive(F32 seconds) const
        {
            char buffer[NET_BUFFER_SIZE];
            LLTimer waited;
            do
            {
                const S32 size = receive_packet(mSocket, buffer);
                if (size > 0)
                {
                    return std::vector<U8>(buffer, buffer + size);
                }
                ms_sleep(5);
            }
            while (waited.getElapsedTimeF32() < seconds);
            return {};
        }

    private:
        S32 mSocket = -1;
        int mPort = NET_USE_OS_ASSIGNED_PORT;
    };

    template <typename T>
    void appendLittleEndian(std::vector<U8>& bytes, T value)
    {
        for (size_t i = 0; i < sizeof(T); ++i)
        {
            bytes.push_back((U8)(value >> (8 * i)));
        }
    }

    template <typename T>
    T readLittleEndian(const std::vector<U8>& bytes, size_t at)
    {
        T value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
        {
            value |= (T)((U64)bytes[at + i] << (8 * i));
        }
        return value;
    }

    // What the viewer sends back carries a six-byte header, then the message's
    // number: all four bytes of a Low message's, the one of a High message's.
    bool isAbortXfer(const std::vector<U8>& packet)
    {
        return packet.size() >= 22 && packet[6] == 0xFF && packet[7] == 0xFF && packet[8] == 0x00 && packet[9] == 157;
    }

    bool isConfirmXferPacket(const std::vector<U8>& packet)
    {
        return packet.size() >= 19 && packet[6] == 19;
    }

    S32 sCallbacks = 0;
    S32 sCallbackResult = 0;

    void countCallback(void**, S32 result, LLExtStat)
    {
        ++sCallbacks;
        sCallbackResult = result;
    }

    // A viewer's message system and transfer manager, as gMessageSystem and
    // gXferManager while this lives, on a circuit to a simulator's socket.
    class XferSession
    {
    public:
        XferSession()
            : mSystem(mTemplate.path(), NET_USE_OS_ASSIGNED_PORT, 1, 0, 0, false, 5.f, 100.f)
            , mPriorSystem(gMessageSystem)
            , mPriorManager(gXferManager)
        {
            // The messages go by their templates, whatever config a case
            // before this one left in the process.
            LLMessageConfig::useConfig(LLSD());
            gMessageSystem = &mSystem;
            gXferManager = &mManager;
            if (mSystem.isOK() && mSimulator.isOpen())
            {
                mManager.registerCallbacks(&mSystem);
                mSystem.enableCircuit(mSimulator.host(), true);
            }
            sCallbacks = 0;
            sCallbackResult = 0;
        }

        ~XferSession()
        {
            // The circuit counts the reliable packets it still holds on
            // gMessageSystem as it goes, and the simulator here acks none.
            if (isReady())
            {
                mSystem.disableCircuit(mSimulator.host());
            }
            gXferManager = mPriorManager;
            gMessageSystem = mPriorSystem;
        }

        XferSession(const XferSession&) = delete;
        XferSession& operator=(const XferSession&) = delete;

        bool isReady() const { return mSystem.isOK() && mSimulator.isOpen(); }

        LLXferManager& manager() { return mManager; }
        const SimulatorSocket& simulator() const { return mSimulator; }

        // Asks the simulator for a file, to go to `local`.
        U64 request(const std::string& local, const std::string& remote = "remote.tmp")
        {
            return mManager.requestFile(local, remote, LL_PATH_CACHE, mSimulator.host(), false, countCallback, nullptr);
        }

        LLXfer* find(U64 id) { return mManager.findXferByID(id, mManager.mReceiveList); }

        // The simulator sends SendXferPacket with these bytes, and the viewer
        // reads it. `packet` is its number, and the unreliable packet that
        // carries it is numbered in the order they go.
        bool deliver(U64 id, U32 packet, const std::vector<U8>& data)
        {
            std::vector<U8> bytes = { 0, 0, 0, 0, (U8)++mPacketsSent, 0, 18 };
            appendLittleEndian(bytes, id);
            appendLittleEndian(bytes, packet);
            appendLittleEndian(bytes, (U16)data.size());
            bytes.insert(bytes.end(), data.begin(), data.end());
            if (!mSimulator.send(bytes, LLHost("127.0.0.1", mSystem.getListenPort())))
            {
                return false;
            }

            const U32 wanted = mSystem.mPacketsIn + 1;
            LockMessageChecker lmc(&mSystem);
            const F64 give_up = LLTimer::getTotalSeconds() + 1.0;
            while (mSystem.mPacketsIn < wanted && LLTimer::getTotalSeconds() < give_up)
            {
                if (!lmc.checkMessages())
                {
                    ms_sleep(1);
                }
            }
            return mSystem.mPacketsIn == wanted;
        }

    private:
        XferTemplateFile mTemplate;
        LLMessageSystem mSystem;
        LLMessageSystem* mPriorSystem;
        LLXferManager* mPriorManager;
        SimulatorSocket mSimulator;
        LLXferManager mManager;
        U32 mPacketsSent = 0;
    };

    TEST_CASE("LLXferManager: a first packet too short to hold the file's size gives the file up", "[LLXferManager]")
    {
        const size_t size = GENERATE(0, 1, 2, 3);
        CAPTURE(size);

        XferSession session;
        REQUIRE(session.isReady());
        const TempFile local;
        const U64 id = session.request(local.path());
        REQUIRE(session.find(id) != nullptr);
        REQUIRE(session.find(id)->mStatus == e_LL_XFER_IN_PROGRESS);
        REQUIRE_FALSE(session.simulator().receive(1.f).empty());

        REQUIRE(session.deliver(id, 0, std::vector<U8>(size, 0xFF)));

        CHECK(session.find(id) == nullptr);
        CHECK(session.manager().mReceiveList.empty());
        CHECK(session.manager().mSendList.empty());

        const std::vector<U8> answer = session.simulator().receive(1.f);
        REQUIRE(isAbortXfer(answer));
        CHECK(readLittleEndian<U64>(answer, 10) == id);
        CHECK(readLittleEndian<S32>(answer, 18) == LL_ERR_EOF);

        // A packet that comes after is for a transfer no longer there.
        REQUIRE(session.deliver(id, 0, std::vector<U8>(8, 0x01)));
        CHECK(session.simulator().receive(0.05f).empty());
    }

    TEST_CASE("LLXferManager: a file given up lets the next file waiting start", "[LLXferManager]")
    {
        XferSession session;
        REQUIRE(session.isReady());
        session.manager().setMaxIncomingXfers(1);
        const TempFile first_local;
        const TempFile second_local;
        const U64 first = session.request(first_local.path(), "first.tmp");
        const U64 second = session.request(second_local.path(), "second.tmp");
        REQUIRE(session.find(first)->mStatus == e_LL_XFER_IN_PROGRESS);
        REQUIRE(session.find(second)->mStatus == e_LL_XFER_PENDING);
        REQUIRE_FALSE(session.simulator().receive(1.f).empty());

        REQUIRE(session.deliver(first, 0, { 1, 2, 3 }));

        CHECK(session.find(first) == nullptr);
        REQUIRE(session.find(second) != nullptr);
        CHECK(session.find(second)->mStatus == e_LL_XFER_IN_PROGRESS);
    }

    TEST_CASE("LLXferManager: a first packet that holds the size and no more is an empty file", "[LLXferManager]")
    {
        XferSession session;
        REQUIRE(session.isReady());
        const TempFile local;
        const U64 id = session.request(local.path());
        REQUIRE_FALSE(session.simulator().receive(1.f).empty());

        REQUIRE(session.deliver(id, LAST_PACKET, { 0, 0, 0, 0 }));

        CHECK(session.find(id) == nullptr);
        CHECK(sCallbacks == 1);
        CHECK(sCallbackResult == LL_ERR_NOERR);
        CHECK(LLFile::isfile(local.path()));
        CHECK(LLFile::size(local.path()) == 0);

        const std::vector<U8> answer = session.simulator().receive(1.f);
        REQUIRE(isConfirmXferPacket(answer));
        CHECK(readLittleEndian<U64>(answer, 7) == id);
    }

    TEST_CASE("LLXferManager: a first packet past the size holds the file's first bytes", "[LLXferManager]")
    {
        XferSession session;
        REQUIRE(session.isReady());
        const TempFile local;
        const U64 id = session.request(local.path());
        REQUIRE_FALSE(session.simulator().receive(1.f).empty());

        REQUIRE(session.deliver(id, LAST_PACKET, { 3, 0, 0, 0, 'a', 'b', 'c' }));

        CHECK(session.find(id) == nullptr);
        CHECK(sCallbacks == 1);
        CHECK(sCallbackResult == LL_ERR_NOERR);
        std::ifstream file(local.path(), std::ios::binary);
        const std::string received((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CHECK(received == "abc");
    }
}
