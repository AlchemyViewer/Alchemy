/**
 * @file llviewertexteditor.cpp
 * @brief Text editor widget to let users enter a multi-line document.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "llviewertexteditor.h"

#include "lltooldraganddrop.h"
#include "lltrans.h"
#include "lluictrlfactory.h"

static LLDefaultChildRegistry::Register<LLViewerTextEditor> r("text_editor");

LLViewerTextEditor::LLViewerTextEditor(const LLViewerTextEditor::Params& p) : LLTextEditor(p)
{
}

LLViewerTextEditor::~LLViewerTextEditor() = default;

// virtual
bool LLViewerTextEditor::handleDragAndDrop(S32 x, S32 y, MASK mask,
                      bool drop, EDragAndDropType cargo_type, void *cargo_data,
                      EAcceptance *accept,
                      std::string& tooltip_msg)
{
    // Out of a notecard: left to whatever is under the text.
    if (LLToolDragAndDrop::getInstance()->getSource() == LLToolDragAndDrop::SOURCE_NOTECARD)
    {
        return false;
    }
    *accept = ACCEPT_NO;
    LL_DEBUGS("UserInput") << "dragAndDrop handled by LLViewerTextEditor " << getName() << LL_ENDL;
    return true;
}

std::string LLViewerTextEditor::appendTime(bool prepend_newline)
{
    time_t utc_time;
    utc_time = time_corrected();
    std::string timeStr ="[["+ LLTrans::getString("TimeHour")+"]:["
        +LLTrans::getString("TimeMin")+"]] ";

    LLSD substitution;

    substitution["datetime"] = (S32) utc_time;
    LLStringUtil::format (timeStr, substitution);
    appendText(timeStr, prepend_newline, LLStyle::Params().color(LLColor4::grey));
    blockUndo();

    return timeStr;
}
