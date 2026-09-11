/**
 * @file alfloaterxuilibrary.h
 * @brief The tags a file may write, what each one is, and one built to look at.
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

#include "llfloater.h"

#include <string>
#include <vector>

class ALXUICatalog;
class LLFilterEditor;
class LLPanel;
class LLScrollListCtrl;
class LLTextEditor;

// A reference, consulted rather than lived in, which is why it is a window
// and not a region of one. On the left every tag a file may write, grouped by
// what it is; on the right what the schema knows about the one chosen, and
// one of it built so the word has something beside it.
class ALFloaterXUILibrary final : public LLFloater
{
    friend class LLFloaterReg;
public:
    AL_VIEW_TYPE(ALFloaterXUILibrary, LLFloater);

    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    ALFloaterXUILibrary(const LLSD& key);
    ~ALFloaterXUILibrary() override = default;

    void fillTags();
    void onTagSelected();
    void onFilter();

    // One of the tag, built with nothing but a name and a size, in the panel
    // beside its description. A picture of a widget is a drawing of one; this
    // is the widget, which is the whole reason the tool can afford to have a
    // library at all.
    void showSpecimen(const std::string& tag);

    // What the schema knows, as a document: the attributes with their types
    // and their enumerations, the parameter elements, what it may contain and
    // -- the question nothing in the tool could answer before -- what may
    // contain it. Then a line the developer can paste.
    std::string describe(const std::string& tag) const;

    // Which tags take this one as a child. The schema answers the other way
    // round for every tag it knows, so this asks all of them.
    std::vector<std::string> acceptedBy(const std::string& tag) const;

    // Containers, controls, text, lists, chrome: the five a developer looks
    // in, decided from what the tag does rather than from what it is called.
    // Answered as a key, which is what the list is ordered by; the heading
    // over each is the floater's string of that name.

    // The catalog the studio has already read, where the studio is open.
    // The library is its companion and does not scan the skin itself.
    static const ALXUICatalog* catalogOf();

    LLFilterEditor*     mFilter = nullptr;
    LLScrollListCtrl*   mTags = nullptr;
    LLTextEditor*       mAbout = nullptr;
    LLPanel*            mSpecimen = nullptr;
};
