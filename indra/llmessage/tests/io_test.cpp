/**
 * @file io.cpp
 * @author Phoenix
 * @date 2005-10-02
 * @brief Tests for io classes and helpers
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
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

#include <iterator>

#include "llwin32headers.h"
#include "apr_pools.h"

#include "llbuffer.h"
#include "llbufferstream.h"
#include "lliosocket.h"
#include "llioutil.h"
#include "llmemorystream.h"
#include "llpipeutil.h"
#include "llpumpio.h"
#include "llsd.h"
#include "llsdserialize.h"
#include "llcommon.h"
#include "lluuid.h"
#include "llinstantmessage.h"

namespace
{
    struct heap_buffer_data
    {
        heap_buffer_data() : mBuffer(NULL) {}
        ~heap_buffer_data() { if(mBuffer) delete mBuffer; }
        LLHeapBuffer* mBuffer;
    };

}

namespace
{
    struct buffer_data
    {
        LLBufferArray mBuffer;
    };

    TEST_CASE_METHOD(buffer_data, "buffer_array: countAfter counts the whole channel or what follows an address", "[buffer_array]")
    {
        const char HELLO_WORLD[] = "hello world";
        const S32 str_len = static_cast<S32>(strlen(HELLO_WORLD));
        LLChannelDescriptors ch = mBuffer.nextChannel();
        mBuffer.append(ch.in(), (U8*)HELLO_WORLD, str_len);
        S32 count = mBuffer.countAfter(ch.in(), NULL);
        CHECK(count == str_len);
        LLBufferArray::segment_iterator_t it = mBuffer.beginSegment();
        REQUIRE(it != mBuffer.endSegment());
        U8* first = (*it).data();
        count = mBuffer.countAfter(ch.in(), first);
        CHECK(count == str_len - 1);
    }

    TEST_CASE_METHOD(buffer_data, "buffer_array: countAfter counts across two appended segments", "[buffer_array]")
    {
        const char HELLO_WORLD[] = "hello world";
        const S32 str_len = static_cast<S32>(strlen(HELLO_WORLD));        /* Flawfinder: ignore */
        LLChannelDescriptors ch = mBuffer.nextChannel();
        mBuffer.append(ch.in(), (U8*)HELLO_WORLD, str_len);
        mBuffer.append(ch.in(), (U8*)HELLO_WORLD, str_len);
        S32 count = mBuffer.countAfter(ch.in(), NULL);
        CHECK(count == 2 * str_len);
        LLBufferArray::segment_iterator_t it = mBuffer.beginSegment();
        REQUIRE(it != mBuffer.endSegment());
        U8* first = (*it).data();
        count = mBuffer.countAfter(ch.in(), first);
        CHECK(count == (2 * str_len) - 1);
    }

    TEST_CASE_METHOD(buffer_data, "buffer_array: readAfter reads two appends in order", "[buffer_array]")
    {
        const char ONE[] = "one";
        const char TWO[] = "two";
        std::string expected(ONE);
        expected.append(TWO);
        LLChannelDescriptors ch = mBuffer.nextChannel();
        mBuffer.append(ch.in(), (U8*)ONE, 3);
        mBuffer.append(ch.in(), (U8*)TWO, 3);
        char buffer[255];   /* Flawfinder: ignore */
        S32 len = 6;
        mBuffer.readAfter(ch.in(), NULL, (U8*)buffer, len);
        CHECK(len == 6);
        buffer[len] = '\0';
        std::string actual(buffer);
        CHECK(actual == expected);
    }

    TEST_CASE_METHOD(buffer_data, "buffer_array: readAfter reads a prepend before an append", "[buffer_array]")
    {
        const char ONE[] = "one";
        const char TWO[] = "two";
        std::string expected(ONE);
        expected.append(TWO);
        LLChannelDescriptors ch = mBuffer.nextChannel();
        mBuffer.append(ch.in(), (U8*)TWO, 3);
        mBuffer.prepend(ch.in(), (U8*)ONE, 3);
        char buffer[255];   /* Flawfinder: ignore */
        S32 len = 6;
        mBuffer.readAfter(ch.in(), NULL, (U8*)buffer, len);
        CHECK(len == 6);
        buffer[len] = '\0';
        std::string actual(buffer);
        CHECK(actual == expected);
    }

    TEST_CASE_METHOD(buffer_data, "buffer_array: readAfter from an address starts after that byte", "[buffer_array]")
    {
        const char ONE[] = "one";
        const char TWO[] = "two";
        std::string expected("netwo");
        LLChannelDescriptors ch = mBuffer.nextChannel();
        mBuffer.append(ch.in(), (U8*)TWO, 3);
        mBuffer.prepend(ch.in(), (U8*)ONE, 3);
        char buffer[255];   /* Flawfinder: ignore */
        S32 len = 5;
        LLBufferArray::segment_iterator_t it = mBuffer.beginSegment();
        REQUIRE(it != mBuffer.endSegment());
        U8* addr = (*it).data();
        mBuffer.readAfter(ch.in(), addr, (U8*)buffer, len);
        CHECK(len == 5);
        buffer[len] = '\0';
        std::string actual(buffer);
        CHECK(actual == expected);
    }

    TEST_CASE_METHOD(buffer_data, "buffer_array: a header prepended to the out channel reads back first", "[buffer_array]")
    {
        std::string request("The early bird catches the worm.");
        std::string response("If you're a worm, sleep late.");
        std::ostringstream expected;
        expected << "ContentLength: " << response.length() << "\r\n\r\n"
                 << response;
        LLChannelDescriptors ch = mBuffer.nextChannel();
        mBuffer.append(ch.in(), (U8*)request.c_str(), static_cast<S32>(request.length()));
        mBuffer.append(ch.out(), (U8*)response.c_str(), static_cast<S32>(response.length()));
        S32 count = mBuffer.countAfter(ch.out(), NULL);
        std::ostringstream header;
        header << "ContentLength: " << count << "\r\n\r\n";
        std::string head(header.str());
        mBuffer.prepend(ch.out(), (U8*)head.c_str(), static_cast<S32>(head.length()));
        char buffer[1024];  /* Flawfinder: ignore */
        S32 len = static_cast<S32>(response.size() + head.length());
        CHECK(len == (S32)expected.str().length());
        mBuffer.readAfter(ch.out(), NULL, (U8*)buffer, len);
        buffer[len] = '\0';
        std::string actual(buffer);
        CHECK(actual == expected.str());
    }

    TEST_CASE_METHOD(buffer_data, "buffer_array: readAfter and seek step through the buffer a line at a time", "[buffer_array]")
    {
        const S32 LINE_COUNT = 3;
        std::string lines[LINE_COUNT] =
            {
                std::string("GET /index.htm HTTP/1.0\r\n"),
                std::string("User-Agent: Wget/1.9.1\r\n"),
                std::string("Host: localhost:8008\r\n")
            };
        std::string text;
        S32 i;
        for(i = 0; i < LINE_COUNT; ++i)
        {
            text.append(lines[i]);
        }
        LLChannelDescriptors ch = mBuffer.nextChannel();
        mBuffer.append(ch.in(), (U8*)text.c_str(), static_cast<S32>(text.length()));
        const S32 BUFFER_LEN = 1024;
        char buf[BUFFER_LEN];
        S32 len;
        U8* last = NULL;
        std::string last_line;
        for(i = 0; i < LINE_COUNT; ++i)
        {
            CAPTURE(i);
            len = BUFFER_LEN;
            last = mBuffer.readAfter(ch.in(), last, (U8*)buf, len);
            // buf holds len bytes and no terminator, so the line ends among those bytes or not at all.
            const char* newline = static_cast<const char*>(memchr(buf, '\n', len));
            REQUIRE(newline != NULL);
            S32 offset = -((len - 1) - (S32)(newline - buf));
            last_line.assign(buf, newline - buf + 1);
            REQUIRE(last_line == lines[i]);
            last = mBuffer.seek(ch.in(), last, offset);
        }
    }

    TEST_CASE_METHOD(buffer_data, "buffer_array: takeContents splices another array between appends", "[buffer_array]")
    {
        LLChannelDescriptors ch = mBuffer.nextChannel();
        mBuffer.append(ch.in(), (U8*)"1", 1);
        LLBufferArray buffer;
        buffer.append(ch.in(), (U8*)"2", 1);
        mBuffer.takeContents(buffer);
        mBuffer.append(ch.in(), (U8*)"3", 1);
        S32 count = mBuffer.countAfter(ch.in(), NULL);
        REQUIRE(count == 3);
        U8* temp = new U8[count];
        mBuffer.readAfter(ch.in(), NULL, temp, count);
        CHECK(memcmp(temp, (void*)"123", 3) == 0);
        delete[] temp;
    }

    TEST_CASE_METHOD(buffer_data, "buffer_array: capacity holds as segments are erased and refilled", "[buffer_array]")
    {
        LLChannelDescriptors ch = mBuffer.nextChannel();
        mBuffer.append(ch.in(), (U8*)"1", 1);
        S32 capacity = mBuffer.capacity();
        CHECK(capacity > 0);
        U8* temp = new U8[capacity - 1];
        mBuffer.append(ch.in(), temp, capacity - 1);
        capacity = mBuffer.capacity();
        UNSCOPED_INFO("has capacity when full");
        CHECK(capacity > 0);
        S32 used = mBuffer.countAfter(ch.in(), NULL);
        CHECK(used == capacity);

        LLBufferArray::segment_iterator_t iter = mBuffer.beginSegment();
        while(iter != mBuffer.endSegment())
        {
            mBuffer.eraseSegment(iter++);
        }

        used = mBuffer.countAfter(ch.in(), NULL);
        CHECK(used == 0);
        S32 capacity2 = mBuffer.capacity();
        UNSCOPED_INFO("capacity the same after erase");
        CHECK(capacity2 == capacity);
        mBuffer.append(ch.in(), temp, capacity - 1);
        capacity2 = mBuffer.capacity();
        UNSCOPED_INFO("capacity the same after append");
        CHECK(capacity2 == capacity);

        delete[] temp;
    }

