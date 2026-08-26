/**
 * @file lluuid.cpp
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
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

#if LL_WINDOWS
#include "llwin32headers.h"
#include <iphlpapi.h>
#endif

#include "llapp.h"
#include "lldefs.h"
#include "llerror.h"

#include "lluuid.h"

#include "llerror.h"
#include "llrand.h"
#include "llstring.h"
#include "lltimer.h"
#include "llthread.h"
#include "llmutex.h"
#include "llmd5.h"

#include <array>

// static
LLMutex* LLUUID::mMutex = NULL;

// Defined here rather than as an inline constexpr in lluuid.h: LLUUID /
// LLTransactionID are exported (LL_COMMON_API), so these static members are
// dllimport in consumers and may not have an in-header definition. Both are
// constant-initialized (constexpr default ctors), so no static-init-order
// hazard -- they hold the all-zero UUID before any dynamic initialization runs.
// static
const LLUUID LLUUID::null;
// static
const LLTransactionID LLTransactionID::tnull;



/*

NOT DONE YET!!!

static char BASE85_TABLE[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
    'y', 'z', '!', '#', '$', '%', '&', '(', ')', '*',
    '+', '-', ';', '[', '=', '>', '?', '@', '^', '_',
    '`', '{', '|', '}', '~', '\0'
};


void encode( char * fiveChars, unsigned int word ) throw( )
{
for( int ix = 0; ix < 5; ++ix ) {
fiveChars[4-ix] = encodeTable[ word % 85];
word /= 85;
}
}

To decode:
unsigned int decode( char const * fiveChars ) throw( bad_input_data )
{
unsigned int ret = 0;
for( int ix = 0; ix < 5; ++ix ) {
char * s = strchr( encodeTable, fiveChars[ ix ] );
if( s == 0 ) LLTHROW(bad_input_data());
ret = ret * 85 + (s-encodeTable);
}
return ret;
}

void LLUUID::toBase85(char* out)
{
    U32* me = (U32*)&(mData[0]);
    for(S32 i = 0; i < 4; ++i)
    {
        char* o = &out[i*i];
        for(S32 j = 0; j < 5; ++j)
        {
            o[4-j] = BASE85_TABLE[ me[i] % 85];
            word /= 85;
        }
    }
}

unsigned int decode( char const * fiveChars ) throw( bad_input_data )
{
    unsigned int ret = 0;
    for( S32 ix = 0; ix < 5; ++ix )
    {
        char * s = strchr( encodeTable, fiveChars[ ix ] );
        ret = ret * 85 + (s-encodeTable);
    }
    return ret;
}
*/

namespace
{
    // Value of each hex digit, -1 for everything else, so a nybble costs one
    // lookup rather than three range checks.
    constexpr auto UUID_NYBBLE = []
    {
        std::array<S8, 256> table{};
        for (auto& entry : table) entry = -1;
        for (S8 i = 0; i < 10; ++i) table[(size_t)('0' + i)] = i;
        for (S8 i = 0; i < 6;  ++i) table[(size_t)('a' + i)] = (S8)(10 + i);
        for (S8 i = 0; i < 6;  ++i) table[(size_t)('A' + i)] = (S8)(10 + i);
        return table;
    }();

    // The dashes fall after the 4th, 6th, 8th and 10th byte.
    constexpr bool uuid_dash_after(S32 byte) { return byte == 3 || byte == 5 || byte == 7 || byte == 9; }

    // A conversion of sixteen fixed-width fields does not need a format string
    // parsed at run time: sixteen %02x through snprintf costs 353 ns against
    // 5.8 ns for a table.
    template <typename CHAR>
    void uuid_to_chars(const U8* data, CHAR* out, const CHAR* hex)
    {
        CHAR* p = out;
        for (S32 i = 0; i < UUID_BYTES; ++i)
        {
            *p++ = hex[data[i] >> 4];
            *p++ = hex[data[i] & 0x0F];
            if (uuid_dash_after(i))
            {
                *p++ = CHAR('-');
            }
        }
        *p = CHAR('\0');
    }
}

