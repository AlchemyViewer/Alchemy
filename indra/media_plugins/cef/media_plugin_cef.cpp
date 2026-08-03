/**
* @file media_plugin_cef.cpp
* @brief CEF (Chromium Embedding Framework) plugin for LLMedia API plugin system
*
* @cond
* $LicenseInfo:firstyear=2008&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2010, Linden Research, Inc.
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

#include "linden_common.h"
#include "indra_constants.h" // for indra keyboard codes

#include "llglheaders.h" // for GL_* constants
#include "llsdutil.h"
#include "llplugininstance.h"
#include "llpluginmessage.h"
#include "llpluginmessageclasses.h"
#include "llstring.h"
#include "volume_catcher.h"
#include "media_plugin_base.h"

// _getpid()/getpid()
#if LL_WINDOWS
#include <process.h>
#else
#include <unistd.h>
#endif

#if LL_DARWIN
#include <IOSurface/IOSurface.h>   // accelerated paint hands the viewer an IOSurface
#include <mach/mach.h>             // ...shared cross-process via a mach send right
#include <servers/bootstrap.h>     // ...rendezvous'd through the bootstrap server
#endif

#if LL_LINUX
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>   // accelerated paint hands the viewer the dma-buf fds...
#include <sys/un.h>       // ...over an AF_UNIX datagram, as SCM_RIGHTS ancillary data
#endif

#include "dullahan.h"

#if LL_WINDOWS
#include <d3d11_1.h>
#include <dxgi1_2.h>

////////////////////////////////////////////////////////////////////////////////
// Producer half of zero-copy paint. Owns a D3D11 device and ONE persistent
// keyed-mutex shared texture. Each accelerated-paint frame it copies CEF's
// pooled (NT-handle) shared texture into the stable one under the mutex. The
// stable texture's NT handle is duplicated to the viewer only when it is
// (re)created (first frame / size change) - no per-frame DuplicateHandle. The
// viewer opens it once and samples under the same single-key (mutual-exclusion)
// mutex, which also provides the cross-process/cross-device GPU sync.
class CefAccelProducer
{
public:
    CefAccelProducer() = default;
    ~CefAccelProducer() { shutdown(); }

    bool init()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       nullptr, 0, D3D11_SDK_VERSION, &mDevice, nullptr, &mContext);
        if (FAILED(hr) || !mDevice)
        {
            return false;
        }
        mDevice->QueryInterface(__uuidof(ID3D11Device1), (void**)&mDevice1);
        return mDevice1 != nullptr;
    }

    void shutdown()
    {
        releaseStable();
        if (mDevice1) { mDevice1->Release(); mDevice1 = nullptr; }
        if (mContext) { mContext->Release(); mContext = nullptr; }
        if (mDevice)  { mDevice->Release();  mDevice = nullptr; }
    }

    // Copy this frame's CEF shared texture (cef_handle) into the stable texture.
    // If the stable texture was (re)created, out_handle receives a fresh NT handle
    // duplicated into viewer_process (to send once) and out_recreated is true.
    bool produce(void* cef_handle, void* viewer_process,
                 HANDLE& out_handle, bool& out_recreated, int& out_w, int& out_h, int& out_fmt)
    {
        out_recreated = false;
        out_handle = nullptr;
        if (!mDevice1 || !cef_handle)
        {
            return false;
        }

        ID3D11Texture2D* cef = nullptr;
        if (FAILED(mDevice1->OpenSharedResource1((HANDLE)cef_handle, __uuidof(ID3D11Texture2D), (void**)&cef)) || !cef)
        {
            return false;
        }

        D3D11_TEXTURE2D_DESC cd = {};
        cef->GetDesc(&cd);
        out_w = (int)cd.Width;
        out_h = (int)cd.Height;
        out_fmt = (int)cd.Format;

        if (!mStable || mW != cd.Width || mH != cd.Height || mFmt != cd.Format)
        {
            if (!createStable(cd, viewer_process, out_handle))
            {
                cef->Release();
                return false;
            }
            out_recreated = true;
        }

        // Only report success when the frame actually lands in the stable texture.
        // Returning true on a missed copy (no mutex / acquire timeout) would emit
        // an accelerated_paint message for a texture the consumer never received -
        // worst case a just-(re)created texture that was never populated.
        if (!mMutex)
        {
            cef->Release();
            return false;
        }
        if (FAILED(mMutex->AcquireSync(0, 1000)))
        {
            cef->Release();
            return false;
        }
        mContext->CopyResource(mStable, cef);
        mContext->Flush();
        mMutex->ReleaseSync(0);
        cef->Release();
        return true;
    }

private:
    bool createStable(const D3D11_TEXTURE2D_DESC& cd, void* viewer_process, HANDLE& out_handle)
    {
        releaseStable();

        D3D11_TEXTURE2D_DESC d = {};
        d.Width = cd.Width;
        d.Height = cd.Height;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = cd.Format;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        if (FAILED(mDevice->CreateTexture2D(&d, nullptr, &mStable)) || !mStable)
        {
            return false;
        }

        mStable->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&mMutex);

        IDXGIResource1* res = nullptr;
        if (FAILED(mStable->QueryInterface(__uuidof(IDXGIResource1), (void**)&res)) || !res)
        {
            return false;
        }
        HANDLE local = nullptr;
        HRESULT hr = res->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &local);
        res->Release();
        if (FAILED(hr) || !local)
        {
            return false;
        }

        // Share the stable texture to the viewer; we keep the texture alive so the
        // local share handle can be closed once duplicated.
        BOOL ok = DuplicateHandle(GetCurrentProcess(), local, (HANDLE)viewer_process,
                                  &out_handle, 0, FALSE, DUPLICATE_SAME_ACCESS);
        CloseHandle(local);
        if (!ok || !out_handle)
        {
            return false;
        }

        mW = cd.Width;
        mH = cd.Height;
        mFmt = cd.Format;
        return true;
    }

    void releaseStable()
    {
        if (mMutex)  { mMutex->Release();  mMutex = nullptr; }
        if (mStable) { mStable->Release(); mStable = nullptr; }
        mW = 0; mH = 0; mFmt = DXGI_FORMAT_UNKNOWN;
    }

    ID3D11Device*        mDevice = nullptr;
    ID3D11Device1*       mDevice1 = nullptr;
    ID3D11DeviceContext* mContext = nullptr;
    ID3D11Texture2D*     mStable = nullptr;
    IDXGIKeyedMutex*     mMutex = nullptr;
    UINT mW = 0, mH = 0;
    DXGI_FORMAT mFmt = DXGI_FORMAT_UNKNOWN;
};
#endif // LL_WINDOWS

////////////////////////////////////////////////////////////////////////////////
//
class MediaPluginCEF :
    public MediaPluginBase
{
public:
    MediaPluginCEF(LLPluginInstance::sendMessageFunction host_send_func, void *host_user_data);
    ~MediaPluginCEF();

    /*virtual*/
    void receiveMessage(const std::string& message_string);

private:
    bool init();

    void onPageChangedCallback(const unsigned char* pixels, int x, int y, const int width, const int height);
    void onAcceleratedPaintCallback(void* native_handle, int format, int width, int height);
#if LL_LINUX
    void onAcceleratedPaintDmabufCallback(const dullahan::dmabuf_plane* planes, int plane_count,
                                          int format, int width, int height, unsigned long long modifier);
