/**
 * @file llappviewersdl.cpp
 * @brief The LLAppViewerSDL class definitions
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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
 */

#include "llviewerprecompiledheaders.h"

#include "llappviewersdl.h"

#include "llcommandlineparser.h"

#include "lldiriterator.h"
#include "llurldispatcher.h"        // SLURL from other app instance
#include "llviewernetwork.h"
#include "llviewercontrol.h"
#include "llmd5.h"
#include "llfindlocale.h"
#include "llversioninfo.h"

#include <exception>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include "SDL3/SDL.h"

#include "llsdl.h"
#include "llwindowsdl.h"

#if LL_WINDOWS
#include "llwin32headers.h"     // GetCommandLineW
#include "llappviewerwin32.h"   // LLAppViewerWin32, create_app_mutex, NVAPI session helpers
#include <shlwapi.h>            // PathGetArgsW
#if LL_VELOPACK
#include "llvelopack.h"
#endif
#endif

#if LL_DARWIN
#include <sys/types.h>
#include <unistd.h>
#include <sys/sysctl.h>

#include <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>   // kInternetEventClass / kAEGetURL

#if defined(LL_BUGSPLAT)
#include <filesystem>
#include <vector>
#include "llbugsplat_mac.h"
@import BugSplatMac;
#endif

// Forward decl so the Obj-C @implementation below can call into the C++ side.
static void handleUrl(const char* url_utf8);

// A thin NSApplicationDelegate that wraps SDL3's delegate so we can receive
// secondlife:// Apple Events. We capture SDL's existing delegate, install
// ourselves as NSApp.delegate, and forward every selector we don't implement
// back to SDL — so SDL's termination / focus / drop-file plumbing keeps
// working unchanged.
@interface LLSDLAppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) id<NSApplicationDelegate> sdlDelegate;
- (instancetype)initWithSDLDelegate:(id<NSApplicationDelegate>)sdlDelegate;
- (void)handleGetURLEvent:(NSAppleEventDescriptor *)event
           withReplyEvent:(NSAppleEventDescriptor *)replyEvent;
@end

#if defined(LL_BUGSPLAT)
// BugSplat delegate. Lives for the duration of the process so it can
// service the BugsplatMac framework's callbacks after a crash report has
// been queued from the *previous* run.
@interface LLBugSplatDelegate : NSObject <BugSplatDelegate>
@property (nonatomic, assign) std::string secondLogPath;
@end
#endif
#endif

#if LL_LINUX && LL_DBUS
#include <dbus/dbus.h>

#define VIEWERAPI_SERVICE   "com.secondlife.ViewerAppAPIService"
#define VIEWERAPI_PATH      "/com/secondlife/ViewerAppAPI"
#define VIEWERAPI_INTERFACE "com.secondlife.ViewerAppAPI"
#endif

#if LL_DARWIN
// *FIX:Mani It would be nice to provide a clean interface to get the
// default_unix_signal_handler for the LLApp class.
extern void default_unix_signal_handler(int, siginfo_t *, void *);
#endif

namespace
{
    int gArgC = 0;
    char **gArgV = NULL;
    // LLAppViewerWin32 on Windows (USE_SDL_WINDOW builds), LLAppViewerSDL elsewhere.
    LLAppViewer* gViewerAppPtr = NULL;
    void (*gOldTerminateHandler)() = NULL;
#if LL_WINDOWS
    // NvDRSSessionHandle from ll_nvapi_session_create(), torn down in SDL_AppQuit.
    void* gNvApiSession = nullptr;
#endif
#if LL_LINUX && LL_DBUS
    // Shared session-bus connection, owned for the lifetime of the process and
    // pumped from SDL_AppIterate so incoming GoSLURL method calls get dispatched.
    DBusConnection* gDBusConn = nullptr;
#endif
#if LL_DARWIN
    // Buffers a secondlife:// URL that arrived from the OS before the viewer
    // was ready to dispatch it. Drained at the end of LLAppViewerSDL::init().
    std::string gHandleSLURL;
    // Becomes true once LLAppViewerSDL::init() finishes. handleUrl() gates on
    // this rather than gViewerAppPtr because the latter is set at the top of
    // SDL_AppInit — long before LLURLDispatcher is ready to dispatch.
    bool gAppFullyInited = false;
#endif
}

#if LL_DARWIN
static void dispatchUrl(std::string url)
{
    // Safari 3.2 silently mangles secondlife:///app/ URLs into
    // secondlife:/app/ (only one leading slash). Fix them up to meet the URL
    // specification — matches indra/newview/llappviewermacosx.cpp:459.
    const std::string prefix = "secondlife:/app/";
    std::string test_prefix = url.substr(0, prefix.length());
    LLStringUtil::toLower(test_prefix);
    if (test_prefix == prefix)
    {
        url.replace(0, prefix.length(), "secondlife:///app/");
    }

    LLMediaCtrl* web = nullptr;
    const bool trusted_browser = false;
    LLURLDispatcher::dispatch(url, "", web, trusted_browser);
}

static void handleUrl(const char* url_utf8)
{
    if (!url_utf8)
    {
        return;
    }
    if (gAppFullyInited)
    {
        gHandleSLURL.clear();
        dispatchUrl(url_utf8);
    }
    else
    {
        // Last URL wins — same as the native macOS path
        // (llappviewermacosx.cpp), which also stashes a single SLURL.
        gHandleSLURL = url_utf8;
    }
}

