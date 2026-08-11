/**
 * @file llrender2dutils.h
 * @brief GL function declarations for immediate-mode gl drawing.
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

// All immediate-mode gl drawing should happen here.


#ifndef LL_RENDER2DUTILS_H
#define LL_RENDER2DUTILS_H

#include "llpointer.h"      // LLPointer<>
#include "llrect.h"
#include "llsingleton.h"
#include "llglslshader.h"
#include "v2math.h"

#include <vector>

class LLColor4;
class LLVector3;
class LLUIImage;
class LLUUID;

extern const LLColor4 UI_VERTEX_COLOR;

bool ui_point_in_rect(S32 x, S32 y, S32 left, S32 top, S32 right, S32 bottom);
void gl_state_for_2d(S32 width, S32 height);

void gl_line_2d(S32 x1, S32 y1, S32 x2, S32 y2);
void gl_line_2d(S32 x1, S32 y1, S32 x2, S32 y2, const LLColor4 &color );

// Anti-aliased polyline through `points`, drawn as a ribbon of triangles with
// a one-pixel alpha falloff along each edge.
//
// GL_LINE_SMOOTH is not used, and deliberately: it appears nowhere in this
// tree, core profiles routinely ignore it, and LLRender::setLineWidth already
// clamps to mAliasedLineRange -- which is [1,1] on most core drivers -- so
// neither smoothing nor width can be relied on from the fixed pipeline. Doing
// the coverage by hand costs a few triangles per segment and looks the same
// everywhere.
//
// Joins are mitred, so a continuous curve has no notches at its vertices. The
// mitre is clamped, so a hairpin turn is blunted rather than shooting off to
// infinity. Fewer than two points draws nothing; coincident points are skipped.
void gl_polyline_2d(const std::vector<LLVector2>& points, const LLColor4& color,
                    F32 width = 1.f, bool closed = false);

// The area between `points` and the horizontal line y = `baseline_y`, flat
// filled. The companion to gl_polyline_2d for a graph whose meaning is how much
// is under the curve rather than where the curve runs -- a weight band, an
// occupancy plot -- where an outline alone reads as three crossing lines.
//
// The edge is left aliased: pass a translucent colour, as such a fill wants,
// and the polyline's feathered skirt would double up against the fill it sits
// on and draw a darker seam along the top. Outline it with gl_polyline_2d if a
// crisp edge is wanted; then the two feathers sit on the same path.
void gl_polyfill_2d(const std::vector<LLVector2>& points, F32 baseline_y,
                    const LLColor4& color);
void gl_triangle_2d(S32 x1, S32 y1, S32 x2, S32 y2, S32 x3, S32 y3, const LLColor4& color, bool filled);
void gl_rect_2d_simple( S32 width, S32 height );

void gl_draw_x(const LLRect& rect, const LLColor4& color);

void gl_rect_2d(S32 left, S32 top, S32 right, S32 bottom, bool filled = true );
void gl_rect_2d(S32 left, S32 top, S32 right, S32 bottom, const LLColor4 &color, bool filled = true );
void gl_rect_2d_offset_local( S32 left, S32 top, S32 right, S32 bottom, const LLColor4 &color, S32 pixel_offset = 0, bool filled = true );
void gl_rect_2d_offset_local( S32 left, S32 top, S32 right, S32 bottom, S32 pixel_offset = 0, bool filled = true );
void gl_rect_2d(const LLRect& rect, bool filled = true );
void gl_rect_2d(const LLRect& rect, const LLColor4& color, bool filled = true );
void gl_rect_2d_checkerboard(const LLRect& rect, GLfloat alpha = 1.0f);

void gl_drop_shadow(S32 left, S32 top, S32 right, S32 bottom, const LLColor4 &start_color, S32 lines);

void gl_circle_2d(F32 x, F32 y, F32 radius, S32 steps, bool filled);
void gl_arc_2d(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, F32 start_angle, F32 end_angle);
void gl_deep_circle( F32 radius, F32 depth );
void gl_ring( F32 radius, F32 width, const LLColor4& center_color, const LLColor4& side_color, S32 steps, bool render_center );
void gl_corners_2d(S32 left, S32 top, S32 right, S32 bottom, S32 length, F32 max_frac);
void gl_washer_2d(F32 outer_radius, F32 inner_radius, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color);
void gl_washer_segment_2d(F32 outer_radius, F32 inner_radius, F32 start_radians, F32 end_radians, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color);

// A washer whose colour varies AROUND the sweep rather than across it: one
// entry in `colors` per step, wrapping back to the first. The washers above
// take a single inner and outer colour held constant along the arc, which can
// only ever make a radial gradient -- this is the angular one, and it is what
// a hue ring needs.
//
// `inner_fade` scales each colour's alpha at the inner edge, so a ring can
// fall off toward its centre instead of ending in a hard step.
//
// Like its siblings and unlike gl_circle_2d, this draws around the CURRENT
// origin and does not push the UI matrix -- wrap it in gGL.pushUIMatrix() and
// translateUI() yourself. Fewer than three colours draws nothing.
void gl_washer_angular_2d(F32 outer_radius, F32 inner_radius,
                          const std::vector<LLColor4>& colors, F32 inner_fade = 1.f);

void gl_draw_image(S32 x, S32 y, LLTexture* image, const LLColor4& color = UI_VERTEX_COLOR, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f));
void gl_draw_scaled_target(S32 x, S32 y, S32 width, S32 height, LLRenderTarget* target, const LLColor4& color = UI_VERTEX_COLOR, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f));
void gl_draw_scaled_image(S32 x, S32 y, S32 width, S32 height, LLTexture* image, const LLColor4& color = UI_VERTEX_COLOR, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f));
void gl_draw_rotated_image(S32 x, S32 y, F32 degrees, LLTexture* image, const LLColor4& color = UI_VERTEX_COLOR, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f));
void gl_draw_scaled_rotated_image(S32 x, S32 y, S32 width, S32 height, F32 degrees, LLTexture* image, const LLColor4& color = UI_VERTEX_COLOR, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f), LLRenderTarget* target = NULL);
void gl_draw_scaled_image_with_border(S32 x, S32 y, S32 border_width, S32 border_height, S32 width, S32 height, LLTexture* image, const LLColor4 &color, bool solid_color = false, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f), bool scale_inner = true);
void gl_draw_scaled_image_with_border(S32 x, S32 y, S32 width, S32 height, LLTexture* image, const LLColor4 &color, bool solid_color = false, const LLRectf& uv_rect = LLRectf(0.f, 1.f, 1.f, 0.f), const LLRectf& scale_rect = LLRectf(0.f, 1.f, 1.f, 0.f), bool scale_inner = true);

void gl_line_3d( const LLVector3& start, const LLVector3& end, const LLColor4& color);

void gl_rect_2d_simple_tex( S32 width, S32 height );

// segmented rectangles

/*
   TL |______TOP_________| TR
     /|                  |\
   _/_|__________________|_\_
   L| |    MIDDLE        | |R
   _|_|__________________|_|_
    \ |    BOTTOM        | /
   BL\|__________________|/ BR
      |                  |
*/

