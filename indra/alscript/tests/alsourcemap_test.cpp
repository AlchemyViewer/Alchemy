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

#include "../test/lltut.h"

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

namespace tut
{
    struct alsourcemap_data
    {
    };

    typedef test_group<alsourcemap_data> alsourcemap_group;
    typedef alsourcemap_group::object    alsourcemap_object;
    alsourcemap_group                    alsourcemap_instance("alsourcemap");

    template<> template<>
    void alsourcemap_object::test<1>()
    {
        set_test_name("verbatim tokens that carry on from one another are one segment; a macro's product, another line or another file start their own");
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
        ensure("the first three one", map.toSource(0, 8).column == 8 && map.toSource(0, 0).line == 0);
        ALSourceMap::Loc begin, end;
        ensure("one stretch copied as it stands", map.verbatimSpan(0, 0, 9, begin, end) && begin.column == 0 && end.column == 9);
        ensure("the macro's to its invocation", map.toSource(0, 11).line == 3 && map.toSource(0, 11).column == 2);
        ensure("the next its own", map.toSource(0, 14).column == 12);
        ensure("the other file's", map.toSource(1, 2).file == 1 && map.toSource(1, 2).column == 2);
        ensure("and back", map.toExpanded(0, 0, 8).line == 0 && map.toExpanded(0, 0, 8).column == 8);
    }

    template<> template<>
    void alsourcemap_object::test<2>()
    {
        set_test_name("a long line of a macro's products looked up: the segment at or before the column, the first before any, nothing on a line with none");
        ALSourceMap map;
        map.addFile("script", "script");
        for (S32 i = 0; i < 3000; ++i)
        {
            map.add(segment(0, 4 + i * 3, 3, 0, 1, i, false));
        }
        map.add(segment(2, 0, 3, 0, 5, 0));
        map.finish();
        ensure_equals("the first", map.toSource(0, 4).column, 0);
        ensure_equals("in the middle", map.toSource(0, 4 + 1500 * 3 + 1).column, 1500);
        ensure_equals("before any: the first", map.toSource(0, 1).column, 0);
        ensure_equals("past the last: the last", map.toSource(0, 20000).column, 2999);
        ensure("a line with none: nowhere", !map.toSource(1, 0).found() && map.toSource(2, 1).found());
    }

    template<> template<>
    void alsourcemap_object::test<3>()
    {
        set_test_name("a map read through another: one copied stretch over several of the other's segments read through each, exact where they are");
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
        ensure("the script's own, exact", through.toSource(0, 3).file == 0 && through.toSource(0, 3).line == 4 && through.toSource(0, 3).column == 1);
        ensure("the macro's, to its invocation", through.toSource(0, 7).line == 9 && through.toSource(0, 7).column == 1);
        ensure("the include's, exact", through.toSource(0, 11).file == 1 && through.toSource(0, 11).column == 1);
        ensure("a line the inner has nothing for: dropped", !through.toSource(1, 0).found());
        ALSourceMap::Loc begin, end;
        ensure("the copied stretches stay verbatim", through.verbatimSpan(0, 2, 5, begin, end) && !through.verbatimSpan(0, 5, 10, begin, end));
    }

    template<> template<>
    void alsourcemap_object::test<4>()
    {
        set_test_name("a text mapped to itself: every line its own, the last without a newline too");
        const ALSourceMap map = ALSourceMap::identity("one\ntwo three\nlast", "same");
        ensure("named", map.files().size() == 1 && map.files()[0].name == "same");
        ensure("each line", map.toSource(1, 5).line == 1 && map.toSource(1, 5).column == 5 && map.toSource(2, 2).line == 2);
    }
    template<> template<>
    void alsourcemap_object::test<5>()
    {
        set_test_name("a verbatim stretch found on a long line: the first segment that reaches the column, as a walk from the line's start finds it");
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
        ensure("within a token copied, deep in the line", map.verbatimSpan(0, at + 1, at + 3, begin, end) && begin.line == 1 && begin.column == 2501 && end.column == 2503);
        ensure("an insertion just after it goes with it", map.verbatimSpan(0, at + 3, at + 3, begin, end) && begin.column == 2503);
        ensure("an insertion just before it goes with the macro's product before it", !map.verbatimSpan(0, at, at, begin, end));
        ensure("and so does a stretch from its first column", !map.verbatimSpan(0, at, at + 3, begin, end));
        ensure("into the macro's product after it: not the source's", !map.verbatimSpan(0, at + 1, at + 5, begin, end));
        ensure("past the line's end", !map.verbatimSpan(0, out + 2, out + 3, begin, end));
    }
}
