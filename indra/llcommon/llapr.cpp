/**
 * @file llapr.cpp
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

#include "linden_common.h"
#include "llapr.h"

apr_pool_t *gAPRPoolp = NULL; // Global APR memory pool

bool gAPRInitialized = false;

int abortfunc(int retcode)
{
    LL_WARNS("APR") << "Allocation failure in apr pool with code " << (S32)retcode << LL_ENDL;
    return 0;
}

void ll_init_apr()
{
    // Initialize APR and create the global pool
    apr_initialize();

    if (!gAPRPoolp)
    {
        apr_pool_create_ex(&gAPRPoolp, NULL, abortfunc, NULL);
    }

    gAPRInitialized = true;
}


bool ll_apr_is_initialized()
{
    return gAPRInitialized;
}

void ll_cleanup_apr()
{
    gAPRInitialized = false;

    LL_DEBUGS("APR") << "Cleaning up APR" << LL_ENDL;

    if (gAPRPoolp)
    {
        apr_pool_destroy(gAPRPoolp);
        gAPRPoolp = NULL;
    }
    apr_terminate();
}

//
//
//LLAPRPool
//
LLAPRPool::LLAPRPool(apr_pool_t *parent, apr_size_t size, bool releasePoolFlag)
    : mParent(parent),
    mReleasePoolFlag(releasePoolFlag),
    mMaxSize(size),
    mPool(nullptr)
{
    createAPRPool() ;
}

LLAPRPool::~LLAPRPool()
{
    releaseAPRPool() ;
}

void LLAPRPool::createAPRPool()
{
    if(mPool)
    {
        return ;
    }

    mStatus = apr_pool_create(&mPool, mParent);
    ll_apr_warn_status(mStatus) ;

    if(mMaxSize > 0) //size is the number of blocks (which is usually 4K), NOT bytes.
    {
        apr_allocator_t *allocator = apr_pool_allocator_get(mPool);
        if (allocator)
        {
            apr_allocator_max_free_set(allocator, mMaxSize) ;
        }
    }
}

void LLAPRPool::releaseAPRPool()
{
    if(!mPool)
    {
        return ;
    }

    if(!mParent || mReleasePoolFlag)
    {
        apr_pool_destroy(mPool) ;
        mPool = NULL ;
    }
}

//virtual
apr_pool_t* LLAPRPool::getAPRPool()
{
    return mPool ;
}

//---------------------------------------------------------------------

bool _ll_apr_warn_status(apr_status_t status, const char* file, int line)
{
    if(APR_SUCCESS == status) return false;
    char buf[MAX_STRING];
    apr_strerror(status, buf, sizeof(buf));

#ifdef LL_WINDOWS
    // On Windows, APR error strings may be in the system's ANSI code page (e.g., Cyrillic)
    // Convert to UTF-8 for proper logging
    std::string error_msg = buf;
    int wlen = MultiByteToWideChar(CP_ACP, 0, buf, -1, nullptr, 0);
    if (wlen > 0)
    {
        std::wstring wbuf(wlen, L'\0');
        MultiByteToWideChar(CP_ACP, 0, buf, -1, &wbuf[0], wlen);

        int utf8len = WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (utf8len > 0)
        {
            std::string utf8buf(utf8len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), -1, &utf8buf[0], utf8len, nullptr, nullptr);
            error_msg = utf8buf.c_str(); // Remove null terminator
        }
        LL_WARNS("APR") << "APR: " << file << ":" << line << " " << error_msg << " (0x" << std::hex << status << std::dec << ")" << LL_ENDL;
    }
    else
    {
        LL_WARNS("APR") << "APR: " << file << ":" << line << " " << buf << " (0x" << std::hex << status << std::dec << ")" << LL_ENDL;
    }
#else
    LL_WARNS("APR") << "APR: " << file << ":" << line << " " << buf << " (0x" << std::hex << status << std::dec << ")" << LL_ENDL;
#endif
    return true;
}

void _ll_apr_assert_status(apr_status_t status, const char* file, int line)
{
    llassert(! _ll_apr_warn_status(status, file, line));
}