#endif
    void onCustomSchemeURLCallback(std::string url, bool user_gesture, bool is_redirect);
    void onConsoleMessageCallback(std::string message, std::string source, int line);
    void onStatusMessageCallback(std::string value);
    void onTitleChangeCallback(std::string title);
    void onTooltipCallback(std::string text);
    void onLoadStartCallback();
    void onRequestExitCallback();
    void onLoadEndCallback(int httpStatusCode, std::string url);
    void onLoadError(int status, const std::string error_text, const std::string error_url);
    void onAddressChangeCallback(std::string url);
    void onOpenPopupCallback(std::string url, std::string target);
    bool onHTTPAuthCallback(const std::string host, const std::string realm, std::string& username, std::string& password);
    void onCursorChangedCallback(dullahan::ECursorType type);
    const std::vector<std::string> onFileDialog(dullahan::EFileDialogType dialog_type, const std::string dialog_title, const std::string default_file, const std::string dialog_accept_filter, bool& use_default);
    bool onJSDialogCallback(const std::string origin_url, const std::string message_text, const std::string default_prompt_text);
    bool onJSBeforeUnloadCallback();
    void onRequestContextMenuCallback(int x, int y, unsigned int edit_flags);

    void postDebugMessage(const std::string& msg);
    void authResponse(LLPluginMessage &message);

    void keyEvent(dullahan::EKeyEvent key_event, LLSD native_key_data);
    void unicodeInput(std::string text, LLSD native_key_data);

    void checkEditState();
    void setVolume();

    bool mEnableMediaPluginDebugging;
    std::string mHostLanguage;
    bool mCookiesEnabled;
    bool mPluginsEnabled;
    bool mJavascriptEnabled;
    bool mProxyEnabled;
    std::string mProxyHost;
    int mProxyPort;
    bool mDisableGPU;
    bool mUseMockKeyChain;
    bool mDisableWebSecurity;
    bool mFileAccessFromFileUrls;
    std::string mUserAgentSubtring;
    std::string mDisplayServer;     // viewer's windowing backend ("wayland"/"x11"); Linux Ozone platform
    std::string mAuthUsername;
    std::string mAuthPassword;
    bool mAuthOK;
    bool mCanUndo;
    bool mCanRedo;
    bool mCanCut;
    bool mCanCopy;
    bool mCanPaste;
    bool mCanDelete;
    bool mCanSelectAll;
    std::string mRootCachePath;
    std::string mCefLogFile;
    bool mCefLogVerbose;
    std::vector<std::string> mPickedFiles;
    VolumeCatcher mVolumeCatcher;
    F32 mCurVolume;
    dullahan* mCEFLib;

    // accelerated (zero-copy) paint: deliver GPU shared-texture handles to the
    // viewer instead of CPU pixels. mHostPid is the viewer process, mViewerProcess
    // its opened handle (with PROCESS_DUP_HANDLE) used to DuplicateHandle the
    // shared texture across the boundary.
    bool mUseAcceleratedPaint;
    int  mHostPid;
    int  mAccelId;        // per-media id echoed in each macOS surface mach message
    void* mViewerProcess;
#if LL_WINDOWS
    CefAccelProducer* mAccelProducer;
#endif
};

////////////////////////////////////////////////////////////////////////////////
//
MediaPluginCEF::MediaPluginCEF(LLPluginInstance::sendMessageFunction host_send_func, void *host_user_data) :
MediaPluginBase(host_send_func, host_user_data)
{
    mWidth = 0;
    mHeight = 0;
    mDepth = 4;
    mPixels = 0;
    mEnableMediaPluginDebugging = true;
    mHostLanguage = "en";
    mCookiesEnabled = true;
    mPluginsEnabled = false;
    mJavascriptEnabled = true;
    mProxyEnabled = false;
    mProxyHost = "";
    mProxyPort = 0;
    mDisableGPU = false;
    mUseAcceleratedPaint = false;
    mHostPid = 0;
    mAccelId = 0;
    mViewerProcess = nullptr;
#if LL_WINDOWS
    mAccelProducer = nullptr;
#endif
    mUseMockKeyChain = true;
    mDisableWebSecurity = false;
    mFileAccessFromFileUrls = false;
    mUserAgentSubtring = "";
    mAuthUsername = "";
    mAuthPassword = "";
    mAuthOK = false;
    mCanUndo = false;
    mCanRedo = false;
    mCanCut = false;
    mCanCopy = false;
    mCanPaste = false;
    mCanDelete = false;
    mCanSelectAll = false;
    mCefLogFile = "";
    mCefLogVerbose = false;
    mPickedFiles.clear();
    mCurVolume = 0.0;

    mCEFLib = new dullahan();

    setVolume();
}

