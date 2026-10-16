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
        CHECK(channelDescriptors.in() == 0);
        CHECK(channelDescriptors.out() == 1);

        S32 val = 50;
        LLChannelDescriptors channelDescriptors1(val);
        CHECK(channelDescriptors1.in() == 50);
        CHECK(channelDescriptors1.out() == 51);
    }

    TEST_CASE("buffer: a default LLSegment is empty and setChannel sets its channel", "[buffer]")
    {
        LLSegment segment;
        CHECK((segment.getChannel() == 0 && segment.data() == NULL));
        CHECK(segment.size() == 0);
        segment.setChannel(50);
        CHECK(segment.getChannel() == 50);
        CHECK(segment.isOnChannel(50));
    }

    TEST_CASE("buffer: an LLSegment over data keeps its channel and data", "[buffer]")
    {
        S32 channel = 30;
        const char str[] = "SecondLife";
        S32 len = sizeof(str);
        LLSegment segment(channel, (U8*)str, len);
        CHECK(segment.getChannel() == 30);
        REQUIRE(segment.size() == len);
        CHECK(segment.data() == (U8*)str);
        CHECK_THAT(ll_test::Memory(segment.data(), segment.size()), ll_test::MemoryMatches((U8*)str, len));
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
        CHECK(buf.createSegment(channel, requestSize, segment));
        CHECK(segment.size() == requestSize);
        // second request for the remaining 1 byte

        requestSize = 1;
        CHECK(buf.createSegment(channel, requestSize, segment));
        CHECK(segment.size() == requestSize);

        // it should fail now.
        requestSize = 1;
        CHECK_FALSE(buf.createSegment(channel, requestSize, segment));

        LLHeapBuffer buf1(bigSize);

        // a request for more than the default size but less than this buffer's size succeeds.
        requestSize = 16384 + 1;
        CHECK(buf1.createSegment(channel, requestSize, segment));
        CHECK(segment.size() == requestSize);

        LLHeapBuffer buf2((U8*) str, smallSize);
        requestSize = smallSize;
        CHECK(buf2.createSegment(channel, requestSize, segment));
        REQUIRE(segment.size() == requestSize);
        CHECK(memcmp(segment.data(), (U8*) str, requestSize) == 0);
        // it is full now, so it refuses any request, even one it would otherwise cut down to fit.
        requestSize = smallSize+1;
        CHECK_FALSE(buf2.createSegment(channel, requestSize, segment));
    }

    TEST_CASE("buffer: makeChannelConsumer reads the out channel of its producer", "[buffer]")
    {
        LLChannelDescriptors inchannelDescriptors(20);
        LLChannelDescriptors outchannelDescriptors = LLBufferArray::makeChannelConsumer(inchannelDescriptors);
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
        CHECK(count == len);
    }

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
        CHECK(len2 == 20);

        buf[len2] = '\0';
        CHECK(buf == str);
    }

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
        CHECK(len2 == 20);

        buf[len2] = '\0';
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
        REQUIRE(bufferArray1.takeContents(bufferArray) == 1);

        char buf[100];
        S32 len2 = len;
        bufferArray1.readAfter(channelDescriptors.in(), NULL, (U8*)buf, len2);
        // the terminator is the last byte read, so a short read leaves buf unterminated
        REQUIRE(len2 == len);
        CHECK(buf == str);
    }

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
        REQUIRE(iterator != bufferArray.endSegment());
        ++iterator;
        REQUIRE(iterator != bufferArray.endSegment());
        std::string str(((char*)(*iterator).data()), (*iterator).size());
        CHECK(str == std::string(" is a Virtual World"));
    }

    TEST_CASE("buffer: splitAfter the last byte of a segment adds no empty segment", "[buffer]")
    {
        const char array[] = "SecondLife";
        S32 len = static_cast<S32>(strlen(array));
        LLBufferArray bufferArray;
        bufferArray.append(0, (U8*)array, len);

        char buf[255];
        S32 len1 = len;
        U8* last = bufferArray.readAfter(0, 0, (U8*)buf, len1);
        REQUIRE(len1 == len);
        LLBufferArray::segment_iterator_t iterator = bufferArray.splitAfter(last);
        REQUIRE(iterator != bufferArray.endSegment());
        CHECK((*iterator).size() == len);
        CHECK(++iterator == bufferArray.endSegment());
    }

    TEST_CASE("buffer: eraseSegment removes the segment makeSegment made", "[buffer]")
    {
        LLBufferArray bufferArray;
        LLChannelDescriptors channelDescriptors;
        LLBufferArray::segment_iterator_t it;
        S32 length = 1000;
        it = bufferArray.makeSegment(channelDescriptors.out(), length);
        REQUIRE(it != bufferArray.endSegment());
        CHECK(bufferArray.eraseSegment(it));
        CHECK(bufferArray.beginSegment() == bufferArray.endSegment());
    }

    TEST_CASE("buffer: constructSegmentAfter on an empty array returns endSegment", "[buffer]")
    {
        LLBufferArray bufferArray;
        LLBufferArray::segment_iterator_t it;
        LLSegment segment;
        LLBufferArray::segment_iterator_t end = bufferArray.endSegment();
        it = bufferArray.constructSegmentAfter(NULL, segment);
        CHECK(it == end);
    }
}
