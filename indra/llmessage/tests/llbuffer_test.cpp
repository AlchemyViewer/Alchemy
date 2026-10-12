/**
 * @file llbuffer_tut.cpp
 * @author Adroit
 * @date 2007-03
 * @brief llbuffer test cases.
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
#include "altestmatchers.h"

#include "linden_common.h"
#include "llbuffer.h"
#include "llerror.h"


namespace
{
    TEST_CASE("buffer: LLChannelDescriptors in() is the base channel and out() the next", "[buffer]")
    {
        LLChannelDescriptors channelDescriptors;
        {
            INFO("in() and out() functions Failed");
            CHECK(channelDescriptors.in() == 0);
            CHECK(channelDescriptors.out() == 1);
        }

        S32 val = 50;
        LLChannelDescriptors channelDescriptors1(val);
        {
            INFO("LLChannelDescriptors in() and out() functions Failed");
            CHECK(channelDescriptors1.in() == 50);
            CHECK(channelDescriptors1.out() == 51);
        }
    }

    TEST_CASE("buffer: a default LLSegment is empty and setChannel sets its channel", "[buffer]")
    {
        LLSegment segment;
        {
            INFO("LLSegment get functions failed");
            REQUIRE((0 == segment.getChannel() && NULL == segment.data()));
            REQUIRE(segment.size() == 0);
        }
        segment.setChannel(50);
        UNSCOPED_INFO("LLSegment setChannel() function failed");
        CHECK(segment.getChannel() == 50);
        UNSCOPED_INFO("LLSegment isOnChannel() function failed");
        CHECK(segment.isOnChannel(50));
    }

    TEST_CASE("buffer: an LLSegment over data keeps its channel and data", "[buffer]")
    {
        S32 channel = 30;
        const char str[] = "SecondLife";
        S32 len = sizeof(str);
        LLSegment segment(channel, (U8*)str, len);
        {
            INFO("LLSegment get functions failed");
            CHECK(segment.getChannel() == 30);
            REQUIRE(len == segment.size());
            CHECK((U8*)str == segment.data());
        }
        UNSCOPED_INFO("LLSegment::data() failed");
        REQUIRE_THAT(ll_test::Memory(segment.data(), segment.size()), ll_test::MemoryMatches((U8*)str, len));
        UNSCOPED_INFO("LLSegment isOnChannel() function failed");
        CHECK(segment.isOnChannel(channel));
    }

    TEST_CASE("buffer: LLHeapBuffer createSegment succeeds until the buffer is full", "[buffer]")
    {
        S32 channel = 50;
        S32 bigSize = 16384*2;
        char str[] = "SecondLife";
        S32 smallSize = sizeof(str);

        LLSegment segment;
        LLHeapBuffer buf; // use default size of DEFAULT_HEAP_BUFFER_SIZE = 16384

        S32 requestSize;

        requestSize = 16384-1;
        {
            INFO("1. LLHeapBuffer createSegment failed");
            REQUIRE(buf.createSegment(channel, requestSize, segment));
            REQUIRE(segment.size() == requestSize);
        }
        // second request for remainign 1 byte

        requestSize = 1;
        {
            INFO("2. LLHeapBuffer createSegment failed");
            CHECK(buf.createSegment(channel, requestSize, segment));
            REQUIRE(segment.size() == requestSize);
        }

        // it should fail now.
        requestSize = 1;
        UNSCOPED_INFO("3. LLHeapBuffer createSegment failed");
        CHECK_FALSE(buf.createSegment(channel, requestSize, segment));

        LLHeapBuffer buf1(bigSize);

        // requst for more than default size but less than total sizeit should fail now.
        requestSize = 16384 + 1;
        {
            INFO("4. LLHeapBuffer createSegment failed");
            CHECK(buf1.createSegment(channel, requestSize, segment));
            REQUIRE(segment.size() == requestSize);
        }

        LLHeapBuffer buf2((U8*) str, smallSize);
        requestSize = smallSize;
        {
            INFO("5. LLHeapBuffer createSegment failed");
            CHECK(buf2.createSegment(channel, requestSize, segment));
            REQUIRE(segment.size() == requestSize);
            CHECK(memcmp(segment.data(), (U8*) str, requestSize) == 0);
        }
        requestSize = smallSize+1;
        UNSCOPED_INFO("6. LLHeapBuffer createSegment failed");
        CHECK_FALSE(buf2.createSegment(channel, requestSize, segment));
    }

    //makeChannelConsumer()
    TEST_CASE("buffer: makeChannelConsumer reads the out channel of its producer", "[buffer]")
    {
        LLChannelDescriptors inchannelDescriptors(20);
        LLChannelDescriptors outchannelDescriptors = LLBufferArray::makeChannelConsumer(inchannelDescriptors);
        UNSCOPED_INFO("LLBufferArray::makeChannelConsumer() function Failed");
        CHECK(outchannelDescriptors.in() == 21);
    }

    TEST_CASE("buffer: countAfter counts the bytes appended to a channel", "[buffer]")
    {
        LLBufferArray bufferArray;
        const char array[] = "SecondLife";
        S32 len = static_cast<S32>(strlen(array));
        LLChannelDescriptors channelDescriptors = bufferArray.nextChannel();
        bufferArray.append(channelDescriptors.in(), (U8*)array, len);
        S32 count = bufferArray.countAfter(channelDescriptors.in(), NULL);
        UNSCOPED_INFO("Appended size is:");
        CHECK(count == len);
    }

    //append() and prepend()
    TEST_CASE("buffer: readAfter reads prepended data before appended data", "[buffer]")
    {
        LLBufferArray bufferArray;
        const char array[] = "SecondLife";
        S32 len = static_cast<S32>(strlen(array));
        const char array1[] = "LindenLabs";
        S32 len1 = static_cast<S32>(strlen(array1));

        std::string str(array1);
        str.append(array);

        LLChannelDescriptors channelDescriptors = bufferArray.nextChannel();
        bufferArray.append(channelDescriptors.in(), (U8*)array, len);
        bufferArray.prepend(channelDescriptors.in(), (U8*)array1, len1);
        char buf[100];
        S32 len2 = 20;
        bufferArray.readAfter(channelDescriptors.in(), NULL, (U8*)buf, len2);
        UNSCOPED_INFO("readAfter length failed");
        CHECK(len2 == 20);

        buf[len2] = '\0';
        UNSCOPED_INFO("readAfter/prepend/append failed");
        CHECK(buf == str);
    }

    //append()
    TEST_CASE("buffer: readAfter reads two appends in order", "[buffer]")
    {
        LLBufferArray bufferArray;
        const char array[] = "SecondLife";
        S32 len = static_cast<S32>(strlen(array));
        const char array1[] = "LindenLabs";
        S32 len1 = static_cast<S32>(strlen(array1));

        std::string str(array);
        str.append(array1);

        LLChannelDescriptors channelDescriptors = bufferArray.nextChannel();
        bufferArray.append(channelDescriptors.in(), (U8*)array, len);
        bufferArray.append(channelDescriptors.in(), (U8*)array1, len1);
        char buf[100];
        S32 len2 = 20;
        bufferArray.readAfter(channelDescriptors.in(), NULL, (U8*)buf, len2);
        UNSCOPED_INFO("readAfter length failed");
        CHECK(len2 == 20);

        buf[len2] = '\0';
        UNSCOPED_INFO("readAfter/append/append failed");
        CHECK(buf == str);
    }

    TEST_CASE("buffer: takeContents moves the data of another array", "[buffer]")
    {
        LLBufferArray bufferArray;
        const char array[] = "SecondLife";
        S32 len = static_cast<S32>(strlen(array)) + 1;
        std::string str(array);
        LLChannelDescriptors channelDescriptors = bufferArray.nextChannel();
        bufferArray.append(channelDescriptors.in(), (U8*)array, len);
        LLBufferArray bufferArray1;
        UNSCOPED_INFO("Contents are not copied and the source buffer is not empty");
        REQUIRE(bufferArray1.takeContents(bufferArray) == 1);

        char buf[100];
        S32 len2 = len;
        bufferArray1.readAfter(channelDescriptors.in(), NULL, (U8*)buf, len2);
        UNSCOPED_INFO("takeContents failed to copy");
        CHECK(buf == str);
    }

    //seek()
    TEST_CASE("buffer: seek moves back from where a read ended", "[buffer]")
    {
        const char array[] = "SecondLife is a Virtual World";
        S32 len = static_cast<S32>(strlen(array));
        LLBufferArray bufferArray;
        bufferArray.append(0, (U8*)array, len);

        char buf[255];
        S32 len1 = 16;
        U8* last = bufferArray.readAfter(0, 0, (U8*)buf, len1);
        buf[len1] = '\0';
        last = bufferArray.seek(0, last, -2);

        len1 = 15;
        last = bufferArray.readAfter(0, last, (U8*)buf, len1);
        buf[len1] = '\0';
        std::string str(buf);
        UNSCOPED_INFO("Seek does'nt worked");
        CHECK(str == std::string("a Virtual World"));
    }

    TEST_CASE("buffer: splitAfter starts a new segment after the address", "[buffer]")
    {
        const char array[] = "SecondLife is a Virtual World";
        S32 len = static_cast<S32>(strlen(array));
        LLBufferArray bufferArray;
        bufferArray.append(0, (U8*)array, len);

        char buf[255];
        S32 len1 = 10;
        U8* last = bufferArray.readAfter(0, 0, (U8*)buf, len1);
        bufferArray.splitAfter(last);
        LLBufferArray::segment_iterator_t iterator = bufferArray.beginSegment();
        ++iterator;
        std::string str(((char*)(*iterator).data()), (*iterator).size());
        UNSCOPED_INFO("Strings are not equal;splitAfter() operation failed");
        CHECK(str == std::string(" is a Virtual World"));
    }

    //makeSegment()->eraseSegment()
    TEST_CASE("buffer: eraseSegment removes the segment makeSegment made", "[buffer]")
    {
        LLBufferArray bufferArray;
        LLChannelDescriptors channelDescriptors;
        LLBufferArray::segment_iterator_t it;
        S32 length = 1000;
        it = bufferArray.makeSegment(channelDescriptors.out(), length);
        UNSCOPED_INFO("makeSegment() function failed");
        CHECK(it != bufferArray.endSegment());
        UNSCOPED_INFO("eraseSegment() function failed");
        CHECK(bufferArray.eraseSegment(it));
        UNSCOPED_INFO("eraseSegment() begin/end should now be same");
        CHECK(bufferArray.beginSegment() == bufferArray.endSegment());
    }

    // constructSegmentAfter()
    TEST_CASE("buffer: constructSegmentAfter on an empty array returns endSegment", "[buffer]")
    {
        LLBufferArray bufferArray;
        LLBufferArray::segment_iterator_t it;
        LLSegment segment;
        LLBufferArray::segment_iterator_t end = bufferArray.endSegment();
        it = bufferArray.constructSegmentAfter(NULL, segment);
        UNSCOPED_INFO("constructSegmentAfter() function failed");
        CHECK(it == end);
    }
}
