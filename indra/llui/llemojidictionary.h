/**
* @file llemojidictionary.h
* @brief Header file for LLEmojiDictionary
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

#pragma once

#include "lldictionary.h"
#include "llinitdestroyclass.h"
#include "llsingleton.h"

// ============================================================================
// LLEmojiVariant / LLEmojiDescriptor classes
//

// A skin-tone / gender alternate of a base emoji. Tone is 0 when the variant
// has no tone axis (a pure gender variant), 1..5 = light..dark. Gender is
// -1 when unset, 0 = man, 1 = woman, 2 = person.
struct LLEmojiVariant
{
    LLWString Character;
    U8 Tone { 0 };
    S8 Gender { -1 };
    std::list<std::string> ShortCodes;
};

struct LLEmojiDescriptor
{
    // LLWString because multi-codepoint emoji sequences (ZWJ families, flag
    // pairs, keycap, tag subdivision flags) are all addressed as a single
    // logical emoji at this layer.
    LLWString Character;
    std::string Category;
    std::list<std::string> ShortCodes;
    // Skin-tone / gender alternates derived from emojibase data. Empty for
    // emoji that have no axes of variation.
    std::vector<LLEmojiVariant> Variants;
    std::string getShortCodes() const;
};

// ============================================================================
// LLEmojiGroup class
//

struct LLEmojiGroup
{
    llwchar Character;
    std::list<std::string> Categories;
};

// ============================================================================
// LLEmojiSearchResult class
//

struct LLEmojiSearchResult
{
    LLWString Character;
    std::string String;
    std::size_t Begin, End;

    LLEmojiSearchResult(LLWString character, const std::string& string, std::size_t begin, std::size_t end)
        : Character(std::move(character))
        , String(string)
        , Begin(begin)
        , End(end)
    {
    }
};

// ============================================================================
// LLEmojiDictionary class
//

class LLEmojiDictionary : public LLSimpleton<LLEmojiDictionary>, public LLInitClass<LLEmojiDictionary>
{
public:
    LLEmojiDictionary();
    ~LLEmojiDictionary() = default;

    typedef std::map<std::string, std::string> cat2cat_map_t;
    typedef std::map<std::string, const LLEmojiGroup*> cat2group_map_t;
    // Keyed by full emoji sequence (LLWString) so ZWJ families and flag
    // pairs have their own entries distinct from their component codepoints.
    typedef std::map<LLWString, const LLEmojiDescriptor*> emoji2descr_map_t;
    typedef std::map<std::string, const LLEmojiDescriptor*> code2descr_map_t;
    typedef std::map<std::string, std::vector<const LLEmojiDescriptor*>> cat2descrs_map_t;
    // Variant lookups: each pair is (base descriptor, index into base->Variants).
    typedef std::pair<const LLEmojiDescriptor*, S32> variant_ref_t;
    typedef std::map<LLWString, variant_ref_t> variant_emoji2ref_map_t;
    typedef std::map<std::string, variant_ref_t> variant_code2ref_map_t;

    static void initClass();
    LLWString findMatchingEmojis(const std::string& needle) const;
    static bool searchInShortCode(std::size_t& begin, std::size_t& end, const std::string& shortCode, const std::string& needle);
    void findByShortCode(std::vector<LLEmojiSearchResult>& result, const std::string& needle) const;
    const LLEmojiDescriptor* getDescriptorFromEmoji(const LLWString& emoji) const;
    const LLEmojiDescriptor* getDescriptorFromShortCode(const std::string& short_code) const;
    std::string getNameFromEmoji(const LLWString& emoji) const;
    // Single-codepoint predicate — used in per-codepoint iteration (font
    // fallback selection, shaping-run detection). For multi-codepoint
    // lookups, go through getDescriptorFromEmoji.
    bool isEmoji(llwchar ch) const;

    // Resolve a variant sequence (e.g. 👍🏿) back to its base descriptor.
    // Returns nullptr if the sequence isn't a known variant. When non-null
    // and outIndex is provided, *outIndex is set to the index into
    // base->Variants for the matched variant.
    const LLEmojiDescriptor* getBaseFromVariant(const LLWString& emoji, S32* outIndex = nullptr) const;
    // Pick the best variant on a base descriptor for a given (tone, gender)
    // preference. tone is 0 (no preference) or 1..5; gender is -1
    // (no preference) or 0/1/2 (man/woman/person). Returns nullptr if the
    // base has no variants matching either axis (caller should fall back to
    // the base character).
    const LLEmojiVariant* findVariant(const LLEmojiDescriptor& base, U8 tone, S8 gender) const;

    const std::vector<LLEmojiGroup>& getGroups() const { return mGroups; }
    const emoji2descr_map_t& getEmoji2Descr() const { return mEmoji2Descr; }
    const cat2descrs_map_t& getCategory2Descrs() const { return mCategory2Descrs; }
    const code2descr_map_t& getShortCode2Descr() const { return mShortCode2Descr; }

private:
    void loadTranslations();
    void loadGroups();
    void loadEmojis();

    static LLWString loadIcon(const LLSD& sd);
    static std::list<std::string> loadCategories(const LLSD& sd);
    static std::list<std::string> loadShortCodes(const LLSD& sd);
    static std::vector<LLEmojiVariant> loadVariants(const LLSD& sd);
    void translateCategories(std::list<std::string>& categories);

private:
    std::vector<LLEmojiGroup> mGroups;
    std::list<LLEmojiDescriptor> mEmojis;
    std::list<std::string> mSkipCategories;

    cat2cat_map_t mTranslations;
    cat2group_map_t mCategory2Group;
    emoji2descr_map_t mEmoji2Descr;
    cat2descrs_map_t mCategory2Descrs;
    code2descr_map_t mShortCode2Descr;
    variant_emoji2ref_map_t mVariantEmoji2Base;
    variant_code2ref_map_t mVariantShortCode2Base;
};

// ============================================================================
