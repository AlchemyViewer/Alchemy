/**
*
* Copyright (C) 2018, NiranV Dean
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
*/


#ifndef BD_ANIMATOR_H
#define BD_ANIMATOR_H

#include "llfloater.h"
#include "llscrolllistctrl.h"
#include "llsliderctrl.h"
#include "llmultisliderctrl.h"
#include "lltimectrl.h"
#include "llkeyframemotion.h"

enum BD_EActionType
{
	WAIT = 0,
	REPEAT = 1,
	POSE = 2
};

enum BD_ELoadType
{
	NOTHING = 0,
	ROTATIONS = 0x1,			// Load rotations
	POSITIONS = 0x1 << 1,		// Load positions
	SCALES = 0x1 << 2,			// Load scales
};

class Action
{
public:
	std::string		mPoseName;
	BD_EActionType	mType = BD_EActionType::WAIT;
	F32				mTime = 1.f;
};

class BDAnimator
{
public:

	BDAnimator() = default;
	~BDAnimator() = default;

	void			onAddAction(LLVOAvatar* avatar, LLScrollListItem* item, S32 location);
	void			onAddAction(LLVOAvatar* avatar, std::string name, BD_EActionType type, F32 time, S32 location);
	void			onAddAction(LLVOAvatar* avatar, Action action, S32 location);
	void			onDeleteAction(LLVOAvatar* avatar, S32 i);

	BOOL			loadPose(const LLSD& name, S32 load_type = 3);
	LLSD			returnPose(const LLSD& name);

	void			update();
	void			startPlayback();
	void			stopPlayback();

	//BD - Animesh Support
	LLVOAvatar*						mTargetAvatar = nullptr;

	std::vector<LLVOAvatar*>		mAvatarsList;

	bool			getIsPlaying() { return mPlaying; }
	bool			mPlaying = false;
};

extern BDAnimator gDragonAnimator;

#endif
