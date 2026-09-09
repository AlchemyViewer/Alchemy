/**
 * @file alfollowscontrol.h
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

#pragma once

#include "lluictrl.h"

#include <string>

// `follows` is four bits that decide where an element goes when the thing
// it is in changes size, and four check boxes say only that four words
// exist. The bits read two ways, and both readings are the same four bits:
//
//  - a **strut** on an edge is that edge's bit. It holds the distance
//    between the element and its parent on that side.
//  - a **spring** across a dimension is both of that dimension's bits at
//    once. Two edges held apart is a width that has to give, which is what
//    "this dimension flexes" means and the only way to say it.
//
// So the picture is a box in a box with four struts and two springs, a
// strut is one bit and a spring is two, and beside it the same element is
// drawn in a smaller parent and a larger one, which is the question anybody
// setting this is actually asking.
//
// The names of the edges are the caller's: they are what a file writes, and
// this library does not know which vocabulary it is being used for.
class ALFollowsControl : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALFollowsControl, LLUICtrl);

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Params() {}
    };

    // The four names a file writes for the edges, in the order the picture
    // draws them, and the two words that stand for every edge and for none.
    // Either word may be empty, and then the list is written out instead.
    void setEdges(std::string left, std::string bottom, std::string right, std::string top,
                  std::string all_word, std::string none_word);

    // What the picture is a picture of. A control that is not told draws a
    // box half the size of the one it is in, since what it is showing then
    // is the rule rather than any particular element.
    void setSubject(const LLRect& child, const LLRect& parent);

    // The value as a file writes it, read against the names given: a name
    // the list does not carry is passed over, which is what the parsers do.
    void setValue(const LLSD& value) override;
    LLSD getValue() const override;

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleToolTip(S32 x, S32 y, MASK mask) override;

    // Where a child lands when the thing it is in grows by this much. This
    // is the whole meaning of the four bits, and it is `LLView::reshape`'s
    // arithmetic written where it can be drawn and tested.
    static LLRect follow(const LLRect& child, S32 grow_width, S32 grow_height,
                         bool left, bool bottom, bool right, bool top);

protected:
    friend class LLUICtrlFactory;
    ALFollowsControl(const Params& p);

private:
    // The order the picture draws them in, which is the order the four
    // names are given in.
    enum Edge : S32 { LEFT, BOTTOM, RIGHT, TOP, EDGES };

    // The six things a click can land on: four edges and two dimensions.
    enum Part : S32 { NONE = -1, STRUT = 0, SPRING_ACROSS = EDGES, SPRING_DOWN, PARTS };

    // Which part of the picture is under a point, or NONE.
    S32 partAt(S32 x, S32 y) const;
    // What that part is called, in the words a file writes: an edge is its
    // own name and a dimension is the two it holds apart.
    std::string partName(S32 part) const;

    // The box the picture is drawn in, and the two beside it. Empty where
    // the control is too narrow to carry them.
    LLRect pictureRect() const;
    LLRect thumbRect(S32 which) const;
    // The element inside a parent this size, in the picture's proportions.
    LLRect childIn(const LLRect& parent) const;

    void drawFrame(const LLRect& parent, const LLRect& child, bool live) const;
    void drawStrut(const LLRect& parent, const LLRect& child, S32 edge) const;
    void drawSpring(S32 x0, S32 y0, S32 x1, S32 y1, bool on) const;

    bool        mSet[EDGES] = { false, false, false, false };
    std::string mNames[EDGES];
    std::string mAll;
    std::string mNone;
    // The proportions of the picture: what fraction of the parent each
    // margin takes. Clamped so a strut always has somewhere to be clicked.
    F32         mMargin[EDGES] = { 0.25f, 0.25f, 0.25f, 0.25f };
};
