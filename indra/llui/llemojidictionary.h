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

// A skin-tone / gender / hair alternate of a base emoji.
//   Tone:   0 = unset, 1..5 = light..dark.
//   Tone2:  0 = single-tone, 1..5 = second tone for pair variants
//           (couples kissing, holding hands). Always 0 unless Tone is
//           also non-zero.
//   Gender: -1 unset, 0 man, 1 woman, 2 person.
//   Hair:   "" / "red" / "curly" / "white" / "bald".
struct LLEmojiVariant
{
    std::string Character;
    U8 Tone { 0 };
    U8 Tone2 { 0 };
    S8 Gender { -1 };
    std::string Hair;
    std::list<std::string> ShortCodes;
};

struct LLEmojiDescriptor
{
    // UTF-8. Multi-codepoint emoji sequences (ZWJ families, flag
    // pairs, keycap, tag subdivision flags) are all addressed as a single
    // logical emoji at this layer.
    std::string Character;
    // Top-level emojibase group (e.g. "smileys and emotion").
    std::string Category;
    // Emojibase subgroup (e.g. "face-smiling"). Optional — empty for
    // entries whose data file didn't include a second category.
    std::string Subcategory;
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
    // Full sequence (not a single codepoint) so default-text emoji that
    // need U+FE0F (variation selector-16) for emoji presentation render
    // correctly — e.g. ✈ U+2708, 🛩 U+1F6E9, ⏲ U+23F2.
    std::string Character;
    std::list<std::string> Categories;
};

// ============================================================================
// LLEmojiSearchResult class
//

struct LLEmojiSearchResult
{
    std::string Character;
    std::string String;
    std::size_t Begin, End;

    LLEmojiSearchResult(std::string character, const std::string& string, std::size_t begin, std::size_t end)
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

class LLEmojiDictionary : public LLParamSingleton<LLEmojiDictionary>, public LLInitClass<LLEmojiDictionary>
{
    LLSINGLETON(LLEmojiDictionary);
    ~LLEmojiDictionary() override {};

public:
    typedef std::map<std::string, std::string> cat2cat_map_t;
    typedef std::map<std::string, const LLEmojiGroup*> cat2group_map_t;
    // Keyed by the full emoji sequence, in UTF-8, so ZWJ families and flag
    // pairs have their own entries distinct from their component codepoints.
    typedef std::map<std::string, const LLEmojiDescriptor*, std::less<>> emoji2descr_map_t;
    typedef std::map<std::string, const LLEmojiDescriptor*> code2descr_map_t;
    typedef std::map<std::string, std::vector<const LLEmojiDescriptor*>> cat2descrs_map_t;
    // Variant lookups: each pair is (base descriptor, index into base->Variants).
    typedef std::pair<const LLEmojiDescriptor*, S32> variant_ref_t;
    typedef std::map<std::string, variant_ref_t, std::less<>> variant_emoji2ref_map_t;
    typedef std::map<std::string, variant_ref_t> variant_code2ref_map_t;

    static void initClass();
    std::string findMatchingEmojis(const std::string& needle) const;
    static bool searchInShortCode(std::size_t& begin, std::size_t& end, const std::string& shortCode, const std::string& needle);
    void findByShortCode(std::vector<LLEmojiSearchResult>& result, const std::string& needle) const;
    const LLEmojiDescriptor* getDescriptorFromEmoji(std::string_view emoji) const;
    const LLEmojiDescriptor* getDescriptorFromShortCode(const std::string& short_code) const;
    // Resolve a shortcode to the text that should be inserted on
    // commit. For variant shortcodes (e.g. :thumbs_up_dark_skin_tone:)
    // this is the variant's character — getDescriptorFromShortCode
    // intentionally returns the *base* descriptor for those, so callers
    // that insert text must go through this helper instead of reading
    // descriptor->Character directly.
    std::string getEmojiFromShortCode(const std::string& short_code) const;
    std::string getNameFromEmoji(std::string_view emoji) const;
    // Single-codepoint predicate — used in per-codepoint iteration (font
    // fallback selection, shaping-run detection). For multi-codepoint
    // lookups, go through getDescriptorFromEmoji.
    bool isEmoji(llwchar ch) const;

    // Resolve a variant sequence (e.g. 👍🏿) back to its base descriptor.
    // Returns nullptr if the sequence isn't a known variant. When non-null
    // and outIndex is provided, *outIndex is set to the index into
    // base->Variants for the matched variant.
    const LLEmojiDescriptor* getBaseFromVariant(std::string_view emoji, S32* outIndex = nullptr) const;
    // Pick the best variant on a base descriptor for a given preference
    // across all four axes:
    //   tone   : 0 = no preference, 1..5 = light..dark
    //   gender : -1 = no preference, 0/1/2 = man/woman/person
    //   hair   : "" = no preference; "red"/"curly"/"white"/"bald"
    //   tone2  : 0 = single-tone preference, 1..5 = paired second tone
    //
    // The scorer prefers variants that match every axis the caller
    // specified AND avoid axes the caller left unset (so a "tone=3, no
    // gender, no hair, single tone" request prefers a clean tone-3
    // variant over one that also happens to carry a gender or hair tag).
    // Returns nullptr if no variant scores above zero (caller falls
    // back to the base character).
    const LLEmojiVariant* findVariant(const LLEmojiDescriptor& base,
                                       U8 tone,
                                       S8 gender,
                                       const std::string& hair = std::string(),
                                       U8 tone2 = 0) const;

    const std::vector<LLEmojiGroup>& getGroups() const { return mGroups; }
    const emoji2descr_map_t& getEmoji2Descr() const { return mEmoji2Descr; }
    const cat2descrs_map_t& getCategory2Descrs() const { return mCategory2Descrs; }
    const code2descr_map_t& getShortCode2Descr() const { return mShortCode2Descr; }

    // Populate the descriptor list from a parsed LLSD blob (the same
    // shape as emoji_characters.xml's top-level <array>). Public so unit
    // tests can build a synthetic dictionary without going through the
    // file-loader / gDirUtilp. Safe to call when mGroups is empty (the
    // "unknown category → others" branch is guarded).
    void loadEmojisFromSD(const LLSD& data);
    // Parse the <Variants> array inside one descriptor's LLSD <map>.
    // Public so tests can exercise it without spinning up a full
    // dictionary load.
    static std::vector<LLEmojiVariant> loadVariants(const LLSD& sd);

private:
    void loadTranslations();
    void loadGroups();
    void loadEmojis();

    static std::string loadIcon(const LLSD& sd);
    static std::list<std::string> loadCategories(const LLSD& sd);
    static std::list<std::string> loadShortCodes(const LLSD& sd);
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
