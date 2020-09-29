/**
* @file alpanelmusicticker.cpp
* @brief ALPanelMusicTicker implementation
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

#include "llviewerprecompiledheaders.h"

#include "alpanelmusicticker.h"

// Library includes
#include "llaudioengine.h"
#include "lliconctrl.h"
#include "llstreamingaudio.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrl.h"

// Viewer includes
#include "llviewercontrol.h"

static LLPanelInjector<ALPanelMusicTicker> t_music_ticker("music_ticker");

ALPanelMusicTicker::ALPanelMusicTicker() : LLPanel(),
	mPlayState(STATE_PLAYING), 
	mArtistScrollChars(0), 
	mTitleScrollChars(0), 
	mCurScrollChar(0),
	mArtistText(nullptr),
	mTitleText(nullptr),
	mVisualizer(nullptr)
{
}

BOOL ALPanelMusicTicker::postBuild()
{
	mArtistText =	getChild<LLTextBox>("artist_text");
	mTitleText	=	getChild<LLTextBox>("title_text");
	mVisualizer =	getChild<LLUICtrl>("visualizer_box");
	mszLoading	=	getString("loading");
	mszPaused	=	getString("paused");
	mOscillatorColor = LLUIColorTable::getInstance()->getColor("ALMediaTickerOscillatorColor");
	
	setPaused(true);

	return LLPanel::postBuild();
}

void ALPanelMusicTicker::draw()
{
	updateTickerText();
	drawOscilloscope();
	LLPanel::draw();
}

void ALPanelMusicTicker::reshape(S32 width, S32 height, BOOL called_from_parent/*=TRUE*/)
{
	bool width_changed = (getRect().getWidth() != width);
	LLPanel::reshape(width, height, called_from_parent);
	if(width_changed)
	{
		if(mTitleText)
			mTitleScrollChars = countExtraChars(mTitleText, mszTitle);
		if(mArtistText)
			mArtistScrollChars = countExtraChars(mArtistText, mszArtist);
		resetTicker();
	}
}

void ALPanelMusicTicker::updateTickerText() //called via draw.
{
	if(!gAudiop)
		return;

	bool stream_paused = gAudiop->getStreamingAudioImpl()->isPlaying() != 1;	//will return 1 if playing.

	bool dirty = setPaused(stream_paused);
	if(!stream_paused)
	{
		if (dirty || gAudiop->getStreamingAudioImpl()->hasNewMetaData())
		{
			const LLSD* metadata = gAudiop->getStreamingAudioImpl()->getMetaData();
			LLSD artist = metadata ? metadata->get("ARTIST") : LLSD();
			LLSD title = metadata ? metadata->get("TITLE") : LLSD();
	
			dirty |= setArtist(artist.isDefined() ? artist.asString() : mszLoading);
			dirty |= setTitle(title.isDefined() ? title.asString() : mszLoading);
			if(artist.isDefined() && title.isDefined())
				mLoadTimer.stop();
			else if(dirty)
				mLoadTimer.start();
			else if(mLoadTimer.getStarted() && mLoadTimer.getElapsedTimeF64() > 10.f) //It has been 10 seconds.. give up.
			{
				if(!artist.isDefined())
					dirty |= setArtist(LLStringUtil::null);
				if(!title.isDefined())
					dirty |= setTitle(LLStringUtil::null);
				mLoadTimer.stop();
			}
		}
	}
	if(dirty)
		resetTicker();
	else 
		iterateTickerOffset();
}

