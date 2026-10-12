/**
 * @file lldatapacker_tut.cpp
 * @date 2007-04
 * @brief LLDataPacker test cases.
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

#include <tut/tut.hpp>
#include "linden_common.h"
#include "lltut.h"
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
        S32 mStatus;
    };
}

namespace tut
{
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

    typedef test_group<LLMessageSystemTestData> LLMessageSystemTestGroup;
    typedef LLMessageSystemTestGroup::object        LLMessageSystemTestObject;
    LLMessageSystemTestGroup messageTestGroup("LLMessageSystem");
}

    template<> template<>
    void LLMessageSystemTestObject::test<1>()
        // dispatch unknown message
    {
        const char* name = "notamessasge";
        const LLSD message;
        const LLPointer<Response> response = new Response();
        gMessageSystem->dispatch(name, message, response);
        ensure_equals(response->mStatus, HTTP_NOT_FOUND);
    }

    template<> template<>
    void LLMessageSystemTestObject::test<2>()
        // a system whose template failed to load still closes its socket
    {
        U32 port = 0;
        {
            // The template loads before the socket opens, so a system
            // without one is not OK, and has a socket all the same.
            LLMessageSystem system("notafile", NET_USE_OS_ASSIGNED_PORT, 1, 0, 0, false, 5.f, 100.f);
            ensure("not OK without its template", !system.isOK());
            ensure("its socket opened", system.mSocket >= 0);
            port = system.getListenPort();
        }
        // Its port is free again: start_net binds it, rather than moving on
        // to the discovery range as it does from a port in use.
        S32 socket = -1;
        int bound = port;
        ensure_equals("start_net binds", start_net(socket, bound), 0);
        end_net(socket);
        ensure_equals("the port the system had", bound, (int)port);
        ensure_equals("and end_net leaves no socket", socket, -1);
    }

#if !LL_WINDOWS
    template<> template<>
    void LLMessageSystemTestObject::test<3>()
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

        ensure_equals("no port to bind", result, 3);
        ensure_equals("and no socket returned", socket, -1);
        ensure_equals("nor one left open", after, before);
    }
#endif
}

