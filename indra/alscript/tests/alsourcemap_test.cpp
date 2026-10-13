/**
 * @file alsourcemap_test.cpp
 * @brief Tests for ALSourceMap: verbatim runs one segment, a line looked up by halves, a map read through another.
 *
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
 */

#include "linden_common.h"

#include "../preprocessor/alsourcemap.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    ALSourceMap::Segment segment(S32 out_line, S32 out_column, S32 length, S32 file, S32 line, S32 column, bool verbatim = true)
    {
        ALSourceMap::Segment s;
        s.outLine   = out_line;
        s.outColumn = out_column;
        s.length    = length;
        s.file      = file;
        s.line      = line;
        s.column    = column;
        s.verbatim  = verbatim;
        return s;
    }
}

namespace
{
    TEST_CASE("alsourcemap: verbatim tokens in a run are one segment; a macro or line or file starts anew", "[alsourcemap]")
    {
        ALSourceMap map;
        map.addFile("script", "script");
        map.addFile("lib", "disk:/lib");
        map.add(segment(0, 0, 7, 0, 0, 0));        // integer
        map.add(segment(0, 7, 1, 0, 0, 7));        // (space)
        map.add(segment(0, 8, 1, 0, 0, 8));        // x
        map.add(segment(0, 9, 5, 0, 3, 2, false)); // a macro's
        map.add(segment(0, 14, 1, 0, 0, 12));      // ; -- not where the last left off
        map.add(segment(1, 0, 4, 1, 0, 0));        // another file
        map.finish();
        {
            INFO("the first three one");
            CHECK(map.toSource(0, 8).column == 8);
            CHECK(map.toSource(0, 0).line == 0);
        }
        ALSourceMap::Loc begin, end;
        {
            INFO("one stretch copied as it stands");
            CHECK(map.verbatimSpan(0, 0, 9, begin, end));
            CHECK(begin.column == 0);
            CHECK(end.column == 9);
        }
        {
            INFO("the macro's to its invocation");
            CHECK(map.toSource(0, 11).line == 3);
            CHECK(map.toSource(0, 11).column == 2);
        }
        UNSCOPED_INFO("the next its own");
        CHECK(map.toSource(0, 14).column == 12);
        {
            INFO("the other file's");
            CHECK(map.toSource(1, 2).file == 1);
            CHECK(map.toSource(1, 2).column == 2);
        }
        {
            INFO("and back");
            CHECK(map.toExpanded(0, 0, 8).line == 0);
            CHECK(map.toExpanded(0, 0, 8).column == 8);
        }
    }

    TEST_CASE("alsourcemap: the segment at or before a column on a long line; nothing on a line with none", "[alsourcemap]")
    {
        ALSourceMap map;
        map.addFile("script", "script");
        for (S32 i = 0; i < 3000; ++i)
        {
            map.add(segment(0, 4 + i * 3, 3, 0, 1, i, false));
        }
        map.add(segment(2, 0, 3, 0, 5, 0));
        map.finish();
        UNSCOPED_INFO("the first");
        CHECK(map.toSource(0, 4).column == 0);
        UNSCOPED_INFO("in the middle");
        CHECK(map.toSource(0, 4 + 1500 * 3 + 1).column == 1500);
        UNSCOPED_INFO("before any: the first");
        CHECK(map.toSource(0, 1).column == 0);
        UNSCOPED_INFO("past the last: the last");
        CHECK(map.toSource(0, 20000).column == 2999);
        {
            INFO("a line with none: nowhere");
            CHECK_FALSE(map.toSource(1, 0).found());
            CHECK(map.toSource(2, 1).found());
        }
    }

