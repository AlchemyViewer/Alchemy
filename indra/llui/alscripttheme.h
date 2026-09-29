/**
 * @file alscripttheme.h
 * @brief The colours Script Studio's editors are painted with, as themes: files of colours applied to the table as one.
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

#include "alsurface.h"
#include "v4color.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// A theme: a name and a colour for each of the names the studio's
// editors are painted by -- the text, the ground, the cursor, the
// selection and the gutter, and every token kind under the Script
// prefix. The ones shipped live under app_settings/script_themes/, a
// person's own under the settings folder's script_themes/. Applying one
// sets each colour in the table, which every editor reads live, and
// the setting remembers which; a colour changed by hand afterwards
// makes the choice "custom" again.
class ALScriptTheme
{
public:
    std::string                     name;
    std::string                     source;
    std::string                     path;
    bool                            dark = false;
    bool                            own  = false;
    std::map<std::string, LLColor4> colors;

    // Every colour name a theme speaks for, in the order a list shows
    // them: the editor's own first, then the kinds.
    static const std::vector<std::string>& names();
    // What a person reads a name as: "Background", "Comment".
    static std::string labelOf(const std::string& name);
    // Whether the name is one of the editor's own rather than a kind's.
    static bool isEditorColor(const std::string& name);

    // A colour that reads less than it should against what it is drawn
    // on: its name, the name of that, and how far apart the two read.
    struct Illegible
    {
        std::string name;
        std::string against;
        F32         contrast = 0.f;
    };
    // Every such colour among those `colour` gives by name -- nothing for
    // one it does not have: the text and each kind of code against the
    // ground and the read-only ground; the text against the caret's line
    // and the selection, each drawn over the ground; the line numbers
    // against the gutter. Text and code are held to `least`, the line
    // numbers, which are not read as the code is, to `numbers`.
    typedef std::function<std::optional<LLColor4>(const std::string&)> lookup_t;
    static std::vector<Illegible> illegible(const lookup_t& colour, F32 least = ALSurface::LEGIBLE, F32 numbers = 3.f);
    std::vector<Illegible>        illegible(F32 least = ALSurface::LEGIBLE, F32 numbers = 3.f) const;

    // The themes on disk, shipped then own, each by name.
    static std::vector<ALScriptTheme> available();
    static bool                       load(const std::string& path, ALScriptTheme& theme);
    // The colours as they stand, under a name.
    static ALScriptTheme capture(const std::string& name);
    // Written as a person's own, under the settings folder -- over the own
    // theme of the same name where there is one, beside every other -- and
    // the path it landed at.
    bool save(std::string& path_out) const;

    // Into the table, every colour the theme has and the skin's own for
    // any it lacks, and remembered as the choice.
    void        apply() const;
    // The skin's colours again, and no theme chosen.
    static void restoreSkin();
    // The theme chosen, or empty for custom or none.
    static std::string chosen();
    static void        setChosen(const std::string& name);
};
