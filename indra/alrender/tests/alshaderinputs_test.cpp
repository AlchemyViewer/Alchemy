/**
 * @file alshaderinputs_test.cpp
 * @brief Which settings change which of the programs' inputs, and whether every program or only some is built again.
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

#include "linden_common.h"

#include "../alshaderinputs.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>

namespace
{
    using Settings = ALShaderSettings;
    using Inputs   = ALShaderInputs;

    // What a change of settings costs: nothing, the programs reading a local input, or every program.
    enum class Cost
    {
        None,
        Local,
        Global,
        Both,
    };

    std::string name(Cost cost)
    {
        switch (cost)
        {
            case Cost::None:   return "none";
            case Cost::Local:  return "local";
            case Cost::Global: return "global";
            case Cost::Both:   return "global and local";
        }
        return "?";
    }

    // The reference: a context with 16 texture units, clip control and compute, and the settings' defaults.
    Settings defaults() { return Settings{}; }

    // The cost of moving from @a before to the settings @a change makes of them.
    Cost cost(const Settings& before, const std::function<void(Settings&)>& change)
    {
        Settings after = before;
        change(after);
        const Inputs a      = Inputs::make(before);
        const Inputs b      = Inputs::make(after);
        const bool   global = a.mGlobal != b.mGlobal;
        const bool   local  = a.mLocal != b.mLocal;
        return global ? (local ? Cost::Both : Cost::Global) : (local ? Cost::Local : Cost::None);
    }

    void check(const char* what, const Settings& before, const std::function<void(Settings&)>& change, Cost expected)
    {
        INFO(what);
        const Cost found = cost(before, change);
        INFO("costs " << name(found) << ", expected " << name(expected));
        CHECK(found == expected);
    }

    TEST_CASE("alshaderinputs: equal settings make equal inputs", "[alshaderinputs]")
    {
        CHECK(Inputs::make(defaults()) == Inputs::make(defaults()));
    }

    TEST_CASE("alshaderinputs: the settings every program is built from are global", "[alshaderinputs]")
    {
        const Settings s = defaults();
        check("reverse-Z", s, [](Settings& x) { x.mReverseZ = !x.mReverseZ; }, Cost::Global);
        check("emissive", s, [](Settings& x) { x.mEmissive = !x.mEmissive; }, Cost::Global);
        check("lighting class", s, [](Settings& x) { x.mLightingMaxLevel = 2; }, Cost::Global);
        check("reserved texture units", s, [](Settings& x) { x.mReservedTextureIndices = 10; }, Cost::Global);
        check("SSR", s, [](Settings& x) { x.mSSR = !x.mSSR; }, Cost::Global);
        check("mirrors", s, [](Settings& x) { x.mMirrors = !x.mMirrors; }, Cost::Global);
        check("reflection probes", s, [](Settings& x) { x.mProbes = !x.mProbes; }, Cost::Global);
        check("probe level", s, [](Settings& x) { x.mProbeLevel = 1; }, Cost::Global);
        check("shadow detail", s, [](Settings& x) { x.mShadowDetail = 1; }, Cost::Global);
        check("shadow filter quality", s, [](Settings& x) { x.mShadowFilterQuality = 2; }, Cost::Global);
        check("terrain mapping", s, [](Settings& x) { x.mTerrainPlanarSampleCount = 3; }, Cost::Global);
        check("terrain detail", s, [](Settings& x) { x.mTerrainPBRDetail = -3; }, Cost::Global);
        check("terrain hex tiling", s, [](Settings& x) { x.mTerrainHexTiling = !x.mTerrainHexTiling; }, Cost::Global);
        check("water hextile", s, [](Settings& x) { x.mWaterHextile = 2; }, Cost::Global);
    }

    TEST_CASE("alshaderinputs: HDR changes the normal layout every program reads, and the post chain's permutations",
              "[alshaderinputs]")
    {
        check("HDR", defaults(), [](Settings& x) { x.mHDR = !x.mHDR; }, Cost::Both);
    }

    TEST_CASE("alshaderinputs: the settings only some programs read are local", "[alshaderinputs]")
    {
        const Settings s = defaults();
        check("transparent water", s, [](Settings& x) { x.mTransparentWater = !x.mTransparentWater; }, Cost::Local);
        check("SSAO", s, [](Settings& x) { x.mSSAO = !x.mSSAO; }, Cost::Local);
        check("avatar cloth", s, [](Settings& x) { x.mAvatarCloth = !x.mAvatarCloth; }, Cost::Local);
        check("glow noise", s, [](Settings& x) { x.mGlowNoise = !x.mGlowNoise; }, Cost::Local);
        check("bloom halation", s, [](Settings& x) { x.mBloomHalation = !x.mBloomHalation; }, Cost::Local);
        check("SMAA predication", s, [](Settings& x) { x.mSMAAPredication = !x.mSMAAPredication; }, Cost::Local);
        check("terrain normals", s, [](Settings& x) { x.mTerrainPBRNormals = !x.mTerrainPBRNormals; }, Cost::Local);
        check("dynamic LOD", s, [](Settings& x) { x.mDynamicLOD = !x.mDynamicLOD; }, Cost::Local);
        check("water waves", s, [](Settings& x) { x.mWaterWaves = !x.mWaterWaves; }, Cost::Local);
        check("compute switched off", s, [](Settings& x) { x.mDisableCompute = true; }, Cost::Local);
        check("local terrain paint", s, [](Settings& x) { x.mLocalTerrainPaint = true; }, Cost::Local);
        check("GLOn12", s, [](Settings& x) { x.mIsD3D12 = true; }, Cost::Local);

        Settings paint = s;
        paint.mLocalTerrainPaint = true;
        check("terrain paint bit depth, with paint", paint, [](Settings& x) { x.mTerrainPaintBitDepth = 4; }, Cost::Local);
    }

    TEST_CASE("alshaderinputs: a setting no program reads under the others changes nothing", "[alshaderinputs]")
    {
        const Settings s = defaults();

        // Reverse-Z needs clip control.
        Settings no_clip = s;
        no_clip.mHasClipControl = false;
        check("reverse-Z without clip control", no_clip, [](Settings& x) { x.mReverseZ = !x.mReverseZ; }, Cost::None);

        // The level only with probes, the filter only with shadows.
        Settings no_probes = s;
        no_probes.mProbes = false;
        check("probe level without probes", no_probes, [](Settings& x) { x.mProbeLevel = 1; }, Cost::None);
        Settings no_shadows = s;
        no_shadows.mShadowDetail = 0;
        check("filter quality without shadows", no_shadows, [](Settings& x) { x.mShadowFilterQuality = 3; }, Cost::None);

        // The paint's precision only with local paint, and up to the paint map's 8 bits.
        check("terrain paint bit depth without paint", s, [](Settings& x) { x.mTerrainPaintBitDepth = 4; }, Cost::None);
        Settings paint = s;
        paint.mLocalTerrainPaint   = true;
        paint.mTerrainPaintBitDepth = 9;
        check("terrain paint bit depth past 8", paint, [](Settings& x) { x.mTerrainPaintBitDepth = 12; }, Cost::None);

        // Compute where the context has none.
        Settings no_compute = s;
        no_compute.mHasCompute = false;
        check("compute switch without compute", no_compute, [](Settings& x) { x.mDisableCompute = true; }, Cost::None);

        // Clamped settings within what their clamps fold together.
        check("shadow detail past 2", s, [](Settings& x) { x.mShadowDetail = 3; }, Cost::None);
        check("terrain mapping 2, which is flat", s, [](Settings& x) { x.mTerrainPlanarSampleCount = 2; }, Cost::None);
        Settings hextile = s;
        hextile.mWaterHextile = 2;
        check("water hextile 2 to 5", hextile, [](Settings& x) { x.mWaterHextile = 5; }, Cost::None);
        check("lighting class past 3", s, [](Settings& x) { x.mLightingMaxLevel = 5; }, Cost::None);
        Settings few_units = s;
        few_units.mReservedTextureIndices = 14;
        check("reserved units below four channels", few_units, [](Settings& x) { x.mReservedTextureIndices = 15; },
              Cost::None);
        // A context of 16 units samples detail -1 at most, so 0 is -1.
        check("terrain detail 0 to -1 on 16 units", s, [](Settings& x) { x.mTerrainPBRDetail = -1; }, Cost::None);
    }

    TEST_CASE("alshaderinputs: the shared objects' digest follows the global inputs alone", "[alshaderinputs]")
    {
        const Settings s = defaults();
        const LLUUID   base = Inputs::make(s).mGlobal.digest();
        CHECK(Inputs::make(s).mGlobal.digest() == base);

        const auto digest_after = [&s](const std::function<void(Settings&)>& change)
        {
            Settings after = s;
            change(after);
            return Inputs::make(after).mGlobal.digest();
        };
        CHECK(digest_after([](Settings& x) { x.mLightingMaxLevel = 2; }) != base);
        CHECK(digest_after([](Settings& x) { x.mReservedTextureIndices = 10; }) != base);
        CHECK(digest_after([](Settings& x) { x.mWaterHextile = 2; }) != base);
        CHECK(digest_after([](Settings& x) { x.mSSR = !x.mSSR; }) != base);
        CHECK(digest_after([](Settings& x) { x.mAvatarCloth = !x.mAvatarCloth; }) == base);
        CHECK(digest_after([](Settings& x) { x.mWaterWaves = !x.mWaterWaves; }) == base);
    }

    TEST_CASE("alshaderinputs: the terrain's detail is what the context's texture units sample", "[alshaderinputs]")
    {
        // pbrterrainF.glsl, flat: 18 samplers at detail 0, 14 at -1 and -2, 10 at -3, 6 at -4.
        CHECK(Inputs::terrainDetail(0, 1, 32) == 0);
        CHECK(Inputs::terrainDetail(0, 1, 18) == 0);
        CHECK(Inputs::terrainDetail(0, 1, 17) == -1);
        CHECK(Inputs::terrainDetail(0, 1, 16) == -1);
        CHECK(Inputs::terrainDetail(0, 1, 13) == -3);
        CHECK(Inputs::terrainDetail(0, 1, 9) == -4);
        CHECK(Inputs::terrainDetail(0, 1, 4) == -4);
        CHECK(Inputs::terrainDetail(-2, 1, 32) == -2);
        CHECK(Inputs::terrainDetail(5, 1, 32) == 0);
        CHECK(Inputs::terrainDetail(-9, 1, 32) == -4);
        // Triplanar reads the height map too: 19 at detail 0, 15 at -1 and -2, 11 at -3.
        CHECK(Inputs::terrainDetail(0, 3, 19) == 0);
        CHECK(Inputs::terrainDetail(0, 3, 18) == -1);
        CHECK(Inputs::terrainDetail(0, 3, 14) == -3);
        CHECK(Inputs::terrainDetail(0, 3, 10) == -4);
    }

    TEST_CASE("alshaderinputs: the terrain's mapping is flat or triplanar", "[alshaderinputs]")
    {
        CHECK(Inputs::terrainMapping(0) == 1);
        CHECK(Inputs::terrainMapping(1) == 1);
        CHECK(Inputs::terrainMapping(2) == 1);
        CHECK(Inputs::terrainMapping(3) == 3);
        CHECK(Inputs::terrainMapping(9) == 3);
    }

    TEST_CASE("alshaderinputs: what the context gives", "[alshaderinputs]")
    {
        Settings s = defaults();
        s.mTextureImageUnits = 32;
        Inputs in = Inputs::make(s);
        CHECK(in.mGlobal.mIndexedTextureChannels == 20);
        CHECK(in.mLocal.mIndexedGLTFChannels == 8);

        s.mTextureImageUnits = 16;
        in = Inputs::make(s);
        CHECK(in.mGlobal.mIndexedTextureChannels == 4);
        CHECK(in.mLocal.mIndexedGLTFChannels == 4);

        s.mIsD3D12 = true;
        CHECK(Inputs::make(s).mLocal.mIndexedGLTFChannels == 0);

        s.mHasClipControl = false;
        CHECK_FALSE(Inputs::make(s).mGlobal.mReverseZ);
        s.mHasCompute = false;
        CHECK_FALSE(Inputs::make(s).mLocal.mCompute);
    }
}
