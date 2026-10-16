/**
 * @file llerror_test.cpp
 * @date   December 2006
 * @brief error unit tests
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

#include <vector>
#include <stdexcept>

#include "linden_common.h"

#include "../llerror.h"

#include "../llerrorcontrol.h"
#include "../llsd.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

namespace
{

enum LogFieldIndex
{
    TIME_FIELD,
    LEVEL_FIELD,
    TAGS_FIELD,
    LOCATION_FIELD,
    FUNCTION_FIELD,
    MSG_FIELD
};

static const char* FieldName[] =
{
    "TIME",
    "LEVEL",
    "TAGS",
    "LOCATION",
    "FUNCTION",
    "MSG"
};

} // anonymous namespace

namespace
{
#if defined(LL_CLANG)
#   pragma clang diagnostic ignored "-Wunused-function"
#elif defined(LL_GNUC)
#   pragma GCC diagnostic ignored "-Wunused-function"
#endif
    void test_that_error_h_includes_enough_things_to_compile_a_message()
    {
        LL_INFOS() << "!" << LL_ENDL;
    }
}

namespace
{
    static bool fatalWasCalled = false;
    struct FatalWasCalled: public std::runtime_error
    {
        FatalWasCalled(const std::string& what): std::runtime_error(what) {}
    };
    void fatalCall(const std::string& msg) { throw FatalWasCalled(msg); }
}

// Because we use LLError::setFatalFunction(fatalCall), any LL_ERRS call we
// issue will throw FatalWasCalled. But we want the test program to continue.
// So instead of writing:
// LL_ERRS("tag") << "some message" << LL_ENDL;
// write:
// CATCH(LL_ERRS("tag"), "some message");
#define CATCH(logcall, expr)                    \
    try                                         \
    {                                           \
        logcall << expr << LL_ENDL;             \
    }                                           \
    catch (const FatalWasCalled&)               \
    {                                           \
        fatalWasCalled = true;                  \
    }

namespace
{
    namespace
    {
        class TestRecorder : public LLError::Recorder
        {
        public:
            TestRecorder()
                {
                    showTime(false);
                }
            virtual ~TestRecorder()
                {}

            virtual void recordMessage(LLError::ELevel level,
                               const std::string& message)
            {
                mMessages.push_back(message);
            }

            int countMessages()         { return (int) mMessages.size(); }
            void clearMessages()        { mMessages.clear(); }

            std::string message(int n)
            {
                REQUIRE(n < countMessages());
                return mMessages[n];
            }

        private:
            typedef std::vector<std::string> MessageVector;
            MessageVector mMessages;
        };
    } // anonymous namespace

    struct ErrorTestData
    {
        LLError::RecorderPtr mRecorder;
        LLError::SettingsStoragePtr mPriorErrorSettings;

        ErrorTestData():
            mRecorder(new TestRecorder())
        {
            fatalWasCalled = false;

            mPriorErrorSettings = LLError::saveAndResetSettings();
            LLError::setDefaultLevel(LLError::LEVEL_DEBUG);
            LLError::setFatalFunction(fatalCall);
            LLError::addRecorder(mRecorder);
        }

        ~ErrorTestData()
        {
            LLError::removeRecorder(mRecorder);
            LLError::restoreSettings(mPriorErrorSettings);
        }

        int countMessages()
        {
            return std::dynamic_pointer_cast<TestRecorder>(mRecorder)->countMessages();
        }

        void clearMessages()
        {
            std::dynamic_pointer_cast<TestRecorder>(mRecorder)->clearMessages();
        }

        void setWantsTime(bool t)
            {
                std::dynamic_pointer_cast<TestRecorder>(mRecorder)->showTime(t);
            }

        void setWantsMultiline(bool t)
            {
                std::dynamic_pointer_cast<TestRecorder>(mRecorder)->showMultiline(t);
            }

        std::string message(int n)
        {
            return std::dynamic_pointer_cast<TestRecorder>(mRecorder)->message(n);
        }

        void require_message_count(int expectedCount)
        {
            REQUIRE(countMessages() == expectedCount);
        }

        std::string message_field(int msgnum, LogFieldIndex fieldnum)
        {
            REQUIRE(msgnum < countMessages());

            std::string msg(message(msgnum));

            std::string field_value;

            // find the start of the field; fields are separated by a single space
            size_t scan = 0;
            int on_field = 0;
            while ( scan < msg.length() && on_field < fieldnum )
            {
                // fields are delimited by one space
                if ( ' ' == msg[scan] )
                {
                    if ( on_field < FUNCTION_FIELD )
                    {
                        on_field++;
                    }
                    // except function, which may have embedded spaces so ends with " : "
                    else if (   ( on_field == FUNCTION_FIELD )
                             && ( ':' == msg[scan+1] && ' ' == msg[scan+2] )
                             )
                    {
                        on_field++;
                        scan +=2;
                    }
                }
                scan++;
            }
            size_t start_field = scan;
            size_t fieldlen = 0;
            if ( fieldnum < FUNCTION_FIELD )
            {
                fieldlen = msg.find(' ', start_field) - start_field;
            }
            else if ( fieldnum == FUNCTION_FIELD )
            {
                fieldlen = msg.find(" : ", start_field) - start_field;
            }
            else if ( MSG_FIELD == fieldnum ) // no delimiter, just everything to the end
            {
                fieldlen = msg.length() - start_field;
            }

            return msg.substr(start_field, fieldlen);
        }

        void require_message_field(int msgnum, LogFieldIndex fieldnum, const std::string& expectedText)
         {
             INFO("message " << msgnum << " field " << FieldName[fieldnum] << ": \"" << message(msgnum) << "\"");
             REQUIRE(message_field(msgnum, fieldnum) == expectedText);
         }

        void require_message_lacks(int n, const std::string& expectedText)
        {
            CAPTURE(n);
            REQUIRE_THAT(message(n), !Catch::Matchers::ContainsSubstring(expectedText));
        }
    };

    TEST_CASE_METHOD(ErrorTestData, "error: messages reach the recorder in order", "[error]")
        // basic test of output
    {
        LL_INFOS() << "test" << LL_ENDL;
        LL_INFOS() << "bob" << LL_ENDL;

        require_message_field(0, MSG_FIELD, "test");
        require_message_field(1, MSG_FIELD, "bob");
    }
}

namespace
{
    void writeSome()
    {
        LL_DEBUGS("WriteTag","AnotherTag") << "one" << LL_ENDL;
        LL_INFOS("WriteTag") << "two" << LL_ENDL;
        LL_WARNS("WriteTag") << "three" << LL_ENDL;
        CATCH(LL_ERRS("WriteTag"), "four");
    }
};

namespace
{
    TEST_CASE_METHOD(ErrorTestData, "error: the default level filters messages", "[error]")
        // messages are filtered based on default level
    {
#ifdef LL_DISABLE_DEBUG_LOGGING
        SKIP("Debug messages disabled");
#endif
        LLError::setDefaultLevel(LLError::LEVEL_DEBUG);
        writeSome();
        require_message_field(0, MSG_FIELD, "one");
        require_message_field(0, LEVEL_FIELD, "DEBUG");
        require_message_field(0, TAGS_FIELD, "#WriteTag#AnotherTag#");
        require_message_field(1, MSG_FIELD, "two");
        require_message_field(1, LEVEL_FIELD, "INFO");
        require_message_field(1, TAGS_FIELD, "#WriteTag#");
        require_message_field(2, MSG_FIELD, "three");
        require_message_field(2, LEVEL_FIELD, "WARNING");
        require_message_field(2, TAGS_FIELD, "#WriteTag#");
        require_message_field(3, MSG_FIELD, "four");
        require_message_field(3, LEVEL_FIELD, "ERROR");
        require_message_field(3, TAGS_FIELD, "#WriteTag#");
        require_message_count(4);

        LLError::setDefaultLevel(LLError::LEVEL_INFO);
        writeSome();
        require_message_field(4, MSG_FIELD, "two");
        require_message_field(5, MSG_FIELD, "three");
        require_message_field(6, MSG_FIELD, "four");
        require_message_count(7);

        LLError::setDefaultLevel(LLError::LEVEL_WARN);
        writeSome();
        require_message_field(7, MSG_FIELD, "three");
        require_message_field(8, MSG_FIELD, "four");
        require_message_count(9);

        LLError::setDefaultLevel(LLError::LEVEL_ERROR);
        writeSome();
        require_message_field(9, MSG_FIELD, "four");
        require_message_count(10);

        LLError::setDefaultLevel(LLError::LEVEL_NONE);
        writeSome();
        require_message_count(10);
    }

    TEST_CASE_METHOD(ErrorTestData, "error: the output names the level of each message", "[error]")
        // error type string in output
    {
#ifdef LL_DISABLE_DEBUG_LOGGING
        SKIP("Debug messages disabled");
#endif
        writeSome();
        require_message_field(0, LEVEL_FIELD, "DEBUG");
        require_message_field(1, LEVEL_FIELD, "INFO");
        require_message_field(2, LEVEL_FIELD, "WARNING");
        require_message_field(3, LEVEL_FIELD, "ERROR");
        require_message_count(4);
    }

    TEST_CASE_METHOD(ErrorTestData, "error: abbreviateFile strips the source root and leaves other paths", "[error]")
        // file abbreviation
    {
        std::string prev, abbreviateFile = __FILE__;
        do
        {
            prev = abbreviateFile;
            abbreviateFile = LLError::abbreviateFile(abbreviateFile);
            // __FILE__ is assumed to end with
            // indra/llcommon/tests/llerror_test.cpp. This test used to call
            // abbreviateFile() exactly once, then check below whether it
            // still contained the string 'indra'. That fails if the FIRST
            // part of the pathname also contains indra! Certain developer
            // machine images put local directory trees under
            // /ngi-persist/indra, which is where we observe the problem. So
            // now, keep calling abbreviateFile() until it returns its
            // argument unchanged, THEN check.
        } while (abbreviateFile != prev);

        CHECK_THAT(abbreviateFile,
            Catch::Matchers::EndsWith("llcommon/tests/llerror_test.cpp"));
        CHECK_THAT(abbreviateFile, !Catch::Matchers::ContainsSubstring("indra"));

        std::string someFile =
#if LL_WINDOWS
            "C:/amy/bob/cam.cpp"
#else
            "/amy/bob/cam.cpp"
#endif
            ;
        std::string someAbbreviation = LLError::abbreviateFile(someFile);

        CHECK(someAbbreviation == someFile);
    }
}

namespace
{
    std::string locationString(int line)
    {
        std::ostringstream location;
        location << LLError::abbreviateFile(__FILE__)
                 << "(" << line << ")";

        return location.str();
    }

    std::string writeReturningLocation()
    {
        LL_INFOS() << "apple" << LL_ENDL;   int this_line = __LINE__;
        return locationString(this_line);
    }

    void writeReturningLocationAndFunction(std::string& location, std::string& function)
    {
        LL_INFOS() << "apple" << LL_ENDL;   int this_line = __LINE__;
        location = locationString(this_line);
        function = __FUNCTION__;
    }

    std::string errorReturningLocation()
    {
        int this_line = __LINE__;   CATCH(LL_ERRS(), "die");
        return locationString(this_line);
    }
}

/* The following helper functions and class members all log a simple message
    from some particular function scope.  Each function takes a bool argument
    that indicates if it should log its own name or not (in the manner that
    existing log messages often do.)  The functions all return their C++
    name so that test can be substantial mechanized.
 */

