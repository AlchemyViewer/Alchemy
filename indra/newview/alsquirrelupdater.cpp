/**
* @file alsquirrelupdater.cpp
* @brief Quick Settings popdown panel
*
* $LicenseInfo:firstyear=2013&license=viewerlgpl$
* Alchemy Viewer Source Code
* Copyright (C) 2013-2023, Alchemy Viewer Project.
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
* $/LicenseInfo$
*/

#include "llviewerprecompiledheaders.h"

#include "alsquirrelupdater.h"

#include "llviewerbuildconfig.h"

#include "llappviewer.h"
#include "llnotificationsutil.h"
#include "llversioninfo.h"
#include "llviewercontrol.h"

#include "llcallbacklist.h"
#include "llprocess.h"
#include "llsdjson.h"
#include "llsdutil.h"
#include "llwin32headerslean.h"
#include "llstartup.h"

#if LL_WINDOWS
#define UPDATER_PLATFORM "win"
#define UPDATER_ARCH "x64"
#elif LL_LINUX
#define UPDATER_PLATFORM "lnx"
#define UPDATER_ARCH "x64"
#elif LL_DARWIN
#define UPDATER_PLATFORM "mac"
#define UPDATER_ARCH "u2"
#endif

static std::string win32_errorcode_to_string(LONG errorMessageID)
{
	if (errorMessageID == 0)
		return std::string(); //No error message has been recorded

	LPWSTR messageBuffer = nullptr;
	size_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), messageBuffer, 0, NULL);

	std::wstring message(messageBuffer, size);

	//Free the buffer.
	LocalFree(messageBuffer);

	return ll_convert_wide_to_string(message);
}

struct ALRegWriter
{
	static void setValueFailed(const LSTATUS& rc) { LL_WARNS() << "Failed to write reg value with error: " << win32_errorcode_to_string(rc) << LL_ENDL; }
	static void checkSuccess(const LSTATUS& rc, std::function<void(const LSTATUS&)> on_failed = setValueFailed)
	{
		if (rc != ERROR_SUCCESS) on_failed(rc);
	}

	ALRegWriter(HKEY parent_key, const std::wstring& key_name) : mSuccess(true)
	{
		checkSuccess(RegCreateKeyEx(parent_key, key_name.c_str(), NULL, NULL, NULL, KEY_ALL_ACCESS, NULL, &mResultKey, NULL), [&](const LSTATUS& rc)
		{
			mSuccess = false;
			LL_WARNS() << "Failed to open " << ll_convert_wide_to_string(key_name) << " with error: " << win32_errorcode_to_string(rc) << LL_ENDL;
		});
	}

	void setValue(const std::wstring& value, DWORD dwType = REG_SZ, LPCTSTR value_name = NULL) const
	{
		checkSuccess(RegSetValueEx(mResultKey, value_name, NULL, dwType, (LPBYTE) value.c_str(), (value.size() + 1) * sizeof(wchar_t)));
	}

	void setValue(const std::string& value, DWORD dwType = REG_SZ, LPCTSTR value_name = NULL) const
	{
		setValue(ll_convert_string_to_wide(value), dwType, value_name);
	}

	std::string getStringValue(const std::wstring& subkey_name, const std::wstring& value_name)
	{
		const DWORD BUFFER_SIZE = 512;
		WCHAR outstr[BUFFER_SIZE];
		DWORD bufsize = BUFFER_SIZE;
		checkSuccess(RegGetValue(mResultKey, subkey_name.c_str(), value_name.c_str(), RRF_RT_REG_SZ, nullptr, outstr, &bufsize), [&](const LSTATUS& rc)
		{
			LL_WARNS() << "Failed to read " << ll_convert_wide_to_string(value_name) << " from key " << ll_convert_wide_to_string(subkey_name)
				<< " with error: " << win32_errorcode_to_string(rc) << LL_ENDL;
			return std::string();
		});
		std::wstring widestr(outstr, bufsize);
		return ll_convert_wide_to_string(widestr);
	}

	ALRegWriter createSubKey(const std::wstring& key_name) const
	{
		return ALRegWriter(mResultKey, key_name);
	}

