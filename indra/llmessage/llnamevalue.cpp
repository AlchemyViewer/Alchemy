/**
 * @file llnamevalue.cpp
 * @brief class for defining name value pairs.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

// Examples:
// AvatarCharacter STRING RW DSV male1

#include "linden_common.h"

#include "llnamevalue.h"

#include "u64.h"
#include "llstring.h"
#include "llstringtable.h"

// Anonymous enumeration to provide constants in this file.
// *NOTE: This value may be used in sscanf statements below as its
// value-1, so search for '63' if you change U64_BUFFER_LEN.
enum
{
    U64_BUFFER_LEN = 64
};

LLStringTable   gNVNameTable(256);

char NameValueTypeStrings[NVT_EOF][NAME_VALUE_TYPE_STRING_LENGTH] = /*Flawfinder: Ignore*/
{
    "NULL",
    "STRING",
    "F32",
    "S32",
    "VEC3",
    "U32",
    "CAMERA", // Deprecated, but leaving in case removing completely would cause problems
    "ASSET",
    "U64"
};

char NameValueClassStrings[NVC_EOF][NAME_VALUE_CLASS_STRING_LENGTH] = /*Flawfinder: Ignore*/
{
    "NULL",
    "R",            // read only
    "RW"            // read write
};

char NameValueSendtoStrings[NVS_EOF][NAME_VALUE_SENDTO_STRING_LENGTH] = /*Flawfinder: Ignore*/
{
    "NULL",
    "S",    // "Sim", formerly SIM
    "DS",   // "Data Sim" formerly SIM_SPACE
    "SV",   // "Sim Viewer" formerly SIM_VIEWER
    "DSV"   // "Data Sim Viewer", formerly SIM_SPACE_VIEWER
};      /*Flawfinder: Ignore*/


//
// Class
//

LLNameValue::LLNameValue()
{
    baseInit();
}

void LLNameValue::baseInit()
{
    mNVNameTable = &gNVNameTable;

    mName = NULL;
    mNameValueReference.string = NULL;

    mType = NVT_NULL;
    mStringType = NameValueTypeStrings[NVT_NULL];

    mClass = NVC_NULL;
    mStringClass = NameValueClassStrings[NVC_NULL];

    mSendto = NVS_NULL;
    mStringSendto = NameValueSendtoStrings[NVS_NULL];
}

