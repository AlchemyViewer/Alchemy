/**
 * @file media_plugin_gstreamer10.cpp
 * @brief GStreamer-1.0 plugin for LLMedia API plugin system
 *
 * @cond
 * $LicenseInfo:firstyear=2016&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2016, Linden Research, Inc. / Nicky Dasmijn
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
 * @endcond
 */

// The Linux media plugin: video and audio on prims and in the media browser,
// and the parcel's audio stream. GStreamer is loaded at run time, so the
// plugin starts without it and says so.
//
// playbin plays whatever it is given; its video goes to an appsink that asks
// for BGRx at the size the viewer's texture is, so playbin's own converter
// scales and converts it, and a frame is copied into the texture upside down
// as the viewer wants it. A resize renegotiates. The appsink keeps only the
// newest frame, and a frame is taken without waiting: an audio stream has
// none, and the plugin must keep answering.

#include "linden_common.h"

#include "llgl.h"

#include "llapr.h"
#include "lltimer.h"
#include "llplugininstance.h"
#include "llpluginmessage.h"
#include "llpluginmessageclasses.h"
#include "media_plugin_base.h"

#include <cstring>

#define G_DISABLE_CAST_CHECKS
extern "C" {
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
}

SymbolGrabber gstSymbolGrabber;

#include "llmediaimplgstreamer_syms_raw.inc"

static inline void llgst_caps_unref( GstCaps * caps )
{
    llgst_mini_object_unref( GST_MINI_OBJECT_CAST( caps ) );
}

static inline void llgst_sample_unref( GstSample *aSample )
{
    llgst_mini_object_unref( GST_MINI_OBJECT_CAST( aSample ) );
}

static inline void llgst_tag_list_unref( GstTagList *aList )
{
    llgst_mini_object_unref( GST_MINI_OBJECT_CAST( aList ) );
}

//////////////////////////////////////////////////////////////////////////////
//
class MediaPluginGStreamer10 : public MediaPluginBase
{
public:
    MediaPluginGStreamer10(LLPluginInstance::sendMessageFunction host_send_func, void *host_user_data);
    ~MediaPluginGStreamer10();

    /* virtual */ void receiveMessage(const std::string &message_string);

    static bool startup();
    static bool closedown();

    gboolean processGSTEvents(GstBus *bus, GstMessage *message);

private:
    std::string getVersion();
    bool navigateTo( const std::string& url );
    bool seek( double time_sec );
    bool setVolume( double volume );

    bool pause();
    bool stop();
    bool play();
    bool setState(GstState state);
    bool getTimePos(double &sec_out);
    bool getDuration(double &sec_out);

    bool unload();
    bool load();

    void update();
    void copyFrame(GstSample* sample);
    void setVideoSize(int width, int height);
    void updateTitle(const GstTagList* tags);

    /*virtual*/ void setDirty(int left, int top, int right, int bottom);
    void sendTimeUpdate();

    static bool mDoneInit;

    guint mBusWatchID;

    // The URL playing, for the browser messages the media system tracks.
    std::string mURL;
    std::string mTitle;

    double mVolume;
    bool mIsLooping;
    // What the viewer last asked for. Buffering pauses and resumes playbin
    // under it, which is not the viewer's pause.
    GstState mTargetState;
    // A live source, which neither prerolls nor buffers to a level.
    bool mIsLive;
    bool mBuffering;
    // Ended and not looping: a play starts again from the beginning.
    bool mAtEnd;

    bool mSeekWanted;
    double mSeekDestination;

    double mCurTime;
    double mDuration;
    // When the time was last asked of playbin, and last sent.
    double mLastTimeQuery;
    double mLastTimeUpdate;

    // Very GStreamer-specific
    GMainLoop *mPump; // event pump for this media
    GstElement *mPlaybin;
    GstAppSink *mAppSink;
};

//static
bool MediaPluginGStreamer10::mDoneInit = false;

