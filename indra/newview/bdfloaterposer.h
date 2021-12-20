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


#ifndef BD_FLOATER_POSER_H
#define BD_FLOATER_POSER_H

#include "llfloater.h"
#include "llscrolllistctrl.h"
#include "llsliderctrl.h"
//#include "llmultisliderctrl.h"
//#include "lltimectrl.h"
#include "lltabcontainer.h"
#include "llkeyframemotion.h"
#include "lltoggleablemenu.h"
#include "llmenubutton.h"

/*struct BDPoseKey
{
public:
	// source of a pose set
	std::string name;

	// for conversion from LLSD
	static const int NAME_IDX = 0;
	static const int SCOPE_IDX = 1;

	inline BDPoseKey(const std::string& n)
		: name(n)
	{
	}

	inline BDPoseKey(LLSD llsd)
		: name(llsd[NAME_IDX].asString())
	{
	}

	inline BDPoseKey() // NOT really valid, just so std::maps can return a default of some sort
		: name("")
	{
	}

	inline BDPoseKey(std::string& stringVal)
	{
		size_t len = stringVal.length();
		if (len > 0)
		{
			name = stringVal.substr(0, len - 1);
		}
	}

	inline std::string toStringVal() const
	{
		std::stringstream str;
		str << name;
		return str.str();
	}

	inline LLSD toLLSD() const
	{
		LLSD llsd = LLSD::emptyArray();
		llsd.append(LLSD(name));
		return llsd;
	}

	inline void fromLLSD(const LLSD& llsd)
	{
		name = llsd[NAME_IDX].asString();
	}

	inline bool operator <(const BDPoseKey other) const
	{
		if (name < other.name)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	inline bool operator ==(const BDPoseKey other) const
	{
		return (name == other.name);
	}

	std::string toString() const;
};*/


typedef enum E_BoneTypes
{
	JOINTS = 0,
	COLLISION_VOLUMES = 1,
	ATTACHMENT_BONES = 2
} E_BoneTypes;

typedef enum E_Columns
{
	COL_ICON = 0,
	COL_NAME = 1,
	COL_ROT_X = 2,
	COL_ROT_Y = 3,
	COL_ROT_Z = 4,
	COL_POS_X = 5,
	COL_POS_Y = 6,
	COL_POS_Z = 7,
	COL_SCALE_X = 8,
	COL_SCALE_Y = 9,
	COL_SCALE_Z = 10
} E_Columns;

class BDFloaterPoser :
	public LLFloater
{
	friend class LLFloaterReg;
private:
	BDFloaterPoser(const LLSD& key);
	/*virtual*/	~BDFloaterPoser();
	/*virtual*/	BOOL postBuild();
	/*virtual*/ void draw();
	/*virtual*/ void onOpen(const LLSD& key);
	/*virtual*/	void onClose(bool app_quitting);

	//BD - Posing
	void onClickPoseSave();
	void onPoseStart();
	void onPoseDelete();
	void onPoseRefresh();
	void onPoseSet(LLUICtrl* ctrl, const LLSD& param);
	void onPoseControlsRefresh();
	void onPoseSave(S32 type, F32 time, bool editing);
	void onPoseLoad();
	void onPoseLoadSelective(const LLSD& param);
	void onPoseMenuAction(const LLSD& param);
	void onPoseScrollRightMouse(LLUICtrl* ctrl, S32 x, S32 y);

	//BD - Joints
	void onJointRefresh();
	void onJointSet(LLUICtrl* ctrl, const LLSD& param);
	void onJointPosSet(LLUICtrl* ctrl, const LLSD& param);
	void onJointScaleSet(LLUICtrl* ctrl, const LLSD& param);
	void onJointChangeState();
	void onJointControlsRefresh();
	void onJointRotPosScaleReset();
	void onJointRotationReset();
	void onJointPositionReset();
	void onJointScaleReset();
	void onCollectDefaults();

	//BD - Animating
	void onAnimAdd(const LLSD& param);
	void onAnimListWrite();
	void onAnimMove(const LLSD& param);
	void onAnimDelete();
	void onAnimSave();
	void onAnimSet();
	void onAnimPlay();
	void onAnimStop();
	void onAnimControlsRefresh();

	//BD - Misc
	void onUpdateLayout();

	//BD - Mirror Bone
	void toggleMirrorMode(LLUICtrl* ctrl) { mMirrorMode = ctrl->getValue().asBoolean(); }
	void toggleEasyRotations(LLUICtrl* ctrl) { mEasyRotations = ctrl->getValue().asBoolean(); }

	//BD - Flip Poses
	void onFlipPose();

	//BD - Animesh
	void onAvatarsRefresh();
	void onAvatarsSelect();

	//BD
	void loadPoseRotations(std::string name, LLVector3 *rotations);
	void loadPosePositions(std::string name, LLVector3 *rotations);
	void loadPoseScales(std::string name, LLVector3 *rotations);

private:
	//BD - Posing
	LLScrollListCtrl*						mPoseScroll;
	LLTabContainer*							mJointTabs;
	LLHandle<LLToggleableMenu>				mPosesMenuHandle;

	std::array<LLUICtrl*, 3>				mRotationSliders;
	std::array<LLSliderCtrl*, 3>			mPositionSliders;
	std::array<LLSliderCtrl*, 3>			mScaleSliders;
	std::array<LLScrollListCtrl*, 3>		mJointScrolls;

	//BD - I really didn't want to do this this way but we have to.
	//     It's the easiest way doing this.
	std::map<const std::string, LLVector3>	mDefaultScales;
	std::map<const std::string, LLVector3>	mDefaultPositions;

	//BD - Animations
	LLScrollListCtrl*						mAnimEditorScroll;

	//BD - Misc
	bool									mDelayRefresh;
	bool									mEasyRotations;
	
	//BD - Mirror Bone
	bool									mMirrorMode;

	//BD - Animesh
	LLScrollListCtrl*						mAvatarScroll;

	LLButton*								mStartPosingBtn;
	LLMenuButton*							mLoadPosesBtn;

	//BD - Experimental
	/*void onAnimEdit(LLUICtrl* ctrl, const LLSD& param);
	void onAddKey();
	void onDeleteKey();
	void addSliderKey(F32 time, BDPoseKey keyframe);
	void onTimeSliderMoved();
	void onKeyTimeMoved();
	void onKeyTimeChanged();
	void onAnimSetValue(LLUICtrl* ctrl, const LLSD& param);

	/// convenience class for holding keyframes mapped to sliders
	struct SliderKey
	{
	public:
		SliderKey(BDPoseKey kf, F32 t) : keyframe(kf), time(t) {}
		SliderKey() : keyframe(), time(0.f) {} // Don't use this default constructor

		BDPoseKey keyframe;
		F32 time;
	};

	LLSD 									mAnimJointMap[134][200][2]; // 134 bones, 200 keyframes , 2 stats (rotation | time)
	std::map<std::string, SliderKey>		mSliderToKey;
	LLMultiSliderCtrl*						mTimeSlider;
	LLMultiSliderCtrl*						mKeySlider; */
};

#endif
