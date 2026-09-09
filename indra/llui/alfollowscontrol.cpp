/**
 * @file alfollowscontrol.cpp
 * @brief The four bits of `follows`, drawn as the thing they do.
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

#include "alfollowscontrol.h"

#include "llrender2dutils.h"
#include "lltooltip.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<ALFollowsControl> r("follows_control");

namespace
{
    constexpr S32 GAP = 8;          // between the picture and the thumbnails
    constexpr S32 LEAST_SIDE = 28;  // a picture smaller than this says nothing
    constexpr F32 LEAST_MARGIN = 0.16f;
    constexpr F32 MOST_MARGIN = 0.34f;
    // The parent the two thumbnails are drawn from, and the two they are
    // drawn at, as fractions of a thumbnail's side. Both fit in the same
    // box, and the box is what the larger of them fills.
    constexpr F32 THUMB_REFERENCE = 0.84f;
    constexpr F32 THUMB_SMALLER = 0.68f;
    constexpr F32 THUMB_LARGER = 1.f;

    bool sameWord(std::string_view a, std::string_view b)
    {
        return a.size() == b.size()
            && std::equal(a.begin(), a.end(), b.begin(),
                          [](char x, char y) { return LLStringOps::toLower(x) == LLStringOps::toLower(y); });
    }
}

ALFollowsControl::ALFollowsControl(const Params& p)
:   LLUICtrl(p)
{
}

void ALFollowsControl::setEdges(std::string left, std::string bottom, std::string right, std::string top,
                                std::string all_word, std::string none_word)
{
    mNames[LEFT] = std::move(left);
    mNames[BOTTOM] = std::move(bottom);
    mNames[RIGHT] = std::move(right);
    mNames[TOP] = std::move(top);
    mAll = std::move(all_word);
    mNone = std::move(none_word);
}

// The margins the element leaves inside its parent, as fractions, kept far
// enough from nought and from a half that every strut has a length and the
// element still has a middle to put a spring across.
void ALFollowsControl::setSubject(const LLRect& child, const LLRect& parent)
{
    const F32 width = (F32)parent.getWidth();
    const F32 height = (F32)parent.getHeight();
    if (width <= 0.f || height <= 0.f)
    {
        return;
    }
    mMargin[LEFT]   = llclamp((F32)(child.mLeft - parent.mLeft) / width, LEAST_MARGIN, MOST_MARGIN);
    mMargin[RIGHT]  = llclamp((F32)(parent.mRight - child.mRight) / width, LEAST_MARGIN, MOST_MARGIN);
    mMargin[BOTTOM] = llclamp((F32)(child.mBottom - parent.mBottom) / height, LEAST_MARGIN, MOST_MARGIN);
    mMargin[TOP]    = llclamp((F32)(parent.mTop - child.mTop) / height, LEAST_MARGIN, MOST_MARGIN);
}

void ALFollowsControl::setValue(const LLSD& value)
{
    const std::string text = value.asString();
    std::fill(std::begin(mSet), std::end(mSet), false);

    for (size_t start = 0; start <= text.size(); )
    {
        const size_t bar = text.find('|', start);
        const std::string_view token(text.data() + start,
                                     (bar == std::string::npos ? text.size() : bar) - start);
        if (!mAll.empty() && sameWord(token, mAll))
        {
            std::fill(std::begin(mSet), std::end(mSet), true);
        }
        else
        {
            for (S32 edge = 0; edge < EDGES; ++edge)
            {
                if (!mNames[edge].empty() && sameWord(token, mNames[edge]))
                {
                    mSet[edge] = true;
                }
            }
        }
        if (bar == std::string::npos)
        {
            break;
        }
        start = bar + 1;
    }
}

LLSD ALFollowsControl::getValue() const
{
    const bool every = std::all_of(std::begin(mSet), std::end(mSet), [](bool on) { return on; });
    const bool any = std::any_of(std::begin(mSet), std::end(mSet), [](bool on) { return on; });
    if (every && !mAll.empty())
    {
        return mAll;
    }
    if (!any)
    {
        return mNone;
    }
    std::string out;
    for (S32 edge = 0; edge < EDGES; ++edge)
    {
        if (mSet[edge] && !mNames[edge].empty())
        {
            out += out.empty() ? mNames[edge] : "|" + mNames[edge];
        }
    }
    return out;
}

// static
// A view keeps its distance from the edges it follows. Following both edges
// of a dimension is following two distances that cannot both be kept at one
// size, so the size gives; following neither leaves it where it is, which is
// what `LLView::reshape` does with a view that follows nothing.
LLRect ALFollowsControl::follow(const LLRect& child, S32 grow_width, S32 grow_height,
                                bool left, bool bottom, bool right, bool top)
{
    LLRect out(child);
    if (left && right)
    {
        out.mRight += grow_width;
    }
    else if (right)
    {
        out.mLeft += grow_width;
        out.mRight += grow_width;
    }
    if (top && bottom)
    {
        out.mTop += grow_height;
    }
    else if (top)
    {
        out.mBottom += grow_height;
        out.mTop += grow_height;
    }
    return out;
}

// The picture is a square at the left, as tall as there is room for, and the
// two thumbnails are squares of the same side beside it. A control too
// narrow for three of them keeps the picture, which is the half that is
// clicked.
LLRect ALFollowsControl::pictureRect() const
{
    const S32 height = getRect().getHeight();
    const S32 width = getRect().getWidth();
    const S32 thirds = (width - 2 * GAP) / 3;
    const S32 side = llmin(height, thirds >= LEAST_SIDE ? thirds : width);
    if (side <= 0)
    {
        return LLRect();
    }
    return LLRect(0, height, side, height - side);
}

LLRect ALFollowsControl::thumbRect(S32 which) const
{
    const LLRect picture = pictureRect();
    const S32 side = picture.getWidth();
    const S32 left = picture.mRight + GAP + which * (side + GAP);
    if (side < LEAST_SIDE || left + side > getRect().getWidth())
    {
        return LLRect();
    }
    return LLRect(left, picture.mTop, left + side, picture.mBottom);
}

LLRect ALFollowsControl::childIn(const LLRect& parent) const
{
    const F32 width = (F32)parent.getWidth();
    const F32 height = (F32)parent.getHeight();
    return LLRect(parent.mLeft + ll_round(width * mMargin[LEFT]),
                  parent.mTop - ll_round(height * mMargin[TOP]),
                  parent.mRight - ll_round(width * mMargin[RIGHT]),
                  parent.mBottom + ll_round(height * mMargin[BOTTOM]));
}

S32 ALFollowsControl::partAt(S32 x, S32 y) const
{
    const LLRect picture = pictureRect();
    if (!picture.pointInRect(x, y))
    {
        return NONE;
    }
    const LLRect child = childIn(picture);
    if (child.pointInRect(x, y))
    {
        // The two springs cross in the middle, so the one that is meant is
        // the one whose line the point is nearer: a click along the middle
        // row is the spring that runs along it whatever else it is near.
        const S32 across = llabs(y - (child.mTop + child.mBottom) / 2);
        const S32 down = llabs(x - (child.mLeft + child.mRight) / 2);
        return across <= down ? SPRING_ACROSS : SPRING_DOWN;
    }
    // Outside the element and inside the parent: the nearest edge is the
    // one the point is beyond.
    if (x < child.mLeft)   { return STRUT + LEFT; }
    if (x > child.mRight)  { return STRUT + RIGHT; }
    if (y < child.mBottom) { return STRUT + BOTTOM; }
    return STRUT + TOP;
}

std::string ALFollowsControl::partName(S32 part) const
{
    if (part >= STRUT && part < EDGES)
    {
        return mNames[part];
    }
    const S32 a = part == SPRING_ACROSS ? LEFT : BOTTOM;
    const S32 b = part == SPRING_ACROSS ? RIGHT : TOP;
    return mNames[a] + "|" + mNames[b];
}

bool ALFollowsControl::handleMouseDown(S32 x, S32 y, MASK mask)
{
    const S32 part = partAt(x, y);
    if (part == NONE)
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }
    if (part < EDGES)
    {
        mSet[part] = !mSet[part];
    }
    else
    {
        // A spring is both of its edges: it goes on by holding them apart
        // and off by letting them both go.
        const S32 a = part == SPRING_ACROSS ? LEFT : BOTTOM;
        const S32 b = part == SPRING_ACROSS ? RIGHT : TOP;
        const bool on = mSet[a] && mSet[b];
        mSet[a] = mSet[b] = !on;
    }
    onCommit();
    return true;
}

bool ALFollowsControl::handleToolTip(S32 x, S32 y, MASK mask)
{
    const S32 part = partAt(x, y);
    if (part == NONE || mNames[LEFT].empty())
    {
        return LLUICtrl::handleToolTip(x, y, mask);
    }
    LLToolTipMgr::instance().show(partName(part));
    return true;
}

void ALFollowsControl::drawFrame(const LLRect& parent, const LLRect& child, bool live) const
{
    static const LLUIColor frame = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);
    static const LLUIColor ink = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);

    LLColor4 fill(ink.get());
    fill.mV[VALPHA] = live ? 0.22f : 0.12f;
    gl_rect_2d(parent, frame.get(), false);
    gl_rect_2d(child, fill, true);
    gl_rect_2d(child, live ? ink.get() : frame.get(), false);
}

// A strut runs from the middle of an edge of the element to the edge of the
// parent it holds it away from, with a bar across the far end: a set edge is
// drawn as the thing that is holding it.
void ALFollowsControl::drawStrut(const LLRect& parent, const LLRect& child, S32 edge) const
{
    static const LLUIColor set = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);
    static const LLUIColor unset = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);

    const bool on = mSet[edge];
    const LLColor4 colour = on ? set.get() : unset.get();
    const S32 mid_x = (child.mLeft + child.mRight) / 2;
    const S32 mid_y = (child.mTop + child.mBottom) / 2;
    constexpr S32 CAP = 4;

    switch (edge)
    {
    case LEFT:
        gl_line_2d(parent.mLeft, mid_y, child.mLeft, mid_y, colour);
        if (on) { gl_line_2d(parent.mLeft + 1, mid_y - CAP, parent.mLeft + 1, mid_y + CAP, colour); }
        break;
    case RIGHT:
        gl_line_2d(child.mRight, mid_y, parent.mRight, mid_y, colour);
        if (on) { gl_line_2d(parent.mRight - 1, mid_y - CAP, parent.mRight - 1, mid_y + CAP, colour); }
        break;
    case BOTTOM:
        gl_line_2d(mid_x, parent.mBottom, mid_x, child.mBottom, colour);
        if (on) { gl_line_2d(mid_x - CAP, parent.mBottom + 1, mid_x + CAP, parent.mBottom + 1, colour); }
        break;
    default:
        gl_line_2d(mid_x, child.mTop, mid_x, parent.mTop, colour);
        if (on) { gl_line_2d(mid_x - CAP, parent.mTop - 1, mid_x + CAP, parent.mTop - 1, colour); }
        break;
    }
}

// A spring at rest is a line. What makes it a spring is that both of its
// edges are held, so it is drawn coiled exactly when it is the dimension
// that has to give.
void ALFollowsControl::drawSpring(S32 x0, S32 y0, S32 x1, S32 y1, bool on) const
{
    static const LLUIColor set = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);
    static const LLUIColor unset = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);

    const F32 dx = (F32)(x1 - x0);
    const F32 dy = (F32)(y1 - y0);
    const F32 length = sqrtf(dx * dx + dy * dy);
    if (length < 6.f)
    {
        return;
    }
    const LLColor4 colour = on ? set.get() : unset.get();
    if (!on)
    {
        gl_line_2d(x0, y0, x1, y1, colour);
        return;
    }

    constexpr S32 SEGMENTS = 8;
    const F32 across_x = -dy / length;
    const F32 across_y = dx / length;
    const F32 swing = llmin(4.f, length / 6.f);
    S32 at_x = x0;
    S32 at_y = y0;
    for (S32 i = 1; i <= SEGMENTS; ++i)
    {
        const F32 along = (F32)i / SEGMENTS;
        const F32 out = (i == SEGMENTS) ? 0.f : ((i & 1) ? swing : -swing);
        const S32 to_x = ll_round((F32)x0 + dx * along + across_x * out);
        const S32 to_y = ll_round((F32)y0 + dy * along + across_y * out);
        gl_line_2d(at_x, at_y, to_x, to_y, colour);
        at_x = to_x;
        at_y = to_y;
    }
}

void ALFollowsControl::draw()
{
    const LLRect picture = pictureRect();
    if (picture.getWidth() <= 0 || picture.getHeight() <= 0)
    {
        LLUICtrl::draw();
        return;
    }

    const LLRect child = childIn(picture);
    drawFrame(picture, child, true);
    for (S32 edge = 0; edge < EDGES; ++edge)
    {
        drawStrut(picture, child, edge);
    }
    drawSpring(child.mLeft + 2, (child.mTop + child.mBottom) / 2,
               child.mRight - 2, (child.mTop + child.mBottom) / 2,
               mSet[LEFT] && mSet[RIGHT]);
    drawSpring((child.mLeft + child.mRight) / 2, child.mBottom + 2,
               (child.mLeft + child.mRight) / 2, child.mTop - 2,
               mSet[BOTTOM] && mSet[TOP]);

    // The same element in a parent that is smaller and one that is larger,
    // drawn from the picture's own proportions by the arithmetic the flags
    // stand for. Both are anchored at the top left, which is where a view's
    // parent grows from as far as its children are concerned.
    const S32 side = picture.getWidth();
    const S32 reference = ll_round((F32)side * THUMB_REFERENCE);
    for (S32 which = 0; which < 2; ++which)
    {
        const LLRect box = thumbRect(which);
        if (box.isEmpty())
        {
            continue;
        }
        const S32 grown = ll_round((F32)side * (which == 0 ? THUMB_SMALLER : THUMB_LARGER)) - reference;
        // Both are drawn from the same bottom left corner, because that is
        // the corner a view's rect grows from and so the corner the four
        // bits are arithmetic about.
        const LLRect was(box.mLeft, box.mBottom + reference, box.mLeft + reference, box.mBottom);
        const LLRect parent(box.mLeft, was.mTop + grown, was.mRight + grown, box.mBottom);
        drawFrame(parent, follow(childIn(was), grown, grown,
                                 mSet[LEFT], mSet[BOTTOM], mSet[RIGHT], mSet[TOP]), false);
    }

    LLUICtrl::draw();
}
