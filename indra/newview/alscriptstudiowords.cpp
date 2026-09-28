/**
 * @file alscriptstudiowords.cpp
 * @brief Script Studio's words of each language: the vocabulary, editors taught it, completions, hovers and the reference's addresses.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptstudiowords.h"

#include "alsaid.h"

#include "alscriptstudiodoc.h"
#include "lluistring.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>

namespace
{
    // The words of each language, built once from the definitions and
    // kept until they change.
    std::vector<ALScriptStudioWords::Vocab> sVocabulary[2];
    bool                                    sVocabularyBuilt[2] = { false, false };
    // How many times each has been built: what an index over one knows it
    // by, since the list built again is the same list, and may be as long.
    U32                                     sVocabularyBuilds[2] = { 0, 0 };
}

// static
ALScriptStudioWords::Sources& ALScriptStudioWords::sources()
{
    static Sources sources;
    return sources;
}

// static
void ALScriptStudioWords::forget()
{
    sVocabularyBuilt[0] = sVocabularyBuilt[1] = false;
}

// static
const std::vector<ALScriptStudioWords::Vocab>& ALScriptStudioWords::vocabulary(bool lua)
{
    std::vector<Vocab>& out = sVocabulary[lua ? 1 : 0];
    if (sVocabularyBuilt[lua ? 1 : 0])
    {
        return out;
    }
    sVocabularyBuilt[lua ? 1 : 0] = true;
    ++sVocabularyBuilds[lua ? 1 : 0];
    out.clear();
    const LLSD keywords = sources().keywords ? sources().keywords(lua) : LLSD();
    if (!keywords.isMap())
    {
        return out;
    }
    auto firstLine = [](const std::string& text) {
        const size_t end = text.find('\n');
        return end == std::string::npos ? text : text.substr(0, end);
    };
    auto arguments = [](const LLSD& args) {
        std::string list;
        auto        one = [&](const std::string& name, const LLSD& attrs) {
            if (!list.empty())
            {
                list += ", ";
            }
            const std::string type = attrs.get("type").asString();
            list += type.empty() ? name : type + " " + name;
        };
        if (args.isArray())
        {
            for (LLSD::array_const_iterator it = args.beginArray(); it != args.endArray(); ++it)
            {
                if (it->isMap())
                {
                    for (LLSD::map_const_iterator arg = it->beginMap(); arg != it->endMap(); ++arg)
                    {
                        one(arg->first, arg->second);
                    }
                }
            }
        }
        else if (args.isMap())
        {
            for (LLSD::map_const_iterator arg = args.beginMap(); arg != args.endMap(); ++arg)
            {
                one(arg->first, arg->second);
            }
        }
        return list;
    };
    for (LLSD::map_const_iterator group = keywords.beginMap(); group != keywords.endMap(); ++group)
    {
        if (!group->second.isMap())
        {
            continue;
        }
        const std::string& name = group->first;
        ALSyntaxKind       kind;
        if (name == "functions")
        {
            kind = ALSyntaxKind::Function;
        }
        else if (name == "events")
        {
            kind = ALSyntaxKind::Event;
        }
        else if (name == "types")
        {
            kind = ALSyntaxKind::Type;
        }
        else if (name == "controls")
        {
            kind = ALSyntaxKind::Control;
        }
        else if (name.compare(0, 9, "constants") == 0)
        {
            kind = ALSyntaxKind::Constant;
        }
        else
        {
            continue;
        }
        for (LLSD::map_const_iterator entry = group->second.beginMap(); entry != group->second.endMap(); ++entry)
        {
            const LLSD& attrs = entry->second;
            Vocab       word;
            word.text          = entry->first;
            word.kind          = kind;
            word.tooltip       = attrs.get("tooltip").asString();
            word.documentation = ALCompletion::shared(word.tooltip);
            word.deprecated    = attrs.has("deprecated") && (attrs["deprecated"].asBoolean() || attrs["deprecated"].asString() == "true");
            word.godMode       = attrs.has("god-mode") && (attrs["god-mode"].asBoolean() || attrs["god-mode"].asString() == "true");
            if (attrs.has("sleep"))
            {
                // No delay said as none.
                const std::string sleep = attrs["sleep"].asString();
                word.sleep              = sleep.find_first_not_of("0.") == std::string::npos ? std::string() : sleep;
            }
            switch (kind)
            {
                case ALSyntaxKind::Function:
                {
                    // Lua says "()" for a function that returns nothing,
                    // which is nothing worth reading before the name.
                    std::string returns = attrs.get("return").asString();
                    if (returns == "()")
                    {
                        returns.clear();
                    }
                    word.detail =
                        (returns.empty() ? std::string() : returns + " ") + word.text + "(" + arguments(attrs.get("arguments")) + ")";
                    break;
                }
                case ALSyntaxKind::Event:
                    word.detail = word.text + "(" + arguments(attrs.get("arguments")) + ")";
                    break;
                case ALSyntaxKind::Constant:
                {
                    const std::string type  = attrs.get("type").asString();
                    const std::string value = attrs.get("value").asString();
                    word.detail =
                        (type.empty() ? std::string() : type + " ") + word.text + (value.empty() ? std::string() : " = " + value);
                    break;
                }
                default:
                    word.detail = firstLine(attrs.get("tooltip").asString());
                    break;
            }
            out.push_back(std::move(word));
        }
    }
    std::sort(out.begin(), out.end(), [](const Vocab& a, const Vocab& b) { return a.text < b.text; });
    return out;
}

// static
void ALScriptStudioWords::teach(ALCodeEditor& editor, bool lua)
{
    const std::vector<Vocab>& words = vocabulary(lua);
    std::vector<std::string>  functions, events, types, controls, constants, deprecated;
    for (const Vocab& word : words)
    {
        if (word.deprecated)
        {
            deprecated.push_back(word.text);
            continue;
        }
        switch (word.kind)
        {
            case ALSyntaxKind::Function: functions.push_back(word.text); break;
            case ALSyntaxKind::Event:    events.push_back(word.text); break;
            case ALSyntaxKind::Type:     types.push_back(word.text); break;
            case ALSyntaxKind::Control:  controls.push_back(word.text); break;
            case ALSyntaxKind::Constant: constants.push_back(word.text); break;
            default: break;
        }
    }
    if (!lua)
    {
        // The preprocessor's words, which the grid's keywords do not list,
        // while their transforms are on: Firestorm's switch and case, the
        // extensions' break and continue, and inline and const, which it
        // takes off whenever it runs.
        const std::vector<std::string> extra = sources().preprocessorWords ? sources().preprocessorWords() : std::vector<std::string>();
        for (const std::string& word : extra)
        {
            if (std::find(controls.begin(), controls.end(), word) == controls.end())
            {
                controls.push_back(word);
            }
        }
    }
    ALSyntaxWords& tables = editor.highlighter().words();
    tables.set("function", std::move(functions));
    tables.set("event", std::move(events));
    tables.set("type", std::move(types));
    tables.set("control", std::move(controls));
    tables.set("constant", std::move(constants));
    tables.set("deprecated", std::move(deprecated));
    editor.highlighter().wordsChanged();
}

// static
bool ALScriptStudioWords::linkArgument(bool lua, std::string_view function, S32 argument)
{
    const Vocab* found = word(lua, function);
    if (!found || found->kind != ALSyntaxKind::Function || argument < 0)
    {
        return false;
    }
    // The detail is the call as the definitions give it: `name(type Name,
    // type Name)`; the parameter asked about is the last word of its part.
    const std::string& detail = found->detail;
    const size_t       open   = detail.find('(');
    const size_t       close  = detail.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open)
    {
        return false;
    }
    std::vector<std::string> parts;
    LLStringUtil::getTokens(detail.substr(open + 1, close - open - 1), parts, ",");
    if (argument >= static_cast<S32>(parts.size()))
    {
        return false;
    }
    std::string name = parts[argument];
    LLStringUtil::trim(name);
    if (const size_t space = name.find_last_of(" \t"); space != std::string::npos)
    {
        name = name.substr(space + 1);
    }
    LLStringUtil::toLower(name);
    return name == "link" || name == "linknumber";
}

// static
bool ALScriptStudioWords::inStateBody(ALCodeEditor& editor, const ALTextPos& at)
{
    // Straight inside a state is one bracket deep, in a block opened after
    // `default` or `state name`: the depth read off the editor's, kept for
    // each line's start, rather than every line above lexed again; and
    // only the block's opening read.
    if (editor.bracketDepthAt(at) != 1)
    {
        return false;
    }
    // The line the block opened on: the last at or above the caret's that
    // starts outside every bracket. What names it may stand on a line or
    // two above its brace.
    S32 opened = at.line;
    while (opened > 0 && editor.bracketDepthBefore(opened) > 0)
    {
        --opened;
    }
    std::string before[2];
    for (S32 line = llmax(0, opened - 2); line <= opened; ++line)
    {
        const std::string& text = editor.document().line(line);
        for (const ALSyntaxToken& token : editor.highlighter().tokens(line))
        {
            if (token.kind == ALSyntaxKind::Comment || token.kind == ALSyntaxKind::DocComment)
            {
                continue;
            }
            const std::string_view word = std::string_view(text).substr(token.begin, token.end - token.begin);
            if (word.find_first_not_of(" \t") == std::string_view::npos)
            {
                continue;
            }
            if (line == opened && token.kind == ALSyntaxKind::Punctuation && word.find('{') != std::string_view::npos)
            {
                return before[0] == "default" || before[1] == "state";
            }
            before[1] = std::move(before[0]);
            before[0] = std::string(word);
        }
    }
    return false;
}

// static
ALCodeEditor::Completion ALScriptStudioWords::completionFor(const Vocab& word, bool lua)
{
    ALCodeEditor::Completion c;
    c.text          = word.text;
    c.detail        = word.detail;
    c.kind          = word.kind;
    c.deprecated    = word.deprecated;
    c.documentation = word.documentation;
    if (word.kind == ALSyntaxKind::Event)
    {
        // A handler to fill in: LSL's with its typed parameters as the
        // detail reads them, SLua's as a function set on LLEvents.
        if (lua)
        {
            std::string params;
            for (const std::string& name : ALCodeEditor::parameterNames(word.detail, word.text))
            {
                params += (params.empty() ? "" : ", ") + name;
            }
            c.snippet = "LLEvents." + word.text + " = function(" + params + ")\n    $0\nend";
        }
        else
        {
            c.snippet = word.detail + "\n{\n    $0\n}";
        }
    }
    return c;
}

// static
const ALScriptStudioWords::Vocab* ALScriptStudioWords::word(bool lua, std::string_view name)
{
    if (name.empty())
    {
        return nullptr;
    }
    // By an index over the words, built with them and thrown away with
    // them: this is asked on every hover, every completion and every
    // settling of the caret, over some hundreds of words.
    static boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> index[2];
    static U32                                                                              indexed[2] = { 0, 0 };
    const std::vector<Vocab>&                                                               words      = vocabulary(lua);
    const size_t                                                                            which      = lua ? 1 : 0;
    if (indexed[which] != sVocabularyBuilds[which])
    {
        index[which].clear();
        index[which].reserve(words.size());
        for (size_t i = 0; i < words.size(); ++i)
        {
            index[which].emplace(words[i].text, i);
        }
        indexed[which] = sVocabularyBuilds[which];
    }
    const auto found = index[which].find(name);
    return found == index[which].end() ? nullptr : &words[found->second];
}

// static
std::string ALScriptStudioWords::helpUrl(bool lua, const std::string& word)
{
    // The wiki's page for an LSL name, which a SLua ll.Name shares; the
    // Luau library's own page for its libraries; the SLua portal for the
    // rest.
    if (!lua || word.compare(0, 3, "ll.") == 0)
    {
        std::string page = word;
        if (lua)
        {
            page.erase(2, 1);
        }
        LLUIString url(sources().lslHelpUrl ? sources().lslHelpUrl() : std::string("[LSL_STRING]"));
        url.setArg("[LSL_STRING]", page.empty() ? std::string("LSL_Portal") : page);
        return url.getString();
    }
    for (const char* library : { "bit32.", "buffer.", "coroutine.", "debug.", "math.", "os.", "string.", "table.", "utf8." })
    {
        if (word.compare(0, strlen(library), library) == 0)
        {
            return "https://luau.org/library";
        }
    }
    return "https://wiki.secondlife.com/wiki/Lua_Alpha";
}

// static
const char* ALScriptStudioWords::imageNameOf(const ALScriptStudioDoc& doc)
{
    if (!doc.notecard)
    {
        return doc.language.lua ? "Inv_Script_Luau" : "Inv_Script";
    }
    if (doc.name == ".luaurc" || doc.name == ".lslrc")
    {
        return "Studio_Config";
    }
    return doc.file.empty() ? "Inv_Notecard" : "Studio_File";
}

// static
const char* ALScriptStudioWords::imageNameOf(ALScriptSymbolKind kind)
{
    switch (kind)
    {
        case ALScriptSymbolKind::Keyword:   return "Symbol_Keyword";
        case ALScriptSymbolKind::Variable:  return "Symbol_Variable";
        case ALScriptSymbolKind::Parameter: return "Symbol_Parameter";
        case ALScriptSymbolKind::Function:  return "Symbol_Function";
        case ALScriptSymbolKind::Field:     return "Symbol_Field";
        case ALScriptSymbolKind::Type:      return "Symbol_Type";
        case ALScriptSymbolKind::Constant:  return "Symbol_Constant";
        case ALScriptSymbolKind::Event:     return "Symbol_Event";
        case ALScriptSymbolKind::State:
        case ALScriptSymbolKind::Label:     return "Symbol_Label";
        case ALScriptSymbolKind::Module:    return "Symbol_Module";
    }
    return "Symbol_Word";
}

// static
const char* ALScriptStudioWords::kindWordOf(ALScriptSymbolKind kind)
{
    switch (kind)
    {
        case ALScriptSymbolKind::Keyword:   return "KindKeyword";
        case ALScriptSymbolKind::Variable:  return "KindVariable";
        case ALScriptSymbolKind::Parameter: return "KindParameter";
        case ALScriptSymbolKind::Function:  return "KindFunction";
        case ALScriptSymbolKind::Field:     return "KindField";
        case ALScriptSymbolKind::Type:      return "KindType";
        case ALScriptSymbolKind::Constant:  return "KindConstant";
        case ALScriptSymbolKind::Event:     return "KindEvent";
        case ALScriptSymbolKind::State:     return "KindState";
        case ALScriptSymbolKind::Label:     return "KindLabel";
        case ALScriptSymbolKind::Module:    return "KindModule";
    }
    return nullptr;
}

// static
// static
std::string ALScriptStudioWords::notesOf(const Vocab& word)
{
    std::string out = word.tooltip;
    const auto  line = [&out](const std::string& said) {
        out += (out.empty() ? "" : "\n") + said;
    };
    if (!word.sleep.empty())
    {
        line(alSaid("ScriptWordSleep", "The script sleeps [SECONDS] s after this", { { "[SECONDS]", word.sleep } }));
    }
    if (word.godMode)
    {
        line(alSaid("ScriptWordGodMode", "Only a god may call this"));
    }
    if (word.deprecated)
    {
        line(ALCodeEditor::deprecatedNote());
    }
    return out;
}

bool ALScriptStudioWords::hoverText(bool lua, const ALTextDocument& text, const ALTextPos& at, std::string_view word, std::string& out)
{
    // The word as the vocabulary knows it: `Say` under the mouse in
    // `ll.Say` is asked about as `ll.Say`, and `pi` in `math.pi` as
    // `math.pi`. A local, a parameter or a member the definitions do
    // not name is the analyzer's to explain.
    std::string        name(word);
    const std::string& line = text.line(at.line);
    S32                from = at.column;
    while (from > 0 && line[from - 1] != '.' && (isalnum(static_cast<unsigned char>(line[from - 1])) || line[from - 1] == '_'))
    {
        --from;
    }
    while (from > 0 && line[from - 1] == '.')
    {
        S32 head = from - 1;
        while (head > 0 && (isalnum(static_cast<unsigned char>(line[head - 1])) || line[head - 1] == '_'))
        {
            --head;
        }
        if (head == from - 1)
        {
            break;
        }
        name = line.substr(head, from - 1 - head) + "." + name;
        from = head;
    }
    // A member not known as one is some table's, not the global of its
    // name: `obj.type` is no `type`.
    const Vocab* known = ALScriptStudioWords::word(lua, name);
    if (!known && name == word)
    {
        known = ALScriptStudioWords::word(lua, word);
    }
    if (!known)
    {
        return false;
    }
    out                     = known->detail.empty() ? known->text : known->detail;
    const std::string notes = notesOf(*known);
    if (!notes.empty())
    {
        out += "\n" + notes;
    }
    return true;
}

// static
void ALScriptStudioWords::complete(bool lua, ALCodeEditor& editor, const ALTextPos& at, std::string_view prefix,
                                   const std::vector<Snippet>& snippets, const std::string& snippet_word,
                                   std::vector<ALCodeEditor::Completion>& out)
{
    auto begins = [&prefix](const std::string& text) { return ALCodeEditor::matchTier(text, prefix) >= 0; };
    // An LSL event's handler goes straight inside a state and nowhere
    // else, so a handler is offered only there; asked once, and only
    // if one matches.
    std::optional<bool> in_state;
    for (const Vocab& each : vocabulary(lua))
    {
        if (!begins(each.text))
        {
            continue;
        }
        if (!lua && each.kind == ALSyntaxKind::Event)
        {
            if (!in_state)
            {
                in_state = inStateBody(editor, at);
            }
            if (!*in_state)
            {
                continue;
            }
        }
        out.push_back(completionFor(each, lua));
    }
    // A snippet by its prefix, where a bare word is being typed
    // rather than a member.
    if (prefix.find('.') == std::string_view::npos)
    {
        for (const Snippet& snippet : snippets)
        {
            if (begins(snippet.prefix))
            {
                ALCodeEditor::Completion c;
                c.text          = snippet.prefix;
                c.detail        = snippet_word + "  " + snippet.name;
                c.kind          = ALSyntaxKind::Control;
                c.snippet       = snippet.body;
                c.documentation = ALCompletion::shared(snippet.detail);
                out.push_back(std::move(c));
            }
        }
    }
}

// static
std::vector<ALQuickOpen::Candidate> ALScriptStudioWords::library(bool lua, const std::string& what, const std::vector<Snippet>& snippets,
                                                                 const std::string& deprecated_word)
{
    std::vector<ALQuickOpen::Candidate> candidates;
    auto                                firstLine = [](const std::string& text) {
        const size_t end = text.find('\n');
        return end == std::string::npos ? text : text.substr(0, end);
    };
    if (what == "snippet")
    {
        for (const Snippet& snippet : snippets)
        {
            ALQuickOpen::Candidate one;
            one.label  = snippet.name;
            one.detail = snippet.detail;
            one.also   = snippet.prefix;
            // By what it is rather than where it stands in a list that
            // may be made again while the picker is up -- the scripter's
            // own saved meanwhile, new definitions from a region.
            one.value  = snippet.name + '\n' + snippet.prefix;
            candidates.push_back(std::move(one));
        }
        return candidates;
    }
    const ALSyntaxKind kind = what == "function" ? ALSyntaxKind::Function : what == "event" ? ALSyntaxKind::Event : ALSyntaxKind::Constant;
    for (const Vocab& each : vocabulary(lua))
    {
        if (each.kind != kind)
        {
            continue;
        }
        ALQuickOpen::Candidate one;
        one.label  = each.text;
        one.detail = each.deprecated ? deprecated_word : firstLine(each.tooltip);
        one.also   = each.detail;
        one.value  = each.text;
        candidates.push_back(std::move(one));
    }
    return candidates;
}

// static
std::string ALScriptStudioWords::referenceText(const Vocab& word, bool lua)
{
    std::string text = word.detail.empty() ? word.text : word.detail;
    if (word.deprecated)
    {
        text += "\n" + ALCodeEditor::deprecatedNote();
    }
    if (!word.tooltip.empty())
    {
        text += "\n\n" + word.tooltip;
    }
    return text + "\n" + helpUrl(lua, word.text);
}