#if 0
    template<> template<>
    void buffer_object::test<9>()
    {
        char buffer[1024];  /* Flawfinder: ignore */
        S32 size = sprintf(buffer,
                        "%d|%d|%s|%s|%s|%s|%s|%x|%x|%x|%x|%x|%s|%s|%d|%d|%x",
                        7,
                        7,
                        "Hang Glider INFO",
                        "18e84d1e-04a4-4c0d-8cb6-6c73477f0a9a",
                        "0e346d8b-4433-4d66-a6b0-fd37083abc4c",
                        "0e346d8b-4433-4d66-a6b0-fd37083abc4c",
                        "00000000-0000-0000-0000-000000000000",
                        0x7fffffff,
                        0x7fffffff,
                        0,
                        0,
                        0x7fffffff,
                        "69e0d357-2e7c-8990-a2bc-7f61c868e5a3",
                        "2004-06-04 16:09:17 note card",
                        0,
                        10,
                        0) + 1;

        //const char* expected = "7|7|Hang Glider INFO|18e84d1e-04a4-4c0d-8cb6-6c73477f0a9a|0e346d8b-4433-4d66-a6b0-fd37083abc4c|0e346d8b-4433-4d66-a6b0-fd37083abc4c|00000000-0000-0000-0000-000000000000|7fffffff|7fffffff|0|0|7fffffff|69e0d357-2e7c-8990-a2bc-7f61c868e5a3|2004-06-04 16:09:17 note card|0|10|0\0";

        LLSD* bin_bucket = LLIMInfo::buildSDfrombuffer((U8*)buffer,size);

        char post_buffer[1024];
        U32 post_size;
        LLIMInfo::getBinaryBucket(bin_bucket,(U8*)post_buffer,post_size);
        ensure_equals("Buffer sizes",size,(S32)post_size);
        ensure("Buffer content",!strcmp(buffer,post_buffer));
    }
#endif

    /*
    template<> template<>
    void buffer_object::test<>()
    {
    }
    */
}

