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

    typedef std::set<std::string>                 HideSet;
    typedef std::shared_ptr<const HideSet>        hide_set_ptr;

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
                take();
                while (mPos < mText.size())
                {
                    const char d = at(mPos);
                    if (d == '\\' && mPos + 1 < mText.size())
                    {
                        take();
                        take();
                    }
                    else if (d == c)
                    {
                        take();
                        break;
                    }
                    else if (d == '\n')
                    {
                        // Luau's error; the string ends where the line does.
                        break;
                    }
                    else
                    {
                        take();
                    }
                }
                finish();
            }
            else if (c == '`')
            {
                start(Kind::String);
                take();
                S32 depth = 0;
                while (mPos < mText.size())
                {
                    const char d = at(mPos);
                    if (d == '\\' && mPos + 1 < mText.size())
                    {
                        take();
                        take();
                    }
                    else if (d == '{')
                    {
                        ++depth;
                        take();
                    }
                    else if (d == '}' && depth > 0)
                    {
                        --depth;
                        take();
                    }
                    else if (d == '`' && depth == 0)
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
            }
            else
            {
                common();
            }
        }

        bool             mLua;
        S32              mFile;
        bool             mVerbatim;
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

        // A macro of the run's own: `NAME body` or `NAME(params) body` as
        // a define line would have it, or one whose value is where it
        // stands.
        void predefine(const std::string& definition);
        void predefine(const std::string& name, Macro::Dynamic dynamic);

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

        // -- expansion --
        void   handle(const Token& t);
        bool   expandDynamic(const Token& t, const Macro& m);
        bool   expandObject(const Token& t, const Macro& m);
        bool   expandFunction(const Token& t, const Macro& m);
        Tokens substitute(const Macro& m, const std::vector<Tokens>& args, const Token& site, const hide_set_ptr& hs);
        Tokens expandAll(const Tokens& in);
        Token  stringize(const Tokens& arg, const Token& site) const;
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
        std::map<std::string, Macro>            mMacros;
        std::set<std::string>                   mOnce;
        std::set<std::string>                   mPendingNames;
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
            problem(ALScriptProblem::Severity::Error, "#if without #endif at the end of the file", at);
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
        while (mFiles.size() > depth)
        {
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
                out.text = "\"" + f.name + "\"";
                break;
            case Macro::Dynamic::AssetId:
                out.kind = Kind::String;
                out.text = "\"" + (f.assetId.empty() ? std::string("NOT_IN_WORLD") : f.assetId) + "\"";
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
            problem(ALScriptProblem::Severity::Error, "unterminated argument list invoking macro '" + m.name + "'", t);
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
            problem(ALScriptProblem::Severity::Error,
                    (args.size() < m.params.size() ? "too few arguments for macro '" : "too many arguments for macro '") + m.name + "'", t);
            emit(t);
            for (const Token& r : read)
            {
                emit(r);
            }
            return true;
        }
        const hide_set_ptr hs  = hideUnion(hideIntersect(t.hide, rparen.hide), { m.name });
        Tokens             out = substitute(m, args, t, hs);
        unread(out);
        return true;
    }

    Token Engine::stringize(const Tokens& arg, const Token& site) const
    {
        std::string text = "\"";
        bool        space = false;
        for (const Token& t : arg)
        {
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
        Tokens lexed = Lexer(mOptions.lua, site.file).run(left.text + right.text);
        if (lexed.size() != 1 || lexed[0].blank())
        {
            problem(ALScriptProblem::Severity::Error, "pasting '" + left.text + "' and '" + right.text + "' does not give a valid token", site);
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
                    out.push_back(stringize(args[idx], site));
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
                        out.insert(out.end(), args[idx].begin(), args[idx].end());
                    }
                }
                else
                {
                    if (!expanded[idx])
                    {
                        expanded[idx] = expandAll(args[idx]);
                    }
                    out.insert(out.end(), expanded[idx]->begin(), expanded[idx]->end());
                }
                continue;
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
            t.hide = hideUnion(t.hide, *hs);
            result.push_back(t);
        }
        return result;
    }

    Tokens Engine::expandAll(const Tokens& in)
    {
        // The list on its own: nothing of the file behind it, and its
        // output to a list of its own.
        std::deque<Token> pending;
        pending.swap(mPending);
        const bool isolated = mIsolated;
        mIsolated           = true;
        Tokens  out;
        Tokens* sink = mSink;
        mSink        = &out;
        mPending.assign(in.begin(), in.end());
        Token t;
        while (next(t))
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
        Expr(const Tokens& tokens, bool lua, Engine& engine, const Token& at) : mTokens(tokens), mLua(lua), mEngine(engine), mAt(at) {}

        S64 parse(bool& ok)
        {
            mOk = true;
            if (!peek())
            {
                fail("#if with no expression");
            }
            S64 v = ternary();
            if (mOk && peek())
            {
                fail("unexpected '" + peek()->text + "' in preprocessor expression");
            }
            ok = mOk;
            return mOk ? v : 0;
        }

    private:
        void fail(const std::string& message)
        {
            if (mOk)
            {
                mEngine.problem(ALScriptProblem::Severity::Error, message, mAt);
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
            S64 c = binary(1);
            if (accept("?"))
            {
                S64 a = ternary();
                if (!accept(":"))
                {
                    fail("expected ':' in preprocessor expression");
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
                    fail("division by zero in preprocessor expression");
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
                fail("expected a value in preprocessor expression");
                return 0;
            }
            if (t->kind == Kind::Number)
            {
                ++mI;
                bool ok = false;
                S64  v  = parseInteger(t->text, ok);
                if (!ok)
                {
                    fail("'" + t->text + "' is not an integer constant");
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
                S64 v = ternary();
                if (!accept(")"))
                {
                    fail("expected ')' in preprocessor expression");
                }
                return v;
            }
            fail("unexpected '" + t->text + "' in preprocessor expression");
            return 0;
        }

        const Tokens& mTokens;
        bool          mLua;
        Engine&       mEngine;
        const Token&  mAt;
        size_t        mI  = 0;
        bool          mOk = true;
    };

    S64 Engine::evalExpression(const Tokens& tokens, const Token& hash, bool& ok)
    {
        return Expr(tokens, mOptions.lua, *this, hash).parse(ok);
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
                    problem(ALScriptProblem::Severity::Error, "macro name missing after 'defined'", hash);
                    return false;
                }
                size_t k = j;
                if (paren)
                {
                    k = skipBlank(line, j + 1);
                    if (k >= line.size() || !line[k].is(Kind::Punct, ")"))
                    {
                        problem(ALScriptProblem::Severity::Error, "missing ')' after 'defined'", hash);
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
            problem(ALScriptProblem::Severity::Error, "macro name missing in #define", hash);
            return;
        }
        Macro m;
        m.name = line[at].text;
        if (m.name == "defined")
        {
            problem(ALScriptProblem::Severity::Error, "'defined' cannot be used as a macro name", line[at]);
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
                        problem(ALScriptProblem::Severity::Error, "duplicate macro parameter '" + line[i].text + "'", line[i]);
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
                problem(ALScriptProblem::Severity::Error, "ill formed parameter list of macro '" + m.name + "'", line[at]);
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
            problem(ALScriptProblem::Severity::Error, "'##' cannot be at either end of a macro body", line[at]);
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
                        problem(ALScriptProblem::Severity::Error, "'#' is not followed by a macro parameter", line[at]);
                        return;
                    }
                }
            }
        }
        auto it = mMacros.find(m.name);
        if (it != mMacros.end() && it->second.dynamic == Macro::Dynamic::None && !it->second.sameAs(m))
        {
            problem(ALScriptProblem::Severity::Warning, "macro '" + m.name + "' redefined", line[at]);
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
                problem(ALScriptProblem::Severity::Error, "missing '>' in #include", hash);
                return;
            }
            ask.name = trim(ask.name);
        }
        else
        {
            problem(ALScriptProblem::Severity::Error, "ill formed #include directive", hash);
            return;
        }
        ask.from = mFiles.back()->path;
        if (S32(mFiles.size()) > mOptions.includeDepth)
        {
            problem(ALScriptProblem::Severity::Error, "#include nested too deeply at '" + ask.name + "'", hash);
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
                problem(ALScriptProblem::Severity::Error, "could not find include file '" + ask.name + "'", hash);
                return;
            case ALPreprocessor::Found::Yes:
                break;
        }
        const std::string identity = found.path.empty() ? ask.name : found.path;
        if (mOnce.count(identity))
        {
            return;
        }
        if (std::find(mResult.includes.begin(), mResult.includes.end(), identity) == mResult.includes.end())
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
                    problem(ALScriptProblem::Severity::Error, "macro name missing in #" + name, hash);
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
                problem(ALScriptProblem::Severity::Error, "#elif without #if", hash);
                return;
            }
            Cond& c = f.conds.back();
            if (c.seenElse)
            {
                problem(ALScriptProblem::Severity::Error, "#elif after #else", hash);
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
                problem(ALScriptProblem::Severity::Error, "#else without #if", hash);
                return;
            }
            Cond& c = f.conds.back();
            if (c.seenElse)
            {
                problem(ALScriptProblem::Severity::Error, "#else after #else", hash);
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
                problem(ALScriptProblem::Severity::Error, "#endif without #if", hash);
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
                problem(ALScriptProblem::Severity::Error, "macro name missing in #undef", hash);
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
            problem(ALScriptProblem::Severity::Error, "ill formed preprocessor directive '#" + (name.empty() ? line[i].text : name) + "'", hash);
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
            Tokens out = assignments(in);
            mScopes.clear();
            return statements(out, 0, out.size());
        }

        bool any() const { return mCounter > 0 || mAssignments > 0; }

    private:
        struct Scope
        {
            bool        loop = false;
            std::string breakLabel;
            std::string continueLabel;
            bool        breakUsed    = false;
            bool        continueUsed = false;
        };

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
                    mEngine.problem(ALScriptProblem::Severity::Error, std::string("no variable before '") + op + "'", t);
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
        // what they govern; anything else to its semicolon.
        size_t statementEnd(const Tokens& t, size_t i) const
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
                size_t end = statementEnd(t, close + 1);
                if (t[i].is(Kind::Ident, "if"))
                {
                    const size_t e = skipBlank(t, end);
                    if (e < t.size() && t[e].is(Kind::Ident, "else"))
                    {
                        end = statementEnd(t, e + 1);
                    }
                }
                return end;
            }
            if (t[i].is(Kind::Ident, "do"))
            {
                const size_t body_end = statementEnd(t, i + 1);
                return semicolonAfter(t, body_end);
            }
            return semicolonAfter(t, i);
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
        // each statement in turn.
        Tokens statements(const Tokens& in, size_t from, size_t to)
        {
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
                    append(out, loop(in, i, end));
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
                                mEngine.problem(ALScriptProblem::Severity::Error, std::string(is_break ? "break" : "continue") + " outside a loop", t);
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
        // and the labels put where the jumps expect them.
        Tokens loop(const Tokens& in, size_t i, size_t end)
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
            mScopes.push_back(scope);
            Tokens body = statements(in, body_begin, body_end);
            const Scope done = mScopes.back();
            mScopes.pop_back();
            Tokens out = slice(in, i, body_begin);
            if (done.continueUsed)
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
            if (done.breakUsed)
            {
                out.push_back(synth(Kind::Punct, "@", site));
                out.push_back(synth(Kind::Ident, done.breakLabel, site));
                out.push_back(synth(Kind::Punct, ";", site));
            }
            return out;
        }

        Engine&            mEngine;
        std::vector<Scope> mScopes;
        S32                mCounter     = 0;
        S32                mAssignments = 0;
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

        Tokens run(const Tokens& in)
        {
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
                                append(out, build(in[i], slice(in, j, k + 1), run(slice(in, l + 1, m))));
                                i = m + 1;
                                continue;
                            }
                        }
                    }
                    mEngine.problem(ALScriptProblem::Severity::Error, "ill formed switch statement", in[i]);
                }
                out.push_back(in[i]);
                ++i;
            }
            return out;
        }

    private:
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
                        mEngine.problem(ALScriptProblem::Severity::Error, "cannot find ':' or '{' after case", t);
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
        Tokens run(const Tokens& in)
        {
            Tokens out = reads(assignments(in));
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

        Tokens reads(const Tokens& in)
        {
            Tokens out;
            size_t i = 0;
            while (i < in.size())
            {
                const Token& t = in[i];
                if (t.is(Kind::Punct, "("))
                {
                    const size_t a      = skipBlank(in, i + 1);
                    const char*  reader = a < in.size() && in[a].kind == Kind::Ident ? readerFor(in[a].text) : nullptr;
                    const size_t b      = reader ? skipBlank(in, a + 1) : in.size();
                    if (reader && b < in.size() && in[b].is(Kind::Punct, ")"))
                    {
                        size_t     c     = skipBlank(in, b + 1);
                        const bool paren = c < in.size() && in[c].is(Kind::Punct, "(");
                        if (paren)
                        {
                            c = skipBlank(in, c + 1);
                        }
                        if (c < in.size() && in[c].kind == Kind::Ident)
                        {
                            const size_t d = skipBlank(in, c + 1);
                            if (d < in.size() && in[d].is(Kind::Punct, "["))
                            {
                                const size_t e = matching(in, d, "[", "]");
                                size_t       after = e == std::string::npos ? e : e + 1;
                                if (paren && after != std::string::npos)
                                {
                                    const size_t f = skipBlank(in, after);
                                    after          = f < in.size() && in[f].is(Kind::Punct, ")") ? f + 1 : std::string::npos;
                                }
                                if (after != std::string::npos)
                                {
                                    mAny = true;
                                    out.push_back(synth(Kind::Ident, reader, t));
                                    out.push_back(synth(Kind::Punct, "(", t));
                                    out.push_back(in[c]);
                                    out.push_back(synth(Kind::Punct, ",", t));
                                    out.push_back(synth(Kind::Space, " ", t));
                                    append(out, reads(slice(in, d + 1, e)));
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
            return out;
        }

        bool mAny = false;
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
                if (t.is(Kind::Ident, "require"))
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
                            out.push_back(synth(Kind::String, "\"" + key + "\"", t));
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
            Token site;
            site.verbatim = false;
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
                word(Kind::String, "\"" + module.first + "\"");
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
                mEngine.problem(ALScriptProblem::Severity::Error, "could not find module '" + name + "'", at);
                return false;
            }
            key = found.path.empty() ? name : found.path;
            if (mDone.count(key))
            {
                return true;
            }
            if (mInProgress.count(key))
            {
                mEngine.problem(ALScriptProblem::Severity::Error, "'" + name + "' requires itself", at);
                return true;
            }
            if (S32(mInProgress.size()) >= mOptions.includeDepth)
            {
                mEngine.problem(ALScriptProblem::Severity::Error, "require nested too deeply at '" + name + "'", at);
                return false;
            }
            if (std::find(mResult.includes.begin(), mResult.includes.end(), key) == mResult.includes.end())
            {
                mResult.includes.push_back(key);
            }
            mInProgress.insert(key);
            Tokens body;
            mEngine.module(found, body);
            gather(body, key);
            mInProgress.erase(key);
            mDone.insert(key);
            mModules.emplace_back(key, std::move(body));
            return true;
        }

        Engine&                                    mEngine;
        const ALPreprocessor::Options&             mOptions;
        ALPreprocessor::Result&                    mResult;
        std::set<std::string>                      mPendingNames;
        std::set<std::string>                      mInProgress;
        std::set<std::string>                      mDone;
        std::vector<std::pair<std::string, Tokens>> mModules;
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
            if (t.kind != Kind::Newline)
            {
                ALSourceMap::Segment s;
                s.outLine   = line;
                s.outColumn = column;
                s.length    = S32(t.text.size());
                s.file      = t.file;
                s.line      = t.line;
                s.column    = t.column;
                s.verbatim  = t.verbatim;
                result.map.add(s);
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

ALPreprocessor::Result ALPreprocessor::run(std::string_view source, const Options& options)
{
    Result result;
    if (source.find(options.lua ? "--fspreprocessor off" : "//fspreprocessor off") != std::string_view::npos)
    {
        result.disabled = true;
        result.text     = std::string(source);
        result.map.addFile(options.fileName, std::string());
        S32    line  = 0;
        size_t start = 0;
        while (start <= source.size())
        {
            size_t end = source.find('\n', start);
            if (end == std::string_view::npos)
            {
                end = source.size();
            }
            ALSourceMap::Segment s;
            s.outLine  = line;
            s.line     = line;
            s.length   = S32(end - start);
            s.verbatim = true;
            result.map.add(s);
            ++line;
            start = end + 1;
        }
        result.map.finish();
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
        engine.predefine("__AGENTKEY__ \"" + options.agentId + "\"");
        engine.predefine("__AGENTID__ \"" + options.agentId + "\"");
        engine.predefine("__AGENTIDRAW__ " + options.agentId);
        engine.predefine("__AGENTNAME__ \"" + options.agentName + "\"");
    }
    if (!options.lua)
    {
        for (const char* type : { "integer", "float", "string", "key", "vector", "rotation", "quaternion", "list" })
        {
            engine.predefine(std::string(type) + "(...) ((" + type + ")(__VA_ARGS__))");
        }
    }

    Tokens tokens = engine.run(source);
    if (options.lua)
    {
        Requires gathered(engine, options, result);
        gathered.gather(tokens, std::string());
        Tokens all = gathered.prologue();
        if (!all.empty())
        {
            // Luau reads its `--!strict` and the like only at the top, so
            // the script's leading ones go ahead of the module table.
            size_t head = 0;
            while (head < tokens.size() && (tokens[head].blank() && (tokens[head].kind != Kind::Comment || tokens[head].text.compare(0, 3, "--!") == 0)))
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
            LazyLists lazy;
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
        }
        if (options.switches || engine.usedSwitches())
        {
            result.usedSwitches = true;
            tokens              = Switches(engine).run(tokens);
        }
    }
    assemble(tokens, result);
    if (options.lua)
    {
        return result;
    }
    if (options.optimize)
    {
        // Over the expanded text, as Firestorm ran its own; what it says
        // is said of the expanded text and brought back to the source.
        ALLSLOptimizer::Result optimized = ALLSLOptimizer::run(result.text, options.optimizer);
        for (ALScriptProblem p : optimized.problems)
        {
            mapProblem(p, result.map);
            result.problems.push_back(std::move(p));
        }
        if (optimized.optimized)
        {
            ALScriptProblem sizes;
            sizes.severity = ALScriptProblem::Severity::Note;
            sizes.source   = ALScriptProblem::Source::Optimizer;
            sizes.message  = "optimized from " + std::to_string(optimized.sizeBefore) + " to " + std::to_string(optimized.sizeAfter) +
                            " bytes of source; script memory is the simulator's to say";
            result.problems.push_back(std::move(sizes));
            result.map       = optimized.map.composed(result.map);
            result.text      = std::move(optimized.text);
            result.optimized = true;
        }
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
    return result;
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
