/**
 * @file alxuinotes.cpp
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

#include "linden_common.h"

#include "alxuinotes.h"

#include "lluictrlfactory.h"
#include "llxmlnode.h"

namespace
{
    const char* const NOTES_FILE = "xui_notes.xml";
}

// static
const ALXUINotes& ALXUINotes::get()
{
    static const ALXUINotes held;
    return held;
}

// Read the way the tool reads everything else in a skin: the base language's
// file with the chosen one layered over it, so a translator writes the same
// tags with the same names and the merge does the rest.
ALXUINotes::ALXUINotes()
{
    LLXMLNodePtr root;
    if (!LLUICtrlFactory::getLayeredXMLNode(NOTES_FILE, root) || root.isNull())
    {
        return;
    }
    for (LLXMLNodePtr child = root->getFirstChild(); child.notNull(); child = child->getNextSibling())
    {
        std::string name;
        if (!child->getAttributeString("name", name) || name.empty())
        {
            continue;
        }
        if (child->hasName("tag"))
        {
            std::string note;
            if (child->getAttributeString("note", note) && !note.empty())
            {
                mNotes.emplace(std::move(name), std::move(note));
            }
            continue;
        }
        if (!child->hasName("attribute"))
        {
            continue;
        }

        Attribute said;
        child->getAttributeString("section", said.section);
        child->getAttributeString("instead", said.instead);
        child->getAttributeBOOL("deprecated", said.deprecated);
        // A name to write instead is a deprecation whether or not the line
        // says so twice.
        said.deprecated = said.deprecated || !said.instead.empty();
        if (said.section.empty() && !said.deprecated)
        {
            continue;   // a line that says nothing
        }
        // Under one tag where the line names one, everywhere where it does
        // not: `watermark_text` is the same mistake under every tag that
        // takes it, and a correction is written once.
        std::string tag;
        const std::string key = child->getAttributeString("tag", tag) && !tag.empty()
                              ? tag + "." + name : name;
        mAttributes.emplace(key, std::move(said));
    }
}

const ALXUINotes::Attribute* ALXUINotes::attribute(std::string_view tag, std::string_view name) const
{
    std::string keyed;
    keyed.reserve(tag.size() + 1 + name.size());
    keyed.assign(tag).append(1, '.').append(name);
    if (const auto found = mAttributes.find(keyed); found != mAttributes.end())
    {
        return &found->second;
    }
    const auto anywhere = mAttributes.find(name);
    return anywhere == mAttributes.end() ? nullptr : &anywhere->second;
}

const std::string& ALXUINotes::note(std::string_view tag) const
{
    const auto found = mNotes.find(tag);
    return found == mNotes.end() ? LLStringUtil::null : found->second;
}
