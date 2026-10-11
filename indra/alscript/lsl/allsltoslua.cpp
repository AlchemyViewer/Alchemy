/**
 * @file allsltoslua.cpp
 * @brief An LSL script written again as SLua, where the two languages differ noted rather than guessed.
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

#include "allsltoslua.h"

#include "alscriptfixes.h"

#include "alscriptengine.h"
#include "alscriptstack.h"
#include "allsleffects.h"
#include "allslservice.h"
#include "allsltraits.h"
#include "allslvalues.h"

#include <tailslide/tailslide.hh>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <optional>
#include <string_view>

using namespace Tailslide;

namespace
{
    // A note a lint of the studio's finds the same as in SLua, with the
    // words it is said in, which the note is written with and read back by.
    struct Linted
    {
        const char* key;
        const char* english;
        const char* lint;
    };
    constexpr Linted linted(const char* key, const char* english, const char* lint)
    {
        return { key, english, lint };
    }
    // An llcompat call left as LSL had it, which SlCompatCall writes as ll's
    // where ll's means the same; a string built in a loop, which
    // SlStringBuild puts in a table.
    constexpr Linted LINTED[] = {
        linted("SluaBool", "llcompat.[1] answers 1 or 0, as LSL did; ll.[1] answers true or false.", "SlCompatCall"),
        linted("SluaDetected",
               "llcompat.Detected* count from 0, as LSL's did. SLua's own way is the handler's detected table: detected[n + 1]:getKey(), "
               ":getName(), :getPos() and the rest.",
               "SlCompatCall"),
        linted("SluaIndex", "llcompat.[1] takes indexes from 0, as LSL did; ll.[1] takes them from 1.", "SlCompatCall"),
        linted("SluaIndexFound", "llcompat.[1] counts from 0 and says -1 for none, as LSL did; ll.[1] counts from 1 and says nil.", "SlCompatCall"),
        linted("SluaStringBuild",
               "[1] is built with .. in a loop, which makes a new string each time. SLua's way is to put the pieces in a table and join them "
               "once, with table.concat.",
               "SlStringBuild"),
    };

    const char* englishOf(std::string_view key)
    {
        for (const Linted& each : LINTED)
        {
            if (key == each.key)
            {
                return each.english;
            }
        }
        return "";
    }

    // Whether words are a note's, as its words with marks say it: what
    // lies between the marks there, in order, the first at the start and
    // the last at the end.
    bool saidAs(std::string_view pattern, std::string_view said)
    {
        std::vector<std::string_view> pieces;
        size_t                        from = 0;
        for (size_t at = 0; at + 2 < pattern.size(); ++at)
        {
            if (pattern[at] == '[' && pattern[at + 1] >= '1' && pattern[at + 1] <= '9' && pattern[at + 2] == ']')
            {
                pieces.push_back(pattern.substr(from, at - from));
                from = at + 3;
                at += 2;
            }
        }
        pieces.push_back(pattern.substr(from));
        if (pieces.size() == 1)
        {
            return pattern == said;
        }
        if (said.substr(0, pieces.front().size()) != pieces.front())
        {
            return false;
        }
        size_t at = pieces.front().size();
        for (size_t i = 1; i + 1 < pieces.size(); ++i)
        {
            at = said.find(pieces[i], at);
            if (at == std::string_view::npos)
            {
                return false;
            }
            at += pieces[i].size();
        }
        const std::string_view last = pieces.back();
        return said.size() >= at + last.size() && said.substr(said.size() - last.size()) == last;
    }

    // What marks a note in the SLua.
    constexpr std::string_view NOTE_MARK = "-- LSL:";

    // What LSL says that SLua always says otherwise, as the writer writes
    // it: its operators, a declaration's type, its truths, a cast. Its
    // braces, a vector's and a rotation's brackets and a string joined are
    // said where the writer writes them so (Writer::sameAs): bare marks
    // made the same everywhere would hide a > or a + changed.
    const std::pair<std::string, std::string> SAID_OTHERWISE[] = {
        { "!=", "~=" },
        { "&&", "and" },
        { "||", "or" },
        { "!", "not" },
        { "integer", "local" },
        { "float", "local" },
        { "string", "local" },
        { "key", "local" },
        { "vector", "local" },
        { "rotation", "local" },
        { "list", "local" },
        { "TRUE", "true" },
        { "FALSE", "false" },
        { "(string)", "tostring" },
        { "(integer)", "lslInteger" },
        { "(float)", "lslFloat" },
        { "(key)", "uuid" },
        { "(vector)", "tovector" },
        { "(rotation)", "toquaternion" },
    };
    // Luau's precedence, loosest first: an if-expression holds everything to
    // its right, so it is bracketed wherever it is an operand.
    enum Prec : int
    {
        IF_EXPR = 0,
        OR      = 1,
        AND     = 2,
        COMPARE = 3,
        CONCAT  = 4,
        ADD     = 5,
        MUL     = 6,
        UNARY   = 7,
        POWER   = 8,
        PRIMARY = 9
    };

    // An expression as written: its text, how tightly it binds, and whether
    // it is a Luau boolean where LSL has 1 or 0. Where it is a find's
    // place counted as LSL's, `(x or 0) - 1`: x, SLua's place, counted
    // from 1 or nil.
    struct Expr
    {
        std::string text;
        int         prec    = PRIMARY;
        bool        boolean = false;
        std::string place;
    };

    // Pairs of words, looked for by views of them without a copy.
    struct PairLess
    {
        using is_transparent = void;
        template<typename A, typename B>
        bool operator()(const A& a, const B& b) const
        {
            return std::pair<std::string_view, std::string_view>(a.first, a.second) <
                   std::pair<std::string_view, std::string_view>(b.first, b.second);
        }
    };
    typedef std::set<std::pair<std::string, std::string>, PairLess> SamePairs;

    std::string bracketed(const Expr& e, int at_least)
    {
        return e.prec < at_least ? "(" + e.text + ")" : e.text;
    }

    S32 zeroBased(int one_based)
    {
        return std::max(0, one_based - 1);
    }

    // What Luau reserves, or SLua has for its own, that an LSL name may be.
    bool reservedName(std::string_view name)
    {
        static const boost::unordered_flat_set<std::string_view> RESERVED = {
            "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local", "nil", "not", "or",
            "repeat", "return", "then", "true", "until", "while", "continue", "export", "type", "typeof",
            // SLua's own and Luau's libraries, which a local of the name would hide.
            "ll", "llcompat", "LLEvents", "LLTimers", "vector", "quaternion", "rotation", "uuid", "bit32", "string", "table",
            "math", "utf8", "buffer", "coroutine", "os", "debug", "print", "tostring", "tonumber", "tovector", "toquaternion",
            "torotation", "touuid", "ipairs", "pairs", "next", "select", "error", "assert", "pcall", "xpcall", "unpack",
            "rawget", "rawset", "rawequal", "rawlen", "setmetatable", "getmetatable", "require", "lljson", "llbase64",
            // What the text written here defines of its own.
            "states", "currentState", "setState", "lslInteger", "lslFloat", "vecNorm", "detected", "setTimer", "timerHandle",
            "timerHandler", "timeBase", "getAndResetTime",
        };
        return RESERVED.contains(name);
    }

    // SLua's events that hand their handler the detected table in place of
    // LSL's count.
    bool detectedEvent(std::string_view name)
    {
        static const boost::unordered_flat_set<std::string_view> DETECTED = {
            "collision", "collision_end", "collision_start", "final_damage", "on_damage", "sensor", "touch", "touch_end", "touch_start",
        };
        return DETECTED.contains(name);
    }

    bool isNull(LSLASTNode* node)
    {
        return !node || node->getNodeType() == NODE_NULL;
    }

    // A string's escapes, and the quote that ends it. Between Luau's
    // backticks the quote is the backtick, and the opening brace would begin
    // an expression; a closing brace is text there, and Luau's compiler
    // escapes a % itself.
    std::string escaped(std::string_view text, char quote, bool braces)
    {
        std::string out;
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (c == quote || (braces && c == '{'))
            {
                out += '\\';
                out += c;
                continue;
            }
            switch (c)
            {
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (u < 0x20 || u == 0x7f)
                    {
                        fmt::format_to(std::back_inserter(out), "\\{:03}", u);
                    }
                    else
                    {
                        out += c;
                    }
                    break;
            }
        }
        return out;
    }

    std::string luaString(std::string_view text)
    {
        return "\"" + escaped(text, '"', false) + "\"";
    }

    // Text between Luau's backticks.
    std::string luaTemplateText(std::string_view text)
    {
        return escaped(text, '`', true);
    }

    // An expression with the brackets around it taken off.
    LSLExpression* unbracketed(LSLExpression* e)
    {
        while (e && e->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
        {
            e = static_cast<LSLParenthesisExpression*>(e)->getChildExpr();
        }
        return e;
    }

    std::string number(double v)
    {
        if (std::isnan(v))
        {
            return "(0 / 0)";
        }
        if (std::isinf(v))
        {
            return v > 0 ? "math.huge" : "-math.huge";
        }
        // A single's shortest text: what LSL's float was.
        return ALLSLValues::floatText(v, false);
    }

    // What a variable of an LSL type holds before anything is put in it.
    std::string defaultOf(LSLIType type)
    {
        switch (type)
        {
            case LST_INTEGER:
            case LST_FLOATINGPOINT: return "0";
            case LST_STRING: return "\"\"";
            case LST_KEY: return "NULL_KEY";
            case LST_VECTOR: return "ZERO_VECTOR";
            case LST_QUATERNION: return "ZERO_ROTATION";
            case LST_LIST: return "{}";
            default: return "nil";
        }
    }

    // NULL_KEY, a TEXTURE_ constant and the rest LSL types a string and
    // SLua a uuid, as written.
    bool uuidConstant(LSLExpression* e)
    {
        e = unbracketed(e);
        if (!e || e->getNodeSubType() != NODE_LVALUE_EXPRESSION)
        {
            return false;
        }
        LSLIdentifier* id = static_cast<LSLLValueExpression*>(e)->getIdentifier();
        return id->getSymbol() && id->getSymbol()->getSubType() == SYM_BUILTIN && ALLSLTraits::uuidConstant(id->getName());
    }

    // Every node under one, and it, in the tree's order.
    void walk(LSLASTNode* node, const std::function<void(LSLASTNode*)>& each)
    {
        if (isNull(node))
        {
            return;
        }
        each(node);
        for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
        {
            walk(child, each);
        }
    }

    // Why a list is not grown in place: something else may hold it.
    enum class Shared : U8
    {
        None,
        // Set where the assignment's value is used.
        Assigned,
        // Read where a call of the script's in the same statement changes it.
        Called,
        // Given a list another may hold.
        Held,
        // Given to another variable.
        Given,
        // Returned by a function.
        Returned,
        // Passed to a function of the script's that keeps it.
        Kept,
    };

    class Writer
    {
    public:
        Writer(LSLScript* script, std::string_view source, const ALLSLToSLua::Options& options)
            : mScript(script), mSource(source), mOptions(options), mEffects(script)
        {
        }

        std::string write();
        ALScriptProblems& notes() { return mNotes; }
        const std::vector<ALLSLToSLua::Span>& spans() const { return mSpans; }
        // Each LSL word written otherwise, and how: a call's name, a name
        // SLua holds for its own.
        const SamePairs& same() const { return mSame; }

    private:
        // --- where the two languages differ -----------------------------------------

        // Words by their key: the studio's, where it gives them
        // (Options::words), else the English with its marks filled.
        std::string said(const char* key, const char* english, const std::vector<std::string>& args = {}) const;
        // Said over the statement being written, and at the LSL's place,
        // carrying the lint that finds the same in SLua where one does;
        // once a statement.
        void note(LSLASTNode* at, const char* key, const char* english, std::vector<std::string> args = {});
        // The same, once in the script for its key and words.
        void noteOnce(LSLASTNode* at, const char* key, const char* english, std::vector<std::string> args = {});
        // Text written out made a key, said where SLua's uuid makes another
        // key of it than LSL's held: no text, which is NULL_KEY, and a key
        // with capitals, which it writes small. Whether it said either.
        bool keyText(LSLExpression* at, std::string_view text);
        // Why a list is shared, as SluaListCopy says it.
        std::string sharedWords(Shared why) const;

        // --- names ------------------------------------------------------------------

        std::string nameOf(LSLIdentifier* id);
        std::string nameOf(const char* lsl);
        std::string stateKey(const std::string& state) const;

        // --- the text ---------------------------------------------------------------

        void line(const std::string& text);

        // --- the script's own comments -----------------------------------------------

        // Where a comment begins, or a node begins or ends, by line and
        // column from one; a node's end is one past it.
        struct Pos
        {
            S32 line   = 0;
            S32 column = 0;
            bool operator<(const Pos& o) const { return line != o.line ? line < o.line : column < o.column; }
        };
        struct Comment
        {
            Pos         at;
            S32         endLine = 0;
            // As SLua writes it: a block comment's lines past its first as
            // they were.
            std::string text;
            // A blank line over it in the LSL, which it keeps.
            bool        apart   = false;
            bool        written = false;
            // A toggle's closer, //*/, which is about the code over it.
            bool        closer  = false;
        };
        // Each comment in the LSL, given to the node it is written over --
        // a global, a function, a state, a handler, a statement -- or to
        // the end of what holds it, where nothing after it in there does.
        void findComments();
        void place(size_t k, const std::vector<LSLASTNode*>& children, LSLASTNode* holder);
        bool placeInside(size_t k, LSLASTNode* node);
        void placeIn(size_t k, LSLASTNode* compound);
        // Those given to a node, written over it; those at a holder's end,
        // with any of what it held that nothing wrote, at its end. Those
        // inside the node are put off to the first line written of it,
        // which may be a list's over several lines that has them in its
        // lines (Writer::listLines), and are over it where not.
        void commentsBefore(LSLASTNode* node);
        std::vector<size_t> mDeferred;
        // Where a list over lines has a comment, in its text: marked with a
        // byte no expression's text has (strings escape it) and the
        // comment's index, and how it goes -- after what is on the line
        // (After), on a line of its own (Own), or before what follows on
        // the line (Before). Written by Writer::line, which writes each
        // comment once: a mark of one written is taken out, its line with
        // it if it had one.
        static constexpr char COMMENT_MARK = '\x7f';
        enum class Marked : char
        {
            After  = 'a',
            Own    = 'o',
            Before = 'b'
        };
        static std::string marked(size_t k, Marked how);
        std::string        withComments(const std::string& text);
        void commentsAtEnd(LSLASTNode* holder);
        void writeComment(Comment& c);
        // Those after a node on the line it ends, where it is all on one
        // line or no if or loop: at the end of the first line of code
        // written of it from `from` in the text, as the LSL had them; or
        // where nothing was, on a line of their own.
        void commentsAfter(LSLASTNode* node, size_t from);
        // Where the line of code that begins at `from` in the text ends:
        // its break, or that of the last line a bracket it opens runs on
        // over.
        size_t codeLineEnd(size_t from) const;
        // How long what commentsAfter put after each node's line was.
        boost::unordered_flat_map<LSLASTNode*, size_t> mTrailed;
        // Those on the line a block's brace opens, before anything in it:
        // after the line that opens the block in SLua -- `if ... then`,
        // `else`, `do`, a function's -- as the LSL had them after its brace,
        // a long one running on over the lines after. Called as that line
        // is written; what no line took is written at the top of the block
        // (commentsOpening), or over what stands for a block not written.
        // Whether one put after the line is a line's comment, which takes
        // the rest of the line with it.
        bool trail(LSLASTNode* body);
        void commentsOpening(LSLASTNode* body);
        // What commentsAfter and trail put after a line: the comments of
        // `given` not yet written, each after a space, written by that, and
        // whether any is a line's comment; and that put in at `end` in the
        // text, a line's break.
        std::string trailing(const std::vector<size_t>& given, bool* takes_line = nullptr);
        void        putTrailing(size_t end, const std::string& after);
        // A toggle's brackets -- ---[[ over code and --]] under it, and a
        // block comment ending in a --]] -- are written with a byte the
        // script has none of between them, where their level goes; nought
        // where it has every one. Once the text is whole, each pair at the
        // level nothing between its first opener and its closer closes.
        char        mToggleMark = 0;
        std::string leveled(const std::string& text) const;

        // Declarations on lines one after another that the LSL lined up --
        // each one line, given a value, and their = at one column, or the
        // comments after them -- lined up again, as what each is written as
        // is as long as it is: each as it is written from `from`, the run
        // ended by anything else.
        void lined(LSLASTNode* node, size_t from);
        void lineUp();
        struct Lined
        {
            LSLASTNode*    node;
            LSLIdentifier* id;
            size_t         at;
        };
        std::vector<Lined> mRun;
        std::string indent() const { return std::string(static_cast<size_t>(mDepth) * 4, ' '); }
        // How far in what a list over lines (Writer::listLines) or a rule
        // table holds goes: a level past the line it opens on, and a level
        // more for each it is in.
        S32         mListDepth = 0;
        std::string listIndent(S32 more) const { return indent() + std::string(static_cast<size_t>(mListDepth + more) * 4, ' '); }

        // --- expressions ------------------------------------------------------------

        Expr expr(LSLExpression* e);
        // A list literal the LSL wrote over several lines, over the same
        // lines: what it put on a line together on one, `{` where its `[`
        // was and `}` on a line of its own where its `]` was, each comment
        // in it where it was.
        std::string listLines(LSLASTNode* list);
        // As a number where LSL has one, a boolean made 1 or 0.
        Expr value(LSLExpression* e);
        // As a boolean, as LSL reads a value in a condition.
        Expr condition(LSLExpression* e);
        // An & asked only whether it is nought, as bit32.btest asks it: an
        // & of &s one call of them all. None for anything else.
        std::optional<Expr> bitTest(LSLExpression* e);
        // As a value of another LSL type, where LSL converts on its own.
        Expr coerced(LSLExpression* e, LSLIType to);
        // An operand of `..`: as coerced to a string, but an integer's cast
        // to one, which `..` makes the same text of itself, left a number.
        Expr joinedPiece(LSLExpression* e);
        // Whether an expression is LSL's + of text; and the pieces a chain
        // of them joins, left to right, through brackets.
        bool joinsText(LSLExpression* e);
        void textPieces(LSLExpression* e, std::vector<LSLExpression*>& out);
        // Pieces of text joined as one interpolated string, where one of them
        // is no string -- a number, a key, a float, a vector, a list made
        // text: string.format once, rather than a conversion a piece and
        // `..`. None where every piece is a string, which `..` joins faster,
        // or where there is one piece, and nothing to join.
        std::optional<Expr> interpolated(const std::vector<LSLExpression*>& pieces);
        Expr constant(LSLConstant* c);
        // How the LSL wrote a number in hexadecimal, on the line Tailslide
        // puts its constant at or the one before: where it did, and wrote
        // it no other way there. Tailslide's constants are where the
        // parser's next token was, not where they were.
        std::optional<std::string> hexAsWritten(LSLConstant* c, int v);
        // The numbers written on a constant's line, or the one before where
        // its own has none: each as written, and its value.
        std::vector<std::pair<std::string, unsigned long long>> numbersNear(LSLConstant* c);
        // The variables declared hexadecimal past 0x7FFFFFFF and never set
        // that nothing but bit32 reads, which takes 0xF0000000 as the
        // -268435456 LSL wrapped it to: their declarations, whose
        // hexadecimal stays.
        void                                   findBitsOnly();
        boost::unordered_flat_set<LSLASTNode*> mBitsOnly;
        // Where each of the LSL's lines starts, from the first.
        const std::vector<size_t>& lineStarts();
        std::vector<size_t>        mLineStarts;
        // Whether a line of the LSL lies blank between two others, counted
        // from one, neither of them itself.
        bool blankBetween(S32 after, S32 before);
        Expr lvalue(LSLLValueExpression* e);
        Expr call(LSLFunctionExpression* e);
        // The call as written, and the name SLua calls it by where it is
        // still a call by name: ll.Say, llcompat.List2String, math.abs.
        Expr writeCall(LSLFunctionExpression* e, std::string& called);
        Expr binary(LSLBinaryExpression* e);
        // A bit32 call as LSL's signed integer, but where `same` says its
        // answer is that already, or nothing asks for more.
        Expr signedUnless(bool same, const std::string& call, LSLASTNode* at);
        // What a scripter from LSL is told once of each, over the first
        // place it is written: hexadecimal past 0x7FFFFFFF, which SLua
        // reads as another number than LSL did; and bit32.s32, which makes
        // bit32's answer LSL's.
        bool mSaidHexSign = false;
        bool mSaidSigned  = false;
        // A number not below nought: as nonNegative says, or a global the
        // script never sets, declared one.
        bool notBelowZero(LSLExpression* e) const;
        Expr unary(LSLUnaryExpression* e);
        Expr typecast(LSLTypecastExpression* e);
        // An assignment or a step inside an expression, which SLua makes a
        // statement: done in a function called on the spot.
        Expr sideEffect(LSLExpression* e);
        // The arguments, each as its parameter's type -- but those SLua takes
        // either text or a uuid for (`text`, a bit for each by its place),
        // as they are; and, for ll's or llcompat's (`builtin`), text given
        // for a key (Writer::textAsKey).
        // One may be given written already: its place, and its text.
        std::string args(LSLASTNode* list, LSLParamList* params, U16 text = 0, int put = -1, const std::string& put_text = {},
                         bool builtin = false);
        // Text given where ll or llcompat takes a key: as it is, which they
        // take as LSL's functions did, where uuid() would stop the script
        // on text that is no UUID, which LSL's function answered as it
        // answers a key of nothing; `:: any` tells the type checker, which
        // asks for a uuid. A UUID written out is a uuid, as anywhere.
        std::string textAsKey(LSLExpression* given);
        // What SLua has in a library call's stead, where it means the same;
        // nothing where it has nothing.
        std::optional<Expr> idiom(LSLFunctionExpression* e, const std::string& lsl, std::string& called);
        // A rule list written out for the particle, media and HTTP calls, as
        // the table SLua's ll takes in its stead, where the table does just
        // what the list did; nothing elsewhere.
        std::optional<Expr> ruleTable(LSLFunctionExpression* e, const std::string& lsl, std::string& called);
        // What was detected, from the handler's own table.
        std::optional<Expr> detected(LSLFunctionExpression* e, const std::string& lsl);
        // LSL's script time where the script resets it: read from the
        // script's own clock (mTimeBase), or from llcompat's alone
        // (mCompatTime); nothing for any other call, or where the script
        // resets nothing and ll.GetTime is LSL's llGetTime.
        std::optional<Expr> scriptTime(const std::string& lsl, std::string& called);
        // A call to a function answering an index or -1, which ll answers
        // from one or nil, read against nil where it is only asked whether
        // it found: `found` says whether the test is of finding.
        std::optional<Expr> foundTest(LSLExpression* call, bool found);
        // A call whose answer is an index or -1, that ll can make with the
        // arguments it has.
        LSLFunctionExpression* findCall(LSLExpression* e);
        // Whether each of a call's index arguments is one ll can be given
        // (llIndex).
        bool constantIndexes(LSLFunctionExpression* e, U16 indexes);
        // An LSL index as ll takes it, where that is sure: a whole number
        // written out, moved on by one where not below nought; a counter of
        // a numeric for known not below nought (mNonNegative), or it plus
        // a whole number not below nought, moved on by one -- or as it is,
        // where the loop counts from 1 (mFromOne).
        std::optional<std::string> llIndex(LSLExpression* e);
        // Whether every read of a counter in a loop's body is an index that
        // ll is given (llIndex), which the loop may count from 1 for.
        bool onlyIndexes(LSLSymbol* var, LSLASTNode* body);
        // Whether every read of a counter in a loop's body is of one list's
        // item at it, as it is (listItemFits): the loop may walk the list's
        // items instead (mEach).
        bool onlyItemsOf(LSLSymbol* var, LSLSymbol* list, LSLASTNode* body);
        // The call's arguments for ll: its index arguments moved on by one.
        std::string llArgs(LSLFunctionExpression* e, U16 indexes);
        // `: type` for an LSL type, where types are written.
        std::string typed(LSLIType type) const;

        // --- statements -------------------------------------------------------------

        void statement(LSLASTNode* s, bool last);
        void statementBody(LSLASTNode* s, bool last);
        void block(LSLASTNode* s);
        // A blank line over a statement or a global, where the LSL had one
        // or more between it and the one written before it, which grouped
        // them: one, and not where nothing has been written since `opened`
        // -- a block's start -- nor where a comment over it keeps the
        // blank line over itself (Comment::apart).
        void keepApart(LSLASTNode* before, LSLASTNode* node, size_t opened);
        // An if written from `at` in the text, `lines` lines down, its
        // stretches from `spans`, put on one line where it is one statement
        // and nothing said over it: if c then s end. Not called where a
        // line's comment trails its first line (trail).
        void onOneLine(size_t at, S32 lines, size_t spans);
        // An expression standing as a statement: an assignment, a step, a
        // call.
        void effect(LSLExpression* e);
        void assign(LSLLValueExpression* target, LSLOperator op, LSLExpression* rhs);
        void stateChange(LSLStateStatement* s, bool last);

        // --- loops as Luau counts -------------------------------------------------

        // A for that counts one variable from a start to a limit by a
        // constant step: its variable, where it starts, the check's limit
        // and comparison, and the step. None for any other shape.
        struct Counting
        {
            LSLSymbol*     var   = nullptr;
            LSLIdentifier* id    = nullptr;
            LSLExpression* from  = nullptr;
            LSLExpression* limit = nullptr;
            LSLOperator    check = OP_NONE;
            int            step  = 0;
            // A step by a constant's name, which the step is written by.
            LSLExpression* by    = nullptr;
        };
        std::optional<Counting> counting(LSLForStatement* f) const;
        // A whole number written out, or the name of one that never
        // changes: a global nothing sets, given one, or one of LSL's own.
        bool steadyWhole(LSLExpression* e, int& v) const;
        // Whether an expression reads the same on every turn of a loop:
        // nothing it reads is set in the loop, and it calls only what the
        // definitions call pure. Where kept is given, what the loop sets of
        // it is known to change nothing the expression reads of it.
        bool steadyIn(LSLExpression* e, LSLASTNode* loop, LSLSymbol* kept = nullptr) const;
        // i < llGetListLength(l), where all the loop does to l is put one
        // item in place of another at i: below the length, i is always one
        // of its places, so the length is the same on every turn.
        bool lengthKept(const Counting& c, LSLForStatement* f) const;
        // Before a function's or a handler's body is written: its for
        // loops that Luau's numeric for says exactly, and the variables
        // that only such loops use, whose declarations go.
        void prepareBody(LSLASTNode* body);
        void numericFor(LSLForStatement* f, const Counting& c);

        // --- truth values ---------------------------------------------------------

        // The integers the script uses only as truth values, and its
        // functions that answer only one, which are written as Luau's
        // booleans: never stepped or added to, and read only as a condition,
        // an operand of !, && or ||, returned by such a function, copied into
        // another such, or -- where only ever TRUE or FALSE -- compared with
        // one of them.
        void findBooleans();
        // Whether an expression is only ever TRUE or FALSE, given those
        // taken to be so far.
        bool zeroOrOne(LSLExpression* e) const;
        // Whether a read of a variable or a call is one a truth value stands
        // for, given those taken as booleans so far.
        bool readAsTruth(LSLASTNode* read) const;
        // What a truth value is set to: true and false for whole numbers,
        // else the value as a condition.
        std::string truthOf(LSLExpression* e);
        // A truth value compared with TRUE or FALSE: itself, or not it.
        std::optional<Expr> truthAsked(LSLBinaryExpression* e, LSLOperator op);
        // An if and its else that give one boolean TRUE and FALSE: which,
        // and whether TRUE where the check holds.
        bool booleanChoice(LSLIfStatement* i, LSLIdentifier*& var, bool& when_true);
        // Whether a statement is the last its event handler runs: the last
        // of the handler's body, or of an if's branch that is.
        static bool handlerTail(LSLASTNode* s);
        bool        boolean(LSLSymbol* var) const { return var && mBooleans.contains(var); }

        // --- LSL's order ----------------------------------------------------------------

        // Whether a binary's sides run the other way round could do or give
        // something else: one changes what the other reads, or both do
        // something past the script. LSL ran the right side first, and Luau
        // runs the left.
        bool orderMatters(LSLExpression* lhs, LSLExpression* rhs) const;
        // l = (l = []) + l + ..., which spared LSO's memory by clearing l
        // after LSL, going right to left, had read it: the clearing taken
        // out of the tree, since Luau would clear it first, and what is left
        // is the append it meant.
        void forgetMemoryHacks();

        // --- lists grown in place --------------------------------------------------

        // The list variables no other holds, which may be grown in place:
        // only ever given a new list, and never handed on whole -- to
        // another variable, a function of the script's own that keeps it, or
        // a return but a local's, which ends with it -- nor set where the
        // assignment's value is used. Those that are not, with why, for the
        // note. And the list parameters that only borrow: never set, and
        // never handed on, so a list passed to one is not kept.
        void findOwnedLists();
        bool owned(LSLSymbol* var) const { return var && mOwned.contains(var); }
        // What makes a list no other holds: [...], a library call, +, a cast
        // to a list, a local list no other holds, or a call to a function of
        // the script's own that only ever returns one of those.
        bool fresh(LSLExpression* e) const;
        // A list made where it is read: written out, or a library call's,
        // but not another +'s. What a table function is given with it,
        // Luau's old solver would hold to the type of its first item, so it
        // is said to hold any value, as LSL's lists did.
        bool        madeHere(LSLExpression* e) const;
        std::string anyItems(LSLExpression* e, const Expr& written) const;
        // The one item of a list written out, or null where it has more or
        // fewer; and an item written as the list holds it.
        LSLASTNode* soleItem(LSLExpression* e) const;
        std::string itemText(LSLASTNode* item);
        // Why a read of a list hands the list itself on, or null where it
        // does not.
        Shared handedOn(LSLASTNode* read) const;
        // l += x, l += [x, y], l += other, l = l + x and l = x + l written
        // as table.insert or table.extend on l; false where it is none of
        // them, l is held elsewhere -- noted, with why -- or what is added
        // could change l.
        bool grow(LSLLValueExpression* target, const std::string& name, LSLOperator op, LSLExpression* rhs);
        // l = llDeleteSubList(l, 0, 0) or (l, -1, -1), l =
        // llListInsertList(l, [x], 0) or (l, [x], llGetListLength(l)), on a
        // list no other holds, as table.remove and table.insert on it: at
        // its front or its back, which no index can fall past. And l =
        // llListReplaceList(l, [x], i, i) at a counter within the list, as
        // l[i + 1] = x.
        bool editInPlace(LSLSymbol* var, const std::string& name, LSLExpression* rhs);

        // --- strings built in loops -------------------------------------------------

        // For each string local, the outermost loop it is only ever
        // appended to in -- s += x, s = s + x + y -- never read otherwise,
        // and declared outside of: written with its pieces put in a table
        // and joined once after. Appends in a loop that cannot be are noted.
        void findStringBuilds();
        // A loop that builds strings, written between its table and the join.
        void buildingLoop(LSLASTNode* loop, bool last);
        // What an append to a string adds, in order; empty where the node
        // is not one.
        static std::vector<LSLExpression*> appended(LSLASTNode* node, LSLSymbol*& var);
        // A name no other in the script has, nor SLua holds.
        std::string freshName(const std::string& base);
        // Every name the script has, and those made fresh, in mTaken.
        void takeNames();

        // --- steps as statements -----------------------------------------------------

        // The steps and assignments inside a statement's expression that can
        // be statements of their own: its variable, whole, read nowhere else
        // in it, and -- a global -- no function of the script's called in it
        // to see it changed early or late. ++x and x = v go before the
        // statement, x++ after it where `after` allows. Each is marked, and
        // stands in the expression as its variable. The root itself too, but
        // where it is a statement's own, which is made as a statement anyway.
        struct Steps
        {
            std::vector<LSLExpression*> before;
            std::vector<LSLExpression*> after;
        };
        Steps hoistSteps(LSLExpression* root, bool after, bool statement = false);
        void  writeSteps(const std::vector<LSLExpression*>& steps);
        boost::unordered_flat_set<LSLExpression*> mHoisted;

        // --- list item types ---------------------------------------------------------

        // The types a list's items may have, as bits: what each list
        // variable, and each function of the script's own answering a list,
        // is given -- literals, llParseString2List's strings, the lists it is
        // made from -- until no more are. Anything else may hold anything.
        enum ItemTypes : U8
        {
            ItemInteger  = 1 << 0,
            ItemFloat    = 1 << 1,
            ItemString   = 1 << 2,
            ItemKey      = 1 << 3,
            ItemVector   = 1 << 4,
            ItemRotation = 1 << 5,
            ItemAny      = (1 << 6) - 1,
        };
        void findListTypes();
        U8   itemTypes(LSLExpression* e);
        U8   itemBit(LSLExpression* e);
        // llList2String and its kin as an index into the list, where the
        // list's items are already what is asked for: l[i + 1], or LSL's
        // empty value past the end.
        std::optional<Expr> listItem(LSLFunctionExpression* e, const std::string& lsl);
        // Whether listItem writes a call as an index into its list: its
        // items already what is asked for.
        bool listItemFits(LSLFunctionExpression* e);

        // --- keys that hold text -----------------------------------------------------

        // LSL's keys hold any text; SLua's uuid() stops the script on text
        // that is no UUID. The event parameters SLua passes as text --
        // link_message's id -- and the key variables given text that is no
        // UUID, or such a parameter, or another such, which SLua holds as
        // strings.
        void findTextKeys();
        // An expression's type as SLua has it: LSL's, but a key for the
        // constants SLua types a uuid, and a string for those above.
        LSLIType slType(LSLExpression* e) const;
        // A variable's type as SLua holds it.
        LSLIType varType(LSLSymbol* var, LSLIType lsl) const
        {
            return var && (mTextKeys.contains(var) || mTextParams.contains(var)) ? LST_STRING : lsl;
        }

        // x = x op y, which Luau writes x op= y: the compound operator and
        // what follows it -- for a string, each piece of s = s + a + b, as
        // Luau's .. joins them whichever way round. False for any other.
        static bool selfOperation(LSLSymbol* var, LSLIType type, LSLExpression* rhs, LSLOperator& op, std::vector<LSLExpression*>& parts);

        // --- the script -------------------------------------------------------------

        void globals();
        void functions();
        void states();
        void singleState(LSLState* state);
        void multiState();
        // What a script of several states has before anything that could
        // change state: the tables and setState; and the timer on LLTimers,
        // before anything that could set it.
        void statesPreamble();
        void timersPreamble();
        // Whether every state has a timer handler that turns the timer off
        // before it does anything else: what makes the timer a one-shot
        // each time it is set, wherever the script is when it runs. A state
        // with none would let LSL's go on running, for the next state's.
        bool oneShotTimer() const;
        std::string handlerParams(LSLEventHandler* handler, std::vector<std::string>& lead);
        void handlerBody(LSLEventHandler* handler);
        void helpers(std::string& out);

        LSLScript*                                       mScript;
        std::string_view                                 mSource;
        const ALLSLToSLua::Options&                      mOptions;
        // What each part of the script may change, for LSL's order.
        ALLSLEffects                                     mEffects;
        std::string                                      mText;
        // Where the block being written began in mText: no blank line kept
        // over what is written first in it.
        size_t                                           mBlockStart = std::string::npos;
        // The LSL's stretches beside the SLua's made of them, the SLua's
        // counted in mText until the head goes over it, and its last lines
        // worked out once all is written (endSpans); the LSL lines of the
        // node whose first line is written next, once its comments are; and
        // how many lines mText was counted to hold, and to where.
        std::vector<ALLSLToSLua::Span>                   mSpans;
        SamePairs                                        mSame;
        // An LSL word written otherwise here, kept in mSame.
        void                                             sameAs(std::string_view lsl, std::string_view slua);
        std::optional<std::pair<S32, S32>>               mAnchorPending;
        void                                             endSpans();
        size_t                                           mCounted      = 0;
        S32                                              mCountedLines = 0;
        S32                                              linesWritten();
        int                                              mDepth = 0;
        std::vector<std::string>                         mPending;
        boost::unordered_flat_set<std::string>           mOnce;
        ALScriptProblems                                 mNotes;
        boost::unordered_flat_map<LSLSymbol*, std::string> mNames;
        boost::unordered_flat_set<std::string>           mTaken;
        // What the text needs of its own, defined once over it.
        // The script's comments; those over its first global, written at the
        // top; and by what each is given to.
        std::vector<Comment>                                         mComments;
        std::vector<size_t>                                          mLeadComments;
        boost::unordered_flat_map<LSLASTNode*, std::vector<size_t>> mCommentsBefore;
        boost::unordered_flat_map<LSLASTNode*, std::vector<size_t>> mCommentsAfter;
        boost::unordered_flat_map<LSLASTNode*, std::vector<size_t>> mCommentsWithin;
        boost::unordered_flat_map<LSLASTNode*, std::vector<size_t>> mCommentsOpening;
        // The globals the script never sets, declared a number not below
        // nought; and declared any whole number, by what it is.
        boost::unordered_flat_set<LSLSymbol*>      mSteadyNonNegative;
        boost::unordered_flat_map<LSLSymbol*, int> mSteadyWhole;
        bool mLslInteger = false;
        bool mLslFloat   = false;
        bool mVecNorm    = false;
        bool mManyStates = false;
        // llSetTimerEvent's timer on LLTimers, where the script sets one;
        // on LLTimers:once, where it is a one-shot (oneShotTimer).
        bool mTimers  = false;
        bool mOneShot = false;
        // Where the script resets its time: a clock of its own, timeBase,
        // that ll.GetTime is read from and each reset sets again, as SLua's
        // ll has no ResetTime and llcompat's may not move the clock ll and
        // LLTimers share; or, where SLua's ll is not asked for, llcompat's
        // GetTime beside its resets. And the helper llGetAndResetTime is
        // read through, where one is.
        bool mTimeBase        = false;
        bool mCompatTime      = false;
        bool mGetAndResetTime = false;
        // Inside a handler of an event SLua hands what was detected, where
        // the detected table is read; and inside on_damage's, whose table
        // alone can adjust the damage.
        bool mInDetected = false;
        bool mOnDamage   = false;
        // The loops being written, innermost last: a for's steps, which a
        // jump to its end runs before `continue`.
        std::vector<LSLASTNode*> mLoops;
        // The labels a jump reaches that SLua has nothing for.
        boost::unordered_flat_set<LSLSymbol*> mUnstructured;
        // Whether a `return` from the handler being written ends the
        // script's setup: state_entry of a script of one state, written at
        // the top level.
        bool mTopLevel = false;
        // Lines made for an expression's function, whose notes go over the
        // statement it stands in.
        bool mInline = false;
        // The for loops of the body being written that are Luau's numeric
        // for, and the variables used by nothing else.
        boost::unordered_flat_map<LSLForStatement*, Counting> mNumeric;
        boost::unordered_flat_set<LSLSymbol*>                 mNonNegative;
        boost::unordered_flat_set<LSLSymbol*>                 mFromOne;
        // Counters within a list's length, i < llGetListLength(l): the list.
        boost::unordered_flat_map<LSLSymbol*, LSLSymbol*>     mWithin;
        // Counters whose loop walks their list's items instead: the item's
        // name. A name for each depth of such loops, made once.
        boost::unordered_flat_map<LSLSymbol*, std::string>    mEach;
        std::vector<std::string>                              mItemNames;
        std::string                                           mUnread;
        boost::unordered_flat_map<LSLSymbol*, U8>             mListTypes;
        boost::unordered_flat_set<LSLSymbol*>                 mLoopOnly;
        boost::unordered_flat_set<LSLSymbol*>                 mBooleans;
        boost::unordered_flat_set<LSLSymbol*>                 mZeroOne;
        // The function of the script's own being written.
        LSLSymbol* mFunction = nullptr;
        // The assignments whose clearing was taken out, noted as each is
        // written.
        boost::unordered_flat_set<LSLASTNode*> mFreedFirst;
        boost::unordered_flat_set<LSLSymbol*>  mOwned;
        boost::unordered_flat_set<LSLSymbol*>  mFreshFunctions;
        boost::unordered_flat_set<LSLSymbol*>  mBorrows;
        // Why a list variable is not one of mOwned.
        boost::unordered_flat_map<LSLSymbol*, Shared>      mSharedWhy;
        // The strings each loop builds, and those being built, by the
        // table of their pieces.
        boost::unordered_flat_map<LSLASTNode*, std::vector<LSLSymbol*>> mBuilds;
        // A string a loop builds, declared empty in the loop's own block
        // with nothing between that touches it: declared where its pieces
        // are joined instead, its declaration not written.
        boost::unordered_flat_set<LSLASTNode*>                          mJoinedDeclarations;
        boost::unordered_flat_set<std::pair<LSLASTNode*, LSLSymbol*>>   mDeclaredAtJoin;
        boost::unordered_flat_map<LSLSymbol*, std::string>              mBuilding;
        // Appends in loops that are noted rather than built.
        boost::unordered_flat_set<LSLASTNode*> mUnbuilt;
        boost::unordered_flat_set<LSLSymbol*>  mTextParams;
        // Comparisons written the other way round, a not before them.
        boost::unordered_flat_set<LSLASTNode*> mFlipped;
        boost::unordered_flat_set<LSLSymbol*>  mTextKeys;
    };

    std::string Writer::said(const char* key, const char* english, const std::vector<std::string>& args) const
    {
        return mOptions.words ? mOptions.words(key, args, english) : ALScriptProblem::fill(english, args);
    }

    void Writer::note(LSLASTNode* at, const char* key, const char* english, std::vector<std::string> args)
    {
        std::string words = said(key, english, args);
        if (std::find(mPending.begin(), mPending.end(), words) != mPending.end())
        {
            return;
        }
        mPending.push_back(words);
        ALScriptProblem p;
        p.severity = ALScriptProblem::Severity::Note;
        p.source   = ALScriptProblem::Source::Assistant;
        p.key      = key;
        p.args     = std::move(args);
        p.message  = std::move(words);
        if (const char* lint = ALLSLToSLua::lintOf(key))
        {
            p.code = lint;
        }
        if (at)
        {
            p.line      = zeroBased(at->getLoc()->first_line);
            p.column    = zeroBased(at->getLoc()->first_column);
            p.endLine   = zeroBased(at->getLoc()->last_line);
            p.endColumn = std::max(0, at->getLoc()->last_column);
        }
        mNotes.push_back(std::move(p));
    }

    std::string Writer::sharedWords(Shared why) const
    {
        switch (why)
        {
            case Shared::Assigned: return said("SluaSharedAssigned", "set where the assignment's value is used");
            case Shared::Called: return said("SluaSharedCalled", "read where a call of the script's in the same statement changes it");
            case Shared::Held: return said("SluaSharedHeld", "given a list another may hold");
            case Shared::Given: return said("SluaSharedGiven", "given to another variable");
            case Shared::Returned: return said("SluaSharedReturned", "returned by a function");
            case Shared::Kept: return said("SluaSharedKept", "passed to a function of the script's that keeps it");
            case Shared::None: break;
        }
        return {};
    }

    void Writer::noteOnce(LSLASTNode* at, const char* key, const char* english, std::vector<std::string> args)
    {
        std::string once = key;
        for (const std::string& arg : args)
        {
            once += '\x1f' + arg;
        }
        if (mOnce.insert(once).second)
        {
            note(at, key, english, std::move(args));
        }
    }

    std::string Writer::nameOf(LSLIdentifier* id)
    {
        LSLSymbol* symbol = id ? id->getSymbol() : nullptr;
        if (symbol)
        {
            if (const auto found = mNames.find(symbol); found != mNames.end())
            {
                return found->second;
            }
        }
        std::string name = nameOf(id ? id->getName() : "");
        if (symbol)
        {
            mNames.emplace(symbol, name);
        }
        return name;
    }

    std::string Writer::nameOf(const char* lsl)
    {
        // A name Luau or SLua holds for its own, with a mark after it.
        std::string name = lsl ? lsl : "";
        while (reservedName(name))
        {
            name += "_";
        }
        if (lsl)
        {
            sameAs(lsl, name);
        }
        return name;
    }

    void Writer::sameAs(std::string_view lsl, std::string_view slua)
    {
        if (lsl != slua && mSame.find(std::pair<std::string_view, std::string_view>(lsl, slua)) == mSame.end())
        {
            mSame.emplace(lsl, slua);
        }
    }

    std::string Writer::stateKey(const std::string& state) const
    {
        return reservedName(state) ? "[" + luaString(state) + "]" : "." + state;
    }

    void Writer::line(const std::string& text)
    {
        // What was noted of it, over it -- over the statement a line made
        // inside an expression stands in, where it is one -- under the
        // comments put off to it that it has no place for.
        if (!mInline)
        {
            const auto has_mark = [&text](size_t k) {
                const std::string head = COMMENT_MARK + std::to_string(k);
                for (size_t at = text.find(head); at != std::string::npos; at = text.find(head, at + 1))
                {
                    if (at + head.size() < text.size() && std::isalpha(static_cast<unsigned char>(text[at + head.size()])))
                    {
                        return true;
                    }
                }
                return false;
            };
            for (size_t k : std::exchange(mDeferred, {}))
            {
                if (!has_mark(k))
                {
                    writeComment(mComments[k]);
                }
            }
            for (const std::string& said : mPending)
            {
                if (mOptions.comments)
                {
                    mText += indent() + std::string(NOTE_MARK) + " " + said + "\n";
                }
            }
            mPending.clear();
        }
        if (mAnchorPending && !mInline && !text.empty())
        {
            const S32 at = linesWritten();
            mSpans.push_back(ALLSLToSLua::Span{ mAnchorPending->first, mAnchorPending->second, at, at });
            mAnchorPending.reset();
        }
        // A block LSL opened and closed with braces.
        const std::string_view written(text);
        if (written == "do" || (written.size() > 3 && written.substr(written.size() - 3) == " do"))
        {
            sameAs("{", "do");
        }
        else if (written.size() > 5 && written.substr(written.size() - 5) == " then")
        {
            sameAs("{", "then");
        }
        if (written.substr(0, 3) == "end" && (written.size() == 3 || !(std::isalnum(static_cast<unsigned char>(written[3])) || written[3] == '_')))
        {
            sameAs("}", "end");
        }
        mText += text.empty() ? std::string("\n") : indent() + withComments(text) + "\n";
    }

    // static
    std::string Writer::marked(size_t k, Marked how)
    {
        return COMMENT_MARK + std::to_string(k) + static_cast<char>(how) + COMMENT_MARK;
    }

    std::string Writer::withComments(const std::string& text)
    {
        if (text.find(COMMENT_MARK) == std::string::npos)
        {
            return text;
        }
        std::string out;
        size_t      at = 0;
        while (at < text.size())
        {
            const size_t mark = text.find(COMMENT_MARK, at);
            if (mark == std::string::npos)
            {
                out.append(text, at, std::string::npos);
                break;
            }
            out.append(text, at, mark - at);
            const size_t close = text.find(COMMENT_MARK, mark + 1);
            const size_t k     = std::strtoul(text.c_str() + mark + 1, nullptr, 10);
            const Marked how   = static_cast<Marked>(text[close - 1]);
            at                 = close + 1;
            Comment& c         = mComments[k];
            if (!c.written)
            {
                c.written = true;
                out += how == Marked::After ? " " + c.text : how == Marked::Before ? c.text + " " : c.text;
            }
            else if (how == Marked::Own)
            {
                // Its line too, break and all.
                const size_t line = out.rfind('\n');
                out.erase(line == std::string::npos ? 0 : line);
            }
        }
        return out;
    }

    S32 Writer::linesWritten()
    {
        // Appended to but for an assignment in an expression, written apart
        // and put back as it was.
        if (mCounted > mText.size())
        {
            mCounted      = 0;
            mCountedLines = 0;
        }
        mCountedLines += static_cast<S32>(std::count(mText.begin() + static_cast<std::ptrdiff_t>(mCounted), mText.end(), '\n'));
        mCounted = mText.size();
        return mCountedLines;
    }

    // --- the script's own comments -----------------------------------------------------

    namespace
    {
        // The runs of a mark a comment's text begins and ends with, as
        // dashes: a rule, or the rule a heading stands in.
        void dashed(std::string& text, char mark)
        {
            for (size_t i = 0; i < text.size() && text[i] == mark; ++i)
            {
                text[i] = '-';
            }
            for (size_t i = text.size(); i > 0 && text[i - 1] == mark; --i)
            {
                text[i - 1] = '-';
            }
        }
    }

    void Writer::findComments()
    {
        if (!mOptions.keepComments)
        {
            return;
        }
        // Each comment, past strings, where it begins.
        const std::string_view src    = mSource;
        Pos                    at     = { 1, 1 };
        const auto             step   = [&](char c) {
            if (c == '\n')
            {
                ++at.line;
                at.column = 1;
            }
            else
            {
                ++at.column;
            }
        };
        // The byte a toggle's level is marked with: one the script has none
        // of, so nothing written of it has one either.
        for (char c = 1; c < ' ' && !mToggleMark; ++c)
        {
            if (c != '\t' && c != '\n' && c != '\r' && src.find(c) == std::string_view::npos)
            {
                mToggleMark = c;
            }
        }
        const std::string mark = mToggleMark ? std::string(1, mToggleMark) : std::string();
        // The openers since the last closer, by the comment and what follows
        // the star; and each opener with the closer it was paired with.
        std::vector<std::pair<size_t, std::string>> pending;
        std::vector<std::pair<size_t, size_t>>      pairs;
        for (size_t i = 0; i < src.size();)
        {
            const char c = src[i];
            if (c == '"')
            {
                step(src[i++]);
                while (i < src.size() && src[i] != '"')
                {
                    if (src[i] == '\\' && i + 1 < src.size())
                    {
                        step(src[i++]);
                    }
                    step(src[i++]);
                }
                if (i < src.size())
                {
                    step(src[i++]);
                }
                continue;
            }
            if (c != '/' || i + 1 >= src.size() || (src[i + 1] != '/' && src[i + 1] != '*'))
            {
                step(src[i++]);
                continue;
            }
            Comment k;
            k.at = at;
            size_t end;
            // //* over code and //*/ under it, which a / taken out of the
            // first makes a block comment: ---[[ and --]], which a - taken
            // out of the first makes one. A closer is one whatever is before
            // it, and so is a block comment that ends in one, the toggle
            // turned off; an opener is one where something after it closes
            // it, and a comment that begins with a star where nothing does.
            bool closes = false;
            if (src[i + 1] == '/')
            {
                end              = std::min(src.find('\n', i), src.size());
                std::string body = std::string(src.substr(i + 2, end - i - 2));
                while (!body.empty() && (body.back() == '\r' || body.back() == ' ' || body.back() == '\t'))
                {
                    body.pop_back();
                }
                // Not a block comment of Luau's, which --[[ would begin.
                if (!body.empty() && body[0] == '[')
                {
                    body = " " + body;
                }
                k.text = "--" + body;
                if (body.compare(0, 2, "*/") == 0)
                {
                    // What follows it is the LSL's code once the toggle is
                    // off, and a comment there SLua's.
                    std::string  rest = body.substr(2);
                    const size_t own  = rest.find_first_not_of(" \t");
                    if (own != std::string::npos && rest.compare(own, 2, "//") == 0)
                    {
                        const std::string said = rest.substr(own + 2);
                        rest                   = rest.substr(0, own) + "--" + (!said.empty() && said[0] == '[' ? " " : "") + said;
                    }
                    k.text   = "--]" + mark + "]" + rest;
                    k.closer = true;
                    closes   = true;
                }
                else if (!body.empty() && body[0] == '*' && (body.size() == 1 || (body[1] != '*' && body[1] != '/')) &&
                         body.find("*/") == std::string::npos)
                {
                    // A word after the star or not, but no rule of stars, nor
                    // a block comment closed on its line once a / is out.
                    pending.emplace_back(mComments.size(), body.substr(1));
                }
                else if (!body.empty() && body[0] == '/')
                {
                    // A rule of slashes, a heading in it or not: of dashes.
                    dashed(body, '/');
                    k.text = "--" + body;
                }
            }
            else
            {
                const size_t close = src.find("*/", i + 2);
                end                = close == std::string_view::npos ? src.size() : close + 2;
                std::string body   = std::string(src.substr(i + 2, (close == std::string_view::npos ? src.size() : close) - i - 2));
                // The toggle's other way, /* over code and //*/ under it: --]]
                // under it, which ---[[ over it would leave a line comment.
                const bool toggled = body.size() >= 2 && body.compare(body.size() - 2, 2, "//") == 0 &&
                                     body.find('\n') != std::string::npos;
                if (toggled)
                {
                    body.resize(body.size() - 2);
                }
                // Brackets of a level nothing in it closes.
                std::string level;
                while (body.find("]" + level + "]") != std::string::npos)
                {
                    level += "=";
                }
                if (!body.empty() && body.back() == ']')
                {
                    body += " ";
                }
                // A toggle's, marked: its level is what nothing from the
                // first opener over it closes.
                const std::string bracket = toggled && !mark.empty() ? mark : level;
                k.text                    = "--[" + bracket + "[" + body + (toggled ? "--" : "") + "]" + bracket + "]";
                closes                    = toggled;
                if (body.size() > 1 && body.front() == '*' && body.back() == '*' && body.find('\n') == std::string::npos)
                {
                    // A rule of stars on its line, a heading in it or not: of
                    // dashes, as long.
                    dashed(body, '*');
                    k.text = "--" + body + "--";
                }
            }
            // A comment that reads as one of the notes this writes is not one.
            if (k.text.rfind(NOTE_MARK, 0) == 0)
            {
                k.text.insert(2, " ");
            }
            while (i < end)
            {
                step(src[i++]);
            }
            k.endLine = at.line;
            if (closes)
            {
                for (const auto& [opener, rest] : pending)
                {
                    mComments[opener].text = "---[" + mark + "[" + rest;
                    pairs.emplace_back(opener, mComments.size());
                }
                pending.clear();
            }
            mComments.push_back(std::move(k));
        }
        // Whether a line of the LSL, from one, is blank.
        std::vector<size_t> starts{ 0 };
        for (size_t i = 0; i < src.size(); ++i)
        {
            if (src[i] == '\n')
            {
                starts.push_back(i + 1);
            }
        }
        const auto blank = [&](S32 line) {
            if (line < 1 || static_cast<size_t>(line) > starts.size())
            {
                return false;
            }
            const size_t from = starts[static_cast<size_t>(line) - 1];
            const size_t to   = std::min(src.find('\n', from), src.size());
            return src.substr(from, to - from).find_first_not_of(" \t\r") == std::string_view::npos;
        };
        for (Comment& c : mComments)
        {
            c.apart = blank(c.at.line - 1);
        }
        if (mComments.empty())
        {
            return;
        }
        std::vector<LSLASTNode*> top;
        for (LSLASTNode* g = mScript->getGlobals()->getChild(0); g; g = g->getNext())
        {
            top.push_back(g);
        }
        for (LSLASTNode* st = mScript->getStates()->getChild(0); st; st = st->getNext())
        {
            top.push_back(st);
        }
        // Over the first global, those with no blank line between them and it
        // are about it; those before, the script's own, go at the top.
        size_t before = 0;
        while (!top.empty() && before < mComments.size() &&
               mComments[before].at < Pos{ top.front()->getLoc()->first_line, top.front()->getLoc()->first_column })
        {
            ++before;
        }
        size_t about = before;
        S32    next  = top.empty() ? 0 : top.front()->getLoc()->first_line;
        while (about > 0 && mComments[about - 1].endLine + 1 >= next && !blank(next - 1))
        {
            --about;
            next = mComments[about].at.line;
        }
        // Not from a toggle's opener whose closer is further on: what is
        // written between the top and the code, the helpers, would be in it.
        for (const auto& [opener, closer] : pairs)
        {
            if (opener < about && closer >= about)
            {
                about = opener;
                break;
            }
        }
        for (size_t k = 0; k < mComments.size(); ++k)
        {
            if (k < about)
            {
                // Written at the top as the text is put together.
                mLeadComments.push_back(k);
                mComments[k].written = true;
                continue;
            }
            place(k, top, nullptr);
        }
    }

    namespace
    {
        // Whether what a node ends with is a statement of its own, not a
        // block's brace: a comment after it on its line is about it.
        bool endsBare(LSLASTNode* node)
        {
            switch (node->getNodeType())
            {
                case NODE_GLOBAL_VARIABLE: return true;
                case NODE_STATEMENT: break;
                default: return false;
            }
            switch (node->getNodeSubType())
            {
                case NODE_COMPOUND_STATEMENT: return false;
                case NODE_IF_STATEMENT:
                {
                    LSLASTNode* last = node->getChild(2);
                    return endsBare(isNull(last) ? node->getChild(1) : last);
                }
                case NODE_WHILE_STATEMENT: return endsBare(node->getChild(1));
                case NODE_FOR_STATEMENT: return endsBare(node->getChild(3));
                default: return true;
            }
        }
    }

    void Writer::place(size_t k, const std::vector<LSLASTNode*>& children, LSLASTNode* holder)
    {
        const Pos   at   = mComments[k].at;
        LSLASTNode* prev = nullptr;
        mCommentsWithin[holder].push_back(k);
        for (LSLASTNode* child : children)
        {
            const Pos begins = { child->getLoc()->first_line, child->getLoc()->first_column };
            const Pos ends   = { child->getLoc()->last_line, child->getLoc()->last_column };
            if (!(at < ends))
            {
                prev = child;
                continue;
            }
            if (!(at < begins))
            {
                // Inside it: in what it holds, or over it.
                if (!placeInside(k, child))
                {
                    mCommentsBefore[child].push_back(k);
                }
                return;
            }
            break;
        }
        // On the line a bare statement ends, after it: about that statement,
        // and after it too, where it is one line of the comment's and the
        // statement one line, or no if or loop, whose first line is not
        // where the comment was.
        if (prev && endsBare(prev) && at.line == prev->getLoc()->last_line)
        {
            const LSLNodeSubType type    = prev->getNodeSubType();
            const bool           control = type == NODE_IF_STATEMENT || type == NODE_WHILE_STATEMENT || type == NODE_FOR_STATEMENT ||
                                 type == NODE_DO_STATEMENT;
            if (mComments[k].endLine == at.line && (!control || prev->getLoc()->first_line == prev->getLoc()->last_line))
            {
                mCommentsAfter[prev].push_back(k);
            }
            else
            {
                mCommentsBefore[prev].push_back(k);
            }
            return;
        }
        // Over what follows it in here; or, where nothing does, at the end.
        for (LSLASTNode* child : children)
        {
            if (at < Pos{ child->getLoc()->first_line, child->getLoc()->first_column })
            {
                mCommentsBefore[child].push_back(k);
                return;
            }
        }
    }

    void Writer::placeIn(size_t k, LSLASTNode* compound)
    {
        std::vector<LSLASTNode*> children;
        for (LSLASTNode* child = compound->getChild(0); child; child = child->getNext())
        {
            children.push_back(child);
        }
        place(k, children, compound);
    }

    bool Writer::placeInside(size_t k, LSLASTNode* node)
    {
        const Pos  at     = mComments[k].at;
        const auto inside = [&at](LSLASTNode* n) {
            return !isNull(n) && !(at < Pos{ n->getLoc()->first_line, n->getLoc()->first_column }) &&
                   at < Pos{ n->getLoc()->last_line, n->getLoc()->last_column };
        };
        // A statement standing where one goes -- a body, a branch -- in what
        // it holds, or over it.
        const auto within = [&](LSLASTNode* body) {
            if (!inside(body))
            {
                return false;
            }
            if (body->getNodeSubType() == NODE_COMPOUND_STATEMENT)
            {
                // On the line its brace opens, before anything in it: after
                // the line that opens the block -- `if (i < 0) { // none
                // saved` -- a long comment running on over the lines after.
                LSLASTNode* first = body->getChild(0);
                if (at.line == body->getLoc()->first_line &&
                    (!first || at < Pos{ first->getLoc()->first_line, first->getLoc()->first_column }))
                {
                    mCommentsOpening[body].push_back(k);
                    return true;
                }
                placeIn(k, body);
            }
            else if (!placeInside(k, body))
            {
                mCommentsBefore[body].push_back(k);
            }
            return true;
        };
        switch (node->getNodeType())
        {
            case NODE_GLOBAL_FUNCTION:
                return within(static_cast<LSLGlobalFunction*>(node)->getStatements());
            case NODE_EVENT_HANDLER:
                return within(static_cast<LSLEventHandler*>(node)->getStatements());
            case NODE_STATE:
            {
                std::vector<LSLASTNode*> handlers;
                for (LSLASTNode* h = static_cast<LSLState*>(node)->getEventHandlers()->getChild(0); h; h = h->getNext())
                {
                    handlers.push_back(h);
                }
                place(k, handlers, node);
                return true;
            }
            case NODE_STATEMENT:
                switch (node->getNodeSubType())
                {
                    case NODE_COMPOUND_STATEMENT: placeIn(k, node); return true;
                    case NODE_IF_STATEMENT:
                    {
                        if (within(node->getChild(1)) || within(node->getChild(2)))
                        {
                            return true;
                        }
                        // Between the branches: over what the else runs -- an
                        // elseif's line, or the else's first statement.
                        LSLASTNode* no = node->getChild(2);
                        if (isNull(no) || !(at < Pos{ no->getLoc()->first_line, no->getLoc()->first_column }) ||
                            at < Pos{ node->getChild(1)->getLoc()->last_line, node->getChild(1)->getLoc()->last_column })
                        {
                            return false;
                        }
                        if (no->getNodeSubType() == NODE_COMPOUND_STATEMENT)
                        {
                            placeIn(k, no);
                        }
                        else
                        {
                            mCommentsBefore[no].push_back(k);
                        }
                        return true;
                    }
                    case NODE_WHILE_STATEMENT: return within(node->getChild(1));
                    case NODE_DO_STATEMENT: return within(node->getChild(0));
                    case NODE_FOR_STATEMENT: return within(node->getChild(3));
                    default: return false;
                }
            default:
                return false;
        }
    }

    void Writer::writeComment(Comment& c)
    {
        if (c.written)
        {
            return;
        }
        c.written = true;
        const bool blank_over = mText.size() >= 2 && mText.compare(mText.size() - 2, 2, "\n\n") == 0;
        if (c.closer && !c.apart && blank_over)
        {
            // Under the code it closes, over the blank line written after
            // that, which the LSL did not have.
            putTrailing(mText.size() - 1, indent() + c.text + "\n");
            return;
        }
        if (c.apart && !mText.empty() && !blank_over && mText.size() != mBlockStart)
        {
            mText += "\n";
        }
        mText += indent() + c.text + "\n";
    }

    void Writer::commentsBefore(LSLASTNode* node)
    {
        // Every node written is through here first: the first line of code
        // written after its comments is the one made of it.
        mAnchorPending.reset();
        // Any put off that no line took, over what follows.
        for (size_t k : std::exchange(mDeferred, {}))
        {
            writeComment(mComments[k]);
        }
        if (const auto found = mCommentsBefore.find(node); found != mCommentsBefore.end())
        {
            const Pos begins = isNull(node) ? Pos{} : Pos{ node->getLoc()->first_line, node->getLoc()->first_column };
            for (size_t k : found->second)
            {
                if (!isNull(node) && begins < mComments[k].at)
                {
                    mDeferred.push_back(k);
                }
                else
                {
                    writeComment(mComments[k]);
                }
            }
        }
        if (!isNull(node) && node->getLoc())
        {
            const S32 first = zeroBased(node->getLoc()->first_line);
            mAnchorPending  = std::make_pair(first, std::max(first, zeroBased(node->getLoc()->last_line)));
        }
    }

    void Writer::commentsAfter(LSLASTNode* node, size_t from)
    {
        const auto found = mCommentsAfter.find(node);
        if (found == mCommentsAfter.end())
        {
            return;
        }
        const std::string after = trailing(found->second);
        if (after.empty())
        {
            return;
        }
        // Past what was said over it, and the comments over what it holds.
        for (size_t at = from; at < mText.size();)
        {
            const size_t end = mText.find('\n', at);
            if (end == std::string::npos)
            {
                break;
            }
            const size_t first = mText.find_first_not_of(' ', at);
            if (first < end && mText.compare(first, 2, "--") != 0)
            {
                putTrailing(codeLineEnd(at), after);
                mTrailed[node] = after.size();
                return;
            }
            at = end + 1;
        }
        mText += indent() + after.substr(1) + "\n";
    }

    size_t Writer::codeLineEnd(size_t from) const
    {
        // Past strings and comments, counting brackets: Luau's, as written
        // here.
        S32    open = 0;
        size_t i    = from;
        // A long bracket's level at `at`, `[==[`, or -1 where there is none.
        const auto level = [this](size_t at) {
            size_t n = at + 1;
            while (n < mText.size() && mText[n] == '=')
            {
                ++n;
            }
            return n < mText.size() && mText[n] == '[' ? static_cast<S32>(n - at - 1) : -1;
        };
        const auto pastLong = [this](size_t at, S32 equals) {
            const std::string closer = "]" + std::string(static_cast<size_t>(equals), '=') + "]";
            const size_t      found  = mText.find(closer, at);
            return found == std::string::npos ? mText.size() : found + closer.size();
        };
        while (i < mText.size())
        {
            const char c = mText[i];
            if (c == '\n')
            {
                if (open <= 0)
                {
                    return i;
                }
                ++i;
            }
            else if (c == '"' || c == '\'' || c == '`')
            {
                for (++i; i < mText.size() && mText[i] != c && mText[i] != '\n'; ++i)
                {
                    i += mText[i] == '\\' ? 1 : 0;
                }
                ++i;
            }
            else if (c == '-' && i + 1 < mText.size() && mText[i + 1] == '-')
            {
                const S32 equals = i + 2 < mText.size() && mText[i + 2] == '[' ? level(i + 2) : -1;
                i                = equals >= 0 ? pastLong(i + 2, equals) : std::min(mText.find('\n', i), mText.size());
            }
            else if (c == '[' && level(i) >= 0)
            {
                i = pastLong(i, level(i));
            }
            else
            {
                open += c == '(' || c == '{' || c == '[' ? 1 : c == ')' || c == '}' || c == ']' ? -1 : 0;
                ++i;
            }
        }
        return std::min(mText.find('\n', from), mText.size());
    }

    bool Writer::trail(LSLASTNode* body)
    {
        const auto found = mCommentsOpening.find(body);
        if (found == mCommentsOpening.end() || mText.empty() || mText.back() != '\n')
        {
            return false;
        }
        // At the end of the line just written, its break kept.
        bool              takes_line = false;
        const std::string after      = trailing(found->second, &takes_line);
        putTrailing(mText.size() - 1, after);
        return takes_line;
    }

    void Writer::commentsOpening(LSLASTNode* body)
    {
        if (const auto opening = mCommentsOpening.find(body); opening != mCommentsOpening.end())
        {
            for (size_t k : opening->second)
            {
                writeComment(mComments[k]);
            }
        }
    }

    std::string Writer::trailing(const std::vector<size_t>& given, bool* takes_line)
    {
        std::string after;
        for (size_t k : given)
        {
            Comment& c = mComments[k];
            if (c.written)
            {
                continue;
            }
            c.written = true;
            after += " " + c.text;
            // A line's comment, which takes the rest of its line: not a long
            // one, `--[[ ]]` or `--[==[ ]==]`, which closes where it is, nor
            // a toggle's, its level marked.
            size_t level = 2;
            if (level < c.text.size() && c.text[level] == '[')
            {
                ++level;
                while (level < c.text.size() && (c.text[level] == '=' || (mToggleMark && c.text[level] == mToggleMark)))
                {
                    ++level;
                }
                if (level < c.text.size() && c.text[level] == '[')
                {
                    continue;
                }
            }
            if (takes_line)
            {
                *takes_line = true;
            }
        }
        return after;
    }

    void Writer::putTrailing(size_t end, const std::string& after)
    {
        mText.insert(end, after);
        // Counted past it: as many more, further on, and the lines a long
        // comment runs on over counted as written before it.
        if (end < mCounted)
        {
            mCounted += after.size();
            mCountedLines += static_cast<S32>(std::count(after.begin(), after.end(), '\n'));
        }
    }

    void Writer::commentsAtEnd(LSLASTNode* holder)
    {
        const auto found = mCommentsWithin.find(holder);
        if (found == mCommentsWithin.end())
        {
            return;
        }
        for (size_t k : found->second)
        {
            writeComment(mComments[k]);
        }
    }

    // --- expressions ------------------------------------------------------------------

    bool intoBit32(LSLASTNode* e);
    bool wholeNumber(LSLExpression* e, int& v);
    namespace
    {
        LSLSymbol* setBy(LSLASTNode* node);
    }

    Expr Writer::constant(LSLConstant* c)
    {
        switch (c->getNodeSubType())
        {
            case NODE_INTEGER_CONSTANT:
            {
                // Hexadecimal as the LSL wrote it, where SLua reads the same
                // number, or where bit32 takes it, which takes one past
                // 0x7FFFFFFF as the number below nought LSL wrapped it to:
                // one constant either way. Elsewhere such a one as LSL had
                // it, which bit32.s32 of the hexadecimal would make a call
                // that SLua's compiler does not fold.
                const int v = static_cast<LSLIntegerConstant*>(c)->getValue();
                // What it is the value of: a declaration of a variable only
                // bit32 reads, or what bit32 takes.
                LSLASTNode* holder = c->getParent();
                while (holder && (holder->getNodeSubType() == NODE_CONSTANT_EXPRESSION || holder->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION))
                {
                    holder = holder->getParent();
                }
                const std::optional<std::string> hex = hexAsWritten(c, v);
                // Past 0x7FFFFFFF: what SLua reads it as, said once.
                if (hex && v < 0 && !mSaidHexSign)
                {
                    mSaidHexSign = true;
                    note(c, "SluaHexSign",
                         "SLua reads [1] as [2], where LSL wrapped it to [3]: bit32 takes either the same, so it stays as written where only "
                         "bit32 reads it, and is [3] where it is read as a number.",
                         { *hex, std::to_string(static_cast<U32>(v)), std::to_string(v) });
                }
                if (hex && (v >= 0 || mBitsOnly.contains(holder) || (c->getParent() && intoBit32(c->getParent()))))
                {
                    return { *hex, PRIMARY };
                }
                // A number past 0xFFFFFFFF, which the grid's 32-bit hosts
                // stop at: -1 once signed, as written here.
                if (v == -1 && !static_cast<LSLIntegerConstant*>(c)->wasNegated())
                {
                    for (const auto& [word, value] : numbersNear(c))
                    {
                        if (value > 0xFFFFFFFFull)
                        {
                            noteOnce(c, "SluaIntegerPast32Bits", "LSL reads [1] as -1: past 0xFFFFFFFF, the most its 32 bits hold, a number stops there.",
                                     { word });
                            break;
                        }
                    }
                }
                return { std::to_string(v), v < 0 ? UNARY : PRIMARY };
            }
            case NODE_FLOAT_CONSTANT:
            {
                const double v = static_cast<LSLFloatConstant*>(c)->getValue();
                return { number(v), std::signbit(v) ? UNARY : PRIMARY };
            }
            case NODE_KEY_CONSTANT:
                return { "uuid(" + luaString(static_cast<LSLKeyConstant*>(c)->getValue()) + ")" };
            case NODE_STRING_CONSTANT:
                return { luaString(static_cast<LSLStringConstant*>(c)->getValue()) };
            case NODE_VECTOR_CONSTANT:
            {
                const Vector3* v = static_cast<LSLVectorConstant*>(c)->getValue();
                sameAs("<", "vector(");
                sameAs(">", ")");
                return { "vector(" + number(v->x) + ", " + number(v->y) + ", " + number(v->z) + ")" };
            }
            case NODE_QUATERNION_CONSTANT:
            {
                const Quaternion* q = static_cast<LSLQuaternionConstant*>(c)->getValue();
                sameAs("<", "quaternion(");
                sameAs(">", ")");
                return { "quaternion(" + number(q->x) + ", " + number(q->y) + ", " + number(q->z) + ", " + number(q->s) + ")" };
            }
            case NODE_LIST_CONSTANT:
            {
                std::string out;
                for (LSLConstant* item = static_cast<LSLListConstant*>(c)->getValue(); item; item = static_cast<LSLConstant*>(item->getNext()))
                {
                    out += (out.empty() ? "" : ", ") + constant(item).text;
                }
                return { "{" + out + "}" };
            }
            default:
                return { "nil" };
        }
    }

    const std::vector<size_t>& Writer::lineStarts()
    {
        if (mLineStarts.empty())
        {
            mLineStarts.push_back(0);
            for (size_t i = 0; i < mSource.size(); ++i)
            {
                if (mSource[i] == '\n')
                {
                    mLineStarts.push_back(i + 1);
                }
            }
        }
        return mLineStarts;
    }

    bool Writer::blankBetween(S32 after, S32 before)
    {
        const std::vector<size_t>& starts = lineStarts();
        for (S32 line = std::max(after + 1, 1); line < before && static_cast<size_t>(line) <= starts.size(); ++line)
        {
            const size_t from = starts[static_cast<size_t>(line) - 1];
            const size_t to   = std::min(mSource.find('\n', from), mSource.size());
            if (mSource.substr(from, to - from).find_first_not_of(" \t\r") == std::string_view::npos)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<std::pair<std::string, unsigned long long>> Writer::numbersNear(LSLConstant* c)
    {
        std::vector<std::pair<std::string, unsigned long long>> found;
        const auto*                                             loc = c->getLoc();
        if (!loc || loc->first_line < 1)
        {
            return found;
        }
        lineStarts();
        // Each number on a line, past strings and comments.
        const auto onLine = [&](S32 line) {
            if (line < 1 || static_cast<size_t>(line) > mLineStarts.size())
            {
                return;
            }
            const size_t           from = mLineStarts[static_cast<size_t>(line) - 1];
            const std::string_view text = mSource.substr(from, std::min(mSource.find('\n', from), mSource.size()) - from);
            for (size_t i = 0; i < text.size();)
            {
                const char ch = text[i];
                if (ch == '"')
                {
                    for (++i; i < text.size() && text[i] != '"'; ++i)
                    {
                        i += text[i] == '\\' ? 1 : 0;
                    }
                    ++i;
                    continue;
                }
                if (ch == '/' && i + 1 < text.size() && (text[i + 1] == '/' || text[i + 1] == '*'))
                {
                    break;
                }
                const bool starts = std::isdigit(static_cast<unsigned char>(ch)) &&
                                    (i == 0 || !(std::isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '_' || text[i - 1] == '.'));
                if (!starts)
                {
                    ++i;
                    continue;
                }
                size_t end = i;
                while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '.' || text[end] == '_'))
                {
                    ++end;
                }
                const std::string word(text.substr(i, end - i));
                const bool        hex = word.size() > 2 && word[0] == '0' && (word[1] == 'x' || word[1] == 'X') &&
                                 word.find_first_not_of("0123456789abcdefABCDEF", 2) == std::string::npos;
                if (hex || word.find_first_not_of("0123456789") == std::string::npos)
                {
                    // Past what strtoull holds: past 32 bits all the same.
                    errno                          = 0;
                    const unsigned long long value = std::strtoull(word.c_str(), nullptr, hex ? 16 : 10);
                    found.emplace_back(word, errno == ERANGE ? ~0ull : value);
                }
                i = end;
            }
        };
        onLine(loc->first_line);
        if (found.empty())
        {
            onLine(loc->first_line - 1);
        }
        return found;
    }

    std::optional<std::string> Writer::hexAsWritten(LSLConstant* c, int v)
    {
        // Hexadecimal of 32 bits at most whose bits are the value, LSL's
        // own reading of it, where no number written another way is.
        std::optional<std::string> hex;
        for (const auto& [word, value] : numbersNear(c))
        {
            const bool is_hex = word.size() > 2 && (word[1] == 'x' || word[1] == 'X');
            if (is_hex && value <= 0xFFFFFFFFull && static_cast<U32>(value) == static_cast<U32>(v))
            {
                hex = word;
            }
            else if (!is_hex && (value == static_cast<unsigned long long>(std::llabs(static_cast<long long>(v)))))
            {
                return std::nullopt;
            }
        }
        return hex;
    }

    void Writer::findBitsOnly()
    {
        // The declarations of integers given such a number.
        boost::unordered_flat_map<LSLSymbol*, LSLASTNode*> given;
        walk(mScript, [&](LSLASTNode* node) {
            LSLIdentifier* id   = nullptr;
            LSLExpression* init = nullptr;
            if (node->getNodeType() == NODE_GLOBAL_VARIABLE)
            {
                id   = static_cast<LSLGlobalVariable*>(node)->getIdentifier();
                init = static_cast<LSLGlobalVariable*>(node)->getInitializer();
            }
            else if (node->getNodeSubType() == NODE_DECLARATION)
            {
                id   = static_cast<LSLDeclaration*>(node)->getIdentifier();
                init = static_cast<LSLDeclaration*>(node)->getInitializer();
            }
            int v = 0;
            if (id && id->getSymbol() && id->getIType() == LST_INTEGER && wholeNumber(init, v) && v < 0)
            {
                given.emplace(id->getSymbol(), node);
            }
        });
        if (given.empty())
        {
            return;
        }
        // Each read of one by anything but bit32, and each that sets one,
        // takes it out.
        walk(mScript, [&](LSLASTNode* node) {
            if (LSLSymbol* set = setBy(node))
            {
                given.erase(set);
            }
            if (node->getNodeSubType() == NODE_LVALUE_EXPRESSION && !intoBit32(node))
            {
                given.erase(static_cast<LSLLValueExpression*>(node)->getIdentifier()->getSymbol());
            }
        });
        for (const auto& [symbol, declaration] : given)
        {
            mBitsOnly.insert(declaration);
        }
    }

    Expr Writer::lvalue(LSLLValueExpression* e)
    {
        LSLIdentifier* id     = e->getIdentifier();
        LSLSymbol*     symbol = id->getSymbol();
        std::string    name;
        if (symbol && symbol->getSubType() == SYM_BUILTIN)
        {
            // LSL's constants are SLua's globals by the same names, but
            // TRUE and FALSE, which SLua has as true and false.
            const std::string_view lsl = id->getName();
            if (lsl == "TRUE" || lsl == "FALSE")
            {
                return { lsl == "TRUE" ? "1" : "0" };
            }
            name = id->getName();
        }
        else
        {
            name = nameOf(id);
        }
        LSLASTNode* member = e->getMember();
        if (!isNull(member))
        {
            name += std::string(".") + static_cast<LSLIdentifier*>(member)->getName();
        }
        return { name, PRIMARY, isNull(member) && boolean(symbol) };
    }

    Expr Writer::value(LSLExpression* e)
    {
        Expr out = expr(e);
        if (out.boolean)
        {
            return { "if " + out.text + " then 1 else 0", IF_EXPR };
        }
        return out;
    }

    Expr Writer::condition(LSLExpression* e)
    {
        // What stands for true or false outright.
        LSLExpression* inner = unbracketed(e);
        if (inner && inner->getNodeSubType() == NODE_BOOL_CONVERSION_EXPRESSION)
        {
            return condition(static_cast<LSLBoolConversionExpression*>(inner)->getChildExpr());
        }
        if (inner && inner->getNodeSubType() == NODE_LVALUE_EXPRESSION)
        {
            LSLIdentifier* id = static_cast<LSLLValueExpression*>(inner)->getIdentifier();
            if (id->getSymbol() && id->getSymbol()->getSubType() == SYM_BUILTIN &&
                (std::string_view(id->getName()) == "TRUE" || std::string_view(id->getName()) == "FALSE"))
            {
                return { std::string_view(id->getName()) == "TRUE" ? "true" : "false", PRIMARY, true };
            }
        }
        // ~ of a find, as LSL asks whether it found: -1 is all bits.
        if (inner && inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_BIT_NOT)
        {
            if (std::optional<Expr> test = foundTest(static_cast<LSLUnaryExpression*>(inner)->getChildExpr(), true))
            {
                return *test;
            }
        }
        if (std::optional<Expr> test = bitTest(inner))
        {
            return *test;
        }
        Expr out = expr(e);
        if (out.boolean)
        {
            return out;
        }
        // LSL's truth: a number not nought, a string not empty, a key that
        // is one and not the null key, a vector or a rotation not zero, a list
        // with something in it. In Luau everything but nil and false is true.
        // One of the constants SLua has as a uuid is LSL's string.
        if (uuidConstant(e))
        {
            out = { "tostring(" + out.text + ")" };
        }
        switch (e->getIType())
        {
            case LST_INTEGER:
            case LST_FLOATINGPOINT: return { bracketed(out, COMPARE + 1) + " ~= 0", COMPARE, true };
            case LST_STRING: return { bracketed(out, COMPARE + 1) + " ~= \"\"", COMPARE, true };
            case LST_KEY: return { bracketed(out, PRIMARY) + ".istruthy", PRIMARY, true };
            case LST_VECTOR: return { bracketed(out, COMPARE + 1) + " ~= ZERO_VECTOR", COMPARE, true };
            case LST_QUATERNION: return { bracketed(out, COMPARE + 1) + " ~= ZERO_ROTATION", COMPARE, true };
            case LST_LIST: return { "#" + bracketed(out, UNARY) + " > 0", COMPARE, true };
            default: return { out.text, out.prec, true };
        }
    }

    std::optional<Expr> Writer::bitTest(LSLExpression* e)
    {
        LSLExpression* inner = unbracketed(e);
        if (!inner || inner->getNodeSubType() != NODE_BINARY_EXPRESSION || inner->getOperation() != OP_BIT_AND)
        {
            return std::nullopt;
        }
        std::string                                   all;
        const std::function<void(LSLExpression* one)> gather = [&](LSLExpression* one) {
            LSLExpression* b = unbracketed(one);
            if (b->getNodeSubType() == NODE_BINARY_EXPRESSION && b->getOperation() == OP_BIT_AND)
            {
                gather(static_cast<LSLBinaryExpression*>(b)->getLHS());
                gather(static_cast<LSLBinaryExpression*>(b)->getRHS());
                return;
            }
            all += (all.empty() ? "" : ", ") + value(one).text;
        };
        gather(inner);
        return Expr{ "bit32.btest(" + all + ")", PRIMARY, true };
    }

    Expr Writer::joinedPiece(LSLExpression* e)
    {
        // `..` makes a number text as tostring does, and an integer's text
        // in LSL is its digits, which is what both write. Not a float's,
        // which LSL writes with six places, nor a key's, which `..` refuses.
        LSLExpression* bare = unbracketed(e);
        if (bare && bare->getNodeSubType() == NODE_TYPECAST_EXPRESSION && bare->getIType() == LST_STRING)
        {
            LSLExpression* child = static_cast<LSLTypecastExpression*>(bare)->getChildExpr();
            // Arithmetic bracketed as the cast had it: `.. (b + c)`, which
            // `.. b + c` means as well but reads less plainly.
            if (slType(child) == LST_INTEGER)
            {
                const Expr number = value(child);
                return number.prec >= UNARY ? number : Expr{ "(" + number.text + ")" };
            }
        }
        return coerced(e, LST_STRING);
    }

    bool Writer::joinsText(LSLExpression* e)
    {
        e = unbracketed(e);
        if (!e || e->getNodeSubType() != NODE_BINARY_EXPRESSION || static_cast<LSLBinaryExpression*>(e)->getOperation() != OP_PLUS)
        {
            return false;
        }
        const LSLIType lt = slType(static_cast<LSLBinaryExpression*>(e)->getLHS());
        const LSLIType rt = slType(static_cast<LSLBinaryExpression*>(e)->getRHS());
        return lt != LST_LIST && rt != LST_LIST && (lt == LST_STRING || lt == LST_KEY || rt == LST_STRING || rt == LST_KEY);
    }

    void Writer::textPieces(LSLExpression* e, std::vector<LSLExpression*>& out)
    {
        if (!joinsText(e))
        {
            out.push_back(e);
            return;
        }
        e = unbracketed(e);
        textPieces(static_cast<LSLBinaryExpression*>(e)->getLHS(), out);
        textPieces(static_cast<LSLBinaryExpression*>(e)->getRHS(), out);
    }

    std::optional<Expr> Writer::interpolated(const std::vector<LSLExpression*>& pieces)
    {
        // For a piece made text by a cast, what was cast.
        const auto cast = [](LSLExpression* e) -> LSLExpression* {
            e = unbracketed(e);
            return e && e->getNodeSubType() == NODE_TYPECAST_EXPRESSION && e->getIType() == LST_STRING ? static_cast<LSLTypecastExpression*>(e)->getChildExpr()
                                                                                                         : nullptr;
        };
        // Anything but a string is made text: by `..` itself for a number,
        // which string.format does in fewer steps, else by a call.
        const auto converted = [&](LSLExpression* e) {
            LSLExpression* from = cast(e);
            return slType(from ? from : unbracketed(e)) != LST_STRING;
        };
        if (pieces.size() < 2 || std::none_of(pieces.begin(), pieces.end(), converted))
        {
            return std::nullopt;
        }
        std::string body;
        for (LSLExpression* piece : pieces)
        {
            LSLExpression* e = unbracketed(piece);
            if (e->getNodeSubType() == NODE_CONSTANT_EXPRESSION && e->getChild(0)->getNodeSubType() == NODE_STRING_CONSTANT)
            {
                body += luaTemplateText(static_cast<LSLStringConstant*>(e->getChild(0))->getValue());
                continue;
            }
            // A string, a number or a key as it is, which {} makes text as
            // tostring does; a float, a vector or a list as LSL wrote it.
            LSLExpression* from = cast(piece);
            const Expr     made = from && slType(from) != LST_FLOATINGPOINT && slType(from) != LST_VECTOR && slType(from) != LST_QUATERNION &&
                                      slType(from) != LST_LIST
                                      ? value(from)
                                      : value(e);
            // {{ is no expression in Luau: a table's braces bracketed.
            body += "{" + (!made.text.empty() && made.text.front() == '{' ? "(" + made.text + ")" : made.text) + "}";
        }
        return Expr{ "`" + body + "`" };
    }

    bool Writer::keyText(LSLExpression* at, std::string_view text)
    {
        if (text.empty())
        {
            noteOnce(at, "SluaKeyEmpty",
                     "SLua's uuid of no text is NULL_KEY: it equals NULL_KEY and reads as NULL_KEY's text, where LSL's key of no text did "
                     "neither.");
            return true;
        }
        const bool capitals = std::any_of(text.begin(), text.end(), [](char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; });
        if (capitals && ALLSLTraits::isUuid(text))
        {
            noteOnce(at, "SluaKeyCase",
                     "SLua's uuid writes a key's letters small, and finds it the same key as one written small, where LSL's key kept its "
                     "capitals and was compared as text.");
            return true;
        }
        return false;
    }

    Expr Writer::coerced(LSLExpression* e, LSLIType to)
    {
        Expr           out  = value(e);
        const LSLIType from = slType(e);
        if (to == LST_KEY && from == LST_STRING)
        {
            // Text written out that is no UUID, where only a uuid will do --
            // text is kept as text where SLua takes it (Writer::args,
            // Writer::varType) -- which uuid() stops the script on.
            LSLExpression* inner = unbracketed(e);
            const bool constant = inner && inner->getNodeSubType() == NODE_CONSTANT_EXPRESSION;
            if (constant && inner->getChild(0)->getNodeSubType() == NODE_STRING_CONSTANT)
            {
                const std::string_view text = static_cast<LSLStringConstant*>(inner->getChild(0))->getValue();
                if (!keyText(e, text) && !ALLSLTraits::isUuid(text))
                {
                    noteOnce(e, "SluaKeyText", "LSL's keys held any text; SLua's uuid() stops the script on text that is no UUID, as it "
                                               "would here, where only a uuid will do.");
                }
                return { "uuid(" + out.text + ")" };
            }
            noteOnce(e, "SluaUuidText", "uuid() stops the script on text that is no UUID, where LSL's keys held any text; touuid() "
                                        "answers nil instead.");
            return { "uuid(" + out.text + ")" };
        }
        if (to == LST_STRING && from == LST_KEY)
        {
            return { "tostring(" + out.text + ")" };
        }
        return out;
    }

    std::string Writer::textAsKey(LSLExpression* given)
    {
        LSLExpression* inner = unbracketed(given);
        const bool     uuid  = inner && inner->getNodeSubType() == NODE_CONSTANT_EXPRESSION &&
                          inner->getChild(0)->getNodeSubType() == NODE_STRING_CONSTANT &&
                          ALLSLTraits::isUuid(static_cast<LSLStringConstant*>(inner->getChild(0))->getValue());
        if (slType(given) != LST_STRING || uuid)
        {
            return coerced(given, LST_KEY).text;
        }
        noteOnce(given, "SluaKeyAsText",
                 "SLua's ll takes a key as text, as LSL's functions did, so text is given as it is; `:: any` tells the type checker, which "
                 "asks for a uuid. uuid() would stop the script on text that is no UUID.");
        return bracketed(value(given), PRIMARY) + " :: any";
    }

    std::string Writer::args(LSLASTNode* list, LSLParamList* params, U16 text, int put, const std::string& put_text, bool builtin)
    {
        std::string out;
        LSLASTNode* param = params ? params->getChild(0) : nullptr;
        int         at    = 0;
        for (LSLASTNode* arg = isNull(list) ? nullptr : list->getChild(0); arg; arg = arg->getNext(), ++at)
        {
            auto*      given = static_cast<LSLExpression*>(arg);
            LSLSymbol* taken = param ? static_cast<LSLIdentifier*>(param)->getSymbol() : nullptr;
            LSLIType   to    = param ? varType(taken, param->getIType()) : given->getIType();
            if ((text & (1 << at)) && (slType(given) == LST_STRING || slType(given) == LST_KEY))
            {
                to = slType(given);
            }
            // A parameter the script only reads as a truth is given one.
            out += (out.empty() ? "" : ", ") + (at == put                 ? put_text
                                                : boolean(taken)            ? truthOf(given)
                                                : builtin && to == LST_KEY ? textAsKey(given)
                                                                            : coerced(given, to).text);
            param = param ? param->getNext() : nullptr;
        }
        return out;
    }

    bool wholeNumber(LSLExpression* e, int& v);

    // A whole number not below nought: written out, or one of LSL's
    // constants, CHANGED_LINK and the rest.
    bool nonNegative(LSLExpression* e)
    {
        int v = 0;
        if (wholeNumber(e, v))
        {
            return v >= 0;
        }
        e = unbracketed(e);
        if (!e || e->getNodeSubType() != NODE_LVALUE_EXPRESSION)
        {
            return false;
        }
        LSLSymbol*   symbol = static_cast<LSLLValueExpression*>(e)->getIdentifier()->getSymbol();
        LSLConstant* value  = symbol && symbol->getSubType() == SYM_BUILTIN ? symbol->getConstantValue() : nullptr;
        return value && value->getNodeSubType() == NODE_INTEGER_CONSTANT && static_cast<LSLIntegerConstant*>(value)->getValue() >= 0;
    }

    // Whether what an expression gives is only asked whether it is
    // nought: a condition, an operand of !, && or ||, or compared with 0.
    bool truthOnly(LSLASTNode* e)
    {
        LSLASTNode* node   = e;
        LSLASTNode* parent = node->getParent();
        while (parent && parent->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
        {
            node   = parent;
            parent = parent->getParent();
        }
        if (!parent)
        {
            return false;
        }
        const int slot = node->getParentSlot();
        switch (parent->getNodeSubType())
        {
            case NODE_BOOL_CONVERSION_EXPRESSION: return true;
            case NODE_IF_STATEMENT:
            case NODE_WHILE_STATEMENT: return slot == 0;
            case NODE_DO_STATEMENT:
            case NODE_FOR_STATEMENT: return slot == 1;
            case NODE_UNARY_EXPRESSION: return static_cast<LSLExpression*>(parent)->getOperation() == OP_BOOLEAN_NOT;
            case NODE_BINARY_EXPRESSION:
            {
                const LSLOperator op = static_cast<LSLExpression*>(parent)->getOperation();
                int               v  = 1;
                auto*             b  = static_cast<LSLBinaryExpression*>(parent);
                return op == OP_BOOLEAN_AND || op == OP_BOOLEAN_OR ||
                       ((op == OP_EQ || op == OP_NEQ) && wholeNumber(slot == 0 ? b->getRHS() : b->getLHS(), v) && v == 0);
            }
            default: return false;
        }
    }

    // Whether what an expression gives is bit32's to take again, which takes
    // a number below nought as it takes the same past 2147483647: an operand
    // of &, | or ^, what ~ turns over, or what a shift moves.
    bool intoBit32(LSLASTNode* e)
    {
        LSLASTNode* node   = e;
        LSLASTNode* parent = node->getParent();
        while (parent && parent->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
        {
            node   = parent;
            parent = parent->getParent();
        }
        if (!parent || parent->getNodeType() != NODE_EXPRESSION)
        {
            return false;
        }
        switch (static_cast<LSLExpression*>(parent)->getOperation())
        {
            case OP_BIT_AND:
            case OP_BIT_OR:
            case OP_BIT_XOR:
            case OP_BIT_NOT:
                return true;
            case OP_SHIFT_LEFT:
            case OP_SHIFT_RIGHT:
                return node->getParentSlot() == 0;
            default:
                return false;
        }
    }

    // A whole number written out, and what it is: a constant, or one with a
    // minus before it.
    bool wholeNumber(LSLExpression* e, int& v)
    {
        e = unbracketed(e);
        if (!e)
        {
            return false;
        }
        if (e->getNodeSubType() == NODE_CONSTANT_EXPRESSION && e->getChild(0)->getNodeSubType() == NODE_INTEGER_CONSTANT)
        {
            v = static_cast<LSLIntegerConstant*>(e->getChild(0))->getValue();
            return true;
        }
        if (e->getNodeSubType() == NODE_UNARY_EXPRESSION && e->getOperation() == OP_MINUS &&
            wholeNumber(static_cast<LSLUnaryExpression*>(e)->getChildExpr(), v))
        {
            v = -v;
            return true;
        }
        return false;
    }

    // A find's place counted as LSL's, compared with a whole number
    // written out, on either side: compared in SLua's count instead, the
    // number moved on by one -- `x == 1` -- or against nil where all it
    // asks is whether anything was found. The same test whatever x is.
    // Not an == or ~= with a number below -1, which no place is.
    std::optional<Expr> placeCompared(std::string_view word, const Expr& a, const Expr& b, LSLExpression* lhs, LSLExpression* rhs)
    {
        int        v     = 0;
        const bool left  = !a.place.empty() && wholeNumber(rhs, v);
        const bool right = !left && !b.place.empty() && wholeNumber(lhs, v);
        if (!left && !right)
        {
            return std::nullopt;
        }
        // With the place on the left.
        std::string_view as = word;
        if (right)
        {
            as = word == "<" ? ">" : word == ">" ? "<" : word == "<=" ? ">=" : word == ">=" ? "<=" : word;
        }
        const std::string& x = left ? a.place : b.place;
        if ((as == "==" && v == -1) || (as == "<" && v == 0) || (as == "<=" && v == -1))
        {
            return Expr{ x + " == nil", COMPARE, true };
        }
        if ((as == "~=" && v == -1) || (as == ">=" && v == 0) || (as == ">" && v == -1))
        {
            return Expr{ x + " ~= nil", COMPARE, true };
        }
        const std::string n = std::to_string(static_cast<S64>(v) + 1);
        if ((as == "==" || as == "~=") && v >= 0)
        {
            return Expr{ x + " " + std::string(as) + " " + n, COMPARE, true };
        }
        if (as == "<" || as == ">" || as == "<=" || as == ">=")
        {
            return Expr{ "(" + x + " or 0) " + std::string(as) + " " + n, COMPARE, true };
        }
        return std::nullopt;
    }

    // A number written out, an integer or a float, and whether it is
    // nought; or one with a minus before it.
    bool numberWritten(LSLExpression* e, bool& nought)
    {
        e = unbracketed(e);
        if (e && e->getNodeSubType() == NODE_UNARY_EXPRESSION && e->getOperation() == OP_MINUS)
        {
            return numberWritten(static_cast<LSLUnaryExpression*>(e)->getChildExpr(), nought);
        }
        LSLASTNode* const c = e && e->getNodeSubType() == NODE_CONSTANT_EXPRESSION ? e->getChild(0) : nullptr;
        if (c && c->getNodeSubType() == NODE_INTEGER_CONSTANT)
        {
            nought = static_cast<LSLIntegerConstant*>(c)->getValue() == 0;
            return true;
        }
        if (c && c->getNodeSubType() == NODE_FLOAT_CONSTANT)
        {
            nought = static_cast<LSLFloatConstant*>(c)->getValue() == 0.0;
            return true;
        }
        return false;
    }

    // A vector written out, each of its parts a number, not all of them
    // nought; or one whose value Tailslide knows, which is not nought.
    bool nonZeroVector(LSLExpression* e)
    {
        e = unbracketed(e);
        if (e && e->getNodeSubType() == NODE_VECTOR_EXPRESSION)
        {
            auto* v = static_cast<LSLVectorExpression*>(e);
            bool  x = false, y = false, z = false;
            return numberWritten(v->getX(), x) && numberWritten(v->getY(), y) && numberWritten(v->getZ(), z) && !(x && y && z);
        }
        LSLConstant* const c = e ? e->getConstantValue() : nullptr;
        if (!c || c->getNodeSubType() != NODE_VECTOR_CONSTANT)
        {
            return false;
        }
        const Vector3* v = static_cast<LSLVectorConstant*>(c)->getValue();
        return v && (v->x != 0.0f || v->y != 0.0f || v->z != 0.0f);
    }

    LSLExpression* argumentAt(LSLFunctionExpression* e, int at)
    {
        LSLASTNode* arg = isNull(e->getArguments()) ? nullptr : e->getArguments()->getChild(0);
        for (int i = 0; arg && i < at; ++i)
        {
            arg = arg->getNext();
        }
        return static_cast<LSLExpression*>(arg);
    }

    std::string Writer::typed(LSLIType type) const
    {
        if (!mOptions.types)
        {
            return std::string();
        }
        switch (type)
        {
            case LST_INTEGER:
            case LST_FLOATINGPOINT: return ": number";
            case LST_STRING: return ": string";
            case LST_KEY: return ": uuid";
            case LST_VECTOR: return ": vector";
            case LST_QUATERNION: return ": quaternion";
            case LST_LIST: return ": { any }";
            default: return std::string();
        }
    }

    bool Writer::constantIndexes(LSLFunctionExpression* e, U16 indexes)
    {
        for (int i = 0; i < 16; ++i)
        {
            if ((indexes & (1 << i)) && !llIndex(argumentAt(e, i)))
            {
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> Writer::llIndex(LSLExpression* e)
    {
        int v = 0;
        if (!e)
        {
            return std::nullopt;
        }
        if (wholeNumber(e, v))
        {
            return std::to_string(v >= 0 ? v + 1 : v);
        }
        e = unbracketed(e);
        // A counter, alone or with a whole number added.
        LSLExpression* counter = e;
        int            added   = 0;
        if (e->getNodeSubType() == NODE_BINARY_EXPRESSION && e->getOperation() == OP_PLUS)
        {
            auto* b = static_cast<LSLBinaryExpression*>(e);
            counter = wholeNumber(b->getRHS(), added) ? b->getLHS() : wholeNumber(b->getLHS(), added) ? b->getRHS() : nullptr;
        }
        counter = unbracketed(counter);
        if (!counter || counter->getNodeSubType() != NODE_LVALUE_EXPRESSION || added < 0 ||
            !isNull(static_cast<LSLLValueExpression*>(counter)->getMember()))
        {
            return std::nullopt;
        }
        LSLSymbol* var = static_cast<LSLLValueExpression*>(counter)->getIdentifier()->getSymbol();
        if (!mNonNegative.contains(var))
        {
            return std::nullopt;
        }
        const int         moved = added + (mFromOne.contains(var) ? 0 : 1);
        const std::string name  = lvalue(static_cast<LSLLValueExpression*>(counter)).text;
        return moved ? name + " + " + std::to_string(moved) : name;
    }

    std::string Writer::llArgs(LSLFunctionExpression* e, U16 indexes)
    {
        // An index from nought moved on by one; one from the end, which
        // counts back from -1 either way, as it is.
        LSLSymbol*    symbol = e->getIdentifier()->getSymbol();
        LSLParamList* params = symbol ? symbol->getFunctionDecl() : nullptr;
        LSLASTNode*   param  = params ? params->getChild(0) : nullptr;
        std::string   out;
        int           at = 0;
        for (LSLASTNode* arg = isNull(e->getArguments()) ? nullptr : e->getArguments()->getChild(0); arg; arg = arg->getNext(), ++at)
        {
            std::string                one;
            std::optional<std::string> index = (indexes & (1 << at)) ? llIndex(static_cast<LSLExpression*>(arg)) : std::nullopt;
            if (index)
            {
                one = *index;
            }
            else
            {
                const LSLIType to = param ? param->getIType() : static_cast<LSLExpression*>(arg)->getIType();
                one               = to == LST_KEY ? textAsKey(static_cast<LSLExpression*>(arg)) : coerced(static_cast<LSLExpression*>(arg), to).text;
            }
            out += (out.empty() ? "" : ", ") + one;
            param = param ? param->getNext() : nullptr;
        }
        return out;
    }

    std::optional<Expr> Writer::detected(LSLFunctionExpression* e, const std::string& lsl)
    {
        // Each llDetected* and the detected event's method that says the
        // same; the group as a boolean, as SLua has it.
        static const boost::unordered_flat_map<std::string_view, std::string_view> METHODS = {
            { "llDetectedKey", "getKey" },           { "llDetectedName", "getName" },
            { "llDetectedOwner", "getOwner" },       { "llDetectedGroup", "getGroup" },
            { "llDetectedPos", "getPos" },           { "llDetectedRot", "getRot" },
            { "llDetectedVel", "getVel" },           { "llDetectedLinkNumber", "getLinkNumber" },
            { "llDetectedGrab", "getGrab" },         { "llDetectedTouchFace", "getTouchFace" },
            { "llDetectedTouchPos", "getTouchPos" }, { "llDetectedTouchNormal", "getTouchNormal" },
            { "llDetectedTouchBinormal", "getTouchBinormal" }, { "llDetectedTouchST", "getTouchST" },
            { "llDetectedTouchUV", "getTouchUV" },   { "llDetectedType", "getType" },
            { "llDetectedRezzer", "getRezzer" },     { "llDetectedDamage", "getDamage" },
        };
        const auto found = METHODS.find(lsl);
        LSLExpression* index = argumentAt(e, 0);
        // And llAdjustDamage, the damage set on what was detected, in
        // on_damage: the one event whose table SLua lets adjust it, as LSL's
        // call worked in it alone. Elsewhere LSL said an error on the debug
        // channel and went on, where adjustDamage would stop the handler:
        // llcompat's there.
        const bool damage = lsl == "llAdjustDamage" && mOnDamage;
        if ((found == METHODS.end() && !damage) || !index)
        {
            return std::nullopt;
        }
        noteOnce(e, "SluaDetectedTable", "detected[n] is what LSL read with llDetected*(n - 1): SLua counts it from 1, and one past its "
                                         "end is an error, where LSL answered nothing.");
        int v = 0;
        const std::string at = wholeNumber(index, v) ? std::to_string(v + 1) : bracketed(value(index), ADD + 1) + " + 1";
        if (damage)
        {
            return Expr{ "detected[" + at + "]:adjustDamage(" + coerced(argumentAt(e, 1), LST_FLOATINGPOINT).text + ")" };
        }
        return Expr{ "detected[" + at + "]:" + std::string(found->second) + "()", PRIMARY, found->second == "getGroup" };
    }

    std::optional<Expr> Writer::scriptTime(const std::string& lsl, std::string& called)
    {
        // llcompat's GetTime beside llcompat's resets, which are LSL's
        // together; the resets themselves go llcompat's way, and are noted.
        if (mCompatTime)
        {
            if (lsl != "llGetTime")
            {
                return std::nullopt;
            }
            called = "llcompat.GetTime";
            return Expr{ "llcompat.GetTime()" };
        }
        if (!mTimeBase)
        {
            return std::nullopt;
        }
        // The time since the last reset, as LSL counted it; a reset, a
        // statement; and both, through the helper (helpers).
        if (lsl == "llGetTime")
        {
            called = "ll.GetTime";
            return Expr{ "ll.GetTime() - timeBase", ADD };
        }
        if (lsl == "llResetTime")
        {
            called = "timeBase";
            return Expr{ "timeBase = ll.GetTime()" };
        }
        if (lsl == "llGetAndResetTime")
        {
            called           = "getAndResetTime";
            mGetAndResetTime = true;
            return Expr{ "getAndResetTime()" };
        }
        return std::nullopt;
    }

    // --- rule lists as tables -----------------------------------------------------------

    // What a rule's value is to the key SLua's table names it by: as the
    // definitions' ParticleParams, MediaParams and HttpRequestParams type
    // it, and as the grid's serializer makes LSL's list of it again.
    enum class Takes : U8
    {
        Number,  // an integer or a float, as it is
        Vector,  // a vector, as it is
        Text,    // a string, as it is
        Asset,   // a string or a key, as it is
        Truth,   // a boolean, which it makes 1 or 0
        Flags,   // a number, and a boolean of each of its masks, ORed in
        Csv,     // a {string}, which it joins with commas, escaping \ and ,
        Headers, // a {[string]: string}, a rule of each, sorted by name
        Each     // a {string}, a rule of each, in order
    };

    struct TableRule
    {
        const char* constant;
        const char* key;
        Takes       takes;
    };

    // Each constant a member of the table's enum in the definitions' YAML,
    // under its pretty-name, or its name without the prefix and the filler
    // words (part, src; http), as they build the type.
    const TableRule PARTICLE_RULES[] = {
        { "PSYS_PART_FLAGS", "flags", Takes::Flags },
        { "PSYS_PART_START_COLOR", "color_begin", Takes::Vector },
        { "PSYS_PART_START_ALPHA", "alpha_begin", Takes::Number },
        { "PSYS_PART_END_COLOR", "color_end", Takes::Vector },
        { "PSYS_PART_END_ALPHA", "alpha_end", Takes::Number },
        { "PSYS_PART_START_SCALE", "scale_begin", Takes::Vector },
        { "PSYS_PART_END_SCALE", "scale_end", Takes::Vector },
        { "PSYS_PART_MAX_AGE", "part_max_age", Takes::Number },
        { "PSYS_SRC_ACCEL", "accel", Takes::Vector },
        { "PSYS_SRC_PATTERN", "pattern", Takes::Number },
        { "PSYS_SRC_INNERANGLE", "angle_inner", Takes::Number },
        { "PSYS_SRC_OUTERANGLE", "angle_outer", Takes::Number },
        { "PSYS_SRC_TEXTURE", "texture", Takes::Asset },
        { "PSYS_SRC_BURST_RATE", "burst_rate", Takes::Number },
        { "PSYS_SRC_BURST_PART_COUNT", "burst_count", Takes::Number },
        { "PSYS_SRC_BURST_RADIUS", "burst_radius", Takes::Number },
        { "PSYS_SRC_BURST_SPEED_MIN", "burst_speed_min", Takes::Number },
        { "PSYS_SRC_BURST_SPEED_MAX", "burst_speed_max", Takes::Number },
        { "PSYS_SRC_MAX_AGE", "src_max_age", Takes::Number },
        { "PSYS_SRC_TARGET_KEY", "target_key", Takes::Asset },
        { "PSYS_SRC_OMEGA", "omega", Takes::Vector },
        { "PSYS_SRC_ANGLE_BEGIN", "angle_begin", Takes::Number },
        { "PSYS_SRC_ANGLE_END", "angle_end", Takes::Number },
        { "PSYS_PART_BLEND_FUNC_SOURCE", "blend_func_source", Takes::Number },
        { "PSYS_PART_BLEND_FUNC_DEST", "blend_func_dest", Takes::Number },
        { "PSYS_PART_START_GLOW", "glow_begin", Takes::Number },
        { "PSYS_PART_END_GLOW", "glow_end", Takes::Number },
    };
    // PSYS_PART_FLAGS' masks, the flag enum's members, each a boolean of
    // its own, its name without the prefix and _MASK.
    const std::pair<const char*, const char*> PARTICLE_FLAGS[] = {
        { "PSYS_PART_INTERP_COLOR_MASK", "color_interp" }, { "PSYS_PART_INTERP_SCALE_MASK", "scale_interp" },
        { "PSYS_PART_BOUNCE_MASK", "bounce" },             { "PSYS_PART_WIND_MASK", "wind" },
        { "PSYS_PART_FOLLOW_SRC_MASK", "follow" },         { "PSYS_PART_FOLLOW_VELOCITY_MASK", "follow_velocity" },
        { "PSYS_PART_TARGET_POS_MASK", "target_pos" },     { "PSYS_PART_TARGET_LINEAR_MASK", "target_linear" },
        { "PSYS_PART_EMISSIVE_MASK", "emissive" },         { "PSYS_PART_RIBBON_MASK", "ribbon" },
    };
    const TableRule MEDIA_RULES[] = {
        { "PRIM_MEDIA_ALT_IMAGE_ENABLE", "alt_image_enable", Takes::Truth },
        { "PRIM_MEDIA_CONTROLS", "controls", Takes::Number },
        { "PRIM_MEDIA_CURRENT_URL", "current_url", Takes::Text },
        { "PRIM_MEDIA_HOME_URL", "home_url", Takes::Text },
        { "PRIM_MEDIA_AUTO_LOOP", "auto_loop", Takes::Truth },
        { "PRIM_MEDIA_AUTO_PLAY", "auto_play", Takes::Truth },
        { "PRIM_MEDIA_AUTO_SCALE", "auto_scale", Takes::Truth },
        { "PRIM_MEDIA_AUTO_ZOOM", "auto_zoom", Takes::Truth },
        { "PRIM_MEDIA_FIRST_CLICK_INTERACT", "first_click_interact", Takes::Truth },
        { "PRIM_MEDIA_WIDTH_PIXELS", "width", Takes::Number },
        { "PRIM_MEDIA_HEIGHT_PIXELS", "height", Takes::Number },
        { "PRIM_MEDIA_WHITELIST_ENABLE", "whitelist_enable", Takes::Truth },
        { "PRIM_MEDIA_WHITELIST", "whitelist", Takes::Csv },
        { "PRIM_MEDIA_PERMS_INTERACT", "perms_interact", Takes::Number },
        { "PRIM_MEDIA_PERMS_CONTROL", "perms_control", Takes::Number },
    };
    const TableRule HTTP_RULES[] = {
        { "HTTP_METHOD", "method", Takes::Text },
        { "HTTP_MIMETYPE", "mimetype", Takes::Text },
        { "HTTP_BODY_MAXLENGTH", "max_body_length", Takes::Number },
        { "HTTP_VERIFY_CERT", "verify_cert", Takes::Truth },
        { "HTTP_VERBOSE_THROTTLE", "verbose_throttle", Takes::Truth },
        { "HTTP_CUSTOM_HEADER", "custom_header", Takes::Headers },
        { "HTTP_PRAGMA_NO_CACHE", "pragma_no_cache", Takes::Truth },
        { "HTTP_USER_AGENT", "user_agent", Takes::Text },
        { "HTTP_ACCEPT", "accept", Takes::Each },
        { "HTTP_EXTENDED_ERROR", "extended_error", Takes::Truth },
    };

    // The name of the LSL constant an expression is, as it is.
    const char* constantName(LSLExpression* e)
    {
        e = unbracketed(e);
        if (!e || e->getNodeSubType() != NODE_LVALUE_EXPRESSION)
        {
            return nullptr;
        }
        auto*      read   = static_cast<LSLLValueExpression*>(e);
        LSLSymbol* symbol = read->getIdentifier()->getSymbol();
        return symbol && symbol->getSubType() == SYM_BUILTIN && isNull(read->getMember()) ? read->getIdentifier()->getName() : nullptr;
    }

    // Text written out, as it is.
    std::optional<std::string_view> textWritten(LSLExpression* e)
    {
        e = unbracketed(e);
        if (e && e->getNodeSubType() == NODE_CONSTANT_EXPRESSION && e->getChild(0)->getNodeSubType() == NODE_STRING_CONSTANT)
        {
            return std::string_view(static_cast<LSLStringConstant*>(e->getChild(0))->getValue());
        }
        return std::nullopt;
    }

    // TRUE or FALSE, or 1 or 0, written out: which.
    std::optional<bool> truthWritten(LSLExpression* e)
    {
        int               n    = -1;
        const char* const name = constantName(e);
        if (wholeNumber(e, n) && (n == 0 || n == 1))
        {
            return n == 1;
        }
        if (name && (std::string_view(name) == "TRUE" || std::string_view(name) == "FALSE"))
        {
            return std::string_view(name) == "TRUE";
        }
        return std::nullopt;
    }

    // An | of PSYS_PART_FLAGS' masks, each once: each mask and its key, in
    // the order written.
    bool flagKeys(LSLExpression* e, std::vector<std::pair<const char*, const char*>>& keys)
    {
        e = unbracketed(e);
        if (e && e->getNodeSubType() == NODE_BINARY_EXPRESSION && e->getOperation() == OP_BIT_OR)
        {
            auto* both = static_cast<LSLBinaryExpression*>(e);
            return flagKeys(both->getLHS(), keys) && flagKeys(both->getRHS(), keys);
        }
        const char* const name = constantName(e);
        for (const auto& flag : PARTICLE_FLAGS)
        {
            if (name && std::string_view(name) == flag.first)
            {
                const bool again = std::find(keys.begin(), keys.end(), flag) != keys.end();
                keys.push_back(flag);
                return !again;
            }
        }
        return false;
    }

    // A whitelist's text as the {string} the serializer joins back into the
    // same text: its pieces between commas. Only where the join gives it
    // back as it was: text with no \ to escape, and neither none nor a
    // first piece of none, which the join leaves out.
    std::optional<std::vector<std::string_view>> whitelistPieces(std::string_view text)
    {
        if (text.empty() || text.front() == ',' || text.find('\\') != std::string_view::npos)
        {
            return std::nullopt;
        }
        std::vector<std::string_view> pieces;
        for (size_t from = 0;;)
        {
            const size_t comma = text.find(',', from);
            pieces.push_back(text.substr(from, comma == std::string_view::npos ? std::string_view::npos : comma - from));
            if (comma == std::string_view::npos)
            {
                return pieces;
            }
            from = comma + 1;
        }
    }

    std::optional<Expr> Writer::ruleTable(LSLFunctionExpression* e, const std::string& lsl, std::string& called)
    {
        // Each call, the argument its rules are, and the table's rules.
        struct Call
        {
            const char*      lsl;
            int              at;
            const TableRule* rules;
            size_t           count;
        };
        static const Call CALLS[] = {
            { "llParticleSystem", 0, PARTICLE_RULES, std::size(PARTICLE_RULES) },
            { "llLinkParticleSystem", 1, PARTICLE_RULES, std::size(PARTICLE_RULES) },
            { "llSetPrimMediaParams", 1, MEDIA_RULES, std::size(MEDIA_RULES) },
            { "llSetLinkMedia", 2, MEDIA_RULES, std::size(MEDIA_RULES) },
            { "llHTTPRequest", 1, HTTP_RULES, std::size(HTTP_RULES) },
        };
        const auto call = std::find_if(std::begin(CALLS), std::end(CALLS), [&](const Call& c) { return lsl == c.lsl; });
        LSLExpression* list = call != std::end(CALLS) ? unbracketed(argumentAt(e, call->at)) : nullptr;
        if (!list || list->getNodeSubType() != NODE_LIST_EXPRESSION)
        {
            return std::nullopt;
        }
        std::vector<LSLExpression*> items;
        for (LSLASTNode* item = list->getChild(0); item; item = item->getNext())
        {
            items.push_back(static_cast<LSLExpression*>(item));
        }
        // Each rule a constant the table has a key for, with what that key
        // takes after it, and each key once -- but a header of each name,
        // and the accepts, which gather. Anything else stays a list.
        struct Rule
        {
            const TableRule* rule;
            size_t           at;
        };
        std::vector<Rule>             rules;
        std::vector<std::string_view> keys;
        std::vector<std::string_view> headers;
        bool                          moved = false;
        for (size_t i = 0; i < items.size();)
        {
            const char* const name = constantName(items[i]);
            const TableRule*  rule = nullptr;
            for (size_t r = 0; name && r < call->count; ++r)
            {
                rule = std::string_view(name) == call->rules[r].constant ? &call->rules[r] : rule;
            }
            const size_t taken = rule && rule->takes == Takes::Headers ? 2 : 1;
            if (!rule || i + taken >= items.size())
            {
                return std::nullopt;
            }
            LSLExpression* v    = items[i + 1];
            const LSLIType type = v->getIType();
            bool           fits = false;
            switch (rule->takes)
            {
                case Takes::Number: fits = type == LST_INTEGER || type == LST_FLOATINGPOINT; break;
                case Takes::Vector: fits = type == LST_VECTOR; break;
                case Takes::Text: fits = type == LST_STRING; break;
                case Takes::Asset: fits = type == LST_STRING || type == LST_KEY; break;
                case Takes::Truth: fits = truthWritten(v).has_value(); break;
                case Takes::Flags: fits = type == LST_INTEGER; break;
                case Takes::Csv: fits = textWritten(v) && whitelistPieces(*textWritten(v)); break;
                case Takes::Headers:
                {
                    // A name written out, each once.
                    const std::optional<std::string_view> header = textWritten(v);
                    fits = header && std::find(headers.begin(), headers.end(), *header) == headers.end() &&
                           items[i + 2]->getIType() == LST_STRING;
                    if (fits)
                    {
                        headers.push_back(*header);
                    }
                    break;
                }
                case Takes::Each: fits = type == LST_STRING; break;
            }
            const bool gathers = rule->takes == Takes::Headers || rule->takes == Takes::Each;
            const bool again   = std::find(keys.begin(), keys.end(), rule->key) != keys.end();
            if (!fits || (again && !gathers))
            {
                return std::nullopt;
            }
            moved |= again && rules.back().rule != rule;
            keys.push_back(rule->key);
            rules.push_back({ rule, i });
            i += 1 + taken;
        }
        // A header or an accept after the first of its kind goes up into
        // the first one's table, run before what stood between: only where
        // nothing in the list changes anything, so that the order cannot
        // show.
        const auto still = [](LSLExpression* item) { return ALLSLTraits::changesNothing(item); };
        if (rules.empty() || (moved && !std::all_of(items.begin(), items.end(), still)))
        {
            return std::nullopt;
        }
        // Each key in the order written, a gathering one where it first is,
        // each value as the list would have had it.
        struct Gathered
        {
            const char* key;
            size_t      at;
            std::string all;
        };
        std::vector<std::string> fields;
        std::vector<Gathered>    gathered;
        for (const Rule& each : rules)
        {
            const TableRule* rule = each.rule;
            LSLExpression*   v    = items[each.at + 1];
            std::string      text;
            switch (rule->takes)
            {
                case Takes::Truth: text = *truthWritten(v) ? "true" : "false"; break;
                case Takes::Flags:
                {
                    std::vector<std::pair<const char*, const char*>> flags;
                    if (flagKeys(v, flags))
                    {
                        for (const auto& [mask, key] : flags)
                        {
                            sameAs(mask, key);
                            fields.push_back(std::string(key) + " = true");
                        }
                        continue;
                    }
                    text = coerced(v, LST_INTEGER).text;
                    break;
                }
                case Takes::Csv:
                {
                    // Held here: a loop over what the optional holds would
                    // otherwise outlive the optional, which only C++23's
                    // rule for a loop's range keeps alive, and not every
                    // compiler has that rule.
                    const std::optional<std::vector<std::string_view>> pieces = whitelistPieces(*textWritten(v));
                    for (std::string_view piece : *pieces)
                    {
                        text += (text.empty() ? "{ " : ", ") + luaString(piece);
                    }
                    text += " }";
                    break;
                }
                case Takes::Headers:
                case Takes::Each:
                {
                    const std::string one   = rule->takes == Takes::Headers
                                                  ? "[" + luaString(*textWritten(v)) + "] = " + coerced(items[each.at + 2], LST_STRING).text
                                                  : coerced(v, LST_STRING).text;
                    const auto        found = std::find_if(gathered.begin(), gathered.end(), [&](const Gathered& g) { return g.key == rule->key; });
                    if (found != gathered.end())
                    {
                        found->all += ", " + one;
                        continue;
                    }
                    gathered.push_back({ rule->key, fields.size(), one });
                    break;
                }
                default: text = coerced(v, v->getIType()).text; break;
            }
            sameAs(rule->constant, rule->key);
            fields.push_back(std::string(rule->key) + " = " + text);
        }
        for (const Gathered& g : gathered)
        {
            fields[g.at] += "{ " + g.all + " }";
        }
        // The serializer sends headers sorted by name: said where that is
        // not the order they were written in.
        if (!std::is_sorted(headers.begin(), headers.end()))
        {
            note(e, "SluaHeaderOrder",
                 "SLua passes ll.HTTPRequest a custom_header table's headers sorted by name, where LSL's list passed them in the order "
                 "written; nothing else differs.");
        }
        // A key to a line where the LSL's list had more than one.
        const bool  lines = list->getLoc()->first_line != list->getLoc()->last_line;
        std::string table = lines ? "{\n" : "{ ";
        for (size_t i = 0; i < fields.size(); ++i)
        {
            table += lines ? listIndent(1) + fields[i] + ",\n" : (i ? ", " : "") + fields[i];
        }
        table += lines ? listIndent(0) + "}" : " }";
        called = "ll." + lsl.substr(2);
        LSLParamList* params = e->getIdentifier()->getSymbol()->getFunctionDecl();
        return Expr{ called + "(" + args(e->getArguments(), params, 0, call->at, table, true) + ")" };
    }

    std::optional<Expr> Writer::idiom(LSLFunctionExpression* e, const std::string& lsl, std::string& called)
    {
        const auto arg = [&](int at) { return value(argumentAt(e, at)); };
        if (mOptions.sluaCalls)
        {
            if (std::optional<Expr> item = listItem(e, lsl))
            {
                return item;
            }
        }
        if (std::optional<Expr> table = ruleTable(e, lsl, called))
        {
            return table;
        }
        // One thing looked for in a list, its place: table.find's, counted
        // from 0, or -1 where it is not there, as LSL's.
        if (lsl == "llListFindList")
        {
            if (LSLASTNode* sought = soleItem(argumentAt(e, 1)))
            {
                called = "table.find";
                const std::string found = "table.find(" + anyItems(argumentAt(e, 0), arg(0)) + ", " + itemText(sought) + ")";
                return Expr{ "(" + found + " or 0) - 1", ADD, false, found };
            }
        }
        // A part of a list between two places written out, from its start
        // on: table.move's copy of it into a new list, which copies nothing
        // past the list's end, as LSL's stops there; and all of it, a clone.
        // Not a place counted from the end, which a short list could have
        // its start past, nor a start after the end, which LSL reads as the
        // part to leave out; nor an end past what a script's 64 KiB could
        // hold, which the copy would count up to.
        if (lsl == "llList2List")
        {
            int from = 0;
            int to   = 0;
            if (wholeNumber(argumentAt(e, 1), from) && wholeNumber(argumentAt(e, 2), to))
            {
                if (from == 0 && to == -1)
                {
                    called = "table.clone";
                    return Expr{ "table.clone(" + arg(0).text + ")" };
                }
                if (from >= 0 && from <= to && to < 65536)
                {
                    called = "table.move";
                    return Expr{ "table.move(" + arg(0).text + ", " + std::to_string(from + 1) + ", " + std::to_string(to + 1) + ", 1, {})" };
                }
            }
        }
        // A list of text and whole numbers joined: table.concat, which writes
        // a whole number as LSL wrote an integer, where ll.DumpList2String
        // writes it as a float, 5.000000, since SLua's numbers are all floats
        // to it.
        if (lsl == "llDumpList2String" && (itemTypes(argumentAt(e, 0)) & ~(ItemString | ItemInteger)) == 0)
        {
            called = "table.concat";
            return Expr{ "table.concat(" + arg(0).text + ", " + coerced(argumentAt(e, 1), LST_STRING).text + ")" };
        }
        if (lsl == "llPow")
        {
            // Luau's ^ binds tighter than a minus before it.
            return Expr{ bracketed(arg(0), POWER + 1) + " ^ " + bracketed(arg(1), UNARY), POWER };
        }
        if (lsl == "llVecMag")
        {
            called = "vector.magnitude";
            return Expr{ called + "(" + arg(0).text + ")" };
        }
        // LSL keeps a zero vector zero, which vector.normalize makes NaN of:
        // vecNorm guards it, but for a vector known to be something else.
        if (lsl == "llVecNorm")
        {
            called = nonZeroVector(argumentAt(e, 0)) ? "vector.normalize" : "vecNorm";
            mVecNorm |= called == "vecNorm";
            return Expr{ called + "(" + arg(0).text + ")" };
        }
        if (lsl == "llVecDist")
        {
            called = "vector.magnitude";
            return Expr{ called + "(" + bracketed(arg(0), ADD) + " - " + bracketed(arg(1), ADD + 1) + ")" };
        }
        if (lsl == "llRot2Fwd" || lsl == "llRot2Left" || lsl == "llRot2Up")
        {
            called = std::string("quaternion.") + (lsl == "llRot2Fwd" ? "tofwd" : lsl == "llRot2Left" ? "toleft" : "toup");
            return Expr{ called + "(" + arg(0).text + ")" };
        }
        if (lsl == "llGetUnixTime")
        {
            called = "os.time";
            return Expr{ "os.time()" };
        }
        if (lsl == "llOwnerSay")
        {
            called = "print";
            return Expr{ "print(" + coerced(argumentAt(e, 0), LST_STRING).text + ")" };
        }
        // Base64 of the string's bytes, its UTF-8, as LSL's was: both are
        // APR's, padded and on one line.
        if (lsl == "llStringToBase64")
        {
            called = "llbase64.encode";
            return Expr{ "llbase64.encode(" + coerced(argumentAt(e, 0), LST_STRING).text + ")" };
        }
        // The bytes decoded, every one: LSL's string could hold neither a
        // NUL nor what is not UTF-8.
        if (lsl == "llBase64ToString")
        {
            noteOnce(e, "SluaBase64Decode", "llbase64.decode keeps every byte it decodes, a NUL and what is not UTF-8 among them, where "
                                            "LSL's llBase64ToString cut the string at a NUL and made what was not UTF-8 '?'.");
            called = "llbase64.decode";
            return Expr{ "llbase64.decode(" + coerced(argumentAt(e, 0), LST_STRING).text + ")" };
        }
        // Half up, as LSL rounds, where math.round rounds a half away from
        // nought.
        if (lsl == "llRound")
        {
            called = "math.floor";
            return Expr{ "math.floor(" + bracketed(arg(0), ADD) + " + 0.5)" };
        }
        // Characters, as LSL counts them: its strings are always UTF-8,
        // which utf8.len answers nil for none of.
        if (lsl == "llStringLength")
        {
            // Bracketed: after `::` Luau reads a < or a - as more type.
            called = "utf8.len";
            return Expr{ "(utf8.len(" + coerced(argumentAt(e, 0), LST_STRING).text + ") :: number)" };
        }
        // Luau's math where it answers what LSL's did.
        static const boost::unordered_flat_map<std::string_view, std::string_view> MATH = {
            { "llAbs", "abs" },   { "llFabs", "abs" },   { "llAcos", "acos" }, { "llAsin", "asin" },   { "llAtan2", "atan2" },
            { "llCeil", "ceil" }, { "llCos", "cos" },    { "llFloor", "floor" }, { "llSin", "sin" },   { "llTan", "tan" },
            { "llSqrt", "sqrt" }, { "llLog", "log" },    { "llLog10", "log10" },
        };
        if (const auto found = MATH.find(lsl); found != MATH.end())
        {
            if (lsl == "llSqrt" || lsl == "llLog" || lsl == "llLog10")
            {
                noteOnce(e, "SluaMathDomain", "math.sqrt, math.log and math.log10 answer nan or -inf out of their range, where LSL "
                                              "answered 0 or stopped with a math error.");
            }
            std::string out;
            for (LSLASTNode* a = isNull(e->getArguments()) ? nullptr : e->getArguments()->getChild(0); a; a = a->getNext())
            {
                out += (out.empty() ? "" : ", ") + value(static_cast<LSLExpression*>(a)).text;
            }
            called = "math." + std::string(found->second);
            return Expr{ called + "(" + out + ")" };
        }
        return std::nullopt;
    }

    LSLFunctionExpression* Writer::findCall(LSLExpression* e)
    {
        e = unbracketed(e);
        if (!mOptions.sluaCalls || !e || e->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
        {
            return nullptr;
        }
        auto*      call   = static_cast<LSLFunctionExpression*>(e);
        LSLSymbol* symbol = call->getIdentifier()->getSymbol();
        if (!symbol || symbol->getSubType() != SYM_BUILTIN)
        {
            return nullptr;
        }
        const ALLSLTraits::Trait* trait = ALLSLTraits::of(call->getIdentifier()->getName());
        if (!trait || !(trait->slua & ALLSLTraits::SluaIndexResult) || (trait->slua & (ALLSLTraits::SluaRemoved | ALLSLTraits::SluaBool)))
        {
            return nullptr;
        }
        return !(trait->slua & ALLSLTraits::SluaIndexArgs) || constantIndexes(call, trait->sluaIndexArgs) ? call : nullptr;
    }

    std::optional<Expr> Writer::foundTest(LSLExpression* e, bool found)
    {
        LSLFunctionExpression* call = findCall(e);
        if (!call)
        {
            return std::nullopt;
        }
        const std::string lsl = call->getIdentifier()->getName();
        std::string       asked;
        // One thing looked for in a list: table.find, which answers as
        // ll.ListFindList does.
        LSLASTNode* sought = mOptions.idioms && lsl == "llListFindList" ? soleItem(argumentAt(call, 1)) : nullptr;
        if (sought)
        {
            asked = "table.find(" + anyItems(argumentAt(call, 0), value(argumentAt(call, 0))) + ", " + itemText(sought) + ")";
        }
        // Text in other text: string.find's plain search from the start,
        // which finds it where LSL's did -- the empty text anywhere, as
        // LSL's found it at 0 -- and asks nothing of where in characters.
        else if (mOptions.idioms && lsl == "llSubStringIndex")
        {
            asked = "string.find(" + coerced(argumentAt(call, 0), LST_STRING).text + ", " + coerced(argumentAt(call, 1), LST_STRING).text +
                    ", 1, true)";
        }
        else
        {
            const ALLSLTraits::Trait* trait = ALLSLTraits::of(lsl.c_str());
            asked = "ll." + lsl.substr(2) + "(" + llArgs(call, trait ? trait->sluaIndexArgs : 0) + ")";
        }
        return Expr{ asked + (found ? " ~= nil" : " == nil"), COMPARE, true };
    }

    Expr Writer::call(LSLFunctionExpression* e)
    {
        // What the call is named in SLua, where that is another name.
        std::string called;
        Expr        written = writeCall(e, called);
        if (!called.empty())
        {
            sameAs(e->getIdentifier()->getName(), called);
        }
        return written;
    }

    Expr Writer::writeCall(LSLFunctionExpression* e, std::string& called)
    {
        LSLIdentifier* id     = e->getIdentifier();
        LSLSymbol*     symbol = id->getSymbol();
        LSLParamList*  params = symbol ? symbol->getFunctionDecl() : nullptr;
        if (!symbol || symbol->getSubType() != SYM_BUILTIN)
        {
            return { nameOf(id) + "(" + args(e->getArguments(), params) + ")", PRIMARY, boolean(symbol) };
        }
        const std::string lsl  = id->getName();
        const std::string bare = lsl.rfind("ll", 0) == 0 ? lsl.substr(2) : lsl;
        // A list's length is its length.
        if (lsl == "llGetListLength")
        {
            return { "#" + bracketed(value(static_cast<LSLExpression*>(e->getArguments()->getChild(0))), UNARY), UNARY };
        }
        if (lsl == "llSetTimerEvent" && mTimers)
        {
            called = "setTimer";
            return { "setTimer(" + args(e->getArguments(), params) + ")" };
        }
        if (std::optional<Expr> time = scriptTime(lsl, called))
        {
            return *time;
        }
        if (mOptions.detectedTable && mInDetected)
        {
            if (std::optional<Expr> read = detected(e, lsl))
            {
                return *read;
            }
        }
        if (mOptions.idioms)
        {
            if (std::optional<Expr> written = idiom(e, lsl, called))
            {
                return *written;
            }
        }
        const ALLSLTraits::Trait* trait   = ALLSLTraits::of(lsl.c_str());
        const U8                  slua    = trait ? trait->slua : 0;
        const U16                 indexes = trait ? trait->sluaIndexArgs : 0;
        // One SLua has nowhere: said, and its type's empty value in its
        // place where what it answered is read (Writer::effect leaves a
        // call alone out).
        if (slua & ALLSLTraits::SluaAbsent)
        {
            noteOnce(e, "SluaAbsent", "SLua has no [1], in ll or in llcompat: it is left out.", { lsl });
            return { defaultOf(e->getIType()) };
        }
        // SLua's word on one it deprecates, what it would use and why: which
        // says more than how its indexes count. The why is the definitions'
        // own. But the two XorBase64Strings: SLua names ll.XorBase64 for
        // both, with no why, and it answers otherwise than either -- said
        // how, since LSL's is kept.
        const auto deprecated = [&]() {
            if (lsl == "llXorBase64Strings")
            {
                noteOnce(e, "SluaXorBase64Wrong",
                         "SLua deprecates ll.XorBase64Strings, for ll.XorBase64, which XORs correctly where this did not, and so answers "
                         "otherwise: ll.XorBase64Strings is LSL's.");
            }
            else if (lsl == "llXorBase64StringsCorrect")
            {
                noteOnce(e, "SluaXorBase64Nul",
                         "SLua deprecates ll.XorBase64StringsCorrect, for ll.XorBase64, which answers otherwise where the second string "
                         "holds a NUL, which ended it here: ll.XorBase64StringsCorrect is LSL's.");
            }
            else if (trait->sluaUse && trait->sluaReason)
            {
                noteOnce(e, "SluaDeprecatedForWhy", "SLua deprecates ll.[1], for [2]: [3]", { bare, trait->sluaUse, trait->sluaReason });
            }
            else if (trait->sluaUse)
            {
                noteOnce(e, "SluaDeprecatedFor", "SLua deprecates ll.[1], for [2].", { bare, trait->sluaUse });
            }
            else if (trait->sluaReason)
            {
                noteOnce(e, "SluaDeprecatedWhy", "SLua deprecates ll.[1]: [2]", { bare, trait->sluaReason });
            }
            else
            {
                noteOnce(e, "SluaDeprecated", "SLua deprecates ll.[1].", { bare });
            }
        };
        // SLua's ll where it means the same: a boolean answer, which a
        // condition reads as it is and a number takes as 1 or 0; index
        // arguments written out, moved on by one. Not where the list it
        // answers has booleans in LSL's 1 and 0's places, nor one SLua
        // deprecates that differs from llcompat's, which keeps LSL's where
        // no way of SLua's own (Writer::idiom) is sure to mean the same.
        // One SLua deprecates and nothing else is the one function in both,
        // deprecated in both, as every llcompat function is: ll's, whose
        // deprecation names what SLua would use.
        const U8 compat_only =
            ALLSLTraits::SluaRemoved | ALLSLTraits::SluaIndexResult | ALLSLTraits::SluaBoolList | ALLSLTraits::SluaDeprecated;
        const bool ll_indexes      = !(slua & ALLSLTraits::SluaIndexArgs) || constantIndexes(e, indexes);
        const bool only_deprecated = slua == ALLSLTraits::SluaDeprecated;
        if (slua == 0 || only_deprecated || (mOptions.sluaCalls && !(slua & compat_only) && ll_indexes))
        {
            called = "ll." + bare;
            if (only_deprecated)
            {
                deprecated();
            }
            else if (trait && trait->sluaUse)
            {
                noteOnce(e, "SluaUse", "SLua would use [1] for [2].", { trait->sluaUse, lsl });
            }
            const std::string args_text =
                (slua & ALLSLTraits::SluaIndexArgs) ? llArgs(e, indexes) : args(e->getArguments(), params, trait ? trait->sluaTextArgs : 0, -1, {}, true);
            return { called + "(" + args_text + ")", PRIMARY, (slua & ALLSLTraits::SluaBool) != 0 };
        }
        // An index SLua's ll counts from 1, or nil for none, read as LSL's:
        // from 0, or -1.
        const U8 not_ll = compat_only & ~ALLSLTraits::SluaIndexResult;
        if (mOptions.sluaCalls && (slua & ALLSLTraits::SluaIndexResult) && !(slua & not_ll) && ll_indexes)
        {
            const std::string args_text = (slua & ALLSLTraits::SluaIndexArgs) ? llArgs(e, indexes) : args(e->getArguments(), params, 0, -1, {}, true);
            called                      = "ll." + bare;
            return { "(" + called + "(" + args_text + ") or 0) - 1", ADD, false, called + "(" + args_text + ")" };
        }
        if (slua & ALLSLTraits::SluaRemoved)
        {
            if (lsl == "llSetTimerEvent")
            {
                noteOnce(e, "SluaCompatOnlyTimer", "SLua's ll has no SetTimerEvent; llcompat's is LSL's. SLua's own timers are "
                                                   "LLTimers:every(seconds, callback) and LLTimers:once, several at a time.");
            }
            else
            {
                noteOnce(e, "SluaCompatOnly", "SLua's ll has no [1]; llcompat.[1] is LSL's.", { bare });
            }
        }
        else if (lsl.rfind("llDetected", 0) == 0)
        {
            noteOnce(e, "SluaDetected", englishOf("SluaDetected"));
        }
        else if (slua & ALLSLTraits::SluaDeprecated)
        {
            deprecated();
        }
        else if (slua & ALLSLTraits::SluaIndexResult)
        {
            noteOnce(e, "SluaIndexFound", englishOf("SluaIndexFound"), { bare });
        }
        else if (slua & ALLSLTraits::SluaIndexArgs)
        {
            noteOnce(e, "SluaIndex", englishOf("SluaIndex"), { bare });
        }
        else if (slua & ALLSLTraits::SluaBool)
        {
            noteOnce(e, "SluaBool", englishOf("SluaBool"), { bare });
        }
        else if (slua & ALLSLTraits::SluaBoolList)
        {
            noteOnce(e, "SluaBoolList", "llcompat.[1]'s list has 1 or 0 where LSL's did; ll.[1]'s has true or false there.", { bare });
        }
        if (trait && trait->sluaUse && !(slua & ALLSLTraits::SluaDeprecated))
        {
            noteOnce(e, "SluaUse", "SLua would use [1] for [2].", { trait->sluaUse, lsl });
        }
        called = "llcompat." + bare;
        return { called + "(" + args(e->getArguments(), params, 0, -1, {}, true) + ")" };
    }

    Expr Writer::signedUnless(bool same, const std::string& call, LSLASTNode* at)
    {
        if (same)
        {
            return { call };
        }
        if (!mSaidSigned)
        {
            mSaidSigned = true;
            note(at, "SluaBit32Signed",
                 "bit32 answers 0 to 4294967295, where LSL's integers were signed: bit32.s32 makes the answer LSL's where it could pass "
                 "2147483647 and is read as a number.");
        }
        return { "bit32.s32(" + call + ")" };
    }

    bool Writer::notBelowZero(LSLExpression* e) const
    {
        if (nonNegative(e))
        {
            return true;
        }
        e = unbracketed(e);
        if (e && e->getNodeSubType() == NODE_BINARY_EXPRESSION)
        {
            // An & with one, an | or ^ of two, a >> of one.
            auto* b = static_cast<LSLBinaryExpression*>(e);
            switch (b->getOperation())
            {
                case OP_BIT_AND: return notBelowZero(b->getLHS()) || notBelowZero(b->getRHS());
                case OP_BIT_OR:
                case OP_BIT_XOR: return notBelowZero(b->getLHS()) && notBelowZero(b->getRHS());
                case OP_SHIFT_RIGHT: return notBelowZero(b->getLHS());
                default: return false;
            }
        }
        return e && e->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(e)->getMember() &&
               mSteadyNonNegative.contains(static_cast<LSLLValueExpression*>(e)->getIdentifier()->getSymbol());
    }

    Expr Writer::binary(LSLBinaryExpression* e)
    {
        // == and ~= the other way round under a not (unary).
        const LSLOperator op  = !mFlipped.contains(e) ? e->getOperation() : e->getOperation() == OP_EQ ? OP_NEQ : OP_EQ;
        LSLExpression*    lhs = e->getLHS();
        LSLExpression*    rhs = e->getRHS();
        const LSLIType    lt  = slType(lhs);
        const LSLIType    rt  = slType(rhs);
        const auto        infix = [&](const char* word, int prec, bool boolean = false, bool right_assoc = false) -> Expr {
            const Expr a = value(lhs);
            const Expr b = value(rhs);
            if (prec == COMPARE && boolean)
            {
                if (std::optional<Expr> compared = placeCompared(word, a, b, lhs, rhs))
                {
                    return *compared;
                }
            }
            return { bracketed(a, right_assoc ? prec + 1 : prec) + " " + word + " " + bracketed(b, right_assoc ? prec : prec + 1), prec, boolean };
        };
        // A find's answer asked only whether it found: against nil, as ll
        // answers, where LSL's -1 said nothing was found.
        {
            int         v    = 0;
            const bool  left = findCall(lhs) != nullptr;
            const bool  right = !left && findCall(rhs) != nullptr;
            LSLExpression* call  = left ? lhs : rhs;
            LSLExpression* other = left ? rhs : lhs;
            if ((left || right) && wholeNumber(other, v))
            {
                // Each test by what it says of the find, with the call on
                // its left: found, or not.
                std::optional<bool> found;
                LSLOperator         as = op;
                if (right)
                {
                    as = op == OP_LESS ? OP_GREATER : op == OP_GREATER ? OP_LESS : op == OP_LEQ ? OP_GEQ : op == OP_GEQ ? OP_LEQ : op;
                }
                if ((as == OP_EQ && v == -1) || (as == OP_LESS && v == 0) || (as == OP_LEQ && v == -1))
                {
                    found = false;
                }
                else if ((as == OP_NEQ && v == -1) || (as == OP_GEQ && v == 0) || (as == OP_GREATER && v == -1))
                {
                    found = true;
                }
                if (found)
                {
                    if (std::optional<Expr> test = foundTest(call, *found))
                    {
                        return *test;
                    }
                }
            }
        }
        // Both sides run, the right first in LSL: said where that could
        // show, but for && and ||, whose note says so.
        const bool assigns = op == OP_ASSIGN || op == OP_ADD_ASSIGN || op == OP_SUB_ASSIGN || op == OP_MUL_ASSIGN || op == OP_DIV_ASSIGN ||
                             op == OP_MOD_ASSIGN;
        const bool andOr = op == OP_BOOLEAN_AND || op == OP_BOOLEAN_OR;
        if (!assigns && !(andOr && !ALLSLTraits::sideEffectFree(rhs)) && orderMatters(lhs, rhs))
        {
            note(e, "SluaRightFirst",
                 "LSL ran the right side of this before the left, and Luau runs the left first: one side changes what the other reads.");
        }
        const auto bit = [&](const char* fn) -> Expr {
            // bit32 answers 0 to 4294967295, where LSL's integers were signed:
            // made LSL's by bit32.s32, but where the answer is the same
            // number -- an & with a number not below nought, an | or ^ of
            // two, a >> of one -- or where it is only asked whether it is
            // nought, or bit32 takes it again.
            const bool same = (op == OP_BIT_AND && (notBelowZero(lhs) || notBelowZero(rhs))) ||
                              ((op == OP_BIT_OR || op == OP_BIT_XOR) && notBelowZero(lhs) && notBelowZero(rhs)) ||
                              (op == OP_SHIFT_RIGHT && notBelowZero(lhs)) || truthOnly(e) || intoBit32(e);
            // An &, | or ^ of the same again, one call of them all: bit32's
            // take as many as are given.
            if (op == OP_BIT_AND || op == OP_BIT_OR || op == OP_BIT_XOR)
            {
                std::string                                   all;
                const std::function<void(LSLExpression* one)> gather = [&](LSLExpression* one) {
                    LSLExpression* inner = unbracketed(one);
                    if (inner->getNodeSubType() == NODE_BINARY_EXPRESSION && inner->getOperation() == op)
                    {
                        gather(static_cast<LSLBinaryExpression*>(inner)->getLHS());
                        gather(static_cast<LSLBinaryExpression*>(inner)->getRHS());
                        return;
                    }
                    all += (all.empty() ? "" : ", ") + value(one).text;
                };
                gather(lhs);
                gather(rhs);
                return signedUnless(same, std::string("bit32.") + fn + "(" + all + ")", e);
            }
            return signedUnless(same, std::string("bit32.") + fn + "(" + value(lhs).text + ", " + value(rhs).text + ")", e);
        };
        switch (op)
        {
            case OP_ASSIGN:
            case OP_ADD_ASSIGN:
            case OP_SUB_ASSIGN:
            case OP_MUL_ASSIGN:
            case OP_DIV_ASSIGN:
            case OP_MOD_ASSIGN:
                return sideEffect(e);
            case OP_PLUS:
                if (lt == LST_LIST || rt == LST_LIST)
                {
                    // A new list of both, the left's then the right's:
                    // table.extend puts the right's on the end of the left
                    // and answers it. The left is copied first where
                    // something else may hold it. A new one -- written out,
                    // or a library call's -- needs no copy, and is said to
                    // hold any value, as LSL's lists did: Luau's old solver
                    // would otherwise hold every item of both to the type of
                    // the left's first. Another +'s answer already is one.
                    const Expr     a     = value(lhs);
                    const Expr     b     = value(rhs);
                    LSLExpression* left  = unbracketed(lhs);
                    std::string    first = madeHere(lhs) ? anyItems(lhs, a) : "table.clone(" + a.text + ")";
                    if (lt != LST_LIST)
                    {
                        first = "{" + a.text + "} :: { any }";
                    }
                    else if (left->getNodeSubType() == NODE_BINARY_EXPRESSION && left->getOperation() == OP_PLUS)
                    {
                        first = a.text;
                    }
                    return { "table.extend(" + first + ", " + (rt == LST_LIST ? b.text : "{" + b.text + "}") + ")" };
                }
                if (lt == LST_STRING || lt == LST_KEY || rt == LST_STRING || rt == LST_KEY)
                {
                    // The whole chain as one string, where a piece of it asks
                    // for a conversion.
                    std::vector<LSLExpression*> pieces;
                    textPieces(e, pieces);
                    if (std::optional<Expr> joined = interpolated(pieces))
                    {
                        return *joined;
                    }
                    const Expr a = joinedPiece(lhs);
                    const Expr b = joinedPiece(rhs);
                    sameAs("+", "..");
                    // Joining text is the same whichever way round it goes:
                    // a chain unbracketed.
                    return { bracketed(a, CONCAT) + " .. " + bracketed(b, CONCAT), CONCAT };
                }
                return infix("+", ADD);
            case OP_MINUS: return infix("-", ADD);
            case OP_MUL:
                if (lt == LST_VECTOR && rt == LST_VECTOR)
                {
                    return { "vector.dot(" + value(lhs).text + ", " + value(rhs).text + ")" };
                }
                return infix("*", MUL);
            case OP_DIV:
                if (lt == LST_INTEGER && rt == LST_INTEGER)
                {
                    // LSL's integers divide toward nought, and Luau's // rounds
                    // down: the same where neither side is below nought.
                    if (notBelowZero(lhs) && notBelowZero(rhs))
                    {
                        return infix("//", MUL);
                    }
                    // Elsewhere the quotient as a double, cut toward nought by
                    // bit32.s32: for two 32-bit integers a double's quotient is
                    // never so far out that the cut misses LSL's. math.modf
                    // would cut it too, but leave a -0 that tostring writes,
                    // and -2147483648 / -1 unwrapped, where LSO wraps it.
                    return { "bit32.s32(" + infix("/", MUL).text + ")" };
                }
                return infix("/", MUL);
            case OP_MOD:
                if (lt == LST_VECTOR && rt == LST_VECTOR)
                {
                    return { "vector.cross(" + value(lhs).text + ", " + value(rhs).text + ")" };
                }
                // LSL's remainder takes the dividend's sign, as math.fmod's
                // does, and Luau's % the divisor's: the same where neither
                // side is below nought. Only text tells fmod's from LSL's:
                // a dividend below nought that divides whole leaves -0.
                if (notBelowZero(lhs) && notBelowZero(rhs))
                {
                    return infix("%", MUL);
                }
                return { "math.fmod(" + value(lhs).text + ", " + value(rhs).text + ")" };
            case OP_EQ:
            case OP_NEQ:
            {
                if (std::optional<Expr> asked = truthAsked(e, op))
                {
                    return *asked;
                }
                const bool eq = op == OP_EQ;
                // An & against nought: whether it is.
                {
                    int                 zero = 1;
                    std::optional<Expr> test = wholeNumber(rhs, zero) && zero == 0 ? bitTest(lhs)
                                               : wholeNumber(lhs, zero) && zero == 0 ? bitTest(rhs)
                                                                                     : std::nullopt;
                    if (test)
                    {
                        return eq ? Expr{ "not " + test->text, UNARY, true } : *test;
                    }
                }
                if (lt == LST_LIST && rt == LST_LIST)
                {
                    // LSL compares two lists by their lengths alone; != says
                    // how much longer the left one is.
                    note(e, "SluaListCompare", "LSL compares lists by their lengths alone, and != answers the difference of them.");
                    const Expr a = value(lhs);
                    const Expr b = value(rhs);
                    return eq ? Expr{ "#" + bracketed(a, UNARY) + " == #" + bracketed(b, UNARY), COMPARE, true }
                              : Expr{ "#" + bracketed(a, UNARY) + " - #" + bracketed(b, UNARY), ADD };
                }
                if ((lt == LST_KEY && rt == LST_STRING) || (lt == LST_STRING && rt == LST_KEY))
                {
                    // A key against a string, as LSL compares them: as text.
                    const Expr a = coerced(lhs, LST_STRING);
                    const Expr b = coerced(rhs, LST_STRING);
                    return { bracketed(a, COMPARE) + (eq ? " == " : " ~= ") + bracketed(b, COMPARE + 1), COMPARE, true };
                }
                return infix(eq ? "==" : "~=", COMPARE, true);
            }
            case OP_LESS: return infix("<", COMPARE, true);
            case OP_GREATER: return infix(">", COMPARE, true);
            case OP_LEQ: return infix("<=", COMPARE, true);
            case OP_GEQ: return infix(">=", COMPARE, true);
            case OP_BOOLEAN_AND:
            case OP_BOOLEAN_OR:
            {
                const bool and_ = op == OP_BOOLEAN_AND;
                // Said where leaving it unrun could show: not a read of what
                // changes, which changes nothing.
                if (!ALLSLTraits::changesNothing(rhs))
                {
                    note(e, "SluaShortCircuit", "[1] leaves its right side unrun once the left decides it; LSL ran both sides, the right one first.",
                         { and_ ? "and" : "or" });
                }
                const int  prec = and_ ? AND : OR;
                const Expr a    = condition(lhs);
                const Expr b    = condition(rhs);
                return { bracketed(a, prec) + (and_ ? " and " : " or ") + bracketed(b, prec + 1), prec, true };
            }
            case OP_BIT_AND: return bit("band");
            case OP_BIT_OR: return bit("bor");
            case OP_BIT_XOR: return bit("bxor");
            case OP_SHIFT_LEFT: return bit("lshift");
            case OP_SHIFT_RIGHT: return bit("arshift");
            default:
                return infix(operation_repr_str(op), COMPARE);
        }
    }

    Expr Writer::unary(LSLUnaryExpression* e)
    {
        LSLExpression* child = e->getChildExpr();
        switch (e->getOperation())
        {
            case OP_MINUS:
            {
                // Bracketed where it begins with a minus of its own, which
                // after this one would be a comment.
                const Expr v = value(child);
                return { "-" + (!v.text.empty() && v.text.front() == '-' ? "(" + v.text + ")" : bracketed(v, UNARY)), UNARY };
            }
            case OP_BOOLEAN_NOT:
            {
                // !(a == b) as a ~= b, and !(a != b) as a == b: not of two
                // lists, whose != is how much longer the left is.
                LSLExpression* inner = unbracketed(child);
                const bool compare = inner->getNodeSubType() == NODE_BINARY_EXPRESSION &&
                                     (inner->getOperation() == OP_EQ || inner->getOperation() == OP_NEQ) &&
                                     !(static_cast<LSLBinaryExpression*>(inner)->getLHS()->getIType() == LST_LIST &&
                                       static_cast<LSLBinaryExpression*>(inner)->getRHS()->getIType() == LST_LIST);
                if (compare)
                {
                    mFlipped.insert(inner);
                    const Expr flipped = condition(inner);
                    mFlipped.erase(inner);
                    return { flipped.text, flipped.prec, true };
                }
                // A whole number, the only thing LSL's ! takes, asked
                // whether it is nought: n == 0, not not (n ~= 0). Not what
                // condition() says its own way.
                const bool plain = inner->getNodeSubType() != NODE_BOOL_CONVERSION_EXPRESSION && !uuidConstant(inner) &&
                                   !(inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_BIT_NOT) &&
                                   !(inner->getNodeSubType() == NODE_LVALUE_EXPRESSION &&
                                     static_cast<LSLLValueExpression*>(inner)->getIdentifier()->getSymbol() &&
                                     static_cast<LSLLValueExpression*>(inner)->getIdentifier()->getSymbol()->getSubType() == SYM_BUILTIN);
                if (plain && inner->getIType() == LST_INTEGER)
                {
                    if (std::optional<Expr> test = bitTest(inner))
                    {
                        return { "not " + test->text, UNARY, true };
                    }
                    const Expr v = expr(inner);
                    if (v.boolean)
                    {
                        return { "not " + bracketed(v, UNARY), UNARY, true };
                    }
                    return { bracketed(v, COMPARE + 1) + " == 0", COMPARE, true };
                }
                return { "not " + bracketed(condition(child), UNARY), UNARY, true };
            }
            case OP_BIT_NOT:
                return signedUnless(truthOnly(e) || intoBit32(e), "bit32.bnot(" + value(child).text + ")", e);
            case OP_PRE_INCR:
            case OP_PRE_DECR:
            case OP_POST_INCR:
            case OP_POST_DECR:
                return sideEffect(e);
            default:
                return value(child);
        }
    }

    Expr Writer::typecast(LSLTypecastExpression* e)
    {
        LSLExpression* child = e->getChildExpr();
        const LSLIType to    = e->getIType();
        const LSLIType from  = slType(child);
        const Expr     v     = value(child);
        if (to == from)
        {
            return v;
        }
        switch (to)
        {
            case LST_INTEGER:
                if (from == LST_FLOATINGPOINT)
                {
                    // Toward zero, as LSL cuts a float.
                    return { "(math.modf(" + v.text + "))" };
                }
                if (from == LST_STRING)
                {
                    mLslInteger = true;
                    return { "lslInteger(" + v.text + ")" };
                }
                return v;
            case LST_FLOATINGPOINT:
                if (from == LST_STRING)
                {
                    mLslFloat = true;
                    return { "lslFloat(" + v.text + ")" };
                }
                return v;
            case LST_STRING:
                switch (from)
                {
                    case LST_FLOATINGPOINT: return { "string.format(\"%.6f\", " + v.text + ")" };
                    case LST_LIST:
                        // Text and whole numbers joined as LSL wrote them
                        // (Writer::idiom).
                        if (mOptions.idioms && (itemTypes(child) & ~(ItemString | ItemInteger)) == 0)
                        {
                            return { "table.concat(" + v.text + ")" };
                        }
                        return { "ll.DumpList2String(" + v.text + ", \"\")" };
                    case LST_VECTOR:
                    case LST_QUATERNION:
                        // LSL writes each part with five places, as ll's
                        // DumpList2String still does; SLua's tostring as
                        // few as each needs.
                        noteOnce(e, "SluaVectorText",
                                 "ll.DumpList2String writes a vector's or a rotation's parts as LSL did, with five places; SLua's tostring "
                                 "writes as few as each needs, <1, 2, 3>, which tovector reads back the same.");
                        return { "ll.DumpList2String({" + v.text + "}, \"\")" };
                    default: return { "tostring(" + v.text + ")" };
                }
            case LST_KEY:
            {
                // Text written out, said for what uuid makes of it where that
                // is another key than LSL's: (key)"" is NULL_KEY.
                LSLExpression* inner   = unbracketed(child);
                const bool     written = inner && inner->getNodeSubType() == NODE_CONSTANT_EXPRESSION &&
                                         inner->getChild(0)->getNodeSubType() == NODE_STRING_CONSTANT;
                if (!written || !keyText(e, static_cast<LSLStringConstant*>(inner->getChild(0))->getValue()))
                {
                    note(e, "SluaUuid", "SLua's uuid holds a key; LSL's key could hold any text.");
                }
                return { "uuid(" + v.text + ")" };
            }
            // Text that is no vector or rotation is LSL's zero one; bracketed
            // only where an operator around it binds more tightly than or.
            case LST_VECTOR: return { "tovector(" + v.text + ") or ZERO_VECTOR", OR };
            case LST_QUATERNION: return { "toquaternion(" + v.text + ") or ZERO_ROTATION", OR };
            case LST_LIST: return { "{" + v.text + "}" };
            default: return v;
        }
    }

    Expr Writer::sideEffect(LSLExpression* e)
    {
        // Made as a statement of its own, before or after: its variable.
        if (mHoisted.contains(e))
        {
            LSLExpression* target = e->getNodeSubType() == NODE_UNARY_EXPRESSION ? static_cast<LSLUnaryExpression*>(e)->getChildExpr()
                                                                                 : static_cast<LSLBinaryExpression*>(e)->getLHS();
            return lvalue(static_cast<LSLLValueExpression*>(target));
        }
        note(e, "SluaAssignInExpression", "SLua's assignments are statements: this one is made by a function called where it stood.");
        // The assignment, then what it gives: the variable after it, or
        // before it for a step after.
        const std::string saved  = mText;
        const int         depth  = mDepth;
        const bool        inline_ = mInline;
        mText.clear();
        mDepth  = 0;
        mInline = true;
        LSLLValueExpression* target;
        const LSLOperator    op   = e->getOperation();
        const bool           post = op == OP_POST_INCR || op == OP_POST_DECR;
        if (e->getNodeSubType() == NODE_UNARY_EXPRESSION)
        {
            target = static_cast<LSLLValueExpression*>(static_cast<LSLUnaryExpression*>(e)->getChildExpr());
        }
        else
        {
            target = static_cast<LSLLValueExpression*>(static_cast<LSLBinaryExpression*>(e)->getLHS());
        }
        const std::string name = lvalue(target).text;
        if (post)
        {
            mText += "local was = " + name + "; ";
        }
        effect(e);
        std::string made = mText;
        while (!made.empty() && made.back() == '\n')
        {
            made.pop_back();
        }
        std::replace(made.begin(), made.end(), '\n', ' ');
        mText   = saved;
        mDepth  = depth;
        mInline = inline_;
        return { "(function() " + made + " return " + (post ? "was" : name) + " end)()" };
    }

    Expr Writer::expr(LSLExpression* e)
    {
        if (isNull(e))
        {
            return { "nil" };
        }
        switch (e->getNodeSubType())
        {
            case NODE_CONSTANT_EXPRESSION: return constant(static_cast<LSLConstant*>(e->getChild(0)));
            case NODE_PARENTHESIS_EXPRESSION: return expr(static_cast<LSLParenthesisExpression*>(e)->getChildExpr());
            case NODE_LVALUE_EXPRESSION: return lvalue(static_cast<LSLLValueExpression*>(e));
            case NODE_FUNCTION_EXPRESSION: return call(static_cast<LSLFunctionExpression*>(e));
            case NODE_BINARY_EXPRESSION: return binary(static_cast<LSLBinaryExpression*>(e));
            case NODE_UNARY_EXPRESSION: return unary(static_cast<LSLUnaryExpression*>(e));
            case NODE_TYPECAST_EXPRESSION: return typecast(static_cast<LSLTypecastExpression*>(e));
            case NODE_BOOL_CONVERSION_EXPRESSION: return condition(static_cast<LSLBoolConversionExpression*>(e)->getChildExpr());
            case NODE_PRINT_EXPRESSION: return { "print(" + value(static_cast<LSLPrintExpression*>(e)->getChildExpr()).text + ")" };
            case NODE_VECTOR_EXPRESSION:
            {
                auto* v = static_cast<LSLVectorExpression*>(e);
                sameAs("<", "vector(");
                sameAs(">", ")");
                return { "vector(" + value(v->getX()).text + ", " + value(v->getY()).text + ", " + value(v->getZ()).text + ")" };
            }
            case NODE_QUATERNION_EXPRESSION:
            {
                auto* q = static_cast<LSLQuaternionExpression*>(e);
                sameAs("<", "quaternion(");
                sameAs(">", ")");
                return { "quaternion(" + value(q->getX()).text + ", " + value(q->getY()).text + ", " + value(q->getZ()).text + ", " +
                         value(q->getS()).text + ")" };
            }
            case NODE_LIST_EXPRESSION:
            {
                // Over the lines the LSL wrote it over; but not in a line
                // made inside an expression, which is made one line.
                if (!mInline && e->getChild(0) && !isNull(e->getChild(0)) && e->getLoc()->first_line != e->getLoc()->last_line)
                {
                    return { listLines(e) };
                }
                std::string out;
                for (LSLASTNode* item = e->getChild(0); item; item = item->getNext())
                {
                    // As LSL typed it: NULL_KEY in a list is LSL's string.
                    auto* each = static_cast<LSLExpression*>(item);
                    out += (out.empty() ? "" : ", ") + coerced(each, each->getIType()).text;
                }
                return { "{" + out + "}" };
            }
            default:
                return { "nil" };
        }
    }

    std::string Writer::listLines(LSLASTNode* list)
    {
        const auto begins = [](LSLASTNode* n) { return Pos{ n->getLoc()->first_line, n->getLoc()->first_column }; };
        const auto ends   = [](LSLASTNode* n) { return Pos{ n->getLoc()->last_line, n->getLoc()->last_column }; };
        std::vector<LSLExpression*> items;
        for (LSLASTNode* item = list->getChild(0); item; item = item->getNext())
        {
            items.push_back(static_cast<LSLExpression*>(item));
        }
        // Its comments, in order: those in no item of it, an item's being
        // its own to place. Not a toggle's, which is about the code over or
        // under it.
        const Pos           open  = begins(list);
        const Pos           close = ends(list);
        std::vector<size_t> comments;
        const auto from = std::lower_bound(mComments.begin(), mComments.end(), open, [](const Comment& c, const Pos& p) { return c.at < p; });
        for (auto it = from; it != mComments.end() && it->at < close; ++it)
        {
            const Comment& c = *it;
            if (c.written || c.closer || c.text.rfind("---[", 0) == 0 ||
                std::any_of(items.begin(), items.end(), [&](LSLExpression* item) { return !(c.at < begins(item)) && c.at < ends(item); }))
            {
                continue;
            }
            comments.push_back(static_cast<size_t>(it - mComments.begin()));
        }
        ++mListDepth;
        std::string out  = "{";
        // The LSL line what is written ends on, and the comment next.
        S32         line = open.line;
        size_t      next = 0;
        // Those before `upto`: after what is on the line so far, on lines of
        // their own, then before what is on the line `upto` is on; a break
        // to that line, written at `in`, or a space where there was one.
        const auto to = [&](const Pos& upto, const std::string& in, bool spaced) {
            std::string before;
            for (; next < comments.size() && mComments[comments[next]].at < upto; ++next)
            {
                const size_t k  = comments[next];
                const S32    at = mComments[k].at.line;
                if (at == line)
                {
                    out += marked(k, Marked::After);
                }
                else if (at < upto.line)
                {
                    out += "\n" + listIndent(0) + marked(k, Marked::Own);
                }
                else
                {
                    before += marked(k, Marked::Before);
                }
            }
            if (upto.line > line)
            {
                out += "\n" + in;
            }
            else if (spaced)
            {
                out += " ";
            }
            out += before;
        };
        for (size_t i = 0; i < items.size(); ++i)
        {
            LSLExpression* item = items[i];
            if (i)
            {
                out += ",";
            }
            to(begins(item), listIndent(0), i > 0);
            // As LSL typed it: NULL_KEY in a list is LSL's string.
            out += coerced(item, item->getIType()).text;
            line = item->getLoc()->last_line;
        }
        to(close, listIndent(-1), false);
        out += "}";
        --mListDepth;
        return out;
    }

    // --- statements -------------------------------------------------------------------

    void Writer::assign(LSLLValueExpression* target, LSLOperator op, LSLExpression* rhs)
    {
        LSLIdentifier* id      = target->getIdentifier();
        const LSLIType type    = varType(id->getSymbol(), id->getSymbol() ? id->getSymbol()->getIType() : target->getIType());
        const std::string name = nameOf(id);
        LSLASTNode*    member  = target->getMember();
        // What the new value is, from the old one where the operator makes
        // it so.
        const auto made = [&](const std::string& old, LSLIType t) -> std::string {
            const Expr v = rhs ? value(rhs) : Expr{ "1" };
            switch (op)
            {
                case OP_ASSIGN: return rhs ? coerced(rhs, t).text : v.text;
                case OP_ADD_ASSIGN:
                case OP_PRE_INCR:
                case OP_POST_INCR:
                    if (t == LST_STRING)
                    {
                        sameAs("+", "..");
                        return old + " .. " + bracketed(joinedPiece(rhs), CONCAT);
                    }
                    if (t == LST_LIST)
                    {
                        // Something else may hold the list: a new one.
                        return "table.extend(table.clone(" + old + "), " + (rhs && rhs->getIType() == LST_LIST ? v.text : "{" + v.text + "}") + ")";
                    }
                    return old + " + " + bracketed(v, ADD + 1);
                case OP_SUB_ASSIGN:
                case OP_PRE_DECR:
                case OP_POST_DECR: return old + " - " + bracketed(v, ADD + 1);
                case OP_MUL_ASSIGN: return old + " * " + bracketed(v, MUL + 1);
                case OP_DIV_ASSIGN:
                    // An integer toward nought, as / of two is written.
                    if (t == LST_INTEGER && rhs && rhs->getIType() == LST_INTEGER)
                    {
                        return "bit32.s32(" + old + " / " + bracketed(v, MUL + 1) + ")";
                    }
                    return old + " / " + bracketed(v, MUL + 1);
                case OP_MOD_ASSIGN:
                    // LSL's % of two vectors is their cross product.
                    if (t == LST_VECTOR)
                    {
                        return "vector.cross(" + old + ", " + v.text + ")";
                    }
                    return "math.fmod(" + old + ", " + v.text + ")";
                default: return v.text;
            }
        };
        if (!isNull(member))
        {
            // A part of a vector or a rotation, which SLua's are made again
            // for: each part as it was, the one assigned as it becomes.
            const std::string part = static_cast<LSLIdentifier*>(member)->getName();
            const bool        rot  = type == LST_QUATERNION;
            std::string       out  = rot ? "quaternion(" : "vector(";
            const char*       parts[] = { "x", "y", "z", "s" };
            for (int i = 0; i < (rot ? 4 : 3); ++i)
            {
                const std::string each = name + "." + parts[i];
                out += (i ? ", " : "") + (part == parts[i] ? made(each, LST_FLOATINGPOINT) : each);
            }
            line(name + " = " + out + ")");
            return;
        }
        if (type == LST_LIST && (op == OP_ADD_ASSIGN || op == OP_ASSIGN) && grow(target, name, op, rhs))
        {
            return;
        }
        if (type == LST_LIST && op == OP_ASSIGN && isNull(member) && editInPlace(id->getSymbol(), name, rhs))
        {
            return;
        }
        // A string whose pieces a loop puts in a table.
        if (const auto building = mBuilding.find(id->getSymbol()); building != mBuilding.end() && rhs)
        {
            LSLBinaryExpression* whole = static_cast<LSLBinaryExpression*>(target->getParent());
            LSLSymbol*           var   = nullptr;
            // A lone piece made a string, so that the table holds strings.
            std::string                       piece;
            const std::vector<LSLExpression*> parts = appended(whole, var);
            std::vector<LSLExpression*>       flat;
            for (LSLExpression* part : parts)
            {
                textPieces(part, flat);
            }
            if (std::optional<Expr> joined = interpolated(flat))
            {
                piece = joined->text;
            }
            else
            {
                sameAs("+", "..");
                for (LSLExpression* part : parts)
                {
                    piece += (piece.empty() ? "" : " .. ") + bracketed(parts.size() > 1 ? joinedPiece(part) : coerced(part, LST_STRING), CONCAT);
                }
            }
            if (!piece.empty())
            {
                line("table.insert(" + building->second + ", " + piece + ")");
                return;
            }
        }
        // x = x op y as x op= y, where Luau has it.
        if (std::vector<LSLExpression*> parts; op == OP_ASSIGN && isNull(member) && selfOperation(id->getSymbol(), type, rhs, op, parts))
        {
            if (type == LST_STRING)
            {
                std::vector<LSLExpression*> flat;
                for (LSLExpression* part : parts)
                {
                    textPieces(part, flat);
                }
                std::string joined;
                if (std::optional<Expr> whole = interpolated(flat))
                {
                    joined = whole->text;
                }
                else
                {
                    sameAs("+", "..");
                    for (LSLExpression* part : parts)
                    {
                        joined += (joined.empty() ? "" : " .. ") + bracketed(joinedPiece(part), CONCAT);
                    }
                }
                sameAs("+=", "..=");
                line(name + " ..= " + joined);
                return;
            }
            rhs = parts.front();
        }
        // Luau's compound assignments where they mean LSL's.
        if (type != LST_LIST && op != OP_ASSIGN)
        {
            // A piece joined on as s = s + piece has it.
            if (type == LST_STRING && op == OP_ADD_ASSIGN && rhs)
            {
                sameAs("+=", "..=");
                line(name + " ..= " + joinedPiece(rhs).text);
                return;
            }
            const Expr v = rhs ? value(rhs) : Expr{ "1" };
            switch (op)
            {
                case OP_ADD_ASSIGN:
                    line(name + " += " + v.text);
                    return;
                case OP_PRE_INCR:
                case OP_POST_INCR: line(name + " += 1"); return;
                case OP_PRE_DECR:
                case OP_POST_DECR: line(name + " -= 1"); return;
                case OP_SUB_ASSIGN: line(name + " -= " + v.text); return;
                case OP_MUL_ASSIGN: line(name + " *= " + v.text); return;
                // An integer's / and % as they are of two: Luau's //= rounds
                // down and its %= takes the divisor's sign, and nothing knows
                // a variable it assigns is not below nought.
                case OP_DIV_ASSIGN:
                    if (type == LST_INTEGER && rhs && rhs->getIType() == LST_INTEGER)
                    {
                        line(name + " = bit32.s32(" + name + " / " + bracketed(v, MUL + 1) + ")");
                        return;
                    }
                    line(name + " /= " + v.text);
                    return;
                case OP_MOD_ASSIGN:
                    if (type == LST_INTEGER)
                    {
                        line(name + " = math.fmod(" + name + ", " + v.text + ")");
                        return;
                    }
                    break;
                default: break;
            }
        }
        if (op == OP_ASSIGN && rhs && boolean(id->getSymbol()))
        {
            line(name + " = " + truthOf(rhs));
            return;
        }
        line(name + " = " + made(name, type));
    }

    void Writer::effect(LSLExpression* e)
    {
        switch (e->getNodeSubType())
        {
            case NODE_PARENTHESIS_EXPRESSION:
                effect(static_cast<LSLParenthesisExpression*>(e)->getChildExpr());
                return;
            case NODE_BINARY_EXPRESSION:
            {
                auto*             b  = static_cast<LSLBinaryExpression*>(e);
                const LSLOperator op = b->getOperation();
                if (op == OP_ASSIGN || op == OP_ADD_ASSIGN || op == OP_SUB_ASSIGN || op == OP_MUL_ASSIGN || op == OP_DIV_ASSIGN ||
                    op == OP_MOD_ASSIGN)
                {
                    // Said once for each variable it clears.
                    const std::string name = static_cast<LSLLValueExpression*>(b->getLHS())->getIdentifier()->getName();
                    if (mUnbuilt.contains(b))
                    {
                        noteOnce(b, "SluaStringBuild", englishOf("SluaStringBuild"), { name });
                    }
                    if (mFreedFirst.contains(b))
                    {
                        const std::string empty = b->getLHS()->getIType() == LST_LIST ? "[]" : "\"\"";
                        noteOnce(b, "SluaMemoryHack",
                                 "([1] = [2]) + [1] spared LSO's memory, clearing [1] once LSL, going right to left, had read it. Luau goes left to "
                                 "right and needs no such thing: the clearing is left out.",
                                 { name, empty });
                    }
                    assign(static_cast<LSLLValueExpression*>(b->getLHS()), op, b->getRHS());
                    return;
                }
                break;
            }
            case NODE_UNARY_EXPRESSION:
            {
                auto*             u  = static_cast<LSLUnaryExpression*>(e);
                const LSLOperator op = u->getOperation();
                if (op == OP_PRE_INCR || op == OP_PRE_DECR || op == OP_POST_INCR || op == OP_POST_DECR)
                {
                    assign(static_cast<LSLLValueExpression*>(u->getChildExpr()), op, nullptr);
                    return;
                }
                break;
            }
            case NODE_FUNCTION_EXPRESSION:
            {
                // The script's own clock reset where nothing takes the time
                // it read, the reset alone; and read for nothing, the call
                // alone, as a difference is no statement.
                const std::string_view name = static_cast<LSLFunctionExpression*>(e)->getIdentifier()->getName();
                if (mTimeBase && (name == "llGetAndResetTime" || name == "llGetTime"))
                {
                    const bool reset = name == "llGetAndResetTime";
                    sameAs(name, reset ? "timeBase" : "ll.GetTime");
                    line(reset ? "timeBase = ll.GetTime()" : "ll.GetTime()");
                    return;
                }
                // One SLua has nowhere, over a line of its own.
                const ALLSLTraits::Trait* trait = ALLSLTraits::of(static_cast<LSLFunctionExpression*>(e)->getIdentifier()->getName());
                if (trait && (trait->slua & ALLSLTraits::SluaAbsent))
                {
                    expr(e);
                    line("-- " + std::string(trait->name) + ", left out");
                    return;
                }
                line(expr(e).text);
                return;
            }
            case NODE_PRINT_EXPRESSION:
                line(expr(e).text);
                return;
            default:
                break;
        }
        // An expression for nothing but what it does: kept where it does
        // anything, in what Luau takes as a statement.
        if (!ALLSLTraits::sideEffectFree(e))
        {
            line("local _ = " + value(e).text);
        }
    }

    void Writer::stateChange(LSLStateStatement* s, bool last)
    {
        const std::string name = s->getIdentifier()->getName();
        if (mManyStates)
        {
            line("setState(" + luaString(name) + ")");
        }
        else
        {
            note(s, "SluaStateSame", "a change to the state the script is in; a script of one state has no other to go to.");
        }
        // LSL's state change ends the event it is made in: nothing to say
        // where nothing in it follows.
        if (last && handlerTail(s))
        {
            return;
        }
        if (!last)
        {
            line("do return end");
        }
        else if (!mTopLevel)
        {
            line("return");
        }
    }

    void Writer::block(LSLASTNode* s)
    {
        if (isNull(s))
        {
            return;
        }
        if (s->getNodeSubType() == NODE_COMPOUND_STATEMENT)
        {
            // What was after its brace that no line opening it took.
            mBlockStart = mText.size();
            commentsOpening(s);
            const size_t opened = mText.size();
            LSLASTNode*  before = nullptr;
            for (LSLASTNode* child = s->getChild(0); child; child = child->getNext())
            {
                keepApart(before, child, opened);
                statement(child, !child->getNext());
                before = isNull(child) || !child->getLoc() ? before : child;
            }
            lineUp();
            commentsAtEnd(s);
            return;
        }
        mBlockStart = mText.size();
        statement(s, true);
        lineUp();
    }

    void Writer::keepApart(LSLASTNode* before, LSLASTNode* node, size_t opened)
    {
        if (!before || isNull(node) || !node->getLoc() || mText.size() == opened ||
            (mText.size() >= 2 && mText.compare(mText.size() - 2, 2, "\n\n") == 0))
        {
            return;
        }
        const Pos begins = { node->getLoc()->first_line, node->getLoc()->first_column };
        if (const auto found = mCommentsBefore.find(node); found != mCommentsBefore.end())
        {
            for (size_t k : found->second)
            {
                if (mComments[k].at < begins)
                {
                    return;
                }
            }
        }
        if (blankBetween(before->getLoc()->last_line, begins.line))
        {
            mText += "\n";
        }
    }

    void Writer::statement(LSLASTNode* s, bool last)
    {
        if (isNull(s))
        {
            return;
        }
        // Lined up before anything else is written, which notes where in
        // the text it was.
        if (s->getNodeSubType() != NODE_DECLARATION)
        {
            lineUp();
        }
        commentsBefore(s);
        const size_t from = mText.size();
        statementBody(s, last);
        commentsAfter(s, from);
        lined(s, from);
    }

    void Writer::lined(LSLASTNode* node, size_t from)
    {
        // Its one line of code, past what was said over it.
        LSLIdentifier* id    = nullptr;
        LSLASTNode*    init  = nullptr;
        if (node->getNodeType() == NODE_GLOBAL_VARIABLE)
        {
            id   = static_cast<LSLGlobalVariable*>(node)->getIdentifier();
            init = static_cast<LSLGlobalVariable*>(node)->getInitializer();
        }
        else if (node->getNodeSubType() == NODE_DECLARATION)
        {
            id   = static_cast<LSLDeclaration*>(node)->getIdentifier();
            init = static_cast<LSLDeclaration*>(node)->getInitializer();
        }
        size_t at    = std::string::npos;
        int    lines = 0;
        for (size_t i = from; id && i < mText.size();)
        {
            const size_t end   = mText.find('\n', i);
            const size_t first = mText.find_first_not_of(' ', i);
            if (end == std::string::npos)
            {
                break;
            }
            if (first < end && mText.compare(first, 2, "--") != 0)
            {
                at = lines++ == 0 ? i : at;
            }
            i = end + 1;
        }
        const bool fits = id && !isNull(init) && lines == 1 && node->getLoc()->first_line == node->getLoc()->last_line;
        if (!fits || (!mRun.empty() && (mRun.back().node->getParent() != node->getParent() ||
                                        mRun.back().node->getLoc()->last_line + 1 != node->getLoc()->first_line)))
        {
            // What it pads is all before this one's line, which moves on.
            const size_t before = mText.size();
            lineUp();
            at += mText.size() - before;
        }
        if (fits)
        {
            mRun.push_back({ node, id, at });
        }
    }

    void Writer::lineUp()
    {
        std::vector<Lined> run;
        run.swap(mRun);
        if (run.size() < 2)
        {
            return;
        }
        lineStarts();
        // Where the LSL put each one's =, and the comment after it.
        const auto lslLine = [&](S32 line) {
            const size_t from = mLineStarts[static_cast<size_t>(line) - 1];
            return mSource.substr(from, std::min(mSource.find('\n', from), mSource.size()) - from);
        };
        bool                 padded = false;
        std::optional<size_t> equals;
        std::optional<S32>    comment;
        bool                  sameEquals  = true;
        bool                  sameComment = true;
        int                   comments    = 0;
        for (const Lined& one : run)
        {
            const auto*            loc  = one.id->getLoc();
            const std::string_view text = loc->first_line >= 1 && static_cast<size_t>(loc->first_line) <= mLineStarts.size()
                                              ? lslLine(loc->first_line)
                                              : std::string_view();
            size_t                 i    = static_cast<size_t>(std::max(1, loc->first_column) - 1);
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
            {
                ++i;
            }
            const size_t name_end = i;
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
            {
                ++i;
            }
            if (i >= text.size() || text[i] != '=')
            {
                sameEquals = false;
            }
            else
            {
                padded     = padded || i - name_end > 1;
                sameEquals = sameEquals && (!equals || *equals == i);
                equals     = i;
            }
            if (const auto after = mCommentsAfter.find(one.node); after != mCommentsAfter.end() && mTrailed.contains(one.node))
            {
                const S32 column = mComments[after->second.front()].at.column;
                sameComment      = sameComment && (!comment || *comment == column);
                comment          = column;
                ++comments;
            }
        }
        const auto lineEnd = [&](size_t at) { return mText.find('\n', at); };
        // Spaces put in, and where each line after them starts moved on.
        const auto pad = [&](size_t at, size_t count) {
            mText.insert(at, count, ' ');
            if (at < mCounted)
            {
                mCounted += count;
            }
            for (Lined& one : run)
            {
                one.at += one.at > at ? count : 0;
            }
        };
        // Each =, at the column of the one furthest on.
        if (sameEquals && padded)
        {
            size_t most = 0;
            for (const Lined& one : run)
            {
                most = std::max(most, mText.find(" = ", one.at) - one.at);
            }
            for (size_t n = run.size(); n > 0; --n)
            {
                const size_t at = mText.find(" = ", run[n - 1].at);
                if (at < lineEnd(run[n - 1].at))
                {
                    pad(at, most - (at - run[n - 1].at));
                }
            }
        }
        // And each comment after them, past the longest line of code that
        // has one.
        if (sameComment && comments > 1)
        {
            size_t most = 0;
            for (const Lined& one : run)
            {
                if (const auto trailed = mTrailed.find(one.node); trailed != mTrailed.end())
                {
                    most = std::max(most, lineEnd(one.at) - trailed->second - one.at);
                }
            }
            for (size_t n = run.size(); n > 0; --n)
            {
                if (const auto trailed = mTrailed.find(run[n - 1].node); trailed != mTrailed.end())
                {
                    const size_t code = lineEnd(run[n - 1].at) - trailed->second;
                    pad(code, most - (code - run[n - 1].at));
                }
            }
        }
    }

    void Writer::onOneLine(size_t at, S32 lines, size_t spans)
    {
        // Its lines, the last three if c then, s, end; any before them what
        // was said over it.
        std::vector<size_t> starts;
        for (size_t i = at; i < mText.size(); i = mText.find('\n', i) + 1)
        {
            starts.push_back(i);
            if (mText.find('\n', i) == std::string::npos)
            {
                return;
            }
        }
        if (starts.size() < 3)
        {
            return;
        }
        const auto row = [&](size_t n) {
            const size_t begin = starts[n];
            const size_t first = std::min(mText.find_first_not_of(' ', begin), mText.find('\n', begin));
            return std::string_view(mText).substr(first, mText.find('\n', begin) - first);
        };
        const size_t head = starts.size() - 3;
        for (size_t n = 0; n < head; ++n)
        {
            if (row(n).substr(0, 2) != "--")
            {
                return;
            }
        }
        const std::string_view body = row(head + 1);
        if (row(head).substr(0, 3) != "if " || body.empty() || body.substr(0, 2) == "--" || row(head + 2) != "end")
        {
            return;
        }
        const std::string joined = mText.substr(starts[head], mText.find('\n', starts[head]) - starts[head]) + " " + std::string(body) + " end\n";
        mText.replace(starts[head], std::string::npos, joined);
        // What is anchored past its first line, on it.
        const S32 first = lines + static_cast<S32>(head);
        for (size_t n = mSpans.size(); n > spans; --n)
        {
            if (mSpans[n - 1].sluaFirst > first)
            {
                mSpans.erase(mSpans.begin() + static_cast<std::ptrdiff_t>(n - 1));
            }
        }
        mCounted      = starts[head];
        mCountedLines = first;
    }

    void Writer::statementBody(LSLASTNode* s, bool last)
    {
        if (mBuilds.contains(s))
        {
            buildingLoop(s, last);
            return;
        }
        switch (s->getNodeSubType())
        {
            case NODE_NOP_STATEMENT: return;
            case NODE_COMPOUND_STATEMENT:
                line("do");
                ++mDepth;
                block(s);
                --mDepth;
                line("end");
                return;
            case NODE_EXPRESSION_STATEMENT:
            {
                LSLExpression* e     = static_cast<LSLExpressionStatement*>(s)->getExpr();
                const Steps    steps = hoistSteps(e, true, true);
                writeSteps(steps.before);
                effect(e);
                writeSteps(steps.after);
                return;
            }
            case NODE_DECLARATION:
            {
                auto*             d    = static_cast<LSLDeclaration*>(s);
                LSLIdentifier*    id   = d->getIdentifier();
                const LSLIType    type = varType(id->getSymbol(), id->getIType());
                LSLExpression*    init = d->getInitializer();
                const std::string name = nameOf(id);
                // A counter that only numeric fors use, each with its own.
                if (mLoopOnly.contains(id->getSymbol()) && (isNull(init) || init->getNodeSubType() == NODE_CONSTANT_EXPRESSION))
                {
                    return;
                }
                // A string declared where the loop building it joins it.
                if (mJoinedDeclarations.contains(d))
                {
                    return;
                }
                const Steps steps = isNull(init) ? Steps() : hoistSteps(init, true);
                writeSteps(steps.before);
                if (boolean(id->getSymbol()))
                {
                    line("local " + name + (mOptions.types ? ": boolean" : "") + " = " + (isNull(init) ? std::string("false") : truthOf(init)));
                }
                else
                {
                    line("local " + name + typed(type) + " = " + (isNull(init) ? defaultOf(type) : coerced(init, type).text));
                }
                writeSteps(steps.after);
                return;
            }
            case NODE_RETURN_STATEMENT:
            {
                LSLExpression*    e    = static_cast<LSLReturnStatement*>(s)->getExpr();
                writeSteps(isNull(e) ? std::vector<LSLExpression*>() : hoistSteps(e, false).before);
                // As the function's type: a string function's NULL_KEY is
                // LSL's string.
                const std::string given = isNull(e)             ? std::string()
                                          : boolean(mFunction) ? truthOf(e)
                                          : mFunction          ? coerced(e, mFunction->getIType()).text
                                                               : value(e).text;
                const std::string said  = isNull(e) ? "return" : "return " + given;
                line(last ? said : "do " + said + " end");
                return;
            }
            case NODE_IF_STATEMENT:
            {
                auto* i = static_cast<LSLIfStatement*>(s);
                // A boolean given TRUE or FALSE by the check: the check.
                LSLIdentifier* chosen    = nullptr;
                bool           when_true = true;
                if (booleanChoice(i, chosen, when_true))
                {
                    // What was after either branch's brace, over the line
                    // that stands for both.
                    commentsOpening(i->getTrueBranch());
                    commentsOpening(i->getFalseBranch());
                    const Expr check = condition(i->getCheckExpr());
                    line(nameOf(chosen) + " = " + (when_true ? check.text : "not " + bracketed(check, UNARY)));
                    return;
                }
                // On one line where the LSL wrote it on one, with no else,
                // and no line's comment after its first line, which would
                // take the rest of the line with it.
                const bool   one_line = s->getLoc()->first_line == s->getLoc()->last_line && isNull(i->getFalseBranch());
                const S32    lines_at = one_line ? linesWritten() : 0;
                const size_t text_at  = mText.size();
                const size_t spans    = mSpans.size();
                line("if " + condition(i->getCheckExpr()).text + " then");
                const bool   trailed  = trail(i->getTrueBranch());
                for (;;)
                {
                    ++mDepth;
                    block(i->getTrueBranch());
                    --mDepth;
                    LSLASTNode* otherwise = i->getFalseBranch();
                    if (isNull(otherwise))
                    {
                        break;
                    }
                    if (otherwise->getNodeSubType() == NODE_IF_STATEMENT)
                    {
                        i = static_cast<LSLIfStatement*>(otherwise);
                        commentsBefore(i);
                        line("elseif " + condition(i->getCheckExpr()).text + " then");
                        trail(i->getTrueBranch());
                        continue;
                    }
                    line("else");
                    trail(otherwise);
                    ++mDepth;
                    block(otherwise);
                    --mDepth;
                    break;
                }
                line("end");
                if (one_line && !trailed)
                {
                    onOneLine(text_at, lines_at, spans);
                }
                return;
            }
            case NODE_WHILE_STATEMENT:
            {
                auto* w = static_cast<LSLWhileStatement*>(s);
                line("while " + condition(w->getCheckExpr()).text + " do");
                trail(w->getBody());
                mLoops.push_back(nullptr);
                ++mDepth;
                block(w->getBody());
                --mDepth;
                mLoops.pop_back();
                line("end");
                return;
            }
            case NODE_DO_STATEMENT:
            {
                auto* d = static_cast<LSLDoStatement*>(s);
                line("repeat");
                trail(d->getBody());
                mLoops.push_back(nullptr);
                ++mDepth;
                block(d->getBody());
                --mDepth;
                mLoops.pop_back();
                line("until " + bracketed(condition(d->getCheckExpr()), UNARY).insert(0, "not "));
                return;
            }
            case NODE_FOR_STATEMENT:
            {
                // Luau's numeric for where it counts as LSL's did.
                auto* f = static_cast<LSLForStatement*>(s);
                if (const auto numeric = mNumeric.find(f); numeric != mNumeric.end())
                {
                    numericFor(f, numeric->second);
                    return;
                }
                // Else as LSL runs one: what starts it, then while the check
                // holds, the body and then the steps.
                for (LSLASTNode* init = f->getInitExprs() ? f->getInitExprs()->getChild(0) : nullptr; init; init = init->getNext())
                {
                    effect(static_cast<LSLExpression*>(init));
                }
                LSLExpression* check = f->getCheckExpr();
                line("while " + (isNull(check) ? std::string("true") : condition(check).text) + " do");
                trail(f->getBody());
                mLoops.push_back(f);
                ++mDepth;
                block(f->getBody());
                for (LSLASTNode* step = f->getIncrExprs() ? f->getIncrExprs()->getChild(0) : nullptr; step; step = step->getNext())
                {
                    effect(static_cast<LSLExpression*>(step));
                }
                --mDepth;
                mLoops.pop_back();
                line("end");
                return;
            }
            case NODE_JUMP_STATEMENT:
            {
                auto* j = static_cast<LSLJumpStatement*>(s);
                if (j->getIsBreakLike() && !mLoops.empty())
                {
                    line("break");
                    return;
                }
                if (j->getIsContinueLike() && !mLoops.empty())
                {
                    // A for's steps run before its next turn, as they would
                    // at the end of its body.
                    if (LSLForStatement* f = mLoops.back() && mLoops.back()->getNodeSubType() == NODE_FOR_STATEMENT
                                                 ? static_cast<LSLForStatement*>(mLoops.back())
                                                 : nullptr)
                    {
                        for (LSLASTNode* step = f->getIncrExprs() ? f->getIncrExprs()->getChild(0) : nullptr; step; step = step->getNext())
                        {
                            effect(static_cast<LSLExpression*>(step));
                        }
                    }
                    line("continue");
                    return;
                }
                note(s, "SluaJump", "jump [1]: SLua has no goto, and this jump is neither out of a loop nor to its next turn. Rewrite it.",
                     { j->getIdentifier()->getName() });
                line("-- jump " + std::string(j->getIdentifier()->getName()));
                return;
            }
            case NODE_LABEL:
            {
                auto* l = static_cast<LSLLabel*>(s);
                if (mUnstructured.contains(l->getIdentifier()->getSymbol()))
                {
                    line("-- @" + std::string(l->getIdentifier()->getName()));
                }
                return;
            }
            case NODE_STATE_STATEMENT: stateChange(static_cast<LSLStateStatement*>(s), last); return;
            default:
                return;
        }
    }

    // --- loops as Luau counts ---------------------------------------------------------

    namespace
    {
        LSLExpression* unwrapped(LSLExpression* e)
        {
            return unbracketed(e);
        }

        // The variable an lvalue names, whole: none for a part of one.
        LSLSymbol* wholeVariable(LSLExpression* e)
        {
            e = unwrapped(e);
            if (!e || e->getNodeSubType() != NODE_LVALUE_EXPRESSION || !isNull(static_cast<LSLLValueExpression*>(e)->getMember()))
            {
                return nullptr;
            }
            return static_cast<LSLLValueExpression*>(e)->getIdentifier()->getSymbol();
        }

        // The variable an expression sets, where it sets one: an
        // assignment of any kind, or a step.
        LSLSymbol* setBy(LSLASTNode* node)
        {
            if (node->getNodeSubType() == NODE_BINARY_EXPRESSION)
            {
                const LSLOperator op = static_cast<LSLExpression*>(node)->getOperation();
                if (op == OP_ASSIGN || op == OP_ADD_ASSIGN || op == OP_SUB_ASSIGN || op == OP_MUL_ASSIGN || op == OP_DIV_ASSIGN || op == OP_MOD_ASSIGN)
                {
                    LSLExpression* lhs = static_cast<LSLBinaryExpression*>(node)->getLHS();
                    return lhs && lhs->getNodeSubType() == NODE_LVALUE_EXPRESSION ? static_cast<LSLLValueExpression*>(lhs)->getIdentifier()->getSymbol()
                                                                                   : nullptr;
                }
            }
            if (node->getNodeSubType() == NODE_UNARY_EXPRESSION)
            {
                const LSLOperator op = static_cast<LSLExpression*>(node)->getOperation();
                if (op == OP_PRE_INCR || op == OP_PRE_DECR || op == OP_POST_INCR || op == OP_POST_DECR)
                {
                    LSLExpression* child = static_cast<LSLUnaryExpression*>(node)->getChildExpr();
                    return child && child->getNodeSubType() == NODE_LVALUE_EXPRESSION
                               ? static_cast<LSLLValueExpression*>(child)->getIdentifier()->getSymbol()
                               : nullptr;
                }
            }
            return nullptr;
        }
    }

    std::optional<Writer::Counting> Writer::counting(LSLForStatement* f) const
    {
        // One start, i = from; one check, i against a limit; one step, by
        // a constant, the way the check is going.
        LSLASTNode* init = f->getInitExprs() ? f->getInitExprs()->getChild(0) : nullptr;
        LSLASTNode* incr = f->getIncrExprs() ? f->getIncrExprs()->getChild(0) : nullptr;
        auto*       check = static_cast<LSLExpression*>(f->getCheckExpr());
        if (!init || init->getNext() || !incr || incr->getNext() || isNull(check) || init->getNodeSubType() != NODE_BINARY_EXPRESSION ||
            static_cast<LSLExpression*>(init)->getOperation() != OP_ASSIGN)
        {
            return std::nullopt;
        }
        Counting c;
        auto*    start = static_cast<LSLBinaryExpression*>(init);
        c.var          = wholeVariable(start->getLHS());
        if (!c.var || c.var->getIType() != LST_INTEGER || c.var->getSubType() != SYM_LOCAL)
        {
            return std::nullopt;
        }
        c.id   = static_cast<LSLLValueExpression*>(unwrapped(start->getLHS()))->getIdentifier();
        c.from = start->getRHS();
        check  = unwrapped(check);
        if (check->getNodeSubType() == NODE_BOOL_CONVERSION_EXPRESSION)
        {
            check = unwrapped(static_cast<LSLBoolConversionExpression*>(check)->getChildExpr());
        }
        if (!check || check->getNodeSubType() != NODE_BINARY_EXPRESSION)
        {
            return std::nullopt;
        }
        auto* test = static_cast<LSLBinaryExpression*>(check);
        c.check    = test->getOperation();
        if ((c.check != OP_LESS && c.check != OP_LEQ && c.check != OP_GREATER && c.check != OP_GEQ) || wholeVariable(test->getLHS()) != c.var)
        {
            return std::nullopt;
        }
        c.limit = test->getRHS();
        // The step: ++ and -- either side, or += and -= a whole number.
        const LSLOperator op = static_cast<LSLExpression*>(incr)->getOperation();
        if (setBy(incr) != c.var)
        {
            return std::nullopt;
        }
        int by = 0;
        if (op == OP_PRE_INCR || op == OP_POST_INCR)
        {
            c.step = 1;
        }
        else if (op == OP_PRE_DECR || op == OP_POST_DECR)
        {
            c.step = -1;
        }
        else if ((op == OP_ADD_ASSIGN || op == OP_SUB_ASSIGN) && steadyWhole(static_cast<LSLBinaryExpression*>(incr)->getRHS(), by) && by > 0)
        {
            c.step = op == OP_ADD_ASSIGN ? by : -by;
            LSLExpression* step = unwrapped(static_cast<LSLBinaryExpression*>(incr)->getRHS());
            c.by                = step->getNodeSubType() == NODE_LVALUE_EXPRESSION ? step : nullptr;
        }
        const bool up = c.check == OP_LESS || c.check == OP_LEQ;
        if (c.step == 0 || (c.step > 0) != up)
        {
            return std::nullopt;
        }
        // Nothing in the loop sets the counter but its step, and the limit
        // and where it starts are read once, as Luau reads them.
        bool setInBody = false;
        walk(f->getBody(), [&](LSLASTNode* node) { setInBody = setInBody || setBy(node) == c.var; });
        if (setInBody || !(steadyIn(c.limit, f) || lengthKept(c, f)))
        {
            return std::nullopt;
        }
        return c;
    }

    bool Writer::lengthKept(const Counting& c, LSLForStatement* f) const
    {
        LSLExpression* limit = unwrapped(c.limit);
        if (!limit || limit->getNodeSubType() != NODE_FUNCTION_EXPRESSION ||
            std::string_view(static_cast<LSLFunctionExpression*>(limit)->getIdentifier()->getName()) != "llGetListLength")
        {
            return false;
        }
        LSLSymbol* list = wholeVariable(argumentAt(static_cast<LSLFunctionExpression*>(limit), 0));
        if (!list)
        {
            return false;
        }
        // Each time the loop sets l: l = llListReplaceList(l, [x], i, i), x
        // changing nothing of l's.
        bool kept = true;
        walk(f, [&](LSLASTNode* node) {
            if (!kept || setBy(node) != list)
            {
                return;
            }
            LSLExpression* rhs = node->getNodeSubType() == NODE_BINARY_EXPRESSION && static_cast<LSLExpression*>(node)->getOperation() == OP_ASSIGN
                                     ? unwrapped(static_cast<LSLBinaryExpression*>(node)->getRHS())
                                     : nullptr;
            auto*          call = rhs && rhs->getNodeSubType() == NODE_FUNCTION_EXPRESSION ? static_cast<LSLFunctionExpression*>(rhs) : nullptr;
            kept = call && std::string_view(call->getIdentifier()->getName()) == "llListReplaceList" &&
                   wholeVariable(argumentAt(call, 0)) == list && soleItem(argumentAt(call, 1)) && !mEffects.of(argumentAt(call, 1)).writes(list) &&
                   wholeVariable(argumentAt(call, 2)) == c.var && wholeVariable(argumentAt(call, 3)) == c.var;
        });
        return kept && steadyIn(c.limit, f, list);
    }

    bool Writer::steadyWhole(LSLExpression* e, int& v) const
    {
        if (wholeNumber(e, v))
        {
            return true;
        }
        e = unwrapped(e);
        if (!e || e->getNodeSubType() != NODE_LVALUE_EXPRESSION || !isNull(static_cast<LSLLValueExpression*>(e)->getMember()))
        {
            return false;
        }
        LSLSymbol* symbol = static_cast<LSLLValueExpression*>(e)->getIdentifier()->getSymbol();
        if (const auto found = mSteadyWhole.find(symbol); found != mSteadyWhole.end())
        {
            v = found->second;
            return true;
        }
        LSLConstant* value = symbol && symbol->getSubType() == SYM_BUILTIN ? symbol->getConstantValue() : nullptr;
        if (value && value->getNodeSubType() == NODE_INTEGER_CONSTANT)
        {
            v = static_cast<LSLIntegerConstant*>(value)->getValue();
            return true;
        }
        return false;
    }

    bool Writer::steadyIn(LSLExpression* e, LSLASTNode* loop, LSLSymbol* kept) const
    {
        // What the loop sets, and whether it calls anything of the script's
        // own, which could set a global.
        boost::unordered_flat_set<LSLSymbol*> set;
        bool                                  calls = false;
        walk(loop, [&](LSLASTNode* node) {
            if (LSLSymbol* var = setBy(node))
            {
                set.insert(var);
            }
            if (node->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
            {
                LSLSymbol* fn = static_cast<LSLFunctionExpression*>(node)->getIdentifier()->getSymbol();
                calls         = calls || !fn || fn->getSubType() != SYM_BUILTIN;
            }
        });
        bool steady = true;
        walk(e, [&](LSLASTNode* node) {
            switch (node->getNodeSubType())
            {
                case NODE_LVALUE_EXPRESSION:
                {
                    LSLSymbol* var = static_cast<LSLLValueExpression*>(node)->getIdentifier()->getSymbol();
                    steady = steady && var && (var->getSubType() == SYM_BUILTIN ||
                                               ((var == kept || !set.contains(var)) && (var->getSubType() != SYM_GLOBAL || !calls)));
                    break;
                }
                case NODE_FUNCTION_EXPRESSION:
                    steady = steady && ALLSLTraits::pure(static_cast<LSLFunctionExpression*>(node)->getIdentifier()->getName());
                    break;
                case NODE_BINARY_EXPRESSION:
                case NODE_UNARY_EXPRESSION:
                    steady = steady && !setBy(node);
                    break;
                default:
                    break;
            }
        });
        return steady;
    }

    void Writer::prepareBody(LSLASTNode* body)
    {
        mNumeric.clear();
        mLoopOnly.clear();
        // The fors of the shape, by their counters.
        boost::unordered_flat_map<LSLSymbol*, std::vector<LSLForStatement*>> byCounter;
        walk(body, [&](LSLASTNode* node) {
            if (node->getNodeSubType() == NODE_FOR_STATEMENT)
            {
                if (std::optional<Counting> c = counting(static_cast<LSLForStatement*>(node)))
                {
                    mNumeric.emplace(static_cast<LSLForStatement*>(node), *c);
                    byCounter[c->var].push_back(static_cast<LSLForStatement*>(node));
                }
            }
        });
        // A counter read or set anywhere but in its own numeric fors keeps
        // them LSL's: Luau's is a new local, gone after the loop.
        for (auto& [var, loops] : byCounter)
        {
            bool elsewhere = false;
            const auto within = [&loops](LSLASTNode* node) {
                for (LSLASTNode* up = node; up; up = up->getParent())
                {
                    if (std::find(loops.begin(), loops.end(), up) != loops.end())
                    {
                        return true;
                    }
                }
                return false;
            };
            walk(body, [&](LSLASTNode* node) {
                if (node->getNodeSubType() == NODE_LVALUE_EXPRESSION && static_cast<LSLLValueExpression*>(node)->getIdentifier()->getSymbol() == var &&
                    !within(node))
                {
                    elsewhere = true;
                }
            });
            if (elsewhere)
            {
                for (LSLForStatement* f : loops)
                {
                    mNumeric.erase(f);
                }
                continue;
            }
            mLoopOnly.insert(var);
        }
    }

    void Writer::numericFor(LSLForStatement* f, const Counting& c)
    {
        // i < b counts to b - 1 in whole numbers, i > b to b + 1.
        int               limit = 0;
        std::string       to;
        const bool        strict = c.check == OP_LESS || c.check == OP_GREATER;
        const int         shift  = c.check == OP_LESS ? -1 : c.check == OP_GREATER ? 1 : 0;
        if (wholeNumber(c.limit, limit))
        {
            to = std::to_string(limit + shift);
        }
        else
        {
            const Expr bound = value(c.limit);
            to = strict ? bracketed(bound, ADD) + (shift < 0 ? " - 1" : " + 1") : bound.text;
        }
        const std::string step = c.by        ? ", " + std::string(c.step < 0 ? "-" : "") + value(c.by).text
                                 : c.step == 1 ? std::string()
                                               : ", " + std::to_string(c.step);
        // A counter never below nought -- up from a whole number that is
        // not, or down to one -- is an index ll may be given moved on by
        // one; and where the body reads it as nothing else, it counts from 1.
        int        from       = 0;
        int        bound      = 0;
        const bool up         = c.step > 0 && wholeNumber(c.from, from) && from >= 0;
        const bool down       = c.step < 0 && wholeNumber(c.limit, bound) &&
                          ((c.check == OP_GEQ && bound >= 0) || (c.check == OP_GREATER && bound >= -1));
        const bool from_one   = up && onlyIndexes(c.var, f->getBody());
        std::string start     = value(c.from).text;
        if (from_one)
        {
            start = std::to_string(from + 1);
            to    = wholeNumber(c.limit, limit) ? std::to_string(limit + shift + 1)
                    : shift < 0 ? value(c.limit).text
                                : bracketed(value(c.limit), ADD) + (shift > 0 ? " + 2" : " + 1");
        }
        // Up by one, below a list's length: within the list.
        LSLExpression* limit_call = unwrapped(c.limit);
        LSLExpression* listed     = nullptr;
        if (up && c.step == 1 && c.check == OP_LESS && limit_call->getNodeSubType() == NODE_FUNCTION_EXPRESSION &&
            std::string_view(static_cast<LSLFunctionExpression*>(limit_call)->getIdentifier()->getName()) == "llGetListLength")
        {
            listed = argumentAt(static_cast<LSLFunctionExpression*>(limit_call), 0);
        }
        LSLSymbol* within = listed ? wholeVariable(listed) : nullptr;
        // From its first item, the counter read as nothing but the list's
        // items: Luau's walk over them, each by name.
        const bool each = from_one && within && from == 0 && onlyItemsOf(c.var, within, f->getBody());
        if (each)
        {
            const size_t depth = mEach.size();
            if (depth == mItemNames.size())
            {
                mItemNames.push_back(freshName("item"));
            }
            // The index unread, as Luau writes one, unless the script has
            // a name that would be hidden by it.
            if (mUnread.empty())
            {
                takeNames();
                mUnread = mTaken.contains("_") ? freshName("_") : std::string("_");
            }
            line("for " + mUnread + ", " + mItemNames[depth] + " in " + lvalue(static_cast<LSLLValueExpression*>(unwrapped(listed))).text + " do");
            mEach[c.var] = mItemNames[depth];
        }
        else
        {
            line("for " + nameOf(c.id) + " = " + start + ", " + to + step + " do");
        }
        if (up || down)
        {
            mNonNegative.insert(c.var);
        }
        if (within)
        {
            mWithin[c.var] = within;
        }
        trail(f->getBody());
        if (from_one)
        {
            mFromOne.insert(c.var);
        }
        // A jump to its next turn is continue, its step Luau's own.
        mLoops.push_back(nullptr);
        ++mDepth;
        block(f->getBody());
        --mDepth;
        mLoops.pop_back();
        mNonNegative.erase(c.var);
        mFromOne.erase(c.var);
        mWithin.erase(c.var);
        mEach.erase(c.var);
        line("end");
    }

    bool Writer::onlyIndexes(LSLSymbol* var, LSLASTNode* body)
    {
        if (!mOptions.sluaCalls)
        {
            return false;
        }
        bool any = false;
        bool only = true;
        walk(body, [&](LSLASTNode* node) {
            if (!only || node->getNodeSubType() != NODE_LVALUE_EXPRESSION ||
                static_cast<LSLLValueExpression*>(node)->getIdentifier()->getSymbol() != var)
            {
                return;
            }
            any = true;
            // Up through a whole number added, to the argument list of a
            // library call that ll is given, at an index's place.
            LSLASTNode* arg = node;
            while (arg->getParent() && arg->getParent()->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
            {
                arg = arg->getParent();
            }
            int v = 0;
            if (arg->getParent() && arg->getParent()->getNodeSubType() == NODE_BINARY_EXPRESSION &&
                static_cast<LSLExpression*>(arg->getParent())->getOperation() == OP_PLUS)
            {
                auto* b = static_cast<LSLBinaryExpression*>(arg->getParent());
                if (!wholeNumber(arg == b->getLHS() ? b->getRHS() : b->getLHS(), v) || v < 0)
                {
                    only = false;
                    return;
                }
                arg = arg->getParent();
            }
            LSLASTNode* list = arg->getParent();
            LSLASTNode* call = list ? list->getParent() : nullptr;
            if (!call || list->getNodeType() != NODE_AST_NODE_LIST || call->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
            {
                only = false;
                return;
            }
            LSLIdentifier*            id      = static_cast<LSLFunctionExpression*>(call)->getIdentifier();
            const bool                builtin = id->getSymbol() && id->getSymbol()->getSubType() == SYM_BUILTIN;
            const ALLSLTraits::Trait* trait   = builtin ? ALLSLTraits::of(id->getName()) : nullptr;
            const U8 not_ll = ALLSLTraits::SluaRemoved | ALLSLTraits::SluaBoolList | ALLSLTraits::SluaDeprecated | ALLSLTraits::SluaAbsent;
            const std::string_view name = id->getName();
            // Or the place in a list its items are read from as they are.
            const bool item = builtin && arg->getParentSlot() == 1 && listItemFits(static_cast<LSLFunctionExpression*>(call));
            only = item || (trait && (trait->slua & ALLSLTraits::SluaIndexArgs) && !(trait->slua & not_ll) &&
                            (trait->sluaIndexArgs & (1 << arg->getParentSlot())) && name.rfind("llDetected", 0) != 0);
        });
        return any && only;
    }

    bool Writer::onlyItemsOf(LSLSymbol* var, LSLSymbol* list, LSLASTNode* body)
    {
        bool any  = false;
        bool only = true;
        walk(body, [&](LSLASTNode* node) {
            if (!only || node->getNodeSubType() != NODE_LVALUE_EXPRESSION ||
                static_cast<LSLLValueExpression*>(node)->getIdentifier()->getSymbol() != var)
            {
                return;
            }
            any = true;
            // As it is, the index of a library call reading an item of the
            // list.
            LSLASTNode* arg = node;
            while (arg->getParent() && arg->getParent()->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
            {
                arg = arg->getParent();
            }
            LSLASTNode* args = arg->getParent();
            LSLASTNode* call = args ? args->getParent() : nullptr;
            if (!call || args->getNodeType() != NODE_AST_NODE_LIST || call->getNodeSubType() != NODE_FUNCTION_EXPRESSION ||
                arg->getParentSlot() != 1)
            {
                only = false;
                return;
            }
            auto*      read    = static_cast<LSLFunctionExpression*>(call);
            LSLSymbol* fn      = read->getIdentifier()->getSymbol();
            only               = fn && fn->getSubType() == SYM_BUILTIN && wholeVariable(argumentAt(read, 0)) == list && listItemFits(read);
        });
        return any && only;
    }

    // --- truth values ----------------------------------------------------------------

    namespace
    {
        // TRUE, FALSE, or 1 or 0 written out, and which.
        bool truthConstant(LSLExpression* e, bool& truth)
        {
            int v = 0;
            if (wholeNumber(e, v))
            {
                truth = v != 0;
                return v == 0 || v == 1;
            }
            e = unwrapped(e);
            if (e && e->getNodeSubType() == NODE_LVALUE_EXPRESSION)
            {
                LSLIdentifier* id = static_cast<LSLLValueExpression*>(e)->getIdentifier();
                if (id->getSymbol() && id->getSymbol()->getSubType() == SYM_BUILTIN &&
                    (std::string_view(id->getName()) == "TRUE" || std::string_view(id->getName()) == "FALSE"))
                {
                    truth = std::string_view(id->getName()) == "TRUE";
                    return true;
                }
            }
            return false;
        }

        // What a read reads: a variable, whole, or a function of the
        // script's own that is called.
        LSLSymbol* readOf(LSLASTNode* node)
        {
            if (node->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
            {
                LSLSymbol* symbol = static_cast<LSLFunctionExpression*>(node)->getIdentifier()->getSymbol();
                return symbol && symbol->getSubType() != SYM_BUILTIN ? symbol : nullptr;
            }
            return wholeVariable(static_cast<LSLExpression*>(node));
        }

        // Whether what an expression gives is used: not where it stands as
        // a statement, or as a for's start or step.
        bool valueUsed(LSLASTNode* node)
        {
            LSLASTNode* parent = node->getParent();
            while (parent && parent->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION)
            {
                parent = parent->getParent();
            }
            if (!parent || parent->getNodeSubType() == NODE_EXPRESSION_STATEMENT)
            {
                return false;
            }
            return !(parent->getNodeType() == NODE_AST_NODE_LIST && parent->getParent() &&
                     parent->getParent()->getNodeSubType() == NODE_FOR_STATEMENT);
        }

        // Whether an assignment is what sets its variable, rather than a
        // read of it.
        bool assignedBy(LSLASTNode* read)
        {
            LSLASTNode* parent = read->getParent();
            return parent && parent->getNodeSubType() == NODE_BINARY_EXPRESSION && read->getParentSlot() == 0 &&
                   static_cast<LSLExpression*>(parent)->getOperation() == OP_ASSIGN;
        }

        // The function of the script's own a node stands in, if any.
        LSLSymbol* enclosingFunction(LSLASTNode* node)
        {
            for (; node; node = node->getParent())
            {
                if (node->getNodeType() == NODE_GLOBAL_FUNCTION)
                {
                    return static_cast<LSLGlobalFunction*>(node)->getSymbol();
                }
            }
            return nullptr;
        }
    }

    bool Writer::zeroOrOne(LSLExpression* e) const
    {
        bool truth = false;
        if (truthConstant(e, truth))
        {
            return true;
        }
        e = unwrapped(e);
        if (!e)
        {
            return false;
        }
        switch (e->getNodeSubType())
        {
            case NODE_LVALUE_EXPRESSION:
            case NODE_FUNCTION_EXPRESSION:
                if (LSLSymbol* read = readOf(e))
                {
                    return mZeroOne.contains(read);
                }
                if (e->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
                {
                    const ALLSLTraits::Trait* trait = ALLSLTraits::of(static_cast<LSLFunctionExpression*>(e)->getIdentifier()->getName());
                    return trait && (trait->slua & ALLSLTraits::SluaBool);
                }
                return false;
            case NODE_UNARY_EXPRESSION: return e->getOperation() == OP_BOOLEAN_NOT;
            case NODE_BINARY_EXPRESSION:
            {
                const LSLOperator op = e->getOperation();
                if (op == OP_NEQ)
                {
                    // != of two lists says how much longer the left one is.
                    auto* b = static_cast<LSLBinaryExpression*>(e);
                    return b->getLHS()->getIType() != LST_LIST || b->getRHS()->getIType() != LST_LIST;
                }
                return op == OP_EQ || op == OP_LESS || op == OP_LEQ || op == OP_GREATER || op == OP_GEQ || op == OP_BOOLEAN_AND ||
                       op == OP_BOOLEAN_OR;
            }
            default: return false;
        }
    }

    bool Writer::readAsTruth(LSLASTNode* read) const
    {
        // Up through brackets and the tree's own conversions to a truth.
        LSLASTNode* node   = read;
        LSLASTNode* parent = node->getParent();
        while (parent && (parent->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION || parent->getNodeSubType() == NODE_BOOL_CONVERSION_EXPRESSION))
        {
            if (parent->getNodeSubType() == NODE_BOOL_CONVERSION_EXPRESSION)
            {
                return true;
            }
            node   = parent;
            parent = parent->getParent();
        }
        if (!parent)
        {
            return false;
        }
        const int slot = node->getParentSlot();
        switch (parent->getNodeSubType())
        {
            case NODE_IF_STATEMENT:
            case NODE_WHILE_STATEMENT: return slot == 0;
            case NODE_DO_STATEMENT: return slot == 1;
            case NODE_FOR_STATEMENT: return slot == 1;
            case NODE_RETURN_STATEMENT: return boolean(enclosingFunction(parent));
            case NODE_UNARY_EXPRESSION: return static_cast<LSLExpression*>(parent)->getOperation() == OP_BOOLEAN_NOT;
            case NODE_BINARY_EXPRESSION:
            {
                const LSLOperator op = static_cast<LSLExpression*>(parent)->getOperation();
                auto*             b  = static_cast<LSLBinaryExpression*>(parent);
                if (op == OP_BOOLEAN_AND || op == OP_BOOLEAN_OR)
                {
                    return true;
                }
                // Asked whether it is TRUE or FALSE, where it only ever is
                // one or the other.
                bool truth = false;
                if ((op == OP_EQ || op == OP_NEQ) && truthConstant(slot == 0 ? b->getRHS() : b->getLHS(), truth))
                {
                    return mZeroOne.contains(readOf(read));
                }
                // Copied, whole, into another truth value.
                return op == OP_ASSIGN && slot == 1 && boolean(wholeVariable(b->getLHS()));
            }
            case NODE_DECLARATION: return slot == 1 && boolean(static_cast<LSLDeclaration*>(parent)->getIdentifier()->getSymbol());
            default:
                if (parent->getNodeType() == NODE_GLOBAL_VARIABLE)
                {
                    return slot == 1 && boolean(static_cast<LSLGlobalVariable*>(parent)->getIdentifier()->getSymbol());
                }
                return false;
        }
    }

    void Writer::findBooleans()
    {
        // Every integer the script declares and every function of its own
        // that answers one, then each let go of that is stepped, added to or
        // read as a number, until none is.
        std::vector<LSLASTNode*>                           reads;
        std::vector<std::pair<LSLSymbol*, LSLExpression*>> sets;
        const auto candidate = [&](LSLIdentifier* id, LSLExpression* init) {
            if (id->getIType() == LST_INTEGER && id->getSymbol())
            {
                mBooleans.insert(id->getSymbol());
                if (!isNull(init))
                {
                    sets.emplace_back(id->getSymbol(), init);
                }
            }
        };
        walk(mScript, [&](LSLASTNode* node) {
            if (node->getNodeType() == NODE_GLOBAL_VARIABLE)
            {
                auto* global = static_cast<LSLGlobalVariable*>(node);
                candidate(global->getIdentifier(), global->getInitializer());
            }
            else if (node->getNodeType() == NODE_GLOBAL_FUNCTION)
            {
                auto* f = static_cast<LSLGlobalFunction*>(node);
                if (f->getIdentifier()->getIType() == LST_INTEGER && f->getSymbol())
                {
                    mBooleans.insert(f->getSymbol());
                }
                // Its integer parameters, each set by what its calls give
                // it. An event's are the grid's, typed as numbers there,
                // and stay numbers.
                for (LSLASTNode* param = f->getArguments() ? f->getArguments()->getChild(0) : nullptr; !isNull(param); param = param->getNext())
                {
                    candidate(static_cast<LSLIdentifier*>(param), nullptr);
                }
            }
            else if (node->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
            {
                // A call of the script's own function: each argument what
                // its parameter is set to.
                auto*         call   = static_cast<LSLFunctionExpression*>(node);
                LSLSymbol*    symbol = call->getIdentifier()->getSymbol();
                LSLParamList* params = symbol && symbol->getSubType() != SYM_BUILTIN ? symbol->getFunctionDecl() : nullptr;
                LSLASTNode*   param  = params ? params->getChild(0) : nullptr;
                for (LSLASTNode* arg = isNull(call->getArguments()) ? nullptr : call->getArguments()->getChild(0); arg && !isNull(param);
                     arg = arg->getNext(), param = param->getNext())
                {
                    if (LSLSymbol* set = static_cast<LSLIdentifier*>(param)->getSymbol(); set && param->getIType() == LST_INTEGER)
                    {
                        sets.emplace_back(set, static_cast<LSLExpression*>(arg));
                    }
                }
            }
            else if (node->getNodeSubType() == NODE_DECLARATION)
            {
                auto* declaration = static_cast<LSLDeclaration*>(node);
                candidate(declaration->getIdentifier(), declaration->getInitializer());
            }
            else if (node->getNodeSubType() == NODE_RETURN_STATEMENT)
            {
                LSLExpression* e = static_cast<LSLReturnStatement*>(node)->getExpr();
                if (LSLSymbol* f = enclosingFunction(node); f && !isNull(e))
                {
                    sets.emplace_back(f, e);
                }
            }
            if ((node->getNodeSubType() == NODE_LVALUE_EXPRESSION || node->getNodeSubType() == NODE_FUNCTION_EXPRESSION) && readOf(node))
            {
                reads.push_back(node);
            }
        });
        // Stepped, added to, or set where what the assignment gives is
        // used: a number. After every declaration, which a function may
        // come before.
        walk(mScript, [&](LSLASTNode* node) {
            if (LSLSymbol* var = setBy(node))
            {
                if (static_cast<LSLExpression*>(node)->getOperation() != OP_ASSIGN || valueUsed(node))
                {
                    mBooleans.erase(var);
                }
                else
                {
                    sets.emplace_back(var, static_cast<LSLBinaryExpression*>(node)->getRHS());
                }
            }
        });
        // Those only ever TRUE or FALSE, which may be asked which.
        mZeroOne = mBooleans;
        for (bool changed = true; changed;)
        {
            changed = false;
            for (const auto& [symbol, e] : sets)
            {
                if (mZeroOne.contains(symbol) && !zeroOrOne(e))
                {
                    mZeroOne.erase(symbol);
                    changed = true;
                }
            }
        }
        for (bool changed = true; changed;)
        {
            changed = false;
            for (LSLASTNode* read : reads)
            {
                LSLSymbol* symbol = readOf(read);
                if (boolean(symbol) && !assignedBy(read) && valueUsed(read) && !readAsTruth(read))
                {
                    mBooleans.erase(symbol);
                    changed = true;
                }
            }
        }
        // Nothing to say of one never read: kept a number, as it was.
        boost::unordered_flat_set<LSLSymbol*> readSome;
        for (LSLASTNode* read : reads)
        {
            if (!assignedBy(read) && valueUsed(read))
            {
                readSome.insert(readOf(read));
            }
        }
        boost::unordered::erase_if(mBooleans, [&readSome](LSLSymbol* symbol) { return !readSome.contains(symbol); });
    }

    namespace
    {
        // The one statement a branch is, as an assignment of a whole
        // variable: its target, or null.
        LSLBinaryExpression* branchAssignment(LSLASTNode* branch)
        {
            if (!isNull(branch) && branch->getNodeSubType() == NODE_COMPOUND_STATEMENT)
            {
                LSLASTNode* only = branch->getChild(0);
                branch           = only && !only->getNext() ? only : nullptr;
            }
            if (isNull(branch) || branch->getNodeSubType() != NODE_EXPRESSION_STATEMENT)
            {
                return nullptr;
            }
            LSLExpression* e = unwrapped(static_cast<LSLExpressionStatement*>(branch)->getExpr());
            if (e->getNodeSubType() != NODE_BINARY_EXPRESSION || e->getOperation() != OP_ASSIGN ||
                !wholeVariable(static_cast<LSLBinaryExpression*>(e)->getLHS()))
            {
                return nullptr;
            }
            return static_cast<LSLBinaryExpression*>(e);
        }
    }

    bool Writer::booleanChoice(LSLIfStatement* i, LSLIdentifier*& var, bool& when_true)
    {
        LSLBinaryExpression* yes = branchAssignment(i->getTrueBranch());
        LSLBinaryExpression* no  = branchAssignment(i->getFalseBranch());
        bool                 a   = false;
        bool                 b   = false;
        if (!yes || !no || wholeVariable(yes->getLHS()) != wholeVariable(no->getLHS()) || !boolean(wholeVariable(yes->getLHS())) ||
            !truthConstant(yes->getRHS(), a) || !truthConstant(no->getRHS(), b) || a == b)
        {
            return false;
        }
        var       = static_cast<LSLLValueExpression*>(unwrapped(yes->getLHS()))->getIdentifier();
        when_true = a;
        return true;
    }

    // static
    bool Writer::handlerTail(LSLASTNode* s)
    {
        for (LSLASTNode* node = s; node;)
        {
            LSLASTNode* parent = node->getParent();
            if (!parent)
            {
                return false;
            }
            if (parent->getNodeType() == NODE_EVENT_HANDLER)
            {
                return true;
            }
            if (parent->getNodeSubType() == NODE_COMPOUND_STATEMENT)
            {
                if (node->getNext())
                {
                    return false;
                }
            }
            else if (parent->getNodeSubType() != NODE_IF_STATEMENT || node->getParentSlot() == 0)
            {
                return false;
            }
            node = parent;
        }
        return false;
    }

    std::optional<Expr> Writer::truthAsked(LSLBinaryExpression* e, LSLOperator op)
    {
        bool           truth = false;
        LSLExpression* side  = nullptr;
        if (boolean(readOf(unwrapped(e->getLHS()))) && truthConstant(e->getRHS(), truth))
        {
            side = e->getLHS();
        }
        else if (boolean(readOf(unwrapped(e->getRHS()))) && truthConstant(e->getLHS(), truth))
        {
            side = e->getRHS();
        }
        if (!side)
        {
            return std::nullopt;
        }
        const Expr is = expr(side);
        if ((op == OP_EQ) == truth)
        {
            return Expr{ is.text, is.prec, true };
        }
        return Expr{ "not " + bracketed(is, UNARY), UNARY, true };
    }

    std::string Writer::truthOf(LSLExpression* e)
    {
        int v = 0;
        if (wholeNumber(e, v))
        {
            return v != 0 ? "true" : "false";
        }
        return condition(e).text;
    }

    // --- LSL's order -------------------------------------------------------------------

    bool Writer::orderMatters(LSLExpression* lhs, LSLExpression* rhs) const
    {
        const ALLSLEffects::Writes left  = mEffects.of(lhs);
        const ALLSLEffects::Writes right = mEffects.of(rhs);
        // Both past the script, where either may change something there.
        if (left.impure && right.impure && !(ALLSLTraits::changesNothing(lhs) && ALLSLTraits::changesNothing(rhs)))
        {
            return true;
        }
        // What one side sets, the other reads, or a function of the
        // script's that it calls may.
        const auto sees = [](const ALLSLEffects::Writes& w, LSLExpression* other) {
            if (w.variables.empty())
            {
                return false;
            }
            bool seen = false;
            walk(other, [&](LSLASTNode* node) {
                if (node->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                {
                    seen = seen || w.writes(static_cast<LSLLValueExpression*>(node)->getIdentifier()->getSymbol());
                }
                else if (node->getNodeSubType() == NODE_FUNCTION_EXPRESSION && readOf(node))
                {
                    seen = true;
                }
            });
            return seen;
        };
        return sees(left, rhs) || sees(right, lhs);
    }

    namespace
    {
        // [] or "" written out.
        bool emptyValue(LSLExpression* e)
        {
            e = unwrapped(e);
            if (!e)
            {
                return false;
            }
            if (e->getNodeSubType() == NODE_LIST_EXPRESSION)
            {
                return isNull(e->getChild(0));
            }
            if (e->getNodeSubType() != NODE_CONSTANT_EXPRESSION)
            {
                return false;
            }
            LSLASTNode* c = e->getChild(0);
            if (c->getNodeSubType() == NODE_LIST_CONSTANT)
            {
                return !static_cast<LSLListConstant*>(c)->getValue();
            }
            return c->getNodeSubType() == NODE_STRING_CONSTANT && !*static_cast<LSLStringConstant*>(c)->getValue();
        }
    }

    void Writer::forgetMemoryHacks()
    {
        // Found first, then taken out, the tree not changed under the walk.
        std::vector<LSLBinaryExpression*> clearings;
        walk(mScript, [&](LSLASTNode* node) {
            if (node->getNodeSubType() != NODE_BINARY_EXPRESSION || static_cast<LSLExpression*>(node)->getOperation() != OP_ASSIGN ||
                valueUsed(node))
            {
                return;
            }
            auto*          assignment = static_cast<LSLBinaryExpression*>(node);
            LSLSymbol*     var        = wholeVariable(assignment->getLHS());
            const LSLIType type       = assignment->getLHS()->getIType();
            if (!var || (type != LST_LIST && type != LST_STRING))
            {
                return;
            }
            // The innermost of the +'s it is set to, whose left runs last.
            LSLExpression*       top   = unwrapped(assignment->getRHS());
            LSLBinaryExpression* first = nullptr;
            for (LSLExpression* e = top; e && e->getNodeSubType() == NODE_BINARY_EXPRESSION && e->getOperation() == OP_PLUS;
                 e = unwrapped(static_cast<LSLBinaryExpression*>(e)->getLHS()))
            {
                first = static_cast<LSLBinaryExpression*>(e);
            }
            if (!first || (first == top && first->getRHS()->getIType() != type))
            {
                return;
            }
            LSLExpression* clear = unwrapped(first->getLHS());
            if (clear->getNodeSubType() == NODE_BINARY_EXPRESSION && clear->getOperation() == OP_ASSIGN &&
                wholeVariable(static_cast<LSLBinaryExpression*>(clear)->getLHS()) == var &&
                emptyValue(static_cast<LSLBinaryExpression*>(clear)->getRHS()))
            {
                clearings.push_back(first);
                mFreedFirst.insert(node);
            }
        });
        for (LSLBinaryExpression* first : clearings)
        {
            LSLASTNode::replaceNode(first, first->takeChild(1));
        }
    }

    // --- lists grown in place -----------------------------------------------------------

    bool Writer::fresh(LSLExpression* e) const
    {
        e = unwrapped(e);
        if (!e)
        {
            return false;
        }
        switch (e->getNodeSubType())
        {
            case NODE_LIST_EXPRESSION:
            case NODE_CONSTANT_EXPRESSION: return true;
            case NODE_BINARY_EXPRESSION: return e->getOperation() == OP_PLUS;
            // The same type is written as the value itself.
            case NODE_TYPECAST_EXPRESSION: return static_cast<LSLTypecastExpression*>(e)->getChildExpr()->getIType() != LST_LIST;
            case NODE_FUNCTION_EXPRESSION:
            {
                LSLSymbol* symbol = static_cast<LSLFunctionExpression*>(e)->getIdentifier()->getSymbol();
                return symbol && (symbol->getSubType() == SYM_BUILTIN || mFreshFunctions.contains(symbol));
            }
            case NODE_LVALUE_EXPRESSION:
            {
                LSLSymbol* var = wholeVariable(e);
                return owned(var) && var->getSubType() == SYM_LOCAL;
            }
            default: return false;
        }
    }

    bool Writer::madeHere(LSLExpression* e) const
    {
        LSLExpression* m = unwrapped(e);
        return m && m->getNodeSubType() != NODE_LVALUE_EXPRESSION && !(m->getNodeSubType() == NODE_BINARY_EXPRESSION && m->getOperation() == OP_PLUS) &&
               fresh(m);
    }

    std::string Writer::anyItems(LSLExpression* e, const Expr& written) const
    {
        return madeHere(e) ? bracketed(written, PRIMARY) + " :: { any }" : written.text;
    }

    LSLASTNode* Writer::soleItem(LSLExpression* e) const
    {
        e = unwrapped(e);
        LSLASTNode* item = nullptr;
        if (e && e->getNodeSubType() == NODE_LIST_EXPRESSION)
        {
            item = e->getChild(0);
        }
        else if (e && e->getNodeSubType() == NODE_CONSTANT_EXPRESSION && e->getChild(0)->getNodeSubType() == NODE_LIST_CONSTANT)
        {
            item = static_cast<LSLListConstant*>(e->getChild(0))->getValue();
        }
        return !isNull(item) && isNull(item->getNext()) ? item : nullptr;
    }

    std::string Writer::itemText(LSLASTNode* item)
    {
        // As LSL typed it: NULL_KEY in a list is LSL's string.
        if (item->getNodeType() == NODE_CONSTANT)
        {
            return constant(static_cast<LSLConstant*>(item)).text;
        }
        auto* e = static_cast<LSLExpression*>(item);
        return coerced(e, e->getIType()).text;
    }

    Shared Writer::handedOn(LSLASTNode* read) const
    {
        // Up through brackets, and casts to what it already is.
        LSLASTNode* node   = read;
        LSLASTNode* parent = node->getParent();
        while (parent && (parent->getNodeSubType() == NODE_PARENTHESIS_EXPRESSION ||
                          (parent->getNodeSubType() == NODE_TYPECAST_EXPRESSION && parent->getIType() == LST_LIST)))
        {
            node   = parent;
            parent = parent->getParent();
        }
        if (!parent)
        {
            return Shared::None;
        }
        const int slot = node->getParentSlot();
        switch (parent->getNodeSubType())
        {
            case NODE_BINARY_EXPRESSION:
                return static_cast<LSLExpression*>(parent)->getOperation() == OP_ASSIGN && slot == 1 ? Shared::Given : Shared::None;
            case NODE_DECLARATION: return slot == 1 ? Shared::Given : Shared::None;
            case NODE_RETURN_STATEMENT:
                return readOf(read) && readOf(read)->getSubType() == SYM_LOCAL ? Shared::None : Shared::Returned;
            default:
                if (parent->getNodeType() == NODE_GLOBAL_VARIABLE)
                {
                    return slot == 1 ? Shared::Given : Shared::None;
                }
                if (parent->getNodeType() == NODE_AST_NODE_LIST && parent->getParent() &&
                    parent->getParent()->getNodeSubType() == NODE_FUNCTION_EXPRESSION && readOf(parent->getParent()))
                {
                    // Not where the parameter it is passed to only borrows it.
                    LSLParamList* params = readOf(parent->getParent())->getFunctionDecl();
                    LSLASTNode*   param  = params ? params->getChild(slot) : nullptr;
                    if (!isNull(param) && mBorrows.contains(static_cast<LSLIdentifier*>(param)->getSymbol()))
                    {
                        return Shared::None;
                    }
                    return Shared::Kept;
                }
                return Shared::None;
        }
    }

    void Writer::findOwnedLists()
    {
        // Every list the script declares and every function of its own that
        // answers one, then each let go of that is given, or hands on, a
        // list another may hold, until none is.
        std::vector<LSLASTNode*>                           reads;
        std::vector<std::pair<LSLSymbol*, LSLExpression*>> sets;
        const auto candidate = [&](LSLIdentifier* id, LSLExpression* init) {
            if (id->getIType() == LST_LIST && id->getSymbol())
            {
                mOwned.insert(id->getSymbol());
                if (!isNull(init))
                {
                    sets.emplace_back(id->getSymbol(), init);
                }
            }
        };
        const auto letGo = [&](LSLSymbol* symbol, Shared why) {
            mSharedWhy.emplace(symbol, why);
            return mOwned.erase(symbol) + mFreshFunctions.erase(symbol) + mBorrows.erase(symbol) != 0;
        };
        walk(mScript, [&](LSLASTNode* node) {
            if (node->getNodeType() == NODE_GLOBAL_VARIABLE)
            {
                auto* global = static_cast<LSLGlobalVariable*>(node);
                candidate(global->getIdentifier(), global->getInitializer());
            }
            else if (node->getNodeType() == NODE_GLOBAL_FUNCTION)
            {
                auto* f = static_cast<LSLGlobalFunction*>(node);
                if (f->getIdentifier()->getIType() == LST_LIST && f->getSymbol())
                {
                    mFreshFunctions.insert(f->getSymbol());
                }
                LSLASTNode* params = f->getArguments();
                for (LSLASTNode* param = params ? params->getChild(0) : nullptr; !isNull(param); param = param->getNext())
                {
                    if (param->getIType() == LST_LIST && static_cast<LSLIdentifier*>(param)->getSymbol())
                    {
                        mBorrows.insert(static_cast<LSLIdentifier*>(param)->getSymbol());
                    }
                }
            }
            else if (node->getNodeSubType() == NODE_DECLARATION)
            {
                auto* declaration = static_cast<LSLDeclaration*>(node);
                candidate(declaration->getIdentifier(), declaration->getInitializer());
            }
            else if (node->getNodeSubType() == NODE_RETURN_STATEMENT)
            {
                LSLExpression* e = static_cast<LSLReturnStatement*>(node)->getExpr();
                if (LSLSymbol* f = enclosingFunction(node); f && !isNull(e))
                {
                    sets.emplace_back(f, e);
                }
            }
            else if (node->getNodeSubType() == NODE_LVALUE_EXPRESSION && node->getIType() == LST_LIST &&
                     wholeVariable(static_cast<LSLExpression*>(node)))
            {
                reads.push_back(node);
            }
        });
        // After every declaration, which a function may come before.
        walk(mScript, [&](LSLASTNode* node) {
            LSLSymbol* var = setBy(node);
            if (!var)
            {
                return;
            }
            // A parameter set holds a list of its own.
            mBorrows.erase(var);
            if (valueUsed(node))
            {
                letGo(var, Shared::Assigned);
            }
            else if (static_cast<LSLExpression*>(node)->getOperation() == OP_ASSIGN)
            {
                sets.emplace_back(var, static_cast<LSLBinaryExpression*>(node)->getRHS());
            }
        });
        // A global read where a call of the script's in the same statement
        // may change it: Luau would hold the list itself, grown by the call,
        // where LSL held a copy.
        for (LSLASTNode* read : reads)
        {
            LSLSymbol* var = readOf(read);
            if (!var || var->getSubType() != SYM_GLOBAL || assignedBy(read))
            {
                continue;
            }
            LSLASTNode* whole = read;
            while (whole->getParent() && (whole->getParent()->getNodeType() == NODE_EXPRESSION ||
                                          (whole->getParent()->getNodeType() == NODE_AST_NODE_LIST && whole->getParent()->getParent() &&
                                           whole->getParent()->getParent()->getNodeType() == NODE_EXPRESSION)))
            {
                whole = whole->getParent();
            }
            bool changes = false;
            walk(whole, [&](LSLASTNode* node) {
                if (LSLSymbol* f = node->getNodeSubType() == NODE_FUNCTION_EXPRESSION ? readOf(node) : nullptr)
                {
                    changes = changes || mEffects.ofFunction(f).writes(var);
                }
            });
            if (changes)
            {
                letGo(var, Shared::Called);
            }
        }
        for (bool changed = true; changed;)
        {
            changed = false;
            for (const auto& [symbol, e] : sets)
            {
                if ((mOwned.contains(symbol) || mFreshFunctions.contains(symbol)) && !fresh(e))
                {
                    changed = letGo(symbol, Shared::Held) || changed;
                }
            }
            for (LSLASTNode* read : reads)
            {
                LSLSymbol* var = readOf(read);
                if ((owned(var) || mBorrows.contains(var)) && !assignedBy(read))
                {
                    if (const Shared why = handedOn(read); why != Shared::None)
                    {
                        changed = letGo(var, why) || changed;
                    }
                }
            }
        }
    }

    bool Writer::editInPlace(LSLSymbol* var, const std::string& name, LSLExpression* rhs)
    {
        LSLExpression* e = unwrapped(rhs);
        if (!mOptions.idioms || !owned(var) || !e || e->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
        {
            return false;
        }
        auto*                  call = static_cast<LSLFunctionExpression*>(e);
        const std::string_view lsl  = call->getIdentifier()->getName();
        if (wholeVariable(argumentAt(call, 0)) != var)
        {
            return false;
        }
        int from = 1;
        int to   = 1;
        if (lsl == "llDeleteSubList" && wholeNumber(argumentAt(call, 1), from) && wholeNumber(argumentAt(call, 2), to) && from == to &&
            (from == 0 || from == -1))
        {
            line(from == 0 ? "table.remove(" + name + ", 1)" : "table.remove(" + name + ")");
            return true;
        }
        // One item, changing nothing of l's: put in at the front, or on the
        // end, at the list's length; or in place of another, at a counter
        // within the list (mWithin), which is always one of its places.
        LSLExpression* added = argumentAt(call, 1);
        LSLASTNode*    item  = soleItem(added);
        if (!item || mEffects.of(added).writes(var))
        {
            return false;
        }
        LSLExpression* at     = unwrapped(argumentAt(call, 2));
        const bool     length = at && at->getNodeSubType() == NODE_FUNCTION_EXPRESSION &&
                            std::string_view(static_cast<LSLFunctionExpression*>(at)->getIdentifier()->getName()) == "llGetListLength" &&
                            wholeVariable(argumentAt(static_cast<LSLFunctionExpression*>(at), 0)) == var;
        if (lsl == "llListInsertList" && ((wholeNumber(at, from) && from == 0) || length))
        {
            line("table.insert(" + name + (length ? ", " : ", 1, ") + itemText(item) + ")");
            return true;
        }
        LSLSymbol* counter = wholeVariable(at);
        const auto within  = counter ? mWithin.find(counter) : mWithin.end();
        if (lsl == "llListReplaceList" && within != mWithin.end() && within->second == var && wholeVariable(argumentAt(call, 3)) == counter)
        {
            if (std::optional<std::string> place = llIndex(at))
            {
                line(name + "[" + *place + "] = " + itemText(item));
                return true;
            }
        }
        return false;
    }

    bool Writer::grow(LSLLValueExpression* target, const std::string& name, LSLOperator op, LSLExpression* rhs)
    {
        LSLSymbol* var = target->getIdentifier()->getSymbol();
        // What is added, in order, and whether before: l += more, l = l + a
        // + b ..., l = more + l.
        std::vector<LSLExpression*> parts;
        bool                        before = false;
        if (op == OP_ADD_ASSIGN)
        {
            parts.push_back(rhs);
        }
        else
        {
            LSLExpression* sum = unwrapped(rhs);
            while (sum && sum->getNodeSubType() == NODE_BINARY_EXPRESSION && sum->getOperation() == OP_PLUS)
            {
                auto* b = static_cast<LSLBinaryExpression*>(sum);
                parts.insert(parts.begin(), b->getRHS());
                sum = unwrapped(b->getLHS());
            }
            if (wholeVariable(sum) != var)
            {
                parts.clear();
                LSLExpression* one = unwrapped(rhs);
                if (one && one->getNodeSubType() == NODE_BINARY_EXPRESSION && one->getOperation() == OP_PLUS &&
                    wholeVariable(static_cast<LSLBinaryExpression*>(one)->getRHS()) == var)
                {
                    parts.push_back(static_cast<LSLBinaryExpression*>(one)->getLHS());
                    before = true;
                }
            }
        }
        if (parts.empty())
        {
            return false;
        }
        if (!owned(var))
        {
            // Why each + makes a new list of it.
            if (const auto why = mSharedWhy.find(var); why != mSharedWhy.end())
            {
                noteOnce(target, "SluaListCopy",
                         "LSL's lists were values, and [1] is [2], so each + makes a new list of it, as LSL did. Where nothing else holds it, "
                         "table.insert([1], x) grows it in place.",
                         { name, sharedWords(why->second) });
            }
            return false;
        }
        const auto readsList = [&](LSLASTNode* e) {
            bool reads = false;
            walk(e, [&](LSLASTNode* node) { reads = reads || (node->getNodeSubType() == NODE_LVALUE_EXPRESSION && readOf(node) == var); });
            return reads;
        };
        // What runs apart from the rest: changes nothing, and reads not l.
        const auto apart = [&](LSLASTNode* e) { return ALLSLTraits::sideEffectFree(e) && !readsList(e); };

        // Each part, as what it adds: one value, the items of a list
        // written out, another list by its name, or any other list.
        enum class Kind
        {
            Value,
            Items,
            Named,
            Other
        };
        struct Part
        {
            Kind                     kind;
            LSLExpression*           e;
            std::vector<LSLASTNode*> items;
        };
        std::vector<Part> shaped;
        for (LSLExpression* e : parts)
        {
            // What could set l itself would leave the insert on a list it
            // no longer is; several parts must each run apart.
            if (mEffects.of(e).writes(var) || (parts.size() > 1 && !apart(e)))
            {
                return false;
            }
            LSLExpression* m = unwrapped(e);
            Part           part{ Kind::Other, e, {} };
            if (e->getIType() != LST_LIST)
            {
                part.kind = Kind::Value;
            }
            else if (m->getNodeSubType() == NODE_LIST_EXPRESSION)
            {
                part.kind = Kind::Items;
                for (LSLASTNode* item = m->getChild(0); !isNull(item); item = item->getNext())
                {
                    part.items.push_back(item);
                }
            }
            else if (m->getNodeSubType() == NODE_CONSTANT_EXPRESSION && m->getChild(0)->getNodeSubType() == NODE_LIST_CONSTANT)
            {
                part.kind = Kind::Items;
                for (LSLConstant* item = static_cast<LSLListConstant*>(m->getChild(0))->getValue(); item;
                     item = static_cast<LSLConstant*>(item->getNext()))
                {
                    part.items.push_back(item);
                }
            }
            else if (wholeVariable(m))
            {
                part.kind = Kind::Named;
            }
            // Before l, only one value.
            if ((part.kind == Kind::Items && part.items.empty()) ||
                (before && !(part.kind == Kind::Value || (part.kind == Kind::Items && part.items.size() == 1))))
            {
                return false;
            }
            shaped.push_back(std::move(part));
        }
        const std::string at   = before ? ", 1, " : ", ";
        const auto        text = [&](LSLASTNode* item) {
            return item->getNodeType() == NODE_CONSTANT ? constant(static_cast<LSLConstant*>(item)).text
                                                        : value(static_cast<LSLExpression*>(item)).text;
        };
        // The values added at the end one after another, one table.append
        // of them all, which has each before it adds any, as a list written
        // out had -- and of several parts, each runs apart -- but one alone,
        // which table.insert, a builtin of Luau's, adds faster; and more than
        // a call takes well, a table of them put on the end.
        std::vector<std::string> values;
        const auto               append = [&]() {
            std::string all;
            for (const std::string& v : values)
            {
                all += (all.empty() ? "" : ", ") + v;
            }
            if (values.size() == 1)
            {
                line("table.insert(" + name + ", " + all + ")");
            }
            else if (values.size() > 32)
            {
                line("table.extend(" + name + ", {" + all + "})");
            }
            else if (!values.empty())
            {
                line("table.append(" + name + ", " + all + ")");
            }
            values.clear();
        };
        for (const Part& part : shaped)
        {
            if (part.kind != Kind::Value && part.kind != Kind::Items)
            {
                append();
            }
            switch (part.kind)
            {
                case Kind::Value:
                    if (before)
                    {
                        line("table.insert(" + name + at + value(part.e).text + ")");
                    }
                    else
                    {
                        values.push_back(value(part.e).text);
                    }
                    break;
                case Kind::Items:
                    for (LSLASTNode* item : part.items)
                    {
                        if (before)
                        {
                            line("table.insert(" + name + at + text(item) + ")");
                        }
                        else
                        {
                            values.push_back(text(item));
                        }
                    }
                    break;
                case Kind::Named:
                case Kind::Other:
                    // Another list's items, put on the end by table.extend,
                    // which runs what it is given once; it may be l itself,
                    // which it copies as memmove.
                    line("table.extend(" + name + ", " + expr(part.e).text + ")");
                    break;
            }
        }
        append();
        return true;
    }

    // --- strings built in loops ---------------------------------------------------------

    std::vector<LSLExpression*> Writer::appended(LSLASTNode* node, LSLSymbol*& var)
    {
        std::vector<LSLExpression*> parts;
        if (!node || node->getNodeSubType() != NODE_BINARY_EXPRESSION || valueUsed(node))
        {
            return parts;
        }
        auto*             b  = static_cast<LSLBinaryExpression*>(node);
        const LSLOperator op = b->getOperation();
        var                  = wholeVariable(b->getLHS());
        if (!var || b->getLHS()->getIType() != LST_STRING)
        {
            return parts;
        }
        if (op == OP_ADD_ASSIGN)
        {
            parts.push_back(b->getRHS());
        }
        else if (op == OP_ASSIGN)
        {
            // s = s + a + b: the +'s down their left to s.
            LSLExpression* sum = unwrapped(b->getRHS());
            while (sum && sum->getNodeSubType() == NODE_BINARY_EXPRESSION && sum->getOperation() == OP_PLUS)
            {
                parts.insert(parts.begin(), static_cast<LSLBinaryExpression*>(sum)->getRHS());
                sum = unwrapped(static_cast<LSLBinaryExpression*>(sum)->getLHS());
            }
            if (wholeVariable(sum) != var)
            {
                parts.clear();
            }
        }
        return parts;
    }

    void Writer::findStringBuilds()
    {
        const auto loop = [](LSLASTNode* node) {
            const LSLNodeSubType t = node->getNodeSubType();
            return t == NODE_FOR_STATEMENT || t == NODE_WHILE_STATEMENT || t == NODE_DO_STATEMENT;
        };
        walk(mScript, [&](LSLASTNode* node) {
            if (!loop(node))
            {
                return;
            }
            // Its appends, and each string read or set any other way in it
            // or declared in it.
            std::vector<std::pair<LSLSymbol*, LSLASTNode*>> appends;
            boost::unordered_flat_set<LSLASTNode*>          accounted;
            walk(node, [&](LSLASTNode* inner) {
                LSLSymbol* var = nullptr;
                if (appended(inner, var).empty())
                {
                    return;
                }
                appends.emplace_back(var, inner);
                auto* b = static_cast<LSLBinaryExpression*>(inner);
                accounted.insert(unwrapped(b->getLHS()));
                // s = s + a: the s it adds to, which is the append's own.
                // Nothing of s += a is: a string appended is read.
                if (b->getOperation() == OP_ASSIGN)
                {
                    LSLExpression* sum = unwrapped(b->getRHS());
                    while (sum->getNodeSubType() == NODE_BINARY_EXPRESSION)
                    {
                        sum = unwrapped(static_cast<LSLBinaryExpression*>(sum)->getLHS());
                    }
                    accounted.insert(sum);
                }
            });
            if (appends.empty())
            {
                return;
            }
            boost::unordered_flat_set<LSLSymbol*> otherwise;
            walk(node, [&](LSLASTNode* inner) {
                if (inner->getNodeSubType() == NODE_LVALUE_EXPRESSION && !accounted.contains(inner))
                {
                    otherwise.insert(static_cast<LSLLValueExpression*>(inner)->getIdentifier()->getSymbol());
                }
                else if (inner->getNodeSubType() == NODE_DECLARATION)
                {
                    otherwise.insert(static_cast<LSLDeclaration*>(inner)->getIdentifier()->getSymbol());
                }
            });
            for (const auto& [var, append] : appends)
            {
                // Built by a loop around this one already.
                bool outer = false;
                for (LSLASTNode* up = node->getParent(); up && !outer; up = up->getParent())
                {
                    const auto builds = mBuilds.find(up);
                    outer = builds != mBuilds.end() && std::ranges::find(builds->second, var) != builds->second.end();
                }
                if (outer)
                {
                    continue;
                }
                std::vector<LSLSymbol*>& built = mBuilds[node];
                if (var->getSubType() == SYM_LOCAL && !otherwise.contains(var))
                {
                    if (std::ranges::find(built, var) == built.end())
                    {
                        built.push_back(var);
                    }
                }
                else
                {
                    mUnbuilt.insert(append);
                }
                if (built.empty())
                {
                    mBuilds.erase(node);
                }
            }
        });
        // Declared empty just before the loop that builds it, in the same
        // block, nothing between reading or setting it: declared at the join.
        const auto touches = [](LSLASTNode* statement, LSLSymbol* var) {
            bool seen = false;
            walk(statement, [&](LSLASTNode* inner) {
                seen = seen || (inner->getNodeSubType() == NODE_LVALUE_EXPRESSION &&
                                static_cast<LSLLValueExpression*>(inner)->getIdentifier()->getSymbol() == var);
            });
            return seen;
        };
        for (const auto& [built_by, vars] : mBuilds)
        {
            LSLASTNode* block = built_by->getParent();
            if (!block || block->getNodeSubType() != NODE_COMPOUND_STATEMENT)
            {
                continue;
            }
            for (LSLSymbol* var : vars)
            {
                LSLDeclaration* declared = nullptr;
                bool            touched  = false;
                for (LSLASTNode* statement = block->getChild(0); statement && statement != built_by; statement = statement->getNext())
                {
                    if (statement->getNodeSubType() == NODE_DECLARATION && static_cast<LSLDeclaration*>(statement)->getIdentifier()->getSymbol() == var)
                    {
                        declared = static_cast<LSLDeclaration*>(statement);
                        touched  = false;
                    }
                    else if (declared && touches(statement, var))
                    {
                        touched = true;
                    }
                }
                if (declared && !touched && (isNull(declared->getInitializer()) || emptyValue(declared->getInitializer())))
                {
                    mJoinedDeclarations.insert(declared);
                    // By this loop alone: a later one building it adds to
                    // what this one declared.
                    mDeclaredAtJoin.insert({ built_by, var });
                }
            }
        }
        // An append an outer loop builds after all is not noted.
        boost::unordered::erase_if(mUnbuilt, [&](LSLASTNode* append) {
            LSLSymbol* var = nullptr;
            appended(append, var);
            bool built = false;
            for (LSLASTNode* up = append->getParent(); up && !built; up = up->getParent())
            {
                const auto builds = mBuilds.find(up);
                built = builds != mBuilds.end() && std::ranges::find(builds->second, var) != builds->second.end();
            }
            return built;
        });
    }

    void Writer::buildingLoop(LSLASTNode* loop, bool last)
    {
        const std::vector<LSLSymbol*> vars = std::move(mBuilds[loop]);
        mBuilds.erase(loop);
        struct Join
        {
            std::string name;
            std::string parts;
            bool        declared = false;
        };
        std::vector<Join> joins;
        for (LSLSymbol* var : vars)
        {
            const std::string name  = mNames.contains(var) ? mNames[var] : nameOf(var->getName());
            const std::string parts = freshName(name + "Parts");
            line("local " + parts + (mOptions.types ? ": { string }" : "") + " = {}");
            mBuilding[var] = parts;
            joins.push_back({ name, parts, mDeclaredAtJoin.contains({ loop, var }) });
        }
        statement(loop, last);
        for (LSLSymbol* var : vars)
        {
            mBuilding.erase(var);
        }
        for (const Join& join : joins)
        {
            line(join.declared ? "local " + join.name + (mOptions.types ? ": string" : "") + " = table.concat(" + join.parts + ")"
                               : join.name + " ..= table.concat(" + join.parts + ")");
        }
    }

    bool Writer::selfOperation(LSLSymbol* var, LSLIType type, LSLExpression* rhs, LSLOperator& op, std::vector<LSLExpression*>& parts)
    {
        LSLExpression* e = unwrapped(rhs);
        if (!var || type == LST_LIST || !e || e->getNodeSubType() != NODE_BINARY_EXPRESSION)
        {
            return false;
        }
        const LSLOperator by = e->getOperation();
        if (type == LST_STRING)
        {
            // Down the +'s to s, which Luau's .. joins either way round.
            while (e && e->getNodeSubType() == NODE_BINARY_EXPRESSION && e->getOperation() == OP_PLUS)
            {
                parts.insert(parts.begin(), static_cast<LSLBinaryExpression*>(e)->getRHS());
                e = unwrapped(static_cast<LSLBinaryExpression*>(e)->getLHS());
            }
            if (wholeVariable(e) != var || parts.empty())
            {
                parts.clear();
                return false;
            }
            op = OP_ADD_ASSIGN;
            return true;
        }
        auto* b = static_cast<LSLBinaryExpression*>(e);
        if (wholeVariable(b->getLHS()) != var)
        {
            return false;
        }
        switch (by)
        {
            case OP_PLUS: op = OP_ADD_ASSIGN; break;
            case OP_MINUS: op = OP_SUB_ASSIGN; break;
            case OP_MUL: op = OP_MUL_ASSIGN; break;
            case OP_DIV: op = OP_DIV_ASSIGN; break;
            case OP_MOD: op = OP_MOD_ASSIGN; break;
            default: return false;
        }
        parts.push_back(b->getRHS());
        return true;
    }

    void Writer::takeNames()
    {
        if (mTaken.empty())
        {
            walk(mScript, [&](LSLASTNode* node) {
                if (node->getNodeType() == NODE_IDENTIFIER)
                {
                    mTaken.insert(static_cast<LSLIdentifier*>(node)->getName());
                    mTaken.insert(nameOf(static_cast<LSLIdentifier*>(node)->getName()));
                }
            });
        }
    }

    std::string Writer::freshName(const std::string& base)
    {
        takeNames();
        std::string name = base;
        for (int n = 2; mTaken.contains(name) || reservedName(name); ++n)
        {
            name = base + std::to_string(n);
        }
        mTaken.insert(name);
        return name;
    }

    // --- steps as statements --------------------------------------------------------

    Writer::Steps Writer::hoistSteps(LSLExpression* root, bool after, bool statement)
    {
        Steps out;
        // A step or an assignment, and what it sets, whole.
        const auto stepOf = [](LSLASTNode* node, bool& post) -> LSLSymbol* {
            post = false;
            if (node->getNodeSubType() == NODE_UNARY_EXPRESSION)
            {
                const LSLOperator op = static_cast<LSLExpression*>(node)->getOperation();
                if (op != OP_PRE_INCR && op != OP_PRE_DECR && op != OP_POST_INCR && op != OP_POST_DECR)
                {
                    return nullptr;
                }
                post = op == OP_POST_INCR || op == OP_POST_DECR;
                return wholeVariable(static_cast<LSLUnaryExpression*>(node)->getChildExpr());
            }
            return node->getNodeSubType() == NODE_BINARY_EXPRESSION ? setBy(node) : nullptr;
        };
        // What the statement reads and calls.
        boost::unordered_flat_map<LSLSymbol*, int> reads;
        bool                                       calls = false;
        walk(root, [&](LSLASTNode* node) {
            if (node->getNodeSubType() == NODE_LVALUE_EXPRESSION)
            {
                ++reads[static_cast<LSLLValueExpression*>(node)->getIdentifier()->getSymbol()];
            }
            calls = calls || (node->getNodeSubType() == NODE_FUNCTION_EXPRESSION && readOf(node));
        });
        walk(root, [&](LSLASTNode* node) {
            bool       post = false;
            LSLSymbol* var  = node == root && statement ? nullptr : stepOf(node, post);
            if (!var || reads[var] != 1 || (post && !after) || (calls && var->getSubType() == SYM_GLOBAL))
            {
                return;
            }
            // Not one inside another step, which is made where it stands.
            for (LSLASTNode* up = node->getParent(); up && up != root; up = up->getParent())
            {
                bool inner = false;
                if (stepOf(up, inner))
                {
                    return;
                }
            }
            auto* step = static_cast<LSLExpression*>(node);
            mHoisted.insert(step);
            (post ? out.after : out.before).push_back(step);
        });
        return out;
    }

    void Writer::writeSteps(const std::vector<LSLExpression*>& steps)
    {
        for (LSLExpression* step : steps)
        {
            // Made as the statement it would have been, not as its variable.
            mHoisted.erase(step);
            effect(step);
            mHoisted.insert(step);
        }
    }

    // --- list item types -----------------------------------------------------------

    U8 Writer::itemBit(LSLExpression* e)
    {
        switch (slType(e))
        {
            case LST_INTEGER: return ItemInteger;
            case LST_FLOATINGPOINT: return ItemFloat;
            case LST_STRING: return ItemString;
            case LST_KEY: return ItemKey;
            case LST_VECTOR: return ItemVector;
            case LST_QUATERNION: return ItemRotation;
            default: return ItemAny;
        }
    }

    U8 Writer::itemTypes(LSLExpression* e)
    {
        e = unwrapped(e);
        if (!e)
        {
            return ItemAny;
        }
        if (e->getIType() != LST_LIST)
        {
            return itemBit(e);
        }
        switch (e->getNodeSubType())
        {
            case NODE_LIST_EXPRESSION:
            {
                U8 out = 0;
                for (LSLASTNode* item = e->getChild(0); !isNull(item); item = item->getNext())
                {
                    out |= itemBit(static_cast<LSLExpression*>(item));
                }
                return out;
            }
            case NODE_CONSTANT_EXPRESSION:
            {
                U8 out = 0;
                for (LSLConstant* item = static_cast<LSLListConstant*>(e->getChild(0))->getValue(); item;
                     item = static_cast<LSLConstant*>(item->getNext()))
                {
                    switch (item->getNodeSubType())
                    {
                        case NODE_INTEGER_CONSTANT: out |= ItemInteger; break;
                        case NODE_FLOAT_CONSTANT: out |= ItemFloat; break;
                        case NODE_STRING_CONSTANT: out |= ItemString; break;
                        case NODE_KEY_CONSTANT: out |= ItemKey; break;
                        case NODE_VECTOR_CONSTANT: out |= ItemVector; break;
                        case NODE_QUATERNION_CONSTANT: out |= ItemRotation; break;
                        default: out |= ItemAny; break;
                    }
                }
                return out;
            }
            case NODE_BINARY_EXPRESSION:
                if (e->getOperation() == OP_PLUS)
                {
                {
                    auto* b = static_cast<LSLBinaryExpression*>(e);
                    return itemTypes(b->getLHS()) | itemTypes(b->getRHS());
                }
                }
                return ItemAny;
            case NODE_TYPECAST_EXPRESSION: return itemTypes(static_cast<LSLTypecastExpression*>(e)->getChildExpr());
            case NODE_LVALUE_EXPRESSION:
            {
                const auto found = mListTypes.find(wholeVariable(e));
                return found == mListTypes.end() ? ItemAny : found->second;
            }
            case NODE_FUNCTION_EXPRESSION:
            {
                auto*      call   = static_cast<LSLFunctionExpression*>(e);
                LSLSymbol* symbol = call->getIdentifier()->getSymbol();
                if (symbol && symbol->getSubType() != SYM_BUILTIN)
                {
                    const auto found = mListTypes.find(symbol);
                    return found == mListTypes.end() ? ItemAny : found->second;
                }
                const std::string_view name = call->getIdentifier()->getName();
                if (name == "llParseString2List" || name == "llParseStringKeepNulls" || name == "llCSV2List")
                {
                    return ItemString;
                }
                // What gives back the list it was given, or part of it.
                if (name == "llList2List" || name == "llListSort" || name == "llDeleteSubList" || name == "llList2ListStrided" ||
                    name == "llListRandomize" || name == "llList2ListSlice" || name == "llListSortStrided")
                {
                    return itemTypes(argumentAt(call, 0));
                }
                if (name == "llListReplaceList" || name == "llListInsertList")
                {
                    return itemTypes(argumentAt(call, 0)) | itemTypes(argumentAt(call, 1));
                }
                return ItemAny;
            }
            default: return ItemAny;
        }
    }

    void Writer::findListTypes()
    {
        // Every list variable and function of the script's own, nothing yet.
        std::vector<std::pair<LSLSymbol*, LSLExpression*>> given;
        const auto candidate = [&](LSLIdentifier* id, LSLExpression* init) {
            if (id->getIType() == LST_LIST && id->getSymbol())
            {
                mListTypes.emplace(id->getSymbol(), 0);
                if (!isNull(init))
                {
                    given.emplace_back(id->getSymbol(), init);
                }
            }
        };
        walk(mScript, [&](LSLASTNode* node) {
            if (node->getNodeType() == NODE_GLOBAL_VARIABLE)
            {
                candidate(static_cast<LSLGlobalVariable*>(node)->getIdentifier(), static_cast<LSLGlobalVariable*>(node)->getInitializer());
            }
            else if (node->getNodeType() == NODE_GLOBAL_FUNCTION)
            {
                auto* f = static_cast<LSLGlobalFunction*>(node);
                if (f->getIdentifier()->getIType() == LST_LIST && f->getSymbol())
                {
                    mListTypes.emplace(f->getSymbol(), 0);
                }
            }
            else if (node->getNodeSubType() == NODE_DECLARATION)
            {
                candidate(static_cast<LSLDeclaration*>(node)->getIdentifier(), static_cast<LSLDeclaration*>(node)->getInitializer());
            }
        });
        walk(mScript, [&](LSLASTNode* node) {
            if (node->getNodeSubType() == NODE_RETURN_STATEMENT)
            {
                LSLExpression* e = static_cast<LSLReturnStatement*>(node)->getExpr();
                if (LSLSymbol* f = enclosingFunction(node); f && !isNull(e) && mListTypes.contains(f))
                {
                    given.emplace_back(f, e);
                }
                return;
            }
            // Given by = or added to by +=.
            if (LSLSymbol* var = setBy(node); var && mListTypes.contains(var) && node->getNodeSubType() == NODE_BINARY_EXPRESSION)
            {
                const LSLOperator op = static_cast<LSLExpression*>(node)->getOperation();
                if (op == OP_ASSIGN || op == OP_ADD_ASSIGN)
                {
                    given.emplace_back(var, static_cast<LSLBinaryExpression*>(node)->getRHS());
                }
            }
        });
        for (bool changed = true; changed;)
        {
            changed = false;
            for (const auto& [symbol, e] : given)
            {
                const U8 before = mListTypes[symbol];
                const U8 after  = before | itemTypes(e);
                if (after != before)
                {
                    mListTypes[symbol] = after;
                    changed            = true;
                }
            }
        }
    }

    bool Writer::listItemFits(LSLFunctionExpression* e)
    {
        static const boost::unordered_flat_map<std::string_view, std::pair<U8, U8>> FITS = {
            { "llList2String", { ItemString, ItemString | ItemKey | ItemInteger } },
            { "llList2Integer", { ItemInteger, 0 } },
            { "llList2Float", { ItemFloat | ItemInteger, 0 } },
            { "llList2Key", { ItemKey, 0 } },
            { "llList2Vector", { ItemVector, 0 } },
            { "llList2Rot", { ItemRotation, 0 } },
        };
        const auto fits = FITS.find(std::string_view(e->getIdentifier()->getName()));
        LSLExpression* list = argumentAt(e, 0);
        if (!mOptions.idioms || fits == FITS.end() || !wholeVariable(list))
        {
            return false;
        }
        const U8 types = itemTypes(list);
        return (types & ~fits->second.first) == 0 || (types & ~fits->second.second) == 0;
    }

    std::optional<Expr> Writer::listItem(LSLFunctionExpression* e, const std::string& lsl)
    {
        struct Kind
        {
            U8          as_is;
            U8          as_text;
            const char* empty;
        };
        // What each gives as it is, what tostring makes right, and what
        // past the end.
        static const boost::unordered_flat_map<std::string_view, Kind> KINDS = {
            { "llList2String", { ItemString, ItemString | ItemKey | ItemInteger, "\"\"" } },
            { "llList2Integer", { ItemInteger, 0, "0" } },
            { "llList2Float", { ItemFloat | ItemInteger, 0, "0" } },
            { "llList2Key", { ItemKey, 0, "NULL_KEY" } },
            { "llList2Vector", { ItemVector, 0, "ZERO_VECTOR" } },
            { "llList2Rot", { ItemRotation, 0, "ZERO_ROTATION" } },
        };
        const auto kind = KINDS.find(lsl);
        if (kind == KINDS.end())
        {
            return std::nullopt;
        }
        LSLExpression* list  = argumentAt(e, 0);
        LSLExpression* index = argumentAt(e, 1);
        LSLSymbol*     var   = wholeVariable(list);
        const U8       types = var ? itemTypes(list) : ItemAny;
        const bool     as_is = (types & ~kind->second.as_is) == 0;
        if (!var || (!as_is && (types & ~kind->second.as_text) != 0))
        {
            return std::nullopt;
        }
        // Where: from 1, or back from the end.
        const std::string name = lvalue(static_cast<LSLLValueExpression*>(unwrapped(list))).text;
        std::string       at;
        int               v = 0;
        if (wholeNumber(index, v) && v < 0)
        {
            at = "#" + name + (v == -1 ? std::string() : " - " + std::to_string(-v - 1));
        }
        else if (std::optional<std::string> from_one = llIndex(index))
        {
            at = *from_one;
        }
        else
        {
            return std::nullopt;
        }
        // A counter within the list's own length finds an item every time.
        LSLSymbol*        counter = wholeVariable(index);
        const auto        within  = counter ? mWithin.find(counter) : mWithin.end();
        const bool        found   = within != mWithin.end() && within->second == var;
        // And where its loop walks the list's items, the item by name.
        const auto        walked  = found ? mEach.find(counter) : mEach.end();
        const std::string item    = walked != mEach.end() ? walked->second : name + "[" + at + "]";
        const Expr        read    = found ? Expr{ item } : Expr{ item + " or " + kind->second.empty, OR };
        return as_is ? read : Expr{ "tostring(" + read.text + ")" };
    }

    // --- keys that hold text --------------------------------------------------------

    LSLIType Writer::slType(LSLExpression* e) const
    {
        if (uuidConstant(e))
        {
            return LST_KEY;
        }
        LSLExpression* inner = unbracketed(e);
        if (inner && inner->getNodeSubType() == NODE_LVALUE_EXPRESSION && isNull(static_cast<LSLLValueExpression*>(inner)->getMember()))
        {
            return varType(static_cast<LSLLValueExpression*>(inner)->getIdentifier()->getSymbol(), e->getIType());
        }
        return e->getIType();
    }

    void Writer::findTextKeys()
    {
        walk(mScript, [&](LSLASTNode* node) {
            if (node->getNodeType() != NODE_EVENT_HANDLER)
            {
                return;
            }
            auto*             handler = static_cast<LSLEventHandler*>(node);
            const std::string event   = handler->getIdentifier()->getName();
            int               at      = 0;
            for (LSLASTNode* param = handler->getArguments() ? handler->getArguments()->getChild(0) : nullptr; !isNull(param);
                 param = param->getNext(), ++at)
            {
                if (ALLSLTraits::eventTextParam(event, at))
                {
                    mTextParams.insert(static_cast<LSLIdentifier*>(param)->getSymbol());
                }
            }
        });
        // What each key variable is given; each that is given text, until
        // none more is.
        std::vector<std::pair<LSLSymbol*, LSLExpression*>> given;
        const auto key = [&](LSLIdentifier* id, LSLExpression* init) {
            if (id->getIType() == LST_KEY && id->getSymbol() && !isNull(init))
            {
                given.emplace_back(id->getSymbol(), init);
            }
        };
        // A function of the script's own, by its symbol: its parameters.
        boost::unordered_flat_map<LSLSymbol*, LSLASTNode*> parameters;
        walk(mScript, [&](LSLASTNode* node) {
            auto* f = static_cast<LSLGlobalFunction*>(node);
            if (node->getNodeType() == NODE_GLOBAL_FUNCTION && f->getArguments())
            {
                parameters.emplace(f->getSymbol(), f->getArguments());
            }
        });
        walk(mScript, [&](LSLASTNode* node) {
            if (node->getNodeType() == NODE_GLOBAL_VARIABLE)
            {
                key(static_cast<LSLGlobalVariable*>(node)->getIdentifier(), static_cast<LSLGlobalVariable*>(node)->getInitializer());
            }
            else if (node->getNodeSubType() == NODE_DECLARATION)
            {
                key(static_cast<LSLDeclaration*>(node)->getIdentifier(), static_cast<LSLDeclaration*>(node)->getInitializer());
            }
            else if (node->getNodeSubType() == NODE_BINARY_EXPRESSION && static_cast<LSLExpression*>(node)->getOperation() == OP_ASSIGN)
            {
                auto* b      = static_cast<LSLBinaryExpression*>(node);
                auto* target = static_cast<LSLLValueExpression*>(b->getLHS());
                if (b->getLHS()->getNodeSubType() == NODE_LVALUE_EXPRESSION && isNull(target->getMember()))
                {
                    key(target->getIdentifier(), b->getRHS());
                }
            }
            else if (node->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
            {
                // Each argument to a parameter of the script's own.
                auto*      call  = static_cast<LSLFunctionExpression*>(node);
                const auto found = parameters.find(call->getIdentifier()->getSymbol());
                if (found == parameters.end() || isNull(call->getArguments()))
                {
                    return;
                }
                LSLASTNode* param = found->second->getChild(0);
                for (LSLASTNode* arg = call->getArguments()->getChild(0); arg && !isNull(param);
                     arg = arg->getNext(), param = param->getNext())
                {
                    key(static_cast<LSLIdentifier*>(param), static_cast<LSLExpression*>(arg));
                }
            }
        });
        const auto text = [&](LSLExpression* e) {
            e = unbracketed(e);
            if (e && e->getNodeSubType() == NODE_CONSTANT_EXPRESSION && e->getChild(0)->getNodeSubType() == NODE_STRING_CONSTANT)
            {
                const std::string_view value = static_cast<LSLStringConstant*>(e->getChild(0))->getValue();
                return !value.empty() && !ALLSLTraits::isUuid(value);
            }
            if (e && e->getNodeSubType() == NODE_LVALUE_EXPRESSION && isNull(static_cast<LSLLValueExpression*>(e)->getMember()))
            {
                LSLSymbol* var = static_cast<LSLLValueExpression*>(e)->getIdentifier()->getSymbol();
                return mTextParams.contains(var) || mTextKeys.contains(var);
            }
            return false;
        };
        for (bool changed = true; changed;)
        {
            changed = false;
            for (const auto& [var, e] : given)
            {
                if (!mTextKeys.contains(var) && text(e))
                {
                    mTextKeys.insert(var);
                    changed = true;
                }
            }
        }
    }

    // --- the script -------------------------------------------------------------------

    void Writer::globals()
    {
        bool         any    = false;
        const size_t opened = mText.size();
        LSLASTNode*  before = nullptr;
        for (LSLASTNode* g = mScript->getGlobals()->getChild(0); g; g = g->getNext())
        {
            if (g->getNodeType() != NODE_GLOBAL_VARIABLE)
            {
                continue;
            }
            auto*          global = static_cast<LSLGlobalVariable*>(g);
            LSLIdentifier* id     = global->getIdentifier();
            LSLExpression* init   = global->getInitializer();
            // Apart from the one before it as the LSL had them, or a
            // function the LSL had between them, which is written later.
            keepApart(before, global, opened);
            before = global;
            commentsBefore(global);
            const size_t from = mText.size();
            if (boolean(id->getSymbol()))
            {
                line("local " + nameOf(id) + (mOptions.types ? ": boolean" : "") + " = " + (isNull(init) ? std::string("false") : truthOf(init)));
            }
            else
            {
                const LSLIType type = varType(id->getSymbol(), id->getIType());
                line("local " + nameOf(id) + typed(type) + " = " + (isNull(init) ? defaultOf(type) : coerced(init, type).text));
            }
            commentsAfter(global, from);
            lined(global, from);
            any = true;
        }
        lineUp();
        if (any)
        {
            line("");
        }
    }

    void Writer::functions()
    {
        // Declared first, together, those a function written before them
        // calls, which LSL allows and a Luau local function does not; the
        // rest local functions.
        std::vector<LSLGlobalFunction*> all;
        for (LSLASTNode* g = mScript->getGlobals()->getChild(0); g; g = g->getNext())
        {
            if (g->getNodeType() == NODE_GLOBAL_FUNCTION)
            {
                all.push_back(static_cast<LSLGlobalFunction*>(g));
            }
        }
        boost::unordered_flat_set<LSLSymbol*> forward;
        for (size_t i = 0; i < all.size(); ++i)
        {
            walk(all[i]->getStatements(), [&](LSLASTNode* node) {
                if (node->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
                {
                    return;
                }
                // A call to one written after it; one to itself a Luau
                // local function reaches.
                LSLSymbol* callee = static_cast<LSLFunctionExpression*>(node)->getIdentifier()->getSymbol();
                for (size_t j = i + 1; j < all.size(); ++j)
                {
                    if (all[j]->getSymbol() == callee)
                    {
                        forward.insert(callee);
                    }
                }
            });
        }
        if (!forward.empty())
        {
            std::string names;
            for (LSLGlobalFunction* f : all)
            {
                if (forward.contains(f->getSymbol()))
                {
                    names += (names.empty() ? "" : ", ") + nameOf(f->getIdentifier());
                }
            }
            line("local " + names);
            line("");
        }
        for (LSLGlobalFunction* f : all)
        {
            std::string params;
            for (LSLASTNode* p = f->getArguments() ? f->getArguments()->getChild(0) : nullptr; p; p = p->getNext())
            {
                LSLSymbol* param = static_cast<LSLIdentifier*>(p)->getSymbol();
                params += (params.empty() ? "" : ", ") + nameOf(static_cast<LSLIdentifier*>(p)) +
                          (boolean(param) ? std::string(mOptions.types ? ": boolean" : "") : typed(varType(param, p->getIType())));
            }
            mFunction = f->getSymbol();
            commentsBefore(f);
            line(std::string(forward.contains(mFunction) ? "function " : "local function ") + nameOf(f->getIdentifier()) + "(" + params + ")" +
                 (boolean(mFunction) ? std::string(mOptions.types ? ": boolean" : "") : typed(f->getIdentifier()->getIType())));
            trail(f->getStatements());
            ++mDepth;
            prepareBody(f->getStatements());
            block(f->getStatements());
            --mDepth;
            mFunction = nullptr;
            line("end");
            line("");
        }
    }

    std::string Writer::handlerParams(LSLEventHandler* handler, std::vector<std::string>& lead)
    {
        // What LSL's handler took: the count of what was detected, made from
        // SLua's detected table. Where types are written, as the definitions
        // type SLua's handler: a key as a uuid, link_message's id as the
        // string SLua passes.
        const std::string event = handler->getIdentifier()->getName();
        std::string       params;
        LSLASTNode*       first = handler->getArguments() ? handler->getArguments()->getChild(0) : nullptr;
        if (detectedEvent(event))
        {
            // Only where the body names it: a local nothing reads is what a
            // lint says to take out.
            LSLSymbol* count = first ? static_cast<LSLIdentifier*>(first)->getSymbol() : nullptr;
            bool       named = false;
            walk(handler->getStatements(), [&](LSLASTNode* node) {
                named = named || (count && node->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(node)->getSymbol() == count);
            });
            if (named)
            {
                lead.push_back("local " + nameOf(static_cast<LSLIdentifier*>(first)) + typed(LST_INTEGER) + " = #detected");
            }
            return mOptions.types ? "detected: { DetectedEvent }" : "detected";
        }
        int at = 0;
        for (LSLASTNode* p = first; p; p = p->getNext(), ++at)
        {
            auto*          id    = static_cast<LSLIdentifier*>(p);
            const LSLIType given = ALLSLTraits::eventTextParam(event, at) ? LST_STRING : p->getIType();
            std::string    name  = nameOf(id);
            if (varType(id->getSymbol(), given) != given)
            {
                // A key the body takes as text, which SLua passes as a
                // uuid: taken under a name of its own and made the text the
                // body reads as the handler begins, as LSL's key was.
                const std::string text = name;
                name                   = freshName(text + "Key");
                lead.push_back("local " + text + typed(LST_STRING) + " = tostring(" + name + ")");
            }
            params += (params.empty() ? "" : ", ") + name + typed(given);
        }
        return params;
    }

    void Writer::handlerBody(LSLEventHandler* handler)
    {
        prepareBody(handler->getStatements());
        mInDetected = detectedEvent(handler->getIdentifier()->getName());
        mOnDamage   = std::string_view(handler->getIdentifier()->getName()) == "on_damage";
        block(handler->getStatements());
        mInDetected = false;
        mOnDamage   = false;
    }

    void Writer::singleState(LSLState* state)
    {
        LSLEventHandler* entry = nullptr;
        commentsBefore(state);
        for (LSLASTNode* h = state->getEventHandlers()->getChild(0); h; h = h->getNext())
        {
            auto*             handler = static_cast<LSLEventHandler*>(h);
            const std::string event   = handler->getIdentifier()->getName();
            if (event == "state_entry")
            {
                entry = handler;
                continue;
            }
            commentsBefore(handler);
            if (event == "state_exit")
            {
                // Not an event SLua has, and one that never came: said, over
                // a line of its own, and left out; what was after its brace
                // over that line, which stands for the handler's.
                commentsOpening(handler->getStatements());
                note(handler, "SluaStateExit", "state_exit runs as a script leaves a state, and this one has no other: it never ran.");
                line("-- state_exit, left out");
                line("");
                continue;
            }
            std::vector<std::string> lead;
            const std::string        params = handlerParams(handler, lead);
            // The timer's handler, which LLTimers calls, where the script's
            // timer is on LLTimers: the local timersPreamble declared, which
            // a function statement of its name assigns.
            const bool timer = event == "timer" && mTimers;
            if (event == "timer" && !mTimers)
            {
                noteOnce(handler, "SluaTimer", "the timer event, set going by llcompat.SetTimerEvent; LLTimers:every is SLua's own.");
            }
            const bool field = mOptions.handlers == ALLSLToSLua::Options::Handlers::Field;
            line(timer   ? "function timerHandler()"
                 : field ? "function LLEvents." + event + "(" + params + ")"
                         : "LLEvents:on(" + luaString(event) + ", function(" + params + ")");
            trail(handler->getStatements());
            ++mDepth;
            for (const std::string& one : lead)
            {
                line(one);
            }
            handlerBody(handler);
            --mDepth;
            line(timer || field ? "end" : "end)");
            line("");
        }
        if (entry)
        {
            // What LSL ran as the script started, run as SLua runs a script:
            // once, from the top.
            commentsBefore(entry);
            line("-- state_entry");
            mTopLevel = true;
            handlerBody(entry);
            mTopLevel = false;
        }
        commentsAtEnd(state);
    }

    void Writer::statesPreamble()
    {
        const bool field = mOptions.handlers == ALLSLToSLua::Options::Handlers::Field;
        line("-- LSL's states, which SLua has none of: each state's handlers in a table,");
        line(field ? "-- set on LLEvents as it is entered and taken off as it is left." : "-- put on LLEvents as it is entered and taken off as it is left.");
        note(nullptr, "SluaStates",
             "LSL let go of a state's listens, sensor repeats and targets as it left the state; setState does not. Remove them "
             "yourself where the script relied on it.");
        line("local states: { [string]: { [string]: (...any) -> () } } = {}");
        line("local currentState: string? = nil");
        line("");
        line("local function setState(name: string)");
        // Not an LLEvents event: state_entry and state_exit, which setState
        // runs, and the timer where LLTimers calls it.
        const std::string own = mTimers ? "event ~= \"state_entry\" and event ~= \"state_exit\" and event ~= \"timer\""
                                        : "event ~= \"state_entry\" and event ~= \"state_exit\"";
        // Taken off by LLEvents:off either way: SLua puts on a handler
        // assigned to a field as LLEvents:on does, and stops the script at
        // a field assigned nil.
        const std::string off = "LLEvents:off(event :: any, handler)";
        const std::string on  = field ? "(LLEvents :: any)[event] = handler" : "LLEvents:on(event :: any, handler)";
        // The state entered looked up before the one left is, so that a
        // name with none -- which LSL's compiler refused, but an edit
        // afterwards may write -- stops the script saying so, at the call
        // that named it, and leaves it in the state it was in.
        mText += "    if name == currentState then\n"
                 "        return\n"
                 "    end\n"
                 "    local entering = states[name]\n"
                 "    if not entering then\n"
                 "        error(\"unknown state: \" .. name, 2)\n"
                 "    end\n"
                 "    local leaving = currentState and states[currentState]\n"
                 "    if leaving then\n"
                 "        if leaving.state_exit then\n"
                 "            leaving.state_exit()\n"
                 "        end\n"
                 "        for event, handler in leaving do\n"
                 "            if " + own + " then\n"
                 "                " + off + "\n"
                 "            end\n"
                 "        end\n"
                 "    end\n"
                 "    currentState = name\n"
                 "    for event, handler in entering do\n"
                 "        if " + own + " then\n"
                 "            " + on + "\n"
                 "        end\n"
                 "    end\n"
                 "    if entering.state_entry then\n"
                 "        entering.state_entry()\n"
                 "    end\n"
                 "end\n\n";
    }

    bool Writer::oneShotTimer() const
    {
        LSLASTNode* first = mScript->getStates()->getChild(0);
        for (LSLASTNode* s = first; s; s = s->getNext())
        {
            LSLEventHandler* timer = nullptr;
            for (LSLASTNode* h = static_cast<LSLState*>(s)->getEventHandlers()->getChild(0); h; h = h->getNext())
            {
                if (std::string_view(static_cast<LSLEventHandler*>(h)->getIdentifier()->getName()) == "timer")
                {
                    timer = static_cast<LSLEventHandler*>(h);
                }
            }
            // Its first statement llSetTimerEvent of nought: written out, or
            // LSL's FALSE.
            LSLASTNode* statement = timer && timer->getStatements() ? timer->getStatements()->getChild(0) : nullptr;
            if (!statement || statement->getNodeSubType() != NODE_EXPRESSION_STATEMENT)
            {
                return false;
            }
            LSLExpression* e = unbracketed(static_cast<LSLExpressionStatement*>(statement)->getExpr());
            if (!e || e->getNodeSubType() != NODE_FUNCTION_EXPRESSION ||
                std::string_view(static_cast<LSLFunctionExpression*>(e)->getIdentifier()->getName()) != "llSetTimerEvent")
            {
                return false;
            }
            LSLExpression* seconds = unbracketed(argumentAt(static_cast<LSLFunctionExpression*>(e), 0));
            bool           nought  = false;
            if (!numberWritten(seconds, nought))
            {
                LSLSymbol* named = seconds && seconds->getNodeSubType() == NODE_LVALUE_EXPRESSION
                                       ? static_cast<LSLLValueExpression*>(seconds)->getIdentifier()->getSymbol()
                                       : nullptr;
                nought = named && named->getSubType() == SYM_BUILTIN && std::string_view(named->getName()) == "FALSE";
            }
            if (!nought)
            {
                return false;
            }
        }
        return first != nullptr;
    }

    void Writer::timersPreamble()
    {
        // llSetTimerEvent's one timer, on LLTimers: set going again, or
        // stopped, by each setTimer, calling the timer handler of the state
        // the script is in. Where every timer handler turns it off first,
        // LLTimers:once, which runs it as often: once each time it is set.
        line(mOneShot ? "-- llSetTimerEvent's timer, on LLTimers: one at a time, as LSL had it, and once each"
                      : "-- llSetTimerEvent's timer, on LLTimers: one at a time, as LSL had it.");
        if (mOneShot)
        {
            line("-- time it is set, as its handler turns it off before anything else.");
        }
        noteOnce(nullptr, "SluaTimers", "SLua's LLTimers can run several timers at once: LLTimers:every(seconds, callback), "
                                        "LLTimers:once(seconds, callback) and LLTimers:off(timer).");
        if (!mManyStates)
        {
            line("local timerHandler: (() -> ())? = nil");
        }
        line("local timerHandle: any = nil");
        line("");
        line("local function setTimer(seconds: number)");
        mText += "    if timerHandle then\n"
                 "        LLTimers:off(timerHandle)\n"
                 "        timerHandle = nil\n"
                 "    end\n"
                 "    if seconds > 0 then\n";
        mText += mOneShot ? "        timerHandle = LLTimers:once(seconds, function()\n" : "        timerHandle = LLTimers:every(seconds, function()\n";
        mText += mManyStates ? "            local handler = currentState and states[currentState].timer\n"
                               "            if handler then\n"
                               "                handler()\n"
                               "            end\n"
                             : "            if timerHandler then\n"
                               "                timerHandler()\n"
                               "            end\n";
        mText += "        end)\n"
                 "    end\n"
                 "end\n\n";
    }

    void Writer::multiState()
    {
        for (LSLASTNode* s = mScript->getStates()->getChild(0); s; s = s->getNext())
        {
            auto*             state = static_cast<LSLState*>(s);
            const std::string name  = state->getIdentifier()->getName();
            commentsBefore(state);
            line("states" + stateKey(name) + " = {");
            ++mDepth;
            for (LSLASTNode* h = state->getEventHandlers()->getChild(0); h; h = h->getNext())
            {
                auto*             handler = static_cast<LSLEventHandler*>(h);
                const std::string event   = handler->getIdentifier()->getName();
                std::vector<std::string> lead;
                const std::string        params = handlerParams(handler, lead);
                commentsBefore(handler);
                if (event == "timer" && !mTimers)
                {
                    noteOnce(handler, "SluaTimerStates",
                             "the timer event, set going by llcompat.SetTimerEvent, keeps going from state to state, as LSL's "
                             "did; LLTimers:every is SLua's own.");
                }
                line(event + " = function(" + params + ")");
                trail(handler->getStatements());
                ++mDepth;
                for (const std::string& one : lead)
                {
                    line(one);
                }
                handlerBody(handler);
                --mDepth;
                line("end,");
            }
            commentsAtEnd(state);
            --mDepth;
            line("}");
            line("");
        }
        line("setState(\"default\")");
    }

    void Writer::states()
    {
        LSLASTNode* first = mScript->getStates()->getChild(0);
        if (mManyStates)
        {
            multiState();
        }
        else if (first)
        {
            singleState(static_cast<LSLState*>(first));
        }
    }

    void Writer::helpers(std::string& out)
    {
        if (mVecNorm)
        {
            out += R"LUA(-- LSL's llVecNorm: a vector one long the same way; a zero vector stays
-- zero, which vector.normalize makes NaN of.
local function vecNorm(v: vector): vector
    return if v == ZERO_VECTOR then v else vector.normalize(v)
end

)LUA";
        }
        // The script's own clock, from where it was last reset: a
        // script-wide local, which a state change leaves as it left LSL's.
        if (mTimeBase)
        {
            out += R"LUA(-- LSL's script time, which llResetTime set back to nought: SLua's ll has
-- no ResetTime, so the script keeps a clock of its own, read as
-- ll.GetTime() - timeBase.
local timeBase = ll.GetTime()

)LUA";
        }
        if (mGetAndResetTime)
        {
            out += R"LUA(-- LSL's llGetAndResetTime: the time since the last reset, and a reset.
local function getAndResetTime(): number
    local was = timeBase
    timeBase = ll.GetTime()
    return timeBase - was
end

)LUA";
        }
        // LSL's casts of a string, as LSL converts a list's item, which is
        // the same: llcompat's, as tonumber reads text its own way.
        if (mLslInteger)
        {
            out += R"LUA(-- LSL's (integer) of a string: the whole number it starts with, in
-- decimal or 0x hexadecimal, after any spaces; 0 where it starts with none.
local function lslInteger(s: string): number
    return llcompat.List2Integer({ s }, 0)
end

)LUA";
        }
        if (mLslFloat)
        {
            out += R"LUA(-- LSL's (float) of a string: the number it starts with; 0 where it
-- starts with none.
local function lslFloat(s: string): number
    return llcompat.List2Float({ s }, 0)
end

)LUA";
        }
    }

    std::string Writer::write()
    {
        // The jumps SLua has nothing for, whose labels are kept as a mark.
        walk(mScript, [&](LSLASTNode* node) {
            if (node->getNodeSubType() != NODE_JUMP_STATEMENT)
            {
                return;
            }
            auto* j = static_cast<LSLJumpStatement*>(node);
            if (!j->getIsBreakLike() && !j->getIsContinueLike())
            {
                mUnstructured.insert(j->getIdentifier()->getSymbol());
            }
        });
        // What the script sets going: a timer on LLTimers where it sets one.
        LSLASTNode* first = mScript->getStates()->getChild(0);
        mManyStates       = first && first->getNext();
        // And where it resets its time, the clock it is read from.
        bool resets = false;
        walk(mScript, [&](LSLASTNode* node) {
            if (node->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
            {
                return;
            }
            const std::string_view name = static_cast<LSLFunctionExpression*>(node)->getIdentifier()->getName();
            if (mOptions.llTimers && name == "llSetTimerEvent")
            {
                mTimers = true;
            }
            resets |= name == "llResetTime" || name == "llGetAndResetTime";
        });
        mOneShot    = mTimers && oneShotTimer();
        mTimeBase   = resets && mOptions.sluaCalls;
        mCompatTime = resets && !mOptions.sluaCalls;
        forgetMemoryHacks();
        findTextKeys();
        findListTypes();
        findBooleans();
        findOwnedLists();
        findStringBuilds();
        if (mManyStates)
        {
            statesPreamble();
        }
        if (mTimers)
        {
            timersPreamble();
        }
        findComments();
        // The globals never set, declared a number not below nought: what an &
        // with one of them gives needs no sign.
        const ALLSLEffects::Writes written = mEffects.of(mScript);
        for (LSLASTNode* g = mScript->getGlobals()->getChild(0); g; g = g->getNext())
        {
            int v = 0;
            if (g->getNodeType() == NODE_GLOBAL_VARIABLE && !written.writes(g->getSymbol()) &&
                wholeNumber(static_cast<LSLGlobalVariable*>(g)->getInitializer(), v))
            {
                mSteadyWhole.emplace(g->getSymbol(), v);
                if (v >= 0)
                {
                    mSteadyNonNegative.insert(g->getSymbol());
                }
            }
        }
        findBitsOnly();
        globals();
        functions();
        states();
        // Those after everything, and any of what nothing wrote, last.
        commentsAtEnd(nullptr);
        for (Comment& c : mComments)
        {
            writeComment(c);
        }
        // Each of its lines a comment, whatever language the studio says
        // it in.
        const std::string head = mOptions.comments
                                     ? said("SluaHeader", "Written from LSL by Script Studio. Each \"-- LSL:\" comment marks a place where\n"
                                                          "SLua means something else than LSL did, or has a way of its own: read it, then\n"
                                                          "delete it. LSL's integers wrapped at 32 bits and its floats were single\n"
                                                          "precision; SLua's numbers are doubles, which do neither.")
                                     : said("SluaHeaderPlain", "Written from LSL by Script Studio. LSL's integers wrapped at 32 bits and its\n"
                                                               "floats were single precision; SLua's numbers are doubles, which do neither.");
        std::string out;
        for (size_t from = 0; from <= head.size();)
        {
            const size_t cut = std::min(head.find('\n', from), head.size());
            out += "-- " + head.substr(from, cut - from) + "\n";
            from = cut + 1;
        }
        out += "\n";
        // The script's own first comments, over everything: what it is, and
        // whose.
        if (!mLeadComments.empty())
        {
            for (size_t k : mLeadComments)
            {
                out += mComments[k].text + "\n";
            }
            out += "\n";
        }
        helpers(out);
        // The head over what was written: the SLua's lines counted from the
        // top.
        endSpans();
        const S32 above = static_cast<S32>(std::count(out.begin(), out.end(), '\n'));
        for (ALLSLToSLua::Span& span : mSpans)
        {
            span.sluaFirst += above;
            span.sluaLast += above;
        }
        return leveled(out + mText);
    }

    std::string Writer::leveled(const std::string& text) const
    {
        if (!mToggleMark || text.find(mToggleMark) == std::string::npos)
        {
            return text;
        }
        // Each mark, an opener's after its [ and a closer's after its ]: the
        // openers since one closer the next one's, all at the level of the
        // fewest =s that nothing from the first of them to it closes, which
        // is what a - taken out of any of them comments out.
        std::vector<std::pair<size_t, size_t>> levels;
        std::vector<size_t>                    opens;
        for (size_t at = text.find(mToggleMark); at != std::string::npos; at = text.find(mToggleMark, at + 1))
        {
            if (at > 0 && text[at - 1] == '[')
            {
                opens.push_back(at);
                continue;
            }
            std::string held;
            if (!opens.empty())
            {
                held = text.substr(opens.front() + 2, at - 1 - (opens.front() + 2));
                std::erase(held, mToggleMark);
            }
            std::string level;
            while (held.find("]" + level + "]") != std::string::npos)
            {
                level += "=";
            }
            for (size_t opener : opens)
            {
                levels.emplace_back(opener, level.size());
            }
            levels.emplace_back(at, level.size());
            opens.clear();
        }
        // Any written after their closer, as they stand.
        for (size_t opener : opens)
        {
            levels.emplace_back(opener, 0);
        }
        std::string out;
        size_t      from = 0;
        for (const auto& [at, n] : levels)
        {
            out.append(text, from, at - from);
            out.append(n, '=');
            from = at + 1;
        }
        out.append(text, from, std::string::npos);
        return out;
    }

    void Writer::endSpans()
    {
        // Each stretch's SLua runs to where the next written that is not
        // inside it (by its LSL) begins, or to the end; less the blank
        // lines and comments before that, which are the next's -- its notes,
        // the comments over it -- or nobody's, and the lines written less
        // deep than its first, which close the blocks it is in.
        std::vector<std::string_view> lines;
        for (size_t from = 0; from < mText.size();)
        {
            const size_t cut = std::min(mText.find('\n', from), mText.size());
            lines.push_back(std::string_view(mText).substr(from, cut - from));
            from = cut + 1;
        }
        const auto depth = [&](S32 line) { return lines[static_cast<size_t>(line)].find_first_not_of(" \t"); };
        const auto spoken = [&](S32 line) {
            const std::string_view text  = lines[static_cast<size_t>(line)];
            const size_t           first = text.find_first_not_of(" \t");
            return first != std::string_view::npos && text.substr(first, 2) != "--";
        };
        const auto written = [&](S32 line) { return line < static_cast<S32>(lines.size()) ? depth(line) : 0; };
        for (size_t i = 0; i < mSpans.size(); ++i)
        {
            ALLSLToSLua::Span& span = mSpans[i];
            S32                end  = static_cast<S32>(lines.size()) - 1;
            const size_t       own  = written(span.sluaFirst);
            for (size_t j = i + 1; j < mSpans.size(); ++j)
            {
                const ALLSLToSLua::Span& next = mSpans[j];
                // Statements sharing an LSL line are side by side, not one
                // in the other: told apart by the SLua, the inner written
                // deeper.
                const bool beside = next.lslFirst == span.lslFirst && next.lslLast == span.lslLast && written(next.sluaFirst) <= own;
                if (next.lslFirst < span.lslFirst || next.lslLast > span.lslLast || beside)
                {
                    end = next.sluaFirst - 1;
                    break;
                }
            }
            end = std::min(end, static_cast<S32>(lines.size()) - 1);
            while (end > span.sluaFirst && (!spoken(end) || depth(end) < own))
            {
                --end;
            }
            span.sluaLast = std::max(span.sluaFirst, end);
        }
    }
}

// static
const char* ALLSLToSLua::lintOf(std::string_view note)
{
    for (const Linted& each : LINTED)
    {
        if (note == each.key)
        {
            return each.lint;
        }
    }
    return nullptr;
}

// static
ALScriptProblems ALLSLToSLua::notesIn(std::string_view slua, const Words& words)
{
    // Each linted note's words, as the studio says them and as English does:
    // a script may have been written in either.
    std::vector<std::pair<std::string, const char*>> patterns;
    for (const Linted& each : LINTED)
    {
        if (words)
        {
            patterns.emplace_back(words(each.key, {}, each.english), each.lint);
        }
        patterns.emplace_back(each.english, each.lint);
    }
    ALScriptProblems out;
    S32              line = 0;
    for (size_t start = 0; start <= slua.size(); ++line)
    {
        const size_t     end  = std::min(slua.find('\n', start), slua.size());
        std::string_view text = slua.substr(start, end - start);
        if (!text.empty() && text.back() == '\r')
        {
            text.remove_suffix(1);
        }
        const size_t at = text.find_first_not_of(" \t");
        if (at != std::string_view::npos && text.substr(at, NOTE_MARK.size()) == NOTE_MARK)
        {
            std::string_view said = text.substr(at + NOTE_MARK.size());
            said.remove_prefix(std::min(said.size(), said.find_first_not_of(' ')));
            ALScriptProblem note;
            note.severity  = ALScriptProblem::Severity::Note;
            note.source    = ALScriptProblem::Source::Assistant;
            note.key       = "SluaNote";
            note.line      = line;
            note.column    = static_cast<S32>(at);
            note.endLine   = line;
            note.endColumn = static_cast<S32>(text.size());
            note.message   = std::string(said);
            note.args      = { note.message };
            for (const auto& [pattern, lint] : patterns)
            {
                if (saidAs(pattern, said))
                {
                    note.code = lint;
                    break;
                }
            }
            // Done: the comment taken out, and its line with it, which
            // holds nothing else. Safe, but never on a save, nor what a Fix
            // All of the whole script takes: a note is to be read.
            ALScriptFix done = ALScriptFixes::titled("ScriptFixNoteDone", "Done: take the note out", {});
            done.safe        = true;
            done.removes     = true;
            done.edits.push_back(end < slua.size() ? ALScriptEdit(line, 0, line + 1, 0, std::string())
                                                   : ALScriptEdit(line, 0, line, static_cast<S32>(text.size()), std::string()));
            note.fixes.push_back(std::move(done));
            out.push_back(std::move(note));
        }
        if (end == slua.size())
        {
            break;
        }
        start = end + 1;
    }
    return out;
}

// static
void ALLSLToSLua::linkNotes(ALScriptProblems& notes, const ALScriptProblems& found, std::string_view slua)
{
    std::vector<std::string_view> lines;
    for (size_t start = 0;;)
    {
        const size_t end = std::min(slua.find('\n', start), slua.size());
        lines.push_back(slua.substr(start, end - start));
        if (end == slua.size())
        {
            break;
        }
        start = end + 1;
    }
    const auto spoken = [&](S32 line) {
        const std::string_view text = lines[line];
        const size_t           at   = text.find_first_not_of(" \t\r");
        return at == std::string_view::npos || text.substr(at, NOTE_MARK.size()) == NOTE_MARK;
    };
    for (ALScriptProblem& note : notes)
    {
        if (note.code.empty() || note.fixes.empty())
        {
            continue;
        }
        // The line it stands over: the first after it neither blank nor
        // another note.
        S32 target = note.line + 1;
        while (target < static_cast<S32>(lines.size()) && spoken(target))
        {
            ++target;
        }
        for (const ALScriptProblem& problem : found)
        {
            if (problem.code != note.code || !problem.file.empty() || problem.line > target || std::max(problem.line, problem.endLine) < target)
            {
                continue;
            }
            const auto fix = std::find_if(problem.fixes.begin(), problem.fixes.end(),
                                          [](const ALScriptFix& each) { return each.preferred && each.kind == ALScriptFix::Kind::Fix; });
            if (fix == problem.fixes.end())
            {
                continue;
            }
            // The lint's fix, and the note taken out with it: offered first.
            ALScriptFix both = ALScriptFixes::titled("ScriptFixNoteLint", "[1], and take the note out", { fix->title });
            both.preferred   = true;
            both.safe        = fix->safe;
            both.edits       = fix->edits;
            both.edits.insert(both.edits.end(), note.fixes.back().edits.begin(), note.fixes.back().edits.end());
            note.fixes.insert(note.fixes.begin(), std::move(both));
            break;
        }
    }
}

// static
ALLSLToSLua::Result ALLSLToSLua::convert(std::string_view lsl, const Options& options)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    Result result;
    if (!ALLSLService::builtinsLoaded())
    {
        ALScriptProblem p;
        p.severity = ALScriptProblem::Severity::Error;
        p.source   = ALScriptProblem::Source::Assistant;
        p.key      = "AssistantNoDefinitions";
        p.message  = "the LSL definitions are not loaded, so nothing was written";
        result.problems.push_back(std::move(p));
        return result;
    }
    // Tailslide's walks and the writer recurse on how the script nests,
    // which LSL lets go deeper than the stack a thread is given holds.
    alScriptOnLargeStack([&]() {
        AL_SCRIPT_ENGINE_HELD;
        ScopedScriptParser parser(nullptr);
        LSLScript*         script = parser.parseLSLBytes(lsl.data(), static_cast<int>(lsl.size()));
        if (script)
        {
            script->collectSymbols();
            script->determineTypes();
        }
        if (!script || parser.logger.getErrors())
        {
            for (LogMessage* message : parser.logger.getMessages())
            {
                if (message->getType() != LOG_ERROR && message->getType() != LOG_INTERNAL_ERROR)
                {
                    continue;
                }
                ALScriptProblem p;
                p.severity = ALScriptProblem::Severity::Error;
                p.source   = ALScriptProblem::Source::Parser;
                p.line     = zeroBased(message->getLoc()->first_line);
                p.column   = zeroBased(message->getLoc()->first_column);
                p.message  = message->getMessage();
                result.problems.push_back(std::move(p));
            }
            return;
        }
        Writer writer(script, lsl, options);
        result.text      = writer.write();
        result.notes     = std::move(writer.notes());
        result.spans     = writer.spans();
        result.converted = true;
        // What SLua always says otherwise, then what this script's calls
        // and names were written as.
        result.same.assign(std::begin(SAID_OTHERWISE), std::end(SAID_OTHERWISE));
        result.same.insert(result.same.end(), writer.same().begin(), writer.same().end());
        result.dropped = { ";" };
    });
    return result;
}

// static
ALLSLToSLua::Result ALLSLToSLua::convert(std::string_view lsl)
{
    return convert(lsl, Options());
}
