/**
 * @file alcutoutimage.h
 * @brief A snapshot cutout's read-back, resolved to the image that is saved
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#ifndef AL_CUTOUTIMAGE_H
#define AL_CUTOUTIMAGE_H

#include "stdtypes.h"
#include "v4color.h"

class LLImageRaw;

/// The arithmetic of a cutout's image, apart from anything that renders it,
/// so it can be tested on its own.
///
/// A cutout frame reads back RGBA whose colour is premultiplied by its alpha,
/// the subject's coverage: everything blended was composited over black. That
/// is the form in which averaging is right -- an edge texel half covered by a
/// red surface is (0.5, 0, 0, 0.5), and averaging it with an empty neighbour
/// gives a quarter-covered red, where straight colour would give a darker red
/// -- so the image is averaged down in it, and only then composited.
///
/// The two steps are apart because only the first depends on the render: a
/// cutout laid over another fill, or kept as another format, is the same
/// resolved image laid down again.
namespace ALCutoutImage
{
    /// Resolves @a raw -- premultiplied RGBA at any size -- in place, to
    /// premultiplied RGBA at @a width by @a height:
    ///
    /// - colour above alpha is clipped to it first. A premultiplied texel
    ///   cannot be brighter than it is opaque; what was (dither over an empty
    ///   texel, light added to one) has nothing under it to belong to;
    /// - it is averaged down, premultiplied.
    ///
    /// With @a drawn, it is told whether any of @a raw is covered at all: a
    /// read-back of nothing is the fill alone, the subject nowhere in view.
    ///
    /// False where @a raw is not four components or cannot be resized.
    bool resolve(LLImageRaw& raw, S32 width, S32 height, bool* drawn = nullptr);

    /// Lays @a raw -- premultiplied RGBA, as resolve() leaves it -- over
    /// @a fill in place: straight colour, and an alpha that says how much of
    /// the fill there is -- 0 leaves the background empty, 1 fills it, and
    /// between is a see-through tint.
    ///
    /// With @a keep_alpha it is left as straight RGBA, the colour divided back
    /// out, which is what every image format that stores alpha means by it.
    /// Without, it is RGB over the fill taken as opaque, for formats that have
    /// nowhere to put alpha.
    ///
    /// False where @a raw is not four components or cannot be reallocated.
    bool layOverFill(LLImageRaw& raw, const LLColor4& fill, bool keep_alpha);

    /// resolve() and then layOverFill().
    bool resolveOverFill(LLImageRaw& raw, S32 width, S32 height, const LLColor4& fill, bool keep_alpha, bool* drawn = nullptr);

    /// Scales @a raw -- straight RGBA, as a resolved cutout is -- to @a width
    /// by @a height without darkening its edges: premultiplied, averaged, and
    /// divided back out, which is resolveOverFill() over a fill of nothing.
    /// Anything but four components is scaled as it is.
    bool scaleStraightAlpha(LLImageRaw& raw, S32 width, S32 height);
}

#endif // AL_CUTOUTIMAGE_H
