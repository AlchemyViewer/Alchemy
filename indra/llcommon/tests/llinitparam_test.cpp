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

#include "../test/lltut.h"

//
// Types under test. These live at namespace scope because a block's parameter
// table is a function-local static keyed by the block type, and because
// ParamValue<> is specialised for TestPoint below.
//

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
            x("x", value.mX),
            y("y", value.mY)
        {}

        void updateValueFromBlock()
        {
            updateValue(TestPoint(x, y));
        }

        void updateBlockFromValue(bool make_block_authoritative)
        {
            const TestPoint& value = getValue();
            x.set(value.mX, make_block_authoritative);
            y.set(value.mY, make_block_authoritative);
        }
    };
}

struct PointBlock : public LLInitParam::Block<PointBlock>
{
    Optional<TestPoint> point;

    PointBlock()
    :   point("point")
    {}
};

namespace tut
{
    struct llinitparam_data
    {
    };
    typedef test_group<llinitparam_data> llinitparam_group;
    typedef llinitparam_group::object object;
    llinitparam_group llinitparamgrp("llinitparam");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("fresh block reports defaults and nothing provided");

        SimpleBlock block;

        ensure("nothing provided yet", !block.name.isProvided());
        ensure("nothing provided yet", !block.count.isProvided());
        ensure_equals("declared default survives", block.count(), 7);
        ensure_equals("declared default survives", block.flag(), false);
        ensure("block itself is not provided", !block.isProvided());
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("assignment marks a parameter provided");

        SimpleBlock block;
        block.count = 42;

        ensure("assignment provides", block.count.isProvided());
        ensure_equals("assignment stores", block.count(), 42);
        ensure("sibling untouched", !block.flag.isProvided());
        ensure("block became provided", block.isProvided());
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("Mandatory gates validation, Optional does not");

        SimpleBlock block;
        ensure("missing mandatory fails validation", !block.validateBlock(false));

        block.name = std::string("widget");
        ensure("supplied mandatory passes validation", block.validateBlock(false));
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("derived block sees its own and its base's parameters");

        DerivedBlock block;
        block.count = 3;    // declared on SimpleBlock
        block.scale = 2.f;  // declared on DerivedBlock

        ensure_equals("base parameter readable", block.count(), 3);
        ensure_equals("derived parameter readable", block.scale(), 2.f);

        // The base's Mandatory is aggregated into the derived table, so it
        // still gates the derived block's validation.
        ensure("inherited mandatory still required", !block.validateBlock(false));
        block.name = std::string("derived");
        ensure("inherited mandatory satisfied", block.validateBlock(false));
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("named values resolve by name and by number");

        S32 value = 0;
        ensure("known name resolves", TestNames::getValueFromName("two", value));
        ensure_equals("known name resolves to its value", value, 2);
        ensure("unknown name does not resolve", !TestNames::getValueFromName("nine", value));

        SimpleBlock block;
        block.named = std::string("one");
        ensure_equals("assignment by name sets the value", block.named(), 1);
        ensure_equals("assignment by name records the name", block.named.getValueName(), std::string("one"));