void LLNameValue::init(const char *name, const char *data, const char *type, const char *nvclass, const char *nvsendto)
{
    mNVNameTable = &gNVNameTable;

    mName = mNVNameTable->addString(name);

    // Nota Bene: Whatever global structure manages this should have these in the name table already!
    mStringType = mNVNameTable->addString(type);
    if (!strcmp(mStringType, "STRING"))
    {
        S32 string_length = (S32)strlen(data);      /*Flawfinder: Ignore*/
        mType = NVT_STRING;

        delete[] mNameValueReference.string;

        // two options here. . .  data can either look like foo or "foo"
        // WRONG! - this is a poorly implemented and incomplete escape
        // mechanism. For example, using this scheme, there is no way
        // to tell an intentional double quotes from a zero length
        // string. This needs to excised. Phoenix
        //if (strchr(data, '\"'))
        //{
        //  string_length -= 2;
        //  mNameValueReference.string = new char[string_length + 1];;
        //  strncpy(mNameValueReference.string, data + 1, string_length);
        //}
        //else
        //{
        mNameValueReference.string = new char[string_length + 1];;
        strncpy(mNameValueReference.string, data, string_length);       /*Flawfinder: Ignore*/
        //}
        mNameValueReference.string[string_length] = 0;
    }
    else if (!strcmp(mStringType, "F32"))
    {
        mType = NVT_F32;
        mNameValueReference.f32 = new F32((F32)atof(data));
    }
    else if (!strcmp(mStringType, "S32"))
    {
        mType = NVT_S32;
        mNameValueReference.s32 = new S32(atoi(data));
    }
    else if (!strcmp(mStringType, "U64"))
    {
        mType = NVT_U64;
        mNameValueReference.u64 = new U64(str_to_U64(ll_safe_string(data)));
    }
    else if (!strcmp(mStringType, "VEC3"))
    {
        mType = NVT_VEC3;
        // A component the value does not give reads as 0.
        F32 t1 = 0.f, t2 = 0.f, t3 = 0.f;

        // two options here. . .  data can either look like 0, 1, 2 or <0, 1, 2>

        if (strchr(data, '<'))
        {
            sscanf(data, "<%f, %f, %f>", &t1, &t2, &t3);
        }
        else
        {
            sscanf(data, "%f, %f, %f", &t1, &t2, &t3);
        }

        // finite checks
        if (!llfinite(t1) || !llfinite(t2) || !llfinite(t3))
        {
            t1 = 0.f;
            t2 = 0.f;
            t3 = 0.f;
        }

        mNameValueReference.vec3 = new LLVector3(t1, t2, t3);
    }
    else if (!strcmp(mStringType, "U32"))
    {
        mType = NVT_U32;
        mNameValueReference.u32 = new U32(atoi(data));
    }
    else if(!strcmp(mStringType, (const char*)NameValueTypeStrings[NVT_ASSET]))
    {
        // assets are treated like strings, except that the name has
        // meaning to an LLAssetInfo object
        S32 string_length = (S32)strlen(data);      /*Flawfinder: Ignore*/
        mType = NVT_ASSET;

        // two options here. . .  data can either look like foo or "foo"
        // WRONG! - this is a poorly implemented and incomplete escape
        // mechanism. For example, using this scheme, there is no way
        // to tell an intentional double quotes from a zero length
        // string. This needs to excised. Phoenix
        //if (strchr(data, '\"'))
        //{
        //  string_length -= 2;
        //  mNameValueReference.string = new char[string_length + 1];;
        //  strncpy(mNameValueReference.string, data + 1, string_length);
        //}
        //else
        //{
        mNameValueReference.string = new char[string_length + 1];;
        strncpy(mNameValueReference.string, data, string_length);       /*Flawfinder: Ignore*/
        //}
        mNameValueReference.string[string_length] = 0;
    }
    else
    {
        LL_WARNS() << "Unknown name value type string " << mStringType << " for " << mName << LL_ENDL;
        mType = NVT_NULL;
    }


    // Nota Bene: Whatever global structure manages this should have these in the name table already!
    if (!strcmp(nvclass, "R") ||
        !strcmp(nvclass, "READ_ONLY"))          // legacy
    {
        mClass = NVC_READ_ONLY;
        mStringClass = mNVNameTable->addString("R");
    }
    else if (!strcmp(nvclass, "RW") ||
            !strcmp(nvclass, "READ_WRITE")) // legacy
    {
        mClass = NVC_READ_WRITE;
        mStringClass = mNVNameTable->addString("RW");
    }
    else
    {
        // assume it's bad
        mClass = NVC_NULL;
        mStringClass = mNVNameTable->addString(nvclass);
    }

    // Initialize the sendto variable
    if (!strcmp(nvsendto, "S") ||
        !strcmp(nvsendto, "SIM"))           // legacy
    {
        mSendto = NVS_SIM;
        mStringSendto = mNVNameTable->addString("S");
    }
    else if (!strcmp(nvsendto, "DS") ||
        !strcmp(nvsendto, "SIM_SPACE")) // legacy
    {
        mSendto = NVS_DATA_SIM;
        mStringSendto = mNVNameTable->addString("DS");
    }
    else if (!strcmp(nvsendto, "SV") ||
            !strcmp(nvsendto, "SIM_VIEWER"))    // legacy
    {
        mSendto = NVS_SIM_VIEWER;
        mStringSendto = mNVNameTable->addString("SV");
    }
    else if (!strcmp(nvsendto, "DSV") ||
            !strcmp(nvsendto, "SIM_SPACE_VIEWER"))  // legacy
    {
        mSendto = NVS_DATA_SIM_VIEWER;
        mStringSendto = mNVNameTable->addString("DSV");
    }
    else
    {
        LL_WARNS() << "LLNameValue::init() - unknown sendto field "
                << nvsendto << " for NV " << mName << LL_ENDL;
        mSendto = NVS_NULL;
        mStringSendto = mNVNameTable->addString("S");
    }

}