std::string logFromGlobal(bool id)
{
    LL_INFOS() << (id ? "logFromGlobal: " : "") << "hi" << LL_ENDL;
    return "logFromGlobal";
}

static std::string logFromStatic(bool id)
{
    LL_INFOS() << (id ? "logFromStatic: " : "") << "hi" << LL_ENDL;
    return "logFromStatic";
}

namespace
{
    std::string logFromAnon(bool id)
    {
        LL_INFOS() << (id ? "logFromAnon: " : "") << "hi" << LL_ENDL;
        return "logFromAnon";
    }
}

namespace Foo {
    std::string logFromNamespace(bool id)
    {
        LL_INFOS() << (id ? "Foo::logFromNamespace: " : "") << "hi" << LL_ENDL;
        //return "Foo::logFromNamespace";
            // there is no standard way to get the namespace name, hence
            // we won't be testing for it
        return "logFromNamespace";
    }
}

namespace
{
    class ClassWithNoLogType {
    public:
        std::string logFromMember(bool id)
        {
            LL_INFOS() << (id ? "ClassWithNoLogType::logFromMember: " : "") << "hi" << LL_ENDL;
            return "ClassWithNoLogType::logFromMember";
        }
        static std::string logFromStatic(bool id)
        {
            LL_INFOS() << (id ? "ClassWithNoLogType::logFromStatic: " : "") << "hi" << LL_ENDL;
            return "ClassWithNoLogType::logFromStatic";
        }
    };

