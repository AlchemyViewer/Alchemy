/**
 * @file alviewkind.h
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

#ifndef AL_ALVIEWKIND_H
#define AL_ALVIEWKIND_H

#include "stdtypes.h"

class LLView;
class LLUICtrl;
class LLPanel;
class LLFloater;
class LLFloaterView;
class LLLayoutStack;
class LLLayoutPanel;
class LLMenuGL;
class LLMenuBarGL;
class LLContextMenu;
class LLMenuItemGL;
class LLMenuItemSeparatorGL;
class LLFolderView;

// The kinds LLView::as<T>() answers with a mask test and a static_cast.
// A class that is one of these ORs its bit into kindMask() on top of its
// immediate base's, so a derived class cannot lose a bit a base claimed, and
// a bit names one class only.
struct ALViewKind
{
    enum : U32
    {
        CTRL         = 1u << 0,
        PANEL        = 1u << 1,
        FLOATER      = 1u << 2,
        FLOATER_VIEW = 1u << 3,
        LAYOUT_STACK = 1u << 4,
        LAYOUT_PANEL = 1u << 5,
        MENU         = 1u << 6,
        MENU_BAR     = 1u << 7,
        CONTEXT_MENU = 1u << 8,
        MENU_ITEM    = 1u << 9,
        MENU_SEPARATOR = 1u << 10,
        FOLDER_VIEW  = 1u << 11,
    };

    // A view as a T, or null for a null view. Defined beside LLView, so a
    // header that names a view without seeing it can still ask.
    template <class T> static T* as(LLView* view);
};

// The bit for a T, or zero. A T with no entry is reached by dynamic_cast,
// which walks the RTTI graph comparing names; an entry added here speeds up
// every as<T>() already written for that T.
template <class T> struct ALViewKindOf { static constexpr U32 bits = 0; };

template <> struct ALViewKindOf<LLUICtrl>     { static constexpr U32 bits = ALViewKind::CTRL; };
template <> struct ALViewKindOf<LLPanel>      { static constexpr U32 bits = ALViewKind::PANEL; };
template <> struct ALViewKindOf<LLFloater>    { static constexpr U32 bits = ALViewKind::FLOATER; };
template <> struct ALViewKindOf<LLFloaterView> { static constexpr U32 bits = ALViewKind::FLOATER_VIEW; };
template <> struct ALViewKindOf<LLLayoutStack> { static constexpr U32 bits = ALViewKind::LAYOUT_STACK; };
template <> struct ALViewKindOf<LLLayoutPanel> { static constexpr U32 bits = ALViewKind::LAYOUT_PANEL; };
template <> struct ALViewKindOf<LLMenuGL>      { static constexpr U32 bits = ALViewKind::MENU; };
template <> struct ALViewKindOf<LLMenuBarGL>   { static constexpr U32 bits = ALViewKind::MENU_BAR; };
template <> struct ALViewKindOf<LLContextMenu> { static constexpr U32 bits = ALViewKind::CONTEXT_MENU; };
template <> struct ALViewKindOf<LLMenuItemGL>  { static constexpr U32 bits = ALViewKind::MENU_ITEM; };
template <> struct ALViewKindOf<LLMenuItemSeparatorGL> { static constexpr U32 bits = ALViewKind::MENU_SEPARATOR; };
template <> struct ALViewKindOf<LLFolderView>  { static constexpr U32 bits = ALViewKind::FOLDER_VIEW; };

#endif // AL_ALVIEWKIND_H