void LLUUID::to_chars(char* out) const
{
    static const char hex[] = "0123456789abcdef";
    uuid_to_chars(mData, out, hex);
}

void LLUUID::to_wchars(wchar_t* out) const
{
    static const wchar_t hex[] = L"0123456789abcdef";
    uuid_to_chars(mData, out, hex);
}

// Common to all UUID implementations
void LLUUID::toString(std::string& out) const
{
    char buffer[UUID_STR_LENGTH];       /* Flawfinder: ignore */
    to_chars(buffer);
    out.assign(buffer, UUID_STR_SIZE);
}

void LLUUID::toCompressedString(std::string& out) const
{
    char bytes[UUID_BYTES + 1];
    memcpy(bytes, mData, UUID_BYTES);       /* Flawfinder: ignore */
    bytes[UUID_BYTES] = '\0';
    out.assign(bytes, UUID_BYTES);
}

std::string LLUUID::getString() const
{
    return asString();
}

std::string LLUUID::asString() const
{
    std::string str;
    toString(str);
    return str;
}

bool LLUUID::set(const char* in_string, bool emit)
{
    return set(ll_safe_string(in_string), emit);
}

bool LLUUID::set(const std::string& in_string, bool emit)
{
    bool broken_format = false;

    // empty strings should make NULL uuid
    if (in_string.empty())
    {
        setNull();
        return true;
    }

    if (in_string.length() != (UUID_STR_LENGTH - 1))        /* Flawfinder: ignore */
    {
        // I'm a moron.  First implementation didn't have the right UUID format.
        // Shouldn't see any of these any more
        if (in_string.length() == (UUID_STR_LENGTH - 2))    /* Flawfinder: ignore */
        {
            if (emit)
            {
                LL_WARNS() << "Warning! Using broken UUID string format" << LL_ENDL;
            }
            broken_format = true;
        }
        else
        {
            // Bad UUID string.  Spam as INFO, as most cases we don't care.
            if (emit)
            {
                //don't spam the logs because a resident can't spell.
                LL_WARNS() << "Bad UUID string: " << in_string << LL_ENDL;
            }
            setNull();
            return false;
        }
    }

    U8 cur_pos = 0;
    for (S32 i = 0; i < UUID_BYTES; i++)
    {
        if ((i == 4) || (i == 6) || (i == 8) || (i == 10))
        {
            cur_pos++;
            if (broken_format && (i == 10))
            {
                // Missing - in the broken format
                cur_pos--;
            }
        }

        // A table beats three range checks per nybble. in_string[size()] is a
        // defined read of the terminator, which the table rejects, so a string
        // that ends early fails here rather than being indexed past its end.
        const S8 hi = UUID_NYBBLE[(U8)in_string[cur_pos]];
        const S8 lo = UUID_NYBBLE[(U8)in_string[cur_pos + 1]];
        if ((hi | lo) < 0)
        {
            if (emit)
            {
                LL_WARNS() << "Invalid UUID string character" << LL_ENDL;
            }
            setNull();
            return false;
        }

        mData[i] = (U8)((hi << 4) | lo);
        cur_pos += 2;
    }

    return true;
}

bool LLUUID::validate(const std::string& in_string)
{
    bool broken_format = false;
    if (in_string.length() != (UUID_STR_LENGTH - 1))        /* Flawfinder: ignore */
    {
        // I'm a moron.  First implementation didn't have the right UUID format.
        if (in_string.length() == (UUID_STR_LENGTH - 2))        /* Flawfinder: ignore */
        {
            broken_format = true;
        }
        else
        {
            return false;
        }
    }

    U8 cur_pos = 0;
    for (U32 i = 0; i < 16; i++)
    {
        if ((i == 4) || (i == 6) || (i == 8) || (i == 10))
        {
            cur_pos++;
            if (broken_format && (i == 10))
            {
                // Missing - in the broken format
                cur_pos--;
            }
        }

        if ((in_string[cur_pos] >= '0') && (in_string[cur_pos] <= '9'))
        {
        }
        else if ((in_string[cur_pos] >= 'a') && (in_string[cur_pos] <= 'f'))
        {
        }
        else if ((in_string[cur_pos] >= 'A') && (in_string[cur_pos] <= 'F'))
        {
        }
        else
        {
            return false;
        }

        cur_pos++;

        if ((in_string[cur_pos] >= '0') && (in_string[cur_pos] <= '9'))
        {
        }
        else if ((in_string[cur_pos] >= 'a') && (in_string[cur_pos] <= 'f'))
        {
        }
        else if ((in_string[cur_pos] >= 'A') && (in_string[cur_pos] <= 'F'))
        {
        }
        else
        {
            return false;
        }
        cur_pos++;
    }
    return true;
}

