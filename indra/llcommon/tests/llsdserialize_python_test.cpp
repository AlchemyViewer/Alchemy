/**
 * @file llsdserialize_python_test.cpp
 * @date 2006-04
 * @brief LLSDSerialize round trips through Python's llsd package
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

// llsdserialize_test.cpp's group that needs a Python interpreter, apart so
// the rest can run without one.

#include "linden_common.h"

#if LL_WINDOWS
#include <process.h>
#include <io.h>
#else
#include <errno.h>
#include <sys/wait.h>
#include "llprocess.h"
#endif

#include "llsd.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llmemorystream.h"
#include "llstring.h"

#include "hexdump.h"
#include <catch2/catch_test_macros.hpp>
#include "altestfailure.h"
#include "altestmatchers.h"
#include "../test/namedtempfile.h"
#include "StringVec.h"
#include <functional>

typedef std::function<void(const LLSD& data, std::ostream& str)> FormatterFunction;
typedef std::function<bool(std::istream& istr, LLSD& data, llssize max_bytes)> ParserFunction;

namespace
{
    // helper for TestPythonCompatible
    static std::string import_llsd("import os.path\n"
                                   "import sys\n"
                                   "import llsd\n");

    // helper for TestPythonCompatible
    template <typename CONTENT, typename... ARGS>
    static void python_expect(const std::string& desc, const CONTENT& script, int expect=0,
                              ARGS&&... args)
    {
        auto PYTHON(LLStringUtil::getenv("PYTHON"));
        UNSCOPED_INFO("Set $PYTHON to the Python interpreter");
        REQUIRE_FALSE(PYTHON.empty());

        INFO(desc << " script");
        NamedTempFile scriptfile("py", script);

#if LL_WINDOWS
        std::string q("\"");
        std::string qPYTHON(q + PYTHON + q);
        std::string qscript(q + scriptfile.getPath().string() + q);
        int rc = (int)_spawnl(_P_WAIT, PYTHON.c_str(), qPYTHON.c_str(), qscript.c_str(),
                         std::forward<ARGS>(args)..., NULL);
        if (rc == -1)
        {
            char buffer[256];
            strerror_s(buffer, errno); // C++ can infer the buffer size!  :-O
            FAIL("couldn't run Python: " << buffer);
        }
        else
        {
            REQUIRE(rc == expect);
        }

#else  // LL_DARWIN, LL_LINUX
        LLProcess::Params params;
        params.executable = PYTHON;
        params.args.add(scriptfile.getPath().string());
        for (const std::string& arg : StringVec{ std::forward<ARGS>(args)... })
        {
            params.args.add(arg);
        }
        LLProcessPtr py(LLProcess::create(params));
        UNSCOPED_INFO("couldn't launch");
        REQUIRE(bool(py));
        // Implementing timeout would mean messing with alarm() and
        // catching SIGALRM... later maybe...
        int status(0);
        if (waitpid(py->getProcessID(), &status, 0) == -1)
        {
            int waitpid_errno(errno);
            // Something else in the process reaped the child first -- a
            // SIGCHLD handler, as LLProcess allows for -- and took the exit
            // code this helper exists to check: the case cannot say, so it
            // skips rather than passing unchecked.
            if (waitpid_errno == ECHILD)
            {
                SKIP(desc << " script's exit code was taken by another waitpid()");
            }
            FAIL("couldn't retrieve rc: waitpid() failed with errno " << waitpid_errno);
        }
        else
        {
            if (WIFEXITED(status))
            {
                int rc(WEXITSTATUS(status));
                REQUIRE(rc == expect);
            }
            else if (WIFSIGNALED(status))
            {
                FAIL("terminated by signal " << WTERMSIG(status));
            }
            else
            {
                FAIL("produced impossible status " << status);
            }
        }
#endif
    }

    // helper for TestPythonCompatible
    template <typename CONTENT, typename... ARGS>
    static void python(const std::string& desc, const CONTENT& script, ARGS&&... args)
    {
        // plain python() expects rc 0
        python_expect(desc, script, 0, std::forward<ARGS>(args)...);
    }

    struct TestPythonCompatible
    {
        TestPythonCompatible() {}
        ~TestPythonCompatible() {}
    };

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: python_expect() sees a nonzero exit code", "[LLSD_serialize_Python_compatibility]")
    {
        python_expect("hello",
                      "import sys\n"
                      "sys.exit(17)\n",
                      17);                 // expect nonzero rc
    }

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: python() runs a script in a NamedTempFile", "[LLSD_serialize_Python_compatibility]")
    {
        python("platform",
               "import sys\n"
               "print('Running on', sys.platform)\n");
    }

    // helper for the "to Python" cases
    static void writeLLSDArray(const FormatterFunction& serialize,
                               std::ostream& out, const LLSD& array)
    {
        for (const LLSD& item: llsd::inArray(array))
        {
            // It's important to delimit the entries in this file somehow
            // because, although Python's llsd.parse() can accept a file
            // stream, the XML parser expects EOF after a single outer element
            // -- it doesn't just stop. So we must extract a sequence of bytes
            // strings from the file. But since one of the serialization
            // formats we want to test is binary, we can't pick any single
            // byte value as a delimiter! Use a binary integer length prefix
            // instead.
            std::ostringstream buffer;
            serialize(item, buffer);
            auto buffstr{ buffer.str() };
            int bufflen{ static_cast<int>(buffstr.length()) };
            out.write(reinterpret_cast<const char*>(&bufflen), sizeof(bufflen));
            LL_DEBUGS() << "Wrote length: "
                        << LL::hexdump(reinterpret_cast<const char*>(&bufflen),
                                   sizeof(bufflen))
                        << LL_ENDL;
            out.write(buffstr.c_str(), buffstr.length());
            LL_DEBUGS() << "Wrote data:   "
                        << LL::hexmix(buffstr.c_str(), buffstr.length())
                        << LL_ENDL;
        }
    }

    // helper for the "to Python" cases
    static void toPythonUsing(const std::string& desc,
                              const FormatterFunction& serialize)
    {
        LLSD cdata(llsd::array(17, 3.14,
                               "This string\n"
                               "has several\n"
                               "lines."));

        const char pydata[] =
            "def verify(iterable):\n"
            "    it = iter(iterable)\n"
            "    assert next(it) == 17\n"
            "    assert abs(next(it) - 3.14) < 0.01\n"
            "    assert next(it) == '''\\\n"
            "This string\n"
            "has several\n"
            "lines.'''\n"
            "    try:\n"
            "        next(it)\n"
            "    except StopIteration:\n"
            "        pass\n"
            "    else:\n"
            "        raise AssertionError('Too many data items')\n";

        // Create an llsdXXXXXX file containing 'data' serialized per
        // FormatterFunction.
        NamedTempFile file("llsd",
                           // NamedTempFile's function constructor
                           // takes a callable. To this callable it passes the
                           // std::ostream with which it's writing the
                           // NamedTempFile.
                           [serialize, cdata]
                           (std::ostream& out)
                           { writeLLSDArray(serialize, out, cdata); });

        // 'debug' starts empty because it's intended as an output file
        NamedTempFile debug("debug", "");

        try
        {
            python("read C++ " + desc,
                   [&](std::ostream& out){ out <<
                   import_llsd <<
                   "from functools import partial\n"
                   "import io\n"
                   "import struct\n"
                   "lenformat = struct.Struct('i')\n"
                   "def parse_each(inf):\n"
                   "    for rawlen in iter(partial(inf.read, lenformat.size), b''):\n"
                   "        print('Read length:', ''.join(('%02x' % b) for b in rawlen),\n"
                   "              file=debug)\n"
                   "        len = lenformat.unpack(rawlen)[0]\n"
                   // Since llsd.parse() has no max_bytes argument, instead of
                   // passing the input stream directly to parse(), read the item
                   // into a distinct bytes object and parse that.
                   "        data = inf.read(len)\n"
                   "        print('Read data:  ', repr(data), file=debug)\n"
                   "        try:\n"
                   "            frombytes = llsd.parse(data)\n"
                   "        except llsd.LLSDParseError as err:\n"
                   "            print(f'*** {err}')\n"
                   "            print(f'Bad content:\\n{data!r}')\n"
                   "            raise\n"
                   // Also try parsing from a distinct stream.
                   "        stream = io.BytesIO(data)\n"
                   "        fromstream = llsd.parse(stream)\n"
                   "        assert frombytes == fromstream\n"
                   "        yield frombytes\n"
                   << pydata <<
                   // Don't forget raw-string syntax for Windows pathnames.
                   "debug = open(r'" << debug.getPath().string() << "', 'w')\n"
                   "verify(parse_each(open(r'" << file.getPath().string() << "', 'rb')))\n";});
        }
        catch (const ll_test::Failure&)
        {
            LL_DEBUGS() << "Script debug output:" << LL_ENDL;
            debug.peep_log();
            throw;
        }
    }

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: to Python using LLSDSerialize::serialize(LLSD_XML)", "[LLSD_serialize_Python_compatibility]")
    {
        toPythonUsing("LLSD_XML",
                      [](const LLSD& sd, std::ostream& out)
        { LLSDSerialize::serialize(sd, out, LLSDSerialize::LLSD_XML); });
    }

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: to Python using LLSDSerialize::serialize(LLSD_NOTATION)", "[LLSD_serialize_Python_compatibility]")
    {
        toPythonUsing("LLSD_NOTATION",
                      [](const LLSD& sd, std::ostream& out)
        { LLSDSerialize::serialize(sd, out, LLSDSerialize::LLSD_NOTATION); });
    }

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: to Python using LLSDSerialize::serialize(LLSD_BINARY)", "[LLSD_serialize_Python_compatibility]")
    {
        toPythonUsing("LLSD_BINARY",
                      [](const LLSD& sd, std::ostream& out)
        { LLSDSerialize::serialize(sd, out, LLSDSerialize::LLSD_BINARY); });
    }

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: to Python using LLSDSerialize::toXML()", "[LLSD_serialize_Python_compatibility]")
    {
        toPythonUsing("toXML()", LLSDSerialize::toXML);
    }

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: to Python using LLSDSerialize::toNotation()", "[LLSD_serialize_Python_compatibility]")
    {
        toPythonUsing("toNotation()", LLSDSerialize::toNotation);
    }

/*==========================================================================*|
    template<> template<>
    void TestPythonCompatibleObject::test<8>()
    {
        set_test_name("to Python using LLSDSerialize::toBinary()");
        // We don't expect this to work because, without a header,
        // llsd.parse() will assume notation rather than binary.
        toPythonUsing("toBinary()", LLSDSerialize::toBinary);
    }
|*==========================================================================*/

    // helper for the "from Python" cases
    static bool itemFromStream(std::istream& istr, LLSD& item, const ParserFunction& parse)
    {
        // reset the output value for debugging clarity
        item.clear();
        // We use an int length prefix as a foolproof delimiter even for
        // binary serialized streams.
        int length{ 0 };
        istr.read(reinterpret_cast<char*>(&length), sizeof(length));
//      return parse(istr, item, length);
        // Sadly, as of 2022-12-01 it seems we can't really trust our LLSD
        // parsers to honor max_bytes: this test works better when we read
        // each item into its own distinct LLMemoryStream, instead of passing
        // the original istr with a max_bytes constraint.
        std::vector<U8> buffer(length);
        istr.read(reinterpret_cast<char*>(buffer.data()), length);
        LLMemoryStream stream(buffer.data(), length);
        return parse(stream, item, length);
    }

    // helper for the "from Python" cases
    static void fromPythonUsing(const std::string& pyformatter,
                                const ParserFunction& parse=
                                [](std::istream& istr, LLSD& data, llssize max_bytes)
                                { return LLSDSerialize::deserialize(data, istr, max_bytes); })
    {
        // Create an empty data file. This is just a placeholder for our
        // script to write into. Create it to establish a unique name that
        // we know.
        NamedTempFile file("llsd", "");

        python("Python " + pyformatter,
               [&](std::ostream& out){ out <<
               import_llsd <<
               "import struct\n"
               "lenformat = struct.Struct('i')\n"
               "DATA = [\n"
               "    17,\n"
               "    3.14,\n"
               "    '''\\\n"
               "This string\n"
               "has several\n"
               "lines.''',\n"
               "]\n"
               // Don't forget raw-string syntax for Windows pathnames.
               // N.B. Using 'print' implicitly adds newlines.
               "with open(r'" << ll_convert<std::string>(file.getPath().u8string()).c_str() << "', 'wb') as f:\n"
               "    for item in DATA:\n"
               "        serialized = llsd." << pyformatter << "(item)\n"
               "        f.write(lenformat.pack(len(serialized)))\n"
               "        f.write(serialized)\n";});

        llifstream inf(file.getPath());
        LLSD item;
        try
        {
            UNSCOPED_INFO("reading the Integer");
            REQUIRE(itemFromStream(inf, item, parse));
            REQUIRE(item.asInteger() == 17);
            UNSCOPED_INFO("reading the Real");
            REQUIRE(itemFromStream(inf, item, parse));
            REQUIRE_THAT(item.asReal(), ll_test::WithinBits(3.14, 7)); // 7 bits ~= 0.01
            UNSCOPED_INFO("reading the String");
            REQUIRE(itemFromStream(inf, item, parse));
            REQUIRE(item.asString() ==
                          "This string\n"
                          "has several\n"
                          "lines.");
        }
        catch (const ll_test::Failure&)
        {
            std::cout << "for item = " << item << std::endl;
            throw;
        }
    }

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: from Python XML using LLSDSerialize::deserialize()", "[LLSD_serialize_Python_compatibility]")
    {
        fromPythonUsing("format_xml");
    }

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: from Python notation using LLSDSerialize::deserialize()", "[LLSD_serialize_Python_compatibility]")
    {
        fromPythonUsing("format_notation");
    }

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: from Python binary using LLSDSerialize::deserialize()", "[LLSD_serialize_Python_compatibility]")
    {
        fromPythonUsing("format_binary");
    }

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: from Python XML using fromXML()", "[LLSD_serialize_Python_compatibility]")
    {
        // fromXML()'s optional 3rd param isn't max_bytes, it's emit_errors
        fromPythonUsing("format_xml",
                        [](std::istream& istr, LLSD& data, llssize)
                        { return LLSDSerialize::fromXML(data, istr) > 0; });
    }

    TEST_CASE_METHOD(TestPythonCompatible, "LLSD serialize Python compatibility: from Python notation using fromNotation()", "[LLSD_serialize_Python_compatibility]")
    {
        fromPythonUsing("format_notation",
                        [](std::istream& istr, LLSD& data, llssize max_bytes)
                        { return LLSDSerialize::fromNotation(data, istr, max_bytes) > 0; });
    }

/*==========================================================================*|
    template<> template<>
    void TestPythonCompatibleObject::test<13>()
    {
        set_test_name("from Python binary using fromBinary()");
        // We don't expect this to work because format_binary() emits a
        // header, but fromBinary() won't recognize a header.
        fromPythonUsing("format_binary",
                        [](std::istream& istr, LLSD& data, llssize max_bytes)
                        { return LLSDSerialize::fromBinary(data, istr, max_bytes) > 0; });
    }
|*==========================================================================*/
}
