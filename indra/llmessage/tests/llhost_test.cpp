/**
 * @file llhost_test.cpp
 * @author Adroit
 * @date 2007-02
 * @brief llhost test cases.
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

#include "linden_common.h"

#include "../llhost.h"

#include <catch2/catch_test_macros.hpp>

namespace
{

    TEST_CASE("LLHost: a default host is zero and not ok", "[LLHost]")
    {
        LLHost host;
        CHECK(host.getAddress() == 0);
        CHECK(host.getPort() == 0);
        CHECK_FALSE(host.isOk());
    }
    TEST_CASE("LLHost: constructing from an address and port sets both and is ok", "[LLHost]")
    {
        U32 ip_addr = 0xc098017d;
        U32 port = 8080;
        LLHost host(ip_addr, port);
        CHECK(ip_addr == host.getAddress());
        CHECK(port == host.getPort());
        CHECK(host.isOk());
    }

    TEST_CASE("LLHost: constructing from a dotted string and port", "[LLHost]")
    {
        const char* str = "192.168.1.1";
        U32 port = 8080;
        LLHost host(str, port);
        CHECK(host.getAddress() == ip_string_to_u32(str));
        CHECK(port == host.getPort());
    }

    TEST_CASE("LLHost: constructing from a U64 packing address and port", "[LLHost]")
    {
        U32 ip = ip_string_to_u32("192.168.1.1");
        U32 port = 22;
        U64 ip_port = (((U64) ip) << 32) | port;
        LLHost host(ip_port);
        CHECK(ip == host.getAddress());
        CHECK(port == host.getPort());
    }

    TEST_CASE("LLHost: constructing from an IP:port string", "[LLHost]")
    {
        std::string ip_port_string = "192.168.1.1:8080";
        U32 ip = ip_string_to_u32("192.168.1.1");
        U32 port = 8080;

        LLHost host(ip_port_string);
        CHECK(ip == host.getAddress());
        CHECK(port == host.getPort());
    }

    TEST_CASE("LLHost: set takes an address and port", "[LLHost]")
    {
        U32 ip = 0xc098017d, port = 8080;
        LLHost host;
        host.set(ip,port);
        CHECK(ip == host.getAddress());
        CHECK(port == host.getPort());
    }

    TEST_CASE("LLHost: setAddress and setPort each change only their own part", "[LLHost]")
    {
        const char* str = "192.168.1.1";
        U32 port = 8080, ip;
        LLHost host;
        host.set(str,port);
        ip = ip_string_to_u32(str);
        CHECK(ip == host.getAddress());
        CHECK(port == host.getPort());

        str = "64.233.187.99";
        ip = ip_string_to_u32(str);
        host.setAddress(str);
        CHECK(ip == host.getAddress());

        ip = 0xc098017b;
        host.setAddress(ip);
        CHECK(ip == host.getAddress());
        // should still use the old port
        CHECK(port == host.getPort());

        port = 8084;
        host.setPort(port);
        CHECK(port == host.getPort());
        // should still use the old address
        CHECK(ip == host.getAddress());
    }

    TEST_CASE("LLHost: getIPString and getIPandPort format the address", "[LLHost]")
    {
        const std::string str("192.168.1.1");
        U32 port = 8080;
        LLHost host;
        host.set(str,port);

        std::string ip_string = host.getIPString();
        CHECK(ip_string == str);

        std::string ip_string_port = host.getIPandPort();
        CHECK(ip_string_port == "192.168.1.1:8080");
    }


//  getHostName()  and setHostByName
    TEST_CASE("LLHost: getHostName reports the name given to setHostByName", "[LLHost]")
    {
        SKIP("this test is irreparably flaky");
//      skip("setHostByName(\"google.com\"); getHostName() -> (e.g.) \"yx-in-f100.1e100.net\"");
        // nat: is it reasonable to expect LLHost::getHostName() to echo
        // back something resembling the string passed to setHostByName()?
        //
        // If that's not even reasonable, would a round trip in the /other/
        // direction make more sense? (Call getHostName() for something with
        // known IP address; call setHostByName(); verify IP address)
        //
        // Failing that... is there a plausible way to test getHostName() and
        // setHostByName()? Hopefully without putting up a dummy local DNS
        // server?

        // monty: If you don't control the DNS server or the DNS configuration
        // for the test point then, no, none of these will necessarily be
        // reliable and may start to fail at any time. Forward translation
        // is subject to CNAME records and round-robin address assignment.
        // Reverse lookup is 1-to-many and is more and more likely to have
        // nothing to do with the forward translation.
        //
        // So the test is increasingly meaningless on a real network.

        std::string hostStr = "lindenlab.com";
        LLHost host;
        host.setHostByName(hostStr);

        // reverse DNS will likely result in appending of some
        // sub-domain to the main hostname. so look for
        // the main domain name and not do the exact compare

        std::string hostname = host.getHostName();
        try
        {
            UNSCOPED_INFO("getHostName failed");
            REQUIRE(hostname.find(hostStr) != std::string::npos);
        }
        catch (const std::exception&)
        {
            std::cerr << "set '" << hostStr << "'; reported '" << hostname << "'" << std::endl;
            throw;
        }
    }

//  setHostByName for dotted IP
    TEST_CASE("LLHost: setHostByName takes a dotted IP address", "[LLHost]")
    {
        std::string hostStr = "64.233.167.99";
        LLHost host;
        host.setHostByName(hostStr);
        CHECK(host.getAddress() == ip_string_to_u32(hostStr.c_str()));
    }

    TEST_CASE("LLHost: a copy keeps address and port", "[LLHost]")
    {
        LLHost host1(0xc098017d, 8080);
        LLHost host2 = host1;
        CHECK(host1.getAddress() == host2.getAddress());
        CHECK(host1.getPort() == host2.getPort());
    }

    TEST_CASE("LLHost: operator<< writes IP:port", "[LLHost]")
    {
        LLHost host1("192.168.1.1", 8080);
        std::string str1 = "192.168.1.1:8080";
        std::ostringstream stream;
        stream << host1;
        CHECK(stream.str()== str1);

        // There is no istream >> llhost operator.
        //std::istringstream is(stream.str());
        //LLHost host2;
        //is >> host2;
        //ensure("Operator >> failed. Not compatible with <<", host1 == host2);
    }

    // operators ==, !=, <
    TEST_CASE("LLHost: == and != and < compare address and port", "[LLHost]")
    {
        U32 ip_addr = 0xc098017d;
        U32 port = 8080;
        LLHost host1(ip_addr, port);
        LLHost host2(ip_addr, port);
        CHECK(host1 == host2);

        // change port
        host2.setPort(7070);
        CHECK(host1 != host2);

        // set port back to 8080 and change IP address now
        host2.setPort(8080);
        host2.setAddress(ip_addr+10);
        CHECK(host1 != host2);

        CHECK(host1 < host2);

        // set IP address back to same value and change port
        host2.setAddress(ip_addr);
        host2.setPort(host1.getPort() + 10);
        CHECK(host1 < host2);
    }

    // invalid ip address string
    TEST_CASE("LLHost: isOk tells valid address strings from invalid ones", "[LLHost]")
    {
        LLHost host1("10.0.1.2", 6143);
        UNSCOPED_INFO("10.0.1.2 is a valid address");
        CHECK(host1.isOk());

        LLHost host2("booger-brains", 6143);
        UNSCOPED_INFO("booger-brains is not an address");
        CHECK_FALSE(host2.isOk());

        LLHost host3("255.255.255.255", 6143);
        UNSCOPED_INFO("255.255.255.255 is the valid broadcast address");
        CHECK(host3.isOk());
    }
}
