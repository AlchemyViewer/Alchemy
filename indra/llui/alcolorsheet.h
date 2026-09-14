/**
 * @file alcolorsheet.h
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

#pragma once

#include "llstl.h"
#include "llxmlnode.h"
#include "v4color.h"

#include <string>
#include <string_view>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

// A colors.xml is a list of <color> elements, each naming a colour and
// giving it either a value or the name of another colour. A skin ships one,
// the default skin ships one, a user may add one to either, and a sheet is
// all of them read in override order: a name declared again shadows what it
// meant before, and every reference is followed once, over the merged
// result, so a skin that redefines a colour redefines it for every default
// entry that refers to it.
//
// A declaration that cannot be used -- a value that is not numbers, a
// reference to nothing, a member of a cycle -- is discarded in favour of the
// one it shadowed, and the diagnostic says so with the file and line. A
// broken skin entry therefore leaves the default's colour in place rather
// than leaving a hole.
class ALColorSheet
{
public:
    struct Declaration
    {
        enum class Kind : U8
        {
            Value,      // value="r g b a"
            Reference,  // reference="OtherName"
            Invalid     // read, reported, never used
        };

        std::string name;
        Kind        kind = Kind::Invalid;
        LLColor4    value;
        std::string reference;
        U16         file = 0;       // index into files()
        S32         line = -1;      // the element's line in that file
        S32         shadowed = -1;  // the declaration this one overrides, or -1
    };

    struct Diagnostic
    {
        enum class Kind : U8
        {
            NoName,           // a <color> without a name; ignored
            NoColor,          // a name with neither value nor reference
            BadValue,         // a value that is not three or four numbers, or has more text after them
            UnknownAttribute, // an attribute other than name, value and reference
            Duplicate,        // a name declared twice by the same file
            Undefined,        // a reference to a name nothing defines
            Cycle             // colours that refer to each other around a loop
        };

        Kind        kind;
        S32         declaration = -1; // index into declarations(); -1 for NoName
        std::string message;          // one line, file(line) first
    };

    // Read one document's <color> elements on top of what is already here.
    // False when the root is not <colors>; nothing is read then.
    bool read(const LLXMLNodePtr& root, std::string_view filename);

    // Follow every reference. A name this sheet does not declare is
    // answered by base, when there is one. Diagnostics are composed here,
    // since what a bad declaration cost is only known once the names it
    // shadowed have been tried; call it once, after every read.
    void resolve(const ALColorSheet* base = nullptr);

    void clear();

    // The colour a name resolved to, or nullptr when it is undefined.
    const LLColor4* find(std::string_view name) const;

    // The declaration whose colour find() returns, or nullptr.
    const Declaration* declarationOf(std::string_view name) const;

    const std::vector<Declaration>& declarations() const { return mDeclarations; }
    const std::vector<std::string>& files() const { return mFiles; }
    const std::vector<Diagnostic>&  diagnostics() const { return mDiagnostics; }
    std::string_view fileOf(const Declaration& declaration) const { return mFiles[declaration.file]; }

    // Every resolved name with its colour, in the order the names were first
    // declared.
    template <typename F>
    void forEachResolved(F&& f) const
    {
        for (const Name& name : mNames)
        {
            if (name.state == State::Resolved)
            {
                f(std::string_view(mDeclarations[name.used].name), name.color);
            }
        }
    }

private:
    enum class State : U8 { Unresolved, Resolving, Resolved, Undefined };

    // One distinct name and what became of it.
    struct Name
    {
        S32      declared = -1;   // the last declaration read
        S32      top = -1;        // the declaration being tried, or in use
        S32      used = -1;       // the declaration that supplied the colour
        S32      cause = -1;      // the note that left it undefined
        State    state = State::Unresolved;
        LLColor4 color;
    };

    // A diagnostic before its sentence is known.
    struct Note
    {
        Diagnostic::Kind kind;
        S32              declaration = -1;
        std::string      detail;              // the attribute, the value text, or the name referred to
        std::vector<S32> members;             // Cycle: its declarations in reference order
        U16              file = 0;            // NoName, which has no declaration
        S32              line = -1;
        bool             target_known = false; // Undefined: the target is a name here that ended undefined
        bool             discarded = false;    // the declaration was tried and given up on this
    };

    S32  indexOf(std::string_view name) const;
    S32  declare(Declaration&& declaration);
    void readValue(Declaration& declaration, const std::string& text, S32 index);
    S32  note(Diagnostic::Kind kind, S32 declaration, std::string detail = {});
    S32  noteFor(S32 declaration) const;
    void discard(S32 name, S32 note, S32 cause, std::vector<S32>& stack);
    void compose();
    std::string where(S32 declaration) const;
    std::string dependents(S32 cause, const std::vector<S32>& subjects, std::string_view pronoun) const;

    std::vector<Declaration> mDeclarations;
    std::vector<Name>        mNames;
    boost::unordered_flat_map<std::string, S32, ll::string_hash, std::equal_to<>> mIndex;
    std::vector<std::string> mFiles;
    std::vector<Note>        mNotes;
    size_t                   mReadNotes = 0;  // how many of mNotes came from read()
    std::vector<Diagnostic>  mDiagnostics;
};
