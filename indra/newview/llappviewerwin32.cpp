/**
 * @file llappviewerwin32.cpp
 * @brief The LLAppViewerWin32 class definitions
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

#ifdef INCLUDE_VLD
#include "vld.h"
#endif
#include "llwin32headerslean.h"

#include "llwindowwin32.h" // *FIX: for setting gIconResource.

#include "llappviewerwin32.h"

#include "llgl.h"
#include "res/resource.h" // *FIX: for setting gIconResource.

#include <fcntl.h>      //_O_APPEND
#include <io.h>         //_open_osfhandle()
#include <WERAPI.H>     // for WerAddExcludedApplication()
#include <process.h>    // _spawnl()
#include <tchar.h>      // For TCHAR support

#include "llviewercontrol.h"
#include "lldxhardware.h"

#ifdef LL_NVAPI
#include "nvapi/nvapi.h"
#include "nvapi/NvApiDriverSettings.h"
#endif

#include <stdlib.h>

#include "llweb.h"

#include "llviewernetwork.h"
#include "llmd5.h"
#include "llfindlocale.h"

#include "llcommandlineparser.h"
#include "lltrans.h"

#ifndef LL_RELEASE_FOR_DOWNLOAD
#include "llwindebug.h"
#endif

#include "stringize.h"
#include "lldir.h"
#include "llerrorcontrol.h"

#include <fstream>
#include <exception>

// Sentry (https://sentry.io) crash reporting tool
#if defined(AL_SENTRY)
#include <sentry.h>
#endif

namespace
{
    void (*gOldTerminateHandler)() = NULL;
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

LONG WINAPI catchallCrashHandler(EXCEPTION_POINTERS * /*ExceptionInfo*/)
{
    LL_WARNS() << "Hit last ditch-effort attempt to catch crash." << LL_ENDL;
    exceptionTerminateHandler();
    return 0;
}
#ifdef LL_DEBUG
#ifdef LL_MSVC7
extern "C" {
    void _wassert(const wchar_t * _Message, const wchar_t *_File, unsigned _Line)
    {
        LL_ERRS() << _Message << LL_ENDL;
    }
}
#endif
#endif

const std::string LLAppViewerWin32::sWindowClass = "Alchemy";

// Create app mutex creates a unique global windows object.
// If the object can be created it returns true, otherwise
// it returns false. The false result can be used to determine
// if another instance of a second life app (this vers. or later)
// is running.
// *NOTE: Do not use this method to run a single instance of the app.
// This is intended to help debug problems with the cross-platform
// locked file method used for that purpose.
bool create_app_mutex()
{
    bool result = true;
    LPCWSTR unique_mutex_name = L"AlchemyAppMutex";
    HANDLE hMutex;
    hMutex = CreateMutex(NULL, TRUE, unique_mutex_name);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        result = false;
    }
    return result;
}

#ifdef LL_NVAPI

#define ALWSTR_SIZE(inwstr) ((inwstr.size() + 1) * sizeof(wchar_t))

static std::wstring NVAPI_APPNAME = TEXT("Alchemy Viewer");

/*
    This function is used to print to the command line a text message
    describing the nvapi error and quits
*/
void nvapi_error(NvAPI_Status status)
{
    NvAPI_ShortString szDesc = { 0 };
    NvAPI_GetErrorMessage(status, szDesc);
    LL_WARNS() << "nvapi error: " << szDesc << LL_ENDL;

    //should always trigger when asserts are enabled
    //llassert(status == NVAPI_OK);
}