        block.named = 2;
        ensure_equals("assignment by value sets the value", block.named(), 2);
        ensure("assignment by value clears the name", block.named.getValueName().empty());
    }

    template<> template<>
    void object::test<6>()
    {
        set_test_name("Multiple counts elements and enforces its range");

        OuterBlock block;
        ensure("empty list is below AtLeast<2>", !block.list.isValid());
        ensure_equals("empty list has no elements", block.list.size(), size_t(0));

        block.list.add(10);
        ensure("one element is still below AtLeast<2>", !block.list.isValid());

        block.list.add(20);
        ensure("two elements satisfy AtLeast<2>", block.list.isValid());
        ensure_equals("elements are kept in order", block.list()[0](), 10);
        ensure_equals("elements are kept in order", block.list()[1](), 20);
    }

    template<> template<>
    void object::test<7>()
    {
        set_test_name("a parameter that is a block is provided by its children");

        OuterBlock block;
        ensure("untouched sub-block is not provided", !block.inner.isProvided());

        block.inner.a = 5;
        ensure("setting a child provides the sub-block", block.inner.isProvided());
        ensure("and provides the enclosing block", block.isProvided());
        ensure_equals("child value readable", block.inner.a(), 5);
        ensure_equals("sibling child keeps its default", block.inner.b(), 0);
    }

    template<> template<>
    void object::test<8>()
    {
        set_test_name("fillFrom yields to what is already provided, overwriteFrom does not");

        SimpleBlock source;
        source.count = 100;
        source.flag = true;

        SimpleBlock destination;
        destination.count = 1;

        destination.fillFrom(source);
        ensure_equals("fillFrom leaves a provided value alone", destination.count(), 1);
        ensure_equals("fillFrom supplies an absent value", destination.flag(), true);

        destination.overwriteFrom(source);
        ensure_equals("overwriteFrom replaces a provided value", destination.count(), 100);
    }

    template<> template<>
    void object::test<9>()
    {
        set_test_name("choosing one alternative unprovides the other");

        ChoiceOfThings choice;
        ensure("first declared alternative is chosen initially", choice.number.isChosen());

        choice.text = std::string("hello");
        ensure("assignment chooses", choice.text.isChosen());
        ensure("and unchooses the previous alternative", !choice.number.isChosen());
        ensure("the displaced alternative is no longer provided", !choice.number.isProvided());
        ensure_equals("the chosen alternative reads back", choice.text(), std::string("hello"));

        choice.number = 3;
        ensure("choosing back works", choice.number.isChosen());
        ensure("and displaces the other", !choice.text.isProvided());
    }

    template<> template<>
    void object::test<10>()
    {
        set_test_name("scalars survive an LLSD round-trip");

        SimpleBlock written;
        written.name = std::string("round");
        written.count = 55;
        written.flag = true;

        LLSD sd;
        LLParamSDParser parser;
        parser.writeSD(sd, written);

        ensure("provided scalars are written", sd.has("count"));
        ensure_equals("written value is the one set", sd["count"].asInteger(), 55);

        SimpleBlock read;
        LLParamSDParser reader;
        reader.readSD(sd, read);

        ensure_equals("string survives", read.name(), std::string("round"));
        ensure_equals("integer survives", read.count(), 55);
        ensure_equals("boolean survives", read.flag(), true);
        ensure("reading marks parameters provided", read.count.isProvided());
    }

    template<> template<>
    void object::test<11>()
    {
        set_test_name("nested blocks and lists survive an LLSD round-trip");

        OuterBlock written;
        written.inner.a = 11;
        written.inner.b = 22;
        written.list.add(1);
        written.list.add(2);
        written.list.add(3);

        LLSD sd;
        LLParamSDParser parser;
        parser.writeSD(sd, written);

        ensure("sub-block is written under its own name", sd.has("inner"));
        ensure_equals("sub-block child is written", sd["inner"]["a"].asInteger(), 11);

        OuterBlock read;
        LLParamSDParser reader;
        reader.readSD(sd, read);

        ensure_equals("nested value survives", read.inner.a(), 11);
        ensure_equals("nested value survives", read.inner.b(), 22);
        ensure_equals("list length survives", read.list.size(), size_t(3));
        ensure_equals("list order survives", read.list()[2](), 3);
    }

    template<> template<>
    void object::test<12>()
    {
        set_test_name("a named value round-trips as its name, not its number");

        SimpleBlock written;
        written.named = std::string("two");

        LLSD sd;
        LLParamSDParser parser;
        parser.writeSD(sd, written);

        ensure_equals("the name is what gets written", sd["named"].asString(), std::string("two"));

        SimpleBlock read;
        LLParamSDParser reader;
        reader.readSD(sd, read);

        ensure_equals("the name resolves back to its value", read.named(), 2);
        ensure_equals("and the name is remembered", read.named.getValueName(), std::string("two"));
    }

    template<> template<>
    void object::test<13>()
    {
        set_test_name("a synonym reaches the same parameter");

        LLSD sd;
        sd["w"] = 64;

        SynonymBlock block;
        LLParamSDParser parser;
        parser.readSD(sd, block);

        ensure("the synonym provided the parameter", block.width.isProvided());
        ensure_equals("through to the same storage", block.width(), 64);
    }

    template<> template<>
    void object::test<14>()
    {
        set_test_name("an Ignored parameter is accepted and discarded");

        LLSD sd;
        sd["kept"] = 5;
        sd["gone"] = 6;

        IgnoredBlock block;
        LLParamSDParser parser;
        parser.readSD(sd, block);

        ensure_equals("the live parameter is read", block.kept(), 5);
        ensure("the block is valid despite the ignored key", block.validateBlock(false));
    }

    template<> template<>
    void object::test<15>()
    {
        set_test_name("a custom value can be set whole or by component");

        PointBlock whole;
        whole.point.set(TestPoint(3, 4));
        ensure_equals("whole assignment reaches the value", whole.point().mX, 3);
        ensure_equals("whole assignment reaches the value", whole.point().mY, 4);
        ensure_equals("and splats out to the components", whole.point.x(), 3);

        PointBlock components;
        components.point.x = 8;
        components.point.y = 9;
        ensure_equals("component assignment builds the value", components.point().mX, 8);
        ensure_equals("component assignment builds the value", components.point().mY, 9);
    }

    template<> template<>
    void object::test<16>()
    {
        set_test_name("a custom value round-trips through its components");

        PointBlock written;
        written.point.x = 12;
        written.point.y = 34;

        LLSD sd;
        LLParamSDParser parser;
        parser.writeSD(sd, written);

        PointBlock read;
        LLParamSDParser reader;
        reader.readSD(sd, read);

        ensure_equals("x survives", read.point().mX, 12);
        ensure_equals("y survives", read.point().mY, 34);
    }

    template<> template<>
    void object::test<17>()
    {
        set_test_name("a Lazy sub-block stays absent until something fills it");

        // Lazy<> keeps its block on the heap rather than deriving from it, so
        // an untouched one costs a pointer and serialises to nothing at all.
        OuterBlock untouched;
        ensure("lazy sub-block starts unprovided", !untouched.lazy_inner.isProvided());

        LLSD empty_sd;
        LLParamSDParser empty_parser;
        empty_parser.writeSD(empty_sd, untouched);
        ensure("an unfilled lazy block writes nothing", !empty_sd.has("lazy_inner"));

        LLSD sd;
        sd["lazy_inner"]["a"] = 77;

        OuterBlock filled;
        LLParamSDParser parser;
        parser.readSD(sd, filled);

        ensure("parsing into it provides it", filled.lazy_inner.isProvided());
        ensure_equals("and the value is reachable", filled.lazy_inner().a(), 77);
    }

    template<> template<>
    void object::test<18>()
    {
        set_test_name("copying a block carries values and provided-ness");

        SimpleBlock original;
        original.name = std::string("copy me");
        original.count = 99;

        SimpleBlock copy(original);

        ensure_equals("values copy", copy.count(), 99);
        ensure_equals("values copy", copy.name(), std::string("copy me"));
        ensure("provided-ness copies", copy.count.isProvided());
        ensure("unprovided stays unprovided", !copy.flag.isProvided());

        // The copy must own its storage: changing it must not disturb the original.
        copy.count = 1;
        ensure_equals("the original is untouched", original.count(), 99);
    }

    template<> template<>
    void object::test<19>()
    {
        set_test_name("the descriptor registry sees every block that has been built");

        // Constructing a block is what registers its parameters, so touch one
        // of each kind declared above before counting.
        SimpleBlock simple;
        DerivedBlock derived;
        OuterBlock outer;
        ChoiceOfThings choice;
        PointBlock point;

        const std::vector<LLInitParam::BlockDescriptor*>& all =
            LLInitParam::BlockDescriptor::getAllDescriptors();

        ensure("every constructed block type registered", all.size() >= 5);

        size_t named_entries = 0;
        for (const LLInitParam::BlockDescriptor* descriptor : all)
        {
            named_entries += descriptor->mNamedParams.size();
        }
        ensure("the tables hold the parameters we declared", named_entries >= 12);

        // DerivedBlock aggregates SimpleBlock's four parameters on top of its
        // own, which is the duplication the census exists to measure.
        ensure_equals("a derived block carries its base's parameters too",
                      DerivedBlock::getBlockDescriptor().mNamedParams.size(),
                      SimpleBlock::getBlockDescriptor().mNamedParams.size() + 1);

        const std::string report = LLInitParam::BlockDescriptor::getStatsReport();
        ensure("the census reports something", !report.empty());
        ensure("the census names what it counted",
               report.find("block types registered") != std::string::npos);
    }
}
