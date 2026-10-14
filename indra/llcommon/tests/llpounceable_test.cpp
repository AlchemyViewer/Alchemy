/**
 * @file   llpounceable_test.cpp
 * @author Nat Goodspeed
 * @date   2015-05-22
 * @brief  Test for llpounceable.
 *
 * $LicenseInfo:firstyear=2015&license=viewerlgpl$
 * Copyright (c) 2015, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "llpounceable.h"
// STL headers
// std headers
// external library headers
#include <boost/bind.hpp>
// other Linden headers
#include <catch2/catch_test_macros.hpp>

namespace
{

/*----------------------------- string testing -----------------------------*/
void append(std::string* dest, const std::string& src)
{
    dest->append(src);
}

/*-------------------------- Data-struct testing ---------------------------*/
struct Data
{
    Data(const std::string& data):
        mData(data)
    {}
    const std::string mData;
};

void setter(Data** dest, Data* ptr)
{
    *dest = ptr;
}

static Data* static_check = 0;

// Declare an LLPounceableStatic ahead of its definition below, so the
// forward reference is resolved before runtime.
extern LLPounceable<Data*, LLPounceableStatic> gForward;

struct EnqueueCall
{
    EnqueueCall()
    {
        // Intentionally use a forward reference to an LLPounceableStatic that
        // we believe is NOT YET CONSTRUCTED. This models the scenario in
        // which a constructor in another translation unit runs before
        // constructors in this one. We very specifically want callWhenReady()
        // to work even in that case: we need the LLPounceableQueueImpl to be
        // initialized even if the LLPounceable itself is not.
        gForward.callWhenReady(boost::bind(setter, &static_check, _1));
    }
} nqcall;
// When this declaration is processed, we should enqueue the
// setter(&static_check, _1) call for when gForward is set non-NULL. Needless
// to remark, we want this call not to crash.