// Pre-init SLURL detection. SDL3 on macOS catches launch-time
// kAEGetURL Apple Events in its own NSApplicationDelegate
// (SDL_cocoaevents.m) and re-emits them as SDL_EVENT_DROP_FILE
// events — by the time our LLSDLAppDelegate wrapper installs
// itself at the end of LLAppViewerSDL::init(), the launch URL
// has already been routed through SDL's file-drop conversion.
// Recognise SLURL-shaped DROP_FILE payloads in SDL_AppEvent and
// hand them to handleUrl() (which buffers or dispatches based on
// init state). Runtime URLs continue to flow through the
// delegate-wrapped kAEGetURL path.
static bool isSLURL(const char* s)
{
    if (!s)
    {
        return false;
    }
    // URL schemes are case-insensitive (RFC 3986 §3.1).
    return strncasecmp(s, "secondlife:", 11) == 0
        || strncasecmp(s, "x-grid-location-info:", 21) == 0;
}

@implementation LLSDLAppDelegate

- (instancetype)initWithSDLDelegate:(id<NSApplicationDelegate>)sdlDelegate
{
    self = [super init];
    if (self)
    {
        _sdlDelegate = sdlDelegate;
        // Re-register the Apple Event handler for kInternetEventClass/kAEGetURL.
        // NSAppleEventManager replaces any previously-installed handler for
        // the same (class, id) pair, so this displaces SDL's own URL handler
        // (set in SDL_cocoaevents.m:556) without disturbing SDL's delegate.
        [[NSAppleEventManager sharedAppleEventManager]
            setEventHandler:self
                andSelector:@selector(handleGetURLEvent:withReplyEvent:)
              forEventClass:kInternetEventClass
                 andEventID:kAEGetURL];
    }
    return self;
}

- (void)dealloc
{
    [[NSAppleEventManager sharedAppleEventManager]
        removeEventHandlerForEventClass:kInternetEventClass
                             andEventID:kAEGetURL];
}

- (void)handleGetURLEvent:(NSAppleEventDescriptor *)event
           withReplyEvent:(NSAppleEventDescriptor *)replyEvent
{
    NSString *url = [[[[NSAppleEventManager sharedAppleEventManager]
                          currentAppleEvent]
                         paramDescriptorForKeyword:keyDirectObject]
                        stringValue];
    if (url)
    {
        handleUrl([url UTF8String]);
    }
}

// Anything we don't implement, defer to SDL's delegate so SDL's lifecycle
// hooks (termination, hide/unhide, screen changes, dock file-open, etc.)
// keep working as if SDL were still the delegate.
- (BOOL)respondsToSelector:(SEL)aSelector
{
    if ([super respondsToSelector:aSelector])
    {
        return YES;
    }
    return [_sdlDelegate respondsToSelector:aSelector];
}

- (id)forwardingTargetForSelector:(SEL)aSelector
{
    if (_sdlDelegate && [_sdlDelegate respondsToSelector:aSelector])
    {
        return _sdlDelegate;
    }
    return nil;
}

@end

#if defined(LL_BUGSPLAT)

namespace
{

struct BugSplatAttachmentInfo
{
    BugSplatAttachmentInfo(const std::string& path, const std::string& type):
        pathname(path),
        basename(std::filesystem::path(path).filename().string()),
        mimetype(type)
    {}

    std::string pathname, basename, mimetype;
};

} // namespace

@implementation LLBugSplatDelegate

- (void)bugSplatWillSendCrashReport:(BugSplat *)bugSplat
{
    infos("bugSplatWillSendCrashReport");
}

- (void)bugSplatWillSendCrashReportsAlways:(BugSplat *)bugSplat
{
    infos("bugSplatWillSendCrashReportsAlways");
}

- (void)bugSplatDidFinishSendingCrashReport:(BugSplat *)bugSplat
{
    infos("bugSplatDidFinishSendingCrashReport");

    if (!_secondLogPath.empty())
    {
        std::filesystem::remove(_secondLogPath);
    }
    clearDumpLogsDir();
}

- (void)bugSplatWillCancelSendingCrashReport:(BugSplat *)bugSplat
{
    infos("bugSplatWillCancelSendingCrashReport");
}

- (void)bugSplatWillShowSubmitCrashReportAlert:(BugSplat *)bugSplat
{
    infos("bugSplatWillShowSubmitCrashReportAlert");
}

- (void)bugSplat:(BugSplat *)bugSplat didFailWithError:(NSError *)error
{
    std::string error_str([[error localizedDescription] UTF8String]);
    infos("bugSplat:didFailWithError: " + error_str);
}

