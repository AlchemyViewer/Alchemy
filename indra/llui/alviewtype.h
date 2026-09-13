/**
 * @file alviewtype.h
 * @brief What a view is, for the questions asked of it every frame
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

#ifndef AL_ALVIEWTYPE_H
#define AL_ALVIEWTYPE_H

#include "stdtypes.h"

#include <type_traits>

class LLView;

// A view class's place in the hierarchy: its ancestors by depth. Every view
// class declares one with AL_VIEW_TYPE, and LLView::as<T>() asks whether the
// ancestor at T's depth is T, or the class itself is: one indexed load and
// one compare, for any number of classes at any depth. A class built without
// its own declaration answers as the nearest base that has one, and asking
// for it by name reaches dynamic_cast rather than a wrong answer.
struct ALViewType
{
    static constexpr U32 MAX_DEPTH = 16;

    constexpr ALViewType(const ALViewType* base, const char* name)
    :   mDepth(base ? base->mDepth + 1 : 0),
        mName(name),
        mAncestors{}
    {
        if (mDepth >= MAX_DEPTH)
        {
            throw "view hierarchy deeper than ALViewType::MAX_DEPTH";
        }
        for (U32 i = 0; i + 1 < mDepth; ++i)
        {
            mAncestors[i] = base->mAncestors[i];
        }
        if (base)
        {
            mAncestors[mDepth - 1] = base;
        }
    }

    // Whether a view of this type is one of the given type, or derives from it.
    constexpr bool isA(const ALViewType& type) const
    {
        return mDepth == type.mDepth ? this == &type
                                     : (mDepth > type.mDepth && mAncestors[type.mDepth] == &type);
    }

    // A view as a T, or null for a null view. Defined beside LLView, so a
    // header that names a view without seeing it can still ask.
    template <class T> static T* as(LLView* view);
    template <class T> static const T* as(const LLView* view);

    U32 mDepth;
    const char* mName;
    const ALViewType* mAncestors[MAX_DEPTH];
};

// Whether T declared its own type with AL_VIEW_TYPE. One that did not is
// reached by dynamic_cast, which walks the RTTI graph comparing names.
template <class T, class = void>
struct ALViewTypeOf
{
    static constexpr bool declared = false;
};

template <class T>
struct ALViewTypeOf<T, std::void_t<typename T::ALViewSelf>>
{
    static constexpr bool declared = std::is_same_v<typename T::ALViewSelf, T>;
};

// Declares a view class's type. Base is its immediate base on the LLView side
// of its inheritance; the class's ancestors are the base's, then itself.
#define AL_VIEW_TYPE(Class, Base)                                          \
    using ALViewSelf = Class;                                              \
    static constexpr ALViewType sViewType{&Base::sViewType, #Class};       \
    const ALViewType* viewType() const override { return &sViewType; }

#endif // AL_ALVIEWTYPE_H