const LLUUID& LLUUID::operator^=(const LLUUID& rhs)
{
    // memcpy through locals avoids strict-aliasing/alignment UB of reading
    // U8[16] as U32*. Result bit pattern matches the previous behavior.
    U64 me[2], other[2];
    memcpy(me,    mData,     UUID_BYTES);
    memcpy(other, rhs.mData, UUID_BYTES);
    me[0] ^= other[0];
    me[1] ^= other[1];
    memcpy(mData, me, UUID_BYTES);
    return *this;
}

LLUUID LLUUID::operator^(const LLUUID& rhs) const
{
    LLUUID id(*this);
    id ^= rhs;
    return id;
}

// WARNING: this algorithm SHALL NOT be changed. It is also used by the server
// and plays a role in some assets validation (e.g. clothing items). Changing
// it would cause invalid assets.
void LLUUID::combine(const LLUUID& other, LLUUID& result) const
{
    LLMD5 md5_uuid;
    md5_uuid.update((unsigned char*)mData, 16);
    md5_uuid.update((unsigned char*)other.mData, 16);
    md5_uuid.finalize();
    md5_uuid.raw_digest(result.mData);
}

LLUUID LLUUID::combine(const LLUUID& other) const
{
    LLUUID combination;
    combine(other, combination);
    return combination;
}

std::ostream& operator<<(std::ostream& s, const LLUUID& uuid)
{
    char uuid_str[UUID_STR_LENGTH];
    uuid.to_chars(uuid_str);
    s << uuid_str;
    return s;
}

std::istream& operator>>(std::istream& s, LLUUID& uuid)
{
    // operator>>(char&) skips whitespace ahead of each character and builds a
    // sentry for it, which is 36 sentries per id. The whitespace skipping is
    // kept -- callers may rely on it -- but it goes through the streambuf, and
    // a short read no longer leaves the tail of the buffer uninitialised.
    char uuid_str[UUID_STR_LENGTH];     /* Flawfinder: ignore */
    U32 i = 0;
    if (s.good())
    {
        std::streambuf* sb = s.rdbuf();
        for (; i < UUID_STR_LENGTH - 1; ++i)
        {
            int c = sb->sgetc();
            while (c != std::istream::traits_type::eof() && isspace(c))
            {
                sb->sbumpc();
                c = sb->sgetc();
            }
            if (c == std::istream::traits_type::eof())
            {
                s.setstate(std::ios::eofbit | std::ios::failbit);
                break;
            }
            uuid_str[i] = (char)sb->sbumpc();
        }
    }
    else
    {
        s.setstate(std::ios::failbit);
    }
    uuid_str[i] = '\0';
    uuid.set(std::string(uuid_str));
    return s;
}

static void get_random_bytes(void* buf, int nbytes)
{
    int i;
    char* cp = (char*)buf;

    // *NOTE: If we are not using the janky generator ll_rand()
    // generates at least 3 good bytes of data since it is 0 to
    // RAND_MAX. This could be made more efficient by copying all the
    // bytes.
    for (i = 0; i < nbytes; i++)
        * cp++ = ll_rand() & 0xFF;

    return;
}

#if LL_WINDOWS

