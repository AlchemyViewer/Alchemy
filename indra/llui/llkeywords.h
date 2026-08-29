/**
 * @file llkeywords.h
 * @brief Keyword list for LSL
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLKEYWORDS_H
#define LL_LLKEYWORDS_H


#include "lldir.h"
#include "llstyle.h"
#include "llstring.h"
#include "v3color.h"
#include "v4color.h"
#include <map>
#include <list>
#include <deque>
#include <vector>
#include <cstddef>
#include <boost/unordered_map.hpp>
#include "llpointer.h"

class LLTextSegment;
typedef LLPointer<LLTextSegment> LLTextSegmentPtr;

class LLKeywordToken
{
public:
    /**
     * @brief Types of tokens/delimters being parsed.
     *
     * @desc Tokens/delimiters that need to be identified/highlighted. All are terminated if an EOF is encountered.
     * - TT_WORD are keywords in the normal sense, i.e. constants, events, etc.
     * - TT_LINE are for entire lines (currently only flow control labels use this).
     * - TT_ONE_SIDED_DELIMITER are for open-ended delimiters which are terminated by EOL.
     * - TT_TWO_SIDED_DELIMITER are for delimiters that end with a different delimiter than they open with.
     * - TT_DOUBLE_QUOTATION_MARKS are for delimiting areas using the same delimiter to open and close.
     * - TT_LONG_BRACKET are for Lua tokens that use brackets with counted equals signs.
     */
    typedef enum e_token_type
    {
        TT_UNKNOWN,
        TT_WORD,
        TT_LINE,
        TT_TWO_SIDED_DELIMITER,
        TT_ONE_SIDED_DELIMITER,
        TT_DOUBLE_QUOTATION_MARKS,
        TT_LONG_BRACKET,                    // Lua long brackets: --[=*[ or [=*[
        // Following constants are more specific versions of the preceding ones
        TT_CONSTANT,                        // WORD
        TT_CONTROL,                         // WORD
        TT_EVENT,                           // WORD
        TT_FUNCTION,                        // WORD
        TT_LABEL,                           // LINE
        TT_SECTION,                         // WORD
        TT_TYPE                             // WORD
    } ETokenType;

    LLKeywordToken( ETokenType type, const LLUIColor& color, const std::string& token, const std::string& tool_tip, const std::string& delimiter  )
        :
        mType( type ),
        mToken( token ),
        mColor( color ),
        mToolTip( tool_tip ),
        mDelimiter( delimiter )     // right delimiter
    {
    }

    ~LLKeywordToken()
    {
    }

    S32                 getLengthHead() const   { return static_cast<S32>(mToken.size()); }
    S32                 getLengthTail() const   { return static_cast<S32>(mDelimiter.size()); }
    bool                isHead(const char* s) const;
    bool                isTail(const char* s) const;
    const std::string&    getToken() const        { return mToken; }
    const LLUIColor&     getColor() const        { return mColor; }
    ETokenType          getType()  const        { return mType; }
    const std::string&    getToolTip() const      { return mToolTip; }
    const std::string&    getDelimiter() const    { return mDelimiter; }

    // The style every segment of this token shares. A script has one segment
    // per occurrence and only a few dozen tokens, so building one style per
    // occurrence meant thousands of copies of a handful of values.
    //
    // The editor's font is the only input that varies. The colour is this
    // token's own, which is what a freshly built style copied anyway, so the
    // shared one follows a colour-table edit exactly as those did.
    const LLStyleConstSP&   getStyle(const LLFontGL* font) const;

#ifdef _DEBUG
    void        dump();
#endif

private:
    ETokenType  mType;
    std::string   mToken;
    LLUIColor    mColor;
    std::string   mToolTip;
    std::string   mDelimiter;

    mutable LLStyleConstSP  mStyle;
    mutable const LLFontGL* mStyleFont = nullptr;
};

class LLKeywords
{
public:
    LLKeywords();
    ~LLKeywords();