void ALPanelMusicTicker::drawOscilloscope() //called via draw.
{
	if(!gAudiop || !mVisualizer || !gAudiop->getStreamingAudioImpl()->supportsWaveData())
		return;

	static const S32 NUM_LINE_STRIPS = 64;			//How many lines to draw. 64 is more than enough.
	static const S32 WAVE_DATA_STEP_SIZE = 4;		//Increase to provide more history at expense of cpu/memory.

	static const S32 NUM_WAVE_DATA_VALUES = NUM_LINE_STRIPS * WAVE_DATA_STEP_SIZE;	//Actual buffer size. Don't toy with this. Change above vars to tweak.
	static F32 buf[NUM_WAVE_DATA_VALUES];

	const LLRect& root_rect = mVisualizer->getRect();

	F32 height = root_rect.getHeight();
	F32 height_scale = height / 2.f;	//WaveData ranges from 1 to -1, so height_scale = height / 2
	F32 width = root_rect.getWidth();
	F32 width_scale = width / (F32)NUM_WAVE_DATA_VALUES;

	gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
	gGL.color4fv(mOscillatorColor.mV);
	gGL.pushMatrix();
		const auto& ui_scale = gGL.getUIScale();
		F32 x = (F32) root_rect.mLeft * ui_scale[VX];
		F32 y = (F32) (root_rect.mBottom + height * 0.5f) * ui_scale[VY];
		gGL.translatef(x, y, 0.f);
		gGL.begin( LLRender::LINE_STRIP );
			if(mPlayState == STATE_PAUSED
			   || !gAudiop->getStreamingAudioImpl()->getWaveData(&buf[0], NUM_WAVE_DATA_VALUES,WAVE_DATA_STEP_SIZE))
			{
				gGL.vertex2i(0, 0);
				gGL.vertex2i((S32)width, 0);
			}
			else
				for(S32 i = NUM_WAVE_DATA_VALUES - 1; i >= 0; i -= WAVE_DATA_STEP_SIZE)
					gGL.vertex2f((F32)i * width_scale, buf[i] * height_scale);
		gGL.end();
	gGL.popMatrix();
	gGL.flush();
}

bool ALPanelMusicTicker::setPaused(bool pause)
{
	if(pause == (mPlayState == STATE_PAUSED))
		return false;
	mPlayState = pause ? STATE_PAUSED : STATE_PLAYING;
	if(pause)
	{
		setArtist(mszPaused);
		setTitle(mszPaused);
	}
	return true;
}

void ALPanelMusicTicker::resetTicker()
{
	mScrollTimer.reset();
	mCurScrollChar = 0;
	if(mArtistText)
		mArtistText->setText(LLStringExplicit(mszArtist.substr(0, mszArtist.length() - mArtistScrollChars)));
	if(mTitleText)
		mTitleText->setText(LLStringExplicit(mszTitle.substr(0, mszTitle.length() - mTitleScrollChars)));
}

bool ALPanelMusicTicker::setArtist(const std::string &artist)
{
	if(!mArtistText || mszArtist == artist)
		return false;
	mszArtist = artist;
	mArtistText->setText(mszArtist);
	mArtistScrollChars = countExtraChars(mArtistText, mszArtist);
	return true;
}

bool ALPanelMusicTicker::setTitle(const std::string &title)
{
	if(!mTitleText || mszTitle == title)
		return false;
	mszTitle = title;
	mTitleText->setText(mszTitle);
	mTitleScrollChars = countExtraChars(mTitleText, mszTitle);
	return true;
}

S32 ALPanelMusicTicker::countExtraChars(LLTextBox *texbox, const std::string &text)
{
	S32 text_width = texbox->getTextPixelWidth();
	S32 box_width = texbox->getRect().getWidth();
	if(text_width > box_width)
	{
		const LLFontGL* font = texbox->getFont();
		for(S32 count = 1; count < (S32)text.length(); count++)
		{
			//This isn't very efficient...
			const std::string substr = text.substr(0, text.length() - count);
			if (font->getWidth(substr) <= box_width)
				return count;
		}
	}
	return 0;
}

void ALPanelMusicTicker::iterateTickerOffset()
{
	if((mPlayState != STATE_PAUSED)
	   && (mArtistScrollChars || mTitleScrollChars)
	   && ((!mCurScrollChar && mScrollTimer.getElapsedTimeF32() >= 5.f)
		   || (mCurScrollChar && mScrollTimer.getElapsedTimeF32() >= .5f)))
	{
		if(++mCurScrollChar > llmax(mArtistScrollChars, mTitleScrollChars))
		{
			if(mScrollTimer.getElapsedTimeF32() >= 2.f)	//pause for a bit when it reaches beyond last character.
				resetTicker();	
		}
		else
		{
			mScrollTimer.reset();
			if(mArtistText && mCurScrollChar <= mArtistScrollChars)
			{
				mArtistText->setText(LLStringExplicit(mszArtist.substr(mCurScrollChar, mszArtist.length()-mArtistScrollChars + mCurScrollChar)));
			}
			if(mTitleText && mCurScrollChar <= mTitleScrollChars)
			{
				mTitleText->setText(LLStringExplicit(mszTitle.substr(mCurScrollChar, mszTitle.length()-mTitleScrollChars + mCurScrollChar)));
			}
		}
	}
}
