/**
 * @file llfilepicker.cpp
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

#include "llfilepicker.h"
#include "llworld.h"
#include "llviewerwindow.h"
#include "llkeyboard.h"
#include "lldir.h"
#include "llframetimer.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llwindow.h"   // beforeDialog()

#if LL_SDL
#include "llwindowsdl.h"
#endif // LL_SDL

#if LL_LINUX
#include "llhttpconstants.h"    // file picker uses some of thes constants on Linux
#endif

#if LL_NFD
#include "nfd.hpp"
#endif

//
// Globals
//

LLFilePicker LLFilePicker::sInstance;

#if LL_WINDOWS && !LL_NFD
#define SOUND_FILTER L"Sounds (*.wav)\0*.wav\0"
#define IMAGE_FILTER L"Images (*.tga; *.bmp; *.jpg; *.jpeg; *.png; *.webp)\0*.tga;*.bmp;*.jpg;*.jpeg;*.png;*.webp\0"
#define ANIM_FILTER L"Animations (*.bvh; *.anim)\0*.bvh;*.anim\0"
#define COLLADA_FILTER L"Scene (*.dae)\0*.dae\0"
#define GLTF_FILTER L"glTF (*.gltf; *.glb)\0*.gltf;*.glb\0"
#define XML_FILTER L"XML files (*.xml)\0*.xml\0"
#define SLOBJECT_FILTER L"Objects (*.slobject)\0*.slobject\0"
#define RAW_FILTER L"RAW files (*.raw)\0*.raw\0"
#define MODEL_FILTER L"Model files (*.dae)\0*.dae\0"
#define MATERIAL_FILTER L"GLTF Files (*.gltf; *.glb)\0*.gltf;*.glb\0"
#define HDRI_FILTER L"HDRI Files (*.exr)\0*.exr\0"
#define MATERIAL_TEXTURES_FILTER L"GLTF Import (*.gltf; *.glb; *.tga; *.bmp; *.jpg; *.jpeg; *.png)\0*.gltf;*.glb;*.tga;*.bmp;*.jpg;*.jpeg;*.png\0"
#define SCRIPT_FILTER L"Script files (*.lsl)\0*.lsl\0"
#define DICTIONARY_FILTER L"Dictionary files (*.dic; *.xcu)\0*.dic;*.xcu\0"
#define ZIP_FILTER L"ZIP files (*.zip)\0*.zip\0"
#define EXECUTABLE_FILTER L"Executables (*.exe)\0*.exe\0"
#endif

#ifdef LL_DARWIN
#include "llfilepicker_mac.h"
//#include <boost/algorithm/string/predicate.hpp>
#endif

//
// Implementation
//
LLFilePicker::LLFilePicker()
    : mCurrentFile(0),
      mLocked(false)

{
    reset();

#if LL_WINDOWS && !LL_NFD
    mOFN.lStructSize = sizeof(OPENFILENAMEW);
    mOFN.hwndOwner = NULL;  // Set later
    mOFN.hInstance = NULL;
    mOFN.lpstrCustomFilter = NULL;
    mOFN.nMaxCustFilter = 0;
    mOFN.lpstrFile = NULL;                          // set in open and close
    mOFN.nMaxFile = LL_MAX_PATH;
    mOFN.lpstrFileTitle = NULL;
    mOFN.nMaxFileTitle = 0;
    mOFN.lpstrInitialDir = NULL;
    mOFN.lpstrTitle = NULL;
    mOFN.Flags = 0;                                 // set in open and close
    mOFN.nFileOffset = 0;
    mOFN.nFileExtension = 0;
    mOFN.lpstrDefExt = NULL;
    mOFN.lCustData = 0L;
    mOFN.lpfnHook = NULL;
    mOFN.lpTemplateName = NULL;
    mFilesW[0] = '\0';
#elif LL_DARWIN
    mPickOptions = 0;
#endif

}

LLFilePicker::~LLFilePicker()
{
    // nothing
}

// utility function to check if access to local file system via file browser
// is enabled and if not, tidy up and indicate we're not allowed to do this.
bool LLFilePicker::check_local_file_access_enabled()
{
    // if local file browsing is turned off, return without opening dialog
    bool local_file_system_browsing_enabled = gSavedSettings.getBOOL("LocalFileSystemBrowsingEnabled");
    if ( ! local_file_system_browsing_enabled )
    {
        mFiles.clear();
        return false;
    }

    return true;
}

const std::string LLFilePicker::getFirstFile()
{
    mCurrentFile = 0;
    return getNextFile();
}

const std::string LLFilePicker::getNextFile()
{
    if (mCurrentFile >= getFileCount())
    {
        mLocked = false;
        return std::string();
    }
    else
    {
        return mFiles[mCurrentFile++];
    }
}

const std::string LLFilePicker::getCurFile()
{
    if (mCurrentFile >= getFileCount())
    {
        mLocked = false;
        return std::string();
    }
    else
    {
        return mFiles[mCurrentFile];
    }
}

void LLFilePicker::reset()
{
    mLocked = false;
    mFiles.clear();
    mCurrentFile = 0;
}

#if LL_NFD
std::vector<nfdfilteritem_t> LLFilePicker::setupFilter(ELoadFilter filter)
{
    std::vector<nfdfilteritem_t> filter_vec;
    switch (filter)
    {
    case FFLOAD_ALL:
        break;
    case FFLOAD_EXE:
#if LL_WINDOWS
        filter_vec.emplace_back(nfdfilteritem_t{"Executables", "exe"});
#endif
        break;
    case FFLOAD_WAV:
        filter_vec.emplace_back(nfdfilteritem_t{"Sounds", "wav"});
        break;
    case FFLOAD_IMAGE:
        filter_vec.emplace_back(nfdfilteritem_t{"Images", "tga,bmp,jpg,jpeg,png,webp"});
        break;
    case FFLOAD_ANIM:
        filter_vec.emplace_back(nfdfilteritem_t{"Animations", "bvh,anim"});
        break;
    case FFLOAD_GLTF:
        filter_vec.emplace_back(nfdfilteritem_t{"GLTF Files", "gltf,glb"});
        break;
    case FFLOAD_COLLADA:
        filter_vec.emplace_back(nfdfilteritem_t{"Scene", "dae"});
        break;
    case FFLOAD_XML:
        filter_vec.emplace_back(nfdfilteritem_t{"XML files", "xml"});
        break;
    case FFLOAD_SLOBJECT:
        filter_vec.emplace_back(nfdfilteritem_t{"Objects", "slobject"});
        break;
    case FFLOAD_RAW:
        filter_vec.emplace_back(nfdfilteritem_t{"RAW files", "raw"});
        break;
    case FFLOAD_MODEL:
        filter_vec.emplace_back(nfdfilteritem_t{"Model files", "dae"});
        break;
    case FFLOAD_MATERIAL:
        filter_vec.emplace_back(nfdfilteritem_t{"GLTF Files", "gltf,glb"});
        break;
    case FFLOAD_MATERIAL_TEXTURE:
        filter_vec.emplace_back(nfdfilteritem_t{"GLTF Import", "gltf,glb,tga,bmp,jpg,jpeg,png"});
        filter_vec.emplace_back(nfdfilteritem_t{"GLTF Files", "gltf,glb"});
        filter_vec.emplace_back(nfdfilteritem_t{"Images", "tga,bmp,jpg,jpeg,png,webp"});
        break;
    case FFLOAD_SCRIPT:
        filter_vec.emplace_back(nfdfilteritem_t{"Script files", "lsl"});
        break;
    case FFLOAD_DICTIONARY:
        filter_vec.emplace_back(nfdfilteritem_t{"Dictionary files", "dic,xcu"});
        break;
    case FFLOAD_ZIP:
        filter_vec.emplace_back(nfdfilteritem_t{"ZIP files", "zip"});
    default:
        break;
    }
    return filter_vec;
}

BOOL LLFilePicker::getOpenFile(ELoadFilter filter, bool blocking)
{
    if( mLocked )
    {
        return FALSE;
    }
    BOOL success = FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    // initialize NFD
    NFD::Guard nfdGuard;

    // auto-freeing memory
    NFD::UniquePath outPath;

    // prepare filters for the dialog
    auto filterItem = setupFilter(filter);
    //
    if (blocking)
    {
        // Modal, so pause agent
        send_agent_pause();
    }

    reset();

    // show the dialog
    nfdresult_t result = NFD::OpenDialog(outPath, filterItem.data(), filterItem.size());
    if (result == NFD_OKAY)
    {
        mFiles.push_back(outPath.get());
        success = TRUE;
    }

    if (blocking)
    {
        send_agent_resume();
        // Account for the fact that the app has been stalled.
        LLFrameTimer::updateFrameTime();
    }

    return success;
}

BOOL LLFilePicker::getOpenFileModeless(ELoadFilter filter,
                                       void (*callback)(bool, std::vector<std::string> &, void*),
                                       void *userdata)
{
    if( mLocked )
        return FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();
    LL_WARNS() << "NOT IMPLEMENTED" << LL_ENDL;
    return FALSE;
}

BOOL LLFilePicker::getMultipleOpenFiles(ELoadFilter filter, bool blocking)
{
    if( mLocked )
    {
        return FALSE;
    }
    BOOL success = FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    // initialize NFD
    NFD::Guard nfdGuard;

    auto filterItem = setupFilter(filter);

    reset();

    if (blocking)
    {
        // Modal, so pause agent
        send_agent_pause();
    }

    // auto-freeing memory
    NFD::UniquePathSet outPaths;

    // show the dialog
    nfdresult_t result = NFD::OpenDialogMultiple(outPaths, filterItem.data(), filterItem.size());
    if (result == NFD_OKAY)
    {
        LL_INFOS() << "Success!" << LL_ENDL;

        nfdpathsetsize_t numPaths;
        NFD::PathSet::Count(outPaths, numPaths);

        nfdpathsetsize_t i;
        for (i = 0; i < numPaths; ++i)
        {
            NFD::UniquePathSetPath path;
            NFD::PathSet::GetPath(outPaths, i, path);
            mFiles.push_back(path.get());
            LL_INFOS() << "Path " << i << ": " << path.get() << LL_ENDL;
        }
        success = TRUE;
    }
    else if (result == NFD_CANCEL)
    {
        LL_INFOS() << "User pressed cancel." << LL_ENDL;
    }
    else
    {
        LL_INFOS() << "Error: " << NFD::GetError() << LL_ENDL;
    }

    if (blocking)
    {
        send_agent_resume();

        // Account for the fact that the app has been stalled.
        LLFrameTimer::updateFrameTime();
    }

    return success;
}

BOOL LLFilePicker::getMultipleOpenFilesModeless(ELoadFilter filter,
                                                void (*callback)(bool, std::vector<std::string> &, void*),
                                                void *userdata )
{
    if( mLocked )
        return FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();

    LL_WARNS() << "NOT IMPLEMENTED" << LL_ENDL;
    return FALSE;
}

BOOL LLFilePicker::getSaveFile(ESaveFilter filter, const std::string& filename, bool blocking)
{
    if( mLocked )
    {
        return FALSE;
    }
    BOOL success = FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    // initialize NFD
    NFD::Guard nfdGuard;

    std::vector<nfdfilteritem_t> filter_vec;
    std::string saved_filename = filename;
    switch( filter )
    {
    case FFSAVE_ALL:
        filter_vec.emplace_back(nfdfilteritem_t{"WAV Sounds", "wav"});
        filter_vec.emplace_back(nfdfilteritem_t{"Targa, Bitmap Images", "tga,bmp"});
        break;
    case FFSAVE_WAV:
        if (filename.empty())
        {
            saved_filename = "untitled.wav";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"WAV Sounds", "wav"});
        break;
    case FFSAVE_TGA:
        if (filename.empty())
        {
            saved_filename = "untitled.tga";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"Targa Images", "tga"});
        break;
    case FFSAVE_BMP:
        if (filename.empty())
        {
            saved_filename = "untitled.bmp";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"Bitmap Images", "bmp"});
        break;
    case FFSAVE_PNG:
        if (filename.empty())
        {
            saved_filename = "untitled.png";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"PNG Images", "png"});
        break;
    case FFSAVE_WEBP:
        if (filename.empty())
        {
            saved_filename = "untitled.webp";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"WebP Images", "webp"});
        break;
    case FFSAVE_TGAPNGWEBP:
        if (filename.empty())
        {
            saved_filename = "untitled.png";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"PNG Images", "png"});
        filter_vec.emplace_back(nfdfilteritem_t{"Targa Images", "tga"});
        filter_vec.emplace_back(nfdfilteritem_t{"Jpeg Images", "jpg,jpeg"});
        filter_vec.emplace_back(nfdfilteritem_t{"Jpeg2000 Images", "j2c"});
        filter_vec.emplace_back(nfdfilteritem_t{"Bitmap Images", "bmp"});
        filter_vec.emplace_back(nfdfilteritem_t{"WebP Images", "webp"});
        break;
    case FFSAVE_JPEG:
        if (filename.empty())
        {
            saved_filename = "untitled.jpeg";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"Jpeg Images", "jpg,jpeg"});
        break;
    case FFSAVE_AVI:
        if (filename.empty())
        {
            saved_filename = "untitled.avi";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"AVI Movie File", "avi"});
        break;
    case FFSAVE_ANIM:
        if (filename.empty())
        {
            saved_filename = "untitled.xaf";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"XAF Anim File", "xaf"});
        break;
    case FFSAVE_CSV:
        if (filename.empty())
        {
            saved_filename = "untitled.csv";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"Comma seperated values", "csv"});
        break;
    case FFSAVE_XML:
        if (filename.empty())
        {
            saved_filename = "untitled.xml";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"XML File", "xml"});
        break;
    case FFSAVE_COLLADA:
        if (filename.empty())
        {
            saved_filename = "untitled.collada";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"COLLADA File", "collada"});
        break;
    case FFSAVE_RAW:
        if (filename.empty())
        {
            saved_filename = "untitled.raw";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"RAW files", "raw"});
        break;
    case FFSAVE_J2C:
        if (filename.empty())
        {
            saved_filename = "untitled.j2c";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"Compressed Images", "j2c"});
        break;
    case FFSAVE_SCRIPT:
        if (filename.empty())
        {
            saved_filename = "untitled.lsl";
        }
        filter_vec.emplace_back(nfdfilteritem_t{"LSL Files", "lsl"});
        break;
    default:
        return FALSE;
    }

    reset();

    if (blocking)
    {
        // Modal, so pause agent
        send_agent_pause();
    }

    {
        NFD::UniquePath savePath;

        // show the dialog
        nfdresult_t result = NFD::SaveDialog(savePath, filter_vec.data(), filter_vec.size(), NULL, saved_filename.c_str());
        if (result == NFD_OKAY) {
            mFiles.push_back(savePath.get());
            success = TRUE;
        }
        gKeyboard->resetKeys();
    }

    if (blocking)
    {
        send_agent_resume();

        // Account for the fact that the app has been stalled.
        LLFrameTimer::updateFrameTime();
    }

    return success;
}

BOOL LLFilePicker::getSaveFileModeless(ESaveFilter filter,
                                       const std::string& filename,
                                       void (*callback)(bool, std::string&, void*),
                                       void *userdata)
{
    if( mLocked )
        return FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();
    LL_WARNS() << "NOT IMPLEMENTED" << LL_ENDL;
    return FALSE;
}
#elif LL_WINDOWS

BOOL LLFilePicker::setupFilter(ELoadFilter filter)
{
    BOOL res = TRUE;
    switch (filter)
    {
    case FFLOAD_ALL:
        mOFN.lpstrFilter = L"All Files (*.*)\0*.*\0" \
        SOUND_FILTER \
        IMAGE_FILTER \
        ANIM_FILTER \
        MATERIAL_FILTER \
        L"\0";
        break;
    case FFLOAD_EXE:
        mOFN.lpstrFilter = EXECUTABLE_FILTER \
            L"\0";
        break;
    case FFLOAD_WAV:
        mOFN.lpstrFilter = SOUND_FILTER \
            L"\0";
        break;
    case FFLOAD_IMAGE:
        mOFN.lpstrFilter = IMAGE_FILTER \
            L"\0";
        break;
    case FFLOAD_ANIM:
        mOFN.lpstrFilter = ANIM_FILTER \
            L"\0";
        break;
    case FFLOAD_GLTF:
        mOFN.lpstrFilter = GLTF_FILTER \
            L"\0";
        break;
    case FFLOAD_COLLADA:
        mOFN.lpstrFilter = COLLADA_FILTER \
            L"\0";
        break;
    case FFLOAD_XML:
        mOFN.lpstrFilter = XML_FILTER \
            L"\0";
        break;
    case FFLOAD_SLOBJECT:
        mOFN.lpstrFilter = SLOBJECT_FILTER \
            L"\0";
        break;
    case FFLOAD_RAW:
        mOFN.lpstrFilter = RAW_FILTER \
            L"\0";
        break;
    case FFLOAD_MODEL:
        mOFN.lpstrFilter = MODEL_FILTER \
            L"\0";
        break;
    case FFLOAD_MATERIAL:
        mOFN.lpstrFilter = MATERIAL_FILTER \
            L"\0";
        break;
    case FFLOAD_MATERIAL_TEXTURE:
        mOFN.lpstrFilter = MATERIAL_TEXTURES_FILTER \
            MATERIAL_FILTER \
            IMAGE_FILTER \
            L"\0";
        break;
    case FFLOAD_HDRI:
        mOFN.lpstrFilter = HDRI_FILTER \
            L"\0";
        break;
    case FFLOAD_SCRIPT:
        mOFN.lpstrFilter = SCRIPT_FILTER \
            L"\0";
        break;
    case FFLOAD_DICTIONARY:
        mOFN.lpstrFilter = DICTIONARY_FILTER \
            L"\0";
        break;
    case FFLOAD_ZIP:
        mOFN.lpstrFilter = ZIP_FILTER \
            L"\0";
        break;
    default:
        res = FALSE;
        break;
    }
    return res;
}

BOOL LLFilePicker::getOpenFile(ELoadFilter filter, bool blocking)
{
    if( mLocked )
    {
        return FALSE;
    }
    BOOL success = FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    // don't provide default file selection
    mFilesW[0] = '\0';

    mOFN.hwndOwner = (HWND)gViewerWindow->getPlatformWindow();
    mOFN.lpstrFile = mFilesW;
    mOFN.nMaxFile = SINGLE_FILENAME_BUFFER_SIZE;
    mOFN.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR ;
    mOFN.nFilterIndex = 1;

    setupFilter(filter);

    if (blocking)
    {
        // Modal, so pause agent
        send_agent_pause();
    }

    reset();

    // NOTA BENE: hitting the file dialog triggers a window focus event, destroying the selection manager!!
    success = GetOpenFileName(&mOFN);
    if (success)
    {
        std::string filename = ll_convert_wide_to_string(mFilesW);
        mFiles.push_back(filename);
    }

    if (blocking)
    {
        send_agent_resume();
        // Account for the fact that the app has been stalled.
        LLFrameTimer::updateFrameTime();
    }

    return success;
}

BOOL LLFilePicker::getOpenFileModeless(ELoadFilter filter,
                                       void (*callback)(bool, std::vector<std::string> &, void*),
                                       void *userdata)
{
    // not supposed to be used yet, use LLFilePickerThread
    LL_ERRS() << "NOT IMPLEMENTED" << LL_ENDL;
    return FALSE;
}

BOOL LLFilePicker::getMultipleOpenFiles(ELoadFilter filter, bool blocking)
{
    if( mLocked )
    {
        return FALSE;
    }
    BOOL success = FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    // don't provide default file selection
    mFilesW[0] = '\0';

    mOFN.hwndOwner = (HWND)gViewerWindow->getPlatformWindow();
    mOFN.lpstrFile = mFilesW;
    mOFN.nFilterIndex = 1;
    mOFN.nMaxFile = FILENAME_BUFFER_SIZE;
    mOFN.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR |
        OFN_EXPLORER | OFN_ALLOWMULTISELECT;

    setupFilter(filter);

    reset();

    if (blocking)
    {
        // Modal, so pause agent
        send_agent_pause();
    }

    // NOTA BENE: hitting the file dialog triggers a window focus event, destroying the selection manager!!
    success = GetOpenFileName(&mOFN); // pauses until ok or cancel.
    if( success )
    {
        // The getopenfilename api doesn't tell us if we got more than
        // one file, so we have to test manually by checking string
        // lengths.
        if( wcslen(mOFN.lpstrFile) > mOFN.nFileOffset ) /*Flawfinder: ignore*/
        {
            std::string filename = ll_convert_wide_to_string(mFilesW);
            mFiles.push_back(filename);
        }
        else
        {
            mLocked = true;
            WCHAR* tptrw = mFilesW;
            std::string dirname;
            while(1)
            {
                if (*tptrw == 0 && *(tptrw+1) == 0) // double '\0'
                    break;
                if (*tptrw == 0)
                    tptrw++; // shouldn't happen?
                std::string filename = ll_convert_wide_to_string(tptrw);
                if (dirname.empty())
                    dirname = filename + "\\";
                else
                    mFiles.push_back(dirname + filename);
                tptrw += wcslen(tptrw);
            }
        }
    }

    if (blocking)
    {
        send_agent_resume();
    }

    // Account for the fact that the app has been stalled.
    LLFrameTimer::updateFrameTime();
    return success;
}

