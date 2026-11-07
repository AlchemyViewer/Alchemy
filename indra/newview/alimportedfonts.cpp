/**
 * @file alimportedfonts.cpp
 * @brief User-imported font files: validation, copy into the per-user font
 *        directory, and the generated imported_fonts.xml overlay.
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

#include "linden_common.h"

#include "alimportedfonts.h"

#include "llfontgl.h"
#include "llfontfreetype.h"
#include "llfontregistry.h"
#include "lldir.h"
#include "llfile.h"
#include "llstring.h"
#include "llviewercontrol.h"

#include <cctype>
#include <cstdint>
#include <filesystem>

namespace
{
    constexpr const char* kSettingImported        = "AlchemyImportedFonts";
    constexpr const char* kSettingUIFontOverrides = "AlchemyUIFontOverrides";
    constexpr const char* kCategoryMonospace      = "monospace";
    constexpr const char* kCategoryEmoji          = "emoji";
    constexpr const char* kCategoryProportional   = "proportional";

    constexpr std::uintmax_t kMaxImportedFontBytes = 64u * 1024u * 1024u;   // 64 MiB

    constexpr const char* kSupportedExtensions[] = { "ttf", "otf", "woff2" };

    constexpr const char* kStyleNormal     = "NORMAL";
    constexpr const char* kStyleBold       = "BOLD";
    constexpr const char* kStyleItalic     = "ITALIC";
    constexpr const char* kStyleBoldItalic = "BOLD|ITALIC";

    bool hasSupportedExtension(const std::string& path)
    {
        if (gDirUtilp == nullptr)
            return false;
        const std::string ext = gDirUtilp->getExtension(path);
        for (const char* supported : kSupportedExtensions)
        {
            if (ext == supported)
                return true;
        }
        return false;
    }

    U32 fnv1a(const std::string& text)
    {
        U32 hash = 2166136261u;
        for (char c : text)
        {
            hash ^= static_cast<U32>(static_cast<unsigned char>(c));
            hash *= 16777619u;
        }
        return hash;
    }

    std::string makeFamilyKey(const std::string& family)
    {
        std::string slug;
        slug.reserve(family.size());
        for (char c : family)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (std::isalnum(u))
                slug.push_back(static_cast<char>(std::tolower(u)));
            else if (c == ' ' || c == '-' || c == '_')
                slug.push_back('_');
        }
        if (slug.empty())
            slug = "font";

        return "AlchemyImported_" + slug + "_" + llformat("%08x", fnv1a(family));
    }

    std::string makeFileSuffix(const std::string& source_path)
    {
        return llformat("%08x", fnv1a(source_path));
    }

    const char* styleFromInfo(const LLFontFreetype::FaceInfo& info)
    {
        if (info.bold && info.italic)
            return kStyleBoldItalic;
        if (info.bold)
            return kStyleBold;
        if (info.italic)
            return kStyleItalic;
        return kStyleNormal;
    }

    std::string categoryToString(ALImportedFonts::Category category)
    {
        switch (category)
        {
            case ALImportedFonts::Category::MONOSPACE: return kCategoryMonospace;
            case ALImportedFonts::Category::EMOJI:     return kCategoryEmoji;
            case ALImportedFonts::Category::PROPORTIONAL:
            default:                                   return kCategoryProportional;
        }
    }

    void upsertFace(LLSD& entry, const std::string& file, const char* style)
    {
        LLSD files = entry["files"];
        if (!files.isArray())
            files = LLSD::emptyArray();

        for (LLSD::array_iterator fit = files.beginArray(); fit != files.endArray(); ++fit)
        {
            if ((*fit)["style"].asString() == style)
            {
                (*fit)["file"] = file;
                entry["files"]  = files;
                return;
            }
        }

        LLSD face = LLSD::emptyMap();
        face["file"]  = file;
        face["style"] = style;
        files.append(face);
        entry["files"] = files;
    }

    // Returns true when a new family entry was created.
    bool addFaceToImported(const std::string& family_key, const std::string& label,
                           const std::string& copied_file, const char* style,
                           ALImportedFonts::Category category)
    {
        LLSD list = ALImportedFonts::getImported();

        for (LLSD::array_iterator it = list.beginArray(); it != list.endArray(); ++it)
        {
            if ((*it)["name"].asString() == family_key)
            {
                upsertFace(*it, copied_file, style);
                gSavedSettings.setLLSD(kSettingImported, list);
                return false;
            }
        }

        LLSD entry = LLSD::emptyMap();
        entry["name"]     = family_key;
        entry["label"]    = label;
        entry["category"] = categoryToString(category);
        entry["files"]    = LLSD::emptyArray();
        upsertFace(entry, copied_file, style);
        list.append(entry);

        gSavedSettings.setLLSD(kSettingImported, list);
        return true;
    }

    void deleteFamilyFiles(const LLSD& entry)
    {
        const std::string dir = ALImportedFonts::getUserFontDir(false);
        if (dir.empty())
            return;

        const LLSD files = entry["files"];
        for (LLSD::array_const_iterator fit = files.beginArray(); fit != files.endArray(); ++fit)
        {
            const std::string file = (*fit)["file"].asString();
            if (!file.empty())
                LLFile::remove(gDirUtilp->add(dir, file), 0);
        }
    }

    // Drop overrides that point at a removed family.
    void pruneOverridesForFamily(const std::string& family_name)
    {
        LLSD overrides = gSavedSettings.getLLSD(kSettingUIFontOverrides);
        if (!overrides.isMap())
            return;

        std::vector<std::string> to_erase;
        for (LLSD::map_const_iterator it = overrides.beginMap(); it != overrides.endMap(); ++it)
        {
            const LLSD& value = it->second;
            const std::string stored = (value.isMap() && value.has("value"))
                ? value["value"].asString() : value.asString();
            if (stored == family_name)
                to_erase.push_back(it->first);
        }
        if (to_erase.empty())
            return;

        for (const std::string& key : to_erase)
            overrides.erase(key);
        gSavedSettings.setLLSD(kSettingUIFontOverrides, overrides);
    }

    void notifyRegistryChanged()
    {
        LLFontGL::markFontsXmlDirty();
        LLFontGL::schedulePendingReload();
    }
}

// static
bool ALImportedFonts::isSupportedFontFile(const std::string& path)
{
    return hasSupportedExtension(path);
}

// static
void ALImportedFonts::resetOverrides()
{
    gSavedSettings.setLLSD(kSettingUIFontOverrides, LLSD::emptyMap());
    notifyRegistryChanged();
}

// static
std::string ALImportedFonts::getUserFontDir(bool create)
{
    if (gDirUtilp == nullptr)
        return {};

    const std::string dir = gDirUtilp->add(gDirUtilp->getOSUserAppDir(), "fonts");
    if (create && !LLFile::isdir(dir))
    {
        LLFile::mkdir(dir);
    }
    return dir;
}

// static
std::string ALImportedFonts::getOverlayPath()
{
    return LLFontGL::getImportedFontsOverlayPath();
}

// static
LLSD ALImportedFonts::getImported()
{
    const LLSD list = gSavedSettings.getLLSD(kSettingImported);
    return list.isArray() ? list : LLSD::emptyArray();
}

// static
std::string ALImportedFonts::categorySuffix(const LLSD& family_entry)
{
    const std::string category = family_entry["category"].asString();
    if (category == kCategoryMonospace)
        return " (mono)";
    if (category == kCategoryEmoji)
        return " (emoji)";
    return {};
}

// static
std::string ALImportedFonts::regenerateOverlay()
{
    // Create the user font directory if needed.
    if (getUserFontDir(true).empty())
        return {};

    const std::string path = getOverlayPath();
    if (path.empty())
        return {};

    auto xml_escape = [](const std::string& s)
    {
        std::string r;
        r.reserve(s.size());
        for (char c : s)
        {
            switch (c)
            {
                case '&':  r += "&amp;";  break;
                case '<':  r += "&lt;";   break;
                case '>':  r += "&gt;";   break;
                case '"':  r += "&quot;"; break;
                case '\'': r += "&apos;"; break;
                default:   r += c;        break;
            }
        }
        return r;
    };

    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\n";
    out += "<!-- Generated by ALImportedFonts. Do not edit; regenerated from the\n";
    out += "     AlchemyImportedFonts setting on every import/remove. -->\n";
    out += "<fonts>\n";

    const LLSD imported = getImported();
    for (LLSD::array_const_iterator eit = imported.beginArray(); eit != imported.endArray(); ++eit)
    {
        const LLSD& entry = *eit;
        const std::string name = entry["name"].asString();
        if (name.empty())
            continue;

        const std::string label    = entry["label"].asString();
        const std::string category = entry["category"].asString();

        std::string by_style[4];   // index by the slot order below
        const LLSD files = entry["files"];
        for (LLSD::array_const_iterator fit = files.beginArray(); fit != files.endArray(); ++fit)
        {
            const std::string style = (*fit)["style"].asString();
            const std::string file  = (*fit)["file"].asString();
            if (style == kStyleNormal)          by_style[0] = file;
            else if (style == kStyleBold)       by_style[1] = file;
            else if (style == kStyleItalic)     by_style[2] = file;
            else if (style == kStyleBoldItalic) by_style[3] = file;
        }

        out += "  <font name=\"" + xml_escape(name) + "\" label=\"" + xml_escape(label) + "\"";
        if (category == kCategoryMonospace)
            out += " monospace=\"true\"";
        else if (category == kCategoryEmoji)
            out += " emoji=\"true\"";
        out += ">\n";

        static const char* kSlots[4] = { kStyleNormal, kStyleBold, kStyleItalic, kStyleBoldItalic };
        for (int i = 0; i < 4; ++i)
        {
            if (by_style[i].empty())
                continue;
            out += "    <style name=\"";
            out += kSlots[i];
            out += "\"";
            if (i != 0)   // every non-NORMAL style inherits the NORMAL faces
                out += " inherit=\"true\"";
            out += ">\n      <file>";
            out += xml_escape(by_style[i]);
            out += "</file>\n    </style>\n";
        }

        out += "  </font>\n";
    }

    out += "</fonts>\n";

    LLFILE* fp = LLFile::fopen(path, LLFILE_MODE("wb"));
    if (!fp)
    {
        LL_WARNS() << "Imported fonts: could not write overlay " << path << LL_ENDL;
        return path;
    }
    fwrite(out.data(), 1, out.size(), fp);
    LLFile::close(fp);
    return path;
}

// static
bool ALImportedFonts::importFiles(const std::vector<std::string>& sources, ImportResult& result)
{
    result = ImportResult();

    const std::string dir = getUserFontDir(true);
    if (dir.empty())
    {
        LL_WARNS() << "Imported fonts: no user font directory available" << LL_ENDL;
        for (const std::string& source : sources)
            result.failed.push_back({ source, ImportResult::RejectReason::COPY_FAILED });
        return false;
    }

    for (const std::string& source : sources)
    {
        using RejectReason = ImportResult::RejectReason;

        if (!LLFile::isfile(source) || !hasSupportedExtension(source))
        {
            result.failed.push_back({ source, RejectReason::UNSUPPORTED_TYPE });
            continue;
        }

        const std::string leaf = gDirUtilp->getBaseFileName(source);
        if (!LLFile::isSafeFileName(leaf))
        {
            LL_WARNS() << "Imported fonts: unsafe file name '" << leaf << "'" << LL_ENDL;
            result.failed.push_back({ source, RejectReason::UNSAFE_NAME });
            continue;
        }

        std::error_code size_ec;
        const std::uintmax_t size = std::filesystem::file_size(source, size_ec);
        if (size_ec || size > kMaxImportedFontBytes)
        {
            LL_WARNS() << "Imported fonts: '" << source << "' is too large or unreadable" << LL_ENDL;
            result.failed.push_back({ source, RejectReason::TOO_LARGE });
            continue;
        }

        const LLFontFreetype::FaceInfo info = LLFontFreetype::probeFaceInfo(source);
        if (!info.valid)
        {
            LL_WARNS() << "Imported fonts: '" << source << "' is not a readable font" << LL_ENDL;
            result.failed.push_back({ source, RejectReason::UNREADABLE });
            continue;
        }

        // Copy with a collision-proof name; getExtension() omits the dot.
        const std::string ext       = gDirUtilp->getExtension(leaf);
        const std::string stem      = leaf.substr(0, leaf.size() - (ext.empty() ? 0 : ext.size() + 1));
        const std::string dest_leaf = stem + "." + makeFileSuffix(source) + "." + ext;
        const std::string dest      = gDirUtilp->add(dir, dest_leaf);

        std::error_code ec;
        if (!LLFile::copy(source, dest, std::filesystem::copy_options::overwrite_existing, ec))
        {
            LL_WARNS() << "Imported fonts: could not copy " << source << " -> " << dest
                       << " (" << ec.message() << ")" << LL_ENDL;
            result.failed.push_back({ source, RejectReason::COPY_FAILED });
            continue;
        }

        const Category category = info.has_color   ? Category::EMOJI
                                : info.fixed_width ? Category::MONOSPACE
                                                   : Category::PROPORTIONAL;

        if (addFaceToImported(makeFamilyKey(info.family), info.family, dest_leaf,
                              styleFromInfo(info), category))
        {
            result.families_added++;
        }
        result.files_copied++;
    }

    regenerateOverlay();
    notifyRegistryChanged();
    return result.anyCopied();
}

// static
bool ALImportedFonts::removeFamily(const std::string& family_name)
{
    const LLSD list = getImported();
    if (!list.isArray())
        return false;

    LLSD kept = LLSD::emptyArray();
    bool removed = false;
    for (LLSD::array_const_iterator it = list.beginArray(); it != list.endArray(); ++it)
    {
        if ((*it)["name"].asString() == family_name)
        {
            deleteFamilyFiles(*it);
            removed = true;
            continue;
        }
        kept.append(*it);
    }

    if (!removed)
        return false;

    gSavedSettings.setLLSD(kSettingImported, kept);
    pruneOverridesForFamily(family_name);
    regenerateOverlay();
    notifyRegistryChanged();
    return true;
}
