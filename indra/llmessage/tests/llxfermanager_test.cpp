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
#include "llzerocode.h"
#include "message.h"
#include "net.h"

#include "../llxfermanager.h"
#include "../llxfer_file.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <fstream>
#include <functional>
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

    // The ID a RequestXfer carries, or 0 for any other packet. Its body is
    // zero-coded where that made it shorter, and its number is then coded too.
    U64 requestXferID(const std::vector<U8>& packet)
    {
        std::vector<U8> body = packet;
        if (!packet.empty() && (packet[0] & LLZeroCode::FLAG))
        {
            body.resize(NET_BUFFER_SIZE);
            bool overflow = false;
            body.resize(LLZeroCode::decode(packet.data(), (U32)packet.size(), body.data(), (U32)body.size(),
                                           LL_PACKET_ID_SIZE, overflow));
        }
        const bool is_request = body.size() >= 18 && body[6] == 0xFF && body[7] == 0xFF && body[8] == 0x00 && body[9] == 156;
        return is_request ? readLittleEndian<U64>(body, 10) : 0;
    }

    // The ID in the next RequestXfer the simulator's socket receives within a
    // second of each packet, passing over other packets, or 0 where none comes.
    U64 nextRequestedID(const SimulatorSocket& simulator)
    {
        std::vector<U8> packet = simulator.receive(1.f);
        while (!packet.empty() && requestXferID(packet) == 0)
        {
            packet = simulator.receive(1.f);
        }
        return packet.empty() ? 0 : requestXferID(packet);
    }

    S32 sCallbacks = 0;
    S32 sCallbackResult = 0;
    // What the requester does the first time it is told of a file.
    std::function<void()> sOnFirstCallback;

    void countCallback(void**, S32 result, LLExtStat)
    {
        ++sCallbacks;
        sCallbackResult = result;
        if (sOnFirstCallback)
        {
            const std::function<void()> first = std::move(sOnFirstCallback);
            sOnFirstCallback = nullptr;
            first();
        }
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
            sOnFirstCallback = nullptr;
        }

        ~XferSession()
        {
            sOnFirstCallback = nullptr;
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

        CHECK(sCallbacks == 1);
        CHECK(sCallbackResult == LL_ERR_EOF);
        CHECK_FALSE(LLFile::isfile(local.path()));

        const std::vector<U8> answer = session.simulator().receive(1.f);
        REQUIRE(isAbortXfer(answer));
        CHECK(readLittleEndian<U64>(answer, 10) == id);
        CHECK(readLittleEndian<S32>(answer, 18) == LL_ERR_EOF);

        // A packet that comes after is for a transfer no longer there.
        REQUIRE(session.deliver(id, 0, std::vector<U8>(8, 0x01)));
        CHECK(session.simulator().receive(0.05f).empty());
        CHECK(sCallbacks == 1);
    }

    // A file whose temporary file is in a folder that is not there, so it can
    // neither be made nor written. On the receive list, waiting its turn as
    // requestFile leaves a file.
    class FolderlessXfer : public LLXfer_File
    {
    public:
        FolderlessXfer()
            : LLXfer_File(-1)
        {
        }

        void loseTemporaryFolder() { mTempFilename = gDirUtilp->add(gDirUtilp->getTempFilename(), "folderless.tmp"); }
    };

    FolderlessXfer* addFolderless(XferSession& session, U64 id, const TempFile& local)
    {
        FolderlessXfer* xfer = new FolderlessXfer();
        xfer->initializeRequest(id, local.path(), "folderless.tmp", LL_PATH_CACHE, session.simulator().host(), false,
                                countCallback, nullptr);
        xfer->loseTemporaryFolder();
        session.manager().mReceiveList.push_back(xfer);
        return xfer;
    }

    TEST_CASE("LLXferManager: a file that cannot be written gives the file up", "[LLXferManager]")
    {
        XferSession session;
        REQUIRE(session.isReady());
        const TempFile local;

        constexpr U64 ID = 0x0123456789ABCDEFull;
        addFolderless(session, ID, local)->mStatus = e_LL_XFER_IN_PROGRESS;

        // The file's data goes to its temporary file once its buffer is full.
        const std::vector<U8> data(LL_XFER_LARGE_PAYLOAD, 0x5A);
        constexpr U32 MOST_PACKETS = 32;
        U32 packets = 0;
        while (session.find(ID) != nullptr && packets < MOST_PACKETS)
        {
            REQUIRE(session.deliver(ID, packets, data));
            ++packets;
        }
        CAPTURE(packets);
        CHECK(packets > 1);

        CHECK(session.find(ID) == nullptr);
        CHECK(sCallbacks == 1);
        CHECK(sCallbackResult == LL_ERR_CANNOT_OPEN_FILE);
        CHECK_FALSE(LLFile::isfile(local.path()));

        // Each packet before it was confirmed.
        std::vector<U8> answer = session.simulator().receive(1.f);
        while (isConfirmXferPacket(answer))
        {
            answer = session.simulator().receive(1.f);
        }
        REQUIRE(isAbortXfer(answer));
        CHECK(readLittleEndian<U64>(answer, 10) == ID);
        CHECK(readLittleEndian<S32>(answer, 18) == LL_ERR_CANNOT_OPEN_FILE);
    }

    constexpr U64 FIRST_UNSTARTABLE = 0x0123456789ABCD01ull;
    constexpr U64 SECOND_UNSTARTABLE = 0x0123456789ABCD02ull;

    TEST_CASE("LLXferManager: a file that cannot start gives the file up and the next one starts", "[LLXferManager]")
    {
        XferSession session;
        REQUIRE(session.isReady());
        session.manager().setMaxIncomingXfers(1);
        const TempFile unstartable_local;
        const TempFile next_local;
        addFolderless(session, FIRST_UNSTARTABLE, unstartable_local);

        const U64 next = session.request(next_local.path(), "next.tmp");

        CHECK(session.find(FIRST_UNSTARTABLE) == nullptr);
        CHECK(sCallbacks == 1);
        CHECK(sCallbackResult == -1);
        CHECK_FALSE(LLFile::isfile(unstartable_local.path()));
        REQUIRE(session.find(next) != nullptr);
        CHECK(session.find(next)->mStatus == e_LL_XFER_IN_PROGRESS);

        const std::vector<U8> answer = session.simulator().receive(1.f);
        REQUIRE(isAbortXfer(answer));
        CHECK(readLittleEndian<U64>(answer, 10) == FIRST_UNSTARTABLE);
        CHECK(readLittleEndian<S32>(answer, 18) == -1);
    }

    TEST_CASE("LLXferManager: a requester told of a file that cannot start may ask for another", "[LLXferManager]")
    {
        XferSession session;
        REQUIRE(session.isReady());
        const TempFile first_local;
        const TempFile second_local;
        const TempFile asked_local;
        const TempFile next_local;
        addFolderless(session, FIRST_UNSTARTABLE, first_local);
        addFolderless(session, SECOND_UNSTARTABLE, second_local);

        U64 asked = 0;
        sOnFirstCallback = [&] { asked = session.request(asked_local.path(), "asked.tmp"); };

        const U64 next = session.request(next_local.path(), "next.tmp");

        CHECK(session.find(FIRST_UNSTARTABLE) == nullptr);
        CHECK(session.find(SECOND_UNSTARTABLE) == nullptr);
        CHECK(sCallbacks == 2);
        REQUIRE(session.find(next) != nullptr);
        CHECK(session.find(next)->mStatus == e_LL_XFER_IN_PROGRESS);
        REQUIRE(asked != 0);
        REQUIRE(session.find(asked) != nullptr);
        CHECK(session.find(asked)->mStatus == e_LL_XFER_IN_PROGRESS);
    }

    TEST_CASE("LLXferManager: a requester told of a file that cannot start may give up another", "[LLXferManager]")
    {
        XferSession session;
        REQUIRE(session.isReady());
        const TempFile first_local;
        const TempFile second_local;
        const TempFile next_local;
        addFolderless(session, FIRST_UNSTARTABLE, first_local);
        addFolderless(session, SECOND_UNSTARTABLE, second_local);

        // The later of the two on the list is tried and told first.
        sOnFirstCallback = [&] { session.manager().abortRequestById(FIRST_UNSTARTABLE, -2); };

        session.request(next_local.path(), "next.tmp");

        CHECK(session.find(FIRST_UNSTARTABLE) == nullptr);
        CHECK(session.find(SECOND_UNSTARTABLE) == nullptr);
        CHECK(sCallbacks == 2);
        CHECK(sCallbackResult == -2);
    }

    TEST_CASE("LLXferManager: a requester told of a file that cannot start may ask for the same file again", "[LLXferManager]")
    {
        XferSession session;
        REQUIRE(session.isReady());
        const TempFile local;
        const TempFile next_local;
        addFolderless(session, FIRST_UNSTARTABLE, local);

        U64 again = 0;
        sOnFirstCallback = [&] { again = session.request(local.path(), "folderless.tmp"); };

        const U64 next = session.request(next_local.path(), "next.tmp");

        CHECK(session.find(FIRST_UNSTARTABLE) == nullptr);
        CHECK(sCallbacks == 1);
        REQUIRE(again != 0);
        CHECK(again != FIRST_UNSTARTABLE);
        REQUIRE(session.find(again) != nullptr);
        CHECK(session.find(again)->mStatus == e_LL_XFER_IN_PROGRESS);

        // The next file starts in the loop, and the one asked for as its
        // requester was told after it.
        CHECK(nextRequestedID(session.simulator()) == next);
        CHECK(nextRequestedID(session.simulator()) == again);
    }

    TEST_CASE("LLXferManager: a requester told of a file given up may ask for the same file again", "[LLXferManager]")
    {
        XferSession session;
        REQUIRE(session.isReady());
        const TempFile local;
        const U64 id = session.request(local.path());
        REQUIRE_FALSE(session.simulator().receive(1.f).empty());

        U64 again = 0;
        sOnFirstCallback = [&] { again = session.request(local.path()); };

        REQUIRE(session.deliver(id, 0, { 1, 2, 3 }));

        CHECK(session.find(id) == nullptr);
        REQUIRE(again != 0);
        CHECK(again != id);
        REQUIRE(session.find(again) != nullptr);
        CHECK(session.find(again)->mStatus == e_LL_XFER_IN_PROGRESS);
        CHECK(nextRequestedID(session.simulator()) == again);

        // The request made while told is a transfer of its own, told in turn.
        REQUIRE(session.deliver(again, 0, { 1, 2, 3 }));
        CHECK(session.find(again) == nullptr);
        CHECK(sCallbacks == 2);
    }

    TEST_CASE("LLXferManager: a requester told of a finished file may ask for the same file again", "[LLXferManager]")
    {
        XferSession session;
        REQUIRE(session.isReady());
        const TempFile local;
        const U64 id = session.request(local.path());
        REQUIRE_FALSE(session.simulator().receive(1.f).empty());

        U64 again = 0;
        sOnFirstCallback = [&] { again = session.request(local.path()); };

        REQUIRE(session.deliver(id, LAST_PACKET, { 0, 0, 0, 0 }));

        CHECK(session.find(id) == nullptr);
        CHECK(sCallbacks == 1);
        CHECK(sCallbackResult == LL_ERR_NOERR);
        REQUIRE(again != 0);
        CHECK(again != id);
        REQUIRE(session.find(again) != nullptr);
        CHECK(session.find(again)->mStatus == e_LL_XFER_IN_PROGRESS);
        CHECK(nextRequestedID(session.simulator()) == again);
    }

    TEST_CASE("LLXferManager: a file asked for again while it runs or waits stays one transfer", "[LLXferManager]")
    {
        XferSession session;
        REQUIRE(session.isReady());
        session.manager().setMaxIncomingXfers(1);
        const TempFile running_local;
        const TempFile waiting_local;
        const U64 running = session.request(running_local.path(), "running.tmp");
        const U64 waiting = session.request(waiting_local.path(), "waiting.tmp");
        REQUIRE(session.find(running)->mStatus == e_LL_XFER_IN_PROGRESS);
        REQUIRE(session.find(waiting)->mStatus == e_LL_XFER_PENDING);

        CHECK(session.request(running_local.path(), "running.tmp") == running);
        CHECK(session.request(waiting_local.path(), "waiting.tmp") == waiting);
        CHECK(session.manager().mReceiveList.size() == 2);
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
        CHECK(sCallbacks == 1);
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
