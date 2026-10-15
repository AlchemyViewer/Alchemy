/**
 * @file   llevents_tut.cpp
 * @author Nat Goodspeed
 * @date   2008-09-12
 * @brief  Test of llevents.h
 *
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
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

#if LL_WINDOWS
#pragma warning (disable : 4675) // "resolved by ADL" -- just as I want!
#endif

// Precompiled header
#include "linden_common.h"
// associated header
// UGLY HACK! We want to verify state internal to the classes without
// providing public accessors.
#define testable public
#include "llevents.h"
#undef testable
// STL headers
// std headers
#include <iostream>
#include <typeinfo>
// external library headers
#include <boost/bind.hpp>
// other Linden headers
#include "tests/listener.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "catch_and_store_what_in.h"

template<typename T>
static T make(const T& value)
{
    return value;
}

/*****************************************************************************
 *   Tests
 *****************************************************************************/
namespace
{
struct events_data
{
    events_data() :
        pumps(LLEventPumps::instance()),
        listener0("first"),
        listener1("second")
    {
    }
    // The named pumps these tests obtain go with each test, and every
    // listener on them, bound to this fixture or to a test's locals, with
    // them: each test finds them new whatever ran before it, and none is
    // left for a later post to call into a dead object.
    ~events_data()
    {
        for (const char* name : { "per-frame", "login", "upstream", "filter0", "filter1", "button",
                                  "stream", "heaptest" })
        {
            const auto found = pumps.mPumpMap.find(name);
            if (found != pumps.mPumpMap.end() && pumps.mOurPumps.count(found->second))
            {
                // unregisters itself
                delete found->second;
            }
        }
    }
    LLEventPumps& pumps;
    Listener listener0;
    Listener listener1;

