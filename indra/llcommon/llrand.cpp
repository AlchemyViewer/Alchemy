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

#include <absl/random/bit_gen_ref.h>
#include <absl/random/random.h>

static absl::BitGenRef tls_bit_gen()
{
	thread_local static absl::BitGen tlsBitGen;
	return tlsBitGen;
}

S32 ll_rand()
{
	return ll_rand(RAND_MAX);
}

S32 ll_rand(S32 val)
{
	return absl::Uniform<S32>(absl::IntervalClosedOpen, tls_bit_gen(), 0, val);
}

F32 ll_frand()
{
	return absl::Uniform<float>(absl::IntervalClosedOpen, tls_bit_gen(), 0.f, 1.f);
}

F32 ll_frand(F32 val)
{
	return absl::Uniform<float>(absl::IntervalClosedOpen, tls_bit_gen(), 0.f, val);
}

F64 ll_drand()
{
	return absl::Uniform<double>(absl::IntervalClosedOpen, tls_bit_gen(), 0.0, 1.0);
}

F64 ll_drand(F64 val)
{
	return absl::Uniform<double>(absl::IntervalClosedOpen, tls_bit_gen(), 0.0, val);
}