MediaPluginGStreamer10::MediaPluginGStreamer10( LLPluginInstance::sendMessageFunction host_send_func,
                                                void *host_user_data )
    : MediaPluginBase(host_send_func, host_user_data)
    , mBusWatchID ( 0 )
    , mVolume ( 1.0 )
    , mIsLooping ( false )
    , mTargetState ( GST_STATE_NULL )
    , mIsLive ( false )
    , mBuffering ( false )
    , mAtEnd ( false )
    , mSeekWanted ( false )
    , mSeekDestination ( 0.0 )
    , mCurTime ( 0.0 )
    , mDuration ( 0.0 )
    , mLastTimeQuery ( 0.0 )
    , mLastTimeUpdate ( 0.0 )
    , mPump ( nullptr )
    , mPlaybin ( nullptr )
    , mAppSink ( nullptr )
{
    mWidth = 0;
    mHeight = 0;
    mTextureWidth = 0;
    mTextureHeight = 0;
    mDepth = 4;
    mPixels = nullptr;
}

gboolean MediaPluginGStreamer10::processGSTEvents(GstBus *bus, GstMessage *message)
{
    if (!message)
        return TRUE; // shield against GStreamer bug

    switch (GST_MESSAGE_TYPE (message))
    {
        case GST_MESSAGE_BUFFERING:
        {
            // A network source fills its buffer before it plays, and again
            // when it runs dry. A live one plays as it comes.
            if (mIsLive || !llgst_message_parse_buffering)
                break;

            gint percent = 0;
            llgst_message_parse_buffering(message, &percent);
            if (percent < 100 && !mBuffering)
            {
                mBuffering = true;
                if (mTargetState == GST_STATE_PLAYING)
                    llgst_element_set_state(mPlaybin, GST_STATE_PAUSED);
            }
            else if (percent >= 100 && mBuffering)
            {
                mBuffering = false;
                if (mTargetState == GST_STATE_PLAYING)
                    llgst_element_set_state(mPlaybin, GST_STATE_PLAYING);
            }
            break;
        }
        case GST_MESSAGE_STATE_CHANGED:
        {
            // Every element in the pipeline says when it changes state; the
            // media's state is playbin's.
            if (GST_MESSAGE_SRC(message) != GST_OBJECT(mPlaybin))
                break;

            GstState old_state;
            GstState new_state;
            GstState pending_state;
            llgst_message_parse_state_changed(message,
                                              &old_state,
                                              &new_state,
                                              &pending_state);

            switch (new_state)
            {
                case GST_STATE_READY:
                    // Stopped, not ended, and not failed.
                    if (!mAtEnd && mStatus != STATUS_ERROR)
                        setStatus(STATUS_LOADED);
                    break;
                case GST_STATE_PAUSED:
                    // Paused at the end is done; paused to buffer is still
                    // loading or playing, to the viewer.
                    if (mAtEnd)
                        break;
                    if (mTargetState == GST_STATE_PAUSED)
                        setStatus(STATUS_PAUSED);
                    else if (mStatus != STATUS_PLAYING)
                        setStatus(STATUS_LOADING);
                    break;
                case GST_STATE_PLAYING:
                    setStatus(STATUS_PLAYING);
                    break;
                default:
                    break;
            }
            break;
        }
        case GST_MESSAGE_DURATION_CHANGED:
            mDuration = 0.0;
            getDuration(mDuration);
            sendTimeUpdate();
            break;
        case GST_MESSAGE_TAG:
        {
            GstTagList* tags = nullptr;
            llgst_message_parse_tag(message, &tags);
            if (tags)
            {
                updateTitle(tags);
                llgst_tag_list_unref(tags);
            }
            break;
        }
        case GST_MESSAGE_ERROR:
        {
            GError *err = nullptr;
            gchar *debug = nullptr;

            llgst_message_parse_error (message, &err, &debug);
            std::cerr << "GStreamer error: " << (err ? err->message : "(unknown)");
            if (debug)
                std::cerr << " (" << debug << ")";
            std::cerr << std::endl;
            if (err)
                llg_error_free (err);
            llg_free (debug);

            mTargetState = GST_STATE_READY;
            llgst_element_set_state(mPlaybin, GST_STATE_READY);
            setStatus(STATUS_ERROR);
            break;
        }
        case GST_MESSAGE_WARNING:
        {
            GError *err = nullptr;
            gchar *debug = nullptr;

            llgst_message_parse_warning (message, &err, &debug);
            if (err)
            {
                std::cerr << "GStreamer warning: " << err->message << std::endl;
                llg_error_free (err);
            }
            llg_free (debug);
            break;
        }
        case GST_MESSAGE_EOS:
            if (mIsLooping && seek(0.0))
            {
                // Played again from the start, in place.
                break;
            }
            mAtEnd = true;
            mTargetState = GST_STATE_PAUSED;
            llgst_element_set_state(mPlaybin, GST_STATE_PAUSED);
            if (getDuration(mDuration))
                mCurTime = mDuration;
            setStatus(STATUS_DONE);
            sendTimeUpdate();
            break;
        default:
            /* unhandled message */
            break;
    }

    /* we want to be notified again the next time there is a message
     * on the bus, so return true (false means we want to stop watching
     * for messages on the bus and our callback should not be called again)
     */
    return TRUE;
}

