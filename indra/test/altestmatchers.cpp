/**
 * @file altestmatchers.cpp
 * @brief Catch2 matchers for what LL's TUT helpers compared.
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

#include "altestmatchers.h"

#include "llformat.h"
#include "llsdserialize.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ll_test
{
    namespace
    {
        // A NaN equals nothing, itself included. Under /fp:fast, which the
        // optimised Windows builds use, MSVC compiles == without its test for
        // an unordered result, so a NaN would equal anything: it is asked
        // about apart.
        bool sameNumber(F64 actual, F64 expected)
        {
            return !std::isnan(actual) && !std::isnan(expected) && actual == expected;
        }

        // Where actual and expected first differ: a path from the top, as in
        // ["key"][3], and what differs there. Empty "what" means equal.
        struct Difference
        {
            std::string path;
            std::string what;
        };

        Difference firstDifference(const LLSD& actual, const LLSD& expected, const std::string& path)
        {
            if (actual.type() != expected.type())
            {
                return { path, "type" };
            }
            bool same = true;
            switch (actual.type())
            {
                case LLSD::TypeUndefined:
                    break;
                case LLSD::TypeBoolean:
                    same = actual.asBoolean() == expected.asBoolean();
                    break;
                case LLSD::TypeInteger:
                    same = actual.asInteger() == expected.asInteger();
                    break;
                case LLSD::TypeReal:
                    same = sameNumber(actual.asReal(), expected.asReal());
                    break;
                case LLSD::TypeString:
                    same = actual.asString() == expected.asString();
                    break;
                case LLSD::TypeUUID:
                    same = actual.asUUID() == expected.asUUID();
                    break;
                case LLSD::TypeDate:
                    // Not LLDate's ==, which is made of < and so holds a NaN
                    // date equal to every date; TUT compared the seconds.
                    same = sameNumber(actual.asDate().secondsSinceEpoch(), expected.asDate().secondsSinceEpoch());
                    break;
                case LLSD::TypeURI:
                    same = actual.asURI().asString() == expected.asURI().asString();
                    break;
                case LLSD::TypeBinary:
                    same = actual.asBinary() == expected.asBinary();
                    break;
                case LLSD::TypeMap:
                {
                    if (actual.size() != expected.size())
                    {
                        return { path, "map size" };
                    }
                    LLSD::map_const_iterator expected_entry = expected.beginMap();
                    for (LLSD::map_const_iterator entry = actual.beginMap(); entry != actual.endMap();
                         ++entry, ++expected_entry)
                    {
                        if (entry->first != expected_entry->first)
                        {
                            return { path, "map keys" };
                        }
                        Difference inner = firstDifference(entry->second, expected_entry->second,
                                                           path + "[\"" + entry->first + "\"]");
                        if (!inner.what.empty())
                        {
                            return inner;
                        }
                    }
                    break;
                }
                case LLSD::TypeArray:
                {
                    if (actual.size() != expected.size())
                    {
                        return { path, "array size" };
                    }
                    for (LLSD::Integer index = 0; index < actual.size(); ++index)
                    {
                        Difference inner = firstDifference(actual[index], expected[index],
                                                           path + "[" + std::to_string(index) + "]");
                        if (!inner.what.empty())
                        {
                            return inner;
                        }
                    }
                    break;
                }
                default:
                    return { path, "type " + std::to_string(actual.type()) + ", which is not an LLSD type" };
            }
            return { path, same ? std::string() : std::string("value") };
        }
    }

    LLSDEqualsMatcher::LLSDEqualsMatcher(LLSD expected)
        : mExpected(std::move(expected))
    {
    }

    bool LLSDEqualsMatcher::match(const LLSD& actual) const
    {
        const Difference difference = firstDifference(actual, mExpected, "");
        mDifference = difference.what.empty()
            ? std::string()
            : (difference.path.empty() ? std::string("the top") : difference.path) + ": " + difference.what;
        return mDifference.empty();
    }

    std::string LLSDEqualsMatcher::describe() const
    {
        std::ostringstream out;
        out << "equals " << LLSDNotationStreamer(mExpected);
        if (!mDifference.empty())
        {
            out << " (they differ at " << mDifference << ")";
        }
        return out.str();
    }

    std::ostream& operator<<(std::ostream& out, const MemoryView& memory)
    {
        // The first bytes are what tell two buffers apart at a glance.
        constexpr size_t SHOWN = 32;
        const U8* bytes = static_cast<const U8*>(memory.data);
        out << memory.size << " bytes";
        if (bytes && memory.size)
        {
            out << ":";
            for (size_t i = 0; i < std::min(memory.size, SHOWN); ++i)
            {
                out << llformat(" %02x", bytes[i]);
            }
            if (memory.size > SHOWN)
            {
                out << " ...";
            }
        }
        return out;
    }

    bool MemoryMatchesMatcher::match(const MemoryView& actual) const
    {
        if (actual.size != mExpected.size)
        {
            mDifference = "the sizes";
            return false;
        }
        if (actual.size == 0 || std::memcmp(actual.data, mExpected.data, actual.size) == 0)
        {
            mDifference.clear();
            return true;
        }
        const U8* a = static_cast<const U8*>(actual.data);
        const U8* e = static_cast<const U8*>(mExpected.data);
        size_t at = 0;
        while (a[at] == e[at])
        {
            ++at;
        }
        mDifference = llformat("byte %zu", at);
        return false;
    }

    std::string MemoryMatchesMatcher::describe() const
    {
        std::ostringstream out;
        out << "matches " << mExpected;
        if (!mDifference.empty())
        {
            out << " (they differ at " << mDifference << ")";
        }
        return out.str();
    }
}
