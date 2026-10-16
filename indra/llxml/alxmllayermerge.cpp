/**
 * @file alxmllayermerge.cpp
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

#include "linden_common.h"

#include "alxmllayermerge.h"

#include "alxmldocument.h"

#include <algorithm>
#include <vector>

#include <boost/unordered_set.hpp>

namespace
{
    // The name a child is matched by: its name attribute, or its value
    // attribute when it has no name, as a combo box's items do.
    std::string matchKey(const LLXMLNode* node)
    {
        std::string key;
        if (!node->getAttributeString("name", key) || key.empty())
        {
            node->getAttributeString("value", key);
        }
        return key;
    }

    bool hasText(const LLXMLNode* node)
    {
        return node->getValue().find_first_not_of(" \t\r\n") != std::string::npos;
    }

    // The base elements a layer has already applied to, for the whole of
    // that layer rather than one element's children: an element rescued
    // from below must not be one a later child would match in its place.
    using claimed_t = boost::unordered_set<const LLXMLNode*>;

    // The unclaimed elements of that name below a base element, at any
    // depth, up to two: one is where the base moved it, and more than one
    // is a guess the merge does not make.
    void findBelow(const LLXMLNode* base, const std::string& key, const claimed_t& claimed,
                   std::vector<LLXMLNodePtr>& found)
    {
        for (LLXMLNodePtr child = base->getFirstChild(); child.notNull() && found.size() < 2;
             child = child->getNextSibling())
        {
            if (matchKey(child) == key && !claimed.count(child.get()))
            {
                found.push_back(child);
            }
            findBelow(child, key, claimed, found);
        }
    }

    // Why a layer did not parse, for whoever is listening; the parse that
    // failed only logged it. Parsed again from where the layer came from,
    // since a text the caller holds is not what is on the disk under the
    // same name.
    void reportSkipped(ALXmlMergeObserver* observer, S32 layer, const ALXmlLayerMerge::Source& source)
    {
        if (!observer)
        {
            return;
        }
        ALXmlDocument doc;
        if (source.text)
        {
            doc.loadBuffer(source.text->data(), source.text->size());
        }
        else
        {
            doc.loadFile(source.path);
        }
        observer->layerSkipped(layer, source.path, doc.errorDescription(), doc.errorLine());
    }

    // One layer's pass over the base: the elements it has applied to, and
    // the children it could not match where they were written, held back
    // until every child written where the base has it has been matched.
    struct Pass
    {
        struct Deferred
        {
            LLXMLNodePtr base;
            LLXMLNodePtr overlay;
        };
        claimed_t               claimed;
        std::vector<Deferred>   later;
    };
}

namespace
{

void mergeInto(LLXMLNodePtr& base, LLXMLNodePtr& overlay, S32 layer, ALXmlMergeObserver* observer,
               Pass& pass)
{
    if (base.isNull() || overlay.isNull())
    {
        LL_WARNS() << "Node invalid" << LL_ENDL;
        return;
    }

    // The text. What the overlay has in its body replaces the base's. A
    // value attribute against a base that carries its text in the body is
    // that text: the two forms say the same thing, and a translator writes
    // whichever the file had when the translation was made. Text the
    // overlay lacks in either form leaves the base's alone, since an
    // element written for its attributes or its children says nothing
    // about the text.
    static const LLStringTableEntry* value_entry = gStringTable.addStringEntry("value");
    LLXMLNodePtr overlay_value;
    overlay->getAttribute(value_entry, overlay_value, false);
    const bool text_in_body = hasText(base) && !base->mAttributes.count(value_entry);
    const bool value_as_text = !hasText(overlay) && text_in_body && overlay_value.notNull() && hasText(overlay_value);
    if (hasText(overlay))
    {
        if (observer)
        {
            observer->textApplied(layer, base, overlay);
        }
        base->setValue(overlay->getValue());
    }
    else if (value_as_text)
    {
        if (observer)
        {
            observer->valueAppliedAsText(layer, base, overlay_value);
        }
        base->setValue(overlay_value->getValue());
    }
    else if (hasText(base) && observer)
    {
        observer->textKept(layer, base, overlay);
    }

    // Every attribute the base has too, except the name: it is the key
    // the element was matched by, equal by construction below the root,
    // and at the root a rename the layer never heard of must not travel
    // into the tree, where the name is what the viewer knows it by.
    static const LLStringTableEntry* name_entry = gStringTable.addStringEntry("name");
    for (auto& [name, overlay_attribute] : overlay->mAttributes)
    {
        if (name == name_entry)
        {
            continue;
        }
        LLXMLNodePtr base_attribute;
        base->getAttribute(name, base_attribute, false);
        if (base_attribute)
        {
            base_attribute->setValue(overlay_attribute->getValue());
            if (observer)
            {
                observer->attributeApplied(layer, base_attribute, overlay_attribute);
            }
        }
        else if (observer && !(value_as_text && name == value_entry))
        {
            observer->attributeDropped(layer, base, overlay_attribute);
        }
    }

    // Each overlay child against the first base child of that name, in
    // document order, that no earlier overlay child took: a repeated name
    // matches in order, and an element matched once is not matched again.
    for (LLXMLNodePtr overlay_child = overlay->getFirstChild(); overlay_child.notNull();
         overlay_child = overlay_child->getNextSibling())
    {
        const std::string overlay_key = matchKey(overlay_child);
        if (overlay_key.empty())
        {
            if (observer)
            {
                observer->childUnmatched(layer, base, overlay_child, ALXmlMergeObserver::Miss::Unnamed);
            }
            continue;
        }

        LLXMLNodePtr match;
        bool any_of_that_name = false;
        for (LLXMLNodePtr child = base->getFirstChild(); child.notNull(); child = child->getNextSibling())
        {
            if (matchKey(child) == overlay_key)
            {
                any_of_that_name = true;
                if (!pass.claimed.count(child.get()))
                {
                    match = child;
                    break;
                }
            }
        }

        if (match.notNull())
        {
            pass.claimed.insert(match.get());
            if (observer)
            {
                observer->childMatched(layer, match, overlay_child);
            }
            mergeInto(match, overlay_child, layer, observer, pass);
            continue;
        }

        // Every element of that name here is taken, so this one is a
        // second translation of the first: it applies to nothing.
        if (any_of_that_name)
        {
            if (observer)
            {
                observer->childUnmatched(layer, base, overlay_child, ALXmlMergeObserver::Miss::Duplicate);
            }
            continue;
        }

        // No child of the base element carries the name. The base may
        // have moved the element deeper, into a layout panel or an
        // accordion it grew after the layer was written; the layer's
        // author cannot know that, and the name is what binds them. Where
        // it went is looked for once the rest of the layer has matched,
        // so that a copy left at the old path never takes the element
        // from the copy written at the new one.
        pass.later.push_back({ base, overlay_child });
    }
}

// The children held back: one element of the name below where each was
// written is where the base moved it; several is a guess. A rescued
// subtree is merged like any other, and what it cannot match where it
// was written joins the end of the same line.
void rescueDeferred(S32 layer, ALXmlMergeObserver* observer, Pass& pass)
{
    for (size_t i = 0; i < pass.later.size(); ++i)
    {
        LLXMLNodePtr base = pass.later[i].base;
        LLXMLNodePtr overlay_child = pass.later[i].overlay;
        std::vector<LLXMLNodePtr> below;
        findBelow(base.get(), matchKey(overlay_child), pass.claimed, below);
        if (below.size() == 1)
        {
            pass.claimed.insert(below.front().get());
            if (observer)
            {
                observer->childRescued(layer, below.front(), overlay_child);
            }
            mergeInto(below.front(), overlay_child, layer, observer, pass);
        }
        else if (observer)
        {
            observer->childUnmatched(layer, base, overlay_child,
                                     below.empty() ? ALXmlMergeObserver::Miss::NotBelow
                                                   : ALXmlMergeObserver::Miss::Ambiguous);
        }
    }
}

} // namespace

namespace
{
    ALXmlMergeObserver* sDefaultObserver = nullptr;
}

std::string ALXmlLayerMerge::namePath(const LLXMLNode* node)
{
    std::vector<std::string> names;
    for (const LLXMLNode* cur = node; cur && cur->mParent; cur = cur->mParent)
    {
        std::string name = matchKey(cur);
        if (name.empty())
        {
            name = std::string("<") + cur->getName()->mString + ">";
        }
        names.push_back(std::move(name));
    }
    std::string path;
    for (auto it = names.rbegin(); it != names.rend(); ++it)
    {
        path += (path.empty() ? "" : "/") + *it;
    }
    return path;
}

void ALXmlLayerMerge::setDefaultObserver(ALXmlMergeObserver* observer)
{
    sDefaultObserver = observer;
}

ALXmlMergeObserver* ALXmlLayerMerge::defaultObserver()
{
    return sDefaultObserver;
}

void ALXmlLayerMerge::merge(LLXMLNodePtr& base, LLXMLNodePtr& overlay, S32 layer, ALXmlMergeObserver* observer)
{
    Pass pass;
    mergeInto(base, overlay, layer, observer, pass);
    rescueDeferred(layer, observer, pass);
}

bool ALXmlLayerMerge::load(const std::vector<std::string>& paths, LLXMLNodePtr& root, ALXmlMergeObserver* observer)
{
    std::vector<Source> sources;
    sources.reserve(paths.size());
    for (const std::string& path : paths)
    {
        sources.push_back({ path, nullptr });
    }
    return loadSources(sources, root, observer);
}

namespace
{
    // A layer from where the caller keeps it. Parsing a buffer and parsing
    // a file are the same parse; only where the bytes come from differs.
    bool parseSource(const ALXmlLayerMerge::Source& source, LLXMLNodePtr& node)
    {
        return source.text
            ? LLXMLNode::parseBuffer(source.text->data(), source.text->size(), node)
            : LLXMLNode::parseFile(source.path, node, nullptr);
    }
}

bool ALXmlLayerMerge::loadSources(const std::vector<Source>& paths, LLXMLNodePtr& root, ALXmlMergeObserver* observer)
{
    if (paths.empty())
    {
        return false;
    }

    if (!observer)
    {
        observer = sDefaultObserver;
    }

    const std::string& base_path = paths.front().path;
    if (base_path.empty())
    {
        return false;
    }

    if (!parseSource(paths.front(), root))
    {
        LL_WARNS() << "Problem reading UI description file: " << base_path << LL_ENDL;
        reportSkipped(observer, 0, paths.front());
        return false;
    }
    if (observer)
    {
        observer->layerParsed(0, base_path);
    }

    S32 layer = 0;
    for (size_t i = 1; i < paths.size(); ++i)
    {
        const std::string& layer_path = paths[i].path;
        ++layer;
        if (layer_path.empty() || layer_path == base_path)
        {
            // no localized version of this file, that's ok, keep looking
            continue;
        }

        // A layer that does not parse is passed over: a syntax error in
        // one language's file leaves that file untranslated, not the
        // floater unbuildable for everyone who speaks the language.
        LLXMLNodePtr overlay;
        if (!parseSource(paths[i], overlay))
        {
            LL_WARNS() << "Problem reading localized UI description file: " << layer_path << ", skipping it" << LL_ENDL;
            reportSkipped(observer, layer, paths[i]);
            continue;
        }
        if (observer)
        {
            observer->layerParsed(layer, layer_path);
        }

        // The file name binds the layer to the base. A root name or tag
        // that differs is a change the layer never heard of, a rename or a
        // widget that became another, and the children still match by
        // their own names.
        if (observer)
        {
            std::string base_name;
            std::string overlay_name;
            overlay->getAttributeString("name", overlay_name);
            root->getAttributeString("name", base_name);
            if (overlay_name != base_name)
            {
                observer->rootNameDiffers(layer, root, overlay);
            }
            if (overlay->getName() != root->getName())
            {
                observer->rootTagDiffers(layer, root, overlay);
            }
            observer->rootMatched(layer, root, overlay);
        }
        merge(root, overlay, layer, observer);
    }

    return true;
}
