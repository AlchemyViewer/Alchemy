/**
 * @file llhudnametag.h
 * @brief Name tags for avatars
 * @author James Cook
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
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

#ifndef LLHUDNAMETAG_H
#define LLHUDNAMETAG_H

#include "llpointer.h"

#include "llhudobject.h"
#include "v4color.h"
//#include "v4coloru.h"
#include "v2math.h"
#include "llrect.h"
//#include "llframetimer.h"
#include "llfontgl.h"
#include "llfonttextcache.h"
#include <set>
#include <vector>

class LLHUDNameTag;
class LLHUDTextScope;
class LLUIImage;

struct llhudnametag_further_away
{
    bool operator()(const LLPointer<LLHUDNameTag>& lhs, const LLPointer<LLHUDNameTag>& rhs) const;
};

class LLHUDNameTag : public LLHUDObject
{
protected:
    class LLHUDTextSegment
    {
    public:
        LLHUDTextSegment(std::string text, const LLFontGL::StyleFlags style, const LLColor4& color, const LLFontGL* font)
        :   mColor(color),
            mStyle(style),
            mText(std::move(text)),
            mFont(font)
        {}
        F32 getWidth(const LLFontGL* font);
        const std::string& getText() const { return mText; }

        // Draws this line through the scope, from the same cache the width
        // came out of -- one piece of text, one place its work is kept.
        void draw(LLHUDTextScope& scope, const LLFontGL& font, U8 style,
                  LLFontGL::ShadowType shadow, F32 x_offset, F32 y_offset,
                  const LLColor4& color) const;

        // Let go of the shaped glyphs, keeping the segment. For text that has
        // gone off screen and may come back.
        void releaseGeometry() const { mFontCache.reset(); }

        LLColor4                mColor;
        LLFontGL::StyleFlags    mStyle;
        const LLFontGL*         mFont;
    private:
        std::string             mText;

        // What this segment's text costs to measure and to draw, kept between
        // frames. A segment is measured once per frame per font to place it
        // and then drawn, and its text never changes -- segments are rebuilt
        // wholesale, not edited. The cache notices a font or scale change for
        // itself, which is what the manual sweep on reshape used to be for.
        mutable LLFontTextCache mFontCache;
    };

public:
    typedef enum e_text_alignment
    {
        ALIGN_TEXT_LEFT,
        ALIGN_TEXT_CENTER
    } ETextAlignment;

    typedef enum e_vert_alignment
    {
        ALIGN_VERT_TOP,
        ALIGN_VERT_CENTER
    } EVertAlignment;

    static const F32 NAMETAG_MAX_WIDTH; // 298px, made to fit 31 M's
    static const F32 HUD_TEXT_MAX_WIDTH; // 190px

public:
    // How many lines have been added since the text was last cleared, and a
    // way to recolour one of them without disturbing what it has shaped. A
    // line that wraps becomes several segments, so a caller that added it has
    // no other way to find them again. Chat bubbles fade continuously while
    // their text stands still, and rebuilding for a colour reshaped every
    // line of every chatting avatar on screen, every frame.
    S32 getNumLines() const { return (S32)mLineSegmentCounts.size(); }
    void setLineColor(S32 line_index, const LLColor4& color);

    // Set entire string, eliminating existing lines
    void setString(const std::string& text_utf8);

    void clearString();

    // Add text a line at a time, allowing custom formatting
    void addLine(
        const std::string &text_utf8,
        const LLColor4& color,
        const LLFontGL::StyleFlags style = LLFontGL::NORMAL,
        const LLFontGL* font = NULL,
        const bool use_ellipses = false,
        F32 max_pixels = HUD_TEXT_MAX_WIDTH);

    // For bubble chat, set the part above the chat text
    void setLabel(const std::string& label_utf8);
    void addLabel(const std::string& label_utf8, F32 max_pixels = HUD_TEXT_MAX_WIDTH);

    // Sets the default font for lines with no font specified
    void setFont(const LLFontGL* font);
    void setColor(const LLColor4 &color);
    void setAlpha(F32 alpha);
    void setZCompare(const bool zcompare);
    void setDoFade(const bool do_fade);
    void setVisibleOffScreen(bool visible) { mVisibleOffScreen = visible; }

    // mMaxLines of -1 means unlimited lines.
    void setMaxLines(S32 max_lines) { mMaxLines = max_lines; }
    void setFadeDistance(F32 fade_distance, F32 fade_range) { mFadeDistance = fade_distance; mFadeRange = fade_range; }
    void updateVisibility();
    LLVector2 updateScreenPos(const LLVector2 &offset_target);
    void updateSize();
//  void setMass(F32 mass) { mMass = llmax(0.1f, mass); }
    void setTextAlignment(ETextAlignment alignment) { mTextAlignment = alignment; }
    void setVertAlignment(EVertAlignment alignment) { mVertAlignment = alignment; }
    /*virtual*/ void markDead();
    friend class LLHUDObject;
    /*virtual*/ F32 getDistance() const { return mLastDistance; }
    S32  getLOD() const { return mLOD; }
    bool getVisible() const { return mVisible; }
    bool getHidden() const { return mHidden; }
    void setHidden( bool hide ) { mHidden = hide; }

    // Drop what every line of this tag has shaped, keeping the lines. Called
    // when the tag leaves the screen, so only what is on it holds geometry.
    void releaseTextGeometry();

    void shift(const LLVector3& offset);
    F32 getWorldHeight() const;

    bool lineSegmentIntersect(const LLVector4a& start, const LLVector4a& end, LLVector4a& intersection, bool debug_render = false);

    static void shiftAll(const LLVector3& offset);
    static void addPickable(std::set<LLViewerObject*> &pick_list);
    static void reshape();
    static void setDisplayText(bool flag) { sDisplayText = flag ; }

