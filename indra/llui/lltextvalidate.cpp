/**
 * @file lltextvalidate.cpp
 * @brief Text validation helper functions
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

// Text editor widget to let users enter a single line.

#include "linden_common.h"

#include "lltextvalidate.h"

#include "llnotificationsutil.h"
#include "lltrans.h"

#include "llresmgr.h" // for LLLocale

namespace LLTextValidate
{

namespace
{

S32 strtol(std::string_view str)
{
    return ::strtol(std::string(str).c_str(), NULL, 10);
}

// A decoded character and where it sits in the string it came from. Every
// predicate the rules below use classifies Unicode, so each has to be handed a
// whole codepoint: one byte of a multi-byte character reaches LLStringOps as a
// lead byte, which is a negative number here, and that is why the same field
// used to accept an accented letter while it was being typed and reject it
// once the text arrived as UTF-8.
struct CharAt
{
    llwchar     cp    = 0;
    std::size_t begin = 0;
    S32         index = 0;  // 1-based, counted in characters
};

// The character an error message names, or none. `bad` is asked about every
// character from `from` onwards, and the LAST one it rejects is reported --
// these rules were written as `while (len--)` loops counting down from the end
// of the string, so that is the character they picked out.
template <typename FN>
bool find_last_bad(std::string_view str, std::size_t from, FN&& bad, CharAt& out)
{
    bool found = false;
    S32  index = 1;
    for (std::size_t i = from; i < str.size(); ++index)
    {
        const LLCodepointAt at = utf8str_decode_at(str, i);
        if (bad(at.cp))
        {
            out   = { at.cp, i, index };
            found = true;
        }
        i = at.next;
    }
    return found;
}

// As above, but stopping at the first character rejected, for the rules that
// were written as forward loops.
template <typename FN>
bool find_first_bad(std::string_view str, std::size_t from, FN&& bad, CharAt& out)
{
    S32 index = 1;
    for (std::size_t i = from; i < str.size(); ++index)
    {
        const LLCodepointAt at = utf8str_decode_at(str, i);
        if (bad(at.cp))
        {
            out = { at.cp, i, index };
            return true;
        }
        i = at.next;
    }
    return false;
}

// The offending character, whole, for the notification. Naming it by its first
// byte would put a stray fragment of a character in the message.
LLSD llsd(llwchar cp) { return LLSD(utf8str_from_cp(cp)); }

}

void ValidatorImpl::setLastErrorShowTime()
{
    mLastErrorShowTime = (U32Seconds)LLTimer::getTotalTime();
}

void Validator::showLastErrorUsingTimeout(U32 timeout)
{
    if (mImpl && (U32Seconds)LLTimer::getTotalTime() >= mImpl->getLastErrorShowTime() + timeout)
    {
        mImpl->setLastErrorShowTime();
        std::string reason = LLTrans::getString(mImpl->getLastErrorName(), mImpl->getLastErrorValues());
        LLNotificationsUtil::add("InvalidKeystroke", LLSD().with("REASON", reason));
    }
}

// Limits what characters can be used to [1234567890.-] with [-] only valid in the first position.
// Does NOT ensure that the string is a well-formed number--that's the job of post-validation--for
// the simple reasons that intermediate states may be invalid even if the final result is valid.
class ValidatorFloat : public ValidatorImpl
{
public:
    bool validate(std::string_view str) override
    {
        LLLocale locale(LLLocale::USER_LOCALE);

        std::string trimmed(str);
        LLStringUtil::trim(trimmed);
        if (!trimmed.empty())
        {
            // May be a comma or period, depending on the locale
            const llwchar decimal_point = (llwchar)(unsigned char)LLResMgr::getInstance()->getDecimalPoint();

            // First character can be a negative sign
            const std::size_t from = trimmed.front() == '-' ? 1 : 0;

            CharAt at;
            if (find_first_bad(trimmed, from,
                               [decimal_point](llwchar c)
                               { return decimal_point != c && !LLStringOps::isDigit(c); },
                               at))
            {
                return setError("Validator_ShouldBeDigitOrDot",
                                LLSD().with("NR", at.index + (S32)from).with("CH", llsd(at.cp)));
            }
        }

        return resetError();
    }
} validatorFloatImpl;
Validator validateFloat(validatorFloatImpl);

// Limits what characters can be used to [1234567890-] with [-] only valid in the first position.
// Does NOT ensure that the string is a well-formed number--that's the job of post-validation--for
// the simple reasons that intermediate states may be invalid even if the final result is valid.
class ValidatorInt : public ValidatorImpl
{
public:
    bool validate(std::string_view str) override
    {
        LLLocale locale(LLLocale::USER_LOCALE);

        std::string trimmed(str);
        LLStringUtil::trim(trimmed);
        if (!trimmed.empty())
        {
            // First character can be a negative sign
            const std::size_t from = trimmed.front() == '-' ? 1 : 0;

            CharAt at;
            if (find_first_bad(trimmed, from,
                               [](llwchar c) { return !LLStringOps::isDigit(c); }, at))
            {
                return setError("Validator_ShouldBeDigit",
                                LLSD().with("NR", at.index + (S32)from).with("CH", llsd(at.cp)));
            }
        }

        return resetError();
    }
} validatorIntImpl;
Validator validateInt(validatorIntImpl);

class ValidatorPositiveS32 : public ValidatorImpl
{
public:
    bool validate(std::string_view str) override
    {
        LLLocale locale(LLLocale::USER_LOCALE);

        std::string trimmed(str);
        LLStringUtil::trim(trimmed);
        if (!trimmed.empty())
        {
            const char ch = trimmed.front();

            if (('-' == ch) || ('0' == ch))
            {
                return setError("Validator_ShouldNotBeMinusOrZero", LLSD().with("CH", llsd((llwchar)ch)));
            }

            CharAt at;
            if (find_first_bad(trimmed, 0,
                               [](llwchar c) { return !LLStringOps::isDigit(c); }, at))
            {
                return setError("Validator_ShouldBeDigit",
                                LLSD().with("NR", at.index).with("CH", llsd(at.cp)));
            }
        }

        if (strtol(trimmed) <= 0)
        {
            return setError("Validator_InvalidNumericString", LLSD().with("STR", LLSD(trimmed)));
        }

        return resetError();
    }
} validatorPositiveS32Impl;
Validator validatePositiveS32(validatorPositiveS32Impl);

class ValidatorNonNegativeS32 : public ValidatorImpl
{
public:
    bool validate(std::string_view str) override
    {
        LLLocale locale(LLLocale::USER_LOCALE);

        std::string trimmed(str);
        LLStringUtil::trim(trimmed);
        if (!trimmed.empty())
        {
            const char ch = trimmed.front();

            if ('-' == ch)
            {
                return setError("Validator_ShouldNotBeMinus", LLSD().with("CH", llsd((llwchar)ch)));
            }

            CharAt at;
            if (find_first_bad(trimmed, 0,
                               [](llwchar c) { return !LLStringOps::isDigit(c); }, at))
            {
                return setError("Validator_ShouldBeDigit",
                                LLSD().with("NR", at.index).with("CH", llsd(at.cp)));
            }
        }

        if (strtol(trimmed) < 0)
        {
            return setError("Validator_InvalidNumericString", LLSD().with("STR", LLSD(trimmed)));
        }

        return resetError();
    }
} validatorNonNegativeS32Impl;
Validator validateNonNegativeS32(validatorNonNegativeS32Impl);

class ValidatorNonNegativeS32NoSpace : public ValidatorImpl
{
public:
    bool validate(std::string_view str) override
    {
        LLLocale locale(LLLocale::USER_LOCALE);

        if (!str.empty())
        {
            const char ch = str.front();

            if ('-' == ch)
            {
                return setError("Validator_ShouldNotBeMinus", LLSD().with("CH", llsd((llwchar)ch)));
            }

            CharAt at;
            if (find_first_bad(str, 0,
                               [](llwchar c)
                               { return !LLStringOps::isDigit(c) || LLStringOps::isSpace(c); },
                               at))
            {
                return setError("Validator_ShouldBeDigitNotSpace",
                                LLSD().with("NR", at.index).with("CH", llsd(at.cp)));
            }
        }

        if (strtol(str) < 0)
        {
            return setError("Validator_InvalidNumericString", LLSD().with("STR", LLSD(std::string(str))));
        }

        return resetError();
    }
} validatorNonNegativeS32NoSpaceImpl;
Validator validateNonNegativeS32NoSpace(validatorNonNegativeS32NoSpaceImpl);

class ValidatorAlphaNum : public ValidatorImpl
{
public:
    bool validate(std::string_view str) override
    {
        LLLocale locale(LLLocale::USER_LOCALE);

        CharAt at;
        if (find_last_bad(str, 0, [](llwchar c) { return !LLStringOps::isAlnum(c); }, at))
        {
            return setError("Validator_ShouldBeDigitOrAlpha",
                            LLSD().with("NR", at.index).with("CH", llsd(at.cp)));
        }

        return resetError();
    }
} validatorAlphaNumImpl;
Validator validateAlphaNum(validatorAlphaNumImpl);

class ValidatorAlphaNumSpace : public ValidatorImpl
{
public:
    bool validate(std::string_view str) override
    {
        LLLocale locale(LLLocale::USER_LOCALE);

        CharAt at;
        if (find_last_bad(str, 0,
                          [](llwchar c) { return !LLStringOps::isAlnum(c) && (U' ' != c); }, at))
        {
            return setError("Validator_ShouldBeDigitOrAlphaOrSpace",
                            LLSD().with("NR", at.index).with("CH", llsd(at.cp)));
        }

        return resetError();
    }
} validatorAlphaNumSpaceImpl;
Validator validateAlphaNumSpace(validatorAlphaNumSpaceImpl);

// Used for most names of things stored on the server, due to old file-formats
// that used the pipe (|) for multiline text storage.  Examples include
// inventory item names, parcel names, object names, etc.
class ValidatorASCIIPrintableNoPipe : public ValidatorImpl
{
public:
    bool validate(std::string_view str) override
    {
        CharAt at;
        if (find_last_bad(str, 0,
                          [](llwchar c)
                          {
                              return c < 0x20 || c > 0x7f || c == U'|' ||
                                     (c != U' ' && !LLStringOps::isAlnum(c) && !LLStringOps::isPunct(c));
                          },
                          at))
        {
            return setError("Validator_ShouldBeDigitOrAlphaOrPunct",
                            LLSD().with("NR", at.index).with("CH", llsd(at.cp)));
        }

        return resetError();
    }
} validatorASCIIPrintableNoPipeImpl;
Validator validateASCIIPrintableNoPipe(validatorASCIIPrintableNoPipeImpl);

// Used for avatar names
class ValidatorASCIIPrintableNoSpace : public ValidatorImpl
{
public:
    bool validate(std::string_view str) override
    {
        CharAt at;
        if (find_last_bad(str, 0,
                          [](llwchar c)
                          {
                              return c <= 0x20 || c > 0x7f || LLStringOps::isSpace(c) ||
                                     (!LLStringOps::isAlnum(c) && !LLStringOps::isPunct(c));
                          },
                          at))
        {
            return setError("Validator_ShouldBeDigitOrAlphaOrPunctNotSpace",
                            LLSD().with("NR", at.index).with("CH", llsd(at.cp)));
        }

        return resetError();
    }
} validatorASCIIPrintableNoSpaceImpl;
Validator validateASCIIPrintableNoSpace(validatorASCIIPrintableNoSpaceImpl);

class ValidatorASCII : public ValidatorImpl
{
public:
    bool validate(std::string_view str) override
    {
        CharAt at;
        if (find_last_bad(str, 0, [](llwchar c) { return c < 0x20 || c > 0x7f; }, at))
        {
            return setError("Validator_ShouldBeASCII",
                            LLSD().with("NR", at.index).with("CH", llsd(at.cp)));
        }

        return resetError();
    }
} validatorASCIIImpl;
Validator validateASCII(validatorASCIIImpl);

class ValidatorASCIINoLeadingSpace : public ValidatorASCII
{
public:
    bool validate(std::string_view str) override
    {
        // Guarding the empty case is not optional here: a view has no
        // terminator to read past, whereas the std::string this used to take
        // handed back its null and quietly answered "not a space".
        if (!str.empty() && LLStringOps::isSpace(str.front()))
        {
            return false;
        }

        return ValidatorASCII::validate(str);
    }
} validatorASCIINoLeadingSpaceImpl;
Validator validateASCIINoLeadingSpace(validatorASCIINoLeadingSpaceImpl);

class ValidatorASCIIWithNewLineNoPipe : public ValidatorImpl
{
    // Used for multiline text stored on the server.
    // Example is landmark description in Places SP.
public:
    bool validate(std::string_view str) override
    {
        CharAt at;
        if (find_last_bad(str, 0,
                          [](llwchar c)
                          { return (c < 0x20 && c != 0xA) || c > 0x7f || c == U'|'; },
                          at))
        {
            return setError("Validator_ShouldBeNewLineOrASCIINoPipe",
                            LLSD().with("NR", at.index).with("CH", llsd(at.cp)));
        }

        return resetError();
    }
} validatorASCIIWithNewLineNoPipeImpl;
Validator validateASCIIWithNewLineNoPipe(validatorASCIIWithNewLineNoPipeImpl);

void Validators::declareValues()
{
    declare("ascii", validateASCII);
    declare("float", validateFloat);
    declare("int", validateInt);
    declare("positive_s32", validatePositiveS32);
    declare("non_negative_s32", validateNonNegativeS32);
    declare("alpha_num", validateAlphaNum);
    declare("alpha_num_space", validateAlphaNumSpace);
    declare("ascii_printable_no_pipe", validateASCIIPrintableNoPipe);
    declare("ascii_printable_no_space", validateASCIIPrintableNoSpace);
    declare("ascii_with_newline_no_pipe", validateASCIIWithNewLineNoPipe);
}

} // namespace LLTextValidate
