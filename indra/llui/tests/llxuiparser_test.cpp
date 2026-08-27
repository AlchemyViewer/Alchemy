/**
 * @file llxuiparser_test.cpp
 * @brief Tests for LLSimpleXUIParser, the fast path that reads XUI straight
 *        into a parameter block.
 *
 * LLXUIParser goes through LLXMLNode and is exercised by every widget the
 * fingerprint harness builds. LLSimpleXUIParser is a separate reader, used
 * for notifications and keybindings, and nothing covered it: it reads every
 * scalar through its own from_chars helpers and splits its own dotted names.
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

#include "llxuiparser.h"

#include "llinitparam.h"
#include "lluuid.h"
#include "v4color.h"

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>

//
// Blocks under test. Three levels deep, so a name with two dots in it has
// somewhere to land: no shipped XUI file has one, which is why the splitter
// could only be checked by reading it.
//

struct XuiLeaf : public LLInitParam::Block<XuiLeaf>
{
    Optional<S32>           depth;
    Optional<std::string>   label;

    XuiLeaf()
    :   depth("depth", 0),
        label("label", "")
    {}
};

struct XuiMiddle : public LLInitParam::Block<XuiMiddle>
{
    Optional<XuiLeaf>   leaf;
    Optional<S32>       depth;

    XuiMiddle()
    :   leaf("leaf"),
        depth("depth", 0)
    {}
};

struct XuiRoot : public LLInitParam::Block<XuiRoot>
{
    Optional<XuiMiddle>     middle;
    Optional<bool>          flag;
    Optional<std::string>   text;
    Optional<U8>            small;
    Optional<S32>           count;
    Optional<F32>           ratio;
    Optional<LLColor4>      tint;
    Optional<LLUUID>        id;

    XuiRoot()
    :   middle("middle"),
        flag("flag", false),
        text("text", ""),
        small("small", 0),
        count("count", 0),
        ratio("ratio", 0.f),
        tint("tint", LLColor4::black),
        id("id")
    {}
};

namespace
{
    // LLSimpleXUIParser reads a file rather than a buffer, so every case here
    // needs one on disk. Named per case so a crashed run leaves something
    // identifiable behind rather than clobbering the next test's input.
    class ScratchXui
    {
    public:
        ScratchXui(const char* tag, const std::string& contents)
        :   mPath(std::filesystem::temp_directory_path() / (std::string("al_xuiparser_") + tag + ".xml"))
        {
            std::ofstream out(mPath, std::ios::binary);
            out << contents;
        }

        ~ScratchXui()
        {
            std::error_code ignored;
            std::filesystem::remove(mPath, ignored);
        }

        std::string name() const { return mPath.string(); }

    private:
        std::filesystem::path mPath;
    };

    // silent=true for the cases that are meant to fail, so a run that passes
    // is not buried in warnings about the values it was supposed to reject.
    bool parse(const char* tag, const std::string& xml, LLInitParam::BaseBlock& block, bool silent = false)
    {
        ScratchXui file(tag, xml);
        LLSimpleXUIParser parser;
        return parser.readXUI(file.name(), block, silent);
    }
}

namespace tut
{
    struct llxuiparser_data
    {
    };
    typedef test_group<llxuiparser_data> llxuiparser_group;
    typedef llxuiparser_group::object object;
    llxuiparser_group llxuiparsergrp("llxuiparser");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("attributes of every scalar kind reach the block");

        XuiRoot block;
        ensure("the document parsed",
               parse("scalars",
                     "<root flag=\"true\""
                     " text=\"hello\""
                     " small=\"200\""
                     " count=\"-17\""
                     " ratio=\"0.5\""
                     " tint=\"1 0.5 0 1\""
                     " id=\"a2e76fcd-9360-4f6d-a924-000000000001\"/>",
                     block));

        ensure("flag provided", block.flag.isProvided());
        ensure_equals("flag", block.flag(), true);
        ensure_equals("text", block.text(), std::string("hello"));
        ensure_equals("small", (S32)block.small(), 200);
        ensure_equals("count", block.count(), -17);
        ensure_equals("ratio", block.ratio(), 0.5f);
        ensure_equals("tint red", block.tint().mV[VRED], 1.f);
        ensure_equals("tint green", block.tint().mV[VGREEN], 0.5f);
        ensure_equals("tint blue", block.tint().mV[VBLUE], 0.f);
        ensure_equals("tint alpha", block.tint().mV[VALPHA], 1.f);
        ensure_equals("id", block.id().asString(),
                      std::string("a2e76fcd-9360-4f6d-a924-000000000001"));
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("a value with trailing junk is not a value");

        // boost::spirit's parse(...).full said the whole attribute had to be
        // consumed. from_chars says the same by its end pointer reaching the
        // end of the text, and this is what tells the two apart from a reader
        // that stops at the first character it cannot use.
        XuiRoot block;
        parse("junk", "<root count=\"12abc\" ratio=\"0.5px\"/>", block, true);

        ensure("a number with a tail is refused", !block.count.isProvided());
        ensure("a real with a tail is refused", !block.ratio.isProvided());
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("a narrow type refuses a value it cannot hold");

        // Assigning into a U8 through spirit's assign_a never range-checked,
        // so 300 landed as 44.
        XuiRoot block;
        parse("narrow", "<root small=\"300\"/>", block, true);

        ensure("out of range is refused", !block.small.isProvided());
        ensure_equals("and the default stands", (S32)block.small(), 0);
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("a leading plus is still a number");

        // from_chars rejects it and spirit's int_p and real_p did not, so the
        // sign is stepped over by hand before the conversion.
        XuiRoot block;
        parse("plus", "<root count=\"+5\" ratio=\"+0.25\"/>", block);

        ensure("signed integer accepted", block.count.isProvided());
        ensure_equals("count", block.count(), 5);
        ensure("signed real accepted", block.ratio.isProvided());
        ensure_equals("ratio", block.ratio(), 0.25f);
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("a dotted attribute name descends into nested blocks");

        XuiRoot block;
        ensure("the document parsed",
               parse("dotted",
                     "<root middle.depth=\"2\" middle.leaf.depth=\"3\""
                     " middle.leaf.label=\"deep\"/>",
                     block));

        ensure("one dot lands", block.middle.depth.isProvided());
        ensure_equals("one dot", block.middle.depth(), 2);
        ensure("two dots land", block.middle.leaf.depth.isProvided());
        ensure_equals("two dots", block.middle.leaf.depth(), 3);
        ensure_equals("and again", block.middle.leaf.label(), std::string("deep"));
    }

    template<> template<>
    void object::test<6>()
    {
        set_test_name("a compound child element descends the same way");

        // The other spelling of the same thing: an element named for the
        // parent's scope plus the path, carrying the rest as attributes.
        XuiRoot block;
        ensure("the document parsed",
               parse("compound",
                     "<root><root.middle leaf.depth=\"7\" depth=\"6\"/></root>",
                     block));

        ensure_equals("attribute on the compound element", block.middle.depth(), 6);
        ensure_equals("dotted attribute under it", block.middle.leaf.depth(), 7);
    }

    template<> template<>
    void object::test<7>()
    {
        set_test_name("a name that does not match the enclosing scope is skipped");

        // startElement checks the leading token against the scope it is in, so
        // a compound element addressed to some other block is ignored rather
        // than folded into this one.
        XuiRoot block;
        parse("mismatch",
              "<root><other.middle depth=\"4\"/></root>",
              block, true);

        ensure("nothing was taken from it", !block.middle.depth.isProvided());
    }

    template<> template<>
    void object::test<8>()
    {
        set_test_name("a missing file is reported, not assumed empty");

        XuiRoot block;
        LLSimpleXUIParser parser;
        ensure("reading a file that is not there fails",
               !parser.readXUI("al_xuiparser_no_such_file.xml", block, true));
    }
}