namespace
{
    struct buffer_and_stream_data
    {
        LLBufferArray mBuffer;
    };

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: operator>> reads words appended to the in channel", "[buffer_stream]")
    {
        const char HELLO_WORLD[] = "hello world";
        const S32 str_len = static_cast<S32>(strlen(HELLO_WORLD));    /* Flawfinder: ignore */
        LLChannelDescriptors ch = mBuffer.nextChannel();
        LLBufferStream str(ch, &mBuffer);
        mBuffer.append(ch.in(), (U8*)HELLO_WORLD, str_len);
        std::string hello;
        std::string world;
        str >> hello >> world;
        CHECK(hello == std::string("hello"));
        CHECK(world == std::string("world"));
    }

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: operator>> reads across segments and ignores the out channel", "[buffer_stream]")
    {
        std::string part1("Eat my shor");
        std::string part2("ts ho");
        std::string part3("mer");
        std::string ignore("ignore me");
        LLChannelDescriptors ch = mBuffer.nextChannel();
        LLBufferStream str(ch, &mBuffer);
        mBuffer.append(ch.in(), (U8*)part1.c_str(), static_cast<S32>(part1.length()));
        mBuffer.append(ch.in(), (U8*)part2.c_str(), static_cast<S32>(part2.length()));
        mBuffer.append(ch.out(), (U8*)ignore.c_str(), static_cast<S32>(ignore.length()));
        mBuffer.append(ch.in(), (U8*)part3.c_str(), static_cast<S32>(part3.length()));
        std::string eat;
        std::string my;
        std::string shorts;
        std::string homer;
        str >> eat >> my >> shorts >> homer;
        CHECK(eat == std::string("Eat"));
        CHECK(my == std::string("my"));
        CHECK(shorts == std::string("shorts"));
        CHECK(homer == std::string("homer"));
    }

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: seek back from a read and read across segments", "[buffer_stream]")
    {
        std::string part1("junk in ");
        std::string part2("the trunk");
        const S32 CHANNEL = 0;
        mBuffer.append(CHANNEL, (U8*)part1.c_str(), static_cast<S32>(part1.length()));
        mBuffer.append(CHANNEL, (U8*)part2.c_str(), static_cast<S32>(part2.length()));
        U8* last = 0;
        const S32 BUF_LEN = 128;
        char buf[BUF_LEN];
        S32 len = 11;
        last = mBuffer.readAfter(CHANNEL, last, (U8*)buf, len);
        buf[len] = '\0';
        std::string actual(buf);
        CHECK(actual == std::string("junk in the"));
        last = mBuffer.seek(CHANNEL, last, -6);
        len = 12;
        last = mBuffer.readAfter(CHANNEL, last, (U8*)buf, len);
        buf[len] = '\0';
        actual.assign(buf);
        CHECK(actual == std::string("in the trunk"));
    }

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: splitAfter splits a segment in two after a read", "[buffer_stream]")
    {
        std::string phrase("zippity do da!");
        const S32 CHANNEL = 0;
        mBuffer.append(CHANNEL, (U8*)phrase.c_str(), static_cast<S32>(phrase.length()));
        const S32 BUF_LEN = 128;
        char buf[BUF_LEN];
        S32 len = 7;
        U8* last = mBuffer.readAfter(CHANNEL, NULL, (U8*)buf, len);
        mBuffer.splitAfter(last);
        LLBufferArray::segment_iterator_t it = mBuffer.beginSegment();
        LLBufferArray::segment_iterator_t end = mBuffer.endSegment();
        REQUIRE(it != end);
        std::string first((char*)((*it).data()), (*it).size());
        CHECK(first == std::string("zippity"));
        ++it;
        REQUIRE(it != end);
        std::string second((char*)((*it).data()), (*it).size());
        CHECK(second == std::string(" do da!"));
        ++it;
        CHECK(it == end);
    }

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: operator<< writes to the out channel", "[buffer_stream]")
    {
        LLChannelDescriptors ch = mBuffer.nextChannel();
        LLBufferStream str(ch, &mBuffer);
        std::string h1("hello");
        std::string h2(", how are you doing?");
        std::string expected(h1);
        expected.append(h2);
        str << h1 << h2;
        str.flush();
        const S32 BUF_LEN = 128;
        char buf[BUF_LEN];
        S32 actual_len = BUF_LEN;
        S32 expected_len = static_cast<S32>(h1.size() + h2.size());
        (void) mBuffer.readAfter(ch.out(), NULL, (U8*)buf, actual_len);
        REQUIRE(actual_len == expected_len);
        buf[actual_len] = '\0';
        std::string actual(buf);
        CHECK(actual == expected);
    }

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: flush after filling the put segment keeps the segment after it", "[buffer_stream]")
    {
        LLChannelDescriptors ch = mBuffer.nextChannel();
        LLBufferStream str(ch, &mBuffer);

        // The first character makes the put area, a segment of the stream's own size.
        str << 'x';
        LLBufferArray::segment_iterator_t it = mBuffer.beginSegment();
        REQUIRE(it != mBuffer.endSegment());
        const S32 segment_size = (*it).size();

        // Fill it to its last byte, and put a segment on another channel after it.
        str << std::string(segment_size - 1, 'x');
        const char TAIL[] = "tail";
        const S32 tail_len = static_cast<S32>(strlen(TAIL));
        mBuffer.append(ch.in(), (U8*)TAIL, tail_len);
        str.flush();

        CHECK(mBuffer.countAfter(ch.out(), NULL) == segment_size);
        CHECK(mBuffer.countAfter(ch.in(), NULL) == tail_len);
    }

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: seekp on the stream matches seekp on an ostringstream", "[buffer_stream]")
    {
        LLChannelDescriptors ch = mBuffer.nextChannel();
        LLBufferStream bstr(ch, &mBuffer);
        std::ostringstream ostr;
        std::vector<LLUUID> ids;
        LLUUID id;
        for(int i = 0; i < 5; ++i)
        {
            id.generate();
            ids.push_back(id);
        }
        bstr << "SELECT concat(u.username, ' ', l.name) "
             << "FROM user u, user_last_name l "
             << "WHERE u.last_name_id = l.last_name_id"
             << " AND u.agent_id IN ('";
        ostr << "SELECT concat(u.username, ' ', l.name) "
             << "FROM user u, user_last_name l "
             << "WHERE u.last_name_id = l.last_name_id"
             << " AND u.agent_id IN ('";
        std::copy(
            ids.begin(),
            ids.end(),
            std::ostream_iterator<LLUUID>(bstr, "','"));
        std::copy(
            ids.begin(),
            ids.end(),
            std::ostream_iterator<LLUUID>(ostr, "','"));
        bstr.seekp(-2, std::ios::cur);
        ostr.seekp(-2, std::ios::cur);
        bstr << ") ";
        ostr << ") ";
        bstr.flush();
        const S32 BUF_LEN = 512;
        char buf[BUF_LEN];      /* Flawfinder: ignore */
        S32 actual_len = BUF_LEN;
        (void) mBuffer.readAfter(ch.out(), NULL, (U8*)buf, actual_len);
        // The read may fill buf, which then has no room for a terminator.
        std::string actual(buf, actual_len);
        std::string expected(ostr.str());
        CHECK(actual.size() == expected.size());
        CHECK(actual == expected);
    }

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: takeContents splices between flushed stream writes", "[buffer_stream]")
    {
        LLChannelDescriptors ch = mBuffer.nextChannel();
        LLBufferStream bstr(ch, &mBuffer);
        bstr << "1";
        bstr.flush();
        S32 count = mBuffer.countAfter(ch.out(), NULL);
        CHECK(count == 1);
        LLBufferArray buffer;
        buffer.append(ch.out(), (U8*)"2", 1);
        mBuffer.takeContents(buffer);
        count = mBuffer.countAfter(ch.out(), NULL);
        CHECK(count == 2);
        bstr << "3";
        bstr.flush();
        count = mBuffer.countAfter(ch.out(), NULL);
        REQUIRE(count == 3);
        U8* temp = new U8[count];
        mBuffer.readAfter(ch.out(), NULL, temp, count);
        CHECK(memcmp(temp, (void*)"123", 3) == 0);
        delete[] temp;
    }

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: binary blobs in a notation array parse back intact", "[buffer_stream]")
    {
        LLChannelDescriptors ch = mBuffer.nextChannel();
        LLBufferStream ostr(ch, &mBuffer);
        typedef std::vector<U8> buf_t;
        typedef std::vector<buf_t> actual_t;
        actual_t actual;
        buf_t source;
        bool need_comma = false;
        ostr << "[";
        S32 total_size = 1;
        for(S32 i = 2000; i < 2003; ++i)
        {
            if(need_comma)
            {
                ostr << ",";
                ++total_size;
            }
            need_comma = true;
            srand(69 + i);  /* Flawfinder: ignore */
            S32 size = rand() % 1000 + 1000;
            std::generate_n(
                std::back_insert_iterator<buf_t>(source),
                size,
                rand);
            actual.push_back(source);
            ostr << "b(" << size << ")\"";
            total_size += 8;
            ostr.write((const char*)(&source[0]), size);
            total_size += size;
            source.clear();
            ostr << "\"";
            ++total_size;
        }
        ostr << "]";
        ++total_size;
        ostr.flush();

        // now that we have a bunch of data on a stream, parse it all.
        ch = mBuffer.nextChannel();
        S32 count = mBuffer.countAfter(ch.in(), NULL);
        CHECK(count == total_size);
        LLBufferStream istr(ch, &mBuffer);
        LLSD data;
        count = LLSDSerialize::fromNotation(data, istr, total_size);
        REQUIRE(data.isDefined());

        for(S32 j = 0; j < 3; ++j)
        {
            CAPTURE(j);
            LLSD child(data[j]);
            REQUIRE(child.isDefined());
            source = child.asBinary();
            REQUIRE(source.size() == actual[j].size());
            REQUIRE(memcmp(&source[0], &actual[j][0], source.size()) == 0);
        }
    }

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: a notation map of 2000 binary blobs parses", "[buffer_stream]")
    {
        LLChannelDescriptors ch = mBuffer.nextChannel();
        LLBufferStream ostr(ch, &mBuffer);
        typedef std::vector<U8> buf_t;
        buf_t source;
        bool need_comma = false;
        ostr << "{";
        S32 total_size = 1;
        for(S32 i = 1000; i < 3000; ++i)
        {
            if(need_comma)
            {
                ostr << ",";
                ++total_size;
            }
            need_comma = true;
            ostr << "'" << i << "':";
            total_size += 7;
            srand(69 + i);      /* Flawfinder: ignore */
            S32 size = rand() % 1000 + 1000;
            std::generate_n(
                std::back_insert_iterator<buf_t>(source),
                size,
                rand);
            ostr << "b(" << size << ")\"";
            total_size += 8;
            ostr.write((const char*)(&source[0]), size);
            total_size += size;
            source.clear();
            ostr << "\"";
            ++total_size;
        }
        ostr << "}";
        ++total_size;
        ostr.flush();

        // now that we have a bunch of data on a stream, parse it all.
        ch = mBuffer.nextChannel();
        S32 count = mBuffer.countAfter(ch.in(), NULL);
        CHECK(count == total_size);
        LLBufferStream istr(ch, &mBuffer);
        LLSD data;
        count = LLSDSerialize::fromNotation(data, istr, total_size);
        CHECK(data.isDefined());
    }

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: a notation login request parses from the stream", "[buffer_stream]")
    {
//#if LL_WINDOWS && _MSC_VER >= 1400
//        skip_fail("Fails on VS2005 due to broken LLSDSerialize::fromNotation() parser.");
//#endif
        const char LOGIN_STREAM[] = "{'method':'login', 'parameter': [ {"
                                        "'uri': 'sl-am:kellys.region.siva.lindenlab.com/location?start=url&px=128&py=128&pz=128&lx=0&ly=0&lz=0'}, "
                                        "{'version': i1}, {'texture_data': [ '61d724fb-ad79-f637-2186-5cf457560daa', '6e38b9be-b7cc-e77a-8aec-029a42b0b416', "
                                        "'a9073524-e89b-2924-ca6e-a81944109a1a', '658f18b5-5f1e-e593-f5d5-36c3abc7249a', '0cc799f4-8c99-6b91-bd75-b179b12429e2', "
                                        "'59fd9b64-8300-a425-aad8-2ffcbe9a49d2', '59fd9b64-8300-a425-aad8-2ffcbe9a49d2', '5748decc-f629-461c-9a36-a35a221fe21f', "
                                        "'b8fc9be2-26a6-6b47-690b-0e902e983484', 'a13ca0fe-3802-dc97-e79a-70d12171c724', 'dd9643cf-fd5d-0376-ed4a-b1cc646a97d5', "
                                        "'4ad13ae9-a112-af09-210a-cf9353a7a9e7', 'c228d1cf-4b5d-4ba8-84f4-899a0796aa97', 'c228d1cf-4b5d-4ba8-84f4-899a0796aa97', "
                                        "'c228d1cf-4b5d-4ba8-84f4-899a0796aa97', 'c228d1cf-4b5d-4ba8-84f4-899a0796aa97', 'c228d1cf-4b5d-4ba8-84f4-899a0796aa97', "
                                        "'5748decc-f629-461c-9a36-a35a221fe21f', 'c228d1cf-4b5d-4ba8-84f4-899a0796aa97', 'c228d1cf-4b5d-4ba8-84f4-899a0796aa97'],"
                                        "'session_id': '324cfa9f-fe5d-4d1c-a317-35f20a86a4d1','position': [ i128, i128, i128],'last_name': 'Linden','group_title': '-> !BLING! <-','group_name': 'test!','agent_access': 'M',"
                                        "'attachment_data': [ {'asset_id': 'aaede2b1-9955-09d4-5c93-2b557c778cf3','attachment_point': i6,'item_id': 'f3694abc-5122-db33-73d9-e0f4288dc2bf'}],"
                                        "'buddy_ids': [ '101358d5-469d-4b24-9b85-4dc3c05e635d', '1b00fec7-6265-4875-acac-80d9cfe9295c', '203ad6df-b522-491d-ba48-4e24eb57aeff', "
                                        "'22d4dcdb-aebb-47fa-b925-a871cc75ee48','27da3df5-1339-4463-80aa-40504ee3b3e5', '299d1720-b61f-4268-8c29-9614aa2d44c2', "
                                        "'2b048a24-2737-4994-9fa5-becc8e466253', '2cd5dc14-a853-49a4-be3c-a5a7178e37bc', '3de548e1-57be-cfea-2b78-83ae3ad95998', "
                                        "'3dee98e4-a6a3-4543-91c3-bbd528447ba7', '3e2d81a3-6263-6ffe-ad5c-8ce04bee07e9', '40e70b98-fed7-47f3-9700-1bce93f9350b', "
                                        "'50a9b68e-b5aa-4d35-9137-3cfebda0a15c', '54295571-9357-43ff-ae74-a83b5138160f', '6191e2d7-5f96-4856-bdab-af0f79f47ae4', "
                                        "'63e577d8-cd34-4235-a0a3-de0500133364', '79cfb666-4fd0-4af7-95df-fb7d96b4e24d', '8121c2f3-4a88-4c33-9899-8fc1273f47ee', "
                                        "'909da964-ef23-4f2a-ba13-f2a8cfd454b6','a2e76fcd-9360-4f6d-a924-000000000001', 'aaa6d664-527e-4d83-9cbb-7ef79ccc7cc8', "
                                        "'b79bfb6c-23be-49eb-b35b-30ff2f501b37', 'ba0d9c79-148c-4a79-8e3c-0665eebe2427', 'bc9bda98-57cd-498f-b993-4ff1ac9dec93', "
                                        "'c62d16f6-81cb-419d-9cac-e46dc394084d', 'd48f8fa7-2512-4fe5-80c8-c0a923412e07', 'd77e3e24-7e6c-4c3f-96d0-a1746337f8fb', "
                                        "'da615c63-a84b-4592-a3d6-a90dd3e92e6e', 'df47190a-7eb7-4aff-985f-2d1d3ad6c6e9', 'e3380196-72cd-499c-a2ba-caa180bd5fe4', "
                                        "'e937863f-f134-4207-803b-d6e686651d6c', 'efcdf98b-5269-45ef-ac7a-0671f09ea9d9'],"
                                        "'circuit_code': i124,'group_id': '8615c885-9cf0-bf0a-6e40-0c11462aa652','limited_to_estate': i1,'look_at': [ i0, i0, i0],"
                                        "'agent_id': '0e346d8b-4433-4d66-a6b0-fd37083abc4c','first_name': 'Kelly','start': 'url'}]}";
        LLChannelDescriptors ch = mBuffer.nextChannel();
        mBuffer.append(ch.out(), (U8*)LOGIN_STREAM, static_cast<S32>(strlen(LOGIN_STREAM)));      /* Flawfinder: ignore */
        ch = mBuffer.nextChannel();
        LLBufferStream istr(ch, &mBuffer);
        LLSD data;
        S32 count = LLSDSerialize::fromNotation(
            data,
            istr,
            mBuffer.count(ch.in()));
        CHECK(count > 0);
        REQUIRE(data.isDefined());
        CHECK(data.type() == LLSD::TypeMap);
        CHECK(data.has("method"));
        REQUIRE(data.has("parameter"));
        LLSD parameter = data["parameter"];
        CHECK(parameter.type() == LLSD::TypeArray);
        LLSD agent_params = parameter[2];
        std::string s_value;
        s_value = agent_params["last_name"].asString();
        UNSCOPED_INFO("last name");
        CHECK(s_value == std::string("Linden"));
        s_value = agent_params["first_name"].asString();
        UNSCOPED_INFO("first name");
        CHECK(s_value == std::string("Kelly"));
        s_value = agent_params["agent_access"].asString();
        UNSCOPED_INFO("agent access");
        CHECK(s_value == std::string("M"));
        s_value = agent_params["group_name"].asString();
        UNSCOPED_INFO("group name");
        CHECK(s_value == std::string("test!"));
        s_value = agent_params["group_title"].asString();
        UNSCOPED_INFO("group title");
        CHECK(s_value == std::string("-> !BLING! <-"));

        LLUUID agent_id("0e346d8b-4433-4d66-a6b0-fd37083abc4c");
        LLUUID id = agent_params["agent_id"];
        CHECK(id == agent_id);
        LLUUID session_id("324cfa9f-fe5d-4d1c-a317-35f20a86a4d1");
        id = agent_params["session_id"];
        CHECK(id == session_id);
        LLUUID group_id ("8615c885-9cf0-bf0a-6e40-0c11462aa652");
        id = agent_params["group_id"];
        CHECK(id == group_id);

        S32 i_val = agent_params["limited_to_estate"];
        UNSCOPED_INFO("limited to estate");
        CHECK(i_val == 1);
        i_val = agent_params["circuit_code"];
        UNSCOPED_INFO("circuit code");
        CHECK(i_val == 124);
    }


    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: fromNotation fails on garbage in a map", "[buffer_stream]")
    {
        std::string val = "{!'foo'@:#'bar'}";
        std::istringstream istr;
        istr.str(val);
        LLSD sd;
        S32 count = LLSDSerialize::fromNotation(sd, istr, val.size());
        CHECK(count == -1);
        CHECK(sd.isUndefined());
    }

