/**
 * @file alrendertargetdesc.h
 * @brief What a render target is: its size, its attachments' formats and its mips, as a value.
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

#include "stdtypes.h"

#include <array>
#include <cstddef>

/// What a render target is: two targets with equal descriptions have storage of the same size and formats, and a
/// target that already matches a description needs nothing done to it.
///
/// Built with designated initializers, naming only what differs from the defaults:
///
///     const ALRenderTargetDesc gbuffer{ .width = w, .height = h,
///                                       .color = { GL_SRGB8_ALPHA8, GL_RGBA8, GL_RGBA16 },
///                                       .depth = ALRenderTargetDesc::Depth::D32F, .stencil = true };
///
/// The fields run in descending alignment, so nothing pads between them.
struct ALRenderTargetDesc
{
    /// The depth attachment's format. Stencil, where asked for, goes in the same texture: D24 becomes
    /// GL_DEPTH24_STENCIL8 and D32F GL_DEPTH32F_STENCIL8.
    enum class Depth : U8
    {
        None, ///< No depth attachment of the target's own.
        D24,  ///< GL_DEPTH_COMPONENT24.
        D32F, ///< GL_DEPTH_COMPONENT32F.
    };

    /// Whether the colour attachments carry a mip chain, and who fills it.
    enum class Mips : U8
    {
        None,   ///< One level.
        Auto,   ///< Every level, regenerated from level 0 when the target is flushed.
        Manual, ///< Every level; the owner fills them when it chooses.
    };

    static constexpr U32 MAX_COLOR = 4;

    U32 width  = 0;
    U32 height = 0;
    /// GL sized internal formats, one an attachment, in attachment order; the first 0 ends the list.
    std::array<U32, MAX_COLOR> color = {};
    Depth depth  = Depth::None;
    bool stencil = false;
    Mips mips    = Mips::None;

    /// The number of colour attachments: the formats before the first 0.
    [[nodiscard]] constexpr U32 colorCount() const
    {
        U32 count = 0;
        while (count < MAX_COLOR && color[count] != 0)
        {
            ++count;
        }
        return count;
    }

    /// Every field folded in, for a cache keyed on descriptions.
    [[nodiscard]] constexpr std::size_t hash() const
    {
        std::size_t h = mix(0, width);
        h = mix(h, height);
        for (U32 format : color)
        {
            h = mix(h, format);
        }
        h = mix(h, static_cast<U32>(depth));
        h = mix(h, static_cast<U32>(stencil));
        return mix(h, static_cast<U32>(mips));
    }

    /// Member-wise, so it cannot fall out of step with the field list.
    friend bool operator==(const ALRenderTargetDesc&, const ALRenderTargetDesc&) = default;

    /// What boost::hash, and so boost::unordered's containers, key a description on.
    friend std::size_t hash_value(const ALRenderTargetDesc& desc) { return desc.hash(); }

private:
    // splitmix64's finalizer per field, the golden-ratio increment folding the value in: descriptions differ in one
    // format or one flag, and a one-bit change here reaches every bit of the result.
    static constexpr std::size_t mix(std::size_t h, U32 value)
    {
        std::size_t x = h + 0x9E3779B97F4A7C15ull + value;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }
};

namespace al_render_target_desc
{
    // Converts to the type of whichever field it initializes, so that how many of it an aggregate's braces take is how
    // many fields the aggregate has. Only ever named in an unevaluated operand.
    struct AnyField
    {
        template <typename T> constexpr operator T() const noexcept;
    };

    template <typename T, typename... Fields> constexpr std::size_t field_count()
    {
        if constexpr (requires { T{ Fields{}..., AnyField{} }; })
        {
            return field_count<T, Fields..., AnyField>();
        }
        else
        {
            return sizeof...(Fields);
        }
    }
}

static_assert(sizeof(std::size_t) == 8, "ALRenderTargetDesc::mix takes a 64-bit std::size_t");
// The fields are counted rather than sized: the 27 bytes they take pad to 28, so a field of a byte would fit in the
// padding unseen.
static_assert(al_render_target_desc::field_count<ALRenderTargetDesc>() == 6 && sizeof(ALRenderTargetDesc) == 28,
              "ALRenderTargetDesc gained, lost or resized a field: fold it into hash(), and keep the fields in "
              "descending alignment so no padding opens between them");
