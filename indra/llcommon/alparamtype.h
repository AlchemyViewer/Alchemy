/**
 * @file alparamtype.h
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

#pragma once

#include "llpreprocessor.h"
#include "stdtypes.h"

#include <algorithm>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

// A parameter block's table records what a parameter is called, how many of
// it there may be, and the functions that read and write it. What it does not
// record is the type: only the template that declares the parameter knows T,
// and by the time a descriptor exists T is gone.
//
// This is that type, kept beside the table rather than in it. A shipped
// viewer carries neither the entries nor the code that fills them, which is
// the whole of what putting a pointer back on every descriptor would cost.
// The readers are developer things: the schema the XUI tool exports, and the
// lint rule that asks whether an attribute exists.

namespace LLInitParam
{
    struct ParamDescriptor;
    class BlockDescriptor;
}

class ALParamType
{
public:
    enum EKind
    {
        SCALAR,          // one value, written as an attribute
        BLOCK,           // one nested block
        MULTIPLE_SCALAR, // several values
        MULTIPLE_BLOCK,  // several nested blocks
        IGNORED          // read from a file and thrown away
    };

    // What a leaf's value is, as far as a schema can say it. Everything
    // else -- a colour, an image, a font, a UUID, structured data -- is a
    // string whose vocabulary lives in another file, and the lint owns it.
    enum EValue
    {
        OTHER,
        BOOLEAN,
        INTEGER,
        UNSIGNED,
        REAL,
        STRING
    };

    // The names a lookup accepts. A function rather than the list, because
    // asking builds the map: a type's names are declared the first time
    // anything wants them, and a schema is the only thing that ever does.
    typedef std::vector<std::string> (*names_func_t)();

    EKind                           mKind{ SCALAR };
    EValue                          mValue{ OTHER };
    // typeid(T).name(), which is a compiler's spelling of the type rather
    // than one anyone would write: for a report, not for a decision.
    const char*                     mTypeName{ "" };
    // The block a BLOCK or MULTIPLE_BLOCK parameter carries, else null.
    LLInitParam::BlockDescriptor*   mBlock{ nullptr };
    // Null where the type names no values.
    names_func_t                    mValueNames{ nullptr };
};

// Filled while parameter blocks first construct, and read once something asks
// for a schema. Blocks construct on whichever thread first uses them, so the
// table locks; it is written a few hundred times in a process and never after
// startup, so the lock is not on any path that matters.
class LL_COMMON_API ALParamTypes
{
public:
    static void record(const LLInitParam::ParamDescriptor* param, const ALParamType& type);
    static const ALParamType* find(const LLInitParam::ParamDescriptor* param);

    // What the table holds, for the census that says what it weighs.
    static size_t count();
    static size_t bytes();
};

#if !LL_RELEASE_FOR_DOWNLOAD

// The names a lookup accepts, flattened out of the map its own type keys.
template <typename NAMED_VALUE>
std::vector<std::string> alParamValueNames()
{
    std::vector<std::string> names;
    if (auto* map = NAMED_VALUE::getValueNames())
    {
        names.reserve(map->size());
        for (const auto& pair : *map)
        {
            names.push_back(pair.first);
        }
        std::sort(names.begin(), names.end());
    }
    return names;
}

// A type that names its values says so by taking a string for a name; the
// unspecialized lookup takes a type nothing outside it can spell, which is
// how the rest of the block system tells the two apart. Only the specialized
// side instantiates the map walk above.
template <typename NAMED_VALUE>
ALParamType::names_func_t alParamValueNamesFunc()
{
    if constexpr (std::is_same_v<typename NAMED_VALUE::name_t, std::string>)
    {
        return &alParamValueNames<NAMED_VALUE>;
    }
    else
    {
        return nullptr;
    }
}

#endif // !LL_RELEASE_FOR_DOWNLOAD

// Which of the few types a schema can name this one is.
template <typename VALUE_T>
constexpr ALParamType::EValue alParamValueKind()
{
    if constexpr (std::is_same_v<VALUE_T, bool>)
    {
        return ALParamType::BOOLEAN;
    }
    else if constexpr (std::is_same_v<VALUE_T, std::string>)
    {
        return ALParamType::STRING;
    }
    else if constexpr (std::is_floating_point_v<VALUE_T>)
    {
        return ALParamType::REAL;
    }
    else if constexpr (std::is_integral_v<VALUE_T> && std::is_unsigned_v<VALUE_T>)
    {
        return ALParamType::UNSIGNED;
    }
    else if constexpr (std::is_integral_v<VALUE_T>)
    {
        return ALParamType::INTEGER;
    }
    else
    {
        return ALParamType::OTHER;
    }
}

// A lookup for a parameter with no value to name: what an Ignored declares.
struct ALParamNoNames
{
    struct Unnamed {};
    typedef Unnamed name_t;
};

// Says what a parameter just added to a block's table is. Called from the
// four templates that declare one, which are the last place T is known;
// compiled away entirely where nothing will read the answer.
template <typename VALUE_T, typename NAMED_VALUE>
inline void alRecordParamType([[maybe_unused]] const LLInitParam::ParamDescriptor* param,
                              [[maybe_unused]] ALParamType::EKind kind,
                              [[maybe_unused]] LLInitParam::BlockDescriptor* block = nullptr)
{
#if !LL_RELEASE_FOR_DOWNLOAD
    ALParamType type;
    type.mKind = kind;
    type.mValue = alParamValueKind<VALUE_T>();
    type.mTypeName = typeid(VALUE_T).name();
    type.mBlock = block;
    type.mValueNames = alParamValueNamesFunc<NAMED_VALUE>();
    ALParamTypes::record(param, type);
#endif
}
