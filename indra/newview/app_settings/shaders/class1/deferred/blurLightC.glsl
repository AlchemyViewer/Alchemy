/**
 * @file blurLightC.glsl
 * @brief The light map's ambient occlusion blur, both axes in one dispatch: a work group walks its tile down a step of
 *        rows at a time, blurring each step across from the view depth and occlusion it fetched once into shared
 *        memory, and down from the rows blurred across that it keeps while the taps down still reach them.
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

// The tile a work group blurs, the most rows above and below it, and columns either side, that its taps can reach, and
// the rows it takes at a step (ALLightMapBlur::TILE_WIDTH, TILE_HEIGHT, MAX_APRON and STEP_ROWS). Tall, since the rows
// of the apron are blurred across again by the tiles above and below; a step's rows go one to each row of threads.
#define TILE_WIDTH 32
#define TILE_HEIGHT 256
#define MAX_APRON 16
#define STEP_ROWS 16

// A step's rows, each from the apron left of the tile to the apron right of it, and what a thread fetches of its row.
#define ROW_SPAN (TILE_WIDTH + 2 * MAX_APRON)
#define ROW_FETCHES (ROW_SPAN / TILE_WIDTH)
// The rows blurred across that a step's taps down read: its own and the apron's either side of them.
#define RING_ROWS (2 * MAX_APRON + STEP_ROWS)

layout(local_size_x = TILE_WIDTH, local_size_y = STEP_ROWS) in;

// The sun pass's light map: sun shadow (r), ambient occlusion (g), the spot lights' shadows (b, a).
uniform sampler2D lightMap;
// The rows above and below the tile, and the columns either side of it, that its taps reach, at most MAX_APRON.
uniform int blur_light_apron;
// The sun's shadow and the blurred occlusion, which the soften pass reads where it would read the light map's. Its size
// is the map's the tiles cover, the one ALLightMapBlur::dispatch sizes the grid by and checks the mirror's bound on.
layout(rg8) writeonly uniform image2D blurLightTarget;

uniform vec3 kern[4];

// The step's rows, from the apron's first column: the view depth of the texel a tap there reads, and its occlusion, as
// the light map's mirrored sampler reads it.
shared float rowDepth[STEP_ROWS][ROW_SPAN];
shared float rowOcclusion[STEP_ROWS][ROW_SPAN];
// The tile's columns of the rows blurred across that taps down may still read, a row's at the row modulo RING_ROWS: its
// view depth, and its occlusion blurred across, which the fragment passes keep in their scratch between the two.
shared float ringDepth[RING_ROWS][TILE_WIDTH];
shared float ringAcross[RING_ROWS][TILE_WIDTH];

// blurLightUtil.glsl's, member for member, as every unit of a program that passes it must declare it.
struct BlurLightPixel
{
    vec2  step;
    vec2  origin;
    vec3  plane;
    float offset;
    float tolerance;
};

float blurLightViewDepth(ivec2 texel);
bool blurLightDrawn(float view_depth);
BlurLightPixel blurLightPixel(ivec2 px, vec2 delta, float view_depth);
ivec2 blurLightTap(BlurLightPixel p, int i);
float blurLightWeight(BlurLightPixel p, int i, ivec2 texel, float view_depth);

// blurLightMirror for a texel within one mirror of the map, before it or past it by no more than its size, without the
// remainder: every texel a tile reads, since ALLightMapBlur dispatches only over a map a tile and its aprons wide and
// tall.
int mirrorNear(int i, int size)
{
    return i < 0 ? -1 - i : (i < size ? i : 2 * size - 1 - i);
}

int ringRow(int y)
{
    return y % RING_ROWS;
}

// The ring's row `offset` rows from the row at `centre`, which is no more than the ring is long.
int ringOffset(int centre, int offset)
{
    int row = centre + offset;
    row += row < 0 ? RING_ROWS : 0;
    return row >= RING_ROWS ? row - RING_ROWS : row;
}

// The occlusion at pixel `px`, of view depth `view_depth`, blurred along `delta`, (1, 0) across or (0, 1) down:
// blurLightTaps' taps and the fragment passes' sums, each tap's view depth and occlusion read from what this work group
// keeps. Across, the step's row `row` of the row caches, from column `left`; down, the ring of rows blurred across. A
// finite tap lies within `apron` of the pixel, which ALLightMapBlur::apron makes a step past the kernel's reach, so it
// is kept; one that is not has no position, so no weight, and what it reads is held to what is kept.
float blurOcclusion(ivec2 px, float view_depth, vec2 delta, int row, int left, int apron)
{
    ivec2 size = imageSize(blurLightTarget);
    BlurLightPixel p = blurLightPixel(px, delta, view_depth);

    bool across = delta.y == 0.0;
    int column = px.x - left;
    int low = max(px.y - apron, 0);
    int high = min(px.y + apron, size.y - 1);
    int centre = ringRow(px.y);
    float defined_weight = kern[0].y;
    float ao = defined_weight * (across ? rowOcclusion[row][column] : ringAcross[centre][column]);

    for (int i = 0; i < 12; ++i)
    {
        ivec2 texel = blurLightTap(p, i);

        // Across, the tap keeps the pixel's row, and its view depth and occlusion are the cache's at its column; down,
        // it keeps the column, its view depth is the clamped row's and its occlusion the mirrored row's blurred across.
        int tap_column = clamp(texel.x - left, 0, TILE_WIDTH + 2 * apron - 1);
        float tap_depth = across ? rowDepth[row][tap_column]
                                 : ringDepth[ringOffset(centre, clamp(texel.y, low, high) - px.y)][column];
        float weight = blurLightWeight(p, i, texel, tap_depth);
        float occlusion = across ? rowOcclusion[row][tap_column]
                                 : ringAcross[ringOffset(centre, clamp(mirrorNear(texel.y, size.y), low, high) - px.y)][column];
        ao += occlusion * weight;
        defined_weight += weight;
    }

    return max(ao / defined_weight, 0.0);
}

// What a thread fetches of a step's row `y` for the row caches: column `lx`, and every tile's width on from it, within
// the row's span from column `left`. Fetched a step ahead of the caches being written, so the fetches wait out a step.
void fetchRow(int y, int last, int lx, int left, int span, ivec2 size, out float view_depth[ROW_FETCHES],
              out float occlusion[ROW_FETCHES])
{
    for (int j = 0; j < ROW_FETCHES; ++j)
    {
        int c = lx + j * TILE_WIDTH;
        view_depth[j] = 0.0;
        occlusion[j] = 0.0;
        if (y < last && c < span)
        {
            ivec2 texel = ivec2(left + c, y);
            view_depth[j] = blurLightViewDepth(texel);
            occlusion[j] = texelFetch(lightMap, ivec2(mirrorNear(texel.x, size.x), y), 0).g;
        }
    }
}

void storeRow(int y, int last, int lx, int ly, int span, float view_depth[ROW_FETCHES], float occlusion[ROW_FETCHES])
{
    for (int j = 0; j < ROW_FETCHES; ++j)
    {
        int c = lx + j * TILE_WIDTH;
        if (y < last && c < span)
        {
            rowDepth[ly][c] = view_depth[j];
            rowOcclusion[ly][c] = occlusion[j];
        }
    }
}

void main()
{
    ivec2 size = imageSize(blurLightTarget);
    ivec2 tile = ivec2(gl_WorkGroupID.xy) * ivec2(TILE_WIDTH, TILE_HEIGHT);
    int lx = int(gl_LocalInvocationID.x);
    int ly = int(gl_LocalInvocationID.y);
    int x = tile.x + lx;
    int apron = blur_light_apron;
    int left = tile.x - apron;
    int span = TILE_WIDTH + 2 * apron;

    // The rows blurred across, the tile's and its apron's within the map, which holds the rows a mirrored tap reads
    // past its edges, and the tile's rows blurred down.
    int first = max(tile.y - apron, 0);
    int last = min(tile.y + TILE_HEIGHT + apron, size.y);
    int end = min(tile.y + TILE_HEIGHT, size.y);

    float depth_ahead[ROW_FETCHES];
    float occlusion_ahead[ROW_FETCHES];
    fetchRow(first + ly, last, lx, left, span, size, depth_ahead, occlusion_ahead);
    storeRow(first + ly, last, lx, ly, span, depth_ahead, occlusion_ahead);

    // A step's rows across, and the rows down whose taps the rows blurred across by then reach: those an apron above.
    int steps = (end - 1 + apron - first) / STEP_ROWS + 1;
    for (int s = 0; s < steps; ++s)
    {
        memoryBarrierShared();
        barrier();

        int y_across = first + s * STEP_ROWS + ly;
        fetchRow(y_across + STEP_ROWS, last, lx, left, span, size, depth_ahead, occlusion_ahead);

        // Across. The sky keeps the clear the fragment passes' scratch holds.
        if (y_across < last && x < size.x)
        {
            float view_depth = rowDepth[ly][lx + apron];
            float ao = 1.0;
            if (blurLightDrawn(view_depth))
            {
                ao = blurOcclusion(ivec2(x, y_across), view_depth, vec2(1.0, 0.0), ly, left, apron);
            }
            ringDepth[ringRow(y_across)][lx] = view_depth;
            ringAcross[ringRow(y_across)][lx] = ao;
        }

        memoryBarrierShared();
        barrier();

        storeRow(y_across + STEP_ROWS, last, lx, ly, span, depth_ahead, occlusion_ahead);

        // Down. The sky is the sun pass's clear, as the fragment passes leave it.
        int y = y_across - apron;
        if (y >= tile.y && y < end && x < size.x)
        {
            ivec2 px = ivec2(x, y);
            vec4 ccol = texelFetch(lightMap, px, 0);
            vec2 blurred = ccol.rg;
            float view_depth = ringDepth[ringRow(y)][lx];
            if (blurLightDrawn(view_depth))
            {
                blurred = vec2(ccol.r, blurOcclusion(px, view_depth, vec2(0.0, 1.0), 0, tile.x, apron));
            }
            imageStore(blurLightTarget, px, vec4(blurred, 0.0, 0.0));
        }
    }
}
