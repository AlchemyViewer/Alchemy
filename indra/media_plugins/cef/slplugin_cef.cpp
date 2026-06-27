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

// Exported by media_plugin_base, which is statically linked into this host.
extern "C" int LLPluginInitEntryPoint(LLPluginInstance::sendMessageFunction host_send_func,
                                      void *host_user_data,
                                      LLPluginInstance::sendMessageFunction *plugin_send_func,
                                      void **plugin_user_data);

LLPluginInstance::pluginInitFunction ll_get_static_plugin_init()
{
    return &LLPluginInitEntryPoint;
}
