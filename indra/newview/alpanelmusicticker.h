/**
* @file alpanelmusicticker.h
* @brief ALPanelMusicTicker declaration
*
* $LicenseInfo:firstyear=2015&license=viewerlgpl$
* Copyright (C) Shyotl Kuhr
* Copyright (C) 2015 Drake Arconis
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
**/

#ifndef AL_PANELMUSICTICKER_H
#define AL_PANELMUSICTICKER_H

#include "llpanel.h"

class LLIconCtrl;
class LLTextBox;

class ALPanelMusicTicker final : public LLPanel
{
public:
	ALPanelMusicTicker();	//ctor

	BOOL postBuild() final override;
	void draw() final override;
	void reshape(S32 width, S32 height, BOOL called_from_parent = TRUE) final override;
private:
	void updateTickerText(); //called via draw.
	void drawOscilloscope(); //called via draw.
	bool setPaused(bool pause); //returns true on state change.
	void resetTicker(); //Resets tickers to their innitial values (no offset).
	bool setArtist(const std::string &artist);	//returns true on change
	bool setTitle(const std::string &title);	//returns true on change
	S32 countExtraChars(LLTextBox *texbox, const std::string &text);	//calculates how many characters are truncated by bounds.
	void iterateTickerOffset();	//Logic that actually shuffles the text to the left.

	enum ePlayState
	{
		STATE_PAUSED,
		STATE_PLAYING
	};

	ePlayState mPlayState;
	std::string mszLoading;
	std::string mszPaused;
	std::string mszArtist;
	std::string mszTitle;
	LLTimer mScrollTimer;
	LLTimer mLoadTimer;
	S32 mArtistScrollChars;
	S32 mTitleScrollChars;
	S32 mCurScrollChar;

	LLColor4 mOscillatorColor;

	//UI elements
	LLTextBox* mArtistText;
	LLTextBox* mTitleText;
	LLUICtrl* mVisualizer;
};

#endif // AL_PANELMUSICTICKER_H
