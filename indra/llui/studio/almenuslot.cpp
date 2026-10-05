/**
 * @file almenuslot.cpp
 * @brief A context menu a view shows, and lets go of when it shows another or goes itself.
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

#include "almenuslot.h"

#include "llmenugl.h"
#include "lluictrlfactory.h"

LLContextMenu* ALMenuSlot::make(const std::string& file)
{
    close();
    if (!LLMenuGL::sMenuContainer)
    {
        return nullptr;
    }
    LLContextMenu* menu =
        LLUICtrlFactory::createFromFile<LLContextMenu>(file, LLMenuGL::sMenuContainer, LLMenuHolderGL::child_registry_t::instance());
    if (menu)
    {
        mMenu = menu->getHandle();
    }
    return menu;
}

void ALMenuSlot::show(LLView* over, S32 x, S32 y)
{
    if (LLContextMenu* menu = mMenu.get())
    {
        // A context menu places itself; the popup puts it in front and
        // takes the mouse. Both, in that order.
        menu->show(x, y);
        LLMenuGL::showPopup(over, menu, x, y);
    }
}

void ALMenuSlot::close()
{
    if (LLContextMenu* menu = mMenu.get())
    {
        menu->hide();
        menu->die();
    }
    mMenu.markDead();
}

LLContextMenu* ALMenuSlot::get() const
{
    return mMenu.get();
}