// static
S32 LLUUID::getNodeID(unsigned char* node_id)
{
    static bool got_node_id = false;
    static unsigned char local_node_id[6];
    if (got_node_id)
    {
        memcpy(node_id, local_node_id, sizeof(local_node_id));
        return 1;
    }

    S32 retval = 0;
    PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
    ULONG outBufLen = 0U;
    DWORD dwRetVal = 0U;

    ULONG family = AF_INET;
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS;

    GetAdaptersAddresses(
        AF_INET,
        flags,
        nullptr,
        nullptr,
        &outBufLen);

    constexpr U32 MAX_TRIES = 3U;
    U32 iteration = 0U;
    do {

        pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(malloc(outBufLen));
        if (pAddresses == nullptr) {
            return 0;
        }

        dwRetVal =
            GetAdaptersAddresses(family, flags, nullptr, pAddresses, &outBufLen);

        if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
            free(pAddresses);
            pAddresses = nullptr;
        }
        else {
            break;
        }

        ++iteration;

    } while ((dwRetVal == ERROR_BUFFER_OVERFLOW) && (iteration < MAX_TRIES));

    if (dwRetVal == NO_ERROR)
    {
        PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
        PIP_ADAPTER_GATEWAY_ADDRESS pFirstGateway = nullptr;
        do {
            pFirstGateway = pCurrAddresses->FirstGatewayAddress;
            if (pFirstGateway)
            {
                if ((pCurrAddresses->IfType == IF_TYPE_ETHERNET_CSMACD || pCurrAddresses->IfType == IF_TYPE_IEEE80211) && pCurrAddresses->ConnectionType == NET_IF_CONNECTION_DEDICATED
                    && pCurrAddresses->OperStatus == IfOperStatusUp)
                {
                    if (pCurrAddresses->PhysicalAddressLength == 6)
                    {
                        for (size_t i = 0; i < 5; ++i)
                        {
                            node_id[i] = pCurrAddresses->PhysicalAddress[i];
                            local_node_id[i] = pCurrAddresses->PhysicalAddress[i];
                        }
                        retval = 1;
                        got_node_id = true;
                        break;
                    }
                }
            }
            pCurrAddresses = pCurrAddresses->Next;
        } while (pCurrAddresses);                    // Terminate if last adapter
    }

    if(pAddresses)
        free(pAddresses);
    pAddresses = nullptr;

    return retval;
}

#elif LL_DARWIN
// macOS version of the UUID generation code...
/*
 * Get an ethernet hardware address, if we can find it...
 */
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <ifaddrs.h>

 // static
S32 LLUUID::getNodeID(unsigned char* node_id)
{
    int i;
    unsigned char* a = NULL;
    struct ifaddrs* ifap, * ifa;
    int rv;
    S32 result = 0;

    if ((rv = getifaddrs(&ifap)) == -1)
    {
        return -1;
    }
    if (ifap == NULL)
    {
        return -1;
    }

    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next)
    {
        //      printf("Interface %s, address family %d, ", ifa->ifa_name, ifa->ifa_addr->sa_family);
        for (i = 0; i < ifa->ifa_addr->sa_len; i++)
        {
            //          printf("%02X ", (unsigned char)ifa->ifa_addr->sa_data[i]);
        }
        //      printf("\n");

        if (ifa->ifa_addr->sa_family == AF_LINK)
        {
            // This is a link-level address
            struct sockaddr_dl* lla = (struct sockaddr_dl*)ifa->ifa_addr;

            //          printf("\tLink level address, type %02X\n", lla->sdl_type);

            if (lla->sdl_type == IFT_ETHER)
            {
                // Use the first ethernet MAC in the list.
                // For some reason, the macro LLADDR() defined in net/if_dl.h doesn't expand correctly.  This is what it would do.
                a = (unsigned char*)&((lla)->sdl_data);
                a += (lla)->sdl_nlen;

                if (!a[0] && !a[1] && !a[2] && !a[3] && !a[4] && !a[5])
                {
                    continue;
                }

                if (node_id)
                {
                    memcpy(node_id, a, 6);
                    result = 1;
                }

                // We found one.
                break;
            }
        }
    }
    freeifaddrs(ifap);

    return result;
}

#else