- (NSString *)applicationLogForBugSplat:(BugSplat *)bugSplat
{
    CrashMetadata& meta(CrashMetadata_instance());
    // As of BugsplatMac 1.0.6, userName and userEmail properties are exposed
    // by the BugsplatStartupManager. Set them here, since the
    // defaultUserNameForBugsplatStartupManager and
    // defaultUserEmailForBugsplatStartupManager methods are called later, for
    // the *current* run, rather than for the previous crashed run whose crash
    // report we are about to send.
    infos("applicationLogForBugsplatStartupManager setting userName = '" +
          meta.agentFullname + '"');
    bugSplat.userName =
        [NSString stringWithCString:meta.agentFullname.c_str()
                           encoding:NSUTF8StringEncoding];
    // Use the email field for OS version, just as we do on Windows, until
    // BugSplat provides more metadata fields.
    infos("applicationLogForBugsplatStartupManager setting userEmail = '" +
          meta.OSInfo + '"');
    bugSplat.userEmail =
        [NSString stringWithCString:meta.OSInfo.c_str()
                           encoding:NSUTF8StringEncoding];

    // This strangely-named override method's return value contributes the
    // User Description metadata field.
    infos("applicationLogForBugsplatStartupManager -> '" + meta.fatalMessage + "'");
    return [NSString stringWithCString:meta.fatalMessage.c_str()
                              encoding:NSUTF8StringEncoding];
}

- (NSString *)applicationKeyForBugSplat:(BugSplat *)bugSplat
                                 signal:(NSString *)signal
                          exceptionName:(NSString *)exceptionName
                        exceptionReason:(NSString *)exceptionReason
{
    // Windows sends location within region as well, but that's because
    // BugSplat for Windows intercepts crashes during the same run, and that
    // information can be queried once. On the Mac, any metadata we have is
    // written (and rewritten) to the static_debug_info.log file that we read
    // at the start of the next viewer run.
    std::string regionName(CrashMetadata_instance().regionName);
    infos("applicationKeyForBugsplatStartupManager -> '" + regionName + "'");
    return [NSString stringWithCString:regionName.c_str()
                              encoding:NSUTF8StringEncoding];
}

- (NSArray<BugSplatAttachment *> *)attachmentsForBugSplat:(BugSplat *)bugSplat
{
    const CrashMetadata& metadata(CrashMetadata_instance());

    std::vector<BugSplatAttachmentInfo> info{
        BugSplatAttachmentInfo(metadata.logFilePathname,         "text/plain"),
        BugSplatAttachmentInfo(metadata.userSettingsPathname,    "text/xml"),
        BugSplatAttachmentInfo(metadata.accountSettingsPathname, "text/xml"),
        BugSplatAttachmentInfo(metadata.staticDebugPathname,     "text/xml"),
        BugSplatAttachmentInfo(metadata.attributesPathname,      "text/xml")
    };

    _secondLogPath = metadata.secondLogFilePathname;
    if (!_secondLogPath.empty())
    {
        info.push_back(BugSplatAttachmentInfo(_secondLogPath, "text/xml"));
    }

    // BugsplatMac only notices a crash during the viewer run *following* the
    // crash, so info[0].basename is "SecondLife.crash". The Bugsplat service
    // doesn't respect the MIME type when returning the log data to a browser,
    // so rename .crash to _log.txt for download usability.
    info[0].basename =
        std::filesystem::path(info[0].pathname).stem().string() + "_log.txt";
    infos("attachmentsForBugsplatStartupManager attaching log " + info[0].basename);

    NSMutableArray *attachments = [[NSMutableArray alloc] init];

    for (const BugSplatAttachmentInfo& attach : info)
    {
        NSString *nspathname = [NSString stringWithCString:attach.pathname.c_str()
                                                  encoding:NSUTF8StringEncoding];
        NSString *nsbasename = [NSString stringWithCString:attach.basename.c_str()
                                                  encoding:NSUTF8StringEncoding];
        NSString *nsmimetype = [NSString stringWithCString:attach.mimetype.c_str()
                                                  encoding:NSUTF8StringEncoding];
        NSData *nsdata = [NSData dataWithContentsOfFile:nspathname];

        BugSplatAttachment *attachment =
            [[BugSplatAttachment alloc] initWithFilename:nsbasename
                                          attachmentData:nsdata
                                             contentType:nsmimetype];

        [attachments addObject:attachment];
        infos("attachmentsForBugsplatStartupManager attaching " + attach.pathname);
    }

    return attachments;
}

@end

namespace
{
    // Strong-ref the delegate for the life of the process; BugsplatMac stores
    // it as a weak/assign reference.
    LLBugSplatDelegate* gBugSplatDelegate = nil;

    void initBugSplat()
    {
        // Engage BugSplat *before* LLAppViewer::init() so any crash during
        // initialization is captured. https://www.bugsplat.com/docs/platforms/os-x#initialization
        gBugSplatDelegate = [[LLBugSplatDelegate alloc] init];
        [[BugSplat shared] setDelegate:gBugSplatDelegate];
        [[BugSplat shared] setAutoSubmitCrashReport:YES];
        [[BugSplat shared] setPersistUserDetails:NO];
        [[BugSplat shared] setAskUserDetails:NO];
        [BugSplat shared].expirationTimeInterval = 0;
        [[BugSplat shared] start];
        infos("bugsplat setup");

        // BugsplatMac submits queued crash reports on a background thread, so
        // its delegate callbacks (attachmentsForBugSplat:, etc.) can land
        // *after* LLAppViewer::init() runs writeSystemInfo() and clobbers
        // static_debug_info.log with this run's data. Force the singleton to
        // read the previous run's file now, while the LLAppViewer constructor
        // has already populated mStaticDebugFileName via setDebugFileNames()
        // but the base init() hasn't yet overwritten the file contents.
        CrashMetadata_instance();
    }
}

#endif // LL_BUGSPLAT
#endif // LL_DARWIN