    TEST_CASE_METHOD(buffer_and_stream_data, "buffer_stream: fromNotation fails on garbage nested in arrays and maps", "[buffer_stream]")
    {
//#if LL_WINDOWS && _MSC_VER >= 1400
//        skip_fail("Fails on VS2005 due to broken LLSDSerialize::fromNotation() parser.");
//#endif
        std::string val = "{!'foo':[i1,'hi',{@'bar'#:[$i2%,^'baz'&]*}+]=}";
        std::istringstream istr;
        istr.str(val);
        LLSD sd;
        S32 count = LLSDSerialize::fromNotation(sd, istr, val.size());
        CHECK(count == -1);
        CHECK(sd.isUndefined());
    }

    TEST_CASE_METHOD(heap_buffer_data, "heap_buffer: capacity does not change when a segment is made", "[heap_buffer]")
    {
        const S32 BUF_SIZE = 100;
        mBuffer = new LLHeapBuffer(BUF_SIZE);
        UNSCOPED_INFO("before any segment");
        CHECK(mBuffer->capacity() == BUF_SIZE);
        const S32 SEGMENT_SIZE = 50;
        LLSegment segment;
        mBuffer->createSegment(0, SEGMENT_SIZE, segment);
        UNSCOPED_INFO("after a segment");
        CHECK(mBuffer->capacity() == BUF_SIZE);
    }

    TEST_CASE_METHOD(heap_buffer_data, "heap_buffer: a full buffer refuses new segments", "[heap_buffer]")
    {
        const S32 BUF_SIZE = 10;
        mBuffer = new LLHeapBuffer(BUF_SIZE);
        LLSegment segment;
        mBuffer->createSegment(0, BUF_SIZE, segment);
        CHECK(mBuffer->containsSegment(segment));
        CHECK(mBuffer->bytesLeft() == 0);
        bool  created;
        created = mBuffer->createSegment(0, 0, segment);
        UNSCOPED_INFO("zero size segment");
        CHECK_FALSE(created);
        created = mBuffer->createSegment(0, BUF_SIZE, segment);
        UNSCOPED_INFO("segment in a full buffer");
        CHECK_FALSE(created);
    }