////////////////////////////////////////////////////////////////////////////////
//
MediaPluginCEF::~MediaPluginCEF()
{
    mCEFLib->shutdown();
#if LL_WINDOWS
    if (mAccelProducer)
    {
        mAccelProducer->shutdown();
        delete mAccelProducer;
        mAccelProducer = nullptr;
    }
    if (mViewerProcess)
    {
        CloseHandle((HANDLE)mViewerProcess);
        mViewerProcess = nullptr;
    }
#endif
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::postDebugMessage(const std::string& msg)
{
    if (mEnableMediaPluginDebugging)
    {
        std::stringstream str;
        str << "@Media Msg> " << msg;

        LLPluginMessage debug_message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "debug_message");
        debug_message.setValue("message_text", str.str());
        debug_message.setValue("message_level", "info");
        sendMessage(debug_message);
    }
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onPageChangedCallback(const unsigned char* pixels, int x, int y, const int width, const int height)
{
    if( mPixels && pixels )
    {
        if (mWidth == width && mHeight == height)
        {
            memcpy(mPixels, pixels, mWidth * mHeight * mDepth);
        }
        else
        {
            mCEFLib->setSize(mWidth, mHeight);
        }
        setDirty(0, 0, mWidth, mHeight);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Zero-copy paint: CEF handed us a GPU shared-texture handle (valid in THIS
// process). Duplicate it into the viewer process and send the viewer the
#if LL_DARWIN
namespace
{
    // Sends a CEF accelerated-paint IOSurface to the viewer over a mach channel.
    // CEF's IOSurface is shared via mach ports (not a global id), so the only way
    // to hand it across the process boundary is a mach send right - which can't go
    // through the socket/LLSD channel. We rendezvous with the viewer's receive
    // port through the bootstrap server using a name derived from its pid, then
    // mach_msg one IOSurfaceCreateMachPort() right per frame, tagged with accel_id.
    //
    // Wire format MUST match LLCEFSurfaceReceiver (newview/llcefsurfacereceiver.cpp).
    typedef struct
    {
        mach_msg_header_t          header;
        mach_msg_body_t            body;
        mach_msg_port_descriptor_t surface;
        int32_t                    accel_id;
        int32_t                    width;
        int32_t                    height;
        int32_t                    format;
    } CefSurfaceSendMsg;

    class CefMacSurfaceSender
    {
    public:
        // Look up the viewer's receive port once; retried each frame until the
        // viewer has registered it (no ordering guarantee between the processes).
        bool connect(int host_pid)
        {
            if (mViewerPort != MACH_PORT_NULL) return true;
            if (host_pid <= 0) return false;

            char name[128];
            snprintf(name, sizeof(name), "org.alchemyviewer.cefsurface.%d", host_pid);
            kern_return_t kr = bootstrap_look_up(bootstrap_port, name, &mViewerPort);
            if (kr != KERN_SUCCESS)
            {
                mViewerPort = MACH_PORT_NULL;   // not up yet; try again next frame
                return false;
            }
            return true;
        }

        bool send(int accel_id, IOSurfaceRef surface, int width, int height, int format)
        {
            if (mViewerPort == MACH_PORT_NULL || !surface) return false;

            // A fresh send right to this frame's surface; the message copies it to
            // the viewer and we drop our reference afterwards.
            mach_port_t surf_port = IOSurfaceCreateMachPort(surface);
            if (surf_port == MACH_PORT_NULL) return false;

            CefSurfaceSendMsg msg = {};
            msg.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0) | MACH_MSGH_BITS_COMPLEX;
            msg.header.msgh_size = sizeof(msg);
            msg.header.msgh_remote_port = mViewerPort;
            msg.header.msgh_local_port = MACH_PORT_NULL;
            msg.body.msgh_descriptor_count = 1;
            msg.surface.name = surf_port;
            msg.surface.disposition = MACH_MSG_TYPE_COPY_SEND;
            msg.surface.type = MACH_MSG_PORT_DESCRIPTOR;
            msg.accel_id = accel_id;
            msg.width = width;
            msg.height = height;
            msg.format = format;

            kern_return_t kr = mach_msg(&msg.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT,
                                        sizeof(msg), 0, MACH_PORT_NULL, 100 /*ms*/, MACH_PORT_NULL);
            mach_port_deallocate(mach_task_self(), surf_port);

            if (kr != KERN_SUCCESS)
            {
                // The viewer likely went away / its port died; drop it and re-look-up.
                if (kr == MACH_SEND_INVALID_DEST)
                {
                    mach_port_deallocate(mach_task_self(), mViewerPort);
                    mViewerPort = MACH_PORT_NULL;
                }
                return false;
            }
            return true;
        }

    private:
        mach_port_t mViewerPort = MACH_PORT_NULL;
    };

    CefMacSurfaceSender& macSurfaceSender()
    {
        static CefMacSurfaceSender sSender;
        return sSender;
    }
}
#endif // LL_DARWIN

#if LL_LINUX
namespace
{
    // Wire header shared with the viewer (newview/llcefsurfacereceiver.cpp).
    // MUST stay in sync. The plane fds ride alongside as SCM_RIGHTS ancillary data.
    struct CefDmabufMsg
    {
        int32_t  accel_id;
        int32_t  plane_count;
        int32_t  width;
        int32_t  height;
        int32_t  format;
        uint32_t stride[4];
        uint64_t offset[4];
        uint64_t modifier;
    };

    // Hands CEF's accelerated-paint dma-buf to the viewer over an AF_UNIX datagram
    // socket. CEF's dma-buf fd cannot cross to the viewer through the LLSD/TCP
    // channel (text fd numbers re-opened via /proc fail with ENXIO for a dma-buf),
    // so we pass the fds themselves as SCM_RIGHTS ancillary data. We send to the
    // viewer's abstract socket named from its pid (learned via the init message's
    // host_pid), tagging each frame with accel_id so the viewer demuxes it back.
    class CefLinuxSurfaceSender
    {
    public:
        // CEF's fds are valid only during this callback, but sendmsg() captures a
        // kernel reference to each underlying dma-buf, so they survive until the
        // viewer recvmsg()s them - we do not dup or keep them alive ourselves.
        bool send(int host_pid, int accel_id, const dullahan::dmabuf_plane* planes, int plane_count,
                  int format, int width, int height, unsigned long long modifier)
        {
            if (host_pid <= 0 || !planes || plane_count <= 0) return false;
            if (plane_count > 4) plane_count = 4;

            if (mSock < 0)
            {
                mSock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
                if (mSock < 0) return false;
            }

            CefDmabufMsg hdr = {};
            hdr.accel_id = accel_id;
            hdr.plane_count = plane_count;
            hdr.width = width;
            hdr.height = height;
            hdr.format = format;
            hdr.modifier = modifier;
            int fds[4];
            for (int i = 0; i < plane_count; ++i)
            {
                fds[i] = planes[i].fd;
                hdr.stride[i] = planes[i].stride;
                hdr.offset[i] = planes[i].offset;
            }

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            int n = snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1,
                             "org.alchemyviewer.cefsurface.%d", host_pid);   // [0] NUL = abstract
            socklen_t addrlen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + n);

            struct iovec iov = { &hdr, sizeof(hdr) };
            char cbuf[CMSG_SPACE(sizeof(int) * 4)];
            memset(cbuf, 0, sizeof(cbuf));

            struct msghdr msg;
            memset(&msg, 0, sizeof(msg));
            msg.msg_name = &addr;
            msg.msg_namelen = addrlen;
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = cbuf;
            msg.msg_controllen = CMSG_SPACE(sizeof(int) * plane_count);

            struct cmsghdr* c = CMSG_FIRSTHDR(&msg);
            c->cmsg_level = SOL_SOCKET;
            c->cmsg_type = SCM_RIGHTS;
            c->cmsg_len = CMSG_LEN(sizeof(int) * plane_count);
            memcpy(CMSG_DATA(c), fds, sizeof(int) * plane_count);

            // Non-blocking + connectionless: a not-yet-listening viewer (ENOENT/
            // ECONNREFUSED) or a full queue (EAGAIN) just drops this frame; the
            // viewer keeps the previous frame and we send a fresh one next paint.
            ssize_t s = sendmsg(mSock, &msg, MSG_DONTWAIT | MSG_NOSIGNAL);
            return s == (ssize_t)sizeof(hdr);
        }

    private:
        int mSock = -1;
    };

    CefLinuxSurfaceSender& linuxSurfaceSender()
    {
        static CefLinuxSurfaceSender sSender;
        return sSender;
    }
}
#endif // LL_LINUX

// duplicated handle, so it can open the texture and bind it with no CPU copy.
// The handle is only valid for the duration of this callback, so duplicate now.
void MediaPluginCEF::onAcceleratedPaintCallback(void* native_handle, int format, int width, int height)
{
#if LL_WINDOWS
    if (!native_handle || !mHostPid)
    {
        return;
    }

    // Open the viewer process once (cached) so we can duplicate handles into it.
    if (!mViewerProcess)
    {
        mViewerProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, (DWORD)mHostPid);
        if (!mViewerProcess)
        {
            LL_WARNS("media") << "accelerated paint: OpenProcess(viewer pid " << mHostPid
                              << ") failed: " << GetLastError() << LL_ENDL;
            return;
        }
    }

    // Bring up the D3D producer once. CEF hands a different pooled texture each
    // frame; the producer copies it into ONE persistent keyed-mutex shared
    // texture so we only duplicate a handle to the viewer when that texture is
    // (re)created, not every frame.
    if (!mAccelProducer)
    {
        mAccelProducer = new CefAccelProducer();
        if (!mAccelProducer->init())
        {
            LL_WARNS("media") << "accelerated paint: D3D producer init failed" << LL_ENDL;
            delete mAccelProducer;
            mAccelProducer = nullptr;
            return;
        }
    }

    HANDLE stable_handle = nullptr;
    bool recreated = false;
    int w = 0, h = 0, fmt = 0;
    if (!mAccelProducer->produce(native_handle, mViewerProcess, stable_handle, recreated, w, h, fmt))
    {
        return;
    }

    // Tell the viewer a new frame is ready. The handle field is the stable
    // texture's viewer-side handle ONLY when it was just (re)created (sent once
    // per size); otherwise "0" means "same texture, new frame". Carried as a
    // decimal string so the full 64-bit value survives the LLSD message.
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "accelerated_paint");
    message.setValue("handle", recreated ? std::to_string((unsigned long long)(uintptr_t)stable_handle)
                                          : std::string("0"));
    message.setValueS32("format", fmt);
    message.setValueS32("width", w);
    message.setValueS32("height", h);
    sendMessage(message);
#elif LL_DARWIN
    // macOS: the handle is an IOSurfaceRef shared (by CEF) via mach ports, so a
    // global-id lookup in the viewer cannot work. Hand the surface over as a mach
    // send right on our side channel, then post the usual message as the viewer's
    // per-frame "dirty" trigger (the surface itself arrives out-of-band).
    if (!native_handle)
    {
        return;
    }
    CefMacSurfaceSender& sender = macSurfaceSender();
    if (!sender.connect(mHostPid))
    {
        return;   // viewer's receive port not registered yet; retry next frame
    }
    if (!sender.send(mAccelId, (IOSurfaceRef)native_handle, width, height, format))
    {
        return;
    }
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "accelerated_paint");
    message.setValue("handle", "0");   // unused on macOS (surface came via mach)
    message.setValueS32("format", format);
    message.setValueS32("width", width);
    message.setValueS32("height", height);
    sendMessage(message);
