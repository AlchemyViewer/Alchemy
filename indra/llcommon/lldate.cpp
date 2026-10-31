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
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <iomanip>
#include <sstream>

#include "lltimer.h"
#include "llstring.h"

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <fast_float/fast_float.h>

namespace
{
    // A date's whole seconds as a time_t, counted down to the second at or
    // before it, or false when they are not a number or lie past what a
    // time_t holds. Converting such a double is undefined, and a binary LLSD
    // date carries whatever eight bytes it was sent.
    bool seconds_to_time_t(F64 seconds, time_t& out)
    {
        // 2^63, exactly representable; any double below it fits an S64
        constexpr F64 LIMIT = 9223372036854775808.0;
        if (!std::isfinite(seconds) || seconds < -LIMIT || seconds >= LIMIT)
        {
            return false;
        }
        out = (time_t)std::floor(seconds);
        return true;
    }

    S64 floor_div(S64 a, S64 b)
    {
        return a / b - (a % b < 0 ? 1 : 0);
    }

    S64 floor_mod(S64 a, S64 b)
    {
        const S64 r = a % b;
        return r < 0 ? r + b : r;
    }

    // Days from 1970-01-01 to a proleptic Gregorian date, and back, after
    // Howard Hinnant's civil-date algorithms. The month runs 1 to 12; the day
    // may lie outside its month, and counts on from the month's first.
    S64 days_from_civil(S64 year, S64 month, S64 day)
    {
        year -= month <= 2 ? 1 : 0;
        const S64 era = floor_div(year, 400);
        const S64 year_of_era = year - era * 400;
        const S64 day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        const S64 day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
        return era * 146097 + day_of_era - 719468;
    }

    void civil_from_days(S64 days, S64& year, S32& month, S32& day)
    {
        days += 719468;
        const S64 era = floor_div(days, 146097);
        const S64 day_of_era = days - era * 146097;
        const S64 year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
        const S64 day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
        const S64 month_from_march = (5 * day_of_year + 2) / 153;
        day = (S32)(day_of_year - (153 * month_from_march + 2) / 5 + 1);
        month = (S32)(month_from_march < 10 ? month_from_march + 3 : month_from_march - 9);
        year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
    }

    // The seconds since 1970 of UTC calendar fields. A field out of its range
    // rolls over into the field above it, so 24:00:00 is the next day's
    // midnight and 2003-02-29 is 2003-03-01.
    S64 seconds_from_utc_fields(S64 year, S64 month, S64 day, S64 hour, S64 min, S64 sec)
    {
        year += floor_div(month - 1, 12);
        month = floor_mod(month - 1, 12) + 1;
        return (days_from_civil(year, month, 1) + day - 1) * 86400 + hour * 3600 + min * 60 + sec;
    }

    // The UTC calendar fields of a count of seconds, the same on every
    // platform: Windows' gmtime_s refuses times before 1970 and past 3000.
    // False when the year does not fit an int, or tm_year, the year less
    // 1900, does not: callers add the 1900 back in an int.
    bool utc_fields(time_t secs, struct tm& out)
    {
        const S64 days = floor_div(secs, 86400);
        const S64 second_of_day = floor_mod(secs, 86400);
        S64 year = 0;
        S32 month = 0;
        S32 day = 0;
        civil_from_days(days, year, month, day);
        if (year < (S64)std::numeric_limits<int>::min() + 1900 || year > std::numeric_limits<int>::max())
        {
            return false;
        }
        out = {};
        out.tm_year = (int)(year - 1900);
        out.tm_mon = month - 1;
        out.tm_mday = day;
        out.tm_hour = (int)(second_of_day / 3600);
        out.tm_min = (int)(second_of_day / 60 % 60);
        out.tm_sec = (int)(second_of_day % 60);
        // 1970-01-01 was a Thursday
        out.tm_wday = (int)floor_mod(days + 4, 7);
        out.tm_yday = (int)(days - days_from_civil(year, 1, 1));
        return true;
    }

