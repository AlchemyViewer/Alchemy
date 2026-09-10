/**
 * @file alxuischema.cpp
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

#include "linden_common.h"

#include "alxuischema.h"

#include "alxuinotes.h"
#include "llinitparam.h"
#include "lluictrlfactory.h"
#include "llxuiparser.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <sstream>

namespace
{
    // How far a nested block's names are followed. A parameter written
    // a.b.c is three levels; past that the forms a file actually uses run
    // out, and the count of attributes stops being worth what it says.
    constexpr S32 MAX_NESTING = 2;

    // What a block reads when a file writes it as one value rather than as
    // an element, or nothing where it reads only its own parts.
    //
    // Two blocks do. One has an unnamed scalar parameter, which is the
    // fallback its own deserialization ends at: that is how text_color=
    // "LtGray" and font="SansSerif" are written where both are blocks. The
    // other declares no parameters at all and reads whatever it is given by
    // hand, which is what LLSD does, and LLSD is the type behind value,
    // initial_value and a callback's parameter.
    std::optional<ALParamType> directValue(const LLInitParam::BlockDescriptor& block)
    {
        for (const LLInitParam::ParamDescriptorPtr descriptor : block.mUnnamedParams)
        {
            const ALParamType* type = ALParamTypes::find(descriptor);
            if (type && type->mKind == ALParamType::SCALAR)
            {
                return *type;
            }
        }

        if (block.mUnnamedParams.empty() && block.namedParams().empty())
        {
            return ALParamType();
        }
        return std::nullopt;
    }

    // typeid spells a type the way a compiler does. This takes off the parts
    // that are the spelling rather than the type, and leaves the rest alone:
    // where it is a mangled name there is nothing to take off, and a mangled
    // name still says more than "value" does.
    std::string readableType(const char* name)
    {
        std::string out(name ? name : "");
        for (const char* prefix : { "class ", "struct ", "enum " })
        {
            if (out.compare(0, std::strlen(prefix), prefix) == 0)
            {
                out.erase(0, std::strlen(prefix));
            }
        }
        for (size_t at = out.find(" __ptr64"); at != std::string::npos; at = out.find(" __ptr64"))
        {
            out.erase(at, 8);
        }
        return out;
    }

    std::string typeWord(const ALParamType& type, const std::vector<std::string>& values)
    {
        if (!values.empty())
        {
            std::string joined;
            for (const std::string& value : values)
            {
                joined += joined.empty() ? "" : "|";
                joined += value;
            }
            return joined;
        }
        switch (type.mValue)
        {
        case ALParamType::BOOLEAN:  return "bool";
        case ALParamType::INTEGER:  return "integer";
        case ALParamType::UNSIGNED: return "unsigned";
        case ALParamType::REAL:     return "real";
        case ALParamType::STRING:   return "string";
        default:                    return readableType(type.mTypeName);
        }
    }

    void addAttribute(ALXUISchema::Tag& tag, std::string name, const ALParamType& type, bool required)
    {
        ALXUISchema::Attribute attribute;
        attribute.name = std::move(name);
        attribute.value = type.mValue;
        attribute.required = required;
        attribute.ignored = type.mKind == ALParamType::IGNORED;
        // A type that takes a name or any string besides names no closed set,
        // so only a C++ enumeration is written as one.
        if (type.mValueNames && type.mValue == ALParamType::OTHER)
        {
            attribute.values = type.mValueNames();
        }
        attribute.type = typeWord(type, attribute.values);
        tag.attributes.push_back(std::move(attribute));
    }

    // The two spellings of a parameter written as an element. Under <panel>,
    // <panel.string> says which block the name belongs to; <string> is read
    // the same way, because a child whose tag is not a widget is taken for a
    // parameter of the element it is under. Both are legal, so both are here.
    void addElements(ALXUISchema::Tag& tag, const std::string& name,
                     const LLInitParam::ParamDescriptor& descriptor, bool top_level)
    {
        if (!top_level)
        {
            return;
        }
        tag.elements.push_back({ tag.name + "." + name, descriptor.mMinCount, descriptor.mMaxCount });
        tag.elements.push_back({ name, descriptor.mMinCount, descriptor.mMaxCount });
    }

    void flatten(const LLInitParam::BlockDescriptor& block,
                 const std::string& prefix,
                 S32 depth,
                 ALXUISchema::Tag& tag)
    {
        for (const auto& named : block.namedParams())
        {
            const std::string name = prefix.empty()
                ? std::string(named.first)
                : prefix + "." + std::string(named.first);
            const ALParamType* type = ALParamTypes::find(named.second);
            if (!type)
            {
                continue;
            }

            switch (type->mKind)
            {
            case ALParamType::SCALAR:
            case ALParamType::IGNORED:
                addAttribute(tag, name, *type, prefix.empty() && named.second->mMinCount > 0);
                break;

            case ALParamType::MULTIPLE_SCALAR:
                addElements(tag, name, *named.second, prefix.empty());
                break;

            case ALParamType::BLOCK:
            case ALParamType::MULTIPLE_BLOCK:
                addElements(tag, name, *named.second, prefix.empty());
                // A block that reads a value written whole takes its own
                // type as an attribute, and its parts are still there
                // beside it: text_color="White" and text_color.red are
                // both legal, and only the first is what files use.
                if (type->mDirectValue)
                {
                    addAttribute(tag, name, *type, false);
                }
                if (type->mBlock)
                {
                    if (!type->mDirectValue)
                    {
                        if (const std::optional<ALParamType> direct = directValue(*type->mBlock))
                        {
                            addAttribute(tag, name, *direct, false);
                        }
                    }
                    if (depth > 0)
                    {
                        flatten(*type->mBlock, name, depth - 1, tag);
                    }
                }
                break;
            }
        }

        // An unnamed block carries its own names at this level, with nothing
        // in front of them: LLView keeps its rect that way, which is why
        // left and width are attributes of every widget there is.
        for (const LLInitParam::ParamDescriptorPtr descriptor : block.mUnnamedParams)
        {
            const ALParamType* type = ALParamTypes::find(descriptor);
            if (type && type->mBlock && depth > 0)
            {
                flatten(*type->mBlock, prefix, depth - 1, tag);
            }
        }
    }

    std::string escaped(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (const char c : text)
        {
            switch (c)
            {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;        break;
            }
        }
        return out;
    }

    const char* xsdType(ALParamType::EValue value)
    {
        switch (value)
        {
        case ALParamType::BOOLEAN:  return "xs:boolean";
        case ALParamType::INTEGER:  return "xs:integer";
        case ALParamType::UNSIGNED: return "xs:nonNegativeInteger";
        case ALParamType::REAL:     return "al_real";
        default:                    return "xs:string";
        }
    }

    // XSD names an element after its tag, and a XUI tag is already a legal
    // one; a parameter element carries a dot, which is legal too.
    std::string typeName(std::string_view tag)
    {
        std::string name(tag);
        std::replace(name.begin(), name.end(), '.', '_');
        return name + "_t";
    }
}

//static
const ALXUISchema& ALXUISchema::get()
{
    static const ALXUISchema sSchema = []
    {
        ALXUISchema schema;
        schema.build();
        return schema;
    }();
    return sSchema;
}

void ALXUISchema::build()
{
    LLWidgetBlockRegistry::instance().forEachItem(
        [this](const std::string& name, empty_param_block_func_t defaults)
    {
        Tag tag;
        tag.name = name;

        // Asking for the block is what builds its table.
        const LLInitParam::BaseBlock& block = (*defaults)();
        const LLInitParam::BlockDescriptor& descriptor = block.mostDerivedBlockDescriptor();

        flatten(descriptor, std::string(), MAX_NESTING, tag);
        tag.text = descriptor.findNamedParam("value") != nullptr;

        if (const widget_registry_t* const* children =
                LLChildRegistryRegistry::instance().getValue(tag.name))
        {
            if (*children)
            {
                (*children)->forEachItem([&tag](const std::string& child, const LLWidgetCreatorFunc&)
                {
                    tag.children.push_back(child);
                });
            }
        }

        // A block that names the same parameter twice -- a synonym, or a
        // derived block shadowing a base's -- reaches here once per name,
        // and a nested block reached down two paths repeats a leaf.
        std::sort(tag.attributes.begin(), tag.attributes.end(),
                  [](const Attribute& a, const Attribute& b) { return a.name < b.name; });
        tag.attributes.erase(std::unique(tag.attributes.begin(), tag.attributes.end(),
                                         [](const Attribute& a, const Attribute& b)
                                         { return a.name == b.name; }),
                             tag.attributes.end());
        std::sort(tag.elements.begin(), tag.elements.end(),
                  [](const Element& a, const Element& b) { return a.name < b.name; });
        tag.elements.erase(std::unique(tag.elements.begin(), tag.elements.end(),
                                       [](const Element& a, const Element& b)
                                       { return a.name == b.name; }),
                           tag.elements.end());
        std::sort(tag.children.begin(), tag.children.end());

        // What each of them carries when the file says nothing. Written from
        // the block the widget is actually built from -- its own template and
        // its base blocks' under that -- under a rule that keeps what holds a
        // value rather than what somebody provided, since nobody provided
        // anything here. The names come back in the same spelling the
        // attributes were flattened into, so they pair by name.
        const auto carried = [&tag](const empty_param_block_func_t* block)
        {
            LLXMLNodePtr written = new LLXMLNode(tag.name.c_str(), false);
            if (block && *block)
            {
                LLXUIParser().writeXUI(written, (**block)(),
                                       ll_make_predicate(LLInitParam::VALID)
                                           && !ll_make_predicate(LLInitParam::EMPTY));
            }
            return written;
        };

        // The two of them: what the widget is built from, and what C++ alone
        // declares. An attribute the first carries is what the element holds
        // when a file says nothing; one the two disagree about is a value
        // somebody chose for this widget rather than where its type starts.
        const LLXMLNodePtr built =
            carried(LLWidgetDefaultsRegistry::instance().getValue(tag.name));
        const LLXMLNodePtr bare =
            carried(LLWidgetBlockRegistry::instance().getValue(tag.name));
        for (Attribute& attribute : tag.attributes)
        {
            if (!built->getAttributeString(attribute.name.c_str(), attribute.held))
            {
                continue;
            }
            attribute.holds = true;
            std::string started;
            attribute.declared = !bare->getAttributeString(attribute.name.c_str(), started)
                              || started != attribute.held;
        }

        // A name that is both a tag and a parameter is read as the parameter,
        // since the parser only takes a child for a widget once the block has
        // refused it. The element form is the permissive one, so it wins.
        tag.children.erase(std::remove_if(tag.children.begin(), tag.children.end(),
                                          [&tag](const std::string& child)
                                          {
                                              return std::any_of(tag.elements.begin(), tag.elements.end(),
                                                                 [&child](const Element& e)
                                                                 { return e.name == child; });
                                          }),
                           tag.children.end());

        mTags.push_back(std::move(tag));
    });

    std::sort(mTags.begin(), mTags.end(), [](const Tag& a, const Tag& b) { return a.name < b.name; });
    for (size_t i = 0; i < mTags.size(); ++i)
    {
        mIndex[mTags[i].name] = i;
    }
}

const ALXUISchema::Tag* ALXUISchema::tag(std::string_view name) const
{
    const auto found = mIndex.find(name);
    return found == mIndex.end() ? nullptr : &mTags[found->second];
}

const ALXUISchema::Attribute* ALXUISchema::attribute(std::string_view name, std::string_view attribute_name) const
{
    const Tag* found = tag(name);
    if (!found)
    {
        return nullptr;
    }
    const auto it = std::lower_bound(found->attributes.begin(), found->attributes.end(), attribute_name,
                                     [](const Attribute& a, std::string_view b) { return a.name < b; });
    return it != found->attributes.end() && it->name == attribute_name ? &*it : nullptr;
}

bool ALXUISchema::accepts(std::string_view name, std::string_view attribute_name) const
{
    return attribute(name, attribute_name) != nullptr;
}

namespace
{
    // Levenshtein over two short names, two rows rather than a matrix. A
    // ceiling as an argument, since every comparison past the best so far is
    // work thrown away: a tag has three hundred attributes and this runs on
    // each of them.
    S32 edit_distance(std::string_view a, std::string_view b, S32 ceiling)
    {
        if ((S32)(a.size() > b.size() ? a.size() - b.size() : b.size() - a.size()) > ceiling)
        {
            return ceiling + 1;
        }
        std::vector<S32> previous(b.size() + 1);
        std::vector<S32> current(b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j)
        {
            previous[j] = (S32)j;
        }
        for (size_t i = 1; i <= a.size(); ++i)
        {
            current[0] = (S32)i;
            S32 best_in_row = current[0];
            for (size_t j = 1; j <= b.size(); ++j)
            {
                const S32 substitute = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
                current[j] = llmin(substitute, llmin(previous[j] + 1, current[j - 1] + 1));
                best_in_row = llmin(best_in_row, current[j]);
            }
            if (best_in_row > ceiling)
            {
                return ceiling + 1;
            }
            previous.swap(current);
        }
        return previous[b.size()];
    }
}

std::string ALXUISchema::nearestSpelling(std::string_view name, std::string_view attribute_name) const
{
    const Tag* found = tag(name);
    if (!found || attribute_name.empty())
    {
        return std::string();
    }

    // A short name has no room to be wrong in: two edits over four
    // characters is a different word, not a slip. One edit per four
    // characters, and never more than two, which is where a suggestion
    // stops being one.
    const S32 ceiling = llmin(2, (S32)(attribute_name.size() / 4));
    if (ceiling < 1)
    {
        return std::string();
    }

    S32 best = ceiling + 1;
    const Attribute* nearest = nullptr;
    bool tied = false;
    for (const Attribute& candidate : found->attributes)
    {
        // A name a file is told not to write is not a name to send it to.
        if (candidate.ignored)
        {
            continue;
        }
        const S32 distance = edit_distance(attribute_name, candidate.name, best);
        if (distance < best)
        {
            best = distance;
            nearest = &candidate;
            tied = false;
        }
        else if (distance == best && nearest && candidate.name != nearest->name)
        {
            tied = true;
        }
    }
    return nearest && !tied ? nearest->name : std::string();
}

bool ALXUISchema::acceptsChild(std::string_view name, std::string_view child) const
{
    const Tag* container = tag(name);
    if (!container)
    {
        return false;
    }
    return std::binary_search(container->children.begin(), container->children.end(), child,
                              [](std::string_view a, std::string_view b) { return a < b; });
}

std::string ALXUISchema::asXSD() const
{
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<!--\n"
        << "  The widget vocabulary of XUI, written out of the viewer's own\n"
        << "  registries by XUI Studio. Do not edit: regenerate it.\n"
        << "\n"
        << "  It is permissive on purpose. A parameter may be written as an\n"
        << "  attribute or as a nested element, a value may be a name or the\n"
        << "  thing it names, and a colour, image, font or setting is a string\n"
        << "  whose vocabulary lives in another file. Nothing here can say\n"
        << "  \"one of these two spellings\", so nothing here tries; the tool's\n"
        << "  lint is where those are checked.\n"
        << "\n"
        << "  Nothing is required, not even a parameter the block declares\n"
        << "  Mandatory: a widget's template in widgets/ supplies values the\n"
        << "  file then leaves out, and no schema can see that layer.\n"
        << "-->\n"
        << "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\""
        << " elementFormDefault=\"qualified\">\n"
        << "\n  <!-- A parameter element: its own leaves are the attributes of\n"
        << "       the tag that owns it, written with dots, so there is nothing\n"
        << "       left here to check. -->\n"
        << "\n  <!-- A number as XUI writes one, which is as C writes one: the\n"
        << "       literals carry a float suffix, bar_max=\"100.f\". -->\n"
        << "  <xs:simpleType name=\"al_real\">\n"
        << "    <xs:restriction base=\"xs:string\">\n"
        << "      <xs:pattern value=\"[+-]?([0-9]+(\\.[0-9]*)?|\\.[0-9]+)([eE][+-]?[0-9]+)?[fF]?\"/>\n"
        << "    </xs:restriction>\n"
        << "  </xs:simpleType>\n"
        << "  <xs:complexType name=\"al_any\" mixed=\"true\">\n"
        << "    <xs:sequence>\n"
        << "      <xs:any minOccurs=\"0\" maxOccurs=\"unbounded\" processContents=\"skip\"/>\n"
        << "    </xs:sequence>\n"
        << "    <xs:anyAttribute processContents=\"skip\"/>\n"
        << "  </xs:complexType>\n";

    // Each tag, carrying what a person wrote about it where there is one: a
    // schema that says what a tag is for is worth more to whoever opens it
    // in an editor than one that only says what it takes.
    for (const Tag& tag : mTags)
    {
        const std::string& note = ALXUINotes::get().note(tag.name);
        out << "\n  <xs:element name=\"" << escaped(tag.name)
            << "\" type=\"" << typeName(tag.name) << "\"";
        if (note.empty())
        {
            out << "/>\n";
            continue;
        }
        out << ">\n"
            << "    <xs:annotation>\n"
            << "      <xs:documentation>" << escaped(note) << "</xs:documentation>\n"
            << "    </xs:annotation>\n"
            << "  </xs:element>\n";
    }

    for (const Tag& tag : mTags)
    {
        out << "\n  <xs:complexType name=\"" << typeName(tag.name) << "\"";
        if (tag.text)
        {
            out << " mixed=\"true\"";
        }
        out << ">\n";

        if (!tag.children.empty() || !tag.elements.empty())
        {
            out << "    <xs:choice minOccurs=\"0\" maxOccurs=\"unbounded\">\n";
            for (const std::string& child : tag.children)
            {
                if (mIndex.find(child) != mIndex.end())
                {
                    out << "      <xs:element ref=\"" << escaped(child) << "\"/>\n";
                }
            }
            for (const Element& element : tag.elements)
            {
                out << "      <xs:element name=\"" << escaped(element.name)
                    << "\" type=\"al_any\"/>\n";
            }
            out << "    </xs:choice>\n";
        }

        for (const Attribute& attribute : tag.attributes)
        {
            out << "    <xs:attribute name=\"" << escaped(attribute.name) << "\"";
            if (attribute.values.empty())
            {
                out << " type=\"" << xsdType(attribute.value) << "\"/>\n";
            }
            else
            {
                out << ">\n      <xs:simpleType>\n"
                    << "        <xs:restriction base=\"xs:string\">\n";
                for (const std::string& value : attribute.values)
                {
                    out << "          <xs:enumeration value=\"" << escaped(value) << "\"/>\n";
                }
                out << "        </xs:restriction>\n      </xs:simpleType>\n"
                    << "    </xs:attribute>\n";
            }
        }

        out << "  </xs:complexType>\n";
    }

    out << "</xs:schema>\n";
    return out.str();
}

std::string ALXUISchema::summary() const
{
    size_t attributes = 0;
    size_t elements = 0;
    for (const Tag& tag : mTags)
    {
        attributes += tag.attributes.size();
        elements += tag.elements.size();
    }

    std::ostringstream out;
    out << mTags.size() << " tags, " << attributes << " attributes, "
        << elements << " parameter elements";
    return out.str();
}
