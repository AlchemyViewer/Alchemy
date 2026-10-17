/**
 * @file llavatarappearancedefines_bench.cpp
 * @brief The cost of the avatar dictionary lookups LLVOAvatar::updateTextures
 * makes for every texture entry.
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

// LLVOAvatar::updateTextures runs for every visible avatar every frame and,
// for each of its TEX_NUM_INDICES texture entries, asks the dictionary which
// wearable owns the entry, reads the entry (whether it is local, and its
// bake), and asks isIndexBakedTexture, which reads the entry again. Each
// benchmark is one avatar's worth: every entry once.
//
// isIndexBakedTexture is LLVOAvatar's; its body is repeated here. The
// dictionary is an instance of its own rather than LLAvatarAppearance's,
// which needs the avatar definition files read first.
//
// The output is not a verdict; a number is read against the same row from
// another build, on the same quiet machine.

#include "linden_common.h"

#include "llavatarappearancedefines.h"

#include "altestbenchmark.h"

using namespace LLAvatarAppearanceDefines;

namespace
{
    bool isIndexBakedTexture(const LLAvatarAppearanceDictionary& dictionary, ETextureIndex index)
    {
        return (index < 0 || index >= TEX_NUM_INDICES) ? false : dictionary.getTexture(index)->mIsBakedTexture;
    }

    TEST_CASE("llavatarappearancedefines_bench: updateTextures' lookups for every texture entry", "[llavatarappearancedefines_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();

        const LLAvatarAppearanceDictionary dictionary;

        BENCHMARK("getTexture, every entry")
        {
            S32 sum = 0;
            for (S32 te = 0; te < TEX_NUM_INDICES; ++te)
            {
                const LLAvatarAppearanceDictionary::TextureEntry* texture_dict = dictionary.getTexture(ETextureIndex(te));
                sum += texture_dict->mBakedTextureIndex;
            }
            return sum;
        };
        BENCHMARK("getTEWearableType, every entry")
        {
            S32 sum = 0;
            for (S32 te = 0; te < TEX_NUM_INDICES; ++te)
            {
                sum += dictionary.getTEWearableType(ETextureIndex(te));
            }
            return sum;
        };
        BENCHMARK("isIndexBakedTexture, every entry")
        {
            S32 sum = 0;
            for (S32 te = 0; te < TEX_NUM_INDICES; ++te)
            {
                sum += isIndexBakedTexture(dictionary, ETextureIndex(te)) ? 1 : 0;
            }
            return sum;
        };
        BENCHMARK("all three, as updateTextures makes them, every entry")
        {
            S32 sum = 0;
            for (S32 te = 0; te < TEX_NUM_INDICES; ++te)
            {
                const ETextureIndex index = ETextureIndex(te);
                sum += dictionary.getTEWearableType(index);
                const LLAvatarAppearanceDictionary::TextureEntry* texture_dict = dictionary.getTexture(index);
                if (texture_dict && texture_dict->mIsLocalTexture)
                {
                    sum += texture_dict->mBakedTextureIndex;
                }
                sum += isIndexBakedTexture(dictionary, index) ? 1 : 0;
            }
            return sum;
        };
    }
} // namespace
