/**
 * @file alxuicatalog.h
 * @brief Every XUI file across the skins and languages on disk, parsed and searchable.
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

#include "stdtypes.h"

#include <pugixml.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ALXmlDocument;

// The files under <skins>/<skin>/xui/<language>/, one entry per file name
// with every skin and language that has it. Each file is parsed once and
// kept, so that a search over all of them is a walk and a lookup of the
// element behind a built view is a walk down its name path.
class ALXUICatalog
{
public:
    enum class Kind : U8
    {
        Floater,
        Panel,
        Menu,
        Widget,         // a registered widget tag at the root, built as itself
        Template,       // widgets/<tag>.xml, the defaults for that tag
        Notifications,
        Strings,
        Other           // not built by the factory: alerts, control tables, LLSD
    };

    // One file on disk.
    struct Layer
    {
        std::string                     skin;
        std::string                     language;
        std::string                     path;
        std::unique_ptr<ALXmlDocument>  doc;        // null when the file did not parse
        std::string                     error;      // why, when it did not
        S32                             errorLine = 0;

        pugi::xml_node root() const;
    };

    struct Entry
    {
        std::string         name;       // "floater_about.xml", "widgets/button.xml"
        Kind                kind = Kind::Other;
        std::string         rootTag;    // from the first layer that parsed
        std::string         title;      // the root's title, else its label
        std::vector<Layer>  layers;     // every file with this name, in scan order

        const Layer* layer(std::string_view skin, std::string_view language) const;
    };

    ALXUICatalog();
    ~ALXUICatalog();

    ALXUICatalog(const ALXUICatalog&) = delete;
    ALXUICatalog& operator=(const ALXUICatalog&) = delete;

    // Reads every skin directory under skins_dir, every language under its
    // xui directory, and every file in each with its widgets subdirectory.
    void scan(const std::string& skins_dir);
    void clear();

    // Re-read the files of one entry, for when they were written from
    // here and the whole tree need not be walked again.
    bool reload(std::string_view name);

    const std::vector<Entry>& entries() const { return mEntries; }
    const Entry* find(std::string_view name) const;

    // "default" first, then the rest in name order; "en" first likewise.
    const std::vector<std::string>& skins() const { return mSkins; }
    const std::vector<std::string>& languages() const { return mLanguages; }

    // The files the viewer's merge reads for a skin and language, in the
    // order it reads them: the base-language file from the most specific
    // skin that has one, then the language's file from the most specific
    // skin that has one. A skin's base-language file replaces default's
    // rather than layering on it. Empty when no skin has the base file.
    std::vector<const Layer*> layersFor(const Entry& entry, std::string_view skin, std::string_view language) const;

    static Kind kindOf(std::string_view root_tag, std::string_view name);
    static const char* kindName(Kind kind);

    // Whether any child registry builds a widget from the tag.
    static bool isWidgetTag(std::string_view tag);

    enum class Field : U8
    {
        Tag,
        Attribute,      // an attribute's name
        Value,          // an attribute's value
        Name,           // the name attribute's value
        Text,
        Any
    };

    // How the words are compared with what is in the file. A search over six
    // hundred files finds a hundred things containing "close" and one whose
    // name is it, and only the reader knows which they meant.
    enum class Match : U8
    {
        Containing,
        Matching,       // the whole of it and nothing else
        Starting,
        Ending
    };

    struct Hit
    {
        const Entry*    entry = nullptr;
        const Layer*    layer = nullptr;
        S32             line = 0;
        std::string     tag;
        std::string     path;       // the element's name path, joined with '/'
        std::string     snippet;    // what matched, as it reads in the file
    };

    // A case-insensitive substring search over every parsed layer, or over
    // one skin's and one language's when given.
    std::vector<Hit> find(std::string_view query, Field field,
                          Match match = Match::Containing,
                          std::string_view skin = std::string_view(),
                          std::string_view language = std::string_view()) const;

    // The names from the root's child down to an element, which is the
    // key the merge matches by and the key the selection is held under.
    // An element without a name is "unnamed"; a repeated name among the
    // siblings carries its ordinal as "name#2".
    //
    // A path that addresses a view counts widget siblings, since only
    // those became views. A path that addresses an element of a file
    // counts every named sibling, because that is what the merge matches
    // on: the parameter elements -- <floater.string>, <scroll_list.columns>
    // -- carry names and are matched by them, and a path that walks past
    // them cannot name what they hold.
    static std::vector<std::string> namePath(pugi::xml_node node, bool any_tag = false);

    // The element at a name path under a root: at each step, the child
    // with that name, or nothing.
    static pugi::xml_node resolve(pugi::xml_node root, const std::vector<std::string>& path, bool any_tag = false);

    // The line an element starts on in the layer that holds it.
    static S32 lineOf(const Layer& layer, pugi::xml_node node);

private:
    void scanLanguage(const std::string& skin, const std::string& language,
                      const std::string& dir, const std::string& prefix);
    Entry& entryFor(const std::string& name);
    static void describe(Entry& entry, const Layer& layer);

    std::vector<Entry>          mEntries;
    std::vector<std::string>    mSkins;
    std::vector<std::string>    mLanguages;
};
