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
// (ALSelectionOutline::BACKDROP_SCALE), read a texel at a time: display colour, and in .a 1 where none of the texel's
// pixels shows a selected surface (selectionBackdropMaskF.glsl).
uniform sampler2D selectionBackdrop;

// How the lines are kept apart from what lies behind them (ALSelectionOutline::EContrast): 0 drawn as the palette has
// them, 1 shifted, 2 cased by a rim.
uniform int selection_contrast;

#define CONTRAST_SHIFT 1

// The window pixels to a texel of the backdrop along each axis (ALSelectionOutline::BACKDROP_SCALE).
#define BACKDROP_SCALE 2

// How far apart in OKLab a shifted line and the backdrop around it are kept (ALSelectionOutline::MIN_CONTRAST).
const float MIN_CONTRAST = 0.3;

// The relative luminance over which a line takes a dark rim, and under which a light one: about OKLab's middle.
const float RIM_TURN = 0.18;

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

// The OKLab colour of what lies behind a line at the centre `pixel` of a window pixel, under CONTRAST_SHIFT: the mean of
// the backdrop's texels over the 8 x 8 pixels about it, so a textured surface gives a steady colour. With `masked`,
// the contour's, only of the texels that show no selected surface, as an object is no backdrop of its own outline;
// without, the wireframe's, drawn on its object, of all of them. .w is 0 where there is none to read, or the lines are
// not shifted, and 1 where there is.
vec4 backdropColour(vec2 pixel, bool masked)
{
    if (selection_contrast != CONTRAST_SHIFT)
    {
        return vec4(0.0);
    }
    ivec2 last = textureSize(selectionBackdrop, 0) - ivec2(1);
    ivec2 centre = ivec2(floor(pixel / float(BACKDROP_SCALE)));
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (int dy = -2; dy < 2; ++dy)
    {
        for (int dx = -2; dx < 2; ++dx)
        {
            vec4 texel = texelFetch(selectionBackdrop, clamp(centre + ivec2(dx, dy), ivec2(0), last), 0);
            float w = masked ? texel.a : 1.0;
            sum += srgb_to_linear(texel.rgb) * w;
            weight += w;
        }
    }
    return (weight > 0.0) ? vec4(linearToOklab(sum / weight), 1.0) : vec4(0.0);
}

// The lightness a line of lightness `line` takes over a backdrop of lightness `back` where their colours are less
// than MIN_CONTRAST apart (ALSelectionOutline::contrastLightness): its own where the lightnesses are MIN_CONTRAST
// apart already; else, below the turn, MIN_CONTRAST lighter than the backdrop; above it, MIN_CONTRAST darker and darker
// again by as much as the line is darker than white, so lines of different lightness keep their order and distance.
// The turn is the line's own lightness held to where both sides have room.
float contrastLightness(float line, float back)
{
    float turn = clamp(line, MIN_CONTRAST, 1.0 - MIN_CONTRAST);
    if (back < turn)
    {
        return max(line, back + MIN_CONTRAST);
    }
    return (line <= back - MIN_CONTRAST) ? line : max(back - MIN_CONTRAST - (1.0 - line), 0.0);
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

// `colour`, display-encoded, as a line shows it over `back`, what backdropColour read behind it: as it is where there
// was nothing to read, or the two colours, hue and all, are MIN_CONTRAST apart; else at contrastLightness.
vec3 contrastColour(vec3 colour, vec4 back)
{
    if (back.w <= 0.0)
    {
        return colour;
    }
    vec3 lab = linearToOklab(srgb_to_linear(colour));
    if (distance(lab, back.xyz) >= MIN_CONTRAST)
    {
        return colour;
    }
    float lightness = contrastLightness(lab.x, back.x);
    return (lightness == lab.x) ? colour : withLightness(colour, lightness);
}

// The rim cased around a contour of `line`, display-encoded, under CONTRAST_RIM: dark around a light line and light
// around a dark one, so that over any backdrop the line or its rim stands out.
vec3 rimColour(vec3 line)
{
    float luminance = dot(srgb_to_linear(line), vec3(0.2126, 0.7152, 0.0722));
    return (luminance > RIM_TURN) ? vec3(0.0) : vec3(1.0);
}
