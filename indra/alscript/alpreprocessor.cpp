/**
 * @file alpreprocessor.cpp
 * @brief The script preprocessor: Firestorm's dialect for LSL, the plugin's for SLua.
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

#include "alpreprocessor.h"

#include "alscriptfixes.h"
#include "alscriptweight.h"

#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <ctime>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace
{
    // ---- tokens -------------------------------------------------------------

    enum class Kind : U8
    {
        Ident,
        Number,
        String,
        Punct,
        Space,
        Newline,
        Comment,
        // A byte the language has no use for, passed through.
        Other,
        // What an empty argument leaves beside `##`, and nothing else.
        Placemarker,
        // A `##` of a macro body, as told from one an argument brought.
        Paste
    };

    typedef boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> HideSet;
    typedef std::shared_ptr<const HideSet>                                          hide_set_ptr;

    struct Token
    {
        Kind        kind = Kind::Other;
        std::string text;
        // Where it came from: a file of the map, and a place in it.
        S32         file     = 0;
        S32         line     = 0;
        S32         column   = 0;
        // The text is the file's own at that place, rather than what a
        // macro made there.
        bool        verbatim = true;
        // The macros this token will not expand as: Prosser's hide set,
        // shared by the tokens one expansion made.
        hide_set_ptr hide;

        bool is(Kind k, std::string_view t) const { return kind == k && text == t; }
        bool blank() const { return kind == Kind::Space || kind == Kind::Newline || kind == Kind::Comment; }
        bool hidden(const std::string& name) const { return hide && hide->count(name); }
    };

    typedef std::vector<Token> Tokens;

    hide_set_ptr hideUnion(const hide_set_ptr& a, const HideSet& b)
    {
        if (b.empty())
        {
            return a;
        }
        auto set = std::make_shared<HideSet>(b);
        if (a)
        {
            set->insert(a->begin(), a->end());
        }
        return set;
    }

    // The union of a token's own hide set with an expansion's, where
    // most tokens have none of their own and take the expansion's as it
    // is: one set shared by the tokens one expansion made, as Prosser
    // has it, rather than a copy of it per token.
    hide_set_ptr hideWith(const hide_set_ptr& own, const hide_set_ptr& theirs)
    {
        if (!own || own == theirs)
        {
            return theirs;
        }
        if (!theirs)
        {
            return own;
        }
        auto set = std::make_shared<HideSet>(*theirs);
        set->insert(own->begin(), own->end());
        return set;
    }

    hide_set_ptr hideIntersect(const hide_set_ptr& a, const hide_set_ptr& b)
    {
        if (!a || !b)
        {
            return hide_set_ptr();
        }
        auto set = std::make_shared<HideSet>();
        for (const std::string& name : *a)
        {
            if (b->count(name))
            {
                set->insert(name);
            }
        }
        return set;
    }

    // ---- the tokenizers ---------------------------------------------------------

    bool isIdentStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    bool isDigit(char c) { return c >= '0' && c <= '9'; }
    bool isIdentChar(char c) { return isIdentStart(c) || isDigit(c); }
    bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }

    // Longest first, so that the first match is the longest.
    // &= |= ^= are not LSL's, but the extensions transform takes them
    // and plain LSL has no use for an & before an =.
    const char* const LSL_PUNCT[] = { "<<=", ">>=", "...", "##", "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
                                      "==",  "!=",  "<=",  ">=", "&&", "||", "<<", ">>", nullptr };
    const char* const LUA_PUNCT[] = { "...", "//=", "..=", "##", "..", "//", "::", "->", "+=", "-=", "*=", "/=", "%=", "^=",
                                      "==",  "~=",  "<=",  ">=", nullptr };

    // Whether two punctuators written together would lex as something
    // longer than the first: what compression needs a space between.
    bool punctJoins(bool lua, std::string_view a, std::string_view b)
    {
        const std::string joined = std::string(a) + std::string(b);
        for (const char* const* p = lua ? LUA_PUNCT : LSL_PUNCT; *p; ++p)
        {
            const std::string_view op = *p;
            if (op.size() > a.size() && joined.compare(0, op.size(), op) == 0)
            {
                return true;
            }
        }
        return false;
    }

    class Lexer
    {
    public:
        // Verbatim: every byte kept as written, for a reader that is not
        // the preprocessor -- no lines joined, no string rewritten.
        Lexer(bool lua, S32 file, bool verbatim = false) : mLua(lua), mFile(file), mVerbatim(verbatim) {}

        Tokens run(std::string_view text)
        {
            mText   = text;
            mPos    = 0;
            mLine   = 0;
            mColumn = 0;
            mJoined = 0;
            mOut.clear();
            while (mPos < mText.size())
            {
                if (mLua)
                {
                    luaToken();
                }
                else
                {
                    lslToken();
                }
            }
            return std::move(mOut);
        }

    private:
        char at(size_t i) const { return i < mText.size() ? mText[i] : '\0'; }

        void start(Kind kind)
        {
            mToken.kind     = kind;
            mToken.text.clear();
            mToken.file     = mFile;
            mToken.line     = mLine;
            mToken.column   = mColumn;
            mToken.verbatim = true;
            mToken.hide.reset();
        }

        // Takes one byte into the token, keeping the place.
        void take()
        {
            const char c = mText[mPos++];
            mToken.text += c;
            if (c == '\n')
            {
                ++mLine;
                mColumn = 0;
            }
            else
            {
                ++mColumn;
            }
        }

        void finish() { mOut.push_back(mToken); }

        // A backslash and a newline join two lines; both vanish, and the
        // newline that ends the joined line stands for them all, so that
        // the lines after keep their numbers.
        bool continuation()
        {
            if (!mVerbatim && at(mPos) == '\\' && at(mPos + 1) == '\n')
            {
                mPos += 2;
                ++mLine;
                mColumn = 0;
                ++mJoined;
                return true;
            }
            return false;
        }

        void common()
        {
            const char c = at(mPos);
            if (c == '\n')
            {
                start(Kind::Newline);
                take();
                mToken.text.append(mJoined, '\n');
                mJoined = 0;
                finish();
            }
            else if (isBlank(c))
            {
                start(Kind::Space);
                while (isBlank(at(mPos)))
                {
                    take();
                }
                finish();
            }
            else if (isDigit(c) || (c == '.' && isDigit(at(mPos + 1))))
            {
                number();
            }
            else if (isIdentStart(c))
            {
                start(Kind::Ident);
                while (isIdentChar(at(mPos)))
                {
                    take();
                }
                finish();
            }
            else
            {
                punct();
            }
        }

        void number()
        {
            // A preprocessing number: what C calls one, which is wider
            // than either language's and holds every literal of both.
            start(Kind::Number);
            take();
            while (mPos < mText.size())
            {
                const char c = at(mPos);
                if ((c == '+' || c == '-') && (mToken.text.back() == 'e' || mToken.text.back() == 'E' ||
                                               (mLua && (mToken.text.back() == 'p' || mToken.text.back() == 'P'))))
                {
                    take();
                }
                else if (isIdentChar(c) || c == '.')
                {
                    take();
                }
                else
                {
                    break;
                }
            }
            finish();
        }

        void punct()
        {
            start(Kind::Punct);
            for (const char* const* p = mLua ? LUA_PUNCT : LSL_PUNCT; *p; ++p)
            {
                const std::string_view op = *p;
                if (mText.compare(mPos, op.size(), op) == 0)
                {
                    for (size_t i = 0; i < op.size(); ++i)
                    {
                        take();
                    }
                    finish();
                    return;
                }
            }
            const char c = at(mPos);
            if (static_cast<unsigned char>(c) >= 0x80 || c < 0x20 || c == '`' || c == '$' || c == '\'' || c == '\\')
            {
                mToken.kind = Kind::Other;
            }
            take();
            // The rest of a multibyte character stays with its first byte.
            while (mToken.kind == Kind::Other && (static_cast<unsigned char>(at(mPos)) & 0xC0) == 0x80)
            {
                take();
            }
            finish();
        }

        // -- LSL --

        void lslToken()
        {
            if (continuation())
            {
                return;
            }
            const char c = at(mPos);
            if (c == '/' && at(mPos + 1) == '/')
            {
                start(Kind::Comment);
                while (mPos < mText.size() && at(mPos) != '\n')
                {
                    take();
                }
                finish();
            }
            else if (c == '/' && at(mPos + 1) == '*')
            {
                start(Kind::Comment);
                take();
                take();
                while (mPos < mText.size() && !(at(mPos) == '*' && at(mPos + 1) == '/'))
                {
                    take();
                }
                if (mPos < mText.size())
                {
                    take();
                    take();
                }
                finish();
            }
            else if (c == '"')
            {
                lslString();
            }
            else
            {
                common();
            }
        }

        // A string with newlines inside becomes one line with `\n` where
        // they were, and the newlines follow it, so that the lines keep
        // their numbers: Firestorm's quirk, kept because scripts have
        // come to rely on it.
        void lslString()
        {
            start(Kind::String);
            take();
            S32 newlines = 0;
            while (mPos < mText.size())
            {
                const char c = at(mPos);
                if (mVerbatim)
                {
                    if (c == '\\' && mPos + 1 < mText.size())
                    {
                        take();
                    }
                    take();
                    if (c == '"')
                    {
                        break;
                    }
                    continue;
                }
                if (c == '\\' && mPos + 1 < mText.size())
                {
                    take();
                    if (at(mPos) == '\n')
                    {
                        ++newlines;
                        mToken.text.back() = '\\';
                        mToken.text += 'n';
                        ++mPos;
                        ++mLine;
                        mColumn = 0;
                        continue;
                    }
                    take();
                }
                else if (c == '\n')
                {
                    ++newlines;
                    mToken.text += "\\n";
                    ++mPos;
                    ++mLine;
                    mColumn = 0;
                }
                else if (c == '"')
                {
                    take();
                    break;
                }
                else
                {
                    take();
                }
            }
            finish();
            for (S32 i = 0; i < newlines; ++i)
            {
                Token nl;
                nl.kind     = Kind::Newline;
                nl.text     = "\n";
                nl.file     = mFile;
                nl.line     = mLine - 1;
                nl.column   = 0;
                nl.verbatim = false;
                mOut.push_back(nl);
            }
        }

        // -- Luau --

        // The level of a long bracket opening at `pos`, or -1.
        S32 longBracket(size_t pos) const
        {
            if (at(pos) != '[')
            {
                return -1;
            }
            size_t i = pos + 1;
            while (at(i) == '=')
            {
                ++i;
            }
            return at(i) == '[' ? S32(i - pos - 1) : -1;
        }

        void takeLong(S32 level)
        {
            // Past the opening bracket, to the closing one of the same level.
            for (S32 i = 0; i < level + 2; ++i)
            {
                take();
            }
            while (mPos < mText.size())
            {
                if (at(mPos) == ']')
                {
                    size_t i = mPos + 1;
                    S32    n = 0;
                    while (at(i) == '=')
                    {
                        ++i;
                        ++n;
                    }
                    if (n == level && at(i) == ']')
                    {
                        for (S32 k = 0; k < level + 2; ++k)
                        {
                            take();
                        }
                        return;
                    }
                }
                take();
            }
        }

        void luaToken()
        {
            if (continuation())
            {
                return;
            }
            const char c = at(mPos);
            if (c == '-' && at(mPos + 1) == '-')
            {
                start(Kind::Comment);
                const S32 level = longBracket(mPos + 2);
                if (level >= 0)
                {
                    take();
                    take();
                    takeLong(level);
                }
                else
                {
                    while (mPos < mText.size() && at(mPos) != '\n')
                    {
                        take();
                    }
                }
                finish();
            }
            else if (c == '[' && longBracket(mPos) >= 0)
            {
                start(Kind::String);
                takeLong(longBracket(mPos));
                finish();
            }
            else if (c == '"' || c == '\'')
            {
                start(Kind::String);
                takeQuoted(c);
                finish();
            }
            else if (c == '`')
            {
                start(Kind::String);
                mInterpolating = 0;
                takeInterpolated();
                finish();
            }
            else
            {
                common();
            }
        }

        // A string in quotes, to its closing quote or the end of its line,
        // where Luau ends one that is not closed.
        void takeQuoted(char quote)
        {
            take();
            while (mPos < mText.size())
            {
                const char d = at(mPos);
                if (d == '\\' && mPos + 1 < mText.size())
                {
                    take();
                    take();
                }
                else if (d == quote)
                {
                    take();
                    return;
                }
                else if (d == '\n')
                {
                    return;
                }
                else
                {
                    take();
                }
            }
        }

        // An interpolated string, to its closing backtick, each `{...}` in
        // it an expression whose own strings -- quoted, long or
        // interpolated -- are taken whole, so that a brace or a backtick
        // inside one ends nothing. Nested past a sensible depth, the rest
        // is taken as it comes.
        void takeInterpolated()
        {
            ++mInterpolating;
            take();
            while (mPos < mText.size())
            {
                const char d = at(mPos);
                if (d == '\\' && mPos + 1 < mText.size())
                {
                    take();
                    take();
                }
                else if (d == '`')
                {
                    take();
                    break;
                }
                else if (d == '{')
                {
                    take();
                    takeInterpolation();
                }
                else
                {
                    take();
                }
            }
            --mInterpolating;
        }

        void takeInterpolation()
        {
            constexpr S32 MOST_NESTED = 32;
            S32           depth       = 0;
            while (mPos < mText.size())
            {
                const char d = at(mPos);
                if ((d == '"' || d == '\'') && mInterpolating < MOST_NESTED)
                {
                    takeQuoted(d);
                }
                else if (d == '`' && mInterpolating < MOST_NESTED)
                {
                    takeInterpolated();
                }
                else if (d == '[' && longBracket(mPos) >= 0 && mInterpolating < MOST_NESTED)
                {
                    takeLong(longBracket(mPos));
                }
                else if (d == '{')
                {
                    ++depth;
                    take();
                }
                else if (d == '}')
                {
                    take();
                    if (depth-- == 0)
                    {
                        return;
                    }
                }
                else
                {
                    take();
                }
            }
        }

        bool             mLua;
        S32              mFile;
        bool             mVerbatim;
        S32              mInterpolating = 0;
        std::string_view mText;
        size_t           mPos    = 0;
        S32              mLine   = 0;
        S32              mColumn = 0;
        S32              mJoined = 0;
        Token            mToken;
        Tokens           mOut;
    };

    // The text of a run of tokens, as written.
    std::string textOf(const Tokens& tokens, size_t from = 0, size_t to = size_t(-1))
    {
        std::string out;
        to = std::min(to, tokens.size());
        for (size_t i = from; i < to; ++i)
        {
            out += tokens[i].text;
        }
        return out;
    }

    std::string trim(std::string s)
    {
        while (!s.empty() && (isBlank(s.back()) || s.back() == '\n'))
        {
            s.pop_back();
        }
        size_t i = 0;
        while (i < s.size() && (isBlank(s[i]) || s[i] == '\n'))
        {
            ++i;
        }
        return s.substr(i);
    }

    // Text as a string literal both languages read back as the text: a
    // name -- a script's, a file's, an agent's -- may hold a quote or a
    // backslash, and a Windows path holds nothing but.
    std::string literalOf(std::string_view text)
    {
        std::string out = "\"";
        for (const char c : text)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:   out += c; break;
            }
        }
        out += '"';
        return out;
    }

    size_t skipBlank(const Tokens& tokens, size_t i)
    {
        while (i < tokens.size() && tokens[i].blank())
        {
            ++i;
        }
        return i;
    }

    // Blanks within the line only.
    size_t skipSpace(const Tokens& tokens, size_t i)
    {
        while (i < tokens.size() && (tokens[i].kind == Kind::Space || tokens[i].kind == Kind::Comment))
        {
            ++i;
        }
        return i;
    }

    // ---- the engine ------------------------------------------------------------

    struct Macro
    {
        // The few whose value is where they stand rather than a body.
        enum class Dynamic : U8
        {
            None,
            Line,
            File,
            AssetId,
            ShortFile
        };
        std::string              name;
        bool                     functionLike = false;
        bool                     variadic     = false;
        std::vector<std::string> params;
        Tokens                   body;
        Dynamic                  dynamic = Dynamic::None;

        bool sameAs(const Macro& o) const
        {
            if (functionLike != o.functionLike || variadic != o.variadic || params != o.params || body.size() != o.body.size())
            {
                return false;
            }
            for (size_t i = 0; i < body.size(); ++i)
            {
                if (body[i].kind != o.body[i].kind || (body[i].kind != Kind::Space && body[i].text != o.body[i].text))
                {
                    return false;
                }
            }
            return true;
        }
    };

    struct Cond
    {
        // Whether this group's lines are kept; whether some group of this
        // conditional has been taken, so a later one is not; and whether
        // the `#else` has been seen.
        bool active   = false;
        bool taken    = false;
        bool seenElse = false;
    };

    struct FileState
    {
        S32               index = 0;
        std::string       path;
        std::string       name;
        std::string       assetId;
        Tokens            tokens;
        size_t            pos       = 0;
        // Nothing but blanks since the last newline: where a directive
        // may stand.
        bool              lineStart = true;
        std::vector<Cond> conds;

        bool active() const { return conds.empty() || conds.back().active; }
    };

    class Engine
    {
    public:
        Engine(const ALPreprocessor::Options& options, ALPreprocessor::Result& result) : mOptions(options), mResult(result)
        {
            mSink = &mOut;
        }

        // The output tokens of the script, with its includes expanded in
        // place.
        Tokens run(std::string_view source);

        // The output tokens of a module of its own, for a `require`:
        // processed with the macros as they stand, into a list of its
        // own. Nothing for a file the resolver did not give.
        bool module(const ALPreprocessor::Include& include, Tokens& out);

        bool usedSwitches() const { return mUsedSwitches; }
        bool usedLazyLists() const { return mUsedLazyLists; }
        bool usedExtensions() const { return mUsedExtensions; }
        void problem(ALScriptProblem::Severity severity, const std::string& message, const Token& at);
        // The same with a key a translation may be found under, and the
        // words -- [1], [2] ... in the text -- it is built from.
        void problem(ALScriptProblem::Severity severity, const char* key, std::string_view text, std::vector<std::string> args, const Token& at);

        // A macro of the run's own: `NAME body` or `NAME(params) body` as
        // a define line would have it, or one whose value is where it
        // stands.
        void predefine(const std::string& definition);
        void predefine(const std::string& name, Macro::Dynamic dynamic);

        // The run stopped where a bound stops it: said once, with what
        // was said of the source, and nothing of the run compiled or
        // analysed.
        void overrun(const char* key, std::string_view text, const Token& at);
        // Whether a transform that descends into what the script nests --
        // a block, a loop's body, a switch in a switch, each a frame of
        // the machine's stack -- may go `depth` levels down; past the
        // bound, the run is stopped.
        bool nest(S32 depth, const Token& at);

    private:
        // -- files --
        void pushFile(std::string_view text, const std::string& path, const std::string& name, const std::string& assetId);
        void popFile();
        void loop(size_t depth);
        bool directiveAhead(const FileState& f) const;

        // -- tokens --
        bool next(Token& t);
        void unread(const Tokens& tokens);
        void emit(const Token& t) { mSink->push_back(t); }
        // What a run may make, against the budget: every token put back
        // to be scanned again and every token given out is one more, and
        // its text so many bytes more. A run that reaches the budget says
        // so once and stops, since hide sets stop a macro expanding as
        // itself but not one that doubles.
        bool spend(size_t made, size_t bytes, const Token& at);
        bool spend(const Tokens& made, const Token& at) { return spend(made.size(), bytesOf(made), at); }
        static size_t bytesOf(const Tokens& tokens)
        {
            size_t bytes = 0;
            for (const Token& t : tokens)
            {
                bytes += t.text.size();
            }
            return bytes;
        }
        // A token as long as a whole script, made by pasting or
        // stringizing: the run stops there, before it is made.
        bool tooLong(size_t bytes, const Token& at);
        bool overran() const { return mOverran; }

        // -- expansion --
        void   handle(const Token& t);
        bool   expandDynamic(const Token& t, const Macro& m);
        bool   expandObject(const Token& t, const Macro& m);
        bool   expandFunction(const Token& t, const Macro& m);
        Tokens substitute(const Macro& m, const std::vector<Tokens>& args, const Token& site, const hide_set_ptr& hs);
        Tokens expandAll(const Tokens& in);
        Token  stringize(const Tokens& arg, const Token& site);
        bool   paste(const Token& left, const Token& right, const Token& site, Token& out);
        static S32 paramIndex(const Macro& m, const Token& t);

        // -- directives --
        Tokens readLine(FileState& f, Token& newline);
        void   directive(FileState& f);
        void   define(const Tokens& line, size_t at, const Token& hash);
        void   include(const Tokens& line, size_t at, const Token& hash);
        bool   evalCondition(const Tokens& line, size_t at, const Token& hash);
        S64    evalExpression(const Tokens& tokens, const Token& hash, bool& ok);

        const ALPreprocessor::Options& mOptions;
        ALPreprocessor::Result&        mResult;
        std::vector<std::unique_ptr<FileState>> mFiles;
        // Looked up for every identifier of the whole script, so flat.
        boost::unordered_flat_map<std::string, Macro, ll::string_hash, std::equal_to<>> mMacros;
        boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>        mOnce;
        boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>        mPendingNames;
        boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>        mIncluded;
        // What the run has made so far, against the budget.
        size_t                                  mMade    = 0;
        size_t                                  mBytes   = 0;
        bool                                    mOverran = false;
        // How many times the loop has gone round, which a look at whether
        // the run is still wanted is made every so many of.
        size_t                                  mRounds  = 0;
        // How deep argument expansion has gone.
        S32                                     mExpandDepth = 0;
        // Tokens to read before the file: what an expansion made, to be
        // scanned again, and what a look-ahead gave back.
        std::deque<Token>                       mPending;
        // Reading a list of tokens on its own, with no file behind it.
        bool                                    mIsolated = false;
        Tokens                                  mOut;
        Tokens*                                 mSink;
        bool                                    mUsedSwitches  = false;
        bool                                    mUsedExtensions = false;
        bool                                    mUsedLazyLists = false;
    };

    void Engine::problem(ALScriptProblem::Severity severity, const std::string& message, const Token& at)
    {
        ALScriptProblem p;
        p.severity  = severity;
        p.source    = ALScriptProblem::Source::Preprocessor;
        p.line      = at.line;
        p.column    = at.column;
        p.endLine   = at.line;
        p.endColumn = at.column + S32(at.text.size());
        p.message   = message;
        if (at.file > 0 && at.file < S32(mResult.map.files().size()))
        {
            p.file = mResult.map.files()[at.file].path;
        }
        mResult.problems.push_back(p);
    }

    void Engine::problem(ALScriptProblem::Severity severity, const char* key, std::string_view text, std::vector<std::string> args, const Token& at)
    {
        problem(severity, ALScriptProblem::fill(text, args), at);
        mResult.problems.back().key  = key;
        mResult.problems.back().args = std::move(args);
    }

    void Engine::predefine(const std::string& definition)
    {
        const Tokens line = Lexer(mOptions.lua, 0).run(definition);
        Token        hash;
        define(line, 0, hash);
    }

    void Engine::predefine(const std::string& name, Macro::Dynamic dynamic)
    {
        Macro m;
        m.name         = name;
        m.dynamic      = dynamic;
        mMacros[name]  = m;
    }

    // -- files --

    void Engine::pushFile(std::string_view text, const std::string& path, const std::string& name, const std::string& assetId)
    {
        auto f     = std::make_unique<FileState>();
        f->index   = mResult.map.addFile(name, path);
        f->path    = path;
        f->name    = name;
        f->assetId = assetId;
        f->tokens  = Lexer(mOptions.lua, f->index).run(text);
        // A file opened is tokens made, against the budget like any
        // other: a file that includes itself twice, as many levels down as
        // the include depth allows, is more files than there are.
        Token at;
        at.file = mFiles.empty() ? 0 : mFiles.back()->index;
        if (!mFiles.empty() && !mFiles.back()->tokens.empty())
        {
            const FileState& asking = *mFiles.back();
            at = asking.tokens[std::min(asking.pos, asking.tokens.size()) - (asking.pos > 0 ? 1 : 0)];
        }
        if (!spend(f->tokens.size(), text.size(), at))
        {
            return;
        }
        mFiles.push_back(std::move(f));
    }

    void Engine::popFile()
    {
        FileState& f = *mFiles.back();
        if (!f.conds.empty())
        {
            Token at;
            at.file = f.index;
            at.line = f.tokens.empty() ? 0 : f.tokens.back().line;
            problem(ALScriptProblem::Severity::Error, "PreprocIfWithoutEndif", "#if without #endif at the end of the file", {}, at);
        }
        mFiles.pop_back();
    }

    bool Engine::directiveAhead(const FileState& f) const
    {
        const size_t i = skipSpace(f.tokens, f.pos);
        return i < f.tokens.size() && f.tokens[i].is(Kind::Punct, "#");
    }

    void Engine::loop(size_t depth)
    {
        while (mFiles.size() > depth && !mOverran)
        {
            // No longer wanted: stopped where it stands, and said so.
            if (mOptions.superseded && (mRounds++ & 1023) == 0 && mOptions.superseded->load(std::memory_order_relaxed))
            {
                mOverran           = true;
                mResult.overran    = true;
                mResult.superseded = true;
                break;
            }
            if (!mPending.empty())
            {
                Token t = mPending.front();
                mPending.pop_front();
                handle(t);
                continue;
            }
            FileState& f = *mFiles.back();
            if (f.lineStart && directiveAhead(f))
            {
                directive(f);
                continue;
            }
            Token t;
            if (!next(t))
            {
                popFile();
                continue;
            }
            if (!f.active())
            {
                if (t.kind == Kind::Newline)
                {
                    emit(t);
                }
                continue;
            }
            handle(t);
        }
    }

    Tokens Engine::run(std::string_view source)
    {
        pushFile(source, std::string(), mOptions.fileName, mOptions.assetId);
        loop(0);
        return std::move(mOut);
    }

    bool Engine::module(const ALPreprocessor::Include& include, Tokens& out)
    {
        Tokens* sink = mSink;
        mSink        = &out;
        const size_t depth = mFiles.size();
        pushFile(include.text, include.path, include.name, include.assetId);
        loop(depth);
        mSink = sink;
        return true;
    }

    // -- tokens --

    bool Engine::next(Token& t)
    {
        if (!mPending.empty())
        {
            t = mPending.front();
            mPending.pop_front();
            return true;
        }
        if (mIsolated || mFiles.empty())
        {
            return false;
        }
        FileState& f = *mFiles.back();
        if (f.pos >= f.tokens.size())
        {
            return false;
        }
        t = f.tokens[f.pos++];
        if (t.kind == Kind::Newline)
        {
            f.lineStart = true;
        }
        else if (!t.blank())
        {
            f.lineStart = false;
        }
        return true;
    }

    void Engine::unread(const Tokens& tokens)
    {
        mPending.insert(mPending.begin(), tokens.begin(), tokens.end());
    }

    bool Engine::spend(size_t made, size_t bytes, const Token& at)
    {
        if (mOverran)
        {
            return false;
        }
        mMade += made;
        mBytes += bytes;
        if (mMade <= mOptions.tokenBudget && mBytes <= mOptions.byteBudget)
        {
            return true;
        }
        overrun("PreprocTooMuch", "the macros expand to more than this preprocessor will make; nothing was preprocessed", at);
        return false;
    }

    bool Engine::tooLong(size_t bytes, const Token& at)
    {
        if (bytes <= ALScriptEnvelope::MAX_ASSET_BYTES)
        {
            return false;
        }
        overrun("PreprocTokenTooLong", "the macros make a token longer than a script may be; nothing was preprocessed", at);
        return true;
    }

    void Engine::overrun(const char* key, std::string_view text, const Token& at)
    {
        if (mOverran)
        {
            return;
        }
        mOverran        = true;
        mResult.overran = true;
        problem(ALScriptProblem::Severity::Error, key, text, {}, at);
    }

    bool Engine::nest(S32 depth, const Token& at)
    {
        if (depth <= mOptions.nestingDepth)
        {
            return true;
        }
        overrun("PreprocNestsTooDeep", "blocks nest more deeply than this preprocessor follows; nothing was preprocessed", at);
        return false;
    }

    // -- expansion --

    void Engine::handle(const Token& t)
    {
        if (t.kind == Kind::Ident && !t.hidden(t.text))
        {
            auto it = mMacros.find(t.text);
            if (it != mMacros.end())
            {
                const Macro& m = it->second;
                if (m.dynamic != Macro::Dynamic::None ? expandDynamic(t, m) : m.functionLike ? expandFunction(t, m) : expandObject(t, m))
                {
                    return;
                }
            }
        }
        emit(t);
    }

    S32 Engine::paramIndex(const Macro& m, const Token& t)
    {
        if (t.kind != Kind::Ident)
        {
            return -1;
        }
        for (size_t i = 0; i < m.params.size(); ++i)
        {
            if (m.params[i] == t.text)
            {
                return S32(i);
            }
        }
        return -1;
    }

    bool Engine::expandDynamic(const Token& t, const Macro& m)
    {
        Token out = t;
        out.verbatim = false;
        out.hide     = hideUnion(t.hide, { m.name });
        const FileState& f = *mFiles.back();
        switch (m.dynamic)
        {
            case Macro::Dynamic::Line:
                out.kind = Kind::Number;
                out.text = std::to_string(t.line + 1);
                break;
            case Macro::Dynamic::File:
            case Macro::Dynamic::ShortFile:
                out.kind = Kind::String;
                out.text = literalOf(f.name);
                break;
            case Macro::Dynamic::AssetId:
                out.kind = Kind::String;
                out.text = literalOf(f.assetId.empty() ? std::string("NOT_IN_WORLD") : f.assetId);
                break;
            case Macro::Dynamic::None:
                return false;
        }
        mPending.push_front(out);
        return true;
    }

    bool Engine::expandObject(const Token& t, const Macro& m)
    {
        const hide_set_ptr hs  = hideUnion(t.hide, { m.name });
        Tokens             out = substitute(m, {}, t, hs);
        if (mOverran)
        {
            return true;
        }
        unread(out);
        return true;
    }

    bool Engine::expandFunction(const Token& t, const Macro& m)
    {
        // The `(` may be some blanks and even lines away; without it the
        // name is only a name. A directive on the next line ends the
        // search, since the directive must be read as one.
        Tokens read;
        Token  n;
        while (true)
        {
            if (!next(n))
            {
                unread(read);
                return false;
            }
            read.push_back(n);
            if (!n.blank())
            {
                break;
            }
            if (n.kind == Kind::Newline && mPending.empty() && !mIsolated && directiveAhead(*mFiles.back()))
            {
                unread(read);
                mFiles.back()->lineStart = true;
                return false;
            }
        }
        if (!n.is(Kind::Punct, "("))
        {
            unread(read);
            return false;
        }
        std::vector<Tokens> args;
        Tokens              current;
        S32                 depth  = 0;
        bool                closed = false;
        Token               rparen;
        while (next(n))
        {
            read.push_back(n);
            if (n.is(Kind::Punct, "("))
            {
                ++depth;
            }
            else if (n.is(Kind::Punct, ")"))
            {
                if (depth == 0)
                {
                    closed = true;
                    rparen = n;
                    break;
                }
                --depth;
            }
            else if (n.is(Kind::Punct, ",") && depth == 0 && !(m.variadic && args.size() + 1 >= m.params.size()))
            {
                args.push_back(current);
                current.clear();
                continue;
            }
            if (n.kind == Kind::Newline)
            {
                n.kind = Kind::Space;
                n.text = " ";
            }
            current.push_back(n);
        }
        if (!closed)
        {
            problem(ALScriptProblem::Severity::Error, "PreprocUnterminatedArguments", "unterminated argument list invoking macro '[1]'", { m.name }, t);
            emit(t);
            for (const Token& r : read)
            {
                emit(r);
            }
            return true;
        }
        args.push_back(current);
        // Each argument without the blanks around it; `M()` with no
        // parameters is no arguments.
        for (Tokens& arg : args)
        {
            size_t from = skipBlank(arg, 0);
            size_t to   = arg.size();
            while (to > from && arg[to - 1].blank())
            {
                --to;
            }
            arg = Tokens(arg.begin() + from, arg.begin() + to);
        }
        if (m.params.empty() && args.size() == 1 && args[0].empty())
        {
            args.clear();
        }
        if (m.variadic && args.size() + 1 == m.params.size())
        {
            args.push_back(Tokens());
        }
        if (args.size() != m.params.size())
        {
            problem(ALScriptProblem::Severity::Error, args.size() < m.params.size() ? "PreprocTooFewArguments" : "PreprocTooManyArguments",
                    args.size() < m.params.size() ? "too few arguments for macro '[1]'" : "too many arguments for macro '[1]'", { m.name }, t);
            emit(t);
            for (const Token& r : read)
            {
                emit(r);
            }
            return true;
        }
        const hide_set_ptr hs  = hideUnion(hideIntersect(t.hide, rparen.hide), { m.name });
        Tokens             out = substitute(m, args, t, hs);
        if (mOverran)
        {
            return true;
        }
        unread(out);
        return true;
    }

    Token Engine::stringize(const Tokens& arg, const Token& site)
    {
        std::string text = "\"";
        bool        space = false;
        for (const Token& t : arg)
        {
            // Twice a token's text at most goes in for it, escaped.
            if (tooLong(text.size() + 2 * t.text.size() + 2, site))
            {
                return Token();
            }
            if (t.blank())
            {
                space = true;
                continue;
            }
            if (space && !text.empty() && text.size() > 1)
            {
                text += ' ';
            }
            space = false;
            if (t.kind == Kind::String)
            {
                for (char c : t.text)
                {
                    if (c == '"' || c == '\\')
                    {
                        text += '\\';
                    }
                    text += c;
                }
            }
            else if (t.kind != Kind::Placemarker)
            {
                text += t.text;
            }
        }
        text += '"';
        Token out;
        out.kind     = Kind::String;
        out.text     = text;
        out.file     = site.file;
        out.line     = site.line;
        out.column   = site.column;
        out.verbatim = false;
        return out;
    }

    bool Engine::paste(const Token& left, const Token& right, const Token& site, Token& out)
    {
        if (left.kind == Kind::Placemarker)
        {
            out = right;
            return true;
        }
        if (right.kind == Kind::Placemarker)
        {
            out = left;
            return true;
        }
        if (tooLong(left.text.size() + right.text.size(), site))
        {
            return false;
        }
        Tokens lexed = Lexer(mOptions.lua, site.file).run(left.text + right.text);
        if (lexed.size() != 1 || lexed[0].blank())
        {
            problem(ALScriptProblem::Severity::Error, "PreprocPastingInvalid", "pasting '[1]' and '[2]' does not give a valid token", { left.text, right.text }, site);
            return false;
        }
        out          = lexed[0];
        out.file     = site.file;
        out.line     = site.line;
        out.column   = site.column;
        out.verbatim = false;
        out.hide     = left.hide;
        return true;
    }

    Tokens Engine::substitute(const Macro& m, const std::vector<Tokens>& args, const Token& site, const hide_set_ptr& hs)
    {
        const Tokens&                  body = m.body;
        Tokens                         out;
        std::vector<std::optional<Tokens>> expanded(args.size());
        const auto pasteBeside = [&](size_t i, bool before) {
            size_t j = i;
            if (before)
            {
                while (j > 0 && body[j - 1].blank())
                {
                    --j;
                }
                return j > 0 && body[j - 1].is(Kind::Punct, "##");
            }
            j = skipBlank(body, i + 1);
            return j < body.size() && body[j].is(Kind::Punct, "##");
        };
        for (size_t i = 0; i < body.size(); ++i)
        {
            const Token& b = body[i];
            if (m.functionLike && b.is(Kind::Punct, "#"))
            {
                const size_t j   = skipBlank(body, i + 1);
                const S32    idx = j < body.size() ? paramIndex(m, body[j]) : -1;
                if (idx >= 0)
                {
                    Token made = stringize(args[idx], site);
                    if (mOverran || !spend(1, made.text.size(), site))
                    {
                        return Tokens();
                    }
                    out.push_back(std::move(made));
                    i = j;
                    continue;
                }
            }
            if (b.is(Kind::Punct, "##"))
            {
                Token op    = b;
                op.kind     = Kind::Paste;
                op.file     = site.file;
                op.line     = site.line;
                op.column   = site.column;
                op.verbatim = false;
                out.push_back(op);
                continue;
            }
            const S32 idx = m.functionLike ? paramIndex(m, b) : -1;
            if (idx >= 0)
            {
                if (pasteBeside(i, true) || pasteBeside(i, false))
                {
                    if (args[idx].empty())
                    {
                        Token mark;
                        mark.kind     = Kind::Placemarker;
                        mark.file     = site.file;
                        mark.line     = site.line;
                        mark.column   = site.column;
                        mark.verbatim = false;
                        out.push_back(mark);
                    }
                    else
                    {
                        if (!spend(args[idx], site))
                        {
                            return Tokens();
                        }
                        out.insert(out.end(), args[idx].begin(), args[idx].end());
                    }
                }
                else
                {
                    if (!expanded[idx])
                    {
                        expanded[idx] = expandAll(args[idx]);
                    }
                    // Spent as it goes in, each time it does: a parameter
                    // used many times over is that many copies, which is
                    // what a macro that doubles makes.
                    if (mOverran || !spend(*expanded[idx], site))
                    {
                        return Tokens();
                    }
                    out.insert(out.end(), expanded[idx]->begin(), expanded[idx]->end());
                }
                continue;
            }
            if (!spend(1, b.text.size(), site))
            {
                return Tokens();
            }
            Token copy    = b;
            copy.file     = site.file;
            copy.line     = site.line;
            copy.column   = site.column;
            copy.verbatim = false;
            out.push_back(copy);
        }
        // The pastes, left to right.
        Tokens pasted;
        for (size_t i = 0; i < out.size(); ++i)
        {
            if (out[i].kind != Kind::Paste)
            {
                pasted.push_back(out[i]);
                continue;
            }
            while (!pasted.empty() && pasted.back().blank())
            {
                pasted.pop_back();
            }
            size_t j = skipBlank(out, i + 1);
            if (pasted.empty() || j >= out.size())
            {
                // `##` at an end of the body was refused at the define.
                continue;
            }
            Token joined;
            if (paste(pasted.back(), out[j], site, joined))
            {
                pasted.back() = joined;
            }
            else if (mOverran)
            {
                return Tokens();
            }
            else
            {
                pasted.push_back(out[j]);
            }
            i = j;
        }
        Tokens result;
        for (Token& t : pasted)
        {
            if (t.kind == Kind::Placemarker)
            {
                continue;
            }
                t.hide = hideWith(t.hide, hs);
            result.push_back(t);
        }
        return result;
    }

    Tokens Engine::expandAll(const Tokens& in)
    {
        // A macro's argument expanded before it goes in, and a macro in
        // that argument's own argument before that: each a level of the
        // machine's stack, so `F(F(F(...)))` is bounded as an expression
        // is, and a run that reaches the bound stops.
        struct Level
        {
            S32& depth;
            explicit Level(S32& d) : depth(++d) {}
            ~Level() { --depth; }
        } level(mExpandDepth);
        if (mExpandDepth > mOptions.macroDepth)
        {
            overrun("PreprocMacrosTooDeep", "macros are invoked inside each other's arguments more deeply than this preprocessor follows; nothing was preprocessed",
                    in.empty() ? Token() : in.front());
            return in;
        }
        std::deque<Token> pending;
        pending.swap(mPending);
        const bool isolated = mIsolated;
        mIsolated           = true;
        Tokens  out;
        Tokens* sink = mSink;
        mSink        = &out;
        mPending.assign(in.begin(), in.end());
        Token t;
        while (!mOverran && next(t))
        {
            handle(t);
        }
        mSink     = sink;
        mIsolated = isolated;
        mPending.swap(pending);
        return out;
    }

    // -- directives --

    Tokens Engine::readLine(FileState& f, Token& newline)
    {
        Tokens line;
        while (f.pos < f.tokens.size())
        {
            const Token& t = f.tokens[f.pos++];
            if (t.kind == Kind::Newline)
            {
                newline     = t;
                f.lineStart = true;
                return line;
            }
            line.push_back(t);
        }
        f.lineStart = true;
        return line;
    }

    S64 parseInteger(const std::string& in, bool& ok)
    {
        std::string text = in;
        while (!text.empty() && (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L'))
        {
            text.pop_back();
        }
        S32    base = 10;
        size_t i    = 0;
        if (text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        {
            base = 16;
            i    = 2;
        }
        else if (text.size() > 1 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B'))
        {
            base = 2;
            i    = 2;
        }
        else if (text.size() > 1 && text[0] == '0')
        {
            base = 8;
            i    = 1;
        }
        if (i >= text.size())
        {
            ok = text == "0";
            return 0;
        }
        U64 value = 0;
        for (; i < text.size(); ++i)
        {
            const char c = text[i];
            S32        digit;
            if (c >= '0' && c <= '9')
            {
                digit = c - '0';
            }
            else if (c >= 'a' && c <= 'f')
            {
                digit = 10 + c - 'a';
            }
            else if (c >= 'A' && c <= 'F')
            {
                digit = 10 + c - 'A';
            }
            else
            {
                ok = false;
                return 0;
            }
            if (digit >= base)
            {
                ok = false;
                return 0;
            }
            value = value * U64(base) + U64(digit);
        }
        ok = true;
        return S64(value);
    }

    // A C integer expression over what `#if` was given, once `defined`
    // and the macros are gone: 64-bit, with any name left standing as 0.
    class Expr
    {
    public:
        Expr(const Tokens& tokens, bool lua, Engine& engine, const Token& at, S32 depth)
            : mTokens(tokens), mLua(lua), mEngine(engine), mAt(at), mDepth(std::max(1, depth))
        {
        }

        S64 parse(bool& ok)
        {
            mOk = true;
            if (!peek())
            {
                fail("PreprocIfNeedsExpression", "#if with no expression");
            }
            S64 v = ternary();
            if (mOk && peek())
            {
                fail("PreprocUnexpectedInExpression", "unexpected '[1]' in preprocessor expression", { peek()->text });
            }
            ok = mOk;
            return mOk ? v : 0;
        }

    private:
        void fail(const char* key, std::string_view text, std::vector<std::string> args = {})
        {
            if (mOk)
            {
                mEngine.problem(ALScriptProblem::Severity::Error, key, text, std::move(args), mAt);
            }
            mOk = false;
        }

        const Token* peek()
        {
            mI = skipBlank(mTokens, mI);
            return mI < mTokens.size() ? &mTokens[mI] : nullptr;
        }

        bool accept(std::string_view op)
        {
            const Token* t = peek();
            if (t && t->kind == Kind::Punct && t->text == op)
            {
                ++mI;
                return true;
            }
            return false;
        }

        S64 ternary()
        {
            // A chain of them -- `a ? b ? c ? ...` -- is a level each, as
            // a bracket is.
            Deeper deeper(*this);
            if (!deeper.ok)
            {
                return 0;
            }
            S64 c = binary(1);
            if (accept("?"))
            {
                S64 a = ternary();
                if (!accept(":"))
                {
                    fail("PreprocExpectedColon", "expected ':' in preprocessor expression");
                }
                S64 b = ternary();
                return c ? a : b;
            }
            return c;
        }

        static S32 precedence(std::string_view op)
        {
            if (op == "||") return 1;
            if (op == "&&") return 2;
            if (op == "|") return 3;
            if (op == "^") return 4;
            if (op == "&") return 5;
            if (op == "==" || op == "!=" || op == "~=") return 6;
            if (op == "<" || op == ">" || op == "<=" || op == ">=") return 7;
            if (op == "<<" || op == ">>") return 8;
            if (op == "+" || op == "-") return 9;
            if (op == "*" || op == "/" || op == "%") return 10;
            return 0;
        }

        S64 binary(S32 minPrec)
        {
            S64 left = unary();
            while (true)
            {
                const Token* t = peek();
                if (!t || t->kind != Kind::Punct)
                {
                    return left;
                }
                const S32 prec = precedence(t->text);
                if (prec == 0 || prec < minPrec)
                {
                    return left;
                }
                const std::string op = t->text;
                ++mI;
                const S64 right = binary(prec + 1);
                left            = apply(op, left, right);
            }
        }

        S64 apply(const std::string& op, S64 a, S64 b)
        {
            const U64 ua = U64(a);
            const U64 ub = U64(b);
            if (op == "||") return (a != 0 || b != 0) ? 1 : 0;
            if (op == "&&") return (a != 0 && b != 0) ? 1 : 0;
            if (op == "|") return S64(ua | ub);
            if (op == "^") return S64(ua ^ ub);
            if (op == "&") return S64(ua & ub);
            if (op == "==") return a == b ? 1 : 0;
            if (op == "!=" || op == "~=") return a != b ? 1 : 0;
            if (op == "<") return a < b ? 1 : 0;
            if (op == ">") return a > b ? 1 : 0;
            if (op == "<=") return a <= b ? 1 : 0;
            if (op == ">=") return a >= b ? 1 : 0;
            if (op == "<<") return (b < 0 || b >= 64) ? 0 : S64(ua << ub);
            if (op == ">>")
            {
                if (b < 0 || b >= 64) return a < 0 ? -1 : 0;
                return a >> b;
            }
            if (op == "+") return S64(ua + ub);
            if (op == "-") return S64(ua - ub);
            if (op == "*") return S64(ua * ub);
            if (op == "/" || op == "%")
            {
                if (b == 0)
                {
                    fail("PreprocDivisionByZero", "division by zero in preprocessor expression");
                    return 0;
                }
                if (a == std::numeric_limits<S64>::min() && b == -1)
                {
                    return op == "/" ? a : 0;
                }
                return op == "/" ? a / b : a % b;
            }
            return 0;
        }

        S64 unary()
        {
            Deeper deeper(*this);
            if (!deeper.ok)
            {
                return 0;
            }
            if (accept("+")) return unary();
            if (accept("-")) return S64(0 - U64(unary()));
            if (accept("!")) return unary() == 0 ? 1 : 0;
            if (accept("~")) return S64(~U64(unary()));
            return primary();
        }

        S64 primary()
        {
            const Token* t = peek();
            if (!t)
            {
                fail("PreprocExpectedValue", "expected a value in preprocessor expression");
                return 0;
            }
            if (t->kind == Kind::Number)
            {
                ++mI;
                bool ok = false;
                S64  v  = parseInteger(t->text, ok);
                if (!ok)
                {
                    fail("PreprocNotAnInteger", "'[1]' is not an integer constant", { t->text });
                }
                return v;
            }
            if (t->kind == Kind::Ident)
            {
                ++mI;
                return t->text == "true" ? 1 : 0;
            }
            if (accept("("))
            {
                Deeper deeper(*this);
                if (!deeper.ok)
                {
                    return 0;
                }
                S64 v = ternary();
                if (!accept(")"))
                {
                    fail("PreprocExpectedClose", "expected ')' in preprocessor expression");
                }
                return v;
            }
            fail("PreprocUnexpectedInExpression", "unexpected '[1]' in preprocessor expression", { t->text });
            return 0;
        }

        const Tokens& mTokens;
        bool          mLua;
        Engine&       mEngine;
        const Token&  mAt;
        size_t        mI  = 0;
        bool          mOk = true;
        // How deep the recursive descent may go: an expression is the
        // script's, and `((((((...))))))` would otherwise be the C++
        // stack's to answer for.
        S32           mDepth = 64;
        S32           mIn    = 0;
        // One level deeper while it stands; false where that is too deep.
        struct Deeper
        {
            Expr& e;
            bool  ok;
            explicit Deeper(Expr& expr) : e(expr), ok(++expr.mIn <= expr.mDepth)
            {
                if (!ok && e.mOk)
                {
                    e.fail("PreprocExpressionTooDeep", "the expression nests too deeply for this preprocessor");
                }
            }
            ~Deeper() { --e.mIn; }
        };
    };

    S64 Engine::evalExpression(const Tokens& tokens, const Token& hash, bool& ok)
    {
        return Expr(tokens, mOptions.lua, *this, hash, mOptions.expressionDepth).parse(ok);
    }

    bool Engine::evalCondition(const Tokens& line, size_t at, const Token& hash)
    {
        // `defined` is answered before anything expands.
        Tokens pre;
        for (size_t i = at; i < line.size(); ++i)
        {
            const Token& t = line[i];
            if (t.kind == Kind::Ident && t.text == "defined")
            {
                size_t j     = skipBlank(line, i + 1);
                bool   paren = false;
                if (j < line.size() && line[j].is(Kind::Punct, "("))
                {
                    paren = true;
                    j     = skipBlank(line, j + 1);
                }
                if (j >= line.size() || line[j].kind != Kind::Ident)
                {
                    problem(ALScriptProblem::Severity::Error, "PreprocDefinedNeedsName", "macro name missing after 'defined'", {}, hash);
                    return false;
                }
                size_t k = j;
                if (paren)
                {
                    k = skipBlank(line, j + 1);
                    if (k >= line.size() || !line[k].is(Kind::Punct, ")"))
                    {
                        problem(ALScriptProblem::Severity::Error, "PreprocDefinedNeedsClose", "missing ')' after 'defined'", {}, hash);
                        return false;
                    }
                }
                Token num = t;
                num.kind  = Kind::Number;
                num.text  = mMacros.count(line[j].text) ? "1" : "0";
                pre.push_back(num);
                i = k;
                continue;
            }
            pre.push_back(t);
        }
        bool ok = true;
        S64  v  = evalExpression(expandAll(pre), hash, ok);
        return ok && v != 0;
    }

    void Engine::define(const Tokens& line, size_t at, const Token& hash)
    {
        if (at >= line.size() || line[at].kind != Kind::Ident)
        {
            problem(ALScriptProblem::Severity::Error, "PreprocDefineNeedsName", "macro name missing in #define", {}, hash);
            return;
        }
        Macro m;
        m.name = line[at].text;
        if (m.name == "defined")
        {
            problem(ALScriptProblem::Severity::Error, "PreprocDefinedNotAName", "'defined' cannot be used as a macro name", {}, line[at]);
            return;
        }
        size_t i = at + 1;
        if (i < line.size() && line[i].is(Kind::Punct, "("))
        {
            m.functionLike = true;
            i              = skipBlank(line, i + 1);
            bool ok        = true;
            if (i < line.size() && line[i].is(Kind::Punct, ")"))
            {
                ++i;
            }
            else
            {
                while (true)
                {
                    if (i >= line.size())
                    {
                        ok = false;
                        break;
                    }
                    if (line[i].is(Kind::Punct, "..."))
                    {
                        m.variadic = true;
                        m.params.push_back("__VA_ARGS__");
                        i = skipBlank(line, i + 1);
                        ok = i < line.size() && line[i].is(Kind::Punct, ")");
                        ++i;
                        break;
                    }
                    if (line[i].kind != Kind::Ident)
                    {
                        ok = false;
                        break;
                    }
                    if (std::find(m.params.begin(), m.params.end(), line[i].text) != m.params.end())
                    {
                        problem(ALScriptProblem::Severity::Error, "PreprocDuplicateParameter", "duplicate macro parameter '[1]'", { line[i].text }, line[i]);
                        return;
                    }
                    m.params.push_back(line[i].text);
                    i = skipBlank(line, i + 1);
                    if (i < line.size() && line[i].is(Kind::Punct, ","))
                    {
                        i = skipBlank(line, i + 1);
                        continue;
                    }
                    if (i < line.size() && line[i].is(Kind::Punct, ")"))
                    {
                        ++i;
                        break;
                    }
                    ok = false;
                    break;
                }
            }
            if (!ok)
            {
                problem(ALScriptProblem::Severity::Error, "PreprocBadParameters", "ill formed parameter list of macro '[1]'", { m.name }, line[at]);
                return;
            }
        }
        // The body, with its blanks as single spaces and none at the ends.
        for (; i < line.size(); ++i)
        {
            const Token& t = line[i];
            if (t.blank())
            {
                if (!m.body.empty() && m.body.back().kind != Kind::Space)
                {
                    Token space = t;
                    space.kind  = Kind::Space;
                    space.text  = " ";
                    m.body.push_back(space);
                }
                continue;
            }
            m.body.push_back(t);
        }
        while (!m.body.empty() && m.body.back().kind == Kind::Space)
        {
            m.body.pop_back();
        }
        if (!m.body.empty() && (m.body.front().is(Kind::Punct, "##") || m.body.back().is(Kind::Punct, "##")))
        {
            problem(ALScriptProblem::Severity::Error, "PreprocPasteAtEnd", "'##' cannot be at either end of a macro body", {}, line[at]);
            return;
        }
        if (m.functionLike)
        {
            for (size_t k = 0; k < m.body.size(); ++k)
            {
                if (m.body[k].is(Kind::Punct, "#"))
                {
                    const size_t j = skipBlank(m.body, k + 1);
                    if (j >= m.body.size() || paramIndex(m, m.body[j]) < 0)
                    {
                        problem(ALScriptProblem::Severity::Error, "PreprocStringizeNeedsParameter", "'#' is not followed by a macro parameter", {}, line[at]);
                        return;
                    }
                }
            }
        }
        auto it = mMacros.find(m.name);
        if (it != mMacros.end() && it->second.dynamic == Macro::Dynamic::None && !it->second.sameAs(m))
        {
            problem(ALScriptProblem::Severity::Warning, "PreprocMacroRedefined", "macro '[1]' redefined", { m.name }, line[at]);
        }
        if (m.name == "USE_SWITCHES")
        {
            mUsedSwitches = true;
        }
        else if (m.name == "USE_LAZY_LISTS")
        {
            mUsedLazyLists = true;
        }
        else if (m.name == "USE_EXTENSIONS")
        {
            mUsedExtensions = true;
        }
        mMacros[m.name] = std::move(m);
    }

    void Engine::include(const Tokens& line, size_t at, const Token& hash)
    {
        Tokens rest(line.begin() + std::min(at, line.size()), line.end());
        size_t i = skipBlank(rest, 0);
        if (i >= rest.size() || !(rest[i].kind == Kind::String || rest[i].is(Kind::Punct, "<")))
        {
            rest = expandAll(rest);
            i    = skipBlank(rest, 0);
        }
        ALPreprocessor::Ask ask;
        if (i < rest.size() && rest[i].kind == Kind::String && rest[i].text.size() >= 2 && rest[i].text.front() == '"' && rest[i].text.back() == '"')
        {
            ask.name = rest[i].text.substr(1, rest[i].text.size() - 2);
        }
        else if (i < rest.size() && rest[i].is(Kind::Punct, "<"))
        {
            ask.angled = true;
            size_t j   = i + 1;
            while (j < rest.size() && !rest[j].is(Kind::Punct, ">"))
            {
                ask.name += rest[j].text;
                ++j;
            }
            if (j >= rest.size())
            {
                problem(ALScriptProblem::Severity::Error, "PreprocIncludeNeedsClose", "missing '>' in #include", {}, hash);
                return;
            }
            ask.name = trim(ask.name);
        }
        else
        {
            problem(ALScriptProblem::Severity::Error, "PreprocBadInclude", "ill formed #include directive", {}, hash);
            return;
        }
        ask.from = mFiles.back()->path;
        if (S32(mFiles.size()) > mOptions.includeDepth)
        {
            problem(ALScriptProblem::Severity::Error, "PreprocIncludeTooDeep", "#include nested too deeply at '[1]'", { ask.name }, hash);
            return;
        }
        ALPreprocessor::Include found;
        const ALPreprocessor::Found answer = mOptions.resolve ? mOptions.resolve(ask, found) : ALPreprocessor::Found::No;
        switch (answer)
        {
            case ALPreprocessor::Found::Pending:
                if (mPendingNames.insert(ask.name).second)
                {
                    mResult.pending.push_back(ask.name);
                }
                return;
            case ALPreprocessor::Found::No:
                problem(ALScriptProblem::Severity::Error, "PreprocIncludeNotFound", "could not find include file '[1]'", { ask.name }, hash);
                return;
            case ALPreprocessor::Found::Yes:
                break;
        }
        const std::string identity = found.path.empty() ? ask.name : found.path;
        mResult.resolved.push_back({ ask.from, ask.name, false, identity });
        if (mOnce.count(identity))
        {
            return;
        }
        if (mIncluded.insert(identity).second)
        {
            mResult.includes.push_back(identity);
        }
        pushFile(found.text, identity, found.name.empty() ? ask.name : found.name, found.assetId);
    }

    void Engine::directive(FileState& f)
    {
        // The line itself stays a line (or the lines it was joined from),
        // so that what follows keeps its number.
        Token nl;
        nl.kind           = Kind::Newline;
        nl.text           = "\n";
        const Tokens line = readLine(f, nl);
        size_t       i    = skipBlank(line, 0);
        const Token  hash = line[i];
        i                 = skipBlank(line, i + 1);
        nl.file           = hash.file;
        nl.line           = hash.line;
        nl.column         = hash.column;
        nl.verbatim       = false;
        emit(nl);
        if (i >= line.size())
        {
            return;
        }
        const std::string name  = line[i].kind == Kind::Ident ? line[i].text : std::string();
        const size_t      after = skipBlank(line, i + 1);
        const bool        active = f.active();
        const auto        parentActive = [&]() { return f.conds.size() < 2 || f.conds[f.conds.size() - 2].active; };
        if (name == "if" || name == "ifdef" || name == "ifndef")
        {
            Cond c;
            if (!active)
            {
                c.taken = true;
            }
            else if (name == "if")
            {
                c.active = evalCondition(line, after, hash);
                c.taken  = c.active;
            }
            else
            {
                if (after >= line.size() || line[after].kind != Kind::Ident)
                {
                    problem(ALScriptProblem::Severity::Error, "PreprocDirectiveNeedsName", "macro name missing in #[1]", { name }, hash);
                }
                else
                {
                    c.active = (mMacros.count(line[after].text) > 0) == (name == "ifdef");
                }
                c.taken = c.active;
            }
            f.conds.push_back(c);
            return;
        }
        if (name == "elif")
        {
            if (f.conds.empty())
            {
                problem(ALScriptProblem::Severity::Error, "PreprocElifWithoutIf", "#elif without #if", {}, hash);
                return;
            }
            Cond& c = f.conds.back();
            if (c.seenElse)
            {
                problem(ALScriptProblem::Severity::Error, "PreprocElifAfterElse", "#elif after #else", {}, hash);
            }
            if (c.taken || !parentActive())
            {
                c.active = false;
            }
            else
            {
                c.active = evalCondition(line, after, hash);
                c.taken  = c.active;
            }
            return;
        }
        if (name == "else")
        {
            if (f.conds.empty())
            {
                problem(ALScriptProblem::Severity::Error, "PreprocElseWithoutIf", "#else without #if", {}, hash);
                return;
            }
            Cond& c = f.conds.back();
            if (c.seenElse)
            {
                problem(ALScriptProblem::Severity::Error, "PreprocElseAfterElse", "#else after #else", {}, hash);
            }
            c.active   = !c.taken && parentActive();
            c.taken    = true;
            c.seenElse = true;
            return;
        }
        if (name == "endif")
        {
            if (f.conds.empty())
            {
                problem(ALScriptProblem::Severity::Error, "PreprocEndifWithoutIf", "#endif without #if", {}, hash);
                return;
            }
            f.conds.pop_back();
            return;
        }
        if (!active)
        {
            return;
        }
        if (name == "define")
        {
            define(line, after, hash);
        }
        else if (name == "undef")
        {
            if (after >= line.size() || line[after].kind != Kind::Ident)
            {
                problem(ALScriptProblem::Severity::Error, "PreprocUndefNeedsName", "macro name missing in #undef", {}, hash);
                return;
            }
            mMacros.erase(line[after].text);
        }
        else if (name == "include")
        {
            include(line, after, hash);
        }
        else if (name == "error" || name == "warning")
        {
            const std::string text = trim(textOf(line, after));
            problem(name == "error" ? ALScriptProblem::Severity::Error : ALScriptProblem::Severity::Warning,
                    text.empty() ? "#" + name : text, hash);
        }
        else if (name == "pragma")
        {
            if (after < line.size() && line[after].is(Kind::Ident, "once"))
            {
                mOnce.insert(f.path);
            }
        }
        else if (name == "line")
        {
            // Passed through as a comment, as Firestorm does with Wave's.
            Token comment    = hash;
            comment.kind     = Kind::Comment;
            comment.text     = (mOptions.lua ? "--#line " : "//#line ") + trim(textOf(line, after));
            comment.verbatim = false;
            mSink->pop_back();
            emit(comment);
            emit(nl);
        }
        else
        {
            problem(ALScriptProblem::Severity::Error, "PreprocBadDirective", "ill formed preprocessor directive '#[1]'", { name.empty() ? line[i].text : name }, hash);
        }
    }

    // ---- Firestorm's transforms, over the output ----------------------------------

    Token synth(Kind kind, std::string text, const Token& site)
    {
        Token t;
        t.kind     = kind;
        t.text     = std::move(text);
        t.file     = site.file;
        t.line     = site.line;
        t.column   = site.column;
        t.verbatim = false;
        return t;
    }

    // The index of the bracket closing the one at `open`, or npos.
    size_t matching(const Tokens& t, size_t open, const char* left, const char* right)
    {
        S32 depth = 0;
        for (size_t i = open; i < t.size(); ++i)
        {
            if (t[i].is(Kind::Punct, left))
            {
                ++depth;
            }
            else if (t[i].is(Kind::Punct, right))
            {
                if (--depth == 0)
                {
                    return i;
                }
            }
        }
        return std::string::npos;
    }

    Tokens slice(const Tokens& t, size_t from, size_t to)
    {
        return Tokens(t.begin() + from, t.begin() + to);
    }

    void append(Tokens& out, const Tokens& more)
    {
        out.insert(out.end(), more.begin(), more.end());
    }

    // A level of a transform's descent into what the script nests,
    // counted while it stands.
    struct Nesting
    {
        S32& depth;
        explicit Nesting(S32& d) : depth(++d) {}
        ~Nesting() { --depth; }
    };

    // The language extensions LSL-PyOptimizer's users know, lowered to
    // LSL: `break` and `continue` in a loop -- `break 2` for the loop
    // outside -- as jumps to labels put after the loop and at the end
    // of its body, and `&= |= ^= <<= >>=` as the assignment each is.
    // A `break` whose nearest scope is a `switch` is left for the switch
    // transform, which runs after; a `continue` inside a switch goes to
    // the loop around it, as in C. The body of a loop that is one
    // statement is put in braces where a label has to follow it.
    class Extensions
    {
    public:
        explicit Extensions(Engine& engine) : mEngine(engine) {}

        Tokens run(const Tokens& in)
        {
            Tokens out = markers(assignments(in));
            mScopes.clear();
            return statements(out, 0, out.size());
        }

        bool any() const { return mCounter > 0 || mAssignments > 0 || !mInlined.empty(); }
        // The functions marked `inline`, for the optimizer to put in
        // place wherever they are called.
        const std::vector<std::string>& inlined() const { return mInlined; }

    private:
        struct Scope
        {
            bool        loop = false;
            std::string breakLabel;
            std::string continueLabel;
            bool        breakUsed    = false;
            bool        continueUsed = false;
            // A label the script already has where the loop's would go
            // -- one right after the loop, one ending its body -- is the
            // loop's, since two labels at one place are one too many.
            bool        breakShared    = false;
            bool        continueShared = false;
        };

        // The name of a label `@name;` whose `@` is at `i`, or nothing.
        static std::string labelAt(const Tokens& t, size_t i, size_t to)
        {
            if (i >= to || !t[i].is(Kind::Punct, "@"))
            {
                return std::string();
            }
            const size_t name = skipBlank(t, i + 1);
            if (name >= to || t[name].kind != Kind::Ident)
            {
                return std::string();
            }
            const size_t semi = skipBlank(t, name + 1);
            return semi < to && t[semi].is(Kind::Punct, ";") ? t[name].text : std::string();
        }

        // The name of a label `@name;` that ends before `end`, the last
        // thing before it but blanks, or nothing.
        static std::string labelBefore(const Tokens& t, size_t from, size_t end)
        {
            size_t at = end;
            while (at > from && t[at - 1].blank())
            {
                --at;
            }
            if (at < from + 3 || !t[at - 1].is(Kind::Punct, ";"))
            {
                return std::string();
            }
            size_t name = at - 1;
            while (name > from && t[name - 1].blank())
            {
                --name;
            }
            if (name == from || t[name - 1].kind != Kind::Ident)
            {
                return std::string();
            }
            size_t sign = name - 1;
            while (sign > from && t[sign - 1].blank())
            {
                --sign;
            }
            return sign > from && t[sign - 1].is(Kind::Punct, "@") ? t[name - 1].text : std::string();
        }

        // An `inline` before a function's definition -- `inline f()`,
        // `inline integer f(x)` -- taken off and the name kept.
        Tokens markers(const Tokens& in)
        {
            Tokens out;
            for (size_t i = 0; i < in.size(); ++i)
            {
                if (in[i].is(Kind::Ident, "inline"))
                {
                    size_t j = skipBlank(in, i + 1);
                    size_t name = std::string::npos;
                    if (j < in.size() && in[j].kind == Kind::Ident)
                    {
                        const size_t k = skipBlank(in, j + 1);
                        if (k < in.size() && in[k].is(Kind::Punct, "("))
                        {
                            name = j;
                        }
                        else if (k < in.size() && in[k].kind == Kind::Ident)
                        {
                            const size_t l = skipBlank(in, k + 1);
                            if (l < in.size() && in[l].is(Kind::Punct, "("))
                            {
                                name = k;
                            }
                        }
                    }
                    if (name != std::string::npos)
                    {
                        mInlined.push_back(in[name].text);
                        i = j - 1;
                        continue;
                    }
                }
                out.push_back(in[i]);
            }
            return out;
        }

        // `a &= b` and the rest as `a = a & (b)`, the left side an
        // identifier or a vector's component before the operator, the
        // right side as far as the expression goes: to a `;` or `,` or
        // a closing bracket at the same depth.
        Tokens assignments(const Tokens& in)
        {
            static const char* const ops[] = { "&=", "|=", "^=", "<<=", ">>=" };
            Tokens                   out;
            for (size_t i = 0; i < in.size(); ++i)
            {
                const Token& t = in[i];
                const char*  op = nullptr;
                for (const char* each : ops)
                {
                    if (t.is(Kind::Punct, each))
                    {
                        op = each;
                    }
                }
                if (!op)
                {
                    out.push_back(t);
                    continue;
                }
                // The left side, off the end of what is out already.
                size_t back = out.size();
                while (back > 0 && out[back - 1].blank())
                {
                    --back;
                }
                size_t lhs_begin = back;
                if (back >= 1 && out[back - 1].kind == Kind::Ident)
                {
                    lhs_begin = back - 1;
                    if (back >= 3 && out[back - 2].is(Kind::Punct, ".") && out[back - 3].kind == Kind::Ident)
                    {
                        lhs_begin = back - 3;
                    }
                }
                if (lhs_begin == back)
                {
                    mEngine.problem(ALScriptProblem::Severity::Error, "PreprocNoVariableBefore", "no variable before '[1]'", { op }, t);
                    out.push_back(t);
                    continue;
                }
                const Tokens lhs(out.begin() + static_cast<std::ptrdiff_t>(lhs_begin), out.begin() + static_cast<std::ptrdiff_t>(back));
                // The right side.
                size_t end   = i + 1;
                S32    depth = 0;
                for (; end < in.size(); ++end)
                {
                    const Token& e = in[end];
                    // Integers on both sides, so < and > are comparisons,
                    // not a vector's brackets.
                    if (e.is(Kind::Punct, "(") || e.is(Kind::Punct, "["))
                    {
                        ++depth;
                    }
                    else if (e.is(Kind::Punct, ")") || e.is(Kind::Punct, "]"))
                    {
                        if (depth == 0)
                        {
                            break;
                        }
                        --depth;
                    }
                    else if (depth == 0 && (e.is(Kind::Punct, ";") || e.is(Kind::Punct, ",")))
                    {
                        break;
                    }
                }
                const size_t rhs_begin = skipBlank(in, i + 1);
                while (!out.empty() && out.back().blank())
                {
                    out.pop_back();
                }
                out.push_back(synth(Kind::Space, " ", t));
                out.push_back(synth(Kind::Punct, "=", t));
                out.push_back(synth(Kind::Space, " ", t));
                append(out, lhs);
                out.push_back(synth(Kind::Space, " ", t));
                out.push_back(synth(Kind::Punct, std::string(op, strlen(op) - 1), t));
                out.push_back(synth(Kind::Space, " ", t));
                out.push_back(synth(Kind::Punct, "(", t));
                for (size_t k = rhs_begin; k < end; ++k)
                {
                    out.push_back(in[k]);
                }
                while (!out.empty() && out.back().blank())
                {
                    out.pop_back();
                }
                out.push_back(synth(Kind::Punct, ")", t));
                ++mAssignments;
                i = end - 1;
            }
            return out;
        }

        // Where a statement starting at `i` ends: the index past its last
        // token. A block to its brace; if, while, for, do to the end of
        // what they govern; anything else to its semicolon. An `else if`
        // is a chain rather than a nesting, walked along, so that a long
        // run of them costs no depth; what a statement governs is a level
        // down.
        size_t statementEnd(const Tokens& t, size_t i)
        {
            while (true)
            {
                i = skipBlank(t, i);
                if (i >= t.size())
                {
                    return t.size();
                }
                if (t[i].is(Kind::Punct, "{"))
                {
                    const size_t m = matching(t, i, "{", "}");
                    return m == std::string::npos ? t.size() : m + 1;
                }
                if (t[i].is(Kind::Ident, "if") || t[i].is(Kind::Ident, "while") || t[i].is(Kind::Ident, "for"))
                {
                    const size_t open = skipBlank(t, i + 1);
                    if (open >= t.size() || !t[open].is(Kind::Punct, "("))
                    {
                        return semicolonAfter(t, i);
                    }
                    const size_t close = matching(t, open, "(", ")");
                    if (close == std::string::npos)
                    {
                        return t.size();
                    }
                    const size_t end = governedEnd(t, close + 1);
                    if (t[i].is(Kind::Ident, "if"))
                    {
                        const size_t e = skipBlank(t, end);
                        if (e < t.size() && t[e].is(Kind::Ident, "else"))
                        {
                            i = e + 1;
                            continue;
                        }
                    }
                    return end;
                }
                if (t[i].is(Kind::Ident, "do"))
                {
                    return semicolonAfter(t, governedEnd(t, i + 1));
                }
                return semicolonAfter(t, i);
            }
        }

        // Where a statement another governs ends, a level down.
        size_t governedEnd(const Tokens& t, size_t i)
        {
            const Nesting level(mNested);
            if (!mEngine.nest(mNested, i < t.size() ? t[i] : Token()))
            {
                return t.size();
            }
            return statementEnd(t, i);
        }

        size_t semicolonAfter(const Tokens& t, size_t i) const
        {
            S32 depth = 0;
            for (; i < t.size(); ++i)
            {
                if (t[i].is(Kind::Punct, "(") || t[i].is(Kind::Punct, "[") || t[i].is(Kind::Punct, "{"))
                {
                    ++depth;
                }
                else if (t[i].is(Kind::Punct, ")") || t[i].is(Kind::Punct, "]") || t[i].is(Kind::Punct, "}"))
                {
                    --depth;
                }
                else if (depth <= 0 && t[i].is(Kind::Punct, ";"))
                {
                    return i + 1;
                }
            }
            return t.size();
        }

        // The tokens from `from` to `to` with every loop in them lowered,
        // each statement in turn; a block or a body in them a level down.
        Tokens statements(const Tokens& in, size_t from, size_t to)
        {
            const Nesting level(mNested);
            if (!mEngine.nest(mNested, from < in.size() ? in[from] : Token()))
            {
                return slice(in, from, to);
            }
            Tokens out;
            size_t i = from;
            while (i < to)
            {
                const Token& t = in[i];
                if (t.blank())
                {
                    out.push_back(t);
                    ++i;
                    continue;
                }
                if (t.is(Kind::Ident, "while") || t.is(Kind::Ident, "for") || t.is(Kind::Ident, "do"))
                {
                    const size_t end = std::min(statementEnd(in, i), to);
                    append(out, loop(in, i, end, to));
                    i = end;
                    continue;
                }
                if (t.is(Kind::Ident, "switch"))
                {
                    // Its body is a scope of its own for `break`.
                    const size_t open = skipBlank(in, i + 1);
                    if (open < to && in[open].is(Kind::Punct, "("))
                    {
                        const size_t close = matching(in, open, "(", ")");
                        const size_t brace = close == std::string::npos ? close : skipBlank(in, close + 1);
                        if (brace != std::string::npos && brace < to && in[brace].is(Kind::Punct, "{"))
                        {
                            const size_t m = matching(in, brace, "{", "}");
                            if (m != std::string::npos && m < to)
                            {
                                for (size_t k = i; k <= brace; ++k)
                                {
                                    out.push_back(in[k]);
                                }
                                mScopes.push_back(Scope());
                                append(out, statements(in, brace + 1, m));
                                mScopes.pop_back();
                                out.push_back(in[m]);
                                i = m + 1;
                                continue;
                            }
                        }
                    }
                }
                if (t.is(Kind::Punct, "{"))
                {
                    const size_t m = matching(in, i, "{", "}");
                    if (m != std::string::npos && m < to)
                    {
                        out.push_back(t);
                        append(out, statements(in, i + 1, m));
                        out.push_back(in[m]);
                        i = m + 1;
                        continue;
                    }
                }
                if (t.is(Kind::Ident, "break") || t.is(Kind::Ident, "continue"))
                {
                    // Only as a statement: what comes before it ends one,
                    // or opens the body one belongs to. Anywhere else the
                    // word is a name, which the extension reserves.
                    size_t back = out.size();
                    while (back > 0 && out[back - 1].blank())
                    {
                        --back;
                    }
                    const bool statement_position =
                        back == 0 || out[back - 1].is(Kind::Punct, ";") || out[back - 1].is(Kind::Punct, "{") || out[back - 1].is(Kind::Punct, "}") ||
                        out[back - 1].is(Kind::Punct, ")") || out[back - 1].is(Kind::Punct, ":") || out[back - 1].is(Kind::Ident, "else");
                    const size_t after_word = skipBlank(in, i + 1);
                    const bool   ends_well  = after_word < to && (in[after_word].is(Kind::Punct, ";") || in[after_word].kind == Kind::Number);
                    if (!statement_position || !ends_well)
                    {
                        mEngine.problem(ALScriptProblem::Severity::Error, "PreprocReservedWordAsName", "'[1]' is a name here, which break and continue reserve", { t.text }, t);
                        out.push_back(t);
                        ++i;
                        continue;
                    }
                    // `break;`, `break 2;`: so many loops out.
                    const bool   is_break = t.is(Kind::Ident, "break");
                    size_t       e        = skipBlank(in, i + 1);
                    S32          levels   = 1;
                    if (e < to && in[e].kind == Kind::Number)
                    {
                        levels = std::max(1, std::atoi(in[e].text.c_str()));
                        e      = skipBlank(in, e + 1);
                    }
                    if (e < to && in[e].is(Kind::Punct, ";"))
                    {
                        Scope* target = nullptr;
                        if (is_break && levels == 1 && !mScopes.empty() && !mScopes.back().loop)
                        {
                            // The switch's own; its transform takes it.
                        }
                        else
                        {
                            S32 seen = 0;
                            for (size_t k = mScopes.size(); k-- > 0;)
                            {
                                if (mScopes[k].loop && ++seen == levels)
                                {
                                    target = &mScopes[k];
                                    break;
                                }
                            }
                            if (!target)
                            {
                                mEngine.problem(ALScriptProblem::Severity::Error, "PreprocOutsideLoop", "[1] outside a loop", { is_break ? "break" : "continue" }, t);
                            }
                        }
                        if (target)
                        {
                            (is_break ? target->breakUsed : target->continueUsed) = true;
                            out.push_back(synth(Kind::Ident, "jump", t));
                            out.push_back(synth(Kind::Space, " ", t));
                            out.push_back(synth(Kind::Ident, is_break ? target->breakLabel : target->continueLabel, t));
                            out.push_back(synth(Kind::Punct, ";", t));
                            i = e + 1;
                            continue;
                        }
                    }
                }
                out.push_back(t);
                ++i;
            }
            return out;
        }

        // A loop from `i` to `end`, its body done with a scope of its own
        // and the labels put where the jumps expect them; `to` bounds
        // what follows the loop, where a label of the script's own may
        // stand in for the break's.
        Tokens loop(const Tokens& in, size_t i, size_t end, size_t to)
        {
            const Token& site = in[i];
            Scope        scope;
            scope.loop          = true;
            const S32 n         = ++mCounter;
            scope.breakLabel    = "_brk" + std::to_string(n);
            scope.continueLabel = "_cnt" + std::to_string(n);
            // The head: `while (...)`, `for (...)`, or `do`; the body; the
            // tail of a do: `while (...);`.
            size_t body_begin, body_end;
            if (site.is(Kind::Ident, "do"))
            {
                body_begin = i + 1;
                body_end   = statementEnd(in, body_begin);
            }
            else
            {
                const size_t open  = skipBlank(in, i + 1);
                const size_t close = open < end && in[open].is(Kind::Punct, "(") ? matching(in, open, "(", ")") : std::string::npos;
                if (close == std::string::npos || close >= end)
                {
                    return slice(in, i, end);
                }
                body_begin = close + 1;
                body_end   = end;
            }
            // The script's own labels where the loop's would go.
            if (const std::string following = labelAt(in, skipBlank(in, end), to); !following.empty())
            {
                scope.breakLabel  = following;
                scope.breakShared = true;
            }
            {
                const size_t first = skipBlank(in, body_begin);
                size_t       last  = body_end;
                while (last > first && in[last - 1].blank())
                {
                    --last;
                }
                if (first < last && in[first].is(Kind::Punct, "{") && in[last - 1].is(Kind::Punct, "}") && matching(in, first, "{", "}") == last - 1)
                {
                    if (const std::string ending = labelBefore(in, first + 1, last - 1); !ending.empty())
                    {
                        scope.continueLabel  = ending;
                        scope.continueShared = true;
                    }
                }
            }
            mScopes.push_back(scope);
            Tokens body = statements(in, body_begin, body_end);
            const Scope done = mScopes.back();
            mScopes.pop_back();
            Tokens out = slice(in, i, body_begin);
            if (done.continueUsed && !done.continueShared)
            {
                // The label at the body's end, inside braces of its own if
                // the body has none.
                size_t     first = 0;
                while (first < body.size() && body[first].blank())
                {
                    ++first;
                }
                const bool block = first < body.size() && body[first].is(Kind::Punct, "{");
                if (block)
                {
                    size_t last = body.size();
                    while (last > 0 && body[last - 1].blank())
                    {
                        --last;
                    }
                    // Before the closing brace.
                    Tokens with(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(last - 1));
                    with.push_back(synth(Kind::Punct, "@", site));
                    with.push_back(synth(Kind::Ident, done.continueLabel, site));
                    with.push_back(synth(Kind::Punct, ";", site));
                    with.insert(with.end(), body.begin() + static_cast<std::ptrdiff_t>(last - 1), body.end());
                    body.swap(with);
                }
                else
                {
                    Tokens with;
                    with.push_back(synth(Kind::Space, " ", site));
                    with.push_back(synth(Kind::Punct, "{", site));
                    append(with, body);
                    with.push_back(synth(Kind::Punct, "@", site));
                    with.push_back(synth(Kind::Ident, done.continueLabel, site));
                    with.push_back(synth(Kind::Punct, ";", site));
                    with.push_back(synth(Kind::Punct, "}", site));
                    body.swap(with);
                }
            }
            append(out, body);
            if (site.is(Kind::Ident, "do"))
            {
                append(out, slice(in, body_end, end));
            }
            if (done.breakUsed && !done.breakShared)
            {
                out.push_back(synth(Kind::Punct, "@", site));
                out.push_back(synth(Kind::Ident, done.breakLabel, site));
                out.push_back(synth(Kind::Punct, ";", site));
            }
            return out;
        }

        Engine&                  mEngine;
        std::vector<Scope>       mScopes;
        S32                      mCounter     = 0;
        S32                      mAssignments = 0;
        std::vector<std::string> mInlined;
        // How far down the descent is.
        S32                      mNested      = 0;
    };

    // The shape Firestorm emits: a block whose first statements test the
    // argument against each case in turn and jump to its label, then to
    // the default's or past the end; each `case` a label, each `break` a
    // jump past the end. Nested switches are done first, so their breaks
    // are already jumps.
    class Switches
    {
    public:
        explicit Switches(Engine& engine) : mEngine(engine) {}

        // A switch's body is run over first, a level down.
        Tokens run(const Tokens& in)
        {
            const Nesting level(mNested);
            if (!mEngine.nest(mNested, in.empty() ? Token() : in.front()))
            {
                return in;
            }
            Tokens out;
            size_t i = 0;
            while (i < in.size())
            {
                if (in[i].is(Kind::Ident, "switch"))
                {
                    const size_t j = skipBlank(in, i + 1);
                    if (j < in.size() && in[j].is(Kind::Punct, "("))
                    {
                        const size_t k = matching(in, j, "(", ")");
                        const size_t l = k == std::string::npos ? k : skipBlank(in, k + 1);
                        if (l != std::string::npos && l < in.size() && in[l].is(Kind::Punct, "{"))
                        {
                            const size_t m = matching(in, l, "{", "}");
                            if (m != std::string::npos)
                            {
                                warnRepeated(in[i], slice(in, j + 1, k));
                                append(out, build(in[i], slice(in, j, k + 1), run(slice(in, l + 1, m))));
                                i = m + 1;
                                continue;
                            }
                        }
                    }
                    mEngine.problem(ALScriptProblem::Severity::Error, "PreprocBadSwitch", "ill formed switch statement", {}, in[i]);
                }
                out.push_back(in[i]);
                ++i;
            }
            return out;
        }

    private:
        // The value is tested against each case in turn, and so worked out
        // again for each, as Firestorm's is: said where that is more than
        // a cost -- a call, which may answer differently each time or do
        // something, or something the value changes as it is worked out.
        void warnRepeated(const Token& site, const Tokens& value)
        {
            static const char* const CHANGES[] = { "++", "--", "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=" };
            bool         repeated = false;
            const Token* before   = nullptr;
            for (const Token& t : value)
            {
                if (t.blank())
                {
                    continue;
                }
                if (t.kind == Kind::Punct && ((t.text == "(" && before && before->kind == Kind::Ident) ||
                                              std::find(std::begin(CHANGES), std::end(CHANGES), t.text) != std::end(CHANGES)))
                {
                    repeated = true;
                    break;
                }
                before = &t;
            }
            if (repeated)
            {
                mEngine.problem(ALScriptProblem::Severity::Warning, "PreprocSwitchValueRepeated",
                                "the switch's value is worked out again for each case: put it in a local first", {}, site);
            }
        }

        Tokens build(const Token& site, const Tokens& arg, const Tokens& body)
        {
            const S32         n       = ++mCounter;
            const std::string prefix  = "_sw" + std::to_string(n) + "_";
            const std::string dflt    = prefix + "default";
            const std::string end     = prefix + "end";
            bool              hasDefault = false;
            S32               cases   = 0;
            std::vector<std::pair<Tokens, std::string>> table;
            Tokens            inner;
            const auto label = [&](const std::string& name, const Token& at, bool brace) {
                inner.push_back(synth(Kind::Punct, "@", at));
                inner.push_back(synth(Kind::Ident, name, at));
                inner.push_back(synth(Kind::Punct, ";", at));
                if (brace)
                {
                    inner.push_back(synth(Kind::Punct, "{", at));
                }
            };
            for (size_t i = 0; i < body.size(); ++i)
            {
                const Token& t = body[i];
                if (t.is(Kind::Ident, "case"))
                {
                    size_t e = i + 1;
                    while (e < body.size() && !body[e].is(Kind::Punct, ":") && !body[e].is(Kind::Punct, "{"))
                    {
                        ++e;
                    }
                    if (e >= body.size())
                    {
                        mEngine.problem(ALScriptProblem::Severity::Error, "PreprocBadCase", "cannot find ':' or '{' after case", {}, t);
                        inner.push_back(t);
                        continue;
                    }
                    Tokens expr;
                    for (size_t k = i + 1; k < e; ++k)
                    {
                        if (!body[k].blank() || (!expr.empty() && !expr.back().blank()))
                        {
                            expr.push_back(body[k]);
                        }
                    }
                    while (!expr.empty() && expr.back().blank())
                    {
                        expr.pop_back();
                    }
                    const std::string name = prefix + std::to_string(++cases);
                    table.emplace_back(expr, name);
                    label(name, t, body[e].is(Kind::Punct, "{"));
                    i = e;
                    continue;
                }
                if (t.is(Kind::Ident, "default"))
                {
                    const size_t e = skipBlank(body, i + 1);
                    if (e < body.size() && (body[e].is(Kind::Punct, ":") || body[e].is(Kind::Punct, "{")))
                    {
                        hasDefault = true;
                        label(dflt, t, body[e].is(Kind::Punct, "{"));
                        i = e;
                        continue;
                    }
                }
                if (t.is(Kind::Ident, "break"))
                {
                    const size_t e = skipBlank(body, i + 1);
                    if (e < body.size() && body[e].is(Kind::Punct, ";"))
                    {
                        inner.push_back(synth(Kind::Ident, "jump", t));
                        inner.push_back(synth(Kind::Space, " ", t));
                        inner.push_back(synth(Kind::Ident, end, t));
                        inner.push_back(synth(Kind::Punct, ";", t));
                        i = e;
                        continue;
                    }
                }
                inner.push_back(t);
            }
            Tokens out;
            out.push_back(synth(Kind::Punct, "{", site));
            for (const auto& entry : table)
            {
                out.push_back(synth(Kind::Ident, "if", site));
                out.push_back(synth(Kind::Punct, "(", site));
                append(out, arg);
                out.push_back(synth(Kind::Space, " ", site));
                out.push_back(synth(Kind::Punct, "==", site));
                out.push_back(synth(Kind::Space, " ", site));
                out.push_back(synth(Kind::Punct, "(", site));
                append(out, entry.first);
                out.push_back(synth(Kind::Punct, ")", site));
                out.push_back(synth(Kind::Punct, ")", site));
                out.push_back(synth(Kind::Ident, "jump", site));
                out.push_back(synth(Kind::Space, " ", site));
                out.push_back(synth(Kind::Ident, entry.second, site));
                out.push_back(synth(Kind::Punct, ";", site));
                out.push_back(synth(Kind::Newline, "\n", site));
            }
            out.push_back(synth(Kind::Ident, "jump", site));
            out.push_back(synth(Kind::Space, " ", site));
            out.push_back(synth(Kind::Ident, hasDefault ? dflt : end, site));
            out.push_back(synth(Kind::Punct, ";", site));
            out.push_back(synth(Kind::Newline, "\n", site));
            append(out, inner);
            out.push_back(synth(Kind::Newline, "\n", site));
            out.push_back(synth(Kind::Punct, "@", site));
            out.push_back(synth(Kind::Ident, end, site));
            out.push_back(synth(Kind::Punct, ";", site));
            out.push_back(synth(Kind::Newline, "\n", site));
            out.push_back(synth(Kind::Punct, "}", site));
            return out;
        }

        Engine& mEngine;
        S32     mCounter = 0;
        S32     mNested  = 0;
    };

    const char* const LAZY_LIST_SET =
        "list lazy_list_set(list L, integer i, list v)\n"
        "{\n"
        "    while (llGetListLength(L) < i)\n"
        "        L = L + 0;\n"
        "    return llListReplaceList(L, v, i, i);\n"
        "}\n";

    // `name[i] = v;` as `name = lazy_list_set(name, i, [v]);` and
    // `(type)name[i]` as `llList2Type(name, i)`, with the helper at the
    // top once anything used it.
    class LazyLists
    {
    public:
        explicit LazyLists(Engine& engine) : mEngine(engine) {}

        Tokens run(const Tokens& in)
        {
            const Tokens assigned = assignments(in);
            Tokens       out;
            reads(assigned, 0, assigned.size(), out);
            if (!mAny)
            {
                return out;
            }
            Tokens helper = Lexer(false, 0).run(LAZY_LIST_SET);
            for (Token& t : helper)
            {
                t.verbatim = false;
                t.line     = 0;
                t.column   = 0;
            }
            helper.push_back(synth(Kind::Newline, "\n", helper.front()));
            append(helper, out);
            return helper;
        }

        bool any() const { return mAny; }

    private:
        static bool isName(const Token& t)
        {
            return t.kind == Kind::Ident && t.text != "return" && t.text != "do" && t.text != "else";
        }

        Tokens assignments(const Tokens& in)
        {
            Tokens out;
            size_t i = 0;
            while (i < in.size())
            {
                const Token& t = in[i];
                if (isName(t))
                {
                    const size_t j = skipBlank(in, i + 1);
                    if (j < in.size() && in[j].is(Kind::Punct, "["))
                    {
                        const size_t k = matching(in, j, "[", "]");
                        const size_t l = k == std::string::npos ? k : skipBlank(in, k + 1);
                        if (l != std::string::npos && l < in.size() && in[l].is(Kind::Punct, "="))
                        {
                            // The value runs to the `;` or the `)` that
                            // closes what the assignment is in.
                            size_t m     = skipBlank(in, l + 1);
                            S32    depth = 0;
                            size_t stop  = std::string::npos;
                            for (size_t p = m; p < in.size(); ++p)
                            {
                                if (in[p].is(Kind::Punct, "("))
                                {
                                    ++depth;
                                }
                                else if (in[p].is(Kind::Punct, ")"))
                                {
                                    if (depth == 0)
                                    {
                                        stop = p;
                                        break;
                                    }
                                    --depth;
                                }
                                else if (in[p].is(Kind::Punct, ";") && depth == 0)
                                {
                                    stop = p;
                                    break;
                                }
                            }
                            if (stop != std::string::npos && stop > m)
                            {
                                mAny = true;
                                out.push_back(t);
                                out.push_back(synth(Kind::Space, " ", t));
                                out.push_back(synth(Kind::Punct, "=", t));
                                out.push_back(synth(Kind::Space, " ", t));
                                out.push_back(synth(Kind::Ident, "lazy_list_set", t));
                                out.push_back(synth(Kind::Punct, "(", t));
                                out.push_back(t);
                                out.push_back(synth(Kind::Punct, ",", t));
                                append(out, slice(in, j + 1, k));
                                out.push_back(synth(Kind::Punct, ",", t));
                                out.push_back(synth(Kind::Punct, "[", t));
                                Tokens value = slice(in, m, stop);
                                while (!value.empty() && value.back().blank())
                                {
                                    value.pop_back();
                                }
                                append(out, value);
                                out.push_back(synth(Kind::Punct, "]", t));
                                out.push_back(synth(Kind::Punct, ")", t));
                                i = stop;
                                continue;
                            }
                        }
                    }
                }
                out.push_back(t);
                ++i;
            }
            return out;
        }

        static const char* readerFor(const std::string& type)
        {
            if (type == "integer") return "llList2Integer";
            if (type == "float") return "llList2Float";
            if (type == "string") return "llList2String";
            if (type == "key") return "llList2Key";
            if (type == "vector") return "llList2Vector";
            if (type == "rotation" || type == "quaternion") return "llList2Rot";
            if (type == "list") return "llList2List";
            return nullptr;
        }

        // The reads between `from` and `to`, each made a call, into `out`;
        // a read inside the brackets of another a level down, as deep as
        // blocks may nest -- a macro can nest them without end -- and each
        // level over the same tokens rather than a copy of its part.
        void reads(const Tokens& in, size_t from, size_t to, Tokens& out)
        {
            const Nesting level(mNested);
            if (mStopped || !mEngine.nest(mNested, from < to ? in[from] : Token()))
            {
                // The run has overrun, and what it made goes unread.
                mStopped = true;
                return;
            }
            size_t i = from;
            while (i < to)
            {
                const Token& t = in[i];
                if (t.is(Kind::Punct, "("))
                {
                    const size_t a      = skipBlank(in, i + 1);
                    const char*  reader = a < to && in[a].kind == Kind::Ident ? readerFor(in[a].text) : nullptr;
                    const size_t b      = reader ? skipBlank(in, a + 1) : to;
                    if (reader && b < to && in[b].is(Kind::Punct, ")"))
                    {
                        size_t     c     = skipBlank(in, b + 1);
                        const bool paren = c < to && in[c].is(Kind::Punct, "(");
                        if (paren)
                        {
                            c = skipBlank(in, c + 1);
                        }
                        if (c < to && in[c].kind == Kind::Ident)
                        {
                            const size_t d = skipBlank(in, c + 1);
                            if (d < to && in[d].is(Kind::Punct, "["))
                            {
                                // A bracket closed past `to` is not closed
                                // within it.
                                size_t e = matching(in, d, "[", "]");
                                if (e != std::string::npos && e >= to)
                                {
                                    e = std::string::npos;
                                }
                                size_t after = e == std::string::npos ? e : e + 1;
                                if (paren && after != std::string::npos)
                                {
                                    const size_t f = skipBlank(in, after);
                                    after          = f < to && in[f].is(Kind::Punct, ")") ? f + 1 : std::string::npos;
                                }
                                if (after != std::string::npos)
                                {
                                    mAny = true;
                                    out.push_back(synth(Kind::Ident, reader, t));
                                    out.push_back(synth(Kind::Punct, "(", t));
                                    out.push_back(in[c]);
                                    out.push_back(synth(Kind::Punct, ",", t));
                                    out.push_back(synth(Kind::Space, " ", t));
                                    reads(in, d + 1, e, out);
                                    if (mStopped)
                                    {
                                        return;
                                    }
                                    out.push_back(synth(Kind::Punct, ")", t));
                                    i = after;
                                    continue;
                                }
                            }
                        }
                    }
                }
                out.push_back(t);
                ++i;
            }
        }

        Engine& mEngine;
        S32     mNested  = 0;
        bool    mStopped = false;
        bool    mAny     = false;
    };

    // Comments gone, each run of blanks one space or one newline, and no
    // space where two tokens stay two tokens without it.
    Tokens compress(const Tokens& in, bool lua)
    {
        const auto needsSpace = [&](const Token& p, const Token& t) {
            const bool pWord = p.kind == Kind::Ident || p.kind == Kind::Number;
            const bool tWord = t.kind == Kind::Ident || t.kind == Kind::Number;
            if (pWord && tWord)
            {
                return true;
            }
            if (p.kind == Kind::Other || t.kind == Kind::Other)
            {
                return true;
            }
            if (p.kind == Kind::Number && !t.text.empty() && t.text[0] == '.')
            {
                return true;
            }
            if (p.kind == Kind::Punct && p.text.back() == '.' && t.kind == Kind::Number)
            {
                return true;
            }
            if (p.kind == Kind::Punct && t.kind == Kind::Punct)
            {
                if (punctJoins(lua, p.text, t.text))
                {
                    return true;
                }
                const std::string joined = p.text + t.text;
                if (lua ? joined.compare(0, 2, "--") == 0 : (joined.compare(0, 2, "//") == 0 || joined.compare(0, 2, "/*") == 0))
                {
                    return true;
                }
            }
            return false;
        };
        Tokens out;
        bool   newline = false;
        bool   blank   = false;
        for (const Token& t : in)
        {
            if (t.kind == Kind::Newline)
            {
                newline = true;
                continue;
            }
            if (t.blank())
            {
                blank = true;
                continue;
            }
            if (!out.empty())
            {
                if (newline)
                {
                    out.push_back(synth(Kind::Newline, "\n", t));
                }
                else if (needsSpace(out.back(), t))
                {
                    out.push_back(synth(Kind::Space, " ", t));
                }
            }
            out.push_back(t);
            newline = false;
            blank   = false;
        }
        if (newline && !out.empty())
        {
            out.push_back(synth(Kind::Newline, "\n", out.back()));
        }
        (void)blank;
        return out;
    }

    // ---- the plugin's require, for SLua ----------------------------------------------

    // Every `require("name")` resolved to a module, each module processed
    // once with its own requires resolved first, and the call replaced by
    // a lookup in a table the modules fill at the top of the text.
    class Requires
    {
    public:
        Requires(Engine& engine, const ALPreprocessor::Options& options, ALPreprocessor::Result& result)
            : mEngine(engine), mOptions(options), mResult(result)
        {
        }

        void gather(Tokens& tokens, const std::string& from)
        {
            Tokens out;
            size_t i = 0;
            while (i < tokens.size())
            {
                const Token& t = tokens[i];
                if (t.is(Kind::Ident, "require") && !indexed(out))
                {
                    const size_t a = skipBlank(tokens, i + 1);
                    const size_t b = a < tokens.size() && tokens[a].is(Kind::Punct, "(") ? skipBlank(tokens, a + 1) : tokens.size();
                    const size_t c = b < tokens.size() && tokens[b].kind == Kind::String ? skipBlank(tokens, b + 1) : tokens.size();
                    if (c < tokens.size() && tokens[c].is(Kind::Punct, ")") && tokens[b].text.size() >= 2 &&
                        (tokens[b].text.front() == '"' || tokens[b].text.front() == '\''))
                    {
                        const std::string name = tokens[b].text.substr(1, tokens[b].text.size() - 2);
                        std::string       key;
                        if (resolve(name, from, t, key))
                        {
                            out.push_back(synth(Kind::Ident, "__modules", t));
                            out.push_back(synth(Kind::Punct, "[", t));
                            out.push_back(synth(Kind::String, literalOf(key), t));
                            out.push_back(synth(Kind::Punct, "]", t));
                            i = c + 1;
                            continue;
                        }
                    }
                }
                out.push_back(t);
                ++i;
            }
            tokens.swap(out);
        }

        // The table and its modules, ahead of the script; nothing where
        // nothing was required.
        Tokens prologue() const
        {
            Tokens out;
            if (mModules.empty())
            {
                return out;
            }
            // Nothing of the script's: the table maps to no line of it, so
            // nothing said about the table is said about the script's first.
            Token site;
            site.verbatim = false;
            site.file     = -1;
            const auto word = [&](Kind kind, const std::string& text) { out.push_back(synth(kind, text, site)); };
            word(Kind::Ident, "local");
            word(Kind::Space, " ");
            word(Kind::Ident, "__modules");
            word(Kind::Space, " ");
            word(Kind::Punct, "=");
            word(Kind::Space, " ");
            word(Kind::Punct, "{");
            word(Kind::Punct, "}");
            word(Kind::Newline, "\n");
            for (const auto& module : mModules)
            {
                word(Kind::Ident, "__modules");
                word(Kind::Punct, "[");
                word(Kind::String, literalOf(module.first));
                word(Kind::Punct, "]");
                word(Kind::Space, " ");
                word(Kind::Punct, "=");
                word(Kind::Space, " ");
                word(Kind::Punct, "(");
                word(Kind::Ident, "function");
                word(Kind::Punct, "(");
                word(Kind::Punct, ")");
                word(Kind::Newline, "\n");
                append(out, module.second);
                if (out.back().kind != Kind::Newline)
                {
                    word(Kind::Newline, "\n");
                }
                word(Kind::Ident, "end");
                word(Kind::Punct, ")");
                word(Kind::Punct, "(");
                word(Kind::Punct, ")");
                word(Kind::Newline, "\n");
            }
            return out;
        }

    private:
        // Whether the word last put out, past blanks, is a `.` or a `:`:
        // `t.require("x")` and `t:require("x")` are a table's, not the
        // global that finds a module.
        static bool indexed(const Tokens& before)
        {
            for (auto it = before.rbegin(); it != before.rend(); ++it)
            {
                if (!it->blank())
                {
                    return it->is(Kind::Punct, ".") || it->is(Kind::Punct, ":");
                }
            }
            return false;
        }

        bool resolve(const std::string& name, const std::string& from, const Token& at, std::string& key)
        {
            ALPreprocessor::Ask ask;
            ask.name    = name;
            ask.require = true;
            ask.from    = from;
            ALPreprocessor::Include      found;
            const ALPreprocessor::Found answer = mOptions.resolve ? mOptions.resolve(ask, found) : ALPreprocessor::Found::No;
            if (answer == ALPreprocessor::Found::Pending)
            {
                if (mPendingNames.insert(name).second)
                {
                    mResult.pending.push_back(name);
                }
                return false;
            }
            if (answer == ALPreprocessor::Found::No)
            {
                mEngine.problem(ALScriptProblem::Severity::Error, "PreprocModuleNotFound", "could not find module '[1]'", { name }, at);
                return false;
            }
            key = found.path.empty() ? name : found.path;
            mResult.resolved.push_back({ from, name, true, key });
            if (mDone.count(key))
            {
                return true;
            }
            if (mInProgress.count(key))
            {
                mEngine.problem(ALScriptProblem::Severity::Error, "PreprocRequiresItself", "'[1]' requires itself", { name }, at);
                return true;
            }
            if (S32(mInProgress.size()) >= mOptions.includeDepth)
            {
                mEngine.problem(ALScriptProblem::Severity::Error, "PreprocRequireTooDeep", "require nested too deeply at '[1]'", { name }, at);
                return false;
            }
            if (mListed.insert(key).second)
            {
                mResult.includes.push_back(key);
            }
            mInProgress.insert(key);
            Tokens body;
            mEngine.module(found, body);
            // Kept as the run made it, its requires still calls, where the
            // analyzers want each module apart.
            if (mOptions.apart)
            {
                mApart.emplace_back(key, body);
            }
            gather(body, key);
            mInProgress.erase(key);
            mDone.insert(key);
            mModules.emplace_back(key, std::move(body));
            return true;
        }

        Engine&                                    mEngine;
        const ALPreprocessor::Options&             mOptions;
        ALPreprocessor::Result&                    mResult;
        boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> mPendingNames;
        boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> mInProgress;
        boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> mDone;
        boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> mListed;
        std::vector<std::pair<std::string, Tokens>> mModules;
        // Each module as the run made it, before its requires became
        // lookups in the table (Options::apart).
        std::vector<std::pair<std::string, Tokens>> mApart;

    public:
        const std::vector<std::pair<std::string, Tokens>>& apart() const { return mApart; }
    };

    // ---- the text and its map -----------------------------------------------------------

    // A problem positioned in the output, moved back to the source.
    void mapProblem(ALScriptProblem& p, const ALSourceMap& map)
    {
        const ALSourceMap::Loc loc = map.toSource(p.line, p.column);
        if (!loc.found())
        {
            return;
        }
        const ALSourceMap::Loc end = map.toSource(p.endLine, p.endColumn);
        p.line                     = loc.line;
        p.column                   = loc.column;
        if (end.found() && end.file == loc.file && (end.line > loc.line || (end.line == loc.line && end.column >= loc.column)))
        {
            p.endLine   = end.line;
            p.endColumn = end.column;
        }
        else
        {
            p.endLine   = loc.line;
            p.endColumn = loc.column;
        }
        if (loc.file > 0 && loc.file < S32(map.files().size()))
        {
            p.file = map.files()[loc.file].path;
        }
    }

    void assemble(const Tokens& tokens, ALPreprocessor::Result& result)
    {
        S32 line   = 0;
        S32 column = 0;
        for (const Token& t : tokens)
        {
            // What came from no file -- the module table -- maps nowhere.
            if (t.kind != Kind::Newline && t.file >= 0)
            {
                // A token over several lines -- a block comment, a long
                // string -- is a segment on each of them, so that every
                // line of it maps back: its own lines in turn where it is
                // the file's text, the invocation where a macro made it.
                size_t start = 0;
                for (S32 piece = 0;; ++piece)
                {
                    const size_t newline = t.text.find('\n', start);
                    const size_t end     = newline == std::string::npos ? t.text.size() : newline;
                    if (piece == 0 || end > start)
                    {
                        ALSourceMap::Segment s;
                        s.outLine   = line + piece;
                        s.outColumn = piece == 0 ? column : 0;
                        s.length    = S32(end - start);
                        s.file      = t.file;
                        s.line      = t.verbatim ? t.line + piece : t.line;
                        s.column    = t.verbatim && piece > 0 ? 0 : t.column;
                        s.verbatim  = t.verbatim;
                        result.map.add(s);
                    }
                    if (newline == std::string::npos)
                    {
                        break;
                    }
                    start = newline + 1;
                }
            }
            for (char c : t.text)
            {
                if (c == '\n')
                {
                    ++line;
                    column = 0;
                }
                else
                {
                    ++column;
                }
            }
            result.text += t.text;
        }
        result.map.finish();
    }

    std::string stamp(S64 when, const char* format)
    {
        const std::time_t t = std::time_t(when);
        std::tm           tm;
#if LL_WINDOWS
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buffer[64];
        const size_t n = std::strftime(buffer, sizeof(buffer), format, &tm);
        return std::string(buffer, n);
    }
} // namespace

bool ALPreprocessor::Result::hasErrors() const
{
    for (const ALScriptProblem& p : problems)
    {
        if (p.severity == ALScriptProblem::Severity::Error)
        {
            return true;
        }
    }
    return false;
}

namespace
{
    // Every line of a text mapped to itself: what the source is when
    // nothing was done to it.
    void asItIs(const std::string& text, const std::string& name, ALSourceMap& map)
    {
        map = ALSourceMap();
        map.addFile(name, std::string());
        S32    line  = 0;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
            {
                end = text.size();
            }
            ALSourceMap::Segment s;
            s.outLine  = line;
            s.line     = line;
            s.length   = S32(end - start);
            s.verbatim = true;
            map.add(s);
            ++line;
            start = end + 1;
        }
        map.finish();
    }
}

ALPreprocessor::Result ALPreprocessor::failed(std::string_view source, const Options& options, std::string_view why)
{
    Result result;
    result.overran = true;
    result.text    = std::string(source);
    asItIs(result.text, options.fileName, result.map);
    ALScriptProblem problem;
    problem.severity = ALScriptProblem::Severity::Error;
    problem.source   = ALScriptProblem::Source::Preprocessor;
    problem.key      = "PreprocFailed";
    problem.args     = { std::string(why) };
    problem.message  = ALScriptProblem::fill("the preprocessor could not finish ([1]); nothing was preprocessed", problem.args);
    result.problems.push_back(std::move(problem));
    return result;
}

ALPreprocessor::Result ALPreprocessor::run(std::string_view source, const Options& options)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    Result result;
    if (source.find(options.lua ? "--fspreprocessor off" : "//fspreprocessor off") != std::string_view::npos)
    {
        result.disabled = true;
        result.text     = std::string(source);
        asItIs(result.text, options.fileName, result.map);
        return result;
    }

    Engine engine(options, result);
    const S64 now = options.unixTime ? options.unixTime : S64(std::time(nullptr));
    engine.predefine("__LINE__", Macro::Dynamic::Line);
    engine.predefine("__FILE__", Macro::Dynamic::File);
    engine.predefine("__SHORTFILE__", Macro::Dynamic::ShortFile);
    engine.predefine("__ASSETID__", Macro::Dynamic::AssetId);
    engine.predefine("__DATE__ \"" + stamp(now, "%b %e %Y") + "\"");
    engine.predefine("__TIME__ \"" + stamp(now, "%H:%M:%S") + "\"");
    engine.predefine("__UNIXTIME__ " + std::to_string(now));
    if (!options.agentId.empty())
    {
        engine.predefine("__AGENTKEY__ " + literalOf(options.agentId));
        engine.predefine("__AGENTID__ " + literalOf(options.agentId));
        engine.predefine("__AGENTIDRAW__ " + options.agentId);
        engine.predefine("__AGENTNAME__ " + literalOf(options.agentName));
    }
    for (const std::string& define : options.defines)
    {
        const size_t      equals = define.find('=');
        std::string       name   = define.substr(0, equals);
        const std::string value  = equals == std::string::npos ? std::string("1") : define.substr(equals + 1);
        const size_t first = name.find_first_not_of(" \t");
        const size_t last  = name.find_last_not_of(" \t");
        name = first == std::string::npos ? std::string() : name.substr(first, last - first + 1);
        const bool identifier = !name.empty() && !isdigit(static_cast<unsigned char>(name[0])) &&
                                std::all_of(name.begin(), name.end(), [](char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_'; });
        if (identifier)
        {
            engine.predefine(name + " " + value);
        }
    }
    if (!options.lua)
    {
        for (const char* type : { "integer", "float", "string", "key", "vector", "rotation", "quaternion", "list" })
        {
            engine.predefine(std::string(type) + "(...) ((" + type + ")(__VA_ARGS__))");
        }
    }

    Tokens tokens = engine.run(source);
    // No longer wanted, before or after the expansion: the transforms and
    // the rest are not made either.
    if (!result.superseded && options.superseded && options.superseded->load(std::memory_order_relaxed))
    {
        result.overran    = true;
        result.superseded = true;
    }
    if (result.superseded)
    {
        result.text = std::string(source);
        asItIs(result.text, options.fileName, result.map);
        return result;
    }
    if (options.lua)
    {
        Requires gathered(engine, options, result);
        // The script as the run made it, its requires still calls, where
        // the analyzers want it apart from its modules.
        Tokens as_made;
        if (options.apart)
        {
            as_made = tokens;
        }
        gathered.gather(tokens, std::string());
        if (options.apart && !gathered.apart().empty())
        {
            // Each piece assembled over the same files as the whole.
            const auto piece = [&result](std::string key, const Tokens& made) {
                Result part;
                for (const ALSourceMap::File& file : result.map.files())
                {
                    part.map.addFile(file.name, file.path);
                }
                assemble(made, part);
                return Result::Piece{ std::move(key), std::move(part.text), std::move(part.map) };
            };
            result.apart.valid  = true;
            result.apart.script = piece(std::string(), as_made);
            for (const auto& [key, made] : gathered.apart())
            {
                result.apart.modules.push_back(piece(key, made));
            }
        }
        Tokens all = gathered.prologue();
        if (!all.empty())
        {
            // Luau reads its `--!strict` and the like only before the first
            // token that is not a comment -- a header of plain comments may
            // come before them -- so the script's leading comments all go
            // ahead of the module table.
            size_t head = 0;
            while (head < tokens.size() && tokens[head].blank())
            {
                ++head;
            }
            Tokens hot(tokens.begin(), tokens.begin() + head);
            tokens.erase(tokens.begin(), tokens.begin() + head);
            append(hot, all);
            all.swap(hot);
        }
        append(all, tokens);
        tokens.swap(all);
    }
    else
    {
        if (options.lazyLists || engine.usedLazyLists())
        {
            LazyLists lazy(engine);
            tokens               = lazy.run(tokens);
            result.usedLazyLists = lazy.any();
        }
        if (options.extensions || engine.usedExtensions())
        {
            // Before the switches, so that a break in a loop inside a
            // switch is the loop's and one in a switch inside a loop the
            // switch's.
            Extensions extensions(engine);
            tokens                = extensions.run(tokens);
            result.usedExtensions = extensions.any();
            result.inlined        = extensions.inlined();
        }
        if (options.switches || engine.usedSwitches())
        {
            result.usedSwitches = true;
            tokens              = Switches(engine).run(tokens);
        }
    }
    assemble(tokens, result);
    if (result.overran)
    {
        // Nothing that came of a run that ran away is to be compiled or
        // analysed: the source as it was, with what was said of it.
        result.text = std::string(source);
        asItIs(result.text, options.fileName, result.map);
        return result;
    }
    finish(result, options);
    return result;
}

void ALPreprocessor::finish(Result& result, const Options& options)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (options.lua || result.overran || result.text.empty())
    {
        return;
    }
    if (options.optimize)
    {
        optimize(result, options);
    }
    if (options.compress)
    {
        // Over whatever the text is by now, mapped back through it.
        Tokens                 again = compress(Lexer(false, 0).run(result.text), false);
        ALPreprocessor::Result squeezed;
        assemble(again, squeezed);
        result.map  = squeezed.map.composed(result.map);
        result.text = std::move(squeezed.text);
    }
}

void ALPreprocessor::optimize(Result& result, const Options& options)
{
    if (options.lua || result.overran || result.text.empty())
    {
        return;
    }
    // Over the expanded text, as Firestorm ran its own; what it says is
    // said of the expanded text and brought back to the source.
    ALLSLOptimizer::Options optimizing = options.optimizer;
    optimizing.inlineNames             = result.inlined;
    optimizing.inlining                = optimizing.inlining || !result.inlined.empty();
    ALLSLOptimizer::Result optimized   = ALLSLOptimizer::run(result.text, optimizing);
    const size_t           first_note  = result.problems.size();
    if (optimized.uncompiled)
    {
        // A script that does not compile is not the preprocessor's to
        // fail: the text goes on as it was expanded, and compiling it says
        // what is wrong, as it would with the optimizer off. The optimizer
        // says only that it stood aside, where the first error is.
        ALScriptProblem p;
        p.severity = ALScriptProblem::Severity::Note;
        p.source   = ALScriptProblem::Source::Optimizer;
        p.key      = "OptimizerUncompiled";
        p.message  = "not optimized: the script does not compile as it stands, and compiling it says why";
        if (!optimized.problems.empty())
        {
            p.line      = optimized.problems.front().line;
            p.column    = optimized.problems.front().column;
            p.endLine   = p.line;
            p.endColumn = p.column;
            mapProblem(p, result.map);
        }
        result.problems.push_back(std::move(p));
        return;
    }
    for (ALScriptProblem p : optimized.problems)
    {
        // What it did, as a change to the source where that can be said,
        // taken back with the note.
        ALScriptFixes::attachOptimizer(p, result.text);
        ALScriptFixes::mapThrough(result.map, p);
        mapProblem(p, result.map);
        if (!p.file.empty())
        {
            p.fixes.clear();
        }
        result.problems.push_back(std::move(p));
    }
    if (!optimized.optimized)
    {
        return;
    }
    const ALSourceMap after_map = optimized.map.composed(result.map);
    // What it saved in code for its target, the whole and line by line in
    // the source's places, where both texts compile: each note given what
    // the lines it stands on came to less.
    ALScriptWeight::Target target = optimizing.target == ALLSLOptimizer::Target::LSO    ? ALScriptWeight::Target::LSO
                                    : optimizing.target == ALLSLOptimizer::Target::Luau ? ALScriptWeight::Target::LSLLuau
                                                                                        : ALScriptWeight::Target::Mono;
    const auto weigh = [target](std::string_view text) {
        return target == ALScriptWeight::Target::LSO       ? ALScriptWeigh::lso(text)
               : target == ALScriptWeight::Target::LSLLuau ? ALScriptWeigh::lslLuau(text)
                                                           : ALScriptWeigh::mono(text);
    };
    bool weighed = false;
    if (options.weigh)
    {
        const ALScriptWeight before = weigh(result.text);
        const ALScriptWeight after  = weigh(optimized.text);
        weighed                     = before.total > 0 && after.total > 0;
        if (weighed)
        {
            result.codeBefore = before.total;
            result.codeAfter  = after.total;
            std::map<std::pair<std::string, S32>, S64> lost;
            for (const ALScriptWeight::Line& line : before.inSource(result.map).lines)
            {
                lost[{ line.file, line.line }] += S64(line.bytes);
            }
            for (const ALScriptWeight::Line& line : after.inSource(after_map).lines)
            {
                lost[{ line.file, line.line }] -= S64(line.bytes);
            }
            for (size_t i = first_note; i < result.problems.size(); ++i)
            {
                ALScriptProblem& note = result.problems[i];
                if (note.source != ALScriptProblem::Source::Optimizer || note.severity != ALScriptProblem::Severity::Note ||
                    note.key == "OptimizerStoppedEarly" || note.endLine < note.line)
                {
                    continue;
                }
                S64 saved = 0;
                for (auto it = lost.lower_bound({ note.file, note.line }); it != lost.end() && it->first.first == note.file && it->first.second <= note.endLine;
                     ++it)
                {
                    saved += it->second;
                }
                note.savedBytes = saved;
            }
        }
    }
    ALScriptProblem sizes;
    sizes.severity = ALScriptProblem::Severity::Note;
    sizes.source   = ALScriptProblem::Source::Optimizer;
    if (weighed)
    {
        sizes.key     = "OptimizerSizesWeighed";
        sizes.args    = { std::to_string(optimized.sizeBefore), std::to_string(optimized.sizeAfter), std::to_string(result.codeBefore),
                          std::to_string(result.codeAfter), ALScriptWeight::nameOf(target) };
        sizes.message = ALScriptProblem::fill("optimized from [1] to [2] bytes of source, and from [3] to [4] bytes of code on [5]", sizes.args);
    }
    else
    {
        sizes.key     = "OptimizerSizes";
        sizes.args    = { std::to_string(optimized.sizeBefore), std::to_string(optimized.sizeAfter) };
        sizes.message = ALScriptProblem::fill("optimized from [1] to [2] bytes of source; script memory is the simulator's to say", sizes.args);
    }
    result.problems.push_back(std::move(sizes));
    result.map       = after_map;
    result.text      = std::move(optimized.text);
    result.optimized = true;
}

std::vector<ALPreprocessor::Token> ALPreprocessor::tokenize(std::string_view text, bool lua)
{
    std::vector<Token> out;
    for (const ::Token& t : Lexer(lua, 0, true).run(text))
    {
        Token one;
        switch (t.kind)
        {
            case Kind::Ident:   one.kind = Token::Kind::Ident; break;
            case Kind::Number:  one.kind = Token::Kind::Number; break;
            case Kind::String:  one.kind = Token::Kind::String; break;
            case Kind::Punct:   one.kind = Token::Kind::Punct; break;
            case Kind::Space:   one.kind = Token::Kind::Space; break;
            case Kind::Newline: one.kind = Token::Kind::Newline; break;
            case Kind::Comment: one.kind = Token::Kind::Comment; break;
            default:            one.kind = Token::Kind::Other; break;
        }
        one.text   = t.text;
        one.line   = t.line;
        one.column = t.column;
        out.push_back(std::move(one));
    }
    return out;
}

// static
std::vector<ALPreprocessor::Required> ALPreprocessor::requiresIn(std::string_view text)
{
    // The shape Requires::gather takes: the word, `(`, a quoted string and
    // `)`, blanks and comments between them, and no `.` or `:` before it.
    const std::vector<Token> tokens = tokenize(text, true);
    const auto blank = [&tokens](size_t i) {
        const Token::Kind kind = tokens[i].kind;
        return kind == Token::Kind::Space || kind == Token::Kind::Newline || kind == Token::Kind::Comment;
    };
    const auto next = [&tokens, &blank](size_t i) {
        while (i < tokens.size() && blank(i))
        {
            ++i;
        }
        return i;
    };
    const auto is = [&tokens](size_t i, Token::Kind kind, std::string_view word) {
        return i < tokens.size() && tokens[i].kind == kind && tokens[i].text == word;
    };
    std::vector<Required> out;
    bool                  indexed = false;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        if (blank(i))
        {
            continue;
        }
        const bool field = indexed;
        indexed          = is(i, Token::Kind::Punct, ".") || is(i, Token::Kind::Punct, ":");
        if (field || !is(i, Token::Kind::Ident, "require"))
        {
            continue;
        }
        const size_t open = next(i + 1);
        const size_t name = is(open, Token::Kind::Punct, "(") ? next(open + 1) : tokens.size();
        const size_t shut = name < tokens.size() ? next(name + 1) : tokens.size();
        if (!is(shut, Token::Kind::Punct, ")") || tokens[name].kind != Token::Kind::String)
        {
            continue;
        }
        const std::string& quoted = tokens[name].text;
        if (quoted.size() < 2 || (quoted.front() != '"' && quoted.front() != '\''))
        {
            continue;
        }
        Required one;
        one.line      = tokens[i].line;
        one.column    = tokens[i].column;
        one.endLine   = tokens[shut].line;
        one.endColumn = tokens[shut].column + 1;
        one.name      = quoted.substr(1, quoted.size() - 2);
        out.push_back(std::move(one));
    }
    return out;
}

// static
ALPreprocessor::Transform ALPreprocessor::transformAt(const std::function<std::string_view(S32)>& line, S32 count, S32 at, std::string& word)
{
    const auto isWord = [](char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    const auto blank  = std::string_view(" \t");
    // The line's first statement's shape, and its word.
    const auto shapeOf = [&](S32 index) {
        const std::string_view text = line(index);
        size_t                 from = text.find_first_not_of(blank);
        // Past a closing or opening brace and a statement's end, which the
        // words may follow on the same line.
        while (from != std::string_view::npos && (text[from] == '{' || text[from] == '}' || text[from] == ';'))
        {
            from = text.find_first_not_of(blank, from + 1);
        }
        if (from == std::string_view::npos || !isWord(text[from]))
        {
            return Transform::None;
        }
        size_t end = from;
        while (end < text.size() && isWord(text[end]))
        {
            ++end;
        }
        word                   = std::string(text.substr(from, end - from));
        const size_t rest      = text.find_first_not_of(blank, end);
        const char   following = rest == std::string_view::npos ? '\0' : text[rest];
        if (word == "switch")
        {
            return following == '(' ? Transform::Switch : Transform::None;
        }
        if (word == "case")
        {
            // `case <what>:` -- the colon somewhere after, and the word
            // not used as a name would be: `case = 1;`, `case(`.
            const bool named = following == '=' || following == '(' || following == '.' || following == ';';
            return !named && text.find(':', end) != std::string_view::npos ? Transform::Switch : Transform::None;
        }
        if (word == "break" || word == "continue")
        {
            return following == ';' || isdigit(static_cast<unsigned char>(following)) ? Transform::Extensions : Transform::None;
        }
        if (word == "inline")
        {
            // `inline name(` or `inline type name(`.
            size_t k = rest;
            for (int words = 0; words < 2 && k != std::string_view::npos && k < text.size() && isWord(text[k]); ++words)
            {
                while (k < text.size() && isWord(text[k]))
                {
                    ++k;
                }
                k = text.find_first_not_of(blank, k);
                if (k != std::string_view::npos && text[k] == '(')
                {
                    return Transform::Extensions;
                }
            }
        }
        return Transform::None;
    };
    if (at < 0 || at >= count)
    {
        return Transform::None;
    }
    const Transform here = shapeOf(at);
    if (here != Transform::None)
    {
        return here;
    }
    // A brace on its own: the switch it opens is on the line before, past
    // blank lines.
    const std::string_view text  = line(at);
    const size_t           first = text.find_first_not_of(blank);
    if (first == std::string_view::npos || text[first] != '{')
    {
        return Transform::None;
    }
    S32 back = at - 1;
    while (back >= 0 && line(back).find_first_not_of(blank) == std::string_view::npos)
    {
        --back;
    }
    return back >= 0 && shapeOf(back) == Transform::Switch ? Transform::Switch : Transform::None;
}