// Now declare gForward. Its constructor should not run until after nqcall's.
LLPounceable<Data*, LLPounceableStatic> gForward;

} // anonymous namespace

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    TEST_CASE("llpounceable: LLPounceableStatic queues calls made before its constructor runs", "[llpounceable]")
    {
        // LLPounceable<T, LLPounceableStatic>::callWhenReady() must work even
        // before LLPounceable's constructor runs. That's the whole point of
        // implementing it with an LLSingleton queue. This models (say)
        // LLPounceableStatic<LLMessageSystem*, LLPounceableStatic>.
        CHECK_FALSE(static_check);
        Data myData("test<1>");
        // gForward and static_check let go of myData with the test, however
        // it ends, rather than point past it at whatever runs next.
        struct LetGo
        {
            ~LetGo()
            {
                gForward = nullptr;
                static_check = nullptr;
            }
        } let_go;
        gForward = &myData;         // should run setter
        CHECK(static_check == &myData);
    }

    TEST_CASE("llpounceable: each LLPounceableQueue instance has its own queue", "[llpounceable]")
    {
        // We expect that LLPounceable<T, LLPounceableQueue> should have
        // different queues because that specialization stores the queue
        // directly in the LLPounceable instance.
        Data *aptr = 0, *bptr = 0;
        LLPounceable<Data*> a, b;
        a.callWhenReady(boost::bind(setter, &aptr, _1));
        b.callWhenReady(boost::bind(setter, &bptr, _1));
        CHECK_FALSE(aptr);
        CHECK_FALSE(bptr);
        Data adata("a"), bdata("b");
        a = &adata;
        CHECK(aptr == &adata);
        // but we haven't yet set b
        CHECK_FALSE(bptr);
        b = &bdata;
        CHECK(bptr == &bdata);
    }

    TEST_CASE("llpounceable: each LLPounceableStatic instance has its own queue", "[llpounceable]")
    {
        // LLPounceable<T, LLPounceableStatic> should also have a distinct
        // queue for each instance, but that engages an additional map lookup
        // because there's only one LLSingleton for each T.
        Data *aptr = 0, *bptr = 0;
        LLPounceable<Data*, LLPounceableStatic> a, b;
        a.callWhenReady(boost::bind(setter, &aptr, _1));
        b.callWhenReady(boost::bind(setter, &bptr, _1));
        CHECK_FALSE(aptr);
        CHECK_FALSE(bptr);
        Data adata("a"), bdata("b");
        a = &adata;
        CHECK(aptr == &adata);
        // but we haven't yet set b
        CHECK_FALSE(bptr);
        b = &bdata;
        CHECK(bptr == &bdata);
    }

    TEST_CASE("llpounceable: LLPounceable<T> looks like T", "[llpounceable]")
    {
        // We want LLPounceable<T, TAG> to be drop-in replaceable for a plain
        // T for read constructs. In particular, it should behave like a dumb
        // pointer -- and with zero abstraction cost for such usage.
        Data* aptr = 0;
        Data a("a");
        // should be able to initialize a pounceable (when its constructor
        // runs)
        LLPounceable<Data*> pounceable(&a);
        // should be able to pass LLPounceable<T> to function accepting T
        setter(&aptr, pounceable);
        CHECK(aptr == &a);
        // should be able to dereference with *
        CHECK((*pounceable).mData == "a");
        // should be able to dereference with ->
        CHECK(pounceable->mData == "a");
        // bool operations
        CHECK(pounceable);
        CHECK_FALSE(! pounceable);
    }

    TEST_CASE("llpounceable: callWhenReady() queues until non-null then calls at once", "[llpounceable]")
    {
        Data *p1 = 0, *p2 = 0, *p3 = 0;
        Data a("a");
        LLPounceable<Data*> pounceable;
        // queue up a couple setter() calls for later
        pounceable.callWhenReady(boost::bind(setter, &p1, _1));
        pounceable.callWhenReady(boost::bind(setter, &p2, _1));
        // should still be pending
        CHECK_FALSE(p1);
        CHECK_FALSE(p2);
        CHECK_FALSE(p3);
        pounceable = 0;
        // assigning a new empty value shouldn't flush the queue
        CHECK_FALSE(p1);
        CHECK_FALSE(p2);
        CHECK_FALSE(p3);
        // using whichever syntax
        pounceable.reset(0);
        CHECK_FALSE(p1);
        CHECK_FALSE(p2);
        CHECK_FALSE(p3);
        pounceable.reset(&a);       // should flush queue
        CHECK(p1 == &a);
        CHECK(p2 == &a);
        CHECK_FALSE(p3);
        // immediate call
        pounceable.callWhenReady(boost::bind(setter, &p3, _1));
        CHECK(p3 == &a);
    }

    TEST_CASE("llpounceable: queued calls run in order and firing empties the queue", "[llpounceable]")
    {
        std::string data;
        LLPounceable<std::string*> pounceable;
        pounceable.callWhenReady(boost::bind(append, _1, "a"));
        pounceable.callWhenReady(boost::bind(append, _1, "b"));
        pounceable.callWhenReady(boost::bind(append, _1, "c"));
        pounceable = &data;
        UNSCOPED_INFO("callWhenReady() must preserve chronological order");
        CHECK(data == "abc");

        std::string data2;
        pounceable = NULL;
        pounceable.callWhenReady(boost::bind(append, _1, "d"));
        pounceable.callWhenReady(boost::bind(append, _1, "e"));
        pounceable.callWhenReady(boost::bind(append, _1, "f"));
        pounceable = &data2;
        UNSCOPED_INFO("LLPounceable must reset queue when fired");
        CHECK(data2 == "def");
    }

    // Only LLPounceableQueue and LLPounceableStatic work as tags: any other
    // tag, such as int, leaves LLPounceableQueueImpl<T, TAG> undefined, so the
    // following declaration must fail to compile. That is a compile-time
    // property with nothing to check at run time; uncomment it to see.
//  LLPounceable<Data*, int> pounceable;
} // anonymous namespace