    void require_last_event(const std::string& desc, const Listener& listener, LLSD::Integer got)
    {
        INFO(listener << ' ' << desc);
        REQUIRE(listener.getLastEvent().asInteger() == got);
    }
};

TEST_CASE_METHOD(events_data, "events: basic operations", "[events]")
{
    // Having to modify this to track the statically-
    // constructed pumps in other test files in this giant monolithic test
    // executable isn't such a hot idea.
    // ensure_equals("initial pump", pumps.mPumpMap.size(), 1);
    size_t initial_pumps(pumps.mPumpMap.size());
    LLEventPump& per_frame(pumps.obtain("per-frame"));
    CHECK(pumps.mPumpMap.size() == initial_pumps + 1);
    // Verify that per_frame was instantiated as an LLEventStream.
    CHECK(dynamic_cast<LLEventStream*> (&per_frame));
    CHECK(per_frame.enabled());
    // Trivial test, but posting an event to an EventPump with no
    // listeners should not blow up. The test is relevant because defining
    // a boost::signal with a non-void return signature, using the default
    // combiner, blows up if there are no listeners. This is because the
    // default combiner is defined to return the value returned by the
    // last listener, which is meaningless if there were no listeners.
    per_frame.post(0);
    LLBoundListener connection = listener0.listenTo(per_frame);
    CHECK(connection.connected());
    CHECK_FALSE(connection.blocked());
    per_frame.post(1);
    require_last_event("received", listener0, 1);
    { // block the connection
        LLEventPump::Blocker block(connection);
        CHECK(connection.blocked());
        per_frame.post(2);
        require_last_event("not updated", listener0, 1);
    } // unblock
    CHECK_FALSE(connection.blocked());
    per_frame.post(3);
    require_last_event("unblocked", listener0, 3);
    LLBoundListener sameConnection = per_frame.getListener(listener0.getName());
    CHECK(sameConnection.connected());
    CHECK_FALSE(sameConnection.blocked());
    { // block it again
        LLEventPump::Blocker block(sameConnection);
        CHECK(sameConnection.blocked());
        per_frame.post(4);
        require_last_event("re-blocked", listener0, 3);
    } // unblock
    std::string threw = catch_what<LLEventPump::DupListenerName>(
        [&per_frame, this](){
            // NOTE: boost::bind() saves its arguments by VALUE! If you pass
            // an object instance rather than a pointer, you'll end up binding
            // to an internal copy of that instance! Use boost::ref() to
            // capture a reference instead.
            per_frame.listen(listener0.getName(), // note bug, dup name
                             boost::bind(&Listener::call, boost::ref(listener1), _1));
        });
    CHECK(threw ==
                  std::string("DupListenerName: "
                              "Attempt to register duplicate listener name '") +
                              listener0.getName() + "' on " + typeid(per_frame).name() +
                              " '" + per_frame.getName() + "'");
    // do it right this time
    listener1.listenTo(per_frame);
    per_frame.post(5);
    require_last_event("got", listener0, 5);
    require_last_event("got", listener1, 5);
    per_frame.enable(false);
    per_frame.post(6);
    require_last_event("didn't get", listener0, 5);
    require_last_event("didn't get", listener1, 5);
    per_frame.enable();
    per_frame.post(7);
    require_last_event("got", listener0, 7);
    require_last_event("got", listener1, 7);
    per_frame.stopListening(listener0.getName());
    CHECK_FALSE(connection.connected());
    CHECK_FALSE(sameConnection.connected());
    per_frame.post(8);
    require_last_event("disconnected", listener0, 7);
    require_last_event("still connected", listener1, 8);
    per_frame.stopListening(listener1.getName());
    per_frame.post(9);
    require_last_event("disconnected", listener1, 8);
}

TEST_CASE_METHOD(events_data, "events: callstop() returning true", "[events]")
{
    LLEventPump& per_frame(pumps.obtain("per-frame"));
    listener0.reset(0);
    listener1.reset(0);
    LLBoundListener bound0 = listener0.listenTo(per_frame, &Listener::callstop);
    LLBoundListener bound1 = listener1.listenTo(per_frame, &Listener::call,
                                                // after listener0
                                                make<LLEventPump::NameList>(LLEventPump::NameList{ listener0.getName() }));
    CHECK(per_frame.enabled());
    CHECK(bound0.connected());
    CHECK_FALSE(bound0.blocked());
    CHECK(bound1.connected());
    CHECK_FALSE(bound1.blocked());
    per_frame.post(1);
    require_last_event("got", listener0, 1);
    // Because listener0.callstop() returns true, control never reaches listener1.call().
    require_last_event("got", listener1, 0);
}

TEST_CASE_METHOD(events_data, "events: an explicit LLEventStream registers itself until destroyed", "[events]")
{
    // Explicitly instantiate an LLEventStream, and verify that it
    // self-registers with LLEventPumps
    size_t registered = pumps.mPumpMap.size();
    size_t owned = pumps.mOurPumps.size();
    LLEventPump* localInstance;
    {
        LLEventStream myEventStream("stream");
        localInstance = &myEventStream;
        LLEventPump& stream(pumps.obtain("stream"));
        CHECK(&stream == localInstance);
        CHECK(pumps.mPumpMap.size() == registered + 1);
        CHECK(pumps.mOurPumps.size() == owned);
    } // destroy myEventStream -- should unregister
    CHECK(pumps.mPumpMap.size() == registered);
    CHECK(pumps.mOurPumps.size() == owned);
    LLEventPump& stream(pumps.obtain("stream"));
    CHECK(&stream != localInstance);
    CHECK(pumps.mPumpMap.size() == registered + 1);
    CHECK(pumps.mOurPumps.size() == owned + 1);
}

TEST_CASE_METHOD(events_data, "events: stopListening() frees the name and a bogus name has no connection", "[events]")
{
    LLEventPump& login(pumps.obtain("login"));
    listener0.listenTo(login);
    login.stopListening(listener0.getName());
    // should not throw because stopListening() should have removed name
    listener0.listenTo(login, &Listener::callstop);
    LLBoundListener wrong = login.getListener("bogus");
    CHECK_FALSE(wrong.connected());
    CHECK(wrong.blocked());
}

TEST_CASE_METHOD(events_data, "events: chaining LLEventPump instances", "[events]")
{
    LLEventPump& upstream(pumps.obtain("upstream"));
    // One potentially-useful construct is to chain LLEventPumps together.
    // Among other things, this allows you to turn subsets of listeners on
    // and off in groups.
    LLEventPump& filter0(pumps.obtain("filter0"));
    LLEventPump& filter1(pumps.obtain("filter1"));
    upstream.listen(filter0.getName(), boost::bind(&LLEventPump::post, boost::ref(filter0), _1));
    upstream.listen(filter1.getName(), boost::bind(&LLEventPump::post, boost::ref(filter1), _1));
    listener0.listenTo(filter0);
    listener1.listenTo(filter1);
    listener0.reset(0);
    listener1.reset(0);
    upstream.post(1);
    require_last_event("got unfiltered", listener0, 1);
    require_last_event("got unfiltered", listener1, 1);
    filter0.enable(false);
    upstream.post(2);
    require_last_event("didn't get filtered", listener0, 1);
    require_last_event("got filtered", listener1, 2);
}

TEST_CASE_METHOD(events_data, "events: listener dependency order", "[events]")
{
    typedef LLEventPump::NameList NameList;
    LLEventPump& button(pumps.obtain("button"));
    Collect collector;
    button.listen("Mary",
                  boost::bind(&Collect::add, boost::ref(collector), "Mary", _1),
                  // state that "Mary" must come after "checked"
                  make<NameList>(NameList{ "checked" }));
    button.listen("checked",
                  boost::bind(&Collect::add, boost::ref(collector), "checked", _1),
                  // "checked" must come after "spot"
                  make<NameList>(NameList{ "spot" }));
    button.listen("spot",
                  boost::bind(&Collect::add, boost::ref(collector), "spot", _1));
    button.post(1);
    CHECK(collector.result == make<StringVec>(StringVec{ "spot", "checked", "Mary" }));
    collector.clear();
    button.stopListening("Mary");
    button.listen("Mary",
            boost::bind(&Collect::add, boost::ref(collector), "Mary", _1),
            LLEventPump::empty, // no after dependencies
            // now "Mary" must come before "spot"
                  make<NameList>(NameList{ "spot" }));
    button.post(2);
    CHECK(collector.result == make<StringVec>(StringVec{ "Mary", "spot", "checked" }));
    collector.clear();
    button.stopListening("spot");
    std::string threw = catch_what<LLEventPump::Cycle>(
        [&button, &collector](){
            button.listen("spot",
                          boost::bind(&Collect::add, boost::ref(collector), "spot", _1),
                          // after "Mary" and "checked" -- whoops!
                          make<NameList>(NameList{ "Mary", "checked" }));
        });
    // Obviously the specific wording of the exception text can
    // change; go ahead and change the test to match.
    // Establish that it contains:
    // - the name and runtime type of the LLEventPump
    CHECK_THAT(threw, Catch::Matchers::ContainsSubstring(typeid(button).name()));
    CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("'button'"));
    // - the name of the new listener that caused the problem
    CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("'spot'"));
    // - a synopsis of the problematic dependencies.
    CHECK_THAT(threw,
                    Catch::Matchers::ContainsSubstring("\"Mary\" -> before (\"spot\")"));
    CHECK_THAT(threw,
                    Catch::Matchers::ContainsSubstring("after (\"spot\") -> \"checked\""));
    CHECK_THAT(threw,
                    Catch::Matchers::ContainsSubstring("after (\"Mary\", \"checked\") -> \"spot\""));
    button.listen("yellow",
                  boost::bind(&Collect::add, boost::ref(collector), "yellow", _1), make<NameList>(NameList{ "checked" }));
    button.listen("shoelaces",
                  boost::bind(&Collect::add, boost::ref(collector), "shoelaces", _1), make<NameList>(NameList{ "checked" }));
    button.post(3);
    CHECK(collector.result == make<StringVec>(StringVec{ "Mary", "checked", "yellow", "shoelaces" }));
    collector.clear();
    threw = catch_what<LLEventPump::OrderChange>(
        [&button, &collector](){
            button.listen("of",
                          boost::bind(&Collect::add, boost::ref(collector), "of", _1),
                          make<NameList>(NameList{ "shoelaces" }),
                          make<NameList>(NameList{ "yellow" }));
        });
    // Same remarks about the specific wording of the exception. Just
    // ensure that it contains enough information to clarify the
    // problem and what must be done to resolve it.
    CHECK_THAT(threw, Catch::Matchers::ContainsSubstring(typeid(button).name()));
    CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("'button'"));
    UNSCOPED_INFO("new listener name");
    CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("'of'"));
    UNSCOPED_INFO("prev listener name");
    CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("'yellow'"));
    // std::cout << "Thrown Exception: " << threw << std::endl;
    CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("was: Mary, checked, yellow, shoelaces"));
    CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("now: Mary, checked, shoelaces, of, yellow"));
    button.post(4);
    CHECK(collector.result == make<StringVec>(StringVec{ "Mary", "checked", "yellow", "shoelaces" }));
}

