/**
 * @file slplugin_generic.cpp
 * @brief Static-plugin hook for the generic SLPlugin host.
 *
 * The generic SLPlugin dlopen()s whatever plugin the viewer names in the
 * load_plugin message, so it has no statically-linked plugin: return NULL and
 * LLPluginInstance::load() takes the normal dlopen path. Dedicated single-plugin
 * hosts (e.g. SLPluginCEF) link a different definition that returns their
 * plugin's &LLPluginInitEntryPoint instead.
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

LLPluginInstance::pluginInitFunction ll_get_static_plugin_init()
{
    return nullptr;
}
