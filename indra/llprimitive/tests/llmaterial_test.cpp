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
#include "lltut.h"

#include "../llmaterial.h"
#include "lluuid.cpp"

namespace tut
{
    struct llmaterial_data { };
    typedef test_group<llmaterial_data> llmaterial_t;
    typedef llmaterial_t::object llmaterial_object_t;
    tut::llmaterial_t tut_llmaterial("llmaterial");

    // Two distinct UUIDs used as before/after values for ID setters.
    static const LLUUID kIdA("11111111-2222-3333-4444-555555555555");
    static const LLUUID kIdB("66666666-7777-8888-9999-aaaaaaaaaaaa");

    // Drive a single-argument LLMaterial setter with two distinct values and
    // verify (1) writing the second value invalidates and changes the cached
    // hash, and (2) writing the first value back restores the original hash
    // (i.e. the hash is a pure function of state with no path dependence).
    template <typename Setter, typename Value>
    static void ensure_setter_round_trip(const std::string& name,
                                         Setter setter,
                                         const Value& before_value,
                                         const Value& after_value)
    {
        LLMaterial m;
        (m.*setter)(before_value);
        const LLUUID before = m.getHash();

        (m.*setter)(after_value);
        ensure((name + ": new value changes hash").c_str(),
            m.getHash() != before);

        (m.*setter)(before_value);
        ensure_equals((name + ": restoring value restores hash").c_str(),
            m.getHash(), before);
    }

    // Hash is deterministic for repeated reads with no mutation. The first
    // call populates the cache; subsequent calls hit it.
    template<> template<>
    void llmaterial_object_t::test<1>()
    {
        LLMaterial m;
        const LLUUID a = m.getHash();
        const LLUUID b = m.getHash();
        const LLUUID c = m.getHash();
        ensure_equals("repeated reads", a, b);
        ensure_equals("repeated reads", b, c);
        ensure("hash is not the null UUID", a.notNull());
    }

    // Every individual setter invalidates the cache AND changes the hash to
    // a value tied to the new state. The pair (changed-when-set / restored-
    // when-set-back) proves the cache invalidation is wired up *and* that the
    // hash function is a pure function of the member state, not of mutation
    // history.
    template<> template<>
    void llmaterial_object_t::test<2>()
    {
        // ID setters.
        ensure_setter_round_trip("setNormalID",
            &LLMaterial::setNormalID,   LLUUID::null,   kIdA);
        ensure_setter_round_trip("setSpecularID",
            &LLMaterial::setSpecularID, LLUUID::null,   kIdB);

        // F32 setters (individual axes).
        ensure_setter_round_trip("setNormalOffsetX",
            &LLMaterial::setNormalOffsetX,   0.0f, 1.5f);
        ensure_setter_round_trip("setNormalOffsetY",
            &LLMaterial::setNormalOffsetY,   0.0f, 2.5f);
        ensure_setter_round_trip("setNormalRepeatX",
            &LLMaterial::setNormalRepeatX,   1.0f, 3.5f);
        ensure_setter_round_trip("setNormalRepeatY",
            &LLMaterial::setNormalRepeatY,   1.0f, 4.5f);
        ensure_setter_round_trip("setNormalRotation",
            &LLMaterial::setNormalRotation,  0.0f, 1.57f);
        ensure_setter_round_trip("setSpecularOffsetX",
            &LLMaterial::setSpecularOffsetX, 0.0f, 5.5f);
        ensure_setter_round_trip("setSpecularOffsetY",
            &LLMaterial::setSpecularOffsetY, 0.0f, 6.5f);
        ensure_setter_round_trip("setSpecularRepeatX",
            &LLMaterial::setSpecularRepeatX, 1.0f, 7.5f);
        ensure_setter_round_trip("setSpecularRepeatY",
            &LLMaterial::setSpecularRepeatY, 1.0f, 8.5f);
        ensure_setter_round_trip("setSpecularRotation",
            &LLMaterial::setSpecularRotation, 0.0f, 2.71f);

        // LLColor4U setter.
        ensure_setter_round_trip<void(LLMaterial::*)(const LLColor4U&), LLColor4U>(
            "setSpecularLightColor",
            &LLMaterial::setSpecularLightColor,
            LLColor4U(0, 0, 0, 0),
            LLColor4U(11, 22, 33, 44));

        // U8 setters.
        ensure_setter_round_trip<void(LLMaterial::*)(U8), U8>(
            "setSpecularLightExponent",
            &LLMaterial::setSpecularLightExponent, U8(0), U8(99));
        ensure_setter_round_trip<void(LLMaterial::*)(U8), U8>(
            "setEnvironmentIntensity",
            &LLMaterial::setEnvironmentIntensity, U8(0), U8(123));
        ensure_setter_round_trip<void(LLMaterial::*)(U8), U8>(
            "setDiffuseAlphaMode",
            &LLMaterial::setDiffuseAlphaMode,
            (U8)LLMaterial::DIFFUSE_ALPHA_MODE_BLEND,
            (U8)LLMaterial::DIFFUSE_ALPHA_MODE_MASK);
        ensure_setter_round_trip<void(LLMaterial::*)(U8), U8>(
            "setAlphaMaskCutoff",
            &LLMaterial::setAlphaMaskCutoff, U8(0), U8(200));
    }

