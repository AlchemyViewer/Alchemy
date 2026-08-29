/**
 * @file lluistring.cpp
 * @brief LLUIString implementation.
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

#include "linden_common.h"
#include "lluistring.h"

#include "llfasttimer.h"
#include "llsd.h"
#include "lltrans.h"

LLUIString::LLUIString(const std::string& instring, const LLStringUtil::format_map_t& args)
:   mOrig(instring),
    mArgs(new LLStringUtil::format_map_t(args))
{
    dirty();
}

void LLUIString::assign(const std::string& s)
{
    mOrig = s;
    dirty();
}

void LLUIString::setArgList(const LLStringUtil::format_map_t& args)

{
    getArgs() = args;
    dirty();
}

void LLUIString::setArgs(const LLSD& sd)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    if (!sd.isMap()) return;
    for(LLSD::map_const_iterator sd_it = sd.beginMap();
        sd_it != sd.endMap();
        ++sd_it)
    {
        setArg(sd_it->first, sd_it->second.asString());
    }
    dirty();
}

void LLUIString::setArg(const std::string& key, const std::string& replacement)
{
    getArgs()[key] = replacement;
    dirty();
}

void LLUIString::truncate(S32 max_bytes)
{
    std::string& result = getUpdatedResult();
    if (result.size() > (size_t)max_bytes)
    {
        // Back off to a whole character. A byte count can fall between a
        // letter and its accent, or inside a flag or a family.
        result.resize(utf8str_grapheme_align_backward(result, (size_t)max_bytes));
    }
}

void LLUIString::erase(S32 byte_idx, S32 byte_len)
{
    getUpdatedResult().erase(byte_idx, byte_len);
}

void LLUIString::insert(S32 byte_idx, std::string_view chars)
{
    getUpdatedResult().insert(byte_idx, chars);
}

void LLUIString::replace(S32 byte_idx, llwchar wc)
{
    // Not an assignment the way the UTF-32 form was: the character being
    // replaced and the one replacing it need not occupy the same number of
    // bytes, so the span of the old one has to be measured first.
    std::string& result = getUpdatedResult();
    const auto at = utf8str_decode_at(result, (size_t)byte_idx);
    result.replace((size_t)byte_idx, at.next - (size_t)byte_idx, utf8str_from_cp(wc));
}

void LLUIString::clear()
{
    // Keep Args
    mOrig.clear();
    mResult.clear();
}

void LLUIString::dirty()
{
    mNeedsResult = true;
}

void LLUIString::updateResult() const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    mNeedsResult = false;

    // optimize for empty strings (don't attempt string replacement)
    if (mOrig.empty())
    {
        mResult.clear();
        return;
    }
    mResult = mOrig;

    // Merging local args into the defaults costs a full copy of LLTrans' map.
    // Most LLUIStrings carry no args of their own, so hand format() the shared
    // map directly in that case. insert() leaves existing keys alone either
    // way, so a default arg still wins over a local one of the same name.
    if (mArgs && !mArgs->empty())
    {
        LLStringUtil::format_map_t combined_args = LLTrans::getDefaultArgs();
        combined_args.insert(mArgs->begin(), mArgs->end());
        LLStringUtil::format(mResult, combined_args);
    }
    else
    {
        LLStringUtil::format(mResult, LLTrans::getDefaultArgs());
    }
}

LLStringUtil::format_map_t& LLUIString::getArgs()
{
    if (!mArgs)
    {
        mArgs = new LLStringUtil::format_map_t;
    }
    return *mArgs;
}