// Linux version of the UUID generation code...
/*
 * Get the ethernet hardware address, if we can find it...
 */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#define HAVE_NETINET_IN_H
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#if !LL_DARWIN
#include <linux/sockios.h>
#endif
#endif

 // static
S32 LLUUID::getNodeID(unsigned char* node_id)
{
    int         sd;
    struct ifreq    ifr, * ifrp;
    struct ifconf   ifc;
    char buf[1024];
    int     n, i;
    unsigned char* a;

    /*
     * BSD 4.4 defines the size of an ifreq to be
     * max(sizeof(ifreq), sizeof(ifreq.ifr_name)+ifreq.ifr_addr.sa_len
     * However, under earlier systems, sa_len isn't present, so the size is
     * just sizeof(struct ifreq)
     */
#ifdef HAVE_SA_LEN
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#define ifreq_size(i) max(sizeof(struct ifreq),\
     sizeof((i).ifr_name)+(i).ifr_addr.sa_len)
#else
#define ifreq_size(i) sizeof(struct ifreq)
#endif /* HAVE_SA_LEN*/

    sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sd < 0) {
        return -1;
    }
    memset(buf, 0, sizeof(buf));
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sd, SIOCGIFCONF, (char*)&ifc) < 0) {
        close(sd);
        return -1;
    }
    n = ifc.ifc_len;
    for (i = 0; i < n; i += ifreq_size(*ifr)) {
        ifrp = (struct ifreq*)((char*)ifc.ifc_buf + i);
        strncpy(ifr.ifr_name, ifrp->ifr_name, IFNAMSIZ);        /* Flawfinder: ignore */
#ifdef SIOCGIFHWADDR
        if (ioctl(sd, SIOCGIFHWADDR, &ifr) < 0)
            continue;
        a = (unsigned char*)&ifr.ifr_hwaddr.sa_data;
#else
#ifdef SIOCGENADDR
        if (ioctl(sd, SIOCGENADDR, &ifr) < 0)
            continue;
        a = (unsigned char*)ifr.ifr_enaddr;
#else
        /*
         * XXX we don't have a way of getting the hardware
         * address
         */
        close(sd);
        return 0;
#endif /* SIOCGENADDR */
#endif /* SIOCGIFHWADDR */
        if (!a[0] && !a[1] && !a[2] && !a[3] && !a[4] && !a[5])
            continue;
        if (node_id) {
            memcpy(node_id, a, 6);      /* Flawfinder: ignore */
            close(sd);
            return 1;
        }
    }
    close(sd);
    return 0;
}

#endif

S32 LLUUID::cmpTime(uuid_time_t* t1, uuid_time_t* t2)
{
    // Compare two time values.

    if (t1->high < t2->high) return -1;
    if (t1->high > t2->high) return 1;
    if (t1->low < t2->low)  return -1;
    if (t1->low > t2->low)  return 1;
    return 0;
}

void LLUUID::getSystemTime(uuid_time_t* timestamp)
{
    // Get system time with 100ns precision. Time is since Oct 15, 1582.
#if LL_WINDOWS
    ULARGE_INTEGER time;
    GetSystemTimeAsFileTime((FILETIME*)&time);
    // NT keeps time in FILETIME format which is 100ns ticks since
    // Jan 1, 1601. UUIDs use time in 100ns ticks since Oct 15, 1582.
    // The difference is 17 Days in Oct + 30 (Nov) + 31 (Dec)
    // + 18 years and 5 leap days.
    time.QuadPart +=
        (unsigned __int64)(1000 * 1000 * 10)       // seconds
        * (unsigned __int64)(60 * 60 * 24)       // days
        * (unsigned __int64)(17 + 30 + 31 + 365 * 18 + 5); // # of days

    timestamp->high = time.HighPart;
    timestamp->low = time.LowPart;
#else
    struct timeval tp;
    gettimeofday(&tp, 0);

    // Offset between UUID formatted times and Unix formatted times.
    // UUID UTC base time is October 15, 1582.
    // Unix base time is January 1, 1970.
    U64 uuid_time = ((U64)tp.tv_sec * 10000000) + (tp.tv_usec * 10) +
        U64L(0x01B21DD213814000);
    timestamp->high = (U32)(uuid_time >> 32);
    timestamp->low = (U32)(uuid_time & 0xFFFFFFFF);
#endif
}

