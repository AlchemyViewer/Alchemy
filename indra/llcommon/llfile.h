/** 
 * @file llfile.h
 * @author Michael Schlachter
 * @date 2006-03-23
 * @brief Declaration of cross-platform POSIX file buffer and c++
 * stream classes.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

#ifndef LL_LLFILE_H
#define LL_LLFILE_H

/**
 * This class provides a cross platform interface to the filesystem.
 * Attempts to mostly mirror the POSIX style IO functions.
 */

typedef FILE	LLFILE;

#include <fstream>
#include <sys/stat.h>

#if LL_WINDOWS
// windows version of stat function and stat data structure are called _stat
typedef struct _stat	llstat;
#else
typedef struct stat		llstat;
#include <sys/types.h>
#endif

#ifndef S_ISREG
# define S_ISREG(x) (((x) & S_IFMT) == S_IFREG)
#endif

#ifndef S_ISDIR
# define S_ISDIR(x) (((x) & S_IFMT) == S_IFDIR)
#endif

#include "llstring.h" // safe char* -> std::string conversion

#include <boost/filesystem.hpp>

#if LL_WINDOWS
#define MODE_T const wchar_t*
#else
#define MODE_T const char*
#define TEXT(quote) quote
#endif

class LL_COMMON_API LLFile
{
public:
	// All these functions take UTF8 path/filenames.
	static	LLFILE* fopen(const char* filename, const char* accessmode);	/* Flawfinder: ignore */
	static	LLFILE*	fopen(const std::string& filename,const char* accessmode);	/* Flawfinder: ignore */
	static	LLFILE* fopen(const boost::filesystem::path& filename, MODE_T accessmode);	/* Flawfinder: ignore */
	static	LLFILE*	_fsopen(const std::string& filename,const char* accessmode,int	sharingFlag);

	static	int		close(LLFILE * file);

	// perms is a permissions mask like 0777 or 0700.  In most cases it will
	// be overridden by the user's umask.  It is ignored on Windows.
	// mkdir() considers "directory already exists" to be SUCCESS.
	static	int		mkdir(const std::string& filename, int perms = 0700);

	static	int		rmdir(const std::string& filename);
	static	int		remove(const char* filename, int supress_error = 0);
	static	int		remove(const std::string& filename, int supress_error = 0);
	static	int		remove(const boost::filesystem::path& filename, int supress_error = 0);
	static	int		rename(const std::string& filename,const std::string& newname, int supress_error = 0);
	static	int		rename(const boost::filesystem::path& filename, const boost::filesystem::path& newname, int supress_error = 0);
	static  bool	copy(const std::string& from, const std::string& to);
	static  bool	copy(const boost::filesystem::path& from, const boost::filesystem::path& to);

	static	int		stat(const std::string&	filename, llstat* file_status);
	static	int		stat(const boost::filesystem::path& filename, llstat* file_status);
	static	bool	isdir(const std::string&	filename);
	static	bool	isfile(const std::string&	filename);
	static	LLFILE *	_Fiopen(const std::string& filename, 
			std::ios::openmode mode);

	static  const char * tmpdir();
};

/// RAII class
class LLUniqueFile
{
public:
    // empty
    LLUniqueFile(): mFileHandle(nullptr) {}
    // wrap (e.g.) result of LLFile::fopen()
    LLUniqueFile(LLFILE* f): mFileHandle(f) {}
    // no copy
    LLUniqueFile(const LLUniqueFile&) = delete;
    // move construction
    LLUniqueFile(LLUniqueFile&& other)
    {
        mFileHandle = other.mFileHandle;
        other.mFileHandle = nullptr;
    }
    // The point of LLUniqueFile is to close on destruction.
    ~LLUniqueFile()
    {
        close();
    }

    // simple assignment
    LLUniqueFile& operator=(LLFILE* f)
    {
        close();
        mFileHandle = f;
        return *this;
    }
    // copy assignment deleted
    LLUniqueFile& operator=(const LLUniqueFile&) = delete;
    // move assignment
    LLUniqueFile& operator=(LLUniqueFile&& other)
    {
        close();
        std::swap(mFileHandle, other.mFileHandle);
        return *this;
    }

    // explicit close operation
    void close()
    {
        if (mFileHandle)
        {
            // in case close() throws, set mFileHandle null FIRST
            LLFILE* h{nullptr};
            std::swap(h, mFileHandle);
            LLFile::close(h);
        }
    }

    // detect whether the wrapped LLFILE is open or not
    explicit operator bool() const { return bool(mFileHandle); }
    bool operator!() { return ! mFileHandle; }

    // LLUniqueFile should be usable for any operation that accepts LLFILE*
    // (or FILE* for that matter)
    operator LLFILE*() const { return mFileHandle; }

private:
    LLFILE* mFileHandle;
};

#if LL_WINDOWS
/**
*  @brief  Wrapper for UTF16 path compatibility on windows operating systems
*/
template< typename BaseType, std::ios_base::openmode DEFAULT_MODE>
class LL_COMMON_API stream_wrapper : public BaseType {
public:

    stream_wrapper() = default;

    explicit stream_wrapper(char const* _Filename, std::ios_base::openmode _Mode = DEFAULT_MODE)
        : BaseType(ll_convert_string_to_wide(_Filename).c_str(), _Mode)
    {
    }

    explicit stream_wrapper(const std::string& _Filename, std::ios_base::openmode _Mode = DEFAULT_MODE)
        : BaseType(ll_convert_string_to_wide(_Filename), _Mode)
    {
    }

    void open(char const* _Filename, std::ios_base::openmode _Mode = DEFAULT_MODE) {
        BaseType::open(ll_convert_string_to_wide(_Filename).c_str(), _Mode);
    }

    void open(const std::string& _Filename, std::ios_base::openmode _Mode = DEFAULT_MODE) {
        BaseType::open(ll_convert_string_to_wide(_Filename), _Mode);
    }
};

typedef stream_wrapper<std::fstream, std::ios_base::in | std::ios_base::out > llfstream;
typedef stream_wrapper<std::ifstream, std::ios_base::in > llifstream;
typedef stream_wrapper<std::ofstream, std::ios_base::out | std::ios_base::trunc > llofstream;


/**
 * @breif filesize helpers.
 *
 * The file size helpers are not considered particularly efficient,
 * and should only be used for config files and the like -- not in a
 * loop.
 */
std::streamsize LL_COMMON_API llifstream_size(llifstream& fstr);
std::streamsize LL_COMMON_API llofstream_size(llofstream& fstr);

#else // ! LL_WINDOWS

// on non-windows, llifstream and llofstream are just mapped directly to the std:: equivalents
typedef std::fstream  llfstream;
typedef std::ifstream llifstream;
typedef std::ofstream llofstream;

#endif // LL_WINDOWS or ! LL_WINDOWS

#endif // not LL_LLFILE_H
