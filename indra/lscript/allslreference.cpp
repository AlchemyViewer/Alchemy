/**
 * @file allslreference.cpp
 * @brief LL's LSL compiler called on a text: the LSO image or the CIL it makes, and what it said.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "allslreference.h"

#include "llapp.h"
#include "lscript_byteformat.h"
#include "lscript_rt_interface.h"

#if LL_WINDOWS
#include "llwin32headers.h"
#include <process.h>
#else
#include <pthread.h>
#endif

#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <system_error>

namespace
{
    std::string readAll(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // LL's compiler walks its tree by recursion, in places as deep as the
    // script is long: a level for each statement in a block, for each term
    // of a chain of operators. The grid takes about 256 KiB of source, a
    // chain of some 128 thousand terms, past what a thread's usual stack
    // holds and far past it under the sanitizers. So a compile runs on a
    // thread of its own whose stack holds the longest: 512 MiB, reserved
    // rather than committed, so it costs what it uses.
    constexpr size_t COMPILE_STACK = size_t(512) << 20;

    struct Work
    {
        std::function<void()> run;
        std::exception_ptr     thrown;  // carried back to the caller, as an LL_ERRS a test throws is
    };

    void runWork(Work& work)
    {
        try
        {
            work.run();
        }
        catch (...)
        {
            work.thrown = std::current_exception();
        }
    }

    void runOnDeepStack(const std::function<void()>& run)
    {
        Work work{ run, nullptr };
#if LL_WINDOWS
        const uintptr_t thread = _beginthreadex(
            nullptr, static_cast<unsigned>(COMPILE_STACK),
            [](void* arg) -> unsigned
            {
                runWork(*static_cast<Work*>(arg));
                return 0;
            },
            &work, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (!thread)
        {
            LL_ERRS() << "No thread for LL's compiler" << LL_ENDL;
        }
        WaitForSingleObject(reinterpret_cast<HANDLE>(thread), INFINITE);
        CloseHandle(reinterpret_cast<HANDLE>(thread));
#else
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, COMPILE_STACK);
        pthread_t  thread;
        const int  made = pthread_create(
            &thread, &attr,
            [](void* arg) -> void*
            {
                runWork(*static_cast<Work*>(arg));
                return nullptr;
            },
            &work);
        pthread_attr_destroy(&attr);
        if (made != 0)
        {
            LL_ERRS() << "No thread for LL's compiler: " << made << LL_ENDL;
        }
        pthread_join(thread, nullptr);
#endif
        if (work.thrown)
        {
            std::rethrow_exception(work.thrown);
        }
    }
}

ALLSLReference::Result ALLSLReference::compile(const std::string& text, Target target)
{
    // LL's compiler keeps the parse, its scopes and its errors in globals.
    static std::mutex one_at_a_time;
    std::lock_guard   lock(one_at_a_time);

    // A folder of this compile's own for the files LL's compiler reads and
    // writes, gone however the compile ends: an LL_ERRS inside it is thrown
    // through here in a test (ll_test::FatalError), and left the folder
    // behind when the removal was the last statement.
    struct TempDir
    {
        std::filesystem::path path;
        ~TempDir()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    };
    static std::atomic<U32> made{ 0 };
    std::error_code         ec;
    const TempDir           temp{ std::filesystem::temp_directory_path(ec) /
                        ("allslreference-" + std::to_string(LLApp::getPid()) + "-" + std::to_string(made++)) };
    const std::filesystem::path& dir = temp.path;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path source = dir / "script.lsl";
    const std::filesystem::path image  = dir / "script.out.bin";
    const std::filesystem::path out    = dir / "script.messages";
    {
        std::ofstream file(source, std::ios::binary);
        file << text;
    }

    Result r;
    runOnDeepStack(
        [&]
        {
            r.ok = lscript_compile(source.string().c_str(), image.string().c_str(), out.string().c_str(), target == Target::CIL,
                                   SCRIPT_ID, FALSE);
        });
    // The image or the assembly goes to the one file, the errors and the
    // compiler's notes to the other.
    r.messages                = readAll(out);
    const std::string written = readAll(image);
    if (target == Target::CIL)
    {
        r.cil = written;
    }
    else
    {
        r.image.assign(written.begin(), written.end());
    }
    return r;
}
