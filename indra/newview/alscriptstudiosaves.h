/**
 * @file alscriptstudiosaves.h
 * @brief What the units split out of the Script Studio's window ask of its saving.
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

#include <string>

struct ALScriptStudioDoc;

// What the units split out of the Script Studio's window ask of its
// saving, whichever unit asks: a tab saved, as asked or to be closed; a
// save that stopped; the preprocessor run for one; and a tab's weight
// heard. ALScriptStudioSaving implements it, and is given to the units
// that ask it; a unit's test fakes it.
class ALScriptStudioSaves
{
public:
    typedef ALScriptStudioDoc Doc;

    // A tab saved as its flow says; and as the author asked, past the one
    // check that stopped the last save of the same text.
    virtual void save(Doc& doc)      = 0;
    virtual void saveAsked(Doc& doc) = 0;
    // A tab saved to be closed once its save comes back, by its id.
    virtual void saveToClose(const std::string& id) = 0;
    // A save that did not go through: a close waiting on it waits no
    // longer.
    virtual void stopped(Doc& doc) = 0;
    // A run of the preprocessor over the text as it stands, which a save
    // waiting on it goes on from.
    virtual void preprocess(Doc& doc) = 0;
    // A tab's weight known: over its target's limit, as a save sent it,
    // said once.
    virtual void warnOverWeight(Doc& doc) = 0;

protected:
    ~ALScriptStudioSaves() = default;
};
