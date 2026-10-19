/**
 * @file blurLightC.glsl
 * @brief The light map's ambient occlusion blur, both axes in one dispatch: a work group blurs its tile's rows across,
 *        and the rows above and below that its taps down can reach, into shared memory, then down from there.
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

// The tile a work group blurs, and the most rows above and below it that its taps down can reach
// (ALLightMapBlur::TILE_WIDTH, TILE_HEIGHT and MAX_APRON). Tall, since every row of the apron is blurred across as a
// row of the tile is; a work group's threads take every THREADS_DOWN-th row.
#define TILE_WIDTH 32
#define TILE_HEIGHT 128
#define MAX_APRON 32
#define THREADS_DOWN 16

layout(local_size_x = TILE_WIDTH, local_size_y = THREADS_DOWN) in;

// The sun pass's light map: sun shadow (r), ambient occlusion (g), the spot lights' shadows (b, a).
uniform sampler2D lightMap;
// The rows above and below the tile its taps down reach, at most MAX_APRON.
uniform int blur_light_apron;
// The sun's shadow and the blurred occlusion, which the soften pass reads where it would read the light map's.
layout(rg8) writeonly uniform image2D blurLightTarget;

// Each row of the tile, and the apron's above and below it, blurred across: what the fragment passes keep in their
// scratch between the two. Row 0 is the tile's first less the apron.
shared float across[TILE_HEIGHT + 2 * MAX_APRON][TILE_WIDTH];

float blurLightTaps(ivec2 px, vec2 delta, out ivec2 tap[12], out float weight[12]);
bool blurLightDrawn(ivec2 px);

void main()
{
    ivec2 size = textureSize(lightMap, 0);
    ivec2 tile = ivec2(gl_WorkGroupID.xy) * ivec2(TILE_WIDTH, TILE_HEIGHT);
    int x = tile.x + int(gl_LocalInvocationID.x);
    int base = tile.y - blur_light_apron;
    int rows = TILE_HEIGHT + 2 * blur_light_apron;

    // Across, over every row a tap down can read: the tile's and its apron's, within the light map, which holds the
    // rows a mirrored tap reads past its edges. The sky keeps the clear the fragment passes' scratch holds.
    int first = max(base, 0);
    int last = min(base + rows, size.y);
    for (int y = first + int(gl_LocalInvocationID.y); y < last; y += THREADS_DOWN)
    {
        ivec2 px = ivec2(x, y);
        float ao = 1.0;
        if (x < size.x && blurLightDrawn(px))
        {
            ivec2 tap[12];
            float weight[12];
            float defined_weight = blurLightTaps(px, vec2(1.0, 0.0), tap, weight);
            ao = defined_weight * texelFetch(lightMap, px, 0).g;
            for (int i = 0; i < 12; ++i)
            {
                if (weight[i] > 0.0)
                {
                    ao += texelFetch(lightMap, tap[i], 0).g * weight[i];
                    defined_weight += weight[i];
                }
            }
            ao = max(ao / defined_weight, 0.0);
        }
        across[y - base][gl_LocalInvocationID.x] = ao;
    }

    memoryBarrierShared();
    barrier();

    if (x >= size.x)
    {
        return;
    }

    // Down, from the rows blurred across. The sky is the sun pass's clear, as the fragment passes leave it.
    int end = min(tile.y + TILE_HEIGHT, size.y);
    for (int y = tile.y + int(gl_LocalInvocationID.y); y < end; y += THREADS_DOWN)
    {
        ivec2 px = ivec2(x, y);
        vec4 ccol = texelFetch(lightMap, px, 0);
        vec2 blurred = ccol.rg;
        if (blurLightDrawn(px))
        {
            ivec2 tap[12];
            float weight[12];
            float defined_weight = blurLightTaps(px, vec2(0.0, 1.0), tap, weight);
            float ao = defined_weight * across[y - base][gl_LocalInvocationID.x];
            for (int i = 0; i < 12; ++i)
            {
                if (weight[i] > 0.0)
                {
                    // A tap down keeps the pixel's column; held to the rows blurred, whatever its offset.
                    ao += across[clamp(tap[i].y - base, 0, rows - 1)][gl_LocalInvocationID.x] * weight[i];
                    defined_weight += weight[i];
                }
            }
            blurred = vec2(ccol.r, max(ao / defined_weight, 0.0));
        }
        imageStore(blurLightTarget, px, vec4(blurred, 0.0, 0.0));
    }
}