BOOL LLFilePicker::getMultipleOpenFilesModeless(ELoadFilter filter,
                                                void (*callback)(bool, std::vector<std::string> &, void*),
                                                void *userdata )
{
    // not supposed to be used yet, use LLFilePickerThread
    LL_ERRS() << "NOT IMPLEMENTED" << LL_ENDL;
    return FALSE;
}

BOOL LLFilePicker::getSaveFile(ESaveFilter filter, const std::string& filename, bool blocking)
{
    if( mLocked )
    {
        return FALSE;
    }
    BOOL success = FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    mOFN.lpstrFile = mFilesW;
    if (!filename.empty())
    {
        std::wstring tstring = ll_convert_string_to_wide(filename);
        wcsncpy(mFilesW, tstring.c_str(), FILENAME_BUFFER_SIZE);    }   /*Flawfinder: ignore*/
    else
    {
        mFilesW[0] = '\0';
    }
    mOFN.hwndOwner = (HWND)gViewerWindow->getPlatformWindow();

    switch( filter )
    {
    case FFSAVE_ALL:
        mOFN.lpstrDefExt = NULL;
        mOFN.lpstrFilter =
            L"All Files (*.*)\0*.*\0" \
            L"WAV Sounds (*.wav)\0*.wav\0" \
            L"Targa, Bitmap Images (*.tga; *.bmp)\0*.tga;*.bmp\0" \
            L"\0";
        break;
    case FFSAVE_WAV:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.wav", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
        }
        mOFN.lpstrDefExt = L"wav";
        mOFN.lpstrFilter =
            L"WAV Sounds (*.wav)\0*.wav\0" \
            L"\0";
        break;
    case FFSAVE_TGA:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.tga", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
        }
        mOFN.lpstrDefExt = L"tga";
        mOFN.lpstrFilter =
            L"Targa Images (*.tga)\0*.tga\0" \
            L"\0";
        break;
    case FFSAVE_BMP:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.bmp", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
        }
        mOFN.lpstrDefExt = L"bmp";
        mOFN.lpstrFilter =
            L"Bitmap Images (*.bmp)\0*.bmp\0" \
            L"\0";
        break;
    case FFSAVE_PNG:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.png", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
        }
        mOFN.lpstrDefExt = L"png";
        mOFN.lpstrFilter =
            L"PNG Images (*.png)\0*.png\0" \
            L"\0";
        break;
    case FFSAVE_WEBP:
        if (filename.empty())
        {
            wcsncpy(mFilesW, L"untitled.webp", FILENAME_BUFFER_SIZE);   /*Flawfinder: ignore*/
        }
        mOFN.lpstrDefExt = L"webp";
        mOFN.lpstrFilter =
            L"WebP Images (*.webp)\0*.webp\0" \
            L"\0";
        break;
    case FFSAVE_TGAPNGWEBP:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.png", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
            //PNG by default
        }
        mOFN.lpstrDefExt = L"png";
        mOFN.lpstrFilter =
            L"PNG Images (*.png)\0*.png\0" \
            L"Targa Images (*.tga)\0*.tga\0" \
            L"Jpeg Images (*.jpg)\0*.jpg\0" \
            L"Jpeg2000 Images (*.j2c)\0*.j2c\0" \
            L"Bitmap Images (*.bmp)\0*.bmp\0" \
            L"WebP Images (*.webp)\0*.webp\0" \
            L"\0";
        break;

    case FFSAVE_JPEG:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.jpeg", FILENAME_BUFFER_SIZE);   /*Flawfinder: ignore*/
        }
        mOFN.lpstrDefExt = L"jpg";
        mOFN.lpstrFilter =
            L"JPEG Images (*.jpg *.jpeg)\0*.jpg;*.jpeg\0" \
            L"\0";
        break;
    case FFSAVE_AVI:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.avi", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
        }
        mOFN.lpstrDefExt = L"avi";
        mOFN.lpstrFilter =
            L"AVI Movie File (*.avi)\0*.avi\0" \
            L"\0";
        break;
    case FFSAVE_ANIM:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.xaf", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
        }
        mOFN.lpstrDefExt = L"xaf";
        mOFN.lpstrFilter =
            L"XAF Anim File (*.xaf)\0*.xaf\0" \
            L"\0";
        break;
    case FFSAVE_GLTF:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.glb", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
        }
        mOFN.lpstrDefExt = L"glb";
        mOFN.lpstrFilter =
            L"glTF Asset File (*.gltf *.glb)\0*.gltf;*.glb\0" \
            L"\0";
        break;
    case FFSAVE_CSV:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.csv", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
        }

        mOFN.lpstrDefExt = L"csv";
        mOFN.lpstrFilter =
            L"Comma seperated values (*.csv)\0*.csv\0" \
            L"\0";
        break;
    case FFSAVE_XML:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.xml", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
        }

        mOFN.lpstrDefExt = L"xml";
        mOFN.lpstrFilter =
            L"XML File (*.xml)\0*.xml\0" \
            L"\0";
        break;
    case FFSAVE_COLLADA:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.collada", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
        }
        mOFN.lpstrDefExt = L"collada";
        mOFN.lpstrFilter =
            L"COLLADA File (*.collada)\0*.collada\0" \
            L"\0";
        break;
    case FFSAVE_RAW:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.raw", FILENAME_BUFFER_SIZE);    /*Flawfinder: ignore*/
        }
        mOFN.lpstrDefExt = L"raw";
        mOFN.lpstrFilter =  RAW_FILTER \
                            L"\0";
        break;
    case FFSAVE_J2C:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.j2c", FILENAME_BUFFER_SIZE);
        }
        mOFN.lpstrDefExt = L"j2c";
        mOFN.lpstrFilter =
            L"Compressed Images (*.j2c)\0*.j2c\0" \
            L"\0";
        break;
    case FFSAVE_SCRIPT:
        if (filename.empty())
        {
            wcsncpy( mFilesW,L"untitled.lsl", FILENAME_BUFFER_SIZE);
        }
        mOFN.lpstrDefExt = L"txt";
        mOFN.lpstrFilter = L"LSL Files (*.lsl)\0*.lsl\0" L"\0";
        break;
    default:
        return FALSE;
    }


    mOFN.nMaxFile = SINGLE_FILENAME_BUFFER_SIZE;
    mOFN.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;

    reset();

    if (blocking)
    {
        // Modal, so pause agent
        send_agent_pause();
    }

    {
        // NOTA BENE: hitting the file dialog triggers a window focus event, destroying the selection manager!!
        try
        {
            success = GetSaveFileName(&mOFN);
            if (success)
            {
                std::string filename = ll_convert_wide_to_string(mFilesW);
                mFiles.push_back(filename);
            }
        }
        catch (...)
        {
            LOG_UNHANDLED_EXCEPTION("");
        }
        gKeyboard->resetKeys();
    }

    if (blocking)
    {
        send_agent_resume();
    }

    // Account for the fact that the app has been stalled.
    LLFrameTimer::updateFrameTime();
    return success;
}

