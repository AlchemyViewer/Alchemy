/**
 * @file llxorcipher_tut.cpp
 * @author Adroit
 * @date 2007-03
 * @brief llxorcipher, llnullcipher test cases.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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
#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"
#include "llxorcipher.h"

namespace
{
    //encrypt->decrypt
    TEST_CASE("cipher: encrypt then decrypt with the same pad round-trips the data", "[cipher]")
    {
        const U32 len = 3;
        const U8 pad[] = "abc";
        const char str[] = "SecondLife";
        const S32 str_len = sizeof(str);
        U8 encrypted[str_len];
        U8 decrypted[str_len];
        LLXORCipher xorCipher(pad, len);
        LLXORCipher xorCipher1(pad, len);

        U32 length = xorCipher.requiredEncryptionSpace(50);
        UNSCOPED_INFO("requiredEncryptionSpace() function failed");
        CHECK(length == 50);

        U32 lenEncrypted = xorCipher.encrypt((U8 *) str, str_len, encrypted, str_len);
        UNSCOPED_INFO("Encryption failed");
        CHECK(lenEncrypted == str_len);
        U32 lenDecrypted = xorCipher1.decrypt(encrypted, str_len, decrypted, str_len);
        UNSCOPED_INFO("Decryption failed");
        CHECK(lenDecrypted == str_len);
        UNSCOPED_INFO("LLXORCipher Encrypt/Decrypt failed");
        CHECK_THAT(ll_test::Memory(str, str_len), ll_test::MemoryMatches(decrypted, lenDecrypted));
    }

    // operator=
    TEST_CASE("cipher: operator= copies the pad so the copy decrypts", "[cipher]")
    {
        const U8 pad[] = "ABCDEFGHIJKLMNOPQ"; // pad len longer than data to be ciphered
        const U32 pad_len = sizeof(pad);
        const U8 pad1[] = "SecondLife";
        const U32 pad_len1 = sizeof(pad1);
        const char str[] = "To Be Ciphered";
        const S32 str_len = sizeof(str);
        U8 encrypted[str_len];
        U8 decrypted[str_len];

        LLXORCipher xorCipher(pad, pad_len);
        LLXORCipher xorCipher1(pad1, pad_len1);

        xorCipher.encrypt((U8 *) str, str_len, encrypted, str_len);
        // make xorCipher1 same as xorCipher..so that xorCipher1 can decrypt what was
        // encrypted using xorCipher
        xorCipher1 = xorCipher;
        U32 lenDecrypted = xorCipher1.decrypt(encrypted, str_len, decrypted, str_len);
        UNSCOPED_INFO("LLXORCipher operator= failed");
        CHECK_THAT(ll_test::Memory(str, str_len), ll_test::MemoryMatches(decrypted, lenDecrypted));
    }

    //in place encrypt->decrypt
    TEST_CASE("cipher: in-place encrypt changes the data and decrypt restores it", "[cipher]")
    {
        U32 padNum = 0x12349087;
        const U8* pad = (U8*) &padNum;
        const U32 pad_len = sizeof(U32);
        char str[] = "To Be Ciphered a long string.........!!!.";
        char str1[] = "To Be Ciphered a long string.........!!!."; // same as str
        const S32 str_len = sizeof(str);

        LLXORCipher xorCipher(pad, pad_len);
        LLXORCipher xorCipher1(pad, pad_len);
        xorCipher.encrypt((U8 *) str, str_len);
        // it should not be the same as original data!
        UNSCOPED_INFO("LLXORCipher: In Place encrypt failed");
        CHECK(memcmp(str, str1, str_len) != 0);
        xorCipher1.decrypt((U8 *) str, str_len);
        // it should not be the same as original data!
        UNSCOPED_INFO("LLXORCipher: In Place decrypt failed");
        CHECK_THAT(ll_test::Memory(str, str_len), ll_test::MemoryMatches(str1, str_len));
    }
}
