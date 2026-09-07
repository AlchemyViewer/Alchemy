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

#include <string>
#include <unordered_map>
#include <vector>

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
    };

    struct Drop
    {
        S32         layer = 0;
        S32         line = 0;
        std::string path;       // the overlay element's name path
        std::string what;       // the attribute or element
        std::string why;
    };

    void clear();

    // The files, by layer index; the base is 0.
    const std::vector<std::string>& layers() const { return mLayers; }
    const std::string& layerPath(S32 layer) const;

    // The layer and line that last wrote an attribute node's value or an
    // element's text; null when the base did.
    const Origin* originOf(const LLXMLNode* node) const;

    const std::vector<Drop>& drops() const { return mDrops; }

    // ALXmlMergeObserver
    void layerParsed(S32 layer, const std::string& path) override;
    void layerSkipped(S32 layer, const std::string& path, const std::string& reason, S32 line) override;
    void rootNameDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void rootTagDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void childUnmatched(S32 layer, LLXMLNode* base_parent, LLXMLNode* overlay, Miss why) override;
    void textApplied(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void textKept(S32 layer, LLXMLNode* base, LLXMLNode* overlay) override;
    void valueAppliedAsText(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute) override;
    void attributeApplied(S32 layer, LLXMLNode* base_attribute, LLXMLNode* overlay_attribute) override;
    void attributeDropped(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute) override;

    // The names from the root's child down to an element, joined with '/'.
    static std::string namePath(const LLXMLNode* node);

private:
    void drop(S32 layer, const LLXMLNode* overlay_node, std::string what, std::string why);

    std::vector<std::string>                            mLayers;
    std::unordered_map<const LLXMLNode*, Origin>        mOrigins;
    std::vector<Drop>                                   mDrops;
};
