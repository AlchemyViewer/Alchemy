/**
 * @file   llviewmodel.cpp
 * @author Nat Goodspeed
 * @date   2008-08-08
 * @brief  Implementation for llviewmodel.
 *
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
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

// Precompiled header
#include "linden_common.h"
// associated header
#include "llviewmodel.h"
// STL headers
// std headers
// external library headers
// other Linden headers

///
LLViewModel::LLViewModel()
:   mDirty(false)
{
}

/// Instantiate an LLViewModel with an existing data value
LLViewModel::LLViewModel(const LLSD& value)
:   mDirty(false)
{
    setValue(value);
}

/// Update the stored value
void LLViewModel::setValue(const LLSD& value)
{
    mValue = value;
    mDirty = true;
}

LLSD LLViewModel::getValue() const
{
    return mValue;
}

////////////////////////////////////////////////////////////////////////////

///
LLTextViewModel::LLTextViewModel()
  : LLViewModel(false),
    mUpdateFromDisplay(false)
{
}

/// Instantiate an LLViewModel with an existing data value
LLTextViewModel::LLTextViewModel(const LLSD& value)
  : LLViewModel(value),
    mUpdateFromDisplay(false)
{
}

/// Update the stored value
void LLTextViewModel::setValue(const LLSD& value)
{
    // approximate LLSD storage usage
    LLViewModel::setValue(value);
    mDisplay = value.asString();
    mDisplayGeneration++;

    // mDisplay and mValue agree
    mUpdateFromDisplay = false;
}

std::string& LLTextViewModel::getEditableDisplayUtf8()
{
    mDirty = true;
    mDisplayGeneration++;
    mUpdateFromDisplay = true;
    return mDisplay;
}

void LLTextViewModel::setDisplayUtf8(std::string_view value)
{
    // This is the strange way to alter the value. Normally we'd setValue().
    // But a text editor edits the string directly, and rebuilds the LLSD from
    // it on commit.
    mDisplay.assign(value);
    mDisplayGeneration++;
    mDirty = true;
    // Don't immediately rebuild the LLSD -- do it lazily -- we expect many
    // more setDisplayUtf8() calls than getValue() calls. Just flag that it
    // needs doing.
    mUpdateFromDisplay = true;
}

inline void updateFromDisplayIfNeeded(const LLTextViewModel* model)
{
    // Has anyone edited the display string since the last setValue()?
    // If so, the LLSD is behind it.
    if (model->mUpdateFromDisplay)
    {
        // The fact that we're lazily updating fields
        // in this object should be transparent to clients,
        // which is why this method is left conventionally const.
        // Nor do we particularly want to make these members mutable.
        // Just cast away constness in this one place.
        LLTextViewModel* nthis = const_cast<LLTextViewModel*>(model);
        nthis->mUpdateFromDisplay = false;
        nthis->mValue = nthis->mDisplay;
    }
}

LLSD LLTextViewModel::getValue() const
{
    updateFromDisplayIfNeeded(this);
    return mValue;
}

////////////////////////////////////////////////////////////////////////////

LLListViewModel::LLListViewModel(const LLSD& values)
  : LLViewModel()
{
}

void LLListViewModel::addColumn(const LLSD& column, EAddPosition pos)
{
}

void LLListViewModel::clearColumns()
{
}

void LLListViewModel::setColumnLabel(const std::string& column, const std::string& label)
{
}

LLScrollListItem* LLListViewModel::addElement(const LLSD& value, EAddPosition pos,
                                         void* userdata)
{
    return NULL;
}

LLScrollListItem* LLListViewModel::addSimpleElement(const std::string& value, EAddPosition pos,
                                               const LLSD& id)
{
    return NULL;
}

void LLListViewModel::clearRows()
{
}

void LLListViewModel::sortByColumn(const std::string& name, bool ascending)
{
}
