/**
 * @file alxuinotes.h
 * @brief What a person knows about the widget vocabulary that a registry cannot.
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

#include <string>
#include <string_view>

#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

// The schema is read out of the viewer's own registries, and that has to stay
// true: a hand-written schema rots the first time somebody adds a parameter.
// But a registry knows that `button` takes a `label` and cannot know what a
// button is for, and "what is this tag for" is the first question anybody
// asks of a vocabulary of a hundred and forty.
//
// So: one small file beside the generated schema, carrying only what a person
// knows. A tag nobody has written a sentence for falls back to what it did
// before there were sentences, which is its own name and its class -- nothing
// waits on prose.
//
// It is a skin file in a language directory, so it layers and translates the
// way every other thing anybody reads in this viewer does.
class ALXUINotes
{
public:
    // Read once, the first time anything asks.
    static const ALXUINotes& get();

    // One sentence saying what the tag is for, or empty where nobody has
    // written one.
    const std::string& note(std::string_view tag) const;

    // And the two things about an attribute that no registry knows.
    //
    // Which heading it belongs under: the tool guesses from the name, which
    // is a guess over a vocabulary, and a short list of corrections beats a
    // longer heuristic.
    //
    // And whether it is a name that exists, works, and should not be used.
    // A block that registers two names for one parameter has said that both
    // work and will keep working; which of them a file should write is a
    // judgement, and this is where it is written down. `instead` is the one
    // to write, so the tool can offer to write it.
    struct Attribute
    {
        std::string section;
        bool        deprecated = false;
        std::string instead;
    };

    // For the attribute of a tag, then for the attribute anywhere: a name
    // like `watermark_text` is the same mistake under every tag that takes
    // it, and a correction is written once.
    const Attribute* attribute(std::string_view tag, std::string_view name) const;

    // How many of each there are, for whoever is counting what is left to
    // write.
    size_t count() const { return mNotes.size(); }
    size_t attributeCount() const { return mAttributes.size(); }

private:
    ALXUINotes();

    boost::unordered_flat_map<std::string, std::string, ll::string_hash, std::equal_to<> > mNotes;
    // Keyed by "tag.name" where the line named a tag, and by the bare name
    // where it did not.
    boost::unordered_flat_map<std::string, Attribute, ll::string_hash, std::equal_to<> > mAttributes;
};
