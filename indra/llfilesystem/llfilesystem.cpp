/**
 * @file filesystem.h
 * @brief Simulate local file system operations.
 * @Note The initial implementation does actually use standard C++
 *       file operations but eventually, there will be another
 *       layer that caches and manages file meta data too.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "linden_common.h"

#include "lldir.h"
#include "llfilesystem.h"
#include "llfasttimer.h"
#include "lldiskcache.h"

static LLTrace::BlockTimerStatHandle FTM_VFILE_WAIT("VFile Wait");

LLFileSystem::LLFileSystem(const LLUUID& file_id, const LLAssetType::EType file_type, S32 mode)
{
    mFileType = file_type;
    mFileID = file_id;
    mPosition = 0;
    mBytesRead = 0;
    mMode = mode;
}

// static
bool LLFileSystem::getExists(const LLUUID& file_id, const LLAssetType::EType file_type)
{
    const std::string filename = LLDiskCache::getInstance()->metaDataToFilepath(file_id, file_type);

    llifstream file(filename, std::ios::binary);
    if (file.is_open())
    {
        file.seekg(0, std::ios::end);
        return file.tellg() > 0;
    }
    return false;
}

// static
bool LLFileSystem::removeFile(const LLUUID& file_id, const LLAssetType::EType file_type)
{
    const std::string filename =  LLDiskCache::getInstance()->metaDataToFilepath(file_id, file_type);

    LLFile::remove(filename.c_str());

    return true;
}

// static
bool LLFileSystem::renameFile(const LLUUID& old_file_id, const LLAssetType::EType old_file_type,
                              const LLUUID& new_file_id, const LLAssetType::EType new_file_type)
{
    const std::string old_filename =  LLDiskCache::getInstance()->metaDataToFilepath(old_file_id, old_file_type);
    const std::string new_filename =  LLDiskCache::getInstance()->metaDataToFilepath(new_file_id, new_file_type);

    // Rename needs the new file to not exist.
    LLFileSystem::removeFile(new_file_id, new_file_type);

    if (LLFile::rename(old_filename, new_filename) != 0)
    {
        // We would like to return FALSE here indicating the operation
        // failed but the original code does not and doing so seems to
        // break a lot of things so we go with the flow...
        //return FALSE;
        LL_WARNS() << "Failed to rename " << old_file_id << " to " << new_file_id << " reason: "  << strerror(errno) << LL_ENDL;
    }

    return TRUE;
}

// static
S32 LLFileSystem::getFileSize(const LLUUID& file_id, const LLAssetType::EType file_type)
{
    const std::string filename = LLDiskCache::getInstance()->metaDataToFilepath(file_id, file_type);

    S32 file_size = 0;
    llifstream file(filename, std::ios::binary);
    if (file.is_open())
    {
        file.seekg(0, std::ios::end);
        file_size = file.tellg();
    }

    return file_size;
}

BOOL LLFileSystem::read(U8* buffer, S32 bytes)
{
    BOOL success = TRUE;

    const std::string filename = LLDiskCache::getInstance()->metaDataToFilepath(mFileID, mFileType);

    llifstream file(filename, std::ios::binary);
    if (file.is_open())
    {
        file.seekg(mPosition, std::ios::beg);

        file.read((char*)buffer, bytes);

        if (file)
        {
            mBytesRead = bytes;
        }
        else
        {
            mBytesRead = file.gcount();
        }

        file.close();

        mPosition += mBytesRead;
        if (!mBytesRead)
        {
            success = FALSE;
        }
    }

    // update the last access time for the file - this is required
    // even though we are reading and not writing because this is the
    // way the cache works - it relies on a valid "last accessed time" for
    // each file so it knows how to remove the oldest, unused files
    LLDiskCache::getInstance()->updateFileAccessTime(filename);

    return success;
}

S32 LLFileSystem::getLastBytesRead()
{
    return mBytesRead;
}

BOOL LLFileSystem::eof()
{
    return mPosition >= getSize();
}

BOOL LLFileSystem::write(const U8* buffer, S32 bytes)
{
    const std::string filename =  LLDiskCache::getInstance()->metaDataToFilepath(mFileID, mFileType);

    BOOL success = FALSE;

    if (mMode == APPEND)
    {
        llofstream ofs(filename, std::ios::app | std::ios::binary);
        if (ofs)
        {
            ofs.write((const char*)buffer, bytes);

            success = TRUE;
        }
    }
    else
    {
        llofstream ofs(filename, std::ios::binary);
        if (ofs)
        {
            ofs.write((const char*)buffer, bytes);

            mPosition += bytes;

            success = TRUE;
        }
    }

    return success;
}

BOOL LLFileSystem::seek(S32 offset, S32 origin)
{
    if (-1 == origin)
    {
        origin = mPosition;
    }

    S32 new_pos = origin + offset;

    S32 size = getSize();

    if (new_pos > size)
    {
        LL_WARNS() << "Attempt to seek past end of file" << LL_ENDL;

        mPosition = size;
        return FALSE;
    }
    else if (new_pos < 0)
    {
        LL_WARNS() << "Attempt to seek past beginning of file" << LL_ENDL;

        mPosition = 0;
        return FALSE;
    }

    mPosition = new_pos;
    return TRUE;
}

S32 LLFileSystem::tell() const
{
    return mPosition;
}

S32 LLFileSystem::getSize()
{
    return LLFileSystem::getFileSize(mFileID, mFileType);
}

S32 LLFileSystem::getMaxSize()
{
    // offer up a huge size since we don't care what the max is
    return INT_MAX;
}

BOOL LLFileSystem::rename(const LLUUID& new_id, const LLAssetType::EType new_type)
{
    LLFileSystem::renameFile(mFileID, mFileType, new_id, new_type);

    mFileID = new_id;
    mFileType = new_type;

    return TRUE;
}

BOOL LLFileSystem::remove()
{
    LLFileSystem::removeFile(mFileID, mFileType);

    return TRUE;
}