BOOL LLFilePicker::getSaveFileModeless(ESaveFilter filter,
                                       const std::string& filename,
                                       void (*callback)(bool, std::string&, void*),
                                       void *userdata)
{
    // not supposed to be used yet, use LLFilePickerThread
    LL_ERRS() << "NOT IMPLEMENTED" << LL_ENDL;
    return FALSE;
}

#elif LL_DARWIN

std::unique_ptr<std::vector<std::string>> LLFilePicker::navOpenFilterProc(ELoadFilter filter) //(AEDesc *theItem, void *info, void *callBackUD, NavFilterModes filterMode)
{
    std::unique_ptr<std::vector<std::string>> allowedv(new std::vector< std::string >);
    switch(filter)
    {
        case FFLOAD_ALL:
        case FFLOAD_EXE:
            allowedv->emplace_back("app");
            allowedv->emplace_back("exe");
            allowedv->emplace_back("wav");
            allowedv->emplace_back("bvh");
            allowedv->emplace_back("anim");
            allowedv->emplace_back("dae");
            allowedv->emplace_back("raw");
            allowedv->emplace_back("lsl");
            allowedv->emplace_back("dic");
            allowedv->emplace_back("xcu");
            allowedv->emplace_back("gif");
            allowedv->emplace_back("gltf");
            allowedv->emplace_back("glb");
        case FFLOAD_IMAGE:
            allowedv->emplace_back("jpg");
            allowedv->emplace_back("jpeg");
            allowedv->emplace_back("bmp");
            allowedv->emplace_back("tga");
            allowedv->emplace_back("bmpf");
            allowedv->emplace_back("tpic");
            allowedv->emplace_back("png");
            allowedv->emplace_back("webp");
            break;
            break;
        case FFLOAD_WAV:
            allowedv->emplace_back("wav");
            break;
        case FFLOAD_ANIM:
            allowedv->emplace_back("bvh");
            allowedv->emplace_back("anim");
            break;
        case FFLOAD_GLTF:
        case FFLOAD_MATERIAL:
            allowedv->push_back("gltf");
            allowedv->push_back("glb");
            break;
        case FFLOAD_HDRI:
            allowedv->push_back("exr");
        case FFLOAD_COLLADA:
            allowedv->emplace_back("dae");
            break;
        case FFLOAD_XML:
            allowedv->emplace_back("xml");
            break;
        case FFLOAD_RAW:
            allowedv->emplace_back("raw");
            break;
        case FFLOAD_SCRIPT:
            allowedv->emplace_back("lsl");
            break;
        case FFLOAD_DICTIONARY:
            allowedv->emplace_back("dic");
            allowedv->emplace_back("xcu");
            break;
        case FFLOAD_ZIP:
            allowedv->emplace_back("zip");
            break;
        case FFLOAD_DIRECTORY:
            break;
        default:
            LL_WARNS() << "Unsupported format." << LL_ENDL;
    }

    return allowedv;
}

