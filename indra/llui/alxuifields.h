/**
 * @file alxuifields.h
 * @brief A XUI attribute as the attribute grid shows it.
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

#include "alpropertygrid.h"
#include "stdtypes.h"

#include <string_view>

// A XUI attribute as the attribute grid shows it: the section it sits
// under, and, for the few whose values the viewer holds rather than the
// schema, the words it may take.
namespace ALXUIFields
{
    // The sections the grid is divided into, numbered as the grid numbers
    // its groups, in the order an author reads them: what the thing is,
    // where it is, what it looks like, what it does, and then everything a
    // tag will take that none of those cover.
    enum Section : S32
    {
        IDENTITY,
        GEOMETRY,
        APPEARANCE,
        BEHAVIOUR,
        OTHER,
        // Two sections that are not subjects but verdicts: what a file may
        // write and the viewer throws away, and what a file writes that
        // nothing declares. Both are worth an author's eye and neither is
        // worth being mixed in with the fields that work.
        IGNORED,
        UNKNOWN
    };

    // Which section a name belongs under: where the notes say, what they
    // say; else a guess from the name. A nested leaf goes where its block
    // goes: bg_alpha_color.alpha is a colour and rect.left a position.
    S32 sectionOf(std::string_view name, std::string_view tag = std::string_view());

    // The words a field may take, for the few whose values are a list the
    // viewer holds rather than an enumeration the schema can read: a font
    // and its size, named in fonts.xml; its style, flags written with bars
    // between them; the corner a layout counts from; and the edges an
    // element follows. A field that has its values already is left as it
    // is.
    void vocabularyFor(ALPropertyGrid::Field& field);
}
