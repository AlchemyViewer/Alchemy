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

#include "boost/filesystem.hpp"
const S32 LLFileSystem::READ        = 0x00000001;
const S32 LLFileSystem::WRITE       = 0x00000002;
const S32 LLFileSystem::READ_WRITE  = 0x00000003;  // LLFileSystem::READ & LLFileSystem::WRITE
const S32 LLFileSystem::APPEND      = 0x00000006;  // 0x00000004 & LLFileSystem::WRITE

static LLTrace::BlockTimerStatHandle FTM_VFILE_WAIT("VFile Wait");
LLFileSystem::LLFileSystem(const LLUUID& file_id, const LLAssetType::EType file_type, S32 mode)
{
    // build the filename (TODO: we do this in a few places - perhaps we should factor into a single function)
    mFilePath = LLDiskCache::getInstance()->metaDataToFilepath(file_id, file_type);
    mFileType = file_type;
    mFileID = file_id;
    mPosition = 0;
    mBytesRead = 0;
    mMode = mode;

    // This block of code was originally called in the read() method but after comments here:
    // https://bitbucket.org/lindenlab/viewer/commits/e28c1b46e9944f0215a13cab8ee7dded88d7fc90#comment-10537114
    // we decided to follow Henri's suggestion and move the code to update the last access time here.
    if (mode == LLFileSystem::READ)
    {
        const std::string filename = LLDiskCache::metaDataToFilepath(mFileID, mFileType);
        // update the last access time for the file if it exists - this is required
        // even though we are reading and not writing because this is the
        // way the cache works - it relies on a valid "last accessed time" for
        // each file so it knows how to remove the oldest, unused files
        bool exists = gDirUtilp->fileExists(filename);
        if (exists)
        {
            updateFileAccessTime(filename);
        }
    }
}

// static
bool LLFileSystem::getExists(const LLUUID& file_id, const LLAssetType::EType file_type)
{
    LL_PROFILE_ZONE_SCOPED;
    const boost::filesystem::path filename = LLDiskCache::getInstance()->metaDataToFilepath(file_id, file_type);
    boost::system::error_code ec;
    return boost::filesystem::exists(filename, ec) && !ec.failed();
}

// static
bool LLFileSystem::removeFile(const LLUUID& file_id, const LLAssetType::EType file_type, int suppress_error /*= 0*/)
{
    const boost::filesystem::path filename = LLDiskCache::getInstance()->metaDataToFilepath(file_id, file_type);

    LLFile::remove(filename, suppress_error);

    return true;
}

// static
bool LLFileSystem::renameFile(const LLUUID& old_file_id, const LLAssetType::EType old_file_type,
                              const LLUUID& new_file_id, const LLAssetType::EType new_file_type)
{
    LLFileSystem old_file(old_file_id, old_file_type);
    return old_file.rename(new_file_id, new_file_type);
}

// static
S32 LLFileSystem::getFileSize(const LLUUID& file_id, const LLAssetType::EType file_type)
{
    const boost::filesystem::path filename = LLDiskCache::getInstance()->metaDataToFilepath(file_id, file_type);
    boost::system::error_code ec;
    S32 file_size = boost::filesystem::file_size(filename, ec);
    if(ec.failed())
    {
        return 0;
    }
    return file_size;
}

bool LLFileSystem::read(U8* buffer, S32 bytes)
{
    bool success = false;

    LLFILE* file = LLFile::fopen(mFilePath, TEXT("rb"));
    if (file)
    {
        if (fseek(file, mPosition, SEEK_SET) == 0)
        {
            mBytesRead = (S32)fread(buffer, 1, bytes, file);
            fclose(file);

            mPosition += mBytesRead;
            // It probably would be correct to check for mBytesRead == bytes,
            // but that will break avatar rezzing...
            if (mBytesRead)
            {
                success = true;
            }
        }
    }


    return success;
}

S32 LLFileSystem::getLastBytesRead() const
{
    return mBytesRead;
}

bool LLFileSystem::eof() const
{
    return mPosition >= getSize();
}

