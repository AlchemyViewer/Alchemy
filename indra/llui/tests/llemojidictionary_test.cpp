/**
 * @file llemojidictionary_test.cpp
 * @brief Unit tests for the variant-aware additions to LLEmojiDictionary.
 *
 * Exercises the pure-CPU surface of the variant feature — the static
 * loadVariants(LLSD) parser and the findVariant(base, tone, gender)
 * scoring helper. The file-loading path (loadEmojis) requires gDirUtilp
 * + a populated skin tree and is left to manual / integration coverage.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llemojidictionary.h"
#include <catch2/catch_test_macros.hpp>
#include "llsd.h"
#include "llstring.h"

namespace
{
// Build a base descriptor with hand-rolled variants so the scoring
// helper has something to chew on without going through the file
// loader.
LLEmojiDescriptor make_thumbs_up_descriptor()
{
    LLEmojiDescriptor d;
    d.Character = "\xF0\x9F\x91\x8D"; // 👍
    d.ShortCodes.push_back("thumbs_up");
    for (U8 tone = 1; tone <= 5; ++tone)
    {
        LLEmojiVariant v;
        // The exact bytes don't matter for scoring tests — just need a
        // distinct sequence per variant so equality comparisons work.
        utf8str_append_cp(v.Character, (llwchar)0x1F44D);
        utf8str_append_cp(v.Character, (llwchar)(0x1F3FA + tone));
        v.Tone = tone;
        v.Gender = -1;
        d.Variants.push_back(std::move(v));
    }
    return d;
}

LLEmojiDescriptor make_astronaut_descriptor()
{
    LLEmojiDescriptor d;
    d.Character = "\xF0\x9F\xA7\x91\xE2\x80\x8D\xF0\x9F\x9A\x80"; // 🧑‍🚀
    d.ShortCodes.push_back("person_astronaut");
    // person, then person + 5 tones
    {
        LLEmojiVariant v;
        utf8str_append_cp(v.Character, (llwchar)0x1F9D1);
        utf8str_append_cp(v.Character, (llwchar)0x200D);
        utf8str_append_cp(v.Character, (llwchar)0x1F680);
        v.Tone = 0;
        v.Gender = 2;
        d.Variants.push_back(v);
    }
    for (U8 tone = 1; tone <= 5; ++tone)
    {
        LLEmojiVariant w;
        utf8str_append_cp(w.Character, (llwchar)0x1F9D1);
        utf8str_append_cp(w.Character, (llwchar)(0x1F3FA + tone));
        utf8str_append_cp(w.Character, (llwchar)0x200D);
        utf8str_append_cp(w.Character, (llwchar)0x1F680);
        w.Tone = tone;
        w.Gender = 2;
        d.Variants.push_back(w);
    }
    // man, then man + 5 tones
    {
        LLEmojiVariant m;
        utf8str_append_cp(m.Character, (llwchar)0x1F468);
        utf8str_append_cp(m.Character, (llwchar)0x200D);
        utf8str_append_cp(m.Character, (llwchar)0x1F680);
        m.Tone = 0;
        m.Gender = 0;
        d.Variants.push_back(m);
    }
    for (U8 tone = 1; tone <= 5; ++tone)
    {
        LLEmojiVariant w;
        utf8str_append_cp(w.Character, (llwchar)0x1F468);
        utf8str_append_cp(w.Character, (llwchar)(0x1F3FA + tone));
        utf8str_append_cp(w.Character, (llwchar)0x200D);
        utf8str_append_cp(w.Character, (llwchar)0x1F680);
        w.Tone = tone;
        w.Gender = 0;
        d.Variants.push_back(w);
    }
    return d;
}
}

namespace
{
    // A dictionary for each test, and none left after it -- unless one was
    // there already, which is used as it is and left.
    struct llemojidictionary_data
    {
        const bool mOwn = !LLEmojiDictionary::instanceExists();

        llemojidictionary_data()
        {
            if (mOwn)
            {
                LLEmojiDictionary::initParamSingleton();
            }
        }
        ~llemojidictionary_data()
        {
            if (mOwn)
            {
                LLEmojiDictionary::deleteSingleton();
            }
        }
    };
}

namespace
{
    // findVariant: exact tone match wins over partial axis matches.
    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: findVariant picks the variant of the tone asked for", "[llemojidictionary]")
    {
        LLEmojiDescriptor d = make_thumbs_up_descriptor();
        const LLEmojiVariant* v = LLEmojiDictionary::instance().findVariant(d, 3, -1);
        REQUIRE(v != nullptr);
        CHECK(v->Tone == U8(3));
    }

    // tone=0, gender=-1: no preference, so the caller uses the base.
    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: findVariant with no tone or gender asked for returns null", "[llemojidictionary]")
    {
        LLEmojiDescriptor d = make_thumbs_up_descriptor();
        const LLEmojiVariant* v = LLEmojiDictionary::instance().findVariant(d, 0, -1);
        CHECK(v == nullptr);
    }

    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: findVariant on a base with no variants returns null", "[llemojidictionary]")
    {
        LLEmojiDescriptor empty;
        empty.Character = "\xF0\x9F\x92\xA9"; // 💩
        const LLEmojiVariant* v = LLEmojiDictionary::instance().findVariant(empty, 3, -1);
        CHECK(v == nullptr);
    }

    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: findVariant asked for a tone and a gender matches both", "[llemojidictionary]")
    {
        LLEmojiDescriptor d = make_astronaut_descriptor();
        const LLEmojiVariant* v = LLEmojiDictionary::instance().findVariant(d, 3, 0); // tone=3, man
        REQUIRE(v != nullptr);
        CHECK(v->Tone == U8(3));
        CHECK(v->Gender == S8(0));
    }

    // findVariant: prefer the cleanest match — gender=-1 user shouldn't
    // get a gendered variant when one of the same tone that carries no
    // gender exists.
    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: findVariant with no gender asked for picks the variant that carries none", "[llemojidictionary]")
    {
        // The astronaut's tone-3 variants all carry a gender, person (2)
        // as much as man (0), and score alike. One that carries none goes
        // after them all, so that the +1 for not asserting a gender we
        // didn't ask for is what picks it, and not the order.
        LLEmojiDescriptor d = make_astronaut_descriptor();
        LLEmojiVariant clean;
        utf8str_append_cp(clean.Character, (llwchar)0x1F9D1);
        utf8str_append_cp(clean.Character, (llwchar)(0x1F3FA + 3));
        clean.Tone = 3;
        clean.Gender = -1;
        d.Variants.push_back(clean);

        const LLEmojiVariant* v = LLEmojiDictionary::instance().findVariant(d, 3, -1);
        REQUIRE(v != nullptr);
        CHECK(v->Tone == U8(3));
        CHECK(v->Gender == S8(-1));
    }

    // findVariant: partial-axis match still beats nothing — user asks
    // for tone+gender combo we don't have, but a tone-only variant
    // matching the tone is still better than the base.
    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: findVariant with no gender match falls back to the tone match", "[llemojidictionary]")
    {
        LLEmojiDescriptor d = make_thumbs_up_descriptor();
        // Ask tone=3, gender=woman, but the descriptor has only
        // gender-neutral variants. Should still return the tone-3
        // variant (partial match).
        const LLEmojiVariant* v = LLEmojiDictionary::instance().findVariant(d, 3, 1);
        REQUIRE(v != nullptr);
        CHECK(v->Tone == U8(3));
    }

    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: loadVariants parses the LLSD blob the generator produces", "[llemojidictionary]")
    {
        LLSD parent;
        parent["Character"] = "\xF0\x9F\x91\x8D";
        LLSD variants = LLSD::emptyArray();
        LLSD v;
        v["Character"] = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBF"; // 👍🏿
        v["Tone"] = 5;
        v["ShortCodes"] = LLSD::emptyArray();
        v["ShortCodes"].append(":thumbs_up_dark_skin_tone:");
        variants.append(v);
        parent["Variants"] = variants;

        std::vector<LLEmojiVariant> out = LLEmojiDictionary::loadVariants(parent);
        REQUIRE(out.size() == size_t(1));
        CHECK(out[0].Tone == U8(5));
        UNSCOPED_INFO("no Gender given: unset");
        CHECK(out[0].Gender == S8(-1));
        REQUIRE(out[0].ShortCodes.size() == size_t(1));
        CHECK(out[0].ShortCodes.front() == std::string(":thumbs_up_dark_skin_tone:"));
    }

    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: loadVariants with no Variants key returns none", "[llemojidictionary]")
    {
        LLSD parent;
        parent["Character"] = "\xF0\x9F\x92\xA9";
        std::vector<LLEmojiVariant> out = LLEmojiDictionary::loadVariants(parent);
        CHECK(out.empty());
    }

    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: loadVariants skips entries with an empty Character", "[llemojidictionary]")
    {
        LLSD parent;
        parent["Character"] = "\xF0\x9F\x91\x8D";
        LLSD variants = LLSD::emptyArray();
        LLSD bad;
        bad["Character"] = ""; // empty — should be dropped
        variants.append(bad);
        LLSD good;
        good["Character"] = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBB";
        good["Tone"] = 1;
        variants.append(good);
        parent["Variants"] = variants;

        std::vector<LLEmojiVariant> out = LLEmojiDictionary::loadVariants(parent);
        REQUIRE(out.size() == size_t(1));
        CHECK(out[0].Tone == U8(1));
    }

    // Helper: build a synthetic dictionary with thumbs_up + 5 tone
    // variants via the loadEmojisFromSD path. Keeps the assembly
    // local to the tests that need it.
    static LLSD make_thumbs_up_dictionary_blob()
    {
        LLSD root = LLSD::emptyArray();
        LLSD entry;
        entry["Character"] = "\xF0\x9F\x91\x8D"; // 👍
        LLSD shortcodes = LLSD::emptyArray();
        shortcodes.append(":thumbs_up:");
        entry["ShortCodes"] = shortcodes;
        LLSD categories = LLSD::emptyArray();
        categories.append("hands");
        entry["Categories"] = categories;

        // Five tone variants.
        LLSD variants = LLSD::emptyArray();
        const char* chars[5] = {
            "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBB", // 👍🏻
            "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBC", // 👍🏼
            "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", // 👍🏽
            "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBE", // 👍🏾
            "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBF", // 👍🏿
        };
        const char* codes[5] = {
            ":thumbs_up_light_skin_tone:",
            ":thumbs_up_medium_light_skin_tone:",
            ":thumbs_up_medium_skin_tone:",
            ":thumbs_up_medium_dark_skin_tone:",
            ":thumbs_up_dark_skin_tone:",
        };
        for (S32 i = 0; i < 5; ++i)
        {
            LLSD v;
            v["Character"] = chars[i];
            v["Tone"] = i + 1;
            LLSD vsc = LLSD::emptyArray();
            vsc.append(codes[i]);
            v["ShortCodes"] = vsc;
            variants.append(v);
        }
        entry["Variants"] = variants;
        root.append(entry);
        return root;
    }

    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: getEmojiFromShortCode gives the base character for a base shortcode", "[llemojidictionary]")
    {
        LLEmojiDictionary::instance().loadEmojisFromSD(make_thumbs_up_dictionary_blob());
        CHECK(LLEmojiDictionary::instance().getEmojiFromShortCode(":thumbs_up:") == "\xF0\x9F\x91\x8D");
    }

    // getEmojiFromShortCode returns the VARIANT character for a variant
    // shortcode — this is the regression we're testing for.
    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: getEmojiFromShortCode gives a variant shortcode its variant character", "[llemojidictionary]")
    {
        LLEmojiDictionary::instance().loadEmojisFromSD(make_thumbs_up_dictionary_blob());
        CHECK(LLEmojiDictionary::instance().getEmojiFromShortCode(":thumbs_up_dark_skin_tone:")
                   == "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBF");
    }

    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: getEmojiFromShortCode gives an empty string for an unknown shortcode", "[llemojidictionary]")
    {
        LLEmojiDictionary::instance().loadEmojisFromSD(make_thumbs_up_dictionary_blob());
        CHECK(LLEmojiDictionary::instance().getEmojiFromShortCode(":nonsense:").empty());
    }

    // findByShortCode prefix-matching now surfaces variants — typing
    // ":thumbs_up_d" should produce results that include the dark variant
    // (with the variant Character, not the base Character).
    TEST_CASE_METHOD(llemojidictionary_data, "llemojidictionary: findByShortCode on a prefix finds a variant by its own character", "[llemojidictionary]")
    {
        LLEmojiDictionary::instance().loadEmojisFromSD(make_thumbs_up_dictionary_blob());

        std::vector<LLEmojiSearchResult> results;
        LLEmojiDictionary::instance().findByShortCode(results, ":thumbs_up_d");
        CHECK_FALSE(results.empty());

        const std::string expected_dark = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBF";
        bool saw_dark = false;
        for (const auto& r : results)
        {
            if (r.Character == expected_dark)
            {
                saw_dark = true;
                break;
            }
        }
        CHECK(saw_dark);
    }
}
