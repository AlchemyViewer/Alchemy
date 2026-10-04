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
        set_test_name("LSO: Tailslide's image is LL's byte for byte; and what each refuses");
        ensure("builtins: " + error, loaded);
        // None, since the port's bitstream-move-to-end.patch: a bit stream
        // moved to its end grew a byte past it, which a last global left all
        // zeros kept, and all after it moved.
        const std::set<std::string> PARTED;
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
        set_test_name("CIL: Tailslide's is LL's, method for method, but for spelling an assembler reads alike");
        ensure("builtins: " + error, loaded);
        // What only LSO's image building refuses: a list in a global's list.
        // Its constants are LL's since the port's mono-constants-as-ll.patch:
        // an integer literal by ldc.i4 alone, and a minus before a number in
        // a global's initializer one constant.
        const std::set<std::string> ONLY_LL        = { "bugs/0019.lsl", "nested_lists.lsl", "print_type_bug.lsl" };
        const std::set<std::string> ONLY_TAILSLIDE = { "label_shadowing.lsl" };
        std::set<std::string>       only_ll, only_tailslide, parted;
        size_t                      both = 0;
        std::string                 where;
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
            const std::vector<std::string> theirs  = spelt(instructionsOf(ll.cil), false);
            const std::vector<std::string> written = spelt(instructionsOf(ours), true);
            if (written != theirs)
            {
                parted.insert(name);
                size_t at = 0;
                while (at < written.size() && at < theirs.size() && written[at] == theirs[at])
                {
                    ++at;
                }
                where += name + ": [" + (at < written.size() ? written[at] : "") + "] against LL's [" + (at < theirs.size() ? theirs[at] : "") + "]\n";
            }
        }
        ensure("most of them: " + std::to_string(both), both >= 75);
        ensure("the same as LL's:\n" + where, parted.empty());
        ensure("LL's compiles alone: " + listed(only_ll), only_ll == ONLY_LL);
        ensure("Tailslide compiles alone: " + listed(only_tailslide), only_tailslide == ONLY_TAILSLIDE);
    }

    template<> template<>
    void allslreferencecheck_object::test<3>()
    {
        set_test_name("Mono: the weigher reads either compiler's CIL alike, and both weigh the same");
        ensure("builtins: " + error, loaded);
        std::string wrong;
        size_t      both = 0;
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
            const ALScriptWeight weighed   = ALScriptWeigh::mono(text);
            const ALScriptWeight tailslide = ALScriptWeigh::monoOfCIL(ours);
            const ALScriptWeight theirs    = ALScriptWeigh::monoOfCIL(ll.cil);
            if (tailslide.total != weighed.total)
            {
                wrong += name + ": Tailslide's CIL weighs " + std::to_string(tailslide.total) + " as text and " + std::to_string(weighed.total) + " weighed\n";
            }
            // LL's numbers are what the grid keeps -- its microthreaded IL
            // loads `ldc.i4 1` in five bytes -- and Tailslide's are LL's.
            if (theirs.total != tailslide.total)
            {
                wrong += name + ": LL's weighs " + std::to_string(theirs.total) + ", Tailslide's " + std::to_string(tailslide.total) + "\n";
            }
        }
        ensure("most of them: " + std::to_string(both), both >= 75);
        ensure(wrong, wrong.empty());
    }

    template<> template<>
    void allslreferencecheck_object::test<4>()
    {
        set_test_name("integer literals past 32 bits as the grid's 32-bit hosts read them, -1, decimal or hexadecimal: the same image from either");
        ensure("builtins: " + error, loaded);
        const std::string text = "integer a = 4294967296;\n"
                                 "integer b = 0x100000000;\n"
                                 "integer c = 99999999999999999999999;\n"
                                 "integer d = 2147483648;\n"
                                 "default { state_entry() { llOwnerSay((string)(a + b + c + d + 4294967297 - 0x1FFFFFFFF)); } }\n";
        std::vector<U8> ours;
        std::string     why;
        ensure("Tailslide compiles it: " + why, ALScriptWeigh::tailslideLSO(text, ours, why));
        const ALLSLReference::Result ll = ALLSLReference::compile(text, Target::LSO);
        ensure("LL's compiles it", ll.ok);
        ensure("the same image", ours == ll.image);
    }
}
