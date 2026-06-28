/**
 * @file slplugin_cef_bootstrap.cpp
 * @brief Windows CEF-sandbox bootstrap entry point for SLPluginCEF.
 *
 * On Windows the dedicated CEF host is built as SLPluginCEF.dll and loaded by
 * CEF's bootstrap executable (shipped renamed to SLPluginCEF.exe). The bootstrap
 * creates the Windows sandbox and calls our exported RunWinMain, handing us the
 * sandbox_info. We:
 *   1. run CEF's sub-process dispatch (CefExecuteProcess) so the SAME executable
 *      image services the renderer/GPU/utility sub-processes - which the
 *      Chromium sandbox requires (broker and targets must be one image);
 *   2. for the browser process, hand the sandbox_info to the dullahan runtime
 *      (so its CefInitialize runs sandboxed) and run the normal slplugin host
 *      message loop.
 *
 * The client DLL does NOT link cef_sandbox.lib (M138+ ships that only inside the
 * bootstrap executables), so there is no static-CRT requirement here.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "cef_app.h"
#include "cef_sandbox_win.h"   // RunWinMain prototype + CEF_BOOTSTRAP_EXPORT
#include "cef_version_info.h"

#include "dullahan.h"
#include "dullahan_runtime.h"

#include "llapr.h"
#include "llerrorcontrol.h"
#include "llstring.h"

// Defined in slplugin.cpp - the shared plugin<->parent host message loop.
int slplugin_run(U32 port);
// Defined in slplugin_daemon.cpp - the multi-tab daemon host loop.
int slplugin_daemon_run(U32 first_port, const std::string& rendezvous_path);

namespace
{
    int run_cef_host(HINSTANCE hInstance, LPTSTR lpCmdLine, void* sandbox_info)
    {
        // Developer escape: SLPLUGIN_CEF_NO_SANDBOX disables the sandbox (so the
        // child processes are attachable/debuggable) without rebuilding. Passing
        // a null sandbox_info makes dullahan_runtime fall back to no_sandbox plus
        // the dullahan_host helper.
        if (getenv("SLPLUGIN_CEF_NO_SANDBOX"))
        {
            sandbox_info = nullptr;
        }

        // CEF sub-process dispatch. For a renderer/GPU/utility sub-process this
        // blocks until it exits and returns its exit code; for the browser
        // process it returns -1 and we continue. The dullahan runtime is the
        // process CefApp.
        CefMainArgs main_args(hInstance);
        CefRefPtr<CefApp> app = &dullahan_runtime::instance();
        int exit_code = CefExecuteProcess(main_args, app, sandbox_info);
        if (exit_code >= 0)
        {
            return exit_code;
        }

        // Browser process: make the sandbox info available to the runtime's
        // CefInitialize, and tell dullahan that this host dispatches CEF
        // sub-processes itself (CEF re-launches this image -> RunWinMain ->
        // CefExecuteProcess), so it never uses the dullahan_host helper -
        // independent of whether the sandbox is active. Then run the host loop.
        dullahan::setSandboxInfo(sandbox_info);
        dullahan::setHostHandlesSubprocesses(true);

        ll_init_apr();
        {
            LLError::initForApplication(".", ".");
            LLError::setDefaultLevel(LLError::LEVEL_INFO);
        }

        // RunWinMain's lpCmdLine is LPTSTR (wide under UNICODE); narrow each char
        // explicitly (ASCII-safe, avoids the implicit-narrowing warning-as-error).
        // The command line is "<port>" for a single tab, or
        // "<port> --daemon <rendezvous-path>" to run as the shared multi-tab
        // daemon (the rendezvous path may contain spaces, so it is taken as the
        // whole remainder after --daemon).
        std::string cmd;
        for (LPCWSTR p = lpCmdLine; p && *p; ++p)
        {
            cmd.push_back(static_cast<char>(*p));
        }
        LLStringUtil::trim(cmd);

        const std::string first = cmd.substr(0, cmd.find(' '));
        U32 port = 0;
        if (first.empty() || !LLStringUtil::convertToU32(first, port) || !port)
        {
            LL_WARNS("slplugin") << "SLPluginCEF: missing/invalid launcher port" << LL_ENDL;
            ll_cleanup_apr();
            return 1;
        }

        std::string rendezvous;
        const size_t dpos = cmd.find("--daemon");
        if (dpos != std::string::npos)
        {
            rendezvous = cmd.substr(dpos + 8);   // strlen("--daemon")
            LLStringUtil::trim(rendezvous);
        }

        const int rc = rendezvous.empty() ? slplugin_run(port)
                                          : slplugin_daemon_run(port, rendezvous);
        ll_cleanup_apr();
        return rc;
    }
}

// Exported entry point the CEF bootstrap executable calls. version_info is
// provided by the bootstrap and not needed here.
extern "C" CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE hInstance,
                                               LPTSTR lpCmdLine,
                                               int /*nCmdShow*/,
                                               void* sandbox_info,
                                               cef_version_info_t* /*version_info*/)
{
    return run_cef_host(hInstance, lpCmdLine, sandbox_info);
}