TEST_CASE_METHOD(events_data, "events: tweaked and untweaked LLEventPump instance names", "[events]")
{
    {   // nested scope
        // Hand-instantiate an LLEventStream...
        LLEventStream bob("bob");
        std::string threw = catch_what<LLEventPump::DupPumpName>(
            [](){
                // then another with a duplicate name.
                LLEventStream bob2("bob");
            });
        CHECK_FALSE(threw.empty());
    }   // delete first 'bob'
    LLEventStream bob("bob");       // should work, previous one unregistered
    LLEventStream bob1("bob", true);// allowed to tweak name
    CHECK(bob1.getName() == "bob1");
    std::vector<std::shared_ptr<LLEventStream> > streams;
    for (int i = 2; i <= 10; ++i)
    {
        streams.push_back(std::shared_ptr<LLEventStream>(new LLEventStream("bob", true)));
    }
    CHECK(streams.back()->getName() == "bob10");
}

// Define a function that accepts an LLListenerOrPumpName
static void eventSource(const LLListenerOrPumpName& listener)
{
    // Pretend that some time has elapsed. Call listener immediately.
    listener(17);
}

TEST_CASE_METHOD(events_data, "events: LLListenerOrPumpName calls a listener or posts to a named pump", "[events]")
{
    // Passing a boost::bind() expression to LLListenerOrPumpName
    listener0.reset(0);
    eventSource(boost::bind(&Listener::call, boost::ref(listener0), _1));
    require_last_event("got by listener", listener0, 17);
    // Passing a string LLEventPump name to LLListenerOrPumpName
    listener0.reset(0);
    LLEventStream random("random");
    listener0.listenTo(random);
    eventSource("random");
    require_last_event("got by pump name", listener0, 17);
    std::string threw = catch_what<LLListenerOrPumpName::Empty>(
        [](){
            LLListenerOrPumpName empty;
            empty(17);
        });

    CHECK_FALSE(threw.empty());
}


