/**
 * @file llappviewerwin32.cpp
 * @brief The LLAppViewerWin32 class definitions
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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

#include "llappviewerwin32.h"

#if !LL_SDL_WINDOW
#include "llwindowwin32.h" // for gIconResource, set in the native WINMAIN below
#endif

#include "res/resource.h" // *FIX: for setting gIconResource.

#include <WERAPI.H>     // for WerAddExcludedApplication()

#include "llviewercontrol.h"
#include "lldxhardware.h"

#include "nvapi/nvapi.h"
#include "nvapi/NvApiDriverSettings.h"

#include <stdlib.h>

#include "llmd5.h"
#include "llfindlocale.h"

#include "llcommandlineparser.h"

#ifndef LL_RELEASE_FOR_DOWNLOAD
#include "llwindebug.h"
#endif

#include "lldir.h"

#include <exception>

// Velopack installer and update framework
#if LL_VELOPACK
#include "llvelopack.h"
#endif
#include "llversioninfovars.h"

extern bool gGPUBenchmarkMode;

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

/*
    This function is used to print to the command line a text message
    describing the nvapi error and quits
*/
void nvapi_error(NvAPI_Status status)
{
    NvAPI_ShortString szDesc = {0};
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

    NvAPI_UnicodeString profile_name;
    std::wstring w_app_name = TEXT("Alchemy Viewer");
    wsprintf(reinterpret_cast<wchar_t*>(profile_name), L"%s", w_app_name.c_str());
    NvDRSProfileHandle hProfile = 0;
    // (3) Check if we already have an application profile for the viewer
    status = NvAPI_DRS_FindProfileByName(hSession, profile_name, &hProfile);
    if (status != NVAPI_OK && status != NVAPI_PROFILE_NOT_FOUND)
    {
        nvapi_error(status);
        return;
    }
    else if (status == NVAPI_PROFILE_NOT_FOUND)
    {
        // Don't have an application profile yet - create one
        LL_INFOS() << "Creating Alchemy Viewer profile for NVIDIA driver" << LL_ENDL;

        NVDRS_PROFILE profileInfo;
        profileInfo.version = NVDRS_PROFILE_VER;
        profileInfo.isPredefined = 0;
        wsprintf(reinterpret_cast<wchar_t*>(profileInfo.profileName), L"%s", w_app_name.c_str());

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

    // (4) Check if current exe is part of the profile
    std::string exe_name = gDirUtilp->getExecutableFilename();
    NVDRS_APPLICATION profile_application = {};
    profile_application.version = NVDRS_APPLICATION_VER;

    std::wstring w_exe_name = ll_convert<std::wstring>(exe_name);
    NvAPI_UnicodeString profile_app_name;
    wsprintf(reinterpret_cast<wchar_t*>(profile_app_name), L"%s", w_exe_name.c_str());

    status = NvAPI_DRS_GetApplicationInfo(hSession, hProfile, profile_app_name, &profile_application);
    if (status != NVAPI_OK && status != NVAPI_EXECUTABLE_NOT_FOUND)
    {
        nvapi_error(status);
        return;
    }
    else if (status == NVAPI_EXECUTABLE_NOT_FOUND)
    {
        LL_INFOS() << "Creating application for " << exe_name << " for NVIDIA application profile" << LL_ENDL;

        // Add this exe to the profile
        NVDRS_APPLICATION application = {};
        application.version = NVDRS_APPLICATION_VER;
        application.isPredefined = 0;
        wsprintf(reinterpret_cast<wchar_t*>(application.appName), L"%s", w_exe_name.c_str());
        wsprintf(reinterpret_cast<wchar_t*>(application.launcher), L"%s", w_exe_name.c_str());
        wsprintf(reinterpret_cast<wchar_t*>(application.userFriendlyName), L"%s", w_app_name.c_str());
        wsprintf(reinterpret_cast<wchar_t*>(application.fileInFolder), L"%s", "");

        status = NvAPI_DRS_CreateApplication(hSession, hProfile, &application);
        if (status != NVAPI_OK)
        {
            nvapi_error(status);
            return;
        }

        // Save application in case we added one
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

void* ll_nvapi_session_create()
{
    NvDRSSessionHandle hSession = 0;
    static LLCachedControl<bool> use_nv_api(gSavedSettings, "NvAPICreateApplicationProfile", true);
    if (use_nv_api)
    {
        NvAPI_Status status;

        // Initialize NVAPI
        status = NvAPI_Initialize();

        if (status == NVAPI_OK)
        {
            // Create the session handle to access driver settings
            status = NvAPI_DRS_CreateSession(&hSession);
            if (status != NVAPI_OK)
            {
                nvapi_error(status);
                hSession = 0;
            }
            else
            {
                //override driver setting as needed
                ll_nvapi_init(hSession);
            }
        }
    }
    return hSession;
}

void ll_nvapi_session_destroy(void* session)
{
    // (NVAPI) (6) We clean up. This is analogous to doing a free()
    if (session)
    {
        NvAPI_DRS_DestroySession(reinterpret_cast<NvDRSSessionHandle>(session));
    }
}

// When USE_SDL_WINDOW is enabled on Windows the exe entry point is SDL's
// generated wWinMain in llappviewersdl.cpp (SDL_MAIN_USE_CALLBACKS), which
// drives LLAppViewerWin32 through the SDL_App* callbacks instead.
#if !LL_SDL_WINDOW

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
#if LL_VELOPACK
    // Velopack MUST be initialized first - it may handle install/uninstall
    // commands and exit the process before we do anything else.
    if (!velopack_initialize())
    {
        // Obsolete? Always return true
        // Velopack handled the invocation (install/uninstall hook)
        return 0;
    }
#endif

    // Call Tracy first thing to have it allocate memory
    // https://github.com/wolfpld/tracy/issues/196
    LL_PROFILER_FRAME_END;
    LL_PROFILER_SET_THREAD_NAME("App");

    const S32 MAX_HEAPS = 255;
    DWORD heap_enable_lfh_error[MAX_HEAPS];
    S32 num_heaps = 0;

#if WINDOWS_CRT_MEM_CHECKS
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

    // *FIX: global
    gIconResource = MAKEINTRESOURCE(IDI_LL_ICON);

    // Benchmark subprocess mode before full init for LLFeatureManager::loadGPUClass().
    {
        std::wstring cmdLineStr(pCmdLine ? pCmdLine : L"");
        if (cmdLineStr.find(L"--gpubenchmark") != std::wstring::npos)
        {
            gGPUBenchmarkMode = true;
        }
    }

    LLAppViewerWin32* viewer_app_ptr = new LLAppViewerWin32(ll_convert_wide_to_string(pCmdLine).c_str());

    gOldTerminateHandler = std::set_terminate(exceptionTerminateHandler);

    // Set a debug info flag to indicate if multiple instances are running.
    bool found_other_instance = gGPUBenchmarkMode || !create_app_mutex();
    gDebugInfo["FoundOtherInstanceAtStartup"] = LLSD::Boolean(found_other_instance);

    bool ok = viewer_app_ptr->init();
    if (!ok)
    {
        LL_WARNS() << "Application init failed." << LL_ENDL;
        return -1;
    }

    NvDRSSessionHandle hSession = reinterpret_cast<NvDRSSessionHandle>(ll_nvapi_session_create());

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

    ll_nvapi_session_destroy(hSession);
    hSession = 0;

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

#endif // !LL_SDL_WINDOW

void LLAppViewerWin32::setWinErrorReportingExcluded(bool excluded)
{
    std::string executable_name = gDirUtilp->getExecutableFilename();
    std::wstring wide_name = ll_convert<std::wstring>(executable_name);

    const char* call = excluded ? "WerAddExcludedApplication" : "WerRemoveExcludedApplication";
    HRESULT result = excluded ? WerAddExcludedApplication(wide_name.c_str(), FALSE)
                              : WerRemoveExcludedApplication(wide_name.c_str(), FALSE);
    if (S_OK == result)
    {
        LL_INFOS() << call << "() succeeded for " << executable_name << LL_ENDL;
    }
    else
    {
        // Removing an entry that is not there is the usual way here.
        LL_INFOS() << call << "() returned 0x" << std::hex << result << std::dec
                   << " for " << executable_name << LL_ENDL;
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

#ifndef LL_RELEASE_FOR_DOWNLOAD
    // Merely requesting the LLSingleton instance initializes it.
    LLWinDebug::instance();
#endif

    bool success = LLAppViewer::init();

#if ! AL_SENTRY
    // Without a crash reporter, Windows Error Reporting is kept away from the
    // viewer's crashes; with one, the reporter decides, since WER is how its
    // handler receives fast-fail crashes.
    setWinErrorReportingExcluded(true);
#endif

    return success;
}

bool LLAppViewerWin32::cleanup()
{
    bool result = LLAppViewer::cleanup();

    gDXHardware.cleanup();
    cleanupConsole();

    return result;
}

bool LLAppViewerWin32::initWindow()
{
    // This is a workaround/hotfix for a change in Windows 11 24H2 (and possibly later)
    // Where the window width and height need to correctly reflect an available FullScreen size
    if (gSavedSettings.getBOOL("FullScreen"))
    {
        DEVMODE dev_mode;
        ::ZeroMemory(&dev_mode, sizeof(DEVMODE));
        dev_mode.dmSize = sizeof(DEVMODE);
        if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dev_mode))
        {
            gSavedSettings.setU32("WindowWidth", dev_mode.dmPelsWidth);
            gSavedSettings.setU32("WindowHeight", dev_mode.dmPelsHeight);
        }
        else
        {
            LL_WARNS("AppInit") << "Unable to set WindowWidth and WindowHeight for FullScreen mode" << LL_ENDL;
        }
    }

    return LLAppViewer::initWindow();
}

void LLAppViewerWin32::initLoggingAndGetLastDuration()
{
    LLAppViewer::initLoggingAndGetLastDuration();
}

void LLAppViewerWin32::initConsole()
{
    // pop up debug console
    if (!gGPUBenchmarkMode)
    {
        mIsConsoleAllocated = create_console();
    }
    return LLAppViewer::initConsole();
}

void LLAppViewerWin32::cleanupConsole()
{
    if (mIsConsoleAllocated)
    {
        FreeConsole();
        mIsConsoleAllocated = false;
    }
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
    if (!restoreErrorTrap())
    {
        LL_WARNS("AppInit") << " Someone took over my exception handler!" << LL_ENDL;
    }

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

//virtual
bool LLAppViewerWin32::sendURLToOtherInstance(const std::string& url)
{
    wchar_t window_class[256]; /* Flawfinder: ignore */   // Assume max length < 255 chars.
    mbstowcs(window_class, sWindowClass, 255);
    window_class[255] = 0;
    // Use the class instead of the window name.
    HWND other_window = FindWindow(window_class, NULL);

    if (other_window != NULL)
    {
        LL_DEBUGS("AppInit") << "Found other window with the name '" << getWindowTitle() << "'" << LL_ENDL;
        COPYDATASTRUCT cds;
        const S32 SLURL_MESSAGE_TYPE = 0;
        cds.dwData = SLURL_MESSAGE_TYPE;
        cds.cbData = static_cast<DWORD>(url.length()) + 1;
        cds.lpData = (void*)url.c_str();

        LRESULT msg_result = SendMessage(other_window, WM_COPYDATA, NULL, (LPARAM)&cds);
        LL_DEBUGS("AppInit") << "SendMessage(WM_COPYDATA) to other window '"
                 << getWindowTitle() << "' returned " << msg_result << LL_ENDL;
        return true;
    }
    return false;
}

void LLAppViewerWin32::setOSHibernationMode(eHibernationMode mode)
{
    // ES_CONTINUOUS tells Windows to reset the idle timer
    // and restore normal operation
    // ES_SYSTEM_REQUIRED prevents system sleep/hibernation
    // ES_DISPLAY_REQUIRED prevents display sleep

    if (mode == LL_HIBERNATE_MODE_DEFAULT)
    {
        // Allow OS to hibernate - clear the previous execution state flags
        // ES_CONTINUOUS without other flags allows the system to idle normally
        SetThreadExecutionState(ES_CONTINUOUS);
        LL_INFOS("OS") << "Permitted OS hibernation/sleep" << LL_ENDL;
    }
    else if (mode == LL_HIBERNATE_MODE_PREVENT)
    {
        // Prevent OS from hibernating while viewer is running
        // ES_CONTINUOUS | ES_SYSTEM_REQUIRED keeps the system awake
        EXECUTION_STATE result = SetThreadExecutionState(
            ES_CONTINUOUS | ES_SYSTEM_REQUIRED
        );
        if (result == NULL)
        {
            LL_WARNS("OS") << "Failed to prevent OS hibernation, error: " << GetLastError() << LL_ENDL;
        }
        else
        {
            LL_INFOS("OS") << "Prevented OS hibernation, but allowed display sleep" << LL_ENDL;
        }
    }
    else if (mode == LL_HIBERNATE_MODE_PREVENT_SCREEN)
    {
        // Prevent OS from hibernating or turning screen off while viewer is running
        // ES_CONTINUOUS | ES_SYSTEM_REQUIRED keeps the system awake
        // ES_DISPLAY_REQUIRED keeps the display on
        EXECUTION_STATE result = SetThreadExecutionState(
            ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
        );

        if (result == NULL)
        {
            LL_WARNS("OS") << "Failed to prevent OS hibernation and display sleep, error: " << GetLastError() << LL_ENDL;
        }
        else
        {
            LL_INFOS("OS") << "Prevented OS hibernation/sleep" << LL_ENDL;
        }
    }
}

bool LLAppViewerWin32::sendShutdownToOtherInstances(const std::wstring& install_dir)
{
    // Velopack installs viewer like this:
    // %appdata%\Local\ChannelNameViewer\Update.exe // which is our uninstaller
    // %appdata%\Local\ChannelNameViewer\SecondLifeViewer.exe // wrapper, redirects to main executable
    // %appdata%\Local\ChannelNameViewer\current\SecondLifeViewer.exe // main executable
    // For reliability don't expect install_dir to be actually in the base path, strip 'current'

    const std::wstring current_suffix = L"\\current";
    std::wstring normalized_path(install_dir);
    if (normalized_path.length() >= current_suffix.length() &&
        _wcsicmp(normalized_path.c_str() + normalized_path.length() - current_suffix.length(),
            current_suffix.c_str()) == 0)
    {
        normalized_path.resize(normalized_path.length() - current_suffix.length());
    }

    wchar_t window_class[256]; // Assume max length < 255 chars.
    mbstowcs(window_class, sWindowClass, 255);
    window_class[255] = 0;

    // Normalize the directory path
    wchar_t our_dir_normalized[MAX_PATH];
    wchar_t* file_part = nullptr;
    DWORD result = GetFullPathNameW(normalized_path.c_str(), MAX_PATH, our_dir_normalized, &file_part);
    if (result == 0 || result >= MAX_PATH)
    {
        LL_WARNS() << "Failed to normalize our executable path" << LL_ENDL;
        return false;
    }

    // Remove trailing backslash if present
    size_t dir_len = wcslen(our_dir_normalized);
    if (dir_len > 0 && our_dir_normalized[dir_len - 1] == L'\\')
    {
        our_dir_normalized[dir_len - 1] = L'\0';
        dir_len--;
    }

    // This message is meant for velopack, so we don't expect to have
    // a window of our own, store any matching windows.
    struct EnumData
    {
        const wchar_t* target_class;
        const wchar_t* our_dir_normalized;
        std::vector<HWND> found_windows;
    };

    EnumData enum_data;
    enum_data.target_class = window_class;
    enum_data.our_dir_normalized = our_dir_normalized;

    // Callback function to find all matching windows
    auto find_windows_callback = [](HWND hwnd, LPARAM lParam) -> BOOL
    {
        EnumData* data = reinterpret_cast<EnumData*>(lParam);
        wchar_t class_name[256];

        if (GetClassName(hwnd, class_name, 256) > 0)
        {
            if (wcscmp(class_name, data->target_class) == 0)
            {
                // Get the process ID for this window
                DWORD process_id = 0;
                GetWindowThreadProcessId(hwnd, &process_id);

                // Open the process to query its executable path
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
                if (hProcess)
                {
                    wchar_t exe_path[MAX_PATH];
                    DWORD size = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProcess, 0, exe_path, &size))
                    {
                        // Normalize the other process's path
                        wchar_t other_dir_normalized[MAX_PATH];
                        wchar_t* other_file_part = nullptr;
                        DWORD result = GetFullPathNameW(exe_path, MAX_PATH, other_dir_normalized, &other_file_part);

                        if (result > 0 && result < MAX_PATH)
                        {
                            // Remove the filename part to get just the directory
                            // We are doing this to avoid incidents, like having
                            // multiple viewer version exes in the same folder.
                            if (other_file_part)
                            {
                                *other_file_part = L'\0';
                            }

                            // Remove trailing backslash if present
                            size_t other_dir_len = wcslen(other_dir_normalized);
                            if (other_dir_len > 0 && other_dir_normalized[other_dir_len - 1] == L'\\')
                            {
                                other_dir_normalized[other_dir_len - 1] = L'\0';
                                other_dir_len--;
                            }

                            // Strip "\current" suffix if present to normalize comparison
                            // This handles both release (with \current) and debug builds (without)
                            const std::wstring current_suffix = L"\\current";
                            if (other_dir_len >= current_suffix.length())
                            {
                                size_t offset = other_dir_len - current_suffix.length();
                                if (_wcsicmp(other_dir_normalized + offset, current_suffix.c_str()) == 0)
                                {
                                    other_dir_normalized[offset] = L'\0';
                                }
                            }

                            // Compare directories (case-insensitive)
                            if (_wcsicmp(other_dir_normalized, data->our_dir_normalized) == 0)
                            {
                                data->found_windows.push_back(hwnd);
                            }
                        }
                    }
                    CloseHandle(hProcess);
                }
            }
        }

        return TRUE; // Continue enumeration
    };

    // Find all matching windows and send shutdown messages
    EnumWindows(find_windows_callback, reinterpret_cast<LPARAM>(&enum_data));

    if (enum_data.found_windows.empty())
    {
        LL_DEBUGS("AppInit") << "No other instances found" << LL_ENDL;
        return false;
    }

    LL_INFOS("AppInit") << "Found " << (S32)(enum_data.found_windows.size()) << " other instance(s), sending shutdown messages" << LL_ENDL;

    // Get our own process ID to include in the message
    DWORD our_process_id = GetCurrentProcessId();

    constexpr UINT timeout_ms = 2000; // 2s. Viewer's message thread is supposed to be fast.
    for (HWND other_window : enum_data.found_windows)
    {
        if (IsWindow(other_window))
        {
            DWORD_PTR result = 0;
            LRESULT send_result = SendMessageTimeout(
                other_window,
                WM_POST_UNINSTALL_,
                static_cast<WPARAM>(our_process_id),
                static_cast<LPARAM>(WM_POST_UNINSTALL_MSG_SHUTDOWN),
                SMTO_ABORTIFHUNG | SMTO_BLOCK,
                timeout_ms,
                &result
            );

            if (send_result == 0)
            {
                DWORD error = GetLastError();
                if (error == ERROR_TIMEOUT)
                {
                    LL_WARNS("AppInit") << "Shutdown message timed out for window " << std::hex << other_window << std::dec << LL_ENDL;
                }
                else
                {
                    LL_WARNS("AppInit") << "Failed to send shutdown message to window " << std::hex << other_window
                        << ", error: " << error << std::dec << LL_ENDL;
                }

                PostMessage(other_window, WM_CLOSE, 0, 0);
            }
            else
            {
                LL_DEBUGS("AppInit") << "Shutdown message sent successfully to window " << std::hex << other_window << std::dec << LL_ENDL;
            }
        }
    }

    // Poll for up to 30 seconds, checking every 5 seconds
    const S32 MAX_WAIT_TIME_MS = 60000; // 30 seconds
    const S32 POLL_INTERVAL_MS = 5000;  // 5 seconds
    S32 elapsed_time_ms = 0;
    size_t still_open_count = enum_data.found_windows.size();

    while (elapsed_time_ms < MAX_WAIT_TIME_MS)
    {
        LL_INFOS("AppInit") << "Waiting for " << (S32)still_open_count << " instance(s) to close... ("
            << (S32)(elapsed_time_ms / 1000) << "s elapsed)" << LL_ENDL;

        ms_sleep(POLL_INTERVAL_MS);
        elapsed_time_ms += POLL_INTERVAL_MS;

        // Check if the specific windows we found still exist
        // Don't enumerate all windows for new ones, assume that
        // no instances were reused and assume user won't open
        // the app again. For now just check our list.
        still_open_count = 0;
        for (HWND hwnd : enum_data.found_windows)
        {
            if (IsWindow(hwnd))
            {
                still_open_count++;
            }
        }

        if (still_open_count == 0)
        {
            LL_INFOS("AppInit") << "All other instances have closed after " << (S32)(elapsed_time_ms / 1000) << " seconds" << LL_ENDL;
            return false;
        }
    }

    if (still_open_count != 0)
    {
        LL_WARNS("AppInit") << "Proceeding with uninstall with " << (S32)still_open_count << " instance(s) still open." << LL_ENDL;
    }

    return true;
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