#else
    // Linux uses the dma-buf callback path instead (see onAcceleratedPaintDmabufCallback).
    (void)native_handle; (void)format; (void)width; (void)height;
#endif
}

#if LL_LINUX
////////////////////////////////////////////////////////////////////////////////
// Linux zero-copy paint: CEF hands us a dma-buf (one or more planes + a DRM
// modifier) valid only for this callback. Pass the plane fds to the viewer over
// the SCM_RIGHTS side channel - a dma-buf fd cannot be reopened across the
// process boundary via /proc (open() returns ENXIO), so the fds themselves must
// be handed over as ancillary data. Then post the usual LLSD "dirty" trigger; the
// fds arrived out-of-band, demuxed by accel_id, mirroring the macOS IOSurface path.
void MediaPluginCEF::onAcceleratedPaintDmabufCallback(const dullahan::dmabuf_plane* planes, int plane_count,
                                                      int format, int width, int height, unsigned long long modifier)
{
    if (!planes || plane_count <= 0)
    {
        return;
    }

    if (!linuxSurfaceSender().send(mHostPid, mAccelId, planes, plane_count, format, width, height, modifier))
    {
        return;   // viewer not listening yet / queue full - retried next paint
    }

    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "accelerated_paint");
    message.setValue("handle", "0");   // fds came via the side channel, not here
    message.setValueS32("format", format);
    message.setValueS32("width", width);
    message.setValueS32("height", height);
    sendMessage(message);
}
#endif

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onConsoleMessageCallback(std::string message, std::string source, int line)
{
    std::stringstream str;
    str << "Console message: " << message << " in file(" << source << ") at line " << line;
    postDebugMessage(str.str());
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onStatusMessageCallback(std::string value)
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "status_text");
    message.setValue("status", value);
    sendMessage(message);
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onTitleChangeCallback(std::string title)
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "name_text");
    message.setValue("name", title);
    message.setValueBoolean("history_back_available", mCEFLib->canGoBack());
    message.setValueBoolean("history_forward_available", mCEFLib->canGoForward());
    sendMessage(message);
}

void MediaPluginCEF::onTooltipCallback(std::string text)
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "tooltip_text");
    message.setValue("tooltip", text);
    sendMessage(message);
}
////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onLoadStartCallback()
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "navigate_begin");
    //message.setValue("uri", event.getEventUri());  // not easily available here in CEF - needed?
    message.setValueBoolean("history_back_available", mCEFLib->canGoBack());
    message.setValueBoolean("history_forward_available", mCEFLib->canGoForward());
    sendMessage(message);
}

/////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onLoadError(int status, const std::string error_text, const std::string error_url)
{
    std::stringstream msg;

    msg << "<b>Loading error</b>";
    msg << "<p>";
    msg << "Error message: " << error_text;
    msg << "<p>";
    msg << "Error URL: <tt>" << error_url << "</tt>";
    msg << "<p>";
    msg << "Error code: " << status;

    mCEFLib->showBrowserMessage(msg.str());
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onRequestExitCallback()
{
    LLPluginMessage message("base", "goodbye");
    sendMessage(message);

    // Will trigger delete on next staticReceiveMessage()
    mDeleteMe = true;
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onLoadEndCallback(int httpStatusCode, std::string url)
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "navigate_complete");
    //message.setValue("uri", event.getEventUri());  // not easily available here in CEF - needed?
    message.setValueS32("result_code", httpStatusCode);
    message.setValueBoolean("history_back_available", mCEFLib->canGoBack());
    message.setValueBoolean("history_forward_available", mCEFLib->canGoForward());
    message.setValue("uri", url);
    sendMessage(message);
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onAddressChangeCallback(std::string url)
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "location_changed");
    message.setValue("uri", url);
    sendMessage(message);
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onOpenPopupCallback(std::string url, std::string target)
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "click_href");
    message.setValue("uri", url);
    message.setValue("target", target);
    sendMessage(message);
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onCustomSchemeURLCallback(std::string url, bool user_gesture, bool is_redirect)
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "click_nofollow");
    message.setValue("uri", url);

    // indicate if this interaction was from a user click (okay on a SLAPP) or
    // via a navigation (e.g. a data URL - see SL-18151) (not okay on a SLAPP)
    const std::string nav_type = user_gesture ? "clicked" : "navigated";

    message.setValue("nav_type", nav_type);
    message.setValueBoolean("is_redirect", is_redirect);

    sendMessage(message);
}

////////////////////////////////////////////////////////////////////////////////
//
bool MediaPluginCEF::onHTTPAuthCallback(const std::string host, const std::string realm, std::string& username, std::string& password)
{
    mAuthOK = false;

    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "auth_request");
    message.setValue("url", host);
    message.setValue("realm", realm);
    message.setValueBoolean("blocking_request", true);

    // The "blocking_request" key in the message means this sendMessage call will block until a response is received.
    sendMessage(message);

    if (mAuthOK)
    {
        username = mAuthUsername;
        password = mAuthPassword;
    }

    return mAuthOK;
}

////////////////////////////////////////////////////////////////////////////////
//
const std::vector<std::string> MediaPluginCEF::onFileDialog(dullahan::EFileDialogType dialog_type, const std::string dialog_title, const std::string default_file, std::string dialog_accept_filter, bool& use_default)
{
    // do not use the default CEF file picker
    use_default = false;

    if (dialog_type == dullahan::FD_OPEN_FILE)
    {
        mPickedFiles.clear();

        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "pick_file");
        message.setValueBoolean("blocking_request", true);
        message.setValueBoolean("multiple_files", false);

        sendMessage(message);

        return mPickedFiles;
    }
    else if (dialog_type == dullahan::FD_OPEN_MULTIPLE_FILES)
    {
        mPickedFiles.clear();

        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "pick_file");
        message.setValueBoolean("blocking_request", true);
        message.setValueBoolean("multiple_files", true);

        sendMessage(message);

        return mPickedFiles;
    }
    else if (dialog_type == dullahan::FD_SAVE_FILE)
    {
        mPickedFiles.clear();
        mAuthOK = false;

        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "file_download");
        message.setValueBoolean("blocking_request", true);
        message.setValue("filename", default_file);

        sendMessage(message);

        return mPickedFiles;
    }

    return std::vector<std::string>();
}

////////////////////////////////////////////////////////////////////////////////
//
bool MediaPluginCEF::onJSDialogCallback(const std::string origin_url, const std::string message_text, const std::string default_prompt_text)
{
    // return true indicates we suppress the JavaScript alert UI entirely
    return true;
}