extern "C" {
    gboolean llmediaimplgstreamer_bus_callback (GstBus     *bus,
                                                GstMessage *message,
                                                gpointer    data)
    {
        MediaPluginGStreamer10 *impl = (MediaPluginGStreamer10*)data;
        return impl->processGSTEvents(bus, message);
    }
} // extern "C"

bool MediaPluginGStreamer10::navigateTo( const std::string& url )
{
    if (!mDoneInit)
        return false; // error

    if (nullptr == mPump || nullptr == mPlaybin)
    {
        setStatus(STATUS_ERROR);
        return false; // error
    }

    mURL = url;
    mTitle.clear();
    mSeekWanted = false;
    mAtEnd = false;
    mBuffering = false;
    mCurTime = 0.0;
    mDuration = 0.0;

    // The media system keeps its idea of the page from the browser messages,
    // which the movie plugins have always sent too (MAINT-6528).
    LLPluginMessage message_begin(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "navigate_begin");
    message_begin.setValue("uri", mURL);
    message_begin.setValueBoolean("history_back_available", false);
    message_begin.setValueBoolean("history_forward_available", false);
    sendMessage(message_begin);

    setStatus(STATUS_LOADING);

    llgst_element_set_state(mPlaybin, GST_STATE_READY);
    llg_object_set (G_OBJECT (mPlaybin), "uri", mURL.c_str(), nullptr);

    // navigateTo implicitly plays, too.
    play();

    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "location_changed");
    message.setValue("uri", mURL);
    sendMessage(message);

    LLPluginMessage message_complete(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "navigate_complete");
    message_complete.setValue("uri", mURL);
    message_complete.setValueS32("result_code", 200);
    message_complete.setValue("result_string", "OK");
    sendMessage(message_complete);

    return true;
}

class GstSampleUnref
{
    GstSample *mT;
public:
    GstSampleUnref( GstSample *aT )
        : mT( aT )
    { llassert_always( mT ); }

    ~GstSampleUnref( )
    { llgst_sample_unref( mT ); }
};

void MediaPluginGStreamer10::update()
{
    if (!mDoneInit || nullptr == mPump || nullptr == mPlaybin)
        return;

    // A seek waits for the media to be under way, or GStreamer may quietly
    // ignore it (with rtsp:// at least).
    if (mSeekWanted && GST_STATE(mPlaybin) >= GST_STATE_PAUSED)
    {
        seek(mSeekDestination);
        mSeekWanted = false;
    }

    while (llg_main_context_pending(llg_main_loop_get_context(mPump)))
    {
        llg_main_context_iteration(llg_main_loop_get_context(mPump), FALSE);
    }

    // The time, for the media controls, a few times a second while
    // playing. A frame carries it; without one it goes alone.
    const bool playing = GST_STATE(mPlaybin) == GST_STATE_PLAYING;
    const double now = LLTimer::getTotalSeconds();
    if (playing && now - mLastTimeQuery >= 0.1)
    {
        mLastTimeQuery = now;
        getTimePos(mCurTime);
        if (mDuration <= 0.0)
            getDuration(mDuration);
    }

    if (mAppSink && mPixels)
    {
        // The newest frame, if there is one; never a wait for one.
        GstSample *sample = llgst_app_sink_try_pull_sample(mAppSink, 0);
        if (sample)
        {
            GstSampleUnref unref(sample);
            copyFrame(sample);
        }
    }

    if (playing && now - mLastTimeUpdate >= 0.25)
    {
        sendTimeUpdate();
    }
}

