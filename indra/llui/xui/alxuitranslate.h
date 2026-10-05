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
        Forbidden,      // translated under translate="false"
        // The language wrote it where the base used to have the element,
        // and the merge applied it where the base has it now: the one
        // element of that name below. Applied, and worth moving all the
        // same, since the next element of that name the base grows makes
        // it a guess the merge does not take.
        Rescued
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

        bool applies() const
        {
            return state == State::Translated || state == State::Placeholders || state == State::Rescued;
        }
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
    // create_when_absent is what a person typing a translation wants and
    // what a repair must not do: a repair moves what the language wrote,
    // and writing it again where it cannot be found puts a second copy of
    // it into the file.
    static bool write(ALXUIEdit& overlay, pugi::xml_node base, const Unit& unit,
                      const std::string& text, std::string& error, bool create_when_absent = true);

    // Take the language's value for a unit out of its file: the attribute,
    // or the text -- and the whole element where the text was all it
    // carried, since an element left with nothing but its name says nothing
    // to the merge. The shells a removal leaves go with it. This is how a
    // value that names something the base no longer has leaves the file,
    // which no repair can do for it.
    static bool remove(ALXUIEdit& overlay, const Unit& unit, std::string& error);

    // A value the language writes for something the base has nowhere: an
    // element of a name the base has lost, an element with no name to be
    // matched by, or an attribute the base's element does not carry. Not
    // one the base has moved, which a repair puts right, and not one the
    // base has at more than one path, which is a person's to place.
    static bool isOrphan(const Unit& unit);

    // Every orphan taken out of the file, one at a time with the file
    // read again between, since taking one out moves the paths of what
    // is beside it. Returns how many went; the caller saves. What a
    // repair would move is left where it is.
    static S32 removeOrphans(ALXUIEdit& overlay, pugi::xml_node base, std::string& error);

    // What a file would say if the merge read it. A value the base has a
    // place for arrives -- at its path already, or after a move -- and a
    // value naming something the base has nowhere is what the file has
    // outlived.
    void weigh(S32& arrives, S32& absent) const;

    // Whether an overlay whose root carries another name is this file
    // under that name. A root with no name at all is an omission. A root
    // with a name of its own is the same file when more of what it says
    // names something the base has than does not; when almost none of it
    // does, it is a translation of something else and renaming it would
    // only move its contents into the count of what applies to nothing.
    bool sameFileRenamed(std::string_view overlay_root) const;

    // Every value the language writes at a path the base has moved on
    // from, moved to where the base has it, with its own text unchanged.
    // Nothing else is touched, and what the base has at more than one
    // path is left for a person. Returns how many moved; the caller
    // saves.
    static S32 repair(ALXUIEdit& overlay, pugi::xml_node base, std::string& error);

    // The ancestors of a path, written into the overlay if they are not
    // there, each carrying nothing but its name -- because the merge matches
    // on names, and a name is all an overlay has to say about the way down to
    // what it overrides.
    //
    // This is about overlays rather than about translations: a language file
    // is the commonest one, and a skin that overrides a number in a file it
    // otherwise says nothing about needs the same chain for the same reason.
    // An ancestor the overlay already has somewhere else is moved rather than
    // written a second time, since a file that names one element twice is a
    // file the merge has to guess about.
    static bool ensureChain(ALXUIEdit& overlay, pugi::xml_node base, const path_t& path, std::string& error);

private:
    void scanBase(pugi::xml_node base, pugi::xml_node overlay);
    void scanOverlay(pugi::xml_node base, pugi::xml_node overlay);

    // One sweep of the moves, which repair runs until it changes nothing.
    static S32 movePass(ALXUIEdit& overlay, pugi::xml_node base, std::string& error);

    // The shells a move leaves behind: elements with nothing in them and
    // nothing on them but a name.
    static void prune(ALXUIEdit& overlay);

    std::vector<Unit> mUnits;
};
