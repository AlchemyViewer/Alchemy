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

#include "altesttempdir.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

//
// Blocks under test. Three levels deep, so a name with two dots in it has
// somewhere to land: no shipped XUI file has one, which is why the splitter
// could only be checked by reading it.
//

namespace
{
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
        Optional<U8>            small_number;
        Optional<S32>           count;
        Optional<F32>           ratio;
        Optional<LLColor4>      tint;
        Optional<LLUUID>        id;

        XuiRoot()
        :   middle("middle"),
            flag("flag", false),
            text("text", ""),
            small_number("small", 0),
            count("count", 0),
            ratio("ratio", 0.f),
            tint("tint", LLColor4::black),
            id("id")
        {}
    };

    // LLSimpleXUIParser reads a file rather than a buffer, so every case here
    // needs one on disk: in a directory of its own, since a fixed name is
    // shared by two runs at once. Named per case within it, so a crashed run
    // leaves something identifiable behind.
    class ScratchXui
    {
    public:
        ScratchXui(const char* tag, const std::string& contents)
        :   mDir("al_xuiparser"),
            mPath(mDir.path() / (std::string("al_xuiparser_") + tag + ".xml"))
        {
            std::ofstream out(mPath, std::ios::binary);
            out << contents;
        }

        std::string name() const { return mPath.string(); }

    private:
        ll_test::TempDir      mDir;
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

namespace
{
    TEST_CASE("llxuiparser: attributes of every scalar kind reach the block", "[llxuiparser]")
    {
        XuiRoot block;
        CHECK(parse("scalars",
                     "<root flag=\"true\""
                     " text=\"hello\""
                     " small=\"200\""
                     " count=\"-17\""
                     " ratio=\"0.5\""
                     " tint=\"1 0.5 0 1\""
                     " id=\"a2e76fcd-9360-4f6d-a924-000000000001\"/>",
                     block));

        CHECK(block.flag.isProvided());
        CHECK(block.flag());
        CHECK(block.text() == std::string("hello"));
        CHECK((S32)block.small_number() == 200);
        CHECK(block.count() == -17);
        CHECK(block.ratio() == 0.5f);
        CHECK(block.tint().mV[VRED] == 1.f);
        CHECK(block.tint().mV[VGREEN] == 0.5f);
        CHECK(block.tint().mV[VBLUE] == 0.f);
        CHECK(block.tint().mV[VALPHA] == 1.f);
        CHECK(block.id().asString() ==
                      std::string("a2e76fcd-9360-4f6d-a924-000000000001"));
    }

    TEST_CASE("llxuiparser: a value with trailing junk is not a value", "[llxuiparser]")
    {
        // boost::spirit's parse(...).full said the whole attribute had to be
        // consumed. from_chars says the same by its end pointer reaching the
        // end of the text, and this is what tells the two apart from a reader
        // that stops at the first character it cannot use.
        XuiRoot block;
        parse("junk", "<root count=\"12abc\" ratio=\"0.5px\"/>", block, true);

        CHECK_FALSE(block.count.isProvided());
        CHECK_FALSE(block.ratio.isProvided());
    }

    TEST_CASE("llxuiparser: a narrow type refuses a value it cannot hold", "[llxuiparser]")
    {
        // Assigning into a U8 through spirit's assign_a never range-checked,
        // so 300 landed as 44.
        XuiRoot block;
        parse("narrow", "<root small=\"300\"/>", block, true);

        CHECK_FALSE(block.small_number.isProvided());
        CHECK((S32)block.small_number() == 0);
    }

    TEST_CASE("llxuiparser: a leading plus is still a number", "[llxuiparser]")
    {
        // from_chars rejects it and spirit's int_p and real_p did not, so the
        // sign is stepped over by hand before the conversion.
        XuiRoot block;
        parse("plus", "<root count=\"+5\" ratio=\"+0.25\"/>", block);

        CHECK(block.count.isProvided());
        CHECK(block.count() == 5);
        CHECK(block.ratio.isProvided());
        CHECK(block.ratio() == 0.25f);
    }

    TEST_CASE("llxuiparser: a dotted attribute name descends into nested blocks", "[llxuiparser]")
    {
        XuiRoot block;
        CHECK(parse("dotted",
                     "<root middle.depth=\"2\" middle.leaf.depth=\"3\""
                     " middle.leaf.label=\"deep\"/>",
                     block));

        CHECK(block.middle.depth.isProvided());
        CHECK(block.middle.depth() == 2);
        CHECK(block.middle.leaf.depth.isProvided());
        CHECK(block.middle.leaf.depth() == 3);
        CHECK(block.middle.leaf.label() == std::string("deep"));
    }

    TEST_CASE("llxuiparser: a compound child element descends the same way", "[llxuiparser]")
    {
        // The other spelling of the same thing: an element named for the
        // parent's scope plus the path, carrying the rest as attributes.
        XuiRoot block;
        CHECK(parse("compound",
                     "<root><root.middle leaf.depth=\"7\" depth=\"6\"/></root>",
                     block));

        CHECK(block.middle.depth() == 6);
        CHECK(block.middle.leaf.depth() == 7);
    }

    TEST_CASE("llxuiparser: a name that does not match the enclosing scope is skipped", "[llxuiparser]")
    {
        // startElement checks the leading token against the scope it is in, so
        // a compound element addressed to some other block is ignored rather
        // than folded into this one.
        XuiRoot block;
        parse("mismatch",
              "<root><other.middle depth=\"4\"/></root>",
              block, true);

        CHECK_FALSE(block.middle.depth.isProvided());
    }

    TEST_CASE("llxuiparser: a missing file is reported and not assumed empty", "[llxuiparser]")
    {
        // In a directory of the test's own, not the working one, where a
        // file of that name could be.
        const ll_test::TempDir dir("al_xuiparser");
        XuiRoot block;
        LLSimpleXUIParser parser;
        CHECK_FALSE(parser.readXUI((dir.path() / "al_xuiparser_no_such_file.xml").string(), block, true));
    }
}