void MediaPluginGStreamer10::copyFrame(GstSample* sample)
{
    GstCaps *caps = llgst_sample_get_caps(sample);
    GstBuffer *buffer = llgst_sample_get_buffer(sample);
    if (!caps || !buffer)
        return;

    gint width = 0, height = 0;
    GstStructure *structure = llgst_caps_get_structure(caps, 0);
    if (!llgst_structure_get_int(structure, "width", &width) ||
        !llgst_structure_get_int(structure, "height", &height) ||
        width <= 0 || height <= 0)
    {
        return;
    }

    GstMapInfo map;
    if (!llgst_buffer_map(buffer, &map, GST_MAP_READ))
        return;

    // A frame from before a resize is copied as far as it fits.
    const int rows = llmin(height, mHeight, mTextureHeight);
    const int cols = llmin(width, mWidth, mTextureWidth);
    const size_t stride = map.size / (size_t)height;
    if (rows > 0 && cols > 0 && stride >= (size_t)cols * mDepth)
    {
        const size_t row_bytes = (size_t)cols * mDepth;
        const size_t texture_stride = (size_t)mTextureWidth * mDepth;
        for (int row = 0; row < rows; ++row)
        {
            // OpenGL's rows run bottom up.
            memcpy(mPixels + (size_t)(rows - 1 - row) * texture_stride,
                   map.data + (size_t)row * stride,
                   row_bytes);
        }
        setDirty(0, 0, cols, rows);
    }

    llgst_buffer_unmap(buffer, &map);
}

// The size the viewer's texture is, which the appsink asks playbin for; a
// change renegotiates the stream already playing. No pixel aspect ratio is
// asked for, so the picture is stretched to fill the texture, as media on a
// face always has been, not letterboxed into it.
void MediaPluginGStreamer10::setVideoSize(int width, int height)
{
    if (!mAppSink)
        return;

    GstCaps* caps = nullptr;
    if (width > 0 && height > 0)
    {
        caps = llgst_caps_new_simple("video/x-raw",
                                     "format", G_TYPE_STRING, "BGRx",
                                     "width", G_TYPE_INT, width,
                                     "height", G_TYPE_INT, height,
                                     nullptr);
    }
    else
    {
        caps = llgst_caps_new_simple("video/x-raw",
                                     "format", G_TYPE_STRING, "BGRx",
                                     nullptr);
    }
    llgst_app_sink_set_caps(mAppSink, caps);
    llgst_caps_unref(caps);

    GstPad* pad = llgst_element_get_static_pad(GST_ELEMENT(mAppSink), "sink");
    if (pad)
    {
        llgst_pad_push_event(pad, llgst_event_new_reconfigure());
        llgst_object_unref(pad);
    }
}

// "Artist - Title" where the stream says both, as a radio station does of
// the song it is playing; the title alone otherwise.
void MediaPluginGStreamer10::updateTitle(const GstTagList* tags)
{
    gchar* title = nullptr;
    gchar* artist = nullptr;
    llgst_tag_list_get_string(tags, GST_TAG_TITLE, &title);
    llgst_tag_list_get_string(tags, GST_TAG_ARTIST, &artist);

    std::string name;
    if (artist && *artist && title && *title)
        name = std::string(artist) + " - " + title;
    else if (title && *title)
        name = title;
    llg_free(title);
    llg_free(artist);

    if (!name.empty() && name != mTitle)
    {
        mTitle = name;
        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "name_text");
        message.setValue("name", mTitle);
        sendMessage(message);
    }
}

// The time and the duration ride on every update, as the media controls
// read them from it.
void MediaPluginGStreamer10::setDirty(int left, int top, int right, int bottom)
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "updated");

    message.setValueS32("left", left);
    message.setValueS32("top", top);
    message.setValueS32("right", right);
    message.setValueS32("bottom", bottom);

    message.setValueReal("current_time", mCurTime);
    message.setValueReal("duration", mDuration);
    message.setValueReal("current_rate", 1.0);

    sendMessage(message);
    mLastTimeUpdate = LLTimer::getTotalSeconds();
}

