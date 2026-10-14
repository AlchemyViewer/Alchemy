/**
 * @file selectionContrastF.glsl
 * @brief The selection outline's colours against what lies behind them (ALSelectionOutline): linked into the edge pass
 *        and the wireframe, which keep a line's lightness apart from the backdrop's, its hue as it is.
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

// The window as it stood before the wireframe and the outline were drawn over it, at half its resolution
// (ALSelectionOutline::BACKDROP_SCALE), bilinearly filtered: display colour.
uniform sampler2D selectionBackdrop;

// 1 where lines are kept apart from the backdrop's lightness (AlchemySelectionOutlineContrast), 0 where they are drawn
// as the palette has them.
uniform int selection_contrast;

// How far apart in OKLab lightness a line and the backdrop around it are kept (ALSelectionOutline::MIN_CONTRAST).
const float MIN_CONTRAST = 0.3;

// srgbF.glsl
vec3 srgb_to_linear(vec3 cs);
vec3 linear_to_srgb(vec3 cl);

// OKLab (Ottosson), from and to linear sRGB.
vec3 linearToOklab(vec3 c)
{
    vec3 lms = mat3(0.4122214708, 0.2119034982, 0.0883024619,
                    0.5363325363, 0.6806995451, 0.2817188376,
                    0.0514459929, 0.1073969566, 0.6299787005) * c;
    lms = sign(lms) * pow(abs(lms), vec3(1.0 / 3.0));
    return mat3(0.2104542553, 1.9779984951, 0.0259040371,
                0.7936177850, -2.4285922050, 0.7827717662,
                -0.0040720468, 0.4505937099, -0.8086757660) * lms;
}

vec3 oklabToLinear(vec3 lab)
{
    vec3 lms = mat3(1.0, 1.0, 1.0,
                    0.3963377774, -0.1055613458, -0.0894841775,
                    0.2158037573, -0.0638541728, -1.2914855480) * lab;
    lms = lms * lms * lms;
    return mat3(4.0767416621, -1.2684380046, -0.0041960863,
                -3.3077115913, 2.6097574011, -0.7034186147,
                0.2309699292, -0.3413193965, 1.7076147010) * lms;
}

// The backdrop's OKLab lightness about the centre `pixel` of a pixel of a window `size` pixels across: of the mean
// colour of the 8 x 8 pixels around it, four bilinear taps of the half-resolution copy, so a textured surface gives a
// steady lightness rather than each of its texels'. -1 without selection_contrast.
float backdropLightness(vec2 pixel, vec2 size)
{
    if (selection_contrast == 0)
    {
        return -1.0;
    }
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; ++i)
    {
        vec2 offset = vec2(((i & 1) == 0) ? -2.0 : 2.0, ((i & 2) == 0) ? -2.0 : 2.0);
        sum += srgb_to_linear(texture(selectionBackdrop, (pixel + offset) / size).rgb);
    }
    return linearToOklab(sum * 0.25).x;
}

// The lightness a line of lightness `line` takes over a backdrop of lightness `back` (ALSelectionOutline::
// contrastLightness): its own where the two are MIN_CONTRAST apart; else MIN_CONTRAST lighter than the backdrop below
// the turn and MIN_CONTRAST darker above it. The turn is the line's own lightness, held to where both sides have room,
// so a light line stays light over all but light backdrops and a dark one dark over all but dark ones.
float contrastLightness(float line, float back)
{
    float turn = clamp(line, MIN_CONTRAST, 1.0 - MIN_CONTRAST);
    return (back < turn) ? max(line, back + MIN_CONTRAST) : min(line, back - MIN_CONTRAST);
}

// `colour`, display-encoded, at OKLab lightness `lightness`: its hue kept, and its chroma as near its own as the display
// can show.
vec3 withLightness(vec3 colour, float lightness)
{
    vec2 ab = linearToOklab(srgb_to_linear(colour)).yz;
    float kept = 1.0;
    vec3 rgb = oklabToLinear(vec3(lightness, ab));
    if (any(lessThan(rgb, vec3(0.0))) || any(greaterThan(rgb, vec3(1.0))))
    {
        float lo = 0.0;
        float hi = 1.0;
        for (int i = 0; i < 10; ++i)
        {
            float k = 0.5 * (lo + hi);
            rgb = oklabToLinear(vec3(lightness, ab * k));
            if (any(lessThan(rgb, vec3(0.0))) || any(greaterThan(rgb, vec3(1.0))))
            {
                hi = k;
            }
            else
            {
                lo = k;
            }
        }
        kept = lo;
    }
    return linear_to_srgb(clamp(oklabToLinear(vec3(lightness, ab * kept)), vec3(0.0), vec3(1.0)));
}

// `colour`, display-encoded, as a line shows it over a backdrop of lightness `back` (backdropLightness): unchanged
// where `back` is negative or the two are far enough apart, else at contrastLightness.
vec3 contrastColour(vec3 colour, float back)
{
    if (back < 0.0)
    {
        return colour;
    }
    float line = linearToOklab(srgb_to_linear(colour)).x;
    float lightness = contrastLightness(line, back);
    return (lightness == line) ? colour : withLightness(colour, lightness);
}
