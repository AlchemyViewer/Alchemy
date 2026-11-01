/**
 * @file message_test.cpp
 * @date 2007-04
 * @brief LLMessageSystem test cases.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include <catch2/catch_test_macros.hpp>
#include "linden_common.h"
#include "llhttpconstants.h"
#include "llmessageconfig.h"
#include "llmessagesystem_fixture.h"
#include "llsdserialize.h"
#include "message.h"
#include "message_prehash.h"
#include "net.h"

#if !LL_WINDOWS
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#endif

namespace ll_test
{
    // What the message system does with a circuit's acks as packets come in
    // from its host.
    struct CircuitProbe
    {
        // A reliable packet came in, to be acked.
        static void receiveReliable(LLCircuitData& circuit, TPACKETID id)
        {
            circuit.collectRAck(id);
        }

        static size_t acksWaiting(const LLCircuitData& circuit)
        {
            return circuit.mAcks.size();
        }
    };
}

namespace
{
    struct Response : public LLHTTPNode::Response
    {
        virtual void result(const LLSD&) {}
        virtual void status(S32 code, const std::string& message)
        {
            mStatus = code;
        }
        virtual void extendedResult(S32 code, const std::string& message, const LLSD& headers) { }
        virtual void extendedResult(S32 code, const LLSD& result, const LLSD& headers) { }
        S32 mStatus = 0; // no status() yet
    };
}

namespace
{
    struct LLMessageSystemTestData
    {
        // currently test disconnected message system
        ll_test::MessageSystemScope mMessageSystem;
        std::string mTestConfigDir;
        std::string mSep;

        LLMessageSystemTestData()
        {
            // generate temp dir
            std::ostringstream ostr;
#if LL_WINDOWS
            mSep = "\\";
            ostr << "C:" << mSep;
#else
            mSep = "/";
            ostr << mSep << "tmp" << mSep;
#endif
            LLUUID random;
            random.generate();
            ostr << "message-test-" << random;
            mTestConfigDir = ostr.str();
            LLFile::mkdir(mTestConfigDir);
            writeConfigFile(LLSD());
            LLMessageConfig::initClass("simulator", ostr.str());
        }

        // A destructor cannot fail a test, so the clean-up only says what
        // it could not remove.
        ~LLMessageSystemTestData()
        {
            // rm contents of temp dir
            std::ostringstream ostr;
            ostr << mTestConfigDir << mSep << "message.xml";
            if (LLFile::remove(ostr.str()) != 0)
            {
                LL_WARNS() << "Could not remove " << ostr.str() << LL_ENDL;
            }

            // rm temp dir
            if (LLFile::remove(mTestConfigDir) != 0)
            {
                LL_WARNS() << "Could not remove " << mTestConfigDir << LL_ENDL;
            }
        }

        void writeConfigFile(const LLSD& config)
        {
            std::string ostr(mTestConfigDir + mSep + "message.xml");
            llofstream file(ostr.c_str());
            if (file.is_open())
            {
                LLSDSerialize::toPrettyXML(config, file);
            }
            file.close();
        }
    };


    TEST_CASE_METHOD(LLMessageSystemTestData, "LLMessageSystem: dispatch of an unknown message answers HTTP_NOT_FOUND", "[LLMessageSystem]")
        // dispatch unknown message
    {
        const char* name = "notamessasge";
        const LLSD message;
        const LLPointer<Response> response = new Response();
        gMessageSystem->dispatch(name, message, response);
        CHECK(response->mStatus == HTTP_NOT_FOUND);
    }

    TEST_CASE_METHOD(LLMessageSystemTestData, "LLMessageSystem: a system whose template failed to load still closes its socket", "[LLMessageSystem]")
        // a system whose template failed to load still closes its socket
    {
        U32 port = 0;
        {
            // The template loads before the socket opens, so a system
            // without one is not OK, and has a socket all the same.
            LLMessageSystem system("notafile", NET_USE_OS_ASSIGNED_PORT, 1, 0, 0, false, 5.f, 100.f);
            UNSCOPED_INFO("not OK without its template");
            CHECK_FALSE(system.isOK());
            CHECK(system.mSocket >= 0);
            port = system.getListenPort();
        }
        // Its port is free again: start_net binds it, rather than moving on
        // to the discovery range as it does from a port in use.
        S32 socket = -1;
        int bound = port;
        UNSCOPED_INFO("start_net binds");
        CHECK(start_net(socket, bound) == 0);
        end_net(socket);
        UNSCOPED_INFO("the port the system had");
        CHECK(bound == (int)port);
        UNSCOPED_INFO("and end_net leaves no socket");
        CHECK(socket == -1);
    }

    // A message template file of the test's own, there while this lives.
    class TemplateFile
    {
    public:
        TemplateFile(const std::string& path, const char* text)
            : mPath(path)
        {
            llofstream file(mPath.c_str());
            file << text;
        }

        ~TemplateFile()
        {
            if (LLFile::remove(mPath) != 0)
            {
                LL_WARNS() << "Could not remove " << mPath << LL_ENDL;
            }
        }

        TemplateFile(const TemplateFile&) = delete;
        TemplateFile& operator=(const TemplateFile&) = delete;

    private:
        std::string mPath;
    };

    // One message, AckBudgetTest, whose one block holds bytes of any length:
    // its packet is the 6-byte header, the 4-byte number of a Low message,
    // the 2-byte length and the bytes.
    const char* const ACK_BUDGET_TEMPLATE =
        "version 2.0\n"
        "{\n"
        "    AckBudgetTest Low 1 NotTrusted Unencoded\n"
        "    {\n"
        "        Payload Single\n"
        "        { Data Variable 2 }\n"
        "    }\n"
        "}\n";

    // gMessageSystem is `system` while this lives, and what it was before
    // once it ends.
    class UseMessageSystem
    {
    public:
        explicit UseMessageSystem(LLMessageSystem& system)
            : mPrior(gMessageSystem)
        {
            gMessageSystem = &system;
        }

        ~UseMessageSystem()
        {
            gMessageSystem = mPrior;
        }

        UseMessageSystem(const UseMessageSystem&) = delete;
        UseMessageSystem& operator=(const UseMessageSystem&) = delete;

    private:
        LLMessageSystem* mPrior;
    };

    TEST_CASE_METHOD(LLMessageSystemTestData, "LLMessageSystem: acks appended to a packet fit in the MTU with their count", "[LLMessageSystem]")
    {
        const std::string template_path = mTestConfigDir + mSep + "message_template.msg";
        const TemplateFile message_template(template_path, ACK_BUDGET_TEMPLATE);
        LLMessageSystem system(template_path, NET_USE_OS_ASSIGNED_PORT, 1, 0, 0, false, 5.f, 100.f);
        REQUIRE(system.isOK());
        const UseMessageSystem use(system);

        // Packets go to the system's own socket, which reads none of them.
        const LLHost host("127.0.0.1", system.getListenPort());
        system.enableCircuit(host, false);
        LLCircuitData* circuit = system.mCircuitInfo.findCircuit(host);
        REQUIRE(circuit != nullptr);

        // Sends AckBudgetTest holding `data_size` bytes, and says how long
        // its packet was.
        auto send = [&](S32 data_size)
        {
            const std::vector<U8> data(data_size, 0x5a);
            system.newMessage("AckBudgetTest");
            system.nextBlock("Payload");
            system.addBinaryData("Data", data.data(), data_size);
            return system.sendMessage(host);
        };

        UNSCOPED_INFO("the message alone");
        CHECK(send(1184) == 1196);

        for (TPACKETID id = 1; id <= 30; ++id)
        {
            ll_test::CircuitProbe::receiveReliable(*circuit, id);
        }

        {
            INFO("4 bytes to the MTU hold an ack but not the count after it");
            CHECK(send(1184) == 1196);
            CHECK(ll_test::CircuitProbe::acksWaiting(*circuit) == 30);
        }
        {
            INFO("5 bytes hold one and its count");
            CHECK(send(1183) == MTUBYTES);
            CHECK(ll_test::CircuitProbe::acksWaiting(*circuit) == 29);
        }
        {
            INFO("100 bytes hold 24 and their count");
            CHECK(send(1088) == 1100 + 24 * 4 + 1);
            CHECK(ll_test::CircuitProbe::acksWaiting(*circuit) == 5);
        }
    }

    // The viewer opens every circuit to a simulator with no circuit code, as
    // EnableSimulator does, and CloseCircuit, DisableSimulator and a region's
    // retirement disable it.
    TEST_CASE_METHOD(LLMessageSystemTestData, "LLMessageSystem: disableCircuit removes a circuit opened without a circuit code", "[LLMessageSystem]")
    {
        const LLHost host("127.0.0.1", 13000);
        gMessageSystem->enableCircuit(host, true);
        REQUIRE(gMessageSystem->mCircuitInfo.findCircuit(host) != nullptr);
        REQUIRE(gMessageSystem->findCircuitCode(host) == 0);

        gMessageSystem->disableCircuit(host);
        CHECK(gMessageSystem->mCircuitInfo.findCircuit(host) == nullptr);

        // A later EnableSimulator opens it again.
        gMessageSystem->enableCircuit(host, true);
        const LLCircuitData* reopened = gMessageSystem->mCircuitInfo.findCircuit(host);
        REQUIRE(reopened != nullptr);
        CHECK(reopened->isAlive());
        CHECK(reopened->getTrusted());
    }

    TEST_CASE_METHOD(LLMessageSystemTestData, "LLMessageSystem: disableCircuit removes a circuit and the circuit code it has", "[LLMessageSystem]")
    {
        const LLHost host("127.0.0.1", 13001);
        const U32 code = 1234;
        const U64 ip_port = ((U64)host.getAddress() << 32) | host.getPort();
        LLUUID session_id;
        session_id.generate();
        REQUIRE(gMessageSystem->addCircuitCode(code, session_id));
        gMessageSystem->mIPPortToCircuitCode[ip_port] = code;
        gMessageSystem->mCircuitCodeToIPPort[code] = ip_port;
        gMessageSystem->enableCircuit(host, false);
        REQUIRE(gMessageSystem->findCircuitCode(host) == code);

        gMessageSystem->disableCircuit(host);
        CHECK(gMessageSystem->mCircuitInfo.findCircuit(host) == nullptr);
        CHECK(gMessageSystem->findCircuitCode(host) == 0);
        CHECK(gMessageSystem->mCircuitCodeToIPPort.count(code) == 0);
    }

#if !LL_WINDOWS
    TEST_CASE_METHOD(LLMessageSystemTestData, "LLMessageSystem: start_net closes its socket when it finds no port to bind", "[LLMessageSystem]")
        // start_net closes the socket it made when it finds no port to bind
    {
        // Every port start_net would try is taken: the one it is asked for
        // is the first of the discovery range, where it goes on to look.
        std::vector<int> held;
        for (U32 port = PORT_DISCOVERY_RANGE_MIN; port <= PORT_DISCOVERY_RANGE_MAX; ++port)
        {
            const int holder = ::socket(AF_INET, SOCK_DGRAM, 0);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_ANY);
            address.sin_port = htons(port);
            if (holder >= 0 && ::bind(holder, (sockaddr*)&address, sizeof(address)) == 0)
            {
                held.push_back(holder);
            }
            else if (holder >= 0)
            {
                // In use by something else, which takes it from start_net too.
                ::close(holder);
            }
        }

        // A new descriptor is the lowest free one, so the one before
        // start_net is the one after, unless start_net left one open.
        const int before = ::open("/dev/null", O_RDONLY);
        ::close(before);
        S32 socket = -1;
        int port = PORT_DISCOVERY_RANGE_MIN;
        const S32 result = start_net(socket, port);
        const int after = ::open("/dev/null", O_RDONLY);
        ::close(after);
        for (int holder : held)
        {
            ::close(holder);
        }

        UNSCOPED_INFO("no port to bind");
        CHECK(result == 3);
        CHECK(socket == -1);
        UNSCOPED_INFO("no descriptor left open");
        CHECK(after == before);
    }
#endif

    // One message, DuplicateTest, whose one block holds a U32.
    const char* const DUPLICATE_TEST_TEMPLATE =
        "version 2.0\n"
        "{\n"
        "    DuplicateTest Low 1 NotTrusted Unencoded\n"
        "    {\n"
        "        Payload Single\n"
        "        { Data U32 }\n"
        "    }\n"
        "}\n";

    // A socket of the test's own, closed when this ends.
    class SenderSocket
    {
    public:
        SenderSocket()
        {
            mPort = NET_USE_OS_ASSIGNED_PORT;
            mResult = start_net(mSocket, mPort);
        }

        ~SenderSocket()
        {
            end_net(mSocket);
        }

        SenderSocket(const SenderSocket&) = delete;
        SenderSocket& operator=(const SenderSocket&) = delete;

        S32 mSocket = -1;
        int mPort = 0;
        S32 mResult = -1;
    };

    S32 sDuplicateTestsHandled = 0;

    void handle_duplicate_test(LLMessageSystem*, void**)
    {
        ++sDuplicateTestsHandled;
    }

    // Reads what has come in until the system has taken `packets` packets
    // in all, or a second has passed.
    void read_until(LLMessageSystem& system, U32 packets)
    {
        LockMessageChecker lmc(&system);
        const F64 give_up = LLTimer::getTotalSeconds() + 1.0;
        while (system.mPacketsIn < packets && LLTimer::getTotalSeconds() < give_up)
        {
            if (!lmc.checkMessages())
            {
                ms_sleep(1);
            }
        }
    }

    TEST_CASE_METHOD(LLMessageSystemTestData, "LLMessageSystem: a reliable packet that arrives twice unmarked is handled once", "[LLMessageSystem]")
    {
        const std::string template_path = mTestConfigDir + mSep + "message_template.msg";
        const TemplateFile message_template(template_path, DUPLICATE_TEST_TEMPLATE);
        LLMessageSystem system(template_path, NET_USE_OS_ASSIGNED_PORT, 1, 0, 0, false, 5.f, 100.f);
        REQUIRE(system.isOK());
        const UseMessageSystem use(system);
        system.setHandlerFunc("DuplicateTest", handle_duplicate_test);
        sDuplicateTestsHandled = 0;

        // The packets come from a socket of the test's own, on a circuit the
        // system has to it, as it has to a simulator.
        const SenderSocket sender;
        REQUIRE(sender.mResult == 0);
        system.enableCircuit(LLHost("127.0.0.1", sender.mPort), true);
        const LLHost receiver("127.0.0.1", system.getListenPort());

        // DuplicateTest numbered `id`: the flags, the packet number, no extra
        // header, the number of Low message 1 and its U32.
        auto send = [&](U8 flags, U8 id)
        {
            const U8 packet[] = { flags, 0, 0, 0, id, 0, 0xff, 0xff, 0x00, 0x01, 1, 2, 3, 4 };
            REQUIRE(send_packet(sender.mSocket, (const char*)packet, (int)sizeof(packet), receiver.getAddress(), receiver.getPort()));
        };

        {
            INFO("both copies in before either is read");
            send(LL_RELIABLE_FLAG, 1);
            send(LL_RELIABLE_FLAG, 1);
            read_until(system, 2);
            REQUIRE(system.mPacketsIn == 2);
            CHECK(sDuplicateTestsHandled == 1);
        }
        {
            INFO("the second copy in after the first is handled");
            send(LL_RELIABLE_FLAG, 2);
            read_until(system, 3);
            send(LL_RELIABLE_FLAG, 2);
            read_until(system, 4);
            REQUIRE(system.mPacketsIn == 4);
            CHECK(sDuplicateTestsHandled == 2);
        }
        {
            INFO("a copy marked resent, as before");
            send(LL_RELIABLE_FLAG | LL_RESENT_FLAG, 2);
            read_until(system, 5);
            REQUIRE(system.mPacketsIn == 5);
            CHECK(sDuplicateTestsHandled == 2);
        }
    }
}

