/**
 * @file llviewertexteditor.h
 * @brief Text editor widget to let users enter a multi-line document//
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

#ifndef LL_VIEWERTEXTEDITOR_H
#define LL_VIEWERTEXTEDITOR_H

#include "lltexteditor.h"

// The viewer's text editor, the widget every XUI text_editor is. A notecard
// and the items it carries are the notecard window's (LLPreviewNotecard,
// ALNotecardEmbedded), not this one's.
class LLViewerTextEditor final : public LLTextEditor
{
public:
    AL_VIEW_TYPE(LLViewerTextEditor, LLTextEditor);

    struct Params : public LLInitParam::Block<Params, LLTextEditor::Params>
    {};

protected:
    LLViewerTextEditor(const Params&);
    friend class LLUICtrlFactory;

public:
    virtual ~LLViewerTextEditor();

    // What is dragged onto the text is not taken, and goes no further --
    // but for an item out of a notecard, which is left to what is under it.
    virtual bool    handleDragAndDrop(S32 x, S32 y, MASK mask,
                                        bool drop, EDragAndDropType cargo_type,
                                        void *cargo_data, EAcceptance *accept, std::string& tooltip_msg) override;

    // Appends Second Life time, small font, grey.
    // If this starts a line, you need to prepend a newline.
    std::string appendTime(bool prepend_newline);
};

#endif  // LL_VIEWERTEXTEDITOR_H