class TempListener: public Listener
{
public:
    TempListener(const std::string& name, bool& liveFlag) :
        Listener(name), mLiveFlag(liveFlag)
    {
        mLiveFlag = true;
    }

    virtual ~TempListener()
    {
        mLiveFlag = false;
    }

private:
    bool& mLiveFlag;
};


TEST_CASE_METHOD(events_data, "events: a destroyed plain listener still looks connected", "[events]")
{
    // listen() can't do anything about a plain TempListener instance:
    // it's not managed with shared_ptr, nor is it an LLEventTrackable subclass
    bool live = false;
    LLEventPump& heaptest(pumps.obtain("heaptest"));
    LLBoundListener connection;
    {
        TempListener tempListener("temp", live);
        UNSCOPED_INFO("TempListener constructed");
        CHECK(live);
        connection = heaptest.listen(tempListener.getName(),
                                     boost::bind(&Listener::call,
                                                 boost::ref(tempListener),
                                                 _1));
        heaptest.post(1);
        require_last_event("received", tempListener, 1);
    } // presumably this will make newListener go away?
    // verify that
    UNSCOPED_INFO("TempListener destroyed");
    CHECK_FALSE(live);
    // This is the case against which we can't defend. Don't even try to
    // post to heaptest -- that would engage Undefined Behavior.
    // Cautiously inspect connection...
    UNSCOPED_INFO("misleadingly connected");
    CHECK(connection.connected());
    // then disconnect by hand.
    heaptest.stopListening("temp");
}


class TempTrackableListener: public TempListener, public LLEventTrackable
{
public:
    TempTrackableListener(const std::string& name, bool& liveFlag):
        TempListener(name, liveFlag)
    {}
};


TEST_CASE_METHOD(events_data, "events: destroying a bound LLEventTrackable disconnects it", "[events]")
{
    bool live = false;
    LLEventPump& heaptest(pumps.obtain("heaptest"));
    LLBoundListener connection;
    {
        TempTrackableListener tempListener("temp", live);
        UNSCOPED_INFO("TempTrackableListener constructed");
        CHECK(live);
        connection = heaptest.listen(tempListener.getName(),
                                     boost::bind(&TempTrackableListener::call,
                                                 boost::ref(tempListener), _1));
        heaptest.post(1);
        require_last_event("received", tempListener, 1);
    } // presumably this will make tempListener go away?
    // verify that
    UNSCOPED_INFO("TempTrackableListener destroyed");
    CHECK_FALSE(live);
    UNSCOPED_INFO("implicit disconnect");
    CHECK_FALSE(connection.connected());
    // now just make sure we don't blow up trying to access a freed object!
    heaptest.post(2);
}

TEST_CASE_METHOD(events_data, "events: deleting a bound LLEventTrackable pointer disconnects it", "[events]")
{
    bool live = false;
    LLEventPump& heaptest(pumps.obtain("heaptest"));
    LLBoundListener connection;
    {
        TempTrackableListener* newListener(new TempTrackableListener("temp", live));
        UNSCOPED_INFO("TempTrackableListener constructed");
        CHECK(live);
        connection = heaptest.listen(newListener->getName(),
                                     boost::bind(&TempTrackableListener::call,
                                                 newListener, _1));
        heaptest.post(1);
        require_last_event("received", *newListener, 1);
        // explicitly destroy newListener
        delete newListener;
    }
    // verify that
    UNSCOPED_INFO("TempTrackableListener destroyed");
    CHECK_FALSE(live);
    UNSCOPED_INFO("implicit disconnect");
    CHECK_FALSE(connection.connected());
    // now just make sure we don't blow up trying to access a freed object!
    heaptest.post(2);
}

} // anonymous namespace
