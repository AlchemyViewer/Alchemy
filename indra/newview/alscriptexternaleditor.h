/**
 * @file alscriptexternaleditor.h
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

#pragma once

#include "alscriptstudiodoc.h"
#include "alscripttypes.h"

#include <memory>
#include <string>

class ALScriptStudioServices;

// A Script Studio window's tabs held open in an editor outside the viewer
// -- Edit Externally -- as the tab's part `doc.external` keeps them: a
// script's copy written to the temp folder for the editor, watched for its
// saves, which are taken into the tab and saved from here; a save there over
// changes made here since held, and the author asked which to keep; the copy
// written again after a save from here, and the compiler's words put in a
// log beside it. A file on disk is edited where it is, watched as any file
// the studio has open is. The bridge VS Code talks to, and starting the
// editor, are the window's.
class ALScriptExternalEditor
{
public:
    typedef ALScriptStudioDoc Doc;

    // What the external editor asks of the window beyond its services.
    class Window
    {
    public:
        // A tab's file on disk watched for changes made to it outside.
        virtual void watchFile(Doc& doc) = 0;
        // The tab's carriedText taken as one step to undo; and a save of
        // the tab from here.
        virtual void takeCarriedText(Doc& doc) = 0;
        virtual void save(Doc& doc)            = 0;
        // The bridge: the id it knows a script by, which the copy's name
        // carries so that whoever reads the temp folder finds the same
        // file; the tab told of, so that VS Code can subscribe to it, false
        // where no bridge is there to tell; and let go of.
        virtual std::string bridgeId(const Doc& doc) const = 0;
        virtual bool        subscribe(Doc& doc)            = 0;
        virtual void        unsubscribe(const Doc& doc)    = 0;
        // A copy in the temp folder, or its log, held while the result
        // is: the old script window may hold the same copy, and it goes
        // with whichever lets go last (ALScriptTempFiles).
        virtual std::shared_ptr<ALScriptTempFiles::Claim> holdCopy(const std::string& path) = 0;
        // The editor outside started on the file at the caret's line: VS
        // Code where the bridge is set to it and the file is the studio's
        // copy, else the command the settings give; what went wrong said.
        virtual void startEditor(Doc& doc, const std::string& file, bool on_disk) = 0;

    protected:
        ~Window() = default;
    };

    ALScriptExternalEditor(ALScriptStudioServices& services, Window& window);

    // Edit Externally: the copy written afresh -- the editor may have been
    // closed on an old one -- and watched, the bridge told, and the editor
    // started. A file on disk is given as it is.
    void edit(Doc& doc);
    // The copy, or the file, changed outside: the tab's by its id. Taken,
    // or held and asked about where the tab has changed since the copy
    // was written; an emptied file looked at again a moment later, as a
    // save in two steps empties it for a moment -- `settled` is that look.
    void changed(const std::string& id, const std::string& file, bool settled = false);
    // A save from outside taken into the tab, as one step to undo, and
    // saved from here. By value: the text is often the one held, which
    // taking it lets go of.
    void take(Doc& doc, std::string text);
    // After a save from here: the copy written again where it holds
    // something else; and the compiler's words put in the log beside it.
    void sync(Doc& doc);
    void log(Doc& doc, const ALScriptCompileResult& result);
    // The tab let go of: the bridge told, the watch and the log gone.
    void stop(Doc& doc);
    // The copy's path, as the old editor names it (ALScriptTempFiles::
    // nameFor), so that the bridge's script.list and whoever reads the
    // temp folder find the same file.
    std::string fileName(const Doc& doc) const;

private:
    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    // Held while this is, for a watch or a timer to know it still is.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
};
