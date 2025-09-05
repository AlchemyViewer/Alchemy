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
#include "lltrans.h"
#include "llurlaction.h"

// Viewer includes
#include "llviewercontrol.h"

static LLPanelInjector<ALPanelMusicTicker> t_music_ticker("music_ticker");

ALPanelMusicTicker::ALPanelMusicTicker() : LLPanel(),
    mPlayState(STATE_PLAYING),
    mStationScrollChars(0),
    mArtistScrollChars(0),
    mTitleScrollChars(0),
    mCurScrollChar(0)
{
}

ALPanelMusicTicker::~ALPanelMusicTicker()
{
    if (mMetadataUpdateConnection.connected())
    {
        mMetadataUpdateConnection.disconnect();
    }
}

bool ALPanelMusicTicker::postBuild()
{
    mStationText = getChild<LLTextBox>("station_text");
    mArtistText =   getChild<LLTextBox>("artist_text");
    mTitleText  =   getChild<LLTextBox>("title_text");
    mVisualizer =   getChild<LLUICtrl>("visualizer_box");
    mszLoading  =   getString("loading");
    mszPaused   =   getString("paused");
    mOscillatorColor = LLUIColorTable::getInstance()->getColor("ALMediaTickerOscillatorColor");

    if (gAudiop && gAudiop->getStreamingAudioImpl() && gAudiop->getStreamingAudioImpl()->supportsMetaData())
    {
        mMetadataUpdateConnection = gAudiop->getStreamingAudioImpl()->setMetadataUpdatedCallback([this](const LLSD& metadata) { metadataUpdateCallback(metadata); });

        metadataUpdateCallback(gAudiop->getStreamingAudioImpl()->getMetadata());
    }
    else
    {
        metadataUpdateCallback(LLSD());
    }

    return LLPanel::postBuild();
}

void ALPanelMusicTicker::draw()
{
    updateTickerText();
    drawOscilloscope();
    LLPanel::draw();
}

void ALPanelMusicTicker::reshape(S32 width, S32 height, bool called_from_parent/*=true*/)
{
    bool width_changed = (getRect().getWidth() != width);
    LLPanel::reshape(width, height, called_from_parent);
    if(width_changed)
    {
        if (mStationText)
            mStationScrollChars = countExtraChars(mStationText, mszStation);
        if(mTitleText)
            mTitleScrollChars = countExtraChars(mTitleText, mszTitle);
        if(mArtistText)
            mArtistScrollChars = countExtraChars(mArtistText, mszArtist);
        resetTicker();
    }
}

void ALPanelMusicTicker::updateTickerText() //called via draw.
{
    iterateTickerOffset();
}

void ALPanelMusicTicker::metadataUpdateCallback(const LLSD& metadata)
{
    bool stream_paused = true;

    if (gAudiop && gAudiop->getStreamingAudioImpl())
    {
        stream_paused = gAudiop->getStreamingAudioImpl()->isPlaying() != 1; //will return 1 if playing.
    }

    bool dirty = setPaused(stream_paused);
    if (!stream_paused)
    {
            LLSD artist = metadata["ARTIST"];
            LLSD title = metadata["TITLE"];

            std::string station = metadata.has("TRSN") ? metadata["TRSN"].asString() : metadata.has("icy-name") ? metadata["icy-name"].asString() : LLTrans::getString("NowPlaying");
            std::string station_url = metadata.has("URL") ? metadata["URL"].asString() : metadata.has("icy-url") ? metadata["icy-url"].asString() : std::string();
            dirty |= setStation(station, station_url);
            dirty |= setArtist(artist.isDefined() ? artist.asString() : LLStringUtil::null);
            dirty |= setTitle(title.isDefined() ? title.asString() : LLStringUtil::null);
    }
    if (dirty)
        resetTicker();
}

void ALPanelMusicTicker::drawOscilloscope() //called via draw.
{
    if(!gAudiop || !mVisualizer || !gAudiop->getStreamingAudioImpl() || !gAudiop->getStreamingAudioImpl()->supportsWaveData())
        return;

    static const S32 NUM_LINE_STRIPS = 64;          //How many lines to draw. 64 is more than enough.
    static const S32 WAVE_DATA_STEP_SIZE = 4;       //Increase to provide more history at expense of cpu/memory.

    static const S32 NUM_WAVE_DATA_VALUES = NUM_LINE_STRIPS * WAVE_DATA_STEP_SIZE;  //Actual buffer size. Don't toy with this. Change above vars to tweak.
    static F32 buf[NUM_WAVE_DATA_VALUES];

    const LLRect& root_rect = mVisualizer->getRect();

    F32 height = (F32)root_rect.getHeight();
    F32 height_scale = height / 2.f;    //WaveData ranges from 1 to -1, so height_scale = height / 2
    F32 width = (F32)root_rect.getWidth();
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
        setStation(mszPaused, LLStringUtil::null);
        setArtist(LLStringUtil::null);
        setTitle(LLStringUtil::null);
    }
    return true;
}

void ALPanelMusicTicker::resetTicker()
{
    mScrollTimer.reset();
    mCurScrollChar = 0;
    if (mStationText)
        mStationText->setText(LLStringExplicit(mszStation.substr(0, mszStation.length() - mStationScrollChars)));
    if(mArtistText)
        mArtistText->setText(LLStringExplicit(mszArtist.substr(0, mszArtist.length() - mArtistScrollChars)));
    if(mTitleText)
        mTitleText->setText(LLStringExplicit(mszTitle.substr(0, mszTitle.length() - mTitleScrollChars)));
}

bool ALPanelMusicTicker::setStation(const std::string& station, const std::string& url)
{
    if (!mStationText || (mszStation == station && mszStationURL == url))
        return false;
    mszStation = station;
    mszStationURL = url;
    if (mszStationURL.empty())
    {
        mStationText->clearClickedCallback();
    }
    else
    {
        mStationText->setClickedCallback([this](void*) { if (!mszStationURL.empty()) LLUrlAction::openURL(mszStationURL); });
    }
    mStationText->setText(mszStation);
    mStationScrollChars = countExtraChars(mStationText, mszStation);
    return true;
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
       && (mStationScrollChars || mArtistScrollChars || mTitleScrollChars)
       && ((!mCurScrollChar && mScrollTimer.getElapsedTimeF32() >= 5.f)
           || (mCurScrollChar && mScrollTimer.getElapsedTimeF32() >= .5f)))
    {
        if(++mCurScrollChar > llmax(mStationScrollChars, llmax(mArtistScrollChars, mTitleScrollChars)))
        {
            if(mScrollTimer.getElapsedTimeF32() >= 2.f) //pause for a bit when it reaches beyond last character.
                resetTicker();
        }
        else
        {
            mScrollTimer.reset();
            if (mStationText && mCurScrollChar <= mStationScrollChars)
            {
                mStationText->setText(LLStringExplicit(mszStation.substr(mCurScrollChar, mszStation.length() - mStationScrollChars + mCurScrollChar)));
            }
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
