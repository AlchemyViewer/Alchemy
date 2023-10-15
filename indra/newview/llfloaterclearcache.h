/** 
 * @file llfloaterclearcache.h
 * @brief Floater to set caches to clear
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2023, Rye Mutt <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#ifndef LL_LLFLOATERCLEARCACHE_H
#define LL_LLFLOATERCLEARCACHE_H

#include "llmodaldialog.h"

class LLComboBox;
class LLRadioGroup;
class LLLineEditor;

class LLFloaterClearCache final : public LLModalDialog
{

public:
	LLFloaterClearCache(const LLSD &key);

	BOOL postBuild() override;
	void onOpen(const LLSD& key) override;

	void onBtnClearSelected();
	void onBtnClearAll();
	void onBtnCancel();

	void refreshButtons();

private:
	LLButton*		mOkButton = nullptr;
};

#endif // LL_LLFLOATERCLEARCACHE_H