////////////////////////////////////////////////////////////////////////////////
//
bool MediaPluginCEF::onJSBeforeUnloadCallback()
{
    // return true indicates we suppress the JavaScript UI entirely
    return true;
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::onCursorChangedCallback(dullahan::ECursorType type)
{
    std::string name = "";

    switch (type)
    {
        case dullahan::CT_POINTER:
            name = "UI_CURSOR_ARROW";
            break;
        case dullahan::CT_CROSS:
            name = "UI_CURSOR_CROSS";
            break;
        case dullahan::CT_HAND:
            name = "UI_CURSOR_HAND";
            break;
        case dullahan::CT_IBEAM:
            name = "UI_CURSOR_IBEAM";
            break;
        case dullahan::CT_WAIT:
            name = "UI_CURSOR_WAIT";
            break;
        //case dullahan::CT_HELP:
        case dullahan::CT_ROWRESIZE:
        case dullahan::CT_NORTHRESIZE:
        case dullahan::CT_SOUTHRESIZE:
        case dullahan::CT_NORTHSOUTHRESIZE:
            name = "UI_CURSOR_SIZENS";
            break;
        case dullahan::CT_COLUMNRESIZE:
        case dullahan::CT_EASTRESIZE:
        case dullahan::CT_WESTRESIZE:
        case dullahan::CT_EASTWESTRESIZE:
            name = "UI_CURSOR_SIZEWE";
            break;
        case dullahan::CT_NORTHEASTRESIZE:
        case dullahan::CT_SOUTHWESTRESIZE:
        case dullahan::CT_NORTHEASTSOUTHWESTRESIZE:
            name = "UI_CURSOR_SIZENESW";
            break;
        case dullahan::CT_SOUTHEASTRESIZE:
        case dullahan::CT_NORTHWESTRESIZE:
        case dullahan::CT_NORTHWESTSOUTHEASTRESIZE:
            name = "UI_CURSOR_SIZENWSE";
            break;
        case dullahan::CT_MOVE:
            name = "UI_CURSOR_SIZEALL";
            break;
        //case dullahan::CT_MIDDLEPANNING:
        //case dullahan::CT_EASTPANNING:
        //case dullahan::CT_NORTHPANNING:
        //case dullahan::CT_NORTHEASTPANNING:
        //case dullahan::CT_NORTHWESTPANNING:
        //case dullahan::CT_SOUTHPANNING:
        //case dullahan::CT_SOUTHEASTPANNING:
        //case dullahan::CT_SOUTHWESTPANNING:
        //case dullahan::CT_WESTPANNING:
        //case dullahan::CT_VERTICALTEXT:
        //case dullahan::CT_CELL:
        //case dullahan::CT_CONTEXTMENU:
        case dullahan::CT_ALIAS:
            name = "UI_CURSOR_TOOLMEDIAOPEN";
            break;
        case dullahan::CT_PROGRESS:
            name = "UI_CURSOR_WORKING";
            break;
        case dullahan::CT_COPY:
            name = "UI_CURSOR_ARROWCOPY";
            break;
        case dullahan::CT_NONE:
            name = "UI_CURSOR_NO";
            break;
        case dullahan::CT_NODROP:
        case dullahan::CT_NOTALLOWED:
            name = "UI_CURSOR_NOLOCKED";
            break;
        case dullahan::CT_ZOOMIN:
            name = "UI_CURSOR_TOOLZOOMIN";
            break;
        case dullahan::CT_ZOOMOUT:
            name = "UI_CURSOR_TOOLZOOMOUT";
            break;
        case dullahan::CT_GRAB:
            name = "UI_CURSOR_TOOLGRAB";
            break;
        //case dullahan::CT_GRABING:
        //case dullahan::CT_CUSTOM:

        default:
            LL_WARNS() << "Unknown cursor ID: " << (int)type << LL_ENDL;
            break;
    }

    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "cursor_changed");
    message.setValue("name", name);
    sendMessage(message);
}