    class ClassWithLogType {
        LOG_CLASS(ClassWithLogType);
    public:
        std::string logFromMember(bool id)
        {
            LL_INFOS() << (id ? "ClassWithLogType::logFromMember: " : "") << "hi" << LL_ENDL;
            return "ClassWithLogType::logFromMember";
        }
        static std::string logFromStatic(bool id)
        {
            LL_INFOS() << (id ? "ClassWithLogType::logFromStatic: " : "") << "hi" << LL_ENDL;
            return "ClassWithLogType::logFromStatic";
        }
    };

    std::string logFromNamespace(bool id) { return Foo::logFromNamespace(id); }
    std::string logFromClassWithLogTypeMember(bool id) { ClassWithLogType c; return c.logFromMember(id); }
    std::string logFromClassWithLogTypeStatic(bool id) { return ClassWithLogType::logFromStatic(id); }

    void require_has(const std::string& message,
        const std::string& actual, const std::string& expected)
    {
        std::string::size_type n1 = actual.find(expected);
        if (n1 == std::string::npos)
        {
            FAIL(message << ": " << "expected to find a copy of '" << expected
                 << "' in actual '" << actual << "'");
        }
    }

    typedef std::string (*LogFromFunction)(bool);
    void require_log_name(LLError::RecorderPtr recorder, LogFromFunction f,
        const std::string& class_name = "")
    {
        std::dynamic_pointer_cast<TestRecorder>(recorder)->clearMessages();
        std::string name = f(false);
        f(true);

        std::string messageWithoutName = std::dynamic_pointer_cast<TestRecorder>(recorder)->message(0);
        std::string messageWithName = std::dynamic_pointer_cast<TestRecorder>(recorder)->message(1);

        require_has(name + " logged without name",
            messageWithoutName, name);
        require_has(name + " logged with name",
            messageWithName, name);

        if (!class_name.empty())
        {
            require_has(name + " logged without name",
                messageWithoutName, class_name);
            require_has(name + " logged with name",
                messageWithName, class_name);
        }
    }
}

