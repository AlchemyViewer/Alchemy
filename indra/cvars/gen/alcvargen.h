/**
 * @file alcvargen.h
 * @brief The settings schema's generator: reads the TOML declarations,
 *        checks them, and writes the headers, tables and schema files the
 *        build compiles.
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

#pragma once

#include <string>
#include <vector>

namespace ALCVarGen
{
    struct Options
    {
        // The declarations, one file an area, in the order their settings
        // are listed.
        std::vector<std::string> inputs;
        // Where the outputs go: include/, src/ and the schema files.
        std::string outDir;
        // The namespace the handles are under, and the directory their
        // headers are included from.
        std::string root       = "cvar";
        std::string includeDir = "cvars";
        // The lock, cvars.lock: each setting's steps, what its stored values
        // have meant. A change of the schema it does not record yet fails
        // the run, saying what it does to stored values; one nothing
        // converts fails unless the entry's migrate answers it. With none,
        // each setting is the step a first lock would record, unsaid.
        std::string lock;
        // The target branch's lock, for a pull request's: every line of it
        // must still be in this one, as a line once committed stays.
        std::string base;
        // The command that records the schema's changes in the lock, as a
        // lock behind the schema names it.
        std::string lockCommand = "cmake --build <tree> --target cvars_lock";
        // A lock behind the schema, or one that cannot be read, is a
        // warning, and each change it does not record is generated as the
        // step it will be: a configure, which writes the tree that builds
        // the lock's target.
        bool provisional = false;
        // A new setting's first step, which changes nothing stored, written
        // into the lock as the run goes on: a local build's. Any other change
        // the lock does not record fails as ever, and a strict build -- CI's
        // -- records nothing.
        bool recordNew = false;
        // Record the schema's changes in the lock, refusing one nothing
        // answers, and write nothing else.
        bool writeLock = false;
        // With writeLock: the lock as the last commit holds it (git's HEAD).
        // The lines it does not hold are worked out again from the entries,
        // so that the steps a setting gained since are recorded as one. A
        // file that is not there folds nothing.
        std::string committed;
        // Accepted words, one a line, beside the abbreviations the
        // generator knows: every word of every name is checked against them
        // when given.
        std::string words;
        // Print where this setting or group is declared, and its header,
        // and write nothing. A name may be an old name, an alias.
        std::string where;
        // Check the declarations and write nothing.
        bool check = false;
        // Messages in Visual Studio's form, "file(line,col): error CV0001:
        // ...", rather than the compilers' "file:line:col: error: ...".
        bool msvc = false;
        // Write the JSON Schema an editor checks the declarations by into
        // this file, and nothing else.
        std::string jsonSchema;
    };

    struct Result
    {
        bool ok = false;
        // What fails the build, as a compiler says it, at the entry's
        // absolute path: "file:line:col: error: name: what is wrong".
        std::vector<std::string> errors;
        // What will happen to stored values, a change the lock does not
        // record yet a line, at the entry: "file:line:col: note: ...".
        std::vector<std::string> notes;
        // What a provisional run would have failed for.
        std::vector<std::string> warnings;
        // Every output, written or left as it was because it had not changed.
        std::vector<std::string> outputs;
        // The outputs whose content changed.
        std::vector<std::string> written;
        // --where's answer.
        std::string where;
    };

    Result run(const Options& options);

    // The command line, which --help explains. Returns the process's exit
    // code.
    int main(int argc, char* argv[]);
}