bool    LLFilePicker::doNavChooseDialog(ELoadFilter filter)
{
    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return false;
    }

    gViewerWindow->getWindow()->beforeDialog();

    std::unique_ptr<std::vector<std::string>> allowed_types = navOpenFilterProc(filter);

    std::unique_ptr<std::vector<std::string>> filev  = doLoadDialog(allowed_types.get(),
                                                    mPickOptions);

    gViewerWindow->getWindow()->afterDialog();

    if (filev && filev->size() > 0)
    {
        mFiles.insert(mFiles.end(), filev->begin(), filev->end());
        return true;
    }

    return false;
}

bool    LLFilePicker::doNavChooseDialogModeless(ELoadFilter filter,
                                                void (*callback)(bool, std::vector<std::string> &,void*),
                                                void *userdata)
{
    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return false;
    }

    std::unique_ptr<std::vector<std::string>> allowed_types=navOpenFilterProc(filter);

    doLoadDialogModeless(allowed_types.get(),
                                                    mPickOptions,
                                                    callback,
                                                    userdata);

    return true;
}

void set_nav_save_data(LLFilePicker::ESaveFilter filter, std::string &extension, std::string &type, std::string &creator)
{
    switch (filter)
    {
        case LLFilePicker::FFSAVE_WAV:
            extension = "wav";
            break;
        case LLFilePicker::FFSAVE_TGA:
            extension = "tga";
            break;
        case LLFilePicker::FFSAVE_TGAPNGWEBP:
            extension = "png,tga,jpg,jpeg,j2c,bmp,bmpf,webp";
            break;
        case LLFilePicker::FFSAVE_BMP:
            extension = "bmp,bmpf";
            break;
        case LLFilePicker::FFSAVE_JPEG:
            extension = "jpeg";
            break;
        case LLFilePicker::FFSAVE_PNG:
            extension = "png";
            break;
        case LLFilePicker::FFSAVE_WEBP:
            extension = "webp";
            break;
        case LLFilePicker::FFSAVE_AVI:
            extension = "mov";
            break;

        case LLFilePicker::FFSAVE_ANIM:
            extension = "xaf";
            break;
        case LLFilePicker::FFSAVE_GLTF:
            extension = "glb";
            break;

        case LLFilePicker::FFSAVE_XML:
            extension = "xml";
            break;
        case LLFilePicker::FFSAVE_CSV:
            extension = "csv";
            break;
        case LLFilePicker::FFSAVE_RAW:
            extension = "raw";
            break;

        case LLFilePicker::FFSAVE_J2C:
            extension = "j2c";
            break;

        case LLFilePicker::FFSAVE_SCRIPT:
            extension = "lsl";
            break;

        case LLFilePicker::FFSAVE_ALL:
        default:
            extension = "";
            break;
    }
}