protected:
    LLHUDNameTag(const U8 type);

    /*virtual*/ void render();
    void renderText();
    static void updateAll();
    void setLOD(S32 lod);
    S32 getMaxLines();

private:
    ~LLHUDNameTag();
    bool            mDoFade;
    F32             mFadeRange;
    F32             mFadeDistance;
    F32             mLastDistance;
    bool            mZCompare;
    bool            mVisibleOffScreen;
    bool            mOffscreen;
    LLColor4        mColor;
//  LLVector3       mScale;
    F32             mWidth;
    F32             mHeight;
//  LLColor4U       mPickColor;
    const LLFontGL* mFontp;
    const LLFontGL* mBoldFontp;
    LLRectf         mSoftScreenRect;
    LLVector3       mPositionAgent;
    // How far a screen pixel reaches in the world at this tag's position,
    // taken once per frame alongside the position it is derived from.
    // Deriving it costs a tangent and three window lookups, and the overlap
    // pass below asks for a tag's screen rectangle once per overlapping
    // neighbour per iteration -- thousands of times over, for an answer that
    // cannot change while the frame is being built.
    LLVector3       mPixelUpVec;
    LLVector3       mPixelRightVec;
    LLVector2       mPositionOffset;
    LLVector2       mTargetPositionOffset;
    F32             mMass;
    S32             mMaxLines;
    S32             mOffsetY;
    F32             mRadius;
    std::vector<LLHUDTextSegment> mTextSegments;
    std::vector<LLHUDTextSegment> mLabelSegments;
    // Segments produced by each addLine, in call order. One line wraps into
    // as many as it needs, and this is what maps a line back to them.
    std::vector<S32>              mLineSegmentCounts;
//  LLFrameTimer    mResizeTimer;
    ETextAlignment  mTextAlignment;
    EVertAlignment  mVertAlignment;
    S32             mLOD;
    bool            mHidden;
    LLPointer<LLUIImage> mRoundedRectImgp;
    LLPointer<LLUIImage> mRoundedRectTopImgp;

    static bool    sDisplayText ;
    static std::set<LLPointer<LLHUDNameTag> > sTextObjects;
    // The same tags in one run of memory, for the passes that visit all of
    // them every frame. The set stays the authority on what exists -- it is
    // what insertion and removal work against, and removal can happen while a
    // pass is running -- so this is rebuilt from it only when it changes, and
    // holds its own references so a removal mid-pass cannot pull a tag out
    // from under one.
    static std::vector<LLPointer<LLHUDNameTag> > sAllTextObjects;
    static bool sAllTextObjectsDirty;
    static const std::vector<LLPointer<LLHUDNameTag> >& getAllTextObjects();
    static std::vector<LLPointer<LLHUDNameTag> > sVisibleTextObjects;
//  static std::vector<LLPointer<LLHUDNameTag> > sVisibleHUDTextObjects;
    typedef std::set<LLPointer<LLHUDNameTag> >::iterator TextObjectIterator;
    typedef std::vector<LLPointer<LLHUDNameTag> >::iterator VisibleTextObjectIterator;
};

#endif
