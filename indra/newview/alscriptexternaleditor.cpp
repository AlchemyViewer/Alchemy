/**
 * @file alscriptexternaleditor.cpp
 * @brief Script Studio's scripts held open in an editor outside the viewer: the copy it is given, its saves taken, the compiler's words beside it.
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

#include "alfloaterscriptstudio.h"

#include "alscriptstudiofileio.h"
#include "llcallbacklist.h"
#include "llexternaleditor.h"
#include "lllogchat.h"
#include "llnotificationsutil.h"
#include "llscripteditorws.h"
#include "lltrans.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"

#include <algorithm>
#include <set>

using ALScriptFileIO::readWholeFile;
using ALScriptFileIO::StudioLiveFile;
using ALScriptFileIO::writeTempFile;

// --- an editor outside ----------------------------------------------------------------

namespace
{
    bool writeWhole(const std::string& path, const std::string& text)
    {
        // An empty script is stored as one space, as it always was.
        return writeTempFile(path, text.empty() ? std::string_view(" ") : std::string_view(text));
    }
}

// static
std::string ALFloaterScriptStudio::externalFileName(const Doc& doc)
{
    // As the old editor named it, so that the bridge's script.list and
    // whoever reads the temp folder find the same file: the name
    // without what a file system refuses, the subscription id, and the
    // language's extension.
    static const std::set<char> forbidden{ '<', '>', ':', '"', '\\', '/', '|', '?', '*' };
    std::string                 name = doc.name;
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return forbidden.count(c) > 0; }), name.end());
    const std::string hash      = LLScriptEditorWSServer::buildScriptSubscriptionId(doc.ref.object, doc.ref.item);
    const std::string extension = doc.language.lua ? ".luau" : ".lsl";
    return std::string(LLFile::tmpdir()) + "sl_script_" + (name.empty() ? std::string() : name + "_") + hash + extension;
}

void ALFloaterScriptStudio::editExternally(Doc& doc)
{
    if (!doc.loaded || !doc.modifiable || doc.notecard)
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    // The file, written afresh -- the editor may have been closed on an
    // old one -- and watched. A file on disk is edited where it is.
    const bool        on_disk  = !doc.file.empty();
    const std::string filename = on_disk ? doc.file : externalFileName(doc);
    if (!on_disk && !writeWhole(filename, doc.editor->text()))
    {
        args["[FILE]"] = filename;
        report(getString("ExternalWriteFailed", args), true, &doc);
        return;
    }
    doc.externalWritten = doc.editor->text();
    doc.externalWaiting.reset();
    if (on_disk)
    {
        // Watched since it was opened; a save there comes in as any
        // outside change does.
        watchFile(doc);
    }
    else if (!doc.liveFile || doc.liveFile->path() != filename)
    {
        // Watched from what was just written, which is no save of the
        // editor's: a tab with unsaved changes is not saved for being
        // opened outside.
        doc.liveFile.reset();
        const LLHandle<LLFloater> handle = getHandle();
        const std::string         id     = doc.id;
        doc.liveFile                     = std::make_unique<StudioLiveFile>(
            filename,
            [handle, id](const std::string& file) {
                if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                {
                    studio->externalChanged(id, file);
                }
            },
            true);
    }
    else
    {
        doc.liveFile->seen();
    }
    doc.liveLog = on_disk ? std::string() : filename + ".log";

    // The bridge, so that VS Code can subscribe to the script and hear
    // what the compiler says of it; a file on disk is nothing to it.
    const bool                       tight  = !on_disk && LLScriptEditorWSServer::isTightIntegration();
    LLScriptEditorWSServer::ptr_t    server = !on_disk && LLScriptEditorWSServer::isEnabled() ? LLScriptEditorWSServer::ensureServerRunning() : nullptr;
    if (server)
    {
        const std::string script_id = LLScriptEditorWSServer::buildScriptSubscriptionId(doc.ref.object, doc.ref.item);
        doc.subscribed = server->subscribeScriptEditor(doc.ref.object, doc.ref.item, doc.name, getHandle(), script_id, doc.language.lua);
    }
    if (tight)
    {
        if (!server)
        {
            LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", LLTrans::getString("ExternalEditorFailedToStart")));
            return;
        }
        LLUUID root_id;
        if (LLViewerObject* object = doc.ref.inInventory() ? nullptr : gObjectList.findObject(doc.ref.object))
        {
            root_id = object->getRootEdit() ? object->getRootEdit()->getID() : object->getID();
        }
        if (!LLScriptEditorWSServer::launchVSCode(root_id, doc.ref.item))
        {
            LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", LLTrans::getString("VSCodeLaunchFailed")));
            return;
        }
        report(getString("ExternalOpenedVSCode", args), false, &doc);
        return;
    }
    LLExternalEditor             editor;
    LLExternalEditor::EErrorCode status = editor.setCommand("LL_SCRIPT_EDITOR");
    if (status != LLExternalEditor::EC_SUCCESS)
    {
        const std::string message = status == LLExternalEditor::EC_NOT_SPECIFIED ? LLTrans::getString("ExternalEditorNotSet")
                                                                                   : LLExternalEditor::getErrorMessage(status);
        LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", message));
        return;
    }
    status = editor.run(filename, doc.editor->caret().line + 1);
    if (status != LLExternalEditor::EC_SUCCESS)
    {
        LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", LLExternalEditor::getErrorMessage(status)));
        return;
    }
    report(getString("ExternalOpened", args), false, &doc);
}

void ALFloaterScriptStudio::externalChanged(const std::string& id, const std::string& file, bool settled)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    if (!doc.loaded || !doc.modifiable)
    {
        return;
    }
    std::string text;
    if (!readWholeFile(file, text))
    {
        // Gone: an editor saving a file in two steps leaves it so for a
        // moment, and says so again once it is back.
        return;
    }
    // An empty script is written out as one space, and so reads back.
    if (text == " ")
    {
        text.clear();
    }
    if (settled && text == doc.externalWritten)
    {
        // Emptied as the first of an editor's two steps, and the second
        // heard and taken since: nothing more to take, nor to save again.
        return;
    }
    if (text.empty() && !settled && !doc.editor->text().empty())
    {
        // Emptied -- or caught between an editor's two steps: taken only
        // if it is still empty a moment later, which a save in two steps
        // is not.
        constexpr F32             SETTLE = 1.5f;
        const LLHandle<LLFloater> handle = getHandle();
        doAfterInterval(
            [handle, id, file]() {
                if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                {
                    studio->externalChanged(id, file, true);
                }
            },
            SETTLE);
        return;
    }
    // Changed here since the copy was written, and changed there too: one
    // of them would be lost, so the author is asked which, rather than
    // what was typed here left a step back in the undo.
    if (text != doc.editor->text() && doc.editor->text() != doc.externalWritten && text != doc.externalWritten)
    {
        doc.externalWaiting = text;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        report(getString("ExternalConflict", args), true, &doc, { "take_external", "keep_here" });
        return;
    }
    takeExternal(doc, text);
}

void ALFloaterScriptStudio::takeExternal(Doc& doc, const std::string& text)
{
    doc.externalWaiting.reset();
    doc.externalWritten = text;
    if (text != doc.editor->text())
    {
        // The editor's text, as one step to undo; then saved from here,
        // over whatever a check finds, since the editor outside is where
        // the author is looking.
        doc.carriedText = text;
        takeCarriedText(doc);
    }
    if (!doc.editor->isDirty() && doc.assetId.notNull())
    {
        return;
    }
    doc.save.fromExternal(doc.editor->document().version());
    mSaving.save(doc);
}

void ALFloaterScriptStudio::syncExternal(Doc& doc)
{
    if (!doc.liveFile)
    {
        return;
    }
    const std::string filename = doc.liveFile->path();
    if (!gDirUtilp->fileExists(filename))
    {
        return;
    }
    // Only where it holds something else: the editor's own save is what
    // was sent, and a file written again under an editor that has it open
    // reads to that editor as changed.
    const std::string text = doc.editor->text();
    doc.externalWritten    = text;
    std::string       held;
    if (readWholeFile(filename, held) && (held == text || (text.empty() && held == " ")))
    {
        return;
    }
    writeWhole(filename, text);
    doc.liveFile->seen();
}

void ALFloaterScriptStudio::logExternal(Doc& doc, const ALScriptWorkspace::CompileResult& result)
{
    if (doc.liveLog.empty())
    {
        return;
    }
    // Beside the copy, and made as it is (writeTempFile): never through a
    // link somebody else left at its name.
    std::string text = "// " + LLLogChat::timestamp2LogString(0, true) + "\n\n";
    if (result.success)
    {
        text += LLTrans::getString("CompileSuccessful") + "\n" + LLTrans::getString("SaveComplete") + "\n";
    }
    for (const std::string& message : result.messages)
    {
        std::string line = message;
        LLStringUtil::stripNonprintable(line);
        text += line + "\n";
    }
    writeTempFile(doc.liveLog, text);
}

void ALFloaterScriptStudio::stopExternal(Doc& doc)
{
    if (doc.subscribed)
    {
        if (LLScriptEditorWSServer::ptr_t server = LLScriptEditorWSServer::getServer())
        {
            const std::string script_id = LLScriptEditorWSServer::buildScriptSubscriptionId(doc.ref.object, doc.ref.item);
            server->sendUnsubscribeScriptEditor(script_id);
            server->unsubscribeEditor(script_id);
        }
        doc.subscribed = false;
    }
    doc.liveFile.reset();
    if (!doc.liveLog.empty())
    {
        LLFile::remove(doc.liveLog);
        doc.liveLog.clear();
    }
    doc.save.endExternal();
}
