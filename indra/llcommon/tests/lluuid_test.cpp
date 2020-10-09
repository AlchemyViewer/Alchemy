/**
 * @file lluuid_test.cpp
 * @author Rye Mutt <rye@alchemyviewer.org>
 * @date 2020-10-08
 *
 * $LicenseInfo:firstyear=2020&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2020, Alchemy Development Group
 
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
#include "../test/lltut.h"

#include "../lluuid.h"

const std::string TEST_ID_STR("ba2a564a-f0f1-4b82-9c61-b7520bfcd09f");
const LLUUID TEST_ID("ba2a564a-f0f1-4b82-9c61-b7520bfcd09f");

namespace tut
{
	struct uuid
	{
	};

	typedef test_group<uuid> uuid_t;
	typedef uuid_t::object uuid_object_t;
	tut::uuid_t tut_uuid("LLUUID");

	template<> template<>
	void uuid_object_t::test<1>()
	{
		std::string out_str;
		TEST_ID.toString(out_str);
		ensure_equals(out_str, TEST_ID_STR);
	}

	template<> template<>
	void uuid_object_t::test<2>()
	{
		char out_cstr[UUID_STR_SIZE] = {};
		TEST_ID.toString(out_cstr);
		ensure(strncmp(out_cstr, TEST_ID_STR.c_str(), UUID_STR_SIZE) == 0);
	}

	template<> template<>
	void uuid_object_t::test<3>()
	{
		auto str = absl::StrFormat("%s", TEST_ID);
		ensure_equals(str, TEST_ID_STR);
	}

	template<> template<>
	void uuid_object_t::test<4>()
	{
		char out_cstr[UUID_STR_SIZE] = {};
		absl::SNPrintF(out_cstr, UUID_STR_SIZE, "%s", TEST_ID);
		ensure_equals(out_cstr, TEST_ID_STR);
	}
}
