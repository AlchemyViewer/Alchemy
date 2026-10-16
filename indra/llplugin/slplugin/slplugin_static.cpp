/**
 * @file slplugin_static.cpp
 * @brief Static-plugin hook for a dedicated single-plugin host executable.
 *
 * Each media plugin is now its own executable (e.g. media_plugin_libvlc) that
 * statically links exactly one plugin's object code. This file registers that
 * plugin's entry point with LLPluginInstance so load() calls it directly - there
 * is no dlopen path any more. The CEF host links its own variant
 * (slplugin_cef.cpp) instead, which adds the shared-daemon mode and the CEF
 * runtime lifecycle.
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

#include <string>

// Exported by media_plugin_base, which is statically linked into this host along
// with exactly one media plugin's object code.
extern "C" int LLPluginInitEntryPoint(LLPluginInstance::sendMessageFunction host_send_func,
                                      void *host_user_data,
                                      LLPluginInstance::sendMessageFunction *plugin_send_func,
                                      void **plugin_user_data);

LLPluginInstance::pluginInitFunction ll_get_static_plugin_init()
{
    return &LLPluginInitEntryPoint;
}

// Defined in slplugin.cpp.
int slplugin_run(U32 port);

// A plain single-plugin host has no daemon mode: ignore any rendezvous path and
// serve the single connection. (Only the CEF host overrides this; see
// slplugin_cef.cpp.)
int ll_run_slplugin_host(U32 port, const std::string& /*daemon_rendezvous*/)
{
    return slplugin_run(port);
}
