/**
* @file alpanelmusicticker.cpp
* @brief ALPanelMusicTicker implementation
*
* $LicenseInfo:firstyear=2015&license=viewerlgpl$
* Copyright (C) Shyotl Kuhr
* Copyright (C) Rye Mutt <rye@alchemyviewer.org>
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

namespace
{
    // The ticker cuts its strings at whatever offset it has scrolled to, so
    // every one of those offsets has to sit on a character boundary. Stream
    // metadata is largely accented Latin and CJK, and stepping a byte at a
    // time drew each multi-byte character as replacement marks on the way past.
    size_t cluster_offset(const std::string& text, S32 clusters)
    {
        size_t pos = 0;
        for (S32 i = 0; i < clusters && pos < text.size(); ++i)
        {
            pos = utf8str_step_grapheme_forward(text, pos);
        }
        return pos;
    }

    S32 cluster_count(const std::string& text)
    {
        S32 n = 0;
        for (size_t pos = 0; pos < text.size(); ++n)
        {
            pos = utf8str_step_grapheme_forward(text, pos);
        }
        return n;
    }

    // The slice on show once `text` has scrolled `step` characters, given that
    // it is `extra` characters wider than its box. The window keeps its width
    // and moves its start, so the whole string passes through it.
    std::string ticker_window(const std::string& text, S32 extra, S32 step)
    {
        const S32 visible = llmax(0, cluster_count(text) - extra);
        const size_t begin = cluster_offset(text, step);
        const size_t end   = cluster_offset(text, step + visible);
        return text.substr(begin, end - begin);
    }
}

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

    gGL.getTextureSlot(0)->unbind();
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
        mStationText->setText(LLStringExplicit(ticker_window(mszStation, mStationScrollChars, 0)));
    if(mArtistText)
        mArtistText->setText(LLStringExplicit(ticker_window(mszArtist, mArtistScrollChars, 0)));
    if(mTitleText)
        mTitleText->setText(LLStringExplicit(ticker_window(mszTitle, mTitleScrollChars, 0)));
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
    const S32 box_width = texbox->getRect().getWidth();
    if (texbox->getTextPixelWidth() <= box_width)
        return 0;

    // Trim whole characters off the end until what is left fits. The count is
    // what the ticker then scrolls through, one character per step.
    const LLFontGL* font = texbox->getFont();
    S32 extra = 0;
    for (size_t end = text.size(); end > 0; )
    {
        end = utf8str_step_grapheme_backward(text, end);
        ++extra;
        if (font->getWidthBytes(text, 0, (S32)end) <= box_width)
            break;
    }
    return extra;
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
                mStationText->setText(LLStringExplicit(ticker_window(mszStation, mStationScrollChars, mCurScrollChar)));
            }
            if(mArtistText && mCurScrollChar <= mArtistScrollChars)
            {
                mArtistText->setText(LLStringExplicit(ticker_window(mszArtist, mArtistScrollChars, mCurScrollChar)));
            }
            if(mTitleText && mCurScrollChar <= mTitleScrollChars)
            {
                mTitleText->setText(LLStringExplicit(ticker_window(mszTitle, mTitleScrollChars, mCurScrollChar)));
            }
        }
    }
}