void check_vm_bloat()
{
#if LL_LINUX
    // watch our own VM and RSS sizes, warn if we bloated rapidly
    static const std::string STATS_FILE = "/proc/self/stat";
    FILE *fp = fopen(STATS_FILE.c_str(), "r");
    if (fp)
    {
        static long long last_vm_size = 0;
        static long long last_rss_size = 0;
        const long long significant_vm_difference = 250 * 1024*1024;
        const long long significant_rss_difference = 50 * 1024*1024;
        long long this_vm_size = 0;
        long long this_rss_size = 0;

        ssize_t res;
        size_t dummy;
        char *ptr = nullptr;
        for (int i=0; i<22; ++i) // parse past the values we don't want
        {
            res = getdelim(&ptr, &dummy, ' ', fp);
            if (-1 == res)
            {
                LL_WARNS() << "Unable to parse " << STATS_FILE << LL_ENDL;
                goto finally;
            }
            free(ptr);
            ptr = nullptr;
        }
        // 23rd space-delimited entry is vsize
        res = getdelim(&ptr, &dummy, ' ', fp);
        llassert(ptr);
        if (-1 == res)
        {
            LL_WARNS() << "Unable to parse " << STATS_FILE << LL_ENDL;
            goto finally;
        }
        this_vm_size = atoll(ptr);
        free(ptr);
        ptr = nullptr;
        // 24th space-delimited entry is RSS
        res = getdelim(&ptr, &dummy, ' ', fp);
        llassert(ptr);
        if (-1 == res)
        {
            LL_WARNS() << "Unable to parse " << STATS_FILE << LL_ENDL;
            goto finally;
        }
        this_rss_size = getpagesize() * atoll(ptr);
        free(ptr);
        ptr = nullptr;

        LL_INFOS() << "VM SIZE IS NOW " << (this_vm_size/(1024*1024)) << " MB, RSS SIZE IS NOW " << (this_rss_size/(1024*1024)) << " MB" << LL_ENDL;

        if (llabs(last_vm_size - this_vm_size) > significant_vm_difference)
        {
            if (this_vm_size > last_vm_size)
            {
                LL_WARNS() << "VM size grew by " << (this_vm_size - last_vm_size)/(1024*1024) << " MB in last frame" << LL_ENDL;
            }
            else
            {
                LL_INFOS() << "VM size shrank by " << (last_vm_size - this_vm_size)/(1024*1024) << " MB in last frame" << LL_ENDL;
            }
        }

        if (llabs(last_rss_size - this_rss_size) > significant_rss_difference)
        {
            if (this_rss_size > last_rss_size)
            {
                LL_WARNS() << "RSS size grew by " << (this_rss_size - last_rss_size)/(1024*1024) << " MB in last frame" << LL_ENDL;
            }
            else
            {
                LL_INFOS() << "RSS size shrank by " << (last_rss_size - this_rss_size)/(1024*1024) << " MB in last frame" << LL_ENDL;
            }
        }

        last_rss_size = this_rss_size;
        last_vm_size = this_vm_size;

finally:
        if (ptr)
        {
            free(ptr);
            ptr = nullptr;
        }
        fclose(fp);
    }
#endif // LL_LINUX
}

static void exceptionTerminateHandler()
{
    // reinstall default terminate() handler in case we re-terminate.
    if (gOldTerminateHandler) std::set_terminate(gOldTerminateHandler);
    // treat this like a regular viewer crash, with nice stacktrace etc.
    long *null_ptr;
    null_ptr = 0;
    *null_ptr = 0xDEADBEEF; //Force an exception that will trigger breakpad.
    // we've probably been killed-off before now, but...
    gOldTerminateHandler(); // call old terminate() handler
}

