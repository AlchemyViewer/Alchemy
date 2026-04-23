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

#include "llwin32headers.h"
#include "apr_thread_proc.h"

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
    LLAPRPool(apr_pool_t *parent = NULL, apr_size_t size = 0, bool releasePoolFlag = true) ;
    virtual ~LLAPRPool() ;

    virtual apr_pool_t* getAPRPool() ;
    apr_status_t getStatus() {return mStatus ; }

protected:
    void releaseAPRPool() ;
    void createAPRPool() ;

protected:
    apr_pool_t*  mPool ;              //pointing to an apr_pool
    apr_pool_t*  mParent ;            //parent pool
    apr_size_t   mMaxSize ;           //max size of mPool, mPool should return memory to system if allocated memory beyond this limit. However it seems not to work.
    apr_status_t mStatus ;            //status when creating the pool
    bool         mReleasePoolFlag ;   //if set, mPool is destroyed when LLAPRPool is deleted. default value is true.
};

#endif // LL_LLAPR_H
