/**
 * @file llversioninfo_test.cpp
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
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

#include <catch2/catch_test_macros.hpp>

#include "../llversioninfo.h"

#include "llversioninfovars.h"

 #include <iostream>

// LL_VIEWER_CHANNEL is a macro defined on the compiler command line. The
// macro expands to the string name of the channel, but without quotes. We
// need to turn it into a quoted string. LL_TO_STRING() does that.
#define ll_viewer_channel LL_TO_STRING(LL_VIEWER_CHANNEL)

namespace
{
    struct versioninfo
    {
        versioninfo()
            : mResetChannel("Reset Channel")
        {
            std::ostringstream stream;
            stream << LL_VIEWER_VERSION_MAJOR << "."
                   << LL_VIEWER_VERSION_MINOR << "."
                   << LL_VIEWER_VERSION_PATCH << "."
                   << LL_VIEWER_VERSION_BUILD;
            mVersion = stream.str();
            stream.str("");

            stream << LL_VIEWER_VERSION_MAJOR << "."
                   << LL_VIEWER_VERSION_MINOR << "."
                   << LL_VIEWER_VERSION_PATCH;
            mShortVersion = stream.str();
            stream.str("");

            stream << ll_viewer_channel
                   << " "
                   << mVersion;
            mVersionAndChannel = stream.str();
            stream.str("");

            stream << mResetChannel
                   << " "
                   << mVersion;
            mResetVersionAndChannel = stream.str();
        }
        std::string mResetChannel;
        std::string mVersion;
        std::string mShortVersion;
        std::string mVersionAndChannel;
        std::string mResetVersionAndChannel;
    };

    TEST_CASE_METHOD(versioninfo, "LLVersionInfo: the version and channel are the build's until resetChannel", "[LLVersionInfo]")
    {
        std::cout << "What we parsed from CMake: " << LL_VIEWER_VERSION_BUILD << std::endl;
        std::cout << "What we get from llversioninfo: " << LLVersionInfo::instance().getBuild() << std::endl;
        CHECK(LLVersionInfo::instance().getMajor() == LL_VIEWER_VERSION_MAJOR);
        CHECK(LLVersionInfo::instance().getMinor() == LL_VIEWER_VERSION_MINOR);
        CHECK(LLVersionInfo::instance().getPatch() == LL_VIEWER_VERSION_PATCH);
        CHECK(LLVersionInfo::instance().getBuild() == LL_VIEWER_VERSION_BUILD);
        CHECK(LLVersionInfo::instance().getChannel() == ll_viewer_channel);
        CHECK(LLVersionInfo::instance().getVersion() == mVersion);
        CHECK(LLVersionInfo::instance().getShortVersion() == mShortVersion);
        CHECK(LLVersionInfo::instance().getChannelAndVersion() == mVersionAndChannel);

        // The channel is the process's: put back however this ends.
        struct ChannelBack
        {
            const std::string was = LLVersionInfo::instance().getChannel();
            ~ChannelBack() { LLVersionInfo::instance().resetChannel(was); }
        } channel_back;
        LLVersionInfo::instance().resetChannel(mResetChannel);
        CHECK(LLVersionInfo::instance().getChannel() == mResetChannel);

        CHECK(LLVersionInfo::instance().getChannelAndVersion() == mResetVersionAndChannel);
    }
}
