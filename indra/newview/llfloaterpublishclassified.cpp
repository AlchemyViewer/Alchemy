/**
 * @file llfloaterpublishclassified.cpp
 * @brief Publish classified floater
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
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

#include "llviewerprecompiledheaders.h"
#include "llfloaterpublishclassified.h"

LLFloaterPublishClassified::LLFloaterPublishClassified(const LLSD& key)
    : LLFloater(key)
{
}

BOOL LLFloaterPublishClassified::postBuild()
{
    LLFloater::postBuild();

    childSetAction("publish_btn", boost::bind(&LLFloater::closeFloater, this, false));
    childSetAction("cancel_btn", boost::bind(&LLFloater::closeFloater, this, false));

    return TRUE;
}

void LLFloaterPublishClassified::setPrice(S32 price)
{
    getChild<LLUICtrl>("price_for_listing")->setValue(price);
}

S32 LLFloaterPublishClassified::getPrice()
{
    return getChild<LLUICtrl>("price_for_listing")->getValue().asInteger();
}

void LLFloaterPublishClassified::setPublishClickedCallback(const commit_signal_t::slot_type& cb)
{
    getChild<LLButton>("publish_btn")->setClickedCallback(cb);
}

void LLFloaterPublishClassified::setCancelClickedCallback(const commit_signal_t::slot_type& cb)
{
    getChild<LLButton>("cancel_btn")->setClickedCallback(cb);
}