    bool localtime_utc(time_t secs, struct tm& out)
    {
#if LL_WINDOWS
        return localtime_s(&out, &secs) == 0;
#else
        return localtime_r(&secs, &out) != nullptr;
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

    // Reads digits, with no sign or whitespace before them, and how many there
    // were. Sets failbit when there is no digit.
    bool date_read_digits(std::istream& s, S32& out, S32& count)
    {
        int c = date_peek(s);
        if (c == date_traits::eof() || c < '0' || c > '9')
        {
            s.setstate(std::ios::failbit);
            return false;
        }
        // A field past nine digits' worth is no date, and is turned down before
        // the digits can overflow. The arithmetic after would carry it into
        // the fields above rather than refuse it.
        constexpr S32 FIELD_MAX = 999999999;
        S32 value = 0;
        count = 0;
        while (c >= '0' && c <= '9')
        {
            if (value > (FIELD_MAX - (c - '0')) / 10)
            {
                s.setstate(std::ios::failbit);
                return false;
            }
            value = value * 10 + (c - '0');
            ++count;
            s.rdbuf()->sbumpc();
            c = date_peek(s);
        }
        out = value;
        return true;
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
        }
        S32 value = 0;
        S32 count = 0;
        if (!date_read_digits(s, value, count))
        {
            return false;
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
        // The microseconds must fit an S64 for the cast below to be defined:
        // about 292,000 years either side of the epoch, and never NaN.
        constexpr F64 MAX_SECONDS = 9.2e12;
        if (!std::isfinite(seconds_since_epoch) || std::fabs(seconds_since_epoch) >= MAX_SECONDS)
        {
            return snprintf(buf, cap, "1970-01-01T00:00:00Z");
        }
        // Counted down, so a date before 1970 is the second before it and the
        // hundredths on from that second, as after 1970.
        const S64 total_usec = (S64)std::floor(seconds_since_epoch * (F64)USEC_PER_SEC);
        const time_t secs = (time_t)floor_div(total_usec, 1000000);
        const int hundredths = (int)(floor_mod(total_usec, 1000000) / 10000);

        struct tm exp_time;
        if (!utc_fields(secs, exp_time))
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
            // A year before 0 is its sign and four digits, which %04d would
            // count the sign among.
            size_t len = snprintf(buf, cap, "%s%04d-%02d-%02dT%02d:%02d:%02d",
                                  year < 0 ? "-" : "", year < 0 ? -year : year, exp_time.tm_mon + 1,
                                  exp_time.tm_mday, exp_time.tm_hour,
                                  exp_time.tm_min, exp_time.tm_sec);
            if (hundredths > 0 && len + 4 <= cap)
            {
                len += snprintf(buf + len, cap - len, ".%02d", hundredths);
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
        if (hundredths > 0)
        {
            *p++ = '.';
            p = put_2(p, hundredths);
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

    time_t locSeconds;
    struct tm lt = {};
    if (!seconds_to_time_t(mSecondsSinceEpoch, locSeconds) || !localtime_utc(locSeconds, lt)) return std::string();
    return toHTTPDateString(&lt, fmt);
}

std::string LLDate::toHTTPDateString (std::string fmt) const
{
    LL_PROFILE_ZONE_SCOPED;

    time_t locSeconds;
    struct tm gmt = {};
    if (!seconds_to_time_t(mSecondsSinceEpoch, locSeconds) || !utc_fields(locSeconds, gmt)) return std::string();
    // Windows' strftime takes the years 0 to 9999 only
    if (gmt.tm_year < -1900 || gmt.tm_year > 8099) return std::string();
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

    // use strftime() as it appears to be faster than std::time_put. It returns
    // 0 and leaves the buffer indeterminate when the result does not fit.
    char buffer[128];
    const size_t len = strftime(buffer, sizeof(buffer), fmt.c_str(), gmt);
    std::string res(buffer, len);
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
    time_t secs;
    struct tm exp_time;
    if (!seconds_to_time_t(mSecondsSinceEpoch, secs) || !utc_fields(secs, exp_time))
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
    S32 year, month, day, hour, min, sec;
    int c;

    if (!date_read_int(s, year)) { return false; }
    c = date_bump(s); // skip the hypen
    if (c != '-') { return false; }
    if (!date_read_int(s, month)) { return false; }
    c = date_bump(s); // skip the hypen
    if (c != '-') { return false; }
    if (!date_read_int(s, day)) { return false; }

    c = date_bump(s); // skip the T
    if (c != 'T') { return false; }

    if (!date_read_int(s, hour)) { return false; }
    c = date_bump(s); // skip the :
    if (c != ':') { return false; }
    if (!date_read_int(s, min)) { return false; }
    c = date_bump(s); // skip the :
    if (c != ':') { return false; }
    if (!date_read_int(s, sec)) { return false; }

    F64 seconds_since_epoch = (F64)seconds_from_utc_fields(year, month, day, hour, min, sec);

    // check for fractional: digits only, read as the double "0.<digits>"
    c = date_peek(s);
    if (c == '.')
    {
        s.rdbuf()->sbumpc();
        std::string fraction("0.");
        c = date_peek(s);
        while (c >= '0' && c <= '9')
        {
            fraction.push_back((char)c);
            s.rdbuf()->sbumpc();
            c = date_peek(s);
        }
        if (fraction.size() == 2) { return false; }
        F64 fractional = 0.0;
        fast_float::from_chars(fraction.data(), fraction.data() + fraction.size(), fractional);
        seconds_since_epoch += fractional;
    }

    c = date_peek(s); // check for offset
    if (c == '+' || c == '-')
    {
        const F64 offset_sign = (c == '+') ? 1.0 : -1.0;
        s.rdbuf()->sbumpc();
        S32 offset_hours = 0;
        S32 offset_minutes = 0;
        S32 digits = 0;
        if (!date_read_digits(s, offset_hours, digits)) { return false; }
        // Peeking at the end would fail a stream the date has been read from.
        if (!s.eof() && date_peek(s) == ':')
        {
            s.rdbuf()->sbumpc();
            if (!date_read_int(s, offset_minutes)) { return false; }
        }
        else if (digits == 4)
        {
            // ISO 8601's basic form, hhmm
            offset_minutes = offset_hours % 100;
            offset_hours /= 100;
        }

        // in F64: the hours may run to nine digits, and scaling them to
        // seconds in S32 would overflow
        seconds_since_epoch -= offset_sign * ((F64)offset_hours * 60.0 + (F64)offset_minutes) * 60.0;
    }
    else if (c != 'Z') { return false; } // skip the Z

    mSecondsSinceEpoch = seconds_since_epoch;
    return true;
}

bool LLDate::fromYMDHMS(S32 year, S32 month, S32 day, S32 hour, S32 min, S32 sec)
{
    mSecondsSinceEpoch = (F64)seconds_from_utc_fields(year, month, day, hour, min, sec);

    return true;
}

/* static */ LLDate LLDate::now()
{
    // The calendar clock, to a fraction of a second. LLTimer's total time is
    // anchored to the epoch once and counts on its own after that, so it
    // drifts from the calendar over a session.
    const std::chrono::system_clock::duration since_epoch = std::chrono::system_clock::now().time_since_epoch();
    return LLDate(std::chrono::duration<F64>(since_epoch).count());
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

