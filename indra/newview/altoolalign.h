/**
 * @file altoolalign.h
 * @brief A tool to align objects
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#ifndef AL_ALTOOLALIGN_H
#define AL_ALTOOLALIGN_H

#include "llsingleton.h"
#include "lltool.h"
#include "llbbox.h"

class LLViewerObject;
class LLPickInfo;
class LLToolSelectRect;

class ALToolAlign final
:   public LLTool, public LLSingleton<ALToolAlign>
{
    LLSINGLETON(ALToolAlign);
    ~ALToolAlign() = default;

public:
    void    handleSelect() override;
    void    handleDeselect() override;
    bool    handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool    handleHover(S32 x, S32 y, MASK mask) override;
    void    render() override;
    bool    canAffectSelection();

    static void pickCallback(const LLPickInfo& pick_info);

private:
    void            align();
    void            computeManipulatorSize();
    void            renderManipulators();
    bool            findSelectedManipulator(S32 x, S32 y);

    LLBBox          mBBox;
    F32             mManipulatorSize;
    S32             mHighlightedAxis;
    F32             mHighlightedDirection;
    bool            mForce;
};

#endif // AL_ALTOOLALIGN_H