bool    LLFilePicker::doNavSaveDialog(ESaveFilter filter, const std::string& filename)
{
    // Setup the type, creator, and extension
    std::string     extension, type, creator;

    set_nav_save_data(filter, extension, type, creator);

    std::string namestring = filename;
    if (namestring.empty()) namestring="Untitled";

    gViewerWindow->getWindow()->beforeDialog();

    // Run the dialog
    std::unique_ptr<std::string> filev = doSaveDialog(&namestring,
                 &extension,
                 mPickOptions);

    gViewerWindow->getWindow()->afterDialog();

    if ( filev && !filev->empty() )
    {
        mFiles.push_back(*filev);
        return true;
    }

    return false;
}

bool    LLFilePicker::doNavSaveDialogModeless(ESaveFilter filter,
                                              const std::string& filename,
                                              void (*callback)(bool, std::string&, void*),
                                              void *userdata)
{
    // Setup the type, creator, and extension
    std::string        extension, type, creator;

    set_nav_save_data(filter, extension, type, creator);

    std::string namestring = filename;
    if (namestring.empty()) namestring="Untitled";

    // Run the dialog
    doSaveDialogModeless(&namestring,
                 &type,
                 &creator,
                 &extension,
                 mPickOptions,
                 callback,
                 userdata);
    return true;
}

