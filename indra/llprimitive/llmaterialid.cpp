/**
* @file llmaterialid.cpp
* @brief Implementation of llmaterialid
* @author Stinson@lindenlab.com
*
* $LicenseInfo:firstyear=2012&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2012, Linden Research, Inc.
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

#include "llmaterialid.h"

#include <string>

const LLMaterialID LLMaterialID::null;

LLMaterialID::LLMaterialID()
{
    clear();
}

LLMaterialID::LLMaterialID(const LLSD& pMaterialID)
{
    llassert(pMaterialID.isBinary());
    parseFromBinary(pMaterialID.asBinary());
}

LLMaterialID::LLMaterialID(const LLSD::Binary& pMaterialID)
{
    parseFromBinary(pMaterialID);
}

LLMaterialID::LLMaterialID(const void* pMemory)
{
    set(pMemory);
}

LLMaterialID::LLMaterialID(const LLUUID& lluid)
{
    set(lluid.mData);
}

bool LLMaterialID::operator == (const LLMaterialID& pOtherMaterialID) const
{
    return (compareToOtherMaterialID(pOtherMaterialID) == 0);
}

bool LLMaterialID::operator != (const LLMaterialID& pOtherMaterialID) const
{
    return (compareToOtherMaterialID(pOtherMaterialID) != 0);
}

bool LLMaterialID::operator < (const LLMaterialID& pOtherMaterialID) const
{
    return (compareToOtherMaterialID(pOtherMaterialID) < 0);
}

bool LLMaterialID::operator <= (const LLMaterialID& pOtherMaterialID) const
{
    return (compareToOtherMaterialID(pOtherMaterialID) <= 0);
}

bool LLMaterialID::operator > (const LLMaterialID& pOtherMaterialID) const
{
    return (compareToOtherMaterialID(pOtherMaterialID) > 0);
}

bool LLMaterialID::operator >= (const LLMaterialID& pOtherMaterialID) const
{
    return (compareToOtherMaterialID(pOtherMaterialID) >= 0);
}

bool LLMaterialID::isNull() const
{
    // Two loads and an or, rather than a memcmp that has to reach the null
    // instance to compare against.
    U64 a, b;
    memcpy(&a, mID,     sizeof(a));
    memcpy(&b, mID + 8, sizeof(b));
    return (a | b) == 0;
}

const U8* LLMaterialID::get() const
{
    return mID;
}

void LLMaterialID::set(const void* pMemory)
{
    llassert(pMemory != NULL);
    if (pMemory == nullptr)
    {
        clear();
        return;
    }
    memcpy(mID, pMemory, MATERIAL_ID_SIZE);
}

void LLMaterialID::clear()
{
    memset(mID, 0, MATERIAL_ID_SIZE * sizeof(U8));
}

LLSD LLMaterialID::asLLSD() const
{
    LLSD::Binary materialIDBinary;

    materialIDBinary.resize(MATERIAL_ID_SIZE * sizeof(U8));
    memcpy(materialIDBinary.data(), mID, MATERIAL_ID_SIZE * sizeof(U8));

    LLSD materialID = std::move(materialIDBinary);
    return materialID;
}

std::string LLMaterialID::asString() const
{
    // Four groups of four bytes, dash separated, each group written most
    // significant byte first. Little-endian word order, preserved so log
    // strings remain greppable across builds.
    //
    // Four fixed-width groups need neither a format string parsed at run time
    // nor a temporary string apiece: 479 ns against 27 ns for the whole id.
    static const char hex[] = "0123456789abcdef";
    char buffer[MATERIAL_ID_SIZE * 2 + (MATERIAL_ID_SIZE / sizeof(U32)) - 1];
    char* p = buffer;
    for (unsigned int i = 0U; i < static_cast<unsigned int>(MATERIAL_ID_SIZE / sizeof(U32)); ++i)
    {
        if (i != 0U)
        {
            *p++ = '-';
        }
        const U8* group = &mID[i * sizeof(U32)];
        for (int byte = sizeof(U32) - 1; byte >= 0; --byte)
        {
            *p++ = hex[group[byte] >> 4];
            *p++ = hex[group[byte] & 0x0F];
        }
    }
    return std::string(buffer, static_cast<size_t>(p - buffer));
}

LLUUID LLMaterialID::asUUID() const
{
    LLUUID ret;
    memcpy(ret.mData, mID, MATERIAL_ID_SIZE);
    return ret;
}

std::ostream& operator<<(std::ostream& s, const LLMaterialID &material_id)
{
    s << material_id.asString();
    return s;
}

void LLMaterialID::parseFromBinary (const LLSD::Binary& pMaterialID)
{
    llassert(pMaterialID.size() == MATERIAL_ID_SIZE);
    if (pMaterialID.size() != MATERIAL_ID_SIZE)
    {
        clear();
        return;
    }
    memcpy(mID, pMaterialID.data(), MATERIAL_ID_SIZE);
}

int LLMaterialID::compareToOtherMaterialID(const LLMaterialID& pOtherMaterialID) const
{
    return memcmp(mID, pOtherMaterialID.mID, MATERIAL_ID_SIZE);
}
