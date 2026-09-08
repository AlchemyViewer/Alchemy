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
        Duplicate,      // every base element of that name here is already matched
        NotBelow,       // nothing of that name anywhere below the base element
        Ambiguous       // several below the base element, so which one is a guess
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

    // An overlay child against the base element's children; or, when none
    // of them carries the name, against the one element below the base
    // element that does, which is where the base moved it.
    virtual void childMatched(S32 layer, LLXMLNode* base, LLXMLNode* overlay) {}
    virtual void childRescued(S32 layer, LLXMLNode* base, LLXMLNode* overlay) {}
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
    // The observer a load uses when its caller passes none, which is what
    // the viewer does everywhere. Null until something installs one.
    void setDefaultObserver(ALXmlMergeObserver* observer);
    ALXmlMergeObserver* defaultObserver();

    // The first path is parsed as the base and each later one is parsed
    // and merged over it in turn, as layers 1, 2, ... A path that is empty
    // or the same as the first is passed over, and so is a layer that
    // does not parse. Every other layer is merged: the file name binds it
    // to the base, and a root name or tag that differs is a change the
    // layer never heard of, not another file. False when the base does
    // not parse.
    bool load(const std::vector<std::string>& paths, LLXMLNodePtr& root, ALXmlMergeObserver* observer = nullptr);

    // A layer as the caller has it rather than as the disk has it: an
    // editor previews what it holds, which is not what was last written.
    // The path still names the layer, so an observer reports it the way
    // it reports any other.
    struct Source
    {
        std::string         path;
        const std::string*  text = nullptr;     // null: read the file
    };

    bool loadSources(const std::vector<Source>& sources, LLXMLNodePtr& root, ALXmlMergeObserver* observer = nullptr);

    // One overlay element over one base element, and their subtrees. The
    // rules: children match by name, or by value when they have no name,
    // and never by tag; a child matches the first base child of its name
    // in document order that no earlier child took, and when no child of
    // the base element carries the name, the one element below it that
    // does; on a match, text the overlay has replaces the
    // base's and text it lacks leaves the base's alone; a value attribute
    // where the base carries its text in the body is that text; every
    // other attribute present in both is overwritten, except the name,
    // which is the key and is never written; an attribute the base lacks
    // is dropped; a child that matches nothing is dropped.
    void merge(LLXMLNodePtr& base, LLXMLNodePtr& overlay, S32 layer = 1, ALXmlMergeObserver* observer = nullptr);
}