void ll_nvapi_init(NvDRSSessionHandle hSession)
{
    // (2) load all the system settings into the session
    NvAPI_Status status = NvAPI_DRS_LoadSettings(hSession);
    if (status != NVAPI_OK)
    {
        nvapi_error(status);
        return;
    }

    NvAPI_UnicodeString profile_name = {};
    memcpy_s(profile_name, sizeof(profile_name), NVAPI_APPNAME.c_str(), ALWSTR_SIZE(NVAPI_APPNAME));

    NvDRSProfileHandle hProfile = 0;
    // Check if we already have a Alchmey Viewer profile
    status = NvAPI_DRS_FindProfileByName(hSession, profile_name, &hProfile);
    if (status != NVAPI_OK && status != NVAPI_PROFILE_NOT_FOUND)
    {
        nvapi_error(status);
        return;
    }
    else if (status == NVAPI_PROFILE_NOT_FOUND)
    {
        // Don't have a Alchemy Viewer profile yet - create one
        LL_INFOS() << "Creating Alchemy Viewer profile for NVIDIA driver" << LL_ENDL;

        NVDRS_PROFILE profileInfo;
        profileInfo.version = NVDRS_PROFILE_VER;
        profileInfo.isPredefined = 0;
        memcpy_s(profileInfo.profileName, sizeof(profileInfo.profileName), NVAPI_APPNAME.c_str(), ALWSTR_SIZE(NVAPI_APPNAME));

        status = NvAPI_DRS_CreateProfile(hSession, &profileInfo, &hProfile);
        if (status != NVAPI_OK)
        {
            nvapi_error(status);
            return;
        }

        // set the preferred power management mode
        {
            NVDRS_SETTING drsSetting = {};
            drsSetting.version = NVDRS_SETTING_VER;
            drsSetting.settingId = PREFERRED_PSTATE_ID;
            drsSetting.settingType = NVDRS_DWORD_TYPE;
            drsSetting.u32CurrentValue = PREFERRED_PSTATE_PREFER_MAX;
            status = NvAPI_DRS_SetSetting(hSession, hProfile, &drsSetting);
            if (status != NVAPI_OK)
            {
                nvapi_error(status);
                return;
            }
            LL_INFOS() << "Set preferred power management mode" << LL_ENDL;
        }

        // set the preferred opengl threading state
        {
            NVDRS_SETTING drsSetting = {};
            drsSetting.version = NVDRS_SETTING_VER;
            drsSetting.settingId = OGL_THREAD_CONTROL_ID;
            drsSetting.settingType = NVDRS_DWORD_TYPE;
            drsSetting.u32CurrentValue = OGL_THREAD_CONTROL_ENABLE;
            status = NvAPI_DRS_SetSetting(hSession, hProfile, &drsSetting);
            if (status != NVAPI_OK)
            {
                nvapi_error(status);
                return;
            }
            LL_INFOS() << "Set preferred GL Threading mode" << LL_ENDL;
        }

        // apply our changes to the system
        status = NvAPI_DRS_SaveSettings(hSession);
        if (status != NVAPI_OK)
        {
            nvapi_error(status);
            return;
        }
    }

    // Check if current exe is part of the profile
    std::string exe_name = gDirUtilp->getExecutableFilename();
    NVDRS_APPLICATION profile_application = {};
    profile_application.version = NVDRS_APPLICATION_VER;

    std::wstring w_exe_name = ll_convert_string_to_wide(exe_name);
    size_t w_exe_bytes = ALWSTR_SIZE(w_exe_name);
    NvAPI_UnicodeString profile_app_name = {};
    memcpy_s(profile_app_name, sizeof(profile_app_name), w_exe_name.c_str(), w_exe_bytes);

    status = NvAPI_DRS_GetApplicationInfo(hSession, hProfile, profile_app_name, &profile_application);
    if (status != NVAPI_OK && status != NVAPI_EXECUTABLE_NOT_FOUND)
    {
        nvapi_error(status);
        return;
    }
    else if (status == NVAPI_EXECUTABLE_NOT_FOUND)
    {
        LL_INFOS() << "Creating application for " << exe_name << " for NVIDIA driver" << LL_ENDL;

        // Add this exe to the profile
        NVDRS_APPLICATION application = {};
        application.version = NVDRS_APPLICATION_VER;
        application.isPredefined = 0;
        memcpy_s(application.appName, sizeof(application.appName), w_exe_name.c_str(), w_exe_bytes);
        memcpy_s(application.launcher, sizeof(application.launcher), w_exe_name.c_str(), w_exe_bytes);

        memcpy_s(application.userFriendlyName, sizeof(application.userFriendlyName), TEXT(LL_VIEWER_CHANNEL), sizeof(TEXT(LL_VIEWER_CHANNEL)));

        status = NvAPI_DRS_CreateApplication(hSession, hProfile, &application);
        if (status != NVAPI_OK)
        {
            nvapi_error(status);
            return;
        }

        // apply our changes to the system
        status = NvAPI_DRS_SaveSettings(hSession);
        if (status != NVAPI_OK)
        {
            nvapi_error(status);
            return;
        }
    }

    // load settings for querying
    status = NvAPI_DRS_LoadSettings(hSession);
    if (status != NVAPI_OK)
    {
        nvapi_error(status);
        return;
    }

    // apply our changes to the system
    status = NvAPI_DRS_SaveSettings(hSession);
    if (status != NVAPI_OK)
    {
        nvapi_error(status);
        return;
    }
}
#endif

