/**
 * @file alfloaterao.h
 * @brief Animation overrider controls
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Copyright (C) 2011, Zi Ree @ Second Life
 * Copyright (C) 2016, Cinder <cinder@sdf.org>
 * Copyright (C) 2020, Rye Mutt <rye@alchemyviewer.org>
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

#ifndef AL_FLOATERAO_H
#define AL_FLOATERAO_H

#include "lleventtimer.h"
#include "lltransientdockablefloater.h"
#include "alaoset.h"

class LLButton;
class LLComboBox;
class LLCheckBoxCtrl;
class LLScrollListCtrl;
class LLScrollListItem;
class LLSpinCtrl;
class LLTextBox;

class ALFloaterAO final : public LLTransientDockableFloater, public LLEventTimer
{
	friend class LLFloaterReg;
	friend class LLPanelAOMini;
	
private:
	ALFloaterAO(const LLSD& key);
	~ALFloaterAO();
	
public:
	BOOL postBuild() override;
	void updateList();
	void updateSetParameters();
	void updateAnimationList();
	static ALFloaterAO* getInstance();
	
	BOOL handleDragAndDrop(S32 x, S32 y, MASK mask, BOOL drop, EDragAndDropType cargo_type, void* cargo_data,
						   EAcceptance* accept, std::string& tooltip_msg) override;
	
protected:
	void onClickPrevious();
	void onClickNext();
	ALAOSet* getSelectedSet() const { return mSelectedSet; }
	
private:
	LLScrollListItem* addAnimation(const std::string& name);
	void onAnimationChanged(const LLUUID& animation);
	void reloading(const bool reload);
	
	void onSelectSet(const LLSD& userdata);
	void onRenameSet();
	void onSelectState();
	void onChangeAnimationSelection();
	void onClickReload();
	void onClickAdd();
	void onClickRemove();
	void onClickActivate();
	void onCheckDefault();
	void onCheckOverrideSits();
	void onCheckSmart();
	void onCheckDisableStands();
	void onClickMoveUp();
	void onClickMoveDown();
	void onClickTrash();
	void onCheckCycle();
	void onCheckRandomize();
	void onChangeCycleTime();
	
	void updateSmart();
	void updateCycleParameters();
	
	void enableSetControls(const bool enable);
	void enableStateControls(const bool enable);
	
	BOOL newSetCallback(const LLSD& notification, const LLSD& response);
	BOOL removeSetCallback(const LLSD& notification, const LLSD& response);
	
	virtual BOOL tick() override;
	
	std::vector<ALAOSet*> mSetList;
	ALAOSet* mSelectedSet;
	ALAOSet::AOState* mSelectedState;
	
	LLPanel* mReloadCoverPanel;
	
	// Full interface
	
	LLPanel* mMainInterfacePanel;
	
	LLComboBox* mSetSelector;
	LLButton* mActivateSetButton;
	LLButton* mAddButton;
	LLButton* mRemoveButton;
	LLCheckBoxCtrl* mDefaultCheckBox;
	LLCheckBoxCtrl* mOverrideSitsCheckBox;
	LLCheckBoxCtrl* mSmartCheckBox;
	LLCheckBoxCtrl* mDisableMouselookCheckBox;
	
	LLComboBox* mStateSelector;
	LLScrollListCtrl* mAnimationList;
	LLScrollListItem* mCurrentBoldItem;
	LLButton* mMoveUpButton;
	LLButton* mMoveDownButton;
	LLButton* mTrashButton;
	LLCheckBoxCtrl* mCycleCheckBox;
	LLCheckBoxCtrl* mRandomizeCheckBox;
	LLTextBox* mCycleTimeTextLabel;
	LLSpinCtrl* mCycleTimeSpinner;
	
	LLButton* mReloadButton;
	
	LLButton* mPreviousButton;
	LLButton* mNextButton;
	
	bool mCanDragAndDrop;
	bool mImportRunning;
	
	boost::signals2::connection mReloadCallback;
	boost::signals2::connection mAnimationChangedCallback;
};

#endif // LL_FLOATERAO_H