LLNameValue::LLNameValue(const char *name, const char *data, const char *type, const char *nvclass)
{
    baseInit();
    // if not specified, send to simulator only
    init(name, data, type, nvclass, "SIM");
}


LLNameValue::LLNameValue(const char *name, const char *data, const char *type, const char *nvclass, const char *nvsendto)
{
    baseInit();
    init(name, data, type, nvclass, nvsendto);
}



// Initialize without any initial data.
LLNameValue::LLNameValue(const char *name, const char *type, const char *nvclass)
{
    baseInit();
    mName = mNVNameTable->addString(name);

    // Nota Bene: Whatever global structure manages this should have these in the name table already!
    mStringType = mNVNameTable->addString(type);
    if (!strcmp(mStringType, "STRING"))
    {
        mType = NVT_STRING;
        mNameValueReference.string = NULL;
    }
    else if (!strcmp(mStringType, "F32"))
    {
        mType = NVT_F32;
        mNameValueReference.f32 = NULL;
    }
    else if (!strcmp(mStringType, "S32"))
    {
        mType = NVT_S32;
        mNameValueReference.s32 = NULL;
    }
    else if (!strcmp(mStringType, "VEC3"))
    {
        mType = NVT_VEC3;
        mNameValueReference.vec3 = NULL;
    }
    else if (!strcmp(mStringType, "U32"))
    {
        mType = NVT_U32;
        mNameValueReference.u32 = NULL;
    }
    else if (!strcmp(mStringType, "U64"))
    {
        mType = NVT_U64;
        mNameValueReference.u64 = NULL;
    }
    else if(!strcmp(mStringType, (const char*)NameValueTypeStrings[NVT_ASSET]))
    {
        mType = NVT_ASSET;
        mNameValueReference.string = NULL;
    }
    else
    {
        mType = NVT_NULL;
        LL_INFOS() << "Unknown name-value type " << mStringType << LL_ENDL;
    }

    // Nota Bene: Whatever global structure manages this should have these in the name table already!
    mStringClass = mNVNameTable->addString(nvclass);
    if (!strcmp(mStringClass, "READ_ONLY"))
    {
        mClass = NVC_READ_ONLY;
    }
    else if (!strcmp(mStringClass, "READ_WRITE"))
    {
        mClass = NVC_READ_WRITE;
    }
    else
    {
        mClass = NVC_NULL;
    }

    // Initialize the sendto variable
    mStringSendto = mNVNameTable->addString("SIM");
    mSendto = NVS_SIM;
}


namespace
{
    bool is_nv_space(char c)
    {
        return c == ' ' || c == '\n' || c == '\t' || c == '\r';
    }

    // Moves pos past the whitespace at it.
    void skip_nv_space(const char* data, size_t& pos)
    {
        while (is_nv_space(data[pos]))
        {
            ++pos;
        }
    }

    // Reads the word at pos, up to the whitespace or the end of the line,
    // and moves pos past it.
    std::string read_nv_word(const char* data, size_t& pos)
    {
        const size_t start = pos;
        while (data[pos] && !is_nv_space(data[pos]))
        {
            ++pos;
        }
        return std::string(data + start, pos - start);
    }

    bool nv_begins_with(const char* data, const char* prefix)
    {
        return !strncmp(prefix, data, strlen(prefix));      /* Flawfinder: ignore */
    }
}