BOOL LLFilePicker::getOpenFile(ELoadFilter filter, bool blocking)
{
    if( mLocked )
        return FALSE;

    BOOL success = FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();

    mPickOptions &= ~F_MULTIPLE;
    mPickOptions |= F_FILE;

    if (filter == FFLOAD_DIRECTORY) //This should only be called from lldirpicker.
    {

        mPickOptions |= ( F_NAV_SUPPORT | F_DIRECTORY );
        mPickOptions &= ~F_FILE;
    }

    if (filter == FFLOAD_ALL)   // allow application bundles etc. to be traversed; important for DEV-16869, but generally useful
    {
        mPickOptions |= F_NAV_SUPPORT;
    }

    if (blocking) // always true for linux/mac
    {
        // Modal, so pause agent
        send_agent_pause();
    }


    success = doNavChooseDialog(filter);

    if (success)
    {
        if (!getFileCount())
            success = false;
    }

    if (blocking)
    {
        send_agent_resume();
        // Account for the fact that the app has been stalled.
        LLFrameTimer::updateFrameTime();
    }

    return success;
}


BOOL LLFilePicker::getOpenFileModeless(ELoadFilter filter,
                                       void (*callback)(bool, std::vector<std::string> &, void*),
                                       void *userdata)
{
    if( mLocked )
        return FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();

    mPickOptions &= ~F_MULTIPLE;
    mPickOptions |= F_FILE;

    if (filter == FFLOAD_DIRECTORY) //This should only be called from lldirpicker.
    {

        mPickOptions |= ( F_NAV_SUPPORT | F_DIRECTORY );
        mPickOptions &= ~F_FILE;
    }

    if (filter == FFLOAD_ALL)    // allow application bundles etc. to be traversed; important for DEV-16869, but generally useful
    {
        mPickOptions |= F_NAV_SUPPORT;
    }

    return doNavChooseDialogModeless(filter, callback, userdata);
}

