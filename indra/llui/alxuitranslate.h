/**
 * @file alxuitranslate.h
 * @brief What a translator writes in a XUI file, whether it arrived, and how to write it.
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

#include <string>
#include <string_view>
#include <vector>

class ALXUIEdit;

// The translatable fields of one file, with what a language has for each
// and whether the merge accepted it. A unit is one field of one element:
// an attribute translators write, or the element's own text.
//
// The write side is the point of it. A translation written from here
// lands where the merge will find it -- under the ancestors the base
// gives it, at the name the base uses -- so that the mis-nesting that
// stranded a fifth of the shipped overlays cannot be authored again.
class ALXUITranslate
{
public:
    using path_t = std::vector<std::string>;

    enum class State : U8
    {
        Translated,     // the language has it, and the merge applies it
        Missing,        // the base has it, the language does not
        NotApplied,     // the language has it and the merge drops it
        Placeholders,   // applied, but its [KEY] tokens are not the base's
        Forbidden       // translated under translate="false"
    };

    // Why a value the language wrote applies to nothing.
    enum class Miss : U8
    {
        None,
        Moved,          // the base has that name somewhere else
        Absent,         // the base has that name nowhere
        Unnamed,        // no name to match on
        Ambiguous,      // the base has that name at more than one path
        AttributeAbsent // the base element does not carry the attribute
    };

    struct Unit
    {
        path_t      path;               // in the base
        std::string tag;                // the base element's tag
        std::string field;              // an attribute's name; empty for text
        std::string english;
        std::string translation;
        std::string where;              // where the language put it, when that is not the path
        State       state = State::Missing;
        Miss        miss = Miss::None;
        S32         baseLine = 0;
        S32         overlayLine = 0;

        bool applies() const { return state == State::Translated || state == State::Placeholders; }
    };

    // Every translatable field of the base with what the overlay has for
    // it, then everything the overlay says that the base has no place
    // for. The overlay may be an empty node: then every unit is missing.
    void scan(pugi::xml_node base, pugi::xml_node overlay);
    void clear();

    const std::vector<Unit>& units() const { return mUnits; }
    S32 count(State state) const;

    // The attributes translators write, from a census of the shipped
    // overlays; the edge cases are decided by shouldTranslate, which is
    // scripts/code_tools/modified_strings.py's rule ported as it stands.
    static bool isTranslatableField(std::string_view name);
    static bool shouldTranslate(pugi::xml_node element, std::string_view field, std::string_view value);

    // A value the file says must not be translated, on the element or on
    // anything above it.
    static bool forbidden(pugi::xml_node element);

    // The [KEY] tokens of a string, in the order they appear. A
    // translation that does not carry the same set has gone stale against
    // an English string that grew one.
    static std::vector<std::string> placeholders(std::string_view text);

    // Write one unit's translation into the overlay: set it on the
    // element already at the path, move the element the language put
    // elsewhere under the ancestors the base gives it, or create it with
    // that chain, each ancestor carrying nothing but its name.
    //
    // It writes no layout attribute, no attribute the base element does
    // not carry, nothing under translate="false" and no empty text: the
    // four ways an overlay is broken by writing it.
    static bool write(ALXUIEdit& overlay, pugi::xml_node base, const Unit& unit,
                      const std::string& text, std::string& error);

private:
    void scanBase(pugi::xml_node base, pugi::xml_node overlay);
    void scanOverlay(pugi::xml_node base, pugi::xml_node overlay);

    // The ancestors of a path, written into the overlay if they are not
    // there, each carrying nothing but its name.
    static bool ensureChain(ALXUIEdit& overlay, pugi::xml_node base, const path_t& path, std::string& error);

    std::vector<Unit> mUnits;
};