// data is in the format:
// "NameValueName   Type    Class   Data"
LLNameValue::LLNameValue(const char *data)
{
    baseInit();

    size_t pos = 0;
    skip_nv_space(data, pos);
    const std::string name = read_nv_word(data, pos);
    skip_nv_space(data, pos);
    const std::string type = read_nv_word(data, pos);
    skip_nv_space(data, pos);

    // A line with no name or no type holds no pair, and is left with no name.
    if (name.empty() || type.empty())
    {
        return;
    }

    // do we have a class argument? If not, default to read-write
    std::string nvclass = "READ_WRITE";
    for (S32 i = NVC_READ_ONLY; i < NVC_EOF; i++)
    {
        if (nv_begins_with(data + pos, NameValueClassStrings[i]))
        {
            nvclass = read_nv_word(data, pos);
            skip_nv_space(data, pos);
            break;
        }
    }

    // Do we have a sendto argument? If not, default to sim only
    std::string nvsendto = "SIM";
    for (S32 i = NVS_SIM; i < NVS_EOF; i++)
    {
        if (nv_begins_with(data + pos, NameValueSendtoStrings[i]))
        {
            nvsendto = read_nv_word(data, pos);
            skip_nv_space(data, pos);
            break;
        }
    }

    // the rest of the line is the value
    init(name.c_str(), data + pos, type.c_str(), nvclass.c_str(), nvsendto.c_str());
}


LLNameValue::~LLNameValue()
{
    mNVNameTable->removeString(mName);
    mName = NULL;

    switch(mType)
    {
    case NVT_STRING:
    case NVT_ASSET:
        delete [] mNameValueReference.string;
        mNameValueReference.string = NULL;
        break;
    case NVT_F32:
        delete mNameValueReference.f32;
        mNameValueReference.string = NULL;
        break;
    case NVT_S32:
        delete mNameValueReference.s32;
        mNameValueReference.string = NULL;
        break;
    case NVT_VEC3:
        delete mNameValueReference.vec3;
        mNameValueReference.string = NULL;
        break;
    case NVT_U32:
        delete mNameValueReference.u32;
        mNameValueReference.u32 = NULL;
        break;
    case NVT_U64:
        delete mNameValueReference.u64;
        mNameValueReference.u64 = NULL;
        break;
    default:
        break;
    }

    delete[] mNameValueReference.string;
    mNameValueReference.string = NULL;
}

char    *LLNameValue::getString()
{
    if (mType == NVT_STRING)
    {
        return mNameValueReference.string;
    }
    else
    {
        LL_ERRS() << mName << " not a string!" << LL_ENDL;
        return NULL;
    }
}

const char *LLNameValue::getAsset() const
{
    if (mType == NVT_ASSET)
    {
        return mNameValueReference.string;
    }
    else
    {
        LL_ERRS() << mName << " not an asset!" << LL_ENDL;
        return NULL;
    }
}

F32     *LLNameValue::getF32()
{
    if (mType == NVT_F32)
    {
        return mNameValueReference.f32;
    }
    else
    {
        LL_ERRS() << mName << " not a F32!" << LL_ENDL;
        return NULL;
    }
}

S32     *LLNameValue::getS32()
{
    if (mType == NVT_S32)
    {
        return mNameValueReference.s32;
    }
    else
    {
        LL_ERRS() << mName << " not a S32!" << LL_ENDL;
        return NULL;
    }
}

U32     *LLNameValue::getU32()
{
    if (mType == NVT_U32)
    {
        return mNameValueReference.u32;
    }
    else
    {
        LL_ERRS() << mName << " not a U32!" << LL_ENDL;
        return NULL;
    }
}

U64     *LLNameValue::getU64()
{
    if (mType == NVT_U64)
    {
        return mNameValueReference.u64;
    }
    else
    {
        LL_ERRS() << mName << " not a U64!" << LL_ENDL;
        return NULL;
    }
}

