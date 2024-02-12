/**
 * @file llfloatermessagerewriter.h
 *
 * $LicenseInfo:firstyear=2015&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#ifndef LL_LLFLOATERMESSAGEREWRITER_H
#define LL_LLFLOATERMESSAGEREWRITER_H

#include "llfloater.h"
#include "lltemplatemessagereader.h"

class LLFloaterMessageRewriter final : public LLFloater
{
public:
	LLFloaterMessageRewriter(const LLSD& key);
	~LLFloaterMessageRewriter() = default;
	BOOL postBuild() override;
	
private:
	void onClickSaveRules();
	void onClickNewRule();
	void refreshRuleList();
};

#endif // LL_LLFLOATERMESSAGEREWRITER_H
