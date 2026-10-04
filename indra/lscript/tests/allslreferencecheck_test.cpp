/**
 * @file allslreferencecheck_test.cpp
 * @brief Tailslide's LSO images and CIL, and the Mono weigher, held against LL's compiler's over Tailslide's test scripts.
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

// Tailslide means to compile LSL as LL's compiler does, and the viewer
// weighs a script by what Tailslide makes of it. Here every script of
// Tailslide's own tests that both compile is compiled by both, and what
// differs is held to the list of what is known to: a difference new to the
// list is a Tailslide that moved, or one of ours.

#include "linden_common.h"

#include "../allslreference.h"
#include "allslservice.h"
#include "alscriptweight.h"
#include "llfile.h"

#include "../test/lltut.h"

#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace
{
    using Target = ALLSLReference::Target;

    // Tailslide's test scripts, by their names under scripts/.
    std::map<std::string, std::string> corpus()
    {
        std::map<std::string, std::string> scripts;
        const std::string                  dir = std::string(AL_ALSCRIPT_TEST_DIR) + "/tailslide/scripts";
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
        {
            if (entry.path().extension() == ".lsl")
            {
                llifstream        in(entry.path().string(), std::ios::binary);
                std::stringstream text;
                text << in.rdbuf();
                scripts[std::filesystem::relative(entry.path(), dir).generic_string()] = text.str();
            }
        }
        return scripts;
    }

    // An LSO register: four bytes, the high one first.
    size_t reg32(const std::vector<U8>& image, size_t at)
    {
        return (size_t)image[at] << 24 | (size_t)image[at + 1] << 16 | (size_t)image[at + 2] << 8 | (size_t)image[at + 3];
    }

    // Where in an LSO image a byte is (gLSCRIPTRegisterAddresses).
    std::string regionOf(const std::vector<U8>& image, size_t at)
    {
        return at < reg32(image, 56) ? "registers" : at < reg32(image, 60) ? "globals" : at < reg32(image, 72) ? "functions"
                                                 : at < reg32(image, 20)   ? "states"
                                                 : at < reg32(image, 24)   ? "heap"
                                                                           : "free";
    }

    bool startsWith(const std::string& text, const char* prefix)
    {
        return text.compare(0, strlen(prefix), prefix) == 0;
    }

    void replaceAll(std::string& text, const std::string& from, const std::string& to)
    {
        for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size())
        {
            text.replace(at, from.size(), to);
        }
    }

    // A CIL text a line to an instruction, blanks run together: LL's writes
    // `cil managed` on a line of its own, and runs what follows a print onto
    // the call's line.
    std::vector<std::string> instructionsOf(const std::string& cil)
    {
        std::vector<std::string> lines;
        const auto add = [&lines](std::string line) {
            std::string one;
            for (const char c : line)
            {
                if (c == ' ' || c == '\t' || c == '\r')
                {
                    if (!one.empty() && one.back() != ' ')
                    {
                        one += ' ';
                    }
                }
                else
                {
                    one += c;
                }
            }
            while (!one.empty() && one.back() == ' ')
            {
                one.pop_back();
            }
            if (one.empty())
            {
                return;
            }
            if (one == "cil managed" && !lines.empty())
            {
                lines.back() += " cil managed";
                return;
            }
            lines.push_back(std::move(one));
        };
        std::istringstream in(cil);
        for (std::string line; std::getline(in, line);)
        {
            const size_t lead = line.find_first_not_of(" \t");
            const size_t paren = lead == std::string::npos || line.compare(lead, 5, "call ") != 0 ? std::string::npos : line.find('(', line.find("::"));
            size_t close = paren;
            for (S32 depth = 0; close != std::string::npos && close < line.size(); ++close)
            {
                depth += line[close] == '(' ? 1 : line[close] == ')' ? -1 : 0;
                if (depth == 0)
                {
                    break;
                }
            }
            if (close != std::string::npos && close + 1 < line.size() && line.find_first_not_of(" \t\r", close + 1) != std::string::npos)
            {
                add(line.substr(0, close + 1));
                add(line.substr(close + 1));
                continue;
            }
            add(line);
        }
        return lines;
    }

    // Spelt alike, what an assembler reads alike: an owner's `class` or
    // `valuetype`, a member's quotes, the blanks in a signature, a double
    // written in decimal or by its bytes, and Tailslide's `ul` before a
    // label of the script's. Each instruction's form is left as written.
    std::vector<std::string> spelt(const std::vector<std::string>& lines, bool tailslide)
    {
        std::vector<std::string> out;
        for (std::string line : lines)
        {
            replaceAll(line, "class [", "[");
            replaceAll(line, "valuetype [", "[");
            replaceAll(line, "( ", "(");
            replaceAll(line, " )", ")");
            replaceAll(line, " ,", ",");
            replaceAll(line, ",", ", ");
            replaceAll(line, ",  ", ", ");
            for (size_t at; (at = line.find("::'")) != std::string::npos;)
            {
                const size_t close = line.find('\'', at + 3);
                if (close == std::string::npos)
                {
                    break;
                }
                line.erase(close, 1);
                line.erase(at + 2, 1);
            }
            if (startsWith(line, "ldc.r8 "))
            {
                double value = 0.0;
                if (line[7] == '(')
                {
                    U8                 bytes[8] = {};
                    std::istringstream hex(line.substr(8));
                    for (U8& b : bytes)
                    {
                        unsigned int v = 0;
                        hex >> std::hex >> v;
                        b = static_cast<U8>(v);
                    }
                    memcpy(&value, bytes, sizeof(value));
                }
                else
                {
                    value = strtod(line.c_str() + 7, nullptr);
                }
                line = llformat("ldc.r8 %.17g", value);
            }
            const bool label = line.size() > 3 && line.front() == '\'' && line.back() == ':';
            const bool jump  = startsWith(line, "br ") || startsWith(line, "brtrue ") || startsWith(line, "brfalse ");
            if (tailslide && (label || jump))
            {
                replaceAll(line, "'ul", "'");
            }
            out.push_back(std::move(line));
        }
        return out;
    }

    // Each constant as LL's compiler writes it: an integer by ldc.i4 alone,
    // and a minus before a number folded into it -- LL's grammar reads `-1`
    // as one constant in places where Tailslide pushes 1 and negates it.
    // Folded on both sides, where LL's own negates a number.
    std::vector<std::string> asLLWrites(const std::vector<std::string>& lines)
    {
        std::vector<std::string> out;
        for (std::string line : lines)
        {
            if (line == "ldc.i4.m1")
            {
                line = "ldc.i4 -1";
            }
            else if (line.size() == 8 && startsWith(line, "ldc.i4.") && isdigit(line[7]))
            {
                line = std::string("ldc.i4 ") + line[7];
            }
            else if (startsWith(line, "ldc.i4.s "))
            {
                line = "ldc.i4 " + line.substr(9);
            }
            if (line == "neg" && !out.empty() && (startsWith(out.back(), "ldc.i4 ") || startsWith(out.back(), "ldc.r8 ")))
            {
                std::string& constant = out.back();
                const size_t number   = constant.find(' ') + 1;
                constant              = constant[number] == '-' ? constant.erase(number, 1) : constant.insert(number, "-");
                continue;
            }
            out.push_back(std::move(line));
        }
        return out;
    }

    // The bytes the constants of a text encode to: an ldc.i4 of four, a
    // short form of one or none, a double's eight, and a negation's byte.
    S32 constantBytes(const std::vector<std::string>& lines)
    {
        S32 bytes = 0;
        for (const std::string& line : lines)
        {
            bytes += startsWith(line, "ldc.i4.s ")                                         ? 2
                     : startsWith(line, "ldc.i4 ")                                         ? 5
                     : line == "ldc.i4.m1" || (line.size() == 8 && startsWith(line, "ldc.i4.")) ? 1
                     : startsWith(line, "ldc.r8 ")                                         ? 9
                     : line == "neg"                                                       ? 1
                                                                                           : 0;
        }
        return bytes;
    }

    std::string listed(const std::set<std::string>& names)
    {
        std::string out;
        for (const std::string& name : names)
        {
            out += (out.empty() ? "" : ", ") + name;
        }
        return out;
    }
}

namespace tut
{
    struct allslreferencecheck_data
    {
        ALLSLService lsl;
        bool         loaded = false;
        std::string  error;
        allslreferencecheck_data() { loaded = lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error); }
    };
    typedef test_group<allslreferencecheck_data> allslreferencecheck_group;
    typedef allslreferencecheck_group::object    allslreferencecheck_object;
    allslreferencecheck_group                    allslreferencecheck_instance("allslreferencecheck");

    template<> template<>
    void allslreferencecheck_object::test<1>()
    {
        set_test_name("LSO: Tailslide's image is LL's byte for byte, but for a list global copied from one never set; and what each refuses");
        ensure("builtins: " + error, loaded);
        // Tailslide's bit stream, moved to its end, grows a byte past it: a
        // last global left all zeros keeps the byte, and all after it moves.
        const std::set<std::string> PARTED = { "global_list_refs.lsl" };
        // LL's lets a list into a local's list, and takes print() as giving
        // a string; it reads `foo` after `@foo` as the label, which Tailslide
        // takes as the global with a warning.
        const std::set<std::string> ONLY_LL        = { "bugs/0019.lsl", "print_type_bug.lsl" };
        const std::set<std::string> ONLY_TAILSLIDE = { "label_shadowing.lsl" };
        std::set<std::string>       parted, only_ll, only_tailslide;
        size_t                      same = 0;
        std::string                 where;
        for (const auto& [name, text] : corpus())
        {
            std::vector<U8>              ours;
            std::string                  why;
            const bool                   tailslide = ALScriptWeigh::tailslideLSO(text, ours, why);
            const ALLSLReference::Result ll        = ALLSLReference::compile(text, Target::LSO);
            if (tailslide != ll.ok)
            {
                (tailslide ? only_tailslide : only_ll).insert(name);
                continue;
            }
            if (!tailslide)
            {
                continue;
            }
            if (ours == ll.image)
            {
                ++same;
                continue;
            }
            parted.insert(name);
            size_t at = 0;
            while (at < ours.size() && at < ll.image.size() && ours[at] == ll.image[at])
            {
                ++at;
            }
            where += name + " from byte " + std::to_string(at) + ", in its " + regionOf(ll.image, at) + "\n";
        }
        ensure("most of them: " + std::to_string(same), same >= 75);
        ensure("the images that part: " + listed(parted) + "\n" + where, parted == PARTED);
        ensure("LL's compiles alone: " + listed(only_ll), only_ll == ONLY_LL);
        ensure("Tailslide compiles alone: " + listed(only_tailslide), only_tailslide == ONLY_TAILSLIDE);
    }

    template<> template<>
    void allslreferencecheck_object::test<2>()
    {
        set_test_name("CIL: Tailslide's is LL's, method for method, once its constants are written as LL writes them");
        ensure("builtins: " + error, loaded);
        // What only LSO's image building refuses: a list in a global's list.
        const std::set<std::string> ONLY_LL        = { "bugs/0019.lsl", "nested_lists.lsl", "print_type_bug.lsl" };
        const std::set<std::string> ONLY_TAILSLIDE = { "label_shadowing.lsl" };
        // Where a minus stands before a number and LL's makes one constant
        // of it, and Tailslide pushes the number and negates it.
        const std::set<std::string> NEGATED = { "apotheus_vendor/main.lsl", "constprop.lsl", "infinity_repr.lsl", "irc-4.lsl",
                                                "libhttpdb.lsl",            "tltp/browser.lsl", "unixtime.lsl",  "xytext1.2.lsl" };
        std::set<std::string> only_ll, only_tailslide, negated, parted;
        size_t                both = 0;
        std::string           where;
        for (const auto& [name, text] : corpus())
        {
            std::string                  ours;
            std::string                  why;
            const bool                   tailslide = ALScriptWeigh::tailslideCIL(text, ours, why);
            const ALLSLReference::Result ll        = ALLSLReference::compile(text, Target::CIL);
            if (tailslide != ll.ok)
            {
                (tailslide ? only_tailslide : only_ll).insert(name);
                continue;
            }
            if (!tailslide)
            {
                continue;
            }
            ++both;
            const std::vector<std::string> theirs  = asLLWrites(spelt(instructionsOf(ll.cil), false));
            const std::vector<std::string> written = spelt(instructionsOf(ours), true);
            const std::vector<std::string> as_ll   = asLLWrites(written);
            if (as_ll != theirs)
            {
                parted.insert(name);
                size_t at = 0;
                while (at < as_ll.size() && at < theirs.size() && as_ll[at] == theirs[at])
                {
                    ++at;
                }
                where += name + ": [" + (at < as_ll.size() ? as_ll[at] : "") + "] against LL's [" + (at < theirs.size() ? theirs[at] : "") + "]\n";
            }
            // Negated where LL's has its constant: LL folds a minus before a
            // number in some places and not others.
            const std::vector<std::string> llwrote = spelt(instructionsOf(ll.cil), false);
            if (std::count(written.begin(), written.end(), "neg") != std::count(llwrote.begin(), llwrote.end(), "neg"))
            {
                negated.insert(name);
            }
        }
        ensure("most of them: " + std::to_string(both), both >= 75);
        ensure("the same as LL's once its constants are LL's:\n" + where, parted.empty());
        ensure("a minus before a number: " + listed(negated), negated == NEGATED);
        ensure("LL's compiles alone: " + listed(only_ll), only_ll == ONLY_LL);
        ensure("Tailslide compiles alone: " + listed(only_tailslide), only_tailslide == ONLY_TAILSLIDE);
    }

    template<> template<>
    void allslreferencecheck_object::test<3>()
    {
        set_test_name("Mono: the weigher reads either compiler's CIL alike, and LL's weighs more by its constants' forms alone");
        ensure("builtins: " + error, loaded);
        std::string wrong;
        size_t      both = 0;
        S32         more = 0;
        for (const auto& [name, text] : corpus())
        {
            std::string                  ours;
            std::string                  why;
            const ALLSLReference::Result ll = ALLSLReference::compile(text, Target::CIL);
            if (!ALScriptWeigh::tailslideCIL(text, ours, why) || !ll.ok)
            {
                continue;
            }
            ++both;
            const ALScriptWeight weighed  = ALScriptWeigh::mono(text);
            const ALScriptWeight tailside = ALScriptWeigh::monoOfCIL(ours);
            const ALScriptWeight theirs   = ALScriptWeigh::monoOfCIL(ll.cil);
            if (tailside.total != weighed.total)
            {
                wrong += name + ": Tailslide's CIL weighs " + std::to_string(tailside.total) + " as text and " + std::to_string(weighed.total) + " weighed\n";
            }
            const S32 forms = constantBytes(instructionsOf(ll.cil)) - constantBytes(instructionsOf(ours));
            const S32 apart = static_cast<S32>(theirs.total) - static_cast<S32>(tailside.total);
            if (apart != forms)
            {
                wrong += name + ": LL's weighs " + std::to_string(apart) + " more, its constants " + std::to_string(forms) + "\n";
            }
            more += apart;
        }
        ensure("most of them: " + std::to_string(both), both >= 75);
        ensure(wrong, wrong.empty());
        // LL writes a number by ldc.i4 alone, which the grid keeps as
        // written (its microthreaded IL loads `ldc.i4 1` in five bytes):
        // what the viewer's weights leave out until Tailslide writes them so.
        ensure("LL's heavier: " + std::to_string(more), more > 0);
    }
}