    TEST_CASE_METHOD(heap_buffer_data, "heap_buffer: reclaiming the only segment frees the whole buffer", "[heap_buffer]")
    {
        const S32 BUF_SIZE = 10;
        mBuffer = new LLHeapBuffer(BUF_SIZE);
        LLSegment segment;
        mBuffer->createSegment(0, BUF_SIZE, segment);
        CHECK(mBuffer->containsSegment(segment));
        CHECK(mBuffer->bytesLeft() == 0);
        bool reclaimed = mBuffer->reclaimSegment(segment);
        CHECK(reclaimed);
        CHECK(mBuffer->bytesLeft() == BUF_SIZE);
        bool  created;
        created = mBuffer->createSegment(0, 0, segment);
        UNSCOPED_INFO("zero size segment");
        CHECK_FALSE(created);
        created = mBuffer->createSegment(0, BUF_SIZE, segment);
        UNSCOPED_INFO("full size segment after the reclaim");
        CHECK(created);
    }

    TEST_CASE_METHOD(heap_buffer_data, "heap_buffer: space comes back only once every segment is reclaimed", "[heap_buffer]")
    {
        const S32 BUF_SIZE = 10;
        const S32 SEGMENT_SIZE = 4;
        mBuffer = new LLHeapBuffer(BUF_SIZE);
        LLSegment seg1;
        mBuffer->createSegment(0, SEGMENT_SIZE, seg1);
        CHECK(mBuffer->containsSegment(seg1));
        LLSegment seg2;
        mBuffer->createSegment(0, SEGMENT_SIZE, seg2);
        CHECK(mBuffer->containsSegment(seg2));
        LLSegment seg3;
        mBuffer->createSegment(0, SEGMENT_SIZE, seg3);
        CHECK(mBuffer->containsSegment(seg3));
        UNSCOPED_INFO("segment is truncated");
        CHECK(seg3.size() == 2);
        LLSegment seg4;
        bool created;
        created = mBuffer->createSegment(0, SEGMENT_SIZE, seg4);
        UNSCOPED_INFO("fourth segment in a full buffer");
        CHECK_FALSE(created);
        bool reclaimed;
        reclaimed = mBuffer->reclaimSegment(seg1);
        CHECK(reclaimed);
        CHECK(mBuffer->bytesLeft() == 0);
        reclaimed = mBuffer->reclaimSegment(seg2);
        CHECK(reclaimed);
        CHECK(mBuffer->bytesLeft() == 0);
        reclaimed = mBuffer->reclaimSegment(seg3);
        CHECK(reclaimed);
        CHECK(mBuffer->bytesLeft() == BUF_SIZE);
        created = mBuffer->createSegment(0, SEGMENT_SIZE, seg1);
        CHECK(mBuffer->containsSegment(seg1));
        UNSCOPED_INFO("segment after every reclaim");
        CHECK(created);
    }

/*
    template<> template<>
    void bas_object::test<13>()
    {
    }
    template<> template<>
    void bas_object::test<14>()
    {
    }
    template<> template<>
    void bas_object::test<15>()
    {
    }
*/
}


namespace
{
    class PumpAndChainTestData
    {
    protected:
        apr_pool_t* mPool;
        LLPumpIO* mPump;
        LLPumpIO::chain_t mChain;

    public:
        PumpAndChainTestData()
        {
            apr_pool_create(&mPool, NULL);
            mPump = new LLPumpIO(mPool);
        }

        ~PumpAndChainTestData()
        {
            mChain.clear();
            delete mPump;
            apr_pool_destroy(mPool);
        }
    };

}

/*
namespace
{
    struct double_construct
    {
    public:
        double_construct()
        {
            LL_INFOS() << "constructed" << LL_ENDL;
        }
        ~double_construct()
        {
            LL_INFOS() << "destroyed" << LL_ENDL;
        }
    };
    typedef test_group<double_construct> double_construct_test_group;
    typedef double_construct_test_group::object dc_test_object;
    double_construct_test_group dctest("double construct");
    template<> template<>
    void dc_test_object::test<1>()
    {
        ensure("test 1", true);
    }
}
*/

namespace
{
    /**
     * @brief we want to test the pipes & pumps under bad conditions.
     */
    struct pipe_and_pump_fitness
    {
    public:
        pipe_and_pump_fitness()
        {
            LLFrameTimer::updateFrameTime();
            apr_pool_create(&mPool, NULL);
            mPump = new LLPumpIO(mPool);
            mSocket = listenOnAnyPort(mPool, mServerPort);
        }

        // LLSocket::create binds and listens only on a port it is given, so
        // the fixture does as it does on port 0 and reads back the port the
        // OS chose: two runs at once then never contend for one.
        static LLSocket::ptr_t listenOnAnyPort(apr_pool_t* parent, U16& port)
        {
            port = 0;
            apr_pool_t* pool = NULL;
            if (apr_pool_create(&pool, parent) != APR_SUCCESS)
            {
                return LLSocket::ptr_t();
            }
            apr_socket_t* socket = NULL;
            apr_sockaddr_t* address = NULL;
            if (apr_socket_create(&socket, APR_INET, SOCK_STREAM, APR_PROTO_TCP, pool) != APR_SUCCESS
                || apr_sockaddr_info_get(&address, "127.0.0.1", APR_INET, 0, 0, pool) != APR_SUCCESS
                || apr_socket_bind(socket, address) != APR_SUCCESS
                || apr_socket_listen(socket, 10) != APR_SUCCESS
                || apr_socket_addr_get(&address, APR_LOCAL, socket) != APR_SUCCESS)
            {
                apr_pool_destroy(pool);
                return LLSocket::ptr_t();
            }
            port = address->port;
            // The socket takes the pool, as it does one LLSocket::create made.
            return LLSocket::create(socket, pool);
        }

        ~pipe_and_pump_fitness()
        {
            mSocket.reset();
            delete mPump;
            apr_pool_destroy(mPool);
        }

    protected:
        apr_pool_t* mPool;
        LLPumpIO* mPump;
        LLSocket::ptr_t mSocket;
        U16 mServerPort = 0;
    };

    TEST_CASE_METHOD(pipe_and_pump_fitness, "pipe and pump fitness: a reply the client never reads does not stall the pump", "[pipe_and_pump_fitness]")
    {
        LL_DEBUGS() << "fitness_test_object::test<1>()" << LL_ENDL;

        // Set up the server
        //LL_DEBUGS() << "fitness_test_object::test<1> - setting up server."
        //   << LL_ENDL;
        LLPumpIO::chain_t chain;
        typedef LLCloneIOFactory<LLPipeStringInjector> emitter_t;
        emitter_t* emitter = new emitter_t(
            new LLPipeStringInjector("suckers never play me"));
        std::shared_ptr<LLChainIOFactory> factory(emitter);
        LLIOServerSocket* server = new LLIOServerSocket(
            mPool,
            mSocket,
            factory);
        server->setResponseTimeout(SHORT_CHAIN_EXPIRY_SECS);
        chain.push_back(LLIOPipe::ptr_t(server));
        mPump->addChain(chain, NEVER_CHAIN_EXPIRY_SECS);

        // We need to tickle the pump a little to set up the listen()
        //LL_DEBUGS() << "fitness_test_object::test<1> - initializing server."
        //   << LL_ENDL;
        pump_loop(mPump, 0.1f);

        // Set up the client
        //LL_DEBUGS() << "fitness_test_object::test<1> - connecting client."
        //   << LL_ENDL;
        LLSocket::ptr_t client = LLSocket::create(mPool, LLSocket::STREAM_TCP);
        LLHost server_host("127.0.0.1", mServerPort);
        bool connected = client->blockingConnect(server_host);
        CHECK(connected);
        LL_DEBUGS() << "connected" << LL_ENDL;

        // We have connected, since the socket reader does not block,
        // the first call to read data will return EAGAIN, so we need
        // to write something.
        chain.clear();
        chain.push_back(LLIOPipe::ptr_t(new LLPipeStringInjector("hi")));
        chain.push_back(LLIOPipe::ptr_t(new LLIOSocketWriter(client)));
        chain.push_back(LLIOPipe::ptr_t(new LLIONull));
        mPump->addChain(chain, 1.0f);

        // Now, the server should immediately send the data, but we'll
        // never read it. pump for a bit
        F32 elapsed = pump_loop(mPump, 2.0f);
        CHECK(elapsed < 3.0f);
    }