    void        clearLoaded() { mLoaded = false; }
    LLUIColor    getColorGroup(std::string_view key_in) const;
    bool        isLoaded() const { return mLoaded; }

    void        findSegments(std::vector<LLTextSegmentPtr> *seg_list,
                             const std::string& text,
                             class LLTextEditor& editor,
                             LLStyleConstSP style);
    struct SegmentOp
    {
        enum EOpType
        {
            OP_LINE_BREAK,
            OP_TOKEN
        };
        EOpType         type;
        S32             start;
        S32             end;
        LLKeywordToken* token;
    };
    typedef std::vector<SegmentOp> segment_ops_t;
    void        collectSegmentOps(segment_ops_t& ops, const std::string& text, bool disable_syntax_highlighting) const;
    void        applySegmentOps(std::vector<LLTextSegmentPtr> *seg_list,
                                const std::string& text,
                                const segment_ops_t& ops,
                                class LLTextEditor& editor,
                                LLStyleConstSP style);
    bool        applySegmentOpsRange(std::vector<LLTextSegmentPtr> *seg_list,
                                     const std::string& text,
                                     const segment_ops_t& ops,
                                     size_t& op_index,
                                     size_t max_ops,
                                     class LLTextEditor& editor,
                                     LLStyleConstSP style);
    void        initialize(LLSD SyntaxXML, bool luau_language = false);
    void        processTokens();

    // Add the token as described
    void addToken(LLKeywordToken::ETokenType type,
                    const std::string& key,
                    const LLUIColor& color,
                    const std::string& tool_tip = LLStringUtil::null,
                    const std::string& delimiter = LLStringUtil::null);

    // Searched with a view onto a span of the text being highlighted, so a
    // lookup copies nothing. std::less<> is what allows that: without a
    // transparent comparator, find() has to be handed a whole std::string built
    // for the purpose, once per word per redraw, which is what the hand-rolled
    // index class that used to sit here existed to avoid.
    typedef std::map<std::string, LLKeywordToken*, std::less<>> word_token_map_t;
    typedef word_token_map_t::const_iterator keyword_iterator_t;
    keyword_iterator_t begin() const { return mWordTokenMap.begin(); }
    keyword_iterator_t end() const { return mWordTokenMap.end(); }

#ifdef _DEBUG
    void        dump();
#endif

protected:
    void        processTokensGroup(const LLSD& Tokens, std::string_view Group);
    void        insertSegment(std::vector<LLTextSegmentPtr>& seg_list,
                              LLTextSegmentPtr new_segment,
                              S32 text_len,
                              const LLUIColor &defaultColor,
                              class LLTextEditor& editor);
    void        insertSegments(const std::string& wtext,
                               std::vector<LLTextSegmentPtr>& seg_list,
                               LLKeywordToken* token,
                               S32 text_len,
                               S32 seg_start,
                               S32 seg_end,
                               LLStyleConstSP style,
                               LLTextEditor& editor);

    void insertSegment(std::vector<LLTextSegmentPtr>& seg_list, LLTextSegmentPtr new_segment, S32 text_len, LLStyleConstSP style, LLTextEditor& editor );

    bool        mLoaded;
    LLSD        mSyntax;
    bool        mLuauLanguage;
    word_token_map_t mWordTokenMap;
    typedef std::deque<LLKeywordToken*> token_list_t;
    token_list_t mLineTokenList;
    token_list_t mDelimiterTokenList;
    typedef std::map<char, token_list_t> token_by_first_char_map_t;
    token_by_first_char_map_t mLineTokenByFirstChar;
    token_by_first_char_map_t mDelimiterTokenByFirstChar;

    typedef boost::unordered_map<std::string, std::string, ll::string_hash, std::equal_to<>> element_attributes_t;
    typedef element_attributes_t::const_iterator attribute_iterator_t;
    element_attributes_t mAttributes;
    std::string getAttribute(std::string_view key);

    std::string getArguments(LLSD& arguments);
};

#endif  // LL_LLKEYWORDS_H
