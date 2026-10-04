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

#include <atomic>
#include <filesystem>
#include <fstream>
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
}

ALLSLReference::Result ALLSLReference::compile(const std::string& text, Target target)
{
    // LL's compiler keeps the parse, its scopes and its errors in globals.
    static std::mutex one_at_a_time;
    std::lock_guard   lock(one_at_a_time);

    static std::atomic<U32> made{ 0 };
    std::error_code         ec;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path(ec) /
        ("allslreference-" + std::to_string(LLApp::getPid()) + "-" + std::to_string(made++));
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path source = dir / "script.lsl";
    const std::filesystem::path image  = dir / "script.out.bin";
    const std::filesystem::path out    = dir / "script.messages";
    {
        std::ofstream file(source, std::ios::binary);
        file << text;
    }

    Result r;
    r.ok = lscript_compile(source.string().c_str(), image.string().c_str(), out.string().c_str(), target == Target::CIL, SCRIPT_ID,
                           FALSE);
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
    std::filesystem::remove_all(dir, ec);
    return r;
}