BOOL LLFilePicker::getMultipleOpenFiles(ELoadFilter filter, bool blocking)
{
    if( mLocked )
        return FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    BOOL success = FALSE;

    reset();

    mPickOptions |= F_FILE;

    mPickOptions |= F_MULTIPLE;

    if (blocking) // always true for linux/mac
    {
        // Modal, so pause agent
        send_agent_pause();
    }

    success = doNavChooseDialog(filter);

    if (blocking)
    {
        send_agent_resume();
    }

    if (success)
    {
        if (!getFileCount())
            success = false;
        if (getFileCount() > 1)
            mLocked = true;
    }

    // Account for the fact that the app has been stalled.
    LLFrameTimer::updateFrameTime();
    return success;
}


BOOL LLFilePicker::getMultipleOpenFilesModeless(ELoadFilter filter,
                                                void (*callback)(bool, std::vector<std::string> &, void*),
                                                void *userdata )
{
    if( mLocked )
        return FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();

    mPickOptions |= F_FILE;

    mPickOptions |= F_MULTIPLE;

    return doNavChooseDialogModeless(filter, callback, userdata);
}

BOOL LLFilePicker::getSaveFile(ESaveFilter filter, const std::string& filename, bool blocking)
{

    if( mLocked )
        return false;
    BOOL success = false;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return false;
    }

    reset();

    mPickOptions &= ~F_MULTIPLE;

    if (blocking)
    {
        // Modal, so pause agent
        send_agent_pause();
    }

    success = doNavSaveDialog(filter, filename);

    if (success)
    {
        if (!getFileCount())
            success = false;
    }

    if (blocking)
    {
        send_agent_resume();
    }

    // Account for the fact that the app has been stalled.
    LLFrameTimer::updateFrameTime();
    return success;
}

