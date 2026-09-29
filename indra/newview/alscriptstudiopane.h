/**
 * @file alscriptstudiopane.h
 * @brief What each of Script Studio's panes does first as it is built: find its window.
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

#include "alscriptstudioservices.h"
#include "llfloater.h"

#include <string_view>

// What each of Script Studio's panes, bars and tabs does first as it is
// built: find the window it is part of through the view tree, as a pane
// is a panel inside the window and nothing hands it the window.
namespace ALScriptStudioPane
{
    // The window a pane is in: the services every pane asks of it, and
    // the pane's own interface to it. False, with a warning naming the
    // pane -- "The outline" -- where it is in no studio window.
    template <class Window>
    bool findWindow(LLView& pane, std::string_view what, ALScriptStudioServices*& services, Window*& window)
    {
        LLFloater* floater = pane.getParentByType<LLFloater>();
        services           = dynamic_cast<ALScriptStudioServices*>(floater);
        window             = dynamic_cast<Window*>(floater);
        if (!services || !window)
        {
            LL_WARNS() << what << " is not in a Script Studio window" << LL_ENDL;
            return false;
        }
        return true;
    }
}
