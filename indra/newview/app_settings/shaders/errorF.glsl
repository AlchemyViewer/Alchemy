<<<<<<<< HEAD:indra/newview/llfloaterpublishclassified.h
/**
 * @file llfloaterpublishclassified.h
 * @brief Publish classified floater
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
========
/** 
 * @file errorF.glsl
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
 * 
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/errorF.glsl
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

<<<<<<<< HEAD:indra/newview/llfloaterpublishclassified.h

#ifndef LL_PUBLISHCLASSIFIEDFLOATER_H
#define LL_PUBLISHCLASSIFIEDFLOATER_H

#include "llfloater.h"
========
 // error fallback on compilation failure

out vec4 frag_color;
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/errorF.glsl

class LLFloaterPublishClassified final : public LLFloater
{
<<<<<<<< HEAD:indra/newview/llfloaterpublishclassified.h
public:
    LLFloaterPublishClassified(const LLSD& key);
    ~LLFloaterPublishClassified() override = default;
========
    frag_color = vec4(1,0,1,1);
}
>>>>>>>> 2020201ba6890feb9a31168c40e1ed14727fa719:indra/newview/app_settings/shaders/errorF.glsl

    BOOL postBuild() override;

    void setPrice(S32 price);
    S32 getPrice();

    void setPublishClickedCallback(const commit_signal_t::slot_type& cb);
    void setCancelClickedCallback(const commit_signal_t::slot_type& cb);
};

#endif // LL_PUBLISHCLASSIFIEDFLOATER_H
