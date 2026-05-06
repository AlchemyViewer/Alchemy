/**
* @file llemojidictionary.cpp
* @brief Implementation of LLEmojiDictionary
*
* $LicenseInfo:firstyear=2014&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2014, Linden Research, Inc.
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

#include "lldir.h"
#include "llemojidictionary.h"
#include "llsdserialize.h"

#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/algorithm/transform.hpp>

// ============================================================================
// Constants
//

static const std::string SKINNED_EMOJI_FILENAME("emoji_characters.xml");
static const std::string SKINNED_CATEGORY_FILENAME("emoji_categories.xml");
static const std::string COMMON_GROUP_FILENAME("emoji_groups.xml");
static const std::string GROUP_NAME_SKIP("skip");
// https://www.compart.com/en/unicode/U+1F302
static const S32 GROUP_OTHERS_IMAGE_INDEX = 0x1F302;

// ============================================================================
// Helper functions
//

template<class T>
std::list<T> llsd_array_to_list(const LLSD& sd, std::function<void(T&)> mutator = {});

template<>
std::list<std::string> llsd_array_to_list(const LLSD& sd, std::function<void(std::string&)> mutator)
{
    std::list<std::string> result;
    for (LLSD::array_const_iterator it = sd.beginArray(), end = sd.endArray(); it != end; ++it)
    {
        const LLSD& entry = *it;
        if (!entry.isString())
            continue;

        result.push_back(entry.asStringRef());
        if (mutator)
        {
            mutator(result.back());
        }
    }
    return result;
}

struct emoji_filter_base
{
    emoji_filter_base(const std::string& needle)
    {
        // Search without the colon (if present) so the user can type ':food' and see all emojis in the 'Food' category
        mNeedle = (boost::starts_with(needle, ":")) ? needle.substr(1) : needle;
        LLStringUtil::toLower(mNeedle);
    }

protected:
    std::string mNeedle;
};

struct emoji_filter_shortcode_or_category_contains : public emoji_filter_base
{
    emoji_filter_shortcode_or_category_contains(const std::string& needle) : emoji_filter_base(needle) {}

    bool operator()(const LLEmojiDescriptor& descr) const
    {
        for (const auto& short_code : descr.ShortCodes)
        {
            if (boost::icontains(short_code, mNeedle))
                return true;
        }

        if (boost::icontains(descr.Category, mNeedle))
            return true;

        return false;
    }
};

// Match a needle against a descriptor's variant shortcodes, returning the
// LLWString of the first matching variant (so a search like ":thumbs_up_dark"
// surfaces 👍🏿 directly rather than the yellow base).
static const LLWString* find_variant_shortcode_match(const LLEmojiDescriptor& descr, const std::string& needle)
{
    for (const LLEmojiVariant& v : descr.Variants)
    {
        for (const std::string& code : v.ShortCodes)
        {
            if (boost::icontains(code, needle))
                return &v.Character;
        }
    }
    return nullptr;
}

std::string LLEmojiDescriptor::getShortCodes() const
{
    std::string result;
    for (const std::string& shortCode : ShortCodes)
    {
        if (!result.empty())
        {
            result += ", ";
        }
        result += shortCode;
    }
    return result;
}

// ============================================================================
// LLEmojiDictionary class
//

LLEmojiDictionary::LLEmojiDictionary()
{
}

// static
void LLEmojiDictionary::initClass()
{
    LLEmojiDictionary::createInstance();
    LLEmojiDictionary* pThis = LLEmojiDictionary::getInstance();

    pThis->loadTranslations();
    pThis->loadGroups();
    pThis->loadEmojis();
}

LLWString LLEmojiDictionary::findMatchingEmojis(const std::string& needle) const
{
    // Each descriptor's Character is itself an LLWString (possibly several
    // codepoints for ZWJ sequences). Concatenate them all into a single
    // LLWString so the caller can render the match as a run of emoji.
    //
    // For the variant-matching fallback we strip the leading colon so a
    // needle like ":thumbs_up_dark" walks the variants of every descriptor —
    // matching against shortcodes that the emoji_filter_* helpers already
    // strip the same way.
    std::string variant_needle = needle;
    if (!variant_needle.empty() && variant_needle.front() == ':')
        variant_needle.erase(variant_needle.begin());
    LLStringUtil::toLower(variant_needle);

    LLWString result;
    for (const LLEmojiDescriptor& d : mEmojis)
    {
        if (emoji_filter_shortcode_or_category_contains(needle)(d))
        {
            result += d.Character;
            continue;
        }
        // The base shortcode/category didn't match — try the variants. If
        // a single variant matches we surface that variant's character so
        // the user gets the tone-or-gender-specific emoji directly.
        if (!variant_needle.empty())
        {
            if (const LLWString* variant_char = find_variant_shortcode_match(d, variant_needle))
            {
                result += *variant_char;
            }
        }
    }
    return result;
}

// static
bool LLEmojiDictionary::searchInShortCode(std::size_t& begin, std::size_t& end, const std::string& shortCode, const std::string& needle)
{
    begin = 0;
    end = 1;
    std::size_t index = 1;
    // Search for begin
    char d = tolower(needle[index++]);
    while (end < shortCode.size())
    {
        char s = tolower(shortCode[end++]);
        if (s == d)
        {
            begin = end - 1;
            break;
        }
    }
    if (!begin)
        return false;
    // Search for end
    d = tolower(needle[index++]);
    if (!d)
        return true;
    while (end < shortCode.size() && index <= needle.size())
    {
        char s = tolower(shortCode[end++]);
        if (s == d)
        {
            if (index == needle.size())
                return true;
            d = tolower(needle[index++]);
            continue;
        }
        switch (s)
        {
        case L'-':
        case L'_':
        case L'+':
            continue;
        }
        break;
    }
    return false;
}

void LLEmojiDictionary::findByShortCode(
    std::vector<LLEmojiSearchResult>& result,
    const std::string& needle
) const
{
    result.clear();

    if (needle.empty() || needle.front() != ':')
        return;

    std::map<llwchar, std::vector<LLEmojiSearchResult>> results;

    for (const LLEmojiDescriptor& d : mEmojis)
    {
        if (!d.ShortCodes.empty())
        {
            const std::string& shortCode = d.ShortCodes.front();
            if (shortCode.size() >= needle.size() && shortCode.front() == needle.front())
            {
                std::size_t begin, end;
                if (searchInShortCode(begin, end, shortCode, needle))
                {
                    results[static_cast<llwchar>(begin)].emplace_back(d.Character, shortCode, begin, end);
                }
            }
        }
    }

    for (const auto& it : results)
    {
#ifdef __cpp_lib_containers_ranges
        result.append_range(it.second);
#else
        result.insert(result.end(), it.second.cbegin(), it.second.cend());
#endif
    }
}

const LLEmojiDescriptor* LLEmojiDictionary::getDescriptorFromEmoji(const LLWString& emoji) const
{
    const auto it = mEmoji2Descr.find(emoji);
    return (mEmoji2Descr.end() != it) ? it->second : nullptr;
}

const LLEmojiDescriptor* LLEmojiDictionary::getDescriptorFromShortCode(const std::string& short_code) const
{
    const auto it = mShortCode2Descr.find(short_code);
    if (mShortCode2Descr.end() != it)
        return it->second;
    // Variant shortcodes (e.g. :thumbs_up_dark_skin_tone:) resolve to their
    // base descriptor — the caller can substitute the variant's character
    // via getBaseFromVariant or findVariant.
    const auto vit = mVariantShortCode2Base.find(short_code);
    return (mVariantShortCode2Base.end() != vit) ? vit->second.first : nullptr;
}

const LLEmojiDescriptor* LLEmojiDictionary::getBaseFromVariant(const LLWString& emoji, S32* outIndex) const
{
    const auto it = mVariantEmoji2Base.find(emoji);
    if (mVariantEmoji2Base.end() == it)
        return nullptr;
    if (outIndex)
        *outIndex = it->second.second;
    return it->second.first;
}

const LLEmojiVariant* LLEmojiDictionary::findVariant(const LLEmojiDescriptor& base, U8 tone, S8 gender) const
{
    if (base.Variants.empty())
        return nullptr;
    if (tone == 0 && gender == -1)
        return nullptr; // no preference → caller uses base character.

    // Score each variant; best wins. An exact match on either axis is
    // worth 4; a "didn't ask for this axis, variant doesn't carry it"
    // bonus of 1 lets us prefer the cleanest match (e.g. for tone=3
    // gender=-1, prefer a variant with tone=3,gender=-1 over a variant
    // with tone=3,gender=1).
    const LLEmojiVariant* best = nullptr;
    int best_score = 0;
    for (const LLEmojiVariant& v : base.Variants)
    {
        int score = 0;
        if (tone != 0)
        {
            if (v.Tone == tone)
                score += 4;
        }
        else if (v.Tone == 0)
        {
            score += 1;
        }
        if (gender != -1)
        {
            if (v.Gender == gender)
                score += 4;
        }
        else if (v.Gender == -1)
        {
            score += 1;
        }
        if (score > best_score)
        {
            best = &v;
            best_score = score;
        }
    }
    // No partial-axis match at all → caller falls back to base.
    return best_score > 0 ? best : nullptr;
}

std::string LLEmojiDictionary::getNameFromEmoji(const LLWString& emoji) const
{
    const auto it = mEmoji2Descr.find(emoji);
    return (mEmoji2Descr.end() != it) ? it->second->ShortCodes.front() : LLStringUtil::null;
}

bool LLEmojiDictionary::isEmoji(llwchar ch) const
{
    // Currently used codes: A9,AE,203C,2049,2122,...,2B55,3030,303D,3297,3299,1F004,...,1FAF6
    if (ch == 0xA9 || ch == 0xAE || (ch >= 0x2000 && ch < 0x3300) || (ch >= 0x1F000 && ch < 0x20000))
    {
        // The dictionary is keyed by full LLWString sequences, so ask
        // whether the single-codepoint emoji exists as its own entry.
        return mEmoji2Descr.find(LLWString(1, ch)) != mEmoji2Descr.end();
    }

    return false;
}

void LLEmojiDictionary::loadTranslations()
{
    std::vector<std::string> filenames = gDirUtilp->findSkinnedFilenames(LLDir::XUI, SKINNED_CATEGORY_FILENAME, LLDir::CURRENT_SKIN);
    if (filenames.empty())
    {
        LL_WARNS() << "Emoji file categories not found" << LL_ENDL;
        return;
    }

    const std::string filename = filenames.back();
    llifstream file(filename.c_str());
    if (!file.is_open())
    {
        LL_WARNS() << "Emoji file categories failed to open" << LL_ENDL;
        return;
    }

    LL_DEBUGS() << "Loading emoji categories file at " << filename << LL_ENDL;

    LLSD data;
    LLSDSerialize::fromXML(data, file);
    if (data.isUndefined())
    {
        LL_WARNS() << "Emoji file categories missing or ill-formed" << LL_ENDL;
        return;
    }

    // Register translations for all categories
    for (LLSD::array_const_iterator it = data.beginArray(), end = data.endArray(); it != end; ++it)
    {
        const LLSD& sd = *it;
        const std::string& name = sd["Name"].asStringRef();
        const std::string& category = sd["Category"].asStringRef();
        if (!name.empty() && !category.empty())
        {
            mTranslations[name] = category;
        }
        else
        {
            LL_WARNS() << "Skipping invalid emoji category '" << name << "' => '" << category << "'" << LL_ENDL;
        }
    }
}

void LLEmojiDictionary::loadGroups()
{
    const std::string filename = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, COMMON_GROUP_FILENAME);
    llifstream file(filename.c_str());
    if (!file.is_open())
    {
        LL_WARNS() << "Emoji file groups failed to open" << LL_ENDL;
        return;
    }

    LL_DEBUGS() << "Loading emoji groups file at " << filename << LL_ENDL;

    LLSD data;
    LLSDSerialize::fromXML(data, file);
    if (data.isUndefined())
    {
        LL_WARNS() << "Emoji file groups missing or ill-formed" << LL_ENDL;
        return;
    }

    mGroups.clear();

    // Register all groups
    for (LLSD::array_const_iterator it = data.beginArray(), end = data.endArray(); it != end; ++it)
    {
        const LLSD& sd = *it;
        const std::string& name = sd["Name"].asStringRef();
        if (name == GROUP_NAME_SKIP)
        {
            mSkipCategories = loadCategories(sd);
            translateCategories(mSkipCategories);
        }
        else
        {
            // Add new group
            mGroups.emplace_back();
            LLEmojiGroup& group = mGroups.back();
            // Groups use a single-codepoint icon as a visual label; take the
            // first codepoint if the source happens to be multi-codepoint.
            const LLWString icon = loadIcon(sd);
            group.Character = icon.empty() ? 0 : icon[0];
            group.Categories = loadCategories(sd);
            translateCategories(group.Categories);

            for (const std::string& category : group.Categories)
            {
                mCategory2Group.insert(std::make_pair(category, &group));
            }
        }
    }

    // Add group "others"
    mGroups.emplace_back();
    mGroups.back().Character = GROUP_OTHERS_IMAGE_INDEX;
}

void LLEmojiDictionary::loadEmojis()
{
    std::vector<std::string> filenames = gDirUtilp->findSkinnedFilenames(LLDir::XUI, SKINNED_EMOJI_FILENAME, LLDir::CURRENT_SKIN);
    if (filenames.empty())
    {
        LL_WARNS() << "Emoji file characters not found" << LL_ENDL;
        return;
    }

    const std::string filename = filenames.back();
    llifstream file(filename.c_str());
    if (!file.is_open())
    {
        LL_WARNS() << "Emoji file characters failed to open" << LL_ENDL;
        return;
    }

    LL_DEBUGS() << "Loading emoji characters file at " << filename << LL_ENDL;

    LLSD data;
    LLSDSerialize::fromXML(data, file);
    if (data.isUndefined())
    {
        LL_WARNS() << "Emoji file characters missing or ill-formed" << LL_ENDL;
        return;
    }

    for (LLSD::array_const_iterator it = data.beginArray(), end = data.endArray(); it != end; ++it)
    {
        const LLSD& sd = *it;

        LLWString icon = loadIcon(sd);
        if (icon.empty())
        {
            LL_WARNS() << "Skipping invalid emoji descriptor (no icon)" << LL_ENDL;
            continue;
        }

        std::string category;
        std::list<std::string> categories = loadCategories(sd);
        if (categories.empty())
        {
            // Should already have a localization for "other symbols"
            category = "other symbols";
        }
        else
        {
            category = categories.front();
        }

        if (std::find(mSkipCategories.begin(), mSkipCategories.end(), category) != mSkipCategories.end())
        {
            // This category is listed for skip
            continue;
        }

        std::list<std::string> shortCodes = loadShortCodes(sd);
        if (shortCodes.empty())
        {
            LL_WARNS() << "Skipping invalid emoji descriptor (no shortCodes)" << LL_ENDL;
            continue;
        }

        if (mCategory2Group.find(category) == mCategory2Group.end())
        {
            // Add unknown category to "others" group
            mGroups.back().Categories.push_back(category);
            mCategory2Group.insert(std::make_pair(category, &mGroups.back()));
        }

        mEmojis.emplace_back();
        LLEmojiDescriptor& emoji = mEmojis.back();
        emoji.Character = icon;
        emoji.Category = category;
        emoji.ShortCodes = std::move(shortCodes);
        emoji.Variants = loadVariants(sd);

        mEmoji2Descr.insert(std::make_pair(std::move(icon), &emoji));
        mCategory2Descrs[category].push_back(&emoji);
        for (const std::string& shortCode : emoji.ShortCodes)
        {
            mShortCode2Descr.insert(std::make_pair(shortCode, &emoji));
        }
        for (S32 vi = 0; vi < (S32)emoji.Variants.size(); ++vi)
        {
            const LLEmojiVariant& v = emoji.Variants[vi];
            mVariantEmoji2Base.insert(std::make_pair(v.Character, std::make_pair(&emoji, vi)));
            for (const std::string& shortCode : v.ShortCodes)
            {
                mVariantShortCode2Base.insert(std::make_pair(shortCode, std::make_pair(&emoji, vi)));
            }
        }
    }
}

LLWString LLEmojiDictionary::loadIcon(const LLSD& sd)
{
    // The Character field may be a single codepoint or a multi-codepoint
    // sequence (ZWJ family, flag pair, keycap, tag subdivision). Return the
    // whole LLWString; the caller decides whether only a single codepoint
    // is acceptable (LLEmojiGroup icons collapse to the first).
    return utf8str_to_wstring(sd["Character"].asString());
}

std::list<std::string> LLEmojiDictionary::loadCategories(const LLSD& sd)
{
    static const std::string key("Categories");
    return llsd_array_to_list<std::string>(sd[key]);
}

std::list<std::string> LLEmojiDictionary::loadShortCodes(const LLSD& sd)
{
    static const std::string key("ShortCodes");
    auto toLower = [](std::string& str) { LLStringUtil::toLower(str); };
    return llsd_array_to_list<std::string>(sd[key], toLower);
}

std::vector<LLEmojiVariant> LLEmojiDictionary::loadVariants(const LLSD& sd)
{
    static const std::string key("Variants");
    std::vector<LLEmojiVariant> result;
    if (!sd.has(key))
        return result;

    const LLSD& arr = sd[key];
    if (!arr.isArray())
        return result;

    result.reserve(arr.size());
    for (LLSD::array_const_iterator it = arr.beginArray(), end = arr.endArray(); it != end; ++it)
    {
        const LLSD& vsd = *it;
        LLEmojiVariant v;
        v.Character = utf8str_to_wstring(vsd["Character"].asString());
        if (v.Character.empty())
            continue;
        if (vsd.has("Tone"))
            v.Tone = (U8)vsd["Tone"].asInteger();
        if (vsd.has("Gender"))
            v.Gender = (S8)vsd["Gender"].asInteger();
        v.ShortCodes = loadShortCodes(vsd);
        result.push_back(std::move(v));
    }
    return result;
}

void LLEmojiDictionary::translateCategories(std::list<std::string>& categories)
{
    for (std::string& category : categories)
    {
        auto it = mTranslations.find(category);
        if (it != mTranslations.end())
        {
            category = it->second;
        }
    }
}

// ============================================================================
