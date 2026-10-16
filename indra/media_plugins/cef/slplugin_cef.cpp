/**
 * @file slplugin_cef.cpp
 * @brief Static-plugin hook for the dedicated SLPluginCEF host.
 *
 * SLPluginCEF statically links the CEF media plugin (and CEF itself) instead of
 * dlopen()ing media_plugin_cef at runtime. media_plugin_base provides the
 * exported LLPluginInitEntryPoint; hand its address to slplugin so
 * LLPluginInstance::load() calls it directly. This avoids dlopen of libcef
 * (which exhausts the static TLS block on Linux) and gives the Windows sandbox
 * the single-image host it requires.
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

#include "llplugininstance.h"

#include "dullahan.h"

#include <string>

// Exported by media_plugin_base, which is statically linked into this host.
extern "C" int LLPluginInitEntryPoint(LLPluginInstance::sendMessageFunction host_send_func,
                                      void *host_user_data,
                                      LLPluginInstance::sendMessageFunction *plugin_send_func,
                                      void **plugin_user_data);

LLPluginInstance::pluginInitFunction ll_get_static_plugin_init()
{
    return &LLPluginInitEntryPoint;
}

// Defined in slplugin.cpp / slplugin_daemon.cpp.
int slplugin_run(U32 port);
int slplugin_daemon_run(U32 first_port, const std::string& rendezvous_path);

// CEF host entry handed control by the platform main() (slplugin.cpp). This is
// the non-Windows analog of slplugin_cef_bootstrap.cpp's run_cef_host (on Windows
// the bootstrap entry is used instead, and CEF sub-processes are dispatched there
// by CefExecuteProcess; on macOS/Linux the DullahanHelper bundles / dullahan_host
// dispatch them, so there is nothing to do here for sub-processes).
int ll_run_slplugin_host(U32 port, const std::string& daemon_rendezvous)
{
    // Keep one process-global CEF runtime up for the whole life of this host (a
    // dedicated single tab, or the shared daemon serving many) and shut it down
    // exactly once below. Without this a zero-browser gap - e.g. the login web
    // surface closing just before the next opens - would CefShutdown and the next
    // CefInitialize would crash (CEF init is once-per-process). The macOS sandbox
    // toggle is applied separately when the plugin builds its settings
    // (media_plugin_cef.cpp).
    dullahan::setPersistentRuntime(true);

    const int rc = daemon_rendezvous.empty()
                       ? slplugin_run(port)
                       : slplugin_daemon_run(port, daemon_rendezvous);

    // Persistent host: release() left CEF running, so tear it down once now,
    // before the process exits, for a clean teardown.
    dullahan::shutdownRuntime();
    return rc;
}