typedef enum e_rounded_edge
{
    ROUNDED_RECT_LEFT   = 0x1,
    ROUNDED_RECT_TOP    = 0x2,
    ROUNDED_RECT_RIGHT  = 0x4,
    ROUNDED_RECT_BOTTOM = 0x8,
    ROUNDED_RECT_ALL    = 0xf
}ERoundedEdge;


void gl_segmented_rect_2d_tex(const S32 left, const S32 top, const S32 right, const S32 bottom, const S32 texture_width, const S32 texture_height, const S32 border_size, const U32 edges = ROUNDED_RECT_ALL);
void gl_segmented_rect_2d_fragment_tex(const LLRect& rect, const S32 texture_width, const S32 texture_height, const S32 border_size, const F32 start_fragment, const F32 end_fragment, const U32 edges = ROUNDED_RECT_ALL);
void gl_segmented_rect_3d_tex(const LLRectf& clip_rect, const LLRectf& center_uv_rect, const LLRectf& center_draw_rect, const LLVector3& width_vec, const LLVector3& height_vec);

inline void gl_rect_2d( const LLRect& rect, bool filled )
{
    gl_rect_2d( rect.mLeft, rect.mTop, rect.mRight, rect.mBottom, filled );
}

inline void gl_rect_2d_offset_local( const LLRect& rect, S32 pixel_offset, bool filled)
{
    gl_rect_2d_offset_local( rect.mLeft, rect.mTop, rect.mRight, rect.mBottom, pixel_offset, filled );
}

class LLImageProviderInterface;

class LLRender2D : public LLSimpleton<LLRender2D>
{
    LOG_CLASS(LLRender2D);
public:
    LLRender2D(LLImageProviderInterface* image_provider);
    ~LLRender2D();

    static void pushMatrix();
    static void popMatrix();
    static void loadIdentity();
    static void translate(F32 x, F32 y, F32 z = 0.0f);

    static void setLineWidth(F32 width);

    LLPointer<LLUIImage> getUIImageByID(const LLUUID& image_id, S32 priority = 0);
    LLPointer<LLUIImage> getUIImage(std::string_view name, S32 priority = 0);

protected:
    // since LLRender2D has no control of image provider's lifecycle
    // we need a way to tell LLRender2D that provider died and
    // LLRender2D needs to be updated.
    static void resetProvider();

private:
    LLImageProviderInterface* mImageProvider;
};

class LLImageProviderInterface
{
protected:
    LLImageProviderInterface() {};
    virtual ~LLImageProviderInterface();
public:
    virtual LLPointer<LLUIImage> getUIImage(std::string_view name, S32 priority) = 0;
    virtual LLPointer<LLUIImage> getUIImageByID(const LLUUID& id, S32 priority) = 0;
    virtual void cleanUp() = 0;

    // to notify holders when pointer gets deleted
    typedef void(*callback_t)();
    void addOnRemovalCallback(callback_t func);
    void deleteOnRemovalCallback(callback_t func);

private:

    typedef std::list< callback_t > callback_list_t;
    callback_list_t mCallbackList;
};


extern LLGLSLShader gSolidColorProgram;
extern LLGLSLShader gUIProgram;

#endif // LL_RENDER2DUTILS_H