    TEST_CASE_METHOD(pipe_and_pump_fitness, "pipe and pump fitness: a fuzz flood does not stall when the client expires first", "[pipe_and_pump_fitness]")
    {
        LL_DEBUGS() << "fitness_test_object::test<2>()" << LL_ENDL;

        // Set up the server
        LLPumpIO::chain_t chain;
        typedef LLCloneIOFactory<LLIOFuzz> emitter_t;
        emitter_t* emitter = new emitter_t(new LLIOFuzz(1000000));
        std::shared_ptr<LLChainIOFactory> factory(emitter);
        LLIOServerSocket* server = new LLIOServerSocket(
            mPool,
            mSocket,
            factory);
        server->setResponseTimeout(SHORT_CHAIN_EXPIRY_SECS);
        chain.push_back(LLIOPipe::ptr_t(server));
        mPump->addChain(chain, NEVER_CHAIN_EXPIRY_SECS);

        // We need to tickle the pump a little to set up the listen()
        pump_loop(mPump, 0.1f);

        // Set up the client
        LLSocket::ptr_t client = LLSocket::create(mPool, LLSocket::STREAM_TCP);
        LLHost server_host("127.0.0.1", mServerPort);
        bool connected = client->blockingConnect(server_host);
        CHECK(connected);
        LL_DEBUGS() << "connected" << LL_ENDL;

        // We have connected, since the socket reader does not block,
        // the first call to read data will return EAGAIN, so we need
        // to write something.
        chain.clear();
        chain.push_back(LLIOPipe::ptr_t(new LLPipeStringInjector("hi")));
        chain.push_back(LLIOPipe::ptr_t(new LLIOSocketWriter(client)));
        chain.push_back(LLIOPipe::ptr_t(new LLIONull));
        mPump->addChain(chain, SHORT_CHAIN_EXPIRY_SECS / 2.0f);

        // Now, the server should immediately send the data, but we'll
        // never read it. pump for a bit
        F32 elapsed = pump_loop(mPump, SHORT_CHAIN_EXPIRY_SECS * 2.0f);
        CHECK(elapsed < 3.0f);
    }

    TEST_CASE_METHOD(pipe_and_pump_fitness, "pipe and pump fitness: a fuzz flood does not stall when the server times out first", "[pipe_and_pump_fitness]")
    {
        LL_DEBUGS() << "fitness_test_object::test<3>()" << LL_ENDL;

        // Set up the server
        LLPumpIO::chain_t chain;
        typedef LLCloneIOFactory<LLIOFuzz> emitter_t;
        emitter_t* emitter = new emitter_t(new LLIOFuzz(1000000));
        std::shared_ptr<LLChainIOFactory> factory(emitter);
        LLIOServerSocket* server = new LLIOServerSocket(
            mPool,
            mSocket,
            factory);
        server->setResponseTimeout(SHORT_CHAIN_EXPIRY_SECS);
        chain.push_back(LLIOPipe::ptr_t(server));
        mPump->addChain(chain, NEVER_CHAIN_EXPIRY_SECS);

        // We need to tickle the pump a little to set up the listen()
        pump_loop(mPump, 0.1f);

        // Set up the client
        LLSocket::ptr_t client = LLSocket::create(mPool, LLSocket::STREAM_TCP);
        LLHost server_host("127.0.0.1", mServerPort);
        bool connected = client->blockingConnect(server_host);
        CHECK(connected);
        LL_DEBUGS() << "connected" << LL_ENDL;

        // We have connected, since the socket reader does not block,
        // the first call to read data will return EAGAIN, so we need
        // to write something.
        chain.clear();
        chain.push_back(LLIOPipe::ptr_t(new LLPipeStringInjector("hi")));
        chain.push_back(LLIOPipe::ptr_t(new LLIOSocketWriter(client)));
        chain.push_back(LLIOPipe::ptr_t(new LLIONull));
        mPump->addChain(chain, SHORT_CHAIN_EXPIRY_SECS * 2.0f);

        // Now, the server should immediately send the data, but we'll
        // never read it. pump for a bit
        F32 elapsed = pump_loop(mPump, SHORT_CHAIN_EXPIRY_SECS * 2.0f + 1.0f);
        CHECK(elapsed < 4.0f);
    }

    TEST_CASE_METHOD(pipe_and_pump_fitness, "pipe and pump fitness: a fuzz flood does not stall a client that never expires", "[pipe_and_pump_fitness]")
    {
        LL_DEBUGS() << "fitness_test_object::test<4>()" << LL_ENDL;

        // Set up the server
        LLPumpIO::chain_t chain;
        typedef LLCloneIOFactory<LLIOFuzz> emitter_t;
        emitter_t* emitter = new emitter_t(new LLIOFuzz(1000000));
        std::shared_ptr<LLChainIOFactory> factory(emitter);
        LLIOServerSocket* server = new LLIOServerSocket(
            mPool,
            mSocket,
            factory);
        server->setResponseTimeout(SHORT_CHAIN_EXPIRY_SECS + 1.80f);
        chain.push_back(LLIOPipe::ptr_t(server));
        mPump->addChain(chain, NEVER_CHAIN_EXPIRY_SECS);

        // We need to tickle the pump a little to set up the listen()
        pump_loop(mPump, 0.1f);

        // Set up the client
        LLSocket::ptr_t client = LLSocket::create(mPool, LLSocket::STREAM_TCP);
        LLHost server_host("127.0.0.1", mServerPort);
        bool connected = client->blockingConnect(server_host);
        CHECK(connected);
        LL_DEBUGS() << "connected" << LL_ENDL;

        // We have connected, since the socket reader does not block,
        // the first call to read data will return EAGAIN, so we need
        // to write something.
        chain.clear();
        chain.push_back(LLIOPipe::ptr_t(new LLPipeStringInjector("hi")));
        chain.push_back(LLIOPipe::ptr_t(new LLIOSocketWriter(client)));
        chain.push_back(LLIOPipe::ptr_t(new LLIONull));
        mPump->addChain(chain, NEVER_CHAIN_EXPIRY_SECS);

        // Now, the server should immediately send the data, but we'll
        // never read it. pump for a bit
        F32 elapsed = pump_loop(mPump, SHORT_CHAIN_EXPIRY_SECS + 3.0f);
        CHECK(elapsed < DEFAULT_CHAIN_EXPIRY_SECS);
    }

    TEST_CASE_METHOD(pipe_and_pump_fitness, "pipe and pump fitness: client and accepted socket chains time out in turn", "[pipe_and_pump_fitness]")
    {
        // Set up the server
        LLPumpIO::chain_t chain;
        typedef LLCloneIOFactory<LLIOSleeper> sleeper_t;
        sleeper_t* sleeper = new sleeper_t(new LLIOSleeper);
        std::shared_ptr<LLChainIOFactory> factory(sleeper);
        LLIOServerSocket* server = new LLIOServerSocket(
            mPool,
            mSocket,
            factory);
        server->setResponseTimeout(1.0);
        chain.push_back(LLIOPipe::ptr_t(server));
        mPump->addChain(chain, NEVER_CHAIN_EXPIRY_SECS);
        // We need to tickle the pump a little to set up the listen()
        pump_loop(mPump, 0.1f);
        auto count = mPump->runningChains();
        UNSCOPED_INFO("server chain onboard");
        CHECK(count == 1);
        LL_DEBUGS() << "** Server is up." << LL_ENDL;

        // Set up the client
        LLSocket::ptr_t client = LLSocket::create(mPool, LLSocket::STREAM_TCP);
        LLHost server_host("127.0.0.1", mServerPort);
        bool connected = client->blockingConnect(server_host);
        CHECK(connected);
        LL_DEBUGS() << "connected" << LL_ENDL;
        pump_loop(mPump,0.1f);
        count = mPump->runningChains();
        UNSCOPED_INFO("accepted socket chain onboard");
        CHECK(count == 2);
        LL_DEBUGS() << "** Client is connected." << LL_ENDL;

        // We have connected, since the socket reader does not block,
        // the first call to read data will return EAGAIN, so we need
        // to write something.
        chain.clear();
        chain.push_back(LLIOPipe::ptr_t(new LLPipeStringInjector("hi")));
        chain.push_back(LLIOPipe::ptr_t(new LLIOSocketWriter(client)));
        chain.push_back(LLIOPipe::ptr_t(new LLIONull));
        mPump->addChain(chain, 0.2f);
        chain.clear();

        // pump for a bit and make sure all 3 chains are running
        pump_loop(mPump,0.1f);
        count = mPump->runningChains();
        // ensure_equals("client chain onboard", count, 3); commented out because it fails frequently - appears to be timing sensitive
        LL_DEBUGS() << "** request should have been sent." << LL_ENDL;

        // pump for long enough the the client socket closes, and the
        // server socket should not be closed yet.
        pump_loop(mPump,0.2f);
        count = mPump->runningChains();
        UNSCOPED_INFO("client chain timed out");
        CHECK(count == 2);
        LL_DEBUGS() << "** client chain should be closed." << LL_ENDL;

        // At this point, the socket should be closed by the timeout
        pump_loop(mPump,1.0f);
        count = mPump->runningChains();
        UNSCOPED_INFO("accepted socket closed");
        CHECK(count == 1);
        LL_DEBUGS() << "** Sleeper should have timed out.." << LL_ENDL;
    }

