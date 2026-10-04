/**
 * @file lscript_library.cpp
 * @brief external library interface
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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


//  ##      ##    ###    ########  ##    ## #### ##    ##  ######   #### ####
//  ##  ##  ##   ## ##   ##     ## ###   ##  ##  ###   ## ##    ##  #### ####
//  ##  ##  ##  ##   ##  ##     ## ####  ##  ##  ####  ## ##        #### ####
//  ##  ##  ## ##     ## ########  ## ## ##  ##  ## ## ## ##   ####  ##   ##
//  ##  ##  ## ######### ##   ##   ##  ####  ##  ##  #### ##    ##
//  ##  ##  ## ##     ## ##    ##  ##   ###  ##  ##   ### ##    ##  #### ####
//   ###  ###  ##     ## ##     ## ##    ## #### ##    ##  ######   #### ####
//
// When adding functions, they <b>MUST</b> be appended to the end of
// the init() method. The init() associates the name with a number,
// which is then serialized into the bytecode. Inserting a new
// function in the middle will lead to many sim crashes. Phoenix 2006-04-10.

#include "linden_common.h"

#include "lscript_library.h"

LLScriptLibrary::LLScriptLibrary()
{
    init();
}

LLScriptLibrary::~LLScriptLibrary()
{
}

void dummy_func(LLScriptLibData *retval, LLScriptLibData *args, const LLUUID &id)
{
}

void LLScriptLibrary::init()
{
    // The table lsl-definitions generates (gen_lscript_library_defs) from
    // the definitions: every function, at its own number.
#include "lscript_library_defs.inc"
}

LLScriptLibraryFunction::LLScriptLibraryFunction(F32 eu, F32 st, void (*exec_func)(LLScriptLibData *, LLScriptLibData *, const LLUUID &), const char *name, const char *ret_type, const char *args, BOOL god_only)
        : mEnergyUse(eu), mSleepTime(st), mExecFunc(exec_func), mName(name), mReturnType(ret_type), mArgs(args), mGodOnly(god_only), mMonoSleepTime(st)
{
}

LLScriptLibraryFunction::~LLScriptLibraryFunction()
{
}

void LLScriptLibrary::addFunction(F32 eu, F32 st, void (*exec_func)(LLScriptLibData *, LLScriptLibData *, const LLUUID &), const char *name, const char *ret_type, const char *args, BOOL god_only)
{
    LLScriptLibraryFunction func(eu, st, exec_func, name, ret_type, args, god_only);
    mFunctions.push_back(func);
}

void LLScriptLibrary::dangerousAddFunction(S32 number, const char *name, const char *ret_type, const char *args, F32 eu, F32 st, F32 mono_st, bool god_only)
{
    // A number no function has stays a gap, with no name.
    if (number >= (S32)mFunctions.size())
    {
        mFunctions.resize(number + 1, LLScriptLibraryFunction(0.f, 0.f, dummy_func, NULL, NULL, NULL));
    }
    mFunctions[number] = LLScriptLibraryFunction(eu, st, dummy_func, name, ret_type, args, god_only);
    mFunctions[number].mMonoSleepTime = mono_st;
}

void LLScriptLibrary::assignExec(const char *name, void (*exec_func)(LLScriptLibData *, LLScriptLibData *, const LLUUID &))
{
    for (std::vector<LLScriptLibraryFunction>::iterator i = mFunctions.begin();
         i != mFunctions.end(); ++i)
    {
        if (i->mName && !strcmp(name, i->mName))
        {
            i->mExecFunc = exec_func;
            return;
        }
    }

    LL_ERRS() << "Unknown LSL function in assignExec: " << name << LL_ENDL;
}

void LLScriptLibData::print(std::ostream &s, BOOL b_prepend_comma)
{
    char tmp[1024]; /*Flawfinder: ignore*/
    if (b_prepend_comma)
    {
            s << ", ";
    }
    switch (mType)
    {
    case LST_INTEGER:
         s << mInteger;
         break;
    case LST_FLOATINGPOINT:
         snprintf(tmp, 1024, "%f", mFP);    /* Flawfinder: ignore */
         s << tmp;
         break;
    case LST_KEY:
         s << mKey;
         break;
    case LST_STRING:
         s << mString;
         break;
    case LST_VECTOR:
         snprintf(tmp, 1024, "<%f, %f, %f>", mVec.mV[VX], /* Flawfinder: ignore */
              mVec.mV[VY], mVec.mV[VZ]);
         s << tmp;
         break;
    case LST_QUATERNION:
         snprintf(tmp, 1024, "<%f, %f, %f, %f>", mQuat.mQ[VX], mQuat.mQ[VY], /* Flawfinder: ignore */
              mQuat.mQ[VZ], mQuat.mQ[VS]);
         s << tmp;
         break;
    default:
         break;
    }
}

void LLScriptLibData::print_separator(std::ostream& ostr, BOOL b_prepend_sep, char* sep)
{
    if (b_prepend_sep)
    {
        ostr << sep;
    }
    //print(ostr, FALSE);
    {
        char tmp[1024]; /* Flawfinder: ignore */
        switch (mType)
        {
        case LST_INTEGER:
             ostr << mInteger;
             break;
        case LST_FLOATINGPOINT:
             snprintf(tmp, 1024, "%f", mFP);    /* Flawfinder: ignore */
             ostr << tmp;
             break;
        case LST_KEY:
             ostr << mKey;
             break;
        case LST_STRING:
             ostr << mString;
             break;
        case LST_VECTOR:
             snprintf(tmp, 1024, "<%f, %f, %f>", mVec.mV[VX], /* Flawfinder: ignore */
                  mVec.mV[VY], mVec.mV[VZ]);
             ostr << tmp;
             break;
        case LST_QUATERNION:
             snprintf(tmp, 1024, "<%f, %f, %f, %f>", mQuat.mQ[VX], mQuat.mQ[VY], /* Flawfinder: ignore */
                  mQuat.mQ[VZ], mQuat.mQ[VS]);
             ostr << tmp;
             break;
        default:
             break;
        }
    }
}


LLScriptLibrary gScriptLibrary;
