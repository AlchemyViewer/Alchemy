/**
 * @file   llheteromap_test.cpp
 * @author Nat Goodspeed
 * @date   2016-10-12
 * @brief  Test for llheteromap.
 *
 * $LicenseInfo:firstyear=2016&license=viewerlgpl$
 * Copyright (c) 2016, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "llheteromap.h"
// STL headers
#include <set>
// std headers
// external library headers

// (pacify clang)
static std::ostream& operator<<(std::ostream& out, const std::set<std::string>& strset);
// other Linden headers
#include <catch2/catch_test_macros.hpp>

static std::string clog;
static std::set<std::string> dlog;

// want to be able to use ensure_equals() on a set<string>
static std::ostream& operator<<(std::ostream& out, const std::set<std::string>& strset)
{
    out << '{';
    const char* delim = "";
    for (std::set<std::string>::const_iterator si(strset.begin()), se(strset.end());
         si != se; ++si)
    {
        out << delim << '"' << *si << '"';
        delim = ", ";
    }
    out << '}';
    return out;
}

namespace
{

// unrelated test classes
struct Chalk
{
    int dummy;
    std::string name;

    Chalk():
        dummy(0)
    {
        clog.append("a");
    }

    ~Chalk()
    {
        dlog.insert("a");
    }

private:
    Chalk(const Chalk&);            // no implementation
};

struct Cheese
{
    std::string name;

    Cheese()
    {
        clog.append("e");
    }

    ~Cheese()
    {
        dlog.insert("e");
    }

private:
    Cheese(const Cheese&);          // no implementation
};

struct Chowdah
{
    char displace[17];
    std::string name;

    Chowdah()
    {
        displace[0] = '\0';
        clog.append("o");
    }

    ~Chowdah()
    {
        dlog.insert("o");
    }

private:
    Chowdah(const Chowdah&);        // no implementation
};

} // anonymous namespace

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    struct llheteromap_data
    {
        llheteromap_data()
        {
            clog.erase();
            dlog.clear();
        }
    };

    TEST_CASE_METHOD(llheteromap_data, "llheteromap: obtain creates each type once and the map destroys them all", "[llheteromap]")
    {
        {
            LLHeteroMap map;

            {
                // create each instance
                Chalk& chalk = map.obtain<Chalk>();
                chalk.name = "Chalk";

                Cheese& cheese = map.obtain<Cheese>();
                cheese.name = "Cheese";

                Chowdah& chowdah = map.obtain<Chowdah>();
                chowdah.name = "Chowdah";
            } // refs go out of scope

            {
                // verify each instance
                Chalk& chalk = map.obtain<Chalk>();
                CHECK(chalk.name == "Chalk");

                Cheese& cheese = map.obtain<Cheese>();
                CHECK(cheese.name == "Cheese");

                Chowdah& chowdah = map.obtain<Chowdah>();
                CHECK(chowdah.name == "Chowdah");
            }
        } // destroy map

        // Chalk, Cheese and Chowdah should have been created in specific order
        CHECK(clog == "aeo");

        // We don't care what order they're destroyed in, as long as each is
        // appropriately destroyed.
        std::set<std::string> dtorset;
        for (const char* cp = "aeo"; *cp; ++cp)
            dtorset.insert(std::string(1, *cp));
        CHECK(dlog == dtorset);
    }
} // anonymous namespace
