/**
 * @file llappviewerlinux.cpp
 * @brief The LLAppViewerLinux class definitions
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

#include "llappviewerlinux.h"

#include "llcommandlineparser.h"

#include "lldiriterator.h"
#include "llurldispatcher.h"		// SLURL from other app instance
#include "llviewernetwork.h"
#include "llviewercontrol.h"
#include "llwindowsdl.h"
#include "llmd5.h"
#include "llfindlocale.h"

#include <exception>

#include "algamemode.h"

// Sentry (https://sentry.io) crash reporting tool
#if defined(AL_SENTRY)
#include <sentry.h>
#endif

#if LL_DBUS_ENABLED
#include <sdbus-c++/sdbus-c++.h>
#include "llappviewerlinux_api-client-glue.h"
#include "llappviewerlinux_api-server-glue.h"

#include "threadpool.h"
#include "workqueue.h"
#endif

namespace
{
	int gArgC = 0;
	char **gArgV = NULL;
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

int main( int argc, char **argv ) 
{
	gArgC = argc;
	gArgV = argv;

	LLAppViewer* viewer_app_ptr = new LLAppViewerLinux();

	// install unexpected exception handler
	gOldTerminateHandler = std::set_terminate(exceptionTerminateHandler);
	// install crash handlers
	viewer_app_ptr->setErrorHandler(LLAppViewer::handleViewerCrash);

	bool ok = viewer_app_ptr->init();
	if(!ok)
	{
		LL_WARNS() << "Application init failed." << LL_ENDL;
		return -1;
	}

		// Run the application main loop
	while (! viewer_app_ptr->frame()) 
	{}

#if LL_DBUS_ENABLED
	((LLAppViewerLinux*)viewer_app_ptr)->shutdownDBUS();
#endif

	if (!LLApp::isError())
	{
		//
		// We don't want to do cleanup here if the error handler got called -
		// the assumption is that the error handler is responsible for doing
		// app cleanup if there was a problem.
		//
		viewer_app_ptr->cleanup();
	}
	delete viewer_app_ptr;
	viewer_app_ptr = NULL;

#if defined(AL_SENTRY)
	sentry_close();
#endif

	//ALGameMode::shutdown();

	return 0;
}

LLAppViewerLinux::LLAppViewerLinux()
{
}

LLAppViewerLinux::~LLAppViewerLinux()
{
}

bool LLAppViewerLinux::init()
{
    LLAppViewer* pApp = LLAppViewer::instance();
    pApp->initCrashReporting();

	bool success = LLAppViewer::init();

	ALGameMode::instance().init();

	return success;
}

bool LLAppViewerLinux::restoreErrorTrap()
{
	// *NOTE:Mani there is a case for implementing this on the mac.
	// Linux doesn't need it to my knowledge.
	return true;
}

/////////////////////////////////////////
#if LL_DBUS_ENABLED

class ViewerAppAPI : public sdbus::AdaptorInterfaces<com::secondlife::ViewerAppAPI_adaptor /*, more adaptor classes if there are more interfaces*/>
{
public:
    ViewerAppAPI(sdbus::IConnection& connection, std::string objectPath)
        : AdaptorInterfaces(connection, std::move(objectPath))
    {
        registerAdaptor();
    }

    ~ViewerAppAPI()
    {
        unregisterAdaptor();
    }

protected:
	bool GoSLURL(const std::string& slurl) override
	{
		LL_INFOS() << "Was asked to go to slurl: " << slurl << LL_ENDL;

	    LL::WorkQueue::ptr_t main_queue = LL::WorkQueue::getInstance("mainloop");
	    LL::WorkQueue::ptr_t general_queue = LL::WorkQueue::getInstance("General");
	    llassert_always(main_queue);
	    llassert_always(general_queue);

        bool posted = main_queue->postTo(
            general_queue,
            [slurl]() // Work done on general queue
            {
                return slurl;
            },
            [](std::string url) // Callback to main thread
            mutable {
				const bool trusted_browser = false;
				if (LLURLDispatcher::dispatch(url, "", nullptr, trusted_browser))
				{
					LL_INFOS() << "Dispatched url from ViewerAppAPI: " << url << LL_ENDL;
				}
            });

		return posted; // the invokation succeeded, even if the actual dispatch didn't.
	}
};

class ViewerAppAPIProxy : public sdbus::ProxyInterfaces<com::secondlife::ViewerAppAPI_proxy /*, more proxy classes if there are more interfaces*/>
{
public:
    ViewerAppAPIProxy(std::string destination, std::string objectPath)
        : ProxyInterfaces(std::move(destination), std::move(objectPath))
    {
        registerProxy();
    }