    // The paired-axis setters share the same underlying members as the X/Y
    // individual setters and must produce the identical hash for the same
    // logical state. This pins that the combined and split write paths agree.
    template<> template<>
    void llmaterial_object_t::test<3>()
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

        ensure_equals("paired vs. split writes agree",
            split.getHash(), paired.getHash());
    }

    // Hash distinguishes which slot a value lives in. If two materials carry
    // the same scalar but in different members, the hashes must differ —
    // otherwise the packing order or hashing scheme has aliased two fields.
    template<> template<>
    void llmaterial_object_t::test<4>()
    {
        // mNormalOffsetX vs mNormalOffsetY
        {
            LLMaterial x; x.setNormalOffsetX(1.0f);
            LLMaterial y; y.setNormalOffsetY(1.0f);
            ensure("Normal offset X vs Y distinguishes",
                x.getHash() != y.getHash());
        }
        // mNormalRepeatX vs mNormalRepeatY
        {
            LLMaterial x; x.setNormalRepeatX(2.0f);
            LLMaterial y; y.setNormalRepeatY(2.0f);
            ensure("Normal repeat X vs Y distinguishes",
                x.getHash() != y.getHash());
        }
        // mSpecularOffsetX vs mSpecularOffsetY
        {
            LLMaterial x; x.setSpecularOffsetX(3.0f);
            LLMaterial y; y.setSpecularOffsetY(3.0f);
            ensure("Specular offset X vs Y distinguishes",
                x.getHash() != y.getHash());
        }
        // mSpecularRepeatX vs mSpecularRepeatY
        {
            LLMaterial x; x.setSpecularRepeatX(4.0f);
            LLMaterial y; y.setSpecularRepeatY(4.0f);
            ensure("Specular repeat X vs Y distinguishes",
                x.getHash() != y.getHash());
        }
        // Same UUIDs swapped between mNormalID and mSpecularID.
        {
            LLMaterial ab; ab.setNormalID(kIdA); ab.setSpecularID(kIdB);
            LLMaterial ba; ba.setNormalID(kIdB); ba.setSpecularID(kIdA);
            ensure("Normal/Specular ID swap distinguishes",
                ab.getHash() != ba.getHash());
        }
        // Normal vs specular rotation
        {
            LLMaterial n; n.setNormalRotation(1.0f);
            LLMaterial s; s.setSpecularRotation(1.0f);
            ensure("Normal vs Specular rotation distinguishes",
                n.getHash() != s.getHash());
        }
        // Trailing U8 fields all share size/alignment — make sure the packer
        // doesn't accidentally read them as a single uniform word.
        {
            LLMaterial e; e.setSpecularLightExponent(7);
            LLMaterial i; i.setEnvironmentIntensity(7);
            LLMaterial d; d.setDiffuseAlphaMode((U8)LLMaterial::DIFFUSE_ALPHA_MODE_MASK);
            LLMaterial c; c.setAlphaMaskCutoff(7);
            ensure("exponent vs intensity distinguishes",
                e.getHash() != i.getHash());
            ensure("intensity vs alpha mode distinguishes",
                i.getHash() != d.getHash());
            ensure("alpha mode vs cutoff distinguishes",
                d.getHash() != c.getHash());
        }
    }

    // fromLLSD touches every member at once. Verify it invalidates the cache
    // and that hashes computed before vs. after differ.
    template<> template<>
    void llmaterial_object_t::test<5>()
    {
        LLMaterial m;
        const LLUUID before = m.getHash();

        const LLSD sd = m.asLLSD();
        // Roundtripping the same LLSD should restore the same hash.
        LLMaterial back;
        back.setAlphaMaskCutoff(99); // make cache dirty + state altered
        back.fromLLSD(sd);
        ensure_equals("fromLLSD round-trip yields baseline hash",
            back.getHash(), before);

        // Now perturb a field through LLSD and check the hash moves.
        LLMaterial perturbed;
        perturbed.fromLLSD(sd);
        ensure_equals("post-fromLLSD matches baseline",
            perturbed.getHash(), before);
        perturbed.setEnvironmentIntensity(42);
        ensure("post-fromLLSD setter still invalidates",
            perturbed.getHash() != before);
    }

    // Copy semantics: a copy carries the cached state, but mutations on the
    // copy invalidate only the copy's cache.
    template<> template<>
    void llmaterial_object_t::test<6>()
    {
        LLMaterial src;
        src.setAlphaMaskCutoff(42);
        const LLUUID src_hash = src.getHash();

        // Copy after the source's cache has been populated.
        LLMaterial copy_warm(src);
        ensure_equals("warm copy preserves hash",
            copy_warm.getHash(), src_hash);

        // Copy from a source whose cache is dirty (never read).
        LLMaterial dirty_src;
        dirty_src.setEnvironmentIntensity(7);  // marks dirty without reading
        LLMaterial copy_cold(dirty_src);
        ensure_equals("cold copy still produces the right hash",
            copy_cold.getHash(), dirty_src.getHash());

        // Mutating the copy must not bleed back into the source.
        copy_warm.setEnvironmentIntensity(99);
        ensure("mutating copy invalidates copy's hash",
            copy_warm.getHash() != src_hash);
        ensure_equals("source hash unchanged",
            src.getHash(), src_hash);
    }

    // Redundantly writing the same value through a setter must still result
    // in the same hash on the next read (the cache may be marked dirty and
    // recomputed, but the recomputed value matches because state didn't move).
    template<> template<>
    void llmaterial_object_t::test<7>()
    {
        LLMaterial m;
        m.setAlphaMaskCutoff(50);
        const LLUUID h1 = m.getHash();

        m.setAlphaMaskCutoff(50);    // redundant
        m.setEnvironmentIntensity(0); // already the default
        m.setNormalRotation(0.0f);    // already the default
        const LLUUID h2 = m.getHash();
        ensure_equals("redundant setter writes preserve hash", h1, h2);
    }

    // operator== iff hashes match for materials of equal state.
    template<> template<>
    void llmaterial_object_t::test<8>()
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

        ensure("operator== for equal state", a == b);
        ensure("hash equality follows operator==", a.getHash() == b.getHash());

        // One-bit perturbation must break both equalities together.
        b.setEnvironmentIntensity(51);
        ensure("operator!= after single-field change", a != b);
        ensure("hash inequality after single-field change",
            a.getHash() != b.getHash());
    }
}