void sdl_logger(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    switch (priority)
    {
        case SDL_LOG_PRIORITY_TRACE:
        case SDL_LOG_PRIORITY_VERBOSE:
        case SDL_LOG_PRIORITY_DEBUG:
            LL_DEBUGS("SDL") << "log='" << message << "'" << LL_ENDL;
            break;
        case SDL_LOG_PRIORITY_INFO:
            LL_INFOS("SDL") << "log='" << message << "'" << LL_ENDL;
            break;
        case SDL_LOG_PRIORITY_WARN:
        case SDL_LOG_PRIORITY_ERROR:
        case SDL_LOG_PRIORITY_CRITICAL:
            LL_WARNS("SDL") << "log='" << message << "'" << LL_ENDL;
            break;
        case SDL_LOG_PRIORITY_INVALID:
        default:
            break;
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
#if LL_WINDOWS && LL_VELOPACK
    // Velopack MUST be initialized first - it may handle install/uninstall
    // commands and exit the process before we do anything else.
    if (!velopack_initialize())
    {
        // Velopack handled the invocation (install/uninstall hook); exit
        // cleanly without ever constructing the app. Mirrors WINMAIN's
        // `return 0` — SDL_AppQuit tolerates the null gViewerAppPtr.
        return SDL_APP_SUCCESS;
    }
#endif

    // Call Tracy first thing to have it allocate memory
    // https://github.com/wolfpld/tracy/issues/196
    LL_PROFILER_FRAME_END;
    LL_PROFILER_SET_THREAD_NAME("App");

    gSDLMainHandled = true;

    gArgC = argc;
    gArgV = argv;

#if LL_WINDOWS
    // Note: gIconResource (consumed by the native LLWindowWin32 window proc)
    // is not set here — SDL_RegisterApp sources the window-class icon from the
    // exe's embedded RT_GROUP_ICON (branded ll_icon.ico) automatically.
    //
    // LLAppViewerWin32::initParseCommandLine parses a single command-tail
    // string with no program name — the same shape WINMAIN's pCmdLine has —
    // so hand it the tail of GetCommandLineW rather than re-joining argv.
    gViewerAppPtr = new LLAppViewerWin32(ll_convert_wide_to_string(PathGetArgsW(GetCommandLineW())).c_str());
#else
    gViewerAppPtr = new LLAppViewerSDL();
#endif

    SDL_SetLogOutputFunction(&sdl_logger, nullptr);

    const int c_sdl_version = SDL_VERSION;
    LL_INFOS() << "Compiled against SDL "
    << SDL_VERSIONNUM_MAJOR(c_sdl_version) << "."
    << SDL_VERSIONNUM_MINOR(c_sdl_version) << "."
    << SDL_VERSIONNUM_MICRO(c_sdl_version) << LL_ENDL;
    const int r_sdl_version = SDL_GetVersion();
    LL_INFOS() << "Running with SDL "
    << SDL_VERSIONNUM_MAJOR(r_sdl_version) << "."
    << SDL_VERSIONNUM_MINOR(r_sdl_version) << "."
    << SDL_VERSIONNUM_MICRO(r_sdl_version) << LL_ENDL;

    // install unexpected exception handler
    gOldTerminateHandler = std::set_terminate(exceptionTerminateHandler);

#if LL_WINDOWS
    // Set a debug info flag to indicate if multiple instances are running.
    bool found_other_instance = !create_app_mutex();
    gDebugInfo["FoundOtherInstanceAtStartup"] = LLSD::Boolean(found_other_instance);
#else
    unsetenv( "LD_PRELOAD" ); // <FS:ND/> Get rid of any preloading, we do not want this to happen during startup of plugins.
#endif

    // This needs to be set as early as possible
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, LLVersionInfo::getInstance()->getChannel().c_str());
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING, LLVersionInfo::getInstance()->getVersion().c_str());
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING, "org.alchemyviewer.viewer");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_CREATOR_STRING, "Linden Research Inc");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_COPYRIGHT_STRING, "Copyright (c) Linden Research, Inc. 2025");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, "https://www.secondlife.com");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "game");

    bool ok = gViewerAppPtr->init();
    if(!ok)
    {
        LL_WARNS() << "Application init failed." << LL_ENDL;
        return SDL_APP_FAILURE;
    }

#if LL_WINDOWS
    // Override NVIDIA driver-profile settings as needed. Reads
    // gSavedSettings, so this must come after init().
    gNvApiSession = ll_nvapi_session_create();
#endif

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    // Run the application main loop
    if (!gViewerAppPtr->frame())
    {
#if LL_LINUX && LL_DBUS
        // Pump the session bus until we've nothing left to dispatch or have
        // passed 1/15th of a second pumping for this frame.
        if (gDBusConn)
        {
            static LLTimer pump_timer;
            pump_timer.reset();
            pump_timer.setTimerExpirySec(1.0f / 15.0f);
            dbus_connection_read_write(gDBusConn, 0); // non-blocking I/O
            while (dbus_connection_dispatch(gDBusConn) == DBUS_DISPATCH_DATA_REMAINS
                   && !pump_timer.hasExpired())
            {
            }
            dbus_connection_flush(gDBusConn); // push any reply out this frame
        }
#endif

        // hack - doesn't belong here - but this is just for debugging
        if (getenv("LL_DEBUG_BLOAT"))
        {
            check_vm_bloat();
        }

        return SDL_APP_CONTINUE;
    }

    if(LLApp::isError())
    {
        return SDL_APP_FAILURE;
    }

    return SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
#if LL_DARWIN
    if (event && event->type == SDL_EVENT_DROP_FILE && isSLURL(event->drop.data))
    {
        handleUrl(event->drop.data);
        return SDL_APP_CONTINUE;
    }
#endif
    return LLWindowSDL::handleEvents(*event);
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    // gViewerAppPtr is null when SDL_AppInit bailed before constructing the
    // app (e.g. the Windows velopack install-hook path).
    if (gViewerAppPtr && !LLApp::isError())
    {
        //
        // We don't want to do cleanup here if the error handler got called -
        // the assumption is that the error handler is responsible for doing
        // app cleanup if there was a problem.
        //
        gViewerAppPtr->cleanup();
    }

    delete gViewerAppPtr;
    gViewerAppPtr = nullptr;

#if LL_WINDOWS
    ll_nvapi_session_destroy(gNvApiSession);
    gNvApiSession = nullptr;
#endif
}

