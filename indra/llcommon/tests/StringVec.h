/**
 * @file   StringVec.h
 * @author Nat Goodspeed
 * @date   2012-02-24
 * @brief  Print a std::vector<std::string>, as a failed check shows one
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Copyright (c) 2012, Linden Research, Inc.
 * $/LicenseInfo$
 */

#if ! defined(LL_STRINGVEC_H)
#define LL_STRINGVEC_H

#include <vector>
#include <string>
#include <iostream>

typedef std::vector<std::string> StringVec;

inline std::ostream& operator<<(std::ostream& out, const StringVec& strings)
{
    out << '(';
    StringVec::const_iterator begin(strings.begin()), end(strings.end());
    if (begin != end)
    {
        out << '"' << *begin << '"';
        while (++begin != end)
        {
            out << ", \"" << *begin << '"';
        }
    }
    out << ')';
    return out;
}

#endif /* ! defined(LL_STRINGVEC_H) */