void LLUUID::getCurrentTime(uuid_time_t* timestamp)
{
    // Get current time as 60 bit 100ns ticks since whenever.
    // Compensate for the fact that real clock resolution is less
    // than 100ns.

    const U32 uuids_per_tick = 1024;

    static uuid_time_t time_last;
    static U32    uuids_this_tick;
    static bool     init = false;

    if (!init) {
        getSystemTime(&time_last);
        uuids_this_tick = uuids_per_tick;
        init = true;
        mMutex = new LLMutex();
    }

    uuid_time_t time_now = { 0,0 };

    while (1) {
        getSystemTime(&time_now);

        // if clock reading changed since last UUID generated
        if (cmpTime(&time_last, &time_now)) {
            // reset count of uuid's generated with this clock reading
            uuids_this_tick = 0;
            break;
        }
        if (uuids_this_tick < uuids_per_tick) {
            uuids_this_tick++;
            break;
        }
        // going too fast for our clock; spin
    }

    time_last = time_now;

    if (uuids_this_tick != 0) {
        if (time_now.low & 0x80000000) {
            time_now.low += uuids_this_tick;
            if (!(time_now.low & 0x80000000))
                time_now.high++;
        }
        else
            time_now.low += uuids_this_tick;
    }

    timestamp->high = time_now.high;
    timestamp->low = time_now.low;
}

void LLUUID::generate()
{
    // Create a UUID.
    uuid_time_t timestamp;

    static unsigned char node_id[6];    /* Flawfinder: ignore */
    static int has_init = 0;

    // Create a UUID.
    static uuid_time_t time_last = { 0,0 };
    static U16 clock_seq = 0;

    if (!has_init)
    {
        has_init = 1;
        if (getNodeID(node_id) <= 0)
        {
            get_random_bytes(node_id, 6);
            /*
             * Set multicast bit, to prevent conflicts
             * with IEEE 802 addresses obtained from
             * network cards
             */
            node_id[0] |= 0x80;
        }

        getCurrentTime(&time_last);

        clock_seq = (U16)ll_rand(65536);
    }

    // get current time
    getCurrentTime(&timestamp);
    U16 our_clock_seq = clock_seq;

    // if clock hasn't changed or went backward, change clockseq
    if (cmpTime(&timestamp, &time_last) != 1)
    {
        LLMutexLock lock(mMutex);
        clock_seq = (clock_seq + 1) & 0x3FFF;
        if (clock_seq == 0)
            clock_seq++;
        our_clock_seq = clock_seq;  // Ensure we're using a different clock_seq value from previous time
    }

    time_last = timestamp;

    memcpy(mData + 10, node_id, 6);     /* Flawfinder: ignore */
    U32 tmp;
    tmp = timestamp.low;
    mData[3] = (unsigned char)tmp;
    tmp >>= 8;
    mData[2] = (unsigned char)tmp;
    tmp >>= 8;
    mData[1] = (unsigned char)tmp;
    tmp >>= 8;
    mData[0] = (unsigned char)tmp;

    tmp = (U16)timestamp.high;
    mData[5] = (unsigned char)tmp;
    tmp >>= 8;
    mData[4] = (unsigned char)tmp;

    tmp = (timestamp.high >> 16) | 0x1000;
    mData[7] = (unsigned char)tmp;
    tmp >>= 8;
    mData[6] = (unsigned char)tmp;

    tmp = our_clock_seq;

    mData[9] = (unsigned char)tmp;
    tmp >>= 8;
    mData[8] = (unsigned char)tmp;

    LLMD5 md5_uuid;

    md5_uuid.update(mData, 16);
    md5_uuid.finalize();
    md5_uuid.raw_digest(mData);
}