bool LLAppViewerSDL::init()
{
#if LL_DARWIN && defined(LL_BUGSPLAT)
    initBugSplat();
#endif

    bool success = LLAppViewer::init();

#if LL_DARWIN
    if (success)
    {
        // initSLURLHandler() is called by LLAppViewer::initApp() *before*
        // initWindow() runs SDL_InitSubSystem(SDL_INIT_VIDEO) — at that point
        // SDL hasn't installed its NSApplicationDelegate yet, so there's
        // nothing to wrap. By the time LLAppViewer::init() returns here,
        // initWindow() has run, SDL3's SDL3AppDelegate (SDL_cocoaevents.m)
        // owns NSApp.delegate, and we can capture+replace it.
        //
        // NSApp.delegate is a weak/assign reference, so we keep a strong
        // reference here for the app's lifetime.
        static LLSDLAppDelegate *sOurs = nil;
        @autoreleasepool
        {
            id<NSApplicationDelegate> sdlDelegate = [NSApp delegate];
            sOurs = [[LLSDLAppDelegate alloc] initWithSDLDelegate:sdlDelegate];
            [NSApp setDelegate:sOurs];
        }

        // From here on, handleUrl() will dispatch directly instead of buffering.
        gAppFullyInited = true;

        // Drain any URL that arrived during launch and was buffered before
        // the viewer was ready. Mirrors initViewer() in llappviewermacosx.cpp.
        if (!gHandleSLURL.empty())
        {
            std::string url;
            url.swap(gHandleSLURL);
            dispatchUrl(url);
        }
    }
#endif

#if LL_SEND_CRASH_REPORTS
    if (success)
    {
        LLAppViewer* pApp = LLAppViewer::instance();
        pApp->initCrashReporting();
    }
#endif

    return success;
}

bool LLAppViewerSDL::restoreErrorTrap()
{
#if LL_DARWIN
    // This method intends to reinstate signal handlers.
    // *NOTE:Mani It was found that the first execution of a shader was overriding
    // our initial signal handlers somehow.
    // This method will be called (at least) once per mainloop execution.
    // *NOTE:Mani The signals used below are copied over from the
    // setup_signals() func in LLApp.cpp
    // LLApp could use some way of overriding that func, but for this viewer
    // fix I opt to avoid affecting the server code.

    // Set up signal handlers that may result in program termination
    //
    struct sigaction act;
    struct sigaction old_act;
    act.sa_sigaction = default_unix_signal_handler;
    sigemptyset( &act.sa_mask );
    act.sa_flags = SA_SIGINFO;

    unsigned int reset_count = 0;

#define SET_SIG(SIGNAL) sigaction(SIGNAL, &act, &old_act); \
if(act.sa_sigaction != old_act.sa_sigaction) ++reset_count;
    // Synchronous signals
#   ifndef LL_BUGSPLAT
    SET_SIG(SIGABRT) // let bugsplat catch this
#   endif
    SET_SIG(SIGALRM)
    SET_SIG(SIGBUS)
    SET_SIG(SIGFPE)
    SET_SIG(SIGHUP)
    SET_SIG(SIGILL)
    SET_SIG(SIGPIPE)
    SET_SIG(SIGSEGV)
    SET_SIG(SIGSYS)

    SET_SIG(LL_HEARTBEAT_SIGNAL)
    SET_SIG(LL_SMACKDOWN_SIGNAL)

    // Asynchronous signals that are normally ignored
    SET_SIG(SIGCHLD)
    SET_SIG(SIGUSR2)

    // Asynchronous signals that result in attempted graceful exit
    SET_SIG(SIGHUP)
    SET_SIG(SIGTERM)
    SET_SIG(SIGINT)

    // Asynchronous signals that result in core
    SET_SIG(SIGQUIT)
#undef SET_SIG

    return reset_count == 0;
#else
    // *NOTE:Mani there is a case for implementing this on the mac.
    // Linux doesn't need it to my knowledge.
    // Windows has its own exception handling that doesn't use Unix signals at all.
    return true;
#endif
}

/////////////////////////////////////////
#if LL_LINUX && LL_DBUS

static void dispatchSLURL(const char* slurl)
{
    LL_INFOS() << "Was asked to go to slurl: " << slurl << LL_ENDL;

    std::string url = slurl;
    LLMediaCtrl* web = nullptr;
    const bool trusted_browser = false;
    LLURLDispatcher::dispatch(url, "", web, trusted_browser);
}

// Handles method calls delivered to VIEWERAPI_PATH. We only implement GoSLURL.
static DBusHandlerResult onBusMessage(DBusConnection* connection, DBusMessage* message, void* user_data)
{
    if (!dbus_message_is_method_call(message, VIEWERAPI_INTERFACE, "GoSLURL"))
    {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    DBusError err;
    dbus_error_init(&err);

    const char* slurl = nullptr;
    if (dbus_message_get_args(message, &err, DBUS_TYPE_STRING, &slurl, DBUS_TYPE_INVALID))
    {
        dispatchSLURL(slurl);

        // Reply so a blocking caller (sendURLToOtherInstance) unblocks promptly.
        if (!dbus_message_get_no_reply(message))
        {
            if (DBusMessage* reply = dbus_message_new_method_return(message))
            {
                dbus_connection_send(connection, reply, nullptr);
                dbus_message_unref(reply);
            }
        }
    }
    else
    {
        LL_WARNS() << "DBUS GoSLURL bad arguments: " << err.message << LL_ENDL;
    }

    dbus_error_free(&err);
    return DBUS_HANDLER_RESULT_HANDLED;
}

// unregister_function, message_function, then four reserved padding slots.
static const DBusObjectPathVTable sViewerApiVTable =
{
    nullptr,
    &onBusMessage,
    nullptr,
    nullptr,
    nullptr,
    nullptr
};

//virtual
bool LLAppViewerSDL::initSLURLHandler()
{
    DBusError err;
    dbus_error_init(&err);

    gDBusConn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!gDBusConn)
    {
        LL_WARNS() << "Cannot connect to session bus: " << err.message << LL_ENDL;
        dbus_error_free(&err);
        return false;
    }

    // We pump the connection ourselves from SDL_AppIterate, so don't let
    // libdbus terminate the process if the bus disconnects.
    dbus_connection_set_exit_on_disconnect(gDBusConn, false);

    if (!dbus_connection_register_object_path(gDBusConn, VIEWERAPI_PATH, &sViewerApiVTable, nullptr))
    {
        LL_WARNS() << "Failed to register dbus object " << VIEWERAPI_PATH << LL_ENDL;
        return false;
    }

    // Claim the well-known service name. DO_NOT_QUEUE so we never silently wait
    // behind an existing owner; the marker-file guard upstream ensures we only
    // reach here as the primary instance.
    dbus_bus_request_name(gDBusConn, VIEWERAPI_SERVICE, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err))
    {
        LL_WARNS() << "Failed to acquire dbus name " << VIEWERAPI_SERVICE << ": " << err.message << LL_ENDL;
        dbus_error_free(&err);
        return false;
    }

    return true;
}

