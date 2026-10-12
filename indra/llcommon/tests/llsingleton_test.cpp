/**
 * @file llsingleton_test.cpp
 * @date 2011-08-11
 * @brief Unit test for the LLSingleton class
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
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

#include "llsingleton.h"
#include "../test/lltut.h"
#include "wrapllerrs.h"
#include "llsd.h"

namespace
{

// Capture execution sequence by appending to log string.
std::string sLog;

#define DECLARE_CLASS(CLS)                          \
struct CLS: public LLSingleton<CLS>                 \
{                                                   \
    LLSINGLETON(CLS);                               \
    ~CLS();                                         \
public:                                             \
    static enum dep_flag {                          \
        DEP_NONE, /* no dependency */               \
        DEP_CTOR, /* dependency in ctor */          \
        DEP_INIT  /* dependency in initSingleton */ \
    } sDepFlag;                                     \
                                                    \
    void initSingleton() override;                  \
    void cleanupSingleton() override;               \
};                                                  \
                                                    \
CLS::dep_flag CLS::sDepFlag = DEP_NONE

DECLARE_CLASS(A);
DECLARE_CLASS(B);

#define DEFINE_MEMBERS(CLS, OTHER)              \
CLS::CLS()                                      \
{                                               \
    sLog.append(#CLS);                          \
    if (sDepFlag == DEP_CTOR)                   \
    {                                           \
        (void)OTHER::instance();                \
    }                                           \
}                                               \
                                                \
void CLS::initSingleton()                       \
{                                               \
    sLog.append("i" #CLS);                      \
    if (sDepFlag == DEP_INIT)                   \
    {                                           \
        (void)OTHER::instance();                \
    }                                           \
}                                               \
                                                \
void CLS::cleanupSingleton()                    \
{                                               \
    sLog.append("x" #CLS);                      \
}                                               \
                                                \
CLS::~CLS()                                     \
{                                               \
    sLog.append("~" #CLS);                      \
}

DEFINE_MEMBERS(A, B)
DEFINE_MEMBERS(B, A)

} // anonymous namespace

namespace tut
{
    struct singleton
    {
        // We need a class created with the LLSingleton template to test with.
        class LLSingletonTest: public LLSingleton<LLSingletonTest>
        {
            LLSINGLETON_EMPTY_CTOR(LLSingletonTest);
        };
    };

    typedef test_group<singleton> singleton_t;
    typedef singleton_t::object singleton_object_t;
    tut::singleton_t tut_llsingleton("LLSingleton");

    template<> template<>
    void singleton_object_t::test<1>()
    {

    }
    template<> template<>
    void singleton_object_t::test<2>()
    {
        LLSingletonTest* singleton_test = LLSingletonTest::getInstance();
        ensure(singleton_test);
    }

    template<> template<>
    void singleton_object_t::test<3>()
    {
        //Construct the instance
        LLSingletonTest::getInstance();
        ensure(LLSingletonTest::instanceExists());

        //Delete the instance
        LLSingletonTest::deleteSingleton();
        ensure(!LLSingletonTest::instanceExists());

        //Construct it again.
        LLSingletonTest* singleton_test = LLSingletonTest::getInstance();
        ensure(singleton_test);
        ensure(LLSingletonTest::instanceExists());
    }

    // Makes CLS, CLS and OTHER depending on each other as the flags say, and
    // checks the order they were made in, then deletes every singleton and
    // checks the order they were cleaned up and destroyed in.
    template <class CLS, class OTHER>
    static void check_order(typename CLS::dep_flag cls_dep, typename OTHER::dep_flag other_dep,
                            const std::string& made, const std::string& deleted)
    {
        CLS::sDepFlag = cls_dep;
        OTHER::sDepFlag = other_dep;
        sLog.clear();

        (void)CLS::instance();
        ensure_equals(sLog, made);
        LLSingletonBase::deleteAll();
        ensure_equals(sLog, made + deleted);
    }

    template<> template<>
    void singleton_object_t::test<4>()
    {
        set_test_name("just A");
        check_order<A, B>(A::DEP_NONE, B::DEP_NONE, "AiA", "xA~A");
    }

    template<> template<>
    void singleton_object_t::test<5>()
    {
        set_test_name("A ctor depends B");
        check_order<A, B>(A::DEP_CTOR, B::DEP_NONE, "ABiBiA", "xA~AxB~B");
    }

    template<> template<>
    void singleton_object_t::test<6>()
    {
        set_test_name("A init depends B");
        check_order<A, B>(A::DEP_INIT, B::DEP_NONE, "AiABiB", "xA~AxB~B");
    }

    template<> template<>
    void singleton_object_t::test<7>()
    {
        set_test_name("A circular init");
        check_order<A, B>(A::DEP_INIT, B::DEP_CTOR, "AiABiB", "xA~AxB~B");
    }

    template<> template<>
    void singleton_object_t::test<8>()
    {
        set_test_name("just B");
        check_order<B, A>(B::DEP_NONE, A::DEP_NONE, "BiB", "xB~B");
    }

    template<> template<>
    void singleton_object_t::test<9>()
    {
        set_test_name("B ctor depends A");
        check_order<B, A>(B::DEP_CTOR, A::DEP_NONE, "BAiAiB", "xB~BxA~A");
    }

    template<> template<>
    void singleton_object_t::test<10>()
    {
        set_test_name("B init depends A");
        check_order<B, A>(B::DEP_INIT, A::DEP_NONE, "BiBAiA", "xB~BxA~A");
    }

    template<> template<>
    void singleton_object_t::test<11>()
    {
        set_test_name("B circular init");
        check_order<B, A>(B::DEP_INIT, A::DEP_CTOR, "BiBAiA", "xB~BxA~A");
    }

#define PARAMSINGLETON(cls)                                             \
    class cls: public LLParamSingleton<cls>                             \
    {                                                                   \
        LLSINGLETON(cls, const LLSD::String& str): mDesc(str) {}        \
        cls(LLSD::Integer i): mDesc(i) {}                               \
                                                                        \
    public:                                                             \
        std::string desc() const { return mDesc.asString(); }           \
                                                                        \
    private:                                                            \
        LLSD mDesc;                                                     \
    }

    // Declare two otherwise-identical LLParamSingleton classes so we can
    // validly initialize each using two different constructors. If we tried
    // to test that with a single LLParamSingleton class within the same test
    // program, we'd get 'trying to use deleted LLParamSingleton' errors.
    namespace
    {
        PARAMSINGLETON(PSing1);
        PARAMSINGLETON(PSing2);
    } // anonymous namespace

    template<> template<>
    void singleton_object_t::test<12>()
    {
        set_test_name("LLParamSingleton");

        WrapLLErrs catcherr;
        // query methods
        ensure("false positive on instanceExists()", ! PSing1::instanceExists());
        ensure("false positive on wasDeleted()", ! PSing1::wasDeleted());
        // try to reference before initializing
        std::string threw = catcherr.catch_llerrs([](){
                (void)PSing1::instance();
            });
        ensure_contains("too-early instance() didn't throw", threw, "Uninitialized");
        // getInstance() behaves the same as instance()
        threw = catcherr.catch_llerrs([](){
                (void)PSing1::getInstance();
            });
        ensure_contains("too-early getInstance() didn't throw", threw, "Uninitialized");
        // initialize using LLSD::String constructor
        PSing1::initParamSingleton("string");
        ensure_equals(PSing1::instance().desc(), "string");
        ensure("false negative on instanceExists()", PSing1::instanceExists());
        // try to initialize again
        threw = catcherr.catch_llerrs([](){
                PSing1::initParamSingleton("again");
            });
        ensure_contains("second ctor(string) didn't throw", threw, "twice");
        // try to initialize using the other constructor -- should be
        // well-formed, but illegal at runtime
        threw = catcherr.catch_llerrs([](){
                PSing1::initParamSingleton(17);
            });
        ensure_contains("other ctor(int) didn't throw", threw, "twice");
        PSing1::deleteSingleton();
        ensure("false negative on wasDeleted()", PSing1::wasDeleted());
        threw = catcherr.catch_llerrs([](){
                (void)PSing1::instance();
            });
        ensure_contains("accessed deleted LLParamSingleton", threw, "deleted");
    }

    template<> template<>
    void singleton_object_t::test<13>()
    {
        set_test_name("LLParamSingleton alternate ctor");

        WrapLLErrs catcherr;
        // We don't have to restate all the tests for PSing1. Only test validly
        // using the other constructor.
        PSing2::initParamSingleton(17);
        ensure_equals(PSing2::instance().desc(), "17");
        // can't do it twice
        std::string threw = catcherr.catch_llerrs([](){
                PSing2::initParamSingleton(34);
            });
        ensure_contains("second ctor(int) didn't throw", threw, "twice");
        // can't use the other constructor either
        threw = catcherr.catch_llerrs([](){
                PSing2::initParamSingleton("string");
            });
        ensure_contains("other ctor(string) didn't throw", threw, "twice");
    }

    namespace
    {
        class CircularPCtor: public LLParamSingleton<CircularPCtor>
        {
            LLSINGLETON(CircularPCtor)
            {
                // never mind indirection, just go straight for the circularity
                (void)instance();
            }
        };
    } // anonymous namespace

    template<> template<>
    void singleton_object_t::test<14>()
    {
        set_test_name("Circular LLParamSingleton constructor");
        WrapLLErrs catcherr;
        std::string threw = catcherr.catch_llerrs([](){
                CircularPCtor::initParamSingleton();
            });
        ensure_contains("constructor circularity didn't throw", threw, "constructor");
    }

    namespace
    {
        class CircularPInit: public LLParamSingleton<CircularPInit>
        {
            LLSINGLETON_EMPTY_CTOR(CircularPInit);
        public:
            virtual void initSingleton() override
            {
                // never mind indirection, just go straight for the circularity
                CircularPInit *pt = getInstance();
                if (!pt)
                {
                    throw;
                }
            }
        };
    } // anonymous namespace

    template<> template<>
    void singleton_object_t::test<15>()
    {
        set_test_name("Circular LLParamSingleton initSingleton()");
        WrapLLErrs catcherr;
        std::string threw = catcherr.catch_llerrs([](){
                CircularPInit::initParamSingleton();
            });
        ensure("initSingleton() circularity threw", threw.empty());
    }
}
