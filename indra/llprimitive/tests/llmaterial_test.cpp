/**
 * @file llmaterial_test.cpp
 * @brief Unit tests for LLMaterial — primarily for the getHash() cache and
 *        the per-field hash distinguishability.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include <catch2/catch_test_macros.hpp>

#include "../llmaterial.h"

namespace
{
    // Two distinct UUIDs used as before/after values for ID setters.
    static const LLUUID kIdA("11111111-2222-3333-4444-555555555555");
    static const LLUUID kIdB("66666666-7777-8888-9999-aaaaaaaaaaaa");

    // Drive a single-argument LLMaterial setter with two distinct values and
    // verify (1) writing the second value invalidates and changes the cached
    // hash, and (2) writing the first value back restores the original hash
    // (i.e. the hash is a pure function of state with no path dependence).
    template <typename Setter, typename Value>
    static void require_setter_round_trip(const std::string& name,
                                          Setter setter,
                                          const Value& before_value,
                                          const Value& after_value)
    {
        CAPTURE(name);
        LLMaterial m;
        (m.*setter)(before_value);
        const LLUUID before = m.getHash();

        (m.*setter)(after_value);
        REQUIRE(m.getHash() != before);

        (m.*setter)(before_value);
        REQUIRE(m.getHash() == before);
    }

    // Hash is deterministic for repeated reads with no mutation. The first
    // call populates the cache; subsequent calls hit it.
    TEST_CASE("llmaterial: getHash is deterministic for repeated reads with no mutation", "[llmaterial]")
    {
        LLMaterial m;
        const LLUUID a = m.getHash();
        const LLUUID b = m.getHash();
        const LLUUID c = m.getHash();
        CHECK(a == b);
        CHECK(b == c);
        CHECK(a.notNull());
    }

    // Every individual setter invalidates the cache AND changes the hash to
    // a value tied to the new state. The pair (changed-when-set / restored-
    // when-set-back) proves the cache invalidation is wired up *and* that the
    // hash function is a pure function of the member state, not of mutation
    // history.
    TEST_CASE("llmaterial: every setter changes the hash and setting back restores it", "[llmaterial]")
    {
        // ID setters.
        require_setter_round_trip("setNormalID",
            &LLMaterial::setNormalID,   LLUUID::null,   kIdA);
        require_setter_round_trip("setSpecularID",
            &LLMaterial::setSpecularID, LLUUID::null,   kIdB);

        // F32 setters (individual axes).
        require_setter_round_trip("setNormalOffsetX",
            &LLMaterial::setNormalOffsetX,   0.0f, 1.5f);
        require_setter_round_trip("setNormalOffsetY",
            &LLMaterial::setNormalOffsetY,   0.0f, 2.5f);
        require_setter_round_trip("setNormalRepeatX",
            &LLMaterial::setNormalRepeatX,   1.0f, 3.5f);
        require_setter_round_trip("setNormalRepeatY",
            &LLMaterial::setNormalRepeatY,   1.0f, 4.5f);
        require_setter_round_trip("setNormalRotation",
            &LLMaterial::setNormalRotation,  0.0f, 1.57f);
        require_setter_round_trip("setSpecularOffsetX",
            &LLMaterial::setSpecularOffsetX, 0.0f, 5.5f);
        require_setter_round_trip("setSpecularOffsetY",
            &LLMaterial::setSpecularOffsetY, 0.0f, 6.5f);
        require_setter_round_trip("setSpecularRepeatX",
            &LLMaterial::setSpecularRepeatX, 1.0f, 7.5f);
        require_setter_round_trip("setSpecularRepeatY",
            &LLMaterial::setSpecularRepeatY, 1.0f, 8.5f);
        require_setter_round_trip("setSpecularRotation",
            &LLMaterial::setSpecularRotation, 0.0f, 2.71f);

        // LLColor4U setter.
        require_setter_round_trip<void(LLMaterial::*)(const LLColor4U&), LLColor4U>(
            "setSpecularLightColor",
            &LLMaterial::setSpecularLightColor,
            LLColor4U(0, 0, 0, 0),
            LLColor4U(11, 22, 33, 44));

        // U8 setters.
        require_setter_round_trip<void(LLMaterial::*)(U8), U8>(
            "setSpecularLightExponent",
            &LLMaterial::setSpecularLightExponent, U8(0), U8(99));
        require_setter_round_trip<void(LLMaterial::*)(U8), U8>(
            "setEnvironmentIntensity",
            &LLMaterial::setEnvironmentIntensity, U8(0), U8(123));
        require_setter_round_trip<void(LLMaterial::*)(U8), U8>(
            "setDiffuseAlphaMode",
            &LLMaterial::setDiffuseAlphaMode,
            (U8)LLMaterial::DIFFUSE_ALPHA_MODE_BLEND,
            (U8)LLMaterial::DIFFUSE_ALPHA_MODE_MASK);
        require_setter_round_trip<void(LLMaterial::*)(U8), U8>(
            "setAlphaMaskCutoff",
            &LLMaterial::setAlphaMaskCutoff, U8(0), U8(200));
    }

    // The paired-axis setters share the same underlying members as the X/Y
    // individual setters and must produce the identical hash for the same
    // logical state. This pins that the combined and split write paths agree.
    TEST_CASE("llmaterial: paired-axis setters hash the same as the split X and Y setters", "[llmaterial]")
    {
        LLMaterial split;
        split.setNormalOffsetX(0.5f);
        split.setNormalOffsetY(1.5f);
        split.setNormalRepeatX(2.5f);
        split.setNormalRepeatY(3.5f);
        split.setSpecularOffsetX(4.5f);
        split.setSpecularOffsetY(5.5f);
        split.setSpecularRepeatX(6.5f);
        split.setSpecularRepeatY(7.5f);

        LLMaterial paired;
        paired.setNormalOffset(0.5f, 1.5f);
        paired.setNormalRepeat(2.5f, 3.5f);
        paired.setSpecularOffset(4.5f, 5.5f);
        paired.setSpecularRepeat(6.5f, 7.5f);

        CHECK(split.getHash() == paired.getHash());
    }

    // Hash distinguishes which slot a value lives in. If two materials carry
    // the same scalar but in different members, the hashes must differ —
    // otherwise the packing order or hashing scheme has aliased two fields.
    TEST_CASE("llmaterial: the hash distinguishes which slot a value lives in", "[llmaterial]")
    {
        // mNormalOffsetX vs mNormalOffsetY
        {
            LLMaterial x; x.setNormalOffsetX(1.0f);
            LLMaterial y; y.setNormalOffsetY(1.0f);
            UNSCOPED_INFO("normal offset X vs Y");
            CHECK(x.getHash() != y.getHash());
        }
        // mNormalRepeatX vs mNormalRepeatY
        {
            LLMaterial x; x.setNormalRepeatX(2.0f);
            LLMaterial y; y.setNormalRepeatY(2.0f);
            UNSCOPED_INFO("normal repeat X vs Y");
            CHECK(x.getHash() != y.getHash());
        }
        // mSpecularOffsetX vs mSpecularOffsetY
        {
            LLMaterial x; x.setSpecularOffsetX(3.0f);
            LLMaterial y; y.setSpecularOffsetY(3.0f);
            UNSCOPED_INFO("specular offset X vs Y");
            CHECK(x.getHash() != y.getHash());
        }
        // mSpecularRepeatX vs mSpecularRepeatY
        {
            LLMaterial x; x.setSpecularRepeatX(4.0f);
            LLMaterial y; y.setSpecularRepeatY(4.0f);
            UNSCOPED_INFO("specular repeat X vs Y");
            CHECK(x.getHash() != y.getHash());
        }
        // Same UUIDs swapped between mNormalID and mSpecularID.
        {
            LLMaterial ab; ab.setNormalID(kIdA); ab.setSpecularID(kIdB);
            LLMaterial ba; ba.setNormalID(kIdB); ba.setSpecularID(kIdA);
            UNSCOPED_INFO("normal and specular IDs swapped");
            CHECK(ab.getHash() != ba.getHash());
        }
        // Normal vs specular rotation
        {
            LLMaterial n; n.setNormalRotation(1.0f);
            LLMaterial s; s.setSpecularRotation(1.0f);
            UNSCOPED_INFO("normal vs specular rotation");
            CHECK(n.getHash() != s.getHash());
        }
        // Trailing U8 fields all share size/alignment — make sure the packer
        // doesn't accidentally read them as a single uniform word.
        {
            LLMaterial e; e.setSpecularLightExponent(7);
            LLMaterial i; i.setEnvironmentIntensity(7);
            LLMaterial d; d.setDiffuseAlphaMode((U8)LLMaterial::DIFFUSE_ALPHA_MODE_MASK);
            LLMaterial c; c.setAlphaMaskCutoff(7);
            UNSCOPED_INFO("exponent vs intensity");
            CHECK(e.getHash() != i.getHash());
            UNSCOPED_INFO("intensity vs alpha mode");
            CHECK(i.getHash() != d.getHash());
            UNSCOPED_INFO("alpha mode vs cutoff");
            CHECK(d.getHash() != c.getHash());
        }
    }

    // fromLLSD touches every member at once. Verify it invalidates the cache
    // and that hashes computed before vs. after differ.
    TEST_CASE("llmaterial: fromLLSD invalidates the cache and round-trips the hash", "[llmaterial]")
    {
        LLMaterial m;
        const LLUUID before = m.getHash();

        const LLSD sd = m.asLLSD();
        // Roundtripping the same LLSD should restore the same hash.
        LLMaterial back;
        back.setAlphaMaskCutoff(99);           // state altered
        CHECK(back.getHash() != before);       // and its hash cached
        back.fromLLSD(sd);
        CHECK(back.getHash() == before);

        // Now perturb a field through LLSD and check the hash moves.
        LLMaterial perturbed;
        perturbed.fromLLSD(sd);
        CHECK(perturbed.getHash() == before);  // caches the unperturbed hash
        LLSD perturbed_sd = sd;
        perturbed_sd["EnvIntensity"] = 42;
        perturbed.fromLLSD(perturbed_sd);
        CHECK(perturbed.getHash() != before);
    }

    // Copy semantics: a copy carries the cached state, but mutations on the
    // copy invalidate only the copy's cache.
    TEST_CASE("llmaterial: a copy carries the cached hash and its changes stay its own", "[llmaterial]")
    {
        LLMaterial src;
        src.setAlphaMaskCutoff(42);
        const LLUUID src_hash = src.getHash();

        // Copy after the source's cache has been populated.
        LLMaterial copy_warm(src);
        CHECK(copy_warm.getHash() == src_hash);

        // Copy from a source whose cache is dirty (never read).
        LLMaterial dirty_src;
        dirty_src.setEnvironmentIntensity(7);  // marks dirty without reading
        LLMaterial copy_cold(dirty_src);
        CHECK(copy_cold.getHash() == dirty_src.getHash());

        // Mutating the copy must not bleed back into the source.
        copy_warm.setEnvironmentIntensity(99);
        CHECK(copy_warm.getHash() != src_hash);
        CHECK(src.getHash() == src_hash);
    }

    // Redundantly writing the same value through a setter must still result
    // in the same hash on the next read (the cache may be marked dirty and
    // recomputed, but the recomputed value matches because state didn't move).
    TEST_CASE("llmaterial: redundant setter writes keep the hash", "[llmaterial]")
    {
        LLMaterial m;
        m.setAlphaMaskCutoff(50);
        const LLUUID h1 = m.getHash();

        m.setAlphaMaskCutoff(50);    // redundant
        m.setEnvironmentIntensity(0); // already the default
        m.setNormalRotation(0.0f);    // already the default
        const LLUUID h2 = m.getHash();
        CHECK(h1 == h2);
    }

    // operator== iff hashes match for materials of equal state.
    TEST_CASE("llmaterial: operator== holds exactly when the hashes match", "[llmaterial]")
    {
        LLMaterial a, b;
        a.setNormalID(kIdA);
        a.setSpecularID(kIdB);
        a.setSpecularLightColor(LLColor4U(10, 20, 30, 40));
        a.setEnvironmentIntensity(50);

        b.setNormalID(kIdA);
        b.setSpecularID(kIdB);
        b.setSpecularLightColor(LLColor4U(10, 20, 30, 40));
        b.setEnvironmentIntensity(50);

        CHECK(a == b);
        CHECK(a.getHash() == b.getHash());

        // One-bit perturbation must break both equalities together.
        b.setEnvironmentIntensity(51);
        CHECK(a != b);
        CHECK(a.getHash() != b.getHash());
    }
}