//virtual
bool LLAppViewerSDL::sendURLToOtherInstance(const std::string& url)
{
    DBusError err;
    dbus_error_init(&err);

    DBusConnection* connection = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!connection)
    {
        LL_WARNS() << "Cannot connect to session bus: " << err.message << LL_ENDL;
        dbus_error_free(&err);
        return false;
    }

    DBusMessage* message = dbus_message_new_method_call(VIEWERAPI_SERVICE, VIEWERAPI_PATH,
                                                        VIEWERAPI_INTERFACE, "GoSLURL");
    if (!message)
    {
        LL_WARNS() << "Cannot create dbus method call." << LL_ENDL;
        return false;
    }

    const char* curl = url.c_str();
    dbus_message_append_args(message, DBUS_TYPE_STRING, &curl, DBUS_TYPE_INVALID);

    // Block for a reply. If no other instance owns the name this returns null
    // with an error set (ServiceUnknown) -> we are the primary instance ->
    // return false so startup continues normally.
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, 2000, &err);
    dbus_message_unref(message);

    const bool handed_off = (reply != nullptr);
    if (reply)
    {
        dbus_message_unref(reply);
    }
    else
    {
        LL_INFOS() << "No running instance to hand off SLURL to: " << err.message << LL_ENDL;
        dbus_error_free(&err);
    }

    return handed_off;
}

#elif LL_DARWIN
bool LLAppViewerSDL::initSLURLHandler()
{
    // The real Apple Event handler / delegate install happens at the tail of
    // LLAppViewerSDL::init(), once initWindow() has let SDL install its own
    // NSApplicationDelegate. Returning true reports that the viewer accepts
    // OS-delivered SLURLs on this platform.
    return true;
}
bool LLAppViewerSDL::sendURLToOtherInstance(const std::string& url)
{
    // macOS routes secondlife:// URLs to the running instance via Launch
    // Services / Apple Events automatically — no IPC needed from us.
    return false;
}
#elif LL_WINDOWS
bool LLAppViewerSDL::initSLURLHandler()
{
    return false; // not required on Windows
}
bool LLAppViewerSDL::sendURLToOtherInstance(const std::string& url)
{
    wchar_t window_class[256]; /* Flawfinder: ignore */ // Assume max length < 255 chars.
    mbstowcs(window_class, sWindowClass, 255);
    window_class[255] = 0;
    // Use the class instead of the window name.
    HWND other_window = FindWindow(window_class, NULL);

    if (other_window != NULL)
    {
        LL_DEBUGS() << "Found other window with the name '" << getWindowTitle() << "'" << LL_ENDL;
        COPYDATASTRUCT cds;
        const S32      SLURL_MESSAGE_TYPE = 0;
        cds.dwData                        = SLURL_MESSAGE_TYPE;
        cds.cbData                        = static_cast<DWORD>(url.length()) + 1;
        cds.lpData                        = (void*)url.c_str();

        LRESULT msg_result = SendMessage(other_window, WM_COPYDATA, NULL, (LPARAM)&cds);
        LL_DEBUGS() << "SendMessage(WM_COPYDATA) to other window '" << getWindowTitle() << "' returned " << msg_result << LL_ENDL;
        return true;
    }
    return false;
}
#else // LL_DBUS
bool LLAppViewerSDL::initSLURLHandler()
{
    return false; // not implemented
}
bool LLAppViewerSDL::sendURLToOtherInstance(const std::string& url)
{
    return false; // not implemented
}
#endif // LL_DBUS

void LLAppViewerSDL::initCrashReporting(bool reportFreeze)
{
}