namespace
{
    void writeMsgNeedsEscaping()
    {
        LL_DEBUGS("WriteTag") << "backslash\\" << LL_ENDL;
        LL_INFOS("WriteTag") << "newline\nafternewline" << LL_ENDL;
        LL_WARNS("WriteTag") << "return\rafterreturn" << LL_ENDL;

        LL_DEBUGS("WriteTag") << "backslash\\backslash\\" << LL_ENDL;
        LL_INFOS("WriteTag") << "backslash\\newline\nanothernewline\nafternewline" << LL_ENDL;
        LL_WARNS("WriteTag") << "backslash\\returnnewline\r\n\\afterbackslash" << LL_ENDL;
    }
};

namespace
{
    TEST_CASE_METHOD(ErrorTestData, "error: multiline output leaves backslashes and line breaks unescaped", "[error]")
        // backslash, return, and newline are not escaped with backslashes
    {
#ifdef LL_DISABLE_DEBUG_LOGGING
        SKIP("Debug messages disabled");
#endif
        LLError::setDefaultLevel(LLError::LEVEL_DEBUG);
        setWantsMultiline(true);
        writeMsgNeedsEscaping(); // but should not be now
        require_message_field(0, MSG_FIELD, "backslash\\");
        require_message_field(1, MSG_FIELD, "newline\nafternewline");
        require_message_field(2, MSG_FIELD, "return\rafterreturn");
        require_message_field(3, MSG_FIELD, "backslash\\backslash\\");
        require_message_field(4, MSG_FIELD, "backslash\\newline\nanothernewline\nafternewline");
        require_message_field(5, MSG_FIELD, "backslash\\returnnewline\r\n\\afterbackslash");
        require_message_count(6);
    }
}

namespace
{
    //  class/function information in output
    TEST_CASE_METHOD(ErrorTestData, "error: messages name the function and class they came from", "[error]")
    {
        require_log_name(mRecorder, logFromGlobal);
        require_log_name(mRecorder, logFromStatic);
        require_log_name(mRecorder, logFromAnon);
        require_log_name(mRecorder, logFromNamespace);
        require_log_name(mRecorder, logFromClassWithLogTypeMember, "ClassWithLogType");
        require_log_name(mRecorder, logFromClassWithLogTypeStatic, "ClassWithLogType");
    }
}