	void deleteTree(const wchar_t* name) const { RegDeleteTree(mResultKey, name); }

	~ALRegWriter()
	{
		RegFlushKey(mResultKey);
		RegCloseKey(mResultKey);
	}

	operator bool() { return mSuccess; }

	bool mSuccess;
	HKEY mResultKey;
};

// static
void ALUpdateUtils::updateSlurlRegistryKeys(const std::string& protocol, const std::string& name, const std::string& executable_path)
{
	// SecondLife slurls
	std::wstring reg_path = ll_convert_string_to_wide(fmt::format("Software\\Classes\\{}", protocol));
	if (auto regpath = ALRegWriter(HKEY_CURRENT_USER, reg_path))
	{
		regpath.setValue(name);

		ALRegWriter::checkSuccess(RegSetValueEx(regpath.mResultKey, TEXT("URL Protocol"), NULL, REG_SZ, NULL, 0));

		if (auto defaulticon = regpath.createSubKey(TEXT("DefaultIcon")))
		{
			defaulticon.setValue(executable_path);
		}

		if (auto shell = regpath.createSubKey(TEXT("shell")))
		{
			shell.setValue(TEXT("open"));

			if (auto open = shell.createSubKey(TEXT("open")))
			{
				open.setValue(LLVersionInfo::instance().getChannel(), REG_SZ, TEXT("FriendlyAppName"));

				if (auto command = open.createSubKey(TEXT("command")))
				{
					std::string open_cmd_string = fmt::format("\"{}\" -url \"%1\"", executable_path);
					command.setValue(open_cmd_string, REG_EXPAND_SZ);
				}
			}
		}
	}
}

// static
bool ALUpdateUtils::handleCommandLineParse(LLControlGroupCLP& clp)
{
	bool is_install = clp.hasOption("squirrel-install");
	bool is_update = clp.hasOption("squirrel-updated");
	bool is_uninstall = clp.hasOption("squirrel-uninstall");
	if (is_install || is_update || is_uninstall)
	{
		std::string install_dir = gDirUtilp->getExecutableDir();
		size_t path_end = install_dir.find_last_of('\\');
		if (path_end != std::string::npos)
		{
			install_dir = install_dir.substr(0, path_end);
		}

		if (is_install)
		{
			std::string executable_path(install_dir);
			gDirUtilp->append(executable_path, gDirUtilp->getExecutableFilename());

			updateSlurlRegistryKeys("secondlife", "URL:Second Life", executable_path);
			updateSlurlRegistryKeys("x-grid-info", "URL:Hypergrid", executable_path);
			updateSlurlRegistryKeys("x-grid-location-info", "URL:Hypergrid", executable_path);
		}
		else if (is_uninstall) // uninstall
		{
			// Delete SecondLife and Hypergrid slurls
			if (auto classes = ALRegWriter(HKEY_CURRENT_USER, TEXT("Software\\Classes")))
			{
				auto appname = classes.getStringValue(TEXT("secondlife\\shell\\open"), TEXT("FriendlyAppName"));
				if (appname.find(LLVersionInfo::instance().getChannel(), 0) != std::string::npos)
				{
					classes.deleteTree(TEXT("secondlife"));
				}
				appname = classes.getStringValue(TEXT("x-grid-info\\shell\\open"), TEXT("FriendlyAppName"));
				if (appname.find(LLVersionInfo::instance().getChannel(), 0) != std::string::npos)
				{
					classes.deleteTree(TEXT("x-grid-info"));
				}
				appname = classes.getStringValue(TEXT("x-grid-location-info\\shell\\open"), TEXT("FriendlyAppName"));
				if (appname.find(LLVersionInfo::instance().getChannel(), 0) != std::string::npos)
				{
					classes.deleteTree(TEXT("x-grid-location-info"));
				}
			}
		}

		std::string updater_path = install_dir;
		gDirUtilp->append(updater_path, "Update.exe");
		if (LLFile::isfile(updater_path))
		{
			LLProcess::Params process_params;
			process_params.executable = updater_path;

			if (is_install)
			{
				process_params.args.add("--createShortcut");
			}
			else if (is_uninstall)
			{
				process_params.args.add("--removeShortcut");
			}
			process_params.args.add(gDirUtilp->getExecutableFilename());
			if (is_update)
			{
				process_params.args.add("--updateOnly");
			}
			process_params.args.add("--shortcut-locations");
			process_params.args.add("Desktop,StartMenu");
			process_params.attached = false;
			process_params.autokill = false;
			LLProcess::create(process_params);
		}
		else
		{
			LL_WARNS() << "Squirrel not found or viewer is not running in squirrel directory" << LL_ENDL;
		}
		return true;
	}
	return false;
}

