/**
 * @file alparamtype.cpp
 * @brief The C++ type behind a declared parameter, kept beside the table.
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

#include "alparamtype.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <mutex>

namespace
{
    typedef boost::unordered_flat_map<const LLInitParam::ParamDescriptor*, ALParamType> table_t;

    // Function-local, so it exists before the first block whose parameters
    // record into it, whatever order the statics run in.
    table_t& table()
    {
        static table_t sTable;
        return sTable;
    }

    std::mutex& lock()
    {
        static std::mutex sMutex;
        return sMutex;
    }
}

//static
void ALParamTypes::record(const LLInitParam::ParamDescriptor* param, const ALParamType& type)
{
    std::lock_guard<std::mutex> guard(lock());
    table()[param] = type;
}

//static
const ALParamType* ALParamTypes::find(const LLInitParam::ParamDescriptor* param)
{
    std::lock_guard<std::mutex> guard(lock());
    const table_t::const_iterator found = table().find(param);
    return found == table().end() ? nullptr : &found->second;
}

//static
size_t ALParamTypes::count()
{
    std::lock_guard<std::mutex> guard(lock());
    return table().size();
}

//static
size_t ALParamTypes::bytes()
{
    std::lock_guard<std::mutex> guard(lock());
    // A flat map's slot is the pair plus a byte of control, and it keeps room
    // it has not filled: the load factor is what the capacity says.
    return (sizeof(table_t::value_type) + 1) * table().bucket_count();
}
