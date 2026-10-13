/**
 * @file allooknotecard.h
 * @brief A Lightbox Look given as a notecard: one made in inventory from the settings, and one read from inventory into the Looks.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "llpresetsmanager.h"
#include "llsd.h"
#include "lluuid.h"

#include <functional>
#include <string>

// The inventory half of sharing a Look. ALLookFormat says what the text is;
// this makes the notecard that holds it and reads one back, through the
// same workspace every notecard editor saves and loads with
// (ALScriptWorkspace), and puts what it read into the Looks through
// LLPresetsManager. Nothing here applies a Look: the Lightbox does that,
// as one undo step, once it has been told what arrived.
namespace ALLookNotecard
{
    // A Look's settings, as a Look file holds them, written into a new
    // notecard in the Notecards folder, called after the Look. Says how it
    // went in a notification either way, and shows the notecard in the
    // inventory once its text is up. Nothing is made where the text would
    // not fit or could not be uploaded, so no empty notecard is left.
    void shareLookAsNotecard(const std::string& look_name, const LLSD& look_file_params);

    // What came of reading a notecard as a Look.
    struct ImportedLook
    {
        enum class Failure : U8
        {
            None,
            // The notecard could not be read: no copy permission, RLVa,
            // the fetch failing. `reason` says which, in words.
            Unreadable,
            // Read, but it does not hold a Look.
            NotALook,
            // A Look, but it could not be written into the Looks folder.
            NotSaved,
            // A Look, but with nothing in it this viewer can use: every
            // setting unknown here or refused. `counts` says how many.
            NothingUsable
        };
        Failure     failure = Failure::None;
        std::string reason;

        // The name the Look was saved under, and the name the notecard
        // asked for.
        std::string lookName;
        std::string wantedName;
        // Why those differ, where they do: a different Look (or a file)
        // already had the name; the name is Neutral's, or the default
        // preset's, which no Look from outside may take; or the name was
        // cut to fit the path a Look's file is written at.
        enum class Renamed : U8
        {
            No,
            Taken,
            Reserved,
            Shortened
        };
        Renamed renamed = Renamed::No;
        // An identical Look already stood under that name: nothing was
        // written, and that one is the Look to apply.
        bool alreadyHad = false;
        // A colour LUT file the Look names that is not installed here.
        std::string missingLut;
        // What was taken, skipped as malformed, and left out as unknown.
        LLPresetsManager::LookSettingCounts counts;
    };
    typedef std::function<void(const ImportedLook& imported)> imported_callback_t;

    // A notecard's text taken in as a Look: parsed, each value judged, and
    // saved to the Looks folder under its own name -- never over a
    // different Look; a name in use gets " (2)", and an identical Look
    // under the name is simply found. `done` is called once, before this
    // returns. `notecard_name` names the Look where its text does not.
    void importLookFromNotecardText(const std::string& text, const std::string& notecard_name, imported_callback_t done);

    // A notecard in the agent's inventory taken in the same way, once its
    // text has been fetched. `done` is called once, later, on the main
    // thread -- or at once where the item is not a notecard that can be
    // read.
    void importLookFromInventoryNotecard(const LLUUID& item_id, imported_callback_t done);

    // Whether a colour LUT of that file name is where the renderer would
    // look for it: the user's colorlut folder, then the viewer's.
    bool colorLutFileIsInstalled(const std::string& lut_name);
}