    ~ViewerAppAPIProxy()
    {
        unregisterProxy();
    }
};

//virtual
bool LLAppViewerLinux::initSLURLHandler()
{
    // Create D-Bus connection to the system bus and requests name on it.
    try
    {
    	mViewerAPIConnection = sdbus::createSessionBusConnection(VIEWERAPI_SERVICE);
    }
    catch(const sdbus::Error& err)
    {
    	LL_WARNS() << "Failed to create DBUS session connection: " << err.what() << LL_ENDL;
    	return false;
    }

    // Create concatenator D-Bus object.
    try
    {
    	mViewerAPIObject = std::make_unique<ViewerAppAPI>(*mViewerAPIConnection, VIEWERAPI_PATH);
    }
    catch(const sdbus::Error& err)
    {
    	LL_WARNS() << "Failed to create DBUS ViewerAppAPI object: " << err.what() << LL_ENDL;
    	mViewerAPIConnection.reset();
    	return false;
    }

    // Run the loop on the connection.
    try
    {
    	mViewerAPIConnection->enterEventLoopAsync();
    }
    catch(const sdbus::Error& err)
    {
    	LL_WARNS() << "Failed to create DBUS event loop: " << err.what() << LL_ENDL;
    	mViewerAPIObject.reset();
    	mViewerAPIConnection.reset();
    	return false;
    }
    return true;
}

void LLAppViewerLinux::shutdownDBUS()
{
	mViewerAPIObject.reset();
	mViewerAPIConnection.reset();
}

//virtual
bool LLAppViewerLinux::sendURLToOtherInstance(const std::string& url)
{
    // Create proxy object for the concatenator object on the server side
    ViewerAppAPIProxy viewerAppAPIProxy(VIEWERAPI_SERVICE, VIEWERAPI_PATH);

    try
    {
        viewerAppAPIProxy.GoSLURL(url);
    }
    catch(const sdbus::Error& e)
    {
        LL_WARNS() << "Got ViewerAppAPI error " << e.getName() << " with message " << e.getMessage() << LL_ENDL;
    }

	return true;
}

#else // LL_DBUS_ENABLED
bool LLAppViewerLinux::initSLURLHandler()
{
	return false; // not implemented without dbus
}
bool LLAppViewerLinux::sendURLToOtherInstance(const std::string& url)
{
	return false; // not implemented without dbus
}
#endif // LL_DBUS_ENABLED

void LLAppViewerLinux::setCrashUserMetadata(const LLUUID& user_id, const std::string& avatar_name)
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

void LLAppViewerLinux::initCrashReporting(bool reportFreeze)
{
#if defined(AL_SENTRY)
	sentry_options_t* options = sentry_options_new();
	sentry_options_set_dsn(options, SENTRY_DSN);
	sentry_options_set_release(options, LL_VIEWER_CHANNEL_AND_VERSION);

#if 0
	std::string crashpad_path = gDirUtilp->getExpandedFilename(LL_PATH_EXECUTABLE, "crashpad_handler");
	sentry_options_set_handler_path(options, crashpad_path.c_str());
#endif
	std::string database_path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "sentry");
	sentry_options_set_database_path(options, database_path.c_str());

	std::string logfile_path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "Alchemy.log");
	sentry_options_add_attachment(options, logfile_path.c_str());

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

bool LLAppViewerLinux::beingDebugged()
{
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
				
				if (strcmp(base, "gdb") == 0)
				{
					debugged = yes;
				}
			}
			free(name);
		}
	}

	return debugged == yes;
}

void LLAppViewerLinux::initLoggingAndGetLastDuration()
{
	LLAppViewer::initLoggingAndGetLastDuration();
}

bool LLAppViewerLinux::initParseCommandLine(LLCommandLineParser& clp)
{
	if (!clp.parseCommandLine(gArgC, gArgV))
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
			LL_INFOS("AppInit") << "Language " << ll_safe_string(locale->lang) << LL_ENDL;
			LL_INFOS("AppInit") << "Location " << ll_safe_string(locale->country) << LL_ENDL;
			LL_INFOS("AppInit") << "Variant " << ll_safe_string(locale->variant) << LL_ENDL;

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

std::string LLAppViewerLinux::generateSerialNumber()
{
	char serial_md5[MD5HEX_STR_SIZE];
	serial_md5[0] = 0;
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

	return serial_md5;
}
