/**
 * @file alxuiservice.cpp
 * @brief The XUI language service.
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

#include "alxuiservice.h"

#include "alxuischema.h"

#include <pugixml.hpp>

#include <algorithm>

namespace
{
    bool nameByte(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-' || c == ':'; }
    bool blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

    // The text from the start of the document to a position, which is all
    // the context there is.
    std::string textBefore(const ALTextDocument& doc, const ALTextPos& at)
    {
        return doc.text(ALTextRange(doc.start(), doc.clamp(at)));
    }

    // The bytes of the text up to `end`, with what line and column the
    // byte at an offset is on.
    ALTextPos posOfOffset(std::string_view text, size_t offset)
    {
        ALTextPos p;
        size_t    line_start = 0;
        for (size_t i = 0; i < offset && i < text.size(); ++i)
        {
            if (text[i] == '\n')
            {
                ++p.line;
                line_start = i + 1;
            }
        }
        p.column = static_cast<S32>(std::min(offset, text.size()) - line_start);
        return p;
    }

    bool matchesPrefix(std::string_view word, std::string_view prefix)
    {
        if (prefix.size() > word.size())
        {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); ++i)
        {
            if (LLStringOps::toLower(word[i]) != LLStringOps::toLower(prefix[i]))
            {
                return false;
            }
        }
        return true;
    }
}

ALXUIService::Context ALXUIService::contextAt(const ALTextDocument& doc, const ALTextPos& at)
{
    Context           context;
    const std::string text = textBefore(doc, at);
    // Read from the top, keeping what is open: a comment or an
    // instruction until it closes, a tag until its `>`, an element until
    // its closing tag or its `/>`.
    enum class In : U8
    {
        Text,
        Comment,
        Instruction,
        CData,
        Doctype,
        Tag
    };
    In     in       = In::Text;
    size_t tag_at   = 0;     // where the current tag's `<` is
    bool   closing  = false; // the current tag is `</...`
    char   quote    = 0;     // inside an attribute's value, which quote
    std::string tag_name;
    std::vector<std::string> present;
    // Where each attribute carried starts, so that one still being typed
    // at the end is told from one carried.
    std::vector<size_t>      present_at;
    std::vector<std::string> parents;
    size_t i = 0;
    auto   readName = [&](size_t from) {
        size_t end = from;
        while (end < text.size() && nameByte(text[end]))
        {
            ++end;
        }
        return text.substr(from, end - from);
    };
    while (i < text.size())
    {
        const char c = text[i];
        switch (in)
        {
            case In::Comment:
                if (text.compare(i, 3, "-->") == 0)
                {
                    in = In::Text;
                    i += 3;
                    continue;
                }
                break;
            case In::Instruction:
                if (text.compare(i, 2, "?>") == 0)
                {
                    in = In::Text;
                    i += 2;
                    continue;
                }
                break;
            case In::CData:
                if (text.compare(i, 3, "]]>") == 0)
                {
                    in = In::Text;
                    i += 3;
                    continue;
                }
                break;
            case In::Doctype:
                if (c == '>')
                {
                    in = In::Text;
                }
                break;
            case In::Text:
                if (c == '<')
                {
                    if (text.compare(i, 4, "<!--") == 0)
                    {
                        in = In::Comment;
                        i += 4;
                        continue;
                    }
                    if (text.compare(i, 2, "<?") == 0)
                    {
                        in = In::Instruction;
                        i += 2;
                        continue;
                    }
                    if (text.compare(i, 9, "<![CDATA[") == 0)
                    {
                        in = In::CData;
                        i += 9;
                        continue;
                    }
                    if (text.compare(i, 2, "<!") == 0)
                    {
                        in = In::Doctype;
                        i += 2;
                        continue;
                    }
                    in      = In::Tag;
                    tag_at  = i;
                    closing = i + 1 < text.size() && text[i + 1] == '/';
                    tag_name = readName(i + (closing ? 2 : 1));
                    present.clear();
                    present_at.clear();
                    quote = 0;
                    i += (closing ? 2 : 1) + tag_name.size();
                    continue;
                }
                break;
            case In::Tag:
                if (quote)
                {
                    if (c == quote)
                    {
                        quote = 0;
                    }
                    break;
                }
                if (c == '"' || c == '\'')
                {
                    quote = c;
                    break;
                }
                if (c == '<')
                {
                    // A tag left open, as typing leaves one: the next `<`
                    // starts another, and the open one is nobody's parent.
                    in = In::Text;
                    continue;
                }
                if (c == '>')
                {
                    const bool self_closing = i > 0 && text[i - 1] == '/';
                    if (closing)
                    {
                        if (!parents.empty())
                        {
                            parents.pop_back();
                        }
                    }
                    else if (!self_closing && !tag_name.empty())
                    {
                        parents.push_back(tag_name);
                    }
                    in = In::Text;
                    break;
                }
                if (nameByte(c) && (i == 0 || !nameByte(text[i - 1])))
                {
                    // An attribute's name, where it is followed by `=`.
                    const std::string name = readName(i);
                    size_t            after = i + name.size();
                    while (after < text.size() && blank(text[after]))
                    {
                        ++after;
                    }
                    if (after < text.size() && text[after] == '=')
                    {
                        present.push_back(name);
                        present_at.push_back(i);
                    }
                    i = i + name.size();
                    continue;
                }
                break;
        }
        ++i;
    }

    context.parents = parents;
    context.wordStart = doc.clamp(at);
    switch (in)
    {
        case In::Comment:
            context.where = Context::Where::Comment;
            return context;
        case In::Instruction:
        case In::CData:
        case In::Doctype:
            context.where = Context::Where::Other;
            return context;
        case In::Text:
            context.where = Context::Where::Text;
            return context;
        case In::Tag:
            break;
    }
    context.tag     = tag_name;
    context.present = present;
    // Just after the `<`, with the name still under the caret: the tag
    // itself; past a blank, its attributes.
    const size_t name_end = tag_at + (closing ? 2 : 1) + tag_name.size();
    if (name_end >= text.size())
    {
        context.where     = closing ? Context::Where::ClosingTag : Context::Where::TagName;
        context.wordStart = posOfOffset(text, tag_at + (closing ? 2 : 1));
        return context;
    }
    if (quote)
    {
        // Inside a value: of the attribute whose `=` came last.
        context.where = Context::Where::AttributeValue;
        size_t        q = text.rfind(quote);
        size_t        eq = q == std::string::npos ? std::string::npos : text.rfind('=', q);
        if (eq != std::string::npos)
        {
            size_t name_start = eq;
            while (name_start > 0 && blank(text[name_start - 1]))
            {
                --name_start;
            }
            size_t s = name_start;
            while (s > 0 && nameByte(text[s - 1]))
            {
                --s;
            }
            context.attribute = text.substr(s, name_start - s);
        }
        context.wordStart = posOfOffset(text, q + 1);
        return context;
    }
    context.where = Context::Where::AttributeName;
    // The name being typed, if the text ends in one.
    size_t s = text.size();
    while (s > name_end && nameByte(text[s - 1]))
    {
        --s;
    }
    context.wordStart = posOfOffset(text, s);
    if (s < text.size() && !present_at.empty() && present_at.back() == s)
    {
        // The last name read was this one, still being typed, not carried.
        context.present.pop_back();
    }
    return context;
}

void ALXUIService::complete(const ALTextDocument& doc, const ALTextPos& at, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out)
{
    const ALXUISchema& schema  = ALXUISchema::get();
    const Context      context = contextAt(doc, at);
    auto add = [&](const std::string& text, const std::string& detail, ALSyntaxKind kind, const std::string& snippet = std::string()) {
        if (!matchesPrefix(text, prefix))
        {
            return;
        }
        ALCodeEditor::Completion completion;
        completion.text    = text;
        completion.detail  = detail;
        completion.kind    = kind;
        completion.snippet = snippet;
        out.push_back(std::move(completion));
    };
    switch (context.where)
    {
        case Context::Where::TagName:
        {
            // Under a parent: what it takes, its parameter elements first;
            // at the top: every tag.
            const ALXUISchema::Tag* parent = context.parents.empty() ? nullptr : schema.tag(context.parents.back());
            if (parent)
            {
                for (const ALXUISchema::Element& element : parent->elements)
                {
                    add(element.name, "parameter", ALSyntaxKind::Attribute);
                }
                for (const ALXUISchema::Tag& tag : schema.tags())
                {
                    if (schema.acceptsChild(parent->name, tag.name))
                    {
                        add(tag.name, tag.note, ALSyntaxKind::Tag);
                    }
                }
            }
            else
            {
                for (const ALXUISchema::Tag& tag : schema.tags())
                {
                    add(tag.name, tag.note, ALSyntaxKind::Tag);
                }
            }
            return;
        }
        case Context::Where::ClosingTag:
            if (!context.parents.empty())
            {
                add(context.parents.back(), "", ALSyntaxKind::Tag, context.parents.back() + ">");
            }
            return;
        case Context::Where::AttributeName:
        {
            const ALXUISchema::Tag* tag = schema.tag(context.tag);
            if (!tag)
            {
                return;
            }
            for (const ALXUISchema::Attribute& attribute : tag->attributes)
            {
                if (attribute.ignored || std::find(context.present.begin(), context.present.end(), attribute.name) != context.present.end())
                {
                    continue;
                }
                std::string detail = attribute.type;
                if (attribute.holds && !attribute.held.empty())
                {
                    detail += " = " + attribute.held;
                }
                if (attribute.deprecated)
                {
                    detail += attribute.instead.empty() ? " (deprecated)" : " (deprecated: " + attribute.instead + ")";
                }
                add(attribute.name, detail, attribute.deprecated ? ALSyntaxKind::Deprecated : ALSyntaxKind::Attribute, attribute.name + "=\"${1}\"");
            }
            return;
        }
        case Context::Where::AttributeValue:
        {
            const ALXUISchema::Attribute* attribute = schema.attribute(context.tag, context.attribute);
            if (!attribute)
            {
                return;
            }
            if (attribute->value == ALParamType::BOOLEAN)
            {
                add("true", "", ALSyntaxKind::Constant);
                add("false", "", ALSyntaxKind::Constant);
            }
            for (const std::string& value : attribute->values)
            {
                add(value, attribute->type, ALSyntaxKind::AttributeValue);
            }
            return;
        }
        default:
            return;
    }
}

std::string ALXUIService::hover(const ALTextDocument& doc, const ALTextPos& at)
{
    const ALXUISchema& schema = ALXUISchema::get();
    // The word under the position, as the text has it.
    const std::string& line = doc.line(at.line);
    S32                s    = std::min(at.column, static_cast<S32>(line.size()));
    S32                e    = s;
    while (s > 0 && nameByte(line[s - 1]))
    {
        --s;
    }
    while (e < static_cast<S32>(line.size()) && nameByte(line[e]))
    {
        ++e;
    }
    if (e <= s)
    {
        return std::string();
    }
    const std::string word = line.substr(s, e - s);
    const Context     context = contextAt(doc, ALTextPos(at.line, e));
    switch (context.where)
    {
        case Context::Where::TagName:
        case Context::Where::ClosingTag:
        {
            if (const ALXUISchema::Tag* tag = schema.tag(word))
            {
                return tag->name + (tag->note.empty() ? std::string() : "\n" + tag->note);
            }
            return std::string();
        }
        case Context::Where::AttributeName:
        case Context::Where::AttributeValue:
        {
            const ALXUISchema::Attribute* attribute = schema.attribute(context.tag, context.where == Context::Where::AttributeValue ? context.attribute : word);
            if (!attribute)
            {
                return std::string();
            }
            std::string text = context.tag + " " + attribute->name + ": " + attribute->type;
            if (attribute->holds)
            {
                text += "\nDefault: " + (attribute->held.empty() ? std::string("(empty)") : attribute->held);
            }
            if (!attribute->values.empty())
            {
                std::string values;
                for (const std::string& value : attribute->values)
                {
                    values += (values.empty() ? "" : ", ") + value;
                }
                text += "\nOne of: " + values;
            }
            if (!attribute->alias.empty())
            {
                text += "\nAlso written: " + attribute->alias;
            }
            if (attribute->deprecated)
            {
                text += "\n(deprecated" + (attribute->instead.empty() ? std::string() : "; use " + attribute->instead) + ")";
            }
            if (attribute->ignored)
            {
                text += "\nRead and thrown away.";
            }
            return text;
        }
        default:
            return std::string();
    }
}

bool ALXUIService::parses(std::string_view text, ALTextPos& where, std::string& message)
{
    pugi::xml_document       document;
    const pugi::xml_parse_result result = document.load_buffer(text.data(), text.size(), pugi::parse_default | pugi::parse_comments);
    if (result)
    {
        message.clear();
        return true;
    }
    where   = posOfOffset(text, static_cast<size_t>(std::max<ptrdiff_t>(0, result.offset)));
    message = result.description();
    return false;
}
