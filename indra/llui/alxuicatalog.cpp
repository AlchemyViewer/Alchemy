/**
 * @file alxuicatalog.cpp
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

#include "linden_common.h"

#include "alxuicatalog.h"

#include "alxmldocument.h"
#include "alxuiselection.h"
#include "fsyspath.h"
#include "llcontainerview.h"
#include "lldir.h"
#include "lllayoutstack.h"
#include "llmenugl.h"
#include "llscrollcontainer.h"
#include "llstatview.h"
#include "lluictrlfactory.h"

#include <algorithm>
#include <filesystem>

namespace
{
    constexpr size_t MAX_HITS = 2000;

    bool isDir(const std::filesystem::directory_entry& entry)
    {
        std::error_code ec;
        return entry.is_directory(ec);
    }

    // Names in a directory, in name order so a scan is the same every time.
    std::vector<std::string> listDir(const std::string& dir, bool directories)
    {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(fsyspath(dir), ec))
        {
            if (isDir(entry) != directories)
            {
                continue;
            }
            std::string name = fsyspath(entry.path().filename()).string();
            if (!directories && !name.ends_with(".xml"))
            {
                continue;
            }
            names.push_back(std::move(name));
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    std::string asciiLower(std::string_view text)
    {
        std::string lower(text);
        for (char& c : lower)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = (char)(c - 'A' + 'a');
            }
        }
        return lower;
    }

    bool matches(std::string_view haystack, const std::string& lower_needle,
                 ALXUICatalog::Match how)
    {
        const std::string in = asciiLower(haystack);
        switch (how)
        {
        case ALXUICatalog::Match::Matching:  return in == lower_needle;
        case ALXUICatalog::Match::Starting:  return in.rfind(lower_needle, 0) == 0;
        case ALXUICatalog::Match::Ending:
            return in.size() >= lower_needle.size()
                && in.compare(in.size() - lower_needle.size(), lower_needle.size(), lower_needle) == 0;
        case ALXUICatalog::Match::Containing: break;
        }
        return in.find(lower_needle) != std::string::npos;
    }

    void sortFirst(std::vector<std::string>& names, const char* first)
    {
        std::sort(names.begin(), names.end());
        auto it = std::find(names.begin(), names.end(), first);
        if (it != names.end())
        {
            std::rotate(names.begin(), it, it + 1);
        }
    }

    std::string elementText(pugi::xml_node node)
    {
        std::string text;
        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
        {
            if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
            {
                text += child.value();
            }
        }
        return text;
    }

    // Every element under a root, the root included, in document order.
    template <typename F>
    bool eachElement(pugi::xml_node node, F& fn)
    {
        if (node.type() == pugi::node_element)
        {
            if (!fn(node))
            {
                return false;
            }
        }
        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
        {
            if (!eachElement(child, fn))
            {
                return false;
            }
        }
        return true;
    }
}

pugi::xml_node ALXUICatalog::Layer::root() const
{
    return doc ? doc->document().document_element() : pugi::xml_node();
}

const ALXUICatalog::Layer* ALXUICatalog::Entry::layer(std::string_view skin, std::string_view language) const
{
    for (const Layer& layer : layers)
    {
        if (layer.skin == skin && layer.language == language)
        {
            return &layer;
        }
    }
    return nullptr;
}

ALXUICatalog::ALXUICatalog() = default;
ALXUICatalog::~ALXUICatalog() = default;

void ALXUICatalog::clear()
{
    mEntries.clear();
    mScanIndex.clear();
    mSkins.clear();
    mLanguages.clear();
}

void ALXUICatalog::scan(const std::string& skins_dir)
{
    clear();

    const std::string delim = gDirUtilp->getDirDelimiter();
    for (const std::string& skin : listDir(skins_dir, true))
    {
        const std::string xui = skins_dir + delim + skin + delim + "xui";
        std::vector<std::string> languages = listDir(xui, true);
        if (languages.empty())
        {
            continue;
        }
        mSkins.push_back(skin);
        for (const std::string& language : languages)
        {
            if (std::find(mLanguages.begin(), mLanguages.end(), language) == mLanguages.end())
            {
                mLanguages.push_back(language);
            }
            const std::string dir = xui + delim + language;
            scanLanguage(skin, language, dir, std::string());
            scanLanguage(skin, language, dir + delim + "widgets", "widgets/");
        }
    }

    sortFirst(mSkins, "default");
    sortFirst(mLanguages, "en");
    std::sort(mEntries.begin(), mEntries.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });
    mScanIndex.clear();
}

bool ALXUICatalog::reload(std::string_view name)
{
    if (Entry* found = findEntry(name))
    {
        Entry& entry = *found;
        for (Layer& layer : entry.layers)
        {
            layer.error.clear();
            layer.errorLine = 0;
            layer.doc = std::make_unique<ALXmlDocument>();
            if (!layer.doc->loadFile(layer.path))
            {
                layer.error = layer.doc->errorDescription();
                layer.errorLine = layer.doc->errorLine();
                layer.doc.reset();
                continue;
            }
            // What the file says it is can have changed with it.
            if (layer.skin == "default" && layer.language == "en")
            {
                describe(entry, layer);
            }
        }
        return true;
    }
    return false;
}

void ALXUICatalog::scanLanguage(const std::string& skin, const std::string& language,
                                const std::string& dir, const std::string& prefix)
{
    const std::string delim = gDirUtilp->getDirDelimiter();
    for (const std::string& file : listDir(dir, false))
    {
        Entry& entry = entryFor(prefix + file);
        Layer& layer = entry.layers.emplace_back();
        layer.skin = skin;
        layer.language = language;
        layer.path = dir + delim + file;
        layer.doc = std::make_unique<ALXmlDocument>();
        if (!layer.doc->loadFile(layer.path))
        {
            layer.error = layer.doc->errorDescription();
            layer.errorLine = layer.doc->errorLine();
            layer.doc.reset();
            continue;
        }
        // The base language of the default skin describes the file; any
        // other layer only until that one is read.
        if (entry.rootTag.empty() || (skin == "default" && language == "en"))
        {
            describe(entry, layer);
        }
    }
}

// What the file is, taken from its root: the tag decides the kind and the
// title is what a list of files reads best by.
void ALXUICatalog::describe(Entry& entry, const Layer& layer)
{
    const pugi::xml_node root = layer.root();
    entry.rootTag = root.name();
    entry.kind = kindOf(entry.rootTag, entry.name);
    entry.title = root.attribute("title").as_string();
    if (entry.title.empty())
    {
        entry.title = root.attribute("label").as_string();
    }
}

// During a scan, when the entries are in the order the directories gave
// them: seven hundred files, each looked up once per skin and language it
// is written in.
ALXUICatalog::Entry& ALXUICatalog::entryFor(const std::string& name)
{
    if (const auto held = mScanIndex.find(name); held != mScanIndex.end())
    {
        return mEntries[held->second];
    }
    mScanIndex.emplace(name, mEntries.size());
    Entry& entry = mEntries.emplace_back();
    entry.name = name;
    return entry;
}

// After one, when they are sorted by name. Asked on every hover of a drag,
// every row of a list and every attribute a rule reads a file name off.
ALXUICatalog::Entry* ALXUICatalog::findEntry(std::string_view name)
{
    const auto at = std::lower_bound(mEntries.begin(), mEntries.end(), name,
                                     [](const Entry& entry, std::string_view wanted)
                                     { return entry.name < wanted; });
    return at != mEntries.end() && at->name == name ? &*at : nullptr;
}

const ALXUICatalog::Entry* ALXUICatalog::find(std::string_view name) const
{
    return const_cast<ALXUICatalog*>(this)->findEntry(name);
}

std::vector<const ALXUICatalog::Layer*> ALXUICatalog::layersFor(const Entry& entry, std::string_view skin,
                                                                std::string_view language) const
{
    // The skins in the order the directory object searches them: default
    // first, then the chosen one, and the last file found wins.
    const Layer* base = nullptr;
    const Layer* localized = nullptr;
    const std::string_view skins_in_order[2] = { "default", skin };
    for (std::string_view s : skins_in_order)
    {
        if (const Layer* l = entry.layer(s, "en"))
        {
            base = l;
        }
        if (language != "en")
        {
            if (const Layer* l = entry.layer(s, language))
            {
                localized = l;
            }
        }
    }

    std::vector<const Layer*> layers;
    if (base)
    {
        layers.push_back(base);
        if (localized)
        {
            layers.push_back(localized);
        }
    }
    return layers;
}

// static
bool ALXUICatalog::isWidgetTag(std::string_view tag)
{
    const std::string key(tag);
    return LLDefaultChildRegistry::instance().getValue(key)
        || MenuRegistry::instance().getValue(key)
        || LLLayoutStack::LayoutStackRegistry::instance().getValue(key)
        || ScrollContainerRegistry::instance().getValue(key)
        || ContainerViewRegistry::instance().getValue(key)
        || StatViewRegistry::instance().getValue(key);
}

// static
ALXUICatalog::Kind ALXUICatalog::kindOf(std::string_view root_tag, std::string_view name)
{
    if (name.starts_with("widgets/"))
    {
        return Kind::Template;
    }
    if (root_tag == "floater" || root_tag == "multi_floater")
    {
        return Kind::Floater;
    }
    if (root_tag == "panel")
    {
        return Kind::Panel;
    }
    if (root_tag == "menu" || root_tag == "menu_bar" || root_tag == "context_menu" || root_tag == "toggleable_menu")
    {
        return Kind::Menu;
    }
    if (root_tag == "notifications")
    {
        return Kind::Notifications;
    }
    if (root_tag == "strings")
    {
        return Kind::Strings;
    }
    if (isWidgetTag(root_tag))
    {
        return Kind::Widget;
    }
    return Kind::Other;
}

// static
const char* ALXUICatalog::kindName(Kind kind)
{
    switch (kind)
    {
    case Kind::Floater:         return "floater";
    case Kind::Panel:           return "panel";
    case Kind::Menu:            return "menu";
    case Kind::Widget:          return "widget";
    case Kind::Template:        return "template";
    case Kind::Notifications:   return "notifications";
    case Kind::Strings:         return "strings";
    case Kind::Other:           break;
    }
    return "other";
}

// static
std::vector<std::string> ALXUICatalog::namePath(pugi::xml_node node, bool any_tag)
{
    std::vector<std::string> path;
    for (pugi::xml_node cur = node; cur && cur.parent() && cur.parent().type() == pugi::node_element; cur = cur.parent())
    {
        const char* name = cur.attribute("name").as_string("unnamed");
        S32 ordinal = 0;
        for (pugi::xml_node sib = cur.previous_sibling(); sib; sib = sib.previous_sibling())
        {
            if (sib.type() == pugi::node_element && (any_tag || isWidgetTag(sib.name()))
                && std::string_view(sib.attribute("name").as_string("unnamed")) == name)
            {
                ++ordinal;
            }
        }
        path.push_back(ALXUISelection::step(name, ordinal));
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// static
pugi::xml_node ALXUICatalog::resolve(pugi::xml_node root, const std::vector<std::string>& path, bool any_tag)
{
    pugi::xml_node cur = root;
    for (const std::string& step : path)
    {
        std::string_view name;
        S32 wanted = 0;
        ALXUISelection::splitOrdinal(step, name, wanted);
        pugi::xml_node found;
        S32 seen = 0;
        for (pugi::xml_node child = cur.first_child(); child; child = child.next_sibling())
        {
            if (child.type() != pugi::node_element || !(any_tag || isWidgetTag(child.name())))
            {
                continue;
            }
            if (std::string_view(child.attribute("name").as_string("unnamed")) == name)
            {
                if (seen++ == wanted)
                {
                    found = child;
                    break;
                }
            }
        }
        if (!found)
        {
            return pugi::xml_node();
        }
        cur = found;
    }
    return cur;
}

// static
S32 ALXUICatalog::lineOf(const Layer& layer, pugi::xml_node node)
{
    return layer.doc && node ? layer.doc->lineOf(node.offset_debug()) : 0;
}

std::vector<ALXUICatalog::Hit> ALXUICatalog::find(std::string_view query, Field field, Match match,
                                                  std::string_view skin, std::string_view language) const
{
    std::vector<Hit> hits;
    if (query.empty())
    {
        return hits;
    }
    const std::string needle = asciiLower(query);

    for (const Entry& entry : mEntries)
    {
        for (const Layer& layer : entry.layers)
        {
            if (!layer.doc
                || (!skin.empty() && layer.skin != skin)
                || (!language.empty() && layer.language != language))
            {
                continue;
            }
            auto visit = [&](pugi::xml_node node) -> bool
            {
                // The comparison is the same for every field; only what is
                // compared and what is shown differ.
                std::string snippet;
                bool matched = false;
                const bool any = field == Field::Any;

                if (any || field == Field::Tag)
                {
                    if (matches(node.name(), needle, match))
                    {
                        matched = true;
                        snippet = std::string("<") + node.name() + ">";
                    }
                }
                if (!matched && (any || field == Field::Name))
                {
                    const pugi::xml_attribute name = node.attribute("name");
                    if (name && matches(name.value(), needle, match))
                    {
                        matched = true;
                        snippet = std::string("name=\"") + name.value() + "\"";
                    }
                }
                if (!matched && (any || field == Field::Attribute || field == Field::Value))
                {
                    for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute())
                    {
                        const bool by_name = (any || field == Field::Attribute) && matches(attr.name(), needle, match);
                        const bool by_value = (any || field == Field::Value) && matches(attr.value(), needle, match);
                        if (by_name || by_value)
                        {
                            matched = true;
                            snippet = std::string(attr.name()) + "=\"" + attr.value() + "\"";
                            break;
                        }
                    }
                }
                if (!matched && (any || field == Field::Text))
                {
                    const std::string text = utf8str_trim(elementText(node));
                    if (!text.empty() && matches(text, needle, match))
                    {
                        matched = true;
                        snippet = text;
                    }
                }
                if (!matched)
                {
                    return true;
                }

                Hit& hit = hits.emplace_back();
                hit.entry = &entry;
                hit.layer = &layer;
                hit.line = lineOf(layer, node);
                hit.tag = node.name();
                hit.path = ALXUISelection::toString(namePath(node));
                hit.snippet = std::move(snippet);
                return hits.size() < MAX_HITS;
            };
            if (!eachElement(layer.root(), visit))
            {
                return hits;
            }
        }
    }
    return hits;
}