void LLUUID::generate(const std::string& hash_string)
{
    LLMD5 md5_uuid((U8*)hash_string.c_str());
    md5_uuid.raw_digest(mData);
}

bool LLUUID::parseUUID(const std::string& buf, LLUUID* value)
{
    if (buf.empty() || value == NULL)
    {
        return false;
    }

    std::string temp(buf);
    LLStringUtil::trim(temp);
    if (LLUUID::validate(temp))
    {
        value->set(temp);
        return true;
    }
    return false;
}

//static
LLUUID LLUUID::generateNewID()
{
    LLUUID new_id;
    new_id.generate();
    return new_id;
}

//static
LLUUID LLUUID::generateNewID(const std::string& hash_string)
{
    LLUUID new_id;
    if (hash_string.empty())
    {
        new_id.generate();
    }
    else
    {
        new_id.generate(hash_string);
    }
    return new_id;
}

LLAssetID LLTransactionID::makeAssetID(const LLUUID& session) const
{
    LLAssetID result;
    if (isNull())
    {
        result.setNull();
    }
    else
    {
        combine(session, result);
    }
    return result;
}

void LLUUID::setNull()
{
    memset(mData, 0, UUID_BYTES);
}


// memcmp lets the compiler emit a single SIMD compare on a fixed 16-byte
// payload while avoiding the strict-aliasing/alignment UB of reading U8[16]
// as U32*.
bool LLUUID::operator==(const LLUUID& rhs) const
{
    return memcmp(mData, rhs.mData, UUID_BYTES) == 0;
}


bool LLUUID::operator!=(const LLUUID& rhs) const
{
    return memcmp(mData, rhs.mData, UUID_BYTES) != 0;
}

/*
// JC: This is dangerous.  It allows UUIDs to be cast automatically
// to integers, among other things.  Use isNull() or notNull().
 LLUUID::operator bool() const
{
    U32 *word = (U32 *)mData;
    return (word[0] | word[1] | word[2] | word[3]) > 0;
}
*/

bool LLUUID::notNull() const
{
    U64 a, b;
    memcpy(&a, mData,     sizeof(a));
    memcpy(&b, mData + 8, sizeof(b));
    return (a | b) != 0;
}

// Faster than == LLUUID::null because doesn't require
// as much memory access.
bool LLUUID::isNull() const
{
    U64 a, b;
    memcpy(&a, mData,     sizeof(a));
    memcpy(&b, mData + 8, sizeof(b));
    return (a | b) == 0;
}

LLUUID::LLUUID(const char* in_string)
{
    if (!in_string || in_string[0] == 0)
    {
        setNull();
        return;
    }

    set(in_string);
}

LLUUID::LLUUID(const std::string& in_string)
{
    if (in_string.empty())
    {
        setNull();
        return;
    }

    set(in_string);
}

// IW: DON'T "optimize" these by reading mData through a wider integer type --
// reinterpreting the bytes as U32/U64 reverses the sort order on LE hosts.
// memcmp is fine: it is defined as byte-wise unsigned comparison and matches
// the byte-by-byte loop exactly.
bool LLUUID::operator<(const LLUUID& rhs) const
{
    return memcmp(mData, rhs.mData, UUID_BYTES) < 0;
}

bool LLUUID::operator>(const LLUUID& rhs) const
{
    return memcmp(mData, rhs.mData, UUID_BYTES) > 0;
}

U16 LLUUID::getCRC16() const
{
    // A UUID is 16 bytes, or 8 shorts. Copy into aligned locals to avoid
    // strict-aliasing/alignment UB; the bit-level sum is unchanged per-platform.
    U16 shorts[UUID_BYTES / sizeof(U16)];
    memcpy(shorts, mData, UUID_BYTES);
    U16 out = 0;
    for (size_t i = 0; i < UUID_BYTES / sizeof(U16); ++i)
    {
        out += shorts[i];
    }
    return out;
}

U32 LLUUID::getCRC32() const
{
    U32 words[UUID_BYTES / sizeof(U32)];
    memcpy(words, mData, UUID_BYTES);
    return words[0] + words[1] + words[2] + words[3];
}
