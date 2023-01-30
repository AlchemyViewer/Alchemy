/**
 * @file alxxh.h
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

#ifndef AL_ALXXH_H
#define AL_ALXXH_H

class LL_COMMON_API ALXXH
{
	// how many bytes to grab at a time when checking files
	static constexpr size_t BLOCK_LEN = 4096;

public:
	ALXXH();
	~ALXXH();

	ALXXH(const ALXXH& rhs);
	ALXXH(ALXXH&& rhs);

	ALXXH& operator=(const ALXXH& rhs);
	ALXXH& operator=(ALXXH&& rhs);

	ALXXH(const unsigned char* string); // digest string, finalize
	ALXXH(std::istream& stream);       // digest stream, finalize
	ALXXH(FILE* file);            // digest file, close, finalize
	ALXXH(const unsigned char* string, const unsigned int number);

	// methods for controlled operation:
	void  update(const void* input, const size_t input_length);
	void  update(std::istream& stream);
	void  update(FILE* file);
	void  update(std::string_view str);
	U64   digest() const;
	void  finalize();

	friend LL_COMMON_API std::ostream& operator<< (std::ostream&, ALXXH context);

private:
	void init(); // called by all constructors

	void* mState;
	U64 mDigest;
};

LL_COMMON_API bool operator==(const ALXXH& a, const ALXXH& b);
LL_COMMON_API bool operator!=(const ALXXH& a, const ALXXH& b);

#endif // LL_LLMD5_H