ALUpdateHandler::ALUpdateHandler()
	: mUpdaterDonePump("SquirrelUpdate", true)
	, mUpdateAction(E_NO_ACTION)
	, mUpdateCallback(nullptr)
{
	if (gSavedSettings.getControl("AlchemyUpdateServiceURL"))
	{
		mUpdateURL = gSavedSettings.getString("AlchemyUpdateServiceURL");
	}
	else
	{
		std::string channel = LLVersionInfo::instance().getChannel();
		channel.erase(std::remove_if(channel.begin(), channel.end(), isspace), channel.end());

		mUpdateURL = fmt::format("{}/{}/{}-{}/", VIEWER_UPDATE_SERVICE, channel, UPDATER_PLATFORM, UPDATER_ARCH);
	}
	LL_INFOS() << "Update service url: " << mUpdateURL << LL_ENDL;

	doPeriodically(boost::bind(&ALUpdateHandler::periodicUpdateCheck, this), gSavedSettings.getF32("AlchemyUpdateCheckInterval"));
}

bool ALUpdateHandler::periodicUpdateCheck()
{
	check();
	return LLApp::isExiting();
}

bool ALUpdateHandler::start(EUpdateAction update_action, update_callback_t callback)
{
	mUpdateAction = update_action;
	mUpdateCallback = callback;

	mUpdatePumpListenerName = LLEventPump::inventName("SquirrelEvent");
	mUpdaterDonePump.listen(mUpdatePumpListenerName, boost::bind(&ALUpdateHandler::processDone, this, _1));

	std::string updater_dir = gDirUtilp->getExecutableDir();
	size_t path_end = updater_dir.find_last_of('\\');
	if (path_end != std::string::npos)
	{
		updater_dir = updater_dir.substr(0, path_end);
	}

	std::string updater_path = updater_dir;
	gDirUtilp->append(updater_path, "Update.exe");

	if (LLFile::isfile(updater_path))
	{
		// Okay, launch child.
		LLProcess::Params params;
		params.executable = updater_path;
		params.cwd = updater_dir;
		if (mUpdateAction == E_CHECK)
		{
			params.args.add("--checkForUpdate");
			params.args.add(mUpdateURL);
		}
		else if (mUpdateAction == E_DOWNLOAD)
		{
			params.args.add("--download");
			params.args.add(mUpdateURL);
		}
		else if (mUpdateAction == E_INSTALL)
		{
			params.autokill = false;
			params.attached = false;
			params.args.add("--update");
			params.args.add(mUpdateURL);
		}
		else if (mUpdateAction == E_QUIT_INSTALL)
		{
			params.autokill = false;
			params.attached = false;
			params.args.add("--processStartAndWait");
			params.args.add(gDirUtilp->getExecutableFilename());
		}
		params.files.add(LLProcess::FileParam()); // stdin
		params.files.add(LLProcess::FileParam("pipe")); // stdout
		params.files.add(LLProcess::FileParam()); // stderr
		params.postend = mUpdaterDonePump.getName();
		mUpdater = LLProcess::create(params);
	}

	if (!mUpdater)
	{
		LL_WARNS() << "Failed to run updater at path '" << updater_path << "'" << LL_ENDL;
		mUpdaterDonePump.stopListening(mUpdatePumpListenerName);
		mUpdater = nullptr;
		return false;
	}
	return true;
}