void    LLNameValue::getVec3(LLVector3 &vec)
{
    if (mType == NVT_VEC3)
    {
        vec = *mNameValueReference.vec3;
    }
    else
    {
        LL_ERRS() << mName << " not a Vec3!" << LL_ENDL;
    }
}

LLVector3   *LLNameValue::getVec3()
{
    if (mType == NVT_VEC3)
    {
         return (mNameValueReference.vec3);
    }
    else
    {
        LL_ERRS() << mName << " not a Vec3!" << LL_ENDL;
        return NULL;
    }
}


bool LLNameValue::sendToData() const
{
    return (mSendto == NVS_DATA_SIM || mSendto == NVS_DATA_SIM_VIEWER);
}


bool LLNameValue::sendToViewer() const
{
    return (mSendto == NVS_SIM_VIEWER || mSendto == NVS_DATA_SIM_VIEWER);
}


LLNameValue &LLNameValue::operator=(const LLNameValue &a)
{
    if (mType != a.mType)
    {
        return *this;
    }
    if (mClass == NVC_READ_ONLY)
        return *this;

    switch(a.mType)
    {
    case NVT_STRING:
    case NVT_ASSET:
        if (mNameValueReference.string)
            delete [] mNameValueReference.string;

        mNameValueReference.string = new char [strlen(a.mNameValueReference.string) + 1];       /* Flawfinder: ignore */
        if(mNameValueReference.string != NULL)
        {
            strcpy(mNameValueReference.string, a.mNameValueReference.string);       /* Flawfinder: ignore */
        }
        break;
    case NVT_F32:
        *mNameValueReference.f32 = *a.mNameValueReference.f32;
        break;
    case NVT_S32:
        *mNameValueReference.s32 = *a.mNameValueReference.s32;
        break;
    case NVT_VEC3:
        *mNameValueReference.vec3 = *a.mNameValueReference.vec3;
        break;
    case NVT_U32:
        *mNameValueReference.u32 = *a.mNameValueReference.u32;
        break;
    case NVT_U64:
        *mNameValueReference.u64 = *a.mNameValueReference.u64;
        break;
    default:
        LL_ERRS() << "Unknown Name value type " << (U32)a.mType << LL_ENDL;
        break;
    }

    return *this;
}

void LLNameValue::setString(const char *a)
{
    if (mClass == NVC_READ_ONLY)
        return;

    switch(mType)
    {
    case NVT_STRING:
        if (a)
        {
            if (mNameValueReference.string)
            {
                delete [] mNameValueReference.string;
            }

            mNameValueReference.string = new char [strlen(a) + 1];      /* Flawfinder: ignore */
            if(mNameValueReference.string != NULL)
            {
                strcpy(mNameValueReference.string,  a);     /* Flawfinder: ignore */
            }
        }
        else
        {
            if (mNameValueReference.string)
                delete [] mNameValueReference.string;

            mNameValueReference.string = new char [1];
            mNameValueReference.string[0] = 0;
        }
        break;
    default:
        break;
    }

    return;
}


void LLNameValue::setAsset(const char *a)
{
    if (mClass == NVC_READ_ONLY)
        return;

    switch(mType)
    {
    case NVT_ASSET:
        if (a)
        {
            if (mNameValueReference.string)
            {
                delete [] mNameValueReference.string;
            }
            mNameValueReference.string = new char [strlen(a) + 1];          /* Flawfinder: ignore */
            if(mNameValueReference.string != NULL)
            {
                strcpy(mNameValueReference.string,  a);     /* Flawfinder: ignore */
            }
        }
        else
        {
            if (mNameValueReference.string)
                delete [] mNameValueReference.string;

            mNameValueReference.string = new char [1];
            mNameValueReference.string[0] = 0;
        }
        break;
    default:
        break;
    }
}


void LLNameValue::setF32(const F32 a)
{
    if (mClass == NVC_READ_ONLY)
        return;

    switch(mType)
    {
    case NVT_F32:
        *mNameValueReference.f32 = a;
        break;
    default:
        break;
    }

    return;
}


