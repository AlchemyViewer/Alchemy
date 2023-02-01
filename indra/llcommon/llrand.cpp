/** 
 * @file llrand.cpp
 * @brief Global random generator.
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

#include "llrand.h"
#include "lluuid.h"

#include <random>

/**
 * Through analysis, we have decided that we want to take values which
 * are close enough to 1.0 to map back to 0.0.  We came to this
 * conclusion from noting that:
 *
 * [0.0, 1.0)
 *
 * when scaled to the integer set:
 *
 * [0, 4)
 *
 * there is some value close enough to 1.0 that when multiplying by 4,
 * gets truncated to 4. Therefore:
 *
 * [0,1-eps] => 0
 * [1,2-eps] => 1
 * [2,3-eps] => 2
 * [3,4-eps] => 3
 *
 * So 0 gets uneven distribution if we simply clamp. The actual
 * clamp utilized in this file is to map values out of range back
 * to 0 to restore uniform distribution.
 *
 * Also, for clamping floats when asking for a distribution from
 * [0.0,g) we have determined that for values of g < 0.5, then
 * rand*g=g, which is not the desired result. As above, we clamp to 0
 * to restore uniform distribution.
 */

static thread_local std::unique_ptr<std::ranlux48> __generator;
inline std::ranlux48* _generator()
{
	if (!__generator.get())
	{
		std::random_device seeder;
		__generator = std::make_unique<std::ranlux48>(seeder());
	}
	return __generator.get();
}

S32 ll_rand()
{
	return ll_rand(S32_MAX);
}

S32 ll_rand(S32 val)
{
	if (val == 0) return 0;

	// The clamping rules are described above.
	S32 rv;
	if (val > 0)
	{
		rv = std::uniform_int_distribution<S32>(0, val)(*_generator());
	}
	else
	{
		rv = std::uniform_int_distribution<S32>(val, 0)(*_generator());
	}
	if (rv == val) return 0;
	return rv;
}

F32 ll_frand()
{
	// see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=63176
	// and https://llvm.org/bugs/show_bug.cgi?id=18767
	// and https://github.com/microsoft/STL/issues/1074
	return (F32)std::generate_canonical<F64, 10>((*_generator()));
}

F32 ll_frand(F32 val)
{
	F32 rv = ll_frand() * val;
	if (val > 0.0f)
	{
		if (rv >= val) return 0.0f;
	}
	else
	{
		if (rv <= val) return 0.0f;
	}
	return rv;
}

F64 ll_drand()
{
	return std::generate_canonical<F64, 10>((*_generator()));
}

F64 ll_drand(F64 val)
{
	F64 rv = ll_drand() * val;
	if (val > 0.0)
	{
		if (rv >= val) return 0.0;
	}
	else
	{
		if(rv <= val) return 0.0;
	}
	return rv;
}
