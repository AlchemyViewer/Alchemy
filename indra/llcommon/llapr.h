/** 
 * @file llapr.h
 * @author Phoenix
 * @date 2004-11-28
 * @brief Helper functions for using the apache portable runtime library.
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
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

#ifndef LL_LLAPR_H
#define LL_LLAPR_H

#if LL_LINUX
#include <sys/param.h>  // Need PATH_MAX in APR headers...
#endif

#include <boost/noncopyable.hpp>
#include "llwin32headerslean.h"
#include "apr_thread_proc.h"
#include "apr_getopt.h"
#include "apr_signal.h"

#include "llstring.h"

#include "absl/synchronization/mutex.h"

struct apr_dso_handle_t;
/**
 * @brief Function which appropriately logs error or remains quiet on
 * APR_SUCCESS.
 * @return Returns <code>true</code> if status is an error condition.
 */
#define ll_apr_warn_status(status) _ll_apr_warn_status(status, __FILE__, __LINE__)
bool LL_COMMON_API _ll_apr_warn_status(apr_status_t status, const char* file, int line);

#define ll_apr_assert_status(status) _ll_apr_assert_status(status, __FILE__, __LINE__)
void LL_COMMON_API _ll_apr_assert_status(apr_status_t status, const char* file, int line);

extern "C" LL_COMMON_API apr_pool_t* gAPRPoolp; // Global APR memory pool

/** 
 * @brief initialize the common apr constructs -- apr itself, the
 * global pool, and a mutex.
 */
void LL_COMMON_API ll_init_apr();

/** 
 * @brief Cleanup those common apr constructs.
 */
void LL_COMMON_API ll_cleanup_apr();

bool LL_COMMON_API ll_apr_is_initialized();


//
//LL apr_pool
//manage apr_pool_t, destroy allocated apr_pool in the destruction function.
//
class LL_COMMON_API LLAPRPool
{
public:
	LLAPRPool(apr_pool_t *parent = nullptr, apr_size_t size = 0, BOOL releasePoolFlag = TRUE) ;
	virtual ~LLAPRPool() ;

	virtual apr_pool_t* getAPRPool() ;
	apr_status_t getStatus() {return mStatus ; }

protected:
	void releaseAPRPool() ;
	void createAPRPool() ;

protected:
	apr_pool_t*  mPool ;              //pointing to an apr_pool
	apr_pool_t*  mParent ;			  //parent pool
	apr_size_t   mMaxSize ;           //max size of mPool, mPool should return memory to system if allocated memory beyond this limit. However it seems not to work.
	apr_status_t mStatus ;            //status when creating the pool
	BOOL         mReleasePoolFlag ;   //if set, mPool is destroyed when LLAPRPool is deleted. default value is true.
};

//
//volatile LL apr_pool
//which clears memory automatically.
//so it can not hold static data or data after memory is cleared
//
class LL_COMMON_API LLVolatileAPRPool final : public LLAPRPool
{
public:
	LLVolatileAPRPool(std::string name, BOOL is_local = TRUE, apr_pool_t *parent = nullptr, apr_size_t size = 0, BOOL releasePoolFlag = TRUE);
	virtual ~LLVolatileAPRPool();

	/*virtual*/ apr_pool_t* getAPRPool() override; //define this virtual function to avoid any mistakenly calling LLAPRPool::getAPRPool().
	apr_pool_t* getVolatileAPRPool() ;	
	void        clearVolatileAPRPool() ;

	BOOL        isFull() ;
	
private:
	std::string mName;

	S32 mNumActiveRef ; //number of active pointers pointing to the apr_pool.
	S32 mNumTotalRef ;  //number of total pointers pointing to the apr_pool since last creating.  

	std::unique_ptr<absl::Mutex> mMutexp;
} ;

