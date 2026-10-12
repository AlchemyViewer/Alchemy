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
}

