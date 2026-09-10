/**
 * @file alxuidocuments_test.cpp
 * @brief More than one file open at once, each with its own edits.
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

#include "../alxuidocuments.h"

#include "lldir.h"
#include "llfile.h"

#include "../test/lltut.h"

#include <fstream>

class LLAvatarName;
const std::string gDocumentsTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gDocumentsTestAnonName;
}

namespace tut
{
    struct alxuidocuments_data
    {
        std::string mDir;

        alxuidocuments_data()
        {
            mDir = gDirUtilp->add(gDirUtilp->getTempDir(), "alxuidocuments_test");
            gDirUtilp->deleteDirAndContents(mDir);
            LLFile::mkdir(mDir);
        }

        ~alxuidocuments_data()
        {
            gDirUtilp->deleteDirAndContents(mDir);
        }

        std::string write(const std::string& name, const std::string& text) const
        {
            const std::string path = gDirUtilp->add(mDir, name);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
            return path;
        }

        static std::string read(const std::string& path)
        {
            std::error_code ec;
            return LLFile::getContents(path, ec);
        }

        // A root with one element under it, since a path names a child.
        static std::string panel(const std::string& name)
        {
            return "<panel name=\"root\" width=\"99\" height=\"99\">\n"
                   "    <panel name=\"" + name + "\" width=\"10\" height=\"10\"/>\n"
                   "</panel>\n";
        }
    };

    typedef test_group<alxuidocuments_data> alxuidocuments_test;
    typedef alxuidocuments_test::object     alxuidocuments_object;
    tut::alxuidocuments_test alxuidocuments_testgroup("alxuidocuments");

    // Two files open together, each with its own edits: the whole of why
    // this exists. A tool holding one at a time has to refuse the second
    // while there is work in the first, and refusing is the thing being
    // removed here.
    template<> template<>
    void alxuidocuments_object::test<1>()
    {
        const std::string base = write("base.xml", panel("a"));
        const std::string other = write("other.xml", panel("b"));

        ALXUIDocuments open;
        ALXUIEdit* first = open.open(base);
        ensure("the first opens", first != nullptr);
        ensure("writes", first->setAttribute({ "a" }, "width", "40"));

        ALXUIEdit* second = open.open(other);
        ensure("the second opens with work in the first", second != nullptr);
        ensure("and is not the first", second != first);
        ensure("writes to the second", second->setAttribute({ "b" }, "width", "50"));

        ensure("the first still has its work", first->dirty());
        ensure_equals("and its own undo stack", first->undoDepth(), 1u);
        ensure_equals("as does the second", second->undoDepth(), 1u);
        ensure_equals("both are unsaved", open.dirtyCount(), 2);

        // Opening one is working on it, so it is the one an operation with
        // no path of its own means.
        ensure_equals("the last opened is the active one", open.activePath(), other);
        open.makeActive(base);
        ensure_equals("and it can be said outright", open.activePath(), base);
        ensure("which is the document itself", &open.active() == first);
    }

    // What a caller merging a file's layers reads: the text in hand where
    // there is one, and nothing where the file is not open, which means
    // read the file.
    template<> template<>
    void alxuidocuments_object::test<2>()
    {
        const std::string base = write("base.xml", panel("a"));
        const std::string shut = write("shut.xml", panel("c"));

        ALXUIDocuments open;
        ALXUIEdit* held = open.open(base);
        ensure("opens", held != nullptr);
        ensure("writes", held->setAttribute({ "a" }, "width", "40"));

        const std::string* text = open.textFor(base);
        ensure("the open one is read from memory", text != nullptr);
        ensure("with the work in it", text->find("width=\"40\"") != std::string::npos);
        ensure("and the disk does not have that yet",
               read(base).find("width=\"40\"") == std::string::npos);
        ensure("a file nobody has open is read from the file", open.textFor(shut) == nullptr);
    }

    // Everything with work in it, written. And a document nobody opened is
    // still a document to answer with, so a caller reading the active one
    // before opening anything does not have to ask twice.
    template<> template<>
    void alxuidocuments_object::test<3>()
    {
        ALXUIDocuments open;
        ensure("nothing is active", !open.hasActive());
        ensure("and the active one is empty", open.active().text().empty());
        ensure_equals("nothing to save", open.saveAll(), 0);

        const std::string base = write("base.xml", panel("a"));
        const std::string other = write("other.xml", panel("b"));
        ensure("opens", open.open(base) != nullptr);
        ensure("opens", open.open(other) != nullptr);
        ensure("writes", open.find(base)->setAttribute({ "a" }, "width", "40"));
        ensure("writes", open.find(other)->setAttribute({ "b" }, "width", "50"));

        ensure_equals("both written", open.saveAll(), 2);
        ensure_equals("and nothing is left unsaved", open.dirtyCount(), 0);
        ensure("the disk has the first", read(base).find("width=\"40\"") != std::string::npos);
        ensure("and the second", read(other).find("width=\"50\"") != std::string::npos);

        // Letting one go leaves the other, and leaves something active.
        ensure("closes", open.close(base));
        ensure_equals("one left", open.count(), 1u);
        ensure_equals("and it is the active one", open.activePath(), other);
        ensure("a file that was never open cannot be closed", !open.close(base));
    }

    // One thing done to two files is one thing to put back. Repairing a
    // file's translations writes a base and every language beside it, and
    // undoing that a file at a time undoes something nobody did.
    template<> template<>
    void alxuidocuments_object::test<5>()
    {
        const std::string base = write("base.xml", panel("a"));
        const std::string other = write("other.xml", panel("b"));

        ALXUIDocuments open;
        ensure("opens", open.open(base) != nullptr);
        ensure("opens", open.open(other) != nullptr);
        ensure("nothing to put back yet", !open.canUndo());

        {
            ALXUIDocuments::Action together(open);
            ensure("writes to one", open.find(base)->setAttribute({ "a" }, "width", "40"));
            ensure("and to the other", open.find(other)->setAttribute({ "b" }, "width", "50"));
        }

        ensure("which is one thing", open.canUndo());
        ensure("put back", open.undo());
        ensure("both of them", open.find(base)->text().find("width=\"40\"") == std::string::npos);
        ensure("at once", open.find(other)->text().find("width=\"50\"") == std::string::npos);
        ensure("and there is nothing else to put back", !open.canUndo());

        ensure("done again", open.redo());
        ensure("both", open.find(base)->text().find("width=\"40\"") != std::string::npos);
        ensure("at once", open.find(other)->text().find("width=\"50\"") != std::string::npos);

        // An action over more than one step says only that: there is no one
        // field for a caller to write onto what it has already built.
        ensure("undone", open.undo());
        ensure("it does not claim to be one field",
               !open.lastChange().oneField);
    }

    // Several steps in one file, asked for as one thing, come back as one --
    // and the same steps not asked for as one come back one at a time.
    template<> template<>
    void alxuidocuments_object::test<6>()
    {
        const std::string base = write("base.xml", panel("a"));

        ALXUIDocuments open;
        ALXUIEdit* held = open.open(base);
        ensure("opens", held != nullptr);

        {
            ALXUIDocuments::Action lining_up(open);
            ensure("writes", held->setAttribute({ "a" }, "width", "40"));
            ensure("writes", held->setAttribute({ "a" }, "height", "41"));
        }
        ensure("put back", open.undo());
        ensure("the first went", held->text().find("width=\"40\"") == std::string::npos);
        ensure("and so did the second", held->text().find("height=\"41\"") == std::string::npos);
        ensure("as one thing", !open.canUndo());

        // The same two, each on its own.
        ensure("writes", held->setAttribute({ "a" }, "width", "60"));
        open.settle();
        ensure("writes", held->setAttribute({ "a" }, "height", "61"));
        open.settle();
        ensure("put back", open.undo());
        ensure("only the second went", held->text().find("height=\"61\"") == std::string::npos);
        ensure("the first is still there", held->text().find("width=\"60\"") != std::string::npos);
        ensure("and there is another to put back", open.canUndo());
        ensure("which is one field of one element", open.lastChange().oneField);
    }

    // A path that names no file is not a document, and says so rather than
    // becoming an empty one nobody can tell from a real one.
    template<> template<>
    void alxuidocuments_object::test<4>()
    {
        ALXUIDocuments open;
        ensure("no file, no document", open.open(gDirUtilp->add(mDir, "not_here.xml")) == nullptr);
        ensure("and it says why", !open.error().empty());
        ensure_equals("nothing was opened", open.count(), 0u);
        ensure("nor is one made by an empty path", open.open(std::string()) == nullptr);
    }
}