    TEST_CASE("alsourcemap: a map composed through another reads a stretch through each inner segment", "[alsourcemap]")
    {
        // The inner map: its output line 0 is `abc` from the script's line
        // 4, then `MACRO` a macro made at line 9 column 1, then `xyz` from
        // the include's line 2.
        ALSourceMap inner;
        inner.addFile("script", "script");
        inner.addFile("lib", "disk:/lib");
        inner.add(segment(0, 0, 3, 0, 4, 0));
        inner.add(segment(0, 3, 5, 0, 9, 1, false));
        inner.add(segment(0, 8, 3, 1, 2, 0));
        inner.finish();
        // The outer copies all of that line, and nothing of line 7.
        ALSourceMap outer;
        outer.addFile("inner", "inner");
        outer.add(segment(0, 2, 11, 0, 0, 0));
        outer.add(segment(1, 0, 3, 0, 7, 0));
        outer.finish();
        const ALSourceMap through = outer.composed(inner);
        {
            INFO("the script's own, exact");
            CHECK(through.toSource(0, 3).file == 0);
            CHECK(through.toSource(0, 3).line == 4);
            CHECK(through.toSource(0, 3).column == 1);
        }
        {
            INFO("the macro's, to its invocation");
            CHECK(through.toSource(0, 7).line == 9);
            CHECK(through.toSource(0, 7).column == 1);
        }
        {
            INFO("the include's, exact");
            CHECK(through.toSource(0, 11).file == 1);
            CHECK(through.toSource(0, 11).column == 1);
        }
        UNSCOPED_INFO("a line the inner has nothing for: dropped");
        CHECK_FALSE(through.toSource(1, 0).found());
        ALSourceMap::Loc begin, end;
        {
            INFO("the copied stretches stay verbatim");
            CHECK(through.verbatimSpan(0, 2, 5, begin, end));
            CHECK_FALSE(through.verbatimSpan(0, 5, 10, begin, end));
        }
    }

    TEST_CASE("alsourcemap: a text mapped to itself: every line its own even the last without a newline", "[alsourcemap]")
    {
        const ALSourceMap map = ALSourceMap::identity("one\ntwo three\nlast", "same");
        REQUIRE(map.files().size() == 1);
        CHECK(map.files()[0].name == "same");
        CHECK(map.toSource(1, 5).line == 1);
        CHECK(map.toSource(1, 5).column == 5);
        CHECK(map.toSource(2, 2).line == 2);
    }
    TEST_CASE("alsourcemap: a verbatim stretch on a long line: the first segment to reach the column", "[alsourcemap]")
    {
        ALSourceMap map;
        map.addFile("script", "script");
        // A long line of alternating pieces: a macro's product, then a
        // token copied from the source, then a macro's again...
        S32 out = 0;
        for (S32 i = 0; i < 400; ++i)
        {
            map.add(segment(0, out, 4, 0, 0, 0, false));
            map.add(segment(0, out + 4, 3, 0, 1, i * 10));
            out += 7;
        }
        map.finish();
        ALSourceMap::Loc begin, end;
        const S32        at = 7 * 250 + 4;
        {
            INFO("within a token copied, deep in the line");
            REQUIRE(map.verbatimSpan(0, at + 1, at + 3, begin, end));
            CHECK(begin.line == 1);
            CHECK(begin.column == 2501);
            CHECK(end.column == 2503);
        }
        {
            INFO("an insertion just after it goes with it");
            CHECK(map.verbatimSpan(0, at + 3, at + 3, begin, end));
            CHECK(begin.column == 2503);
        }
        UNSCOPED_INFO("an insertion just before it goes with the macro's product before it");
        CHECK_FALSE(map.verbatimSpan(0, at, at, begin, end));
        UNSCOPED_INFO("and so does a stretch from its first column");
        CHECK_FALSE(map.verbatimSpan(0, at, at + 3, begin, end));
        UNSCOPED_INFO("into the macro's product after it: not the source's");
        CHECK_FALSE(map.verbatimSpan(0, at + 1, at + 5, begin, end));
        UNSCOPED_INFO("past the line's end");
        CHECK_FALSE(map.verbatimSpan(0, out + 2, out + 3, begin, end));
    }
}
