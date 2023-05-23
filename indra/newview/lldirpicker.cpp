/** 
 * @file lldirpicker.cpp
 * @brief OS-specific file picker
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "lldirpicker.h"
#include "llworld.h"
#include "llviewerwindow.h"
#include "llkeyboard.h"
#include "lldir.h"
#include "llframetimer.h"
#include "lltrans.h"
#include "llwindow.h"	// beforeDialog()
#include "llviewercontrol.h"
#include "llwin32headerslean.h"

#if LL_NFD
#include "nfd.hpp"
#endif

#if LL_LINUX || LL_DARWIN
# include "llfilepicker.h"
#endif

//
// Globals
//

LLDirPicker LLDirPicker::sInstance;

#if LL_WINDOWS
#include <shlobj.h>
#endif

//
// Implementation
//

// utility function to check if access to local file system via file browser 
// is enabled and if not, tidy up and indicate we're not allowed to do this.
bool LLDirPicker::check_local_file_access_enabled()
{
	// if local file browsing is turned off, return without opening dialog
	bool local_file_system_browsing_enabled = gSavedSettings.getBOOL("LocalFileSystemBrowsingEnabled");
	if ( ! local_file_system_browsing_enabled )
	{
		mDir.clear();	// Windows
		mFileName = NULL; // Mac/Linux
		return false;
	}

	return true;
}

#if LL_NFD
LLDirPicker::LLDirPicker() :
	mFileName(NULL),
	mLocked(false)
{
	reset();
}

LLDirPicker::~LLDirPicker()
{
}


void LLDirPicker::reset()
{
	mDir.clear();
}

BOOL LLDirPicker::getDir(std::string* filename, bool blocking)
{
	if( mLocked )
	{
		return FALSE;
	}

	// if local file browsing is turned off, return without opening dialog
	if ( check_local_file_access_enabled() == false )
	{
		return FALSE;
	}

	BOOL success = FALSE;

	if (blocking)
	{
		// Modal, so pause agent
		send_agent_pause();
	}

    // initialize NFD
    NFD::Guard nfdGuard;

    // auto-freeing memory
    NFD::UniquePath outPath;

    // show the dialog
    nfdresult_t result = NFD::PickFolder(outPath);
    if (result == NFD_OKAY) 
	{
        mDir = std::string(outPath.get());
		success = true;
    }
	else if (result == NFD_CANCEL) 
	{
        LL_INFOS() << "User pressed cancel." << LL_ENDL;
    } 
	else 
	{
        LL_INFOS() << "DirPicker Error: " << NFD::GetError() << LL_ENDL;
    }

	if (blocking)
	{
		send_agent_resume();

		// Account for the fact that the app has been stalled.
		LLFrameTimer::updateFrameTime();
	}

	return success;
}

std::string LLDirPicker::getDirName()
{
	return mDir;
}

#elif LL_WINDOWS

LLDirPicker::LLDirPicker() :
	mFileName(NULL),
	mLocked(false)
{
	bi.hwndOwner = NULL;
	bi.pidlRoot = NULL;
	bi.pszDisplayName = NULL;
	bi.lpszTitle = NULL;
	bi.ulFlags = BIF_USENEWUI;
	bi.lpfn = NULL;
	bi.lParam = NULL;
	bi.iImage = 0;
}

LLDirPicker::~LLDirPicker()
{
	// nothing
}

template <typename T>
struct Release_Guard {
    T* data;
    Release_Guard(T* releasable) noexcept : data(releasable) {}
    ~Release_Guard() { data->Release(); }
};

BOOL LLDirPicker::getDir(std::string* filename, bool blocking)
{
	if( mLocked )
	{
		return FALSE;
	}

	// if local file browsing is turned off, return without opening dialog
	if ( check_local_file_access_enabled() == false )
	{
		return FALSE;
	}

	BOOL success = FALSE;

	
	if (blocking)
	{
		// Modal, so pause agent
		send_agent_pause();
	}

	CoInitialize(0);

	::IFileOpenDialog* fileOpenDialog;

	// Create dialog
	if (SUCCEEDED(::CoCreateInstance(::CLSID_FileOpenDialog,
		nullptr,
		CLSCTX_ALL,
		::IID_IFileOpenDialog,
		reinterpret_cast<void**>(&fileOpenDialog))))
	{
		Release_Guard<::IFileOpenDialog> fileOpenDialogGuard(fileOpenDialog);

		FILEOPENDIALOGOPTIONS existingOptions;
		if (SUCCEEDED(fileOpenDialog->GetOptions(&existingOptions)))
		{
			if (SUCCEEDED(fileOpenDialog->SetOptions(existingOptions | (::FOS_FORCEFILESYSTEM | ::FOS_PICKFOLDERS))))
			{
				// Show the dialog to the user
				const HRESULT result = fileOpenDialog->Show(nullptr);
				if (result != HRESULT_FROM_WIN32(ERROR_CANCELLED) && SUCCEEDED(result))
				{
					// Get the shell item result
					::IShellItem* psiResult;
					if (SUCCEEDED(fileOpenDialog->GetResult(&psiResult)))
					{
						Release_Guard<::IShellItem> psiResultGuard(psiResult);
						wchar_t* filePath;
						if (SUCCEEDED(psiResult->GetDisplayName(::SIGDN_FILESYSPATH, &filePath)))
						{
							mDir = ll_convert_wide_to_string(std::wstring(filePath));
							success = TRUE;
						}
					}
				}
			}
		}
	}

	CoUninitialize();

	if (blocking)
	{
		send_agent_resume();
	}

	// Account for the fact that the app has been stalled.
	LLFrameTimer::updateFrameTime();
	return success;
}

std::string LLDirPicker::getDirName()
{
	return mDir;
}

/////////////////////////////////////////////DARWIN
#elif LL_DARWIN

LLDirPicker::LLDirPicker() :
mFileName(NULL),
mLocked(false)
{
	mFilePicker = new LLFilePicker();
	reset();
}

LLDirPicker::~LLDirPicker()
{
	delete mFilePicker;
}

void LLDirPicker::reset()
{
	if (mFilePicker)
		mFilePicker->reset();
}


//static
BOOL LLDirPicker::getDir(std::string* filename, bool blocking)
{
    LLFilePicker::ELoadFilter filter=LLFilePicker::FFLOAD_DIRECTORY;
    
    return mFilePicker->getOpenFile(filter, true);
}

std::string LLDirPicker::getDirName()
{
	return mFilePicker->getFirstFile();
}

#elif LL_LINUX

LLDirPicker::LLDirPicker() :
	mFileName(NULL),
	mLocked(false)
{
	mFilePicker = new LLFilePicker();
	reset();
}

LLDirPicker::~LLDirPicker()
{
	delete mFilePicker;
}


void LLDirPicker::reset()
{
	if (mFilePicker)
		mFilePicker->reset();
}

BOOL LLDirPicker::getDir(std::string* filename, bool blocking)
{
	reset();

	// if local file browsing is turned off, return without opening dialog
	if ( check_local_file_access_enabled() == false )
	{
		return FALSE;
	}

#if !LL_MESA_HEADLESS

	if (mFilePicker)
	{
		GtkWindow* picker = mFilePicker->buildFilePicker(false, true,
								 "dirpicker");

		if (picker)
		{		   
		   gtk_window_set_title(GTK_WINDOW(picker), LLTrans::getString("choose_the_directory").c_str());
		   gtk_widget_show_all(GTK_WIDGET(picker));
		   gtk_main();
		   return (!mFilePicker->getFirstFile().empty());
		}
	}
#endif // !LL_MESA_HEADLESS

	return FALSE;
}

std::string LLDirPicker::getDirName()
{
	if (mFilePicker)
	{
		return mFilePicker->getFirstFile();
	}
	return "";
}

#else // not implemented

LLDirPicker::LLDirPicker() 
{
	reset();
}

LLDirPicker::~LLDirPicker()
{
}


void LLDirPicker::reset()
{
}

BOOL LLDirPicker::getDir(std::string* filename, bool blocking)
{
	return FALSE;
}

std::string LLDirPicker::getDirName()
{
	return "";
}

#endif


LLMutex* LLDirPickerThread::sMutex = NULL;
std::queue<LLDirPickerThread*> LLDirPickerThread::sDeadQ;

void LLDirPickerThread::getFile()
{
#if (LL_WINDOWS && !LL_NFD) || (LL_LINUX && LL_NFD)
	start();
#else
	run();
#endif
}

//virtual 
void LLDirPickerThread::run()
{
#if (LL_WINDOWS && !LL_NFD) || (LL_LINUX && LL_NFD)
	bool blocking = false;
#else
	bool blocking = true; // modal
#endif

	LLDirPicker picker;

	if (picker.getDir(&mProposedName, blocking))
	{
		mResponses.push_back(picker.getDirName());
	}	

	{
		LLMutexLock lock(sMutex);
		sDeadQ.push(this);
	}

}

//static
void LLDirPickerThread::initClass()
{
	sMutex = new LLMutex();
}

//static
void LLDirPickerThread::cleanupClass()
{
	clearDead();

	delete sMutex;
	sMutex = NULL;
}

//static
void LLDirPickerThread::clearDead()
{
	if (!sDeadQ.empty())
	{
		LLMutexLock lock(sMutex);
		while (!sDeadQ.empty())
		{
			LLDirPickerThread* thread = sDeadQ.front();
			thread->notify(thread->mResponses);
			delete thread;
			sDeadQ.pop();
		}
	}
}

LLDirPickerThread::LLDirPickerThread(const dir_picked_signal_t::slot_type& cb, const std::string &proposed_name)
	: LLThread("dir picker"),
	mFilePickedSignal(NULL)
{
	mFilePickedSignal = new dir_picked_signal_t();
	mFilePickedSignal->connect(cb);
}

LLDirPickerThread::~LLDirPickerThread()
{
	delete mFilePickedSignal;
}

void LLDirPickerThread::notify(const std::vector<std::string>& filenames)
{
	if (!filenames.empty())
	{
		if (mFilePickedSignal)
		{
			(*mFilePickedSignal)(filenames, mProposedName);
		}
	}
}
