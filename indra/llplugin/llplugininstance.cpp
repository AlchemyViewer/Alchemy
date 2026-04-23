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

#include <boost/dll/shared_library.hpp>
#include <boost/dll/shared_library_load_mode.hpp>

#if LL_WINDOWS
#include "direct.h" // needed for _chdir()
#endif

/** Virtual destructor. */
LLPluginInstanceMessageListener::~LLPluginInstanceMessageListener()
{
}

/**
 * TODO:DOC describe how it's used
 */
const char *LLPluginInstance::PLUGIN_INIT_FUNCTION_NAME = "LLPluginInitEntryPoint";

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
    // mDSOHandle's destructor unloads the shared library if loaded.
}

/**
 * Dynamically loads the plugin and runs the plugin's init function.
 *
 * @param[in] plugin_file Name of plugin dll/dylib/so. TODO:DOC is this correct? see .h
 * @return 0 if successful, APR error code or error code from the plugin's init function on failure.
 */
int LLPluginInstance::load(const std::string& plugin_dir, std::string &plugin_file)
{
    pluginInitFunction init_function = NULL;

    if ( plugin_dir.length() )
    {
#if LL_WINDOWS
        // VWR-21275:
        // *SOME* Windows systems fail to load the Qt plugins if the current working
        // directory is not the same as the directory with the Qt DLLs in.
        // This should not cause any run time issues since we are changing the cwd for the
        // plugin shell process and not the viewer.
        // Changing back to the previous directory is not necessary since the plugin shell
        // quits once the plugin exits.
        _chdir( plugin_dir.c_str() );
#endif
    };

    int result = 0;

    try
    {
        mDSOHandle.load(boost::dll::fs::path(plugin_file),
                        boost::dll::load_mode::rtld_now);
    }
    catch (const std::exception& e)
    {
        LL_WARNS("Plugin") << "boost::dll load of " << plugin_file
                           << " failed: " << e.what() << LL_ENDL;
        result = -1;
    }

    if(result == 0)
    {
        try
        {
            init_function = &mDSOHandle.get<std::remove_pointer_t<pluginInitFunction>>(
                PLUGIN_INIT_FUNCTION_NAME);
        }
        catch (const std::exception& e)
        {
            LL_WARNS("Plugin") << "symbol lookup for " << PLUGIN_INIT_FUNCTION_NAME
                               << " failed: " << e.what() << LL_ENDL;
            result = -1;
        }
    }

    if(result == 0)
    {
        result = init_function(staticReceiveMessage, (void*)this, &mPluginSendMessageFunction, &mPluginUserData);

        if(result != 0)
        {
            LL_WARNS("Plugin") << "call to init function failed with error " << result << LL_ENDL;
        }
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
        mPluginSendMessageFunction(message.c_str(), &mPluginUserData);
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
void LLPluginInstance::staticReceiveMessage(const char *message_string, void **user_data)
{
    // TODO: validate that the user_data argument is still a valid LLPluginInstance pointer
    // we could also use a key that's looked up in a map (instead of a direct pointer) for safety, but that's probably overkill
    LLPluginInstance *self = (LLPluginInstance*)*user_data;
    self->receiveMessage(message_string);
}

/**
 * Plugin receives message from plugin loader shell.
 *
 * @param[in] message_string Message
 */
void LLPluginInstance::receiveMessage(const char *message_string)
{
    if(mOwner)
    {
        LL_DEBUGS("Plugin") << "processing incoming message: \"" << message_string << "\"" << LL_ENDL;
        mOwner->receivePluginMessage(message_string);
    }
    else
    {
        LL_WARNS("Plugin") << "dropping incoming message: \"" << message_string << "\"" << LL_ENDL;
    }
}