namespace
{
    std::string innerLogger()
    {
        LL_INFOS() << "inside" << LL_ENDL;
        return "moo";
    }

    std::string outerLogger()
    {
        LL_INFOS() << "outside(" << innerLogger() << ")" << LL_ENDL;
        return "bar";
    }

    class LogWhileLogging
    {
    public:
        void print(std::ostream& out) const
        {
            LL_INFOS() << "logging" << LL_ENDL;
            out << "baz";
        }
    };

    std::ostream& operator<<(std::ostream& out, const LogWhileLogging& l)
        { l.print(out); return out; }

    void metaLogger()
    {
        LogWhileLogging l;
        LL_INFOS() << "meta(" << l << ")" << LL_ENDL;
    }

}

namespace
{
    // handle nested logging
    TEST_CASE_METHOD(ErrorTestData, "error: logging while building a message logs both", "[error]")
    {
        outerLogger();
        require_message_field(0, MSG_FIELD, "inside");
        require_message_field(1, MSG_FIELD, "outside(moo)");
        require_message_count(2);

        metaLogger();
        require_message_field(2, MSG_FIELD, "logging");
        require_message_field(3, MSG_FIELD, "meta(baz)");
        require_message_count(4);
    }

    // special handling of LL_ERRS() calls
    TEST_CASE_METHOD(ErrorTestData, "error: LL_ERRS logs its location and calls the fatal function", "[error]")
    {
        std::string location = errorReturningLocation();

        require_message_field(0, LOCATION_FIELD, location);
        require_message_field(0, MSG_FIELD, "die");
        require_message_count(1);

        CHECK(fatalWasCalled);
    }
}

namespace
{
    std::string roswell()
    {
        return "1947-07-08T03:04:05Z";
    }

    void ufoSighting()
    {
        LL_INFOS() << "ufo" << LL_ENDL;
    }
}

namespace
{
    // time in output (for recorders that need it)
    TEST_CASE_METHOD(ErrorTestData, "error: a recorder gets the time only when it asks for it", "[error]")
    {
        LLError::setTimeFunction(roswell);

        setWantsTime(false);
        ufoSighting();
        require_message_field(0, MSG_FIELD, "ufo");
        require_message_lacks(0, roswell());

        setWantsTime(true);
        ufoSighting();
        require_message_field(1, MSG_FIELD, "ufo");
        require_message_field(1, TIME_FIELD, roswell());
    }

    // output order
    TEST_CASE_METHOD(ErrorTestData, "error: fields come in time level tags location function message order", "[error]")
    {
        LLError::setTimeFunction(roswell);
        setWantsTime(true);

        std::string location,
                    function;
        writeReturningLocationAndFunction(location, function);

        CHECK(message(0) ==
                      roswell() + " INFO " + "# " /* no tag */ + location + " " + function + " : " + "apple");
    }

    // multiple recorders
    TEST_CASE_METHOD(ErrorTestData, "error: every recorder gets each message with its own time setting", "[error]")
    {
        LLError::RecorderPtr altRecorder(new TestRecorder());
        LLError::addRecorder(altRecorder);

        LL_INFOS() << "boo" << LL_ENDL;

        require_message_field(0, MSG_FIELD, "boo");
        CHECK(std::dynamic_pointer_cast<TestRecorder>(altRecorder)->countMessages() == 1);
        CHECK_THAT(std::dynamic_pointer_cast<TestRecorder>(altRecorder)->message(0), Catch::Matchers::ContainsSubstring("boo"));

        LLError::setTimeFunction(roswell);

        LLError::RecorderPtr anotherRecorder(new TestRecorder());
        std::dynamic_pointer_cast<TestRecorder>(anotherRecorder)->showTime(true);
        LLError::addRecorder(anotherRecorder);

        LL_INFOS() << "baz" << LL_ENDL;

        std::string when = roswell();

        require_message_lacks(1, when);
        CHECK(std::dynamic_pointer_cast<TestRecorder>(altRecorder)->countMessages() == 2);
        CHECK_THAT(std::dynamic_pointer_cast<TestRecorder>(altRecorder)->message(1), !Catch::Matchers::ContainsSubstring(when));
        CHECK(std::dynamic_pointer_cast<TestRecorder>(anotherRecorder)->countMessages() == 1);
        CHECK_THAT(std::dynamic_pointer_cast<TestRecorder>(anotherRecorder)->message(0), Catch::Matchers::ContainsSubstring(when));

        LLError::removeRecorder(altRecorder);
        LLError::removeRecorder(anotherRecorder);
    }
}

