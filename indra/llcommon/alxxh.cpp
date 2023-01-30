/**
 * @file alxxh.cpp
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

#include "linden_common.h"

#include "alxxh.h"

#include <iostream>		// cerr

#define XXH_INLINE_ALL
#include "xxhash.h"

ALXXH::ALXXH()
{
	init();
}

ALXXH::~ALXXH()
{
	if (mState)
	{
		XXH3_freeState((XXH3_state_t*)mState);
	}
}

ALXXH::ALXXH(const ALXXH& rhs)
{
	init();
	if(rhs.mState)
	{
		XXH3_copyState((XXH3_state_t*)mState, (XXH3_state_t*)rhs.mState);
	}
	mDigest = rhs.mDigest;
}
ALXXH::ALXXH(ALXXH&& rhs)
{
	mState = rhs.mState;
	rhs.mState = nullptr;

	mDigest = rhs.mDigest;
}

ALXXH& ALXXH::operator=(const ALXXH& rhs) 
{
	if(!mState)
	{
		init();
	}
	if(rhs.mState)
	{
		XXH3_copyState((XXH3_state_t*)mState, (XXH3_state_t*)rhs.mState);
	}
	mDigest = rhs.mDigest;
	return *this;
}

ALXXH& ALXXH::operator=(ALXXH&& rhs) 
{
	if (mState)
	{
		XXH3_freeState((XXH3_state_t*)mState);
	}

	mState = rhs.mState;
	rhs.mState = nullptr;

	mDigest = rhs.mDigest;
	return *this;
}

ALXXH::ALXXH(FILE* file)
{

	init();  // must be called be all constructors
	update(file);
	finalize();
}

ALXXH::ALXXH(std::istream& stream)
{

	init();  // must called by all constructors
	update(stream);
	finalize();
}

// Digest a string of the format ("%s:%i" % (s, number))
ALXXH::ALXXH(const unsigned char* string, const unsigned int number)
{
	const char* colon = ":";
	char tbuf[16];		/* Flawfinder: ignore */
	init();
	update(string, (U32)strlen((const char*)string));		/* Flawfinder: ignore */
	update((const unsigned char*)colon, (U32)strlen(colon));		/* Flawfinder: ignore */
	snprintf(tbuf, sizeof(tbuf), "%i", number);	/* Flawfinder: ignore */
	update((const unsigned char*)tbuf, (U32)strlen(tbuf));	/* Flawfinder: ignore */
	finalize();
}

// Digest a string
ALXXH::ALXXH(const unsigned char* s)
{
	init();
	update(s, (U32)strlen((const char*)s));		/* Flawfinder: ignore */
	finalize();
}

void ALXXH::update(const void* input, const size_t input_length)
{
	if (mState)
	{
		XXH3_64bits_update((XXH3_state_t*)mState, input, input_length);
	}
}

void ALXXH::update(FILE* file)
{
	if (mState)
	{
		unsigned char buffer[BLOCK_LEN];		/* Flawfinder: ignore */
		int len;

		while ((len = (int)fread(buffer, 1, BLOCK_LEN, file)))
			update(buffer, len);
	}
	fclose(file);
}

void ALXXH::update(std::istream& stream)
{
	if (mState)
	{
		unsigned char buffer[BLOCK_LEN];		/* Flawfinder: ignore */
		int len;

		while (stream.good()) {
			stream.read((char*)buffer, BLOCK_LEN); 	/* Flawfinder: ignore */		// note that return value of read is unusable.
			len = (int)stream.gcount();
			update(buffer, len);
		}
	}
}

void  ALXXH::update(std::string_view instr)
{
	if (mState && !instr.empty())
	{
		XXH3_64bits_update((XXH3_state_t*)mState, (const void*)instr.data(), instr.size());
	}
}

U64 ALXXH::digest() const
{
	return mState ? XXH3_64bits_digest((XXH3_state_t*)mState) : mDigest;
}

void ALXXH::finalize()
{
	if (!mState)
	{
		return;
	}
	mDigest = XXH3_64bits_digest((XXH3_state_t*)mState);
	XXH3_freeState((XXH3_state_t*)mState);
	mState = nullptr;
}

std::ostream& operator<<(std::ostream& stream, ALXXH context)
{
	stream << context.digest();
	return stream;
}

bool operator==(const ALXXH& a, const ALXXH& b)
{
	return a.digest() == b.digest();
}

bool operator!=(const ALXXH& a, const ALXXH& b)
{
	return !(a == b);
}

void ALXXH::init()
{
	mDigest = 0;
	mState = (void*)XXH3_createState();
	if (!mState || XXH3_64bits_reset((XXH3_state_t*)mState) == XXH_ERROR)
	{
		LL_WARNS() << "Failed to init xxhash" << LL_ENDL;
	}
}

