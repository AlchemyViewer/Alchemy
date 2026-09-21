/**
 * @file alsyntaxgrammar.h
 * @brief A highlighting grammar: states of rules, read from a file.
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

#include "stdtypes.h"
#include "llsd.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What a token of source text is, to a theme: the fixed vocabulary every
// grammar speaks and every colour table answers.
enum class ALSyntaxKind : U8
{
    Text,
    Comment,
    DocComment,
    String,
    Escape,
    Number,
    Keyword,
    Control,
    Type,
    Constant,
    Function,
    Event,
    Label,
    Operator,
    Punctuation,
    Preprocessor,
    Tag,
    Attribute,
    AttributeValue,
    Entity,
    Variable,
    Parameter,
    Property,
    Deprecated,
    Invalid,
    COUNT
};

const char*                  alSyntaxKindName(ALSyntaxKind kind);
std::optional<ALSyntaxKind>  alSyntaxKindFromName(std::string_view name);

// A run of one kind on a line, in bytes.
struct ALSyntaxToken
{
    S32          begin = 0;
    S32          end   = 0;
    ALSyntaxKind kind  = ALSyntaxKind::Text;

    friend bool operator==(const ALSyntaxToken& a, const ALSyntaxToken& b)
    {
        return a.begin == b.begin && a.end == b.end && a.kind == b.kind;
    }
    friend bool operator!=(const ALSyntaxToken& a, const ALSyntaxToken& b) { return !(a == b); }
};

// Where a line starts, as the grammar sees it: the stack of states entered
// and not yet left, each with whatever the rule that entered it captured
// -- the level of a Lua long bracket, say -- so that the rule that leaves
// it knows what to look for. Two lines that start in the same state lex
// the same way from the same text, which is what lets an edit stop
// re-lexing where the states settle.
struct ALSyntaxState
{
    struct Frame
    {
        U16         state = 0;
        std::string payload;

        friend bool operator==(const Frame& a, const Frame& b) { return a.state == b.state && a.payload == b.payload; }
        friend bool operator!=(const Frame& a, const Frame& b) { return !(a == b); }
    };

    std::vector<Frame> frames;

    friend bool operator==(const ALSyntaxState& a, const ALSyntaxState& b) { return a.frames == b.frames; }
    friend bool operator!=(const ALSyntaxState& a, const ALSyntaxState& b) { return !(a == b); }
};

// The words a grammar looks an identifier up in, by table: a language's
// functions, constants, events and types. Runtime data rather than grammar
// text, filled by whoever knows the vocabulary -- for LSL and SLua, the
// definitions the region delivers.
class ALSyntaxWords
{
public:
    void set(std::string_view table, std::vector<std::string> words);
    void clear();
    bool has(std::string_view table, std::string_view word) const;
    bool empty() const { return mTables.empty(); }
    // Every word beginning with the prefix, case aside, as the word and
    // its table: what completion offers.
    void collect(std::string_view prefix, std::vector<std::pair<std::string, std::string>>& out) const;

private:
    typedef boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> set_t;
    std::map<std::string, set_t, std::less<>> mTables;
};

// A grammar: named states, each a list of rules tried in order at every
// position. A rule matches a literal, a run of characters from a class, an
// identifier looked up in word tables, a span with an end and an escape
// that may run on across lines, an anchored regular expression, or the end
// of the line; it names the kind of token it makes and the state to enter,
// push or pop. What nothing matches is one character of the state's
// default kind.
//
// Read from LLSD (see app_settings/syntax/*.xml for the shape), and shared
// between every view of the same language: it holds nothing about any
// document.
class ALSyntaxGrammar
{
public:
    ALSyntaxGrammar();
    ~ALSyntaxGrammar();
    ALSyntaxGrammar(const ALSyntaxGrammar&) = delete;
    ALSyntaxGrammar& operator=(const ALSyntaxGrammar&) = delete;

    // False, with what was wrong, for a description that does not hold
    // together: an unknown kind, a rule that pushes a state nobody
    // declared, a class that does not parse.
    bool load(const LLSD& description, std::string& error);
    static std::shared_ptr<const ALSyntaxGrammar> fromFile(const std::string& path, std::string& error);

    const std::string&              name() const;
    const std::vector<std::string>& extensions() const;
    // The tables the word rules consult, so a language service knows what
    // to fill.
    const std::vector<std::string>& wordTables() const;
    // What a comment to the end of the line begins with, or nothing.
    const std::string& lineComment() const;
    // Whether the text is prose rather than code: the spell check then
    // looks at all of it, not only at comments and strings.
    bool prose() const;
    // The words the grammar itself declares that begin with the prefix,
    // as the word and its table.
    void collectWords(std::string_view prefix, std::vector<std::pair<std::string, std::string>>& out) const;

    ALSyntaxState initialState() const;

    // One line, from the state it starts in to the state the next line
    // starts in. The tokens cover the line without a gap or an overlap,
    // and two neighbours of one kind are one token.
    void lexLine(std::string_view line, ALSyntaxState& state, std::vector<ALSyntaxToken>& tokens, const ALSyntaxWords& words) const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

// The grammars on disk, by name and by file extension.
class ALSyntaxLibrary
{
public:
    // Every .xml in the directory. One that does not load is logged and
    // left out. Answers how many loaded.
    S32  loadDirectory(const std::string& directory);
    bool loadFile(const std::string& path, std::string& error);
    void add(std::shared_ptr<const ALSyntaxGrammar> grammar);

    std::shared_ptr<const ALSyntaxGrammar> find(std::string_view name) const;
    std::shared_ptr<const ALSyntaxGrammar> forExtension(std::string_view extension) const;
    std::vector<std::string>               names() const;

private:
    std::map<std::string, std::shared_ptr<const ALSyntaxGrammar>, std::less<>> mGrammars;
};