class TestAlpha
{
    LOG_CLASS(TestAlpha);
public:
    static void doDebug()   { LL_DEBUGS() << "add dice" << LL_ENDL; }
    static void doInfo()    { LL_INFOS()  << "any idea" << LL_ENDL; }
    static void doWarn()    { LL_WARNS()  << "aim west" << LL_ENDL; }
    static void doError()   { CATCH(LL_ERRS(), "ate eels"); }
    static void doAll() { doDebug(); doInfo(); doWarn(); doError(); }
};

class TestBeta
{
    LOG_CLASS(TestBeta);
public:
    static void doDebug()   { LL_DEBUGS() << "bed down" << LL_ENDL; }
    static void doInfo()    { LL_INFOS()  << "buy iron" << LL_ENDL; }
    static void doWarn()    { LL_WARNS()  << "bad word" << LL_ENDL; }
    static void doError()   { CATCH(LL_ERRS(), "big easy"); }
    static void doAll() { doDebug(); doInfo(); doWarn(); doError(); }
};

namespace
{
    // filtering by class
    TEST_CASE_METHOD(ErrorTestData, "error: a class level overrides the default level", "[error]")
    {
        LLError::setDefaultLevel(LLError::LEVEL_WARN);
        LLError::setClassLevel("TestBeta", LLError::LEVEL_INFO);

        TestAlpha::doAll();
        TestBeta::doAll();

        require_message_field(0, MSG_FIELD, "aim west");
        require_message_field(1, MSG_FIELD, "ate eels");
        require_message_field(2, MSG_FIELD, "buy iron");
        require_message_field(3, MSG_FIELD, "bad word");
        require_message_field(4, MSG_FIELD, "big easy");
        require_message_count(5);
    }

    // filtering by function, and that it will override class filtering
    TEST_CASE_METHOD(ErrorTestData, "error: a function level overrides the class level", "[error]")
    {
        LLError::setDefaultLevel(LLError::LEVEL_DEBUG);
        LLError::setClassLevel("TestBeta", LLError::LEVEL_WARN);
        LLError::setFunctionLevel("TestBeta::doInfo", LLError::LEVEL_DEBUG);
        LLError::setFunctionLevel("TestBeta::doError", LLError::LEVEL_NONE);

        TestBeta::doAll();
        require_message_field(0, MSG_FIELD, "buy iron");
        require_message_field(1, MSG_FIELD, "bad word");
        require_message_count(2);
    }

    // filtering by file
    // and that it is overridden by both class and function filtering
    TEST_CASE_METHOD(ErrorTestData, "error: a file level yields to class and function levels", "[error]")
    {
        LLError::setDefaultLevel(LLError::LEVEL_DEBUG);
        LLError::setFileLevel(LLError::abbreviateFile(__FILE__),
                                    LLError::LEVEL_WARN);
        LLError::setClassLevel("TestAlpha", LLError::LEVEL_INFO);
        LLError::setFunctionLevel("TestAlpha::doError",
                                    LLError::LEVEL_NONE);
        LLError::setFunctionLevel("TestBeta::doError",
                                    LLError::LEVEL_NONE);

        TestAlpha::doAll();
        TestBeta::doAll();
        require_message_field(0, MSG_FIELD, "any idea");
        require_message_field(1, MSG_FIELD, "aim west");
        require_message_field(2, MSG_FIELD, "bad word");
        require_message_count(3);
    }

