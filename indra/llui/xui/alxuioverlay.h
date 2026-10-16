/**
 * @file alxuioverlay.h
 * @brief Which layer and line wrote each value of a merged XUI tree, and what applied to nothing.
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

#include "alxmllayermerge.h"

#include "llstring.h"

#include <string>
#include <vector>

#include <boost/unordered_map.hpp>

// The tool's observer of the viewer's own merge. It records, per
// attribute node and per element's text, the layer and the line in that
// layer's file that wrote it, keyed by the merged node's address and valid
// while the tool holds the tree; and it keeps every decision that applied
// nothing, with the file, line and reason, for the diagnostics pane.
// Nothing is added to the node: origin is the tool's to remember.
class ALXUIOverlay final : public ALXmlMergeObserver
{
public:
    struct Origin
    {
        S32 layer = 0;
        S32 line = 0;
        // What that layer wrote, where there is a value to keep: an
        // attribute has one, an element's text does not come through here
        // as a string.
        std::string value;
    };

    struct Drop
    {
        S32         layer = 0;
        S32         line = 0;
        std::string path;       // the overlay element's name path
        std::string what;       // the attribute or element, where it has a name
        // Why, as the name of a sentence in the lint's words and the names
        // that sentence is about: what a person reads is said by whoever is
        // showing it, in their language.
        std::string key;
        LLStringUtil::format_map_t args;
    };

    // An element the layer wrote at a path the base no longer has, applied
    // to the one element below that path carrying its name: the base moved
    // it, and the layer's author could not have known.
    struct Rescue
    {
        S32         layer = 0;
        S32         line = 0;
        std::string from;
        std::string to;
    };

    void clear();

    // The files, by layer index; the base is 0.
    const std::vector<std::string>& layers() const { return mLayers; }
    const std::string& layerPath(S32 layer) const;

    // The layer and line that last wrote an attribute node's value or an
    // element's text; null when the base did.
    const Origin* originOf(const LLXMLNode* node) const;

    // Every layer that wrote it, in the order they were applied, so the
    // last of them is the one in force. The base is not among them: a layer
    // may only write over an attribute the base already has, so an empty
    // list is a value nobody has overridden and a list of one is a value
    // one skin or one language disagrees about.
    const std::vector<Origin>& writersOf(const LLXMLNode* node) const;

    const std::vector<Drop>& drops() const { return mDrops; }
    const std::vector<Rescue>& rescues() const { return mRescues; }

    // ALXmlMergeObserver
    void layerParsed(S32 layer, const std::string& path) override;
    void layerSkipped(S32 layer, const std::string& path, const std::string& reason, S32 line) override;
    void rootNameDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void rootTagDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void childRescued(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void childUnmatched(S32 layer, LLXMLNode* base_parent, LLXMLNode* overlay, Miss why) override;
    void textApplied(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void textKept(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void valueAppliedAsText(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute) override;
    void attributeApplied(S32 layer, LLXMLNode* base_attribute, LLXMLNode* overlay_attribute) override;
    void attributeDropped(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute) override;

    // The names from the root's child down to an element, joined with '/'.
    static std::string namePath(const LLXMLNode* node);

private:
    void drop(S32 layer, const LLXMLNode* overlay_node, std::string what, const char* key,
              LLStringUtil::format_map_t args = LLStringUtil::format_map_t());

    std::vector<std::string>                                    mLayers;
    boost::unordered_map<const LLXMLNode*, std::vector<Origin>> mOrigins;
    std::vector<Drop>                               mDrops;
    std::vector<Rescue>                             mRescues;
};
