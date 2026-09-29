/**
 * @file alscriptstudioglue.cpp
 * @brief What Script Studio asks of the system through the viewer: an editor of the person's own, and the file pickers.
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

#include "alscriptstudioglue.h"

#include "llexternaleditor.h"
#include "llviewermenufile.h"
#include "lltrans.h"

namespace ALScriptStudioGlue
{
    bool startEditor(const std::string& filename, S32 line, std::string& error)
    {
        LLExternalEditor             editor;
        LLExternalEditor::EErrorCode status = editor.setCommand("LL_SCRIPT_EDITOR");
        if (status != LLExternalEditor::EC_SUCCESS)
        {
            error = status == LLExternalEditor::EC_NOT_SPECIFIED ? LLTrans::getString("ExternalEditorNotSet") : LLExternalEditor::getErrorMessage(status);
            return false;
        }
        status = editor.run(filename, line);
        if (status != LLExternalEditor::EC_SUCCESS)
        {
            error = LLExternalEditor::getErrorMessage(status);
            return false;
        }
        return true;
    }

    void pickToOpen(bool several, chosen_t chosen)
    {
        LLFilePickerReplyThread::startPicker(
            [chosen = std::move(chosen)](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) { chosen(files); },
            LLFilePicker::FFLOAD_SCRIPT, several);
    }

    void pickToSave(const std::string& name, chosen_t chosen)
    {
        LLFilePickerReplyThread::startPicker(
            [chosen = std::move(chosen)](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) { chosen(files); },
            LLFilePicker::FFSAVE_SCRIPT, name);
    }
}