bool ALUpdateHandler::processDone(const LLSD& data)
{
	bool success = false;
	if (data.has("data"))
	{
		S32 exit_code = data["data"].asInteger();
		if (exit_code == 0)
		{
			success = true;
		}
	}
	if (success)
	{
		LLSD update_data = LLSD::emptyMap();
		if (mUpdateAction != E_INSTALL || mUpdateAction != E_QUIT_INSTALL)
		{
			LLProcess::ReadPipe& childout(mUpdater->getReadPipe(LLProcess::STDOUT));
			if (childout.size())
			{
				std::string output = childout.read(childout.size());
				size_t found = output.find_first_of('{', 0);
				if (found != std::string::npos)
				{
					output = output.substr(found, std::string::npos);

					nlohmann::json update_info;
					try
					{
						update_info = nlohmann::json::parse(output);
					}
					catch (nlohmann::json::exception &e)
					{
						LL_WARNS() << "Exception during json processing: " << e.what() << LL_ENDL;
						if (mUpdateCallback != nullptr)
						{
							mUpdateCallback(update_data);
						}
						mUpdaterDonePump.stopListening(mUpdatePumpListenerName);
						mUpdater = nullptr;
						mUpdateAction = E_NO_ACTION;
						return false;
					}

					if (update_info.is_object())
					{
						update_data = LlsdFromJson(update_info);
						LL_INFOS() << "Outdata from updater test: " << ll_pretty_print_sd(update_data) << LL_ENDL;
					}
				}
			}
		}

		mUpdaterDonePump.stopListening(mUpdatePumpListenerName);
		mUpdater = nullptr;
		if (mUpdateCallback != nullptr)
		{
			mUpdateCallback(update_data);
		}
	}
	else
	{
		LL_WARNS() << "Process error for listener " << mUpdatePumpListenerName << " with data " << ll_pretty_print_sd(data) << LL_ENDL;
		mUpdaterDonePump.stopListening(mUpdatePumpListenerName);
		mUpdateAction = E_NO_ACTION;
		mUpdater = nullptr;
	}

	mUpdateAction = E_NO_ACTION;
	return true;
}

bool ALUpdateHandler::check()
{
	if (!mUpdater && mUpdateAction == E_NO_ACTION)
	{
		LL_INFOS() << "Discovering viewer update..." << LL_ENDL;
		return start(ALUpdateHandler::E_CHECK,
			std::bind(&ALUpdateHandler::updateCheckFinished, this, std::placeholders::_1));
	}
	else
	{
		LL_INFOS() << "Not checking update because mUpdater=" << mUpdater << " and mUpdateAction=" << mUpdateAction << LL_ENDL;
	}
	return false;
}

bool ALUpdateHandler::download()
{
	if (!mUpdater)
	{
		LL_INFOS() << "Downloading a new update!" << LL_ENDL;
		return start(ALUpdateHandler::E_DOWNLOAD,
			std::bind(&ALUpdateHandler::updateDownloadFinished, this, std::placeholders::_1));
	}
	return false;
}

bool ALUpdateHandler::install()
{
	if (!mUpdater)
	{
		return start(ALUpdateHandler::E_INSTALL,
			std::bind(&ALUpdateHandler::updateInstallFinished, this, std::placeholders::_1));
	}
	return false;
}

void ALUpdateHandler::restartToNewVersion()
{
	if (!mUpdater)
	{
		start(ALUpdateHandler::E_QUIT_INSTALL, update_callback_t());
		LLAppViewer::instance()->requestQuit();
	}
}