//#define DEBUGGING_SEH_FILTER 1
#if DEBUGGING_SEH_FILTER
#   define WINMAIN DebuggingWinMain
#else
#   define WINMAIN wWinMain
#endif

int APIENTRY WINMAIN(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     PWSTR     pCmdLine,
                     int       nCmdShow)
{
    // Call Tracy first thing to have it allocate memory
    // https://github.com/wolfpld/tracy/issues/196
    LL_PROFILER_FRAME_END;
    LL_PROFILER_SET_THREAD_NAME("App");

    const S32 MAX_HEAPS = 255;
    DWORD heap_enable_lfh_error[MAX_HEAPS];
    S32 num_heaps = 0;

#if WINDOWS_CRT_MEM_CHECKS && !INCLUDE_VLD
    _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF ); // dump memory leaks on exit
#elif 0
    // Experimental - enable the low fragmentation heap
    // This results in a 2-3x improvement in opening a new Inventory window (which uses a large numebr of allocations)
    // Note: This won't work when running from the debugger unless the _NO_DEBUG_HEAP environment variable is set to 1

    // Enable to get mem debugging within visual studio.
#if LL_DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#else
    _CrtSetDbgFlag(0); // default, just making explicit

    ULONG ulEnableLFH = 2;
    HANDLE* hHeaps = new HANDLE[MAX_HEAPS];
    num_heaps = GetProcessHeaps(MAX_HEAPS, hHeaps);
    for(S32 i = 0; i < num_heaps; i++)
    {
        bool success = HeapSetInformation(hHeaps[i], HeapCompatibilityInformation, &ulEnableLFH, sizeof(ulEnableLFH));
        if (success)
            heap_enable_lfh_error[i] = 0;
        else
            heap_enable_lfh_error[i] = GetLastError();
    }
#endif
#endif

    LLAppViewerWin32* viewer_app_ptr = new LLAppViewerWin32(ll_convert_wide_to_string(pCmdLine).c_str());

    gOldTerminateHandler = std::set_terminate(exceptionTerminateHandler);

    // Set a debug info flag to indicate if multiple instances are running.
    bool found_other_instance = !create_app_mutex();
    gDebugInfo["FoundOtherInstanceAtStartup"] = LLSD::Boolean(found_other_instance);

    bool ok = viewer_app_ptr->init();
    if (!ok)
    {
        LL_WARNS() << "Application init failed." << LL_ENDL;
        return -1;
    }

#ifdef LL_NVAPI
    // Initialize NVAPI
    NvAPI_Status nvStatus = NvAPI_Initialize();
    NvDRSSessionHandle nvhSession = 0;

    if (nvStatus == NVAPI_OK)
    {
        // Create the session handle to access driver settings
        nvStatus = NvAPI_DRS_CreateSession(&nvhSession);
        if (nvStatus != NVAPI_OK)
        {
            nvapi_error(nvStatus);
        }
        else
        {
            //override driver setting as needed
            ll_nvapi_init(nvhSession);
        }
    }
#endif

    // Have to wait until after logging is initialized to display LFH info
    if (num_heaps > 0)
    {
        LL_INFOS() << "Attempted to enable LFH for " << num_heaps << " heaps." << LL_ENDL;
        for(S32 i = 0; i < num_heaps; i++)
        {
            if (heap_enable_lfh_error[i])
            {
                LL_INFOS() << "  Failed to enable LFH for heap: " << i << " Error: " << heap_enable_lfh_error[i] << LL_ENDL;
            }
        }
    }

    // Run the application main loop
    while (! viewer_app_ptr->frame())
    {}

    if (!LLApp::isError())
    {
        //
        // We don't want to do cleanup here if the error handler got called -
        // the assumption is that the error handler is responsible for doing
        // app cleanup if there was a problem.
        //
#if WINDOWS_CRT_MEM_CHECKS
        LL_INFOS() << "CRT Checking memory:" << LL_ENDL;
        if (!_CrtCheckMemory())
        {
            LL_WARNS() << "_CrtCheckMemory() failed at prior to cleanup!" << LL_ENDL;
        }
        else
        {
            LL_INFOS() << " No corruption detected." << LL_ENDL;
        }
#endif

        gGLActive = true;

        viewer_app_ptr->cleanup();

#if WINDOWS_CRT_MEM_CHECKS
        LL_INFOS() << "CRT Checking memory:" << LL_ENDL;
        if (!_CrtCheckMemory())
        {
            LL_WARNS() << "_CrtCheckMemory() failed after cleanup!" << LL_ENDL;
        }
        else
        {
            LL_INFOS() << " No corruption detected." << LL_ENDL;
        }
#endif

    }
    delete viewer_app_ptr;
    viewer_app_ptr = NULL;

