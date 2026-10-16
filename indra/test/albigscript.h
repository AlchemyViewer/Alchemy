/**
 * @file albigscript.h
 * @brief Big scripts for tests and benchmarks: LSL and SLua of any length
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

#ifndef AL_ALBIGSCRIPT_H
#define AL_ALBIGSCRIPT_H

#include <string>

// A script as long as a test asks for, and the same every time: helper
// functions of about twenty lines each, called in turn from the script's
// entry, which check without a problem. Between them is what a lexer and
// a layout meet in real scripts -- line and block comments, strings with
// escapes, nested blocks, a long line now and then -- so that a number
// measured over one is a number about scripts, not about a line repeated.
//
// The count is of lines; the script is within a few lines of it, never
// over, and never less than one helper.
namespace ll_test
{
    namespace big_script_detail
    {
        inline int linesIn(const std::string& text)
        {
            int lines = 1;
            for (char c : text)
            {
                lines += c == '\n';
            }
            return lines;
        }
    }

    inline std::string bigLSL(int lines)
    {
        const std::string head = "// A script made for measuring: helpers, and a state that calls each.\n"
                                 "integer gTotal = 0;\n"
                                 "list gSeen = [];\n"
                                 "string gName = \"bench\";\n"
                                 "\n";
        std::string helpers;
        std::string calls;
        int         used = big_script_detail::linesIn(head) + 12;
        for (int n = 0;; ++n)
        {
            const std::string name = "helper" + std::to_string(n);
            std::string       one;
            if (n % 10 == 0)
            {
                one += "/* A block comment over lines, as a script's notes are:\n"
                       "   what " + name + " is for, and \"what it is not\". */\n";
            }
            one += "integer " + name + "(integer a, string b)\n"
                   "{\n"
                   "    // Counts the parts of b, and more for a long one.\n"
                   "    integer total = a + " + std::to_string(n) + ";\n"
                   "    if (llStringLength(b) > 3)\n"
                   "    {\n"
                   "        total += llStringLength(b) * 2;\n"
                   "        if (total > 100) { total = total % 100; }\n"
                   "    }\n"
                   "    else\n"
                   "    {\n"
                   "        total -= 1;\n"
                   "    }\n"
                   "    list parts = llParseString2List(b, [\",\", \";\"], [\"\\\\\"]);\n"
                   "    total += llGetListLength(parts);\n";
            if (n % 7 == 0)
            {
                one += "    gSeen += [\"" + name + "\", total, <1.0, 2.0, 3.0>, ZERO_ROTATION, \"a longer string than most, "
                       "with \\\"quotes\\\" and \\\\ in it, as a line of settings or a message to an owner would have\"];\n";
            }
            one += "    return total;\n"
                   "}\n"
                   "\n";
            const std::string call = "        gTotal += " + name + "(" + std::to_string(n) + ", \"a,b;c\");\n";
            const int         cost = big_script_detail::linesIn(one) - 1 + 1;
            if (n > 0 && used + cost > lines)
            {
                break;
            }
            helpers += one;
            calls += call;
            used += cost;
        }
        return head + helpers +
               "default\n"
               "{\n"
               "    state_entry()\n"
               "    {\n" +
               calls +
               "        llSetTimerEvent(1.0);\n"
               "    }\n"
               "\n"
               "    timer()\n"
               "    {\n"
               "        llOwnerSay(gName + \": \" + (string)gTotal);\n"
               "    }\n"
               "}\n";
    }

    inline std::string bigSLua(int lines)
    {
        const std::string head = "-- A script made for measuring: helpers, and a main that calls each.\n"
                                 "local total = 0\n"
                                 "local seen = {}\n"
                                 "local name = \"bench\"\n"
                                 "\n";
        std::string helpers;
        std::string calls;
        int         used = big_script_detail::linesIn(head) + 6;
        for (int n = 0;; ++n)
        {
            const std::string fn = "helper" + std::to_string(n);
            std::string       one;
            if (n % 10 == 0)
            {
                one += "--[[ A block comment over lines, as a script's notes are:\n"
                       "   what " + fn + " is for, and \"what it is not\". ]]\n";
            }
            one += "local function " + fn + "(a: number, b: string): number\n"
                   "    -- Counts the parts of b, and more for a long one.\n"
                   "    local count = a + " + std::to_string(n) + "\n"
                   "    if #b > 3 then\n"
                   "        count += #b * 2\n"
                   "        if count > 100 then count = count % 100 end\n"
                   "    else\n"
                   "        count -= 1\n"
                   "    end\n"
                   "    local parts = string.split(b, \",\")\n"
                   "    for _, part in parts do\n"
                   "        count += #part\n"
                   "    end\n";
            if (n % 7 == 0)
            {
                one += "    table.insert(seen, \"" + fn + ": a longer string than most, with \\\"quotes\\\" and \\\\ in it, "
                       "as a line of settings or a message to an owner would have\")\n";
            }
            one += "    return count\n"
                   "end\n"
                   "\n";
            const std::string call = "    total += " + fn + "(" + std::to_string(n) + ", \"a,b,c\")\n";
            const int         cost = big_script_detail::linesIn(one) - 1 + 1;
            if (n > 0 && used + cost > lines)
            {
                break;
            }
            helpers += one;
            calls += call;
            used += cost;
        }
        return head + helpers +
               "local function main()\n" +
               calls +
               "    print(name .. \": \" .. tostring(total))\n"
               "end\n"
               "\n"
               "main()\n";
    }
}

#endif // AL_ALBIGSCRIPT_H