void ALUpdateHandler::updateCheckFinished(const LLSD& data)
{
	if (data.emptyMap()) return;
	ALVersionInfo cur_ver(LLVersionInfo::instance().getMajor(), LLVersionInfo::instance().getMinor(), LLVersionInfo::instance().getPatch());
	ALVersionInfo new_ver;
	if (data.has("futureVersion")) new_ver.parse(data["futureVersion"].asString());

	if (new_ver > cur_ver)
	{
		mSavedUpdateInfo = data;
		LL_WARNS() << "New ver found: " << new_ver.version() << LL_ENDL;
		static LLCachedControl<S32> update_preference(gSavedSettings, "AlchemyUpdatePreference", 0);
		if (update_preference == 0)
		{
			install();
		}
		else if (update_preference == 1)
		{
			download();
		}
		else if (update_preference == 2)
		{
			LLSD args;
			args["VIEWER_VER"] = fmt::format("{} {}", LLVersionInfo::instance().getChannel(), LLVersionInfo::instance().getShortVersion());
			args["VIEWER_UPDATES"] = fmt::format("{}", new_ver.version());
			LLSD payload;
			payload["user_update_action"] = LLSD(E_DOWNLOAD_INSTALL);
			LLNotificationsUtil::add("UpdateDownloadRequest", args, payload, boost::bind(&ALUpdateHandler::onUpdateNotification, this, _1, _2));
		}
	}
	else
	{
		LL_INFOS() << "No new update was found." << LL_ENDL;
		mUpdateAction = E_NO_ACTION;
	}
}

void ALUpdateHandler::updateDownloadFinished(const LLSD& data)
{
	static LLCachedControl<S32> update_preference(gSavedSettings, "AlchemyUpdatePreference", 0);
	if (update_preference == 1)
	{
		ALVersionInfo new_ver;
		if (mSavedUpdateInfo.has("futureVersion")) new_ver.parse(mSavedUpdateInfo["futureVersion"].asString());

		std::string releases;
		if (mSavedUpdateInfo.has("releasesToApply"))
		{
			LLSD release_array = mSavedUpdateInfo["releasesToApply"];
			if (release_array.size() > 0)
			{
				for (LLSD::array_const_iterator it = release_array.beginArray(), end_it = release_array.endArray(); it != end_it; ++it)
				{
					auto release_entry = *it;
					if (release_entry.isMap())
					{
						releases += release_entry["version"].asString();
					}
				}
			}
		}
		LLSD args;
		args["VIEWER_VER"] = fmt::format("{} {}", LLVersionInfo::instance().getChannel(), new_ver.version());
		args["VIEWER_UPDATES"] = releases;
		LLSD payload;
		payload["user_update_action"] = LLSD(E_DOWNLOADED);
		LLNotificationsUtil::add("UpdateDownloaded", args, payload, boost::bind(&ALUpdateHandler::onUpdateNotification, this, _1, _2));
	}
}

void ALUpdateHandler::updateInstallFinished(const LLSD& data)
{
	static LLCachedControl<S32> update_preference(gSavedSettings, "AlchemyUpdatePreference", 0);
	// Autodownload and install
	if (update_preference == 0)
	{
		ALVersionInfo new_ver;
		if (mSavedUpdateInfo.has("futureVersion")) new_ver.parse(mSavedUpdateInfo["futureVersion"].asString());
		LLSD args;
		args["VIEWER_VER"] = fmt::format("{} {}", LLVersionInfo::instance().getChannel(), LLVersionInfo::instance().getShortVersion());
		args["VIEWER_UPDATES"] = fmt::format("{}", new_ver.version());
		LLSD payload;
		payload["user_update_action"] = LLSD(E_INSTALLED_RESTART);
		LLNotificationsUtil::add((LLStartUp::getStartupState() < STATE_STARTED ? "UpdateInstalledRestart" : "UpdateInstalledRestartToast"), args, payload, boost::bind(&ALUpdateHandler::onUpdateNotification, this, _1, _2));
	}
	// Autodownload and request install or Request download and request install
	else if (update_preference == 1 || update_preference == 2)
	{
		restartToNewVersion();
	}
}

void ALUpdateHandler::onUpdateNotification(const LLSD& notification, const LLSD& response)
{
	const S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
	if (option == 0)
	{
		S32 update_action = notification["payload"]["user_update_action"].asInteger();
		if (update_action == E_INSTALLED_RESTART)
		{
			restartToNewVersion();
		}
		else if (update_action == E_DOWNLOAD || update_action == E_DOWNLOAD_INSTALL)
		{
			install();
		}
		return;
	}
	mUpdateAction = E_NO_ACTION;
	mSavedUpdateInfo = LLSD::emptyMap();
}
