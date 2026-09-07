/**
 * @file alxmllayermerge.h
 * @brief The one merge of a localized or skinned XML layer over its base, observed.
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

#include "llxmlnode.h"

#include <string>
#include <vector>

// Every decision the merge makes, told to whoever asked. The viewer asks
// nothing and pays a pointer test per decision; a tool records where each
// value came from; a check counts what was dropped. There is no second
// copy of the rules: an observer sees the behaviour because it is the
// behaviour.
class ALXmlMergeObserver
{
public:
    // Why an overlay child applied to nothing.
    enum class Miss : U8
    {
        Unnamed,        // no name and no value to match by
        NoSibling       // no child of the base element carries the name
    };

    virtual ~ALXmlMergeObserver() = default;

    // A layer file: its index among the paths, from 1 for the first
    // overlay. The base is layer 0.
    virtual void layerParsed(S32 layer, const std::string& path) {}
    virtual void layerSkipped(S32 layer, const std::string& path, const std::string& reason, S32 line) {}

    // The overlay's root against the base's: always merged, since the
    // file name binds the two, with a word when the name or the tag
    // differs from the base's.
    virtual void rootMatched(S32 layer, LLXMLNode* base, LLXMLNode* overlay) {}
    virtual void rootNameDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) {}
    virtual void rootTagDiffers(S32 layer, LLXMLNode* base, LLXMLNode* overlay) {}

    // An overlay child against the base element's children.
    virtual void childMatched(S32 layer, LLXMLNode* base, LLXMLNode* overlay) {}
    virtual void childUnmatched(S32 layer, LLXMLNode* base_parent, LLXMLNode* overlay, Miss why) {}

    // Text: applied from the overlay; kept from the base when the overlay
    // has none; or applied from an overlay's value attribute where the
    // base carries its text in the body.
    virtual void textApplied(S32 layer, LLXMLNode* base, LLXMLNode* overlay) {}
    virtual void textKept(S32 layer, LLXMLNode* base, LLXMLNode* overlay) {}
    virtual void valueAppliedAsText(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute) {}

    // An attribute: applied over the base's, or dropped because the base
    // element has no attribute of that name.
    virtual void attributeApplied(S32 layer, LLXMLNode* base_attribute, LLXMLNode* overlay_attribute) {}
    virtual void attributeDropped(S32 layer, LLXMLNode* base, LLXMLNode* overlay_attribute) {}
};

namespace ALXmlLayerMerge
{
    // The first path is parsed as the base and each later one is parsed
    // and merged over it in turn, as layers 1, 2, ... A path that is empty
    // or the same as the first is passed over. Every layer is merged: the
    // file name binds it to the base, and a root name or tag that differs
    // is a change the layer never heard of, not another file. False when
    // the base does not parse, or a later layer does not.
    bool load(const std::vector<std::string>& paths, LLXMLNodePtr& root, ALXmlMergeObserver* observer = nullptr);

    // One overlay element over one base element, and their subtrees. The
    // rules: children match by name, or by value when they have no name,
    // and never by tag; the search for a match resumes after the previous
    // one and wraps once; on a match, text the overlay has replaces the
    // base's and text it lacks leaves the base's alone; a value attribute
    // where the base carries its text in the body is that text; every
    // other attribute present in both is overwritten, except the name,
    // which is the key and is never written; an attribute the base lacks
    // is dropped; a child that matches nothing is dropped.
    void merge(LLXMLNodePtr& base, LLXMLNodePtr& overlay, S32 layer = 1, ALXmlMergeObserver* observer = nullptr);
}
