/**
 * @file alxuiedit.h
 * @brief One XUI file, edited as the bytes it is.
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

// A XUI file under edit. An operation finds the span of the attribute it
// changes in the file's own text and splices it, so every byte the
// operation does not name is the byte that was already there: comments,
// indentation, attribute order, entity spellings, the declaration and the
// line endings the file was written with. Re-serializing the tree cannot
// promise that, and a file under version control is read as its diff.
//
// The text is reparsed after every operation, so an element is named by
// its name path rather than held as a node.
class ALXUIEdit
{
public:
    using path_t = std::vector<std::string>;

    ALXUIEdit();
    ~ALXUIEdit();

    ALXUIEdit(const ALXUIEdit&) = delete;
    ALXUIEdit& operator=(const ALXUIEdit&) = delete;

    bool loadFile(const std::string& path);
    bool loadBuffer(std::string_view text);

    // Writes the text as it stands, byte for byte, to the file it came
    // from or to another.
    bool save();
    bool saveAs(const std::string& path);

    const std::string& text() const { return mText; }
    const std::string& path() const { return mPath; }
    const std::string& error() const { return mError; }
    bool dirty() const { return mDirty; }

    pugi::xml_node root() const;
    pugi::xml_node resolve(const path_t& path) const;

    // The two operations. A value is written escaped and an attribute the
    // element does not carry is added after the last one it does, spaced
    // the way that one is spaced.
    bool setAttribute(const path_t& path, const std::string& name, const std::string& value);
    bool removeAttribute(const path_t& path, const std::string& name);

    // An attribute's value as the file writes it, entity spellings and
    // all, which is not always what the parser read it as. False when the
    // element does not carry the attribute.
    bool valueText(pugi::xml_node node, std::string_view name, std::string& out) const;

    // What the element occupies now, in the terms the file would have to
    // use to put it there: read only when the attribute an edit needs is
    // not in the file at all, and one has to be written.
    struct Anchor
    {
        S32     left = 0;
        S32     top = 0;        // down from the parent's top, as topleft layout counts
        S32     bottom = 0;     // up from the parent's bottom
        S32     width = 0;
        S32     height = 0;
        bool    topLeft = true; // the layout the element is laid out under
    };

    // Move the element by a delta in view coordinates, x to the right and
    // y up, and resize it by a delta on each axis. The attribute that
    // moves is whichever one the author used -- the rule of
    // LLView::applyXUILayout read backwards -- and no form is ever
    // converted into another.
    bool translate(const path_t& path, S32 dx, S32 dy, const Anchor& now);
    bool resize(const path_t& path, S32 dw, S32 dh, const Anchor& now);

    // The attributes the last translate or resize wrote, for the panel
    // that says what an edit is about to do.
    const std::vector<std::string>& lastWritten() const { return mWritten; }

    // Which of these an element carries decides what a move writes.
    static bool isGeometryAttribute(std::string_view name);

private:
    struct Span
    {
        size_t offset = 0;
        size_t length = 0;
    };

    bool parse();
    void splice(const Span& span, std::string_view text);

    // The spans of one attribute of an element: the value between its
    // quotes, and the attribute with the whitespace that precedes it.
    bool spanOf(pugi::xml_node node, std::string_view name, Span& value, Span& whole) const;

    // Where an attribute the element does not carry would go, and the
    // whitespace that separates the one before it.
    bool insertionPoint(pugi::xml_node node, size_t& offset, std::string& separator) const;

    bool addDelta(pugi::xml_node node, std::string_view name, S32 delta, std::vector<std::pair<std::string, S32>>& writes);

    std::string                     mPath;
    std::string                     mText;
    std::string                     mError;
    std::unique_ptr<ALXmlDocument>  mDoc;
    std::vector<std::string>        mWritten;
    bool                            mDirty = false;
};
