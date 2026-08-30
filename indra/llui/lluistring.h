/**
 * @file lluistring.h
 * @author: Steve Bennetts
 * @brief A fancy wrapper for std::string supporting argument substitutions.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

#ifndef LL_LLUISTRING_H
#define LL_LLUISTRING_H

#include "llstring.h"
#include <string>

// Use this class to store translated text that may have arguments
// e.g. "Welcome [USERNAME] to [SECONDLIFE]!"

// Adding or changing an argument will update the result string, preserving the origianl
// Thus, subsequent changes to arguments or even the original string will produce
//  the correct result

// Example Usage:
// LLUIString mMessage("Welcome [USERNAME] to [SECONDLIFE]!");
// mMessage.setArg("[USERNAME]", "Steve");
// mMessage.setArg("[SECONDLIFE]", "Second Life");
// LL_INFOS() << mMessage.getString() << LL_ENDL; // outputs "Welcome Steve to Second Life"
// mMessage.setArg("[USERNAME]", "Joe");
// LL_INFOS() << mMessage.getString() << LL_ENDL; // outputs "Welcome Joe to Second Life"
// mMessage = "Bienvenido a la [SECONDLIFE] [USERNAME]"
// mMessage.setArg("[SECONDLIFE]", "Segunda Vida");
// LL_INFOS() << mMessage.getString() << LL_ENDL; // outputs "Bienvenido a la Segunda Vida Joe"

// Implementation Notes:
// Attempting to have operator[](const std::string& s) return mArgs[s] fails because we have
// to call format() after the assignment happens.

class LLUIString
{
public:
    // These methods all perform appropriate argument substitution
    // and modify mOrig where appropriate
    LLUIString() : mArgs(NULL), mNeedsResult(false) {}
    LLUIString(const std::string& instring, const LLStringUtil::format_map_t& args);
    LLUIString(const std::string& instring) : mArgs(NULL) { assign(instring); }
    ~LLUIString() { delete mArgs; }

    void assign(const std::string& instring);
    LLUIString& operator=(const std::string& s) { assign(s); return *this; }

    void setArgList(const LLStringUtil::format_map_t& args);
    void setArgs(const LLStringUtil::format_map_t& args) { setArgList(args); }
    void setArgs(const class LLSD& sd);
    void setArg(const std::string& key, const std::string& replacement);

    const std::string& getString() const { return getUpdatedResult(); }
    operator std::string() const { return getUpdatedResult(); }

    // Bumped whenever the result could have moved -- a new value, a new
    // argument, or one of the edit helpers below. Anything caching work
    // derived from this text (shaped glyphs, a measured width) keys on this
    // rather than comparing the string, and so cannot forget to. It may bump
    // without the text actually differing; that costs a rebuild, where the
    // reverse would leave the wrong text on screen.
    U32 getGeneration() const { return mGeneration; }

    bool empty() const { return getUpdatedResult().empty(); }

    // Named for its unit, because an S32 length reads the same whether it
    // counts bytes or characters and the two used to disagree here: this
    // returned a codepoint count while everything it fed measured and drew in
    // bytes.
    S32 lengthBytes() const { return static_cast<S32>(getUpdatedResult().size()); }

    void clear();
    void clearArgs() { if (mArgs) mArgs->clear(); }

    // These utility functions are included for text editing.
    // They do not affect mOrig and do not perform argument substitution.
    // Every offset and length below counts BYTES of the UTF-8 result, and each
    // is expected to sit at a character start; replace() and truncate() are the
    // two that move on their own, to whole characters.
    void truncate(S32 max_bytes);
    void erase(S32 byte_idx, S32 byte_len);
    void insert(S32 byte_idx, std::string_view chars);
    void replace(S32 byte_idx, llwchar wc);

private:
    // something changed, requiring reformatting of strings
    void dirty();

    std::string& getUpdatedResult() const { if (mNeedsResult) { updateResult(); } return mResult; }

    // do actual work of updating strings (non-inlined)
    void updateResult() const;
    LLStringUtil::format_map_t& getArgs();

    std::string mOrig;
    mutable std::string mResult;
    LLStringUtil::format_map_t* mArgs;

    // controls lazy evaluation
    mutable bool    mNeedsResult { true };

    // Not mutable: updateResult() recomputes what the last change already
    // announced, so producing the result must not count as a change.
    U32             mGeneration { 0 };
};

#endif // LL_LLUISTRING_H