// File IO convenience functions.
// Returns NULL if the file fails to open, sets *sizep to file size if not NULL
// abbreviated flags
#define LL_APR_R (APR_READ) // "r"
#define LL_APR_W (APR_CREATE|APR_TRUNCATE|APR_WRITE) // "w"
#define LL_APR_A (APR_CREATE|APR_WRITE|APR_APPEND) // "w"
#define LL_APR_RB (APR_READ|APR_BINARY) // "rb"
#define LL_APR_WB (APR_CREATE|APR_TRUNCATE|APR_WRITE|APR_BINARY) // "wb"
#define LL_APR_AB (APR_CREATE|APR_WRITE|APR_BINARY|APR_APPEND)
#define LL_APR_RPB (APR_READ|APR_WRITE|APR_BINARY) // "r+b"
#define LL_APR_WPB (APR_CREATE|APR_TRUNCATE|APR_READ|APR_WRITE|APR_BINARY) // "w+b"

//
//apr_file manager
//which: 1)only keeps one file open;
//       2)closes the open file in the destruction function
//       3)informs the apr_pool to clean the memory when the file is closed.
//Note: please close an open file at the earliest convenience. 
//      especially do not put some time-costly operations between open() and close().
//      otherwise it might lock the APRFilePool.
//there are two different apr_pools the APRFile can use:
//      1, a temporary pool passed to an APRFile function, which is used within this function and only once.
//      2, a global pool.
//

class LL_COMMON_API LLAPRFile
{
private:
	apr_file_t* mFile ;
	LLVolatileAPRPool *mCurrentFilePoolp ; //currently in use apr_pool, could be one of them: sAPRFilePoolp, or a temp pool. 

public:
	LLAPRFile() ;
	LLAPRFile(const std::string& filename, apr_int32_t flags, LLVolatileAPRPool* pool = nullptr);
	~LLAPRFile() ;
	
	// make this non copyable since a copy closes the file
	LLAPRFile(const LLAPRFile&) = delete;
	LLAPRFile& operator=(const LLAPRFile&) = delete;
	
	apr_status_t open(const std::string& filename, apr_int32_t flags, LLVolatileAPRPool* pool = nullptr, apr_off_t* sizep = nullptr);
	apr_status_t open(const std::string& filename, apr_int32_t flags, BOOL use_global_pool); //use gAPRPoolp.
	apr_status_t close() ;

	// Returns actual offset, -1 if seek fails
	apr_off_t seek(apr_seek_where_t where, apr_off_t offset);
	apr_status_t eof() { return apr_file_eof(mFile);}

	// Returns bytes read/written, 0 if read/write fails:
	apr_size_t read(void* buf, apr_size_t nbytes);
	apr_size_t write(const void* buf, apr_size_t nbytes);
	
	apr_file_t* getFileHandle() {return mFile;}	
	
//
//*******************************************************************************************************************************
//static components
//
public:
	static LLVolatileAPRPool *sAPRFilePoolp ; //a global apr_pool for APRFile, which is used only when local pool does not exist.

private:
	static apr_file_t* open(const std::string& filename, apr_pool_t* apr_pool, apr_int32_t flags);
	static apr_status_t close(apr_file_t* file) ;
	static apr_off_t seek(apr_file_t* file, apr_seek_where_t where, apr_off_t offset);
public:
	// returns false if failure:
	static bool remove(const std::string& filename, LLVolatileAPRPool* pool = nullptr);
	static bool rename(const std::string& filename, const std::string& newname, LLVolatileAPRPool* pool = nullptr);
	static bool isExist(const std::string& filename, LLVolatileAPRPool* pool = nullptr, apr_int32_t flags = APR_READ);
	static apr_off_t size(const std::string& filename, LLVolatileAPRPool* pool = nullptr);
	static bool makeDir(const std::string& dirname, LLVolatileAPRPool* pool = nullptr);
	static bool removeDir(const std::string& dirname, LLVolatileAPRPool* pool = nullptr);

	// Returns bytes read/written, 0 if read/write fails:
	static apr_size_t readEx(const std::string& filename, void *buf, apr_off_t offset, apr_size_t nbytes, LLVolatileAPRPool* pool = nullptr);
	static apr_size_t writeEx(const std::string& filename, void *buf, apr_off_t offset, apr_size_t nbytes, LLVolatileAPRPool* pool = nullptr); // offset<0 means append
//*******************************************************************************************************************************
};


#endif // LL_LLAPR_H
