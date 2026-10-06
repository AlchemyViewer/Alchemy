/**
 * @file alxuischema.h
 * @brief What a XUI file may say, read out of the widget registries.
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

#include "alparamtype.h"
#include "llstl.h"
#include "stdtypes.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <string>
#include <string_view>
#include <vector>

// Every widget tag, and what a file may write under it: the attributes its
// parameter block answers to, the parameter elements it takes, the tags that
// may appear below it, and whether it reads its own text.
//
// The registries hold all of this already, one piece each. What they had lost
// was the type of a leaf, which only the template declaring the parameter
// knew; ALParamTypes carries it back, and this is what reads it.
//
// Two things use the answer. The lint asks whether an attribute exists, which
// is a whole-tree schema validation with no XSD library in it. The XSD writer
// gives an editor the same answer while a file is being typed. Where XUI is
// ambiguous -- the same parameter written as an attribute or an element, the
// same value named or spelled out -- the schema is permissive and the lint is
// strict.
class ALXUISchema
{
public:
    struct Attribute
    {
        // As it is written: a nested block's leaves carry the dots.
        std::string                 name;
        ALParamType::EValue         value = ALParamType::OTHER;
        // Mandatory, and only where a file could satisfy it: an attribute
        // below the top level has an element form too, and no schema can
        // say "one of these two spellings".
        bool                        required = false;
        // A C++ enumeration's names. Empty for everything else, including
        // the types that accept a name or any string besides.
        std::vector<std::string>    values;
        // The type to read: a word for the few a schema can name, the names
        // themselves for an enumeration, and the C++ type for the rest,
        // whose vocabulary is in another file. For the inspector, which is
        // the only place a developer sees it.
        std::string                 type;
        // Declared only so that a file may write it and be quiet about it:
        // `type=` and `length=` are read off every widget and thrown away,
        // and a tool that shows them beside the ones that do something is
        // telling an author they do something.
        bool                        ignored = false;
        // What the element carries when the file says nothing about it, in
        // the spelling a file would use. Empty where the parameter holds no
        // value to write -- a colour named by nothing, a callback nobody
        // set -- which is not the same as carrying an empty one, and the
        // two are told apart by asking whether anything is here at all.
        std::string                 held;
        bool                        holds = false;
        // Another name for the same parameter, where the block registered
        // one. Both work and both always will; which of the two a file
        // should use is not something a registry can know, so the pair is
        // read out of the code and the preference out of the notes.
        std::string                 alias;
        // What the notes say about it, or nothing where nobody has written
        // any: which heading it belongs under when the tool's guess over the
        // vocabulary is wrong, and whether it is a name that works and should
        // not be used.
        std::string                 section;
        bool                        deprecated = false;
        std::string                 instead;
        // And whether that is a value somebody chose for this widget rather
        // than the zero its type starts at. A button is twenty-three pixels
        // tall because its template says so; it is at left nought because
        // nought is where an S32 starts, and a file writing `left="0"` is
        // saying where the thing goes rather than repeating anybody.
        bool                        declared = false;
    };

    struct Element
    {
        std::string name;       // written whole: tag.param
        S32         minCount = 0;
        S32         maxCount = 1;
    };

    struct Tag
    {
        std::string              name;
        // One sentence saying what it is for, from the notes; empty where
        // nobody has written one.
        std::string              note;
        std::vector<Attribute>   attributes;
        std::vector<Element>     elements;
        std::vector<std::string> children;
        // The tag reads what is between its tags into `value`.
        bool                     text = false;
    };

    // Built once, the first time anything asks. Building constructs a default
    // parameter block per tag, which is what fills that block's table: a
    // widget type this session never created is in here all the same.
    static const ALXUISchema& get();

    const Tag*                  tag(std::string_view name) const;
    const std::vector<Tag>&     tags() const { return mTags; }

    // Whether a tag takes an attribute of that name. False for a tag the
    // schema does not know, which is not the same question -- ask tag()
    // first if the difference matters.
    bool                        accepts(std::string_view tag, std::string_view attribute) const;

    // What a tag says about one attribute, or null where it says nothing.
    const Attribute*            attribute(std::string_view tag, std::string_view name) const;

    // The declared attribute a name was probably meant to be. A typo is one
    // or two edits away from a real name, and the whole vocabulary of the tag
    // is here to compare against: `dynamicwidth` is `dynamic_width` with a
    // character missing, and it has shipped in nineteen places for years
    // because nothing was in a position to say so.
    //
    // Empty where nothing is near enough, and empty where two names are
    // equally near -- a guess between two is not a suggestion, it is a coin
    // toss with the file as the stake.
    std::string                 nearestSpelling(std::string_view tag, std::string_view name) const;

    // Whether a container takes that tag as a child. The question a drop
    // asks: a container with a child registry of its own answers for the
    // few tags in it, and a container that never declared one answers for
    // every widget there is. False for a tag the schema does not know.
    bool                        acceptsChild(std::string_view tag, std::string_view child) const;

    // Whether a specimen of the tag can be built from a name and a size
    // alone. The rest want a parent of a kind, children, or parameters no
    // default carries, and a specimen of one is a picture of a failure.
    static bool                 buildsAlone(const std::string& tag);

    // The five kinds a developer looks for a tag under, in the order they
    // are listed. The key is the name of the string a window says the kind
    // in.
    enum class Group : U8 { Containers, Controls, Lists, Text, Chrome };
    static Group                groupOf(std::string_view tag);
    static const char*          groupKey(Group group);

    // The whole model as one XSD document.
    std::string                 asXSD() const;

    // Counts, for the line a regeneration writes.
    std::string                 summary() const;

private:
    void build();

    std::vector<Tag> mTags;
    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<> > mIndex;
};
