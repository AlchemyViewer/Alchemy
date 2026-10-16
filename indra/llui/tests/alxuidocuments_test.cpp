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

#include "alxuidocuments.h"

#include "lldir.h"
#include "llfile.h"

#include "altesttempdir.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

namespace
{
    struct alxuidocuments_data
    {
        // A directory of its own for each test, removed as it ends.
        ll_test::TempDir mTemp{ "alxuidocuments_test" };
        std::string      mDir = mTemp.string();

        std::string write(const std::string& name, const std::string& text) const
        {
            const std::string path = gDirUtilp->add(mDir, name);
            llofstream out(path, std::ios::binary | std::ios::trunc);
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

    // Two files open together, each with its own edits: the whole of why
    // this exists. A tool holding one at a time has to refuse the second
    // while there is work in the first, and refusing is the thing being
    // removed here.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: two files open together each keep their own edits and undo", "[alxuidocuments]")
    {
        const std::string base = write("base.xml", panel("a"));
        const std::string other = write("other.xml", panel("b"));

        ALXUIDocuments open;
        ALXUIEdit* first = open.open(base);
        REQUIRE(first != nullptr);
        CHECK(first->setAttribute({ "a" }, "width", "40"));

        ALXUIEdit* second = open.open(other);
        UNSCOPED_INFO("the second opens with work in the first");
        REQUIRE(second != nullptr);
        CHECK(second != first);
        CHECK(second->setAttribute({ "b" }, "width", "50"));

        UNSCOPED_INFO("the first still has its work");
        CHECK(first->dirty());
        UNSCOPED_INFO("and its own undo stack");
        CHECK(first->undoDepth() == 1u);
        UNSCOPED_INFO("as does the second");
        CHECK(second->undoDepth() == 1u);
        UNSCOPED_INFO("both are unsaved");
        CHECK(open.dirtyCount() == 2);

        // Opening one is working on it, so it is the one an operation with
        // no path of its own means.
        UNSCOPED_INFO("the last opened is the active one");
        CHECK(open.activePath() == other);
        open.makeActive(base);
        UNSCOPED_INFO("and it can be said outright");
        CHECK(open.activePath() == base);
        UNSCOPED_INFO("which is the document itself");
        CHECK(&open.active() == first);
    }

    // What a caller merging a file's layers reads: the text in hand where
    // there is one, and nothing where the file is not open, which means
    // read the file.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: textFor gives an open file's text and nothing for one not open", "[alxuidocuments]")
    {
        const std::string base = write("base.xml", panel("a"));
        const std::string shut = write("shut.xml", panel("c"));

        ALXUIDocuments open;
        ALXUIEdit* held = open.open(base);
        REQUIRE(held != nullptr);
        CHECK(held->setAttribute({ "a" }, "width", "40"));

        const std::string* text = open.textFor(base);
        UNSCOPED_INFO("the open one is read from memory");
        REQUIRE(text != nullptr);
        UNSCOPED_INFO("with the work in it");
        CHECK(text->find("width=\"40\"") != std::string::npos);
        UNSCOPED_INFO("and the disk does not have that yet");
        CHECK(read(base).find("width=\"40\"") == std::string::npos);
        UNSCOPED_INFO("a file nobody has open is read from the file");
        CHECK(open.textFor(shut) == nullptr);
    }

    // Everything with work in it, written. And a document nobody opened is
    // still a document to answer with, so a caller reading the active one
    // before opening anything does not have to ask twice.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: saveAll writes everything with work in it", "[alxuidocuments]")
    {
        ALXUIDocuments open;
        CHECK_FALSE(open.hasActive());
        UNSCOPED_INFO("and the active one is empty");
        CHECK(open.active().text().empty());
        UNSCOPED_INFO("nothing to save");
        CHECK(open.saveAll() == 0);

        const std::string base = write("base.xml", panel("a"));
        const std::string other = write("other.xml", panel("b"));
        REQUIRE(open.open(base) != nullptr);
        REQUIRE(open.open(other) != nullptr);
        CHECK(open.find(base)->setAttribute({ "a" }, "width", "40"));
        CHECK(open.find(other)->setAttribute({ "b" }, "width", "50"));

        UNSCOPED_INFO("both written");
        CHECK(open.saveAll() == 2);
        UNSCOPED_INFO("and nothing is left unsaved");
        CHECK(open.dirtyCount() == 0);
        UNSCOPED_INFO("the disk has the first");
        CHECK(read(base).find("width=\"40\"") != std::string::npos);
        UNSCOPED_INFO("and the second");
        CHECK(read(other).find("width=\"50\"") != std::string::npos);

        // Letting one go leaves the other, and leaves something active.
        CHECK(open.close(base));
        CHECK(open.count() == 1u);
        UNSCOPED_INFO("and it is the active one");
        CHECK(open.activePath() == other);
        UNSCOPED_INFO("a file that was never open cannot be closed");
        CHECK_FALSE(open.close(base));
    }

    // A path that names no file is not a document, and says so rather than
    // becoming an empty one nobody can tell from a real one.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: a path that names no file is not a document", "[alxuidocuments]")
    {
        ALXUIDocuments open;
        UNSCOPED_INFO("no file, no document");
        CHECK(open.open(gDirUtilp->add(mDir, "not_here.xml")) == nullptr);
        UNSCOPED_INFO("and it says why");
        CHECK_FALSE(open.error().empty());
        UNSCOPED_INFO("nothing was opened");
        CHECK(open.count() == 0u);
        UNSCOPED_INFO("nor is one made by an empty path");
        CHECK(open.open(std::string()) == nullptr);
    }

    // One thing done to two files is one thing to put back. Repairing a
    // file's translations writes a base and every language beside it, and
    // undoing that a file at a time undoes something nobody did.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: one thing done to two files is one thing to put back", "[alxuidocuments]")
    {
        const std::string base = write("base.xml", panel("a"));
        const std::string other = write("other.xml", panel("b"));

        ALXUIDocuments open;
        REQUIRE(open.open(base) != nullptr);
        REQUIRE(open.open(other) != nullptr);
        UNSCOPED_INFO("nothing to put back yet");
        CHECK_FALSE(open.canUndo());

        {
            ALXUIDocuments::Action together(open);
            CHECK(open.find(base)->setAttribute({ "a" }, "width", "40"));
            CHECK(open.find(other)->setAttribute({ "b" }, "width", "50"));
        }

        UNSCOPED_INFO("which is one thing");
        CHECK(open.canUndo());
        CHECK(open.undo());
        UNSCOPED_INFO("both of them");
        CHECK(open.find(base)->text().find("width=\"40\"") == std::string::npos);
        UNSCOPED_INFO("at once");
        CHECK(open.find(other)->text().find("width=\"50\"") == std::string::npos);
        UNSCOPED_INFO("and there is nothing else to put back");
        CHECK_FALSE(open.canUndo());

        CHECK(open.redo());
        UNSCOPED_INFO("both");
        CHECK(open.find(base)->text().find("width=\"40\"") != std::string::npos);
        UNSCOPED_INFO("at once");
        CHECK(open.find(other)->text().find("width=\"50\"") != std::string::npos);

        // An action over more than one step says only that: there is no one
        // field for a caller to write onto what it has already built.
        CHECK(open.undo());
        UNSCOPED_INFO("it does not claim to be one field");
        CHECK_FALSE(open.lastChange().oneField);
    }

    // Several steps in one file, asked for as one thing, come back as one --
    // and the same steps not asked for as one come back one at a time.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: several steps in one file asked for as one come back as one", "[alxuidocuments]")
    {
        const std::string base = write("base.xml", panel("a"));

        ALXUIDocuments open;
        ALXUIEdit* held = open.open(base);
        REQUIRE(held != nullptr);

        {
            ALXUIDocuments::Action lining_up(open);
            CHECK(held->setAttribute({ "a" }, "width", "40"));
            CHECK(held->setAttribute({ "a" }, "height", "41"));
        }
        CHECK(open.undo());
        UNSCOPED_INFO("the first went");
        CHECK(held->text().find("width=\"40\"") == std::string::npos);
        UNSCOPED_INFO("and so did the second");
        CHECK(held->text().find("height=\"41\"") == std::string::npos);
        UNSCOPED_INFO("as one thing");
        CHECK_FALSE(open.canUndo());

        // The same two, each on its own.
        CHECK(held->setAttribute({ "a" }, "width", "60"));
        open.settle();
        CHECK(held->setAttribute({ "a" }, "height", "61"));
        open.settle();
        CHECK(open.undo());
        UNSCOPED_INFO("only the second went");
        CHECK(held->text().find("height=\"61\"") == std::string::npos);
        UNSCOPED_INFO("the first is still there");
        CHECK(held->text().find("width=\"60\"") != std::string::npos);
        UNSCOPED_INFO("and there is another to put back");
        CHECK(open.canUndo());
        UNSCOPED_INFO("which is one field of one element");
        CHECK(open.lastChange().oneField);
    }

    // The whole stack as one timeline: everything done, then everything put
    // back, with a mark saying where the present is. Putting a step back does
    // not make it never have happened.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: history keeps every action done or put back and marks the present", "[alxuidocuments]")
    {
        const std::string base = write("base.xml", panel("a"));
        const std::string other = write("other.xml", panel("b"));

        ALXUIDocuments open;
        ALXUIEdit* first = open.open(base);
        ALXUIEdit* second = open.open(other);
        REQUIRE(first);
        REQUIRE(second);
        UNSCOPED_INFO("nothing yet");
        CHECK(open.history().empty());

        CHECK(first->setAttribute({ "a" }, "width", "40"));
        open.settle();
        {
            ALXUIDocuments::Action together(open);
            CHECK(first->setAttribute({ "a" }, "height", "41"));
            CHECK(second->setAttribute({ "b" }, "width", "50"));
        }

        std::vector<ALXUIDocuments::Entry> all = open.history();
        REQUIRE(all.size() == 2u);
        UNSCOPED_INFO("and both are in force");
        CHECK(open.inForce() == 2u);

        UNSCOPED_INFO("the first was one step");
        CHECK(all[0].steps == 1);
        UNSCOPED_INFO("in one document");
        CHECK(all[0].documents == 1);
        UNSCOPED_INFO("which it names");
        CHECK(all[0].document == base);
        UNSCOPED_INFO("and says what it did");
        CHECK(all[0].change.did == ALXUIEdit::Did::WroteField);
        UNSCOPED_INFO("on which field");
        CHECK(all[0].change.field == std::string("width"));

        UNSCOPED_INFO("the second was two steps");
        CHECK(all[1].steps == 2);
        UNSCOPED_INFO("over two documents");
        CHECK(all[1].documents == 2);
        UNSCOPED_INFO("so it names none of them");
        CHECK(all[1].document.empty());

        // Put back, and it is still listed: the present moved, not the past.
        CHECK(open.undo());
        all = open.history();
        UNSCOPED_INFO("both still listed");
        REQUIRE(all.size() == 2u);
        UNSCOPED_INFO("with one of them in force");
        CHECK(open.inForce() == 1u);
        UNSCOPED_INFO("and the one put back is still the second");
        CHECK(all[1].steps == 2);

        CHECK(open.redo());
        UNSCOPED_INFO("back where it was");
        CHECK(open.inForce() == 2u);
    }

    // A document's own stack is capped by what it weighs, and a file heavy
    // enough fills it in one step. The history here is of steps taken, not
    // of the stack's depth, so the second edit to such a file is an action
    // like the first rather than one the set never saw.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: a file that fills its own stack in one step still has each in the history", "[alxuidocuments]")
    {
        const std::string heavy = write("heavy.xml",
            "<panel name=\"root\">\n"
            "    <!-- " + std::string(2500000, 'x') + " -->\n"
            "    <panel name=\"a\" width=\"1\"/>\n"
            "</panel>\n");

        ALXUIDocuments open;
        ALXUIEdit* held = open.open(heavy);
        REQUIRE(held != nullptr);
        CHECK(held->setAttribute({ "a" }, "width", "2"));
        open.settle();
        CHECK(held->setAttribute({ "a" }, "width", "3"));
        open.settle();
        UNSCOPED_INFO("the stack held one of them");
        CHECK(held->undoDepth() == 1u);
        UNSCOPED_INFO("and the history both");
        CHECK(open.history().size() == 2u);
        CHECK(open.inForce() == 2u);
        CHECK(open.undo());
        UNSCOPED_INFO("puts back the second");
        CHECK(held->resolve({ "a" }).attribute("width").as_int() == 2);
        UNSCOPED_INFO("and lists it as put back");
        CHECK(open.inForce() == 1u);
    }

    // A file that has gone from the disk is not read again: what was read
    // the first time stays, and the merge that reads the document in place
    // of the file goes on reading a document rather than nothing.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: a file that has gone from the disk is not read again", "[alxuidocuments]")
    {
        const std::string path = write("going.xml", panel("a"));
        ALXUIDocuments open;
        ALXUIEdit* held = open.open(path);
        REQUIRE(held != nullptr);
        const std::string was = held->text();

        LLFile::remove(path);
        UNSCOPED_INFO("nothing was read again");
        CHECK(open.rereadClean() == 0);
        UNSCOPED_INFO("and the text is what it was");
        CHECK(held->text() == was);
        UNSCOPED_INFO("and reading it did not put it back");
        CHECK_FALSE(LLFile::isfile(path));
        UNSCOPED_INFO("which the merge is still given");
        CHECK(open.textFor(path) != nullptr);
        UNSCOPED_INFO("and the document says why");
        CHECK_FALSE(held->error().empty());
    }

    // Closing a document takes the history with it only where an action
    // named it: one opened to be read and closed again is not a reason to
    // lose what was done to the others.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: closing a document drops the history only where an action named it", "[alxuidocuments]")
    {
        const std::string base = write("base.xml", panel("a"));
        const std::string other = write("other.xml", panel("b"));
        ALXUIDocuments open;
        ALXUIEdit* first = open.open(base);
        REQUIRE(first != nullptr);
        CHECK(first->setAttribute({ "a" }, "width", "40"));
        open.settle();
        CHECK(open.history().size() == 1u);

        CHECK(open.open(other) != nullptr);
        UNSCOPED_INFO("closes it untouched");
        CHECK(open.close(other));
        UNSCOPED_INFO("and the action is still there");
        CHECK(open.history().size() == 1u);
        UNSCOPED_INFO("to be put back");
        CHECK(open.canUndo());

        UNSCOPED_INFO("opens it again");
        REQUIRE(open.open(other) != nullptr);
        CHECK(open.find(other)->setAttribute({ "b" }, "width", "50"));
        open.settle();
        CHECK(open.history().size() == 2u);
        UNSCOPED_INFO("closes it with work in the history");
        CHECK(open.close(other));
        UNSCOPED_INFO("and the history is gone, since half of it named a file that is");
        CHECK(open.history().empty());
    }

    // Given a window, a run of edits to one field is one action: a number
    // stepped three times comes back in one undo, as the field it is, and
    // is put on again in one redo. Another field is another action, an
    // Action over several steps is never joined, and an edit after an undo
    // is a new run rather than more of the one put back.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: given a window a run of edits to one field is one action", "[alxuidocuments]")
    {
        const std::string base = write("base.xml", panel("a"));
        ALXUIDocuments open;
        open.setCoalesceWindow(60.0);
        ALXUIEdit* held = open.open(base);
        REQUIRE(held != nullptr);

        for (const char* value : { "11", "12", "13" })
        {
            REQUIRE(held->setAttribute({ "a" }, "width", value));
            open.settle();
        }
        std::vector<ALXUIDocuments::Entry> all = open.history();
        UNSCOPED_INFO("three steps are one action");
        REQUIRE(all.size() == 1u);
        UNSCOPED_INFO("of three steps");
        CHECK(all[0].steps == 3);
        UNSCOPED_INFO("to one field");
        CHECK(all[0].sameField);
        UNSCOPED_INFO("which it names");
        CHECK(all[0].change.field == std::string("width"));

        CHECK(held->setAttribute({ "a" }, "height", "20"));
        open.settle();
        UNSCOPED_INFO("which is its own action");
        CHECK(open.history().size() == 2u);
        {
            ALXUIDocuments::Action together(open);
            CHECK(held->setAttribute({ "a" }, "height", "21"));
            CHECK(held->setAttribute({ "a" }, "height", "22"));
        }
        all = open.history();
        UNSCOPED_INFO("an Action is never joined");
        REQUIRE(all.size() == 3u);
        UNSCOPED_INFO("and is not one field");
        CHECK_FALSE(all[2].sameField);

        CHECK(open.undo());
        CHECK(open.undo());
        CHECK(open.undo());
        UNSCOPED_INFO("the run went as one");
        CHECK(held->resolve({ "a" }).attribute("width").as_int() == 10);
        {
            INFO("and says which field it was");
            CHECK(open.lastChange().oneField);
            CHECK(open.lastChange().field == "width");
        }
        UNSCOPED_INFO("nothing else to put back");
        CHECK_FALSE(open.canUndo());

        CHECK(open.redo());
        UNSCOPED_INFO("the whole run");
        CHECK(held->resolve({ "a" }).attribute("width").as_int() == 13);

        CHECK(open.undo());
        UNSCOPED_INFO("writes after an undo");
        CHECK(held->setAttribute({ "a" }, "width", "14"));
        open.settle();
        all = open.history();
        UNSCOPED_INFO("a new run, and the one put back is gone");
        REQUIRE(all.size() == 1u);
        UNSCOPED_INFO("of one step");
        CHECK(all[0].steps == 1);
    }

    // Looked at in the middle -- a caller settling after each edit while an
    // Action is open -- an Action is still the one thing: its steps in each
    // document added up, not one field though its first step was, and put
    // back and on again in one.
    TEST_CASE_METHOD(alxuidocuments_data, "alxuidocuments: an Action settled after each edit is still one action", "[alxuidocuments]")
    {
        const std::string first  = write("first.xml", panel("a"));
        const std::string second = write("second.xml", panel("b"));
        ALXUIDocuments open;
        open.setCoalesceWindow(60.0);
        ALXUIEdit* one = open.open(first);
        ALXUIEdit* two = open.open(second);
        REQUIRE(one);
        REQUIRE(two);
        {
            ALXUIDocuments::Action together(open);
            CHECK(one->setAttribute({ "a" }, "width", "11"));
            open.settle();
            CHECK(two->setAttribute({ "b" }, "width", "21"));
            open.settle();
            CHECK(one->setAttribute({ "a" }, "height", "12"));
            open.settle();
        }
        std::vector<ALXUIDocuments::Entry> all = open.history();
        REQUIRE(all.size() == 1u);
        UNSCOPED_INFO("of three steps");
        CHECK(all[0].steps == 3);
        UNSCOPED_INFO("in two documents");
        CHECK(all[0].documents == 2);
        UNSCOPED_INFO("not one field");
        CHECK_FALSE(all[0].sameField);
        CHECK(open.undo());
        {
            INFO("all of it");
            CHECK(one->resolve({ "a" }).attribute("width").as_int() == 10);
            CHECK(one->resolve({ "a" }).attribute("height").as_int() == 10);
            CHECK(two->resolve({ "b" }).attribute("width").as_int() == 10);
        }
        UNSCOPED_INFO("nothing else");
        CHECK_FALSE(open.canUndo());
        CHECK(open.redo());
        {
            INFO("all of it");
            CHECK(one->resolve({ "a" }).attribute("height").as_int() == 12);
            CHECK(two->resolve({ "b" }).attribute("width").as_int() == 21);
        }
    }
}
