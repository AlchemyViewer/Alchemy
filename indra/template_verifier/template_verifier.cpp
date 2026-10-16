/**
 * @file template_verifier.cpp
 * @brief Checks a message template against the master template it must stay compatible with
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

// template_verifier [--mode development|production] <template> [<master>]
//
// Parses the template, and with a master parses that too and reports how the
// two relate: Same, Newer (messages, blocks or variables added, or a message
// more deprecated), Older (the reverse), Mixed (both), or Incompatible (a
// message, block or variable changed in place). Development mode accepts
// anything but Incompatible; production accepts Same and Newer only. Exit
// status is 0 when acceptable, 1 when not, 2 when a template does not parse.
//
// This is the grammar and the comparison of Linden's template_verifier.py
// and its indra.ipc modules, including the version-1 rules the master no
// longer uses.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Compatibility: a verdict with its reasons. Combining two takes the lower
// verdict and keeps both lists of reasons, except that Older and Newer
// together are Mixed.

enum class Level
{
    Incompatible,
    Mixed,
    Older,
    Newer,
    Same
};

struct Compatibility
{
    Level level = Level::Same;
    std::vector<std::string> reasons;

    static Compatibility of(Level level, std::string reason)
    {
        Compatibility c;
        c.level = level;
        c.reasons.push_back(std::move(reason));
        return c;
    }

    bool same() const { return level == Level::Same; }

    Compatibility combine(const Compatibility& other) const
    {
        const bool aged = (level == Level::Older || level == Level::Newer);
        const bool other_aged = (other.level == Level::Older || other.level == Level::Newer);
        Compatibility result;
        if (aged && other_aged && level != other.level)
        {
            result.level = Level::Mixed;
            result.reasons = reasons;
        }
        else if (level <= other.level)
        {
            result.level = level;
            result.reasons = reasons;
        }
        else
        {
            result.level = other.level;
            result.reasons = other.reasons;
            result.reasons.insert(result.reasons.end(), reasons.begin(), reasons.end());
            return result;
        }
        result.reasons.insert(result.reasons.end(), other.reasons.begin(), other.reasons.end());
        return result;
    }

    void prefix(const std::string& leadin)
    {
        for (std::string& reason : reasons)
        {
            reason = leadin + reason;
        }
    }

    const char* name() const
    {
        switch (level)
        {
            case Level::Incompatible: return "Incompatible";
            case Level::Mixed: return "Mixed";
            case Level::Older: return "Older";
            case Level::Newer: return "Newer";
            case Level::Same: return "Same";
        }
        return "";
    }

    std::string explain() const
    {
        std::string text = name();
        text += '\n';
        for (const std::string& reason : reasons)
        {
            text += reason;
            text += '\n';
        }
        return text;
    }
};

// ---------------------------------------------------------------------------
// The template.

struct Variable
{
    std::string name;
    std::string type;
    std::optional<std::string> size;

    Compatibility compatibleWithBase(const Variable& base) const
    {
        if (name != base.name)
            return Compatibility::of(Level::Incompatible, "has different name: " + name + " vs. " + base.name + " in base");
        if (type != base.type)
            return Compatibility::of(Level::Incompatible, "has different type: " + type + " vs. " + base.type + " in base");
        if ((type == "Fixed" || type == "Variable") && size != base.size)
            return Compatibility::of(Level::Incompatible, "has different size: " + size.value_or("None") + " vs. " + base.size.value_or("None") + " in base");
        return Compatibility{};
    }
};

struct Block
{
    std::string name;
    std::string repeat;
    std::optional<std::string> count;
    std::vector<Variable> variables;

    Compatibility compatibleWithBase(const Block& base) const
    {
        if (name != base.name)
            return Compatibility::of(Level::Incompatible, "has different name: " + name + " vs. " + base.name + " in base");
        if (repeat != base.repeat)
            return Compatibility::of(Level::Incompatible, "has different repeat: " + repeat + " vs. " + base.repeat + " in base");
        if (repeat == "Multiple" && count != base.count)
            return Compatibility::of(Level::Incompatible, "has different count: " + count.value_or("None") + " vs. " + base.count.value_or("None") + " in base");

        Compatibility compatibility;
        const size_t common = std::min(variables.size(), base.variables.size());
        for (size_t i = 0; i < common; ++i)
        {
            Compatibility c = variables[i].compatibleWithBase(base.variables[i]);
            if (!c.same())
                c = Compatibility::of(Level::Incompatible, "variable " + std::to_string(i) + " isn't identical");
            compatibility = compatibility.combine(c);
        }
        if (variables.size() > base.variables.size())
            compatibility = compatibility.combine(Compatibility::of(Level::Newer, "has " + std::to_string(variables.size() - base.variables.size()) + " extra variables"));
        else if (variables.size() < base.variables.size())
            compatibility = compatibility.combine(Compatibility::of(Level::Older, "missing " + std::to_string(base.variables.size() - variables.size()) + " extra variables"));
        return compatibility;
    }
};

const std::vector<std::string> DEPRECATIONS = { "NotDeprecated", "UDPDeprecated", "UDPBlackListed", "Deprecated" };

struct Message
{
    std::string name;
    unsigned long number = 0;
    std::string priority;
    std::string trust;
    std::string coding;
    size_t deprecateLevel = 0;
    std::vector<Block> blocks;

    Compatibility compatibleWithBase(const Message& base) const
    {
        if (name != base.name)
            return Compatibility::of(Level::Incompatible, "has different name: " + name + " vs. " + base.name + " in base");
        if (priority != base.priority)
            return Compatibility::of(Level::Incompatible, "has different priority: " + priority + " vs. " + base.priority + " in base");
        if (trust != base.trust)
            return Compatibility::of(Level::Incompatible, "has different trust: " + trust + " vs. " + base.trust + " in base");
        if (coding != base.coding)
            return Compatibility::of(Level::Incompatible, "has different coding: " + coding + " vs. " + base.coding + " in base");
        if (number != base.number)
            return Compatibility::of(Level::Incompatible, "has different number: " + std::to_string(number) + " vs. " + std::to_string(base.number) + " in base");

        Compatibility compatibility;
        if (deprecateLevel != base.deprecateLevel)
        {
            const std::string detail = DEPRECATIONS[deprecateLevel] + " vs. " + DEPRECATIONS[base.deprecateLevel] + " in base";
            if (deprecateLevel < base.deprecateLevel)
                compatibility = compatibility.combine(Compatibility::of(Level::Older, "is less deprecated: " + detail));
            else
                compatibility = compatibility.combine(Compatibility::of(Level::Newer, "is more deprecated: " + detail));
        }

        const size_t common = std::min(blocks.size(), base.blocks.size());
        for (size_t i = 0; i < common; ++i)
        {
            Compatibility c = blocks[i].compatibleWithBase(base.blocks[i]);
            if (!c.same())
                c = Compatibility::of(Level::Incompatible, "block " + std::to_string(i) + " isn't identical");
            compatibility = compatibility.combine(c);
        }
        if (blocks.size() > base.blocks.size())
            compatibility = compatibility.combine(Compatibility::of(Level::Newer, "has " + std::to_string(blocks.size() - base.blocks.size()) + " extra blocks"));
        else if (blocks.size() < base.blocks.size())
            compatibility = compatibility.combine(Compatibility::of(Level::Older, "missing " + std::to_string(base.blocks.size() - blocks.size()) + " extra blocks"));
        return compatibility;
    }
};

struct Template
{
    std::map<std::string, Message> messages;

    Compatibility compatibleWithBase(const Template& base) const
    {
        std::set<std::string> names;
        for (const auto& [name, message] : messages) names.insert(name);
        for (const auto& [name, message] : base.messages) names.insert(name);

        Compatibility compatibility;
        for (const std::string& name : names)
        {
            const auto mine = messages.find(name);
            const auto theirs = base.messages.find(name);
            Compatibility c;
            if (mine == messages.end())
                c = Compatibility::of(Level::Older, "missing message " + name + ", did you mean to deprecate?");
            else if (theirs == base.messages.end())
                c = Compatibility::of(Level::Newer, "added message " + name);
            else
            {
                c = mine->second.compatibleWithBase(theirs->second);
                c.prefix("in message " + name + ": ");
            }
            compatibility = compatibility.combine(c);
        }
        return compatibility;
    }
};

// ---------------------------------------------------------------------------
// Tokens: whitespace-separated, "//" to end of line is a comment.

struct ParseError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

class TokenStream
{
public:
    explicit TokenStream(const std::string& contents)
    {
        std::istringstream in(contents);
        std::string line;
        unsigned lineno = 0;
        while (std::getline(in, line))
        {
            ++lineno;
            if (const size_t comment = line.find("//"); comment != std::string::npos)
                line.erase(comment);
            std::istringstream words(line);
            std::string word;
            while (words >> word)
                mTokens.push_back({ word, lineno });
        }
    }

    bool atEOF() const { return mNext >= mTokens.size(); }

    std::optional<std::string> peek() const
    {
        if (atEOF()) return std::nullopt;
        return mTokens[mNext].text;
    }

    std::string consume()
    {
        if (atEOF()) fail("unexpected end of file");
        return mTokens[mNext++].text;
    }

    bool want(std::string_view token)
    {
        if (!atEOF() && mTokens[mNext].text == token)
        {
            ++mNext;
            return true;
        }
        return false;
    }

    void require(std::string_view token)
    {
        if (!want(token)) fail("expected \"" + std::string(token) + "\"");
    }

    std::optional<std::string> wantOneOf(const std::vector<std::string>& options)
    {
        if (!atEOF() && std::find(options.begin(), options.end(), mTokens[mNext].text) != options.end())
            return consume();
        return std::nullopt;
    }

    std::string requireOneOf(const std::vector<std::string>& options)
    {
        if (auto token = wantOneOf(options)) return *token;
        std::string text;
        for (size_t i = 0; i < options.size(); ++i)
        {
            if (i) text += (i + 1 == options.size()) ? " or " : ", ";
            text += '"' + options[i] + '"';
        }
        fail("expected one of " + text);
        return {};
    }

    std::string requireSymbol()
    {
        const auto token = peek();
        if (token && isSymbol(*token)) return consume();
        fail("expected symbol");
        return {};
    }

    std::optional<std::string> wantInteger()
    {
        const auto token = peek();
        if (token && isInteger(*token)) return consume();
        return std::nullopt;
    }

    std::string requireInteger()
    {
        if (auto token = wantInteger()) return *token;
        fail("expected integer");
        return {};
    }

    std::string requireFloat()
    {
        const auto token = peek();
        if (token && isFloat(*token)) return consume();
        fail("expected float");
        return {};
    }

    void requireEOF()
    {
        if (!atEOF()) fail("expected end of file");
    }

private:
    struct Token
    {
        std::string text;
        unsigned line;
    };

    static bool isSymbol(const std::string& s)
    {
        if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
        return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
    }

    static bool isInteger(const std::string& s)
    {
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            return std::all_of(s.begin() + 2, s.end(), [](unsigned char c) { return std::isxdigit(c); });
        return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
    }

    static bool isFloat(const std::string& s)
    {
        const size_t dot = s.find('.');
        const std::string_view whole(s.data(), dot == std::string::npos ? s.size() : dot);
        if (whole.empty() || !std::all_of(whole.begin(), whole.end(), [](unsigned char c) { return std::isdigit(c); })) return false;
        if (dot == std::string::npos) return true;
        return std::all_of(s.begin() + dot + 1, s.end(), [](unsigned char c) { return std::isdigit(c); });
    }

    [[noreturn]] void fail(const std::string& reason) const
    {
        std::string context;
        for (size_t i = mNext; i < mTokens.size() && i < mNext + 5; ++i)
        {
            if (i > mNext) context += ' ';
            context += mTokens[i].text;
        }
        const unsigned line = atEOF() ? (mTokens.empty() ? 0 : mTokens.back().line) : mTokens[mNext].line;
        throw ParseError("line " + std::to_string(line) + ": " + reason + " @ ... " + context);
    }

    std::vector<Token> mTokens;
    size_t mNext = 0;
};

// ---------------------------------------------------------------------------
// The grammar. Version 2 templates number every message and may deprecate
// it; version 1 templates number messages by their order within a priority
// and carry free text between them.

class TemplateParser
{
public:
    explicit TemplateParser(TokenStream& tokens) : mTokens(tokens) {}

    Template parseTemplate()
    {
        Template t;
        while (true)
        {
            if (mTokens.want("version"))
            {
                mVersion = std::stod(mTokens.requireFloat());
                continue;
            }
            if (auto message = parseMessage())
            {
                t.messages[message->name] = std::move(*message);
                continue;
            }
            if (mVersion >= 2.0)
            {
                mTokens.requireEOF();
                break;
            }
            if (mTokens.atEOF()) break;
            mTokens.consume();
        }
        return t;
    }

private:
    std::optional<Message> parseMessage()
    {
        if (!mTokens.want("{")) return std::nullopt;
        Message m;
        m.name = mTokens.requireSymbol();
        m.priority = mTokens.requireOneOf({ "High", "Medium", "Low", "Fixed" });
        if (mVersion >= 2.0 || m.priority == "Fixed")
            m.number = std::stoul(mTokens.requireInteger(), nullptr, 0);
        else
            m.number = ++mNumbers[m.priority];
        m.trust = mTokens.requireOneOf({ "Trusted", "NotTrusted" });
        m.coding = mTokens.requireOneOf({ "Unencoded", "Zerocoded" });
        if (mVersion >= 2.0)
        {
            if (auto deprecation = mTokens.wantOneOf(DEPRECATIONS))
                m.deprecateLevel = std::find(DEPRECATIONS.begin(), DEPRECATIONS.end(), *deprecation) - DEPRECATIONS.begin();
        }
        while (auto block = parseBlock())
            m.blocks.push_back(std::move(*block));
        mTokens.require("}");
        return m;
    }

    std::optional<Block> parseBlock()
    {
        if (!mTokens.want("{")) return std::nullopt;
        Block b;
        b.name = mTokens.requireSymbol();
        b.repeat = mTokens.requireOneOf({ "Single", "Multiple", "Variable" });
        if (b.repeat == "Multiple")
            b.count = mTokens.requireInteger();
        while (auto variable = parseVariable())
            b.variables.push_back(std::move(*variable));
        mTokens.require("}");
        return b;
    }

    std::optional<Variable> parseVariable()
    {
        if (!mTokens.want("{")) return std::nullopt;
        Variable v;
        v.name = mTokens.requireSymbol();
        v.type = mTokens.requireOneOf({ "U8", "U16", "U32", "U64", "S8", "S16", "S32", "S64", "F32", "F64",
                                        "LLVector3", "LLVector3d", "LLVector4", "LLQuaternion",
                                        "LLUUID", "BOOL", "IPADDR", "IPPORT", "Fixed", "Variable" });
        if (v.type == "Fixed" || v.type == "Variable")
            v.size = mTokens.requireInteger();
        else
            mTokens.wantInteger(); // LandStatRequest writes "{ ParcelLocalID S32 1 }"
        mTokens.require("}");
        return v;
    }

    TokenStream& mTokens;
    double mVersion = 0;
    std::map<std::string, unsigned long> mNumbers;
};

Template parseFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path);
    std::stringstream contents;
    contents << in.rdbuf();
    TokenStream tokens(contents.str());
    return TemplateParser(tokens).parseTemplate();
}

} // namespace

int main(int argc, char** argv)
{
    std::string mode = "development";
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc)
            mode = argv[++i];
        else if (arg.rfind("--mode=", 0) == 0)
            mode = arg.substr(7);
        else
            files.push_back(arg);
    }
    if (files.empty() || files.size() > 2 || (mode != "development" && mode != "production"))
    {
        std::fprintf(stderr, "usage: template_verifier [--mode development|production] <template> [<master>]\n");
        return 2;
    }

    Template current, master;
    try
    {
        current = parseFile(files[0]);
        if (files.size() == 1)
        {
            std::printf("%s parses; no master to compare against\n", files[0].c_str());
            return 0;
        }
        master = parseFile(files[1]);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "template_verifier: %s\n", e.what());
        return 2;
    }

    const Compatibility compat = current.compatibleWithBase(master);
    bool acceptable = false;
    switch (compat.level)
    {
        case Level::Same:
        case Level::Newer:
            acceptable = true;
            break;
        case Level::Older:
        case Level::Mixed:
            acceptable = (mode == "development");
            break;
        case Level::Incompatible:
            break;
    }

    std::string explanation = compat.explain();
    for (size_t pos = 0; (pos = explanation.find('\n', pos)) != std::string::npos && pos + 1 < explanation.size(); ++pos)
        explanation.insert(pos + 1, "\t");
    std::printf("%s\n%s", acceptable ? "--- PASS ---" : "*** FAIL ***", explanation.c_str());
    return acceptable ? 0 : 1;
}