#ifdef LL_NVAPI
    // (NVAPI) (6) We clean up. This is analogous to doing a free()
    if (nvhSession)
    {
        NvAPI_DRS_DestroySession(nvhSession);
        nvhSession = 0;
    }

    if (nvStatus == NVAPI_OK)
    {
        nvStatus = NvAPI_Unload();
        if (nvStatus != NVAPI_OK)
        {
            nvapi_error(nvStatus);
        }
    }

#endif

#if defined(AL_SENTRY)
    sentry_close();
#endif

    return 0;
}

#if DEBUGGING_SEH_FILTER
// The compiler doesn't like it when you use __try/__except blocks
// in a method that uses object destructors. Go figure.
// This winmain just calls the real winmain inside __try.
// The __except calls our exception filter function. For debugging purposes.
int APIENTRY wWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     PWSTR     lpCmdLine,
                     int       nCmdShow)
{
    __try
    {
        WINMAIN(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
    }
    __except( viewer_windows_exception_handler( GetExceptionInformation() ) )
    {
        _tprintf( _T("Exception handled.\n") );
    }
}
#endif

void LLAppViewerWin32::disableWinErrorReporting()
{
    std::string executable_name = gDirUtilp->getExecutableFilename();

    if( S_OK == WerRemoveExcludedApplication( ll_convert_string_to_wide(executable_name).c_str(), FALSE ) )
    {
        LL_INFOS() << "WerRemoveExcludedApplication() succeeded for " << executable_name << LL_ENDL;
    }
    else
    {
        LL_INFOS() << "WerRemoveExcludedApplication() failed for " << executable_name << LL_ENDL;
    }
}

const S32 MAX_CONSOLE_LINES = 7500;
// Only defined in newer SDKs than we currently use
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 4
#endif

namespace {

void set_stream(const char* desc, FILE* fp, DWORD handle_id, const char* name, const char* mode="w");

bool create_console()
{
    // allocate a console for this app
    const bool isConsoleAllocated = AllocConsole();

    if (isConsoleAllocated)
    {
        // set the screen buffer to be big enough to let us scroll text
        CONSOLE_SCREEN_BUFFER_INFO coninfo;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &coninfo);
        coninfo.dwSize.Y = MAX_CONSOLE_LINES;
        SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), coninfo.dwSize);

        // redirect unbuffered STDOUT to the console
        set_stream("stdout", stdout, STD_OUTPUT_HANDLE, "CONOUT$");
        // redirect unbuffered STDERR to the console
        set_stream("stderr", stderr, STD_ERROR_HANDLE, "CONOUT$");
        // redirect unbuffered STDIN to the console
        // Don't bother: our console is solely for log output. We never read stdin.
//      set_stream("stdin", stdin, STD_INPUT_HANDLE, "CONIN$", "r");
    }

    return isConsoleAllocated;
}

void set_stream(const char* desc, FILE* fp, DWORD handle_id, const char* name, const char* mode)
{
    // SL-13528: This code used to be based on
    // http://dslweb.nwnexus.com/~ast/dload/guicon.htm
    // (referenced in https://stackoverflow.com/a/191880).
    // But one of the comments on that StackOverflow answer points out that
    // assigning to *stdout or *stderr "probably doesn't even work with the
    // Universal CRT that was introduced in 2015," suggesting freopen_s()
    // instead. Code below is based on https://stackoverflow.com/a/55875595.
    auto std_handle = GetStdHandle(handle_id);
    if (std_handle == INVALID_HANDLE_VALUE)
    {
        LL_WARNS() << "create_console() failed to get " << desc << " handle" << LL_ENDL;
    }
    else
    {
        if (mode == std::string("w"))
        {
            // Enable color processing on Windows 10 console windows.
            DWORD dwMode = 0;
            GetConsoleMode(std_handle, &dwMode);
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(std_handle, dwMode);
        }
        // Redirect the passed fp to the console.
        FILE* ignore;
        if (freopen_s(&ignore, name, mode, fp) == 0)
        {
            // use unbuffered I/O
            setvbuf( fp, NULL, _IONBF, 0 );
        }
    }
}

} // anonymous namespace

