/**
 * @file alscriptexternaleditor.cpp
 * @brief Script Studio's scripts held open in an editor outside the viewer: the copy it is given, its saves taken.
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

#include "alscriptexternaleditor.h"

#include "alscriptstudiofileio.h"
#include "alscriptstudioservices.h"
#include "llcallbacklist.h"
#include "llfile.h"
#include "lllogchat.h"
#include "lltrans.h"

#include <algorithm>
#include <set>

using ALScriptFileIO::readWholeFile;
using ALScriptFileIO::StudioLiveFile;
using ALScriptFileIO::writeTempFile;

namespace
{
    bool writeWhole(const std::string& path, const std::string& text)
    {
        // An empty script is stored as one space, as it always was.
        return writeTempFile(path, text.empty() ? std::string_view(" ") : std::string_view(text));
    }
}

ALScriptExternalEditor::ALScriptExternalEditor(ALScriptStudioServices& services, Window& window)
    : mServices(services), mWindow(window)
{
}

std::string ALScriptExternalEditor::fileName(const Doc& doc) const
{
    // As the old editor named it, so that the bridge's script.list and
    // whoever reads the temp folder find the same file: the name
    // without what a file system refuses, the subscription id, and the
    // language's extension.
    static const std::set<char> forbidden{ '<', '>', ':', '"', '\\', '/', '|', '?', '*' };
    std::string                 name = doc.name;
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return forbidden.count(c) > 0; }), name.end());
    const std::string hash      = mWindow.bridgeId(doc);
    const std::string extension = doc.language.lua ? ".luau" : ".lsl";
    return std::string(LLFile::tmpdir()) + "sl_script_" + (name.empty() ? std::string() : name + "_") + hash + extension;
}

void ALScriptExternalEditor::edit(Doc& doc)
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
    const std::string filename = on_disk ? doc.file : fileName(doc);
    if (!on_disk && !writeWhole(filename, doc.editor->wholeText()))
    {
        args["[FILE]"] = filename;
        mServices.report(mServices.words("ExternalWriteFailed", args), true, &doc);
        return;
    }
    Doc::External& external = doc.external;
    external.written        = doc.editor->wholeText();
    external.waiting.reset();
    if (on_disk)
    {
        // Watched since it was opened; a save there comes in as any
        // outside change does.
        mWindow.watchFile(doc);
    }
    else if (!external.watch || external.watch->path() != filename)
    {
        // Watched from what was just written, which is no save of the
        // editor's: a tab with unsaved changes is not saved for being
        // opened outside.
        external.watch.reset();
        const std::weak_ptr<bool> alive = mAlive;
        const std::string         id    = doc.id;
        external.watch                  = std::make_unique<StudioLiveFile>(
            filename,
            [this, alive, id](const std::string& file) {
                if (alive.lock())
                {
                    changed(id, file);
                }
            },
            true);
    }
    else
    {
        external.watch->seen();
    }
    external.log = on_disk ? std::string() : filename + ".log";

    // The bridge, so that VS Code can subscribe to the script and hear
    // what the compiler says of it; a file on disk is nothing to it.
    if (!on_disk)
    {
        external.subscribed = mWindow.subscribe(doc);
    }
    mWindow.startEditor(doc, filename, on_disk);
}

void ALScriptExternalEditor::changed(const std::string& id, const std::string& file, bool settled)
{
    Doc* found = mServices.findDoc(id);
    if (!found)
    {
        return;
    }
    Doc& doc = *found;
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
    if (settled && text == doc.external.written)
    {
        // Emptied as the first of an editor's two steps, and the second
        // heard and taken since: nothing more to take, nor to save again.
        return;
    }
    if (text.empty() && !settled && !doc.editor->document().empty())
    {
        // Emptied -- or caught between an editor's two steps: taken only
        // if it is still empty a moment later, which a save in two steps
        // is not.
        constexpr F32             SETTLE = 1.5f;
        const std::weak_ptr<bool> alive  = mAlive;
        doAfterInterval(
            [this, alive, id, file]() {
                if (alive.lock())
                {
                    changed(id, file, true);
                }
            },
            SETTLE);
        return;
    }
    // Changed here since the copy was written, and changed there too: one
    // of them would be lost, so the author is asked which, rather than
    // what was typed here left a step back in the undo.
    const std::string& here = doc.editor->wholeText();
    if (text != here && here != doc.external.written && text != doc.external.written)
    {
        doc.external.waiting = text;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        mServices.report(mServices.words("ExternalConflict", args), true, &doc, { "take_external", "keep_here" });
        return;
    }
    take(doc, text);
}

void ALScriptExternalEditor::take(Doc& doc, const std::string& text)
{
    doc.external.waiting.reset();
    doc.external.written = text;
    if (text != doc.editor->wholeText())
    {
        // The editor's text, as one step to undo; then saved from here,
        // over whatever a check finds, since the editor outside is where
        // the author is looking.
        doc.carriedText = text;
        mWindow.takeCarriedText(doc);
    }
    if (!doc.editor->isDirty() && doc.assetId.notNull())
    {
        return;
    }
    doc.save.fromExternal(doc.editor->document().version());
    mWindow.save(doc);
}

void ALScriptExternalEditor::sync(Doc& doc)
{
    if (!doc.external.watch)
    {
        return;
    }
    const std::string filename = doc.external.watch->path();
    if (!gDirUtilp->fileExists(filename))
    {
        return;
    }
    // Only where it holds something else: the editor's own save is what
    // was sent, and a file written again under an editor that has it open
    // reads to that editor as changed.
    const std::string& text = doc.editor->wholeText();
    doc.external.written    = text;
    std::string        held;
    if (readWholeFile(filename, held) && (held == text || (text.empty() && held == " ")))
    {
        return;
    }
    writeWhole(filename, text);
    doc.external.watch->seen();
}

void ALScriptExternalEditor::log(Doc& doc, const ALScriptWorkspace::CompileResult& result)
{
    if (doc.external.log.empty())
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
    writeTempFile(doc.external.log, text);
}

void ALScriptExternalEditor::stop(Doc& doc)
{
    if (doc.external.subscribed)
    {
        mWindow.unsubscribe(doc);
        doc.external.subscribed = false;
    }
    doc.external.watch.reset();
    if (!doc.external.log.empty())
    {
        LLFile::remove(doc.external.log);
        doc.external.log.clear();
    }
    doc.save.endExternal();
}