bool LLFileSystem::write(const U8* buffer, S32 bytes)
{
    bool success = false;

    if (mMode == APPEND)
    {
        LLFILE* ofs = LLFile::fopen(mFilePath, TEXT("a+b"));
        if (ofs)
        {
            S32 bytes_written = (S32)fwrite(buffer, 1, bytes, ofs);
            mPosition = ftell(ofs);
            fclose(ofs);
            success = (bytes_written == bytes);
        }
    }
    else if (mMode == READ_WRITE)
    {
        LLFILE* ofs = LLFile::fopen(mFilePath, TEXT("r+b"));
        if (ofs)
        {
            if (fseek(ofs, mPosition, SEEK_SET) == 0)
            {
                S32 bytes_written = (S32)fwrite(buffer, 1, bytes, ofs);
                mPosition = ftell(ofs);
                fclose(ofs);
                success = (bytes_written == bytes);
            }
        }
        else
        {
            ofs = LLFile::fopen(mFilePath, TEXT("wb"));
            if (ofs)
            {
                S32 bytes_written = (S32)fwrite(buffer, 1, bytes, ofs);
                mPosition = ftell(ofs);
                fclose(ofs);
                success = (bytes_written == bytes);
            }
        }
    }
    else
    {
        LLFILE* ofs = LLFile::fopen(mFilePath, TEXT("wb"));
        if (ofs)
        {
            S32 bytes_written = (S32)fwrite(buffer, 1, bytes, ofs);
            mPosition = ftell(ofs);
            fclose(ofs);
            success = (bytes_written == bytes);
        }
    }


    return success;
}

bool LLFileSystem::seek(S32 offset, S32 origin)
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
        return false;
    }
    else if (new_pos < 0)
    {
        LL_WARNS() << "Attempt to seek past beginning of file" << LL_ENDL;

        mPosition = 0;
        return false;
    }

    mPosition = new_pos;
    return true;
}

S32 LLFileSystem::tell() const
{
    return mPosition;
}

S32 LLFileSystem::getSize()
{
    boost::system::error_code ec;
    S32 file_size = boost::filesystem::file_size(mFilePath, ec);
    if(ec.failed())
    {
        return 0;
    }
    return file_size;
}

S32 LLFileSystem::getMaxSize() const
{
    // offer up a huge size since we don't care what the max is
    return INT_MAX;
}

bool LLFileSystem::rename(const LLUUID& new_id, const LLAssetType::EType new_type)
{
    const boost::filesystem::path new_filename = LLDiskCache::getInstance()->metaDataToFilepath(new_id, new_type);

    // Rename needs the new file to not exist.
    boost::system::error_code ec;
    boost::filesystem::remove(new_filename, ec);
    if(ec.failed())
    {
        //LL_WARNS() << "Failed to remove existing file " << new_filename << " reason: " << ec.what() << LL_ENDL;
        ec.clear();
    }

    boost::filesystem::rename(mFilePath, new_filename, ec);
    if (ec.failed())
    {
        // We would like to return false here indicating the operation
        // failed but the original code does not and doing so seems to
        // break a lot of things so we go with the flow...
        //return false;
        LL_WARNS() << "Failed to rename " << mFileID << " to " << new_id << " reason: "  << ec.what() << LL_ENDL;
    }

    mFileID = new_id;
    mFileType = new_type;
    mFilePath = new_filename;

    return true;
}

bool LLFileSystem::remove()
{
    boost::system::error_code ec;
    boost::filesystem::remove(mFilePath, ec);
    return true;
}

void LLFileSystem::updateFileAccessTime(const std::string& file_path)
{
    /**
     * Threshold in time_t units that is used to decide if the last access time
     * time of the file is updated or not. Added as a precaution for the concern
     * outlined in SL-14582  about frequent writes on older SSDs reducing their
     * lifespan. I think this is the right place for the threshold value - rather
     * than it being a pref - do comment on that Jira if you disagree...
     *
     * Let's start with 1 hour in time_t units and see how that unfolds
     */
    constexpr std::time_t time_threshold = 1 * 60 * 60;

    // current time
    const std::time_t cur_time = std::time(nullptr);

    boost::system::error_code ec;
#if LL_WINDOWS
    // file last write time
    const std::time_t last_write_time = boost::filesystem::last_write_time(utf8str_to_utf16str(file_path), ec);
    if (ec.failed())
    {
        LL_WARNS() << "Failed to read last write time for cache file " << file_path << ": " << ec.message() << LL_ENDL;
        return;
    }

    // delta between cur time and last time the file was written
    const std::time_t delta_time = cur_time - last_write_time;

    // we only write the new value if the time in time_threshold has elapsed
    // before the last one
    if (delta_time > time_threshold)
    {
        boost::filesystem::last_write_time(utf8str_to_utf16str(file_path), cur_time, ec);
    }
#else
    // file last write time
    const std::time_t last_write_time = boost::filesystem::last_write_time(file_path, ec);
    if (ec.failed())
    {
        LL_WARNS() << "Failed to read last write time for cache file " << file_path << ": " << ec.message() << LL_ENDL;
        return;
    }

    // delta between cur time and last time the file was written
    const std::time_t delta_time = cur_time - last_write_time;

    // we only write the new value if the time in time_threshold has elapsed
    // before the last one
    if (delta_time > time_threshold)
    {
        boost::filesystem::last_write_time(file_path, cur_time, ec);
    }
#endif

    if (ec.failed())
    {
        LL_WARNS() << "Failed to update last write time for cache file " << file_path << ": " << ec.message() << LL_ENDL;
    }
}
