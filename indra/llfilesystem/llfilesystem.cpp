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

const S32 LLFileSystem::READ        = 0x00000001;
const S32 LLFileSystem::WRITE       = 0x00000002;
const S32 LLFileSystem::READ_WRITE  = 0x00000003;  // LLFileSystem::READ & LLFileSystem::WRITE
const S32 LLFileSystem::APPEND      = 0x00000006;  // 0x00000004 & LLFileSystem::WRITE

LLFileSystem::LLFileSystem(const LLUUID& file_id, const LLAssetType::EType file_type, S32 mode)
{
    // build the filename (TODO: we do this in a few places - perhaps we should factor into a single function)
    mFilePath = LLDiskCache::metaDataToFilepath(file_id, file_type);
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
        // update the last access time for the file if it exists - this is required
        // even though we are reading and not writing because this is the
        // way the cache works - it relies on a valid "last accessed time" for
        // each file so it knows how to remove the oldest, unused files
        boost::system::error_code ec;
        bool exists = boost::filesystem::exists(mFilePath, ec);
        if (exists && !ec.failed())
        {
            LLDiskCache::updateFileAccessTime(mFilePath);
        }
    }
}

LLFileSystem::~LLFileSystem()
{
}

// static
bool LLFileSystem::getExists(const LLUUID& file_id, const LLAssetType::EType file_type)
{
    const boost::filesystem::path filename = LLDiskCache::metaDataToFilepath(file_id, file_type);
    boost::system::error_code ec;
    return boost::filesystem::exists(filename, ec) && !ec.failed();
}

// static
bool LLFileSystem::removeFile(const LLUUID& file_id, const LLAssetType::EType file_type, int suppress_error /*= 0*/)
{
    const boost::filesystem::path filename = LLDiskCache::metaDataToFilepath(file_id, file_type);

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
    const boost::filesystem::path filename = LLDiskCache::metaDataToFilepath(file_id, file_type);
    boost::system::error_code ec;
    S32 file_size = boost::filesystem::file_size(filename, ec);
    if(ec.failed())
    {
        return 0;
    }
    return file_size;
}

BOOL LLFileSystem::read(U8* buffer, S32 bytes)
{
    BOOL success = FALSE;

    LLFILE* file = LLFile::fopen(mFilePath, TEXT("rb"));
    if (file)
    {
        if (fseek(file, mPosition, SEEK_SET) == 0)
        {
            mBytesRead = fread(buffer, 1, bytes, file);
            fclose(file);

            mPosition += mBytesRead;
            // It probably would be correct to check for mBytesRead == bytes,
            // but that will break avatar rezzing...
            if (mBytesRead)
            {
                success = TRUE;
            }
        }
    }


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
    BOOL success = FALSE;

    if (mMode == APPEND)
    {
        LLFILE* ofs = LLFile::fopen(mFilePath, TEXT("a+b"));
        if (ofs)
        {
            S32 bytes_written = fwrite(buffer, 1, bytes, ofs);
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
                S32 bytes_written = fwrite(buffer, 1, bytes, ofs);
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
                S32 bytes_written = fwrite(buffer, 1, bytes, ofs);
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
            S32 bytes_written = fwrite(buffer, 1, bytes, ofs);
            mPosition = ftell(ofs);
            fclose(ofs);
            success = (bytes_written == bytes);
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
    boost::system::error_code ec;
    S32 file_size = boost::filesystem::file_size(mFilePath, ec);
    if(ec.failed())
    {
        return 0;
    }
    return file_size;
}

S32 LLFileSystem::getMaxSize()
{
    // offer up a huge size since we don't care what the max is
    return INT_MAX;
}

BOOL LLFileSystem::rename(const LLUUID& new_id, const LLAssetType::EType new_type)
{
    const boost::filesystem::path new_filename = LLDiskCache::metaDataToFilepath(new_id, new_type);

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
        // We would like to return FALSE here indicating the operation
        // failed but the original code does not and doing so seems to
        // break a lot of things so we go with the flow...
        //return FALSE;
        LL_WARNS() << "Failed to rename " << mFileID << " to " << new_id << " reason: "  << ec.what() << LL_ENDL;
    }

    mFileID = new_id;
    mFileType = new_type;
    mFilePath = new_filename;

    return TRUE;
}

BOOL LLFileSystem::remove()
{
    boost::system::error_code ec;
    boost::filesystem::remove(mFilePath, ec);
    return TRUE;
}
