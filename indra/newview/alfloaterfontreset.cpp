/**
 * @file alfloaterfontreset.cpp
 * @brief Confirmation dialog for the "reset fonts to defaults" recovery action.
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

#include "llviewerprecompiledheaders.h"

#include "alfloaterfontreset.h"

#include "alimportedfonts.h"
#include "llbutton.h"
#include "llfloaterpreference.h"
#include "llfloaterreg.h"

ALFloaterFontReset::ALFloaterFontReset(const LLSD& key)
:   LLFloater(key)
{
}

bool ALFloaterFontReset::postBuild()
{
    childSetAction("reset_btn", boost::bind(&ALFloaterFontReset::onReset, this));
    childSetAction("cancel_btn", boost::bind(&ALFloaterFontReset::onCancel, this));
    return true;
}

void ALFloaterFontReset::onOpen(const LLSD& key)
{
    // Focus Cancel so Enter can't clear fonts by accident.
    if (LLUICtrl* cancel = getChild<LLUICtrl>("cancel_btn"))
    {
        cancel->setFocus(true);
    }
}

void ALFloaterFontReset::onReset()
{
    ALImportedFonts::resetOverrides();
    LLFloaterPreference::scheduleFontDropdownRefresh();
    closeFloater();
}

void ALFloaterFontReset::onCancel()
{
    closeFloater();
}

// static
void ALFloaterFontReset::showConfirm()
{
    LLFloaterReg::showInstance("font_reset");
}
