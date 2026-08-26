/**
 * @file lldate.cpp
 * @author Phoenix
 * @date 2006-02-05
 * @brief Implementation of the date class
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

#include "linden_common.h"
#include "lldate.h"

#include <time.h>
#include <locale.h>
#include <string>
#include <iomanip>
#include <sstream>

#include "lltimer.h"
#include "llstring.h"
#include "llfasttimer.h"

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

namespace
{
    bool gmtime_utc(time_t secs, struct tm& out)
    {
#if LL_WINDOWS
        return gmtime_s(&out, &secs) == 0;
#else
        return gmtime_r(&secs, &out) != nullptr;
#endif
    }

    bool localtime_utc(time_t secs, struct tm& out)
    {
#if LL_WINDOWS
        return localtime_s(&out, &secs) == 0;
#else
        return localtime_r(&secs, &out) != nullptr;
#endif
    }

    time_t timegm_utc(struct tm& in)
    {
#if LL_WINDOWS
        return _mkgmtime(&in);
#else
        return timegm(&in);
#endif
    }

    // The widest form is "YYYY-MM-DDTHH:MM:SS.ffZ", 23 characters.
    const size_t ISO8601_MAX = 23;

    // operator>>(S32&) and istream::get() each build a sentry, and the integer
    // extraction goes out through the locale's num_get. These read the same
    // things off the streambuf, keeping the leniency operator>> gave the
    // format: leading whitespace is skipped and a field may be any number of
    // digits, so "2006-4-24T1:2:3Z" still parses.
    typedef std::istream::traits_type date_traits;

    int date_peek(std::istream& s)
    {
        if (!s.good())
        {
            s.setstate(std::ios::failbit);
            return date_traits::eof();
        }
        const int c = s.rdbuf()->sgetc();
        if (c == date_traits::eof())
        {
            s.setstate(std::ios::eofbit);
        }
        return c;
    }

    int date_bump(std::istream& s)
    {
        if (!s.good())
        {
            s.setstate(std::ios::failbit);
            return date_traits::eof();
        }
        const int c = s.rdbuf()->sbumpc();
        if (c == date_traits::eof())
        {
            s.setstate(std::ios::eofbit | std::ios::failbit);
        }
        return c;
    }

    // Reads what operator>>(S32&) would: optional whitespace, optional sign,
    // then digits. Sets failbit when there is no digit, as the extractor does.
    bool date_read_int(std::istream& s, S32& out)
    {
        int c = date_peek(s);
        while (c != date_traits::eof() && isspace(c))
        {
            s.rdbuf()->sbumpc();
            c = date_peek(s);
        }
        bool negative = false;
        if (c == '+' || c == '-')
        {
            negative = (c == '-');
            s.rdbuf()->sbumpc();
            c = date_peek(s);
        }
        if (c == date_traits::eof() || c < '0' || c > '9')
        {
            s.setstate(std::ios::failbit);
            return false;
        }
        S32 value = 0;
        while (c >= '0' && c <= '9')
        {
            value = value * 10 + (c - '0');
            s.rdbuf()->sbumpc();
            c = date_peek(s);
        }
        out = negative ? -value : value;
        return true;
    }

    inline char* put_2(char* p, int value)
    {
        *p++ = (char)('0' + (value / 10));
        *p++ = (char)('0' + (value % 10));
        return p;
    }

    // Format "YYYY-MM-DDTHH:MM:SS[.ff]Z" into buf and return the length.
    // buf must hold at least 32 chars.
    size_t format_iso8601(F64 seconds_since_epoch, char* buf, size_t cap)
    {
        S64 total_usec = (S64)(seconds_since_epoch * (F64)USEC_PER_SEC);
        time_t secs = (time_t)(total_usec / 1000000);
        int usec = (int)(total_usec % 1000000);

        struct tm exp_time;
        if (!gmtime_utc(secs, exp_time))
        {
            return snprintf(buf, cap, "1970-01-01T00:00:00Z");
        }

        // Six fixed-width fields do not need a format string parsed at run
        // time; that snprintf was three quarters of the cost of asString().
        // A year outside four digits has no fixed-width form, so it keeps the
        // general path.
        const int year = exp_time.tm_year + 1900;
        if (year < 0 || year > 9999 || cap <= ISO8601_MAX)
        {
            size_t len = snprintf(buf, cap, "%04d-%02d-%02dT%02d:%02d:%02d",
                                  year, exp_time.tm_mon + 1,
                                  exp_time.tm_mday, exp_time.tm_hour,
                                  exp_time.tm_min, exp_time.tm_sec);
            if (usec > 0 && len + 4 <= cap)
            {
                len += snprintf(buf + len, cap - len, ".%02d", usec / 10000);
            }
            if (len < cap)
            {
                buf[len++] = 'Z';
            }
            return len;
        }

        char* p = buf;
        p = put_2(p, year / 100);
        p = put_2(p, year % 100);
        *p++ = '-';
        p = put_2(p, exp_time.tm_mon + 1);
        *p++ = '-';
        p = put_2(p, exp_time.tm_mday);
        *p++ = 'T';
        p = put_2(p, exp_time.tm_hour);
        *p++ = ':';
        p = put_2(p, exp_time.tm_min);
        *p++ = ':';
        p = put_2(p, exp_time.tm_sec);
        if (usec > 0)
        {
            *p++ = '.';
            p = put_2(p, usec / 10000);
        }
        *p++ = 'Z';
        return (size_t)(p - buf);
    }
}


LLDate::LLDate(F64SecondsImplicit seconds_since_epoch) :
    mSecondsSinceEpoch(seconds_since_epoch.value())
{}

LLDate::LLDate(const std::string& iso8601_date)
{
    if(!fromString(iso8601_date))
    {
        LL_WARNS() << "date " << iso8601_date << " failed to parse; "
            << "ZEROING IT OUT" << LL_ENDL;
        mSecondsSinceEpoch = DATE_EPOCH;
    }
}

std::string LLDate::asString() const
{
    char buffer[48];
    return std::string(buffer, format_iso8601(mSecondsSinceEpoch, buffer, sizeof(buffer)));
}

//@ brief Converts time in seconds since EPOCH
//        to RFC 1123 compliant date format
//        E.g. 1184797044.037586 == Wednesday, 18 Jul 2007 22:17:24 GMT
//        in RFC 1123. HTTP dates are always in GMT and RFC 1123
//        is one of the standards used and the prefered format
std::string LLDate::asRFC1123() const
{
    return toHTTPDateString (std::string ("%A, %d %b %Y %H:%M:%S GMT"));
}

std::string LLDate::toLocalDateString (std::string fmt) const
{
    LL_PROFILE_ZONE_SCOPED;

    time_t locSeconds = (time_t) mSecondsSinceEpoch;
    struct tm lt = {};
    if (!localtime_utc(locSeconds, lt)) return std::string();
    return toHTTPDateString(&lt, fmt);
}

std::string LLDate::toHTTPDateString (std::string fmt) const
{
    LL_PROFILE_ZONE_SCOPED;

    time_t locSeconds = (time_t) mSecondsSinceEpoch;
    struct tm gmt = {};
    if (!gmtime_utc(locSeconds, gmt)) return std::string();
    return toHTTPDateString(&gmt, fmt);
}

std::string LLDate::toHTTPDateString (tm * gmt, std::string fmt)
{
    LL_PROFILE_ZONE_SCOPED;

    // avoid calling setlocale() unnecessarily - it's expensive.
    static std::string prev_locale = "";
    std::string this_locale = LLStringUtil::getLocale();
    if (this_locale != prev_locale)
    {
        setlocale(LC_TIME, this_locale.c_str());
        prev_locale = this_locale;
    }

    // use strftime() as it appears to be faster than std::time_put
    char buffer[128];
    strftime(buffer, 128, fmt.c_str(), gmt);
    std::string res(buffer);
#if LL_WINDOWS
    // Convert from locale-dependant charset to UTF-8 (EXT-8524).
    res = ll_convert_string_to_utf8_string(res);
#endif
    return res;
}

void LLDate::toStream(std::ostream& s) const
{
    char buffer[48];
    s.write(buffer, format_iso8601(mSecondsSinceEpoch, buffer, sizeof(buffer)));
}

bool LLDate::split(S32 *year, S32 *month, S32 *day, S32 *hour, S32 *min, S32 *sec) const
{
    time_t secs = (time_t)mSecondsSinceEpoch;

    struct tm exp_time;
    if (!gmtime_utc(secs, exp_time))
    {
        return false;
    }

    if (year)
        *year = exp_time.tm_year + 1900;

    if (month)
        *month = exp_time.tm_mon + 1;

    if (day)
        *day = exp_time.tm_mday;

    if (hour)
        *hour = exp_time.tm_hour;

    if (min)
        *min = exp_time.tm_min;

    if (sec)
        *sec = exp_time.tm_sec;

    return true;
}

bool LLDate::fromString(const std::string& iso8601_date)
{
    boost::iostreams::stream<boost::iostreams::array_source> stream(iso8601_date.data(), iso8601_date.size());
    return fromStream(stream);
}

bool LLDate::fromStream(std::istream& s)
{
    struct tm exp_time = {};
    S32 tm_part;
    int c;

    if (!date_read_int(s, tm_part)) { return false; }
    exp_time.tm_year = tm_part - 1900;
    c = date_bump(s); // skip the hypen
    if (c != '-') { return false; }
    if (!date_read_int(s, tm_part)) { return false; }
    exp_time.tm_mon = tm_part - 1;
    c = date_bump(s); // skip the hypen
    if (c != '-') { return false; }
    if (!date_read_int(s, tm_part)) { return false; }
    exp_time.tm_mday = tm_part;

    c = date_bump(s); // skip the T
    if (c != 'T') { return false; }

    if (!date_read_int(s, tm_part)) { return false; }
    exp_time.tm_hour = tm_part;
    c = date_bump(s); // skip the :
    if (c != ':') { return false; }
    if (!date_read_int(s, tm_part)) { return false; }
    exp_time.tm_min = tm_part;
    c = date_bump(s); // skip the :
    if (c != ':') { return false; }
    if (!date_read_int(s, tm_part)) { return false; }
    exp_time.tm_sec = tm_part;

    // generate a time_t from that
    time_t time = timegm_utc(exp_time);
    if (time == (time_t)-1)
    {
        return false;
    }

    F64 seconds_since_epoch = (F64)time;

    // check for fractional
    c = s.peek();
    if(c == '.')
    {
        F64 fractional = 0.0;
        s >> fractional;
        seconds_since_epoch += fractional;
    }

    c = s.peek(); // check for offset
    if (c == '+' || c == '-')
    {
        S32 offset_sign = (c == '+') ? 1 : -1;
        S32 offset_hours = 0;
        S32 offset_minutes = 0;
        S32 offset_in_seconds = 0;

        s >> offset_hours;

        c = s.get(); // skip the colon a get the minutes if there are any
        if (c == ':')
        {
            s >> offset_minutes;
        }

        offset_in_seconds =  (offset_hours * 60 + offset_sign * offset_minutes) * 60;
        seconds_since_epoch -= offset_in_seconds;
    }
    else if (c != 'Z') { return false; } // skip the Z

    mSecondsSinceEpoch = seconds_since_epoch;
    return true;
}

bool LLDate::fromYMDHMS(S32 year, S32 month, S32 day, S32 hour, S32 min, S32 sec)
{
    struct tm exp_time = {};

    exp_time.tm_year = year - 1900;
    exp_time.tm_mon = month - 1;
    exp_time.tm_mday = day;
    exp_time.tm_hour = hour;
    exp_time.tm_min = min;
    exp_time.tm_sec = sec;

    // generate a time_t from that
    time_t time = timegm_utc(exp_time);
    if (time == (time_t)-1)
    {
        return false;
    }

    mSecondsSinceEpoch = (F64)time;

    return true;
}

/* static */ LLDate LLDate::now()
{
    // time() returns seconds, we want fractions of a second, which LLTimer provides --RN
    return LLDate(LLTimer::getTotalSeconds());
}

std::ostream& operator<<(std::ostream& s, const LLDate& date)
{
    date.toStream(s);
    return s;
}

std::istream& operator>>(std::istream& s, LLDate& date)
{
    date.fromStream(s);
    return s;
}

