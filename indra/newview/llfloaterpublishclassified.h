/**
 * @file llfloaterpublishclassified.h
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


#ifndef LL_PUBLISHCLASSIFIEDFLOATER_H
#define LL_PUBLISHCLASSIFIEDFLOATER_H

#include "llfloater.h"

class LLFloaterPublishClassified final : public LLFloater
{
public:
    LLFloaterPublishClassified(const LLSD& key);
    ~LLFloaterPublishClassified() override = default;

    BOOL postBuild() override;

    void setPrice(S32 price);
    S32 getPrice();

    void setPublishClickedCallback(const commit_signal_t::slot_type& cb);
    void setCancelClickedCallback(const commit_signal_t::slot_type& cb);
};

#endif // LL_PUBLISHCLASSIFIEDFLOATER_H
