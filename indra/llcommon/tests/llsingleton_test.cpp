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
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
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

namespace
{
    struct singleton
    {
        // We need a class created with the LLSingleton template to test with.
        class LLSingletonTest: public LLSingleton<LLSingletonTest>
        {
            LLSINGLETON_EMPTY_CTOR(LLSingletonTest);
        };
    };

    TEST_CASE_METHOD(singleton, "LLSingleton: getInstance() makes an instance", "[LLSingleton]")
    {
        LLSingletonTest* singleton_test = LLSingletonTest::getInstance();
        CHECK(singleton_test);
    }

    TEST_CASE_METHOD(singleton, "LLSingleton: deleteSingleton() removes the instance and getInstance() remakes it", "[LLSingleton]")
    {
        //Construct the instance
        LLSingletonTest::getInstance();
        CHECK(LLSingletonTest::instanceExists());

        //Delete the instance
        LLSingletonTest::deleteSingleton();
        CHECK_FALSE(LLSingletonTest::instanceExists());

        //Construct it again.
        LLSingletonTest* singleton_test = LLSingletonTest::getInstance();
        CHECK(singleton_test);
        CHECK(LLSingletonTest::instanceExists());
    }

    // Makes CLS, with CLS and OTHER depending on each other as the flags say,
    // and checks the order they were made in, then deletes every singleton
    // and checks the order they were cleaned up and destroyed in. CHECKs, so
    // that a wrong order still deletes them all and leaves the next case its
    // own to make.
    template <class CLS, class OTHER>
    static void require_order(typename CLS::dep_flag cls_dep, typename OTHER::dep_flag other_dep,
                              const std::string& made, const std::string& deleted)
    {
        CLS::sDepFlag = cls_dep;
        OTHER::sDepFlag = other_dep;
        sLog.clear();

        (void)CLS::instance();
        CHECK(sLog == made);
        LLSingletonBase::deleteAll();
        CHECK(sLog == made + deleted);
    }

    TEST_CASE_METHOD(singleton, "LLSingleton: A alone is made and deleted in order", "[LLSingleton]")
    {
        require_order<A, B>(A::DEP_NONE, B::DEP_NONE, "AiA", "xA~A");
    }

    TEST_CASE_METHOD(singleton, "LLSingleton: A needing B in its constructor is deleted before B", "[LLSingleton]")
    {
        require_order<A, B>(A::DEP_CTOR, B::DEP_NONE, "ABiBiA", "xA~AxB~B");
    }

    TEST_CASE_METHOD(singleton, "LLSingleton: A needing B in initSingleton is deleted before B", "[LLSingleton]")
    {
        require_order<A, B>(A::DEP_INIT, B::DEP_NONE, "AiABiB", "xA~AxB~B");
    }

    TEST_CASE_METHOD(singleton, "LLSingleton: A needing B in initSingleton and B needing A is deleted before B", "[LLSingleton]")
    {
        require_order<A, B>(A::DEP_INIT, B::DEP_CTOR, "AiABiB", "xA~AxB~B");
    }

    TEST_CASE_METHOD(singleton, "LLSingleton: B alone is made and deleted in order", "[LLSingleton]")
    {
        require_order<B, A>(B::DEP_NONE, A::DEP_NONE, "BiB", "xB~B");
    }

    TEST_CASE_METHOD(singleton, "LLSingleton: B needing A in its constructor is deleted before A", "[LLSingleton]")
    {
        require_order<B, A>(B::DEP_CTOR, A::DEP_NONE, "BAiAiB", "xB~BxA~A");
    }

    TEST_CASE_METHOD(singleton, "LLSingleton: B needing A in initSingleton is deleted before A", "[LLSingleton]")
    {
        require_order<B, A>(B::DEP_INIT, A::DEP_NONE, "BiBAiA", "xB~BxA~A");
    }