bool LLAppViewerSDL::beingDebugged()
{
#if LL_WINDOWS
    return IsDebuggerPresent() != 0;
#elif LL_DARWIN
    int                 junk;
    int                 mib[4];
    struct kinfo_proc   info;
    size_t              size;

    // Initialize the flags so that, if sysctl fails for some bizarre
    // reason, we get a predictable result.

    info.kp_proc.p_flag = 0;

    // Initialize mib, which tells sysctl the info we want, in this case
    // we're looking for information about a specific process ID.

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = getpid();

    // Call sysctl.

    size = sizeof(info);
    junk = sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
    assert(junk == 0);

    // We're being debugged if the P_TRACED flag is set.

    return ( (info.kp_proc.p_flag & P_TRACED) != 0 );
#elif LL_LINUX
    static enum {unknown, no, yes} debugged = unknown;

    if (debugged == unknown)
    {
        pid_t ppid = getppid();
        char *name;
        int ret;

        ret = asprintf(&name, "/proc/%d/exe", ppid);
        if (ret != -1)
        {
            char buf[1024];
            ssize_t n;

            n = readlink(name, buf, sizeof(buf) - 1);
            if (n != -1)
            {
                char *base = strrchr(buf, '/');
                buf[n + 1] = '\0';
                if (base == NULL)
                {
                    base = buf;
                } else {
                    base += 1;
                }

                if (strcmp(base, "gdb") == 0 || strcmp(base, "lldb") == 0)
                {
                    debugged = yes;
                }
            }
            free(name);
        }
    }

    return debugged == yes;
#else
    return false;
#endif
}

#if LL_DARWIN
void LLAppViewerSDL::forceErrorOSSpecificException()
{
    NSException *exception = [NSException exceptionWithName:@"Forced NSException" reason:nullptr userInfo:nullptr];
    @throw exception;
}
#endif

bool LLAppViewerSDL::initParseCommandLine(LLCommandLineParser& clp)
{
    if (!clp.parseCommandLine(gArgC, gArgV))
    {
        return false;
    }

    int count = 0;
    // Returns a NULL-terminated array of locale pointers
    SDL_Locale **locales = SDL_GetPreferredLocales(&count);

    if (locales == nullptr)
    {
        LL_WARNS() << "Could not retrieve locales: " << SDL_GetError() << LL_ENDL;
    }
    else
    {
        for (int i = 0; locales[i] != nullptr; ++i)
        {
            const char *lang = locales[i]->language; // e.g., "en"
            const char *country = locales[i]->country; // e.g., "US" or NULL

            LL_INFOS("AppInit") << "Language " << ll_safe_string(lang) << LL_ENDL;
            LL_INFOS("AppInit") << "Location " << ll_safe_string(country) << LL_ENDL;

            LLControlVariable* c = gSavedSettings.getControl("SystemLanguage");
            if(c)
            {
                // SystemLanguage expects bare ISO 639-1 codes ("en", "de"); strip
                // any region/script suffix SDL may emit ("en-us", "zh-Hans-CN").
                std::string lang_str = ll_safe_string(lang);
                const auto sep = lang_str.find_first_of("-_");
                if (sep != std::string::npos)
                {
                    lang_str.erase(sep);
                }
                c->setValue(lang_str, false);
            }
            break;
        }
        // Remember to free the returned memory
        SDL_free(locales);
    }

    return true;
}

std::string LLAppViewerSDL::generateSerialNumber()
{
    char serial_md5[MD5HEX_STR_SIZE];       // Flawfinder: ignore
    serial_md5[0] = 0;
#if LL_WINDOWS
    DWORD serial  = 0;
    DWORD flags   = 0;
    BOOL  success = GetVolumeInformation(L"C:\\",
                                         NULL,    // volume name buffer
                                         0,       // volume name buffer size
                                         &serial, // volume serial
                                         NULL,    // max component length
                                         &flags,  // file system flags
                                         NULL,    // file system name buffer
                                         0);      // file system name buffer size
    if (success)
    {
        LLMD5 md5;
        md5.update((unsigned char*)&serial, sizeof(DWORD));
        md5.finalize();
        md5.hex_digest(serial_md5);
    }
    else
    {
        LL_WARNS() << "GetVolumeInformation failed" << LL_ENDL;
    }
    return serial_md5;
#elif LL_DARWIN
    // JC: Sample code from http://developer.apple.com/technotes/tn/tn1103.html
    CFStringRef serialNumber = NULL;
    io_service_t    platformExpert = IOServiceGetMatchingService(kIOMainPortDefault,
                                                                 IOServiceMatching("IOPlatformExpertDevice"));
    if (platformExpert)
    {
        serialNumber = (CFStringRef) IORegistryEntryCreateCFProperty(platformExpert,
                                                                     CFSTR(kIOPlatformSerialNumberKey),
                                                                     kCFAllocatorDefault, 0);
        IOObjectRelease(platformExpert);
    }

    if (serialNumber)
    {
        char buffer[MAX_STRING];        // Flawfinder: ignore
        if (CFStringGetCString(serialNumber, buffer, MAX_STRING, kCFStringEncodingASCII))
        {
            LLMD5 md5( (unsigned char*)buffer );
            md5.hex_digest(serial_md5);
        }
        CFRelease(serialNumber);
    }
#elif LL_LINUX
    std::string best;
    std::string uuiddir("/dev/disk/by-uuid/");

    // trawl /dev/disk/by-uuid looking for a good-looking UUID to grab
    std::string this_name;

    LLDirIterator iter(uuiddir, "*");
    while (iter.next(this_name))
    {
        if (this_name.length() > best.length() ||
            (this_name.length() == best.length() &&
             this_name > best))
        {
            // longest (and secondarily alphabetically last) so far
            best = this_name;
        }
    }

    // we don't return the actual serial number, just a hash of it.
    LLMD5 md5( reinterpret_cast<const unsigned char*>(best.c_str()) );
    md5.hex_digest(serial_md5);
#endif
    return serial_md5;
}
