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

#include <boost/random/lagged_fibonacci.hpp>

static boost::lagged_fibonacci2281 gRandomGenerator(LLUUID::getRandomSeed());
inline F64 ll_internal_random_double()
{
	F64 rv = gRandomGenerator();
	if(!((rv >= 0.0) && (rv < 1.0))) return fmod(rv, 1.0);
	return rv;
}

inline F32 ll_internal_random_float()
{
	// The clamping rules are described above.
	F32 rv = (F32)gRandomGenerator();
	if(!((rv >= 0.0f) && (rv < 1.0f))) return fmod(rv, 1.f);
	return rv;
}

S32 ll_rand()
{
	return ll_rand(RAND_MAX);
}

S32 ll_rand(S32 val)
{
	S32 rv = (S32)(ll_internal_random_double() * val);
	if(rv == val) return 0;
	return rv;
}

F32 ll_frand()
{
	return ll_internal_random_float();
}

F32 ll_frand(F32 val)
{
	// The clamping rules are described above.
	F32 rv = ll_internal_random_float() * val;
	if(val > 0)
	{
		if(rv >= val) return 0.0f;
	}
	else
	{
		if(rv <= val) return 0.0f;
	}
	return rv;
}

F64 ll_drand()
{
	return ll_internal_random_double();
}

F64 ll_drand(F64 val)
{
	// The clamping rules are described above.
	F64 rv = ll_internal_random_double() * val;
	if(val > 0)
	{
		if(rv >= val) return 0.0;
	}
	else
	{
		if(rv <= val) return 0.0;
	}
	return rv;
}