    TEST_CASE_METHOD(singleton, "LLSingleton: B needing A in initSingleton and A needing B is deleted before A", "[LLSingleton]")
    {
        require_order<B, A>(B::DEP_INIT, A::DEP_CTOR, "BiBAiA", "xB~BxA~A");
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
    PARAMSINGLETON(PSing1);
    PARAMSINGLETON(PSing2);

    TEST_CASE_METHOD(singleton, "LLSingleton: LLParamSingleton must be initialized once and not used after deletion", "[LLSingleton]")
    {
        WrapLLErrs catcherr;
        // query methods
        CHECK_FALSE(PSing1::instanceExists());
        CHECK_FALSE(PSing1::wasDeleted());
        // try to reference before initializing
        std::string threw = catcherr.catch_llerrs([](){
                (void)PSing1::instance();
            });
        UNSCOPED_INFO("too-early instance() didn't throw");
        CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("Uninitialized"));
        // getInstance() behaves the same as instance()
        threw = catcherr.catch_llerrs([](){
                (void)PSing1::getInstance();
            });
        UNSCOPED_INFO("too-early getInstance() didn't throw");
        CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("Uninitialized"));
        // initialize using LLSD::String constructor
        PSing1::initParamSingleton("string");
        CHECK(PSing1::instance().desc() == "string");
        CHECK(PSing1::instanceExists());
        // try to initialize again
        threw = catcherr.catch_llerrs([](){
                PSing1::initParamSingleton("again");
            });
        UNSCOPED_INFO("second ctor(string) didn't throw");
        CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("twice"));
        // try to initialize using the other constructor -- should be
        // well-formed, but illegal at runtime
        threw = catcherr.catch_llerrs([](){
                PSing1::initParamSingleton(17);
            });
        UNSCOPED_INFO("other ctor(int) didn't throw");
        CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("twice"));
        PSing1::deleteSingleton();
        CHECK(PSing1::wasDeleted());
        threw = catcherr.catch_llerrs([](){
                (void)PSing1::instance();
            });
        UNSCOPED_INFO("accessed deleted LLParamSingleton");
        CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("deleted"));
    }

    TEST_CASE_METHOD(singleton, "LLSingleton: LLParamSingleton by its alternate constructor initializes only once", "[LLSingleton]")
    {
        WrapLLErrs catcherr;
        // We don't have to restate all the tests for PSing1. Only test validly
        // using the other constructor.
        PSing2::initParamSingleton(17);
        CHECK(PSing2::instance().desc() == "17");
        // can't do it twice
        std::string threw = catcherr.catch_llerrs([](){
                PSing2::initParamSingleton(34);
            });
        UNSCOPED_INFO("second ctor(int) didn't throw");
        CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("twice"));
        // can't use the other constructor either
        threw = catcherr.catch_llerrs([](){
                PSing2::initParamSingleton("string");
            });
        UNSCOPED_INFO("other ctor(string) didn't throw");
        CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("twice"));
    }

    class CircularPCtor: public LLParamSingleton<CircularPCtor>
    {
        LLSINGLETON(CircularPCtor)
        {
            // never mind indirection, just go straight for the circularity
            (void)instance();
        }
    };

    TEST_CASE_METHOD(singleton, "LLSingleton: a circular LLParamSingleton constructor errs", "[LLSingleton]")
    {
        WrapLLErrs catcherr;
        std::string threw = catcherr.catch_llerrs([](){
                CircularPCtor::initParamSingleton();
            });
        CHECK_THAT(threw, Catch::Matchers::ContainsSubstring("constructor"));
    }

    class CircularPInit: public LLParamSingleton<CircularPInit>
    {
        LLSINGLETON_EMPTY_CTOR(CircularPInit);
    public:
        // Whether getInstance() from initSingleton() gave the instance being
        // initialized. Refused, it would have erred, which WrapLLErrs throws.
        static inline bool sFoundItself = false;

        void initSingleton() override
        {
            // never mind indirection, just go straight for the circularity
            sFoundItself = getInstance() == this;
        }
    };

    TEST_CASE_METHOD(singleton, "LLSingleton: a circular LLParamSingleton initSingleton() finds the instance", "[LLSingleton]")
    {
        WrapLLErrs catcherr;
        std::string threw = catcherr.catch_llerrs([](){
                CircularPInit::initParamSingleton();
            });
        CHECK(threw.empty());
        CHECK(CircularPInit::sFoundItself);
    }
}