////////////////////////////////////////////////////////////////////////////////
// Fired by CEF when the page requests a context menu (right-click). This is the
// point at which the edit-state is known to be current, so push it to the host
// first (so canCut/canCopy/... are fresh), then send a context_menu message
// telling the host to show its own menu at the given page coordinate. The host
// defers showing its menu until this arrives precisely so the enable state is
// correct - see LLMediaCtrl.
void MediaPluginCEF::onRequestContextMenuCallback(int x, int y, unsigned int edit_flags)
{
    // make sure the cached can-undo/copy/paste/... state the menu reads is up
    // to date before we ask the host to show the menu
    checkEditState();

    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "context_menu");
    message.setValueS32("x", x);
    message.setValueS32("y", y);
    sendMessage(message);
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::authResponse(LLPluginMessage &message)
{
    mAuthOK = message.getValueBoolean("ok");
    if (mAuthOK)
    {
        mAuthUsername = message.getValue("username");
        mAuthPassword = message.getValue("password");
    }
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::receiveMessage(const std::string& message_string)
{
    //  std::cerr << "MediaPluginCEF::receiveMessage: received message: \"" << message_string << "\"" << std::endl;
    LLPluginMessage message_in;

    if (message_in.parse(message_string) >= 0)
    {
        std::string message_class = message_in.getClass();
        std::string message_name = message_in.getName();
        if (message_class == LLPLUGIN_MESSAGE_CLASS_BASE)
        {
            if (message_name == "init")
            {
                LLPluginMessage message("base", "init_response");
                LLSD versions = LLSD::emptyMap();
                versions[LLPLUGIN_MESSAGE_CLASS_BASE] = LLPLUGIN_MESSAGE_CLASS_BASE_VERSION;
                versions[LLPLUGIN_MESSAGE_CLASS_MEDIA] = LLPLUGIN_MESSAGE_CLASS_MEDIA_VERSION;
                versions[LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER] = LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER_VERSION;
                message.setValueLLSD("versions", versions);

                std::string plugin_version = "CEF plugin 1.1.412";
                message.setValue("plugin_version", plugin_version);
                sendMessage(message);
            }
            else if (message_name == "idle")
            {
                mCEFLib->update();

                mVolumeCatcher.pump();

                // this seems bad but unless the state changes (it won't until we figure out
                // how to get CEF to tell us if copy/cut/paste is available) then this function
                // will return immediately
                checkEditState();
            }
            else if (message_name == "cleanup")
            {
                mCEFLib->requestExit();
            }
            else if (message_name == "force_exit")
            {
                mDeleteMe = true;
            }
            else if (message_name == "shm_added")
            {
                SharedSegmentInfo info;
                info.mAddress = message_in.getValuePointer("address");
                info.mSize = (size_t)message_in.getValueS32("size");
                std::string name = message_in.getValue("name");

                mSharedSegments.insert(SharedSegmentMap::value_type(name, info));

            }
            else if (message_name == "shm_remove")
            {
                std::string name = message_in.getValue("name");

                SharedSegmentMap::iterator iter = mSharedSegments.find(name);
                if (iter != mSharedSegments.end())
                {
                    if (mPixels == iter->second.mAddress)
                    {
                        mPixels = NULL;
                        mTextureSegmentName.clear();
                    }
                    mSharedSegments.erase(iter);
                }
                else
                {
                }

                LLPluginMessage message("base", "shm_remove_response");
                message.setValue("name", name);
                sendMessage(message);
            }
            else
            {
            }
        }
        else if (message_class == LLPLUGIN_MESSAGE_CLASS_MEDIA)
        {
            if (message_name == "init")
            {
                // Zero-copy paint: the viewer asks for GPU shared-texture handles
                // and tells us its process id so we can DuplicateHandle into it.
                mUseAcceleratedPaint = message_in.getValueBoolean("accelerated_paint");
                mHostPid = message_in.getValueS32("host_pid");
                mAccelId = message_in.getValueS32("accel_id");
                // Linux: the viewer's windowing backend, used below to pin CEF's
                // Ozone platform (X11 vs Wayland) instead of guessing from env.
                mDisplayServer = message_in.getValue("display_server");

                // event callbacks from Dullahan
                mCEFLib->setOnPageChangedCallback(std::bind(&MediaPluginCEF::onPageChangedCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
                if (mUseAcceleratedPaint)
                {
#if LL_LINUX
                    // Linux frames are dma-bufs (planes + modifier), not a single handle.
                    mCEFLib->setOnAcceleratedPaintDmabufCallback(std::bind(&MediaPluginCEF::onAcceleratedPaintDmabufCallback, this,
                        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                        std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
#else
                    mCEFLib->setOnAcceleratedPaintCallback(std::bind(&MediaPluginCEF::onAcceleratedPaintCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
#endif
                }
                mCEFLib->setOnCustomSchemeURLCallback(std::bind(&MediaPluginCEF::onCustomSchemeURLCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
                mCEFLib->setOnConsoleMessageCallback(std::bind(&MediaPluginCEF::onConsoleMessageCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
                mCEFLib->setOnStatusMessageCallback(std::bind(&MediaPluginCEF::onStatusMessageCallback, this, std::placeholders::_1));
                mCEFLib->setOnTitleChangeCallback(std::bind(&MediaPluginCEF::onTitleChangeCallback, this, std::placeholders::_1));
                mCEFLib->setOnTooltipCallback(std::bind(&MediaPluginCEF::onTooltipCallback, this, std::placeholders::_1));
                mCEFLib->setOnLoadStartCallback(std::bind(&MediaPluginCEF::onLoadStartCallback, this));
                mCEFLib->setOnLoadEndCallback(std::bind(&MediaPluginCEF::onLoadEndCallback, this, std::placeholders::_1, std::placeholders::_2));

                // CEF 139 seems to have introduced a loading failure at the login page (only?) I haven't seen it on
                // any other page and it only happens about 1 in 8 times. Without this handler for the error page
                // (red box, error message/code/url) the page load recovers after display a brief built in error.
                // Not ideal but better than stopping altgoether. Will restore this once I discover the error.
                //mCEFLib->setOnLoadErrorCallback(std::bind(&MediaPluginCEF::onLoadError, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
                mCEFLib->setOnAddressChangeCallback(std::bind(&MediaPluginCEF::onAddressChangeCallback, this, std::placeholders::_1));
                mCEFLib->setOnOpenPopupCallback(std::bind(&MediaPluginCEF::onOpenPopupCallback, this, std::placeholders::_1, std::placeholders::_2));
                mCEFLib->setOnHTTPAuthCallback(std::bind(&MediaPluginCEF::onHTTPAuthCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
                mCEFLib->setOnFileDialogCallback(std::bind(&MediaPluginCEF::onFileDialog, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
                mCEFLib->setOnCursorChangedCallback(std::bind(&MediaPluginCEF::onCursorChangedCallback, this, std::placeholders::_1));
                mCEFLib->setOnRequestExitCallback(std::bind(&MediaPluginCEF::onRequestExitCallback, this));
                mCEFLib->setOnJSDialogCallback(std::bind(&MediaPluginCEF::onJSDialogCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
                mCEFLib->setOnJSBeforeUnloadCallback(std::bind(&MediaPluginCEF::onJSBeforeUnloadCallback, this));
                mCEFLib->setOnRequestContextMenuCallback(std::bind(&MediaPluginCEF::onRequestContextMenuCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

                dullahan::dullahan_settings settings;
#if LL_WINDOWS
                // As of CEF version 83+, for Windows versions, we need to tell CEF
                // where the host helper process is since this DLL is not in the same
                // dir as the executable that loaded it (SLPlugin.exe). The code in
                // Dullahan that tried to figure out the location automatically uses
                // the location of the exe which isn't helpful so we tell it explicitly.
                std::vector<wchar_t> buffer(MAX_PATH + 1);
                GetCurrentDirectoryW(MAX_PATH, &buffer[0]);
                settings.host_process_path = ll_convert_wide_to_string(&buffer[0]);
#endif
                settings.accept_language_list = mHostLanguage;
                settings.accelerated_paint = mUseAcceleratedPaint;

                // SL-15560: Product team overruled my change to set the default
                // embedded background color to match the floater background
                // and set it to white
                settings.background_color = 0xffffffff; // white

                settings.root_cache_path = mRootCachePath;
                settings.cookies_enabled = mCookiesEnabled;

                // Linux only: pin CEF's Ozone backend to the viewer's windowing
                // backend (empty = let dullahan auto-detect). No-op elsewhere.
                settings.ozone_platform = mDisplayServer;

                // configure proxy argument if enabled and valid
                if (mProxyEnabled && mProxyHost.length())
                {
                    std::ostringstream proxy_url;
                    proxy_url << mProxyHost << ":" << mProxyPort;
                    settings.proxy_host_port = proxy_url.str();
                }
                settings.disable_gpu = mDisableGPU;
#if LL_DARWIN
               settings.use_mock_keychain = mUseMockKeyChain;
#endif
                // these were added to facilitate loading images directly into a local
                // web page for the prototype 360 project in 2017 - something that is
                // disallowed normally by the browser security model. Now the the source
                // (cubemap) images are stores as JavaScript, we can avoid opening up
                // this security hole (it was only set for the 360 floater but still
                // a concern). Leaving them here, explicitly turn off vs removing
                // entirely from this source file so that others are aware of them
                // in the future.
                settings.disable_web_security = false;
                settings.file_access_from_file_urls = false;

                // This setting applies to all plugins, not just Flash
                // Regarding, SL-15559 PDF files do not load in CEF v91,
                // it turns out that on Windows, PDF support is treated
                // as a plugin on Windows only so turning all plugins
                // off, disabled built in PDF support.  (Works okay in
                // macOS surprisingly). To mitigrate this, we set the global
                // media enabled flag to whatever the consumer wants and
                // explicitly disable Flash with a different setting (below)
                settings.plugins_enabled = mPluginsEnabled;

                settings.flip_mouse_y = false;
                settings.flip_pixels_y = true;
                settings.frame_rate = 60;
                settings.force_wave_audio = true;
                settings.initial_height = 1024;
                settings.initial_width = 1024;
                settings.javascript_enabled = mJavascriptEnabled;
                settings.media_stream_enabled = false; // MAINT-6060 - WebRTC media removed until we can add granularity/query UI

                settings.user_agent_substring = mCEFLib->makeCompatibleUserAgentString(mUserAgentSubtring);
                settings.webgl_enabled = true;
                settings.log_file = mCefLogFile;
                settings.log_verbose = mCefLogVerbose;
                settings.autoplay_without_gesture = true;

                std::vector<std::string> custom_schemes(1, "secondlife");
                mCEFLib->setCustomSchemes(custom_schemes);

                bool result = mCEFLib->init(settings);
                if (!result)
                {
                    // if this fails, the media system in viewer will put up a message
                }

                // now we can set page zoom factor
                F32 factor = (F32)message_in.getValueReal("factor");
                mCEFLib->setPageZoom(factor);

                // Plugin gets to decide the texture parameters to use.
                mDepth = 4;
                LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "texture_params");
                message.setValueS32("default_width", 1024);
                message.setValueS32("default_height", 1024);
                message.setValueS32("depth", mDepth);
                // Accelerated paint copies the BGRA shared texture into the media
                // texture with glCopyImageSubData, which needs matching 32-bit
                // (RGBA8) storage; the CPU path uploads BGRA into RGB as before.
                message.setValueU32("internalformat", mUseAcceleratedPaint ? GL_RGBA8 : GL_RGB8);
                message.setValueU32("format", GL_BGRA);
                message.setValueU32("type", GL_UNSIGNED_BYTE);
                message.setValueBoolean("coords_opengl", true);
                sendMessage(message);
            }
            else if (message_name == "set_user_data_path")
            {
                std::string user_data_path_cache = message_in.getValue("cache_path");
                std::string subfolder = message_in.getValue("username");

                // media plugin doesn't have access to gDirUtilp
                std::string path_separator;
#if LL_WINDOWS
                path_separator = "\\";
#else
                path_separator = "/";
#endif

                mRootCachePath = user_data_path_cache + "cef_cache";

                // Issue #4498 Introduce an additional sub-folder underneath the main cache
                // folder so that each CEF media instance gets its own (as per the CEF API
                // official position). These folders will be removed at startup by Viewer code
                // so that their non-trivial size does not exhaust available disk space. This
                // begs the question - why turn on the cache at all? There are 2 reasons - firstly
                // some of the instances will benefit from per Viewer session caching and will
                // use the injected SL cookie and secondly, it's not clear how having no cache
                // interacts with the multiple simultaneous paradigm we use.
                mRootCachePath += path_separator;
# if LL_WINDOWS
                mRootCachePath += std::to_string(_getpid());
# else
                mRootCachePath += std::to_string(getpid());
# endif


                mCefLogFile = message_in.getValue("cef_log_file");
                mCefLogVerbose = message_in.getValueBoolean("cef_verbose_log");
            }
            else if (message_name == "size_change")
            {
                std::string name = message_in.getValue("name");
                S32 width = message_in.getValueS32("width");
                S32 height = message_in.getValueS32("height");
                S32 texture_width = message_in.getValueS32("texture_width");
                S32 texture_height = message_in.getValueS32("texture_height");

                if (!name.empty())
                {
                    // Find the shared memory region with this name
                    SharedSegmentMap::iterator iter = mSharedSegments.find(name);
                    if (iter != mSharedSegments.end())
                    {
                        mPixels = (unsigned char*)iter->second.mAddress;
                        mWidth = width;
                        mHeight = height;

                        mTextureWidth = texture_width;
                        mTextureHeight = texture_height;

                        mCEFLib->setSize(mWidth, mHeight);
                    };
                };

                LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "size_change_response");
                message.setValue("name", name);
                message.setValueS32("width", width);
                message.setValueS32("height", height);
                message.setValueS32("texture_width", texture_width);
                message.setValueS32("texture_height", texture_height);
                sendMessage(message);

            }
            else if (message_name == "set_language_code")
            {
                mHostLanguage = message_in.getValue("language");
            }
            else if (message_name == "load_uri")
            {
                std::string uri = message_in.getValue("uri");
                mCEFLib->navigate(uri);
            }
            else if (message_name == "execute_javascript")
            {
                std::string code = message_in.getValue("code");
                mCEFLib->executeJavaScript(code);
            }
            else if (message_name == "set_cookie")
            {
                std::string uri = message_in.getValue("uri");
                std::string name = message_in.getValue("name");
                std::string value = message_in.getValue("value");
                std::string domain = message_in.getValue("domain");
                std::string path = message_in.getValue("path");
                bool httponly = message_in.getValueBoolean("httponly");
                bool secure = message_in.getValueBoolean("secure");
                mCEFLib->setCookie(uri, name, value, domain, path, httponly, secure);
            }
            else if (message_name == "mouse_event")
            {
                std::string event = message_in.getValue("event");

                S32 x = message_in.getValueS32("x");
                S32 y = message_in.getValueS32("y");

                // map the viewer's button index to the dullahan button. The
                // viewer uses 0 = left, 1 = right, 2 = middle (see
                // LLViewerMediaImpl::mouseDown/Up callers). Unknown values fall
                // back to left.
                S32 button = message_in.getValueS32("button");
                dullahan::EMouseButton btn = dullahan::MB_MOUSE_BUTTON_LEFT;
                if (button == 1)
                {
                    btn = dullahan::MB_MOUSE_BUTTON_RIGHT;
                }
                else if (button == 2)
                {
                    btn = dullahan::MB_MOUSE_BUTTON_MIDDLE;
                }

                // keyboard modifiers held during the event (shift/ctrl/alt/meta)
                uint32_t mods = message_in.getValueU32("modifiers");

                if (event == "down")
                {
                    // A right-click is forwarded to CEF so the renderer runs its
                    // context-menu path - that's what fires the browser client's
                    // OnBeforeContextMenu and refreshes the edit-state the menus
                    // read - but it must not be treated as a page interaction:
                    // only a left-click gives the page input focus. (Note: CEF
                    // has no way to drive the context-menu callback without the
                    // renderer also seeing the click, so the page's own
                    // contextmenu/mousedown JS still runs; the native CEF menu is
                    // suppressed in the browser client.)
                    mCEFLib->mouseButton(btn, dullahan::ME_MOUSE_DOWN, x, y, mods);
                    if (btn == dullahan::MB_MOUSE_BUTTON_LEFT)
                    {
                        mCEFLib->setFocus(true);
                    }

                    std::stringstream str;
                    str << "Mouse down at = " << x << ", " << y;
                    postDebugMessage(str.str());
                }
                else if (event == "up")
                {
                    mCEFLib->mouseButton(btn, dullahan::ME_MOUSE_UP, x, y, mods);

                    std::stringstream str;
                    str << "Mouse up at = " << x << ", " << y;
                    postDebugMessage(str.str());
                }
                else if (event == "double_click")
                {
                    // CEF only synthesizes the multi-click sequence (and the
                    // text selection it drives) for the left button
                    mCEFLib->mouseButton(btn, dullahan::ME_MOUSE_DOUBLE_CLICK, x, y, mods);
                }
                else
                {
                    mCEFLib->mouseMove(x, y, false, mods);
                }
            }
            else if (message_name == "scroll_event")
            {
                // Mouse coordinates for cef to be able to scroll 'containers'
                S32 x = message_in.getValueS32("x");
                S32 y = message_in.getValueS32("y");

                // Wheel's clicks
                S32 delta_x = message_in.getValueS32("clicks_x");
                S32 delta_y = message_in.getValueS32("clicks_y");
                const int scaling_factor = 40;
                delta_x *= -scaling_factor;
                delta_y *= -scaling_factor;

                uint32_t mods = message_in.getValueU32("modifiers");
                mCEFLib->mouseWheel(x, y, delta_x, delta_y, mods);
            }
            else if (message_name == "text_event")
            {
                // NB: the committed text is sent under "text" (see
                // LLPluginClassMedia::textInput); reading "event" here always
                // yielded an empty string, so no characters were ever inserted
                // via this path.
                std::string text = message_in.getValue("text");
                LLSD native_key_data = message_in.getValueLLSD("native_key_data");
                unicodeInput(text, native_key_data);
            }
            else if (message_name == "key_event")
            {
                std::string event = message_in.getValue("event");
                LLSD native_key_data = message_in.getValueLLSD("native_key_data");

                // Treat unknown events as key-up for safety.
                dullahan::EKeyEvent key_event = dullahan::KE_KEY_UP;
                if (event == "down")
                {
                    key_event = dullahan::KE_KEY_DOWN;
                }
                else if (event == "repeat")
                {
                    key_event = dullahan::KE_KEY_REPEAT;
                }

                keyEvent(key_event, native_key_data);
            }
            else if (message_name == "enable_media_plugin_debugging")
            {
                mEnableMediaPluginDebugging = message_in.getValueBoolean("enable");
            }
#if LL_LINUX
            else if (message_name == "enable_pipewire_volume_catcher")
            {
                bool enable = message_in.getValueBoolean("enable");
                mVolumeCatcher.onEnablePipeWireVolumeCatcher(enable);
            }
#endif
            if (message_name == "pick_file_response")
            {
                LLSD file_list_llsd = message_in.getValueLLSD("file_list");

                LLSD::array_const_iterator iter = file_list_llsd.beginArray();
                LLSD::array_const_iterator end = file_list_llsd.endArray();
                for (; iter != end; ++iter)
                {
                    mPickedFiles.push_back(((*iter).asString()));
                }
            }
            if (message_name == "auth_response")
            {
                authResponse(message_in);
            }
            if (message_name == "edit_undo")
            {
                mCEFLib->editUndo();
            }
            if (message_name == "edit_redo")
            {
                mCEFLib->editRedo();
            }
            if (message_name == "edit_cut")
            {
                mCEFLib->editCut();
            }
            if (message_name == "edit_copy")
            {
                mCEFLib->editCopy();
            }
            if (message_name == "edit_paste")
            {
                mCEFLib->editPaste();
            }
            if (message_name == "edit_delete")
            {
                mCEFLib->editDelete();
            }
            if (message_name == "edit_select_all")
            {
                mCEFLib->editSelectAll();
            }
            if (message_name == "edit_show_source")
            {
                mCEFLib->viewSource();
            }
        }
        else if (message_class == LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER)
        {
            if (message_name == "set_page_zoom_factor")
            {
                F32 factor = (F32)message_in.getValueReal("factor");
                mCEFLib->setPageZoom(factor);
            }
            if (message_name == "browse_stop")
            {
                mCEFLib->stop();
            }
            else if (message_name == "browse_reload")
            {
                bool ignore_cache = true;
                mCEFLib->reload(ignore_cache);
            }
            else if (message_name == "browse_forward")
            {
                mCEFLib->goForward();
            }
            else if (message_name == "browse_back")
            {
                mCEFLib->goBack();
            }
            else if (message_name == "cookies_enabled")
            {
                mCookiesEnabled = message_in.getValueBoolean("enable");
            }
            else if (message_name == "clear_cookies")
            {
                mCEFLib->deleteAllCookies();
            }
            else if (message_name == "set_user_agent")
            {
                mUserAgentSubtring = message_in.getValue("user_agent");
            }
            else if (message_name == "show_web_inspector")
            {
                mCEFLib->showDevTools();
            }
            else if (message_name == "plugins_enabled")
            {
                mPluginsEnabled = message_in.getValueBoolean("enable");
            }
            else if (message_name == "javascript_enabled")
            {
                mJavascriptEnabled = message_in.getValueBoolean("enable");
            }
            else if (message_name == "gpu_disabled")
            {
                mDisableGPU = message_in.getValueBoolean("disable");
            }
            else if (message_name == "proxy_setup")
            {
                mProxyEnabled = message_in.getValueBoolean("enable");
                mProxyHost = message_in.getValue("host");
                mProxyPort = message_in.getValueS32("port");
            }
            else if (message_name == "web_security_disabled")
            {
                mDisableWebSecurity = message_in.getValueBoolean("disabled");
            }
            else if (message_name == "file_access_from_file_urls")
            {
                mFileAccessFromFileUrls = message_in.getValueBoolean("enabled");
            }
            else if (message_name == "focused")
            {
                bool focused = message_in.getValueBoolean("focused");
                mCEFLib->setFocus(focused);
            }
        }
        else if (message_class == LLPLUGIN_MESSAGE_CLASS_MEDIA_TIME)
        {
            if (message_name == "set_volume")
            {
                F32 volume = (F32)message_in.getValueReal("volume");
                mCurVolume = volume;
                setVolume();
            }
        }
        else
        {
        };
    }
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::keyEvent(dullahan::EKeyEvent key_event, LLSD native_key_data = LLSD::emptyMap())
{
#if LL_LINUX || LL_DARWIN
    uint32_t native_virtual_key = (uint32_t)(native_key_data["virtual_key"].asInteger());       // this is actually the SDL event.key.keysym.sym;
    uint32_t native_virtual_key_win = (uint32_t)(native_key_data["virtual_key_win"].asInteger());
    // raw SDL modifier mask (SDL_Keymod); dullahan translates it to CEF flags
    uint32_t native_modifiers = (uint32_t)(native_key_data["modifiers"].asInteger());
    // platform scancode (SDL_KeyboardEvent.raw); CEF needs it as
    // native_key_code or the key event is dropped before reaching the page
    uint32_t native_scan_code = (uint32_t)(native_key_data["sdl_scancode"].asInteger());

    // Send key-down/up (RAWKEYDOWN / KEYUP) for EVERY key, including printable
    // ones. dullahan maps the SDL keysym to the correct Windows virtual-key
    // code, so the page receives a DOM keydown with the right keyCode - which
    // JS-driven fields (search boxes, SPAs) rely on. The actual character
    // insertion happens separately in unicodeInput() (a CHAR event from the
    // committed text). Previously printable keys were excluded here and the
    // character path tried to synthesise the keydown itself with a bogus
    // (always-zero) key code, so those fields misbehaved.
    bool keypad = (native_virtual_key_win >= 0x60 && native_virtual_key_win <= 0x6f);
    mCEFLib->nativeKeyboardEventSDL2(key_event, native_virtual_key, native_modifiers, keypad, native_scan_code);
#elif LL_WINDOWS
    U32 msg = ll_U32_from_sd(native_key_data["msg"]);
    U32 wparam = ll_U32_from_sd(native_key_data["w_param"]);
    U64 lparam = ll_U32_from_sd(native_key_data["l_param"]);

    mCEFLib->nativeKeyboardEventWin(msg, wparam, lparam);
#endif
};

void MediaPluginCEF::unicodeInput(std::string text, LLSD native_key_data = LLSD::emptyMap())
{
#if LL_LINUX || LL_DARWIN
    // 'text' is the committed UTF-8 string for this text event. Deliver it to
    // the page as CHAR events - one per Unicode codepoint - which is what
    // actually inserts characters into the focused DOM field. The matching
    // keydown/keyup (with the correct virtual-key code) is sent separately by
    // keyEvent(). dullahan strips control/shift from CHAR internally so a
    // CHAR isn't mistaken for a shortcut.
    //
    // Previously this read native_key_data["sdl_sym"], which the SDL window
    // backend never sets (getNativeKeyData only provides virtual_key /
    // virtual_key_win / modifiers), so the synthesised key code was always 0
    // and every page keydown carried keyCode 0. Sending the real text avoids
    // depending on the keysym at all and fixes IME / dead-key / multi-byte
    // input that a single keysym can't represent.
    LLWString wtext = utf8str_to_wstring(text);
    for (llwchar cp : wtext)
    {
        mCEFLib->nativeKeyboardEventSDL2(dullahan::KE_KEY_CHAR, (uint32_t)cp, 0, false);
    }
#elif LL_WINDOWS
    text = ""; // not needed here but prevents unused var warning as error
    U32 msg = ll_U32_from_sd(native_key_data["msg"]);
    U32 wparam = ll_U32_from_sd(native_key_data["w_param"]);
    U64 lparam = ll_U32_from_sd(native_key_data["l_param"]);
    mCEFLib->nativeKeyboardEventWin(msg, wparam, lparam);
#endif // LL_LINUX
};

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginCEF::checkEditState()
{
    bool can_undo = mCEFLib->editCanUndo();
    bool can_redo = mCEFLib->editCanRedo();
    bool can_cut = mCEFLib->editCanCut();
    bool can_copy = mCEFLib->editCanCopy();
    bool can_paste = mCEFLib->editCanPaste();
    bool can_delete = mCEFLib->editCanDelete();
    bool can_select_all = mCEFLib->editCanSelectAll();

    if ((can_undo != mCanUndo) || (can_redo != mCanRedo) || (can_cut != mCanCut) || (can_copy != mCanCopy)
        || (can_paste != mCanPaste) || (can_delete != mCanDelete) || (can_select_all != mCanSelectAll))
    {
        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "edit_state");

        if (can_undo != mCanUndo)
        {
            mCanUndo = can_undo;
            message.setValueBoolean("undo", can_undo);
        }

        if (can_redo != mCanRedo)
        {
            mCanRedo = can_redo;
            message.setValueBoolean("redo", can_redo);
        }

        if (can_cut != mCanCut)
        {
            mCanCut = can_cut;
            message.setValueBoolean("cut", can_cut);
        }

        if (can_copy != mCanCopy)
        {
            mCanCopy = can_copy;
            message.setValueBoolean("copy", can_copy);
        }

        if (can_paste != mCanPaste)
        {
            mCanPaste = can_paste;
            message.setValueBoolean("paste", can_paste);
        }

        if (can_delete != mCanDelete)
        {
            mCanDelete = can_delete;
            message.setValueBoolean("delete", can_delete);
        }

        if (can_select_all != mCanSelectAll)
        {
            mCanSelectAll = can_select_all;
            message.setValueBoolean("select_all", can_select_all);
        }

        sendMessage(message);
    }
}

void MediaPluginCEF::setVolume()
{
    mVolumeCatcher.setVolume(mCurVolume);
}

////////////////////////////////////////////////////////////////////////////////
//
bool MediaPluginCEF::init()
{
    LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "name_text");
    message.setValue("name", "CEF Plugin");
    sendMessage(message);

    return true;
};

////////////////////////////////////////////////////////////////////////////////
//
int init_media_plugin(LLPluginInstance::sendMessageFunction host_send_func,
    void* host_user_data,
    LLPluginInstance::sendMessageFunction *plugin_send_func,
    void **plugin_user_data)
{
    MediaPluginCEF* self = new MediaPluginCEF(host_send_func, host_user_data);
    *plugin_send_func = MediaPluginCEF::staticReceiveMessage;
    *plugin_user_data = (void*)self;

    return 0;
}