LLAppViewerWin32::LLAppViewerWin32(const char* cmd_line) :
    mCmdLine(cmd_line),
    mIsConsoleAllocated(false)
{
}

LLAppViewerWin32::~LLAppViewerWin32()
{
}

bool LLAppViewerWin32::init()
{
    // Platform specific initialization.

    // Turn off Windows Error Reporting
    // (Don't send our data to Microsoft--at least until we are Logo approved and have a way
    // of getting the data back from them.)
    //
    // LL_INFOS() << "Turning off Windows error reporting." << LL_ENDL;
    disableWinErrorReporting();

#ifndef LL_RELEASE_FOR_DOWNLOAD
    // Merely requesting the LLSingleton instance initializes it.
    LLWinDebug::instance();
#endif

    LLAppViewer* pApp = LLAppViewer::instance();
    pApp->initCrashReporting();

    bool success = LLAppViewer::init();

    return success;
}

bool LLAppViewerWin32::cleanup()
{
    bool result = LLAppViewer::cleanup();

    gDXHardware.cleanup();

    if (mIsConsoleAllocated)
    {
        FreeConsole();
        mIsConsoleAllocated = false;
    }

    return result;
}

void LLAppViewerWin32::setCrashUserMetadata(const LLUUID& user_id, const std::string& avatar_name)
{
#if defined(AL_SENTRY)
    if (mSentryInitialized)
    {
        sentry_value_t user = sentry_value_new_object();
        sentry_value_set_by_key(user, "id", sentry_value_new_string(user_id.asString().c_str()));
        sentry_value_set_by_key(user, "username", sentry_value_new_string(avatar_name.c_str()));
        sentry_set_user(user);
    }
#endif
}

void LLAppViewerWin32::initLoggingAndGetLastDuration()
{
    LLAppViewer::initLoggingAndGetLastDuration();
}

void LLAppViewerWin32::initConsole()
{
    // pop up debug console
    mIsConsoleAllocated = create_console();
    return LLAppViewer::initConsole();
}

void write_debug_dx(const char* str)
{
    std::string value = gDebugInfo["DXInfo"].asString();
    value += str;
    gDebugInfo["DXInfo"] = value;
}

void write_debug_dx(const std::string& str)
{
    write_debug_dx(str.c_str());
}

bool LLAppViewerWin32::initHardwareTest()
{
    //
    // Do driver verification and initialization based on DirectX
    // hardware polling and driver versions
    //
    if (FALSE == gSavedSettings.getBOOL("NoHardwareProbe"))
    {
        LLSplashScreen::update(LLTrans::getString("StartupDetectingHardware"));

        LL_DEBUGS("AppInit") << "Attempting to poll DirectX for hardware info" << LL_ENDL;
        gDXHardware.setWriteDebugFunc(write_debug_dx);
        gDXHardware.updateVRAM();
        LL_DEBUGS("AppInit") << "Done polling DXGI for vram info" << LL_ENDL;

        // Disable so debugger can work
        std::string splash_msg;
        LLStringUtil::format_map_t args;
        args["[APP_NAME]"] = LLAppViewer::instance()->getSecondLifeTitle();
        splash_msg = LLTrans::getString("StartupLoading", args);

        LLSplashScreen::update(splash_msg);
    }

    if (!restoreErrorTrap())
    {
        LL_WARNS("AppInit") << " Someone took over my exception handler (post hardware probe)!" << LL_ENDL;
    }

    if (gGLManager.mVRAM == 0)
    {
        gGLManager.mVRAM = gDXHardware.getVRAM();
    }

    LL_INFOS("AppInit") << "Detected VRAM: " << gGLManager.mVRAM << LL_ENDL;

    return true;
}