    // proper cached, efficient lookup of filtering
    TEST_CASE_METHOD(ErrorTestData, "error: filter lookups are cached until the levels change", "[error]")
    {
        LLError::setDefaultLevel(LLError::LEVEL_NONE);

        TestAlpha::doInfo();
        require_message_count(0);
        CHECK(LLError::shouldLogCallCount() == 1);
        TestAlpha::doInfo();
        require_message_count(0);
        CHECK(LLError::shouldLogCallCount() == 1);

        LLError::setClassLevel("TestAlpha", LLError::LEVEL_DEBUG);
        TestAlpha::doInfo();
        require_message_count(1);
        CHECK(LLError::shouldLogCallCount() == 2);
        TestAlpha::doInfo();
        require_message_count(2);
        CHECK(LLError::shouldLogCallCount() == 2);

        LLError::setClassLevel("TestAlpha", LLError::LEVEL_WARN);
        TestAlpha::doInfo();
        require_message_count(2);
        CHECK(LLError::shouldLogCallCount() == 3);
        TestAlpha::doInfo();
        require_message_count(2);
        CHECK(LLError::shouldLogCallCount() == 3);
    }

    // configuration from LLSD
    TEST_CASE_METHOD(ErrorTestData, "error: configure sets levels from LLSD and can be run again", "[error]")
    {
        LLSD config;
        config["print-location"] = true;
        config["default-level"] = "DEBUG";

        LLSD set1;
        set1["level"] = "WARN";
        set1["files"][0] = LLError::abbreviateFile(__FILE__);

        LLSD set2;
        set2["level"] = "INFO";
        set2["classes"][0] = "TestAlpha";

        LLSD set3;
        set3["level"] = "NONE";
        set3["functions"][0] = "TestAlpha::doError";
        set3["functions"][1] = "TestBeta::doError";

        config["settings"][0] = set1;
        config["settings"][1] = set2;
        config["settings"][2] = set3;

        LLError::configure(config);

        TestAlpha::doAll();
        TestBeta::doAll();
        require_message_field(0, MSG_FIELD, "any idea");
        require_message_field(1, MSG_FIELD, "aim west");
        require_message_field(2, MSG_FIELD, "bad word");
        require_message_count(3);

        // make sure reconfiguring works
        LLSD config2;
        config2["default-level"] = "WARN";

        LLError::configure(config2);

        TestAlpha::doAll();
        TestBeta::doAll();
        require_message_field(3, MSG_FIELD, "aim west");
        require_message_field(4, MSG_FIELD, "ate eels");
        require_message_field(5, MSG_FIELD, "bad word");
        require_message_field(6, MSG_FIELD, "big easy");
        require_message_count(7);
    }
}

namespace
{
    TEST_CASE_METHOD(ErrorTestData, "error: single-line output escapes backslashes and line breaks", "[error]")
        // backslash, return, and newline are escaped with backslashes
    {
#ifdef LL_DISABLE_DEBUG_LOGGING
        SKIP("Debug messages disabled");
#endif
        LLError::setDefaultLevel(LLError::LEVEL_DEBUG);
        writeMsgNeedsEscaping();
        require_message_field(0, MSG_FIELD, "backslash\\\\");
        require_message_field(1, MSG_FIELD, "newline\\nafternewline");
        require_message_field(2, MSG_FIELD, "return\\rafterreturn");
        require_message_field(3, MSG_FIELD, "backslash\\\\backslash\\\\");
        require_message_field(4, MSG_FIELD, "backslash\\\\newline\\nanothernewline\\nafternewline");
        require_message_field(5, MSG_FIELD, "backslash\\\\returnnewline\\r\\n\\\\afterbackslash");
        require_message_count(6);
    }
}

namespace
{
    std::string writeTagWithSpaceReturningLocation()
    {
        int this_line = __LINE__; CATCH(LL_DEBUGS("Write Tag"), "not allowed");
        return locationString(this_line);
    }
};

namespace
{
    TEST_CASE_METHOD(ErrorTestData, "error: a space in a tag is an error", "[error]")
        // space character is not allowed in a tag
    {
#ifdef LL_DISABLE_DEBUG_LOGGING
        SKIP("Debug messages disabled");
#endif
        LLError::setDefaultLevel(LLError::LEVEL_DEBUG);
        fatalWasCalled = false;

        std::string location = writeTagWithSpaceReturningLocation();
        std::string expected = "Space is not allowed in a log tag at " + location;
        require_message_field(0, LEVEL_FIELD, "ERROR");
        require_message_field(0, MSG_FIELD, expected);
        CHECK(fatalWasCalled);
    }
}

/* Tests left:
    handling of classes without LOG_CLASS

    live update of filtering from file

    syslog recorder
    file recorder
    cerr/stderr recorder
    fixed buffer recorder
    windows recorder

    mutex use when logging (?)
    strange careful about to crash handling (?)
*/
