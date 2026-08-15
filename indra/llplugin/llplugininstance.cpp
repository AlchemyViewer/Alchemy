/**
 * @file llplugininstance.cpp
 * @brief LLPluginInstance handles loading the dynamic library of a plugin and setting up its entry points for message passing.
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

#include "llplugininstance.h"

/** Virtual destructor. */
LLPluginInstanceMessageListener::~LLPluginInstanceMessageListener()
{
}

LLPluginInstance::pluginInitFunction LLPluginInstance::sStaticInitFunction = NULL;

// static
void LLPluginInstance::setStaticInitFunction(pluginInitFunction func)
{
    sStaticInitFunction = func;
}

/**
 * Constructor.
 *
 * @param[in] owner Plugin instance. TODO:DOC is this a good description of what "owner" is?
 */
LLPluginInstance::LLPluginInstance(LLPluginInstanceMessageListener *owner) :
    mPluginUserData(NULL),
    mPluginSendMessageFunction(NULL)
{
    mOwner = owner;
}

/**
 * Destructor.
 */
LLPluginInstance::~LLPluginInstance()
{
}

/**
 * Runs the statically-linked plugin's init function.
 *
 * Each plugin now lives in its own host executable that statically links exactly
 * one plugin and registers its entry point via setStaticInitFunction(), so there
 * is no longer a dlopen path. plugin_dir/plugin_file are vestigial - they remain
 * in the load_plugin message contract but are ignored here.
 *
 * @return 0 if successful, or the error code returned from the plugin's init function.
 */
int LLPluginInstance::load(const std::string& plugin_dir, std::string &plugin_file)
{
    (void)plugin_dir;
    (void)plugin_file;

    if (!sStaticInitFunction)
    {
        LL_WARNS("Plugin") << "no statically-linked plugin init function registered" << LL_ENDL;
        return -1;
    }

    int result = sStaticInitFunction(staticReceiveMessage, (void*)this, &mPluginSendMessageFunction, &mPluginUserData);
    if (result != 0)
    {
        LL_WARNS("Plugin") << "call to init function failed with error " << result << LL_ENDL;
    }

    return result;
}

/**
 * Sends a message to the plugin.
 *
 * @param[in] message Message
 */
void LLPluginInstance::sendMessage(const std::string &message)
{
    if(mPluginSendMessageFunction)
    {
        LL_DEBUGS("Plugin") << "sending message to plugin: \"" << message << "\"" << LL_ENDL;
        mPluginSendMessageFunction(message.data(), message.size(), &mPluginUserData);
    }
    else
    {
        LL_WARNS("Plugin") << "dropping message: \"" << message << "\"" << LL_ENDL;
    }
}

/**
 * Idle. TODO:DOC what's the purpose of this?
 *
 */
void LLPluginInstance::idle(void)
{
}

// static
void LLPluginInstance::staticReceiveMessage(const char *message_string, size_t message_size, void **user_data)
{
    // TODO: validate that the user_data argument is still a valid LLPluginInstance pointer
    // we could also use a key that's looked up in a map (instead of a direct pointer) for safety, but that's probably overkill
    LLPluginInstance *self = (LLPluginInstance*)*user_data;
    self->receiveMessage(message_string, message_size);
}

/**
 * Plugin receives message from plugin loader shell.
 *
 * @param[in] message_string Message bytes (binary LLSD, may contain embedded NULs)
 * @param[in] message_size Length of message_string in bytes
 */
void LLPluginInstance::receiveMessage(const char *message_string, size_t message_size)
{
    // Reconstruct the message with its exact length: the payload is binary and
    // may contain embedded NULs, so it can't be treated as a C string.
    std::string message(message_string, message_size);
    if(mOwner)
    {
        mOwner->receivePluginMessage(message);
    }
    else
    {
        LL_WARNS("Plugin") << "dropping incoming message: \"" << message << "\"" << LL_ENDL;
    }
}