void MediaPluginGStreamer10::sendTimeUpdate()
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "updated");

    message.setValueReal("current_time", mCurTime);
    message.setValueReal("duration", mDuration);
    message.setValueReal("current_rate", 1.0);

    sendMessage(message);
    mLastTimeUpdate = LLTimer::getTotalSeconds();
}

bool MediaPluginGStreamer10::setState(GstState state)
{
    if (!mDoneInit || !mPlaybin)
        return false;

    mTargetState = state;
    GstStateChangeReturn result = llgst_element_set_state(mPlaybin, state);
    if (result == GST_STATE_CHANGE_FAILURE)
    {
        setStatus(STATUS_ERROR);
        return false;
    }
    if (result == GST_STATE_CHANGE_NO_PREROLL)
    {
        mIsLive = true;
    }
    return true;
}

bool MediaPluginGStreamer10::pause()
{
    return setState(GST_STATE_PAUSED);
}

bool MediaPluginGStreamer10::stop()
{
    mAtEnd = false;
    mCurTime = 0.0;
    return setState(GST_STATE_READY);
}

bool MediaPluginGStreamer10::play()
{
    if (mAtEnd)
    {
        // From the beginning, as a player does after the end: at once, as
        // playing on from the end would only end again.
        mSeekWanted = false;
        seek(0.0);
        mAtEnd = false;
    }
    if (GST_STATE(mPlaybin) <= GST_STATE_READY)
    {
        mIsLive = false;
    }
    // Buffering holds it paused until the buffer is full.
    bool result = setState(mBuffering ? GST_STATE_PAUSED : GST_STATE_PLAYING);
    mTargetState = GST_STATE_PLAYING;
    return result;
}

bool MediaPluginGStreamer10::setVolume( double volume )
{
    mVolume = llclamp(volume, 0.0, 1.0);
    if (mDoneInit && mPlaybin)
    {
        llg_object_set(mPlaybin, "volume", (gdouble)mVolume, nullptr);
        return true;
    }

    return false;
}

