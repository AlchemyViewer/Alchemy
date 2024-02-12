/**
* @file alsquirrelupdater.h
* @brief Quick Settings popdown panel
*
* $LicenseInfo:firstyear=2013&license=viewerlgpl$
* Alchemy Viewer Source Code
* Copyright (C) 2018, Alchemy Viewer Project.
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


#include "llcommandlineparser.h"

#include "llevents.h"
#include "lleventtimer.h"
#include "llprocess.h"
#include "llsingleton.h"

class ALUpdateUtils
{
public:
	// static functions
	static void updateSlurlRegistryKeys(const std::string& protocol, const std::string& name, const std::string& executable_path);
	static bool handleCommandLineParse(LLControlGroupCLP& clp);
};

class ALUpdateHandler : public LLSingleton<ALUpdateHandler>
{
	LLSINGLETON(ALUpdateHandler);
public:
	typedef enum EUpdateAction {
		E_NO_ACTION = 0,
		E_CHECK,
		E_DOWNLOAD,
		E_INSTALL,
		E_QUIT_INSTALL
	} e_update_action;

	typedef std::function<void(const LLSD& data)> update_callback_t;

	bool periodicUpdateCheck();

	bool check();
	bool download();
	bool install();

	void restartToNewVersion();

	struct ALVersionInfo
	{
		ALVersionInfo()
			: mMajor(0), mMinor(0), mPatch(0)
		{}
		ALVersionInfo(S32 major, S32 minor, S32 patch)
			: mMajor(major), mMinor(minor), mPatch(patch)
		{}

		bool parse(const std::string& instr)
		{
			std::istringstream inver(instr);
			S32 ver_part;
			try
			{
				inver >> ver_part;
				mMajor = ver_part;
				char c = inver.get(); // skip the period
				if (c != '.') { return false; }
				inver >> ver_part;
				mMinor = ver_part;
				c = inver.get(); // skip the hypen
				if (c != '.') { return false; }
				inver >> ver_part;
				mPatch = ver_part;
			}
			catch (...)
			{
				return false;
			}
			return true;
		}

		std::string version()
		{
			return llformat("%i.%i.%i", mMajor, mMinor, mPatch);
		}

		bool operator == (const ALVersionInfo& rhs) const
		{
			return (mMajor == rhs.mMajor && mMinor == rhs.mMinor && mPatch == rhs.mPatch);
		}
		bool operator != (const ALVersionInfo& rhs) const
		{
			return (mMajor != rhs.mMajor || (mMinor != rhs.mMinor || mPatch != rhs.mPatch));
		}
		bool operator < (const ALVersionInfo& rhs) const
		{
			return ((mMajor < rhs.mMajor)
				|| ((mMajor <= rhs.mMajor && mMinor < rhs.mMinor)
					|| (mMajor <= rhs.mMajor && mMinor <= rhs.mMinor && mPatch < rhs.mPatch)));
		}
		bool operator <= (const ALVersionInfo& rhs) const
		{
			return (*this < rhs) || (*this == rhs);
		}
		bool operator > (const ALVersionInfo& rhs) const
		{
			return ((mMajor > rhs.mMajor)
				|| ((mMajor >= rhs.mMajor && mMinor > rhs.mMinor)
					|| (mMajor >= rhs.mMajor && mMinor >= rhs.mMinor && mPatch > rhs.mPatch)));
		}
		bool operator >= (const ALVersionInfo& rhs) const
		{
			return (*this > rhs) || (*this == rhs);
		}

	private:
		S32 mMajor, mMinor, mPatch;
	};

	static bool isSupported()
	{
		std::string updater_path = gDirUtilp->getExecutableDir();
		size_t path_end = updater_path.find_last_of('\\');
		if (path_end != std::string::npos)
		{
			updater_path = updater_path.substr(0, path_end);
		}

		gDirUtilp->append(updater_path, "Update.exe");
		return LLFile::isfile(updater_path);
	}

private:
	bool start(EUpdateAction update_action, update_callback_t callback);
	bool processDone(const LLSD& data);

	typedef enum EUserUpdateAction {
		E_INSTALLED_RESTART = 0,
		E_DOWNLOADED,
		E_DOWNLOAD_INSTALL
	} e_user_update_action;

	void updateCheckFinished(const LLSD& data);
	void updateDownloadFinished(const LLSD& data);
	void updateInstallFinished(const LLSD& data);

	void onUpdateNotification(const LLSD& notification, const LLSD& response);

	LLProcessPtr mUpdater;

	std::string mUpdatePumpListenerName;
	LLEventStream mUpdaterDonePump;

	EUpdateAction mUpdateAction;
	update_callback_t mUpdateCallback;

	LLSD mSavedUpdateInfo;
	std::string mUpdateURL;
};
