/**
 * @file alscriptstudiotabs.h
 * @brief What the units split out of the Script Studio's window ask of its tabs.
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

// What the units split out of the Script Studio's window ask of its tabs,
// whichever unit asks: a tab opened, brought forward, let go of or put
// back, and what shows the tabs said again. Finding a tab is the services'
// (ALScriptStudioServices). The window implements it; a unit's test fakes
// it.
class ALScriptStudioTabs
{
public:
    typedef ALScriptStudioDoc Doc;

    // A file on disk opened in a tab where it is, read as the language its
    // extension says, else `lua`; null where it could not be.
    virtual Doc* openFileTab(const std::string& path, bool lua) = 0;
    // A tab brought to the front.
    virtual void activate(Doc& doc) = 0;
    // A tab let go of as it stands, nothing asked: saved to be closed, the
    // tab a copy was made of, a preview walked past.
    virtual void letGoOf(Doc& doc) = 0;
    // A tab's text put back as it was last saved or loaded, what it held
    // set aside first, nothing asked.
    virtual void revert(Doc& doc) = 0;
    // What a tab carries (Doc::carriedText) put in over its text, as one
    // step to undo.
    virtual void takeCarriedText(Doc& doc) = 0;

    // Said again: the tabs' strip, the toolbar, the notice over the editor,
    // and the strip under a tab's editor.
    virtual void fillTabs()               = 0;
    virtual void refreshToolbar()         = 0;
    virtual void refreshNotice()          = 0;
    virtual void refreshTrailer(Doc& doc) = 0;

protected:
    ~ALScriptStudioTabs() = default;
};