void LLNameValue::setS32(const S32 a)
{
    if (mClass == NVC_READ_ONLY)
        return;

    switch(mType)
    {
    case NVT_S32:
        *mNameValueReference.s32 = a;
        break;
    case NVT_U32:
        *mNameValueReference.u32 = a;
        break;
    case NVT_F32:
        *mNameValueReference.f32 = (F32)a;
        break;
    default:
        break;
    }

    return;
}


void LLNameValue::setU32(const U32 a)
{
    if (mClass == NVC_READ_ONLY)
        return;

    switch(mType)
    {
    case NVT_S32:
        *mNameValueReference.s32 = a;
        break;
    case NVT_U32:
        *mNameValueReference.u32 = a;
        break;
    case NVT_F32:
        *mNameValueReference.f32 = (F32)a;
        break;
    default:
        LL_ERRS() << "NameValue: Trying to set U32 into a " << mStringType << ", unknown conversion" << LL_ENDL;
        break;
    }
    return;
}


void LLNameValue::setVec3(const LLVector3 &a)
{
    if (mClass == NVC_READ_ONLY)
        return;

    switch(mType)
    {
    case NVT_VEC3:
        *mNameValueReference.vec3 = a;
        break;
    default:
        LL_ERRS() << "NameValue: Trying to set LLVector3 into a " << mStringType << ", unknown conversion" << LL_ENDL;
        break;
    }
    return;
}


std::string LLNameValue::printNameValue() const
{
    std::string buffer;
    buffer = llformat("%s %s %s %s ", mName, mStringType, mStringClass, mStringSendto);
    buffer += printData();
//  LL_INFOS() << "Name Value Length: " << buffer.size() + 1 << LL_ENDL;
    return buffer;
}

std::string LLNameValue::printData() const
{
    std::string buffer;
    switch(mType)
    {
    case NVT_STRING:
    case NVT_ASSET:
        buffer = mNameValueReference.string;
        break;
    case NVT_F32:
        buffer = llformat("%f", *mNameValueReference.f32);
        break;
    case NVT_S32:
        buffer = llformat("%d", *mNameValueReference.s32);
        break;
    case NVT_U32:
        buffer = llformat("%u", *mNameValueReference.u32);
        break;
    case NVT_U64:
        {
            char u64_string[U64_BUFFER_LEN];    /* Flawfinder: ignore */
            U64_to_str(*mNameValueReference.u64, u64_string, sizeof(u64_string));
            buffer = u64_string;
        }
        break;
    case NVT_VEC3:
        buffer = llformat( "%f, %f, %f", mNameValueReference.vec3->mV[VX], mNameValueReference.vec3->mV[VY], mNameValueReference.vec3->mV[VZ]);
        break;
    default:
        LL_ERRS() << "Trying to print unknown NameValue type " << mStringType << LL_ENDL;
        break;
    }
    return buffer;
}

std::ostream&       operator<<(std::ostream& s, const LLNameValue &a)
{
    switch(a.mType)
    {
    case NVT_STRING:
    case NVT_ASSET:
        s << a.mNameValueReference.string;
        break;
    case NVT_F32:
        s << (*a.mNameValueReference.f32);
        break;
    case NVT_S32:
        s << *(a.mNameValueReference.s32);
        break;
    case NVT_U32:
        s << *(a.mNameValueReference.u32);
        break;
    case NVT_U64:
        {
            char u64_string[U64_BUFFER_LEN];    /* Flawfinder: ignore */
            U64_to_str(*a.mNameValueReference.u64, u64_string, sizeof(u64_string));
            s << u64_string;
        }
        break;
    case NVT_VEC3:
        s << *(a.mNameValueReference.vec3);
        break;
    default:
        LL_ERRS() << "Trying to print unknown NameValue type " << a.mStringType << LL_ENDL;
        break;
    }
    return s;
}

