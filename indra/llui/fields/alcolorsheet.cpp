/**
 * @file alcolorsheet.cpp
 * @brief What any number of colors.xml files declare, merged by name and followed.
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

#include "alcolorsheet.h"

#include <algorithm>
#include <cctype>

#include <fast_float/fast_float.h>
#include <fmt/format.h>

namespace
{
    using Kind = ALColorSheet::Declaration::Kind;
    using Report = ALColorSheet::Diagnostic::Kind;

    bool isSpace(char c)
    {
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    }

    // The numbers at the front of a value, whitespace between them, and
    // whether anything followed. A token that starts with a number and goes
    // on -- "0.66," or "0.50.5", which the tree's own float parser reads as
    // two -- yields the number and ends the reading, so what the value
    // meant is kept where it can be and the value is reported either way.
    struct Numbers
    {
        F32  v[4] = { 0.f, 0.f, 0.f, 1.f };
        S32  count = 0;    // at most four
        bool more = false; // there was text after them
    };

    Numbers readNumbers(std::string_view text)
    {
        Numbers out;
        size_t pos = 0;
        while (true)
        {
            while (pos < text.size() && isSpace(text[pos]))
            {
                ++pos;
            }
            if (pos == text.size())
            {
                break;
            }
            size_t end = pos;
            while (end < text.size() && !isSpace(text[end]))
            {
                ++end;
            }
            F32 value = 0.f;
            const auto result = fast_float::from_chars(text.data() + pos, text.data() + end, value);
            if (out.count == 4 || result.ec != std::errc())
            {
                out.more = true;
                break;
            }
            out.v[out.count++] = value;
            if (result.ptr != text.data() + end)
            {
                out.more = true;
                break;
            }
            pos = end;
        }
        return out;
    }
}

bool ALColorSheet::read(const LLXMLNodePtr& root, std::string_view filename)
{
    if (root.isNull() || !root->hasName("colors"))
    {
        return false;
    }

    const U16 file = static_cast<U16>(mFiles.size());
    mFiles.emplace_back(filename);

    // Whatever an earlier resolve() added is about declarations that are
    // about to be shadowed; the next one says it again if it still holds.
    mNotes.resize(mReadNotes);

    for (LLXMLNodePtr node = root->getFirstChild(); node.notNull(); node = node->getNextSibling())
    {
        if (!node->hasName("color"))
        {
            continue;
        }

        Declaration declaration;
        declaration.file = file;
        declaration.line = node->getLineNumber();

        const std::string* value = nullptr;
        const std::string* reference = nullptr;
        std::vector<std::string_view> unknown;
        for (const auto& [entry, attribute] : node->mAttributes)
        {
            const std::string_view attribute_name(entry->mString);
            if (attribute_name == "name")
            {
                declaration.name = attribute->getValue();
            }
            else if (attribute_name == "value")
            {
                value = &attribute->getValue();
            }
            else if (attribute_name == "reference")
            {
                reference = &attribute->getValue();
            }
            else
            {
                unknown.push_back(attribute_name);
            }
        }

        if (declaration.name.empty())
        {
            Note& n = mNotes.emplace_back();
            n.kind = Report::NoName;
            n.file = file;
            n.line = declaration.line;
            continue;
        }

        const S32 index = declare(std::move(declaration));
        for (std::string_view attribute_name : unknown)
        {
            note(Report::UnknownAttribute, index, std::string(attribute_name));
        }

        Declaration& declared = mDeclarations[index];
        if (reference && !reference->empty())
        {
            declared.kind = Kind::Reference;
            declared.reference = *reference;
            if (value)
            {
                note(Report::BadValue, index, *value);
            }
        }
        else if (value)
        {
            readValue(declared, *value, index);
        }
        else
        {
            declared.kind = Kind::Invalid;
            note(Report::NoColor, index);
        }
    }

    mReadNotes = mNotes.size();
    return true;
}

S32 ALColorSheet::declare(Declaration&& declaration)
{
    const S32 index = static_cast<S32>(mDeclarations.size());
    const S32 existing = indexOf(declaration.name);
    bool same_file = false;
    if (existing < 0)
    {
        mIndex.emplace(declaration.name, static_cast<S32>(mNames.size()));
        Name& name = mNames.emplace_back();
        name.declared = index;
        name.top = index;
    }
    else
    {
        Name& name = mNames[existing];
        declaration.shadowed = name.declared;
        same_file = mDeclarations[name.declared].file == declaration.file;
        name.declared = index;
        name.top = index;
    }
    mDeclarations.push_back(std::move(declaration));
    if (same_file)
    {
        note(Report::Duplicate, index);
    }
    return index;
}

void ALColorSheet::readValue(Declaration& declaration, const std::string& text, S32 index)
{
    const Numbers numbers = readNumbers(text);
    if (numbers.count < 3)
    {
        declaration.kind = Kind::Invalid;
        note(Report::BadValue, index, text);
        return;
    }
    declaration.kind = Kind::Value;
    declaration.value = LLColor4(numbers.v[0], numbers.v[1], numbers.v[2], numbers.v[3]);
    if (numbers.more)
    {
        note(Report::BadValue, index, text);
    }
}

S32 ALColorSheet::note(Diagnostic::Kind kind, S32 declaration, std::string detail)
{
    Note& n = mNotes.emplace_back();
    n.kind = kind;
    n.declaration = declaration;
    n.detail = std::move(detail);
    return static_cast<S32>(mNotes.size() - 1);
}

// The read-time note that made a declaration invalid.
S32 ALColorSheet::noteFor(S32 declaration) const
{
    for (size_t i = 0; i < mReadNotes; ++i)
    {
        const Note& n = mNotes[i];
        if (n.declaration == declaration && (n.kind == Report::NoColor || n.kind == Report::BadValue))
        {
            return static_cast<S32>(i);
        }
    }
    llassert(false);
    return -1;
}

// The declaration on top for a name has failed for the reason the note gives:
// the one it shadowed is tried in its place, and a name with nothing left
// is undefined because of cause. The name is the top of the stack.
void ALColorSheet::discard(S32 name_index, S32 note_index, S32 cause, std::vector<S32>& stack)
{
    mNotes[note_index].discarded = true;
    Name& name = mNames[name_index];
    name.top = mDeclarations[name.top].shadowed;
    if (name.top < 0)
    {
        name.state = State::Undefined;
        name.cause = cause;
        stack.pop_back();
    }
}

void ALColorSheet::resolve(const ALColorSheet* base)
{
    mNotes.resize(mReadNotes);
    for (Note& n : mNotes)
    {
        n.discarded = false;
    }
    for (Name& name : mNames)
    {
        name.top = name.declared;
        name.used = -1;
        name.cause = -1;
        name.state = State::Unresolved;
    }

    const auto resolved = [](Name& name, const LLColor4& color)
    {
        name.color = color;
        name.used = name.top;
        name.state = State::Resolved;
    };

    // Depth first from each name in turn, the stack holding the chain of
    // names being followed. Every failure discards one declaration, so the
    // walk ends however the files refer to each other.
    std::vector<S32> stack;
    for (S32 root = 0; root < static_cast<S32>(mNames.size()); ++root)
    {
        if (mNames[root].state != State::Unresolved)
        {
            continue;
        }
        mNames[root].state = State::Resolving;
        stack.assign(1, root);
        while (!stack.empty())
        {
            const S32 name_index = stack.back();
            Name& name = mNames[name_index];
            const Declaration& declaration = mDeclarations[name.top];

            if (declaration.kind == Kind::Invalid)
            {
                const S32 n = noteFor(name.top);
                discard(name_index, n, n, stack);
                continue;
            }
            if (declaration.kind == Kind::Value)
            {
                resolved(name, declaration.value);
                stack.pop_back();
                continue;
            }

            const S32 target_index = indexOf(declaration.reference);
            if (target_index < 0)
            {
                if (const LLColor4* color = base ? base->find(declaration.reference) : nullptr)
                {
                    resolved(name, *color);
                    stack.pop_back();
                    continue;
                }
                const S32 n = note(Report::Undefined, name.top, declaration.reference);
                discard(name_index, n, n, stack);
                continue;
            }

            Name& target = mNames[target_index];
            switch (target.state)
            {
            case State::Resolved:
                resolved(name, target.color);
                stack.pop_back();
                break;

            case State::Undefined:
            {
                const S32 n = note(Report::Undefined, name.top, declaration.reference);
                mNotes[n].target_known = true;
                discard(name_index, n, target.cause, stack);
                break;
            }

            case State::Unresolved:
                target.state = State::Resolving;
                stack.push_back(target_index);
                break;

            case State::Resolving:
            {
                // The stack from the target up is a loop. Every declaration
                // in it is discarded; the target is retried at once with
                // what it shadowed so the chain beneath it stays valid, and
                // the rest are retried when the walk reaches them again.
                const size_t from = std::find(stack.begin(), stack.end(), target_index) - stack.begin();
                const S32 n = note(Report::Cycle, target.top);
                mNotes[n].discarded = true;
                for (size_t i = from; i < stack.size(); ++i)
                {
                    mNotes[n].members.push_back(mNames[stack[i]].top);
                }
                for (size_t i = from; i < stack.size(); ++i)
                {
                    Name& member = mNames[stack[i]];
                    member.top = mDeclarations[member.top].shadowed;
                    if (member.top < 0)
                    {
                        member.state = State::Undefined;
                        member.cause = n;
                    }
                    else
                    {
                        member.state = State::Unresolved;
                    }
                }
                stack.resize(from);
                if (target.state == State::Unresolved)
                {
                    target.state = State::Resolving;
                    stack.push_back(target_index);
                }
                break;
            }
            }
        }
    }

    compose();
}

void ALColorSheet::clear()
{
    mDeclarations.clear();
    mNames.clear();
    mIndex.clear();
    mFiles.clear();
    mNotes.clear();
    mReadNotes = 0;
    mDiagnostics.clear();
}

S32 ALColorSheet::indexOf(std::string_view name) const
{
    const auto found = mIndex.find(name);
    return found == mIndex.end() ? -1 : found->second;
}

const LLColor4* ALColorSheet::find(std::string_view name) const
{
    const S32 index = indexOf(name);
    if (index < 0 || mNames[index].state != State::Resolved)
    {
        return nullptr;
    }
    return &mNames[index].color;
}

const ALColorSheet::Declaration* ALColorSheet::declarationOf(std::string_view name) const
{
    const S32 index = indexOf(name);
    if (index < 0 || mNames[index].state != State::Resolved)
    {
        return nullptr;
    }
    return &mDeclarations[mNames[index].used];
}

std::string ALColorSheet::where(S32 declaration) const
{
    const Declaration& d = mDeclarations[declaration];
    return fmt::format("{}({})", mFiles[d.file], d.line);
}

// The names left undefined by a cause other than the ones the sentence is
// already about, as the clause that follows "It is undefined".
std::string ALColorSheet::dependents(S32 cause, const std::vector<S32>& subjects, std::string_view pronoun) const
{
    std::string list;
    for (size_t i = 0; i < mNames.size(); ++i)
    {
        const Name& other = mNames[i];
        if (other.state != State::Undefined || other.cause != cause
            || std::find(subjects.begin(), subjects.end(), static_cast<S32>(i)) != subjects.end())
        {
            continue;
        }
        if (!list.empty())
        {
            list += ", ";
        }
        list += mDeclarations[other.declared].name;
    }
    if (list.empty())
    {
        return {};
    }
    return fmt::format(", and so are the colours that refer to {}: {}", pronoun, list);
}

// Every note as a sentence, in file and line order: the reading notes come
// in that order already, and the ones resolution added afterwards belong
// beside the declarations they are about.
void ALColorSheet::compose()
{
    struct Placed
    {
        U16        file;
        S32        line;
        Diagnostic diagnostic;
    };
    std::vector<Placed> placed;

    for (size_t i = 0; i < mNotes.size(); ++i)
    {
        const Note& n = mNotes[i];
        const S32 cause = static_cast<S32>(i);
        Diagnostic diagnostic;
        diagnostic.kind = n.kind;
        diagnostic.declaration = n.declaration;

        if (n.kind == Report::NoName)
        {
            diagnostic.message = fmt::format("{}({}): a <color> with no name is ignored.", mFiles[n.file], n.line);
            placed.push_back({ n.file, n.line, std::move(diagnostic) });
            continue;
        }

        const Declaration& d = mDeclarations[n.declaration];
        const S32 name_index = indexOf(d.name);
        const Name& name = mNames[name_index];
        std::string body;
        bool per_member = false;
        switch (n.kind)
        {
        case Report::NoColor:
            body = fmt::format("\"{}\" has neither value nor reference", d.name);
            break;

        case Report::BadValue:
            if (d.kind == Kind::Invalid)
            {
                body = fmt::format("\"{}\" has value=\"{}\", which is not three or four numbers", d.name, n.detail);
                if (!n.detail.empty() && std::isalpha(static_cast<unsigned char>(n.detail[0])))
                {
                    body += fmt::format("; a name goes in reference=\"{}\"", n.detail);
                }
            }
            else if (d.kind == Kind::Reference)
            {
                body = fmt::format("\"{}\" has both value=\"{}\" and reference=\"{}\"; the reference is used",
                                   d.name, n.detail, d.reference);
            }
            else
            {
                body = fmt::format("\"{}\" has value=\"{}\"; only the first {} numbers are read",
                                   d.name, n.detail, readNumbers(n.detail).count);
            }
            break;

        case Report::UnknownAttribute:
            body = fmt::format("\"{}\" has an attribute \"{}\" the colour table does not read; it reads name, value and reference",
                               d.name, n.detail);
            break;

        case Report::Duplicate:
            body = fmt::format("\"{}\" is declared again; line {} is shadowed", d.name, mDeclarations[d.shadowed].line);
            break;

        case Report::Undefined:
            if (n.target_known && name.state == State::Undefined)
            {
                // Undefined because its target is: it is listed on the
                // target's own line, and has none of its own.
                continue;
            }
            body = fmt::format("\"{}\" refers to \"{}\", which {}", d.name, n.detail,
                               n.target_known ? "is undefined" : "no colors.xml defines");
            break;

        case Report::Cycle:
        {
            body = fmt::format("\"{}\" refers to itself", d.name);
            const size_t count = n.members.size();
            if (count > 1)
            {
                body += " through ";
                for (size_t k = 1; k < count; ++k)
                {
                    if (k > 1)
                    {
                        body += (k + 1 == count) ? " and " : ", ";
                    }
                    body += fmt::format("\"{}\" ({})", mDeclarations[n.members[k]].name, where(n.members[k]));
                }
                body += fmt::format(". None of the {} declarations is used: ", count);
                std::vector<S32> subjects;
                for (size_t k = 0; k < count; ++k)
                {
                    const std::string& member_name = mDeclarations[n.members[k]].name;
                    const S32 member_index = indexOf(member_name);
                    const Name& member = mNames[member_index];
                    subjects.push_back(member_index);
                    if (k > 0)
                    {
                        body += ", ";
                    }
                    if (member.state == State::Resolved)
                    {
                        body += fmt::format("\"{}\" takes {}", member_name, where(member.used));
                    }
                    else
                    {
                        body += fmt::format("\"{}\" is undefined", member_name);
                    }
                }
                body += dependents(cause, subjects, "them");
                per_member = true;
            }
            break;
        }

        case Report::NoName:
            break;
        }

        std::string text = fmt::format("{}: {}", where(n.declaration), body);
        if (n.discarded && !per_member)
        {
            if (name.state == State::Resolved)
            {
                text += fmt::format("; {} is used instead.", where(name.used));
            }
            else
            {
                text += ". It is undefined";
                text += dependents(cause, { name_index }, "it");
                text += ".";
            }
        }
        else
        {
            text += ".";
        }
        diagnostic.message = std::move(text);
        placed.push_back({ d.file, d.line, std::move(diagnostic) });
    }

    std::stable_sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b)
    {
        return a.file != b.file ? a.file < b.file : a.line < b.line;
    });
    mDiagnostics.clear();
    mDiagnostics.reserve(placed.size());
    for (Placed& p : placed)
    {
        mDiagnostics.push_back(std::move(p.diagnostic));
    }
}
