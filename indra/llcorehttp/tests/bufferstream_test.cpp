/**
 * @file bufferstream_test.cpp
 * @brief unit tests for the LLCore::BufferArrayStreamBuf/BufferArrayStream classes
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

#include "bufferstream.h"

#include <iostream>

#include "llsd.h"
#include "llsdserialize.h"


using namespace LLCore;


namespace
{

typedef BufferArrayStreamBuf::traits_type tst_traits_t;


TEST_CASE("BufferStream Tests: BufferArrayStreamBuf over a NULL BufferArray fails every call", "[BufferStream_Tests]")
{
    // a stream buffer over no BufferArray
    BufferArrayStreamBuf * bsb = new BufferArrayStreamBuf(NULL);

    // Not much will work with a NULL
    CHECK(tst_traits_t::eof() == bsb->underflow());
    CHECK(tst_traits_t::eof() == bsb->uflow());
    CHECK(tst_traits_t::eof() == bsb->pbackfail('c'));
    CHECK(bsb->showmanyc() == -1);
    CHECK(tst_traits_t::eof() == bsb->overflow('c'));
    CHECK(bsb->xsputn("blah", 4) == 0);
    CHECK(bsb->seekoff(0, std::ios_base::beg, std::ios_base::in) == std::streampos(-1));

    // the stream buffer is not ref counted
    delete bsb;
    bsb = NULL;
}


TEST_CASE("BufferStream Tests: BufferArrayStream over a NULL BufferArray starts out good", "[BufferStream_Tests]")
{
    // a stream over no BufferArray
    BufferArrayStream * bas = new BufferArrayStream(NULL);

    // Not much will work with a NULL here
    CHECK_FALSE(bas->eof());
    CHECK_FALSE(bas->fail());
    CHECK(bas->good());

    // the stream is not ref counted
    delete bas;
    bas = NULL;
}


TEST_CASE("BufferStream Tests: BufferArrayStreamBuf takes a reference on an empty BufferArray", "[BufferStream_Tests]")
{
    // create a new ref counted BufferArray with implicit reference
    BufferArray * ba = new BufferArray;
    BufferArrayStreamBuf * bsb = new BufferArrayStreamBuf(ba);
    CHECK(ba->getRefCount() == 2);

    // I can release my ref on the BA
    ba->release();
    ba = NULL;

    // the stream buffer is not ref counted; deleting it drops the BufferArray's last reference
    delete bsb;
    bsb = NULL;
}


TEST_CASE("BufferStream Tests: BufferArrayStream holds a reference on an empty BufferArray while it lives", "[BufferStream_Tests]")
{
    // create a new ref counted BufferArray with implicit reference
    BufferArray * ba = new BufferArray;

    {
        BufferArrayStream bas(ba);
        CHECK(ba->getRefCount() == 2);
    }
    CHECK(ba->getRefCount() == 1);

    // release the implicit reference, causing the object to be released
    ba->release();
    ba = NULL;
}


TEST_CASE("BufferStream Tests: BufferArrayStreamBuf over an empty BufferArray seeks to its start and reads nothing", "[BufferStream_Tests]")
{
    // create a new ref counted BufferArray with implicit reference
    BufferArray * ba = new BufferArray;
    BufferArrayStreamBuf bsb(ba);
    ba->release();
    ba = NULL;

    CHECK(bsb.seekoff(0, std::ios_base::beg, std::ios_base::in) == std::streampos(0));
    CHECK(bsb.seekoff(0, std::ios_base::end, std::ios_base::in) == std::streampos(0));
    CHECK(tst_traits_t::eof() == bsb.underflow());
    CHECK(tst_traits_t::eof() == bsb.pbackfail(tst_traits_t::eof()));
}


TEST_CASE("BufferStream Tests: BufferArrayStreamBuf reads and appends and seeks a BufferArray", "[BufferStream_Tests]")
{
    // create a new ref counted BufferArray with implicit reference
    BufferArray * ba = new BufferArray;
    const char * content("This is a string.  A fragment.");
    const size_t c_len(strlen(content));
    ba->append(content, c_len);

    // Creat an adapter for the BufferArray
    BufferArrayStreamBuf * bsb = new BufferArrayStreamBuf(ba);

    // I can release my ref on the BA
    ba->release();
    ba = NULL;

    // Various static state
    CHECK(bsb->underflow() == 'T');
    UNSCOPED_INFO("underflow() does not advance");
    CHECK(bsb->underflow() == 'T');
    CHECK(bsb->uflow() == 'T');
    CHECK(bsb->uflow() == 'h');
    CHECK(tst_traits_t::eof() == bsb->pbackfail('i'));
    CHECK(tst_traits_t::eof() == bsb->pbackfail('T'));
    CHECK(bsb->pbackfail('h') == 'h');
    UNSCOPED_INFO("showmanyc() is everything but the 'T'");
    CHECK(bsb->showmanyc() == (c_len - 1));
    UNSCOPED_INFO("overflow() appends");
    CHECK(bsb->overflow('c') == 'c');
    UNSCOPED_INFO("showmanyc() reflects append");
    CHECK(bsb->showmanyc() == (c_len - 1 + 1));
    UNSCOPED_INFO("xsputn() appends");
    CHECK(bsb->xsputn("bla!", 4) == 4);
    UNSCOPED_INFO("showmanyc() reflects 2nd append");
    CHECK(bsb->showmanyc() == (c_len - 1 + 5));
    CHECK(bsb->seekoff(0, std::ios_base::beg, std::ios_base::in) == std::streampos(0));
    CHECK(bsb->seekoff(4, std::ios_base::cur, std::ios_base::in) == std::streampos(4));
    UNSCOPED_INFO("showmanyc() picks up seekoff");
    CHECK(bsb->showmanyc() == (c_len + 5 - 4));
    UNSCOPED_INFO("the end is past the last character");
    CHECK(bsb->seekoff(0, std::ios_base::end, std::ios_base::in) == std::streampos(c_len + 5));
    CHECK(tst_traits_t::eof() == bsb->underflow());
    CHECK(bsb->showmanyc() == 0);
    // so the last character, the '!', is the one that puts back there
    CHECK(tst_traits_t::eof() == bsb->pbackfail('a'));
    CHECK(bsb->pbackfail('!') == '!');
    UNSCOPED_INFO("an offset from the end counts back from it");
    CHECK(bsb->seekoff(-2, std::ios_base::end, std::ios_base::in) == std::streampos(c_len + 3));
    CHECK(bsb->underflow() == 'a');
    UNSCOPED_INFO("past the end is the end");
    CHECK(bsb->seekoff(1, std::ios_base::end, std::ios_base::in) == std::streampos(c_len + 5));
    UNSCOPED_INFO("before the start fails and leaves the position");
    CHECK(bsb->seekoff(-1, std::ios_base::beg, std::ios_base::in) == std::streampos(-1));
    CHECK(bsb->seekoff(0, std::ios_base::cur, std::ios_base::in) == std::streampos(c_len + 5));
    UNSCOPED_INFO("a write from the end overwrites the last character");
    CHECK(bsb->seekoff(-1, std::ios_base::end, std::ios_base::out) == std::streampos(c_len + 4));
    CHECK(bsb->xsputn("?", 1) == 1);
    CHECK(bsb->seekoff(-1, std::ios_base::end, std::ios_base::in) == std::streampos(c_len + 4));
    CHECK(bsb->underflow() == '?');

    // the stream buffer is not ref counted; deleting it drops the BufferArray's last reference
    delete bsb;
    bsb = NULL;
}


TEST_CASE("BufferStream Tests: BufferArrayStream writes to and reads back a BufferArray", "[BufferStream_Tests]")
{
    // create a new ref counted BufferArray with implicit reference
    BufferArray * ba = new BufferArray;
    //const char * content("This is a string.  A fragment.");
    //const size_t c_len(strlen(content));
    //ba->append(content, strlen(content));

    {
        // Creat an adapter for the BufferArray
        BufferArrayStream bas(ba);

        // Basic operations
        bas << "Hello" << 27 << ".";
        CHECK(ba->size() == 8);

        std::string str;
        bas >> str;
        CHECK(str == "Hello27.");
    }

    // release the implicit reference, causing the object to be released
    ba->release();
    ba = NULL;
}


TEST_CASE("BufferStream Tests: BufferArrayStream with LLSD serialization", "[BufferStream_Tests]")
{
    // create a new ref counted BufferArray with implicit reference
    BufferArray * ba = new BufferArray;

    {
        // Creat an adapter for the BufferArray
        BufferArrayStream bas(ba);

        // LLSD
        LLSD llsd = LLSD::emptyMap();

        llsd["int"] = LLSD::Integer(3);
        llsd["float"] = LLSD::Real(923289.28992);
        llsd["string"] = LLSD::String("aksjdl;ajsdgfjgfal;sdgjakl;sdfjkl;ajsdfkl;ajsdfkl;jaskl;dfj");

        LLSD llsd_map = LLSD::emptyMap();
        llsd_map["int"] = LLSD::Integer(-2889);
        llsd_map["float"] = LLSD::Real(2.37829e32);
        llsd_map["string"] = LLSD::String("OHIGODHSPDGHOSDHGOPSHDGP");

        llsd["map"] = llsd_map;

        // Serialize it
        LLSDSerialize::toXML(llsd, bas);

        std::string str;
        bas >> str;
        // std::cout << "SERIALIZED LLSD:  " << str << std::endl;
        CHECK(str.size() > 60);
    }

    // release the implicit reference, causing the object to be released
    ba->release();
    ba = NULL;
}


}  // anonymous namespace
