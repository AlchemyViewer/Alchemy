/**
 * @file llinitparam_test.cpp
 * @brief Characterisation tests for LLInitParam parameter blocks.
 *
 * These lock in the observable semantics that the parameter-block machinery
 * has today -- declaration, provided-ness, validation, merging, and LLSD
 * round-trips -- so that the descriptor tables, name stacks and block layout
 * underneath can be reworked without changing what callers see.
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

#include "llinitparam.h"

#include "llsd.h"
#include "llsdparam.h"

#include <catch2/catch_test_macros.hpp>

//
// Types under test. These live at namespace scope because a block's parameter
// table is a function-local static keyed by the block type, and because
// ParamValue<> is specialised for TestPoint below.
//

namespace
{

// A named-value lookup, the mechanism behind "left"/"center" in XUI.
struct TestNames : public LLInitParam::TypeValuesHelper<S32, TestNames>
{
    static void declareValues()
    {
        declare("one", 1);
        declare("two", 2);
    }
};

// The simplest useful block: one of each declaration kind.
struct SimpleBlock : public LLInitParam::Block<SimpleBlock>
{
    Mandatory<std::string>  name;
    Optional<S32>           count;
    Optional<bool>          flag;
    Optional<S32, TestNames> named;

    SimpleBlock()
    :   name("name"),
        count("count", 7),
        flag("flag", false),
        named("named", 0)
    {}
};

// Derives from SimpleBlock, so its table aggregates the base's parameters.
struct DerivedBlock : public LLInitParam::Block<DerivedBlock, SimpleBlock>
{
    Optional<F32>   scale;

    DerivedBlock()
    :   scale("scale", 1.f)
    {}
};

struct InnerBlock : public LLInitParam::Block<InnerBlock>
{
    Optional<S32>   a;
    Optional<S32>   b;

    InnerBlock()
    :   a("a", 0),
        b("b", 0)
    {}
};

struct OuterBlock : public LLInitParam::Block<OuterBlock>
{
    Optional<InnerBlock>            inner;
    Optional<Lazy<InnerBlock, IS_A_BLOCK> > lazy_inner;
    Multiple<S32, AtLeast<2> >      list;

    OuterBlock()
    :   inner("inner"),
        lazy_inner("lazy_inner"),
        list("list")
    {}
};

// A block whose parameter carries a second name.
struct SynonymBlock : public LLInitParam::Block<SynonymBlock>
{
    Optional<S32>   width;

    SynonymBlock()
    :   width("width", 0)
    {
        addSynonym(width, "w");
    }
};

// Exactly one alternative is chosen at a time.
struct ChoiceOfThings : public LLInitParam::ChoiceBlock<ChoiceOfThings>
{
    Alternative<S32>            number;
    Alternative<std::string>    text;

    ChoiceOfThings()
    :   number("number", 0),
        text("text", "")
    {}
};

// The same, built only by the case on choosing one alternative. A choice block
// starts with its first alternative chosen only when it is the first of its
// type, the one whose construction builds the type's descriptor: with a type of
// its own, that case sees that whatever ran before it.
struct FirstChoiceOfThings : public LLInitParam::ChoiceBlock<FirstChoiceOfThings>
{
    Alternative<S32>            number;
    Alternative<std::string>    text;

    FirstChoiceOfThings()
    :   number("number", 0),
        text("text", "")
    {}
};

struct IgnoredBlock : public LLInitParam::Block<IgnoredBlock>
{
    Optional<S32>   kept;
    Ignored         gone;

    IgnoredBlock()
    :   kept("kept", 0),
        gone("gone")
    {}
};

// A value that can be written either whole or as its components -- the shape
// LLRect and LLUIColor use.
struct TestPoint
{
    TestPoint(S32 x = 0, S32 y = 0) : mX(x), mY(y) {}
    bool operator==(const TestPoint& other) const
    {
        return mX == other.mX && mY == other.mY;
    }

    S32 mX;
    S32 mY;
};

} // anonymous namespace

namespace LLInitParam
{
    template<>
    class ParamValue<TestPoint>
    :   public CustomParamValue<TestPoint>
    {
        typedef CustomParamValue<TestPoint> super_t;

    public:
        Optional<S32>   x;
        Optional<S32>   y;

        ParamValue(const TestPoint& value)
        :   super_t(value),
            x("x"),
            y("y")
        {
            // Splatting the value across the components in the constructor is
            // what every one of these does in llui, so this block pays what
            // they pay.
            updateBlockFromValue(false);
        }

        void updateValueFromBlock()
        {
            updateValue(TestPoint(x, y));
        }

        void updateBlockFromValue(bool make_block_authoritative)
        {
            const TestPoint& value = getValue();
            setComponent(x, value.mX, make_block_authoritative);
            setComponent(y, value.mY, make_block_authoritative);
        }
    };
}

namespace
{

struct PointBlock : public LLInitParam::Block<PointBlock>
{
    Optional<TestPoint> point;

    PointBlock()
    :   point("point")
    {}
};

// LLScrollListColumn's header is this shape: a choice between a plain value
// and one that carries components of its own.
struct PointChoice : public LLInitParam::ChoiceBlock<PointChoice>
{
    Alternative<S32>        scalar;
    Alternative<TestPoint>  point;

    PointChoice()
    :   scalar("scalar"),
        point("point")
    {}
};

} // anonymous namespace

namespace
{
    TEST_CASE("llinitparam: fresh block reports defaults and nothing provided", "[llinitparam]")
    {
        SimpleBlock block;

        CHECK_FALSE(block.name.isProvided());
        CHECK_FALSE(block.count.isProvided());
        CHECK(block.count() == 7);
        CHECK_FALSE(block.flag());
        CHECK_FALSE(block.isProvided());
    }

    TEST_CASE("llinitparam: assignment marks a parameter provided", "[llinitparam]")
    {
        SimpleBlock block;
        block.count = 42;

        CHECK(block.count.isProvided());
        CHECK(block.count() == 42);
        CHECK_FALSE(block.flag.isProvided());
        CHECK(block.isProvided());
    }

    TEST_CASE("llinitparam: Mandatory gates validation and Optional does not", "[llinitparam]")
    {
        SimpleBlock block;
        UNSCOPED_INFO("missing mandatory fails validation");
        CHECK_FALSE(block.validateBlock(false));

        block.name = std::string("widget");
        CHECK(block.validateBlock(false));
    }

    TEST_CASE("llinitparam: derived block sees its own and its base's parameters", "[llinitparam]")
    {
        DerivedBlock block;
        block.count = 3;    // declared on SimpleBlock
        block.scale = 2.f;  // declared on DerivedBlock

        CHECK(block.count() == 3);
        CHECK(block.scale() == 2.f);

        // The base's Mandatory is aggregated into the derived table, so it
        // still gates the derived block's validation.
        UNSCOPED_INFO("inherited mandatory still required");
        CHECK_FALSE(block.validateBlock(false));
        block.name = std::string("derived");
        CHECK(block.validateBlock(false));
    }

    TEST_CASE("llinitparam: named values resolve by name and by number", "[llinitparam]")
    {
        S32 value = 0;
        CHECK(TestNames::getValueFromName("two", value));
        CHECK(value == 2);
        CHECK_FALSE(TestNames::getValueFromName("nine", value));

        SimpleBlock block;
        block.named = std::string("one");
        CHECK(block.named() == 1);
        CHECK(block.named.getValueName() == std::string("one"));

        block.named = 2;
        CHECK(block.named() == 2);
        CHECK(block.named.getValueName().empty());
    }

    TEST_CASE("llinitparam: Multiple counts elements and enforces its range", "[llinitparam]")
    {
        OuterBlock block;
        UNSCOPED_INFO("empty list is below AtLeast<2>");
        CHECK_FALSE(block.list.isValid());
        CHECK(block.list.size() == size_t(0));

        block.list.add(10);
        UNSCOPED_INFO("one element is still below AtLeast<2>");
        CHECK_FALSE(block.list.isValid());

        block.list.add(20);
        UNSCOPED_INFO("two elements satisfy AtLeast<2>");
        REQUIRE(block.list.isValid());
        CHECK(block.list()[0]() == 10);
        CHECK(block.list()[1]() == 20);
    }

    TEST_CASE("llinitparam: a parameter that is a block is provided by its children", "[llinitparam]")
    {
        OuterBlock block;
        CHECK_FALSE(block.inner.isProvided());

        block.inner.a = 5;
        CHECK(block.inner.isProvided());
        CHECK(block.isProvided());
        CHECK(block.inner.a() == 5);
        UNSCOPED_INFO("sibling child keeps its default");
        CHECK(block.inner.b() == 0);
    }

    TEST_CASE("llinitparam: fillFrom yields to what is already provided and overwriteFrom does not", "[llinitparam]")
    {
        SimpleBlock source;
        source.count = 100;
        source.flag = true;

        SimpleBlock destination;
        destination.count = 1;

        destination.fillFrom(source);
        CHECK(destination.count() == 1);
        UNSCOPED_INFO("fillFrom supplies an absent value");
        CHECK(destination.flag());

        destination.overwriteFrom(source);
        CHECK(destination.count() == 100);
    }

    TEST_CASE("llinitparam: choosing one alternative unprovides the other", "[llinitparam]")
    {
        FirstChoiceOfThings choice;
        UNSCOPED_INFO("first declared alternative is chosen initially");
        CHECK(choice.number.isChosen());

        choice.text = std::string("hello");
        CHECK(choice.text.isChosen());
        CHECK_FALSE(choice.number.isChosen());
        CHECK_FALSE(choice.number.isProvided());
        CHECK(choice.text() == std::string("hello"));

        choice.number = 3;
        CHECK(choice.number.isChosen());
        CHECK_FALSE(choice.text.isProvided());
    }

    TEST_CASE("llinitparam: scalars survive an LLSD round-trip", "[llinitparam]")
    {
        SimpleBlock written;
        written.name = std::string("round");
        written.count = 55;
        written.flag = true;

        LLSD sd;
        LLParamSDParser parser;
        parser.writeSD(sd, written);

        REQUIRE(sd.has("count"));
        CHECK(sd["count"].asInteger() == 55);

        SimpleBlock read;
        LLParamSDParser reader;
        reader.readSD(sd, read);

        CHECK(read.name() == std::string("round"));
        CHECK(read.count() == 55);
        CHECK(read.flag());
        CHECK(read.count.isProvided());
    }

    TEST_CASE("llinitparam: nested blocks and lists survive an LLSD round-trip", "[llinitparam]")
    {
        OuterBlock written;
        written.inner.a = 11;
        written.inner.b = 22;
        written.list.add(1);
        written.list.add(2);
        written.list.add(3);

        LLSD sd;
        LLParamSDParser parser;
        parser.writeSD(sd, written);

        REQUIRE(sd.has("inner"));
        CHECK(sd["inner"]["a"].asInteger() == 11);

        OuterBlock read;
        LLParamSDParser reader;
        reader.readSD(sd, read);

        CHECK(read.inner.a() == 11);
        CHECK(read.inner.b() == 22);
        REQUIRE(read.list.size() == size_t(3));
        UNSCOPED_INFO("list order survives");
        CHECK(read.list()[2]() == 3);
    }

    TEST_CASE("llinitparam: a named value round-trips as its name and not its number", "[llinitparam]")
    {
        SimpleBlock written;
        written.named = std::string("two");

        LLSD sd;
        LLParamSDParser parser;
        parser.writeSD(sd, written);

        CHECK(sd["named"].asString() == std::string("two"));

        SimpleBlock read;
        LLParamSDParser reader;
        reader.readSD(sd, read);

        CHECK(read.named() == 2);
        CHECK(read.named.getValueName() == std::string("two"));
    }

    TEST_CASE("llinitparam: a synonym reaches the same parameter", "[llinitparam]")
    {
        LLSD sd;
        sd["w"] = 64;

        SynonymBlock block;
        LLParamSDParser parser;
        parser.readSD(sd, block);

        CHECK(block.width.isProvided());
        CHECK(block.width() == 64);
    }

    TEST_CASE("llinitparam: an Ignored parameter is accepted and discarded", "[llinitparam]")
    {
        LLSD sd;
        sd["kept"] = 5;
        sd["gone"] = 6;

        IgnoredBlock block;
        LLParamSDParser parser;
        parser.readSD(sd, block);

        CHECK(block.kept() == 5);
        UNSCOPED_INFO("the block is valid despite the ignored key");
        CHECK(block.validateBlock(false));
    }

    TEST_CASE("llinitparam: a custom value can be set whole or by component", "[llinitparam]")
    {
        PointBlock whole;
        whole.point.set(TestPoint(3, 4));
        CHECK(whole.point().mX == 3);
        CHECK(whole.point().mY == 4);
        UNSCOPED_INFO("whole assignment splats out to the components");
        CHECK(whole.point.x() == 3);

        PointBlock components;
        components.point.x = 8;
        components.point.y = 9;
        CHECK(components.point().mX == 8);
        CHECK(components.point().mY == 9);
    }

    TEST_CASE("llinitparam: a custom value round-trips through its components", "[llinitparam]")
    {
        PointBlock written;
        written.point.x = 12;
        written.point.y = 34;

        LLSD sd;
        LLParamSDParser parser;
        parser.writeSD(sd, written);

        PointBlock read;
        LLParamSDParser reader;
        reader.readSD(sd, read);

        CHECK(read.point().mX == 12);
        CHECK(read.point().mY == 34);
    }

    TEST_CASE("llinitparam: a Lazy sub-block stays absent until something fills it", "[llinitparam]")
    {
        // Lazy<> keeps its block on the heap rather than deriving from it, so
        // an untouched one costs a pointer and serialises to nothing at all.
        OuterBlock untouched;
        CHECK_FALSE(untouched.lazy_inner.isProvided());

        LLSD empty_sd;
        LLParamSDParser empty_parser;
        empty_parser.writeSD(empty_sd, untouched);
        CHECK_FALSE(empty_sd.has("lazy_inner"));

        LLSD sd;
        sd["lazy_inner"]["a"] = 77;

        OuterBlock filled;
        LLParamSDParser parser;
        parser.readSD(sd, filled);

        CHECK(filled.lazy_inner.isProvided());
        CHECK(filled.lazy_inner().a() == 77);
    }

    TEST_CASE("llinitparam: copying a block carries values and provided-ness", "[llinitparam]")
    {
        SimpleBlock original;
        original.name = std::string("copy me");
        original.count = 99;

        SimpleBlock copy(original);

        CHECK(copy.count() == 99);
        CHECK(copy.name() == std::string("copy me"));
        CHECK(copy.count.isProvided());
        CHECK_FALSE(copy.flag.isProvided());

        // The copy must own its storage: changing it must not disturb the original.
        copy.count = 1;
        CHECK(original.count() == 99);
    }

    TEST_CASE("llinitparam: the descriptor registry sees every block that has been built", "[llinitparam]")
    {
        // Constructing a block is what registers its parameters, so touch one
        // of each kind declared above before counting.
        SimpleBlock simple;
        DerivedBlock derived;
        OuterBlock outer;
        ChoiceOfThings choice;
        PointBlock point;

        const std::vector<LLInitParam::BlockDescriptor*>& all =
            LLInitParam::BlockDescriptor::getAllDescriptors();

        UNSCOPED_INFO("every constructed block type registered");
        CHECK(all.size() >= 5);

        size_t named_entries = 0;
        for (const LLInitParam::BlockDescriptor* descriptor : all)
        {
            named_entries += descriptor->namedParams().size();
        }
        UNSCOPED_INFO("the tables hold the parameters we declared");
        CHECK(named_entries >= 12);

        // DerivedBlock aggregates SimpleBlock's four parameters on top of its
        // own, which is the duplication the census exists to measure.
        CHECK(DerivedBlock::getBlockDescriptor().namedParams().size() ==
                    SimpleBlock::getBlockDescriptor().namedParams().size() + 1);

        const std::string report = LLInitParam::BlockDescriptor::getStatsReport();
        CHECK_FALSE(report.empty());
        CHECK(report.find("block types registered") != std::string::npos);
    }

    TEST_CASE("llinitparam: merging reports whether it actually took anything", "[llinitparam]")
    {
        // The return value is what tells a caller its block changed. A
        // Multiple<> used to answer yes unconditionally, so any block holding
        // one looked modified by a merge that moved nothing.
        OuterBlock empty_source;
        OuterBlock destination;
        CHECK_FALSE(destination.fillFrom(empty_source));
        CHECK(destination.list.size() == size_t(0));

        OuterBlock source;
        source.list.add(1);
        source.list.add(2);
        CHECK(destination.fillFrom(source));
        CHECK(destination.list.size() == size_t(2));

        // Already-filled destination, still-empty source.
        CHECK_FALSE(destination.fillFrom(empty_source));
        CHECK(destination.list.size() == size_t(2));
    }

    TEST_CASE("llinitparam: a value filling in its own components is not a choice", "[llinitparam]")
    {
        // TestPoint fills x and y from its value as it is built. That must
        // read as the block keeping itself consistent, not as someone
        // supplying a point.
        //
        // The first block of a type is the one that builds the descriptor, and
        // while it does the first alternative declares itself the standing
        // choice. Blocks built afterwards start with no choice at all, which
        // is the case at issue here.
        PointChoice initializes_the_descriptor;
        (void)initializes_the_descriptor;

        PointChoice choice;

        CHECK_FALSE(choice.point.isChosen());
        CHECK_FALSE(choice.scalar.isChosen());
    }

    TEST_CASE("llinitparam: providing an alternative still chooses it", "[llinitparam]")
    {
        PointChoice choice;
        choice.scalar = 5;
        CHECK(choice.scalar.isChosen());
        CHECK(choice.scalar() == 5);

        // Reaching into the components of the other alternative counts as
        // providing it, the same way assigning the whole value would.
        choice.point.x = 3;
        CHECK(choice.point.isChosen());
        CHECK_FALSE(choice.scalar.isChosen());
        CHECK(choice.point().mX == 3);
    }

    TEST_CASE("llinitparam: merging does not hand the choice to a value with components", "[llinitparam]")
    {
        // A choice merges as a unit: the destination takes the source's
        // choice, and an alternative the source never provided is passed over
        // rather than visited -- which matters here because visiting the point
        // would refresh its components, and by merge time the block is built
        // enough for that to reach the choice.
        PointChoice source;
        source.scalar = 5;

        PointChoice destination;
        destination.fillFrom(source);

        CHECK(destination.scalar.isChosen());
        CHECK_FALSE(destination.point.isChosen());
        CHECK(destination.scalar() == 5);
    }
}
