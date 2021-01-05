/**
 *
 * Copyright (c) 2011-2016, Kitty Barnett
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 * By copying, modifying or distributing this software, you acknowledge that
 * you have read and understood your obligations described above, and agree to
 * abide by those obligations.
 *
 */

#ifndef LL_FLOATERSCRIPTRECOVER_H
#define LL_FLOATERSCRIPTRECOVER_H

#include "llfloater.h"

// ============================================================================
// LLFloaterAssetRecovery class
//

class LLFloaterAssetRecovery : public LLFloater
{
	friend class LLFloaterReg;
private:
	LLFloaterAssetRecovery(const LLSD& sdKey);

	/*
	 * LLFloater overrides
	 */
public:
	/*virtual*/ void onOpen(const LLSD& sdKey);
	/*virtual*/ BOOL postBuild();

	/*
	 * Member functions
	 */
protected:
	void onBtnCancel();
	void onBtnRecover();
};

// ============================================================================
// LLAssetRecoverQueue class
//

class LLAssetRecoverQueue
{
	friend class LLCreateRecoverAssetCallback;
	friend class LLFloaterAssetRecovery;

	struct LLAssetRecoverItem
	{
		std::string strPath;
		std::string strName;
		std::string strDescription;
		LLAssetType::EType eAssetType;
		LLInventoryType::EType eInvType;
		U32 nNextOwnerPerm;
		LLUUID idItem;

		LLAssetRecoverItem() : eAssetType(LLAssetType::AT_NONE), eInvType(LLInventoryType::IT_NONE) {}
	};
protected:
	LLAssetRecoverQueue(const LLSD& sdFiles);

	/*
	 * Member functions
	 */
public:
	static void recoverIfNeeded();
protected:
	bool recoverNext();

	void onCreateItem(const LLUUID& idItem);
	void onSavedAsset(const LLUUID& idItem, const LLSD& sdResponse);
	void onUploadError(const LLUUID& idItem);

	/*
	 * Member variables
	 */
protected:
	typedef std::list<LLAssetRecoverItem> recovery_list_t;
	recovery_list_t m_RecoveryQueue;
};

// ============================================================================

#endif // LL_FLOATERSCRIPTRECOVER_H