    TEST_CASE_METHOD(PumpAndChainTestData, "pump_and_chain: a flushed chain hands the extractor an empty string", "[pump_and_chain]")
    {
        LLPipeStringExtractor* extractor = new LLPipeStringExtractor();

        mChain.push_back(LLIOPipe::ptr_t(new LLIOFlush));
        mChain.push_back(LLIOPipe::ptr_t(extractor));

        LLTimer timer;
        timer.setTimerExpirySec(100.0f);

        mPump->addChain(mChain, DEFAULT_CHAIN_EXPIRY_SECS);
        while(!extractor->done() && !timer.hasExpired())
        {
            mPump->pump();
            mPump->callback();
        }

        CHECK(extractor->done());
        CHECK(extractor->string() == "");
    }
}

/*
'asset_data':b(12100)"{'task_id':ucc706f2d-0b68-68f8-11a4-f1043ff35ca0}\n{\n\tname\tObject|\n\tpermissions 0\n\t{\n\t\tbase_mask\t7fffffff\n\t\towner_mask\t7fffffff\n\t\tgroup_mask\t00000000\n\t\teveryone_mask\t00000000\n\t\tnext_owner_mask\t7fffffff\n\t\tcreator_id\t13fd9595-a47b-4d64-a5fb-6da645f038e0\n\t\towner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tlast_owner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tgroup_id\t00000000-0000-0000-0000-000000000000\n\t}\n\tlocal_id\t217444921\n\ttotal_crc\t323\n\ttype\t2\n\ttask_valid\t2\n\ttravel_access\t13\n\tdisplayopts\t2\n\tdisplaytype\tv\n\tpos\t-0.368634403\t0.00781063363\t-0.569040775\n\toldpos\t150.117996\t25.8658009\t8.19664001\n\trotation\t-0.06293071806430816650390625\t-0.6995697021484375\t-0.7002241611480712890625\t0.1277817934751510620117188\n\tchildpos\t-0.00499999989\t-0.0359999985\t0.307999998\n\tchildrot\t-0.515492737293243408203125\t-0.46601200103759765625\t0.529055416584014892578125\t0.4870323240756988525390625\n\tscale\t0.074629\t0.289956\t0.01\n\tsit_offset\t0\t0\t0\n\tcamera_eye_offset\t0\t0\t0\n\tcamera_at_offset\t0\t0\t0\n\tsit_quat\t0\t0\t0\t1\n\tsit_hint\t0\n\tstate\t160\n\tmaterial\t3\n\tsoundid\t00000000-0000-0000-0000-000000000000\n\tsoundgain\t0\n\tsoundradius\t0\n\tsoundflags\t0\n\ttextcolor\t0 0 0 1\n\tselected\t0\n\tselector\t00000000-0000-0000-0000-000000000000\n\tusephysics\t0\n\trotate_x\t1\n\trotate_y\t1\n\trotate_z\t1\n\tphantom\t0\n\tremote_script_access_pin\t0\n\tvolume_detect\t0\n\tblock_grabs\t0\n\tdie_at_edge\t0\n\treturn_at_edge\t0\n\ttemporary\t0\n\tsandbox\t0\n\tsandboxhome\t0\t0\t0\n\tshape 0\n\t{\n\t\tpath 0\n\t\t{\n\t\t\tcurve\t16\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\tscale_x\t1\n\t\t\tscale_y\t1\n\t\t\tshear_x\t0\n\t\t\tshear_y\t0\n\t\t\ttwist\t0\n\t\t\ttwist_begin\t0\n\t\t\tradius_offset\t0\n\t\t\ttaper_x\t0\n\t\t\ttaper_y\t0\n\t\t\trevolutions\t1\n\t\t\tskew\t0\n\t\t}\n\t\tprofile 0\n\t\t{\n\t\t\tcurve\t1\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\thollow\t0\n\t\t}\n\t}\n\tfaces\t6\n\t{\n\t\timageid\tddde1ffc-678b-3cda-1748-513086bdf01b\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tddde1ffc-678b-3cda-1748-513086bdf01b\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t-1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\tps_next_crc\t1\n\tgpw_bias\t1\n\tip\t0\n\tcomplete\tTRUE\n\tdelay\t50000\n\tnextstart\t0\n\tbirthtime\t1061088050622956\n\treztime\t1094866329019785\n\tparceltime\t1133568981980596\n\ttax_rate\t1.00084\n\tscratchpad\t0\n\t{\n\t\n\t}\n\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n\t\tsale_price\t10\n\t}\n\tcorrect_family_id\t00000000-0000-0000-0000-000000000000\n\thas_rezzed\t0\n\tpre_link_base_mask\t7fffffff\n\tlinked \tchild\n\tdefault_pay_price\t-2\t1\t5\t10\t20\n}\n{'task_id':u61fa7364-e151-0597-774c-523312dae31b}\n{\n\tname\tObject|\n\tpermissions 0\n\t{\n\t\tbase_mask\t7fffffff\n\t\towner_mask\t7fffffff\n\t\tgroup_mask\t00000000\n\t\teveryone_mask\t00000000\n\t\tnext_owner_mask\t7fffffff\n\t\tcreator_id\t13fd9595-a47b-4d64-a5fb-6da645f038e0\n\t\towner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tlast_owner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tgroup_id\t00000000-0000-0000-0000-000000000000\n\t}\n\tlocal_id\t217444922\n\ttotal_crc\t324\n\ttype\t2\n\ttask_valid\t2\n\ttravel_access\t13\n\tdisplayopts\t2\n\tdisplaytype\tv\n\tpos\t-0.367110789\t0.00780026987\t-0.566269755\n\toldpos\t150.115005\t25.8479004\t8.18669987\n\trotation\t0.47332942485809326171875\t-0.380102097988128662109375\t-0.5734078884124755859375\t0.550168216228485107421875\n\tchildpos\t-0.00499999989\t-0.0370000005\t0.305000007\n\tchildrot\t-0.736649334430694580078125\t-0.03042060509324073791503906\t-0.02784589119255542755126953\t0.67501628398895263671875\n\tscale\t0.074629\t0.289956\t0.01\n\tsit_offset\t0\t0\t0\n\tcamera_eye_offset\t0\t0\t0\n\tcamera_at_offset\t0\t0\t0\n\tsit_quat\t0\t0\t0\t1\n\tsit_hint\t0\n\tstate\t160\n\tmaterial\t3\n\tsoundid\t00000000-0000-0000-0000-000000000000\n\tsoundgain\t0\n\tsoundradius\t0\n\tsoundflags\t0\n\ttextcolor\t0 0 0 1\n\tselected\t0\n\tselector\t00000000-0000-0000-0000-000000000000\n\tusephysics\t0\n\trotate_x\t1\n\trotate_y\t1\n\trotate_z\t1\n\tphantom\t0\n\tremote_script_access_pin\t0\n\tvolume_detect\t0\n\tblock_grabs\t0\n\tdie_at_edge\t0\n\treturn_at_edge\t0\n\ttemporary\t0\n\tsandbox\t0\n\tsandboxhome\t0\t0\t0\n\tshape 0\n\t{\n\t\tpath 0\n\t\t{\n\t\t\tcurve\t16\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\tscale_x\t1\n\t\t\tscale_y\t1\n\t\t\tshear_x\t0\n\t\t\tshear_y\t0\n\t\t\ttwist\t0\n\t\t\ttwist_begin\t0\n\t\t\tradius_offset\t0\n\t\t\ttaper_x\t0\n\t\t\ttaper_y\t0\n\t\t\trevolutions\t1\n\t\t\tskew\t0\n\t\t}\n\t\tprofile 0\n\t\t{\n\t\t\tcurve\t1\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\thollow\t0\n\t\t}\n\t}\n\tfaces\t6\n\t{\n\t\timageid\tddde1ffc-678b-3cda-1748-513086bdf01b\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tddde1ffc-678b-3cda-1748-513086bdf01b\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t-1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\tps_next_crc\t1\n\tgpw_bias\t1\n\tip\t0\n\tcomplete\tTRUE\n\tdelay\t50000\n\tnextstart\t0\n\tbirthtime\t1061087839248891\n\treztime\t1094866329020800\n\tparceltime\t1133568981981983\n\ttax_rate\t1.00084\n\tscratchpad\t0\n\t{\n\t\n\t}\n\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n\t\tsale_price\t10\n\t}\n\tcorrect_family_id\t00000000-0000-0000-0000-000000000000\n\thas_rezzed\t0\n\tpre_link_base_mask\t7fffffff\n\tlinked \tchild\n\tdefault_pay_price\t-2\t1\t5\t10\t20\n}\n{'task_id':ub8d68643-7dd8-57af-0d24-8790032aed0c}\n{\n\tname\tObject|\n\tpermissions 0\n\t{\n\t\tbase_mask\t7fffffff\n\t\towner_mask\t7fffffff\n\t\tgroup_mask\t00000000\n\t\teveryone_mask\t00000000\n\t\tnext_owner_mask\t7fffffff\n\t\tcreator_id\t13fd9595-a47b-4d64-a5fb-6da645f038e0\n\t\towner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tlast_owner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tgroup_id\t00000000-0000-0000-0000-000000000000\n\t}\n\tlocal_id\t217444923\n\ttotal_crc\t235\n\ttype\t2\n\ttask_valid\t2\n\ttravel_access\t13\n\tdisplayopts\t2\n\tdisplaytype\tv\n\tpos\t-0.120029509\t-0.00284469454\t-0.0302077383\n\toldpos\t150.710999\t25.8584995\t8.19172001\n\trotation\t0.145459949970245361328125\t-0.1646589934825897216796875\t0.659558117389678955078125\t-0.718826770782470703125\n\tchildpos\t0\t-0.182999998\t-0.26699999\n\tchildrot\t0.991444766521453857421875\t3.271923924330621957778931e-05\t-0.0002416197530692443251609802\t0.1305266767740249633789062\n\tscale\t0.0382982\t0.205957\t0.368276\n\tsit_offset\t0\t0\t0\n\tcamera_eye_offset\t0\t0\t0\n\tcamera_at_offset\t0\t0\t0\n\tsit_quat\t0\t0\t0\t1\n\tsit_hint\t0\n\tstate\t160\n\tmaterial\t3\n\tsoundid\t00000000-0000-0000-0000-000000000000\n\tsoundgain\t0\n\tsoundradius\t0\n\tsoundflags\t0\n\ttextcolor\t0 0 0 1\n\tselected\t0\n\tselector\t00000000-0000-0000-0000-000000000000\n\tusephysics\t0\n\trotate_x\t1\n\trotate_y\t1\n\trotate_z\t1\n\tphantom\t0\n\tremote_script_access_pin\t0\n\tvolume_detect\t0\n\tblock_grabs\t0\n\tdie_at_edge\t0\n\treturn_at_edge\t0\n\ttemporary\t0\n\tsandbox\t0\n\tsandboxhome\t0\t0\t0\n\tshape 0\n\t{\n\t\tpath 0\n\t\t{\n\t\t\tcurve\t32\n\t\t\tbegin\t0.3\n\t\t\tend\t0.65\n\t\t\tscale_x\t1\n\t\t\tscale_y\t0.05\n\t\t\tshear_x\t0\n\t\t\tshear_y\t0\n\t\t\ttwist\t0\n\t\t\ttwist_begin\t0\n\t\t\tradius_offset\t0\n\t\t\ttaper_x\t0\n\t\t\ttaper_y\t0\n\t\t\trevolutions\t1\n\t\t\tskew\t0\n\t\t}\n\t\tprofile 0\n\t\t{\n\t\t\tcurve\t0\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\thollow\t0\n\t\t}\n\t}\n\tfaces\t3\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1.57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1.57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1.57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\tps_next_crc\t1\n\tgpw_bias\t1\n\tip\t0\n\tcomplete\tTRUE\n\tdelay\t50000\n\tnextstart\t0\n\tbirthtime\t1061087534454174\n\treztime\t1094866329021741\n\tparceltime\t1133568981982889\n\ttax_rate\t1.00326\n\tscratchpad\t0\n\t{\n\t\n\t}\n\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n\t\tsale_price\t10\n\t}\n\tcorrect_family_id\t00000000-0000-0000-0000-000000000000\n\thas_rezzed\t0\n\tpre_link_base_mask\t7fffffff\n\tlinked \tchild\n\tdefault_pay_price\t-2\t1\t5\t10\t20\n}\n{'task_id':ue4b19200-9d33-962f-c8c5-6f25be3a3fd0}\n{\n\tname\tApotheosis_Immolaine_tail|\n\tpermissions 0\n\t{\n\t\tbase_mask\t7fffffff\n\t\towner_mask\t7fffffff\n\t\tgroup_mask\t00000000\n\t\teveryone_mask\t00000000\n\t\tnext_owner_mask\t7fffffff\n\t\tcreator_id\t13fd9595-a47b-4d64-a5fb-6da645f038e0\n\t\towner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tlast_owner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tgroup_id\t00000000-0000-0000-0000-000000000000\n\t}\n\tlocal_id\t217444924\n\ttotal_crc\t675\n\ttype\t1\n\ttask_valid\t2\n\ttravel_access\t13\n\tdisplayopts\t2\n\tdisplaytype\tv\n\tpos\t-0.34780401\t-0.00968400016\t-0.260098994\n\toldpos\t0\t0\t0\n\trotation\t0.73164522647857666015625\t-0.67541944980621337890625\t-0.07733880728483200073242188\t0.05022468417882919311523438\n\tvelocity\t0\t0\t0\n\tangvel\t0\t0\t0\n\tscale\t0.0382982\t0.32228\t0.383834\n\tsit_offset\t0\t0\t0\n\tcamera_eye_offset\t0\t0\t0\n\tcamera_at_offset\t0\t0\t0\n\tsit_quat\t0\t0\t0\t1\n\tsit_hint\t0\n\tstate\t160\n\tmaterial\t3\n\tsoundid\t00000000-0000-0000-0000-000000000000\n\tsoundgain\t0\n\tsoundradius\t0\n\tsoundflags\t0\n\ttextcolor\t0 0 0 1\n\tselected\t0\n\tselector\t00000000-0000-0000-0000-000000000000\n\tusephysics\t0\n\trotate_x\t1\n\trotate_y\t1\n\trotate_z\t1\n\tphantom\t0\n\tremote_script_access_pin\t0\n\tvolume_detect\t0\n\tblock_grabs\t0\n\tdie_at_edge\t0\n\treturn_at_edge\t0\n\ttemporary\t0\n\tsandbox\t0\n\tsandboxhome\t0\t0\t0\n\tshape 0\n\t{\n\t\tpath 0\n\t\t{\n\t\t\tcurve\t32\n\t\t\tbegin\t0.3\n\t\t\tend\t0.65\n\t\t\tscale_x\t1\n\t\t\tscale_y\t0.05\n\t\t\tshear_x\t0\n\t\t\tshear_y\t0\n\t\t\ttwist\t0\n\t\t\ttwist_begin\t0\n\t\t\tradius_offset\t0\n\t\t\ttaper_x\t0\n\t\t\ttaper_y\t0\n\t\t\trevolutions\t1\n\t\t\tskew\t0\n\t\t}\n\t\tprofile 0\n\t\t{\n\t\t\tcurve\t0\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\thollow\t0\n\t\t}\n\t}\n\tfaces\t3\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1.57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1.57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1.57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\tps_next_crc\t1\n\tgpw_bias\t1\n\tip\t0\n\tcomplete\tTRUE\n\tdelay\t50000\n\tnextstart\t0\n\tbirthtime\t1061087463950186\n\treztime\t1094866329022555\n\tparceltime\t1133568981984359\n\tdescription\t(No Description)|\n\ttax_rate\t1.01736\n\tnamevalue\tAttachPt U32 RW S 10\n\tnamevalue\tAttachmentOrientation VEC3 RW DS -3.110088, -0.182018, 1.493795\n\tnamevalue\tAttachmentOffset VEC3 RW DS -0.347804, -0.009684, -0.260099\n\tnamevalue\tAttachItemID STRING RW SV 20f36c3a-b44b-9bc7-87f3-018bfdfc8cda\n\tscratchpad\t0\n\t{\n\t\n\t}\n\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n\t\tsale_price\t10\n\t}\n\torig_asset_id\t8747acbc-d391-1e59-69f1-41d06830e6c0\n\torig_item_id\t20f36c3a-b44b-9bc7-87f3-018bfdfc8cda\n\tfrom_task_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\tcorrect_family_id\t00000000-0000-0000-0000-000000000000\n\thas_rezzed\t0\n\tpre_link_base_mask\t7fffffff\n\tlinked \tlinked\n\tdefault_pay_price\t-2\t1\t5\t10\t20\n}\n"
*/
