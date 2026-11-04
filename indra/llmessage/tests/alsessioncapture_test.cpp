/**
 * @file alsessioncapture_test.cpp
 * @brief The session recording's file: its line, its framed records and what they hold.
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

#include <catch2/catch_test_macros.hpp>

#if AL_SESSION_CAPTURE

#include "../alsessioncapture.h"

#include "bufferarray.h"
#include "httpresponse.h"
#include "llapp.h"
#include "llhost.h"
#include "llsd.h"
#include "llsdserialize.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr const char* DIRECTORY_VARIABLE = "ALCHEMY_SESSION_CAPTURE_DIR";

    // Sets a variable, or with an empty value removes it.
    void setVariable(const char* name, const std::string& value)
    {
#if LL_WINDOWS
        _putenv_s(name, value.c_str());
#else
        if (value.empty())
        {
            unsetenv(name);
        }
        else
        {
            setenv(name, value.c_str(), 1);
        }
#endif
    }

    // A directory of the case's own, named in the environment while the case
    // runs; the recording ended, and the directory and the variable gone,
    // after it.
    struct capture_data
    {
        std::filesystem::path mDirectory;

        capture_data()
        {
            static int sCases = 0;
            mDirectory = std::filesystem::temp_directory_path()
                / ("alsessioncapture_test-" + std::to_string(LLApp::getPid()) + "-" + std::to_string(++sCases));
            std::filesystem::create_directories(mDirectory);
            setVariable(DIRECTORY_VARIABLE, mDirectory.string());
        }

        ~capture_data()
        {
            ALSessionCapture::deleteSingleton();
            setVariable(DIRECTORY_VARIABLE, "");
            std::error_code ec;
            std::filesystem::remove_all(mDirectory, ec);
        }

        std::vector<std::filesystem::path> files() const
        {
            std::vector<std::filesystem::path> found;
            for (const auto& entry : std::filesystem::directory_iterator(mDirectory))
            {
                found.push_back(entry.path());
            }
            return found;
        }

        // The one recording the directory holds: its first line, and each
        // record after it.
        std::vector<LLSD> records(std::string& line) const
        {
            const std::vector<std::filesystem::path> recordings = files();
            REQUIRE(recordings.size() == 1);
            std::ifstream file(recordings.front(), std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            const size_t end = bytes.find('\n');
            REQUIRE(end != std::string::npos);
            line = bytes.substr(0, end + 1);

            std::vector<LLSD> read;
            for (size_t at = end + 1; at < bytes.size();)
            {
                REQUIRE(bytes.size() - at >= 4);
                const auto byte = [&](size_t offset) { return U32(U8(bytes[offset])); };
                const size_t size = (byte(at) << 24) | (byte(at + 1) << 16) | (byte(at + 2) << 8) | byte(at + 3);
                at += 4;
                REQUIRE(bytes.size() - at >= size);
                std::istringstream stream(bytes.substr(at, size));
                LLSD record;
                REQUIRE(LLSDSerialize::fromBinary(record, stream, (llssize)size) > 0);
                read.push_back(record);
                at += size;
            }
            return read;
        }
    };

    LLSD::Binary binary(std::initializer_list<U8> bytes)
    {
        return LLSD::Binary(bytes);
    }

    TEST_CASE_METHOD(capture_data, "alsessioncapture: a recording is its line then a framed record of each thing met", "[alsessioncapture]")
    {
        LLSD viewer;
        viewer["channel"] = "Alchemy Test";
        ALSessionCapture::startFromEnvironment(viewer);
        REQUIRE(ALSessionCapture::capturing());

        // A zero-coded reliable message carrying two acks: the wire's flags
        // byte, which the decoded copy of a message sent lacks, and the acks'
        // IDs then their count after the message.
        const U8 wire[] = { 0xD0, 0, 0, 0, 7, 0, 0xFF, 0x01, 0x00, 0x03, 0, 0, 0, 1, 0, 0, 1, 2, 2 };
        const U8 decoded[] = { 0x00, 0, 0, 0, 7, 0, 0xFF, 0x01, 0x00, 0x00, 0x00 };
        ALSessionCapture::instance().recordPacket(true, LLHost("127.0.0.1", 13005), wire, (S32)sizeof(wire),
                                                  decoded, (S32)sizeof(decoded), false);

        LLCore::HttpResponse* response = new LLCore::HttpResponse();
        response->setStatus(LLCore::HttpStatus(200));
        response->setRequestURL("https://simhost.example:12043/cap/seed");
        response->setRequestMethod("POST");
        response->setContentType("application/llsd+xml");
        LLCore::BufferArray* body = new LLCore::BufferArray();
        body->append("<llsd><map /></llsd>", 20);
        response->setBody(body);
        body->release();
        ALSessionCapture::instance().recordHttp(*response);
        response->release();

        LLSD event_body;
        event_body["Info"]["Count"] = 3;
        ALSessionCapture::instance().recordEvent("127.0.0.1:13005", "EnableSimulator", event_body);

        ALSessionCapture::deleteSingleton();

        std::string line;
        const std::vector<LLSD> read = records(line);
        CHECK(line == "ALSESSIONCAPTURE 1\n");
        REQUIRE(read.size() == 4);

        CHECK(read[0]["type"].asString() == "capture");
        CHECK(read[0]["version"].asInteger() == 1);
        CHECK(read[0]["viewer"]["channel"].asString() == "Alchemy Test");
        CHECK(read[0]["time"].asReal() > 0.0);

        CHECK(read[1]["type"].asString() == "packet");
        CHECK(read[1]["direction"].asString() == "out");
        CHECK(read[1]["host"].asString() == "127.0.0.1:13005");
        CHECK(read[1]["bytes"].asBinary() == binary({ 0xD0, 0, 0, 0, 7, 0, 0xFF, 0x01, 0x00, 0x00, 0x00 }));
        REQUIRE(read[1]["acks"].size() == 2);
        CHECK(read[1]["acks"][0].asInteger() == 1);
        CHECK(read[1]["acks"][1].asInteger() == 0x102);
        CHECK(read[1]["wire_size"].asInteger() == (S32)sizeof(wire));
        CHECK_FALSE(read[1].has("injected"));

        CHECK(read[2]["type"].asString() == "http");
        CHECK(read[2]["method"].asString() == "POST");
        CHECK(read[2]["url"].asString() == "https://simhost.example:12043/cap/seed");
        CHECK(read[2]["status"].asInteger() == 200);
        CHECK(read[2]["success"].asBoolean());
        CHECK(read[2]["content_type"].asString() == "application/llsd+xml");
        const LLSD::Binary http_body = read[2]["body"].asBinary();
        CHECK(std::string(http_body.begin(), http_body.end()) == "<llsd><map /></llsd>");

        CHECK(read[3]["type"].asString() == "event");
        CHECK(read[3]["sender"].asString() == "127.0.0.1:13005");
        CHECK(read[3]["message"].asString() == "EnableSimulator");
        CHECK(read[3]["body"]["Info"]["Count"].asInteger() == 3);
    }

    TEST_CASE_METHOD(capture_data, "alsessioncapture: a login is recorded with no password or MFA hash", "[alsessioncapture]")
    {
        ALSessionCapture::startFromEnvironment(LLSD::emptyMap());
        REQUIRE(ALSessionCapture::capturing());

        LLSD request;
        request["uri"] = "https://login.example/cgi-bin/login.cgi";
        request["method"] = "login_to_simulator";
        request["params"]["first"] = "Resident";
        request["params"]["passwd"] = "$1$0123456789abcdef0123456789abcdef";
        request["params"]["mfa_hash"] = "fedcba9876543210";
        LLSD response;
        response["status"] = "Complete";
        response["responses"]["login"] = "true";
        response["responses"]["circuit_code"] = 1234;
        ALSessionCapture::instance().recordLogin(request, response);
        ALSessionCapture::deleteSingleton();

        std::string line;
        const std::vector<LLSD> read = records(line);
        REQUIRE(read.size() == 2);
        CHECK(read[1]["type"].asString() == "login");
        CHECK(read[1]["request"]["uri"].asString() == "https://login.example/cgi-bin/login.cgi");
        CHECK(read[1]["request"]["params"]["first"].asString() == "Resident");
        CHECK_FALSE(read[1]["request"]["params"].has("passwd"));
        CHECK_FALSE(read[1]["request"]["params"].has("mfa_hash"));
        CHECK(read[1]["response"]["responses"]["circuit_code"].asInteger() == 1234);

        // Nor anywhere in the file.
        std::ifstream file(files().front(), std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CHECK(bytes.find("0123456789abcdef0123456789abcdef") == std::string::npos);
        CHECK(bytes.find("fedcba9876543210") == std::string::npos);
    }

    TEST_CASE_METHOD(capture_data, "alsessioncapture: nothing is recorded unless the variable names a directory", "[alsessioncapture]")
    {
        setVariable(DIRECTORY_VARIABLE, "");
        ALSessionCapture::startFromEnvironment(LLSD::emptyMap());
        CHECK_FALSE(ALSessionCapture::capturing());

        // A file is not a directory.
        const std::filesystem::path file = mDirectory / "not-a-directory";
        std::ofstream(file) << "x";
        setVariable(DIRECTORY_VARIABLE, file.string());
        ALSessionCapture::startFromEnvironment(LLSD::emptyMap());
        CHECK_FALSE(ALSessionCapture::capturing());
        CHECK(files().size() == 1);
    }

    TEST_CASE_METHOD(capture_data, "alsessioncapture: a message received keeps its flags and acks and marks an injected one", "[alsessioncapture]")
    {
        ALSessionCapture::startFromEnvironment(LLSD::emptyMap());
        REQUIRE(ALSessionCapture::capturing());

        // An ack count past the message reads no acks.
        const U8 wire[] = { 0x10, 0, 0, 0, 9, 0, 0xFF, 0x05, 9 };
        ALSessionCapture::instance().recordPacket(false, LLHost("127.0.0.1", 13006), wire, (S32)sizeof(wire), wire,
                                                  6 + 2, false);
        // Unacked, from the message builder.
        const U8 injected[] = { 0x00, 0, 0, 0, 1, 0, 0xFF, 0x06 };
        ALSessionCapture::instance().recordPacket(false, LLHost("127.0.0.1", 13006), injected,
                                                  (S32)sizeof(injected), injected, (S32)sizeof(injected), true);
        ALSessionCapture::deleteSingleton();

        std::string line;
        const std::vector<LLSD> read = records(line);
        REQUIRE(read.size() == 3);
        CHECK(read[1]["direction"].asString() == "in");
        CHECK(read[1]["bytes"].asBinary() == binary({ 0x10, 0, 0, 0, 9, 0, 0xFF, 0x05 }));
        CHECK(read[1]["acks"].size() == 0);
        CHECK(read[2]["injected"].asBoolean());
        CHECK(read[2]["acks"].size() == 0);
    }
}

#else

// Nothing to test in a build the recording is compiled out of, but the test
// binary still has to link and report success.
namespace
{
    TEST_CASE("alsessioncapture: compiled out of Release builds", "[alsessioncapture]")
    {
        CHECK(true);
    }
}

#endif // AL_SESSION_CAPTURE