bool LLAppViewerWin32::initParseCommandLine(LLCommandLineParser& clp)
{
    if (!clp.parseCommandLineString(mCmdLine))
    {
        return false;
    }

    // Find the system language.
    FL_Locale *locale = NULL;
    FL_Success success = FL_FindLocale(&locale, FL_MESSAGES);
    if (success != 0)
    {
        if (success >= 2 && locale->lang) // confident!
        {
            LL_INFOS("AppInit") << "Language: " << ll_safe_string(locale->lang) << LL_ENDL;
            LL_INFOS("AppInit") << "Location: " << ll_safe_string(locale->country) << LL_ENDL;
            LL_INFOS("AppInit") << "Variant: " << ll_safe_string(locale->variant) << LL_ENDL;
            LLControlVariable* c = gSavedSettings.getControl("SystemLanguage");
            if(c)
            {
                c->setValue(std::string(locale->lang), false);
            }
        }
    }
    FL_FreeLocale(&locale);

    return true;
}

bool LLAppViewerWin32::beingDebugged()
{
    return IsDebuggerPresent();
}

bool LLAppViewerWin32::restoreErrorTrap()
{
    return true; // we don't check for handler collisions on windows, so just say they're ok
}

void LLAppViewerWin32::initCrashReporting(bool reportFreeze)
{
#if defined(AL_SENTRY)
    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options, SENTRY_DSN);
    sentry_options_set_release(options, LL_VIEWER_CHANNEL_AND_VERSION);

    std::string crashpad_path = gDirUtilp->getExpandedFilename(LL_PATH_EXECUTABLE, "crashpad_handler.exe");
    sentry_options_set_handler_pathw(options, ll_convert_string_to_wide(crashpad_path).c_str());

    std::string database_path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "sentry");
    sentry_options_set_database_pathw(options, ll_convert_string_to_wide(database_path).c_str());

    std::string logfile_path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "Alchemy.log");
    sentry_options_add_attachmentw(options, ll_convert_string_to_wide(logfile_path).c_str());

    mSentryInitialized = (sentry_init(options) == 0);
    if (mSentryInitialized)
    {
        LL_INFOS() << "Successfully initialized Sentry" << LL_ENDL;
    }
    else
    {
        LL_WARNS() << "Failed to initialize Sentry" << LL_ENDL;
    }
#endif
}

//virtual
bool LLAppViewerWin32::sendURLToOtherInstance(const std::string& url)
{
    wchar_t window_class[256]; /* Flawfinder: ignore */   // Assume max length < 255 chars.
    mbstowcs(window_class, sWindowClass.c_str(), 255);
    window_class[255] = 0;
    // Use the class instead of the window name.
    HWND other_window = FindWindow(window_class, NULL);

    if (other_window != NULL)
    {
        LL_DEBUGS() << "Found other window with the name '" << getWindowTitle() << "'" << LL_ENDL;
        COPYDATASTRUCT cds;
        const S32 SLURL_MESSAGE_TYPE = 0;
        cds.dwData = SLURL_MESSAGE_TYPE;
        cds.cbData = static_cast<DWORD>(url.length()) + 1;
        cds.lpData = (void*)url.c_str();

        LRESULT msg_result = SendMessage(other_window, WM_COPYDATA, NULL, (LPARAM)&cds);
        LL_DEBUGS() << "SendMessage(WM_COPYDATA) to other window '"
                 << getWindowTitle() << "' returned " << msg_result << LL_ENDL;
        return true;
    }
    return false;
}


std::string LLAppViewerWin32::generateSerialNumber()
{
    char serial_md5[MD5HEX_STR_SIZE];       // Flawfinder: ignore
    serial_md5[0] = 0;

    DWORD serial = 0;
    DWORD flags = 0;
    BOOL success = GetVolumeInformation(
            L"C:\\",
            NULL,       // volume name buffer
            0,          // volume name buffer size
            &serial,    // volume serial
            NULL,       // max component length
            &flags,     // file system flags
            NULL,       // file system name buffer
            0);         // file system name buffer size
    if (success)
    {
        LLMD5 md5;
        md5.update( (unsigned char*)&serial, sizeof(DWORD));
        md5.finalize();
        md5.hex_digest(serial_md5);
    }
    else
    {
        LL_WARNS() << "GetVolumeInformation failed" << LL_ENDL;
    }
    return serial_md5;
}
