/**
 * @file   lazyeventapi_test.cpp
 * @author Nat Goodspeed
 * @date   2022-06-18
 * @brief  Test for lazyeventapi.
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Copyright (c) 2022, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "lazyeventapi.h"
// STL headers
// std headers
#include <memory>
// external library headers
// other Linden headers
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "llevents.h"
#include "llsdutil.h"

// observable side effect, solely for testing
static LLSD data;

namespace
{

// LLEventAPI listener subclass
class MyListener: public LLEventAPI
{
public:
    // need this trivial forwarding constructor
    // (of course do any other initialization your subclass requires)
    MyListener(const LL::LazyEventAPIParams& params):
        LLEventAPI(params)
    {}

    // example operation, registered by LazyEventAPI subclass below
    void set_data(const LLSD& event)
    {
        data = event["data"];
    }
};

// LazyEventAPI registrar subclass
class MyRegistrar: public LL::LazyEventAPI<MyListener>
{
    using super = LL::LazyEventAPI<MyListener>;
    using super::listener;
public:
    // LazyEventAPI subclass initializes like a classic LLEventAPI subclass
    // constructor, with API name and desc plus add() calls for the defined
    // operations
    MyRegistrar():
        super("Test", "This is a test LLEventAPI")
    {
        add("set", "This is a set operation", &listener::set_data);
    }
};

} // anonymous namespace

// Normally we'd declare a static instance of MyRegistrar -- but because we
// want to test both with and without, defer declaration to individual test
// methods.

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    struct lazyeventapi_data
    {
        lazyeventapi_data()
        {
            // before every test, reset 'data'
            data.clear();
        }
        ~lazyeventapi_data()
        {
            // after every test, reset LLEventPumps
            LLEventPumps::deleteSingleton();
        }
    };

    TEST_CASE_METHOD(lazyeventapi_data, "lazyeventapi: a LazyEventAPI creates its listener when its pump is used", "[lazyeventapi]")
    {
        // this is where the magic (should) happen
        // 'register' still a keyword until C++17
        MyRegistrar regster;
        LLEventPumps::instance().obtain("Test").post(llsd::map("op", "set", "data", "hey"));
        CHECK(data.asString() == "hey");
    }

    TEST_CASE_METHOD(lazyeventapi_data, "lazyeventapi: a destroyed LazyEventAPI no longer creates its listener", "[lazyeventapi]")
    {
        // A registrar destroyed before anyone obtains its pump takes its pump
        // factory with it, so this post must NOT reach a MyListener instance.
        auto regster{ std::make_unique<MyRegistrar>() };
        regster.reset();
        LLEventPumps::instance().obtain("Test").post(llsd::map("op", "set", "data", "moot"));
        CHECK_FALSE(data.isDefined());
    }

    TEST_CASE_METHOD(lazyeventapi_data, "lazyeventapi: instance_snapshot finds a LazyEventAPI and its metadata", "[lazyeventapi]")
    {
        MyRegistrar regster;
        // Of course we have 'regster' in hand; we don't need to search for
        // it. But this next test verifies that we can find (all) LazyEventAPI
        // instances using LazyEventAPIBase::instance_snapshot. Normally we
        // wouldn't search; normally we'd just look at each instance in the
        // loop body.
        const MyRegistrar* found = nullptr;
        for (const auto& registrar : LL::LazyEventAPIBase::instance_snapshot())
            if ((found = dynamic_cast<const MyRegistrar*>(&registrar)))
                break;
        UNSCOPED_INFO("Failed to find MyRegistrar via LLInstanceTracker");
        REQUIRE(found);

        CHECK(found->getName() == "Test");
        CHECK_THAT(found->getDesc(), Catch::Matchers::ContainsSubstring("test LLEventAPI"));
        CHECK(found->getDispatchKey() == "op");
        // Normally we'd just iterate over *found. But for test purposes,
        // actually capture the range of NameDesc pairs in a vector.
        std::vector<LL::LazyEventAPIBase::NameDesc> ops{ found->begin(), found->end() };
        REQUIRE(ops.size() == 1);
        CHECK(ops[0].first == "set");
        CHECK_THAT(ops[0].second, Catch::Matchers::ContainsSubstring("set operation"));
        LLSD metadata{ found->getMetadata(ops[0].first) };
        CHECK(metadata["name"].asString() == ops[0].first);
        CHECK(metadata["desc"].asString() == ops[0].second);
    }
} // anonymous namespace