BOOL LLFilePicker::getSaveFileModeless(ESaveFilter filter,
                                       const std::string& filename,
                                       void (*callback)(bool, std::string&, void*),
                                       void *userdata)
{
    if( mLocked )
        return false;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return false;
    }

    reset();

    mPickOptions &= ~F_MULTIPLE;

    return doNavSaveDialogModeless(filter, filename, callback, userdata);
}
//END LL_DARWIN

#elif LL_LINUX

// Hacky stubs designed to facilitate fake getSaveFile and getOpenFile with
// static results, when we don't have a real filepicker.

BOOL LLFilePicker::getSaveFile( ESaveFilter filter, const std::string& filename, bool blocking )
{
    // if local file browsing is turned off, return without opening dialog
    // (Even though this is a stub, I think we still should not return anything at all)
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();

    LL_INFOS() << "getSaveFile suggested filename is [" << filename
        << "]" << LL_ENDL;
    if (!filename.empty())
    {
        mFiles.push_back(gDirUtilp->getLindenUserDir() + gDirUtilp->getDirDelimiter() + filename);
        return TRUE;
    }
    return FALSE;
}

BOOL LLFilePicker::getSaveFileModeless(ESaveFilter filter,
                                       const std::string& filename,
                                       void (*callback)(bool, std::string&, void*),
                                       void *userdata)
{
    if( mLocked )
        return FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();
    LL_WARNS() << "NOT IMPLEMENTED" << LL_ENDL;
    return FALSE;
}

BOOL LLFilePicker::getOpenFile( ELoadFilter filter, bool blocking )
{
    if( mLocked )
        return FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();

    // HACK: Static filenames for 'open' until we implement filepicker
    std::string filename = gDirUtilp->getLindenUserDir() + gDirUtilp->getDirDelimiter() + "upload";
    switch (filter)
    {
    case FFLOAD_WAV: filename += ".wav"; break;
    case FFLOAD_IMAGE: filename += ".tga"; break;
    case FFLOAD_ANIM: filename += ".bvh"; break;
    default: break;
    }
    mFiles.push_back(filename);
    LL_INFOS() << "getOpenFile: Will try to open file: " << filename << LL_ENDL;
    return TRUE;
}

BOOL LLFilePicker::getOpenFileModeless(ELoadFilter filter,
                                       void (*callback)(bool, std::vector<std::string> &, void*),
                                       void *userdata)
{
    if( mLocked )
        return FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();
    LL_WARNS() << "NOT IMPLEMENTED" << LL_ENDL;
    return FALSE;
}

BOOL LLFilePicker::getMultipleOpenFiles( ELoadFilter filter, bool blocking)
{
    if( mLocked )
        return FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();
    return FALSE;
}

BOOL LLFilePicker::getMultipleOpenFilesModeless(ELoadFilter filter,
                                                void (*callback)(bool, std::vector<std::string> &, void*),
                                                void *userdata )
{
    if( mLocked )
        return FALSE;

    // if local file browsing is turned off, return without opening dialog
    if ( check_local_file_access_enabled() == false )
    {
        return FALSE;
    }

    reset();

    LL_WARNS() << "NOT IMPLEMENTED" << LL_ENDL;
    return FALSE;
}

#else // not implemented

BOOL LLFilePicker::getSaveFile( ESaveFilter filter, const std::string& filename )
{
    reset();
    return FALSE;
}

BOOL LLFilePicker::getOpenFile( ELoadFilter filter )
{
    reset();
    return FALSE;
}

BOOL LLFilePicker::getMultipleOpenFiles( ELoadFilter filter, bool blocking)
{
    reset();
    return FALSE;
}

#endif // LL_LINUX
