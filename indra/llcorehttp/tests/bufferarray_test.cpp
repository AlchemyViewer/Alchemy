/**
 * @file bufferarray_test.cpp
 * @brief unit tests for the LLCore::BufferArray class
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012, Linden Research, Inc.
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

#include "bufferarray.h"

#include <iostream>


using namespace LLCore;



namespace
{
TEST_CASE("BufferArray Tests: a new BufferArray holds one reference and no data", "[BufferArray_Tests]")
{
    // create a new ref counted object with an implicit reference
    BufferArray * ba = new BufferArray();
    CHECK(ba->getRefCount() == 1);
    CHECK(ba->size() == 0);

    // Try to read
    char buffer[20];
    size_t read_len(ba->read(0, buffer, sizeof(buffer)));
    CHECK(read_len == 0);

    // release the implicit reference, causing the object to be released
    ba->release();
}

TEST_CASE("BufferArray Tests: BufferArray single write", "[BufferArray_Tests]")
{
    // create a new ref counted object with an implicit reference
    BufferArray * ba = new BufferArray();

    // write some data to the buffer
    char str1[] = "abcdefghij";
    char buffer[256];

    size_t len = ba->write(0, str1, strlen(str1));
    CHECK(strlen(str1) == len);
    CHECK(strlen(str1) == ba->size());

    // read some data back
    memset(buffer, 'X', sizeof(buffer));
    len = ba->read(2, buffer, 2);
    CHECK(len == 2);
    CHECK(buffer[0] == 'c');
    CHECK(buffer[1] == 'd');
    UNSCOPED_INFO("nothing written past what was read");
    CHECK(buffer[2] == 'X');

    // release the implicit reference, causing the object to be released
    ba->release();
}


TEST_CASE("BufferArray Tests: BufferArray multiple writes", "[BufferArray_Tests]")
{
    // create a new ref counted object with an implicit reference
    BufferArray * ba = new BufferArray();

    // write some data to the buffer
    char str1[] = "abcdefghij";
    size_t str1_len(strlen(str1));
    char buffer[256];

    size_t len = ba->write(0, str1, str1_len);
    CHECK(str1_len == len);
    CHECK(str1_len == ba->size());

    // again...
    len = ba->write(str1_len, str1, strlen(str1));
    CHECK(str1_len == len);
    CHECK((2 * str1_len) == ba->size());

    // read some data back
    memset(buffer, 'X', sizeof(buffer));
    len = ba->read(8, buffer, 4);
    CHECK(len == 4);
    CHECK(buffer[0] == 'i');
    CHECK(buffer[1] == 'j');
    CHECK(buffer[2] == 'a');
    CHECK(buffer[3] == 'b');
    UNSCOPED_INFO("nothing written past what was read");
    CHECK(buffer[4] == 'X');

    // Read whole thing
    memset(buffer, 'X', sizeof(buffer));
    len = ba->read(0, buffer, sizeof(buffer));
    CHECK((2 * str1_len) == len);
    CHECK(strncmp(buffer, str1, str1_len) == 0);
    CHECK(strncmp(&buffer[str1_len], str1, str1_len) == 0);
    UNSCOPED_INFO("nothing written past what was read");
    CHECK(buffer[2 * str1_len] == 'X');

    // release the implicit reference, causing the object to be released
    ba->release();
}

TEST_CASE("BufferArray Tests: BufferArray overwriting", "[BufferArray_Tests]")
{
    // create a new ref counted object with an implicit reference
    BufferArray * ba = new BufferArray();

    // write some data to the buffer
    char str1[] = "abcdefghij";
    size_t str1_len(strlen(str1));
    char str2[] = "ABCDEFGHIJ";
    char buffer[256];

    size_t len = ba->write(0, str1, str1_len);
    CHECK(str1_len == len);
    CHECK(str1_len == ba->size());

    // again...
    len = ba->write(str1_len, str1, strlen(str1));
    CHECK(str1_len == len);
    CHECK((2 * str1_len) == ba->size());

    // reposition and overwrite
    len = ba->write(8, str2, 4);
    CHECK(len == 4);

    // Leave position and read verifying content (stale really from seek() days)
    memset(buffer, 'X', sizeof(buffer));
    len = ba->read(12, buffer, 4);
    CHECK(len == 4);
    CHECK(buffer[0] == 'c');
    CHECK(buffer[1] == 'd');
    CHECK(buffer[2] == 'e');
    CHECK(buffer[3] == 'f');
    UNSCOPED_INFO("nothing written past what was read");
    CHECK(buffer[4] == 'X');

    // reposition and check
    len = ba->read(6, buffer, 8);
    CHECK(len == 8);
    CHECK(buffer[0] == 'g');
    CHECK(buffer[1] == 'h');
    CHECK(buffer[2] == 'A');
    CHECK(buffer[3] == 'B');
    CHECK(buffer[4] == 'C');
    CHECK(buffer[5] == 'D');
    CHECK(buffer[6] == 'c');
    CHECK(buffer[7] == 'd');
    UNSCOPED_INFO("nothing written past what was read");
    CHECK(buffer[8] == 'X');

    // release the implicit reference, causing the object to be released
    ba->release();
}

TEST_CASE("BufferArray Tests: BufferArray multiple writes - sequential reads", "[BufferArray_Tests]")
{
    // create a new ref counted object with an implicit reference
    BufferArray * ba = new BufferArray();

    // write some data to the buffer
    char str1[] = "abcdefghij";
    size_t str1_len(strlen(str1));
    char buffer[256];

    size_t len = ba->write(0, str1, str1_len);
    CHECK(str1_len == len);
    CHECK(str1_len == ba->size());

    // again...
    len = ba->write(str1_len, str1, str1_len);
    CHECK(str1_len == len);
    CHECK((2 * str1_len) == ba->size());

    // read some data back
    memset(buffer, 'X', sizeof(buffer));
    len = ba->read(8, buffer, 4);
    CHECK(len == 4);
    CHECK(buffer[0] == 'i');
    CHECK(buffer[1] == 'j');
    CHECK(buffer[2] == 'a');
    CHECK(buffer[3] == 'b');
    UNSCOPED_INFO("nothing written past what was read");
    CHECK(buffer[4] == 'X');

    // Read on from where the last read stopped
    memset(buffer, 'X', sizeof(buffer));
    len = ba->read(12, buffer, sizeof(buffer));
    CHECK((str1_len - 2) == len);
    CHECK(strncmp(buffer, str1+2, str1_len-2) == 0);
    UNSCOPED_INFO("nothing written past what was read");
    CHECK(buffer[str1_len-2] == 'X');

    // release the implicit reference, causing the object to be released
    ba->release();
}

TEST_CASE("BufferArray Tests: BufferArray overwrite spanning blocks and appending", "[BufferArray_Tests]")
{
    // create a new ref counted object with an implicit reference
    BufferArray * ba = new BufferArray();

    // write some data to the buffer
    char str1[] = "abcdefghij";
    size_t str1_len(strlen(str1));
    char str2[] = "ABCDEFGHIJKLMNOPQRST";
    size_t str2_len(strlen(str2));
    char buffer[256];

    size_t len = ba->write(0, str1, str1_len);
    CHECK(str1_len == len);
    CHECK(str1_len == ba->size());

    // again...
    len = ba->write(str1_len, str1, strlen(str1));
    CHECK(str1_len == len);
    CHECK((2 * str1_len) == ba->size());

    // reposition and overwrite
    len = ba->write(8, str2, str2_len);
    CHECK(str2_len == len);

    // A zero-length read at the end of the data reads nothing
    memset(buffer, 'X', sizeof(buffer));
    len = ba->read(8 + str2_len, buffer, 0);
    CHECK(len == 0);
    UNSCOPED_INFO("nothing written past what was read");
    CHECK(buffer[0] == 'X');

    // reposition and check
    len = ba->read(0, buffer, sizeof(buffer));
    CHECK((str1_len + str2_len - 2) == len);
    CHECK(strncmp(buffer, str1, str1_len-2) == 0);
    CHECK(strncmp(buffer+str1_len-2, str2, str2_len) == 0);
    UNSCOPED_INFO("nothing written past what was read");
    CHECK(buffer[str1_len + str2_len - 2] == 'X');

    // release the implicit reference, causing the object to be released
    ba->release();
}

TEST_CASE("BufferArray Tests: BufferArray overwrite spanning blocks and sequential writes", "[BufferArray_Tests]")
{
    // create a new ref counted object with an implicit reference
    BufferArray * ba = new BufferArray();

    // write some data to the buffer
    char str1[] = "abcdefghij";
    size_t str1_len(strlen(str1));
    char str2[] = "ABCDEFGHIJKLMNOPQRST";
    size_t str2_len(strlen(str2));
    char buffer[256];

    // 2x str1
    size_t len = ba->write(0, str1, str1_len);
    len = ba->write(str1_len, str1, str1_len);

    // reposition and overwrite
    len = ba->write(6, str2, 2);
    CHECK(len == 2);

    len = ba->write(8, str2, 2);
    CHECK(len == 2);

    len = ba->write(10, str2, 2);
    CHECK(len == 2);

    // append some data
    len = ba->append(str2, str2_len);
    CHECK(str2_len == len);

    // append some more
    void * out_buf(ba->appendBufferAlloc(str1_len));
    memcpy(out_buf, str1, str1_len);

    // And some final writes
    len = ba->write(3 * str1_len + str2_len, str2, 2);
    CHECK(len == 2);

    // Check contents
    memset(buffer, 'X', sizeof(buffer));
    len = ba->read(0, buffer, sizeof(buffer));
    CHECK((3 * str1_len + str2_len + 2) == len);
    CHECK(strncmp(buffer, str1, 6) == 0);
    CHECK(strncmp(buffer + 6, str2, 2) == 0);
    CHECK(strncmp(buffer + 8, str2, 2) == 0);
    CHECK(strncmp(buffer + 10, str2, 2) == 0);
    CHECK(strncmp(buffer + str1_len + 2, str1 + 2, str1_len - 2) == 0);
    CHECK(strncmp(buffer + str1_len + str1_len, str2, str2_len) == 0);
    CHECK(strncmp(buffer + str1_len + str1_len + str2_len, str1, str1_len) == 0);
    CHECK(strncmp(buffer + str1_len + str1_len + str2_len + str1_len, str2, 2) == 0);
    UNSCOPED_INFO("nothing written past what was read");
    CHECK(buffer[str1_len + str1_len + str2_len + str1_len + 2] == 'X');

    // release the implicit reference, causing the object to be released
    ba->release();
}

TEST_CASE("BufferArray Tests: BufferArray zero-length appendBufferAlloc", "[BufferArray_Tests]")
{
    // create a new ref counted object with an implicit reference
    BufferArray * ba = new BufferArray();

    // write some data to the buffer
    char str1[] = "abcdefghij";
    size_t str1_len(strlen(str1));
    char str2[] = "ABCDEFGHIJKLMNOPQRST";
    size_t str2_len(strlen(str2));
    char buffer[256];

    // 2x str1
    size_t len = ba->write(0, str1, str1_len);
    len = ba->write(str1_len, str1, str1_len);

    // zero-length allocate (we allow this with a valid pointer returned)
    void * out_buf(ba->appendBufferAlloc(0));
    REQUIRE(NULL != out_buf);

    // Do it again
    void * out_buf2(ba->appendBufferAlloc(0));
    REQUIRE(NULL != out_buf2);
    CHECK(out_buf != out_buf2);

    // And some final writes
    len = ba->write(2 * str1_len, str2, str2_len);

    // Check contents
    memset(buffer, 'X', sizeof(buffer));
    len = ba->read(0, buffer, sizeof(buffer));
    CHECK((2 * str1_len + str2_len) == len);
    CHECK(strncmp(buffer, str1, str1_len) == 0);
    CHECK(strncmp(buffer + str1_len, str1, str1_len) == 0);
    CHECK(strncmp(buffer + str1_len + str1_len, str2, str2_len) == 0);
    UNSCOPED_INFO("nothing written past what was read");
    CHECK(buffer[str1_len + str1_len + str2_len] == 'X');

    // release the implicit reference, causing the object to be released
    ba->release();
}

}  // anonymous namespace
