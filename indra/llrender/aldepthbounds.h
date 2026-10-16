/**
 * @file aldepthbounds.h
 * @brief The depth bounds test, and the full-screen passes held to what the world drew.
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

#pragma once

#include "llgl.h"
#include "llmatrix4a.h"

#include <optional>

/// GL_EXT_depth_bounds_test: a fragment passes only where the depth already stored under it lies in [zmin, zmax],
/// whatever depth the fragment has itself.
///
/// A far-plane triangle held off the clear's depth by it shades only what the world drew with the depth test off.
/// The depth test can do the same only by passing the triangle where it lies behind the stored depth, which turns
/// the test against the direction the scene was drawn in, and NVIDIA's hierarchical depth stops culling until the
/// depth is next cleared once it has been turned. A light held to the depths its sphere spans also skips the pixels
/// its volume's depth test passes in front of and behind it.
class ALDepthBounds
{
public:
    /// Whether this context has the test and the session uses it. Without it callers keep to the depth test.
    static bool available() { return sEnabled && gGLManager.mHasDepthBoundsTest; }

    /// Whether the session uses the test where the context has it. Newview's to set, from AlchemyRenderDepthBoundsTest,
    /// as llrender reads no settings.
    static bool sEnabled;

    /// Holds the test to [zmin, zmax], in depth as stored, until destroyed, when it goes off. Scopes do not nest.
    ALDepthBounds(F64 zmin, F64 zmax);
    ~ALDepthBounds();

    ALDepthBounds(const ALDepthBounds&) = delete;
    ALDepthBounds& operator=(const ALDepthBounds&) = delete;

    /// Moves the range while the scope stands.
    void set(F64 zmin, F64 zmax);

    /// Every stored depth but the clear's, under the current convention: what the world drew, and not the sky.
    static void drawn(F64& zmin, F64& zmax);

    /// The depths `proj` stores for drawn points whose view-space z (negative ahead) lies between `z_a` and `z_b`.
    /// The part of the span at or behind the eye is the near plane's depth. False, with nothing set, when the
    /// projection's depth depends on more than view z, as an oblique near plane's does.
    static bool viewSpan(const LLMatrix4a& proj, F32 z_a, F32 z_b, F64& zmin, F64& zmax);

private:
    F64 mMin;
    F64 mMax;
    static bool sActive;
};

/// A far-plane triangle that shades only what the world drew (sunLightV.glsl, softenLightV.glsl, hazeV.glsl and the
/// rest): the depth bounds test holding it off the clear's depth with the depth test off, or without the test the
/// depth test passing it where the stored depth is nearer than it.
class ALDrawnOnly
{
public:
    ALDrawnOnly();

    ALDrawnOnly(const ALDrawnOnly&) = delete;
    ALDrawnOnly& operator=(const ALDrawnOnly&) = delete;

    /// The depth bounds the scope holds, for a draw that narrows them, or null where it keeps to the depth test.
    ALDepthBounds* bounds() { return mBounds ? &*mBounds : nullptr; }

private:
    LLGLDepthTest mDepthTest;
    std::optional<ALDepthBounds> mBounds;
};
