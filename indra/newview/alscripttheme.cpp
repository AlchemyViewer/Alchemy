/**
 * @file alscripttheme.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alscripttheme.h"

#include "altextview.h"
#include "lldir.h"
#include "lldiriterator.h"
#include "llsdserialize.h"
#include "lluicolortable.h"
#include "llviewercontrol.h"

#include <algorithm>
#include <fstream>

namespace
{
    const char* const SETTING = "ALScriptStudioTheme";
    const char* const FOLDER  = "script_themes";

    // The editor's own colours, with what a person reads them as.
    const std::pair<const char*, const char*> EDITOR_COLORS[] = {
        { "ScriptText", "Text" },
        { "ScriptBackground", "Background" },
        { "ScriptBgReadOnlyColor", "Background, read-only" },
        { "ScriptCursorColor", "Cursor" },
        { "ScriptSelectionColor", "Selection" },
        { "ScriptCurrentLineColor", "Current line" },
        { "ScriptGutterColor", "Gutter" },
        { "ScriptLineNumberColor", "Line numbers" },
        { "ScriptBracketMatchColor", "Matched bracket" },
        { "ScriptFindMatchColor", "Find match" },
        { "ScriptFoldColor", "Fold marks" },
        { "ScriptHighlightColor", "References" },
    };

    // A kind's name split for reading: "DocComment" as "Doc comment".
    std::string spaced(const std::string& camel)
    {
        std::string out;
        for (size_t i = 0; i < camel.size(); ++i)
        {
            const char c = camel[i];
            if (i > 0 && isupper(static_cast<unsigned char>(c)))
            {
                out += ' ';
                out += static_cast<char>(tolower(static_cast<unsigned char>(c)));
            }
            else
            {
                out += c;
            }
        }
        return out;
    }

    std::string slugOf(const std::string& name)
    {
        std::string slug;
        for (char c : name)
        {
            if (isalnum(static_cast<unsigned char>(c)))
            {
                slug += static_cast<char>(tolower(static_cast<unsigned char>(c)));
            }
            else if (!slug.empty() && slug.back() != '_')
            {
                slug += '_';
            }
        }
        while (!slug.empty() && slug.back() == '_')
        {
            slug.pop_back();
        }
        return slug.empty() ? std::string("theme") : slug;
    }

    void themesIn(const std::string& folder, bool own, std::vector<ALScriptTheme>& out)
    {
        LLDirIterator files(folder, "*.xml");
        std::string   file;
        while (files.next(file))
        {
            ALScriptTheme theme;
            if (ALScriptTheme::load(gDirUtilp->add(folder, file), theme))
            {
                theme.own = own;
                out.push_back(std::move(theme));
            }
        }
    }
}

// static
const std::vector<std::string>& ALScriptTheme::names()
{
    static std::vector<std::string> all;
    if (all.empty())
    {
        for (const auto& [name, label] : EDITOR_COLORS)
        {
            all.push_back(name);
        }
        for (size_t kind = 1; kind < static_cast<size_t>(ALSyntaxKind::COUNT); ++kind)
        {
            all.push_back(ALTextView::kindColorName("Script", static_cast<ALSyntaxKind>(kind)));
        }
    }
    return all;
}

// static
std::string ALScriptTheme::labelOf(const std::string& name)
{
    for (const auto& [own, label] : EDITOR_COLORS)
    {
        if (name == own)
        {
            return label;
        }
    }
    if (name.compare(0, 6, "Script") == 0)
    {
        return spaced(name.substr(6));
    }
    return name;
}

// static
bool ALScriptTheme::isEditorColor(const std::string& name)
{
    for (const auto& [own, label] : EDITOR_COLORS)
    {
        if (name == own)
        {
            return true;
        }
    }
    return false;
}

// static
bool ALScriptTheme::load(const std::string& path, ALScriptTheme& theme)
{
    llifstream in(path.c_str());
    if (!in.is_open())
    {
        return false;
    }
    LLSD data;
    if (LLSDSerialize::fromXML(data, in) == LLSDParser::PARSE_FAILURE || !data.isMap() || !data.has("colors"))
    {
        LL_WARNS("ScriptStudio") << "The theme at " << path << " could not be read" << LL_ENDL;
        return false;
    }
    theme.name   = data["name"].asString();
    theme.source = data["source"].asString();
    theme.dark   = data["dark"].asBoolean();
    theme.path   = path;
    theme.colors.clear();
    const LLSD& colors = data["colors"];
    for (LLSD::map_const_iterator it = colors.beginMap(); it != colors.endMap(); ++it)
    {
        LLColor4 color;
        color.setValue(it->second);
        theme.colors[it->first] = color;
    }
    if (theme.name.empty())
    {
        theme.name = gDirUtilp->getBaseFileName(path, true);
    }
    return !theme.colors.empty();
}

// static
std::vector<ALScriptTheme> ALScriptTheme::available()
{
    std::vector<ALScriptTheme> out;
    themesIn(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, FOLDER), false, out);
    themesIn(gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, FOLDER), true, out);
    std::stable_sort(out.begin(), out.end(), [](const ALScriptTheme& a, const ALScriptTheme& b) {
        if (a.own != b.own)
        {
            return !a.own;
        }
        return LLStringUtil::compareDict(a.name, b.name) < 0;
    });
    return out;
}

// static
ALScriptTheme ALScriptTheme::capture(const std::string& name)
{
    ALScriptTheme         theme;
    const LLUIColorTable& table = LLUIColorTable::instance();
    theme.name                  = name;
    theme.own                   = true;
    for (const std::string& color : names())
    {
        theme.colors[color] = table.getColor(color).get();
    }
    // Dark where the ground is.
    const LLColor4& ground = theme.colors["ScriptBackground"];
    theme.dark             = (ground.mV[0] + ground.mV[1] + ground.mV[2]) / 3.f < 0.5f;
    return theme;
}

bool ALScriptTheme::save(std::string& path_out) const
{
    const std::string folder = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, FOLDER);
    LLFile::mkdir(folder);
    path_out = gDirUtilp->add(folder, slugOf(name) + ".xml");
    LLSD data;
    data["name"]   = name;
    data["source"] = source;
    data["dark"]   = dark;
    LLSD colors    = LLSD::emptyMap();
    for (const auto& [color, value] : this->colors)
    {
        colors[color] = value.getValue();
    }
    data["colors"] = colors;
    llofstream out(path_out.c_str());
    if (!out.is_open())
    {
        return false;
    }
    LLSDSerialize::toPrettyXML(data, out);
    return out.good();
}

void ALScriptTheme::apply() const
{
    LLUIColorTable& table = LLUIColorTable::instance();
    for (const std::string& color : names())
    {
        const auto found = colors.find(color);
        if (found != colors.end())
        {
            table.setColor(color, found->second);
        }
        else
        {
            table.resetToDefault(color);
        }
    }
    table.saveUserSettings();
    setChosen(name);
}

// static
void ALScriptTheme::restoreSkin()
{
    LLUIColorTable& table = LLUIColorTable::instance();
    for (const std::string& color : names())
    {
        table.resetToDefault(color);
    }
    table.saveUserSettings();
    setChosen(LLStringUtil::null);
}

// static
std::string ALScriptTheme::chosen()
{
    return gSavedSettings.getString(SETTING);
}

// static
void ALScriptTheme::setChosen(const std::string& name)
{
    gSavedSettings.setString(SETTING, name);
}