bool MediaPluginGStreamer10::seek(double time_sec)
{
    bool success = false;
    if (mDoneInit && mPlaybin)
    {
        success = llgst_element_seek(mPlaybin, 1.0, GST_FORMAT_TIME,
                GstSeekFlags(GST_SEEK_FLAG_FLUSH |
                         GST_SEEK_FLAG_KEY_UNIT),
                GST_SEEK_TYPE_SET, gint64(time_sec*GST_SECOND),
                GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
        if (success)
        {
            mAtEnd = false;
            mCurTime = time_sec;
            sendTimeUpdate();
        }
    }
    return success;
}

bool MediaPluginGStreamer10::getTimePos(double &sec_out)
{
    if (!mDoneInit || !mPlaybin || !llgst_element_query_position)
        return false;

    // The position is undefined but while PLAYING or PAUSED.
    if (GST_STATE(mPlaybin) != GST_STATE_PLAYING && GST_STATE(mPlaybin) != GST_STATE_PAUSED)
        return false;

    gint64 pos = 0;
    if (!llgst_element_query_position(mPlaybin, GST_FORMAT_TIME, &pos) || !GST_CLOCK_TIME_IS_VALID(pos))
        return false;

    sec_out = double(pos) / double(GST_SECOND);
    return true;
}

// None for a live stream, which has no end.
bool MediaPluginGStreamer10::getDuration(double &sec_out)
{
    if (!mDoneInit || !mPlaybin || !llgst_element_query_duration)
        return false;

    gint64 duration = 0;
    if (!llgst_element_query_duration(mPlaybin, GST_FORMAT_TIME, &duration) ||
        !GST_CLOCK_TIME_IS_VALID(duration) || duration <= 0)
    {
        return false;
    }

    sec_out = double(duration) / double(GST_SECOND);
    return true;
}

bool MediaPluginGStreamer10::load()
{
    if (!mDoneInit)
        return false; // error

    setStatus(STATUS_LOADING);

    // Create a pumpable main-loop for this media
    mPump = llg_main_loop_new (nullptr, FALSE);
    if (!mPump)
    {
        setStatus(STATUS_ERROR);
        return false; // error
    }

    // instantiate a playbin element to do the hard work
    mPlaybin = llgst_element_factory_make ("playbin", "");
    if (!mPlaybin)
    {
        setStatus(STATUS_ERROR);
        return false; // error
    }

    // get playbin's bus
    GstBus *bus = llgst_pipeline_get_bus (GST_PIPELINE (mPlaybin));
    if (!bus)
    {
        setStatus(STATUS_ERROR);
        return false; // error
    }
    mBusWatchID = llgst_bus_add_watch (bus,
                       llmediaimplgstreamer_bus_callback,
                       this);
    llgst_object_unref (bus);

    mAppSink = (GstAppSink*)(llgst_element_factory_make ("appsink", ""));
    if (!mAppSink)
    {
        setStatus(STATUS_ERROR);
        return false;
    }
    // One frame waits at most, the newest; the sink keeps time with the
    // audio by dropping the rest.
    llg_object_set(mAppSink, "max-buffers", (guint)1, "drop", TRUE, nullptr);
    setVideoSize(mWidth, mHeight);

    llg_object_set(mPlaybin, "video-sink", mAppSink, nullptr);
    llg_object_set(mPlaybin, "volume", (gdouble)mVolume, nullptr);

    return true;
}

bool MediaPluginGStreamer10::unload ()
{
    if (!mDoneInit)
        return false; // error

    // stop getting callbacks for this bus
    if (mBusWatchID)
    {
        llg_source_remove(mBusWatchID);
        mBusWatchID = 0;
    }

    if (mPlaybin)
    {
        llgst_element_set_state (mPlaybin, GST_STATE_NULL);
        llgst_object_unref (GST_OBJECT (mPlaybin));
        mPlaybin = nullptr;
    }

    if (mPump)
    {
        llg_main_loop_quit(mPump);
        llg_main_loop_unref(mPump);
        mPump = nullptr;
    }

    // playbin owned it.
    mAppSink = nullptr;

    setStatus(STATUS_NONE);

    return true;
}

void LogFunction(GstDebugCategory *category, GstDebugLevel level, const gchar *file, const gchar *function, gint line, GObject *object, GstDebugMessage *message, gpointer user_data )
{
    std::cerr << file << ":" << line << "(" << function << "): " << llgst_debug_message_get( message ) << std::endl;
}

//static
bool MediaPluginGStreamer10::startup()
{
    // first - check if GStreamer is explicitly disabled
    if (nullptr != getenv("LL_DISABLE_GSTREAMER"))
        return false;

    // only do global GStreamer initialization once.
    if (!mDoneInit)
    {
        ll_init_apr();

        // Get symbols! By the names the runtime packages install.
        std::vector< std::string > vctDSONames;
        vctDSONames.push_back( "libgstreamer-1.0.so.0"  );
        vctDSONames.push_back( "libgstapp-1.0.so.0"  );
        vctDSONames.push_back( "libglib-2.0.so.0" );
        vctDSONames.push_back( "libgobject-2.0.so.0" );
        if( !gstSymbolGrabber.grabSymbols( vctDSONames ) )
        {
            std::cerr << "GStreamer 1.0, with its app library, could not be loaded; media will not play." << std::endl;
            return false;
        }

        if (llgst_segtrap_set_enabled)
        {
            llgst_segtrap_set_enabled(FALSE);
        }

        // Gstreamer tries a fork during init, waitpid-ing on it,
        // which conflicts with any installed SIGCHLD handler...
        struct sigaction tmpact, oldact;
        if (llgst_registry_fork_set_enabled ) {
            // if we can disable SIGCHLD-using forking behaviour,
            // do it.
            llgst_registry_fork_set_enabled(false);
        }
        else {
            // else temporarily install default SIGCHLD handler
            // while GStreamer initialises
            tmpact.sa_handler = SIG_DFL;
            sigemptyset( &tmpact.sa_mask );
            tmpact.sa_flags = SA_SIGINFO;
            sigaction(SIGCHLD, &tmpact, &oldact);
        }
        // Protect against GStreamer resetting the locale, yuck.
        static std::string saved_locale;
        saved_locale = setlocale(LC_ALL, nullptr);

        llgst_debug_set_default_threshold( GST_LEVEL_WARNING );
        llgst_debug_add_log_function( LogFunction, nullptr, nullptr );
        llgst_debug_set_active( false );

        // finally, try to initialize GStreamer!
        GError *err = nullptr;
        gboolean init_gst_success = llgst_init_check(nullptr, nullptr, &err);

        // restore old locale
        setlocale(LC_ALL, saved_locale.c_str() );

        // restore old SIGCHLD handler
        if (!llgst_registry_fork_set_enabled)
            sigaction(SIGCHLD, &oldact, nullptr);

        if (!init_gst_success) // fail
        {
            if (err)
            {
                std::cerr << "GStreamer failed to initialize: " << err->message << std::endl;
                llg_error_free(err);
            }
            return false;
        }

        mDoneInit = true;
    }

    return true;
}

//static
bool MediaPluginGStreamer10::closedown()
{
    if (!mDoneInit)
        return false; // error

    gstSymbolGrabber.ungrabSymbols();
    mDoneInit = false;

    return true;
}

MediaPluginGStreamer10::~MediaPluginGStreamer10()
{
    unload();
    closedown();
}

std::string MediaPluginGStreamer10::getVersion()
{
    std::string plugin_version = "GStreamer10 media plugin, GStreamer version ";
    if (mDoneInit &&
        llgst_version)
    {
        guint major, minor, micro, nano;
        llgst_version(&major, &minor, &micro, &nano);
        plugin_version += llformat("%u.%u.%u.%u (runtime), %u.%u.%u.%u (headers)", (unsigned int)major, (unsigned int)minor,
                                   (unsigned int)micro, (unsigned int)nano, (unsigned int)GST_VERSION_MAJOR, (unsigned int)GST_VERSION_MINOR,
                                   (unsigned int)GST_VERSION_MICRO, (unsigned int)GST_VERSION_NANO);
    }
    else
    {
        plugin_version += "(unknown)";
    }
    return plugin_version;
}

void MediaPluginGStreamer10::receiveMessage(const std::string &message_string)
{
    LLPluginMessage message_in;

    if(message_in.parse(message_string) >= 0)
    {
        std::string message_class = message_in.getClass();
        std::string message_name = message_in.getName();

        if(message_class == LLPLUGIN_MESSAGE_CLASS_BASE)
        {
            if(message_name == "init")
            {
                LLPluginMessage message("base", "init_response");
                LLSD versions = LLSD::emptyMap();
                versions[LLPLUGIN_MESSAGE_CLASS_BASE] = LLPLUGIN_MESSAGE_CLASS_BASE_VERSION;
                versions[LLPLUGIN_MESSAGE_CLASS_MEDIA] = LLPLUGIN_MESSAGE_CLASS_MEDIA_VERSION;
                versions[LLPLUGIN_MESSAGE_CLASS_MEDIA_TIME] = LLPLUGIN_MESSAGE_CLASS_MEDIA_TIME_VERSION;
                message.setValueLLSD("versions", versions);

                load();

                message.setValue("plugin_version", getVersion());
                sendMessage(message);
            }
            else if(message_name == "idle")
            {
                // no response is necessary here.
                update();
            }
            else if(message_name == "cleanup")
            {
                unload();
                closedown();
            }
            else if(message_name == "force_exit")
            {
                mDeleteMe = true;
            }
            else if(message_name == "shm_added")
            {
                SharedSegmentInfo info;
                info.mAddress = message_in.getValuePointer("address");
                info.mSize = (size_t)message_in.getValueS32("size");
                std::string name = message_in.getValue("name");

                mSharedSegments.insert(SharedSegmentMap::value_type(name, info));
            }
            else if(message_name == "shm_remove")
            {
                std::string name = message_in.getValue("name");

                SharedSegmentMap::iterator iter = mSharedSegments.find(name);
                if(iter != mSharedSegments.end())
                {
                    if(mPixels == iter->second.mAddress)
                    {
                        // This is the currently active pixel buffer.  Make sure we stop drawing to it.
                        mPixels = nullptr;
                        mTextureSegmentName.clear();
                    }
                    mSharedSegments.erase(iter);
                }

                // Send the response so it can be cleaned up.
                LLPluginMessage message("base", "shm_remove_response");
                message.setValue("name", name);
                sendMessage(message);
            }
        }
        else if(message_class == LLPLUGIN_MESSAGE_CLASS_MEDIA)
        {
            if(message_name == "init")
            {
                // BGRx, as the appsink asks for it, at whatever size the
                // viewer gives the texture.
                LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "texture_params");
                message.setValueS32("default_width", 1024);
                message.setValueS32("default_height", 1024);
                message.setValueS32("depth", mDepth);
                message.setValueU32("internalformat", GL_RGB8);
                message.setValueU32("format", GL_BGRA);
                message.setValueU32("type", GL_UNSIGNED_BYTE);
                message.setValueBoolean("coords_opengl", true); // true == use OpenGL-style coordinates, false == (0,0) is upper left.
                message.setValueBoolean("allow_downsample", true); // we respond with grace and performance if asked to downscale
                sendMessage(message);
            }
            else if(message_name == "size_change")
            {
                std::string name = message_in.getValue("name");
                S32 width = message_in.getValueS32("width");
                S32 height = message_in.getValueS32("height");
                S32 texture_width = message_in.getValueS32("texture_width");
                S32 texture_height = message_in.getValueS32("texture_height");

                if(!name.empty())
                {
                    // Find the shared memory region with this name
                    SharedSegmentMap::iterator iter = mSharedSegments.find(name);
                    if(iter != mSharedSegments.end())
                    {
                        mPixels = (unsigned char*)iter->second.mAddress;
                        mTextureSegmentName = name;

                        mWidth = width;
                        mHeight = height;
                        mTextureWidth = texture_width;
                        mTextureHeight = texture_height;
                        memset( mPixels, 0, (size_t)mTextureWidth * mTextureHeight * mDepth );

                        setVideoSize(mWidth, mHeight);
                    }
                }

                LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "size_change_response");
                message.setValue("name", name);
                message.setValueS32("width", width);
                message.setValueS32("height", height);
                message.setValueS32("texture_width", texture_width);
                message.setValueS32("texture_height", texture_height);
                sendMessage(message);
            }
            else if(message_name == "load_uri")
            {
                std::string uri = message_in.getValue("uri");
                navigateTo( uri );
                sendStatus();
            }
        }
        else if(message_class == LLPLUGIN_MESSAGE_CLASS_MEDIA_TIME)
        {
            if(message_name == "stop")
            {
                stop();
            }
            else if(message_name == "start")
            {
                // NOTE: we don't actually support rate.
                play();
            }
            else if(message_name == "pause")
            {
                pause();
            }
            else if(message_name == "seek")
            {
                double time = message_in.getValueReal("time");
                // defer the actual seek in case we haven't
                // really truly started yet in which case there
                // is nothing to seek upon
                mSeekWanted = true;
                mSeekDestination = time;
            }
            else if(message_name == "set_loop")
            {
                bool loop = message_in.getValueBoolean("loop");
                mIsLooping = loop;
            }
            else if(message_name == "set_volume")
            {
                double volume = message_in.getValueReal("volume");
                setVolume(volume);
            }
        }
    }
}

int init_media_plugin(LLPluginInstance::sendMessageFunction host_send_func, void *host_user_data, LLPluginInstance::sendMessageFunction *plugin_send_func, void **plugin_user_data)
{
    if( MediaPluginGStreamer10::startup() )
    {
        MediaPluginGStreamer10 *self = new MediaPluginGStreamer10(host_send_func, host_user_data);
        *plugin_send_func = MediaPluginGStreamer10::staticReceiveMessage;
        *plugin_user_data = (void*)self;

        return 0; // okay
    }
    else
    {
        return -1; // failed to init
    }
}
